# SPDX-License-Identifier: MIT
"""The ``sengoku`` character style: the eight roles in Warring States Japan.

This is the **strongest style in the library on silhouette separation**, and
the reason is in the vocabulary before anyone draws it: a kabuto's crest, a
naginata's horizontal, a straw hat, a monk's shaved profile, a fox's ears. The
screen a candidate style has to pass is in
``tools/placeholder_art/README.md``; separation is the gate this style was
drawn against.

The name is the period rather than the place. A style menu has to be one
dimension and that dimension is the setting: ``feudal`` fails that test, since
medieval Europe is feudal too, and ``samurai`` would name one of the eight
dressings and make the other seven read as exceptions. ``sengoku`` names the era
every device below belongs to, roughly 1467 to 1600.

Eight heads, and the crest spent once
-------------------------------------
The roster page's reading of the shipped styles found that ``mythical`` leans on
one horned silhouette across four of its eight, and named **the helmet crest**
as this setting's version of the same trap. So the crest is spent once, on the
Daimyo, where it is a rank mark; the Samurai's kabuto carries none. Counted
across the roster the head reads eight different ways (helmet, straw hat, tall
cap, loose hair, bare skull, flag-over-helmet, cowl, animal) and no device
appears more than twice.

The same reading found two other faults to avoid rather than repeat. The
stormcaller is the same raised-arms pose in all four shipped styles and reads as
a rig rather than a character, so the Kagura dancer moves its width **to the
hem**: a stormcaller has always been the wide one, and this one is wide at the
sleeves and the hakama, with a single arm raised. And nothing here is drawn
small: every humanoid stands on the ground line with its head between rows 5
and 13, because a unit at a different scale from its siblings reads as a bug
rather than as a character.

No new palette entries
----------------------
None, for the reason *Adding a style* in ``tools/placeholder_art/README.md``
gives. The look was priced at five materials — lacquer, cloth, steel, skin and
straw — and two of them turn out cheaper than that:

``ink``
    **Lacquer.** Black lacquer over iron is what this armour is, and every
    sprite in the library already spends ``ink`` on its outline, so the darkest
    step of a lacquered plate costs nothing new. Three of its four rungs, not
    four: the paper-white top read as chrome on armour, and dropping it is what
    keeps the lacquered half of the roster a different material from the paper.
``snow``
    **Paper**, lifted by ``ink``'s top step: ofuda strips, shide streamers, the
    slips tied to a shakujo's rings, a monk's robe, a fox's throat. It is this
    style's mark at the ends of things, as leaves are ``nature``'s, which is
    what lets the three casters share one material instead of each naming a robe.
``steel``
    Blades and the bowl of a helmet. Nothing else.
``skin``
    Faces and hands, through the shared :func:`~.characters.head` and
    :func:`~.characters.face`.
``sand``
    **Straw**: the archer's jingasa, and the rope at a shrine.
``wood``
    Shafts: a bow, a haft, a pilgrim's staff.
``dirt``
    Fur, and only the fox's.
``gold``
    The two-step warm mark: a mon, a crest, a bell, an eye.

Five ramps a sprite, not eight
------------------------------
The list above is what the *style* holds; what binds is what one **sprite**
spends, because each shading ramp costs three to five of a sprite's sixteen
CI4 colours and the sixteen are a budget spent on materials rather than on
colours. A Samurai names lacquer, cloth, steel,
wood and skin; a Daimyo names lacquer, cloth, steel, gold and skin; a Shinobi
names four and is the leanest sprite in the style. Choosing which five is most
of what drawing a style is, and two of the choices here were made by
measurement rather than by taste. The third rung dropped off the lacquer, and
the Temple monk's staff moved to the off side, which took the share of opaque
texels the ``n64_ci4`` remap moves from 0.14% to **0.04%**.

The margin rule
---------------
Stated and enforced by :mod:`.frames`: ``walk_contact`` moves everything at or
below row 24 outward by a column, so nothing here is drawn on column 0 or
column 31 below the knee: no sleeve, no tail, no scabbard chape, no haft. The
two masses that break the top edge, the archer's bow and the daimyo's flag, sit
above the shoulder line where no pose moves them outward at all.
"""

