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

from placeholder_art import playstation_header, preview3d, styles
from placeholder_art.meshes import Part, authored, rules
import roster_comparison as rc

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


def frontmost_at(parts: List[dict], head: dict) -> int:
    """The nearest z of anything standing in front of the head's own band.

    A band placed two units in front of the *head* is not in front of the
    *face* if a mask, a muzzle or a hood hollow reaches further forward, and it
    is then drawn inside another box and never seen. Three of the sixty-one
    bands this tool wrote did exactly that -- `sengoku/commander`'s sits at
    z -7..-4 while its face box reaches -11 -- and geometry alone could not tell,
    because the band was correctly two units in front of the box it was measured
    from. So the whole head region is asked, not just the part chosen.
    """
    low, high = head["y0"], head["y1"]
    reaching = [part["z0"] for part in parts
                if part["y1"] >= low and part["y0"] <= high]
    return min(reaching) if reaching else head["z0"]


def band_for(head: dict, front: int) -> dict:
    """A face band measured off one head box, in front of everything near it."""
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
        "z0": front - PROUD,
        "z1": front + 1,
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


def drawn_pixels(entries: List[dict], style_name: str, archetype: str,
                 cells) -> set:
    """What a part table actually puts on screen, at the shipped camera."""
    style = styles.STYLES_BY_NAME[style_name]
    parts = [Part(e["x0"], e["x1"], e["y0"], e["y1"], e["z0"], e["z1"],
                  e["ramp"], e["rung"], str(e.get("name", "")))
             for e in entries]
    parts.sort(key=rules.depth_key, reverse=True)
    ramps = preview3d.ramp_colours(
        playstation_header.mesh_ramp_words(cells, style, archetype, rc.FACTION),
        playstation_header.clut_channels)
    drawn = preview3d.render(rc.faces_from(parts), ramps)
    return {(x, y, drawn.at(x, y))
            for y in range(drawn.height) for x in range(drawn.width)
            if drawn.at(x, y) is not None
            and drawn.at(x, y) != preview3d.TILE_COLOUR}


def visible_band(entries: List[dict], head: dict, style_name: str,
                 archetype: str, cells) -> Optional[dict]:
    """A band placed so that it can actually be seen, or None.

    Geometry was not enough, twice. Placing two units in front of the head left
    three bands buried inside a face box that reached further forward; placing
    them in front of everything in the head's y-range moved the failure to three
    different units rather than removing it. Occlusion at this pitch depends on
    the whole scene and the draw order, not on one comparison of two boxes.

    So the band is *rendered* and kept only if it changed the picture, stepping
    forward until it does. That is the same discipline every other lesson here
    ended in: look at the output rather than reason about the input.
    """
    without = drawn_pixels(entries, style_name, archetype, cells)
    front = frontmost_at(entries, head)
    for extra in range(0, 7):
        band = band_for(head, front - extra)
        if drawn_pixels(entries + [band], style_name, archetype,
                        cells) != without:
            return band
    return None


def main() -> int:
    dry_run = "--dry-run" in sys.argv
    root = authored.ROSTER_DIRECTORY
    ceiling = rules.TRIANGLE_BAND[1]
    per_part = rules.TRIANGLES_PER_PART

    added, skipped, already = [], [], 0
    seen: Dict[str, object] = {}
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

        cells = seen.setdefault(document["style"],
                                rc.converted_for(
                                    styles.STYLES_BY_NAME[document["style"]]))
        band = visible_band(parts, head, document["style"],
                            document["archetype"], cells)
        if band is None:
            skipped.append(f"{where}: no room in front of the head for a face "
                           f"that can be seen")
            continue
        parts.append(band)
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
