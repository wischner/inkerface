//
// Declares the backend behind tcp_socket, shared by the socket and the
// listener that hands out accepted connections. Internal to the platform
// layer: it names Arduino and FreeInk types.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <Client.h>
#include <SecureClient.h>
#include <WiFiClient.h>

namespace inkerface::detail {

// One connection. Both clients are members so no heap object or virtual
// construction is needed; active points at the one in use.
class socket_backend {
public:
    socket_backend() = default;

    // Adopt a connected socket descriptor as a plain connection, as
    // tcp_listener does with an accepted one. The client owns it from
    // here and closes it.
    explicit socket_backend(int descriptor)
        : plain(descriptor)
        , active(&plain) {}

    socket_backend(const socket_backend &) = delete;
    socket_backend &operator=(const socket_backend &) = delete;
    socket_backend(socket_backend &&) = delete;
    socket_backend &operator=(socket_backend &&) = delete;
    ~socket_backend() = default;

    WiFiClient plain;
    freeink::SecureClient secure;
    Client *active = nullptr;
};

} // namespace inkerface::detail
