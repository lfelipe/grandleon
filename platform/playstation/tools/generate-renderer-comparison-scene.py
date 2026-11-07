#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate PS1 constants from the shared renderer-comparison scene."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


TERRAIN_KINDS = {"grass": 0, "forest": 1, "paved": 2}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def colour15(value: str) -> int:
    require(len(value) == 6, f"invalid RGB colour: {value}")
    red, green, blue = (int(value[index:index + 2], 16)
                        for index in (0, 2, 4))
    return (red >> 3) | (green >> 3) << 5 | (blue >> 3) << 10


def fraction(value: float) -> int:
    return round(value * 1000)


def fnv1a32(value: str) -> int:
    result = 2166136261
    for byte in value.encode("utf-8"):
        result = ((result ^ byte) * 16777619) & 0xFFFFFFFF
    return result


def flatten(rows: list[list[int]], width: int, height: int,
            label: str) -> list[int]:
    require(len(rows) == height, f"{label} must have {height} rows")
    require(all(len(row) == width for row in rows),
            f"each {label} row must have {width} cells")
    return [cell for row in rows for cell in row]


def array(name: str, values: list[int], ctype: str = "unsigned char") -> str:
    return (f"inline constexpr {ctype} {name}[{len(values)}] = {{" +
            ",".join(str(value) for value in values) + "};")


