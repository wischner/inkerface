//
// Host stand-in for Arduino's Client interface (with the timeout part of
// Stream), matching the virtual functions FreeInk's network code calls.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

class Client {
public:
    virtual ~Client() = default;
    virtual int connect(IPAddress ip, uint16_t port) = 0;
    virtual int connect(const char *host, uint16_t port) = 0;
    virtual size_t write(uint8_t byte) = 0;
    virtual size_t write(const uint8_t *data, size_t size) = 0;
    // Bytes readable now.
    virtual int available() = 0;
    // One byte, or -1 when none is available.
    virtual int read() = 0;
    // Up to size bytes; -1 when none is available.
    virtual int read(uint8_t *data, size_t size) = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;
    virtual void stop() = 0;
    // True while connected or unread data remains.
    virtual uint8_t connected() = 0;
    virtual operator bool() = 0;

    // Stream timeout, in milliseconds.
    void setTimeout(unsigned long ms) {
        timeout_ms_ = ms;
    }
    [[nodiscard]] unsigned long getTimeout() const {
        return timeout_ms_;
    }

private:
    unsigned long timeout_ms_ = 1000;
};
