# SPDX-License-Identifier: MIT
"""The reduction the Nintendo 64 actually performs on a figure.

Why this exists beside the legibility reduction
-----------------------------------------------
:data:`.profiles.LEGIBILITY` asks whether a sprite still reads at half size in
four tones, and it asks it through a **box filter** (which is what the Pillow
pin in ``requirements.txt`` protects). That is a real question and it has
forced a redraw in every style commission.

It is not, however, the transform a player sees. The console draws a 32-texel
cell into a `tile`-pixel one and figures are **point sampled**: ``play_rom.cpp``
sets ``FILTER_BILINEAR`` for terrain and says so in as many words a few lines
above the token draw, "the ground is filtered and the figures are not". Point
sampling does not average a dropped column into its neighbour the way a box
filter does. It deletes it. A one-texel highlight that lands on a dropped
column is not dimmed, it is *gone*, and the box-filtered reduction cannot see
that happen because a box filter always carries some of every texel forward.

So the two reductions fail on opposite mistakes, and the library was only ever
measured against one of them. This module is the other one. It writes nothing
and ships nothing; :func:`.verify.check_decimation` is the whole of what reads
it.

The arithmetic is the console's, not an approximation of it
-----------------------------------------------------------
It is the same derivation ``play_rom.cpp`` already carries for its probes, and
it is reproduced rather than re-invented so that the two cannot drift into
disagreeing about which texel reaches a pixel.

libdragon writes a ``TEXRECT`` whose step is ``((s1 - s0) << 7) / (x1 - x0)``
with the source in 10.5 and the screen in 10.2, which is the step in 5.10 per
screen pixel; the texture unit accumulates it and keeps five fractional bits.
For a 32-texel cell drawn into a ``tile``-pixel one::

    step     = (32 * 32 << 7) / (tile * 4)
    coord(p) = p * step
    texel(p) = coord(p) >> 10

:func:`taps` is that last line for every ``p`` across the cell. The check that
it is the console's arithmetic and not a plausible-looking guess is that it
reproduces the one value ``play_rom.cpp`` states in prose: on the 25-pixel cell
the centre pixel lands on texel 15 and 11/32 of the way to texel 16.
:func:`check_centre_tap` asserts exactly that, so a change to this formula fails
here rather than drifting silently away from the ROM it claims to model.

Which cell sizes a figure must survive
--------------------------------------
Not one. ``play_rom.cpp`` gives the console ``view::FitRule{300, 200, 26, 14,
18}``: a 300x200 board frame, a cell never larger than 26 nor smaller than 14,
and 18 for a board too large to fit. The cell is then ``frame_w / width`` for
the board actually being played, so **every integer from 14 to 26 is reachable**
and which one a figure gets is a property of the map, not of the art.

That is the part worth stating plainly, because it is the reason a single
measurement at 25 would have been the wrong instrument: a sprite that reads
beautifully on a small board can lose a face on a large one, and the art has no
say in which board it is drawn on.

These five numbers are the ROM's and are restated here. That restatement is a
drift risk of exactly the kind this repository refuses to leave to prose, so
``play_rom.cpp`` carries a ``static_assert`` naming :data:`SMALLEST_CELL` and
:data:`LARGEST_CELL` against its own ``FitRule``, and this module is what that
assert is defending.
"""

from __future__ import annotations

from typing import Dict, List, Tuple

from . import palette
from .raster import Canvas

#: The native grid a figure is authored on. Spelled here rather than imported
#: from :mod:`.profiles` so that this module depends on nothing that writes an
#: output tree: it is a measuring instrument and imports like one.
NATIVE_GRID = 32

#: The smallest and largest cell the Nintendo 64 will draw a board at, from
#: ``view::FitRule{300, 200, 26, 14, 18}`` in ``play_rom.cpp``. The scrolling
#: cell (18) is inside this range and needs no separate entry; it is named in
#: the module docstring because it is the one a large board actually gets.
SMALLEST_CELL = 14
LARGEST_CELL = 26

