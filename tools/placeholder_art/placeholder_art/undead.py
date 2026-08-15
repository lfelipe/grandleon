# SPDX-License-Identifier: MIT
"""The ``undead`` character style: the same eight roles, raised rather than hired.

Two claims make this setting worth drawing and this module is what carries
them. On silhouette separation it beats ``nature``'s floor of 28 pixels of 256,
because bone, cloth and beast are three different body plans and a wraith with
no legs is a silhouette device nothing in any other style has. On materials it
is the leanest style in the library: bone, dark cloth, rust, a cold green, and
**no skin ramp at all**.

What makes it more than a fifth dress is that it is the first style drawn as **an
opposing force**. Every style before it dresses both sides of a battle
identically and a faction is a colour swap, so an author who wanted their enemies
to look like enemies had no answer. That reading lives entirely in the pixels:
these are the same eight tactical roles, offered in all six faction colours, and
nothing here is a side. A player must still be able to find the healer.

Three body plans, which is the separation argument
--------------------------------------------------
The prediction above is only true if the roster uses all three plans rather than
dressing eight skeletons:

**bone**, on knight, archer, stormcaller, commander and rogue: articulated,
hard-edged, a pale ramp over a dark one.

**cloth**, on mage and healer, and they are deliberately inverted against each
other. The wraith is a downward kite, widest at the shoulders and tapering to a
point that never reaches the ground; the mourner is an upward trapezoid,
narrowest at the crown and widest where it pools on the floor. At three shades
over sixteen pixels those are two different triangles.

**beast**, the hound, on four legs, with a horizontal spine.

The skull budget
----------------
The skull is this style's obvious device and would happily land on all eight,
which is the fault ``mythical``'s horns fell into. So it is **detail, never
silhouette**, and it is rationed: bare on three (the archer's thrust forward, the
commander's crowned, the hound's long and animal), implied as a dark hollow on
the wraith, and hidden on the other four under a helm, a knotted hood, a hood
pulled forward and a veil.

What carries the style instead is the **ribcage**: short pale bars across a dark
torso, the counterpart of ``scifi``'s visor and ``nature``'s snout. It appears on
four of the eight, not on all of them, because the two cloth bodies have no torso
to show and the knight's is under plate.

No new palette entries
----------------------
None, for the reason *Adding a style* in ``tools/placeholder_art/README.md``
gives: the ``n64_ci8`` profile writes the whole master palette into every
asset's ``PLTE`` chunk. Four materials, which is the shortest list any style has
brought:

``BONE``
    ``ink``'s third step under ``snow``. Every skeleton, the yoke, the banner
    pole and the bow come off it.
``SHROUD``
    ``hide`` with a lit top step: grave cloth, hoods, cowls, hanging tatters.
``RUST``
    ``autumn_leaf``. Metal, and **only** metal: plate, blades, bells, a broken
    crown. It is a terrain ramp, so it takes ``nature``'s ``foliage``
    discipline: marks at the ends of things and never a body, because under the
    autumn theme it is the canopy and a unit that reads as ground is not a unit.
``WISP``
    two steps of ``ash_water``, the cold green §13.3 names. Eyes and small
    flames, so it stays a mark and never a mass.

``BONE`` is extended **downward** rather than upward, which is new here and worth
the sentence. ``snow`` is three light steps, and a body shaded across three light
steps has no form at all: bone needs a shadow before it needs a highlight. The
step it borrows is an index the palette already holds, so this appends nothing.

No skin
-------
There is no skin ramp in the style, which means :func:`~.characters.head`,
:func:`~.characters.hand` and :func:`~.characters.face` cannot be called: all
three are skin. The replacements are below, and that absence is most of what
makes the roster read as the dead rather than as people in costume.

Five ramps a sprite
-------------------
A sprite names about five **materials**, not five colours, because a shading
ramp costs three to five of its sixteen CI4 entries. This is the easiest
style in the library to hold to it: a sprite spends bone, cloth, the faction
ramp, ink, and rust if it carries metal. Five, or six on the four that do.

The margin rule
---------------
Stated and enforced by :mod:`.frames`: ``walk_contact`` moves everything at or
below row 24 outward by a column, so nothing here is drawn on column 0 or
column 31 below the knee: no heel, no shield rim, no bell, no tail. The bells
hang at rows 14 to 21, well above it.

Nothing floats but the drawing
------------------------------
The wraith has no legs and clears its own faction disc by three rows. It is still
a ``mage`` that walks: ``RoleDress.traversal`` in
``editor/src/domain/character-recipe.ts`` is a fence with exactly one occupant,
the Dragon, and a picture is a name rather than a number.
"""

