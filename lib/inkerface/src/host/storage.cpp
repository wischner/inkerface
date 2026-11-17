// Implements native storage on littlefs and FAT disk images, under
// INKERFACE_STORAGE_ROOT. Missing images are created on first use; existing
// images are mounted without formatting. No host directories are exposed as
// application files. Hardware adapters are independent of this implementation.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih

#include "../common/storage_backend.h"
#include "image_volume.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace inkerface::detail {

namespace {

std::size_t index_of(volume where) {
    return where == volume::internal ? 0U : 1U;
}

std::optional<std::filesystem::path> root_from_environment() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once, on the UI thread.
    const char *raw = std::getenv("INKERFACE_STORAGE_ROOT");
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string value(raw);
    if (value.empty() || value.size() > 4096) {
        return std::nullopt;
    }
    return std::filesystem::path(value);
}

// FAT would otherwise interpret a drive prefix or silently trim names.
bool valid_fat_path(const std::string &path) {
    if (path.find_first_of("<>:\"|?*") != std::string::npos) {
        return false;
    }
    for (std::size_t index = 0; index < path.size(); ++index) {
        if ((path[index] == ' ' || path[index] == '.') &&
            (index + 1 == path.size() || path[index + 1] == '/')) {
            return false;
        }
    }
    return true;
}

std::string fat_path(const std::string &path) {
    return "0:/" + path;
}

} // namespace

class storage_backend {
public:
    std::optional<std::filesystem::path> root = root_from_environment();
    std::array<std::unique_ptr<image_volume>, 2> volumes;
    image_volume &at(volume where) {
        return *volumes.at(index_of(where));
    }
};

class file_backend {
public:
    explicit file_backend(image_volume &disk, file_mode access)
        : owner(disk)
        , mode(access) {}
    ~file_backend() {
        static_cast<void>(close_file(*this));
    }
    file_backend(const file_backend &) = delete;
    file_backend &operator=(const file_backend &) = delete;
    file_backend(file_backend &&) = delete;
    file_backend &operator=(file_backend &&) = delete;

    image_volume &owner;
    file_mode mode;
    lfs_file_t flash{};
    FIL card{};
    bool opened = false;
};

class directory_backend {
public:
    explicit directory_backend(image_volume &disk)
        : owner(disk) {}
    ~directory_backend() {
        if (opened) {
            if (owner.flash()) {
                lfs_dir_close(&owner.littlefs(), &flash);
            } else {
                f_closedir(&card);
            }
        }
    }
    directory_backend(const directory_backend &) = delete;
    directory_backend &operator=(const directory_backend &) = delete;
    directory_backend(directory_backend &&) = delete;
    directory_backend &operator=(directory_backend &&) = delete;

    image_volume &owner;
    lfs_dir_t flash{};
    DIR card{};
    bool opened = false;
};

void backend_deleter::operator()(storage_backend *backend) const noexcept {
    // This is unique_ptr's deleter for an opaque backend.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete backend;
}
void backend_deleter::operator()(file_backend *backend) const noexcept {
    // This is unique_ptr's deleter for an opaque backend.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete backend;
}
void backend_deleter::operator()(directory_backend *backend) const noexcept {
    // This is unique_ptr's deleter for an opaque backend.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete backend;
}

storage_handle make_storage_backend() {
    return storage_handle(new storage_backend());
}

storage_error mount(storage_backend &disks, volume where) {
    if (!disks.root) {
        return storage_error::unavailable;
    }
    auto &slot = disks.volumes.at(index_of(where));
    if (slot) {
        return storage_error::none;
    }
    std::error_code error;
    std::filesystem::create_directories(*disks.root, error);
    if (error) {
        return storage_error::unavailable;
    }
    auto candidate = std::make_unique<image_volume>(where);
    const auto path =
        *disks.root / (where == volume::internal ? "internal.lfs" : "card.fat");
    if (!candidate->mount(path)) {
        return storage_error::unavailable;
    }
    slot = std::move(candidate);
    return storage_error::none;
}

