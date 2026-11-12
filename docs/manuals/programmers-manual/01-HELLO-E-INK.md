# 1. Hello, e-ink

In this chapter you write the smallest inkerface program, build it, and
look at the result, first as an image file and then in a live window.

## Get the project and build it once

Clone the project, change into its folder, and run `make`:

```sh
make
```

The first `make` downloads the FreeInk SDK into `lib/freeink-sdk`, which
takes a minute. Later builds reuse it. When `make` finishes, the program
is in `bin/debug/inkerface`. Check that it works:

```sh
make preview
```

A 480×800 window opens with a logo and "Hello World". This window shows
the same pixels the X4 would show. Close it to exit.

## Where your program starts

An ordinary C++ program starts in `main()`. An inkerface program starts in
`program()` instead:

```cpp
int program(int argc, char **argv);
```

You can't write `main()` yourself, because the device has none. Arduino
starts the firmware in a function called `setup()`. inkerface provides
`main()` on the desktop and `setup()` on the device, and both of them call
your `program()`. As a result, the same file works on both targets.

## The smallest program

Open `src/app/main.cpp` and replace its contents with this:

File: `src/app/main.cpp`

```cpp
//
// Draws a greeting once and shows it on the display.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/color.h>
#include <inkerface/display.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>
#include <inkerface/program.h>
#include <inkerface/surface.h>

#include <cstdlib>

int program(int /*argc*/, char ** /*argv*/) {
    // 1. Bring the display up.
    inkerface::display screen;

    // 2. Get the drawing surface of its frame.
    inkerface::surface frame = screen.frame();

    // 3. Draw: a white page with centered text.
    frame.clear(inkerface::color::white);
    const inkerface::size whole = frame.extent();
    inkerface::text_style style;
    style.align = inkerface::text_align::center;
    frame.draw_text(inkerface::rect{0, 0, whole.width, whole.height},
                    "Hello, e-ink!",
                    style);

    // 4. Send the frame to the panel.
    if (!screen.present(inkerface::refresh_mode::full)) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
```

Use your own project's license and copyright holder in the file header.

Every inkerface program follows the same four steps:

1. **Create a `display`.** This turns the panel on, or on the desktop
   creates an in-memory copy of it. A program has exactly one display.
2. **Get a `surface`** from `display::frame()`. All drawing goes through
   the surface. It is only a handle to the display's frame, so it must
   not be used after the display is gone.
3. **Draw.** Drawing changes only the frame in memory. Nothing appears
   yet.
4. **Present** the frame with `display::present()`. This is the moment the
   e-paper actually changes.

Steps 3 and 4 are separate because an e-paper refresh is slow and visible.
Draw everything first, then refresh once.

## Build and look at the result

```sh
make run
```

`make run` builds the program and runs it with the environment variable
`INKERFACE_FRAME_PATH` set. Each time the program presents a frame, the
desktop build writes that frame to the named file as a PBM image. The
output is `bin/debug/hello_world.pbm`. Open it with any image viewer
(for example `eog`, `feh` or GIMP).

You can also run the program yourself:

```sh
INKERFACE_FRAME_PATH=hello.pbm bin/debug/inkerface
```

## Keep the window open

The program above exits immediately after it presents. On the X4 that is
fine: e-paper keeps its image without power, so the greeting stays on the
screen. In a desktop window it isn't useful, because the window closes
before you can see it.

To keep the program running until the user is done, create an `input` and
wait until it closes. Add `#include <inkerface/input.h>`, then replace the
end of `program()` (from step 4 on) like this:

File: `src/app/main.cpp`

```cpp
//
// Draws a greeting and keeps it on screen until input closes.
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

#include <cstdlib>

int program(int /*argc*/, char ** /*argv*/) {
    inkerface::display screen;
    inkerface::input buttons(screen);
    inkerface::surface frame = screen.frame();

    frame.clear(inkerface::color::white);
    const inkerface::size whole = frame.extent();
    inkerface::text_style style;
    style.align = inkerface::text_align::center;
    frame.draw_text(inkerface::rect{0, 0, whole.width, whole.height},
                    "Hello, e-ink!",
                    style);

    if (!screen.present(inkerface::refresh_mode::full)) {
        return EXIT_FAILURE;
    }

    // Wait until the input source closes; ignore every button for now.
    while (!buttons.closed()) {
        static_cast<void>(buttons.poll());
    }
    return EXIT_SUCCESS;
}
```

Now run it in the window:

```sh
make preview
```

The window stays open until you close it.

`inkerface::input buttons(screen)` passes the display to the input. On the
desktop, `make preview` sets `INKERFACE_PREVIEW=1`, which makes this input
open a window that shows the display's frame and reads your keyboard.
Without the preview, the input reads commands from the terminal, one per
line (see chapter 4). On the device it reads the physical buttons.

`poll()` waits for the next input and returns what happened. Its result
is marked `[[nodiscard]]`, so ignoring it on purpose needs
`static_cast<void>(...)`. `closed()` becomes true when the window closes
or the terminal input ends. On the device it is never true, so the
program runs until the power is switched off.

## Summary

- Your program starts in `program()`, not `main()`.
- The basic sequence is display, then surface, then drawing, then present.
- `make run` writes the frame to a PBM file, and `make preview` shows it
  in a window.
- An `input` keeps the program alive, and `closed()` tells you when to
  stop.

Next: [How the build works](02-HOW-THE-BUILD-WORKS.md).
