//
// Implements tcp_socket over the Arduino Client interface: WiFiClient for
// plain TCP and FreeInk's SecureClient for TLS. The same code runs on the
// X4 (real Arduino and FreeInk) and on the host (the shim in host/arduino
// and an OpenSSL SecureClient).
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/network.h>
#include <inkerface/socket.h>

#include "net_time.h"

#include <Arduino.h>
#include <Client.h>
#include <SecureClient.h>
#include <WiFiClient.h>

#include <algorithm>
#include <string>
#include <utility>

namespace inkerface::detail {

// One connection. Both clients are members so no heap object or virtual
// construction is needed; active points at the one in use.
class socket_backend {
public:
    WiFiClient plain;
    freeink::SecureClient secure;
    Client *active = nullptr;
};

void net_deleter::operator()(socket_backend *backend) const noexcept {
    if (backend != nullptr && backend->active != nullptr) {
        backend->active->stop();
    }
    delete backend;
}

} // namespace inkerface::detail

namespace inkerface {

tcp_socket::tcp_socket(network &link,
                       std::string_view host,
                       std::uint16_t port,
                       const socket_options &options)
    : timeout_(options.timeout) {
    if (!link.connected()) {
        error_ = net_error::not_connected;
        return;
    }
    if (host.empty() || port == 0) {
        error_ = net_error::invalid_address;
        return;
    }
    const std::string name(host);
    // Arduino-ESP32 waits out the whole connect timeout on a failed name
    // lookup; FreeInk's check fails fast instead.
    if (!freeink::hostResolves(name.c_str())) {
        error_ = net_error::resolve;
        return;
    }
    detail::socket_handle connection(new detail::socket_backend());
    const std::uint32_t wait = detail::to_milliseconds(options.timeout);
    if (options.tls) {
        if (!freeink::SecureClient::tls13Available()) {
            error_ = net_error::tls_unavailable;
            return;
        }
        if (options.insecure) {
            connection->secure.setInsecure();
        } else {
            connection->secure.setCACert(options.ca_certificate);
        }
        connection->secure.setTimeout(wait);
        if (connection->secure.connect(name.c_str(), port) == 0) {
            error_ = net_error::connect;
            return;
        }
        connection->active = &connection->secure;
    } else {
        connection->plain.setTimeout(wait);
        connection->plain.setConnectionTimeout(wait);
        if (connection->plain.connect(name.c_str(), port) == 0) {
            error_ = net_error::connect;
            return;
        }
        connection->active = &connection->plain;
    }
    backend_ = std::move(connection);
}

tcp_socket::~tcp_socket() = default;

tcp_socket::tcp_socket(tcp_socket &&other) noexcept
    : backend_(std::move(other.backend_))
    , error_(other.error_)
    , timeout_(other.timeout_) {}

tcp_socket &tcp_socket::operator=(tcp_socket &&other) noexcept {
    if (this != &other) {
        backend_ = std::move(other.backend_);
        error_ = other.error_;
        timeout_ = other.timeout_;
    }
    return *this;
}

bool tcp_socket::is_open() const {
    return backend_ != nullptr;
}

net_error tcp_socket::error() const {
    return error_;
}

tcp_socket::operator bool() const {
    return is_open() && error_ == net_error::none;
}

std::size_t tcp_socket::write(std::span<const std::byte> in) {
    if (!*this || in.empty()) {
        return 0;
    }
    // Client takes uint8_t; std::byte and uint8_t share representation.
    const auto *data = reinterpret_cast<const std::uint8_t *>(in.data());
    const std::size_t sent = backend_->active->write(data, in.size());
    if (sent != in.size()) {
        error_ = net_error::connect;
        close();
    }
    return sent;
}

std::size_t tcp_socket::write(std::string_view text) {
    return write(std::as_bytes(std::span(text.data(), text.size())));
}

std::size_t tcp_socket::read(std::span<std::byte> out) {
    return read(out, timeout_);
}

std::size_t tcp_socket::read(std::span<std::byte> out,
                             std::chrono::milliseconds wait) {
    if (!*this || out.empty()) {
        return 0;
    }
    Client &client = *backend_->active;
    auto *data = reinterpret_cast<std::uint8_t *>(out.data());
    const detail::deadline until(wait);
    while (true) {
        const int ready = client.available();
        if (ready > 0) {
            const std::size_t want =
                std::min(out.size(), static_cast<std::size_t>(ready));
            const int got = client.read(data, want);
            if (got > 0) {
                return static_cast<std::size_t>(got);
            }
            if (got < 0 && !client.connected()) {
                error_ = net_error::connect;
                close();
                return 0;
            }
        } else if (!client.connected()) {
            error_ = net_error::closed;
            close();
            return 0;
        }
        if (until.passed()) {
            return 0; // timeout: not an error, the socket stays open
        }
        delay(1);
    }
}

std::size_t tcp_socket::available() {
    if (!*this) {
        return 0;
    }
    const int ready = backend_->active->available();
    return ready > 0 ? static_cast<std::size_t>(ready) : 0;
}

void tcp_socket::close() {
    backend_.reset();
}

} // namespace inkerface
