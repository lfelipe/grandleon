# SPDX-License-Identifier: MIT
"""The backdrop menu: what a conversation between maps is drawn against.

A backdrop is **a table of colours, not a picture**. It is an ordered run of
horizontal bands from the top of the scene to the bottom, each band a run of
rows in one master palette entry, and that is the whole of it: no pixels, no
sheet, no tile, no texture.

That shape was chosen by measurement rather than taste. A full-screen picture
behind a scene is cheap in a browser and unaffordable on the consoles this
repository targets:

* **Nintendo 64.** A CI4 texture may hold 2,048 bytes of TMEM, which is one
  128x32 sheet. A 320x240 backdrop is 38,400 bytes of CI4 and would have to be
  cut into twenty strips and uploaded twenty times a frame. A band is a
  ``graphics_draw_box`` and touches TMEM not at all.
* **PlayStation.** A flat rectangle names its colour outright, so a band
  is one of the sixty-odd primitives a paint already draws.

So every client draws the same backdrop from the same table, and none of them
pays for an image none of them could afford.

The rows are counted in **twenty-eighths**, which is the coarsest grid any
client lays a scene out on: a text row on a machine that draws its scene in
characters. Every other client scales: row ``r`` of ``BACKDROP_ROWS`` begins at
``round(r * height / 28)`` pixels down its own scene area. So a band boundary
that is exact on the coarsest grid is exact everywhere.

Adding a backdrop
-----------------
**Append** to :data:`BACKDROPS`. Menu order is index order in the schema, the
manifest, the generated header and the compiled package alike, so an insertion
would renumber a choice an author already made. Spend only entries the master
palette already holds: a backdrop adds no colour, for the same reason a style
adds none. The ``n64_ci8`` profile writes the whole master palette into every
asset, so one appended entry rewrites art nobody asked to change.

The two rules below are asserted at import time rather than reviewed.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Tuple

from . import palette

#: How many rows a backdrop divides the scene into. Twenty-eight, the coarsest
#: grid any client lays a scene out on; every client scales it to its own
#: height.
BACKDROP_ROWS = 28

#: The most bands one backdrop may name. Six, because a band narrower than two
#: rows is not a band on the coarsest grid, and six two-row bands already leave
#: the twenty-eight unevenly spent.
MAX_BANDS = 6

#: The colour a client draws a scene's words in, and the one every band is
#: measured against. This is the Nintendo 64 body-text cream at
#: `platform/nintendo64/src/play_rom.cpp`; every other client's ink is lighter
#: still, so a band legible under this is legible under all of them.
TEXT_COLOUR = (245, 234, 210)

#: The contrast ratio every band must reach against :data:`TEXT_COLOUR`. WCAG
#: 2.1's threshold for body text, applied here because a backdrop that swallows
#: the words is a backdrop that broke the scene. It is what makes this library
#: a set of night and interior backdrops rather than an arbitrary one: a dawn
#: sky cannot carry cream letters, so the library does not contain one.
MIN_CONTRAST = 4.5


def _relative_luminance(colour: Tuple[int, int, int]) -> float:
    """WCAG 2.1 relative luminance of an sRGB colour."""
    channels = []
    for value in colour:
        srgb = value / 255.0
        channels.append(
            srgb / 12.92 if srgb <= 0.04045
            else ((srgb + 0.055) / 1.055) ** 2.4
        )
    red, green, blue = channels
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue


def contrast(first: Tuple[int, int, int], second: Tuple[int, int, int]) -> float:
    """WCAG 2.1 contrast ratio between two sRGB colours."""
    lighter = _relative_luminance(first)
    darker = _relative_luminance(second)
    if lighter < darker:
        lighter, darker = darker, lighter
    return (lighter + 0.05) / (darker + 0.05)


@dataclass(frozen=True)
class Band:
    """One horizontal run of the backdrop, in master palette entries."""

    #: How many of :data:`BACKDROP_ROWS` this band occupies.
    rows: int
    #: The master palette index the band is drawn in.
    colour: int


@dataclass(frozen=True)
class Backdrop:
    """One backdrop a scene may name."""

    name: str
    label: str
    summary: str
    #: Top of the scene to the bottom of it. Rows sum to :data:`BACKDROP_ROWS`.
    bands: Tuple[Band, ...]


def _ramp(name: str) -> Tuple[int, ...]:
    return palette.ramp(name)


#: The menu, in the order every client indexes it by. Seven, one reaching for
#: each setting :mod:`.styles` dresses, and every one of them usable by more
#: than the setting it was drawn for. That reuse is the whole reason the set is
#: seven and not twenty.
BACKDROPS: Tuple[Backdrop, ...] = (
    Backdrop(
        name="throne_hall",
        label="Throne hall",
        summary=(
            "A hall by lamplight: a dark roof, stone above and below a beam, "
            "and a carpeted dais. For a court scene in any setting that has "
            "one: medieval, sengoku, or a governor's house in pirates."
        ),
        bands=(
            Band(5, _ramp("ink")[0]),
            Band(6, _ramp("rock")[1]),
            Band(2, _ramp("wood")[0]),
            Band(9, _ramp("rock")[0]),
            Band(4, _ramp("leather")[1]),
            Band(2, _ramp("wood")[1]),
        ),
    ),
    Backdrop(
        name="night_camp",
        label="Night camp",
        summary=(
            "A field under a night sky, a treeline on the horizon, and "
            "firelight on the ground at the player's feet. The between-battles "
            "backdrop: medieval, nature and sengoku all camp."
        ),
        bands=(
            Band(7, _ramp("ink")[0]),
            Band(5, _ramp("ink")[1]),
            Band(4, _ramp("foliage")[0]),
            Band(5, _ramp("muck")[0]),
            Band(5, _ramp("muck")[1]),
            Band(2, _ramp("amber")[0]),
        ),
    ),
    Backdrop(
        name="deep_wood",
        label="Deep wood",
        summary=(
            "Under a canopy: three depths of leaf closing overhead, wet ground "
            "beneath, and leaf litter at the foot. For nature and mythical, "
            "and for any march that leaves the road."
        ),
        bands=(
            Band(8, _ramp("foliage")[0]),
            Band(5, _ramp("foliage")[1]),
            Band(4, _ramp("foliage")[2]),
            Band(5, _ramp("muck")[0]),
            Band(4, _ramp("leather")[0]),
            Band(2, _ramp("dirt")[0]),
        ),
    ),
    Backdrop(
        name="mountain_dusk",
        label="Mountain dusk",
        summary=(
            "Ridges at dusk: a violet sky, two ranges falling away, and pines "
            "at the foot of the near one. Drawn for sengoku and mythical, and "
            "the one backdrop that reads as distance."
        ),
        bands=(
            Band(6, _ramp("violet")[0]),
            Band(4, _ramp("violet")[1]),
            Band(4, _ramp("hide")[1]),
            Band(5, _ramp("rock")[1]),
            Band(5, _ramp("rock")[0]),
            Band(4, _ramp("winter_pine")[1]),
        ),
    ),
    Backdrop(
        name="open_sea",
        label="Open sea",
        summary=(
            "Night water under a night sky, with a rail in the foreground so "
            "the scene is aboard something rather than adrift. For pirates, "
            "and for any crossing."
        ),
        bands=(
            Band(6, _ramp("blue")[0]),
            Band(4, _ramp("blue")[1]),
            Band(6, _ramp("water")[0]),
            Band(7, _ramp("water")[1]),
            Band(5, _ramp("wood")[0]),
        ),
    ),
    Backdrop(
        name="star_field",
        label="Star field",
        summary=(
            "The void, a nebula across it, and a hull along the bottom of the "
            "frame. For scifi, and the only backdrop in the library with no "
            "ground in it."
        ),
        bands=(
            Band(9, _ramp("ink")[0]),
            Band(4, _ramp("violet")[0]),
            Band(5, _ramp("ink")[1]),
            Band(6, _ramp("steel")[0]),
            Band(4, _ramp("blue")[0]),
        ),
    ),
    Backdrop(
        name="crypt",
        label="Crypt",
        summary=(
            "Stone underground: a vault overhead, a wall with bone set in it, "
            "and char at the floor. Drawn for undead, and it suits every "
            "mythical scene that happens below ground."
        ),
        bands=(
            Band(6, _ramp("ink")[0]),
            Band(5, _ramp("hide")[0]),
            Band(6, _ramp("rock")[0]),
            Band(4, _ramp("bone")[0]),
            Band(4, _ramp("hide")[1]),
            Band(3, _ramp("ash_char")[1]),
        ),
    ),
)

BACKDROPS_BY_NAME: Dict[str, Backdrop] = {
    backdrop.name: backdrop for backdrop in BACKDROPS
}


def band_rows(backdrop: Backdrop) -> Tuple[Tuple[int, int, int], ...]:
    """``(top_row, row_count, palette_index)`` for every band, top to bottom.

    The one place a band's position is worked out, so no client derives it
    twice and none of them can derive it differently.
    """
    rows: list[Tuple[int, int, int]] = []
    top = 0
    for band in backdrop.bands:
        rows.append((top, band.rows, band.colour))
        top += band.rows
    return tuple(rows)


# ---------------------------------------------------------------------------
# The rules, refused at import time rather than reviewed
# ---------------------------------------------------------------------------

for _backdrop in BACKDROPS:
    assert 1 <= len(_backdrop.bands) <= MAX_BANDS, (
        f"{_backdrop.name} names {len(_backdrop.bands)} bands, "
        f"at most {MAX_BANDS} are allowed")
    _total = sum(band.rows for band in _backdrop.bands)
    assert _total == BACKDROP_ROWS, (
        f"{_backdrop.name} spends {_total} rows, expected {BACKDROP_ROWS}")
    for _band in _backdrop.bands:
        assert _band.rows >= 2, (
            f"{_backdrop.name} names a band of {_band.rows} rows; a band "
            "narrower than two is not a band on the coarsest grid")
        assert _band.colour != palette.TRANSPARENT, (
            f"{_backdrop.name} names the transparent entry; a backdrop is "
            "opaque, because there is nothing behind it")
        _ratio = contrast(palette.RGB[_band.colour], TEXT_COLOUR)
        assert _ratio >= MIN_CONTRAST, (
            f"{_backdrop.name} has a band at {_ratio:.2f}:1 against the "
            f"scene's own text, under the {MIN_CONTRAST}:1 a client needs to "
            "keep the words readable")

assert len(BACKDROPS_BY_NAME) == len(BACKDROPS), "duplicate backdrop name"
