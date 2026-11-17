# Verifies that no FreeInk, Arduino, SdFat or LittleFS header or name
# leaks outside the adapter layer (lib/inkerface/src/).
# Run as: cmake -DROOT=<repo> -P <this file>.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT must name the repository root")
endif()

file(GLOB_RECURSE guarded_files
    "${ROOT}/include/*.h"
    "${ROOT}/src/*.h"
    "${ROOT}/src/*.cpp"
    "${ROOT}/tests/*.h"
    "${ROOT}/tests/*.cpp")

# Platform library headers and namespaces that only the adapter may use.
set(forbidden_patterns
    "#include[ \t]*[<\"](FreeInk|EInkDisplay|InputManager|BoardConfig|XteinkDetect|BatteryMonitor|SDCardManager|SdFat|LittleFS|Arduino|SecureClient|SecureHttpClient|WiFi|Client\\.h|openssl/|esp_|freertos/|lfs\\.h|ff\\.h|diskio\\.h)"
    "freeink::"
    "EInkDisplay"
    "lib/inkerface/src")

set(violations "")
foreach(file IN LISTS guarded_files)
    file(READ "${file}" content)
    foreach(pattern IN LISTS forbidden_patterns)
        if(content MATCHES "${pattern}")
            list(APPEND violations "${file}: matches '${pattern}'")
        endif()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n" report)
    message(FATAL_ERROR "FreeInk leaked outside the adapter:\n${report}")
endif()
list(LENGTH guarded_files checked)
message(STATUS "isolation: ${checked} files free of FreeInk")
