//
// Declares tcp_socket: a TCP connection, optionally secured with TLS, for
// protocols other than HTTP. Reads wait at most the socket's timeout, so
// a program never blocks indefinitely. Also tcp_listener, which accepts
// connections from other devices, and udp_socket, for datagrams and for
// finding other devices on the local network by broadcast.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <inkerface/network.h>

namespace inkerface {

// How a socket connects.
struct socket_options {
    // Secure the connection with TLS.
    bool tls = false;
    // Longest wait to connect, and for each read.
    std::chrono::milliseconds timeout{10'000};
    // PEM root certificate that must have signed the server's chain;
    // borrowed, must outlive the socket. When null, the host uses the
    // system certificates; the X4 has none and refuses the server.
    const char *ca_certificate = nullptr;
    // Skip certificate verification. For testing only: anyone on the
    // path can then read and change the traffic.
    bool insecure = false;
};

// A connected TCP (or TLS) stream.
//
// Notes:
//     Failures are sticky: the first one is kept in error(), the socket
//     closes, and later calls do nothing. A read that times out is not a
//     failure; it returns 0 and the socket stays open.
//     Thread-safety: not thread-safe.
class tcp_socket {
    friend class tcp_listener;

public:
    //
    // Connect to host:port.
    //
    // Parameters:
    //     link    - Network; must be connected and outlive the socket.
    //     host    - Host name or IP address.
    //     port    - Port, 1 to 65535.
    //     options - TLS and timeout settings.
    //
    // Notes:
    //     On failure the socket is closed and error() says why.
    //
    tcp_socket(network &link,
               std::string_view host,
               std::uint16_t port,
               const socket_options &options = {});
    ~tcp_socket();

    tcp_socket(const tcp_socket &) = delete;
    tcp_socket &operator=(const tcp_socket &) = delete;
    // A moved-from socket is closed.
    tcp_socket(tcp_socket &&other) noexcept;
    tcp_socket &operator=(tcp_socket &&other) noexcept;

    // Return true while the connection is open.
    [[nodiscard]] bool is_open() const;

    // Return the first failure, or net_error::none.
    [[nodiscard]] net_error error() const;

    // Return true when open and nothing has failed.
    explicit operator bool() const;

    //
    // Send bytes.
    //
    // Returns:
    //     Bytes sent; fewer than in.size() means a failure (see error()).
    //
    [[nodiscard]] std::size_t write(std::span<const std::byte> in);

    // Send text; same as write() of its bytes.
    [[nodiscard]] std::size_t write(std::string_view text);

    //
    // Receive bytes, waiting up to the timeout for the first one.
    //
    // Returns:
    //     Bytes received, at most out.size(). 0 means the timeout passed
    //     (error() is none), the peer closed (net_error::closed) or the
    //     connection failed.
    //
    [[nodiscard]] std::size_t read(std::span<std::byte> out);

    //
    // Receive bytes, waiting at most wait for the first one.
    //
    // Returns:
    //     As read(out). With wait 0 it never waits: it returns the bytes
    //     already received, or 0, and still notices a closed connection.
    //     Use it to poll a socket from a main loop.
    //
    [[nodiscard]] std::size_t read(std::span<std::byte> out,
                                   std::chrono::milliseconds wait);

    // Return how many bytes can be read without waiting.
    [[nodiscard]] std::size_t available();

    // Close the connection. Closing twice is harmless.
    void close();

private:
    // Adopt an accepted connection, or make a closed socket when backend
    // is null.
    tcp_socket(detail::socket_handle backend,
               std::chrono::milliseconds timeout);

    detail::socket_handle backend_;
    net_error error_ = net_error::none;
    std::chrono::milliseconds timeout_{0};
};

// Accepts TCP connections from other devices.
//
// Notes:
//     Listens on every interface: the joined network, or the device's
//     own network while it is an access point. Accepted connections are
//     plain TCP; TLS is client-side only.
//     Failures are sticky, as for tcp_socket.
//     Thread-safety: not thread-safe.
class tcp_listener {
public:
    //
    // Listen on a port.
    //
    // Parameters:
    //     link    - Network; must be connected or hosting, and outlive
    //               the listener.
    //     port    - Port, 1 to 65535.
    //     timeout - Read timeout given to every accepted socket.
    //
    // Notes:
    //     On failure the listener is closed and error() says why:
    //     net_error::bind when the port is taken or not permitted.
    //
    tcp_listener(network &link,
                 std::uint16_t port,
                 std::chrono::milliseconds timeout = std::chrono::seconds(10));
    ~tcp_listener();

