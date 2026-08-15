# SPDX-License-Identifier: MIT
"""Output profiles: one source definition, several target machines.

Terrain and characters are authored once, at 32x32, against the master
palette. A *profile* is the only thing that knows how to get from there to a
particular machine's constraints. Nothing in :mod:`.terrain` or
:mod:`.characters` may branch on a target.

Profiles shipped
----------------
``modern``     32x32 RGBA. The web build. Full master palette, no quantisation.
``n64_ci8``    32x32 indexed, 8bpp, master palette. Straight CI8 for ``mksprite``.
``n64_ci4``    32x32 indexed, 4bpp, a per-asset 16-colour subset of the master
               palette. This is the TLUT bank model the hardware actually uses.

One profile is not shipped
--------------------------
:data:`LEGIBILITY` is a measuring instrument rather than a target: 16x16 and
four tones, the reduction :func:`.verify.check_legibility` asks "does this
sprite still read when it is small?" through. It is deliberately outside
:data:`PROFILES`, so no file is written from it and no manifest describes it.
The only things that read it are the legibility pass and the gallery page that
shows an author what that pass sees.

Why quantisation lives here and is explicit
-------------------------------------------
Leaving colour reduction to a conversion tool at build time would make the
low-end output unreviewable and unreproducible. Instead each reduction is a
deliberate, checked-in step:

* **Palette subsetting (CI4)** picks the 16 most-used master colours in that
  asset, ties broken by palette index, then maps the rest to their nearest
  surviving neighbour. Deterministic, and it keeps an asset's own colours
  rather than a global compromise.
* **Ordered dithering** uses the same 8x8 Bayer matrix as the renderers,
  indexed by *destination* pixel coordinates. Every tile dimension is a
  multiple of 8, so the dither pattern is continuous across tile boundaries and
  a quantised tile still joins its neighbour.
* **Downscaling** is a box filter at an exact integer ratio, so a 2x2 source
  block never straddles a tile boundary and adjacency survives the reduction.

Adding a profile
----------------
Append a :class:`Profile` to :data:`PROFILES`. If it needs a colour mapping the
existing ``palette_mode`` values do not cover, add a mode and handle it in
:func:`convert`; keep the mapping table in this module so it is reviewable as a
diff. Everything downstream is driven off the registry: sheets, manifest,
verification, gallery.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

from PIL import Image

from . import palette, themes
from .palette import Rgb
from .raster import Canvas, bayer

#: The four tones the legibility reduction resolves to, lightest first. Four
#: neutral steps, evenly spaced, taken from the master palette: what is being
#: measured is separation of tone, so a hue here would be a hue the measurement
#: has to see past. Every colour in this library is a master palette entry, this
#: one included.
LEGIBILITY_SHADES: Tuple[Rgb, ...] = tuple(
    palette.RGB[palette.RAMPS[_ramp][_step]]
    for _ramp, _step in (("ink", 3), ("steel", 2), ("ink", 2), ("ink", 0))
)

#: Tone assignments for the legibility reduction, per palette ramp.
#:
#: Mapping colour to tone by luminance alone fails twice at four tones, and both
#: failures are the kind that only show up once the colour is gone:
#:
#: * grass (#4d9147) and water (#3182a4) have almost the same luminance, so a
#:   river would vanish into the field it runs through; and
#: * the two faction colours are equally bright, so the sides would be
#:   indistinguishable with no colour left to tell them apart.
#:
#: So the mapping is assigned rather than measured. Terrains are spread across
#: the four tones in a deliberate order, foliage darkest, then water, rock,
#: swamp, grass, road, sand, snow. The factions are separated by tone, with the
#: Dawn Guard light and the Ashen Coil dark. Ramps not listed here keep their
#: luminance.
_LEGIBILITY_RAMP_TONES: Dict[str, Tuple[float, ...]] = {
    "foliage": (0.00, 0.02, 0.05, 0.08, 0.12),
    "water": (0.22, 0.28, 0.34, 0.40, 0.50),
    "muck": (0.26, 0.32, 0.38),
    "rock": (0.28, 0.36, 0.44, 0.52),
    "hide": (0.04, 0.09, 0.14),
    "grass": (0.50, 0.56, 0.62, 0.68, 0.74),
    "dirt": (0.66, 0.72, 0.80, 0.88),
    "sand": (0.76, 0.82, 0.88, 0.96),
    "snow": (0.93, 0.97, 1.00),
    # The faction colour menu. Four tones cannot separate six colours, so the
    # table spends the extremes on the two the sample content uses most and
    # spreads the rest between them; through this reduction a game that picks
    # green and amber gets two sides that read as mid-tones and must lean on
    # silhouette instead. README.md records that as a known limitation.
    "blue": (0.78, 0.86, 0.94, 1.00),
    "red": (0.00, 0.04, 0.08, 0.14),
    "green": (0.30, 0.36, 0.42, 0.50),
    "violet": (0.10, 0.16, 0.22, 0.30),
    "amber": (0.62, 0.70, 0.78, 0.88),
    "bone": (0.86, 0.90, 0.96, 1.00),
    "ink": (0.00, 0.05, 0.40, 1.00),
}

#: A theme's replacement ramp inherits the tone assignment of the ramp it
#: stands in for. The separation this table buys was reasoned about per
#: terrain, foliage darkest, water below rock, snow at the top, and a recolour
#: does not change which terrain is which, so an autumn field must still read
#: as the field and not as the road beside it.
for _theme in themes.THEMES:
    for _original, _replacement in _theme.substitutions.items():
        if _original in _LEGIBILITY_RAMP_TONES:
            _LEGIBILITY_RAMP_TONES.setdefault(
                _replacement, _LEGIBILITY_RAMP_TONES[_original])

_LEGIBILITY_TONE_OVERRIDES: Dict[int, float] = {}
for _ramp, _tones in _LEGIBILITY_RAMP_TONES.items():
    _entries = palette.RAMPS[_ramp]
    assert len(_tones) == len(_entries), (
        f"legibility tone table for ramp {_ramp!r} has {len(_tones)} entries "
        f"but the ramp has {len(_entries)}"
    )
    for _position, _tone in enumerate(_tones):
        _LEGIBILITY_TONE_OVERRIDES[_entries[_position]] = _tone


@dataclass(frozen=True)
class Profile:
    """One output target."""

    name: str
    label: str
    tile_size: int
    sprite_size: int
    encoding: str  # "rgba" or "indexed"
    bit_depth: int
    max_colours: int
    palette_mode: str  # "master", "subset", or "tones"
    notes: str
    #: Redraw a one-pixel silhouette border on sprites after reduction. A
    #: profile that halves the resolution averages the authored outline away,
    #: and a unit without a hard border is unreadable against textured ground.
    restore_outline: bool = False

    @property
    def emits_palette(self) -> bool:
        return self.encoding == "indexed"


PROFILES: Tuple[Profile, ...] = (
    Profile(
        name="modern",
        label="Modern / web",
        tile_size=32,
        sprite_size=32,
        encoding="rgba",
        bit_depth=8,
        max_colours=palette.PALETTE_SIZE,
        palette_mode="master",
        notes=f"Full {palette.PALETTE_SIZE}-entry master palette written as "
              "straight RGBA8888.",
    ),
    Profile(
        name="n64_ci8",
        label="Nintendo 64 CI8",
        tile_size=32,
        sprite_size=32,
        encoding="indexed",
        bit_depth=8,
        max_colours=256,
        palette_mode="master",
        notes=(
            f"8bpp indexed against the shared {palette.PALETTE_SIZE}-entry "
            "master palette. Power-of-"
            "two dimensions; feed straight to mksprite as CI8."
        ),
    ),
    Profile(
        name="n64_ci4",
        label="Nintendo 64 CI4",
        tile_size=32,
        sprite_size=32,
        encoding="indexed",
        bit_depth=4,
        max_colours=16,
        palette_mode="subset",
        notes=(
            "4bpp indexed against a per-asset 16-colour TLUT bank chosen from "
            "the master palette. Power-of-two dimensions; CI4 for mksprite."
        ),
    ),
)

PROFILES_BY_NAME: Dict[str, Profile] = {profile.name: profile for profile in PROFILES}

#: The reduction the legibility pass measures on, and the one profile here that
#: writes nothing. Half the resolution and four tones is the smallest a sprite
#: in this library is ever asked to read at, so it is where "can two of these be
#: told apart?" has a hard answer; :func:`.verify.check_legibility` asks it on
#: every build and the gallery's figure pages show what it saw.
#:
#: Kept out of :data:`PROFILES` deliberately. Everything downstream is driven
#: off that registry: sheets, manifests, per-profile verification, the
#: gallery's profile table. A measuring instrument that appeared in it would
#: ship as an output target, which it is not.
LEGIBILITY: Profile = Profile(
    name="legibility",
    label="Legibility reduction",
    tile_size=16,
    sprite_size=16,
    encoding="indexed",
    bit_depth=2,
    max_colours=len(LEGIBILITY_SHADES),
    palette_mode="tones",
    notes=(
        "Half size and four neutral tones. Measured, never written: the "
        "reduction the legibility pass asks whether a sprite still reads at."
    ),
    restore_outline=True,
)


# ---------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Converted:
    """A canvas reduced to one profile's constraints, ready to encode."""

    width: int
    height: int
    indices: List[int]
    colours: Tuple[Rgb, ...]
    transparent: Tuple[int, ...]


