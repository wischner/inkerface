//
// Tests the power action and power_off() on the host: a `power` line on
// standard input becomes input_snapshot::power, counts as input, and
// power_off() returns at once, leaving the presented frame alone, because
// a desktop has nothing to switch off.
//
// Standard input is replaced by a pipe for the test and restored after,
// so the rest of the suite reads what it always did.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include <inkerface/color.h>
#include <inkerface/display.h>
#include <inkerface/input.h>
#include <inkerface/power.h>
#include <inkerface/surface.h>

#include <string_view>

#include <unistd.h>

namespace tests {

namespace {

// Feed text to standard input for the life of this object.
class scripted_stdin {
public:
    explicit scripted_stdin(std::string_view script)
        : saved_(::dup(STDIN_FILENO)) {
        int ends[2] = {-1, -1};
        if (::pipe(ends) != 0) {
            return;
        }
        static_cast<void>(::write(ends[1], script.data(), script.size()));
        ::close(ends[1]);
        ::dup2(ends[0], STDIN_FILENO);
        ::close(ends[0]);
    }

    ~scripted_stdin() {
        ::dup2(saved_, STDIN_FILENO);
        ::close(saved_);
    }

    scripted_stdin(const scripted_stdin &) = delete;
    scripted_stdin &operator=(const scripted_stdin &) = delete;
    scripted_stdin(scripted_stdin &&) = delete;
    scripted_stdin &operator=(scripted_stdin &&) = delete;

private:
    int saved_;
};

} // namespace

void run_power_tests(test_context &context) {
    {
        const scripted_stdin script("power\nconfirm\n");
        inkerface::input buttons;

        const inkerface::input_snapshot first = buttons.poll();
        context.check(first.power && !first.confirm,
                      "a power line is the power action");
        context.check(first.any(), "a power hold counts as input");

        const inkerface::input_snapshot second = buttons.poll();
        context.check(second.confirm && !second.power,
                      "other actions do not carry power");
    }

    inkerface::display screen;
    inkerface::surface frame = screen.frame();
    frame.clear(inkerface::color::white);
    frame.fill_rect({0, 0, 8, 8}, inkerface::color::black);
    static_cast<void>(screen.present(inkerface::refresh_mode::full));

    inkerface::power_off(screen);
    context.check(screen.frame().pixel_at({0, 0}) == inkerface::color::black,
                  "power_off returns on the host and leaves the frame");
}

} // namespace tests
