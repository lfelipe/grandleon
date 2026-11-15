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
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "platform/console_art"))
from scenery_style import HOUSE_FAMILIES, document as scenery_style, house_at, house_image, grove_image, ground_image, materials as ground_materials
from settlement_scenery import load_models as load_settlement_models, placements as settlement_placements
from stone_keep import load_keep, textured_part_mesh, face_normal, lit_colour

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageOps


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
RELIEF_MODES = ("flat", "smooth")
CELL_SOURCE_SIZE = 443
# The two 320x240 framebuffers occupy x=0..319.  The original adapter used
# only the upper-right 384x256 corner, leaving most of the PS1's actual texture
# space unavailable even though Mipsync already supports texture pages in both
# 256-line bands.  Keep the whole framebuffer-free strip available to the
# stage packer; individual textures still remain inside one 256-line page.
TEXTURE_X = 320
TEXTURE_Y = 0
TEXTURE_WIDTH = 704
TEXTURE_HEIGHT = 512
TEXTURE_PAGE_HEIGHT = 256
TEXTURE_DEPTH_BITS = 8
# One shared 256-colour stage palette keeps every texture page compatible
# while avoiding a separate 512-byte CLUT for every pose. The final VRAM row
# is outside all framebuffer rectangles and is reserved in TexturePacker.
CLUT_X = 768
CLUT_Y = 511
CLUT_COLOURS = 256
UNIT_POSES = (
    ("idle", 0, False, 64),
    ("move_contact", 2, False, 64),
    ("move_pass", 3, False, 64),
    # Indexed textures recover enough resident memory for full-size compass
    # poses. Board turns no longer drop to a visibly softer 32px cutout.
    ("move_north_contact", 2, True, 64),
    ("move_north_pass", 3, True, 64),
    ("move_south_contact", 6, True, 64),
    ("move_south_pass", 7, True, 64),
    ("attack", 5, False, 64),
    ("hurt", 7, False, 64),
)
UnitTextureIndices = tuple[int, int, int, int, int, int, int, int, int]
MAX_CACHE_VERTICES = 1024
MAX_CACHE_TOTAL = 1280  # Minimum pools; stage audits size them within hard limits.

# Colour and proportions for the tiny low-poly forms embedded into each
# terrain page. The same bounded topology reads as reeds, rubble, crops,
# bamboo or rock. Forest crowns use a separate moving page batch.
TERRAIN_FORM_STYLES = (
    ((73, 130, 92), (0.28, 1.15, 0.28)),   # water reeds
    ((143, 127, 102), (0.62, 0.42, 0.54)), # road stone
    ((62, 121, 58), (0.72, 0.72, 0.72)),   # forest undergrowth
    ((154, 159, 155), (0.92, 1.42, 0.82)), # mountain outcrop
    ((213, 178, 105), (0.92, 0.48, 0.66)), # sand ridge
    ((199, 226, 231), (0.72, 1.05, 0.64)), # snow/ice
    ((91, 117, 62), (0.32, 1.28, 0.32)),   # swamp rushes
    ((126, 139, 92), (0.82, 1.02, 0.74)),  # hill rock
    ((135, 142, 139), (0.68, 0.62, 0.58)), # ruin rubble
    ((75, 143, 63), (0.64, 0.70, 0.64)),   # grass shrub
    ((202, 167, 69), (0.30, 1.18, 0.30)),  # crops
    ((83, 151, 68), (0.26, 2.05, 0.26)),   # bamboo
    ((155, 158, 153), (0.58, 0.34, 0.48)), # paved kerb
)

