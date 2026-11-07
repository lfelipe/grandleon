#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate original, deterministic N64 terrain experiment assets.

The material atlas is greyscale because the RDP multiplies it by authored
terrain vertex colours. The prop atlas carries its own restrained palette and
uses binary alpha. Both are converted to CI4 by the pinned N64 toolchain.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[3]
OUTPUT = ROOT / "platform/nintendo64/prototype/assets/sevenfold_terrain"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def noise(x: int, y: int, salt: int) -> int:
    value = x * 73 + y * 151 + x * y * 19 + salt * 199
    value ^= value >> 7
    value *= 0x45D9F3B
    value ^= value >> 11
    return value & 0xFF


def material_atlas() -> Image.Image:
    image = Image.new("RGBA", (64, 64), (0, 0, 0, 255))
    pixels = image.load()
    for y in range(64):
        for x in range(64):
            region = (x // 32) + (y // 32) * 2
            u = x & 31
            v = y & 31
            grain = noise(u, v, region) % 25 - 12
            value = 216 + grain
            if region == 0:
                # Grass: clustered blades over a softly mottled base.
                value = 216 + grain // 3
                if noise(u // 2, v // 2, 7) % 19 < 3:
                    value = 238
                # Two-pixel diagonal blade marks survive a 320x240 board,
                # unlike single-texel noise which bilinear filtering erases.
                if (u + 2 * v) % 13 < 2 and (v + u // 3) % 7 < 3:
                    value = 174
            elif region == 1:
                # Paved/stone: offset blocks, mortar, cracks and moss flecks.
                offset = 8 if (v // 8) & 1 else 0
                mortar = v % 8 == 0 or (u + offset) % 16 == 0
                value = 224 + grain // 2
                if mortar:
                    value = 148
                elif (u * 3 + v * 5 + noise(u, v, 11)) % 53 == 0:
                    value = 181
                elif noise(u, v, 17) % 61 == 0:
                    value = 246
            elif region == 2:
                # Earth/sand/road: compacted bands and small pebbles.
                value = 214 + grain // 2
                if (u + 3 * v) % 23 == 0:
                    value = 244
                if noise(u, v, 23) % 47 < 2:
                    value = 173
            else:
                # Water/snow/swamp: quiet horizontal ripples with varied gaps.
                value = 220 + grain // 3
                if v in (5, 14, 24) and (u + v) % 5 != 0:
                    value = 248
                elif v in (6, 15, 25) and (u + 2 * v) % 7 != 0:
                    value = 165
            value = max(135, min(248, value))
            pixels[x, y] = (value, value, value, 255)
    return image


def prop_atlas() -> Image.Image:
    image = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    # Cell 0: grass tuft.
    for base_x, height, colour in (
        (7, 16, (58, 112, 55, 255)),
        (12, 22, (78, 139, 67, 255)),
        (17, 19, (102, 157, 76, 255)),
        (22, 14, (52, 103, 50, 255)),
        (26, 18, (87, 145, 66, 255)),
    ):
        draw.polygon(
            ((base_x - 3, 30), (base_x, 30 - height), (base_x + 2, 30)),
            fill=colour,
        )

    # Cell 1: low fern/shrub.
    shrub = (67, 124, 59, 255)
    highlight = (104, 158, 76, 255)
    draw.rectangle((46, 13, 49, 29), fill=(77, 72, 44, 255))
    for offset in range(0, 14, 3):
        draw.polygon(((48, 17 + offset), (35, 9 + offset), (46, 21 + offset)), fill=shrub)
        draw.polygon(((48, 17 + offset), (61, 8 + offset), (50, 22 + offset)), fill=highlight)

    # Cell 2: rock cluster.
    draw.polygon(((3, 58), (7, 43), (18, 38), (26, 48), (29, 61)), fill=(91, 87, 82, 255))
    draw.polygon(((7, 45), (18, 40), (23, 48), (12, 51)), fill=(145, 137, 125, 255))
    draw.polygon(((19, 53), (26, 48), (29, 61), (17, 61)), fill=(67, 65, 65, 255))
    draw.polygon(((2, 61), (9, 52), (15, 61)), fill=(119, 109, 92, 255))

    # Cell 3: compact, irregular broadleaf tree for map-edge depth.
    draw.rectangle((46, 48, 50, 63), fill=(76, 52, 35, 255))
    draw.rectangle((49, 49, 53, 63), fill=(104, 70, 40, 255))
    draw.polygon(((34, 52), (37, 40), (44, 34), (53, 36), (62, 45), (60, 55)), fill=(41, 85, 45, 255))
    draw.polygon(((36, 44), (39, 31), (48, 26), (58, 32), (62, 44), (54, 50)), fill=(55, 113, 53, 255))
    draw.polygon(((41, 34), (44, 22), (52, 18), (59, 27), (57, 38), (49, 41)), fill=(81, 143, 65, 255))
    draw.polygon(((38, 39), (42, 30), (48, 28), (46, 43)), fill=(99, 157, 72, 255))
    return image


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    material_path = OUTPUT / "material_atlas.png"
    prop_path = OUTPUT / "prop_atlas.png"
    material_atlas().save(material_path, optimize=True)
    prop_atlas().save(prop_path, optimize=True)
    generator = Path(__file__).resolve()
    manifest = {
        "format": "Grandleon N64 terrain experiment assets v1",
        "generated_by": str(generator.relative_to(ROOT)),
        "generator_sha256": sha256(generator),
        "assets": [
            {
                "path": material_path.name,
                "sha256": sha256(material_path),
                "width": 64,
                "height": 64,
                "format": "CI4",
                "regions": ["grass", "stone", "earth", "water"],
            },
            {
                "path": prop_path.name,
                "sha256": sha256(prop_path),
                "width": 64,
                "height": 64,
                "format": "CI4",
                "cells": ["grass-tuft", "shrub", "rocks", "tree"],
            },
        ],
    }
    (OUTPUT / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
