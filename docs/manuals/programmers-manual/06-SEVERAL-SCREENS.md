# 6. Several screens

A reader app is made of pages that lead to other pages: a library, a
book, settings. In this chapter you add a **menu** that opens the
counter and an **about** page, and you learn how the application moves
between screens.

## The screen stack

The application keeps open screens on a stack. Only the top screen gets
input and is drawn.

```text
start           confirm on "Counter"      back
┌─────────┐     ┌─────────┐               ┌─────────┐
│  menu   │     │ counter │ <- top        │  menu   │
└─────────┘     ├─────────┤               └─────────┘
                │  menu   │
                └─────────┘
```

- `screen_result::open(id)` pushes a *new* screen onto the stack.
- `screen_result::close()` pops the top screen, and the one below is
  painted again.
- The bottom screen can't be closed.
- The stack holds at most `application::max_screen_depth` (8) screens.
  Further `open` requests are ignored.

A closed screen is destroyed, together with its state. If you open the
counter again, it starts from zero.

## Focus

A menu needs a *highlight* (the *focus*): `focus_next` and `focus_prev`
move it, and `confirm` acts on the highlighted item. The focus is just a
member variable, an index, and `paint()` draws the focused row
differently. Black with white text reads well on e-paper.

## The menu screen

File: `src/screens/menu_screen.h`

```cpp
//
// Declares the main menu: a list of screens. focus_next and focus_prev
// move the highlight, confirm opens the highlighted screen.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include <inkerface/input.h>
#include <inkerface/surface.h>

#include "screen.h"

namespace ui {

// The startup screen: a focusable list of other screens.
class menu_screen {
public:
    // One menu row and the screen it opens.
    struct entry {
        std::string_view label;
        screen_id target;
    };

    static constexpr std::array<entry, 2> entries{{
        {"Counter", screen_id::counter},
        {"About", screen_id::about},
    }};

    // Draw a title bar and one row per entry; invert the focused row.
    void paint(inkerface::surface &out) const;

    // Move the focus, or open the focused entry on confirm.
    [[nodiscard]] screen_result handle(const inkerface::input_snapshot &in);

    // Return the index of the focused entry.
    [[nodiscard]] std::size_t focus() const;

private:
    std::size_t focus_ = 0;
};

static_assert(screen<menu_screen>);

} // namespace ui
```

File: `src/screens/menu_screen.cpp`

```cpp
//
// Implements the main menu.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "menu_screen.h"

#include <inkerface/color.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>

#include <cstdint>

namespace ui {

namespace {

using inkerface::color;
using inkerface::coord;
using inkerface::rect;

// Space around the text in the title and in each row, in pixels.
constexpr coord padding = 12;
constexpr coord margin = 24;

constexpr coord to_coord(std::int32_t value) {
    return static_cast<coord>(value);
}

} // namespace

void menu_screen::paint(inkerface::surface &out) const {
    const inkerface::size whole = out.extent();
    const coord line = out.line_height(inkerface::font::regular());
    const coord row_height = to_coord(line + 2 * padding);

    out.clear(color::white);

    // Title bar.
    const rect title{0, 0, whole.width, row_height};
    out.fill_rect(title, color::black);
    inkerface::text_style title_style;
    title_style.ink = color::white;
    title_style.align = inkerface::text_align::center;
    out.draw_text(title, "Menu", title_style);

    // One row per entry; the focused row is inverted.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto index = static_cast<std::int32_t>(i);
        const rect row{0,
                       to_coord(title.bottom() + margin + index * row_height),
                       whole.width,
                       row_height};
        const bool focused = i == focus_;
        if (focused) {
            out.fill_rect(row, color::black);
        }
        inkerface::text_style style;
        style.ink = focused ? color::white : color::black;
        out.draw_text(rect{margin,
                           row.y,
                           to_coord(row.width - 2 * margin),
                           row.height},
                      entries.at(i).label,
                      style);
    }

    // Help line along the bottom.
    inkerface::text_style help;
    help.align = inkerface::text_align::center;
    out.draw_text(rect{0, to_coord(whole.height - row_height), whole.width,
                       line},
                  "next / prev to move, confirm to open",
                  help);
}

screen_result menu_screen::handle(const inkerface::input_snapshot &in) {
    if (in.confirm) {
        return screen_result::open(entries.at(focus_).target);
    }
    if (in.focus_next) {
        focus_ = (focus_ + 1) % entries.size();
        return screen_result::repaint();
    }
    if (in.focus_prev) {
        focus_ = (focus_ + entries.size() - 1) % entries.size();
        return screen_result::repaint();
    }
    return screen_result::stay();
}

std::size_t menu_screen::focus() const {
    return focus_;
}

} // namespace ui
```

