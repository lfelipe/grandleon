# SPDX-License-Identifier: MIT
"""The ``sengoku`` mesh commission: the eight roles in the Warring States.

This setting earns its place on a claim about silhouettes: its vocabulary is
**silhouette-first**, a kabuto's crest and a straw hat and a fox's ears being
shapes before they are pictures. (``tools/placeholder_art/README.md`` holds the
screen a candidate style has to pass, separation included.) A sprite roster is not a
test of that claim and a mesh roster is: the camera at sixty degrees keeps a
part's top face and its front face and throws the rest away, so a device that
survives the reduction to a solid is a silhouette and one that does not was a
picture all along.

Three of the eight walk into traps :mod:`.rules` already names, which is why
this commission is worth reading rather than skimming.

**The Onmyoji's eboshi is the pointed-hat case, and it is one wall.** The
`medieval` `mage` was refused after passing every mechanical rule because a
feature stacked in
y draws as a staircase of bright top faces, and gave the test that decides it: a
part is a *wall* when ``dy > dz·tan φ`` and a *plate* when it is not. A tall
lacquered cap is therefore a **single** box seven units deep and twenty-two
tall (twenty-two against 12.1) and not a taper. What the taper would have
bought is bought instead by what stands under it: rung 2 of this archetype
resolves to a warm mid skin and the cap to near-black, so the cap is a dark
spike on a lit head rather than a dark shape on the board.

**The Daimyo's sashimono is the raised-banner case, and the `medieval`
commander had already solved
it.** Screen height is bought in z: a unit of depth draws ``tan φ`` = 1.73 times
taller than a unit of y *and* arrives as a top face lit at 255. The banner is
authored **behind** the shoulder. The medieval commander's pole was authored in
front and its tip landed below its own helm. It is a flat plate in x–z, because
the plane this camera keeps most of is the horizontal one. It costs
sixteen units of z, and depth is charged against the whole figure's height, so
the rest of this model is held inside twenty-two.

**The Shinobi's sword is the diagonal case, and the sprite had already given
up.** A five-step diagonal steps four pixels at a time at
board size and reads as rubble, and that a diagonal is affordable in two boxes.
This sprite draws its scabbard corner to corner *before the body*, so only its
two ends show. That is the two-box answer arrived at from the drawing rather
than from the arithmetic. The mesh keeps exactly those two ends, a mid-grey
chape thrown out low on the left and a bright tsuba with a faction-wrapped grip
above the right shoulder, and lets the figure carry the diagonal the weapon
cannot.

The palette, measured rather than intended
------------------------------------------
A mesh's two ramps resolve from its own sprite's CLUT and the four rungs
sample that list by luminance, so a colour the drawing uses can simply be
unreachable. This style paints in lacquer, paper and cloth, and the sampling is
tight. What the eight actually got, on faction colour zero:

===========  ===========  ===========  ===========  ===========
figure       rung 0       rung 1       rung 2       rung 3
===========  ===========  ===========  ===========  ===========
Samurai      ink 0        wood 0       wood 2       skin 2
Yumi archer  ink 0        wood 1       sand 0       snow 2
Onmyoji      ink 0        wood 1       skin 1       snow 2
Kagura       ink 0        gold 0       skin 2       ink 3
Temple monk  ink 0        skin 0       skin 2       ink 3
Daimyo       ink 0        ink 2        skin 1       steel 3
Shinobi      ink 0        ink 1        steel 2      steel 3
Shrine fox   ink 0        dirt 1       dirt 3       ink 3
===========  ===========  ===========  ===========  ===========

Four of those changed a figure, and three of the four are a **material the
sprite spends and the mesh cannot reach**:

* The **Samurai** reaches no steel at all. Its neutral list is nine entries and
  the rungs land on the first, third, sixth and ninth, which puts steel 2
  eighth and out; so the brightest thing in the model is pale skin, and the
  naginata's blade and the figure's face wear the same rung. They are at
  opposite ends of the figure, which is the whole of why that is survivable.
* The **Daimyo** reaches no gold, and gold is what its sprite states rank in:
  the mon on the sashimono, the maedate over the brow, the studs on its sode.
  Every one of those is the near-white top of ``steel`` here instead, which
  turns out to be the better mark: the maedate is a bright wall on the front of
  a black helm, and nothing in the figure is drawn over it.
* The **Temple monk** reaches neither the wood of its shakujo nor the gold of
  its rings. Its rung 1 is ``skin 0``, a dark warm brown, and that is what the
  staff is made of. It reads as wood by accident and is recorded as an
  accident. Its rings are the paper rung.
* The **Kagura dancer** is the one figure whose gold *is* sampled, at rung 1,
  where the other seven land on a brown, a skin or a near-black. The suzu it
  shakes is therefore the only gold in the whole commission.

Two more facts the sampling forced. The **Temple monk** and the **Shrine fox**
both resolve a three-entry faction ramp, so their rungs 0 and 1 are the same
blue and a faction mark on this style's pale figures has three values and not
four.

And the one worth an author's attention: **rung 0 is `ink 0` for all eight**.
`ink` is the darkest entry of every one of these CLUTs, so the darkest rung is
not a style axis at all here: it is one near-black shared by the whole roster,
and every plate any of these figures wears in it is the same colour as every
other figure's. Only **two** of the eight reach a *second* step of it and can
therefore shade black against black: the Daimyo, whose rung 1 is `ink 2`, and
the Shinobi, whose rung 1 is `ink 1`. The other six have one dark and must part
two dark masses with something that is not dark. That is why the Samurai states
itself with what is *laid on* the lacquer, laced edges and a bright band and a
pale crest, rather than with the lacquer's own shading, which its sprite has and
its mesh cannot. Three figures reach `ink 3` at rung 3: the Kagura dancer, the
Temple monk and the Shrine fox. That is not lacquer at all but the paper white
at the top of the same ramp.
"""

