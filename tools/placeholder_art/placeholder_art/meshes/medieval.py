# SPDX-License-Identifier: MIT
"""The ``medieval`` mesh commission: the eight archetypes as solids.

The style :mod:`..characters` draws, drawn again with depth. Every figure here
is held to its own ``medieval`` sprite's measured silhouette, so this table and
that drawing move together and neither can be read without the other.

Each table below carries the reasoning that produced it, because the camera
findings in :mod:`.rules` were written out of these eight and a reader who wants
to know *why* a box is where it is should not have to go and find them. The
findings in short, in the order they were paid for: a feature
stacked in y is a staircase of bright top faces (the refused mage), screen
height is bought in z at ``tan φ`` to one (the stormcaller and the commander), a
planar emblem belongs in the horizontal plane and a legless robe needs something
that runs vertically (the robed pair), and horizontal beats standing while a
crouch buys nothing (the rogue and the beast).
"""

from __future__ import annotations

from typing import Mapping, Tuple

from .rules import RAMP_FACTION, RAMP_NEUTRAL, Part

#: The style these tables are the commission for. It is declared rather than
#: taken from the filename so the registry in :mod:`.` cannot bind a module to
#: the wrong style's sprites, which is the one mistake the silhouette check
#: would not catch: it would simply hold this knight to a badger's box.
STYLE = "medieval"


# ---------------------------------------------------------------------------
# The knight
#
# Modelled first, and defended: it is entry zero of
# `characters.ARCHETYPE_CLASSES`, the first of the six roles the sample
# campaign expects, and the melee unit a tactics board is mostly made of. It is
# also the *hardest* case, because the art contract is silhouette-first and a
# knight's silhouette is the least distinctive in the roster. A mage's hat, an
# archer's bow and a beast's four legs all read at eight pixels, and a knight
# is a rectangle with a head.
#
# The order below was derived from `depth_key` and not from anatomy: feet come
# before shins, shins before thighs, and the crest on the helm is the very last
# thing drawn.
# ---------------------------------------------------------------------------

KNIGHT: Tuple[Part, ...] = (
    Part(-13,  -3,   0,   5,  -7,   7, RAMP_NEUTRAL, 0, "left boot"),
    Part(  3,  13,   0,   5,  -7,   7, RAMP_NEUTRAL, 0, "right boot"),
    Part(-12,  -4,   5,  37,  -5,   5, RAMP_NEUTRAL, 1, "left shin"),
    Part(  4,  12,   5,  37,  -5,   5, RAMP_NEUTRAL, 1, "right shin"),
    Part(-12,  -3,  37,  60,  -6,   6, RAMP_FACTION, 0, "left thigh"),
    Part(  3,  12,  37,  60,  -6,   6, RAMP_FACTION, 0, "right thigh"),
    Part(-13,  13,  60,  71,  -8,   8, RAMP_NEUTRAL, 0, "belt"),
    Part( 14,  22,  57,  65, -12,  -6, RAMP_NEUTRAL, 1, "sword hilt"),
    Part(-18, -11,  69,  95,  -6,   6, RAMP_NEUTRAL, 2, "left arm"),
    Part( 11,  18,  69,  95,  -6,   6, RAMP_NEUTRAL, 2, "right arm"),
    Part(-11,  11,  71,  97,  -7,   7, RAMP_FACTION, 3, "tabard"),
    Part(-23,  -9,  55, 101, -16,  -9, RAMP_FACTION, 2, "shield"),
    Part( 16,  20,  57, 126, -12,  -8, RAMP_NEUTRAL, 3, "sword blade"),
    Part(-19, -10,  91, 105,  -8,   8, RAMP_FACTION, 2, "left pauldron"),
    Part( 10,  19,  91, 105,  -8,   8, RAMP_FACTION, 2, "right pauldron"),
    Part( -4,   4,  95, 102,  -4,   4, RAMP_NEUTRAL, 0, "gorget"),
    Part( -6,   6, 101, 112,  -6,   6, RAMP_NEUTRAL, 1, "head"),
    Part( -8,   8, 105, 119,  -8,   8, RAMP_NEUTRAL, 3, "helm"),
    Part( -8,   8, 107, 113, -10,  -8, RAMP_NEUTRAL, 0, "visor"),
    Part( -2,   2, 117, 128,  -7,   3, RAMP_FACTION, 3, "crest"),
)