file_opening open_file(storage_backend &disks,
                       volume where,
                       const std::string &path,
                       file_mode mode) {
    file_opening result;
    auto &disk = disks.at(where);
    if (!disk.flash() && !valid_fat_path(path)) {
        result.error = storage_error::invalid_path;
        return result;
    }
    if (const auto found = info(disks, where, path);
        found && found->is_directory) {
        result.error = storage_error::is_a_directory;
        return result;
    }
    file_handle handle(new file_backend(disk, mode));
    if (disk.flash()) {
        const int flags =
            mode == file_mode::read
                ? LFS_O_RDONLY
                : LFS_O_WRONLY | LFS_O_CREAT |
                      (mode == file_mode::write ? LFS_O_TRUNC : 0);
        result.error = from_littlefs(lfs_file_open(
            &disk.littlefs(), &handle->flash, path.c_str(), flags));
    } else {
        const BYTE flags =
            mode == file_mode::read
                ? FA_READ
                : FA_WRITE | (mode == file_mode::write ? FA_CREATE_ALWAYS
                                                       : FA_OPEN_ALWAYS);
        result.error =
            from_fatfs(f_open(&handle->card, fat_path(path).c_str(), flags));
    }
    if (result.error != storage_error::none) {
        return result;
    }
    handle->opened = true;
    if (disk.flash()) {
        const auto length = lfs_file_size(&disk.littlefs(), &handle->flash);
        if (length < 0) {
            result.error = from_littlefs(length);
            return result;
        }
        result.size = static_cast<std::uint64_t>(length);
    } else {
        result.size = f_size(&handle->card);
    }
    if (mode == file_mode::append) {
        result.error = seek_file(*handle, result.size);
        if (result.error != storage_error::none) {
            return result;
        }
    }
    result.handle = std::move(handle);
    return result;
}

transfer read_file(file_backend &open, std::span<std::byte> out) {
    transfer done;
    if (!open.opened || open.mode != file_mode::read) {
        done.error = storage_error::io;
        return done;
    }
    while (done.count < out.size()) {
        const auto part = out.subspan(
            done.count, std::min<std::size_t>(out.size() - done.count, 65536));
        std::size_t received = 0;
        if (open.owner.flash()) {
            const auto count =
                lfs_file_read(&open.owner.littlefs(),
                              &open.flash,
                              part.data(),
                              static_cast<lfs_size_t>(part.size()));
            if (count < 0) {
                done.error = from_littlefs(count);
            } else {
                received = static_cast<std::size_t>(count);
            }
        } else {
            UINT count = 0;
            done.error = from_fatfs(f_read(&open.card,
                                           part.data(),
                                           static_cast<UINT>(part.size()),
                                           &count));
            received = count;
        }
        done.count += received;
        if (done.error != storage_error::none || received < part.size()) {
            break;
        }
    }
    return done;
}

transfer write_file(file_backend &open, std::span<const std::byte> in) {
    transfer done;
    if (!open.opened || open.mode == file_mode::read) {
        done.error = storage_error::io;
        return done;
    }
    while (done.count < in.size()) {
        const auto part = in.subspan(
            done.count, std::min<std::size_t>(in.size() - done.count, 65536));
        std::size_t written = 0;
        if (open.owner.flash()) {
            const auto count =
                lfs_file_write(&open.owner.littlefs(),
                               &open.flash,
                               part.data(),
                               static_cast<lfs_size_t>(part.size()));
            if (count < 0) {
                done.error = from_littlefs(count);
            } else {
                written = static_cast<std::size_t>(count);
            }
        } else {
            UINT count = 0;
            done.error = from_fatfs(f_write(&open.card,
                                            part.data(),
                                            static_cast<UINT>(part.size()),
                                            &count));
            written = count;
        }
        done.count += written;
        if (done.error == storage_error::none && written < part.size()) {
            done.error = storage_error::no_space;
        }
        if (done.error != storage_error::none) {
            break;
        }
    }
    return done;
}

storage_error seek_file(file_backend &open, std::uint64_t offset) {
    if (!open.opened || offset > static_cast<std::uint64_t>(
                                     std::numeric_limits<lfs_soff_t>::max())) {
        return storage_error::io;
    }
    if (open.owner.flash()) {
        const auto result = lfs_file_seek(&open.owner.littlefs(),
                                          &open.flash,
                                          static_cast<lfs_soff_t>(offset),
                                          LFS_SEEK_SET);
        return result < 0 ? from_littlefs(result) : storage_error::none;
    }
    return from_fatfs(f_lseek(&open.card, static_cast<FSIZE_t>(offset)));
}

storage_error close_file(file_backend &open) {
    if (!open.opened) {
        return storage_error::none;
    }
    open.opened = false;
    const auto error =
        open.owner.flash()
            ? from_littlefs(lfs_file_close(&open.owner.littlefs(), &open.flash))
            : from_fatfs(f_close(&open.card));
    return error != storage_error::none ? error
           : open.owner.image().sync()  ? storage_error::none
                                        : storage_error::io;
}

