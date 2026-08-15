# SPDX-License-Identifier: MIT
"""Character archetypes: the single source of truth for unit sprites.

Sprites are drawn once at the native 32x32 size and then pushed through every
output profile, exactly like terrain. Nothing here knows about a target
platform.

Reading at a glance
-------------------
Three devices carry the read, in order of how much work they do:

1. a **faction-coloured ground disc** under every unit, which is legible even at
   the halved reduction where the body itself is barely a dozen pixels tall;
2. a **faction-coloured garment** covering the torso; and
3. a **one-pixel ink outline** around the whole silhouette, added last, which
   is what stops the sprite dissolving into the terrain behind it.

The roster is closed at eight (knight, archer, mage, stormcaller, healer,
commander, rogue, beast) and a style may not add, remove or rename one: every
archetype added afterwards costs one draw routine in *every* character style
(:mod:`.styles`), so the list below is a fixed vocabulary rather than a growing
one.

Adding an archetype
-------------------
1. Subclass :class:`Archetype`, set ``name`` and ``label``.
2. Implement :meth:`Archetype.draw`. Build from the shared body helpers
   (:func:`legs`, :func:`torso`, :func:`head`, :func:`arm`) so proportions stay
   consistent, then add the gear that makes the silhouette distinctive.
   Silhouette does more work than detail: a shape that survives being reduced
   to four shades at half size is worth more than a face.
3. Put the faction ramp on a large, contiguous area. Faction colour on a
   belt is invisible; faction colour on a tabard is not.
4. Append the class to :data:`ARCHETYPE_CLASSES`.

Drawing a role's second figure
------------------------------
:mod:`.figures` states the vocabulary; this is where it lands in code.

1. Move the role's **identifying equipment** into methods marked :func:`kit`:
   the shield, the bow, the staff, the banner, the object it takes its width
   from. Call them from ``draw`` at the point in its own draw order where
   they already were. Do this first and check that no pixel of the first figure
   moved; a kit method takes no figure and cannot know of one, which is what
   stops two routines drawing two shields.
2. **Put the grip in the kit.** The hand on a bow or a hilt is the kit's, not
   the body's, so a second figure may set its arms wherever its shoulders want
   them and still hold the weapon where the weapon is.
3. Write ``draw_second``, calling the same kit methods with the same arguments.
   Differ in the body: hair first, in a ramp the sprite already spends; then the
   line, taken from the garment with :func:`gown` rather than from a body that
   is probably covered; then the hem, which exists to pay for the line.
4. Stay inside the first figure's standing box.
   :func:`verify.check_figure_room` refuses a second figure that grows it or
   that names a material the first did not, and :func:`verify.check_kit_shared`
   refuses one whose kit calls differ.

Roster
------
``knight`` melee, shield forward. ``archer`` ranged, bow arc. ``mage`` magic,
narrow cone of hat and staff. ``stormcaller`` area effect, widest at the waist
behind a storm brazier, deliberately unlike the mage. ``healer`` support, no
offensive weapon, softer rounded shape. ``commander`` a leader, marked by a cape
and a banner because a map can be won or lost on finding one. ``rogue`` and
``beast`` round out the set with a crouched humanoid and the only quadruped.

Width comes from a thing, not from a pose
-----------------------------------------
The first reading of the whole library on one page (``ROSTER.md``) found that
four styles drew ``stormcaller`` as the same figure with both arms raised and a
mark thrown from each hand, and that a pose four styles share reduces to a rig
rather than to a character. Three later commissions had each answered it the
same way and none of them had been read back into this file: `sengoku`'s dancer
is widest at the hem, `undead`'s bellringer carries a yoke, `pirates`' gunner
leans on a deck gun with both arms down. **Take the width from an object and put
the arms down** is the rule that falls out of all three, and it is now what this
style's ``stormcaller`` does too.

It has a second, measured consequence that an author needs. ``lunge`` and
``cast`` displace a body identically between the shoulder cut and the knee cut
(:mod:`.frames`), and everything a figure has below the knee cut stands inside
the faction disc, which no pose moves. So **the band above the shoulder is the
only one where a body's two gestures can differ**, and a body that spends that
band on raised arms has nothing left to spend there.
"""

from __future__ import annotations

import contextlib
import functools
import math
from dataclasses import dataclass
from typing import (Callable, Dict, Iterator, List, Mapping, Optional, Protocol,
                    Tuple)

from . import raster
from .palette import RAMPS, TRANSPARENT, Ramp
from .raster import Canvas
from .rng import Rng, seed_of

#: Native sprite size. A power of two, so the Nintendo 64 profiles need no
#: padding, and a multiple of sixteen, so it halves onto a whole grid.
SPRITE = 32

#: An animation frame's displacement of a drawn body (:mod:`.frames`). Spelled
#: as a callable rather than imported, so the roster stays free of a cycle with
#: the transforms that are applied to it.
Pose = Callable[[Canvas], Canvas]


class Figure(Protocol):
    """What :func:`sprite` needs of a figure (:mod:`.figures`).

    A structural type rather than an import, for the same reason :data:`Pose` is
    one: the roster may not depend on the axes applied to it.

    ``routine`` names the :class:`Archetype` method that draws this figure's
    body: a second figure is a second drawing, so choosing one is choosing a
    routine. ``stand_in`` is the body transform that draws it in a style whose
    commission has not reached it yet, and ``None`` on the first figure, which
    every style has always drawn.
    """

    routine: str
    stand_in: Optional[Pose]


#: Ground line: where feet and the faction disc sit.
GROUND_Y = 28


# ---------------------------------------------------------------------------
# The kit: what a role carries, drawn once for both of its figures
# ---------------------------------------------------------------------------
#
# The rule this machinery enforces is :mod:`.figures`'s first one: a second
# figure is a second *body* under the **same kit**, and a knight whose second
# figure carries a slightly different shield is two units rather than one unit
# drawn twice. When a figure was a transform that guarantee was free, because
# one routine drew the pixels and the transform moved them blind. A second
# drawing can drift, so the guarantee has to be bought back.
#
# It is bought back by construction and then measured. By construction: the
# shield, the bow, the staff and the banner move into methods of their own that
# take no figure and know of none, so both of a role's routines can only call
# the same code. Measured: :func:`verify.check_kit_shared` records every kit
# call and its arguments while drawing each figure and refuses a role whose two
# recordings differ. That catches the failure construction alone cannot, a
# routine that calls the right helper with a number changed by one.

_KIT_TRACE: Optional[List[Tuple[object, ...]]] = None


def kit(method: Callable[..., None]) -> Callable[..., None]:
    """Mark an archetype method as the role's identifying equipment.

    A kit method draws a thing the role is known by and **takes no figure**, so
    it cannot draw one figure's version of it. Both of a role's drawing routines
    call it, at the point in their own draw order where that thing belongs, with
    the same arguments; :func:`tracing_kit` is how the build proves the last
    clause.
    """

    @functools.wraps(method)
    def drawing(self: "Archetype", canvas: Canvas, *args: object,
                **named: object) -> None:
        if _KIT_TRACE is not None:
            _KIT_TRACE.append(
                (self.name, method.__name__, args,
                 tuple(sorted(named.items()))))
        method(self, canvas, *args, **named)

    return drawing


