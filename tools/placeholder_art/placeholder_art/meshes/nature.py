# SPDX-License-Identifier: MIT
"""The ``nature`` mesh commission: the same eight roles, carried by animal folk.

The third style drawn as solids, and the first drawn against *two*. Every figure
here has to separate itself from its own seven, from the `medieval` figure in
the same role and from the `scifi` one: three constraints where
:mod:`.medieval` had one and :mod:`.scifi` two.

What this style was expected to make cheap, and what it actually did
--------------------------------------------------------------------
:mod:`.rules` carries the finding the `medieval` `beast` paid for:
**horizontal beats standing**, because a quadruped's body runs along the
axis a sixty-degree camera shows best, and that figure read on its *first* draft
where the crouching rogue took ten. A style whose whole idea is animals looks
like the favourable case for it.

It is not, and the reason is worth an author's attention. **Seven of these eight
stand upright.** An animal drawn as folk is a humanoid to this camera: two
legs, a torso, a head at the top. The species has to be spent on parts that
differ in x and z exactly as a knight's does. What the animals give is a
*vocabulary* for those parts rather than a discount on them, and it is a good
one because every item in it is already in an axis this camera reads:

* a **muzzle** thrown forward in −z, which is the front face a viewer sees and
  which the whole style already shares as a sprite mark;
* **ears** set back in +z above the skull, so they rise on screen for free:
  round on the badger and the bear, sharp on the cat, the stoat and the lion;
* a **tail** run out in x at hip height, which nothing humanoid in either other
  style has;
* **antlers** and a **mane**, which are the two ways a head can be made wider
  than the shoulders instead of narrower.

The one figure the finding did pay in full is the **boar**, and it paid exactly
as predicted: it is long in x, it differs from all seven of its siblings before
a detail is authored, and the work went entirely into separating it from the
*other* styles' quadrupeds rather than from its own roster.

What the palette gave these parts
---------------------------------
A mesh's ramps are resolved from its own sprite's CLUT: the neutral ramp is
the entries all six factions share, luminance-sorted and sampled at four rungs.
This style is where that stops being a curiosity, because
``nature``'s docstring is explicit that fur is what it spends most of its
colours on, and a material that a whole body wears still occupies **one CLUT
entry**. Measured, per sprite, on the four rungs the machine actually resolves:

* the **archer cat** and the **storm stag** both paint their bodies in ``dirt``
  and **not one** of ``dirt``'s entries is sampled in either. The cat's fur is
  drawn in the bark browns its bow is drawn in; the stag's in the warm sand its
  antlers are. Both were authored to that rather than against it: the stag's
  antlers take the *brightest* rung and its body the warm one, which is the
  medieval stormcaller's separation reused.
* the **mage bear**'s green is unreachable and the **healer owl**'s gold beak
  is, so the acorn takes the brightest neutral rung and the beak takes the
  darkest. A dark beak on a pale facial disc is the better drawing anyway: at
  twenty pixels a face is a hole with a rim of light, and the darkest neutral
  rung is usually ``ink``, which draws as exactly that hole. It is the right
  answer for a beak, a nose or a visor and the wrong one for anything a viewer
  must read as *material*. That is why the stag's separator under the chin is
  the faction ramp's brightest rung rather than the neutral's darkest, and why
  the stoat's black tail tip was dropped: an ink-rung part outboard of the
  figure is drawn against the board, where it is a gap and not a mark.
* the **lion warden** reaches neither white nor gold, its brightest rung being
  a pale sand, so its mane, belt, spear head and finial are one tone separated
  by being in four places.
* the **storm stag** and the **healer owl** are the two whose rung 1 *is* a
  green, so their leaves and their herb sprig are the only parts in this
  commission wearing a colour the author chose rather than accepted.
* five of the eight resolve a **three-entry faction ramp**, where rungs 0 and 1
  are the same colour. No part in those five names rung 0.

The rule that follows, and it is in the spec: check what the machine gives a
part, not what the drawing intended, and separate two parts that share a rung by
geometry rather than by a colour difference that does not exist.

Two numbers this commission paid for
------------------------------------
Both are consequences of arithmetic :mod:`.rules` already states, and both cost
a draft because nobody had written the consequence down.

**A part thrown forward sinks, and a face is where an author notices.** Screen
height is ``0.5·y + 0.866·z``, so moving a detail toward the viewer lowers it by
``0.866`` of the move. Every muzzle in this style was first authored at ten or
twelve units proud of its skull, the way a snout is proud of a head in the
sprite, and every one of them drew **at the chest**: the badger's pale mask
stripe at z −21 landed sixteen world units below the head that was supposed to
be wearing it, which is a quarter of the drawn figure. Held to about **five
units proud** the same parts sit on the face. Depth is how a raised feature is
bought and it is equally how a face is lost: *the throw that makes a front face
visible is also a fall*, and five or six units is the whole budget a head detail
has.

**A full-height front strip has to be cut into one box a mass.** The far-to-near
order is ``z·cos φ − y·sin φ``, so height dominates depth in it: a strip
authored from hem to collar averages a *low* y and is therefore sorted **far**,
and every mass above it is drawn over it. The mage bear's placket, the storm
stag's tabard and the healer owl's stole were all authored as one tall box each
and all three lost their upper two thirds to the chest behind them. The fix is
not more depth, it is **one strip box per mass, each a little prouder than the
mass it fronts**. That is the construction the `scifi` medic's stole already
used, at two boxes, without the reason being recorded.
"""

