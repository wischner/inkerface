//
// Runs every unit test and reports the totals. Exits non-zero when any
// check fails.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "check.h"

#include <cstdlib>
#include <iostream>

int main() {
    tests::test_context context;
    tests::run_geometry_tests(context);
    tests::run_surface_tests(context);
    tests::run_hello_world_tests(context);
    tests::run_application_tests(context);
    tests::run_storage_tests(context);
    tests::run_storage_image_tests(context);
    tests::run_network_tests(context);
    tests::run_font_tests(context);
    std::cout << context.checks() << " checks, " << context.failures()
              << " failures\n";
    return context.failures() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
