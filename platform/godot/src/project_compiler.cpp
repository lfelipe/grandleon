// SPDX-License-Identifier: MIT
#include <grandleon/godot/project_compiler.hpp>

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
            "set_deployment_tiles", "source_json", "campaign_id", "node_id",
            "deployment_id", "coordinates"
        ),
        &GrandleonProjectCompiler::set_deployment_tiles
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

gd::String GrandleonProjectCompiler::supported_schema() const {
    return text(gc::supported_source_schema);
}

}  // namespace grandleon::godot_frontend
