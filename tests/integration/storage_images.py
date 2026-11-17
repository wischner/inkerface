#!/usr/bin/env python3
"""Exercise native image persistence across separate storage-tool processes.

MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="image-processes-", dir=sys.argv[2]) as tmp:
        root = Path(tmp)
        env = dict(os.environ, INKERFACE_STORAGE_ROOT=str(root / "volumes"))
        payload = bytes(range(256)) * 39 + b"persistent data\x00\xff"
        source = root / "source.bin"
        source.write_bytes(payload)

        def run(*args):
            return subprocess.run([binary, *map(str, args)], env=env,
                                  check=True, capture_output=True, text=True).stdout

        run("init")
        for volume in ("internal", "card"):
            run("mkdir", volume, "books")
            run("put", volume, source, "books/Živjo.bin")
            listing = run("ls", volume, "books")
            assert f"Živjo.bin\t{len(payload)}" in listing, listing
            target = root / f"{volume}.bin"
            run("get", volume, "books/Živjo.bin", target)
            assert target.read_bytes() == payload, volume
        # Initialization is idempotent; it must never reset existing images.
        run("init")
        run("get", "internal", "books/Živjo.bin", root / "again.bin")
        assert (root / "again.bin").read_bytes() == payload
        image = root / "volumes/card.fat"
        with image.open("rb") as stream:
            boot = stream.read(512)
        assert boot[510:512] == b"\x55\xaa", "FAT boot signature"
        # FatFs writes the generic label "FAT     ". FAT type is determined
        # by the data-cluster count, not by that informational label.
        word = lambda offset: int.from_bytes(boot[offset:offset + 2], "little")
        sector_bytes = word(11)
        sectors = word(19) or int.from_bytes(boot[32:36], "little")
        root_sectors = (word(17) * 32 + sector_bytes - 1) // sector_bytes
        data_sectors = sectors - word(14) - boot[16] * word(22) - root_sectors
        clusters = data_sectors // boot[13]
        assert 4085 <= clusters < 65525, "expected a real FAT16 volume"
        print("Image import/export, UTF-8, and cross-process persistence passed")


if __name__ == "__main__":
    main()
