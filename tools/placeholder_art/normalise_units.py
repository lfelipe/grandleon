#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Put an authored unit table into the order and the proportions the rules want.

    tools/placeholder_art/normalise_units.py            # every unit, in place
    tools/placeholder_art/normalise_units.py --dry-run  # say what would change

The roster under ``units/`` is authored outside this repository against each
role's sprite, and an author — human or otherwise — writes a figure down the way
a figure is *described*: crest, head, chest, legs, the sword last because the
sword is the interesting part. That is not the order a console can draw it in,
and it is not the author's job to know that. This is the step between.

Two things are fixed, and both are fixes the rule that rejects them names:

**Draw order.** There is no depth buffer, so the array must be far-to-near at
the shipped pitch and the parts are sorted on :func:`~.meshes.rules.depth_key`.
Every one of the 112 units failed on this before it ran, which is the expected
outcome rather than a surprising one: nothing upstream was ordering by depth.

**Depth the machine can see.** Exact arithmetic is not what the console has. It
sums eight projected corner depths, each already truncated by a shift, so two
overlapping parts less than two units apart in ``depth_key`` can come back in
either order — and when they overlap, that shows. Overlapping pairs inside that
margin are pushed apart in z until they clear it, which is what
:mod:`~.meshes.rules` means by "buy the depth in the boxes".

**Width.** A figure is held to its own sprite's silhouette within a tolerance.
A unit outside it is scaled about its own centreline until it is inside, rather
than the tolerance being widened, which is the trade the rule explicitly
refuses.

What this does **not** do is move a part to make a figure look better. Every
change here is one a rule demanded, and running it on a roster that already
passes changes nothing.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path
from typing import Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art import (characters, figures, playstation_header,
                             profiles, styles)
from placeholder_art.meshes import authored, rules

#: The profile the silhouette is measured in — the console's, because that is
#: the sprite the hardware draws and the one a mesh is answerable to.
PROFILE = profiles.PROFILES_BY_NAME["n64_ci4"]

#: How far apart two overlapping parts must be in ``depth_key`` before the
#: console's truncated corner sum can be trusted to order them. Two, from
#: :mod:`~.meshes.rules`; named here so the reason travels with the number.
MACHINE_MARGIN = 2.0

#: How many times to re-sort and re-separate before giving up. Pushing one part
#: back can bring it inside another's margin, so this settles rather than fixes
#: in one pass; it converges in two or three on every unit here, and the cap is
#: a runaway guard rather than a tuning knob.
SETTLE_PASSES = 24


def _part(entry: Dict[str, object]) -> rules.Part:
    return rules.Part(entry["x0"], entry["x1"], entry["y0"], entry["y1"],
                      entry["z0"], entry["z1"], entry["ramp"], entry["rung"],
                      str(entry.get("name", "")))


def target_width_for(style_name: str, archetype: str, figure: str) -> int:
    """The world width this unit's own sprite silhouette asks for."""
    style = styles.STYLES_BY_NAME[style_name]
    named = "" if figure == figures.DEFAULT_FIGURE.name else figure
    colour = characters.FACTION_COLOURS[0].name
    canvas = styles.sprite(style, archetype, colour, figure=named)
    converted = profiles.convert(canvas, PROFILE, is_sprite=True)
    return rules.target_width(playstation_header.silhouette_of(converted))


def reproportion(parts: List[Dict[str, object]], want: int) -> bool:
    """Scale x about the centreline until the figure is the width asked for."""
    have = rules.authored_width([_part(entry) for entry in parts])
    if have == want:
        return False
    low = min(entry["x0"] for entry in parts)
    high = max(entry["x1"] for entry in parts)
    centre = (low + high) / 2.0
    scale = want / have
    for entry in parts:
        entry["x0"] = int(round(centre + (entry["x0"] - centre) * scale))
        entry["x1"] = int(round(centre + (entry["x1"] - centre) * scale))
        # Rounding two edges independently can collapse a one-unit box. A part
        # with no width is not a thinner part, it is a part that vanishes.
        if entry["x1"] <= entry["x0"]:
            entry["x1"] = entry["x0"] + 1
    return True


def settle(parts: List[Dict[str, object]]) -> List[str]:
    """Sort far-to-near, then separate only the pairs the rule would reject.

    The condition mirrors :func:`~.meshes.rules.check_commission` exactly —
    **adjacent** parts whose machine order margin is negative *and* which are
    drawn over one another — and deliberately no more than that. An earlier
    version separated every overlapping pair to a flat two units of
    ``depth_key``, which is stricter than the rule, and it moved geometry in 72
    of 112 units that the rules already accepted. Being stricter than the rule
    here is not caution: it is editing art nobody asked to change, and it made
    this script unable to leave a settled roster alone.
    """
    notes: List[str] = []
    order = [dict(entry) for entry in parts]
    parts.sort(key=lambda entry: rules.depth_key(_part(entry)), reverse=True)
    if parts != order:
        notes.append("re-ordered far-to-near")

    for _ in range(SETTLE_PASSES):
        moved = False
        for index in range(len(parts) - 1):
            before, after = _part(parts[index]), _part(parts[index + 1])
            if rules.machine_order_margin(before, after) >= 0:
                continue
            if not rules.drawn_over_one_another(before, after):
                continue
            gap = rules.depth_key(before) - rules.depth_key(after)
            push = max(1, int(math.ceil(MACHINE_MARGIN - gap)))
            parts[index + 1]["z0"] -= push
            parts[index + 1]["z1"] -= push
            notes.append(f"pushed {after.name!r} {push} units nearer, "
                         f"behind {before.name!r}")
            moved = True
        if not moved:
            break
        parts.sort(key=lambda entry: rules.depth_key(_part(entry)), reverse=True)
    return notes


def main() -> int:
    dry_run = "--dry-run" in sys.argv
    wanted = [argument for argument in sys.argv[1:]
              if not argument.startswith("--")]

    root = authored.ROSTER_DIRECTORY
    if not root.is_dir():
        print(f"no authored roster under {root}")
        return 1

    touched: List[str] = []
    total = 0
    for path in sorted(root.glob("*/*.json")):
        document = json.loads(path.read_text())
        style_name = document["style"]
        if wanted and style_name not in wanted:
            continue
        total += 1
        archetype, figure = document["archetype"], document["figure"]
        parts = document["parts"]
        where = f"{style_name}/{archetype}/{figure}"

        notes: List[str] = []
        want = target_width_for(style_name, archetype, figure)
        have = rules.authored_width([_part(entry) for entry in parts])
        if abs(have - want) > rules.WIDTH_TOLERANCE:
            if reproportion(parts, want):
                now = rules.authored_width([_part(entry) for entry in parts])
                notes.append(f"width {have} -> {now}, asked {want}")
        notes.extend(settle(parts))

        if notes:
            touched.append(f"  {where}: {'; '.join(notes)}")
            if not dry_run:
                document["parts"] = parts
                path.write_text(json.dumps(document, indent=2) + "\n")

    verb = "would change" if dry_run else "changed"
    print(f"{len(touched)} of {total} units {verb}")
    for line in touched:
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