from __future__ import annotations

from typing import Mapping, Tuple

from .rules import RAMP_FACTION, RAMP_NEUTRAL, Part

#: The style these tables are the commission for. Declared rather than taken
#: from the filename so the registry in :mod:`.` cannot bind a module to the
#: wrong style's sprites. That is the one mistake the silhouette check would
#: not catch, because it would simply hold this badger to a knight's box.
STYLE = "nature"


# ---------------------------------------------------------------------------
# The badger guard
#
# The knight's role, and the widest torso in this style. It stands beside a
# `medieval` knight with a sword held vertical and a `scifi` trooper with a
# carbine held level, so the third answer to "what is the weapon doing" had to
# be neither, and the sprite already gives it: a spear carried **up and out to
# the right**. It is authored the one way a diagonal survives this camera and
# no other: **two boxes, each longer than the step between them is high**, with
# a broad flint head standing at the top of the second. Two drafts is what a
# diagonal costs; five is rubble.
#
# The shield is the other half. The knight's is a mid-height panel at the
# shoulder and the trooper's a rectangle from ankle to chest; this is a
# **disc** and it is authored as three concentric boxes each proud of the last
# in −z: a bark rim, a faction field, a pale boss. Depth is what makes a disc
# a disc at this pitch: nested boxes at one depth would be one flat plate.
#
# The head is the style's shared vocabulary in its plainest form: a muzzle
# thrown forward in −z, round ears set *back* in +z so they rise, a dark nose
# at the very front and the badger's mask stripe as a narrow pale wall down the
# brow. The stripe is the only thing in the figure that runs vertically, which
# is what a figure whose masses are all horizontal bands needs: without one it
# is a stack of slabs.
#
# The head is also where this commission learned what a forward throw costs.
# The stripe was first authored at z −21..−18, eleven units proud of the skull,
# and it drew at the badger's *chest*: a part moved toward the viewer sinks by
# 0.866 of the move, so eleven units of throw is sixteen of fall out of a
# sixty-four-unit figure. At three units proud it is on the brow. The same
# bound holds the shield's three rings to three units of step each. What makes
# that disc a disc is the inset in x, not the depth.
# ---------------------------------------------------------------------------

BADGER_GUARD: Tuple[Part, ...] = (
    Part(-14,  -4,   0,   5,  -5,   5, RAMP_NEUTRAL, 0, "left paw"),
    Part(  4,  14,   0,   5,  -5,   5, RAMP_NEUTRAL, 0, "right paw"),
    Part(-13,  -5,   5,  25,  -4,   4, RAMP_NEUTRAL, 1, "left shin"),
    Part(  5,  13,   5,  25,  -4,   4, RAMP_NEUTRAL, 1, "right shin"),
    Part(-14,  -4,  24,  40,  -5,   5, RAMP_NEUTRAL, 1, "left haunch"),
    Part(  4,  14,  24,  40,  -5,   5, RAMP_NEUTRAL, 1, "right haunch"),
    Part(-13,  13,  39,  55,  -6,   6, RAMP_FACTION, 3, "belly"),
    Part(-26,  -8,  23,  65,  -8,  -5, RAMP_NEUTRAL, 2, "shield, bark rim"),
    Part(-23, -11,  29,  59, -10,  -8, RAMP_FACTION, 2, "shield, faction field"),
    Part(-19, -15,  41,  51, -12, -10, RAMP_NEUTRAL, 3, "shield, boss"),
    Part(-12,  12,  53,  68,  -7,   7, RAMP_NEUTRAL, 1, "chest"),
    Part( 12,  24,  56,  63,  -9,  -7, RAMP_NEUTRAL, 2, "spear, haft"),
    Part(-19, -11,  61,  69,  -6,   6, RAMP_NEUTRAL, 1, "left shoulder"),
    Part( 11,  19,  61,  69,  -6,   6, RAMP_NEUTRAL, 1, "right shoulder"),
    Part( 20,  28,  64,  72,  -9,  -7, RAMP_NEUTRAL, 2, "spear, haft raised"),
    Part(-12,  12,  67,  83,  -7,   7, RAMP_NEUTRAL, 1, "head"),
    Part( -5,   5,  69,  76, -10,  -7, RAMP_NEUTRAL, 2, "muzzle"),
    Part( -2,   2,  70,  73, -11, -10, RAMP_NEUTRAL, 0, "nose"),
    Part(-13,  -7,  79,  85,   0,   5, RAMP_NEUTRAL, 1, "left ear"),
    Part(  7,  13,  79,  85,   0,   5, RAMP_NEUTRAL, 1, "right ear"),
    Part( 22,  28,  71,  83, -10,  -7, RAMP_NEUTRAL, 3, "spear, head"),
    Part( -4,   4,  75,  83,  -9,  -7, RAMP_NEUTRAL, 3, "mask stripe"),
)

