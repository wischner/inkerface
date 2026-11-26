//
// Implements the host network link. The operating system's network is
// always used, so connect() succeeds at once and ignores the credentials;
// disconnect() only marks the link down, which lets programs exercise
// their not-connected paths on the host.
//
// The host has no radio. scan() returns a fixed list, and an access point
// is emulated: nothing is created, the address reported is this machine's
// own LAN address, and other devices reach the program over the network
// the machine is already on. That is enough to develop a setup page and
// try it from a real phone.
//
// SIGPIPE is ignored for the process once a network exists: OpenSSL
// writes to sockets with write(), and a peer that closed mid-write would
// otherwise terminate the program instead of reporting an error.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/network.h>

#include "../net/net_platform.h"

#include <algorithm>
#include <csignal>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

namespace inkerface {

namespace {

// WPA2 password limits; a non-empty password outside them is refused, as
// the device's radio would.
constexpr std::size_t shortest_password = 8;
constexpr std::size_t longest_password = 63;
constexpr std::size_t longest_name = 32;

// Return this machine's first IPv4 address that is not loopback.
std::string lan_address() {
    std::string found = "127.0.0.1";
    ifaddrs *interfaces = nullptr;
    if (::getifaddrs(&interfaces) != 0) {
        return found;
    }
    for (const ifaddrs *entry = interfaces; entry != nullptr;
         entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr ||
            entry->ifa_addr->sa_family != AF_INET ||
            (entry->ifa_flags & IFF_UP) == 0U ||
            (entry->ifa_flags & IFF_LOOPBACK) != 0U) {
            continue;
        }
        char text[INET_ADDRSTRLEN] = {};
        const auto *address =
            reinterpret_cast<const sockaddr_in *>(entry->ifa_addr);
        if (::inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) !=
            nullptr) {
            found = text;
            break;
        }
    }
    ::freeifaddrs(interfaces);
    return found;
}

} // namespace

namespace detail {

std::vector<std::uint32_t> broadcast_addresses(const network &link) {
    std::vector<std::uint32_t> out;
    if (!link.connected() && !link.hosting()) {
        return out;
    }
    // Loopback first, so programs on this machine hear the broadcast even
    // with no network at all.
    out.push_back(htonl(INADDR_LOOPBACK));

    ifaddrs *interfaces = nullptr;
    if (::getifaddrs(&interfaces) == 0) {
        for (const ifaddrs *entry = interfaces; entry != nullptr;
             entry = entry->ifa_next) {
            if ((entry->ifa_flags & IFF_UP) == 0U ||
                (entry->ifa_flags & IFF_BROADCAST) == 0U ||
                (entry->ifa_flags & IFF_LOOPBACK) != 0U ||
                entry->ifa_broadaddr == nullptr ||
                entry->ifa_broadaddr->sa_family != AF_INET) {
                continue;
            }
            const std::uint32_t address =
                reinterpret_cast<const sockaddr_in *>(entry->ifa_broadaddr)
                    ->sin_addr.s_addr;
            if (std::find(out.begin(), out.end(), address) == out.end()) {
                out.push_back(address);
            }
        }
        ::freeifaddrs(interfaces);
    }
    // A plain 255.255.255.255 leaves through one interface only, so it is
    // the fallback, not the default.
    if (out.size() == 1) {
        out.push_back(htonl(INADDR_BROADCAST));
    }
    return out;
}

} // namespace detail

network::network() {
    static_cast<void>(std::signal(SIGPIPE, SIG_IGN));
}

network::~network() = default;

bool network::connect(std::string_view /*ssid*/,
                      std::string_view /*password*/,
                      std::chrono::milliseconds /*timeout*/) {
    hosting_ = false;
    joined_ = true;
    return true;
}

void network::disconnect() {
    joined_ = false;
    hosting_ = false;
}

std::vector<wifi_network> network::scan() {
    // Emulated: the host has no radio to scan with.
    return {
        wifi_network{"Home", -48, true},
        wifi_network{"Office", -61, true},
        wifi_network{"Guest", -72, false},
    };
}

std::optional<access_point>
network::start_access_point(std::string_view name, std::string_view password) {
    if (name.empty() || name.size() > longest_name ||
        (!password.empty() && (password.size() < shortest_password ||
                               password.size() > longest_password))) {
        return std::nullopt;
    }
    joined_ = false;
    hosting_ = true;
    return access_point{lan_address(), true};
}

void network::stop_access_point() {
    hosting_ = false;
}

bool network::hosting() const {
    return hosting_;
}

bool network::connected() const {
    return joined_;
}

} // namespace inkerface
