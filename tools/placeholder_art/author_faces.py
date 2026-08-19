#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Give every figure a face, instead of letting half of them have one by luck.

    tools/placeholder_art/author_faces.py            # every unit that needs one
    tools/placeholder_art/author_faces.py --dry-run

The roster comparison found this and it was the largest single difference between
a sprite and its solid: every sprite has eyes, and at the shipped 60-degree pitch
a head box shows the viewer mostly its *top*, so a model has a bare plate where
the sprite has a face.

It was never that faces were impossible. Fifty-one of the 112 units already carry
one -- a dark band standing proud of the front of the head, which is what a visor
or a pair of eyes reduces to at this size -- and they read markedly better for
it. The other sixty-one do not, and nothing decided which was which. That is the
defect: not the absence of a rule but the absence of *any* rule.

So this adds the band the fifty-one already have to the sixty-one that lack it,
measured off each unit's own head rather than from a table of constants. A
quadruped's muzzle takes the same band and it reads as an eye stripe, which is
what its sprite draws too.

One box, twelve triangles. Units already at the top of the triangle band are
reported and left alone rather than pushed past it.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Dict, List, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art.meshes import authored, rules

#: How far the band stands proud of the head's front face, in world units. Two,
#: which is one screen pixel at the size a figure is drawn and enough for the
#: face to catch a different shade from the head behind it.
PROUD = 2

#: The band's share of the head's width and height. Narrower than the head so it
#: reads as a feature rather than as a differently-coloured head, and shallow
#: enough that it cannot be mistaken for a chin.
WIDTH_SHARE = 0.66
HEIGHT_SHARE = 0.26

#: The band's rung, chosen against the head's. Rung 3 is the lightest of the
#: four and rung 0 the darkest, so these are the two ends of the ramp.
FACE_ON_DARK = 3
FACE_ON_LIGHT = 0

#: Where the band sits between the head's bottom and top. Eyes sit above the
#: middle of a face, and at this pitch anything lower disappears under the brow.
HEIGHT_AT = 0.56


#: How tall a head part has to be relative to its width before a face can sit on
#: it. A brim, a visor slot or a collar is wide and shallow; a skull or a helm is
#: roughly as tall as it is wide.
UPRIGHT = 0.5


def main_head(parts: List[dict]) -> Optional[dict]:
    """The skull or helm a face belongs on.

    Not simply the widest thing slotted to the head, which is what the first
    version took and which is wrong on every figure wearing a hat: a mage's brim
    is 28 wide and 6 tall, so it won that comparison and the face was painted
    across the hat instead of the face. A brim is wide and shallow and a head is
    not, so shallow parts are excluded before the widest is chosen.
    """
    heads = [part for part in parts if part.get("slot") == "head"]
    if not heads:
        return None
    upright = [part for part in heads
               if (part["y1"] - part["y0"]) >=
                  UPRIGHT * max(1, part["x1"] - part["x0"])]
    return max(upright or heads,
               key=lambda part: (part["x1"] - part["x0"]) *
                                (part["z1"] - part["z0"]))


def has_face(parts: List[dict], head: dict) -> bool:
    """Whether something dark already stands proud of the head's front."""
    return any(part["z0"] < head["z0"]
               and part["y0"] >= head["y0"] - 2
               and part["y1"] <= head["y1"] + 4
               and part["rung"] <= 1
               for part in parts)


def band_for(head: dict) -> dict:
    """A face band measured off one head box."""
    width = head["x1"] - head["x0"]
    height = head["y1"] - head["y0"]
    centre_x = (head["x0"] + head["x1"]) / 2.0
    half = max(2, int(round(width * WIDTH_SHARE / 2)))
    thickness = max(2, int(round(height * HEIGHT_SHARE)))
    middle = head["y0"] + height * HEIGHT_AT
    y0 = int(round(middle - thickness / 2.0))
    return {
        "name": "face band",
        "slot": "head",
        "x0": int(round(centre_x)) - half,
        "x1": int(round(centre_x)) + half,
        "y0": y0,
        "y1": y0 + thickness,
        "z0": head["z0"] - PROUD,
        "z1": head["z0"] + 1,
        # The neutral ramp, never the faction one: a face is not a thing that
        # should change colour when the banner does.
        "ramp": rules.RAMP_NEUTRAL,
        # The rung is chosen against the head it sits on rather than fixed.
        #
        # A dark band was the obvious choice and it is invisible on half the
        # roster: the rogue's hood is already the darkest rung, so a darkest-rung
        # face on it is a face nobody can see. `textures/README.md` says the same
        # thing about painting -- value contrast is what makes a figure legible
        # at this size, more than hue -- and it applies to solids for the same
        # reason. So a dark head takes a light band and a light head takes a
        # dark one.
        "rung": FACE_ON_DARK if head["rung"] <= 1 else FACE_ON_LIGHT,
    }


def main() -> int:
    dry_run = "--dry-run" in sys.argv
    root = authored.ROSTER_DIRECTORY
    ceiling = rules.TRIANGLE_BAND[1]
    per_part = rules.TRIANGLES_PER_PART

    added, skipped, already = [], [], 0
    for path in sorted(root.glob("*/*.json")):
        document = json.loads(path.read_text())
        parts = document["parts"]
        where = f"{document['style']}/{document['archetype']}/{document['figure']}"

        head = main_head(parts)
        if head is None:
            skipped.append(f"{where}: no head to measure a face from")
            continue
        if has_face(parts, head):
            already += 1
            continue
        if (len(parts) + 1) * per_part > ceiling:
            skipped.append(f"{where}: {len(parts)} parts already, a face would "
                           f"pass {ceiling} triangles")
            continue

        parts.append(band_for(head))
        added.append(where)
        if not dry_run:
            document["parts"] = parts
            path.write_text(json.dumps(document, indent=2) + "\n")

    verb = "would gain" if dry_run else "gained"
    print(f"{already} units already had a face; {len(added)} {verb} one")
    for line in skipped:
        print(f"  skipped {line}")
    if not dry_run and added:
        print("\nRun normalise_units.py next: the band stands in front of the "
              "head and the draw order has to follow it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
