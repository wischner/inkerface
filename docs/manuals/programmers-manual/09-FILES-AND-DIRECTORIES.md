# 9. Files and directories

An e-reader has to remember things: the last page, the settings, and the
books themselves. In this chapter you learn the three storage classes,
then use a cookbook of complete recipes: detecting the card and free
space, reading and writing text and binary files, copying, saving safely,
and creating, listing, walking and deleting folders. At the end you write
a program that counts its own starts and keeps a log on the SD card.

## The three classes

| Header | Class | Job |
| --- | --- | --- |
| `<inkerface/storage.h>` | `storage` | owns the volumes; creates directories, removes, renames, reports free space |
| `<inkerface/file.h>` | `file` | one open file: read, write, seek |
| `<inkerface/directory.h>` | `directory` | one pass over the entries of a directory |

A program creates **one** `storage`, usually next to its `display`.
Files and directories are opened *on* it.

## Volumes

Storage is split into two **volumes**, and each has its own root:

| Volume | On the X4 | Use it for |
| --- | --- | --- |
| `volume::internal` | about 10 MB of internal flash, always present | settings, progress, small caches |
| `volume::card` | the SD card, which may be missing | books, downloads, logs |

A volume is only switched on (*mounted*) the first time you use it, so
creating `storage` costs nothing. `storage::available(volume)` mounts the
volume and tells you whether it can be used. This is the way to check
whether a card is inserted.

## Paths

Paths are **relative to the volume root** and use `/`:

```text
"settings.txt"          ok
"books/alice.epub"      ok
"/books/alice.epub"     invalid: starts with '/'
"books/"                invalid: ends with '/'
"../secret"             invalid: ".." is never allowed
```

The rules are strict on purpose. Paths may contain no empty names, no
`.` or `..`, no `\` and no control characters, and they may be at most
`max_path_length` (255) bytes long. Every path is checked before it
reaches a filesystem, so a path built from untrusted data (a book's file
name, a downloaded name) can never escape its volume. A rejected path
gives `storage_error::invalid_path`.

## Errors

Storage can fail for ordinary reasons: there is no card, the card is
full, or a file is missing. Failures are reported as values of
`storage_error`, and `storage_error::none` means success.

- `storage` operations (`make_directory`, `remove`, `rename`) return a
  `storage_error`. `info` and `free_space` return an `std::optional`
  that is empty when there is no answer.
- `file` and `directory` work like `std::fstream`. After construction,
  test the object with `if (!in)`. When something fails, `error()` says
  why. The error is **sticky**: after the first failure, further reads
  and writes do nothing, so a sequence of operations can be checked once
  at the end.

## The recipes

The rest of this chapter is a cookbook: each recipe is a complete
function you can copy into your program. Together they show every
storage operation. All of them assume these includes and `using`
declarations:

```cpp
#include <inkerface/directory.h>
#include <inkerface/file.h>
#include <inkerface/storage.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using inkerface::directory;
using inkerface::directory_entry;
using inkerface::file;
using inkerface::file_mode;
using inkerface::storage;
using inkerface::storage_error;
using inkerface::volume;
```

### Turning an error into a message

```cpp
// Describe a storage error in words a reader understands.
std::string_view describe(storage_error error) {
    switch (error) {
    case storage_error::none:
        return "OK";
    case storage_error::unavailable:
        return "Storage not available";
    case storage_error::invalid_path:
        return "Invalid file name";
    case storage_error::not_found:
        return "Not found";
    case storage_error::already_exists:
        return "Already exists";
    case storage_error::not_a_directory:
        return "Not a folder";
    case storage_error::is_a_directory:
        return "Is a folder";
    case storage_error::not_empty:
        return "Folder is not empty";
    case storage_error::no_space:
        return "Storage is full";
    case storage_error::too_many_open:
        return "Too many open files";
    case storage_error::io:
        return "Read or write error";
    }
    return "Unknown error";
}
```

## Detecting storage

### Is there a card?

`available()` mounts a volume if it isn't mounted yet, and reports
whether it can be used. For the card, that answers "is a card inserted
and readable?":

```cpp
// Return true when an SD card is inserted and readable.
bool card_inserted(storage &disks) {
    return disks.available(volume::card);
}
```

If no card was found, the next `available()` tries again, so a card
inserted later is picked up. Every attempt initializes the card
interface, though, so check when the user does something that needs the
card (opening the library, starting a download), not on every pass of the
main loop.

Once the card is mounted, inkerface doesn't notice it being removed.
Operations then fail with `storage_error::io` until the program
restarts.

### How much space is left?

```cpp
// Format a byte count as "512 bytes", "45.6 KB" or "7.8 MB".
std::string format_size(std::uint64_t bytes) {
    constexpr std::array<std::string_view, 3> units{"KB", "MB", "GB"};
    if (bytes < 1024) {
        return std::to_string(bytes) + " bytes";
    }
    std::uint64_t scale = 1024;
    std::size_t unit = 0;
    while (unit + 1 < units.size() && bytes >= scale * 1024) {
        scale *= 1024;
        ++unit;
    }
    const std::uint64_t tenths = bytes * 10 / scale;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) +
           " " + std::string(units.at(unit));
}

