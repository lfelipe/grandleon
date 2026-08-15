# SPDX-License-Identifier: MIT
"""Palette-index canvas and lit drawing primitives.

Everything is drawn as *palette indices*, never as RGB. Two consequences make
the whole pipeline work:

1. no drawing operation can invent an off-palette colour, so indexed output for
   the Nintendo 64 profiles is exact rather than approximate; and
2. shadows and highlights are expressed as movement along a ramp
   (:func:`palette.shift`), which keeps the light model consistent.

Lighting
--------
One light direction is used by every asset: from the upper left and slightly
toward the viewer, ``LIGHT``. Primitives approximate a surface normal and take
the Lambert term against it. Continuous tone is then quantised onto a ramp with
an ordered 8x8 Bayer matrix indexed by *tile-local* coordinates. Because every
tile dimension in this generator is a multiple of 8, the dither pattern is
continuous across tile boundaries instead of restarting at each seam.
"""

from __future__ import annotations

import math
from typing import Iterable, List, Optional, Sequence, Tuple

from . import palette
from .palette import Ramp

#: Light direction, normalised: upper-left, tilted toward the viewer.
LIGHT = (-0.5774, -0.5774, 0.5774)

#: Two-dimensional light direction, used for edge and rim shading.
LIGHT_2D = (-0.7071, -0.7071)

_BAYER8 = (
    (0, 32, 8, 40, 2, 34, 10, 42),
    (48, 16, 56, 24, 50, 18, 58, 26),
    (12, 44, 4, 36, 14, 46, 6, 38),
    (60, 28, 52, 20, 62, 30, 54, 22),
    (3, 35, 11, 43, 1, 33, 9, 41),
    (51, 19, 59, 27, 49, 17, 57, 25),
    (15, 47, 7, 39, 13, 45, 5, 37),
    (63, 31, 55, 23, 61, 29, 53, 21),
)


def bayer(x: int, y: int) -> float:
    """Ordered-dither threshold in ``[0, 1)`` for a pixel."""
    return _BAYER8[y & 7][x & 7] / 64.0


def clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
    return low if value < low else high if value > high else value


def pick(ramp: Ramp, tone: float, x: int, y: int, dither: bool = True) -> int:
    """Quantise ``tone`` in ``[0, 1]`` onto ``ramp``, ordered-dithering the gap.

    Dithering is what gives the textures their layered, non-flat read: instead
    of hard bands, adjacent ramp steps interleave in a stable pattern.
    """
    position = clamp(tone) * (len(ramp) - 1)
    low = int(position)
    if low >= len(ramp) - 1:
        return ramp[-1]
    if not dither:
        return ramp[low + 1] if position - low >= 0.5 else ramp[low]
    return ramp[low + 1] if position - low > bayer(x, y) else ramp[low]


