# 13. Responsive without threads

Chapter 12 kept the UI responsive with a second thread. Threads work, but
they have costs: 8 KB of RAM per thread on the X4, a mutex around every
shared value, and a whole class of bugs (data races) that tests rarely
catch. Often you don't need them. A program stays responsive as long as
**no single step of its main loop takes long**. This chapter shows three
ways to get there on one thread.

## The idea

A responsive program returns to its main loop often, say every 50 to
100 ms, and checks the buttons each time:

```text
loop:
    check input (don't wait, or wait briefly)
    do a little work          <- never more than ~50 ms
    redraw if something changed
```

Anything long has to be cut into pieces that fit into "a little work",
or it has to give the loop a chance to run from inside. The three
techniques:

| Technique | Use it for | Example in this chapter |
| --- | --- | --- |
| **Slice the work** | jobs you control: copying, parsing, computing | a file copy with progress, cancel and restart |
| **Check input from inside** | calls that take a callback: HTTP downloads, line reading | a download that Back can cancel |
| **Never wait, poll** | sockets and other event sources | a socket read only when data is there |

## Checking input without waiting

All three rely on asking "was a button pressed?" without blocking.
`input::poll_for(0ms)` does that:

| Target | `poll_for(0ms)` costs |
| --- | --- |
| Desktop | almost nothing: it only looks whether input is waiting |
| X4 | about 10 ms: one scan of the buttons |

On the X4, checking input after every small step would spend most of
the time scanning buttons. So budget your work in **time**: work for, say,
50 ms, then check input once. The device then spends about 80% of its
time on the job and still reacts to a button within 60 ms.

## Technique 1: slice the work

Turn the long job into an object that remembers how far it got, and give
it a `run_for(budget)` member that does as much as fits into the budget
and returns. The main loop calls it between input checks.

### Example: a responsive file copy

The program creates a 512 KB sample file on internal flash, then copies
it to the card (or to internal flash without a card). While it copies, a
progress bar fills, Back cancels the copy (and removes the incomplete
file), and confirm starts it again.

Creating the sample file is **deliberately not sliced**. While it runs,
the program ignores the buttons. Compare that with the copy, which
doesn't.

Replace `src/app/main.cpp`:

File: `src/app/main.cpp`

