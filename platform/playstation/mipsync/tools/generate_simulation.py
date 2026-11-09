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


def cpp_string(value: object) -> str:
    text = str(value)
    for source, replacement in (
        ("\u2014", "-"), ("\u2013", "-"), ("\u2018", "'"),
        ("\u2019", "'"), ("\u201c", '"'), ("\u201d", '"'),
        ("\u2026", "..."),
    ):
        text = text.replace(source, replacement)
    return json.dumps(text, ensure_ascii=True)


def campaign_stage_names(campaign: dict[str, object]) -> dict[int, str]:
    encounters = sorted(
        int(node["id"])
        for node in campaign.get("nodes", [])
        if int(node.get("kind", 0)) == 1
    )
    if len(encounters) > 99:
        raise ValueError("PlayStation campaign supports at most 99 encounter executables")
    return {
        node_id: f"STG{index + 1:02d}.EXE"
        for index, node_id in enumerate(encounters)
    }


def emit_definition(
    function_name: str,
    document: dict[str, object],
    simulation: dict[str, object],
) -> list[str]:
    lines = [
        f"sim::EncounterDefinition {function_name}() {{",
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
            lines.append(
                f"      value.{field}={'true' if int(source[field]) else 'false'};"
            )
        for field in ("ability_ids", "weapon_ids", "item_ids"):
            lines.append(f"      value.{field}={ids(source[field])};")
        lines += [
            f"      value.item_counts={nums(source['item_counts'])};",
            f"      value.drop_item_id={int(source['drop_item_id'])}ULL; value.talk_record_id={int(source['talk_record_id'])}ULL;",
            "      definition.units.push_back(value); }",
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
    lines += ["    return definition;", "}", ""]
    return lines


def emit_behavior_values(
    function_name: str, simulation: dict[str, object]
) -> list[str]:
    lines = [
        f"const std::vector<grandleon_generated_behavior>& {function_name}() {{",
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
    return lines


def emit_localizations(document: dict[str, object]) -> list[str]:
    languages = document.get("languages", [])
    if not isinstance(languages, list) or not languages:
        languages = [{
            "code": "en",
            "project_title": document.get("project_title", "Grandleon"),
            "objective": document.get("campaign", {}).get(
                "objective", "Complete the objective."
            ),
            "nodes": document.get("campaign", {}).get("nodes", []),
        }]
    lines = [
        "namespace {",
        "struct generated_localized_node final {",
        "    std::uint64_t id; const char* name; const char* dialogue;",
        "};",
        "struct generated_language final {",
        "    const char* code; const char* project_title; const char* objective;",
        "    std::vector<generated_localized_node> nodes;",
        "};",
        "const std::vector<generated_language>& generated_languages() {",
        "    static const std::vector<generated_language> values = {",
    ]
    for language in languages:
        if not isinstance(language, dict):
            raise ValueError("language entry is not an object")
        nodes = language.get("nodes", [])
        if not isinstance(nodes, list):
            raise ValueError("language nodes are not an array")
        node_values = ",".join(
            "{" + ",".join((
                f"{int(node['id'])}ULL",
                cpp_string(node.get("name", "")),
                cpp_string(node.get("dialogue", "")),
            )) + "}"
            for node in nodes
        )
        lines.append(
            "        {" + ",".join((
                cpp_string(language.get("code", "en")),
                cpp_string(language.get("project_title", "Grandleon")),
                cpp_string(language.get("objective", "Complete the objective.")),
                "{" + node_values + "}",
            )) + "},"
        )
    lines += [
        "    };",
        "    return values;",
        "}",
        "const generated_language& generated_language(unsigned index) {",
        "    const auto& values = generated_languages();",
        "    return values[index < values.size() ? index : 0U];",
        "}",
        "const generated_localized_node* generated_localized_node_for(",
        "    unsigned language, std::uint64_t id",
        ") {",
        "    for (const auto& node : generated_language(language).nodes)",
        "        if (node.id == id) return &node;",
        "    return nullptr;",
        "}",
        "}",
        "",
        "unsigned grandleon_generated_language_count() {",
        "    return static_cast<unsigned>(generated_languages().size());",
        "}",
        "const char* grandleon_generated_language_code(unsigned language) {",
        "    return generated_language(language).code;",
        "}",
        "const char* grandleon_generated_project_title_for(unsigned language) {",
        "    return generated_language(language).project_title;",
        "}",
        "const char* grandleon_generated_objective_for(unsigned language) {",
        "    return generated_language(language).objective;",
        "}",
        "const char* grandleon_generated_campaign_node_name_for(",
        "    unsigned language, std::uint64_t id",
        ") {",
        "    const auto* node = generated_localized_node_for(language, id);",
        "    return node ? node->name : \"\";",
        "}",
        "const char* grandleon_generated_campaign_node_dialogue_for(",
        "    unsigned language, std::uint64_t id",
        ") {",
        "    const auto* node = generated_localized_node_for(language, id);",
        "    return node ? node->dialogue : \"\";",
        "}",
        "",
    ]
    return lines


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
    if args.one_player_manifest is not None:
        one_player = json.loads(args.one_player_manifest.read_text(encoding="utf-8"))
        if one_player.get("schema") != "grandleon.mipsync.scene.v2":
            raise ValueError("one-player manifest is not a Mipsync scene v2 document")
    campaign = document.get("campaign")
    if not isinstance(campaign, dict) or not isinstance(campaign.get("nodes"), list):
        raise ValueError("manifest has no package-authored campaign graph")
    stage_names = campaign_stage_names(campaign)
    current_node_id = int(campaign.get("current_node_id", 0))
    if current_node_id not in stage_names:
        raise ValueError("current encounter has no campaign node")
    entry_node = next(
        (node for node in campaign["nodes"]
         if int(node["id"]) == int(campaign["entry_node_id"])),
        None,
    )
    if not entry_node or not int(entry_node.get("has_fallback", 0)):
        raise ValueError("campaign entry must lead to its first encounter")
    boot_node_id = int(entry_node["fallback"])
    if boot_node_id not in stage_names:
        raise ValueError("campaign entry does not lead to an encounter")
    lines = [
        "/* Generated from a real Grandleon package. Do not edit. */",
        '#include "grandleon_simulation.hpp"', "",
        "namespace sim = grandleon::simulation;", "",
    ]
    lines += emit_definition(
        "grandleon_generated_two_player_definition", document, simulation
    )
    if one_player is not None:
        one_player_simulation = one_player.get("simulation")
        if not isinstance(one_player_simulation, dict):
            raise ValueError("one-player manifest has no simulation definition")
        lines += emit_definition(
            "grandleon_generated_one_player_definition",
            one_player,
            one_player_simulation,
        )
    else:
        one_player_simulation = simulation
    lines += [
        "sim::EncounterDefinition grandleon_generated_definition(unsigned local_players) {",
        "    return local_players > 1",
        "        ? grandleon_generated_two_player_definition()",
        "        : grandleon_generated_one_player_definition();",
        "}", "",
    ] if one_player is not None else [
        "sim::EncounterDefinition grandleon_generated_definition(unsigned) {",
        "    return grandleon_generated_two_player_definition();",
        "}", "",
    ]
    lines += emit_behavior_values(
        "grandleon_generated_two_player_behaviors", simulation
    )
    lines += emit_behavior_values(
        "grandleon_generated_one_player_behaviors", one_player_simulation
    )
    lines += [
        "const std::vector<grandleon_generated_behavior>& grandleon_generated_behaviors(unsigned local_players) {",
        "    return local_players > 1",
        "        ? grandleon_generated_two_player_behaviors()",
        "        : grandleon_generated_one_player_behaviors();",
        "}", "",
    ]
    lines += emit_localizations(document)
    lines += [
        "namespace camp = grandleon::campaign;", "",
        "namespace {",
        "camp::DefinitionRef generated_ref(grandleon::core::ContentCategory category, std::uint64_t stable_id) {",
        "    camp::DefinitionRef result{};",
        "    for (unsigned i = 0; i < 16; ++i) result.package_id[i] = grandleon_generated_game_id[i];",
        "    result.category = category; result.stable_id = stable_id; return result;",
        "}",
        "}", "",
        "camp::CampaignGraph grandleon_generated_campaign_graph() {",
        f"    const auto campaign = generated_ref(grandleon::core::ContentCategory::campaign, {int(campaign['id'])}ULL);",
        "    std::vector<camp::CampaignGraphNode> nodes;",
    ]
    for node in campaign["nodes"]:
        lines += [
            "    { camp::CampaignGraphNode node{};",
            f"      node.node=generated_ref(grandleon::core::ContentCategory::campaign_node, {int(node['id'])}ULL);",
            f"      node.terminal={'true' if int(node['kind']) == 2 else 'false'};",
            f"      node.has_fallback={'true' if int(node.get('has_fallback', 0)) else 'false'};",
        ]
        if int(node.get("has_fallback", 0)):
            lines.append(
                "      node.fallback=generated_ref("
                "grandleon::core::ContentCategory::campaign_node, "
                f"{int(node['fallback'])}ULL);"
            )
        for branch in node.get("branches", []):
            lines += [
                "      { camp::CampaignTransition transition{};",
                "        transition.target=generated_ref("
                "grandleon::core::ContentCategory::campaign_node, "
                f"{int(branch['target'])}ULL);",
                f"        transition.priority={int(branch['priority'])}U;",
                "        transition.combinator=static_cast<camp::ConditionCombinator>("
                f"{int(branch['combinator'])});",
            ]
            for predicate in branch.get("predicates", []):
                kind = int(predicate["kind"])
                if kind == 3:
                    value_type = 2 if int(predicate.get("value_type", 0)) == 2 else 1
                    lines.append(
                        "        transition.predicates.push_back(camp::world_flag_equals("
                        "generated_ref(grandleon::core::ContentCategory::world_flag, "
                        f"{int(predicate['subject'])}ULL), "
                        "{static_cast<camp::WorldValueType>("
                        f"{value_type}), {int(predicate.get('value', 0))}LL}}));"
                    )
                elif kind == 4:
                    quantity = int(predicate.get("value", 0))
                    if quantity < 1 or quantity > 0xFFFFFFFF:
                        quantity = 0
                    lines.append(
                        "        transition.predicates.push_back(camp::inventory_at_least("
                        "camp::PersistentEntityId{}, generated_ref("
                        "grandleon::core::ContentCategory::item, "
                        f"{int(predicate['subject'])}ULL), {quantity}U));"
                    )
                else:
                    result = 2 if int(predicate.get("result", 1)) == 2 else 1
                    lines.append(
                        "        transition.predicates.push_back(camp::objective_result_is("
                        "generated_ref(grandleon::core::ContentCategory::objective, "
                        f"{int(predicate['subject'])}ULL), "
                        f"static_cast<camp::ObjectiveOutcome>({result})));"
                    )
            lines += ["        node.transitions.push_back(std::move(transition)); }",]
        lines += ["      nodes.push_back(std::move(node)); }",]
    lines += [
        "    return camp::make_campaign_graph(campaign,",
        "        generated_ref(grandleon::core::ContentCategory::campaign_node, "
        f"{int(campaign['entry_node_id'])}ULL), std::move(nodes));",
        "}", "",
        "const std::vector<grandleon_generated_campaign_node>& grandleon_generated_campaign_nodes() {",
        "    static const std::vector<grandleon_generated_campaign_node> values = {",
    ]
    for node in campaign["nodes"]:
        stage_name = stage_names.get(int(node["id"]), "")
        disc_path = "cdrom:\\" + stage_name + ";1" if stage_name else ""
        lines.append(
            "        {"
            f"{int(node['id'])}ULL, {int(node.get('encounter_id', 0))}ULL, "
            f"{int(node['kind'])}, {cpp_string(node.get('name', ''))}, "
            f"{cpp_string(node.get('dialogue', ''))}, {cpp_string(disc_path)}"
            "},"
        )
    lines += [
        "    };", "    return values;", "}", "",
        "const grandleon_generated_campaign_node* grandleon_generated_campaign_node_for(std::uint64_t id) {",
        "    for (const auto& node : grandleon_generated_campaign_nodes()) if (node.id == id) return &node;",
        "    return nullptr;", "}", "",
        "const std::vector<std::uint64_t>& grandleon_generated_objective_ids() {",
        "    static const std::vector<std::uint64_t> values = "
        + ids([source["id"] for source in simulation["objectives"]]) + ";",
        "    return values;", "}", "",
    ]
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "simulation_data.cpp").write_text("\n".join(lines), encoding="utf-8")
    header = f"""/* Generated from a real Grandleon package. Do not edit. */
#pragma once
#include <grandleon/campaign/graph.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/tactics/policy.hpp>
#include <cstdint>
#include <vector>
struct grandleon_generated_behavior final {{
    grandleon::simulation::UnitId unit_id{{}};
    grandleon::tactics::Behavior behavior{{grandleon::tactics::Behavior::hold}};
    std::vector<grandleon::simulation::Position> patrol;
}};
struct grandleon_generated_campaign_node final {{
    std::uint64_t id{{}};
    std::uint64_t encounter_id{{}};
    std::uint8_t kind{{}};
    const char* name{{}};
    const char* dialogue{{}};
    const char* disc_path{{}};
}};
grandleon::simulation::EncounterDefinition grandleon_generated_definition(unsigned local_players = 2);
const std::vector<grandleon_generated_behavior>& grandleon_generated_behaviors(unsigned local_players = 2);
grandleon::campaign::CampaignGraph grandleon_generated_campaign_graph();
const std::vector<grandleon_generated_campaign_node>& grandleon_generated_campaign_nodes();
const grandleon_generated_campaign_node* grandleon_generated_campaign_node_for(std::uint64_t id);
const std::vector<std::uint64_t>& grandleon_generated_objective_ids();
unsigned grandleon_generated_language_count();
const char* grandleon_generated_language_code(unsigned language);
const char* grandleon_generated_project_title_for(unsigned language);
const char* grandleon_generated_objective_for(unsigned language);
const char* grandleon_generated_campaign_node_name_for(unsigned language, std::uint64_t id);
const char* grandleon_generated_campaign_node_dialogue_for(unsigned language, std::uint64_t id);
inline constexpr std::uint64_t grandleon_generated_canonical_hash(unsigned local_players) {{
    return local_players > 1 ? {int(document['canonical_hash'])}ULL : {int(one_player['canonical_hash']) if one_player else int(document['canonical_hash'])}ULL;
}}
inline constexpr unsigned char grandleon_generated_game_id[16] = {{{','.join(str(int(document.get('game_id', '0' * 32)[i:i+2], 16)) for i in range(0, 32, 2))}}};
inline constexpr unsigned grandleon_generated_content_revision = {int(document.get('content_revision', 0))}U;
inline constexpr unsigned grandleon_generated_local_players = {int(document.get('local_players', 2))}U;
inline constexpr const char* grandleon_generated_project_title = {cpp_string(document.get('project_title', 'Grandleon'))};
inline constexpr std::uint64_t grandleon_generated_current_node_id = {current_node_id}ULL;
inline constexpr std::uint64_t grandleon_generated_boot_node_id = {boot_node_id}ULL;
inline constexpr std::uint64_t grandleon_generated_current_encounter_id = {int(document['encounter_id'])}ULL;
inline constexpr const char* grandleon_generated_objective = {cpp_string(campaign.get('objective', 'Complete the objective.'))};
inline constexpr const char* grandleon_generated_stage_filename = {cpp_string(stage_names[current_node_id])};
"""
    (args.output / "grandleon_simulation.hpp").write_text(header, encoding="utf-8")
    (args.output / "stage-filename.txt").write_text(
        stage_names[current_node_id] + "\n", encoding="ascii"
    )
    (args.output / "stage-is-boot.txt").write_text(
        ("1\n" if current_node_id == boot_node_id else "0\n"), encoding="ascii"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"generate_simulation.py: {error}") from error
