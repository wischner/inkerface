//
// Host stand-in for Arduino-ESP32's WiFiClient: a TCP connection over a
// non-blocking POSIX socket, with Arduino's semantics (read() returns -1
// when no data is ready; connected() stays true while data is unread).
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <Client.h>

#include <cstddef>
#include <cstdint>

class WiFiClient : public Client {
public:
    WiFiClient() = default;
    ~WiFiClient() override;

    WiFiClient(const WiFiClient &) = delete;
    WiFiClient &operator=(const WiFiClient &) = delete;
    WiFiClient(WiFiClient &&) = delete;
    WiFiClient &operator=(WiFiClient &&) = delete;

    int connect(IPAddress ip, uint16_t port) override;
    int connect(const char *host, uint16_t port) override;
    size_t write(uint8_t byte) override;
    size_t write(const uint8_t *data, size_t size) override;
    int available() override;
    int read() override;
    int read(uint8_t *data, size_t size) override;
    int peek() override;
    void flush() override;
    void stop() override;
    uint8_t connected() override;
    operator bool() override;

    // Longest wait for connect(), in milliseconds.
    void setConnectionTimeout(uint32_t ms);

    // The socket descriptor, or -1; used by the host TLS client.
    [[nodiscard]] int fd() const;

private:
    int descriptor_ = -1;
    uint32_t connect_timeout_ms_ = 10'000;
};
