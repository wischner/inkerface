//
// Implements udp_socket over a BSD datagram socket. Broadcast
// destinations come from the platform (net_platform.h), because only it
// knows which networks the device is on.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/network.h>
#include <inkerface/socket.h>

#include "descriptor.h"
#include "net_platform.h"

#include <string>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace inkerface {

namespace {

// Send one datagram to an address in network byte order.
bool send_datagram(int descriptor,
                   std::uint32_t address,
                   std::uint16_t port,
                   std::span<const std::byte> data) {
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = address;
    const ssize_t sent =
        ::sendto(descriptor, data.data(), data.size(), 0,
                 reinterpret_cast<const sockaddr *>(&target), sizeof(target));
    return sent >= 0 && static_cast<std::size_t>(sent) == data.size();
}

} // namespace

udp_socket::udp_socket(network &link, std::uint16_t port)
    : link_(&link) {
    if (!link.connected() && !link.hosting()) {
        error_ = net_error::not_connected;
        return;
    }
    const int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket < 0) {
        error_ = net_error::bind;
        return;
    }
    const int on = 1;
    static_cast<void>(
        ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)));
    // Without this the system refuses every send to a broadcast address.
    static_cast<void>(
        ::setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)));
    if (!detail::bind_any(socket, port) ||
        !detail::make_non_blocking(socket)) {
        ::close(socket);
        error_ = net_error::bind;
        return;
    }
    descriptor_ = socket;
}

udp_socket::~udp_socket() {
    close();
}

udp_socket::udp_socket(udp_socket &&other) noexcept
    : link_(other.link_)
    , descriptor_(std::exchange(other.descriptor_, -1))
    , error_(other.error_) {}

udp_socket &udp_socket::operator=(udp_socket &&other) noexcept {
    if (this != &other) {
        close();
        link_ = other.link_;
        descriptor_ = std::exchange(other.descriptor_, -1);
        error_ = other.error_;
    }
    return *this;
}

bool udp_socket::is_open() const {
    return descriptor_ >= 0;
}

net_error udp_socket::error() const {
    return error_;
}

udp_socket::operator bool() const {
    return is_open() && error_ == net_error::none;
}

std::uint16_t udp_socket::port() const {
    return is_open() ? detail::local_port(descriptor_) : std::uint16_t{0};
}

bool udp_socket::send_to(std::string_view address,
                         std::uint16_t port,
                         std::span<const std::byte> data) {
    if (!*this || port == 0) {
        return false;
    }
    const std::string text(address);
    in_addr parsed{};
    if (::inet_pton(AF_INET, text.c_str(), &parsed) != 1) {
        return false;
    }
    return send_datagram(descriptor_, parsed.s_addr, port, data);
}

bool udp_socket::broadcast(std::uint16_t port,
                           std::span<const std::byte> data) {
    if (!*this || port == 0) {
        return false;
    }
    bool any = false;
    for (const std::uint32_t address : detail::broadcast_addresses(*link_)) {
        any = send_datagram(descriptor_, address, port, data) || any;
    }
    return any;
}

std::size_t udp_socket::receive(std::span<std::byte> out,
                                datagram_source &from,
                                std::chrono::milliseconds wait) {
    if (!*this || out.empty() || !detail::wait_readable(descriptor_, wait)) {
        return 0;
    }
    sockaddr_in sender{};
    socklen_t size = sizeof(sender);
    const ssize_t got =
        ::recvfrom(descriptor_, out.data(), out.size(), 0,
                   reinterpret_cast<sockaddr *>(&sender), &size);
    if (got <= 0) {
        return 0;
    }
    char text[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &sender.sin_addr, text, sizeof(text)) == nullptr) {
        return 0;
    }
    from.address = text;
    from.port = ntohs(sender.sin_port);
    return static_cast<std::size_t>(got);
}

void udp_socket::close() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
}

} // namespace inkerface
