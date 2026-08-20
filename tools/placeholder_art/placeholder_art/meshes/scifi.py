# SPDX-License-Identifier: MIT
"""The ``scifi`` mesh commission: the same eight roles, dressed as a war fleet.

The second style drawn as solids, and the first drawn *against* another one.
Everything a `medieval` figure had to separate itself from was another archetype
inside its own style; every figure here has to separate itself from its own
seven **and** from the `medieval` figure that stands beside it in the same role
on ``ROSTER.md``. That second constraint is what most of the choices below are
spending, and it is stated per figure rather than in general because it costs a
different thing each time.

Two of the camera findings in :mod:`.rules` do most of the work here:

* **A figure reads by x and z, not by y.** So the trooper's weapon runs
  *sideways* where the knight's sword stands up, the sniper's rail runs level
  across the whole figure where the archer's bow stands beside it, and the
  xenoform's tail climbs where the beast's sweeps down.
* **What separates two masses is the gap between them.** The drone swarm is
  built on it: four detached bodies out past the arms with nothing in between.
  It is also why the medic's cross floats clear of a device that starts at the
  chest, where the healer's rides a staff planted in the ground.

Nothing here is a `medieval` figure recoloured, and rule 4 is why it could not
be: each of these is held to its own sci-fi sprite's measured silhouette, and
those are different numbers. The trooper's sprite is 27 texels wide where the
knight's is 22.
"""

from __future__ import annotations

from typing import Mapping, Tuple

from .rules import RAMP_FACTION, RAMP_NEUTRAL, Part

#: The style these tables are the commission for. It is declared rather than
#: taken from the filename so the registry in :mod:`.` cannot bind a module to
#: the wrong style's sprites, which is the one mistake the silhouette check
#: would not catch: it would simply hold this trooper to a knight's box.
STYLE = "scifi"


# ---------------------------------------------------------------------------
# The trooper
#
# The knight's role in powered armour, and the figure it has to be told apart
# from is the knight. Three things carry that, and all three are the camera's
# axis rather than taste:
#
# * The **carbine runs sideways.** A knight's sword is the tallest thing in its
#   model; this is a bar out to +x at chest height with a lit vent on the end,
#   so the two figures differ in the one direction this camera reads best. It
#   is a plate by the wall test (six units tall against eight deep), and that
#   is what is wanted: a bright top face streaking to one side.
# * The **shield is a rectangle and it hangs low.** The knight's is a mid-height
#   panel at the shoulder; this one runs from the ankle to the chest, seventy
#   units of height against six of depth, which is a wall three times over. It
#   is also the largest faction surface in the figure, which is where a faction
#   ramp belongs.
# * The **visor is lit rather than dark.** The knight's helm is a bright mass
#   with a dark band across it; this is the inverse, a dull helm with the
#   brightest rung of the neutral ramp in a slit across its front. The sprite's
#   whole style rests on that one mark, and it is the only place in either
#   style where the head's brightest pixel is a horizontal line.
#
# Nothing is stacked above the helm, which is the other half of making a head
# read: pauldrons at plus or minus nineteen under a helm at plus or minus nine,
# and nothing over it.
# ---------------------------------------------------------------------------