class Canvas:
    """A rectangular grid of palette indices; index 0 is transparent.

    Set ``wrap`` while drawing anything that must survive being repeated: with
    wrapping on, a shape drawn near an edge reappears on the opposite edge, so
    the tile stays periodic.
    """

    __slots__ = ("width", "height", "data", "wrap")

    def __init__(self, width: int, height: int, fill: int = palette.TRANSPARENT) -> None:
        self.width = width
        self.height = height
        self.data = bytearray([fill]) * (width * height)
        self.wrap = False

    # -- basic access -----------------------------------------------------

    def put(self, x: int, y: int, index: int) -> None:
        if self.wrap:
            x %= self.width
            y %= self.height
        elif not (0 <= x < self.width and 0 <= y < self.height):
            return
        self.data[y * self.width + x] = index

    def get(self, x: int, y: int) -> int:
        if self.wrap:
            x %= self.width
            y %= self.height
        elif not (0 <= x < self.width and 0 <= y < self.height):
            return palette.TRANSPARENT
        return self.data[y * self.width + x]

    def fill(self, index: int) -> None:
        self.data = bytearray([index]) * (self.width * self.height)

    def copy(self) -> "Canvas":
        clone = Canvas(self.width, self.height)
        clone.data = bytearray(self.data)
        clone.wrap = self.wrap
        return clone

    def shift_pixel(self, x: int, y: int, steps: int) -> None:
        """Darken (negative) or lighten (positive) an existing pixel in place."""
        self.put(x, y, palette.shift(self.get(x, y), steps))

    # -- composition ------------------------------------------------------

    def blit(self, source: "Canvas", x: int, y: int) -> None:
        for sy in range(source.height):
            row = sy * source.width
            for sx in range(source.width):
                index = source.data[row + sx]
                if index != palette.TRANSPARENT:
                    self.put(x + sx, y + sy, index)

    def sub(self, x: int, y: int, width: int, height: int) -> "Canvas":
        out = Canvas(width, height)
        for row in range(height):
            source = (y + row) * self.width + x
            out.data[row * width:(row + 1) * width] = self.data[source:source + width]
        return out

    def upscale(self, factor: int) -> "Canvas":
        out = Canvas(self.width * factor, self.height * factor)
        for y in range(self.height):
            for repeat in range(factor):
                target = (y * factor + repeat) * out.width
                row = y * self.width
                for x in range(self.width):
                    index = self.data[row + x]
                    start = target + x * factor
                    for offset in range(factor):
                        out.data[start + offset] = index
        return out

    def tiled(self, columns: int, rows: int) -> "Canvas":
        """Repeat the canvas into a ``columns`` x ``rows`` grid (tiling proof)."""
        out = Canvas(self.width * columns, self.height * rows)
        for row in range(rows):
            for column in range(columns):
                out.blit_opaque(self, column * self.width, row * self.height)
        return out

    def blit_opaque(self, source: "Canvas", x: int, y: int) -> None:
        """Blit including transparent pixels (a straight copy)."""
        for sy in range(source.height):
            target_y = y + sy
            if not 0 <= target_y < self.height:
                continue
            row = sy * source.width
            base = target_y * self.width
            for sx in range(source.width):
                target_x = x + sx
                if 0 <= target_x < self.width:
                    self.data[base + target_x] = source.data[row + sx]

    def mask_with(self, alpha: Sequence[int]) -> None:
        """Clear every pixel whose ``alpha`` entry is falsy."""
        for i, keep in enumerate(alpha):
            if not keep:
                self.data[i] = palette.TRANSPARENT

    def to_rgba(self) -> List[Tuple[int, int, int, int]]:
        out: List[Tuple[int, int, int, int]] = []
        for index in self.data:
            if index == palette.TRANSPARENT:
                out.append((0, 0, 0, 0))
            else:
                red, green, blue = palette.RGB[index]
                out.append((red, green, blue, 255))
        return out


# -- shading helpers ------------------------------------------------------


def sphere_tone(dx: float, dy: float, radius: float, ambient: float = 0.30) -> Optional[float]:
    """Lambert term for a point on a sphere of ``radius`` offset by ``dx, dy``."""
    u = dx / radius
    v = dy / radius
    squared = u * u + v * v
    if squared > 1.0:
        return None
    nz = math.sqrt(1.0 - squared)
    lambert = clamp(u * LIGHT[0] + v * LIGHT[1] + nz * LIGHT[2])
    return ambient + (1.0 - ambient) * lambert


def edge_tone(nx: float, ny: float, ambient: float = 0.25) -> float:
    """Lambert term for a vertical face whose 2-D outward normal is ``nx, ny``."""
    length = math.hypot(nx, ny)
    if length < 1e-6:
        return ambient + 0.5 * (1.0 - ambient)
    lambert = clamp(0.5 + 0.5 * ((nx / length) * LIGHT_2D[0] + (ny / length) * LIGHT_2D[1]))
    return ambient + (1.0 - ambient) * lambert


# -- primitives -----------------------------------------------------------


def disc(canvas: Canvas, cx: float, cy: float, radius: float, ramp: Ramp,
         ambient: float = 0.30, tone_bias: float = 0.0, squash: float = 1.0) -> None:
    """A lit sphere. ``squash`` > 1 flattens it vertically into an ellipsoid."""
    left = int(math.floor(cx - radius))
    right = int(math.ceil(cx + radius))
    top = int(math.floor(cy - radius / squash))
    bottom = int(math.ceil(cy + radius / squash))
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            tone = sphere_tone(x + 0.5 - cx, (y + 0.5 - cy) * squash, radius, ambient)
            if tone is None:
                continue
            canvas.put(x, y, pick(ramp, tone + tone_bias, x, y))


