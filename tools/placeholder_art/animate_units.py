#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Move the figures, using the poses their own sprites already move through.

    tools/placeholder_art/animate_units.py medieval knight
    tools/placeholder_art/animate_units.py --all --out sheets/

The sprites animate: `frames.py` gives every one of them four cells beyond
standing -- the two halves of a walk, a blow, and a cast -- and a client indexes
the sheet by position. The solids have had none, so a board drawn with models
would stand perfectly still while the same board drawn with sprites walks.

This gives them the same four, by the same arithmetic. That matters more than it
sounds: the poses are not invented here, they are read out of `frames.py` and
converted from sprite rows into world units, so a walking model and a walking
sprite are the same walk and stay the same walk if somebody retunes the sprite.

**Everything is a translation.** A part is an axis-aligned box and must stay one
-- there is no rotation available, and a rotated box is not a box. That is less
of a loss than it looks, because the sprite animation is translation too: it
displaces bands of pixels rather than rotating limbs. A game whose art is boxes
gets a walk made of shifts, and the sprite already proved that reads.

**Parts are re-sorted after posing.** Moving a leg forward moves it nearer the
eye, and with no depth buffer the draw order is what decides the picture, so a
posed figure is not the authored order any more. This is the run-time sort the
PlayStation already does when a figure turns and the Nintendo 64 scratch does
every frame.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art import (characters, frames, gallery, playstation_header,
                             pngio, preview3d, styles)
from placeholder_art.meshes import Part, authored, rules
import roster_comparison as rc

#: World units per sprite row. A figure stands `GROUND_Y` rows tall in its cell
#: and `MESH_WORLD_HEIGHT` units tall in the world, so this is the one number
#: that carries a pose from one to the other.
PER_ROW = rules.MESH_WORLD_HEIGHT / characters.GROUND_Y

#: The sprite's landmarks, in world height. A sprite counts rows downward from
#: the top of the cell and a figure counts units upward from its feet, so these
#: are measured from the ground line rather than converted directly.
KNEE = (characters.GROUND_Y - frames.KNEE_Y) * PER_ROW
SHOULDER = (characters.GROUND_Y - frames.SHOULDER_Y) * PER_ROW

#: Slots that are a leg. A part-based figure can say which boxes are legs, where
#: a sprite can only say which rows are low, so the stride is applied to the
#: limbs rather than to a band -- the same pose, more precisely aimed.
LEGS = ("leg.l", "leg.r", "foot.l", "foot.r")
ARMS = ("arm.l", "arm.r", "hand.l", "hand.r")
HELD = ("prop.main", "prop.off")


#: How near the ground a part's underside must be to count as standing on it,
#: and how much of the figure's height it must span.
GROUNDED = 2
LEG_SHARE = 0.18


def is_leg(part: Part, slot: str, tall: float) -> bool:
    """Whether a part is something the figure stands on.

    The slot is asked first and not trusted alone. The medieval beast carries
    four legs of which three are slotted `crest` -- their names say "rear leg,
    near" and the slot says otherwise -- so a pose that believed the slot left
    the quadrupeds standing still while every biped walked. A part with its
    underside on the ground and a fair share of the figure's height is a leg
    whatever it is labelled, and that is checkable.
    """
    if slot in LEGS:
        return True
    return (part.y0 <= GROUNDED
            and (part.y1 - part.y0) >= tall * LEG_SHARE)


def side_of(part: Part, centre: float) -> int:
    """-1 for a part on the left of the figure, +1 on the right."""
    middle = (part.x0 + part.x1) / 2.0
    if middle < centre - 1:
        return -1
    if middle > centre + 1:
        return 1
    return 0


