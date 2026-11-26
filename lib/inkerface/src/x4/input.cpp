//
// Implements the Xteink X4 input source on FreeInk's InputManager. The
// button roles follow the FreeInk convention: UP/DOWN move focus,
// LEFT/RIGHT turn pages, CONFIRM/BACK act. The power button reports
// `power` once it has been held for power_hold_ms, once per hold.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/input.h>

#include <Arduino.h>
#include <InputManager.h>

#include <cstdint>

#include "../net/net_time.h"

namespace inkerface::detail {

namespace {

// Poll pacing; keeps the loop near 100 Hz without busy-waiting.
constexpr std::uint32_t poll_interval_ms = 10;

// How long the power button must be held to mean "switch off". Long
// enough that a knock does not count, short enough to feel immediate.
constexpr unsigned long power_hold_ms = 1000;

} // namespace

// Device input state: the FreeInk button/touch manager.
class input_backend {
public:
    input_backend() {
        buttons_.begin();
    }

    input_snapshot poll() {
        delay(poll_interval_ms);
        buttons_.update();
        input_snapshot snapshot;
        snapshot.confirm = buttons_.wasPressed(InputManager::BTN_CONFIRM);
        snapshot.back = buttons_.wasPressed(InputManager::BTN_BACK);
        snapshot.focus_next = buttons_.wasPressed(InputManager::BTN_DOWN);
        snapshot.focus_prev = buttons_.wasPressed(InputManager::BTN_UP);
        snapshot.page_next = buttons_.wasPressed(InputManager::BTN_RIGHT);
        snapshot.page_prev = buttons_.wasPressed(InputManager::BTN_LEFT);
        snapshot.power = power_held();
        return snapshot;
    }

private:
    // Return true once per hold, when the hold reaches power_hold_ms.
    bool power_held() {
        if (!buttons_.isPressed(InputManager::BTN_POWER)) {
            power_reported_ = false;
            return false;
        }
        if (power_reported_ ||
            buttons_.getPowerButtonHeldTime() < power_hold_ms) {
            return false;
        }
        power_reported_ = true;
        return true;
    }

    InputManager buttons_;
    bool power_reported_ = false;
};

} // namespace inkerface::detail

namespace inkerface {

input::input()
    : backend_(std::make_unique<detail::input_backend>()) {}

input::input(display & /*out*/)
    : input() {}

input::~input() = default;

input_snapshot input::poll() {
    return backend_->poll();
}

input_snapshot input::poll_for(std::chrono::milliseconds wait) {
    const detail::deadline until(wait);
    do {
        const input_snapshot snapshot = backend_->poll(); // about 10 ms
        if (snapshot.any()) {
            return snapshot;
        }
    } while (!until.passed());
    return input_snapshot{};
}

bool input::closed() const {
    return false;
}

} // namespace inkerface
