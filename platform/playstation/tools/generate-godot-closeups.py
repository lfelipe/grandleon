#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Quantize selected Godot close-battle frames into native PS1 CI4 cutouts."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from PIL import Image


SIZE = 96
QUALITY_CLOSE_SIZE = 128
TACTICAL_SIZE = 64
FRAME = 443
TEAM_COLOURS = {"blue": (0x23, 0x75, 0xA9), "red": (0xB3, 0x48, 0x3F)}
SOURCES = (
    ("medieval_male_blue", "medieval_knight_male_hd.png", "blue", False, 0),
    ("medieval_female_red", "medieval_knight_female_hd.png", "red", True, 0),
    ("mythical_male_blue", "mythical_mage_male_hd.png", "blue", False, 0),
    ("mythical_female_red", "mythical_mage_female_hd.png", "red", True, 0),
    ("medieval_male_blue_move_a", "medieval_knight_male_hd.png", "blue", False, 2),
    ("medieval_male_blue_move_b", "medieval_knight_male_hd.png", "blue", False, 3),
)

QUALITY_CLOSE_SOURCES = tuple(
    (f"quality_{style}_{body}_{team}_{pose}", filename, team, flip, frame)
    for style, body, team, filename, flip in (
        ("medieval", "male", "blue", "medieval_knight_male_hd.png", False),
        ("medieval", "female", "red", "medieval_knight_female_hd.png", True),
        ("mythical", "male", "blue", "mythical_mage_male_hd.png", False),
        ("mythical", "female", "red", "mythical_mage_female_hd.png", True),
    )
    for pose, frame in (
        ("idle", 0), ("windup", 4), ("follow", 5), ("hurt", 7)
    )
)

TACTICAL_SOURCES = (
    ("tactical_medieval_knight_male_blue_idle",
     "medieval_knight_male_hd.png", "blue", False, 0),
    ("tactical_medieval_archer_female_red_idle",
     "medieval_archer_female_hd.png", "red", True, 0),
    ("tactical_medieval_mage_male_blue_idle",
     "medieval_mage_male_hd.png", "blue", False, 0),
    ("tactical_medieval_stormcaller_female_red_idle",
     "medieval_stormcaller_female_hd.png", "red", True, 0),
    ("tactical_medieval_rogue_male_blue_idle",
     "medieval_rogue_male_hd.png", "blue", False, 0),
    ("tactical_medieval_beast_female_red_idle",
     "medieval_beast_female_hd.png", "red", True, 0),
    ("tactical_medieval_healer_male_blue_idle",
     "medieval_healer_male_hd.png", "blue", False, 0),
    ("tactical_medieval_commander_female_red_idle",
     "medieval_commander_female_hd.png", "red", True, 0),
    ("tactical_medieval_rogue_male_blue_move_a",
     "medieval_rogue_male_hd.png", "blue", False, 2),
    ("tactical_medieval_rogue_male_blue_move_b",
     "medieval_rogue_male_hd.png", "blue", False, 3),
)


def image_pixels(image: Image.Image) -> list[tuple[int, ...]]:
    getter = getattr(image, "get_flattened_data", None)
    return list(getter() if getter is not None else image.getdata())


def luminance(colour: tuple[int, int, int]) -> float:
    return (colour[0] * 0.2126 + colour[1] * 0.7152 + colour[2] * 0.0722) / 255.0


def tint_for_luminance(
    team: tuple[int, int, int], value: float
) -> tuple[int, int, int]:
    base = luminance(team)
    if value <= base:
        scale = value / max(base, 0.0001)
        return tuple(round(channel * scale) for channel in team)
    scale = (value - base) / max(1.0 - base, 0.0001)
    return tuple(round(channel + (255 - channel) * scale) for channel in team)


def recolour(source: Image.Image, mask: Image.Image, team: tuple[int, int, int]) -> Image.Image:
    source = source.convert("RGBA")
    mask = mask.convert("RGBA")
    pixels = image_pixels(source)
    weights = [pixel[3] / 255.0 for pixel in image_pixels(mask)]
    out: list[tuple[int, int, int, int]] = []
    for pixel, weight in zip(pixels, weights, strict=True):
        if weight <= 0.0 or pixel[3] == 0:
            out.append(pixel)
            continue
        target = tint_for_luminance(team, luminance(pixel[:3]))
        out.append(tuple(
            round(pixel[channel] + (target[channel] - pixel[channel]) * weight)
            for channel in range(3)
        ) + (pixel[3],))
    result = Image.new("RGBA", source.size)
    result.putdata(out)
    return result


