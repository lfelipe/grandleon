# SPDX-License-Identifier: MIT
"""Build orchestration: render once, emit for every profile.

Native art is rendered a single time into :class:`Asset` records, then each
profile converts and encodes the same records. Adding a terrain type, an
archetype, or a profile therefore never touches this module.

Terrain is rendered once per theme (:mod:`.themes`). The default theme's files
keep the names they always had; every other theme's carry its name as a
suffix, so adding a theme adds files and moves none.

Output layout, under the chosen output directory::

    assets/<profile>/terrain/<name>_base.png        4 interior variants
    assets/<profile>/terrain/<name>_blob.png        47 autotile variants
    assets/<profile>/terrain/<name>_over_<under>.png  47 transition variants
    assets/<profile>/terrain/<name>_base_<theme>.png   the same, themed
    assets/<profile>/characters/<archetype>_<colour>.png
    assets/<profile>/sample_map.png
    assets/<profile>/palette.png                    shared-palette profiles only
    assets/<profile>/palettes.json
    assets/<profile>/manifest.json
    assets/palette_usage.json                       profile-independent
    GALLERY.md and gallery/*.png
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Tuple

from . import (autotile, backdrops, characters, figures, frames, gallery,
               logo, playstation_header, pngio,
               profiles, provided, scene, shimmer, sprites_header, styles,
               terrain, themes, verify)
from .palette import PALETTE_SIZE, RGB, TRANSPARENT
from .palette import ramp as palette_ramp
from .raster import Canvas

#: Blob sheets are laid out eight variants wide; 47 variants leave one empty
#: slot, which is left transparent rather than duplicated.
BLOB_COLUMNS = 8


@dataclass
class Asset:
    """One native-resolution image, before any profile has touched it."""

    path: str
    kind: str
    canvas: Canvas
    columns: int
    rows: int
    metadata: Dict[str, object] = field(default_factory=dict)


def _sheet(tiles: List[Canvas], columns: int) -> Tuple[Canvas, int, int]:
    size = terrain.TILE
    rows = (len(tiles) + columns - 1) // columns
    canvas = Canvas(columns * size, rows * size)
    for index, tile in enumerate(tiles):
        canvas.blit_opaque(tile, (index % columns) * size, (index // columns) * size)
    return canvas, columns, rows


def _character_keys() -> List[Tuple[str, characters.FactionColour,
                                    figures.Figure]]:
    """Every sprite one style owes, in the order its files are listed.

    The figure is the innermost axis rather than the outermost, so a role's two
    figures land next to each other in every listing a reader or a diff ever
    sees. The first figure's suffix is empty, so its files are byte-for-byte the
    ones that shipped before this axis existed.
    """
    return [
        (archetype, colour, shape)
        for archetype in characters.ARCHETYPE_ORDER
        for colour in characters.FACTION_COLOURS
        for shape in figures.FIGURE_ORDER
    ]


def _is_default_figure(asset: Asset) -> bool:
    """Whether an asset is the first figure, or is not a figure's at all.

    The seam this repository draws between the two halves of the figure axis. A
    **file** is addressed by name, so a second figure's PNG costs bytes on a disk
    and nothing at run time; a **header entry** is compiled into a binary and
    costs every build whether anything draws it or not. Nothing selects a
    figure yet, so the second figure reaches the profile trees and stops there.
    No ROM moves, and the wave that makes a figure selectable pays the console
    cost together with the selection that earns it.
    """
    return asset.metadata.get(
        "figure", figures.DEFAULT_FIGURE.name) == figures.DEFAULT_FIGURE.name


def native_assets() -> List[Asset]:
    """Render every source asset once, at the native 32-pixel grid."""
    assets: List[Asset] = []

    for theme in themes.THEMES:
        suffix = themes.asset_suffix(theme)
        with terrain.rendering(theme):
            for name in terrain.TERRAIN_ORDER:
                variants = [terrain.base_tile(name, index)
                            for index in range(terrain.BASE_VARIANTS)]
                canvas, columns, rows = _sheet(variants, terrain.BASE_VARIANTS)
                assets.append(Asset(
                    f"terrain/{name}_base{suffix}.png", "terrain-base", canvas,
                    columns, rows,
                    {"terrain": name, "theme": theme.name,
                     "variants": terrain.BASE_VARIANTS},
                ))

                blob = [terrain.blob_tile(name, mask) for mask in autotile.BLOB_MASKS]
                canvas, columns, rows = _sheet(blob, BLOB_COLUMNS)
                assets.append(Asset(
                    f"terrain/{name}_blob{suffix}.png", "terrain-blob", canvas,
                    columns, rows,
                    {"terrain": name, "theme": theme.name,
                     "variants": len(autotile.BLOB_MASKS)},
                ))

            for name, under in terrain.TRANSITION_PAIRS:
                tiles = [terrain.blob_tile(name, mask, under)
                         for mask in autotile.BLOB_MASKS]
                canvas, columns, rows = _sheet(tiles, BLOB_COLUMNS)
                assets.append(Asset(
                    f"terrain/{name}_over_{under}{suffix}.png",
                    "terrain-transition", canvas, columns, rows,
                    {"terrain": name, "over": under, "theme": theme.name,
                     "variants": len(autotile.BLOB_MASKS)},
                ))

    for style in styles.STYLES:
        style_suffix = styles.asset_suffix(style)
        # The figure axis is the innermost loop rather than the outermost, so a
        # role's two figures land next to each other in every listing a reader
        # or a diff ever sees. The first figure's suffix is empty, so its files
        # are byte-for-byte the ones that shipped before this axis existed.
        for archetype, colour, shape in _character_keys():
            suffix = f"{style_suffix}{shape.suffix}"
            assets.append(Asset(
                f"characters/{archetype}_{colour.name}{suffix}.png",
                "character",
                styles.sprite(style, archetype, colour.name,
                              figure=shape.name), 1, 1,
                {"archetype": archetype, "faction_colour": colour.name,
                 "style": style.name, "figure": shape.name},
            ))
            # The animation cells, as one horizontal strip per sprite
            # rather than one file per cell. Three reasons, in the order
            # they were measured: a strip pays a PNG's ~150 bytes of header
            # once instead of three times; it is the sheet convention the
            # terrain sheets already publish, `columns` and all, so a client
            # and an external artist meet the same layout twice rather than
            # two layouts once; and it is one `mksprite` invocation and one
            # embedded symbol per sprite on the Nintendo 64 instead of
            # three. The standing sprite is frame 0 and is deliberately not
            # in the strip: its filename may not move.
            strip = Canvas(
                characters.SPRITE * frames.FRAME_COUNT, characters.SPRITE)
            for index, frame in enumerate(frames.FRAME_ORDER):
                strip.blit_opaque(
                    styles.sprite(style, archetype, colour.name,
                                  frame.name, shape.name),
                    index * characters.SPRITE, 0)
            assets.append(Asset(
                f"characters/{archetype}_{colour.name}{suffix}_frames.png",
                "character-frames", strip, frames.FRAME_COUNT, 1,
                {"archetype": archetype, "faction_colour": colour.name,
                 "style": style.name, "figure": shape.name},
            ))

    # One shadow for every archetype, every style and every faction: the
    # silhouettes differ above the ankles, not below them. Style-independent
    # and colour-independent, so it carries no suffix.
    assets.append(Asset(
        "characters/shadow.png", "shadow", characters.drop_shadow(), 1, 1,
        {"role": "drop-shadow"},
    ))

    map_canvas = scene.compose()
    assets.append(Asset(
        "sample_map.png", "scene", map_canvas,
        map_canvas.width // terrain.TILE, map_canvas.height // terrain.TILE,
        {"legend": {key: value for key, value in sorted(scene.LEGEND.items())}},
    ))
    return assets


def _terrain_manifest() -> Dict[str, object]:
    """The terrain registry: what a client needs to turn an authored terrain
    name into a sheet, a flat colour, and a mark, without a table of its own."""
    return {
        "match_order": list(terrain.KEYWORD_ORDER),
        "kinds": [
            {
                "name": name,
                "keywords": list(terrain.TERRAINS[name].keywords),
                "glyph": terrain.TERRAINS[name].glyph,
                "layer": terrain.TERRAINS[name].layer,
                "elevation": terrain.TERRAINS[name].elevation,
            }
            for name in terrain.KEYWORD_ORDER
        ],
    }


def _themes_manifest() -> Dict[str, object]:
    """The theme menu, in the order every client indexes it by."""
    return {
        "default": themes.DEFAULT_THEME.name,
        "menu": [
            {
                "name": theme.name,
                "label": theme.label,
                "summary": theme.summary,
                "suffix": themes.asset_suffix(theme),
                "terrain_colours": {
                    name: list(terrain.flat_colour(name, theme))
                    for name in terrain.KEYWORD_ORDER
                },
                # The master palette entries this theme's water shimmer
                # rotates, dark to light. Addresses, never pixels: the rotation
                # is presentation-side and the palette below is unpermuted.
                "water_cycle": list(shimmer.window_for(theme)),
            }
            for theme in themes.THEMES
        ],
    }


def _styles_manifest() -> Dict[str, object]:
    """The character style menu, in the order every client indexes it by.

    The animation vocabulary lives here rather than on each sequence sheet, and
    deliberately: it is a property of the library, identical for all 96 sheets,
    and repeating it on each of them cost more manifest bytes than the sheets
    themselves cost pixels.
    """
    return {
        "default": styles.DEFAULT_STYLE.name,
        "archetypes": list(characters.ARCHETYPE_ORDER),
        # The cells of every sequence sheet, in sheet order. A client indexes
        # by position; the names are for a reader. Frame 0 of a sequence is the
        # standing sprite and is not a cell of the sheet.
        "frames": {
            "sheet_suffix": "_frames",
            "cell_size": characters.SPRITE,
            "cells": [
                {"name": frame.name, "label": frame.label,
                 "animation": frame.animation, "summary": frame.summary}
                for frame in frames.FRAME_ORDER
            ],
            "animations": {
                name: list(cells)
                for name, cells in frames.ANIMATION_FRAMES.items()
            },
        },
        # The figures a role is drawn as, in menu order, published here for the
        # same reason the cells are: it is a property of the library, identical
        # for every style, and a client that has this table needs none of its
        # own to turn a role and a figure into a filename. Nothing selects a
        # figure yet; the menu exists before the choice does, exactly as the
        # style menu did.
        "figures": {
            "default": figures.DEFAULT_FIGURE.name,
            "menu": [
                {"name": shape.name, "label": shape.label,
                 "summary": shape.summary, "suffix": shape.suffix}
                for shape in figures.FIGURE_ORDER
            ],
        },
        "menu": [
            {
                "name": style.name,
                "label": style.label,
                "summary": style.summary,
                "suffix": styles.asset_suffix(style),
            }
            for style in styles.STYLES
        ],
    }


def _backdrops_manifest() -> Dict[str, object]:
    """The backdrop menu, in the order every client indexes it by.

    Colours rather than pixels, because that is what a backdrop is: see
    :mod:`.backdrops` for the measurement that decided it. Each entry carries
    the bands top to bottom as ``(top row, row count, master palette index)``
    and the RGB beside the index, so a client with no palette (the browser
    holds RGBA sheets) reads the same table as one that has.
    """
    return {
        "rows": backdrops.BACKDROP_ROWS,
        "max_bands": backdrops.MAX_BANDS,
        "text_colour": list(backdrops.TEXT_COLOUR),
        "min_contrast": backdrops.MIN_CONTRAST,
        "menu": [
            {
                "name": backdrop.name,
                "label": backdrop.label,
                "summary": backdrop.summary,
                "bands": [
                    {
                        "top": top,
                        "rows": rows,
                        "entry": entry,
                        "rgb": list(RGB[entry]),
                        # Measured rather than promised: the assertion in
                        # `backdrops.py` is what keeps it above the floor, and
                        # this is that number where a reader can see it.
                        "text_contrast": round(
                            backdrops.contrast(
                                RGB[entry], backdrops.TEXT_COLOUR), 2),
                    }
                    for top, rows, entry in backdrops.band_rows(backdrop)
                ],
            }
            for backdrop in backdrops.BACKDROPS
        ],
    }


def _backdrops_header() -> str:
    """The backdrop menu as a C++ header.

    Bands rather than pixels, so the whole library costs a console a table of
    a few dozen bytes and no VRAM, no TMEM and no tile at all. A client draws
    band ``i`` from ``round(top * height / rows)`` to
    ``round((top + count) * height / rows)`` of whatever height its scene has.
    """
    menu = backdrops.BACKDROPS
    widest = max(len(backdrop.bands) for backdrop in menu)
    lines: List[str] = [
        "// Generated by tools/placeholder_art/generate.py from"
        " placeholder_art/backdrops.py.",
        "// Do not edit.",
        "#pragma once",
        "// What a conversation between maps is drawn against: an ordered run"
        " of flat",
        "// horizontal bands, top to bottom, in master palette entries. No"
        " pixels, so a",
        "// backdrop costs no tile, no TMEM and no VRAM on any client: the"
        " same picture",
        "// as CI4 texels would be 38,400 bytes against the Nintendo 64's"
        " 2,048 usable.",
        "// Presentation only: no rule reads it, it never enters canonical"
        " state, and a",
        "// scene that names none is drawn exactly as it was before this menu"
        " existed.",
        f"inline constexpr int grandleon_backdrop_count = {len(menu)};",
        "inline constexpr const char* grandleon_backdrop_names[] = {"
        + ",".join('"%s"' % backdrop.name for backdrop in menu) + "};",
        "// The rows a backdrop divides a scene into: the coarsest grid any"
        " client",
        "// lays a scene out on, which every client scales to its own height.",
        "inline constexpr int grandleon_backdrop_rows = "
        f"{backdrops.BACKDROP_ROWS};",
        "// How many bands each backdrop actually names, of the"
        f" {widest} the widest one does.",
        f"inline constexpr int grandleon_backdrop_band_count[{len(menu)}] = {{"
        + ",".join(str(len(backdrop.bands)) for backdrop in menu) + "};",
        "// Per backdrop, per band: the first row, the row count, and the"
        " colour, as",
        "// red, green and blue. Bands past the backdrop's own count are"
        " zeroes.",
        f"inline constexpr unsigned char grandleon_backdrop_bands[{len(menu)}]"
        f"[{widest}][5] = {{",
    ]
    rows: List[str] = []
    for backdrop in menu:
        cells = []
        for top, count, entry in backdrops.band_rows(backdrop):
            red, green, blue = RGB[entry]
            cells.append(f"{{{top},{count},{red},{green},{blue}}}")
        cells.extend(["{0,0,0,0,0}"] * (widest - len(backdrop.bands)))
        rows.append("{" + ",".join(cells) + "}")
    lines.append(",".join(rows) + "};")
    return "\n".join(lines) + "\n"


def _styles_header() -> str:
    """The character style and figure menus as a C++ header.

    Names only. Unlike the theme header there is no per-style table to carry:
    a style and a figure select which character sheets a build embeds, and the
    console binds that at build time, so the ROM never indexes either menu at
    run time. It exists so the native reader's menus can be asserted against
    the generator's rather than trusted to match them.
    """
    return "\n".join([
        "// Generated by tools/placeholder_art/generate.py from"
        " placeholder_art/styles.py.",
        "// Do not edit.",
        "#pragma once",
        f"inline constexpr int grandleon_character_style_count = {len(styles.STYLES)};",
        "inline constexpr const char* grandleon_character_style_names[] = {"
        + ",".join('"%s"' % style.name for style in styles.STYLES) + "};",
        "// The archetype roster, shared by every style and closed at this"
        " length: a",
        "// ninth would cost one draw routine in every style, forever. A style"
        " may not",
        "// add, remove or rename one.",
        "inline constexpr int grandleon_archetype_count = "
        f"{len(characters.ARCHETYPE_ORDER)};",
        "inline constexpr const char* grandleon_archetype_names[] = {"
        + ",".join('"%s"' % name for name in characters.ARCHETYPE_ORDER) + "};",
        "// The bodies a role can be drawn with, in the order every client"
        " indexes",
        "// them by (placeholder_art/figures.py). Appended to, never inserted"
        " into:",
        "// the first entry is the sprite that shipped before figures existed.",
        f"inline constexpr int grandleon_character_figure_count = "
        f"{figures.FIGURE_COUNT};",
        "inline constexpr const char* grandleon_character_figure_names[] = {"
        + ",".join('"%s"' % name for name in figures.FIGURE_NAMES) + "};",
    ]) + "\n"


def _themes_header(subset_slots: Dict[str, Tuple[int, ...]]) -> str:
    """The theme menu and terrain registry as a C++ header.

    Small on purpose: it carries names, flat colours, and the palette
    substitution a theme performs, which is everything a client needs to
    resolve a theme. It carries no pixels, so the Nintendo 64 can include it.
    The art itself reaches that client as the CI4 sheets its build embeds.

    ``subset_slots`` maps a theme name to the water shimmer's slots inside that
    theme's water base sheet's own palette, measured off the subset profile
    rather than assumed, so a sheet whose palette ever stopped holding the whole
    water ramp would fail the build instead of shimmering the wrong colours.
    """
    lines: List[str] = [
        "// Generated by tools/placeholder_art/generate.py from"
        " placeholder_art/themes.py.",
        "// Do not edit.",
        "#pragma once",
        f"inline constexpr int grandleon_theme_count = {len(themes.THEMES)};",
        "inline constexpr const char* grandleon_theme_names[] = {"
        + ",".join('"%s"' % theme.name for theme in themes.THEMES) + "};",
        "inline constexpr int grandleon_terrain_kind_count = "
        f"{len(terrain.KEYWORD_ORDER)};",
        "// Terrain kinds in keyword match order, with the keywords an authored"
        " terrain",
        "// name may contain to select each one. The first match wins.",
        "inline constexpr const char* grandleon_terrain_kind_names[] = {"
        + ",".join('"%s"' % name for name in terrain.KEYWORD_ORDER) + "};",
        "inline constexpr int grandleon_terrain_keyword_count = "
        f"{max(len(terrain.TERRAINS[name].keywords) for name in terrain.KEYWORD_ORDER)};",
        "inline constexpr const char* grandleon_terrain_keywords[]["
        f"{max(len(terrain.TERRAINS[name].keywords) for name in terrain.KEYWORD_ORDER)}] = {{"
        + ",".join(
            "{%s}" % ",".join('"%s"' % keyword
                              for keyword in terrain.TERRAINS[name].keywords)
            for name in terrain.KEYWORD_ORDER
        ) + "};",
        "// How many levels above the valley floor each terrain kind reads as."
        " A client",
        "// draws the cell's art lifted by this many steps and changes nothing"
        " else:",
        "// presentation only, never state, never hashed. A client that",
        "// ignores it draws the flat board.",
        "inline constexpr unsigned char grandleon_terrain_elevation"
        f"[{len(terrain.KEYWORD_ORDER)}] = {{"
        + ",".join(
            str(terrain.TERRAINS[name].elevation)
            for name in terrain.KEYWORD_ORDER
        ) + "};",
        "// The flat colour each terrain kind is drawn in, per theme: what a"
        " client",
        "// paints when it is painting cells rather than tiles.",
        "inline constexpr unsigned char grandleon_terrain_flat"
        f"[{len(themes.THEMES)}][{len(terrain.KEYWORD_ORDER)}][3] = {{"
        + ",".join(
            "{%s}" % ",".join(
                "{%d,%d,%d}" % terrain.flat_colour(name, theme)
                for name in terrain.KEYWORD_ORDER
            )
            for theme in themes.THEMES
        ) + "};",
        "// Master palette index to the index a theme paints in its place, for"
        " a client",
        "// holding art in master-palette indices. The default theme is the"
        " identity.",
        "inline constexpr unsigned char grandleon_theme_palette"
        f"[{len(themes.THEMES)}][{PALETTE_SIZE}] = {{"
        + ",".join(
            "{%s}" % ",".join(str(index) for index in _theme_palette(theme))
            for theme in themes.THEMES
        ) + "};",
        "// Animated water, presentation-side only"
        " (placeholder_art/shimmer.py). These are",
        "// addresses a client rotates in the palette it loaded, dark to light;"
        " the",
        "// master palette above is unpermuted and every asset byte is what it"
        " always",
        "// was. The phase is timing and lives in view/motion.hpp.",
        "inline constexpr int grandleon_water_cycle_entries = "
        f"{shimmer.CYCLE_ENTRIES};",
        "// Master palette indices, for a client holding art in them.",
        "inline constexpr unsigned char grandleon_water_cycle_master"
        f"[{len(themes.THEMES)}][{shimmer.CYCLE_ENTRIES}] = {{"
        + ",".join(
            "{%s}" % ",".join(str(index) for index in shimmer.window_for(theme))
            for theme in themes.THEMES
        ) + "};",
        "// Slots in the water base sheet's own palette, for a client whose"
        " hardware",
        "// holds one lookup table per texture — the Nintendo 64's CI4 TLUT.",
        "inline constexpr unsigned char grandleon_water_cycle_tlut"
        f"[{len(themes.THEMES)}][{shimmer.CYCLE_ENTRIES}] = {{"
        + ",".join(
            "{%s}" % ",".join(str(slot) for slot in subset_slots[theme.name])
            for theme in themes.THEMES
        ) + "};",
    ]
    return "\n".join(lines) + "\n"


def _theme_palette(theme: themes.Theme) -> List[int]:
    """Every master palette index, substituted as ``theme`` substitutes it."""
    mapping = list(range(PALETTE_SIZE))
    for original, replacement in theme.substitutions.items():
        for position, index in enumerate(palette_ramp(original)):
            mapping[index] = palette_ramp(replacement)[position]
    return mapping


def _autotile_manifest() -> Dict[str, object]:
    return {
        "convention": "blob-47",
        "description": (
            "Eight-bit neighbour mask; a bit is set when that neighbour holds "
            "the same terrain. Diagonal bits are cleared unless both adjoining "
            "cardinal bits are set, collapsing 256 masks onto 47 variants."
        ),
        "bits": {"N": 1, "NE": 2, "E": 4, "SE": 8, "S": 16, "SW": 32, "W": 64,
                 "NW": 128},
        "variant_count": len(autotile.BLOB_MASKS),
        "variant_masks": list(autotile.BLOB_MASKS),
        "mask_to_variant": list(autotile.MASK_TO_VARIANT),
        "interior_variant": autotile.INTERIOR_VARIANT,
        "sheet_columns": BLOB_COLUMNS,
    }


def _palette_usage(assets: List[Asset]) -> Dict[str, object]:
    """Which master palette entries each drawing actually spends.

    Measured off the native canvases, which hold master palette indices
    directly, so this is a count of the art rather than of any one profile's
    re-encoding of it. The numbers are the same in every profile, which is why
    it is written once beside the headers instead of into each manifest.

    It exists because a console's budget is spent by an authored game, and the
    quantity a game spends is which drawings it uses. A client holding this
    table can add up one game's colours without holding a pixel.

    Two rosters, because those are the two an author chooses from: characters,
    keyed by the style, figure, archetype and faction colour that select a
    sprite; and terrain, keyed by theme and terrain kind. The terrain rows are
    the *base* sheets, the four-variant sheets a console keeps resident, where
    the 47-variant blob sheets are too large to stay resident. A console budget
    counted against them is counted against what a console would carry.

    The transparent entry is included in every row, because it occupies a slot
    in a hardware palette exactly as an opaque colour does.

    Why the figure is in the key
    ----------------------------
    A character row is **one drawing's own entries**, and the figure is part of
    what names that drawing.

    Keying the row without the figure (one row, the first figure's alone)
    would hold only if a figure were a transform, because a transform moves an
    index the drawn body already spent and a second figure's row could then only
    be a subset of the first's. That argument fails for a drawing: a second
    figure drawn rather than displaced may spend a darker step of a ramp its
    first figure used two steps of, and `medieval`'s do. The best a figure-less
    key can carry is the union across figures, and a union is a bound rather
    than an answer.

    A figure is selectable through `characterFigureId`, on the project and on
    each character, so the key carries it and the row is exact. A consumer resolves
    the figure it draws exactly as the compiler and every console do, asks for
    that drawing, and is answered with what that drawing spends. That is
    stronger than the union in both directions at once: it can no more
    under-count a game that fields a second figure (it prices the second figure)
    than it can over-count one that does not (it does not price a drawing the
    game never puts on screen). The console side reads it the same way:
    `grandleon_require_single_character_combination` refuses a console project
    drawing more than one style-and-figure pair, so a cartridge carries one
    figure's roster and never both.
    """
    characters: Dict[str, List[int]] = {}
    terrain_base: Dict[str, Dict[str, List[int]]] = {}
    for asset in assets:
        entries = sorted({int(index) for index in asset.canvas.data})
        if asset.kind == "character":
            key = (f"{asset.metadata['style']}_{asset.metadata['figure']}"
                   f"_{asset.metadata['archetype']}"
                   f"_{asset.metadata['faction_colour']}")
            characters[key] = entries
        elif asset.kind == "terrain-base":
            theme = str(asset.metadata["theme"])
            terrain_base.setdefault(theme, {})[
                str(asset.metadata["terrain"])] = entries
    characters = dict(sorted(characters.items()))
    return {
        "generator": "tools/placeholder_art/generate.py",
        "measured": (
            "Master palette entries used by each native drawing, including the "
            "transparent entry. Profile-independent: measured before any "
            "profile converts the art."
        ),
        "characters": characters,
        "terrain_base": terrain_base,
    }


def _palette_swatch(colours: Tuple[Tuple[int, int, int], ...], swatch: int = 8
                    ) -> Tuple[int, int, List[Tuple[int, int, int, int]]]:
    columns = 8
    rows = (len(colours) + columns - 1) // columns
    width, height = columns * swatch, rows * swatch
    pixels = [(0, 0, 0, 0)] * (width * height)
    for index, colour in enumerate(colours):
        left = (index % columns) * swatch
        top = (index // columns) * swatch
        for y in range(top, top + swatch):
            for x in range(left, left + swatch):
                pixels[y * width + x] = (colour[0], colour[1], colour[2], 255)
    return width, height, pixels


def substitute(assets: List[Asset],
               replacements: Mapping[str, provided.Replacement]
               ) -> Tuple[List[Asset], Dict[str, provided.Replacement]]:
    """Swap in every provided canvas. Returns the assets and what was used.

    This is the whole of where a replacement enters the pipeline, and it is one
    statement deep on purpose: the substitution happens on the **native canvas**,
    before the first profile has touched it, so a provided sheet reaches the five
    profile trees, the PlayStation header, the Nintendo 64's CI4
    sheets that ``mksprite`` reads at configure time, the palette usage table,
    the gallery and the editor's board copies by exactly the route the generated
    one reaches them. Nothing downstream learns that anything was replaced, and
    nothing downstream had to be told.

    A key that names no asset cannot arrive here: :func:`.provided.read` refuses
    it at the door. The assertion is kept anyway, because the two tables are
    built from the same registries and a silent miss would be a replacement that
    validated and never drew.
    """
    used: Dict[str, provided.Replacement] = {}
    out: List[Asset] = []
    for asset in assets:
        replacement = replacements.get(asset.path)
        if replacement is None:
            out.append(asset)
            continue
        used[asset.path] = replacement
        out.append(Asset(asset.path, asset.kind, replacement.canvas,
                         asset.columns, asset.rows, dict(asset.metadata)))
    missing = sorted(set(replacements) - set(used))
    if missing:
        raise verify.VerificationError(
            "provided art names assets this build does not generate: "
            + ", ".join(missing))
    return out, used


def build(output: Path, quiet: bool = False,
          replacements: Optional[Mapping[str, provided.Replacement]] = None
          ) -> Dict[str, object]:
    """Render, verify, and write every asset, the manifests, and the gallery.

    ``replacements`` is what :func:`.provided.read` accepted, keyed by the
    manifest path each one stands in for. Passing none is the default and is
    byte-for-byte the build this repository has always run.

    Sheets go through :func:`substitute` onto the native canvas list. That seam
    is one statement deep and sits in front of everything, so a provided drawing
    reaches every profile, every console header and the roster page by exactly
    the route a generated one reaches them.
    """
    accepted = replacements or {}
    return _build(output, quiet, accepted)


def _reduces_as_sprite(asset: Asset) -> bool:
    """Whether a profile should reduce this asset the way it reduces a unit.

    Sprites reduce differently from tiles: they keep their outline and snap
    rather than dither. The shadow is a sprite by the same argument: it is
    blitted over terrain, not tiled into it.
    """
    return asset.kind in ("character", "character-frames", "shadow")


def _build(output: Path, quiet: bool,
           replacements: Mapping[str, provided.Replacement]
           ) -> Dict[str, object]:
    """The build itself."""
    assets = native_assets()
    assets, replaced = substitute(assets,
                                  provided.canvas_replacements(replacements))
    verify.check_native()

    summary: Dict[str, object] = {"files": 0, "profiles": {}, "skipped": []}
    # A question about the drawing routines rather than about any profile's
    # pixels, so it is asked here, before a profile has touched anything: do the
    # two figures of every role carry one kit?
    summary.update(verify.check_kit_shared())
    converted_by_profile: Dict[str, Dict[str, profiles.Converted]] = {}
    #: Theme name to the water shimmer's slots inside that theme's water base
    #: sheet's own palette, measured off the subset profile the Nintendo 64
    #: reads rather than assumed.
    water_cycle_slots: Dict[str, Tuple[int, ...]] = {}

    for profile in profiles.PROFILES:
        root = output / "assets" / profile.name
        entries: List[Dict[str, object]] = []
        palettes: Dict[str, object] = {}
        converted_assets: Dict[str, profiles.Converted] = {}

        for asset in assets:
            converted = profiles.convert(asset.canvas, profile,
                                         is_sprite=_reduces_as_sprite(asset))
            converted_assets[asset.path] = converted
            pngio.write(root / asset.path, profiles.encode(converted, profile))
            summary["files"] = int(summary["files"]) + 1
            entry: Dict[str, object] = {
                "path": asset.path,
                "kind": asset.kind,
                "width": converted.width,
                "height": converted.height,
                "columns": asset.columns,
                "rows": asset.rows,
                "tile_width": converted.width // asset.columns,
                "tile_height": converted.height // asset.rows,
            }
            entry.update(asset.metadata)
            # Written only for an asset that was actually replaced. A project
            # that provides nothing gets the manifest it always got, byte for
            # byte, which is why this is a key that appears rather than a key
            # whose value is false.
            if asset.path in replaced:
                entry["provided"] = True
            entries.append(entry)
            if profile.palette_mode == "subset":
                palettes[asset.path] = [list(colour) for colour in converted.colours]
                if (asset.kind == "terrain-base"
                        and asset.metadata.get("terrain") == shimmer.CYCLED_RAMP):
                    water_cycle_slots[str(asset.metadata["theme"])] = (
                        shimmer.subset_window(
                            themes.THEMES_BY_NAME[str(asset.metadata["theme"])],
                            converted.colours))

        converted_by_profile[profile.name] = converted_assets
        summary["skipped"].extend(  # type: ignore[union-attr]
            verify.check_profile(profile, converted_assets))

        shared = profiles.palette_of(profile)
        if shared is not None:
            palettes = {"shared": [list(colour) for colour in shared]}
            width, height, pixels = _palette_swatch(shared)
            pngio.write(root / "palette.png", pngio.encode_rgba(width, height, pixels))
            summary["files"] = int(summary["files"]) + 1

        manifest = {
            "generator": "tools/placeholder_art/generate.py",
            "profile": {
                "name": profile.name,
                "label": profile.label,
                "tile_size": profile.tile_size,
                "sprite_size": profile.sprite_size,
                "encoding": profile.encoding,
                "bit_depth": profile.bit_depth,
                "max_colours": profile.max_colours,
                "palette_mode": profile.palette_mode,
                "notes": profile.notes,
            },
            "master_palette": {
                "size": PALETTE_SIZE,
                "transparent_index": TRANSPARENT,
                "colours": [list(colour) for colour in RGB],
            },
            "autotile": _autotile_manifest(),
            "terrain": _terrain_manifest(),
            "themes": _themes_manifest(),
            "character_styles": _styles_manifest(),
            "backdrops": _backdrops_manifest(),
            "assets": entries,
        }
        # Which key each provided file stands in for, with the digest of the
        # bytes that were read. Provided art lives outside the tree the
        # generator owns, so that `--check` can stay a byte-for-byte assertion
        # over a whole regenerated tree; this manifest is therefore the only
        # record of a replacement anywhere in the output. The art itself is
        # indistinguishable
        # from generated art by design, because a client that had to tell them
        # apart would be a client a replacement could break.
        # The record of what stood in for what is the one place a replacement
        # is visible at all.
        if replacements:
            manifest["provided"] = provided.manifest_block(replacements)
        palettes_document: Dict[str, object] = {
            "profile": profile.name, "mode": profile.palette_mode,
            "palettes": palettes,
        }
        _write_json(root / "manifest.json", manifest)
        _write_json(root / "palettes.json", palettes_document)
        summary["files"] = int(summary["files"]) + 2
        summary["profiles"][profile.name] = len(entries)  # type: ignore[index]
        if not quiet:
            print(f"  {profile.name}: {len(entries)} assets")

    # The legibility reduction: half size and four tones, measured and never
    # written. It is deliberately outside `profiles.PROFILES`, so it gets no
    # `assets/` tree, no manifest and no palette file. The pass below and the
    # gallery's figure pages are the whole of what reads it.
    converted_by_profile[profiles.LEGIBILITY.name] = {
        asset.path: profiles.convert(asset.canvas, profiles.LEGIBILITY,
                                     is_sprite=_reduces_as_sprite(asset))
        for asset in assets
    }

    # The legibility pass, which is the one check here that reads across two
    # sprites rather than into one. It runs once rather than per profile because
    # it is not a question about a profile: it measures on the one reduction
    # above, and the comparability of seven styles' floors rests on that
    # reduction never changing.
    summary["legibility"] = verify.check_legibility(
        {asset.path: asset.canvas for asset in assets},
        converted_by_profile[profiles.LEGIBILITY.name],
        sorted(replaced))

    (output / "assets" / "themes.h").write_text(
        _themes_header(water_cycle_slots), encoding="utf-8")
    (output / "assets" / "styles.h").write_text(_styles_header(), encoding="utf-8")
    (output / "assets" / "backdrops.h").write_text(
        _backdrops_header(), encoding="utf-8")
    _write_json(output / "assets" / "palette_usage.json", _palette_usage(assets))
    summary["files"] = int(summary["files"]) + 4

    # The PlayStation, from the Nintendo 64's own profile and no other. Its 4bpp
    # per-asset 16-colour subsets already are what a PlayStation texture page
    # and CLUT want, and the master palette survives 15-bit colour with nothing
    # merged, so there is nothing to re-quantise and no profile to add. What
    # there is to do is repack: the leftmost texel moves from the high nibble to
    # the low one, and the palette becomes halfwords. This is where that
    # happens, for the reason the header above exists.
    source = playstation_header.SOURCE_PROFILE
    (output / "assets" / "playstation.h").write_text(
        playstation_header.emit(converted_by_profile[source],
                                profiles.PROFILES_BY_NAME[source]),
        encoding="utf-8",
    )
    for style in styles.STYLES:
        (output / "assets"
         / playstation_header.characters_header_name(style)).write_text(
            playstation_header.emit_characters(
                style, converted_by_profile[source],
                profiles.PROFILES_BY_NAME[source]),
            encoding="utf-8",
        )
    summary["files"] = int(summary["files"]) + 1 + len(styles.STYLES)

    blobs = [asset for asset in assets
             if asset.kind == "terrain-blob"
             and asset.metadata["theme"] == themes.DEFAULT_THEME.name]
    # The embedding clients that read this header carry no style choice yet, so
    # it holds the default style's roster, exactly as the blob rows above hold
    # the default theme's, and the default figure's, for the same reason and
    # by the same seam (:func:`_is_default_figure`).
    roster = [asset for asset in assets
              if asset.kind == "character"
              and asset.metadata["style"] == styles.DEFAULT_STYLE.name
              and _is_default_figure(asset)]
    (output / "assets" / "sprites.h").write_text(
        sprites_header.emit(
            [str(asset.metadata["terrain"]) for asset in blobs],
            [asset.canvas for asset in blobs],
            [f"{asset.metadata['archetype']}_{asset.metadata['faction_colour']}"
             for asset in roster],
            [asset.canvas for asset in roster],
            next(asset.canvas for asset in assets if asset.kind == "shadow"),
        ),
        encoding="utf-8",
    )
    summary["files"] = int(summary["files"]) + 1

    logo_width, logo_height, logo_pixels = logo.rendered()
    pngio.write(
        output / "gallery" / "logo.png",
        pngio.encode_rgba(logo_width, logo_height, logo_pixels),
    )
    (output / "assets" / "logo.h").write_text(logo.header(), encoding="utf-8")
    summary["files"] = int(summary["files"]) + 2

    written = gallery.write(output, assets, converted_by_profile)
    summary["files"] = int(summary["files"]) + written
    return summary


def _write_json(path: Path, payload: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")