def _downscale(canvas: Canvas, divisor: int) -> Canvas:
    """Box-filter a canvas by an exact integer ratio, via Pillow.

    Pillow does the resampling; the result is immediately re-quantised back
    onto the master palette, so no intermediate colour ever reaches an output
    file.

    Colour is *premultiplied* by alpha before resampling and divided out
    afterwards. Without that, a block straddling a silhouette edge averages the
    black stored behind transparent pixels into the visible colour and every
    sprite grows a dark fringe at half size.
    """
    if divisor == 1:
        return canvas
    assert canvas.width % divisor == 0 and canvas.height % divisor == 0
    premultiplied = bytearray()
    for red, green, blue, alpha in canvas.to_rgba():
        premultiplied += bytes((red * alpha // 255, green * alpha // 255,
                                blue * alpha // 255, alpha))
    source = Image.frombytes("RGBA", (canvas.width, canvas.height),
                             bytes(premultiplied))
    reduced = source.resize(
        (canvas.width // divisor, canvas.height // divisor), Image.Resampling.BOX
    )
    out = Canvas(reduced.width, reduced.height)
    pixels = reduced.load()
    opaque = tuple(range(1, palette.PALETTE_SIZE))
    for y in range(reduced.height):
        for x in range(reduced.width):
            red, green, blue, alpha = pixels[x, y]
            if alpha < 128:
                continue
            colour = (min(255, red * 255 // alpha), min(255, green * 255 // alpha),
                      min(255, blue * 255 // alpha))
            out.data[y * out.width + x] = palette.nearest(colour, opaque)
    return out


def _subset(indices: Sequence[int], limit: int) -> Dict[int, int]:
    """Map every master index used to one of at most ``limit`` survivors.

    Survivors are the most frequent colours, ties broken by palette index so
    the choice never depends on iteration order. Index 0 (transparent) always
    survives and always stays at slot 0.
    """
    counts: Dict[int, int] = {}
    for index in indices:
        counts[index] = counts.get(index, 0) + 1
    used = sorted(index for index in counts if index != palette.TRANSPARENT)
    has_transparency = palette.TRANSPARENT in counts
    ranked = sorted(used, key=lambda index: (-counts[index], index))
    survivors = sorted(ranked[: limit - (1 if has_transparency else 0)])
    mapping: Dict[int, int] = {}
    if has_transparency:
        mapping[palette.TRANSPARENT] = palette.TRANSPARENT
    for index in used:
        if index in survivors:
            mapping[index] = index
        else:
            mapping[index] = palette.nearest(palette.RGB[index], tuple(survivors))
    return mapping


def _tone(index: int) -> float:
    return _LEGIBILITY_TONE_OVERRIDES.get(index, palette.luminance(index))


#: Every native canvas is authored on a 32-pixel grid, tiles and sprites alike.
NATIVE_GRID = 32


def _restore_outline(indices: List[int], width: int, height: int,
                     blank: int, darkest: int) -> None:
    """Redraw a hard silhouette border, one pixel inside the shape.

    Drawn inward rather than outward so a sprite cannot outgrow its frame. It
    costs a pixel of body at 16x16, and buys back the read that the downscale
    took away.
    """
    edge = []
    for y in range(height):
        for x in range(width):
            position = y * width + x
            if indices[position] == blank:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if (not (0 <= nx < width and 0 <= ny < height)
                        or indices[ny * width + nx] == blank):
                    edge.append(position)
                    break
    for position in edge:
        indices[position] = darkest


def convert(canvas: Canvas, profile: Profile,
            is_sprite: bool = False) -> Converted:
    """Reduce a native canvas to ``profile``'s size, palette, and bit depth.

    The reduction ratio comes from the profile's tile size against the native
    32-pixel grid, so a whole sheet reduces exactly as its individual tiles
    would and no filter window ever straddles a tile boundary.

    """
    reduced = _downscale(canvas, NATIVE_GRID // profile.tile_size)

    if profile.palette_mode == "master":
        colours = palette.RGB
        return Converted(reduced.width, reduced.height, list(reduced.data), colours,
                         (palette.TRANSPARENT,))

    if profile.palette_mode == "subset":
        mapping = _subset(reduced.data, profile.max_colours)
        survivors = sorted({value for value in mapping.values()})
        slots = {index: slot for slot, index in enumerate(survivors)}
        colours = tuple(palette.RGB[index] for index in survivors)
        indices = [slots[mapping[index]] for index in reduced.data]
        transparent = ((slots[palette.TRANSPARENT],)
                       if palette.TRANSPARENT in slots else ())
        return Converted(reduced.width, reduced.height, indices, colours, transparent)

    if profile.palette_mode == "tones":
        # Two bits give four slots and no more, and a slot spent on
        # transparency is a slot not spent on tone. So an asset that has
        # transparency gets three tones and one that has none gets four, sorted
        # by whether the art actually contains any. That is what makes the
        # reduction hardest exactly where it should be, on the sprites.
        sprite_like = palette.TRANSPARENT in set(reduced.data)
        if sprite_like:
            colours = (LEGIBILITY_SHADES[0],) + LEGIBILITY_SHADES[1:]
            transparent: Tuple[int, ...] = (0,)
            first_slot, levels = 1, len(LEGIBILITY_SHADES) - 2
        else:
            colours = LEGIBILITY_SHADES
            transparent = ()
            first_slot, levels = 0, len(LEGIBILITY_SHADES) - 1
        indices = []
        for y in range(reduced.height):
            for x in range(reduced.width):
                index = reduced.data[y * reduced.width + x]
                if index == palette.TRANSPARENT:
                    indices.append(0)
                    continue
                position = (1.0 - _tone(index)) * levels
                low = int(position)
                if low >= levels:
                    shade = levels
                elif is_sprite:
                    # Units snap to the nearest tone. With three tones in a
                    # sixteen-pixel figure, dithering reads as noise rather
                    # than as tone.
                    shade = low + (1 if position - low >= 0.5 else 0)
                else:
                    # Ground keeps the ordered dither, which is what carries
                    # texture once colour is gone.
                    shade = low + (1 if position - low > bayer(x, y) else 0)
                indices.append(first_slot + shade)
        if is_sprite and profile.restore_outline and sprite_like:
            _restore_outline(indices, reduced.width, reduced.height, 0,
                             len(colours) - 1)
        return Converted(reduced.width, reduced.height, indices, colours, transparent)

    raise AssertionError(f"unknown palette mode {profile.palette_mode!r}")


def encode(converted: Converted, profile: Profile) -> bytes:
    """Encode a converted canvas as a deterministic PNG."""
    from . import pngio

    if profile.encoding == "rgba":
        pixels = []
        for index in converted.indices:
            if index == palette.TRANSPARENT:
                pixels.append((0, 0, 0, 0))
            else:
                red, green, blue = converted.colours[index]
                pixels.append((red, green, blue, 255))
        return pngio.encode_rgba(converted.width, converted.height, pixels)
    return pngio.encode_indexed(
        converted.width,
        converted.height,
        converted.indices,
        converted.colours,
        converted.transparent,
        profile.bit_depth,
    )


def palette_of(profile: Profile) -> Optional[Tuple[Rgb, ...]]:
    """The palette shared by every asset in a profile, if it has one."""
    if profile.palette_mode == "master":
        return palette.RGB
    if profile.palette_mode == "tones":
        return LEGIBILITY_SHADES
    return None
