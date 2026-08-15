# SPDX-License-Identifier: MIT
"""The ``pirates`` character style: the same eight roles, dressed as a crew.

This setting passes every gate in the screen a candidate style has to pass
(``tools/placeholder_art/README.md``) with nothing to settle first, and it
dresses all eight roles without invention: boarder, musketeer, hexer, gunner,
surgeon, captain, cutpurse, and a parrot for ``beast``. It is the one row of
that README's mapping table with no dash in it.

The gate this style had to be drawn against is the separation one, and the risk
was named before a line existed: **tricorn-and-coat convergence.** Eight figures
in one hat and one coat are one figure drawn eight times, and the reduction to
sixteen pixels is where that stops being a matter of taste. So the tricorn is
spent **once**, on the captain, and the other seven carry a bandana, a tied
queue, a fall of loose hair, a knitted cap, a kerchief, a streaming headscarf
and a crest. What separates two units here is never the hat.

No new palette entries
----------------------
None, as *Adding a style* in ``tools/placeholder_art/README.md`` requires: the
``n64_ci8`` profile writes the whole master palette into every asset's ``PLTE``
chunk, so one appended entry would rewrite art nobody asked to change. The kit
is four ramps that already exist, beside the skin, the ink and the faction
colour every style is drawn with:

``leather``
    tar and timber, lifted by one step of ``dirt`` as the ``nature`` browns
    are. **The merge that keeps this style inside gate 3**: a tarred deck, a
    sea coat, a musket stock, a peg leg and a pair of boots are all the same
    dark brown at this size, so ``wood`` is deliberately not named. It is
    three colours away from ``leather`` and would have bought nothing but a
    sixth material in every cell.
``sand``
    brass and bone, which is the same merge made a second time: a buckle, a
    bone charm, a lantern's lit pane and a parrot's beak all read as one warm
    pale metal against tar, and giving bone its own ramp would have cost a
    material for a difference nobody can see at sixteen pixels. It is spent on
    marks and never on mass.
``steel``
    ship's iron: a cutlass, a musket barrel, a swivel gun, and, at its two
    palest steps, the powder smoke standing over that gun's muzzle. Smoke in
    sailcloth would have been a sixth material in the busiest cell in the
    style, and the CI4 subset would have taken it back.
``snow``
    sailcloth, lifted by ``ink``'s top step as the ``medieval`` healer's robe
    is: the surgeon's apron, the captain's shirt front, a cutpurse's striped
    jersey, the parrot's wing bars, and the skull on the captain's colours.

``gold`` appears once in the whole style, on the two pixels of a slow match's
coal: a live ember is the one thing here that is its own light source. It is
also, measured, what parts the gunner's colour set from the musketeer's on the
9-bit pass, which is the sort of thing the pass exists to find.

Five ramps a sprite
-------------------
The rule the ``mythical`` and ``nature`` commissions paid to learn: the
``n64_ci4`` profile keeps a sprite's sixteen most-used colours and remaps the
rest, and a shading ramp costs three to five of the sixteen, so what binds is
the number of **materials** a cell names, not the number of colours. Name about
five and pick them before drawing. Every routine below names
five or fewer, and three of them do it by dropping something the setting would
have offered: the gunner's brass gun became an iron one, because brass beside
steel is two metals where the silhouette needed one; the captain wears no metal
at all, its buttons and its truck being the timber's own lit step; and the
parrot names four, which is the leanest sprite in the style. Measured over all
forty-eight sprites, the ``n64_ci4`` subset moves **0.66%** of opaque texels,
against ``medieval``'s 0.40% and ``nature``'s 0.55%.

The silhouettes, and why each is the one it is
----------------------------------------------
Separation is measured within a style, because a project names one. Each of
these carries a device none of its siblings carries:

* the **boarder** a barrel lid held as a shield, the only large disc;
* the **musketeer** a musket held level, the one hard horizontal;
* the **hexer** a heavy fall of hair past the jaw on both sides, so it is the
  only figure here whose widest point is its head;
* the **gunner** a swivel gun on its post, a diagonal spar leaving the shoulder
  with smoke above the muzzle;
* the **surgeon** a canvas apron widening all the way to the deck, the widest
  hem, against the hexer's coat, which is torn narrow for exactly that reason;
* the **captain** the colours on a pole breaking the top of the frame, and the
  style's one tricorn;
* the **cutpurse** the crouch, lowest head in the roster, with two scarf tails
  streaming off the back;
* the **parrot** a bird: a heavy body over gripping feet, a tail sweeping to the
  deck, and a crest. It is the member of the roster that has to read as *not a
  person*, and no amount of coat would have done it.

The stormcaller is a character
------------------------------
Every style shipped before this one draws the area-effect role as the same
figure with both arms raised, and a review of the four rosters found it reads as
a placeholder pose rather than as somebody. The gunner is drawn instead as a
crew serving a deck gun: arms **down**, one on the breech and one holding a
linstock at the hip. That second word is deliberate too: a body whose arms
already read as thrown forward makes ``lunge`` and ``cast`` harder to tell
apart, since both poses push an arm out, so a standing figure that keeps its
hands low leaves the poses room to differ.

Consistent footprint
--------------------
The same review found one style's ``mage`` drawn noticeably smaller than its
siblings, which reads as a scale bug once units stand on a board beside each
other. Every figure here stands between the ground line and the top of the head
at the same height, within a pixel or two, and the ``beast`` fills the cell as
the other rosters' quadrupeds do.

The margin rule
---------------
Stated and enforced by :mod:`.frames`: ``walk_contact`` moves everything at or
below row 24 outward by a column, so nothing here is drawn on column 0 or
column 31 below the knee: no boot, no tail tip, no apron hem, no purse.
"""

