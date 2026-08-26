# SPDX-License-Identifier: MIT
"""The ``scifi`` character style: the same eight roles, dressed as a war fleet.

This is the roster of :mod:`.characters` redrawn, not recoloured. Each routine
here answers the same tactical question its medieval counterpart does, and
``tools/placeholder_art/README.md``'s mapping table names the answers: knight is a
trooper, archer a sniper, mage a psion, stormcaller a drone swarm, healer a
medic, commander a captain, rogue an infiltrator, beast a xenoform. The class
names below are those readings; the ``name`` attributes are the shared
archetype keys, because a class called "Sniper Kade" must resolve to the ranged
archetype whichever style is drawing it.

The shared body helpers, the faction disc and the ink outline come from
:mod:`.characters` unchanged. A style that reinvented proportions would make
two units of the same archetype different sizes, and the disc and the outline
are the frame every style is drawn inside.

No new palette entries
----------------------
This commission appends **nothing** to the master palette, and that is a
constraint the pipeline imposes rather than a saving taken. The ``n64_ci8``
profile writes the whole 124-entry master palette into every asset's ``PLTE``
chunk, so growing the palette rewrites every checked-in indexed asset. That
would move ``medieval``'s bytes, which nothing about drawing a second style may
do.

So the sci-fi kit is assembled from ramps that already exist, and the mapping
is deliberate rather than incidental:

``steel``
    hull, plating, weapons: the mass of the style.
``ink``
    the sealed undersuit beneath the plate, and, at its paper-white top step,
    the hot core of an emitter.
``water``
    every energy read: visor slits, blade edges, drone lights, psion focus.
    Kept to accents rather than mass, because the ramp is a terrain ramp and a
    unit that reads as ground is not a unit. Inside an ink outline, over a
    faction disc, a few pixels of cyan read as light rather than as river.
``snow``
    ceramic white: the medic's suit, and the coolest highlight available.
``gold``
    thruster and muzzle heat, the one warm light in the style.
``hide``
    the xenoform's chitin, as it is the beast's hide.
``skin``
    the faces that are visible. Fewer are than in ``medieval``: a sealed helm
    is the sci-fi read, so only the sniper, the psion and the captain show one,
    which is itself a silhouette cue.

The two legibility passes
-------------------------
**Small.** Three tones and a restored outline over sixteen pixels, so the
whole read is silhouette. The eight are separated by outline shape before
anything else: the trooper is the only rectangle (slab shield), the sniper the
only long diagonal (rail barrel), the psion the only figure whose hem stops
short of its own ground, the drone swarm the only figure with detached mass out
at the frame edges, the medic the only wide soft dome, the captain the only one
breaking the top edge, the infiltrator the only crouch, the xenoform the only
horizontal body.

Narrow is a proportion, not a size
----------------------------------
The psion is the style's narrow figure, and drawing it *small* instead is the
easy mistake: 294 opaque texels where the other seven mean 474, and an opaque
box bounded entirely by the faction disc every unit shares. What follows from
that is that on a board it reads as a unit a size smaller than the units beside
it. So the figure is drawn at the style's own
scale, and the narrowness is carried by the robe's taper and the gap under its
hem.
"""

from __future__ import annotations

import math

from . import raster
from .characters import (GROUND_Y, Archetype, FactionColour, arm, boots, cloak,
                         eyes, face, hand, head, legs, torso)
from .palette import RAMPS
from .raster import Canvas

#: Ceramic white, four steps: the ``snow`` ramp is three and every shading
#: helper wants a fourth. The medic wears it as the healer wears the same ramp.
CERAMIC = RAMPS["snow"] + (RAMPS["ink"][3],)

#: Energy: the cool end of ``water`` used as light rather than as liquid.
ENERGY = RAMPS["water"]


def visor(canvas: Canvas, top: int, width: int = 8, height: int = 2) -> None:
    """A lit visor band across a sealed helm.

    The single most style-defining mark here, and the reason most of the roster
    shows no face: a dark recess with a bright slit in it survives the
    reduction as one hard horizontal, where a face survives as nothing.
    """
    left = 16 - width // 2
    raster.rect(canvas, left, top - 1, width, height + 2, RAMPS["ink"][0])
    for row in range(height):
        raster.rect(canvas, left + 1, top + row, width - 2, 1, ENERGY[4 - row])


