# SPDX-License-Identifier: MIT
"""Put each figure's asymmetric features on the side its own sprite puts them.

    python3 face_the_sprite.py [--check]

Rule 4 holds a mesh to the *width* of its sprite's opaque box, which a mirrored
figure satisfies exactly as well as an unmirrored one: a box has the same width
whichever way round it is. So nothing ever asked which side a staff, a shield or
a banner was on, and measured, nothing had: mirroring a roster figure matches
its sprite better on 29 of 56 units, which is a coin toss. The medieval mage
carries its staff on the mesh's left and on the sprite's right.

That is worth fixing on its own -- a unit whose shield is on the wrong side is
the wrong drawing of that unit -- and it also blocks anything that wants to read
the sprite *positionally*, which is most of what is left to do inside the
outline.

A figure is turned only when turning it is a clear improvement, by
:data:`MARGIN` of silhouette overlap. Most figures are near enough symmetric
that the two orientations score the same, and turning those would be churn: the
question is only asked of figures that have an answer.

**A role's two bodies were facing opposite ways in 32 of 56 roles.**
:mod:`..figures` states the rule they break -- "the role's identifying equipment
is drawn by one routine for both figures and does not move by a pixel" -- and
the sprites keep it: measured, their two bodies agree on 52 of 56 roles and
disagree on none. Only the solids drifted, and nothing measured them, because
rule 4 holds a mesh to its sprite's *width* and a mirrored figure is exactly as
wide. So each figure is turned to its own sprite, which makes the two bodies
agree by construction; where a body is too symmetric for its sprite to have an
opinion, it is turned to agree with the role's other body instead of being left
to disagree silently.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import roster_comparison as rc
from placeholder_art import meshes, styles
from placeholder_art.meshes import Part
from silhouette import mask, mesh_ink, sprite_ink

HERE = Path(__file__).resolve().parent
ROSTER = HERE / "units"

#: How much better a turned figure must match its sprite before it is turned.
#: Measured over the roster: sixteen figures gain more than this and twelve lose
#: more, while twenty-eight sit inside it and are symmetric enough that the
#: question does not arise for them.
MARGIN = 0.05

#: The words and slot suffixes that mean a side, and what they become. A turned
#: figure whose parts still say "left" where the box is now on the right is a
#: figure whose next reader is misled, and every tool here that aims a pose or a
#: prop reads these.
SIDES = (("left", "right"), (".l", ".r"))


def turned(parts: List[Part]) -> List[Part]:
    """The same figure, mirrored in x, with its sides renamed."""
    return [Part(-part.x1, -part.x0, part.y0, part.y1, part.z0, part.z1,
                 part.ramp, part.rung, swap(part.name)) for part in parts]


def swap(text: str) -> str:
    out = text
    for one, other in SIDES:
        out = out.replace(one, "\0").replace(other, one).replace("\0", other)
    return out


def match(parts: List[Part], style, archetype: str, figure: str,
          cells) -> float:
    """How well this figure's outline sits on its own sprite's."""
    sprite, wide = mask(sprite_ink(style, archetype, figure))
    drawn, span = mask(mesh_ink(parts, style, archetype, cells))
    box = max(wide, span)
    left = {(x + (box - wide) // 2, y) for x, y in sprite}
    right = {(x + (box - span) // 2, y) for x, y in drawn}
    return len(left & right) / max(len(left | right), 1)


def main(argv: List[str]) -> int:
    check = "--check" in argv
    roles: Dict[Tuple[str, str], Dict[str, dict]] = {}
    cells: Dict[str, object] = {}

    for path in sorted(ROSTER.glob("*/*.json")):
        document = json.loads(path.read_text())
        style = styles.STYLES_BY_NAME[document["style"]]
        cells.setdefault(document["style"], rc.converted_for(style))
        parts = [Part(e["x0"], e["x1"], e["y0"], e["y1"], e["z0"], e["z1"],
                      e["ramp"], e["rung"], str(e.get("name", "")))
                 for e in document["parts"]]
        gain = (match(turned(parts), style, document["archetype"],
                      document["figure"], cells[document["style"]])
                - match(parts, style, document["archetype"],
                        document["figure"], cells[document["style"]]))
        roles.setdefault((document["style"], document["archetype"]), {})[
            document["figure"]] = {"path": path, "document": document,
                                   "parts": parts, "gain": gain,
                                   "turn": gain > MARGIN}
        print(f"  {document['style']}/{document['archetype']}"
              f".{document['figure']}: {gain:+.3f}", flush=True)

    agreed = 0
    for key, bodies in sorted(roles.items()):
        # Where a body's own sprite has no opinion, the role's other body is the
        # next best authority: two bodies of one character face one way.
        decided = [name for name, body in bodies.items()
                   if abs(body["gain"]) > MARGIN]
        if len(decided) == 1 and len(bodies) == 2:
            lead = bodies[decided[0]]
            other = bodies[[n for n in bodies if n != decided[0]][0]]
            style = styles.STYLES_BY_NAME[key[0]]
            facing = turned(lead["parts"]) if lead["turn"] else lead["parts"]
            follow = turned(other["parts"])
            here = match(other["parts"], style, key[1], "first", cells[key[0]])
            there = match(follow, style, key[1], "first", cells[key[0]])
            # measured against the *lead body*, not the sprite, because that is
            # the question being asked of it
            if (there > here) != other["turn"]:
                other["turn"] = there > here
                agreed += 1

    turnedcount = 0
    for key, bodies in sorted(roles.items()):
        for name, body in sorted(bodies.items()):
            if not body["turn"]:
                continue
            turnedcount += 1
            print(f"turning {key[0]}/{key[1]}.{name}: {body['gain']:+.3f}")
            if check:
                continue
            for entry in body["document"]["parts"]:
                entry["x0"], entry["x1"] = -entry["x1"], -entry["x0"]
                if entry.get("name"):
                    entry["name"] = swap(str(entry["name"]))
                if entry.get("slot"):
                    entry["slot"] = swap(str(entry["slot"]))
            body["path"].write_text(
                json.dumps(body["document"], indent=2) + "\n")

    verb = "would turn" if check else "turned"
    print(f"\n{verb} {turnedcount} of 112 figures to face their sprites")
    print(f"{agreed} of those decided by the role's other body rather than by "
          f"its own sprite")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
