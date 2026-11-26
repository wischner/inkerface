# 7. Testing

The desktop build draws exactly the pixels the X4 would draw, so most of
your application can be tested on your own machine in milliseconds. In
this chapter you test the counter and the menu at three levels: behavior,
pixels, and a complete scripted run.

## What already exists

```sh
make test
```

`make test` runs three kinds of test:

| Test | Checks |
| --- | --- |
| `unit` | the program `build/debug/inkerface_tests`, built from `tests/unit/*.cpp` |
| `isolation` | that no FreeInk or Arduino header is used in `include/`, `src/` or `tests/` |
| `hello_world_frame` | that the real program starts, presents and writes a frame |

To run a single test from the build folder, use `ctest --test-dir
build/debug -R unit --output-on-failure`.

The unit tests use a small harness in `tests/unit/check.h`, so no test
framework needs to be installed:

```cpp
void run_something_tests(test_context &context) {
    context.check(1 + 1 == 2, "arithmetic works");
}
```

A failed check prints its file, line and description. The program exits
with an error if any check failed.

## Three levels of screen tests

1. **Behavior.** Call `handle()` with a snapshot and check the returned
   `screen_result` and the screen's state. This needs no display.
2. **Pixels.** Create an `inkerface::display` (on the desktop it is an
   in-memory panel), `paint()` into its frame, and read pixels back with
   `surface::pixel_at()`.
3. **End to end.** Run the real program with scripted standard input and
   check what it wrote.

For pixel checks, prefer **comparisons** over exact coordinates. "The
corners stay white" and "focus changes the picture" survive a layout
tweak. "Pixel (37, 112) is black" breaks as soon as you move something.

## Testing the counter and the menu

File: `tests/unit/test_screens.cpp`

```cpp
//
// Tests the counter and menu screens directly, without the main loop:
// behavior through handle(), and drawing through the host display.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include "../../src/screens/counter_screen.h"
#include "../../src/screens/menu_screen.h"

#include <inkerface/color.h>
#include <inkerface/display.h>
#include <inkerface/geometry.h>
#include <inkerface/input.h>
#include <inkerface/surface.h>

#include <vector>

namespace tests {

namespace {

using inkerface::color;
using inkerface::input_snapshot;
using inkerface::point;
using ui::navigation;

// Copy every pixel of the frame, so two paintings can be compared.
std::vector<bool> capture(const inkerface::surface &frame) {
    const inkerface::size whole = frame.extent();
    std::vector<bool> ink;
    for (inkerface::coord y = 0; y < whole.height; ++y) {
        for (inkerface::coord x = 0; x < whole.width; ++x) {
            ink.push_back(frame.pixel_at(point{x, y}) == color::black);
        }
    }
    return ink;
}

input_snapshot confirm() {
    input_snapshot in;
    in.confirm = true;
    return in;
}

input_snapshot page_prev() {
    input_snapshot in;
    in.page_prev = true;
    return in;
}

input_snapshot back() {
    input_snapshot in;
    in.back = true;
    return in;
}

input_snapshot focus_next() {
    input_snapshot in;
    in.focus_next = true;
    return in;
}

void counter_behavior(test_context &context) {
    ui::counter_screen counter;
    context.check(counter.handle(page_prev()).action == navigation::stay,
                  "counter cannot go below the minimum");
    context.check(counter.value() == ui::counter_screen::min_count,
                  "counter starts at the minimum");
    context.check(counter.handle(confirm()).action == navigation::repaint,
                  "confirm asks for a repaint");
    context.check(counter.value() == 1, "confirm adds one");
    context.check(counter.handle(input_snapshot{}).action == navigation::stay,
                  "an empty snapshot changes nothing");
    context.check(counter.handle(back()).action == navigation::close,
                  "back closes the counter");

    ui::counter_screen full;
    for (int i = 0; i < ui::counter_screen::max_count; ++i) {
        static_cast<void>(full.handle(confirm()));
    }
    context.check(full.value() == ui::counter_screen::max_count,
                  "counter reaches the maximum");
    context.check(full.handle(confirm()).action == navigation::stay,
                  "counter stops at the maximum");
}

void counter_drawing(test_context &context) {
    inkerface::display screen;
    inkerface::surface frame = screen.frame();
    const inkerface::size whole = frame.extent();

    // Start from black, to prove paint() clears the page.
    frame.clear(color::black);
    ui::counter_screen counter;
    counter.paint(frame);
    context.check(frame.pixel_at(point{0, 0}) == color::white,
                  "counter clears the page");
    const std::vector<bool> at_zero = capture(frame);

    static_cast<void>(counter.handle(confirm()));
    counter.paint(frame);
    context.check(capture(frame) != at_zero, "a new count draws differently");
    context.check(frame.pixel_at(point{static_cast<inkerface::coord>(
                                           whole.width - 1),
                                       0}) == color::white,
                  "counter leaves the top-right corner white");
}

void menu_behavior_and_drawing(test_context &context) {
    ui::menu_screen menu;
    const std::size_t rows = ui::menu_screen::entries.size();

    inkerface::display screen;
    inkerface::surface frame = screen.frame();
    menu.paint(frame);
    const std::vector<bool> first_focused = capture(frame);

    context.check(menu.handle(focus_next()).action == navigation::repaint,
                  "focus_next asks for a repaint");
    context.check(menu.focus() == 1, "focus moves down");
    menu.paint(frame);
    context.check(capture(frame) != first_focused,
                  "the highlight follows the focus");

    for (std::size_t i = 1; i < rows; ++i) {
        static_cast<void>(menu.handle(focus_next()));
    }
    context.check(menu.focus() == 0, "focus wraps to the first row");

    const ui::screen_result opened = menu.handle(confirm());
    context.check(opened.action == navigation::open, "confirm opens");
    context.check(opened.target == ui::menu_screen::entries.at(0).target,
                  "confirm opens the focused entry");
}

} // namespace

void run_screen_tests(test_context &context) {
    counter_behavior(context);
    counter_drawing(context);
    menu_behavior_and_drawing(context);
}

} // namespace tests
```

