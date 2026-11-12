#!/usr/bin/env python3
"""Regenerate the embedded 1-bit logo from assets/logos/l1.png (Pillow needed).

MIT License (see LICENSE). Copyright (C) 2026 Tomaz Stih.
"""

from pathlib import Path

from PIL import Image


def main():
    root = Path(__file__).resolve().parents[1]
    with Image.open(root / "assets/logos/l1.png") as source:
        gray = source.convert("L").resize((240, 320), Image.Resampling.LANCZOS)
    # PBM/our bitmap convention: 1 = black. A fixed threshold keeps the
    # source's pale background white and its gray logo strokes legible.
    mask = gray.point(lambda value: 255 if value < 180 else 0, mode="1")
    data = mask.tobytes()
    rows = [
        "    " + ", ".join(f"0x{byte:02x}" for byte in data[i:i + 12]) + ","
        for i in range(0, len(data), 12)
    ]
    header = """// Embedded logo from assets/logos/l1.png; regenerate with
// python3 tools/convert_logo.py. Row-major, MSB-first, set bit = ink.
// Generated asset data is kept in source so firmware needs no PNG decoder.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih

#pragma once

#include <inkerface/geometry.h>

#include <array>
#include <cstdint>

namespace ui::assets {

inline constexpr inkerface::size logo_size{240, 320};
inline constexpr std::array<std::uint8_t, 9600> logo_bits{
"""
    destination = root / "src/assets/logo.h"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(header + "\n".join(rows)
                           + "\n};\n\n} // namespace ui::assets\n")


if __name__ == "__main__":
    main()
