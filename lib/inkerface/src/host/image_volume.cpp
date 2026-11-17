// Connects littlefs erase/program operations and FatFs sector I/O to image
// files. The FatFs C callback API requires one non-owning disk pointer;
// its lifetime is confined to the sole mounted card (storage is
// UI-thread-only). MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
#include "image_volume.h"

#include <diskio.h>

#include <array>
#include <cstddef>
#include <span>

namespace {

// FatFs C callbacks carry no context; mount owns this borrowed pointer.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inkerface::detail::disk_image *card_disk = nullptr;

constexpr std::uint64_t sector_size = 512;

bool flash_range(const lfs_config *config,
                 lfs_block_t block,
                 lfs_off_t offset,
                 lfs_size_t length) {
    return block < config->block_count && offset <= config->block_size &&
           length <= config->block_size - offset;
}

int flash_read(const lfs_config *config,
               lfs_block_t block,
               lfs_off_t offset,
               void *buffer,
               lfs_size_t length) {
    auto &disk = *static_cast<inkerface::detail::disk_image *>(config->context);
    if (!flash_range(config, block, offset, length)) {
        return LFS_ERR_IO;
    }
    return disk.read(static_cast<std::uint64_t>(block) * config->block_size +
                         offset,
                     {static_cast<std::byte *>(buffer), length})
               ? 0
               : LFS_ERR_IO;
}

int flash_program(const lfs_config *config,
                  lfs_block_t block,
                  lfs_off_t offset,
                  const void *buffer,
                  lfs_size_t length) {
    auto &disk = *static_cast<inkerface::detail::disk_image *>(config->context);
    if (!flash_range(config, block, offset, length) ||
        offset % config->prog_size != 0 || length % config->prog_size != 0) {
        return LFS_ERR_IO;
    }
    const auto address =
        static_cast<std::uint64_t>(block) * config->block_size + offset;
    const std::span data(static_cast<const std::byte *>(buffer), length);
    std::array<std::byte, 4096> previous{};
    if (length > previous.size() ||
        !disk.read(address, std::span(previous).first(length))) {
        return LFS_ERR_IO;
    }
    for (std::size_t index = 0; index < length; ++index) {
        // NOR flash cannot turn a zero back into a one without erase.
        if ((previous[index] & data[index]) != data[index]) {
            return LFS_ERR_CORRUPT;
        }
    }
    return disk.write(address, data) ? 0 : LFS_ERR_IO;
}

int flash_erase(const lfs_config *config, lfs_block_t block) {
    auto &disk = *static_cast<inkerface::detail::disk_image *>(config->context);
    return block < config->block_count &&
                   disk.erase(static_cast<std::uint64_t>(block) *
                                  config->block_size,
                              config->block_size)
               ? 0
               : LFS_ERR_IO;
}

int flash_sync(const lfs_config *config) {
    return static_cast<inkerface::detail::disk_image *>(config->context)->sync()
               ? 0
               : LFS_ERR_IO;
}

} // namespace

extern "C" {

DSTATUS disk_initialize(BYTE drive) {
    return drive == 0 && card_disk != nullptr ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE drive) {
    return disk_initialize(drive);
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count) {
    if (disk_status(drive) != 0) {
        return RES_NOTRDY;
    }
    if (buffer == nullptr || count == 0 ||
        count > card_disk->capacity() / sector_size) {
        return RES_PARERR;
    }
    return card_disk->read(
               static_cast<std::uint64_t>(sector) * sector_size,
               std::as_writable_bytes(std::span(
                   buffer, static_cast<std::size_t>(count) * sector_size)))
               ? RES_OK
               : RES_ERROR;
}

DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count) {
    if (disk_status(drive) != 0) {
        return RES_NOTRDY;
    }
    if (buffer == nullptr || count == 0 ||
        count > card_disk->capacity() / sector_size) {
        return RES_PARERR;
    }
    return card_disk->write(
               static_cast<std::uint64_t>(sector) * sector_size,
               std::as_bytes(std::span(
                   buffer, static_cast<std::size_t>(count) * sector_size)))
               ? RES_OK
               : RES_ERROR;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer) {
    if (disk_status(drive) != 0) {
        return RES_NOTRDY;
    }
    if (command == CTRL_SYNC) {
        return card_disk->sync() ? RES_OK : RES_ERROR;
    }
    if (buffer == nullptr) {
        return RES_PARERR;
    }
    if (command == GET_SECTOR_COUNT) {
        *static_cast<LBA_t *>(buffer) =
            static_cast<LBA_t>(card_disk->capacity() / sector_size);
    } else if (command == GET_BLOCK_SIZE) {
        *static_cast<DWORD *>(buffer) = 1;
    } else {
        return RES_PARERR;
    }
    return RES_OK;
}

} // extern "C"

