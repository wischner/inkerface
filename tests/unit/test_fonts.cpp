//
// Tests the font set: built-in fonts, loading inkerface fonts (IKF1)
// from memory and from storage, the loader's validation of malformed
// data, replacing and resetting slots, and the single-line text and
// baseline drawing functions. Test fonts are built here byte by byte, so
// no TrueType files are needed.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include <inkerface/color.h>
#include <inkerface/display.h>
#include <inkerface/file.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>
#include <inkerface/storage.h>
#include <inkerface/surface.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

namespace tests {

namespace {

using inkerface::color;
using inkerface::font;
using inkerface::font_error;
using inkerface::point;
using bytes = std::vector<std::uint8_t>;

void put_u16(bytes &data, std::size_t at, std::uint16_t value) {
    data.at(at) = static_cast<std::uint8_t>(value & 0xFFU);
    data.at(at + 1) = static_cast<std::uint8_t>(value >> 8U);
}

// A valid two-glyph font: 'A' is a solid 4x6 block (advance 5), 'B' a
// 2x2 block (advance 3); line height 10, baseline 8.
bytes tiny_font() {
    bytes data{'I', 'K', 'F', '1', 0, 0, 0, 0, 10, 8, 4, 6, 1, 0, 0, 0};
    put_u16(data, 4, 'A');
    put_u16(data, 6, 'B');
    put_u16(data, 14, 4); // bitmap bytes
    // offset, width, height, advance, x offset, y offset (-6: above)
    const bytes glyph_a{0, 0, 4, 6, 5, 0, static_cast<std::uint8_t>(-6)};
    const bytes glyph_b{3, 0, 2, 2, 3, 0, static_cast<std::uint8_t>(-2)};
    data.insert(data.end(), glyph_a.begin(), glyph_a.end());
    data.insert(data.end(), glyph_b.begin(), glyph_b.end());
    const bytes bitmap{0xFF, 0xFF, 0xFF, 0xF0};
    data.insert(data.end(), bitmap.begin(), bitmap.end());
    return data;
}

int black_pixels(const inkerface::surface &frame) {
    int count = 0;
    const inkerface::size whole = frame.extent();
    for (inkerface::coord y = 0; y < whole.height; ++y) {
        for (inkerface::coord x = 0; x < whole.width; ++x) {
            count += frame.pixel_at(point{x, y}) == color::black ? 1 : 0;
        }
    }
    return count;
}

void built_in_fonts(test_context &context, inkerface::display &screen) {
    const inkerface::surface frame = screen.frame();
    context.check(frame.line_height(font::small()) <
                          frame.line_height(font::regular()) &&
                      frame.line_height(font::regular()) <
                          frame.line_height(font::title()),
                  "built-in sizes grow from small to title");
    context.check(frame.measure_text("Hello", font::bold()).width >
                      frame.measure_text("Hello", font::regular()).width,
                  "bold text is wider than regular");
    context.check(frame.baseline(font::regular()) > 0 &&
                      frame.baseline(font::regular()) <
                          frame.line_height(font::regular()),
                  "the baseline lies inside the line");
    const font first_custom = *font::custom(0);
    context.check(first_custom.slot() == 4 && !font::custom(4) &&
                      first_custom != font::regular(),
                  "four custom slots follow the built-ins");
    context.check(frame.measure_text("Hello", first_custom).width ==
                      frame.measure_text("Hello", font::regular()).width,
                  "an empty custom slot draws with the regular font");
}

void loading(test_context &context, inkerface::display &screen) {
    inkerface::surface frame = screen.frame();
    const font mine = *font::custom(0);
    context.check(screen.load_font(mine, tiny_font()) == font_error::none,
                  "a valid font loads");
    context.check(frame.line_height(mine) == 10 && frame.baseline(mine) == 8,
                  "the loaded font's metrics are used");
    context.check(frame.measure_text("AB", mine).width == 8,
                  "the loaded font's advances are used");

    frame.clear(color::white);
    frame.draw_text(point{10, 10}, "A", mine);
    context.check(black_pixels(frame) == 24, "the glyph draws all its ink");
    context.check(frame.pixel_at(point{10, 12}) == color::black &&
                      frame.pixel_at(point{13, 17}) == color::black &&
                      frame.pixel_at(point{14, 17}) == color::white &&
                      frame.pixel_at(point{10, 18}) == color::white,
                  "draw_text(point) places the glyph from the line top");

    const font other = *font::custom(1);
    const auto rejected = [&](bytes data) {
        return screen.load_font(other, data);
    };
    bytes bad = tiny_font();
    bad.at(0) = 'X';
    context.check(rejected(bad) == font_error::malformed, "bad magic");
    bad = tiny_font();
    bad.pop_back();
    context.check(rejected(bad) == font_error::malformed, "truncated");
    bad = tiny_font();
    bad.push_back(0);
    context.check(rejected(bad) == font_error::malformed, "trailing bytes");
    bad = tiny_font();
    put_u16(bad, 6, 'A' - 1);
    context.check(rejected(bad) == font_error::malformed, "last before first");
    bad = tiny_font();
    bad.at(12) = 3;
    context.check(rejected(bad) == font_error::malformed, "bad bit depth");
    bad = tiny_font();
    bad.at(9) = 11;
    context.check(rejected(bad) == font_error::malformed, "ascent > line");
    bad = tiny_font();
    bad.at(16 + 7) = 4; // glyph 'B' starts at the bitmap's end
    context.check(rejected(bad) == font_error::malformed,
                  "a glyph outside the bitmap");
    bad = tiny_font();
    bad.at(16 + 2) = 5; // 'A' wider than the widest glyph
    context.check(rejected(bad) == font_error::malformed,
                  "a glyph wider than declared");
    bad = tiny_font();
    put_u16(bad, 4, 0x20);
    put_u16(bad, 6, 0x20 + 256);
    context.check(rejected(bad) == font_error::too_large,
                  "more than 256 glyphs");
    context.check(rejected(bytes(70 * 1024, 0)) == font_error::too_large,
                  "more than 64 KB");
    context.check(rejected({}) == font_error::malformed, "no data");
    context.check(frame.line_height(other) ==
                      frame.line_height(font::regular()),
                  "a rejected font leaves the slot unchanged");

    const inkerface::coord bold_height = frame.line_height(font::bold());
    context.check(screen.load_font(font::bold(), tiny_font()) ==
                          font_error::none &&
                      frame.line_height(font::bold()) == 10,
                  "a built-in slot can be replaced");
    screen.reset_font(font::bold());
    screen.reset_font(mine);
    context.check(frame.line_height(font::bold()) == bold_height &&
                      frame.line_height(mine) ==
                          frame.line_height(font::regular()),
                  "reset_font restores the built-in fonts");
}

void loading_from_storage(test_context &context, inkerface::display &screen) {
    std::string root =
        (std::filesystem::temp_directory_path() / "inkerface-fonts-XXXXXX")
            .string();
    if (::mkdtemp(root.data()) == nullptr) {
        context.check(false, "create a temporary storage root");
        return;
    }
    ::setenv("INKERFACE_STORAGE_ROOT", root.c_str(), 1);
    {
        inkerface::storage disks;
        const auto volume = inkerface::volume::internal;
        const bytes data = tiny_font();
        inkerface::file out(
            disks, volume, "tiny.ikf", inkerface::file_mode::write);
        static_cast<void>(out.write(std::as_bytes(std::span(data))));
        context.check(out.close(), "write a font file");
        const font slot = *font::custom(2);
        context.check(screen.load_font(slot, disks, volume, "tiny.ikf") ==
                              font_error::none &&
                          screen.frame().line_height(slot) == 10,
                      "a font file loads from storage");
        context.check(screen.load_font(slot, disks, volume, "none.ikf") ==
                          font_error::unreadable,
                      "a missing font file is unreadable");
        screen.reset_font(slot);
    }
    ::unsetenv("INKERFACE_STORAGE_ROOT");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

void single_line_text(test_context &context, inkerface::display &screen) {
    inkerface::surface frame = screen.frame();
    frame.clear(color::white);
    const point origin{100, 200};
    frame.draw_text(origin, "Hi there", font::title());
    const inkerface::size box = frame.measure_text("Hi there", font::title());
    int inside = 0;
    int outside = 0;
    const inkerface::size whole = frame.extent();
    for (inkerface::coord y = 0; y < whole.height; ++y) {
        for (inkerface::coord x = 0; x < whole.width; ++x) {
            if (frame.pixel_at(point{x, y}) != color::black) {
                continue;
            }
            const bool in = x >= origin.x && x < origin.x + box.width &&
                            y >= origin.y && y < origin.y + box.height;
            ++(in ? inside : outside);
        }
    }
    context.check(inside > 0 && outside == 0,
                  "single-line text stays in its measured box");
}

} // namespace

void run_font_tests(test_context &context) {
    inkerface::display screen;
    built_in_fonts(context, screen);
    loading(context, screen);
    loading_from_storage(context, screen);
    single_line_text(context, screen);
}

} // namespace tests