# ---------------------------------------------------------------------------
# The archer
#
# The second archetype, and chosen because its silhouette
# is the knight's opposite: where a knight is a rectangle with a head, an archer
# is a figure holding a tall thin arc. The bow is what the silhouette rule says
# has to survive, so it is the tallest thing in the model: two thin limbs and a
# grip, offset toward the viewer the way the knight's sword is. The quiver sits
# behind the shoulder for the same reason.
#
# The measurement of it is worth keeping beside the table, because it is a
# shape difference and not a defect: the archer *sprite* paints its bow as a
# filled arc where the model's bow is honestly two thin limbs, so the model's
# drawn mass runs lighter than the sprite's at the middle and far rows inside a
# silhouette that matches on both axes.
# ---------------------------------------------------------------------------

ARCHER: Tuple[Part, ...] = (
    Part(-11,  -3,   0,   5,  -6,   6, RAMP_NEUTRAL, 0, "left boot"),
    Part(  3,  11,   0,   5,  -6,   6, RAMP_NEUTRAL, 0, "right boot"),
    Part(-10,  -4,   5,  37,  -4,   4, RAMP_NEUTRAL, 1, "left shin"),
    Part(  4,  10,   5,  37,  -4,   4, RAMP_NEUTRAL, 1, "right shin"),
    Part(-10,  -3,  37,  58,  -5,   5, RAMP_FACTION, 0, "left thigh"),
    Part(  3,  10,  37,  58,  -5,   5, RAMP_FACTION, 0, "right thigh"),
    Part(-20, -15,  28,  78,  -8,  -4, RAMP_NEUTRAL, 3, "bow, lower limb"),
    Part(-11,  11,  58,  72,  -7,   7, RAMP_FACTION, 1, "tunic skirt"),
    Part(  5,  12,  70, 100,   7,  14, RAMP_NEUTRAL, 1, "quiver"),
    Part( 10,  17,  70,  94,  -6,   6, RAMP_NEUTRAL, 2, "right arm"),
    Part(-17, -10,  70,  96,  -6,   6, RAMP_NEUTRAL, 2, "left arm, on the bow"),
    Part(-10,  10,  70,  98,  -6,   6, RAMP_FACTION, 3, "torso"),
    Part(-21, -14,  78,  92,  -9,  -3, RAMP_NEUTRAL, 1, "bow, grip"),
    Part( -5,   5, 100, 111,  -5,   5, RAMP_NEUTRAL, 1, "head"),
    Part( -7,   7, 104, 118,  -7,   7, RAMP_FACTION, 2, "hood"),
    Part(-20, -15,  92, 128,  -8,  -4, RAMP_NEUTRAL, 3, "bow, upper limb"),
    Part( -3,   3, 114, 124,   1,   7, RAMP_FACTION, 2, "hood point"),
)

