//
// Declares HTTP(S) requests for web APIs: a request description, the
// response, and http_client, which keeps the connection to a server open
// between requests so a series of calls pays for one connection setup.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <inkerface/network.h>

namespace inkerface {

// The request method.
enum class http_method : std::uint8_t {
    get,
    post,
    put,
    // DELETE (named remove because delete is a C++ keyword).
    remove
};

// One header line.
struct http_header {
    std::string name{};
    std::string value{};
};

// What to send.
struct http_request {
    http_method method = http_method::get;
    // http:// or https:// URL.
    std::string url{};
    std::vector<http_header> headers{};
    // Sent with a Content-Length header when not empty.
    std::string body{};
    // Longest wait to connect, and between received bytes.
    std::chrono::milliseconds timeout{15'000};
    // Largest body accepted into http_response::body. Streamed bodies are
    // not limited here; the sink decides.
    std::size_t max_body_bytes = 64 * 1024;
    // Redirects followed before the 3xx response itself is returned.
    std::uint8_t max_redirects = 3;
    // As in socket_options: PEM root certificate, borrowed.
    const char *ca_certificate = nullptr;
    // As in socket_options: testing only.
    bool insecure = false;
};

// What came back.
struct http_response {
    // none when a response was received, whatever its status.
    net_error error = net_error::none;
    // HTTP status, such as 200 or 404; 0 when nothing was received.
    int status = 0;
    // Response headers; names are lower case.
    std::vector<http_header> headers{};
    // The body, unless it was streamed to a sink.
    std::string body{};

    // Return true for a complete 2xx response.
    [[nodiscard]] bool ok() const;

    // Return a header's value (name compared without case), or "".
    [[nodiscard]] std::string_view header(std::string_view name) const;
};

// Receives a streamed body chunk by chunk; return false to stop.
using body_sink = std::function<bool(std::span<const std::byte> chunk)>;

// Polled while a request runs; return true to cancel it.
using cancel_check = std::function<bool()>;

// Sends HTTP requests.
//
// Notes:
//     Requests block until the response is complete, the timeout passes
//     or the cancel check returns true. The connection to the last server
//     stays open for the next request; close() releases it.
//     Thread-safety: not thread-safe.
class http_client {
public:
    // Create a client; link must outlive it.
    explicit http_client(network &link);
    ~http_client();

    http_client(const http_client &) = delete;
    http_client &operator=(const http_client &) = delete;
    http_client(http_client &&) = delete;
    http_client &operator=(http_client &&) = delete;

    // Send a request and collect the body into the response.
    [[nodiscard]] http_response send(const http_request &request,
                                     const cancel_check &cancel = {});

    // Send a request and stream the body to sink instead.
    [[nodiscard]] http_response send(const http_request &request,
                                     const body_sink &sink,
                                     const cancel_check &cancel = {});

    // Close the kept-open connection (saves power and memory).
    void close();

private:
    network *link_;
    detail::http_handle backend_;
};

} // namespace inkerface
