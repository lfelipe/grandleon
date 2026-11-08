#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Audit authored compass coverage in the curated Godot/console roster."""

from __future__ import annotations

import json
from pathlib import Path
import sys

from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parents[2]
PROFILE_PATH = ROOT / "platform/godot/project/content/graphics_profiles.json"
PROJECT_ROOT = ROOT / "platform/godot/project"
EXPECTED_BINDINGS = 7 * 2 * 8
EXPECTED_SHEET_SIZE = (1772, 886)
EXPECTED_MOVE_FRAMES = [2, 3]
DIRECTIONAL_WALK_FRAMES = {"north": [2, 3], "south": [6, 7]}
DIRECTIONAL_COVERAGE = {
    "archer|*|medieval|first",
    "archer|*|scifi|second",
    "archer|*|sengoku|first",
    "archer|*|sengoku|second",
    "archer|*|undead|second",
    "beast|*|medieval|first",
    "beast|*|mythical|second",
    "beast|*|nature|first",
    "beast|*|nature|second",
    "beast|*|scifi|first",
    "beast|*|medieval|second",
    "commander|*|medieval|second",
    "commander|*|medieval|first",
    "commander|*|mythical|second",
    "commander|*|scifi|second",
    "commander|*|nature|first",
    "healer|*|medieval|second",
    "healer|*|mythical|first",
    "healer|*|nature|first",
    "healer|*|pirates|first",
    "healer|*|scifi|second",
    "healer|*|sengoku|first",
    "healer|*|undead|first",
    "healer|*|undead|second",
    "knight|*|medieval|first",
    "knight|*|medieval|second",
    "knight|*|mythical|second",
    "knight|*|nature|second",
    "knight|*|pirates|second",
    "knight|*|scifi|first",
    "knight|*|scifi|second",
    "knight|*|sengoku|first",
    "knight|*|undead|second",
    "mage|*|medieval|second",
    "mage|*|mythical|first",
    "mage|*|mythical|second",
    "mage|*|nature|first",
    "mage|*|pirates|second",
    "mage|*|sengoku|second",
    "rogue|*|nature|second",
    "rogue|*|pirates|first",
    "rogue|*|pirates|second",
    "rogue|*|sengoku|first",
    "rogue|*|undead|second",
    "stormcaller|*|medieval|first",
    "stormcaller|*|pirates|first",
    "stormcaller|*|undead|first",
}
CI4_FRAME_BYTES = 64 * 64 // 2 + 16 * 2


def fail(message: str) -> None:
    raise ValueError(message)


def resolved_resource(value: object) -> Path:
    path = str(value)
    if not path.startswith("res://"):
        fail(f"curated resource is not project-relative: {path!r}")
    return PROJECT_ROOT / path.removeprefix("res://")


def sheet_frame(sheet: Image.Image, index: int) -> Image.Image:
    left = index % 4 * 443
    top = index // 4 * 443
    return sheet.crop((left, top, left + 443, top + 443))