# ---------------------------------------------------------------------------
# The mage
#
# The archetype the wall-and-plate test was learned from. An
# earlier draft passed every rule here and was still not shipped, because at
# this pitch a pointed hat built as a stack of shrinking boxes is a ziggurat.
# Three measurements, not a fourth draft, are what changed:
#
# 1. **A part is a wall when its height beats `tan φ` times its depth, and a
#    plate when it does not.** The camera's vertical axis takes `cos φ` of a
#    world y and `sin φ` of a world z, so a box deeper than 0.58 of its own
#    height shows more bright top face than front face. Every box of the
#    rejected hat was a plate by that test, which is the whole of why it read as
#    a staircase. Its widths tapered perfectly.
# 2. **Depth costs `tan φ` times what height costs.** The silhouette match
#    squeezes y and z together until the drawn box is the sprite's, so a unit of
#    z spends 1.73 units of the figure's whole height budget. This figure is
#    held inside ten units either side of centre, and the hat gets the room the
#    rejected draft had spent on being deep.
# 3. **The brim is a shelf.** The sprite's hat is no wider than its robe: both
#    are fourteen texels of a twenty-texel silhouette. So the mesh's is no
#    wider than its sleeves, and what identifies the hat is the taper above it:
#    thirty units wide, then twenty, then twelve, then four.
#
# And because a robe has no legs it has no gap, so the front placket carries a
# rung the flanks do not: a legless figure banded only horizontally is a stack
# of slabs whatever is on top of it.
# ---------------------------------------------------------------------------

MAGE: Tuple[Part, ...] = (
    Part(-16,  16,   0,   8,  -9,   9, RAMP_NEUTRAL, 0, "hem shadow"),
    Part(-14,  14,   7,  48,  -8,   8, RAMP_FACTION, 1, "robe, hem"),
    Part( -5,   5,   7,  48, -10,  -8, RAMP_FACTION, 3, "robe, front fold"),
    Part( 17,  22,  10,  64,  -9,  -5, RAMP_NEUTRAL, 1, "staff, lower"),
    Part(-11,  11,  46,  78,  -7,   7, RAMP_FACTION, 2, "robe, body"),
    Part( -5,   5,  46,  78,  -9,  -7, RAMP_FACTION, 3, "robe, front placket"),
    Part(-15, -11,  54,  80,  -6,   6, RAMP_FACTION, 1, "left sleeve"),
    Part( 11,  15,  54,  80,  -7,   5, RAMP_FACTION, 1, "right sleeve"),
    Part( 12,  17,  62,  72, -10,  -4, RAMP_NEUTRAL, 2, "hand on the staff"),
    Part( -6,   6,  76,  92,  -6,   6, RAMP_NEUTRAL, 2, "head"),
    Part( 17,  22,  64, 102,  -9,  -5, RAMP_NEUTRAL, 1, "staff, upper"),
    Part(-15,  15,  90,  98,  -8,   2, RAMP_FACTION, 2, "hat, brim"),
    Part(-10,  10,  96, 110,  -6,   3, RAMP_FACTION, 3, "hat, crown"),
    Part( 16,  24, 100, 109, -10,  -3, RAMP_NEUTRAL, 3, "orb"),
    Part( -6,   6, 108, 120,  -5,   2, RAMP_FACTION, 3, "hat, upper cone"),
    Part( -2,   2, 118, 128,  -4,   1, RAMP_FACTION, 3, "hat, point"),
)

# ---------------------------------------------------------------------------
# The healer
#
# The mage's sibling and the second robed figure: no shank below the hem, so it
# cannot borrow the knight's legs either. What identifies it is a **flat glyph**,
# and that is the third measurement of the pair, stated the way an author needs
# it: the plane this camera reads is the *horizontal* one. A top face keeps
# `sin φ` of its area and a front face `cos φ`, so an upright cross is drawn a
# bar and a cross lying flat is drawn a cross. The finial here therefore lies
# in x and z, the axis-aligned form of a standard's head carried tilted back
# toward the viewer, and it is legible as a cross at twenty-six pixels.
#
# The rest is contrast rather than shape. The robe wears the bright end of the
# neutral ramp where the mage wears its faction ramp, so the two read apart at a
# glance before either one's silhouette is resolved; the faction ramp goes on a
# stole the full height of the robe and a mantle across the shoulders, which are
# the two largest contiguous surfaces this figure has.
#
# One ordering trap is worth naming, because it cost a face. Depth falls with
# height by `sin φ`, so parts at shoulder height sort *near* and a mantle
# authored after the head draws over it, leaving the figure faceless.
# Shoulders, head and wimple ascend here for that reason and not for anatomy's.
# ---------------------------------------------------------------------------