```cpp
//
// Copies a file in slices from the main loop, so the buttons stay
// responsive during the copy without a second thread. Confirm restarts
// the copy, back cancels it.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/color.h>
#include <inkerface/display.h>
#include <inkerface/file.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>
#include <inkerface/input.h>
#include <inkerface/program.h>
#include <inkerface/storage.h>
#include <inkerface/surface.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using inkerface::color;
using inkerface::coord;
using inkerface::file;
using inkerface::file_mode;
using inkerface::rect;
using inkerface::storage;
using inkerface::volume;

constexpr std::string_view sample_path = "sample.bin";
constexpr std::uint64_t sample_size = 512 * 1024;
constexpr std::size_t block_size = 4096;

// Longest stretch of copying between two input checks.
constexpr std::chrono::milliseconds work_budget{50};

// Redraw the progress bar every this many percent: an e-paper refresh is
// slow, so don't refresh for every block.
constexpr int progress_step = 10;

// A file's place. Owns its path, because a job outlives the caller's
// strings.
struct location {
    volume where;
    std::string path;
};

// Copies one file a few blocks at a time.
class copy_job {
public:
    enum class state : std::uint8_t {
        running,
        done,
        failed,
        cancelled
    };

    copy_job(storage &disks, location from, location to)
        : disks_(&disks)
        , to_(std::move(to))
        , in_(disks, from.where, from.path, file_mode::read)
        , out_(disks, to_.where, to_.path, file_mode::write)
        , buffer_(block_size) {
        if (!in_ || !out_) {
            finish(state::failed);
        }
    }

    // Copy for at most budget, then return so the caller can check input.
    void run_for(std::chrono::milliseconds budget) {
        const auto until = std::chrono::steady_clock::now() + budget;
        while (state_ == state::running &&
               std::chrono::steady_clock::now() < until) {
            const std::size_t got = in_.read(buffer_);
            if (got == 0) {
                const bool clean =
                    in_.error() == inkerface::storage_error::none;
                finish(clean ? state::done : state::failed);
            } else if (out_.write(std::span(buffer_).first(got)) != got) {
                finish(state::failed);
            }
        }
    }

    // Stop, and remove the incomplete copy.
    void cancel() {
        if (state_ == state::running) {
            finish(state::cancelled);
        }
    }

    [[nodiscard]] state status() const {
        return state_;
    }

    // Progress from 0 to 100.
    [[nodiscard]] int percent() const {
        const std::uint64_t total = in_.size();
        return total == 0 ? 100 : static_cast<int>(out_.size() * 100 / total);
    }

    [[nodiscard]] const location &target() const {
        return to_;
    }

private:
    // Close both files; keep the copy only if it is complete.
    void finish(state result) {
        const bool committed = out_.close();
        static_cast<void>(in_.close());
        if (result == state::done && !committed) {
            result = state::failed;
        }
        if (result != state::done) {
            static_cast<void>(disks_->remove(to_.where, to_.path));
        }
        state_ = result;
    }

    storage *disks_;
    location to_;
    file in_;
    file out_;
    // On the heap: a 4 KB local array would take a quarter of the UI
    // thread's stack.
    std::vector<std::byte> buffer_;
    state state_ = state::running;
};

// Create the sample file if it is missing. This blocks on purpose:
// compare it with the copy, which doesn't.
bool prepare_sample(storage &disks) {
    if (disks.info(volume::internal, sample_path)) {
        return true;
    }
    file out(disks, volume::internal, sample_path, file_mode::write);
    std::array<std::byte, block_size> block{};
    for (std::uint64_t written = 0; written < sample_size;
         written += block_size) {
        for (std::size_t i = 0; i < block.size(); ++i) {
            block.at(i) =
                static_cast<std::byte>((written / block_size + i) & 0xFFU);
        }
        static_cast<void>(out.write(block));
    }
    return out.close();
}

std::string describe(const copy_job &job) {
    const std::string where =
        job.target().where == volume::card ? "the card" : "internal flash";
    switch (job.status()) {
    case copy_job::state::running:
        return "Copying to " + where + "...";
    case copy_job::state::done:
        return "Copied to " + where;
    case copy_job::state::failed:
        return "Copy failed";
    case copy_job::state::cancelled:
        return "Cancelled";
    }
    return "";
}

void paint(inkerface::surface &frame, std::string_view status, int percent) {
    const inkerface::size whole = frame.extent();
    const coord line = frame.line_height(inkerface::font::regular());
    constexpr coord margin = 24;
    frame.clear(color::white);
    inkerface::text_style centered;
    centered.align = inkerface::text_align::center;

    const rect title{0, 0, whole.width, static_cast<coord>(line + 16)};
    frame.fill_rect(title, color::black);
    inkerface::text_style title_style = centered;
    title_style.ink = color::white;
    frame.draw_text(title, "Copy without threads", title_style);

    const coord middle = static_cast<coord>(whole.height / 2);
    frame.draw_text(
        rect{0, static_cast<coord>(middle - 3 * line), whole.width, line},
        status,
        centered);
    const rect bar{margin,
                   static_cast<coord>(middle - line),
                   static_cast<coord>(whole.width - 2 * margin),
                   static_cast<coord>(line + 8)};
    frame.draw_rect(bar, color::black, 2);
    const auto filled = static_cast<coord>((bar.width - 8) * percent / 100);
    frame.fill_rect(rect{static_cast<coord>(bar.x + 4),
                         static_cast<coord>(bar.y + 4),
                         filled,
                         static_cast<coord>(bar.height - 8)},
                    color::black);
    frame.draw_text(rect{0, static_cast<coord>(bar.bottom() + 8), whole.width,
                         line},
                    std::to_string(percent) + " %",
                    centered);
    frame.draw_text(
        rect{0, static_cast<coord>(whole.height - line - margin), whole.width,
             line},
        "confirm: copy again   back: cancel",
        centered);
}

} // namespace

int program(int /*argc*/, char ** /*argv*/) {
    inkerface::display screen;
    inkerface::input buttons(screen);
    storage disks;
    inkerface::surface frame = screen.frame();

    paint(frame, "Preparing the sample file...", 0);
    static_cast<void>(screen.present(inkerface::refresh_mode::full));
    if (!prepare_sample(disks)) {
        paint(frame, "Could not create the sample file", 0);
        static_cast<void>(screen.present(inkerface::refresh_mode::full));
        while (!buttons.closed()) {
            static_cast<void>(buttons.poll());
        }
        return EXIT_SUCCESS;
    }

    const location source{volume::internal, std::string(sample_path)};
    const location target{disks.available(volume::card) ? volume::card
                                                        : volume::internal,
                          "copy.bin"};
    std::optional<copy_job> job;
    job.emplace(disks, source, target);
    std::optional<copy_job::state> drawn_state;
    int drawn_step = -1;

    while (!buttons.closed()) {
        const bool busy = job->status() == copy_job::state::running;
        // Busy: just look at the buttons. Idle: sleep until one is pressed.
        const inkerface::input_snapshot in =
            busy ? buttons.poll_for(0ms) : buttons.poll();
        if (in.back && busy) {
            job->cancel();
        }
        if (in.confirm && !busy) {
            job.emplace(disks, source, target);
        }
        job->run_for(work_budget);

        const int step = job->percent() / progress_step;
        if (job->status() != drawn_state || step != drawn_step) {
            paint(frame, describe(*job), job->percent());
            const bool finished = job->status() != copy_job::state::running;
            if (!screen.present(finished ? inkerface::refresh_mode::full
                                         : inkerface::refresh_mode::fast)) {
                return EXIT_FAILURE;
            }
            drawn_state = job->status();
            drawn_step = step;
        }
    }
    job->cancel(); // closing mid-copy: don't leave half a file behind
    return EXIT_SUCCESS;
}
```