from __future__ import annotations

import math

from . import raster
from .characters import (GROUND_Y, Archetype, FactionColour, arm, boots, cloak,
                         eyes, face, hand, head, legs, torso)
from .palette import RAMPS
from .raster import Canvas

#: Black lacquer over iron: armour, a cowl, a court cap, loose hair.
#:
#: ``ink`` without its paper-white top step, and dropping that step is a
#: measured decision rather than a tidy-up. Drawn with the full ramp, every
#: lacquered surface in the style picked up white speckle where the Lambert
#: term reached the last rung, a plate glinting like chrome, and the roster
#: read as five figures in armour made of the same material as the paper. Three
#: rungs of near-black to mid-grey is what black lacquer is, and it costs one
#: colour less per sprite.
LACQUER = RAMPS["ink"][:3]

#: Paper, and cloth pale enough to read as it. Spent at the ends of things.
PAPER = RAMPS["snow"] + (RAMPS["ink"][3],)

#: Blades and a helmet bowl.
STEEL = RAMPS["steel"]

#: Shafts: a bow's belly, a naginata's haft, a pilgrim's staff.
SHAFT = RAMPS["wood"]

#: Straw: a foot soldier's wide hat, and a shrine's rope.
STRAW = RAMPS["sand"]

#: Fur, and only the fox's.
FUR = RAMPS["dirt"]

#: The warm mark: a mon, a crest, a bell, an eye.
GOLD = RAMPS["gold"]


def plate(canvas: Canvas, x0: float, y0: float, x1: float, y1: float, ramp,
          tone: float = 0.42) -> None:
    """A flat lacquered plate with hard corners.

    The style's one rectilinear primitive, and the reason it exists is that
    everything else in this generator is a disc or a capsule. Armour here is
    made of *plates*, and a rectangle is what a sode, a jingasa's crown and a
    sashimono all reduce to. A hard corner is a silhouette device the rounded
    roster does not otherwise hold.
    """
    raster.polygon(canvas, ((x0, y0), (x1, y0), (x1, y1), (x0, y1)), ramp, tone,
                   dither=False)


def lacing(canvas: Canvas, x: int, width: int, rows, colours) -> None:
    """The silk cords that hold lacquered plates together, in faction colour.

    Where the faction ramp goes on an armoured body, and it is chosen rather
    than borrowed: odoshi is the one part of this armour that was coloured to
    say whose it was, so the thing the rules need, a large, contiguous, legible
    faction area, is the thing the armour already had.
    """
    for row in rows:
        raster.rect(canvas, x, row, width, 1, colours[2])
        raster.rect(canvas, x, row + 1, width, 1, colours[1])