#: Every cell size a figure can be drawn at on the console, smallest first. The
#: smallest is where the reduction bites hardest and is the number a bound
#: should be read at.
CELL_SIZES: Tuple[int, ...] = tuple(range(SMALLEST_CELL, LARGEST_CELL + 1))

#: The cell a board that fits is drawn at on the shipped campaigns, and the one
#: ``play_rom.cpp`` states its worked derivation for. Kept as a name because it
#: is the number the ROM's own prose can be checked against.
FITTING_CELL = 25


def step_of(cell: int) -> int:
    """The texture unit's step per screen pixel, in 5.10 fixed point."""
    return ((NATIVE_GRID * NATIVE_GRID) << 7) // (cell * 4)


def taps(cell: int) -> Tuple[int, ...]:
    """The texel each of a cell's screen pixels samples, across one axis.

    Point sampling takes the texel a coordinate falls in, which is the whole of
    what the shift below does. The same sequence serves both axes: the cell is
    square and the texture unit accumulates the same step down it.
    """
    step = step_of(cell)
    return tuple((pixel * step) >> 10 for pixel in range(cell))


def surviving(cell: int) -> Tuple[int, ...]:
    """The native columns that reach the screen at ``cell``, ascending.

    A column not in here is drawn, stored, uploaded, and then not sampled by
    any pixel. It costs everything a drawn column costs and shows nobody
    anything.
    """
    return tuple(sorted(set(taps(cell))))


def dropped(cell: int) -> Tuple[int, ...]:
    """The native columns no pixel samples at ``cell``, ascending."""
    kept = set(taps(cell))
    return tuple(index for index in range(NATIVE_GRID) if index not in kept)


def always_dropped() -> Tuple[int, ...]:
    """The columns no cell size samples: a figure's dead margin.

    There is exactly one, and it is the last. The texture unit accumulates its
    step from zero, so the last screen pixel of a cell lands on the texel below
    the top of the range rather than on it: ``play_rom.cpp`` states the same
    thing from the other side, that the last pixel "reaches texel 30 and 31",
    which is the pair a *filtered* sample blends. Point sampling takes the
    lower of that pair at every cell size, and never the upper.

    So column 31 and row 31 of every figure are drawn, stored, uploaded, and
    then sampled by nothing on this console. That is a property of the cell
    geometry rather than of any one drawing, which is why
    :func:`.verify.check_decimation` reports it and does not refuse it.
    """
    kept = set()
    for cell in CELL_SIZES:
        kept |= set(taps(cell))
    return tuple(index for index in range(NATIVE_GRID) if index not in kept)


