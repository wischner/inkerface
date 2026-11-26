//
// Implements the host input source. Each line read from standard input
// names one semantic action: confirm, back, next, prev, page_next,
// page_prev, or power. Blank lines are ignored, unknown words are reported and
// skipped, and end of input closes the source. Lines are length-limited
// because standard input is untrusted.
//
// Standard input is read directly from its descriptor into a small
// buffer, not through std::cin, so a timed wait (poll_for) can never miss
// lines that a stdio buffer has already taken in.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/input.h>

#ifdef INKERFACE_HAS_PREVIEW
#include "preview.h"
#endif

#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <poll.h>
#include <unistd.h>

namespace inkerface::detail {

namespace {

// Longest accepted script line; longer lines are rejected whole.
constexpr std::size_t max_line_length = 64;

// Wait forever, in the int-milliseconds convention of poll().
constexpr int wait_forever = -1;

// Map one script word to the snapshot it produces.
std::optional<input_snapshot> parse_action(const std::string &word) {
    input_snapshot snapshot;
    if (word == "confirm") {
        snapshot.confirm = true;
    } else if (word == "back") {
        snapshot.back = true;
    } else if (word == "next") {
        snapshot.focus_next = true;
    } else if (word == "prev") {
        snapshot.focus_prev = true;
    } else if (word == "page_next") {
        snapshot.page_next = true;
    } else if (word == "page_prev") {
        snapshot.page_prev = true;
    } else if (word == "power") {
        snapshot.power = true;
    } else {
        return std::nullopt;
    }
    return snapshot;
}

// Clamp a wait to poll()'s int milliseconds.
int to_poll_timeout(std::chrono::milliseconds wait) {
    const auto count = wait.count();
    if (count <= 0) {
        return 0;
    }
    return count > INT_MAX ? INT_MAX : static_cast<int>(count);
}

// One line from the script, or a line that was too long to accept.
struct script_line {
    std::string text;
    bool too_long = false;
};

} // namespace

// Host input state: the script reader (or preview window) and whether
// input has ended.
class input_backend {
public:
    input_backend() = default;

    explicit input_backend(display &out) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe): single UI thread.
        const char *requested = std::getenv("INKERFACE_PREVIEW");
        if (requested != nullptr && std::string_view(requested) == "1") {
#ifdef INKERFACE_HAS_PREVIEW
            preview_ = std::make_unique<preview_window>(out);
#else
            static_cast<void>(out);
            throw std::runtime_error(
                "preview support was disabled at build time");
#endif
        }
    }

    // Wait up to timeout_ms (wait_forever: no limit) for one action.
    input_snapshot poll(int timeout_ms) {
#ifdef INKERFACE_HAS_PREVIEW
        if (preview_) {
            return preview_->poll(timeout_ms);
        }
#endif
        if (closed_) {
            return input_snapshot{};
        }
        const std::optional<script_line> line = next_line(timeout_ms);
        if (!line || line->text.empty()) {
            return input_snapshot{}; // timeout, end of input, or blank
        }
        const std::optional<input_snapshot> parsed =
            line->too_long ? std::nullopt : parse_action(line->text);
        if (!parsed) {
            std::cerr << "inkerface: unknown input action ignored\n";
            return input_snapshot{};
        }
        return *parsed;
    }

    [[nodiscard]] bool closed() const {
#ifdef INKERFACE_HAS_PREVIEW
        if (preview_) {
            return preview_->closed();
        }
#endif
        return closed_;
    }

private:
    // Return the next complete line, std::nullopt on timeout or at the
    // end of input (which also closes the source).
    std::optional<script_line> next_line(int timeout_ms) {
        while (true) {
            const std::size_t newline = pending_.find('\n');
            if (newline != std::string::npos) {
                script_line line{pending_.substr(0, newline), discarding_};
                pending_.erase(0, newline + 1);
                discarding_ = false;
                if (!line.text.empty() && line.text.back() == '\r') {
                    line.text.pop_back();
                }
                return line;
            }
            if (pending_.size() > max_line_length) {
                // Too long: drop it now, report it when it ends.
                pending_.clear();
                discarding_ = true;
            }
            if (end_of_input_) {
                closed_ = true;
                if (pending_.empty()) {
                    return std::nullopt;
                }
                script_line last{pending_, discarding_};
                pending_.clear();
                return last;
            }
            if (!read_more(timeout_ms)) {
                return std::nullopt; // timeout
            }
        }
    }

    // Read what standard input has; false when timeout_ms passed first.
    bool read_more(int timeout_ms) {
        if (timeout_ms != wait_forever) {
            pollfd watched{STDIN_FILENO, POLLIN, 0};
            const int ready = ::poll(&watched, 1, timeout_ms);
            if (ready == 0) {
                return false;
            }
            if (ready < 0 && errno == EINTR) {
                return true; // try again
            }
        }
        std::array<char, 256> buffer{};
        const ssize_t got = ::read(STDIN_FILENO, buffer.data(), buffer.size());
        if (got > 0) {
            pending_.append(buffer.data(), static_cast<std::size_t>(got));
        } else if (got == 0 || errno != EINTR) {
            end_of_input_ = true;
        }
        return true;
    }

    std::string pending_;
    bool discarding_ = false;
    bool end_of_input_ = false;
    bool closed_ = false;
#ifdef INKERFACE_HAS_PREVIEW
    std::unique_ptr<preview_window> preview_;
#endif
};

} // namespace inkerface::detail

namespace inkerface {

input::input()
    : backend_(std::make_unique<detail::input_backend>()) {}

input::input(display &out)
    : backend_(std::make_unique<detail::input_backend>(out)) {}

input::~input() = default;

input_snapshot input::poll() {
    return backend_->poll(detail::wait_forever);
}

input_snapshot input::poll_for(std::chrono::milliseconds wait) {
    return backend_->poll(detail::to_poll_timeout(wait));
}

bool input::closed() const {
    return backend_->closed();
}

} // namespace inkerface
