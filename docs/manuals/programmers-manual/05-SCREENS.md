# 5. Screens

The counter from chapter 4 mixes three jobs in one function: owning the
loop, keeping the state, and drawing. Real applications have many pages,
and each of those pages would repeat the loop. In this chapter you move
the counter into a **screen** and let the **application** own the loop.

## What a screen is

A screen is a plain class with two member functions:

```cpp
void paint(inkerface::surface &out) const;                 // draw yourself
screen_result handle(const inkerface::input_snapshot &in); // react to input
```

There is no base class and nothing to inherit. The `ui::screen` concept in
`src/screens/screen.h` checks these two functions at compile time:

```cpp
static_assert(ui::screen<my_screen>);  // a clear error if one is missing
```

`handle()` returns a `screen_result` that tells the application what to
do next:

| Return | Meaning |
| --- | --- |
| `screen_result::stay()` | nothing changed, so don't refresh |
| `screen_result::repaint()` | my state changed, so redraw me |
| `screen_result::open(screen_id::x)` | open screen `x` on top of me |
| `screen_result::close()` | close me and go back to the screen below |

The division of work is:

- **The screen** keeps its state, draws itself completely in `paint()`,
  and decides in `handle()`.
- **The application** (`app::application` in `src/app/`) polls input,
  calls the top screen's `handle()`, keeps the stack of open screens, and
  calls `paint()` and `present()` when needed. It uses a full refresh for
  the first frame and a fast refresh after that.

The application already exists, so you only write screens.

## The counter as a screen

Create the header:

File: `src/screens/counter_screen.h`

```cpp
//
// Declares the counter screen: confirm adds one, page_prev subtracts
// one, back closes the screen.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <inkerface/input.h>
#include <inkerface/surface.h>

#include "screen.h"

namespace ui {

// A bounded counter driven by confirm and page_prev.
class counter_screen {
public:
    static constexpr int min_count = 0;
    static constexpr int max_count = 9999;

    // Draw the current count centered, with a help line at the bottom.
    void paint(inkerface::surface &out) const;

    // Update the count, or ask to close on back.
    [[nodiscard]] screen_result handle(const inkerface::input_snapshot &in);

    // Return the current count.
    [[nodiscard]] int value() const;

private:
    int value_ = min_count;
};

static_assert(screen<counter_screen>);

} // namespace ui
```

Then the implementation:

File: `src/screens/counter_screen.cpp`

```cpp
//
// Implements the counter screen.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "counter_screen.h"

#include <inkerface/color.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>

#include <string>

namespace ui {

void counter_screen::paint(inkerface::surface &out) const {
    const inkerface::size whole = out.extent();
    const inkerface::coord line = out.line_height(inkerface::font::regular());
    inkerface::text_style centered;
    centered.align = inkerface::text_align::center;

    out.clear(inkerface::color::white);
    out.draw_text(inkerface::rect{0, 0, whole.width, whole.height},
                  "Count: " + std::to_string(value_),
                  centered);
    out.draw_text(
        inkerface::rect{0,
                        static_cast<inkerface::coord>(whole.height - 2 * line),
                        whole.width,
                        line},
        "confirm +1    page_prev -1    back",
        centered);
}

screen_result counter_screen::handle(const inkerface::input_snapshot &in) {
    if (in.back) {
        return screen_result::close();
    }
    const int before = value_;
    if (in.confirm && value_ < max_count) {
        ++value_;
    }
    if (in.page_prev && value_ > min_count) {
        --value_;
    }
    return value_ != before ? screen_result::repaint() : screen_result::stay();
}

int counter_screen::value() const {
    return value_;
}

} // namespace ui
```

Compare it with chapter 4. The drawing and the counting are the same, but
the loop, `display`, `input` and `present()` have all disappeared. The
screen only knows about the surface it gets and the snapshot it receives.

`back` now means "leave this screen". At the bottom of the stack there is
nothing to go back to, so the application ignores the request.

## Registering the screen

The application must know the new screen. Make four small changes.

**1. Give it an id** in `src/screens/screen.h`:

```cpp
enum class screen_id : std::uint8_t {
    hello_world,
    counter
};
```

**2. Add it to the screen list** in `src/app/app.h`:

```cpp
#include "../screens/counter_screen.h"
#include "../screens/hello_world_screen.h"
#include "../screens/screen.h"

// ...

using any_screen = std::variant<ui::hello_world_screen, ui::counter_screen>;
```

`any_screen` is a `std::variant`, so it can hold any one of these screens
without heap allocation. A `static_assert` in `app.cpp` checks that every
type in the list satisfies `ui::screen`.

**3. Construct it from its id**, and make it the startup screen, in
`src/app/app.cpp`:

```cpp
application::application(inkerface::display &out, inkerface::input &in)
    : display_(&out)
    , input_(&in) {
    stack_.reserve(max_screen_depth);
    stack_.push_back(make_screen(ui::screen_id::counter));  // startup screen
}

// ...

any_screen application::make_screen(ui::screen_id id) {
    switch (id) {
    case ui::screen_id::hello_world:
        return ui::hello_world_screen{};
    case ui::screen_id::counter:
        return ui::counter_screen{};
    }
    return ui::hello_world_screen{};
}
```

If you forget a `case`, G++ warns that the `switch` doesn't handle every
enumeration value. Because warnings are errors, the build stops.

**4. Add the source file** to `inkerface_app` in `CMakeLists.txt`:

```cmake
add_library(inkerface_app STATIC
    src/app/app.cpp
    src/screens/hello_world_screen.cpp
    src/screens/counter_screen.cpp)
```

## Back to the standard `program()`

Your `main.cpp` no longer needs a loop of its own. Put back the version
the project started with:

File: `src/app/main.cpp`

```cpp
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
```

Build it and run `make preview`. The counter behaves as before, but it is
now one screen among potentially many. From here on, `program()` stays
exactly like this. All new features are new screens.

## Rules for good screens

- **`paint()` draws everything.** Start with `clear()`, and draw only from
  your member variables. The application may call `paint()` at any time,
  for example after returning from another screen.
- **`paint()` is `const`.** Drawing must never change your state.
- **`handle()` returns `stay()` when nothing changed.** Every `repaint()`
  costs a visible refresh.
- **Don't keep the surface.** It is lent to you only during `paint()`.
- **Use only `<inkerface/...>`** and your own headers.

## Summary

- A screen is a class with `paint()` and `handle()`. The `ui::screen`
  concept checks it.
- `handle()` returns `stay`, `repaint`, `open` or `close`.
- The application owns the loop, the screen stack, and the refreshes.
- To register a screen, give it an id, add it to `any_screen`, add a case
  to `make_screen`, and add the file to `CMakeLists.txt`.

Next: [Several screens](06-SEVERAL-SCREENS.md).