def paper_strips(canvas: Canvas, x: int, top: int, lengths, step: int = 2,
                 lean: int = 0) -> None:
    """Hanging slips of paper: ofuda, shide, the slips tied to a staff's rings.

    Drawn as hard one-column strips with a clear gap between them rather than
    as capsules. Rounded slips of a plausible width merged into one pale blob
    the moment they sat side by side, and a blob is not paper. The gap is what
    says there are several of something.
    """
    for index, length in enumerate(lengths):
        column = x + index * step
        for row in range(length):
            canvas.put(column + (row // 3) * lean, top + row,
                       PAPER[2 - (row % 3 == 2)])


class Samurai(Archetype):
    """Melee, shield forward: square sode and a naginata held level.

    This armour has no shield, and it does not need one. The sode were arm
    shields, worn on the shoulders, and they are the widest hard rectangles in
    the roster. The naginata supplies the second half of the read: the one hard
    horizontal here, held low across the body rather than thrust, so the lunge
    and the cast still have somewhere to go.
    """

    name = "knight"
    label = "Samurai"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, LACQUER, top=19, bottom=26, spread=3)
        boots(canvas, LACQUER)
        torso(canvas, colours, top=13.0, bottom=21.0, radius=4.6)
        raster.capsule(canvas, 16, 14.0, 16, 20.0, 3.2, LACQUER, ambient=0.34)
        lacing(canvas, 13, 7, (15, 18), colours)
        for side in (-1, 1):  # sode: the hard squares nothing else here has
            plate(canvas, 16 + side * 9.4, 12.5, 16 + side * 4.4, 20.5, LACQUER,
                  0.34 if side < 0 else 0.50)
            plate(canvas, 16 + side * 9.4, 12.5, 16 + side * 4.4, 13.5, colours,
                  0.70)
        arm(canvas, LACQUER, 11.5, 16.5, 8.5, 19.5, 1.5)
        arm(canvas, LACQUER, 20.5, 16.5, 22.5, 18.5, 1.5)
        hand(canvas, 8.2, 20.0)
        hand(canvas, 22.8, 19.0)
        raster.capsule(canvas, 2.5, 21.5, 25.5, 18.0, 1.0, SHAFT, ambient=0.44)
        raster.polygon(canvas, ((25.0, 19.0), (30.5, 12.5), (28.0, 18.5)),
                       STEEL, 0.80, dither=False)
        head(canvas, cy=9.0, radius=4.0)
        raster.disc(canvas, 16, 8.2, 4.3, LACQUER, ambient=0.36)  # the kabuto
        raster.polygon(canvas, ((10.5, 9.5), (21.5, 9.5), (23.0, 12.5),
                                (9.0, 12.5)), LACQUER, 0.56)  # the shikoro
        raster.rect(canvas, 11, 8, 10, 1, STEEL[2])  # the brim over the brow
        face(canvas, 10.4, 2.6)


class YumiArcher(Archetype):
    """Ranged, cannot strike adjacent: the asymmetric bow, and a straw hat.

    A yumi is gripped a third of the way up, so almost all of it is above the
    hand: the arc breaks the top edge on the off side and barely reaches the
    knee below it, which is a shape no symmetric bow in the library makes. The
    jingasa is the second half: a wide low straw disc where every other head
    here is tall, round or bare.
    """

    name = "archer"
    label = "Yumi archer"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, colours, top=19, bottom=26, spread=3)
        boots(canvas, LACQUER)
        raster.capsule(canvas, 21.0, 12.5, 20.0, 18.5, 1.5, SHAFT,
                       ambient=0.30)  # the ebira, behind the shoulder
        for offset in range(3):
            canvas.put(21 - offset, 9 + offset, PAPER[2])
        torso(canvas, colours, top=13.0, bottom=20.5, radius=4.2)
        raster.capsule(canvas, 16, 14.0, 16, 19.5, 2.7, LACQUER, ambient=0.36)
        lacing(canvas, 14, 5, (16,), colours)
        arm(canvas, colours, 12.0, 15.0, 9.0, 15.5, 1.6)
        arm(canvas, colours, 19.5, 15.0, 14.0, 13.5, 1.6)  # drawn to the ear
        hand(canvas, 8.6, 15.6)
        # The yumi, and the asymmetry is the whole point: the grip sits at row
        # 16 of 24, so the upper limb is more than twice the lower one and
        # bellies out four columns where the lower belly is under two.
        for step in range(25):
            if step <= 16:
                bulge = 4.0 * math.sin(math.pi * step / 16.0)
            else:
                bulge = 1.8 * math.sin(math.pi * (step - 16) / 8.0)
            canvas.put(int(round(9.0 - bulge)), step, SHAFT[1])
            canvas.put(int(round(9.0 - bulge)) + 1, step, SHAFT[2])
        raster.line(canvas, 9, 0, 9, 24, RAMPS["ink"][2])
        head(canvas, cy=10.5, radius=3.8)
        raster.polygon(canvas, ((8.0, 8.5), (16.0, 3.0), (24.0, 8.5)), STRAW,
                       0.62, dither=False)  # the jingasa
        raster.rect(canvas, 8, 8, 16, 1, STRAW[0])
        face(canvas, 11.0, 2.7)


