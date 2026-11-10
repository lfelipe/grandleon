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


def validate_moments(
    document: dict[str, object], label: str,
) -> None:
    moments = document.get("moments", [])
    if not isinstance(moments, list):
        raise ValueError(f"{label} moments are not an array")
    simulation = document.get("simulation")
    if not isinstance(simulation, dict):
        raise ValueError(f"{label} manifest has no simulation definition")
    units = simulation.get("units", [])
    if not isinstance(units, list):
        raise ValueError(f"{label} simulation units are not an array")
    unit_ids = {
        int(unit["id"])
        for unit in units
        if isinstance(unit, dict)
    }
    dialogue_ids: set[int] = set()
    moment_ids: set[int] = set()
    for moment in moments:
        if not isinstance(moment, dict):
            raise ValueError(f"{label} moment entry is not an object")
        moment_id = int(moment["id"])
        trigger = int(moment["trigger"])
        placement_id = int(moment["placement_id"])
        dialogue_id = int(moment["dialogue_id"])
        if moment_id == 0 or dialogue_id == 0:
            raise ValueError(f"{label} moment identities must be nonzero")
        if moment_id in moment_ids:
            raise ValueError(f"{label} moment identity is duplicated: {moment_id}")
        moment_ids.add(moment_id)
        if trigger not in (1, 2, 3):
            raise ValueError(f"{label} moment has unknown trigger: {trigger}")
        if trigger == 1 and placement_id != 0:
            raise ValueError(f"{label} stage-open moment names a unit")
        if trigger != 1 and placement_id not in unit_ids:
            raise ValueError(
                f"{label} event moment names an unknown unit: {placement_id}"
            )
        dialogue_ids.add(dialogue_id)

    languages = document.get("languages", [])
    if not isinstance(languages, list) or not languages:
        raise ValueError(f"{label} manifest has no localized languages")
    for language in languages:
        if not isinstance(language, dict):
            raise ValueError(f"{label} language entry is not an object")
        code = str(language.get("code", ""))
        localized = language.get("moments", [])
        if not isinstance(localized, list):
            raise ValueError(f"{label} language moments are not an array")
        localized_ids: set[int] = set()
        for dialogue in localized:
            if not isinstance(dialogue, dict):
                raise ValueError(f"{label} localized moment is not an object")
            dialogue_id = int(dialogue["dialogue_id"])
            lines = dialogue.get("lines", [])
            if not isinstance(lines, list) or not lines:
                raise ValueError(
                    f"{label} {code} dialogue has no lines: {dialogue_id}"
                )
            localized_ids.add(dialogue_id)
        missing = dialogue_ids - localized_ids
        if missing:
            raise ValueError(
                f"{label} {code} is missing moment dialogue: {min(missing)}"
            )


def emit_moment_values(
    prefix: str, document: dict[str, object]
) -> list[str]:
    moments = document.get("moments", [])
    if not isinstance(moments, list):
        raise ValueError("moments are not an array")
    lines = [
        "const std::vector<grandleon_generated_moment>& "
        f"grandleon_generated_{prefix}_moments() {{",
        "    static const std::vector<grandleon_generated_moment> values = {",
    ]
    for moment in moments:
        if not isinstance(moment, dict):
            raise ValueError("moment entry is not an object")
        lines.append(
            "        {"
            f"{int(moment['id'])}ULL, "
            f"{int(moment['trigger'])}U, "
            f"{int(moment['placement_id'])}ULL, "
            f"{int(moment['dialogue_id'])}ULL"
            "},"
        )
    lines += ["    };", "    return values;", "}", ""]
    return lines


def dialogue_span_declaration() -> str:
    return """struct grandleon_generated_dialogue_span final {
    const grandleon_generated_dialogue_line* data{};
    unsigned count{};
    bool empty() const { return count == 0; }
    unsigned size() const { return count; }
    const grandleon_generated_dialogue_line& front() const { return *data; }
    const grandleon_generated_dialogue_line& operator[](unsigned index) const { return data[index]; }
    const grandleon_generated_dialogue_line* begin() const { return data; }
    const grandleon_generated_dialogue_line* end() const { return count ? data + count : data; }
};"""


def node_dialogue_lines(node: dict[str, object]) -> list[dict[str, object]]:
    structured = node.get("dialogue_lines")
    if structured is not None:
        if not isinstance(structured, list) or any(not isinstance(line, dict) for line in structured):
            raise ValueError("node dialogue_lines must be an array of speaker/text objects")
        return structured
    text = str(node.get("dialogue", ""))
    return [{"speaker": "", "text": text}] if text else []


