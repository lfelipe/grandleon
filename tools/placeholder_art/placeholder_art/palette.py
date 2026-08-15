# SPDX-License-Identifier: MIT
"""The fixed master palette.

**Committed palette size: 124 entries.** Index 0 is fully transparent and the
remaining 123 are opaque colours. The size fits a Nintendo 64 CI8 texture with
room to spare, leaves an obvious 16-colour subset selection path for CI4, and
is small enough that every asset shares one palette, which is what makes
cross-asset colour consistency automatic rather than a review chore.

It was 64 until the faction colour menu needed six ramps rather than two, and
80 until the biome and season themes needed a recoloured ground ramp per theme
(see :mod:`.themes`). Every new ramp is appended after every ramp that existed,
deliberately: ramp order is palette index order, so appending is what lets an
existing asset keep every index it already had, and therefore every rendered
pixel.

Colours are organised into named *ramps*: ordered runs from dark to light that
share a hue family. Renderers never name a colour directly; they pick a
position along a ramp from a lighting term, which is what keeps the light
direction consistent across every asset (see :mod:`.raster`).

The grass, water, road, forest, mountain, sand, snow, and swamp base colours
are the same values the editor already uses in
``editor/src/domain/terrain-presentation.ts``, so generated tiles agree with
the flat colours the map editor draws today.

Adding a colour
---------------
Append it to an existing ramp (preferred) or add a new ramp to ``_RAMP_ORDER``
and ``RAMPS``. ``PALETTE_SIZE`` is asserted at import time; if you exceed the
committed size you must either drop a colour or raise that size and update the
Nintendo 64 notes in ``README.md``. Append rather than insert: inserting
renumbers every later index and rewrites assets that had no reason to change.
"""

from __future__ import annotations

from typing import Dict, Tuple

Rgb = Tuple[int, int, int]
Ramp = Tuple[int, ...]

PALETTE_SIZE = 124
TRANSPARENT = 0

