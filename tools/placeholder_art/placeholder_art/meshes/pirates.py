# SPDX-License-Identifier: MIT
"""The ``pirates`` mesh commission: the same eight roles, dressed as a crew.

Drawn against four commissioned styles rather than one, and mostly against
precedent: the four drawn before it already hold a solved case for four of
these eight. The `medieval` stormcaller's brazier serves the gunner's swivel
gun, the `medieval` commander's raised banner the captain's colours, the
`scifi` sniper's weapon held level the musketeer's musket, and the `scifi`
stormcaller's lit strip on a front face the surgeon's lantern. What each of
those cost when it was applied here is stated beside the figure it was applied
to, because two of them transferred unchanged and two did not.

The fifth figure is the one nothing in the library covers. Every ``beast``
drawn as a solid so far is a **quadruped**; this one is a parrot, and the note
beside it is the longest in the file for that reason.

**The palette is the sprite's, and it was measured rather than intended.** A
mesh's two ramps resolve from its own sprite's CLUT and the four rungs sample
that list by luminance, so a colour the drawing spends can simply not be
reachable. What this style's eight actually got, measured on the generated
sprites at faction colour zero:

===========  ===========  ===========  ===========  ===========
figure       rung 0       rung 1       rung 2       rung 3
===========  ===========  ===========  ===========  ===========
boarder      ink 0        leather 1    skin 1       steel 3
musketeer    ink 0        leather 1    skin 1       steel 3
hexer        ink 0        leather 1    skin 1       sand 3
gunner       ink 0        leather 1    skin 1       steel 3
surgeon      ink 0        leather 1    skin 2       ink 3
captain      ink 0        leather 1    dirt 3       ink 3
cutpurse     ink 0        skin 0       skin 2       ink 3
parrot       ink 0        ink 2        sand 2       snow 2
===========  ===========  ===========  ===========  ===========

Three of those decided a figure.

The **captain samples no skin at all**. Its neutral list is eleven entries and
the rungs land on the first, fourth, seventh and eleventh; ``skin 1`` and
``skin 2`` are the sixth and eighth, so both are missed and the only warm colour
this figure can reach is ``dirt 3``, the lit step of its own timber. Its face,
its cuffs and its flag truck are therefore all one rung. That is right, since
what the sprite draws in those three places is a face, a cuff and a wooden
truck, and a hand and a spar being the same tone at twenty-five pixels is what
this style's ``leather``/``sand`` merges were already betting on.

The **cutpurse reaches neither leather nor steel**. Its brightest rung is the
sailcloth white its jersey is drawn in, and its second is ``skin 0``: the
darkest step of the skin ramp, a red-brown that is the only thing in the figure
that can stand for a boot. So its legs and its knife grip wear the *skin* ramp's
darkest entry, and its blade is white rather than steel.

And ``gold`` is **unreachable everywhere**. The style spends it on two pixels in
the whole roster (the coal of the gunner's slow match) and no archetype's four
rungs land on it, so the ember mark this commission would have liked is the pale
steel rung instead. That is the same finding the medieval stormcaller's grey
antlers recorded, met a third time, and it is why nothing in this file is gold.

One thing the coupling *gave*: the parrot's neutral list is seven entries, the
leanest sprite in the style, and the rungs land on ``ink 0``, ``ink 2``,
``sand 2`` and ``snow 2``: a hole, a grey, a brass and a white. A bird whose
palette is exactly eye, shadow, beak and crest is a palette that was measured
and could not have been planned.
"""

from __future__ import annotations

from typing import Mapping, Tuple

from .rules import RAMP_FACTION, RAMP_NEUTRAL, Part

#: The style these tables are the commission for. Declared rather than taken
#: from the filename, so the registry in :mod:`.` cannot bind a module to the
#: wrong style's sprites. That is the one mistake the silhouette check would
#: not catch, since it would simply hold this boarder to a knight's box.
STYLE = "pirates"