from __future__ import annotations

from typing import Mapping, Tuple

from .rules import RAMP_FACTION, RAMP_NEUTRAL, Part

#: The style these tables are the commission for. Declared rather than taken
#: from the filename, so the registry in :mod:`.` cannot bind a module to the
#: wrong style's sprites. That is the one mistake the silhouette check would
#: not catch, since it would hold this Samurai to a knight's box.
STYLE = "sengoku"


# ---------------------------------------------------------------------------
# The Samurai
#
# The knight's role, and the fifth figure in the library to stand in that
# place. The other four are a torso with a shield on one side and a weapon on
# the other: a knight, a trooper, a drakeguard, a badger guard. The separation
# here is that this one **has no shield at all**. What it has instead is two
# things none of the others can spend:
#
# * The **naginata is held level across the whole width**, at x -32 to 22 with
#   its blade carrying on to 33. Six units of height against six of depth makes
#   it a plate by the wall test, which is exactly what is wanted: a bar whose
#   top face is lit at 255 streaking the entire figure. It is the sprite's own
#   read, and it costs the figure its whole authored width: 65 world units
#   against the 64 this sprite's silhouette asks, which is the knight's own
#   +1 in §5.5 arrived at again. The sprite fills its cell edge to edge, 32
#   texels of 32, and nothing but the haft is why.
# * The **sode are two hard squares at the shoulders**, wider than the torso,
#   each capped by a faction plate along its top edge. Every other style's
#   shoulder is a spike, a pauldron or a curve; a rectangle is the silhouette
#   device this style's `plate` primitive exists for, and at this camera the
#   cap is a top face at full light, so what a viewer gets is two bright blue
#   rectangles either side of a black head. That is the faction ramp doing the
#   work asked of it: large, contiguous, and where the eye already is.
#
# The head is the palette's problem stated plainly. There is no steel in this
# archetype's rungs, so the sprite's pale brim over the brow cannot be built;
# what replaces it is the **face itself**, on the brightest rung there is, set
# proud of a shikoro that flares wider than it on both sides and under a kabuto
# drawn over its top. A pale block inside dark on three sides is "a face
# is a hole with a rim of light" run the other way round, and it is the only
# construction the sampling leaves.
#
# The two darks are deliberately different rungs: the kabuto on rung 0's
# near-black, the shikoro on rung 1's dark wood. Two large masses on one rung
# which touch are one mass. The `mythical` drakeguard measured it:
# its shield reaches x -29 where its coat stops at -12, three units of overlap
# and nowhere to put the five a gap needs, and the first draft lost the shield
# inside the coat entirely until the ramp's own value parted them. A helm
# sitting straight on its own neck guard is that case with no room at all. This
# archetype has one step of lacquer and no second, so the neck guard has to
# borrow a colour from somewhere else in its own list or disappear into the
# helm.
# ---------------------------------------------------------------------------