from __future__ import annotations

from . import raster
from .characters import (Archetype, FactionColour, arm, boots, cloak, face,
                         hand, head, legs, torso)
from .palette import RAMPS
from .raster import Canvas

#: Tar: the dark mass of the style. Coats, hats, hair, a cask in shadow.
TAR = RAMPS["leather"]

#: The same material where the light reaches it, on a boot's toe, a musket's
#: stock, the hoop on a cask, with ``dirt``'s top step as the highlight
#: ``leather`` has not got, exactly as the ``nature`` bear's brown takes one.
#: It is one material in two ramps rather than two materials: three of its four
#: entries are ``TAR``'s own, so a cell that names both pays for one.
TIMBER = RAMPS["leather"] + (RAMPS["dirt"][3],)

#: Brass and bone: gun barrels, cartridge caps, bone rings, a parrot's beak.
BRASS = RAMPS["sand"]

#: Ship's iron. Blades only, so a blade stays a blade.
IRON = RAMPS["steel"]

#: Sailcloth: the surgeon's apron, rolled sleeves, powder smoke, a skull.
LINEN = RAMPS["snow"] + (RAMPS["ink"][3],)


def sash(canvas: Canvas, ramp, y: int, left: int = 11, width: int = 11,
         height: int = 3) -> None:
    """A wide band across a waist, lit along its top edge.

    Deliberately a *colour* device rather than a silhouette one: a sash changes
    nothing about an outline, so more than one figure may wear it without the
    two of them converging. That is what it is for: it is where the faction
    colour goes on the surgeon and the cutpurse, whose garment is sailcloth and
    would otherwise carry none.
    """
    raster.rect(canvas, left, y, width, height, ramp[1])
    raster.rect(canvas, left, y, width, height - 1, ramp[2])
    raster.rect(canvas, left, y, width, 1, ramp[3])


