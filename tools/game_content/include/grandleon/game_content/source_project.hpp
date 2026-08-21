// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/game_content/compiler.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace grandleon::game_content {

// The one source schema this compiler reads.
//
// It refuses every other version rather than migrating between them, and that
// is deliberate. Bringing an out-of-date project up is a thing an author does
// once, to a file they then keep. It belongs where the author is, which is the
// editor, and the editor's load path is pure TypeScript that reaches no
// WebAssembly. A migration living here would be unavailable at the moment it is
// needed and duplicated at the moment it is not. So this end names the version
// it found, the version it wants, and the command that closes the gap;
// `tools/source_schema/migration.mjs` is the other end, and
// `tools/source_schema/test.mjs` pins this string to it.
inline constexpr std::string_view supported_source_schema = "1.2.0";

enum class SourceDiagnosticCode : std::uint8_t {
    invalid_json,
    missing_value,
    invalid_value,
    unsupported_content,
    stable_id_collision,
};

[[nodiscard]] std::string_view source_diagnostic_name(
    SourceDiagnosticCode code
) noexcept;

struct SourceDiagnostic final {
    SourceDiagnosticCode code{};
    std::string path;
    std::string detail;
};

struct SourceParseResult final {
    GameSource source;
    std::vector<SourceDiagnostic> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

// Parses canonical source schema v1 into the native semantic compiler model.
// This is deliberately narrower than schema validation: unsupported gameplay
// registries are rejected rather than silently omitted from the package.
[[nodiscard]] SourceParseResult parse_source_project_json(
    std::string_view json
);

}  // namespace grandleon::game_content