# ---------------------------------------------------------------------------
# The boarder
#
# The knight's role, and the fifth figure in the library to be a torso with a
# shield on one side and a weapon on the other. What separates it from the four
# is what the shield *is*: a cask head snatched off the deck, which is the only
# **disc** in the roster, where the knight's is a rectangle, the trooper's a
# slab from ankle to chest, the drakeguard's a kite that narrows downward and
# the badger's a round buckler carried tight against the chest.
#
# A disc is built here as three boards whose widths step 14, 19, 14, so the
# outline is a hexagon rather than a rectangle and it is widest across its own
# middle. The steps are not a cost: at this pitch a join between two stacked
# parts shows a bright top-face stripe, which is what the sprite draws as the
# cask's hoops, and the one hoop authored as a part is the bar the sprite draws
# proud below the middle board.
#
# The whole shield is held at **z -19 to -10**, twelve to nineteen units proud
# of a body that lives inside ±8. That is deliberate and it is the sinking a
# forward throw costs: 0.866 of the throw, so the shield draws about eleven
# world units lower than its own y puts it, which is a shield carried low
# across the hip rather than a plate hung at the shoulder. The faction boss is
# the proudest part in the figure so that nothing can be drawn over it.
#
# The cutlass is the sprite's diagonal in the two boxes a diagonal is
# affordable in and not the five that read as rubble: a grip at the shoulder
# and a blade out and up beyond it. It takes the brightest rung there is, which
# for this archetype is pale steel. Both arms are bare, which no other figure
# in this style is, and the skin rung is the one this archetype does sample.
#
# **The two shirt stripes are four units proud and not two, and that is an
# ordering number rather than a drawing one.** A stripe across the middle of
# the mass it fronts is very nearly *tied* with that mass in depth: the height
# it gives up against the mass's centre is almost exactly what its proudness
# buys back. Authored two units proud the low stripe came out 1.4 units nearer
# than the shirt in the rule's own arithmetic, which
# :func:`.rules.check_commission` accepts, because that arithmetic is exact on
# part **centres**. The console sorts on the sum of eight **projected
# corner** depths, each of them truncated to a whole unit by the same shift the
# GTE applies. Eight truncations are worth up to eight units of the sum, so a
# gap of 1.4 is noise and the machine put the stripe *behind* its own shirt.
# Four units proud is a nine-unit gap, and it survives every board row, every
# elevation and every pan the camera can reach. **A margin under about two
# units in the ordering rule is not a margin**, and it is the one defect in
# this file that no check in the generator could have caught.
# ---------------------------------------------------------------------------

BOARDER: Tuple[Part, ...] = (
    Part( -13,  -5,   0,  10,  -8,   8, RAMP_NEUTRAL, 1, "left boot"),
    Part(   5,  13,   0,  10,  -8,   8, RAMP_NEUTRAL, 1, "right boot"),
    Part( -12,  -5,   8,  44,  -7,   7, RAMP_NEUTRAL, 1, "left leg"),
    Part(   5,  12,   8,  44,  -7,   7, RAMP_NEUTRAL, 1, "right leg"),
    Part( -13,  13,  42,  54,  -8,   8, RAMP_NEUTRAL, 0, "belt"),
    Part(  -5,   5,  44,  50, -11,  -9, RAMP_NEUTRAL, 3, "buckle"),
    Part( -22,  -8,  38,  58, -16, -10, RAMP_NEUTRAL, 1, "cask head, lower board"),
    Part( -25,  -6,  50,  56, -18, -16, RAMP_NEUTRAL, 3, "cask head, hoop"),
    Part( -12,  12,  52,  88,  -8,   8, RAMP_FACTION, 2, "striped shirt"),
    Part( -12,  12,  62,  68, -12, -10, RAMP_FACTION, 0, "shirt stripe, low"),
    Part( -19, -12,  62,  84,  -6,   6, RAMP_NEUTRAL, 2, "left arm, bare"),
    Part( -25,  -6,  54,  80, -16, -10, RAMP_NEUTRAL, 1, "cask head, middle board"),
    Part(  12,  19,  68,  92,  -6,   6, RAMP_NEUTRAL, 2, "right arm, bare"),
    Part( -19,  -9,  62,  78, -19, -17, RAMP_FACTION, 3, "cask head, boss"),
    Part( -12,  12,  76,  82, -12, -10, RAMP_FACTION, 0, "shirt stripe, high"),
    Part(  16,  22,  84,  96,  -8,  -2, RAMP_NEUTRAL, 1, "cutlass grip"),
    Part( -22,  -8,  78,  96, -16, -10, RAMP_NEUTRAL, 1, "cask head, upper board"),
    Part( -16,  -8,  98, 108,   6,  12, RAMP_FACTION, 1, "bandana tail"),
    Part(  -9,   9,  88, 110,  -7,   7, RAMP_NEUTRAL, 2, "head"),
    Part(  -8,   8,  96, 102, -10,  -8, RAMP_NEUTRAL, 0, "brow shadow"),
    Part(  20,  26,  94, 122,  -6,   2, RAMP_NEUTRAL, 3, "cutlass blade"),
    Part( -11,  11, 108, 128,  -8,   2, RAMP_FACTION, 2, "bandana"),
)

# ---------------------------------------------------------------------------
# The musketeer
#
# **The level-weapon precedent transferred unchanged, and it is the cheapest
# figure in the commission.** The `scifi` sniper's rail was measured twice:
# five walls stepping corner to corner read as rubble, and the same weapon held
# level in two boxes read as one stroke across the whole width. This sprite
# already draws the level line; it is the one hard horizontal in the style. So
# the mesh is two boxes meeting at the figure's centre, a pale barrel out to
# one side and a darker stock out to the other, thirty and twenty-eight world
# units long.
#
# The one number that had to be found rather than inherited is the **height**.
# The musket is authored proud, at z -12 to -6, because a weapon carried across
# the body has to be drawn over it; and the sinking then applies to the whole
# bar, which drops about eight world units of screen. Authored at the sprite's
# own chest height the pair drew at the waist. It is authored at y 78-88, eight
# above where the drawing puts it, so that it lands on the chest and clears the
# head's front face by two units rather than being cut in half by it.
#
# Four archers, four different lines: a tall vertical bow beside the body, a
# level rail at the waist with a lit muzzle, a level prod at the chest doubled
# by a stock, and this one, level at the chest, *asymmetric* in value, with
# the bright half on the off side and the dark half on the weapon side. The
# bandolier is the vertical the coat needs so that a brown mass is not a slab,
# and it is one box because it fronts one mass.
# ---------------------------------------------------------------------------

