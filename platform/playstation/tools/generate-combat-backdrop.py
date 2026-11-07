#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Downconvert the shared Godot combat-backdrop layers for the PS1 proof."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from PIL import Image


WIDTH = 320
MAIN_TILE_WIDTH = 64
MAIN_TILE_HEIGHT = 80
OVERLAY_TILE_WIDTH = 64
OVERLAY_TILE_HEIGHT = 64


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def fnv1a32(value: str) -> int:
    result = 2166136261
    for byte in value.encode("utf-8"):
        result = ((result ^ byte) * 16777619) & 0xFFFFFFFF
    return result


def psx_colour(colour: tuple[int, int, int]) -> int:
    red, green, blue = (channel >> 3 for channel in colour)
    return red | green << 5 | blue << 10


def palette_colours(image: Image.Image, count: int) -> list[tuple[int, int, int]]:
    pixels = [pixel[:3] for pixel in image.get_flattened_data()
              if len(pixel) == 3 or pixel[3] >= 48]
    require(bool(pixels), "backdrop layer has no opaque pixels")
    sample = Image.new("RGB", (len(pixels), 1))
    sample.putdata(pixels)
    reduced = sample.quantize(colors=count, method=Image.Quantize.MEDIANCUT,
                              dither=Image.Dither.NONE)
    raw = reduced.getpalette() or []
    return [tuple(raw[index:index + 3])
            for index in range(0, count * 3, 3)]


def nearest_index(colour: tuple[int, int, int],
                  palette: list[tuple[int, int, int]]) -> int:
    return min(range(len(palette)), key=lambda index: sum(
        (colour[channel] - palette[index][channel]) ** 2
        for channel in range(3)
    ))


def indexed(image: Image.Image, transparent: bool) -> tuple[list[int], list[int]]:
    image = image.convert("RGBA")
    colours = palette_colours(image, 255)
    palette = [0] + [psx_colour(colour) for colour in colours]
    indices: list[int] = []
    cache: dict[tuple[int, int, int], int] = {}
    for pixel in image.get_flattened_data():
        if transparent and pixel[3] < 48:
            indices.append(0)
            continue
        colour = pixel[:3]
        selected = cache.get(colour)
        if selected is None:
            selected = nearest_index(colour, colours) + 1
            cache[colour] = selected
        indices.append(selected)
    return indices, palette


def tile_indices(indices: list[int], width: int, height: int,
                 tile_width: int, tile_height: int) -> list[list[int]]:
    tiles: list[list[int]] = []
    for top in range(0, height, tile_height):
        for left in range(0, width, tile_width):
            tile: list[int] = []
            for y in range(tile_height):
                source_y = min(height - 1, top + y)
                for x in range(tile_width):
                    source_x = min(width - 1, left + x)
                    tile.append(indices[source_y * width + source_x])
            tiles.append(tile)
    return tiles


def pack_ci8(indices: list[int]) -> list[int]:
    require(len(indices) % 2 == 0, "CI8 tile must contain an even pixel count")
    return [indices[index] | indices[index + 1] << 8
            for index in range(0, len(indices), 2)]


def array(name: str, values: list[int], ctype: str = "unsigned short") -> str:
    rows = [",".join(str(value) for value in values[index:index + 16])
            for index in range(0, len(values), 16)]
    return (f"inline constexpr {ctype} {name}[{len(values)}] = {{\n    " +
            ",\n    ".join(rows) + "\n};")


def tiled_array(name: str, tiles: list[list[int]]) -> str:
    packed = [pack_ci8(tile) for tile in tiles]
    lines = [f"inline constexpr unsigned short {name}"
             f"[{len(packed)}][{len(packed[0])}] = {{"]
    for tile in packed:
        lines.append("    {" + ",".join(str(value) for value in tile) + "},")
    lines.append("};")
    return "\n".join(lines)


def resolve_resource(root: Path, resource: str) -> Path:
    prefix = "res://"
    require(resource.startswith(prefix), f"not a Godot resource: {resource}")
    return root / "platform/godot/project" / resource[len(prefix):]


