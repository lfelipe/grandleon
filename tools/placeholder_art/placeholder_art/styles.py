# SPDX-License-Identifier: MIT
"""Character styles: the unit roster drawn in more than one genre.

A style is **a set of drawing routines, one per archetype**, and the archetype
roster is the same in every style. The ``medieval`` style draws a knight in
plate and an archer with a longbow; a sci-fi style would draw the same two
tactical roles as a trooper and a sniper. What the rules do is identical; the
pixels are not.

This is the one place a style differs in kind from a theme (:mod:`.themes`),
and the difference decides everything else. A theme is a substitution between
equal-length palette ramps, so it costs no drawing code and a terrain added
later is themed the moment it exists. A style cannot be that: a gunslinger is
not an archer recoloured. So a style costs one draw routine per archetype and a
legibility pass, the 16x16 four-tone reduction the gate measures a silhouette
on. It is priced as a commission rather than offered as a setting, which is why
this registry starts at one entry.

The first style, ``medieval``, is the roster :mod:`.characters` already held.
Its files keep the names they always had, so its output is byte-for-byte what
it always was and a project that names no style is unchanged.

Why the roster is shared
------------------------
If each style brought its own archetypes, the sprite key would stop factoring:
``character_art[archetype][faction]`` would become a ragged table, the keyword
convention that resolves a class name to an archetype would need a per-style
list, and the console would need a table naming each style's roster. The value
of the capability is that a class called "Sniper Kade" resolves to the ranged
archetype whatever style is drawing it, so one keyword table serves every style
forever. The cost is stated plainly: a style whose genre has no natural
counterpart for one archetype still has to draw all of them.

Adding a style
--------------
1. Write one :class:`~.characters.Archetype` subclass per name in
   :data:`~.characters.ARCHETYPE_ORDER`. Fewer is refused at import time, so a
   style cannot ship with an archetype undrawn.
2. Append a :class:`Style` to :data:`STYLES`. Appending keeps the menu index of
   every existing style, which is what the schema, the editor, and the console
   agree on.
3. Add the name to the ``characterStyleId`` enum in
   ``schemas/source/v1/project.schema.json`` and to the menu in
   ``tools/game_content/src/compiler.cpp``, then regenerate. Sheets, manifests,
   the generated tables for the editor and the console, and the editor's
   contracts all enumerate this registry.

Brief a commission at **sixteen colours per sprite**: the ``n64_ci4`` profile
caps a sprite there, and most of the checked-in character sprites already spend
all sixteen. Review a new style in ``n64_ci4`` and at the legibility reduction
before ``modern``.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Mapping, Tuple

from . import (characters, figures, frames, mythical, nature, pirates, scifi,
               sengoku, undead)
from .characters import Archetype
from .raster import Canvas


@dataclass(frozen=True)
class Style:
    """One genre the whole character roster can be drawn in."""

    name: str
    label: str
    summary: str
    #: Archetype name to the routine that draws it. Every style holds every
    #: name in :data:`~.characters.ARCHETYPE_ORDER`, and no others.
    archetypes: Mapping[str, Archetype]
    #: The animation cells this style ships, in sheet order. Declared rather
    #: than assumed so the refusal below has something to refuse, and fixed at
    #: the full sequence because a style that shipped fewer would make the
    #: frame index ragged in exactly the way the shared roster exists to
    #: prevent: a client would need a per-style table saying which cell of the
    #: sheet is the lunge. See :func:`~.frames.FRAME_ORDER`.
    frames: Tuple[str, ...] = frames.FRAME_NAMES
    #: The figures this style ships, in menu order. Declared and fixed for the
    #: same reason the cells are: a client indexes the figure menu by position,
    #: so a style holding fewer would make that index ragged. A style whose
    #: commission has drawn no second figure still holds every one of them,
    #: because :data:`~.figures.FIGURE_ORDER` carries a stand-in transform for
    #: exactly that case (:mod:`.figures`). The refusal below is what turns
    #: "cannot happen" into "does not happen".
    figures: Tuple[str, ...] = figures.FIGURE_NAMES


STYLES: Tuple[Style, ...] = (
    Style(
        name="medieval",
        label="Medieval",
        summary="Plate, longbows and staves: the high-fantasy roster.",
        archetypes=characters.ARCHETYPES,
    ),
    Style(
        name="scifi",
        label="Sci-fi",
        summary="Powered armour, rail rifles and drones: the same roster in a war fleet.",
        archetypes=scifi.ARCHETYPES,
    ),
    Style(
        name="mythical",
        label="Mythical",
        summary="Scale, horn and dragonfire: the same roster among dragonkin.",
        archetypes=mythical.ARCHETYPES,
    ),
    Style(
        name="nature",
        label="Nature",
        summary="Animal folk: the same roster carried by badgers, cats, bears and a boar.",
        archetypes=nature.ARCHETYPES,
    ),
    Style(
        name="sengoku",
        label="Sengoku Japan",
        summary="Lacquer, the naginata and the yumi: the same roster in the "
                "Warring States.",
        archetypes=sengoku.ARCHETYPES,
    ),
    Style(
        name="undead",
        label="Undead",
        summary="Bone, shroud and rust: the same roster as the force a player fights.",
        archetypes=undead.ARCHETYPES,
    ),
    Style(
        name="pirates",
        label="Pirates",
        summary="Tar, brass and sailcloth: the same roster as a ship's crew.",
        archetypes=pirates.ARCHETYPES,
    ),
)

#: The style a project that names none is drawn in. It is the first entry
#: because the menu index is what every client agrees on, and the default has
#: to be the one that existed before the menu did.
DEFAULT_STYLE: Style = STYLES[0]

STYLES_BY_NAME: Dict[str, Style] = {style.name: style for style in STYLES}

for _style in STYLES:
    for _archetype in characters.ARCHETYPE_ORDER:
        assert _archetype in _style.archetypes, (
            f"style {_style.name} draws no {_archetype}; every style holds "
            f"every archetype, so the sprite key factors"
        )
    for _drawn in _style.archetypes:
        assert _drawn in characters.ARCHETYPE_ORDER, (
            f"style {_style.name} draws {_drawn}, which is not an archetype; "
            f"a style may not add one"
        )
    # The same refusal, applied to time rather than to the roster. A style may
    # not ship a shorter sequence than the default: every client indexes a
    # sequence sheet by cell position, so a missing cell is not a missing
    # animation, it is every later animation shifted by one.
    assert tuple(_style.frames) == frames.FRAME_NAMES, (
        f"style {_style.name} ships frames {tuple(_style.frames)}; every "
        f"style ships {frames.FRAME_NAMES}, in that order, because the sheet "
        f"is indexed by position"
    )
    # And the same refusal applied to the body rather than to the gesture. A
    # figure costs a style no drawing at all, so a style holding fewer figures
    # than the library would be a ragged table bought with nothing saved.
    assert tuple(_style.figures) == figures.FIGURE_NAMES, (
        f"style {_style.name} ships figures {tuple(_style.figures)}; every "
        f"style ships {figures.FIGURE_NAMES}, in that order, because the "
        f"figure menu is indexed by position"
    )


def style(name: str) -> Style:
    return STYLES_BY_NAME[name]


def sprite(active: Style, archetype: str, colour: str,
           frame: str = "", figure: str = "") -> Canvas:
    """One archetype in one faction colour, drawn as ``active`` draws it.

    ``frame`` names a cell of :data:`~.frames.FRAME_ORDER`; the empty default is
    the standing sprite, which is frame 0 of every sequence. ``figure`` names an
    entry of :data:`~.figures.FIGURE_ORDER`; the empty default is the first
    figure, which is the body the archetype's own routine draws.
    """
    pose = frames.FRAMES_BY_NAME[frame].pose if frame else None
    body = figures.FIGURES_BY_NAME[figure] if figure else None
    return characters.sprite(archetype, colour, active.archetypes, pose, body)


def asset_suffix(active: Style) -> str:
    """The filename suffix a style's assets carry.

    The default style carries none: its files are the ones that existed before
    styles did, and renaming them would move art no author asked to change.
    """
    return "" if active.name == DEFAULT_STYLE.name else f"_{active.name}"
