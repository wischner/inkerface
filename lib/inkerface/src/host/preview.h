// Declares the optional Linux preview window. All native window types
// stay inside this host adapter; the window borrows the application display.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih

#pragma once

#include <inkerface/display.h>
#include <inkerface/input.h>

#include <memory>

namespace inkerface::detail {

class preview_backend;

// Owns a desktop window and its event source, confined to the UI thread.
class preview_window {
public:
    // Open a window; out must outlive this object. Throws on failure.
    explicit preview_window(display &out);
    ~preview_window();
    preview_window(const preview_window &) = delete;
    preview_window &operator=(const preview_window &) = delete;
    preview_window(preview_window &&) = delete;
    preview_window &operator=(preview_window &&) = delete;

    // Show the current frame if it changed, wait up to timeout_ms (-1: no
    // limit) for an event, and map keyboard input. Returns an empty
    // snapshot on timeout.
    [[nodiscard]] input_snapshot poll(int timeout_ms);

    // True after the user closes the window.
    [[nodiscard]] bool closed() const;

private:
    std::unique_ptr<preview_backend> backend_;
};

} // namespace inkerface::detail
