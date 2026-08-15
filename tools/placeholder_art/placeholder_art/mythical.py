# SPDX-License-Identifier: MIT
"""The ``mythical`` character style: the same eight roles, dressed as dragonkin.

This is the roster of :mod:`.characters` redrawn, not recoloured, exactly as
:mod:`.scifi` is. The setting is dragons and their kin: the ``beast`` archetype
is the dragon, and the seven people-roles are the folk who live alongside it.
**There are no multi-tile units anywhere in this repository**, so the dragon
stands in one tile like everything else, and what makes it a boss is the record
an author gives it rather than a footprint the rules do not have.

The ``name`` attributes are the shared archetype keys and the class names are
the mythical readings, because a class called "Drakeguard Rue" must resolve to
the melee archetype whichever style is drawing it.

No new palette entries
----------------------
None, for the reason :mod:`.scifi` records and *Adding a style* in
``tools/placeholder_art/README.md`` states: the
``n64_ci8`` profile writes the whole master palette into every asset's ``PLTE``
chunk, so appending one entry would rewrite art nobody asked to change.
The kit is a deliberately short list of ramps that already exist, and the
shortness is a second constraint rather than taste, as below:

``hide``
    drake scale: the mass of the style, on armour, hoods and the dragon
    itself. It is the ``beast``'s own material in ``medieval``, which is the
    point: here every role wears a little of what the dragon is made of.
``sand``
    horn. Horns, claws, fangs, glaive shafts, a crossbow's prod and an egg's
    shell all come off this one ramp, so the style has a second material that
    reads pale against the scale without spending the white.
``gold``
    ember: runes, dragonfire, eyes. Two steps only, so it is spent on marks
    rather than on mass.
``snow``, ``leather``, ``skin``, ``ink``
    the priest's mantle, a hunter's quiver, the faces that show, and the
    outline every style is drawn inside.

Why the list is short
---------------------
The binding constraint is per *sprite*, not per library
(``tools/placeholder_art/README.md``): the ``n64_ci4`` profile keeps a sprite's
sixteen most-used colours and remaps the rest to their nearest survivor. Drawn
first with a steel ramp beside the scale and a wood ramp beside the horn, this
style spent 2.3% of its opaque texels on that remap against ``medieval``'s
0.4%, and one sprite lost 56. Two materials merged into two the style already
had brought it to 0.3%, which is **better** than either style drawn before it.
The rule that falls out of it, and it is
the one an external artist most needs after the margin rule: **a sprite may name
about five ramps, and a style is the discipline of choosing which five.**

The silhouettes, and why each is the one it is
----------------------------------------------
Separation is per style, and a project names one, so what matters is that these
eight stay eight after the legibility reduction takes them to three tones over
sixteen pixels. Each carries one device nothing else here carries: the
drakeguard a downward-pointing wing-shield, the wyrm-hunter a level crossbow
(the one hard horizontal), the runecaster a closed ring of runes with the cell's
own transparency inside it, the stormsinger a wyrmling roosting on each shoulder
so the outline holds three skulls in a row (the widest), the wyrmpriest a
soft round mantle with no hard edge, the dragonlord a wing breaking the top of
the frame on one side only, the scalethief the crouch, and the dragon a long low
body with a neck raised above the shoulder line.

One device, spent twice and not five times
------------------------------------------
The first draft of this style put :func:`horns` on five of the eight, and the
roster review named that as the fault that made `mythical` the least internally
separable style in the library: a mark carried by four of the roles the review
named (knight, commander, rogue, beast) is not a style's signature, it is four
units drawn once. The mark above a head is now three different marks: a pair of
swept horns (the dragonlord's rank, the dragon's anatomy), a single fore-and-aft
fin (:func:`comb`, the drakeguard), and a one-sided backswept fringe
(:func:`crest`, the scalethief). The stormsinger wears none of its own: the
crests it carries belong to the wyrmlings sitting on it.

The margin rule
---------------
:mod:`.frames` states the rule and enforces it: a pose displaces pixels, and
``walk_contact`` moves everything at or below row 24 outward by one column.
Nothing here is drawn on column 0 or column 31 below the knee, so every pose
fits rather than raising.
"""

from __future__ import annotations

import math

from . import raster
from .characters import (Archetype, FactionColour, arm, boots, cloak, eyes,
                         face, hand, head, legs, torso)
from .palette import RAMPS
from .raster import Canvas

