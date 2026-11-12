# Architecture

inkerface is an e-ink reader UI built in layers. Each layer depends only
on the layer below it, and FreeInk is visible to exactly one layer.

```text
src/app/          application, main loop, screen switching, program()
src/screens/      screens: value types satisfying ui::screen
   │  uses only
   ▼
include/inkerface/   public platform API (snake_case, no FreeInk types)
   │  implemented by
   ▼
lib/inkerface/src/   platform layer (the only code that includes FreeInk)
   freeink/          surface -> FreeInkUI DisplayTarget, font slots and the
                     font loader, built-in fonts in fonts/ (shared)
   common/           storage, file, directory over platform primitives (shared)
   posix/            POSIX file primitives (host volumes, X4 internal flash)
   net/              tcp_socket, http_client over FreeInk SecureNet (shared)
   host/             in-memory X4 panel, stdin input, folder volumes, main(),
                     OpenSSL SecureClient, arduino/ API stand-in
   x4/               FreeInkDisplay, InputManager, LittleFS, SDCardManager,
                     Wi-Fi, Arduino setup()/loop()
   │  links
   ▼
lib/freeink-sdk/     ignored SDK download cache, compiled unmodified
```

## Isolation rules

- Headers under `include/` forward-declare backend classes
  (`detail::canvas`, `detail::display_backend`, `detail::input_backend`,
  `detail::storage_backend`, `detail::file_backend`,
  `detail::directory_backend`) and hold them by pointer or
  `std::unique_ptr`. FreeInk, Arduino, SdFat and LittleFS types never
  appear in a public declaration.
- In CMake, `freeink_ui` is linked `PRIVATE` into `inkerface_platform`,
  so FreeInk include paths do not reach the application or the tests.
- The `isolation` test (`tests/isolation/check_isolation.cmake`) fails
  when `include/`, `src/`, or `tests/` include a FreeInk, Arduino, SdFat,
  LittleFS, Wi-Fi, SecureNet or OpenSSL header or name `freeink::`.

## Backend selection

The backend is chosen at build time by which sources are compiled, not by
virtual dispatch. CMake compiles `host/`; PlatformIO compiles `freeink/`
and `x4/` (see `lib/inkerface/library.json`). Both compile `common/` and
`posix/`. Both use the same FreeInk
renderer (`DisplayTarget`), so host frames are pixel-identical to the
device frames for the same panel geometry.

## Design choices

- **Composition, no inheritance.** `display` and `input` own a backend
  through a pimpl. `surface` is a non-owning handle. Screens are plain
  classes checked by the `ui::screen` concept and held in a
  `std::variant`, so there is no screen base class and no heap
  allocation per screen.
- **Portrait logical frame.** The X4 panel is 800x480 landscape-native.
  The canvas uses FreeInk's `Orientation::Portrait` (90 degrees
  clockwise), giving a 480x800 logical frame. `canvas::pixel_at`
  implements the inverse of that mapping; `tests/unit/test_surface.cpp`
  checks that the two agree.
- **Portable entry point.** The application implements
  `int program(int, char **)`. The host calls it from `main()`. The device
  calls it from Arduino `setup()` and idles in `loop()` if it returns.
- **Semantic input.** Screens see only `input_snapshot` actions. The X4
  mapping follows the FreeInk convention: UP/DOWN focus, LEFT/RIGHT page,
  CONFIRM/BACK act.
- **Desktop preview.** `input(display&)` borrows the display. When
  `INKERFACE_PREVIEW=1`, the host input backend owns an X11 window, copies
  logical framebuffer pixels into it, and translates keyboard events.
  The display outlives input; all window operations stay on the UI thread.
  Closing the window closes input. Default construction keeps stdin input
  for tests and scripts. Hardware input ignores the display argument.
