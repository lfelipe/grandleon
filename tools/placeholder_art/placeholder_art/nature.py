# SPDX-License-Identifier: MIT
"""The ``nature`` character style: the eight roles carried by animal folk.

What makes this style cheap is the reading behind it: a mage bear is not a
ninth archetype, it is the ``mage`` role in an animal body. The roster stays
closed at eight, and every one of the eight follows one rule, which is that
**the animal is chosen for the silhouette the role already needs**, never the
other way round. A stag's antlers are the stormcaller's wide radiating star; an
owl's facial disc is the healer's soft dome; a badger is stocky enough to stand
behind the knight's shield. Where an animal would have been charming and
illegible it was not used.

The ``beast`` is the one member of the roster that is not folk: a boar, on four
legs, because a roster where everybody is an animal still needs one silhouette
that is unmistakably *not a person*.

No new palette entries
----------------------
None, for the reason *Adding a style* in ``tools/placeholder_art/README.md``
gives. Fur is the one thing this style needs a lot of, and the palette
already holds four browns and a grey:

``rock``
    the badger's grey pelt.
``dirt``
    tan fur: the cat, the stag, the lion's body, the stoat.
``leather``
    dark brown, lifted by one step of ``dirt``: the bear.
``sand``
    antler, tusk, horn, mane and the lion's gold.
``hide``
    the boar's bristled hide, as it is the ``beast``'s in ``medieval``.
``snow``
    the owl's plumage and the pale marks: a badger's stripe, a stoat's throat,
    and the bone knives a stoat fights with.
``foliage``
    leaves, and **only** leaves. It is a terrain ramp, so it is spent on marks
    at the ends of things rather than on any body: a unit that reads as ground
    is not a unit, and this is the style most at risk of it.
``wood``
    staves, bows and a shield's bark.
``gold``
    the two-step warm mark: a beak, a spearpoint, a banner's finial, an eye.

Five ramps a sprite, not eight
------------------------------
The list above is what the *style* holds; what binds is what one **sprite**
spends, because ``n64_ci4`` keeps a sprite's sixteen most-used colours and
remaps the rest. Drawn first with a fur, a brown, a bark, a horn, a white, a
leaf and a leather all in the same 32×32 cell, the badger and the lion were
losing several times what ``medieval`` loses. Merging the near-duplicates (grey
boots on a grey badger, a stone spearhead rather than a horn one, the gold this
style already spends instead of a second pale ramp) took the style to 0.6% of
opaque texels remapped against ``medieval``'s 0.4%. **A sprite may name about
five ramps.** Choosing which five is most of what drawing a style is.

The margin rule
---------------
Stated and enforced by :mod:`.frames`: ``walk_contact`` moves everything at or
below row 24 outward by a column, so nothing here is drawn on column 0 or
column 31 below the knee: no paw, no tail tip, no shield rim.
"""

from __future__ import annotations

from . import raster
from .characters import (GROUND_Y, Archetype, FactionColour, arm, boots, cloak,
                         legs, torso)
from .palette import RAMPS
from .raster import Canvas

#: The badger's grey.
GREY = RAMPS["rock"]

#: Tan fur: the cat, the stag, the lion, the stoat.
TAN = RAMPS["dirt"]

#: The bear's dark brown. ``leather`` alone is three steps and too dark to
#: shade a whole body with, so it takes ``dirt``'s lit top step, exactly as the
#: ``beast``'s hide takes one from ``ink``.
BROWN = RAMPS["leather"] + (RAMPS["dirt"][3],)

#: Antler, tusk, claw, and the lion's mane.
HORN = RAMPS["sand"]

#: The owl's plumage, and every pale marking in the style.
DOWN = RAMPS["snow"] + (RAMPS["ink"][3],)

#: Bark: shields, staves, a bow's belly.
BARK = RAMPS["wood"] + (RAMPS["dirt"][3],)

#: Leaves. Spent at the ends of things and never on a body.
LEAF = RAMPS["foliage"]