#: Drake scale: the ``hide`` ramp with a lit top step, as the ``beast`` takes it.
#: Three steps alone are too dark to shade a body with.
SCALE = RAMPS["hide"] + (RAMPS["ink"][2],)

#: Horn: claws, fangs, lance shafts, a crossbow's prod, an eggshell.
HORN = RAMPS["sand"]

#: Ember: runes and dragonfire. Two steps, so it stays a mark and never a mass.
EMBER = RAMPS["gold"]

#: The priest's mantle, and the only white in the style.
PALE = RAMPS["snow"] + (RAMPS["ink"][3],)


def horns(canvas: Canvas, cy: float, cx: float = 16.0, spread: float = 4.2,
          sweep: float = 3.5) -> None:
    """A swept pair of horns above a head.

    Two hard diagonals leaving the top of a skull survive the reduction where a
    face does not, and they say *dragonkin* on a helm, a hood and the dragon
    alike.

    **It is spent twice, and the roster review is why.** This was the style's
    shared mark, as the visor is ``scifi``'s, and it was drawn on five of the
    eight, which made `mythical` the least internally separable style in the
    library: four of its silhouettes carried one device and were told apart only
    by what they held. A mark on more than half a roster is not a style, it is
    four units drawn once. So the drakeguard took a helm comb, the scalethief a
    backswept crest and the stormsinger its wyrmlings' own crests, and horns are
    now the dragonlord's rank mark and the dragon's own anatomy and nothing
    else.
    """
    for side in (-1, 1):
        raster.capsule(canvas, cx + side * spread * 0.45, cy,
                       cx + side * spread, cy - sweep, 1.1, HORN, ambient=0.45)


def comb(canvas: Canvas, cy: float, cx: float = 16.0, rise: float = 4.6,
         half_width: float = 1.6) -> None:
    """A single fore-and-aft fin along the crown of a helm.

    The drakeguard's mark, and the first thing this style drew that is not two
    diagonals: one vertical, seen end-on, where :func:`horns` is a pair leaning
    apart. At sixteen pixels a fin is one lit column above a skull and a horn
    pair is two, which is the whole distinction and all of it that survives.
    """
    raster.polygon(canvas, ((cx - half_width, cy), (cx, cy - rise),
                            (cx + half_width, cy)), HORN, 0.80, dither=False)
    raster.line(canvas, int(cx), int(cy - rise) + 1, int(cx), int(cy), HORN[3])


def crest(canvas: Canvas, cy: float, cx: float = 16.0, reach: float = 6.0,
          lean: int = -1) -> None:
    """A low fringe of spines swept back off a hood, all on one side.

    The scalethief's mark. A horizontal serration leaving one side of a skull is
    the third thing a mark above a head can be here, after a pair of diagonals
    and a single fin, and it is the only asymmetric one, which suits the one
    archetype in the roster that is drawn crouched and turned.
    """
    for step in range(3):
        base = cx + lean * (1.6 + step * 1.7)
        raster.polygon(
            canvas,
            ((base, cy + 0.4), (base + lean * reach * 0.42, cy - 1.4 + step * 0.9),
             (base + lean * 0.6, cy - 1.6)),
            HORN, 0.62 + 0.08 * (step % 2),
        )


class Drakeguard(Archetype):
    """Melee, shield forward: scale plate behind a shield cut from a wing.

    The knight's shield is a round disc and the trooper's a slab rectangle;
    this one is a kite, pointed at the bottom. At sixteen pixels the three are
    a blob, a right angle and a wedge, and the wedge is the only one that
    points.
    """

    name = "knight"
    label = "Drakeguard"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, SCALE, top=19, bottom=26, spread=3, radius=2.0)
        boots(canvas, SCALE)
        torso(canvas, colours, top=13.0, bottom=21.0, radius=4.8)
        raster.capsule(canvas, 16, 14.0, 16, 19.0, 3.2, SCALE, ambient=0.30)
        for side in (-1, 1):  # spiked pauldrons, swept back like small wings
            raster.polygon(canvas, ((16 + side * 2.8, 12.5),
                                    (16 + side * 7.0, 10.5),
                                    (16 + side * 5.0, 15.5)), SCALE, 0.55)
        arm(canvas, SCALE, 20.0, 15.0, 22.5, 12.5)
        hand(canvas, 22.8, 12.2)
        # A fanged glaive, held short: the shield carries the read, not this.
        raster.capsule(canvas, 22.0, 12.5, 27.5, 6.0, 1.1, HORN, ambient=0.32)
        raster.polygon(canvas, ((26.5, 7.5), (29.5, 1.5), (25.5, 4.0)), HORN,
                       0.85, dither=False)
        # Wing shield: a kite pointed at the bottom, ribbed like a membrane.
        # Drawn wide and light on purpose: it is the whole read at sixteen
        # pixels, and a dark shield beside a dark body is one shape.
        raster.polygon(canvas, ((5.5, 9.5), (13.0, 12.5), (9.5, 24.0),
                                (2.5, 15.0)), colours, 0.58)
        raster.polygon(canvas, ((6.5, 11.5), (11.5, 13.5), (9.2, 20.5)),
                       colours, 0.88)
        for step in range(3):
            raster.line(canvas, 4 + step, 12 + step * 2, 11, 15 + step * 2,
                        SCALE[2])
        head(canvas, cy=9.0, radius=4.2)
        raster.disc(canvas, 16, 8.4, 4.4, SCALE, ambient=0.32)
        raster.rect(canvas, 13, 9, 6, 2, RAMPS["ink"][1])
        raster.rect(canvas, 14, 9, 4, 1, EMBER[1])
        comb(canvas, 7.2)


