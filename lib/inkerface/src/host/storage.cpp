//
// Implements host storage. Each volume is a folder under the directory
// named by the INKERFACE_STORAGE_ROOT environment variable:
// <root>/internal and <root>/card. Without that variable every volume is
// unavailable, so a program never writes somewhere unexpected. Folders
// are created on first use.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "../common/storage_backend.h"
#include "../posix/posix_storage.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>

namespace inkerface::detail {

namespace {

// Longest storage root accepted from the environment.
constexpr std::size_t max_root_length = 4096;

constexpr std::size_t volume_count = 2;

std::size_t index_of(volume where) {
    return where == volume::internal ? 0U : 1U;
}

const char *folder_of(volume where) {
    return where == volume::internal ? "internal" : "card";
}

std::optional<std::filesystem::path> root_from_environment() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once, single-threaded.
    const char *raw = std::getenv("INKERFACE_STORAGE_ROOT");
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string value(raw);
    if (value.empty() || value.size() > max_root_length) {
        std::cerr << "inkerface: ignoring invalid INKERFACE_STORAGE_ROOT\n";
        return std::nullopt;
    }
    return std::filesystem::path(value);
}

} // namespace

// Host storage: the root folder and which volume folders exist.
class storage_backend {
public:
    std::optional<std::filesystem::path> root = root_from_environment();
    std::array<bool, volume_count> ready{};

    std::filesystem::path folder(volume where) const {
        return *root / folder_of(where);
    }

    std::string native(volume where, const std::string &path) const {
        std::string full = folder(where).string();
        if (!path.empty()) {
            full += '/';
            full += path;
        }
        return full;
    }
};

// An open host file: a POSIX descriptor.
class file_backend {
public:
    int descriptor = -1;
};

// An open host listing.
class directory_backend {
public:
    posix::listing open;
};

void backend_deleter::operator()(storage_backend *backend) const noexcept {
    delete backend;
}

void backend_deleter::operator()(file_backend *backend) const noexcept {
    delete backend;
}

void backend_deleter::operator()(directory_backend *backend) const noexcept {
    delete backend;
}

storage_handle make_storage_backend() {
    return storage_handle(new storage_backend());
}

storage_error mount(storage_backend &disks, volume where) {
    if (!disks.root) {
        return storage_error::unavailable;
    }
    bool &ready = disks.ready.at(index_of(where));
    if (!ready) {
        std::error_code failure;
        std::filesystem::create_directories(disks.folder(where), failure);
        ready = !failure;
    }
    return ready ? storage_error::none : storage_error::unavailable;
}

file_opening open_file(storage_backend &disks,
                       volume where,
                       const std::string &path,
                       file_mode mode) {
    const posix::opened_file opened =
        posix::open_file(disks.native(where, path), mode);
    file_opening result;
    result.error = opened.error;
    result.size = opened.size;
    if (opened.error == storage_error::none) {
        result.handle.reset(new file_backend{opened.descriptor});
    }
    return result;
}

transfer read_file(file_backend &open, std::span<std::byte> out) {
    return posix::read_file(open.descriptor, out);
}

transfer write_file(file_backend &open, std::span<const std::byte> in) {
    return posix::write_file(open.descriptor, in);
}

storage_error seek_file(file_backend &open, std::uint64_t offset) {
    return posix::seek_file(open.descriptor, offset);
}

storage_error close_file(file_backend &open) {
    return posix::close_file(open.descriptor);
}

directory_opening open_directory(storage_backend &disks,
                                 volume where,
                                 const std::string &path) {
    posix::opened_listing opened =
        posix::open_directory(disks.native(where, path));
    directory_opening result;
    result.error = opened.error;
    if (opened.error == storage_error::none) {
        result.handle.reset(new directory_backend{std::move(opened.open)});
    }
    return result;
}

directory_step next_entry(directory_backend &listing) {
    return posix::next_entry(listing.open);
}

std::optional<entry_info>
info(storage_backend &disks, volume where, const std::string &path) {
    return posix::info(disks.native(where, path));
}

storage_error
make_directory(storage_backend &disks, volume where, const std::string &path) {
    return posix::make_directory(disks.native(where, path));
}

storage_error
remove(storage_backend &disks, volume where, const std::string &path) {
    return posix::remove(disks.native(where, path));
}

storage_error rename(storage_backend &disks,
                     volume where,
                     const std::string &from,
                     const std::string &to) {
    return posix::rename(disks.native(where, from), disks.native(where, to));
}

std::optional<std::uint64_t> free_space(storage_backend &disks, volume where) {
    std::error_code failure;
    const std::filesystem::space_info space =
        std::filesystem::space(disks.folder(where), failure);
    if (failure) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(space.available);
}

} // namespace inkerface::detail