def paw(canvas: Canvas, x: float, y: float, ramp) -> None:
    """A furred hand. The humanoid roster's :func:`~.characters.hand` is skin."""
    raster.disc(canvas, x, y, 1.6, ramp, ambient=0.45)


def snout(canvas: Canvas, cx: float, cy: float, ramp, length: float = 2.4,
          radius: float = 1.7) -> None:
    """A muzzle pushed forward off a face, with a dark nose on the end.

    The style's shared mark, as the visor is ``scifi``'s and the horns are
    ``mythical``'s. A round head is a round head at sixteen pixels; a round head
    with something sticking out of one side of it is an animal.
    """
    raster.capsule(canvas, cx, cy, cx + length, cy + 0.5, radius, ramp,
                   ambient=0.46)
    raster.disc(canvas, cx + length + 0.2, cy + 0.6, 1.0, RAMPS["ink"],
                ambient=0.55)


def round_ears(canvas: Canvas, cx: float, cy: float, ramp,
               spread: float = 3.6, radius: float = 1.9) -> None:
    """Two discs above a skull: the bear's ears, and the badger's."""
    for side in (-1, 1):
        raster.disc(canvas, cx + side * spread, cy, radius, ramp, ambient=0.40)


def sharp_ears(canvas: Canvas, cx: float, cy: float, ramp, spread: float = 2.8,
               height: float = 4.2, width: float = 1.5) -> None:
    """Two triangles above a skull: the cat's ears, the stoat's, the lion's."""
    for side in (-1, 1):
        raster.polygon(canvas, ((cx + side * (spread - width), cy),
                                (cx + side * (spread + 0.6), cy - height),
                                (cx + side * (spread + width), cy)), ramp, 0.58)


def eye_pair(canvas: Canvas, cy: int, spread: int = 2, cx: int = 16) -> None:
    for side in (-1, 1):
        canvas.put(cx + side * spread, cy, RAMPS["ink"][0])
        canvas.put(cx + side * spread, cy - 1, RAMPS["ink"][3])


class BadgerGuard(Archetype):
    """Melee, shield forward: a stocky badger behind a round bark shield.

    The knight's own read, a big disc beside a body, kept because it is the
    cheapest one there is. What makes it this style's is what carries it: the
    widest torso here, a striped skull, and a shield of bark rather than steel.
    """

    name = "knight"
    label = "Badger guard"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, GREY, top=19, bottom=26, spread=3, radius=2.1)
        boots(canvas, GREY)
        torso(canvas, colours, top=13.0, bottom=21.0, radius=5.0)
        raster.capsule(canvas, 16, 14.5, 16, 19.5, 3.2, GREY, ambient=0.30)
        for side in (-1, 1):  # heavy shoulders
            raster.disc(canvas, 16 + side * 4.8, 14.0, 2.6, GREY, ambient=0.30)
        arm(canvas, GREY, 20.0, 15.5, 22.5, 12.5)
        paw(canvas, 22.8, 12.2, GREY)
        raster.capsule(canvas, 22.5, 12.5, 26.0, 6.0, 1.2, BARK, ambient=0.44)
        raster.polygon(canvas, ((24.5, 7.5), (27.5, 3.0), (28.0, 8.0)), GREY,
                       0.85, dither=False)
        # The bark shield: the biggest disc in the roster, rimmed and leafed.
        raster.disc(canvas, 9.0, 17.5, 5.4, BARK, ambient=0.34, squash=1.06)
        raster.disc(canvas, 9.0, 17.5, 3.4, colours, ambient=0.42, squash=1.06)
        raster.disc(canvas, 9.0, 17.5, 1.5, DOWN, ambient=0.58, squash=1.06)
        raster.disc(canvas, 16, 9.2, 4.3, GREY, ambient=0.38)
        raster.rect(canvas, 15, 4, 2, 8, DOWN[2])  # the stripe down the mask
        snout(canvas, 17.0, 9.6, GREY, length=2.6)
        round_ears(canvas, 16, 6.0, GREY, spread=3.8, radius=1.8)
        eye_pair(canvas, 9, spread=3)


