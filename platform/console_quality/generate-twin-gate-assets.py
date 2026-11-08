#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the matched Twin Gate console-quality prototype assets.

The two prototype renderers consume the real Steel and Cedar map and the
approved Godot keep pair.  This tool derives only console encodings: one
source-validated scene header, shared 192x176 indexed keep states, N64 tile
PNGs, and a PlayStation CI8 header.  No campaign rule or placement is changed.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "games/steel_and_cedar/source/project.json"
FEATURES = ROOT / "platform/godot/project/art/map_features"
N64_OUTPUT = ROOT / "platform/nintendo64/assets/twin_gate_quality"
PS1_OUTPUT = ROOT / "platform/playstation/assets/twin_gate_quality.h"
SCENE_OUTPUT = ROOT / "platform/console_quality/twin_gate_scene.h"
MANIFEST_OUTPUT = ROOT / "platform/console_quality/twin_gate_assets.json"

WIDTH = 192
HEIGHT = 176
TILE_WIDTH = 64
TILE_HEIGHT = 32
TILE_COLUMNS = WIDTH // TILE_WIDTH
TILE_ROWS = (HEIGHT + TILE_HEIGHT - 1) // TILE_HEIGHT
TILE_COUNT = TILE_COLUMNS * TILE_ROWS
TERRAIN_KINDS = (
    "water", "road", "forest", "mountain", "sand", "snow", "swamp",
    "hills", "ruins", "grass", "farmland", "bamboo", "paved",
)
FEATURE_IDS = {
    "mountain_castle_wall": 1,
    "paved_castle_door": 2,
    "paved_castle_floor": 3,
    "paved_castle_dais": 4,
}
STATES = ("exterior", "threshold_1", "threshold_2", "threshold_3", "cutaway")
BAYER = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)


def pixels(image: Image.Image) -> list[object]:
    """Return flattened pixels without depending on Pillow's retired API."""
    flattened = getattr(image, "get_flattened_data", None)
    if flattened is not None:
        return list(flattened())
    return list(image.getdata())


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def terrain_kind(identity: str) -> int:
    if identity in FEATURE_IDS:
        identity = "mountain" if identity.startswith("mountain_") else "paved"
    if identity not in TERRAIN_KINDS:
        raise ValueError(f"Twin Gate uses an unknown terrain identity: {identity}")
    return TERRAIN_KINDS.index(identity)


def load_scene() -> tuple[list[int], list[int]]:
    project = json.loads(PROJECT.read_text(encoding="utf-8"))
    game_map = next(value for value in project["maps"] if value["id"] == "twin_gate")
    if (game_map["width"], game_map["height"]) != (20, 14):
        raise ValueError("Twin Gate must remain the authored 20x14 map")
    terrain = [terrain_kind(str(value)) for value in game_map["terrain"]]
    features = [FEATURE_IDS.get(str(value), 0) for value in game_map["terrain"]]

    expected: list[int] = []
    for local_y in range(6):
        for local_x in range(6):
            feature = 3
            if (
                local_y in (0, 5) or local_x == 5 or
                (local_x == 0 and local_y in (1, 4))
            ):
                feature = 1
            elif local_x == 0:
                feature = 2
            elif local_x == 4 and local_y in (2, 3):
                feature = 4
            expected.append(feature)
    actual = [
        features[(4 + local_y) * 20 + 13 + local_x]
        for local_y in range(6) for local_x in range(6)
    ]
    if actual != expected or sum(value != 0 for value in features) != 36:
        raise ValueError("Twin Gate's exact 6x6 keep footprint changed")
    return terrain, features


def fitted_keep(path: Path) -> Image.Image:
    source = Image.open(path).convert("RGBA")
    bounds = source.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError(f"keep source is empty: {path}")
    crop = source.crop(bounds)
    scale = min((WIDTH - 4) / crop.width, (HEIGHT - 4) / crop.height)
    crop = crop.resize(
        (round(crop.width * scale), round(crop.height * scale)),
        Image.Resampling.LANCZOS,
    )
    alpha = crop.getchannel("A").point(lambda value: 255 if value >= 64 else 0)
    crop.putalpha(alpha)
    canvas = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
    left = (WIDTH - crop.width) // 2
    top = HEIGHT - 2 - crop.height

    # A one-pixel dark rim survives both consoles' indexed conversion and keeps
    # the large landmark readable over similarly coloured paved cells.
    expanded = alpha.filter(ImageFilter.MaxFilter(3))
    rim = Image.new("RGBA", crop.size, (27, 22, 20, 0))
    rim.putalpha(Image.eval(expanded, lambda value: value))
    rim_alpha = Image.eval(
        Image.frombytes(
            "L", crop.size,
            bytes(max(0, outer - inner) for outer, inner in zip(expanded.tobytes(), alpha.tobytes())),
        ),
        lambda value: 255 if value else 0,
    )
    rim.putalpha(rim_alpha)
    canvas.alpha_composite(rim, (left, top))
    canvas.alpha_composite(crop, (left, top))
    return canvas


