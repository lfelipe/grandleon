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

struct SourceTextResult final {
    std::string json;
    std::vector<SourceDiagnostic> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

// One author-facing map coordinate. Source edits keep these wider than the
// compiled package representation so an out-of-range value can be refused
// before any narrowing conversion changes what the caller supplied.
struct SourceMapCell final {
    std::int64_t x{};
    std::int64_t y{};
};

// Parses canonical source schema v1 into the native semantic compiler model.
// This is deliberately narrower than schema validation: unsupported gameplay
// registries are rejected rather than silently omitted from the package.
[[nodiscard]] SourceParseResult parse_source_project_json(
    std::string_view json
);

// Creates a new source project from a canonical template without passing its
// integer tokens through a floating-point JSON representation. The result is
// fully parsed before it is returned, so a caller cannot persist a malformed
// template under a fresh identity.
[[nodiscard]] SourceTextResult instantiate_source_project_json(
    std::string_view template_json,
    std::string_view package_id,
    std::string_view game_id,
    std::string_view title
);

// Replaces one terrain cell in a canonical source map and serializes the
// complete document again.  Authoring clients use this rather than a generic
// floating-point JSON round trip so every unrelated integer and field retains
// the source schema's exact representation.  The returned text is fully
// parsed before it is exposed; a bad map, coordinate, or terrain identifier
// therefore cannot become a partially edited project.
[[nodiscard]] SourceTextResult update_source_map_cell_json(
    std::string_view source_json,
    std::string_view map_id,
    std::int64_t x,
    std::int64_t y,
    std::string_view terrain_id
);

// Replaces a complete terrain array as one validated authoring command.  This
// is the efficient boundary for flood fill, rectangle, resize, and undo/redo:
// even a large edit parses and serializes the source once.
[[nodiscard]] SourceTextResult update_source_map_terrain_json(
    std::string_view source_json,
    std::string_view map_id,
    const std::vector<std::string>& terrain_ids
);

// Resizes a map from its top-left origin. Existing cells inside the new bounds
// retain their authored identities and newly exposed cells use fill_terrain.
// Encounter-aware clipping policy belongs to the creator preview; this narrow
// source operation changes no placement or deployment data implicitly.
[[nodiscard]] SourceTextResult resize_source_map_json(
    std::string_view source_json,
    std::string_view map_id,
    std::int64_t width,
    std::int64_t height,
    std::string_view fill_terrain
);

// Appends a canonical map definition to a source project. The caller supplies
// portable identities and presentation text only; no path or host detail can
// enter the document through this authoring boundary.
[[nodiscard]] SourceTextResult add_source_map_json(
    std::string_view source_json,
    std::string_view map_id,
    std::string_view name,
    std::int64_t width,
    std::int64_t height,
    std::string_view fill_terrain
);

// Adds the next linear campaign chapter after an existing encounter. The new
// chapter contains a story moment, a battle cloned from the known-playable
// encounter setup, and a fresh map filled with the selected terrain. The
// existing encounter's single unconditional transition is rerouted through
// the new chapter as one atomic edit; branched campaign graphs are left for a
// future graph editor rather than guessed at here.
[[nodiscard]] SourceTextResult add_source_campaign_chapter_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view after_node_id,
    std::string_view map_id,
    std::string_view map_name,
    std::string_view story_node_id,
    std::string_view story_name,
    std::string_view battle_node_id,
    std::string_view battle_name,
    std::string_view fill_terrain
);

// Replaces the presentation outside one tactical map. Manual terrain is the
// complete perimeter in row-major outer-rectangle order, omitting the playable
// interior. It is never appended to the map itself and therefore cannot become
// reachable. `none` removes the optional rule.
[[nodiscard]] SourceTextResult set_source_map_surroundings_json(
    std::string_view source_json,
    std::string_view map_id,
    std::string_view mode,
    std::string_view colour,
    std::int64_t depth,
    const std::vector<std::string>& terrain_ids
);

// Moves one encounter placement within the map used by its node. The nested
// identities keep the command unambiguous without exposing array positions to
// an authoring client. A project that compiled before the edit is never
// returned in an uncompilable state after it.
[[nodiscard]] SourceTextResult move_source_placement_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view node_id,
    std::string_view placement_id,
    std::int64_t x,
    std::int64_t y
);