namespace inkerface::detail {

image_volume::~image_volume() {
    if (mounted_ && flash()) {
        lfs_unmount(&littlefs_);
    }
    if (card_disk == &image_) {
        f_mount(nullptr, "0:", 0);
        card_disk = nullptr;
    }
}

bool image_volume::mount(const std::filesystem::path &path) {
    if (mounted_) {
        return true;
    }
    if ((!flash() && card_disk != nullptr) ||
        !image_.open(
            path, flash() ? internal_image_bytes : card_image_bytes, flash())) {
        return false;
    }
    if (flash()) {
        config_.context = &image_;
        config_.read = flash_read;
        config_.prog = flash_program;
        config_.erase = flash_erase;
        config_.sync = flash_sync;
        config_.read_size = 16;
        config_.prog_size = 16;
        config_.block_size = 4096;
        config_.block_count =
            static_cast<lfs_size_t>(internal_image_bytes / 4096);
        config_.block_cycles = 500;
        config_.cache_size = 256;
        config_.lookahead_size = 32;
        if (image_.created() && lfs_format(&littlefs_, &config_) != 0) {
            return false;
        }
        mounted_ = lfs_mount(&littlefs_, &config_) == 0;
    } else {
        card_disk = &image_;
        if (image_.created()) {
            std::array<std::byte, 4096> workspace{};
            const MKFS_PARM options{FM_FAT | FM_SFD, 1, 0, 512, 0};
            if (f_mkfs("0:", &options, workspace.data(), workspace.size()) !=
                FR_OK) {
                return false;
            }
        }
        mounted_ = f_mount(&fatfs_, "0:", 1) == FR_OK;
    }
    return mounted_;
}

storage_error from_littlefs(int result) {
    switch (result) {
    case 0:
        return storage_error::none;
    case LFS_ERR_NOENT:
        return storage_error::not_found;
    case LFS_ERR_EXIST:
        return storage_error::already_exists;
    case LFS_ERR_NOTDIR:
        return storage_error::not_a_directory;
    case LFS_ERR_ISDIR:
        return storage_error::is_a_directory;
    case LFS_ERR_NOTEMPTY:
        return storage_error::not_empty;
    case LFS_ERR_NOSPC:
    case LFS_ERR_FBIG:
        return storage_error::no_space;
    case LFS_ERR_NAMETOOLONG:
    case LFS_ERR_INVAL:
        return storage_error::invalid_path;
    default:
        return storage_error::io;
    }
}

storage_error from_fatfs(FRESULT result) {
    switch (result) {
    case FR_OK:
        return storage_error::none;
    case FR_NO_FILE:
    case FR_NO_PATH:
        return storage_error::not_found;
    case FR_EXIST:
        return storage_error::already_exists;
    case FR_INVALID_NAME:
        return storage_error::invalid_path;
    case FR_TOO_MANY_OPEN_FILES:
        return storage_error::too_many_open;
    case FR_NOT_READY:
    case FR_NO_FILESYSTEM:
        return storage_error::unavailable;
    default:
        return storage_error::io;
    }
}

} // namespace inkerface::detail
