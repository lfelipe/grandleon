#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The whole roster drawn both ways, side by side, at the size a player sees.

    tools/placeholder_art/roster_comparison.py [--out DIR] [style ...]

The question this page exists to answer is the one the whole experiment was
started for: does a role read *better* as a solid than as the sprite it would
replace? That is not a question a triangle count answers, and it is not one a
figure blown up to fill a screen answers either. It is answered at the size the
board actually draws a unit, with the two drawings of the same role beside each
other, and it is answered by looking.

So both drawings are rendered here at **matched scale** and upscaled by the same
nearest-neighbour factor. The sprite is 32 pixels of cell; the model projects to
a figure about thirty pixels tall at the shipped camera. Neither is smoothed,
because smoothing shows something no console draws, and neither is enlarged past
the other, because a comparison in which one side is bigger is not a comparison.

Both bodies are shown. That is the part the old roster page could not show at
all: :data:`~.meshes.COMMISSIONS` holds one figure per role, so a second body
existed as a sprite and as nothing else. The authored roster under ``units/``
holds both, which is why this page has four columns rather than two.

Nothing here is a check and nothing is in a gate. It writes a markdown file and
the pictures it references, and it is meant to be looked at.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from placeholder_art import (characters, figures, gallery, gltf, meshes,
                             playstation_header, pngio, preview3d, profiles,
                             styles)
from placeholder_art.meshes import authored

#: The faction every unit on this page is drawn in. One, and the same one the
#: roster page uses, because six recolours of the same figure answer nothing
#: about whether the figure reads.
FACTION = gallery.UNIT_FACTION

#: The profile the sprites are converted through before they are shown. The
#: console's, so the sprite on this page is the sprite the hardware draws rather
#: than the full-colour master it was reduced from — otherwise the comparison
#: flatters the sprite with colours no ROM has.
PROFILE = profiles.PROFILES_BY_NAME["n64_ci4"]

#: How much each drawing is magnified. One number for both, which is the whole
#: discipline of this page.
ZOOM = preview3d.PREVIEW_ZOOM

PANEL = (26, 28, 36, 255)
INK = (226, 232, 240, 255)
DIM = (148, 160, 178, 255)


def faces_from(parts: Sequence[meshes.Part]) -> List[preview3d.Face]:
    """The faces of a part table, in the figure's own space.

    :func:`.gltf.faces_of` is the one place a box becomes six wound quads, and
    it is reused rather than repeated so this page cannot disagree with the
    export about which way a face points. It answers in glTF space, which is the
    figure's with z negated, so this applies that mapping's inverse exactly as
    :func:`.preview3d.load` does when it reads a file back.
    """
    built: List[preview3d.Face] = []
    for part in parts:
        for normal, corners in gltf.faces_of(part):
            built.append(preview3d.Face(
                corners=tuple((x, y, -z) for x, y, z in corners),
                normal=(normal[0], normal[1], -normal[2]),
                ramp=part.ramp, rung=part.rung))
    return built


def sprite_cell(style: styles.Style, archetype: str, figure: str):
    """One standing sprite, through the console's own reduction and palette."""
    named = "" if figure == figures.DEFAULT_FIGURE.name else figure
    colour = characters.FACTION_COLOURS[FACTION].name
    canvas = styles.sprite(style, archetype, colour, figure=named)
    return profiles.convert(canvas, PROFILE, is_sprite=True)


def converted_for(style: styles.Style) -> Dict[str, profiles.Converted]:
    """The style's character cells, keyed as `mesh_ramp_words` expects them."""
    suffix = styles.asset_suffix(style)
    cells: Dict[str, profiles.Converted] = {}
    for archetype in characters.ARCHETYPE_ORDER:
        for colour in characters.FACTION_COLOURS:
            canvas = styles.sprite(style, archetype, colour.name)
            key = f"characters/{archetype}_{colour.name}{suffix}.png"
            cells[key] = profiles.convert(canvas, PROFILE, is_sprite=True)
    return cells


#: How tall every figure on this page is drawn, in display pixels. Both
#: drawings are brought to this height, which is what "at similar size" means to
#: somebody looking: the same person, the same number of pixels of them.
FIGURE_HEIGHT = 132


def _sprite_ink(cell) -> List[Tuple[int, int, Tuple[int, int, int]]]:
    """Every drawn pixel of a converted cell, as (x, y, colour)."""
    transparent = set(cell.transparent)
    return [(x, y, cell.colours[cell.indices[y * cell.width + x]])
            for y in range(cell.height) for x in range(cell.width)
            if cell.indices[y * cell.width + x] not in transparent]


def _mesh_ink(preview: preview3d.Preview
              ) -> List[Tuple[int, int, Tuple[int, int, int]]]:
    """Every drawn pixel of a preview, with the board tile left out.

    The tile is the cell the figure stands on and not part of the figure, so
    measuring the figure with it included would make every model shorter than
    it is by the depth of a floor — and this page's whole discipline is that the
    two drawings are the same height.
    """
    return [(x, y, colour)
            for y in range(preview.height) for x in range(preview.width)
            if (colour := preview.at(x, y)) is not None
            and colour != preview3d.TILE_COLOUR]