def generate(root: Path, scene_path: Path,
             preview_dir: Path | None) -> str:
    scene = json.loads(scene_path.read_text(encoding="utf-8"))
    backdrop = scene["combat_backdrop"]
    descriptor = backdrop["descriptor"]
    manifest_path = root / backdrop["source"]["manifest"]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(manifest["schema"] == 1 and
            manifest["selector"]["seed_algorithm"] == "fnv1a32_utf8" and
            manifest["selector"]["freeze_for_exchange"],
            "combat backdrop manifest selector is unsupported")
    terrain_ids = {terrain["name"]: terrain["id"]
                   for terrain in manifest["terrain_kinds"]}
    require(len(terrain_ids) == 13,
            "combat backdrop manifest must retain all terrain IDs")
    pack = next(
        value for value in manifest["packs"]
        if value["dominant_terrain"] == descriptor["dominant_terrain"] and
        value["secondary_terrain"] == descriptor["secondary_terrain"] and
        value["transition_terrain"] == descriptor["transition_terrain"]
    )
    seed = fnv1a32(descriptor["variant_seed_input"])
    selected = seed % descriptor["variant_count"]
    require(seed == descriptor["variant_seed"] and
            selected == descriptor["selected_variant"] and
            descriptor["freeze_for_exchange"],
            "shared combat backdrop selection is not deterministic")
    variant = next(value for value in backdrop["variants"]
                   if value["index"] == selected)
    roles = {layer["role"]: resolve_resource(root, layer["godot_resource"])
             for layer in variant["layers"]}
    require(list(backdrop["layer_order"]) ==
            ["panorama", "transition", "midground", "props"],
            "PS1 compositor requires the canonical layer order")
    require(set(roles) == set(backdrop["layer_order"]),
            "selected variant is missing a source layer")
    require(pack["variant_count"] == descriptor["variant_count"],
            "manifest and scene disagree on backdrop variants")
    for role, path in roles.items():
        pattern = pack["filename_patterns"][role]
        require(path.name == pattern.format(variant=selected),
                f"manifest and scene disagree on {role} plate")
    require(all(path.is_file() for path in roles.values()),
            "selected Godot source plate is missing")

    source_size = tuple(backdrop["source"]["resolution"])
    images = {role: Image.open(path).convert("RGBA")
              for role, path in roles.items()}
    require(all(image.size == source_size for image in images.values()),
            "Godot source plates disagree with their manifest resolution")
    # The shared plates are 16:9 while the native PS1 frame is 4:3. Apply one
    # centred 4:3 crop to every layer before reduction. This keeps the Godot
    # horizon/floor relationship instead of vertically compressing the plate
    # and opening a seam above the live foreground.
    source_width, source_height = source_size
    target_ratio = WIDTH / 240
    source_ratio = source_width / source_height
    if source_ratio > target_ratio:
        crop_width = round(source_height * target_ratio)
        crop_left = (source_width - crop_width) // 2
        crop_box = (crop_left, 0, crop_left + crop_width, source_height)
    else:
        crop_height = round(source_width / target_ratio)
        crop_top = (source_height - crop_height) // 2
        crop_box = (0, crop_top, source_width, crop_top + crop_height)
    images = {role: image.crop(crop_box) for role, image in images.items()}

    main = images["panorama"].copy()
    main.alpha_composite(images["transition"])
    main.alpha_composite(images["midground"])
    scaled_height = 240
    main = main.resize((WIDTH, scaled_height), Image.Resampling.LANCZOS)
    overlay = images["props"].resize(
        (WIDTH, scaled_height), Image.Resampling.LANCZOS
    )
    foreground = backdrop["foreground_contract"]
    require(not foreground["live_floor_strip"] and
            foreground["floor_composition"] == "pre_rendered_full_frame",
            "PS1 proof expects one continuous pre-rendered floor")
    height = scaled_height
    main = main.crop((0, 0, WIDTH, height))
    overlay = overlay.crop((0, 0, WIDTH, height))

    alpha = overlay.getchannel("A")
    bounds = alpha.getbbox()
    require(bounds is not None, "props plate has no visible pixels")
    left = max(0, bounds[0] // 32 * 32)
    top = max(0, bounds[1] // 32 * 32)
    right = min(WIDTH, int(math.ceil(bounds[2] / 64) * 64))
    bottom = min(height, int(math.ceil(bounds[3] / 64) * 64))
    overlay = overlay.crop((left, top, right, bottom))

    main_indices, main_palette = indexed(main, False)
    overlay_indices, overlay_palette = indexed(overlay, True)
    main_tiles = tile_indices(main_indices, WIDTH, height,
                              MAIN_TILE_WIDTH, MAIN_TILE_HEIGHT)
    overlay_tiles = tile_indices(overlay_indices, overlay.width, overlay.height,
                                 OVERLAY_TILE_WIDTH, OVERLAY_TILE_HEIGHT)
    require(len(main_tiles) <= 18,
            "main CI8 backdrop no longer fits three texture pages")
    require(len(overlay_tiles) <= 18,
            "props overlay no longer fits three texture pages")

    if preview_dir is not None:
        preview_dir.mkdir(parents=True, exist_ok=True)
        main.save(preview_dir / "ps1-backdrop-main-source.png")
        overlay.save(preview_dir / "ps1-backdrop-props-source.png")
        composite = main.copy()
        composite.alpha_composite(overlay, (left, top))
        composite.save(preview_dir / "ps1-backdrop-composite-source.png")

    lines = [
        "// Generated by platform/playstation/tools/generate-combat-backdrop.py.",
        "// Do not edit. Exact downconversion of shared Godot source plates.",
        "#pragma once",
        f"inline constexpr unsigned int grandleon_psx_backdrop_seed = {seed}U;",
        f"inline constexpr int grandleon_psx_backdrop_variant = {selected};",
        f"inline constexpr int grandleon_psx_backdrop_dominant_terrain_id = {terrain_ids[descriptor['dominant_terrain']]};",
        f"inline constexpr int grandleon_psx_backdrop_secondary_terrain_id = {terrain_ids[descriptor['secondary_terrain']]};",
        f"inline constexpr int grandleon_psx_backdrop_transition_terrain_id = {terrain_ids[descriptor['transition_terrain']]};",
        f"inline constexpr int grandleon_psx_backdrop_width = {WIDTH};",
        f"inline constexpr int grandleon_psx_backdrop_height = {height};",
        f"inline constexpr int grandleon_psx_backdrop_tile_width = {MAIN_TILE_WIDTH};",
        f"inline constexpr int grandleon_psx_backdrop_tile_height = {MAIN_TILE_HEIGHT};",
        f"inline constexpr int grandleon_psx_backdrop_tile_count = {len(main_tiles)};",
        f"inline constexpr int grandleon_psx_backdrop_overlay_x = {left};",
        f"inline constexpr int grandleon_psx_backdrop_overlay_y = {top};",
        f"inline constexpr int grandleon_psx_backdrop_overlay_width = {overlay.width};",
        f"inline constexpr int grandleon_psx_backdrop_overlay_height = {overlay.height};",
        f"inline constexpr int grandleon_psx_backdrop_overlay_tile_width = {OVERLAY_TILE_WIDTH};",
        f"inline constexpr int grandleon_psx_backdrop_overlay_tile_height = {OVERLAY_TILE_HEIGHT};",
        f"inline constexpr int grandleon_psx_backdrop_overlay_tile_count = {len(overlay_tiles)};",
        tiled_array("grandleon_psx_backdrop_tiles", main_tiles),
        tiled_array("grandleon_psx_backdrop_overlay_tiles", overlay_tiles),
        array("grandleon_psx_backdrop_clut", main_palette),
        array("grandleon_psx_backdrop_overlay_clut", overlay_palette),
    ]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview-dir", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    content = generate(root, args.scene, args.preview_dir)
    if args.check:
        if not args.output.exists() or \
                args.output.read_text(encoding="utf-8") != content:
            raise SystemExit(f"stale PS1 combat backdrop: {args.output}")
        print(f"PS1 combat backdrop is current: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
