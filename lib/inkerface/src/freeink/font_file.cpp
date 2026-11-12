//
// Implements display::load_font() from storage: reads a font file into
// memory (bounded by max_font_bytes) and loads it like compiled-in data.
// Shared by every platform; it uses only the public storage API.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/display.h>
#include <inkerface/file.h>

#include "canvas.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace inkerface {

font_error display::load_font(font face,
                              storage &disks,
                              volume where,
                              std::string_view path) {
    file in(disks, where, path, file_mode::read);
    if (!in) {
        return font_error::unreadable;
    }
    if (in.size() > detail::max_font_bytes) {
        return font_error::too_large;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(in.size()));
    if (in.read(std::as_writable_bytes(std::span(data))) != data.size()) {
        return font_error::unreadable;
    }
    return load_font(face, data);
}

} // namespace inkerface
