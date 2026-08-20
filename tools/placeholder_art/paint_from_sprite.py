# SPDX-License-Identifier: MIT
"""Paint every part the colour its own sprite has in that place.

    python3 paint_from_sprite.py [--check]

Rule 2 says a face names a ramp and a rung and never a colour, and says nothing
about *which*. Rule 4 holds a mesh's outline to its sprite's, measured; nothing
held its insides to anything, and it showed. Measured over the roster, a
figure's interior identifies its archetype barely better than chance -- eight of
fifty-six against a floor of seven -- because the outline is the only part of a
solid that was ever aimed at the drawing it replaces.

So the ramp and the rung are taken the same way the width is: from the sprite.
Each part is drawn on its own into an ownership buffer at the shipped camera, so
what is known for each part is exactly the pixels a viewer sees of it; those
pixels are mapped into the sprite's own figure box, and the part takes the ramp
and rung the sprite spends there. A part nothing can see keeps what it had.

Two things this deliberately does not do:

* **It does not move a box.** The geometry is rules 1, 3 and 4's and is settled;
  this is only rule 2's half of the drawing.
* **It does not invent a value.** A rung is chosen among the four the
  archetype's own CLUT already resolves to, so a repainted figure spends no
  colour its sprite does not.

The value is matched **after the light, not before it.** A first version
compared the sprite's texel against the ramp's own colour and every figure came
out too dark, because the renderer multiplies a face by its normal's light
before drawing it -- a front face at 178/256, a top at 255. What has to look
like the sprite is what lands on screen, so the rung is chosen against
``shade(rung, light of the face this pixel belongs to)``. Which face a pixel
came from is recorded beside which part, for exactly this.

``--check`` repaints nothing and reports what would move.
"""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import roster_comparison as rc
from placeholder_art import (characters, figures, meshes, playstation_header,
                             preview3d, profiles, styles)
from placeholder_art.meshes import Part

HERE = Path(__file__).resolve().parent
ROSTER = HERE / "units"


def ownership(parts: Sequence[Part]) -> Dict[Tuple[int, int], int]:
    """Which part, and which of its faces, a viewer sees at each pixel.

    A transcription of :func:`preview3d.render`'s own body, painting a part's
    index where that function paints its colour. It has to be the same walk in
    the same order or the answer would be about a different picture: the parts
    arrive far-to-near, later ones cover earlier ones, and the canvas offset is
    computed over the figure and its tile together exactly as there.
    """
    normals = list(preview3d.LIGHT_BY_NORMAL)
    drawn: List[Tuple[List[Tuple[int, int]], Tuple[int, int, int]]] = []
    for index, part in enumerate(parts):
        for face in rc.faces_from([part]):
            quad = [preview3d.project(*corner) for corner in face.corners]
            if preview3d.faces_the_viewer(quad):
                drawn.append((quad, (index, normals.index(face.normal), 0)))
    if not drawn:
        return {}

    half = meshes.UNIT_WORLD // 2
    tile = [preview3d.project(-half, 0, -half), preview3d.project(-half, 0, half),
            preview3d.project(half, 0, half), preview3d.project(half, 0, -half)]
    points = [point for quad, _ in drawn for point in quad] + tile
    left = min(p[0] for p in points); right = max(p[0] for p in points)
    top = min(p[1] for p in points); bottom = max(p[1] for p in points)
    width, height = preview3d.PREVIEW_WIDTH, preview3d.PREVIEW_HEIGHT
    offset_x = (width - (right - left + 1)) // 2 - left
    offset_y = (height - (bottom - top + 1)) // 2 - top

    buffer: List[Optional[Tuple[int, int, int]]] = [None] * (width * height)
    for quad, tag in drawn:
        preview3d._fill(buffer, width, height,
                        [(x + offset_x, y + offset_y) for x, y in quad], tag)
    return {(x, y): (buffer[y * width + x][0], buffer[y * width + x][1])
            for y in range(height) for x in range(width)
            if buffer[y * width + x] is not None}


def sprite_palette(style: styles.Style, archetype: str, figure: str):
    """The unit's own cell, and the two ramps its CLUT resolves to."""
    named = "" if figure == figures.DEFAULT_FIGURE.name else figure
    colour = characters.FACTION_COLOURS[rc.FACTION].name
    cell = profiles.convert(styles.sprite(style, archetype, colour, figure=named),
                            rc.PROFILE, is_sprite=True)
    cells = rc.converted_for(style)
    ramps = playstation_header.mesh_ramp_words(cells, style, archetype, rc.FACTION)
    words = [playstation_header.clut_word(entry) for entry in cell.colours]
    faction = set(ramps[meshes.RAMP_FACTION]) - {0}
    return cell, ramps, words, faction


def wanted(cell, words, faction, x: int, y: int) -> Optional[Tuple[bool, float]]:
    """What the sprite spends at one texel: faction-bearing, and how light."""
    if not (0 <= x < cell.width and 0 <= y < characters.GROUND_Y):
        return None
    index = cell.indices[y * cell.width + x]
    if index in set(cell.transparent):
        return None
    word = words[index]
    if word == 0:
        return None
    return word in faction, playstation_header.luminance_of(word)


