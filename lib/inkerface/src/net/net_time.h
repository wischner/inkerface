//
// Declares small time helpers shared by the network code: converting
// durations to the millisecond counts Arduino APIs take, and a deadline
// over millis() that is safe across its 49-day wrap-around.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <Arduino.h>

#include <chrono>
#include <cstdint>
#include <limits>

namespace inkerface::detail {

// Clamp a duration to [0, UINT32_MAX] milliseconds.
inline std::uint32_t to_milliseconds(std::chrono::milliseconds duration) {
    const auto count = duration.count();
    if (count <= 0) {
        return 0;
    }
    constexpr auto largest = std::numeric_limits<std::uint32_t>::max();
    return count > largest ? largest : static_cast<std::uint32_t>(count);
}

// A point in time on the millis() clock.
class deadline {
public:
    explicit deadline(std::chrono::milliseconds from_now)
        : start_(static_cast<std::uint32_t>(millis()))
        , length_(to_milliseconds(from_now)) {}

    // Return true once the duration has passed.
    [[nodiscard]] bool passed() const {
        // Unsigned subtraction is wrap-around safe.
        const auto now = static_cast<std::uint32_t>(millis());
        return now - start_ >= length_;
    }

private:
    std::uint32_t start_;
    std::uint32_t length_;
};

} // namespace inkerface::detail