MUSKETEER: Tuple[Part, ...] = (
    Part( -13,  -5,   0,  10,  -8,   8, RAMP_NEUTRAL, 1, "left boot"),
    Part(   5,  13,   0,  10,  -8,   8, RAMP_NEUTRAL, 1, "right boot"),
    Part( -12,  -5,   8,  44,  -7,   7, RAMP_NEUTRAL, 1, "left leg"),
    Part(   5,  12,   8,  44,  -7,   7, RAMP_NEUTRAL, 1, "right leg"),
    Part( -13,  13,  28,  42, -10,  10, RAMP_NEUTRAL, 1, "sea coat, skirt"),
    Part( -14,  14,  40,  64,  -9,   9, RAMP_FACTION, 2, "shirt, where the coat falls open"),
    Part( -18, -12,  62,  84,  -6,   6, RAMP_NEUTRAL, 2, "left arm, out to the muzzle"),
    Part(  12,  18,  64,  88,  -6,   6, RAMP_NEUTRAL, 2, "right arm, at the stock"),
    Part( -12,  12,  62,  94,  -9,   9, RAMP_NEUTRAL, 1, "sea coat, body"),
    Part(  10,  17,  84,  94,   6,  10, RAMP_FACTION, 3, "queue ribbon"),
    Part(  -5,   5,  64,  94, -12, -10, RAMP_NEUTRAL, 1, "bandolier"),
    Part(   0,  28,  78,  88, -12,  -6, RAMP_NEUTRAL, 1, "musket stock, level"),
    Part( -30,   2,  80,  86, -12,  -8, RAMP_NEUTRAL, 3, "musket barrel, level"),
    Part(  10,  17,  94, 108,   4,  12, RAMP_NEUTRAL, 1, "tied queue"),
    Part(  -4,   6,  86,  92, -15, -11, RAMP_NEUTRAL, 0, "musket lock"),
    Part(  -9,   9,  88, 112,  -7,   7, RAMP_NEUTRAL, 2, "head"),
    Part(  -8,   8,  98, 103, -10,  -8, RAMP_NEUTRAL, 0, "brow shadow"),
    Part( -11,  11, 108, 128,  -8,   4, RAMP_NEUTRAL, 1, "tarred hair"),
)

# ---------------------------------------------------------------------------
# The hexer
#
# The narrowest sprite in the library at eighteen texels, which asks the mesh
# for thirty-six world units, and the only figure in any style whose widest
# point is its **head**. Every other mage in the roster puts its width at the
# hem: a cone, a column, a bell, a robe. This one is a stick with a mass of hair
# on it.
#
# The hair is two parts and they are **walls, not plates**: forty-four units of
# height against twelve of depth, where the wall test asks for more than
# twenty-one. That distinction is the whole figure. Authored as shallow lobes
# off the skull each one would be a bright plate at ear height and the head
# would be a staircase; as walls they are two dark falls past the jaw, and the
# face between them is the lit thing. There is no gap between hair and face and
# there is no room for one, so what parts them is value, the answer when x, y
# and z are all spent: the falls on the timber rung and the face on the skin
# rung above it.
#
# The **bone hex is the one bright mark**, and it is the only figure in this
# style whose brightest rung is ``sand`` rather than ``steel`` or white: this
# archetype's CLUT carries brass where the others carry ship's iron, so the
# charm resolves warm by construction. It hangs at the hip, proud, on the
# weapon side, which is where the sprite holds it. It is not thrust forward,
# because a body whose arms read as thrown out makes ``lunge`` and ``cast`` one
# pose.
#
# The stance is uneven: a boot under one knee and a peg under the other, five
# units narrower and two units shorter. A silhouette keeps that and a face does
# not.
# ---------------------------------------------------------------------------

