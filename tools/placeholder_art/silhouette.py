# SPDX-License-Identifier: MIT
"""The two drawings of one figure as outlines, brought to a common height.

A sprite and a model of the same role are drawn by different machinery at
different sizes, so the only way to ask how well one sits on the other is to
render both, throw the colour away and scale the two outlines to the same
number of rows. That is all this module does: :func:`sprite_ink` and
:func:`mesh_ink` render, :func:`mask` reduces a rendering to a set of cells.

It exists as its own module because more than one tool asks that question and
the answer has to be the same one each time -- a tool that measured a figure
against a differently-cropped or differently-scaled sprite would report a
difference that is its own.

:mod:`roster_comparison` holds the pieces underneath (the console profile the
sprite is reduced through, the faction it is drawn in, and the tile-dropping
that :func:`~.roster_comparison._mesh_ink` does), and is imported rather than
repeated so a measurement here cannot disagree with the page a person looks at.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import List, Sequence, Set, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import roster_comparison as rc
from placeholder_art import (characters, meshes, playstation_header, preview3d,
                             styles)

Ink = List[Tuple[int, int, Tuple[int, int, int]]]

#: How many rows both outlines are scaled to before they are compared. The
#: character grid's own height, so a sprite is measured at its native size and
#: only the model is resampled.
ROWS = characters.GROUND_Y


def mesh_ink(parts: Sequence[meshes.Part], style: styles.Style,
             archetype: str, cells) -> Ink:
    """A part table rendered at the shipped camera, as (x, y, colour)."""
    ramps = preview3d.ramp_colours(
        playstation_header.mesh_ramp_words(cells, style, archetype,
                                           rc.FACTION),
        playstation_header.clut_channels)
    preview = preview3d.render(
        rc.faces_from(sorted(parts, key=meshes.rules.depth_key, reverse=True)),
        ramps)
    return rc._mesh_ink(preview)


def sprite_ink(style: styles.Style, archetype: str,
               figure: str = "first") -> Ink:
    """One standing sprite as (x, y, colour), without its faction disc.

    The disc is the cell the figure stands on, exactly as the board tile is for
    a model, and :func:`~.roster_comparison._mesh_ink` already drops the tile.
    Leaving it in on one side only would make every sprite measure four rows
    taller than the person in it, and so be drawn smaller than the model beside
    it -- the same bug that skewed the width profiles, in a different place.
    """
    cell = rc.sprite_cell(style, archetype, figure)
    return [(x, y, colour) for (x, y, colour) in rc._sprite_ink(cell)
            if y < characters.GROUND_Y]


def mask(ink: Ink, rows: int = ROWS) -> Tuple[Set[Tuple[int, int]], int]:
    """An outline as a set of cells, scaled to `rows` tall, and its width.

    The width comes back because the caller needs it to centre two outlines of
    different widths against each other: a figure narrower than its sprite that
    sits at the left of its own box is not thereby a worse match.
    """
    if not ink:
        return set(), 0
    xs = [point[0] for point in ink]
    ys = [point[1] for point in ink]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    scale = rows / (y1 - y0 + 1)
    columns = max(1, int(round((x1 - x0 + 1) * scale)))
    drawn = {(x, y) for x, y, _ in ink}
    cells = {(column, row)
             for row in range(rows)
             for column in range(columns)
             if (x0 + int(column / scale), y0 + int(row / scale)) in drawn}
    return cells, columns