// Describe one volume for a status screen, e.g. "Card: 7.8 GB free".
std::string describe_volume(storage &disks, volume where) {
    const std::string name = where == volume::internal ? "Internal" : "Card";
    if (!disks.available(where)) {
        return name + ": not available";
    }
    const std::optional<std::uint64_t> free = disks.free_space(where);
    return name + ": " +
           (free ? format_size(*free) + " free" : "free space unknown");
}
```

On the card, `free_space()` can be up to 20 seconds old, because counting
free space means scanning the whole card. Before a large download,
compare it against the size you are about to write.

## Working with files

| Mode | Opens | Writes go |
| --- | --- | --- |
| `file_mode::read` | an existing file | (reading only) |
| `file_mode::write` | creates the file, or **empties** an existing one | from the start |
| `file_mode::append` | creates the file if needed | after the existing contents |

Two rules apply to all the recipes:

- **Close after writing, and check the result.** The data is committed
  when the file closes. The destructor closes too, but a destructor
  can't report a failure.
- **Use large blocks.** Reads and writes go straight to the filesystem
  with no buffer in between. That saves RAM, but every call costs a
  filesystem operation, and on the card a bus transfer. Use 512 bytes or
  more per call, never single bytes. `size()` and `position()` are free:
  the file tracks them in memory.

### Writing a text file

```cpp
// Replace a file's contents with text. Returns storage_error::none on
// success.
storage_error write_text(storage &disks,
                         volume where,
                         std::string_view path,
                         std::string_view text) {
    file out(disks, where, path, file_mode::write);
    static_cast<void>(out.write(text)); // a failure stays in out.error()
    static_cast<void>(out.close());
    return out.error();
}
```

Thanks to the sticky error, there is no need to check `write()`
separately: if it failed, `close()` returns `false` and `error()` holds
the reason.

### Reading a whole text file

```cpp
// Read a whole file as text; std::nullopt when it is missing, unreadable
// or larger than limit (so a huge file can't exhaust memory).
std::optional<std::string> read_text(storage &disks,
                                     volume where,
                                     std::string_view path,
                                     std::size_t limit = 64 * 1024) {
    file in(disks, where, path, file_mode::read);
    if (!in || in.size() > limit) {
        return std::nullopt;
    }
    std::string text(static_cast<std::size_t>(in.size()), '\0');
    if (in.read(std::as_writable_bytes(std::span(text))) != text.size()) {
        return std::nullopt;
    }
    return text;
}
```

The file's size is known as soon as it is open, so the string is
allocated once, at its final size.

### Appending to a log

```cpp
// Append one line to a log file, creating the file if needed.
bool append_line(storage &disks,
                 volume where,
                 std::string_view path,
                 std::string_view line) {
    file log(disks, where, path, file_mode::append);
    const std::string text = std::string(line) + "\n";
    static_cast<void>(log.write(text));
    return log.close();
}
```

Joining the line and its newline first makes one write instead of two.

### Reading a file line by line

```cpp
// Call on_line for every line of a text file. The file is read 512 bytes
// at a time, so even a large file needs little memory. Line endings may
// be "\n" or "\r\n"; lines longer than 1 KB are cut short.
storage_error
for_each_line(storage &disks,
              volume where,
              std::string_view path,
              const std::function<void(std::string_view)> &on_line) {
    constexpr std::size_t max_line = 1024;
    file in(disks, where, path, file_mode::read);
    if (!in) {
        return in.error();
    }
    std::array<char, 512> block{};
    std::string line;
    while (const std::size_t got =
               in.read(std::as_writable_bytes(std::span(block)))) {
        for (const char c : std::span(block).first(got)) {
            if (c == '\n') {
                on_line(line);
                line.clear();
            } else if (c != '\r' && line.size() < max_line) {
                line.push_back(c);
            }
        }
    }
    if (!line.empty()) {
        on_line(line); // last line without a newline
    }
    return in.error();
}
```

For example, to read a simple settings file of `name=value` lines:

```cpp
// Return the value of one "name=value" line, if present.
std::optional<std::string>
setting(storage &disks, std::string_view path, std::string_view name) {
    std::optional<std::string> value;
    const auto match = [&](std::string_view line) {
        const std::size_t equals = line.find('=');
        if (equals != std::string_view::npos &&
            line.substr(0, equals) == name) {
            value = std::string(line.substr(equals + 1));
        }
    };
    static_cast<void>(for_each_line(disks, volume::internal, path, match));
    return value;
}
```

### Copying a file, also between volumes

A file is named by a volume and a path. A small struct keeps the two
together and the parameters in the right order:

```cpp
// A file's place: which volume, and the path on it.
struct location {
    volume where;
    std::string_view path;
};

