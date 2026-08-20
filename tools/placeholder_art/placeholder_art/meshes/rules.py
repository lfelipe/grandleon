# SPDX-License-Identifier: MIT
"""What a mesh is, and every rule one is held to.

The rules live here and the **art** lives beside them, one module a style:
:mod:`.medieval` holds the ``medieval`` commission, :mod:`.scifi` the ``scifi``
one, and a style with no commission has no module. That is the same shape the
sprite side already has, and it is the same reason: :mod:`..characters` draws
``medieval`` and :mod:`..scifi`, :mod:`..mythical`, :mod:`..nature` draw the
others. A style is a different *drawing* of the same archetype, so two styles'
meshes have nothing to share but the rules in this file.

What a mesh is here
-------------------
A short list of **convex axis-aligned boxes** in the figure's own space: x
right, y up, z away, origin at the centre of the feet. Eight integer corners
and six quad faces each, so a part costs twelve triangles and a whole figure
costs a couple of hundred. No curves, no vertex sharing between parts, no
authored UV layout. Flat shading was measured against a stretched texture at
the size a unit is drawn and the texture lost decisively, so there is nothing
for a UV to be.

The four rules, and every one of them was learned by measurement
---------------------------------------------------------------
1. **A figure is built to be drawn the height its own sprite is drawn.** A
   world-vertical extent H is drawn ``focal·H·cos φ / depth`` pixels tall, so at
   the shipped sixty-degree pitch a figure must be built taller than it is meant
   to appear. :func:`build_scale` is that compensation computed rather than
   typed. This is the world-stretch that is wrong for a *billboard* and right
   for a mesh, for the same reason: a billboard's stretch runs away with the
   pitch, and at the one pitch this camera uses a solid's is bounded.

   For a long time this rule read "built at ``unit_world / cos φ``", a flat 128
   for all 112 figures, and it was wrong twice over. It sized the figure to a
   **tile**, 32 rows, where a sprite's figure is the 28 above the faction disc;
   and it ignored that the pitch sends **depth** to screen height as well, at
   ``tan φ`` per unit -- which the paragraph below already said, and which the
   rule did not do. The two compound to 1.39, and that is exactly how much
   narrower than its own sprite every mesh in this repository measured. The
   height was wrong, not the width. What replaces it is rule 4 applied to the
   other axis of the same box, so the two are now one measurement.

2. **Every face names a ramp and a rung, never a colour.** A mesh carries no
   colour at all. The ramp resolves with no table and no naming convention: an
   entry of an archetype's generated CLUT is *faction-bearing* exactly when its
   colour word is absent from at least one of the other five factions' CLUTs
   for the same archetype, and *neutral* when all six carry it. So a face says
   "faction ramp, rung 3" and the six faction recolourings stay free. A seventh
   faction would be a palette change and not a mesh change.

3. **Parts are convex and authored far-to-near at the shipped pitch.** This is
   what buys the whole scene freedom from a depth buffer, an ordering table and
   an ``AVSZ4``. Within a box, faces are wound outward and the projected winding
   decides visibility, which is exact for a convex solid. Between boxes, depth
   at this pitch falls with height by ``sin φ`` and rises with z by ``cos φ``,
   so :func:`depth_key` is the ordering the array must already be in. Because
   a pan is a translation it cannot reorder two parts in depth, the authored
   order is correct at every camera position a pan can reach.
   :func:`check_commission` measures the order rather than trusting the comment,
   and measures it **twice**: exactly, which is what the order means, and then
   in the console's own arithmetic, which is what the console will do with it.
   Those two disagree, because the machine sums eight corner depths it has
   already truncated, so a lead of one or two units is a lead it cannot see.
   The construction that produces a tie the exact reading lets through is a
   strip authored across the middle of the mass it fronts: the height such a
   strip gives up against that mass's centre is very nearly what its own
   proudness buys back. :func:`machine_order_margin` is the second reading.

4. **The silhouette is the sprite's, measured.** The width a mesh is authored
   to and the height it is *drawn* at are the opaque box of the archetype's own
   generated sprite **in that same style**, the height taken above the faction
   disc so that four rows of ground are not counted as part of the person, in texels of its cell, read from the very arrays the console
   uploads to VRAM by :func:`..playstation_header.silhouettes`. They are
   *emitted* into the generated header so that redrawing a sprite moves its
   mesh's target with it and no renderer, document or hand-written constant has
   to be found and edited. It is also why a mesh cannot be shared between
   styles: two styles draw the same archetype at different widths, so one
   figure would be held to two different numbers.

What this module does not decide
--------------------------------
The face winding order, the six per-normal light factors and the ramp
resolution are properties of the *renderer*, not of the art: they are the same
for every mesh in every style, and a mesh that carried them would be carrying a
copy of the renderer. They stay where they are drawn.

What the camera does to a figure
--------------------------------
The rules above are **necessary and not sufficient**: a figure that satisfies
every one of them and still cannot be told apart from another archetype is a
gap to record, not a mesh to ship. What the rules do not catch is what this
camera does to a solid, and every finding below is a measured property of the
camera rather than a taste. Each commission module states what its own eight
paid to learn them.

A viewer sees a part's **top face and its front face and almost nothing else**,
so a figure reads by parts that differ in x and z (shoulders, a shield, an
outstretched weapon), and a feature built by stacking parts in y is a staircase
of bright top faces. Screen height is bought in **z**: the camera sends world y
to screen vertical through ``cos φ`` and world z through ``sin φ``, so one unit
of z is drawn ``tan φ`` = 1.73 times taller than one of y. A part moved *away*
rises and a part moved *toward* the viewer sinks just as fast, which is the sign
an author gets wrong: a banner streams in z behind the shoulder, and a muzzle
thrown ten units forward lands on the chest. Five or six units proud is the
whole budget a head detail has. Depth is not free either: rule 4's match wears y
and z on one scale, so depth spent on one part is height the whole figure gives
back, and it is worth spending on the one or two parts that carry the archetype.

A part is a **wall** when ``dy > dz·tan φ`` and a **plate** when it is not. A
box deeper than 0.58 of its own height shows more lit top than front and reads
as a horizontal slab whatever its width does, so a spire is few parts each
taller than that bound, and when a plate is the right answer it should be the
thinnest plate that still reads. A planar emblem belongs in the **horizontal**
plane, a top face keeping ``sin φ`` of its area against a front face's
``cos φ``: a cross authored lying flat is drawn a cross and authored upright is
drawn a bar. The exception is a **ring**, where what must survive is the
background inside it: authored upright, set back, and twice as tall as it is
meant to look, because an upright circle is drawn squashed two to one.

What separates two masses is the **background between them**, and a gap is
structure at five world units and decoration under four, measured in x. Screen
vertical keeps 0.50 of y and 0.87 of z, so the same strip of background costs
about six units in z and ten in y. Every top face is lit at full brightness, so
two boxes whose top faces meet are one plate whatever their outlines are: make
the part below a head wider than the head and the part above it nothing at all.
Where there is no room for background the separator is **value**, since two
large masses on one rung that touch are one mass. The darkest rung is usually
``ink``, which draws as a hole: right for a visor, wrong for a material.

**Horizontal beats standing.** A quadruped is legible at twenty pixels before a
detail is authored, because its mass runs along the axis this camera shows best;
an upright animal is a humanoid to it. A crouch cannot be spent in y, since rule
1 builds every figure two tiles tall whatever it is doing, so it is spent in x
as a stance. A diagonal is affordable in two boxes and not in five: a figure is
drawn about twenty-five pixels tall, so a five-step diagonal steps four pixels
at a time and reads as rubble, while two boxes meeting at the centre draw as one
stroke. A mass beside a torso taller than it is wide is a limb, a part set
behind the shoulders is drawn above the head rather than behind it, and a
legless robe needs a full-height front panel cut into **one box a mass**, each a
little prouder than the mass it fronts. A single hem-to-collar strip averages a
low y, sorts far, and is drawn over.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import List, Mapping, Sequence, Tuple

from .. import characters
from .. import terrain


#: The camera pitch a 3D board would be drawn at. Sixty is measured rather than
#: chosen: at a focal length of 300 pixels and a board centre 744 world units
#: in front of the eye, a near tile is drawn 300 x 64 / 600 = 32 pixels across,
#: which is the art's own cell size. A mesh's build height and its authored
#: part order both depend on this angle, which is why it is named here once
#: rather than assumed in two places.
PITCH_DEGREES = 60

#: One board tile, in world units. Because the screen-space billboard this
#: camera settles on is a ``side x side`` square, it is also the world extent a
#: unit is drawn the size of.
UNIT_WORLD = 64

#: The tallest a figure may be **built**, which is what a figure of no depth at
#: all would be built to stand its sprite's full height: ``unit_world / cos φ``.
#: It is a ceiling and not the build height. This used to be the build height,
#: applied flat to all 112 figures, and that was wrong twice over -- see
#: :func:`build_scale`.
MESH_WORLD_HEIGHT = round(UNIT_WORLD / math.cos(math.radians(PITCH_DEGREES)))
assert MESH_WORLD_HEIGHT == 128, "the shipped pitch bounds a figure at two tiles"

#: What ``sin φ`` and ``cos φ`` are, once, for the two places that project.
PITCH_COSINE = math.cos(math.radians(PITCH_DEGREES))
PITCH_SINE = math.sin(math.radians(PITCH_DEGREES))

#: How far a figure's drawn height may sit from its sprite's before it stops
#: being a rounding and starts being a mistake. Integer corners cannot land the
#: projection exactly; the worst rounding over all 112 figures is 0.38 units,
#: so two is slack enough to never fire on arithmetic and tight enough to fire
#: on a figure built to the wrong height.
HEIGHT_TOLERANCE = 2

#: The two ramps a face may name. Neutral is the archetype's own colours, which
#: every faction shares; faction is the ramp the six factions differ on. The
#: numbering is the header's, so it is fixed here rather than in the emitter.
RAMP_NEUTRAL = 0
RAMP_FACTION = 1
RAMP_COUNT = 2

#: Rungs on a ramp, sampled by luminance. Four is the most the thinnest faction
#: ramp in the whole roster (mage, stormcaller, healer, commander and beast, at
#: three entries) can still be sampled at.
RUNG_COUNT = 4

#: Corners and quad faces of one box. A box is the only primitive here.
VERTICES_PER_PART = 8
FACES_PER_PART = 6
TRIANGLES_PER_PART = FACES_PER_PART * 2

#: The triangle count a figure must land inside, measured rather than chosen:
#: the knight came out at 240 triangles and the archer at 204. Sixteen figures
#: anywhere in this band is 2,400 to 4,800 triangles, against the 9,400 a
#: thirty-frames-a-second budget allows on the PlayStation.
#: ``platform/playstation/scratch/scratch3d_exe.cpp`` measures that budget and
#: prints what a board of sixteen actually submits against it. The floor is as
#: real as the ceiling: a figure below it is not a figure, it is a box with a
#: head.
#:
#: **Both consoles now afford this band, and the Nintendo 64 only just started
#: to.** The 9,400 figure above is the PlayStation's. The Nintendo 64 was
#: re-measured on the `preview` pin (``platform/nintendo64/scratch/``) and the
#: answer depends entirely on how a triangle is submitted:
#:
#:   ``rdpq_triangle``   12.9 us each   2,586 in a 30 fps frame
#:   Tiny3D               4.3 us each   7,701 in a 30 fps frame
#:
#: Sixteen figures at this band's ceiling is 4,800 triangles. That does not fit
#: the first row and does fit the second, so the band is affordable on that
#: console only through Tiny3D, which is why the toolchain pins it.
#:
#: The band has not been widened on the strength of it. 7,701 is a whole frame
#: and a board has to come out of it too, the measurement is submission cost
#: rather than fill, and nothing here has run on real hardware. A band widened
#: ahead of a scene that has actually been drawn is not a budget.
TRIANGLE_BAND = (150, 300)

#: Values a part contributes to the generated flat array, in this order.
PART_FIELDS = ("x0", "x1", "y0", "y1", "z0", "z1", "ramp", "rung")
VALUES_PER_PART = len(PART_FIELDS)


@dataclass(frozen=True)
class Part:
    """One convex box of a figure, in the figure's own space.

    ``name`` is carried for the diagnostics :func:`check` raises and is not
    emitted: a generated header of integers is byte-reproducible by the same
    argument every other generated header here is, and a part name in it would
    be a string nothing reads.
    """

    x0: int
    x1: int
    y0: int
    y1: int
    z0: int
    z1: int
    ramp: int
    rung: int
    name: str

    def values(self) -> Tuple[int, ...]:
        return (self.x0, self.x1, self.y0, self.y1, self.z0, self.z1,
                self.ramp, self.rung)


def depth_key(part: Part) -> float:
    """How far from the eye a part sits, at the shipped pitch.

    Depth falls with height by ``sin φ`` and rises with z by ``cos φ``, so this
    is the quantity an array authored far-to-near must be non-increasing in. The
    box's centre stands for the box, doubled to stay in integers-times-a-trig:
    a part's eight corners average to its centre, which is what the renderer's
    own corner-sum ordering reduces to.
    """
    sine = math.sin(math.radians(PITCH_DEGREES))
    cosine = math.cos(math.radians(PITCH_DEGREES))
    return cosine * (part.z0 + part.z1) - sine * (part.y0 + part.y1)


# ---------------------------------------------------------------------------
# The order the *console* puts the parts in
#
# :func:`depth_key` above is exact, and that is the whole of the trouble. The
# console does not have exact arithmetic and does not order on centres. It sums
# the eight **projected corner** depths of a part, and the Geometry
# Transformation Engine hands each of those back already truncated to a whole
# unit by a shift. Eight corners, eight truncations, so the sum a part is
# ordered by can sit up to eight units below the sum an exact reading gives. A
# pair whose exact lead is one or two units is not led at all on the machine.
# Two `pirates` figures the rule above accepted came back from the console with
# three ordering assertions down: a shirt stripe drawn behind its own shirt and
# a scarf tail authored behind the shoulder sorted in front of the head. That
# is why the second reading is here. Rule 3 above names the construction that
# produces the tie.
#
# Everything below is that comparison, written out. It carries no tolerance and
# no threshold: it is the machine's own arithmetic, and the question it answers
# is the machine's own question: *would the console agree that this array is
# far-to-near?* A rule stated as "keep a margin of at least N units" would be a
# number somebody picked; this is a number nobody picked.
# ---------------------------------------------------------------------------

#: The console's fixed point for a trigonometric value: 1.12, so one is 4096.
#: It is the width of the GTE's rotation-matrix entries, and it is the shift
#: that truncates each corner depth. The two are the same 4096, and that is why
#: the truncation is worth a whole world unit rather than a fraction of one.
MACHINE_ONE = 4096

#: The pitch's sine and cosine as the console holds them. Computed rather than
#: transcribed, so that a change of pitch moves both this and
#: :data:`MESH_WORLD_HEIGHT` and cannot move only one.
MACHINE_SINE = round(MACHINE_ONE * math.sin(math.radians(PITCH_DEGREES)))
MACHINE_COSINE = round(MACHINE_ONE * math.cos(math.radians(PITCH_DEGREES)))

#: One elevation step, in world units: a quarter of a tile. A figure that the
#: console draws in the order this file authored is a figure standing still, and
#: a figure standing still stands on a cell, so its feet are at a whole number
#: of these and never between two. (A moving figure is turned or leaning, and a
#: turned figure is re-sorted at run time from its projected corners, so the
#: authored order is not what draws it.)
ELEVATION_STEP = UNIT_WORLD // 4


def _corner_terms(part: Part) -> Tuple[int, ...]:
    """The eight corner depths of a part, less the constant its pose adds.

    A corner at world ``(x, y, z)`` is projected to depth
    ``(TRZ*4096 - sin*y + cos*z) >> 12``. The ``x`` is absent because the camera
    has no yaw — the projection has none by decision, rather than the camera
    happening to sit square — so a box's eight corners take only four distinct
    values, each twice.
    """
    return tuple(
        term
        for y in (part.y0, part.y1)
        for z in (part.z0, part.z1)
        for term in (-MACHINE_SINE * y + MACHINE_COSINE * z,) * 2
    )


def _standing_phases() -> Tuple[int, ...]:
    """Every remainder a standing figure's pose can leave in the shift.

    The pose enters every corner's depth as one added constant, so the only
    thing about it that survives the truncation is that constant's remainder
    modulo 4096. Two of its three terms cannot contribute one:

    * the camera's own ``TRZ`` is whole camera units and is multiplied by 4096,
      so it is a whole number of units at every corner and cancels;
    * the cell's ``z`` is a whole number of half-tiles, and at this pitch the
      cosine is exactly one half, so a cell centre also moves every corner by a
      whole unit.

    What is left is the figure's **height**, through a sine that is not a round
    fraction of anything. A standing figure's height is a whole number of
    :data:`ELEVATION_STEP`, capped by the tallest ground the terrain library
    draws. So this is a short list rather than all 4,096 remainders, and it is
    the art library's own registry that decides how short.
    """
    highest = max(kind.elevation for kind in terrain.TERRAINS.values())
    return tuple(sorted({
        (-MACHINE_SINE * ELEVATION_STEP * step) % MACHINE_ONE
        for step in range(highest + 1)
    }))


#: Computed once: it is a property of the pitch and of the terrain library, and
#: neither moves while a commission is being checked.
STANDING_PHASES = _standing_phases()


def machine_order_margin(before: Part, after: Part) -> int:
    """By how much the console agrees ``before`` is the further of the two.

    Positive is agreement, zero is a tie the console preserves either way, and
    **negative is the machine putting them the other way round**, the reading
    :func:`depth_key` cannot produce and the console acts on.

    The units are units of the eight-corner sum, which is four times a
    :func:`depth_key` difference: eight corners, each carrying a quarter of the
    box's doubled centre. So the eight units a truncation is worth are two units
    of :func:`depth_key`, which is where "a margin under two units is not a
    margin" came from, measured here rather than remembered.

    The worst of :data:`STANDING_PHASES` is returned, because a figure is
    authored once and stands wherever the board puts it.
    """
    return min(_margin_at(before, after, phase) for phase in STANDING_PHASES)


def machine_order_margin_at_focus(before: Part, after: Part) -> int:
    """The same margin, at the one pose the console's own check stands a figure.

    :func:`machine_order_margin` takes the worst of every standing phase, which
    is the right question for a figure that will be put anywhere on a board.
    This is the narrower one: a pair may be safe everywhere the board can
    actually place it and still be inverted at the focus, which is where the
    scratch photographs and where a reader looking at a still would see it.
    A caller wanting both readings asks for both.
    """
    return _margin_at(before, after, 0)


def _margin_at(before: Part, after: Part, phase: int) -> int:
    """The eight-corner sums differenced, at one pose remainder."""
    return (_truncated_sum(before, phase) - _truncated_sum(after, phase))


def _truncated_sum(part: Part, phase: int) -> int:
    """A part's eight corner depths, truncated and summed, less its pose.

    Python's ``//`` floors, which is what an arithmetic shift right does, so the
    truncation here is the machine's and not a rounding that resembles it.
    """
    return sum((phase + term) // MACHINE_ONE for term in _corner_terms(part))


def drawn_over_one_another(a: Part, b: Part) -> bool:
    """Whether swapping the two parts' order could change a pixel.

    Two parts drawn in disjoint parts of the screen have no order to get wrong,
    which is why a figure may carry pairs the console cannot separate, and
    every one of the fifty-six does. Both axes are decided in the figure's own
    space and neither needs the camera:

    * **x** survives projection untouched but for the perspective divide, since
      the camera has no yaw. Two boxes disjoint in x are drawn in disjoint
      columns, up to the fraction of a pixel their differing depths cost: a
      quarter-pixel at the widths and depths this board reaches, and the closest
      the library comes is one world unit.
    * **height on screen** falls with ``y·cos φ + z·sin φ`` and with nothing
      else, so a box's screen rows run from that quantity at its low near corner
      to its high far one. Two boxes disjoint in it are drawn in disjoint rows.
    """
    if a.x1 <= b.x0 or b.x1 <= a.x0:
        return False

    def rise(y: int, z: int) -> int:
        return MACHINE_COSINE * y + MACHINE_SINE * z

    a_low, a_high = rise(a.y0, a.z0), rise(a.y1, a.z1)
    b_low, b_high = rise(b.y0, b.z0), rise(b.y1, b.z1)
    return not (a_high <= b_low or b_high <= a_low)


# ---------------------------------------------------------------------------
# The silhouette a mesh is held to
#
# The rule is a mesh rule and lives here; the *measurement* is a PlayStation
# format question (which colour word the GPU skips) and lives in
# `playstation_header`, beside `clut_word`, which is the one definition of that
# word in this repository. So this module never has to say what transparent
# means, and the number a mesh is checked against is the number the console
# uploads by construction rather than by agreement.
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Silhouette:
    """One archetype's opaque box inside its 32x32 cell, in texels.

    Measured on faction colour zero. Every faction recolours inside the same
    silhouette: the drawing routine is the same and only the ramp differs.
    """

    width: int
    height: int
    area: int
    #: The opaque box of the **figure alone**, in texels: the same measurement
    #: as :attr:`height` taken above :data:`..characters.GROUND_Y`, so the
    #: faction disc under the feet is not counted as part of the person. The
    #: disc is four rows of every cell and identical for every class, and
    #: measuring through it is a mistake this repository has now made four
    #: separate times -- in ``fit_silhouette.py``, in ``taper_headgear.py``, in
    #: the recognisability metric, and in rule 1 itself.
    figure_height: int


def target_width(silhouette: Silhouette) -> int:
    """The world width the silhouette rule asks a mesh to be.

    The billboard is the 32x32 cell stretched to a ``side x side`` square, so
    the sprite's opaque box is drawn ``side*sw/32`` wide; a figure built
    ``unit_world`` tall is therefore asked for ``unit_world*sw/32`` of authored
    width. It is a *target*, not a constraint the emitter enforces: the knight
    was authored at 45 where the rule asks 44, and the archer at
    38 against 42, found the defect was never the boxes' proportions, and
    reproportioned neither.
    """
    return (UNIT_WORLD * silhouette.width) // characters.SPRITE


def target_height(silhouette: Silhouette) -> int:
    """The world height the silhouette rule asks a mesh to be **drawn**.

    Rule 4's other half, and for years the missing one. It is the same
    arithmetic as :func:`target_width` on the same box, and it is a *drawn*
    height rather than a built one: what a figure must be built to is whatever
    projects to this, which is :func:`build_scale`.

    Measured on :attr:`Silhouette.figure_height`, so the faction disc is not
    counted. A beast's sprite is nineteen rows where a knight's is twenty-eight,
    and holding both to one number is what made every beast in the roster half
    again too tall for its own drawing.
    """
    return (UNIT_WORLD * silhouette.figure_height) // characters.SPRITE


def projected_height(parts: Sequence[Part], scale: float = 1.0) -> float:
    """How tall this figure is actually drawn, in world units of screen height.

    The camera sends world y to screen vertical through ``cos φ`` and world z
    through ``sin φ``. Both, which is the whole of what rule 1 used to miss: a
    figure's drawn height is not its y-extent, it is the extent of ``y·cos φ +
    z·sin φ`` over every corner it has. A part pushed away raises the crown
    and a part pulled forward lowers the feet, so the top and the bottom are not
    in general the topmost and bottommost boxes.

    ``scale`` multiplies y only, because that is the one axis
    :func:`build_scale` is allowed to move: the width is rule 4's and the depth
    is what the ordering in rule 3 is built out of.
    """
    top = max(part.y1 * scale * PITCH_COSINE + part.z1 * PITCH_SINE
              for part in parts)
    bottom = min(part.y0 * scale * PITCH_COSINE + part.z0 * PITCH_SINE
                 for part in parts)
    return top - bottom


def build_scale(parts: Sequence[Part], silhouette: Silhouette) -> float:
    """What to multiply this figure's y **and z** by to draw it its own height.

    Rule 1 used to build every figure ``unit_world / cos φ`` = 128 units tall
    and say why: "two tiles, to be drawn one tile tall". It was not drawn one
    tile tall, for two reasons that multiply to 1.39 -- which is exactly the
    factor by which every mesh in this repository measured narrower than its
    sprite when the two were brought to the same height.

    * **A tile is 32 rows and the sprite's figure is 28 of them.** The other
      four are the faction disc. Sized to the tile, a figure stands 32/28
      taller than the sprite beside it.
    * **Depth buys screen height and was never paid for.** The paragraph above
      rule 1 has always said so -- "depth spent on one part is height the whole
      figure gives back" -- but rule 1 pinned the height to a constant, so the
      figure never gave it back. A 16-unit z-span adds 22%.

    **Both axes, and that is the whole of why it is one number.** The projection
    is ``y·cos φ + z·sin φ``, so scaling y and z together scales the drawn
    height by exactly that factor and nothing has to be solved. Scaling y alone
    would also land the height -- and it was tried -- but it changes every box's
    ``dy/dz``, which is the one ratio the paragraph above rule 1 says decides
    whether a part reads as a wall or as a plate. Measured over the roster,
    y alone turns 41% of parts into plates into 70%, and the figures go flat:
    a staircase of bright top faces, which is exactly the failure that
    paragraph was written about. Scaling both leaves the ratio alone, and
    leaves the plate share where the author put it.

    x is not scaled, because the width is rule 4's and is already right.
    """
    return target_height(silhouette) / projected_height(parts)


def authored_width(parts: Sequence[Part]) -> int:
    """How wide the part list actually is, in world units."""
    return max(part.x1 for part in parts) - min(part.x0 for part in parts)


# ---------------------------------------------------------------------------
# The rules, enforced
# ---------------------------------------------------------------------------

#: How far an authored width may sit from what the silhouette rule asks before
#: it stops being a proportion and starts being a mistake. Measured rather than
#: picked: the two shipped models sit +1 and -4 world units off their targets,
#: and drawing them established that the fit closes that gap without
#: reproportioning anything. A model twice as far out is one whose scales would
#: be doing the work its boxes should have done.
WIDTH_TOLERANCE = 8



def check_commission(commission: Mapping[str, Tuple[Part, ...]],
                     measured_silhouettes: Sequence[Silhouette],
                     where: str) -> List[str]:
    """Every mesh rule, checked over one style's commission.

    Returns the archetypes it accepted, in the roster's order. ``commission``
    is one style's ``archetype -> parts`` mapping and ``where`` names that
    style, so the diagnostics say which of the seven a bad part is in.

    ``measured_silhouettes`` is one entry per archetype in
    :data:`characters.ARCHETYPE_ORDER`, as
    :func:`..playstation_header.silhouettes` measures them **for that same
    style**. A mesh is held to its own style's sprite and to no other.

    Raises :class:`AssertionError` naming the offending part rather than
    emitting a mesh that breaks a rule, which is the discipline
    :mod:`..frames` already applies to a pose that would clip a pixel.
    """
    assert len(measured_silhouettes) == len(characters.ARCHETYPE_ORDER), (
        "a silhouette per archetype, in the roster's order")
    measured = dict(zip(characters.ARCHETYPE_ORDER, measured_silhouettes))
    for archetype in commission:
        assert archetype in characters.ARCHETYPE_ORDER, (
            f"{where}/{archetype}: a mesh names an archetype the roster does "
            f"not hold")
    accepted: List[str] = []
    # The roster's order rather than the mapping's, so the accepted list is the
    # same list however an author happened to write the table down.
    for archetype in characters.ARCHETYPE_ORDER:
        parts = commission.get(archetype)
        if parts is None:
            continue
        _check_figure(parts, measured[archetype], f"{where}/{archetype}")
        accepted.append(archetype)
    return accepted


def _check_figure(parts: Tuple[Part, ...], silhouette: Silhouette,
                  where: str) -> None:
    """One figure against every rule. ``where`` is ``style/archetype``."""
    assert parts, f"{where}: a mesh with no parts is not a mesh"

    # Rule 3, first half: every part is a non-degenerate convex box.
    for part in parts:
        assert part.x0 < part.x1 and part.y0 < part.y1 and part.z0 < part.z1, (
            f"{where}: part '{part.name}' is not a box with volume")

    # Rule 1: feet on the ground, and drawn the height its own sprite is drawn.
    # Measured on the projection rather than on the y-extent, because the pitch
    # sends depth to screen height too and a figure that ignores that is drawn
    # taller than the sprite it stands in for.
    assert min(part.y0 for part in parts) == 0, (
        f"{where}: the figure's feet are not at y = 0")
    assert max(part.y1 for part in parts) <= MESH_WORLD_HEIGHT, (
        f"{where}: built {max(part.y1 for part in parts)} units tall, past the "
        f"{MESH_WORLD_HEIGHT}-unit ceiling a figure of no depth would need")

    # Rule 2: a ramp and a rung, never a colour. There is nowhere for a
    # colour to be, so what is checkable is that both indices exist.
    for part in parts:
        assert 0 <= part.ramp < RAMP_COUNT, (
            f"{where}: part '{part.name}' names ramp {part.ramp}")
        assert 0 <= part.rung < RUNG_COUNT, (
            f"{where}: part '{part.name}' names rung {part.rung}")
    assert any(part.ramp == RAMP_FACTION for part in parts), (
        f"{where}: no part wears the faction ramp, so all six factions "
        f"would draw the same figure")

    # Rule 3, second half: authored far to near at the shipped pitch. Asked
    # twice, because the two readings disagree and the second is the one that
    # draws: exactly, on centres, which is what the order *means*; and then in
    # the console's own truncated arithmetic, which is what the console will
    # actually do with it.
    for before, after in zip(parts, parts[1:]):
        assert depth_key(before) >= depth_key(after), (
            f"{where}: part '{after.name}' is further from the eye than "
            f"'{before.name}' before it, so the array is not far-to-near "
            f"at {PITCH_DEGREES} degrees and the scene would need a depth "
            f"buffer")
        margin = machine_order_margin(before, after)
        if margin < 0 and drawn_over_one_another(before, after):
            raise AssertionError(
                f"{where}: the console orders part '{after.name}' behind "
                f"'{before.name}' before it, by {-margin} units of its "
                f"eight-corner sum, at an elevation the board can stand this "
                f"figure at, and the two are drawn over one another, so the "
                f"order shows. Exact arithmetic on centres puts them "
                f"{depth_key(before) - depth_key(after):.2f} units apart, "
                f"which is inside what the Geometry Transformation Engine's "
                f"shift is worth. Buy the depth in the boxes: a part needs "
                f"two units of depth_key over the one behind it before the "
                f"machine can tell them apart")

    # The triangle cap, on both sides.
    triangles = len(parts) * TRIANGLES_PER_PART
    assert TRIANGLE_BAND[0] <= triangles <= TRIANGLE_BAND[1], (
        f"{where}: {triangles} triangles is outside the measured "
        f"{TRIANGLE_BAND[0]}-{TRIANGLE_BAND[1]} band")

    # Rule 4: the mesh is in the neighbourhood of its own sprite's width.
    # The fit closes the rest; a model this far out is not a fit's problem.
    assert silhouette.width > 0 and silhouette.height > 0, (
        f"{where}: the sprite has no opaque silhouette to target")
    asked = target_width(silhouette)
    actual = authored_width(parts)
    assert abs(actual - asked) <= WIDTH_TOLERANCE, (
        f"{where}: authored {actual} world units wide where its sprite's "
        f"silhouette asks {asked}, past the {WIDTH_TOLERANCE}-unit "
        f"tolerance. Reproportion the boxes rather than widening the "
        f"tolerance")

    # Rule 4, the other half: drawn the height its sprite is drawn. This is
    # rule 1's second sentence, measured here because it is the same
    # measurement on the same box.
    assert silhouette.figure_height > 0, (
        f"{where}: the sprite has no figure above the faction disc to target")
    wants = target_height(silhouette)
    drawn = projected_height(parts)
    assert abs(drawn - wants) <= HEIGHT_TOLERANCE, (
        f"{where}: drawn {drawn:.1f} world units tall where its sprite's "
        f"figure asks {wants}. Scale the figure's y by "
        f"build_scale(parts, silhouette) rather than moving the tolerance")
