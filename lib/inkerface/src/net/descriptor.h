//
// Declares small helpers over BSD socket descriptors, shared by
// tcp_listener and udp_socket. glibc provides the calls on the host and
// lwIP on the device, so this code is the same on both.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <chrono>
#include <cstdint>

namespace inkerface::detail {

// Return true when the descriptor becomes readable within wait. A wait
// of zero or less only looks.
[[nodiscard]] bool wait_readable(int descriptor, std::chrono::milliseconds wait);

// Put a descriptor into non-blocking mode; false when that failed.
[[nodiscard]] bool make_non_blocking(int descriptor);

// Bind a descriptor to a port on every IPv4 interface.
[[nodiscard]] bool bind_any(int descriptor, std::uint16_t port);

// Return the local port a descriptor is bound to, or 0.
[[nodiscard]] std::uint16_t local_port(int descriptor);

} // namespace inkerface::detail