TROOPER: Tuple[Part, ...] = (
    Part(-13,  -4,   0,   4,  -5,   5, RAMP_NEUTRAL, 0, "left boot"),
    Part(  4,  13,   0,   4,  -5,   5, RAMP_NEUTRAL, 0, "right boot"),
    Part(-12,  -5,   4,  24,  -4,   4, RAMP_NEUTRAL, 1, "left shin"),
    Part(  5,  12,   4,  24,  -4,   4, RAMP_NEUTRAL, 1, "right shin"),
    Part(-12,  -4,  23,  37,  -4,   4, RAMP_NEUTRAL, 1, "left thigh"),
    Part(  4,  12,  23,  37,  -4,   4, RAMP_NEUTRAL, 1, "right thigh"),
    Part(-24, -10,   5,  47, -11,  -7, RAMP_FACTION, 3, "slab shield"),
    Part(-13,  13,  36,  43,  -5,   5, RAMP_FACTION, 1, "hip armour"),
    Part( 13,  17,  42,  55,  -4,   4, RAMP_NEUTRAL, 1, "right arm"),
    Part(-17, -13,  43,  58,  -4,   4, RAMP_NEUTRAL, 1, "left arm"),
    Part(-13,  13,  42,  61,  -5,   5, RAMP_FACTION, 1, "faction suit"),
    Part( 12,  26,  48,  52,  -7,  -2, RAMP_NEUTRAL, 2, "carbine, barrel"),
    Part( 24,  30,  47,  54,  -8,  -2, RAMP_NEUTRAL, 3, "carbine, muzzle vent"),
    Part( -9,   9,  46,  59,  -7,  -5, RAMP_NEUTRAL, 1, "chest plate"),
    Part(-19, -11,  58,  65,  -5,   5, RAMP_FACTION, 2, "left pauldron"),
    Part( 11,  19,  58,  65,  -5,   5, RAMP_FACTION, 2, "right pauldron"),
    Part( -6,   6,  60,  64,  -3,   3, RAMP_NEUTRAL, 0, "gorget"),
    Part( -9,   9,  62,  77,  -5,   5, RAMP_NEUTRAL, 1, "sealed helm"),
    Part( -9,   9,  66,  70,  -7,  -5, RAMP_NEUTRAL, 3, "visor slit"),
)

# ---------------------------------------------------------------------------
# The sniper
#
# The archer's role, and the figure this commission spent the most drafts on,
# because the sprite's read is a **diagonal** (a rail rifle corner to corner),
# and a diagonal is the one line an axis-aligned box cannot draw. Two drafts
# tried to build it as a staircase of walls stepping up and to the right. Both
# passed every rule and both read as rubble: at twenty-five pixels each step is
# four, so a five-step diagonal is five separate blocks and not a stroke.
#
# What is authored instead is the same weapon held **level**, which is the axis
# this camera reads by: a breech and a barrel meeting at the figure's centre,
# six units of height and four of depth each, so the pair is drawn as
# one three-pixel line running the whole width with a lit muzzle at the far
# end, and a brace under the near end. Nothing else in either style has a long
# horizontal at waist height, and the medieval archer is its opposite by
# construction: a tall thin bar standing beside the body.
#
# The rail is authored forward of the body in z, so it sorts near and draws
# over the torso rather than under it. The one thing lost against the sprite is
# the diagonal's slope, and the finding is worth the loss: **a diagonal built
# from boxes is legible only if each box is longer than a step is high**, and
# at this size that leaves room for two, not five.
# ---------------------------------------------------------------------------

SNIPER: Tuple[Part, ...] = (
    Part(-11,  -3,   0,   4,  -4,   4, RAMP_NEUTRAL, 0, "left boot"),
    Part(  3,  11,   0,   4,  -4,   4, RAMP_NEUTRAL, 0, "right boot"),
    Part(-10,  -4,   4,  24,  -3,   3, RAMP_NEUTRAL, 1, "left shin"),
    Part(  4,  10,   4,  24,  -3,   3, RAMP_NEUTRAL, 1, "right shin"),
    Part(-10,  -3,  23,  38,  -4,   4, RAMP_NEUTRAL, 0, "left thigh"),
    Part(  3,  10,  23,  38,  -4,   4, RAMP_NEUTRAL, 0, "right thigh"),
    Part(-24, -18,  19,  42, -11,  -9, RAMP_NEUTRAL, 1, "rail, brace"),
    Part(-11,  11,  37,  61,  -4,   4, RAMP_FACTION, 3, "torso"),
    Part(-16, -11,  42,  57,  -4,   4, RAMP_FACTION, 1, "left arm"),
    Part(-24,   2,  42,  46, -11,  -9, RAMP_NEUTRAL, 1, "rail, breech"),
    Part( 11,  16,  44,  60,  -4,   4, RAMP_FACTION, 1, "right arm"),
    Part( -2,  27,  46,  50, -11,  -9, RAMP_NEUTRAL, 2, "rail, barrel"),
    Part( 26,  32,  44,  52, -13,  -8, RAMP_NEUTRAL, 3, "rail, muzzle"),
    Part( -6,   6,  60,  70,  -4,   4, RAMP_NEUTRAL, 2, "head"),
    Part( -8,   8,  64,  75,  -4,   5, RAMP_FACTION, 2, "cowl"),
    Part(  0,   8,  66,  70,  -6,  -4, RAMP_NEUTRAL, 3, "targeting monocle"),
    Part( -4,   4,  71,  81,  -1,   6, RAMP_FACTION, 2, "cowl point"),
)