SAMURAI: Tuple[Part, ...] = (
    Part(-16,  -6,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "left sabaton"),
    Part(  6,  16,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "right sabaton"),
    Part(-14,  -6,   8,  44,  -6,   6, RAMP_NEUTRAL, 1, "left suneate"),
    Part(  6,  14,   8,  44,  -6,   6, RAMP_NEUTRAL, 1, "right suneate"),
    Part(-17,  17,  42,  62,  -9,   9, RAMP_FACTION, 1, "kusazuri"),
    Part(-13,  13,  60, 100,  -9,   9, RAMP_FACTION, 2, "do"),
    Part(-32,  22,  74,  80, -16, -10, RAMP_NEUTRAL, 2, "naginata haft, level"),
    Part( 22,  33,  79,  87, -16, -10, RAMP_NEUTRAL, 3, "naginata blade"),
    Part(-24, -13,  84, 104,  -8,   8, RAMP_NEUTRAL, 1, "left sode"),
    Part( 13,  24,  84, 104,  -8,   8, RAMP_NEUTRAL, 1, "right sode"),
    Part(-13,  13,  88,  94, -12, -10, RAMP_FACTION, 3, "odoshi lacing"),
    Part(-14,  14,  94, 106,  -6,   8, RAMP_NEUTRAL, 1, "shikoro"),
    Part(-24, -13, 104, 108,  -8,   8, RAMP_FACTION, 3, "left sode, laced edge"),
    Part( 13,  24, 104, 108,  -8,   8, RAMP_FACTION, 3, "right sode, laced edge"),
    Part( -9,   9,  96, 116, -12,  -2, RAMP_NEUTRAL, 3, "face"),
    Part(-11,  11, 110, 128,  -8,   8, RAMP_NEUTRAL, 0, "kabuto"),
)

# ---------------------------------------------------------------------------
# The Yumi archer
#
# The archer's role, and the fifth line in the library. The medieval archer's
# bow is a tall symmetric vertical beside the body, the sniper's rail is a
# level bar at the waist, the wyrm-hunter's prod is level at the chest, and the
# nature cat's bow is deliberately the medieval construction again; this one is
# a vertical too, and what keeps it from being either of those is that **a yumi
# is gripped a third of the way up**. It is authored as two boxes on one line:
# a short lower limb from y 24 to 58 and a long upper limb from 56 to 128, the
# upper one both wider and set five units further out. The grip therefore sits
# at 45 per cent of the figure and the asymmetry is in the silhouette rather
# than in the shading. Both are walls by the wall test, 34 and 72 units of
# height against six of depth, and both are offset **back** in z, which is what
# the medieval bow does and the reason it survived where a pointed hat did not.
#
# The **jingasa is a plate, and deliberately the thinnest one that reads.** The
# `medieval` storm brazier measured that a bowl authored as a bowl draws as a
# table: two drafts of that bowl spent eighteen to twenty-two units of z and
# both came out a counter, because depth is
# charged against the whole figure's height; a straw hat wants to be a disc
# seen from above and gets five units of y and sixteen of z, which is a bright
# wide top face and almost no front. It is also the only wide thing on this
# figure, so it does the work the medieval archer's quiver and hood share.
#
# The palette gives this archetype no skin. Its neutral list is eleven entries
# and the rungs land on the first, fourth, seventh and eleventh, which takes
# ink, wood, straw and paper and leaves both skin steps out. The face is
# therefore the **wood** rung, a dark warm block under the hat's bright brim.
# That is what a face under a wide hat is at twenty-five pixels anyway, and the
# separation is by light rather than by hue: the brim is a top face at 255 and
# the face is a front face at 178.
# ---------------------------------------------------------------------------

YUMI_ARCHER: Tuple[Part, ...] = (
    Part(-13,  -4,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "left sabaton"),
    Part(  4,  13,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "right sabaton"),
    Part(-12,  -5,   8,  44,  -6,   6, RAMP_FACTION, 1, "left shin"),
    Part(  5,  12,   8,  44,  -6,   6, RAMP_FACTION, 1, "right shin"),
    Part(-22, -18,  24,  58,   2,   8, RAMP_NEUTRAL, 1, "yumi, lower limb"),
    Part(-14,  14,  42,  58,  -8,   8, RAMP_FACTION, 1, "haidate"),
    Part( 13,  19,  60,  92,   6,  12, RAMP_NEUTRAL, 1, "ebira"),
    Part(-16, -12,  60,  84,  -6,   6, RAMP_FACTION, 1, "left sleeve"),
    Part( 12,  16,  60,  84,  -6,   6, RAMP_FACTION, 1, "right sleeve"),
    Part(-12,  12,  56,  94,  -8,   8, RAMP_FACTION, 2, "do"),
    Part(-12,  12,  72,  78, -11,  -9, RAMP_FACTION, 3, "obi"),
    Part(-27, -20,  56, 128,   2,   8, RAMP_NEUTRAL, 1, "yumi, upper limb"),
    Part( 13,  19,  92, 104,   8,  14, RAMP_NEUTRAL, 3, "fletching"),
    Part( -8,   8,  88, 108, -12,  -2, RAMP_NEUTRAL, 1, "face"),
    Part(-15,  15, 104, 109,  -8,   8, RAMP_NEUTRAL, 2, "jingasa, brim"),
    Part( -7,   7, 109, 116,  -6,   6, RAMP_NEUTRAL, 2, "jingasa, crown"),
)

