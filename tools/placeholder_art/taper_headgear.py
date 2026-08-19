#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Make a tapering hat taper, instead of jumping from a box to a spike.

    tools/placeholder_art/taper_headgear.py [--dry-run] [style ...]

The medieval mage is the case that showed this. Its sprite is a cone -- a pointed
hat over a robe widening to the hem -- and the cone is the whole of what says
"mage" at 25 pixels. The commissioned mesh keeps it in four steps, 30 units wide
at the brim then 20, then 12, then 4 at the point. The authored roster has three,
28 then 22 then 6: the crown is nearly as wide as the brim and then the outline
leaps to a spike, so it reads as a box wearing a needle.

Fitting widths to the sprite's profile (`fit_silhouette.py`) cannot repair that,
because the step that would carry the taper does not exist. This adds it: where
one headgear part sits above another and is far narrower, a box of intermediate
width is inserted across the join.

Only the headgear stack is considered -- parts slotted `head` or `crest` above
the head proper -- and only one step is inserted per figure, because two would
spend twenty-four triangles on an outline that is about ten pixels tall.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art import characters, profiles, styles
from placeholder_art.meshes import authored, rules

PROFILE = profiles.PROFILES_BY_NAME["n64_ci4"]

#: How much narrower an upper part has to be before the join reads as a jump
#: rather than a taper. The commissioned mage steps 30-20-12-4, so each step is
#: about two thirds of the one below; anything under half is a cliff.
CLIFF = 0.5

#: How tall the inserted step is, as a share of the gap it spans. The step wants
#: to overlap both neighbours so the outline has no seam in it.
REACH = 1.35

#: How much wider than the narrow part the *sprite* has to be at the join before
#: a step is worth adding. This is the whole of what tells a cone from a plume,
#: and geometry alone cannot: a mage's hat and a knight's crest both narrow by
#: about four to one, so a rule reading only the boxes fired on 81 of 112
#: figures and thickened every plume in the roster. The sprite knows which is
#: which -- a hat is wide below its point and a plume is thin all the way down --
#: so the sprite is asked.
SPRITE_WIDER_BY = 1.6


def sprite_profile(style_name: str, archetype: str,
                   figure: str) -> Optional[List[int]]:
    """Sprite width at each row, bottom to top, in texels."""
    style = styles.STYLES_BY_NAME[style_name]
    named = "" if figure == "first" else figure
    colour = characters.FACTION_COLOURS[0].name
    canvas = styles.sprite(style, archetype, colour, figure=named)
    cell = profiles.convert(canvas, PROFILE, is_sprite=True)
    transparent = set(cell.transparent)
    # The figure only: the bottom rows of a cell are the faction disc, and
    # including them maps the feet below the ground line (see fit_silhouette).
    rows = []
    for y in range(min(cell.height, characters.GROUND_Y + 1)):
        xs = [x for x in range(cell.width)
              if cell.indices[y * cell.width + x] not in transparent]
        rows.append(max(xs) - min(xs) + 1 if xs else 0)
    drawn = [y for y, w in enumerate(rows) if w]
    if not drawn:
        return None
    return [rows[y] for y in reversed(range(min(drawn), max(drawn) + 1))]


def sprite_width_at(profile: List[int], low: int, high: int, y: float) -> float:
    """The sprite's width at a figure-space height, in texels."""
    span = max(1, high - low)
    row = int((y - low) / span * (len(profile) - 1))
    return profile[max(0, min(len(profile) - 1, row))]


def stack(parts: List[dict]) -> List[dict]:
    """Headgear, bottom to top: the head slots and any crest above them."""
    wearing = [part for part in parts
               if part.get("slot") in ("head", "crest")
               and part.get("name") != "face band"]
    return sorted(wearing, key=lambda part: (part["y0"] + part["y1"]) / 2.0)


def cliff_in(worn: List[dict]) -> Optional[Tuple[dict, dict]]:
    """The widest jump in the stack, if any is steep enough to be one."""
    steepest, found = CLIFF, None
    for below, above in zip(worn, worn[1:]):
        wide = below["x1"] - below["x0"]
        narrow = above["x1"] - above["x0"]
        if wide <= 0 or narrow <= 0:
            continue
        ratio = narrow / wide
        # Only a jump *upward and inward*, which is what a taper is. A part that
        # widens above another is a brim and wants leaving alone.
        if ratio < steepest and above["y1"] > below["y1"]:
            steepest, found = ratio, (below, above)
    return found