@contextlib.contextmanager
def tracing_kit() -> Iterator[List[Tuple[object, ...]]]:
    """Record every :func:`kit` call made inside the block, in order.

    The recording is the archetype's name, the method's name and the arguments:
    everything that decides what lands on the canvas except the canvas itself.
    Two recordings that agree are two figures carrying one shield.
    """
    global _KIT_TRACE
    if _KIT_TRACE is not None:
        raise RuntimeError("kit tracing does not nest")
    _KIT_TRACE = []
    try:
        yield _KIT_TRACE
    finally:
        _KIT_TRACE = None


@dataclass(frozen=True)
class FactionColour:
    """One colour a game's faction can wear.

    This is the menu, not the roster: the library knows colours, and a game's
    own factions choose among them. Naming the entries after their palette
    ramps rather than after any one game's factions is the point: an asset
    called ``knight_blue`` belongs to every game, an asset called
    ``knight_dawn_guard`` belongs to one.
    """

    name: str
    label: str

    @property
    def colours(self) -> Ramp:
        return RAMPS[self.name]


#: The committed menu. Order is the menu order everywhere: manifest, gallery
#: and sheets alike. It is also what a source project's faction order falls
#: back to when a faction names no colour of its own.
FACTION_COLOURS: Tuple[FactionColour, ...] = (
    FactionColour("blue", "Blue"),
    FactionColour("red", "Red"),
    FactionColour("green", "Green"),
    FactionColour("violet", "Violet"),
    FactionColour("amber", "Amber"),
    FactionColour("bone", "Bone"),
)
FACTION_COLOURS_BY_NAME: Dict[str, FactionColour] = {
    colour.name: colour for colour in FACTION_COLOURS
}

#: Colour names in the menu order above.
FACTION_COLOUR_ORDER: Tuple[str, ...] = tuple(
    colour.name for colour in FACTION_COLOURS
)


# ---------------------------------------------------------------------------
# Shared body parts
# ---------------------------------------------------------------------------


def drop_shadow() -> Canvas:
    """The shadow a client blits under a unit before the unit itself.

    It lives on the character grid and uses :data:`GROUND_Y` deliberately: a
    shadow that does not line up with the feet it belongs to is worse than no
    shadow at all. Being one sprite rather than one per archetype is the whole
    saving: the silhouettes differ above the ankles, not below them.

    No output profile has partial alpha; the master palette's index 0 is the
    only transparency there is. So the softness is a half-tone stipple on the
    same odd diagonal the console's range highlight uses, with a solid core
    that survives the halved downscale. A renderer needs nothing for it
    beyond the alpha compare it already does for the sprites.
    """
    canvas = Canvas(SPRITE, SPRITE)
    outer = Canvas(SPRITE, SPRITE)
    raster.ground_shadow(outer, 16, GROUND_Y + 0.8, 10.5, 3.9, RAMPS["ink"][1])
    for y in range(SPRITE):
        for x in range(SPRITE):
            index = outer.data[y * SPRITE + x]
            if index != TRANSPARENT and (x + y) % 2 == 0:
                canvas.put(x, y, index)
    raster.ground_shadow(canvas, 16, GROUND_Y + 0.8, 6.2, 2.2, RAMPS["ink"][0])
    return canvas


def faction_disc(canvas: Canvas, faction: FactionColour) -> None:
    """The faction-coloured stand under the unit, drawn before anything else."""
    colours = faction.colours
    raster.ground_shadow(canvas, 16, GROUND_Y + 0.8, 9.2, 3.4, RAMPS["ink"][1])
    raster.ground_shadow(canvas, 16, GROUND_Y + 0.8, 8.1, 2.7, colours[1])
    raster.ground_shadow(canvas, 16, GROUND_Y + 0.5, 6.1, 1.9, colours[2])
    raster.ground_shadow(canvas, 15, GROUND_Y + 0.2, 3.2, 1.0, colours[3])


def legs(canvas: Canvas, ramp: Ramp, top: int = 20, bottom: int = 27,
         spread: int = 3, radius: float = 1.8) -> None:
    for side in (-1, 1):
        raster.capsule(canvas, 16 + side * spread, top, 16 + side * spread * 1.15,
                       bottom, radius, ramp, ambient=0.30)
    # Separate the two legs, or they merge into one shapeless block.
    raster.line(canvas, 16, top + 1, 16, bottom, RAMPS["ink"][1])


def boots(canvas: Canvas, ramp: Ramp, spread: int = 3) -> None:
    for side in (-1, 1):
        raster.capsule(canvas, 16 + side * spread * 1.15, 26,
                       16 + side * spread * 1.35, 27, 1.9, ramp, ambient=0.34)


def torso(canvas: Canvas, ramp: Ramp, top: float = 13.0, bottom: float = 21.0,
          radius: float = 4.6) -> None:
    raster.capsule(canvas, 16, top, 16, bottom, radius, ramp, ambient=0.28)


def head(canvas: Canvas, cy: float = 9.0, radius: float = 4.3) -> None:
    raster.disc(canvas, 16, cy, radius, RAMPS["skin"], ambient=0.34)


def arm(canvas: Canvas, ramp: Ramp, x0: float, y0: float, x1: float, y1: float,
        radius: float = 1.8) -> None:
    raster.capsule(canvas, x0, y0, x1, y1, radius, ramp, ambient=0.32)


def hand(canvas: Canvas, x: float, y: float) -> None:
    raster.disc(canvas, x, y, 1.5, RAMPS["skin"], ambient=0.45)


def eyes(canvas: Canvas, cy: int = 9, spread: int = 2,
         colour: int = RAMPS["ink"][0]) -> None:
    for side in (-1, 1):
        canvas.put(16 + side * spread, cy, colour)


def face(canvas: Canvas, cy: float, radius: float = 2.9) -> None:
    """Cut a face back into a helmet or hood.

    Headgear is drawn as a solid dome for silhouette, then the face is punched
    back over it. Without this the hooded archetypes read as featureless
    bottles at 32x32 and as nothing at all once downscaled.
    """
    raster.disc(canvas, 16, cy, radius, RAMPS["skin"], ambient=0.50)
    eyes(canvas, cy=int(cy))


def hair(canvas: Canvas, ramp: Ramp, cy: float, radius: float = 4.4,
         cx: float = 16.0) -> None:
    """The mass of hair on a head, drawn before the headgear that sits on it.

    The first of the marks a second figure is drawn with (:mod:`.figures`), and
    the one a transform could never make: hair is a **thing added**, not a
    shape changed. The ramp is the caller's, and it must be one the sprite
    already spends, so a second figure cannot cost a material its first figure
    did not. :func:`verify.check_figure_room` refuses one that does.
    """
    raster.disc(canvas, cx, cy, radius, ramp, ambient=0.30)


def braid(canvas: Canvas, ramp: Ramp, x0: float, y0: float, x1: float,
          y1: float, radius: float = 1.5) -> None:
    """A fall of hair leaving the head, with a tie where it narrows.

    Drawn *after* the headgear, because what carries at the reduction is the
    part below the rim: a braid inside a helmet is a braid nobody sees. It is
    the only mark in this vocabulary that adds to the outline above the shoulder
    cut at row 12, and that band is the only one where a body's two gestures can
    differ at all: between rows 12 and 24 the lunge and the cast displace a body
    identically, and below row 24 it stands over the faction disc, which no pose
    moves.
    """
    raster.capsule(canvas, x0, y0, x1, y1, radius, ramp, ambient=0.28)
    raster.capsule(canvas, x1, y1, x1, y1 + radius, radius * 0.62, ramp,
                   ambient=0.22)