class ArcherCat(Archetype):
    """Ranged, cannot strike adjacent: a slender cat with a reed bow.

    The animal is named by the art direction. The archer's vertical arc down the
    off side is kept because a line is what survives, and the cat supplies the
    two things that separate it from every other line here: sharp ears at the
    top and a tail curling out at the bottom.
    """

    name = "archer"
    label = "Archer cat"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, TAN, top=19, bottom=26, spread=3, radius=1.6)
        boots(canvas, TAN)
        # Tail, curling out low and behind: the one loose line in the roster.
        raster.capsule(canvas, 20.0, 22.0, 25.0, 23.5, 1.4, TAN, ambient=0.34)
        raster.capsule(canvas, 25.0, 23.5, 27.5, 19.5, 1.2, TAN, ambient=0.40)
        raster.capsule(canvas, 21.0, 11.5, 19.5, 18.0, 1.8, BARK,
                       ambient=0.26)  # quiver
        for offset in range(3):
            canvas.put(21 - offset, 9 + offset, DOWN[2])
        torso(canvas, colours, top=13.5, bottom=20.5, radius=3.9)
        raster.capsule(canvas, 16, 14.5, 16, 19.0, 2.4, TAN, ambient=0.36)
        arm(canvas, TAN, 12.5, 15.0, 9.8, 14.5, 1.5)
        arm(canvas, TAN, 19.5, 15.0, 15.8, 14.5, 1.5)
        paw(canvas, 9.4, 14.4, TAN)
        for step in range(19):  # the bow, a tall arc drawn on the off side
            bulge = 3.2 - abs(step - 9) * 0.33
            canvas.put(int(round(8.0 - bulge)), 5 + step, BARK[1])
            canvas.put(int(round(8.0 - bulge)) + 1, 5 + step, BARK[2])
        raster.line(canvas, 8, 5, 8, 23, RAMPS["ink"][2])
        raster.disc(canvas, 16, 9.4, 3.7, TAN, ambient=0.40)
        snout(canvas, 17.2, 10.0, TAN, length=1.9, radius=1.4)
        sharp_ears(canvas, 16, 6.6, TAN, spread=2.9, height=4.4)
        raster.rect(canvas, 13, 12, 6, 2, colours[2])
        raster.rect(canvas, 13, 12, 6, 1, colours[3])
        eye_pair(canvas, 9)


class MageBear(Archetype):
    """Magic, short band: the broadest body here, hooded, with an acorn staff.

    The animal is named by the art direction too. A bear is the opposite of the
    narrow cone the mage usually is, so the separation from the stormcaller is
    rebuilt rather than inherited: the stag is **tall and antlered** and this is
    **broad and round-eared**, which are the two halves a skull can be reduced
    to.
    """

    name = "mage"
    label = "Mage bear"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=11.5, bottom=27.0, width=7.6)
        torso(canvas, BROWN, top=13.0, bottom=21.0, radius=5.2)
        raster.rect(canvas, 14, 12, 4, 14, colours[2])  # the open robe
        raster.rect(canvas, 14, 12, 2, 14, colours[3])
        arm(canvas, BROWN, 21.0, 15.5, 22.5, 20.0)
        arm(canvas, BROWN, 11.0, 15.5, 10.0, 18.5)
        paw(canvas, 22.7, 20.5, BROWN)
        # Staff of green wood with an acorn at the top, held on the off side.
        # The warden's banner is a pole on the weapon side, and two poles on the
        # same side would make one silhouette of two.
        raster.capsule(canvas, 9.0, 5.0, 10.0, 26.0, 1.2, BARK, ambient=0.40)
        raster.disc(canvas, 8.8, 4.2, 2.4, BARK, ambient=0.48)
        raster.disc(canvas, 8.8, 2.6, 2.0, LEAF, ambient=0.55, squash=1.3)
        raster.disc(canvas, 16, 9.0, 4.6, BROWN, ambient=0.40)
        snout(canvas, 17.4, 9.8, BROWN, length=2.6, radius=1.9)
        round_ears(canvas, 16, 5.4, BROWN, spread=4.2, radius=2.1)
        raster.rect(canvas, 11, 12, 10, 2, colours[1])  # collar
        raster.rect(canvas, 11, 12, 10, 1, colours[2])
        eye_pair(canvas, 9, spread=3)


