//
// Declares the input facade. Physical buttons, touch, or host scripts
// are translated into semantic actions so screens never see pin names or
// device-specific events.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <chrono>
#include <memory>

namespace inkerface {

class display;

namespace detail {
class input_backend;
} // namespace detail

// Semantic actions that occurred since the previous poll.
struct input_snapshot {
    bool confirm = false;
    bool back = false;
    bool focus_next = false;
    bool focus_prev = false;
    bool page_next = false;
    bool page_prev = false;

    // The power button was held long enough to mean "switch off", about a
    // second. Reported once per hold; a brief press does not count, so a
    // bumped button cannot switch a device off. See power.h.
    bool power = false;

    // Return true when at least one action occurred.
    [[nodiscard]] constexpr bool any() const {
        return confirm || back || focus_next || focus_prev || page_next ||
               page_prev || power;
    }
};

// The application's single input source.
//
// Notes:
//     Construction initializes the input hardware (or host source).
//     Thread-safety: not thread-safe; use from the UI thread only.
class input {
public:
    input();

    // Bind input to a display, which must outlive this input. On Linux,
    // INKERFACE_PREVIEW=1 opens a window showing that display's frame;
    // otherwise stdin is used. Device input still uses physical buttons.
    // Throws std::runtime_error if a requested preview cannot be opened.
    explicit input(display &out);
    ~input();

    input(const input &) = delete;
    input &operator=(const input &) = delete;
    input(input &&) = delete;
    input &operator=(input &&) = delete;

    //
    // Collect the actions that occurred since the previous call.
    //
    // Returns:
    //     The actions that occurred; may be empty.
    //
    // Notes:
    //     May block briefly: the device paces polling to about 100 Hz,
    //     and the host blocks for the next script line or window event.
    //
    [[nodiscard]] input_snapshot poll();

    //
    // Wait at most wait for actions.
    //
    // Returns:
    //     The actions that occurred, or an empty snapshot once wait has
    //     passed without any.
    //
    // Notes:
    //     Use this instead of poll() when the screen must also change
    //     without input, for example when another thread has new data:
    //     the loop wakes at least once per wait.
    //
    [[nodiscard]] input_snapshot poll_for(std::chrono::milliseconds wait);

    // Return true when the source can produce no further input. The
    // device never closes; the host closes at the end of its script.
    [[nodiscard]] bool closed() const;

private:
    std::unique_ptr<detail::input_backend> backend_;
};

} // namespace inkerface
