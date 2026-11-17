// Owns a bounded, exclusively locked disk image. Only missing images are
// created; existing files are never truncated or reformatted by this layer.
// MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace inkerface::detail {

class disk_image {
public:
    disk_image() = default;
    ~disk_image();
    disk_image(const disk_image &) = delete;
    disk_image &operator=(const disk_image &) = delete;
    disk_image(disk_image &&) = delete;
    disk_image &operator=(disk_image &&) = delete;

    // Exact size is part of this proof-of-concept's device geometry.
    [[nodiscard]] bool
    open(const std::filesystem::path &path, std::uint64_t capacity, bool flash);
    [[nodiscard]] bool read(std::uint64_t offset, std::span<std::byte> data);
    [[nodiscard]] bool write(std::uint64_t offset,
                             std::span<const std::byte> data);
    [[nodiscard]] bool erase(std::uint64_t offset, std::size_t length);
    [[nodiscard]] bool sync();
    [[nodiscard]] bool created() const {
        return created_;
    }
    [[nodiscard]] std::uint64_t capacity() const {
        return capacity_;
    }

private:
    int descriptor_ = -1;
    std::uint64_t capacity_ = 0;
    bool created_ = false;
};

} // namespace inkerface::detail
