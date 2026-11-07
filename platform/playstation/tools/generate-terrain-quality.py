#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate quiet PS1-native CI4 terrain tiles for the 2.5D experiment."""

from __future__ import annotations

import argparse
from pathlib import Path


SIDE = 32
VARIANTS = 4
TRANSITIONS = 16

PALETTES = (
    # grass: shadow, two broad ground values, moss, blade and field-stone cues
    ((0, 0, 0), (37, 78, 45), (43, 91, 50), (55, 105, 55),
     (72, 119, 60), (84, 132, 65), (70, 91, 73), (92, 104, 91),
     (28, 61, 38), (49, 97, 45), (64, 111, 51), (104, 113, 95),
     (31, 68, 41), (48, 94, 49), (68, 116, 58), (121, 124, 103)),
    # forest: dark floor/roots/leaves plus three grass colours for edge blends
    ((0, 0, 0), (22, 57, 36), (27, 68, 40), (34, 80, 43),
     (45, 94, 47), (62, 108, 52), (75, 54, 34), (96, 67, 39),
     (19, 45, 31), (39, 73, 38), (53, 87, 43), (81, 101, 55),
     (37, 78, 45), (48, 94, 49), (68, 116, 58), (109, 91, 58)),
    # stone: slabs/seams/highlights, moss and three grass transition colours
    ((0, 0, 0), (82, 90, 88), (48, 57, 59), (101, 109, 105),
     (118, 124, 117), (67, 75, 75), (132, 132, 119), (58, 72, 65),
     (72, 91, 66), (90, 105, 74), (37, 78, 45), (48, 94, 49),
     (68, 116, 58), (41, 50, 53), (145, 141, 122), (104, 91, 70)),
)


def ellipse(tile: list[int], cx: int, cy: int, rx: int, ry: int, colour: int) -> None:
    for y in range(max(0, cy - ry), min(SIDE, cy + ry + 1)):
        for x in range(max(0, cx - rx), min(SIDE, cx + rx + 1)):
            if (x - cx) ** 2 * ry * ry + (y - cy) ** 2 * rx * rx <= rx * rx * ry * ry:
                tile[y * SIDE + x] = colour


def grass(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    ellipse(tile, 7 + variant * 5, 8 + (variant & 1) * 14, 8, 5, 1)
    ellipse(tile, 25 - variant * 3, 23 - (variant & 1) * 13, 10, 6, 3)
    for blade in range(5):
        x = (variant * 9 + blade * 7 + 3) % 29 + 1
        y = (variant * 5 + blade * 11 + 4) % 27 + 2
        tile[y * SIDE + x] = 5
        tile[(y - 1) * SIDE + min(SIDE - 1, x + 1)] = 4
    if variant in (1, 3):
        ellipse(tile, 25 if variant == 1 else 6, 25, 2, 1, 7)
    return tile


def forest(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    ellipse(tile, 8 + variant * 5, 9 + (variant & 1) * 13, 10, 7, 1)
    ellipse(tile, 24 - variant * 3, 24 - (variant & 1) * 12, 9, 6, 3)
    # Broad root forks read at native resolution without noisy single pixels.
    root_x = 7 + variant * 6
    for step in range(13):
        y = 8 + step
        x = root_x + step // 4
        if 0 <= x < SIDE and 0 <= y < SIDE:
            tile[y * SIDE + x] = 7 if step < 5 else 6
        branch = x + (step - 6) // 2
        if step >= 6 and 0 <= branch < SIDE:
            tile[y * SIDE + branch] = 6
    for leaf in range(4):
        ellipse(tile, (variant * 7 + leaf * 9 + 4) % 29 + 1,
                (variant * 11 + leaf * 7 + 5) % 27 + 2, 2, 1, 5)
    return tile


def stone(variant: int) -> list[int]:
    tile = [1] * (SIDE * SIDE)
    offset = (variant & 1) * 4
    for y in range(SIDE):
        for x in range(SIDE):
            row = y // 8
            seam_x = (x + (row & 1) * 8 + offset) % 16
            if y % 8 == 0 or seam_x == 0:
                tile[y * SIDE + x] = 2
            elif ((x // 8 + y // 8 + variant) & 1) == 0:
                tile[y * SIDE + x] = 3
    ellipse(tile, 6 + variant * 6, 24 - variant * 4, 5, 2, 8)
    if variant in (2, 3):
        ellipse(tile, 26, 6 + variant * 3, 3, 2, 9)
    return tile


def transition(tile: list[int], mask: int, first_colour: int) -> list[int]:
    result = list(tile)
    for distance in range(5):
        colour = first_colour + min(2, (4 - distance) // 2)
        span = range(distance, SIDE - distance)
        if mask & 1:  # north
            for x in span:
                result[distance * SIDE + x] = colour
        if mask & 2:  # east
            for y in span:
                result[y * SIDE + SIDE - 1 - distance] = colour
        if mask & 4:  # south
            for x in span:
                result[(SIDE - 1 - distance) * SIDE + x] = colour
        if mask & 8:  # west
            for y in span:
                result[y * SIDE + distance] = colour
    return result


def pack(tile: list[int]) -> list[int]:
    return [sum(tile[offset + nibble] << (nibble * 4) for nibble in range(4))
            for offset in range(0, len(tile), 4)]


def colour15(colour: tuple[int, int, int]) -> int:
    red, green, blue = (channel >> 3 for channel in colour)
    return red | green << 5 | blue << 10


def generate() -> str:
    bases = ([grass(index) for index in range(VARIANTS)] +
             [forest(index) for index in range(VARIANTS)] +
             [stone(index) for index in range(VARIANTS)])
    cells = list(bases)
    cells.extend(transition(forest(mask & 3), mask, 12)
                 for mask in range(TRANSITIONS))
    cells.extend(transition(stone(mask & 3), mask, 10)
                 for mask in range(TRANSITIONS))
    lines = [
        "// Generated by platform/playstation/tools/generate-terrain-quality.py.",
        "// Do not edit. Deterministic PS1-native CI4 terrain experiment data.",
        "#pragma once",
        f"inline constexpr int grandleon_psx_quality_terrain_side = {SIDE};",
        f"inline constexpr int grandleon_psx_quality_terrain_cell_count = {len(cells)};",
        "inline constexpr unsigned short grandleon_psx_quality_terrain_cells"
        f"[{len(cells)}][{SIDE * SIDE // 4}] = {{",
    ]
    for cell in cells:
        values = pack(cell)
        rows = [",".join(str(value) for value in values[index:index + 16])
                for index in range(0, len(values), 16)]
        lines.append("    {" + ",".join(rows) + "},")
    lines.append("};")
    lines.append(
        "inline constexpr unsigned short grandleon_psx_quality_terrain_cluts"
        "[3][16] = {"
    )
    for palette in PALETTES:
        lines.append("    {" + ",".join(str(colour15(colour)) for colour in palette) + "},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content = generate()
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != content:
            raise SystemExit(f"stale generated terrain header: {args.output}")
        print(f"PS1 quality terrain header is current: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