    tcp_listener(const tcp_listener &) = delete;
    tcp_listener &operator=(const tcp_listener &) = delete;
    // A moved-from listener is closed.
    tcp_listener(tcp_listener &&other) noexcept;
    tcp_listener &operator=(tcp_listener &&other) noexcept;

    // Return true while listening.
    [[nodiscard]] bool is_open() const;
    // Return the first failure, or net_error::none.
    [[nodiscard]] net_error error() const;
    // Return true when open and nothing has failed.
    explicit operator bool() const;

    //
    // Take one waiting connection, waiting at most wait for it.
    //
    // Returns:
    //     The connection, or a closed socket (is_open() false, error()
    //     none) when none arrived in time. With wait 0 it never waits,
    //     so a main loop can poll it.
    //
    [[nodiscard]] tcp_socket accept(std::chrono::milliseconds wait = {});

    // Stop listening. Accepted sockets stay open. Closing twice is
    // harmless.
    void close();

private:
    int descriptor_ = -1;
    net_error error_ = net_error::none;
    std::chrono::milliseconds timeout_{0};
};

// Where a datagram came from.
struct datagram_source {
    // Sender's IPv4 address, such as "192.168.4.2".
    std::string address{};
    std::uint16_t port = 0;
};

// Sends and receives UDP datagrams.
//
// Notes:
//     Each send is one datagram and each receive returns one. Datagrams
//     can be lost or arrive out of order; that is the protocol's nature.
//     A failed send is reported by its return value and leaves the socket
//     open, because the next datagram may well go through.
//     Thread-safety: not thread-safe.
class udp_socket {
public:
    //
    // Open a socket on a local port.
    //
    // Parameters:
    //     link - Network; must be connected or hosting, and outlive the
    //            socket.
    //     port - Local port to receive on; 0 lets the system choose,
    //            which suits a socket that only asks and hears replies.
    //
    // Notes:
    //     On failure the socket is closed and error() says why.
    //
    explicit udp_socket(network &link, std::uint16_t port = 0);
    ~udp_socket();

    udp_socket(const udp_socket &) = delete;
    udp_socket &operator=(const udp_socket &) = delete;
    // A moved-from socket is closed.
    udp_socket(udp_socket &&other) noexcept;
    udp_socket &operator=(udp_socket &&other) noexcept;

    // Return true while the socket is open.
    [[nodiscard]] bool is_open() const;
    // Return why opening failed, or net_error::none.
    [[nodiscard]] net_error error() const;
    // Return true when open and nothing has failed.
    explicit operator bool() const;

    // Return the local port, which is the chosen one when 0 was given.
    [[nodiscard]] std::uint16_t port() const;

    //
    // Send one datagram to one device.
    //
    // Parameters:
    //     address - IPv4 address, such as "192.168.4.2".
    //     port    - Port, 1 to 65535.
    //     data    - The datagram, at most about 1400 bytes to be safe.
    //
    // Returns:
    //     True when it was handed to the network.
    //
    [[nodiscard]] bool send_to(std::string_view address,
                               std::uint16_t port,
                               std::span<const std::byte> data);

    //
    // Send one datagram to every device on the local network.
    //
    // Returns:
    //     True when it went out on at least one network.
    //
    // Notes:
    //     The platform picks the destinations: the broadcast address of
    //     the joined network or of the access point on the device; every
    //     interface's broadcast address and loopback on the host, so
    //     programs on the same machine hear it too.
    //
    [[nodiscard]] bool broadcast(std::uint16_t port,
                                 std::span<const std::byte> data);

    //
    // Receive one datagram, waiting at most wait for it.
    //
    // Parameters:
    //     out  - Buffer; a longer datagram is cut to its size.
    //     from - Receives the sender.
    //     wait - Longest wait; 0 never waits, for polling from a loop.
    //
    // Returns:
    //     Bytes received, or 0 when nothing arrived in time.
    //
    [[nodiscard]] std::size_t receive(std::span<std::byte> out,
                                      datagram_source &from,
                                      std::chrono::milliseconds wait = {});

    // Close the socket. Closing twice is harmless.
    void close();

private:
    network *link_ = nullptr;
    int descriptor_ = -1;
    net_error error_ = net_error::none;
};

} // namespace inkerface
