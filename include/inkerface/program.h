//
// Declares the portable application entry point. Every backend provides
// the real platform entry (main() on the host, Arduino setup() on the
// device) and calls program() from it, so application code has one
// standard main on every target.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

//
// Run the application. Implemented by the application, called by the
// platform layer exactly once.
//
// Parameters:
//     argc - Argument count; zero on targets without a command line.
//     argv - argc arguments followed by a null pointer.
//
// Returns:
//     Process exit status. On the device the program normally never
//     returns; if it does, the platform idles.
//
int program(int argc, char **argv);
