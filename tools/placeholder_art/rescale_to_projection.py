# SPDX-License-Identifier: MIT
"""Rebuild every figure to the height its own sprite is drawn.

    python3 rescale_to_projection.py [--check]

Rule 1 used to build all 112 figures a flat ``unit_world / cos 60`` = 128 units
tall. That was wrong twice over -- it sized them to a 32-row tile where a
sprite's figure is the 28 rows above the faction disc, and it never subtracted
the screen height a figure's own depth buys at ``tan 60`` per unit. The two
compound to 1.39, which is exactly how much narrower than its sprite every mesh
measured once both were brought to the same height.

This scales each figure's **y and z** by :func:`meshes.build_scale` so that its
projection is its sprite's figure height, and writes the result back into the
art: the roster's JSON files and the literal part tables in the commission
modules. Both axes, because the projection is made of both and because scaling
y alone flattens every box into a plate -- see :func:`meshes.build_scale`. x is
left alone: the width is rule 4's, and it was never the thing that was wrong.

It is kept rather than run once and deleted, because it is the derivation: the
new coordinates are a function of the sprites, and if a sprite is redrawn the
figures want rebuilding rather than hand-editing.

``--check`` rescales nothing and reports what would move.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import normalise_units
import roster_comparison as rc
from placeholder_art import characters, meshes, playstation_header, styles
from placeholder_art.meshes import Part

HERE = Path(__file__).resolve().parent
ROSTER = HERE / "units"
COMMISSIONS = HERE / "placeholder_art" / "meshes"

#: One `Part(` line of a commission module: six integer corners, then the rest.
#: Each corner is captured with the separator that follows it so a rewritten
#: line keeps the column its author wrote it in.
PART_LINE = re.compile(
    r"^(\s*Part\(\s*)(-?\d+)(,\s*)(-?\d+)(,\s*)(-?\d+)(,\s*)(-?\d+)"
    r"(,\s*)(-?\d+)(,\s*)(-?\d+)(,.*)$")


def render(line: str, entry: Dict[str, object]) -> str:
    """One `Part(` line with this entry's corners, in the columns it had."""
    match = PART_LINE.match(line.rstrip("\n"))
    assert match is not None, f"not a part line: {line!r}"
    head, x0, s1, x1, s2, y0, s3, y1, s4, z0, s5, z1, tail = match.groups()
    values = [entry["x0"], entry["x1"], entry["y0"], entry["y1"],
              entry["z0"], entry["z1"]]
    widths = [len(x0), len(x1), len(y0), len(y1), len(z0), len(z1)]
    a, b, c, d, e, f = (str(v).rjust(w) for v, w in zip(values, widths))
    return f"{head}{a}{s1}{b}{s2}{c}{s3}{d}{s4}{e}{s5}{f}{tail}\n"


def silhouette_for(style: styles.Style, archetype: str, figure: str):
    """The sprite this figure is held to: its own style, its own body."""
    return playstation_header.silhouette_of(
        rc.sprite_cell(style, archetype, figure))


def shrink(low: int, high: int, scale: float) -> Tuple[int, int]:
    """One axis of one box, scaled, keeping the volume rule 3 needs.

    A box an integer thick cannot always be scaled and stay an integer thick,
    and a box with no thickness has faces a viewer would cull rather than see.
    So a span that would round away keeps one unit.
    """
    first, second = round(low * scale), round(high * scale)
    return (first, first + 1) if first >= second else (first, second)


def parts_of(entries: List[dict]) -> List[Part]:
    return [Part(e["x0"], e["x1"], e["y0"], e["y1"], e["z0"], e["z1"],
                 e["ramp"], e["rung"], str(e.get("name", ""))) for e in entries]


def report(where: str, parts: List[Part], scale: float, silhouette) -> str:
    was = meshes.projected_height(parts)
    now = meshes.projected_height(parts, scale)
    return (f"{where:34} drawn {was:5.1f} -> {now:5.1f} "
            f"(asked {meshes.target_height(silhouette)}), y x{scale:.3f}")


def rescale_roster(check: bool) -> Tuple[int, List[str]]:
    lines: List[str] = []
    moved = 0
    for path in sorted(ROSTER.glob("*/*.json")):
        document = json.loads(path.read_text())
        style = styles.STYLES_BY_NAME[document["style"]]
        silhouette = silhouette_for(style, document["archetype"],
                                    document["figure"])
        parts = parts_of(document["parts"])
        scale = meshes.build_scale(parts, silhouette)
        lines.append(report(f"{document['style']}/{document['archetype']}"
                            f".{document['figure']}", parts, scale, silhouette))
        if check:
            continue
        for entry in document["parts"]:
            entry["y0"], entry["y1"] = shrink(entry["y0"], entry["y1"], scale)
            entry["z0"], entry["z1"] = shrink(entry["z0"], entry["z1"], scale)
        floor = min(entry["y0"] for entry in document["parts"])
        for entry in document["parts"]:
            entry["y0"] -= floor
            entry["y1"] -= floor
        path.write_text(json.dumps(document, indent=2) + "\n")
        moved += 1
    return moved, lines


