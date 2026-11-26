# inkerface

**inkerface** is a small C++20 platform for e-ink reader applications,
built on the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk). It
runs on the Xteink X4 and, unchanged, in a desktop window that shows the
same pixels. Application code uses only inkerface's own `snake_case` API;
FreeInk is an external, linked dependency that only the platform layer
sees.

- **Display and drawing:** shapes, dithered grays, bitmaps, text in boxes
  or at points, four built-in fonts and loadable fonts.
- **Input:** semantic actions (`confirm`, `back`, focus, paging) from the
  X4 buttons, the preview window's keyboard or a script, with timed waits.
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
                     display, input, storage, file, directory, network,
                     http, socket, program()
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
                     SDCardManager, Wi-Fi, setup()
lib/freeink-sdk/     FreeInk SDK (ignored download cache, unmodified)
lib/host-filesystems/ native-only littlefs/FatFs source cache (ignored)
src/app/             application class, main loop, program()
src/screens/         application screens (platform API only)
src/assets/          embedded monochrome logo generated from assets/logos/l1.png
tools/               SDK download, logo and font generators (make_font.py)
docker/              Dockerfiles and supporting build configuration (reserved)
tests/unit/          host unit tests
tests/isolation/     check that FreeInk stays in lib/inkerface/src/
partitions-x4.csv    X4 flash layout: two 3 MB firmware slots, LittleFS data
build/  bin/         generated; not committed
```

The platform layer is a library in `lib/inkerface/src/` with its public
headers in `include/inkerface/`, as the project standards require; the
layout follows [native](https://github.com/wischner/native).

Future container builds will keep their definitions in `docker/`, stage
generated files and image exports under `build/docker/`, and place final
exported image archives under `bin/docker/`. Docker's own image store is
managed separately by the container engine. No container build targets
are implemented yet.

## Getting FreeInk

`make` automatically downloads FreeInk to `lib/freeink-sdk` if it is
missing. Git and Python 3 are needed for this first download. The SDK is
pinned to `e637713a533ea7583ba73fa2052967c21cf0335d`; it is an ignored cache,
not checked in and not a submodule. An existing checkout is reused without
network access or automatic updates.

```sh
make sdk     # fetch only, if missing
make         # fetch if needed, then build
make clean   # remove build output, KEEP the downloaded SDK
make purge   # remove build output AND the downloaded SDK
make         # download again after purge (or manually deleting the SDK)
```

Failed downloads are cleaned up and never become the cached checkout.
Direct CMake or PlatformIO invocation requires `make sdk` first. `make
purge` only deletes this project's default cache, never a custom external
`FREEINK` checkout.

FreeInk's sources are never copied into this project. They are linked in
two ways:

- **Host:** `CMakeLists.txt` compiles `libs/ui/FreeInkUI` into a
  `freeink_ui` static library. That library is linked `PRIVATE` into
  `inkerface_platform`, so FreeInk include paths never reach the
  application.
- **X4:** `platformio.ini` adds the FreeInk libraries as
  `symlink://lib/freeink-sdk/libs/...` `lib_deps`, as FreeInk's own
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
make            # debug build -> bin/debug/
make test       # debug + ASan/UBSan builds, runs all tests
make release    # hardened -O2 build -> bin/release/
make run        # renders the hello world frame to bin/debug/hello_world.pbm
make preview    # opens the live 480x800 X4 framebuffer in a desktop window
make fonts      # regenerates the built-in fonts (Pillow and Noto Sans needed)
make storage-images # initializes missing native storage images
```

The host emulates the X4 panel (800x480 native, 480x800 portrait) and
renders with the same FreeInk code as the device. Set
`INKERFACE_FRAME_PATH` to have each presented frame written as a PBM
image. Input is read from stdin, one action per line: `confirm`, `back`,
`next`, `prev`, `page_next`, `page_prev`. End of input exits.

```sh
printf 'confirm\nback\n' | INKERFACE_FRAME_PATH=frame.pbm bin/debug/inkerface
```

To use a FreeInk checkout somewhere else, pass
`make FREEINK=/path/to/freeink-sdk`.

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
bin/debug/inkerface_storage mkdir card books
bin/debug/inkerface_storage put card /path/to/book.epub books/book.epub
bin/debug/inkerface_storage ls card books
bin/debug/inkerface_storage get card books/book.epub /tmp/book.epub
```

`put` and `get` replace the destination file. Use `internal` instead of
`card` for internal storage. The application API is unchanged; display,
buttons, rendering and networking still run natively with the existing
FreeInk integration. Device builds retain their FreeInk/SdFat storage.

`make filesystems` downloads checksum-pinned native sources into the ignored
`lib/host-filesystems/` cache; normal host builds invoke it automatically.
Downloads and staging go through `build/`. Direct CMake builds need
`make sdk filesystems` first. `make clean` preserves dependency caches;
`make purge` removes them. Upstream code and notices remain in the cache:
[littlefs](https://github.com/littlefs-project/littlefs/releases/tag/v2.11.3)
uses BSD-3-Clause; [FatFs](https://elm-chan.org/fsw/ff/) has a permissive
notice in its source files. No filesystem sources are copied from FreeInk.

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
python3 lib/freeink-sdk/libs/ui/FreeInkUI/tools/builder/server.py
# Open http://127.0.0.1:8088/
```

That tool designs FreeInkUI layouts; F5 previews this application's screen.

## Building for the Xteink X4

You need [PlatformIO](https://platformio.org/).

```sh
make x4          # pio run -e x4
make x4-upload   # flash over USB
```

The `x4` env builds the ESP32-C3 binary with `-DFREEINK_DEVICE_X4=1`. It
uses the platform and flags from FreeInk's sample configuration. PIO
output goes to `build/pio/`.

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

| Target | Status |
| --- | --- |
| Linux host, G++ 15, CMake | built and tested |
| Xteink X4 (ESP32-C3), PlatformIO | builds and links (pioarduino 55.03.37); not yet run on hardware |

See [the requirements review](docs/notes/REQUIREMENTS-REVIEW.md) for
verified behavior and outstanding acceptance checks.

## License

MIT, see [LICENSE](LICENSE). FreeInk is MIT-licensed; see its `LICENSE`
and `NOTICE`.
