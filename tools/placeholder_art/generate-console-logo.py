#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package the existing Grandleon mark as compact C-compatible rectangles."""
from __future__ import annotations

import argparse
from pathlib import Path

from placeholder_art import logo, palette


def header() -> str:
    canvas = logo.compose()
    rectangles: list[list[int]] = []
    active: dict[tuple[int, int, int], int] = {}
    for y in range(canvas.height):
        current: dict[tuple[int, int, int], int] = {}
        x = 0
        while x < canvas.width:
            colour = canvas.data[y * canvas.width + x]
            end = x + 1
            while end < canvas.width and canvas.data[y * canvas.width + end] == colour:
                end += 1
            if colour:
                key = (x, end - x, colour)
                if key in active:
                    index = active[key]
                    rectangles[index][3] += 1
                else:
                    index = len(rectangles)
                    rectangles.append([x, y, end - x, 1, *palette.RGB[colour]])
                current[key] = index
            x = end
        active = current
    # Check the complete reconstructed mark, including its transparent pixels.
    restored = [None] * (canvas.width * canvas.height)
    for x, y, width, height, r, g, b in rectangles:
        for yy in range(y, y + height):
            for xx in range(x, x + width):
                index = yy * canvas.width + xx
                assert restored[index] is None, "overlapping logo rectangles"
                restored[index] = (r, g, b)
    expected = [palette.RGB[index] if index else None for index in canvas.data]
    assert restored == expected, "console mark differs from the source logo"
    return (
        "// SPDX-License-Identifier: MIT\n"
        "// Generated from placeholder_art/logo.py by tools/placeholder_art/generate-console-logo.py.\n"
        "#ifndef GRANDLEON_VIEW_BRAND_LOGO_H\n#define GRANDLEON_VIEW_BRAND_LOGO_H\n"
        "#include <stdint.h>\n"
        f"enum {{ GRANDLEON_LOGO_WIDTH = {canvas.width}, GRANDLEON_LOGO_HEIGHT = {canvas.height} }};\n"
        "typedef struct { uint8_t x, y, width, height, r, g, b; } grandleon_logo_rect;\n"
        "static const grandleon_logo_rect grandleon_logo_rects[] = {\n"
        + "\n".join("    { " + ", ".join(map(str, rectangle)) + " }," for rectangle in rectangles)
        + "\n};\n"
        "#define GRANDLEON_LOGO_RECT_COUNT (sizeof(grandleon_logo_rects) / sizeof(grandleon_logo_rects[0]))\n"
        "#endif\n"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    target = Path(__file__).resolve().parents[2] / "platform/view/include/grandleon/view/brand_logo.h"
    output = header()
    if args.check:
        if target.read_text() != output:
            raise SystemExit("Console logo needs regeneration")
        print("Console logo matches its source")
    else:
        target.write_text(output)