def transition(exterior: Image.Image, cutaway: Image.Image, level: int) -> Image.Image:
    result = Image.new("RGBA", exterior.size)
    first = exterior.load()
    second = cutaway.load()
    output = result.load()
    threshold = level * 4
    for y in range(HEIGHT):
        for x in range(WIDTH):
            output[x, y] = second[x, y] if BAYER[y & 3][x & 3] < threshold else first[x, y]
    return result


def palette_for(image: Image.Image) -> tuple[list[int], list[int]]:
    rgba = image.convert("RGBA")
    # A restrained ordered perturbation makes gradients read as deliberate
    # PS1 colour rather than broad accidental bands. It is shared with N64 so
    # the matched landmark itself stays comparable.
    dithered = Image.new("RGB", rgba.size)
    converted: list[tuple[int, int, int]] = []
    opaque: list[tuple[int, int, int]] = []
    for x, y, red, green, blue, alpha in (
        (index % WIDTH, index // WIDTH, *pixel)
        for index, pixel in enumerate(pixels(rgba))
    ):
        amount = BAYER[y & 3][x & 3] - 8
        colour = tuple(max(0, min(255, channel + amount)) for channel in (red, green, blue))
        converted.append(colour)
        if alpha >= 64:
            opaque.append(colour)
    dithered.putdata(converted)
    sample = Image.new("RGB", (len(opaque), 1))
    sample.putdata(opaque)
    reduced = sample.quantize(
        colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE
    )
    raw = reduced.getpalette() or []
    colours = [(0, 0, 0)] + [
        tuple(raw[index:index + 3]) for index in range(0, 255 * 3, 3)
    ]
    while len(colours) < 256:
        colours.append((0, 0, 0))
    palette_image = Image.new("P", (1, 1))
    palette_image.putpalette([channel for colour in colours for channel in colour])
    mapped = dithered.quantize(palette=palette_image, dither=Image.Dither.NONE)
    indices = pixels(mapped)
    source = pixels(rgba)
    for index, pixel in enumerate(source):
        if pixel[3] < 64:
            indices[index] = 0
        elif indices[index] == 0:
            indices[index] = min(
                range(1, 256),
                key=lambda at: sum(
                    (pixel[channel] - colours[at][channel]) ** 2
                    for channel in range(3)
                ),
            )
    flattened = [channel for colour in colours for channel in colour]
    return indices, flattened


def indexed_image(indices: list[int], palette: list[int]) -> Image.Image:
    result = Image.new("P", (WIDTH, HEIGHT))
    result.putdata(indices)
    result.putpalette(palette)
    result.info["transparency"] = 0
    return result


def bgr555(palette: list[int]) -> list[int]:
    result = [0]
    for offset in range(3, len(palette), 3):
        red, green, blue = palette[offset:offset + 3]
        result.append((blue >> 3) << 10 | (green >> 3) << 5 | (red >> 3))
    return result[:256]


def rows(values: Iterable[int], width: int = 16) -> str:
    values = list(values)
    return ",\n        ".join(
        ",".join(str(value) for value in values[offset:offset + width])
        for offset in range(0, len(values), width)
    )


def write_scene(terrain: list[int], features: list[int]) -> None:
    SCENE_OUTPUT.write_text(
        "// Generated from Steel and Cedar's authored Twin Gate map. Do not edit.\n"
        "#pragma once\n#include <cstdint>\n"
        "namespace grandleon::console_quality::twin_gate {\n"
        "inline constexpr int map_width = 20;\n"
        "inline constexpr int map_height = 14;\n"
        "inline constexpr int keep_x = 13;\n"
        "inline constexpr int keep_y = 4;\n"
        "inline constexpr int keep_size = 6;\n"
        "inline constexpr std::uint8_t terrain[] = {\n        " + rows(terrain, 20) + "\n};\n"
        "inline constexpr std::uint8_t feature[] = {\n        " + rows(features, 20) + "\n};\n"
        "struct Unit { int x, y, style, figure, archetype, colour; };\n"
        "inline constexpr Unit approach_units[] = {\n"
        "    {6,6,0,0,5,0}, {5,6,0,1,4,0}, {12,3,4,0,0,1},\n"
        "};\n"
        "inline constexpr Unit revealed_units[] = {\n"
        "    {14,6,0,0,5,0}, {14,7,0,1,4,0}, {16,6,4,0,5,1}, {13,7,4,1,2,1},\n"
        "};\n"
        "}\n",
        encoding="utf-8",
    )


def write_assets(states: dict[str, Image.Image]) -> list[dict[str, object]]:
    N64_OUTPUT.mkdir(parents=True, exist_ok=True)
    for old in N64_OUTPUT.glob("*.png"):
        old.unlink()
    declarations: list[str] = [
        "// Generated from the approved Twin Gate source pair. Do not edit.\n",
        "#pragma once\n#include <cstddef>\n#include <cstdint>\n",
        f"inline constexpr int twin_gate_quality_width = {WIDTH};\n",
        f"inline constexpr int twin_gate_quality_height = {HEIGHT};\n",
        f"inline constexpr int twin_gate_quality_tile_width = {TILE_WIDTH};\n",
        f"inline constexpr int twin_gate_quality_tile_height = {TILE_HEIGHT};\n",
        f"inline constexpr int twin_gate_quality_tile_columns = {TILE_COLUMNS};\n",
        f"inline constexpr int twin_gate_quality_tile_count = {TILE_COUNT};\n",
        f"inline constexpr int twin_gate_quality_state_count = {len(STATES)};\n",
    ]
    manifest_assets: list[dict[str, object]] = []
    for state_index, state_name in enumerate(STATES):
        indices, palette = palette_for(states[state_name])
        indexed = indexed_image(indices, palette)
        clut = bgr555(palette)
        declarations.append(
            f"inline constexpr unsigned short twin_gate_quality_clut_{state_name}[256] = {{\n"
            f"        {rows(clut)}\n}};\n"
        )
        declarations.append(
            f"inline constexpr unsigned short twin_gate_quality_tiles_{state_name}"
            f"[{TILE_COUNT}][{TILE_WIDTH * TILE_HEIGHT // 2}] = {{\n"
        )
        for tile_y in range(TILE_ROWS):
            for tile_x in range(TILE_COLUMNS):
                box = (
                    tile_x * TILE_WIDTH, tile_y * TILE_HEIGHT,
                    (tile_x + 1) * TILE_WIDTH, (tile_y + 1) * TILE_HEIGHT,
                )
                tile = indexed.crop(box)
                filename = f"{state_name}_r{tile_y}_c{tile_x}.png"
                tile.save(N64_OUTPUT / filename, optimize=True, transparency=0)
                tile_indices = pixels(tile)
                packed = [
                    tile_indices[offset] | tile_indices[offset + 1] << 8
                    for offset in range(0, len(tile_indices), 2)
                ]
                declarations.append("    {\n        " + rows(packed) + "\n    },\n")
                manifest_assets.append({
                    "state": state_name,
                    "stateIndex": state_index,
                    "row": tile_y,
                    "column": tile_x,
                    "path": f"platform/nintendo64/assets/twin_gate_quality/{filename}",
                    "sha256": sha256(N64_OUTPUT / filename),
                })
        declarations.append("};\n")
    PS1_OUTPUT.write_text("".join(declarations), encoding="utf-8")
    return manifest_assets


def main() -> None:
    terrain, features = load_scene()
    exterior_path = FEATURES / "medieval_castle_keep_exterior.png"
    cutaway_path = FEATURES / "medieval_castle_keep_cutaway.png"
    exterior = fitted_keep(exterior_path)
    cutaway = fitted_keep(cutaway_path)
    states = {
        "exterior": exterior,
        "threshold_1": transition(exterior, cutaway, 1),
        "threshold_2": transition(exterior, cutaway, 2),
        "threshold_3": transition(exterior, cutaway, 3),
        "cutaway": cutaway,
    }
    write_scene(terrain, features)
    assets = write_assets(states)
    manifest = {
        "format": "Grandleon Twin Gate console quality prototype v1",
        "project": str(PROJECT.relative_to(ROOT)),
        "projectSha256": sha256(PROJECT),
        "source": {
            "exterior": str(exterior_path.relative_to(ROOT)),
            "exteriorSha256": sha256(exterior_path),
            "cutaway": str(cutaway_path.relative_to(ROOT)),
            "cutawaySha256": sha256(cutaway_path),
        },
        "keepFootprint": {"x": 13, "y": 4, "width": 6, "height": 6},
        "states": list(STATES),
        "image": {"width": WIDTH, "height": HEIGHT},
        "tile": {"width": TILE_WIDTH, "height": TILE_HEIGHT, "count": TILE_COUNT},
        "encoding": "CI8, transparent index 0, shared 256-entry BGR555 palette per state",
        "dithering": "original 4x4 ordered colour perturbation before deterministic median-cut",
        "nintendo64Assets": assets,
        "playstationHeader": str(PS1_OUTPUT.relative_to(ROOT)),
        "playstationHeaderSha256": sha256(PS1_OUTPUT),
        "sceneHeader": str(SCENE_OUTPUT.relative_to(ROOT)),
        "sceneHeaderSha256": sha256(SCENE_OUTPUT),
    }
    MANIFEST_OUTPUT.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        f"Twin Gate quality assets: {len(STATES)} states, "
        f"{len(assets)} N64 tiles, {PS1_OUTPUT.stat().st_size} PS1 header bytes"
    )


if __name__ == "__main__":
    main()