HEXER: Tuple[Part, ...] = (
    Part(   2,   8,   0,   8,  -7,   5, RAMP_NEUTRAL, 1, "peg foot"),
    Part( -11,  -3,   0,  10,  -8,   6, RAMP_NEUTRAL, 1, "boot"),
    Part(   3,   8,   6,  44,  -6,   5, RAMP_NEUTRAL, 1, "peg leg"),
    Part( -10,  -3,   8,  46,  -7,   6, RAMP_NEUTRAL, 1, "left shank"),
    Part( -11,  11,  30,  62,  -8,   8, RAMP_FACTION, 1, "torn coat"),
    Part(  10,  17,  40,  52, -12,  -6, RAMP_NEUTRAL, 3, "bone hex"),
    Part(  10,  15,  52,  76,  -6,   6, RAMP_NEUTRAL, 2, "right arm"),
    Part( -10,  10,  60,  70,  -9,   9, RAMP_NEUTRAL, 0, "belt"),
    Part( -15, -10,  56,  80,  -6,   6, RAMP_NEUTRAL, 2, "left arm"),
    Part( -10,  10,  68, 100,  -8,   8, RAMP_FACTION, 2, "jersey"),
    Part(  -4,   4,  66, 100, -11,  -9, RAMP_FACTION, 0, "jersey placket"),
    Part( -18, -10,  76, 120,  -6,   6, RAMP_NEUTRAL, 1, "hair, left fall"),
    Part(  10,  18,  76, 120,  -6,   6, RAMP_NEUTRAL, 1, "hair, right fall"),
    Part(  -8,   8,  88, 112,  -7,   7, RAMP_NEUTRAL, 2, "face"),
    Part(  -7,   7, 100, 104, -10,  -8, RAMP_FACTION, 3, "headband"),
    Part( -11,  11, 112, 128,  -6,   8, RAMP_NEUTRAL, 1, "hair, crown"),
)

# ---------------------------------------------------------------------------
# The gunner
#
# **The brazier precedent transferred, and every clause of it earned its
# keep.** The `medieval` storm brazier measured three things and this figure
# needed all three. Two drafts of that bowl spent eighteen to twenty-two units
# of z on being a vessel and both came out a counter with a figure standing
# behind it, because twenty-two units of z is nineteen units of screen height
# and a top face is lit at full brightness whatever shape it is; eight units is
# what made it a brazier. So a vessel authored as a vessel draws as a table,
# and the gun barrel here is the **thinnest plate that still reads**: six
# units of height against six of depth, a bright bar with almost no front face,
# which is what the sprite's barrel row is; the identity goes into the
# **stand**, three legs with ten world units of background between them on the
# brazier and here a post of two legs with five world units between them under
# a cap; and the smoke goes **off the source, out in +x and back in +z**,
# because that is the only thing that lets it reach the top of the frame.
#
# The two smoke parts are the tallest thing in the model and neither is
# stacked: the first is at z 2-12 and the second at z 12-22, so the second
# draws about fifteen world units of screen height above its own y for free.
# Twenty-two units of depth on one part is the same bill the medieval
# stormcaller paid, and it is paid for the same reason: a plume that climbs in
# y is a staircase, and a plume that climbs in z is a plume.
#
# The arms are **down**, one at the breech and one on the linstock, which is
# what the sprite's pose of this archetype is for. The head sits low
# under a knitted cap with a lit band, the only figure here with no neck.
#
# One thing the precedent could not give: the sprite's slow match burns a coal
# in ``gold``, and **no rung in this style reaches gold**. The match's tip is
# the pale steel rung instead, and it is the one bright mark on the off side.
# That turns out to be worth having anyway, because the gun takes every other
# bright rung in the figure and the model would otherwise lean hard to one side
# in value as well as in mass.
# ---------------------------------------------------------------------------

GUNNER: Tuple[Part, ...] = (
    Part( -13,  -5,   0,  10,  -8,   8, RAMP_NEUTRAL, 1, "left boot"),
    Part(   5,  13,   0,  10,  -8,   8, RAMP_NEUTRAL, 1, "right boot"),
    Part( -12,  -5,   8,  44,  -7,   7, RAMP_NEUTRAL, 1, "left leg"),
    Part(   5,  12,   8,  44,  -7,   7, RAMP_NEUTRAL, 1, "right leg"),
    Part( -13,  13,  28,  42,  -9,   9, RAMP_NEUTRAL, 1, "tarred apron"),
    Part(  26,  32,   0,  78,   0,   8, RAMP_NEUTRAL, 1, "gun post, far leg"),
    Part(  15,  21,   0,  78,  -6,   2, RAMP_NEUTRAL, 1, "gun post, near leg"),
    Part( -14,  14,  40,  52,  -8,   8, RAMP_NEUTRAL, 0, "belt"),
    Part( -18, -12,  46,  74,  -6,   6, RAMP_NEUTRAL, 2, "left arm, on the linstock"),
    Part(  12,  18,  50,  78,  -6,   6, RAMP_NEUTRAL, 2, "right arm, at the breech"),
    Part( -12,  12,  50,  90,  -8,   8, RAMP_FACTION, 2, "shirt"),
    Part( -19, -13,  62,  84, -12,  -8, RAMP_NEUTRAL, 1, "linstock, low"),
    Part(  14,  32,  76,  84,  -6,   8, RAMP_NEUTRAL, 1, "gun post, cap"),
    Part(  12,  18,  82,  94, -10,  -2, RAMP_NEUTRAL, 1, "breech"),
    Part(  16,  30,  86,  92, -12,  -6, RAMP_NEUTRAL, 3, "swivel gun, barrel"),
    Part(  30,  34,  83,  95, -13,  -5, RAMP_NEUTRAL, 3, "swivel gun, muzzle"),
    Part( -17, -11,  82,  98, -12,  -8, RAMP_NEUTRAL, 1, "linstock, high"),
    Part(  22,  32,  98, 110,   2,  12, RAMP_NEUTRAL, 3, "powder smoke, off the muzzle"),
    Part(  -9,   9,  88, 112,  -7,   7, RAMP_NEUTRAL, 2, "head"),
    Part(  -8,   8,  98, 103, -10,  -8, RAMP_NEUTRAL, 0, "brow shadow"),
    Part( -16, -12,  96, 102, -14, -12, RAMP_NEUTRAL, 3, "slow match"),
    Part(  24,  34, 112, 128,  12,  22, RAMP_NEUTRAL, 3, "powder smoke, leaving the frame"),
    Part( -11,  11, 108, 126,  -8,   6, RAMP_NEUTRAL, 1, "knitted cap"),
    Part( -10,  10, 112, 117, -10,  -8, RAMP_NEUTRAL, 3, "cap band"),
)