# ---------------------------------------------------------------------------
# The psion
#
# The mage's role, and the figure that shows most sharply why rule 4 constrains
# one number while a figure has a shape. Both halves of it are things a mesh
# author will meet again.
#
# **A sprite drawn small makes a mesh small, and the fix belongs to the
# sprite.** A sci-fi mage at 294 opaque texels against its seven siblings' mean
# of 474, in an opaque box 18 texels wide where the rest of the style is 21 to
# 30 (and that 18 only because the shared faction disc under the feet is 18
# wide), earns a silhouette target of 36 world units where the trooper earns
# 54, and a faithful model at 34. Correcting that from a mesh commission is the
# wrong place to correct it: it moves a drawing nobody asked to change, and
# moves the mesh's own target with it. This sprite carries its own answer
# instead, drawn at its style's scale: a bell flaring to the widest row of the
# figure, arms carried **out and down** with the hands open past the bell, the
# narrowness carried by the robe's taper and the gap under its hem rather than
# by being small.
#
# **A uniform scale is not a re-model.** Rule 2 asks this figure for 48 world
# units, and reaching that by scaling every part's x from a narrower drawing
# meets the width while leaving the *widest* part of the figure at the head:
# hands drawn at chest height scale where they stand, and this sprite's widest
# row is at the hem. So the sleeves and the hands are authored here where the
# drawing puts them (down at 46 to 66 rather than up at 80 to 88), and the
# hands are held outboard of the sleeves, because a hand tucked inside a sleeve
# is drawn over by it and the outermost thing in this figure is what the
# silhouette is measured on.
#
# What the figure spends, inside that:
#
# * It **floats**, which no other unit does. Rule 1 still requires something at
#   y = 0, so what is there is the hover flare rather than a foot: two boxes,
#   the lower one on the brightest neutral rung, tapering up into the robe.
# * The **focus shard** is detached above the head with a dark thread through
#   the gap, which is the recipe for making a head read used on the part
#   above it instead of the part below. Both shard boxes are walls, not plates,
#   for the reason the refused mage's hat was refused, and the shard is held at
#   the eight world units the sprite's own top rows measure rather than at the
#   ten a uniform scale would give it.
# ---------------------------------------------------------------------------

PSION: Tuple[Part, ...] = (
    Part( -8,   8,   0,   9,  -5,   5, RAMP_NEUTRAL, 3, "hover flare"),
    Part( -6,   6,   8,  19,  -3,   3, RAMP_NEUTRAL, 2, "hover column"),
    Part(-11,  11,  17,  33,  -6,   6, RAMP_FACTION, 1, "robe, point"),
    Part(-17,  17,  32,  51,  -7,   7, RAMP_FACTION, 2, "robe, skirt"),
    Part(-24, -18,  36,  44,  -6,   0, RAMP_NEUTRAL, 2, "left hand, open"),
    Part( 18,  24,  36,  44,  -6,   0, RAMP_NEUTRAL, 2, "right hand, open"),
    Part(-18, -10,  41,  52,  -3,   3, RAMP_FACTION, 2, "left sleeve, out and down"),
    Part( 10,  18,  41,  52,  -3,   3, RAMP_FACTION, 2, "right sleeve, out and down"),
    Part(-14,  14,  49,  68,  -6,   6, RAMP_FACTION, 3, "torso"),
    Part( -6,   6,  57,  63,  -8,  -6, RAMP_NEUTRAL, 3, "chest emitter"),
    Part(-10,  10,  63,  76,  -5,   5, RAMP_NEUTRAL, 1, "head"),
    Part(-10,  10,  71,  76,  -6,   2, RAMP_NEUTRAL, 3, "implant band"),
    Part( -3,   3,  76,  88,  -2,   2, RAMP_NEUTRAL, 0, "field thread"),
    Part( -8,   8,  87,  98,  -3,   3, RAMP_NEUTRAL, 3, "focus shard"),
    Part( -4,   4,  93, 101,  -2,   2, RAMP_NEUTRAL, 3, "focus shard, tip"),
)

