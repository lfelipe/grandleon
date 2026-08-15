# SPDX-License-Identifier: MIT
"""The ``undead`` mesh commission: the eight archetypes as solids.

The fifth style drawn as solids, and the first whose identity is an **absence**.
Its sprite commission concluded that what makes this roster read is the *missing*
skin ramp, and three of its eight silhouettes are drawn out of holes: the
Bonepicker's bow is held off the body so the background between the two is part
of the outline, the Bone hound's waist and ribs let the cell show through, and
the Wraith's body stops with two clear rows under it and one lone mark on the
ground line.

A mesh has no holes. Every part is an opaque solid and the background between
two masses is the only gap there is, so this commission asks the one question
no earlier style could: *does a roster designed around transparency survive a
medium that has none*? The answer, measured on all eight, is **partly, and the
part that survives is the one that can be paid for in x**.

What that cost, in numbers this table was authored against
---------------------------------------------------------
The rules measure a gap at **five world units** across the
figure's width. That number is one axis of three, and the other two follow from
the same projection: screen horizontal keeps all of x, screen vertical keeps
``cos φ`` = 0.50 of y and ``sin φ`` = 0.87 of z. So the same strip of background
costs **5 units in x, 6 in z, and 10 in y**. A gap in height is twice the price
of a gap in width. Measured on the shipped camera's own projection over a forty
unit baseline: 17 px across x, 9 down y, 15 down z.

Every sprite device in this style was re-authored against those three numbers,
and the three answers are different:

* The Bonepicker's **bow gap** is a gap in x and it is affordable twice over.
  The sprite draws six texels of background at the top of the bow, which is
  twelve world units, and one texel lower down, which is two and is gone. The
  model widens the narrow half to eight and crosses it with the bow arm, so the
  one hole the sprite loses becomes two holes the mesh keeps.
* The Bone hound's **ribs** are a gap in x as well, but the sprite draws them at
  a one-texel pitch that is two world units. Authored at the pitch the *rule*
  asks (five-unit bars with five-unit gaps), there is room for four, and the
  ribcage is the one place in the roster where the board is meant to be seen
  through the figure.
* The Wraith's **clear ground is lost**, and this is the finding worth most.
  Rule 1 requires a part at y = 0 and nothing may be authored below it, so a
  figure cannot end above its ground; it can only change what it ends *in*. The
  `scifi` psion escapes by ending in light, a bright flare on its brightest
  neutral rung. A dark part cannot do the same job, because ink against the
  board is a hole and a hole on the ground line reads as a shadow. What is
  authored here instead is a **pool of grave-light**: the cold green this style's
  palette resolves, wide and low, with the shroud stopping thirty units above it.

The palette, which is this style's other measurement
----------------------------------------------------
This is the leanest material list in the library: bone, dark cloth, rust, a
cold green, and deliberately no skin. The short list **helped**. All eight
resolve four *distinct* neutral rungs, where `nature`'s eight resolve ramps on
which whole materials are unreachable. The reason is that ``ink`` runs the full
range of the master palette rather than being the dark ramp its name suggests:
its darkest entry is the darkest colour there is and its brightest outranks
``snow``'s by one step of luminance. On the Wraith, the Mourner and the
Grave-thief ``ink`` therefore supplies **both ends** of the neutral ramp, so a
socket and a bone highlight come out of one material. What did not survive is the
faction ramp: five of the eight carry a three-entry faction list and their rungs
0 and 1 are the same colour, exactly as `nature`'s were.

Two numbers per figure that were read off the machine rather than intended: the
Bone hound cannot reach ``ash_water``'s brightest entry at all (its seven-entry
neutral list samples the first, third, fifth and seventh), so its eye is a
*dark* mark on a bright skull rather than a light, which is the "a face is a
hole with a rim of light" construction arrived at by the palette rather than by
choice. And the Barrow knight, the Bellringer and the Grave-thief all had their
two eye lights four world units apart, which is one pixel and drew as **one
bar**; the knight keeps that as a single lit slit and the other two were opened
to six.
"""

from __future__ import annotations

from typing import Mapping, Tuple

from .rules import RAMP_FACTION, RAMP_NEUTRAL, Part

#: The style these tables are the commission for. Declared rather than taken
#: from the filename, so the registry in :mod:`.` cannot bind a module to the
#: wrong style's sprites. That is the one mistake the silhouette check would
#: not catch, since it would simply hold this Barrow knight to a knight's box.
STYLE = "undead"


