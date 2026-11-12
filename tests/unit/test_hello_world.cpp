//
// Tests that the hello world screen clears the page and draws its
// logo above a centered greeting, and that it ignores input.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include "../../src/screens/hello_world_screen.h"

#include <inkerface/display.h>

namespace tests {

namespace {

using inkerface::color;
using inkerface::point;
using inkerface::rect;

// Return the bounding box of all black pixels, or an empty rect.
rect ink_bounds(const inkerface::surface &frame, const rect &area) {
    const inkerface::size extent = frame.extent();
    inkerface::coord left = extent.width;
    inkerface::coord top = extent.height;
    inkerface::coord right = -1;
    inkerface::coord bottom = -1;
    for (inkerface::coord y = area.y; y < area.bottom(); ++y) {
        for (inkerface::coord x = area.x; x < area.right(); ++x) {
            if (frame.pixel_at(point{x, y}) != color::black) {
                continue;
            }
            left = x < left ? x : left;
            top = y < top ? y : top;
            right = x > right ? x : right;
            bottom = y > bottom ? y : bottom;
        }
    }
    if (right < 0) {
        return rect{};
    }
    return rect{left,
                top,
                static_cast<inkerface::coord>(right - left + 1),
                static_cast<inkerface::coord>(bottom - top + 1)};
}

} // namespace

void run_hello_world_tests(test_context &context) {
    inkerface::display screen;
    inkerface::surface frame = screen.frame();
    const inkerface::size extent = frame.extent();

    // Start dirty so the test proves paint() clears the page.
    frame.clear(color::black);

    ui::hello_world_screen hello;
    hello.paint(frame);

    const rect logo = ink_bounds(frame, rect{0, 0, 480, 520});
    const rect ink = ink_bounds(frame, rect{0, 520, 480, 280});
    context.check(logo.width > 150 && logo.height > 150,
                  "a substantial logo is drawn above the greeting");
    context.check(logo.bottom() + 24 <= ink.y,
                  "logo and greeting have a clear vertical gap");
    const std::int32_t logo_center = logo.x + logo.width / 2;
    context.check(logo_center > extent.width / 2 - 8 &&
                      logo_center < extent.width / 2 + 8,
                  "logo is horizontally centered");
    context.check(!ink.empty(), "greeting is drawn");

    const inkerface::size text = frame.measure_text(
        ui::hello_world_screen::greeting, inkerface::font::regular());
    context.check(ink.width <= text.width, "ink fits the measured width");
    context.check(ink.height <= text.height, "ink fits one line");

    // Centered within a few pixels (glyph side bearings are asymmetric).
    const std::int32_t center_x = ink.x + ink.width / 2;
    context.check(center_x > extent.width / 2 - 8 &&
                      center_x < extent.width / 2 + 8,
                  "greeting is horizontally centered");
    context.check(ink.y > extent.height / 2,
                  "greeting is below the center of the logo group");

    context.check(frame.pixel_at(point{0, 0}) == color::white,
                  "page is cleared at the top-left corner");
    context.check(frame.pixel_at(point{
                      static_cast<inkerface::coord>(extent.width - 1),
                      static_cast<inkerface::coord>(extent.height - 1)}) ==
                      color::white,
                  "page is cleared at the bottom-right corner");

    inkerface::input_snapshot press;
    press.confirm = true;
    context.check(hello.handle(press).action == ui::navigation::stay,
                  "confirm is ignored");
    press = inkerface::input_snapshot{};
    press.back = true;
    context.check(hello.handle(press).action == ui::navigation::stay,
                  "back is ignored");
}

} // namespace tests
