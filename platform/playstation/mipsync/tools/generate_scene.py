#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Convert a neutral Grandleon scene manifest into Mipsync C data.

This is deliberately a Grandleon-side adapter.  The engine and package
runtime know nothing about Mipsync's structs, texture layout or cache limits.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont, ImageOps


TERRAIN_NAMES = (
    "water", "road", "forest", "mountain", "sand", "snow", "swamp",
    "hills", "ruins", "grass", "farmland", "bamboo", "paved",
)
STYLE_NAMES = (
    "medieval", "scifi", "mythical", "nature", "sengoku", "undead",
    "pirates",
)
ARCHETYPE_NAMES = (
    "knight", "archer", "mage", "stormcaller", "healer", "commander",
    "rogue", "beast",
)
FIGURE_NAMES = ("first", "second")
COLOUR_NAMES = ("blue", "red", "green", "violet", "amber", "bone")
TEAM_COLOURS = (
    (0x23, 0x75, 0xA9), (0xB3, 0x48, 0x3F), (0x2C, 0x8A, 0x48),
    (0x6B, 0x42, 0xA3), (0xBC, 0x83, 0x25), (0x9E, 0x98, 0x86),
)
POLICIES = ("extend_edges", "fixed_terrain", "natural_blend")
CELL_SOURCE_SIZE = 443
TEXTURE_X = 640
TEXTURE_Y = 0
TEXTURE_WIDTH = 384
TEXTURE_HEIGHT = 256
MAX_CACHE_VERTICES = 1024
MAX_CACHE_TRIANGLES = 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--surroundings-policy", choices=POLICIES)
    parser.add_argument("--surroundings-seed", type=int, default=0x47A4D1E)
    parser.add_argument("--apron-depth", type=int)
    return parser.parse_args()


def require_int(value: object, name: str, low: int, high: int) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or not low <= value <= high:
        raise ValueError(f"{name} must be an integer from {low} through {high}")
    return value


def load_manifest(path: Path) -> dict[str, object]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise ValueError("scene manifest must be a JSON object")
    if document.get("schema") != "grandleon.mipsync.scene.v2":
        raise ValueError("unsupported scene manifest schema")
    if document.get("renderer") != "mipsync":
        raise ValueError("scene manifest is not marked for Mipsync")
    width = require_int(document.get("width"), "width", 1, 64)
    height = require_int(document.get("height"), "height", 1, 64)
    terrain = document.get("terrain")
    passability = document.get("passability")
    units = document.get("units")
    surroundings = document.get("surroundings")
    if not isinstance(terrain, list) or len(terrain) != width * height:
        raise ValueError("terrain length does not match width times height")
    if not isinstance(passability, list) or len(passability) != width * height:
        raise ValueError("passability length does not match width times height")
    if not isinstance(units, list) or len(units) > 48:
        raise ValueError("units must be an array with at most 48 entries")
    if not isinstance(surroundings, dict):
        raise ValueError("surroundings must be an object")
    if not isinstance(document.get("simulation"), dict):
        raise ValueError("simulation must be an object")
    for index, kind in enumerate(terrain):
        require_int(kind, f"terrain[{index}]", 0, len(TERRAIN_NAMES) - 1)
    return document


def hash_cell(x: int, y: int, seed: int) -> int:
    value = (seed ^ ((x & 0xFFFF) * 0x45D9F3B) ^ ((y & 0xFFFF) * 0x119DE1F3)) & 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    value = (value * 0x846CA68B) & 0xFFFFFFFF
    return (value ^ (value >> 16)) & 0xFFFFFFFF


def default_policy(mode: int) -> str:
    if mode == 3:
        return "fixed_terrain"
    if mode == 2:
        return "natural_blend"
    return "extend_edges"


def apron_kind(
    terrain: list[int], width: int, height: int, x: int, y: int,
    policy: str, fixed: list[int], seed: int,
) -> int:
    clamped_x = min(max(x, 0), width - 1)
    clamped_y = min(max(y, 0), height - 1)
    edge = terrain[clamped_y * width + clamped_x]
    if policy == "extend_edges":
        return edge
    roll = hash_cell(x, y, seed)
    if policy == "fixed_terrain":
        return fixed[roll % len(fixed)] if fixed else edge

    # Natural blend keeps the authored edge dominant, but occasionally borrows
    # a nearby edge cell. It is deterministic in world coordinates, so panning
    # or rebuilding a visible chunk cannot make the surroundings shimmer.
    candidates = [edge, edge, edge]
    if x < 0 or x >= width:
        candidates.extend(
            terrain[yy * width + clamped_x]
            for yy in range(max(0, clamped_y - 2), min(height, clamped_y + 3))
        )
    if y < 0 or y >= height:
        candidates.extend(
            terrain[clamped_y * width + xx]
            for xx in range(max(0, clamped_x - 2), min(width, clamped_x + 3))
        )
    return candidates[roll % len(candidates)]


