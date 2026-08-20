# SPDX-License-Identifier: MIT
"""The ``mythical`` mesh commission: the same eight roles, dressed as dragonkin.

The third style drawn as solids, and the first drawn against *two* others: every
figure here has to separate itself from its own seven, from the ``medieval``
figure standing beside it in the same role on ``ROSTER.md``, and from the
``scifi`` one standing beside that. What each spends on the third constraint is
stated per figure, because it costs a different thing each time.

Two things decided more of this table than taste did.

**The sprites are read as they are drawn.** One horned silhouette on four of
this style's eight separates none of them from each other, so the drakeguard
carries a fore-and-aft **comb**, the scalethief a one-sided backswept **crest**,
and the stormsinger no mark of its own at all: the crests it carries belong to
the wyrmlings sitting on its shoulders. The three marks above a head here are
therefore three different shapes, and each is built the way this camera draws
that shape rather than the way a sprite draws it: a comb is one narrow part long
in **z**, a crest is a fan of parts stepping back in z on one side, and horns
are a pair swept out and back.

**The palette is the sprite's, and it is measured rather than intended.**
A mesh's two ramps resolve from its own sprite's CLUT, and the four rungs
sample that list by luminance, so a colour a drawing
uses can simply not be reachable by a mesh. What this style's eight actually
got, measured on the generated sprites:

===========  ===========  ===========  ===========  ===========
figure       rung 0       rung 1       rung 2       rung 3
===========  ===========  ===========  ===========  ===========
drakeguard   ink 0        hide 1       skin 1       sand 3
wyrm-hunter  ink 0        hide 1       ink 2        sand 3
runecaster   ink 0        skin 0       gold 0       gold 1
stormsinger  ink 0        hide 2       skin 1       sand 3
wyrmpriest   ink 0        skin 1       gold 1       ink 3
dragonlord   ink 0        hide 2       skin 1       sand 3
scalethief   ink 0        hide 1       skin 1       sand 3
dragon       ink 0        hide 2       sand 1       sand 3
===========  ===========  ===========  ===========  ===========

Three of those changed a figure. The **runecaster**'s brightest two rungs are
both ember gold, so it can reach no scale and no horn at all. Its ring of runes
is the brightest thing in the model by construction and its body is dark, which
is the sprite. The **wyrmpriest**'s brightest rung is paper white and its third
is gold, so the mantle takes the white and the egg is the only gold in the
figure. The **dragon** samples no skin, which is what a dragon wants: black,
scale, dull horn, pale horn.

Five of the eight would have liked ember gold for a rune or a trim and cannot
have it: gold sits eighth of eleven entries and the rungs land on the first,
fourth, seventh and eleventh. So every ember mark in this style's *meshes* is
the pale horn rung instead. That is stated here rather than worked around: it is
the same finding the medieval stormcaller's grey antlers recorded, met again.
"""

from __future__ import annotations

from typing import Mapping, Tuple

from .rules import RAMP_FACTION, RAMP_NEUTRAL, Part

#: The style these tables are the commission for. Declared rather than taken
#: from the filename, so the registry in :mod:`.` cannot bind a module to the
#: wrong style's sprites. That is the one mistake the silhouette check would
#: not catch, since it would simply hold this drakeguard to a knight's box.
STYLE = "mythical"


# ---------------------------------------------------------------------------
# The drakeguard
#
# The knight's role in scale plate, and it has to differ from a knight *and* a
# trooper, both of which are a torso with a shield on one side and a weapon on
# the other. Three things carry it:
#
# * The **shield is a kite that points down**, and it is built as three walls
#   whose widths step 20, 16, 8 rather than as one panel. A knight's shield is
#   a mid-height rectangle beside the torso and a trooper's a slab from ankle
#   to chest; this one runs from the shin to the shoulder and *narrows* on the
#   way down, so the outline is a wedge where theirs are rectangles. The steps
#   are not a cost: at this pitch a stack shows a bright top-face stripe at
#   every join, which is exactly what the sprite draws as the membrane's ribs.
#
#   The shield and the coat are **both** faction, because the sprite draws both
#   that way, and the first draft of this table lost the shield inside the coat
#   for exactly that reason: two large masses of one colour that touch are one
#   mass. What parts them is *value* rather than shape: the shield takes the
#   two brightest faction rungs and the coat and skirt the darkest, so at
#   twenty-five pixels a pale wedge stands against a dark body. That is the
#   same answer the gap rule gives when there is no room for a gap: the shield
#   reaches x -29 where the coat stops at -12, and three world units of overlap
#   is not background enough to separate anything.
# * The **comb is one part, narrow in x and long in z.** The mark that replaced
#   this style's shared horns, and the only construction that draws a fin seen
#   end-on: six world units of width is three pixels, and fourteen of depth
#   draws twelve of screen height, so it is a thin bright stroke standing above
#   the helm. Built as a stack in y it would have been a ziggurat, and built
#   flat it would have been a plate on the crown.
#
#   **It also has no arms**, and that is not an oversight. They were authored,
#   and the console drew neither: the left sits entirely behind the shield and
#   the right is a five-unit sliver between the coat and the glaive. Two parts
#   that no camera this scene reaches can show are two parts the sixteen-unit
#   cache rebuild pays for, and dropping them took this figure from 23 parts to
#   21 and the rebuild back inside a thirtieth of a second.
# * The **glaive is short and held out**, not up. It is the sprite's diagonal
#   given up deliberately, because a diagonal is affordable in two boxes and
#   not in five. What is kept is a pale shaft and a fang at head height on the
#   far side from the shield, which is the diagonal the *figure* carries rather
#   than one the weapon does.
# ---------------------------------------------------------------------------

