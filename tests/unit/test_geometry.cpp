//
// Tests the platform geometry value types, including edge arithmetic at
// the limits of the coordinate type.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include <inkerface/geometry.h>

#include <limits>

namespace tests {

void run_geometry_tests(test_context &context) {
    using inkerface::coord;
    using inkerface::point;
    using inkerface::rect;

    constexpr rect box{10, 20, 30, 40};
    static_assert(box.right() == 40);
    static_assert(box.bottom() == 60);

    context.check(!box.empty(), "non-empty rect");
    context.check(rect{0, 0, 0, 5}.empty(), "zero width is empty");
    context.check(rect{0, 0, 5, -1}.empty(), "negative height is empty");

    context.check(box.contains(point{10, 20}), "top-left corner inside");
    context.check(box.contains(point{39, 59}), "last pixel inside");
    context.check(!box.contains(point{40, 59}), "right edge outside");
    context.check(!box.contains(point{39, 60}), "bottom edge outside");
    context.check(!rect{}.contains(point{0, 0}), "empty rect has no pixels");

    constexpr coord max = std::numeric_limits<coord>::max();
    constexpr rect edge{max, max, max, max};
    context.check(edge.right() == 2 * static_cast<std::int32_t>(max),
                  "right() does not overflow");
}

} // namespace tests