class TexturePacker:
    def __init__(self) -> None:
        self.columns = TEXTURE_WIDTH // 64
        self.rows = TEXTURE_HEIGHT // 32
        self.used = [[False] * self.columns for _ in range(self.rows)]

    def place(self, width: int, height: int) -> tuple[int, int]:
        columns = math.ceil(width / 64)
        rows = math.ceil(height / 32)
        for y in range(self.rows - rows + 1):
            for x in range(self.columns - columns + 1):
                if any(self.used[yy][xx] for yy in range(y, y + rows) for xx in range(x, x + columns)):
                    continue
                for yy in range(y, y + rows):
                    for xx in range(x, x + columns):
                        self.used[yy][xx] = True
                return TEXTURE_X + x * 64, TEXTURE_Y + y * 32
        raise ValueError("generated textures exceed the reserved 384x256 VRAM atlas")


def opaque_psx_pixel(red: int, green: int, blue: int) -> int:
    value = (red >> 3) | ((green >> 3) << 5) | ((blue >> 3) << 10)
    return value or 1


def tim_words(image: Image.Image, x: int, y: int, cutout: bool) -> list[int]:
    rgba = image.convert("RGBA")
    pixels: list[int] = []
    flattened = getattr(rgba, "get_flattened_data", None)
    source_pixels = flattened() if flattened is not None else rgba.getdata()
    for red, green, blue, alpha in source_pixels:
        pixels.append(0 if cutout and alpha < 40 else opaque_psx_pixel(red, green, blue))
    data = bytearray(struct.pack("<II", 0x10, 2))
    data.extend(struct.pack("<IHHHH", 12 + len(pixels) * 2, x, y, rgba.width, rgba.height))
    data.extend(struct.pack(f"<{len(pixels)}H", *pixels))
    if len(data) % 4:
        data.extend(b"\0" * (4 - len(data) % 4))
    return list(struct.unpack(f"<{len(data) // 4}I", data))


def c_words(words: list[int]) -> str:
    lines = []
    for offset in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{word:08x}u" for word in words[offset:offset + 8]) + ",")
    return "\n".join(lines)