DRAKEGUARD: Tuple[Part, ...] = (
    Part(-14,  -5,   0,   5,  -5,   5, RAMP_NEUTRAL, 0, "left boot"),
    Part(  5,  14,   0,   5,  -5,   5, RAMP_NEUTRAL, 0, "right boot"),
    Part(-13,  -6,   5,  28,  -4,   4, RAMP_NEUTRAL, 1, "left greave"),
    Part(  6,  13,   5,  28,  -4,   4, RAMP_NEUTRAL, 1, "right greave"),
    Part(-23, -15,  11,  29,  -7,  -3, RAMP_FACTION, 2, "wing shield, point"),
    Part(-13,  -5,  26,  41,  -5,   5, RAMP_NEUTRAL, 1, "left thigh"),
    Part(  5,  13,  26,  41,  -5,   5, RAMP_NEUTRAL, 1, "right thigh"),
    Part(-27, -11,  28,  46,  -7,  -3, RAMP_FACTION, 3, "wing shield, membrane"),
    Part(-14,  14,  38,  48,  -6,   6, RAMP_FACTION, 1, "scale skirt"),
    Part( 22,  28,  44,  66,   3,   7, RAMP_NEUTRAL, 3, "glaive shaft"),
    Part(-29,  -9,  44,  61,  -7,  -3, RAMP_FACTION, 3, "wing shield, head"),
    Part(-12,  12,  46,  67,  -7,   7, RAMP_FACTION, 1, "faction plate"),
    Part( -5,   5,  46,  67,  -8,  -7, RAMP_NEUTRAL, 1, "scale ridge"),
    Part(-19,  -9,  63,  71,  -4,   7, RAMP_NEUTRAL, 2, "left pauldron spike"),
    Part(  9,  19,  63,  71,  -4,   7, RAMP_NEUTRAL, 2, "right pauldron spike"),
    Part( 24,  30,  63,  77,   4,   9, RAMP_NEUTRAL, 3, "glaive fang"),
    Part( -8,   8,  66,  70,  -5,   5, RAMP_NEUTRAL, 0, "gorget"),
    Part(-10,  10,  69,  82,  -6,   6, RAMP_NEUTRAL, 1, "drake helm"),
    Part(-10,  10,  71,  75,  -7,  -6, RAMP_NEUTRAL, 0, "visor band"),
    Part( -3,   3,  77,  85,   1,  11, RAMP_NEUTRAL, 3, "helm comb"),
    Part( -7,   7,  75,  77,  -8,  -7, RAMP_NEUTRAL, 3, "ember slit"),
)

# ---------------------------------------------------------------------------
# The wyrm-hunter
#
# The archer's role, and the easiest figure in this commission, because its
# sprite's read is already the line this camera draws best: a horn crossbow held
# **level across the whole width** at chest height. It is one box sixty world
# units long, six tall and six deep, a plate by the wall test, which is what
# is wanted: a bright bar streaking the full width with its own top face lit at
# 255.
#
# The three archers are then three different lines, which is the whole
# separation and it costs one part each: the medieval archer's bow is a tall
# vertical beside the body, the sniper's rail is a level bar at the *waist* on
# one side with a lit muzzle at the far end, and this is level, symmetric, at
# the *chest*, and doubled, with a darker stock two units in front of and below
# the prod, so the figure carries two parallel horizontals rather than one.
#
# The quiver climbs behind the shoulder rather than beside it, and the
# fletching above it is the one part in this model spent on depth: at z 8 to 16
# it draws fourteen world units higher than its own y would put it, which is
# how a feature rises at this pitch.
#
# Everything above the waist is faction, because the sprite draws a faction
# coat under a faction hood, and it is spread across three rungs for the reason
# the drakeguard's shield is: one colour over a whole upper body is one mass.
# The coat takes the brightest, the sleeves the darkest, the hood the one
# between, and the **face is a grey block below the hood rather than inside
# it**. This archetype's second rung resolves to ink 2 and not to skin, so
# there is no warm face to be had, and a head tucked under a hood at the same z
# is drawn over by it however proud its recess is. The dark recess that reads
# as the eyes is on the *hood*, in its vertical middle, which is the
# construction the medieval knight's visor already uses and the only one that
# survives the far-to-near order: a band low on a tall part is always drawn
# first.
# ---------------------------------------------------------------------------