def generate(scene_path: Path) -> str:
    scene = json.loads(scene_path.read_text(encoding="utf-8"))
    require(scene.get("schema") == 2, "renderer scene schema must be 2")
    require(scene.get("kind") == "renderer_showcase",
            "renderer scene must be a renderer_showcase")
    board = scene["board"]
    surroundings = scene["surroundings"]
    composition = scene["camera"]["composition"]
    close = scene["camera"]["close_view"]
    hud = scene["capture_hud"]

    terrain_ids = board["terrain_ids"]
    require(set(terrain_ids) == set(TERRAIN_KINDS),
            "PS1 comparison supports exactly forest, grass and paved")
    id_to_kind = {terrain_id: TERRAIN_KINDS[name]
                  for name, terrain_id in terrain_ids.items()}
    board_width, board_height = board["width"], board["height"]
    board_ids = flatten(board["rows"], board_width, board_height, "board")
    surrounding_width = surroundings["width"]
    surrounding_height = surroundings["height"]
    surrounding_ids = flatten(surroundings["rows"], surrounding_width,
                              surrounding_height, "surroundings")
    require(all(value in id_to_kind for value in board_ids + surrounding_ids),
            "renderer scene contains an unsupported terrain ID")
    board_kinds = [id_to_kind[value] for value in board_ids]
    surrounding_kinds = [id_to_kind[value] for value in surrounding_ids]

    origin_x, origin_y = surroundings["active_origin"]
    depth = surroundings["depth"]
    require(surrounding_width == board_width + depth * 2 and
            surrounding_height == board_height + depth * 2,
            "surroundings dimensions must extend every active edge")
    require([origin_x, origin_y] == [depth, depth],
            "active origin must equal the surrounding depth")
    extracted = [surrounding_kinds[(origin_y + row) * surrounding_width +
                                  origin_x + column]
                 for row in range(board_height)
                 for column in range(board_width)]
    require(extracted == board_kinds,
            "active board must match its surroundings window")

    units = sorted(scene["units"], key=lambda unit: unit["slot"])
    require([unit["slot"] for unit in units] == ["left", "right"],
            "comparison requires stable left and right slots")
    require(close["lock_unit_slots_for_exchange"],
            "comparison must lock unit sides")
    active = scene["active_cells"]
    require(active["selected"]["cell"] == units[0]["cell"] and
            active["attack_target"]["cell"] == units[1]["cell"],
            "active markers must match unit cells")
    require(hud["content_rect_native"] == [0, 0, 320, 240],
            "capture HUD must use the full native frame")
    forbidden = set(hud["forbidden"])
    require({"diagnostic_text", "reserved_title_band",
             "reserved_footer_band"} <= forbidden,
            "capture must forbid diagnostics and title/footer bands")
    require(composition["native_reference_resolution"] == [320, 240] and
            composition["reserved_title_pixels"] == 0 and
            composition["reserved_footer_pixels"] == 0,
            "capture composition must use all of native 320x240")

    forest = surroundings["decoration"]["forest"]
    active_trees = forest["active_cell_trees"]
    surrounding_trees = forest["surrounding_cell_trees"]
    require(forest["clustered"] and len(active_trees) == 2 and
            len(surrounding_trees) == 2,
            "comparison requires two clustered trees per forest cell")
    require(round(scene["duration_seconds"] * 1000) == 8150 and
            scene["video_fps"] == 30 and len(scene["events"]) == 2,
            "PS1 capture requires the canonical 8.15s/30fps timing")
    backdrop = scene["combat_backdrop"]
    descriptor = backdrop["descriptor"]
    seed = fnv1a32(descriptor["variant_seed_input"])
    require(backdrop["schema"] == 1 and
            descriptor["variant_seed_algorithm"] == "fnv1a32_utf8" and
            seed == descriptor["variant_seed"] and
            descriptor["selected_variant"] ==
            seed % descriptor["variant_count"] and
            len(backdrop["variants"]) == descriptor["variant_count"],
            "combat backdrop descriptor must select a deterministic variant")
    foreground = backdrop["foreground_contract"]
    require(backdrop["layer_order"] ==
            ["panorama", "transition", "midground", "props"] and
            foreground["live_floor_strip"] and foreground["live_units"] and
            foreground["live_shadows"] and
            foreground["live_attack_effects"] and
            not foreground["props_may_occlude_units"],
            "PS1 backdrop requires frozen layers behind live combat")

    lines = [
        "// Generated by platform/playstation/tools/generate-renderer-comparison-scene.py.",
        "// Do not edit. Source: tools/renderer_comparison/scene.json schema 2.",
        "#pragma once",
        f"inline constexpr int grandleon_psx_comparison_board_width = {board_width};",
        f"inline constexpr int grandleon_psx_comparison_board_height = {board_height};",
        f"inline constexpr int grandleon_psx_comparison_surrounding_width = {surrounding_width};",
        f"inline constexpr int grandleon_psx_comparison_surrounding_height = {surrounding_height};",
        f"inline constexpr int grandleon_psx_comparison_active_origin_x = {origin_x};",
        f"inline constexpr int grandleon_psx_comparison_active_origin_y = {origin_y};",
        array("grandleon_psx_comparison_board", board_kinds),
        array("grandleon_psx_comparison_surroundings", surrounding_kinds),
        array("grandleon_psx_comparison_unit_cells",
              [coordinate for unit in units for coordinate in unit["cell"]]),
        array("grandleon_psx_comparison_unit_initial_hp",
              [unit["initial_hp"] for unit in units]),
        array("grandleon_psx_comparison_unit_final_hp",
              [unit["final_hp"] for unit in units]),
        array("grandleon_psx_comparison_team_colours",
              [colour15(unit["team_colour"]) for unit in units],
              "unsigned short"),
        array("grandleon_psx_comparison_marker_colours",
              [colour15(active["selected"]["marker_colour"]),
               colour15(active["attack_target"]["marker_colour"])],
              "unsigned short"),
        array("grandleon_psx_comparison_safe_margin",
              composition["safe_margin_pixels"]),
        array("grandleon_psx_comparison_minimum_fill_milli",
              [fraction(value)
               for value in composition["minimum_scene_fill_normalized"]],
              "unsigned short"),
        array("grandleon_psx_comparison_close_layout_milli",
              [fraction(close["left_slot_x_normalized"]),
               fraction(close["right_slot_x_normalized"]),
               fraction(close["ground_y_normalized"]),
               fraction(close["horizon_y_normalized"])],
              "unsigned short"),
        array("grandleon_psx_comparison_active_tree_xz_milli",
              [fraction(value)
               for tree in active_trees for value in tree["offset_xz"]],
              "short"),
        array("grandleon_psx_comparison_active_tree_scale_milli",
              [fraction(tree["scale"]) for tree in active_trees],
              "unsigned short"),
        array("grandleon_psx_comparison_surrounding_tree_xz_milli",
              [fraction(value)
               for tree in surrounding_trees for value in tree["offset_xz"]],
              "short"),
        array("grandleon_psx_comparison_surrounding_tree_scale_milli",
              [fraction(tree["scale"]) for tree in surrounding_trees],
              "unsigned short"),
        array("grandleon_psx_comparison_cluster_height_milli",
              [fraction(value) for value in
               forest["connected_cluster_vertical_scale_range"]],
              "unsigned short"),
        "inline constexpr int grandleon_psx_comparison_duration_ms = 8150;",
        array("grandleon_psx_comparison_event_time_ms",
              [round(event["time_seconds"] * 1000)
               for event in scene["events"]], "unsigned short"),
        array("grandleon_psx_comparison_damage",
              [event["damage"] for event in scene["events"]]),
        f"inline constexpr unsigned int grandleon_psx_comparison_backdrop_seed = {seed}U;",
        f"inline constexpr int grandleon_psx_comparison_backdrop_variant = {descriptor['selected_variant']};",
        f"inline constexpr int grandleon_psx_comparison_floor_strip_top_milli = {fraction(foreground['floor_strip_top_normalized'])};",
    ]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content = generate(args.scene)
    if args.check:
        if not args.output.exists() or \
                args.output.read_text(encoding="utf-8") != content:
            raise SystemExit(f"stale renderer-comparison header: {args.output}")
        print(f"PS1 renderer-comparison header is current: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