# ---------------------------------------------------------------------------
# The Barrow knight
#
# A revenant carrying the lid of its own coffin as a shield. The lid is the
# whole of what separates it from the four other knights, and it is spent in
# the axis this camera reads: a slab taller than the figure, held off the body
# by five world units of background, narrow at both ends so the outline steps
# twice rather than being a rectangle.
#
# The head is the one flat-topped rectangle in the library. Every other helm in
# every style tapers or carries a crest; this one is a box with a dark visor
# band across it, because a barrow knight's head should read as something that
# was closed rather than worn.
#
# What the eye lights measured. They were first authored as two boxes four
# world units apart, which is two pixels of bar and one of background, and they
# drew as a single lit slit. Rather than open them, they are one part, which is
# a part back for the coffin lid: a barrow knight's face should not be two
# sockets, it should be one line of light in a shut visor. The lid's lower
# band went the same way: it drew four pixels of the eight hundred this figure
# paints, because the boards in front of it are drawn later, and the rule that
# an invisible part is still paid for applies to a nearly invisible one.
# ---------------------------------------------------------------------------

BARROW_KNIGHT: Tuple[Part, ...] = (
    Part(-13,  -4,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "left boot"),
    Part(  4,  13,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "right boot"),
    Part(-34, -24,   0,  14, -10,  -4, RAMP_NEUTRAL, 1, "coffin lid, foot"),
    Part(-12,  -5,   6,  40,  -6,   6, RAMP_NEUTRAL, 3, "left shin"),
    Part(  5,  12,   6,  40,  -6,   6, RAMP_NEUTRAL, 3, "right shin"),
    Part(-12,  -4,  38,  58,  -7,   7, RAMP_NEUTRAL, 3, "left thigh"),
    Part(  4,  12,  38,  58,  -7,   7, RAMP_NEUTRAL, 3, "right thigh"),
    Part(-13,  13,  54,  68,  -9,   9, RAMP_FACTION, 1, "grave skirt"),
    Part(-38, -20,  10, 118, -10,  -4, RAMP_FACTION, 3, "coffin lid, boards"),
    Part( 15,  21,  74,  86,  -6,   2, RAMP_NEUTRAL, 3, "bone sword arm"),
    Part(-15,  15,  64, 100, -10,  10, RAMP_FACTION, 2, "faction plate"),
    Part(-38, -20,  74,  84, -12, -10, RAMP_NEUTRAL, 2, "coffin lid, band"),
    Part( -6,   6,  70,  96, -12, -10, RAMP_NEUTRAL, 2, "pitted cuirass"),
    Part( 14,  20,  78, 116,   2,  12, RAMP_NEUTRAL, 2, "sword blade"),
    Part(-15,  -9,  92, 102,  -7,   7, RAMP_NEUTRAL, 1, "left pauldron"),
    Part(  9,  15,  92, 102,  -7,   7, RAMP_NEUTRAL, 1, "right pauldron"),
    Part(-10,  10,  96, 120,  -9,   9, RAMP_NEUTRAL, 1, "flat-topped helm"),
    Part(-10,  10, 102, 110, -11,  -9, RAMP_NEUTRAL, 0, "visor band"),
    Part( -8,   8, 103, 109, -12, -11, RAMP_NEUTRAL, 3, "visor light"),
    Part(-34, -24, 114, 128, -10,  -4, RAMP_NEUTRAL, 1, "coffin lid, head"),
)


# ---------------------------------------------------------------------------
# The Bonepicker
#
# The figure this commission was taken for. Its sprite holds a bow off the body
# so that the background between the two is part of the outline, and the model
# is the only place in the library where a *hole* had to be paid for rather
# than drawn.
#
# The arithmetic, in full, because it is the finding. A cell is thirty-two
# texels across sixty-four world units, so one texel of background is two world
# units and the five-unit threshold is two and a half texels. The sprite draws
# six texels of background at the top of the bow (twelve world units, more
# than twice what is needed) and **one** texel lower down, which is two units
# and is not a gap at all. So the mesh cannot copy the drawing: the narrow half
# is opened to eight units, and the bow arm is thrown across it level at the
# height the sprite crosses it, which turns the one hole the sprite is losing
# into the two holes the mesh keeps. A gap that cannot be afforded at five
# units is a gap that will not be there, and the answer is to widen it or to
# lose it, never to draw it at the sprite's own pitch and hope.
#
# The skull is thrust forward five units and no more. Every muzzle in the
# `nature` commission was first authored ten to twelve units proud of its skull
# and every one of them drew at the chest, because a part thrown forward sinks
# by 0.866 of the throw. The badger's pale mask stripe at z −21 landed sixteen
# world units below the head it belongs to, a quarter of the drawn figure.
# Five or six units is the whole budget a head detail has.
# ---------------------------------------------------------------------------

