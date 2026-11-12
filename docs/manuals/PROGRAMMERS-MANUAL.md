# inkerface Programmer's Manual

This manual shows you how to write e-ink applications with **inkerface**,
step by step. You start with a program that draws one line of text. You
finish with an application that has several screens and its own tests,
keeps files on internal flash and the SD card, talks to web APIs from a
background thread, and runs both in a desktop window and on the Xteink
X4.

You only need to know C++. You don't need to know anything about e-paper
hardware or the FreeInk SDK, because inkerface hides both behind its own
small API.

## Before you start

- A Linux machine with G++ 15 (or another G++ with C++20 support),
  CMake 3.20 or newer, GNU make, Git and Python 3.
- For the desktop preview window: X11 development headers
  (`libx11-dev` on Debian and Ubuntu) and an X11 or XWayland session.
- For HTTPS and TLS on the desktop (chapter 11): OpenSSL development
  headers (`libssl-dev`).
- For the Xteink X4 (chapter 8 only): [PlatformIO](https://platformio.org/)
  and a USB cable.

All code in this manual follows the project's
[C++ standards](../standards/C++.md). Names are lower `snake_case`, and
every source file starts with a short comment that describes it.

## Chapters

| # | Chapter | You will learn |
| --- | --- | --- |
| 1 | [Hello, e-ink](programmers-manual/01-HELLO-E-INK.md) | Write, build and run the smallest program |
| 2 | [How the build works](programmers-manual/02-HOW-THE-BUILD-WORKS.md) | What `make` does, where files go, how to add sources |
| 3 | [Drawing](programmers-manual/03-DRAWING.md) | Coordinates, colors, shapes, text and bitmaps |
| 4 | [Input and the main loop](programmers-manual/04-INPUT-AND-THE-MAIN-LOOP.md) | Read buttons, update the page, choose refresh modes |
| 5 | [Screens](programmers-manual/05-SCREENS.md) | Turn a program into a screen and let the application run it |
| 6 | [Several screens](programmers-manual/06-SEVERAL-SCREENS.md) | Menus, focus, and opening and closing screens |
| 7 | [Testing](programmers-manual/07-TESTING.md) | Check pixels and behavior on the host, without hardware |
| 8 | [Running on the Xteink X4](programmers-manual/08-RUNNING-ON-THE-X4.md) | Build, flash and troubleshoot the device |
| 9 | [Files and directories](programmers-manual/09-FILES-AND-DIRECTORIES.md) | Detect storage; read, write, copy and safely save files; create, list, walk and delete folders (with complete recipes) |
| 10 | [Storage on the device](programmers-manual/10-STORAGE-ON-THE-DEVICE.md) | Flash layout, power and wear, memory and limits on the X4 |
| 11 | [Networking](programmers-manual/11-NETWORKING.md) | Join Wi-Fi, call web APIs, stream downloads, use TCP sockets |
| 12 | [Multithreading](programmers-manual/12-MULTITHREADING.md) | Standard C++ threads: a pager that polls an API in the background |
| 13 | [Responsive without threads](programmers-manual/13-RESPONSIVE-WITHOUT-THREADS.md) | Slicing long jobs, checking input from callbacks, polling sockets |
| 14 | [Fonts](programmers-manual/14-FONTS.md) | Built-in fonts, making fonts, loading them from storage or memory |

Read the chapters in order. Each chapter changes the project that the
previous one left behind.

## The big picture

```text
your code          src/app/, src/screens/   uses only <inkerface/...>
                         │
inkerface API      include/inkerface/       display, surface, input, ...
                         │
platform layer     lib/inkerface/src/       the only code that knows FreeInk
                         │
FreeInk SDK        lib/freeink-sdk/         downloaded automatically
```

You write code only in the top layer. The same code runs on the desktop
and on the device, because the platform layer is swapped at build time.

## Quick reference

| Header | Main names |
| --- | --- |
| `<inkerface/program.h>` | `program(argc, argv)`, the entry point you write |
| `<inkerface/display.h>` | `display`, `display::frame()`, `display::present()`, `load_font()`, `reset_font()`, `refresh_mode` |
| `<inkerface/surface.h>` | `surface`: `clear`, `fill_rect`, `draw_rect`, `draw_line`, `draw_bitmap`, `draw_text`, `measure_text`, `line_height`, `baseline`, `pixel_at` |
| `<inkerface/input.h>` | `input`, `input::poll()`, `input::poll_for()`, `input::closed()`, `input_snapshot` |
| `<inkerface/geometry.h>` | `coord`, `point`, `size`, `rect` |
| `<inkerface/color.h>` | `color::white`, `light_gray`, `dark_gray`, `black` |
| `<inkerface/font.h>` | `font::regular()`, `small()`, `bold()`, `title()`, `custom(n)`; `font_error`, `text_style`, `text_align` |
| `<inkerface/storage.h>` | `storage`, `volume::internal`/`card`, `storage_error`, `entry_info`, `max_path_length` |
| `<inkerface/file.h>` | `file`: `read`, `write`, `seek`, `position`, `size`, `close`, `error`; `file_mode` |
| `<inkerface/directory.h>` | `directory`: `next`, `error`; `directory_entry` |
| `<inkerface/network.h>` | `network`: `connect`, `disconnect`, `connected`; `net_error` |
| `<inkerface/http.h>` | `http_client::send`, `close`; `http_request`, `http_response`, `http_method`, `body_sink`, `cancel_check` |
| `<inkerface/socket.h>` | `tcp_socket`: `read` (optionally with a wait), `write`, `available`, `close`, `error`; `socket_options` |
| `src/screens/screen.h` | `ui::screen` concept, `screen_result`, `screen_id`, `navigation` |
| `src/app/app.h` | `app::application`, `app::any_screen` |
