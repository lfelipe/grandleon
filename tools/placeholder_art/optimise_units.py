#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Search each figure's boxes for a shape that reads as its own class.

*** THIS TOOL WORKS AND ITS OUTPUT IS STILL NOT SHIPPABLE. READ THE WARNING. ***

Run across the roster it takes recognisability from 15 of 56 to **43 of 56**,
past the hand-authored meshes' 33, keeping every figure legal. It also destroys
the art. The figures come apart: limbs separate from torsos, the undead
stormcaller becomes a scatter of blocks, the beast disintegrates. Look at
`comparison/` after a run and it is obvious in a second.

The mechanism is measurable. Shared coordinate boundaries between parts fall
from 0.478 per part-pair to 0.262 -- a hand-placed shoulder starts exactly where
the torso ends, and a random nudge breaks that join. A broken join is a
one-pixel step in the outline, and a great many one-pixel steps overlap a
pixel-art sprite better than a clean edge does. The score rises as the figure
falls apart, because the score cannot tell the difference.

The held-out test does not catch it. Optimising against one sprite pose and
scoring against the other transfers about 86% of the gain -- which proves the
result is not memorising pixel positions, and says nothing at all about whether
it still looks like a person.

Both of those repairs were then made, and they work as designed:

* the proposal step now moves **joints** rather than faces -- every face sharing
  a plane travels together, so an edit deforms the figure the way a body
  deforms;
* the objective carries a **cohesion** penalty and a **depth** penalty.

Measured on medieval: joins now *rise* under optimisation, 0.518 to 0.829,
where the first version drove them to 0.262; depth holds at 9.7 to 10.6 rather
than collapsing; margin still gains +0.157 mean; all 16 stay legal. The figures
no longer come apart.

**They still degrade.** The medieval healer's clean white robe becomes a dark
striped mess with its staff cutting through it; the beast scatters; the archer's
prop grows until it dominates the figure. Better than disintegration and still
not art anybody would ship.

Two rewards had to be turned into penalties along the way, and the reason is the
same both times. Rewarding *extra* cohesion made the search collapse parts onto
shared planes -- maximal joins, no silhouette cost, and the figure flattened
(the healer went from 8.8 units of depth to 3.8). A silhouette has no gradient
on z whatsoever, so anything z-shaped is free unless it is paid for explicitly.
Both terms now only ever penalise falling below where a figure started.

The conclusion the tool has earned: **silhouette margin is not a sufficient
objective, and adding terms to it is patching symptoms.** Each repair fixed the
specific way the search was cheating and the search found another. What is
missing is a judge that can see what the metric cannot -- everything inside the
outline, and whether the thing still looks like a person. The research points at
a small CNN trained on synthetic renders for that, and explicitly away from
putting a vision-language model in the inner loop.

    tools/placeholder_art/optimise_units.py [--budget N] [--dry-run] [style ...]

The roster's problem was never that it lacked variety. Rescaling each silhouette
into a common box and asking which of the eight sprites it overlaps best, only a
quarter of the figures named their own class -- while differing from each other
*more* than the sprites do. They vary; the variation is not aimed at anything.

It turns out very little aiming is needed. A perfect figure would beat its
nearest rival by about 0.27 of overlap and the hand-authored meshes manage
0.005, so almost none of the available margin is being used, and most figures
are near-misses rather than wrecks. A few hundred random one- and two-integer
edits, keeping whatever improves the margin, recovers a large part of it in
about a second a figure.

Three things make that safe to do to art:

* **Every proposal is held to the game's own rules.** Not "the box still has
  positive volume", which is what a first pass at this checked, but
  `rules.check_commission`: feet on the ground, 128 units tall, inside the
  triangle band, within the sprite's width tolerance, wearing a faction ramp,
  ordered far-to-near, and separated enough for the console to order it. A
  proposal that breaks any of them is discarded rather than repaired.
* **Draw order is repaired, not rejected.** Re-sorting parts far-to-near cannot
  change a silhouette -- the mask is a union and unions do not care about order
  -- so every proposal is sorted before it is judged. That turns the one rule a
  random edit breaks constantly into a non-event.
* **Depth is watched.** A silhouette has *no* gradient on z at all: an
  axis-aligned figure's outline is identical whatever its parts' depth, so this
  objective cannot see z and an optimiser is free to flatten a figure toward
  cardboard without the score objecting. The machine-order rule stops the worst
  of it, and the depth extent is reported before and after so the rest is
  visible rather than silent.