class Onmyoji(Archetype):
    """Magic, short band: the narrowest column, under a tall court cap.

    The mage is a narrow cone in every style and the cone is drawn by the cap
    here rather than by a hat brim: an eboshi is a thin lacquered spike, so the
    silhouette is a column with a spike on it where the Kagura dancer beside it
    is a triangle standing on its base. That is the pair that must not collide,
    and it is settled by which end of the figure is wide.
    """

    name = "mage"
    label = "Onmyoji"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=12.5, bottom=27.0, width=4.6)
        torso(canvas, colours, top=13.5, bottom=21.0, radius=3.2)
        raster.rect(canvas, 15, 13, 3, 14, colours[3])  # the robe's fold
        arm(canvas, colours, 12.5, 15.5, 11.5, 19.5, 1.6)
        arm(canvas, colours, 19.5, 15.5, 20.5, 18.5, 1.6)
        hand(canvas, 20.6, 19.0)
        raster.capsule(canvas, 21.0, 6.0, 21.0, 19.0, 0.9, SHAFT,
                       ambient=0.46)  # the shaku, held upright at the chest
        # The ofuda hang on the weapon side, tight against the shaku, because
        # this figure's whole read is that it is the narrowest thing here:
        # paper spread on the off hand cost it two columns it could not spare.
        #
        # They hang from the *top* of the shaku rather than from its grip, and
        # the roster review is why. ``lunge`` and ``cast`` displace a body
        # identically between row 12 and row 24, and everything a robe has below
        # row 24 is hidden behind a faction disc that no pose moves. So the
        # only band where this figure's two gestures can differ is above the
        # shoulder, and drawn with its paper at the chest it had four columns of
        # eboshi there and nothing else. The strips did not move outward, which
        # is the column this silhouette cannot spare; they moved up.
        paper_strips(canvas, 22, 6, (9, 7), step=2)
        head(canvas, cy=10.5, radius=3.6)
        raster.polygon(canvas, ((13.8, 7.5), (18.2, 7.5), (17.4, 0.0),
                                (14.6, 1.5)), LACQUER, 0.45)  # the eboshi
        raster.rect(canvas, 13, 7, 6, 1, LACQUER[1])
        face(canvas, 10.8, 2.7)


class KaguraDancer(Archetype):
    """Area effect: wide at the hem rather than at the shoulders.

    Every shipped style draws this role as raised arms and thrown marks, which
    reduces to a rig rather than to a character. A stormcaller is the wide one,
    so the width is kept and moved: sleeves and a hakama sweep out between the
    hip and the knee, one arm goes up to a suzu, and paper streamers fly off it.
    Nothing else in this roster is widest below its own waist.
    """

    name = "stormcaller"
    label = "Kagura dancer"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=17.0, bottom=27.0, width=8.6)  # the hakama
        # Furisode sleeves, hung from the shoulders and swept outward *and
        # down*: this is where the style's widest row is, at the hip rather
        # than at the shoulder, and the two are different lengths because a
        # dancer with one arm up is not symmetric.
        raster.polygon(canvas, ((12.5, 14.0), (2.5, 18.5), (4.0, 23.0),
                                (13.0, 19.5)), PAPER, 0.56)
        raster.polygon(canvas, ((19.5, 14.5), (27.0, 17.5), (27.5, 21.0),
                                (19.0, 19.0)), PAPER, 0.74)
        raster.capsule(canvas, 15.0, 11.5, 14.0, 18.0, 2.1, LACQUER,
                       ambient=0.34)  # the fall of hair down the back
        torso(canvas, PAPER, top=12.5, bottom=18.5, radius=3.5)
        raster.rect(canvas, 13, 16, 7, 2, colours[2])  # the obi
        raster.rect(canvas, 13, 16, 7, 1, colours[3])
        arm(canvas, PAPER, 19.5, 14.0, 23.0, 10.0, 1.5)
        hand(canvas, 23.6, 9.2)
        for x, y in ((24.4, 6.6), (26.8, 5.6), (25.4, 3.8)):  # the suzu
            raster.disc(canvas, x, y, 1.3, GOLD, ambient=0.55)
        paper_strips(canvas, 27, 6, (5, 4), step=2, lean=1)
        raster.disc(canvas, 16, 8.6, 4.1, LACQUER, ambient=0.42)  # the hair
        face(canvas, 9.2, 2.8)
        raster.rect(canvas, 13, 5, 6, 1, colours[3])


