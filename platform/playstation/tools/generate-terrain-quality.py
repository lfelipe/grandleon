#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate distinct, quiet PS1-native CI4 terrain materials.

The tactical renderer loads four 32×32 cells and one 16-colour CLUT for each
portable terrain kind.  Keeping the recipes here makes the art deterministic,
reviewable, and cheap enough to remain resident beside the character atlas.
"""

from __future__ import annotations

import argparse
from collections.abc import Callable
from pathlib import Path


SIDE = 32
VARIANTS = 4


def ellipse(tile: list[int], cx: int, cy: int, rx: int, ry: int, colour: int) -> None:
    for y in range(max(0, cy - ry), min(SIDE, cy + ry + 1)):
        for x in range(max(0, cx - rx), min(SIDE, cx + rx + 1)):
            if (x - cx) ** 2 * ry * ry + (y - cy) ** 2 * rx * rx <= rx * rx * ry * ry:
                tile[y * SIDE + x] = colour


def line(tile: list[int], x0: int, y0: int, x1: int, y1: int, colour: int) -> None:
    dx = abs(x1 - x0)
    sx = 1 if x0 < x1 else -1
    dy = -abs(y1 - y0)
    sy = 1 if y0 < y1 else -1
    error = dx + dy
    while True:
        if 0 <= x0 < SIDE and 0 <= y0 < SIDE:
            tile[y0 * SIDE + x0] = colour
        if x0 == x1 and y0 == y1:
            return
        twice = 2 * error
        if twice >= dy:
            error += dy
            x0 += sx
        if twice <= dx:
            error += dx
            y0 += sy


def speckles(tile: list[int], variant: int, colour: int, count: int, salt: int) -> None:
    for index in range(count):
        x = (index * 17 + variant * 11 + salt * 7) % 30 + 1
        y = (index * 23 + variant * 13 + salt * 5) % 30 + 1
        tile[y * SIDE + x] = colour


def water(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    for row in range(5):
        y = 4 + row * 6 + (variant + row) % 2
        start = (variant * 7 + row * 11) % 12 - 4
        for x in range(start, SIDE, 16):
            line(tile, x, y, x + 7, y, 4 if row & 1 else 3)
            line(tile, x + 8, y - 1, x + 11, y - 1, 5)
    ellipse(tile, 8 + variant * 5, 12 + (variant & 1) * 9, 5, 2, 1)
    return tile


def road(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    ellipse(tile, 7 + variant * 5, 9 + (variant & 1) * 14, 8, 5, 3)
    ellipse(tile, 25 - variant * 3, 23 - (variant & 1) * 12, 9, 6, 1)
    # Short scuffs survive the diamond projection as packed earth.  Full-height
    # vertical ruts looked like upright timber walls on the real PS1 board.
    for mark in range(5):
        x = (variant * 7 + mark * 9 + 2) % 27
        y = (variant * 5 + mark * 6 + 4) % 27
        line(tile, x, y, min(SIDE - 1, x + 5), y + (mark & 1), 5 if mark & 1 else 4)
    speckles(tile, variant, 7, 7, 1)
    return tile


def forest(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    ellipse(tile, 8 + variant * 5, 9 + (variant & 1) * 13, 10, 7, 1)
    ellipse(tile, 24 - variant * 3, 24 - (variant & 1) * 12, 9, 6, 3)
    root_x = 7 + variant * 6
    for step in range(13):
        y = 8 + step
        x = root_x + step // 4
        if 0 <= x < SIDE:
            tile[y * SIDE + x] = 7 if step < 5 else 6
        if step >= 6 and 0 <= x + (step - 6) // 2 < SIDE:
            tile[y * SIDE + x + (step - 6) // 2] = 6
    for leaf in range(4):
        ellipse(tile, (variant * 7 + leaf * 9 + 4) % 29 + 1,
                (variant * 11 + leaf * 7 + 5) % 27 + 2, 2, 1, 5)
    return tile


def mountain(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    for band in range(4):
        y = 4 + band * 8 + (variant + band) % 3
        line(tile, -2, y, 11, y - 4, 4 if band & 1 else 3)
        line(tile, 11, y - 4, 23, y + 1, 5)
        line(tile, 23, y + 1, 34, y - 3, 4)
    line(tile, 5 + variant * 3, 4, 14 + variant * 2, 27, 1)
    line(tile, 15 + variant, 10, 26, 29, 6)
    speckles(tile, variant, 7, 8, 2)
    return tile


def sand(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    for row in range(4):
        y = 5 + row * 8 + ((variant + row) & 1)
        line(tile, 0, y, 11, y - 2, 3)
        line(tile, 11, y - 2, 24, y, 4)
        line(tile, 24, y, 31, y - 1, 3)
    ellipse(tile, 6 + variant * 7, 25 - variant * 3, 3, 1, 1)
    speckles(tile, variant, 5, 6, 3)
    return tile


def snow(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    ellipse(tile, 8 + variant * 5, 10 + (variant & 1) * 12, 10, 6, 1)
    ellipse(tile, 25 - variant * 4, 24 - (variant & 1) * 11, 8, 5, 3)
    for sparkle in range(5):
        x = (variant * 9 + sparkle * 7 + 3) % 28 + 2
        y = (variant * 5 + sparkle * 11 + 4) % 28 + 2
        tile[y * SIDE + x] = 6
        tile[y * SIDE + x - 1] = 4
    return tile


def swamp(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    ellipse(tile, 9 + variant * 4, 10 + (variant & 1) * 13, 10, 6, 1)
    ellipse(tile, 25 - variant * 4, 23 - (variant & 1) * 10, 8, 5, 3)
    for reed in range(5):
        x = (variant * 7 + reed * 9 + 4) % 28 + 2
        y = (variant * 11 + reed * 5 + 9) % 22 + 8
        line(tile, x, y, x + (reed & 1), y - 5, 6)
        tile[(y - 4) * SIDE + max(0, x - 1)] = 7
    return tile


def hills(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    ellipse(tile, 16 + (variant - 1) * 3, 20 - (variant & 1) * 5, 18, 11, 3)
    for offset, colour in ((0, 5), (3, 4), (7, 1)):
        y = 15 + offset - (variant & 1) * 3
        line(tile, 0, y + 3, 9, y - 1, colour)
        line(tile, 9, y - 1, 22, y - 3, colour)
        line(tile, 22, y - 3, 31, y, colour)
    speckles(tile, variant, 6, 5, 4)
    return tile


def ruins(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    offset = (variant & 1) * 4
    for y in range(SIDE):
        for x in range(SIDE):
            row = y // 8
            seam_x = (x + (row & 1) * 8 + offset) % 16
            if y % 8 == 0 or seam_x == 0:
                tile[y * SIDE + x] = 1
            elif ((x // 8 + y // 8 + variant) & 1) == 0:
                tile[y * SIDE + x] = 3
    line(tile, 5 + variant * 4, 4, 13 + variant * 2, 16, 6)
    line(tile, 13 + variant * 2, 16, 9 + variant * 3, 29, 6)
    ellipse(tile, 25 if variant & 1 else 7, 25, 4, 2, 5)
    return tile


def grass(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    ellipse(tile, 7 + variant * 5, 8 + (variant & 1) * 14, 8, 5, 1)
    ellipse(tile, 25 - variant * 3, 23 - (variant & 1) * 13, 10, 6, 3)
    for blade in range(6):
        x = (variant * 9 + blade * 7 + 3) % 29 + 1
        y = (variant * 5 + blade * 11 + 4) % 27 + 2
        tile[y * SIDE + x] = 5
        tile[(y - 1) * SIDE + min(SIDE - 1, x + 1)] = 4
    return tile


def farmland(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    for row in range(-4, 36, 7):
        line(tile, 0, row + variant, 31, row + 10 + variant, 1)
        line(tile, 0, row + 2 + variant, 31, row + 12 + variant, 3)
    for sprout in range(7):
        x = (variant * 5 + sprout * 8 + 4) % 29 + 1
        y = (variant * 11 + sprout * 7 + 5) % 27 + 2
        tile[y * SIDE + x] = 6
        tile[(y - 1) * SIDE + x] = 5
    return tile


def bamboo(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    ellipse(tile, 9 + variant * 4, 10 + (variant & 1) * 12, 10, 7, 1)
    for stalk in range(6):
        x = (variant * 7 + stalk * 9 + 3) % 27 + 2
        y = (variant * 11 + stalk * 5 + 8) % 22 + 5
        line(tile, x - 2, y + 2, x + 2, y - 2, 6 if stalk & 1 else 5)
        tile[y * SIDE + x] = 7
    speckles(tile, variant, 4, 6, 5)
    return tile


def paved(variant: int) -> list[int]:
    tile = [2] * (SIDE * SIDE)
    offset = (variant & 1) * 4
    for y in range(SIDE):
        for x in range(SIDE):
            row = y // 8
            seam_x = (x + (row & 1) * 8 + offset) % 16
            if y % 8 == 0 or seam_x == 0:
                tile[y * SIDE + x] = 1
            elif ((x // 8 + y // 8 + variant) & 1) == 0:
                tile[y * SIDE + x] = 3
    ellipse(tile, 6 + variant * 6, 24 - variant * 4, 4, 2, 4)
    return tile


Palette = tuple[tuple[int, int, int], ...]
Recipe = tuple[str, Callable[[int], list[int]], Palette]


RECIPES: tuple[Recipe, ...] = (
    ("water", water, ((0,0,0),(19,54,73),(27,76,99),(35,98,122),(53,121,143),(86,151,164),(141,188,191),(31,67,82),(22,62,86),(44,109,129),(74,136,151),(108,166,174),(18,47,67),(54,89,101),(102,142,147),(174,207,201))),
    ("road", road, ((0,0,0),(76,54,34),(103,72,42),(126,90,49),(149,111,61),(176,139,82),(73,88,54),(166,154,112),(57,42,30),(89,63,38),(115,82,45),(139,104,61),(66,76,48),(126,116,76),(188,162,104),(104,91,70))),
    ("forest", forest, ((0,0,0),(22,57,36),(27,68,40),(34,80,43),(45,94,47),(62,108,52),(75,54,34),(96,67,39),(19,45,31),(39,73,38),(53,87,43),(81,101,55),(37,78,45),(48,94,49),(68,116,58),(109,91,58))),
    ("mountain", mountain, ((0,0,0),(47,50,51),(67,70,69),(84,86,82),(102,101,92),(119,115,101),(59,63,65),(137,130,109),(35,39,42),(75,79,78),(96,95,85),(150,141,116),(54,61,58),(112,106,92),(164,153,126),(92,82,70))),
    ("sand", sand, ((0,0,0),(117,88,48),(151,112,58),(180,139,78),(204,168,99),(222,192,123),(145,104,55),(234,210,151),(92,68,40),(166,124,66),(194,151,84),(211,181,113),(126,93,49),(181,141,77),(238,218,164),(160,118,65))),
    ("snow", snow, ((0,0,0),(154,174,181),(192,207,210),(220,229,228),(235,240,235),(250,250,242),(116,146,162),(255,255,252),(105,129,145),(177,195,202),(208,219,220),(238,243,240),(137,159,171),(201,214,216),(245,248,245),(170,188,195))),
    ("swamp", swamp, ((0,0,0),(35,61,48),(50,75,52),(63,88,54),(77,101,57),(91,113,62),(112,89,47),(128,107,58),(28,50,42),(45,71,48),(66,91,52),(102,116,69),(52,69,43),(76,94,49),(118,129,72),(92,79,48))),
    ("hills", hills, ((0,0,0),(45,78,42),(58,96,49),(72,112,56),(88,127,61),(106,141,69),(90,82,48),(132,150,81),(34,63,36),(64,103,48),(82,119,56),(113,135,75),(49,87,42),(73,111,52),(121,147,72),(103,96,58))),
    ("ruins", ruins, ((0,0,0),(62,68,67),(86,91,87),(105,108,101),(124,125,113),(73,83,78),(142,139,121),(57,73,62),(70,91,65),(91,107,74),(48,76,49),(67,92,58),(101,119,80),(44,52,54),(158,151,125),(112,96,73))),
    ("grass", grass, ((0,0,0),(37,78,45),(43,91,50),(55,105,55),(72,119,60),(84,132,65),(70,91,73),(92,104,91),(28,61,38),(49,97,45),(64,111,51),(104,113,95),(31,68,41),(48,94,49),(68,116,58),(121,124,103))),
    ("farmland", farmland, ((0,0,0),(73,48,30),(103,68,36),(129,88,43),(151,110,54),(173,134,70),(74,112,49),(102,137,61),(55,37,26),(91,60,32),(119,80,39),(145,101,49),(62,91,43),(91,123,54),(185,153,87),(122,91,53))),
    ("bamboo", bamboo, ((0,0,0),(29,65,39),(39,84,44),(50,101,49),(64,119,53),(82,138,61),(113,90,43),(137,111,49),(23,51,34),(45,91,43),(62,112,50),(97,132,62),(39,75,38),(57,101,45),(106,146,65),(121,101,51))),
    ("paved", paved, ((0,0,0),(72,78,77),(94,100,96),(113,118,111),(132,135,124),(84,91,88),(151,151,135),(65,79,71),(78,97,71),(99,113,81),(55,82,54),(73,98,63),(111,126,88),(52,61,63),(169,165,143),(121,104,79))),
)


def pack(tile: list[int]) -> list[int]:
    return [sum(tile[offset + nibble] << (nibble * 4) for nibble in range(4))
            for offset in range(0, len(tile), 4)]


def colour15(colour: tuple[int, int, int]) -> int:
    red, green, blue = (channel >> 3 for channel in colour)
    return red | green << 5 | blue << 10


def validate(cells: list[list[int]]) -> None:
    expected = len(RECIPES) * VARIANTS
    if len(cells) != expected or any(len(cell) != SIDE * SIDE for cell in cells):
        raise ValueError("the PS1 terrain pack does not contain four complete cells per kind")
    if len({tuple(cell) for cell in cells}) != expected:
        raise ValueError("every PS1 terrain kind and variation must have a distinct cell")
    for cell in cells:
        if any(index <= 0 or index >= 16 for index in cell):
            raise ValueError("opaque terrain texels must use CLUT entries 1 through 15")
    palettes = [palette for _, _, palette in RECIPES]
    if len({palette for palette in palettes}) != len(palettes):
        raise ValueError("every portable terrain kind must have a distinct PS1 CLUT")
    for palette in palettes:
        if len(palette) != 16 or palette[0] != (0, 0, 0):
            raise ValueError("every terrain CLUT must have 16 entries and transparent index zero")


def generate() -> str:
    cells = [recipe(variant) for _, recipe, _ in RECIPES for variant in range(VARIANTS)]
    validate(cells)
    lines = [
        "// Generated by platform/playstation/tools/generate-terrain-quality.py.",
        "// Do not edit. Deterministic PS1-native CI4 terrain data.",
        "#pragma once",
        f"inline constexpr int grandleon_psx_quality_terrain_side = {SIDE};",
        f"inline constexpr int grandleon_psx_quality_terrain_kind_count = {len(RECIPES)};",
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
        f"[{len(RECIPES)}][16] = {{"
    )
    for _, _, palette in RECIPES:
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
