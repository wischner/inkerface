# inkerface

**inkerface** is a small C++20 platform for e-ink reader applications,
built on the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk). It
runs on the Xteink X4 and, unchanged, in a desktop window that shows the
same pixels. Application code uses only inkerface's own `snake_case` API;
FreeInk is an external, linked dependency that only the platform layer
sees.

- **Display and drawing:** shapes, dithered grays, bitmaps, text in boxes
  or at points, four built-in fonts and loadable fonts.
- **Input:** semantic actions (`confirm`, `back`, focus, paging, a held
  power button) from the X4 buttons, the preview window's keyboard or a
  script, with timed waits.
- **Power:** switch the device off into deep sleep, waking on the power
  button.
- **Screens:** a screen stack with navigation, owned by the application.
- **Storage:** files and directories on internal flash (LittleFS) and the
  SD card.
- **Networking:** Wi-Fi, HTTP(S) web API calls and TCP/TLS sockets, using
  FreeInk's HTTP client on both targets; listening sockets, UDP with
  broadcast, network scans, and a soft access point for setting a device
  up from a phone.
- **Threads:** standard C++ threads, set up for the device automatically.

The project standards are [docs/standards/C++.md](docs/standards/C++.md). The
layering and design choices are described in
[docs/notes/ARCHITECTURE.md](docs/notes/ARCHITECTURE.md). To learn how to
write an application step by step, read the
[Programmer's Manual](docs/manuals/PROGRAMMERS-MANUAL.md).

## Layout

```text
include/inkerface/   public platform API: geometry, color, font, surface,
                     display, input, power, storage, file, directory,
                     network, http, socket, program()
lib/inkerface/src/   platform layer, the only code that includes FreeInk
  freeink/           surface implemented on FreeInkUI's DisplayTarget;
                     font slots, built-in fonts (fonts/) and the font loader
  common/            storage, file and directory logic shared by all targets
  posix/             POSIX file primitives for X4 internal flash
  net/               sockets and HTTP on FreeInk's SecureClient and
                     SecureHttpClient; listener and UDP over BSD sockets
                     (shared by all targets)
  host/              native backend: in-memory X4 panel, stdin input, disk
                     images (littlefs/FAT), OpenSSL TLS, main()
    arduino/         minimal Arduino API (POSIX sockets) for FreeInk's
                     network code on the host
  x4/                device backend: FreeInkDisplay, InputManager, LittleFS,
                     SDCardManager, Wi-Fi station and soft AP,
                     PowerManager deep sleep, setup()
src/app/             application class, main loop, program()
src/screens/         application screens (platform API only)
src/assets/          embedded monochrome logo generated from assets/logos/l1.png
tools/               package downloads, X4 staging, logo and font generators
docker/              Dockerfile of the tool image (PlatformIO, clang-format, Pillow)
.github/workflows/   build pipeline: every platform, tests, releases
tests/unit/          host unit tests
tests/isolation/     check that FreeInk stays in lib/inkerface/src/
partitions-x4.csv    X4 flash layout: two 3 MB firmware slots, LittleFS data
packages/            downloaded third-party packages (kept by make clean)
build/               generated: objects, the demo program, tests, frames
bin/                 generated: the distributable libraries only (see below)
```

The platform layer is a library in `lib/inkerface/src/` with its public
headers in `include/inkerface/`, as the project standards require; the
layout follows [native](https://github.com/wischner/native).

## Building

```sh
make          # every platform: downloads missing packages, stages bin/
make help     # the supported platforms and every target
make clean    # removes build/ and bin/, keeps packages/
make purge    # also removes packages/ and the tool image
```

| Platform | Output | Built with |
| --- | --- | --- |
| `linux`: Linux desktop, with an X11 preview of the X4 panel | `bin/linux/debug/`, `bin/linux/release/` | the machine's g++ and CMake |
| `x4`: Xteink X4 e-reader (ESP32-C3) | `bin/x4/` | PlatformIO, in the tool image |

`bin/` receives only the distributable libraries (see
[Using the library](#using-the-library)). Everything else, from objects
to the demo program, the tests and the X4 firmware, stays in `build/`.

### Packages

Every third-party package is downloaded into `packages/` the first time
a build needs it, and reused from there without network access:

| Package | Directory | Pin |
| --- | --- | --- |
| FreeInk SDK | `packages/freeink-sdk/` | `e637713a533ea7583ba73fa2052967c21cf0335d` |
| littlefs, FatFs | `packages/host-filesystems/` | littlefs 2.11.3, FatFs R0.16 + patches 1 and 2, checksummed |
| PlatformIO platform, toolchain, framework | `packages/platformio/` (`core_dir`) | pioarduino 55.03.37 |
| PlatformIO libraries (SdFat) | `packages/pio-libdeps/` | from FreeInk's library manifests |

`make packages` fetches them all at once. Failed downloads are cleaned
up and never become a cached package. `make clean` keeps `packages/`;
only `make purge` removes it. Direct CMake or PlatformIO invocation
requires `make packages` first. To use a FreeInk checkout somewhere
else, pass `make FREEINK=/path/to/freeink-sdk`. `make purge` never
deletes such an external checkout.

### Tool image

Tools a developer machine should not have to install run in a Docker
image built from [docker/Dockerfile](docker/Dockerfile): PlatformIO with
the ESP32 toolchain, clang-format, and Pillow with the Noto fonts for
`make fonts`. The Makefile builds the image when it is missing, tags it
with a hash of the Dockerfile, and runs it as your user with the project
mounted at the same path, so its output belongs to you and its downloads
land in `packages/`. Nothing is installed in your home directory. The
only requirement is Docker.

The Linux host build runs natively, so the preview window and debugging
work as usual. The image contains the same host toolchain, which is what
CI uses.

### How FreeInk is linked

FreeInk's sources are never copied into this project. They are linked in
two ways:

- **Host:** `CMakeLists.txt` compiles `libs/ui/FreeInkUI` into a
  `freeink_ui` static library. That library is linked `PRIVATE` into
  `inkerface_platform`, so FreeInk include paths never reach the
  application.
- **X4:** `platformio.ini` adds the FreeInk libraries as
  `symlink://packages/freeink-sdk/libs/...` `lib_deps`, as FreeInk's own
  `platformio.sample.ini` does. Our platform library is `lib/inkerface`,
  and its `library.json` leaves out the host backend.

## Building for the host

You need GCC/G++ with C99/C++20 support (GCC 15 is tested), CMake 3.20 or
newer, GNU make, Python 3, and `patch`. The desktop preview also needs X11
development headers
(`libx11-dev` on Debian/Ubuntu) and an X11 or XWayland desktop session.
For headless builds without X11, use `make PREVIEW=OFF`. HTTPS and TLS
sockets on the host use OpenSSL (`libssl-dev`); build without it with
`make TLS=OFF`.

```sh
make linux      # debug and release libraries into bin/linux/
make debug      # debug build in build/debug/; library staged in bin/linux/debug/
make release    # hardened -O2 build in build/release/; library in bin/linux/release/
make test       # debug + ASan/UBSan builds, runs all tests
make run        # renders the hello world frame to build/debug/hello_world.pbm
make preview    # opens the live 480x800 X4 framebuffer in a desktop window
make fonts      # regenerates the built-in fonts (in the tool image)
make storage-images # initializes missing native storage images
```

The host emulates the X4 panel (800x480 native, 480x800 portrait) and
renders with the same FreeInk code as the device. Set
`INKERFACE_FRAME_PATH` to have each presented frame written as a PBM
image. Input is read from stdin, one action per line: `confirm`, `back`,
`next`, `prev`, `page_next`, `page_prev`, `power`. End of input exits.

```sh
printf 'confirm\nback\n' | INKERFACE_FRAME_PATH=frame.pbm build/debug/inkerface
```

Native storage runs the actual filesystem libraries over ordinary image
files under `INKERFACE_STORAGE_ROOT`, without mounts, root privileges or
QEMU:

| Image | Filesystem | Capacity |
| --- | --- | --- |
| `internal.lfs` | littlefs 2.11.3 | `0x9E0000` bytes, matching the X4 data partition |
| `card.fat` | FAT16, using FatFs R0.16 with upstream patches 1 and 2 | 32 MiB |

Images are created and formatted only when missing, on the volume's first
use. Existing images must match these capacities; invalid images report
`unavailable` and are never automatically reformatted. Images are locked
against concurrent access. Close the preview before editing its images
with another process. FAT names are case-insensitive and reject reserved
characters and trailing spaces/dots; the native card supports FAT12/16/32,
not exFAT. Flash programming enforces erase-before-setting-bits semantics,
but this does not model flash timing, endurance, or physical power loss.

`make run`, `make preview`, `make storage-images` and F5 use `build/storage`.
`make clean` deletes those images; use `STORAGE=/some/folder` to keep them
elsewhere. Without `INKERFACE_STORAGE_ROOT`, volumes remain unavailable.
The hello-world screen does not access storage; `make storage-images`
initializes both volumes explicitly. Old `internal/` and `card/` folders
are left untouched and are not automatically imported.

Use the native storage utility to populate or inspect images:

```sh
make storage-images
export INKERFACE_STORAGE_ROOT="$PWD/build/storage"
bin/linux/debug/bin/inkerface_storage mkdir card books
bin/linux/debug/bin/inkerface_storage put card /path/to/book.epub books/book.epub
bin/linux/debug/bin/inkerface_storage ls card books
bin/linux/debug/bin/inkerface_storage get card books/book.epub /tmp/book.epub
```

`put` and `get` replace the destination file. Use `internal` instead of
`card` for internal storage. The application API is unchanged; display,
buttons, rendering and networking still run natively with the existing
FreeInk integration. Device builds retain their FreeInk/SdFat storage.

The native filesystem sources are the `packages/host-filesystems/`
package; their archives are kept in `packages/.downloads/`. Upstream code
and notices remain in the cache:
[littlefs](https://github.com/littlefs-project/littlefs/releases/tag/v2.11.3)
uses BSD-3-Clause; [FatFs](https://elm-chan.org/fsw/ff/) has a permissive
notice in its source files. No filesystem sources are copied from FreeInk.

## Using the library

Each `bin/` directory is replaced on every build and holds only what
another project needs to build against inkerface.

### Linux

`make debug` and `make release` stage `bin/linux/debug/` and
`bin/linux/release/`:

```text
bin/linux/<profile>/
  include/inkerface/*.h        public API
  lib/libinkerface.a           platform layer, FreeInkUI and native filesystems in one archive
  lib/libinkerface_main.a      main(), which calls your program()
  lib/pkgconfig/inkerface.pc   compiler and linker flags (relocatable)
  bin/inkerface_storage        native storage image tool
```

Copy the directory anywhere and build against it. Your code implements
`program()` from `<inkerface/program.h>`:

```sh
export PKG_CONFIG_PATH=/path/to/bin/linux/release/lib/pkgconfig
g++ -std=c++20 reader.cpp $(pkg-config --cflags --libs inkerface) -o reader
```

The pkg-config file adds the system libraries the build was configured
with: `-pthread`, OpenSSL unless `TLS=OFF`, and X11 unless
`PREVIEW=OFF`. The headers need C++20. A program that provides its own
`main()` (a test runner, say) leaves out `-linkerface_main` and calls
`program()` itself.

### Xteink X4

`make x4` stages `bin/x4/`:

```text
bin/x4/
  include/inkerface/*.h        public API
  lib/libinkerface.a           ESP32-C3 archive: the platform layer (with setup() and
                               loop(), which call your program()), the FreeInk
                               libraries and SdFat
  partitions-x4.csv            flash layout
  platformio.ini               configuration for a project that links the library
  VERSION
```

Copy `platformio.ini` into a PlatformIO project, set `inkerface.dir` to
the staged directory, and implement `program()` in `src/`. The Arduino-ESP32
libraries the archive calls (`WiFi`, `Network`, `LittleFS`, `FS`, `SPI`,
`Wire`) come from your project's own framework and are listed in its
`lib_deps`. Keep the platform line exactly as written: the archive was
compiled against that framework version.

## F5 desktop preview

In VS Code, install the recommended Microsoft C/C++ extension and GDB,
select **inkerface (X4 preview)** in Run and Debug, and press **F5**.
The pre-launch task downloads the SDK if needed and builds the app. The
debugger runs the actual application and opens a fixed 480×800 window,
showing the same 1-bit FreeInk framebuffer used by the device adapter.
Breakpoints in `src/app/` and `src/screens/` work normally.

Enter/Space maps to confirm, Escape/Backspace to back, Down/Tab and Up
move focus, and Right/Page Down and Left/Page Up turn pages. The initial
screen intentionally ignores these actions. Close the window to exit.
The preview reproduces layout, fonts and monochrome pixels; it does not
simulate e-paper refresh timing, ghosting, or hardware drivers.

The screen uses `assets/logos/l1.png`, embedded as a 240×320 monochrome
bitmap, with “Hello World” below it. Regenerate the embedded asset with
`python3 tools/convert_logo.py` (requires Pillow). Normal builds do not
need Pillow, a PNG decoder, or runtime access to the source image.

The separate FreeInkUI builder is also available after `make sdk`:

```sh
python3 packages/freeink-sdk/libs/ui/FreeInkUI/tools/builder/server.py
# Open http://127.0.0.1:8088/
```

That tool designs FreeInkUI layouts; F5 previews this application's screen.

## Building for the Xteink X4

You need only Docker. PlatformIO runs in the tool image, and its
platform, toolchain and framework (about 2.4 GB) are downloaded into
`packages/platformio/` on the first build.

```sh
make x4                         # demo firmware in build/pio/, library in bin/x4/
make x4-upload                  # flash the demo firmware over USB
make x4-upload X4_PORT=/dev/ttyACM1
```

The `x4` env builds the ESP32-C3 binary with `-DFREEINK_DEVICE_X4=1`. It
uses the platform and flags from FreeInk's sample configuration. PIO
output goes to `build/pio/`. `x4-upload` passes the serial port into the
container, so your user needs access to it (the `dialout` group on most
distributions).

Libraries are linked from archives (`lib_archive = yes`), so a program
contains only what it uses: hello world is about 404 KB of flash and
16 KB of static RAM; a networking program about 1.1 MB, most of it the
ESP32 Wi-Fi stack. Device specifics, each covered in the
[Programmer's Manual](docs/manuals/PROGRAMMERS-MANUAL.md):

- **Flash layout** (`partitions-x4.csv`): two 3 MB firmware slots for safe
  updates and a 9.9 MB LittleFS partition for `volume::internal`. The SD
  card (`volume::card`) uses FreeInk's `SDCardManager`. Both mount on
  first use (chapters 9 and 10).
- **Networking**: the Arduino-ESP32 Wi-Fi station and FreeInk's SecureNet.
  TLS on the X4 needs wolfSSL, which is GPLv2 or commercially licensed,
  so it is not enabled; HTTPS reports `tls_unavailable` until you enable
  it (chapter 11).
- **Threads**: FreeRTOS underneath. Every standard C++ thread gets an 8 KB
  stack and the UI's priority before `program()` starts (chapters 8
  and 12).
- **Fonts**: four built-in ASCII bitmap fonts in about 14 KB of flash;
  loaded fonts cost RAM about their file size (chapter 14).

## Writing an application

A screen is a plain class with `paint()` and `handle()`, checked at
compile time by the `ui::screen` concept; the application owns the
screen stack, the main loop and the panel refresh. The
[Programmer's Manual](docs/manuals/PROGRAMMERS-MANUAL.md) walks through
it step by step, from a first program to screens, testing, storage,
networking, threads and fonts, and every example in it is built and
tested against this code.

## Supported platforms

`make help` lists them.

| Platform | Target | Status |
| --- | --- | --- |
| `linux` | Linux desktop, G++ 15, CMake | built and tested |
| `x4` | Xteink X4 (ESP32-C3), PlatformIO | builds and links (pioarduino 55.03.37); not yet run on hardware |

## Releases

[.github/workflows/build.yml](.github/workflows/build.yml) runs `make ci`:
the tool image builds every platform, runs every test and packs `bin/`
into `build/archives/`:

| Archive | Contents |
| --- | --- |
| `inkerface-<version>-linux-x86_64.tar.gz` | `bin/linux/release/` |
| `inkerface-<version>-linux-x86_64-debug.tar.gz` | `bin/linux/debug/` |
| `inkerface-<version>-x4.tar.gz` | `bin/x4/` |

Pushing a tag `vX.Y.Z` (digits only, such as `v1.2.3`) runs it and
publishes a GitHub release with the archives, versioned `X.Y.Z`. Without
a tag, start it from the repository's Actions tab with **Run workflow**.
It then builds a `0.1.0-dev-<commit>` version and attaches the archives
to the run as an artifact, without releasing. Packages are cached between
runs. `make ci` runs the same pipeline locally.

See [the requirements review](docs/notes/REQUIREMENTS-REVIEW.md) for
verified behavior and outstanding acceptance checks.

## License

MIT, see [LICENSE](LICENSE). FreeInk is MIT-licensed; see its `LICENSE`
and `NOTICE`.