# ---------------------------------------------------------------------------
# The drone swarm
#
# The stormcaller's role, and the figure the finding about gaps was waiting
# for: *what separates two masses at this size is the background between them.*
# The sprite is the only silhouette in the roster that is not connected, and
# the model keeps that literally: four bodies out past the arms with four
# world units of nothing between the nearest one and the body it left.
#
# Where the stormcaller throws bolts that taper away from the hands, these are
# solid, level, and paired at two reaches: the near pair on the brightest
# neutral rung at shoulder height, the far pair dimmer and higher. Both pairs
# are plates by the wall test, and here that is right: a drone seen from a
# sixty-degree pitch should be a lit horizontal disc, not a panel.
#
# **The arms come down onto a control slab**, and that is the sprite followed
# rather than a choice made here. Both arms thrown out with a fourteen-texel
# rig at the chest is the raised-arm rig four styles share, so this handler
# works a slab at the waist with both hands down, the way three other
# commissions in the role do. The drones are untouched by that, because the gap
# out at the frame edge is this figure's read and no pose of the arms carries
# it, which is why the arms are the only part of the model the pose decides.
#
# The slab is a plate, at fourteen units of height against eight of depth, and
# that is what a panel carried flat in two hands should be. Its lit strip is
# **not** a second plate on top of it: authored there it is drawn under the
# torso, whose own front face starts two units of screen higher and is drawn
# later. It is a strip proud of the slab's *front* instead, two units of z
# ahead of everything else in the figure, so nothing can be drawn over it. That
# is the same construction the visor slit already uses on the helm.
#
# The **antenna array** is the only thing in this style that climbs, and it is
# allowed to because it is three thin rods rather than a mass: eighteen units
# tall against six deep is a wall each, spread across twenty-two units of x, so
# what the camera gets is a comb and not a staircase. The shroud takes the
# stormcaller's trick unchanged: a large faction slab kept *below* the
# shoulders, at z 8 to 16, because a slab any higher would be drawn above the
# head rather than behind the back.
# ---------------------------------------------------------------------------

DRONE_SWARM: Tuple[Part, ...] = (
    Part(-14,  14,   0,  15,  -7,   7, RAMP_FACTION, 0, "robe hem"),
    Part(-16,  16,   3,  48,   6,  12, RAMP_FACTION, 1, "shroud"),
    Part(-12,  12,  13,  38,  -6,   6, RAMP_FACTION, 2, "robe"),
    Part(-14,  14,  32,  39, -12,  -6, RAMP_NEUTRAL, 2, "control slab"),
    Part( -9,   9,  33,  37, -13, -12, RAMP_NEUTRAL, 3, "control slab, lit strip"),
    Part(-10,  10,  36,  58,  -6,   6, RAMP_FACTION, 3, "torso"),
    Part(-16, -10,  36,  54,  -9,  -3, RAMP_FACTION, 1, "left arm, down to the slab"),
    Part( 10,  16,  36,  54,  -9,  -3, RAMP_FACTION, 1, "right arm, down to the slab"),
    Part(-29, -19,  46,  55,  -7,   1, RAMP_NEUTRAL, 3, "drone, near left"),
    Part( 19,  29,  46,  55,  -7,   1, RAMP_NEUTRAL, 3, "drone, near right"),
    Part( -8,   8,  57,  70,  -5,   5, RAMP_NEUTRAL, 1, "head"),
    Part( -8,   8,  61,  65,  -7,  -6, RAMP_NEUTRAL, 3, "visor slit"),
    Part(-30, -20,  70,  78,  -3,   6, RAMP_NEUTRAL, 2, "drone, far left"),
    Part( 20,  30,  70,  78,  -3,   6, RAMP_NEUTRAL, 2, "drone, far right"),
    Part(-12,  12,  73,  78,  -3,   3, RAMP_FACTION, 3, "antenna bar"),
    Part(-11,  -7,  77,  90,  -2,   2, RAMP_NEUTRAL, 2, "antenna rod, left"),
    Part(  7,  11,  77,  90,  -2,   2, RAMP_NEUTRAL, 2, "antenna rod, right"),
    Part( -2,   2,  77,  93,  -2,   2, RAMP_NEUTRAL, 2, "antenna rod, centre"),
)