class StormStag(Archetype):
    """Area effect: antlers wider than the frame's middle, and leaves thrown.

    The wide radiating star, and for once the animal *is* the device: a stag's
    antlers reduce to exactly the shape the stormcaller has always needed, and
    nothing else in this roster reaches past the shoulders on both sides.
    """

    name = "stormcaller"
    label = "Storm stag"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=13.0, bottom=27.0, width=7.2)
        torso(canvas, TAN, top=13.5, bottom=20.5, radius=4.0)
        raster.rect(canvas, 13, 13, 6, 8, colours[2])
        arm(canvas, TAN, 12.5, 15.0, 7.0, 12.0)
        arm(canvas, TAN, 19.5, 15.0, 25.0, 12.0)
        paw(canvas, 6.6, 11.6, TAN)
        paw(canvas, 25.4, 11.6, TAN)
        for direction in (-1, 1):  # leaves loosed from each hand
            origin = 16 + direction * 9.5
            for step, size in ((0.0, 1.6), (2.4, 1.2), (4.2, 0.9)):
                raster.disc(canvas, origin + direction * step, 9.5 - step * 0.9,
                            size, LEAF, ambient=0.60, squash=1.4)
        raster.disc(canvas, 16, 10.2, 3.6, TAN, ambient=0.40)
        snout(canvas, 17.2, 10.8, TAN, length=2.2, radius=1.4)
        # The antlers: three prongs a side, the widest thing in the roster.
        for direction in (-1, 1):
            raster.capsule(canvas, 16 + direction * 2.0, 7.5,
                           16 + direction * 6.5, 3.0, 1.1, HORN, ambient=0.46)
            for reach, rise in ((3.5, 1.0), (5.5, 0.5), (7.5, 2.0)):
                raster.capsule(canvas, 16 + direction * reach, 5.5,
                               16 + direction * (reach + 1.5), rise, 0.9, HORN,
                               ambient=0.52)
        eye_pair(canvas, 10)


class HealerOwl(Archetype):
    """Support, no offensive weapon: a wide facial disc over folded wings.

    The healer's soft dome, drawn as the animal that already is one. The widest
    head here by some distance, the palest mass, and the only figure whose
    shoulders are wider than its stance, which is what keeps it clear of the
    badger, whose width is all torso.
    """

    name = "healer"
    label = "Healer owl"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, DOWN, top=12.5, bottom=27.0, width=6.6)
        # Folded wings, wider than anything below them and softly edged.
        for side in (-1, 1):
            raster.disc(canvas, 16 + side * 5.6, 17.0, 4.4, DOWN, ambient=0.50,
                        squash=0.72)
        raster.rect(canvas, 14, 12, 4, 14, colours[2])
        raster.rect(canvas, 14, 12, 2, 14, colours[3])
        raster.capsule(canvas, 22.0, 13.0, 23.5, 8.0, 1.1, LEAF, ambient=0.30)
        for step in range(3):  # a sprig of herbs held up
            raster.disc(canvas, 23.0 + step * 0.4, 7.0 - step * 1.6,
                        1.7 - step * 0.3, LEAF, ambient=0.58, squash=1.3)
        raster.disc(canvas, 16, 9.0, 5.0, DOWN, ambient=0.56, squash=0.94)
        for side in (-1, 1):  # the facial disc's two rings
            raster.disc(canvas, 16 + side * 2.2, 9.2, 2.4, DOWN, ambient=0.72)
        raster.polygon(canvas, ((15.0, 9.5), (16.0, 12.0), (17.0, 9.5)),
                       RAMPS["gold"], 0.80, dither=False)
        raster.rect(canvas, 11, 13, 10, 2, colours[2])
        raster.rect(canvas, 11, 13, 10, 1, colours[3])
        for side in (-1, 1):
            canvas.put(16 + side * 2, 9, RAMPS["ink"][0])
            canvas.put(16 + side * 2, 8, RAMPS["gold"][1])
        canvas.put(16, 3, DOWN[2])