from __future__ import annotations

from . import raster
from .characters import Archetype, FactionColour, arm, boots, cloak, legs, torso
from .palette import RAMPS
from .raster import Canvas

#: Bone. ``snow`` is three light steps and shades nothing on its own, so it is
#: given a slate shadow underneath rather than another highlight on top.
BONE = (RAMPS["ink"][2],) + RAMPS["snow"]

#: Grave cloth: shrouds, cowls, hoods and hanging tatters. ``hide`` alone is
#: three dark steps, so it takes a lit top exactly as the ``beast``'s does.
SHROUD = RAMPS["hide"] + (RAMPS["ink"][2],)

#: Rust. Spent on metal and on nothing else, because it is a terrain ramp.
RUST = RAMPS["autumn_leaf"]

#: The cold green: eyes and small flames. Two steps, so it stays a mark.
WISP = (RAMPS["ash_water"][2], RAMPS["ash_water"][4])


def skull(canvas: Canvas, cx: float, cy: float, radius: float = 3.6) -> None:
    """A bare skull: a pale dome, two sunk sockets, and a short jaw under it.

    The style's face, in place of :func:`~.characters.face`, which is skin. The
    sockets are the whole read at small sizes: a pale head with two dark holes
    in it is a skull, and a pale head without them is a hood.
    """
    raster.disc(canvas, cx, cy, radius, BONE, ambient=0.46)
    raster.disc(canvas, cx, cy + radius * 0.62, radius * 0.62, BONE, ambient=0.52,
                squash=1.5)
    for side in (-1, 1):
        canvas.put(int(cx + side * radius * 0.42), int(cy), RAMPS["ink"][1])
        canvas.put(int(cx + side * radius * 0.42), int(cy) + 1, RAMPS["ink"][0])


def cowl(canvas: Canvas, cx: float, cy: float, radius: float = 4.2) -> None:
    """A hood with nothing in it: a cloth dome, a dark hollow, two lights.

    The wraith's head and the stormcaller's, and the one place the style states
    a skull by leaving it out. A hollow reads at any size; a face does not.
    """
    raster.disc(canvas, cx, cy, radius, SHROUD, ambient=0.36)
    raster.disc(canvas, cx, cy + radius * 0.30, radius * 0.58, RAMPS["ink"],
                ambient=0.12)
    for side in (-1, 1):
        canvas.put(int(cx + side * radius * 0.30), int(cy + radius * 0.30), WISP[1])


def bone_hand(canvas: Canvas, x: float, y: float) -> None:
    """A fleshless hand. The humanoid roster's :func:`~.characters.hand` is skin."""
    raster.disc(canvas, x, y, 1.4, BONE, ambient=0.50)


def ribs(canvas: Canvas, cx: float, top: int, count: int = 3,
         half_width: int = 3) -> None:
    """Pale bars across a dark torso: the style's shared mark.

    It is on four of the eight rather than on all of them, which is the point:
    the two cloth bodies have no torso to show and the knight's is under plate.
    """
    for step in range(count):
        row = top + step * 2
        raster.rect(canvas, int(cx) - half_width, row, half_width * 2, 1, BONE[2])
        raster.rect(canvas, int(cx) - half_width + 1, row + 1, half_width * 2 - 2, 1,
                    BONE[1])