- **Storage.** `storage`, `file` and `directory` are implemented once in
  `common/storage.cpp` over a small set of platform primitives declared
  in `common/storage_backend.h` (open, read, write, seek, close, list,
  info, make_directory, remove, rename, free_space). Each platform
  defines the backend types and a `backend_deleter`, so public headers
  hold backends in `std::unique_ptr` without seeing their definitions,
  and path checks, error stickiness and size/position tracking are the
  same code on every target and covered by the host tests.
  - Errors follow the `std::fstream` model: natural return values plus a
    sticky `storage_error`. Volume operations return `storage_error` or
    `std::optional`.
  - Volumes mount lazily (no power or time until first use). I/O is
    unbuffered; size and position are tracked in memory. The device code
    avoids stdio buffers, iostreams and `std::filesystem` to save RAM and
    flash.
  - `rename` never replaces, so POSIX (replaces) and FAT (refuses)
    behave the same.
  - Host: folders `<INKERFACE_STORAGE_ROOT>/internal` and `/card` over
    POSIX. X4: internal flash is LittleFS on the `spiffs` partition of
    `partitions-x4.csv`, mounted at `/littlefs` with at most 4 open
    handles and reached through the same POSIX code; the card is
    FreeInk's `SDCardManager` (SdFat), whose `FsFile` is held in a
    `std::variant` next to the POSIX descriptor.
- **Networking.** `net/socket.cpp` and `net/http.cpp` are written once
  against the Arduino `Client` interface and FreeInk's `SecureClient`
  and `SecureHttpClient`, which does the HTTP/1.1 work (framing,
  redirects, chunked bodies, keep-alive, cancellation). On the X4 these
  are the real libraries. On the host, `host/arduino/` provides the few
  Arduino declarations they need (`millis`, `delay`, `String`, `Client`,
  `WiFiClient` over non-blocking POSIX sockets, `base64`), and
  `host/secure_client.cpp` defines FreeInk's `SecureClient` members with
  OpenSSL, the same substitution FreeInk uses in its own host tests. Only
  `network` (the link) differs per platform: Arduino-ESP32 Wi-Fi station
  on the X4, an always-available link on the host.
  - Errors are `net_error` values. Sockets keep a sticky error like
    files; a read timeout is not an error. HTTP returns a response whose
    `error` covers transport failures, while the status covers HTTP.
  - `http_client` keeps FreeInk's connection reuse. It drops the
    connection whenever the TLS verification settings change, so a
    connection opened insecurely is never reused for a verified request.
  - Bodies are collected up to `max_body_bytes`, or streamed to a sink.
  - Requests block; a cancel check is polled during connect and reads.
  - `tcp_socket::read(out, wait)` bounds the wait per call; with 0 it
    returns at once (still detecting a closed peer), so a single-threaded
    loop can poll a socket. `read(out)` uses the socket's timeout.
- **Linking on the X4.** Libraries are linked from archives
  (`lib_archive = yes`), so unused storage and networking cost no flash;
  pioarduino disables archives unless the option is set explicitly. The
  loop task's 16 KB stack is set with `SET_LOOP_TASK_STACK_SIZE` in
  `x4/entry.cpp`, because FreeInkUI's weak default does not win once
  libraries are archived.