### How it works

- **`copy_job` remembers its place.** Its open files, their positions and
  its state are members, so the job can stop after any block and continue
  on the next call. That is all slicing needs: work that is resumable.
- **`run_for()` is budgeted in time, not in blocks.** A fast medium does
  many blocks per call and a slow one fewer, but the loop always gets
  control back within about 50 ms, plus one block.
- **The loop waits only when idle.** While copying it uses
  `poll_for(0ms)` and never sleeps. Once the job has finished it uses
  `poll()`, which sleeps until a button is pressed, so an idle program
  uses no CPU.
- **Progress redraws are rationed.** The bar redraws every 10% and when
  the state changes. On the X4 each `present()` takes a few hundred
  milliseconds, during which nothing else runs. Refreshing for every
  block would make the copy many times slower and the device unresponsive.
- **Ending is clean.** Back calls `cancel()`, which closes both files and
  removes the incomplete copy. The same happens when the program ends
  mid-copy.

Run it with `make preview`. Then try the same with a large file on the
X4, where the difference between the blocking preparation and the
responsive copy is easy to see.

### Making any job sliceable

The pattern applies to every long job:

| Job | What `run_for()` does per step | State kept between calls |
| --- | --- | --- |
| Copy or checksum a file | one 4 KB block | open files |
| Parse a large text file | one block, split into lines | open file, partial last line |
| Walk a folder tree | one folder | a list of folders still to visit |
| Lay out a book | one paragraph or page | position in the text |
| Compute | a fixed number of iterations | loop counters, partial result |

Recursion doesn't slice well, because the call stack *is* the state, and
the stack can't be put aside. Replace it with an explicit list of work to
do. A tree walk then keeps a `std::vector` of folders still to visit
instead of recursing.

## Technique 2: check input from inside

Some operations can't be sliced from outside: one `http_client::send()`
downloads a whole file. But it calls back into your code while it works,
through the body sink and the cancel check (chapter 11). Check the
buttons from there, rate-limited so the checks don't slow the transfer:

```cpp
// Download url into out. Back cancels: the cancel check looks at the
// buttons at most every 100 ms (on the X4 each look takes ~10 ms).
inkerface::net_error download(inkerface::http_client &client,
                              inkerface::input &buttons,
                              inkerface::file &out,
                              const std::string &url) {
    using clock = std::chrono::steady_clock;
    auto next_look = clock::now();
    bool back = false;
    const auto cancel = [&]() {
        if (!back && clock::now() >= next_look) {
            next_look = clock::now() + std::chrono::milliseconds(100);
            back = buttons.poll_for(std::chrono::milliseconds(0)).back;
        }
        return back;
    };
    const auto save = [&out](std::span<const std::byte> chunk) {
        return out.write(chunk) == chunk.size();
    };
    return client.send({.url = url}, save, cancel).error;
}
```

Keep in mind:

- **Other buttons are consumed.** A confirm pressed during the download is
  read and dropped here. If it matters, remember it in a variable and act
  on it afterwards.
- **You can't redraw much from inside.** A callback can update a progress
  bar (rationed, as above), but a full screen change belongs in the main
  loop.
- **It needs the operation's help.** The cancel check is called while
  connecting, and between received chunks. A step that doesn't call back
  can't be interrupted, so give network calls a `timeout`.

The line reader from chapter 9 (`for_each_line()`) works the same way:
the callback runs for each line, and may check the buttons.

## Technique 3: never wait, poll

`tcp_socket::read(buffer)` waits for data up to the socket's timeout.
`read(buffer, 0ms)` doesn't wait at all: it returns the bytes that have
already arrived, or 0, and still notices when the server closes the
connection. The main loop can then serve the buttons and the socket
together:

