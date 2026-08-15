# SPDX-License-Identifier: MIT
"""Height-field shading: the shared look of every terrain surface.

A terrain is described as a periodic *height field* rather than as a colour
pattern. The field is lit once, here, by the single global light direction, and
only then quantised onto that terrain's ramp. Two things follow:

* every surface in the game agrees about where the light comes from, which is
  most of what separates "rendered" from "coloured squares"; and
* because the field is periodic and the gradient is taken with wrapping, the
  lighting is continuous across a tile seam, not merely the colour.

Adding a terrain surface is therefore mostly a question of writing a height
field, not of writing a renderer.
"""

from __future__ import annotations

import math
from typing import Optional, Sequence

from .noise import Field
from .palette import Ramp
from .raster import Canvas, LIGHT, clamp, pick


def shade_field(
    canvas: Canvas,
    height: Field,
    ramp: Ramp,
    *,
    relief: float = 6.0,
    ambient: float = 0.30,
    height_gain: float = 0.35,
    bias: float = 0.0,
    mask: Optional[Sequence[int]] = None,
) -> None:
    """Light ``height`` and paint it onto ``canvas`` using ``ramp``.

    ``relief`` scales the surface slope: low values look like packed ground,
    high values like rock. ``height_gain`` adds raw altitude to the tone, which
    keeps peaks reading as peaks even where they face away from the light.
    """
    for y in range(canvas.height):
        for x in range(canvas.width):
            if mask is not None and not mask[y * canvas.width + x]:
                continue
            centre = height.at(x, y)
            gx = (height.at(x + 1, y) - height.at(x - 1, y)) * 0.5 * relief
            gy = (height.at(x, y + 1) - height.at(x, y - 1)) * 0.5 * relief
            length = math.sqrt(gx * gx + gy * gy + 1.0)
            lambert = clamp(
                (-gx * LIGHT[0] - gy * LIGHT[1] + LIGHT[2]) / length
            )
            tone = ambient + (1.0 - ambient) * lambert + height_gain * (centre - 0.5) + bias
            canvas.put(x, y, pick(ramp, tone, x, y))


def sine_field(width: int, height: int, freq_x: int, freq_y: int, phase: float = 0.0,
               warp: Optional[Field] = None, warp_amount: float = 0.0) -> Field:
    """A periodic sinusoid, optionally domain-warped by another periodic field.

    ``freq_x`` and ``freq_y`` are whole cycles across the tile, so the result
    stays periodic; the warp is what stops waves and dune ripples from looking
    like a ruler drew them.
    """
    values = []
    for y in range(height):
        for x in range(width):
            offset = 0.0
            if warp is not None:
                offset = (warp.at(x, y) - 0.5) * warp_amount
            angle = 2.0 * math.pi * (
                (x / width) * freq_x + (y / height) * freq_y + phase + offset
            )
            values.append(0.5 + 0.5 * math.sin(angle))
    return Field(width, height, values)


def blend(first: Field, second: Field, amount: float) -> Field:
    """Linear blend of two same-sized periodic fields."""
    assert first.width == second.width and first.height == second.height
    return Field(
        first.width,
        first.height,
        [a * (1.0 - amount) + b * amount for a, b in zip(first.values, second.values)],
    )


def scaled(field: Field, gain: float, offset: float = 0.0) -> Field:
    return Field(field.width, field.height, [clamp(v * gain + offset) for v in field.values])


def posterise(field: Field, steps: int, smoothing: float = 0.0) -> Field:
    """Quantise a field into flat terraces.

    Shading the gradient of a terraced field produces crisp lit and shadowed
    faces at every step, which is how rock strata and cliff faces get their
    read. ``smoothing`` mixes a little of the original field back in so the
    terraces are not perfectly flat.
    """
    values = []
    for value in field.values:
        stepped = math.floor(clamp(value) * steps) / max(1, steps - 1)
        values.append(clamp(stepped * (1.0 - smoothing) + value * smoothing))
    return Field(field.width, field.height, values)


def threshold(field: Field, level: float) -> bytearray:
    """Binary mask of the field above ``level``."""
    return bytearray(1 if v > level else 0 for v in field.values)