WYRM_HUNTER: Tuple[Part, ...] = (
    Part(-13,  -4,   0,   5,  -5,   5, RAMP_NEUTRAL, 0, "left boot"),
    Part(  4,  13,   0,   5,  -5,   5, RAMP_NEUTRAL, 0, "right boot"),
    Part(-12,  -5,   5,  24,  -4,   4, RAMP_NEUTRAL, 1, "left shin"),
    Part(  5,  12,   5,  24,  -4,   4, RAMP_NEUTRAL, 1, "right shin"),
    Part(-12,  -4,  23,  36,  -4,   4, RAMP_NEUTRAL, 1, "left thigh"),
    Part(  4,  12,  23,  36,  -4,   4, RAMP_NEUTRAL, 1, "right thigh"),
    Part(-13,  13,  34,  41,  -5,   5, RAMP_FACTION, 1, "scale skirt"),
    Part( 10,  18,  36,  58,   4,   8, RAMP_NEUTRAL, 2, "quiver"),
    Part(-15, -11,  40,  52,  -4,   4, RAMP_FACTION, 1, "left arm"),
    Part( 11,  15,  40,  52,  -4,   4, RAMP_FACTION, 1, "right arm"),
    Part(-11,  11,  40,  59,  -5,   5, RAMP_FACTION, 3, "hunter's coat"),
    Part( 22,  32,  42,  48, -12,  -8, RAMP_NEUTRAL, 3, "loaded bolt, fang"),
    Part(-11,  11,  45,  48, -11, -10, RAMP_NEUTRAL, 1, "prod stock"),
    Part( -5,   5,  40,  59,  -6,  -5, RAMP_NEUTRAL, 1, "scale placket"),
    Part(-30,  30,  47,  51, -10,  -6, RAMP_NEUTRAL, 3, "horn prod, level"),
    Part( 12,  16,  55,  65,   5,  10, RAMP_NEUTRAL, 3, "bolt fletching"),
    Part( -6,   6,  57,  65,  -6,   1, RAMP_NEUTRAL, 2, "face"),
    Part(-12,  12,  63,  76,  -4,   6, RAMP_FACTION, 2, "scaled hood"),
    Part( -2,  10,  69,  77,   2,   8, RAMP_FACTION, 1, "hood point"),
    Part( -9,   9,  66,  70,  -5,  -4, RAMP_NEUTRAL, 0, "hood recess"),
)

# ---------------------------------------------------------------------------
# The runecaster
#
# The mage's role, and the figure this commission spent its cleverness on,
# because its sprite's device is a **hole**: a closed ring of runes with the
# cell's own transparency inside it, and nothing else in the whole library has
# a silhouette that is not simply connected.
#
# The ring is eight parts and it is authored **upright**, in x and y, which is
# the one place "a planar emblem belongs in the horizontal plane" is
# deliberately not followed. The reason is what that rule is actually about. A
# ring lying flat above a head is a *halo*: its near arc draws below the head's
# crown and over it, so what a viewer gets is a head with a gold band across
# it. Upright and set back at z 6 to 11, the ring's lowest bar draws a world
# unit above the head's highest point, so the hole is background and stays
# background. The price the upright choice pays is that screen vertical keeps
# only half of y where it keeps 0.87 of z, so the hole is squashed two to one,
# which is why the ring is authored forty-four world units tall to be thirteen
# pixels wide and five high, and why the figure's body had to be pushed down to
# make room for it. **The sprite does that too**: its head sits at row 13 where
# every other archetype's is at row 9.
#
# There is no hood. The sprite draws one, and the palette cannot: scale is not
# in this archetype's neutral list at all, so a hood would have been the black
# rung over a dark brown head, and the first draft's was a void that swallowed
# the face. What is there instead is a plain head with a **gold band across its
# middle**, two units proud of its front so nothing is drawn over it. That is
# the commission notes' "a face is a hole with a rim of light", inverted
# because here the light is the only colour available.
#
# Two rungs, alternating, on the ring's eight parts, because the sprite
# alternates ``EMBER[step % 2]`` around its circle and both of this archetype's
# top rungs are gold. That is the palette coupling paying rather than costing
# for once.
#
# The robe is the medieval mage's problem met again: a legless figure banded
# horizontally is a stack of slabs. It takes that case's answer, a full-height
# **rune placket** down the front in a rung the flanks do not wear, so
# something on the figure runs vertically. Here it is gold on blue, and it is
# the only neutral mark below the collar.
# ---------------------------------------------------------------------------