// Copy a file in 512-byte blocks. On failure the incomplete copy is
// removed.
storage_error copy_file(storage &disks, location from, location to) {
    file in(disks, from.where, from.path, file_mode::read);
    if (!in) {
        return in.error();
    }
    file out(disks, to.where, to.path, file_mode::write);
    if (!out) {
        return out.error();
    }
    std::array<std::byte, 512> block{};
    while (const std::size_t got = in.read(block)) {
        if (out.write(std::span(block).first(got)) != got) {
            break;
        }
    }
    const storage_error read_error = in.error();
    const bool written = out.close();
    if (read_error != storage_error::none || !written) {
        static_cast<void>(disks.remove(to.where, to.path));
        return read_error != storage_error::none ? read_error : out.error();
    }
    return storage_error::none;
}
```

Use it to keep a backup of the settings on the card:

```cpp
storage_error backup_settings(storage &disks) {
    return copy_file(disks,
                     {volume::internal, "settings.txt"},
                     {volume::card, "settings-backup.txt"});
}
```

### Saving safely

Writing in `write` mode empties the file first. If power is lost halfway,
the old contents are gone and the new ones are incomplete. For data that
matters, write a *new* file and only then swap it in:

```cpp
// Replace a file so that a power cut leaves either the old or the new
// contents, never a mix.
storage_error save_safely(storage &disks,
                          volume where,
                          std::string_view path,
                          std::string_view text) {
    const std::string temporary = std::string(path) + ".tmp";
    const storage_error written = write_text(disks, where, temporary, text);
    if (written != storage_error::none) {
        static_cast<void>(disks.remove(where, temporary));
        return written;
    }
    const storage_error removed = disks.remove(where, path);
    if (removed != storage_error::none &&
        removed != storage_error::not_found) {
        return removed;
    }
    return disks.rename(where, temporary, path);
}