class TempleMonk(Archetype):
    """Support, no offensive weapon: a bare shaved skull and a ringed staff.

    The one head here with nothing on it, which is the whole read: seven of
    these eight are identified by what they wear above the eyes, so the eighth
    is identified by wearing nothing. The kesa carries the faction colour
    diagonally across a pale robe, and the shakujo is a pilgrim's staff rather
    than a weapon: the role has none, and the naginata belongs to the Samurai.
    """

    name = "healer"
    label = "Temple monk"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        # Wide at the shoulders and narrowing to the hem: the exact inverse of
        # the Kagura dancer beside it, which is what separates the two robed
        # figures once the reduction has taken everything else away.
        raster.polygon(canvas, ((9.0, 14.0), (23.0, 14.0), (21.0, 27.0),
                                (11.0, 27.0)), PAPER, 0.46)
        torso(canvas, PAPER, top=14.0, bottom=21.0, radius=4.6)
        raster.polygon(canvas, ((10.5, 14.0), (15.0, 14.0), (20.5, 23.0),
                                (16.0, 23.0)), colours, 0.62)  # the kesa
        arm(canvas, PAPER, 11.0, 16.0, 8.0, 19.0)
        arm(canvas, PAPER, 21.0, 16.0, 22.0, 20.0)
        hand(canvas, 22.2, 20.5)
        hand(canvas, 7.6, 19.5)
        # The shakujo goes on the *off* side, and the Kagura dancer is why: a
        # ring cluster at the top of a pole on the weapon side reduced to the
        # dancer's suzu almost exactly, and two figures whose only mark is a
        # small cluster in the same corner are one figure at sixteen pixels.
        raster.capsule(canvas, 6.0, 8.0, 6.5, 26.0, 1.1, SHAFT, ambient=0.42)
        for x, y in ((4.2, 6.4), (7.8, 6.4), (6.0, 4.2)):  # shakujo rings
            raster.disc(canvas, x, y, 1.4, GOLD, ambient=0.52)
        paper_strips(canvas, 4, 8, (5,), lean=-1)
        raster.capsule(canvas, 16, 12.0, 16, 14.0, 1.6, RAMPS["skin"],
                       ambient=0.40)  # the neck, so the skull is not the body
        raster.disc(canvas, 16, 9.0, 3.6, RAMPS["skin"], ambient=0.36)
        raster.disc(canvas, 16, 8.2, 2.6, RAMPS["skin"], ambient=0.62)
        eyes(canvas, cy=10, spread=2)
        raster.rect(canvas, 13, 13, 6, 1, colours[3])  # the collar of the robe


