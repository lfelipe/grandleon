#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build project-specific console cutouts from the curated Godot roster.

The console builds deliberately do not carry a second character library.  This
tool reads the same bindings and HD sprite sheets as Godot, resolves the exact
drawings a project can show, applies its faction colours, and emits either PNG
inputs for libdragon's pinned ``mksprite`` or native PS1 CI4 arrays.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from PIL import Image
import numpy as np


ROOT = Path(__file__).resolve().parents[2]
CHARACTERS = ROOT / "platform/godot/project/art/characters"
PROFILES = ROOT / "platform/godot/project/content/graphics_profiles.json"
CELL = 443
BOARD_SIDE = 64
N64_CLOSE_SIDE = 84

STYLES = ("medieval", "scifi", "mythical", "nature", "sengoku", "undead", "pirates")
FIGURES = ("first", "second")
ARCHETYPES = ("knight", "archer", "mage", "stormcaller", "healer", "commander", "rogue", "beast")
COLOURS = ("blue", "red", "green", "violet", "amber", "bone")
TEAM_COLOURS = {
    "blue": (0x23, 0x75, 0xA9),
    "red": (0xB3, 0x48, 0x3F),
    "green": (0x2C, 0x8A, 0x48),
    "violet": (0x6B, 0x42, 0xA3),
    "amber": (0xBC, 0x83, 0x25),
    "bone": (0x9E, 0x98, 0x86),
}
POSES = (
    ("idle", 0),
    ("move", 2),
    ("windup", 4),
    ("attack", 5),
    ("hurt", 7),
)

# The committed 4x2 Godot sheets are right-facing animation sheets, not
# compass sheets. Both console renderers may mirror these frames for westward
# travel without another texture. North/south stay an explicit unsupported
# source view instead of silently pretending that a side silhouette is one.
DIRECTIONAL_ART = {
    "eastWest": "mirrored-native-right",
    "northSouth": "source-frames-required",
}


@dataclass(frozen=True, order=True)
class Drawing:
    style: str
    figure: str
    archetype: str
    colour: str

    @property
    def stem(self) -> str:
        return f"{self.style}_{self.figure}_{self.archetype}_{self.colour}"


def project_drawings(project: dict[str, object]) -> tuple[Drawing, ...]:
    default_style = str(project.get("characterStyleId") or "medieval")
    default_figure = str(project.get("characterFigureId") or "first")
    factions = {
        str(value.get("id")): str(value.get("color") or COLOURS[index % len(COLOURS)])
        for index, value in enumerate(project.get("factions", []))
        if isinstance(value, dict) and value.get("id")
    }
    classes = {
        str(value.get("id")): str(value.get("name") or value.get("id"))
        for value in project.get("classes", [])
        if isinstance(value, dict) and value.get("id")
    }

    result: set[Drawing] = set()
    for unit in project.get("unitTypes", []):
        if not isinstance(unit, dict):
            continue
        style = str(unit.get("characterStyleId") or default_style)
        figure = str(unit.get("characterFigureId") or default_figure)
        class_id = str(unit.get("classId") or "")
        words = f"{classes.get(class_id, class_id)} {unit.get('name', '')}".lower()
        archetype = next((name for name in ARCHETYPES if name in words), "knight")
        faction = unit.get("factionId")
        colours = (factions[str(faction)],) if faction is not None and str(faction) in factions else ("blue", "red")
        if style not in STYLES:
            raise ValueError(f"project asks for unknown character style {style!r}")
        if figure not in FIGURES:
            raise ValueError(f"project asks for unknown character figure {figure!r}")
        for colour in colours:
            if colour not in COLOURS:
                raise ValueError(f"project asks for unknown faction colour {colour!r}")
            result.add(Drawing(style, figure, archetype, colour))
    return tuple(sorted(result))