def thruster(canvas: Canvas, cx: float, cy: float, radius: float = 1.6) -> None:
    """A glowing vent: gold body, white core. Marks anything that hovers."""
    raster.disc(canvas, cx, cy, radius, RAMPS["gold"], ambient=0.70)
    canvas.put(int(cx), int(cy), RAMPS["ink"][3])


class Trooper(Archetype):
    """Melee, shield forward: powered armour and a slab riot shield.

    The shield is rectangular where the knight's is round, on purpose. At
    sixteen pixels the two archetypes are a blob beside a shape, and a hard
    right-angled edge is the only shape that reads.
    """

    name = "knight"
    label = "Trooper"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, RAMPS["ink"], top=19, bottom=26, spread=3, radius=2.0)
        boots(canvas, RAMPS["steel"])
        torso(canvas, colours, top=13.0, bottom=21.0, radius=4.8)
        # Squared chest plate over the faction suit, colour left at the flanks.
        raster.rect(canvas, 13, 14, 6, 6, RAMPS["steel"][1])
        raster.rect(canvas, 13, 14, 6, 1, RAMPS["steel"][2])
        for side in (-1, 1):  # blocky pauldrons, not the knight's round discs
            raster.rect(canvas, 16 + side * 6 - 2, 12, 4, 5, RAMPS["steel"][1])
            raster.rect(canvas, 16 + side * 6 - 2, 12, 4, 1, RAMPS["steel"][2])
        # Carbine braced on the weapon side: short and level, unlike a sword.
        arm(canvas, RAMPS["ink"], 20.0, 15.0, 22.5, 13.0)
        raster.capsule(canvas, 21.0, 13.5, 29.0, 11.5, 1.3, RAMPS["steel"],
                       ambient=0.45)
        raster.capsule(canvas, 22.5, 14.5, 24.0, 17.0, 1.0, RAMPS["ink"],
                       ambient=0.40)
        thruster(canvas, 29.4, 11.3, 1.2)
        # Slab shield: the rectangle nothing else in the roster has.
        raster.rect(canvas, 6, 12, 7, 12, colours[1])
        raster.rect(canvas, 7, 13, 5, 10, colours[2])
        raster.rect(canvas, 7, 13, 5, 1, colours[3])
        raster.rect(canvas, 6, 17, 7, 1, RAMPS["steel"][2])
        head(canvas, cy=9.0, radius=4.2)
        raster.disc(canvas, 16, 8.4, 4.4, RAMPS["steel"], ambient=0.30)
        visor(canvas, 9)


class Sniper(Archetype):
    """Ranged, cannot strike adjacent: a rail rifle across the whole frame.

    The archer's read is a bow arc down one side; this one is a single long
    diagonal corner to corner. Both are lines, and a line is what survives; the
    two lines point in different directions, which is what separates them.
    """

    name = "archer"
    label = "Sniper"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, RAMPS["ink"], top=19, bottom=26, spread=3)
        boots(canvas, RAMPS["ink"])
        torso(canvas, colours, top=13.0, bottom=20.5, radius=4.3)
        raster.capsule(canvas, 16, 14.0, 16, 19.0, 2.6, RAMPS["ink"],
                       ambient=0.36)
        raster.rect(canvas, 13, 16, 6, 1, ENERGY[3])  # spare rail cells
        arm(canvas, colours, 12.0, 15.5, 9.0, 18.0)
        arm(canvas, colours, 19.5, 15.0, 16.5, 13.5)
        hand(canvas, 8.8, 18.4)
        hand(canvas, 16.2, 13.2)
        # Rail barrel, muzzle high on the off side, brace low on the near one.
        raster.capsule(canvas, 4.5, 24.0, 27.0, 5.5, 1.2, RAMPS["steel"],
                       ambient=0.40)
        raster.capsule(canvas, 9.0, 20.0, 13.0, 16.5, 1.9, RAMPS["ink"],
                       ambient=0.34)
        raster.disc(canvas, 14.5, 15.5, 1.7, RAMPS["steel"], ambient=0.50)
        canvas.put(15, 15, ENERGY[4])
        thruster(canvas, 27.4, 5.2, 1.3)
        head(canvas, cy=9.5, radius=3.9)
        # Cowl left open: the sniper is one of three faces the style shows.
        raster.disc(canvas, 16, 8.0, 4.2, colours, ambient=0.40)
        raster.polygon(canvas, ((19.5, 5.0), (23.0, 9.5), (18.0, 9.0)), colours, 0.30)
        face(canvas, 10.2, 2.8)
        raster.rect(canvas, 17, 9, 4, 2, RAMPS["ink"][0])  # targeting monocle
        canvas.put(19, 9, ENERGY[4])


