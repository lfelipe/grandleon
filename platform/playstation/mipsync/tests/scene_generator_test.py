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


def test_stone_keep_exports_solid_terrace_with_local_sorting_faces() -> None:
    # Exercise the exported Godot keep through the actual PS1 mesh builder.
    # Large roof triangles otherwise let courtyard tiles paint over the roof.
    parts = SCENE.load_keep(ROOT.parents[2], "ps1")
    terrace = next(part for part in parts if min(part["size"][0], part["size"][2]) > 4)
    vertices, uvs, triangles = [], [], []
    lifts = {(x, y): 0 for y in range(14) for x in range(20)}
    SCENE.append_keep_part(vertices, uvs, triangles, terrace, 0, 0, 4, lifts, 20, 14)
    top = max(point[1] for point in vertices)
    top_faces = [face for face in triangles if all(vertices[i][1] == top for i in face[:3])]
    area = 0
    for face in top_faces:
        a, b, c = [vertices[i] for i in face[:3]]
        cross = (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])
        assert cross < 0, "terrace must face upward after the PS1 Z conversion"
        area += abs(cross) / 2
        for axis in (0, 2):
            assert max(vertices[i][axis] for i in face[:3]) - min(vertices[i][axis] for i in face[:3]) <= 132
        uv = [uvs[i] for i in face[:3]]
        assert (uv[1][0] - uv[0][0]) * (uv[2][1] - uv[0][1]) != (uv[1][1] - uv[0][1]) * (uv[2][0] - uv[0][0]), "roof UVs must span an area rather than a diagonal stripe"
    expected = (round(terrace["size"][0] * 64) * 2) ** 2
    assert area == expected, "stone terrace must cover its whole footprint without holes"