# ---------------------------------------------------------------------------
# The medic
#
# The healer's role, and the hardest separation in this commission, because
# both figures are a pale mass with a cross over one shoulder and the cross is
# the read in each. Three differences, and the first two are silhouette rather
# than detail:
#
# * The healer has **no legs**: a robe to the ground. This one does. A leg gap
#   under a wide dome is a different outline before anything above the waist is
#   drawn.
# * The healer's cross rides a **staff planted in the ground**; this one is on
#   an injector that starts at the chest, so there is a hole in the silhouette
#   under it where the healer has a line. That hole is the separation, and it
#   is the gap rule spent on a negative space rather than on a part.
# * The cross itself **lies flat**, in x and z, which is the healer's finding
#   reused rather than reinvented: the plane this camera keeps most of is the
#   horizontal one, so a cross authored upright is drawn a bar. Two boxes at
#   the same height, one long in x and one long in z, are drawn a cross.
#
# The suit is the brightest neutral rung over the whole body, which is what the
# sprite's ceramic white is, so the faction ramp has to go somewhere that is
# not mass: a stole the full height of the front, a yoke across the shoulders,
# and a band at the brow.
# ---------------------------------------------------------------------------

MEDIC: Tuple[Part, ...] = (
    Part(-11,  -3,   0,   4,  -5,   5, RAMP_NEUTRAL, 0, "left boot"),
    Part(  3,  11,   0,   4,  -5,   5, RAMP_NEUTRAL, 0, "right boot"),
    Part(-10,  -4,   4,  26,  -4,   4, RAMP_NEUTRAL, 3, "left shin"),
    Part(  4,  10,   4,  26,  -4,   4, RAMP_NEUTRAL, 3, "right shin"),
    Part(-13,  13,  25,  46,  -7,   7, RAMP_NEUTRAL, 3, "suit, skirt"),
    Part( -6,   6,  25,  46,  -8,  -7, RAMP_FACTION, 3, "stole, skirt"),
    Part(-19, -15,  47,  65,  -4,   4, RAMP_NEUTRAL, 3, "left arm"),
    Part(-15,  15,  44,  71,  -8,   8, RAMP_NEUTRAL, 3, "suit, dome"),
    Part( 15,  19,  50,  69,  -4,   4, RAMP_NEUTRAL, 3, "right arm"),
    Part( -6,   6,  44,  71, -10,  -8, RAMP_FACTION, 3, "stole, chest"),
    Part(-15,  15,  68,  73,  -7,   2, RAMP_FACTION, 2, "shoulder yoke"),
    Part( -7,   7,  71,  82,  -4,   4, RAMP_NEUTRAL, 2, "head"),
    Part( 16,  22,  63,  88,  -4,  -1, RAMP_NEUTRAL, 1, "injector"),
    Part( -8,   8,  73,  78,  -6,  -4, RAMP_NEUTRAL, 0, "visor recess"),
    Part( -8,   8,  79,  83,  -5,   1, RAMP_FACTION, 2, "brow band"),
    Part( 10,  26,  88,  94,  -6,  -1, RAMP_NEUTRAL, 3, "cross, bar"),
    Part( 16,  22,  88,  94, -12,   3, RAMP_NEUTRAL, 3, "cross, stem"),
)