def profile_bindings() -> dict[tuple[str, str, str], tuple[Path, Path]]:
    document = json.loads(PROFILES.read_text(encoding="utf-8"))
    sprites = document["profiles"]["voxel-sprites"]["assets"]["characterSprites"]
    result: dict[tuple[str, str, str], tuple[Path, Path]] = {}
    for key, binding in sprites.items():
        archetype, _identity, style, figure = key.split("|")
        source = ROOT / str(binding["path"]).removeprefix("res://")
        mask = ROOT / str(binding["teamMaskPath"]).removeprefix("res://")
        # res:// is rooted at the Godot project, not the repository.
        if not source.exists():
            source = ROOT / "platform/godot/project" / str(binding["path"]).removeprefix("res://")
        if not mask.exists():
            mask = ROOT / "platform/godot/project" / str(binding["teamMaskPath"]).removeprefix("res://")
        result[(style, figure, archetype)] = (source, mask)
    return result


def luminance(colour: tuple[int, int, int]) -> float:
    return (colour[0] * 0.2126 + colour[1] * 0.7152 + colour[2] * 0.0722) / 255.0


def tint_for_luminance(team: tuple[int, int, int], value: float) -> tuple[int, int, int]:
    base = luminance(team)
    if value <= base:
        scale = value / max(base, 0.0001)
        return tuple(round(channel * scale) for channel in team)
    scale = (value - base) / max(1.0 - base, 0.0001)
    return tuple(round(channel + (255 - channel) * scale) for channel in team)


def pixels(image: Image.Image) -> list[tuple[int, ...]]:
    flattened = getattr(image, "get_flattened_data", None)
    return list(flattened() if flattened is not None else image.getdata())


def recolour(source: Image.Image, mask: Image.Image, colour: str) -> Image.Image:
    original = np.asarray(source.convert("RGBA"), dtype=np.float32)
    marked = np.asarray(mask.convert("RGBA"), dtype=np.float32)
    weight = marked[:, :, 3:4] / 255.0
    team = np.asarray(TEAM_COLOURS[colour], dtype=np.float32)
    source_luminance = (
        original[:, :, 0] * 0.2126
        + original[:, :, 1] * 0.7152
        + original[:, :, 2] * 0.0722
    ) / 255.0
    base = luminance(TEAM_COLOURS[colour])
    darker = source_luminance[:, :, None] / max(base, 0.0001) * team
    lighter_amount = (
        (source_luminance - base) / max(1.0 - base, 0.0001)
    )[:, :, None]
    lighter = team + (255.0 - team) * lighter_amount
    target = np.where(source_luminance[:, :, None] <= base, darker, lighter)
    rgb = original[:, :, :3] + (target - original[:, :, :3]) * weight
    result = original.copy()
    result[:, :, :3] = np.clip(np.rint(rgb), 0, 255)
    return Image.fromarray(result.astype(np.uint8), "RGBA")


def frame(image: Image.Image, index: int) -> Image.Image:
    left = index % 4 * CELL
    top = index // 4 * CELL
    return image.crop((left, top, left + CELL, top + CELL))


@lru_cache(maxsize=None)
def shared_bounds(path: str) -> tuple[int, int, int, int]:
    with Image.open(path) as sheet:
        boxes = [frame(sheet, index).convert("RGBA").getchannel("A").getbbox() for _, index in POSES]
    visible = [box for box in boxes if box is not None]
    if not visible:
        return (0, 0, CELL, CELL)
    return (
        min(box[0] for box in visible), min(box[1] for box in visible),
        max(box[2] for box in visible), max(box[3] for box in visible),
    )


def fitted(source: Image.Image, bounds: tuple[int, int, int, int], side: int) -> Image.Image:
    left, top, right, bottom = bounds
    pivot_x, pivot_y = CELL * 0.5, CELL * 0.94
    target_x, target_y = side * 0.5, side * 0.94
    margin = 2.0
    scale = min(
        (target_x - margin) / max(pivot_x - left, 1.0),
        (side - margin - target_x) / max(right - pivot_x, 1.0),
        (target_y - margin) / max(pivot_y - top, 1.0),
        (side - target_y) / max(bottom - pivot_y, 1.0),
    )
    crop = source.crop(bounds)
    crop = crop.resize(
        (max(1, round(crop.width * scale)), max(1, round(crop.height * scale))),
        Image.Resampling.LANCZOS,
    )
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.alpha_composite(crop, (
        round(target_x - (pivot_x - left) * scale),
        round(target_y - (pivot_y - top) * scale),
    ))
    canvas.putalpha(canvas.getchannel("A").point(lambda value: 255 if value >= 40 else 0))
    return canvas


