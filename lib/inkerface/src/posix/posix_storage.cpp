//
// Implements storage primitives over POSIX descriptors. Reads and writes
// loop over short transfers and EINTR; nothing is buffered here.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "posix_storage.h"

#include <cerrno>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace inkerface::detail::posix {

namespace {

// Do not follow a symbolic link as the last path name where supported.
#ifdef O_NOFOLLOW
constexpr int no_follow = O_NOFOLLOW;
#else
constexpr int no_follow = 0;
#endif

// Permissions for new files and directories; the umask still applies.
constexpr mode_t file_permissions = 0644;
constexpr mode_t directory_permissions = 0755;

int flags_for(file_mode mode) {
    switch (mode) {
    case file_mode::read:
        return O_RDONLY | no_follow;
    case file_mode::write:
        return O_WRONLY | O_CREAT | O_TRUNC | no_follow;
    case file_mode::append:
        // No O_APPEND: seek() must still work before the next write.
        return O_WRONLY | O_CREAT | no_follow;
    }
    return O_RDONLY | no_follow;
}

bool fits_offset(std::uint64_t offset) {
    constexpr auto largest = std::numeric_limits<off_t>::max();
    return offset <= static_cast<std::uint64_t>(largest);
}

} // namespace

storage_error from_errno(int code) {
    switch (code) {
    case ENOENT:
        return storage_error::not_found;
    case EEXIST:
        return storage_error::already_exists;
    case ENOTDIR:
        return storage_error::not_a_directory;
    case EISDIR:
        return storage_error::is_a_directory;
    case ENOTEMPTY:
        return storage_error::not_empty;
    case ENOSPC:
        return storage_error::no_space;
    case EMFILE:
    case ENFILE:
        return storage_error::too_many_open;
    case ENAMETOOLONG:
    case EINVAL:
#ifdef ELOOP
    case ELOOP:
#endif
        return storage_error::invalid_path;
    default:
        return storage_error::io;
    }
}

opened_file open_file(const std::string &native, file_mode mode) {
    opened_file result;
    const int descriptor =
        ::open(native.c_str(), flags_for(mode), file_permissions);
    if (descriptor < 0) {
        result.error = from_errno(errno);
        return result;
    }
    struct stat status {};
    if (::fstat(descriptor, &status) != 0 || S_ISDIR(status.st_mode)) {
        const bool directory = S_ISDIR(status.st_mode);
        ::close(descriptor);
        result.error =
            directory ? storage_error::is_a_directory : storage_error::io;
        return result;
    }
    result.size = static_cast<std::uint64_t>(status.st_size);
    if (mode == file_mode::append && result.size > 0 &&
        ::lseek(descriptor, 0, SEEK_END) < 0) {
        ::close(descriptor);
        result.error = from_errno(errno);
        return result;
    }
    result.descriptor = descriptor;
    return result;
}

transfer read_file(int descriptor, std::span<std::byte> out) {
    transfer done;
    while (done.count < out.size()) {
        const std::span<std::byte> rest = out.subspan(done.count);
        const ssize_t got = ::read(descriptor, rest.data(), rest.size());
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            done.error = from_errno(errno);
            break;
        }
        if (got == 0) {
            break; // end of file
        }
        done.count += static_cast<std::size_t>(got);
    }
    return done;
}

transfer write_file(int descriptor, std::span<const std::byte> in) {
    transfer done;
    while (done.count < in.size()) {
        const std::span<const std::byte> rest = in.subspan(done.count);
        const ssize_t put = ::write(descriptor, rest.data(), rest.size());
        if (put < 0) {
            if (errno == EINTR) {
                continue;
            }
            done.error = from_errno(errno);
            break;
        }
        if (put == 0) {
            done.error = storage_error::io;
            break;
        }
        done.count += static_cast<std::size_t>(put);
    }
    return done;
}

storage_error seek_file(int descriptor, std::uint64_t offset) {
    if (!fits_offset(offset)) {
        return storage_error::io;
    }
    if (::lseek(descriptor, static_cast<off_t>(offset), SEEK_SET) < 0) {
        return from_errno(errno);
    }
    return storage_error::none;
}

storage_error close_file(int &descriptor) {
    if (descriptor < 0) {
        return storage_error::none;
    }
    const int closing = descriptor;
    descriptor = -1;
    // close() commits LittleFS data; no fsync, to spare flash and power.
    return ::close(closing) == 0 ? storage_error::none : from_errno(errno);
}

void dir_closer::operator()(DIR *stream) const noexcept {
    if (stream != nullptr) {
        ::closedir(stream);
    }
}

opened_listing open_directory(const std::string &native) {
    opened_listing result;
    DIR *stream = ::opendir(native.c_str());
    if (stream == nullptr) {
        result.error = from_errno(errno);
        return result;
    }
    result.open.stream.reset(stream);
    result.open.native = native;
    return result;
}

directory_step next_entry(listing &open) {
    directory_step step;
    while (true) {
        errno = 0;
        const dirent *raw = ::readdir(open.stream.get());
        if (raw == nullptr) {
            if (errno != 0) {
                step.error = from_errno(errno);
            }
            return step;
        }
        const std::string_view name(raw->d_name);
        if (name == "." || name == ".." || name.size() > max_path_length) {
            continue;
        }
        // d_type is unreliable on some filesystems; stat the entry.
        const std::optional<entry_info> found =
            info(open.native + "/" + std::string(name));
        if (!found) {
            continue; // removed while listing
        }
        step.entry = directory_entry{
            std::string(name), found->is_directory, found->size};
        return step;
    }
}

std::optional<entry_info> info(const std::string &native) {
    struct stat status {};
    if (::stat(native.c_str(), &status) != 0) {
        return std::nullopt;
    }
    entry_info found;
    found.is_directory = S_ISDIR(status.st_mode);
    found.size =
        found.is_directory ? 0 : static_cast<std::uint64_t>(status.st_size);
    return found;
}

storage_error make_directory(const std::string &native) {
    if (::mkdir(native.c_str(), directory_permissions) != 0) {
        return from_errno(errno);
    }
    return storage_error::none;
}

storage_error remove(const std::string &native) {
    const std::optional<entry_info> found = info(native);
    if (!found) {
        return storage_error::not_found;
    }
    if (!found->is_directory) {
        return ::unlink(native.c_str()) == 0 ? storage_error::none
                                             : from_errno(errno);
    }
    if (::rmdir(native.c_str()) == 0) {
        return storage_error::none;
    }
    // POSIX allows EEXIST for a directory that is not empty.
    return errno == EEXIST ? storage_error::not_empty : from_errno(errno);
}

storage_error rename(const std::string &from, const std::string &to) {
    if (::rename(from.c_str(), to.c_str()) != 0) {
        return from_errno(errno);
    }
    return storage_error::none;
}

} // namespace inkerface::detail::posix