def cloak(canvas: Canvas, ramp: Ramp, top: float = 13.0, bottom: float = 25.0,
          width: float = 6.5) -> None:
    raster.polygon(
        canvas,
        ((16 - width * 0.55, top), (16 + width * 0.55, top),
         (16 + width, bottom), (16 - width, bottom)),
        ramp,
        0.40,
    )
    for y in range(int(top), int(bottom) + 1):
        canvas.shift_pixel(int(16 + width * 0.85), y, -1)


def gown(canvas: Canvas, ramp: Ramp, shoulder: float, waist: float,
         hem: float, top: float = 12.0, cinch: float = 20.0,
         bottom: float = 27.0) -> None:
    """A garment stated as three widths rather than one.

    :func:`cloak` is a trapezium: it has a top and a bottom, and everything
    between them is interpolated, so the only line it can draw is a straight
    one. This is the same garment with a **waist** in the middle of it, which is
    the second of the marks a second figure is drawn with (:mod:`.figures`):
    *the line*.

    It is what makes the line affordable rather than merely available. Half a
    roster wears something that covers the body entirely, so on those roles the
    body cannot carry a line at all and the garment has to; and a width taken in
    at the waist and given back at the hem is a **redistribution**, which is the
    only kind of change a figure may make: a second figure's occupied box has to
    sit inside its first figure's, and the standing cell has no margin to
    enlarge into anyway.
    """
    raster.polygon(
        canvas,
        ((16 - shoulder, top), (16 + shoulder, top), (16 + waist, cinch),
         (16 + hem, bottom), (16 - hem, bottom), (16 - waist, cinch)),
        ramp,
        0.40,
    )
    for y in range(int(top), int(bottom) + 1):
        across = (waist if y <= cinch
                  else waist + (hem - waist) * (y - cinch) / (bottom - cinch))
        canvas.shift_pixel(int(16 + across * 0.85), y, -1)


# ---------------------------------------------------------------------------
# Archetypes
# ---------------------------------------------------------------------------


class Archetype:
    """Base class for a unit archetype."""

    name: str = ""
    label: str = ""

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        raise NotImplementedError

    #: The routine that draws this role's **second figure**, in a style whose
    #: commission has drawn one. ``None`` until it has, and the stand-in
    #: transform on :data:`~.figures.FIGURE_ORDER` draws the second figure
    #: meanwhile. That is how a library of seven styles takes real second
    #: drawings one commission at a time rather than all at once.
    draw_second: Optional[Callable[[Canvas, FactionColour], None]] = None

    def rng(self, faction: FactionColour, key: str) -> Rng:
        return Rng(seed_of(self.name, faction.name, key))


class Knight(Archetype):
    name = "knight"
    label = "Knight"

    @kit
    def sword(self, canvas: Canvas) -> None:
        """The raised blade, and the gauntlet that grips it.

        The hand is the kit's rather than the body's, and that is the general
        rule: **the kit carries its own grip.** A body may put its
        arm wherever it likes as long as it reaches this hand, so two figures
        cannot end up holding one sword in two places.
        """
        hand(canvas, 22.0, 10.5)
        raster.capsule(canvas, 22.0, 10.0, 25.0, 2.0, 1.3, RAMPS["steel"],
                       ambient=0.45)
        raster.capsule(canvas, 20.5, 10.5, 23.5, 10.0, 0.9, RAMPS["gold"],
                       ambient=0.60)

    @kit
    def shield(self, canvas: Canvas, colours: Ramp) -> None:
        """The shield in the off hand, carrying the faction colour again."""
        raster.disc(canvas, 10.0, 17.0, 4.4, colours, ambient=0.34, squash=1.15)
        raster.disc(canvas, 10.0, 17.0, 1.6, RAMPS["gold"], ambient=0.55,
                    squash=1.15)

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, RAMPS["steel"], top=19, bottom=26, spread=3)
        boots(canvas, RAMPS["leather"])
        torso(canvas, colours, top=13.0, bottom=21.0, radius=4.8)
        # Breastplate over the tabard, leaving faction colour at the flanks.
        raster.capsule(canvas, 16, 14.0, 16, 19.0, 3.2, RAMPS["steel"], ambient=0.30)
        for side in (-1, 1):  # pauldrons
            raster.disc(canvas, 16 + side * 4.6, 14.0, 2.6, RAMPS["steel"],
                        ambient=0.30)
        # Sword arm, raised.
        arm(canvas, RAMPS["steel"], 20.0, 15.0, 22.0, 11.0)
        self.sword(canvas)
        self.shield(canvas, colours)
        head(canvas, cy=9.0, radius=4.2)
        # Helmet: dome plus a visor slit, plus a faction plume.
        raster.disc(canvas, 16, 8.2, 4.4, RAMPS["steel"], ambient=0.30)
        raster.rect(canvas, 13, 9, 6, 2, RAMPS["ink"][1])
        raster.rect(canvas, 14, 9, 4, 1, RAMPS["skin"][0])
        raster.capsule(canvas, 16, 3.6, 16, 5.4, 1.4, colours, ambient=0.55)

    def draw_second(self, canvas: Canvas, faction: FactionColour) -> None:
        """The same helm, sword and shield over a plate fauld and a braid.

        The role with the least body showing, and the one that proves the
        vocabulary rather than flatters it. Nothing of this knight is visible
        between the gorget and the boots, so the line has to be spent on the
        armour's own cut: the pauldrons come in, the cuirass ends at a waist
        instead of running to the hip, and a **fauld**, a flared skirt of
        plate, carries the width the first figure carries in a straight
        tabard. The braid leaves the helm at the nape, which is the one place
        this helmet is open.
        """
        colours = faction.colours
        legs(canvas, RAMPS["steel"], top=21, bottom=26, spread=3)
        boots(canvas, RAMPS["leather"])
        torso(canvas, colours, top=13.5, bottom=20.0, radius=4.2)
        # The fauld: where the first figure's tabard falls straight, this one
        # flares. Drawn over the legs, under the shield, and it stops at row 23
        # so the sequence's outward step at row 24 has nothing to push off.
        raster.polygon(canvas, ((11.5, 18.5), (20.5, 18.5), (23.6, 23.8),
                                (8.4, 23.8)), RAMPS["steel"], 0.36)
        raster.rect(canvas, 11, 19, 10, 1, colours[2])
        # Cuirass to a waist rather than to the hip, and narrower pauldrons.
        raster.capsule(canvas, 16, 14.2, 16, 18.4, 2.9, RAMPS["steel"],
                       ambient=0.30)
        for side in (-1, 1):
            raster.disc(canvas, 16 + side * 4.2, 14.2, 2.2, RAMPS["steel"],
                        ambient=0.30)
        arm(canvas, RAMPS["steel"], 19.6, 15.0, 22.0, 11.0)
        self.sword(canvas)
        self.shield(canvas, colours)
        head(canvas, cy=9.0, radius=4.2)
        raster.disc(canvas, 16, 8.2, 4.4, RAMPS["steel"], ambient=0.30)
        raster.rect(canvas, 13, 9, 6, 2, RAMPS["ink"][1])
        raster.rect(canvas, 14, 9, 4, 1, RAMPS["skin"][0])
        raster.capsule(canvas, 16, 3.6, 16, 5.4, 1.4, colours, ambient=0.55)
        braid(canvas, RAMPS["leather"], 20.4, 12.2, 23.0, 18.6, 1.6)