HEALER: Tuple[Part, ...] = (
    Part(-16,  16,   0,   8,  -9,   9, RAMP_NEUTRAL, 0, "hem shadow"),
    Part(-14,  14,   7,  50,  -8,   8, RAMP_NEUTRAL, 3, "robe, hem"),
    Part( -5,   5,   7,  50, -10,  -8, RAMP_FACTION, 3, "stole, skirt"),
    Part(-11,  11,  48,  82,  -7,   7, RAMP_NEUTRAL, 3, "robe, body"),
    Part( 15,  20,  14, 112,  -6,  -2, RAMP_NEUTRAL, 1, "staff"),
    Part( -5,   5,  48,  82,  -9,  -7, RAMP_FACTION, 3, "stole, chest"),
    Part(-15, -11,  56,  84,  -6,   6, RAMP_NEUTRAL, 3, "left sleeve"),
    Part( 11,  15,  56,  84,  -7,   5, RAMP_NEUTRAL, 3, "right sleeve"),
    Part(-13,  13,  82,  92,  -8,   2, RAMP_FACTION, 3, "mantle"),
    Part( -6,   6,  92, 108,  -6,   6, RAMP_NEUTRAL, 2, "head"),
    Part( -7,   7, 102, 118,  -5,   7, RAMP_NEUTRAL, 3, "wimple"),
    Part(  8,  27, 112, 121,  -6,  -2, RAMP_NEUTRAL, 2, "cross, arms"),
    Part( 15,  20, 112, 121, -14,   6, RAMP_NEUTRAL, 2, "cross, stem"),
    Part( 15,  20, 119, 128,  -6,  -2, RAMP_NEUTRAL, 3, "cross, head"),
)

# ---------------------------------------------------------------------------
# The stormcaller
#
# The first of the two archetypes whose *sprite* carries its identity upward,
# which is the one direction this camera does not show. It is also the figure
# that shows most plainly what a sprite and a mesh owe each other, which is the
# half of it an author will meet again.
#
# What the sprite is. Four styles drawing this role as the same arms-raised
# figure separate it from nothing, and the answer three other commissions in the
# role take is *take the width from an object and put the arms down*. So the
# drawing is a figure working a **storm brazier**: a wide iron bowl on three
# legs, both hands low on the rim, fire standing over the coals, smoke drifting
# into one top corner, and the antlered crown kept, because at the reduction the
# crown is the only thing that still separates this from the mage. It is widest
# at the *waist*, which nothing else in this style is.
#
# **A redrawn sprite is a re-model, and only looking says so.** A mesh of raised
# arms, four thrown bolts and an antlered crown is a different figure entirely
# from the one above, and nothing mechanical refuses it: a silhouette width, a
# triangle band, a build height and a far-to-near order are none of them a
# likeness check.
#
# The four numbers this figure is built on, all of them the camera's:
#
# 1. **A part is a wall when `dy > dz·tan φ`, and a plate when it is not.** The
#    bowl's rim is the one part in either commission where being a plate is the
#    *point*: 66 units of x and 8 of z at 4 of height, so it draws as a bright
#    horizontal bar and almost no front face, which is exactly what the sprite's
#    rim row is. Everything that must not be a plate is held over the bound: the
#    three legs are 26 tall against 4 deep, the belly 14 against 5, the fire
#    tongues 10, 18 and 32 against 4, both puffs of smoke 14 and 18 against 8.
# 2. **A bowl's first draft was a solid, and it drew as a table.** Two whole
#    drafts spent the bowl's identity on depth, 18 to 22 units of z, so that it
#    would be a disc seen from above. Both came out a counter with a figure
#    behind it, because 22 units of z is 19 of screen height and the top face is
#    lit at 255 whatever it is. What made it a brazier was taking the depth back
#    out and letting the *stand* do the work: three legs with ten units of
#    background between them, under a faction belly, under a thin rim.
# 3. **What separates two masses is the background between them, and a gap is
#    not a gap under about five world units.** The fire is three tongues with
#    five and six units between them, and what shows through those gaps is the
#    torso: faction blue behind a near-white flame. The smoke is *two* puffs
#    and not the sprite's three, because a third would have had two units of
#    screen between it and its neighbour and would have drawn as one slab.
# 4. **Screen height is bought in z.** The smoke is what reaches the built
#    height, and it gets there by leaving in +x and *back* in +z, which is the
#    commander's banner-pole sign and the reason it tops the antlers.
#
# And the palette, which is rule 2's: **a sprite's materials decide its mesh's
# palette.** Spending a steel ramp on the brazier makes this archetype's neutral
# list ten entries rather than nine and moves the four-rung sampling, so the
# rungs here are chosen against what the generator resolves from the drawing as
# it stands rather than against what a rung number means on its own.
# Measured, faction blue, neutral: rung 0 is ink (16,16,24), rung 1 a warm brown
# (123,74,49), rung 2 a light steel (164,172,189) and rung 3 near-white
# (222,230,230). **Neither the sprite's gold nor its skin is sampled**: they
# are the eighth and ninth of ten entries by luminance and the four rungs land
# on the first, fourth, seventh and tenth. So the fire and the antlers take the
# brightest rung there is, and the head takes the warm one, which is a
# deliberate choice: a pale head between a pale crown and pale prongs is one
# grey slab, and the dark band is what separates them.
#
# The sprite spans its whole 32-texel cell, so this is the widest silhouette in
# the roster to meet: 66 world units authored against the 64 the rule asks, and
# it is the rim that spends them rather than the arms.
# ---------------------------------------------------------------------------

