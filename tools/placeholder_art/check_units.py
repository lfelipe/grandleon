#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Hold the authored roster to the same mesh rules the commissioned figures meet.

    tools/placeholder_art/check_units.py            # every unit
    tools/placeholder_art/check_units.py medieval   # one style

The roster under ``units/`` is generated art: it was authored against each
role's own sprite by a generator that lives outside this repository, and it will
be regenerated. That is exactly why it needs this. A commissioned figure is a
table a person typed and a reviewer read; a generated one is a table nobody has
read, and the only thing standing between it and a ROM is whether the rules
accept it.

The rules are not restated here. :func:`~.meshes.check` is the same entry point
the commissioned figures go through, and it is handed the silhouettes measured
off **this style's own sprites, for this figure** — a mesh is held to the body
it is drawn as, so the second figure is measured against the second figure's
sprite rather than against the first one's.

Exit status is 0 when every unit is accepted, 1 otherwise, so this can sit in a
gate. It prints what it rejected and why, one line a unit, because "35 of 112
failed" is not something anybody can act on.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art import (characters, figures, meshes, playstation_header,
                             profiles, styles)
from placeholder_art.meshes import authored

#: The profile the silhouettes are measured in. The console one rather than
#: ``modern``: a mesh is held to the sprite the hardware actually draws, and
#: that sprite has been through the reduction and the 16-colour bank.
PROFILE = profiles.PROFILES_BY_NAME["n64_ci4"]


def silhouettes_for(style: styles.Style, figure: str) -> List[meshes.Silhouette]:
    """One silhouette per archetype, in the roster's order, for one figure."""
    colour = characters.FACTION_COLOURS[0].name
    name = "" if figure == figures.DEFAULT_FIGURE.name else figure
    measured = []
    for archetype in characters.ARCHETYPE_ORDER:
        canvas = styles.sprite(style, archetype, colour, figure=name)
        converted = profiles.convert(canvas, PROFILE, is_sprite=True)
        measured.append(playstation_header.silhouette_of(converted))
    return measured


def main() -> int:
    wanted = sys.argv[1:]
    units = authored.load()
    if not units:
        print(f"no authored roster under {authored.ROSTER_DIRECTORY}")
        return 1

    by_style: Dict[Tuple[str, str], Dict[str, Tuple[meshes.Part, ...]]] = {}
    for (style_name, archetype, figure), parts in units.items():
        if wanted and style_name not in wanted:
            continue
        by_style.setdefault((style_name, figure), {})[archetype] = parts
    if not by_style:
        print(f"no units matched {' '.join(wanted)}")
        return 1

    accepted = rejected = 0
    failures: List[str] = []
    for (style_name, figure), commission in sorted(by_style.items()):
        style = styles.STYLES_BY_NAME[style_name]
        measured = silhouettes_for(style, figure)
        # One unit at a time rather than the whole style at once: check_commission
        # asserts, so a single bad table would take its 15 companions down with
        # it and report one problem where there are several.
        for archetype in characters.ARCHETYPE_ORDER:
            parts = commission.get(archetype)
            if parts is None:
                continue
            where = f"{style_name}/{archetype}/{figure}"
            try:
                with meshes.roster({(style_name, archetype, figure): parts}):
                    meshes.check(measured, style, figure)
            except AssertionError as error:
                rejected += 1
                first = str(error).strip().splitlines()[0]
                failures.append(f"  {where}: {first}")
            else:
                accepted += 1

    total = accepted + rejected
    print(f"{accepted} of {total} authored units meet every mesh rule")
    for line in failures:
        print(line)
    return 0 if rejected == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
