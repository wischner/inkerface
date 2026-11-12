# 12. Multithreading

A network request can take seconds. If the UI thread makes it, the device
ignores the buttons until the answer arrives. In this chapter you move the
network work onto its own thread with standard C++ (`std::jthread`,
`std::mutex`, `std::condition_variable_any`) and build a **pager**: it
fetches messages from a web API every *n* seconds and shows them, while
the buttons stay responsive.

## Standard threads on both targets

You write ordinary C++20 threads. There is no inkerface thread class, and
no ESP-specific code:

| | Desktop | Xteink X4 |
| --- | --- | --- |
| A `std::thread` is | a Linux thread | a FreeRTOS task |
| Stack per thread | the OS default | 8 KB, set by inkerface before `program()` |
| Priority | normal | equal to the UI thread, so they take turns |
| Cores | several | one: threads take turns, nothing runs in parallel |

On the X4, threads don't make anything faster. They let one part of the
program **wait** (for the network, for a timer) while another keeps
working. That is exactly what a pager needs. Chapter 8 lists the device
details.

## The plan: who owns what

```text
UI thread (program)                    worker thread (fetch_messages)
───────────────────                    ──────────────────────────────
display, input, storage                network, http_client

loop:                                  loop:
  poll_for(250 ms)  ─── confirm ──▶      wait up to n seconds, or until woken
  if the board changed: draw, present    fetch the URL, parse the lines
        ▲                                publish to the board
        └──────── message_board ◀────────┘
                  (mutex-protected)
```

Four rules make this safe:

1. **Every inkerface object has one owning thread.** The UI thread creates
   and uses `display`, `input` and `storage`. The worker creates and uses
   `network` and `http_client`. Neither touches the other's objects.
2. **Shared data is behind a mutex.** The threads share only the
   `message_board`, the latest messages and a status line, and the
   `wake_signal`, which asks the worker to check now. Both lock a
   `std::mutex` for every access, and the board hands out *copies*, so no
   reference to shared data escapes the lock.
3. **Nothing waits forever.** The UI waits for input with
   `input::poll_for()`, which returns after 250 ms even without a key
   press, so the screen updates when new messages arrive. The worker
   waits with a condition variable that also wakes when the thread is
   asked to stop.
4. **Threads stop cooperatively.** `std::jthread` passes a
   `std::stop_token` to the worker. When the `jthread` is destroyed, it
   requests a stop and joins. The worker's waits and its HTTP request
   (through the cancel check) both react to the request.

## The message format

The API is as simple as it can be: an HTTP `GET` that returns plain text,
**one message per line**. Any web server can provide it, including
Python's built-in one serving a text file, and any real service can do it
in a few lines.

## The pager

The program reads two small files from internal storage:

- `wifi.txt`: the Wi-Fi network name on the first line, the password on
  the second (as in chapter 11);
- `pager.txt`: the URL on the first line, and optionally the interval in
  seconds (1 to 3600, default 30) on the second.

Replace `src/app/main.cpp`:

File: `src/app/main.cpp`

