//
// Provides the host process entry point. It forwards to the portable
// program() and is the outermost exception boundary: an escaping
// exception is reported without internal detail and fails the process.
// Kept in its own library so tests can supply their own main().
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/program.h>

#include <cstdlib>
#include <exception>
#include <iostream>

int main(int argc, char **argv) {
    try {
        return program(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "inkerface: fatal error: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "inkerface: fatal error\n";
    }
    return EXIT_FAILURE;
}