// Load a file saved with save_safely(). If power was lost between the
// remove and the rename, only the complete temporary copy exists.
std::optional<std::string>
load_safely(storage &disks, volume where, std::string_view path) {
    if (disks.info(where, path)) {
        return read_text(disks, where, path);
    }
    return read_text(disks, where, std::string(path) + ".tmp");
}
```

`rename()` never replaces an existing file, so the old file has to be
removed first. That is why the fallback in `load_safely()` is needed.

### Binary data and random access

To store numbers, write their bytes **explicitly**, never by writing a
struct's memory: struct layout and byte order differ between compilers
and processors, and the file must read back the same everywhere. Records
of a fixed size can then be read and updated at any position with
`seek()`:

```cpp
// A reading position: page and offset within the page.
struct bookmark {
    std::uint32_t page = 0;
    std::uint32_t offset = 0;
};

// Bytes per stored bookmark: two 32-bit numbers, least significant byte
// first.
constexpr std::size_t bookmark_size = 8;

std::array<std::byte, bookmark_size> encode(const bookmark &mark) {
    std::array<std::byte, bookmark_size> bytes{};
    const std::array<std::uint32_t, 2> fields{mark.page, mark.offset};
    for (std::size_t field = 0; field < fields.size(); ++field) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes.at(field * 4 + i) =
                static_cast<std::byte>((fields.at(field) >> (8 * i)) & 0xFFU);
        }
    }
    return bytes;
}

bookmark decode(std::span<const std::byte, bookmark_size> bytes) {
    const auto field = [bytes](std::size_t first) {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value |= std::to_integer<std::uint32_t>(bytes[first + i])
                     << (8 * i);
        }
        return value;
    };
    return bookmark{field(0), field(4)};
}

// Store bookmark number index. An index up to the current count works:
// existing records are overwritten in place, the next one is appended.
bool write_bookmark(storage &disks, std::size_t index, const bookmark &mark) {
    // append mode keeps the existing records; seek() picks the position.
    file out(disks, volume::internal, "bookmarks.bin", file_mode::append);
    const std::uint64_t offset = std::uint64_t{index} * bookmark_size;
    if (!out || !out.seek(offset)) {
        return false;
    }
    static_cast<void>(out.write(encode(mark)));
    return out.close();
}

// Read bookmark number index, if it exists.
std::optional<bookmark> read_bookmark(storage &disks, std::size_t index) {
    file in(disks, volume::internal, "bookmarks.bin", file_mode::read);
    const std::uint64_t offset = std::uint64_t{index} * bookmark_size;
    std::array<std::byte, bookmark_size> bytes{};
    if (!in || !in.seek(offset) || in.read(bytes) != bytes.size()) {
        return std::nullopt;
    }
    return decode(bytes);
}

// Number of bookmarks stored.
std::size_t bookmark_count(storage &disks) {
    const auto found = disks.info(volume::internal, "bookmarks.bin");
    return found ? static_cast<std::size_t>(found->size / bookmark_size) : 0;
}
```

`seek()` accepts offsets from 0 to `size()`. A position beyond the end of
the file is refused, which is why `write_bookmark()` can append the next
record but can't leave a gap.

### Size, existence, moving and deleting

```cpp
// Return a file's size, or std::nullopt if there is no such file.
std::optional<std::uint64_t>
file_size(storage &disks, volume where, std::string_view path) {
    const auto found = disks.info(where, path);
    if (!found || found->is_directory) {
        return std::nullopt;
    }
    return found->size;
}

