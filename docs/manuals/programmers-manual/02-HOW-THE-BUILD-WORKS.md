# 2. How the build works

Before you write bigger programs, it helps to know what `make` actually
does. In this chapter you follow one build from start to finish, learn
where every file ends up, and add your first source file.

## The build chain

```text
make debug
 ├─ make sdk filesystems  download FreeInk, littlefs and FatFs to packages/ (once)
 ├─ cmake configure       write build files to build/debug/
 ├─ cmake build           compile and link
 │     freeink_ui           FreeInk's UI library, compiled unchanged
 │     inkerface_platform   the inkerface API, implemented on FreeInk
 │     inkerface_app        your screens and application (src/)
 │     inkerface            the program: src/app/main.cpp + main()
 │     inkerface_tests      the unit tests (tests/unit/)
 └─ cmake --install       copy the library into bin/linux/debug/
```

The **Makefile** is the entry point, and it is the only command you need.
Underneath, it runs **CMake** for the desktop and **PlatformIO** for the
X4. A plain `make` builds both platforms; `make help` lists them with
every target.

PlatformIO, clang-format and the font generator's Pillow run in a Docker
image that the Makefile builds from `docker/Dockerfile` the first time
one of them is needed. You install only Docker, never the tools
themselves. The desktop build uses your own g++ and CMake, so the preview
window and the debugger work as usual.

## Targets

| Command | What it does | Output |
| --- | --- | --- |
| `make` | Every platform: `linux` (debug and release) and `x4` | `bin/linux/`, `bin/x4/` |
| `make help` | Lists the supported platforms and every target | |
| `make debug` | Debug build with all warnings as errors | `build/debug/`; library staged in `bin/linux/debug/` |
| `make release` | Optimized and hardened build | `build/release/`; library staged in `bin/linux/release/` |
| `make sanitize` | Debug build with AddressSanitizer and UBSan | `build/sanitize/` |
| `make test` | Builds `debug` and `sanitize`, then runs every test | test report |
| `make run` | Runs the program and saves the frame as `build/debug/hello_world.pbm` | PBM file |
| `make preview` | Runs the program in a desktop window | window |
| `make x4` | Builds the X4 firmware and library (chapter 8) | `build/pio/`, `bin/x4/` |
| `make x4-upload` | Builds the X4 firmware and flashes it over USB | device |
| `make x4-uploadfs` | Writes `data/` to the X4's internal volume (chapter 10) | device |
| `make x4-monitor` | Opens the X4's serial console | terminal |
| `make format` | Checks formatting with clang-format | report |
| `make fonts` | Regenerates the built-in fonts (chapter 14) | font headers |
| `make packages` | Downloads every third-party package into `packages/` | `packages/` |
| `make clean` | Deletes `build/` and `bin/`, but keeps `packages/` | |
| `make purge` | Like `clean`, and also deletes `packages/` and the tool image | |

Useful variables:

- `make PREVIEW=OFF`: builds without the X11 window, for example on a
  server without a desktop.
- `make TLS=OFF`: builds without OpenSSL; HTTPS and TLS sockets then
  report `tls_unavailable` (chapter 11).
- `make FREEINK=/path/to/freeink-sdk`: uses a FreeInk checkout you
  already have instead of downloading one.
- `make run STORAGE=/some/folder`: keeps the emulated storage volumes
  (chapter 9) somewhere other than `build/storage`.

## Where files go

| Folder | Contents | Committed to Git? |
| --- | --- | --- |
| `src/` | Your application | yes |
| `include/inkerface/` | The inkerface API headers | yes |
| `lib/inkerface/` | The platform layer | yes |
| `packages/` | Downloaded third-party packages: FreeInk, littlefs, FatFs, PlatformIO's toolchain | no (cache) |
| `docker/` | The tool image's Dockerfile | yes |
| `build/` | Everything the compiler produces: your program (`build/debug/inkerface`), the tests, frame images, and the emulated storage volumes in `build/storage/` | no |
| `bin/` | The distributable libraries only: `bin/linux/debug/`, `bin/linux/release/` and `bin/x4/` (see below) | no |
| `partitions-x4.csv` | The X4 flash layout (chapter 10) | yes |