# ---------------------------------------------------------------------------
# The captain
#
# The commander's role, and rank stated the way the sprite states it: mass, a
# silhouette that breaks the top of the frame, and detail. What separates it
# from the medieval commander is that its standard is **planted**. The
# commander's pole starts at the hip and carries a cloth that spends
# twenty-two units in z; this mast runs from y = 0 to the built height on one
# side of the figure, with an element bracketed off it, and the pennant it
# carries is small and high.
#
# A full-height vertical at one side is a thing the rules would normally
# refuse, being a stack in the one axis this camera flattens. It survives for
# the reason the archer's bow does: it is six units wide against a hundred and
# sixteen tall, so it is a line rather than a mass, and a line is drawn a line
# at any pitch. It is also authored *behind* the shoulder at z 4 to 10, which
# is where the commander's pole had to move before its tip stopped landing
# below the plume.
#
# The drape is the commander's cape held under the shoulders for the same
# reason the drone swarm's shroud is, and the sidearm with its lit vent is on
# the far side from the mast so the figure carries a diagonal rather than a
# stack. At twenty-four parts this is the largest model in either style and it
# is near the top of the band on purpose: the mast, its element, its pennant
# and its tip are four parts spent on one device, and the figure would not read
# as a leader without all four.
# ---------------------------------------------------------------------------

CAPTAIN: Tuple[Part, ...] = (
    Part(-11,  -3,   0,   4,  -5,   5, RAMP_NEUTRAL, 0, "left boot"),
    Part(  3,  11,   0,   4,  -5,   5, RAMP_NEUTRAL, 0, "right boot"),
    Part(-10,  -4,   4,  26,  -4,   4, RAMP_NEUTRAL, 1, "left shin"),
    Part(  4,  10,   4,  26,  -4,   4, RAMP_NEUTRAL, 1, "right shin"),
    Part(-15,  15,   3,  61,   6,  11, RAMP_FACTION, 1, "command drape"),
    Part(-10,  -3,  25,  42,  -5,   5, RAMP_NEUTRAL, 2, "left thigh"),
    Part(  3,  10,  25,  42,  -5,   5, RAMP_NEUTRAL, 2, "right thigh"),
    Part( 17,  29,  36,  40,   1,   8, RAMP_NEUTRAL, 2, "array element"),
    Part( 20,  26,   0,  79,   3,   7, RAMP_NEUTRAL, 2, "comms mast"),
    Part(-12,  12,  40,  52,  -6,   6, RAMP_FACTION, 2, "hip armour"),
    Part(-17, -12,  50,  65,  -4,   4, RAMP_NEUTRAL, 2, "left arm"),
    Part( 12,  17,  50,  65,  -4,   4, RAMP_NEUTRAL, 2, "right arm"),
    Part(-12,  12,  50,  70,  -7,   7, RAMP_FACTION, 3, "cuirass"),
    Part(-22, -15,  54,  60,  -8,  -3, RAMP_NEUTRAL, 1, "sidearm"),
    Part( -8,   8,  56,  60,  -8,  -7, RAMP_NEUTRAL, 3, "rank bars"),
    Part(-24, -18,  56,  63, -10,  -4, RAMP_NEUTRAL, 3, "sidearm, muzzle vent"),
    Part( 26,  30,  64,  78,   0,  11, RAMP_FACTION, 3, "mast pennant"),
    Part(-19, -11,  65,  74,  -6,   6, RAMP_FACTION, 2, "left pauldron"),
    Part( 11,  19,  65,  74,  -6,   6, RAMP_FACTION, 2, "right pauldron"),
    Part( -9,   9,  71,  82,  -6,   6, RAMP_NEUTRAL, 2, "open helm"),
    Part( -9,   9,  74,  78,  -8,  -6, RAMP_NEUTRAL, 0, "face recess"),
    Part( 21,  25,  77,  89,   3,   7, RAMP_NEUTRAL, 3, "mast tip"),
    Part( -9,   9,  79,  84,  -6,   6, RAMP_NEUTRAL, 3, "circlet"),
    Part( -3,   3,  81,  89,  -1,   8, RAMP_FACTION, 3, "plume"),
)

