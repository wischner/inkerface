//
// Implements the application main loop and screen switching.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "app.h"

#include <cstdlib>

namespace app {

namespace {

// Compile-time check that every screen alternative meets the contract.
template <typename variant> struct all_screens;

template <typename... types> struct all_screens<std::variant<types...>> {
    static constexpr bool value = (ui::screen<types> && ...);
};

static_assert(all_screens<any_screen>::value,
              "every any_screen alternative must satisfy ui::screen");

} // namespace

application::application(inkerface::display &out, inkerface::input &in)
    : display_(&out)
    , input_(&in) {
    stack_.reserve(max_screen_depth);
    stack_.push_back(make_screen(ui::screen_id::hello_world));
}

int application::run() {
    paint();
    if (!display_->present(inkerface::refresh_mode::full)) {
        return EXIT_FAILURE;
    }
    while (!input_->closed()) {
        const inkerface::input_snapshot in = input_->poll();
        if (!in.any() || !dispatch(in)) {
            continue;
        }
        paint();
        if (!display_->present(inkerface::refresh_mode::fast)) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

bool application::dispatch(const inkerface::input_snapshot &in) {
    const ui::screen_result result = std::visit(
        [&in](auto &top) {
            return top.handle(in);
        },
        stack_.back());
    switch (result.action) {
    case ui::navigation::stay:
        return false;
    case ui::navigation::repaint:
        return true;
    case ui::navigation::open:
        if (stack_.size() >= max_screen_depth) {
            return false;
        }
        stack_.push_back(make_screen(result.target));
        return true;
    case ui::navigation::close:
        if (stack_.size() <= 1) {
            return false;
        }
        stack_.pop_back();
        return true;
    }
    return false;
}

void application::paint() {
    inkerface::surface frame = display_->frame();
    std::visit(
        [&frame](const auto &top) {
            top.paint(frame);
        },
        stack_.back());
}

std::size_t application::depth() const {
    return stack_.size();
}

any_screen application::make_screen(ui::screen_id id) {
    switch (id) {
    case ui::screen_id::hello_world:
        return ui::hello_world_screen{};
    }
    return ui::hello_world_screen{};
}

} // namespace app
