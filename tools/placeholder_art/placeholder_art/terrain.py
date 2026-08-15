# SPDX-License-Identifier: MIT
"""Terrain definitions: the single source of truth for every ground surface.

Each terrain is one :class:`Terrain` subclass describing a *periodic height
field* plus optional decorative props. The same definition feeds every output
profile; nothing here knows about the Nintendo 64 or about any other target.

What each terrain produces
--------------------------
``base``        four seamless variants for interior fill.
``blob``        the 47-variant autotile set (see :mod:`.autotile`).
``over/<x>``    the same 47 variants with a rim styled for meeting terrain
                ``x``. These are the transition tiles: a shoreline drawn where
                water meets sand should be foam, and where it meets grass
                should be a reedy bank, and that difference is the whole point.

Adding a terrain type
---------------------
1. Subclass :class:`Terrain`, set ``name``, ``layer`` (compositing order, low
   is painted first), ``rim``, and the flat colour a client draws when it is
   not drawing tiles (``flat``, with the ramp position it stands for).
2. Implement :meth:`Terrain.paint_ground`: write a periodic height field and
   hand it to :func:`texture.shade_field`. Use only periodic noise from
   :mod:`.noise`, or the tile will not be seamless; :mod:`.verify` will fail
   the build if it is not.
3. Optionally implement :meth:`Terrain.paint_props`. Props are drawn with
   wrapping enabled, so a prop crossing an edge reappears on the far side and
   the tile stays periodic. Do not disable wrapping.
4. Add rim styles to ``rim_over`` for the neighbouring terrains where a
   deliberate transition matters.
5. Append the class to :data:`TERRAIN_CLASSES`.

Nothing else needs to change: profiles, sheets, the manifest, verification, and
the gallery all enumerate the registry, and every theme in :mod:`.themes`
recolours the new terrain the moment it exists, because a recipe asks for its
ramps by name rather than by palette index.

Themes
------
Every ramp a recipe paints with is fetched through :meth:`Terrain.ramp`, which
resolves it against the theme being rendered. Painting is otherwise
theme-blind: the height fields, prop placement, and rim geometry are identical
in every theme, so a themed sheet is the untinted one recoloured, tile for tile
and variant for variant.
"""

from __future__ import annotations

import math
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Dict, Iterator, List, Mapping, Optional, Tuple

from . import autotile, noise, raster, texture, themes
from .noise import Field
from .palette import RAMPS, RGB, Rgb
from .raster import Canvas, edge_tone, pick
from .rng import Rng, seed_of
from .texture import shade_field
from .themes import Theme

#: Native authoring tile size. Every profile derives from this.
TILE = 32

#: Number of interchangeable interior variants generated per terrain.
BASE_VARIANTS = 4

#: The theme currently being painted. Painting is a pure function of it and of
#: the recipe; :func:`rendering` is the only thing that moves it.
_ACTIVE: Theme = themes.DEFAULT_THEME


@contextmanager
def rendering(active: Theme) -> Iterator[Theme]:
    """Paint everything inside the block in ``active``'s colours."""
    global _ACTIVE
    previous = _ACTIVE
    _ACTIVE = active
    try:
        yield active
    finally:
        _ACTIVE = previous


def ramp(name: str) -> Tuple[int, ...]:
    """The palette indices painted for ramp ``name`` in the active theme."""
    return themes.ramp(_ACTIVE, name)


def snow_ramp() -> Tuple[int, ...]:
    """Snow gains a cold shadow step; the snow ramp on its own is all
    highlight and renders as a flat white field."""
    return (ramp("steel")[1],) + ramp("snow")


def rock_ramp() -> Tuple[int, ...]:
    """Rock gains a warm dark crevice step and a warm lit-face step. Both are
    borrowed from other ramps: the rock ramp alone spans too little contrast to
    shade a faceted boulder, and neutral greys read as ice rather than stone."""
    return (ramp("leather")[0],) + ramp("rock") + (ramp("sand")[0],)


@dataclass(frozen=True)
class Rim:
    """How a terrain finishes where it meets something else.

    ``ramp`` is sampled between ``low`` and ``high`` by the *lit* edge normal,
    so a boundary facing the light is bright and one facing away is dark
    without any per-tile authoring. ``inner_shift`` optionally shades the band
    immediately inside the rim, which reads as thickness.
    """

    ramp: str
    low: float
    high: float
    inner_shift: int = 0
    pixels: float = 2.0


class Terrain:
    """Base class for a terrain surface."""

    name: str = ""
    layer: int = 0
    rim: Rim = Rim("ink", 0.2, 0.8)
    rim_over: Mapping[str, Rim] = {}
    #: Boundary wobble, in units of the coverage field (0.5 is one half-tile).
    jitter: float = 0.11
    #: Coverage threshold; above 0.5 the terrain pulls back inside its cell.
    threshold: float = 0.50
    #: The single colour a client draws for this terrain when it is drawing
    #: cells rather than tiles, as the editor's map grid does. Held explicitly
    #: rather than read out of a ramp because it is the value that client
    #: already ships.
    flat: Rgb = (0x4D, 0x91, 0x47)
    #: The ramp position ``flat`` stands for. A theme that substitutes this
    #: ramp recolours the flat colour through the same substitution, so the
    #: cell view and the tile view of a themed map agree.
    flat_ramp: str = "grass"
    flat_step: int = 2
    #: The words an authored terrain name may contain to select this terrain.
    #: This is the whole selection mechanism: an author writes a terrain name,
    #: and the first terrain in :data:`KEYWORD_ORDER` whose keyword appears in
    #: the lowered name draws it. Adding a terrain here is what makes it
    #: reachable from the editor and from the console.
    keywords: Tuple[str, ...] = ()
    #: A one-character mark, so a client never carries terrain identity by
    #: colour alone. No two terrains may share one.
    glyph: str = "?"
    #: How many levels above the valley floor this terrain reads as, for the
    #: 2.5D presentation. This is data, not pixels: a client lifts the cell's art up the screen by
    #: this many steps and draws exactly the same tile. It is presentation
    #: only: no rule reads it, nothing hashes it, and a client that ignores
    #: it draws a flat board.
    #:
    #: Level ground is 0 and is the default, so a terrain added to the library
    #: is flat until someone says otherwise. Only the two the art already
    #: draws as high ground stand above it.
    elevation: int = 0

    def paint_ground(self, canvas: Canvas) -> None:
        raise NotImplementedError

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        """Draw wrapped decoration. Default is none."""

    # -- helpers available to subclasses ---------------------------------

    def ramp(self, name: str) -> Tuple[int, ...]:
        """The ramp this recipe paints with, resolved against the theme."""
        return ramp(name)

    def fbm(self, key: str, cells: int, octaves: int, gain: float = 0.5) -> Field:
        return noise.fbm(TILE, TILE, cells, octaves, seed_of(self.name, key), gain)

    def ridged(self, key: str, cells: int, octaves: int) -> Field:
        return noise.ridged(TILE, TILE, cells, octaves, seed_of(self.name, key))


