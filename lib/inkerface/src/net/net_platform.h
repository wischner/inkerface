//
// Declares what the shared socket code needs from each platform's
// network link: where a broadcast should go. Defined in host/network.cpp
// and x4/network.cpp.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <inkerface/network.h>

#include <cstdint>
#include <vector>

namespace inkerface::detail {

//
// List the IPv4 addresses a broadcast should be sent to.
//
// Parameters:
//     link - The network the broadcast goes out on.
//
// Returns:
//     Addresses in network byte order, without repeats. Empty when the
//     link has no network to broadcast on.
//
std::vector<std::uint32_t> broadcast_addresses(const network &link);

} // namespace inkerface::detail
