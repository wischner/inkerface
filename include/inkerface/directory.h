//
// Declares directory: a one-pass listing of the entries in a directory
// on a storage volume.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <inkerface/storage.h>

namespace inkerface {

// One entry of a directory listing.
struct directory_entry {
    // Name without the directory path.
    std::string name;
    bool is_directory = false;
    // Size in bytes; zero for directories.
    std::uint64_t size = 0;
};

// An open directory listing.
//
// Notes:
//     Entries come in filesystem order, not sorted, without "." and "..".
//     Names longer than max_path_length are skipped. Changing the
//     directory while listing it gives unspecified results.
//     Failures are sticky, as with file.
//     Thread-safety: not thread-safe.
class directory {
public:
    //
    // Open a directory for listing.
    //
    // Parameters:
    //     disks - Storage the volume belongs to; must outlive the listing.
    //     where - Volume to list.
    //     path  - Directory path; "" lists the volume root.
    //
    directory(storage &disks, volume where, std::string_view path);
    ~directory();

    directory(const directory &) = delete;
    directory &operator=(const directory &) = delete;
    // A moved-from listing is closed.
    directory(directory &&other) noexcept;
    directory &operator=(directory &&other) noexcept;

    // Return true while the listing is open.
    [[nodiscard]] bool is_open() const;

    // Return the first failure, or storage_error::none.
    [[nodiscard]] storage_error error() const;

    // Return true when the listing is open and nothing has failed.
    explicit operator bool() const;

    // Return the next entry, or std::nullopt at the end or on failure.
    [[nodiscard]] std::optional<directory_entry> next();

private:
    detail::directory_handle backend_;
    storage_error error_ = storage_error::none;
};

} // namespace inkerface
