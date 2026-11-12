//
// Implements font slots: the built-in fonts (compiled-in FreeInk bitmap
// fonts, no RAM), and loading inkerface font files (IKF1) into a slot.
// Loading is a trust boundary: every header field and every glyph's
// bitmap range is checked before FreeInk sees the data.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "canvas.h"

#include "fonts/bold_font.h"
#include "fonts/small_font.h"
#include "fonts/title_font.h"

#include <FreeInkUIFont.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace inkerface::detail {

namespace {

constexpr std::array<std::uint8_t, 4> magic{'I', 'K', 'F', '1'};
constexpr std::size_t header_size = 16;
constexpr std::size_t glyph_record_size = 7;
constexpr std::size_t max_glyphs = 256;

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8U));
}

// The built-in font of a slot: regular, small, bold, title; the custom
// slots start with regular.
const freeink::ui::BitmapFont &built_in(std::uint8_t slot) {
    switch (slot) {
    case 1:
        return fonts::small_font;
    case 2:
        return fonts::bold_font;
    case 3:
        return fonts::title_font;
    default:
        return freeink::ui::kNotoSansFont;
    }
}

// Validate and copy IKF1 data; nullptr with a reason when rejected.
std::unique_ptr<loaded_font> parse(std::span<const std::uint8_t> data,
                                   font_error &reason) {
    reason = font_error::malformed;
    if (data.size() > max_font_bytes) {
        reason = font_error::too_large;
        return nullptr;
    }
    if (data.size() < header_size ||
        !std::equal(magic.begin(), magic.end(), data.begin())) {
        return nullptr;
    }
    const std::uint16_t first = read_u16(data, 4);
    const std::uint16_t last = read_u16(data, 6);
    const std::uint8_t line_height = data[8];
    const std::uint8_t ascent = data[9];
    const std::uint8_t widest = data[10];
    const std::uint8_t tallest = data[11];
    const std::uint8_t depth = data[12];
    const std::uint16_t bitmap_size = read_u16(data, 14);
    if (last < first || data[13] != 0 || (depth != 1 && depth != 4) ||
        line_height == 0 || ascent > line_height) {
        return nullptr;
    }
    const std::size_t count = std::size_t{last} - first + 1;
    if (count > max_glyphs) {
        reason = font_error::too_large;
        return nullptr;
    }
    const std::size_t glyphs_end = header_size + count * glyph_record_size;
    if (data.size() != glyphs_end + bitmap_size) {
        return nullptr; // truncated, or trailing bytes
    }

    auto loaded = std::make_unique<loaded_font>();
    const auto bitmap_start =
        data.begin() + static_cast<std::ptrdiff_t>(glyphs_end);
    loaded->bitmap.assign(bitmap_start, data.end());
    loaded->glyphs.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = header_size + i * glyph_record_size;
        const std::uint16_t offset = read_u16(data, at);
        const std::uint8_t width = data[at + 2];
        const std::uint8_t height = data[at + 3];
        const std::size_t bits = std::size_t{width} * height * depth;
        if (width > widest || height > tallest ||
            std::size_t{offset} + (bits + 7) / 8 > bitmap_size) {
            return nullptr; // glyph outside the bitmap
        }
        loaded->glyphs[i] = freeink::ui::FontGlyph{
            offset,
            width,
            height,
            data[at + 4],
            static_cast<std::int8_t>(data[at + 5]),
            static_cast<std::int8_t>(data[at + 6])};
    }
    loaded->font = freeink::ui::BitmapFont{loaded->bitmap.data(),
                                           loaded->glyphs.data(),
                                           first,
                                           last,
                                           line_height,
                                           ascent,
                                           widest,
                                           tallest,
                                           depth};
    reason = font_error::none;
    return loaded;
}

} // namespace

font_error canvas::load_font(std::uint8_t slot,
                             std::span<const std::uint8_t> data) {
    if (slot >= font_slot_count) {
        return font_error::malformed;
    }
    font_error reason = font_error::none;
    std::unique_ptr<loaded_font> loaded = parse(data, reason);
    if (!loaded) {
        return reason;
    }
    target_.setFont(slot, loaded->font);
    fonts_.at(slot) = &loaded->font;
    loaded_.at(slot) = std::move(loaded); // frees any previous one
    return font_error::none;
}

void canvas::reset_font(std::uint8_t slot) {
    if (slot >= font_slot_count) {
        return;
    }
    const freeink::ui::BitmapFont &font = built_in(slot);
    target_.setFont(slot, font);
    fonts_.at(slot) = &font;
    loaded_.at(slot).reset();
}

const freeink::ui::BitmapFont &canvas::font_in(std::uint8_t slot) const {
    return *fonts_.at(slot < font_slot_count ? slot : 0);
}

} // namespace inkerface::detail