class Psion(Archetype):
    """Magic, short band: a hovering figure held open by two field vanes.

    Narrow in the *body*, like the mage it stands in for, and for the same
    reason: the stormcaller's replacement is wide, and the pair have to stay
    unmistakable from each other once they are a dozen pixels tall. It is the
    only archetype in either style whose silhouette does not reach the ground
    line, and the drop under the hem is the whole read.

    Narrow in the body is not the same as small in the cell, and the roster
    review is where that distinction was paid for. Drawn as a body alone this
    figure painted 294 opaque texels against its style's other seven at a mean
    of 474, and its opaque box was **entirely** the shared faction disc. So it
    read as a unit a size smaller than everything it stands beside. The vanes
    are the fix: the figure now
    reaches past the disc on both sides, at chest height where nothing else in
    this style is wide, while the body inside them stays the taper it was.
    """

    name = "mage"
    label = "Psion"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        # The robe: a bell at the style's own scale, flaring to the widest row
        # of the figure and then drawn back to three trailing points that stop
        # short of the ground. This one floats, and the gap under the hem is the
        # whole read, so the points are what has to survive the reduction.
        raster.polygon(
            canvas,
            ((10.5, 12.5), (21.5, 12.5), (24.5, 21.0), (16.0, 24.5), (7.5, 21.0)),
            colours, 0.44,
        )
        for offset in (-5.0, 0.0, 5.0):
            raster.polygon(
                canvas,
                ((16 + offset - 2.4, 20.5), (16 + offset, 25.8),
                 (16 + offset + 2.4, 20.5)),
                colours, 0.62,
            )
        thruster(canvas, 16.0, GROUND_Y - 1.6, 1.6)
        torso(canvas, colours, top=13.0, bottom=20.5, radius=4.6)
        # Arms carried out and down, hands open and clear of the body. Down
        # rather than raised, and out rather than folded: the psion is the style
        # 's narrow figure and it still has to reach past its own faction disc,
        # or it is drawn a size smaller than everything it stands beside.
        arm(canvas, colours, 12.5, 15.0, 7.0, 19.0, 1.6)
        arm(canvas, colours, 19.5, 15.0, 25.0, 19.0, 1.6)
        hand(canvas, 6.2, 19.6)
        hand(canvas, 25.8, 19.6)
        # Focus shard, suspended above the head on a thread of field: with the
        # body this narrow the middle of the archetype is one tall vertical.
        raster.polygon(canvas, ((16.0, 0.0), (19.2, 4.0), (16.0, 8.0),
                                (12.8, 4.0)), ENERGY, 0.95, dither=False)
        raster.polygon(canvas, ((16.0, 1.4), (17.8, 4.0), (16.0, 6.6),
                                (14.2, 4.0)), CERAMIC, 0.90, dither=False)
        raster.line(canvas, 16, 8, 16, 9, ENERGY[3])
        head(canvas, cy=11.0, radius=3.9)
        raster.disc(canvas, 16, 10.4, 4.1, RAMPS["ink"], ambient=0.38)
        face(canvas, 11.6, 2.7)
        raster.rect(canvas, 13, 8, 6, 1, ENERGY[4])  # implant band


