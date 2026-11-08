// SPDX-License-Identifier: MIT
#include <grandleon/godot/project_compiler.hpp>

#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_runtime/target_budget.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace grandleon::godot_frontend {
namespace {

namespace gc = game_content;
namespace gd = ::godot;
namespace pr = ::grandleon::package_runtime;

gd::String text(std::string_view value) {
    return gd::String::utf8(value.data(), static_cast<int64_t>(value.size()));
}

std::string narrow(const gd::String& value) {
    const gd::CharString utf8 = value.utf8();
    return std::string(utf8.get_data(), static_cast<std::size_t>(utf8.length()));
}

std::string_view diagnostic_screen(std::string_view path) noexcept {
    if (path.find("unit_types") != std::string_view::npos ||
        path.find("unitTypes") != std::string_view::npos ||
        path.find("classes") != std::string_view::npos ||
        path.find("factions") != std::string_view::npos ||
        path.find("roster") != std::string_view::npos) {
        return "army";
    }
    if (path.find("weapons") != std::string_view::npos ||
        path.find("weapon_types") != std::string_view::npos ||
        path.find("weaponTypes") != std::string_view::npos ||
        path.find("items") != std::string_view::npos ||
        path.find("item_types") != std::string_view::npos ||
        path.find("itemTypes") != std::string_view::npos ||
        path.find("abilities") != std::string_view::npos) {
        return "gear";
    }
    if (path.find("dialogues") != std::string_view::npos) return "story";
    if (path.find("encounters") != std::string_view::npos ||
        path.find("maps") != std::string_view::npos ||
        path.find("objectives") != std::string_view::npos ||
        path.find("placements") != std::string_view::npos ||
        path.find("deployment") != std::string_view::npos) {
        return "map";
    }
    if (path.find("campaigns") != std::string_view::npos ||
        path.find("transitions") != std::string_view::npos) {
        return "campaign";
    }
    return "project";
}

bool diagnostic_owner(std::string_view path, std::uint64_t& owner) noexcept {
    const std::size_t opening = path.find('[');
    if (opening == std::string_view::npos) return false;
    const std::size_t closing = path.find(']', opening + 1U);
    if (closing == std::string_view::npos || closing == opening + 1U) {
        return false;
    }
    const std::string_view encoded =
        path.substr(opening + 1U, closing - opening - 1U);
    const char* const begin = encoded.data();
    const char* const end = begin + encoded.size();
    const auto parsed = std::from_chars(begin, end, owner);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::string_view diagnostic_category(std::string_view path) noexcept {
    if (path.size() >= 2U && path[0] == '$' && path[1] == '.') {
        path.remove_prefix(2U);
    }
    const std::size_t end = path.find_first_of("[.");
    return path.substr(0, end);
}

std::string_view diagnostic_field(std::string_view path) noexcept {
    const std::size_t dot = path.rfind('.');
    std::string_view field =
        dot == std::string_view::npos ? path : path.substr(dot + 1U);
    const std::size_t bracket = field.find('[');
    if (bracket != std::string_view::npos) field = field.substr(0, bracket);
    return field;
}

bool diagnostic_index_after(
    std::string_view path,
    std::string_view marker,
    std::size_t& index
) noexcept {
    const std::size_t marker_at = path.find(marker);
    if (marker_at == std::string_view::npos) return false;
    const std::size_t begin = marker_at + marker.size();
    const std::size_t end = path.find(']', begin);
    if (end == std::string_view::npos || end == begin) return false;
    const std::string_view encoded = path.substr(begin, end - begin);
    const char* const first = encoded.data();
    const char* const last = first + encoded.size();
    const auto parsed = std::from_chars(first, last, index);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

void add_path_focus(gd::Dictionary& result, std::string_view path) {
    const std::string_view category = diagnostic_category(path);
    const std::string_view field = diagnostic_field(path);
    if (!category.empty() && category != "$") {
        result["category"] = text(category);
    }
    if (!field.empty() && field != category && field != "$") {
        result["field"] = text(field);
    }

    std::size_t line = 0;
    if (diagnostic_index_after(path, ".lines[", line)) {
        result["line_index"] = static_cast<std::int64_t>(line);
    }
}

template <typename Definition>
std::string_view definition_name(
    const std::vector<Definition>& definitions,
    std::uint64_t owner
) noexcept {
    for (const Definition& definition : definitions) {
        if (definition.id == owner) return definition.name;
    }
    return {};
}

std::string_view diagnostic_subject(
    const gc::GameSource& source,
    std::string_view path
) noexcept {
    std::uint64_t owner = 0;
    if (!diagnostic_owner(path, owner)) return {};
    const std::size_t opening = path.find('[');
    const std::string_view category = path.substr(0, opening);
    if (category == "unit_types") {
        return definition_name(source.unit_types, owner);
    }
    if (category == "classes") return definition_name(source.classes, owner);
    if (category == "factions") {
        return definition_name(source.factions, owner);
    }
    if (category == "weapons") return definition_name(source.weapons, owner);
    if (category == "weapon_types") {
        return definition_name(source.weapon_types, owner);
    }
    if (category == "items") return definition_name(source.items, owner);
    if (category == "item_types") {
        return definition_name(source.item_types, owner);
    }
    if (category == "abilities") {
        return definition_name(source.abilities, owner);
    }
    if (category == "dialogues") {
        return definition_name(source.dialogues, owner);
    }
    if (category == "encounters") {
        return definition_name(source.encounters, owner);
    }
    if (category == "maps") return definition_name(source.maps, owner);
    if (category == "objectives") {
        return definition_name(source.objectives, owner);
    }
    if (category == "campaigns") {
        return definition_name(source.campaigns, owner);
    }
    return {};
}

template <typename Definition>
const Definition* definition_with_id(
    const std::vector<Definition>& definitions,
    std::uint64_t id
) noexcept {
    for (const Definition& definition : definitions) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

std::string_view placement_name(
    const gc::GameSource& source,
    const gc::Placement& placement
) noexcept {
    if (!placement.name.empty()) return placement.name;
    if (placement.member_id != 0) {
        for (const gc::Campaign& campaign : source.campaigns) {
            for (const gc::CampaignMember& member : campaign.roster) {
                if (member.id == placement.member_id) return member.name;
            }
        }
    }
    const gc::UnitType* const unit_type =
        definition_with_id(source.unit_types, placement.unit_type_id);
    return unit_type == nullptr ? std::string_view{} : unit_type->name;
}

void add_compile_focus(
    gd::Dictionary& result,
    const gc::Diagnostic& diagnostic,
    const gc::GameSource& source
) {
    add_path_focus(result, diagnostic.path);
    std::uint64_t owner = 0;
    if (!diagnostic_owner(diagnostic.path, owner)) return;
    result["owner_id"] = text(std::to_string(owner));

    const std::string_view category = diagnostic_category(diagnostic.path);
    if (category == "maps") {
        const gc::Map* const map = definition_with_id(source.maps, owner);
        std::size_t cell = 0;
        if (map != nullptr && map->width != 0 &&
            diagnostic.path.find(".surroundings.terrain[") ==
                std::string::npos &&
            diagnostic_index_after(diagnostic.path, ".terrain[", cell) &&
            cell < static_cast<std::size_t>(map->width) * map->height) {
            result["cell_index"] = static_cast<std::int64_t>(cell);
            result["x"] = static_cast<std::int64_t>(cell % map->width);
            result["y"] = static_cast<std::int64_t>(cell / map->width);
        }
        return;
    }
    if (category != "encounters") return;

    const gc::Encounter* const encounter =
        definition_with_id(source.encounters, owner);
    if (encounter == nullptr) return;
    result["map_id"] = text(std::to_string(encounter->map_id));
    if (diagnostic.path.find(".placements") == std::string::npos) return;

    const gc::Placement* placement = nullptr;
    for (const gc::Placement& candidate : encounter->placements) {
        if (candidate.id == diagnostic.related_id ||
            candidate.source_key_id == diagnostic.related_id ||
            candidate.member_id == diagnostic.related_id ||
            candidate.unit_type_id == diagnostic.related_id) {
            placement = &candidate;
            break;
        }
    }
    if (placement == nullptr) return;
    result["object_kind"] = "placement";
    result["object_id"] = text(std::to_string(placement->id));
    const std::string_view name = placement_name(source, *placement);
    if (!name.empty()) result["object"] = text(name);
    result["x"] = static_cast<std::int64_t>(placement->x);
    result["y"] = static_cast<std::int64_t>(placement->y);
}

gd::Dictionary source_diagnostic(const gc::SourceDiagnostic& diagnostic) {
    gd::Dictionary result;
    result["phase"] = "source";
    result["code"] = text(gc::source_diagnostic_name(diagnostic.code));
    result["path"] = text(diagnostic.path);
    result["detail"] = text(diagnostic.detail);
    result["screen"] = text(diagnostic_screen(diagnostic.path));
    add_path_focus(result, diagnostic.path);
    return result;
}

gd::Dictionary compile_diagnostic(
    const gc::Diagnostic& diagnostic,
    const gc::GameSource& source
) {
    gd::Dictionary result;
    result["phase"] = "compile";
    result["code"] = text(gc::diagnostic_name(diagnostic.code));
    result["path"] = text(diagnostic.path);
    result["screen"] = text(diagnostic_screen(diagnostic.path));
    add_compile_focus(result, diagnostic, source);
    const std::string_view subject = diagnostic_subject(source, diagnostic.path);
    if (!subject.empty()) result["subject"] = text(subject);
    if (diagnostic.related_id != 0) {
        // Stable ids are unsigned 64-bit values. Godot integers are signed,
        // so preserve the complete value as decimal text for diagnostics.
        result["related_id"] = text(std::to_string(diagnostic.related_id));
    }
    return result;
}

std::uint64_t fielded_units(const gc::Encounter& encounter) noexcept {
    std::uint64_t total = 0;
    for (const gc::Placement& placement : encounter.placements) {
        const bool recurring =
            placement.arrival_every != 0U && placement.arrival_times != 0U;
        total += recurring ? placement.arrival_times : 1U;
    }
    return total;
}

gd::Dictionary target_budget(
    const gc::GameSource& source,
    std::string_view id,
    std::string_view label,
    std::uint32_t units_per_encounter
) {
    gd::Dictionary target;
    target["id"] = text(id);
    target["label"] = text(label);
    target["limited"] = units_per_encounter != 0U;
    target["units_per_encounter"] =
        static_cast<std::int64_t>(units_per_encounter);

    std::uint64_t worst_units = 0;
    const gc::Encounter* worst = nullptr;
    gd::Array overages;
    for (const gc::Encounter& encounter : source.encounters) {
        const std::uint64_t units = fielded_units(encounter);
        if (worst == nullptr || units > worst_units) {
            worst = &encounter;
            worst_units = units;
        }
        if (units_per_encounter == 0U || units <= units_per_encounter) {
            continue;
        }
        gd::Dictionary overage;
        overage["screen"] = "map";
        overage["owner_id"] = text(std::to_string(encounter.id));
        overage["name"] = text(encounter.name);
        overage["units"] = static_cast<std::int64_t>(units);
        overage["over_by"] = static_cast<std::int64_t>(
            units - units_per_encounter
        );
        overages.push_back(overage);
    }

    target["fits"] = overages.is_empty();
    target["maximum_units"] = static_cast<std::int64_t>(worst_units);
    target["worst_stage"] =
        worst == nullptr ? gd::String{} : text(worst->name);
    target["worst_owner_id"] = worst == nullptr
        ? gd::String{}
        : text(std::to_string(worst->id));
    target["overages"] = overages;
    return target;
}

gd::Array budget_stages(const gc::GameSource& source) {
    gd::Array stages;
    for (const gc::Encounter& encounter : source.encounters) {
        gd::Dictionary stage;
        stage["owner_id"] = text(std::to_string(encounter.id));
        stage["name"] = text(encounter.name);
        stage["units"] = static_cast<std::int64_t>(fielded_units(encounter));
        stages.push_back(stage);
    }
    return stages;
}

}  // namespace

void GrandleonProjectCompiler::_bind_methods() {
    gd::ClassDB::bind_method(
        gd::D_METHOD("compile_source_json", "source_json"),
        &GrandleonProjectCompiler::compile_source_json
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("target_budget_report", "source_json"),
        &GrandleonProjectCompiler::target_budget_report
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("stable_source_id", "source_key"),
        &GrandleonProjectCompiler::stable_source_id
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "instantiate_source_json", "template_json", "package_id",
            "game_id", "title"
        ),
        &GrandleonProjectCompiler::instantiate_source_json
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "update_map_cell", "source_json", "map_id", "x", "y",
            "terrain_id"
        ),
        &GrandleonProjectCompiler::update_map_cell
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "update_map_terrain", "source_json", "map_id", "terrain_ids"
        ),
        &GrandleonProjectCompiler::update_map_terrain
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "resize_map", "source_json", "map_id", "width", "height",
            "fill_terrain"
        ),
        &GrandleonProjectCompiler::resize_map
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_map", "source_json", "map_id", "name", "width", "height",
            "fill_terrain"
        ),
        &GrandleonProjectCompiler::add_map
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_campaign_chapter", "source_json", "campaign_id",
            "after_node_id", "map_id", "map_name", "story_node_id",
            "story_name", "battle_node_id", "battle_name", "fill_terrain"
        ),
        &GrandleonProjectCompiler::add_campaign_chapter
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "move_campaign_chapter", "source_json", "campaign_id",
            "story_node_id", "direction"
        ),
        &GrandleonProjectCompiler::move_campaign_chapter
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_campaign_branch", "source_json", "campaign_id",
            "from_node_id", "target_node_id", "objective_id",
            "objective_result", "map_id", "map_name", "story_node_id",
            "story_name", "battle_node_id", "battle_name", "fill_terrain"
        ),
        &GrandleonProjectCompiler::add_campaign_branch
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_map_surroundings", "source_json", "map_id", "mode",
            "colour", "depth", "terrain_ids", "fill_terrain"
        ),
        &GrandleonProjectCompiler::set_map_surroundings
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_map_visual_visibility", "source_json", "map_id",
            "reveal_radius", "cell_indices"
        ),
        &GrandleonProjectCompiler::set_map_visual_visibility
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "move_placement", "source_json", "campaign_id", "node_id",
            "placement_id", "x", "y"
        ),
        &GrandleonProjectCompiler::move_placement
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_placement", "source_json", "campaign_id", "node_id",
            "placement_id", "unit_type_id", "side", "member_id", "x", "y"
        ),
        &GrandleonProjectCompiler::add_placement
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "remove_placement", "source_json", "campaign_id", "node_id",
            "placement_id"
        ),
        &GrandleonProjectCompiler::remove_placement
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_placement_minimum_players", "source_json", "campaign_id",
            "node_id", "placement_id", "minimum_players"
        ),
        &GrandleonProjectCompiler::set_placement_minimum_players
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_placement_player", "source_json", "campaign_id", "node_id",
            "placement_id", "player"
        ),
        &GrandleonProjectCompiler::set_placement_player
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_placement_talkable", "source_json", "campaign_id",
            "node_id", "placement_id", "talkable"
        ),
        &GrandleonProjectCompiler::set_placement_talkable
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_deployment_tiles", "source_json", "campaign_id", "node_id",
            "deployment_id", "coordinates"
        ),
        &GrandleonProjectCompiler::set_deployment_tiles
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_encounter_objective", "source_json", "campaign_id",
            "node_id", "objective_id", "name", "kind", "side",
            "target_placement_id", "rounds"
        ),
        &GrandleonProjectCompiler::add_encounter_objective
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_encounter_objectives", "source_json", "campaign_id",
            "node_id", "objective_ids"
        ),
        &GrandleonProjectCompiler::set_encounter_objectives
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "update_dialogue_line", "source_json", "dialogue_id",
            "line_index", "speaker", "line_text"
        ),
        &GrandleonProjectCompiler::update_dialogue_line
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_dialogue_line_character", "source_json", "dialogue_id",
            "line_index", "speaker", "unit_type_id", "line_text"
        ),
        &GrandleonProjectCompiler::set_dialogue_line_character
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_dialogue_backdrop", "source_json", "dialogue_id",
            "background_id"
        ),
        &GrandleonProjectCompiler::set_dialogue_backdrop
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "source_character_preview", "source_json", "unit_type_id"
        ),
        &GrandleonProjectCompiler::source_character_preview
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_unit_type_from_template", "source_json",
            "template_unit_type_id", "unit_type_id", "name"
        ),
        &GrandleonProjectCompiler::add_unit_type_from_template
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_role_from_template", "source_json",
            "template_unit_type_id", "unit_type_id", "class_id", "name"
        ),
        &GrandleonProjectCompiler::add_role_from_template
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_campaign_member", "source_json", "campaign_id",
            "join_node_id", "member_id", "name", "unit_type_id"
        ),
        &GrandleonProjectCompiler::add_campaign_member
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "rename_campaign_member", "source_json", "campaign_id",
            "member_id", "name"
        ),
        &GrandleonProjectCompiler::rename_campaign_member
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_campaign_member_player_nameable", "source_json",
            "campaign_id", "member_id", "player_nameable"
        ),
        &GrandleonProjectCompiler::set_campaign_member_player_nameable
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_campaign_member_modifier", "source_json", "campaign_id",
            "member_id", "modifier", "value"
        ),
        &GrandleonProjectCompiler::set_campaign_member_modifier
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_campaign_grant", "source_json", "campaign_id", "node_id",
            "kind", "asset_id", "quantity"
        ),
        &GrandleonProjectCompiler::set_campaign_grant
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_project_presentation", "source_json", "field", "value"
        ),
        &GrandleonProjectCompiler::set_project_presentation
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "set_unit_type_kit_entry", "source_json", "unit_type_id",
            "kind", "asset_id", "included"
        ),
        &GrandleonProjectCompiler::set_unit_type_kit_entry
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "insert_dialogue_line", "source_json", "dialogue_id",
            "after_line_index", "speaker", "line_text"
        ),
        &GrandleonProjectCompiler::insert_dialogue_line
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "remove_dialogue_line", "source_json", "dialogue_id",
            "line_index"
        ),
        &GrandleonProjectCompiler::remove_dialogue_line
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "move_dialogue_line", "source_json", "dialogue_id",
            "line_index", "new_index"
        ),
        &GrandleonProjectCompiler::move_dialogue_line
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_dialogue_scene", "source_json", "campaign_id", "node_id",
            "dialogue_id", "name", "background_id", "speaker",
            "unit_type_id", "line_text"
        ),
        &GrandleonProjectCompiler::add_dialogue_scene
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD(
            "add_talk_dialogue", "source_json", "campaign_id", "node_id",
            "placement_id", "name", "background_id", "speaker", "line_text"
        ),
        &GrandleonProjectCompiler::add_talk_dialogue
    );
    gd::ClassDB::bind_method(
        gd::D_METHOD("supported_schema"),
        &GrandleonProjectCompiler::supported_schema
    );
}

