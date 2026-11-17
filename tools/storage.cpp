// Provides native image initialization, listing and file transfer through
// Inkerface's public API. Filesystem engines and image paths stay in the
// backend. MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
#include <inkerface/directory.h>
#include <inkerface/file.h>
#include <inkerface/storage.h>

#include <array>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>

namespace {
using namespace inkerface;

volume parse_volume(std::string_view text) {
    if (text == "internal") {
        return volume::internal;
    }
    if (text == "card") {
        return volume::card;
    }
    throw std::invalid_argument("volume must be internal or card");
}

void require(bool success, const char *message) {
    if (!success) {
        throw std::runtime_error(message);
    }
}

void copy_in(storage &disks,
             volume where,
             const char *source,
             const char *destination) {
    std::ifstream in{source, std::ios::binary};
    require(static_cast<bool>(in), "cannot open source file");
    file out(disks, where, destination, file_mode::write);
    require(static_cast<bool>(out), "cannot open image file for writing");
    std::array<char, 4096> buffer{};
    while (true) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(in.gcount());
        require(out.write(std::as_bytes(std::span(buffer).first(count))) ==
                    count,
                "image write failed (possibly full)");
        if (!in) {
            break;
        }
    }
    require(in.eof() && !in.bad(), "source read failed");
    require(out.close(), "image commit failed");
}

void copy_out(storage &disks,
              volume where,
              const char *source,
              const char *destination) {
    file in(disks, where, source, file_mode::read);
    require(static_cast<bool>(in), "cannot open image file for reading");
    std::ofstream out{destination, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(out), "cannot open destination file");
    std::array<char, 4096> buffer{};
    while (true) {
        const auto count = in.read(std::as_writable_bytes(std::span(buffer)));
        require(in.error() == storage_error::none, "image read failed");
        if (count == 0) {
            break;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(count));
        require(static_cast<bool>(out), "destination write failed");
    }
    out.close();
    require(static_cast<bool>(out), "destination commit failed");
}

int run(std::span<char *> argv) {
    const auto argc = argv.size();
    storage disks;
    const std::string_view command = argc > 1 ? argv[1] : "";
    if (command == "init" && argc == 2) {
        require(disks.available(volume::internal) &&
                    disks.available(volume::card),
                "cannot mount images; check INKERFACE_STORAGE_ROOT, existing "
                "images and locks");
        std::cout << "Storage images ready\n";
        return EXIT_SUCCESS;
    }
    if ((command == "ls" && (argc == 3 || argc == 4)) ||
        (command == "mkdir" && argc == 4) ||
        ((command == "put" || command == "get") && argc == 5)) {
        const auto where = parse_volume(argv[2]);
        if (command == "ls") {
            directory listing(disks, where, argc == 4 ? argv[3] : "");
            require(static_cast<bool>(listing), "cannot open image directory");
            while (const auto item = listing.next()) {
                std::cout << item->name << (item->is_directory ? "/" : "")
                          << '\t' << item->size << '\n';
            }
            require(listing.error() == storage_error::none,
                    "image listing failed");
        } else if (command == "mkdir") {
            require(disks.make_directory(where, argv[3]) == storage_error::none,
                    "cannot create image directory");
        } else if (command == "put") {
            copy_in(disks, where, argv[3], argv[4]);
        } else {
            copy_out(disks, where, argv[3], argv[4]);
        }
        return EXIT_SUCCESS;
    }
    std::cerr
        << "usage: inkerface_storage init\n"
           "       inkerface_storage ls VOLUME [PATH]\n"
           "       inkerface_storage mkdir VOLUME PATH\n"
           "       inkerface_storage put VOLUME HOST_FILE IMAGE_PATH\n"
           "       inkerface_storage get VOLUME IMAGE_PATH HOST_FILE\n"
           "Set INKERFACE_STORAGE_ROOT to the directory containing images.\n";
    return EXIT_FAILURE;
}
} // namespace

int main(int argc, char **argv) {
    try {
        return run({argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception &error) {
        std::cerr << "storage: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