# ---------------------------------------------------------------------------
# The Onmyoji
#
# The mage's role, the narrowest figure in this style at nineteen texels of
# sprite against the Samurai's thirty-two, and the one that had to be drawn
# against the most expensive finding in the library.
#
# **The eboshi is one wall.** Twenty-two units of y against seven of z is
# twenty-two against 12.1, so it is a wall by the test and shows more front
# than top; authored as the taper the sprite draws it would have been the
# refused mage's ziggurat, and widening its base would have made it a plate.
# There is nothing else to say about the shape, and that is the point of the
# finding: the whole of the answer is "do not build it out of more than one
# box".
#
# What the shape cannot do, the value does. Rung 0 here is near-black and rung
# 2 a warm mid skin, and the cap sits directly on the face with a paper cord
# between them, so the mark is a dark spike **on a lit head**, not a dark
# shape on the board. The reverse case was recorded as a cost: the nature
# stoat's black tail tip was dropped because an ink-rung part outboard of a
# figure is drawn against the board, where it is a gap rather than a mark. A
# cap resting on a head is inboard, and the head is what keeps it a cap.
#
# The robe is legless and therefore a stack of slabs unless something runs
# vertically, and it takes the standing answer twice over: the sprite's bright
# faction fold down the centre is authored as **one box per mass**, one
# fronting the skirt and one fronting the chest, each a little prouder than the
# mass it fronts. Authored as one box from hem to collar it would have averaged
# a low y, sorted far, and lost its upper two thirds behind the chest. That is
# exactly what happened to three plackets in the `nature` commission.
#
# The shaku and its ofuda are the second mark and they are set **back**, at z 2
# to 10, for the reason the bow is: a part behind the shoulder rises for free
# at this pitch. The paper is one mass rather than the sprite's two strips,
# because two strips of a plausible width leave three world units between them,
# and a gap is decoration under four units and structure at five.
# ---------------------------------------------------------------------------

ONMYOJI: Tuple[Part, ...] = (
    Part(-14,  14,   0,  58,  -8,   8, RAMP_FACTION, 1, "robe, skirt"),
    Part( -4,   4,   4,  58, -11,  -8, RAMP_FACTION, 3, "fold, skirt"),
    Part(-16, -11,  46,  54,  -9,  -3, RAMP_NEUTRAL, 2, "left hand"),
    Part( 11,  16,  46,  54,  -9,  -3, RAMP_NEUTRAL, 2, "right hand"),
    Part(-16, -11,  52,  76,  -6,   6, RAMP_FACTION, 1, "left sleeve"),
    Part( 11,  16,  52,  76,  -6,   6, RAMP_FACTION, 1, "right sleeve"),
    Part(-11,  11,  56,  94,  -7,   7, RAMP_FACTION, 2, "robe, chest"),
    Part( 14,  19,  46, 110,   2,   8, RAMP_NEUTRAL, 1, "shaku"),
    Part( -4,   4,  58,  94, -10,  -7, RAMP_FACTION, 3, "fold, chest"),
    Part(-12,  12,  90,  94, -11,  -8, RAMP_NEUTRAL, 3, "collar"),
    Part( 19,  26,  92, 114,   4,  10, RAMP_NEUTRAL, 3, "ofuda"),
    Part( -8,   8,  92, 112, -12,  -2, RAMP_NEUTRAL, 2, "face"),
    Part( -6,   6, 106, 110,  -7,  -5, RAMP_NEUTRAL, 3, "eboshi cord"),
    Part( -4,   4, 106, 128,  -5,   2, RAMP_NEUTRAL, 0, "eboshi"),
)

# ---------------------------------------------------------------------------
# The Kagura dancer
#
# The stormcaller's role, and the one the library has spent the most redrawing.
# Two of its four earlier figures were taken *out* of a shared rig: the
# medieval stormcaller's arms came down onto a brazier and the stormsinger's
# raised arms became wyrmlings sitting on its shoulders. A pair of arms up with
# marks thrown off them reduces to a rig rather than to a character.
# This sprite's answer is to keep the width and **carry it below
# the waist**, and the mesh's job is to keep that where the camera can see it.
#
# It is kept in the one axis that does not move a part vertically at all. The
# furisode are four boxes, two a side, stepping **out and down**: the outer
# pair lower than the inner and the left pair lower than the right, because a
# dancer with one arm up is not symmetric. So the widest row of the model is
# at y 42 to 57, which is below its own obi, and nothing about that is spent in
# y or z. What is unusual is not that a figure is widest low; three rogues and
# two robed healers in the library already are. It is *what* carries it: a
# stance or a hem in every one of those, and a pair of sleeves here.
#
# The one raised arm is authored as an arm and reads as one, which is right:
# The commission notes' stormsinger found that a vertical mass beside a body
# *is* an arm, and here there is one and only one. What is on the end of it is
# the only gold in this commission: rung 1 of this archetype resolves to `gold
# 0` where every other figure's resolves to a wood, a skin or a grey. The suzu
# are the single warm mark in the style's solids and they are set back at
# z 2 to 10 so they draw level with the crown without a unit of y being spent on
# them.
#
# The hair is a cap and a pair of cheek boxes, not a fall down the back.
# **A cape haloes rather than hangs**: a part set behind the shoulders is
# drawn above the head. A black fall of hair authored where the sprite
# draws it would have been a dark slab over the face. On the crown and beside
# the cheeks it does the opposite: the head is rung 2, a pale skin, and black
# on three of its four sides is what makes it read as a face at all.
# ---------------------------------------------------------------------------