class Mage(Archetype):
    name = "mage"
    label = "Mage"

    @kit
    def hat(self, canvas: Canvas, colours: Ramp) -> None:
        """The wide pointed hat, which is this role's read and not its headgear.

        Kit rather than something a figure chooses, and the reason is the
        library's tightest measurement: healer against mage is the closest
        cross-role pair there is, at 12 of 256, and the brim is most of what
        separates them once a sprite is halved.
        """
        raster.polygon(canvas, ((16.0, 1.5), (22.5, 8.0), (9.5, 8.0)), colours, 0.55)
        raster.polygon(canvas, ((16.0, 1.5), (19.0, 8.0), (9.5, 8.0)), colours, 0.78)
        raster.rect(canvas, 9, 8, 14, 2, colours[1])
        raster.rect(canvas, 9, 8, 14, 1, colours[2])
        raster.disc(canvas, 16.0, 6.0, 1.2, RAMPS["gold"], ambient=0.60)

    @kit
    def staff(self, canvas: Canvas) -> None:
        """The lit orb on its shaft: the tallest silhouette in the roster."""
        hand(canvas, 22.0, 18.5)
        raster.capsule(canvas, 23.0, 4.0, 22.0, 26.0, 1.2, RAMPS["wood"],
                       ambient=0.40)
        raster.disc(canvas, 23.2, 3.2, 2.8, RAMPS["gold"], ambient=0.45)
        canvas.put(22, 2, RAMPS["ink"][3])

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=12.0, bottom=27.0, width=6.8)
        torso(canvas, colours, top=13.0, bottom=20.0, radius=4.2)
        arm(canvas, colours, 11.5, 15.0, 10.0, 20.0)
        arm(canvas, colours, 20.5, 15.0, 22.0, 18.0)
        self.staff(canvas)
        head(canvas, cy=9.5, radius=3.9)
        self.hat(canvas, colours)
        face(canvas, 11.0, 2.7)

    def draw_second(self, canvas: Canvas, faction: FactionColour) -> None:
        """The same hat and staff over a fitted robe, and hair below the brim.

        The hat is kit and not headgear this figure may choose, because at the
        reduction the brim is the only thing separating this role's head from
        the healer's, and healer against mage is the tightest cross-role pair
        in the whole library. So the hair goes where a wide brim leaves it: on
        the shoulder, below the rim, on the side the staff does not take.
        """
        colours = faction.colours
        # The robe with a waist in it: narrower than the first figure's from the
        # shoulder to the hip, and wider than it below the knee. Every column
        # this lets out at the hem it has already taken in above.
        gown(canvas, colours, shoulder=3.0, waist=4.0, hem=8.2, top=12.0,
             cinch=19.0, bottom=27.0)
        torso(canvas, colours, top=13.5, bottom=20.0, radius=3.7)
        arm(canvas, colours, 11.8, 15.0, 10.4, 20.0)
        arm(canvas, colours, 20.2, 15.0, 22.0, 18.0)
        self.staff(canvas)
        head(canvas, cy=9.8, radius=3.6)
        self.hat(canvas, colours)
        face(canvas, 11.2, 2.7)
        braid(canvas, RAMPS["wood"], 11.4, 9.8, 9.6, 16.6, 1.7)


class Archer(Archetype):
    name = "archer"
    label = "Archer"

    @kit
    def quiver(self, canvas: Canvas) -> None:
        """The quiver behind the shoulder, and the fletchings over its rim."""
        raster.capsule(canvas, 21.0, 11.0, 19.0, 19.0, 2.0, RAMPS["leather"],
                       ambient=0.30)
        for offset in range(3):
            canvas.put(21 - offset, 9 + offset, RAMPS["ink"][3])
            canvas.put(21 - offset, 10 + offset, RAMPS["wood"][2])

    @kit
    def bow(self, canvas: Canvas) -> None:
        """A tall arc on the off side with a drawn string, and the hand on it."""
        hand(canvas, 9.0, 14.0)
        for step in range(19):
            angle = math.pi * (0.5 + step / 18.0)
            x = 8.0 + math.cos(angle - math.pi / 2) * 0.0 - 0.0
            y = 5.0 + step
            bulge = math.sin(step / 18.0 * math.pi) * 3.2
            canvas.put(int(round(x - bulge)), int(y), RAMPS["wood"][1])
            canvas.put(int(round(x - bulge)) + 1, int(y), RAMPS["wood"][2])
        raster.line(canvas, 8, 5, 8, 23, RAMPS["ink"][2])

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, RAMPS["ink"], top=19, bottom=26, spread=3)
        boots(canvas, RAMPS["leather"])
        self.quiver(canvas)
        torso(canvas, colours, top=13.0, bottom=20.5, radius=4.3)
        raster.capsule(canvas, 16, 14.0, 16, 19.0, 2.6, RAMPS["leather"],
                       ambient=0.36)
        arm(canvas, colours, 12.0, 15.0, 9.5, 14.0)
        arm(canvas, colours, 19.5, 15.0, 15.5, 14.5)
        self.bow(canvas)
        head(canvas, cy=9.5, radius=3.9)
        # Hood with a peak, kept off the face so the head still reads.
        raster.disc(canvas, 16, 8.0, 4.2, colours, ambient=0.40)
        raster.polygon(canvas, ((19.5, 5.0), (23.0, 9.5), (18.0, 9.0)), colours, 0.30)
        face(canvas, 10.2, 2.8)
        raster.rect(canvas, 13, 12, 6, 1, RAMPS["leather"][1])

    def draw_second(self, canvas: Canvas, faction: FactionColour) -> None:
        """The same bow and quiver over a knee-length tunic, hood down.

        The hood is not this role's read, the bow is, so it can come down,
        and it is the one archetype in this style where the whole head of hair
        shows rather than a braid escaping from under something. The tunic takes
        the place of the first figure's belted jerkin: taken in at the waist and
        let out to the knee, which is where the archer had spare outline.
        """
        colours = faction.colours
        legs(canvas, RAMPS["ink"], top=21, bottom=26, spread=3)
        boots(canvas, RAMPS["leather"])
        self.quiver(canvas)
        gown(canvas, colours, shoulder=3.6, waist=3.0, hem=7.6, top=13.0,
             cinch=18.0, bottom=24.5)
        torso(canvas, colours, top=13.5, bottom=19.0, radius=3.8)
        raster.capsule(canvas, 16, 14.5, 16, 18.5, 2.3, RAMPS["leather"],
                       ambient=0.36)
        arm(canvas, colours, 12.4, 15.0, 9.5, 14.0)
        arm(canvas, colours, 19.2, 15.0, 15.5, 14.5)
        self.bow(canvas)
        hair(canvas, RAMPS["leather"], 8.6, 4.4)
        head(canvas, cy=9.8, radius=3.7)
        face(canvas, 10.4, 2.8)
        raster.rect(canvas, 13, 12, 6, 1, RAMPS["leather"][1])
        braid(canvas, RAMPS["leather"], 19.6, 11.0, 22.4, 17.4, 1.6)