class BarrowKnight(Archetype):
    """Melee, shield forward: rusted plate over bone, behind a grave door.

    Every style's knight is a big shape beside a body, and the shape is what
    separates them: a round disc in ``medieval``, a slab in ``scifi``, a kite in
    ``mythical``, bark in ``nature``. This one is a **coffin lid**: a tall
    six-sided plank, longer than it is wide, which is the only shield in any
    roster taller than the figure carrying it. The helm is flat-topped rather
    than domed, so this is also the one head in the style that is a rectangle.
    """

    name = "knight"
    label = "Barrow knight"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, BONE, top=19, bottom=26, spread=3, radius=1.7)
        boots(canvas, RUST)
        torso(canvas, colours, top=13.0, bottom=21.0, radius=4.8)
        raster.capsule(canvas, 16, 14.0, 16, 19.0, 3.2, RUST, ambient=0.28)
        for side in (-1, 1):  # pitted pauldrons
            raster.disc(canvas, 16 + side * 4.6, 14.0, 2.6, RUST, ambient=0.30)
        arm(canvas, BONE, 20.0, 15.0, 22.5, 12.5, 1.5)
        bone_hand(canvas, 22.8, 12.2)
        raster.capsule(canvas, 22.2, 12.5, 26.5, 5.5, 1.2, RUST, ambient=0.44)
        raster.polygon(canvas, ((25.5, 6.5), (28.0, 2.0), (28.0, 7.5)), RUST, 0.82,
                       dither=False)
        # The grave door: a six-sided plank, rimmed and barred in rust.
        raster.polygon(canvas, ((6.5, 6.0), (10.5, 8.5), (10.5, 22.5), (6.5, 25.0),
                                (2.5, 22.5), (2.5, 8.5)), RUST, 0.42)
        raster.polygon(canvas, ((6.5, 7.2), (9.8, 9.4), (9.8, 21.8), (6.5, 24.0),
                                (3.2, 21.8), (3.2, 9.4)), colours, 0.58)
        for row in (12, 19):  # two rusted bands across the boards
            raster.rect(canvas, 3, row, 7, 1, RUST[4])
            raster.rect(canvas, 3, row + 1, 7, 1, RUST[1])
        # A flat-topped great helm: the one head in the style that is a rectangle.
        raster.polygon(canvas, ((12.5, 5.0), (19.5, 5.0), (20.0, 12.5), (12.0, 12.5)),
                       RUST, 0.52)
        raster.rect(canvas, 13, 8, 6, 2, RAMPS["ink"][0])
        for side in (-1, 1):
            canvas.put(16 + side * 2, 8, WISP[1])


class Bonepicker(Archetype):
    """Ranged, cannot strike adjacent: the thinnest figure, behind a bone bow.

    The archer's vertical arc is kept because a line is what survives every
    reduction, and what makes it this style's is what stands beside it: the
    narrowest body in the roster, a bare ribcage with the cell's own
    transparency showing between the bars, and a skull carried forward of the
    shoulders rather than above them.
    """

    name = "archer"
    label = "Bonepicker"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, BONE, top=19, bottom=26, spread=2, radius=1.4)
        cloak(canvas, colours, top=12.5, bottom=26.0, width=5.6)
        raster.capsule(canvas, 20.5, 11.0, 19.0, 18.0, 1.8, SHROUD,
                       ambient=0.28)  # quiver
        for offset in range(3):
            canvas.put(20 - offset, 8 + offset, BONE[3])
        raster.capsule(canvas, 16, 13.5, 16, 20.5, 2.2, BONE, ambient=0.34)  # spine
        ribs(canvas, 16, 14, count=3, half_width=3)
        arm(canvas, BONE, 12.8, 14.5, 10.0, 14.0, 1.3)
        arm(canvas, BONE, 19.2, 14.5, 15.8, 14.0, 1.3)
        bone_hand(canvas, 9.4, 14.0)
        # The bow, held far enough off the body that the gap between the two is
        # part of the silhouette. Without that gap this reduces to the mourner:
        # both are a narrow top over a spread hem, and a hole is what a
        # three-shade reduction keeps when an outline does not.
        for step in range(23):
            bulge = 2.8 - abs(step - 11) * 0.25
            canvas.put(int(round(7.0 - bulge)), 3 + step, BONE[1])
            canvas.put(int(round(7.0 - bulge)) + 1, 3 + step, BONE[2])
        raster.line(canvas, 7, 3, 7, 25, RAMPS["ink"][2])
        skull(canvas, 17.2, 9.0, 3.5)
        raster.rect(canvas, 12, 12, 8, 2, colours[2])
        raster.rect(canvas, 12, 12, 8, 1, colours[3])


