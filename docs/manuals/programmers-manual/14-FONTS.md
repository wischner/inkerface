# 14. Fonts

inkerface draws text with **bitmap fonts**: every character is a small,
ready-made picture, made once from a TrueType font at a fixed size. They
are the cheapest kind of font there is. They draw fast and crisply, and
the built-in ones use no RAM at all. In this chapter you use the built-in
fonts, make your own, and load them from storage or compile them into the
program.

## The built-in fonts

| Font | Typeface | Line height | Flash |
| --- | --- | --- | --- |
| `font::small()` | Noto Sans, 16 px | 23 px | 1.8 KB |
| `font::regular()` | Noto Sans, 24 px | 34 px | 3.3 KB |
| `font::bold()` | Noto Sans Bold, 24 px | 34 px | 3.2 KB |
| `font::title()` | Noto Sans Bold, 32 px | 45 px | 5.4 KB |

All of them contain **printable ASCII only** (space to `~`). Other
characters, such as `é` or `č`, are drawn as blanks. Keep text ASCII, or
make a font that covers what you need (see below).

There are eight font **slots**. The four above are slots 0 to 3. Slots 4
to 7 are free for your own fonts, `font::custom(0)` to `font::custom(3)`,
and draw with the regular font until you load something into them.

## Drawing text

Every text function takes a font:

```cpp
inkerface::text_style heading;
heading.face = inkerface::font::title();
frame.draw_text(box, "Library", heading);              // in a box, wrapped

frame.draw_text(inkerface::point{20, 100}, "Chapter 1",  // one line, at a point
                inkerface::font::bold());

const inkerface::size size =
    frame.measure_text("Chapter 1", inkerface::font::bold());
const inkerface::coord line = frame.line_height(inkerface::font::small());
```

`draw_text(point, text, font, ink)` draws one line with its **top-left
corner** at the point. Nothing wraps. Text that runs past the edge of the
frame is clipped.

### Mixing fonts on one line

Each font sits at its own height inside its line box. To put words in
different fonts on one line so that their letters stand on the same
baseline, use `baseline(font)`, the distance from the top of a line box
to the baseline:

```cpp
// Draw words in different fonts along one baseline, starting at x.
void draw_mixed(inkerface::surface &frame,
                inkerface::coord x,
                inkerface::coord baseline_y) {
    const std::array<std::pair<inkerface::font, std::string_view>, 3> words{{
        {inkerface::font::title(), "Big "},
        {inkerface::font::regular(), "and "},
        {inkerface::font::small(), "small"},
    }};
    for (const auto &[face, word] : words) {
        const auto top =
            static_cast<inkerface::coord>(baseline_y - frame.baseline(face));
        frame.draw_text(inkerface::point{x, top}, word, face);
        const inkerface::coord width = frame.measure_text(word, face).width;
        x = static_cast<inkerface::coord>(x + width);
    }
}
```

## Making a font

`tools/make_font.py` turns a TrueType or OpenType file into an inkerface
font. It uses FreeInk's own generator, so the result matches FreeInk's
fonts, and it needs Pillow (`pip install pillow`) on your PC only, not on
the device.

```sh
python3 tools/make_font.py --ttf NotoSerif-Regular.ttf --size 20 \
    --format ikf --out reading.ikf
```

| Option | Meaning |
| --- | --- |
| `--ttf` | the font file to convert |
| `--size` | pixel size (the em size; the line height comes out about 1.4 times larger) |
| `--first`, `--last` | the character range, default `0x20` to `0x7E` (ASCII); at most 256 characters |
| `--alpha` | anti-aliased 4-bit glyphs: smoother edges, but about three times larger, and dithered (slightly grainy) on the 1-bit panel |
| `--format ikf` | a font **file** to put on storage |
| `--format header` | a C++ **header** to compile the font into your program |
| `--name` | the variable name for `--format header` |

Sizes for a 24 px ASCII font: about 3 KB at 1 bit per pixel, about 11 KB
anti-aliased. Check the font's licence: it must allow embedding rasterized
glyphs. The SIL Open Font License, used by the Noto fonts, does.

## Loading a font

### From storage

```cpp
const inkerface::font reading = *inkerface::font::custom(0);
const inkerface::font_error loaded = screen.load_font(
    reading, disks, inkerface::volume::card, "fonts/reading.ikf");
```

The file is read, checked and copied into RAM. The program can then use
`reading` in any text call. Readers can thus pick their fonts at run
time: put several `.ikf` files on the card, list them with a `directory`
(chapter 9), and load the chosen one.

### Compiled into the program

```sh
python3 tools/make_font.py --ttf NotoSerif-Regular.ttf --size 20 \
    --format header --name serif_20 --out src/assets/serif_20_font.h
```

```cpp
#include "../assets/serif_20_font.h"   // defines serif_20_font

const inkerface::font serif = *inkerface::font::custom(1);
static_cast<void>(screen.load_font(serif, serif_20_font));
```

The header holds the same bytes as the file, as a `constexpr` array in
flash. `load_font()` copies them into RAM too. It is the same loader, so
the same checks apply.

### What can go wrong

`load_font()` returns a `font_error`. On any error the slot keeps the
font it had:

| Error | Meaning |
| --- | --- |
| `none` | loaded |
| `unreadable` | the file couldn't be read (missing, no card, ...) |
| `malformed` | not an inkerface font, or damaged |
| `too_large` | more than 256 characters, or more than 64 KB |

Font files are untrusted input, like any file a user can put on a card.
The loader checks every field and every character's bitmap before it
draws anything, so a damaged or hostile file is rejected instead of
crashing the program.

### Replacing and restoring fonts