STORMCALLER: Tuple[Part, ...] = (
    Part(-17,  17,   0,  18,  -4,  10, RAMP_FACTION, 1, "robe hem"),
    Part(-18, -13,   6,  32, -10,  -6, RAMP_NEUTRAL, 1, "brazier leg, left"),
    Part( -3,   3,   6,  32, -10,  -6, RAMP_NEUTRAL, 1, "brazier leg, centre"),
    Part( 13,  18,   6,  32, -10,  -6, RAMP_NEUTRAL, 1, "brazier leg, right"),
    Part(-16,  16,   4,  64,   8,  16, RAMP_FACTION, 0, "cloak"),
    Part(-13,  13,  18,  44,  -2,   8, RAMP_FACTION, 2, "robe"),
    Part(-27,  27,  30,  44, -11,  -6, RAMP_FACTION, 3, "bowl, belly"),
    Part(-33,  33,  44,  48, -12,  -4, RAMP_NEUTRAL, 2, "bowl, rim"),
    Part(-10,  10,  44,  72,  -2,   8, RAMP_FACTION, 3, "torso"),
    Part(  7,  11,  48,  58, -10,  -6, RAMP_NEUTRAL, 3, "fire, right tongue"),
    Part(-17,  -9,  48,  70,  -6,   2, RAMP_FACTION, 2, "left arm, down to the rim"),
    Part(  9,  17,  48,  70,  -6,   2, RAMP_FACTION, 2, "right arm, down to the rim"),
    Part(-12,  -8,  48,  66, -10,  -6, RAMP_NEUTRAL, 3, "fire, left tongue"),
    Part( -2,   2,  48,  80, -10,  -6, RAMP_NEUTRAL, 3, "fire, centre tongue"),
    Part( -7,   7,  72,  90,  -3,   7, RAMP_NEUTRAL, 1, "head"),
    Part(  8,  18,  84,  98,   4,  12, RAMP_NEUTRAL, 2, "smoke, off the coals"),
    Part( -9,   9,  90,  97,  -3,   9, RAMP_FACTION, 3, "crown ring"),
    Part(-10,  -4,  98, 114,   4,  12, RAMP_NEUTRAL, 3, "left antler"),
    Part(  4,  10,  98, 114,   4,  12, RAMP_NEUTRAL, 3, "right antler"),
    Part( 18,  27, 110, 128,  12,  20, RAMP_NEUTRAL, 2, "smoke, leaving the frame"),
)