class Wraith(Archetype):
    """Magic, short band: a shroud with no legs, clear of its own ground.

    The device this style's separation rests on. It is a downward kite, widest
    at the shoulders, tapering to a ragged point that stops three rows above the
    faction disc. So it is the one silhouette in any style with **nothing on
    the ground line**, and the transparency under it is what says so.

    It is a ``mage`` and it walks. The drawing is a name, not a number: nothing
    here declares a traversal, and the catalogue's one flying entry stays the
    Dragon's.
    """

    name = "mage"
    label = "Wraith"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        # One continuous cloth from the peak of the hood to the point of the
        # hem: a vertical diamond. Drawn as one piece rather than as a head on a
        # body, because a wraith with a neck is a person in a sheet.
        raster.polygon(canvas, ((16.0, 0.5), (24.0, 11.5), (19.5, 17.5),
                                (16.0, 21.5), (12.5, 17.5), (8.0, 11.5)), colours,
                       0.52)
        raster.polygon(canvas, ((16.0, 4.0), (20.5, 11.5), (18.0, 18.0),
                                (16.0, 21.5), (14.0, 18.0)), colours, 0.80)
        for tail_x in (10.0, 20.5):  # trailing tatters, ragged at the hem
            raster.polygon(canvas, ((tail_x, 13.0), (tail_x + 2.4, 13.0),
                                    (tail_x + 1.2, 22.0)), colours, 0.36)
        raster.capsule(canvas, 11.5, 12.5, 9.0, 18.0, 1.6, SHROUD, ambient=0.30)
        raster.capsule(canvas, 20.5, 12.5, 23.0, 18.0, 1.6, SHROUD, ambient=0.36)
        bone_hand(canvas, 8.6, 18.8)
        bone_hand(canvas, 23.4, 18.8)
        # The hollow where a face would be, and the two lights in it.
        raster.disc(canvas, 16, 8.4, 3.0, SHROUD, ambient=0.30)
        raster.disc(canvas, 16, 8.8, 2.1, RAMPS["ink"], ambient=0.12)
        for side in (-1, 1):
            canvas.put(16 + side * 1, 9, WISP[1])
        # A cold light cupped in each hand, low and out to the sides. They are
        # the third and fourth points of the hem: a bell hangs to one edge and
        # this hangs to three, which is what separates it from the mourner once
        # both are sixteen pixels of the same green.
        for x in (8.4, 23.6):
            raster.disc(canvas, x, 20.6, 1.8, WISP, ambient=0.62)


