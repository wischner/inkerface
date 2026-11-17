// Owns one native filesystem and its disk image. Files/listings borrow the
// mounted volume, following the public storage lifetime contract.
// MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
#pragma once

#include "disk_image.h"

#include <inkerface/storage.h>

#include <ff.h>
#include <lfs.h>

#include <filesystem>

namespace inkerface::detail {

inline constexpr std::uint64_t internal_image_bytes = 0x9e0000;
inline constexpr std::uint64_t card_image_bytes =
    std::uint64_t{32} * 1024 * 1024;

class image_volume {
public:
    explicit image_volume(volume where)
        : kind_(where) {}
    ~image_volume();
    image_volume(const image_volume &) = delete;
    image_volume &operator=(const image_volume &) = delete;
    image_volume(image_volume &&) = delete;
    image_volume &operator=(image_volume &&) = delete;

    [[nodiscard]] bool mount(const std::filesystem::path &path);
    [[nodiscard]] bool flash() const {
        return kind_ == volume::internal;
    }
    [[nodiscard]] lfs_t &littlefs() {
        return littlefs_;
    }
    [[nodiscard]] FATFS &fatfs() {
        return fatfs_;
    }
    [[nodiscard]] disk_image &image() {
        return image_;
    }

private:
    volume kind_;
    disk_image image_;
    lfs_config config_{};
    lfs_t littlefs_{};
    FATFS fatfs_{};
    bool mounted_ = false;
};

[[nodiscard]] storage_error from_littlefs(int result);
[[nodiscard]] storage_error from_fatfs(FRESULT result);

} // namespace inkerface::detail