KAGURA_DANCER: Tuple[Part, ...] = (
    Part(-20,  20,   0,  26, -10,  10, RAMP_FACTION, 1, "hakama, hem"),
    Part(-18,  18,  24,  58,  -9,   9, RAMP_FACTION, 2, "hakama"),
    Part(-31, -21,  34,  58,  -6,   6, RAMP_NEUTRAL, 3, "left furisode, outer"),
    Part( 21,  31,  42,  66,  -6,   6, RAMP_NEUTRAL, 3, "right furisode, outer"),
    Part(-24, -16,  50,  74,  -6,   6, RAMP_NEUTRAL, 3, "left furisode, inner"),
    Part( 16,  24,  52,  76,  -6,   6, RAMP_NEUTRAL, 3, "right furisode, inner"),
    Part(-14,  14,  58,  66, -11,  -9, RAMP_FACTION, 3, "obi"),
    Part(-11,  11,  60,  96,  -8,   8, RAMP_NEUTRAL, 3, "kimono"),
    Part( 19,  25,  78, 104,  -6,   2, RAMP_NEUTRAL, 3, "raised sleeve"),
    Part( 20,  26, 102, 108,  -6,   0, RAMP_NEUTRAL, 2, "hand"),
    Part(-12,  -8,  98, 116,  -6,   4, RAMP_NEUTRAL, 0, "hair, left side"),
    Part(  8,  12,  98, 116,  -6,   4, RAMP_NEUTRAL, 0, "hair, right side"),
    Part( 22,  32, 106, 120,   2,  10, RAMP_NEUTRAL, 1, "suzu"),
    Part( -8,   8,  98, 114, -12,  -2, RAMP_NEUTRAL, 2, "face"),
    Part( -9,   9, 112, 116,  -9,  -7, RAMP_FACTION, 3, "brow band"),
    Part(-11,  11, 114, 128,  -6,   8, RAMP_NEUTRAL, 0, "hair, crown"),
)

# ---------------------------------------------------------------------------
# The Temple monk
#
# The healer's role, and the fifth pale mass with a glyph. The medieval healer
# is a robe to the ground with a cross on a planted staff, the medic has legs
# and a floating cross, the wyrmpriest is a bell widest at the hem, the healer
# owl carries a disc and rings. This one is the **inverse bell**: widest at the
# shoulders and narrowing all the way to the hem, which is what its sprite
# draws and which is also what separates it from the Kagura dancer standing
# beside it in this same style, that figure being widest below its waist.
#
# The head is the read. Seven of these eight are identified by what they wear
# above the eyes and the eighth by wearing nothing, and a bare skull is the one
# thing this camera renders generously. It is a mass with no mark on it and
# nothing above it, which is the recipe for making a head read: "the part
# below it wider than the head and the part above it nothing at all".
#
# It is also where the palette nearly cost the figure. Rung 2 is a pale skin
# and rung 3 the paper white the robe is made of, and a pale head sitting on a
# pale robe is the single grey slab two earlier figures were redrawn for. What
# parts them is the **collar**, on the faction ramp's brightest rung, which is
# exactly what the sprite does and exactly the move the `nature` stag's chin
# separator made: the faction's brightest rather than the neutral's darkest,
# because ink draws as a hole and a hole under a chin is a severed head.
#
# The kesa is the diagonal this style's healer carries, in the two boxes a
# diagonal is affordable in and not the five that read as rubble. Each is
# twenty world units long in x against a step of eighteen to twenty in y, and
# y is halved on screen where x is not, so on the board each box is about twice
# as long as the step is high, which is the ratio that makes a pair of boxes a
# stroke instead of a stair.
#
# The shakujo is on the **off** side, at x -22, for the reason the sprite puts
# it there: a ring cluster at the top of a pole on the weapon side reduces to
# the dancer's suzu almost exactly. Here the two are further apart still: the
# dancer's cluster is gold on the right and this one is paper white on the
# left, and that is the palette paying rather than costing. What it costs
# instead is the staff's own material: this archetype reaches no wood at all,
# so the pole is `skin 0`, a dark warm brown that reads as wood by accident.
# ---------------------------------------------------------------------------

