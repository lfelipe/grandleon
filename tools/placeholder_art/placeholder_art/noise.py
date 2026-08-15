# SPDX-License-Identifier: MIT
"""Periodic value noise.

Every texture in this generator is built from noise that is *periodic over the
tile*. That single property is what makes a repeated tile seamless: the field
sampled at ``x = width`` is by construction the same value as at ``x = 0``, so
the right column joins the left column exactly.

The lattice count must divide the tile size for the period to land on the tile
boundary; :func:`fbm` asserts this rather than producing a subtly broken tile.
"""

from __future__ import annotations

from typing import List

from .rng import Rng, seed_of


class Field:
    """A ``width x height`` grid of floats in ``[0, 1]``, periodic over both axes."""

    __slots__ = ("width", "height", "values")

    def __init__(self, width: int, height: int, values: List[float]) -> None:
        self.width = width
        self.height = height
        self.values = values

    def at(self, x: int, y: int) -> float:
        return self.values[(y % self.height) * self.width + (x % self.width)]

    def remap(self, low: float, high: float) -> "Field":
        span = high - low
        return Field(self.width, self.height, [low + span * v for v in self.values])


def _smoothstep(t: float) -> float:
    return t * t * (3.0 - 2.0 * t)


def value_noise(width: int, height: int, cells_x: int, cells_y: int, seed: int) -> Field:
    """Interpolated value noise with ``cells_x`` x ``cells_y`` lattice cells.

    The lattice wraps modulo the cell counts, so the result is periodic over
    ``width`` x ``height``.
    """
    rng = Rng(seed)
    lattice = [rng.random() for _ in range(cells_x * cells_y)]
    values: List[float] = [0.0] * (width * height)
    scale_x = cells_x / width
    scale_y = cells_y / height
    for y in range(height):
        fy = y * scale_y
        y0 = int(fy) % cells_y
        y1 = (y0 + 1) % cells_y
        ty = _smoothstep(fy - int(fy))
        row0 = y0 * cells_x
        row1 = y1 * cells_x
        base = y * width
        for x in range(width):
            fx = x * scale_x
            x0 = int(fx) % cells_x
            x1 = (x0 + 1) % cells_x
            tx = _smoothstep(fx - int(fx))
            top = lattice[row0 + x0] * (1.0 - tx) + lattice[row0 + x1] * tx
            bottom = lattice[row1 + x0] * (1.0 - tx) + lattice[row1 + x1] * tx
            values[base + x] = top * (1.0 - ty) + bottom * ty
    return Field(width, height, values)


def fbm(
    width: int,
    height: int,
    base_cells: int,
    octaves: int,
    seed: int,
    gain: float = 0.5,
) -> Field:
    """Fractional Brownian motion: octaves of :func:`value_noise`, normalised.

    ``base_cells`` is the lattice count of the first octave along the *shorter*
    axis; each octave doubles it. The result is normalised to ``[0, 1]`` using
    the analytic amplitude sum, so two fields built with the same parameters
    but different seeds share a contrast range.
    """
    assert width % base_cells == 0 and height % base_cells == 0, (
        f"base_cells={base_cells} must divide the tile size {width}x{height} "
        "or the noise will not be periodic over the tile"
    )
    values = [0.0] * (width * height)
    amplitude = 1.0
    total = 0.0
    cells = base_cells
    for octave in range(octaves):
        if width % cells or height % cells:
            break
        layer = value_noise(width, height, cells, cells, seed_of(seed, "octave", octave))
        layer_values = layer.values
        for i in range(width * height):
            values[i] += layer_values[i] * amplitude
        total += amplitude
        amplitude *= gain
        cells *= 2
    inverse = 1.0 / total if total else 1.0
    return Field(width, height, [v * inverse for v in values])


def ridged(width: int, height: int, base_cells: int, octaves: int, seed: int) -> Field:
    """fBm folded around its midpoint, giving creases suitable for rock strata."""
    base = fbm(width, height, base_cells, octaves, seed)
    return Field(width, height, [1.0 - abs(v * 2.0 - 1.0) for v in base.values])
