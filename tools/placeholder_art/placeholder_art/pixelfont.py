# SPDX-License-Identifier: MIT
"""A 3x5 bitmap font for gallery labels.

The gallery needs text baked into its contact sheets. Using a system font would
make output depend on which fonts happen to be installed, which is exactly the
kind of environment dependency this generator exists to avoid, so the glyphs
live here as source.

Unknown characters render as a blank cell rather than raising, so a label can
never fail a build.
"""

from __future__ import annotations

from typing import Dict, Tuple

from .raster import Canvas

GLYPH_WIDTH = 3
GLYPH_HEIGHT = 5
TRACKING = 1

_GLYPHS: Dict[str, Tuple[str, ...]] = {
    "A": ("010", "101", "111", "101", "101"),
    "B": ("110", "101", "110", "101", "110"),
    "C": ("011", "100", "100", "100", "011"),
    "D": ("110", "101", "101", "101", "110"),
    "E": ("111", "100", "110", "100", "111"),
    "F": ("111", "100", "110", "100", "100"),
    "G": ("011", "100", "101", "101", "011"),
    "H": ("101", "101", "111", "101", "101"),
    "I": ("111", "010", "010", "010", "111"),
    "J": ("001", "001", "001", "101", "010"),
    "K": ("101", "101", "110", "101", "101"),
    "L": ("100", "100", "100", "100", "111"),
    "M": ("101", "111", "111", "101", "101"),
    "N": ("101", "111", "111", "111", "101"),
    "O": ("010", "101", "101", "101", "010"),
    "P": ("110", "101", "110", "100", "100"),
    "Q": ("010", "101", "101", "111", "011"),
    "R": ("110", "101", "110", "101", "101"),
    "S": ("011", "100", "010", "001", "110"),
    "T": ("111", "010", "010", "010", "010"),
    "U": ("101", "101", "101", "101", "011"),
    "V": ("101", "101", "101", "101", "010"),
    "W": ("101", "101", "111", "111", "101"),
    "X": ("101", "101", "010", "101", "101"),
    "Y": ("101", "101", "010", "010", "010"),
    "Z": ("111", "001", "010", "100", "111"),
    "0": ("111", "101", "101", "101", "111"),
    "1": ("010", "110", "010", "010", "111"),
    "2": ("110", "001", "010", "100", "111"),
    "3": ("111", "001", "110", "001", "111"),
    "4": ("101", "101", "111", "001", "001"),
    "5": ("111", "100", "111", "001", "111"),
    "6": ("011", "100", "111", "101", "111"),
    "7": ("111", "001", "010", "010", "010"),
    "8": ("111", "101", "111", "101", "111"),
    "9": ("111", "101", "111", "001", "110"),
    " ": ("000", "000", "000", "000", "000"),
    "-": ("000", "000", "111", "000", "000"),
    "_": ("000", "000", "000", "000", "111"),
    ".": ("000", "000", "000", "000", "010"),
    ",": ("000", "000", "000", "010", "100"),
    ":": ("000", "010", "000", "010", "000"),
    "/": ("001", "001", "010", "100", "100"),
    "+": ("000", "010", "111", "010", "000"),
    "(": ("001", "010", "010", "010", "001"),
    ")": ("100", "010", "010", "010", "100"),
    "#": ("101", "111", "101", "111", "101"),
    "%": ("101", "001", "010", "100", "101"),
    "*": ("000", "101", "010", "101", "000"),
}


def measure(text: str, scale: int = 1) -> Tuple[int, int]:
    """Pixel size of ``text`` when drawn at ``scale``."""
    if not text:
        return (0, 0)
    width = len(text) * (GLYPH_WIDTH + TRACKING) - TRACKING
    return (width * scale, GLYPH_HEIGHT * scale)


def draw(canvas: Canvas, text: str, x: int, y: int, index: int, scale: int = 1,
         shadow: int = 0) -> None:
    """Draw ``text`` at ``x, y`` in palette colour ``index``.

    ``shadow``, if non-zero, first draws the same text one pixel down and right
    in that colour, which keeps labels legible over busy contact sheets.
    """
    if shadow:
        draw(canvas, text, x + scale, y + scale, shadow, scale)
    cursor = x
    for character in text.upper():
        glyph = _GLYPHS.get(character)
        if glyph is not None:
            for row, bits in enumerate(glyph):
                for column, bit in enumerate(bits):
                    if bit != "1":
                        continue
                    for dy in range(scale):
                        for dx in range(scale):
                            canvas.put(cursor + column * scale + dx,
                                       y + row * scale + dy, index)
        cursor += (GLYPH_WIDTH + TRACKING) * scale