def _blit(sheet: gallery.Sheet,
          ink: Sequence[Tuple[int, int, Tuple[int, int, int]]],
          x: int, y: int, scale: float) -> Tuple[int, int]:
    """Draw ink scaled about its own bounding box, nearest neighbour.

    Nearest neighbour and never a filter: a smoothed figure is a picture of
    something no console draws, and this page is meant to be evidence. The
    scale is a real number rather than an integer because the two drawings do
    not arrive at the same size and one of them has to move to meet the other;
    every destination pixel is still one source pixel, so nothing is blended.
    """
    if not ink:
        return 0, 0
    left = min(px for px, _, _ in ink)
    top = min(py for _, py, _ in ink)
    right = max(px for px, _, _ in ink)
    bottom = max(py for _, py, _ in ink)
    width = int(round((right - left + 1) * scale))
    height = int(round((bottom - top + 1) * scale))
    for row in range(height):
        for column in range(width):
            source_x = left + int(column / scale)
            source_y = top + int(row / scale)
            for px, py, colour in ink:
                if px == source_x and py == source_y:
                    sheet.rect(x + column, y + row, 1, 1,
                               (colour[0], colour[1], colour[2], 255))
                    break
    return width, height


def _blit_fast(sheet: gallery.Sheet,
               ink: Sequence[Tuple[int, int, Tuple[int, int, int]]],
               x: int, y: int, scale: float) -> Tuple[int, int]:
    """:func:`_blit`, with the ink indexed first so it is not scanned per pixel."""
    if not ink:
        return 0, 0
    left = min(px for px, _, _ in ink)
    top = min(py for _, py, _ in ink)
    right = max(px for px, _, _ in ink)
    bottom = max(py for _, py, _ in ink)
    lookup = {(px, py): colour for px, py, colour in ink}
    width = int(round((right - left + 1) * scale))
    height = int(round((bottom - top + 1) * scale))
    for row in range(height):
        source_y = top + int(row / scale)
        for column in range(width):
            colour = lookup.get((left + int(column / scale), source_y))
            if colour is None:
                continue
            sheet.rect(x + column, y + row, 1, 1,
                       (colour[0], colour[1], colour[2], 255))
    return width, height


def _scale_for(ink: Sequence[Tuple[int, int, Tuple[int, int, int]]]) -> float:
    """What this drawing must be multiplied by to stand `FIGURE_HEIGHT` tall."""
    if not ink:
        return 1.0
    top = min(py for _, py, _ in ink)
    bottom = max(py for _, py, _ in ink)
    return FIGURE_HEIGHT / (bottom - top + 1)


