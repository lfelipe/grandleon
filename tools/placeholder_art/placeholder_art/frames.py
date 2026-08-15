# SPDX-License-Identifier: MIT
"""Animation frames: the roster in motion, without a second drawing routine.

Displacing a whole sprite is enough for a token that slides, a struck token
knocked back, or a cursor ring closing in. A walk cycle and an attack lunge are
not offsets: they are changes to the body itself, and this module is where they
come from.

Why a pose and not a drawing
----------------------------
The obvious way to grow frames is to write them: one more ``draw`` per pose per
archetype. Priced, that is the reason not to. The roster is closed at eight
precisely because *one* new archetype costs one routine in every style, and the
same multiplication applies here with an extra factor: three
poses across eight archetypes is twenty-four routines **per style**, forever, and
every style commissioned afterwards would owe twenty-four rather than eight. That
would have tripled the price of the capability the roster was closed to protect.

So a frame is a **pose applied to the drawn body**, not a second drawing. Each
archetype's own routine renders its own pixels exactly as it always did; the
pose then displaces those pixels (a shank lifted at the knee, a torso coiled
onto the legs) before the ink outline is traced, so the silhouette that outline
follows is the posed one rather than the standing one.

Three consequences, and all three are load-bearing:

* **Every style ships every frame, by construction.** A pose knows nothing about
  which routine drew the pixels, so a style cannot ship a partial sequence any
  more than it can ship a partial roster. :mod:`.styles` asserts it anyway.
* **Zero palette entries are appended.** A pose only ever *moves* an index it
  was handed. It cannot invent a colour, so the master palette is untouched and
  the ``n64_ci8`` profile's ``PLTE`` chunk, which carries the whole palette into
  every asset, is byte-identical to what it was.
* **The frames are as deterministic as the standing sprite.** No RNG is drawn
  here; the pose is integer displacement of an already-deterministic canvas.

The sequence
------------
:data:`FRAME_ORDER` is the order the cells appear in a sequence sheet, and the
order itself is the contract. A sheet is
``characters/<archetype>_<colour>_frames.png``: one row of four 32x32 cells,
128x32 in all, holding ``walk_contact``, ``walk_pass``, ``lunge`` and ``cast``
in exactly that sequence. **Position is the contract, not the name.** A client
indexes the strip by cell number and never by name, so a sheet that omits a
cell shifts every later one into a pose it is not, and a sheet that renames one
changes nothing a client can see. Every style ships every cell for the same
reason: there is no per-style animation table for a shorter sequence to be
declared in.

The **standing** sprite is not in the sheet: it is the sequence's frame 0, and
it ships as ``characters/<archetype>_<colour>.png``, one 32x32 cell under a
filename that may not move. It is drawn whenever a unit is at rest, so
duplicating it into the strip would cost every console a cell it already holds.

``walk_contact`` / ``walk_pass``
    The two beats of a walk: weight landing, then weight over the planted foot
    with the other swinging past. Two cells and not four: at six frames a tile
    (``grandleon::view::slide_frames_per_tile``) a four-pose cycle would hold
    each pose for a frame and a half, and a pose held for a fraction of a frame
    is a pose no console can draw. One cell a tile is what the six divides
    cleanly into, and it also makes a step mean a tile.
``lunge``
    The body coiled into a blow, over the six frames a landed hit already
    spends. The struck token's flinch and the striker's lunge are the same six
    frames seen from the two ends.
``cast``
    The body holding itself still around a spell. It is also the last cell the
    sheet will ever hold. See ``FRAME_ORDER`` for the arithmetic that closes
    it: 512 bytes a cell against 2,048 texel bytes of the Nintendo 64's texture
    memory is four, and this is the fourth.

    It is the negative of the lunge rather than a new idea, because the negative
    is what was affordable. A lunge closes the sprite by a row at the shoulder;
    a cast opens it by two, and draws the feet up instead of planting them. That
    is one row of arithmetic, applied to the same body, and it is legible on the
    six robed silhouettes that have no legs to pose at all.

A travelling bolt and a resolving flare deliberately appear nowhere in this
file. They are not poses of a body but marks over the board, they are drawn from
primitives in colours the palette already holds, and keeping them out of the
sheet is exactly what left the fourth cell for a pose.

Direction is not drawn, and the note below says why it cannot be: the standing
art leaves no margin to lean into. The two ways out are a roster redrawn inside
a smaller bounding box, which moves art nobody asked to change, and a
horizontal flip at draw time, which the Nintendo 64 pays a texture coordinate
for. Neither is priced, so neither is guessed at here.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Dict, Tuple

from .palette import TRANSPARENT
from .raster import Canvas

#: Where the body is cut for a walk pose. Below this row a swinging leg leaves
#: the body behind; above it, the body moves as one. It is the knee and not the
#: hip on purpose: a leg that bends at the hip reads as a body split in two,
#: and a leg that bends at the knee reads as a leg.
KNEE_Y = 24

#: Where the body is cut again for a lunge, between the head and the torso.
SHOULDER_Y = 12

# ---------------------------------------------------------------------------
# Every pose here displaces pixels *inward*, and that is measured rather than
# stylistic. The standing sprites already fill their cells: across both styles
# the widest reach x 0..31 (``stormcaller``) and four archetypes touch row 0
# (``knight``, ``mage``, ``stormcaller``, ``commander``). There is no margin to
# lean into. A forward lean is the pose an attack most obviously wants, and it
# would push a staff, a banner or a helmet crest straight off the cell.
#
# So a pose here only ever moves a pixel toward ground the drawing already
# occupies: a shank rises into the body above it, a torso settles onto the legs
# below it. The one direction with room is *down*, and only two rows of it: the
# lowest opaque row across the whole roster is 28 before the ink outline is
# traced, and the outline needs the row under that.
#
# This is the number an external artist most needs from this file, and it is a
# rule rather than advice: **leave a margin or the frame cannot move.** At the
# 32x32 cell size it makes three demands. ``walk_contact`` moves everything at
# or below row 24 outward by one column, so **nothing may sit on column 0 or
# column 31 below row 24**. ``walk_contact`` moves everything above row 24 down
# two rows and ``lunge`` moves it down three, so a drawing owes three rows of
# headroom under the lowest opaque row above the knee line, which the ground
# line at row 28 already leaves. ``walk_pass`` and ``cast`` displace only
# inward and ask for nothing.
#
# The pipeline neither makes room nor clips silently: ``displace`` raises with
# the offending pixel's coordinates, and a provided sheet is measured against
# the same bound under the refusal code ``cell-margin``. The generated roster
# left no margin, so the generated frames pose inward. The lean, and with it
# facing, is deferred rather than faked.
# ---------------------------------------------------------------------------

#: How far the body settles on the contact frame, and how far the swinging
#: shank rises on the passing one. Two rows down and three rows up on a
#: 32-pixel sprite. Two rows because one is invisible at the console's 25-pixel
#: cell, where the sprite is drawn at about three quarters scale, and because
#: **the body's rise and fall is the only part of a walk a robe can show**. Four
#: of the sixteen roster silhouettes (both styles' ``mage`` and ``healer``) have
#: no shank below the hem at all, so a walk carried by the legs alone would have
#: left a quarter of the roster gliding.
WALK_BOB = 2
WALK_LIFT = 3

#: How far the feet are drawn apart on the contact frame, and how far a swinging
#: foot comes toward the centreline on the passing one. A foot that lifts
#: straight up reads as a limp; a foot that lifts and comes in reads as a step.
WALK_STRIDE_OUT = 1
WALK_SWING_IN = 1

#: The coil of a lunge: the torso settles this far onto the planted legs, and
#: the head and shoulders this far again, so the sprite squashes by one row at
#: the shoulder line rather than merely sinking. Squash is what makes a blow
#: read as thrown from the body instead of dropped.
LUNGE_CROUCH = 2
LUNGE_SQUASH = 3

#: The gather of a cast, and the exact photographic negative of the lunge at the
#: shoulder line. The torso settles this far while **the head does not move at
#: all**, so the sprite *lengthens* by two rows at the shoulder where a lunge
#: loses one. Lengthening is what makes a cast read as a body holding itself
#: still around something rather than throwing something.
#:
#: Choosing the shoulder line rather than a new cut is the whole trick. There is
#: no upward margin to draw a figure up into (see the note above: seven of
#: thirty-two silhouettes touch row 0), so a cast cannot rise. It can only be the
#: *distribution* of a settle that a lunge is not, and the shoulder is where a
#: viewer reads the difference.
CAST_GATHER = 2

#: How far both shanks draw up off the ground on a cast: the same three rows a
#: walk's swinging shank rises, applied to both legs at once, so the feet leave
#: the ground together and the hem lifts. The lift is strictly inward: a shank
#: rises into the body above it. Like every pose here, it cannot break the
#: margin rule.
#:
#: And it is the reason the cast is drawn at the shoulder as well as at the
#: knee. Six of the thirty-two generated silhouettes have no shank below the
#: hem: ``mage`` and ``healer`` in every style, which are **exactly the
#: archetypes that cast**. A pose carried by the legs alone would have left the
#: whole casting half of the roster standing perfectly still through its own
#: gesture, which is the same trap the walk found and the reason ``WALK_BOB``
#: exists.
CAST_LIFT = 3


@dataclass(frozen=True)
class Frame:
    """One cell of a sequence: a name, and the pose that makes it."""

    name: str
    label: str
    animation: str
    summary: str
    pose: Callable[[Canvas], Canvas]


def displace(
    body: Canvas, offset: Callable[[int, int], Tuple[int, int]]
) -> Canvas:
    """Move every opaque pixel of ``body`` by a per-pixel integer offset.

    Nothing is dropped silently: a pose that would push a pixel off the cell is
    a pose that would lose a limb, so it raises instead. That is what makes
    the inward-only rule above a measured constraint rather than a stylistic
    one: a pose that tried to lean would fail the build here.

    :mod:`.figures` displaces a body by this same function, and deliberately: a
    figure is applied exactly as a pose is, so it obeys the margin rule for the
    same reason and fails at the same line when it does not.
    """
    out = Canvas(body.width, body.height)
    for y in range(body.height):
        row = y * body.width
        for x in range(body.width):
            index = body.data[row + x]
            if index == TRANSPARENT:
                continue
            dx, dy = offset(x, y)
            target_x = x + dx
            target_y = y + dy
            if not (0 <= target_x < out.width and 0 <= target_y < out.height):
                raise AssertionError(
                    f"a pose pushed the pixel at ({x}, {y}) to "
                    f"({target_x}, {target_y}), outside the "
                    f"{out.width}x{out.height} cell; the frame would lose it"
                )
            out.data[target_y * out.width + target_x] = index
    return out


def stand(body: Canvas) -> Canvas:
    """Frame 0: the body exactly as its archetype drew it."""
    return body.copy()


def _contact(body: Canvas) -> Canvas:
    """The down-beat: weight landing, feet apart, body at its lowest.

    The body settling rather than the far foot reaching is what keeps every
    crest, staff and banner on the cell (see the note above). It is also the
    truthful half of the beat, because a walking body is lowest at the moment a
    foot takes it.
    """

    def offset(x: int, y: int) -> Tuple[int, int]:
        if y >= KNEE_Y:
            centre = body.width // 2
            if x < centre:
                return (-WALK_STRIDE_OUT, 0)
            return (WALK_STRIDE_OUT, 0)
        return (0, WALK_BOB)

    return displace(body, offset)


def _pass(body: Canvas) -> Canvas:
    """The up-beat: weight over the planted foot, the other one swinging past.

    The body is back at its standing height, which is what makes the pair a
    cycle rather than a crouch: over a move the token rises and falls once a
    tile, and a silhouette with no legs still reads as walking.
    """

    def offset(x: int, y: int) -> Tuple[int, int]:
        if y >= KNEE_Y and x > body.width // 2:
            return (-WALK_SWING_IN, -WALK_LIFT)
        return (0, 0)

    return displace(body, offset)


def _lunge(body: Canvas) -> Canvas:
    """The body coiled into a blow, feet planted.

    The torso settles onto the legs and the head and shoulders settle further
    still, so the sprite loses a row at the shoulder line: a squash rather than
    a sink. A lean would have read better and cannot be drawn: the standing art
    leaves no margin to lean into.
    """

    def offset(x: int, y: int) -> Tuple[int, int]:
        if y >= KNEE_Y:
            return (0, 0)
        return (0, LUNGE_SQUASH if y < SHOULDER_Y else LUNGE_CROUCH)

    return displace(body, offset)


def _cast(body: Canvas) -> Canvas:
    """The body holding itself still around a spell, feet drawn off the ground.

    The lunge and the cast are the two gestures a body can make here, and they
    are told apart at the shoulder line by the opposite halves of one row of
    arithmetic. A lunge settles the torso two rows and the head three, so the
    sprite *closes* by one at the shoulder. A cast settles the torso two rows and
    the head not at all, so the sprite *opens* by two. The shanks rise into the
    body rather than planting, so the figure gathers at the hem instead of
    bracing against the ground.

    Nothing moves horizontally and nothing moves up out of the cell, so this pose
    cannot fail the margin rule at any cell size: the lowest opaque row rises
    from 28 to 25 and the highest does not move at all. It is the only pose in
    the sequence with that property, which is a happy consequence of there being
    no room left to lean and none needed.
    """

    def offset(x: int, y: int) -> Tuple[int, int]:
        del x
        if y >= KNEE_Y:
            return (0, -CAST_LIFT)
        if y < SHOULDER_Y:
            return (0, 0)
        return (0, CAST_GATHER)

    return displace(body, offset)


#: The cells of a sequence sheet, left to right: ``walk_contact``,
#: ``walk_pass``, ``lunge``, ``cast``. This order is the contract; a client
#: indexes it by position and never by name, so a sheet that drops a cell
#: shifts every later one, and appending is the only safe way to grow it.
#:
#: **This order is full at four.** A cell is 32x32 at four bits a texel, which
#: is 512 bytes, and a colour-indexed texture may hold 2,048 bytes of the
#: Nintendo 64's 4 KiB TMEM (the palette bank takes the other half), so a strip
#: that a client can upload whole stops at 2,048 / 512 = 4 cells.
#: ``grandleon::view::sequence_cell_count`` states that arithmetic as a
#: ``static_assert`` rather than as advice, and
#: ``editor/src/domain/board-motion.ts`` mirrors it as
#: ``SEQUENCE_CELL_CEILING``. Every renderer blits one cell at a time, so a
#: fifth cell would compile and run and break only the property that the strip
#: uploads whole. A fifth pose costs a narrower cell, a second upload or a
#: different texture format, and whichever is chosen is priced before it is
#: spent.
FRAME_ORDER: Tuple[Frame, ...] = (
    Frame(
        "walk_contact", "Walk: contact", "walk",
        "The down-beat: weight landing, feet apart, the body at its lowest.",
        _contact,
    ),
    Frame(
        "walk_pass", "Walk: pass", "walk",
        "The up-beat: the body back at standing height with a foot swinging "
        "past it, which is what makes the pair a cycle.",
        _pass,
    ),
    Frame(
        "lunge", "Lunge", "attack",
        "The body leaned into a blow, feet planted, over the six frames a "
        "landed hit already takes.",
        _lunge,
    ),
    Frame(
        "cast", "Cast", "cast",
        "The body holding itself still around a spell: the head held where it "
        "stood, the torso settled under it and both feet drawn off the ground.",
        _cast,
    ),
)

#: Cells in a sequence sheet. The standing sprite is frame 0 and is not in it.
FRAME_COUNT: int = len(FRAME_ORDER)

FRAMES_BY_NAME: Dict[str, Frame] = {frame.name: frame for frame in FRAME_ORDER}

#: Frame names in sheet order, which is what the manifest and every generated
#: table publish.
FRAME_NAMES: Tuple[str, ...] = tuple(frame.name for frame in FRAME_ORDER)

#: The animations the sheet's cells belong to, in sheet order, so a client can
#: group the cells without a table of its own.
ANIMATION_ORDER: Tuple[str, ...] = ("walk", "attack", "cast")

ANIMATION_FRAMES: Dict[str, Tuple[str, ...]] = {
    animation: tuple(
        frame.name for frame in FRAME_ORDER if frame.animation == animation
    )
    for animation in ANIMATION_ORDER
}

for _animation in ANIMATION_ORDER:
    assert ANIMATION_FRAMES[_animation], (
        f"animation {_animation} has no frames; an animation named in "
        f"ANIMATION_ORDER must own at least one cell of the sheet"
    )
for _frame in FRAME_ORDER:
    assert _frame.animation in ANIMATION_ORDER, (
        f"frame {_frame.name} belongs to animation {_frame.animation}, which "
        f"is not in ANIMATION_ORDER"
    )
