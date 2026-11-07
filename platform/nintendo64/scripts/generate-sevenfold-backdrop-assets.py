#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Downconvert the shared Godot combat plates for the N64 viewer.

The shared 1280x720 PNG layers remain the source of truth. This script
composites each approved variant in the documented order, crops the immersive
close-view band, downsamples it to 320x160, and splits it into 80x40 tiles.
The pinned N64 toolchain later gives every tile its own CI4 palette, keeping
each upload below TMEM without reducing the source plates to one global
sixteen-colour image.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "platform/godot/project/art/combat_backdrops"
PACK = SOURCE / "medieval_forest_gate"
OUTPUT = ROOT / "platform/nintendo64/prototype/assets/sevenfold_backdrops"
LAYERS = (
    "panorama_{variant}.png",
    "transition_grass_paved_{variant}.png",
    "midground_forest_{variant}.png",
    "props_{variant}.png",
)
VARIANTS = 3
WIDTH = 320
HEIGHT = 160
TILE_WIDTH = 80
TILE_HEIGHT = 40


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def render_variant(variant: int) -> tuple[Image.Image, list[dict[str, str]]]:
    sources: list[dict[str, str]] = []
    composite = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
    for pattern in LAYERS:
        path = PACK / pattern.format(variant=variant)
        layer = Image.open(path).convert("RGBA")
        composite.alpha_composite(layer)
        sources.append(
            {
                "path": str(path.relative_to(ROOT)),
                "sha256": digest(path),
            }
        )
    # The approved plate places the useful sky, treeline, grass and paved
    # transition in this centred 2:1 band. Cropping here removes unused lower
    # clear colour; the live N64 floor strip owns that part of the frame.
    composite = composite.crop((80, 0, 1200, 560))
    composite = composite.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    return composite, sources


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    assets: list[dict[str, object]] = []
    composites: list[dict[str, object]] = []
    source_variants: list[dict[str, object]] = []
    for variant in range(VARIANTS):
        composite, sources = render_variant(variant)
        composite_path = OUTPUT / f"medieval_forest_gate_v{variant}.png"
        composite.save(composite_path, optimize=True)
        composites.append(
            {
                "path": composite_path.name,
                "sha256": digest(composite_path),
                "variant": variant,
                "width": WIDTH,
                "height": HEIGHT,
            }
        )
        source_variants.append({"variant": variant, "layers": sources})
        for row in range(HEIGHT // TILE_HEIGHT):
            for column in range(WIDTH // TILE_WIDTH):
                tile = composite.crop(
                    (
                        column * TILE_WIDTH,
                        row * TILE_HEIGHT,
                        (column + 1) * TILE_WIDTH,
                        (row + 1) * TILE_HEIGHT,
                    )
                )
                name = (
                    f"medieval_forest_gate_v{variant}_"
                    f"r{row}_c{column}.png"
                )
                path = OUTPUT / name
                tile.save(path, optimize=True)
                assets.append(
                    {
                        "path": name,
                        "sha256": digest(path),
                        "variant": variant,
                        "row": row,
                        "column": column,
                        "width": TILE_WIDTH,
                        "height": TILE_HEIGHT,
                        "format": "CI4",
                    }
                )

    generator = Path(__file__).resolve()
    manifest = {
        "schema": 1,
        "pack": "medieval_forest_gate",
        "terrain_ids": {
            "dominant": 2,
            "secondary": 9,
            "transition": 12,
        },
        "transition_orientation": "north_south",
        "variants": VARIANTS,
        "selected_seed": 2031659219,
        "selected_variant": 2031659219 % VARIANTS,
        "freeze_for_exchange": True,
        "composite_resolution": [WIDTH, HEIGHT],
        "tile_resolution": [TILE_WIDTH, TILE_HEIGHT],
        "tile_layout": [WIDTH // TILE_WIDTH, HEIGHT // TILE_HEIGHT],
        "generated_by": str(generator.relative_to(ROOT)),
        "generator_sha256": digest(generator),
        "source_manifest": str((SOURCE / "manifest.json").relative_to(ROOT)),
        "source_manifest_sha256": digest(SOURCE / "manifest.json"),
        "source_variants": source_variants,
        "composites": composites,
        "assets": assets,
    }
    (OUTPUT / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