class WyrmHunter(Archetype):
    """Ranged, cannot strike adjacent: a horn crossbow held level.

    The archer's read is a vertical arc down one side and the sniper's a
    corner-to-corner diagonal. This one is the third line a frame can hold, a
    hard horizontal across the chest, and it is the only horizontal in the
    roster that is not the dragon's back.
    """

    name = "archer"
    label = "Wyrm-hunter"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, SCALE, top=19, bottom=26, spread=3)
        boots(canvas, SCALE)
        # Quiver of bolts over the shoulder, fletched in horn.
        raster.capsule(canvas, 21.0, 11.0, 19.5, 18.0, 1.9, RAMPS["leather"],
                       ambient=0.30)
        for offset in range(3):
            canvas.put(21 - offset, 8 + offset, HORN[3])
            canvas.put(21 - offset, 9 + offset, HORN[1])
        torso(canvas, colours, top=13.0, bottom=20.5, radius=4.3)
        raster.capsule(canvas, 16, 14.0, 16, 19.0, 2.6, SCALE, ambient=0.36)
        arm(canvas, colours, 12.5, 15.5, 10.5, 17.0)
        arm(canvas, colours, 19.5, 15.5, 21.5, 17.0)
        # The prod and its drawn string, level and thin: thin so the body still
        # reads through it, level so the horizontal is the whole silhouette cue.
        raster.capsule(canvas, 2.5, 16.5, 29.5, 16.5, 0.9, HORN, ambient=0.42)
        raster.capsule(canvas, 13.0, 17.5, 21.0, 17.5, 1.2, HORN, ambient=0.26)
        raster.line(canvas, 3, 18, 29, 18, SCALE[1])
        raster.polygon(canvas, ((21.0, 16.5), (28.0, 20.0), (21.0, 19.5)),
                       HORN, 0.80, dither=False)
        canvas.put(16, 17, EMBER[1])
        head(canvas, cy=9.5, radius=3.9)
        raster.disc(canvas, 16, 8.0, 4.2, colours, ambient=0.40)  # scaled hood
        raster.polygon(canvas, ((19.5, 5.0), (23.0, 9.5), (18.0, 9.0)), colours,
                       0.30)
        face(canvas, 10.2, 2.8)


class Runecaster(Archetype):
    """Magic, short band: a narrow figure under a closed ring of runes.

    Narrow, like the mage and the psion it stands in for, and for the same
    reason: the stormcaller's replacement is wide and the pair must stay
    unmistakable. Its own device is a **hole**: a circle of marks with the
    sprite's transparency inside it, which no other silhouette here has.
    """

    name = "mage"
    label = "Runecaster"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=12.5, bottom=27.0, width=6.2)
        torso(canvas, colours, top=13.5, bottom=20.0, radius=3.8)
        arm(canvas, colours, 12.2, 15.5, 10.8, 20.0, 1.5)
        arm(canvas, colours, 19.8, 15.5, 21.2, 20.0, 1.5)
        hand(canvas, 10.6, 20.4)
        hand(canvas, 21.4, 20.4)
        # The ring: a closed circle of runes with the cell's own transparency
        # inside it. A hole is the one silhouette device no other archetype in
        # any style has, and it is what a three-shade reduction keeps.
        for step in range(24):
            angle = step / 24.0 * math.tau
            x = int(round(16 + math.cos(angle) * 6.4))
            y = int(round(5.6 + math.sin(angle) * 4.6))
            canvas.put(x, y, EMBER[step % 2])
        raster.line(canvas, 16, 10, 16, 11, EMBER[1])
        head(canvas, cy=13.5, radius=3.6)
        raster.disc(canvas, 16, 12.8, 3.9, SCALE, ambient=0.38)
        face(canvas, 14.2, 2.4)


