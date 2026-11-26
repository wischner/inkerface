# 8. Running on the Xteink X4

Everything so far ran on your desktop. In this chapter you build the
same application for the Xteink X4, flash it, and learn what is
different on real e-paper hardware.

> **Status:** `make x4` builds and links the firmware with the pinned
> toolchain, and the platform layer compiles without warnings under the
> project's full warning set. It has not yet run on a physical X4, so
> panel, button and storage behavior on hardware still need confirming.

## What you need

Only [Docker](https://docs.docker.com/engine/install/). PlatformIO, which
builds the firmware, runs in a tool image that the Makefile builds from
`docker/Dockerfile` the first time you need it. The first `make x4` also
downloads the ESP32 platform, toolchain and Arduino framework, about
2.4 GB, into `packages/platformio/`. Nothing is installed in your home
directory, and `make clean` keeps the download.

To flash, your user needs access to the X4's serial port, usually
`/dev/ttyACM0`. On most distributions that means being in the `dialout`
group.

## Build and flash

```sh
make x4                          # firmware in build/pio/, library in bin/x4/
make x4-upload                   # build and flash over USB
make x4-upload X4_PORT=/dev/ttyACM1
```

`make` downloads the FreeInk SDK and PlatformIO's packages first if
needed, then runs `pio run -e x4` in the tool image. The `x4` environment
in `platformio.ini`:

- targets the ESP32-C3 (`board = esp32-c3-devkitm-1`) with
  `-DFREEINK_DEVICE_X4=1`, which selects the X4's SSD1677 panel driver;
- compiles your `src/` and the device half of the platform layer
  (`lib/inkerface/src/freeink/` and `x4/`);
- links the FreeInk libraries directly from `packages/freeink-sdk/libs/`, and
  our platform library `lib/inkerface`, which is listed in `lib_deps` by
  name because PlatformIO can't discover it on its own.

After a successful build PlatformIO prints the static memory use. For the
hello-world app it is about 16 KB of RAM (4.9%) and 404 KB of the 3 MB
firmware slot (12.8%). The RAM figure covers only static data: the heap,
including the 48 KB framebuffer, is allocated at run time.

Libraries are linked from archives (`lib_archive = yes`), so a program
contains only the code it uses: storage and networking cost nothing in
programs that don't use them. The pioarduino platform turns archives off
unless `platformio.ini` sets this explicitly, and without archives the
Wi-Fi stack alone adds about 640 KB to every program. The UI's 16 KB
loop stack is set in `lib/inkerface/src/x4/entry.cpp`.

You change nothing in `src/` for this: the code from chapters 5 to 7
is the firmware.

To see log messages, open the serial console at 115200 baud:

```sh
make x4-monitor        # Ctrl+C leaves
```

## What happens at power-on

```text
setup()                         (Arduino, in lib/inkerface/src/x4/)
 └─ program()                   your src/app/main.cpp
     ├─ display                 power rails, controller detection, panel begin, clear
     ├─ input                   button manager begin
     └─ application::run()      paint, full refresh, then the loop, forever
```

`input::closed()` is never true on the device, so `program()` does not
return. If it ever does, or an exception escapes it, the message is
printed to the serial console and the device idles. The last image
stays on the panel.

The program ends only when it switches the device off with
`power_off()` (chapters 4 and 5), usually after the power button is held
for about a second. The panel and the chip go into deep sleep, and the
last frame stays on the e-paper. The next press of the power button
wakes the chip, and waking is a reset. You get the same sequence as
above, from `setup()` and a freshly started `program()`. No variables
survive, and the program paints its first page as at any other start.
Anything the program must remember across a power-off goes to storage
first (chapter 9).

## Differences from the desktop

| | Desktop | X4 |
| --- | --- | --- |
| Pixels | identical | identical |
| Refresh | instant | `full` flashes and takes noticeably longer; `fast` is quicker but leaves ghosts |
| Input | keyboard or script | physical buttons, polled about 100 times a second |
| End of program | window closed or input ended | `power_off()`; waking boots afresh |
| Power button | P in the preview, `power` in a script | held about a second |
| Memory | gigabytes | about 400 KB RAM in total |
| Output | PBM files on request | the panel only |

What this means for your code:

- **Refreshes are expensive.** Return `screen_result::stay()` whenever
  nothing visible changed, and batch several changes into one repaint.
  While `present()` runs, the loop is not polling. A very short press
  during a refresh can be missed.
- **Ghosting is real.** The application uses `fast` after the first
  frame. If ghosts build up in your screens, add a periodic full refresh
  (chapter 4 shows the pattern).
- **Memory is small.** The 48,000-byte framebuffer (800 × 480 bits) is
  owned by FreeInk. Keep screen state small and bounded. Prefer
  fixed-size `std::array` to growing containers. Put fixed data, like
  icons and the logo, in `constexpr` arrays so it lives in flash rather
  than RAM. `draw_text` makes a short temporary copy of each string, so
  don't draw thousands of strings per frame.
- **No terminal.** `std::cout` goes nowhere useful. Diagnose on the
  desktop first, where the same drawing and logic code runs.

## Threads

Use standard C++ threads, `std::thread` or better `std::jthread`, exactly
as on a desktop. No ESP-specific code is needed:

```cpp
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

std::mutex guard;
std::condition_variable_any changed;
int counter = 0;
std::jthread worker([&](std::stop_token stop) {
    std::unique_lock lock(guard);
    while (!stop.stop_requested()) {
        ++counter;   // the work; counter is shared, so guard protects it
        changed.wait_for(lock, stop, std::chrono::milliseconds(10),
                         [] { return false; });
    }
});
// ... later: the destructor asks the worker to stop and waits for it.
```

Underneath, the X4 runs **FreeRTOS**, and each standard thread is a
FreeRTOS task. Before your `program()` starts, inkerface sets the
defaults that every thread gets, including threads started by other
threads:

| | X4 | Desktop |
| --- | --- | --- |
| Stack per thread | 8 KB, from the heap | the OS default (megabytes) |
| Priority | equal to the UI, so they take turns | normal |
| Cores | 1: threads take turns, nothing runs in parallel | several |

What this means for your code:

- **Threads keep the UI responsive; they don't make it faster.** Use
  them for work that *waits*, such as network requests, not to speed up
  computation.
- **Shared data still needs a mutex.** A thread can be interrupted at any
  point, even on one core.
- **Each thread costs 8 KB of RAM** while it runs. Start a few long-lived
  workers rather than a thread per task.
- **Keep stacks shallow** in threads: no large local arrays (use
  `std::vector`), no deep recursion. Overflowing 8 KB crashes the device.
  The UI thread itself has 16 KB.
- **Stop threads cooperatively** with `std::jthread` and
  `std::stop_token`. No thread can be killed safely. Give network calls a
  timeout, so a blocked request ends.
- **Give each inkerface object one owning thread.** `display`, `input`,
  `storage` and `network` are not thread-safe, and the display and the
  card share one bus. Let the UI thread own the display and input, let a
  worker own the network, and pass results between them under a mutex.
- **Never spin.** A thread that loops without waiting starves the UI;
  wait on a condition variable, or sleep.

Don't include ESP-IDF or FreeRTOS headers (`esp_*.h`, `freertos/...`) in
your program; the isolation test rejects them. Chapter 12 builds a
complete multithreaded program with these rules. The per-thread defaults
live in `lib/inkerface/src/x4/entry.cpp`, if the platform ever needs
different ones.

## A workflow that works

1. Develop and debug a screen on the desktop with `make preview` and F5.
2. Cover its behavior and drawing with tests (chapter 7), and run
   `make test`.
3. Build and flash with `make x4-upload` only to check refresh behavior,
   button feel and memory on the device.

Most bugs are found in steps 1 and 2, in seconds and without hardware.

## Troubleshooting

| Symptom | Things to check |
| --- | --- |
| `make x4` says `docker: command not found` or `permission denied` on the Docker socket | Install Docker, and add your user to the `docker` group |
| `make x4-upload` can't open the port | Check `X4_PORT` (`ls /dev/ttyACM*`) and that your user is in the port's group (`dialout`) |
| Linker reports undefined `setup()` or `loop()` | `inkerface` must stay in `lib_deps` in `platformio.ini`, so the platform layer is compiled |
| Firmware suddenly ~640 KB larger | `lib_archive = yes` was removed from `platformio.ini`; the platform then links every library object, including the whole Wi-Fi stack |
| Blank panel after flashing | Open `make x4-monitor` and look for `inkerface: fatal error`. Some X4 batches use a different panel controller, which the platform layer tries to detect at start-up |
| Build errors in your code only on the device | The device compiles with `-std=gnu++2a` and a different G++. Check your code builds warning-free on the desktop first |
| Upload fails | Use a data-capable USB cable and close any open serial monitor |

## Summary

- `make x4` builds and `make x4-upload` flashes. Your `src/` needs no
  changes.
- On the device, `program()` runs until it calls `power_off()`, and the
  panel keeps the last image. The power button wakes the device into a
  fresh boot.
- Design for slow refreshes, ghosting and small memory.
- Standard C++ threads work: each gets 8 KB of stack and takes turns with
  the UI on the single core.
- Develop on the desktop, and confirm on the device.

Next: [Files and directories](09-FILES-AND-DIRECTORIES.md).
