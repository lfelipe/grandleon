// SPDX-License-Identifier: MIT
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

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
    [[nodiscard]] ::godot::String supported_schema() const;

protected:
    static void _bind_methods();
};

}  // namespace grandleon::godot_frontend
