// SPDX-License-Identifier: MIT
#include <grandleon/godot/project_compiler.hpp>

#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace grandleon::godot_frontend {
namespace {

namespace gc = game_content;
namespace gd = ::godot;

gd::String text(std::string_view value) {
    return gd::String::utf8(value.data(), static_cast<int64_t>(value.size()));
}

std::string narrow(const gd::String& value) {
    const gd::CharString utf8 = value.utf8();
    return std::string(utf8.get_data(), static_cast<std::size_t>(utf8.length()));
}

gd::Dictionary source_diagnostic(const gc::SourceDiagnostic& diagnostic) {
    gd::Dictionary result;
    result["phase"] = "source";
    result["code"] = text(gc::source_diagnostic_name(diagnostic.code));
    result["path"] = text(diagnostic.path);
    result["detail"] = text(diagnostic.detail);
    return result;
}

gd::Dictionary compile_diagnostic(const gc::Diagnostic& diagnostic) {
    gd::Dictionary result;
    result["phase"] = "compile";
    result["code"] = text(gc::diagnostic_name(diagnostic.code));
    result["path"] = text(diagnostic.path);
    if (diagnostic.related_id != 0) {
        // Stable ids are unsigned 64-bit values. Godot integers are signed,
        // so preserve the complete value as decimal text for diagnostics.
        result["related_id"] = text(std::to_string(diagnostic.related_id));
    }
    return result;
}

}  // namespace

void GrandleonProjectCompiler::_bind_methods() {
    gd::ClassDB::bind_method(
        gd::D_METHOD("compile_source_json", "source_json"),
        &GrandleonProjectCompiler::compile_source_json
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
            "set_map_surroundings", "source_json", "map_id", "mode",
            "colour", "depth", "terrain_ids"
        ),
        &GrandleonProjectCompiler::set_map_surroundings
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
        gd::D_METHOD("supported_schema"),
        &GrandleonProjectCompiler::supported_schema
    );
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
            diagnostics.push_back(compile_diagnostic(diagnostic));
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
    const gd::PackedStringArray& terrain_ids
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
            depth, ids
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

gd::String GrandleonProjectCompiler::supported_schema() const {
    return text(gc::supported_source_schema);
}

}  // namespace grandleon::godot_frontend
