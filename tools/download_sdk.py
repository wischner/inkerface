#!/usr/bin/env python3
"""Fetch the pinned SDK once, publishing only a complete checkout.

MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


sdk_url = "https://github.com/Free-Ink/freeink-sdk.git"
sdk_revision = "e637713a533ea7583ba73fa2052967c21cf0335d"


def download(destination):
    """Reuse an existing SDK; failed fetches never populate the cache."""
    required = Path("libs/ui/FreeInkUI/src/FreeInkUI.cpp")
    if (destination / required).is_file():
        return
    if destination.exists():
        raise RuntimeError(f"incomplete SDK at {destination}; remove it and retry")
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Same filesystem as the destination, allowing an atomic rename.
    with tempfile.TemporaryDirectory(
        prefix=".freeink-sdk-", dir=destination.parent
    ) as temporary:
        checkout = Path(temporary) / "sdk"
        subprocess.run(["git", "init", "--quiet", str(checkout)], check=True)
        command = ["git", "-C", str(checkout)]
        subprocess.run(command + ["remote", "add", "origin", sdk_url], check=True)
        subprocess.run(command + ["fetch", "--depth", "1", "origin", sdk_revision],
                       check=True)
        subprocess.run(command + ["checkout", "--quiet", "--detach", sdk_revision],
                       check=True)
        if not (checkout / required).is_file():
            raise RuntimeError("downloaded SDK is missing FreeInkUI")
        checkout.rename(destination)
    print(f"Cached FreeInk SDK {sdk_revision} in {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    try:
        download(arguments.destination.resolve())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"SDK download failed: {error}\n")


if __name__ == "__main__":
    main()
