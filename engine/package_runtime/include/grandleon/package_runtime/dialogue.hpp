// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace grandleon::package_runtime {

struct DialogueLine final {
    std::string speaker;
    std::string text;
    // Which of the scene's cast entries speaks this line, as the entry's
    // position plus one; zero is a line the scene names nobody for, which is
    // every line of every scene authored before a scene could name anybody.
    // Use `Dialogue::speaker_unit_type` rather than indexing this by hand.
    std::uint8_t cast_entry{0};
};

struct Dialogue final {
    std::uint64_t id{};
    std::string name;
    // Presentation order, exactly as authored.
    std::vector<DialogueLine> lines;
    // The unit type identity of each speaker this scene cast, in authored
    // order. A client that draws a face asks the package's presentation
    // records what that unit type wears (its style, its figure, its
    // archetype and its colour) and draws the character the board draws.
    // Empty is a scene that cast nobody. Presentation only: no rule reads it.
    std::vector<std::uint64_t> cast;
    // What the scene is drawn against, as the art library's backdrop menu
    // index plus one; zero is a scene that names none, which is every scene
    // authored before backdrops existed. Presentation only: a client that
    // ignores it draws the scene exactly as it did before.
    std::uint8_t backdrop{0};

    // The unit type whose character speaks this line, or `nullptr` when the
    // scene named nobody for it. A pointer rather than a sentinel because a
    // unit type identity is a hash and no value of it is reserved: there is
    // no number this could return that means "none" and could not also mean
    // some project's unit type.
    [[nodiscard]] const std::uint64_t* speaker_unit_type(
        const DialogueLine& line
    ) const noexcept {
        if (line.cast_entry == 0 || line.cast_entry > cast.size()) {
            return nullptr;
        }
        return &cast[line.cast_entry - 1];
    }
};

enum class DialogueError : std::uint8_t {
    none = 0,
    missing_section,
    missing_record,
    malformed_payload,
};

[[nodiscard]] std::string_view error_name(DialogueError error) noexcept;

struct DialogueLoadResult final {
    DialogueError error{DialogueError::none};
    Dialogue dialogue;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == DialogueError::none;
    }
};

// Decodes one dialogue record. A campaign story node names a dialogue; without
// this a story node can be advanced past but never read.
[[nodiscard]] DialogueLoadResult load_dialogue(
    const package_format::LoadedPackage& package,
    std::uint64_t dialogue_id
);

}  // namespace grandleon::package_runtime
