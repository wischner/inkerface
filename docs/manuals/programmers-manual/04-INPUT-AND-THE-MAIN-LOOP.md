# 4. Input and the main loop

So far the page never changes. In this chapter you write a counter: each
button press changes a number, and the page updates. Along the way you
learn how input works and how to choose an e-paper refresh mode.

## Actions, not buttons

inkerface never tells you *which key* was pressed. It tells you *what the
user meant*. `input::poll()` returns an `input_snapshot`:

```cpp
struct input_snapshot {
    bool confirm;     // select, activate, OK
    bool back;        // cancel, leave this screen
    bool focus_next;  // move the highlight down
    bool focus_prev;  // move the highlight up
    bool page_next;   // turn to the next page
    bool page_prev;   // turn to the previous page
    bool power;       // the power button was held: switch off
    bool any() const; // true if any of the above is set
};
```

Each target maps its own hardware to these actions:

| Action | Xteink X4 button | Preview window key | Terminal line |
| --- | --- | --- | --- |
| `confirm` | Confirm | Enter, Space | `confirm` |
| `back` | Back | Escape, Backspace | `back` |
| `focus_next` | Down | Down, Tab | `next` |
| `focus_prev` | Up | Up | `prev` |
| `page_next` | Right | Right, Page Down | `page_next` |
| `page_prev` | Left | Left, Page Up | `page_prev` |
| `power` | Power, held about a second | P | `power` |

Because screens only see actions, the same code works with buttons, a
keyboard, or a test script.

## Switching off

`power` is reported once when the power button has been held for about
a second, so a knocked button does not count. What it means is up to the
program, but on a reader it almost always means "switch off", which is
`power_off()` from `<inkerface/power.h>`:

```cpp
const inkerface::input_snapshot in = buttons.poll();
if (in.power) {
    paint_off_screen(screen);           // the panel keeps it while off
    static_cast<void>(screen.present(inkerface::refresh_mode::full));
    inkerface::power_off(screen);       // returns only on the desktop
    return EXIT_SUCCESS;
}
```

- **Device:** the panel and the chip go to deep sleep, drawing almost
  nothing, and the last frame stays on the e-paper. The next press of
  the power button wakes the device, and waking is a fresh boot:
  `program()` starts again from the beginning. Nothing is shut down in
  an orderly way, so close files and say goodbye to servers first.
- **Desktop:** there is nothing to switch off. `power_off()` returns at
  once, and the program should end, as it would on the device.

## How `poll()` behaves

- **Device:** checks the buttons about 100 times a second. Most calls
  return an empty snapshot (`any()` is false).
- **Preview window:** waits for the next window event. Before waiting, it
  shows the current frame.
- **Terminal:** waits for the next line on standard input. Unknown words
  are reported and skipped.

`closed()` becomes true when the window is closed or standard input ends.
On the device it never becomes true.

`poll_for(wait)` is the same, but returns an empty snapshot once `wait`
has passed without an action. Use it when the screen must also change
without a key press, such as a clock or new messages from another thread
(chapter 12):

```cpp
const inkerface::input_snapshot in =
    buttons.poll_for(std::chrono::milliseconds(250));
```

The loop is always the same: wait for an action, change your state,
draw, present.

## Refresh modes

`display::present()` takes a `refresh_mode`:

| Mode | Looks like | Use it for |
| --- | --- | --- |
| `refresh_mode::full` | the panel flashes black and white | the first frame and screen changes |
| `refresh_mode::fast` | a quick update, but faint "ghosts" of old pixels remain | small changes within a page |

Ghosts build up with each fast refresh. A common rule is a full refresh
after every N fast ones. The desktop build has no ghosting and ignores the
mode, so check the result on the device.

## The counter

Replace `src/app/main.cpp` with this:

File: `src/app/main.cpp`