RUNECASTER: Tuple[Part, ...] = (
    Part(-14,  14,   0,  14,  -6,   6, RAMP_FACTION, 1, "robe hem"),
    Part(-12,  12,  13,  31,  -5,   5, RAMP_FACTION, 2, "robe skirt"),
    Part( -4,   4,   3,  45,  -7,  -6, RAMP_NEUTRAL, 2, "rune placket"),
    Part(-17, -12,  24,  31,  -6,  -1, RAMP_NEUTRAL, 1, "left hand, open"),
    Part( 12,  17,  24,  31,  -6,  -1, RAMP_NEUTRAL, 1, "right hand, open"),
    Part(-14,  -8,  28,  40,  -3,   3, RAMP_FACTION, 2, "left sleeve"),
    Part(  8,  14,  28,  40,  -3,   3, RAMP_FACTION, 2, "right sleeve"),
    Part(-10,  10,  29,  45,  -4,   4, RAMP_FACTION, 2, "robe"),
    Part( -9,   9,  40,  46,  -5,   5, RAMP_FACTION, 3, "collar"),
    Part( -8,   8,  42,  60,  -6,   3, RAMP_NEUTRAL, 1, "face"),
    Part( -6,   6,  50,  54,  -7,  -6, RAMP_NEUTRAL, 3, "rune eyes"),
    Part( -8,   8,  58,  64,   4,   8, RAMP_NEUTRAL, 3, "rune ring, foot"),
    Part(-17,  -7,  61,  68,   4,   8, RAMP_NEUTRAL, 2, "rune ring, lower left"),
    Part(  7,  17,  61,  68,   4,   8, RAMP_NEUTRAL, 2, "rune ring, lower right"),
    Part(-19, -13,  66,  81,   4,   8, RAMP_NEUTRAL, 3, "rune ring, left"),
    Part( 13,  19,  66,  81,   4,   8, RAMP_NEUTRAL, 3, "rune ring, right"),
    Part(-17,  -7,  80,  87,   4,   8, RAMP_NEUTRAL, 2, "rune ring, upper left"),
    Part(  7,  17,  80,  87,   4,   8, RAMP_NEUTRAL, 2, "rune ring, upper right"),
    Part( -8,   8,  84,  89,   4,   8, RAMP_NEUTRAL, 3, "rune ring, crown"),
)

# ---------------------------------------------------------------------------
# The stormsinger
#
# The stormcaller's role, and where its width comes from is the whole shape of
# it. Raised arms with a wyrmling thrown off each hand read as the rig four
# styles share rather than as anybody, so the wyrmlings come home: they **sit
# on the shoulders**, their necks arch out and level, and the arms hang.
#
# What that buys is a thing no other silhouette in the library has: **three
# skulls in one row**, at one height, with sixteen world units of background
# between each pair. The gap rule says five units is structure, so eight pixels
# of nothing on either side of the head is not a near miss, it is the read.
# Every part of the wyrmlings is therefore placed to keep that band clear: the
# folded membranes hang from y 46 to 76, *below* the heads, and the crests are
# small and swept back in z rather than stacked up in y.
#
# **A draft of this read as raised arms**, which is the exact fault the
# sprite's shoulder-borne wyrmlings exist to avoid, and the cause is worth
# writing down because it is a shape rule and not a mistake: an outer element
# that runs from the hem to above the head is a vertical column whatever is
# drawn on it, and a pale cap on top of a column is a hand holding something.
# What fixed it was **stopping the outer column at the shoulder**, so the
# wyrmling's head is its topmost part, ten world units below the singer's own
# crown, and making the skull *wider than tall*, twelve by ten, where the draft
# had it eight by sixteen. A skull at this camera is a horizontal block; a
# vertical one is a forearm.
#
# Nothing here climbs. The medieval stormcaller throws bolts outward, the drone
# swarm's antennae are the one thing in its style that rises, and this figure's
# tallest part is its own circlet, which is right, because the sprite's is too.
# ---------------------------------------------------------------------------

