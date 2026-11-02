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
    [[nodiscard]] ::godot::String supported_schema() const;

protected:
    static void _bind_methods();
};

}  // namespace grandleon::godot_frontend