class Stormsinger(Archetype):
    """Area effect: two wyrmlings perched on the shoulders, arms hanging.

    The wide radiating star the stormcaller is, rebuilt out of living things.
    It is the widest silhouette here and the only symmetric one that reaches
    past the shoulders, which is what keeps it clear of the runecaster at the
    sizes where only outline survives.

    Taking the width from **raised arms** with a wyrmling thrown off each hand
    reads as the same rig four styles share rather than as anybody, so the
    wyrmlings come home: they sit on the shoulders with their necks arching out
    and up, and the arms hang. The width is the same width, carried by the two
    necks instead of by two arms, and what it buys is **three skulls in one
    silhouette**, which nothing else in the library has, and a body whose arms
    are not already thrown out, which is what lets ``lunge`` and ``cast`` be two
    pictures of it.

    It is also the style's third de-horned role. See :func:`horns`.
    """

    name = "stormcaller"
    label = "Stormsinger"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=13.0, bottom=27.0, width=7.4)
        torso(canvas, colours, top=13.0, bottom=20.0, radius=4.0)
        arm(canvas, colours, 12.5, 15.0, 11.0, 20.0)
        arm(canvas, colours, 19.5, 15.0, 21.0, 20.0)
        hand(canvas, 10.7, 20.5)
        hand(canvas, 21.3, 20.5)
        for direction in (-1, 1):
            # A wyrmling roosting on each shoulder, drawn sideways-on and
            # *level*: a folded membrane hung down the outside, a body gripping
            # the shoulder, a short neck, and a head no higher than the one it
            # sits beside. Level is the whole point: a neck that rose would be
            # an arm again, and this style already has a role whose device
            # points upward.
            x = 16 + direction * 7.5
            raster.polygon(canvas, ((x, 13.0), (x - direction * 3.0, 19.5),
                                    (x + direction * 1.5, 18.0)), colours, 0.60)
            raster.disc(canvas, x, 12.6, 2.6, SCALE, ambient=0.38, squash=1.30)
            raster.capsule(canvas, x - direction * 0.5, 11.8,
                           x + direction * 2.4, 11.2, 1.6, SCALE, ambient=0.34)
            raster.disc(canvas, x + direction * 4.0, 11.0, 2.2, SCALE,
                        ambient=0.48)
            raster.capsule(canvas, x + direction * 4.6, 11.4,
                           x + direction * 6.6, 11.8, 1.2, SCALE, ambient=0.40)
            raster.polygon(canvas, ((x + direction * 3.0, 9.4),
                                    (x + direction * 6.6, 8.0),
                                    (x + direction * 4.4, 10.4)), HORN, 0.72)
            canvas.put(int(x + direction * 4.0), 11, EMBER[1])
        head(canvas, cy=10.5, radius=3.9)
        raster.disc(canvas, 16, 9.9, 4.1, SCALE, ambient=0.36)
        face(canvas, 11.1, 2.6)
        raster.rect(canvas, 13, 6, 6, 1, EMBER[0])  # the singer's ember circlet