STORMSINGER: Tuple[Part, ...] = (
    Part(-15,  15,   0,  12,  -5,   5, RAMP_FACTION, 1, "robe hem"),
    Part(-13,  13,  11,  31,  -5,   5, RAMP_FACTION, 2, "robe"),
    Part( -4,   4,   4,  50,  -6,  -5, RAMP_NEUTRAL, 1, "scale placket"),
    Part(-30, -16,  27,  45,   1,   6, RAMP_FACTION, 0, "left membrane, folded"),
    Part( 16,  30,  27,  45,   1,   6, RAMP_FACTION, 0, "right membrane, folded"),
    Part(-15, -11,  30,  45,  -4,   4, RAMP_FACTION, 2, "left arm, hanging"),
    Part( 11,  15,  30,  45,  -4,   4, RAMP_FACTION, 2, "right arm, hanging"),
    Part(-11,  11,  30,  50,  -5,   5, RAMP_FACTION, 3, "torso"),
    Part(-30, -16,  43,  51,  -2,   5, RAMP_NEUTRAL, 1, "left wyrmling, body"),
    Part( 16,  30,  43,  51,  -2,   5, RAMP_NEUTRAL, 1, "right wyrmling, body"),
    Part(-30, -20,  49,  55,  -4,   1, RAMP_NEUTRAL, 1, "left wyrmling, neck"),
    Part( 20,  30,  49,  55,  -4,   1, RAMP_NEUTRAL, 1, "right wyrmling, neck"),
    Part(-32, -20,  53,  59,  -5,   1, RAMP_NEUTRAL, 1, "left wyrmling, head"),
    Part( 20,  32,  53,  59,  -5,   1, RAMP_NEUTRAL, 1, "right wyrmling, head"),
    Part(-30, -25,  55,  57,  -6,  -5, RAMP_NEUTRAL, 3, "left wyrmling, eye"),
    Part( 25,  30,  55,  57,  -6,  -5, RAMP_NEUTRAL, 3, "right wyrmling, eye"),
    Part(-31, -23,  59,  63,   1,   5, RAMP_NEUTRAL, 3, "left wyrmling, crest"),
    Part( 23,  31,  59,  63,   1,   5, RAMP_NEUTRAL, 3, "right wyrmling, crest"),
    Part( -8,   8,  55,  67,  -4,   4, RAMP_NEUTRAL, 2, "head"),
    Part(-10,  10,  61,  72,  -2,   6, RAMP_NEUTRAL, 1, "scale hood"),
    Part( -9,   9,  71,  76,  -4,   4, RAMP_NEUTRAL, 3, "singer's circlet"),
)

# ---------------------------------------------------------------------------
# The wyrmpriest
#
# The healer's role, and the third pale-mass-with-a-glyph in the library. The
# medieval healer is a robe to the ground with a cross on a planted staff; the
# medic has legs, and a cross floating clear of an injector. This one keeps the
# legless mass and changes both the mass and the glyph, which is what its
# sprite does: **the widest row is the hem, not the shoulders**, so the outline
# is a bell rather than a column, and the glyph is a **hatching egg** held up
# at head height rather than a cross.
#
# The egg is the only gold in the figure, and that is the palette rather than a
# choice: rung 2 resolves to gold and rung 3 to paper white, so the mantle is
# white, the face is the one warm rung, and there is nothing else the egg could
# be. It is three parts because a glyph at twenty-five pixels is a shape and
# not a hint: a shell, a cracked crown above it, and an ember proud of the
# front.
#
# The mantle is a stack of horizontal masses, which is the trap the refused
# mage fell into, and it survives for the reason the wall test gives: it is
# held to sixteen units of depth, which makes its two tall sections **walls**
# where a uniformly deep robe would have made every one of them a plate, and
# the faction stole runs the full height of the front in a colour the flanks do
# not wear. A white bell with one bright blue vertical stripe is legible at any
# size this board draws; a white bell alone is a snowman.
# ---------------------------------------------------------------------------

WYRMPRIEST: Tuple[Part, ...] = (
    Part(-16,  16,   0,  13,  -5,   5, RAMP_NEUTRAL, 3, "mantle hem"),
    Part( -6,   6,   3,  36,  -6,  -5, RAMP_FACTION, 3, "stole, skirt"),
    Part(-22,  22,  11,  35,  -5,   5, RAMP_NEUTRAL, 3, "mantle, widest row"),
    Part(-22, -16,  26,  33,  -5,  -1, RAMP_NEUTRAL, 1, "left hand"),
    Part(-20, -15,  31,  48,  -4,   4, RAMP_NEUTRAL, 3, "left arm"),
    Part(-15,  15,  34,  53,  -5,   5, RAMP_NEUTRAL, 3, "mantle, shoulders"),
    Part( 15,  20,  39,  58,  -4,   4, RAMP_NEUTRAL, 3, "right arm, raised"),
    Part( -6,   6,  34,  57,  -6,  -5, RAMP_FACTION, 3, "stole, chest"),
    Part( -7,   7,  57,  60,  -5,  -4, RAMP_NEUTRAL, 0, "face shadow"),
    Part( -8,   8,  55,  68,  -4,   4, RAMP_NEUTRAL, 1, "face"),
    Part(-11,  11,  60,  74,  -3,   6, RAMP_NEUTRAL, 3, "pale hood"),
    Part( 16,  26,  62,  73,  -5,   3, RAMP_NEUTRAL, 2, "egg, shell"),
    Part(-10,  10,  67,  70,  -5,  -3, RAMP_FACTION, 3, "brow band"),
    Part( 19,  23,  67,  70,  -6,  -5, RAMP_NEUTRAL, 2, "egg, ember"),
    Part( -8,   8,  73,  80,   0,   6, RAMP_NEUTRAL, 3, "hood crown"),
    Part( 18,  26,  72,  78,  -4,   1, RAMP_NEUTRAL, 2, "egg, cracked crown"),
)

