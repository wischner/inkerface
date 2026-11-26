//
// Implements tcp_listener over a BSD socket. An accepted connection is
// handed to a WiFiClient, the same client tcp_socket uses for outgoing
// connections, so reading and writing it is the existing tcp_socket code.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/network.h>
#include <inkerface/socket.h>

#include "descriptor.h"
#include "socket_backend.h"

#include <utility>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace inkerface {

namespace {

// Connections the system may queue before accept() takes them.
constexpr int backlog = 4;

} // namespace

tcp_listener::tcp_listener(network &link,
                           std::uint16_t port,
                           std::chrono::milliseconds timeout)
    : timeout_(timeout) {
    if (!link.connected() && !link.hosting()) {
        error_ = net_error::not_connected;
        return;
    }
    if (port == 0) {
        error_ = net_error::invalid_address;
        return;
    }
    const int listening = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listening < 0) {
        error_ = net_error::bind;
        return;
    }
    const int reuse = 1;
    static_cast<void>(::setsockopt(listening, SOL_SOCKET, SO_REUSEADDR, &reuse,
                                   sizeof(reuse)));
    // Non-blocking, so a connection that vanishes between poll() and
    // accept() cannot stall the program.
    if (!detail::bind_any(listening, port) ||
        ::listen(listening, backlog) != 0 ||
        !detail::make_non_blocking(listening)) {
        ::close(listening);
        error_ = net_error::bind;
        return;
    }
    descriptor_ = listening;
}

tcp_listener::~tcp_listener() {
    close();
}

tcp_listener::tcp_listener(tcp_listener &&other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1))
    , error_(other.error_)
    , timeout_(other.timeout_) {}

tcp_listener &tcp_listener::operator=(tcp_listener &&other) noexcept {
    if (this != &other) {
        close();
        descriptor_ = std::exchange(other.descriptor_, -1);
        error_ = other.error_;
        timeout_ = other.timeout_;
    }
    return *this;
}

bool tcp_listener::is_open() const {
    return descriptor_ >= 0;
}

net_error tcp_listener::error() const {
    return error_;
}

tcp_listener::operator bool() const {
    return is_open() && error_ == net_error::none;
}

tcp_socket tcp_listener::accept(std::chrono::milliseconds wait) {
    if (!*this || !detail::wait_readable(descriptor_, wait)) {
        return tcp_socket(nullptr, timeout_);
    }
    const int peer = ::accept(descriptor_, nullptr, nullptr);
    if (peer < 0) {
        return tcp_socket(nullptr, timeout_);
    }
    return tcp_socket(detail::socket_handle(new detail::socket_backend(peer)),
                      timeout_);
}

void tcp_listener::close() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
}

} // namespace inkerface
