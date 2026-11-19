#!/usr/bin/env python3
"""Stage one authored project for the Pyrite Nintendo 64 runtime.

The creator places the selected campaign first in project.json. This tool
validates the subset the base-memory renderer can hold, produces a small
project manifest, stages every required character pose, and authors a compact
terrain atlas/backdrop from the selected campaign's own terrain vocabulary.
Nothing falls back to Steel and Cedar assets.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "platform/console_art"))
from scenery_style import document as scenery_document, write_n64_header as write_style_header, save_ci8_house, save_ci4_image, grove_image, ground_image
from settlement_scenery import write_n64_header as write_settlement_header
from stone_keep import write_n64_header
from keep_motion import write_n64_motion_header

# The pinned builder uses Pillow 9.0; filter values are unchanged across APIs.
RESAMPLING = getattr(Image, "Resampling", Image)
QUANTIZE = getattr(Image, "Quantize", Image)


TERRAIN_KINDS = (
    (("water", "river"), "water", (55, 112, 157), (91, 166, 190)),
    (("bridge", "road"), "road", (142, 111, 70), (210, 179, 119)),
    (("forest", "wood"), "forest", (50, 100, 54), (99, 151, 75)),
    (("mountain", "rock"), "mountain", (100, 104, 96), (159, 163, 144)),
    (("sand", "desert"), "sand", (176, 143, 80), (226, 194, 119)),
    (("snow", "ice"), "snow", (171, 199, 207), (233, 242, 238)),
    (("swamp", "marsh"), "swamp", (65, 91, 63), (116, 130, 76)),
    (("hill", "highland"), "hills", (105, 124, 72), (164, 174, 105)),
    (("ruin", "rubble"), "ruins", (106, 95, 87), (171, 157, 137)),
    (("grass", "plain"), "grass", (84, 139, 67), (139, 185, 95)),
    (("farm", "field"), "farmland", (129, 122, 61), (194, 169, 91)),
    (("bamboo", "thicket"), "bamboo", (57, 119, 67), (110, 166, 79)),
    (("pave", "cobble"), "paved", (116, 121, 117), (188, 191, 177)),
)
MAX_BOARD_CELLS = 32 * 24  # Matches ProjectBoard.cpp; arenas grow with loaded maps.
MAX_DRAWINGS = 16
MAX_TERRAIN_KINDS = 9
ATLAS_COLUMNS = 3
# CI textures can use only TMEM's lower 2 KiB while the TLUT occupies the
# upper bank. Libdragon aligns each 60-pixel row to 32 bytes, so 20x20 cells
# produce a 60x60 atlas occupying 1,920 texel bytes.
ATLAS_CELL = 20


class ProjectError(RuntimeError):
    pass


def stable_id(value: str) -> int:
    result = 14695981039346656037
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return result


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def terrain_kind(name: str) -> int:
    lowered = name.lower()
    for index, (keywords, _label, _dark, _light) in enumerate(TERRAIN_KINDS):
        if any(keyword in lowered for keyword in keywords):
            return index
    raise ProjectError(
        f"terrain {name!r} has no Nintendo 64 presentation kind; "
        "rename it with a supported terrain word"
    )


def reachable_campaign(project: dict) -> tuple[dict, list[dict]]:
    campaigns = project.get("campaigns")
    if not isinstance(campaigns, list) or not campaigns or not isinstance(campaigns[0], dict):
        raise ProjectError("the selected project has no playable campaign")
    campaign = campaigns[0]
    flow = campaign.get("flow")
    if not isinstance(flow, dict):
        raise ProjectError("the selected campaign has no flow graph")
    nodes = {
        str(node.get("id")): node
        for node in flow.get("nodes", [])
        if isinstance(node, dict) and node.get("id")
    }
    current = str(flow.get("entryNodeId") or "")
    if current not in nodes:
        raise ProjectError("the selected campaign entry node is missing")
    # Stage assets for every reachable battle. Runtime campaign predicates,
    # rather than the exporter, decide whether play follows victory or defeat.
    route: list[dict] = []
    visited: set[str] = set()
    active: set[str] = set()

    pending = [(current, False)]
    while pending:
        identity, exiting = pending.pop()
        if exiting:
            active.remove(identity)
            visited.add(identity)
            continue
        if identity in active:
            raise ProjectError("the selected campaign route contains a cycle")
        if identity in visited:
            continue
        if identity not in nodes:
            raise ProjectError(f"campaign targets missing node {identity!r}")
        active.add(identity)
        node = nodes[identity]
        route.append(node)
        pending.append((identity, True))
        if str(node.get("kind")) != "terminal":
            transitions = node.get("transitions", [])
            if not isinstance(transitions, list) or not transitions:
                raise ProjectError(f"campaign node {identity!r} has no route to another node")
            for transition in reversed(transitions):
                if not isinstance(transition, dict):
                    raise ProjectError(f"campaign node {identity!r} has an invalid transition")
                pending.append((str(transition.get("targetNodeId") or ""), False))

    encounters = [node for node in route if str(node.get("kind")) == "encounter"]
    if not encounters:
        raise ProjectError("the selected campaign contains no battles")
    return campaign, encounters


def selected_maps(project: dict, encounters: list[dict]) -> list[dict]:
    maps = {
        str(value.get("id")): value
        for value in project.get("maps", [])
        if isinstance(value, dict) and value.get("id")
    }
    result: list[dict] = []
    seen: set[str] = set()
    for encounter in encounters:
        map_id = str(encounter.get("mapId") or "")
        if map_id not in maps:
            raise ProjectError(
                f"campaign encounter {encounter.get('id')!r} names missing map {map_id!r}"
            )
        if map_id not in seen:
            seen.add(map_id)
            result.append(maps[map_id])
    return result


def draw_atlas(path: Path, kinds: list[int], theme: int = 0) -> None:
    rows = 3
    image = Image.new(
        "RGB", (ATLAS_COLUMNS * ATLAS_CELL, rows * ATLAS_CELL), (237, 237, 237)
    )
    for slot, kind in enumerate(kinds):
        # Vertex colours carry terrain identity; a neutral detail atlas avoids
        # double tinting and shares all sixteen luminance levels across kinds.
        x0 = (slot % ATLAS_COLUMNS) * ATLAS_CELL
        y0 = (slot // ATLAS_COLUMNS) * ATLAS_CELL
        tile = ground_image(Path(__file__).resolve().parents[4], kind, theme).resize((ATLAS_CELL, ATLAS_CELL), RESAMPLING.BOX).convert("L")
        values = list(tile.getdata())
        mean = max(1.0, sum(values) / len(values))
        for y in range(ATLAS_CELL):
            for x in range(ATLAS_CELL):
                value = max(0,min(255,round(values[y*ATLAS_CELL+x] * 238 / mean)))
                image.putpixel((x0+x,y0+y),(value,value,value))
    image.quantize(colors=16, method=QUANTIZE.MEDIANCUT).convert("RGBA").save(path)


def draw_backdrop(path: Path, dominant_kind: int) -> None:
    _keywords, _label, dark, light = TERRAIN_KINDS[dominant_kind]
    image = Image.new("RGB", (320, 240), (32, 50, 66))
    pixels = image.load()
    for y in range(240):
        if y < 116:
            blend = y / 116.0
            colour = tuple(round(34 + blend * component * 0.35) for component in (65, 86, 104))
        else:
            blend = (y - 116) / 124.0
            colour = tuple(round(light[channel] * (1.0 - blend * 0.48)) for channel in range(3))
        for x in range(320):
            noise = ((x * 13 + y * 7 + (x // 19) * 11) & 7) - 3
            pixels[x, y] = tuple(max(0, min(255, channel + noise)) for channel in colour)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 112, 319, 121), fill=dark)
    for x in range(-20, 340, 34):
        draw.polygon(((x, 120), (x + 17, 104), (x + 34, 120)), fill=dark)
    for y in range(152, 240, 24):
        draw.line((0, y, 319, y), fill=dark, width=2)
    image.quantize(colors=256, method=QUANTIZE.MEDIANCUT).convert("RGBA").save(path)


def write_asset_config(path: Path, name: str, format_index: int) -> None:
    payload = {
        "baseScale": 16,
        "baseScaleOverride": 0,
        "compression": 0,
        "data": None,
        "exclude": False,
        "fontCharset": "",
        "fontId": 0,
        "format": format_index,
        "gltfBVH": False,
        "uuid": stable_id("grandleon-pyrite/" + name) & 0x7FFFFFFFFFFFFFFF,
        "wavCompression": 0,
        "wavForceMono": False,
        "wavResampleRate": 0,
    }
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def draw_surface_detail(path: Path) -> None:
    """Neutral relief grain: vertex colours still define each material.

    A 32x32 RGBA16 swatch occupies 2 KiB of TMEM and can be reused for the
    entire prop pass. Broad clusters survive the tactical camera's filtering.
    """
    image = Image.new("RGB", (32, 32))
    pixels = image.load()
    for y in range(32):
        for x in range(32):
            cluster = ((x // 4 * 13 + y // 4 * 7) % 11) - 5
            grain = ((x * 17 + y * 29) % 13) - 6
            value = max(140, min(255, 218 + cluster * 5 + grain))
            pixels[x, y] = (value, value, value)
    image.save(path)


def prepare(project_path: Path, manifest_path: Path, project_root: Path,
            review_driver: bool = False) -> None:
    project = json.loads(project_path.read_text(encoding="utf-8"))
    campaign, encounters = reachable_campaign(project)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    maps = selected_maps(project, encounters)
    # Reject unsupported layouts before producing any cartridge assets. A
    # compiled package can be larger than this renderer's current buffers.
    for value in maps:
        width, height = int(value.get("width", 0)), int(value.get("height", 0))
        if width * height > MAX_BOARD_CELLS:
            raise ProjectError(
                f"map {value.get('name') or value.get('id')!r} is {width}×{height} "
                f"({width * height} cells); the current Nintendo 64 renderer "
                f"supports at most {MAX_BOARD_CELLS} cells. "
                "Use a smaller console map or test this layout in Godot."
            )

    kinds = sorted({
        terrain_kind(str(cell))
        for map_value in maps
        for cell in map_value.get("terrain", [])
    })
    if len(kinds) > MAX_TERRAIN_KINDS:
        labels = ", ".join(TERRAIN_KINDS[index][1] for index in kinds)
        raise ProjectError(
            f"campaign uses {len(kinds)} terrain kinds ({labels}); the base-memory "
            f"Nintendo 64 atlas supports at most {MAX_TERRAIN_KINDS}"
        )
    drawings = manifest.get("drawings", [])
    if not isinstance(drawings, list) or not drawings:
        raise ProjectError("console art generator produced no character drawings")
    if len(drawings) > MAX_DRAWINGS:
        raise ProjectError(
            f"campaign needs {len(drawings)} character drawings; the base-memory "
            f"Nintendo 64 cache supports at most {MAX_DRAWINGS}"
        )

    assets = project_root / "assets"
    generated = project_root / "generated"
    source_art = manifest_path.parent
    assets.mkdir(parents=True, exist_ok=True)
    generated.mkdir(parents=True, exist_ok=True)

    staged_sprites: list[str] = []
    drawing_rows: list[str] = []
    for index, drawing in enumerate(drawings):
        directional = drawing["assets"].get("directional", {})
        drawing_rows.append(
            "    {%dU,%dU,%dU,%dU,%s}," % (
                int(drawing["style"]), int(drawing["figure"]),
                int(drawing["archetype"]), int(drawing["colour"]),
                "true" if directional else "false",
            )
        )
        assets_by_view = drawing["assets"]
        for target, view, pose in (
            ("board_idle", "board", "idle"),
            ("board_move", "board", "move"),
            ("close_idle", "close", "idle"),
            ("close_attack", "close", "attack"),
            ("close_hurt", "close", "hurt"),
        ):
            name = f"unit_{index:02d}_{target}"
            source = source_art / str(assets_by_view[view][pose])
            if not source.is_file():
                raise ProjectError(f"generated character asset is missing: {source.name}")
            shutil.copyfile(source, assets / f"{name}.png")
            write_asset_config(assets / f"{name}.png.conf", name, 4)
            staged_sprites.append(f"filesystem/{name}.sprite")
        if directional:
            for pose in ("north_idle", "north_contact", "north_move", "south_idle", "south_contact", "south_move"):
                source = source_art / str(directional.get(pose, ""))
                if not source.is_file():
                    raise ProjectError(f"generated directional asset is missing: {pose}")
                name = f"unit_{index:02d}_{pose}"
                shutil.copyfile(source, assets / f"{name}.png")
                write_asset_config(assets / f"{name}.png.conf", name, 4)
                staged_sprites.append(f"filesystem/{name}.sprite")

    slot_by_kind = [255] * (len(TERRAIN_KINDS) + 1)
    for slot, kind in enumerate(kinds):
        slot_by_kind[kind] = slot
    theme = ["temperate", "autumn", "winter", "ashland"].index(project.get("themeId", "temperate"))
    draw_atlas(assets / "material_atlas.png", kinds, theme)
    write_asset_config(assets / "material_atlas.png.conf", "material_atlas", 4)
    draw_surface_detail(assets / "surface_detail.png")
    write_asset_config(assets / "surface_detail.png.conf", "surface_detail", 0)
    castle_source = Path(__file__).resolve().parents[4] / "platform/console_art"
    masonry = Image.open(castle_source / "stone_keep_masonry.png").convert("RGBA")
    masonry.resize((32, 32), RESAMPLING.BOX).save(assets / "castle_masonry.png")
    write_asset_config(assets / "castle_masonry.png.conf", "castle_masonry", 0)
    write_n64_header(castle_source.parents[1], project_root / "src/user/GeneratedStoneKeep.hpp",
                     any("castle_wall" in str(cell) for map_value in maps for cell in map_value.get("terrain", [])))
    write_n64_motion_header(castle_source.parents[1], project_root / "src/user/GeneratedKeepMotion.hpp",
                            any("castle_wall" in str(cell) for map_value in maps for cell in map_value.get("terrain", [])))
    write_settlement_header(castle_source.parents[1], project_root / "src/user/GeneratedSettlement.hpp")
    write_style_header(castle_source.parents[1], project_root / "src/user/GeneratedSceneryStyle.hpp", theme)
    for drawing in range(len(scenery_document(castle_source.parents[1])["houses"]["drawings"])):
        name = f"scenery_house_{drawing}"
        save_ci8_house(castle_source.parents[1], drawing, assets / f"{name}.png")
        write_asset_config(assets / f"{name}.png.conf", name, 3)  # Pyrite TexFormat::CI8

    save_ci4_image(grove_image(castle_source.parents[1], 64, theme), assets / "scenery_grove.png")
    write_asset_config(assets / "scenery_grove.png.conf", "scenery_grove", 4)
    dominant = max(
        kinds,
        key=lambda kind: sum(
            terrain_kind(str(cell)) == kind
            for map_value in maps for cell in map_value.get("terrain", [])
        ),
    )
    draw_backdrop(assets / "combat_backdrop.png", dominant)
    write_asset_config(assets / "combat_backdrop.png.conf", "combat_backdrop", 3)
    backdrop_paths = []
    art_root = Path(__file__).resolve().parents[3] / "godot/project"
    backdrop_manifest = json.loads((art_root / "art/combat_backdrops/manifest.json").read_text())
    for index, encounter in enumerate(encounters):
        encounter_hash = f"{stable_id(str(encounter.get('mapId'))):016x}"
        pack = next((pack for pack in backdrop_manifest["packs"]
                     if encounter_hash in pack.get("compatibility", {}).get("stage_ids", [])), None)
        if pack is None and int(manifest["defaultStyle"]) == 0:
            stage_map = next(value for value in maps if value.get("id") == encounter.get("mapId"))
            stage_kinds = [terrain_kind(str(cell)) for cell in stage_map.get("terrain", [])]
            dominant_name = TERRAIN_KINDS[max(set(stage_kinds), key=stage_kinds.count)][1]
            pack = next((value for value in backdrop_manifest["packs"]
                         if not value.get("compatibility") and value.get("dominant_terrain") == dominant_name), None)
        name = f"combat_stage_{index:02d}"
        if pack is not None:
            variant = stable_id(encounter_hash) % int(pack["variant_count"])
            source = art_root / pack["root"].removeprefix("res://") / pack["filename_patterns"]["panorama"].format(variant=variant)
            with Image.open(source) as panorama:
                panorama.convert("RGB").resize((320, 240), RESAMPLING.LANCZOS).quantize(
                    colors=256, method=QUANTIZE.MEDIANCUT
                ).convert("RGBA").save(assets / f"{name}.png")
        else:
            shutil.copyfile(assets / "combat_backdrop.png", assets / f"{name}.png")
        write_asset_config(assets / f"{name}.png.conf", name, 3)
        backdrop_paths.append(f"rom:/{name}.sprite")

    campaign_id = str(campaign.get("id") or "")
    title = str(project.get("title") or campaign.get("name") or "Grandleon")
    game_id = str(project.get("gameId") or "game")
    safe = re.sub(r"[^a-z0-9]+", "_", game_id.lower()).strip("_") or "game"
    save_base = f"g{stable_id(safe) & 0xFFFFFFFF:08x}"
    first_encounter = str(encounters[0].get("id"))
    scroll = any(int(value.get("width", 0)) > 8 or int(value.get("height", 0)) > 6 for value in maps)

    header = f"""// Generated from the selected authored project. Do not edit.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

