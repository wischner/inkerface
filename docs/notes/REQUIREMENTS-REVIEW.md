# Requirements review

Reviewed on 2026-11-11 against `freeink_hello_world_prompt.md` and
`docs/standards/C++.md`. The attached prompt was used as acceptance
criteria for the existing project, rather than as an instruction to
regenerate it. The later SDK caching request supersedes the original
preference for a submodule.

The functional host deliverable is fulfilled. The X4 firmware builds and
links (see "X4 build" below). Full standards compliance and behavior on
real X4 hardware remain unverified for the reasons below.

Later sections are dated and record each extension (storage, the X4
build, networking, threads, the manual chapters, fonts). Where a later
section corrects an earlier one, the later one applies.

| Requirement | Result |
| --- | --- |
| Modern C++ and snake_case application API | C++20 explicitly selected; project types and functions use snake_case. Third-party and framework names stay in adapters. |
| External, linked FreeInk dependency | SDK cached under `lib/freeink-sdk`, ignored by Git. CMake links a separate static FreeInkUI library privately into the platform adapter. |
| No FreeInk types/includes in application or public headers | Automated isolation test passes. Screens and app use the project API. |
| Own font, geometry, color, surface, drawing, semantic input | Implemented, including validated bitmap drawing. Wrappers use composition. |
| Application owns loop and screen switching | `application::run()` owns the loop and variant-based screen stack. Host `main()` and device `setup()` call the same portable `program()`. |
| Hello World and host execution | Startup screen clears to white, draws the supplied `l1.png` logo and then “Hello World”. Host builds, renders and runs. |
| X4 target | PlatformIO configuration and adapters exist with `FREEINK_DEVICE_X4=1`; build and hardware behavior are not verified. |
| Build, documentation, screen example | Root Makefile, CMake, PlatformIO, README and own-API screen example exist. Corrected the case-sensitive standards link. |
| F5 display preview | VS Code launch builds and runs the actual app with a native 480×800 X11/XWayland window and GDB. Native window lifecycle and pixels tested; VS Code's F5 UI itself was not driven. |
| SDK cache lifecycle | Real pinned download from GitHub succeeded after purge. Normal make reuses it; clean preserves it; purge removes it. Git ignore verified. |

## Verification performed

- Debug and release builds: passed with first-party warnings as errors.
- `ctest --test-dir build/debug --output-on-failure`: 3/3 passed.
- `ctest --test-dir build/release --output-on-failure`: 3/3 passed.
- Separate UBSan build and tests: 3/3 passed. Configured using
  `-DCMAKE_CXX_FLAGS='-fsanitize=undefined -fno-omit-frame-pointer'` and
  `-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined` in `build/ubsan`.
- Formatting: passed using clang-format 18.1.8 and the repository config.
- Logo converter: output reproduces the checked-in embedded byte array.
- Native desktop preview: opened successfully, captured its client area,
  compared every pixel with the PBM framebuffer (`cmp` passed), sent the
  window-close event, and verified normal process exit.
- GDB: hit a breakpoint in `hello_world_screen::paint`, inspected the
  application stack, continued and observed a normal exit.
- Cache: ran `make purge`, downloaded the pinned SDK with `make debug`,
  verified the revision, checked reuse without updating `FETCH_HEAD`,
  ran `make clean`, and confirmed the cache survived before rebuilding.
- A failed network download left no partial SDK cache behind.

## Remaining gaps

- **X4:** builds and links since 2026-11-12 (see "X4 build" below).
  Physical panel, input and storage behavior still need validation.
- **ASan:** the instrumented build succeeds, but this host injects
  `/usr/local/lib/AppProtection/libAppProtection.so` through
  `/etc/ld.so.preload`. ASan fails before tests execute because its runtime
  is not first. Explicitly preloading ASan first also failed to start.
  No system protection settings or sanitizer checks were disabled.
- **Static analysis:** clang-tidy is not installed. A separate GCC
  `-fanalyzer` build reports uninitialized-value diagnostics in the
  existing `frame_path_from_environment()` optional/path construction in
  `lib/inkerface/src/host/display.cpp`. These need investigation with
  clang-tidy and/or a reduced reproducer; this is not a passed check.
- **CI:** no checked-in CI workflow enforces formatting, static analysis,
  tests, sanitizers and release builds. The standards' complete
  acceptance gate is therefore not fulfilled.