def step_at(above: dict, y: float, want: float) -> dict:
    """A box at the height the outline is thin, at the width the sprite has."""
    half = max(1, int(round(want / 2.0)))
    centre = int(round((above["x0"] + above["x1"]) / 2.0))
    span = max(5, int(round(abs(above["y1"] - above["y0"]) * 0.5)))
    y0 = int(round(y - span / 2.0))
    return {
        "name": "hat step",
        "slot": above.get("slot", "head"),
        "x0": centre - half, "x1": centre + half,
        "y0": y0, "y1": y0 + span,
        "z0": above["z0"], "z1": above["z1"],
        "ramp": above["ramp"], "rung": above["rung"],
    }


def main() -> int:
    dry_run = "--dry-run" in sys.argv
    wanted = [a for a in sys.argv[1:] if not a.startswith("--")]
    ceiling = rules.TRIANGLE_BAND[1]
    per_part = rules.TRIANGLES_PER_PART

    added, skipped = [], []
    for path in sorted(authored.ROSTER_DIRECTORY.glob("*/*.json")):
        document = json.loads(path.read_text())
        if wanted and document["style"] not in wanted:
            continue
        parts = document["parts"]
        where = f"{document['style']}/{document['archetype']}/{document['figure']}"

        worn = stack(parts)
        if len(worn) < 2:
            continue

        profile = sprite_profile(document["style"], document["archetype"],
                                 document["figure"])
        if profile is None:
            continue

        low = min(part["y0"] for part in parts)
        high = max(part["y1"] for part in parts)
        widest = max(profile)
        body = max(part["x1"] - part["x0"] for part in parts)

        # Where is the figure's outline thinner than its sprite's?
        #
        # Comparing at the join between two parts does not tell a cone from a
        # plume: both join at helmet height where the sprite is broad. What
        # separates them is what happens *above* the join. A mage's sprite is
        # still wide a third of the way up its hat and the boxes are not; a
        # knight's sprite is as thin as its plume all the way. So the model's
        # own width is scanned against the sprite's across the headgear, and a
        # step is added at the worst deficit if there is one worth adding.
        span_low = min(part["y0"] for part in worn)
        span_high = max(part["y1"] for part in worn)
        worst, at, want = 1.0, None, 0.0
        for step in range(1, 12):
            y = span_low + (span_high - span_low) * step / 12.0
            covering = [part for part in parts
                        if part["y0"] <= y <= part["y1"]]
            if not covering:
                continue
            model = max(part["x1"] - part["x0"] for part in covering)
            sprite = body * sprite_width_at(profile, low, high, y) / max(widest, 1)
            if sprite <= 0:
                continue
            ratio = model / sprite
            if ratio < worst:
                worst, at, want = ratio, y, sprite

        if at is None or worst > 1.0 / SPRITE_WIDER_BY:
            continue
        if (len(parts) + 1) * per_part > ceiling:
            skipped.append(f"{where}: {len(parts)} parts already")
            continue

        # The step is placed at the deficit, taking its depth and colour from
        # whichever headgear part is nearest above it.
        above = min((part for part in worn if part["y1"] >= at),
                    key=lambda part: part["y0"], default=worn[-1])
        added.append(f"{where}: outline {worst:.2f} of the sprite at y{at:.0f}")
        step = step_at(above, at, want)
        # A step is an addition to the outline, never a change to the figure's
        # height: `rules` requires the tallest part to sit exactly at the world
        # height, and a step placed near the crown can otherwise push past it.
        ceiling_y = max(part["y1"] for part in parts)
        if step["y1"] > ceiling_y:
            drop = step["y1"] - ceiling_y
            step["y0"] -= drop
            step["y1"] -= drop
        parts.append(step)
        if not dry_run:
            document["parts"] = parts
            path.write_text(json.dumps(document, indent=2) + "\n")

    verb = "would gain" if dry_run else "gained"
    print(f"{len(added)} figures {verb} a taper step")
    for line in added[:12]:
        print(f"  {line}")
    if len(added) > 12:
        print(f"  ... and {len(added) - 12} more")
    for line in skipped:
        print(f"  skipped {line}")
    if not dry_run and added:
        print("\nRun normalise_units.py, then check_units.py.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