inline constexpr char grandleon_project_title[] = {cpp_string(title)};
inline constexpr bool grandleon_project_review_driver = {'true' if review_driver else 'false'};
inline constexpr char grandleon_project_game_id[] = {cpp_string(game_id)};
inline constexpr char grandleon_project_campaign_source[] = {cpp_string(campaign_id)};
inline constexpr char grandleon_project_save_base[] = {cpp_string(save_base)};
inline constexpr char grandleon_project_save_1p[] = {cpp_string(save_base + '_1p')};
inline constexpr char grandleon_project_save_2p[] = {cpp_string(save_base + '_2p')};
inline constexpr std::uint64_t grandleon_project_campaign_id = {stable_id(campaign_id)}ULL;
inline constexpr std::uint64_t grandleon_project_first_encounter_id = {stable_id(campaign_id + '/' + first_encounter)}ULL;
inline constexpr std::size_t grandleon_project_encounter_count = {len(encounters)}U;
inline constexpr std::array<std::uint64_t, {len(encounters)}> grandleon_project_encounter_ids{{{{
    {','.join(str(stable_id(campaign_id + '/' + str(value.get('id')))) + 'ULL' for value in encounters)}
}}}};
inline constexpr std::array<const char*, {len(encounters)}> grandleon_project_backdrop_paths{{{{
    {','.join(cpp_string(path) for path in backdrop_paths)}
}}}};
inline constexpr bool grandleon_project_requires_scroll = {'true' if scroll else 'false'};
inline constexpr int grandleon_project_atlas_columns = {ATLAS_COLUMNS};
inline constexpr int grandleon_project_atlas_cell = {ATLAS_CELL};
inline constexpr std::array<std::uint8_t, {len(slot_by_kind)}> grandleon_project_terrain_slots{{{{
    {','.join(str(value) + 'U' for value in slot_by_kind)}
}}}};
inline constexpr std::uint8_t grandleon_project_default_style = {int(manifest['defaultStyle'])}U;
inline constexpr std::uint8_t grandleon_project_default_figure = {int(manifest['defaultFigure'])}U;
struct GrandleonProjectDrawing final {{
    std::uint8_t style, figure, archetype, colour;
    bool directional;
}};
inline constexpr std::array<GrandleonProjectDrawing, {len(drawings)}> grandleon_project_drawings{{{{
{chr(10).join(drawing_rows)}
}}}};
"""
    (project_root / "src/user/GeneratedProject.hpp").write_text(header, encoding="utf-8")

    makefile = (
        "# Generated project sprite list.\n"
        "GRANDLEON_PROJECT_SPRITES := \\\n\t"
        + " \\\n\t".join(staged_sprites)
        + "\n"
    )
    (generated / "ProjectAssets.mk").write_text(makefile, encoding="utf-8")
    print(
        f"Pyrite project: {title} / {campaign_id}: {len(encounters)} battles, "
        f"{len(drawings)} drawings, {len(kinds)} terrain kinds",
        file=sys.stderr,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--art-manifest", type=Path, required=True)
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument("--review-driver", action="store_true",
                        help="enable automated input for emulator review builds only")
    args = parser.parse_args()
    try:
        prepare(args.project, args.art_manifest, args.project_root, args.review_driver)
    except (OSError, ValueError, KeyError, TypeError, ProjectError) as error:
        print(f"Nintendo 64 Pyrite export refused: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