class Healer(Archetype):
    name = "healer"
    label = "Healer"

    @kit
    def crozier(self, canvas: Canvas) -> None:
        """The staff and its cross finial: the clearest glyph in the roster."""
        hand(canvas, 22.0, 17.5)
        raster.capsule(canvas, 23.0, 6.0, 22.5, 26.0, 1.1, RAMPS["wood"],
                       ambient=0.40)
        raster.rect(canvas, 21, 4, 5, 2, RAMPS["gold"][1])
        raster.rect(canvas, 22, 2, 2, 6, RAMPS["gold"][1])
        raster.rect(canvas, 22, 3, 1, 4, RAMPS["gold"][0])

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, RAMPS["snow"] + (RAMPS["ink"][3],), top=12.0, bottom=27.0,
              width=6.4)
        # Faction-coloured stole down the front of a white robe.
        raster.rect(canvas, 14, 12, 4, 15, colours[2])
        raster.rect(canvas, 14, 12, 2, 15, colours[3])
        arm(canvas, RAMPS["snow"] + (RAMPS["ink"][3],), 11.5, 15.0, 10.5, 19.0)
        arm(canvas, RAMPS["snow"] + (RAMPS["ink"][3],), 20.5, 15.0, 22.0, 17.0)
        self.crozier(canvas)
        head(canvas, cy=9.5, radius=3.9)
        # Wimple: white framing, faction band across the brow.
        raster.disc(canvas, 16, 8.2, 4.3, RAMPS["snow"] + (RAMPS["ink"][3],),
                    ambient=0.55)
        raster.rect(canvas, 12, 12, 8, 1, RAMPS["snow"][1])
        face(canvas, 9.8, 2.8)
        raster.rect(canvas, 12, 6, 8, 2, colours[2])
        raster.rect(canvas, 12, 6, 8, 1, colours[3])

    def draw_second(self, canvas: Canvas, faction: FactionColour) -> None:
        """The same crozier over a veiled head and a robe with a waist in it.

        This is the role whose second figure had the most to prove, and the
        reason is a measurement rather than a preference: healer against mage is
        the closest cross-role pair in the library, and the pinch made it worse
        by narrowing both of them the same way, to 10 of 256. So the two second
        figures are drawn **apart**: the mage keeps its mass at the brim of a
        hat, and this one carries a veil that falls past the shoulder, which is
        a shape nothing else in the roster has.
        """
        colours = faction.colours
        pale = RAMPS["snow"] + (RAMPS["ink"][3],)
        gown(canvas, pale, shoulder=3.4, waist=4.0, hem=8.6, top=12.0,
             cinch=19.5, bottom=27.0)
        raster.rect(canvas, 14, 12, 4, 15, colours[2])
        raster.rect(canvas, 14, 12, 2, 15, colours[3])
        arm(canvas, pale, 11.8, 15.0, 10.8, 19.0)
        arm(canvas, pale, 20.2, 15.0, 22.0, 17.0)
        self.crozier(canvas)
        head(canvas, cy=9.8, radius=3.7)
        # The veil: the wimple carried past the shoulder on the side the staff
        # leaves free, which is where this role has outline to spend.
        raster.disc(canvas, 15.3, 8.6, 4.5, pale, ambient=0.55)
        raster.polygon(canvas, ((11.2, 7.4), (13.8, 7.4), (10.4, 21.0),
                                (7.9, 20.2)), pale, 0.52)
        raster.rect(canvas, 12, 12, 8, 1, RAMPS["snow"][1])
        face(canvas, 10.0, 2.8)
        raster.rect(canvas, 12, 6, 8, 2, colours[2])
        raster.rect(canvas, 12, 6, 8, 1, colours[3])


