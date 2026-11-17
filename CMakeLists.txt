# Host (native) build of inkerface. FreeInkUI is compiled from the SDK
# checkout as a separate static library and linked PRIVATELY into the
# platform library, so FreeInk include paths never reach application or
# test targets. The device build uses platformio.ini instead.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih

cmake_minimum_required(VERSION 3.20)
project(inkerface LANGUAGES C CXX)

include(CTest)

# Keep every intermediate artifact under the single root build/ tree.
get_filename_component(inkerface_source_dir "${CMAKE_SOURCE_DIR}" REALPATH)
get_filename_component(inkerface_binary_dir "${CMAKE_BINARY_DIR}" REALPATH)
string(FIND "${inkerface_binary_dir}/" "${inkerface_source_dir}/build/"
    inkerface_build_prefix_position)
if(NOT inkerface_build_prefix_position EQUAL 0)
    message(FATAL_ERROR
        "Build directory must be inside ${inkerface_source_dir}/build")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FREEINK_SDK_DIR "${CMAKE_SOURCE_DIR}/lib/freeink-sdk"
    CACHE PATH "Cached FreeInk SDK checkout")
set(INKERFACE_BIN_DIR "${CMAKE_SOURCE_DIR}/bin"
    CACHE PATH "Directory for final binaries")
