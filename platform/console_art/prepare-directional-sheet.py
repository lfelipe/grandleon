#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Normalize one generated compass sheet and derive its team-colour mask.

Directional authoring outputs are deliberately kept separate from the 4x2
combat sheet. This tool turns an accepted image-generation result into the
same exact 1772x886 / 443px-cell contract used by the rest of the roster and
reuses the reference body's reviewed mask recipe. It does not invent poses or
repair art: identity and pose review happen before this deterministic step.
"""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parents[2]
MASK_TOOL = ROOT / "platform/godot/scripts/generate-mythical-team-masks.py"
SHEET_SIZE = (1772, 886)
CELL = 443
ROWS = 2
COLS = 4
MIN_GUTTER = 3


def load_mask_tool():
    spec = importlib.util.spec_from_file_location("grandleon_team_masks", MASK_TOOL)
    if spec is None or spec.loader is None:
        raise ValueError(f"cannot load mask tool {MASK_TOOL}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def mask_config(reference_name: str):
    tool = load_mask_tool()
    for style in ("mythical", "scifi", *tool.CURATED_STYLES):
        configs, _names, _idle, _action = tool.style_settings(style)
        if reference_name in configs:
            return tool, configs[reference_name]
    raise ValueError(f"no reviewed team-mask recipe for {reference_name}")


def normalized(source: Image.Image) -> Image.Image:
    rgba = source.convert("RGBA")
    ratio = rgba.width / max(1, rgba.height)
    if not 1.8 <= ratio <= 2.2:
        raise ValueError(
            f"directional candidate is {rgba.size}, expected a roughly 2:1 4x2 sheet"
        )
    if rgba.size != SHEET_SIZE:
        rgba = rgba.resize(SHEET_SIZE, Image.Resampling.LANCZOS)
    pixels = np.asarray(rgba, dtype=np.uint8).copy()
    # Remove only effectively transparent generation noise. A hard cut at 24
    # retains ordinary antialiasing while making the empty cell gutters truly
    # empty for CI4 quantization.
    pixels[pixels[..., 3] < 24] = 0
    return Image.fromarray(pixels, mode="RGBA")


def cells(sheet: Image.Image) -> list[Image.Image]:
    return [
        sheet.crop((column * CELL, row * CELL, (column + 1) * CELL, (row + 1) * CELL))
        for row in range(ROWS)
        for column in range(COLS)
    ]


def cell_metrics(sheet: Image.Image) -> tuple[list[float], list[float]]:
    occupancy: list[float] = []
    differences: list[float] = []
    frames = cells(sheet)
    for index, frame in enumerate(frames):
        alpha = frame.getchannel("A")
        box = alpha.point(lambda value: 255 if value >= 24 else 0).getbbox()
        if box is None:
            raise ValueError(f"directional cell {index} is empty")
        left, top, right, bottom = box
        if (
            left < MIN_GUTTER or top < MIN_GUTTER
            or CELL - right < MIN_GUTTER or CELL - bottom < MIN_GUTTER
        ):
            raise ValueError(
                f"directional cell {index} breaches the {MIN_GUTTER}px safety gutter: {box}"
            )
        opaque = np.asarray(alpha, dtype=np.uint8) >= 24
        occupancy.append(float(opaque.sum()) / float(CELL * CELL))

    # Idle A/B may be subtle, but each walk cell must materially differ from
    # its direction's first idle. North occupies indices 0..3 and south 4..7.
    for idle, walk in ((0, 2), (0, 3), (4, 6), (4, 7)):
        difference = ImageChops.difference(frames[idle], frames[walk]).convert("RGBA")
        changed = np.asarray(difference, dtype=np.uint8).max(axis=2) >= 16
        ratio = float(changed.sum()) / float(CELL * CELL)
        if ratio < 0.025:
            raise ValueError(
                f"walk cell {walk} differs from idle cell {idle} by only {ratio:.2%}"
            )
        differences.append(ratio)
    return occupancy, differences


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.input.is_file():
        raise FileNotFoundError(args.input)
    if not args.reference.is_file():
        raise FileNotFoundError(args.reference)
    if args.output.exists():
        raise ValueError(f"refusing to overwrite existing asset {args.output}")
    if not args.output.name.endswith("_directions_hd.png"):
        raise ValueError("directional output must end in _directions_hd.png")

    with Image.open(args.input) as candidate:
        sheet = normalized(candidate)
    occupancy, differences = cell_metrics(sheet)
    tool, config = mask_config(args.reference.name)
    mask = tool.generate_mask(sheet, config)
    coverage = tool.validate_mask(args.output, sheet, mask, config)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output, optimize=True)
    mask_output = args.output.with_name(f"{args.output.stem}_team_mask.png")
    mask.save(mask_output, optimize=True)
    print(
        f"wrote {args.output} ({args.output.stat().st_size} bytes); "
        f"mask {mask_output.stat().st_size} bytes"
    )
    print("occupancy " + " ".join(f"{value:.1%}" for value in occupancy))
    print("walk-delta " + " ".join(f"{value:.1%}" for value in differences))
    print("mask-coverage " + " ".join(f"{value:.1%}" for value in coverage))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError) as error:
        print(f"directional sheet preparation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