def fitted_frame(
    source: Image.Image, mask: Image.Image, team: str, flip: bool, frame: int,
    size: int = SIZE, shared_bounds: tuple[int, int, int, int] | None = None,
) -> Image.Image:
    left = (frame % 4) * FRAME
    top = (frame // 4) * FRAME
    source = source.crop((left, top, left + FRAME, top + FRAME))
    mask = mask.crop((left, top, left + FRAME, top + FRAME))
    result = recolour(source, mask, TEAM_COLOURS[team])
    bounds = shared_bounds or result.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError("source frame has no opaque pixels")
    result = result.crop(bounds)
    result.thumbnail((size - 8, size - 4), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(result, ((size - result.width) // 2,
                                    size - 2 - result.height))
    return canvas.transpose(Image.Transpose.FLIP_LEFT_RIGHT) if flip else canvas


def union_frame_bounds(
    source: Image.Image, frame_indices: set[int]
) -> tuple[int, int, int, int]:
    bounds: tuple[int, int, int, int] | None = None
    for frame_index in frame_indices:
        left = (frame_index % 4) * FRAME
        top = (frame_index // 4) * FRAME
        frame = source.crop((left, top, left + FRAME, top + FRAME))
        current = frame.getchannel("A").getbbox()
        if current is None:
            raise ValueError(f"source frame {frame_index} has no opaque pixels")
        if bounds is None:
            bounds = current
        else:
            bounds = (
                min(bounds[0], current[0]), min(bounds[1], current[1]),
                max(bounds[2], current[2]), max(bounds[3], current[3]),
            )
    if bounds is None:
        raise ValueError("pose group has no frames")
    return bounds


def psx_colour(colour: tuple[int, int, int]) -> tuple[int, int, int]:
    levels = tuple(channel >> 3 for channel in colour)
    if levels == (0, 0, 0):
        levels = (1, 1, 1)
    return tuple(level * 255 // 31 for level in levels)


def quantize(image: Image.Image) -> tuple[list[int], list[int], Image.Image]:
    image_data = image_pixels(image)
    opaque = [pixel[:3] for pixel in image_data if pixel[3] >= 48]
    sample = Image.new("RGB", (len(opaque), 1))
    sample.putdata(opaque)
    reduced = sample.quantize(
        colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE
    )
    raw_palette = reduced.getpalette() or []
    palette = [psx_colour(tuple(raw_palette[index:index + 3])) for index in range(0, 45, 3)]
    while len(palette) < 15:
        palette.append((8, 8, 8))

    indices: list[int] = []
    preview = Image.new("RGBA", image.size)
    preview_pixels: list[tuple[int, int, int, int]] = []
    for pixel in image_data:
        if pixel[3] < 48:
            indices.append(0)
            preview_pixels.append((0, 0, 0, 0))
            continue
        nearest = min(
            range(15),
            key=lambda index: sum(
                (pixel[channel] - palette[index][channel]) ** 2
                for channel in range(3)
            ),
        )
        indices.append(nearest + 1)
        preview_pixels.append(palette[nearest] + (255,))
    preview.putdata(preview_pixels)

    halfwords: list[int] = []
    for offset in range(0, len(indices), 4):
        halfwords.append(sum(indices[offset + step] << (4 * step) for step in range(4)))
    clut = [0]
    for red, green, blue in palette:
        clut.append((blue >> 3) << 10 | (green >> 3) << 5 | (red >> 3))
    return halfwords, clut, preview


def array(name: str, values: list[int]) -> str:
    rows = [",".join(str(value) for value in values[index:index + 16])
            for index in range(0, len(values), 16)]
    return (f"inline constexpr unsigned short {name}[{len(values)}] = {{\n    "
            + ",\n    ".join(rows) + "\n};")


def generate(root: Path, preview_dir: Path | None) -> str:
    art = root / "platform/godot/project/art/characters"
    lines = [
        "// Generated by platform/playstation/tools/generate-godot-closeups.py.",
        "// Do not edit. Sources are exact frames from curated Godot sheets.",
        "#pragma once",
        f"inline constexpr int grandleon_psx_godot_closeup_width = {SIZE};",
        f"inline constexpr int grandleon_psx_godot_closeup_height = {SIZE};",
        f"inline constexpr int grandleon_psx_godot_quality_close_width = {QUALITY_CLOSE_SIZE};",
        f"inline constexpr int grandleon_psx_godot_quality_close_height = {QUALITY_CLOSE_SIZE};",
        f"inline constexpr int grandleon_psx_godot_tactical_width = {TACTICAL_SIZE};",
        f"inline constexpr int grandleon_psx_godot_tactical_height = {TACTICAL_SIZE};",
    ]
    groups = (
        (SOURCES, SIZE),
        (QUALITY_CLOSE_SOURCES, QUALITY_CLOSE_SIZE),
        (TACTICAL_SOURCES, TACTICAL_SIZE),
    )
    for sources, size in groups:
        grouped_frames: dict[str, set[int]] = {}
        for _, filename, _, _, frame_index in sources:
            grouped_frames.setdefault(filename, set()).add(frame_index)
        grouped_bounds: dict[str, tuple[int, int, int, int]] = {}
        if size != SIZE:
            for filename, frame_indices in grouped_frames.items():
                grouped_bounds[filename] = union_frame_bounds(
                    Image.open(art / filename), frame_indices
                )
        for name, filename, team, flip, frame_index in sources:
            source_path = art / filename
            mask_path = art / filename.replace(".png", "_team_mask.png")
            source = Image.open(source_path)
            mask = Image.open(mask_path)
            frame = fitted_frame(
                source, mask, team, flip, frame_index, size,
                grouped_bounds.get(filename),
            )
            texels, clut, preview = quantize(frame)
            digest = hashlib.sha256(
                source_path.read_bytes() + mask_path.read_bytes()
            ).hexdigest()
            symbol = f"grandleon_psx_godot_closeup_{name}"
            lines.append(
                f"// {filename} frame {frame_index} + team mask sha256 {digest}"
            )
            lines.append(array(symbol, texels))
            lines.append(array(symbol + "_clut", clut))
            if preview_dir is not None:
                preview_dir.mkdir(parents=True, exist_ok=True)
                preview.save(preview_dir / f"{name}.png")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview-dir", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    content = generate(root, args.preview_dir)
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != content:
            raise SystemExit(f"stale generated close-up header: {args.output}")
        print(f"Godot close-up CI4 header is current: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
