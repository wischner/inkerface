//
// Tests the drawing surface through the host display. The host renders
// with the same FreeInk canvas as the device, so these checks also cover
// the adapter's conversions and its pixel read-back mapping.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include <inkerface/display.h>
#include <inkerface/surface.h>

#include <array>
#include <stdexcept>

namespace tests {

namespace {

using inkerface::color;
using inkerface::point;
using inkerface::rect;

// Count black pixels in an area of the surface.
int black_pixels(const inkerface::surface &frame, const rect &area) {
    int count = 0;
    for (inkerface::coord y = area.y; y < area.bottom(); ++y) {
        for (inkerface::coord x = area.x; x < area.right(); ++x) {
            if (frame.pixel_at(point{x, y}) == color::black) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

void run_surface_tests(test_context &context) {
    inkerface::display screen;
    inkerface::surface frame = screen.frame();

    const inkerface::size extent = frame.extent();
    context.check(extent.width == 480 && extent.height == 800,
                  "host frame is the X4 panel held in portrait");
    const rect whole{0, 0, extent.width, extent.height};

    frame.clear(color::white);
    context.check(black_pixels(frame, whole) == 0, "clear white");

    frame.clear(color::black);
    context.check(black_pixels(frame, whole) == 480 * 800, "clear black");

    frame.clear(color::white);
    frame.fill_rect(rect{10, 700, 5, 3}, color::black);
    context.check(black_pixels(frame, whole) == 15, "fill exact pixel count");
    context.check(frame.pixel_at(point{10, 700}) == color::black,
                  "fill origin is at logical (10, 700)");
    context.check(frame.pixel_at(point{14, 702}) == color::black,
                  "fill last pixel");
    context.check(frame.pixel_at(point{15, 702}) == color::white,
                  "fill stops at right edge");

    frame.clear(color::white);
    frame.fill_rect(rect{-5, -5, 10, 10}, color::black);
    context.check(black_pixels(frame, whole) == 25, "fill is clipped");

    frame.clear(color::white);
    frame.draw_rect(rect{100, 100, 10, 10}, color::black, 1);
    context.check(black_pixels(frame, whole) == 36, "outline pixel count");
    context.check(frame.pixel_at(point{105, 105}) == color::white,
                  "outline interior is untouched");

    frame.clear(color::white);
    frame.draw_line(point{0, 0}, point{9, 0}, color::black, 1);
    context.check(black_pixels(frame, whole) == 10, "line is inclusive");

    context.check(!frame.pixel_at(point{-1, 0}).has_value(),
                  "read-back left of frame");
    context.check(!frame.pixel_at(point{480, 0}).has_value(),
                  "read-back right of frame");
    context.check(!frame.pixel_at(point{0, 800}).has_value(),
                  "read-back below frame");

    // Two padded 9-pixel rows exercise MSB order and row stride.
    const std::array<std::uint8_t, 4> bitmap{0x80, 0x80, 0x40, 0x00};
    frame.clear(color::white);
    frame.draw_bitmap({10, 20}, {9, 2}, bitmap);
    context.check(black_pixels(frame, whole) == 3,
                  "bitmap draws only its set bits");
    context.check(frame.pixel_at({10, 20}) == color::black &&
                      frame.pixel_at({18, 20}) == color::black &&
                      frame.pixel_at({11, 21}) == color::black,
                  "bitmap uses MSB-first bits and padded rows");
    frame.draw_bitmap({10, 20}, {9, 2}, bitmap, color::white);
    context.check(black_pixels(frame, whole) == 0,
                  "bitmap respects the requested ink color");
    frame.draw_bitmap({-1, -1}, {9, 2}, bitmap);
    context.check(black_pixels(frame, whole) == 1,
                  "bitmap is clipped at display edges");

    bool rejected = false;
    try {
        frame.draw_bitmap({0, 0}, {9, 3}, bitmap);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    context.check(rejected, "truncated bitmap is rejected");
    rejected = false;
    try {
        frame.draw_bitmap({0, 0}, {-1, 2}, bitmap);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    context.check(rejected, "invalid bitmap dimensions are rejected");

    const inkerface::font face = inkerface::font::regular();
    const inkerface::size measured = frame.measure_text("Hello", face);
    context.check(measured.width > 0, "text has width");
    context.check(measured.height == frame.line_height(face),
                  "measured height is the line height");
    context.check(frame.measure_text("", face).width == 0,
                  "empty text has no width");
    context.check(
        frame.measure_text(std::string_view("Hi\0there", 8), face).width ==
            frame.measure_text("Hi", face).width,
        "text stops at embedded NUL");

    frame.clear(color::white);
    frame.draw_text(whole, "", inkerface::text_style{});
    context.check(black_pixels(frame, whole) == 0, "empty text draws nothing");

    context.check(screen.present(inkerface::refresh_mode::full),
                  "host present succeeds without a frame path");
}

} // namespace tests
