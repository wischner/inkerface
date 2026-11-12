//
// Implements the hello world screen.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "hello_world_screen.h"

#include "../assets/logo.h"

#include <inkerface/color.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>

namespace ui {

void hello_world_screen::paint(inkerface::surface &out) const {
    out.clear(inkerface::color::white);
    const inkerface::size whole = out.extent();
    constexpr inkerface::coord gap = 24;
    const auto text_height = out.line_height(inkerface::font::regular());
    const auto top = static_cast<inkerface::coord>(
        (whole.height - assets::logo_size.height - gap - text_height) / 2);
    const auto left = static_cast<inkerface::coord>(
        (whole.width - assets::logo_size.width) / 2);
    out.draw_bitmap({left, top}, assets::logo_size, assets::logo_bits);
    inkerface::text_style style;
    style.align = inkerface::text_align::center;
    out.draw_text(inkerface::rect{0,
                                  static_cast<inkerface::coord>(
                                      top + assets::logo_size.height + gap),
                                  whole.width,
                                  text_height},
                  greeting,
                  style);
}

screen_result
hello_world_screen::handle(const inkerface::input_snapshot & /*in*/) {
    return screen_result::stay();
}

} // namespace ui