# ---------------------------------------------------------------------------
# The commander
#
# The second of the pair, and the harder one, because a banner on a pole is a
# raised feature that also has to *read as cloth*. Three parts carry the rank
# the sprite signals three ways, and each is placed by the same rule the
# stormcaller's bolts are:
#
# * The **pole** is the archer's bow trick: a tall thin thing offset in x. It is
#   set *behind* the shoulder rather than in front of it, because a part pushed
#   toward the viewer loses screen height as fast as one pushed away gains it.
#   Authored forward of the body, the pole's tip at 128 landed *below* the
#   plume; at the same height behind it, it tops the figure.
# * The **cloth** takes most of its size in z, so it is a bright top face
#   twenty-two units deep rather than a panel stacked over the shoulder. It is
#   also the largest faction part after the cape, and a faction ramp belongs on
#   large contiguous parts.
# * The **plume** is the knight's crest: narrow in x, swept back in z, and not
#   the thing that reaches the built height. The banner's finial is.
# ---------------------------------------------------------------------------

COMMANDER: Tuple[Part, ...] = (
    Part(-11,  -3,   0,   5,  -7,   7, RAMP_NEUTRAL, 0, "left boot"),
    Part(  3,  11,   0,   5,  -7,   7, RAMP_NEUTRAL, 0, "right boot"),
    Part(-10,  -4,   5,  36,  -5,   5, RAMP_NEUTRAL, 1, "left shin"),
    Part(  4,  10,   5,  36,  -5,   5, RAMP_NEUTRAL, 1, "right shin"),
    Part(-15,  15,   4,  94,   8,  16, RAMP_FACTION, 1, "cape"),
    Part(-10,  -3,  36,  58,  -6,   6, RAMP_NEUTRAL, 2, "left thigh"),
    Part(  3,  10,  36,  58,  -6,   6, RAMP_NEUTRAL, 2, "right thigh"),
    Part(-12,  12,  58,  72,  -7,   7, RAMP_FACTION, 2, "surcoat"),
    Part( 19,  24,  20, 122,   5,  11, RAMP_NEUTRAL, 1, "banner pole"),
    Part(-18, -11,  70,  94,  -6,   6, RAMP_NEUTRAL, 2, "left arm"),
    Part( 11,  18,  70,  94,  -6,   6, RAMP_NEUTRAL, 2, "right arm"),
    Part(-11,  11,  72,  98,  -7,   7, RAMP_FACTION, 3, "cuirass"),
    Part(-19, -10,  92, 104,  -8,   8, RAMP_FACTION, 2, "left pauldron"),
    Part( 10,  19,  92, 104,  -8,   8, RAMP_FACTION, 2, "right pauldron"),
    Part( -5,   5,  98, 104,  -5,   5, RAMP_NEUTRAL, 0, "gorget"),
    Part( 23,  34,  98, 120,  -4,  18, RAMP_FACTION, 3, "banner cloth"),
    Part( -8,   8, 102, 116,  -8,   8, RAMP_NEUTRAL, 2, "great helm"),
    Part(-22, -17,  90, 122, -12,  -8, RAMP_NEUTRAL, 3, "raised sword"),
    Part( -8,   8, 105, 111, -10,  -8, RAMP_NEUTRAL, 0, "visor"),
    Part( -9,   9, 112, 117,  -9,   9, RAMP_NEUTRAL, 1, "circlet"),
    Part( -3,   3, 114, 124,  -2,  12, RAMP_FACTION, 3, "plume"),
    Part( 18,  25, 122, 128,   4,  12, RAMP_NEUTRAL, 3, "banner finial"),
)