# ---------------------------------------------------------------------------
# The surgeon
#
# **The lit-strip precedent transferred as a construction and not as a rung.**
# The ship's lantern is raised on the off side, and its pane is a thin wall two
# units proud of the case's front rather than an inset plate on its top. That
# is the construction the commission notes measured on the sci-fi
# stormcaller's readout, and for the reason it gives: the last part in depth is
# the only part guaranteed to survive. That half needed nothing.
#
# What did not transfer is the *value*. The sci-fi readout is bright against a
# dark slab; this archetype's brightest neutral rung is the sailcloth its whole
# apron and its kerchief are drawn in, and the kerchief's edge is **two world
# units** from the lantern case, under the four units that are decoration, let
# alone the five that are structure. Authored on the neutral ramp the pane came
# out the same tone as the head beside it and the lantern read as a brown box
# with a grey smear. There is no width to buy the gap with: rule 4 measures the
# *whole* sprite, whose own lantern is carried hard against its head and parted
# from it by one texel of ink outline, and a mesh has no outline. So the
# separator is the last one there is: value. The only saturated mark this
# figure can reach is the **faction ramp's brightest rung**, which the sash and
# the shoulders already wear. The pane takes it, and the lantern reads as lit.
# **A sprite separates two touching masses with an outline and a mesh cannot**,
# and where the silhouette rule leaves no room to open a gap the ramp is what is
# left.
#
# **The apron did not transfer, and it is the figure's whole problem.** The
# warning about a robe is that a legless mass banded only horizontally is a
# stack of slabs, and its answer is a full-height front panel in a rung the
# flanks do not wear. This archetype's four rungs are black, tar, skin and
# white, and its apron is white, so there is *no second rung a placket could
# be*, and the sprite draws no vertical on the garment either. What is used
# instead is the three things the drawing does have: two bare arms in the skin
# rung running the height of the mass and standing clear of it on both sides,
# the faction sash and shoulders across the chest, and the lantern held out and
# up on one side so the outline is not symmetric. The apron itself is three
# masses on one rung, and they are meant to be one garment; each is a wall by
# the wall test (26 against 14, 32 against 16, 38 against 16) rather than the
# plates a uniformly deep apron would have made of all three.
#
# The hem is the widest row, at ±21, which is what its sprite measures and what
# no other figure in this style does. The hexer's coat is torn narrow for
# exactly that reason.
# ---------------------------------------------------------------------------

SURGEON: Tuple[Part, ...] = (
    Part( -21,  21,   0,  26,  -7,   7, RAMP_NEUTRAL, 3, "apron hem"),
    Part(   6,  11,  18,  24, -10,  -8, RAMP_NEUTRAL, 1, "tar stain, low"),
    Part( -18,  18,  24,  56,  -8,   8, RAMP_NEUTRAL, 3, "apron skirt"),
    Part( -10,  -5,  34,  40, -11,  -9, RAMP_NEUTRAL, 1, "tar stain, high"),
    Part( -13,  13,  54,  92,  -8,   8, RAMP_NEUTRAL, 3, "apron bib"),
    Part(  14,  20,  58,  88,  -6,   6, RAMP_NEUTRAL, 2, "right arm"),
    Part( -20, -14,  74, 104,  -6,   6, RAMP_NEUTRAL, 2, "left arm, raised"),
    Part( -13,  13,  82,  92, -11,  -9, RAMP_FACTION, 3, "sash"),
    Part( -17, -11,  88,  98,  -8,   4, RAMP_FACTION, 3, "left shoulder"),
    Part(  11,  17,  88,  98,  -8,   4, RAMP_FACTION, 3, "right shoulder"),
    Part(  -9,   9,  88, 112,  -7,   7, RAMP_NEUTRAL, 2, "face"),
    Part(  -8,   8,  98, 103, -10,  -8, RAMP_NEUTRAL, 0, "brow shadow"),
    Part( -25, -13, 104, 122,  -6,   4, RAMP_NEUTRAL, 1, "ship's lantern, case"),
    Part( -23, -15, 108, 118,  -9,  -7, RAMP_FACTION, 3, "ship's lantern, lit pane"),
    Part( -11,  11, 108, 128,  -8,   4, RAMP_NEUTRAL, 3, "kerchief"),
    Part( -26, -12, 120, 126,  -6,   4, RAMP_NEUTRAL, 1, "ship's lantern, cap"),
)

