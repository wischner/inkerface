//
// Host stand-in for Arduino-ESP32's base64 encoder (used by FreeInk for
// HTTP Basic authentication).
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <Arduino.h>

struct base64 {
    // Encode a NUL-terminated string as standard base64.
    static String encode(const char *text);
};