# ---------------------------------------------------------------------------
# The rogue
#
# The second humanoid a tactics board fields beside the knight, and the hardest
# separation in this commission: the two are the same creature at
# 22 pixels unless something other than "a crouch" separates them, and a crouch
# is exactly what a mesh cannot spend, because rule 1 builds every figure 128
# units tall whatever it is doing. What separates them here is spent in **x**,
# which is the axis a figure reads by:
#
# * the **stance**. Feet and knees are authored at ±26 against the knight's
#   ±13, and the thighs draw back in to ±20, so the silhouette is a triangle
#   where the knight's is a rectangle. Three drafts put the crouch in y
#   instead, with shorter legs and a longer torso, and every one of them read
#   as a knight that had been sat on.
# * the **two blades**, and the reason they are four parts rather than two. A
#   dagger authored as one box is a bright square; authored as an out-and-along
#   pair it is a streak, and the pair is hung low on the left and high on the
#   right so the figure carries a diagonal nothing else in the roster has. The
#   sprite's blades are curved and a box is not, so the curve is spent on the
#   asymmetry instead.
# * the **head**, as a hole rather than as a shape. A hood built as a stack of
#   shrinking boxes is a ziggurat; a dark crown set back behind a brighter
#   brim is a cavity with a rim around it, which is the one thing at this size
#   that says a face is in shadow. The brim is on the faction ramp because that
#   rim is where a viewer is already looking, and because the chest alone is a
#   thin showing of a faction under a mantle that overhangs it.
#
# What was tried and abandoned is worth as much: a cape. A slab behind the
# shoulders is drawn *above* the head at this pitch, because `z·cos φ` raises a
# part on screen exactly as height does. A cloak does not hang behind a figure
# here, it haloes it. There is no cloak in this model for that reason.
# ---------------------------------------------------------------------------

ROGUE: Tuple[Part, ...] = (
    Part(-26, -16,   0,   8, -18,  -2, RAMP_NEUTRAL, 0, "left foot"),
    Part( 16,  26,   0,   8, -18,  -2, RAMP_NEUTRAL, 0, "right foot"),
    Part(-25, -17,   8,  28, -16,  -4, RAMP_NEUTRAL, 1, "left shin"),
    Part( 17,  25,   8,  28, -16,  -4, RAMP_NEUTRAL, 1, "right shin"),
    Part(-26, -17,  26,  44, -22,  -8, RAMP_NEUTRAL, 1, "left knee"),
    Part( 17,  26,  26,  44, -22,  -8, RAMP_NEUTRAL, 1, "right knee"),
    Part(-20,  -9,  44,  64, -10,   4, RAMP_NEUTRAL, 0, "left thigh"),
    Part(  9,  20,  44,  64, -10,   4, RAMP_NEUTRAL, 0, "right thigh"),
    Part(-27, -20,  44,  50, -34, -28, RAMP_NEUTRAL, 3, "left blade, tip"),
    Part(-24, -14,  54,  58, -30, -24, RAMP_NEUTRAL, 3, "left blade"),
    Part(-14,  14,  60,  84, -16,  -2, RAMP_FACTION, 1, "waist"),
    Part(-21, -14,  68,  92, -22, -12, RAMP_NEUTRAL, 1, "left arm"),
    Part( 14,  21,  68,  92, -22, -12, RAMP_NEUTRAL, 1, "right arm"),
    Part(-14,  14,  84, 104, -16,  -2, RAMP_FACTION, 3, "chest"),
    Part( 14,  24,  88,  92, -24, -18, RAMP_NEUTRAL, 3, "right blade"),
    Part(-18,  18,  96, 106, -12,   2, RAMP_NEUTRAL, 1, "mantle"),
    Part( 20,  27,  96, 102, -20, -14, RAMP_NEUTRAL, 3, "right blade, tip"),
    Part(-12,  12,  98, 110, -28, -16, RAMP_FACTION, 3, "hood brim"),
    Part( -8,   8, 108, 128, -22, -10, RAMP_NEUTRAL, 0, "hood"),
)