# Ramp definitions, dark to light. Order here defines palette index order.
_RAMP_COLOURS: Dict[str, Tuple[Rgb, ...]] = {
    # Neutral ink: outlines, deep shadow, and paper-white highlights.
    "ink": ((0x10, 0x13, 0x18), (0x23, 0x2A, 0x33), (0x59, 0x63, 0x6E), (0xF2, 0xF5, 0xF7)),
    "grass": (
        (0x2E, 0x5C, 0x33),
        (0x3B, 0x71, 0x39),
        (0x4D, 0x91, 0x47),
        (0x62, 0xA8, 0x52),
        (0x83, 0xC2, 0x68),
    ),
    "foliage": (
        (0x14, 0x36, 0x1F),
        (0x1C, 0x4A, 0x2A),
        (0x27, 0x61, 0x3A),
        (0x35, 0x78, 0x4A),
        (0x4C, 0x95, 0x60),
    ),
    "water": (
        (0x12, 0x38, 0x4F),
        (0x1B, 0x56, 0x6F),
        (0x25, 0x6D, 0x8C),
        (0x31, 0x82, 0xA4),
        (0x63, 0xB6, 0xCF),
    ),
    "sand": ((0x9C, 0x7F, 0x45), (0xBF, 0xA0, 0x57), (0xD4, 0xB8, 0x6B), (0xEA, 0xD3, 0x8F)),
    "snow": ((0xA8, 0xC2, 0xCE), (0xC9, 0xDE, 0xE5), (0xEE, 0xF7, 0xFA)),
    "rock": ((0x40, 0x38, 0x2F), (0x5B, 0x51, 0x45), (0x75, 0x69, 0x5D), (0x94, 0x87, 0x79)),
    "dirt": ((0x5E, 0x4A, 0x2C), (0x7D, 0x64, 0x40), (0xA8, 0x87, 0x53), (0xC4, 0xA6, 0x74)),
    "muck": ((0x3B, 0x44, 0x23), (0x52, 0x60, 0x2F), (0x65, 0x73, 0x3B)),
    "wood": ((0x4A, 0x34, 0x21), (0x66, 0x49, 0x2E), (0x8A, 0x6A, 0x44)),
    "skin": ((0x7A, 0x4A, 0x33), (0xC9, 0x8A, 0x63), (0xED, 0xC0, 0xA0)),
    "steel": ((0x4A, 0x55, 0x60), (0x73, 0x7F, 0x8B), (0xA3, 0xAE, 0xB8), (0xD8, 0xE0, 0xE6)),
    # The first two faction ramps. The third step is the exact colour the
    # editor's tactical board already uses for each side, so a sprite and its
    # board highlight agree: #2375a9 for the first side, #b3483f for the
    # second. The other four are at the end of the palette, not here, so that
    # adding them left every earlier index alone.
    "blue": ((0x0F, 0x33, 0x50), (0x17, 0x54, 0x7A), (0x23, 0x75, 0xA9), (0x55, 0xA7, 0xD8)),
    "red": ((0x4D, 0x1A, 0x17), (0x7D, 0x2C, 0x26), (0xB3, 0x48, 0x3F), (0xE0, 0x8A, 0x7E)),
    "leather": ((0x3C, 0x2A, 0x1C), (0x5C, 0x42, 0x27), (0x85, 0x60, 0x39)),
    "gold": ((0xB8, 0x86, 0x2B), (0xF0, 0xCF, 0x6A)),
    "hide": ((0x2B, 0x1F, 0x26), (0x4A, 0x35, 0x43), (0x6E, 0x50, 0x63)),
    # The rest of the faction colour menu, four steps each like blue and red
    # so every archetype's shading code works unchanged whichever colour a
    # faction picks. Each is separated from the terrain ramp nearest its hue,
    # green from grass and foliage, amber from sand and dirt, bone from rock
    # and snow, because a unit that reads as ground is not a unit.
    "green": ((0x12, 0x3A, 0x22), (0x1B, 0x5C, 0x33), (0x2C, 0x8A, 0x48), (0x64, 0xC0, 0x7E)),
    "violet": ((0x2C, 0x1B, 0x45), (0x46, 0x2A, 0x6E), (0x6B, 0x42, 0xA3), (0xA8, 0x86, 0xD6)),
    "amber": ((0x4E, 0x33, 0x0F), (0x7D, 0x55, 0x18), (0xBC, 0x83, 0x25), (0xE8, 0xBE, 0x62)),
    "bone": ((0x3A, 0x38, 0x30), (0x6B, 0x66, 0x59), (0x9E, 0x98, 0x86), (0xDC, 0xD6, 0xC2)),
    # The theme ramps. Each one replaces exactly one ramp above when a theme
    # asks for it (:mod:`.themes`), and holds exactly as many steps as the ramp
    # it replaces, so a themed tile is the same drawing in different colours
    # rather than a different drawing: every renderer picks the same ramp
    # position from the same lighting term either way.
    #
    # Autumn: a dry golden field and a russet canopy.
    "autumn_grass": (
        (0x4A, 0x52, 0x22),
        (0x63, 0x6B, 0x28),
        (0x86, 0x8B, 0x33),
        (0xA8, 0xA4, 0x42),
        (0xC6, 0xBE, 0x6A),
    ),
    "autumn_leaf": (
        (0x3A, 0x1A, 0x12),
        (0x5A, 0x28, 0x16),
        (0x7E, 0x3B, 0x1B),
        (0xA5, 0x55, 0x22),
        (0xC8, 0x7C, 0x33),
    ),
    # Winter: frost over the field, blue-green pine, water under ice.
    "winter_grass": (
        (0x5B, 0x6B, 0x63),
        (0x74, 0x86, 0x7C),
        (0x92, 0xA4, 0x99),
        (0xB0, 0xC0, 0xB6),
        (0xD2, 0xDE, 0xD6),
    ),
    "winter_pine": (
        (0x10, 0x28, 0x2A),
        (0x17, 0x3A, 0x3A),
        (0x20, 0x4F, 0x4C),
        (0x2E, 0x66, 0x60),
        (0x54, 0x8A, 0x82),
    ),
    "winter_water": (
        (0x2A, 0x4C, 0x60),
        (0x3C, 0x68, 0x80),
        (0x54, 0x88, 0xA2),
        (0x76, 0xAA, 0xC0),
        (0xA6, 0xD2, 0xE2),
    ),
    # Ashland: burnt scrub, charred canopy with an ember in the highlight,
    # standing water gone sulphurous, and ash where sand would be.
    "ash_scrub": (
        (0x2A, 0x2E, 0x2A),
        (0x3C, 0x42, 0x3A),
        (0x50, 0x57, 0x4A),
        (0x67, 0x6D, 0x5B),
        (0x82, 0x87, 0x71),
    ),
    "ash_char": (
        (0x1A, 0x16, 0x14),
        (0x2A, 0x22, 0x1E),
        (0x3C, 0x30, 0x28),
        (0x52, 0x40, 0x33),
        (0x7A, 0x4E, 0x2E),
    ),
    "ash_water": (
        (0x1C, 0x2C, 0x2A),
        (0x2A, 0x40, 0x3A),
        (0x3A, 0x56, 0x4A),
        (0x4E, 0x6E, 0x5C),
        (0x74, 0x94, 0x76),
    ),
    "ash_dust": ((0x55, 0x53, 0x52), (0x71, 0x6F, 0x6D), (0x8E, 0x8B, 0x88), (0xAF, 0xAB, 0xA6)),
}