directory_opening
open_directory(storage_backend &disks, volume where, const std::string &path) {
    directory_opening result;
    auto &disk = disks.at(where);
    if (!disk.flash() && !valid_fat_path(path)) {
        result.error = storage_error::invalid_path;
        return result;
    }
    if (const auto found = info(disks, where, path);
        found && !found->is_directory) {
        result.error = storage_error::not_a_directory;
        return result;
    }
    directory_handle handle(new directory_backend(disk));
    result.error =
        disk.flash()
            ? from_littlefs(lfs_dir_open(&disk.littlefs(),
                                         &handle->flash,
                                         path.empty() ? "/" : path.c_str()))
            : from_fatfs(f_opendir(&handle->card, fat_path(path).c_str()));
    if (result.error == storage_error::none) {
        handle->opened = true;
        result.handle = std::move(handle);
    }
    return result;
}

directory_step next_entry(directory_backend &listing) {
    directory_step step;
    while (true) {
        directory_entry entry;
        if (listing.owner.flash()) {
            lfs_info raw{};
            const int found =
                lfs_dir_read(&listing.owner.littlefs(), &listing.flash, &raw);
            if (found <= 0) {
                step.error = from_littlefs(found);
                return step;
            }
            entry = {std::data(raw.name), raw.type == LFS_TYPE_DIR, raw.size};
        } else {
            FILINFO raw{};
            step.error = from_fatfs(f_readdir(&listing.card, &raw));
            if (step.error != storage_error::none || raw.fname[0] == '\0') {
                return step;
            }
            entry = {
                std::data(raw.fname), (raw.fattrib & AM_DIR) != 0, raw.fsize};
        }
        if (entry.name == "." || entry.name == ".." ||
            entry.name.size() > max_path_length) {
            continue;
        }
        if (entry.is_directory) {
            entry.size = 0;
        }
        step.entry = std::move(entry);
        return step;
    }
}

std::optional<entry_info>
info(storage_backend &disks, volume where, const std::string &path) {
    auto &disk = disks.at(where);
    if (disk.flash()) {
        lfs_info found{};
        if (lfs_stat(&disk.littlefs(), path.c_str(), &found) == 0) {
            return entry_info{found.type == LFS_TYPE_DIR,
                              found.type == LFS_TYPE_DIR ? 0U : found.size};
        }
    } else if (valid_fat_path(path)) {
        FILINFO found{};
        if (f_stat(fat_path(path).c_str(), &found) == FR_OK) {
            const bool folder = (found.fattrib & AM_DIR) != 0;
            return entry_info{folder, folder ? 0U : found.fsize};
        }
    }
    return std::nullopt;
}

storage_error
make_directory(storage_backend &disks, volume where, const std::string &path) {
    auto &disk = disks.at(where);
    if (disk.flash()) {
        return from_littlefs(lfs_mkdir(&disk.littlefs(), path.c_str()));
    }
    return valid_fat_path(path) ? from_fatfs(f_mkdir(fat_path(path).c_str()))
                                : storage_error::invalid_path;
}

storage_error
remove(storage_backend &disks, volume where, const std::string &path) {
    auto &disk = disks.at(where);
    if (disk.flash()) {
        return from_littlefs(lfs_remove(&disk.littlefs(), path.c_str()));
    }
    if (!valid_fat_path(path)) {
        return storage_error::invalid_path;
    }
    const auto found = info(disks, where, path);
    const auto result = f_unlink(fat_path(path).c_str());
    return result == FR_DENIED && found && found->is_directory
               ? storage_error::not_empty
               : from_fatfs(result);
}

storage_error rename(storage_backend &disks,
                     volume where,
                     const std::string &from,
                     const std::string &to) {
    auto &disk = disks.at(where);
    if (disk.flash()) {
        return from_littlefs(
            lfs_rename(&disk.littlefs(), from.c_str(), to.c_str()));
    }
    return valid_fat_path(from) && valid_fat_path(to)
               ? from_fatfs(
                     f_rename(fat_path(from).c_str(), fat_path(to).c_str()))
               : storage_error::invalid_path;
}

std::optional<std::uint64_t> free_space(storage_backend &disks, volume where) {
    auto &disk = disks.at(where);
    if (disk.flash()) {
        const auto used = lfs_fs_size(&disk.littlefs());
        if (used >= 0 &&
            static_cast<std::uint64_t>(used) <= internal_image_bytes / 4096) {
            return internal_image_bytes -
                   static_cast<std::uint64_t>(used) * 4096;
        }
    } else {
        DWORD clusters = 0;
        FATFS *filesystem = nullptr;
        if (f_getfree("0:", &clusters, &filesystem) == FR_OK &&
            filesystem != nullptr) {
            return static_cast<std::uint64_t>(clusters) * filesystem->csize *
                   512;
        }
    }
    return std::nullopt;
}

} // namespace inkerface::detail
