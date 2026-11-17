//
// Declares the minimal test harness used by the unit tests. It avoids a
// third-party test framework; each test receives a context that records
// failures with their source location.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <iostream>
#include <source_location>
#include <string_view>

namespace tests {

// Collects check results for one test run.
class test_context {
public:
    //
    // Record one check.
    //
    // Parameters:
    //     ok   - Check outcome.
    //     what - Description printed on failure.
    //     where - Call site, captured automatically.
    //
    void check(bool ok,
               std::string_view what,
               std::source_location where = std::source_location::current()) {
        ++checks_;
        if (!ok) {
            ++failures_;
            std::cerr << where.file_name() << ':' << where.line()
                      << ": check failed: " << what << '\n';
        }
    }

    // Return the number of failed checks.
    [[nodiscard]] int failures() const {
        return failures_;
    }

    // Return the number of checks performed.
    [[nodiscard]] int checks() const {
        return checks_;
    }

private:
    int checks_ = 0;
    int failures_ = 0;
};

// Test entry points, one per test file.
void run_geometry_tests(test_context &context);
void run_surface_tests(test_context &context);
void run_hello_world_tests(test_context &context);
void run_application_tests(test_context &context);
void run_storage_tests(test_context &context);
void run_storage_image_tests(test_context &context);
void run_network_tests(test_context &context);
void run_font_tests(test_context &context);

} // namespace tests
