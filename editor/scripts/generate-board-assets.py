#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Pipes the generated board art from tools/placeholder_art into the editor.

The source of truth is ``tools/placeholder_art/assets/modern``, the
full-colour profile of the checked-in placeholder art. This script copies the
sprites the tactical board draws into ``editor/public/board/`` and derives
``editor/src/generated/board-art.ts`` from the profile's ``manifest.json``, so
the board applies the generator's own autotile convention rather than
re-deriving it. Everything it writes is committed, so the editor builds
without Python, exactly like the logo assets from
``generate-logo-assets.py``.

What is copied, byte for byte:

- one 47-variant blob sheet per terrain the board can name, in every theme the
  library offers;
- one 32x32 sprite per character archetype and faction colour, in every
  character style the library offers;
- the sequence strip beside each of those sprites, one row of animation cells
  in the fixed order `walk_contact`, `walk_pass`, `lunge`, `cast`; and
- the single drop shadow that grounds a raised character against the tile it
  stands on.

The editor holds every style because it opens arbitrary projects and resolves
the style one names at run time. A console build, which carries exactly one
project, instead binds the style at build time and embeds only that one.

It also carries the generator's terrain registry: which authored names select
which terrain, the flat colour of each in each theme, the mark that keeps
terrain identity off colour alone, and how far each kind stands above the
ground. The editor then applies the same selection rule the console does rather
than keeping a list of its own.

Alongside that it carries the master palette and, from
``tools/placeholder_art/assets/palette_usage.json``, which of its entries each
drawing spends. The editor needs those to tell an author what one authored game
costs a console, and measuring them belongs to the generator that drew the art:
a hand-written table here would be a transcription that could drift.