# ---------------------------------------------------------------------------
# The captain
#
# **The raised-banner precedent transferred, sign and all.** The `medieval`
# commander's pole was first authored in front of the shoulder and
# its tip landed below its own helm plume; moved behind, the same pole topped
# the figure. This one is authored behind from the start, at z 6-12, and its
# cloth streams further back still, to z 18, so the colours break the top of
# the frame the way the sprite draws them without one unit being spent in y
# above the hat.
#
# **The cape warning did not need re-solving, it needed obeying.** A long coat
# is exactly the slab that haloes a figure rather than hanging behind it, so
# this coat is not behind anything: it is the body's own mass, at z -8 to 8,
# two faction boxes stacked from the knee to the collar with the sleeves
# outboard of them. Nothing in this figure sits behind the shoulders except the
# pole, the plume and the flag, three narrow parts, which is what the axis is
# for.
#
# The **tricorn is the style's one hat and it is two parts**, because the
# refused `medieval` mage measured what a taper costs: every box of that hat
# was a plate by the wall test and a perfectly graded stack read as a ziggurat.
# Here the brim is *deliberately* a plate, eighteen units of depth against six
# of height, which is what a brim is, and it is on the darkest rung, so it
# draws as a wide black bar above a lit face, which is the medieval knight's
# visor construction used one part higher. The crown above it is one box and
# narrower, and the faction plume behind it is the thing that actually reaches
# 128.
#
# **Where the brim sits in z is the whole of whether this figure has a face**,
# and it is the sign question already settled for the banner, met on a part
# that is *not* trying to gain height. The first draft carried the brim at
# z −12 to 10, proud of the face the way a hat is worn: screen height is
# 0.5·y + 0.866·z, so its front edge landed at 44.6 where the brow's top face
# ends at 44.6 and the face's own top is at 49.9. The brim covered every unit
# of face above the brow, and the head drew as a brown box over a black box
# with no skin in it at all. The other seven figures in this style all draw a
# lit band with a dark brow bar in it. Pulled back to z −6 to 12, the same
# brim, the same size and the same rung, starts at 49.8: about five world units
# of lit face survive above the brow, which is two pixels at the size the board
# draws, and the head reads. **A part carried proud of a face does not shade
# it, it deletes it**: the depth that buys a plume its height buys a hat brim
# the face.
#
# The face and the cuffs are the same rung because the palette leaves no choice
# (see the table at the top of this file). The white shirt front is the
# vertical the coat needs, in the one rung the coat does not wear, and it is a
# single box because it fronts a single mass. A strip run from hem to collar
# would average a low y, sort far, and be drawn over by everything above it.
# ---------------------------------------------------------------------------

CAPTAIN: Tuple[Part, ...] = (
    Part( -13,  -5,   0,  10,  -8,   8, RAMP_NEUTRAL, 1, "left boot"),
    Part(   5,  13,   0,  10,  -8,   8, RAMP_NEUTRAL, 1, "right boot"),
    Part( -12,  -5,   8,  40,  -7,   7, RAMP_NEUTRAL, 1, "left leg"),
    Part(   5,  12,   8,  40,  -7,   7, RAMP_NEUTRAL, 1, "right leg"),
    Part( -18,  18,  38,  70,  -8,   8, RAMP_FACTION, 1, "long coat, skirt"),
    Part(  20,  26,   8, 116,   6,  12, RAMP_NEUTRAL, 1, "colours, pole"),
    Part( -19, -12,  62,  90,  -6,   6, RAMP_FACTION, 2, "left sleeve"),
    Part(  12,  19,  62,  90,  -6,   6, RAMP_FACTION, 2, "right sleeve"),
    Part( -13,  13,  68, 104,  -8,   8, RAMP_FACTION, 2, "long coat, body"),
    Part(  -5,   5,  68, 102, -11,  -9, RAMP_NEUTRAL, 3, "shirt front"),
    Part( -19, -13,  88,  94,  -8,  -2, RAMP_NEUTRAL, 2, "left cuff"),
    Part(  13,  19,  88,  94,  -8,  -2, RAMP_NEUTRAL, 2, "right cuff"),
    Part(  26,  34,  98, 114,   4,  18, RAMP_FACTION, 3, "colours, cloth"),
    Part(  -9,   9,  88, 112,  -7,   7, RAMP_NEUTRAL, 2, "face"),
    Part(  28,  33, 102, 108,   0,   3, RAMP_NEUTRAL, 3, "colours, skull"),
    Part(  -8,   8,  98, 103, -10,  -8, RAMP_NEUTRAL, 0, "brow shadow"),
    Part( -16,  16, 110, 116,  -6,  12, RAMP_NEUTRAL, 0, "tricorn brim"),
    Part(  19,  27, 114, 124,   6,  12, RAMP_NEUTRAL, 2, "colours, truck"),
    Part(  -4,   4, 116, 128,   2,  12, RAMP_FACTION, 3, "hat plume"),
    Part(  -9,   9, 114, 126,  -7,   7, RAMP_NEUTRAL, 1, "tricorn crown"),
)

