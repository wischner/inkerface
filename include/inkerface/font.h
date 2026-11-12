//
// Declares the font handle and text style used by the drawing API.
// A font is a small value naming one of eight font slots. Four slots hold
// built-in bitmap fonts (Noto Sans, ASCII); the other four are for fonts
// the program loads with display::load_font(). Any slot can be replaced
// by a loaded font.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstdint>
#include <optional>

#include <inkerface/color.h>

namespace inkerface {

// Number of font slots every backend must provide.
inline constexpr std::uint8_t font_slot_count = 8;

// Slots reserved for loaded fonts: font::custom(0) to custom(3).
inline constexpr std::uint8_t custom_font_count = 4;

// Why loading a font failed. none means success.
enum class font_error : std::uint8_t {
    none,
    // The font file could not be read from storage.
    unreadable,
    // The data is not a valid inkerface font (see tools/make_font.py).
    malformed,
    // The font is valid but larger than the loader accepts.
    too_large
};

// A handle to one backend font.
//
// Notes:
//     Invariant: slot() < font_slot_count. Instances can only be obtained
//     from the named factories, so an invalid slot cannot be represented.
class font {
public:
    // Return the default font: Noto Sans, 24 px.
    [[nodiscard]] static constexpr font regular() {
        return font(0);
    }

    // Return the small font: Noto Sans, 16 px.
    [[nodiscard]] static constexpr font small() {
        return font(1);
    }

    // Return the bold font: Noto Sans Bold, 24 px.
    [[nodiscard]] static constexpr font bold() {
        return font(2);
    }

    // Return the title font: Noto Sans Bold, 32 px.
    [[nodiscard]] static constexpr font title() {
        return font(3);
    }

    //
    // Return a slot for a loaded font.
    //
    // Parameters:
    //     index - 0 to custom_font_count - 1.
    //
    // Returns:
    //     The slot, or std::nullopt for an index out of range. Until a
    //     font is loaded into it, the slot draws with the regular font.
    //
    [[nodiscard]] static constexpr std::optional<font>
    custom(std::uint8_t index) {
        if (index >= custom_font_count) {
            return std::nullopt;
        }
        return font(static_cast<std::uint8_t>(font_slot_count -
                                              custom_font_count + index));
    }

    // Return true when two handles name the same slot.
    [[nodiscard]] constexpr bool operator==(const font &) const = default;

    // Return the backend slot index this font refers to.
    [[nodiscard]] constexpr std::uint8_t slot() const {
        return slot_;
    }

private:
    explicit constexpr font(std::uint8_t slot)
        : slot_(slot) {}

    std::uint8_t slot_;
};

// Horizontal placement of text inside its layout rectangle.
enum class text_align : std::uint8_t {
    left,
    center,
    right
};

// Describes how draw_text renders a string.
//
// Notes:
//     max_lines of zero is treated as one line. Text that does not fit is
//     word-wrapped up to max_lines and then truncated with an ellipsis.
struct text_style {
    font face = font::regular();
    color ink = color::black;
    text_align align = text_align::left;
    std::uint8_t max_lines = 1;
};

} // namespace inkerface