# ---------------------------------------------------------------------------
# The dragonlord
#
# The commander's role, and rank stated the way the sprite states it and the
# way the other two leaders do not: **asymmetrically**. The commander's banner
# and the captain's mast are both a vertical carried at one side with the
# figure square behind it; this one's wing is a mass that starts at the hip and
# leaves the top of the frame on one side only, and its counterweight on the
# far side is a lance that stops at the shoulder. So the outline leans, which
# is the separation from the stormsinger inside this style as well: that
# figure is wide on both sides and this one is wide on one.
#
# The wing is three walls stepping out and up, and each is a wall by the test
# (34 units of height against 8 of depth) rather than a plate, so it reads as a
# membrane standing beside the figure and not as a shelf over it. Its tip is
# the tallest thing in the model and it is authored *behind* the shoulder, at z
# 8 to 16, for the reason the commander's banner pole had to move: a part in
# front of the shoulder is drawn below the helm however tall it is.
#
# Horns are this style's rank mark and the dragon's anatomy, and nothing else
# wears them any more. They are a pair of backswept parts flanking the crown
# rather than a stack above it, because two bright marks either side of a helm
# is what a pair of horns is at twenty-five pixels and a taper is a staircase.
# ---------------------------------------------------------------------------

DRAGONLORD: Tuple[Part, ...] = (
    Part(-12,  -5,   0,  28,  -4,   4, RAMP_NEUTRAL, 1, "left shin"),
    Part(  5,  12,   0,  28,  -4,   4, RAMP_NEUTRAL, 1, "right shin"),
    Part(-17,  17,   4,  59,   3,   7, RAMP_FACTION, 1, "drake cape"),
    Part(-12,  -4,  27,  42,  -5,   5, RAMP_NEUTRAL, 1, "left thigh"),
    Part(  4,  12,  27,  42,  -5,   5, RAMP_NEUTRAL, 1, "right thigh"),
    Part( 16,  32,  27,  53,   1,   6, RAMP_FACTION, 1, "wing, lower"),
    Part(-13,  13,  39,  49,  -6,   6, RAMP_FACTION, 2, "hip armour"),
    Part(-21, -15,  31,  68,  -6,  -1, RAMP_NEUTRAL, 3, "lance shaft"),
    Part(-17, -12,  49,  66,  -4,   4, RAMP_NEUTRAL, 1, "left arm"),
    Part( 12,  17,  49,  66,  -4,   4, RAMP_NEUTRAL, 1, "right arm"),
    Part(-12,  12,  48,  72,  -7,   7, RAMP_NEUTRAL, 1, "scale cuirass"),
    Part( 18,  34,  51,  75,   3,   7, RAMP_FACTION, 2, "wing, middle"),
    Part( -9,   9,  56,  61,  -8,  -7, RAMP_NEUTRAL, 3, "ember trim"),
    Part(-20, -10,  68,  75,  -6,   4, RAMP_NEUTRAL, 1, "left pauldron"),
    Part( 10,  20,  68,  75,  -6,   4, RAMP_NEUTRAL, 1, "right pauldron"),
    Part(-23, -13,  65,  82,  -4,   1, RAMP_NEUTRAL, 3, "lance head"),
    Part( 20,  32,  72,  90,   4,   8, RAMP_FACTION, 3, "wing, tip"),
    Part(-16,  -7,  77,  84,   1,   6, RAMP_NEUTRAL, 3, "horn, left"),
    Part(  7,  16,  77,  84,   1,   6, RAMP_NEUTRAL, 3, "horn, right"),
    Part(-10,  10,  73,  86,  -6,   6, RAMP_NEUTRAL, 1, "drake helm"),
    Part(-10,  10,  76,  80,  -8,  -6, RAMP_NEUTRAL, 0, "visor band"),
    Part( -7,   7,  80,  82,  -8,  -7, RAMP_NEUTRAL, 3, "ember slit"),
)