class Wyrmpriest(Archetype):
    """Support, no offensive weapon: a soft mantle and an egg held up.

    Keeps the healer's two loudest devices for the reason the medic keeps them:
    a pale mass reads at any size, and a single glyph says what the unit is for.
    Both change here. The mass is round rather than robed, and the glyph is a
    cracked shell with light in it rather than a cross.
    """

    name = "healer"
    label = "Wyrmpriest"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, PALE, top=12.0, bottom=27.0, width=7.0)
        # The roundest, lowest mass in the roster: no hard edge anywhere on it.
        raster.disc(canvas, 16, 19.5, 6.4, PALE, ambient=0.52, squash=1.30)
        raster.rect(canvas, 14, 12, 4, 14, colours[2])  # faction stole
        raster.rect(canvas, 14, 12, 2, 14, colours[3])
        arm(canvas, PALE, 11.0, 15.5, 9.5, 19.0)
        arm(canvas, PALE, 21.0, 15.5, 22.5, 12.5)
        hand(canvas, 9.3, 19.5)
        # The egg, held up and hatching: the style's one clear glyph.
        raster.disc(canvas, 23.5, 9.0, 3.2, HORN, ambient=0.55, squash=1.18)
        raster.line(canvas, 22, 11, 24, 6, EMBER[1])
        raster.line(canvas, 23, 9, 25, 10, EMBER[1])
        raster.line(canvas, 22, 8, 24, 9, EMBER[0])
        head(canvas, cy=9.5, radius=3.9)
        raster.disc(canvas, 16, 8.2, 4.3, PALE, ambient=0.58)
        raster.rect(canvas, 12, 12, 8, 1, PALE[1])
        face(canvas, 9.8, 2.8)
        raster.rect(canvas, 12, 6, 8, 2, colours[2])
        raster.rect(canvas, 12, 6, 8, 1, colours[3])


class Dragonlord(Archetype):
    """The leader, and a win condition: one wing raised over a mailed back.

    Rank stated three ways again, because one of them has to survive every
    reduction. Mass is the cape, silhouette is a wing that breaks the top of
    the frame on one side only, detail is ember trim. The asymmetry is what
    separates it from the stormsinger, which is wide on both sides.
    """

    name = "commander"
    label = "Dragonlord"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        raster.polygon(canvas, ((22.0, 22.5), (24.5, 0.0), (30.5, 5.5),
                                (28.5, 19.5)), colours, 0.55)
        raster.polygon(canvas, ((23.0, 19.0), (24.5, 2.5), (27.5, 6.5)),
                       colours, 0.80)
        for step in range(4):  # the wing's fingers
            raster.line(canvas, 24, 3 + step * 4, 30, 7 + step * 3, SCALE[2])
        cloak(canvas, colours, top=11.5, bottom=27.0, width=7.6)
        legs(canvas, SCALE, top=19, bottom=26, spread=3)
        boots(canvas, SCALE)
        torso(canvas, colours, top=12.5, bottom=21.0, radius=5.0)
        raster.capsule(canvas, 16, 13.5, 16, 19.5, 3.4, SCALE, ambient=0.30)
        for row in (15, 17):
            raster.rect(canvas, 13, row, 5, 1, EMBER[0])
        for side in (-1, 1):
            raster.disc(canvas, 16 + side * 5.0, 13.5, 2.8, SCALE, ambient=0.34)
        arm(canvas, SCALE, 11.0, 15.0, 8.5, 18.5)
        hand(canvas, 8.2, 19.0)
        raster.capsule(canvas, 8.0, 19.5, 6.5, 11.5, 1.2, HORN, ambient=0.34)
        raster.polygon(canvas, ((5.0, 12.0), (6.5, 5.5), (8.5, 12.0)), HORN,
                       0.85, dither=False)
        head(canvas, cy=8.5, radius=4.2)
        raster.disc(canvas, 16, 7.8, 4.4, SCALE, ambient=0.30)
        raster.rect(canvas, 12, 8, 8, 2, RAMPS["ink"][1])
        raster.rect(canvas, 13, 8, 6, 1, EMBER[1])
        horns(canvas, 6.2, spread=5.0, sweep=4.4)


class Scalethief(Archetype):
    """Fast, acts after striking: crouched, with two fangs taken from a wyrm.

    The rogue's crouch is kept because it is the only pose in either roster
    below the others' shoulder line and nothing cheaper does the same work.
    What changes is the weapons: curved fangs rather than straight daggers, so
    the pair reads as taken rather than forged.
    """

    name = "rogue"
    label = "Scalethief"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, SCALE, top=21, bottom=27, spread=4, radius=2.1)
        cloak(canvas, SCALE, top=14.0, bottom=25.0, width=6.0)
        torso(canvas, colours, top=15.0, bottom=21.0, radius=4.0)
        raster.rect(canvas, 12, 17, 9, 2, SCALE[1])
        raster.rect(canvas, 12, 17, 9, 1, HORN[1])
        arm(canvas, SCALE, 12.0, 16.5, 8.5, 19.0, 1.7)
        arm(canvas, SCALE, 20.0, 16.5, 23.5, 14.0, 1.7)
        hand(canvas, 8.2, 19.4)
        hand(canvas, 23.8, 13.6)
        for x0, y0, x1, y1 in ((7.6, 20.2, 4.8, 23.0), (24.4, 12.8, 27.2, 10.0)):
            raster.capsule(canvas, x0, y0, x1, y1, 1.1, HORN, ambient=0.50)
        raster.polygon(canvas, ((4.8, 22.0), (2.5, 24.5), (6.0, 23.5)), HORN,
                       0.75)
        raster.polygon(canvas, ((27.2, 11.0), (29.5, 7.0), (25.5, 9.0)), HORN,
                       0.75)
        head(canvas, cy=11.0, radius=3.8)
        raster.disc(canvas, 16, 10.0, 4.3, SCALE, ambient=0.42)
        raster.polygon(canvas, ((20.0, 7.0), (24.0, 12.0), (19.0, 11.0)), SCALE,
                       0.35)
        raster.rect(canvas, 13, 12, 7, 2, colours[2])
        raster.rect(canvas, 13, 12, 7, 1, colours[3])
        eyes(canvas, cy=10, spread=2, colour=EMBER[1])
        crest(canvas, 8.4, reach=6.4, lean=-1)