option(INKERFACE_WERROR "Treat first-party warnings as errors" ON)
option(INKERFACE_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(INKERFACE_PREVIEW "Build the Linux X11/XWayland desktop preview" ON)
option(INKERFACE_TLS "Build host TLS (sockets and https) with OpenSSL" ON)

set(freeink_ui_dir "${FREEINK_SDK_DIR}/libs/ui/FreeInkUI")
if(NOT EXISTS "${freeink_ui_dir}/src/FreeInkUI.cpp")
    message(FATAL_ERROR
        "FreeInk SDK not found at ${FREEINK_SDK_DIR}. Run:\n"
        "  make sdk")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${INKERFACE_BIN_DIR}")

# Warning baseline for first-party code (docs/standards/C++.md, 22).
set(inkerface_warnings
    -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow
    -Wconversion -Wsign-conversion -Wnull-dereference)
if(INKERFACE_WERROR)
    list(APPEND inkerface_warnings -Werror)
endif()

if(INKERFACE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-D_FORTIFY_SOURCE=3 -fstack-protector-strong
        -fstack-clash-protection)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_link_options(-pie -Wl,-z,relro,-z,now)
endif()

# --- Third party: FreeInkUI (freestanding C++, compiled unmodified) -----
add_library(freeink_ui STATIC "${freeink_ui_dir}/src/FreeInkUI.cpp")
# SYSTEM narrows third-party warning suppression to FreeInk headers.
target_include_directories(freeink_ui SYSTEM PUBLIC "${freeink_ui_dir}/include")

# Native image files use the real filesystem engines; no mounts or root.
set(filesystem_dir "${CMAKE_SOURCE_DIR}/lib/host-filesystems")
if(NOT EXISTS "${filesystem_dir}/VERSION")
    message(FATAL_ERROR "Native filesystems not cached. Run: make filesystems")
endif()
add_library(host_filesystems STATIC
    "${filesystem_dir}/littlefs/lfs.c"
    "${filesystem_dir}/littlefs/lfs_util.c"
    "${filesystem_dir}/fatfs/ff.c"
    "${filesystem_dir}/fatfs/ffunicode.c")
target_include_directories(host_filesystems SYSTEM PUBLIC
    "${filesystem_dir}/littlefs" "${filesystem_dir}/fatfs")
target_include_directories(host_filesystems PUBLIC
    lib/inkerface/src/host/filesystem_config)
target_compile_definitions(host_filesystems PRIVATE LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)

# --- Platform library: our API over FreeInk ----------------------------
add_library(inkerface_platform STATIC
    lib/inkerface/src/freeink/canvas.cpp
    lib/inkerface/src/freeink/surface.cpp
    lib/inkerface/src/freeink/fonts.cpp
    lib/inkerface/src/freeink/font_file.cpp
    lib/inkerface/src/host/display.cpp
    lib/inkerface/src/host/input.cpp
    lib/inkerface/src/common/storage.cpp
    lib/inkerface/src/host/storage.cpp
    lib/inkerface/src/host/disk_image.cpp
    lib/inkerface/src/host/image_volume.cpp
    lib/inkerface/src/net/socket.cpp
    lib/inkerface/src/net/http.cpp
    lib/inkerface/src/host/network.cpp
    lib/inkerface/src/host/arduino/arduino_shim.cpp
    lib/inkerface/src/host/secure_client.cpp)
target_include_directories(inkerface_platform PUBLIC include)
target_link_libraries(inkerface_platform PRIVATE freeink_ui host_filesystems)
target_compile_options(inkerface_platform PRIVATE ${inkerface_warnings})
# Networking runs FreeInk's SecureNet headers unchanged; on the host the
# Arduino API they need comes from our shim in host/arduino.
target_include_directories(inkerface_platform PRIVATE
    lib/inkerface/src/host/arduino)
target_include_directories(inkerface_platform SYSTEM PRIVATE
    "${FREEINK_SDK_DIR}/libs/network/SecureNet/include")
if(INKERFACE_TLS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(inkerface_platform PRIVATE OpenSSL::SSL)
    target_compile_definitions(inkerface_platform PRIVATE INKERFACE_HAS_TLS=1)
endif()
if(INKERFACE_PREVIEW)
    find_package(X11 REQUIRED)
    target_sources(inkerface_platform PRIVATE lib/inkerface/src/host/preview.cpp)
    target_link_libraries(inkerface_platform PRIVATE X11::X11)
    target_compile_definitions(inkerface_platform PRIVATE INKERFACE_HAS_PREVIEW=1)
endif()

# Host process entry (main -> program); separate so tests own main().
add_library(inkerface_entry OBJECT lib/inkerface/src/host/entry.cpp)
target_include_directories(inkerface_entry PRIVATE include)
target_compile_options(inkerface_entry PRIVATE ${inkerface_warnings})

# --- Application -------------------------------------------------------
add_library(inkerface_app STATIC
    src/app/app.cpp
    src/screens/hello_world_screen.cpp)
target_link_libraries(inkerface_app PUBLIC inkerface_platform)
target_compile_options(inkerface_app PRIVATE ${inkerface_warnings})

add_executable(inkerface src/app/main.cpp $<TARGET_OBJECTS:inkerface_entry>)
target_link_libraries(inkerface PRIVATE inkerface_app)
target_compile_options(inkerface PRIVATE ${inkerface_warnings})

# Inspect and populate native images through the same public storage API.
add_executable(inkerface_storage tools/storage.cpp)
target_link_libraries(inkerface_storage PRIVATE inkerface_platform)
target_compile_options(inkerface_storage PRIVATE ${inkerface_warnings})

# --- Tests -------------------------------------------------------------
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(inkerface_tests
        tests/unit/test_main.cpp
        tests/unit/test_geometry.cpp
        tests/unit/test_surface.cpp
        tests/unit/test_hello_world.cpp
        tests/unit/test_application.cpp
        tests/unit/test_storage.cpp
        tests/unit/test_storage_images.cpp
        tests/unit/test_network.cpp
        tests/unit/test_fonts.cpp)
    target_link_libraries(inkerface_tests PRIVATE inkerface_app)
    target_compile_options(inkerface_tests PRIVATE ${inkerface_warnings})
    add_test(NAME unit COMMAND inkerface_tests)

    add_test(NAME storage_images
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/integration/storage_images.py"
                "$<TARGET_FILE:inkerface_storage>" "${CMAKE_BINARY_DIR}")

    add_test(NAME isolation
        COMMAND "${CMAKE_COMMAND}" -DROOT=${CMAKE_SOURCE_DIR}
                -P "${CMAKE_SOURCE_DIR}/tests/isolation/check_isolation.cmake")

    # End-to-end: run the program with no input and check a frame exists.
    add_test(NAME hello_world_frame
        COMMAND "${CMAKE_COMMAND}" -E env
                "INKERFACE_FRAME_PATH=${CMAKE_BINARY_DIR}/hello_world.pbm"
                sh -c "$<TARGET_FILE:inkerface> < /dev/null && test -s '${CMAKE_BINARY_DIR}/hello_world.pbm'")
endif()
