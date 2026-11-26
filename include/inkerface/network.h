//
// Declares the network link (Wi-Fi on the device) and the errors every
// network operation reports. Sockets and HTTP requests are made through
// socket.h and http.h, and both need a connected network. The link can
// also scan for networks and become an access point of its own, which is
// how a device with no keyboard is set up from a phone.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace inkerface {

namespace detail {
class socket_backend;
class http_backend;

// Destroys network backend objects; defined by the platform layer.
struct net_deleter {
    void operator()(socket_backend *backend) const noexcept;
    void operator()(http_backend *backend) const noexcept;
};

using socket_handle = std::unique_ptr<socket_backend, net_deleter>;
using http_handle = std::unique_ptr<http_backend, net_deleter>;
} // namespace detail

// Why a network operation failed. none means success.
enum class net_error : std::uint8_t {
    none,
    // The network link is down; call network::connect() first.
    not_connected,
    // Malformed URL, empty host or port 0.
    invalid_address,
    // The host name could not be resolved.
    resolve,
    // The server could not be reached, refused, failed the TLS handshake
    // or certificate check, or the connection broke.
    connect,
    // TLS was requested, but this build has no TLS support.
    tls_unavailable,
    // The peer closed the connection.
    closed,
    // The caller's cancel check or body sink stopped the transfer.
    cancelled,
    // The response body exceeded http_request::max_body_bytes.
    too_large,
    // The response body ended early.
    incomplete,
    // A local port could not be taken: it is in use, or the program may
    // not use it (ports below 1024 on the host).
    bind
};

// One network a scan found.
struct wifi_network {
    // Network name.
    std::string ssid{};
    // Received signal strength in dBm: about -50 is excellent, below -80
    // barely usable.
    int rssi = -100;
    // True when the network asks for a password.
    bool secured = true;
};

// The program's own Wi-Fi network, while it is one.
struct access_point {
    // This device's address on its own network, such as "192.168.4.1".
    std::string address{};
    // True on the host, which creates no network: address is the
    // machine's own LAN address and other devices reach it over the
    // network the machine is already on. Servers there should use ports
    // above 1023.
    bool emulated = false;
};

// The program's network link.
//
// Notes:
//     On the X4 this is the Wi-Fi radio: connect() joins a network and
//     disconnect() switches the radio off, which saves the most power.
//     On the host the operating system's network is used; connect()
//     succeeds immediately and ignores the credentials.
//     Exactly one network should exist.
//     Thread-safety: not thread-safe.
class network {
public:
    network();
    ~network();

    network(const network &) = delete;
    network &operator=(const network &) = delete;
    network(network &&) = delete;
    network &operator=(network &&) = delete;

    //
    // Join a Wi-Fi network, waiting at most timeout.
    //
    // Parameters:
    //     ssid     - Network name.
    //     password - Network password; empty for an open network.
    //     timeout  - Longest time to wait for the connection.
    //
    // Returns:
    //     True when connected. Credentials are not stored or logged.
    //
    [[nodiscard]] bool
    connect(std::string_view ssid,
            std::string_view password,
            std::chrono::milliseconds timeout = std::chrono::seconds(15));

    // Leave the network and switch the radio off.
    void disconnect();

    // Return true while the link is up.
    [[nodiscard]] bool connected() const;

    //
    // Look for networks in range.
    //
    // Returns:
    //     The networks found, strongest first; empty when none were
    //     found or the radio could not scan.
    //
    // Notes:
    //     Blocks for a few seconds on the device while the radio listens
    //     on every channel. The host has no radio and returns a fixed,
    //     emulated list.
    //
    [[nodiscard]] std::vector<wifi_network> scan();

    //
    // Become a Wi-Fi access point that other devices can join.
    //
    // Parameters:
    //     name     - Network name to broadcast, at most 32 bytes.
    //     password - WPA2 password, 8 to 63 characters; empty for an open
    //                network.
    //
    // Returns:
    //     The access point, or std::nullopt when it could not start.
    //
    // Notes:
    //     Leaves any joined network first. While hosting, udp_socket and
    //     tcp_listener work on the access point's own network, so a phone
    //     that joins can be served. connect() ends hosting. The password
    //     is not stored or logged.
    //
    [[nodiscard]] std::optional<access_point>
    start_access_point(std::string_view name, std::string_view password);

    // Stop being an access point and switch the radio off. Harmless when
    // not hosting.
    void stop_access_point();

    // Return true while this program is an access point.
    [[nodiscard]] bool hosting() const;

private:
    bool joined_ = false;
    bool hosting_ = false;
};

} // namespace inkerface