class DroneSwarm(Archetype):
    """Area effect: a handler at a control slab, and the drones it has loosed.

    The wide radiating star the stormcaller is, rebuilt out of detached parts.
    Where the stormcaller's bolts leave the hands and taper away, these are
    solid bodies sitting at the frame edge with a gap of nothing between them
    and the handler: the only silhouette in the roster that is not connected.

    **The arms are down**, and the roster review is why. Four styles drew this
    role as the same figure with both arms raised, which reduces to a rig rather
    than to anybody; here the width was never carried by the arms but by the
    gap between the drones and the body, so lowering them onto a waist-height
    slab costs the silhouette nothing and buys back a person working a machine.
    It also buys the sequence its two gestures: ``lunge`` and ``cast`` displace a
    body identically between the shoulder cut and the knee cut, so a figure whose
    arms are already thrown out has nothing left for them to differ by.
    """

    name = "stormcaller"
    label = "Drone swarm"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=13.0, bottom=27.0, width=7.0)
        torso(canvas, colours, top=13.0, bottom=20.0, radius=4.0)
        arm(canvas, colours, 12.5, 15.0, 11.0, 19.0)
        arm(canvas, colours, 19.5, 15.0, 21.0, 19.0)
        hand(canvas, 10.7, 19.4)
        hand(canvas, 21.3, 19.4)
        # The control slab, carried at the waist in both hands: the mass this
        # figure holds instead of holding its arms up.
        raster.rect(canvas, 10, 19, 12, 3, RAMPS["steel"][1])
        raster.rect(canvas, 10, 19, 12, 1, RAMPS["steel"][2])
        raster.rect(canvas, 12, 20, 8, 1, ENERGY[4])
        # Four drones, detached, two a side, the outer pair higher and smaller.
        for direction in (-1, 1):
            for reach, level, size in ((7.2, 9.5, 2.7), (11.8, 4.5, 2.3)):
                cx = 16 + direction * reach
                raster.disc(canvas, cx, level, size, RAMPS["steel"], ambient=0.36,
                            squash=1.4)
                raster.rect(canvas, int(cx) - 1, int(level), 3, 1, ENERGY[4])
                thruster(canvas, cx, level + size / 1.4, 0.9)
        head(canvas, cy=10.0, radius=3.8)
        raster.disc(canvas, 16, 9.4, 4.0, RAMPS["steel"], ambient=0.34)
        visor(canvas, 10, width=7)
        # Antenna array: three straight rods, wider than the head.
        raster.rect(canvas, 12, 7, 8, 2, colours[1])
        for offset in (-4, 0, 4):
            raster.line(canvas, 16 + offset, 7, 16 + offset, 3, RAMPS["steel"][1])
            canvas.put(16 + offset, 2, ENERGY[4])


class Medic(Archetype):
    """Support, no offensive weapon: ceramic whites and a lit cross.

    Keeps the healer's two loudest devices, a white mass and a cross glyph,
    because both are style-independent reads. What changes is what carries
    them: a sealed suit rather than a robe, a raised injector rather than a
    staff.
    """

    name = "healer"
    label = "Medic"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, CERAMIC, top=19, bottom=26, spread=3, radius=2.0)
        boots(canvas, RAMPS["ink"])
        # Soft, wide suit: the roundest torso in the roster, no hard plate.
        torso(canvas, CERAMIC, top=12.5, bottom=21.5, radius=5.0)
        raster.rect(canvas, 14, 12, 4, 12, colours[2])  # faction stole
        raster.rect(canvas, 14, 12, 2, 12, colours[3])
        arm(canvas, CERAMIC, 11.5, 15.0, 10.0, 19.0)
        arm(canvas, CERAMIC, 20.5, 15.0, 23.0, 12.0)
        hand(canvas, 10.0, 19.5)
        # Injector held up, and the cross that says what this unit is for.
        raster.capsule(canvas, 23.0, 12.5, 24.5, 7.0, 1.6, RAMPS["steel"],
                       ambient=0.44)
        raster.rect(canvas, 22, 4, 5, 2, ENERGY[4])
        raster.rect(canvas, 23, 2, 2, 6, ENERGY[4])
        raster.rect(canvas, 23, 3, 1, 4, CERAMIC[3])
        head(canvas, cy=9.5, radius=3.9)
        raster.disc(canvas, 16, 8.6, 4.4, CERAMIC, ambient=0.55)
        visor(canvas, 9, width=7)
        raster.rect(canvas, 12, 5, 8, 2, colours[2])  # faction band at the brow
        raster.rect(canvas, 12, 5, 8, 1, colours[3])
        raster.rect(canvas, 13, 17, 3, 1, ENERGY[3])  # chest cross, small
        raster.rect(canvas, 14, 16, 1, 3, ENERGY[3])