Register the new test function in three places:

1. Declare it in `tests/unit/check.h`, next to the others:

   ```cpp
   void run_screen_tests(test_context &context);
   ```

2. Call it in `tests/unit/test_main.cpp`:

   ```cpp
   tests::run_screen_tests(context);
   ```

3. Add the file to `inkerface_tests` in `CMakeLists.txt`:

   ```cmake
   add_executable(inkerface_tests
       tests/unit/test_main.cpp
       # ... the existing test files ...
       tests/unit/test_screens.cpp)   # yours, added as the last entry
   ```

Then run `make test`.

Notice what the tests cover beyond the obvious: the lower and upper
limits, an empty snapshot, and the wrap-around of the focus. Bugs hide at
these edges.

## A scripted end-to-end test

The terminal input (chapter 4) makes the whole program scriptable. This
test opens the counter from the menu, counts to two, and checks that a
frame was written. Add it inside the `if(BUILD_TESTING)` block of
`CMakeLists.txt`:

```cmake
    add_test(NAME counter_script
        COMMAND sh -c "printf 'confirm\\nconfirm\\nconfirm\\n' | INKERFACE_FRAME_PATH='${CMAKE_BINARY_DIR}/counter.pbm' '$<TARGET_FILE:inkerface>' && test -s '${CMAKE_BINARY_DIR}/counter.pbm'")
```

Use these tests sparingly. They show that the parts fit together, but
when one fails it doesn't tell you *what* broke. Unit tests do.

## When a pixel test fails

Look at the picture. Run the same sequence by hand and write the frame
to a file:

```sh
printf 'confirm\nconfirm\n' | INKERFACE_FRAME_PATH=debug.pbm build/debug/inkerface
```

Then open `debug.pbm`. To step through the code instead, choose
**inkerface tests (host)** in VS Code's Run and Debug and press F5.

## What host tests can't tell you

The desktop build shares the drawing code with the device, but not the
hardware. It can't show refresh speed, ghosting, how the buttons feel, or
how much memory is used. Check those on the device (chapter 8).

## Summary

- Test behavior through `handle()`, pixels through `display::frame()` and
  `pixel_at()`, and the whole program through scripted standard input.
- Compare pictures rather than hard-coding coordinates.
- Test the edges: limits, empty input, wrap-around.
- Register a test file in `check.h`, `test_main.cpp` and
  `CMakeLists.txt`.

Next: [Running on the Xteink X4](08-RUNNING-ON-THE-X4.md).
