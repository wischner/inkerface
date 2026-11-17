// Implements disk images with bounded pread/pwrite and an exclusive flock.
// Flash erase writes 0xff; FAT sectors use ordinary overwrite semantics.
// MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
#include "disk_image.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace inkerface::detail {

disk_image::~disk_image() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
    }
}

bool disk_image::open(const std::filesystem::path &path,
                      std::uint64_t capacity,
                      bool flash) {
    if (descriptor_ >= 0 || capacity == 0 ||
        capacity >
            static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return false;
    }
    constexpr int flags = O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open boundary.
    descriptor_ = ::open(path.c_str(), flags);
    if (descriptor_ < 0 && errno == ENOENT) {
        // POSIX open requires a variadic creation mode.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        descriptor_ = ::open(path.c_str(), flags | O_CREAT | O_EXCL, 0600);
        created_ = descriptor_ >= 0;
    }
    if (descriptor_ < 0) {
        return false;
    }
    struct stat status{};
    if (::fstat(descriptor_, &status) != 0 || !S_ISREG(status.st_mode) ||
        ::flock(descriptor_, LOCK_EX | LOCK_NB) != 0 ||
        (!created_ &&
         (status.st_size < 0 ||
          static_cast<std::uint64_t>(status.st_size) != capacity))) {
        return false;
    }
    capacity_ = capacity;
    if (created_) {
        if (::ftruncate(descriptor_, static_cast<off_t>(capacity)) != 0 ||
            (flash && !erase(0, static_cast<std::size_t>(capacity))) ||
            !sync()) {
            return false;
        }
    }
    return true;
}

bool disk_image::read(std::uint64_t offset, std::span<std::byte> data) {
    if (offset > capacity_ || data.size() > capacity_ - offset) {
        return false;
    }
    std::size_t done = 0;
    while (done < data.size()) {
        const auto rest = data.subspan(done);
        const ssize_t count = ::pread(descriptor_,
                                      rest.data(),
                                      rest.size(),
                                      static_cast<off_t>(offset + done));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(count);
    }
    return true;
}

bool disk_image::write(std::uint64_t offset, std::span<const std::byte> data) {
    if (offset > capacity_ || data.size() > capacity_ - offset) {
        return false;
    }
    std::size_t done = 0;
    while (done < data.size()) {
        const auto rest = data.subspan(done);
        const ssize_t count = ::pwrite(descriptor_,
                                       rest.data(),
                                       rest.size(),
                                       static_cast<off_t>(offset + done));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(count);
    }
    return true;
}

bool disk_image::erase(std::uint64_t offset, std::size_t length) {
    std::array<std::byte, 4096> erased{};
    erased.fill(std::byte{0xff});
    while (length > 0) {
        const auto count = std::min(length, erased.size());
        if (!write(offset, std::span(erased).first(count))) {
            return false;
        }
        length -= count;
        offset += count;
    }
    return true;
}

bool disk_image::sync() {
    return ::fsync(descriptor_) == 0;
}

} // namespace inkerface::detail
