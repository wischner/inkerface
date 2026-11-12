//
// Implements the portable program entry: create the platform objects and
// run the application. Identical on the host and on the device.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/display.h>
#include <inkerface/input.h>
#include <inkerface/program.h>

#include "app.h"

int program(int /*argc*/, char ** /*argv*/) {
    inkerface::display screen;
    inkerface::input buttons(screen);
    app::application application(screen, buttons);
    return application.run();
}
