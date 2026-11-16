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


def test_masonry_keeps_one_texture_tile_per_board_cell() -> None:
    # Paving and ruins must not stretch across merged rows. Plain ground can.
    for kind in (8, 12):
        cells = [(0, 0, kind, True, 1, 1), (0, 1, kind, True, 1, 1)]
        assert SCENE.coalesce_vertical_runs(cells, {(x, y): 0 for x in range(2) for y in range(3)}) == cells
    grass = [(0, 0, 9, True, 1, 1), (0, 1, 9, True, 1, 1)]
    assert SCENE.coalesce_vertical_runs(grass, {(x, y): 0 for x in range(2) for y in range(3)}) == [(0, 0, 9, True, 1, 2)]
    source = SCENE.ground_image(ROOT.parents[2], 12)
    image = SCENE.terrain_image(ROOT.parents[2], 12)
    assert image.crop((0, 0, 32, 32)).tobytes() == source.resize((32, 32), Image.Resampling.LANCZOS).tobytes()


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


def test_continuous_floor_covers_every_playable_tile_outside_scenery_pages() -> None:
    # Movement ranges can extend beyond the nearest scenery page. Floor
    # triangles must cover the entire board, independently of that page.
    cells = [(x, y, 9, True, 1, 1) for y in range(12) for x in range(18)]
    lifts = {(x, y): 0 for y in range(12) for x in range(18)}
    with tempfile.TemporaryDirectory() as folder:
        SCENE.write_meshes(Path(folder), [((0, 0), cells)] + SCENE.page_cells(cells, 18, 12, 0),
                           18, 12, [], {9: 1}, 0, "smooth", lifts, [], [], surface_count=1)
        generated = (Path(folder) / "mesh_data.c").read_text()
        body = generated.split("k_board_0_vertices[] = {")[1].split("};")[0]
        points = [tuple(map(int, m)) for m in re.findall(r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", body)]
        faces = generated.split("k_board_0_triangles[] = {")[1].split("};")[0]
        area = 0
        for a, b, c in re.findall(r"\.i0=(\d+), \.i1=(\d+), \.i2=(\d+)", faces):
            pa, pb, pc = (points[int(i)] for i in (a, b, c))
            area += abs((pb[0] - pa[0]) * (pc[2] - pa[2]) - (pb[2] - pa[2]) * (pc[0] - pa[0])) / 2
        assert area == 18 * 12 * 128 * 128
        assert min(p[0] for p in points) == -18 * 64
        assert max(p[0] for p in points) == 18 * 64
        assert min(p[2] for p in points) == -12 * 64
        assert max(p[2] for p in points) == 12 * 64


def test_large_paved_floor_is_complete_without_an_oversized_mesh() -> None:
    width, height = 32, 32
    cells = [(x, y, 12, True, 1, 1) for y in range(height) for x in range(width)]
    strips = SCENE.surface_strips(cells, width, height, 0)
    assert len(strips) > 1
    covered = []
    for _origin, patch in strips:
        corners = set()
        for x, y, _kind, _authored, sx, sy in patch:
            covered.extend((xx, yy) for yy in range(y, y + sy) for xx in range(x, x + sx))
            corners.update(((x, y), (x + sx, y), (x, y + sy), (x + sx, y + sy)))
        assert len(corners) <= SCENE.MAX_CACHE_VERTICES
    assert len(covered) == width * height
    assert set(covered) == {(x, y) for y in range(height) for x in range(width)}
    with tempfile.TemporaryDirectory() as folder:
        metadata = SCENE.write_meshes(Path(folder), strips, width, height, [], {12: 1}, 0, "smooth",
                                      {(x, y): 0 for y in range(height) for x in range(width)}, [], [], surface_count=len(strips))
        assert len(metadata[2]) == len(strips)


def test_scenery_cache_counts_only_simultaneously_enabled_instances() -> None:
    groups = {"test_prop": ([None] * 40, [], [None] * 20)}
    far_apart = [(0, 0, "test_prop", 1), (19, 13, "test_prop", 1)]
    assert SCENE.scenery_cache_peak(far_apart, groups, 20, 14) == (40, 20)
    nearby = far_apart + [(1, 0, "test_prop", 1)]
    assert SCENE.scenery_cache_peak(nearby, groups, 20, 14) == (80, 40)


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


def test_scenery_compaction_preserves_drawn_faces_and_texture_seams() -> None:
    def drawn_faces(vertices, uvs, triangles):
        return [(tuple((vertices[i], uvs[i] if face[3] else None)
                       for i in face[:3]), face[3:]) for face in triangles]

    models = SCENE.load_settlement_models(ROOT.parents[2])
    savings = 0
    for kind in models:
        vertices, uvs, triangles = [], [], []
        SCENE.append_settlement_model(vertices, uvs, triangles, kind, models, {-3: 7, -4: 8})
        compact = SCENE.compact_flat_mesh(vertices, uvs, triangles)
        assert drawn_faces(*compact) == drawn_faces(vertices, uvs, triangles), kind
        savings += len(vertices) - len(compact[0])
    assert savings > 0, "the real scenery should project fewer vertices"

    # Coincident textured vertices with different UVs remain distinct, while
    # differing per-face colours survive sharing untextured positions.
    vertices = [(0, 0, 0, 0), (10, 0, 0, 0), (0, 10, 0, 0)] * 2
    uvs = [(0, 0), (31, 0), (0, 31), (64, 0), (95, 0), (64, 31)]
    for texture in (0, 7):
        triangles = [(0, 1, 2, texture, 255, 90, 80, 70),
                     (3, 4, 5, texture, 255, 10, 20, 30)]
        compact = SCENE.compact_flat_mesh(vertices, uvs, triangles)
        assert drawn_faces(*compact) == drawn_faces(vertices, uvs, triangles)
        assert len(compact[0]) == (3 if texture == 0 else 6)


def test_foliage_pages_preserve_every_face_and_leave_trunks_static() -> None:
    from collections import Counter
    with tempfile.TemporaryDirectory() as folder:
        output = Path(folder)
        metadata = []
        SCENE.write_meshes(
            output, [((0, 0), [(0, 0, 2, True, 4, 4)]),
                     ((10, 10), [(10, 10, 2, False, 1, 1)])],
            4, 4, [(1, 1, 2)], {2: 1, -1: 2, -2: 3, -3: 4},
            0, "flat", {}, [], [], foliage_metadata=metadata)
        emitted = (output / "mesh_data.c").read_text()
        assert len(metadata) == 2 and metadata[0] > 0 and metadata[1] == 0

        def read_mesh(name):
            def body(suffix):
                return emitted.split(name + suffix + "[] = {")[1].split("};")[0]
            vertices = [tuple(map(int, match)) for match in re.findall(
                r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", body("_vertices"))]
            uvs = [tuple(map(int, match)) for match in re.findall(
                r"\{\s*(\d+),\s*(\d+)\s*\}", body("_uvs"))]
            triangles = []
            for line in body("_triangles").splitlines():
                fields = dict(re.findall(r"\.(\w+)\s*=\s*(-?\d+)", line))
                if fields:
                    triangles.append(tuple(int(fields[key]) for key in
                        ("i0", "i1", "i2", "texture_index", "shade", "color_r", "color_g", "color_b")))
            return vertices, uvs, triangles

        def faces(mesh, materials):
            verts, uvs, tris = mesh
            return Counter((tuple((verts[i], uvs[i]) for i in tri[:3]), tri[3:])
                           for tri in tris if tri[3] in materials)

        roll = SCENE.hash_cell(1, 1, 0)
        cx = -64 + (26 if roll & 1 else -26)
        cz = 64 + (22 if roll & 2 else -22)
        original = ([], [], [])
        SCENE.append_terrain_form(*original, 2, cx, 0, cz, roll,
                                  {2: 1, -1: 2, -2: 3, -3: 4})
        board = read_mesh("k_board_0")
        foliage = read_mesh("k_foliage_0")
        assert {tri[3] for tri in foliage[2]} == {3}
        assert all(tri[3] != 3 for tri in board[2])
        assert faces(board, {2}) == faces(original, {2}), "trunk geometry must stay planted"
        assert faces(foliage, {3}) == faces(original, {3}), "wind must retain every original leaf face and UV"
        assert not any("k_foliage_1" in line for line in emitted.splitlines())


def test_camera_pages_keep_separate_fixed_canopy_meshes() -> None:
    # Mipsync reserves an entity's geometry cache from its first mesh. A
    # camera sweep must not reuse a small-page reservation for a larger crown.
    with tempfile.TemporaryDirectory() as folder:
        output = Path(folder)
        unit = dict(id=1, x=0, y=0, side=0, style=0, archetype=0, figure=0, colour=0)
        document = dict(width=4, height=4, units=[unit], simulation={},
                        visual_visibility=dict(hidden_cells=[], reveal_radius=1))
        chunks = [(2, 0, 0, 4, 4, 0, 0), (3, 2, 0, 6, 4, 2, 0)]
        count = SCENE.write_scene(output, document, {(0, 0, 0, 0): tuple(range(1, 10))},
                                 chunks, 1, [0] * 16, (0, 0, 0), foliage_metadata=[9, 10],
                                 water_texture_index=7)
        assert "#define GRANDLEON_WATER_TEXTURE_INDEX 7u" in (output / "grandleon_scene.h").read_text()
        emitted = (output / 'scene_data.c').read_text()
        canopies = [line for line in emitted.splitlines() if '.name="forest_canopies"' in line]
        assert len(canopies) == 2 and count == 9
        assert [int(re.search(r'\.mesh_index=(\d+)', line)[1]) for line in canopies] == [9, 10]
        assert all('.mesh_enabled=0' in line for line in canopies)
        bindings = emitted.split('g_grandleon_foliage_entity_indices[] = {')[1].split('};')[0]
        assert [int(value) for value in re.findall(r'(\d+)u', bindings)] == [6, 7]
        count = SCENE.write_scene(output, document, {(0, 0, 0, 0): tuple(range(1, 10))},
                                 chunks, 1, [0] * 16, (0, 0, 0))
        assert count == 7 and 'forest_canopies' not in (output / 'scene_data.c').read_text()
        assert "#define GRANDLEON_WATER_TEXTURE_INDEX 0u" in (output / "grandleon_scene.h").read_text()


def test_geometry_pool_covers_both_complete_cursor_outlines() -> None:
    with tempfile.TemporaryDirectory() as folder:
        output = Path(folder)
        counts = SCENE.write_meshes(output, [((0, 0), [(0, 0, 9, True, 2, 2)])],
                                    2, 2, [], {9: 1}, 0, "smooth",
                                    {(x, y): 0 for y in range(2) for x in range(2)}, [], [])
        emitted = (output / 'mesh_data.c').read_text()
        def count(name):
            body = emitted.split(name + '[] = {')[1].split('};')[0]
            return len(re.findall(r'^\s*\{', body, re.MULTILINE))
        for index, suffix in enumerate(('vertices', 'triangles')):
            world = max(count('k_board_0_' + suffix), count('k_combat_floor_' + suffix))
            assert counts[index] >= world + 2 * count('k_cursor_' + suffix)


def test_themed_houses_and_grove_keep_distinct_texture_bindings() -> None:
    with tempfile.TemporaryDirectory() as folder:
        textures, _, _, _, _ = SCENE.write_textures(
            Path(folder), ROOT.parents[2], [9], [], houses=True, grove=True,
            house_drawings=tuple(range(16)))
        selected = []
        for kind in [f"house_cutout_{drawing}_{drawing % 2}" for drawing in range(16)] + ["grove_cutout"]:
            vertices, uvs, faces = [], [], []
            SCENE.append_settlement_model(vertices, uvs, faces, kind, {}, textures)
            assert len(vertices) == 4 and len(faces) == 2
            assert max(u for u, _v in uvs) == 95
            selected.append(faces[0][3])
        assert len(set(selected)) == 17, "a new house must never alias the grove texture"
        emitted = (Path(folder) / "textures_data.c").read_text()
        rows = []
        for drawing in range(16):
            body = re.search(r"k_scenery_house_%d_tim\[\] = \{(.*?)\};" % drawing, emitted, re.S).group(1)
            words = [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", body)]
            payload = struct.pack(f"<{len(words)}I", *words)
            assert struct.unpack_from('<II', payload) == (0x10, 9)
            length, x, y, width, height = struct.unpack_from('<IHHHH', payload, 8)
            assert (length, x, width, height) == (524, SCENE.CLUT_X, 256, 1)
            assert 480 <= y <= SCENE.CLUT_Y - 4
            rows.append(y)
            image_offset = 8 + length
            _, px, py, image_width, image_height = struct.unpack_from('<IHHHH', payload, image_offset)
            assert (image_width, image_height) == (48, 96)
            assert not (py <= y < py + image_height and px + image_width > x)
            source = SCENE.house_image(ROOT.parents[2], drawing, 96)
            texels = payload[image_offset + 12:image_offset + 12 + 96 * 96]
            assert sum(value != 0 for value in texels) == sum(pixel[3] >= 40 for pixel in source.getdata())
        assert len(set(rows)) == 16, "house palettes must not overwrite one another or UI palettes"


def test_house_only_bank_does_not_require_a_shared_unit_palette() -> None:
    with tempfile.TemporaryDirectory() as folder:
        _, _, _, metrics, _ = SCENE.write_textures(Path(folder), ROOT.parents[2], [9], [],
                                                 houses=True, house_drawings=(8, 10))
        assert metrics["house_palette_count"] == 2
        emitted = (Path(folder) / "textures_data.c").read_text()
        for body in re.findall(r"static const uint32_t k_.*?_tim\[\] = \{(.*?)\};", emitted, re.S):
            words = [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", body)]
            assert words[1] & 8, "a bank with only independently paletted textures must own every CLUT"


def test_river_crossings_keep_water_and_a_narrow_raised_deck() -> None:
    # Full cells remain traversable roads in the simulation. The presentation
    # must keep water on both sides of a smaller timber crossing.
    terrain = [9,0,9, 1,1,1, 9,0,9]
    assert SCENE.river_crossings(terrain,3,3) == {(1,1):1}
    assert SCENE.river_crossings([9,1,9, 0,1,0, 9,1,9],3,3) == {(1,1):2}
    assert SCENE.river_crossings([9,0,9, 9,1,1, 9,9,9],3,3) == {}, "one bank is not a bridge"
    for axis in [1,2]:
        vertices,uvs,faces = [],[],[]
        SCENE.append_river_bridge(vertices,uvs,faces,1,1,axis,3,3,6,2)
        assert len(vertices)==len(uvs) and faces
        deck=[face for face in faces if all(vertices[i][1] == 6 for i in face[:3])]
        assert deck, "the walking deck is above water"
        perpendicular=2 if axis==1 else 0
        assert max(abs(vertices[i][perpendicular]) for f in faces for i in f[:3]) < 32, "water shoulders remain visible instead of a full-width dam"
        assert all(min(face[:3])>=0 and max(face[:3])<len(vertices) for face in faces)
        assert max(v[0 if axis==1 else 2] for v in vertices)-min(v[0 if axis==1 else 2] for v in vertices)>=128, "deck reaches both banks"


def test_modular_castle_uses_authored_walls_and_independent_roof_cells() -> None:
    source = ROOT.parents[2] / "games/steel_and_cedar/prototypes/castle_expansion.py"
    spec = spec_from_file_location("castle_prototype", source)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    layout = module.create_project()["maps"][0]
    identities = [SCENE.content_id(tile) for tile in layout["terrain"]]
    hidden = [cell["y"] * 32 + cell["x"] for cell in layout["visualVisibility"]["hiddenCells"]]
    placements = SCENE.modular_castle_placements(identities,32,24,hidden,SCENE.content_id)
    roofs = {(x,y) for x,y,kind,_size in placements if kind == "castle_roof"}
    assert {y*32+x for x,y in roofs} == set(hidden)
    assert (18,15) not in roofs, "the open court must not acquire a roof"
    doors = [(x,y) for x,y,kind,_size in placements if kind == "castle_door"]
    assert len(doors) == 5, "one shared arch per real two-cell doorway"
    walls = {(x,y) for x,y,kind,_size in placements if kind.startswith("castle_wall_")}
    assert walls == {(i%32,i//32) for i,t in enumerate(layout["terrain"]) if t == "mountain_castle_wall"}
    mixed = SCENE.modular_castle_placements(identities,32,24,hidden+[15*32+18],SCENE.content_id)
    assert (18,15,"visual_cover",1) in mixed, "generic covered areas may coexist with castle rooms"
    models = SCENE.load_settlement_models(ROOT.parents[2])
    for kind in {p[2] for p in mixed}:
        vertices,uvs,faces = [],[],[]
        SCENE.append_settlement_model(vertices,uvs,faces,kind,models,{-4:4,-3:3})
        vertices,uvs,faces = SCENE.compact_flat_mesh(vertices,uvs,faces)
        assert faces and len(vertices) <= SCENE.MAX_CACHE_VERTICES
        assert all(0 <= i < len(vertices) for face in faces for i in face[:3])
        if any(part["masonry"] for part in models[kind]):
            assert any(face[3] == 4 for face in faces), "shared castle masonry uses the castle texture"
        else:
            assert all(face[3] == 0 for face in faces), "untextured Godot stone keeps its authored material"


if __name__ == "__main__":
    test_modular_castle_uses_authored_walls_and_independent_roof_cells()
    test_river_crossings_keep_water_and_a_narrow_raised_deck()
    test_camera_pages_keep_separate_fixed_canopy_meshes()
    test_foliage_pages_preserve_every_face_and_leave_trunks_static()
    test_house_only_bank_does_not_require_a_shared_unit_palette()
    test_themed_houses_and_grove_keep_distinct_texture_bindings()
    test_geometry_pool_covers_both_complete_cursor_outlines()
    test_scenery_compaction_preserves_drawn_faces_and_texture_seams()
    test_stone_keep_exports_solid_terrace_with_local_sorting_faces()
    test_masonry_keeps_one_texture_tile_per_board_cell()
    test_console_font_has_complete_pixel_cells()
    test_accented_font_uses_complete_cells_and_dedicated_four_bit_clut()
    test_cutout_keeps_grounding_inside_existing_texture()
    test_stage_palette_keeps_transparency_and_uses_8bpp_tim()
    test_texture_packer_reserves_shared_clut_row()
    test_four_bit_material_packs_texels_and_owns_palette()
    test_hidden_cover_faces_the_same_way_as_the_floor()
    test_interior_terrain_pages_overlap_the_visible_window()
    test_continuous_floor_covers_every_playable_tile_outside_scenery_pages()
    test_large_paved_floor_is_complete_without_an_oversized_mesh()
    test_scenery_cache_counts_only_simultaneously_enabled_instances()
    test_forest_variation_preserves_topology_and_rooted_trunks()
    test_fractured_rock_has_grounded_volume_and_outward_faces()
    print("Mipsync scene generator presentation passed.")
