//
// Declares the display: the owner of the frame screens draw on and of
// the policy that pushes it to the panel. The backend (real e-paper
// hardware or the host emulation) is selected at build time.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include <inkerface/font.h>
#include <inkerface/storage.h>
#include <inkerface/surface.h>

namespace inkerface {

namespace detail {
class display_backend;
} // namespace detail

// How the panel should update when a frame is presented.
enum class refresh_mode : std::uint8_t {
    // Flashing refresh that removes ghosting; use for screen changes.
    full,
    // Quick partial refresh; use for small updates within a screen.
    fast
};

// The application's single display.
//
// Notes:
//     Construction brings the display hardware (or host emulation) up and
//     the destructor releases it. Exactly one display should exist.
//     Failure: construction throws std::bad_alloc when the frame cannot
//     be allocated.
//     Thread-safety: not thread-safe; use from the UI thread only.
class display {
public:
    display();
    ~display();

    display(const display &) = delete;
    display &operator=(const display &) = delete;
    display(display &&) = delete;
    display &operator=(display &&) = delete;

    // Return the drawing surface of the frame. The surface must not
    // outlive this display.
    [[nodiscard]] surface frame();

    //
    // Push the frame to the panel.
    //
    // Parameters:
    //     mode - Panel refresh mode.
    //
    // Returns:
    //     True on success, false when the frame could not be delivered
    //     (for example, the host could not write its frame file).
    //
    [[nodiscard]] bool present(refresh_mode mode);

    //
    // Load a font into a slot, replacing the font it had.
    //
    // Parameters:
    //     face - Slot to load into; a built-in slot can be replaced too.
    //     data - A font in inkerface format, made with tools/make_font.py.
    //            The data is validated and copied, so it may be discarded
    //            afterwards.
    //
    // Returns:
    //     font_error::none on success; otherwise the slot is unchanged.
    //
    // Notes:
    //     A loaded font costs RAM about the size of its data (an ASCII
    //     font at 24 px is roughly 3 KB). Built-in fonts cost none.
    //
    [[nodiscard]] font_error load_font(font face,
                                       std::span<const std::uint8_t> data);

    // Load a font file from storage; as load_font(face, data). Returns
    // font_error::unreadable when the file can't be read.
    [[nodiscard]] font_error load_font(font face,
                                       storage &disks,
                                       volume where,
                                       std::string_view path);

    // Give a slot its built-in font back, freeing a loaded font's memory;
    // a custom slot returns to the regular font.
    void reset_font(font face);

private:
    // Puts the panel to sleep, which only the backend can do. See power.h.
    friend void power_off(display &screen);

    std::unique_ptr<detail::display_backend> backend_;
};

} // namespace inkerface
