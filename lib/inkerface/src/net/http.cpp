//
// Implements http_client on FreeInk's SecureHttpClient, which does the
// HTTP/1.1 work: request framing, redirects, chunked and fixed-length
// bodies, keep-alive reuse and cancellation. This file only translates
// between inkerface types and FreeInk's, enforces the body limit, and
// rejects header values that could inject extra header lines.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/http.h>
#include <inkerface/network.h>

#include "net_time.h"

#include <SecureClient.h>
#include <SecureHttpClient.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace inkerface::detail {

// One FreeInk client, kept for connection reuse, plus the verification
// settings its open connection was made with.
class http_backend {
public:
    freeink::SecureHttpClient client;
    bool insecure = false;
    const char *ca_certificate = nullptr;
};

void net_deleter::operator()(http_backend *backend) const noexcept {
    delete backend;
}

} // namespace inkerface::detail

namespace inkerface {

namespace {

bool equal_without_case(std::string_view left, std::string_view right) {
    const auto same = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    };
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), same);
}

bool starts_with_without_case(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           equal_without_case(text.substr(0, prefix.size()), prefix);
}

// A line break in a header would start a new header or the body.
bool safe_header(const http_header &header) {
    const auto unsafe = [](char c) { return c == '\r' || c == '\n'; };
    return !header.name.empty() &&
           std::none_of(header.name.begin(), header.name.end(), unsafe) &&
           std::none_of(header.value.begin(), header.value.end(), unsafe);
}

const char *method_name(http_method method) {
    switch (method) {
    case http_method::get:
        return "GET";
    case http_method::post:
        return "POST";
    case http_method::put:
        return "PUT";
    case http_method::remove:
        return "DELETE";
    }
    return "GET";
}

// Run one request, streaming the body to on_data.
http_response transfer(network &link,
                       detail::http_backend &state,
                       const http_request &request,
                       const freeink::SecureHttpClient::DataCallback &on_data,
                       const cancel_check &cancel) {
    http_response response;
    if (!link.connected()) {
        response.error = net_error::not_connected;
        return response;
    }
    const std::vector<http_header> &headers = request.headers;
    if (!std::all_of(headers.begin(), headers.end(), safe_header)) {
        response.error = net_error::invalid_address;
        return response;
    }
    if (starts_with_without_case(request.url, "https://") &&
        !freeink::SecureClient::tls13Available()) {
        response.error = net_error::tls_unavailable;
        return response;
    }

    freeink::SecureHttpClient &client = state.client;
    // Never reuse a connection made under weaker verification settings.
    if (state.insecure != request.insecure ||
        state.ca_certificate != request.ca_certificate) {
        client.end();
        state.insecure = request.insecure;
        state.ca_certificate = request.ca_certificate;
    }
    if (request.insecure) {
        client.setInsecure();
    } else {
        client.setCACert(request.ca_certificate); // also clears insecure
    }
    client.setTimeout(detail::to_milliseconds(request.timeout));
    client.setFollowRedirects(request.max_redirects);
    client.setUserAgent("inkerface/0.1");
    if (!client.begin(request.url)) {
        response.error = net_error::invalid_address;
        return response;
    }
    for (const http_header &header : request.headers) {
        client.addHeader(header.name, header.value);
    }

    freeink::SecureHttpClient::AbortCallback abort;
    if (cancel) {
        abort = [&cancel]() { return cancel(); };
    }
    // FreeInk takes uint8_t; char and uint8_t share representation.
    const auto *payload =
        reinterpret_cast<const std::uint8_t *>(request.body.data());
    const int status =
        client.sendRequest(method_name(request.method),
                           request.body.empty() ? nullptr : payload,
                           request.body.size(),
                           on_data,
                           abort);
    for (auto &[name, value] : client.getHeaders()) {
        response.headers.push_back(http_header{name, value});
    }
    if (status < 0) {
        const bool stopped = client.aborted() || client.callbackAborted();
        response.error = stopped ? net_error::cancelled : net_error::connect;
        return response;
    }
    response.status = status;
    if (!client.responseComplete()) {
        const bool stopped = client.aborted() || client.callbackAborted();
        response.error = stopped ? net_error::cancelled : net_error::incomplete;
    }
    return response;
}

} // namespace

bool http_response::ok() const {
    return error == net_error::none && status >= 200 && status < 300;
}

std::string_view http_response::header(std::string_view name) const {
    for (const http_header &line : headers) {
        if (equal_without_case(line.name, name)) {
            return line.value;
        }
    }
    return {};
}

http_client::http_client(network &link)
    : link_(&link)
    , backend_(new detail::http_backend()) {}

http_client::~http_client() = default;

http_response http_client::send(const http_request &request,
                                const cancel_check &cancel) {
    std::string body;
    bool too_large = false;
    const auto collect = [&](const std::uint8_t *data, std::size_t length) {
        if (body.size() + length > request.max_body_bytes) {
            too_large = true;
            return false;
        }
        body.append(reinterpret_cast<const char *>(data), length);
        return true;
    };
    http_response response =
        transfer(*link_, *backend_, request, collect, cancel);
    if (too_large) {
        response.error = net_error::too_large;
    }
    response.body = std::move(body);
    return response;
}

http_response http_client::send(const http_request &request,
                                const body_sink &sink,
                                const cancel_check &cancel) {
    const auto forward = [&sink](const std::uint8_t *data, std::size_t length) {
        return sink(std::as_bytes(std::span(data, length)));
    };
    return transfer(*link_, *backend_, request, forward, cancel);
}

void http_client::close() {
    backend_->client.end();
}

} // namespace inkerface
