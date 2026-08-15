#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Derives the editor's logo assets from the project mark.

The source of truth is ``tools/placeholder_art/gallery/logo.png``, the
checked-in project mark produced by the placeholder-art generator. This script
derives two files under ``editor/public/``, which are committed so the editor
builds without Python or Pillow.

At header and favicon sizes the wordmark under the sword melts into noise, so
both assets carry the sword alone: the glyphs are separated by the widest
horizontal gap of empty rows, and everything above it is the sword. It is
padded to a square so browsers that force square favicons do not stretch it.

- ``logo.png``: the squared sword at source resolution, scaled down by CSS
  in the editor header; and
- ``favicon.png``: the same square resized to 64x64 with nearest-neighbour to
  keep the pixel art crisp.

Run from anywhere: ``python3 editor/scripts/generate-logo-assets.py``.
Requires Pillow (the same dependency as tools/placeholder_art).
"""

from pathlib import Path
import shutil

from PIL import Image

REPOSITORY = Path(__file__).resolve().parents[2]
SOURCE = REPOSITORY / "tools" / "placeholder_art" / "gallery" / "logo.png"
PUBLIC = REPOSITORY / "editor" / "public"
FAVICON_SIZE = 64


def rows_with_content(image: Image.Image) -> list[int]:
    alpha = image.getchannel("A")
    width, height = image.size
    data = alpha.load()
    return [
        y for y in range(height)
        if any(data[x, y] > 0 for x in range(width))
    ]


def sword_region(image: Image.Image) -> Image.Image:
    """Crops the glyph above the widest vertical gap: the sword, not the text."""
    rows = rows_with_content(image)
    gap_start, gap_size = rows[-1], 0
    for previous, current in zip(rows, rows[1:]):
        if current - previous > gap_size:
            gap_start, gap_size = previous, current - previous
    top = image.crop((0, 0, image.width, gap_start + 1))
    return top.crop(top.getbbox())


def squared(image: Image.Image, margin_fraction: float = 0.08) -> Image.Image:
    side = int(max(image.size) * (1 + 2 * margin_fraction))
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(
        image,
        ((side - image.width) // 2, (side - image.height) // 2)
    )
    return canvas


def main() -> None:
    PUBLIC.mkdir(parents=True, exist_ok=True)

    with Image.open(SOURCE) as source:
        sword = squared(sword_region(source.convert("RGBA")))
        sword.save(PUBLIC / "logo.png", optimize=True)
        favicon = sword.resize((FAVICON_SIZE, FAVICON_SIZE), Image.NEAREST)
        favicon.save(PUBLIC / "favicon.png", optimize=True)

    print(f"wrote {PUBLIC / 'logo.png'} ({sword.size}) and {PUBLIC / 'favicon.png'}")


if __name__ == "__main__":
    main()
