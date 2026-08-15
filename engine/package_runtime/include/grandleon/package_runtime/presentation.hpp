// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace grandleon::package_runtime {

enum class PresentationError : std::uint8_t {
    none = 0,
    malformed_payload,
};

[[nodiscard]] std::string_view error_name(PresentationError error) noexcept;

//: The theme a project that named none is drawn in: the first on the art
//: library's menu, which substitutes nothing. A package carrying no
//: presentation section resolves to it, so such a package renders exactly as
//: it did before the section existed.
inline constexpr std::uint8_t default_theme = 0;

//: No faction claims this unit type, so the client falls back to the side the
//: unit fights on. Also what every unit type resolves to when the package
//: carries no presentation section.
inline constexpr std::uint8_t colour_unresolved = 0xFF;

//: This package says nothing about that terrain identity, so a client draws
//: whatever it drew before it could ask. Every identity resolves to this when
//: the package carries no terrain join.
//:
//: Deliberately not the compiler's "no art-library keyword matched this name",
//: which is a smaller number and a different fact: that one the compiler
//: established and carried, and a client may draw a considered fallback for
//: it. Conflating the two draws the wrong thing for one of them.
inline constexpr std::uint8_t terrain_kind_unresolved = 0xFF;

//: This package says nothing about that unit type's archetype. Every unit type
//: resolves to this when the package carries no archetype join.
inline constexpr std::uint8_t archetype_unresolved = 0xFF;

//: This package says nothing about that unit type's character style, so the
//: client draws it in whatever style it was going to draw the whole game in.
//: Every unit type resolves to this when the package carries no style join,
//: and every package written by a game where no character names a style of its
//: own carries none, which is why this reads as "unchanged" rather than as a
//: missing answer. The style a whole game is drawn in has never been a package
//: byte: a console build resolves it from the project it embeds.
inline constexpr std::uint8_t character_style_unresolved = 0xFF;

//: This package says nothing about that unit type's figure, so the client
//: draws it with whatever body it was going to draw the whole game with. Read
//: exactly as `character_style_unresolved` above, and absent for the same
//: reason: a game where no character names a figure of its own carries no
//: figure join.
inline constexpr std::uint8_t character_figure_unresolved = 0xFF;

struct FactionColour final {
    std::uint64_t faction_id{};
    //: Already resolved by the compiler: an index into the art library's
    //: colour menu, never the "unchosen" marker. The rule that an unchosen
    //: colour follows the faction's position in the project's list is
    //: authoring semantics and is applied before the package is written.
    std::uint8_t colour{};
};

struct UnitTypeColour final {
    std::uint64_t unit_type_id{};
    //: `colour_unresolved` when the unit type names no faction, or names one
    //: the presentation section does not hold.
    std::uint8_t colour{colour_unresolved};
};

struct TerrainKind final {
    std::uint64_t terrain_id{};
    //: Already resolved by the compiler: an index into the art library's
    //: terrain registry, or its "no keyword matched this name" value. The
    //: keyword convention that chose it is authoring semantics and is applied
    //: before the package is written, because a cell's identity is a hash of
    //: the authored name and a client cannot recover the name to match.
    std::uint8_t kind{};
};

struct UnitTypeArchetype final {
    std::uint64_t unit_type_id{};
    //: Already resolved by the compiler: an index into the art library's
    //: archetype roster. A unit type whose class and whose own name spell no
    //: archetype carries the roster's first, so this is always drawable.
    std::uint8_t archetype{};
};

struct UnitTypeCharacterStyle final {
    std::uint64_t unit_type_id{};
    //: Already resolved by the compiler: an index into the art library's
    //: character style menu. A unit type that named no style of its own
    //: carries the game's, so this is always drawable and a client never has
    //: to know which of the two authored it.
    std::uint8_t style{};
};

struct UnitTypeCharacterFigure final {
    std::uint64_t unit_type_id{};
    //: Already resolved by the compiler: an index into the art library's
    //: figure menu, on the same terms as the style beside it.
    std::uint8_t figure{};
};

// The authored presentation choices a package carries. Purely presentational:
// the simulation neither sees nor validates any of it, and changing it cannot
// move a canonical hash.
struct Presentation final {
    std::uint8_t theme{default_theme};
    // Sorted by faction identity.
    std::vector<FactionColour> factions;
    // Sorted by unit type identity. The join a client actually wants: a unit
    // snapshot names its unit type, and this says what colour that unit type's
    // characters wear.
    std::vector<UnitTypeColour> unit_types;
    // Sorted by terrain identity. What a map cell draws as: a cell carries
    // only its identity, and this is the only thing that turns one into a
    // picture.
    std::vector<TerrainKind> terrain;
    // Sorted by unit type identity. Which of the art library's archetypes a
    // unit type wears, beside the colour it wears it in.
    std::vector<UnitTypeArchetype> archetypes;
    // Sorted by unit type identity. Which of the art library's styles draws
    // that archetype, beside the archetype itself. Empty in every package
    // whose game draws every character in the one style it names, which is
    // every package written before a character could name its own.
    std::vector<UnitTypeCharacterStyle> character_styles;
    // Sorted by unit type identity. Which body draws that archetype, beside
    // whose hand drew it. Empty for the same reason and in the same games.
    std::vector<UnitTypeCharacterFigure> character_figures;

    [[nodiscard]] std::uint8_t colour_of_faction(
        std::uint64_t faction_id
    ) const noexcept;
    [[nodiscard]] std::uint8_t colour_of_unit_type(
        std::uint64_t unit_type_id
    ) const noexcept;
    [[nodiscard]] std::uint8_t kind_of_terrain(
        std::uint64_t terrain_id
    ) const noexcept;
    [[nodiscard]] std::uint8_t archetype_of_unit_type(
        std::uint64_t unit_type_id
    ) const noexcept;
    [[nodiscard]] std::uint8_t character_style_of_unit_type(
        std::uint64_t unit_type_id
    ) const noexcept;
    [[nodiscard]] std::uint8_t character_figure_of_unit_type(
        std::uint64_t unit_type_id
    ) const noexcept;
};

struct PresentationLoadResult final {
    PresentationError error{PresentationError::none};
    Presentation presentation;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == PresentationError::none;
    }
};

// Decodes the package's presentation section, if it has one.
//
// A package without the section is not an error: it resolves to the default
// theme, to no faction colours, and to no resolved terrain kind or archetype,
// which is what a client drew before the section existed. The same is true one
// record at a time: a package whose presentation section holds only the
// project record (every package written before the content joins existed)
// resolves every terrain identity and every unit type to "unresolved". A
// record that is present but malformed is refused, and no partially decoded
// presentation is published.
[[nodiscard]] PresentationLoadResult load_presentation(
    const package_format::LoadedPackage& package
);

}  // namespace grandleon::package_runtime