def lit(ramps, ramp: int, rung: int, normal: int) -> Optional[float]:
    """How light one rung lands on screen on a face with this normal.

    The measured thing, rather than the authored one: a rung is a colour in a
    CLUT and what a viewer sees is that colour through the key light, so this
    is what a sprite texel has to be compared against.
    """
    word = ramps[ramp][rung]
    if not word:
        return None
    light = list(preview3d.LIGHT_BY_NORMAL.values())[normal]
    shaded = preview3d.shade(
        preview3d.ramp_colours([[word] * meshes.RUNG_COUNT] * meshes.RAMP_COUNT,
                               playstation_header.clut_channels)[0][0], light)
    return 0.299 * shaded[0] + 0.587 * shaded[1] + 0.114 * shaded[2]


def repaint(parts: List[Part], style: styles.Style, archetype: str,
            figure: str) -> Tuple[List[Part], int]:
    """Every visible part given the ramp and rung its sprite spends there."""
    seen = ownership(parts)
    if not seen:
        return parts, 0
    cell, ramps, words, faction = sprite_palette(style, archetype, figure)

    xs = [x for x, _ in seen]; ys = [y for _, y in seen]
    x0, x1 = min(xs), max(xs); y0, y1 = min(ys), max(ys)
    rows = [y for y in range(characters.GROUND_Y)
            if any(cell.indices[y * cell.width + x] not in set(cell.transparent)
                   for x in range(cell.width))]
    columns = [x for x in range(cell.width)
               if any(cell.indices[y * cell.width + x] not in set(cell.transparent)
                      for y in range(characters.GROUND_Y))]
    if not rows or not columns:
        return parts, 0
    sy0, sy1 = min(rows), max(rows)
    sx0, sx1 = min(columns), max(columns)

    # Two questions per part, answered separately because they are separate
    # questions. *Which ramp* is a majority of what the sprite spends there --
    # faction colour or the archetype's own -- and cannot be traded against
    # value. *Which rung* is then whichever of that ramp's four lands closest
    # to the sprite once the light has been applied, summed over every pixel of
    # the part rather than voted per pixel, so one bright corner cannot outvote
    # a whole flank.
    ramp_votes: Dict[int, Counter] = {}
    error: Dict[int, Dict[Tuple[int, int], float]] = {}
    for (x, y), (index, normal) in seen.items():
        tx = sx0 + round((x - x0) * (sx1 - sx0) / max(1, x1 - x0))
        ty = sy0 + round((y - y0) * (sy1 - sy0) / max(1, y1 - y0))
        got = wanted(cell, words, faction, tx, ty)
        if got is None:
            continue
        bearing, want = got
        ramp_votes.setdefault(index, Counter())[
            meshes.RAMP_FACTION if bearing else meshes.RAMP_NEUTRAL] += 1
        table = error.setdefault(index, {})
        for ramp in range(meshes.RAMP_COUNT):
            for rung in range(meshes.RUNG_COUNT):
                value = lit(ramps, ramp, rung, normal)
                if value is None:
                    continue
                table[(ramp, rung)] = table.get((ramp, rung), 0.0) + abs(value - want)

    votes: Dict[int, Tuple[int, int]] = {}
    for index, counter in ramp_votes.items():
        ramp = counter.most_common(1)[0][0]
        table = error.get(index, {})
        rungs = [(cost, rung) for (r, rung), cost in table.items() if r == ramp]
        if not rungs:
            continue
        votes[index] = (ramp, min(rungs)[1])

    out = list(parts)
    moved = 0
    for index, (ramp, rung) in votes.items():
        part = out[index]
        if (part.ramp, part.rung) != (ramp, rung):
            out[index] = Part(part.x0, part.x1, part.y0, part.y1, part.z0,
                              part.z1, ramp, rung, part.name)
            moved += 1

    # Rule 2 asks that some part wear the faction ramp. If the sprite spends
    # none where this figure's parts land, the part with the most faction votes
    # takes it rather than the rule being met by an arbitrary choice.
    if not any(part.ramp == meshes.RAMP_FACTION for part in out):
        best = max(ramp_votes,
                   key=lambda i: ramp_votes[i][meshes.RAMP_FACTION],
                   default=None)
        if best is not None:
            part = out[best]
            rungs = [(cost, rung) for (r, rung), cost in error[best].items()
                     if r == meshes.RAMP_FACTION]
            rung = min(rungs)[1] if rungs else part.rung
            out[best] = Part(part.x0, part.x1, part.y0, part.y1, part.z0,
                             part.z1, meshes.RAMP_FACTION, rung, part.name)
            moved += 1
    return out, moved


def main(argv: List[str]) -> int:
    check = "--check" in argv
    touched = repainted = 0
    for path in sorted(ROSTER.glob("*/*.json")):
        document = json.loads(path.read_text())
        style = styles.STYLES_BY_NAME[document["style"]]
        parts = [Part(e["x0"], e["x1"], e["y0"], e["y1"], e["z0"], e["z1"],
                      e["ramp"], e["rung"], str(e.get("name", "")))
                 for e in document["parts"]]
        painted, moved = repaint(parts, style, document["archetype"],
                                 document["figure"])
        if not moved:
            continue
        touched += 1
        repainted += moved
        if check:
            continue
        for entry, part in zip(document["parts"], painted):
            entry["ramp"], entry["rung"] = part.ramp, part.rung
        path.write_text(json.dumps(document, indent=2) + "\n")
    verb = "would repaint" if check else "repainted"
    print(f"{verb} {repainted} parts across {touched} of 112 units")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