class Stormcaller(Archetype):
    """Area-effect caster: a storm brazier, and the hands that tend it.

    Deliberately built to be unmistakable from :class:`Mage` in silhouette:
    the mage is a narrow cone (pointed hat, staff held at one side), the
    stormcaller is **widest at the waist**, which nothing else in this style is.
    At the reduction the hat and the antlered crown are the only parts of the
    two heads still distinguishable, so the crown carries that difference.

    Why an object and not a gesture
    -------------------------------
    Both arms raised with a bolt thrown from each hand is the obvious drawing
    for this role, and reading the whole library on one page is what refuses it:
    four styles reach for that same figure, and a pose four styles share is a
    rig rather than a character. Three other commissions in the role answer it
    the same way, with a dancer wide at the hem, a bellringer under a yoke and a
    gunner leaning on a deck gun: **take the width from an object and put the
    arms down.**

    So the width is a brazier: a wide iron bowl on three legs with coals in it,
    which is the area-effect role stated as a thing rather than as a gesture. It
    is drawn *in front of* the body, so the bowl's rim is the widest row in the
    roster and the figure reads as standing behind something.

    Two properties come with the arms being down, and both are measured. The
    silhouette is not symmetric-about-the-shoulders, which the mage, the healer
    and the commander all are with their poles; and ``lunge`` and ``cast`` have
    something to differ by, because those two poses
    displace a body identically between the shoulder cut and the knee cut and a
    body whose arms are already thrown out has nothing left above the shoulder
    but a head.
    """

    name = "stormcaller"
    label = "Stormcaller"

    @kit
    def brazier(self, canvas: Canvas, colours: Ramp) -> None:
        """The width this role takes from an object, and the hands that tend it.

        The two hands are the kit's, not the body's: they are the grip, and a
        second figure whose arms are set differently still has to reach them.
        """
        hand(canvas, 8.9, 19.0)
        hand(canvas, 23.1, 19.0)
        # Fire standing over the coals, drawn before the bowl so the rim closes
        # over its feet. Three tongues, because one is a candle.
        for offset, rise in ((-7.5, 14.4), (-4.0, 16.6), (4.0, 16.6),
                             (7.5, 14.0)):
            raster.polygon(
                canvas,
                ((16 + offset - 1.9, 20.5), (16 + offset, rise),
                 (16 + offset + 1.9, 20.5)),
                RAMPS["gold"], 0.96, dither=False,
            )
        # The brazier's three legs, thin and set apart, so the frame under the
        # bowl reads as a stand rather than as a plinth.
        for foot, lean in ((9.0, -1), (16.0, 0), (23.0, 1)):
            raster.capsule(canvas, 16 + lean * 4.5, 24.0, foot, 27.0, 1.0,
                           RAMPS["steel"], ambient=0.24)
        # The bowl: the widest row anything in this style has, and it is at the
        # waist rather than at the shoulders.
        raster.polygon(canvas, ((2.5, 20.5), (29.5, 20.5), (22.0, 24.5),
                                (10.0, 24.5)), RAMPS["steel"], 0.32)
        raster.capsule(canvas, 2.5, 19.8, 29.5, 19.8, 1.5, RAMPS["steel"],
                       ambient=0.26)
        # The faction colour across the bowl's belly, so a unit's side is
        # legible from the widest thing it carries. This is the archetype rule
        # about a large contiguous faction area, applied to an object rather
        # than to a garment: below the rim the bowl is most of what a board sees
        # of this unit, and iron all the way down read as a plinth.
        raster.rect(canvas, 8, 21, 16, 3, colours[2])
        raster.rect(canvas, 8, 21, 16, 1, colours[3])

    @kit
    def smoke(self, canvas: Canvas) -> None:
        """Smoke off the coals, drifting to one side.

        In the brazier's own iron at its palest, so no ramp this sprite does
        not already spend. It is drawn *above the shoulder cut*, and that is
        measured rather than decorative: ``lunge`` and ``cast`` displace a body
        identically between the shoulder cut and the knee cut, and everything
        this figure has below the knee stands inside a faction disc that no pose
        moves, so the band over its head is the only one where its two gestures
        can differ.
        """
        for cx, cy, size, lit in ((21.4, 10.0, 2.3, 0.52), (24.8, 6.2, 2.0, 0.62),
                                  (27.6, 2.6, 1.6, 0.74)):
            raster.disc(canvas, cx, cy, size, RAMPS["steel"], ambient=lit,
                        squash=1.25)

    @kit
    def crown(self, canvas: Canvas, colours: Ramp) -> None:
        """Antlered crown: three prongs, wider than the head.

        Kit and not headgear a figure may choose, because at the reduction it is
        the only thing still separating this role's head from the mage's. A
        second figure may wear hair under it; it may not wear a different crown.
        """
        raster.rect(canvas, 12, 7, 8, 2, colours[1])
        for offset, height in ((-4, 3.0), (0, 1.5), (4, 3.0)):
            raster.polygon(
                canvas,
                ((16 + offset - 1.2, 7.0), (16 + offset, height),
                 (16 + offset + 1.2, 7.0)),
                RAMPS["gold"],
                0.80,
                dither=False,
            )

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=13.0, bottom=27.0, width=5.8)
        torso(canvas, colours, top=13.0, bottom=20.0, radius=4.0)
        # Both arms down and out, hands low on the bowl's rim.
        arm(canvas, colours, 12.5, 15.0, 9.5, 18.5)
        arm(canvas, colours, 19.5, 15.0, 22.5, 18.5)
        self.brazier(canvas, colours)
        self.smoke(canvas)
        head(canvas, cy=10.0, radius=3.9)
        face(canvas, 10.4, 2.9)
        self.crown(canvas, colours)

    def draw_second(self, canvas: Canvas, faction: FactionColour) -> None:
        """The same brazier and crown over loose hair and a narrower robe.

        The role with the least of itself visible, because the object it takes
        its width from is the widest thing in the style and covers everything
        below the waist. What is left is the band between the crown and the
        bowl's rim, so that is where the whole difference is spent: hair falling
        either side of the face, and shoulders drawn in behind it.
        """
        colours = faction.colours
        gown(canvas, colours, shoulder=2.8, waist=4.0, hem=6.2, top=13.0,
             cinch=18.0, bottom=27.0)
        torso(canvas, colours, top=13.5, bottom=20.0, radius=3.6)
        arm(canvas, colours, 12.8, 15.2, 9.5, 18.5)
        arm(canvas, colours, 19.2, 15.2, 22.5, 18.5)
        self.brazier(canvas, colours)
        self.smoke(canvas)
        # Hair either side of the face, below the crown and above the rim: the
        # only band this archetype has left, and the reason the crown is kit.
        for side in (-1, 1):
            braid(canvas, RAMPS["ink"], 16 + side * 3.2, 8.8,
                  16 + side * 5.4, 14.2, 2.4)
        head(canvas, cy=10.2, radius=3.6)
        face(canvas, 10.6, 2.9)
        self.crown(canvas, colours)


class Commander(Archetype):
    """The named leader, and a win condition in its own right.

    Rank is signalled three ways at once because one of them has to survive
    every reduction: a full-height faction cape (mass), a banner on a pole that
    breaks the top of the frame (silhouette), and gold trim (detail). The cape
    and banner are what still read at 16x16 in four shades.
    """

    name = "commander"
    label = "Commander"

    @kit
    def banner(self, canvas: Canvas, colours: Ramp) -> None:
        """The pole, planted, breaking the top edge of the frame, and its flag."""
        raster.capsule(canvas, 25.0, 1.0, 25.0, 27.0, 1.1, RAMPS["wood"],
                       ambient=0.42)
        raster.polygon(canvas, ((24.0, 2.0), (31.0, 4.0), (24.0, 11.0)), colours,
                       0.72, dither=False)
        raster.polygon(canvas, ((24.0, 2.0), (28.0, 2.8), (24.0, 6.0)), colours,
                       0.95, dither=False)
        raster.disc(canvas, 25.0, 0.8, 1.4, RAMPS["gold"], ambient=0.60)

    @kit
    def baton(self, canvas: Canvas) -> None:
        """The gold-barred baton of command, and the gauntlet that holds it."""
        hand(canvas, 8.2, 19.0)
        raster.capsule(canvas, 8.0, 19.5, 8.0, 12.0, 1.2, RAMPS["steel"],
                       ambient=0.50)
        raster.capsule(canvas, 6.5, 19.0, 9.5, 19.0, 0.9, RAMPS["gold"],
                       ambient=0.60)

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        self.banner(canvas, colours)
        # Cape behind the body, wider and longer than any other archetype's.
        cloak(canvas, colours, top=11.5, bottom=27.0, width=8.2)
        legs(canvas, RAMPS["steel"], top=19, bottom=26, spread=3)
        boots(canvas, RAMPS["leather"])
        torso(canvas, colours, top=12.5, bottom=21.0, radius=5.0)
        raster.capsule(canvas, 16, 13.5, 16, 19.5, 3.4, RAMPS["steel"], ambient=0.32)
        raster.rect(canvas, 13, 15, 6, 1, RAMPS["gold"][0])
        for side in (-1, 1):
            raster.disc(canvas, 16 + side * 5.0, 13.5, 2.8, RAMPS["steel"],
                        ambient=0.32)
            raster.disc(canvas, 16 + side * 5.0, 13.0, 1.2, RAMPS["gold"],
                        ambient=0.55)
        arm(canvas, RAMPS["steel"], 11.0, 15.0, 8.5, 18.5)
        self.baton(canvas)
        head(canvas, cy=8.5, radius=4.2)
        # Crested great helm: a gold circlet and a tall faction plume.
        raster.disc(canvas, 16, 7.8, 4.4, RAMPS["steel"], ambient=0.32)
        raster.rect(canvas, 12, 8, 8, 2, RAMPS["ink"][1])
        raster.rect(canvas, 13, 8, 6, 1, RAMPS["skin"][0])
        raster.rect(canvas, 12, 5, 8, 1, RAMPS["gold"][1])
        raster.polygon(canvas, ((14.5, 5.0), (16.0, 0.0), (18.5, 5.0)), colours,
                       0.90, dither=False)
        raster.polygon(canvas, ((15.5, 5.0), (16.0, 1.0), (17.0, 5.0)), colours,
                       0.55, dither=False)

    def draw_second(self, canvas: Canvas, faction: FactionColour) -> None:
        """The same banner and baton over a caped waist and a long fall of hair.

        Rank is still signalled three ways (cape, banner, gold) and none of
        them moves. What differs is the cape's line: the first figure's falls
        straight from the shoulder, this one's is taken in at the waist and let
        out below the knee, which is the largest piece of outline any role in
        this style has to redistribute. The circlet stays and the great helm's
        visor goes, so the hair has somewhere to leave from.
        """
        colours = faction.colours
        self.banner(canvas, colours)
        gown(canvas, colours, shoulder=3.7, waist=3.4, hem=10.7, top=11.5,
             cinch=20.0, bottom=27.0)
        # A standing collar, which is the only outline this role has left: the
        # banner pins its right edge from row 10 down and the baton pins its
        # left from row 11, so the cape can differ at the neck and at the hem
        # and nowhere between them.
        raster.polygon(canvas, ((8.2, 13.5), (11.8, 5.8), (20.2, 5.8),
                                (23.8, 13.5)), colours, 0.30)
        legs(canvas, RAMPS["steel"], top=20, bottom=26, spread=3)
        boots(canvas, RAMPS["leather"])
        torso(canvas, colours, top=13.0, bottom=21.0, radius=4.5)
        raster.capsule(canvas, 16, 13.8, 16, 19.0, 3.1, RAMPS["steel"],
                       ambient=0.32)
        raster.rect(canvas, 13, 15, 6, 1, RAMPS["gold"][0])
        for side in (-1, 1):
            raster.disc(canvas, 16 + side * 4.4, 13.8, 2.4, RAMPS["steel"],
                        ambient=0.32)
            raster.disc(canvas, 16 + side * 4.4, 13.4, 1.1, RAMPS["gold"],
                        ambient=0.55)
        arm(canvas, RAMPS["steel"], 11.4, 15.0, 8.5, 18.5)
        self.baton(canvas)
        head(canvas, cy=8.8, radius=4.0)
        raster.disc(canvas, 16, 8.0, 4.2, RAMPS["steel"], ambient=0.32)
        raster.rect(canvas, 13, 9, 6, 1, RAMPS["ink"][1])
        raster.rect(canvas, 13, 5, 7, 1, RAMPS["gold"][1])
        # A crest swept back rather than a plume standing up: the one mark this
        # role has above the brow that neither the banner nor the baton pins.
        raster.polygon(canvas, ((18.6, 5.0), (14.6, 5.0), (11.4, 2.6),
                                (14.2, 0.6), (17.0, 2.0)), colours,
                       0.90, dither=False)
        raster.polygon(canvas, ((17.4, 4.6), (15.0, 4.6), (13.2, 2.6),
                                (14.6, 1.8)), colours, 0.55, dither=False)
        face(canvas, 9.4, 2.6)
        braid(canvas, RAMPS["leather"], 19.6, 10.4, 21.6, 17.2, 1.6)