# ---------------------------------------------------------------------------
# The scalethief
#
# The rogue's role, and the hardest separation in the library, because all
# three rogues are a crouched figure with two blades and their sprites ask for
# almost the same width: 56 world units here against the rogue's 54 and the
# infiltrator's 56. The two obvious answers are already spent: the rogue is
# wide in x and the infiltrator is deep in z, and the notes beside those two
# figures record what each of them cost.
#
# What is left, and what this sprite actually draws, is **asymmetry above the
# head**. The crest is three pale spines stepping back and out on the *left* of
# the hood (2 to 10, 4 to 12, 6 to 14 in z), so each one draws higher than the
# last without a single unit being spent in y. A stack of three would have been
# a ziggurat; a fan of three in depth is a fringe, and it is the only mark in
# the library that is on one side of a head. Neither the rogue nor the
# infiltrator has anything above the hood at all.
#
# The two fangs keep the diagonal both other rogues carry, in the two boxes a
# diagonal is affordable in rather than the five that read as rubble: a low one
# thrown out to the left at knee height and a high one at the right shoulder,
# each a hilt and a blade meeting at an angle. And the crouch is stated in
# **x** (shins at plus or minus twenty-two), because the `medieval` rogue's
# three drafts that put a crouch in y all drew a knight that had been sat on.
# ---------------------------------------------------------------------------

SCALETHIEF: Tuple[Part, ...] = (
    Part(-22, -13,   0,  20,  -5,   2, RAMP_NEUTRAL, 1, "left shin"),
    Part( 13,  22,   0,  20,  -5,   2, RAMP_NEUTRAL, 1, "right shin"),
    Part(-16,  16,   3,  32,   2,   7, RAMP_NEUTRAL, 1, "scale cloak"),
    Part(-28, -19,  13,  17, -10,  -7, RAMP_NEUTRAL, 3, "wyrm fang, low blade"),
    Part(-21, -14,  17,  24,  -9,  -6, RAMP_NEUTRAL, 3, "wyrm fang, low hilt"),
    Part(-20, -11,  18,  31,  -5,   2, RAMP_NEUTRAL, 1, "left thigh"),
    Part( 11,  20,  18,  31,  -5,   2, RAMP_NEUTRAL, 1, "right thigh"),
    Part(-13,  13,  17,  32,  -7,  -2, RAMP_FACTION, 2, "scale kilt"),
    Part(-15,  15,  30,  35,  -7,  -2, RAMP_NEUTRAL, 3, "horn belt"),
    Part(-18, -13,  34,  45,  -6,   0, RAMP_NEUTRAL, 1, "left arm"),
    Part(-13,  13,  32,  50,  -6,   5, RAMP_NEUTRAL, 1, "torso"),
    Part( 13,  18,  36,  49,  -6,   0, RAMP_NEUTRAL, 1, "right arm"),
    Part(-12,  12,  34,  49,  -8,  -5, RAMP_FACTION, 3, "scale-thief's breast"),
    Part(-11,  11,  49,  53,  -7,   2, RAMP_FACTION, 2, "collar"),
    Part( 14,  24,  49,  54, -10,  -7, RAMP_NEUTRAL, 3, "wyrm fang, high hilt"),
    Part( -9,   9,  52,  64,  -5,   3, RAMP_NEUTRAL, 2, "face"),
    Part(-17, -10,  60,  65,   1,   6, RAMP_NEUTRAL, 3, "crest, near spine"),
    Part(-21, -13,  62,  67,   2,   7, RAMP_NEUTRAL, 3, "crest, middle spine"),
    Part( 20,  30,  53,  60, -12,  -8, RAMP_NEUTRAL, 3, "wyrm fang, high blade"),
    Part(-11,  11,  58,  71,  -2,   6, RAMP_NEUTRAL, 1, "hood"),
    Part(-24, -16,  65,  69,   3,   8, RAMP_NEUTRAL, 3, "crest, far spine"),
    Part( -7,   7,  61,  64,  -5,  -3, RAMP_NEUTRAL, 3, "ember eyes"),
    Part( -8,   8,  67,  74,   0,   6, RAMP_NEUTRAL, 1, "hood crown"),
)