class Captain(Archetype):
    """The leader, and a win condition: rank stated three ways over again.

    Mass (a full-height command drape), silhouette (a comms mast that breaks
    the top of the frame), detail (gold rank). The commander's answer, and it
    is kept rather than reinvented because the reason for it is the reduction,
    not the genre: on a map, finding the leader must not depend on colour.
    """

    name = "commander"
    label = "Captain"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        # Comms mast, planted, breaking the top edge.
        raster.capsule(canvas, 25.0, 1.0, 25.0, 27.0, 1.1, RAMPS["steel"],
                       ambient=0.42)
        raster.polygon(canvas, ((24.0, 3.0), (31.0, 5.0), (24.0, 11.0)), colours,
                       0.72, dither=False)
        raster.polygon(canvas, ((24.0, 3.0), (28.0, 3.8), (24.0, 7.0)), colours,
                       0.95, dither=False)
        for offset in (0, 2, 4):  # array elements up the mast
            raster.rect(canvas, 23, 13 + offset, 4, 1, RAMPS["steel"][2])
        canvas.put(25, 0, ENERGY[4])
        cloak(canvas, colours, top=11.5, bottom=27.0, width=8.2)
        legs(canvas, RAMPS["ink"], top=19, bottom=26, spread=3)
        boots(canvas, RAMPS["steel"])
        torso(canvas, colours, top=12.5, bottom=21.0, radius=5.0)
        raster.rect(canvas, 13, 14, 6, 6, RAMPS["steel"][1])
        raster.rect(canvas, 13, 14, 6, 1, RAMPS["steel"][2])
        for side in (-1, 1):
            raster.rect(canvas, 16 + side * 6 - 2, 12, 4, 4, RAMPS["steel"][1])
            raster.rect(canvas, 16 + side * 6 - 2, 12, 4, 1, RAMPS["gold"][1])
        for row in (15, 17):  # rank bars
            raster.rect(canvas, 14, row, 4, 1, RAMPS["gold"][0])
        arm(canvas, RAMPS["ink"], 11.0, 15.0, 8.5, 18.5)
        hand(canvas, 8.2, 19.0)
        raster.capsule(canvas, 7.5, 19.5, 8.5, 15.5, 1.2, RAMPS["steel"],
                       ambient=0.50)
        thruster(canvas, 8.6, 15.0, 1.1)
        head(canvas, cy=8.5, radius=4.2)
        # Open officer's helm: the face shows, which no soldier's does.
        raster.disc(canvas, 16, 7.8, 4.4, RAMPS["steel"], ambient=0.32)
        raster.rect(canvas, 12, 5, 8, 1, RAMPS["gold"][1])
        face(canvas, 9.6, 2.8)
        raster.polygon(canvas, ((14.5, 5.0), (16.0, 0.5), (18.5, 5.0)), colours,
                       0.90, dither=False)