BONEPICKER: Tuple[Part, ...] = (
    Part( -9,  -3,   0,  42,  -6,   6, RAMP_NEUTRAL, 3, "left shin"),
    Part(  3,   9,   0,  42,  -6,   6, RAMP_NEUTRAL, 3, "right shin"),
    Part(-26, -20,   4,  72,  -6,   2, RAMP_NEUTRAL, 3, "bone bow, lower limb"),
    Part(-11,  11,  38,  58,  -7,   7, RAMP_FACTION, 2, "hip wrap"),
    Part(-13,  13,  24,  84,   4,  10, RAMP_FACTION, 1, "grave cloak"),
    Part( 11,  17,  56,  92,   2,  10, RAMP_NEUTRAL, 1, "quiver"),
    Part(-12,  12,  64,  72,  -9,  -7, RAMP_NEUTRAL, 3, "rib, lower"),
    Part(  6,  12,  62,  84,  -6,   4, RAMP_NEUTRAL, 3, "draw arm"),
    Part(-20,  -6,  68,  76,  -6,   0, RAMP_NEUTRAL, 3, "bow arm, level"),
    Part( -4,   4,  72,  98,  -6,   6, RAMP_NEUTRAL, 3, "spine"),
    Part(-12,  12,  86,  94,  -9,  -7, RAMP_NEUTRAL, 3, "rib, upper"),
    Part( 12,  16,  90, 112,   4,  12, RAMP_NEUTRAL, 3, "loosed shafts"),
    Part(-26, -20,  68, 124,  -6,   2, RAMP_NEUTRAL, 3, "bone bow, upper limb"),
    Part(-11,  11,  96, 102,  -9,   7, RAMP_FACTION, 3, "collarbone"),
    Part( -6,   6,  92, 100, -12,  -6, RAMP_NEUTRAL, 1, "jaw"),
    Part( -9,   9,  98, 120, -12,   2, RAMP_NEUTRAL, 3, "skull, thrust forward"),
    Part( -9,   9, 102, 108, -14, -12, RAMP_NEUTRAL, 0, "sockets"),
    Part( -6,   6, 116, 128,  -8,   4, RAMP_NEUTRAL, 3, "crown"),
)


# ---------------------------------------------------------------------------
# The Wraith
#
# The sprite ends two clear rows above its own base with a single four-texel
# mark on the ground line, and that is the device this whole commission was
# proposed to test. It does not survive, and the reason is a rule rather than a
# drawing: rule 1 puts the feet at y = 0 and there is nowhere below that for a
# figure to stop. A mesh cannot end above its ground. It can only choose what
# it ends in, and three drafts measured what the choices are worth.
#
# The first ended in a dark spike on the darkest rung: the sprite's own mark,
# authored honestly. It drew as a black rectangle standing on the tile, which
# is the ink warning read from the other side: ink against the board is a hole,
# and a hole on the ground line is a shadow, not an absence. The second dropped
# the spike and hung two tatters low instead; two masses at the bottom of a
# humanoid outline are legs whatever they are called.
#
# What is authored is the `scifi` psion's answer in this style's own colour.
# The psion floats by ending in **light**: a bright flare on its brightest
# neutral rung, tapering up into the robe. Light is the one thing a viewer will
# not read as a foot. This style has no white to spare there but it has
# the cold green, which is the only green in the library that is not a plant,
# so the Wraith stands in a low pool of grave-light with a dimmer column rising
# out of it and its shroud beginning twenty world units above the pool's top.
# Nothing about the part changed but its rung, and it stopped being a shadow.
#
# The rest is a taper and two seams. The sprite's widest rows are its *lowest*,
# so the hem is the widest part and the hood the narrowest. The warning that a
# scale which satisfies rule 4 can move the width to the wrong height applies
# to a robe more than to anything else. The two seam boxes are the legless
# figure's vertical, cut one box a mass because a full-height front strip
# averages a low y, sorts far, and is drawn over by everything above it.
# ---------------------------------------------------------------------------