The score is the **margin**: how much better a figure matches its own sprite
than the nearest of the other seven. Matching your own sprite is not enough when
every archetype shares a humanoid core; what has to improve is the gap.
"""

from __future__ import annotations

import argparse
import copy
import json
import random
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art import (characters, figures, playstation_header,
                             preview3d, profiles, styles)
from placeholder_art.meshes import Part, Silhouette, authored, rules
import roster_comparison as rc
import judge as perceptual

PROFILE = profiles.PROFILES_BY_NAME["n64_ci4"]

#: The box every silhouette is rescaled into before being compared, so only
#: shape is judged and not size or where a figure sits in its frame.
GRID = 24

#: What a lost join costs, against the margin it might buy.
#:
#: The first version of this scored margin alone and took the roster from 15 of
#: 56 to 43 while pulling the figures apart, because a broken join is a
#: one-pixel step and many small steps overlap a pixel sprite better than a
#: clean edge. Cohesion fell 45% while the score rose. This is set so that
#: losing 45% of a figure's joins costs about 0.22 -- comfortably more than the
#: 0.12 mean margin that run gained, so the trade is refused rather than made.
COHESION_WEIGHT = 0.5

#: What lost depth costs. A silhouette has no gradient on z at all, so the score
#: is free to flatten a figure to nothing without noticing -- and rewarding
#: cohesion made that worse, because collapsing parts onto shared z planes buys
#: joins for free. Depth is therefore defended explicitly rather than left to
#: the machine-order rule.
DEPTH_WEIGHT = 0.5

#: What the perceptual judge is worth against the silhouette margin.
#:
#: The judge (`judge.py`) sees inside the outline, which every other term here
#: is blind to. It is weighted equally with the silhouette rather than trusted
#: over it: it reads held-out *sprites* at 66% against 12% chance, but its
#: margins on *meshes* are a third the size, so it is a weaker signal on the
#: thing being optimised than on the thing it learned from.
JUDGE_WEIGHT = 1.0

#: Which coordinates an edit may touch, and by how much. Depth is included --
#: leaving it out would freeze the one axis the rules constrain and the score
#: does not -- but it is edited at the same rate as the rest, so nothing drives
#: it anywhere in particular.
FIELDS = ("x0", "x1", "y0", "y1", "z0", "z1")
NUDGE = (-2, -1, 1, 2)


def cohesion(parts: Sequence[Part]) -> float:
    """How much of the figure is still joined, per pair of parts.

    A hand-placed shoulder starts exactly where the torso ends. Counting faces
    that share a coordinate on the same axis measures how much of that is left:
    two faces at the same value are a join, and a figure that has come apart has
    almost none. Counted per axis through a tally rather than pair by pair,
    which is the same number and far cheaper inside a search loop.
    """
    pairs = max(len(parts) * (len(parts) - 1) / 2.0, 1.0)
    shared = 0
    for axis in ("x", "y", "z"):
        tally: Dict[int, int] = {}
        for part in parts:
            for end in ("0", "1"):
                value = getattr(part, f"{axis}{end}")
                tally[value] = tally.get(value, 0) + 1
        for count in tally.values():
            shared += count * (count - 1) // 2
    return shared / pairs


def rescale(points: Set[Tuple[int, int]]) -> Set[Tuple[int, int]]:
    if not points:
        return set()
    xs = [x for x, _ in points]
    ys = [y for _, y in points]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    width = max(1, x1 - x0 + 1)
    height = max(1, y1 - y0 + 1)
    return {(min(GRID - 1, (x - x0) * GRID // width),
             min(GRID - 1, (y - y0) * GRID // height)) for x, y in points}


def sprite_mask(style: styles.Style, archetype: str,
                figure: str) -> Set[Tuple[int, int]]:
    cell = rc.sprite_cell(style, archetype, figure)
    transparent = set(cell.transparent)
    return rescale({(x, y)
                    for y in range(cell.height) for x in range(cell.width)
                    if cell.indices[y * cell.width + x] not in transparent})


def silhouettes_for(style: styles.Style, figure: str) -> List[Silhouette]:
    named = "" if figure == figures.DEFAULT_FIGURE.name else figure
    colour = characters.FACTION_COLOURS[0].name
    measured = []
    for archetype in characters.ARCHETYPE_ORDER:
        canvas = styles.sprite(style, archetype, colour, figure=named)
        converted = profiles.convert(canvas, PROFILE, is_sprite=True)
        measured.append(playstation_header.silhouette_of(converted))
    return measured


class Judge:
    """Everything one unit is scored and judged against, computed once."""

    def __init__(self, style_name: str, archetype: str, figure: str,
                 critic: "perceptual.Judge" = None):
        self.critic = critic
        self.style = styles.STYLES_BY_NAME[style_name]
        self.style_name = style_name
        self.archetype = archetype
        self.figure = figure
        self.cells = rc.converted_for(self.style)
        self.sprites = {a: sprite_mask(self.style, a, figure)
                        for a in characters.ARCHETYPE_ORDER}
        self.measured = silhouettes_for(self.style, figure)
        self.ramps = preview3d.ramp_colours(
            playstation_header.mesh_ramp_words(self.cells, self.style,
                                               archetype, rc.FACTION),
            playstation_header.clut_channels)

    def mask(self, parts: Sequence[Part]) -> Set[Tuple[int, int]]:
        drawn = preview3d.render(rc.faces_from(list(parts)), self.ramps)
        return rescale({(x, y)
                        for y in range(drawn.height) for x in range(drawn.width)
                        if drawn.at(x, y) is not None
                        and drawn.at(x, y) != preview3d.TILE_COLOUR})

    def seen(self, parts: Sequence[Part]) -> float:
        """What the perceptual judge makes of it, or nothing if there is none."""
        if self.critic is None:
            return 0.0
        points = perceptual.mesh_points(parts, self.style, self.archetype,
                                        self.cells)
        _, margin = self.critic.read(perceptual.describe(points),
                                     self.archetype)
        return margin

    def margin(self, parts: Sequence[Part]) -> float:
        mask = self.mask(parts)
        if not mask:
            return -1.0
        def overlap(other: Set[Tuple[int, int]]) -> float:
            return len(mask & other) / max(len(mask | other), 1)
        own = overlap(self.sprites[self.archetype])
        rival = max(overlap(s) for a, s in self.sprites.items()
                    if a != self.archetype)
        return own - rival

    def legal(self, parts: Sequence[Part]) -> bool:
        try:
            rules.check_commission({self.archetype: tuple(parts)},
                                   self.measured, self.style_name)
        except AssertionError:
            return False
        return True


def ordered(parts: List[Part]) -> List[Part]:
    """Far-to-near, which a silhouette cannot tell apart but the console can."""
    return sorted(parts, key=rules.depth_key, reverse=True)


def depth_extent(parts: Sequence[Part]) -> float:
    return sum(p.z1 - p.z0 for p in parts) / max(len(parts), 1)


def propose(parts: List[Part], rng: random.Random) -> List[Part]:
    """An edit that moves a joint, not a face.

    The move that broke the figures was "pick one coordinate of one box and
    nudge it", which pulls a face away from whatever it was meeting. These move
    every face that sits on the same plane together, so a shoulder and the torso
    it meets travel as one and the join survives the edit:

    * a **slice**: every face at one coordinate value on one axis, moved
      together. This is the articulated move -- it deforms the figure the way a
      body deforms and cannot break a join at all, because every face that
      shared that plane is still sharing it.
    * a **part slide**: one whole box translated. It keeps that box's own shape
      but does break its joins with neighbours, so it is proposed less often and
      the cohesion term decides whether it was worth it.
    """
    candidate = list(parts)
    if rng.random() < 0.75:
        axis = rng.choice(("x", "y", "z"))
        values = sorted({getattr(p, f"{axis}{e}")
                         for p in candidate for e in ("0", "1")})
        if not values:
            return candidate
        plane = rng.choice(values)
        step = rng.choice(NUDGE)
        for index, part in enumerate(candidate):
            fields = {}
            for end in ("0", "1"):
                field = f"{axis}{end}"
                if getattr(part, field) == plane:
                    fields[field] = plane + step
            if fields:
                candidate[index] = part.__replace__(**fields)
        return candidate

    index = rng.randrange(len(candidate))
    part = candidate[index]
    axis = rng.choice(("x", "y", "z"))
    step = rng.choice(NUDGE)
    candidate[index] = part.__replace__(**{
        f"{axis}0": getattr(part, f"{axis}0") + step,
        f"{axis}1": getattr(part, f"{axis}1") + step,
    })
    return candidate


def optimise(parts: List[Part], judge: Judge, budget: int,
             rng: random.Random) -> Tuple[List[Part], float, float, int]:
    current = ordered(list(parts))
    start_cohesion = max(cohesion(current), 1e-6)

    start_depth = max(depth_extent(current), 1e-6)

    def value_of(candidate: Sequence[Part]) -> float:
        # Both terms are penalties only. Rewarding *extra* cohesion invited the
        # search to collapse parts onto shared planes -- which maximises joins,
        # costs nothing in silhouette, and flattens the figure. A figure is
        # asked to keep what it had, not to exceed it.
        held = min(0.0, cohesion(candidate) / start_cohesion - 1.0)
        deep = min(0.0, depth_extent(candidate) / start_depth - 1.0)
        return (judge.margin(candidate)
                + JUDGE_WEIGHT * judge.seen(candidate)
                + COHESION_WEIGHT * held
                + DEPTH_WEIGHT * deep)

    score = value_of(current)
    best, best_score = list(current), score
    accepted = 0

    for _ in range(budget):
        candidate = ordered(propose(current, rng))
        if any(p.x0 >= p.x1 or p.y0 >= p.y1 or p.z0 >= p.z1 for p in candidate):
            continue
        if not judge.legal(candidate):
            continue
        value = value_of(candidate)
        # Equal scores are accepted so the search can cross the flat ground a
        # quantised pixel objective is mostly made of; only strict improvements
        # are remembered as best.
        if value >= score:
            current, score = candidate, value
            accepted += 1
            if value > best_score:
                best, best_score = list(candidate), value
    return best, judge.margin(parts), judge.margin(best), accepted


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--budget", type=int, default=600)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--judge", action="store_true",
                        help="add the perceptual judge to the objective")
    parser.add_argument("styles", nargs="*", default=[])
    arguments = parser.parse_args()

    critic = None
    if arguments.judge:
        covered = sorted({p.parent.name
                          for p in authored.ROSTER_DIRECTORY.glob("*/*.json")})
        print("learning the perceptual judge from the sprites...", flush=True)
        critic = perceptual.Judge(perceptual.gather(covered))

    paths = sorted(authored.ROSTER_DIRECTORY.glob("*/*.json"))
    started = time.time()
    improved = flat = 0
    gained: List[float] = []
    flattened: List[str] = []

    for path in paths:
        document = json.loads(path.read_text())
        if arguments.styles and document["style"] not in arguments.styles:
            continue
        parts = [Part(p["x0"], p["x1"], p["y0"], p["y1"], p["z0"], p["z1"],
                      p["ramp"], p["rung"], str(p.get("name", "")))
                 for p in document["parts"]]
        by_name = {(p.x0, p.x1, p.y0, p.y1, p.z0, p.z1): p.name for p in parts}

        judge = Judge(document["style"], document["archetype"],
                      document["figure"], critic)
        rng = random.Random(arguments.seed)
        best, before, after, accepted = optimise(parts, judge,
                                                 arguments.budget, rng)
        where = f"{document['style']}/{document['archetype']}/{document['figure']}"
        deep_before, deep_after = depth_extent(parts), depth_extent(best)
        thinner = deep_after < deep_before * 0.85
        if thinner:
            flattened.append(f"{where}: depth {deep_before:.1f} -> {deep_after:.1f}")

        if after > before + 1e-9:
            improved += 1
            gained.append(after - before)
            if not arguments.dry_run:
                document["parts"] = [
                    {"name": by_name.get((p.x0, p.x1, p.y0, p.y1, p.z0, p.z1),
                                         p.name),
                     "slot": next((q.get("slot", "") for q in document["parts"]
                                   if q.get("name", "") == p.name), ""),
                     "x0": p.x0, "x1": p.x1, "y0": p.y0, "y1": p.y1,
                     "z0": p.z0, "z1": p.z1, "ramp": p.ramp, "rung": p.rung}
                    for p in best]
                path.write_text(json.dumps(document, indent=2) + "\n")
        else:
            flat += 1
        print(f"  {where:34} {before:+.3f} -> {after:+.3f} "
              f"({accepted:3} accepted)", flush=True)

    total = improved + flat
    print(f"\n{improved} of {total} figures improved, {flat} unchanged, "
          f"{time.time() - started:.0f}s")
    if gained:
        gained.sort()
        print(f"margin gained: mean {sum(gained)/len(gained):+.3f}, "
              f"median {gained[len(gained)//2]:+.3f}, max {gained[-1]:+.3f}")
    if flattened:
        print(f"\n{len(flattened)} figures lost depth -- the score cannot see z, "
              f"so check these by eye:")
        for line in flattened[:10]:
            print(f"  {line}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
