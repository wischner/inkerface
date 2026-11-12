//
// Declares tcp_socket: a TCP connection, optionally secured with TLS, for
// protocols other than HTTP. Reads wait at most the socket's timeout, so
// a program never blocks indefinitely.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
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
    detail::socket_handle backend_;
    net_error error_ = net_error::none;
    std::chrono::milliseconds timeout_{0};
};

} // namespace inkerface
