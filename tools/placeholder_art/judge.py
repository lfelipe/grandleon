#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A judge that looks inside the outline, and says which class a figure reads as.

    tools/placeholder_art/judge.py --validate      # can it read the sprites?
    tools/placeholder_art/judge.py                 # score the roster

Every measure this project has used to score a figure has been a **silhouette**
measure, and every one of them has eventually been gamed. A silhouette cannot see
a face, a rung, a ramp or a value contrast: removing every face band from the
roster moves the silhouette score by exactly zero. So a search told to maximise
it will spend anything that does not change the outline, and it did -- breaking
joins, then collapsing depth, then swelling props, all while scoring better.

This is the missing half. It describes a figure by **coverage and value in a
grid**, so what is inside the outline counts, and it learns what each archetype
looks like from the **sprites** -- the art the meshes are answerable to -- rather
than from the meshes themselves. Then it can be asked of a mesh render: which
archetype does this read as, and by how much.

No deep learning framework, because this package has one dependency and keeps
it. The model is a Fisher-weighted nearest centroid: the mean descriptor of each
archetype, with every feature weighted by how well it separates the archetypes
in the first place (between-class spread over within-class spread). That is a
learned weighting arrived at in closed form -- no training loop, no seeds, no
GPU, and it is reproducible to the last decimal.

**It is validated before it is used.** A judge that cannot recognise the sprites
it was built from has no business scoring anything, so `--validate` holds out
each style in turn, trains on the other six and reports how well it reads the
style it never saw. It gets **74 of 112, 66%, against 12% for chance** -- so it
has learned what an archer is in a way that survives a style it has never met.

## What it is not

It reads *sprites* well and *meshes* only weakly, and the gap is measurable:

| | reads as own class | mean margin |
|---|---|---|
| sprites (what it learned from) | 52/56 positive | +0.041 |
| commissioned meshes | 21/56 (38%) | -0.004 |
| this roster | 15/56 (27%) | -0.013 |

The ordering is right -- it puts the hand-authored meshes above this roster,
which is what the silhouette measure says too -- but every mesh margin is an
order of magnitude smaller than every sprite margin, and both mesh sets average
*negative*. It learned from 32x32 pixel art with outlines and dithering and is
being asked about flat-shaded boxes; that is a domain gap, and the held-out test
above does not measure it, because that test was sprite against sprite.

**So it did not fix the optimiser.** Added to the search objective at equal
weight with the silhouette margin (`optimise_units.py --judge`), the figures
still degrade: the medieval healer's robe-and-legs structure collapses into a
blob, the archer's prop grows until it dominates, the rogue's blades lengthen.
Less violently than silhouette alone, and still not art.

The honest reading is that a weak signal added to a strong exploitable one does
not stop the exploitation. Closing the domain gap would mean learning from mesh
renders rather than sprites -- and the only mesh art worth learning from is the
56 commissioned figures, which is very little data and would teach the judge to
prefer exactly the meshes it was trained on.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art import (characters, figures, playstation_header,
                             preview3d, profiles, styles)
from placeholder_art.meshes import Part, authored
import roster_comparison as rc

PROFILE = profiles.PROFILES_BY_NAME["n64_ci4"]

#: The grid a figure is described in. Twelve is about the size a figure is
#: actually drawn at, so a cell is roughly a pixel of the thing being judged.
GRID = 12

#: Cells whose coverage is below this are treated as empty, so a single stray
#: corner pixel does not carry a value into the descriptor.
INK_FLOOR = 0.08


def luminance(colour: Tuple[int, int, int]) -> float:
    red, green, blue = colour[0], colour[1], colour[2]
    return (0.2126 * red + 0.7152 * green + 0.0722 * blue) / 255.0