TEMPLE_MONK: Tuple[Part, ...] = (
    Part(-13,  13,   0,  30,  -8,   8, RAMP_NEUTRAL, 3, "robe, hem"),
    Part(-15,  15,  28,  58,  -8,   8, RAMP_NEUTRAL, 3, "robe, skirt"),
    Part(-21, -15,  50,  58,  -9,  -3, RAMP_NEUTRAL, 2, "left hand"),
    Part( 15,  21,  50,  58,  -9,  -3, RAMP_NEUTRAL, 2, "right hand"),
    Part(-22, -16,  20, 112,   0,   6, RAMP_NEUTRAL, 1, "shakujo"),
    Part(-16,  16,  56,  78,  -8,   8, RAMP_NEUTRAL, 3, "robe, waist"),
    Part(-20, -15,  60,  84,  -6,   6, RAMP_NEUTRAL, 3, "left sleeve"),
    Part( 15,  20,  60,  84,  -6,   6, RAMP_NEUTRAL, 3, "right sleeve"),
    Part( -4,  16,  58,  78, -11,  -8, RAMP_FACTION, 2, "kesa, lower"),
    Part(-18,  18,  76,  96,  -9,   9, RAMP_NEUTRAL, 3, "robe, shoulders"),
    Part(-16,   4,  78,  96, -12,  -9, RAMP_FACTION, 2, "kesa, upper"),
    Part(-15,  15,  88,  94, -12,  -9, RAMP_FACTION, 3, "collar"),
    Part(-24, -19,  96, 110,   0,   4, RAMP_NEUTRAL, 3, "shakujo, paper"),
    Part( -9,   9,  94, 118, -12,   0, RAMP_NEUTRAL, 2, "shaved skull"),
    Part( -7,   7, 106, 108, -14, -12, RAMP_NEUTRAL, 0, "brow"),
    Part(-26, -14, 110, 128,   2,   8, RAMP_NEUTRAL, 3, "shakujo, rings"),
)

# ---------------------------------------------------------------------------
# The Daimyo
#
# The commander's role, and the figure the `medieval` commander had already
# done the hard part for.
# The medieval commander's banner pole was first authored *in front of* the
# shoulder, 128 units tall and the tallest part in that figure, and its tip
# still landed 13.7 world units of screen below the helm plume; moved behind
# the shoulder, the same pole topped the figure, and nothing about it changed
# but the sign of its z. **Screen height is bought in z.**
#
# So the sashimono is authored at z 8 to 24, behind everything, and it is a
# **plate in x–z** rather than a wall in x–y: sixteen units of height against
# sixteen of depth is a plate by the test, which is the form this camera keeps
# most of. That form gives a top face at 255 rather than a front face at 178
# showing half its true extent. Its back edge draws 9.9 world units of screen
# above the kabuto's crown and its front edge disappears behind the head, which
# is the sprite's own arrangement: a squared-off flag standing over a helmet.
#
# The price is the measured one and it is paid by the whole figure. y and z
# ride the same scale in §5.5's match, so sixteen units of depth on one part is
# sixteen units the height fit then shrinks away from every other. The rest of
# this model is therefore held inside twenty-two units of z, tachi to maedate,
# and the banner and its staff are the only things that spend any of the budget.
#
# Rank is stated three times because one of the three has to survive the
# reduction, and the palette decided which. This archetype reaches no gold: its
# rungs land on ink, ink, skin and steel. So the mon and the maedate are both
# the near-white top of `steel`. The maedate is the better of the two by a wide
# margin: it is a wall four units proud of the helm's front, so it is the last
# part in depth in the whole figure and nothing can be drawn over it, which is
# the construction the scifi stormcaller's readout arrived at after two
# drafts had lost it under a torso.
# ---------------------------------------------------------------------------

DAIMYO: Tuple[Part, ...] = (
    Part(-14,  -5,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "left sabaton"),
    Part(  5,  14,   0,   8,  -8,   8, RAMP_NEUTRAL, 1, "right sabaton"),
    Part(-12,  -5,   8,  40,  -6,   6, RAMP_NEUTRAL, 1, "left suneate"),
    Part(  5,  12,   8,  40,  -6,   6, RAMP_NEUTRAL, 1, "right suneate"),
    Part(-20,  20,  18,  52,  -9,   9, RAMP_FACTION, 1, "jinbaori, hem"),
    Part(-26, -12,  34,  40, -12,  -8, RAMP_NEUTRAL, 3, "tachi"),
    Part(-18,  18,  50,  74,  -8,   8, RAMP_FACTION, 2, "jinbaori"),
    Part(  8,  13,  60, 106,  10,  16, RAMP_NEUTRAL, 1, "sashimono, staff"),
    Part(-13,  13,  66, 102,  -9,   9, RAMP_NEUTRAL, 0, "do"),
    Part(-13,  13,  78,  84, -12, -10, RAMP_FACTION, 3, "odoshi lacing"),
    Part(-21, -13,  84,  98,  -8,   6, RAMP_NEUTRAL, 1, "left sode"),
    Part( 13,  21,  84,  98,  -8,   6, RAMP_NEUTRAL, 1, "right sode"),
    Part(-13,  13, 104, 120,  10,  26, RAMP_FACTION, 2, "sashimono"),
    Part(-14,  14,  98, 110,  -6,  10, RAMP_NEUTRAL, 0, "shikoro"),
    Part( -4,   4, 118, 122,  12,  20, RAMP_NEUTRAL, 3, "mon"),
    Part( -9,   9,  98, 116, -12,  -2, RAMP_NEUTRAL, 2, "face"),
    Part(-11,  11, 110, 128,  -8,   8, RAMP_NEUTRAL, 0, "kabuto"),
    Part( -9,   9, 116, 126, -12,  -9, RAMP_NEUTRAL, 3, "maedate"),
)

