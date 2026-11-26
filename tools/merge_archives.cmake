# Runs `ar -M` on an MRI script to merge static archives into one.
# Usage: cmake -DAR=<ar> -DSCRIPT=<file.mri> -P merge_archives.cmake
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih

execute_process(COMMAND "${AR}" -M
    INPUT_FILE "${SCRIPT}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "ar -M failed (${result}) on ${SCRIPT}")
endif()