WRAITH: Tuple[Part, ...] = (
    Part( -9,   9,   0,  10,  -5,   5, RAMP_NEUTRAL, 2, "grave light"),
    Part( -4,   4,   8,  34,  -3,   3, RAMP_NEUTRAL, 1, "grave light, column"),
    Part(-20,  -4,  30,  42,  -9,  -7, RAMP_NEUTRAL, 0, "hem tatter, left"),
    Part(  4,  20,  30,  42,  -9,  -7, RAMP_NEUTRAL, 0, "hem tatter, right"),
    Part(-20,  20,  40,  68,  -6,   6, RAMP_FACTION, 2, "shroud, hem"),
    Part( -4,   4,  42,  68,  -8,  -6, RAMP_NEUTRAL, 1, "seam, hem"),
    Part(-14,  14,  66,  94,  -6,   6, RAMP_FACTION, 3, "shroud, body"),
    Part( -4,   4,  68,  94,  -8,  -6, RAMP_NEUTRAL, 1, "seam, body"),
    Part(-11,  11,  78,  84,  -9,  -7, RAMP_NEUTRAL, 3, "rib bar"),
    Part(-10,  10,  92,  98,  -8,  -6, RAMP_NEUTRAL, 1, "collar"),
    Part(-10,  10,  96, 118,  -6,   6, RAMP_FACTION, 1, "cowl"),
    Part( -9,   9, 102, 112,  -8,  -6, RAMP_NEUTRAL, 0, "hollow"),
    Part( -8,  -3, 104, 110, -10,  -8, RAMP_NEUTRAL, 2, "eye light, left"),
    Part(  3,   8, 104, 110, -10,  -8, RAMP_NEUTRAL, 2, "eye light, right"),
    Part( -5,   5, 116, 128,  -4,   4, RAMP_FACTION, 1, "hood point"),
)


# ---------------------------------------------------------------------------
# The Bellringer
#
# Width taken from an object rather than from a body: a level yoke carried
# across the shoulders with a heavy lobe hanging at each end. It cost four
# drafts, and the three that failed each failed for a reason an earlier
# commission had already written down.
#
# The first hung the bells beside the torso as tall slabs running from the hem
# to the shoulder. A vertical mass beside a body is an arm (the `mythical`
# stormsinger's finding), and two of them are pauldrons, which is what they
# drew as. The second moved them down to the hips where the sprite's
# widest rows put them and stood a level bar over the head; a wide horizontal
# plate directly above a head is a hat brim, because this camera draws a plate
# as its top face at full light and there was nothing above it to say
# otherwise. The third put the bar at the waist, which cured the brim and left
# the lobes touching the robe.
#
# What ships hangs the lobes at the height the sprite's own widest rows put
# them, on straps that climb to the ends of a level yoke: thirty world units of
# board under each lobe, five between it and the robe, and the head standing
# thirty-six above the bar. That is the one arrangement in which a mass beside
# a figure is neither an arm nor a shoulder, because nothing else in the roster
# has a solid with background on three sides of it. Each lobe is capped below
# by a wider, darker lip, so the profile widens downward the way a bell does
# and does not read as a box.
#
# The fourth draft was the machine's, and it is the finding to keep. Its bells
# were carried high, beside the head, and the scratch's matched-width check
# refused the figure at 22 px against a 19 px sprite on the far row: the
# model's widest part was at the shoulders where the *sprite's* widest row is
# at the hips, and it was also authored three world units nearer the eye than
# the body's centre, which draws it wider than a width scale solved from the
# authored number allows for. **The widest part of a figure has to be at the
# right depth as well as at the right height.** Moved down and centred on z =
# 0, with the yoke narrowed from ±31 to ±27, the far row measures 21 px and the
# check passes.
#
# The head is pale and continuous: a hood and its peak on the same rung, which
# is what stops a hood being a stovepipe. Its two eye lights are six units
# apart, opened from the four that drew as one bar.
# ---------------------------------------------------------------------------

