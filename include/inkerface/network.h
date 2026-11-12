//
// Declares the network link (Wi-Fi on the device) and the errors every
// network operation reports. Sockets and HTTP requests are made through
// socket.h and http.h, and both need a connected network.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>

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
    incomplete
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

private:
    bool joined_ = false;
};

} // namespace inkerface
