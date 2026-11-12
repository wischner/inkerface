//
// Declares persistent storage: the volumes a program can use, the errors
// storage operations report, and the volume-level operations (inspect,
// create directories, remove, rename, free space). Files and directories
// are opened through file.h and directory.h.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace inkerface {

namespace detail {
class storage_backend;
class file_backend;
class directory_backend;

// Destroys backend objects; defined by the platform layer, where the
// backend types are complete.
struct backend_deleter {
    void operator()(storage_backend *backend) const noexcept;
    void operator()(file_backend *backend) const noexcept;
    void operator()(directory_backend *backend) const noexcept;
};

using storage_handle = std::unique_ptr<storage_backend, backend_deleter>;
using file_handle = std::unique_ptr<file_backend, backend_deleter>;
using directory_handle = std::unique_ptr<directory_backend, backend_deleter>;
} // namespace detail

// A storage area. Each volume has its own root; paths never cross volumes.
enum class volume : std::uint8_t {
    // Internal flash. Small, always present, survives power loss.
    internal,
    // Removable SD card. Large, may be missing.
    card
};

// Why a storage operation failed. none means success.
enum class storage_error : std::uint8_t {
    none,
    // The volume is missing or cannot be mounted (for example, no card).
    unavailable,
    // The path breaks the path rules (see max_path_length).
    invalid_path,
    not_found,
    already_exists,
    not_a_directory,
    is_a_directory,
    not_empty,
    no_space,
    too_many_open,
    // Any other device or filesystem failure.
    io
};

// Longest accepted path, in bytes.
//
// Notes:
//     Paths are relative to the volume root and use '/' between names.
//     They must not be empty (except "" for the root of a directory
//     listing), start or end with '/', contain empty names, ".", "..",
//     '\', or control characters.
inline constexpr std::size_t max_path_length = 255;

// What storage::info() reports about a file or directory.
struct entry_info {
    bool is_directory = false;
    // Size in bytes; zero for directories.
    std::uint64_t size = 0;
};

// The program's storage.
//
// Notes:
//     Exactly one storage should exist. Volumes are mounted on first use,
//     so constructing storage costs no power or time. Files and
//     directories must be closed before storage is destroyed.
//     Thread-safety: not thread-safe; use from one thread.
class storage {
public:
    storage();
    ~storage();

    storage(const storage &) = delete;
    storage &operator=(const storage &) = delete;
    storage(storage &&) = delete;
    storage &operator=(storage &&) = delete;

    // Mount the volume if needed; return true when it can be used.
    [[nodiscard]] bool available(volume where);

    // Describe a file or directory; std::nullopt when it does not exist,
    // the path is invalid, or the volume is unavailable.
    [[nodiscard]] std::optional<entry_info> info(volume where,
                                                 std::string_view path);

    // Create one directory. The parent must already exist.
    [[nodiscard]] storage_error make_directory(volume where,
                                               std::string_view path);

    // Remove a file or an empty directory.
    [[nodiscard]] storage_error remove(volume where, std::string_view path);

    //
    // Rename or move a file or directory within one volume.
    //
    // Returns:
    //     storage_error::already_exists when to exists; rename never
    //     replaces anything.
    //
    [[nodiscard]] storage_error
    rename(volume where, std::string_view from, std::string_view to);

    // Return free bytes, or std::nullopt when unknown. On the card the
    // value may be up to 20 seconds old, because counting is slow.
    [[nodiscard]] std::optional<std::uint64_t> free_space(volume where);

private:
    friend class file;
    friend class directory;

    detail::storage_handle backend_;
};

} // namespace inkerface
