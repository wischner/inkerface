//
// Declares the application: it owns the screen stack, runs the main loop,
// routes input to the top screen, and performs screen switching. It sees
// only the inkerface platform API, never the backend.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstddef>
#include <variant>
#include <vector>

#include <inkerface/display.h>
#include <inkerface/input.h>

#include "../screens/hello_world_screen.h"
#include "../screens/screen.h"

namespace app {

// Every screen type the application can hold. Add new screens here.
using any_screen = std::variant<ui::hello_world_screen>;

// The application and its main loop.
//
// Notes:
//     The display and input are borrowed and must outlive the
//     application. The screen stack is never empty; the bottom screen
//     cannot be closed.
class application {
public:
    // Deepest allowed screen stack; open requests beyond it are ignored.
    static constexpr std::size_t max_screen_depth = 8;

    //
    // Create the application with the startup screen on its stack.
    //
    // Parameters:
    //     out - Display to paint on.
    //     in  - Source of semantic input.
    //
    application(inkerface::display &out, inkerface::input &in);

    //
    // Paint the startup screen and run until input closes.
    //
    // Returns:
    //     EXIT_SUCCESS when input closed normally, EXIT_FAILURE when a
    //     frame could not be presented.
    //
    [[nodiscard]] int run();

    //
    // Apply one input snapshot to the top screen without presenting.
    //
    // Returns:
    //     True when the visible screen changed and must be repainted.
    //
    [[nodiscard]] bool dispatch(const inkerface::input_snapshot &in);

    // Paint the top screen into the frame without presenting it.
    void paint();

    // Return the number of screens on the stack.
    [[nodiscard]] std::size_t depth() const;

private:
    [[nodiscard]] static any_screen make_screen(ui::screen_id id);

    inkerface::display *display_;
    inkerface::input *input_;
    std::vector<any_screen> stack_;
};

} // namespace app