```cpp
//
// A pager: a worker thread fetches messages from a web API every few
// seconds, and the UI thread shows and scrolls them, staying responsive
// to the buttons while the network is busy. Standard C++ threads only.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/color.h>
#include <inkerface/display.h>
#include <inkerface/file.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>
#include <inkerface/http.h>
#include <inkerface/input.h>
#include <inkerface/network.h>
#include <inkerface/program.h>
#include <inkerface/storage.h>
#include <inkerface/surface.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using inkerface::color;
using inkerface::coord;
using inkerface::rect;

// --- settings ---------------------------------------------------------

constexpr std::size_t max_messages = 20;
constexpr std::size_t max_message_length = 200;
constexpr std::chrono::seconds default_interval{30};
constexpr std::chrono::seconds shortest_interval{1};
constexpr std::chrono::seconds longest_interval{3600};

// How often the UI wakes to look for new messages.
constexpr std::chrono::milliseconds ui_tick{250};

// Fast refreshes allowed before a full one clears the ghosting.
constexpr int fast_refreshes_per_full = 10;

struct settings {
    std::string wifi_name;
    std::string wifi_password;
    std::string url;
    std::chrono::seconds interval = default_interval;
};

// Read the lines of a small file on internal storage.
std::vector<std::string> read_lines(inkerface::storage &disks,
                                    std::string_view path) {
    inkerface::file in(
        disks, inkerface::volume::internal, path, inkerface::file_mode::read);
    std::array<char, 512> text{};
    const std::size_t length =
        in.read(std::as_writable_bytes(std::span(text)));
    std::vector<std::string> lines;
    std::string line;
    for (const char c : std::span(text).first(length)) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line.push_back(c);
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// Load wifi.txt (name, password) and pager.txt (URL, optional seconds).
std::optional<settings> load_settings(inkerface::storage &disks) {
    const std::vector<std::string> wifi = read_lines(disks, "wifi.txt");
    const std::vector<std::string> pager = read_lines(disks, "pager.txt");
    if (wifi.empty() || pager.empty()) {
        return std::nullopt;
    }
    settings loaded;
    loaded.wifi_name = wifi.at(0);
    loaded.wifi_password = wifi.size() > 1 ? wifi.at(1) : "";
    loaded.url = pager.at(0);
    if (pager.size() > 1) {
        const std::string &text = pager.at(1);
        long seconds = 0;
        const auto parsed =
            std::from_chars(text.data(), text.data() + text.size(), seconds);
        if (parsed.ec == std::errc{}) {
            loaded.interval = std::clamp(std::chrono::seconds(seconds),
                                         shortest_interval,
                                         longest_interval);
        }
    }
    return loaded;
}

// --- shared between the threads ---------------------------------------

// What the UI shows.
struct board_state {
    std::vector<std::string> messages;
    std::string status = "Starting...";
    // Changes on every update, so the UI can tell when to redraw.
    std::uint32_t revision = 0;
};

// The latest board_state. Written by the worker, read by the UI; every
// access locks guard_, and readers get copies.
class message_board {
public:
    // Replace messages and status; nothing changes if both are the same,
    // so the e-paper is only refreshed for real news.
    void publish(std::vector<std::string> messages, std::string status) {
        const std::lock_guard lock(guard_);
        if (messages == state_.messages && status == state_.status) {
            return;
        }
        state_.messages = std::move(messages);
        state_.status = std::move(status);
        ++state_.revision;
    }

    // Replace only the status line.
    void set_status(std::string status) {
        const std::lock_guard lock(guard_);
        if (status != state_.status) {
            state_.status = std::move(status);
            ++state_.revision;
        }
    }

    // Cheap check whether anything changed since revision.
    [[nodiscard]] std::uint32_t revision() const {
        const std::lock_guard lock(guard_);
        return state_.revision;
    }

    // A copy of everything, for drawing.
    [[nodiscard]] board_state snapshot() const {
        const std::lock_guard lock(guard_);
        return state_;
    }

private:
    mutable std::mutex guard_;
    board_state state_;
};

// Lets the UI ask the worker to check now instead of at the next
// interval.
class wake_signal {
public:
    void notify() {
        {
            const std::lock_guard lock(guard_);
            requested_ = true;
        }
        changed_.notify_one();
    }

    // Wait until notified, stopped, or the interval has passed.
    void wait(const std::stop_token &stop, std::chrono::seconds interval) {
        std::unique_lock lock(guard_);
        static_cast<void>(changed_.wait_for(
            lock, stop, interval, [this] { return requested_; }));
        requested_ = false;
    }

private:
    std::mutex guard_;
    std::condition_variable_any changed_;
    bool requested_ = false;
};

// --- the worker thread ------------------------------------------------

// Split a response body into messages: one per non-empty line.
std::vector<std::string> parse_messages(std::string_view body) {
    std::vector<std::string> messages;
    std::size_t start = 0;
    while (start < body.size() && messages.size() < max_messages) {
        const std::size_t newline = body.find('\n', start);
        const std::size_t end =
            newline == std::string_view::npos ? body.size() : newline;
        std::string_view line = body.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            messages.emplace_back(line.substr(0, max_message_length));
        }
        start = end + 1;
    }
    return messages;
}

// Fetch messages every interval until stopped. Runs on its own thread and
// owns the network: no other thread touches link or client.
void fetch_messages(const std::stop_token &stop,
                    const settings &config,
                    message_board &board,
                    wake_signal &wake) {
    inkerface::network link;
    inkerface::http_client client(link);
    const auto cancel = [&stop] { return stop.stop_requested(); };
    while (!stop.stop_requested()) {
        if (!link.connected()) {
            board.set_status("Joining " + config.wifi_name + "...");
            if (!link.connect(config.wifi_name, config.wifi_password)) {
                board.set_status("Could not join " + config.wifi_name);
                wake.wait(stop, config.interval);
                continue;
            }
        }
        const inkerface::http_response answer = client.send(
            {.url = config.url, .timeout = 10s, .max_body_bytes = 8 * 1024},
            cancel);
        if (answer.error == inkerface::net_error::cancelled) {
            break; // the program is closing
        }
        if (answer.ok()) {
            board.publish(parse_messages(answer.body), "Up to date");
        } else if (answer.error == inkerface::net_error::none) {
            board.set_status("Server answered " +
                             std::to_string(answer.status));
        } else {
            board.set_status("Server not reachable");
        }
        wake.wait(stop, config.interval);
    }
}

// --- the UI thread ----------------------------------------------------

constexpr coord to_coord(std::int32_t value) {
    return static_cast<coord>(value);
}

// Draw the board, starting with message number first.
void paint(inkerface::surface &frame,
           const board_state &board,
           std::size_t first) {
    const inkerface::size whole = frame.extent();
    const coord line = frame.line_height(inkerface::font::regular());
    constexpr coord margin = 16;
    frame.clear(color::white);

    const rect title{0, 0, whole.width, to_coord(line + 16)};
    frame.fill_rect(title, color::black);
    inkerface::text_style title_style;
    title_style.ink = color::white;
    title_style.align = inkerface::text_align::center;
    frame.draw_text(title,
                    "Pager (" + std::to_string(board.messages.size()) + ")",
                    title_style);

    inkerface::text_style status_style;
    status_style.align = inkerface::text_align::center;
    frame.draw_text(
        rect{0, to_coord(title.bottom() + 4), whole.width, line},
        board.status,
        status_style);

    const coord footer_top = to_coord(whole.height - line - margin);
    coord y = to_coord(title.bottom() + line + 2 * margin);
    inkerface::text_style message_style;
    message_style.max_lines = 3;
    for (std::size_t i = first; i < board.messages.size(); ++i) {
        const rect box{margin,
                       y,
                       to_coord(whole.width - 2 * margin),
                       to_coord(3 * line)};
        if (box.bottom() > footer_top) {
            break; // the rest is reached by scrolling
        }
        frame.draw_text(box, board.messages.at(i), message_style);
        y = to_coord(box.bottom() + margin / 2);
        frame.draw_line(inkerface::point{margin, y},
                        inkerface::point{to_coord(whole.width - margin), y},
                        color::light_gray,
                        1);
        y = to_coord(y + margin / 2);
    }

    inkerface::text_style footer_style;
    footer_style.align = inkerface::text_align::center;
    frame.draw_text(rect{0, footer_top, whole.width, line},
                    "confirm: check now   next/prev: scroll",
                    footer_style);
}

// Show a short text alone, for setup problems.
void show_text(inkerface::display &screen, std::string_view text) {
    inkerface::surface frame = screen.frame();
    const inkerface::size whole = frame.extent();
    frame.clear(color::white);
    inkerface::text_style style;
    style.align = inkerface::text_align::center;
    style.max_lines = 4;
    frame.draw_text(rect{16, 0, to_coord(whole.width - 32), whole.height},
                    text,
                    style);
    static_cast<void>(screen.present(inkerface::refresh_mode::full));
}

} // namespace

int program(int /*argc*/, char ** /*argv*/) {
    inkerface::display screen;
    inkerface::input buttons(screen);
    inkerface::storage disks;

    const std::optional<settings> config = load_settings(disks);
    if (!config) {
        show_text(screen, "Put the Wi-Fi name and password in wifi.txt, "
                          "and the URL in pager.txt.");
        while (!buttons.closed()) {
            static_cast<void>(buttons.poll());
        }
        return EXIT_SUCCESS;
    }

    // Declared before the worker, so they outlive it: the jthread is
    // destroyed first, which stops and joins the worker.
    message_board board;
    wake_signal wake;
    std::jthread worker(
        fetch_messages, *config, std::ref(board), std::ref(wake));

    inkerface::surface frame = screen.frame();
    std::optional<std::uint32_t> shown;
    std::size_t first = 0;
    int fast_refreshes = 0;
    while (!buttons.closed()) {
        const inkerface::input_snapshot in = buttons.poll_for(ui_tick);
        if (in.confirm) {
            wake.notify();
        }
        const bool scroll_down = in.page_next || in.focus_next;
        const bool scroll_up = in.page_prev || in.focus_prev;
        if (!scroll_down && !scroll_up && shown == board.revision()) {
            continue; // nothing new: no refresh, no power spent
        }
        const board_state now = board.snapshot();
        const std::size_t last = now.messages.empty() ? 0
                                                      : now.messages.size() - 1;
        if (scroll_down && first < last) {
            ++first;
        }
        if (scroll_up && first > 0) {
            --first;
        }
        first = std::min(first, last);

        paint(frame, now, first);
        inkerface::refresh_mode mode = inkerface::refresh_mode::fast;
        if (!shown || ++fast_refreshes >= fast_refreshes_per_full) {
            mode = inkerface::refresh_mode::full;
            fast_refreshes = 0;
        }
        if (!screen.present(mode)) {
            return EXIT_FAILURE;
        }
        shown = now.revision;
    }
    return EXIT_SUCCESS; // ~jthread: stop the worker, wait for it
}
```