BELLRINGER: Tuple[Part, ...] = (
    Part(-11,  -4,   0,  26,  -6,   6, RAMP_NEUTRAL, 2, "left shin"),
    Part(  4,  11,   0,  26,  -6,   6, RAMP_NEUTRAL, 2, "right shin"),
    Part(-13,  13,  18,  24,  -9,  -7, RAMP_NEUTRAL, 1, "hem lip"),
    Part(-30, -18,  30,  36,  -8,   8, RAMP_NEUTRAL, 1, "bell lip, left"),
    Part( 18,  30,  30,  36,  -8,   8, RAMP_NEUTRAL, 1, "bell lip, right"),
    Part(-13,  13,  22,  64,  -7,   7, RAMP_FACTION, 2, "grave robe"),
    Part(-28, -18,  34,  62,  -6,   6, RAMP_NEUTRAL, 3, "bell, left"),
    Part( 18,  28,  34,  62,  -6,   6, RAMP_NEUTRAL, 3, "bell, right"),
    Part(-11,  11,  50,  56, -10,  -8, RAMP_NEUTRAL, 3, "rib, lower"),
    Part(-27, -21,  60,  86,  -5,   1, RAMP_NEUTRAL, 1, "bell strap, left"),
    Part( 21,  27,  60,  86,  -5,   1, RAMP_NEUTRAL, 1, "bell strap, right"),
    Part(-11,  11,  60,  90,  -6,   6, RAMP_FACTION, 3, "shroud torso"),
    Part(-10,  10,  78,  84,  -9,  -7, RAMP_NEUTRAL, 3, "rib, upper"),
    Part(-27,  27,  84,  92,  -4,   4, RAMP_NEUTRAL, 2, "yoke, level"),
    Part( -9,   9,  88,  94,  -8,  -6, RAMP_NEUTRAL, 1, "collar"),
    Part(-10,  10,  92, 116,  -6,   6, RAMP_NEUTRAL, 2, "cowl"),
    Part( -9,   9, 100, 112,  -8,  -6, RAMP_NEUTRAL, 0, "hollow"),
    Part( -8,  -3, 103, 109, -10,  -8, RAMP_NEUTRAL, 3, "eye light, left"),
    Part(  3,   8, 103, 109, -10,  -8, RAMP_NEUTRAL, 3, "eye light, right"),
    Part( -6,   6, 114, 128,  -4,   4, RAMP_NEUTRAL, 2, "cowl peak"),
)


# ---------------------------------------------------------------------------
# The Mourner
#
# An upward trapezoid with no shoulder line and no head. The figure read first
# and needed no second draft, and it is worth saying why, because it is the
# cheapest thing in this commission and the most legible.
#
# It is one continuous mass. A veil pooled on the ground, a body, and a crown,
# each a little narrower than the one below and each on the same rung, so the
# outline is a taper rather than three boxes; the only breaks in it are the
# things that carry the archetype. The warning about a legless robe is
# answered by the stole, cut into **three** boxes (one on the hem, one on the
# body, one on the veil), because a full-height strip authored as a single box
# averages a low y, sorts far by `z·cos φ − y·sin φ`, and loses its upper two
# thirds behind every mass above it. Each box is two units prouder than the
# mass it fronts.
#
# The face is the "hole with a rim of light" construction: a dark hollow with
# two cold green lights in it and a green brow band above, and nothing else.
# The lamp is carried out and down at arm's length on the left, which is the
# only thing in the figure spent in x, and it is what separates a Mourner from
# a Wraith at twenty-five pixels.
# ---------------------------------------------------------------------------

MOURNER: Tuple[Part, ...] = (
    Part(-16,  16,   0,  40,  -7,   7, RAMP_NEUTRAL, 3, "veil, pooled"),
    Part( -6,   6,   4,  40,  -9,  -7, RAMP_FACTION, 3, "stole, on the hem"),
    Part(-26, -18,  30,  38,  -6,  -2, RAMP_NEUTRAL, 1, "lamp hook"),
    Part(-22, -16,  36,  46,  -6,   0, RAMP_NEUTRAL, 3, "bone hand"),
    Part(-18, -12,  44,  62,  -5,   3, RAMP_NEUTRAL, 3, "arm, out and down"),
    Part(-13,  13,  38,  76,  -7,   7, RAMP_NEUTRAL, 3, "veil, body"),
    Part(-30, -20,  52,  66,  -8,   0, RAMP_NEUTRAL, 1, "lamp cage"),
    Part( -5,   5,  40,  76,  -9,  -7, RAMP_FACTION, 2, "stole, on the body"),
    Part(-28, -22,  55,  63, -10,  -8, RAMP_NEUTRAL, 2, "lamp light"),
    Part(-10,  10,  74, 102,  -6,   6, RAMP_NEUTRAL, 3, "veil, shoulderless"),
    Part(-10,  10,  84,  90,  -8,  -6, RAMP_FACTION, 2, "brow band"),
    Part( -4,   4,  76, 100,  -8,  -6, RAMP_FACTION, 3, "stole, on the veil"),
    Part( -7,   7,  92, 104,  -8,  -6, RAMP_NEUTRAL, 0, "hollow"),
    Part( -7,  -3,  95, 101, -10,  -8, RAMP_NEUTRAL, 2, "eye light, left"),
    Part(  3,   7,  95, 101, -10,  -8, RAMP_NEUTRAL, 2, "eye light, right"),
    Part( -6,   6, 100, 128,  -5,   5, RAMP_NEUTRAL, 3, "veil, crown"),
)


