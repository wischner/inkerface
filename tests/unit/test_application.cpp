//
// Tests the application's screen stack and dispatch without running the
// blocking main loop.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include "../../src/app/app.h"

#include <inkerface/display.h>
#include <inkerface/input.h>

namespace tests {

void run_application_tests(test_context &context) {
    inkerface::display screen;
    inkerface::input buttons;
    app::application application(screen, buttons);

    context.check(application.depth() == 1, "starts with one screen");

    inkerface::input_snapshot press;
    press.back = true;
    context.check(!application.dispatch(press), "back on root changes nothing");
    context.check(application.depth() == 1, "root screen is never closed");

    press = inkerface::input_snapshot{};
    press.focus_next = true;
    context.check(!application.dispatch(press), "focus on hello world");

    context.check(!inkerface::input_snapshot{}.any(), "empty snapshot");
    context.check(press.any(), "non-empty snapshot");

    application.paint();
    context.check(screen.frame().pixel_at(inkerface::point{0, 0}) ==
                      inkerface::color::white,
                  "application paints the top screen");
}

} // namespace tests