# ---------------------------------------------------------------------------
# The Shinobi
#
# The rogue's role, and the fifth crouch in the library. The medieval rogue
# measured that the load-bearing half of "horizontal beats standing" is
# **horizontal**: a crouch put in y is a knight that has been sat on, and
# everything that separated that figure was spent in x. This one keeps that,
# with shins at plus or minus twenty-four, a stance half again the Samurai's.
# It adds the one thing no other rogue in the library has, which is that it is
# **black from the ground to the crown**. Its sprite is the leanest in the
# style, six neutral CLUT entries against the monk's twelve, and its rungs come
# out ink, ink, steel, steel: there is no skin in this figure and no warm
# colour at all. It is also one of only two figures here to reach a *second*
# step of lacquer, and that second step is what makes the rest possible: every
# mass below the collar is rung 1 and the two it must be told apart from, the
# kilt and the breast, are rung 0. Six of these eight could not have done that,
# and the model spends every bright thing it has left on three marks.
#
# **The sword is the two-box diagonal, given up in the same place the sprite
# gives it up.** A five-step diagonal steps four pixels at a time at board size
# and reads as rubble; two boxes are affordable. The sprite draws its scabbard
# corner to corner and then draws the body over it, so what a viewer ever sees
# is a chape low on the left and a hilt high on the right. That is what is
# built here: a mid-grey chape at y 26 to 34 thrown out to x -26 and a bright
# tsuba with a faction-wrapped grip at y 90 to 108 out to x 28. Nothing joins
# them, and nothing should: the figure carries the diagonal, which is that
# finding stated as art rather than as a limit.
#
# The other two marks are both faction and both bands, because a band across a
# black mass is the only thing that reads on one: the obi at the waist and the
# mask band across the zukin. The sprite's gold eyes are not built. This
# archetype samples no gold, and two pips at this size are a rung nobody can
# see paid for at twelve triangles.
#
# One part is neither a mark nor a mass and earns its twelve triangles anyway:
# the **collar**, on `steel 2`. A black hood sitting straight on a black torso
# is "two large masses on one rung that touch are one mass" with the two
# masses the whole upper half of the figure, and there is no room for a
# five-unit gap between a neck and a shoulder. A mid-grey band is the only
# separator the sampling leaves, and it is the same move the Samurai's shikoro
# makes for the same reason at the other end of the palette.
# ---------------------------------------------------------------------------

SHINOBI: Tuple[Part, ...] = (
    Part(-24, -14,   0,  36,  -8,   4, RAMP_NEUTRAL, 1, "left shin"),
    Part( 14,  24,   0,  36,  -8,   4, RAMP_NEUTRAL, 1, "right shin"),
    Part(-26, -13,  26,  34, -16, -10, RAMP_NEUTRAL, 2, "scabbard, chape"),
    Part(-21, -12,  34,  56,  -8,   4, RAMP_NEUTRAL, 1, "left thigh"),
    Part( 12,  21,  34,  56,  -8,   4, RAMP_NEUTRAL, 1, "right thigh"),
    Part(-15,  15,  32,  58, -12,  -4, RAMP_NEUTRAL, 0, "kilt"),
    Part(-14,  14,  54,  62, -14,  -6, RAMP_FACTION, 3, "obi"),
    Part(-19, -13,  60,  80, -10,   0, RAMP_NEUTRAL, 1, "left arm"),
    Part( 13,  19,  60,  80, -10,   0, RAMP_NEUTRAL, 1, "right arm"),
    Part(-13,  13,  58,  88, -10,   6, RAMP_NEUTRAL, 1, "torso"),
    Part(-11,  11,  60,  86, -14, -10, RAMP_NEUTRAL, 0, "breast"),
    Part(-11,  11,  86,  94, -12,   4, RAMP_NEUTRAL, 2, "collar"),
    Part( 12,  22,  90,  97, -18, -12, RAMP_NEUTRAL, 3, "tsuba"),
    Part(-11,  11,  92, 116,  -8,   6, RAMP_NEUTRAL, 1, "zukin"),
    Part(-10,  10, 100, 106, -10,  -8, RAMP_FACTION, 3, "mask band"),
    Part( 20,  28,  96, 108, -18, -12, RAMP_FACTION, 3, "grip, wrapped"),
    Part( -8,   8, 112, 128,  -4,   8, RAMP_NEUTRAL, 1, "zukin, crown"),
)