# ---------------------------------------------------------------------------
# The Barrow lord
#
# The figure that meets the cape finding head on: *a cape cannot hang behind a
# figure at this pitch, it haloes it*, because screen height rises with
# `0.866·z` exactly as it rises with `0.5·y` and a slab behind the shoulders is
# drawn above the head. The sprite gives this one a cape **and** three torn
# banner rags, which is the same trap four times over.
#
# The cape is answered by stopping it below the shoulder. It runs from y 4 to
# 78 at z 4 to 10, so its highest drawn point is `0.5·78 + 0.866·10` = 47.7
# against the skull's `0.5·122 + 0.866·7` = 67.1: nineteen world units of
# screen clear above it, and the cape reads as cloth behind the legs rather
# than as a collar around the head.
#
# The rags are the same arithmetic used deliberately. They are the one feature
# in the figure that is *meant* to be above the head, so they are thrown
# further back (z 8 to 18) and the highest of them tops the figure at 77.6
# against the skull's 67.1 without one of them being stacked in y. That is the
# finding that screen height is bought in z rather than in y, and it is the
# whole reason the banner pole did not have to be authored in front of the
# shoulder and lose.
#
# The circlet leans: three parts at three heights and three widths, none of
# them centred on the skull, so the crown is broken rather than a ring.
# ---------------------------------------------------------------------------

BARROW_LORD: Tuple[Part, ...] = (
    Part(-13,  -4,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "left boot"),
    Part(  4,  13,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "right boot"),
    Part(-12,  -5,   6,  58,  -6,   6, RAMP_NEUTRAL, 3, "left leg"),
    Part(  5,  12,   6,  58,  -6,   6, RAMP_NEUTRAL, 3, "right leg"),
    Part(-18,  18,   4,  78,   4,  10, RAMP_FACTION, 1, "grave cape"),
    Part(-14,  14,  54,  70,  -9,   9, RAMP_FACTION, 2, "grave skirt"),
    Part( 16,  22,   6, 128,   4,  10, RAMP_NEUTRAL, 3, "banner pole"),
    Part(-20, -14,  30,  92,  -8,  -2, RAMP_NEUTRAL, 1, "polearm shaft"),
    Part( 22,  28,  70,  80,   8,  18, RAMP_FACTION, 1, "banner rag, lowest"),
    Part(-13,  13,  64,  70, -11,  -9, RAMP_NEUTRAL, 2, "rusted belt"),
    Part(-15,  15,  68, 100, -10,  10, RAMP_FACTION, 3, "faction plate"),
    Part( 22,  30,  92, 102,   8,  18, RAMP_FACTION, 2, "banner rag, middle"),
    Part( -7,   7,  74,  96, -12, -10, RAMP_NEUTRAL, 1, "pitted cuirass"),
    Part(-17,  -9,  88,  98,  -7,   7, RAMP_NEUTRAL, 1, "left pauldron"),
    Part(  9,  17,  88,  98,  -7,   7, RAMP_NEUTRAL, 1, "right pauldron"),
    Part(-24, -16,  88, 112,  -8,  -2, RAMP_NEUTRAL, 2, "polearm head"),
    Part(-11,  11,  96, 122,  -7,   7, RAMP_NEUTRAL, 3, "bare skull"),
    Part( 22,  32, 114, 124,   8,  18, RAMP_FACTION, 3, "banner rag, highest"),
    Part(-10,  10, 104, 111,  -9,  -7, RAMP_NEUTRAL, 0, "sockets"),
    Part(-10,  10, 120, 126,  -4,   4, RAMP_NEUTRAL, 2, "broken circlet"),
    Part( -4,   2, 120, 128,  -4,   4, RAMP_NEUTRAL, 2, "circlet prong, centre"),
    Part(  6,  12, 122, 128,  -3,   3, RAMP_NEUTRAL, 2, "circlet prong, right"),
)