def check_centre_tap() -> None:
    """Assert this module reproduces the ROM's own stated derivation.

    ``play_rom.cpp`` states, in prose beside ``centre_tap``, that on the
    console's 25-pixel cell the sampled centre pixel "comes out at texel 15 and
    11/32 of the way to texel 16, in both axes". If the arithmetic here is the
    console's, that value falls out of it. If it is not, this fails, and it
    fails at the formula rather than three checks downstream in a number nobody
    can trace back.
    """
    step = step_of(FITTING_CELL)
    coord = (FITTING_CELL // 2) * step
    texel, frac = coord >> 10, (coord >> 5) & 31
    if (texel, frac) != (15, 11):
        raise AssertionError(
            f"the decimation model puts the centre of a {FITTING_CELL}-pixel "
            f"cell at texel {texel} and {frac}/32, but play_rom.cpp derives "
            "texel 15 and 11/32. One of the two has moved, and they model the "
            "same texture unit"
        )


def decimate(canvas: Canvas, cell: int) -> Canvas:
    """Reduce one native cell exactly as the console's point sampler does.

    ``canvas`` is a single :data:`NATIVE_GRID` square cell, which is what the
    console blits: ``blit_sprite_cell`` sets ``s0 = cell * 32`` and
    ``width = 32``, so a sequence sheet's cells reduce independently and a
    figure never samples across the cell beside it.
    """
    if canvas.width != NATIVE_GRID or canvas.height != NATIVE_GRID:
        raise ValueError(
            f"a decimation reads one {NATIVE_GRID}x{NATIVE_GRID} cell, "
            f"got {canvas.width}x{canvas.height}"
        )
    tap = taps(cell)
    out = Canvas(cell, cell)
    for y, source_y in enumerate(tap):
        row = source_y * canvas.width
        for x, source_x in enumerate(tap):
            out.data[y * cell + x] = canvas.data[row + source_x]
    return out


def cells_of(canvas: Canvas) -> List[Canvas]:
    """Split a sheet into the :data:`NATIVE_GRID` cells the console blits."""
    if canvas.height != NATIVE_GRID or canvas.width % NATIVE_GRID:
        raise ValueError(
            f"a figure sheet is a row of {NATIVE_GRID}-pixel cells, "
            f"got {canvas.width}x{canvas.height}"
        )
    out: List[Canvas] = []
    for index in range(canvas.width // NATIVE_GRID):
        cell = Canvas(NATIVE_GRID, NATIVE_GRID)
        left = index * NATIVE_GRID
        for y in range(NATIVE_GRID):
            start = y * canvas.width + left
            cell.data[y * NATIVE_GRID:(y + 1) * NATIVE_GRID] = \
                canvas.data[start:start + NATIVE_GRID]
        out.append(cell)
    return out


def lost_colours(cell_canvas: Canvas, cell: int) -> Dict[int, int]:
    """Palette indices drawn in this cell that no pixel samples, and their cost.

    The value is how many native texels that index covers, which is the honest
    unit for "how much drawing went nowhere": one texel of a colour vanishing is
    a rounding accident, and eighty is a feature the player never sees.

    Transparency is not a loss and is never reported.
    """
    kept = set(taps(cell))
    drawn: Dict[int, int] = {}
    shown = set()
    for y in range(NATIVE_GRID):
        row = y * NATIVE_GRID
        for x in range(NATIVE_GRID):
            index = cell_canvas.data[row + x]
            if index == palette.TRANSPARENT:
                continue
            drawn[index] = drawn.get(index, 0) + 1
            if x in kept and y in kept:
                shown.add(index)
    return {index: count for index, count in drawn.items() if index not in shown}


def losses_by_size(cell_canvas: Canvas) -> Dict[int, Tuple[int, int]]:
    """``(colours, texels)`` lost at every cell size, for one cell.

    The whole sweep in one pass over the cell, because this runs on every sheet
    of every build: the drawn tally is taken once, and each size then reads only
    the positions it samples rather than the whole cell again.
    """
    drawn: Dict[int, int] = {}
    for y in range(NATIVE_GRID):
        row = y * NATIVE_GRID
        for x in range(NATIVE_GRID):
            index = cell_canvas.data[row + x]
            if index != palette.TRANSPARENT:
                drawn[index] = drawn.get(index, 0) + 1
    out: Dict[int, Tuple[int, int]] = {}
    for size in CELL_SIZES:
        kept = surviving(size)
        shown = set()
        for y in kept:
            row = y * NATIVE_GRID
            for x in kept:
                shown.add(cell_canvas.data[row + x])
        gone = [index for index in drawn if index not in shown]
        out[size] = (len(gone), sum(drawn[index] for index in gone))
    return out


def worst_loss(canvas: Canvas) -> Tuple[int, int, int]:
    """The heaviest colour loss anywhere in a sheet, over every cell size.

    Returns ``(texels, cell_size, cell_index)`` for the single worst cell, where
    ``texels`` is how many native texels the vanished colours covered. Zero
    means every colour this sheet draws reaches the screen at every size the
    console can draw it at.

    The maximum over cell sizes rather than a measurement at one of them,
    because a figure does not choose its board.
    """
    worst = (0, 0, 0)
    for cell_index, cell_canvas in enumerate(cells_of(canvas)):
        for size, (_, texels) in losses_by_size(cell_canvas).items():
            if texels > worst[0]:
                worst = (texels, size, cell_index)
    return worst


def describe(canvas: Canvas, cell: int, cell_index: int = 0) -> str:
    """One line naming what a cell loses at one size, for a report."""
    lost = lost_colours(cells_of(canvas)[cell_index], cell)
    if not lost:
        return f"cell {cell_index} at {cell}px: nothing lost"
    texels = sum(lost.values())
    names = ", ".join(
        f"{palette.RAMP_OF_INDEX[index][0]}"
        f"[{palette.RAMP_OF_INDEX[index][1]}]({count})"
        for index, count in sorted(lost.items(),
                                   key=lambda pair: (-pair[1], pair[0]))
    )
    return (f"cell {cell_index} at {cell}px: {len(lost)} colours and "
            f"{texels} texels never sampled -- {names}")


def report_markdown(report: Dict[str, object]) -> str:
    """``DECIMATION.md``: what the console's sampler drops, by cell size.

    The point of writing this down rather than leaving it in a build summary is
    that the numbers are only useful next to the sheet that produced them. A
    bound says the library has not got worse; this says where it is weakest,
    which is what a redraw needs.
    """
    worst = report["worst"]  # type: ignore[index]
    by_size = report["by_cell_size"]  # type: ignore[index]
    dead = ", ".join(str(column) for column in report["dead_margin_columns"])  # type: ignore[index]
    lines = [
        "# What the Nintendo 64 does not draw",
        "",
        "Generated by `tools/placeholder_art/generate.py`. Do not hand-edit.",
        "",
        "The console draws a 32-texel cell into a cell of "
        f"{SMALLEST_CELL} to {LARGEST_CELL} pixels and **point samples "
        "figures** (`play_rom.cpp`: \"the ground is filtered and the figures "
        "are not\"). A native column the sampler skips is not dimmed, it is "
        "dropped. This page is the measurement of that, taken on the same "
        "native canvases the build drew; `profiles.LEGIBILITY` measures a "
        "different reduction through a box filter and structurally cannot see "
        "it.",
        "",
        "## The dead margin",
        "",
        f"Column and row **{dead}** are sampled at no cell size at all, and "
        f"{report['cells_drawing_on_dead_margin']} of "  # type: ignore[index]
        f"{report['cells_measured']} figure cells draw on them. "  # type: ignore[index]
        "Every texel there is drawn, stored, uploaded and seen by nobody on "
        "this console.",
        "",
        "## Worst loss, by cell size",
        "",
        "A cell size is reachable when a board's width divides the frame to "
        "it, so all of these are real. `losing` counts cells that lose at "
        "least one colour outright.",
        "",
        "| cell | worst | losing | heaviest sheet |",
        "|---:|---:|---:|---|",
    ]
    for size in CELL_SIZES:
        row = by_size[size]  # type: ignore[index]
        lines.append(
            f"| {size}px | {float(row['worst_fraction']):.2%} | "
            f"{row['cells_losing']} | `{row['worst_path']}` |"
        )
    lines += [
        "",
        "## The heaviest cells in the library",
        "",
        f"Bound: a figure may lose at most **{float(report['bound']):.0%}** of "  # type: ignore[index]
        "its own opaque texels. The worst below is "
        f"{float(worst['fraction']):.2%}.",  # type: ignore[index]
        "",
        "| loss | texels | cell | sheet |",
        "|---:|---:|---:|---|",
    ]
    for entry in report["heaviest"]:  # type: ignore[index]
        lines.append(
            f"| {float(entry['fraction']):.2%} | {entry['texels']} | "
            f"{entry['cell_size']}px #{entry['cell_index']} | "
            f"`{entry['path']}` |"
        )
    lines.append("")
    return "\n".join(lines)