class Dragon(Archetype):
    """Non-humanoid: a long low body under spread wings, its neck raised.

    Keeps the beast's whole structural argument: a horizontal body, a faction
    ridge along the back, a second faction marker at head height. That is what
    makes an archetype read as *not a person* at any size. What changes is
    everything above it: two membranes wider than any other silhouette here, and
    a head carried above the shoulder line rather than level with it.

    It stands in one tile. Nothing in the rules expresses a footprint larger
    than one, so what makes a dragon a boss is the record an author gives it,
    not a shape.
    """

    name = "beast"
    label = "Dragon"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        for direction in (-1, 1):  # membranes, drawn behind the body
            raster.polygon(canvas, ((16.0, 15.0), (16 + direction * 13.0, 3.5),
                                    (16 + direction * 10.5, 15.5)), colours,
                           0.48)
            for step in range(3):
                raster.line(canvas, 16, 14,
                            int(16 + direction * (5 + step * 4)), 5 + step * 3,
                            SCALE[2])
        for x in (10.5, 14.0, 19.0, 22.5):  # four clawed legs
            raster.capsule(canvas, x, 20.0, x + 0.6, 25.5, 1.6, SCALE,
                           ambient=0.30)
            raster.disc(canvas, x + 0.9, 25.8, 1.5, SCALE, ambient=0.42,
                        squash=1.5)
        raster.capsule(canvas, 11.0, 19.0, 21.0, 18.5, 4.0, SCALE, ambient=0.34)
        raster.capsule(canvas, 10.0, 18.0, 5.0, 21.5, 1.5, SCALE, ambient=0.36)
        raster.polygon(canvas, ((5.5, 20.5), (2.0, 23.5), (6.0, 23.0)), HORN,
                       0.62)
        for step in range(4):  # spine ridge, in the faction colour
            raster.polygon(canvas, ((12.0 + step * 2.6, 15.6),
                                    (13.2 + step * 2.6, 11.6),
                                    (14.4 + step * 2.6, 15.6)), colours,
                           0.58 + 0.06 * (step % 2))
        raster.capsule(canvas, 20.5, 17.5, 24.5, 10.0, 2.4, SCALE, ambient=0.38)
        raster.capsule(canvas, 19.5, 12.5, 20.0, 17.5, 1.4, colours,
                       ambient=0.55)
        raster.disc(canvas, 25.5, 8.8, 3.5, SCALE, ambient=0.44)
        raster.capsule(canvas, 26.5, 9.8, 29.0, 10.6, 1.7, SCALE, ambient=0.36)
        for offset in range(3):  # teeth
            canvas.put(27 + offset % 2, 12 + offset, HORN[3])
        canvas.put(25, 8, EMBER[1])
        canvas.put(26, 8, EMBER[0])
        horns(canvas, 6.0, cx=25.5, spread=3.2, sweep=3.2)


#: One routine per name in ``characters.ARCHETYPE_ORDER``, in that order. The
#: registry in :mod:`.styles` asserts the set matches; the order here is for
#: readers, not for indexing.
ARCHETYPE_CLASSES = (
    Drakeguard, WyrmHunter, Runecaster, Stormsinger, Wyrmpriest, Dragonlord,
    Scalethief, Dragon,
)

ARCHETYPES = {cls.name: cls() for cls in ARCHETYPE_CLASSES}
