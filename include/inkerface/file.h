//
// Declares file: an open file on a storage volume. Reads and writes are
// unbuffered and go straight to the filesystem, so use buffers of a few
// hundred bytes or more; the size and position are tracked in memory and
// cost no I/O to query.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include <inkerface/storage.h>

namespace inkerface {

// How a file is opened.
enum class file_mode : std::uint8_t {
    // Read an existing file from its start.
    read,
    // Create the file, or empty an existing one, and write from the start.
    write,
    // Create the file if needed and write after its existing contents.
    append
};

// An open file.
//
// Notes:
//     Failures are sticky, as with std::fstream: the first failure is
//     kept in error(), and later reads and writes do nothing. Check the
//     file (or is_open()) after construction.
//     The destructor closes the file but cannot report errors; call
//     close() after writing to know that the data was stored.
//     Thread-safety: not thread-safe.
class file {
public:
    //
    // Open a file.
    //
    // Parameters:
    //     disks - Storage the volume belongs to; must outlive the file.
    //     where - Volume to open the file on.
    //     path  - Path relative to the volume root.
    //     mode  - How to open the file.
    //
    // Notes:
    //     On failure the file is closed and error() says why.
    //
    file(storage &disks, volume where, std::string_view path, file_mode mode);
    ~file();

    file(const file &) = delete;
    file &operator=(const file &) = delete;
    // A moved-from file is closed.
    file(file &&other) noexcept;
    file &operator=(file &&other) noexcept;

    // Return true while the file is open.
    [[nodiscard]] bool is_open() const;

    // Return the first failure, or storage_error::none.
    [[nodiscard]] storage_error error() const;

    // Return true when the file is open and nothing has failed.
    explicit operator bool() const;

    //
    // Read into a buffer.
    //
    // Returns:
    //     Bytes read. Less than out.size() means the end of the file was
    //     reached, or a failure occurred (see error()).
    //
    [[nodiscard]] std::size_t read(std::span<std::byte> out);

    //
    // Write bytes at the current position.
    //
    // Returns:
    //     Bytes written. Less than in.size() means a failure (see error()).
    //
    [[nodiscard]] std::size_t write(std::span<const std::byte> in);

    // Write text; same as write() of its bytes.
    [[nodiscard]] std::size_t write(std::string_view text);

    // Move to an absolute offset in [0, size()]; false if the offset is
    // out of range (error() is unchanged) or the move failed.
    [[nodiscard]] bool seek(std::uint64_t offset);

    // Return the current offset from the start of the file.
    [[nodiscard]] std::uint64_t position() const;

    // Return the file size in bytes, including data written so far.
    [[nodiscard]] std::uint64_t size() const;

    // Close the file and commit written data. Returns false if anything
    // failed while the file was open. Closing twice is harmless.
    [[nodiscard]] bool close();

private:
    detail::file_handle backend_;
    storage_error error_ = storage_error::none;
    std::uint64_t position_ = 0;
    std::uint64_t size_ = 0;
};

} // namespace inkerface