class Rogue(Archetype):
    name = "rogue"
    label = "Rogue"

    @kit
    def daggers(self, canvas: Canvas) -> None:
        """Two blades held low and high, and the two hands on them."""
        hand(canvas, 8.2, 19.4)
        hand(canvas, 23.8, 13.6)
        raster.capsule(canvas, 7.5, 20.0, 4.5, 23.5, 1.0, RAMPS["steel"],
                       ambient=0.50)
        raster.capsule(canvas, 24.5, 13.0, 27.5, 9.0, 1.0, RAMPS["steel"],
                       ambient=0.50)

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        # Crouched: lower and wider than the other humanoids.
        legs(canvas, RAMPS["ink"], top=21, bottom=27, spread=4, radius=2.1)
        cloak(canvas, RAMPS["ink"], top=14.0, bottom=25.0, width=6.0)
        torso(canvas, colours, top=15.0, bottom=21.0, radius=4.0)
        raster.rect(canvas, 12, 17, 9, 2, RAMPS["leather"][1])
        arm(canvas, RAMPS["ink"], 12.0, 16.5, 8.5, 19.0, 1.7)
        arm(canvas, RAMPS["ink"], 20.0, 16.5, 23.5, 14.0, 1.7)
        self.daggers(canvas)
        head(canvas, cy=11.0, radius=3.8)
        # Hood pulled forward, faction scarf across the mouth.
        raster.disc(canvas, 16, 10.0, 4.3, RAMPS["ink"], ambient=0.42)
        raster.polygon(canvas, ((20.0, 7.0), (24.0, 12.0), (19.0, 11.0)),
                       RAMPS["ink"], 0.35)
        raster.rect(canvas, 13, 12, 7, 2, colours[2])
        raster.rect(canvas, 13, 12, 7, 1, colours[3])
        for side in (-1, 1):
            canvas.put(16 + side * 2, 10, RAMPS["gold"][1])

    def draw_second(self, canvas: Canvas, faction: FactionColour) -> None:
        """The same two daggers over a crouch drawn from a longer cloak.

        Both figures crouch, because the crouch is what tells this role from the
        archer once a sprite is halved. The difference is the cloak: the first
        figure's is a short cape ending above the knee, this one's is a hooded
        travelling cloak with a waist, and a tail of hair comes out of the hood
        rather than the hood coming to a point.
        """
        colours = faction.colours
        legs(canvas, RAMPS["ink"], top=21, bottom=27, spread=4, radius=2.1)
        gown(canvas, RAMPS["ink"], shoulder=3.4, waist=4.2, hem=7.0, top=14.5,
             cinch=19.5, bottom=25.0)
        torso(canvas, colours, top=15.0, bottom=21.0, radius=3.7)
        raster.rect(canvas, 12, 17, 9, 2, RAMPS["leather"][1])
        arm(canvas, RAMPS["ink"], 12.4, 16.5, 8.5, 19.0, 1.7)
        arm(canvas, RAMPS["ink"], 19.6, 16.5, 23.5, 14.0, 1.7)
        self.daggers(canvas)
        head(canvas, cy=11.0, radius=3.6)
        raster.disc(canvas, 16, 10.2, 4.1, RAMPS["ink"], ambient=0.42)
        raster.rect(canvas, 13, 12, 7, 2, colours[2])
        raster.rect(canvas, 13, 12, 7, 1, colours[3])
        for side in (-1, 1):
            canvas.put(16 + side * 2, 10, RAMPS["gold"][1])
        braid(canvas, RAMPS["leather"], 13.0, 8.4, 10.4, 14.6, 1.6)