// Move a file into a folder on the same volume, keeping its name:
// move_into(disks, volume::card, "inbox/a.epub", "books") gives
// "books/a.epub".
storage_error move_into(storage &disks,
                        volume where,
                        std::string_view path,
                        std::string_view folder) {
    const std::size_t slash = path.rfind('/');
    const std::string_view name =
        slash == std::string_view::npos ? path : path.substr(slash + 1);
    return disks.rename(where, path, std::string(folder) + "/" +
                                         std::string(name));
}
```

Deleting a file is a single call. Treat "already gone" as success when
that is what you wanted:

```cpp
// Delete a file; a file that doesn't exist counts as deleted.
bool delete_file(storage &disks, volume where, std::string_view path) {
    const storage_error removed = disks.remove(where, path);
    return removed == storage_error::none ||
           removed == storage_error::not_found;
}
```

## Working with directories

`""` stands for the volume root when listing. Entries come in the order
the filesystem stores them, without `.` and `..`.

### Does a folder exist?

```cpp
// Return true when path exists and is a directory.
bool is_directory(storage &disks, volume where, std::string_view path) {
    const auto found = disks.info(where, path);
    return found && found->is_directory;
}
```

### Creating nested folders

`make_directory()` creates one level, and its parent must exist. This
recipe creates every missing level, like `mkdir -p`:

```cpp
// Create a directory and any missing parents; existing ones are fine.
storage_error
make_directories(storage &disks, volume where, std::string_view path) {
    std::size_t end = 0;
    do {
        end = path.find('/', end + 1);
        const std::string_view level = path.substr(0, end);
        const storage_error made = disks.make_directory(where, level);
        if (made == storage_error::already_exists &&
            is_directory(disks, where, level)) {
            continue;
        }
        if (made != storage_error::none) {
            return made;
        }
    } while (end != std::string_view::npos);
    return storage_error::none;
}
```

### A sorted listing

```cpp
// Most entries read from one folder; keeps memory bounded on huge folders.
constexpr std::size_t max_entries = 500;