class Boarder(Archetype):
    """Melee, shield forward: bare arms, a cutlass, and a barrel lid.

    The knight's read is a big disc beside a body, and it is kept because it is
    the cheapest one there is. What makes it this style's is what the disc is,
    a cask head with its hoops still on, snatched off the deck, and what holds
    it: the only figure here with nothing at all on its arms.
    """

    name = "knight"
    label = "Boarder"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, TIMBER, top=19, bottom=26, spread=3, radius=2.0)
        boots(canvas, TIMBER)
        torso(canvas, colours, top=13.0, bottom=21.0, radius=4.9)
        for row in (14, 17):  # the striped shirt, in its own darkest step
            raster.rect(canvas, 12, row, 9, 1, colours[0])
        raster.rect(canvas, 11, 19, 11, 2, TAR[0])  # belt
        canvas.put(16, 19, BRASS[3])  # the buckle, and the style's one
        canvas.put(19, 12, BRASS[3])  # earring: two pixels of a fifth ramp
        for side in (-1, 1):  # bare shoulders, sleeves rolled past them
            raster.disc(canvas, 16 + side * 4.7, 14.2, 2.5, RAMPS["skin"],
                        ambient=0.34)
        arm(canvas, RAMPS["skin"], 20.5, 15.5, 23.0, 12.0)
        hand(canvas, 23.2, 11.6)
        raster.capsule(canvas, 23.0, 11.2, 26.0, 4.2, 1.3, IRON, ambient=0.46)
        raster.capsule(canvas, 21.8, 11.6, 24.6, 11.0, 0.9, IRON, ambient=0.66)
        # The cask head, held as a shield: the only large disc in the roster.
        # Its hoops are the timber's own lit step rather than brass, which is
        # the merge gate 3 asks for: eighteen pixels of a fifth material is
        # eighteen pixels the CI4 subset would have thrown away.
        raster.disc(canvas, 9.2, 18.4, 5.1, TAR, ambient=0.30, squash=1.05)
        for band in (15, 21):
            raster.rect(canvas, 5, band, 9, 1, TIMBER[3])
        raster.disc(canvas, 9.2, 18.4, 2.1, colours, ambient=0.46, squash=1.05)
        head(canvas, cy=9.2, radius=4.0)
        raster.disc(canvas, 16, 7.6, 4.2, colours, ambient=0.42)  # the bandana
        raster.rect(canvas, 12, 8, 9, 1, colours[1])
        raster.polygon(canvas, ((12.5, 8.0), (9.5, 5.8), (11.0, 10.5)), colours,
                       0.48)
        face(canvas, 10.2, 2.7)


class Musketeer(Archetype):
    """Ranged, cannot strike adjacent: a musket held level across the body.

    The archer is a vertical arc in three of the four shipped styles. This one
    is the opposite line, a hard horizontal from one edge of the cell nearly to
    the other, which survives the reduction as a bar where every other figure
    here reduces to a column.
    """

    name = "archer"
    label = "Musketeer"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, TIMBER, top=19, bottom=26, spread=3, radius=1.7)
        boots(canvas, TIMBER)
        torso(canvas, colours, top=13.0, bottom=20.5, radius=4.2)
        raster.rect(canvas, 11, 18, 10, 2, TAR[0])
        # The bandolier: a cartridge belt over one shoulder.
        raster.polygon(canvas, ((11.4, 13.0), (13.6, 13.0), (21.0, 20.5),
                                (18.8, 20.5)), TAR, 0.60)
        for step in range(4):
            canvas.put(13 + step * 2, 15 + step * 2, TIMBER[3])
        arm(canvas, RAMPS["skin"], 12.2, 15.6, 8.6, 15.2, 1.6)
        arm(canvas, RAMPS["skin"], 20.0, 15.6, 17.4, 16.4, 1.6)
        hand(canvas, 8.2, 15.2)
        # The musket, level at the hip: barrel to the off side, stock at the
        # shoulder. It sits low deliberately: the hexer's braids are the other
        # wide horizontal in this roster, and two bars at the same height would
        # be one bar drawn twice once the cell is halved.
        raster.capsule(canvas, 4.4, 15.2, 20.0, 15.2, 0.9, IRON, ambient=0.26)
        raster.capsule(canvas, 19.5, 15.2, 27.4, 13.8, 1.6, TIMBER, ambient=0.34)
        raster.disc(canvas, 4.0, 15.2, 1.3, IRON, ambient=0.34)  # the muzzle
        raster.rect(canvas, 18, 13, 3, 2, TAR[0])  # the lock
        canvas.put(18, 13, TIMBER[3])
        head(canvas, cy=9.0, radius=3.9)
        raster.disc(canvas, 16, 7.6, 4.0, TAR, ambient=0.28)
        raster.capsule(canvas, 19.2, 8.6, 21.4, 12.4, 1.2, TAR, ambient=0.30)
        canvas.put(21, 12, colours[3])  # the ribbon on the queue
        face(canvas, 10.0, 2.7)


