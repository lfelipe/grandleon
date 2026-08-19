#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Push a held object out until something of it is visible.

    tools/placeholder_art/clear_props.py [--dry-run] [style ...]

A weapon is read at this size by its **silhouette against the background**, not
by its surface: at 22 to 34 pixels a blade is two pixels wide and the only thing
telling a viewer it is there is that it breaks the body's outline. Measured
across four styles, most props already do that -- they add 7 to 35 percent of a
figure's drawn pixels outside the body -- so this is not a sweeping change.

Two units were carrying props that break nothing at all. `sengoku/commander`'s
sashimono pole sits behind the torso, inside its width and no taller than its
helmet, and contributes **zero** pixels: a banner nobody can see. `nature/rogue`
carries two daggers inside the animal's own wide body and contributes twenty.

So this finds props entirely inside the outline and slides them outward, to
whichever side they are already nearer, until they clear it. Only x moves; the
height, the depth, the ramp and the rung are left alone. A move that would push
the figure past the width its sprite is held to is refused rather than taken --
that rule is the one thing keeping a figure the size of the unit it represents.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import List

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art import characters, playstation_header, profiles, styles
from placeholder_art.meshes import authored, rules

PROFILE = profiles.PROFILES_BY_NAME["n64_ci4"]

#: How far past the body's edge a prop has to reach before it counts as visible.
#: Three world units is about two pixels at the size a figure is drawn, which is
#: the narrowest thing that still reads as a line rather than as noise.
CLEAR_BY = 3


def sprite_width(style_name: str, archetype: str, figure: str) -> int:
    style = styles.STYLES_BY_NAME[style_name]
    named = "" if figure == "first" else figure
    colour = characters.FACTION_COLOURS[0].name
    canvas = styles.sprite(style, archetype, colour, figure=named)
    converted = profiles.convert(canvas, PROFILE, is_sprite=True)
    return rules.target_width(playstation_header.silhouette_of(converted))


def as_parts(entries: List[dict]) -> List[rules.Part]:
    return [rules.Part(e["x0"], e["x1"], e["y0"], e["y1"], e["z0"], e["z1"],
                       e["ramp"], e["rung"], str(e.get("name", "")))
            for e in entries]


def main() -> int:
    dry_run = "--dry-run" in sys.argv
    wanted = [a for a in sys.argv[1:] if not a.startswith("--")]

    moved: List[str] = []
    refused: List[str] = []
    for path in sorted(authored.ROSTER_DIRECTORY.glob("*/*.json")):
        document = json.loads(path.read_text())
        if wanted and document["style"] not in wanted:
            continue
        entries = document["parts"]
        where = f"{document['style']}/{document['archetype']}/{document['figure']}"

        body = [e for e in entries
                if not str(e.get("slot", "")).startswith("prop")]
        props = [e for e in entries
                 if str(e.get("slot", "")).startswith("prop")]
        if not body or not props:
            continue

        low = min(e["x0"] for e in body)
        high = max(e["x1"] for e in body)
        top = max(e["y1"] for e in body)
        allowed = sprite_width(document["style"], document["archetype"],
                               document["figure"]) + rules.WIDTH_TOLERANCE

        def hidden(prop: dict) -> bool:
            return (prop["x0"] >= low and prop["x1"] <= high
                    and prop["y1"] <= top)

        # Only units whose props are *all* hidden.
        #
        # The first version moved any prop that sat inside the outline, which
        # was 37 of 112 units -- a third of the roster, on a signal that turned
        # out ambiguous: silhouettes matched their own sprite slightly worse
        # afterwards and their rivals slightly less well. A figure that already
        # has one weapon breaking its outline is read; a second one tucked
        # inside is not a defect worth moving art for. What is a defect is a
        # unit carrying nothing visible at all.
        if not all(hidden(prop) for prop in props):
            continue

        changed = False
        for prop in props:
            centre = (prop["x0"] + prop["x1"]) / 2.0
            body_centre = (low + high) / 2.0
            if centre <= body_centre:
                shift = (low - CLEAR_BY) - prop["x1"]
            else:
                shift = (high + CLEAR_BY) - prop["x0"]

            trial = [dict(e) for e in entries]
            for e in trial:
                if e is not prop and not (e["x0"] == prop["x0"]
                                          and e["x1"] == prop["x1"]
                                          and e["y0"] == prop["y0"]):
                    continue
                e["x0"] += shift
                e["x1"] += shift
            if rules.authored_width(as_parts(trial)) > allowed:
                refused.append(f"{where}: {prop.get('name','a prop')} would "
                               f"take the figure past {allowed} wide")
                continue
            prop["x0"] += shift
            prop["x1"] += shift
            changed = True

        if changed:
            moved.append(where)
            if not dry_run:
                path.write_text(json.dumps(document, indent=2) + "\n")

    verb = "would be" if dry_run else "were"
    print(f"{len(moved)} units {verb} given a prop that breaks the outline")
    for line in moved:
        print(f"  {line}")
    for line in refused:
        print(f"  refused {line}")
    if not dry_run and moved:
        print("\nRun normalise_units.py, then check_units.py.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