class Infiltrator(Archetype):
    """Fast, acts after striking: crouched under a camouflage shroud.

    The rogue's crouch is kept, being the only pose in the roster below the
    others' shoulder line, and the daggers become blades that are light rather
    than metal, which is the one place the style spends bright colour on a
    silhouette's extremities instead of its centre.
    """

    name = "rogue"
    label = "Infiltrator"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, RAMPS["ink"], top=21, bottom=27, spread=4, radius=2.1)
        cloak(canvas, RAMPS["ink"], top=14.0, bottom=25.0, width=6.0)
        torso(canvas, colours, top=15.0, bottom=21.0, radius=4.0)
        raster.rect(canvas, 12, 18, 9, 2, RAMPS["ink"][1])  # harness
        raster.rect(canvas, 12, 18, 9, 1, RAMPS["steel"][0])
        arm(canvas, RAMPS["ink"], 12.0, 16.5, 8.5, 19.0, 1.7)
        arm(canvas, RAMPS["ink"], 20.0, 16.5, 23.5, 14.0, 1.7)
        hand(canvas, 8.2, 19.4)
        hand(canvas, 23.8, 13.6)
        # Two energy blades, held low and high, drawn as light.
        for x0, y0, x1, y1 in ((7.6, 20.2, 4.0, 24.0), (24.4, 12.8, 28.0, 9.0)):
            raster.capsule(canvas, x0, y0, x1, y1, 1.1, ENERGY, ambient=0.80)
            raster.line(canvas, int(x0), int(y0), int(x1), int(y1), CERAMIC[2])
        head(canvas, cy=11.0, radius=3.8)
        # Sealed hood: no face at all, the darkest head in either style.
        raster.disc(canvas, 16, 10.0, 4.3, RAMPS["ink"], ambient=0.42)
        raster.polygon(canvas, ((20.0, 7.0), (24.0, 12.0), (19.0, 11.0)),
                       RAMPS["ink"], 0.35)
        raster.rect(canvas, 13, 12, 7, 2, colours[2])  # faction throat plate
        raster.rect(canvas, 13, 12, 7, 1, colours[3])
        eyes(canvas, cy=10, spread=2, colour=ENERGY[4])


class Xenoform(Archetype):
    """Non-humanoid: a six-legged carapace, the only horizontal body.

    Keeps the beast's whole structural argument: a long low body, a faction
    ridge along the back, a second faction marker at head height. That is what
    makes the archetype legible as "not a person" at any size. What changes is
    the count of legs and the shape of the head.
    """

    name = "beast"
    label = "Xenoform"

    #: Chitin. As with the beast's hide, the ramp needs a lit top step.
    CHITIN = RAMPS["hide"] + (RAMPS["ink"][2],)

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        chitin = self.CHITIN
        for index in range(6):  # six legs, jointed outward
            x = 7.5 + index * 3.2
            raster.capsule(canvas, x, 19.5, x - 1.2, 23.0, 1.2, chitin, ambient=0.28)
            raster.capsule(canvas, x - 1.2, 23.0, x + 0.4, 26.8, 1.1, chitin,
                           ambient=0.34)
        raster.capsule(canvas, 10.0, 18.5, 21.0, 18.0, 4.2, chitin, ambient=0.34)
        # Segmented tail, curled up over the back and unbroken to the stinger.
        for x0, y0, x1, y1, radius in ((10.5, 18.0, 6.5, 15.5, 1.7),
                                       (6.5, 15.5, 4.0, 11.5, 1.4),
                                       (4.0, 11.5, 5.5, 8.0, 1.2)):
            raster.capsule(canvas, x0, y0, x1, y1, radius, chitin, ambient=0.40)
        raster.polygon(canvas, ((5.5, 8.5), (7.6, 5.4), (4.2, 5.8)), ENERGY, 0.95,
                       dither=False)
        for index in range(5):  # carapace plates along the back
            raster.polygon(
                canvas,
                ((11.2 + index * 2.3, 15.4), (12.9 + index * 2.3, 10.6),
                 (14.6 + index * 2.3, 15.4)),
                colours, 0.55 + 0.06 * (index % 2),
            )
        raster.disc(canvas, 23.5, 15.0, 4.2, chitin, ambient=0.42)
        for side in (-1, 1):  # mandibles, not a muzzle
            raster.capsule(canvas, 25.0, 15.0 + side * 1.8, 30.0,
                           15.0 + side * 3.2, 1.1, chitin, ambient=0.36)
        for index, offset in enumerate(((0, 0), (2, 1), (1, 2))):  # eye cluster
            canvas.put(24 + offset[0], 13 + offset[1], ENERGY[4 - index % 2])
        raster.capsule(canvas, 20.0, 12.5, 20.5, 17.5, 1.4, colours, ambient=0.55)


#: One routine per name in ``characters.ARCHETYPE_ORDER``, in that order. The
#: registry in :mod:`.styles` asserts the set matches; the order here is for
#: readers, not for indexing.
ARCHETYPE_CLASSES = (
    Trooper, Sniper, Psion, DroneSwarm, Medic, Captain, Infiltrator, Xenoform,
)

ARCHETYPES = {cls.name: cls() for cls in ARCHETYPE_CLASSES}
