#!/usr/bin/env python3
"""Focused checks for generated PlayStation presentation assets."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
SPEC = spec_from_file_location("grandleon_mipsync_scene", ROOT / "tools/generate_scene.py")
assert SPEC is not None and SPEC.loader is not None
SCENE = module_from_spec(SPEC)
SPEC.loader.exec_module(SCENE)


def test_cutout_keeps_grounding_inside_existing_texture() -> None:
    source = Image.new("RGBA", (8, 20), (0, 0, 0, 0))
    ImageDraw.Draw(source).rectangle((2, 0, 5, 18), fill=(220, 180, 120, 255))

    cutout = SCENE.fitted_cutout(source, 64)

    assert cutout.size == (64, 64)
    # The narrow figure leaves the sides of the baked contact oval visible.
    assert cutout.getpixel((16, 59))[:3] == (20, 23, 26)
    assert cutout.getpixel((16, 59))[3] == 255
    # Its perimeter is stippled rather than a second hard-edged platform.
    edge_alpha = {cutout.getpixel((x, 57))[3] for x in range(12, 52)}
    assert edge_alpha == {0, 255}


if __name__ == "__main__":
    test_cutout_keeps_grounding_inside_existing_texture()
    print("Mipsync scene generator presentation passed.")