CMake refuses to build anywhere outside `build/`. This keeps the source
tree clean.

`packages/` survives `make clean`, so a clean build never downloads
again. Only `make purge` removes it.

`bin/` holds nothing you build *with* the project, only the libraries you
could hand to someone else. After each `make debug` or `make release`,
`bin/linux/<profile>/` is replaced with exactly this:

```text
bin/linux/release/
  include/inkerface/*.h        the API headers
  lib/libinkerface.a           the platform layer, with FreeInk inside
  lib/libinkerface_main.a      main(), which calls your program()
  lib/pkgconfig/inkerface.pc   compiler and linker flags
  bin/inkerface_storage        the storage image tool (chapter 9)
```

A program in another folder can then be built with only that directory:

```sh
export PKG_CONFIG_PATH=/path/to/bin/linux/release/lib/pkgconfig
g++ -std=c++20 main.cpp $(pkg-config --cflags --libs inkerface) -o reader
```

`make x4` stages the same for the device into `bin/x4/`: the headers, one
ESP32-C3 `libinkerface.a`, the flash layout and a `platformio.ini` for a
project that links it. The README describes how to use it.

## The two targets

The same `src/` folder is compiled for two very different machines.

| | Desktop (host) | Xteink X4 (device) |
| --- | --- | --- |
| Build tool | CMake | PlatformIO |
| Compiler | your G++ | ESP32-C3 cross compiler |
| Platform files | `freeink/`, `common/`, `posix/`, `net/` and `host/` in `lib/inkerface/src/` | `freeink/`, `common/`, `posix/`, `net/` and `x4/` |
| Display | 800×480 framebuffer in memory | the real SSD1677 panel |
| Storage | two folders under `INKERFACE_STORAGE_ROOT` | LittleFS on internal flash, and the SD card |
| Network | the PC's network; TLS through OpenSSL | Wi-Fi; TLS only if wolfSSL is enabled |
| Input | keyboard window or terminal lines | the X4 buttons |
| Entry | `main()` calls `program()` | `setup()` calls `program()` |

Both targets draw with the same FreeInk code (`freeink/`). This is why the
desktop shows exactly the pixels the device will show.

## Warnings are errors

Debug builds compile your code with `-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Wsign-conversion` and more, all treated as errors. The
warning you will meet most often comes from arithmetic on coordinates:

```cpp
const inkerface::coord x = whole.width - 16;   // error: int to int16_t
```

C++ computes `whole.width - 16` as an `int`. Storing it back into a 16-bit
`coord` could lose data, so you have to say that you mean it:

```cpp
const auto x = static_cast<inkerface::coord>(whole.width - 16);
```

Chapter 3 shows a small helper that keeps this readable.

## Adding a source file

When you add a `.cpp` file to `src/`, the desktop build needs to know
about it. Open `CMakeLists.txt` and add the file to `inkerface_app`:

```cmake
add_library(inkerface_app STATIC
    src/app/app.cpp
    src/screens/hello_world_screen.cpp
    src/screens/my_new_screen.cpp)        # <- your new file
```

You don't need to register headers. PlatformIO finds everything under
`src/` automatically, so the X4 build needs no change.

## Rules for your code

The project enforces one rule: **never include FreeInk or Arduino headers
in `src/` or `tests/`**. Use only `<inkerface/...>`. The `isolation` test
in `make test` fails if you break this rule. The point of the rule is
that the platform underneath can change without your application
changing.

## Debugging

In VS Code, select **inkerface (X4 preview)** in Run and Debug and press
**F5**. VS Code builds the program, starts it under GDB, and opens the
preview window. Breakpoints in `src/` work normally.

## Summary

- `make` downloads the packages once into `packages/`, then builds
  every platform into `build/`. Your program and the tests stay there;
  `bin/` gets only the distributable libraries.
- Tools you would otherwise install (PlatformIO, clang-format, Pillow)
  run in a Docker image the Makefile builds for you.
- The desktop and the X4 compile the same `src/`. Only the platform files
  differ.
- Warnings are errors, so cast coordinate arithmetic back to `coord`.
- Add new `.cpp` files to `inkerface_app` in `CMakeLists.txt`.

Next: [Drawing](03-DRAWING.md).
