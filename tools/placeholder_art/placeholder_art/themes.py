# SPDX-License-Identifier: MIT
"""Biome and season themes: the terrain library in more than one climate.

A theme is a **ramp substitution**, and nothing else. It names, for some of the
master palette's ramps, another ramp of exactly the same length to use in its
place; every terrain recipe then paints the same height fields, the same props,
and the same autotile rims, and only the colours differ. Two consequences make
this the cheapest possible way to grow the library sideways:

* a theme costs no drawing code: a terrain added later is themed the moment it
  exists, because it asks for its ramps by name; and
* a themed tile is the same shape as the untinted one, so a project that
  changes theme keeps every silhouette, every seam, and every autotile variant
  it already had.

The first theme, ``temperate``, substitutes nothing. It is what every asset in
the tree was generated as before themes existed, so its output is byte-for-byte
what it always was and a project that names no theme is unchanged.

Adding a theme
--------------
1. Append the ramps it needs to :mod:`.palette`. Appended, never inserted, so
   no existing palette index moves.
2. Append a :class:`Theme` to :data:`THEMES`. Appending keeps the menu index of
   every existing theme, which is what the schema, the editor, and the console
   agree on.
3. Regenerate. Sheets, manifests, the gallery, and the generated tables for the
   editor and the console all enumerate this registry.

The legibility reduction needs no per-theme work: :mod:`.profiles` derives a
substituted ramp's four-tone assignment from the ramp it replaces, so a themed
tile keeps the tone separation the original was given.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, Mapping, Tuple

from .palette import RAMPS, Ramp


@dataclass(frozen=True)
class Theme:
    """One climate the whole terrain library can be drawn in."""

    name: str
    label: str
    summary: str
    #: Ramp name to the ramp used in its place. Absent ramps are unchanged.
    substitutions: Mapping[str, str] = field(default_factory=dict)


THEMES: Tuple[Theme, ...] = (
    Theme(
        name="temperate",
        label="Temperate",
        summary="Green fields, blue water, dark evergreen woods.",
    ),
    Theme(
        name="autumn",
        label="Autumn",
        summary="A dry golden field under a russet canopy.",
        substitutions={"grass": "autumn_grass", "foliage": "autumn_leaf"},
    ),
    Theme(
        name="winter",
        label="Winter",
        summary="Frost over the ground, blue-green pine, water gone to ice.",
        substitutions={
            "grass": "winter_grass",
            "foliage": "winter_pine",
            "water": "winter_water",
        },
    ),
    Theme(
        name="ashland",
        label="Ashland",
        summary="Burnt scrub, charred woods, ash where the sand was.",
        substitutions={
            "grass": "ash_scrub",
            "foliage": "ash_char",
            "water": "ash_water",
            "sand": "ash_dust",
        },
    ),
)

#: The theme a project that names none is drawn in. It is the first entry
#: because the menu index is what every client agrees on, and the default has
#: to be the one that existed before the menu did.
DEFAULT_THEME: Theme = THEMES[0]

THEMES_BY_NAME: Dict[str, Theme] = {theme.name: theme for theme in THEMES}

for _theme in THEMES:
    for _original, _replacement in _theme.substitutions.items():
        assert _original in RAMPS, f"{_theme.name} substitutes unknown ramp {_original}"
        assert _replacement in RAMPS, f"{_theme.name} uses unknown ramp {_replacement}"
        assert len(RAMPS[_original]) == len(RAMPS[_replacement]), (
            f"{_theme.name} replaces ramp {_original} "
            f"({len(RAMPS[_original])} steps) with {_replacement} "
            f"({len(RAMPS[_replacement])} steps); a theme may only recolour"
        )


def theme(name: str) -> Theme:
    return THEMES_BY_NAME[name]


def ramp_name(active: Theme, name: str) -> str:
    """The ramp painted in place of ``name`` under ``active``."""
    return active.substitutions.get(name, name)


def ramp(active: Theme, name: str) -> Ramp:
    """The palette indices painted in place of ramp ``name``."""
    return RAMPS[ramp_name(active, name)]


def asset_suffix(active: Theme) -> str:
    """The filename suffix a theme's assets carry.

    The default theme carries none: its files are the ones that existed before
    themes did, and renaming them would move art no author asked to change.
    """
    return "" if active.name == DEFAULT_THEME.name else f"_{active.name}"