class Bellringer(Archetype):
    """Area effect: a bone yoke across the shoulders, a rusted bell at each end.

    The stormcaller is the same raised-arms pose in all four shipped styles, and
    it reads as a placeholder rather than as anybody. This one takes its width
    from **an object** instead: the arms stay short and bent, gripping the yoke
    beside the neck, and the reach comes from the beam and the two heavy lobes
    hanging under it. Raised arms taper to nothing at their ends; this is
    heaviest at its ends, which is what separates the two at sixteen pixels.

    Nothing is thrown forward, so ``lunge`` and ``cast`` still land apart. And a
    bell that tolls over a field is the area-effect role stated plainly.
    """

    name = "stormcaller"
    label = "Bellringer"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, BONE, top=20, bottom=26, spread=3, radius=1.6)
        cloak(canvas, colours, top=14.5, bottom=26.0, width=6.0)
        raster.capsule(canvas, 16, 15.0, 16, 20.5, 2.6, SHROUD, ambient=0.30)
        ribs(canvas, 16, 16, count=2, half_width=2)
        # The yoke: short and thin on purpose. It is the two bells that have to
        # carry the width, because a long bright beam reads as a plank rather
        # than as something a figure is under.
        raster.capsule(canvas, 7.0, 13.0, 25.0, 13.0, 0.9, BONE, ambient=0.44)
        arm(canvas, BONE, 13.0, 15.5, 12.0, 13.5, 1.3)
        arm(canvas, BONE, 19.0, 15.5, 20.0, 13.5, 1.3)
        for side in (-1, 1):  # a bell hanging off each end, mouth downward
            x = 16 + side * 9.0
            raster.line(canvas, int(x), 14, int(x), 15, BONE[1])
            raster.polygon(canvas, ((x - 1.4, 15.0), (x + 1.4, 15.0), (x + 3.2, 21.0),
                                    (x - 3.2, 21.0)), RUST, 0.46)
            raster.polygon(canvas, ((x - 0.8, 16.0), (x + 0.6, 16.0), (x + 1.6, 20.5),
                                    (x - 1.6, 20.5)), RUST, 0.72)
            raster.rect(canvas, int(x) - 3, 21, 7, 1, RUST[4])
            raster.disc(canvas, x, 22.0, 1.1, RUST, ambient=0.55)
            # The yoke's ends curl up into two hooks, standing *above the
            # shoulder cut*, and the roster review is why: ``lunge`` and
            # ``cast`` displace a body identically between row 12 and row 24,
            # and everything this figure has below row 24 stands inside a
            # faction disc that no pose moves. So the band above the shoulder
            # is the only one where its two gestures can differ, and without
            # these it would have a cowl there and nothing else.
            #
            # Drawn in bone rather than in the cold green, which is the same
            # discipline the style was briefed with: four materials, and the
            # green spent on marks rather than on mass. A fifth material's worth
            # of new pixels here costs the sprite three of its sixteen CI4
            # entries and the subset silently remaps whatever it drops.
            raster.capsule(canvas, x, 13.0, x + side * 1.8, 8.4, 1.2, BONE,
                           ambient=0.46)
            raster.capsule(canvas, x + side * 1.8, 8.4, x - side * 0.6, 5.6,
                           1.1, BONE, ambient=0.56)
            canvas.put(int(x - side * 1.0), 5, WISP[1])
            canvas.put(int(x + side * 4), 17, WISP[1])
            canvas.put(int(x + side * 5), 19, WISP[0])
        cowl(canvas, 16, 9.0, 3.4)
        raster.rect(canvas, 13, 12, 7, 1, colours[2])


class Mourner(Archetype):
    """Support, no offensive weapon: a veil to the floor, and a small lamp.

    The healer's soft pale mass, and the roster's second cloth body, drawn as the
    inverse of the first: an upward trapezoid, narrowest at the crown and widest
    where it pools on the ground, against the wraith's downward kite. It is also
    the only figure in the style with **no neck**: the veil falls from the crown
    over the shoulders in one line, so head and body are one shape.

    No weapon, as the role requires. What it carries is a lamp, held low, which
    is the style's one warm-hearted object.
    """

    name = "healer"
    label = "Mourner"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        cloak(canvas, BONE, top=6.0, bottom=27.0, width=7.2)
        raster.disc(canvas, 16, 8.0, 4.0, BONE, ambient=0.58)  # the crown of the veil
        raster.rect(canvas, 14, 7, 4, 20, colours[2])  # the stole down the front
        raster.rect(canvas, 14, 7, 2, 20, colours[3])
        raster.rect(canvas, 11, 11, 10, 2, colours[1])  # the band across the brow
        raster.rect(canvas, 11, 11, 10, 1, colours[2])
        raster.disc(canvas, 16, 9.6, 2.5, RAMPS["ink"], ambient=0.16)  # under the veil
        for side in (-1, 1):
            canvas.put(16 + side * 1, 9, WISP[1])
        raster.capsule(canvas, 11.0, 14.5, 8.5, 17.5, 1.5, BONE, ambient=0.50)
        bone_hand(canvas, 8.2, 18.0)
        # The lamp: a hook, a cage, and a cold light in it. Small and held out
        # to the side, because a support unit that reads as armed is misread,
        # but clear of the veil, or it is not there at all.
        raster.capsule(canvas, 7.6, 18.5, 6.6, 14.5, 0.9, RUST, ambient=0.46)
        raster.disc(canvas, 6.2, 13.0, 2.2, RUST, ambient=0.40)
        raster.disc(canvas, 6.2, 13.0, 1.2, WISP, ambient=0.66)