# ---------------------------------------------------------------------------
# The cutpurse
#
# The fifth crouched figure with a blade, and the rules are unambiguous about
# what a crouch is worth: nothing. Rule 1 builds every figure the same height
# whatever it is doing and rule 4 then holds it to its own sprite's box, so the
# sprite's lowest head in the roster cannot be a mesh's lowest head. Everything
# here is spent in **x** (a stance at ±23, wider than any other humanoid in
# this style) and in the **diagonal** two marks at different heights on
# opposite sides make: the scarf tails off the back at chest height on one
# side, the knife up at head height on the other.
#
# The tails are the one thing this rogue has that the other four do not, and
# they are bought in z rather than in y: both sit at z 8-16 and draw above
# where their own y puts them, so a pair of streamers that the sprite trails
# *downward* becomes a pair that trails *back*, which is the same gesture in
# the axis this camera reads. They are two boxes at two heights rather than
# one, so they carry the diagonal rather than sitting as one bar.
#
# The upper tail's depth is the same ordering number the boarder's shirt
# stripes paid, met from the other side. At z 4-12 it sat 2.6 units *nearer*
# than the face on part centres, and the machine's eight truncated corner
# depths put it 4 units the wrong way: a streamer authored behind the
# shoulder, sorted in front of the head. Moved into the lower tail's own band
# at z 8-16 it is unambiguously the farther part by twenty-seven, and it moves
# ahead of the face in the array where it belonged: **the array is the drawing
# order, so a part that changes depth changes its line.**
#
# Its y came down two units with that move, and the two units are the whole of
# whether this figure has two marks or one. Four units of extra depth is
# 0.866 x 4 of extra screen height, which lifted the tail's top past the
# bandana's. The tail and the bandana are one world unit apart in x and
# wear the same faction rung, so "two large masses on one rung that touch
# are one mass" applied instantly and the head grew a wing. Held just under the
# bandana's crown the tail is a step off the back again. **Buying depth to fix
# an ordering raises a part, and what it rises into has to be checked.**
#
# The palette is what decided the values here and it is stated in the table at
# the top: this archetype reaches no leather and no steel at all, so the boots
# and legs are the *skin* ramp's darkest step (the one red-brown in its list)
# and the blade is white rather than pale iron. The jersey is white with two
# faction stripes proud of its front, which is what a striped jersey is and
# what stops a white torso and a white blade reading as one thing.
# ---------------------------------------------------------------------------

CUTPURSE: Tuple[Part, ...] = (
    Part( -23, -13,   0,  10,  -9,   7, RAMP_NEUTRAL, 1, "left boot"),
    Part(  13,  23,   0,  10,  -9,   7, RAMP_NEUTRAL, 1, "right boot"),
    Part( -22, -12,   8,  46,  -8,   6, RAMP_NEUTRAL, 1, "left leg, braced wide"),
    Part(  12,  22,   8,  46,  -8,   6, RAMP_NEUTRAL, 1, "right leg, braced wide"),
    Part( -14,  14,  44,  54,  -9,   9, RAMP_FACTION, 2, "sash"),
    Part( -19, -13,  50,  76,  -6,   6, RAMP_NEUTRAL, 2, "left arm, low"),
    Part( -13,  13,  52,  88,  -8,   8, RAMP_NEUTRAL, 3, "striped jersey"),
    Part( -13,  13,  62,  68, -11,  -8, RAMP_FACTION, 3, "jersey stripe, low"),
    Part( -26, -12,  78,  86,   8,  16, RAMP_FACTION, 1, "scarf tail, lower"),
    Part(  13,  19,  66,  90,  -6,   6, RAMP_NEUTRAL, 2, "right arm, high"),
    Part( -13,  13,  72,  78, -11,  -8, RAMP_FACTION, 3, "jersey stripe, high"),
    Part( -26, -12,  88,  96,   8,  16, RAMP_FACTION, 2, "scarf tail, upper"),
    Part(  -9,   9,  78, 100,  -7,   7, RAMP_NEUTRAL, 2, "face"),
    Part(  15,  21,  84,  94,  -8,  -2, RAMP_NEUTRAL, 1, "knife grip"),
    Part(  -8,   8,  88,  94, -10,  -8, RAMP_NEUTRAL, 0, "brow shadow"),
    Part(  18,  26,  92, 114,  -6,   0, RAMP_NEUTRAL, 3, "knife blade"),
    Part( -11,  11,  98, 118,  -8,   4, RAMP_FACTION, 2, "bandana"),
    Part(  -6,   6, 116, 128,  -6,   6, RAMP_FACTION, 3, "bandana knot"),
)