def test_console_font_has_complete_pixel_cells() -> None:
    atlas = Image.open(ROOT.parents[2] / "assets/fonts/spleen/console-6x12.png").convert("RGBA")
    assert atlas.size == (64, 96)
    alpha = atlas.getchannel("A").histogram()
    assert alpha[0] and alpha[255] and not any(alpha[1:255])
    for code in range(32, 96):
        slot = code - 32
        cell = atlas.crop((slot % 8 * 8, slot // 8 * 12,
                           slot % 8 * 8 + 8, slot // 8 * 12 + 12))
        # The next character starts at +7: that overlap column must be blank.
        assert not cell.crop((7, 0, 8, 12)).getbbox(), chr(code)
        if code != 32:
            assert cell.getbbox(), chr(code)
    # Wide letters must retain both stems rather than spill into the next cell.
    slot = ord("W") - 32
    w = atlas.crop((slot % 8 * 8, slot // 8 * 12, slot % 8 * 8 + 8, slot // 8 * 12 + 12))
    assert w.getbbox() == (1, 1, 6, 9)


def test_accented_font_uses_complete_cells_and_dedicated_four_bit_clut() -> None:
    atlas = Image.open(ROOT.parents[2] / "assets/fonts/spleen/console-latin-6x12.png").convert("RGBA")
    assert atlas.size == (64, 144)
    codes = list(range(32, 96)) + [value for value in range(0xc0, 0xdf) if value != 0xd7] + [0xa1, 0xbf]
    cells = {}
    for slot, code in enumerate(codes):
        cell = atlas.crop((slot % 8 * 8, slot // 8 * 12, slot % 8 * 8 + 8, slot // 8 * 12 + 12))
        assert not cell.crop((7, 0, 8, 12)).getbbox()
        if code != 32:
            assert cell.getbbox(), code
        cells[code] = cell
    for accent, plain in ((0xc7, ord('C')), (0xc3, ord('A')), (0xc9, ord('E')), (0xd1, ord('N')), (0xdc, ord('U'))):
        assert cells[accent].tobytes() != cells[plain].tobytes()
    colours, palette = SCENE.shared_stage_palette([atlas])
    words = SCENE.tim_words(atlas, 320, 0, True, colours, palette,
                            include_palette=False, depth_bits=4, font_palette=True)
    data = struct.pack(f"<{len(words)}I", *words)
    assert struct.unpack_from('<II', data) == (0x10, 8)
    assert struct.unpack_from('<IHHHH', data, 8) == (44, SCENE.CLUT_X, SCENE.CLUT_Y - 3, 16, 1)
    assert struct.unpack_from('<16H', data, 20) == (0,) + (0x7fff,) * 15
    assert struct.unpack_from('<IHHHH', data, 52) == (4620, 320, 0, 16, 144)
    texels = [value for byte in data[64:] for value in (byte & 15, byte >> 4)]
    assert set(texels) == {0, 1}
    alpha = atlas.getchannel('A')
    flattened = getattr(alpha, 'get_flattened_data', None)
    alpha_values = flattened() if flattened is not None else alpha.getdata()
    assert sum(value != 0 for value in texels) == sum(value != 0 for value in alpha_values)
    assert len(data) == 4672


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


def test_forest_variation_preserves_topology_and_rooted_trunks() -> None:
    profiles = set()
    palettes = set()
    counts = set()
    for x in range(12):
        roll = SCENE.hash_cell(x, 3, 0x47A4D1E)
        vertices, uvs, triangles = [], [], []
        SCENE.append_terrain_form(vertices, uvs, triangles, 2, 0, 7, 0,
                                  roll, {-1: 1, -2: 2, -3: 3})
        counts.add((len(vertices), len(uvs), len(triangles)))
        profiles.add(tuple(vertices))
        palettes.add(tuple(face[5:] for face in triangles if face[3] == 2))
        # Trunks retain their fixed footprint and contact with the ground;
        # shape variation must not turn them into floating or moving props.
        assert all(vertex[1] == 8 for vertex in vertices[:4])
        assert all(abs(vertex[0]) == 6 and abs(vertex[2]) == 6
                   for vertex in vertices[:4])
        # Baked lighting must keep every trunk face on the bark material,
        # including its brighter facets. The upper crown forms a tip.
        assert all(face[3] == 1 and face[5] > face[6]
                   for face in triangles[:4])
        assert vertices[16][1] > vertices[11][1]
        for triangle in triangles:
            if triangle[3] != 2:
                continue
            a, b, c = (vertices[index] for index in triangle[:3])
            # Both crowns must face outward/upward after changing topology;
            # reversed winding makes foliage disappear under backface culling.
            normal_y = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2])
            assert normal_y > 0
        assert all(0 <= component < 32 for uv in uvs for component in uv)
    assert len(profiles) > 6
    assert len(palettes) >= 3
    # Variation uses the existing trunk and two canopies, with no additional
    # vertices, texture references or triangles in a resident terrain page.
    assert counts == {(17, 17, 14)}


def test_fractured_rock_has_grounded_volume_and_outward_faces() -> None:
    for x in range(8):
        vertices, uvs, triangles = [], [], []
        SCENE.append_terrain_form(vertices, uvs, triangles, 3, 0, 9, 0,
                                  SCENE.hash_cell(x, 4, 42), {-1: 1, -2: 2, -3: 3})
        assert len(vertices) <= 15 and len(triangles) == 12
        assert all(vertices[i][1] == 10 for i in range(4))
        assert len({vertices[i][1] for i in range(4, 7)}) == 3
        for triangle in triangles:
            assert triangle[3] == 3
            a, b, c = (vertices[index] for index in triangle[:3])
            normal_y = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2])
            assert normal_y > 0


if __name__ == "__main__":
    test_stone_keep_exports_solid_terrace_with_local_sorting_faces()
    test_console_font_has_complete_pixel_cells()
    test_accented_font_uses_complete_cells_and_dedicated_four_bit_clut()
    test_cutout_keeps_grounding_inside_existing_texture()
    test_stage_palette_keeps_transparency_and_uses_8bpp_tim()
    test_texture_packer_reserves_shared_clut_row()
    test_four_bit_material_packs_texels_and_owns_palette()
    test_hidden_cover_faces_the_same_way_as_the_floor()
    test_interior_terrain_pages_overlap_the_visible_window()
    test_forest_variation_preserves_topology_and_rooted_trunks()
    test_fractured_rock_has_grounded_volume_and_outward_faces()
    print("Mipsync scene generator presentation passed.")
