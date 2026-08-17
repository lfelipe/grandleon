# SPDX-License-Identifier: MIT
"""Figures: a second body for every role, drawn rather than derived.

Every role is drawn twice, at two builds of body. That is a fourth axis of the
sprite key beside archetype, style and faction colour, and the axis is called
the *figure*. This module is that axis.

The vocabulary
--------------
**A second figure is the same kit over a different person.** The role's
identifying equipment is drawn by one routine for both figures and does not
move by a pixel; what differs is the body, never the weapon. Three marks make
that body, and they are taken in order, each only where the kit leaves room.

*Mark one is the hair*: a fall, a braid, a tie or a mane, wherever the
headgear leaves the crown or the nape free. It is taken first because it is the
only mark that adds a thing rather than changing a shape, and the only one that
lands above the shoulder cut at :data:`.frames.SHOULDER_Y`, which is the only
band where a body's two gestures can differ at all. Hair is drawn in **a ramp
the sprite already spends**;
:mod:`.verify` refuses a second figure that names a ramp its first figure does
not.

*Mark two is the line*: narrower shoulders, a cut-in waist, and the hip left
alone, so the contrast is between the two. On half of any roster the body is
covered, so the garment carries the line instead:
:func:`~.characters.gown` states a robe as a shoulder, a waist and a hem for
exactly that.

*Mark three, optional, is the hem*: longer, flared or split, where the role
wears one.

**The prohibition is redistribute, never enlarge.** A second figure's occupied
box must sit inside its first figure's on the standing cell, which
:func:`~.verify.check_figure_room` measures; a width let out at the hem must
have been taken in at the waist. Containment is asked of the standing cell
only, because the poses are already guaranteed by :func:`.frames.displace`,
which raises rather than dropping a pixel.

The order runs out honestly. If a role's honest answer is a small difference, a
small difference is the right answer; a measurement is not something to
exaggerate toward. Three pieces of arithmetic decide what a mark is worth:
interior detail is free and worthless, because both bounds measure opacity and
a change that does not reach the outline buys nothing; the faction disc reaches
row 25 at the centre only and most of the width at row 26 and row 24 not at
all, so rows 22 to 24 are the most valuable outline a robed role has and
anything below row 26 is worth nothing; and depth breaks the role where length
buys the person, so a deviation should be long and shallow and depth should be
spent only on hair.

Why a drawing and not a transform
---------------------------------
A transform is the cheaper mechanism, and the transform's own measurements are
what disqualify it. A pinch applied to the drawn body moves 14 to 37 pixels of a
roughly 1,000-pixel silhouette, which the halved reduction collapses to 1 to 10
of 256; and it cannot be turned up, because ``medieval``'s floor leaves a
one-pixel window. Then the finding that decides it: **a transform can change a
shape and cannot add a thing.** It is style-blind, which is exactly the
property that keeps a role's kit safe, and a style-blind rule cannot tell a
head from a sword tip: a fall of hair at the nape grows a lobe on every
archetype with a point anywhere. What it carries is a build and not a person.

So a second figure is a **second drawing routine**: an
:class:`~.characters.Archetype` may hold a ``draw_second`` beside its ``draw``,
and choosing a figure chooses which of them renders the body.

What a second drawing has to buy back
-------------------------------------
The transform has one guarantee worth keeping: **a role's kit cannot drift**,
because one routine draws the shield, the bow and the staff for both figures and
the transform moves those pixels blind. Two routines can drift, and a knight
whose second figure carries a slightly different shield is two units rather than
one unit drawn twice.

That guarantee is bought back in :mod:`.characters`, by construction and then by
measurement: a role's identifying equipment moves into :func:`~.characters.kit`
methods that take no figure and know of none, both routines call them, and
:func:`~.verify.check_kit_shared` records every kit call with its arguments
while drawing each figure and refuses a role whose two recordings differ.

The stand-in, and the debt it names
-----------------------------------
Seven styles hold the roster and one commission drew the second figures:
``medieval``'s eight are drawn, and ``scifi``, ``mythical``, ``nature``,
``sengoku``, ``undead`` and ``pirates`` are stood in for. A style whose
archetypes hold no ``draw_second`` still ships every figure, because
:data:`FIGURE_ORDER` carries the transform as a **stand-in**: it renders the
first figure and pinches it. That is not a second opinion about what a figure
is. It is a placeholder with a measurement attached: the gate's native floor is
the number it scrapes. A commission that adds ``draw_second`` to a style's eight
archetypes retires the stand-in for that style without adding a file.

How a figure is spelled
-----------------------
A figure names the archetype method that draws it. The first figure names
:data:`FIRST_ROUTINE`, which is ``draw``, the routine every archetype has always
had. So the first figure is the sprite the library shipped before this axis
existed, in every style, with the filename it always carried.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Dict, Optional, Tuple

from .frames import SHOULDER_Y, displace
from .raster import Canvas

#: The bottom of the stand-in's pinched band, at the hip. Below it nothing
#: moves: a body's stance stands inside the faction disc, which no transform
#: touches, and a figure that narrowed its feet would read as a smaller person
#: rather than as a differently built one.
HIP_Y = 21

#: How many columns either side of the centreline the stand-in's pinch leaves
#: alone. Three, which is inside every archetype's torso, so it always reaches a
#: body's own edge and never leaves a figure untouched.
#:
#: It reaches the gear as well, and that is the reason this transform is a
#: stand-in rather than the answer: it cannot tell a waist from the bell of a
#: yoke, so on the archetypes whose width comes from an object, most of them by
#: the rule :mod:`.characters` records, it does its largest work on the object.
PINCH_KEEP = 3

#: How far the stand-in's pinch pulls, in columns. One. Two reads plainly as a
#: different build at 32×32 and breaks ``medieval``'s separation by one pixel,
#: which is the measurement that made a drawing the only way forward.
PINCH = 1

#: The centreline every body is drawn about.
CENTRE_X = 16

#: The :class:`~.characters.Archetype` method that draws the first figure. It
#: is the one every archetype in every style has always had. A figure that
#: names it is the first figure, which is what decides that its files carry no
#: suffix.
FIRST_ROUTINE = "draw"

#: The method a commission adds to draw a role's second figure.
SECOND_ROUTINE = "draw_second"


@dataclass(frozen=True)
class Figure:
    """One body a role can be drawn as: a name, and the routine that draws it."""

    name: str
    label: str
    summary: str
    #: The :class:`~.characters.Archetype` method that draws this figure's body.
    routine: str
    #: The transform that stands in where an archetype holds no such method.
    #: ``None`` on the first figure, which every archetype draws.
    stand_in: Optional[Callable[[Canvas], Canvas]] = None

    @property
    def suffix(self) -> str:
        """The filename suffix this figure's assets carry.

        The first figure carries none: its files are the ones that existed
        before figures did, and renaming them would move art no author asked to
        change. This is the rule a style and a theme already follow.
        """
        return "" if self.routine == FIRST_ROUTINE else f"_{self.name}"


def _pinch(body: Canvas) -> Canvas:
    """Narrower across the shoulders and through the waist, hips left alone.

    The retired transform, kept as the stand-in a style without a commission is
    drawn by. Every displacement is horizontal and toward the centreline, so it
    cannot fail the margin rule at any cell size: no pixel moves to a column
    further out than the one it came from, and the sprite's outermost opaque
    column can only come in.
    """

    def offset(x: int, y: int) -> Tuple[int, int]:
        if not SHOULDER_Y <= y < HIP_Y:
            return (0, 0)
        across = x - CENTRE_X
        if abs(across) < PINCH_KEEP:
            return (0, 0)
        return (-PINCH if across > 0 else PINCH, 0)

    return displace(body, offset)


#: The figures a role is drawn as, in the order every client indexes them by.
#: Appending is the only safe way to grow this: a client indexes the menu by
#: position, and the first entry is the sprite that shipped before figures
#: existed.
#:
#: **The names and the labels say different things on purpose.** ``name`` is the
#: stored identity: it reaches asset filenames, the source schema's enum and
#: every package built from one, so it is ``first`` and ``second`` and moving it
#: would rewrite five thousand files and every project on disk for nothing a
#: reader would see. ``label`` is what a person is shown, and these two figures
#: are drawn male and female — the second has the narrower line and the hair the
#: headgear leaves room for — so that is what it says. The neutral wording that
#: used to stand here described the drawings without naming them, which left an
#: author looking for a female character with nothing to search for.
FIGURE_ORDER: Tuple[Figure, ...] = (
    Figure(
        "first", "Male",
        "The male body, and the sprite that shipped before there was a second "
        "one.",
        FIRST_ROUTINE,
    ),
    Figure(
        "second", "Female",
        "The female body: the same role and the same kit, carried by a woman "
        "— a different line from shoulder to hem, and hair where the headgear "
        "leaves room.",
        SECOND_ROUTINE,
        _pinch,
    ),
)

#: The figure a caller that names none is drawn as. The first entry, because the
#: menu index is what every client agrees on and the default has to be the one
#: that existed before the menu did.
DEFAULT_FIGURE: Figure = FIGURE_ORDER[0]

FIGURES_BY_NAME: Dict[str, Figure] = {
    figure.name: figure for figure in FIGURE_ORDER
}

#: Figure names in menu order, which is what the manifest and every generated
#: table publish.
FIGURE_NAMES: Tuple[str, ...] = tuple(figure.name for figure in FIGURE_ORDER)

FIGURE_COUNT: int = len(FIGURE_ORDER)

assert DEFAULT_FIGURE.routine == FIRST_ROUTINE, (
    "the default figure must be the routine every archetype already has, or "
    "the sprites that shipped before figures existed would change"
)
assert DEFAULT_FIGURE.stand_in is None, (
    "the first figure is drawn, never stood in for"
)
assert DEFAULT_FIGURE.suffix == "", (
    "the default figure's assets must keep the filenames they always had"
)
for _figure in FIGURE_ORDER[1:]:
    assert _figure.routine != FIRST_ROUTINE, (
        f"figure {_figure.name} is drawn by the first figure's own routine, "
        f"which makes it the first figure under another filename"
    )
    assert _figure.stand_in is not None, (
        f"figure {_figure.name} has no stand-in; a style whose commission has "
        f"not reached it would ship a partial set, and the sprite key would "
        f"stop factoring"
    )
    assert _figure.suffix, (
        f"figure {_figure.name} carries no filename suffix; only the first "
        f"figure may, and it is the one whose files may not move"
    )
assert len({figure.suffix for figure in FIGURE_ORDER}) == FIGURE_COUNT, (
    "two figures share a filename suffix, so one would overwrite the other"
)
assert len({figure.routine for figure in FIGURE_ORDER}) == FIGURE_COUNT, (
    "two figures name one drawing routine, so one of them is not a figure"
)


def figure(name: str) -> Figure:
    return FIGURES_BY_NAME[name]