# ---------------------------------------------------------------------------
# The Grave-thief
#
# The crouch is stated in x and nowhere else, which is the `medieval` rogue's
# finding applied a second time: rule 1 builds every figure `unit_world / cos
# φ` tall whatever it is doing, so a crouch authored in y is a knight that has
# been sat on. The legs are braced eight units wider apart than any other
# upright in this style, and the two picks are hung at opposite corners, one
# low and left, one high and right, so the figure carries a diagonal that no
# box has to step.
#
# The low pick was first authored across the left leg, at the same x and two
# units nearer the eye. It hid it: the leg drew six pixels against the right
# leg's forty-six, and the wide stance the whole figure is built on was visible
# on one side only. It is now outboard of the leg entirely, which costs four
# world units of authored width and is inside tolerance.
#
# The cowl has two lights and no face, and the ribs are the one thing in the
# figure on ``ink``'s brightest rung, which on this archetype is the brightest
# neutral there is, a near-white the sprite's own bone is drawn in. Two bars on
# a faction torso is what makes this a skeleton at twenty-five pixels and not a
# hooded man.
# ---------------------------------------------------------------------------

GRAVE_THIEF: Tuple[Part, ...] = (
    Part(-22, -14,   0,  38,  -6,   6, RAMP_NEUTRAL, 3, "left leg, braced wide"),
    Part( 14,  22,   0,  38,  -6,   6, RAMP_NEUTRAL, 3, "right leg, braced wide"),
    Part(-32, -24,  12,  20, -14,  -8, RAMP_NEUTRAL, 2, "low pick, hook"),
    Part(-28, -22,  18,  34, -12,  -6, RAMP_NEUTRAL, 1, "low pick, haft"),
    Part(-16,  16,  10,  64,   4,  10, RAMP_NEUTRAL, 1, "grave cloak"),
    Part(-13,  13,  36,  54,  -8,   4, RAMP_FACTION, 2, "thief's kilt"),
    Part(-22, -14,  38,  54,  -6,   2, RAMP_NEUTRAL, 3, "left arm, down"),
    Part( -8,   8,  54,  70, -11,  -9, RAMP_NEUTRAL, 1, "shroud wrap, lower"),
    Part(-12,  12,  52,  88,  -9,   7, RAMP_FACTION, 3, "faction torso"),
    Part( -9,   9,  63,  69, -13, -11, RAMP_NEUTRAL, 3, "rib, lower"),
    Part( 14,  22,  66,  82,  -6,   2, RAMP_NEUTRAL, 3, "right arm, up"),
    Part( -8,   8,  68,  88, -11,  -9, RAMP_NEUTRAL, 1, "shroud wrap, upper"),
    Part( -9,   9,  80,  86, -13, -11, RAMP_NEUTRAL, 3, "rib, upper"),
    Part( 14,  24,  94, 108,   6,  14, RAMP_NEUTRAL, 1, "hood point, swept"),
    Part(-10,  10,  88,  94, -10,  -8, RAMP_FACTION, 3, "collar band"),
    Part( 20,  28,  84, 100, -12,  -6, RAMP_NEUTRAL, 1, "high pick, haft"),
    Part( -9,   9,  92, 128,  -6,   8, RAMP_NEUTRAL, 1, "cowl"),
    Part( 22,  30,  98, 110, -18, -12, RAMP_NEUTRAL, 2, "high pick, hook"),
    Part( -9,   9, 104, 118,  -8,  -6, RAMP_NEUTRAL, 0, "hollow"),
    Part( -8,  -3, 108, 114, -10,  -8, RAMP_NEUTRAL, 3, "eye light, left"),
    Part(  3,   8, 108, 114, -10,  -8, RAMP_NEUTRAL, 3, "eye light, right"),
)


