#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Give each figure its sprite's *profile*, not just its width.

    tools/placeholder_art/fit_silhouette.py [--dry-run] [style ...]

The mesh rules hold a figure to its sprite's silhouette by **one number**: total
width, within a tolerance. That is enough to stop a figure being the wrong size
and does nothing at all about its shape, and the shape is what says which class
it is. The medieval mage is the plain case -- its sprite is a cone: a pointed hat
over a robe that widens to the hem. The commissioned mesh keeps that cone and
reads as a mage. The authored roster has a box for a head and a box for a body,
is exactly the right width, and reads as somebody else.

That is measurable. Rescaling every silhouette to a common box and asking which
sprite it overlaps best, the commissioned meshes name their own class 33 times in
56 and the authored roster 13 -- against 7 for chance. The roster is closer to
guessing than to the art it replaced.

So this fits each part's width to the sprite's width *at that part's height*.
Each part keeps its position, its depth, its ramp and its rung; only its x extent
moves, and only toward the profile its own sprite already has. Nothing is
invented: the target comes from the sprite the unit is answerable to.

The total-width rule still applies afterwards, and `normalise_units.py` will
re-seat anything this pushes outside it.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art import characters, profiles, styles
from placeholder_art.meshes import authored, rules

PROFILE = profiles.PROFILES_BY_NAME["n64_ci4"]

#: How far a part is allowed to move toward the sprite's profile, 0 to 1. Not
#: all the way: a box cannot be a cone, and a part driven exactly to the sprite's
#: width at its centre height ends up narrower at the top than the silhouette it
#: is meant to fill. Two thirds keeps the taper and keeps the mass.
PULL = 0.66

#: Parts narrower than this are left alone whatever the profile says. A face
#: band, a belt buckle or a blade is a detail sitting on the figure rather than
#: part of its outline, and scaling it to the body's width would swallow it.
DETAIL_WIDTH = 8


def sprite_profile(style: styles.Style, archetype: str,
                   figure: str) -> Optional[List[Tuple[int, int]]]:
    """Half-width of the sprite at each row, bottom to top, in texels."""
    named = "" if figure == "first" else figure
    colour = characters.FACTION_COLOURS[0].name
    canvas = styles.sprite(style, archetype, colour, figure=named)
    cell = profiles.convert(canvas, PROFILE, is_sprite=True)
    transparent = set(cell.transparent)

    # Only the figure, never the disc it stands on.
    #
    # A sprite cell carries a faction disc under the feet -- four rows of it,
    # the same for every class -- and reading the profile from the whole cell
    # maps the figure's feet to the bottom of that disc instead of to the ground
    # line. Everything above is then shifted by an eighth of the figure's height,
    # which is enough to fit a hat's width to a shoulder.
    rows: List[Tuple[int, int]] = []
    for y in range(min(cell.height, characters.GROUND_Y + 1)):
        xs = [x for x in range(cell.width)
              if cell.indices[y * cell.width + x] not in transparent]
        rows.append((min(xs), max(xs)) if xs else (0, -1))

    drawn = [y for y, (lo, hi) in enumerate(rows) if hi >= lo]
    if not drawn:
        return None
    # Rows are top-down in the cell and a figure is built bottom-up, so this is
    # returned in the figure's own direction.
    return [rows[y] for y in reversed(range(min(drawn), max(drawn) + 1))]


def fit(parts: List[dict], profile: List[Tuple[int, int]]) -> bool:
    """Pull each part's width toward the sprite's width at its own height."""
    low = min(part["y0"] for part in parts)
    high = max(part["y1"] for part in parts)
    span = max(1, high - low)

    widest = max((hi - lo + 1) for lo, hi in profile if hi >= lo)
    body = max((part["x1"] - part["x0"]) for part in parts)
    if widest <= 0 or body <= 0:
        return False

    moved = False
    for part in parts:
        width = part["x1"] - part["x0"]
        if width <= DETAIL_WIDTH:
            continue
        middle = (part["y0"] + part["y1"]) / 2.0
        row = int((middle - low) / span * (len(profile) - 1))
        row = max(0, min(len(profile) - 1, row))
        lo, hi = profile[row]
        if hi < lo:
            continue

        # The sprite's width at this height, as a share of its widest row, then
        # applied to the figure's own widest part. Shares rather than texels
        # because the two live at different scales.
        want = body * ((hi - lo + 1) / widest)
        target = width + (want - width) * PULL
        centre = (part["x0"] + part["x1"]) / 2.0
        half = max(1, int(round(target / 2.0)))
        x0, x1 = int(round(centre)) - half, int(round(centre)) + half
        if (x0, x1) != (part["x0"], part["x1"]):
            part["x0"], part["x1"] = x0, x1
            moved = True
    return moved


def main() -> int:
    dry_run = "--dry-run" in sys.argv
    wanted = [a for a in sys.argv[1:] if not a.startswith("--")]

    touched = 0
    total = 0
    for path in sorted(authored.ROSTER_DIRECTORY.glob("*/*.json")):
        document = json.loads(path.read_text())
        if wanted and document["style"] not in wanted:
            continue
        total += 1
        style = styles.STYLES_BY_NAME[document["style"]]
        profile = sprite_profile(style, document["archetype"], document["figure"])
        if profile is None:
            continue
        if fit(document["parts"], profile):
            touched += 1
            if not dry_run:
                path.write_text(json.dumps(document, indent=2) + "\n")

    verb = "would be" if dry_run else "were"
    print(f"{touched} of {total} units {verb} fitted to their sprite's profile")
    if not dry_run and touched:
        print("Run normalise_units.py, then check_units.py.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