def style_sheet(style: styles.Style,
                roster: Dict[Tuple[str, str, str], Tuple[meshes.Part, ...]]
                ) -> Optional[gallery.Sheet]:
    """One style: a row an archetype, four columns, sprite and model per body."""
    cells = converted_for(style)
    order = [name for name in figures.FIGURE_NAMES]

    tile = FIGURE_HEIGHT + 60
    tall = FIGURE_HEIGHT + 24
    gap, label, margin = 12, 16, 96
    columns = len(order) * 2
    width = margin + columns * (tile + gap) + gap
    height = label + 12 + len(characters.ARCHETYPE_ORDER) * (tall + label + gap)
    sheet = gallery.Sheet(width, height)

    x = margin
    for figure in order:
        for drawn in ("sprite", "model"):
            words = figures.FIGURES_BY_NAME[figure].label
            sheet.text(f"{words} {drawn}".upper(), x, 4, DIM, 1)
            x += tile + gap

    y = label + 12
    for archetype in characters.ARCHETYPE_ORDER:
        sheet.text(archetype.upper(), 6, y + tall // 2, INK, 1)
        x = margin
        for figure in order:
            cell = sprite_cell(style, archetype, figure)
            sheet.rect(x, y, tile, tall, PANEL)
            ink = _sprite_ink(cell)
            scale = _scale_for(ink)
            drawn_w, drawn_h = _blit_fast(
                sheet, ink, x + (tile - int(round(
                    (max(px for px, _, _ in ink) - min(px for px, _, _ in ink)
                     + 1) * scale))) // 2,
                y + (tall - FIGURE_HEIGHT) // 2, scale)
            x += tile + gap

            sheet.rect(x, y, tile, tall, PANEL)
            parts = roster.get((style.name, archetype, figure))
            if parts:
                ramps = preview3d.ramp_colours(
                    playstation_header.mesh_ramp_words(cells, style, archetype,
                                                       FACTION),
                    playstation_header.clut_channels)
                preview = preview3d.render(faces_from(parts), ramps)
                mesh_ink = _mesh_ink(preview)
                mesh_scale = _scale_for(mesh_ink)
                span = (max(px for px, _, _ in mesh_ink)
                        - min(px for px, _, _ in mesh_ink) + 1)
                _blit_fast(sheet, mesh_ink,
                           x + (tile - int(round(span * mesh_scale))) // 2,
                           y + (tall - FIGURE_HEIGHT) // 2, mesh_scale)
                sheet.text(f"{len(parts)} PARTS  "
                           f"{len(parts) * meshes.TRIANGLES_PER_PART} TRIS",
                           x + 4, y + tall + 3, DIM, 1)
            else:
                sheet.text("NOT DRAWN", x + 4, y + tall // 2, DIM, 1)
            x += tile + gap
        y += tall + label + gap
    return sheet


def markdown(pages: Dict[str, str]) -> str:
    """The page, and what it shows, said plainly.

    The prose below is a finding rather than a caption. This page exists to be
    looked at and to be *believed*, and a gallery that shows a weak result under
    neutral wording is a gallery that has quietly argued for its own subject.
    """
    lines = [
        "# The roster, drawn both ways",
        "",
        "<!-- Generated by tools/placeholder_art/roster_comparison.py."
        " Do not edit. -->",
        "",
        "Every role in every style, drawn as the sprite the board has always"
        " shown and as the solid that can replace it, with both bodies.",
        "",
        "## How to read it",
        "",
        f"Both drawings are brought to the same **{FIGURE_HEIGHT}-pixel figure"
        " height** and magnified by nearest neighbour, never filtered. Matching"
        " the height is the whole discipline of the page: the model stands"
        " about twice as tall in its board cell as the sprite does in its"
        " 32-pixel one, so showing each at its native framing makes the model"
        " larger and the comparison worthless. The board tile under each model"
        " is excluded when its height is measured, because a floor is not part"
        " of a figure.",
        "",
        "The sprites are shown **after** the console's own reduction and"
        " 16-colour bank, not as their full-colour masters, so what is on this"
        " page is what the hardware draws.",
        "",
        "## What it shows",
        "",
        "At matched size, **the sprite currently reads better than the model on"
        " most roles**, and the gap is widest exactly where a role is"
        " identified by something small: the archer's drawn bow, the mage's"
        " staff and orb, the rogue's dagger, the beast's legs and tail. A box"
        " approximation keeps the mass and loses the read.",
        "",
        "It is not uniform, and the exceptions are informative. The **undead**"
        " roster is the closest of the seven, because its sprites are already"
        " skeletal and boxy, so little is lost in the approximation \u2014 its"
        " beast reads plainly as a four-legged skeleton. The **medieval** and"
        " **nature** rosters lose the most, because their sprites carry the"
        " most rounded detail. Whatever fixes this will be worth measuring per"
        " style rather than once.",
        "",
        "Three causes are visible on this page, and they are structural rather"
        " than a matter of tuning:",
        "",
        "1. **Faces are inconsistent.** Every sprite has eyes. At the shipped"
        " 60-degree pitch a head box shows the viewer mostly its top, so a face"
        " survives only where a part happens to put a dark band across the"
        " front — several undead and pirate models get one and read much"
        " better for it, while most medieval models have a bare plate. That it"
        " happens by accident rather than by rule is the finding.",
        "2. **Held objects become slabs.** A bow, a staff and a blade are thin"
        " and mostly silhouette, which is the one thing an axis-aligned box"
        " cannot be.",
        "3. **The two bodies read alike.** Several roles differ clearly as"
        " sprites, mostly by hair, and barely at all as models.",
        "",
        "None of that is an argument against models — it is the list of what"
        " has to be solved, measured on the whole roster rather than on one"
        " figure. The constraint it has to be solved inside is real: no depth"
        f" buffer on either console, and {meshes.TRIANGLE_BAND[0]}-"
        f"{meshes.TRIANGLE_BAND[1]} triangles a figure, which is a measured"
        " thirty-frames-a-second budget rather than a stylistic choice.",
        "",
    ]
    for name, path in pages.items():
        style = styles.STYLES_BY_NAME[name]
        lines += [f"## {style.label}", "", style.summary, "",
                  f"![{name} roster compared]({path})", ""]
    return "\n".join(lines)


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    # Not `gallery/`: that directory belongs to `generate.py`, which treats
    # anything in it that it did not write as stale and fails `--check` on it.
    # This page is not generated art and is in no gate.
    parser.add_argument("--out", type=Path,
                        default=Path(__file__).resolve().parent)
    parser.add_argument("styles", nargs="*", default=[])
    arguments = parser.parse_args(argv)

    roster = authored.load()
    if not roster:
        print(f"no authored roster under {authored.ROSTER_DIRECTORY}")
        return 1

    wanted = arguments.styles or authored.styles_covered(roster)
    directory = arguments.out / "comparison"
    directory.mkdir(parents=True, exist_ok=True)

    pages: Dict[str, str] = {}
    for name in wanted:
        style = styles.STYLES_BY_NAME[name]
        sheet = style_sheet(style, roster)
        if sheet is None:
            continue
        filename = f"compare_{name}.png"
        pngio.write(directory / filename, sheet.encode())
        pages[name] = f"comparison/{filename}"
        print(f"  {name}: {filename}")

    out = arguments.out / "ROSTER_3D.md"
    out.write_text(markdown(pages), encoding="utf-8")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
