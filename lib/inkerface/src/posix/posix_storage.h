//
// Declares storage primitives over POSIX file descriptors and native
// paths. Used by the host for both volumes and by the X4 for internal
// flash (LittleFS behind ESP-IDF's VFS). Only calls ESP-IDF's VFS
// supports are used: no statvfs, dirfd, *at() calls or stdio buffers.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include "../common/storage_backend.h"

#include <dirent.h>

#include <memory>
#include <optional>
#include <span>
#include <string>

namespace inkerface::detail::posix {

// Map an errno value to a storage error.
[[nodiscard]] storage_error from_errno(int code);

// An open descriptor and its size at open time.
struct opened_file {
    int descriptor = -1;
    storage_error error = storage_error::none;
    std::uint64_t size = 0;
};

[[nodiscard]] opened_file open_file(const std::string &native, file_mode mode);
[[nodiscard]] transfer read_file(int descriptor, std::span<std::byte> out);
[[nodiscard]] transfer write_file(int descriptor,
                                  std::span<const std::byte> in);
[[nodiscard]] storage_error seek_file(int descriptor, std::uint64_t offset);
// Close the descriptor and set it to -1; harmless when already -1.
[[nodiscard]] storage_error close_file(int &descriptor);

// Closes a DIR stream.
struct dir_closer {
    void operator()(DIR *stream) const noexcept;
};

// An open listing: the stream and the native path it lists.
struct listing {
    std::unique_ptr<DIR, dir_closer> stream;
    std::string native;
};

struct opened_listing {
    listing open;
    storage_error error = storage_error::none;
};

[[nodiscard]] opened_listing open_directory(const std::string &native);
[[nodiscard]] directory_step next_entry(listing &open);

[[nodiscard]] std::optional<entry_info> info(const std::string &native);
[[nodiscard]] storage_error make_directory(const std::string &native);
[[nodiscard]] storage_error remove(const std::string &native);
[[nodiscard]] storage_error rename(const std::string &from,
                                   const std::string &to);

} // namespace inkerface::detail::posix