class BarrowLord(Archetype):
    """The leader, and a win condition: a crowned skull under a banner in rags.

    Rank three ways, as every style states it, because one of them has to survive
    every reduction: the cape carries the mass, the pole breaks the top of the
    frame, and the broken circlet is the detail. What makes this one the style's
    is that the banner is **torn into three tails** rather than being one solid
    triangle, so the top corner of the cell is a fringe rather than a wedge. It
    is also the only crown in the library with a prong missing from it.
    """

    name = "commander"
    label = "Barrow lord"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        raster.capsule(canvas, 25.0, 1.0, 25.0, 27.0, 1.1, BONE, ambient=0.42)
        for step, drop in enumerate((2.0, 5.5, 9.0)):  # the banner, in three rags
            reach = 31.0 - step * 2.0
            raster.polygon(canvas, ((24.0, drop), (reach, drop + 1.4),
                                    (24.0, drop + 3.2)), colours, 0.78 - step * 0.12,
                           dither=False)
        cloak(canvas, colours, top=11.5, bottom=27.0, width=8.0)
        legs(canvas, BONE, top=19, bottom=26, spread=3, radius=1.7)
        boots(canvas, RUST)
        torso(canvas, colours, top=12.5, bottom=21.0, radius=5.0)
        raster.capsule(canvas, 16, 13.5, 16, 19.5, 3.4, RUST, ambient=0.30)
        for side in (-1, 1):
            raster.disc(canvas, 16 + side * 5.0, 13.5, 2.8, RUST, ambient=0.32)
        raster.rect(canvas, 13, 16, 6, 1, RUST[4])
        arm(canvas, BONE, 11.0, 15.0, 8.5, 18.5, 1.5)
        bone_hand(canvas, 8.2, 19.0)
        raster.capsule(canvas, 8.0, 19.5, 7.0, 12.0, 1.2, RUST, ambient=0.48)
        raster.polygon(canvas, ((5.5, 12.5), (7.0, 6.5), (8.5, 12.5)), RUST, 0.80,
                       dither=False)
        skull(canvas, 16, 8.4, 4.0)
        # The circlet, with the second prong gone.
        raster.rect(canvas, 12, 4, 8, 1, RUST[3])
        for offset, height in ((-3, 2.0), (0, 1.0), (3, 2.0)):
            if offset == -3:
                continue
            raster.polygon(canvas, ((16 + offset - 1.0, 4.5), (16 + offset, height),
                                    (16 + offset + 1.0, 4.5)), RUST, 0.86,
                           dither=False)


class GraveThief(Archetype):
    """Fast, acts after striking: the crouch, and two rusted picks.

    The rogue's crouch is kept in every style for the one reason nothing cheaper
    supplies: it is the only figure below the others' shoulder line, and height
    is what a three-shade reduction keeps longest. What this style adds is a hood
    with two lights under it and no face at all, and hooked picks rather than
    straight blades, so the pair reads as dug up rather than drawn.
    """

    name = "rogue"
    label = "Grave-thief"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        legs(canvas, BONE, top=21, bottom=27, spread=4, radius=1.8)
        cloak(canvas, SHROUD, top=14.5, bottom=25.0, width=5.8)
        torso(canvas, colours, top=15.0, bottom=21.0, radius=3.9)
        raster.capsule(canvas, 16, 16.0, 16, 20.5, 2.2, SHROUD, ambient=0.32)
        ribs(canvas, 16, 17, count=2, half_width=2)
        arm(canvas, BONE, 12.0, 16.5, 8.5, 19.0, 1.4)
        arm(canvas, BONE, 20.0, 16.5, 23.5, 14.0, 1.4)
        bone_hand(canvas, 8.2, 19.4)
        bone_hand(canvas, 23.8, 13.6)
        for x0, y0, x1, y1 in ((7.6, 20.2, 5.0, 23.0), (24.2, 12.8, 27.0, 10.0)):
            raster.capsule(canvas, x0, y0, x1, y1, 1.0, RUST, ambient=0.48)
        raster.polygon(canvas, ((5.0, 22.0), (2.8, 23.5), (5.8, 23.8)), RUST, 0.72)
        raster.polygon(canvas, ((27.0, 11.0), (29.2, 8.0), (26.0, 8.4)), RUST, 0.72)
        cowl(canvas, 16, 10.4, 4.0)
        raster.polygon(canvas, ((20.0, 7.5), (23.5, 12.5), (18.5, 11.5)), SHROUD,
                       0.34)
        raster.rect(canvas, 13, 13, 7, 2, colours[2])
        raster.rect(canvas, 13, 13, 7, 1, colours[3])