# ---------------------------------------------------------------------------
# The archer cat
#
# The archer's role. The bow is the `medieval` archer's construction kept
# deliberately, a tall thin thing offset in z rather than stacked in y. The two
# figures are then separated by everything that is *not* the bow, which is
# where the cat earns its place:
#
# * **sharp ears**, two thin walls twenty units tall with eight units of nothing
#   between them. A comb of narrow walls survives this camera where a mass does
#   not, which is the construction the `scifi` drone swarm's antenna array
#   already uses;
# * a **tail**, run out to +x at hip height and then turned up. Two boxes, one
#   long horizontal and one short vertical, so it draws an L rather than a
#   staircase. Nothing humanoid in `medieval` or `scifi` has anything at all
#   out there.
#
# The quiver is set *behind* the right shoulder at z 4..12 rather than in front
# of it, which is the sign an author gets wrong: a part moved away
# rises, and a quiver that did not rise would be a bar across the ribs.
# ---------------------------------------------------------------------------

ARCHER_CAT: Tuple[Part, ...] = (
    Part(-13,  -5,   0,   6,  -6,   6, RAMP_NEUTRAL, 0, "left paw"),
    Part(  5,  13,   0,   6,  -6,   6, RAMP_NEUTRAL, 0, "right paw"),
    Part(-12,  -6,   6,  26,  -4,   4, RAMP_NEUTRAL, 2, "left shin"),
    Part(  6,  12,   6,  26,  -4,   4, RAMP_NEUTRAL, 2, "right shin"),
    Part( 12,  26,  18,  25,   3,   8, RAMP_NEUTRAL, 2, "tail, out"),
    Part( 20,  28,  24,  37,   4,  10, RAMP_NEUTRAL, 2, "tail, curl"),
    Part(-24, -16,   6,  44,  -6,   0, RAMP_NEUTRAL, 1, "bow, lower limb"),
    Part(-12,  -4,  25,  42,  -5,   5, RAMP_NEUTRAL, 2, "left haunch"),
    Part(  4,  12,  25,  42,  -5,   5, RAMP_NEUTRAL, 2, "right haunch"),
    Part(-11,  11,  40,  62,  -6,   6, RAMP_FACTION, 3, "torso"),
    Part(-18,  -8,  51,  57,  -8,  -3, RAMP_NEUTRAL, 2, "left arm, drawing"),
    Part(  8,  14,  49,  61,  -7,  -1, RAMP_NEUTRAL, 2, "right arm"),
    Part( 10,  16,  51,  74,   3,   8, RAMP_NEUTRAL, 1, "quiver"),
    Part(-24, -16,  43,  80,  -6,   0, RAMP_NEUTRAL, 1, "bow, upper limb"),
    Part(-11,  11,  60,  65,  -8,  -6, RAMP_FACTION, 2, "collar"),
    Part( -9,   9,  64,  78,  -6,   6, RAMP_NEUTRAL, 2, "head"),
    Part( -4,   4,  66,  72, -10,  -6, RAMP_NEUTRAL, 1, "muzzle"),
    Part( -2,   2,  67,  70, -11, -10, RAMP_NEUTRAL, 0, "nose"),
    Part(-10,  -4,  75,  89,   0,   6, RAMP_NEUTRAL, 2, "left ear"),
    Part(  4,  10,  75,  89,   0,   6, RAMP_NEUTRAL, 2, "right ear"),
)

