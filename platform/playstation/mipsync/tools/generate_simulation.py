#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate target-side EncounterDefinition data from the neutral manifest."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def ids(values: list[object]) -> str:
    return "{" + ",".join(f"{int(value)}ULL" for value in values) + "}"


def nums(values: list[object]) -> str:
    return "{" + ",".join(str(int(value)) for value in values) + "}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--one-player-manifest", type=Path)
    args = parser.parse_args()
    document = json.loads(args.manifest.read_text(encoding="utf-8"))
    if document.get("schema") != "grandleon.mipsync.scene.v2":
        raise ValueError("manifest is not a Mipsync scene v2 document")
    simulation = document.get("simulation")
    if not isinstance(simulation, dict):
        raise ValueError("manifest has no simulation definition")
    one_player = None
    one_player_ids: set[int] = set()
    if args.one_player_manifest is not None:
        one_player = json.loads(args.one_player_manifest.read_text(encoding="utf-8"))
        if one_player.get("schema") != "grandleon.mipsync.scene.v2":
            raise ValueError("one-player manifest is not a Mipsync scene v2 document")
        one_player_ids = {
            int(unit["id"]) for unit in one_player["simulation"]["units"]
        }
    lines = [
        "/* Generated from a real Grandleon package. Do not edit. */",
        '#include "grandleon_simulation.hpp"', "",
        "namespace sim = grandleon::simulation;", "",
        "sim::EncounterDefinition grandleon_generated_definition(unsigned local_players) {",
        "    sim::EncounterDefinition definition{};",
        f"    definition.width={int(document['width'])}; definition.height={int(document['height'])};",
        f"    definition.turn_order=static_cast<sim::TurnOrder>({int(simulation['turn_order'])});",
        f"    definition.random_seed={int(simulation['random_seed'])}ULL;",
        "    definition.terrain={" + ",".join(
            f"static_cast<sim::Terrain>({int(value)})"
            for value in document["passability"]
        ) + "};",
        f"    definition.movement_cost={nums(simulation['movement_cost'])};",
        "    definition.deployment_tiles={" + ",".join(
            f"sim::Position{{{int(value[0])},{int(value[1])}}}"
            for value in simulation["deployment_tiles"]
        ) + "};",
    ]
    for source in simulation["units"]:
        lines += [
            "    { sim::UnitDefinition value{};",
            f"      value.id={int(source['id'])}ULL; value.unit_type_id={int(source['unit_type_id'])}ULL;",
            f"      value.side=static_cast<sim::Side>({int(source['side'])}); value.position={{{int(source['position'][0])},{int(source['position'][1])}}};",
        ]
        for field in (
            "health", "strength", "power", "defense", "resistance", "skill",
            "luck", "evasion", "magic", "movement", "action_points", "speed",
            "minimum_reach", "maximum_reach", "crossings", "accuracy",
            "drop_chance", "reach_bonus", "arrival_round", "arrival_every",
            "arrival_times", "local_player",
        ):
            lines.append(f"      value.{field}={int(source[field])};")
        for field in ("acts_after_attacking", "endures"):
            lines.append(f"      value.{field}={'true' if int(source[field]) else 'false'};")
        for field in ("ability_ids", "weapon_ids", "item_ids"):
            lines.append(f"      value.{field}={ids(source[field])};")
        lines += [
            f"      value.item_counts={nums(source['item_counts'])};",
            f"      value.drop_item_id={int(source['drop_item_id'])}ULL; value.talk_record_id={int(source['talk_record_id'])}ULL;",
            ("      if (local_players > 1 || "
             f"{str(int(source['id']) in one_player_ids).lower()}) "
             "definition.units.push_back(value); }"),
        ]
    for source in simulation["abilities"]:
        lines += [
            "    { sim::AbilityDefinition value{};",
            f"      value.id={int(source['id'])}ULL; value.kind=static_cast<sim::AbilityKind>({int(source['kind'])}); value.damage_type=static_cast<sim::DamageType>({int(source['damage_type'])}); value.area=static_cast<sim::AreaShape>({int(source['area'])});",
            f"      value.power={int(source['power'])}; value.minimum_reach={int(source['minimum_reach'])}; value.maximum_reach={int(source['maximum_reach'])}; value.radius={int(source['radius'])}; value.accuracy={int(source['accuracy'])};",
            "      definition.abilities.push_back(value); }",
        ]
    for source in simulation["objectives"]:
        lines += [
            "    { sim::ObjectiveDefinition value{};",
            f"      value.id={int(source['id'])}ULL; value.kind=static_cast<sim::ObjectiveKind>({int(source['kind'])}); value.side=static_cast<sim::Side>({int(source['side'])}); value.target_unit_id={int(source['target_unit_id'])}ULL; value.round_count={int(source['round_count'])};",
            "      definition.objectives.push_back(value); }",
        ]
    for source in simulation["weapons"]:
        lines += [
            "    { sim::WeaponDefinition value{};",
            f"      value.id={int(source['id'])}ULL; value.power={int(source['power'])}; value.minimum_reach={int(source['minimum_reach'])}; value.maximum_reach={int(source['maximum_reach'])}; value.accuracy={int(source['accuracy'])}; value.weapon_type={int(source['weapon_type'])}ULL;",
            "      definition.weapons.push_back(value); }",
        ]
    for source in simulation["weapon_types"]:
        lines += [
            "    { sim::WeaponTypeDefinition value{};",
            f"      value.id={int(source['id'])}ULL; value.strong_against={ids(source['strong_against'])}; value.damage={int(source['damage'])}; value.accuracy={int(source['accuracy'])};",
            "      definition.weapon_types.push_back(value); }",
        ]
    for source in simulation["items"]:
        lines += [
            "    { sim::ItemDefinition value{};",
            f"      value.id={int(source['id'])}ULL; value.kind=static_cast<sim::ItemKind>({int(source['kind'])}); value.power={int(source['power'])};",
            "      definition.items.push_back(value); }",
        ]
    lines += ["    return definition;", "}", "",
        "const std::vector<grandleon_generated_behavior>& grandleon_generated_behaviors() {",
        "    static const std::vector<grandleon_generated_behavior> values = {",
    ]
    for source in simulation.get("behaviors", []):
        patrol = ",".join(
            f"sim::Position{{{int(value[0])},{int(value[1])}}}"
            for value in source["patrol"]
        )
        lines.append(
            "        {"
            f"{int(source['unit_id'])}ULL, "
            f"static_cast<grandleon::tactics::Behavior>({int(source['behavior'])}), "
            "{" + patrol + "}},"
        )
    lines += ["    };", "    return values;", "}", ""]
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "simulation_data.cpp").write_text("\n".join(lines), encoding="utf-8")
    header = f"""/* Generated from a real Grandleon package. Do not edit. */
#pragma once
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/tactics/policy.hpp>
#include <cstdint>
#include <vector>
struct grandleon_generated_behavior final {{
    grandleon::simulation::UnitId unit_id{{}};
    grandleon::tactics::Behavior behavior{{grandleon::tactics::Behavior::hold}};
    std::vector<grandleon::simulation::Position> patrol;
}};
grandleon::simulation::EncounterDefinition grandleon_generated_definition(unsigned local_players = 2);
const std::vector<grandleon_generated_behavior>& grandleon_generated_behaviors();
inline constexpr std::uint64_t grandleon_generated_canonical_hash(unsigned local_players) {{
    return local_players > 1 ? {int(document['canonical_hash'])}ULL : {int(one_player['canonical_hash']) if one_player else int(document['canonical_hash'])}ULL;
}}
inline constexpr unsigned char grandleon_generated_game_id[16] = {{{','.join(str(int(document.get('game_id', '0' * 32)[i:i+2], 16)) for i in range(0, 32, 2))}}};
inline constexpr unsigned grandleon_generated_content_revision = {int(document.get('content_revision', 0))}U;
inline constexpr unsigned grandleon_generated_local_players = {int(document.get('local_players', 2))}U;
inline constexpr const char* grandleon_generated_project_title = "{str(document.get('project_title', 'Grandleon')).replace('\\', '\\\\').replace('"', '\\"')}";
inline constexpr const char* grandleon_generated_prologue = {json.dumps(str(document.get('campaign', {}).get('prologue', 'The orchard bell has sounded.')))};
inline constexpr const char* grandleon_generated_aftermath = {json.dumps(str(document.get('campaign', {}).get('aftermath', 'The trail leads into Cedar Deepwood.')))};
inline constexpr const char* grandleon_generated_objective = {json.dumps(str(document.get('campaign', {}).get('objective', 'Defeat the enemy company.')))};
"""
    (args.output / "grandleon_simulation.hpp").write_text(header, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"generate_simulation.py: {error}") from error
