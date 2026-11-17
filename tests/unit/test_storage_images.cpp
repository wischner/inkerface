// Checks image persistence, capacity, FAT names, exclusive access and
// refusal to reformat damaged media through the public storage interface.
// MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
#include "check.h"

#include <inkerface/directory.h>
#include <inkerface/file.h>
#include <inkerface/storage.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace tests {
namespace {
using namespace inkerface;

class temporary_root {
public:
    temporary_root() {
        path_ =
            (std::filesystem::temp_directory_path() / "inkerface-images-XXXXXX")
                .string();
        if (::mkdtemp(path_.data()) == nullptr) {
            throw std::runtime_error("cannot create image test root");
        }
        ::setenv("INKERFACE_STORAGE_ROOT", path_.c_str(), 1);
    }
    ~temporary_root() {
        ::unsetenv("INKERFACE_STORAGE_ROOT");
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    temporary_root(const temporary_root &) = delete;
    temporary_root &operator=(const temporary_root &) = delete;
    temporary_root(temporary_root &&) = delete;
    temporary_root &operator=(temporary_root &&) = delete;
    std::filesystem::path path(std::string_view name) const {
        return std::filesystem::path(path_) / name;
    }

private:
    std::string path_;
};

std::string read_text(storage &disks, volume where, std::string_view name) {
    file in(disks, where, name, file_mode::read);
    std::array<char, 64> text{};
    const auto count = in.read(std::as_writable_bytes(std::span(text)));
    return {text.data(), count};
}

void persistence(test_context &context) {
    temporary_root root;
    const std::string name = "books/Živjo reader.txt";
    {
        storage disks;
        for (const auto where : {volume::internal, volume::card}) {
            context.check(disks.make_directory(where, "books") ==
                              storage_error::none,
                          "image directory created");
            file out(disks, where, name, file_mode::write);
            context.check(out.write("hello") == 5 && out.close(),
                          "image file committed");
            file append(disks, where, name, file_mode::append);
            context.check(append.position() == 5 && append.write("!") == 1,
                          "image append starts at end");
            context.check(append.seek(0) && append.write("H") == 1 &&
                              append.close(),
                          "seek after append writes at requested offset");
            directory listing(disks, where, "books");
            const auto item = listing.next();
            context.check(item && item->name == "Živjo reader.txt" &&
                              item->size == 6,
                          "image listing preserves UTF-8 long names and size");
        }
        storage locked;
        context.check(!locked.available(volume::internal),
                      "mounted flash image is exclusively locked");
        context.check(!locked.available(volume::card),
                      "mounted card cannot be opened twice");
    }
    context.check(std::filesystem::file_size(root.path("internal.lfs")) ==
                      0x9e0000,
                  "internal image matches X4 partition size");
    context.check(std::filesystem::file_size(root.path("card.fat")) ==
                      32U * 1024U * 1024U,
                  "card image has fixed 32 MiB capacity");
    context.check(!std::filesystem::exists(root.path("internal")) &&
                      !std::filesystem::exists(root.path("card")),
                  "no unpacked volume directories");
    {
        storage disks;
        for (const auto where : {volume::internal, volume::card}) {
            context.check(read_text(disks, where, name) == "Hello!",
                          "data persists after remount");
            context.check(disks.rename(where, name, "books/kept.txt") ==
                              storage_error::none,
                          "rename persists on image");
            context.check(disks.remove(where, "books") ==
                              storage_error::not_empty,
                          "nonempty image directory cannot be removed");
            context.check(
                disks.remove(where, "books/kept.txt") == storage_error::none &&
                    disks.remove(where, "books") == storage_error::none,
                "remove file and empty image directory");
        }
        for (const auto *bad :
             {"0:escape", "trailing.", "trailing ", "wild*card"}) {
            file rejected(disks, volume::card, bad, file_mode::write);
            context.check(rejected.error() == storage_error::invalid_path,
                          "FAT aliases and reserved names rejected");
        }
    }
}

void full_images(test_context &context) {
    temporary_root root;
    storage disks;
    std::array<std::byte, 65536> chunk{};
    for (const auto where : {volume::internal, volume::card}) {
        const auto before = disks.free_space(where);
        file out(disks, where, "full.bin", file_mode::write);
        std::uint64_t written = 0;
        // Bounded even if capacity enforcement regresses.
        for (int index = 0; index < 600 && out; ++index) {
            written += out.write(chunk);
        }
        context.check(
            out.error() == storage_error::no_space && written > 0,
            "full image reports no_space rather than growing the host file");
        static_cast<void>(out.close());
        context.check(disks.remove(where, "full.bin") == storage_error::none,
                      "full image remains usable for removal");
        const auto recovered = disks.free_space(where);
        context.check(before && recovered && *recovered > *before / 2,
                      "removing a full file recovers image capacity");
    }
}

void damaged_images(test_context &context) {
    temporary_root root;
    for (const auto where : {volume::internal, volume::card}) {
        const auto path =
            root.path(where == volume::internal ? "internal.lfs" : "card.fat");
        const std::string sentinel = "not a filesystem; keep these bytes";
        {
            std::ofstream out(path, std::ios::binary);
            out << sentinel;
        }
        {
            storage disks;
            context.check(!disks.available(where),
                          "truncated image is unavailable");
        }
        std::ifstream existing(path, std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(existing),
                                   {}};
        context.check(contents == sentinel,
                      "failed mount leaves existing image untouched");
        existing.close();
        std::filesystem::resize_file(
            path, where == volume::internal ? 0x9e0000 : 32U * 1024U * 1024U);
        const auto modified = std::filesystem::last_write_time(path);
        {
            storage disks;
            context.check(!disks.available(where),
                          "corrupt full-sized image is unavailable");
        }
        context.check(
            std::filesystem::last_write_time(path) == modified,
            "corrupt full-sized image is never automatically formatted");
        std::filesystem::remove(path);
        std::filesystem::create_symlink(root.path("target"), path);
        {
            storage disks;
            context.check(!disks.available(where),
                          "image path cannot be a symlink");
        }
        context.check(!std::filesystem::exists(root.path("target")),
                      "image symlink target is untouched");
    }
}

} // namespace

void run_storage_image_tests(test_context &context) {
    persistence(context);
    full_images(context);
    damaged_images(context);
}
} // namespace tests