# ---------------------------------------------------------------------------
# The mage bear
#
# The mage's role, and the narrowest figure in the style: its sprite's opaque
# box asks for forty world units where the stag's asks sixty-four. So the
# bear's breadth cannot be bought in x at all, and it is bought in **what the
# width is spent on** instead: a body of one constant width from the hem to
# the collar, with no shank and no leg gap anywhere in it, and round ears set
# as far apart as the skull allows.
#
# That makes it the legless case, and it takes that case's answer unaltered:
# **a robe has no legs, so it has no gap**, and something has to run
# vertically. The faction placket runs from y 14 to y 94 (two thirds of the
# whole figure) and it is, for a style that puts fur on everything, the only
# large contiguous surface this figure has to give the faction ramp.
#
# It is **three boxes and not one**, and that is the second thing this
# commission measured rather than assumed. Authored as a single tall strip it
# averaged a low y, sorted far by `z·cos φ − y·sin φ`, and the belly and the
# chest were both drawn over the top of it: the stripe reached the hem and
# stopped at the waist. One box a mass, each three units prouder than the mass
# it fronts, and the whole strip survives. Depth does not fix it; only cutting
# it does.
#
# The staff is on the off side, which the sprite chose and this keeps for the
# sprite's own stated reason: the lion's banner is a pole on the weapon side
# and two poles on the same side would make one silhouette of two. Its acorn is
# the tallest thing in the model and it is authored in the *brightest neutral
# rung* rather than in a green, because this sprite's CLUT does not reach one.
# The measurement is in the module docstring and the drawing follows it.
# ---------------------------------------------------------------------------

MAGE_BEAR: Tuple[Part, ...] = (
    Part(-14,  14,   0,  13,  -6,   6, RAMP_NEUTRAL, 1, "robe hem"),
    Part(-19, -13,   0,  41,  -4,   1, RAMP_NEUTRAL, 1, "staff, lower"),
    Part(-13,  13,  11,  36,  -6,   6, RAMP_NEUTRAL, 1, "robe skirt"),
    Part( -5,   5,  10,  36,  -9,  -6, RAMP_FACTION, 3, "placket, skirt"),
    Part(-18, -12,  36,  43,  -9,  -3, RAMP_NEUTRAL, 2, "left paw"),
    Part( 12,  18,  36,  43,  -9,  -3, RAMP_NEUTRAL, 2, "right paw"),
    Part(-13,  13,  34,  53,  -7,   7, RAMP_NEUTRAL, 2, "belly"),
    Part( -5,   5,  34,  53,  -9,  -7, RAMP_FACTION, 3, "placket, belly"),
    Part(-17, -11,  40,  60,  -6,   3, RAMP_NEUTRAL, 2, "left arm"),
    Part( 11,  17,  40,  60,  -6,   3, RAMP_NEUTRAL, 2, "right arm"),
    Part(-19, -13,  40,  74,  -4,   1, RAMP_NEUTRAL, 1, "staff, upper"),
    Part(-12,  12,  51,  67,  -7,   7, RAMP_NEUTRAL, 2, "chest"),
    Part( -5,   5,  51,  67,  -9,  -7, RAMP_FACTION, 3, "placket, chest"),
    Part(-12,  12,  64,  70, -10,  -8, RAMP_FACTION, 2, "collar"),
    Part(-10,  10,  69,  84,  -6,   6, RAMP_NEUTRAL, 2, "head"),
    Part( -5,   5,  71,  79, -11,  -6, RAMP_NEUTRAL, 1, "muzzle"),
    Part(-20, -12,  73,  86,  -6,   3, RAMP_NEUTRAL, 2, "acorn"),
    Part( -2,   2,  72,  76, -12, -11, RAMP_NEUTRAL, 0, "nose"),
    Part(-14,  -8,  79,  89,   1,   7, RAMP_NEUTRAL, 1, "left ear"),
    Part(  8,  14,  79,  89,   1,   7, RAMP_NEUTRAL, 1, "right ear"),
    Part(-18, -14,  84,  91,  -5,   2, RAMP_NEUTRAL, 3, "acorn cap"),
)

# ---------------------------------------------------------------------------
# The storm stag
#
# The stormcaller's role, and the one figure in this commission where the animal
# *is* the device rather than a dress on it. That is why this sprite keeps
# its raised arms where three others in the role lower theirs. A stag's antlers
# reduce to exactly the wide radiating star the archetype needs.
#
# So the antlers get the depth budget and everything else is held out of it.
# Four boxes, two a side: a **beam** sweeping out and back and a **prong**
# further out and further back again. Both are plates by the wall test and both
# are meant to be: a part moved away rises at `tan φ` to one, so a fan
# authored at z 2..16 is drawn as a bright horizontal spread *above* the head
# without one unit of it being stacked in y. Nothing else in any style has
# anything that wide over its own skull.
#
# The head survives being under it because of what is between: the head is on
# the warm rung, the antlers on the brightest one, and the collar under the
# chin is the faction ramp's brightest. It is a bright band rather than a dark
# one, because this sprite's darkest neutral rung is an ink that draws as a
# hole at this size and a hole under a skull reads as a missing jaw. That is the
# `medieval` stormcaller's separation with its sign flipped, and the reason for
# the flip is the palette rather than the drawing.
#
# The muzzle *is* on that ink rung, and small: six units wide inside an
# eighteen-unit head, four proud of it. A dark hole that small is a snout; the
# first draft made it ten wide and it read as an open mouth.
#
# The leaves are the loosed cast the sprite draws, and they are the only parts
# in this commission wearing a colour that was chosen: this sprite's neutral
# rung 1 resolves to a foliage green, so a leaf is a leaf. They sit outboard of
# the paws and behind them in z, so the throw reads as going away.
# ---------------------------------------------------------------------------

