//
// Declares the drawing surface screens paint on. A surface is a cheap,
// non-owning handle to the display's frame; the backend that implements
// it is hidden behind an opaque forward declaration.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <inkerface/color.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>

namespace inkerface {

namespace detail {
class canvas;
} // namespace detail

// Drawing operations over one display frame.
//
// Notes:
//     Lifetime: a surface refers to storage owned by the display that
//     created it and MUST NOT outlive that display.
//     Drawing outside the surface is clipped, never an error.
//     Thread-safety: not thread-safe; use from the UI thread only.
class surface {
public:
    //
    // Wrap a backend canvas. Only display implementations call this.
    //
    // Parameters:
    //     target - Backend canvas; must outlive the surface.
    //
    explicit surface(detail::canvas &target);

    // Return the logical size of the surface in pixels.
    [[nodiscard]] size extent() const;

    // Fill the whole surface with one color.
    void clear(color fill);

    // Fill a rectangle with one color.
    void fill_rect(const rect &area, color fill);

    //
    // Draw the outline of a rectangle inside its bounds.
    //
    // Parameters:
    //     area      - Outer edge of the outline.
    //     ink       - Outline color.
    //     thickness - Outline width in pixels; zero draws nothing.
    //
    void draw_rect(const rect &area, color ink, std::uint8_t thickness = 1);

    //
    // Draw a straight line between two points, inclusive.
    //
    // Parameters:
    //     from      - First endpoint.
    //     to        - Second endpoint.
    //     ink       - Line color.
    //     thickness - Line width in pixels.
    //
    void draw_line(point from, point to, color ink, std::uint8_t thickness = 1);

    // Draw a row-major, MSB-first 1-bit bitmap at its original size.
    // Set bits draw ink; clear bits leave the surface unchanged. Rows
    // occupy ceil(width / 8) bytes. Data is borrowed only during the call.
    // Throws std::invalid_argument for non-positive dimensions or a
    // buffer smaller than the dimensions require. Drawing is clipped.
    void draw_bitmap(point origin,
                     size dimensions,
                     std::span<const std::uint8_t> data,
                     color ink = color::black);

    //
    // Draw text inside a layout rectangle.
    //
    // Parameters:
    //     area  - Layout box; text is vertically centered in it.
    //     text  - UTF-8 text. Characters missing from the font render as
    //             blanks. Text stops at the first NUL character.
    //     style - Font, color, alignment, and line limit.
    //
    void
    draw_text(const rect &area, std::string_view text, const text_style &style);

    //
    // Draw one line of text with its top-left corner at origin.
    //
    // Parameters:
    //     origin - Top of the line box, and left edge of the text.
    //     text   - UTF-8 text; nothing wraps, the line is clipped.
    //     face   - Font to draw with.
    //     ink    - Text color.
    //
    void draw_text(point origin,
                   std::string_view text,
                   font face = font::regular(),
                   color ink = color::black);

    // Return the distance from the top of a line box to the baseline, so
    // lines in different fonts can share a baseline.
    [[nodiscard]] coord baseline(font face) const;

    //
    // Measure single-line text.
    //
    // Returns:
    //     Width of the text and the line height of the font.
    //
    [[nodiscard]] size measure_text(std::string_view text, font face) const;

    // Return the line advance of a font in pixels.
    [[nodiscard]] coord line_height(font face) const;

    //
    // Read back one logical pixel.
    //
    // Returns:
    //     color::black or color::white for a pixel inside the surface, or
    //     std::nullopt when the point lies outside it.
    //
    [[nodiscard]] std::optional<color> pixel_at(point p) const;

private:
    detail::canvas *target_;
};

} // namespace inkerface