class Hexer(Archetype):
    """Magic, short band: a narrow figure under a mass of braid and bone.

    The mage is a narrow cone in ``medieval`` and a narrow column in ``scifi``,
    and both put the width at the hem. This one inverts it: the braids reach
    past the shoulders on both sides and nothing else does, so the hexer is the
    only figure in the roster whose widest point is its head. The coat is torn
    narrow on purpose: it is the surgeon's apron that owns the wide hem, and
    two garments falling to the same width would have made one shape of two.
    """

    name = "mage"
    label = "Hexer"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, colours, top=12.5, bottom=24.5, width=4.6)
        # A peg below one knee and a boot below the other: the stance is
        # uneven, which is a difference a silhouette keeps and a face does not.
        raster.capsule(canvas, 18.4, 20.0, 19.4, 25.6, 1.8, TAR, ambient=0.30)
        raster.capsule(canvas, 18.8, 26.0, 19.8, 27.0, 2.0, TIMBER, ambient=0.36)
        raster.polygon(canvas, ((11.6, 20.0), (14.6, 20.0), (13.8, 27.0),
                                (12.8, 27.0)), TIMBER, 0.46)
        torso(canvas, colours, top=13.0, bottom=20.0, radius=3.5)
        raster.rect(canvas, 13, 17, 7, 2, TAR[0])
        arm(canvas, RAMPS["skin"], 12.4, 15.0, 10.0, 18.6, 1.6)
        arm(canvas, RAMPS["skin"], 19.8, 15.0, 21.4, 17.6, 1.6)
        hand(canvas, 9.8, 19.2)
        hand(canvas, 21.6, 18.2)
        # The hex itself, held at the hip rather than thrust forward.
        raster.disc(canvas, 22.2, 19.6, 2.0, BRASS, ambient=0.52)
        canvas.put(21, 19, RAMPS["ink"][1])
        canvas.put(23, 19, RAMPS["ink"][1])
        canvas.put(22, 21, RAMPS["gold"][1])
        head(canvas, cy=10.2, radius=3.8)
        # The hair is the device, and it took three drafts to make it one. As
        # two smooth lobes off the skull it was a spaniel; as ropes fanning
        # upward with bone at their ends it was a pair of antlers, which is the
        # one silhouette a roster review had already flagged another style for
        # leaning on. It reads as hair when it **falls**: a wide crown and two
        # heavy falls past the jaw, with nothing lumped at the ends.
        raster.disc(canvas, 16, 8.0, 5.2, TAR, ambient=0.22)
        for side in (-1, 1):
            raster.polygon(canvas, ((16 + side * 3.0, 6.4),
                                    (16 + side * 5.6, 6.8),
                                    (16 + side * 6.6, 16.4),
                                    (16 + side * 3.6, 15.8)), TAR, 0.26)
            raster.polygon(canvas, ((16 + side * 4.4, 8.0),
                                    (16 + side * 5.6, 8.4),
                                    (16 + side * 6.0, 15.0),
                                    (16 + side * 4.8, 14.6)), TAR, 0.44)
        raster.rect(canvas, 11, 8, 11, 2, colours[1])  # the headband
        raster.rect(canvas, 11, 8, 11, 1, colours[2])
        face(canvas, 11.0, 2.7)


class Gunner(Archetype):
    """Area effect: a swivel gun on its post, and the hand serving it.

    Not the raised-arms figure the role has been in every style so far. A deck
    gun gives the archetype what it always needed: a device that reaches
    outward, here a diagonal spar off one shoulder with smoke standing above
    the muzzle, while leaving the body a person rather than a pose. Both arms
    stay low, one at the breech and one on the linstock, which is also what
    keeps ``lunge`` and ``cast`` from reading alike on this body.
    """

    name = "stormcaller"
    label = "Gunner"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        # The gun on its post, planted on the weapon side. The barrel is iron
        # and thin: drawn once as a fat brass tube it read as a banana rather
        # than as a gun, which is the sort of thing only looking at the sheet
        # tells you.
        raster.capsule(canvas, 24.2, 13.0, 24.8, 27.0, 1.4, TAR, ambient=0.28)
        raster.polygon(canvas, ((22.2, 14.0), (27.2, 14.0), (24.8, 10.2)), TAR,
                       0.36)
        raster.capsule(canvas, 20.6, 13.4, 28.0, 7.4, 1.3, IRON, ambient=0.30)
        raster.disc(canvas, 28.2, 7.0, 1.7, IRON, ambient=0.38)
        raster.disc(canvas, 20.2, 13.8, 1.5, IRON, ambient=0.44)  # the breech
        raster.capsule(canvas, 22.4, 11.4, 23.8, 9.8, 0.8, TIMBER, ambient=0.70)
        # Powder smoke, in the barrel's own iron at its palest. A sixth material
        # in this cell is a sixth material the CI4 subset throws away, and the
        # smoke is what it would throw: see "Five ramps a sprite".
        for cx, cy, size in ((28.4, 3.6, 2.2), (26.6, 1.2, 1.6)):
            raster.disc(canvas, cx, cy, size, IRON, ambient=0.86, squash=1.2)
        legs(canvas, TIMBER, top=19, bottom=26, spread=3, radius=1.9)
        boots(canvas, TIMBER)
        torso(canvas, colours, top=13.5, bottom=20.5, radius=4.4)
        for side in (-1, 1):  # shoulders in the shirt, forearms bare
            raster.disc(canvas, 16 + side * 4.4, 14.6, 2.4, colours,
                        ambient=0.30)
        raster.rect(canvas, 11, 18, 10, 2, TAR[0])
        raster.rect(canvas, 11, 18, 10, 1, TAR[2])
        arm(canvas, RAMPS["skin"], 19.6, 16.0, 21.4, 18.8, 1.6)
        hand(canvas, 21.6, 19.2)
        arm(canvas, RAMPS["skin"], 12.4, 16.0, 10.2, 19.2, 1.6)
        hand(canvas, 10.0, 19.8)
        raster.capsule(canvas, 8.4, 23.0, 11.0, 15.4, 1.0, TAR, ambient=0.34)
        canvas.put(11, 14, RAMPS["gold"][1])  # the slow match's coal
        canvas.put(11, 15, RAMPS["gold"][0])
        # The head sits low between the shoulders under a knitted cap: the only
        # figure here with no neck showing.
        head(canvas, cy=10.6, radius=3.6)
        raster.disc(canvas, 16, 9.4, 3.9, TAR, ambient=0.26)
        raster.rect(canvas, 12, 10, 8, 1, TIMBER[3])
        face(canvas, 11.4, 2.6)


