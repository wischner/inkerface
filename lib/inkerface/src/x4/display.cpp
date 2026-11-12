//
// Implements the Xteink X4 display on FreeInkDisplay. Bring-up follows
// the FreeInk SDK order: assert board power rails, resolve the panel
// controller of this production batch, then begin the display. The
// framebuffer is owned by FreeInkDisplay; the canvas only borrows it.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/display.h>

#include "../freeink/canvas.h"

#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <XteinkDetect.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace inkerface::detail {

namespace {

// Bring the panel up and return its framebuffer.
std::span<std::uint8_t> start_panel(EInkDisplay &panel) {
    BoardConfig::holdPowerRails();
    // Some batches ship a different controller; no-op when not needed.
    static_cast<void>(freeink::applyXteinkDisplayController());
    panel.begin();
    if (!panel.framebufferReady()) {
        throw std::runtime_error("display framebuffer unavailable");
    }
    panel.clearScreen(0xFF);
    return std::span<std::uint8_t>(
        panel.getFrameBuffer(),
        static_cast<std::size_t>(panel.getBufferSize()));
}

// Read the panel geometry selected by begin().
panel_geometry geometry_of(const EInkDisplay &panel) {
    return panel_geometry{static_cast<coord>(panel.getDisplayWidth()),
                          static_cast<coord>(panel.getDisplayHeight()),
                          static_cast<coord>(panel.getDisplayWidthBytes())};
}

} // namespace

// Device display state. Member order matters: the panel must be started
// before the canvas borrows its framebuffer.
class display_backend {
public:
    display_backend()
        // Pin arguments are legacy; FreeInk reads pins from BoardConfig.
        : panel_(0, 0, 0, 0, 0, 0)
        , pixels_(start_panel(panel_))
        , canvas_(pixels_, geometry_of(panel_)) {}

    canvas &drawing() {
        return canvas_;
    }

    bool present(refresh_mode mode) {
        panel_.displayBuffer(mode == refresh_mode::full
                                 ? EInkDisplay::FULL_REFRESH
                                 : EInkDisplay::FAST_REFRESH);
        return true;
    }

private:
    EInkDisplay panel_;
    std::span<std::uint8_t> pixels_;
    canvas canvas_;
};

} // namespace inkerface::detail

namespace inkerface {

display::display()
    : backend_(std::make_unique<detail::display_backend>()) {}

display::~display() = default;

surface display::frame() {
    return surface(backend_->drawing());
}

bool display::present(refresh_mode mode) {
    return backend_->present(mode);
}

font_error display::load_font(font face, std::span<const std::uint8_t> data) {
    return backend_->drawing().load_font(face.slot(), data);
}

void display::reset_font(font face) {
    backend_->drawing().reset_font(face.slot());
}

} // namespace inkerface
