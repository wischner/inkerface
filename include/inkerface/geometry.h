//
// Declares backend-neutral coordinates, sizes, points, and rectangles.
// Coordinates are logical display pixels with the origin at the top-left
// corner. Edge arithmetic is widened to 32 bits so it cannot overflow.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstdint>

namespace inkerface {

// Logical display coordinate or dimension, in pixels.
using coord = std::int16_t;

// A position in logical display pixels.
struct point {
    coord x = 0;
    coord y = 0;
};

// A width and height in logical display pixels.
struct size {
    coord width = 0;
    coord height = 0;
};

// An axis-aligned rectangle in logical display pixels.
//
// Notes:
//     A rectangle with a non-positive width or height is empty. Edges are
//     half-open: the pixel at (right(), y) is outside the rectangle.
struct rect {
    coord x = 0;
    coord y = 0;
    coord width = 0;
    coord height = 0;

    // Return true when the rectangle covers no pixels.
    [[nodiscard]] constexpr bool empty() const {
        return width <= 0 || height <= 0;
    }

    // Return the first column to the right of the rectangle.
    [[nodiscard]] constexpr std::int32_t right() const {
        return std::int32_t{x} + std::int32_t{width};
    }

    // Return the first row below the rectangle.
    [[nodiscard]] constexpr std::int32_t bottom() const {
        return std::int32_t{y} + std::int32_t{height};
    }

    // Return true when the pixel at p lies inside the rectangle.
    [[nodiscard]] constexpr bool contains(point p) const {
        return !empty() && p.x >= x && p.y >= y && p.x < right() &&
               p.y < bottom();
    }

    // Return the rectangle with its origin and extent as a size.
    [[nodiscard]] constexpr inkerface::size extent() const {
        return inkerface::size{width, height};
    }
};

} // namespace inkerface