Run from anywhere: ``python3 editor/scripts/generate-board-assets.py``.
``--check`` regenerates into a temporary directory and fails on any byte
difference from the committed output, mirroring ``generate.py --check``.
"""

from __future__ import annotations

import filecmp
import json
import shutil
import sys
import tempfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
SOURCE = REPOSITORY / "tools" / "placeholder_art" / "assets" / "modern"
EDITOR = REPOSITORY / "editor"


def hex_colour(colour: list[int]) -> str:
    return "#" + "".join(f"{channel:02x}" for channel in colour)


def entry_list(entries: list[int]) -> str:
    return "[" + ", ".join(str(entry) for entry in entries) + "]"


def water_ramps(manifest: dict, usage: dict) -> dict[str, list[list[int]]]:
    """Every colour a theme's water is drawn in, dark to light.

    The browser has no palette to rotate, holding RGBA sheets, so where the
    consoles are handed *addresses* (``grandleon_water_cycle_tlut``, the Mega
    Drive's colour-RAM entries) it has to be handed the colours themselves.
    They are read rather than restated: the ramp is the master entries the
    generator measured the water base sheet spending, and the four the shimmer
    rotates are the manifest's own ``water_cycle``. This asserts that the four
    are the light end of the ramp, so a theme whose water stopped spending the
    whole ramp fails here exactly as it fails the art build.
    """
    palette = manifest["master_palette"]["colours"]
    ramps: dict[str, list[list[int]]] = {}
    for theme in manifest["themes"]["menu"]:
        name = theme["name"]
        ramp = list(usage["terrain_base"][name]["water"])
        window = list(theme["water_cycle"])
        if ramp[-len(window):] != window:
            raise SystemExit(
                f"{name}: the shimmer rotates {window}, which is not the light "
                f"end of the water ramp {ramp}"
            )
        colours = [list(palette[index]) for index in ramp]
        for channel in range(3):
            values = [colour[channel] for colour in colours]
            if len(set(values)) != len(values):
                raise SystemExit(
                    f"{name}: two steps of the water ramp share a value in "
                    f"channel {channel} ({values}); the browser rotates the "
                    f"ramp with a per-channel transfer table and cannot tell "
                    f"them apart"
                )
        ramps[name] = colours
    return ramps


def emit_module(manifest: dict, usage: dict,
                terrains: list[str], characters: list[str]) -> str:
    autotile = manifest["autotile"]
    profile = manifest["profile"]
    registry = manifest["terrain"]
    themes = manifest["themes"]
    styles = manifest["character_styles"]
    backdrops = manifest["backdrops"]
    mask_table = ",".join(str(value) for value in autotile["mask_to_variant"])
    kind_entries = ",\n".join(f'  "{kind["name"]}"' for kind in registry["kinds"])
    keyword_entries = ",\n".join(
        f'  {kind["name"]}: [{", ".join(chr(34) + word + chr(34) for word in kind["keywords"])}]'
        for kind in registry["kinds"]
    )
    glyph_entries = ",\n".join(
        f'  {kind["name"]}: {json.dumps(kind["glyph"])}' for kind in registry["kinds"]
    )
    elevation_entries = ",\n".join(
        f'  {kind["name"]}: {kind["elevation"]}' for kind in registry["kinds"]
    )
    theme_entries = ",\n".join(
        f'  {{ id: "{theme["name"]}", label: {json.dumps(theme["label"])}, '
        f'summary: {json.dumps(theme["summary"])} }}'
        for theme in themes["menu"]
    )
    colour_entries = ",\n".join(
        "  {}: {{\n{}\n  }}".format(
            theme["name"],
            ",\n".join(
                f'    {name}: "{hex_colour(colour)}"'
                for name, colour in sorted(theme["terrain_colours"].items())
            ),
        )
        for theme in themes["menu"]
    )
    sheet_entries = ",\n".join(
        "  {}: {{\n{}\n  }}".format(
            theme["name"],
            ",\n".join(
                f'    {name}: "board/terrain/{name}_blob{theme["suffix"]}.png"'
                for name in terrains
            ),
        )
        for theme in themes["menu"]
    )
    style_entries = ",\n".join(
        f'  {{ id: "{style["name"]}", label: {json.dumps(style["label"])}, '
        f'summary: {json.dumps(style["summary"])} }}'
        for style in styles["menu"]
    )
    archetype_entries = ",\n".join(
        f'  "{name}"' for name in styles["archetypes"]
    )
    backdrop_entries = ",\n".join(
        "  {{ id: \"{}\", label: {}, summary: {},\n"
        "    bands: [{}] }}".format(
            backdrop["name"],
            json.dumps(backdrop["label"]),
            json.dumps(backdrop["summary"]),
            ", ".join(
                '{{ top: {}, rows: {}, color: "{}" }}'.format(
                    band["top"], band["rows"], hex_colour(band["rgb"]))
                for band in backdrop["bands"]
            ),
        )
        for backdrop in backdrops["menu"]
    )
    figures = styles["figures"]
    figure_entries = ",\n".join(
        "  {{ id: \"{}\", label: {}, summary: {} }}".format(
            figure["name"],
            json.dumps(figure["label"]),
            json.dumps(figure["summary"]),
        )
        for figure in figures["menu"]
    )
    sprite_entries = ",\n".join(
        f'  {style["name"]}_{figure["name"]}_{name}: '
        f'"board/characters/{name}{style["suffix"]}{figure["suffix"]}.png"'
        for style in styles["menu"]
        for figure in figures["menu"]
        for name in characters
    )
    master = manifest["master_palette"]
    master_entries = ",\n".join(
        "  [{}, {}, {}]".format(*colour) for colour in master["colours"]
    )
    character_usage = ",\n".join(
        f'  {key}: {entry_list(usage["characters"][key])}'
        for style in styles["menu"]
        for figure in figures["menu"]
        for key in [f'{style["name"]}_{figure["name"]}_{name}'
                    for name in characters]
    )
    terrain_usage = ",\n".join(
        "  {}: {{\n{}\n  }}".format(
            theme["name"],
            ",\n".join(
                f'    {name}: '
                f'{entry_list(usage["terrain_base"][theme["name"]][name])}'
                for name in terrains
            ),
        )
        for theme in themes["menu"]
    )
    frames = styles["frames"]
    frame_entries = ",\n".join(
        f'  {style["name"]}_{figure["name"]}_{name}: '
        f'"board/characters/{name}{style["suffix"]}{figure["suffix"]}'
        f'{frames["sheet_suffix"]}.png"'
        for style in styles["menu"]
        for figure in figures["menu"]
        for name in characters
    )
    cell_entries = ",\n".join(
        f'  {{ name: "{cell["name"]}", animation: "{cell["animation"]}", '
        f'label: {json.dumps(cell["label"])} }}'
        for cell in frames["cells"]
    )
    strip = next(
        entry for entry in manifest["assets"]
        if entry["kind"] == "character-frames"
    )
    ramps = water_ramps(manifest, usage)
    ramp_entries = ",\n".join(
        "  {}: [\n{}\n  ]".format(
            name,
            ",\n".join(
                "    [{}, {}, {}]".format(*colour) for colour in colours
            ),
        )
        for name, colours in ramps.items()
    )
    blob = next(
        entry for entry in manifest["assets"] if entry["kind"] == "terrain-blob"
    )
    shadow = next(
        entry for entry in manifest["assets"] if entry["kind"] == "shadow"
    )
    return f"""/*
 * Generated by editor/scripts/generate-board-assets.py from
 * tools/placeholder_art/assets/modern/manifest.json.
 * Do not edit this file; run the script to refresh it.
 */

export const TILE_SIZE = {profile["tile_size"]};
export const BLOB_SHEET_COLUMNS = {autotile["sheet_columns"]};
export const BLOB_SHEET_WIDTH = {blob["width"]};
export const BLOB_SHEET_HEIGHT = {blob["height"]};
export const INTERIOR_VARIANT = {autotile["interior_variant"]};

/** Raw eight-bit neighbour mask to blob sheet variant, {autotile["convention"]}. */
export const MASK_TO_VARIANT: readonly number[] = [{mask_table}];

/**
 * Terrain kinds in the order an authored name is matched against them: the
 * first kind with a keyword in the lowered name wins.
 */
export const TERRAIN_KINDS: readonly string[] = [
{kind_entries}
];

/** The words an authored terrain name may contain to select each kind. */
export const TERRAIN_KEYWORDS: Readonly<Record<string, readonly string[]>> = {{
{keyword_entries}
}};

/** One mark per kind, so terrain identity is never carried by colour alone. */
export const TERRAIN_GLYPHS: Readonly<Record<string, string>> = {{
{glyph_entries}
}};

/**
 * How many levels above the ground each kind stands. It is a drawing offset
 * and nothing else: no rule reads it, no snapshot carries it, and a kind at
 * zero draws exactly where it always did.
 */
export const TERRAIN_ELEVATION: Readonly<Record<string, number>> = {{
{elevation_entries}
}};

/** The theme menu, in the order every client indexes it by. */
export const THEMES: readonly {{ id: string; label: string; summary: string }}[] = [
{theme_entries}
];

/** The theme a project that names none is drawn in. */
export const DEFAULT_THEME = "{themes["default"]}";

/** The flat colour of each terrain kind, per theme. */
export const TERRAIN_COLORS:
  Readonly<Record<string, Readonly<Record<string, string>>>> = {{
{colour_entries}
}};

/** Blob sheet per theme and terrain, relative to the deployment base. */
export const TERRAIN_SHEETS:
  Readonly<Record<string, Readonly<Record<string, string>>>> = {{
{sheet_entries}
}};

/** The character style menu, in the order every client indexes it by. */
export const CHARACTER_STYLES:
  readonly {{ id: string; label: string; summary: string }}[] = [
{style_entries}
];

/** The style a project that names none is drawn in. */
export const DEFAULT_CHARACTER_STYLE = "{styles["default"]}";

/**
 * The figure menu, in the order every client indexes it by. A figure is the
 * body a role is drawn at rather than the role: every figure draws every
 * archetype in every style, so the two axes combine freely.
 */
export const CHARACTER_FIGURES:
  readonly {{ id: string; label: string; summary: string }}[] = [
{figure_entries}
];

/** The figure a project that names none is drawn with. */
export const DEFAULT_CHARACTER_FIGURE = "{figures["default"]}";

/**
 * The archetype roster, shared by every style: a class name resolves to the
 * same archetype whatever style draws it.
 */
export const ARCHETYPES: readonly string[] = [
{archetype_entries}
];

/**
 * How many rows a backdrop divides a scene into. The coarsest grid any client
 * lays a scene out on, so a band boundary exact on it is exact everywhere.
 */
export const BACKDROP_ROWS = {backdrops["rows"]};

/**
 * The scene backdrop menu, in the order every client indexes it by. A backdrop
 * is a run of flat horizontal bands from the top of the scene to the bottom
 * and nothing else: no pixels, so it costs no client a texture.
 *
 * There is no default: a scene that names no backdrop is drawn on whatever
 * flat fill its client already used, which is not an entry of this menu.
 */
export const BACKDROPS: readonly {{
  id: string;
  label: string;
  summary: string;
  bands: readonly {{ top: number; rows: number; color: string }}[];
}}[] = [
{backdrop_entries}
];

/**
 * Character sprite per style, figure, archetype and faction colour, keyed
 * `<style>_<figure>_<archetype>_<faction>` and relative to the deployment
 * base.
 */
export const CHARACTER_SPRITES: Readonly<Record<string, string>> = {{
{sprite_entries}
}};

/**
 * The sequence strip beside each character sprite, keyed the same way. One row
 * of {strip["columns"]} cells of {strip["tile_width"]}x{strip["tile_height"]}, in the order below; a client windows the
 * strip by cell position and never by name. The standing sprite above is frame
 * 0 of every sequence and is deliberately not in the strip: it is drawn
 * whenever a unit is at rest, so duplicating it into every sequence would cost
 * every console a cell it already holds.
 */
export const CHARACTER_FRAME_SHEETS: Readonly<Record<string, string>> = {{
{frame_entries}
}};

/** The strip's own pixel size, so a client can window it without measuring. */
export const SEQUENCE_SHEET_WIDTH = {strip["width"]};
export const SEQUENCE_SHEET_HEIGHT = {strip["height"]};

/**
 * The cells of a sequence, in the one order every sheet in the library ships
 * them in. Position is the contract: a sheet missing a cell does not lose one
 * animation, it shifts every later one.
 */
export const SEQUENCE_CELLS:
  readonly {{ name: string; animation: string; label: string }}[] = [
{cell_entries}
];

/**
 * Every colour a theme's water is drawn in, dark to light, as red, green and
 * blue in 0-255.
 *
 * This is the browser's coordinate system for the shimmer. The consoles are
 * handed *addresses*, a slot in the Nintendo 64's lookup table, because they
 * hold indexed art and can permute the palette they loaded. The browser holds
 * RGBA sheets, so it is handed the colours and permutes them at draw time
 * instead. The four at the light end
 * are the ones that rotate; the darkest is the anchor that does not move,
 * because rotating all five of a five-step ramp reads as a strobe.
 */
export const WATER_RAMPS:
  Readonly<Record<string, readonly (readonly [number, number, number])[]>> = {{
{ramp_entries}
}};

/**
 * The drop shadow drawn under a character, relative to the deployment base.
 * One sprite serves every archetype, style and colour: it grounds the
 * billboard against its tile, so it belongs to the tile rather than to
 * whoever is standing on it.
 */
export const SHADOW_SPRITE = "board/{shadow["path"]}";

/**
 * The master palette every drawing in the library is made of, as red, green
 * and blue in 0-255. Entry {master["transparent_index"]} is transparent. A client that has to ask what
 * a colour becomes on a machine with less colour depth than this quantises
 * these values; nothing else in the editor reads them.
 */
export const MASTER_PALETTE:
  readonly (readonly [number, number, number])[] = [
{master_entries}
];

/**
 * Which master palette entries one character sprite spends, keyed
 * `<style>_<figure>_<archetype>_<faction>`, transparent entry included.
 * Measured from the native drawing before any profile converts it, so it is
 * what the art costs rather than what one encoding of it costs.
 *
 * The same four keys `CHARACTER_SPRITES` is keyed by, and deliberately: a
 * budget prices the drawing a game puts on screen, so it has to be able to
 * name the same drawing the board draws.
 */
export const CHARACTER_PALETTE_ENTRIES:
  Readonly<Record<string, readonly number[]>> = {{
{character_usage}
}};

/**
 * The same, per theme and terrain kind, from the four-variant base sheets,
 * the sheets a console keeps resident. The 47-variant blob sheets the editor
 * draws are the same colours in more arrangements, so a budget counted here is
 * counted against what a console would actually carry.
 */
export const TERRAIN_PALETTE_ENTRIES:
  Readonly<Record<string, Readonly<Record<string, readonly number[]>>>> = {{
{terrain_usage}
}};

"""


def generate(editor_root: Path) -> None:
    manifest = json.loads((SOURCE / "manifest.json").read_text(encoding="utf-8"))
    usage = json.loads(
        (SOURCE.parent / "palette_usage.json").read_text(encoding="utf-8")
    )
    themes = manifest["themes"]["menu"]
    terrains = sorted(
        entry["terrain"] for entry in manifest["assets"]
        if entry["kind"] == "terrain-blob"
        and entry["theme"] == manifest["themes"]["default"]
    )
    styles = manifest["character_styles"]["menu"]
    # Every style holds every archetype in every faction colour, so one style's
    # rows name the whole grid and the style dimension multiplies it.
    #
    # The figure axis is a second multiplier, because the source schema selects
    # one: a character may name a figure of its own, so the editor has to be
    # able to draw both rather than filtering the second figure out.
    #
    # `characters` stays the role grid, archetype and faction colour, because
    # the style and the figure are the two dimensions the tables and the copy
    # loop below multiply it by. It is still read through the default figure so
    # that the grid is named once rather than once per figure.
    default_figure = manifest["character_styles"]["figures"]["default"]
    characters = sorted(
        f'{entry["archetype"]}_{entry["faction_colour"]}'
        for entry in manifest["assets"]
        if entry["kind"] == "character"
        and entry["style"] == manifest["character_styles"]["default"]
        and entry["figure"] == default_figure
    )

    terrain_out = editor_root / "public" / "board" / "terrain"
    character_out = editor_root / "public" / "board" / "characters"
    for target in (terrain_out, character_out):
        if target.exists():
            shutil.rmtree(target)
        target.mkdir(parents=True)
    for theme in themes:
        for name in terrains:
            sheet = f"{name}_blob{theme['suffix']}.png"
            shutil.copyfile(SOURCE / "terrain" / sheet, terrain_out / sheet)
    suffix = manifest["character_styles"]["frames"]["sheet_suffix"]
    figures = manifest["character_styles"]["figures"]["menu"]
    for style in styles:
        for figure in figures:
            for name in characters:
                dressed = f"{name}{style['suffix']}{figure['suffix']}"
                for sprite in (f"{dressed}.png", f"{dressed}{suffix}.png"):
                    shutil.copyfile(
                        SOURCE / "characters" / sprite, character_out / sprite,
                    )
    # The shadow is not a character: no archetype, no colour, no style. So it
    # is copied by name from its own manifest entry rather than by the grid
    # above.
    shadow = next(
        entry for entry in manifest["assets"] if entry["kind"] == "shadow"
    )
    shadow_path = Path(shadow["path"])
    shutil.copyfile(
        SOURCE / shadow_path,
        editor_root / "public" / "board" / shadow_path,
    )

    module = editor_root / "src" / "generated" / "board-art.ts"
    module.parent.mkdir(parents=True, exist_ok=True)
    module.write_text(
        emit_module(manifest, usage, terrains, characters),
        encoding="utf-8",
    )


def check() -> int:
    with tempfile.TemporaryDirectory(prefix="board-art-") as scratch:
        fresh = Path(scratch)
        generate(fresh)
        problems: list[str] = []
        for path in sorted(
            candidate.relative_to(fresh)
            for candidate in fresh.rglob("*") if candidate.is_file()
        ):
            committed = EDITOR / path
            if not committed.is_file():
                problems.append(f"missing from the committed output: editor/{path}")
            elif not filecmp.cmp(fresh / path, committed, shallow=False):
                problems.append(f"differs from the committed output: editor/{path}")
    if problems:
        print("Board art is out of date. Run:", file=sys.stderr)
        print("  python3 editor/scripts/generate-board-assets.py", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    print("Board art is up to date.")
    return 0


def main() -> int:
    if "--check" in sys.argv[1:]:
        return check()
    generate(EDITOR)
    print(f"wrote {EDITOR / 'public' / 'board'} and src/generated/board-art.ts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
