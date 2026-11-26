//
// Implements the host display. It emulates the Xteink X4 panel (800x480
// native, 1-bit) in memory and renders through the same FreeInk canvas as
// the device. When the INKERFACE_FRAME_PATH environment variable names a
// file, every presented frame is written there as a binary PBM image in
// logical (portrait) orientation; otherwise frames stay in memory only.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/display.h>
#include <inkerface/power.h>

#include "../freeink/canvas.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace inkerface::detail {

namespace {

// Xteink X4 panel geometry, so host frames match the device exactly.
constexpr panel_geometry x4_panel{800, 480, 800 / 8};

// Longest frame path accepted from the environment.
constexpr std::size_t max_frame_path_length = 4096;

// Read and validate the optional frame output path.
std::optional<std::filesystem::path> frame_path_from_environment() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once, single-threaded.
    const char *raw = std::getenv("INKERFACE_FRAME_PATH");
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string value(raw);
    if (value.empty() || value.size() > max_frame_path_length) {
        std::cerr << "inkerface: ignoring invalid INKERFACE_FRAME_PATH\n";
        return std::nullopt;
    }
    return std::filesystem::path(value);
}

} // namespace

// Host display state: an owned framebuffer and the canvas over it.
class display_backend {
public:
    display_backend()
        : pixels_(static_cast<std::size_t>(x4_panel.width_bytes) *
                      static_cast<std::size_t>(x4_panel.height),
                  std::uint8_t{0xFF})
        , canvas_(pixels_, x4_panel)
        , frame_path_(frame_path_from_environment()) {}

    canvas &drawing() {
        return canvas_;
    }

    // Write the logical frame as PBM when a frame path is configured.
    bool present(refresh_mode /*mode*/) {
        if (!frame_path_) {
            return true;
        }
        std::ofstream out(*frame_path_, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "inkerface: cannot open frame file\n";
            return false;
        }
        const size logical = canvas_.extent();
        out << "P4\n" << logical.width << ' ' << logical.height << '\n';
        const auto row_bytes =
            (static_cast<std::size_t>(logical.width) + 7U) / 8U;
        std::vector<char> row(row_bytes);
        for (coord y = 0; y < logical.height; ++y) {
            std::fill(row.begin(), row.end(), char{0});
            for (coord x = 0; x < logical.width; ++x) {
                // PBM: a set bit is black.
                if (canvas_.pixel_at(point{x, y}) == color::black) {
                    const auto column = static_cast<std::size_t>(x);
                    row[column / 8U] = static_cast<char>(
                        static_cast<unsigned char>(row[column / 8U]) |
                        (0x80U >> (column % 8U)));
                }
            }
            out.write(row.data(), static_cast<std::streamsize>(row.size()));
        }
        out.flush();
        if (!out) {
            std::cerr << "inkerface: cannot write frame file\n";
            return false;
        }
        return true;
    }

private:
    std::vector<std::uint8_t> pixels_;
    canvas canvas_;
    std::optional<std::filesystem::path> frame_path_;
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

void power_off(display & /*screen*/) {
    // A desktop has nothing to switch off. The frame already presented is
    // what the device would leave on its panel; the program ends instead.
}

} // namespace inkerface
