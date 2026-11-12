//
// Declares the startup screen: a centered logo with "Hello World" below
// it on a cleared page. It uses only the inkerface platform API.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <string_view>

#include <inkerface/input.h>
#include <inkerface/surface.h>

#include "screen.h"

namespace ui {

// The first screen shown at startup.
class hello_world_screen {
public:
    // Text the screen displays.
    static constexpr std::string_view greeting = "Hello World";

    // Clear to white and center the logo and greeting as one group.
    void paint(inkerface::surface &out) const;

    // The screen has no interactive state; every input is ignored.
    [[nodiscard]] screen_result handle(const inkerface::input_snapshot &in);
};

static_assert(screen<hello_world_screen>);

} // namespace ui