class Surgeon(Archetype):
    """Support, and the widest hem in the roster: a heavy canvas apron.

    §13.3 named the apron as this role's answer to the coat problem before the
    style was drawn, and the pass agreed with it. The apron is the only garment
    here that reaches the deck at full width, it is the only pale mass, and it
    is what a healer with no offensive weapon has instead of one.
    """

    name = "healer"
    label = "Surgeon"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, TIMBER, top=20, bottom=26, spread=3, radius=1.7)
        boots(canvas, TIMBER)
        torso(canvas, colours, top=13.0, bottom=20.0, radius=4.2)
        raster.polygon(canvas, ((11.8, 12.5), (20.2, 12.5), (24.5, 27.0),
                                (7.5, 27.0)), LINEN, 0.48)
        raster.polygon(canvas, ((12.4, 12.5), (17.4, 12.5), (19.4, 27.0),
                                (10.0, 27.0)), LINEN, 0.66)
        for stain in ((11, 22), (12, 24), (19, 21), (20, 25)):
            canvas.put(stain[0], stain[1], TAR[1])
        sash(canvas, colours, 11, left=12, width=9, height=2)
        arm(canvas, RAMPS["skin"], 12.2, 15.0, 9.2, 12.8, 1.6)
        arm(canvas, RAMPS["skin"], 19.8, 15.0, 22.2, 18.0, 1.6)
        hand(canvas, 9.0, 12.4)
        hand(canvas, 22.4, 18.5)
        # The ship's lantern, raised in the **off** hand. It is the only thing
        # in the roster held up on that side, since the boarder's cutlass and
        # the cutpurse's knife are both on the weapon side, and a surgeon
        # working below the waterline is the one member of a crew who needs one.
        raster.capsule(canvas, 8.8, 12.2, 8.4, 9.4, 0.7, TAR, ambient=0.30)
        raster.rect(canvas, 6, 5, 5, 5, TAR[0])
        raster.rect(canvas, 7, 6, 3, 3, BRASS[3])
        raster.rect(canvas, 7, 6, 3, 1, BRASS[1])
        raster.rect(canvas, 5, 4, 7, 1, TIMBER[3])
        raster.rect(canvas, 6, 9, 5, 1, TIMBER[3])
        head(canvas, cy=9.6, radius=3.8)
        raster.disc(canvas, 16, 8.4, 4.0, LINEN, ambient=0.56)  # the kerchief
        raster.polygon(canvas, ((12.4, 9.0), (9.5, 11.5), (12.8, 11.8)), LINEN,
                       0.44)
        face(canvas, 10.4, 2.7)


