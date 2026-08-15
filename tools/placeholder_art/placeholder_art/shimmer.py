# SPDX-License-Identifier: MIT
"""Animated terrain by palette cycling: which entries move, and where they live.

There are two routes to animated terrain and this module is the one taken:
**water shimmers by rotating a few colour entries at display time.** The other
route, a second set of drawn terrain tiles, does not fit. The Nintendo 64's
terrain sheet is sized to fill CI4 texture memory exactly, so a second terrain
frame costs an extra sheet upload every frame for every animated kind.

The hard rule
-------------
**The rotation is presentation-side only.** Nothing here permutes, reorders or
extends the master palette, and nothing here changes one byte of any asset. What
it produces is a *table*: for each theme, which entries of that theme's water
ramp a client should rotate, expressed in the three coordinate systems the three
kinds of client hold a palette in.

That is why this module emits addresses and not pixels. A client applies the
rotation to the palette it loaded; the palette it loaded stays the palette the
generator wrote.

Why four entries, and why the darkest is pinned
-----------------------------------------------
A water ramp is five steps, dark to light. Rotating all five makes the whole
surface change brightness together, which reads as a strobe rather than as
light on water. Rotating the four lightest and pinning the darkest keeps a fixed
shadow the eye can hold the tile by, and moves the highlights through it, which
is what light on water actually does.

Four is also one number for every theme. :func:`window_for` refuses a theme
whose water ramp cannot reach four rather than shimmering a shorter ramp,
because a shimmer of a different length on a different theme is a shimmer every
client would need a per-theme table of periods for.

The phase itself is not here. It is thirty-two frames in four steps of eight,
phase zero the identity permutation, and a function of the board's own frame
counter and nothing else. Being timing, it lives with the other frame counts in
``platform/view/include/grandleon/view/motion.hpp``, where the period is the
same thirty-two the cursor pulses on.
"""

from __future__ import annotations

from typing import Dict, List, Mapping, Sequence, Tuple

from . import palette, profiles, themes

#: How many entries of a water ramp are rotated. See the note above.
CYCLE_ENTRIES = 4

#: The ramp every theme animates. A theme that recolours water substitutes an
#: equal-length ramp for this one (:mod:`.themes`), so the name resolves per
#: theme and the length is the same in all of them.
CYCLED_RAMP = "water"


def ramp_for(theme: themes.Theme) -> Tuple[int, ...]:
    """The master palette indices of ``theme``'s water ramp, dark to light."""
    name = theme.substitutions.get(CYCLED_RAMP, CYCLED_RAMP)
    return tuple(palette.RAMPS[name])


def window_for(theme: themes.Theme) -> Tuple[int, ...]:
    """The master palette indices this theme rotates, dark to light.

    The four lightest steps of its water ramp. Raises rather than shortening if
    a theme's ramp is too short to hold them, because a shimmer of a different
    length on a different theme is a shimmer a client would need a per-theme
    table of periods for.
    """
    ramp = ramp_for(theme)
    if len(ramp) < CYCLE_ENTRIES:
        raise AssertionError(
            f"theme {theme.name} draws water on a {len(ramp)}-step ramp; the "
            f"shimmer rotates {CYCLE_ENTRIES} entries and cannot be shortened "
            f"for one theme without every client holding a table of periods"
        )
    return ramp[-CYCLE_ENTRIES:]


def _distinct_in_order(values: Sequence[int]) -> List[int]:
    seen: List[int] = []
    for value in values:
        if value not in seen:
            seen.append(value)
    return seen


def subset_window(theme: themes.Theme,
                  colours: Sequence[Tuple[int, int, int]]) -> Tuple[int, ...]:
    """The slots ``theme``'s shimmer rotates in a per-asset subset palette.

    ``colours`` is the palette a subset profile wrote for the water base sheet.
    On the Nintendo 64 that is the sheet's own sixteen-entry TLUT bank, of which
    water spends five. Slots are indices into that palette, which is exactly what
    a client hands the hardware.
    """
    lookup = {tuple(palette.RGB[index]): index for index in range(palette.PALETTE_SIZE)}
    by_master: Dict[int, int] = {}
    for slot, colour in enumerate(colours):
        master = lookup.get(tuple(colour))
        if master is not None:
            by_master.setdefault(master, slot)
    entries: List[int] = []
    for index in window_for(theme):
        if index not in by_master:
            raise AssertionError(
                f"theme {theme.name} rotates master palette entry {index}, "
                f"which the water base sheet's own palette does not hold; the "
                f"shimmer would rotate a colour the sheet never draws"
            )
        entries.append(by_master[index])
    return tuple(entries)


def manifest(subset_of_theme: Mapping[str, Sequence[Tuple[int, int, int]]]
             ) -> Dict[str, object]:
    """The cycled window in every coordinate system a client might hold.

    ``subset_of_theme`` maps a theme name to its water base sheet's subset
    palette. It may be empty for a profile that has none.
    """
    document: Dict[str, object] = {
        "ramp": CYCLED_RAMP,
        "entries": CYCLE_ENTRIES,
        "measured": (
            "Presentation-side only: these are addresses a client rotates in "
            "the palette it loaded. No master palette entry moves and no asset "
            "byte changes."
        ),
        "themes": {},
    }
    for theme in themes.THEMES:
        row: Dict[str, object] = {"master": list(window_for(theme))}
        subset = subset_of_theme.get(theme.name)
        if subset is not None:
            row["subset_slots"] = list(subset_window(theme, subset))
        document["themes"][theme.name] = row  # type: ignore[index]
    return document