The focus wraps around: `next` on the last row goes to the first. The
`% entries.size()` arithmetic stays in range, because `entries` is never
empty.

## The about screen

Small screens can live entirely in a header:

File: `src/screens/about_screen.h`

```cpp
//
// Declares the about screen: a framed title; back returns to the menu.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <inkerface/color.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>
#include <inkerface/input.h>
#include <inkerface/surface.h>

#include "screen.h"

namespace ui {

// Shows the application name inside a frame.
class about_screen {
public:
    void paint(inkerface::surface &out) const {
        const inkerface::size whole = out.extent();
        out.clear(inkerface::color::white);
        out.draw_rect(
            inkerface::rect{16,
                            16,
                            static_cast<inkerface::coord>(whole.width - 32),
                            static_cast<inkerface::coord>(whole.height - 32)},
            inkerface::color::black,
            3);
        inkerface::text_style style;
        style.align = inkerface::text_align::center;
        style.max_lines = 3;
        out.draw_text(inkerface::rect{0, 0, whole.width, whole.height},
                      "inkerface\nProgrammer's Manual\npress back",
                      style);
    }

    [[nodiscard]] screen_result handle(const inkerface::input_snapshot &in) {
        return in.back ? screen_result::close() : screen_result::stay();
    }
};

static_assert(screen<about_screen>);

} // namespace ui
```

## Registering both screens

The steps are the same as in chapter 5.

**1. Ids** in `src/screens/screen.h`:

```cpp
enum class screen_id : std::uint8_t {
    hello_world,
    counter,
    menu,
    about
};
```

**2. The screen list** in `src/app/app.h`:

```cpp
#include "../screens/about_screen.h"
#include "../screens/counter_screen.h"
#include "../screens/hello_world_screen.h"
#include "../screens/menu_screen.h"
#include "../screens/screen.h"

// ...

using any_screen = std::variant<ui::hello_world_screen,
                                ui::counter_screen,
                                ui::menu_screen,
                                ui::about_screen>;
```

**3. Construction and the startup screen** in `src/app/app.cpp`:

```cpp
    stack_.push_back(make_screen(ui::screen_id::menu));  // startup screen

// ...

    case ui::screen_id::counter:
        return ui::counter_screen{};
    case ui::screen_id::menu:
        return ui::menu_screen{};
    case ui::screen_id::about:
        return ui::about_screen{};
```

**4. The source file** in `CMakeLists.txt`. Only `menu_screen.cpp` needs
adding, because `about_screen` has no `.cpp`:

```cmake
    src/screens/counter_screen.cpp
    src/screens/menu_screen.cpp)
```

**5. Two existing checks** in `tests/unit/test_application.cpp` describe
the old startup screen, and they now fail. The first expected
`focus_next` to be ignored, but the menu reacts to it:

```cpp
    context.check(!application.dispatch(press), "focus on hello world");
```

becomes

```cpp
    context.check(application.dispatch(press), "focus moves in the menu");
```

The second expected a white top-left corner, but the menu has a black
title bar there:

```cpp
    context.check(screen.frame().pixel_at(inkerface::point{0, 0}) ==
                      inkerface::color::white,
                  "application paints the top screen");
```

becomes

```cpp
    context.check(screen.frame().pixel_at(inkerface::point{0, 0}) ==
                      inkerface::color::black,
                  "application paints the menu title bar");
```

When a test fails after you change behavior on purpose, update the test.
Don't delete it. Chapter 7 covers tests in detail.

Now run `make preview`. Use Down and Up to move, Enter to open, and Escape
to go back.

## Sharing data between screens

`make_screen()` constructs each screen from its id alone, so screens
can't pass data to each other yet. This is enough for menus and simple
pages. When screens need common state (the open book, the settings), the
usual next step is:

1. Keep that state in a struct owned by `app::application`.
2. Give the screens that need it a constructor that takes a reference to
   it.
3. Pass the struct in `make_screen()`.

The state then outlives individual screens, and each screen still draws
only from what it was given.

## Summary

- The application keeps a stack of screens. `open` pushes a new screen,
  `close` pops the top one, and the bottom screen stays.
- Closed screens lose their state.
- Focus is a member index: `focus_next` and `focus_prev` move it, and
  `confirm` acts on it.
- Registering a screen always takes the same four steps: id, list,
  `make_screen`, and `CMakeLists.txt`.

Next: [Testing](07-TESTING.md).
