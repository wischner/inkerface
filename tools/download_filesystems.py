#!/usr/bin/env python3
"""Cache pinned native filesystem sources and upstream FatFs fixes.

Downloads and staging stay in build/; only selected source files are
published into lib/host-filesystems. Never installs system packages.
MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
"""

import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile


root = Path(__file__).resolve().parents[1]
revision = "6cb4e86540eca0d9ba62500a298385c9d863c8be"  # littlefs v2.11.3
sources = {
    "littlefs.tar.gz": (
        f"https://codeload.github.com/littlefs-project/littlefs/tar.gz/{revision}",
        "4803faf6db6c60093558a315a10c50926d2cfa5668a3f03fa2523fd09b10b747"),
    "fatfs.zip": (
        "https://elm-chan.org/fsw/ff/arc/ff16.zip",
        "99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e"),
    "ff16p1.diff": (
        "https://elm-chan.org/fsw/ff/patch/ff16p1.diff",
        "7996ddc3135f3d534a8153f7d75d587030e5c8551b003c3b7ee823ffb7fc64bf"),
    "ff16p2.diff": (
        "https://elm-chan.org/fsw/ff/patch/ff16p2.diff",
        "5cd39f1fc299f0f1dec9fa1ce544dc0bc048b2f6eb3b8161476ed20f9cf1e290"),
}


def download(name, url, digest):
    path = root / "build/downloads" / name
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as out:
            temporary = Path(out.name)
            try:
                with urllib.request.urlopen(url, timeout=60) as response:
                    shutil.copyfileobj(response, out)
                out.close()
                if hashlib.sha256(temporary.read_bytes()).hexdigest() != digest:
                    raise RuntimeError(f"checksum mismatch: {url}")
                temporary.replace(path)
            finally:
                temporary.unlink(missing_ok=True)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f"checksum mismatch: {path}; remove it and retry")
    return path


def main():
    destination = root / "lib/host-filesystems"
    marker = "littlefs-2.11.3-fatfs-0.16-p2\n"
    required = ("littlefs/lfs.c", "littlefs/lfs.h", "littlefs/lfs_util.c",
                "littlefs/lfs_util.h", "littlefs/LICENSE.md", "fatfs/ff.c",
                "fatfs/ff.h", "fatfs/diskio.h", "fatfs/ffunicode.c")
    if destination.exists():
        if ((destination / "VERSION").is_file()
                and (destination / "VERSION").read_text() == marker
                and all((destination / name).is_file() for name in required)):
            return
        raise RuntimeError(f"incomplete or outdated cache: {destination}")
    archives = {name: download(name, *source) for name, source in sources.items()}
    with tempfile.TemporaryDirectory(prefix="filesystems-", dir=root / "build") as tmp:
        stage = Path(tmp) / "sources"
        (stage / "littlefs").mkdir(parents=True)
        (stage / "fatfs").mkdir()
        with tarfile.open(archives["littlefs.tar.gz"]) as archive:
            for name in ("lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h", "LICENSE.md"):
                data = archive.extractfile(f"littlefs-{revision}/{name}")
                if data is None:
                    raise RuntimeError(f"missing littlefs source: {name}")
                (stage / "littlefs" / name).write_bytes(data.read())
        with zipfile.ZipFile(archives["fatfs.zip"]) as archive:
            for name in ("ff.c", "ff.h", "diskio.h", "ffunicode.c", "00readme.txt"):
                # Upstream diffs use LF; retain the embedded copyright notices.
                data = archive.read(f"source/{name}").replace(b"\r\n", b"\n")
                (stage / "fatfs" / name).write_bytes(data)
        for name in ("ff16p1.diff", "ff16p2.diff"):
            subprocess.run(["patch", "--batch", "--fuzz=0", "ff.c",
                            str(archives[name])], cwd=stage / "fatfs", check=True)
        (stage / "VERSION").write_text(marker)
        stage.rename(destination)
    print(f"Cached littlefs 2.11.3 and FatFs R0.16 + patches 1/2 in {destination}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            tarfile.TarError, zipfile.BadZipFile) as error:
        raise SystemExit(f"Filesystem download failed: {error}") from error
