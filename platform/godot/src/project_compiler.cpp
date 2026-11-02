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

gd::String GrandleonProjectCompiler::supported_schema() const {
    return text(gc::supported_source_schema);
}

}  // namespace grandleon::godot_frontend