class Beast(Archetype):
    name = "beast"
    label = "Beast"

    #: Hide gains a lit top step; the hide ramp alone is too dark to shade.
    HIDE = RAMPS["hide"] + (RAMPS["ink"][2],)

    @kit
    def spine(self, canvas: Canvas, colours: Ramp) -> None:
        """The faction ridge running the length of the back.

        Kit rather than body, and the constraint that comes with it is real:
        both figures' backs have to sit at the same height, or the ridge floats.
        """
        for step in range(5):
            raster.polygon(
                canvas,
                ((12.0 + step * 2.4, 14.6), (13.2 + step * 2.4, 11.0),
                 (14.4 + step * 2.4, 14.6)),
                colours,
                0.55 + 0.06 * (step % 2),
            )

    @kit
    def collar(self, canvas: Canvas, colours: Ramp) -> None:
        """A second, unmissable faction marker at head height."""
        raster.capsule(canvas, 21.0, 12.5, 21.5, 17.5, 1.4, colours, ambient=0.55)

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        hide = self.HIDE
        # Quadruped: a long low body, the only non-humanoid silhouette.
        for x in (9.5, 13.5, 19.5, 23.5):
            raster.capsule(canvas, x, 20.0, x + 0.6, 26.5, 1.7, hide, ambient=0.30)
            raster.disc(canvas, x + 0.9, 26.6, 1.6, hide, ambient=0.40, squash=1.5)
        raster.capsule(canvas, 11.0, 18.5, 22.0, 18.5, 4.4, hide, ambient=0.34)
        # Tail.
        raster.capsule(canvas, 9.5, 17.5, 4.5, 12.5, 1.4, hide, ambient=0.36)
        raster.disc(canvas, 4.0, 11.5, 1.8, colours, ambient=0.50)
        self.spine(canvas, colours)
        # Head, muzzle, ear, eye, teeth.
        raster.disc(canvas, 24.0, 14.0, 4.4, hide, ambient=0.42)
        raster.capsule(canvas, 25.0, 16.0, 29.0, 17.0, 2.2, hide, ambient=0.36)
        raster.polygon(canvas, ((22.0, 11.5), (24.0, 6.5), (25.5, 11.5)), hide, 0.55)
        raster.polygon(canvas, ((22.8, 11.0), (24.0, 8.0), (25.0, 11.0)), colours, 0.60)
        canvas.put(25, 14, RAMPS["gold"][1])
        canvas.put(26, 14, RAMPS["gold"][0])
        for offset in range(3):
            canvas.put(27 + offset % 2, 18 + offset, RAMPS["ink"][3])
        self.collar(canvas, colours)

    def draw_second(self, canvas: Canvas, faction: FactionColour) -> None:
        """The same collar and spine ridge over a maned, deeper-chested beast.

        The role where the vocabulary's first two moves both apply to an animal
        and one of them survives: hair is a **mane**, and it goes where a mane
        goes: over the shoulder, in front of the spine ridge, which is kit and
        does not move. The line is the barrel: this one is shallower through the
        loin and heavier at the chest, and its tail carries a plume where the
        first figure's carries a tuft.
        """
        colours = faction.colours
        hide = self.HIDE
        for x in (9.0, 13.2, 19.8, 24.0):
            raster.capsule(canvas, x, 20.0, x + 0.6, 26.5, 1.7, hide, ambient=0.30)
            raster.disc(canvas, x + 0.9, 26.6, 1.6, hide, ambient=0.40, squash=1.5)
        raster.capsule(canvas, 12.0, 18.6, 21.0, 18.4, 4.2, hide, ambient=0.34)
        raster.capsule(canvas, 19.0, 18.2, 22.4, 17.6, 4.6, hide, ambient=0.36)
        # Tail, carried higher, with a plume rather than a tuft on the end.
        raster.capsule(canvas, 9.5, 17.5, 5.0, 11.5, 1.3, hide, ambient=0.36)
        raster.polygon(canvas, ((5.4, 13.4), (2.6, 8.8), (5.0, 7.7),
                                (7.2, 11.6)), hide, 0.44)
        raster.disc(canvas, 4.4, 10.0, 1.7, colours, ambient=0.50)
        self.spine(canvas, colours)
        # The mane, over the shoulder and in front of the ridge.
        raster.polygon(canvas, ((17.4, 12.4), (21.6, 10.6), (23.4, 15.0),
                                (19.0, 17.4)), hide, 0.30)
        raster.disc(canvas, 24.0, 14.0, 4.2, hide, ambient=0.42)
        raster.capsule(canvas, 25.0, 16.0, 29.0, 17.0, 2.2, hide, ambient=0.36)
        raster.polygon(canvas, ((22.0, 11.5), (24.0, 6.5), (25.5, 11.5)), hide, 0.55)
        raster.polygon(canvas, ((22.8, 11.0), (24.0, 8.0), (25.0, 11.0)), colours, 0.60)
        canvas.put(25, 14, RAMPS["gold"][1])
        canvas.put(26, 14, RAMPS["gold"][0])
        for offset in range(3):
            canvas.put(27 + offset % 2, 18 + offset, RAMPS["ink"][3])
        self.collar(canvas, colours)


#: Registry order is the roster order everywhere: manifest, gallery, sheets.
#: The first six are the roles the sample campaign at ``games/tworivers/``
#: expects; rogue and beast follow as additional archetypes.
ARCHETYPE_CLASSES: Tuple[type, ...] = (
    Knight, Archer, Mage, Stormcaller, Healer, Commander, Rogue, Beast,
)

ARCHETYPES: Dict[str, Archetype] = {cls.name: cls() for cls in ARCHETYPE_CLASSES}

#: Archetype names in a stable, documented order.
ARCHETYPE_ORDER: Tuple[str, ...] = tuple(cls.name for cls in ARCHETYPE_CLASSES)


def body_of(drawn: Archetype, colour: FactionColour,
            figure: Optional[Figure] = None) -> Canvas:
    """One archetype's body, as ``figure`` draws it, with no disc and no outline.

    A figure is chosen by **routine**: the second figure of a role a style has
    drawn twice is a second drawing, so selecting it selects the method that
    draws it. Where a style's commission has not reached a role yet the method
    is absent, and the figure's stand-in transform of the first figure's pixels
    draws it instead: visible at 32×32, honestly not a person, and the debt
    :mod:`.figures` names as one.
    """
    canvas = Canvas(SPRITE, SPRITE)
    routine = None if figure is None else getattr(drawn, figure.routine, None)
    if routine is not None:
        routine(canvas, colour)
        return canvas
    drawn.draw(canvas, colour)
    if figure is not None and figure.stand_in is not None:
        return figure.stand_in(canvas)
    return canvas


def sprite(archetype: str, colour: str,
           roster: Optional[Mapping[str, Archetype]] = None,
           pose: Optional[Pose] = None,
           figure: Optional[Figure] = None) -> Canvas:
    """Render one archetype in one faction colour at the native sprite size.

    ``roster`` is the drawing routines to use, which is what a character style
    supplies (:mod:`.styles`); omitting it draws the default style's roster.
    The faction disc and the ink outline are the style-independent frame every
    style is drawn inside, so a unit reads as a unit whatever draws it.

    ``figure`` names a body (:mod:`.figures`) and ``pose`` is an animation
    frame's displacement (:mod:`.frames`); omitting both draws the first figure
    standing, which is what shipped before either axis existed.

    The two are applied in that order, and the order is the meaning: a pose is a
    gesture **of** a body, so the body is drawn first and then made to move. The
    pose is applied **before** the ink outline is traced, so the outline follows
    the silhouette that will actually be drawn, and it is not applied to the
    faction disc: the disc is the ground the unit stands on, and ground that
    walks with the unit is not ground.
    """
    canvas = Canvas(SPRITE, SPRITE)
    colour = FACTION_COLOURS_BY_NAME[colour]
    faction_disc(canvas, colour)
    body = body_of((ARCHETYPES if roster is None else roster)[archetype],
                   colour, figure)
    if pose is not None:
        body = pose(body)
    raster.outline(body, RAMPS["ink"][0])
    canvas.blit(body, 0, 0)
    return canvas