- **Formatting since the first review:** clang-format is not installed on
  the host used for the later extensions, so storage, networking, fonts
  and the related tests were kept to the 80-column limit by hand but not
  checked with `make format`.
- **ThreadSanitizer:** crashes on an empty program on this host, like
  ASan, so the threaded tests and examples have not been race-checked.
- **Desktop preview after the timed-input change (2026-11-12):** a
  scripted 2-second run in a real X11/XWayland window opened the window,
  woke every 100 ms (19 wake-ups), redrew 11 times without input, and
  exited normally. Key handling in the window was not re-tested by hand.

The preview uses the same screen and FreeInk rasterizer as the device.
It verifies layout, font and monochrome framebuffer output, not e-paper
ghosting, refresh timing, controller initialization or physical input.

## Storage (2026-11-12)

Request: minimal file, directory and storage classes, optimized for
operation, power and flash, with manual chapters and documentation.

| Requirement | Result |
| --- | --- |
| `storage`, `file`, `directory` in the public API | `include/inkerface/storage.h`, `file.h`, `directory.h`; snake_case, no platform types. |
| One implementation of the logic | `common/storage.cpp` over primitives in `common/storage_backend.h`; host and X4 supply only primitives. |
| Power | Lazy volume mounting; size and position tracked in memory; card free space from FreeInk's 20 s cache; no fsync. |
| Flash and RAM | No stdio, iostreams or `std::filesystem` on the device; unbuffered I/O; at most 4 open LittleFS handles. |
| Safety | Central path validation (no `..`, absolute or malformed paths); rename never replaces; host volumes off unless a root is set. |
| X4 storage | LittleFS on `partitions-x4.csv` data partition; SD card via FreeInk `SDCardManager`. |
| Documentation | Manual chapters 9 and 10; index, chapters 2 and 8, README and ARCHITECTURE updated. |

Verification performed:

- Debug, release and UBSan builds with warnings as errors; all three ctest
  tests pass; the unit test now runs 109 checks, including 55 storage
  checks against a fresh temporary root (removed afterwards).
- The isolation test also forbids SdFat and LittleFS headers outside the
  adapter.
- `lib/inkerface/src/x4/storage.cpp` type-checks (`-fsyntax-only`, full
  warning set) against stubs declaring the assumed LittleFS and SdFat
  calls. `open`, `read`, `fileSize`, `isDirectory`, `openNextFile`,
  `getName`, `close` and the volume calls match FreeInk's own usage and
  its SdFat test stub; `write`, `seekSet` and `LittleFS.begin/totalBytes/
  usedBytes` are taken from the libraries' documented APIs and are not
  yet confirmed by a real build.
- Every code example in manual chapters 1 and 3–7 and chapter 9 was applied
  to a copy of the project, built and tested; the chapter 9 program was
  run three times and produced the expected count file and card log.

Remaining gaps: hardware behavior of LittleFS mounting/formatting, the
partition table and the SD card is unverified (the X4 build itself now
succeeds; see below);
the host does not defend against symbolic links planted inside its
storage folders.

## X4 build (2026-11-12)

PlatformIO became available, and `make x4` was run for the first time.

- **First result:** compilation succeeded, but linking failed with
  undefined `setup()` and `loop()`. PlatformIO's dependency finder adds a
  library only when a source includes one of its headers, and inkerface's
  public headers live in the project `include/`, so `lib/inkerface` was
  never compiled. Fixed by listing `inkerface` in `lib_deps`.
- **Generated files:** PlatformIO's library downloads were moved from
  `.pio/libdeps` to `build/pio/libdeps` (`libdeps_dir`), so all generated
  output stays under `build/`.
- **Result:** `make x4` succeeds. Hello world: 16,752 bytes static RAM
  (5.1%), 413,836 bytes flash (13.2% of the 3 MB slot from
  `partitions-x4.csv`, confirming the custom partition table is used).
- **Storage linked for real:** the chapter 9 example built for the X4
  (472,238 bytes flash, 16,912 bytes static RAM). The firmware contains
  `fs::LittleFSFS::begin(bool, const char*, unsigned char, const char*)`,
  `FsBaseFile::write(const void*, unsigned)` and
  `FsBaseFile::seekSet(unsigned long long)`, confirming the calls that were
  previously only type-checked against stubs. Storage costs about 57 KB
  flash and 160 bytes static RAM, only in programs that use it.
