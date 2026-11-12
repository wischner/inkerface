//
// Implements inkerface::surface by forwarding to FreeInkUI's
// DisplayTarget. This file and canvas.cpp are the only translation units
// that translate platform drawing calls into FreeInk calls.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/surface.h>

#include "canvas.h"

#include <string>
#include <stdexcept>

namespace inkerface {

namespace {

// FreeInk takes NUL-terminated strings; copy so a view need not be.
std::string terminated(std::string_view text) {
    return std::string(text.substr(0, text.find('\0')));
}

freeink::ui::TextAlign to_freeink(text_align value) {
    switch (value) {
    case text_align::left:
        return freeink::ui::TextAlign::Left;
    case text_align::center:
        return freeink::ui::TextAlign::Center;
    case text_align::right:
        return freeink::ui::TextAlign::Right;
    }
    return freeink::ui::TextAlign::Left;
}

} // namespace

surface::surface(detail::canvas &target)
    : target_(&target) {}

size surface::extent() const {
    return target_->extent();
}

void surface::clear(color fill) {
    const size whole = extent();
    fill_rect(rect{0, 0, whole.width, whole.height}, fill);
}

void surface::fill_rect(const rect &area, color fill) {
    target_->target().fill(detail::to_freeink(area),
                           freeink::ui::Paint::solid(detail::to_freeink(fill)));
}

void surface::draw_rect(const rect &area, color ink, std::uint8_t thickness) {
    target_->target().stroke(detail::to_freeink(area),
                             freeink::ui::Paint::solid(detail::to_freeink(ink)),
                             thickness);
}

void surface::draw_line(point from,
                        point to,
                        color ink,
                        std::uint8_t thickness) {
    target_->target().line(detail::to_freeink(from),
                           detail::to_freeink(to),
                           thickness,
                           freeink::ui::Paint::solid(detail::to_freeink(ink)));
}

void surface::draw_bitmap(point origin,
                          size dimensions,
                          std::span<const std::uint8_t> data,
                          color ink) {
    if (dimensions.width <= 0 || dimensions.height <= 0) {
        throw std::invalid_argument("bitmap dimensions must be positive");
    }
    const auto row_bytes =
        (static_cast<std::size_t>(dimensions.width) + 7U) / 8U;
    // Positive coord dimensions fit in 15 bits, so this cannot overflow.
    const auto required =
        row_bytes * static_cast<std::size_t>(dimensions.height);
    if (data.size() < required) {
        throw std::invalid_argument("bitmap buffer is too small");
    }
    const freeink::ui::BitmapRef bitmap{
        data.data(),
        static_cast<std::uint16_t>(dimensions.width),
        static_cast<std::uint16_t>(dimensions.height),
        freeink::ui::BitmapFormat::BW1,
        false};
    target_->target().bitmap(
        detail::to_freeink(
            rect{origin.x, origin.y, dimensions.width, dimensions.height}),
        bitmap,
        freeink::ui::BitmapMode::Center,
        freeink::ui::Paint::solid(detail::to_freeink(ink)));
}

void surface::draw_text(const rect &area,
                        std::string_view text,
                        const text_style &style) {
    freeink::ui::TextStyle native_style;
    native_style.font = detail::to_freeink(style.face);
    native_style.color = detail::to_freeink(style.ink);
    native_style.align = to_freeink(style.align);
    native_style.maxLines = style.max_lines == 0 ? 1 : style.max_lines;
    const std::string copy = terminated(text);
    target_->target().text(
        detail::to_freeink(area), copy.c_str(), native_style);
}

void surface::draw_text(point origin,
                        std::string_view text,
                        font face,
                        color ink) {
    const std::string copy = terminated(text);
    freeink::ui::TextStyle native_style;
    native_style.font = detail::to_freeink(face);
    native_style.color = detail::to_freeink(ink);
    native_style.maxLines = 1;
    const freeink::ui::Size measured = target_->target().measureText(
        native_style.font, copy.c_str(), native_style);
    // The box is exactly as wide as the text, so nothing wraps or is cut
    // short; drawing past the frame edge is clipped.
    target_->target().text(
        freeink::ui::Rect{origin.x, origin.y, measured.width, measured.height},
        copy.c_str(),
        native_style);
}

coord surface::baseline(font face) const {
    return static_cast<coord>(target_->font_in(face.slot()).ascent);
}

size surface::measure_text(std::string_view text, font face) const {
    const std::string copy = terminated(text);
    const freeink::ui::Size measured = target_->target().measureText(
        detail::to_freeink(face), copy.c_str(), freeink::ui::TextStyle{});
    return size{measured.width, measured.height};
}

coord surface::line_height(font face) const {
    return target_->target().lineHeight(detail::to_freeink(face));
}

std::optional<color> surface::pixel_at(point p) const {
    return target_->pixel_at(p);
}

} // namespace inkerface
