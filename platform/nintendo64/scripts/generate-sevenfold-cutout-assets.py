#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Derive the N64 2.5D cutouts from the curated Godot sheets.

The checked-in PNGs are build inputs so the pinned N64 container only needs
libdragon's mksprite. Re-running this script is the explicit provenance path
when a curated source sheet changes.
"""

from __future__ import annotations

import hashlib
import json
import math
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "platform/godot/project/art/characters"
OUTPUT = ROOT / "platform/nintendo64/prototype/assets/sevenfold_cutouts"
CELL = 443
SIDE = 84
TACTICAL_SIDE = 56
TEAM_COLOURS = {
    "blue": (0x23, 0x75, 0xA9),
    "red": (0xB3, 0x48, 0x3F),
}
CLOSE_LAYOUT_FRAMES = (0, 1, 4, 5, 6, 7)
TACTICAL_LAYOUT_FRAMES = (0, 1, 2, 3)

ASSETS = (
    ("medieval_knight_male", "blue", "idle", 0),
    ("medieval_knight_male", "blue", "idle_b", 1),
    ("medieval_knight_male", "blue", "windup", 4),
    ("medieval_knight_male", "blue", "attack", 5),
    ("medieval_knight_male", "blue", "hurt", 7),
    ("medieval_knight_female", "red", "idle", 0),
    ("medieval_knight_female", "red", "idle_b", 1),
    ("medieval_knight_female", "red", "windup", 4),
    ("medieval_knight_female", "red", "attack", 5),
    ("medieval_knight_female", "red", "hurt", 7),
    ("mythical_mage_male", "blue", "idle", 0),
    ("mythical_mage_male", "blue", "idle_b", 1),
    ("mythical_mage_male", "blue", "windup", 4),
    ("mythical_mage_male", "blue", "attack", 5),
    ("mythical_mage_male", "blue", "hurt", 7),
    ("mythical_mage_female", "red", "idle", 0),
    ("mythical_mage_female", "red", "idle_b", 1),
    ("mythical_mage_female", "red", "windup", 4),
    ("mythical_mage_female", "red", "attack", 5),
    ("mythical_mage_female", "red", "hurt", 7),
)

TACTICAL_ASSETS = (
    ("medieval_knight_male", "blue", "idle", 0),
    ("medieval_archer_female", "red", "idle", 0),
    ("medieval_mage_male", "blue", "idle", 0),
    ("medieval_stormcaller_female", "red", "idle", 0),
    ("medieval_healer_male", "blue", "idle", 0),
    ("medieval_commander_female", "red", "idle", 0),
    ("medieval_rogue_male", "blue", "idle", 0),
    ("medieval_beast_female", "red", "idle", 0),
    ("medieval_knight_male", "blue", "idle_b", 1),
    ("medieval_archer_female", "red", "idle_b", 1),
    ("medieval_mage_male", "blue", "idle_b", 1),
    ("medieval_stormcaller_female", "red", "idle_b", 1),
    ("medieval_healer_male", "blue", "idle_b", 1),
    ("medieval_commander_female", "red", "idle_b", 1),
    ("medieval_rogue_male", "blue", "idle_b", 1),
    ("medieval_beast_female", "red", "idle_b", 1),
    ("medieval_rogue_male", "blue", "move_a", 2),
    ("medieval_rogue_male", "blue", "move_b", 3),
)


def luminance(colour: tuple[int, int, int]) -> float:
    return (
        colour[0] / 255.0 * 0.2126
        + colour[1] / 255.0 * 0.7152
        + colour[2] / 255.0 * 0.0722
    )


def tint_for_luminance(
    colour: tuple[int, int, int], source_luminance: float
) -> tuple[int, int, int]:
    team = tuple(channel / 255.0 for channel in colour)
    base = luminance(colour)
    if source_luminance <= base:
        amount = source_luminance / max(base, 0.0001)
        result = tuple(channel * amount for channel in team)
    else:
        amount = (source_luminance - base) / max(1.0 - base, 0.0001)
        result = tuple(channel + (1.0 - channel) * amount for channel in team)
    return tuple(round(channel * 255.0) for channel in result)


def recolour(
    source: Image.Image, mask: Image.Image, colour: tuple[int, int, int]
) -> Image.Image:
    source = source.convert("RGBA")
    mask = mask.convert("RGBA")
    pixels: list[tuple[int, int, int, int]] = []
    source_pixels = source.load()
    mask_pixels = mask.load()
    for y in range(source.height):
        for x in range(source.width):
            original = source_pixels[x, y]
            masked = mask_pixels[x, y]
            weight = masked[3] / 255.0
            if weight <= 0.0:
                pixels.append(original)
                continue
            tinted = tint_for_luminance(colour, luminance(original[:3]))
            pixels.append(
                (
                    round(original[0] + (tinted[0] - original[0]) * weight),
                    round(original[1] + (tinted[1] - original[1]) * weight),
                    round(original[2] + (tinted[2] - original[2]) * weight),
                    original[3],
                )
            )
    result = Image.new("RGBA", source.size)
    result.putdata(pixels)
    return result


def frame(image: Image.Image, index: int) -> Image.Image:
    left = (index % 4) * CELL
    top = (index // 4) * CELL
    return image.crop((left, top, left + CELL, top + CELL))


@lru_cache(maxsize=None)
def union_bounds(
    identity: str, layout_frames: tuple[int, ...]
) -> tuple[int, int, int, int]:
    source_path = SOURCE / f"{identity}_hd.png"
    with Image.open(source_path) as source_image:
        boxes = [
            frame(source_image, index)
            .convert("RGBA")
            .getchannel("A")
            .point(lambda value: 255 if value >= 32 else 0)
            .getbbox()
            for index in layout_frames
        ]
    visible = [box for box in boxes if box is not None]
    if not visible:
        return (0, 0, CELL, CELL)
    return (
        min(box[0] for box in visible),
        min(box[1] for box in visible),
        max(box[2] for box in visible),
        max(box[3] for box in visible),
    )


def fit_to_canvas(
    image: Image.Image, bounds: tuple[int, int, int, int], side: int
) -> Image.Image:
    left, top, right, bottom = bounds
    source_pivot_x = CELL * 0.5
    source_pivot_y = CELL * 0.94
    target_pivot_x = side * 0.5
    target_pivot_y = side * 0.94
    margin = 2.0
    limits = [
        (target_pivot_x - margin) / max(source_pivot_x - left, 1.0),
        (side - margin - target_pivot_x) / max(right - source_pivot_x, 1.0),
        (target_pivot_y - margin) / max(source_pivot_y - top, 1.0),
        (side - target_pivot_y) / max(bottom - source_pivot_y, 1.0),
    ]
    scale = min(limits)
    cropped = image.crop(bounds)
    width = max(1, round(cropped.width * scale))
    height = max(1, round(cropped.height * scale))
    resized = cropped.resize((width, height), Image.Resampling.LANCZOS)
    target_left = round(target_pivot_x - (source_pivot_x - left) * scale)
    target_top = round(target_pivot_y - (source_pivot_y - top) * scale)
    result = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    result.alpha_composite(resized, (target_left, target_top))
    return result


def source_digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_cutout(
    identity: str, team: str, pose: str, frame_index: int,
    side: int, layout_frames: tuple[int, ...], prefix: str = ""
) -> dict[str, object]:
    source_path = SOURCE / f"{identity}_hd.png"
    mask_path = SOURCE / f"{identity}_hd_team_mask.png"
    with Image.open(source_path) as source_image, Image.open(mask_path) as mask_image:
        cutout = recolour(
            frame(source_image, frame_index),
            frame(mask_image, frame_index),
            TEAM_COLOURS[team],
        )
    bounds = union_bounds(identity, layout_frames)
    cutout = fit_to_canvas(cutout, bounds, side)
    alpha = cutout.getchannel("A").point(lambda value: 255 if value >= 32 else 0)
    cutout.putalpha(alpha)
    name = f"{prefix}{identity}_{team}_{pose}.png"
    cutout.save(OUTPUT / name, optimize=True)
    return {
        "path": name,
        "source": str(source_path.relative_to(ROOT)),
        "source_sha256": source_digest(source_path),
        "mask": str(mask_path.relative_to(ROOT)),
        "mask_sha256": source_digest(mask_path),
        "frame": frame_index,
        "team": team,
        "pose": pose,
        "width": side,
        "height": side,
        "layout_frames": list(layout_frames),
        "union_bounds": list(bounds),
        "pivot": [0.5, 0.94],
    }


def write_supporting_effects() -> list[dict[str, object]]:
    shadow = Image.new("RGBA", (32, 16), (0, 0, 0, 0))
    shadow_draw = ImageDraw.Draw(shadow)
    # Three opaque tonal bands survive CI4 alpha testing while approximating a
    # soft projected contact shadow without blended-transparency fill cost.
    shadow_draw.ellipse((1, 3, 30, 14), fill=(31, 35, 34, 255))
    shadow_draw.ellipse((4, 5, 27, 12), fill=(18, 21, 22, 255))
    shadow_draw.ellipse((8, 6, 23, 11), fill=(9, 12, 14, 255))
    shadow.save(OUTPUT / "shadow.png", optimize=True)

    impact = Image.new("RGBA", (48, 48), (0, 0, 0, 0))
    impact_draw = ImageDraw.Draw(impact)
    points = []
    for index in range(24):
        angle = index * math.pi / 12.0
        radius = 22 if index % 2 == 0 else 8
        points.append(
            (24 + radius * math.cos(angle), 24 + radius * math.sin(angle))
        )
    impact_draw.polygon(points, fill=(241, 163, 43, 255))
    impact_draw.ellipse((15, 15, 33, 33), fill=(255, 232, 135, 255))
    impact_draw.ellipse((20, 20, 28, 28), fill=(255, 255, 240, 255))
    impact.save(OUTPUT / "impact.png", optimize=True)
    return [
        {"path": "shadow.png", "generated": "ellipse", "width": 32, "height": 16},
        {"path": "impact.png", "generated": "starburst", "width": 48, "height": 48},
    ]


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    manifest_assets = []
    for identity, team, pose, frame_index in ASSETS:
        manifest_assets.append(
            write_cutout(
                identity, team, pose, frame_index, SIDE,
                CLOSE_LAYOUT_FRAMES,
            )
        )
    for identity, team, pose, frame_index in TACTICAL_ASSETS:
        manifest_assets.append(
            write_cutout(
                identity, team, pose, frame_index, TACTICAL_SIDE,
                TACTICAL_LAYOUT_FRAMES,
                prefix="tactical_",
            )
        )
    manifest_assets.extend(write_supporting_effects())
    (OUTPUT / "manifest.json").write_text(
        json.dumps(
            {
                "format": "Grandleon N64 fixed-camera 2.5D cutouts v3",
                "generated_by": str(Path(__file__).resolve().relative_to(ROOT)),
                "generator_sha256": source_digest(Path(__file__).resolve()),
                "assets": manifest_assets,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