def rescale_commissions(check: bool) -> Tuple[int, List[str]]:
    lines: List[str] = []
    moved = 0
    for name in sorted(meshes.commissioned_styles()):
        module = COMMISSIONS / f"{name}.py"
        style = styles.STYLES_BY_NAME[name]
        source = module.read_text().splitlines(keepends=True)
        # The table a style writes an archetype down as is named for the
        # *drawing* -- sengoku's knight is SAMURAI, nature's is BADGER_GUARD --
        # so the variable is found by identity against the module's own mapping
        # rather than by guessing the archetype's name in capitals. A first
        # version guessed, matched only `medieval`, and reported having written
        # all seven modules while writing one.
        loaded = __import__(f"placeholder_art.meshes.{name}",
                            fromlist=["MESHES"])
        named = {variable: value for variable, value in vars(loaded).items()
                 if variable.isupper()}
        scales: Dict[str, float] = {}
        expected: Dict[str, int] = {}
        tables: Dict[str, List[Part]] = {}
        for archetype in characters.ARCHETYPE_ORDER:
            parts = meshes.COMMISSIONS[name].get(archetype)
            if not parts:
                continue
            variable = next((v for v, value in named.items()
                             if value is loaded.MESHES[archetype]), None)
            assert variable is not None, (
                f"{name}.py: no module-level table is {archetype}'s; the "
                f"rewrite would skip it silently")
            silhouette = silhouette_for(style, archetype, "first")
            scale = meshes.build_scale(list(parts), silhouette)
            scales[variable] = scale
            expected[variable] = len(parts)
            tables[variable] = list(parts)
            lines.append(report(f"{name}/{archetype}", list(parts), scale,
                                silhouette))
        if check:
            continue
        blocks: Dict[str, List[int]] = {}
        variable = None
        for index, line in enumerate(source):
            opening = re.match(r"^([A-Z_]+): Tuple\[Part, \.\.\.\] = \($", line)
            if opening:
                variable = opening.group(1)
            elif line.startswith(")"):
                variable = None
            elif variable is not None and PART_LINE.match(line.rstrip("\n")):
                blocks.setdefault(variable, []).append(index)

        out = list(source)
        written = {name: len(rows) for name, rows in blocks.items()
                   if name in scales}
        for table, rows in blocks.items():
            scale = scales.get(table)
            if scale is None:
                continue
            entries = []
            for index, part in zip(rows, tables[table]):
                match = PART_LINE.match(source[index].rstrip("\n"))
                x0, x1, y0, y1, z0, z1 = (int(match.group(i))
                                          for i in (2, 4, 6, 8, 10, 12))
                assert (x0, x1, y0, y1, z0, z1) == (
                    part.x0, part.x1, part.y0, part.y1, part.z0, part.z1), (
                    f"{name}.py: the line at {index + 1} is not the part the "
                    f"module loaded there; the rewrite would scramble the table")
                ny0, ny1 = shrink(y0, y1, scale)
                nz0, nz1 = shrink(z0, z1, scale)
                entries.append({"x0": x0, "x1": x1, "y0": ny0, "y1": ny1,
                                "z0": nz0, "z1": nz1, "ramp": part.ramp,
                                "rung": part.rung, "name": part.name,
                                "_line": source[index]})
            floor = min(entry["y0"] for entry in entries)
            for entry in entries:
                entry["y0"] -= floor
                entry["y1"] -= floor
            # Scaling y moves depth_key, so an array its author ordered
            # far-to-near at the old height is not ordered at the new one.
            # Settled by the same routine the roster uses, and by nothing
            # stricter: rule 3 is what the console needs, not a tidier margin.
            normalise_units.settle(entries)
            for index, entry in zip(rows, entries):
                out[index] = render(entry["_line"], entry)

        assert written == expected, (
            f"{name}.py: rewrote {written} part lines where the tables hold "
            f"{expected}")
        module.write_text("".join(out))
        moved += 1
    return moved, lines


def main(argv: List[str]) -> int:
    check = "--check" in argv
    r_moved, r_lines = rescale_roster(check)
    c_moved, c_lines = rescale_commissions(check)
    for line in r_lines + c_lines:
        print(line)
    verb = "would move" if check else "moved"
    print(f"\n{verb}: {len(r_lines)} roster figures, {len(c_lines)} commissioned")
    if not check:
        print(f"wrote {r_moved} JSON files and {c_moved} commission modules")
        print("run normalise_units.py next: scaling y changes depth_key, and "
              "the draw order is measured on it")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
