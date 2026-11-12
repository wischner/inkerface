# 3. Drawing

In this chapter you learn every drawing call by building one page: a
title bar, a paragraph that wraps, gray swatches, a divider, an icon, and
a footer.

## Coordinates

The X4 panel is 800×480 pixels in landscape, but a reader is held
upright. inkerface therefore gives you a **portrait** frame:

```text
(0,0) ──────────── x ───────────▶ (479,0)
  │
  │      480 × 800 logical pixels
  y
  │
  ▼
(0,799)                           (479,799)
```

- `point{x, y}`: one position. `x` grows to the right and `y` grows down.
- `size{width, height}`: a size.
- `rect{x, y, width, height}`: an area. It is *half-open*: it covers
  columns `x` up to `x + width - 1`. `rect::right()` and `rect::bottom()`
  return the first pixel *outside* the rectangle.
- `coord` is a 16-bit signed integer. `right()` and `bottom()` return
  32 bits, so they can't overflow.

Always ask the frame for its size with `frame.extent()` instead of
writing 480 or 800 yourself. Your screens then keep working on other
panels.

Drawing outside the frame is not an error. Anything outside is simply
cut off (clipped).

### Casting coordinates

Arithmetic on two `coord` values produces an `int`. Because warnings are
errors (chapter 2), a small helper keeps layout code readable:

```cpp
// Convert a computed value back to a coordinate.
constexpr inkerface::coord to_coord(std::int32_t value) {
    return static_cast<inkerface::coord>(value);
}
```

The example in this chapter uses this helper.

## Colors

```cpp
inkerface::color::white
inkerface::color::light_gray   // about 25% ink
inkerface::color::dark_gray    // about 75% ink
inkerface::color::black
```

The X4 panel has only black and white pixels. Gray is faked with a fine,
regular dot pattern (a *dither*). Two consequences follow:

- A gray fill sets only its dark dots and leaves the other pixels as they
  were. Draw gray on white (for example, right after `clear()`). Gray on
  black stays black.
- `surface::pixel_at()` reads back only `color::white` or `color::black`.

## Shapes

| Call | Draws |
| --- | --- |
| `clear(color)` | the whole frame in one color |
| `fill_rect(rect, color)` | a filled rectangle |
| `draw_rect(rect, color, thickness)` | an outline, drawn *inside* the rectangle |
| `draw_line(point, point, color, thickness)` | a line that includes both end points |

## Text

```cpp
inkerface::text_style style;
style.face = inkerface::font::regular();      // or small, bold, title
style.ink = inkerface::color::black;
style.align = inkerface::text_align::center;  // left, center, right
style.max_lines = 3;                          // wrap at most 3 lines
frame.draw_text(area, "Some text", style);
```

- The rectangle is a **layout box**. Text is aligned horizontally inside
  it as `align` says, and it is always centered vertically.
- With `max_lines` greater than 1, text wraps at spaces. `\n` forces a
  new line. Text that still doesn't fit is cut off.
- `measure_text(text, font)` returns how wide one line of text is.
  `line_height(font)` returns how tall a line is. Use these to size your
  boxes instead of guessing.
- There are four built-in fonts: `font::small()`, `font::regular()`,
  `font::bold()` and `font::title()`, all Noto Sans, containing the
  printable ASCII characters (space to `~`). Other characters are drawn
  as blanks. Chapter 14 shows how to make and load your own fonts.
- `draw_text(point, text, font)` draws a single line with its top-left
  corner at a point, without a layout box.

## Bitmaps

`draw_bitmap(origin, size, bytes, ink)` draws a 1-bit image at its
original size.

- Rows run from top to bottom. Each row takes `(width + 7) / 8` bytes.
- The first pixel of a byte is its highest bit (`0x80`).
- A set bit draws `ink`. A clear bit leaves the frame unchanged.
- If the byte array is shorter than the size requires, the call throws
  `std::invalid_argument`.

Small icons can be typed in by hand, as the example below does. For
larger images, convert a PNG at development time.
`tools/convert_logo.py` turns `assets/logos/l1.png` into
`src/assets/logo.h` this way, and you can copy it for your own images.
The firmware then needs no image decoder.

## The example page

Replace `src/app/main.cpp` with this:

File: `src/app/main.cpp`

