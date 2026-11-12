//
// Declares the FreeInk-backed canvas behind inkerface::surface. It wraps
// FreeInkUI's DisplayTarget over a 1-bit framebuffer the caller owns, so
// the host emulation and the device render through the same FreeInk code.
// This header is internal to the platform library: it is the boundary
// where FreeInk types are allowed, and it must never be included from
// include/, src/, or tests/.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <inkerface/color.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>

#include <FreeInkUIDisplayTarget.h>

namespace inkerface::detail {

// Panel-native geometry of a 1-bit framebuffer.
//
// Notes:
//     Rows are width_bytes long, MSB-first; a set bit is white paper and
//     a clear bit is black ink (the FreeInkDisplay convention).
// Largest font data load_font() accepts, in bytes.
inline constexpr std::size_t max_font_bytes = 64 * 1024;

// A font loaded at run time: its validated glyph data and the FreeInk
// font that points into it.
struct loaded_font {
    std::vector<std::uint8_t> bitmap;
    std::vector<freeink::ui::FontGlyph> glyphs;
    freeink::ui::BitmapFont font{};
};

struct panel_geometry {
    coord width = 0;
    coord height = 0;
    coord width_bytes = 0;
};

// FreeInk drawing target over a caller-owned framebuffer.
//
// Notes:
//     The logical frame is portrait: the panel's native landscape frame
//     rotated 90 degrees clockwise, which is how an e-reader is held.
//     Lifetime: the framebuffer must outlive the canvas.
class canvas {
public:
    //
    // Wrap a framebuffer.
    //
    // Parameters:
    //     framebuffer - Panel-native 1-bit pixels; must hold at least
    //                   width_bytes * height bytes.
    //     geometry    - Panel-native dimensions.
    //
    // Failure:
    //     Throws std::invalid_argument when the geometry is not positive,
    //     width_bytes cannot hold width pixels, or the buffer is too small.
    //
    canvas(std::span<std::uint8_t> framebuffer, panel_geometry geometry);

    canvas(const canvas &) = delete;
    canvas &operator=(const canvas &) = delete;
    canvas(canvas &&) = delete;
    canvas &operator=(canvas &&) = delete;
    ~canvas() = default;

    // Return the FreeInk drawing target.
    [[nodiscard]] freeink::ui::DisplayTarget &target();

    // Return the FreeInk drawing target for measurement.
    [[nodiscard]] const freeink::ui::DisplayTarget &target() const;

    // Return the logical (portrait) size.
    [[nodiscard]] size extent() const;

    // Read one logical pixel; std::nullopt outside the frame.
    [[nodiscard]] std::optional<color> pixel_at(point p) const;

    //
    // Load an inkerface font (IKF1, see tools/make_font.py) into a slot.
    //
    // Returns:
    //     font_error::none, or why the data was rejected; on failure the
    //     slot keeps its font. The data is validated field by field before
    //     use, because font files are untrusted input.
    //
    [[nodiscard]] font_error load_font(std::uint8_t slot,
                                       std::span<const std::uint8_t> data);

    // Give a slot its built-in font back.
    void reset_font(std::uint8_t slot);

    // Return the font a slot draws with.
    [[nodiscard]] const freeink::ui::BitmapFont &
    font_in(std::uint8_t slot) const;

private:
    std::span<std::uint8_t> framebuffer_;
    panel_geometry geometry_;
    freeink::ui::DisplayTarget target_;
    std::array<const freeink::ui::BitmapFont *, font_slot_count> fonts_{};
    std::array<std::unique_ptr<loaded_font>, font_slot_count> loaded_;
};

// Convert a platform color to the FreeInk color.
[[nodiscard]] freeink::ui::Color to_freeink(color value);

// Convert a platform rectangle to the FreeInk rectangle.
[[nodiscard]] freeink::ui::Rect to_freeink(const rect &value);

// Convert a platform point to the FreeInk point.
[[nodiscard]] freeink::ui::Point to_freeink(point value);

// Convert a platform font to a FreeInk font slot.
[[nodiscard]] freeink::ui::FontId to_freeink(font value);

} // namespace inkerface::detail