_RAMP_ORDER = tuple(_RAMP_COLOURS)


def _build() -> Tuple[Tuple[Rgb, ...], Dict[str, Ramp]]:
    colours: list[Rgb] = [(0, 0, 0)]  # index 0, transparent; RGB is irrelevant.
    ramps: Dict[str, Ramp] = {}
    for name in _RAMP_ORDER:
        indices = []
        for colour in _RAMP_COLOURS[name]:
            indices.append(len(colours))
            colours.append(colour)
        ramps[name] = tuple(indices)
    return tuple(colours), ramps


RGB, RAMPS = _build()

assert len(RGB) == PALETTE_SIZE, f"palette holds {len(RGB)} entries, expected {PALETTE_SIZE}"
assert len(set(RGB[1:])) == PALETTE_SIZE - 1, "duplicate colour in palette"

#: For every palette index, the ramp it belongs to and its position in it.
#: Index 0 maps to ``("", 0)``. Used by :func:`shift` to darken or lighten a
#: pixel without leaving the palette.
RAMP_OF_INDEX: Tuple[Tuple[str, int], ...] = tuple(
    [("", 0)]
    + [
        (name, position)
        for name in _RAMP_ORDER
        for position in range(len(_RAMP_COLOURS[name]))
    ]
)


def ramp(name: str) -> Ramp:
    return RAMPS[name]


def shift(index: int, steps: int) -> int:
    """Move a palette index ``steps`` along its own ramp, clamped at the ends.

    This is how shadows and highlights are applied to already-painted pixels:
    it can never produce an off-palette colour, which is what keeps indexed
    output exact for every profile.
    """
    if index == TRANSPARENT:
        return TRANSPARENT
    name, position = RAMP_OF_INDEX[index]
    entries = RAMPS[name]
    return entries[max(0, min(len(entries) - 1, position + steps))]


def luminance(index: int) -> float:
    """Perceptual luminance in ``[0, 1]``; index 0 returns 0."""
    if index == TRANSPARENT:
        return 0.0
    red, green, blue = RGB[index]
    return (0.2126 * red + 0.7152 * green + 0.0722 * blue) / 255.0


def nearest(colour: Rgb, candidates: Tuple[int, ...]) -> int:
    """Return the candidate palette index closest to ``colour``.

    Distance is weighted sRGB, which tracks perception closely enough for a
    palette this size and is far cheaper than a proper colour space.
    """
    red, green, blue = colour
    best_index = candidates[0]
    best_distance = None
    for index in candidates:
        cr, cg, cb = RGB[index]
        distance = 2 * (red - cr) ** 2 + 4 * (green - cg) ** 2 + 3 * (blue - cb) ** 2
        if best_distance is None or distance < best_distance:
            best_distance = distance
            best_index = index
    return best_index
