// SPDX-License-Identifier: MIT
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

namespace grandleon::godot_frontend {

// The authoring boundary exposed to Godot.
//
// Source projects remain portable JSON and packages remain the engine's
// portable bytes.  No path crosses this boundary, so a project created on one
// PC can be opened on another without retaining anything about the first.
class GrandleonProjectCompiler final : public ::godot::RefCounted {
    GDCLASS(GrandleonProjectCompiler, ::godot::RefCounted)

public:
    [[nodiscard]] ::godot::Dictionary compile_source_json(
        const ::godot::String& source_json
    ) const;
    [[nodiscard]] ::godot::Dictionary instantiate_source_json(
        const ::godot::String& template_json,
        const ::godot::String& package_id,
        const ::godot::String& game_id,
        const ::godot::String& title
    ) const;
    [[nodiscard]] ::godot::Dictionary update_map_cell(
        const ::godot::String& source_json,
        const ::godot::String& map_id,
        std::int64_t x,
        std::int64_t y,
        const ::godot::String& terrain_id
    ) const;
    [[nodiscard]] ::godot::Dictionary update_map_terrain(
        const ::godot::String& source_json,
        const ::godot::String& map_id,
        const ::godot::PackedStringArray& terrain_ids
    ) const;
    [[nodiscard]] ::godot::Dictionary resize_map(
        const ::godot::String& source_json,
        const ::godot::String& map_id,
        std::int64_t width,
        std::int64_t height,
        const ::godot::String& fill_terrain
    ) const;
    [[nodiscard]] ::godot::Dictionary add_map(
        const ::godot::String& source_json,
        const ::godot::String& map_id,
        const ::godot::String& name,
        std::int64_t width,
        std::int64_t height,
        const ::godot::String& fill_terrain
    ) const;
    [[nodiscard]] ::godot::Dictionary add_campaign_chapter(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& after_node_id,
        const ::godot::String& map_id,
        const ::godot::String& map_name,
        const ::godot::String& story_node_id,
        const ::godot::String& story_name,
        const ::godot::String& battle_node_id,
        const ::godot::String& battle_name,
        const ::godot::String& fill_terrain
    ) const;
    [[nodiscard]] ::godot::Dictionary set_map_surroundings(
        const ::godot::String& source_json,
        const ::godot::String& map_id,
        const ::godot::String& mode,
        const ::godot::String& colour,
        std::int64_t depth,
        const ::godot::PackedStringArray& terrain_ids
    ) const;
    [[nodiscard]] ::godot::Dictionary move_placement(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::String& placement_id,
        std::int64_t x,
        std::int64_t y
    ) const;
    [[nodiscard]] ::godot::Dictionary add_placement(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::String& placement_id,
        const ::godot::String& unit_type_id,
        const ::godot::String& side,
        const ::godot::String& member_id,
        std::int64_t x,
        std::int64_t y
    ) const;
    [[nodiscard]] ::godot::Dictionary set_placement_minimum_players(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::String& placement_id,
        std::int64_t minimum_players
    ) const;
    [[nodiscard]] ::godot::Dictionary set_placement_player(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::String& placement_id,
        std::int64_t player
    ) const;
    [[nodiscard]] ::godot::Dictionary remove_placement(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::String& placement_id
    ) const;
    [[nodiscard]] ::godot::Dictionary set_deployment_tiles(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::String& deployment_id,
        const ::godot::PackedInt32Array& coordinates
    ) const;
    [[nodiscard]] ::godot::Dictionary add_encounter_objective(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::String& objective_id,
        const ::godot::String& name,
        const ::godot::String& kind,
        const ::godot::String& side,
        const ::godot::String& target_placement_id,
        std::int64_t rounds
    ) const;
    [[nodiscard]] ::godot::Dictionary set_encounter_objectives(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::PackedStringArray& objective_ids
    ) const;
    [[nodiscard]] ::godot::Dictionary update_dialogue_line(
        const ::godot::String& source_json,
        const ::godot::String& dialogue_id,
        std::int64_t line_index,
        const ::godot::String& speaker,
        const ::godot::String& line_text
    ) const;
    [[nodiscard]] ::godot::Dictionary set_dialogue_line_character(
        const ::godot::String& source_json,
        const ::godot::String& dialogue_id,
        std::int64_t line_index,
        const ::godot::String& speaker,
        const ::godot::String& unit_type_id,
        const ::godot::String& line_text
    ) const;
    [[nodiscard]] ::godot::Dictionary set_dialogue_backdrop(
        const ::godot::String& source_json,
        const ::godot::String& dialogue_id,
        const ::godot::String& background_id
    ) const;
    [[nodiscard]] ::godot::Dictionary source_character_preview(
        const ::godot::String& source_json,
        const ::godot::String& unit_type_id
    ) const;
    [[nodiscard]] ::godot::Dictionary add_campaign_member(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& join_node_id,
        const ::godot::String& member_id,
        const ::godot::String& name,
        const ::godot::String& unit_type_id
    ) const;
    [[nodiscard]] ::godot::Dictionary rename_campaign_member(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& member_id,
        const ::godot::String& name
    ) const;
    [[nodiscard]] ::godot::Dictionary set_campaign_member_modifier(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& member_id,
        const ::godot::String& modifier,
        std::int64_t value
    ) const;
    [[nodiscard]] ::godot::Dictionary set_campaign_grant(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::String& kind,
        const ::godot::String& asset_id,
        std::int64_t quantity
    ) const;
    [[nodiscard]] ::godot::Dictionary set_project_presentation(
        const ::godot::String& source_json,
        const ::godot::String& field,
        const ::godot::String& value
    ) const;
    [[nodiscard]] ::godot::Dictionary set_unit_type_kit_entry(
        const ::godot::String& source_json,
        const ::godot::String& unit_type_id,
        const ::godot::String& kind,
        const ::godot::String& asset_id,
        bool included
    ) const;
    [[nodiscard]] ::godot::Dictionary insert_dialogue_line(
        const ::godot::String& source_json,
        const ::godot::String& dialogue_id,
        std::int64_t after_line_index,
        const ::godot::String& speaker,
        const ::godot::String& line_text
    ) const;
    [[nodiscard]] ::godot::Dictionary remove_dialogue_line(
        const ::godot::String& source_json,
        const ::godot::String& dialogue_id,
        std::int64_t line_index
    ) const;
    [[nodiscard]] ::godot::Dictionary move_dialogue_line(
        const ::godot::String& source_json,
        const ::godot::String& dialogue_id,
        std::int64_t line_index,
        std::int64_t new_index
    ) const;
    [[nodiscard]] ::godot::Dictionary add_dialogue_scene(
        const ::godot::String& source_json,
        const ::godot::String& campaign_id,
        const ::godot::String& node_id,
        const ::godot::String& dialogue_id,
        const ::godot::String& name,
        const ::godot::String& background_id,
        const ::godot::String& speaker,
        const ::godot::String& unit_type_id,
        const ::godot::String& line_text
    ) const;
    [[nodiscard]] ::godot::String supported_schema() const;

protected:
    static void _bind_methods();
};

}  // namespace grandleon::godot_frontend
