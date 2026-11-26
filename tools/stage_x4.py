#!/usr/bin/env python3
"""Stage the prebuilt Xteink X4 library from a PlatformIO build.

PlatformIO builds every library as an archive (lib_archive = yes). This
merges inkerface with the FreeInk libraries and their third-party
dependencies into one libinkerface.a, leaving out the Arduino-ESP32
framework libraries, which a consumer's own framework provides. Next to
it go the public headers, the flash layout and a platformio.ini for a
consumer project. The output directory is replaced.

MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
"""

import argparse
import configparser
from pathlib import Path
import shutil
import subprocess
import sys


root = Path(__file__).resolve().parents[1]


def framework_libraries(core):
    """Return the names of the libraries bundled with Arduino-ESP32."""
    names = {"FrameworkArduino"}
    for libraries in core.glob("packages/framework-arduinoespressif32*/libraries"):
        names.update(entry.name for entry in libraries.iterdir() if entry.is_dir())
    if len(names) == 1:
        raise RuntimeError(f"no Arduino-ESP32 framework under {core}")
    return names


def find_ar(core):
    """Return the ESP32-C3 toolchain's ar, which indexes RISC-V objects."""
    found = sorted(core.glob("packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-ar"))
    if not found:
        raise RuntimeError(f"no riscv32-esp-elf-ar under {core}")
    return found[0]


def project_setting(section, key):
    """Read one plain setting from the project's platformio.ini."""
    parser = configparser.ConfigParser(
        interpolation=None, comment_prefixes=(";", "#"),
        inline_comment_prefixes=(";",))
    parser.read(root / "platformio.ini")
    return parser.get(section, key).strip()


def consumer_ini(version, framework_deps):
    """Return a platformio.ini for a project linking the staged library."""
    deps = "\n".join(f"  {name}" for name in framework_deps)
    return f"""; PlatformIO project for an application linking the prebuilt
; inkerface {version} Xteink X4 library. Copy it into your project, set
; inkerface.dir to this folder, and implement program() from
; <inkerface/program.h> in src/. The platform must stay exactly this one:
; the library was compiled against its Arduino-ESP32 framework.

[inkerface]
dir = /path/to/inkerface-x4

[env:x4]
platform = {project_setting("base", "platform")}
framework = arduino
board = {project_setting("env:x4", "board")}
board_build.flash_mode = dio
board_build.flash_size = 16MB
board_build.partitions = ${{inkerface.dir}}/partitions-x4.csv
board_build.filesystem = littlefs
build_unflags = -std=gnu++11
build_flags =
  -std=gnu++2a
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
  -I${{inkerface.dir}}/include
  -L${{inkerface.dir}}/lib
  -linkerface
; Link only what the program uses (see the inkerface README).
lib_archive = yes
; Arduino-ESP32 libraries the prebuilt archive calls into.
lib_deps =
{deps}
"""


def stage(build, core, out, version):
    archives = sorted(build.rglob("*.a"))
    framework = framework_libraries(core)
    merged, framework_deps = [], set()
    for archive in archives:
        name = archive.stem.removeprefix("lib")
        if name in framework:
            if name != "FrameworkArduino":
                framework_deps.add(name)
        else:
            merged.append(archive)
    if not any(a.name == "libinkerface.a" for a in merged):
        raise RuntimeError(f"no libinkerface.a under {build}; build the x4 env first")

    if out.exists():
        shutil.rmtree(out)
    (out / "lib").mkdir(parents=True)
    target = out / "lib/libinkerface.a"
    script = "".join([f"CREATE {target}\n"]
                     + [f"ADDLIB {archive}\n" for archive in merged]
                     + ["SAVE\n", "END\n"])
    subprocess.run([str(find_ar(core)), "-M"], input=script, text=True, check=True)

    shutil.copytree(root / "include/inkerface", out / "include/inkerface")
    shutil.copy2(root / "partitions-x4.csv", out / "partitions-x4.csv")
    (out / "platformio.ini").write_text(consumer_ini(version, sorted(framework_deps)))
    (out / "VERSION").write_text(version + "\n")
    names = ", ".join(a.stem.removeprefix("lib") for a in merged)
    print(f"Staged {out}: libinkerface.a from {names}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=root / "build/pio/x4")
    parser.add_argument("--core", type=Path, default=root / "packages/platformio")
    parser.add_argument("--out", type=Path, default=root / "bin/x4")
    parser.add_argument("--version", default="0.1.0-dev")
    arguments = parser.parse_args()
    try:
        stage(arguments.build.resolve(), arguments.core.resolve(),
              arguments.out.resolve(), arguments.version)
    except (OSError, RuntimeError, configparser.Error,
            subprocess.CalledProcessError) as error:
        sys.exit(f"X4 staging failed: {error}")


if __name__ == "__main__":
    main()