# ---------------------------------------------------------------------------
# The dragon
#
# The beast's role, the third quadruped in the library, and the figure the
# rules said would start ahead: a body that runs along x is legible at twenty
# pixels before a single detail is authored. It does start ahead, and the work
# is entirely in separating it from the beast and the xenoform, both of which
# are also long, low and four- or six-legged.
#
# **The wings are the whole answer, and they are the one thing here that costs
# depth.** Four walls, two a side, stepping out and up from the shoulder: the
# same two-box diagonal the sniper's rail refused to be, used in the axis where
# it works, because each box is sixteen and fourteen world units long against a
# twelve-unit step. The beast has nothing above its own back; the xenoform has
# a tail, which is a narrow climbing line; this has two broad faction panels
# standing over the spine, and at twenty pixels that is a completely different
# animal. The outer pair takes the brightest faction rung and the inner pair
# the next, so the V opens *upward in value* as well as in x.
#
# The rest is the beast's structural argument kept because it is right: a
# horizontal body, a faction ridge along the back in plates with real gaps
# between them, and a second faction marker at head height. Here that is the
# throat, under a neck carried **above** the shoulder line rather than level
# with it, which is the other half of what makes this a dragon and not a hound.
#
# The legs are two pairs rather than the sprite's row of four, because a row of
# four along x is a side view and this camera is not one; and the near pair
# takes the scale rung where the far pair takes black, and each pair is offset
# five units in x from the other so all four show; the sixteen world units of
# background between fore and hind is the gap that makes it a body on legs
# rather than a slab. There is deliberately **no belly plate**: the first draft
# had one on the black rung and it drew as a hole punched through the animal,
# which is what a dark box between two lit ones is at this size. The tail
# ends in the sprite's own horn tip, which is also the only pale thing at that
# end of the figure and therefore the thing that says which end is which.
# ---------------------------------------------------------------------------

DRAGON: Tuple[Part, ...] = (
    Part(-21, -13,   0,  27,   4,  10, RAMP_NEUTRAL, 0, "hind leg, far"),
    Part(  3,  11,   0,  27,   4,  10, RAMP_NEUTRAL, 0, "fore leg, far"),
    Part(-16,  -8,   0,  27, -10,  -4, RAMP_NEUTRAL, 1, "hind leg, near"),
    Part(  8,  16,   0,  27, -10,  -4, RAMP_NEUTRAL, 1, "fore leg, near"),
    Part(-32, -24,  14,  23,  -4,   1, RAMP_NEUTRAL, 3, "tail, horn tip"),
    Part(-30, -18,  19,  29,  -5,   1, RAMP_NEUTRAL, 1, "tail, middle"),
    Part(-22, -10,  25,  37,  -6,   3, RAMP_NEUTRAL, 1, "tail, root"),
    Part(-20,  18,  24,  50, -10,   6, RAMP_NEUTRAL, 1, "long body"),
    Part( 12,  22,  38,  54,  -5,   1, RAMP_NEUTRAL, 1, "neck, lower"),
    Part(-22,  -6,  45,  55,  -1,   4, RAMP_FACTION, 2, "left wing, inner"),
    Part(  6,  22,  45,  55,  -1,   4, RAMP_FACTION, 2, "right wing, inner"),
    Part( 10,  16,  41,  52,  -8,  -5, RAMP_FACTION, 3, "throat"),
    Part(-14,  -6,  47,  52,  -6,  -3, RAMP_FACTION, 3, "spine ridge, rear"),
    Part(  0,   8,  47,  52,  -6,  -3, RAMP_FACTION, 3, "spine ridge, middle"),
    Part(-32, -18,  52,  66,   0,   5, RAMP_FACTION, 3, "left wing, outer"),
    Part( 18,  32,  52,  66,   0,   5, RAMP_FACTION, 3, "right wing, outer"),
    Part( 18,  28,  51,  64,  -5,   1, RAMP_NEUTRAL, 1, "neck, upper"),
    Part( 26,  32,  57,  61, -13, -11, RAMP_NEUTRAL, 3, "fangs"),
    Part( 19,  32,  59,  70,  -9,   0, RAMP_NEUTRAL, 1, "skull"),
    Part( 25,  32,  60,  66, -11,  -6, RAMP_NEUTRAL, 1, "muzzle"),
    Part( 21,  26,  65,  68, -10,  -9, RAMP_NEUTRAL, 3, "ember eye"),
    Part( 17,  25,  69,  78,   0,   5, RAMP_NEUTRAL, 3, "horn, left"),
    Part( 25,  33,  69,  82,   0,   5, RAMP_NEUTRAL, 3, "horn, right"),
)

#: This style's commissioned meshes, by archetype name. Complete: all eight
#: roles, drawn from the eight mythical sprites as they stand after the roster
#: review's redraw, and held to their measured silhouettes.
MESHES: Mapping[str, Tuple[Part, ...]] = {
    "knight": DRAKEGUARD,
    "archer": WYRM_HUNTER,
    "mage": RUNECASTER,
    "stormcaller": STORMSINGER,
    "healer": WYRMPRIEST,
    "commander": DRAGONLORD,
    "rogue": SCALETHIEF,
    "beast": DRAGON,
}
