//
// Implements the BSD socket helpers shared by tcp_listener and
// udp_socket.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "descriptor.h"

#include <algorithm>
#include <climits>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>

namespace inkerface::detail {

bool wait_readable(int descriptor, std::chrono::milliseconds wait) {
    const auto clamped =
        std::clamp<std::chrono::milliseconds::rep>(wait.count(), 0, INT_MAX);
    pollfd watched{};
    watched.fd = descriptor;
    watched.events = POLLIN;
    return ::poll(&watched, 1, static_cast<int>(clamped)) > 0 &&
           (watched.revents & POLLIN) != 0;
}

bool make_non_blocking(int descriptor) {
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 && ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool bind_any(int descriptor, std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    return ::bind(descriptor,
                  reinterpret_cast<const sockaddr *>(&address),
                  sizeof(address)) == 0;
}

std::uint16_t local_port(int descriptor) {
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    if (::getsockname(descriptor, reinterpret_cast<sockaddr *>(&address),
                      &size) != 0) {
        return 0;
    }
    return ntohs(address.sin_port);
}

} // namespace inkerface::detail