def emit_dialogue_lines(values: list[dict[str, object]]) -> str:
    return "{" + ",".join(
        "{" + cpp_string(line.get("speaker", "")) + "," + cpp_string(line.get("text", "")) +
        "," + str(int(line.get("unit_type_id", 0))) + "ULL}"
        for line in values
    ) + "}"


def emit_localizations(
    document: dict[str, object], additional: dict[str, object] | None = None,
) -> list[str]:
    languages = document.get("languages", [])
    if not isinstance(languages, list) or not languages:
        languages = [{
            "code": "en",
            "project_title": document.get("project_title", "Grandleon"),
            "objective": document.get("campaign", {}).get(
                "objective", "Complete the objective."
            ),
            "members": document.get("campaign", {}).get("members", []),
            "nodes": document.get("campaign", {}).get("nodes", []),
        }]
    additional_by_code: dict[str, dict[str, object]] = {}
    if additional is not None:
        additional_languages = additional.get("languages", [])
        if not isinstance(additional_languages, list):
            raise ValueError("additional languages are not an array")
        for language in additional_languages:
            if not isinstance(language, dict):
                raise ValueError("additional language entry is not an object")
            additional_by_code[str(language.get("code", "en"))] = language
        existing_codes = {
            str(language.get("code", "en"))
            for language in languages
            if isinstance(language, dict)
        }
        languages = list(languages) + [
            language for code, language in additional_by_code.items()
            if code not in existing_codes
        ]
    lines = [
        "namespace {",
        "struct generated_localized_member final {",
        "    std::uint64_t persistent_id; const char* name;",
        "};",
        "struct generated_localized_node final {",
        "    std::uint64_t id; const char* name; const char* dialogue;",
        "    grandleon_generated_dialogue_span lines;",
        "};",
        "struct generated_localized_dialogue final {",
        "    std::uint64_t id; std::vector<grandleon_generated_dialogue_line> lines;",
        "};",
        "struct generated_language final {",
        "    const char* code; const char* project_title; const char* objective;",
        "    std::vector<generated_localized_member> members;",
        "    std::vector<generated_localized_node> nodes;",
        "    std::vector<generated_localized_dialogue> moments;",
        "};",
        "const std::vector<generated_language>& generated_languages() {",
    ]
    language_values = []
    for language_index, language in enumerate(languages):
        if not isinstance(language, dict):
            raise ValueError("language entry is not an object")
        nodes = language.get("nodes", [])
        if not isinstance(nodes, list):
            raise ValueError("language nodes are not an array")
        members = language.get("members", [])
        if not isinstance(members, list):
            raise ValueError("language members are not an array")
        moments = language.get("moments", [])
        if not isinstance(moments, list):
            raise ValueError("language moments are not an array")
        moments = list(moments)
        additional_language = additional_by_code.get(
            str(language.get("code", "en"))
        )
        if additional_language is not None and additional_language is not language:
            additional_moments = additional_language.get("moments", [])
            if not isinstance(additional_moments, list):
                raise ValueError("additional language moments are not an array")
            existing_dialogues = {
                int(moment["dialogue_id"])
                for moment in moments
                if isinstance(moment, dict)
            }
            moments.extend(
                moment for moment in additional_moments
                if isinstance(moment, dict) and
                int(moment["dialogue_id"]) not in existing_dialogues
            )
        member_values = ",".join(
            "{" + ",".join((
                f"{int(member['persistent_id'])}ULL",
                cpp_string(member.get("name", "")),
            )) + "}"
            for member in members
        )
        node_rows = []
        for node_index, node in enumerate(nodes):
            values = node_dialogue_lines(node)
            span = "{nullptr,0}"
            if values:
                symbol = f"story_lines_{language_index}_{node_index}"
                lines.append("    static const grandleon_generated_dialogue_line " + symbol + "[] = " + emit_dialogue_lines(values) + ";")
                span = "{" + symbol + "," + str(len(values)) + "}"
            node_rows.append("{" + ",".join((
                f"{int(node['id'])}ULL", cpp_string(node.get("name", "")),
                cpp_string("" if "dialogue_lines" in node else node.get("dialogue", "")), span,
            )) + "}")
        node_values = ",".join(node_rows)
        moment_values = ",".join(
            "{" + ",".join((
                f"{int(moment['dialogue_id'])}ULL",
                "{" + ",".join(
                    "{" + ",".join((
                        cpp_string(line.get("speaker", "")),
                        cpp_string(line.get("text", "")),
                        str(int(line.get("unit_type_id", 0))) + "ULL",
                    )) + "}"
                    for line in moment.get("lines", [])
                ) + "}",
            )) + "}"
            for moment in moments
        )
        language_values.append(
            "        {" + ",".join((
                cpp_string(language.get("code", "en")),
                cpp_string(language.get("project_title", "Grandleon")),
                cpp_string(language.get("objective", "Complete the objective.")),
                "{" + member_values + "}",
                "{" + node_values + "}",
                "{" + moment_values + "}",
            )) + "},"
        )
    lines += ["    static const std::vector<generated_language> values = {"] + language_values
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
        "const generated_localized_member* generated_localized_member_for(",
        "    unsigned language, std::uint64_t persistent_id",
        ") {",
        "    for (const auto& member : generated_language(language).members)",
        "        if (member.persistent_id == persistent_id) return &member;",
        "    return nullptr;",
        "}",
        "const generated_localized_dialogue* generated_localized_moment_for(",
        "    unsigned language, std::uint64_t id",
        ") {",
        "    for (const auto& dialogue : generated_language(language).moments)",
        "        if (dialogue.id == id) return &dialogue;",
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
        "    return node ? (node->lines.empty() ? node->dialogue : node->lines.front().text) : \"\";",
        "}",
        "grandleon_generated_dialogue_span",
        "grandleon_generated_campaign_node_lines(unsigned language, std::uint64_t id) {",
        "    const auto* node = generated_localized_node_for(language, id);",
        "    return node ? node->lines : grandleon_generated_dialogue_span{};",
        "}",
        "const char* grandleon_generated_campaign_member_name_for(",
        "    unsigned language, std::uint64_t persistent_id",
        ") {",
        "    const auto* member = generated_localized_member_for(",
        "        language, persistent_id",
        "    );",
        "    return member ? member->name : \"\";",
        "}",
        "const std::vector<grandleon_generated_dialogue_line>& ",
        "grandleon_generated_moment_lines(unsigned language, std::uint64_t id) {",
        "    static const std::vector<grandleon_generated_dialogue_line> empty;",
        "    const auto* dialogue = generated_localized_moment_for(language, id);",
        "    return dialogue ? dialogue->lines : empty;",
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
    validate_moments(document, "two-player")
    one_player = None
    if args.one_player_manifest is not None:
        one_player = json.loads(args.one_player_manifest.read_text(encoding="utf-8"))
        if one_player.get("schema") != "grandleon.mipsync.scene.v2":
            raise ValueError("one-player manifest is not a Mipsync scene v2 document")
        validate_moments(one_player, "one-player")
    campaign = document.get("campaign")
    if not isinstance(campaign, dict) or not isinstance(campaign.get("nodes"), list):
        raise ValueError("manifest has no package-authored campaign graph")
    player_mode = int(campaign.get("player_mode", 0))
    if player_mode not in (1, 2, 3):
        raise ValueError("campaign has no valid local-player policy")
    stage_names = campaign_stage_names(campaign)
    current_node_id = int(campaign.get("current_node_id", 0))
    if current_node_id not in stage_names:
        raise ValueError("current encounter has no campaign node")
    entry_node = next(
        (node for node in campaign["nodes"]
         if int(node["id"]) == int(campaign["entry_node_id"])),
        None,
    )
    if not entry_node:
        raise ValueError("campaign entry is missing")
    if int(entry_node.get("kind", 0)) == 1:
        boot_node_id = int(entry_node["id"])
    elif int(entry_node.get("has_fallback", 0)):
        boot_node_id = int(entry_node["fallback"])
    else:
        raise ValueError("campaign entry must be an encounter or lead to one")
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
    lines += emit_moment_values("two_player", document)
    lines += emit_moment_values(
        "one_player", one_player if one_player is not None else document
    )
    lines += [
        "const std::vector<grandleon_generated_moment>& grandleon_generated_moments(unsigned local_players) {",
        "    return local_players > 1 ? grandleon_generated_two_player_moments()",
        "        : grandleon_generated_one_player_moments();",
        "}", "",
    ]
    lines += emit_localizations(document, one_player)
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
        "const std::vector<grandleon_generated_campaign_member>& grandleon_generated_campaign_members() {",
        "    static const std::vector<grandleon_generated_campaign_member> values = {",
    ]
    for member in campaign.get("members", []):
        lines.append(
            "        {"
            f"{int(member['persistent_id'])}ULL, "
            f"{int(member['source_id'])}ULL, "
            f"{int(member['unit_type_id'])}ULL, "
            f"{int(member.get('join_node_id', 0))}ULL, "
            f"{cpp_string(member.get('name', ''))}, "
            f"{'true' if int(member.get('player_nameable', 0)) else 'false'}"
            "},"
        )
    lines += [
        "    };", "    return values;", "}", "",
        "const grandleon_generated_campaign_member* grandleon_generated_campaign_member_for(std::uint64_t persistent_id) {",
        "    for (const auto& member : grandleon_generated_campaign_members())",
        "        if (member.persistent_id == persistent_id) return &member;",
        "    return nullptr;", "}", "",
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
    presentation_units: dict[int, dict[str, object]] = {}
    for manifest in (document, one_player or {}):
        rendered = {int(unit["id"]): unit for unit in manifest.get("units", [])}
        for unit in manifest.get("simulation", {}).get("units", []):
            value = dict(unit)
            value["name"] = rendered.get(int(unit["id"]), {}).get("name", unit.get("name", ""))
            presentation_units.setdefault(int(unit["id"]), value)
    lines += ["const char* grandleon_generated_unit_name(std::uint64_t id) {", "    switch (id) {"]
    for identifier, unit in presentation_units.items():
        lines.append(f"    case {identifier}ULL: return {cpp_string(unit['name'])};")
    lines += ["    default: return \"\";", "    }", "}",
              "std::uint64_t grandleon_generated_unit_type(std::uint64_t id) {", "    switch (id) {"]
    for identifier, unit in presentation_units.items():
        lines.append(f"    case {identifier}ULL: return {int(unit['unit_type_id'])}ULL;")
    lines += ["    default: return 0;", "    }", "}"]
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
struct grandleon_generated_campaign_member final {{
    std::uint64_t persistent_id{{}};
    std::uint64_t source_id{{}};
    std::uint64_t unit_type_id{{}};
    std::uint64_t join_node_id{{}};
    const char* authored_name{{}};
    bool player_nameable{{}};
}};
struct grandleon_generated_moment final {{
    std::uint64_t id{{}};
    std::uint8_t trigger{{}};
    std::uint64_t placement_id{{}};
    std::uint64_t dialogue_id{{}};
}};
struct grandleon_generated_dialogue_line final {{
    const char* speaker{{}};
    const char* text{{}};
    std::uint64_t unit_type_id{{}};
}};
{dialogue_span_declaration()}
grandleon::simulation::EncounterDefinition grandleon_generated_definition(unsigned local_players = 2);
const std::vector<grandleon_generated_behavior>& grandleon_generated_behaviors(unsigned local_players = 2);
const std::vector<grandleon_generated_moment>& grandleon_generated_moments(unsigned local_players = 2);
const std::vector<grandleon_generated_dialogue_line>& grandleon_generated_moment_lines(unsigned language, std::uint64_t id);
grandleon_generated_dialogue_span grandleon_generated_campaign_node_lines(unsigned language, std::uint64_t id);
const char* grandleon_generated_unit_name(std::uint64_t id);
std::uint64_t grandleon_generated_unit_type(std::uint64_t id);
grandleon::campaign::CampaignGraph grandleon_generated_campaign_graph();
const std::vector<grandleon_generated_campaign_member>& grandleon_generated_campaign_members();
const grandleon_generated_campaign_member* grandleon_generated_campaign_member_for(std::uint64_t persistent_id);
const std::vector<grandleon_generated_campaign_node>& grandleon_generated_campaign_nodes();
const grandleon_generated_campaign_node* grandleon_generated_campaign_node_for(std::uint64_t id);
const std::vector<std::uint64_t>& grandleon_generated_objective_ids();
unsigned grandleon_generated_language_count();
const char* grandleon_generated_language_code(unsigned language);
const char* grandleon_generated_project_title_for(unsigned language);
const char* grandleon_generated_objective_for(unsigned language);
const char* grandleon_generated_campaign_node_name_for(unsigned language, std::uint64_t id);
const char* grandleon_generated_campaign_node_dialogue_for(unsigned language, std::uint64_t id);
const char* grandleon_generated_campaign_member_name_for(unsigned language, std::uint64_t persistent_id);
inline constexpr std::uint64_t grandleon_generated_canonical_hash(unsigned local_players) {{
    return local_players > 1 ? {int(document['canonical_hash'])}ULL : {int(one_player['canonical_hash']) if one_player else int(document['canonical_hash'])}ULL;
}}
inline constexpr unsigned char grandleon_generated_game_id[16] = {{{','.join(str(int(document.get('game_id', '0' * 32)[i:i+2], 16)) for i in range(0, 32, 2))}}};
inline constexpr unsigned grandleon_generated_content_revision = {int(document.get('content_revision', 0))}U;
inline constexpr unsigned grandleon_generated_local_players = {int(document.get('local_players', 2))}U;
inline constexpr unsigned grandleon_generated_player_mode = {player_mode}U;
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
