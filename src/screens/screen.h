//
// Declares the contract every application screen satisfies and the
// navigation results screens return. Screens are plain value types that
// meet the ui::screen concept; there is no screen base class.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <concepts>
#include <cstdint>

#include <inkerface/input.h>
#include <inkerface/surface.h>

namespace ui {

// Identifies each screen the application can open.
enum class screen_id : std::uint8_t {
    hello_world
};

// What the application should do after a screen handled input.
enum class navigation : std::uint8_t {
    // Nothing changed.
    stay,
    // The screen's own state changed; repaint it.
    repaint,
    // Open the target screen on top of this one.
    open,
    // Close this screen and return to the one below it.
    close
};

// A navigation request returned by screen::handle().
struct screen_result {
    navigation action = navigation::stay;
    // Screen to open; meaningful only for navigation::open.
    screen_id target = screen_id::hello_world;

    // Request no change.
    [[nodiscard]] static constexpr screen_result stay() {
        return screen_result{};
    }

    // Request a repaint of the current screen.
    [[nodiscard]] static constexpr screen_result repaint() {
        return screen_result{navigation::repaint, screen_id::hello_world};
    }

    // Request opening another screen.
    [[nodiscard]] static constexpr screen_result open(screen_id next) {
        return screen_result{navigation::open, next};
    }

    // Request closing the current screen.
    [[nodiscard]] static constexpr screen_result close() {
        return screen_result{navigation::close, screen_id::hello_world};
    }
};

// A type usable as an application screen.
//
// Notes:
//     paint() draws the whole screen from current state and must not
//     retain the surface. handle() reacts to one input snapshot.
template <typename type>
concept screen = requires(const type &view,
                          type &state,
                          inkerface::surface &out,
                          const inkerface::input_snapshot &in) {
    { view.paint(out) } -> std::same_as<void>;
    { state.handle(in) } -> std::same_as<screen_result>;
};

} // namespace ui
