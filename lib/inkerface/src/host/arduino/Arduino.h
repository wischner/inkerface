//
// Host stand-in for the slice of the Arduino core API that FreeInk's
// SecureNet headers use: millis(), delay(), String and IPAddress. Only the
// host build sees this directory; the X4 uses the real Arduino core. The
// file and type names are mandated by Arduino and exempt from the naming
// rules.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

// Milliseconds since the program started; wraps like Arduino's.
unsigned long millis();

// Sleep for ms milliseconds.
void delay(unsigned long ms);

// Minimal Arduino String: holds text and exposes it as a C string.
class String {
public:
    String(const char *text = "")
        : text_(text) {}
    explicit String(std::string text)
        : text_(std::move(text)) {}

    [[nodiscard]] const char *c_str() const {
        return text_.c_str();
    }

private:
    std::string text_;
};

// Present only so Client's connect(IPAddress, port) overload compiles; the
// host connects by name only.
class IPAddress {};