# ---------------------------------------------------------------------------
# The parrot
#
# The library's fifth beast and the first that is neither a quadruped nor an
# upright humanoid, so it is the one figure in this commission with no
# precedent at all. The rules hold two findings about animals and they point
# opposite ways here: *horizontal beats standing*, which a bird on two feet does
# not get; and
# *an upright animal is a humanoid to this camera*, which is exactly what a
# bird risks being. Four of the library's beasts are long bodies running along
# x. This one has to earn the same read from a body that stands.
#
# What it is built on is that the sprite's body **leans off its own feet**. The
# head is carried thirteen world units to the weapon side of the toes that hold
# it up and the tail sweeps twenty-four units the other way and down to the
# deck, so the figure is a diagonal rather than a column. Every person in this
# roster stacks head over feet; this is the one drawing that does not, and it
# is the whole separation.
#
# Three constructions carry it, and each is one of the rules read in a new
# place:
#
# * **The tail is the horizontal, and it is at a height nothing else occupies.**
#   Three boxes step down and out from y 48 at the hip to y 4 at the deck,
#   fourteen units long against a twelve-unit step, which is the ratio a
#   diagonal built from boxes requires. It is the only mass in the figure
#   below the knee line, so at twenty pixels the outline has a limb going one
#   way at the bottom and a head going the other way at the top.
# * **There are no legs, only feet.** Two short brass shanks under a body that
#   starts at y 26. The body is two thirds of the figure's height and every
#   humanoid here gives that to a torso half the size over legs twice as long.
#   The toes are thrown forward in -z and spread six units apart, which is the
#   width at which a gap is structure rather than decoration.
# * **The skull is wider than it is tall** (twenty by fourteen, the shape
#   measured on the `mythical` stormsinger's wyrmlings) and the beak is thrown
#   out in +x rather than in -z. That last is the finding this figure paid for:
#   a beak is a snout, and every muzzle in the `nature` style drew at the
#   chest when it was authored ten units proud. Held to six units proud and
#   extended in x instead, it sits on the face and lengthens the head in the
#   one axis that costs no screen height at all.
#
# The crest is three white spines with five world units of background between
# them, upright walls rather than a plate, and the middle one is the part that
# reaches 128. It is the only mark in the library above a head that is
# *symmetric about its own head* and offset from the figure's centre, which is
# what a crest on a leaning skull is.
#
# The palette gave this figure exactly what it needed and none of it was
# chosen: a hole for the eye, a brass for the beak and the feet, a white for
# the crest and the wing bar, and the faction ramp for everything else. That is
# why the only neutral mass on the body is the four-unit wing bar and the bird
# is otherwise entirely the faction's colour.
# ---------------------------------------------------------------------------

PARROT: Tuple[Part, ...] = (
    Part( -10,  -2,   0,   8, -10,   2, RAMP_NEUTRAL, 2, "left foot, toes spread"),
    Part(   4,  12,   0,   8, -10,   2, RAMP_NEUTRAL, 2, "right foot, toes spread"),
    Part( -24, -10,   4,  18,  -3,   7, RAMP_FACTION, 1, "tail, tip"),
    Part(  -8,  -3,   6,  28,  -6,   2, RAMP_NEUTRAL, 2, "left shank"),
    Part(   5,  10,   6,  28,  -6,   2, RAMP_NEUTRAL, 2, "right shank"),
    Part( -18,  -4,  16,  32,  -3,   7, RAMP_FACTION, 2, "tail, middle"),
    Part( -12,   2,  28,  48,  -3,   7, RAMP_FACTION, 2, "tail, root"),
    Part(  -8,  16,  26,  74,  -9,  11, RAMP_FACTION, 2, "body, leaning out"),
    Part(  -6,  12,  40,  70, -12,  -9, RAMP_FACTION, 1, "folded wing"),
    Part(  -4,   8,  56,  62, -14, -12, RAMP_NEUTRAL, 3, "wing bar"),
    Part(   6,  18,  68,  88,  -7,   7, RAMP_FACTION, 3, "neck"),
    Part(   4,  24,  88, 102,  -6,  10, RAMP_FACTION, 3, "skull"),
    Part(  16,  26,  88,  96, -10,  -4, RAMP_NEUTRAL, 2, "beak"),
    Part(   9,  13,  94,  98,  -8,  -6, RAMP_NEUTRAL, 0, "eye"),
    Part(  22,  27, 100, 118,   0,   8, RAMP_NEUTRAL, 3, "crest, far"),
    Part(   2,   7, 100, 120,  -4,   4, RAMP_NEUTRAL, 3, "crest, near"),
    Part(  12,  17, 102, 128,  -2,   6, RAMP_NEUTRAL, 3, "crest, middle"),
)

#: This style's commissioned meshes, by archetype name. Complete: all eight
#: roles, drawn from the eight pirates sprites as they stand and held to their
#: measured silhouettes.
MESHES: Mapping[str, Tuple[Part, ...]] = {
    "knight": BOARDER,
    "archer": MUSKETEER,
    "mage": HEXER,
    "stormcaller": GUNNER,
    "healer": SURGEON,
    "commander": CAPTAIN,
    "rogue": CUTPURSE,
    "beast": PARROT,
}