class Daimyo(Archetype):
    """The leader, and a win condition: a flag squared off above the helmet.

    Rank three ways, as every style states it, because one of them has to
    survive the reduction: the sashimono is a flat rectangle standing over the
    head, the only straight-edged mass above a skull in any style here; the
    jinbaori carries the width; and the crescent maedate is the detail. The
    crest is spent here and nowhere else, so it stays a rank mark instead of
    becoming the thing four of these eight have in common.
    """

    name = "commander"
    label = "Daimyo"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        raster.capsule(canvas, 21.0, 1.0, 20.0, 15.0, 1.0, LACQUER,
                       ambient=0.40)  # the sashimono's staff, worn on the back
        plate(canvas, 10.5, 0.0, 21.0, 5.5, colours, 0.62)
        plate(canvas, 10.5, 0.0, 21.0, 1.0, colours, 0.88)
        raster.disc(canvas, 15.8, 2.6, 1.6, GOLD, ambient=0.55)  # the mon
        cloak(canvas, colours, top=11.5, bottom=27.0, width=8.2)  # the jinbaori
        legs(canvas, LACQUER, top=19, bottom=26, spread=3)
        boots(canvas, LACQUER)
        torso(canvas, colours, top=12.5, bottom=21.0, radius=5.0)
        raster.capsule(canvas, 16, 13.5, 16, 20.0, 3.4, LACQUER, ambient=0.36)
        lacing(canvas, 13, 7, (15, 18), colours)
        raster.rect(canvas, 13, 14, 6, 1, GOLD[0])
        for side in (-1, 1):
            raster.disc(canvas, 16 + side * 5.2, 13.5, 2.7, LACQUER,
                        ambient=0.34)
            raster.disc(canvas, 16 + side * 5.2, 13.0, 1.1, GOLD, ambient=0.55)
        raster.capsule(canvas, 11.5, 20.0, 5.5, 22.5, 1.1, STEEL,
                       ambient=0.48)  # the tachi, slung at the hip
        arm(canvas, LACQUER, 11.0, 16.0, 9.0, 19.5)
        hand(canvas, 8.6, 20.0)
        raster.capsule(canvas, 8.6, 20.5, 6.5, 24.0, 1.0, LACQUER,
                       ambient=0.44)  # the saihai, held low
        for offset in range(3):
            canvas.put(6 - offset % 2, 24 + offset, GOLD[1])
        head(canvas, cy=9.8, radius=4.0)
        raster.disc(canvas, 16, 9.0, 4.2, LACQUER, ambient=0.36)
        raster.polygon(canvas, ((11.0, 10.2), (21.0, 10.2), (22.5, 13.2),
                                (9.5, 13.2)), LACQUER, 0.56)  # the shikoro
        raster.rect(canvas, 11, 9, 10, 1, STEEL[2])
        raster.polygon(canvas, ((12.0, 8.5), (16.0, 4.5), (20.0, 8.5),
                                (16.0, 7.0)), GOLD, 0.85,
                       dither=False)  # the maedate, spent once in the style
        face(canvas, 11.0, 2.5)


class Shinobi(Archetype):
    """Fast, acts after striking: the crouch, and one long diagonal.

    The rogue's crouch is kept for the reason every style keeps it: it is the
    only figure below the others' shoulder line. What this setting adds is a
    straight sword slung corner to corner across the back, drawn before the
    body so only its two ends show. The hands stay drawn in against the crouch:
    an arm already thrown forward is what makes a lunge and a cast the same cell.
    """

    name = "rogue"
    label = "Shinobi"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        raster.capsule(canvas, 6.5, 24.0, 27.0, 6.5, 1.2, LACQUER,
                       ambient=0.50)  # the scabbard, behind everything
        raster.disc(canvas, 24.6, 8.6, 1.6, STEEL, ambient=0.50)  # the tsuba
        raster.capsule(canvas, 25.5, 7.6, 27.5, 5.6, 1.0, colours, ambient=0.55)
        legs(canvas, LACQUER, top=21, bottom=27, spread=4, radius=2.0)
        cloak(canvas, LACQUER, top=15.5, bottom=25.0, width=5.8)
        torso(canvas, colours, top=16.0, bottom=21.5, radius=3.9)
        raster.capsule(canvas, 16, 17.0, 16, 21.0, 2.6, LACQUER, ambient=0.38)
        raster.rect(canvas, 12, 19, 9, 2, colours[2])  # the obi
        raster.rect(canvas, 12, 19, 9, 1, colours[3])
        arm(canvas, LACQUER, 12.0, 17.5, 10.0, 21.0, 1.6)
        arm(canvas, LACQUER, 20.0, 17.5, 18.5, 21.0, 1.6)
        raster.disc(canvas, 16, 12.5, 3.8, LACQUER, ambient=0.44)  # the zukin
        raster.polygon(canvas, ((12.5, 13.0), (19.5, 13.0), (18.5, 16.5),
                                (13.5, 16.5)), LACQUER, 0.34)
        raster.rect(canvas, 12, 13, 8, 2, colours[1])
        for side in (-1, 1):
            canvas.put(16 + side * 2, 12, GOLD[1])