class LionWarden(Archetype):
    """The leader, and a win condition: a mane, a banner, and gold.

    Rank three ways, as every style states it, because one of them has to
    survive the reduction: the banner breaks the top of the frame, the cape
    carries the mass, and the mane is the detail. The mane is also what keeps
    this clear of the owl: both are round heads, and only one of them has a
    pole beside it.
    """

    name = "commander"
    label = "Lion warden"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        raster.capsule(canvas, 25.0, 1.0, 25.0, 27.0, 1.1, BARK, ambient=0.42)
        raster.polygon(canvas, ((24.0, 2.0), (31.0, 4.0), (24.0, 11.0)), colours,
                       0.72, dither=False)
        raster.polygon(canvas, ((24.0, 2.0), (28.0, 2.8), (24.0, 6.0)), colours,
                       0.95, dither=False)
        raster.disc(canvas, 25.0, 0.8, 1.4, RAMPS["gold"], ambient=0.60)
        cloak(canvas, colours, top=11.5, bottom=27.0, width=8.0)
        legs(canvas, TAN, top=19, bottom=26, spread=3)
        boots(canvas, TAN)
        torso(canvas, colours, top=12.5, bottom=21.0, radius=5.0)
        raster.capsule(canvas, 16, 13.5, 16, 19.5, 3.4, TAN, ambient=0.32)
        raster.rect(canvas, 13, 15, 6, 1, RAMPS["gold"][0])
        for side in (-1, 1):
            raster.disc(canvas, 16 + side * 5.0, 13.5, 2.8, TAN, ambient=0.32)
            raster.disc(canvas, 16 + side * 5.0, 13.0, 1.2, RAMPS["gold"],
                        ambient=0.55)
        arm(canvas, TAN, 11.0, 15.0, 8.5, 18.5)
        paw(canvas, 8.2, 19.0, TAN)
        raster.capsule(canvas, 8.0, 19.5, 8.0, 12.0, 1.2, BARK, ambient=0.50)
        raster.polygon(canvas, ((6.5, 12.5), (8.0, 7.5), (9.5, 12.5)),
                       RAMPS["gold"], 0.82, dither=False)
        raster.disc(canvas, 16, 8.6, 5.2, HORN, ambient=0.36)  # the mane
        raster.disc(canvas, 16, 9.0, 3.4, TAN, ambient=0.46)
        snout(canvas, 17.0, 9.6, TAN, length=2.0, radius=1.5)
        sharp_ears(canvas, 16, 5.0, HORN, spread=3.6, height=2.6, width=1.4)
        eye_pair(canvas, 9)


class Stoat(Archetype):
    """Fast, acts after striking: crouched, long-bodied, with a black tail tip.

    The rogue's crouch, unchanged in its purpose: it is the only figure here
    below the others' shoulder line. The stoat adds the one thing a crouch does
    not give on its own, which is a direction: a long tail out behind it.
    """

    name = "rogue"
    label = "Stoat"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, TAN, top=21, bottom=27, spread=4, radius=1.9)
        raster.capsule(canvas, 19.0, 21.0, 26.5, 22.5, 1.5, TAN, ambient=0.34)
        raster.capsule(canvas, 26.5, 22.5, 29.0, 19.0, 1.3, RAMPS["ink"],
                       ambient=0.40)  # the black tip
        cloak(canvas, RAMPS["leather"], top=14.5, bottom=25.0, width=5.6)
        torso(canvas, colours, top=15.0, bottom=21.0, radius=3.8)
        raster.capsule(canvas, 16, 16.0, 16, 20.5, 2.4, DOWN, ambient=0.44)
        raster.rect(canvas, 12, 17, 9, 2, RAMPS["leather"][1])
        arm(canvas, TAN, 12.0, 16.5, 8.5, 19.0, 1.5)
        arm(canvas, TAN, 20.0, 16.5, 23.0, 14.0, 1.5)
        paw(canvas, 8.2, 19.4, TAN)
        paw(canvas, 23.4, 13.6, TAN)
        for x0, y0, x1, y1 in ((7.6, 20.2, 5.0, 23.0), (24.0, 12.8, 26.5, 10.0)):
            raster.capsule(canvas, x0, y0, x1, y1, 1.0, DOWN, ambient=0.50)
        raster.disc(canvas, 16, 11.2, 3.5, TAN, ambient=0.42)
        snout(canvas, 17.2, 11.8, TAN, length=2.2, radius=1.3)
        sharp_ears(canvas, 16, 8.8, TAN, spread=2.6, height=2.4, width=1.3)
        raster.rect(canvas, 13, 13, 7, 2, colours[2])
        raster.rect(canvas, 13, 13, 7, 1, colours[3])
        eye_pair(canvas, 11)