# ---------------------------------------------------------------------------
# The beast
#
# The one non-humanoid in the roster, and the easiest figure in it for a reason
# that is worth stating rather than enjoying: **its whole body is a horizontal
# thing, and horizontal is the axis this camera reads.** Where the knight has
# to spend a shield and a sword to differ from the archer, the beast differs
# from every humanoid before a single detail is authored, because it is long in
# x and short in y and nothing else here is.
#
# The failure mode it does have is furniture: four legs and a slab is a table.
# Three parts answer that and all three are placed in x or z rather than
# stacked in y:
#
# * the **head**, at the +x end and the highest thing in the model, so it is
#   drawn last and reads clean, with the muzzle thrown forward in −z where a
#   viewer sees a front face and the ear set back in +z above the skull;
# * the **tail**, at the −x end, sweeping out and down from the rump so that it
#   leaves the spine's line, which the first draft proved necessary: a tail
#   authored level with the back reads as one more plate on it;
# * the **spine**, as three separate plates spread along x rather than one
#   ridge. A single long faction-coloured box on a quadruped's back is a bright
#   slab and reads as cargo; three with gaps between them read as an animal.
#
# The faction ramp rides the plates, the collar and the tail tuft: they are
# small, but they are the three places a viewer is already looking, and the
# hide has to stay hide.
# ---------------------------------------------------------------------------

BEAST: Tuple[Part, ...] = (
    Part(-19, -10,   0,  46,   6,  14, RAMP_NEUTRAL, 0, "rear leg, far"),
    Part( 11,  20,   0,  46,   6,  14, RAMP_NEUTRAL, 0, "front leg, far"),
    Part(-19, -10,   0,  46, -16,  -8, RAMP_NEUTRAL, 1, "rear leg, near"),
    Part( 11,  20,   0,  46, -16,  -8, RAMP_NEUTRAL, 1, "front leg, near"),
    Part(-34, -29,  46,  58,   1,   7, RAMP_FACTION, 3, "tail tuft"),
    Part(-34, -25,  52,  64,   0,   8, RAMP_NEUTRAL, 1, "tail"),
    Part(-27, -19,  58,  68,  -2,   8, RAMP_NEUTRAL, 1, "tail, root"),
    Part(-23,  19,  42,  86, -16,  14, RAMP_NEUTRAL, 1, "body"),
    Part( 14,  24,  66,  96, -10,   6, RAMP_NEUTRAL, 1, "neck"),
    Part( 12,  22,  78,  88, -14,   6, RAMP_FACTION, 2, "collar"),
    Part(-16,  -8,  84,  98,  -5,   5, RAMP_FACTION, 3, "spine plate, rear"),
    Part(  8,  16,  84,  98,  -5,   5, RAMP_FACTION, 3, "spine plate, front"),
    Part( -4,   4,  86, 102,  -5,   5, RAMP_FACTION, 3, "spine plate, middle"),
    Part( 24,  31,  84,  98, -26, -12, RAMP_NEUTRAL, 1, "muzzle"),
    Part( 18,  30,  94, 118, -14,   0, RAMP_NEUTRAL, 2, "skull"),
    Part( 19,  26, 112, 128,  -6,   2, RAMP_NEUTRAL, 0, "ear"),
)

#: This style's commissioned meshes, by archetype name. The roster is closed at
#: eight: knight, archer, mage, stormcaller, healer, commander, rogue, beast. A
#: commission is allowed to be a *subset* of it: a mesh is an additional
#: drawing of an archetype, not a cell of the animation sequence every style is
#: required to ship, so an archetype without one is not an incomplete
#: archetype. This one is complete.
MESHES: Mapping[str, Tuple[Part, ...]] = {
    "knight": KNIGHT,
    "archer": ARCHER,
    "mage": MAGE,
    "stormcaller": STORMCALLER,
    "healer": HEALER,
    "commander": COMMANDER,
    "rogue": ROGUE,
    "beast": BEAST,
}