class ShrineFox(Archetype):
    """Non-humanoid: the one member of the roster on four legs.

    The ``beast``'s structure exactly, a long low body, a faction marker along
    the back and a second one at head height, stated as the fox that stands at
    an Inari shrine, where the faction marker is the bib those stone foxes
    wear. A single tail, raised: a fan of them is a striking idea that fills as
    one blob at sixteen pixels, and the raised brush is what carries the read
    instead.
    """

    name = "beast"
    label = "Shrine fox"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        raster.capsule(canvas, 10.5, 17.5, 6.5, 11.0, 2.5, FUR,
                       ambient=0.34)  # the brush, raised behind
        raster.capsule(canvas, 6.5, 11.0, 4.5, 7.5, 1.7, FUR, ambient=0.42)
        raster.disc(canvas, 4.0, 6.6, 1.5, PAPER, ambient=0.58)  # the white tip
        for x in (10.0, 13.5, 19.0, 22.0):
            raster.capsule(canvas, x, 20.0, x + 0.4, 25.5, 1.5, FUR,
                           ambient=0.30)
            raster.capsule(canvas, x + 0.2, 24.0, x + 0.6, 26.2, 1.4, LACQUER,
                           ambient=0.42)  # black stockings
        raster.capsule(canvas, 11.0, 18.5, 21.5, 18.0, 4.0, FUR, ambient=0.36)
        raster.polygon(canvas, ((11.5, 14.5), (21.5, 14.0), (22.0, 16.5),
                                (11.5, 17.0)), colours, 0.60)  # the votive cloth
        raster.rect(canvas, 12, 14, 9, 1, colours[3])
        raster.disc(canvas, 24.0, 15.5, 4.0, FUR, ambient=0.42)
        raster.capsule(canvas, 25.5, 17.5, 29.5, 18.2, 1.7, FUR, ambient=0.38)
        raster.disc(canvas, 29.8, 18.4, 1.0, RAMPS["ink"], ambient=0.55)
        raster.disc(canvas, 26.5, 18.0, 1.2, PAPER, ambient=0.60)  # the muzzle
        for offset, tone in ((-2.2, 0.44), (2.0, 0.58)):  # sharp ears
            raster.polygon(canvas, ((24.0 + offset - 1.4, 12.5),
                                    (24.0 + offset + 0.4, 7.5),
                                    (24.0 + offset + 1.4, 12.5)), FUR, tone)
        raster.polygon(canvas, ((22.0, 19.5), (26.5, 19.0), (25.0, 23.0),
                                (21.5, 22.5)), colours, 0.72)  # the bib
        raster.capsule(canvas, 21.0, 14.5, 21.5, GROUND_Y - 8.5, 1.3, colours,
                       ambient=0.55)
        canvas.put(25, 15, GOLD[1])
        canvas.put(26, 15, GOLD[0])


#: One routine per name in ``characters.ARCHETYPE_ORDER``, in that order. The
#: registry in :mod:`.styles` asserts the set matches; the order here is for
#: readers, not for indexing.
ARCHETYPE_CLASSES = (
    Samurai, YumiArcher, Onmyoji, KaguraDancer, TempleMonk, Daimyo, Shinobi,
    ShrineFox,
)

ARCHETYPES = {cls.name: cls() for cls in ARCHETYPE_CLASSES}