STORM_STAG: Tuple[Part, ...] = (
    Part(-15,  15,   0,  16,  -6,   6, RAMP_FACTION, 2, "robe hem"),
    Part(-13,  13,  14,  34,  -6,   6, RAMP_FACTION, 2, "robe"),
    Part( -6,   6,  14,  34,  -8,  -6, RAMP_FACTION, 3, "tabard, skirt"),
    Part(-12,  12,  32,  54,  -6,   6, RAMP_NEUTRAL, 2, "torso"),
    Part( -6,   6,  32,  54,  -8,  -6, RAMP_FACTION, 3, "tabard, chest"),
    Part(-26, -12,  49,  56,  -4,   3, RAMP_NEUTRAL, 2, "left arm, out"),
    Part( 12,  26,  49,  56,  -4,   3, RAMP_NEUTRAL, 2, "right arm, out"),
    Part(-32, -24,  49,  57,  -5,   1, RAMP_NEUTRAL, 2, "left paw"),
    Part( 24,  32,  49,  57,  -5,   1, RAMP_NEUTRAL, 2, "right paw"),
    Part(-32, -22,  57,  63,   0,   8, RAMP_NEUTRAL, 1, "leaves, loosed left"),
    Part( 22,  32,  57,  63,   0,   8, RAMP_NEUTRAL, 1, "leaves, loosed right"),
    Part( -8,   8,  54,  58,  -8,  -6, RAMP_FACTION, 3, "collar"),
    Part( -9,   9,  57,  70,  -6,   6, RAMP_NEUTRAL, 2, "head"),
    Part( -3,   3,  60,  64,  -8,  -6, RAMP_NEUTRAL, 0, "muzzle"),
    Part(-26,  -8,  67,  75,   1,   8, RAMP_NEUTRAL, 3, "antler beam, left"),
    Part(  8,  26,  67,  75,   1,   8, RAMP_NEUTRAL, 3, "antler beam, right"),
    Part(-30, -16,  74,  83,   4,  10, RAMP_NEUTRAL, 3, "antler prongs, left"),
    Part( 16,  30,  74,  83,   4,  10, RAMP_NEUTRAL, 3, "antler prongs, right"),
)

# ---------------------------------------------------------------------------
# The healer owl
#
# The healer's role, and the third pale figure with a device over one shoulder
# in as many styles. What separates it from the `medieval` healer's robe and
# staff and the `scifi` medic's legs and injector is the outline, before any
# detail:
#
# * it has **no shank and no leg gap**: a plumage hem straight to the ground,
#   with two dark talons at the very front of it and nothing else below the
#   waist. The medic has legs; this does not;
# * it is **widest at the shoulders**, because the folded wings run from y 20 to
#   y 74 outboard of everything else in the figure. Both other healers narrow
#   toward the top;
# * the **facial disc is as wide as the body**, which is the thing this camera
#   punishes: the part above a head should be nothing and the part below it
#   wider. It is the owl's entire read, so it is kept and paid for
#   instead. What pays for it is the faction yoke immediately under it, an
#   eight-unit band proud in −z, and the two rings and the dark beak on the
#   disc itself. At twenty pixels a face is a hole with a rim of light around
#   it, and the beak is that hole.
#
# The rings are on the *brightest* rung and the disc behind them on the one
# below, which is the way round the sprite draws it and the opposite of the
# first draft. A bright disc with duller rings on it draws as one pale slab
# with a grey bar across it: goggles, not an owl. The wings take the bright
# rung too, so what the figure is made of at a distance is two bright wings and
# two bright rings with a duller body between them.
#
# The herb sprig is the one green in the figure (this sprite's neutral rung 1
# resolves to a foliage entry) and it is set back at z 0..12 so it rises above
# the shoulder rather than crossing it.
# ---------------------------------------------------------------------------