def capsule(canvas: Canvas, x0: float, y0: float, x1: float, y1: float, radius: float,
            ramp: Ramp, ambient: float = 0.32, tone_bias: float = 0.0) -> None:
    """A lit cylinder with rounded ends: limbs, staves, trunks, and branches."""
    dx = x1 - x0
    dy = y1 - y0
    length_squared = dx * dx + dy * dy
    left = int(math.floor(min(x0, x1) - radius))
    right = int(math.ceil(max(x0, x1) + radius))
    top = int(math.floor(min(y0, y1) - radius))
    bottom = int(math.ceil(max(y0, y1) + radius))
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            px = x + 0.5 - x0
            py = y + 0.5 - y0
            t = 0.0 if length_squared == 0 else clamp((px * dx + py * dy) / length_squared)
            offset_x = px - t * dx
            offset_y = py - t * dy
            distance = math.hypot(offset_x, offset_y)
            if distance > radius:
                continue
            tone = sphere_tone(offset_x, offset_y, radius, ambient)
            if tone is None:
                continue
            canvas.put(x, y, pick(ramp, tone + tone_bias, x, y))


def polygon(canvas: Canvas, points: Sequence[Tuple[float, float]], ramp: Ramp,
            tone: float, dither: bool = True) -> None:
    """Flat-shaded convex or concave polygon, even-odd filled."""
    if len(points) < 3:
        return
    ys = [p[1] for p in points]
    for y in range(int(math.floor(min(ys))), int(math.ceil(max(ys))) + 1):
        scan = y + 0.5
        crossings: List[float] = []
        for i in range(len(points)):
            ax, ay = points[i]
            bx, by = points[(i + 1) % len(points)]
            if (ay <= scan < by) or (by <= scan < ay):
                crossings.append(ax + (scan - ay) / (by - ay) * (bx - ax))
        crossings.sort()
        for i in range(0, len(crossings) - 1, 2):
            for x in range(int(math.floor(crossings[i])), int(math.ceil(crossings[i + 1]))):
                if crossings[i] <= x + 0.5 <= crossings[i + 1]:
                    canvas.put(x, y, pick(ramp, tone, x, y, dither))


def rect(canvas: Canvas, x: int, y: int, width: int, height: int, index: int) -> None:
    for row in range(y, y + height):
        for column in range(x, x + width):
            canvas.put(column, row, index)


def line(canvas: Canvas, x0: int, y0: int, x1: int, y1: int, index: int) -> None:
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    step_x = 1 if x0 < x1 else -1
    step_y = 1 if y0 < y1 else -1
    error = dx + dy
    while True:
        canvas.put(x0, y0, index)
        if x0 == x1 and y0 == y1:
            return
        doubled = 2 * error
        if doubled >= dy:
            error += dy
            x0 += step_x
        if doubled <= dx:
            error += dx
            y0 += step_y


def outline(canvas: Canvas, index: int, diagonal: bool = False) -> None:
    """Trace a one-pixel border around every opaque region.

    This is the single biggest contributor to a readable silhouette at 32x32,
    and it is what keeps sprites legible once they are quantised down to
    sixteen colours or to four tones.
    """
    neighbours = ((1, 0), (-1, 0), (0, 1), (0, -1))
    if diagonal:
        neighbours += ((1, 1), (1, -1), (-1, 1), (-1, -1))
    additions: List[int] = []
    for y in range(canvas.height):
        for x in range(canvas.width):
            if canvas.data[y * canvas.width + x] != palette.TRANSPARENT:
                continue
            for offset_x, offset_y in neighbours:
                nx, ny = x + offset_x, y + offset_y
                if 0 <= nx < canvas.width and 0 <= ny < canvas.height:
                    if canvas.data[ny * canvas.width + nx] != palette.TRANSPARENT:
                        additions.append(y * canvas.width + x)
                        break
    for position in additions:
        canvas.data[position] = index


def ground_shadow(canvas: Canvas, cx: float, cy: float, radius_x: float, radius_y: float,
                  index: int) -> None:
    """A soft contact shadow, drawn before the subject so the subject sits on it."""
    for y in range(int(cy - radius_y) - 1, int(cy + radius_y) + 2):
        for x in range(int(cx - radius_x) - 1, int(cx + radius_x) + 2):
            u = (x + 0.5 - cx) / radius_x
            v = (y + 0.5 - cy) / radius_y
            if u * u + v * v <= 1.0:
                canvas.put(x, y, index)


def scatter(canvas: Canvas, positions: Iterable[Tuple[int, int]], index: int) -> None:
    for x, y in positions:
        canvas.put(x, y, index)