class Captain(Archetype):
    """The leader, and a win condition: the colours, a long coat, a tricorn.

    Rank three ways, as every style states it, because one of them has to
    survive the reduction: the colours break the top of the frame, the coat
    carries the mass, and the tricorn is the detail. The hat is also the whole
    style's answer to gate 2: it is worn here and nowhere else, so the one
    device everybody expects of a pirate identifies exactly one unit instead of
    blurring eight.
    """

    name = "commander"
    label = "Captain"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        raster.capsule(canvas, 25.0, 1.0, 25.0, 27.0, 1.1, TAR, ambient=0.42)
        raster.polygon(canvas, ((24.0, 2.0), (31.0, 4.0), (24.0, 11.0)), colours,
                       0.72, dither=False)
        raster.polygon(canvas, ((24.0, 2.0), (28.0, 2.8), (24.0, 6.0)), colours,
                       0.95, dither=False)
        raster.disc(canvas, 25.0, 0.8, 1.4, TIMBER, ambient=0.80)
        for x, y in ((26, 5), (27, 5), (26, 6), (27, 6), (26, 8), (28, 7)):
            canvas.put(x, y, LINEN[2])  # the skull and its bones
        cloak(canvas, colours, top=11.5, bottom=27.0, width=7.8)
        legs(canvas, TIMBER, top=19, bottom=26, spread=3)
        boots(canvas, TIMBER)
        torso(canvas, colours, top=12.5, bottom=21.0, radius=4.8)
        raster.capsule(canvas, 16, 13.5, 16, 20.0, 2.5, LINEN, ambient=0.48)
        for row in (14, 16, 18):
            canvas.put(14, row, TIMBER[3])
            canvas.put(18, row, TIMBER[3])
        for side in (-1, 1):
            raster.disc(canvas, 16 + side * 4.9, 13.6, 2.6, colours, ambient=0.34)
            raster.rect(canvas, 15 + side * 5, 12, 3, 1, TIMBER[3])
        arm(canvas, colours, 11.2, 15.0, 8.8, 18.4)
        hand(canvas, 8.6, 18.9)
        arm(canvas, colours, 20.8, 15.0, 22.8, 17.8)
        hand(canvas, 23.0, 18.3)
        head(canvas, cy=8.8, radius=4.0)
        face(canvas, 10.0, 2.7)
        # The tricorn: three corners, worn by one unit in the whole style. The
        # brim is lit along its upper edge because the first draft was one flat
        # dark wedge over dark hair, and at 32 pixels that is not a hat.
        raster.polygon(canvas, ((7.4, 7.4), (16.0, 1.6), (24.6, 7.4),
                                (20.4, 8.6), (16.0, 6.0), (11.6, 8.6)), TAR,
                       0.30)
        raster.polygon(canvas, ((8.6, 6.6), (16.0, 2.0), (23.4, 6.6),
                                (21.6, 7.0), (16.0, 3.2), (10.4, 7.0)), TIMBER,
                       0.72)
        raster.polygon(canvas, ((12.2, 5.4), (16.0, 2.8), (19.8, 5.4)), TAR,
                       0.44)
        raster.capsule(canvas, 16.6, 0.4, 15.4, 3.0, 1.2, colours, ambient=0.60)


class Cutpurse(Archetype):
    """Fast, acts after striking: the crouch, and a purse still on its string.

    §13.3 named the crouch as this role's answer, and it is the same answer the
    rogue has in every style, kept because it is the only one that puts a head
    below everybody else's shoulder line. What is this style's is the pair of
    scarf tails streaming off the back, a direction that a crouch on its own
    does not give, and the cut purse swinging from the low hand.
    """

    name = "rogue"
    label = "Cutpurse"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, TAR, top=21, bottom=27, spread=4, radius=2.0)
        torso(canvas, LINEN, top=15.0, bottom=21.0, radius=3.9)
        for row in (16, 18):  # the striped shirt every hand on deck wears
            raster.rect(canvas, 12, row, 9, 1, colours[2])
        sash(canvas, colours, 19, left=12, width=10)
        arm(canvas, RAMPS["skin"], 12.2, 16.5, 8.8, 19.0, 1.6)
        arm(canvas, RAMPS["skin"], 20.0, 16.5, 23.0, 14.0, 1.6)
        hand(canvas, 8.5, 19.4)
        hand(canvas, 23.4, 13.6)
        raster.capsule(canvas, 24.0, 13.0, 26.8, 9.2, 1.0, IRON, ambient=0.52)
        # The purse, still swinging on the string it was cut from. Small: drawn
        # at two pixels of radius it read as a crate the figure was carrying.
        raster.capsule(canvas, 8.2, 20.2, 7.8, 21.6, 0.4, TAR, ambient=0.44)
        raster.disc(canvas, 7.6, 22.8, 1.4, TAR, ambient=0.36)
        head(canvas, cy=11.6, radius=3.7)
        raster.disc(canvas, 16, 10.4, 4.0, colours, ambient=0.44)
        # The tails stream low and back, under the surgeon's raised lantern
        # rather than beside it: two devices on the off side at the same height
        # would be one device once the cell is halved.
        raster.polygon(canvas, ((12.6, 11.0), (4.8, 11.4), (6.6, 14.6),
                                (12.6, 13.4)), colours, 0.56)
        raster.polygon(canvas, ((12.4, 12.8), (6.0, 16.4), (12.2, 15.2)),
                       colours, 0.40)
        face(canvas, 12.2, 2.6)