HEALER_OWL: Tuple[Part, ...] = (
    Part(-17,  17,   0,  11,  -6,   6, RAMP_NEUTRAL, 2, "plumage hem"),
    Part( -9,  -4,   0,   4,  -8,  -6, RAMP_NEUTRAL, 0, "left talon"),
    Part(  4,   9,   0,   4,  -8,  -6, RAMP_NEUTRAL, 0, "right talon"),
    Part(-16,  16,  10,  30,  -6,   6, RAMP_NEUTRAL, 2, "body, lower"),
    Part( -5,   5,  10,  30,  -8,  -6, RAMP_FACTION, 3, "stole, skirt"),
    Part(-22, -14,  13,  47,  -5,   4, RAMP_NEUTRAL, 3, "left wing, folded"),
    Part( 14,  22,  13,  47,  -5,   4, RAMP_NEUTRAL, 3, "right wing, folded"),
    Part(-15,  15,  29,  52,  -6,   6, RAMP_NEUTRAL, 2, "body, upper"),
    Part( -5,   5,  29,  52,  -8,  -6, RAMP_FACTION, 3, "stole, chest"),
    Part( 14,  20,  39,  63,   0,   5, RAMP_NEUTRAL, 1, "herb sprig, stem"),
    Part(-15,  15,  52,  57,  -8,  -4, RAMP_FACTION, 2, "shoulder yoke"),
    Part( 12,  22,  61,  71,   1,   8, RAMP_NEUTRAL, 1, "herb sprig, head"),
    Part(-14,  14,  56,  73,  -6,   6, RAMP_NEUTRAL, 2, "facial disc"),
    Part(-13,  -3,  58,  70,  -8,  -6, RAMP_NEUTRAL, 3, "disc ring, left"),
    Part(  3,  13,  58,  70,  -8,  -6, RAMP_NEUTRAL, 3, "disc ring, right"),
    Part( -2,   2,  63,  70,  -9,  -8, RAMP_NEUTRAL, 0, "beak"),
    Part( -9,   9,  71,  81,  -5,   5, RAMP_NEUTRAL, 2, "crown"),
)

# ---------------------------------------------------------------------------
# The lion warden
#
# The commander's role, and the hardest separation in this commission, because
# the `scifi` captain already owns the obvious answer to a sprite that draws a
# full-height standard: a mast on one side, behind the shoulder, with a pennant
# high on it. This sprite draws the same thing and there is no second way to
# author a pole.
#
# So the separation is spent elsewhere, and in two places rather than one:
#
# * the figure carries **two vertical lines, one either side**: the banner pole
#   on the right and a spear on the left, with the body between them. The
#   captain has a mast and a low sidearm, so its outline is a column with one
#   line; this is a column with two, and the spear's head is a bright block at
#   shoulder height on the side the captain keeps empty;
# * the **mane**. It is a ring twenty-four units tall and thirty wide on the
#   *brightest* neutral rung, with a face two rungs darker set forward of it in
#   −z, so the head is drawn as a dark block *inside* a halo. Nothing in either
#   other style has anything around its skull, and at twenty pixels it is the
#   single mark that says which commander this is. The mane was one rung dimmer
#   in the first draft and the figure read as a gold blob with a brown notch in
#   it; the halo only works when it is the brightest thing in the model.
#
# The cloak is the drape held below the shoulders at z 8..16, which is the
# construction two earlier commissions arrived at: a cape cannot hang behind a
# figure at this pitch, it haloes it, and the only cape that survives is one
# authored low enough that its top face never reaches the head's.
#
# This sprite's CLUT reaches no white at all, and no gold either, both of the
# `gold` ramp's entries falling between the four the sampling lands on. So the
# brightest thing available is a pale sand, and the mane, the belt, the spear
# head and the banner finial are all the same rung. They are separated by being
# in four different places rather than by tone, and the shoulder studs step
# down one rung so that they do not join the mane into a single bright cap.
# ---------------------------------------------------------------------------

LION_WARDEN: Tuple[Part, ...] = (
    Part(-13,  -3,   0,   6,  -6,   6, RAMP_NEUTRAL, 1, "left paw"),
    Part(  3,  13,   0,   6,  -6,   6, RAMP_NEUTRAL, 1, "right paw"),
    Part(-12,  -4,   6,  27,  -4,   4, RAMP_NEUTRAL, 1, "left shin"),
    Part(  4,  12,   6,  27,  -4,   4, RAMP_NEUTRAL, 1, "right shin"),
    Part(-18,  18,   3,  58,   6,  11, RAMP_FACTION, 2, "warden's cloak"),
    Part(-20, -14,   7,  58,  -3,   3, RAMP_NEUTRAL, 1, "spear, haft"),
    Part( 20,  26,   0,  80,   3,   7, RAMP_NEUTRAL, 1, "banner pole"),
    Part(-14,  14,  38,  53,  -6,   6, RAMP_FACTION, 3, "skirt"),
    Part(-12,  12,  48,  53,  -9,  -7, RAMP_NEUTRAL, 3, "gold belt"),
    Part(-17, -12,  51,  66,  -6,   3, RAMP_NEUTRAL, 1, "left arm"),
    Part( 12,  17,  51,  66,  -6,   3, RAMP_NEUTRAL, 1, "right arm"),
    Part(-12,  12,  52,  70,  -7,   7, RAMP_NEUTRAL, 1, "torso"),
    Part(-20, -12,  56,  73,  -4,   3, RAMP_NEUTRAL, 3, "spear, head"),
    Part(-18, -10,  62,  69,  -8,   1, RAMP_NEUTRAL, 2, "shoulder stud, left"),
    Part( 10,  18,  62,  69,  -8,   1, RAMP_NEUTRAL, 2, "shoulder stud, right"),
    Part( 24,  32,  66,  82,   0,  10, RAMP_FACTION, 3, "banner pennant"),
    Part(-15,  15,  68,  84,  -8,   8, RAMP_NEUTRAL, 3, "mane"),
    Part( -8,   8,  70,  82, -10,  -8, RAMP_NEUTRAL, 1, "face"),
    Part( -5,   5,  72,  78, -12, -10, RAMP_NEUTRAL, 2, "muzzle"),
    Part( 20,  26,  80,  90,   3,   7, RAMP_NEUTRAL, 3, "banner finial"),
    Part(-12,  -6,  82,  89,   0,   6, RAMP_NEUTRAL, 2, "left ear"),
    Part(  6,  12,  82,  89,   0,   6, RAMP_NEUTRAL, 2, "right ear"),
)

