#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render the pinned Spleen 6x12 bitmap into the console's ASCII atlas."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / 'assets/fonts/spleen'


def main() -> None:
    lines = (FONT / 'spleen-6x12.bdf').read_text().splitlines()
    ascent = int(next(line.split()[1] for line in lines if line.startswith('FONT_ASCENT ')))
    atlas = Image.new('RGBA', (64, 96), (0, 0, 0, 0))
    found = set()
    index = 0
    while index < len(lines):
        if not lines[index].startswith('STARTCHAR '):
            index += 1
            continue
        end = lines.index('ENDCHAR', index)
        record = lines[index:end]
        code = int(next(line.split()[1] for line in record if line.startswith('ENCODING ')))
        if 32 <= code < 96:
            width, height, left, bottom = map(int, next(line for line in record if line.startswith('BBX ')).split()[1:])
            assert width <= 6 and height <= 12
            top = ascent - bottom - height
            slot = code - 32
            x, y = slot % 8 * 8 + 1 + left, slot // 8 * 12 + top
            bitmap = record.index('BITMAP') + 1
            for row, value in enumerate(record[bitmap:bitmap + height]):
                bits = int(value, 16)
                stride = (width + 7) // 8 * 8
                for column in range(width):
                    if bits & (1 << (stride - 1 - column)):
                        assert slot % 8 * 8 <= x + column < slot % 8 * 8 + 7
                        assert slot // 8 * 12 <= y + row < (slot // 8 + 1) * 12
                        atlas.putpixel((x + column, y + row), (255, 255, 255, 255))
            found.add(code)
        index = end + 1
    assert found == set(range(32, 96))
    atlas.save(FONT / 'console-6x12.png', optimize=True)


if __name__ == '__main__':
    main()