## How it works

### Starting the worker

```cpp
std::jthread worker(fetch_messages, *config, std::ref(board), std::ref(wake));
```

`std::jthread` sees that `fetch_messages` takes a `std::stop_token` first,
and passes the thread's own token in front of the arguments. The
arguments are **copied** into the new thread: the worker gets its own
copy of `settings`, so it never reads the UI thread's data. `std::ref`
passes the board and the signal by reference instead. That is why they
must outlive the thread, and why they are declared *before* `worker`.

### Sharing results: `message_board`

The worker never draws, and the UI never fetches. They meet only in
`message_board`:

- Every member function locks `guard_` with `std::lock_guard`, so a
  reader can never see half an update.
- `snapshot()` returns a **copy**. The UI draws from its copy at leisure
  while the worker may already be writing the next update.
- `revision` changes with every real update, and `publish()` ignores an
  identical one. The UI compares one number every 250 ms and only
  redraws, and refreshes the e-paper, when something actually changed.
  An unchanged message list costs no refresh and no power.

### Waiting: `poll_for` and `wake_signal`

The UI loop uses `buttons.poll_for(250ms)` instead of `poll()`.
`poll_for` returns either a button press or, after 250 ms, an empty
snapshot. Either way the loop runs, checks the board, and goes back to
waiting. With `poll()`, new messages would only appear after the next
button press.

