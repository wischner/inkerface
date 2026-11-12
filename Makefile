# Top-level build entry point. Host builds go through CMake into build/,
# binaries land in bin/<profile>/. The Xteink X4 build goes through
# PlatformIO (platformio.ini).
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih

CMAKE      ?= cmake
PIO        ?= pio
PYTHON     ?= python3
CLANG_FORMAT ?= clang-format
FREEINK    ?= $(CURDIR)/lib/freeink-sdk
PREVIEW    ?= ON
# Host TLS (sockets and https) with OpenSSL.
TLS        ?= ON
# Host storage volumes live in $(STORAGE)/internal and $(STORAGE)/card.
STORAGE    ?= $(CURDIR)/build/storage
JOBS       ?= $(shell nproc 2>/dev/null || echo 4)
# Noto Sans TTF files for `make fonts` (Debian/Ubuntu: fonts-noto-core).
NOTO       ?= /usr/share/fonts/truetype/noto
FONTS_OUT  := lib/inkerface/src/freeink/fonts

cmake_configure = $(CMAKE) -S . -B build/$(1) \
	-DCMAKE_BUILD_TYPE=$(2) \
	-DFREEINK_SDK_DIR=$(FREEINK) \
	-DINKERFACE_PREVIEW=$(PREVIEW) \
	-DINKERFACE_TLS=$(TLS) \
	-DINKERFACE_BIN_DIR=$(CURDIR)/bin/$(1) $(3)

.PHONY: all sdk debug release sanitize test run preview x4 x4-upload format clean purge fonts

all: debug

sdk:
	$(PYTHON) tools/download_sdk.py "$(FREEINK)"

debug: sdk
	$(call cmake_configure,debug,Debug,)
	$(CMAKE) --build build/debug -j $(JOBS)

release: sdk
	$(call cmake_configure,release,Release,)
	$(CMAKE) --build build/release -j $(JOBS)

sanitize: sdk
	$(call cmake_configure,sanitize,Debug,-DINKERFACE_SANITIZE=ON)
	$(CMAKE) --build build/sanitize -j $(JOBS)

test: debug sanitize
	cd build/debug && ctest --output-on-failure
	cd build/sanitize && ctest --output-on-failure

# Render the hello world screen on the host into bin/debug/hello_world.pbm.
run: debug
	INKERFACE_FRAME_PATH=$(CURDIR)/bin/debug/hello_world.pbm \
	INKERFACE_STORAGE_ROOT=$(STORAGE) \
		bin/debug/inkerface < /dev/null
	@echo "frame written to bin/debug/hello_world.pbm"

preview: debug
	INKERFACE_PREVIEW=1 INKERFACE_STORAGE_ROOT=$(STORAGE) bin/debug/inkerface

# Regenerate the built-in fonts (needs Pillow). Regular is FreeInk's own.
fonts: sdk
	$(PYTHON) tools/make_font.py --ttf $(NOTO)/NotoSans-Regular.ttf --size 16 \
		--format builtin --name small_font --out $(FONTS_OUT)/small_font.h
	$(PYTHON) tools/make_font.py --ttf $(NOTO)/NotoSans-Bold.ttf --size 24 \
		--format builtin --name bold_font --out $(FONTS_OUT)/bold_font.h
	$(PYTHON) tools/make_font.py --ttf $(NOTO)/NotoSans-Bold.ttf --size 32 \
		--format builtin --name title_font --out $(FONTS_OUT)/title_font.h

x4: sdk
	$(PIO) run -e x4

x4-upload: sdk
	$(PIO) run -e x4 -t upload

format:
	$(CLANG_FORMAT) --dry-run --Werror \
		$$(find include lib/inkerface src tests -name '*.h' -o -name '*.cpp')

clean:
	rm -rf build bin .pio

# Only purge our managed cache; an external FREEINK checkout is never deleted.
purge: clean
	rm -rf lib/freeink-sdk