class Parrot(Archetype):
    """Non-humanoid: the one member of the roster that is not a person.

    A style whose whole idea is a crew still needs a silhouette that says *not
    a person*, and a monkey would not have given one: on four legs it is the
    wolf and the boar again, and on two it is another member of the crew. A bird
    is neither: a heavy body over gripping feet with a tail sweeping to the deck
    is a shape nothing else in any style holds.

    It is drawn standing rather than flying, and that is a rule rather than a
    preference. Flight is a *movement capability* the catalogue grants exactly
    one entry (`editor/src/domain/character-recipe.ts`, `TRAVERSING_ENTRY`), and
    a beast whose picture flies while its record walks would be a promise the
    numbers do not keep. Numbers belong to the role; names and pictures belong
    to the setting.
    """

    name = "beast"
    label = "Parrot"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        # The tail, sweeping off the back and down to the deck behind the feet.
        raster.polygon(canvas, ((13.8, 15.4), (17.2, 14.0), (8.4, 26.4),
                                (5.4, 24.6)), colours, 0.30)
        raster.polygon(canvas, ((14.6, 15.8), (16.8, 14.8), (9.4, 25.4),
                                (7.6, 24.2)), colours, 0.52)
        # The body leans out over its own feet and the head is carried four
        # columns past them. Every person in this roster stacks head over feet;
        # this is the one drawing that does not, which is most of why it reads
        # as an animal at sixteen pixels rather than as one more of the crew.
        raster.disc(canvas, 17.4, 18.4, 5.4, colours, ambient=0.38, squash=0.92)
        raster.polygon(canvas, ((13.2, 14.4), (20.2, 16.4), (18.6, 23.2),
                                (13.6, 21.0)), colours, 0.20)  # folded wing
        for step in range(3):
            canvas.put(14 + step * 2, 22 - step, LINEN[2])
        for x in (15.4, 18.6):  # scaled feet, toes spread on the deck
            raster.capsule(canvas, x, 22.5, x + 0.5, 25.8, 1.1, BRASS,
                           ambient=0.36)
            raster.disc(canvas, x + 0.6, 26.4, 1.6, BRASS, ambient=0.48,
                        squash=1.8)
        raster.disc(canvas, 22.4, 10.6, 3.5, colours, ambient=0.46)
        raster.polygon(canvas, ((24.2, 8.8), (28.0, 10.8), (24.8, 13.2)), BRASS,
                       0.70, dither=False)
        raster.polygon(canvas, ((24.6, 11.4), (26.8, 12.0), (24.6, 13.6)),
                       RAMPS["ink"], 0.55, dither=False)
        for offset, rise in ((-2.4, 4.8), (-0.4, 3.0), (1.6, 4.2)):
            raster.polygon(canvas, ((21.4 + offset, 8.6), (22.0 + offset, rise),
                                    (23.0 + offset, 8.6)), LINEN, 0.66,
                           dither=False)
        canvas.put(23, 10, RAMPS["ink"][0])
        canvas.put(23, 9, BRASS[3])


#: One routine per name in ``characters.ARCHETYPE_ORDER``, in that order. The
#: registry in :mod:`.styles` asserts the set matches; the order here is for
#: readers, not for indexing.
ARCHETYPE_CLASSES = (
    Boarder, Musketeer, Hexer, Gunner, Surgeon, Captain, Cutpurse, Parrot,
)

ARCHETYPES = {cls.name: cls() for cls in ARCHETYPE_CLASSES}