# ---------------------------------------------------------------------------
# The Bone hound
#
# The only figure in the style that is not an upright, and the `nature`
# finding says exactly what that is worth: *horizontal beats standing*, and an
# upright animal is a humanoid to this camera. This one is horizontal, so it
# differs from its own seven for free and had to spend its work on differing
# from the other three styles' quadrupeds instead.
#
# The ribcage is the roster's one true hole. The sprite lets the cell show
# through at the waist under a thin bar, at a one-texel pitch that is two world
# units and is nothing at all; authored at the pitch the *gap* rule asks
# (five-unit bars with five-unit gaps between them), four fit across the barrel,
# and they hang in front of a midsection with the haunch at one end and the
# shoulder at the other and **nothing in between**. So the board is seen
# through the animal, which is the thing this whole commission was proposed to
# find out whether a solid could do. It can, once, and only because the gap is
# spent in x.
#
# The far pair of legs was dropped. They drew four and seven pixels of the five
# hundred this figure paints, because at sixty degrees the near pair stands
# directly in front of them, and the `mythical` drakeguard measured what a part
# no camera can show costs: two invisible arms took its sixteen-unit cache
# rebuild to 34,872 µs against a thirtieth of a second's 33,333, and dropping
# them brought it back to 32,182. The band is a ceiling and not a target, and
# the parts to spend it on are the ones a viewer can see. A quadruped at this
# camera has two legs, and the two it has are longer for it.
#
# The eye is the palette's decision rather than the drawing's. This archetype's
# neutral list is seven entries and the four rungs land on the first, third,
# fifth and seventh, so ``ash_water``'s brightest (the cold green every other
# figure in this style puts in its sockets) is **unreachable** here. The eye
# is therefore a dark mark on a bright skull rather than a light in a dark one,
# which is the same "hole with a rim of light" construction arrived at from the
# other side.
# ---------------------------------------------------------------------------

BONE_HOUND: Tuple[Part, ...] = (
    Part(-15,  -8,   0,  46, -13,  -5, RAMP_NEUTRAL, 2, "hind leg"),
    Part(  8,  15,   0,  46, -13,  -5, RAMP_NEUTRAL, 2, "fore leg"),
    Part(-26, -18,  46,  56,  -6,   2, RAMP_NEUTRAL, 2, "tail"),
    Part(-22, -12,  42,  68,  -9,   5, RAMP_NEUTRAL, 2, "haunch"),
    Part(-11,  -6,  38,  62, -13, -10, RAMP_NEUTRAL, 3, "rib, first"),
    Part( -1,   4,  38,  62, -13, -10, RAMP_NEUTRAL, 3, "rib, second"),
    Part(  9,  14,  38,  62, -13, -10, RAMP_NEUTRAL, 3, "rib, third"),
    Part( 19,  24,  38,  62, -13, -10, RAMP_NEUTRAL, 3, "rib, fourth"),
    Part( 20,  30,  44,  70,  -9,   5, RAMP_NEUTRAL, 2, "shoulder"),
    Part(-29, -24,  52,  64,  -6,   2, RAMP_FACTION, 2, "tail tip"),
    Part(-16,  26,  60,  70,  -8,   2, RAMP_NEUTRAL, 3, "spine"),
    Part(-10,  -2,  68,  78,  -6,   0, RAMP_FACTION, 3, "spine ridge, rear"),
    Part(  4,  12,  68,  78,  -6,   0, RAMP_FACTION, 3, "spine ridge, fore"),
    Part( 22,  30,  66,  98,  -6,   2, RAMP_NEUTRAL, 3, "neck"),
    Part( 21,  29,  76,  84, -10,  -4, RAMP_FACTION, 3, "collar"),
    Part( 25,  35,  88,  98, -14,  -6, RAMP_NEUTRAL, 2, "long muzzle"),
    Part( 26,  35,  91,  95, -16, -14, RAMP_NEUTRAL, 0, "jaw gap"),
    Part( 18,  32,  92, 114,  -8,   2, RAMP_NEUTRAL, 3, "skull"),
    Part( 20,  25,  98, 104, -10,  -8, RAMP_NEUTRAL, 1, "eye socket"),
    Part( 18,  26, 106, 128,   0,  10, RAMP_NEUTRAL, 2, "skull crest, swept"),
)

#: This style's commissioned meshes, by archetype name.
MESHES: Mapping[str, Tuple[Part, ...]] = {
    "knight": BARROW_KNIGHT,
    "archer": BONEPICKER,
    "mage": WRAITH,
    "stormcaller": BELLRINGER,
    "healer": MOURNER,
    "commander": BARROW_LORD,
    "rogue": GRAVE_THIEF,
    "beast": BONE_HOUND,
}