class Boar(Archetype):
    """Non-humanoid: the one member of the roster on four legs.

    A style whose whole idea is animals still needs a silhouette that says *not
    a person*, or the archetype the rules treat as a beast reads as one more of
    the folk. So the boar keeps the ``beast``'s exact structure, a long low
    body, a faction ridge along the back, a second faction marker at head
    height, and states it in bristles and tusks.
    """

    name = "beast"
    label = "Boar"

    #: Bristled hide. As with the ``beast``'s, the ramp needs a lit top step.
    BRISTLE = RAMPS["hide"] + (RAMPS["ink"][2],)

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        bristle = self.BRISTLE
        for x in (10.0, 13.5, 19.0, 22.5):  # four short legs, trotters below
            raster.capsule(canvas, x, 20.5, x + 0.4, 25.5, 1.5, bristle,
                           ambient=0.30)
            raster.disc(canvas, x + 0.6, 26.2, 1.4, RAMPS["ink"], ambient=0.40,
                        squash=1.6)
        raster.capsule(canvas, 11.0, 19.0, 21.5, 18.5, 4.4, bristle,
                       ambient=0.34)
        raster.capsule(canvas, 9.5, 18.0, 6.0, 15.5, 1.1, bristle, ambient=0.36)
        raster.disc(canvas, 5.6, 15.0, 1.4, colours, ambient=0.50)  # tail tuft
        for step in range(6):  # the bristled ridge, in the faction colour
            raster.polygon(canvas, ((11.5 + step * 2.1, 14.8),
                                    (12.4 + step * 2.1, 10.2),
                                    (13.3 + step * 2.1, 14.8)), colours,
                           0.55 + 0.06 * (step % 2))
        raster.disc(canvas, 24.0, 16.5, 4.4, bristle, ambient=0.42)
        raster.capsule(canvas, 25.0, 18.0, 29.5, 18.6, 2.3, bristle,
                       ambient=0.36)  # the snout
        for side in (-1, 1):  # tusks, curling up out of the jaw
            raster.capsule(canvas, 28.0, 19.5 + side * 0.8, 29.5,
                           15.5 + side * 0.6, 1.0, HORN, ambient=0.55)
        raster.polygon(canvas, ((22.0, 13.5), (23.6, 9.5), (25.2, 13.5)),
                       bristle, 0.55)
        canvas.put(25, 15, RAMPS["gold"][1])
        canvas.put(26, 15, RAMPS["gold"][0])
        raster.capsule(canvas, 20.5, 14.5, 21.0, GROUND_Y - 8.5, 1.4, colours,
                       ambient=0.55)


#: One routine per name in ``characters.ARCHETYPE_ORDER``, in that order. The
#: registry in :mod:`.styles` asserts the set matches; the order here is for
#: readers, not for indexing.
ARCHETYPE_CLASSES = (
    BadgerGuard, ArcherCat, MageBear, StormStag, HealerOwl, LionWarden, Stoat,
    Boar,
)

ARCHETYPES = {cls.name: cls() for cls in ARCHETYPE_CLASSES}