def main() -> int:
    document = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))
    bindings = document["profiles"]["voxel-sprites"]["assets"]["characterSprites"]
    if len(bindings) != EXPECTED_BINDINGS:
        fail(
            f"expected {EXPECTED_BINDINGS} curated identities, found {len(bindings)}"
        )

    inspected_sources: set[Path] = set()
    asymmetric_move_frames = 0
    directional_source_bytes = 0
    directional_mask_bytes = 0
    for key, binding in bindings.items():
        if binding.get("columns") != 4 or binding.get("rows") != 2:
            fail(f"{key}: curated sheet is no longer the audited 4x2 layout")
        if binding.get("directionalFlip") is not True:
            fail(f"{key}: east/west mirroring is not enabled")
        if binding.get("sequences", {}).get("move", {}).get("frames") != EXPECTED_MOVE_FRAMES:
            fail(f"{key}: walk frames are not the curated cells 2 and 3")
        directional = binding.get("directionalSheet")
        if key in DIRECTIONAL_COVERAGE and not isinstance(directional, dict):
            fail(f"{key}: representative identity has no directional sheet")
        if key not in DIRECTIONAL_COVERAGE and directional is not None:
            fail(f"{key}: directional coverage changed without updating the audit")
        if isinstance(directional, dict):
            if directional.get("columns") != 4 or directional.get("rows") != 2:
                fail(f"{key}: directional sheet is not a 4x2 grid")
            headings = directional.get("headings", {})
            for heading, expected_frames in DIRECTIONAL_WALK_FRAMES.items():
                if headings.get(heading, {}).get("move", {}).get("frames") != expected_frames:
                    fail(f"{key}: {heading} walk cells are not {expected_frames}")
            directional_source = resolved_resource(directional.get("path"))
            directional_mask = resolved_resource(directional.get("teamMaskPath"))
            for resource in (directional_source, directional_mask):
                if not resource.is_file():
                    fail(f"{key}: missing directional resource {resource.name}")
                with Image.open(resource) as image:
                    if image.size != EXPECTED_SHEET_SIZE:
                        fail(
                            f"{key}: {resource.name} is {image.size}, expected "
                            f"{EXPECTED_SHEET_SIZE}"
                        )
            directional_source_bytes += directional_source.stat().st_size
            directional_mask_bytes += directional_mask.stat().st_size
            with Image.open(directional_source) as sheet:
                rgba = sheet.convert("RGBA")
                north = rgba.crop((0, 0, 443, 443))
                south = rgba.crop((0, 443, 443, 886))
                if ImageChops.difference(north, south).getbbox() is None:
                    fail(f"{key}: north and south silhouettes are identical")
                for frames in DIRECTIONAL_WALK_FRAMES.values():
                    first = sheet_frame(rgba, frames[0])
                    second = sheet_frame(rgba, frames[1])
                    if ImageChops.difference(first, second).getbbox() is None:
                        fail(f"{key}: directional walk phases are identical")

        source = resolved_resource(binding.get("path"))
        mask = resolved_resource(binding.get("teamMaskPath"))
        for resource in (source, mask):
            if not resource.is_file():
                fail(f"{key}: missing curated resource {resource.name}")
            with Image.open(resource) as image:
                if image.size != EXPECTED_SHEET_SIZE:
                    fail(
                        f"{key}: {resource.name} is {image.size}, expected "
                        f"{EXPECTED_SHEET_SIZE}"
                    )

        if source in inspected_sources:
            continue
        inspected_sources.add(source)
        with Image.open(source) as sheet:
            rgba = sheet.convert("RGBA")
            for frame in EXPECTED_MOVE_FRAMES:
                left = frame % 4 * 443
                top = frame // 4 * 443
                cell = rgba.crop((left, top, left + 443, top + 443))
                mirrored = cell.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
                if ImageChops.difference(cell, mirrored).getbbox() is not None:
                    asymmetric_move_frames += 1

    expected_sources = 7 * 2 * 8
    if len(inspected_sources) != expected_sources:
        fail(
            f"expected {expected_sources} distinct body sheets, found "
            f"{len(inspected_sources)}"
        )
    if asymmetric_move_frames != expected_sources * len(EXPECTED_MOVE_FRAMES):
        fail("one or more walk cells is horizontally symmetric and cannot show a turn")

    console_bytes = len(DIRECTIONAL_COVERAGE) * 4 * CI4_FRAME_BYTES
    full_console_bytes = EXPECTED_BINDINGS * 4 * CI4_FRAME_BYTES
    print(
        "directional art audit passed: "
        f"{len(inspected_sources)} bodies, {asymmetric_move_frames} asymmetric "
        f"east/west walk cells; authored north/south {len(DIRECTIONAL_COVERAGE)}/"
        f"{EXPECTED_BINDINGS} bodies ({EXPECTED_BINDINGS - len(DIRECTIONAL_COVERAGE)} open)"
    )
    print(
        "directional budgets: "
        f"Godot PNG {directional_source_bytes + directional_mask_bytes} bytes "
        f"({directional_source_bytes} source + {directional_mask_bytes} masks); "
        f"console CI4 projection {console_bytes} bytes/platform, "
        f"full roster {full_console_bytes} bytes/platform, "
        f"{CI4_FRAME_BYTES} bytes/frame including palette"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"directional art audit failed: {error}", file=sys.stderr)
        raise SystemExit(1)
