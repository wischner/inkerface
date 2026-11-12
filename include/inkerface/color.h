//
// Declares the grayscale levels the drawing API accepts. E-paper targets
// are 1-bit; gray levels are reproduced with an ordered dither by the
// backend, so only white and black read back as exact pixel values.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstdint>

namespace inkerface {

// Grayscale ink level, ordered from paper to full ink.
enum class color : std::uint8_t {
    white,
    light_gray,
    dark_gray,
    black
};

} // namespace inkerface
