# Top-level build entry point.
#
#   make          build every platform and stage the libraries into bin/
#   make help     list the platforms and every target
#
# Third-party packages are downloaded into packages/ the first time they
# are needed and kept there: `make clean` removes build/ and bin/ only,
# `make purge` also removes packages/. Tools that a developer machine
# should not have to install (PlatformIO and the ESP32 toolchain,
# clang-format, Pillow) run in the image built from docker/Dockerfile.
# The Linux host build uses the machine's own g++ and CMake, so the
# preview window and debugging stay native.
#
# Build output stays in build/: objects, the demo program, the tests and
# the X4 firmware. bin/ receives only the distributable libraries:
# bin/linux/debug/, bin/linux/release/ and bin/x4/.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih

CMAKE      ?= cmake
PYTHON     ?= python3
DOCKER     ?= docker
FREEINK    ?= $(CURDIR)/packages/freeink-sdk
PREVIEW    ?= ON
# Host TLS (sockets and https) with OpenSSL.
TLS        ?= ON
# Native storage images live in $(STORAGE)/internal.lfs and card.fat.
STORAGE    ?= $(CURDIR)/build/storage
JOBS       ?= $(shell nproc 2>/dev/null || echo 4)
# Serial port of the X4 for x4-upload.
X4_PORT    ?= /dev/ttyACM0
# Noto Sans TTF files for `make fonts`; installed in the tool image.
NOTO       ?= /usr/share/fonts/truetype/noto
FONTS_OUT  := lib/inkerface/src/freeink/fonts
HOST_ARCH  := $(shell uname -m)

# Version recorded in the distribution: the vX.Y.Z tag on this commit,
# or a development version. CI passes it explicitly.
tag_version := $(patsubst v%,%,$(shell git describe --tags --exact-match \
	--match 'v[0-9]*.[0-9]*.[0-9]*' 2>/dev/null))
VERSION    ?= $(if $(tag_version),$(tag_version),0.1.0-dev)

# --- Tool image ----------------------------------------------------------
# The tag hashes the Dockerfile, so an edited Dockerfile builds a new image.
IMAGE      ?= inkerface-tools:$(shell sha256sum docker/Dockerfile | cut -c1-12)
docker_run  = $(DOCKER) run --rm --user $(shell id -u):$(shell id -g) \
	--volume "$(CURDIR):$(CURDIR)" --workdir "$(CURDIR)" $(1) $(IMAGE)

# Inside the image the tools are on PATH; outside, run them through it.
ifeq ($(INKERFACE_IN_DOCKER),1)
tool        :=
tool_upload :=
tool_monitor :=
tool_image  :=
else
tool         = $(call docker_run,)
tool_upload  = $(call docker_run,--device=$(X4_PORT) \
	--group-add=$$(stat -c %g $(X4_PORT)))
tool_monitor = $(call docker_run,--interactive --tty --device=$(X4_PORT) \
	--group-add=$$(stat -c %g $(X4_PORT)))
tool_image  := docker-image
endif

# --- Host build helpers ----------------------------------------------------
cmake_configure = $(CMAKE) -S . -B build/$(1) \
	-DCMAKE_BUILD_TYPE=$(2) \
	-DFREEINK_SDK_DIR=$(FREEINK) \
	-DINKERFACE_PREVIEW=$(PREVIEW) \
	-DINKERFACE_TLS=$(TLS) \
	-DINKERFACE_VERSION=$(VERSION) $(3)

# Replace bin/linux/<profile>/ with exactly the distributable library.
stage = rm -rf bin/linux/$(1) && \
	$(CMAKE) --install build/$(1) --prefix $(CURDIR)/bin/linux/$(1)

# Pack one bin/ directory as build/archives/<name>.tar.gz, <name>/ inside.
archive = tar -C $(dir $(1)) -czf build/archives/$(2).tar.gz \
	--transform 's,^$(notdir $(1)),$(2),' $(notdir $(1))

.PHONY: all help packages sdk filesystems x4-packages docker-image \
	linux debug release sanitize test run preview storage-images \
	x4 x4-upload x4-uploadfs x4-monitor fonts format archives ci clean purge

all: packages linux x4

help:
	@echo "inkerface $(VERSION)"
	@echo ""
	@echo "Supported platforms:"
	@echo "  linux    Linux desktop ($(HOST_ARCH)), with an X11 preview of the X4 panel"
	@echo "           make linux  ->  bin/linux/debug/, bin/linux/release/"
	@echo "  x4       Xteink X4 e-reader (ESP32-C3, SSD1677 800x480 e-paper)"
	@echo "           make x4     ->  bin/x4/  (built in the tool image)"
	@echo ""
	@echo "Building:"
	@echo "  make              all platforms; downloads missing packages first"
	@echo "  make linux        debug and release host libraries"
	@echo "  make debug        debug host build      (bin/linux/debug/)"
	@echo "  make release      hardened -O2 build    (bin/linux/release/)"
	@echo "  make sanitize     ASan/UBSan build, in build/sanitize/ only"
	@echo "  make x4           X4 library            (bin/x4/)"
	@echo "  make test         debug + sanitize builds, then every test"
	@echo "  make archives     bin/ as release tarballs in build/archives/"
	@echo "  make ci           what the GitHub pipeline runs, in the tool image"
	@echo ""
	@echo "Running:"
	@echo "  make run          render the demo frame to build/debug/hello_world.pbm"
	@echo "  make preview      run the demo in a desktop window"
	@echo "  make storage-images  create the native storage images"
	@echo "  make x4-upload    flash the demo firmware (X4_PORT=$(X4_PORT))"
	@echo "  make x4-uploadfs  write data/ to the X4's internal volume (erases it)"
	@echo "  make x4-monitor   open the X4's serial console, 115200 baud"
	@echo ""
	@echo "Maintenance:"
	@echo "  make packages     download every third-party package into packages/"
	@echo "  make docker-image build the tool image ($(IMAGE))"
	@echo "  make fonts        regenerate the built-in fonts"
	@echo "  make format       check formatting with clang-format"
	@echo "  make clean        remove build/ and bin/; keep packages/"
	@echo "  make purge        clean, and remove packages/ and the tool image"
	@echo ""
	@echo "Variables: PREVIEW=ON|OFF TLS=ON|OFF STORAGE=dir FREEINK=dir"
	@echo "           X4_PORT=dev VERSION=x.y.z JOBS=n"