// Makes an opposing placement always present (one) or a two-player addition
// (two). The source stores only the latter; restoring one removes the optional
// field so older projects retain their canonical shape.
[[nodiscard]] SourceTextResult set_source_placement_minimum_players_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view node_id,
    std::string_view placement_id,
    std::int64_t minimum_players
);

// Assigns an allied placement to player one or two in local co-op. Player one
// is stored by omission so existing projects retain their canonical shape.
[[nodiscard]] SourceTextResult set_source_placement_player_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view node_id,
    std::string_view placement_id,
    std::int64_t player
);

// Adds or removes an opening encounter placement by portable identities.
// Allied placements name the campaign member they field; enemy placements
// omit that identity. Both operations preserve playability when the input was
// already playable.
[[nodiscard]] SourceTextResult add_source_placement_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view node_id,
    std::string_view placement_id,
    std::string_view unit_type_id,
    std::string_view side,
    std::string_view member_id,
    std::int64_t x,
    std::int64_t y
);

[[nodiscard]] SourceTextResult remove_source_placement_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view node_id,
    std::string_view placement_id
);

// Replaces an encounter's complete deployment region. The deployment identity
// is created when no rule exists and must match an existing rule; capacity and
// notes are preserved. An empty region removes the deployment object only when
// it carries no capacity, so clearing tiles cannot silently clear another
// authoring rule. A previously playable project remains playable.
[[nodiscard]] SourceTextResult set_source_deployment_tiles_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view node_id,
    std::string_view deployment_id,
    const std::vector<SourceMapCell>& tiles
);

// Creates one objective definition and attaches it to an encounter as one
// validated edit. Target identity and round count follow the canonical kind's
// exact vocabulary; values no selected kind can read are refused rather than
// silently omitted. Existing objective definitions are never replaced.
[[nodiscard]] SourceTextResult add_source_encounter_objective_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view node_id,
    std::string_view objective_id,
    std::string_view name,
    std::string_view kind,
    std::string_view side,
    std::string_view target_placement_id,
    std::int64_t rounds
);

// Replaces an encounter's objective membership with existing portable
// objective identities. At least one objective is required, because a board
// with no decision cannot be opened by any runtime. Shared definitions remain
// in the project when one encounter stops using them.
[[nodiscard]] SourceTextResult set_source_encounter_objectives_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view node_id,
    const std::vector<std::string>& objective_ids
);

// Replaces one authored dialogue line without rebuilding the scene, cast, or
// campaign references. Speaker and text remain ordinary portable source data;
// no editor path or machine-local configuration can cross this boundary.
[[nodiscard]] SourceTextResult update_source_dialogue_line_json(
    std::string_view source_json,
    std::string_view dialogue_id,
    std::int64_t line_index,
    std::string_view speaker,
    std::string_view line_text
);

// Inserts, removes, or reorders complete lines inside an existing scene. The
// commands use the schema's portable text bounds and preserve at least one
// line, so the native creator cannot leave a scene in an unusable half-state.
[[nodiscard]] SourceTextResult insert_source_dialogue_line_json(
    std::string_view source_json,
    std::string_view dialogue_id,
    std::int64_t after_line_index,
    std::string_view speaker,
    std::string_view line_text
);

[[nodiscard]] SourceTextResult remove_source_dialogue_line_json(
    std::string_view source_json,
    std::string_view dialogue_id,
    std::int64_t line_index
);

[[nodiscard]] SourceTextResult move_source_dialogue_line_json(
    std::string_view source_json,
    std::string_view dialogue_id,
    std::int64_t line_index,
    std::int64_t new_index
);

// Creates one complete scene and attaches it to an existing story node as one
// validated edit. The opening speaker may name a unit type for portable
// portraits; backdrop may be empty to retain the ordinary flat presentation.
[[nodiscard]] SourceTextResult add_source_dialogue_scene_json(
    std::string_view source_json,
    std::string_view campaign_id,
    std::string_view node_id,
    std::string_view dialogue_id,
    std::string_view name,
    std::string_view background_id,
    std::string_view speaker,
    std::string_view unit_type_id,
    std::string_view line_text
);

}  // namespace grandleon::game_content