def fitted_cutout(image: Image.Image, side: int = 64) -> Image.Image:
    source = image.convert("RGBA")
    bounds = source.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError("cutout source has no visible pixels")
    crop = source.crop(bounds)
    scale = min((side - 4) / crop.width, (side - 2) / crop.height)
    crop = crop.resize(
        (max(1, round(crop.width * scale)), max(1, round(crop.height * scale))),
        Image.Resampling.LANCZOS,
    )
    result = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    result.alpha_composite(crop, ((side - crop.width) // 2, side - crop.height))
    result.putalpha(result.getchannel("A").point(lambda value: 255 if value >= 40 else 0))
    return result


def unit_image(repo: Path, unit: dict[str, object], profiles: dict[str, object]) -> Image.Image:
    style_index = require_int(unit.get("style"), "unit style", 0, len(STYLE_NAMES) - 1)
    archetype_index = require_int(unit.get("archetype"), "unit archetype", 0, len(ARCHETYPE_NAMES) - 1)
    figure_index = require_int(unit.get("figure"), "unit figure", 0, len(FIGURE_NAMES) - 1)
    colour_index = require_int(unit.get("colour"), "unit colour", 0, len(COLOUR_NAMES) - 1)
    key = f"{ARCHETYPE_NAMES[archetype_index]}|*|{STYLE_NAMES[style_index]}|{FIGURE_NAMES[figure_index]}"
    binding = profiles.get(key)
    if not isinstance(binding, dict):
        raise ValueError(f"Godot graphics profile has no character binding {key!r}")

    def resource(value: object) -> Path:
        text = str(value)
        if not text.startswith("res://"):
            raise ValueError(f"character resource is not project-relative: {text!r}")
        return repo / "platform/godot/project" / text.removeprefix("res://")

    with Image.open(resource(binding.get("path"))) as sheet:
        source = sheet.convert("RGBA").crop((0, 0, CELL_SOURCE_SIZE, CELL_SOURCE_SIZE))
    with Image.open(resource(binding.get("teamMaskPath"))) as sheet:
        mask = sheet.convert("RGBA").crop((0, 0, CELL_SOURCE_SIZE, CELL_SOURCE_SIZE)).getchannel("A")
    team = TEAM_COLOURS[colour_index]
    gray = ImageOps.grayscale(source)
    tinted = ImageOps.colorize(
        gray, tuple(max(0, channel // 5) for channel in team),
        tuple(min(255, channel + (255 - channel) * 3 // 4) for channel in team),
    ).convert("RGBA")
    tinted.putalpha(source.getchannel("A"))
    return fitted_cutout(Image.composite(tinted, source, mask))


def tree_image(repo: Path) -> Image.Image:
    path = repo / "platform/godot/project/art/map_features/ancient_tree_cluster_2x2.png"
    with Image.open(path) as image:
        return fitted_cutout(image)


def write_textures(
    output: Path, repo: Path, terrain_kinds: list[int], units: list[dict[str, object]],
) -> tuple[dict[int, int], dict[tuple[int, int, int, int], int], int, int, dict[str, int]]:
    profiles_path = repo / "platform/godot/project/content/graphics_profiles.json"
    profiles = json.loads(profiles_path.read_text(encoding="utf-8"))["profiles"]["voxel-sprites"]["assets"]["characterSprites"]
    packer = TexturePacker()
    textures: list[tuple[str, Image.Image, bool, int, int]] = []
    terrain_indices: dict[int, int] = {}
    unit_indices: dict[tuple[int, int, int, int], int] = {}

    for kind in sorted(set(terrain_kinds)):
        path = repo / "tools/placeholder_art/assets/modern/terrain" / f"{TERRAIN_NAMES[kind]}_base.png"
        with Image.open(path) as image:
            texture = image.convert("RGBA")
        x, y = packer.place(texture.width, texture.height)
        textures.append((f"terrain_{TERRAIN_NAMES[kind]}", texture, False, x, y))
        terrain_indices[kind] = len(textures)

    for unit in units:
        key = (
            int(unit["style"]), int(unit["archetype"]), int(unit["figure"]),
            int(unit["colour"]),
        )
        if key in unit_indices:
            continue
        texture = unit_image(repo, unit, profiles)
        x, y = packer.place(texture.width, texture.height)
        textures.append((f"unit_{len(unit_indices)}", texture, True, x, y))
        unit_indices[key] = len(textures)

    canopy = tree_image(repo)
    x, y = packer.place(canopy.width, canopy.height)
    textures.append(("tree_canopy", canopy, True, x, y))
    tree_index = len(textures)

    ui = Image.new("RGBA", (128, 32), (12, 19, 30, 255))
    draw = ImageDraw.Draw(ui)
    font = ImageFont.load_default()
    draw.rounded_rectangle(
        (0, 0, 127, 31), radius=4, outline=(94, 206, 227, 255), width=1,
    )
    draw.text((5, 3), "MIPSYNC", font=font, fill=(94, 206, 227, 255))
    draw.text((5, 17), "X MOVE   [] WAIT", font=font, fill=(247, 239, 209, 255))
    x, y = packer.place(ui.width, ui.height)
    textures.append(("compact_ui", ui, False, x, y))
    ui_index = len(textures)

    lines = ["/* Generated from a real Grandleon scene manifest. Do not edit. */", '#include "../runtime/textures.h"', ""]
    occupied_pixels = 0
    for name, image, cutout, x, y in textures:
        words = tim_words(image, x, y, cutout)
        lines.extend((f"static const uint32_t k_{name}_tim[] = {{", c_words(words), "};", ""))
        occupied_pixels += image.width * image.height
    lines.append("const ps1_texture_desc g_ps1_textures[] = {")
    for name, _image, cutout, _x, _y in textures:
        lines.append(
            f"    {{ k_{name}_tim, sizeof(k_{name}_tim) / sizeof(k_{name}_tim[0]), 0, {1 if cutout else 0} }},"
        )
    lines.extend(("};", "const unsigned int g_ps1_texture_count =", "    sizeof(g_ps1_textures) / sizeof(g_ps1_textures[0]);", ""))
    (output / "textures_data.c").write_text("\n".join(lines), encoding="utf-8")
    metrics = {
        "texture_count": len(textures),
        "texture_bytes": occupied_pixels * 2,
        "vram_reserved_bytes": TEXTURE_WIDTH * TEXTURE_HEIGHT * 2 - occupied_pixels * 2,
    }
    return terrain_indices, unit_indices, tree_index, ui_index, metrics


def emit_vectors(name: str, values: list[tuple[int, int, int, int]]) -> list[str]:
    result = [f"static const SVECTOR {name}[] = {{"]
    result.extend(f"    {{ {x}, {y}, {z}, {w} }}," for x, y, z, w in values)
    result.extend(("};", ""))
    return result


def page_starts(extent: int, visible: int) -> list[int]:
    if extent <= visible:
        return [0]
    last = extent - visible
    step = max(1, visible - 2)
    starts = list(range(0, last + 1, step))
    if starts[-1] != last:
        starts.append(last)
    return starts


def page_cells(
    cells: list[tuple[int, int, int, bool, int, int]],
    width: int, height: int, apron_depth: int,
) -> list[tuple[tuple[int, int], list[tuple[int, int, int, bool, int, int]]]]:
    pages = []
    for start_y in page_starts(height, CAMERA_VIEW_TILES_Y):
        end_y = min(height, start_y + CAMERA_VIEW_TILES_Y)
        page_min_y = -apron_depth if start_y == 0 else start_y
        page_max_y = height + apron_depth if end_y == height else end_y
        for start_x in page_starts(width, CAMERA_VIEW_TILES_X):
            end_x = min(width, start_x + CAMERA_VIEW_TILES_X)
            page_min_x = -apron_depth if start_x == 0 else start_x
            page_max_x = width + apron_depth if end_x == width else end_x
            visible = []
            for x, y, kind, authored, span_x, span_y in cells:
                clipped_x0 = max(x, page_min_x)
                clipped_y0 = max(y, page_min_y)
                clipped_x1 = min(x + span_x, page_max_x)
                clipped_y1 = min(y + span_y, page_max_y)
                if clipped_x0 >= clipped_x1 or clipped_y0 >= clipped_y1:
                    continue
                visible.append((
                    clipped_x0, clipped_y0, kind, authored,
                    clipped_x1 - clipped_x0, clipped_y1 - clipped_y0,
                ))
            pages.append(((start_x, start_y), visible))
    return pages


def write_meshes(
    output: Path,
    cell_groups: list[tuple[tuple[int, int], list[tuple[int, int, int, bool, int, int]]]],
    width: int, height: int,
    terrain_indices: dict[int, int], tree_texture_index: int, seed: int,
) -> tuple[int, int, list[tuple[int, int, int, int, int, int, int]]]:
    board_groups: list[tuple[
        list[tuple[int, int, int, int]], list[tuple[int, int]],
        list[tuple[int, int, int, int, int]], tuple[int, int, int, int],
    ]] = []
    page_origins: list[tuple[int, int]] = []
    for key, group_cells in cell_groups:
        page_origins.append(key)
        board_vertices: list[tuple[int, int, int, int]] = []
        board_uvs: list[tuple[int, int]] = []
        board_triangles: list[tuple[int, int, int, int, int]] = []
        min_x = min(cell[0] for cell in group_cells)
        min_y = min(cell[1] for cell in group_cells)
        max_x = max(cell[0] + cell[4] for cell in group_cells)
        max_y = max(cell[1] + cell[5] for cell in group_cells)
        for x, y, kind, authored, span_x, span_y in group_cells:
            left = (x * 2 - width) * 64
            right = left + span_x * 128
            top = (height - y * 2) * 64
            bottom = top - span_y * 128
            first = len(board_vertices)
            board_vertices.extend((
                (left, 0, top, 0), (right, 0, top, 0),
                (left, 0, bottom, 0), (right, 0, bottom, 0),
            ))
            if authored:
                variant = hash_cell(x, y, seed) % (5 - span_x)
                u0 = variant * 32
                u1 = u0 + span_x * 32 - 1
            else:
                u0 = 0
                u1 = min(127, span_x * 32 - 1)
            board_uvs.extend(((u0, 31), (u1, 31), (u0, 0), (u1, 0)))
            shade = 255 if authored else 210
            texture = terrain_indices[kind]
            board_triangles.extend((
                (first, first + 1, first + 2, texture, shade),
                (first + 2, first + 1, first + 3, texture, shade),
            ))
        board_groups.append((
            board_vertices, board_uvs, board_triangles,
            (min_x, min_y, max_x, max_y),
        ))

    billboard_vertices = [(-512, 0, 0, 0), (512, 0, 0, 0), (512, 1024, 0, 0), (-512, 1024, 0, 0)]
    billboard_uvs = [(0, 63), (63, 63), (63, 0), (0, 0)]

    # Crossed trunk plus two crossed/layered canopy cards. The cards are
    # double-sided: tactical cameras may orbit later, while the far-LOD path
    # can still collapse these instances to one card without changing art.
    tree_vertices = [
        (-72, 0, 0, 0), (72, 0, 0, 0), (72, 560, 0, 0), (-72, 560, 0, 0),
        (0, 0, -72, 0), (0, 0, 72, 0), (0, 560, 72, 0), (0, 560, -72, 0),
        (-420, 360, 0, 0), (420, 360, 0, 0), (420, 1320, 0, 0), (-420, 1320, 0, 0),
        (0, 360, -420, 0), (0, 360, 420, 0), (0, 1320, 420, 0), (0, 1320, -420, 0),
    ]
    tree_uvs = [(0, 0)] * 8 + [(0, 63), (63, 63), (63, 0), (0, 0)] * 2
    # Two crossed trunk planes are enough at board scale and preserve volume
    # without spending twelve triangles on a cube that occupies four pixels.
    trunk_faces = [(0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7)]
    tree_faces: list[tuple[int, int, int, int, tuple[int, int, int]]] = [
        (a, b, c, 0, (112, 74, 45)) for a, b, c in trunk_faces
    ]
    for first in (8, 12):
        tree_faces.extend((
            (first, first + 1, first + 2, tree_texture_index, (255, 255, 255)),
            (first, first + 2, first + 3, tree_texture_index, (255, 255, 255)),
            (first + 2, first + 1, first, tree_texture_index, (255, 255, 255)),
            (first + 3, first + 2, first, tree_texture_index, (255, 255, 255)),
        ))

    cursor_vertices = [
        (-450, 24, -450, 0), (450, 24, -450, 0),
        (450, 24, 450, 0), (-450, 24, 450, 0),
    ]

    vertex_count = sum(len(group[0]) for group in board_groups)
    triangle_count = sum(len(group[2]) for group in board_groups)
    lines = ["/* Generated from a real Grandleon scene manifest. Do not edit. */", '#include "../runtime/scene.h"', ""]
    lines += emit_vectors("k_billboard_vertices", billboard_vertices)
    lines += emit_vectors("k_billboard_normals", [(0, 0, 4096, 0)] * 4)
    lines.extend(("static const ps1_uv8 k_billboard_uvs[] = {", "    { 0, 63 }, { 63, 63 }, { 63, 0 }, { 0, 0 },", "};", "", "static const ps1_mesh_tri k_billboard_triangles[] = {", "    { .i0=0, .i1=1, .i2=2, .nz=4096, .shade=255, .color_r=255, .color_g=255, .color_b=255 },", "    { .i0=0, .i1=2, .i2=3, .nz=4096, .shade=255, .color_r=255, .color_g=255, .color_b=255 },", "};", ""))
    lines += emit_vectors("k_tree_vertices", tree_vertices)
    lines += emit_vectors("k_tree_normals", [(0, 0, 4096, 0)] * len(tree_vertices))
    lines.append("static const ps1_uv8 k_tree_uvs[] = {")
    lines.extend(f"    {{ {u}, {v} }}," for u, v in tree_uvs)
    lines.extend(("};", "", "static const ps1_mesh_tri k_tree_triangles[] = {"))
    for i0, i1, i2, texture, colour in tree_faces:
        lines.append(f"    {{ .i0={i0}, .i1={i1}, .i2={i2}, .nz=4096, .shade=255, .color_r={colour[0]}, .color_g={colour[1]}, .color_b={colour[2]}, .texture_index={texture} }},")
    lines.extend(("};", ""))
    chunk_descriptors: list[tuple[int, int, int, int, int, int, int]] = []
    chunk_mesh_lines: list[str] = []
    for chunk_index, (vertices, uvs, triangles, bounds) in enumerate(board_groups):
        name = f"k_board_{chunk_index}"
        lines += emit_vectors(f"{name}_vertices", vertices)
        lines += emit_vectors(f"{name}_normals", [(0, 4096, 0, 0)] * len(vertices))
        lines.append(f"static const ps1_uv8 {name}_uvs[] = {{")
        lines.extend(f"    {{ {u}, {v} }}," for u, v in uvs)
        lines.extend(("};", "", f"static const ps1_mesh_tri {name}_triangles[] = {{"))
        for i0, i1, i2, texture, shade in triangles:
            lines.append(f"    {{ .i0={i0}, .i1={i1}, .i2={i2}, .ny=4096, .shade={shade}, .color_r=255, .color_g=255, .color_b=255, .texture_index={texture} }},")
        lines.extend(("};", ""))
        centre_x = (min(vertex[0] for vertex in vertices) + max(vertex[0] for vertex in vertices)) // 2
        centre_z = (min(vertex[2] for vertex in vertices) + max(vertex[2] for vertex in vertices)) // 2
        radius = math.ceil(max(math.hypot(vertex[0] - centre_x, vertex[2] - centre_z) for vertex in vertices))
        chunk_mesh_lines.append(
            f"    {{ .verts={name}_vertices, .norms={name}_normals, .vert_count=sizeof({name}_vertices)/sizeof({name}_vertices[0]), .uvs={name}_uvs, .tris={name}_triangles, .tri_count=sizeof({name}_triangles)/sizeof({name}_triangles[0]), .scale_q12=4096, .bounds_center_x={centre_x}, .bounds_center_z={centre_z}, .bounds_radius={radius}, .flags=PS1_MESH_FLAG_TERRAIN }},"
        )
        start_x, start_y = page_origins[chunk_index]
        end_x = min(width, start_x + CAMERA_VIEW_TILES_X)
        end_y = min(height, start_y + CAMERA_VIEW_TILES_Y)
        focus_x = start_x + end_x - width
        focus_z = height - start_y - end_y
        chunk_descriptors.append((chunk_index + 3, *bounds, focus_x, focus_z))
    lines += emit_vectors("k_cursor_vertices", cursor_vertices)
    lines += emit_vectors("k_cursor_normals", [(0, 4096, 0, 0)] * 4)
    lines.extend((
        "static const ps1_uv8 k_cursor_uvs[] = { {0,0}, {0,0}, {0,0}, {0,0} };",
        "static const ps1_mesh_tri k_cursor_triangles[] = {",
        "    { .i0=0, .i1=2, .i2=1, .ny=4096, .shade=255, .color_r=255, .color_g=225, .color_b=48 },",
        "    { .i0=0, .i1=3, .i2=2, .ny=4096, .shade=255, .color_r=255, .color_g=225, .color_b=48 },",
        "};", "", "const ps1_mesh g_ps1_meshes[] = {",
        "    { .verts=k_billboard_vertices, .norms=k_billboard_normals, .vert_count=4, .uvs=k_billboard_uvs, .tris=k_billboard_triangles, .tri_count=2, .scale_q12=256, .bounds_center_y=512, .bounds_radius=724 },",
        "    { .verts=k_tree_vertices, .norms=k_tree_normals, .vert_count=sizeof(k_tree_vertices)/sizeof(k_tree_vertices[0]), .uvs=k_tree_uvs, .tris=k_tree_triangles, .tri_count=sizeof(k_tree_triangles)/sizeof(k_tree_triangles[0]), .scale_q12=256, .bounds_center_y=660, .bounds_radius=1000 },",
        *chunk_mesh_lines,
        "    { .verts=k_cursor_vertices, .norms=k_cursor_normals, .vert_count=4, .uvs=k_cursor_uvs, .tris=k_cursor_triangles, .tri_count=2, .scale_q12=256, .bounds_radius=640, .flags=PS1_MESH_FLAG_TERRAIN },",
        "};", "const unsigned int g_ps1_mesh_count =", "    sizeof(g_ps1_meshes) / sizeof(g_ps1_meshes[0]);", ""))
    (output / "mesh_data.c").write_text("\n".join(lines), encoding="utf-8")
    return vertex_count + 4, triangle_count + 2, chunk_descriptors


def fixed(value: float) -> int:
    return round(value * 65536)


CAMERA_VIEW_TILES_X = 8
CAMERA_VIEW_TILES_Y = 6
CAMERA_HEIGHT = 11.5
CAMERA_TRAILING_DISTANCE = 14.0


def camera_focus(coordinate: int, extent: int, visible: int) -> int:
    """Return a clamped world-space camera focus for one tactical axis."""
    if extent <= visible:
        return 0
    world = coordinate * 2 + 1 - extent
    world_min = 1 - extent
    world_max = extent - 1
    margin = visible - 1
    return max(world_min + margin, min(world_max - margin, world))


def write_scene(
    output: Path, document: dict[str, object], unit_indices: dict[tuple[int, int, int, int], int],
    tree_cells: list[tuple[int, int]],
    terrain_chunks: list[tuple[int, int, int, int, int, int, int]],
    seed: int, ui_texture_index: int,
) -> int:
    width = int(document["width"])
    height = int(document["height"])
    units = document["units"]
    assert isinstance(units, list)
    if len(units) > 64:
        raise ValueError("Mipsync playable slice supports at most 64 units")
    lines = ["/* Generated from a real Grandleon scene manifest. Do not edit. */", '#include "grandleon_scene.h"', "", "static ps1_entity k_ps1_entities[] = {"]
    next_id = 1
    entity_index = 0
    terrain_entity_indices: list[int] = []
    tree_entity_indices: list[int] = []
    for mesh_index, _min_x, _min_y, _max_x, _max_y, _focus_x, _focus_z in terrain_chunks:
        terrain_entity_indices.append(entity_index)
        lines.append(
            f"    {{ .id={next_id}u, .name=\"terrain_apron_chunk\", .parent_entity_index=-1, .scale={{65536,65536,65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index={mesh_index}, .mesh_size=65536, .mesh_enabled=1, .color_r=255, .color_g=255, .color_b=255, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_FLOOR, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
        )
        next_id += 1
        entity_index += 1
    for x, y in tree_cells:
        tree_entity_indices.append(entity_index)
        roll = hash_cell(x, y, seed)
        scale = 1.05 + (roll & 7) * 0.045
        world_x = (x + 0.5 - width / 2) * 2
        world_z = (height / 2 - y - 0.5) * 2
        lines.append(
            f"    {{ .id={next_id}u, .name=\"forest_tree\", .parent_entity_index=-1, .position={{{fixed(world_x)},1311,{fixed(world_z)}}}, .rotation={{0,{fixed((roll >> 8) % 180)},0}}, .scale={{{fixed(scale)},{fixed(scale)},{fixed(scale)}}}, .mesh=PS1_MESH_CUSTOM, .mesh_index=2, .mesh_size=65536, .mesh_enabled=1, .color_r=255, .color_g=255, .color_b=255, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_PROP, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
        )
        next_id += 1
        entity_index += 1
    unit_entity_indices: list[int] = []
    for unit in units:
        if not isinstance(unit, dict):
            raise ValueError("every unit must be an object")
        x = require_int(unit.get("x"), "unit x", 0, width - 1)
        y = require_int(unit.get("y"), "unit y", 0, height - 1)
        key = (int(unit["style"]), int(unit["archetype"]), int(unit["figure"]), int(unit["colour"]))
        texture = unit_indices[key]
        unit_entity_indices.append(entity_index)
        world_x = (x + 0.5 - width / 2) * 2
        world_z = (height / 2 - y - 0.5) * 2
        lines.append(
            f"    {{ .id={next_id}u, .name=\"unit\", .parent_entity_index=-1, .position={{{fixed(world_x)},2621,{fixed(world_z)}}}, .scale={{{fixed(1.35)},{fixed(1.9)},65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index=1, .mesh_size=65536, .mesh_enabled=1, .color_r=255, .color_g=255, .color_b=255, .texture_index={texture}, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_CHARACTER, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
        )
        next_id += 1
        entity_index += 1
    first_unit = next(
        (unit for unit in units if isinstance(unit, dict) and int(unit["side"]) == 0),
        units[0],
    )
    assert isinstance(first_unit, dict)
    cursor_x = int(first_unit["x"])
    cursor_y = int(first_unit["y"])
    cursor_world_x = (cursor_x + 0.5 - width / 2) * 2
    cursor_world_z = (height / 2 - cursor_y - 0.5) * 2
    camera_focus_x = camera_focus(cursor_x, width, CAMERA_VIEW_TILES_X)
    camera_focus_z = -camera_focus(cursor_y, height, CAMERA_VIEW_TILES_Y)
    cursor_index = entity_index
    cursor_mesh_index = 3 + len(terrain_chunks)
    lines.append(
        f"    {{ .id={next_id}u, .name=\"tactical_cursor\", .parent_entity_index=-1, .position={{{fixed(cursor_world_x)},0,{fixed(cursor_world_z)}}}, .scale={{65536,65536,65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index={cursor_mesh_index}, .mesh_size=65536, .mesh_enabled=1, .color_r=255, .color_g=225, .color_b=48, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_FLOOR, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
    )
    next_id += 1
    entity_index += 1
    camera_index = cursor_index + 1
    lines.extend((
        f"    {{ .id={next_id}u, .name=\"clamped_board_camera\", .parent_entity_index=-1, .position={{{fixed(camera_focus_x)},{fixed(CAMERA_HEIGHT)},{fixed(camera_focus_z + CAMERA_TRAILING_DISTANCE)}}}, .rotation={{{fixed(-40)},0,0}}, .scale={{65536,65536,65536}}, .rigid_root_entity_index=-1, .collider_shape=-1, .has_camera=1, .camera_primary=1, .camera_fov={fixed(45)}, .camera_near={fixed(0.2)}, .camera_far={fixed(80)}, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},",
        "};", "", "const ps1_scene g_ps1_scene = {", "    .entities=k_ps1_entities,", "    .entity_count=sizeof(k_ps1_entities)/sizeof(k_ps1_entities[0]),", f"    .camera_entity_index={camera_index},", "    .light={ .enabled=1, .dir_x=-1024, .dir_y=3072, .dir_z=2048, .color_r=255, .color_g=244, .color_b=220, .ambient_r=96, .ambient_g=96, .ambient_b=112 },", "    .fog_enabled=1, .fog_r=88, .fog_g=132, .fog_b=170,", f"    .fog_start={fixed(18)}, .fog_end={fixed(36)},", f"    .meshes=g_ps1_meshes, .mesh_count={cursor_mesh_index},", "};", "",
        "const uint64_t g_grandleon_unit_ids[] = {",
        *(f"    {int(unit['id'])}ULL," for unit in units if isinstance(unit, dict)),
        "};",
        "const unsigned int g_grandleon_unit_entity_indices[] = {",
        *(f"    {index}u," for index in unit_entity_indices),
        "};",
        "const unsigned int g_grandleon_unit_count = sizeof(g_grandleon_unit_ids) / sizeof(g_grandleon_unit_ids[0]);",
        "const unsigned int g_grandleon_terrain_entity_indices[] = {",
        *(f"    {index}u," for index in terrain_entity_indices),
        "};",
        "const int16_t g_grandleon_terrain_chunk_bounds[][4] = {",
        *(f"    {{{min_x},{min_y},{max_x},{max_y}}}," for _mesh, min_x, min_y, max_x, max_y, _focus_x, _focus_z in terrain_chunks),
        "};",
        "const int16_t g_grandleon_terrain_page_focus[][2] = {",
        *(f"    {{{focus_x},{focus_z}}}," for _mesh, _min_x, _min_y, _max_x, _max_y, focus_x, focus_z in terrain_chunks),
        "};",
        "const unsigned int g_grandleon_terrain_chunk_count = sizeof(g_grandleon_terrain_entity_indices) / sizeof(g_grandleon_terrain_entity_indices[0]);",
        "const unsigned int g_grandleon_tree_entity_indices[] = {",
        *(f"    {index}u," for index in tree_entity_indices),
        "};",
        "const unsigned int g_grandleon_tree_count = sizeof(g_grandleon_tree_entity_indices) / sizeof(g_grandleon_tree_entity_indices[0]);",
        f"const unsigned int g_grandleon_cursor_entity_index = {cursor_index}u;",
        f"const unsigned int g_grandleon_camera_entity_index = {camera_index}u;",
        f"const int32_t g_grandleon_camera_height = {fixed(CAMERA_HEIGHT)};",
        f"const int32_t g_grandleon_camera_trailing_distance = {fixed(CAMERA_TRAILING_DISTANCE)};",
        f"const unsigned int g_grandleon_camera_view_tiles_x = {CAMERA_VIEW_TILES_X}u;",
        f"const unsigned int g_grandleon_camera_view_tiles_y = {CAMERA_VIEW_TILES_Y}u;",
        f"const unsigned int g_grandleon_ui_texture_index = {ui_texture_index}u;",
        "ps1_entity* grandleon_scene_mutable_entity(unsigned int index) {",
        "    return index < g_ps1_scene.entity_count ? &k_ps1_entities[index] : 0;",
        "}", ""))
    (output / "scene_data.c").write_text("\n".join(lines), encoding="utf-8")
    header = """/* Generated from a real Grandleon scene manifest. Do not edit. */
#ifndef GRANDLEON_GENERATED_SCENE_H
#define GRANDLEON_GENERATED_SCENE_H
#include <stdint.h>
#include "../runtime/scene.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const uint64_t g_grandleon_unit_ids[];
extern const unsigned int g_grandleon_unit_entity_indices[];
extern const unsigned int g_grandleon_unit_count;
extern const unsigned int g_grandleon_terrain_entity_indices[];
extern const int16_t g_grandleon_terrain_chunk_bounds[][4];
extern const int16_t g_grandleon_terrain_page_focus[][2];
extern const unsigned int g_grandleon_terrain_chunk_count;
extern const unsigned int g_grandleon_tree_entity_indices[];
extern const unsigned int g_grandleon_tree_count;
extern const unsigned int g_grandleon_cursor_entity_index;
extern const unsigned int g_grandleon_camera_entity_index;
extern const int32_t g_grandleon_camera_height;
extern const int32_t g_grandleon_camera_trailing_distance;
extern const unsigned int g_grandleon_camera_view_tiles_x;
extern const unsigned int g_grandleon_camera_view_tiles_y;
extern const unsigned int g_grandleon_ui_texture_index;
ps1_entity* grandleon_scene_mutable_entity(unsigned int index);
#ifdef __cplusplus
}
#endif
#endif
"""
    (output / "grandleon_scene.h").write_text(header, encoding="utf-8")
    return len(terrain_chunks) + len(tree_cells) + len(units) + 2


def main() -> int:
    args = parse_args()
    document = load_manifest(args.manifest)
    repo = args.repo_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    width = int(document["width"])
    height = int(document["height"])
    terrain = [int(value) for value in document["terrain"]]
    surroundings = document["surroundings"]
    assert isinstance(surroundings, dict)
    mode = require_int(surroundings.get("mode"), "surroundings mode", 0, 3)
    requested_depth = require_int(surroundings.get("depth"), "surroundings depth", 0, 32)
    depth = min(requested_depth, 2) if args.apron_depth is None else require_int(args.apron_depth, "apron depth", 0, 2)
    if mode == 0 and args.apron_depth is None:
        depth = 0
    policy = args.surroundings_policy or default_policy(mode)
    fixed_terrain = [int(value) for value in surroundings.get("terrain", [])]
    for index, kind in enumerate(fixed_terrain):
        require_int(kind, f"surroundings terrain[{index}]", 0, len(TERRAIN_NAMES) - 1)

    cells: list[tuple[int, int, int, bool, int, int]] = []
    # Coalesce horizontal runs of authored terrain. The PS1 can draw a strip
    # with the same texture in two triangles, so retaining four independent
    # vertices and two triangles per cell only burns CPU/cache without adding
    # detail. Runs stop at four cells because each terrain strip contains four
    # authored 32-pixel variants.
    for y in range(height):
        x = 0
        while x < width:
            kind = terrain[y * width + x]
            span = 1
            while (
                span < 4 and x + span < width
                and terrain[y * width + x + span] == kind
            ):
                span += 1
            cells.append((x, y, kind, True, span, 1))
            x += span
    if depth:
        horizontal_chunk = math.ceil((width + depth * 2) / 2)
        for y in (-depth, height):
            for x in range(-depth, width + depth, horizontal_chunk):
                span_x = min(horizontal_chunk, width + depth - x)
                kind = apron_kind(terrain, width, height, x, y, policy, fixed_terrain, args.surroundings_seed)
                cells.append((x, y, kind, False, span_x, depth))
        vertical_chunk = math.ceil(height / 2)
        for y in range(0, height, vertical_chunk):
            span_y = min(vertical_chunk, height - y)
            for x in (-depth, width):
                kind = apron_kind(terrain, width, height, x, y, policy, fixed_terrain, args.surroundings_seed)
                cells.append((x, y, kind, False, depth, span_y))

    units = document["units"]
    assert isinstance(units, list)
    typed_units = [unit for unit in units if isinstance(unit, dict)]
    all_kinds = [kind for _x, _y, kind, _authored, _span_x, _span_y in cells]
    terrain_indices, unit_indices, tree_texture, ui_texture, texture_metrics = write_textures(
        output, repo, all_kinds, typed_units,
    )
    terrain_groups = page_cells(cells, width, height, depth)
    board_vertices, board_triangles, terrain_chunks = write_meshes(
        output, terrain_groups, width, height, terrain_indices, tree_texture,
        args.surroundings_seed,
    )
    tree_cells = []
    for y in range(height):
        for x in range(width):
            if terrain[y * width + x] != 2:
                continue
            neighbours = sum(
                0 <= xx < width and 0 <= yy < height and terrain[yy * width + xx] == 2
                for xx, yy in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
            )
            nearby_hashes = [
                hash_cell(xx, yy, args.surroundings_seed)
                for xx, yy in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
                if 0 <= xx < width and 0 <= yy < height and terrain[yy * width + xx] == 2
            ]
            if neighbours > 0 and hash_cell(x, y, args.surroundings_seed) <= min(nearby_hashes):
                tree_cells.append((x, y))
    # Keep board-scale landmarks sparse enough for 60 Hz while retaining one
    # deterministic volume cluster per roughly sixteen tactical cells.
    tree_budget = max(1, (width * height + 15) // 16)
    tree_cells.sort(key=lambda cell: hash_cell(cell[0], cell[1], args.surroundings_seed))
    tree_cells = tree_cells[:tree_budget]
    entities = write_scene(
        output, document, unit_indices, tree_cells, terrain_chunks,
        args.surroundings_seed, ui_texture,
    )
    cache_vertices = board_vertices + len(tree_cells) * 16 + len(units) * 4
    cache_triangles = board_triangles + len(tree_cells) * 12 + len(units) * 2
    if cache_vertices > MAX_CACHE_VERTICES or cache_triangles > MAX_CACHE_TRIANGLES:
        raise ValueError(
            f"visible scene exceeds trimmed Mipsync caches: {cache_vertices}/{MAX_CACHE_VERTICES} vertices, "
            f"{cache_triangles}/{MAX_CACHE_TRIANGLES} triangles; production paging must split this view"
        )
    metadata = {
        "schema": "grandleon.mipsync.generated.v2", "renderer": "mipsync",
        "map": {"width": width, "height": height},
        "surroundings": {"policy": policy, "seed": args.surroundings_seed, "visible_depth": depth, "authored_depth": requested_depth},
        "entities": entities,
        "terrain_chunks": len(terrain_chunks),
        "cache": {"vertices": cache_vertices, "vertex_limit": MAX_CACHE_VERTICES, "triangles": cache_triangles, "triangle_limit": MAX_CACHE_TRIANGLES},
        "vram": texture_metrics,
        "camera": {
            "clamp": "viewport_aware_playable_bounds", "static_probe": False,
            "visible_tiles": [CAMERA_VIEW_TILES_X, CAMERA_VIEW_TILES_Y],
        },
    }
    (output / "scene-metrics.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(
        "GRANDLEON_PS1_RENDERER=MIPSYNC "
        f"generated={output} policy={policy} cells={len(cells)} entities={entities} "
        f"cache={cache_vertices}v/{cache_triangles}t textures={texture_metrics['texture_count']}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"generate_scene.py: {error}") from error
