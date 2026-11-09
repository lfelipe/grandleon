#!/usr/bin/env python3
"""Focused checks for generated PlayStation presentation assets."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import struct
import re
import tempfile

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


def test_stage_palette_keeps_transparency_and_uses_8bpp_tim() -> None:
    opaque = Image.new("RGBA", (64, 64), (210, 48, 36, 255))
    cutout = opaque.copy()
    cutout.putpixel((0, 0), (0, 0, 0, 0))
    colours, palette = SCENE.shared_stage_palette([opaque, cutout])
    words = SCENE.tim_words(
        cutout, 320, 0, True, colours, palette, include_palette=True,
    )
    payload = struct.pack(f"<{len(words)}I", *words)

    magic, flags = struct.unpack_from("<II", payload)
    assert magic == 0x10
    assert flags == 0x9
    clut_size, clut_x, clut_y, clut_width, clut_height = struct.unpack_from(
        "<IHHHH", payload, 8,
    )
    assert (clut_x, clut_y, clut_width, clut_height) == (
        SCENE.CLUT_X, SCENE.CLUT_Y, 256, 1,
    )
    image_offset = 8 + clut_size
    _image_size, image_x, image_y, width_words, image_height = struct.unpack_from(
        "<IHHHH", payload, image_offset,
    )
    assert (image_x, image_y, width_words, image_height) == (320, 0, 32, 64)
    assert payload[image_offset + 12] == 0

    shared_words = SCENE.tim_words(
        opaque, 352, 0, False, colours, palette, include_palette=False,
    )
    shared_payload = struct.pack(f"<{len(shared_words)}I", *shared_words)
    assert struct.unpack_from("<II", shared_payload) == (0x10, 0x1)


def test_texture_packer_reserves_shared_clut_row() -> None:
    packer = SCENE.TexturePacker()
    placements = [packer.place(64, 64) for _ in range(60)]
    assert all(
        not (
            y <= SCENE.CLUT_Y < y + 64
            and x < SCENE.CLUT_X + SCENE.CLUT_COLOURS
            and x + 32 > SCENE.CLUT_X
        )
        for x, y in placements
    )


def test_four_bit_material_packs_texels_and_owns_palette() -> None:
    source = Image.new("RGBA", (32, 32), (90, 140, 70, 255))
    source.putpixel((0, 0), (0, 0, 0, 0))
    colours, palette = SCENE.shared_stage_palette([source])
    words = SCENE.tim_words(source, 320, 0, True, colours, palette,
                            depth_bits=4, palette_slot=2)
    payload = struct.pack(f"<{len(words)}I", *words)
    assert struct.unpack_from("<II", payload) == (0x10, 8)
    clut_size, x, y, width, height = struct.unpack_from("<IHHHH", payload, 8)
    assert (x, y, width, height) == (SCENE.CLUT_X + 32, SCENE.CLUT_Y - 1, 16, 1)
    image_offset = 8 + clut_size
    assert struct.unpack_from("<IHHHH", payload, image_offset)[3:] == (8, 32)
    assert payload[image_offset + 12] & 15 == 0
    assert payload[image_offset + 12] >> 4 != 0


def test_hidden_cover_faces_the_same_way_as_the_floor() -> None:
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory)
        lifts = {(x, y): 0 for y in range(-2, 5) for x in range(-2, 5)}
        SCENE.write_meshes(output, [((0, 0), [(0, 0, 9, True, 2, 2)])],
                           2, 2, [], {9: 1}, 0, "smooth", lifts, [0], [])
        generated = (output / "mesh_data.c").read_text()
        section = generated.split("k_visibility_vertices[] = {")[1].split("};")[0]
        vertices = [tuple(map(int, values)) for values in
                    re.findall(r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", section)]
        section = generated.split("k_visibility_triangles[] = {")[1].split("};")[0]
        for indices in re.findall(r"\.i0=(\d+), \.i1=(\d+), \.i2=(\d+)", section):
            a, b, c = (vertices[int(index)] for index in indices)
            # Cross-product Y must point upward; reversed covers disappear
            # under the production renderer's backface rejection.
            assert (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]) > 0


def test_interior_terrain_pages_overlap_the_visible_window() -> None:
    cells = [(x, y, 9, True, 1, 1) for y in range(14) for x in range(20)]
    for (start_x, start_y), page in SCENE.page_cells(cells, 20, 14, 0):
        xs, ys = [cell[0] for cell in page], [cell[1] for cell in page]
        if start_x > 0:
            assert min(xs) <= start_x - 2
        if start_y > 0:
            assert min(ys) <= start_y - 2


if __name__ == "__main__":
    test_cutout_keeps_grounding_inside_existing_texture()
    test_stage_palette_keeps_transparency_and_uses_8bpp_tim()
    test_texture_packer_reserves_shared_clut_row()
    test_four_bit_material_packs_texels_and_owns_palette()
    test_hidden_cover_faces_the_same_way_as_the_floor()
    test_interior_terrain_pages_overlap_the_visible_window()
    print("Mipsync scene generator presentation passed.")