- **Fonts.** Eight font slots on FreeInkUI's `DisplayTarget`. Slots 0 to 3
  hold built-in bitmap fonts in flash (regular is FreeInk's bundled Noto
  Sans 24 px; small, bold and title are generated headers in
  `freeink/fonts/`, made by `tools/make_font.py --format builtin`, which
  runs FreeInk's `gen_font.py` unchanged). Slots 4 to 7 start as regular.
  `display::load_font()` loads the IKF1 format (documented in
  `tools/make_font.py`) into any slot: `freeink/fonts.cpp` validates the
  header, sizes and every glyph's bitmap range, then copies the data into
  a `loaded_font` owned by the canvas and points the slot at it.
  `reset_font()` restores the built-in font and frees the copy. Loading
  from storage (`freeink/font_file.cpp`) reads at most 64 KB through the
  public `file` API. Only the bitmap engine is used; FreeInk's TrueType
  engine (FreeInkFont) is not linked.
- **Timed input.** `input::poll_for(wait)` returns an empty snapshot when
  `wait` passes, so loops can redraw without a key press. X4: repeated
  10 ms button polls until the deadline. Host terminal: standard input
  is read from its descriptor with `poll()` into a small line buffer, not
  through `std::cin`, because a stdio buffer could hold lines that a
  timed wait on the descriptor would not see. Host preview: waits on the
  X11 connection's descriptor, and repaints the window only when the
  frame differs from what it shows (or after an Expose). The comparison
  reads the whole frame on every wake-up: a loop waking every 100 ms
  costs about a quarter of one desktop core, which is acceptable for a
  development preview and absent on the device.
- **Threads.** Applications use standard C++ threads only. On the X4,
  `x4/entry.cpp` calls `esp_pthread_set_cfg()` once in `setup()`, before
  `program()`, so every `std::thread` (which reaches FreeRTOS through
  ESP-IDF's pthread layer with no attributes) gets an 8 KB stack and the
  loop task's priority 1; `inherit_cfg` passes this on to threads started
  by threads. ESP-IDF's defaults (2 KB, priority 5) would overflow or
  starve the UI. The isolation test rejects `esp_*` and `freertos/`
  headers in application code.
- **Embedded bitmap.** The logo is generated into `src/assets/logo.h`.
  `surface::draw_bitmap` validates dimensions and buffer size before
  adapting an MSB-first, padded-row bit span to FreeInk's bitmap API.
  Clear bits are transparent. The byte span is not retained.

## Failure model

| Situation | Behavior |
| --- | --- |
| Invalid panel geometry or a framebuffer that is too small | `canvas` throws `std::invalid_argument` |
| Device framebuffer not allocated by `begin()` | `display` throws `std::runtime_error` |
| Host frame file cannot be written | `display::present` returns false; `run()` returns `EXIT_FAILURE` |
| Exception escaping `program()` | Reported by `main()` or `setup()`; the process fails or the device idles |
| Storage operation fails | A `storage_error` value; files and listings keep the first failure in `error()` |
| No storage root on the host, no card, or mount failure | `storage_error::unavailable`; the card mount is retried on later calls |
| Network operation fails | A `net_error` value; sockets keep the first failure and close; HTTP returns it in `http_response::error` |
| TLS requested without TLS support (X4 default, host `TLS=OFF`) | `net_error::tls_unavailable` before any connection is made |

## Trust boundaries

- Host standard input is untrusted. Lines are capped at 64 characters,
  and unknown actions are reported and ignored.
- Storage paths are untrusted. `common/storage.cpp` rejects paths that
  are empty, longer than 255 bytes, start or end with `/`, or contain
  empty names, `.`, `..`, `\` or control characters, before any
  platform code runs, so a path cannot leave its volume. The host opens
  files with `O_NOFOLLOW` where available; symbolic links placed inside
  the host storage folders by other programs are not otherwise defended
  against.
- Network data is untrusted. HTTP parsing is FreeInk's; bodies are
  limited by `max_body_bytes` unless streamed. Request header names and
  values containing CR or LF are rejected, so they cannot inject header
  lines.
- TLS verification is on by default: the server chain must lead to the
  given CA certificate (or, on the host, the system store), and the host
  name is checked. `insecure` disables this and is documented as
  testing-only. On the X4 without a CA certificate, verification fails
  closed. Redirects never downgrade https to http (FreeInk default).
- Wi-Fi credentials are passed to the ESP32 station only; persistence is
  turned off so they are not written to NVS. They are never logged.
- The host ignores SIGPIPE once a `network` exists, so a peer closing
  during an OpenSSL write is reported as an error instead of ending the
  process.
- Font data is untrusted (a font file can come from a card). The loader
  rejects anything but IKF1 with consistent sizes: at most 256 glyphs and
  64 KB, bit depth 1 or 4, ascent within the line, glyph sizes within the
  declared maxima, and every glyph bitmap inside the bitmap block. A
  rejected font leaves the slot unchanged.
- `INKERFACE_STORAGE_ROOT` is untrusted. It must be non-empty and at most
  4096 characters. Without it, host volumes are unavailable.
- `INKERFACE_FRAME_PATH` is untrusted. It must be non-empty and at most
  4096 characters. It is used as given, with the user's own permissions.

## Toolchain

- Host: C++20 (`-std=c++20`, no extensions), G++ 15 tested, CMake 3.20 or newer.
- Device: the pioarduino ESP32 platform 55.03.37 pinned by the FreeInk
  sample. It uses `-std=gnu++2a`, the dialect FreeInk is verified with on
  that toolchain. This is a documented deviation from the
  no-extensions rule and applies to the device build only.
- FreeInk is pinned by `tools/download_sdk.py` (tested: `e637713`).
  Make downloads it if absent. Clean preserves it; purge removes the
  managed cache. Download staging directories are ignored and cleaned up.