# ---------------------------------------------------------------------------
# The stoat
#
# The rogue's role, and the third crouched figure with two blades. The
# `medieval` rogue spent ten drafts learning that **a crouch is not something a
# mesh can author**: rule 1 builds every figure `unit_world / cos φ` tall
# whatever it is doing. So nothing here tries to. What the stoat has that
# neither the `medieval` rogue nor the `scifi` infiltrator has is a **tail**,
# and it is given the room the crouch cannot use:
#
# * two boxes out to +x, one long and level and one turning up at its end, both
#   on the rung *below* the body's so that the tail is not read as a third arm.
#   Where it sits took three tries and the answer was **height, not length**:
#   at hip height it drew a hand's breadth under the arm and the knife and the
#   figure grew a limb; at shin height, y 14 to 38, there is nothing else
#   within twenty units of it and it reads as a tail. The sprite's black tip is
#   not here. An ink-rung part outboard of the silhouette is drawn against the
#   board rather than against the figure, and at that size it is a gap, not a
#   mark.
#
# The blades are the diagonal both other rogues carry, one low on the far side
# and one high on the near, and they are two boxes, not four, each fourteen
# units long against eight of height. What keeps this figure from being the
# infiltrator is that the infiltrator is a narrow column that spends its width
# entirely on its blades; here the blades reach only as far as the tail does on
# the other side, and there is a bare head with sharp ears where the
# infiltrator has a hood.
#
# The mantle is behind at z 8..14 and stops at y 58, which is the same low
# bound the lion's cloak and the infiltrator's shroud are both held to.
# ---------------------------------------------------------------------------

STOAT: Tuple[Part, ...] = (
    Part(-14,  -6,   0,   6,  -6,   1, RAMP_NEUTRAL, 2, "left paw"),
    Part(  6,  14,   0,   6,  -6,   1, RAMP_NEUTRAL, 2, "right paw"),
    Part( 14,  30,   8,  13,   2,   7, RAMP_NEUTRAL, 1, "tail, out"),
    Part(-13,  -7,   4,  18,  -6,   0, RAMP_NEUTRAL, 2, "left shin"),
    Part(  7,  13,   4,  18,  -6,   0, RAMP_NEUTRAL, 2, "right shin"),
    Part( 24,  30,  12,  21,   3,   8, RAMP_NEUTRAL, 1, "tail, tip"),
    Part(-14,  14,   2,  32,   4,   8, RAMP_NEUTRAL, 1, "mantle"),
    Part(-28, -14,  12,  17, -10,  -7, RAMP_NEUTRAL, 3, "bone knife, low"),
    Part(-13,  -5,  17,  30,  -4,   1, RAMP_NEUTRAL, 2, "left thigh"),
    Part(  5,  13,  17,  30,  -4,   1, RAMP_NEUTRAL, 2, "right thigh"),
    Part(-12,  12,  29,  34,  -7,   0, RAMP_NEUTRAL, 1, "belt"),
    Part(-17, -11,  34,  44,  -8,  -1, RAMP_NEUTRAL, 2, "left arm"),
    Part(-11,  11,  33,  49,  -6,   3, RAMP_FACTION, 3, "torso"),
    Part( 11,  17,  39,  49,  -8,  -1, RAMP_NEUTRAL, 2, "right arm"),
    Part( -5,   5,  36,  51,  -7,  -6, RAMP_NEUTRAL, 3, "throat bib"),
    Part(-11,  11,  48,  52,  -7,  -6, RAMP_FACTION, 2, "chest band"),
    Part( 14,  28,  46,  51, -11,  -8, RAMP_NEUTRAL, 3, "bone knife, high"),
    Part( -9,   9,  51,  63,  -5,   5, RAMP_NEUTRAL, 2, "head"),
    Part( -4,   4,  53,  57,  -8,  -5, RAMP_NEUTRAL, 3, "muzzle"),
    Part( -2,   2,  53,  56,  -9,  -8, RAMP_NEUTRAL, 0, "nose"),
    Part( -9,  -3,  61,  71,   0,   4, RAMP_NEUTRAL, 2, "left ear"),
    Part(  3,   9,  61,  71,   0,   4, RAMP_NEUTRAL, 2, "right ear"),
)