def posed(parts: Sequence[Part], slots: Sequence[str], frame: str) -> List[Part]:
    """One pose, as world-unit offsets per part."""
    if frame == "stand":
        return list(parts)

    low = min(p.x0 for p in parts)
    high = max(p.x1 for p in parts)
    centre = (low + high) / 2.0
    tall = max(p.y1 for p in parts) - min(p.y0 for p in parts)
    bob = frames.WALK_BOB * PER_ROW
    lift = frames.WALK_LIFT * PER_ROW
    stride = frames.WALK_STRIDE_OUT * PER_ROW
    swing = frames.WALK_SWING_IN * PER_ROW

    out: List[Part] = []
    lifted: set = set()
    for part, slot in zip(parts, slots):
        middle = (part.y0 + part.y1) / 2.0
        leg = is_leg(part, slot, tall)
        arm = slot in ARMS
        held = slot in HELD
        side = side_of(part, centre)
        dx = dy = dz = 0.0

        if frame == "walk_contact":
            # The down-beat: feet apart, the body at its lowest. The sprite
            # settles the body rather than reaching the far foot, so that a
            # crest or a banner stays inside the cell; the same choice keeps a
            # figure inside its width here.
            if leg:
                dx = stride * (side if side else 1)
            else:
                dy = -bob
        elif frame == "walk_pass":
            # The up-beat: one leg swings past the planted one, body back at
            # standing height, so the pair is a cycle and not a crouch.
            #
            # A lifting leg is *shortened from below*, not floated upward. A box
            # translated up leaves a hole at the hip -- which is what the first
            # version drew, and it is the difference between a figure stepping
            # and a figure coming apart. A real leg rotates at the hip and the
            # foot rises; a box cannot rotate, but it can keep its top where the
            # hip is and raise its bottom, which reads as the same thing and
            # stays attached. The foot travels with it whole.
            if leg and side >= 0:
                dx = -swing
                if slot in ("foot.l", "foot.r"):
                    dy = lift
                else:
                    lifted.add(len(out))
        elif frame == "lunge":
            # Coiled into a blow: a squash, not a sink. Legs planted, the torso
            # settling and the shoulders settling further.
            if not leg:
                dy = -(frames.LUNGE_SQUASH if middle > SHOULDER
                       else frames.LUNGE_CROUCH) * PER_ROW
            if held or arm:
                dz = -frames.LUNGE_CROUCH * PER_ROW
        elif frame == "cast":
            # Gathered, and what is carried raised: the one pose where the prop
            # is the whole of the read at this size.
            if held or arm:
                dy = frames.CAST_LIFT * PER_ROW
            elif not leg:
                dy = -frames.CAST_GATHER * PER_ROW

        raise_floor = lift if len(out) in lifted else 0.0
        low = int(round(part.y0 + dy + raise_floor))
        high = int(round(part.y1 + dy))
        if low >= high:
            low = high - 1
        out.append(part.__replace__(
            x0=int(round(part.x0 + dx)), x1=int(round(part.x1 + dx)),
            y0=low, y1=high,
            z0=int(round(part.z0 + dz)), z1=int(round(part.z1 + dz))))

    # Far to near again: a leg that stepped forward is nearer than it was, and
    # with no depth buffer that is the whole of what decides the picture.
    return sorted(out, key=rules.depth_key, reverse=True)


def load(style_name: str, archetype: str,
         figure: str) -> Tuple[List[Part], List[str]]:
    path = authored.ROSTER_DIRECTORY / style_name / f"{archetype}.{figure}.json"
    document = json.loads(path.read_text())
    parts = [Part(p["x0"], p["x1"], p["y0"], p["y1"], p["z0"], p["z1"],
                  p["ramp"], p["rung"], str(p.get("name", "")))
             for p in document["parts"]]
    return parts, [str(p.get("slot", "")) for p in document["parts"]]


def render(parts: Sequence[Part], style: styles.Style, archetype: str, cells):
    ramps = preview3d.ramp_colours(
        playstation_header.mesh_ramp_words(cells, style, archetype, rc.FACTION),
        playstation_header.clut_channels)
    return preview3d.render(rc.faces_from(list(parts)), ramps)


#: The cells a sheet holds, standing first, in the sprite sheet's own order.
SEQUENCE = ("stand",) + frames.FRAME_NAMES


def sheet_for(style_name: str, archetype: str, figure: str,
              zoom: int) -> gallery.Sheet:
    style = styles.STYLES_BY_NAME[style_name]
    cells = rc.converted_for(style)
    parts, slots = load(style_name, archetype, figure)

    tile_w = preview3d.PREVIEW_WIDTH * zoom
    tile_h = preview3d.PREVIEW_HEIGHT * zoom
    sheet = gallery.Sheet(len(SEQUENCE) * (tile_w + 8) + 8, tile_h + 34)
    for index, frame in enumerate(SEQUENCE):
        x = 8 + index * (tile_w + 8)
        sheet.text(frame.upper().replace("_", " "), x, 4, (200, 210, 225, 255), 1)
        sheet.rect(x, 18, tile_w, tile_h, (26, 28, 36, 255))
        drawn = render(posed(parts, slots, frame), style, archetype, cells)
        for row in range(drawn.height):
            for column in range(drawn.width):
                colour = drawn.at(column, row)
                if colour is None:
                    continue
                sheet.rect(x + column * zoom, 18 + row * zoom, zoom, zoom,
                           (colour[0], colour[1], colour[2], 255))
    return sheet


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("style", nargs="?", default="medieval")
    parser.add_argument("archetype", nargs="?", default="knight")
    parser.add_argument("--figure", default="first")
    parser.add_argument("--zoom", type=int, default=5)
    parser.add_argument("--out", type=Path,
                        default=Path(__file__).resolve().parent / "animation")
    arguments = parser.parse_args()

    arguments.out.mkdir(parents=True, exist_ok=True)
    sheet = sheet_for(arguments.style, arguments.archetype,
                      arguments.figure, arguments.zoom)
    name = f"{arguments.style}-{arguments.archetype}-{arguments.figure}.png"
    pngio.write(arguments.out / name, sheet.encode())
    print(f"wrote {arguments.out / name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
