# SPDX-License-Identifier: MIT
"""Autotiling: the 47-tile blob convention and its coverage field.

Convention
==========
This generator uses the **47-tile blob set** driven by an eight-bit neighbour
mask. The 16-variant four-bit edge set was rejected because it cannot express
inner corners, which is exactly where a naive tileset produces the broken seams
this work exists to avoid.

Bit layout, most significant to least::

    bit 7  NW  128        128   1   2      NW  N  NE
    bit 6  W    64         64   *   4      W   *  E
    bit 5  SW   32         32  16   8      SW  S  SE
    bit 4  SE    8
    bit 3  S    16
    bit 2  E     4
    bit 1  NE    2
    bit 0  N     1

A bit is set when the neighbouring cell holds **the same terrain** as the
centre cell. Diagonal bits are only meaningful when both adjoining cardinal
bits are set: a north-east neighbour cannot influence the corner if either the
north or the east cell is a different terrain, because the corner is already
open. :func:`canonicalise` clears such bits, which collapses the 256 raw masks
onto 47 distinct tiles. :data:`MASK_TO_VARIANT` is the full 256-entry lookup a
renderer needs; it is also emitted into ``manifest.json``.

Coverage field
==============
Tile shapes are **not** authored per variant. Instead the 3x3 occupancy around
a cell is interpolated bilinearly between cell centres to produce a scalar
field ``f``, and the tile covers every pixel where ``f + jitter > threshold``.

That construction has the property this whole task turns on: ``f`` is a single
globally defined function of map position, so any two variants that can legally
sit next to each other agree exactly along their shared edge, whichever of the
47 tiles they are. There is no per-variant artwork to get wrong, and
:mod:`.verify` proves the agreement exhaustively rather than by inspection.

``jitter`` is periodic over one tile, so it too is globally consistent, and it
is what stops every boundary from being a mechanically straight line.
"""

from __future__ import annotations

import math
from typing import Callable, Dict, List, Sequence, Tuple

N = 1
NE = 2
E = 4
SE = 8
S = 16
SW = 32
W = 64
NW = 128

#: Cardinal bit to (dx, dy) offset.
CARDINALS: Dict[int, Tuple[int, int]] = {N: (0, -1), E: (1, 0), S: (0, 1), W: (-1, 0)}

#: Diagonal bit to (dx, dy) offset and the two cardinal bits that gate it.
DIAGONALS: Dict[int, Tuple[Tuple[int, int], int, int]] = {
    NE: ((1, -1), N, E),
    SE: ((1, 1), S, E),
    SW: ((-1, 1), S, W),
    NW: ((-1, -1), N, W),
}

BIT_NAMES = {N: "N", NE: "NE", E: "E", SE: "SE", S: "S", SW: "SW", W: "W", NW: "NW"}


def canonicalise(mask: int) -> int:
    """Clear diagonal bits whose adjoining cardinals are not both set."""
    result = mask & 0xFF
    for bit, (_, first, second) in DIAGONALS.items():
        if not (result & first and result & second):
            result &= ~bit
    return result


def mask_name(mask: int) -> str:
    """Human-readable bit list, e.g. ``N+E+NE``; ``none`` for an isolated cell."""
    parts = [BIT_NAMES[bit] for bit in (N, NE, E, SE, S, SW, W, NW) if mask & bit]
    return "+".join(parts) if parts else "none"


BLOB_MASKS: Tuple[int, ...] = tuple(sorted({canonicalise(m) for m in range(256)}))
assert len(BLOB_MASKS) == 47, f"expected 47 blob variants, found {len(BLOB_MASKS)}"

#: Raw 0-255 neighbour mask to variant index in ``BLOB_MASKS``.
MASK_TO_VARIANT: Tuple[int, ...] = tuple(
    BLOB_MASKS.index(canonicalise(m)) for m in range(256)
)

#: The variant a fully surrounded cell uses; interchangeable with a base tile.
INTERIOR_VARIANT = MASK_TO_VARIANT[0xFF]


def mask_from(same: Callable[[int, int], bool]) -> int:
    """Build a raw mask from a ``same(dx, dy)`` predicate over the 8 neighbours."""
    mask = 0
    for bit, (dx, dy) in CARDINALS.items():
        if same(dx, dy):
            mask |= bit
    for bit, ((dx, dy), _, _) in DIAGONALS.items():
        if same(dx, dy):
            mask |= bit
    return mask


def occupancy(mask: int) -> Dict[Tuple[int, int], float]:
    """The 3x3 same-terrain grid implied by ``mask``; the centre is always 1."""
    grid = {(0, 0): 1.0}
    for bit, offset in CARDINALS.items():
        grid[offset] = 1.0 if mask & bit else 0.0
    for bit, (offset, _, _) in DIAGONALS.items():
        grid[offset] = 1.0 if mask & bit else 0.0
    return grid


_coverage_cache: Dict[Tuple[int, int], List[float]] = {}


def coverage_field(mask: int, size: int) -> List[float]:
    """Bilinear interpolation of the 3x3 occupancy, sampled over one tile.

    Returns ``size * size`` values in ``[0, 1]``. A value of 1 is deep interior;
    0.5 falls exactly on the tile boundary.
    """
    key = (canonicalise(mask), size)
    cached = _coverage_cache.get(key)
    if cached is not None:
        return cached
    grid = occupancy(key[0])
    values: List[float] = [0.0] * (size * size)
    for y in range(size):
        v = (y + 0.5) / size - 0.5
        if v >= 0:
            ny, wy = 1, v
        else:
            ny, wy = -1, -v
        for x in range(size):
            u = (x + 0.5) / size - 0.5
            if u >= 0:
                nx, wx = 1, u
            else:
                nx, wx = -1, -u
            values[y * size + x] = (
                (1 - wx) * (1 - wy) * grid[(0, 0)]
                + wx * (1 - wy) * grid[(nx, 0)]
                + (1 - wx) * wy * grid[(0, ny)]
                + wx * wy * grid[(nx, ny)]
            )
    _coverage_cache[key] = values
    return values


def gradient(values: Sequence[float], size: int, x: int, y: int) -> Tuple[float, float]:
    """Central difference of a coverage field, clamped at the tile border."""
    def sample(sx: int, sy: int) -> float:
        return values[min(size - 1, max(0, sy)) * size + min(size - 1, max(0, sx))]

    return (
        (sample(x + 1, y) - sample(x - 1, y)) * 0.5,
        (sample(x, y + 1) - sample(x, y - 1)) * 0.5,
    )


class Coverage:
    """Per-pixel alpha and rim classification for one blob variant."""

    __slots__ = ("size", "alpha", "rim", "normals")

    def __init__(self, mask: int, size: int, jitter: Sequence[float], threshold: float,
                 rim_pixels: float) -> None:
        field = coverage_field(mask, size)
        band = rim_pixels / size
        self.size = size
        self.alpha = bytearray(size * size)
        self.rim = bytearray(size * size)
        self.normals: List[Tuple[float, float]] = [(0.0, 0.0)] * (size * size)
        for y in range(size):
            for x in range(size):
                position = y * size + x
                value = field[position] + jitter[position]
                if value <= threshold:
                    continue
                self.alpha[position] = 1
                if value <= threshold + band:
                    self.rim[position] = 1
                    gx, gy = gradient(field, size, x, y)
                    length = math.hypot(gx, gy)
                    if length > 1e-6:
                        # Outward normal points away from the terrain body.
                        self.normals[position] = (-gx / length, -gy / length)

    def is_full(self) -> bool:
        return all(self.alpha)