# --- Packages ----------------------------------------------------------------
packages: sdk filesystems x4-packages

sdk:
	$(PYTHON) tools/download_sdk.py "$(FREEINK)"

filesystems:
	$(PYTHON) tools/download_filesystems.py

# PlatformIO's platform, toolchain, framework and libraries. The stamp is
# older than platformio.ini when a dependency changed, which reinstalls.
x4-packages: packages/.x4-packages

packages/.x4-packages: platformio.ini | sdk $(tool_image)
	$(tool) pio pkg install --environment x4
	touch $@

docker-image:
	@$(DOCKER) image inspect $(IMAGE) >/dev/null 2>&1 || \
		$(DOCKER) build --tag $(IMAGE) docker

# --- Linux host ----------------------------------------------------------------
linux: debug release

debug: sdk filesystems
	$(call cmake_configure,debug,Debug,)
	$(CMAKE) --build build/debug -j $(JOBS)
	$(call stage,debug)

release: sdk filesystems
	$(call cmake_configure,release,Release,)
	$(CMAKE) --build build/release -j $(JOBS)
	$(call stage,release)

sanitize: sdk filesystems
	$(call cmake_configure,sanitize,Debug,-DINKERFACE_SANITIZE=ON)
	$(CMAKE) --build build/sanitize -j $(JOBS)

test: debug sanitize
	cd build/debug && ctest --output-on-failure
	cd build/sanitize && ctest --output-on-failure

# Render the hello world screen on the host into build/debug/hello_world.pbm.
run: debug
	INKERFACE_FRAME_PATH=$(CURDIR)/build/debug/hello_world.pbm \
	INKERFACE_STORAGE_ROOT=$(STORAGE) \
		build/debug/inkerface < /dev/null
	@echo "frame written to build/debug/hello_world.pbm"

preview: debug
	INKERFACE_PREVIEW=1 INKERFACE_STORAGE_ROOT=$(STORAGE) build/debug/inkerface

storage-images: debug
	INKERFACE_STORAGE_ROOT=$(STORAGE) bin/linux/debug/bin/inkerface_storage init

# --- Xteink X4 -----------------------------------------------------------------
# Builds the demo firmware in build/pio/ and stages the library from its
# archives into bin/x4/.
x4: sdk x4-packages | $(tool_image)
	$(tool) pio run --environment x4
	$(tool) python3 tools/stage_x4.py --version $(VERSION)

x4-upload: sdk x4-packages | $(tool_image)
	$(tool_upload) pio run --environment x4 --target upload \
		--upload-port $(X4_PORT)

# Replace the internal volume with the contents of data/.
x4-uploadfs: sdk x4-packages | $(tool_image)
	$(tool_upload) pio run --environment x4 --target uploadfs \
		--upload-port $(X4_PORT)

x4-monitor: x4-packages | $(tool_image)
	$(tool_monitor) pio device monitor --environment x4 --port $(X4_PORT)

# --- Tools ---------------------------------------------------------------------
# Regenerate the built-in fonts. Regular is FreeInk's own.
fonts: sdk | $(tool_image)
	$(tool) python3 tools/make_font.py --ttf $(NOTO)/NotoSans-Regular.ttf --size 16 \
		--format builtin --name small_font --out $(FONTS_OUT)/small_font.h
	$(tool) python3 tools/make_font.py --ttf $(NOTO)/NotoSans-Bold.ttf --size 24 \
		--format builtin --name bold_font --out $(FONTS_OUT)/bold_font.h
	$(tool) python3 tools/make_font.py --ttf $(NOTO)/NotoSans-Bold.ttf --size 32 \
		--format builtin --name title_font --out $(FONTS_OUT)/title_font.h

format: | $(tool_image)
	$(tool) clang-format --dry-run --Werror \
		$$(find include lib/inkerface src tests -name '*.h' -o -name '*.cpp')

# --- Release -------------------------------------------------------------------
archives:
	@for dir in bin/linux/debug bin/linux/release bin/x4; do \
		test -d $$dir || { echo "$$dir is missing; run make first" >&2; exit 1; }; \
	done
	rm -rf build/archives && mkdir -p build/archives
	$(call archive,bin/linux/debug,inkerface-$(VERSION)-linux-$(HOST_ARCH)-debug)
	$(call archive,bin/linux/release,inkerface-$(VERSION)-linux-$(HOST_ARCH))
	$(call archive,bin/x4,inkerface-$(VERSION)-x4)
	@ls -l build/archives

# The whole pipeline in the tool image. LeakSanitizer needs ptrace.
ci: docker-image
	$(call docker_run,--cap-add=SYS_PTRACE) \
		make VERSION=$(VERSION) all test archives

# --- Cleaning ------------------------------------------------------------------
clean:
	rm -rf build bin .pio

# Only our managed cache; an external FREEINK checkout is never deleted.
purge: clean
	rm -rf packages
	-@$(DOCKER) image ls --quiet inkerface-tools 2>/dev/null | \
		xargs -r $(DOCKER) image rm --force >/dev/null