gd::String GrandleonProjectCompiler::stable_source_id(
    const gd::String& source_key
) const {
    return text(std::to_string(core::stable_content_id_v1(narrow(source_key))));
}

gd::Dictionary GrandleonProjectCompiler::compile_source_json(
    const gd::String& source_json
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["schema"] = supported_schema();
    result["package"] = gd::PackedByteArray{};

    gd::Array diagnostics;
    const auto parsed = gc::parse_source_project_json(narrow(source_json));
    if (!parsed) {
        for (const gc::SourceDiagnostic& diagnostic : parsed.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }

    const auto compiled = gc::compile(parsed.source);
    if (!compiled) {
        for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
            diagnostics.push_back(
                compile_diagnostic(diagnostic, parsed.source)
            );
        }
        result["diagnostics"] = diagnostics;
        return result;
    }

    gd::PackedByteArray package;
    package.resize(static_cast<int64_t>(compiled.package.size()));
    std::uint8_t* const output = package.ptrw();
    for (std::size_t index = 0; index < compiled.package.size(); ++index) {
        output[index] = compiled.package[index];
    }
    result["ok"] = true;
    result["package"] = package;
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::target_budget_report(
    const gd::String& source_json
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["targets"] = gd::Array{};
    result["stages"] = gd::Array{};
    gd::Array diagnostics;

    const auto parsed = gc::parse_source_project_json(narrow(source_json));
    if (!parsed) {
        for (const gc::SourceDiagnostic& diagnostic : parsed.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }

    gd::Array targets;
    targets.push_back(target_budget(parsed.source, "pc", "PC", 0U));
    targets.push_back(target_budget(
        parsed.source, "nintendo64", "Nintendo 64",
        pr::target_budget::nintendo64_units_per_encounter
    ));
    targets.push_back(target_budget(
        parsed.source, "playstation", "PlayStation",
        pr::target_budget::playstation_units_per_encounter
    ));
    result["ok"] = true;
    result["targets"] = targets;
    result["stages"] = budget_stages(parsed.source);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::instantiate_source_json(
    const gd::String& template_json,
    const gd::String& package_id,
    const gd::String& game_id,
    const gd::String& title
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult instantiated =
        gc::instantiate_source_project_json(
            narrow(template_json), narrow(package_id), narrow(game_id),
            narrow(title)
        );
    if (!instantiated) {
        for (const gc::SourceDiagnostic& diagnostic :
             instantiated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(instantiated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::update_map_cell(
    const gd::String& source_json,
    const gd::String& map_id,
    std::int64_t x,
    std::int64_t y,
    const gd::String& terrain_id
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::update_source_map_cell_json(
        narrow(source_json), narrow(map_id), x, y, narrow(terrain_id)
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::update_map_terrain(
    const gd::String& source_json,
    const gd::String& map_id,
    const gd::PackedStringArray& terrain_ids
) const {
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(terrain_ids.size()));
    for (std::int64_t index = 0; index < terrain_ids.size(); ++index) {
        names.push_back(narrow(terrain_ids[index]));
    }

    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::update_source_map_terrain_json(
        narrow(source_json), narrow(map_id), names
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::resize_map(
    const gd::String& source_json,
    const gd::String& map_id,
    std::int64_t width,
    std::int64_t height,
    const gd::String& fill_terrain
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult resized = gc::resize_source_map_json(
        narrow(source_json), narrow(map_id), width, height,
        narrow(fill_terrain)
    );
    if (!resized) {
        for (const gc::SourceDiagnostic& diagnostic : resized.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(resized.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_map(
    const gd::String& source_json,
    const gd::String& map_id,
    const gd::String& name,
    std::int64_t width,
    std::int64_t height,
    const gd::String& fill_terrain
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult added = gc::add_source_map_json(
        narrow(source_json), narrow(map_id), narrow(name), width, height,
        narrow(fill_terrain)
    );
    if (!added) {
        for (const gc::SourceDiagnostic& diagnostic : added.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(added.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_campaign_chapter(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& after_node_id,
    const gd::String& map_id,
    const gd::String& map_name,
    const gd::String& story_node_id,
    const gd::String& story_name,
    const gd::String& battle_node_id,
    const gd::String& battle_name,
    const gd::String& fill_terrain
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult added =
        gc::add_source_campaign_chapter_json(
            narrow(source_json), narrow(campaign_id), narrow(after_node_id),
            narrow(map_id), narrow(map_name), narrow(story_node_id),
            narrow(story_name), narrow(battle_node_id), narrow(battle_name),
            narrow(fill_terrain)
        );
    if (!added) {
        for (const gc::SourceDiagnostic& diagnostic : added.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(added.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::move_campaign_chapter(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& story_node_id,
    std::int64_t direction
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult moved =
        gc::move_source_campaign_chapter_json(
            narrow(source_json), narrow(campaign_id), narrow(story_node_id),
            direction
        );
    if (!moved) {
        for (const gc::SourceDiagnostic& diagnostic : moved.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(moved.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_campaign_branch(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& from_node_id,
    const gd::String& target_node_id,
    const gd::String& objective_id,
    const gd::String& objective_result,
    const gd::String& map_id,
    const gd::String& map_name,
    const gd::String& story_node_id,
    const gd::String& story_name,
    const gd::String& battle_node_id,
    const gd::String& battle_name,
    const gd::String& fill_terrain
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult added = gc::add_source_campaign_branch_json(
        narrow(source_json), narrow(campaign_id), narrow(from_node_id),
        narrow(target_node_id), narrow(objective_id), narrow(objective_result),
        narrow(map_id), narrow(map_name), narrow(story_node_id),
        narrow(story_name), narrow(battle_node_id), narrow(battle_name),
        narrow(fill_terrain)
    );
    if (!added) {
        for (const gc::SourceDiagnostic& diagnostic : added.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(added.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::move_placement(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& placement_id,
    std::int64_t x,
    std::int64_t y
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult moved = gc::move_source_placement_json(
        narrow(source_json), narrow(campaign_id), narrow(node_id),
        narrow(placement_id), x, y
    );
    if (!moved) {
        for (const gc::SourceDiagnostic& diagnostic : moved.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(moved.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_map_surroundings(
    const gd::String& source_json,
    const gd::String& map_id,
    const gd::String& mode,
    const gd::String& colour,
    std::int64_t depth,
    const gd::PackedStringArray& terrain_ids,
    const gd::String& fill_terrain
) const {
    std::vector<std::string> ids;
    ids.reserve(static_cast<std::size_t>(terrain_ids.size()));
    for (std::int64_t index = 0; index < terrain_ids.size(); ++index) {
        ids.push_back(narrow(terrain_ids[index]));
    }
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_map_surroundings_json(
            narrow(source_json), narrow(map_id), narrow(mode), narrow(colour),
            depth, ids, narrow(fill_terrain)
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_map_visual_visibility(
    const gd::String& source_json,
    const gd::String& map_id,
    std::int64_t reveal_radius,
    const gd::PackedInt32Array& cell_indices
) const {
    std::vector<std::int64_t> cells;
    cells.reserve(static_cast<std::size_t>(cell_indices.size()));
    for (std::int64_t index = 0; index < cell_indices.size(); ++index) {
        cells.push_back(cell_indices[index]);
    }
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_map_visual_visibility_json(
            narrow(source_json), narrow(map_id), reveal_radius, cells
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_placement(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& placement_id,
    const gd::String& unit_type_id,
    const gd::String& side,
    const gd::String& member_id,
    std::int64_t x,
    std::int64_t y
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult added = gc::add_source_placement_json(
        narrow(source_json), narrow(campaign_id), narrow(node_id),
        narrow(placement_id), narrow(unit_type_id), narrow(side),
        narrow(member_id), x, y
    );
    if (!added) {
        for (const gc::SourceDiagnostic& diagnostic : added.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(added.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::remove_placement(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& placement_id
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult removed = gc::remove_source_placement_json(
        narrow(source_json), narrow(campaign_id), narrow(node_id),
        narrow(placement_id)
    );
    if (!removed) {
        for (const gc::SourceDiagnostic& diagnostic : removed.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(removed.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_placement_minimum_players(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& placement_id,
    std::int64_t minimum_players
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_placement_minimum_players_json(
            narrow(source_json), narrow(campaign_id), narrow(node_id),
            narrow(placement_id), minimum_players
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_placement_player(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& placement_id,
    std::int64_t player
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::set_source_placement_player_json(
        narrow(source_json), narrow(campaign_id), narrow(node_id),
        narrow(placement_id), player
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_placement_talkable(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& placement_id,
    bool talkable
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_placement_talkable_json(
            narrow(source_json), narrow(campaign_id), narrow(node_id),
            narrow(placement_id), talkable
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_deployment_tiles(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& deployment_id,
    const gd::PackedInt32Array& coordinates
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    if (coordinates.size() % 2 != 0) {
        gd::Dictionary diagnostic;
        diagnostic["phase"] = "source";
        diagnostic["code"] = "invalid_value";
        diagnostic["path"] = "$.campaigns.deployment.tiles";
        diagnostic["detail"] =
            "deployment coordinates must contain complete x/y pairs";
        diagnostics.push_back(diagnostic);
        result["diagnostics"] = diagnostics;
        return result;
    }
    std::vector<gc::SourceMapCell> tiles;
    tiles.reserve(static_cast<std::size_t>(coordinates.size() / 2));
    for (std::int64_t index = 0; index < coordinates.size(); index += 2) {
        tiles.push_back({coordinates[index], coordinates[index + 1]});
    }
    const gc::SourceTextResult updated =
        gc::set_source_deployment_tiles_json(
            narrow(source_json), narrow(campaign_id), narrow(node_id),
            narrow(deployment_id), tiles
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_encounter_objective(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& objective_id,
    const gd::String& name,
    const gd::String& kind,
    const gd::String& side,
    const gd::String& target_placement_id,
    std::int64_t rounds
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult added =
        gc::add_source_encounter_objective_json(
            narrow(source_json), narrow(campaign_id), narrow(node_id),
            narrow(objective_id), narrow(name), narrow(kind), narrow(side),
            narrow(target_placement_id), rounds
        );
    if (!added) {
        for (const gc::SourceDiagnostic& diagnostic : added.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(added.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_encounter_objectives(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::PackedStringArray& objective_ids
) const {
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(objective_ids.size()));
    for (std::int64_t index = 0; index < objective_ids.size(); ++index) {
        names.push_back(narrow(objective_ids[index]));
    }
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_encounter_objectives_json(
            narrow(source_json), narrow(campaign_id), narrow(node_id), names
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::update_dialogue_line(
    const gd::String& source_json,
    const gd::String& dialogue_id,
    std::int64_t line_index,
    const gd::String& speaker,
    const gd::String& line_text
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::update_source_dialogue_line_json(
        narrow(source_json), narrow(dialogue_id), line_index,
        narrow(speaker), narrow(line_text)
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_dialogue_line_character(
    const gd::String& source_json,
    const gd::String& dialogue_id,
    std::int64_t line_index,
    const gd::String& speaker,
    const gd::String& unit_type_id,
    const gd::String& line_text
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_dialogue_line_character_json(
            narrow(source_json), narrow(dialogue_id), line_index,
            narrow(speaker), narrow(unit_type_id), narrow(line_text)
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_dialogue_backdrop(
    const gd::String& source_json,
    const gd::String& dialogue_id,
    const gd::String& background_id
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::set_source_dialogue_backdrop_json(
        narrow(source_json), narrow(dialogue_id), narrow(background_id)
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::source_character_preview(
    const gd::String& source_json,
    const gd::String& unit_type_id
) const {
    gd::Dictionary result;
    result["ok"] = false;
    gd::Array diagnostics;
    const gc::SourceParseResult parsed =
        gc::parse_source_project_json(narrow(source_json));
    if (!parsed) {
        for (const gc::SourceDiagnostic& diagnostic : parsed.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    const gc::StableId wanted =
        core::stable_content_id_v1(narrow(unit_type_id));
    const gc::UnitType* unit = nullptr;
    for (const gc::UnitType& candidate : parsed.source.unit_types) {
        if (candidate.id == wanted) {
            unit = &candidate;
            break;
        }
    }
    if (unit == nullptr) {
        gd::Dictionary diagnostic;
        diagnostic["phase"] = "source";
        diagnostic["code"] = "missing_reference";
        diagnostic["path"] = "$.unitTypes";
        diagnostic["detail"] = "dialogue character was not found";
        diagnostics.push_back(diagnostic);
        result["diagnostics"] = diagnostics;
        return result;
    }
    std::uint8_t colour = 0;
    for (std::size_t index = 0; index < parsed.source.factions.size(); ++index) {
        const gc::Faction& faction = parsed.source.factions[index];
        if (faction.id == unit->faction_id) {
            colour = gc::resolved_faction_colour(faction, index);
            break;
        }
    }
    result["ok"] = true;
    result["archetype"] = gc::resolved_archetype(parsed.source, *unit);
    result["colour"] = colour;
    result["style"] = gc::resolved_character_style(parsed.source, *unit);
    result["figure"] = gc::resolved_character_figure(parsed.source, *unit);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_campaign_member(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& join_node_id,
    const gd::String& member_id,
    const gd::String& name,
    const gd::String& unit_type_id
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::add_source_campaign_member_json(
        narrow(source_json), narrow(campaign_id), narrow(join_node_id),
        narrow(member_id), narrow(name), narrow(unit_type_id)
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_unit_type_from_template(
    const gd::String& source_json,
    const gd::String& template_unit_type_id,
    const gd::String& unit_type_id,
    const gd::String& name
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::add_source_unit_type_from_template_json(
            narrow(source_json), narrow(template_unit_type_id),
            narrow(unit_type_id), narrow(name)
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_role_from_template(
    const gd::String& source_json,
    const gd::String& template_unit_type_id,
    const gd::String& unit_type_id,
    const gd::String& class_id,
    const gd::String& name
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::add_source_role_from_template_json(
            narrow(source_json), narrow(template_unit_type_id),
            narrow(unit_type_id), narrow(class_id), narrow(name)
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::rename_campaign_member(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& member_id,
    const gd::String& name
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::rename_source_campaign_member_json(
            narrow(source_json), narrow(campaign_id), narrow(member_id),
            narrow(name)
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_campaign_member_player_nameable(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& member_id,
    bool player_nameable
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_campaign_member_player_nameable_json(
            narrow(source_json), narrow(campaign_id), narrow(member_id),
            player_nameable
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_campaign_member_modifier(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& member_id,
    const gd::String& modifier,
    std::int64_t value
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_campaign_member_modifier_json(
            narrow(source_json), narrow(campaign_id), narrow(member_id),
            narrow(modifier), value
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_campaign_grant(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& kind,
    const gd::String& asset_id,
    std::int64_t quantity
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::set_source_campaign_grant_json(
        narrow(source_json), narrow(campaign_id), narrow(node_id),
        narrow(kind), narrow(asset_id), quantity
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_project_presentation(
    const gd::String& source_json,
    const gd::String& field,
    const gd::String& value
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_project_presentation_json(
            narrow(source_json), narrow(field), narrow(value)
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::set_unit_type_kit_entry(
    const gd::String& source_json,
    const gd::String& unit_type_id,
    const gd::String& kind,
    const gd::String& asset_id,
    bool included
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated =
        gc::set_source_unit_type_kit_entry_json(
            narrow(source_json), narrow(unit_type_id), narrow(kind),
            narrow(asset_id), included
        );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::insert_dialogue_line(
    const gd::String& source_json,
    const gd::String& dialogue_id,
    std::int64_t after_line_index,
    const gd::String& speaker,
    const gd::String& line_text
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::insert_source_dialogue_line_json(
        narrow(source_json), narrow(dialogue_id), after_line_index,
        narrow(speaker), narrow(line_text)
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::remove_dialogue_line(
    const gd::String& source_json,
    const gd::String& dialogue_id,
    std::int64_t line_index
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::remove_source_dialogue_line_json(
        narrow(source_json), narrow(dialogue_id), line_index
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::move_dialogue_line(
    const gd::String& source_json,
    const gd::String& dialogue_id,
    std::int64_t line_index,
    std::int64_t new_index
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult updated = gc::move_source_dialogue_line_json(
        narrow(source_json), narrow(dialogue_id), line_index, new_index
    );
    if (!updated) {
        for (const gc::SourceDiagnostic& diagnostic : updated.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(updated.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_dialogue_scene(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& dialogue_id,
    const gd::String& name,
    const gd::String& background_id,
    const gd::String& speaker,
    const gd::String& unit_type_id,
    const gd::String& line_text
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    gd::Array diagnostics;
    const gc::SourceTextResult added = gc::add_source_dialogue_scene_json(
        narrow(source_json), narrow(campaign_id), narrow(node_id),
        narrow(dialogue_id), narrow(name), narrow(background_id),
        narrow(speaker), narrow(unit_type_id), narrow(line_text)
    );
    if (!added) {
        for (const gc::SourceDiagnostic& diagnostic : added.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    result["ok"] = true;
    result["source"] = text(added.json);
    result["diagnostics"] = diagnostics;
    return result;
}

gd::Dictionary GrandleonProjectCompiler::add_talk_dialogue(
    const gd::String& source_json,
    const gd::String& campaign_id,
    const gd::String& node_id,
    const gd::String& placement_id,
    const gd::String& name,
    const gd::String& background_id,
    const gd::String& speaker,
    const gd::String& line_text
) const {
    gd::Dictionary result;
    result["ok"] = false;
    result["source"] = "";
    result["dialogue_id"] = "";
    gd::Array diagnostics;
    const std::string campaign = narrow(campaign_id);
    const std::string node = narrow(node_id);
    const std::string placement = narrow(placement_id);
    const gc::SourceTextResult added =
        gc::add_source_placement_talk_dialogue_json(
            narrow(source_json), campaign, node, placement, narrow(name),
            narrow(background_id), narrow(speaker), narrow(line_text)
        );
    if (!added) {
        for (const gc::SourceDiagnostic& diagnostic : added.diagnostics) {
            diagnostics.push_back(source_diagnostic(diagnostic));
        }
        result["diagnostics"] = diagnostics;
        return result;
    }
    const std::string owner = campaign + "/" + node + "/" + placement;
    result["ok"] = true;
    result["source"] = text(added.json);
    result["dialogue_id"] = text(
        "talk_scene_" + std::to_string(
            core::stable_content_id_v1(owner + "/dialogue")
        )
    );
    result["diagnostics"] = diagnostics;
    return result;
}

gd::String GrandleonProjectCompiler::supported_schema() const {
    return text(gc::supported_source_schema);
}

}  // namespace grandleon::godot_frontend