```cpp
//
// Counts button presses: confirm adds one, page_prev subtracts one, back
// resets. The page is redrawn and presented after each change.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/color.h>
#include <inkerface/display.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>
#include <inkerface/input.h>
#include <inkerface/program.h>
#include <inkerface/surface.h>

#include <cstdint>
#include <cstdlib>
#include <string>

namespace {

using inkerface::coord;

// Keep the count in a range that always fits on the page.
constexpr int min_count = 0;
constexpr int max_count = 9999;

// Fast refreshes allowed before a full one clears the ghosting.
constexpr int fast_refreshes_per_full = 10;

constexpr coord to_coord(std::int32_t value) {
    return static_cast<coord>(value);
}

// Draw the whole page from the current count.
void paint_counter(inkerface::surface &frame, int count) {
    const inkerface::size whole = frame.extent();
    const coord line = frame.line_height(inkerface::font::regular());
    inkerface::text_style centered;
    centered.align = inkerface::text_align::center;

    frame.clear(inkerface::color::white);
    frame.draw_text(inkerface::rect{0, 0, whole.width, whole.height},
                    "Count: " + std::to_string(count),
                    centered);
    frame.draw_text(inkerface::rect{0,
                                    to_coord(whole.height - 2 * line),
                                    whole.width,
                                    line},
                    "confirm +1    page_prev -1    back 0",
                    centered);
}

// Apply one snapshot to the count; return true if the count changed.
bool update_count(const inkerface::input_snapshot &in, int &count) {
    const int before = count;
    if (in.confirm && count < max_count) {
        ++count;
    }
    if (in.page_prev && count > min_count) {
        --count;
    }
    if (in.back) {
        count = min_count;
    }
    return count != before;
}

} // namespace

int program(int /*argc*/, char ** /*argv*/) {
    inkerface::display screen;
    inkerface::input buttons(screen);
    inkerface::surface frame = screen.frame();

    int count = min_count;
    int fast_refreshes = 0;

    paint_counter(frame, count);
    if (!screen.present(inkerface::refresh_mode::full)) {
        return EXIT_FAILURE;
    }

    while (!buttons.closed()) {
        const inkerface::input_snapshot in = buttons.poll();
        if (!in.any() || !update_count(in, count)) {
            continue;  // nothing happened, or nothing changed
        }

        paint_counter(frame, count);

        inkerface::refresh_mode mode = inkerface::refresh_mode::fast;
        if (++fast_refreshes >= fast_refreshes_per_full) {
            mode = inkerface::refresh_mode::full;
            fast_refreshes = 0;
        }
        if (!screen.present(mode)) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
```

Run it with `make preview` and press Enter a few times.

Some details to notice:

- **The state lives in one place.** `count` is ordinary data, and
  `paint_counter()` draws the *entire* page from it. Always redraw the
  whole page from your state instead of patching the old picture. This
  is simpler, and it is always correct.
- **The count is bounded.** `min_count` and `max_count` keep the number
  on screen and rule out integer overflow, however long someone presses
  the button.
- **Present only when something changed.** `update_count()` reports
  whether the state changed. This avoids a pointless refresh, for example
  for "back" when the count is already zero.
- **Draw, then present.** Drawing is cheap and invisible. Presenting is
  slow and visible.

## Scripting a run

Without the preview, input comes from standard input, so you can script
a session and look at the last frame:

```sh
printf 'confirm\nconfirm\nconfirm\npage_prev\n' \
    | INKERFACE_FRAME_PATH=count.pbm bin/debug/inkerface
```

`count.pbm` now shows "Count: 2". Chapter 7 uses this technique for
automated tests.

## Summary

- `poll()` returns semantic actions, and each target maps its own
  hardware to them.
- `power` is a held power button; `power_off()` switches the device off
  until the next press, which boots it afresh.
- The main loop is: poll, update the state, redraw everything, present.
- Use `full` for new pages and `fast` for small changes, with an
  occasional `full` to clear ghosts.
- Keep state bounded, and present only when it changed.

This loop works, but every new feature would make `program()` longer.
The next chapter splits it into a reusable shape.

Next: [Screens](05-SCREENS.md).