# ---------------------------------------------------------------------------
# The boar
#
# The beast's role, and the one figure here that the `medieval` beast's finding
# paid for in full: it is long in x, it is the only thing in this style that
# is, and it read on the first draft. All of the work went into separating it
# from the *other two* quadrupeds, which is exactly what the first two
# predicted a third one would cost.
#
# Three things do it, and every one is placed rather than shaped:
#
# * **no neck, and a longer, lower body.** The `medieval` beast lifts a skull
#   well above its back on a neck and the `scifi` xenoform lifts a head on a
#   neck plate; this head sits *in* the body's line, from y 44 to y 82 against
#   a body of 38 to 76, and the body runs forty units in x where the beast's
#   runs forty-two at a taller stance. All three quadrupeds resolve almost the
#   same two rungs from their own CLUTs, hide over ink and a dark maroon, so
#   colour separates none of them and the profile has to do all of it.
# * **a comb, at two heights.** The beast has three spine plates and the
#   xenoform three carapace plates, all short and all level. This has five
#   bristles four units wide with five units of nothing between them, the
#   measured width at which a gap is structure, and they stand at *two*
#   heights: the two nearest the rump short and the three over the shoulder
#   running all the way to the built height. Two heights is one step; five
#   would be the staircase the sniper's rail was refused for. They are also the
#   whole of this figure's faction ramp, which is where the eye already is.
# * **tusks thrown forward.** Two boxes at the muzzle on the warm rung, the near
#   one low and four units proud of the snout and the far one higher and
#   further out in x, so the pair is drawn as one curving up past the other.
#   Neither other quadruped has anything bright at its mouth. They were first
#   authored twelve units apart in y and adjacent in z and drew as one gold
#   slab; what made them a pair was putting the difference in **depth** rather
#   than in height, because depth is what moves a part on screen fastest.
# ---------------------------------------------------------------------------

BOAR: Tuple[Part, ...] = (
    Part(-22, -15,   0,  20,   3,   7, RAMP_NEUTRAL, 0, "rear leg, far"),
    Part( -1,   6,   0,  20,   3,   7, RAMP_NEUTRAL, 0, "front leg, far"),
    Part(-22, -15,   0,  20,  -7,  -4, RAMP_NEUTRAL, 1, "rear leg, near"),
    Part( -1,   6,   0,  20,  -7,  -4, RAMP_NEUTRAL, 1, "front leg, near"),
    Part(-25, -19,  21,  26,  -1,   3, RAMP_NEUTRAL, 1, "tail"),
    Part(-28, -22,  22,  28,   0,   4, RAMP_FACTION, 3, "tail tuft"),
    Part(-26,  14,  18,  36,  -7,   6, RAMP_NEUTRAL, 1, "body"),
    Part( 24,  34,  22,  30,  -8,  -4, RAMP_NEUTRAL, 1, "snout"),
    Part( 16,  32,  21,  38,  -6,   4, RAMP_NEUTRAL, 1, "head"),
    Part( 28,  34,  28,  35,  -4,  -1, RAMP_NEUTRAL, 2, "tusk, far"),
    Part( 26,  32,  26,  32,  -9,  -7, RAMP_NEUTRAL, 2, "tusk, near"),
    Part( 18,  24,  34,  36,  -7,  -6, RAMP_NEUTRAL, 3, "eye"),
    Part(-24, -20,  36,  49,  -2,   2, RAMP_FACTION, 3, "bristle, rump"),
    Part(-15, -11,  36,  49,  -2,   2, RAMP_FACTION, 3, "bristle, loin"),
    Part( -6,  -2,  36,  60,  -2,   2, RAMP_FACTION, 3, "bristle, back"),
    Part(  3,   7,  36,  60,  -2,   2, RAMP_FACTION, 3, "bristle, shoulder"),
    Part( 12,  16,  36,  60,  -2,   2, RAMP_FACTION, 3, "bristle, nape"),
)

#: This style's commissioned meshes, by archetype name.
MESHES: Mapping[str, Tuple[Part, ...]] = {
    "knight": BADGER_GUARD,
    "archer": ARCHER_CAT,
    "mage": MAGE_BEAR,
    "stormcaller": STORM_STAG,
    "healer": HEALER_OWL,
    "commander": LION_WARDEN,
    "rogue": STOAT,
    "beast": BOAR,
}