# Board-space height, where 64 mesh units equal one scene unit.  The values are
# deliberately shallow: they must read as a landscape without hiding units or
# turning an authored tactical tile into an apparent cliff.
TERRAIN_RELIEF_HEIGHTS = (
    -5, 0, 7, 26, 6, 10, -3, 17, 10, 3, 6, 9, 3,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--surroundings-policy", choices=POLICIES)
    parser.add_argument("--surroundings-seed", type=int, default=0x47A4D1E)
    parser.add_argument("--apron-depth", type=int)
    parser.add_argument("--terrain-relief", choices=RELIEF_MODES, default="smooth")
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
    visibility = document.get("visual_visibility")
    if not isinstance(terrain, list) or len(terrain) != width * height:
        raise ValueError("terrain length does not match width times height")
    terrain_ids = document.get("terrain_ids")
    if terrain_ids is not None:
        if not isinstance(terrain_ids, list) or len(terrain_ids) != width * height:
            raise ValueError("terrain_ids length does not match the map")
        if any(not isinstance(value, str) or not value.isdecimal() or
               not 0 <= int(value) <= 0xFFFFFFFFFFFFFFFF for value in terrain_ids):
            raise ValueError("terrain_ids must contain unsigned 64-bit decimal strings")
    if not isinstance(passability, list) or len(passability) != width * height:
        raise ValueError("passability length does not match width times height")
    if not isinstance(units, list) or len(units) > 48:
        raise ValueError("units must be an array with at most 48 entries")
    if not isinstance(surroundings, dict):
        raise ValueError("surroundings must be an object")
    if not isinstance(visibility, dict):
        raise ValueError("visual_visibility must be an object")
    require_int(visibility.get("reveal_radius"), "reveal radius", 0, 8)
    hidden_cells = visibility.get("hidden_cells")
    if not isinstance(hidden_cells, list):
        raise ValueError("hidden_cells must be an array")
    if len(hidden_cells) > width * height:
        raise ValueError("hidden_cells exceeds the map")
    if len(set(hidden_cells)) != len(hidden_cells):
        raise ValueError("hidden_cells contains duplicates")
    for index, cell in enumerate(hidden_cells):
        require_int(cell, f"hidden_cells[{index}]", 0, width * height - 1)
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


def content_id(name: str) -> int:
    value = 14695981039346656037
    for byte in name.encode("utf-8"):
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


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
    # Sample one decision for a 2x2 world patch.  Cell-by-cell borrowing made
    # the infinite apron look like a noisy checkerboard and fragmented the
    # PS1 mesh into needless tiny runs.  Broad patches read as landscape and
    # remain deterministic while the camera scrolls.
    roll = hash_cell(x // 2, y // 2, seed)
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
        # 8-bpp stores two texels in each VRAM word. A 32-word grid permits
        # two 64px poses in one hardware texture page while retaining a valid
        # 0/64 UV offset for each.
        self.column_words = 32
        self.columns = TEXTURE_WIDTH // self.column_words
        self.rows = TEXTURE_HEIGHT // 32
        self.used = [[False] * self.columns for _ in range(self.rows)]
        palette_left = (CLUT_X - TEXTURE_X) // self.column_words
        palette_right = math.ceil(
            (CLUT_X - TEXTURE_X + CLUT_COLOURS) / self.column_words
        )
        palette_row = (CLUT_Y - TEXTURE_Y) // 32
        for column in range(palette_left, palette_right):
            self.used[palette_row][column] = True

    def place(self, width: int, height: int, depth_bits: int = 8) -> tuple[int, int]:
        page_width = 1024 // depth_bits
        if width > page_width or height > TEXTURE_PAGE_HEIGHT:
            raise ValueError("generated texture exceeds one PS1 texture page")
        width_words = math.ceil(width * depth_bits / 16)
        columns = math.ceil(width_words / self.column_words)
        rows = math.ceil(height / 32)
        for y in range(self.rows - rows + 1):
            pixel_y = TEXTURE_Y + y * 32
            if pixel_y // TEXTURE_PAGE_HEIGHT != (
                pixel_y + height - 1
            ) // TEXTURE_PAGE_HEIGHT:
                continue
            for x in range(self.columns - columns + 1):
                if any(self.used[yy][xx] for yy in range(y, y + rows) for xx in range(x, x + columns)):
                    continue
                for yy in range(y, y + rows):
                    for xx in range(x, x + columns):
                        self.used[yy][xx] = True
                pixel_x = TEXTURE_X + x * self.column_words
                page_word_offset = pixel_x & 63
                if page_word_offset + width_words > 64:
                    for yy in range(y, y + rows):
                        for xx in range(x, x + columns):
                            self.used[yy][xx] = False
                    continue
                return pixel_x, TEXTURE_Y + y * 32
        raise ValueError("generated textures exceed the framebuffer-free VRAM strip")


def opaque_psx_pixel(red: int, green: int, blue: int) -> int:
    value = (red >> 3) | ((green >> 3) << 5) | ((blue >> 3) << 10)
    return value or 1


def shared_stage_palette(
    images: list[Image.Image],
) -> tuple[tuple[tuple[int, int, int], ...], Image.Image]:
    """Build one deterministic 8-bpp palette for the complete stage.

    Index zero remains transparent. The remaining 255 colours are learned
    jointly from terrain, UI, every team colour and every animation pose, so
    page changes never need a palette upload and visual identity is retained.
    """
    visible: list[tuple[int, int, int]] = []
    for image in images:
        rgba = image.convert("RGBA")
        flattened = getattr(rgba, "get_flattened_data", None)
        pixels = flattened() if flattened is not None else rgba.getdata()
        for red, green, blue, alpha in pixels:
            if alpha >= 40:
                visible.append((red, green, blue))
    if not visible:
        raise ValueError("stage texture bank has no visible pixels")
    sample_width = 256
    sample_height = math.ceil(len(visible) / sample_width)
    sample = Image.new("RGB", (sample_width, sample_height), visible[-1])
    sample.putdata(visible + [visible[-1]] * (sample_width * sample_height - len(visible)))
    quantized = sample.quantize(
        colors=CLUT_COLOURS - 1,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    )
    raw = list(quantized.getpalette() or [])
    fallback = raw[-3:] if len(raw) >= 3 else [255, 255, 255]
    while len(raw) < (CLUT_COLOURS - 1) * 3:
        raw.extend(fallback)
    colours = tuple(
        (raw[index], raw[index + 1], raw[index + 2])
        for index in range(0, (CLUT_COLOURS - 1) * 3, 3)
    )
    palette_image = Image.new("P", (1, 1))
    palette_image.putpalette(
        [channel for colour in colours for channel in colour] + list(colours[-1])
    )
    return colours, palette_image


def tim_words(
    image: Image.Image, x: int, y: int, cutout: bool,
    colours: tuple[tuple[int, int, int], ...], palette_image: Image.Image,
    include_palette: bool = True,
    depth_bits: int = 8, palette_slot: int = 0,
    palette_row: int = CLUT_Y,
    font_palette: bool = False,
) -> list[int]:
    rgba = image.convert("RGBA")
    palette_size = 256
    palette_x, palette_y = CLUT_X, palette_row
    if font_palette:
        if depth_bits != 4:
            raise ValueError("font palette requires 4-bit texture storage")
        colours = ((255, 255, 255),) * 15
        palette_image = Image.new("P", (1, 1))
        palette_image.putpalette([255, 255, 255] * 256)
        palette_size = 16
        palette_x, palette_y = CLUT_X, CLUT_Y - 3
        include_palette = True
    elif depth_bits == 4:
        quantized = rgba.convert("RGB").quantize(colors=15, method=Image.Quantize.MEDIANCUT)
        raw_palette = list(quantized.getpalette() or [])
        raw_palette += [0] * max(0, 45 - len(raw_palette))
        colours = tuple(tuple(raw_palette[i:i + 3]) for i in range(0, 45, 3))
        palette_image = Image.new("P", (1, 1))
        palette_image.putpalette([c for colour in colours for c in colour] + list(colours[-1]) * 241)
        palette_size = 16
        palette_x, palette_y = CLUT_X + palette_slot * 16, CLUT_Y - 1
        include_palette = True
    indexed = rgba.convert("RGB").quantize(
        palette=palette_image, dither=Image.Dither.FLOYDSTEINBERG,
    )
    indexed_flattened = getattr(indexed, "get_flattened_data", None)
    alpha = rgba.getchannel("A")
    alpha_flattened = getattr(alpha, "get_flattened_data", None)
    source_indices = list(
        indexed_flattened() if indexed_flattened is not None else indexed.getdata()
    )
    source_alpha = list(
        alpha_flattened() if alpha_flattened is not None else alpha.getdata()
    )
    pixels = bytearray()
    for row in range(rgba.height):
        start = row * rgba.width
        row_indices = []
        for column in range(rgba.width):
            offset = start + column
            row_indices.append(
                0 if cutout and source_alpha[offset] < 40
                else min(source_indices[offset], palette_size - 2) + 1
            )
        texels_per_word = 16 // depth_bits
        row_indices.extend([0] * (-len(row_indices) % texels_per_word))
        if depth_bits == 4:
            pixels.extend(row_indices[i] | (row_indices[i + 1] << 4)
                          for i in range(0, len(row_indices), 2))
        else:
            pixels.extend(row_indices)
    palette = [0, *(opaque_psx_pixel(*colour) for colour in colours)]
    mode = 0 if depth_bits == 4 else 1
    data = bytearray(struct.pack("<II", 0x10, mode | (8 if include_palette else 0)))
    if include_palette:
        data.extend(struct.pack("<IHHHH", 12 + len(palette) * 2,
                                palette_x, palette_y, len(palette), 1))
        data.extend(struct.pack(f"<{len(palette)}H", *palette))
    width_words = math.ceil(rgba.width * depth_bits / 16)
    data.extend(struct.pack("<IHHHH", 12 + len(pixels),
                            x, y, width_words, rgba.height))
    data.extend(pixels)
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
    crop_alpha = crop.getchannel("A")
    colour = ImageEnhance.Contrast(crop.convert("RGB")).enhance(1.08)
    colour = ImageEnhance.Color(colour).enhance(1.06)
    colour = ImageEnhance.Sharpness(colour).enhance(1.3)
    crop = colour.convert("RGBA")
    crop.putalpha(crop_alpha)
    result = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    # Ground the billboard without spending another entity, mesh or texture.
    # A separate ground-plane shadow made combat miss the 30 fps frame gate;
    # this deliberately coarse, dithered cue is part of the existing cutout.
    shadow = ImageDraw.Draw(result)
    shadow_left = side * 3 // 16
    shadow_right = side - shadow_left - 1
    shadow_top = side - max(4, side // 9)
    shadow_bottom = side - 2
    shadow.ellipse(
        (shadow_left, shadow_top, shadow_right, shadow_bottom),
        fill=(20, 23, 26, 255),
    )
    # Knock alternating pixels from the perimeter so the hard PS1 cutout
    # reads as a soft contact shadow after texture filtering.
    pixels = result.load()
    assert pixels is not None
    for y in range(shadow_top, shadow_bottom + 1):
        for x in range(shadow_left, shadow_right + 1):
            if pixels[x, y][3] and (
                x <= shadow_left + 1 or x >= shadow_right - 1
                or y == shadow_top or y == shadow_bottom
            ) and (x + y) % 2:
                pixels[x, y] = (0, 0, 0, 0)
    placement = ((side - crop.width) // 2, side - crop.height)
    # A one-pixel silhouette survives affine nearest-neighbour sampling much
    # better than the soft alpha fringe in the high-resolution source art.
    outline = Image.new("RGBA", crop.size, (18, 22, 29, 255))
    outline.putalpha(crop.getchannel("A").filter(ImageFilter.MaxFilter(3)))
    result.alpha_composite(outline, placement)
    result.alpha_composite(crop, placement)
    result.putalpha(result.getchannel("A").point(lambda value: 255 if value >= 40 else 0))
    return result


def shifted_colour(
    colour: tuple[int, int, int], numerator: int,
) -> tuple[int, int, int]:
    return tuple(
        max(0, min(255, channel * numerator // 100)) for channel in colour
    )


def blended_colour(
    first: tuple[int, int, int], second: tuple[int, int, int],
    second_weight: int,
) -> tuple[int, int, int]:
    return tuple(
        (left * (100 - second_weight) + right * second_weight) // 100
        for left, right in zip(first, second)
    )


def terrain_colours(repo: Path, kind: int, theme: int = 0) -> tuple:
    base = tuple(ground_materials(repo)["palettes"][theme][kind])
    return base, shifted_colour(base, 90), shifted_colour(base, 110)


def terrain_image(repo: Path, kind: int, theme: int = 0) -> Image.Image:
    source = ground_image(repo, kind, theme)
    image = Image.new("RGB", (128, 32))
    tile = source.resize((32, 32), Image.Resampling.LANCZOS)
    for variant in range(4):
        # Keep the full Godot tile: quarter crops magnify paving fourfold.
        rotated = tile.transpose((Image.Transpose.FLIP_LEFT_RIGHT,
                                  Image.Transpose.FLIP_TOP_BOTTOM,
                                  Image.Transpose.ROTATE_180)[variant - 1]) if variant else tile
        image.paste(rotated, (variant * 32, 0))
    return image


def unit_image(
    repo: Path, unit: dict[str, object], profiles: dict[str, object],
    pose: int, directional: bool = False, side: int = 64,
) -> Image.Image:
    style_index = require_int(unit.get("style"), "unit style", 0, len(STYLE_NAMES) - 1)
    archetype_index = require_int(unit.get("archetype"), "unit archetype", 0, len(ARCHETYPE_NAMES) - 1)
    figure_index = require_int(unit.get("figure"), "unit figure", 0, len(FIGURE_NAMES) - 1)
    colour_index = require_int(unit.get("colour"), "unit colour", 0, len(COLOUR_NAMES) - 1)
    key = f"{ARCHETYPE_NAMES[archetype_index]}|*|{STYLE_NAMES[style_index]}|{FIGURE_NAMES[figure_index]}"
    binding = profiles.get(key)
    if not isinstance(binding, dict):
        raise ValueError(f"Godot graphics profile has no character binding {key!r}")
    sheet_binding = binding.get("directionalSheet") if directional else binding
    if not isinstance(sheet_binding, dict):
        raise ValueError(f"Godot graphics profile has no directional binding {key!r}")

    def resource(value: object) -> Path:
        text = str(value)
        if not text.startswith("res://"):
            raise ValueError(f"character resource is not project-relative: {text!r}")
        return repo / "platform/godot/project" / text.removeprefix("res://")

    left = pose % 4 * CELL_SOURCE_SIZE
    top = pose // 4 * CELL_SOURCE_SIZE
    bounds = (left, top, left + CELL_SOURCE_SIZE, top + CELL_SOURCE_SIZE)
    with Image.open(resource(sheet_binding.get("path"))) as sheet:
        source = sheet.convert("RGBA").crop(bounds)
    with Image.open(resource(sheet_binding.get("teamMaskPath"))) as sheet:
        mask = sheet.convert("RGBA").crop(bounds).getchannel("A")
    team = TEAM_COLOURS[colour_index]
    gray = ImageOps.grayscale(source)
    tinted = ImageOps.colorize(
        gray, tuple(max(0, channel // 5) for channel in team),
        tuple(min(255, channel + (255 - channel) * 3 // 4) for channel in team),
    ).convert("RGBA")
    tinted.putalpha(source.getchannel("A"))
    return fitted_cutout(Image.composite(tinted, source, mask), side)


def write_textures(
    output: Path, repo: Path, terrain_kinds: list[int], units: list[dict[str, object]],
    map_id: str = "0", speakers: list[dict[str, object]] | None = None,
    castle: bool = False, houses: bool = False, theme: int = 0, grove: bool = False,
    house_drawings: tuple[int, ...] = (),
) -> tuple[
    dict[int, int], dict[tuple[int, int, int, int], UnitTextureIndices],
    int, dict[str, object], dict[int, int],
]:
    profiles_path = repo / "platform/godot/project/content/graphics_profiles.json"
    profiles = json.loads(profiles_path.read_text(encoding="utf-8"))["profiles"]["voxel-sprites"]["assets"]["characterSprites"]
    packer = TexturePacker()
    textures: list[tuple[str, Image.Image, bool, int, int]] = []
    terrain_indices: dict[int, int] = {}
    unit_indices: dict[
        tuple[int, int, int, int], UnitTextureIndices
    ] = {}

    for kind in sorted(set(terrain_kinds)):
        texture = terrain_image(repo, kind, theme)
        textures.append((f"terrain_{TERRAIN_NAMES[kind]}", texture, False, 0, 0))
        terrain_indices[kind] = len(textures)

    # Small material swatches give native terrain forms surface detail at
    # every viewing distance, without adding an entity or another polygon.
    # Face colours supply bark, foliage and stone hues. Neutral detail avoids
    # multiplying two coloured surfaces and muddying those authored colours.
    for material, base in ((-1, (235, 235, 235)), (-2, (235, 235, 235)),
                           (-3, (235, 235, 235))):
        swatch = Image.new("RGBA", (32, 32), (*base, 255))
        pixels = swatch.load()
        for y in range(32):
            for x in range(32):
                roll = hash_cell(x, y, 0x505331 + material)
                grain = (roll % 19) - 9
                if material == -1:
                    grain += -24 if (x + y // 9) % 9 == 0 else 0
                elif material == -2:
                    grain += 15 if (x // 3 + y // 4) % 3 == 0 else -7
                else:
                    grain += -23 if y % 11 == 0 or (x + y // 11 * 7) % 17 == 0 else 0
                pixels[x, y] = tuple(max(0, min(255, c + grain)) for c in base) + (255,)
        textures.append((f"form_material_{-material}", swatch, False, 0, 0))
        terrain_indices[material] = len(textures)

    if castle:
        stone = Image.open(repo / "platform/console_art/stone_keep_masonry.png").convert("RGBA")
        stone = stone.resize((32, 32), Image.Resampling.BOX)
        masonry = Image.new("RGBA", (128, 128))
        for y in range(0, 128, 32):
            for x in range(0, 128, 32):
                masonry.paste(stone, (x, y))
        textures.append(("form_material_4", masonry, False, 0, 0))
        terrain_indices[-4] = len(textures)

    if houses:
        for drawing in (house_drawings or tuple(range(4))):
            textures.append((f"scenery_house_{drawing}", house_image(repo,drawing,96), True, 0, 0))
            terrain_indices[-10-drawing] = len(textures)

    if grove:
        textures.append(("scenery_grove", grove_image(repo, 96, theme), True, 0, 0))
        terrain_indices[-100] = len(textures)

    for unit in units:
        key = (
            int(unit["style"]), int(unit["archetype"]), int(unit["figure"]),
            int(unit["colour"]),
        )
        if key in unit_indices:
            continue
        drawing = len(unit_indices)
        pose_indices = []
        for pose_name, pose_index, directional, side in UNIT_POSES:
            texture = unit_image(
                repo, unit, profiles, pose_index, directional, side
            )
            textures.append((
                f"unit_{drawing}_{pose_name}", texture, True, 0, 0,
            ))
            pose_indices.append(len(textures))
        unit_indices[key] = tuple(pose_indices)  # type: ignore[assignment]

    # Cast members may speak before entering the board. Reuse resident idle
    # textures and add only a small portrait for each absent visual variant.
    speaker_indices: dict[int, int] = {}
    portrait_variants: dict[tuple[int, int, int, int], int] = {}
    for speaker in speakers or []:
        key = tuple(int(speaker[field]) for field in
                    ("style", "archetype", "figure", "colour"))
        if key in unit_indices:
            texture_index = unit_indices[key][0]
        elif key in portrait_variants:
            texture_index = portrait_variants[key]
        else:
            texture = unit_image(repo, speaker, profiles, 0, side=32)
            textures.append((f"speaker_{len(portrait_variants)}", texture, True, 0, 0))
            texture_index = len(textures)
            portrait_variants[key] = texture_index
        speaker_indices[int(speaker["unit_type_id"])] = texture_index

    # One tiny, deterministic font atlas lets the target draw live health,
    # forecasts and arbitrary ability counts without shipping Sony's debug
    # font or baking encounter-specific text into the background.
    # Keep the atlas inside one indexed PS1 texture page.
    ui = Image.open(repo / "assets/fonts/spleen/console-latin-6x12.png").convert("RGBA")
    if ui.size != (64, 144):
        raise ValueError("console font atlas must contain 8x12 pixel cells")
    textures.append(("compact_ui_font", ui, True, 0, 0))
    ui_index = len(textures)
    backdrop_indices = []
    backdrop_root = repo / "platform/godot/project"
    backdrop_manifest = json.loads((backdrop_root / "art/combat_backdrops/manifest.json").read_text())
    map_hash = f"{int(map_id):016x}"
    backdrop_pack = next((pack for pack in backdrop_manifest["packs"]
                          if map_hash in pack.get("compatibility", {}).get("map_ids", [])), None)
    if backdrop_pack is None and units and all(int(unit["style"]) == 0 for unit in units):
        dominant = TERRAIN_NAMES[max(set(terrain_kinds), key=terrain_kinds.count)]
        backdrop_pack = next((pack for pack in backdrop_manifest["packs"]
                              if not pack.get("compatibility") and pack.get("dominant_terrain") == dominant), None)
    if backdrop_pack is not None:
        source = backdrop_root / backdrop_pack["root"].removeprefix("res://") / backdrop_pack["filename_patterns"]["panorama"].format(variant=0)
        with Image.open(source) as panorama:
            plate = panorama.convert("RGBA").resize((320, 240), Image.Resampling.LANCZOS)
        for strip in range(4):
            textures.append((f"combat_plate_{strip}", plate.crop((strip * 80, 0, (strip + 1) * 80, 240)), False, 0, 0))
            backdrop_indices.append(len(textures))
    if len(textures) > 256:
        raise ValueError("stage exceeds Mipsync's 256 texture descriptors")

    # Pack the tallest rectangles first. Scene order is semantic (indices are
    # embedded in meshes), but atlas placement is not; separating the two
    # avoids the narrow holes produced by first-fit terrain strips on stages
    # with a broader unit roster.
    placements: dict[int, tuple[int, int]] = {}
    material_palettes = {
        index: slot for slot, index in enumerate(
            index for index, (name, *_rest) in enumerate(textures)
            if name.startswith(("terrain_", "form_material_"))
        )
    }
    if len(material_palettes) > 16:
        raise ValueError("4-bit terrain bank exceeds reserved CLUT slots")
    font_index = ui_index - 1
    house_palettes = {
        index: slot for slot, index in enumerate(index for index, (name, *_rest) in enumerate(textures)
                                               if name.startswith("scenery_house_"))
    }
    # These rows are already excluded from texture allocation by TexturePacker.
    # Keep all private CLUTs inside that reserved final 32-line block.
    if house_palettes and CLUT_Y - 4 - (len(house_palettes) - 1) < (CLUT_Y // 32) * 32:
        raise ValueError("house palettes exceed the reserved CLUT rows")
    palette_owner = next((index for index in range(len(textures))
                          if index not in material_palettes and index != font_index
                          and index not in house_palettes and index + 1 not in backdrop_indices), None)
    # Only one unit walks at a time. Its six compass/contact frames share a
    # slot, while idle, attack and hurt remain independently resident so two
    # identical archetypes can fight without overwriting each other's art.
    residency_slots: dict[int, int] = {}
    for poses in unit_indices.values():
        owner = poses[1] - 1
        for pose in poses[1:7]:
            residency_slots[pose - 1] = owner
    for index in sorted(
        range(len(textures)),
        key=lambda item: (
            -math.ceil(textures[item][1].height / 32),
            -math.ceil(textures[item][1].width / 64),
            item,
        ),
    ):
        if index in residency_slots and residency_slots[index] != index:
            continue
        image = textures[index][1]
        placements[index] = packer.place(image.width, image.height,
                                         4 if index in material_palettes or index == font_index else 8)
    for index, owner in residency_slots.items():
        placements[index] = placements[owner]
    textures = [
        (name, image, cutout, *placements[index])
        for index, (name, image, cutout, _x, _y) in enumerate(textures)
    ]
    palette_colours, palette_image = shared_stage_palette(
        [image.crop((0, 0, 64, 96)) if name == "compact_ui_font" else image
         for name, image, _cutout, _x, _y in textures
         if not name.startswith("combat_plate_")]
    )
    backdrop_palette = shared_stage_palette(
        [textures[index - 1][1] for index in backdrop_indices]
    ) if backdrop_indices else None

    lines = ["/* Generated from a real Grandleon scene manifest. Do not edit. */", '#include "../runtime/textures.h"', ""]
    image_vram_bytes = 0
    embedded_texture_bytes = 0
    for index, (name, image, cutout, x, y) in enumerate(textures):
        is_backdrop = index + 1 in backdrop_indices
        colours, palette = (shared_stage_palette([image]) if index in house_palettes else
                            backdrop_palette if is_backdrop else (palette_colours, palette_image))
        words = tim_words(
            image, x, y, cutout, colours, palette,
            include_palette=True if index in house_palettes else
                            (index + 1 == backdrop_indices[0]) if is_backdrop else index == palette_owner,
            depth_bits=4 if index in material_palettes or index == font_index else 8,
            palette_slot=material_palettes.get(index, 0),
            palette_row=CLUT_Y - 4 - house_palettes[index] if index in house_palettes else
                        CLUT_Y - 2 if is_backdrop else CLUT_Y,
            font_palette=index == font_index,
        )
        lines.extend((f"static const uint32_t k_{name}_tim[] = {{", c_words(words), "};", ""))
        if residency_slots.get(index, index) == index:
            bits = 4 if index in material_palettes or index == font_index else 8
            image_vram_bytes += math.ceil(image.width * bits / 16) * image.height * 2
        embedded_texture_bytes += len(words) * 4
    lines.append("const ps1_texture_desc g_ps1_textures[] = {")
    for index, (name, _image, cutout, _x, _y) in enumerate(textures):
        slot = residency_slots.get(index, index) + 1
        clut_owner = (index + 1 if index == font_index or index in house_palettes or index in material_palettes else
                      backdrop_indices[0] if index + 1 in backdrop_indices else
                      palette_owner + 1 if palette_owner is not None else 0)
        lines.append(
            f"    {{ k_{name}_tim, sizeof(k_{name}_tim) / sizeof(k_{name}_tim[0]), 0, {1 if cutout else 0}, {clut_owner}, {slot} }},"
        )
    lines.extend(("};", "const unsigned int g_ps1_texture_count =", "    sizeof(g_ps1_textures) / sizeof(g_ps1_textures[0]);", ""))
    lines.append("const unsigned int g_grandleon_combat_plate_indices[4] = {" + ",".join(map(str, backdrop_indices or [0] * 4)) + "};")
    (output / "textures_data.c").write_text("\n".join(lines), encoding="utf-8")
    occupied_vram_bytes = image_vram_bytes + (CLUT_COLOURS * 2 if palette_owner is not None else 0) + len(material_palettes) * 32 + 32 + (512 if backdrop_indices else 0) + len(house_palettes) * 512
    metrics = {
        "texture_count": len(textures),
        "texture_depth_bits": TEXTURE_DEPTH_BITS,
        "material_depth_bits": 4,
        "font_depth_bits": 4,
        "font_glyph_count": 96,
        "font_clut_row": CLUT_Y - 3,
        "palette_colours": CLUT_COLOURS,
        "palette_strategy": "shared_units_private_houses" if house_palettes else "shared_stage_clut",
        "house_palette_count": len(house_palettes),
        "residency_strategy": "resident_idle_combat_paged_walk",
        "combat_plate": bool(backdrop_indices),
        "texture_bytes": occupied_vram_bytes,
        "embedded_texture_bytes": embedded_texture_bytes,
        "vram_reserved_bytes": (
            TEXTURE_WIDTH * TEXTURE_HEIGHT * 2 - occupied_vram_bytes
        ),
    }
    return terrain_indices, unit_indices, ui_index, metrics, speaker_indices


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


def relief_height(kind: int, x: int, y: int, seed: int) -> int:
    """Return a deterministic, restrained height for one terrain cell."""
    base = TERRAIN_RELIEF_HEIGHTS[kind]
    if kind in (0, 1, 6, 12):
        return base
    variation = int((hash_cell(x, y, seed) >> 9) % 5) - 2
    return base + variation


def raw_corner_relief(
    x: int, y: int, cell_lifts: dict[tuple[int, int], int],
) -> int:
    values = [
        cell_lifts[(cell_x, cell_y)]
        for cell_x, cell_y in (
            (x - 1, y - 1), (x, y - 1), (x - 1, y), (x, y),
        )
        if (cell_x, cell_y) in cell_lifts
    ]
    return round(sum(values) / len(values)) if values else 0


def corner_relief(
    x: int, y: int, cell_lifts: dict[tuple[int, int], int],
) -> int:
    """Sample a terrain-aware height field linear across four-cell bands."""
    minimum_x = min(cell[0] for cell in cell_lifts)
    maximum_x = max(cell[0] for cell in cell_lifts) + 1
    clamped_x = max(minimum_x, min(maximum_x, x))
    left = minimum_x + ((clamped_x - minimum_x) // 4) * 4
    right = min(maximum_x, left + 4)
    if left == right:
        return raw_corner_relief(left, y, cell_lifts)
    left_height = raw_corner_relief(left, y, cell_lifts)
    right_height = raw_corner_relief(right, y, cell_lifts)
    return round(
        (left_height * (right - clamped_x) +
         right_height * (clamped_x - left)) / (right - left)
    )


def surface_relief(
    x: int, y: int, cell_lifts: dict[tuple[int, int], int],
) -> int:
    return round(sum((
        corner_relief(x, y, cell_lifts),
        corner_relief(x + 1, y, cell_lifts),
        corner_relief(x, y + 1, cell_lifts),
        corner_relief(x + 1, y + 1, cell_lifts),
    )) / 4)


def page_cells(
    cells: list[tuple[int, int, int, bool, int, int]],
    width: int, height: int, apron_depth: int,
) -> list[tuple[tuple[int, int], list[tuple[int, int, int, bool, int, int]]]]:
    pages = []
    for start_y in page_starts(height, CAMERA_VIEW_TILES_Y):
        end_y = min(height, start_y + CAMERA_VIEW_TILES_Y)
        page_min_y = max(-apron_depth, start_y - 2)
        page_max_y = min(height + apron_depth, end_y + 2)
        for start_x in page_starts(width, CAMERA_VIEW_TILES_X):
            end_x = min(width, start_x + CAMERA_VIEW_TILES_X)
            page_min_x = max(-apron_depth, start_x - 2)
            page_max_x = min(width + apron_depth, end_x + 2)
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


def surface_strips(cells: list[tuple], width: int, height: int, depth: int) -> list[tuple]:
    """Continuous floor with each mesh below the engine's vertex limit."""
    rows = max(1, MAX_CACHE_VERTICES // (width + depth * 2 + 1) - 1)
    strips = []
    for first_y in range(-depth, height + depth, rows):
        last_y = min(height + depth, first_y + rows)
        clipped = []
        for x, y, kind, authored, span_x, span_y in cells:
            top, bottom = max(y, first_y), min(y + span_y, last_y)
            if top < bottom:
                clipped.append((x, top, kind, authored, span_x, bottom - top))
        strips.append(((0, first_y), clipped))
    return strips


def coalesce_vertical_runs(
    cells: list[tuple[int, int, int, bool, int, int]],
    cell_lifts: dict[tuple[int, int], int],
) -> list[tuple[int, int, int, bool, int, int]]:
    """Join compatible row pairs; seam-fill covers PS1 T-junction pinholes."""
    lookup = {
        (x, y, kind, authored, span_x): cell
        for cell in cells
        for x, y, kind, authored, span_x, span_y in (cell,)
        if span_y == 1
    }
    consumed: set[tuple[int, int, int, bool, int]] = set()
    merged: list[tuple[int, int, int, bool, int, int]] = []
    for cell in sorted(cells, key=lambda value: (value[1], value[0])):
        x, y, kind, authored, span_x, span_y = cell
        key = (x, y, kind, authored, span_x)
        if key in consumed:
            continue
        below = (x, y + 1, kind, authored, span_x)
        can_merge = kind not in (8, 12) and span_y == 1 and below in lookup and below not in consumed
        if can_merge:
            for edge_x in (x, x + span_x):
                top = corner_relief(edge_x, y, cell_lifts)
                middle = corner_relief(edge_x, y + 1, cell_lifts)
                bottom = corner_relief(edge_x, y + 2, cell_lifts)
                if middle * 2 != top + bottom:
                    can_merge = False
                    break
        merged.append((x, y, kind, authored, span_x, 2 if can_merge else span_y))
        consumed.add(key)
        if can_merge:
            consumed.add(below)
    return merged


def append_terrain_form(
    vertices: list[tuple[int, int, int, int]],
    uvs: list[tuple[int, int]],
    triangles: list[tuple[int, int, int, int, int, int, int, int]],
    kind: int, cx: int, floor: int, cz: int, roll: int,
    materials: dict[int, int],
) -> None:
    """Append one small native low-poly landmark for a terrain cell."""
    colour, scale = TERRAIN_FORM_STYLES[kind]
    if kind in (0, 2, 6, 9, 10, 11):
        # Stable per-cell variation survives scrolling and scene reloads.
        # The narrow palette stays within the terrain's own visual identity.
        foliage_tints = ((100, 100, 100), (112, 103, 84),
                         (86, 96, 109), (103, 111, 92))
        tint = foliage_tints[(roll >> 13) & 3]
        colour = tuple(min(255, c * t // 100) for c, t in zip(colour, tint))
    light = shifted_colour(colour, 122)
    width_percent = 88 + int((roll >> 3) % 25)
    depth_percent = 88 + int((roll >> 19) % 25)
    height_percent = 90 + int((roll >> 25) % 21)

    def vertex(x: int, y: int, z: int) -> int:
        if kind != 2:
            # Vary reeds, outcrops and shrubs too; authored castle walls and
            # the forest's independently varied canopies retain their shape.
            x = cx + (x - cx) * width_percent // 100
            z = cz + (z - cz) * depth_percent // 100
            y = floor + 1 + (y - floor - 1) * height_percent // 100
        index = len(vertices)
        vertices.append((x, y, z, 0))
        uvs.append((((x - cx + z - cz) // 4 + 16) % 32,
                    ((y - floor + z - cz) // 4) % 32))
        return index

    def face(a: int, b: int, c: int, face_colour: tuple[int, int, int],
             material: int | None = None) -> None:
        if material is None:
            material = -2 if kind in (0, 2, 6, 10, 11) else -3
        # Neutral texture modulation preserves the form's authored palette.
        triangles.append((a, b, c, materials[material], 255, *face_colour))

    def pyramid(
        x: int, z: int, radius_x: int, radius_z: int, height: int,
        face_colour: tuple[int, int, int], bottom: int = 1,
    ) -> None:
        front_left = vertex(x - radius_x, floor + bottom, z + radius_z)
        front_right = vertex(x + radius_x, floor + bottom, z + radius_z)
        back_right = vertex(x + radius_x, floor + bottom, z - radius_z)
        back_left = vertex(x - radius_x, floor + bottom, z - radius_z)
        apex = vertex(x, floor + bottom + height, z)
        if kind == 2:
            for index, uv in zip((front_left, front_right, back_right, back_left, apex),
                                 ((0, 31), (31, 31), (31, 0), (0, 0), (16, 16))):
                uvs[index] = uv
        material = -1 if face_colour == (102, 70, 42) else None
        face(front_left, front_right, apex, shifted_colour(face_colour, 114), material)
        face(front_right, back_right, apex, face_colour, material)
        face(back_right, back_left, apex, shifted_colour(face_colour, 82), material)
        face(back_left, front_left, apex, shifted_colour(face_colour, 92), material)

    def box(
        x: int, z: int, radius_x: int, radius_z: int, height: int,
        face_colour: tuple[int, int, int], bottom: int = 1,
    ) -> None:
        base = floor + bottom
        top = base + height
        front_left = vertex(x - radius_x, base, z + radius_z)
        front_right = vertex(x + radius_x, base, z + radius_z)
        back_right = vertex(x + radius_x, base, z - radius_z)
        back_left = vertex(x - radius_x, base, z - radius_z)
        top_front_left = vertex(x - radius_x, top, z + radius_z)
        top_front_right = vertex(x + radius_x, top, z + radius_z)
        top_back_right = vertex(x + radius_x, top, z - radius_z)
        top_back_left = vertex(x - radius_x, top, z - radius_z)
        material = -1 if face_colour == (102, 70, 42) else None
        front_colour = shifted_colour(face_colour, 114)
        face(front_left, front_right, top_front_right, front_colour, material)
        face(front_left, top_front_right, top_front_left, front_colour, material)
        face(front_right, back_right, top_back_right, face_colour, material)
        face(front_right, top_back_right, top_front_right, face_colour, material)
        face(top_front_left, top_front_right, top_back_right,
             shifted_colour(face_colour, 128), material)
        face(top_front_left, top_back_right, top_back_left,
             shifted_colour(face_colour, 128), material)

    def boulder(x: int, z: int, width: int, depth: int, height: int,
                face_colour: tuple[int, int, int]) -> None:
        # A fractured ridge has a broad footprint and unequal shoulders,
        # rather than the single apex that made every outcrop a white spike.
        points = ((-width, 1, depth), (width, 1, depth),
                  (width, 1, -depth), (-width, 1, -depth),
                  (-width // 2, height * 3 // 4, depth // 5),
                  (width // 3, height * 3 // 5, depth // 4),
                  (-width // 7, height, -depth // 2))
        indices = [vertex(x + dx, floor + dy, z + dz) for dx, dy, dz in points]
        for a, b, c, brightness in ((0, 1, 5, 106), (0, 5, 4, 106),
                                    (1, 2, 6, 75), (1, 6, 5, 75),
                                    (2, 3, 6, 82), (3, 0, 4, 116),
                                    (3, 4, 6, 116), (4, 5, 6, 124)):
            face(indices[a], indices[b], indices[c],
                 shifted_colour(face_colour, brightness))

    if kind == 2:  # forest: trunk plus a layered, faceted canopy
        canopy_height = 60 + int((roll >> 5) % 37)
        canopy_width = 33 + int((roll >> 18) % 18)
        canopy_depth = canopy_width - 8 + int((roll >> 23) % 13)
        lean = int((roll >> 9) % 13) - 6
        trunk_height = 35 + canopy_height // 10
        # The sheltered trunk needs no top cap. Its tapered four-face mesh
        # leaves the same triangle budget for a rounder six-sided main crown.
        pyramid(cx, cz, 6, 6, trunk_height, (102, 70, 42))
        ring = [vertex(cx - 5 + lean + dx, floor + 22, cz + 2 + dz)
                for dx, dz in ((-canopy_width, 0), (-canopy_width // 2, -canopy_depth),
                               (canopy_width // 2, -canopy_depth), (canopy_width, 0),
                               (canopy_width // 2, canopy_depth), (-canopy_width // 2, canopy_depth))]
        apex = vertex(cx - 5 + lean, floor + 22 + canopy_height, cz + 2)
        for index, uv in zip(ring + [apex],
                             ((0, 16), (8, 0), (24, 0), (31, 16),
                              (24, 31), (8, 31), (16, 16))):
            uvs[index] = uv
        for side, brightness in enumerate((100, 86, 78, 92, 114, 122)):
            face(ring[(side + 1) % 6], ring[side], apex,
                 shifted_colour(colour, brightness))
        pyramid(cx + lean, cz - 3, canopy_width * 2 // 3, canopy_depth * 2 // 3,
                canopy_height // 2, colour, 22 + canopy_height * 3 // 4)
    elif kind == 3:  # mountain: fractured stone, with a smaller weathered spur
        stone_tones = ((111, 128, 136), (130, 125, 112), (115, 132, 125))
        stone = stone_tones[(roll >> 15) % len(stone_tones)]
        boulder(cx - 12, cz + 7, 48, 42, 102, stone)
        pyramid(cx + 32, cz - 17, 29, 26, 65, shifted_colour(stone, 106))
    elif kind == 11:  # bamboo: solid clustered stalks
        box(cx - 9, cz + 4, 4, 4, 108, colour)
        box(cx + 10, cz - 3, 4, 4, 128, light)
        box(cx + 1, cz + 13, 3, 3, 91, shifted_colour(colour, 90))
    elif kind == 10:  # farmland: short crop rows with visible volume
        for offset, height in ((-22, 42), (0, 54), (22, 48)):
            box(cx + offset, cz + offset // 6, 4, 5, height, colour)
    elif kind in (0, 6):  # water/swamp: sparse native reed fans
        for offset, height in ((-11, 45), (12, 58)):
            pyramid(
                cx + offset, cz + offset // 5, 4, 5, height,
                light if offset < 0 else colour,
            )
    elif kind in (1, 12):  # road/paved: two shallow raised stones
        box(cx - 22, cz + 8, 27, 20, 9, colour)
        box(cx + 24, cz - 14, 24, 18, 7, light)
    elif kind == 8:  # ruins: broken wall profile plus fallen masonry
        if roll & 3 == 0:
            box(cx - 25, cz, 7, 9, 58, colour)
            box(cx + 25, cz, 7, 9, 58, light)
            box(cx, cz, 33, 10, 12, colour, 58)
        else:
            box(cx - 10, cz, 28, 8, 58, colour)
        box(cx + 29, cz + 15, 20, 16, 15, light)
    elif kind == 9:  # meadow: rounded low scrub, not miniature conifers
        boulder(cx - 18, cz + 7, 24, 21, round(31 * scale[1]), colour)
        boulder(cx + 24, cz - 13, 17, 15, round(23 * scale[1]), light)
    elif kind == 5:  # snow: one clear faceted ice shard
        pyramid(cx, cz, 19, 16, 82, light)
    elif kind == 4:  # sand: broad, low dune crest
        pyramid(cx, cz, 48, 31, 30, colour)
    else:  # hills/grass: a restrained cluster of low-poly shrubs or rocks
        pyramid(cx - 18, cz + 7, 25, 22, round(50 * scale[1]), colour)
        pyramid(cx + 24, cz - 13, 18, 16, round(38 * scale[1]), light)


def append_keep_part(vertices: list, uvs: list, triangles: list, part: dict,
                     centre_x: int, centre_z: int, texture: int,
                     cell_lifts: dict, width: int, height: int) -> None:
    points, faces, source_uvs = textured_part_mesh(part, 8)
    if part["shape"] == "box" and min(part["size"][0], part["size"][2]) > 4:
        # A single roof-sized triangle crosses many floor ordering buckets.
        # Split its top into tile-sized faces so the PS1 painter's ordering
        # cannot draw nearer courtyard tiles over the raised stone terrace.
        faces = [face for face in faces if face_normal(points, face)[1] < 0.5]
        first_top = len(points)
        cx, cy, cz = part["centre"]
        sx, sy, sz = part["size"]
        for z in range(6):
            for x in range(6):
                px, pz = cx + (x / 5 - 0.5) * sx, cz + (z / 5 - 0.5) * sz
                points.append((px, cy + sy / 2, pz))
                source_uvs.append((px * 10 + 64, pz * 10 + 64))
        for z in range(5):
            for x in range(5):
                i = first_top + z * 6 + x
                faces.extend(((i, i + 1, i + 7), (i, i + 7, i + 6)))
    faces = [face for face in faces
             if not (face_normal(points, face)[1] < -0.5 or
                     face_normal(points, face)[2] > 0.2 and face_normal(points, face)[1] < 0.4)]
    if not part["masonry"] and part["size"][0] < 0.02 and part["colour"][0] > part["colour"][1] * 2:
        faces = [face for face in faces if face_normal(points, face)[0] < -0.5]
    used = sorted({index for face in faces for index in face})
    first = len(vertices)
    indices = {old: first + new for new, old in enumerate(used)}
    bottom = min(point[1] for point in points)
    tile_x = round((centre_x / 64 + width - 1) / 2 + part["centre"][0])
    tile_y = round((height - 1 - centre_z / 64) / 2 + part["centre"][2])
    floor = surface_relief(tile_x, tile_y, cell_lifts) - 4
    for old in used:
        x, y, z = points[old]
        elevation = round(y * 128)
        if part["grounded"] and abs(y - bottom) < 0.0001:
            elevation = min(elevation, floor)
        vertices.append((centre_x + round(x * 128), elevation, centre_z - round(z * 128), 0))
        u, v = source_uvs[old]
        uvs.append((max(0, min(127, round(u))), max(0, min(127, round(v)))))
    for face in faces:
        normal = face_normal(points, face)
        triangles.append(tuple(indices[index] for index in face) +
                         (texture if part["masonry"] else 0, 255,
                          *lit_colour(part["colour"], normal)))


def compact_flat_mesh(vertices: list, uvs: list, triangles: list) -> tuple[list, list, list]:
    """Share identical exported vertices without changing faces or materials.

    Flat scenery has per-face baked colour and one unused up normal. Texture
    seams must stay separate; untextured faces need only the same position.
    Work on final integer coordinates, as those are what the console projects.
    """
    points, coordinates, faces = [], [], []
    remap = {}
    for face in triangles:
        mapped = []
        for index in face[:3]:
            uv = uvs[index] if face[3] else (0, 0)
            key = (vertices[index], uv)
            if key not in remap:
                remap[key] = len(points)
                points.append(vertices[index])
                coordinates.append(uv)
            mapped.append(remap[key])
        faces.append(tuple(mapped) + face[3:])
    return points, coordinates, faces


def append_settlement_model(board_vertices, board_uvs, board_triangles, kind, settlement_models, terrain_indices):
    cx = cz = floor = 0
    if kind.startswith("house_cutout_") or kind == "grove_cutout":
        _prefix,_cutout,drawing,mirror = kind.split("_") if kind != "grove_cutout" else ("grove", "cutout", "4", "0")
        # Same full camera-facing canvas as Godot Sprite3D, fixed board angle.
        angle = math.radians(40.0)
        style = scenery_style(Path(__file__).resolve().parents[4])["ancientGrove" if kind == "grove_cutout" else "houses"]
        half = style["portableCanvasSide"] * 64
        centre = half * math.cos(angle)
        for x,y in [(-half,-half),(half,-half),(half,half),(-half,half)]:
            board_vertices.append((round(x),round(centre + y*math.cos(angle)),round(-y*math.sin(angle)),0))
        board_uvs.extend([(95 if mirror == "1" else 0,95),(0 if mirror == "1" else 95,95),(0 if mirror == "1" else 95,0),(95 if mirror == "1" else 0,0)])
        texture = terrain_indices[-100 if kind == "grove_cutout" else -10-int(drawing)]
        board_triangles.extend([(0,1,2,texture,255,255,255,255),(0,2,3,texture,255,255,255,255)])
        return
    for part in settlement_models[kind]:
        source_indices = part["indices"]
        remap = {}
        for offset in range(0, len(source_indices), 3):
            face = source_indices[offset:offset + 3]
            normal = face_normal(part["vertices"], face)
            # The board uses a fixed camera direction. Drop buried and
            # rear faces exactly as the shared keep path already does.
            if normal[1] < -0.5 or (normal[2] > 0.2 and normal[1] < 0.4):
                continue
            mapped = []
            for old in face:
                point, uv = part["vertices"][old], part["uvs"][old]
                key = (tuple(point), tuple(uv) if part["masonry"] else ())
                if key not in remap:
                    px, py, pz = point
                    remap[key] = len(board_vertices)
                    board_vertices.append((cx + round(px * 128), floor + round(py * 128), cz - round(pz * 128), 0))
                    board_uvs.append((max(0, min(31, round(uv[0] * 31))), max(0, min(31, round(uv[1] * 31)))))
                mapped.append(remap[key])
            board_triangles.append(tuple(mapped) +
                (terrain_indices[-3] if part["masonry"] else 0, 255,
                 *lit_colour(part["colour"], normal)))


def scenery_position(repo: Path, x: int, y: int, kind: str, size: int, width: int, height: int) -> tuple[int, int]:
    offset = house_at(repo, x, y)["offset"] if kind.startswith("house_cutout_") else (0, 0)
    return ((x * 2 + size - width) * 64 + round(offset[0] * 128),
            (height - y * 2 - size) * 64 - round(offset[1] * 128))


def scenery_cache_peak(instances: list[tuple], groups: dict, width: int, height: int) -> tuple[int, int]:
    # Match tick_camera's actual centre-based culling, including integer
    # truncation during smooth camera movement. Exhaust all camera centres;
    # invisible scenery cannot consume the transformed-geometry cache.
    repo = Path(__file__).resolve().parents[4]
    costs = []
    for x, y, kind, size in instances:
        px, pz = scenery_position(repo, x, y, kind, size, width, height)
        costs.append((int(px / 64), int(pz / 64), len(groups[kind][0]), len(groups[kind][2])))
    half_x, half_z = CAMERA_VIEW_TILES_X + 2, CAMERA_VIEW_TILES_Y + 2
    peak_v = peak_t = 0
    for focus_x in range(-width - half_x, width + half_x + 1):
        for focus_z in range(-height - half_z, height + half_z + 1):
            visible = [c for c in costs if abs(c[0] - focus_x) <= half_x and abs(c[1] - focus_z) <= half_z]
            peak_v = max(peak_v, sum(c[2] for c in visible))
            peak_t = max(peak_t, sum(c[3] for c in visible))
    return peak_v, peak_t


def write_meshes(
    output: Path,
    cell_groups: list[tuple[tuple[int, int], list[tuple[int, int, int, bool, int, int]]]],
    width: int, height: int,
    terrain_form_cells: list[tuple[int, int, int]],
    terrain_indices: dict[int, int], seed: int,
    relief_mode: str, cell_lifts: dict[tuple[int, int], int],
    hidden_cells: list[int],
    castle_cells: list[tuple[int, int]],
    keep_parts: list[dict] | None = None,
    settlement_instances: list[tuple] | None = None,
    settlement_models: dict | None = None,
    mesh_vertex_limit: int = MAX_CACHE_VERTICES,
    scenery_metadata: list | None = None,
    surface_count: int = 0,
    foliage_metadata: list | None = None,
) -> tuple[int, int, list[tuple[int, int, int, int, int, int, int]]]:
    keep_parts = keep_parts or []
    settlement_instances = settlement_instances or []
    settlement_models = settlement_models or {}
    occupied = {(x + dx, y + dy) for x, y, _kind, size in settlement_instances for dy in range(size) for dx in range(size)}
    keep_centre_x = keep_centre_z = 0
    if keep_parts and castle_cells:
        keep_centre_x = (min(x for x, _ in castle_cells) + max(x for x, _ in castle_cells) + 1 - width) * 64
        keep_centre_z = (height - min(y for _, y in castle_cells) - max(y for _, y in castle_cells) - 1) * 64
    board_groups: list[tuple[
        list[tuple[int, int, int, int]], list[tuple[int, int]],
        list[tuple[int, int, int, int, int, int, int, int]],
        tuple[int, int, int, int],
    ]] = []
    foliage_groups = []
    page_origins: list[tuple[int, int]] = []
    for group_index, (key, group_cells) in enumerate(cell_groups):
        # The complete floor is drawn once. Near scenery remains paged.
        floor_cells = group_cells if not surface_count or group_index < surface_count else []
        board_vertices: list[tuple[int, int, int, int]] = []
        board_uvs: list[tuple[int, int]] = []
        board_triangles: list[tuple[int, int, int, int, int, int, int, int]] = []
        min_x = min(cell[0] for cell in group_cells)
        min_y = min(cell[1] for cell in group_cells)
        max_x = max(cell[0] + cell[4] for cell in group_cells)
        max_y = max(cell[1] + cell[5] for cell in group_cells)
        if relief_mode == "smooth":
            # Runs align to four-cell anchors. A shared grid can therefore use
            # a monotonic 0..127 texture ramp inside one anchor and reverse it
            # in the next without collapsing a patch's UVs at the apex.
            u_wave = (0, 31, 63, 95, 127, 95, 63, 31)
            relief_min_x = min(cell[0] for cell in cell_lifts)
            grid: dict[tuple[int, int], int] = {}
            corners = {
                point for x, y, _kind, _authored, span_x, span_y in floor_cells
                for point in ((x, y), (x + span_x, y),
                              (x, y + span_y), (x + span_x, y + span_y))
            }
            for grid_y, grid_x in sorted((y, x) for x, y in corners):
                grid[(grid_x, grid_y)] = len(board_vertices)
                board_vertices.append((
                    (grid_x * 2 - width) * 64,
                    corner_relief(grid_x, grid_y, cell_lifts),
                    (height - grid_y * 2) * 64,
                    0,
                ))
                board_uvs.append((
                    u_wave[(grid_x - relief_min_x) % len(u_wave)],
                    31 if grid_y & 1 else 0,
                ))
            for x, y, kind, authored, span_x, span_y in floor_cells:
                top_left = grid[(x, y)]
                top_right = grid[(x + span_x, y)]
                bottom_left = grid[(x, y + span_y)]
                bottom_right = grid[(x + span_x, y + span_y)]
                heights = (
                    board_vertices[top_left][1], board_vertices[top_right][1],
                    board_vertices[bottom_left][1],
                    board_vertices[bottom_right][1],
                )
                relief_shade = min(24, (max(heights) - min(heights)) * 2)
                shade = max(0, (255 if authored else 210) - relief_shade)
                texture = terrain_indices[kind]
                board_triangles.extend((
                    (top_left, top_right, bottom_left, texture, shade,
                     255, 255, 255),
                    (bottom_left, top_right, bottom_right, texture,
                     shade, 255, 255, 255),
                ))
        else:
            for x, y, kind, authored, span_x, span_y in floor_cells:
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
                    (first, first + 1, first + 2, texture, shade,
                     255, 255, 255),
                    (first + 2, first + 1, first + 3, texture, shade,
                     255, 255, 255),
                ))
        foliage_triangles = []
        for x, y, kind in ([] if surface_count and group_index < surface_count else terrain_form_cells):
            if (x, y) in castle_cells or (x, y) in occupied:
                continue
            if not (min_x <= x < max_x and min_y <= y < max_y):
                continue
            # The outer floor guard closes the perspective frustum. Keep
            # expensive raised scenery to the nearer guard ring; distant floor
            # remains continuous and the next page supplies its nearer props.
            if not (key[0] - 1 <= x < key[0] + CAMERA_VIEW_TILES_X + 1 and
                    key[1] - 1 <= y < key[1] + CAMERA_VIEW_TILES_Y + 1):
                continue
            roll = hash_cell(x, y, seed)
            cx = (x * 2 + 1 - width) * 64 + (26 if roll & 1 else -26)
            cz = (height - y * 2 - 1) * 64 + (22 if roll & 2 else -22)
            floor = surface_relief(x, y, cell_lifts) if relief_mode == "smooth" else 0
            first_face = len(board_triangles)
            append_terrain_form(
                board_vertices, board_uvs, board_triangles,
                kind, cx, floor, cz, roll,
                terrain_indices,
            )
            if kind == 2:
                # Keep trunks planted. Only the leaf material joins the small
                # moving batch; mesh data itself stays immutable at runtime.
                faces = board_triangles[first_face:]
                foliage_triangles.extend(face for face in faces if face[3] == terrain_indices[-2])
                board_triangles[first_face:] = [face for face in faces if face[3] != terrain_indices[-2]]
        for part in ([] if surface_count and group_index < surface_count else keep_parts):
            if part["upper"]:
                continue
            tile_x = (keep_centre_x / 64 + width - 1) / 2 + part["centre"][0]
            tile_y = (height - 1 - keep_centre_z / 64) / 2 + part["centre"][2]
            if min_x - 0.5 <= tile_x < max_x + 0.5 and min_y - 0.5 <= tile_y < max_y + 0.5:
                append_keep_part(board_vertices, board_uvs, board_triangles, part,
                                 keep_centre_x, keep_centre_z, terrain_indices[-4], cell_lifts, width, height)
        # Keep native triangle pairs together for the cached quad fast path,
        # and group their material descriptors to reduce texture switches.
        batches = [board_triangles[i:i + 2]
                   for i in range(0, len(board_triangles), 2)]
        batches.sort(key=lambda pair: pair[0][3])
        board_triangles = [triangle for pair in batches for triangle in pair]
        if not board_vertices:
            continue
        foliage_groups.append(compact_flat_mesh(
            board_vertices, board_uvs, foliage_triangles))
        board_vertices, board_uvs, board_triangles = compact_flat_mesh(
            board_vertices, board_uvs, board_triangles)
        page_origins.append(key)
        board_groups.append((
            board_vertices, board_uvs, board_triangles,
            (min_x, min_y, max_x, max_y),
        ))

    billboard_vertices = [(-512, 0, 0, 0), (512, 0, 0, 0), (512, 1024, 0, 0), (-512, 1024, 0, 0)]
    billboard_uvs = [(0, 63), (63, 63), (63, 0), (0, 0)]

    # A tile is 128 scene units across. The cursor mesh uses a 1/16 scale, so
    # +/-960 covers 120 of them and leaves only a four-unit breathing edge.
    # Four narrow quads make a border instead of the old solid 56-unit square:
    # the player can now see which whole tile is selected without losing the
    # terrain, unit feet, or team-colour information inside it.
    cursor_vertices = [
        (-960, 28, -960, 0), (960, 28, -960, 0),
        (960, 28, -800, 0), (-960, 28, -800, 0),
        (800, 28, -800, 0), (960, 28, -800, 0),
        (960, 28, 800, 0), (800, 28, 800, 0),
        (-960, 28, 800, 0), (960, 28, 800, 0),
        (960, 28, 960, 0), (-960, 28, 960, 0),
        (-960, 28, -800, 0), (-800, 28, -800, 0),
        (-800, 28, 800, 0), (-960, 28, 800, 0),
    ]
    cursor_triangles = tuple(
        triangle
        for first in range(0, len(cursor_vertices), 4)
        for triangle in (
            (first, first + 2, first + 1),
            (first, first + 3, first + 2),
        )
    )
    visibility_vertices: list[tuple[int, int, int, int]] = []
    visibility_uvs: list[tuple[int, int]] = []
    visibility_triangles: list[tuple] = []
    for part in keep_parts:
        if part["upper"]:
            append_keep_part(visibility_vertices, visibility_uvs, visibility_triangles, part,
                             keep_centre_x, keep_centre_z, terrain_indices[-4], cell_lifts, width, height)
    for order, cell in enumerate([] if keep_parts else hidden_cells):
        x = cell % width
        y = cell // width
        left = (x * 2 - width) * 64
        right = left + 128
        top = (height - y * 2) * 64
        bottom = top - 128
        roof = surface_relief(x, y, cell_lifts) + 43
        first = len(visibility_vertices)
        visibility_vertices.extend((
            (left, roof, top, 0), (right, roof, top, 0),
            (left, roof, bottom, 0), (right, roof, bottom, 0),
        ))
        visibility_uvs.extend(((0, 0), (0, 0), (0, 0), (0, 0)))
        colour = (109, 59, 49) if order % 2 == 0 else (91, 48, 43)
        visibility_triangles.extend((
            (first, first + 1, first + 2, 0, 255, *colour),
            (first + 2, first + 1, first + 3, 0, 255, *colour),
        ))
    visibility_vertices, visibility_uvs, visibility_triangles = compact_flat_mesh(
        visibility_vertices, visibility_uvs, visibility_triangles)
    # The close view used to expose the edge of one small quad, making its
    # actors look as though they stood on a card in empty space. A modest grid
    # reaches past the camera frustum, repeats a readable material instead of
    # stretching it, and still costs much less geometry than one board page.
    combat_floor_vertices: list[tuple[int, int, int, int]] = []
    combat_floor_uvs: list[tuple[int, int]] = []
    combat_floor_triangles: list[tuple[int, int, int, int]] = []
    combat_floor_columns = 4
    combat_floor_rows = 6
    combat_floor_cell = 192
    combat_floor_left = -(combat_floor_columns * combat_floor_cell) // 2
    combat_floor_front = (combat_floor_rows * combat_floor_cell) // 3
    for row in range(combat_floor_rows):
        front = combat_floor_front - row * combat_floor_cell
        back = front - combat_floor_cell
        for column in range(combat_floor_columns):
            left = combat_floor_left + column * combat_floor_cell
            right = left + combat_floor_cell
            first = len(combat_floor_vertices)
            combat_floor_vertices.extend((
                (left, 0, front, 0), (right, 0, front, 0),
                (left, 0, back, 0), (right, 0, back, 0),
            ))
            combat_floor_uvs.extend(((0, 31), (31, 31), (0, 0), (31, 0)))
            shade = 255 if (row + column) % 2 == 0 else 232
            combat_floor_triangles.extend((
                (first, first + 1, first + 2, shade),
                (first + 2, first + 1, first + 3, shade),
            ))
    combat_floor_counts: dict[int, int] = {}
    for _, group_cells in cell_groups:
        for _, _, terrain_id, authored, span_x, span_y in group_cells:
            if authored:
                combat_floor_counts[terrain_id] = (
                    combat_floor_counts.get(terrain_id, 0) + span_x * span_y
                )
    combat_floor_id = min(
        combat_floor_counts or terrain_indices,
        key=lambda terrain_id: (
            -combat_floor_counts.get(terrain_id, 0), terrain_id
        ),
    )
    combat_floor_texture = terrain_indices[combat_floor_id]

    # Only one terrain page is enabled by the camera at a time. All pages stay
    # in executable data, but the renderer's transformed-geometry cache must
    # hold the largest active page, not the sum of mutually exclusive pages.
    active_groups = [(len(group[0]) + len(foliage[0]), len(group[2]) + len(foliage[2]))
                     for group, foliage in zip(board_groups, foliage_groups)]
    if surface_count:
        vertex_count = sum(g[0] for g in active_groups[:surface_count]) + max((g[0] for g in active_groups[surface_count:]), default=0)
        triangle_count = sum(g[1] for g in active_groups[:surface_count]) + max((g[1] for g in active_groups[surface_count:]), default=0)
    else:
        vertex_count = max(group[0] for group in active_groups)
        triangle_count = max(group[1] for group in active_groups)
    scenery_groups = {}
    for _x, _y, kind, _size in settlement_instances:
        if kind in scenery_groups:
            continue
        verts, uvs, tris = [], [], []
        append_settlement_model(verts, uvs, tris, kind, settlement_models, terrain_indices)
        verts, uvs, tris = compact_flat_mesh(verts, uvs, tris)
        scenery_groups[kind] = (verts, uvs, tris)
    scenery_vertices, scenery_triangles = scenery_cache_peak(
        settlement_instances, scenery_groups, width, height,
    )
    if any(len(group[0]) > mesh_vertex_limit for group in scenery_groups.values()):
        raise ValueError("a settlement model exceeds its per-mesh cache")
    lines = ["/* Generated from a real Grandleon scene manifest. Do not edit. */", '#include "../runtime/scene.h"', ""]
    lines += emit_vectors("k_billboard_vertices", billboard_vertices)
    lines += emit_vectors("k_billboard_normals", [(0, 0, 4096, 0)] * 4)
    lines.extend(("static const ps1_uv8 k_billboard_uvs[] = {", "    { 0, 63 }, { 63, 63 }, { 63, 0 }, { 0, 0 },", "};", "", "static const ps1_mesh_tri k_billboard_triangles[] = {", "    { .i0=0, .i1=1, .i2=2, .nz=4096, .shade=255, .color_r=255, .color_g=255, .color_b=255 },", "    { .i0=0, .i1=2, .i2=3, .nz=4096, .shade=255, .color_r=255, .color_g=255, .color_b=255 },", "};", ""))
    lines.extend((
        "static const ps1_uv8 k_billboard_mirrored_uvs[] = {",
        "    { 63, 63 }, { 0, 63 }, { 0, 0 }, { 63, 0 },",
        "};", "",
        "static const ps1_uv8 k_directional_billboard_uvs[] = {",
        "    { 0, 63 }, { 63, 63 }, { 63, 0 }, { 0, 0 },",
        "};", "",
        "static const ps1_uv8 k_directional_billboard_mirrored_uvs[] = {",
        "    { 63, 63 }, { 0, 63 }, { 0, 0 }, { 63, 0 },",
        "};", "",
    ))
    chunk_descriptors: list[tuple[int, int, int, int, int, int, int]] = []
    chunk_mesh_lines: list[str] = []
    for chunk_index, (vertices, uvs, triangles, bounds) in enumerate(board_groups):
        name = f"k_board_{chunk_index}"
        lines += emit_vectors(f"{name}_vertices", vertices)
        lines += emit_vectors(f"{name}_normals", [(0, 4096, 0, 0)] * len(vertices))
        lines.append(f"static const ps1_uv8 {name}_uvs[] = {{")
        lines.extend(f"    {{ {u}, {v} }}," for u, v in uvs)
        lines.extend(("};", "", f"static const ps1_mesh_tri {name}_triangles[] = {{"))
        for i0, i1, i2, texture, shade, red, green, blue in triangles:
            lines.append(f"    {{ .i0={i0}, .i1={i1}, .i2={i2}, .ny=4096, .shade={shade}, .color_r={red}, .color_g={green}, .color_b={blue}, .texture_index={texture} }},")
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
        chunk_descriptors.append((chunk_index + 2, *bounds, focus_x, focus_z))
    scenery_mesh_lines = []
    scenery_indices = {}
    scenery_mesh_start = len(board_groups) + 7 + (1 if hidden_cells else 0)
    for model_index, (kind, (verts, uvs, tris)) in enumerate(scenery_groups.items()):
        scenery_indices[kind] = scenery_mesh_start + model_index
        name = "k_scenery_" + kind
        lines += emit_vectors(name + "_vertices", verts)
        lines += emit_vectors(name + "_normals", [(0,4096,0,0)] * len(verts))
        lines.append("static const ps1_uv8 " + name + "_uvs[] = {")
        lines += [f"    {{{u},{v}}}," for u,v in uvs]
        lines += ["};", "static const ps1_mesh_tri " + name + "_triangles[] = {"]
        for i0,i1,i2,texture,shade,r,g,b in tris:
            lines.append(f"    {{.i0={i0},.i1={i1},.i2={i2},.ny=4096,.shade={shade},.color_r={r},.color_g={g},.color_b={b},.texture_index={texture}}},")
        lines.append("};")
        centre_y = (min(v[1] for v in verts)+max(v[1] for v in verts))//2
        radius = math.ceil(max(math.sqrt(v[0]**2+(v[1]-centre_y)**2+v[2]**2) for v in verts))
        scenery_mesh_lines.append(f"    {{.verts={name}_vertices,.norms={name}_normals,.vert_count={len(verts)},.uvs={name}_uvs,.tris={name}_triangles,.tri_count={len(tris)},.scale_q12=4096,.bounds_center_y={centre_y},.bounds_radius={radius},.flags=PS1_MESH_FLAG_TERRAIN}},")
    if scenery_metadata is not None:
        for x,y,kind,size in settlement_instances:
            floor = max(surface_relief(x+dx,y+dy,cell_lifts) for dy in range(size) for dx in range(size)) if relief_mode == "smooth" else 0
            px, pz = scenery_position(Path(__file__).resolve().parents[4], x, y, kind, size, width, height)
            scenery_metadata.append((scenery_indices[kind], px, floor, pz, kind.startswith("house_cutout_") or kind == "grove_cutout"))
    foliage_mesh_lines = []
    foliage_mesh_start = scenery_mesh_start + len(scenery_groups)
    for page, (verts, uvs, tris) in enumerate(foliage_groups):
        if not tris:
            if foliage_metadata is not None:
                foliage_metadata.append(0)
            continue
        if len(verts) > mesh_vertex_limit:
            raise ValueError("a foliage page exceeds its per-mesh cache")
        if foliage_metadata is not None:
            foliage_metadata.append(foliage_mesh_start + len(foliage_mesh_lines))
        name = f"k_foliage_{page}"
        lines += emit_vectors(name + "_vertices", verts)
        lines += emit_vectors(name + "_normals", [(0,4096,0,0)] * len(verts))
        lines.append(f"static const ps1_uv8 {name}_uvs[] = {{")
        lines += [f"    {{{u},{v}}}," for u, v in uvs]
        lines += ["};", f"static const ps1_mesh_tri {name}_triangles[] = {{"]
        for i0, i1, i2, texture, shade, r, g, b in tris:
            lines.append(f"    {{.i0={i0},.i1={i1},.i2={i2},.ny=4096,.shade={shade},.color_r={r},.color_g={g},.color_b={b},.texture_index={texture}}},")
        lines.append("};")
        cx = (min(v[0] for v in verts) + max(v[0] for v in verts)) // 2
        cz = (min(v[2] for v in verts) + max(v[2] for v in verts)) // 2
        radius = math.ceil(max(math.hypot(v[0] - cx, v[2] - cz) for v in verts))
        foliage_mesh_lines.append(f"    {{.verts={name}_vertices,.norms={name}_normals,.vert_count={len(verts)},.uvs={name}_uvs,.tris={name}_triangles,.tri_count={len(tris)},.scale_q12=4096,.bounds_center_x={cx},.bounds_center_z={cz},.bounds_radius={radius},.flags=PS1_MESH_FLAG_TERRAIN}},")
    lines += emit_vectors("k_combat_floor_vertices", combat_floor_vertices)
    lines += emit_vectors(
        "k_combat_floor_normals",
        [(0, 4096, 0, 0)] * len(combat_floor_vertices),
    )
    lines.append("static const ps1_uv8 k_combat_floor_uvs[] = {")
    lines.extend(f"    {{ {u}, {v} }}," for u, v in combat_floor_uvs)
    lines.extend(("};", "", "static const ps1_mesh_tri k_combat_floor_triangles[] = {"))
    for i0, i1, i2, shade in combat_floor_triangles:
        lines.append(
            f"    {{ .i0={i0}, .i1={i1}, .i2={i2}, .ny=4096, "
            f".shade={shade}, .color_r=255, .color_g=255, .color_b=255, "
            f".texture_index={combat_floor_texture} }},"
        )
    lines.extend(("};", ""))
    lines += emit_vectors("k_cursor_vertices", cursor_vertices)
    lines += emit_vectors(
        "k_cursor_normals", [(0, 4096, 0, 0)] * len(cursor_vertices)
    )
    if visibility_vertices:
        lines += emit_vectors("k_visibility_vertices", visibility_vertices)
        lines += emit_vectors(
            "k_visibility_normals",
            [(0, 4096, 0, 0)] * len(visibility_vertices),
        )
        lines.append("static const ps1_uv8 k_visibility_uvs[] = {")
        lines.extend(f"    {{ {u}, {v} }}," for u, v in visibility_uvs)
        lines.extend((
            "};", "", "static const ps1_mesh_tri k_visibility_triangles[] = {",
        ))
        for i0, i1, i2, texture, shade, red, green, blue in visibility_triangles:
            lines.append(
                f"    {{ .i0={i0}, .i1={i1}, .i2={i2}, .ny=4096, "
                f".shade={shade}, .color_r={red}, .color_g={green}, .color_b={blue}, .texture_index={texture} }},"
            )
        lines.extend(("};", ""))
        centre_x = (
            min(vertex[0] for vertex in visibility_vertices)
            + max(vertex[0] for vertex in visibility_vertices)
        ) // 2
        centre_y = (
            min(vertex[1] for vertex in visibility_vertices)
            + max(vertex[1] for vertex in visibility_vertices)
        ) // 2
        centre_z = (
            min(vertex[2] for vertex in visibility_vertices)
            + max(vertex[2] for vertex in visibility_vertices)
        ) // 2
        radius = math.ceil(max(
            math.sqrt(
                (vertex[0] - centre_x) ** 2
                + (vertex[1] - centre_y) ** 2
                + (vertex[2] - centre_z) ** 2
            )
            for vertex in visibility_vertices
        ))
        visibility_mesh_line = (
            "    { .verts=k_visibility_vertices, .norms=k_visibility_normals, "
            ".vert_count=sizeof(k_visibility_vertices)/sizeof(k_visibility_vertices[0]), "
            ".uvs=k_visibility_uvs, .tris=k_visibility_triangles, "
            ".tri_count=sizeof(k_visibility_triangles)/sizeof(k_visibility_triangles[0]), "
            f".scale_q12=4096, .bounds_center_x={centre_x}, "
            f".bounds_center_y={centre_y}, .bounds_center_z={centre_z}, "
            f".bounds_radius={radius}, .flags=PS1_MESH_FLAG_TERRAIN }},"
        )
    else:
        visibility_mesh_line = None
    lines.append("static const ps1_uv8 k_cursor_uvs[] = {")
    lines.extend("    {0,0}," for _ in cursor_vertices)
    lines.extend(("};", "", "static const ps1_mesh_tri k_cursor_triangles[] = {"))
    lines.extend(
        f"    {{ .i0={i0}, .i1={i1}, .i2={i2}, .ny=4096, .shade=255, .color_r=255, .color_g=255, .color_b=255 }},"
        for i0, i1, i2 in cursor_triangles
    )
    lines.extend((
        "};", "", "const ps1_mesh g_ps1_meshes[] = {",
        "    { .verts=k_billboard_vertices, .norms=k_billboard_normals, .vert_count=4, .uvs=k_billboard_uvs, .tris=k_billboard_triangles, .tri_count=2, .scale_q12=256, .bounds_center_y=512, .bounds_radius=724 },",
        *chunk_mesh_lines,
        "    { .verts=k_combat_floor_vertices, .norms=k_combat_floor_normals, .vert_count=sizeof(k_combat_floor_vertices)/sizeof(k_combat_floor_vertices[0]), .uvs=k_combat_floor_uvs, .tris=k_combat_floor_triangles, .tri_count=sizeof(k_combat_floor_triangles)/sizeof(k_combat_floor_triangles[0]), .scale_q12=4096, .bounds_radius=860, .flags=PS1_MESH_FLAG_TERRAIN },",
        "    { .verts=k_cursor_vertices, .norms=k_cursor_normals, .vert_count=sizeof(k_cursor_vertices)/sizeof(k_cursor_vertices[0]), .uvs=k_cursor_uvs, .tris=k_cursor_triangles, .tri_count=sizeof(k_cursor_triangles)/sizeof(k_cursor_triangles[0]), .scale_q12=256, .bounds_radius=1360, .flags=PS1_MESH_FLAG_TERRAIN },",
        *([visibility_mesh_line] if visibility_mesh_line else []),
        "    { .verts=k_billboard_vertices, .norms=k_billboard_normals, .vert_count=4, .uvs=k_billboard_mirrored_uvs, .tris=k_billboard_triangles, .tri_count=2, .scale_q12=256, .bounds_center_y=512, .bounds_radius=724 },",
        "    { .verts=k_billboard_vertices, .norms=k_billboard_normals, .vert_count=4, .uvs=k_directional_billboard_uvs, .tris=k_billboard_triangles, .tri_count=2, .scale_q12=256, .bounds_center_y=512, .bounds_radius=724 },",
        "    { .verts=k_billboard_vertices, .norms=k_billboard_normals, .vert_count=4, .uvs=k_directional_billboard_mirrored_uvs, .tris=k_billboard_triangles, .tri_count=2, .scale_q12=256, .bounds_center_y=512, .bounds_radius=724 },",
        *scenery_mesh_lines,
        *foliage_mesh_lines,
        "};", "const unsigned int g_ps1_mesh_count =", "    sizeof(g_ps1_meshes) / sizeof(g_ps1_meshes[0]);", ""))
    (output / "mesh_data.c").write_text("\n".join(lines), encoding="utf-8")
    if max(max(len(group[0]) for group in board_groups), len(visibility_vertices)) > mesh_vertex_limit:
        raise ValueError(f"terrain page needs {vertex_count} vertices; per-mesh limit is {mesh_vertex_limit}")
    return (
        max(vertex_count, len(combat_floor_vertices)) + scenery_vertices + 2 * len(cursor_vertices) + (len(visibility_vertices) - len(hidden_cells) * 4 if keep_parts else 0),
        max(triangle_count, len(combat_floor_triangles)) + scenery_triangles + 2 * len(cursor_triangles) + (len(visibility_triangles) - len(hidden_cells) * 2 if keep_parts else 0),
        chunk_descriptors,
    )


def fixed(value: float) -> int:
    return round(value * 65536)


def select_terrain_form_cells(
    terrain: list[int], width: int, height: int, seed: int,
) -> list[tuple[int, int, int]]:
    """Choose sparse geometry only where relief/material cannot carry identity."""
    structured_kinds = {0, 2, 3, 4, 5, 6, 7, 8, 10, 11}
    candidates = [
        (x, y, terrain[y * width + x])
        for y in range(height) for x in range(width)
        if terrain[y * width + x] in structured_kinds
    ]
    if not candidates:
        return []
    present_kinds = {cell[2] for cell in candidates}
    # A handful of props over an entire large map still reads as a flat tile
    # sheet.  The active PS1 page has ample transformed-geometry headroom, so
    # give roughly one structural form to every three cells while keeping
    # a firm global ceiling. This is dense enough to define regions but keeps
    # worst-case terrain-page changes inside the two-retrace 30 fps budget.
    # Open grass, road and paved cells remain uncluttered.
    budget = max(len(present_kinds), max(1, (width * height + 2) // 3))
    budget = min(96, budget, len(candidates))
    selected: list[tuple[int, int, int]] = []
    selected_coordinates: set[tuple[int, int]] = set()
    for kind in sorted(present_kinds):
        member = min(
            (cell for cell in candidates if cell[2] == kind),
            key=lambda cell: hash_cell(cell[0], cell[1], seed),
        )
        selected.append(member)
        selected_coordinates.add((member[0], member[1]))
    ordered = sorted(
        candidates, key=lambda item: hash_cell(item[0], item[1], seed)
    )
    # Prefer coverage before density. This produces readable forest/mountain
    # regions without erecting a prop on every tactical square.
    for allow_adjacent in (False, True):
        for cell in ordered:
            if len(selected) >= budget:
                break
            coordinate = (cell[0], cell[1])
            if coordinate in selected_coordinates:
                continue
            if not allow_adjacent and any(
                abs(cell[0] - other_x) <= 1 and abs(cell[1] - other_y) <= 1
                for other_x, other_y in selected_coordinates
            ):
                continue
            selected.append(cell)
            selected_coordinates.add(coordinate)
    return selected


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


def visible_position_peak(
    positions: list[tuple[int, int]], width: int, height: int,
) -> int:
    peak = 0
    for cursor_y in range(height):
        focus_z = -camera_focus(cursor_y, height, CAMERA_VIEW_TILES_Y)
        for cursor_x in range(width):
            focus_x = camera_focus(cursor_x, width, CAMERA_VIEW_TILES_X)
            visible = sum(
                focus_x - CAMERA_VIEW_TILES_X <= x * 2 + 1 - width
                <= focus_x + CAMERA_VIEW_TILES_X
                and focus_z - CAMERA_VIEW_TILES_Y
                <= height - y * 2 - 1
                <= focus_z + CAMERA_VIEW_TILES_Y
                for x, y in positions
            )
            peak = max(peak, visible)
    return peak


def write_scene(
    output: Path, document: dict[str, object],
    unit_indices: dict[tuple[int, int, int, int], UnitTextureIndices],
    terrain_chunks: list[tuple[int, int, int, int, int, int, int]],
    ui_texture_index: int,
    surface_lifts: list[int], fog_colour: tuple[int, int, int],
    speaker_indices: dict[int, int] | None = None,
    scenery_metadata: list | None = None,
    surface_count: int = 0,
    foliage_metadata: list | None = None,
    water_texture_index: int = 0,
) -> int:
    foliage_metadata = foliage_metadata or [0] * len(terrain_chunks)
    if len(foliage_metadata) != len(terrain_chunks):
        raise ValueError("foliage bindings must match terrain pages")
    scenery_metadata = scenery_metadata or []
    width = int(document["width"])
    height = int(document["height"])
    units = document["units"]
    assert isinstance(units, list)
    visibility = document["visual_visibility"]
    assert isinstance(visibility, dict)
    hidden_cells = [int(value) for value in visibility["hidden_cells"]]
    mirrored_billboard_mesh_index = (
        len(terrain_chunks) + 4 + (1 if hidden_cells else 0)
    )
    directional_billboard_mesh_index = mirrored_billboard_mesh_index + 1
    directional_mirrored_billboard_mesh_index = mirrored_billboard_mesh_index + 2
    if len(units) > 64:
        raise ValueError("Mipsync playable slice supports at most 64 units")
    lines = ["/* Generated from a real Grandleon scene manifest. Do not edit. */", '#include "grandleon_scene.h"', "", "static ps1_entity k_ps1_entities[] = {"]
    next_id = 1
    entity_index = 0
    terrain_entity_indices: list[int] = []
    tree_entity_indices: list[int] = []
    for chunk_index, (mesh_index, _min_x, _min_y, _max_x, _max_y, _focus_x, _focus_z) in enumerate(terrain_chunks):
        if not surface_count or chunk_index >= surface_count:
            terrain_entity_indices.append(entity_index)
        lines.append(
            f"    {{ .id={next_id}u, .name=\"terrain_apron_chunk\", .parent_entity_index=-1, .scale={{65536,65536,65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index={mesh_index}, .mesh_size=65536, .mesh_enabled={1 if chunk_index < max(1, surface_count) else 0}, .color_r=255, .color_g=255, .color_b=255, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_FLOOR, .seam_fill=1, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
        )
        next_id += 1
        entity_index += 1
    for mesh_index,x,y,z,is_house in scenery_metadata:
        tree_entity_indices.append(entity_index)
        preset = "PS1_RENDER_PRESET_CHARACTER" if is_house else "PS1_RENDER_PRESET_FLOOR"
        lines.append(f"    {{.id={next_id}u,.name=\"settlement_landmark\",.parent_entity_index=-1,.position={{{fixed(x/64)},{fixed(y/64)},{fixed(z/64)}}},.scale={{65536,65536,65536}},.mesh=PS1_MESH_CUSTOM,.mesh_index={mesh_index},.mesh_size=65536,.mesh_enabled=0,.color_r=255,.color_g=255,.color_b=255,.texture_tiling_u_q8=256,.texture_tiling_v_q8=256,.render_unlit=1,.render_preset={preset},.rigid_root_entity_index=-1,.collider_shape=-1,.camera_shot_trigger_index=-1,.collider_camera_target_index=-1}},")
        next_id += 1
        entity_index += 1
    combat_floor_index = entity_index
    combat_floor_mesh_index = 2 + len(terrain_chunks)
    lines.append(
        f"    {{ .id={next_id}u, .name=\"close_combat_floor\", .parent_entity_index=-1, .scale={{65536,65536,65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index={combat_floor_mesh_index}, .mesh_size=65536, .mesh_enabled=0, .color_r=255, .color_g=255, .color_b=255, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_FLOOR, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
    )
    next_id += 1
    entity_index += 1
    unit_entity_indices: list[int] = []
    unit_pose_texture_indices: list[UnitTextureIndices] = []
    for unit in units:
        if not isinstance(unit, dict):
            raise ValueError("every unit must be an object")
        x = require_int(unit.get("x"), "unit x", 0, width - 1)
        y = require_int(unit.get("y"), "unit y", 0, height - 1)
        key = (int(unit["style"]), int(unit["archetype"]), int(unit["figure"]), int(unit["colour"]))
        pose_textures = unit_indices[key]
        texture = pose_textures[0]
        unit_pose_texture_indices.append(pose_textures)
        unit_entity_indices.append(entity_index)
        world_x = (x + 0.5 - width / 2) * 2
        world_z = (height / 2 - y - 0.5) * 2
        world_y = fixed((surface_lifts[y * width + x] + 3) / 64)
        lines.append(
            f"    {{ .id={next_id}u, .name=\"unit\", .parent_entity_index=-1, .position={{{fixed(world_x)},{world_y},{fixed(world_z)}}}, .scale={{{fixed(1.35)},{fixed(1.9)},65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index=1, .mesh_size=65536, .mesh_enabled=1, .color_r=255, .color_g=255, .color_b=255, .texture_index={texture}, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_CHARACTER, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
        )
        next_id += 1
        entity_index += 1
    cursor_mesh_index = 3 + len(terrain_chunks)
    visibility_entity_index = 0
    if hidden_cells:
        visibility_entity_index = entity_index
        lines.append(
            f"    {{ .id={next_id}u, .name=\"visual_visibility_roof\", .parent_entity_index=-1, .scale={{65536,65536,65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index={cursor_mesh_index + 1}, .mesh_size=65536, .mesh_enabled=1, .color_r=255, .color_g=255, .color_b=255, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_FLOOR, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
        )
        next_id += 1
        entity_index += 1
    simulation = document["simulation"]
    assert isinstance(simulation, dict)
    deployment_tiles = simulation.get("deployment_tiles", [])
    if not isinstance(deployment_tiles, list):
        raise ValueError("simulation deployment tiles are not an array")
    deployment_entity_indices: list[int] = []
    for tile in deployment_tiles:
        if not isinstance(tile, list) or len(tile) != 2:
            raise ValueError("deployment tile is not an x/y pair")
        deploy_x = require_int(tile[0], "deployment x", 0, width - 1)
        deploy_y = require_int(tile[1], "deployment y", 0, height - 1)
        deploy_world_x = (deploy_x + 0.5 - width / 2) * 2
        deploy_world_z = (height / 2 - deploy_y - 0.5) * 2
        deploy_world_y = fixed(
            (surface_lifts[deploy_y * width + deploy_x] + 2) / 64
        )
        deployment_entity_indices.append(entity_index)
        lines.append(
            f"    {{ .id={next_id}u, .name=\"deployment_tile\", .parent_entity_index=-1, .position={{{fixed(deploy_world_x)},{deploy_world_y},{fixed(deploy_world_z)}}}, .scale={{65536,65536,65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index={cursor_mesh_index}, .mesh_size=65536, .mesh_enabled=1, .color_r=65, .color_g=190, .color_b=175, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_FLOOR, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
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
    scenery_model_count = len({entry[0] for entry in scenery_metadata})
    camera_focus_x = camera_focus(cursor_x, width, CAMERA_VIEW_TILES_X)
    camera_focus_z = -camera_focus(cursor_y, height, CAMERA_VIEW_TILES_Y)
    cursor_world_y = fixed(surface_lifts[cursor_y * width + cursor_x] / 64)
    cursor_index = entity_index
    lines.append(
        f"    {{ .id={next_id}u, .name=\"player_one_cursor\", .parent_entity_index=-1, .position={{{fixed(cursor_world_x)},{cursor_world_y},{fixed(cursor_world_z)}}}, .scale={{65536,65536,65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index={cursor_mesh_index}, .mesh_size=65536, .mesh_enabled=1, .color_r=255, .color_g=225, .color_b=48, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_FLOOR, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
    )
    next_id += 1
    entity_index += 1
    lines.append(
        f"    {{ .id={next_id}u, .name=\"player_two_cursor\", .parent_entity_index=-1, .position={{{fixed(cursor_world_x)},{cursor_world_y},{fixed(cursor_world_z)}}}, .scale={{65536,65536,65536}}, .mesh=PS1_MESH_CUSTOM, .mesh_index={cursor_mesh_index}, .mesh_size=65536, .mesh_enabled=0, .color_r=72, .color_g=220, .color_b=255, .texture_tiling_u_q8=256, .texture_tiling_v_q8=256, .render_unlit=1, .render_preset=PS1_RENDER_PRESET_FLOOR, .rigid_root_entity_index=-1, .collider_shape=-1, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},"
    )
    next_id += 1
    entity_index += 1
    foliage_entity_indices = []
    for mesh in foliage_metadata[surface_count:]:
        if not mesh:
            foliage_entity_indices.append(0xffffffff)
            continue
        foliage_entity_indices.append(entity_index)
        lines.append(f'    {{.id={next_id}u,.name="forest_canopies",.parent_entity_index=-1,.scale={{65536,65536,65536}},.mesh=PS1_MESH_CUSTOM,.mesh_index={mesh},.mesh_size=65536,.mesh_enabled=0,.color_r=255,.color_g=255,.color_b=255,.texture_tiling_u_q8=256,.texture_tiling_v_q8=256,.render_unlit=1,.render_preset=PS1_RENDER_PRESET_FLOOR,.rigid_root_entity_index=-1,.collider_shape=-1,.camera_shot_trigger_index=-1,.collider_camera_target_index=-1}},')
        next_id += 1
        entity_index += 1
    camera_index = entity_index
    lines.extend((
        f"    {{ .id={next_id}u, .name=\"clamped_board_camera\", .parent_entity_index=-1, .position={{{fixed(camera_focus_x)},{fixed(CAMERA_HEIGHT)},{fixed(camera_focus_z + CAMERA_TRAILING_DISTANCE)}}}, .rotation={{{fixed(-40)},0,0}}, .scale={{65536,65536,65536}}, .rigid_root_entity_index=-1, .collider_shape=-1, .has_camera=1, .camera_primary=1, .camera_fov={fixed(45)}, .camera_near={fixed(0.2)}, .camera_far={fixed(80)}, .camera_shot_trigger_index=-1, .collider_camera_target_index=-1 }},",
        "};", "",
        '_Static_assert(sizeof(k_ps1_entities) / sizeof(k_ps1_entities[0]) <= PS1_RENDER_ENTITY_LIST_MAX,',
        '               "renderer entity index capacity omits scene entries");',
        "", "const ps1_scene g_ps1_scene = {", "    .entities=k_ps1_entities,", "    .entity_count=sizeof(k_ps1_entities)/sizeof(k_ps1_entities[0]),", f"    .camera_entity_index={camera_index},", "    .light={ .enabled=1, .dir_x=-1024, .dir_y=3072, .dir_z=2048, .color_r=255, .color_g=244, .color_b=220, .ambient_r=96, .ambient_g=96, .ambient_b=112 },", f"    .fog_enabled=1, .fog_r={fog_colour[0]}, .fog_g={fog_colour[1]}, .fog_b={fog_colour[2]},",
        # Keep foreground terrain legible; haze belongs beyond the near board.
        f"    .fog_start={fixed(24)}, .fog_end={fixed(36)},",
        f"    .meshes=g_ps1_meshes, .mesh_count={cursor_mesh_index + (1 if hidden_cells else 0) + 3 + scenery_model_count + sum(bool(mesh) for mesh in foliage_metadata)},", "};", "",
        "const uint64_t g_grandleon_unit_ids[] = {",
        *(f"    {int(unit['id'])}ULL," for unit in units if isinstance(unit, dict)),
        "};",
        "const unsigned int g_grandleon_unit_entity_indices[] = {",
        *(f"    {index}u," for index in unit_entity_indices),
        "};",
        "const unsigned int g_grandleon_unit_pose_texture_indices[][9] = {",
        *("    {" + ",".join(f"{texture}u" for texture in textures) + "},"
          for textures in unit_pose_texture_indices),
        "};",
        "const uint64_t g_grandleon_speaker_unit_type_ids[] = {",
        *(f"    {identity}ULL," for identity in (speaker_indices or {})),
        "    0ULL," if not speaker_indices else "",
        "};",
        "const unsigned int g_grandleon_speaker_texture_indices[] = {",
        *(f"    {texture}u," for texture in (speaker_indices or {}).values()),
        "    0u," if not speaker_indices else "",
        "};",
        f"const unsigned int g_grandleon_speaker_count = {len(speaker_indices or {})}u;",
        "const unsigned int g_grandleon_board_billboard_mesh_index = 1u;",
        f"const unsigned int g_grandleon_mirrored_billboard_mesh_index = {mirrored_billboard_mesh_index}u;",
        f"const unsigned int g_grandleon_directional_billboard_mesh_index = {directional_billboard_mesh_index}u;",
        f"const unsigned int g_grandleon_directional_mirrored_billboard_mesh_index = {directional_mirrored_billboard_mesh_index}u;",
        "const unsigned int g_grandleon_unit_count = sizeof(g_grandleon_unit_ids) / sizeof(g_grandleon_unit_ids[0]);",
        f"const unsigned int g_grandleon_visibility_entity_index = {visibility_entity_index}u;",
        "const uint16_t g_grandleon_visibility_cell_indices[] = {",
        *(f"    {cell}u," for cell in hidden_cells),
        "    0u," if not hidden_cells else "",
        "};",
        f"const unsigned int g_grandleon_visibility_cell_count = {len(hidden_cells)}u;",
        f"const unsigned int g_grandleon_visibility_reveal_radius = {int(visibility['reveal_radius'])}u;",
        "const unsigned int g_grandleon_deployment_entity_indices[] = {",
        *(f"    {index}u," for index in deployment_entity_indices),
        "    0u," if not deployment_entity_indices else "",
        "};",
        f"const unsigned int g_grandleon_deployment_entity_count = {len(deployment_entity_indices)}u;",
        "const int32_t g_grandleon_terrain_surface_y_q16[] = {",
        *(f"    {fixed(value / 64)}," for value in surface_lifts),
        "};",
        "const unsigned int g_grandleon_terrain_surface_count = sizeof(g_grandleon_terrain_surface_y_q16) / sizeof(g_grandleon_terrain_surface_y_q16[0]);",
        "const unsigned int g_grandleon_board_surface_entity_indices[] = {",
        *(f"    {index}u," for index in range(surface_count)),
        "};",
        f"const unsigned int g_grandleon_board_surface_count = {surface_count}u;",
        "const unsigned int g_grandleon_terrain_entity_indices[] = {",
        *(f"    {index}u," for index in terrain_entity_indices),
        "};",
        "const int16_t g_grandleon_terrain_chunk_bounds[][4] = {",
        *(f"    {{{min_x},{min_y},{max_x},{max_y}}}," for _mesh, min_x, min_y, max_x, max_y, _focus_x, _focus_z in terrain_chunks[surface_count:]),
        "};",
        "const int16_t g_grandleon_terrain_page_focus[][2] = {",
        *(f"    {{{focus_x},{focus_z}}}," for _mesh, _min_x, _min_y, _max_x, _max_y, focus_x, focus_z in terrain_chunks[surface_count:]),
        "};",
        "const unsigned int g_grandleon_terrain_chunk_count = sizeof(g_grandleon_terrain_entity_indices) / sizeof(g_grandleon_terrain_entity_indices[0]);",
        "const unsigned int g_grandleon_foliage_entity_indices[] = {",
        *(f"    {index}u," for index in foliage_entity_indices),
        "};",
        "const unsigned int g_grandleon_tree_entity_indices[] = {",
        *(f"    {index}u," for index in tree_entity_indices),
        "};",
        "const unsigned int g_grandleon_tree_count = sizeof(g_grandleon_tree_entity_indices) / sizeof(g_grandleon_tree_entity_indices[0]);",
        "const unsigned int g_grandleon_cursor_entity_indices[] = {",
        f"    {cursor_index}u, {cursor_index + 1}u,",
        "};",
        f"const unsigned int g_grandleon_combat_floor_entity_index = {combat_floor_index}u;",
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
extern const uint64_t g_grandleon_speaker_unit_type_ids[];
extern const unsigned int g_grandleon_speaker_texture_indices[];
extern const unsigned int g_grandleon_speaker_count;
extern const unsigned int g_grandleon_unit_entity_indices[];
extern const unsigned int g_grandleon_unit_pose_texture_indices[][9];
extern const unsigned int g_grandleon_board_billboard_mesh_index;
extern const unsigned int g_grandleon_mirrored_billboard_mesh_index;
extern const unsigned int g_grandleon_directional_billboard_mesh_index;
extern const unsigned int g_grandleon_directional_mirrored_billboard_mesh_index;
extern const unsigned int g_grandleon_unit_count;
extern const unsigned int g_grandleon_visibility_entity_index;
extern const uint16_t g_grandleon_visibility_cell_indices[];
extern const unsigned int g_grandleon_visibility_cell_count;
extern const unsigned int g_grandleon_visibility_reveal_radius;
extern const unsigned int g_grandleon_deployment_entity_indices[];
extern const unsigned int g_grandleon_deployment_entity_count;
extern const int32_t g_grandleon_terrain_surface_y_q16[];
extern const unsigned int g_grandleon_terrain_surface_count;
extern const unsigned int g_grandleon_terrain_entity_indices[];
extern const int16_t g_grandleon_terrain_chunk_bounds[][4];
extern const int16_t g_grandleon_terrain_page_focus[][2];
extern const unsigned int g_grandleon_board_surface_entity_indices[];
extern const unsigned int g_grandleon_board_surface_count;
extern const unsigned int g_grandleon_terrain_chunk_count;
extern const unsigned int g_grandleon_foliage_entity_indices[];
extern const unsigned int g_grandleon_tree_entity_indices[];
extern const unsigned int g_grandleon_tree_count;
extern const unsigned int g_grandleon_cursor_entity_indices[2];
extern const unsigned int g_grandleon_combat_floor_entity_index;
extern const unsigned int g_grandleon_camera_entity_index;
extern const int32_t g_grandleon_camera_height;
extern const int32_t g_grandleon_camera_trailing_distance;
extern const unsigned int g_grandleon_camera_view_tiles_x;
extern const unsigned int g_grandleon_camera_view_tiles_y;
extern const unsigned int g_grandleon_ui_texture_index;
extern const unsigned int g_grandleon_combat_plate_indices[4];
ps1_entity* grandleon_scene_mutable_entity(unsigned int index);
#ifdef __cplusplus
}
#endif
#endif
"""
    header = header.replace("#define GRANDLEON_GENERATED_SCENE_H",
        f"#define GRANDLEON_GENERATED_SCENE_H\n#define GRANDLEON_WATER_TEXTURE_INDEX {water_texture_index}u", 1)
    (output / "grandleon_scene.h").write_text(header, encoding="utf-8")
    return entity_index + 1  # plus the camera appended below the indexed entities


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
    depth = min(requested_depth, 2) if args.apron_depth is None else require_int(args.apron_depth, "apron depth", 0, 8)
    if mode == 0 and args.apron_depth is None:
        depth = 0
    policy = args.surroundings_policy or default_policy(mode)
    fixed_terrain = [int(value) for value in surroundings.get("terrain", [])]
    for index, kind in enumerate(fixed_terrain):
        require_int(kind, f"surroundings terrain[{index}]", 0, len(TERRAIN_NAMES) - 1)

    def kind_at(x: int, y: int) -> int:
        if 0 <= x < width and 0 <= y < height:
            return terrain[y * width + x]
        return apron_kind(
            terrain, width, height, x, y, policy, fixed_terrain,
            args.surroundings_seed,
        )

    cell_lifts = {
        (x, y): relief_height(kind_at(x, y), x, y, args.surroundings_seed)
        for y in range(-depth, height + depth)
        for x in range(-depth, width + depth)
    }
    cells: list[tuple[int, int, int, bool, int, int]] = []
    if args.terrain_relief == "smooth":
        # Coherent horizontal patches share globally-derived corner heights.
        # Runs stop at the four-cell relief anchors and when terrain changes;
        # this preserves authored materials while allowing the PS1 to submit
        # one native textured quad instead of a primitive for every cell.
        minimum_x = -depth
        maximum_x = width + depth
        for y in range(-depth, height + depth):
            x = minimum_x
            while x < maximum_x:
                kind = kind_at(x, y)
                authored = 0 <= x < width and 0 <= y < height
                anchor_end = min(
                    maximum_x,
                    minimum_x + ((x - minimum_x) // 4 + 1) * 4,
                )
                span = 1
                while kind not in (8, 12) and x + span < anchor_end:
                    next_authored = (
                        0 <= x + span < width and 0 <= y < height
                    )
                    if kind_at(x + span, y) != kind or next_authored != authored:
                        break
                    span += 1
                cells.append((
                    x, y, kind, authored, span, 1,
                ))
                x += span
        cells = coalesce_vertical_runs(cells, cell_lifts)
    else:
        # Flat comparison mode retains the compact run-coalesced board used by
        # the previous renderer so captures can isolate the value of relief.
        for y in range(height):
            x = 0
            while x < width:
                kind = terrain[y * width + x]
                span = 1
                while (
                    kind not in (8, 12) and span < 4 and x + span < width
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
                    cells.append((x, y, kind_at(x, y), False, span_x, depth))
            vertical_chunk = math.ceil(height / 2)
            for y in range(0, height, vertical_chunk):
                span_y = min(vertical_chunk, height - y)
                for x in (-depth, width):
                    cells.append((x, y, kind_at(x, y), False, depth, span_y))

    units = document["units"]
    assert isinstance(units, list)
    typed_units = [unit for unit in units if isinstance(unit, dict)]
    castle_cells = [(index % width, index // width)
                    for index, value in enumerate(document.get("terrain_ids", []))
                    if int(value) == content_id("mountain_castle_wall")]
    keep_parts = []
    if castle_cells:
        if len(castle_cells) != 18 or max(x for x, _ in castle_cells) - min(x for x, _ in castle_cells) != 5 or max(y for _, y in castle_cells) - min(y for _, y in castle_cells) != 5:
            raise ValueError("stone keep requires the authored six-by-six castle footprint")
        keep_parts = load_keep(repo, "ps1")
    settlement_instances = settlement_placements(document["terrain_ids"], width, height, content_id)
    settlement_models = load_settlement_models(repo) if settlement_instances else {}
    house_instances = []
    for x, y, kind, size in settlement_instances:
        if kind == "village_house" or kind in tuple(f"{family}_house" for family in HOUSE_FAMILIES[1:]):
            family = "medieval" if kind == "village_house" else kind.removesuffix("_house")
            drawing = house_at(repo, x, y, family)
            kind = f"house_cutout_{drawing['drawing']}_{int(drawing['mirror'])}"
        elif kind == "ancient_grove":
            kind = "grove_cutout"
        house_instances.append((x, y, kind, size))
    settlement_instances = house_instances
    all_kinds = [kind for _x, _y, kind, _authored, _span_x, _span_y in cells]
    terrain_indices, unit_indices, ui_texture, texture_metrics, speaker_indices = write_textures(
        output, repo, all_kinds, typed_units, str(document.get("map_id", "0")),
        document.get("speakers", []),
        castle=bool(keep_parts), houses=any(k.startswith("house_cutout_") for _x,_y,k,_size in settlement_instances), theme=int(document.get("theme",0)), grove=any(k == "grove_cutout" for _x,_y,k,_size in settlement_instances),
        house_drawings=tuple(sorted({int(k.split("_")[2]) for _x,_y,k,_size in settlement_instances if k.startswith("house_cutout_")})),
    )
    terrain_form_cells = select_terrain_form_cells(
        terrain, width, height, args.surroundings_seed,
    )
    floor_groups = surface_strips(cells, width, height, depth)
    terrain_groups = floor_groups + page_cells(cells, width, height, depth)
    distant_kind = max(
        sorted({kind for kind in terrain if kind not in (1, 12)} or set(terrain)),
        key=terrain.count,
    )
    visibility = document["visual_visibility"]
    assert isinstance(visibility, dict)
    hidden_cells = [int(value) for value in visibility["hidden_cells"]]
    surface_lifts = (
        [
            surface_relief(x, y, cell_lifts)
            for y in range(height) for x in range(width)
        ]
        if args.terrain_relief == "smooth" else [0] * (width * height)
    )
    scenery_metadata = []
    foliage_metadata = []
    board_vertices, board_triangles, terrain_chunks = write_meshes(
        output, terrain_groups, width, height, terrain_form_cells,
        terrain_indices, args.surroundings_seed,
        args.terrain_relief, cell_lifts, hidden_cells, castle_cells,
        keep_parts, settlement_instances, settlement_models,
        MAX_CACHE_VERTICES, scenery_metadata, surface_count=len(floor_groups),
        foliage_metadata=foliage_metadata,
    )
    entities = write_scene(
        output, document, unit_indices, terrain_chunks,
        ui_texture,
        surface_lifts,
        shifted_colour(terrain_colours(repo, distant_kind, int(document.get("theme",0)))[0], 65),
        speaker_indices, scenery_metadata, surface_count=len(floor_groups),
        foliage_metadata=foliage_metadata,
        water_texture_index=terrain_indices.get(0, 0),
    )
    visible_units = visible_position_peak(
        [(int(unit["x"]), int(unit["y"])) for unit in typed_units],
        width, height,
    )
    visible_covers = len(hidden_cells)
    simulation = document["simulation"]
    assert isinstance(simulation, dict)
    deployment_tiles = simulation.get("deployment_tiles", [])
    if not isinstance(deployment_tiles, list):
        raise ValueError("simulation deployment tiles are not an array")
    deployment_markers = len(deployment_tiles)
    cache_vertices = board_vertices + (
        visible_units + visible_covers + deployment_markers
    ) * 4
    cache_triangles = board_triangles + (
        visible_units + visible_covers + deployment_markers
    ) * 2
    cache_vertex_limit = max(MAX_CACHE_TOTAL, (cache_vertices + 63) // 64 * 64)
    cache_triangle_limit = max(MAX_CACHE_TOTAL, (cache_triangles + 63) // 64 * 64)
    if cache_vertex_limit > 4096 or cache_triangle_limit > 4096:
        raise ValueError("authored scenery exceeds the bounded static geometry pools")
    if cache_vertices > cache_vertex_limit or cache_triangles > cache_triangle_limit:
        raise ValueError(
            f"visible scene exceeds Mipsync static pools: {cache_vertices}/{cache_vertex_limit} vertices, "
            f"{cache_triangles}/{cache_triangle_limit} triangles; production paging must split this view"
        )
    metadata = {
        "schema": "grandleon.mipsync.generated.v2", "renderer": "mipsync",
        "map": {"width": width, "height": height},
        "surroundings": {"policy": policy, "seed": args.surroundings_seed, "visible_depth": depth, "authored_depth": requested_depth},
        "entities": entities,
        "terrain_chunks": len(terrain_chunks),
        "foliage": {"pages": sum(bool(mesh) for mesh in foliage_metadata),
                    "entities": sum(bool(mesh) for mesh in foliage_metadata),
                    "max_displacement_mesh_units": [4, 0, 2],
                    "update_logic_ticks": 8},
        "terrain_forms": len(terrain_form_cells),
        "terrain_relief": args.terrain_relief,
        "terrain_material": "relay",
        "visibility": {
            "cells": len(hidden_cells),
            "entities": 1 if hidden_cells else 0,
            "reveal": "whole_authored_region_on_entry",
        },
        "deployment_markers": deployment_markers,
        "visible": {"units": visible_units, "covers": visible_covers},
        "cache": {"vertices": cache_vertices, "vertex_limit": cache_vertex_limit, "triangles": cache_triangles, "triangle_limit": cache_triangle_limit, "mesh_vertex_limit": MAX_CACHE_VERTICES},
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
