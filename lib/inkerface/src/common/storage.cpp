//
// Implements the public storage, file and directory classes on top of
// the platform primitives in storage_backend.h. Path validation is the
// trust boundary: every path from the application is checked here
// before any platform code sees it.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/directory.h>
#include <inkerface/file.h>
#include <inkerface/storage.h>

#include "storage_backend.h"

#include <algorithm>
#include <string>
#include <utility>

namespace inkerface {

namespace {

// Check one path against the rules documented with max_path_length.
storage_error check_path(std::string_view path, bool allow_root) {
    if (path.empty()) {
        return allow_root ? storage_error::none : storage_error::invalid_path;
    }
    if (path.size() > max_path_length) {
        return storage_error::invalid_path;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view name = path.substr(start, end - start);
        if (name.empty() || name == "." || name == "..") {
            return storage_error::invalid_path;
        }
        for (const char c : name) {
            const auto code = static_cast<unsigned char>(c);
            if (code < 0x20U || code == 0x7FU || c == '\\') {
                return storage_error::invalid_path;
            }
        }
        start = end + 1;
    }
    return storage_error::none;
}

// Validate a path and mount its volume; none when both are fine.
storage_error prepare(detail::storage_backend &disks,
                      volume where,
                      std::string_view path,
                      bool allow_root) {
    const storage_error checked = check_path(path, allow_root);
    if (checked != storage_error::none) {
        return checked;
    }
    return detail::mount(disks, where);
}

} // namespace

// --- storage -----------------------------------------------------------

storage::storage()
    : backend_(detail::make_storage_backend()) {}

storage::~storage() = default;

bool storage::available(volume where) {
    return detail::mount(*backend_, where) == storage_error::none;
}

std::optional<entry_info> storage::info(volume where, std::string_view path) {
    if (prepare(*backend_, where, path, false) != storage_error::none) {
        return std::nullopt;
    }
    return detail::info(*backend_, where, std::string(path));
}

storage_error storage::make_directory(volume where, std::string_view path) {
    const storage_error ready = prepare(*backend_, where, path, false);
    if (ready != storage_error::none) {
        return ready;
    }
    return detail::make_directory(*backend_, where, std::string(path));
}

storage_error storage::remove(volume where, std::string_view path) {
    const storage_error ready = prepare(*backend_, where, path, false);
    if (ready != storage_error::none) {
        return ready;
    }
    return detail::remove(*backend_, where, std::string(path));
}

storage_error
storage::rename(volume where, std::string_view from, std::string_view to) {
    storage_error ready = prepare(*backend_, where, from, false);
    if (ready == storage_error::none) {
        ready = check_path(to, false);
    }
    if (ready != storage_error::none) {
        return ready;
    }
    const std::string target(to);
    // POSIX rename replaces and FAT rename refuses; make both refuse.
    if (detail::info(*backend_, where, target)) {
        return storage_error::already_exists;
    }
    return detail::rename(*backend_, where, std::string(from), target);
}

std::optional<std::uint64_t> storage::free_space(volume where) {
    if (detail::mount(*backend_, where) != storage_error::none) {
        return std::nullopt;
    }
    return detail::free_space(*backend_, where);
}

// --- file --------------------------------------------------------------

file::file(storage &disks,
           volume where,
           std::string_view path,
           file_mode mode) {
    error_ = prepare(*disks.backend_, where, path, false);
    if (error_ != storage_error::none) {
        return;
    }
    detail::file_opening opened =
        detail::open_file(*disks.backend_, where, std::string(path), mode);
    error_ = opened.error;
    if (error_ != storage_error::none) {
        return;
    }
    backend_ = std::move(opened.handle);
    size_ = opened.size;
    position_ = mode == file_mode::append ? size_ : 0;
}

file::~file() {
    if (backend_) {
        static_cast<void>(detail::close_file(*backend_));
    }
}

file::file(file &&other) noexcept
    : backend_(std::move(other.backend_))
    , error_(other.error_)
    , position_(other.position_)
    , size_(other.size_) {}

file &file::operator=(file &&other) noexcept {
    if (this != &other) {
        if (backend_) {
            static_cast<void>(detail::close_file(*backend_));
        }
        backend_ = std::move(other.backend_);
        error_ = other.error_;
        position_ = other.position_;
        size_ = other.size_;
    }
    return *this;
}

bool file::is_open() const {
    return backend_ != nullptr;
}

storage_error file::error() const {
    return error_;
}

file::operator bool() const {
    return is_open() && error_ == storage_error::none;
}

std::size_t file::read(std::span<std::byte> out) {
    if (!*this || out.empty()) {
        return 0;
    }
    const detail::transfer done = detail::read_file(*backend_, out);
    position_ += done.count;
    error_ = done.error;
    return done.count;
}

std::size_t file::write(std::span<const std::byte> in) {
    if (!*this || in.empty()) {
        return 0;
    }
    const detail::transfer done = detail::write_file(*backend_, in);
    position_ += done.count;
    size_ = std::max(size_, position_);
    error_ = done.error;
    return done.count;
}

std::size_t file::write(std::string_view text) {
    return write(std::as_bytes(std::span(text.data(), text.size())));
}

bool file::seek(std::uint64_t offset) {
    if (!*this || offset > size_) {
        return false;
    }
    error_ = detail::seek_file(*backend_, offset);
    if (error_ != storage_error::none) {
        return false;
    }
    position_ = offset;
    return true;
}

std::uint64_t file::position() const {
    return position_;
}

std::uint64_t file::size() const {
    return size_;
}

bool file::close() {
    if (backend_) {
        const storage_error closed = detail::close_file(*backend_);
        backend_.reset();
        if (error_ == storage_error::none) {
            error_ = closed;
        }
    }
    return error_ == storage_error::none;
}

// --- directory ---------------------------------------------------------

directory::directory(storage &disks, volume where, std::string_view path) {
    error_ = prepare(*disks.backend_, where, path, true);
    if (error_ != storage_error::none) {
        return;
    }
    detail::directory_opening opened =
        detail::open_directory(*disks.backend_, where, std::string(path));
    error_ = opened.error;
    backend_ = std::move(opened.handle);
}

directory::~directory() = default;

directory::directory(directory &&other) noexcept
    : backend_(std::move(other.backend_))
    , error_(other.error_) {}

directory &directory::operator=(directory &&other) noexcept {
    if (this != &other) {
        backend_ = std::move(other.backend_);
        error_ = other.error_;
    }
    return *this;
}

bool directory::is_open() const {
    return backend_ != nullptr;
}

storage_error directory::error() const {
    return error_;
}

directory::operator bool() const {
    return is_open() && error_ == storage_error::none;
}

std::optional<directory_entry> directory::next() {
    if (!*this) {
        return std::nullopt;
    }
    detail::directory_step step = detail::next_entry(*backend_);
    error_ = step.error;
    return std::move(step.entry);
}

} // namespace inkerface
