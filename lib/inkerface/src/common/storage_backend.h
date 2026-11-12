//
// Declares the primitive storage operations each platform implements
// (host/storage.cpp, x4/storage.cpp). The public storage, file and
// directory classes are implemented once on top of them in
// common/storage.cpp, so path checks, error stickiness and position
// tracking behave identically on every target. Paths passed in here are
// already validated and relative to the volume root.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <inkerface/directory.h>
#include <inkerface/file.h>
#include <inkerface/storage.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace inkerface::detail {

// Result of opening a file.
struct file_opening {
    file_handle handle;
    storage_error error = storage_error::none;
    // Size at open; position starts at 0, or at size for append.
    std::uint64_t size = 0;
};

// Result of one read or write.
struct transfer {
    std::size_t count = 0;
    storage_error error = storage_error::none;
};

// Result of opening a directory listing.
struct directory_opening {
    directory_handle handle;
    storage_error error = storage_error::none;
};

// Result of one listing step; no entry and no error means the end.
struct directory_step {
    std::optional<directory_entry> entry;
    storage_error error = storage_error::none;
};

// Create the platform's storage state; mounts nothing yet.
[[nodiscard]] storage_handle make_storage_backend();

// Mount a volume if needed; storage_error::none when it is ready.
[[nodiscard]] storage_error mount(storage_backend &disks, volume where);

[[nodiscard]] file_opening open_file(storage_backend &disks,
                                     volume where,
                                     const std::string &path,
                                     file_mode mode);

// Read until out is full, the end of the file, or a failure.
[[nodiscard]] transfer read_file(file_backend &open, std::span<std::byte> out);

// Write all of in, or stop at the first failure.
[[nodiscard]] transfer write_file(file_backend &open,
                                  std::span<const std::byte> in);

// Move to an offset already checked against the file size.
[[nodiscard]] storage_error seek_file(file_backend &open,
                                      std::uint64_t offset);

// Close and commit; harmless when already closed.
[[nodiscard]] storage_error close_file(file_backend &open);

[[nodiscard]] directory_opening open_directory(storage_backend &disks,
                                               volume where,
                                               const std::string &path);

[[nodiscard]] directory_step next_entry(directory_backend &listing);

[[nodiscard]] std::optional<entry_info>
info(storage_backend &disks, volume where, const std::string &path);

[[nodiscard]] storage_error
make_directory(storage_backend &disks, volume where, const std::string &path);

// Remove a file or an empty directory.
[[nodiscard]] storage_error
remove(storage_backend &disks, volume where, const std::string &path);

// Rename; the caller has checked that to does not exist.
[[nodiscard]] storage_error rename(storage_backend &disks,
                                   volume where,
                                   const std::string &from,
                                   const std::string &to);

[[nodiscard]] std::optional<std::uint64_t> free_space(storage_backend &disks,
                                                      volume where);

} // namespace inkerface::detail
