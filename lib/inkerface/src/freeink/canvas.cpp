//
// Implements the FreeInk-backed canvas and the value conversions between
// platform types and FreeInk types.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "canvas.h"

#include <cstddef>
#include <stdexcept>

namespace inkerface::detail {

namespace {

// Our font slot count must match what the FreeInk target provides.
static_assert(font_slot_count == freeink::ui::DisplayTarget::FONT_SLOTS);

// Validate geometry before the FreeInk target is constructed over it.
panel_geometry checked(std::span<std::uint8_t> framebuffer,
                       panel_geometry geometry) {
    if (geometry.width <= 0 || geometry.height <= 0 ||
        geometry.width_bytes <= 0) {
        throw std::invalid_argument("panel geometry must be positive");
    }
    const auto row_bits = static_cast<std::size_t>(geometry.width_bytes) * 8U;
    if (row_bits < static_cast<std::size_t>(geometry.width)) {
        throw std::invalid_argument("panel row too short for its width");
    }
    // Both factors fit in 15 bits, so the product cannot overflow size_t.
    const std::size_t needed = static_cast<std::size_t>(geometry.width_bytes) *
                               static_cast<std::size_t>(geometry.height);
    if (framebuffer.size() < needed) {
        throw std::invalid_argument("framebuffer smaller than panel");
    }
    return geometry;
}

} // namespace

canvas::canvas(std::span<std::uint8_t> framebuffer, panel_geometry geometry)
    : framebuffer_(framebuffer)
    , geometry_(checked(framebuffer, geometry))
    , target_(framebuffer.data(),
              geometry.width,
              geometry.height,
              geometry.width_bytes,
              freeink::ui::Orientation::Portrait) {
    for (std::uint8_t slot = 0; slot < font_slot_count; ++slot) {
        reset_font(slot);
    }
}

freeink::ui::DisplayTarget &canvas::target() {
    return target_;
}

const freeink::ui::DisplayTarget &canvas::target() const {
    return target_;
}

size canvas::extent() const {
    return size{target_.logicalWidth(), target_.logicalHeight()};
}

std::optional<color> canvas::pixel_at(point p) const {
    const size logical = extent();
    if (p.x < 0 || p.y < 0 || p.x >= logical.width || p.y >= logical.height) {
        return std::nullopt;
    }
    // Inverse of DisplayTarget's Portrait mapping (90 degrees clockwise):
    // panel_x = logical_y, panel_y = panel_height - 1 - logical_x.
    const auto panel_x = static_cast<std::size_t>(p.y);
    const auto panel_y = static_cast<std::size_t>(geometry_.height - 1 - p.x);
    const std::size_t index =
        panel_y * static_cast<std::size_t>(geometry_.width_bytes) +
        panel_x / 8U;
    const auto mask = static_cast<std::uint8_t>(0x80U >> (panel_x % 8U));
    // index is in bounds: the constructor checked the buffer size.
    const bool paper = (framebuffer_[index] & mask) != 0U;
    return paper ? color::white : color::black;
}

freeink::ui::Color to_freeink(color value) {
    switch (value) {
    case color::white:
        return freeink::ui::Color::White;
    case color::light_gray:
        return freeink::ui::Color::LightGray;
    case color::dark_gray:
        return freeink::ui::Color::DarkGray;
    case color::black:
        return freeink::ui::Color::Black;
    }
    return freeink::ui::Color::Black;
}

freeink::ui::Rect to_freeink(const rect &value) {
    return freeink::ui::Rect{value.x, value.y, value.width, value.height};
}

freeink::ui::Point to_freeink(point value) {
    return freeink::ui::Point{value.x, value.y};
}

freeink::ui::FontId to_freeink(font value) {
    return freeink::ui::FontId{value.slot()};
}

} // namespace inkerface::detail
