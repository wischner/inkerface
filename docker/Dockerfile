# Tool image for inkerface: the build tools that should not be installed
# on the developer's machine (PlatformIO, clang-format, Pillow and the
# Noto fonts), plus the host toolchain, so CI can build everything in it.
#
# Only tools live here. Every downloaded package (FreeInk, littlefs,
# FatFs, PlatformIO platforms and toolchains) goes to the project's
# packages/ directory, which the Makefile mounts; the image stays small
# and `make clean` keeps the downloads.
#
# Built and run by the Makefile (`make docker-image`); the tag includes
# a hash of this file, so editing it rebuilds the image.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih

FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive
ARG PLATFORMIO_VERSION=6.2.0

RUN apt-get update \
 && apt-get install --yes --no-install-recommends \
      ca-certificates git make cmake g++ patch pkg-config \
      python3 python3-venv python3-pil \
      libx11-dev libssl-dev \
      clang-format fonts-noto-core \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/platformio \
 && /opt/platformio/bin/pip install --no-cache-dir \
      "platformio==${PLATFORMIO_VERSION}"

# The Makefile runs tools directly when it sees INKERFACE_IN_DOCKER.
# HOME is writable for any user id the container runs as.
ENV PATH=/opt/platformio/bin:$PATH \
    INKERFACE_IN_DOCKER=1 \
    HOME=/tmp \
    PLATFORMIO_DISABLE_PROGRESSBAR=true

WORKDIR /work