@lru_cache(maxsize=None)
def painted_frame(source_name: str, mask_name: str, colour: str, pose: int) -> Image.Image:
    source_path, mask_path = Path(source_name), Path(mask_name)
    with Image.open(source_path) as source, Image.open(mask_path) as mask:
        return recolour(frame(source, pose), frame(mask, pose), colour)


def rendered_frame(binding: tuple[Path, Path], drawing: Drawing, pose: int, side: int) -> Image.Image:
    source_path, mask_path = binding
    painted = painted_frame(str(source_path), str(mask_path), drawing.colour, pose)
    return fitted(painted, shared_bounds(str(source_path)), side)


def psx_colour(colour: tuple[int, int, int]) -> tuple[int, int, int]:
    levels = tuple(channel >> 3 for channel in colour)
    if levels == (0, 0, 0):
        levels = (1, 1, 1)
    return tuple(level * 255 // 31 for level in levels)


def quantize(image: Image.Image) -> tuple[list[int], list[int]]:
    image_data = pixels(image)
    opaque = [pixel[:3] for pixel in image_data if pixel[3] >= 40]
    if not opaque:
        raise ValueError("character frame has no opaque pixels")
    sample = Image.new("RGB", (len(opaque), 1))
    sample.putdata(opaque)
    reduced = sample.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw = reduced.getpalette() or []
    palette = [psx_colour(tuple(raw[index:index + 3])) for index in range(0, 45, 3)]
    while len(palette) < 15:
        palette.append((8, 8, 8))
    indices: list[int] = []
    for pixel in image_data:
        if pixel[3] < 40:
            indices.append(0)
            continue
        nearest = min(range(15), key=lambda index: sum(
            (pixel[channel] - palette[index][channel]) ** 2 for channel in range(3)
        ))
        indices.append(nearest + 1)
    halfwords = [
        sum(indices[offset + step] << (4 * step) for step in range(4))
        for offset in range(0, len(indices), 4)
    ]
    clut = [0] + [((blue >> 3) << 10) | ((green >> 3) << 5) | (red >> 3) for red, green, blue in palette]
    return halfwords, clut


def cpp_array(name: str, values: list[int]) -> str:
    rows = [",".join(str(value) for value in values[offset:offset + 16]) for offset in range(0, len(values), 16)]
    return f"inline constexpr unsigned short {name}[] = {{\n    " + ",\n    ".join(rows) + "\n};\n"


def write_n64(
    output: Path, project: dict[str, object], drawings: tuple[Drawing, ...],
    bindings: dict[tuple[str, str, str], tuple[Path, Path]],
) -> None:
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for drawing in drawings:
        binding = bindings[(drawing.style, drawing.figure, drawing.archetype)]
        assets: dict[str, dict[str, str]] = {"board": {}, "close": {}}
        for pose_name, pose_index in POSES:
            for view, side in (("board", BOARD_SIDE), ("close", N64_CLOSE_SIDE)):
                filename = f"{view}_{drawing.stem}_{pose_name}.png"
                rendered_frame(binding, drawing, pose_index, side).save(output / filename, optimize=True)
                assets[view][pose_name] = filename
        records.append({
            "style": STYLES.index(drawing.style),
            "figure": FIGURES.index(drawing.figure),
            "archetype": ARCHETYPES.index(drawing.archetype),
            "colour": COLOURS.index(drawing.colour),
            "name": drawing.stem,
            "assets": assets,
        })
    manifest = {
        "format": "Grandleon project console cutouts v1",
        "boardSide": BOARD_SIDE,
        "closeSide": N64_CLOSE_SIDE,
        "defaultStyle": STYLES.index(str(project.get("characterStyleId") or "medieval")),
        "defaultFigure": FIGURES.index(str(project.get("characterFigureId") or "first")),
        "directionalArt": DIRECTIONAL_ART,
        "poses": [name for name, _ in POSES],
        "drawings": records,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def write_ps1(output: Path, project: dict[str, object], drawings: tuple[Drawing, ...], bindings: dict[tuple[str, str, str], tuple[Path, Path]]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    chunks = [
        "// Generated from the curated Godot HD roster. Do not edit.\n",
        "#pragma once\n#include <cstddef>\n",
        f"inline constexpr int grandleon_console_character_side = {BOARD_SIDE};\n",
        f"inline constexpr int grandleon_console_character_pose_count = {len(POSES)};\n",
        "inline constexpr bool grandleon_console_mirrors_east_west = true;\n",
        "inline constexpr bool grandleon_console_has_north_south = false;\n",
    ]
    rows = []
    for index, drawing in enumerate(drawings):
        binding = bindings[(drawing.style, drawing.figure, drawing.archetype)]
        texels = []
        cluts = []
        for pose_name, pose_index in POSES:
            values, palette = quantize(rendered_frame(binding, drawing, pose_index, BOARD_SIDE))
            symbol = f"grandleon_console_character_{index}_{pose_name}"
            chunks.append(cpp_array(symbol, values))
            chunks.append(cpp_array(f"{symbol}_clut", palette))
            texels.append(symbol)
            cluts.append(f"{symbol}_clut")
        rows.append(
            "    {" + ",".join(str(value) for value in (
                STYLES.index(drawing.style), FIGURES.index(drawing.figure),
                ARCHETYPES.index(drawing.archetype), COLOURS.index(drawing.colour),
            )) + ",{" + ",".join(texels) + "},{" + ",".join(cluts) + "}},\n"
        )
    default_style = str(project.get("characterStyleId") or "medieval")
    default_figure = str(project.get("characterFigureId") or "first")
    chunks.extend((
        "struct GrandleonConsoleCharacterDrawing final {\n"
        "    int style; int figure; int archetype; int colour;\n"
        "    const unsigned short* texels[grandleon_console_character_pose_count];\n"
        "    const unsigned short* cluts[grandleon_console_character_pose_count];\n"
        "};\n",
        f"inline constexpr int grandleon_console_project_default_style = {STYLES.index(default_style)};\n",
        f"inline constexpr int grandleon_console_project_default_figure = {FIGURES.index(default_figure)};\n",
        f"inline constexpr int grandleon_console_character_count = {len(drawings)};\n",
        "inline constexpr GrandleonConsoleCharacterDrawing grandleon_console_characters[] = {\n",
        "".join(rows),
        "};\n",
    ))
    output.write_text("".join(chunks), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument(
        "--also-project", type=Path, action="append", default=[],
        help="include drawings needed by another executable in the same build",
    )
    parser.add_argument("--platform", choices=("n64", "ps1"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    project = json.loads(arguments.project.read_text(encoding="utf-8"))
    drawings = set(project_drawings(project))
    for extra_path in arguments.also_project:
        extra_project = json.loads(extra_path.read_text(encoding="utf-8"))
        drawings.update(project_drawings(extra_project))
    resolved_drawings = tuple(sorted(drawings))
    bindings = profile_bindings()
    missing = [
        drawing for drawing in resolved_drawings
        if (drawing.style, drawing.figure, drawing.archetype) not in bindings
    ]
    if missing:
        raise ValueError("Godot graphics profile has no bindings for " + ", ".join(drawing.stem for drawing in missing))
    if arguments.platform == "n64":
        write_n64(arguments.output, project, resolved_drawings, bindings)
    else:
        write_ps1(arguments.output, project, resolved_drawings, bindings)
    print(
        f"console art: {len(resolved_drawings)} project drawings -> "
        f"{arguments.output}"
    )


if __name__ == "__main__":
    main()