The worker sleeps in `wake_signal::wait()`, on a
`std::condition_variable_any`. Unlike a plain `sleep_for`, this wait ends
early in two cases: when the UI calls `notify()` (the user pressed
confirm: "check now"), and when the thread is asked to stop. The
predicate `[this] { return requested_; }` protects against spurious
wake-ups and against a `notify()` that arrives before the worker starts
waiting.

### Stopping cleanly

When the window is closed (or, on the device, the program otherwise
ends), `program()` returns and the destructors run in reverse order:

1. `~jthread` requests a stop and **joins**.
2. The worker notices immediately wherever it is: a `wake_signal::wait()`
   returns, or the cancel check stops the running HTTP request
   (`net_error::cancelled`).
3. The worker returns, destroying its `http_client` and `network` on its
   own thread.
4. Only then are `wake` and `board` destroyed.

Nothing is killed, nothing is left running, and no object is destroyed
while another thread uses it.

## Running it on the desktop

Serve a text file of messages with Python's built-in web server, and
point the pager at it:

```sh
mkdir -p build/pager
printf 'Hello from the pager\nMeeting moved to 3 pm\n' > build/pager/messages.txt
python3 -m http.server 8000 --bind 127.0.0.1 --directory build/pager &

mkdir -p build/storage/internal
printf 'my-wifi\nsecret\n' > build/storage/internal/wifi.txt
printf 'http://127.0.0.1:8000/messages.txt\n5\n' > build/storage/internal/pager.txt
make preview
```

The pager shows both messages. Add a line to `build/pager/messages.txt`:
within 5 seconds it appears, without touching the keyboard. Press Enter
to check immediately, and the arrow keys to scroll. Close the window to
quit; stop the server with `kill %1`.

## Running it on the X4

Put `wifi.txt` and `pager.txt` on the internal volume (chapter 10), with
the URL of a server reachable from your Wi-Fi network, for example your
PC's address instead of `127.0.0.1`. Use `http://` unless you have
enabled TLS (chapter 11).

On the device:

- **The worker has 8 KB of stack.** Plain HTTP fits comfortably. Keep
  large buffers in `std::string` or `std::vector` (on the heap), not in
  local arrays.
- **The radio stays on between checks.** For long intervals, save power by
  calling `link.disconnect()` before `wake.wait()`. The loop already
  reconnects when the link is down.
- **E-paper refreshes only on real changes,** thanks to the revision check.
  Every tenth refresh is a full one to clear ghosting.

## Testing threaded code

- Test the pieces without threads first: `parse_messages()` and the board
  are plain functions and classes.
- Then test the threads with short intervals. The worker accepts any
  `settings`, so a test can point it at a local server (as in
  `tests/unit/test_network.cpp`) with a 1-second interval.
- Build with ThreadSanitizer (`-fsanitize=thread`, in a build of its own)
  to find data races. A race is a bug even when the program seems to
  work.

## Summary

- Use standard `std::jthread`, `std::mutex` and
  `std::condition_variable_any`. On the X4, inkerface gives every thread
  8 KB of stack and the UI's priority.
- Give every inkerface object one owning thread. Share results through a
  small mutex-protected class that hands out copies.
- Wait with timeouts: `input::poll_for()` in the UI,
  `condition_variable_any::wait_for()` with a stop token in workers.
- Pass a `stop_token`-based cancel check to HTTP requests, so a worker
  stops promptly.
- Declare shared objects before the `jthread`, so they outlive the thread.
- Threads are not the only way to stay responsive: chapter 13 shows the
  single-threaded alternatives.

Next: [Responsive without threads](13-RESPONSIVE-WITHOUT-THREADS.md).