def describe(points: Dict[Tuple[int, int], Tuple[int, int, int]]) -> List[float]:
    """A figure as coverage and value over a grid, normalised to its own box.

    Two numbers a cell. Coverage is the silhouette, which is what every earlier
    measure had; value is everything the silhouette could not see -- a dark
    visor against a light helm, a white robe against a blue one, the whole of
    what a ramp and a rung are for.
    """
    if not points:
        return [0.0] * (GRID * GRID * 2)
    xs = [x for x, _ in points]
    ys = [y for _, y in points]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    width = max(1, x1 - x0 + 1)
    height = max(1, y1 - y0 + 1)

    filled: List[int] = [0] * (GRID * GRID)
    value: List[float] = [0.0] * (GRID * GRID)
    for (x, y), colour in points.items():
        column = min(GRID - 1, (x - x0) * GRID // width)
        row = min(GRID - 1, (y - y0) * GRID // height)
        cell = row * GRID + column
        filled[cell] += 1
        value[cell] += luminance(colour)

    per_cell = max(1.0, (width * height) / (GRID * GRID))
    out: List[float] = []
    for cell in range(GRID * GRID):
        coverage = min(1.0, filled[cell] / per_cell)
        mean = (value[cell] / filled[cell]) if filled[cell] else 0.0
        out.append(coverage)
        out.append(mean if coverage >= INK_FLOOR else 0.0)
    return out


def sprite_points(style: styles.Style, archetype: str, colour: str,
                  figure: str, frame: str = ""):
    cell = rc.sprite_cell(style, archetype, figure) if not frame else None
    if cell is None:
        canvas = styles.sprite(style, archetype, colour,
                               frame=frame, figure=figure)
        cell = profiles.convert(canvas, PROFILE, is_sprite=True)
    transparent = set(cell.transparent)
    return {(x, y): cell.colours[cell.indices[y * cell.width + x]]
            for y in range(cell.height) for x in range(cell.width)
            if cell.indices[y * cell.width + x] not in transparent}


def sprite_sample(style: styles.Style, archetype: str, colour: str,
                  figure: str, frame: str):
    named = "" if figure == figures.DEFAULT_FIGURE.name else figure
    canvas = styles.sprite(style, archetype, colour, frame=frame, figure=named)
    cell = profiles.convert(canvas, PROFILE, is_sprite=True)
    transparent = set(cell.transparent)
    return {(x, y): cell.colours[cell.indices[y * cell.width + x]]
            for y in range(cell.height) for x in range(cell.width)
            if cell.indices[y * cell.width + x] not in transparent}


def mesh_points(parts: Sequence[Part], style: styles.Style, archetype: str,
                cells) -> Dict[Tuple[int, int], Tuple[int, int, int]]:
    ramps = preview3d.ramp_colours(
        playstation_header.mesh_ramp_words(cells, style, archetype, rc.FACTION),
        playstation_header.clut_channels)
    drawn = preview3d.render(rc.faces_from(list(parts)), ramps)
    out = {}
    for y in range(drawn.height):
        for x in range(drawn.width):
            colour = drawn.at(x, y)
            if colour is not None and colour != preview3d.TILE_COLOUR:
                out[(x, y)] = colour
    return out


class Judge:
    """Archetype centroids from sprites, with features weighted by how much
    they separate one archetype from another."""

    def __init__(self, samples: List[Tuple[str, List[float]]]):
        self.classes = sorted({name for name, _ in samples})
        width = len(samples[0][1])

        grouped: Dict[str, List[List[float]]] = defaultdict(list)
        for name, vector in samples:
            grouped[name].append(vector)

        self.centroid = {
            name: [sum(v[i] for v in group) / len(group) for i in range(width)]
            for name, group in grouped.items()
        }
        overall = [sum(self.centroid[c][i] for c in self.classes)
                   / len(self.classes) for i in range(width)]

        # Fisher weighting: a feature counts for as much as it separates the
        # archetypes and as little as it varies inside one. A feature that is
        # the same for every class, or wildly different between two knights,
        # carries no information about which class a figure is.
        self.weight = []
        for i in range(width):
            between = sum((self.centroid[c][i] - overall[i]) ** 2
                          for c in self.classes) / len(self.classes)
            within = 0.0
            count = 0
            for name, group in grouped.items():
                for vector in group:
                    within += (vector[i] - self.centroid[name][i]) ** 2
                    count += 1
            within = within / max(count, 1)
            self.weight.append(between / (within + 1e-4))
        total = sum(self.weight) or 1.0
        self.weight = [w * len(self.weight) / total for w in self.weight]

    def scores(self, vector: List[float]) -> List[Tuple[float, str]]:
        """Every archetype, best first, as a similarity in [0, 1]."""
        out = []
        for name in self.classes:
            centre = self.centroid[name]
            distance = sum(self.weight[i] * (vector[i] - centre[i]) ** 2
                           for i in range(len(vector)))
            out.append((1.0 / (1.0 + math.sqrt(distance)), name))
        out.sort(reverse=True)
        return out

    def read(self, vector: List[float], archetype: str) -> Tuple[str, float]:
        """What it reads as, and by how much it beats the nearest rival."""
        ranked = self.scores(vector)
        best = ranked[0][1]
        own = next(s for s, n in ranked if n == archetype)
        rival = max(s for s, n in ranked if n != archetype)
        return best, own - rival


def gather(style_names: Sequence[str]) -> List[Tuple[str, List[float]]]:
    """Sprite descriptors: every style, faction, body and animation cell."""
    samples = []
    for name in style_names:
        style = styles.STYLES_BY_NAME[name]
        for archetype in characters.ARCHETYPE_ORDER:
            for colour in characters.FACTION_COLOURS:
                for shape in figures.FIGURE_ORDER:
                    for frame in style.frames:
                        points = sprite_sample(style, archetype, colour.name,
                                               shape.name, frame)
                        samples.append((archetype, describe(points)))
    return samples


def validate() -> int:
    """Hold out each style, learn from the rest, read the one never seen."""
    every = [s.name for s in styles.STYLES if s.name in
             {p.parent.name for p in authored.ROSTER_DIRECTORY.glob("*/*.json")}]
    print(f"holding out one style at a time, learning from the other "
          f"{len(every) - 1}\n")
    hits = total = 0
    for held in every:
        train = gather([n for n in every if n != held])
        judge = Judge(train)
        style = styles.STYLES_BY_NAME[held]
        good = count = 0
        for archetype in characters.ARCHETYPE_ORDER:
            for shape in figures.FIGURE_ORDER:
                points = sprite_sample(style, archetype,
                                       characters.FACTION_COLOURS[0].name,
                                       shape.name, "")
                best, _ = judge.read(describe(points), archetype)
                good += (best == archetype)
                count += 1
        hits += good
        total += count
        print(f"  {held:10} {good}/{count} sprites read correctly")
    print(f"\noverall {hits}/{total} ({100 * hits / total:.0f}%), chance 12%")
    return 0 if hits > total * 0.5 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--validate", action="store_true")
    arguments = parser.parse_args()
    if arguments.validate:
        return validate()

    covered = sorted({p.parent.name
                      for p in authored.ROSTER_DIRECTORY.glob("*/*.json")})
    judge = Judge(gather(covered))
    hits = total = 0
    for name in covered:
        style = styles.STYLES_BY_NAME[name]
        cells = rc.converted_for(style)
        for archetype in characters.ARCHETYPE_ORDER:
            path = authored.ROSTER_DIRECTORY / name / f"{archetype}.first.json"
            document = json.loads(path.read_text())
            parts = [Part(p["x0"], p["x1"], p["y0"], p["y1"], p["z0"], p["z1"],
                          p["ramp"], p["rung"], str(p.get("name", "")))
                     for p in document["parts"]]
            points = mesh_points(parts, style, archetype, cells)
            best, margin = judge.read(describe(points), archetype)
            hits += (best == archetype)
            total += 1
            if best != archetype:
                print(f"  {name}/{archetype:12} reads as {best:12} "
                      f"(margin {margin:+.3f})")
    print(f"\n{hits}/{total} meshes read as their own class "
          f"({100 * hits / total:.0f}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