# ---------------------------------------------------------------------------
# Shared prop helpers
# ---------------------------------------------------------------------------


def _tuft(canvas: Canvas, x: int, y: int, rng: Rng, colours: Tuple[int, ...]) -> None:
    """A few blades of grass, lit at the tips."""
    for offset in (-1, 0, 1):
        height = rng.randint(2, 4)
        lean = rng.randint(-1, 1)
        canvas.put(x + offset, y + 1, colours[0])
        canvas.put(x + offset, y, colours[1])
        for step in range(1, height):
            tip = step == height - 1
            canvas.put(
                x + offset + (lean if step > height // 2 else 0),
                y - step,
                colours[4 if tip else 3],
            )


def _pebble(canvas: Canvas, x: float, y: float, radius: float,
            colours: Tuple[int, ...]) -> None:
    raster.disc(canvas, x, y, radius, colours, ambient=0.28, squash=1.35)
    canvas.put(int(x + radius * 0.5), int(y + radius * 0.9), colours[0])


def _rock_chunk(canvas: Canvas, cx: float, cy: float, radius: float, rng: Rng,
                colours: Tuple[int, ...]) -> None:
    """A faceted boulder.

    The silhouette is an irregular polygon; each face is a triangle from an
    apex to one silhouette edge, shaded by that face's outward normal against
    the global light. Flat lit facets are what makes stone read as stone
    rather than as noise.
    """
    sides = rng.randint(5, 7)
    points = []
    for index in range(sides):
        angle = (index / sides) * 2.0 * math.pi + rng.uniform(-0.22, 0.22)
        distance = radius * rng.uniform(0.78, 1.0)
        points.append((cx + math.cos(angle) * distance,
                       cy + math.sin(angle) * distance * 0.85))
    apex = (cx + rng.uniform(-0.8, 0.4), cy - radius * rng.uniform(0.35, 0.65))
    raster.polygon(canvas, points, colours, 0.42, dither=False)
    for index in range(sides):
        first = points[index]
        second = points[(index + 1) % sides]
        mid_x = (first[0] + second[0]) * 0.5 - cx
        mid_y = (first[1] + second[1]) * 0.5 - cy
        raster.polygon(canvas, (apex, first, second), colours,
                       0.34 + 0.52 * edge_tone(mid_x, mid_y), dither=False)
    for index in range(sides):
        first = points[index]
        second = points[(index + 1) % sides]
        raster.line(canvas, int(first[0]), int(first[1]), int(second[0]),
                    int(second[1]), colours[0])


def _reed(canvas: Canvas, x: int, y: int, rng: Rng) -> None:
    height = rng.randint(3, 6)
    lean = rng.randint(-1, 1)
    foliage = ramp("foliage")
    for step in range(height):
        canvas.put(x + (lean if step > height // 2 else 0), y - step,
                   foliage[1 if step < height - 2 else 3])


# ---------------------------------------------------------------------------
# Terrain definitions
# ---------------------------------------------------------------------------


class Grass(Terrain):
    name = "grass"
    layer = 0
    rim = Rim("grass", 0.15, 0.80, inner_shift=-1)
    rim_over = {"sand": Rim("grass", 0.10, 0.65, inner_shift=-1)}
    flat = (0x4D, 0x91, 0x47)
    flat_ramp = "grass"
    flat_step = 2
    keywords = ("grass", "plain")
    glyph = "\""

    def paint_ground(self, canvas: Canvas) -> None:
        broad = self.fbm("broad", 2, 3)
        fine = self.fbm("fine", 8, 2)
        surface = texture.blend(broad, fine, 0.22)
        shade_field(canvas, surface, self.ramp("grass"), relief=7.0, ambient=0.30,
                    height_gain=0.70)

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(16 + variant * 2):
            _tuft(canvas, rng.randint(0, TILE - 1), rng.randint(0, TILE - 1), rng,
                  self.ramp("grass"))
        for _ in range(variant):
            canvas.put(rng.randint(0, TILE - 1), rng.randint(0, TILE - 1),
                       self.ramp("gold")[1])


class Forest(Terrain):
    name = "forest"
    layer = 5
    rim = Rim("foliage", 0.02, 0.45, inner_shift=-1, pixels=2.5)
    rim_over = {
        "grass": Rim("foliage", 0.00, 0.42, inner_shift=-1, pixels=2.5),
        "snow": Rim("foliage", 0.05, 0.55, inner_shift=-1, pixels=2.5),
    }
    jitter = 0.13
    flat = (0x27, 0x61, 0x3A)
    flat_ramp = "foliage"
    flat_step = 2
    keywords = ("forest", "wood")
    glyph = "♣"

    #: Crowns sit on a jittered 8-pixel grid so they wrap with the tile and
    #: still avoid the regularity of an unjittered lattice.
    CROWN_CELL = 8

    def paint_ground(self, canvas: Canvas) -> None:
        floor = self.fbm("floor", 8, 3)
        foliage = self.ramp("foliage")
        shade_field(canvas, floor, foliage, relief=4.0, ambient=0.24,
                    height_gain=0.25, bias=-0.35)
        for _, cx, cy, radius in self._crowns():
            # Cast shadow first, then the crown over it: light is upper left.
            raster.disc(canvas, cx + 1.8, cy + 2.2, radius * 0.95,
                        (foliage[0],), ambient=1.0)
            raster.disc(canvas, cx, cy, radius, foliage, ambient=0.22,
                        squash=1.12)

    def _crowns(self) -> List[Tuple[float, float, float, float]]:
        """Tree crowns as ``(sort key, x, y, radius)``, painted back to front."""
        crowns = []
        cells = TILE // self.CROWN_CELL
        for gy in range(cells):
            for gx in range(cells):
                rng = Rng(seed_of(self.name, "crown", gx, gy))
                cx = gx * self.CROWN_CELL + self.CROWN_CELL / 2 + rng.uniform(-2.2, 2.2)
                cy = gy * self.CROWN_CELL + self.CROWN_CELL / 2 + rng.uniform(-2.2, 2.2)
                crowns.append((cy, cx, cy, rng.uniform(3.6, 5.4)))
        crowns.sort()
        return crowns

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(variant):
            x = rng.uniform(0, TILE)
            y = rng.uniform(0, TILE)
            raster.disc(canvas, x, y, 2.0, self.ramp("foliage"), ambient=0.55,
                        tone_bias=0.30)


class Water(Terrain):
    name = "water"
    layer = 7
    rim = Rim("water", 0.55, 1.00, pixels=2.0)
    rim_over = {
        "sand": Rim("snow", 0.30, 1.00, pixels=2.0),
        "grass": Rim("foliage", 0.10, 0.55, inner_shift=-1, pixels=2.0),
        "swamp": Rim("muck", 0.05, 0.60, inner_shift=-1, pixels=2.0),
    }
    jitter = 0.09
    threshold = 0.52
    flat = (0x31, 0x82, 0xA4)
    flat_ramp = "water"
    flat_step = 3
    keywords = ("water", "river")
    glyph = "≈"

    def paint_ground(self, canvas: Canvas) -> None:
        warp = self.fbm("warp", 2, 3)
        swell = texture.sine_field(TILE, TILE, 0, 2, warp=warp, warp_amount=0.85)
        chop = texture.sine_field(TILE, TILE, 1, 4, phase=0.25, warp=warp,
                                  warp_amount=0.55)
        surface = texture.blend(swell, chop, 0.30)
        water = self.ramp("water")
        shade_field(canvas, surface, water, relief=6.0, ambient=0.40,
                    height_gain=0.45)
        crest = self.fbm("crest", 4, 2)
        for y in range(TILE):
            for x in range(TILE):
                if surface.at(x, y) > 0.80 and crest.at(x, y) > 0.52:
                    canvas.put(x, y, water[4])


class Mountain(Terrain):
    name = "mountain"
    layer = 6
    rim = Rim("rock", 0.00, 1.00, inner_shift=-1, pixels=3.0)
    rim_over = {
        "grass": Rim("rock", 0.00, 0.95, inner_shift=-1, pixels=3.0),
        "snow": Rim("rock", 0.05, 1.00, inner_shift=-1, pixels=3.0),
    }
    jitter = 0.14
    flat = (0x75, 0x69, 0x5D)
    flat_ramp = "rock"
    flat_step = 2
    keywords = ("mountain", "rock")
    glyph = "▲"
    #: The high ground of the library, two steps above the valley.
    elevation = 2

    def paint_ground(self, canvas: Canvas) -> None:
        scree = texture.blend(self.ridged("scree", 4, 3), self.fbm("grain", 8, 2), 0.30)
        rock = rock_ramp()
        shade_field(canvas, scree, rock, relief=8.0, ambient=0.26,
                    height_gain=0.40, bias=-0.10)
        for _, cx, cy, radius, seed in self._boulders():
            rng = Rng(seed)
            raster.disc(canvas, cx + 1.6, cy + 1.8, radius * 0.9,
                        (rock[0],), ambient=1.0)
            _rock_chunk(canvas, cx, cy, radius, rng, rock)

    def _boulders(self) -> List[Tuple[float, float, float, float, int]]:
        """Boulders as ``(sort key, x, y, radius, seed)``, painted back to front.

        Two jittered grids are used: a coarse one for masses that give the tile
        its silhouette, and a finer one for rubble that fills the gaps.
        """
        boulders = []
        for cell, low, high, key in ((16, 6.5, 9.0, "mass"), (16, 2.4, 4.0, "rubble")):
            for gy in range(TILE // cell):
                for gx in range(TILE // cell):
                    seed = seed_of(self.name, key, gx, gy)
                    rng = Rng(seed)
                    cx = gx * cell + cell / 2 + rng.uniform(-cell / 3, cell / 3)
                    cy = gy * cell + cell / 2 + rng.uniform(-cell / 3, cell / 3)
                    boulders.append((cy + (0.0 if key == "mass" else 0.5), cx, cy,
                                     rng.uniform(low, high), seed_of(seed, "shape")))
        boulders.sort()
        return boulders

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(variant):
            _pebble(canvas, rng.uniform(0, TILE), rng.uniform(0, TILE),
                    rng.uniform(1.2, 2.0), self.ramp("rock"))


class Sand(Terrain):
    name = "sand"
    layer = 1
    rim = Rim("sand", 0.10, 0.80, inner_shift=-1)
    rim_over = {
        "grass": Rim("sand", 0.05, 0.70, inner_shift=-1),
        "snow": Rim("sand", 0.15, 0.85, inner_shift=-1),
    }
    flat = (0xD4, 0xB8, 0x6B)
    flat_ramp = "sand"
    flat_step = 2
    keywords = ("sand", "desert")
    glyph = "·"

    def paint_ground(self, canvas: Canvas) -> None:
        dunes = self.fbm("dunes", 2, 3)
        ripples = texture.sine_field(TILE, TILE, 2, 3, warp=self.fbm("warp", 2, 3),
                                     warp_amount=0.85)
        surface = texture.blend(dunes, ripples, 0.30)
        shade_field(canvas, surface, self.ramp("sand"), relief=6.0, ambient=0.34,
                    height_gain=0.55)

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(1 + variant):
            _pebble(canvas, rng.uniform(0, TILE), rng.uniform(0, TILE),
                    rng.uniform(1.0, 1.6), self.ramp("dirt"))


class Snow(Terrain):
    name = "snow"
    layer = 2
    rim = Rim("snow", 0.10, 1.00, inner_shift=-1)
    rim_over = {
        "grass": Rim("snow", 0.00, 0.85, inner_shift=-1),
        "mountain": Rim("snow", 0.15, 1.00, inner_shift=-1),
    }
    jitter = 0.12
    # Not a palette colour: this is the flat snow the editor's grid already
    # draws, brighter than any step of the snow ramp because a flat cell has no
    # texture to lift it.
    flat = (0xDC, 0xEB, 0xEE)
    flat_ramp = "snow"
    flat_step = 1
    keywords = ("snow", "ice")
    glyph = "*"

    def paint_ground(self, canvas: Canvas) -> None:
        drifts = self.fbm("drifts", 2, 3)
        grain = self.fbm("grain", 8, 2)
        surface = texture.blend(drifts, grain, 0.09)
        shade_field(canvas, surface, snow_ramp(), relief=8.0, ambient=0.30,
                    height_gain=0.50)

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(3 + variant * 2):
            canvas.put(rng.randint(0, TILE - 1), rng.randint(0, TILE - 1),
                       self.ramp("ink")[3])


class Swamp(Terrain):
    name = "swamp"
    layer = 3
    rim = Rim("muck", 0.00, 0.70, inner_shift=-1)
    rim_over = {"grass": Rim("muck", 0.00, 0.60, inner_shift=-1)}
    jitter = 0.13
    flat = (0x65, 0x73, 0x3B)
    flat_ramp = "muck"
    flat_step = 2
    keywords = ("swamp", "marsh")
    glyph = "~"

    def paint_ground(self, canvas: Canvas) -> None:
        muck = self.fbm("muck", 2, 4)
        shade_field(canvas, muck, self.ramp("muck"), relief=7.0, ambient=0.26,
                    height_gain=0.60)
        # Low ground holds standing water; the pools are deliberately broad and
        # smooth so they read as puddles rather than as noise.
        basin = self.fbm("basin", 4, 3)
        pools = bytearray(1 if value < 0.32 else 0 for value in basin.values)
        shade_field(canvas, basin, self.ramp("water"), relief=2.0, ambient=0.40,
                    height_gain=0.25, bias=-0.40, mask=pools)
        for y in range(TILE):
            for x in range(TILE):
                if pools[y * TILE + x]:
                    continue
                if pools[((y + 1) % TILE) * TILE + x] or pools[y * TILE + (x + 1) % TILE]:
                    canvas.shift_pixel(x, y, -1)

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(5 + variant):
            _reed(canvas, rng.randint(0, TILE - 1), rng.randint(0, TILE - 1), rng)
        for _ in range(variant):
            canvas.put(rng.randint(0, TILE - 1), rng.randint(0, TILE - 1),
                       self.ramp("muck")[2])


class Road(Terrain):
    name = "road"
    #: Above water on purpose: a road crossing a river is a bridge, and the
    #: rim styled for water is what makes it read as one.
    layer = 8
    rim = Rim("dirt", 0.00, 0.60, inner_shift=-1)
    rim_over = {
        "grass": Rim("dirt", 0.00, 0.55, inner_shift=-1),
        "sand": Rim("dirt", 0.05, 0.65, inner_shift=-1),
        "water": Rim("wood", 0.00, 0.90, inner_shift=-1, pixels=3.0),
    }
    jitter = 0.10
    flat = (0xA8, 0x87, 0x53)
    flat_ramp = "dirt"
    flat_step = 2
    keywords = ("bridge", "road")
    glyph = "="

    def paint_ground(self, canvas: Canvas) -> None:
        packed = self.fbm("packed", 2, 3)
        shade_field(canvas, packed, self.ramp("dirt"), relief=7.0, ambient=0.26,
                    height_gain=0.60)

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(5 + variant):
            _pebble(canvas, rng.uniform(0, TILE), rng.uniform(0, TILE),
                    rng.uniform(1.4, 2.4), self.ramp("rock"))
        for _ in range(5):
            canvas.put(rng.randint(0, TILE - 1), rng.randint(0, TILE - 1),
                       self.ramp("dirt")[0])


class Hills(Terrain):
    """Rolling high ground: the field lifted into long ridges, with the rock
    beneath it breaking through at the crests.

    It sits between grass and mountain in every sense: in the compositing
    order, in how much of the ground shows through, and in what an author
    means by it. So it takes the grass ramp and borrows the rock one.
    """

    name = "hills"
    layer = 4
    rim = Rim("grass", 0.10, 0.70, inner_shift=-1, pixels=2.5)
    rim_over = {
        "grass": Rim("grass", 0.05, 0.60, inner_shift=-1, pixels=2.5),
        "snow": Rim("grass", 0.15, 0.80, inner_shift=-1, pixels=2.5),
    }
    jitter = 0.12
    flat = (0x62, 0xA8, 0x52)
    flat_ramp = "grass"
    flat_step = 3
    keywords = ("hill", "highland")
    glyph = "^"
    #: One step: it sits between grass and mountain here too.
    elevation = 1

    def paint_ground(self, canvas: Canvas) -> None:
        ridges = texture.sine_field(TILE, TILE, 1, 2, warp=self.fbm("warp", 2, 3),
                                    warp_amount=0.90)
        turf = self.fbm("turf", 4, 3)
        surface = texture.blend(ridges, turf, 0.35)
        shade_field(canvas, surface, self.ramp("grass"), relief=9.0, ambient=0.28,
                    height_gain=0.75)
        # Rock shows where the ridge is highest, which is what separates a hill
        # from a field at a glance.
        rock = self.ramp("rock")
        for y in range(TILE):
            for x in range(TILE):
                if surface.at(x, y) > 0.84 and turf.at(x, y) > 0.46:
                    canvas.put(x, y, rock[2 if surface.at(x, y) > 0.90 else 1])

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(6 + variant * 2):
            _tuft(canvas, rng.randint(0, TILE - 1), rng.randint(0, TILE - 1), rng,
                  self.ramp("grass"))
        for _ in range(1 + variant):
            _pebble(canvas, rng.uniform(0, TILE), rng.uniform(0, TILE),
                    rng.uniform(1.2, 1.8), self.ramp("rock"))


class Ruins(Terrain):
    """What is left of something built: broken flagstone, fallen wall stubs,
    and the ground taking it back.

    Drawn as stone over dirt rather than as a building, because it is ground a
    unit stands on. The wall stubs are placed on a wrapped grid like the
    forest's crowns, so the tile still repeats.
    """

    name = "ruins"
    layer = 9
    rim = Rim("rock", 0.00, 0.80, inner_shift=-1, pixels=2.5)
    rim_over = {
        "grass": Rim("rock", 0.00, 0.70, inner_shift=-1, pixels=2.5),
        "sand": Rim("rock", 0.05, 0.85, inner_shift=-1, pixels=2.5),
    }
    jitter = 0.11
    flat = (0x94, 0x87, 0x79)
    flat_ramp = "rock"
    flat_step = 3
    keywords = ("ruin", "rubble")
    glyph = "†"

    #: Wall stubs sit on a jittered 16-pixel grid, wrapped with the tile.
    WALL_CELL = 16

    def paint_ground(self, canvas: Canvas) -> None:
        rubble = texture.blend(self.fbm("rubble", 4, 3), self.fbm("grit", 8, 2), 0.30)
        shade_field(canvas, rubble, self.ramp("dirt"), relief=6.0, ambient=0.28,
                    height_gain=0.45, bias=-0.20)
        rock = self.ramp("rock")
        # Flagstones: a coarse cell grid, most of it still in place.
        stone = self.fbm("stone", 4, 2)
        for y in range(TILE):
            for x in range(TILE):
                if (x % 8 == 0 or y % 8 == 0) or stone.at(x, y) < 0.42:
                    continue
                canvas.put(x, y, rock[2 if stone.at(x, y) > 0.62 else 1])
        for _, cx, cy, length, seed in self._walls():
            self._wall(canvas, cx, cy, length, Rng(seed), rock)

    def _walls(self) -> List[Tuple[float, float, float, int, int]]:
        """Wall stubs as ``(sort key, x, y, length, seed)``, back to front."""
        walls = []
        cells = TILE // self.WALL_CELL
        for gy in range(cells):
            for gx in range(cells):
                seed = seed_of(self.name, "wall", gx, gy)
                rng = Rng(seed)
                cx = gx * self.WALL_CELL + self.WALL_CELL / 2 + rng.uniform(-3.0, 3.0)
                cy = gy * self.WALL_CELL + self.WALL_CELL / 2 + rng.uniform(-3.0, 3.0)
                walls.append((cy, cx, cy, rng.randint(4, 8), seed))
        walls.sort()
        return walls

    def _wall(self, canvas: Canvas, cx: float, cy: float, length: int, rng: Rng,
              colours: Tuple[int, ...]) -> None:
        horizontal = rng.chance(0.5)
        height = rng.randint(2, 3)
        for step in range(length):
            if rng.chance(0.18):
                continue  # A gap: a wall stub is a wall that lost pieces.
            x = int(cx) + (step if horizontal else 0)
            y = int(cy) + (0 if horizontal else step)
            canvas.put(x, y + 1, colours[0])
            for lift in range(height):
                canvas.put(x, y - lift, colours[3 if lift == height - 1 else 2])

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(2 + variant):
            _pebble(canvas, rng.uniform(0, TILE), rng.uniform(0, TILE),
                    rng.uniform(1.0, 1.8), self.ramp("rock"))
        for _ in range(variant):
            _tuft(canvas, rng.randint(0, TILE - 1), rng.randint(0, TILE - 1), rng,
                  self.ramp("grass"))


class Farmland(Terrain):
    """Ground somebody has turned: parallel furrows with a crop coming up.

    The library's first straight-line geometry, and that is the whole point of
    it. Thirteen terrains cannot all be told apart by tone, so nothing added
    after the first few can be given a tone of its own; it has to be told apart
    by the shape that repeats in it. Furrows are the one shape nothing else
    here draws. Every other field in the library is noise, and ``hills``'s
    ridges are warped on purpose.

    Four furrows to a tile rather than the eight a closer plough would give,
    because a pattern finer than four pixels a cycle averages to a flat tone as
    soon as the tile is drawn smaller than it was authored, and a pattern that
    does not survive that is decoration rather than identity.
    """

    name = "farmland"
    #: With sand, and under everything worked: a road cuts through a field.
    layer = 1
    #: No inner shift, and the reason is arithmetic rather than taste.
    #: ``inner_shift`` darkens the band just inside the rim by moving each pixel
    #: one step down its own ramp, and one step down from this terrain's darkest
    #: soil is ``dirt[0]``. Spending that colour here is invisible in the tile
    #: and expensive in every palette reduction downstream, because a colour's
    #: share of the sheet is what decides whether a sixteen-entry palette keeps
    #: it: measured, the shift on this terrain and on :class:`Paved` would paint
    #: 8,182 such texels.
    #:
    #: The rim starts at 0.28 rather than at 0.00 for the same reason: below a
    #: quarter of the ramp, :func:`raster.pick` returns its first step, and this
    #: ramp's first step is ``dirt[0]``. Starting one step up costs the edge its
    #: darkest pixel and takes this terrain's contribution to that colour to
    #: zero.
    rim = Rim("dirt", 0.28, 0.62)
    #: Only a lower-layer neighbour is ever the terrain underneath, and at
    #: layer 1 that is grass and nothing else, so one transition set is the
    #: whole honest cost rather than a sheet nobody can reach.
    rim_over = {"grass": Rim("dirt", 0.28, 0.58)}
    jitter = 0.10
    flat = (0x7D, 0x64, 0x40)
    flat_ramp = "dirt"
    flat_step = 1
    keywords = ("farm", "field")
    glyph = "≡"

    #: Whole furrow cycles across the tile. Four, so a furrow is eight pixels
    #: wide as authored and still four when the tile is halved.
    FURROWS = 4

    def _tilth(self) -> Field:
        """The furrow field: straight, with just enough wander to look ploughed
        rather than ruled."""
        return texture.blend(
            texture.sine_field(TILE, TILE, 0, self.FURROWS,
                               warp=self.fbm("wander", 2, 2), warp_amount=0.14),
            self.fbm("clod", 8, 2), 0.22)

    def paint_ground(self, canvas: Canvas) -> None:
        # The dirt ramp minus its darkest step: the added terrains
        # deliberately introduce no colour the ground was not already drawn
        # in, and the older ground terrains do not spend that step.
        soil = self.ramp("dirt")[1:]
        surface = self._tilth()
        shade_field(canvas, surface, soil, relief=9.0, ambient=0.26,
                    height_gain=0.55)
        # The crop, sown along the crest of each ridge: a broken line rather
        # than a solid one, so the row reads as plants rather than as paint.
        grass = self.ramp("grass")
        sow = self.fbm("sow", 8, 2)
        for y in range(TILE):
            for x in range(TILE):
                if surface.at(x, y) < 0.74:
                    continue
                if sow.at(x, y) < 0.44:
                    continue
                canvas.put(x, y, grass[2 if surface.at(x, y) > 0.86 else 1])

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(2 + variant):
            _pebble(canvas, rng.uniform(0, TILE), rng.uniform(0, TILE),
                    rng.uniform(1.0, 1.6), self.ramp("dirt")[1:])
        for _ in range(variant * 2):
            canvas.put(rng.randint(0, TILE - 1), rng.randint(0, TILE - 1),
                       self.ramp("grass")[3])


class Bamboo(Terrain):
    """A stand of culms: thick vertical stems over a dark floor.

    Drawn in the ``grass`` ramp rather than in ``foliage``, which is the
    opposite of the obvious choice and is forced twice over. ``forest`` already
    holds the dark end of the library's tone range, so a second terrain in the
    canopy ramp would be a second black square; and a bamboo stand really is
    paler than a pine wood, so the ramp that separates it is also the ramp that
    is right for it. The floor is ``leather``'s darkest step, which no theme
    substitutes, so the gaps between the culms stay dark while the culms
    themselves take the season.

    Four culms to a tile, for the reason ``Farmland`` gives: a thinner stand
    would average away as soon as the tile is drawn small and the terrain would
    be a flat green field there.
    """

    name = "bamboo"
    #: With forest, so it composites like the canopy it is; the tie breaks on
    #: name, which puts forest over bamboo where the two meet.
    layer = 5
    rim = Rim("grass", 0.05, 0.65, inner_shift=-1, pixels=2.5)
    rim_over = {
        "grass": Rim("grass", 0.00, 0.55, inner_shift=-1, pixels=2.5),
        "snow": Rim("grass", 0.10, 0.75, inner_shift=-1, pixels=2.5),
    }
    jitter = 0.12
    flat = (0x83, 0xC2, 0x68)
    flat_ramp = "grass"
    flat_step = 4
    keywords = ("bamboo", "thicket")
    glyph = "‖"

    #: Culms per tile, and how wide one is. Both are floors set by the Game
    #: Boy's 2:1 box filter rather than by taste.
    CULMS = 4
    CULM_WIDTH = 3

    def _culms(self) -> List[Tuple[float, float, int, int]]:
        """Culms as ``(centre x, bow amplitude, width, seed)``, left to right."""
        spacing = TILE / self.CULMS
        stand = []
        for index in range(self.CULMS):
            seed = seed_of(self.name, "culm", index)
            rng = Rng(seed)
            centre = index * spacing + spacing * 0.5 + rng.uniform(-1.0, 1.0)
            # Nearly straight, because bamboo is: enough bow that four culms are
            # not four ruled lines, not enough to read as a vine.
            width = self.CULM_WIDTH - (1 if rng.chance(0.3) else 0)
            stand.append((centre, rng.uniform(0.3, 0.8), width, seed))
        return stand

    def paint_ground(self, canvas: Canvas) -> None:
        litter = self.fbm("litter", 8, 3)
        floor = (self.ramp("leather")[0],) + self.ramp("rock")[:2]
        shade_field(canvas, litter, floor, relief=4.0, ambient=0.30,
                    height_gain=0.30, bias=-0.25)
        green = self.ramp("grass")
        for centre, bow, width, seed in self._culms():
            rng = Rng(seed)
            base = rng.randint(2, 3)
            node = rng.randint(0, 7)
            for y in range(TILE):
                # A whole cycle of bow over the tile height, so the culm leaves
                # the top edge exactly where it enters the bottom one.
                offset = bow * math.sin(2.0 * math.pi * y / TILE)
                left = int(round(centre + offset - width * 0.5))
                # A node ring every eight pixels: one row, one step down. It is
                # what separates a culm from a reed, and it has to be quiet
                # enough that the stem still reads as one stem.
                banded = (y % 8) == node
                for step in range(width):
                    shade = base
                    if step == 0:
                        shade = base + 1
                    elif step == width - 1:
                        shade = base - 1
                    if banded:
                        shade -= 1
                    canvas.put(left + step, y,
                               green[max(0, min(len(green) - 1, shade))])

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        green = self.ramp("grass")
        for _ in range(2 + variant):
            # Leaf sprigs: a lanceolate blade off a culm, two pixels thick in
            # the middle so it reads as a leaf rather than as a speck. Drawn
            # wrapped like every other prop, so the tile stays periodic.
            x = rng.randint(0, TILE - 1)
            y = rng.randint(0, TILE - 1)
            direction = 1 if rng.chance(0.5) else -1
            length = rng.randint(3, 5)
            for step in range(length):
                canvas.put(x + direction * step, y - step, green[4])
                if 0 < step < length - 1:
                    canvas.put(x + direction * step, y - step + 1, green[3])


class Paved(Terrain):
    """Worked stone that is still standing: a laid floor, joints and all.

    The library's only built surface before this one is ``ruins``, which is
    broken by definition, so a courtyard, a plaza, a temple approach and a crypt
    floor had no ground of their own. The two are separated by exactly what
    separates the things themselves: this is a regular running bond with every
    stone in place, where ``ruins`` is the same grid with holes in it and wall
    stubs lying across it.

    The joints are packed earth rather than dark mortar, and that is a
    legibility decision: the stone falls on the same tones as ``mountain`` and
    ``ruins``, so the grid has to be carried by the *light* line between the
    stones.
    """

    name = "paved"
    #: With ruins, so rubble composites over a floor rather than under it.
    layer = 9
    #: No inner shift, for the reason ``Farmland.rim`` states: one step down
    #: from the bed this floor is set in is ``dirt[0]``, and enough of that
    #: colour changes which entries a reduced palette keeps. A laid floor is
    #: also the terrain that least wants a soft shoulder.
    rim = Rim("rock", 0.00, 0.80, pixels=2.5)
    rim_over = {
        "grass": Rim("rock", 0.00, 0.70, pixels=2.5),
        "road": Rim("rock", 0.05, 0.85, pixels=2.5),
    }
    jitter = 0.09
    #: Below the threshold on purpose: a laid floor runs to its edge rather
    #: than pulling back inside its cell the way a natural surface does.
    threshold = 0.46
    flat = (0x5B, 0x51, 0x45)
    flat_ramp = "rock"
    flat_step = 1
    keywords = ("pave", "cobble")
    glyph = "▦"

    #: One stone, and the course it is laid in. Eight divides the tile, and
    #: alternate courses are offset by half a stone, so the bond repeats every
    #: two courses and the tile stays periodic.
    STONE = 8
    COURSE = 8

    #: How wide the joint between two stones is. Two rather than one, and the
    #: reason is the same measurement that set ``Farmland.FURROWS``: the Game
    #: Boy profile box-filters 32 pixels to 16, so a one-pixel joint is an
    #: eighth of the eight-pixel cycle and contributes an eighth of a filtered
    #: pixel, so the grid averages into gravel. Two pixels is a quarter of the
    #: cycle and survives as a line, which is the whole identity of this
    #: terrain on that profile.
    JOINT = 2

    #: Where the bond starts, in pixels from the tile's own corner, and the one
    #: number here that two separate measurements pin rather than one.
    #:
    #: Not zero, because :mod:`.verify` measures periodicity as the mean
    #: luminance step across the wrap against the mean interior step, and a
    #: joint sitting exactly on the seam makes the wrap row the single
    #: highest-contrast row in the tile, which reads as a seam to that
    #: measurement even though the tile is perfectly periodic. Moving the bond
    #: puts an ordinary row on the edge instead of the loudest one.
    #:
    #: And *even*, because halving the tile averages 2x2 native blocks: an
    #: odd offset splits every joint across two output rows at half strength
    #: each, and the grid this terrain is identified by dissolves into noise.
    #: Four is the smallest even offset that also keeps both joint rows off the
    #: tile edge.
    BOND = 4

    def paint_ground(self, canvas: Canvas) -> None:
        # The bed the stones are set in: two middle steps of the dirt ramp. Not
        # its darkest, which this terrain deliberately spends nowhere, and not
        # its lightest, which at a two-pixel joint turns the bed into the
        # subject and leaves the stones looking scattered in it.
        bed = self.ramp("dirt")[1:3]
        grit = texture.blend(self.fbm("grit", 8, 2), self.fbm("bed", 4, 3), 0.35)
        shade_field(canvas, grit, bed, relief=3.0, ambient=0.30,
                    height_gain=0.25, bias=-0.32)
        # The stone, minus the rock ramp's darkest step. That step reads as a
        # distinctly red brown, which is unremarkable scattered through a
        # boulder field and conspicuous as a clean line under every stone in a
        # regular bond.
        rock = self.ramp("rock")[1:]
        # Fine rather than broad: a coarse wear field puts one dark blob in the
        # middle of every stone, which reads as a stain. Grain reads as stone.
        wear = self.fbm("wear", 8, 2)
        for course in range(TILE // self.COURSE):
            offset = (self.STONE // 2) if course % 2 else 0
            for index in range(TILE // self.STONE):
                self._stone(canvas, index * self.STONE + offset + self.BOND,
                            course * self.COURSE + self.BOND,
                            Rng(seed_of(self.name, "stone", index, course)),
                            rock, wear)

    def _stone(self, canvas: Canvas, x0: int, y0: int, rng: Rng,
               colours: Tuple[int, ...], wear: Field) -> None:
        """One laid stone, short of its cell by the joint on two sides, so the
        joint between stones is the bed showing through."""
        # Mostly the middle face, with the occasional darker stone. The split
        # is deliberately lopsided: a coarse colour depth pulls the two faces
        # further apart than they are here, and an even split reads as a
        # chequerboard rather than as paving.
        face = 0 if rng.chance(0.25) else 1
        height = self.COURSE - self.JOINT
        width = self.STONE - self.JOINT
        for y in range(y0, y0 + height):
            for x in range(x0, x0 + width):
                shade = face
                if y == y0:
                    shade = face + 1  # the lit top edge
                elif y == y0 + height - 1:
                    shade = face - 1  # the shaded lower edge
                elif x == x0:
                    shade = face + 1
                grain = wear.at(x % TILE, y % TILE)
                if grain > 0.70:
                    shade += 1
                elif grain < 0.28:
                    shade -= 1
                canvas.put(x, y, colours[max(0, min(len(colours) - 1, shade))])

    def paint_props(self, canvas: Canvas, variant: int, rng: Rng) -> None:
        for _ in range(variant):
            # Grass finding a joint. One ramp step, and only in the later
            # variants, so a plaza does not read as a lawn.
            canvas.put(rng.randint(0, TILE - 1), rng.randint(0, TILE - 1),
                       self.ramp("grass")[1])
        for _ in range(1 + variant):
            _pebble(canvas, rng.uniform(0, TILE), rng.uniform(0, TILE),
                    rng.uniform(1.0, 1.4), self.ramp("rock"))


TERRAIN_CLASSES: Tuple[type, ...] = (
    Grass, Sand, Snow, Swamp, Road, Forest, Mountain, Water, Hills, Ruins,
    Farmland, Bamboo, Paved,
)

TERRAINS: Dict[str, Terrain] = {cls.name: cls() for cls in TERRAIN_CLASSES}

#: Terrain names in compositing order, low layer first.
TERRAIN_ORDER: Tuple[str, ...] = tuple(
    sorted(TERRAINS, key=lambda name: (TERRAINS[name].layer, name))
)

#: The order an authored terrain name is matched against terrain keywords: the
#: first terrain here with a keyword in the name wins. It is not the
#: compositing order, because it settles ambiguity between words rather than
#: between layers: "mountain road" is a road, and "stone bridge" is a bridge.
#: Grass is last because it is also the fallback for a name that reads as open
#: ground. Appending a terrain to the end cannot change how an existing name
#: resolves.
#:
#: Every terrain must carry exactly two keywords. That is not a style rule: the
#: generated table is emitted with an inner dimension of the longest keyword
#: list, and ``tools/game_content/src/compiler.cpp`` mirrors it with a
#: hand-written ``std::string_view keywords[2]``. A terrain with one keyword
#: leaves a null the round-trip test walks; a terrain with three overflows the
#: mirror. A known hazard, obeyed here rather than fixed.
KEYWORD_ORDER: Tuple[str, ...] = (
    "water", "road", "forest", "mountain", "sand", "snow", "swamp",
    "hills", "ruins", "grass", "farmland", "bamboo", "paved",
)

assert set(KEYWORD_ORDER) == set(TERRAINS), (
    "every terrain must appear exactly once in the keyword match order")
assert len({TERRAINS[name].glyph for name in TERRAIN_ORDER}) == len(TERRAINS), (
    "two terrains share a glyph; terrain identity would rest on colour alone")


def kind_of(name: str) -> Optional[str]:
    """The terrain an authored name draws as, or ``None`` for none of them.

    This is the whole selection mechanism, and the only implementation of it:
    the editor and the console both apply this table rather than each holding
    a list of one game's terrain names.
    """
    lowered = name.lower()
    for candidate in KEYWORD_ORDER:
        if any(keyword in lowered for keyword in TERRAINS[candidate].keywords):
            return candidate
    return None

#: Every (overlay, underlying) pair that gets a dedicated transition sheet.
TRANSITION_PAIRS: Tuple[Tuple[str, str], ...] = tuple(
    (name, under)
    for name in TERRAIN_ORDER
    for under in sorted(TERRAINS[name].rim_over)
)


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

_ground_cache: Dict[Tuple[str, str], Canvas] = {}
_jitter_cache: Dict[str, List[float]] = {}


def flat_colour(name: str, active: Optional[Theme] = None) -> Rgb:
    """The one colour a client draws for a terrain cell in a theme.

    Under the default theme this is the terrain's own committed value, so the
    editor's map grid keeps the exact colours it ships. Under any other theme
    it is the same ramp position in
    whatever ramp that theme paints instead, which is what keeps a cell view
    and a tile view of the same themed map agreeing.
    """
    terrain = TERRAINS[name]
    resolved = active or themes.DEFAULT_THEME
    replacement = themes.ramp_name(resolved, terrain.flat_ramp)
    if replacement == terrain.flat_ramp:
        return terrain.flat
    return RGB[RAMPS[replacement][terrain.flat_step]]


def ground(name: str) -> Canvas:
    """The variant-independent periodic surface for a terrain."""
    key = (_ACTIVE.name, name)
    cached = _ground_cache.get(key)
    if cached is None:
        terrain = TERRAINS[name]
        canvas = Canvas(TILE, TILE)
        canvas.wrap = True
        terrain.paint_ground(canvas)
        cached = canvas
        _ground_cache[key] = cached
    return cached.copy()


def jitter_field(name: str) -> List[float]:
    """Periodic boundary wobble, shared by every variant of a terrain.

    Being periodic over exactly one tile is what keeps the wobble continuous
    from tile to tile: two neighbours evaluate the same function of position.
    """
    cached = _jitter_cache.get(name)
    if cached is None:
        terrain = TERRAINS[name]
        field = noise.fbm(TILE, TILE, 4, 3, seed_of(name, "boundary-jitter"))
        amplitude = terrain.jitter
        cached = [(v - 0.5) * 2.0 * amplitude for v in field.values]
        _jitter_cache[name] = cached
    return cached


def base_tile(name: str, variant: int) -> Canvas:
    """A seamless interior tile. Props wrap, so the tile stays periodic."""
    canvas = ground(name)
    canvas.wrap = True
    TERRAINS[name].paint_props(canvas, variant, Rng(seed_of(name, "variant", variant)))
    canvas.wrap = False
    return canvas


def blob_tile(name: str, mask: int, under: Optional[str] = None) -> Canvas:
    """One of the 47 autotile variants, optionally rimmed for a neighbour.

    ``under`` selects a transition rim from ``rim_over``; passing a terrain
    with no entry falls back to the terrain's own rim.
    """
    terrain = TERRAINS[name]
    rim = terrain.rim_over.get(under, terrain.rim) if under else terrain.rim
    coverage = autotile.Coverage(mask, TILE, jitter_field(name), terrain.threshold,
                                 rim.pixels)
    canvas = base_tile(name, 0)
    canvas.mask_with(coverage.alpha)
    _paint_rim(canvas, coverage, rim)
    return canvas


def _paint_rim(canvas: Canvas, coverage: autotile.Coverage, rim: Rim) -> None:
    colours = snow_ramp() if rim.ramp == "snow" else ramp(rim.ramp)
    size = coverage.size
    for y in range(size):
        for x in range(size):
            position = y * size + x
            if not coverage.rim[position]:
                continue
            normal_x, normal_y = coverage.normals[position]
            tone = rim.low + (rim.high - rim.low) * edge_tone(normal_x, normal_y)
            canvas.put(x, y, pick(colours, tone, x, y))
    if not rim.inner_shift:
        return
    for y in range(size):
        for x in range(size):
            position = y * size + x
            if coverage.rim[position] or not coverage.alpha[position]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < size and 0 <= ny < size and coverage.rim[ny * size + nx]:
                    canvas.shift_pixel(x, y, rim.inner_shift)
                    break