# ---------------------------------------------------------------------------
# The Shrine fox
#
# The beast's role, the fifth quadruped in the library, and the figure the
# rules said would start ahead: a body that runs along x is legible at twenty
# pixels before a detail is authored. It does start ahead, and every unit of
# the work is spent separating it from the other four, all of which are also
# long, low and four-legged. What it has that none of them has is the **tail**.
#
# It is a mass that climbs from the hip to the top of the figure at the end
# opposite the head, in three boxes stepping out and back, and its last box is
# the paper rung, so the tallest thing in this model is a white brush tip at
# x -29, the next tallest is the brush under it, and the tallest thing that is
# not the tail is a pair of ears at x 19 to 34. Nothing else in the library
# puts its highest point at the *back* of an animal. The medieval beast has
# nothing above its own spine, the xenoform has a narrow climbing tail, the
# dragon has two broad wings standing over the shoulder, the boar's bristles
# stand over its shoulder and its back; a single thick tail rising at one end,
# tipped white, is the fifth answer and it is the one the sprite draws.
#
# The white tip is also the stoat finding read the right way up. That
# commission dropped a black tail tip because an ink-rung part outboard of a
# figure is drawn against the board, where it is a gap and not a mark. This one
# is outboard by the same amount and on the **brightest** rung there is, so the
# same geometry that lost a mark there gains one here.
#
# The rest is the beast's structural argument, kept because it is right: two
# pairs of legs rather than the sprite's row of four, offset in x and in z, the
# near pair on the dark fur rung and the far pair on black; a faction plate
# lying flat along the spine, which is the horizontal plane this camera keeps
# most of; and a second faction mark at head height (the bib those stone foxes
# wear) authored four units proud of the chest so nothing at that end is drawn
# over it. There is no gold: this archetype samples none, so the sprite's eye
# is not built and the muzzle takes the paper rung, thrown six units proud of
# the skull, which is inside the five-or-six-unit budget a head detail has and
# is what says which end of the animal is which.
# ---------------------------------------------------------------------------

SHRINE_FOX: Tuple[Part, ...] = (
    Part(-18, -10,   0,  44,   4,  12, RAMP_NEUTRAL, 0, "hind leg, far"),
    Part(  6,  14,   0,  44,   4,  12, RAMP_NEUTRAL, 0, "fore leg, far"),
    Part(-14,  -6,   0,  44, -14,  -6, RAMP_NEUTRAL, 1, "hind leg, near"),
    Part( 10,  18,   0,  44, -14,  -6, RAMP_NEUTRAL, 1, "fore leg, near"),
    Part(-20,  20,  40,  76, -14,  10, RAMP_NEUTRAL, 1, "body, long in x"),
    Part( 14,  24,  46,  72, -12,   4, RAMP_NEUTRAL, 1, "chest"),
    Part(-24, -12,  48,  80,   0,   8, RAMP_NEUTRAL, 1, "tail, root"),
    Part( 16,  28,  48,  64, -16, -10, RAMP_FACTION, 3, "bib"),
    Part( 26,  36,  60,  70, -18, -12, RAMP_NEUTRAL, 3, "muzzle"),
    Part( 19,  34,  62,  80, -12,   2, RAMP_NEUTRAL, 2, "skull"),
    Part(-14,  14,  72,  78, -10,   8, RAMP_FACTION, 2, "votive cloth"),
    Part( 19,  24,  78,  94,   0,   6, RAMP_NEUTRAL, 2, "left ear"),
    Part( 29,  34,  78,  94,   0,   6, RAMP_NEUTRAL, 2, "right ear"),
    Part(-27, -19,  76, 108,   2,  10, RAMP_NEUTRAL, 2, "tail, brush"),
    Part(-29, -23, 106, 128,   4,  12, RAMP_NEUTRAL, 3, "tail, white tip"),
)

#: This style's commissioned meshes, by archetype name. Complete: all eight
#: roles, drawn from the eight ``sengoku`` sprites as they stand and held to
#: their measured silhouettes.
MESHES: Mapping[str, Tuple[Part, ...]] = {
    "knight": SAMURAI,
    "archer": YUMI_ARCHER,
    "mage": ONMYOJI,
    "stormcaller": KAGURA_DANCER,
    "healer": TEMPLE_MONK,
    "commander": DAIMYO,
    "rogue": SHINOBI,
    "beast": SHRINE_FOX,
}
