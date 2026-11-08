#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Audit the directional coverage of the curated Godot/console roster.

This check intentionally does not generate substitute north/south frames. It
pins what the shared source actually contains, so the renderer may safely use
mirrored east/west movement without documentation claiming four authored
views. When real compass frames are added, this check is the place that must
change alongside their profile metadata and console budget measurements.
"""

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


def fail(message: str) -> None:
    raise ValueError(message)


def resolved_resource(value: object) -> Path:
    path = str(value)
    if not path.startswith("res://"):
        fail(f"curated resource is not project-relative: {path!r}")
    return PROJECT_ROOT / path.removeprefix("res://")


def main() -> int:
    document = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))
    bindings = document["profiles"]["voxel-sprites"]["assets"]["characterSprites"]
    if len(bindings) != EXPECTED_BINDINGS:
        fail(
            f"expected {EXPECTED_BINDINGS} curated identities, found {len(bindings)}"
        )

    inspected_sources: set[Path] = set()
    asymmetric_move_frames = 0
    for key, binding in bindings.items():
        if binding.get("columns") != 4 or binding.get("rows") != 2:
            fail(f"{key}: curated sheet is no longer the audited 4x2 layout")
        if binding.get("directionalFlip") is not True:
            fail(f"{key}: east/west mirroring is not enabled")
        if binding.get("sequences", {}).get("move", {}).get("frames") != EXPECTED_MOVE_FRAMES:
            fail(f"{key}: walk frames are not the curated cells 2 and 3")
        # There is deliberately no accepted spelling hidden here. Adding any
        # compass metadata without teaching this audit/runtime about it is a
        # contract change and must fail visibly.
        unexpected = {
            name for name in binding
            if "direction" in name.lower() and name != "directionalFlip"
        }
        if unexpected:
            fail(f"{key}: unaudited directional metadata {sorted(unexpected)}")

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

    print(
        "directional art audit passed: "
        f"{len(inspected_sources)} bodies, {asymmetric_move_frames} asymmetric "
        "walk cells; east/west mirror-ready, north/south source frames absent"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"directional art audit failed: {error}", file=sys.stderr)
        raise SystemExit(1)