# ---------------------------------------------------------------------------
# The infiltrator
#
# The rogue's role, and the second-hardest separation, because the medieval
# rogue is also a crouched figure with two blades and its sprite's silhouette
# asks for almost the same width: 56 world units against 54. So the two cannot
# be told apart by the size of their outlines, and the ten drafts the rogue
# cost had already spent the obvious answer: a stance twice the knight's width.
#
# This one goes the other way, and that is the finding. **The rogue is wide in
# x and this is deep in z.** Its legs come in to plus or minus fifteen where
# the rogue's go out to plus or minus twenty-six, and the width it is held to
# is bought entirely by the two blades: one long bar low on the far side and
# one high on the near, each sixteen units of x and sixteen of z, thrown
# forward of the whole body. A rogue's four blade boxes are short streaks hung
# off a wide figure; these are two long ones that *are* the figure's width. The
# outlines differ as a result: the rogue is a triangle, this is a narrow column
# with a bright bar across each end of a diagonal.
#
# The shroud is on the darkest neutral rung and kept below the shoulders, so it
# widens the footprint from above without haloing the hood, which is the thing
# the rogue could not have at all, and the only reason this figure can is that
# it is authored low. The hood is the rogue's cavity kept unchanged, because it
# is a property of the camera rather than of a genre: a dark crown behind a
# brighter rim, with the eye slits as the one lit mark.
# ---------------------------------------------------------------------------

INFILTRATOR: Tuple[Part, ...] = (
    Part(-15,  -6,   0,   6,  -8,   0, RAMP_NEUTRAL, 0, "left boot"),
    Part(  6,  15,   0,   6,  -8,   0, RAMP_NEUTRAL, 0, "right boot"),
    Part(-14,  -7,   6,  24,  -8,  -1, RAMP_NEUTRAL, 1, "left shin"),
    Part(  7,  14,   6,  24,  -8,  -1, RAMP_NEUTRAL, 1, "right shin"),
    Part(-14,  14,   3,  45,   6,  10, RAMP_NEUTRAL, 0, "camouflage shroud"),
    Part(-28,  -8,  21,  27, -15, -11, RAMP_NEUTRAL, 3, "energy blade, low"),
    Part(-13,  -5,  22,  41,  -7,   1, RAMP_NEUTRAL, 1, "left thigh"),
    Part(  5,  13,  22,  41,  -7,   1, RAMP_NEUTRAL, 1, "right thigh"),
    Part(-12,  12,  39,  50, -10,   0, RAMP_FACTION, 1, "harness"),
    Part(-18, -12,  46,  62, -13,  -4, RAMP_NEUTRAL, 1, "left arm"),
    Part(-12,  12,  49,  67, -10,   0, RAMP_FACTION, 3, "torso"),
    Part( 12,  18,  52,  67, -13,  -4, RAMP_NEUTRAL, 1, "right arm"),
    Part(  8,  28,  60,  66, -15, -11, RAMP_NEUTRAL, 3, "energy blade, high"),
    Part(-10,  10,  66,  71, -11,  -4, RAMP_FACTION, 3, "throat plate"),
    Part(-15,  15,  67,  73, -12,   -2, RAMP_NEUTRAL, 1, "mantle"),
    Part( -9,   9,  70,  85, -14,  -6, RAMP_NEUTRAL, 0, "hood"),
    Part( -7,   7,  74,  78, -15, -14, RAMP_NEUTRAL, 3, "eye slits"),
    Part( -7,   7,  83,  90, -13,  -7, RAMP_NEUTRAL, 0, "hood crown"),
)