- **Warnings:** a one-off device build with the project's full warning
  set (`-Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wconversion
  -Wsign-conversion -Wnull-dereference`) gave 0 warnings in `src/`,
  `include/` and `lib/inkerface/`, against 6,915 in third-party code,
  which shows the flags were active.
- SdFat prints one `#warning` (its `File` alias is skipped because
  Arduino's `FS.h` exists); inkerface uses `FsFile` only, so it is
  harmless.

Not verified: flashing and running on a physical X4 (display bring-up,
buttons, LittleFS formatting on first boot, SD card access).

## Networking (2026-11-12)

Request: socket classes and web API classes, as economical as possible,
reusing FreeInk.

| Requirement | Result |
| --- | --- |
| Socket classes | `include/inkerface/socket.h`: `tcp_socket` (TCP, optional TLS) over Arduino `WiFiClient` and FreeInk `SecureClient`. |
| Web API classes | `include/inkerface/http.h`: `http_client`, `http_request`, `http_response`, streaming sinks, cancel checks, over FreeInk `SecureHttpClient`. |
| Network link | `include/inkerface/network.h`: `network` (Wi-Fi station on the X4; credentials not persisted; radio off on disconnect). |
| Reuse of FreeInk | The same `net/` code runs on both targets. The host supplies a small Arduino stand-in (`host/arduino/`) and an OpenSSL `SecureClient`, so FreeInk's HTTP code is exercised by the host tests. |
| Economy | Requests block (no extra task or stack); connection reuse kept; bodies streamable; libraries archived so unused networking costs nothing. |
| TLS on the X4 | Left off: FreeInk's TLS needs wolfSSL (GPLv2 or commercial). HTTPS reports `tls_unavailable` until it is enabled. |

Verification performed:

- Debug, release and UBSan builds with warnings as errors; 144 unit checks
  pass, 34 of them network checks against a scripted loopback server
  (plain sockets, read timeouts, peer close, refused connections, a TLS
  handshake against a plain server, GET/POST, chunked bodies, body
  limit, streaming and stopping sinks, redirects, 404, connection reuse,
  cancellation in flight, malformed URLs, header injection). The host
  build with `TLS=OFF` also passes and links no OpenSSL.
- Two defects in the test's own scripted server were found by timing the
  run (11 s) with `strace` and fixed; the suite now takes about 1.3 s,
  all of it deliberate delays.
- The chapter 11 example ran on the host against the real internet: an
  http and an https download of example.com were streamed to the card
  volume, and a self-signed certificate (self-signed.badssl.com) was
  refused.
- The X4 builds. The chapter 11 example links the real Wi-Fi station and
  FreeInk's `SecureHttpClient`: 1,136,951 bytes flash, 39,476 bytes static
  RAM. A full-warning device build reported 0 warnings in project code
  (8,154 in third-party code).

Correction to "X4 build" above: pioarduino silently sets
`lib_archive = False` unless `platformio.ini` sets it, and `lib_archive = no`
had been set explicitly. Either way every library object was linked, and
once networking existed the Wi-Fi stack's global objects added about
640 KB flash and 22 KB RAM to every program. `lib_archive = yes` is now set;
`setup()` and `loop()` still resolve. Archiving changed which weak
`getArduinoLoopTaskStackSize()` won (the core's 8 KB instead of
FreeInkUI's 16 KB), so `x4/entry.cpp` now sets 16 KB explicitly with
`SET_LOOP_TASK_STACK_SIZE`; the disassembly confirms 0x4000. Current sizes:
hello world 403,756 bytes flash and 16,048 bytes static RAM; the storage
example 472,008 and 16,904 (storage costs about 67 KB); the networking
example as above.

Not verified: ThreadSanitizer (it crashes on an empty program on this
host, like ASan); Wi-Fi joining, HTTP and sockets on real X4 hardware;
TLS on the X4 (wolfSSL not enabled); DNS failure reporting (no test, to
avoid depending on the host's resolver).

## File system recipes (2026-11-12)

Request: the file system chapter lacked code samples for detecting
storage, handling directories and working with files.

Chapter 9 now has a cookbook of 20 complete recipes, replacing the earlier
fragments (some of which used undefined names): error messages; card
detection and free space; writing, reading, appending and line-by-line
reading of text; copying (also between volumes); safe saving with
power-cut recovery; binary records with explicit encoding and in-place
updates via seek; size, move and delete; folder existence; nested folder
creation; sorted listings; filtering by extension; tree walking; and tree
removal.

Verification: the 21 code blocks were extracted from the chapter exactly
as printed, compiled together with the project's full warning set as
errors, and run by a driver with 75 checks against a temporary storage
root, in a normal and a UBSan build: all pass. The checks include line
reading across 512-byte block boundaries with mixed line endings, a
simulated power cut during a safe save, and a 7-level tree (beyond the
X4's 4 open handles on internal flash, which the recipes avoid by closing
each listing before descending). Writing the driver showed that the first
draft of `remove_tree("")` would have emptied the whole volume; the recipe
now refuses the root.

## Standard threads (2026-11-12)

Request: use standard C++ threads only, with no ESP-specific code in the
application.

- The X4 runs FreeRTOS (ESP-IDF 5.5, single core, 1 kHz tick; the UI's
  Arduino loop task has priority 1). `std::thread` reaches FreeRTOS
  through ESP-IDF's pthread layer, whose defaults (from the framework's
  `sdkconfig.h`) are a 2048-byte stack and priority 5.
- `x4/entry.cpp` now applies 8 KB / priority 1 / `inherit_cfg` once in
  `setup()`, before `program()`. The disassembly of `setup()` shows the
  `esp_pthread_set_cfg` call with `0x2000` and `1` ahead of the
  `program()` call. Cost: 414 bytes flash, 8 bytes RAM.
- A program using only `std::jthread`, `std::stop_token`, `std::mutex` and
  `std::condition_variable_any` builds and links for the X4 and runs on
  the host. The isolation test now rejects `esp_*` and `freertos/`
  includes in application code (checked with a planted include).

Not verified: running threads on X4 hardware (stack and priority taking
effect at run time); ThreadSanitizer (unavailable on this host).

## Multithreading chapter (2026-11-12)

Request: a programmer's manual chapter on standard C++ threading, with a
pager that reads messages from an API every n seconds and displays them.

- Platform addition: `input::poll_for(wait)`. Without it the UI thread
  could not notice new data from a worker on the host, because `poll()`
  blocks until the next key or script line. The host terminal reader now
  reads its descriptor directly (a timed wait cannot see lines already in
  a stdio buffer), and the preview repaints only when the frame changed.
  A scripted check showed wake-ups every 100 ms, actions at 501 and 804 ms
  for a script sending them at 500 and 800 ms, and several lines at once
  all delivered.
- Chapter 12: the pager uses `std::jthread`, `std::stop_token`,
  `std::mutex`, `std::lock_guard` and `std::condition_variable_any`. The
  worker owns `network` and `http_client`; the UI owns display, input and
  storage; they share a copy-on-read `message_board` and a `wake_signal`.
- Verified on the host against Python's `http.server` with a 1-second
  interval: a message appended to the served file appeared without input,
  scrolling worked, and the program ended when its input did (4.5 s run
  for 4.5 s of input). With a server that accepts but never answers, it
  quit 1.51 s after starting with input ending at 1.5 s, instead of
  waiting for the 10 s request timeout. With a 30 s interval, confirm
  caused an immediate second request.
- The pager builds for the X4 (1,145,259 bytes flash, 39,500 bytes static
  RAM), with 0 warnings in project code under the full warning set.

Not verified: the pager on X4 hardware; ThreadSanitizer (unavailable on
this host).

## Responsive without threads chapter (2026-11-12)

Request: a chapter on alternatives to multithreading that keep software
responsive during networking, file operations and other long work.

- Chapter 13 covers slicing long jobs into resumable objects with a time
  budget (`run_for`), checking input from callbacks (HTTP cancel check and
  body sink, rate-limited because `poll_for(0)` scans the X4's buttons for
  about 10 ms), polling sockets, coroutines, idle-time work, rationing
  e-paper refreshes, and a comparison table with threads.
- Platform addition: `tcp_socket::read(out, wait)`. The first draft of the
  socket snippet could not detect a closed connection without blocking;
  a per-call wait (0 = never wait) fixes that without coupling to the
  connect timeout. Two new unit checks: an immediate return (< 20 ms) and
  detection of the peer closing; 146 unit checks pass.
- Verified: the full example (a sliced copy) builds and passes; scripted
  runs showed a byte-identical 512 KB copy when left alone, the partial
  file removed when Back arrives at the start, and a complete copy after
  Back then confirm. The technique 2 and 3 snippets, extracted as
  printed, compile with all warnings as errors; `download()` saved a
  300,000-byte file identical to the served one, and `serve()` received
  three lines (one split across packets, one without a newline) and ended
  when the server closed. The example builds for the X4 (466,038 bytes
  flash, 16,912 bytes static RAM) with 0 warnings in project code.

Not verified: on X4 hardware; cancelling a download with Back (the rate
limiting logic was compiled and the download path run, but no slow
download was interrupted).

## Fonts (2026-11-12)

Request: a font class on the cheaper engine (bitmap fonts, not runtime
TrueType), reduced ASCII fonts, functions to load and use fonts, a basic
built-in set, and drawing functions.

- API: `font::small()`, `regular()`, `bold()`, `title()`, `custom(0..3)`;
  `font_error`; `display::load_font()` from memory or storage,
  `display::reset_font()`; `surface::draw_text(point, text, font, ink)`
  and `surface::baseline(font)`.
- Built-in fonts: Noto Sans 16 px, 24 px (FreeInk's bundled font), Bold
  24 px and Bold 32 px, ASCII only, about 14 KB flash in total, no RAM.
  The X4 firmware grew by 11,146 bytes (from 404,178 to 415,324); static
  RAM is unchanged.
- `tools/make_font.py` reuses FreeInk's `gen_font.py` and writes IKF1 font
  files, compiled-in headers, or built-in headers (`--alpha` for 4-bit
  fonts). `make fonts` regenerates the built-ins and reproduces the
  committed headers exactly.
- Verified: 28 new unit checks (174 in total) in the debug and UBSan
  builds, using fonts built byte by byte in the test: metrics, advances,
  pixel-exact glyph placement, 11 kinds of malformed or oversized data
  (each leaving the slot unchanged), replacing and resetting a built-in
  slot, loading from storage, and single-line text staying inside its
  measured box. A font file and a compiled-in header made with the tool
  loaded and rendered; an IKF1 file made from the same TTF and size as
  the built-in title font rendered pixel-identically to it. A 4-bit
  (`--alpha`) font loaded. Chapter 14's example builds and renders with
  and without a font on the card. The X4 builds with 0 warnings in
  project code under the full warning set.
- Observation: FreeInk's own `gen_font.py`, run on this host's Noto Sans,
  does not reproduce FreeInk's bundled bitmap byte for byte (different
  TTF or Pillow version); the committed headers keep builds reproducible.

Not verified: fonts on X4 hardware.

## Serving, broadcast and access point (2026-11-26)

Request, from the tabcast reader (Disciple), which runs on inkerface: a
device with no keyboard must be set up over Wi-Fi from a phone, and must
find its server on the LAN by itself. That needs an access point, a
network scan, a listening socket, and UDP with broadcast.

| Requirement | Result |
| --- | --- |
| Listening socket | `tcp_listener` in `socket.h`; `accept(wait)` hands over an ordinary `tcp_socket`, polled with a zero wait from a main loop. |
| UDP and broadcast | `udp_socket` in `socket.h`: `send_to`, `broadcast`, `receive(out, from, wait)`, `port()`. Broadcast destinations are the one platform hook. |
| Network scan | `network::scan()`: strongest first; Arduino-ESP32 `scanNetworks()` on the X4, a fixed emulated list on the host. |
| Access point | `network::start_access_point()`, `stop_access_point()`, `hosting()`; soft AP on the X4, emulated on the host at the machine's own LAN address (`access_point::emulated`). |
| Errors | `net_error::bind` for a port that is taken or not permitted. |

Verification performed:

- Debug build with warnings as errors; 252 unit checks pass, the new ones
  in `tests/unit/test_network_server.cpp` over loopback: listener open,
  zero-wait accept, port in use, privileged port, accepted socket
  reading and writing, peer close seen by the accepted side, moved-from
  listener; UDP send, receive, sender reported, reply, malformed address,
  broadcast reaching a socket on the same machine; emulated scan and
  access point, WPA2 password limits, joining ending hosting.
- The X4 builds with the new code (`sys/poll.h`, lwIP sockets, soft AP,
  scan). The tabcast reader, which uses all of it, links for the X4:
  1,426,247 bytes flash and 46,756 bytes static RAM.

Not verified: any of it on X4 hardware (soft AP, scan, broadcast through
lwIP, accepting connections); the sanitize build (ASan cannot start on
this host because of a system-wide preload, even for `/bin/true`).
