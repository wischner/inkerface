//
// Implements Xteink X4 storage. The internal volume is LittleFS on the
// "spiffs" data partition (see partitions-x4.csv), mounted at /littlefs
// and reached through POSIX. The card volume is FreeInk's SDCardManager
// (SdFat). Both are mounted on first use only, so a program that never
// touches a volume never powers or initializes it.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "../common/storage_backend.h"
#include "../posix/posix_storage.h"

#include <LittleFS.h>
#include <SDCardManager.h>

#include <cstring>
#include <utility>
#include <variant>

namespace inkerface::detail {

namespace {

constexpr const char *internal_mount_point = "/littlefs";
constexpr const char *internal_partition = "spiffs";

// Files and listings open at once on internal flash. Each costs a
// LittleFS cache buffer, so keep this small.
constexpr std::uint8_t internal_max_open = 4;

std::string internal_path(const std::string &path) {
    std::string full(internal_mount_point);
    if (!path.empty()) {
        full += '/';
        full += path;
    }
    return full;
}

std::string card_path(const std::string &path) {
    return "/" + path;
}

// Explain why a card path could not be opened.
storage_error card_open_failure(const std::string &native) {
    FsFile probe = SdMan.open(native.c_str());
    if (!probe) {
        return storage_error::not_found;
    }
    const bool directory = probe.isDirectory();
    probe.close();
    return directory ? storage_error::is_a_directory : storage_error::io;
}

} // namespace

// Which volumes are mounted.
class storage_backend {
public:
    bool internal_ready = false;
    bool card_ready = false;
};

// An open file: a descriptor on internal flash or an SdFat file.
class file_backend {
public:
    std::variant<int, FsFile> handle;
};

// An open listing on either volume.
class directory_backend {
public:
    std::variant<posix::listing, FsFile> handle;
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
    if (where == volume::internal) {
        if (!disks.internal_ready) {
            // Format only an empty partition; LittleFS survives power loss.
            disks.internal_ready = LittleFS.begin(true,
                                                  internal_mount_point,
                                                  internal_max_open,
                                                  internal_partition);
        }
        return disks.internal_ready ? storage_error::none
                                    : storage_error::unavailable;
    }
    if (!disks.card_ready) {
        disks.card_ready = SdMan.begin(); // retried later: card may be added
    }
    return disks.card_ready ? storage_error::none : storage_error::unavailable;
}

file_opening open_file(storage_backend & /*disks*/,
                       volume where,
                       const std::string &path,
                       file_mode mode) {
    file_opening result;
    if (where == volume::internal) {
        const posix::opened_file opened =
            posix::open_file(internal_path(path), mode);
        result.error = opened.error;
        result.size = opened.size;
        if (opened.error == storage_error::none) {
            result.handle.reset(new file_backend{opened.descriptor});
        }
        return result;
    }
    const std::string native = card_path(path);
    oflag_t flags = O_RDWR | O_CREAT; // append
    if (mode == file_mode::read) {
        flags = O_RDONLY;
    } else if (mode == file_mode::write) {
        flags = O_RDWR | O_CREAT | O_TRUNC;
    }
    FsFile opened = SdMan.open(native.c_str(), flags);
    if (!opened) {
        result.error = card_open_failure(native);
        return result;
    }
    if (opened.isDirectory()) {
        opened.close();
        result.error = storage_error::is_a_directory;
        return result;
    }
    result.size = opened.fileSize();
    if (mode == file_mode::append && !opened.seekSet(result.size)) {
        opened.close();
        result.error = storage_error::io;
        return result;
    }
    result.handle.reset(new file_backend{std::move(opened)});
    return result;
}

transfer read_file(file_backend &open, std::span<std::byte> out) {
    if (int *descriptor = std::get_if<int>(&open.handle)) {
        return posix::read_file(*descriptor, out);
    }
    FsFile &card = std::get<FsFile>(open.handle);
    transfer done;
    while (done.count < out.size()) {
        const std::span<std::byte> rest = out.subspan(done.count);
        const int got = card.read(rest.data(), rest.size());
        if (got < 0) {
            done.error = storage_error::io;
            break;
        }
        if (got == 0) {
            break; // end of file
        }
        done.count += static_cast<std::size_t>(got);
    }
    return done;
}

transfer write_file(file_backend &open, std::span<const std::byte> in) {
    if (int *descriptor = std::get_if<int>(&open.handle)) {
        return posix::write_file(*descriptor, in);
    }
    FsFile &card = std::get<FsFile>(open.handle);
    transfer done;
    done.count = card.write(in.data(), in.size());
    if (done.count != in.size()) {
        done.error = storage_error::io; // a full card or a card error
    }
    return done;
}

storage_error seek_file(file_backend &open, std::uint64_t offset) {
    if (int *descriptor = std::get_if<int>(&open.handle)) {
        return posix::seek_file(*descriptor, offset);
    }
    return std::get<FsFile>(open.handle).seekSet(offset) ? storage_error::none
                                                          : storage_error::io;
}

storage_error close_file(file_backend &open) {
    if (int *descriptor = std::get_if<int>(&open.handle)) {
        return posix::close_file(*descriptor);
    }
    FsFile &card = std::get<FsFile>(open.handle);
    if (!card) {
        return storage_error::none;
    }
    return card.close() ? storage_error::none : storage_error::io;
}

directory_opening open_directory(storage_backend & /*disks*/,
                                 volume where,
                                 const std::string &path) {
    directory_opening result;
    if (where == volume::internal) {
        posix::opened_listing opened =
            posix::open_directory(internal_path(path));
        result.error = opened.error;
        if (opened.error == storage_error::none) {
            result.handle.reset(new directory_backend{std::move(opened.open)});
        }
        return result;
    }
    FsFile opened = SdMan.open(card_path(path).c_str());
    if (!opened) {
        result.error = storage_error::not_found;
        return result;
    }
    if (!opened.isDirectory()) {
        opened.close();
        result.error = storage_error::not_a_directory;
        return result;
    }
    result.handle.reset(new directory_backend{std::move(opened)});
    return result;
}

directory_step next_entry(directory_backend &listing) {
    if (auto *open = std::get_if<posix::listing>(&listing.handle)) {
        return posix::next_entry(*open);
    }
    FsFile &folder = std::get<FsFile>(listing.handle);
    directory_step step;
    while (true) {
        FsFile item = folder.openNextFile();
        if (!item) {
            return step; // end of listing
        }
        char name[max_path_length + 1] = {};
        item.getName(name, sizeof(name));
        const std::size_t length = ::strnlen(name, sizeof(name));
        const bool directory = item.isDirectory();
        const std::uint64_t size = directory ? 0 : item.fileSize();
        item.close();
        if (length == 0 || length >= max_path_length) {
            continue; // unnamed, or possibly truncated
        }
        step.entry =
            directory_entry{std::string(name, length), directory, size};
        return step;
    }
}

std::optional<entry_info>
info(storage_backend & /*disks*/, volume where, const std::string &path) {
    if (where == volume::internal) {
        return posix::info(internal_path(path));
    }
    FsFile item = SdMan.open(card_path(path).c_str());
    if (!item) {
        return std::nullopt;
    }
    entry_info found;
    found.is_directory = item.isDirectory();
    found.size = found.is_directory ? 0 : item.fileSize();
    item.close();
    return found;
}

storage_error
make_directory(storage_backend &disks, volume where, const std::string &path) {
    if (where == volume::internal) {
        return posix::make_directory(internal_path(path));
    }
    if (info(disks, where, path)) {
        return storage_error::already_exists;
    }
    const std::size_t slash = path.rfind('/');
    if (slash != std::string::npos &&
        !info(disks, where, path.substr(0, slash))) {
        return storage_error::not_found;
    }
    return SdMan.mkdir(card_path(path).c_str(), false) ? storage_error::none
                                                       : storage_error::io;
}

storage_error
remove(storage_backend &disks, volume where, const std::string &path) {
    if (where == volume::internal) {
        return posix::remove(internal_path(path));
    }
    const std::optional<entry_info> found = info(disks, where, path);
    if (!found) {
        return storage_error::not_found;
    }
    const std::string native = card_path(path);
    if (!found->is_directory) {
        return SdMan.remove(native.c_str()) ? storage_error::none
                                            : storage_error::io;
    }
    FsFile folder = SdMan.open(native.c_str());
    FsFile first = folder.openNextFile();
    const bool empty = !first;
    first.close();
    folder.close();
    if (!empty) {
        return storage_error::not_empty;
    }
    return SdMan.rmdir(native.c_str()) ? storage_error::none
                                       : storage_error::io;
}

storage_error rename(storage_backend &disks,
                     volume where,
                     const std::string &from,
                     const std::string &to) {
    if (where == volume::internal) {
        return posix::rename(internal_path(from), internal_path(to));
    }
    if (!info(disks, where, from)) {
        return storage_error::not_found;
    }
    return SdMan.rename(card_path(from).c_str(), card_path(to).c_str())
               ? storage_error::none
               : storage_error::io;
}

std::optional<std::uint64_t> free_space(storage_backend & /*disks*/,
                                        volume where) {
    if (where == volume::internal) {
        const std::size_t total = LittleFS.totalBytes();
        const std::size_t used = LittleFS.usedBytes();
        return used <= total ? std::optional<std::uint64_t>(total - used)
                             : std::nullopt;
    }
    // FreeInk caches the slow FAT scan for 20 seconds.
    std::uint64_t used = 0;
    const std::uint64_t total = SdMan.sdTotalBytes();
    if (!SdMan.sdUsedBytes(used) || used > total) {
        return std::nullopt;
    }
    return total - used;
}

} // namespace inkerface::detail