# ---------------------------------------------------------------------------
# The xenoform
#
# The beast's role, and the easiest figure here for the reason the beast was
# the easiest there: it is long in x, and nothing else in either style is. It
# starts legible and the work is separating it from the *other* quadruped.
#
# Two things do that, and both are placed in the axis the camera reads:
#
# * The **tail climbs.** The beast's sweeps out and down from the rump because
#   a tail level with the back reads as one more plate on it; this one curls up
#   over the body in three segments (each a wall, eighteen to twenty units tall
#   against ten deep) and tops the whole figure with a lit stinger. So the
#   xenoform is the only animal in the library with something above its own
#   back, and at twenty pixels that is the entire difference.
# * **Six legs, three a side**, spread along x rather than paired at the ends.
#   Four legs and a slab is furniture; six in a row under a body that runs from
#   x -18 to +20 is an arthropod. The three carapace plates keep the beast's
#   own lesson (separate plates with real gaps, not one ridge), and the gaps
#   had to be widened to five units before they stopped drawing as one slab.
#
# The mandibles are thrown forward in -z where the beast's muzzle is, which is
# the face the camera sees, and the faction ramp rides the three carapace
# plates and the neck plate for the beast's reason exactly: they are small, but
# they are where a viewer is already looking, and chitin has to stay chitin.
# ---------------------------------------------------------------------------

XENOFORM: Tuple[Part, ...] = (
    Part(-16,  -9,   0,  26,   4,   8, RAMP_NEUTRAL, 0, "rear leg, far"),
    Part( -3,   4,   0,  26,   4,   8, RAMP_NEUTRAL, 0, "middle leg, far"),
    Part( 10,  17,   0,  26,   4,   8, RAMP_NEUTRAL, 0, "front leg, far"),
    Part(-16,  -9,   0,  26, -11,  -6, RAMP_NEUTRAL, 1, "rear leg, near"),
    Part( -3,   4,   0,  26, -11,  -6, RAMP_NEUTRAL, 1, "middle leg, near"),
    Part( 10,  17,   0,  26, -11,  -6, RAMP_NEUTRAL, 1, "front leg, near"),
    Part(-18,  20,  24,  45,  -9,   7, RAMP_NEUTRAL, 1, "carapace body"),
    Part( 28,  35,  30,  34, -11,  -5, RAMP_NEUTRAL, 1, "mandible, lower"),
    Part( 21,  31,  31,  46,  -7,   2, RAMP_NEUTRAL, 2, "head"),
    Part( 15,  21,  34,  47,  -6,   1, RAMP_FACTION, 2, "neck plate"),
    Part(-22, -12,  37,  47,  -4,   2, RAMP_NEUTRAL, 1, "tail, root"),
    Part( 28,  35,  37,  41, -11,  -5, RAMP_NEUTRAL, 1, "mandible, upper"),
    Part( 23,  31,  39,  44,  -9,  -7, RAMP_NEUTRAL, 3, "eye cluster"),
    Part(-17,  -9,  45,  54,  -4,   2, RAMP_FACTION, 3, "carapace plate, rear"),
    Part( -4,   4,  45,  54,  -4,   2, RAMP_FACTION, 3, "carapace plate, middle"),
    Part(  9,  17,  45,  54,  -4,   2, RAMP_FACTION, 3, "carapace plate, front"),
    Part(-27, -19,  46,  58,  -4,   2, RAMP_NEUTRAL, 1, "tail, middle"),
    Part(-28, -20,  55,  67,  -4,   2, RAMP_NEUTRAL, 1, "tail, upper"),
    Part(-27, -19,  65,  76,  -5,   1, RAMP_NEUTRAL, 3, "stinger"),
)

#: This style's commissioned meshes, by archetype name. Complete: all eight
#: roles, drawn from the eight sci-fi sprites and held to their silhouettes.
MESHES: Mapping[str, Tuple[Part, ...]] = {
    "knight": TROOPER,
    "archer": SNIPER,
    "mage": PSION,
    "stormcaller": DRONE_SWARM,
    "healer": MEDIC,
    "commander": CAPTAIN,
    "rogue": INFILTRATOR,
    "beast": XENOFORM,
}