```cpp
// Serve the buttons and a line-based socket from one loop. Each received
// line goes to on_line. Back, or the server closing, ends the loop.
void serve(inkerface::input &buttons,
           inkerface::tcp_socket &peer,
           const std::function<void(std::string_view)> &on_line) {
    using std::chrono::milliseconds;
    std::string partial;
    std::array<std::byte, 256> buffer{};
    while (peer && !buttons.closed()) {
        if (buttons.poll_for(milliseconds(50)).back) {
            return;
        }
        while (const std::size_t got = peer.read(buffer, milliseconds(0))) {
            partial.append(reinterpret_cast<const char *>(buffer.data()), got);
        }
        for (std::size_t end = partial.find('\n'); end != std::string::npos;
             end = partial.find('\n')) {
            on_line(std::string_view(partial).substr(0, end));
            partial.erase(0, end + 1);
        }
    }
    if (!partial.empty()) {
        on_line(partial); // a last line without a newline
    }
}
```

The loop never blocks on the socket. Its only wait is the 50 ms
`poll_for()`, which lets the program sleep between events instead of
spinning, and bounds how late socket data is noticed. When the server
closes the connection, `read()` records `net_error::closed`, the socket
tests false, and the loop ends.

The server side polls the same way. `tcp_listener::accept()` and
`udp_socket::receive()` wait 0 ms by default: `accept()` returns a
closed socket and `receive()` returns 0 when nothing has arrived. A
device that answers discovery broadcasts and takes connections, all
while watching the buttons, still needs only one loop:

```cpp
// Answer "who?" broadcasts with "here", take connections on port 8080,
// and hand each one to on_peer. Back ends the loop.
void serve_lan(inkerface::network &link,
               inkerface::input &buttons,
               const std::function<void(inkerface::tcp_socket &)> &on_peer) {
    using std::chrono::milliseconds;
    inkerface::udp_socket finder(link, 7001);
    inkerface::tcp_listener server(link, 8080);
    if (!finder || !server) {
        return; // not connected or hosting, or a port is taken (bind)
    }
    std::array<std::byte, 64> buffer{};
    while (!buttons.closed()) {
        if (buttons.poll_for(milliseconds(50)).back) {
            return;
        }
        inkerface::datagram_source from;
        if (finder.receive(buffer, from) > 0) {
            static_cast<void>(finder.send_to(
                from.address, from.port,
                std::as_bytes(std::span("here", 4))));
        }
        if (inkerface::tcp_socket peer = server.accept()) {
            on_peer(peer);
        }
    }
}
```

`on_peer` blocks the loop while it runs, so keep each exchange short, or
read the peer with `read(buffer, 0ms)` across several passes as `serve()`
does above.

## Other approaches

- **Coroutines (C++20).** A coroutine is a function that can suspend
  (`co_await`) and later resume where it left off, with its local
  variables kept. A job written as a coroutine reads like ordinary
  sequential code but suspends at every slice boundary. That removes the
  manual "remember where I was" state of technique 1. C++20 provides only
  the language mechanism, though, not a ready-made task type or
  scheduler, so you would add those yourself (about a hundred lines).
  Each suspended coroutine's frame lives on the heap. It's worth it once
  you have many interleaved jobs. For one or two, a job class is simpler.
- **Do work when idle.** Low-priority work (building an index, cleaning a
  cache) can run in slices only while no button has been pressed for a
  while, and pause as soon as the user acts.
- **Keep refreshes rare.** Of everything on the X4, the e-paper refresh
  blocks longest. Present only when something visible changed, combine
  several changes into one refresh, and prefer fast refreshes during
  interaction.

## Choosing

| | Threads (chapter 12) | Slicing | Checking from inside | Polling |
| --- | --- | --- | --- | --- |
| Extra RAM | 8 KB stack per thread | a few bytes of job state | none | none |
| Shared-data locking | needed | not needed | not needed | not needed |
| Data races possible | yes | no | no | no |
| Works with blocking library calls | yes | no | if they call back | no |
| Code shape | straight-line, in its own thread | state kept between steps | callbacks | event loop |
| Typical use | background network sync | file jobs, parsing, computing | downloads | sockets |

A practical rule: **start single-threaded**, slicing long jobs and
checking input from callbacks. Add a thread when a library call blocks
for long and offers no callback, or when work must keep going while the
UI itself is busy, as in the pager.

## Summary

- Responsiveness means returning to the main loop often, not using
  threads.
- `poll_for(0ms)` checks input without waiting. On the X4 it costs about
  10 ms, so budget work in time (about 50 ms) between checks.
- Slice long jobs into resumable objects with `run_for(budget)`. Keep
  their state in members and use explicit lists instead of recursion.
- Check input from callbacks (body sinks, cancel checks, line handlers),
  rate-limited.
- Poll sources such as sockets with `available()` or `read(buffer, 0ms)`,
  and read only what is there. `accept()` and `receive()` never wait by
  default, so a listener and a UDP socket share the same loop.
- Ration e-paper refreshes. They are the longest blocking step on the
  device.

Next: [Fonts](14-FONTS.md).