// List a folder: subfolders first, then files, each sorted by name.
std::vector<directory_entry>
sorted_listing(storage &disks, volume where, std::string_view path) {
    std::vector<directory_entry> entries;
    directory listing(disks, where, path);
    while (auto entry = listing.next()) {
        if (entries.size() == max_entries) {
            break;
        }
        entries.push_back(std::move(*entry));
    }
    std::sort(entries.begin(),
              entries.end(),
              [](const directory_entry &a, const directory_entry &b) {
                  if (a.is_directory != b.is_directory) {
                      return a.is_directory;
                  }
                  return a.name < b.name;
              });
    return entries;
}
```

### Finding files by extension

```cpp
// Return true when name ends with extension, ignoring letter case.
bool has_extension(std::string_view name, std::string_view extension) {
    if (name.size() < extension.size()) {
        return false;
    }
    const std::string_view tail = name.substr(name.size() - extension.size());
    return std::equal(tail.begin(),
                      tail.end(),
                      extension.begin(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

// Names of the files in a folder with the given extension, sorted:
// books(disks) lists every EPUB in the card's books folder.
std::vector<std::string> books(storage &disks) {
    std::vector<std::string> names;
    for (const directory_entry &entry :
         sorted_listing(disks, volume::card, "books")) {
        if (!entry.is_directory && has_extension(entry.name, ".epub")) {
            names.push_back(entry.name);
        }
    }
    return names;
}
```

### Walking a folder tree

A recursive walk must not keep each level's listing open while it
descends. On the X4's internal flash at most 4 files and listings can be
open at once, so the walk would fail four levels down. Read a level's
entries first, let the listing close, and only then descend:

```cpp
// Deepest folder level the tree recipes visit; bounds recursion.
constexpr int max_depth = 8;

// Files and bytes in a folder tree.
struct tree_size {
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
};

// Read every entry of one folder, closing the listing before returning.
std::vector<directory_entry>
read_folder(storage &disks, volume where, const std::string &path) {
    std::vector<directory_entry> entries;
    directory listing(disks, where, path);
    while (auto entry = listing.next()) {
        entries.push_back(std::move(*entry));
    }
    return entries;
}

// Join a folder path and a name; "" is the volume root.
std::string join(const std::string &folder, const std::string &name) {
    return folder.empty() ? name : folder + "/" + name;
}

// Count the files and bytes below path ("" for the whole volume).
tree_size measure_tree(storage &disks,
                       volume where,
                       const std::string &path,
                       int depth = 0) {
    tree_size total;
    for (const directory_entry &entry : read_folder(disks, where, path)) {
        if (!entry.is_directory) {
            ++total.files;
            total.bytes += entry.size;
        } else if (depth < max_depth) {
            const tree_size inner =
                measure_tree(disks, where, join(path, entry.name), depth + 1);
            total.files += inner.files;
            total.bytes += inner.bytes;
        }
    }
    return total;
}
```

### Deleting a folder and everything in it

`remove()` only deletes an *empty* folder. This recipe empties it first,
using the same read-then-descend pattern, which also avoids changing a
folder while it is being listed:

```cpp
// Remove a folder and everything below it.
storage_error remove_tree(storage &disks,
                          volume where,
                          const std::string &path,
                          int depth = 0) {
    if (path.empty()) {
        return storage_error::invalid_path; // never empty a whole volume
    }
    if (depth > max_depth) {
        return storage_error::not_empty; // too deep: leave it alone
    }
    for (const directory_entry &entry : read_folder(disks, where, path)) {
        const std::string child = join(path, entry.name);
        const storage_error removed =
            entry.is_directory ? remove_tree(disks, where, child, depth + 1)
                               : disks.remove(where, child);
        if (removed != storage_error::none) {
            return removed;
        }
    }
    return disks.remove(where, path);
}
```

## Example: counting starts

This program keeps a start counter in internal flash, appends a line to
a log on the card, and shows the result. Replace `src/app/main.cpp`:

File: `src/app/main.cpp`

```cpp
//
// Counts program starts in internal flash, logs each start on the card,
// and shows both on the display.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/color.h>
#include <inkerface/directory.h>
#include <inkerface/display.h>
#include <inkerface/file.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>
#include <inkerface/input.h>
#include <inkerface/program.h>
#include <inkerface/storage.h>
#include <inkerface/surface.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using inkerface::file;
using inkerface::file_mode;
using inkerface::storage;
using inkerface::storage_error;
using inkerface::volume;

constexpr std::string_view count_path = "starts.txt";
constexpr std::string_view count_temporary = "starts.tmp";
constexpr unsigned max_count = 999999;

// Lines shown from the log directory listing.
constexpr std::size_t max_listed = 8;

// Parse an unsigned number from the start of a file; 0 when unreadable.
unsigned read_number(storage &disks, std::string_view path) {
    file in(disks, volume::internal, path, file_mode::read);
    std::array<char, 16> text{};
    const std::size_t length =
        in.read(std::as_writable_bytes(std::span(text)));
    unsigned value = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + length, value);
    return parsed.ec == std::errc{} ? value : 0;
}

// Read the stored count, falling back to an interrupted save.
unsigned read_count(storage &disks) {
    if (disks.info(volume::internal, count_path)) {
        return read_number(disks, count_path);
    }
    return read_number(disks, count_temporary);
}

// Store the count: write a new file completely, then swap it in.
bool write_count(storage &disks, unsigned count) {
    const std::string text = std::to_string(count);
    file out(disks, volume::internal, count_temporary, file_mode::write);
    if (out.write(text) != text.size() || !out.close()) {
        return false;
    }
    static_cast<void>(disks.remove(volume::internal, count_path));
    return disks.rename(volume::internal, count_temporary, count_path) ==
           storage_error::none;
}

// Append one line to logs/starts.log on the card; false without a card.
bool log_start(storage &disks, unsigned count) {
    if (!disks.available(volume::card)) {
        return false;
    }
    const storage_error made = disks.make_directory(volume::card, "logs");
    if (made != storage_error::none && made != storage_error::already_exists) {
        return false;
    }
    file log(disks, volume::card, "logs/starts.log", file_mode::append);
    const std::string line = "start " + std::to_string(count) + "\n";
    return log.write(line) == line.size() && log.close();
}

// Return the entries of logs/ as "name (size)" lines.
std::vector<std::string> list_logs(storage &disks) {
    std::vector<std::string> lines;
    inkerface::directory logs(disks, volume::card, "logs");
    while (const auto entry = logs.next()) {
        if (lines.size() == max_listed) {
            break;
        }
        lines.push_back(entry->name + " (" + std::to_string(entry->size) +
                        " bytes)");
    }
    return lines;
}

void paint(inkerface::surface &frame,
           const std::vector<std::string> &lines) {
    const inkerface::size whole = frame.extent();
    const inkerface::coord line =
        frame.line_height(inkerface::font::regular());
    frame.clear(inkerface::color::white);
    inkerface::coord y = 40;
    for (const std::string &text : lines) {
        frame.draw_text(inkerface::rect{24, y, whole.width, line},
                        text,
                        inkerface::text_style{});
        y = static_cast<inkerface::coord>(y + line);
    }
}

} // namespace

int program(int /*argc*/, char ** /*argv*/) {
    inkerface::display screen;
    inkerface::input buttons(screen);
    storage disks;

    std::vector<std::string> lines;
    unsigned count = read_count(disks);
    if (count < max_count) {
        ++count;
    }
    lines.push_back("Started " + std::to_string(count) + " times");
    if (!write_count(disks, count)) {
        lines.emplace_back("Could not save the count");
    }
    if (log_start(disks, count)) {
        lines.emplace_back("Logged on the card:");
        for (std::string &entry : list_logs(disks)) {
            lines.push_back("- " + std::move(entry));
        }
    } else {
        lines.emplace_back("No card: start not logged");
    }

    inkerface::surface frame = screen.frame();
    paint(frame, lines);
    if (!screen.present(inkerface::refresh_mode::full)) {
        return EXIT_FAILURE;
    }
    while (!buttons.closed()) {
        static_cast<void>(buttons.poll());
    }
    return EXIT_SUCCESS;
}
```

Run it a few times:

```sh
make run && make run && make run
```

The page shows "Started 3 times" and a log with three lines. On the
desktop the volumes are filesystem images. After closing the preview,
export a file through the storage utility:

```sh
export INKERFACE_STORAGE_ROOT="$PWD/build/storage"
bin/debug/inkerface_storage get internal starts.txt /tmp/starts.txt
cat /tmp/starts.txt     # 3
bin/debug/inkerface_storage get card logs/starts.log /tmp/starts.log
cat /tmp/starts.log     # start 1, start 2, start 3
```

## Where the desktop keeps the volumes

The desktop build stores each volume in an image file under the directory
named by the `INKERFACE_STORAGE_ROOT` environment variable:

```text
$INKERFACE_STORAGE_ROOT/internal.lfs   volume::internal (littlefs)
$INKERFACE_STORAGE_ROOT/card.fat       volume::card (FAT16)
```

`make run`, `make preview` and F5 set it to `build/storage`, so
`make clean` wipes the emulated volumes. Use
`make run STORAGE=/some/folder` to keep them somewhere else. When the
variable is **not** set, every volume is `unavailable`. A program then
never writes to an unexpected place, for example while running in a test.

Internal storage has `0x9E0000` bytes, matching the X4 data partition;
the emulated card has 32 MiB. Missing images are formatted on first use.
Existing images are never erased or reformatted automatically, and only
one process can mount an image at a time. No root privileges or operating
system mounts are needed. `make storage-images` initializes both images;
the storage tool supports `ls`, `mkdir`, `put` and `get`. Old folder-based
volumes are left untouched; use `put` to import their files explicitly.

To test code that uses storage, point the variable at a fresh temporary
folder before creating `storage`. `tests/unit/test_storage.cpp` shows
how.

## Using storage from screens

Screens are created by `make_screen()` without arguments (chapter 6), so
they can't receive `storage` yet. To keep a screen's state across starts
today, load and save it in `program()` around `application.run()`. A
screen that reads and writes files itself needs the shared-state pattern
from chapter 6: the application owns `storage`, and the screen receives a
reference in its constructor.

## Summary

- One `storage` per program. Volumes mount on first use, and
  `available()` checks for a card.
- Paths are relative, use `/`, and are strictly checked.
- `file` and `directory` report failures through a sticky `error()`.
  Always check `close()` after writing.
- I/O is unbuffered, so use blocks of 512 bytes or more. `size()` and
  `position()` are free.
- For important data, write a temporary file and swap it in
  (`save_safely()` and `load_safely()`).
- Store numbers with explicit byte encoding, never as raw struct memory.
- When walking folders, read a level's entries and close the listing
  before descending (`read_folder()`).

Next: [Storage on the device](10-STORAGE-ON-THE-DEVICE.md).