Any slot can be loaded, including the built-in ones. To make the whole UI
use a different typeface, load it into `font::regular()`. Text styles
default to that slot, so everything switches at once.
`screen.reset_font(face)` gives a slot its built-in font back and frees
the loaded font's memory.

### Memory

| | Flash | RAM |
| --- | --- | --- |
| Built-in fonts | about 14 KB for all four | none |
| A loaded font | (the file, or the header's bytes) | about the file's size, e.g. 3 KB for 24 px ASCII |

With 4 custom slots of ASCII fonts, loaded fonts cost at most a few tens
of kilobytes. Free a slot with `reset_font()` when a font is no longer
needed.

## Example: a font sheet

This program shows every built-in font, a line mixing three fonts on one
baseline, and, if the card has `fonts/reading.ikf`, a paragraph in that
font. Replace `src/app/main.cpp`:

File: `src/app/main.cpp`

```cpp
//
// Shows the built-in fonts, three fonts sharing one baseline, and a
// paragraph in a font loaded from the card, if there is one.
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
#include <inkerface/storage.h>
#include <inkerface/surface.h>

#include <array>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace {

using inkerface::coord;
using inkerface::font;
using inkerface::point;

constexpr coord margin = 20;

coord to_coord(int value) {
    return static_cast<coord>(value);
}

// Draw words in different fonts along one baseline, starting at x.
void draw_mixed(inkerface::surface &frame, coord x, coord baseline_y) {
    const std::array<std::pair<font, std::string_view>, 3> words{{
        {font::title(), "Big "},
        {font::regular(), "and "},
        {font::small(), "small"},
    }};
    for (const auto &[face, word] : words) {
        frame.draw_text(point{x, to_coord(baseline_y - frame.baseline(face))},
                        word,
                        face);
        x = to_coord(x + frame.measure_text(word, face).width);
    }
}

} // namespace

int program(int /*argc*/, char ** /*argv*/) {
    inkerface::display screen;
    inkerface::input buttons(screen);
    inkerface::storage disks;
    inkerface::surface frame = screen.frame();

    const font reading = *font::custom(0);
    const inkerface::font_error loaded =
        screen.load_font(reading, disks, inkerface::volume::card,
                         "fonts/reading.ikf");

    frame.clear(inkerface::color::white);
    coord y = margin;
    const std::array<std::pair<font, std::string_view>, 4> samples{{
        {font::small(), "small: Noto Sans 16 px"},
        {font::regular(), "regular: Noto Sans 24 px"},
        {font::bold(), "bold: Noto Sans Bold 24 px"},
        {font::title(), "title: Bold 32 px"},
    }};
    for (const auto &[face, text] : samples) {
        frame.draw_text(point{margin, y}, text, face);
        y = to_coord(y + frame.line_height(face) + 8);
    }

    y = to_coord(y + 2 * margin);
    draw_mixed(frame, margin, to_coord(y + frame.baseline(font::title())));
    y = to_coord(y + frame.line_height(font::title()) + 2 * margin);

    inkerface::text_style paragraph;
    paragraph.max_lines = 8;
    const coord width = to_coord(frame.extent().width - 2 * margin);
    if (loaded == inkerface::font_error::none) {
        paragraph.face = reading;
        frame.draw_text(
            inkerface::rect{
                margin, y, width, to_coord(8 * frame.line_height(reading))},
            "This paragraph uses the font from fonts/reading.ikf on the "
            "card. Make your own with tools/make_font.py and copy it there.",
            paragraph);
    } else {
        paragraph.face = font::small();
        const coord height = to_coord(4 * frame.line_height(paragraph.face));
        frame.draw_text(
            inkerface::rect{margin, y, width, height},
            "No fonts/reading.ikf on the card: make one with "
            "tools/make_font.py to see it here.",
            paragraph);
    }

    if (!screen.present(inkerface::refresh_mode::full)) {
        return EXIT_FAILURE;
    }
    while (!buttons.closed()) {
        static_cast<void>(buttons.poll());
    }
    return EXIT_SUCCESS;
}
```

To try it with a reading font on the desktop:

```sh
make storage-images
export INKERFACE_STORAGE_ROOT="$PWD/build/storage"
mkdir -p bin/fonts
python3 tools/make_font.py \
    --ttf /usr/share/fonts/truetype/noto/NotoSerif-Regular.ttf \
    --size 22 --out bin/fonts/reading.ikf
bin/debug/inkerface_storage mkdir card fonts
bin/debug/inkerface_storage put card bin/fonts/reading.ikf fonts/reading.ikf
make preview
```

## For maintainers: the built-in fonts

The built-in fonts are generated headers in
`lib/inkerface/src/freeink/fonts/`, committed to the repository, so
building needs neither Pillow nor the TTF files. `make fonts` regenerates
them from the Noto Sans files in `NOTO` (default
`/usr/share/fonts/truetype/noto`). Different versions of a TTF file or of
Pillow can shift single pixels, so regenerate only when you mean to
change the fonts. The regular font is FreeInk's own bundled Noto Sans
24 px.

## Summary

- Four built-in bitmap fonts, `small`, `regular`, `bold` and `title`, in
  ASCII, cost no RAM.
- `draw_text(point, ...)` draws a single line at a position, and
  `baseline()` aligns fonts on one line.
- `tools/make_font.py` makes font files (`--format ikf`) and compiled-in
  headers (`--format header`), ASCII by default and at most 256
  characters.
- `display::load_font()` loads them into any slot, from storage or from
  memory. The data is validated and copied, and on failure the slot is
  unchanged. `reset_font()` restores a slot.

Back to the [manual contents](../PROGRAMMERS-MANUAL.md).
