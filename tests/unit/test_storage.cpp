//
// Tests storage, file and directory through the host backend, in a fresh
// temporary storage root. The checks cover the shared logic the device
// also runs: path rules, sticky errors, position and size tracking, and
// rename never replacing a file.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include <inkerface/directory.h>
#include <inkerface/file.h>
#include <inkerface/storage.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

namespace tests {

namespace {

using inkerface::directory;
using inkerface::file;
using inkerface::file_mode;
using inkerface::storage;
using inkerface::storage_error;
using inkerface::volume;

// Read a whole small file as text; empty on failure.
std::string read_text(storage &disks, volume where, std::string_view path) {
    file in(disks, where, path, file_mode::read);
    std::array<char, 256> buffer{};
    const std::size_t count =
        in.read(std::as_writable_bytes(std::span(buffer)));
    return std::string(buffer.data(), count);
}

// Write text to a file and close it; true on success.
bool write_text(storage &disks,
                volume where,
                std::string_view path,
                std::string_view text,
                file_mode mode = file_mode::write) {
    file out(disks, where, path, mode);
    return out.write(text) == text.size() && out.close();
}

// List a directory as sorted "name/" (directories) and "name:size" lines.
std::vector<std::string>
list(storage &disks, volume where, std::string_view path) {
    std::vector<std::string> names;
    directory listing(disks, where, path);
    while (const auto entry = listing.next()) {
        names.push_back(entry->is_directory
                            ? entry->name + "/"
                            : entry->name + ":" + std::to_string(entry->size));
    }
    std::sort(names.begin(), names.end());
    return names;
}

void without_root(test_context &context) {
    ::unsetenv("INKERFACE_STORAGE_ROOT");
    storage disks;
    context.check(!disks.available(volume::internal),
                  "no storage root means no volumes");
    file note(disks, volume::internal, "note.txt", file_mode::write);
    context.check(!note && note.error() == storage_error::unavailable,
                  "opening without a volume reports unavailable");
}

void path_rules(test_context &context, storage &disks) {
    const std::array<std::string_view, 10> bad{
        "", "/root", "end/", "a//b", "..", "a/../b", ".", "a\\b", "tab\there",
        "a/./b"};
    for (const std::string_view path : bad) {
        file attempt(disks, volume::internal, path, file_mode::write);
        context.check(attempt.error() == storage_error::invalid_path,
                      "invalid path is rejected: " + std::string(path));
    }
    const std::string too_long(inkerface::max_path_length + 1, 'x');
    file attempt(disks, volume::internal, too_long, file_mode::write);
    context.check(attempt.error() == storage_error::invalid_path,
                  "overlong path is rejected");
    context.check(!disks.info(volume::internal, "../escape"),
                  "info rejects escaping paths");
}

void reading_and_writing(test_context &context, storage &disks) {
    context.check(write_text(disks, volume::internal, "note.txt", "hello"),
                  "write and close succeed");
    context.check(read_text(disks, volume::internal, "note.txt") == "hello",
                  "data reads back");

    context.check(write_text(disks,
                             volume::internal,
                             "note.txt",
                             " world",
                             file_mode::append),
                  "append succeeds");
    context.check(read_text(disks, volume::internal, "note.txt") ==
                      "hello world",
                  "append adds after existing data");

    file in(disks, volume::internal, "note.txt", file_mode::read);
    context.check(static_cast<bool>(in), "existing file opens");
    context.check(in.size() == 11 && in.position() == 0,
                  "size and start position are known at open");
    context.check(in.seek(6), "seek inside the file");
    std::array<char, 16> buffer{};
    const std::size_t count =
        in.read(std::as_writable_bytes(std::span(buffer)));
    context.check(std::string_view(buffer.data(), count) == "world",
                  "read continues at the seek offset");
    context.check(in.position() == 11, "position follows reads");
    context.check(in.read(std::as_writable_bytes(std::span(buffer))) == 0 &&
                      in.error() == storage_error::none,
                  "reading at the end returns 0 without an error");
    context.check(!in.seek(12) && in.error() == storage_error::none,
                  "seeking past the end is refused, not an error");

    context.check(in.write("x") == 0 && in.error() != storage_error::none,
                  "writing a read-only file fails");
    context.check(in.read(std::as_writable_bytes(std::span(buffer))) == 0,
                  "errors are sticky");

    context.check(write_text(disks, volume::internal, "note.txt", "new"),
                  "write mode replaces the contents");
    context.check(read_text(disks, volume::internal, "note.txt") == "new",
                  "old contents are gone");

    file missing(disks, volume::internal, "missing.txt", file_mode::read);
    context.check(!missing && missing.error() == storage_error::not_found,
                  "missing file reports not_found");

    file moved(disks, volume::internal, "note.txt", file_mode::read);
    file target = std::move(moved);
    context.check(!moved.is_open() && target.is_open() && target.size() == 3,
                  "moving a file transfers it");
    context.check(target.close() && !target.is_open() && target.close(),
                  "close succeeds and can be repeated");
}

void directories(test_context &context, storage &disks) {
    context.check(disks.make_directory(volume::internal, "books") ==
                      storage_error::none,
                  "make a directory");
    context.check(disks.make_directory(volume::internal, "books") ==
                      storage_error::already_exists,
                  "making it again reports already_exists");
    context.check(disks.make_directory(volume::internal, "x/y") ==
                      storage_error::not_found,
                  "a missing parent reports not_found");
    context.check(write_text(disks, volume::internal, "books/a.txt", "abc"),
                  "write into a subdirectory");

    file folder(disks, volume::internal, "books", file_mode::read);
    context.check(folder.error() == storage_error::is_a_directory,
                  "a directory cannot be opened as a file");

    context.check(list(disks, volume::internal, "") ==
                      std::vector<std::string>{"books/", "note.txt:3"},
                  "root listing shows files with sizes and directories");
    context.check(list(disks, volume::internal, "books") ==
                      std::vector<std::string>{"a.txt:3"},
                  "subdirectory listing");

    directory not_folder(disks, volume::internal, "note.txt");
    context.check(not_folder.error() == storage_error::not_a_directory,
                  "listing a file reports not_a_directory");
    directory missing(disks, volume::internal, "nowhere");
    context.check(!missing && missing.error() == storage_error::not_found,
                  "listing a missing directory reports not_found");

    const auto found = disks.info(volume::internal, "books/a.txt");
    context.check(found && !found->is_directory && found->size == 3,
                  "info describes a file");
    context.check(!disks.info(volume::internal, "books/zzz"),
                  "info of a missing path is empty");
}

void removing_and_renaming(test_context &context, storage &disks) {
    context.check(disks.remove(volume::internal, "books") ==
                      storage_error::not_empty,
                  "a directory with files is not removed");
    context.check(disks.remove(volume::internal, "books/a.txt") ==
                      storage_error::none,
                  "remove a file");
    context.check(disks.remove(volume::internal, "books") ==
                      storage_error::none,
                  "remove an empty directory");
    context.check(disks.remove(volume::internal, "books") ==
                      storage_error::not_found,
                  "removing again reports not_found");

    context.check(disks.rename(volume::internal, "note.txt", "kept.txt") ==
                      storage_error::none,
                  "rename a file");
    context.check(write_text(disks, volume::internal, "other.txt", "o"),
                  "create a second file");
    context.check(disks.rename(volume::internal, "other.txt", "kept.txt") ==
                      storage_error::already_exists,
                  "rename never replaces a file");
    context.check(read_text(disks, volume::internal, "kept.txt") == "new",
                  "the existing file is untouched");
    context.check(disks.rename(volume::internal, "gone.txt", "x.txt") ==
                      storage_error::not_found,
                  "renaming a missing file reports not_found");
}

void volumes_are_separate(test_context &context, storage &disks) {
    context.check(disks.available(volume::card), "the host card is available");
    context.check(list(disks, volume::card, "").empty(),
                  "the card does not see internal files");
    context.check(disks.free_space(volume::internal).has_value(),
                  "free space is reported");
}

} // namespace

void run_storage_tests(test_context &context) {
    without_root(context);

    std::string pattern =
        (std::filesystem::temp_directory_path() / "inkerface-test-XXXXXX")
            .string();
    if (::mkdtemp(pattern.data()) == nullptr) {
        context.check(false, "create a temporary storage root");
        return;
    }
    ::setenv("INKERFACE_STORAGE_ROOT", pattern.c_str(), 1);
    {
        storage disks;
        path_rules(context, disks);
        reading_and_writing(context, disks);
        directories(context, disks);
        removing_and_renaming(context, disks);
        volumes_are_separate(context, disks);
    }
    ::unsetenv("INKERFACE_STORAGE_ROOT");
    std::error_code ignored;
    std::filesystem::remove_all(pattern, ignored);
}

} // namespace tests