class BoneHound(Archetype):
    """Non-humanoid: a hound's frame, with the cell showing through its ribs.

    Keeps the ``beast``'s whole structural argument: a long low body, a faction
    ridge along the back, a second faction marker at head height. That is what
    makes an archetype read as *not a person* at any size. What changes
    is that the body is not a mass: a spine runs the length of it with ribs
    hanging off, and the gap at the waist is the sprite's own transparency, so
    the silhouette is pinched in the middle where every other quadruped in the
    library is solid.
    """

    name = "beast"
    label = "Bone hound"

    def draw(self, canvas: Canvas, faction: FactionColour) -> None:
        colours = faction.colours
        for x in (10.0, 12.5, 19.5, 22.0):  # four thin legs
            raster.capsule(canvas, x, 20.0, x + 0.5, 25.5, 1.4, BONE, ambient=0.32)
            raster.disc(canvas, x + 0.8, 26.0, 1.3, BONE, ambient=0.44, squash=1.6)
        raster.capsule(canvas, 9.0, 17.0, 24.0, 15.5, 1.8, BONE, ambient=0.42)
        for step in range(5):  # the ribcage, hanging off the front of the spine
            x = 16.5 + step * 1.9
            raster.capsule(canvas, x, 16.0, x - 1.0, 21.0, 1.0, BONE, ambient=0.38)
        raster.disc(canvas, 22.0, 17.5, 2.6, BONE, ambient=0.36)  # the shoulder
        raster.disc(canvas, 11.0, 18.5, 2.6, BONE, ambient=0.34)  # the haunch
        raster.capsule(canvas, 8.5, 16.5, 4.0, 13.0, 1.0, BONE, ambient=0.40)  # tail
        raster.disc(canvas, 3.6, 12.4, 1.4, colours, ambient=0.52)
        for step in range(4):  # spine ridge, in the faction colour
            raster.polygon(canvas, ((13.0 + step * 2.6, 15.0),
                                    (14.0 + step * 2.6, 11.5),
                                    (15.0 + step * 2.6, 15.0)), colours,
                           0.56 + 0.06 * (step % 2))
        raster.capsule(canvas, 24.0, 15.5, 26.0, 13.0, 1.4, BONE, ambient=0.40)
        raster.capsule(canvas, 23.0, 12.0, 23.5, 16.5, 1.3, colours,
                       ambient=0.55)  # collar
        raster.disc(canvas, 26.5, 12.0, 2.6, BONE, ambient=0.46)
        raster.capsule(canvas, 27.0, 13.0, 30.0, 14.2, 1.5, BONE,
                       ambient=0.40)  # the long muzzle
        raster.rect(canvas, 27, 15, 4, 1, RAMPS["ink"][1])  # the jaw's gap
        canvas.put(26, 12, WISP[1])
        canvas.put(25, 12, WISP[0])


#: One routine per name in ``characters.ARCHETYPE_ORDER``, in that order. The
#: registry in :mod:`.styles` asserts the set matches; the order here is for
#: readers, not for indexing.
ARCHETYPE_CLASSES = (
    BarrowKnight, Bonepicker, Wraith, Bellringer, Mourner, BarrowLord, GraveThief,
    BoneHound,
)

ARCHETYPES = {cls.name: cls() for cls in ARCHETYPE_CLASSES}