```cpp
//
// Draws a sample reader page that uses every drawing call, then waits
// until input closes.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/color.h>
#include <inkerface/display.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>
#include <inkerface/input.h>
#include <inkerface/program.h>
#include <inkerface/surface.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace {

using inkerface::color;
using inkerface::coord;
using inkerface::rect;
using inkerface::text_align;
using inkerface::text_style;

// Convert a computed value back to a coordinate.
constexpr coord to_coord(std::int32_t value) {
    return static_cast<coord>(value);
}

// An 8x8 bookmark ribbon, one byte per row; set bits are ink.
constexpr std::array<std::uint8_t, 8> bookmark_bits{
    0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x66, 0x42, 0x00};

constexpr std::string_view paragraph =
    "E-paper keeps its image without power. Draw the whole page "
    "into the frame first, then present it once: every refresh is "
    "slow and visible, so fewer refreshes feel faster.";

// Space between page elements, in pixels.
constexpr coord margin = 24;

void paint_page(inkerface::surface &frame) {
    const inkerface::size whole = frame.extent();
    const inkerface::font face = inkerface::font::regular();
    const coord line = frame.line_height(face);

    frame.clear(color::white);

    // Title bar: white text on a black band, with a white icon.
    const rect title{0, 0, whole.width, to_coord(line + 16)};
    frame.fill_rect(title, color::black);
    text_style title_style;
    title_style.ink = color::white;
    title_style.align = text_align::center;
    frame.draw_text(title, "Chapter 3: Drawing", title_style);
    frame.draw_bitmap(
        inkerface::point{to_coord(whole.width - margin - 8),
                         to_coord((title.height - 8) / 2)},
        inkerface::size{8, 8},
        bookmark_bits,
        color::white);

    // A wrapped paragraph inside a framed box.
    const rect body{margin,
                    to_coord(title.bottom() + 2 * margin),
                    to_coord(whole.width - 2 * margin),
                    to_coord(line * 6)};
    text_style body_style;
    body_style.max_lines = 6;
    frame.draw_text(body, paragraph, body_style);
    frame.draw_rect(rect{to_coord(body.x - 12),
                         to_coord(body.y - 12),
                         to_coord(body.width + 24),
                         to_coord(body.height + 24)},
                    color::black,
                    2);

    // Three swatches: gray levels are dithered on the 1-bit panel.
    constexpr std::array<color, 3> swatches{
        color::light_gray, color::dark_gray, color::black};
    const coord swatch_top = to_coord(body.bottom() + 3 * margin);
    const coord swatch_size = to_coord((whole.width - 4 * margin) / 3);
    for (std::size_t i = 0; i < swatches.size(); ++i) {
        const auto offset = static_cast<std::int32_t>(i);
        const coord left = to_coord(margin + offset * (swatch_size + margin));
        frame.fill_rect(rect{left, swatch_top, swatch_size, swatch_size},
                        swatches.at(i));
    }

    // A divider, then a word underlined to exactly its measured width.
    const coord divider_y = to_coord(swatch_top + swatch_size + margin);
    frame.draw_line(inkerface::point{margin, divider_y},
                    inkerface::point{to_coord(whole.width - margin - 1),
                                     divider_y},
                    color::black,
                    1);
    constexpr std::string_view word = "Measured";
    const inkerface::size word_size = frame.measure_text(word, face);
    const rect word_box{
        margin, to_coord(divider_y + margin), word_size.width, line};
    frame.draw_text(word_box, word, text_style{});
    frame.draw_line(
        inkerface::point{word_box.x, to_coord(word_box.bottom())},
        inkerface::point{to_coord(word_box.right() - 1),
                         to_coord(word_box.bottom())},
        color::black,
        2);

    // Footer, right-aligned along the bottom edge.
    text_style footer_style;
    footer_style.align = text_align::right;
    frame.draw_text(rect{margin,
                         to_coord(whole.height - line - margin),
                         to_coord(whole.width - 2 * margin),
                         line},
                    "page 1 of 1",
                    footer_style);
}

} // namespace

int program(int /*argc*/, char ** /*argv*/) {
    inkerface::display screen;
    inkerface::input buttons(screen);
    inkerface::surface frame = screen.frame();

    paint_page(frame);
    if (!screen.present(inkerface::refresh_mode::full)) {
        return EXIT_FAILURE;
    }
    while (!buttons.closed()) {
        static_cast<void>(buttons.poll());
    }
    return EXIT_SUCCESS;
}
```

Build and look at it with `make preview` or `make run`.

The page is built from boxes. Each element's position comes from the one
above it (`title.bottom()`, `body.bottom()`, and so on), and sizes come
from `extent()`, `line_height()` and `measure_text()`. This way, if you
change one element, the rest of the page moves with it.

## Summary

- The frame is portrait, 480×800 on the X4. Ask `extent()` instead of
  hard-coding the size.
- Rectangles are half-open, and drawing is clipped.
- Gray levels are dithered, so draw them on white.
- Text is placed in a layout box and measured with `measure_text()` and
  `line_height()`.
- Bitmaps are 1-bit, one row after another, with the highest bit first.

Next: [Input and the main loop](04-INPUT-AND-THE-MAIN-LOOP.md).
