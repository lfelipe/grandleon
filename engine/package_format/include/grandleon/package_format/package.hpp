// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/core/content_identity.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace grandleon::package_format {

inline constexpr std::uint16_t container_major = 0;
inline constexpr std::uint16_t container_minor = 1;
inline constexpr std::uint32_t section_flag_required = 1U;

struct Version final {
    std::uint16_t major{};
    std::uint16_t minor{};
    std::uint16_t patch{};
};

[[nodiscard]] constexpr bool operator==(Version lhs, Version rhs) noexcept {
    return lhs.major == rhs.major && lhs.minor == rhs.minor &&
           lhs.patch == rhs.patch;
}

[[nodiscard]] constexpr bool operator<(Version lhs, Version rhs) noexcept {
    if (lhs.major != rhs.major) {
        return lhs.major < rhs.major;
    }
    if (lhs.minor != rhs.minor) {
        return lhs.minor < rhs.minor;
    }
    return lhs.patch < rhs.patch;
}

struct VersionRange final {
    Version minimum{};
    Version maximum{};

    [[nodiscard]] constexpr bool contains(Version value) const noexcept {
        return !(value < minimum) && !(maximum < value);
    }
};

// Values are serialized and therefore append-only. 3 and 4 are spent and stay
// spent: a package written against them exists, and reusing either number would
// make an old file claim a machine it was never built for. The next profile
// added takes 5.
enum class TargetProfile : std::uint32_t {
    portable = 0,
    desktop = 1,
    nintendo64 = 2,
};

// Values are serialized and therefore append-only.
enum class SectionType : std::uint32_t {
    manifest = 1,
    classes = 2,
    unit_types = 3,
    weapons = 4,
    items = 5,
    maps = 6,
    dialogue = 7,
    presentation = 8,
    weapon_types = 9,
    item_types = 10,
    factions = 11,
    objectives = 12,
    encounters = 13,
    campaigns = 14,
    abilities = 15,
    // Who can be talked to, per encounter. A section of its own rather than a
    // tail on the encounter record, because that record already ends in a
    // positional deployment tail whose capacity is discriminated by "are there
    // bytes left". A further tail after it could not be told apart from a
    // capacity. Sharing bytes with nothing makes the byte-identity claim total:
    // a package where nobody is talkable has no directory entry here at all.
    talks = 16,
    // When a placement comes in, per encounter. A section of its own for the
    // reason `talks` is one, and it is the same reason: the encounter record
    // already ends in a positional deployment tail discriminated by whether
    // bytes remain, so nothing further can be told apart from it. A package no
    // encounter of which authors a wave has no directory entry here at all.
    arrivals = 17,
    // What the author called one placement, keyed by the placement's own
    // identity. A section of its own for the reason `talks` is one, and pruned
    // the same way: a project where nobody names a placement has no directory
    // entry here at all, so every package authored without one is byte for byte
    // the package it was.
    //
    // Keyed by placement identity rather than by encounter, unlike the two
    // above, and that is what the record can afford to be: a placement's
    // identity is already namespaced by the encounter it stands on
    // (`tools/game_content/src/compiler.cpp` hashes `encounter/placement`), so
    // it is unique across a whole project and one record per name needs no
    // enclosing list. The record is then exactly a length-prefixed string in
    // the leading position every named section writes one in, which is why
    // `package_runtime::content_name` reads this section with no new decoder.
    //
    // **Optional**, where `talks` and `arrivals` are required, and the
    // difference is the whole of what a name is. A runtime that skipped a talk
    // or a wave would play a different battle; a runtime that skips this draws
    // the same battle under a derived name. Nothing here is read by a rule.
    placement_names = 18,
    // What is said during a battle and what has to happen for it to be said,
    // per encounter. A section of its own for the reason `talks` is one: the
    // encounter record already ends in a positional deployment tail that only
    // "are there bytes left" tells apart, so nothing further can be appended to
    // it and be read back. Pruned the same way, so an encounter nobody speaks
    // during has no record and a project with none has no directory entry.
    //
    // **Optional**, where `talks` and `arrivals` are required, and on the same
    // test those two are judged by: a runtime skipping a talk or a wave plays a
    // different battle, and one skipping this plays the same battle in silence.
    // Nothing here is read by a rule; the engine never learns a moment exists.
    moments = 19,
};

// The presentation section holds records about the whole game, the way the
// manifest section does, so their identities are format constants rather than
// values a game could collide with. Append-only, like the section types above.
//
// The project record carries the theme and every faction's resolved colour.
// The ones beside it carry the joins a client needs to draw content it can
// otherwise only identify: a cell's identity is a hash of an authored terrain
// name, and a unit type's archetype, the style that draws it and the body it
// is drawn with all follow from choices no client should have to re-derive.
inline constexpr std::uint64_t presentation_record_id = 1;

// The manifest's own record, on the same terms: it says something about the
// whole game rather than about a piece of content, so its identity is a format
// constant. It carries the project's title as a length-prefixed string, and it
// is the only record the manifest section has ever held. It is named here
// because two sides read it, and a number written twice is a number that can
// be written differently twice.
inline constexpr std::uint64_t manifest_title_record_id = 1;
inline constexpr std::uint64_t presentation_terrain_record_id = 2;
inline constexpr std::uint64_t presentation_archetype_record_id = 3;
// Which of the art library's character styles each unit type is drawn in,
// beside the archetype record's answer to which drawing it is. A record of its
// own rather than a second byte on the archetype record, for one reason and it
// is the load-bearing one: a game where every character follows the game's
// style has nothing to say here, and a record that is absent costs nothing.
// A widened archetype entry would instead move every existing package's bytes
// to say what they already said. So this record is written only when some
// character names a style of its own, and every game authored before per-
// character styles existed compiles byte-identical.
inline constexpr std::uint64_t presentation_style_record_id = 4;
// And which body each is drawn with, on the same terms. A record of its own
// rather than a second byte on the one above, because the two axes are chosen
// independently: a game that dresses one character in another setting's style
// says nothing about figures, and a game that draws one character at the
// second build says nothing about styles. Each pays only for what it authored,
// and a reader that knows one of them finds it by its identity and is
// unaffected by the other.
inline constexpr std::uint64_t presentation_figure_record_id = 5;

struct RecordSource final {
    std::uint64_t stable_id{};
    std::vector<std::uint8_t> payload;
};

struct SectionSource final {
    SectionType type{};
    std::uint16_t schema_major{1};
    std::uint16_t schema_minor{0};
    std::uint32_t flags{section_flag_required};
    std::vector<RecordSource> records;
};

struct PackageSource final {
    core::PackageId game_id{};
    std::uint32_t content_revision{};
    VersionRange required_engine{};
    TargetProfile target{TargetProfile::portable};
    std::uint64_t required_features{};
    std::vector<SectionSource> sections;
};

enum class Error : std::uint8_t {
    none = 0,
    truncated,
    invalid_magic,
    unsupported_container,
    incompatible_engine,
    incompatible_target,
    unsupported_feature,
    unsupported_required_section,
    unsupported_schema,
    invalid_directory,
    duplicate_section,
    invalid_section,
    checksum_mismatch,
    invalid_record,
    duplicate_record,
};

[[nodiscard]] std::string_view error_name(Error error) noexcept;

struct LoadOptions final {
    Version engine_version{};
    TargetProfile target{TargetProfile::desktop};
    std::uint64_t supported_features{};
    std::uint32_t maximum_sections{1024};
    std::uint32_t maximum_records_per_section{1'000'000};
    // How many characters one board may put on the field before this target
    // refuses it. Zero is the default and means no budget at all, which is what
    // a host with four gigabytes wants and what every caller written before
    // this said.
    //
    // **This is the working-set budget DESIGN.md §6 promises and nothing here
    // used to supply.** The two numbers above bound what a package *is*; this
    // bounds what playing one *costs*, which is a different question and the
    // one a console actually loses on. `simulation::danger_tiles` runs one
    // movement search per character on a side, so the cost of the queries a
    // single button press triggers climbs steeply with the number of characters
    // standing on the board.
    //
    // Measured on Nintendo 64 hardware timing, through the console's own COP0
    // clock under the pinned emulator, timing the pair of queries picking a
    // character up actually triggers: where it may walk, and which of those
    // tiles the other side threatens.
    //
    // **Set from the worst case, which is a board whose two sides are
    // engaged.** The warning skips a character too far from the lit tiles to
    // threaten one, so a board where the opposition stands off costs almost
    // nothing however many of them there are: the same 20x14 board with the
    // sides on opposite edges answers a press in well under a millisecond at
    // every count measured. That is the common case and it is not what a budget
    // is for. Packed around a line that still has room to walk, on one 20x14
    // board with only the character count varied:
    //
    //     characters   per press   frames at 60Hz
    //         20          9.5 ms        0.6
    //         32         28.2 ms        1.7
    //         48         34.5 ms        2.1
    //
    // Past that the board stops answering: a 98-character board loaded, opened,
    // and would not take a press, which is the failure this bound exists for. A
    // machine that answers in two frames is slow; one that answers in half a
    // second reads as broken.
    //
    // Appended last, so every caller that brace-initialises this by position
    // keeps meaning what it meant.
    std::uint32_t maximum_units_per_encounter{0};
};

struct RecordView final {
    std::uint64_t stable_id{};
    std::uint32_t payload_offset{};
    std::uint32_t payload_size{};
};

struct SectionView final {
    SectionType type{};
    std::uint16_t schema_major{};
    std::uint16_t schema_minor{};
    std::uint32_t flags{};
    std::vector<RecordView> records;
};

// The bytes of a package, wherever the caller already keeps them.
//
// Two members rather than a `std::span`, because the documented subset here is
// C++17. Nothing is owned: whoever hands these over promises they stay where
// they are for as long as the package loaded from them is used.
struct PackageBytes final {
    const std::uint8_t* data{nullptr};
    std::size_t size{};
};

struct LoadedPackage final {
    core::PackageId game_id{};
    std::uint32_t content_revision{};
    VersionRange required_engine{};
    TargetProfile target{};
    std::uint64_t required_features{};
    // The package's bytes, when this load copied them. Empty when it did not,
    // which is what `borrowed` below is for. Public and a `std::vector` so that
    // a caller can look at a package it opened the ordinary way, and because a
    // caller assembling a package out of records that arrive one at a time
    // appends to it, as the browser's campaign entry points do.
    //
    // Appending is safe, and it is safe for the same reason copying is: nothing
    // decoded out of a package holds a pointer into these bytes. Every view is
    // an offset resolved through `byte_data()` at the moment of asking, so a
    // reallocation moves the buffer and changes no answer. Whoever adds a
    // decoded form that borrows from here owes that shape; a captured pointer
    // would be a use-after-free the first time somebody appended a record.
    std::vector<std::uint8_t> bytes;
    // Where the package's bytes are when this load did not copy them. Read
    // through `byte_data()` rather than directly: the rule that picks between
    // the two is what makes a `LoadedPackage` safe to copy.
    PackageBytes borrowed{};
    std::vector<SectionView> sections;
    // The working-set budget this package was opened under, carried forward
    // from `LoadOptions` so that whatever decodes a board out of it can hold
    // the board to the same limit. Zero is no budget, which is every caller
    // that names none.
    //
    // Carried rather than passed again because it is a fact about the machine
    // that opened the package, not about the board being asked for, and a
    // second parameter on every decoder would be the same number asked for
    // twice with two chances to disagree.
    std::uint32_t maximum_units_per_encounter{0};

    // Where this package's bytes are, whoever owns them. Every offset in every
    // `RecordView` is relative to this pointer.
    //
    // The choice is made on every call rather than cached, and that is the
    // point: a cached pointer into `bytes` would be a pointer into somebody
    // else's vector the moment a `LoadedPackage` was copied, while this answers
    // the copy's own buffer for an owning package and the caller's bytes for a
    // borrowing one, which is where the bytes still are in both cases.
    [[nodiscard]] const std::uint8_t* byte_data() const noexcept {
        return bytes.empty() ? borrowed.data : bytes.data();
    }

    [[nodiscard]] std::size_t byte_size() const noexcept {
        return bytes.empty() ? borrowed.size : bytes.size();
    }

    [[nodiscard]] const SectionView* find(SectionType type) const noexcept;
    [[nodiscard]] const RecordView* find(
        SectionType type,
        std::uint64_t stable_id
    ) const noexcept;
    [[nodiscard]] const RecordView* find(
        const core::ContentRef& reference
    ) const noexcept;
};

struct LoadResult final {
    Error error{Error::none};
    LoadedPackage package;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Error::none;
    }
};

// Experimental host-side writer. The envelope is not a released file format.
[[nodiscard]] std::vector<std::uint8_t> write_mock_package(
    const PackageSource& source
);

// Validates the entire package before returning any content.
//
// The loaded package owns a copy of `bytes`, so the caller's vector may go away
// the moment this returns. That is what nearly every caller wants and it is
// what this function does.
[[nodiscard]] LoadResult load_mock_package(
    const std::vector<std::uint8_t>& bytes,
    const LoadOptions& options
);

// The same load, over bytes the caller already has somewhere the package can
// keep reading them: a memory-mapped file, a static buffer, the read-only half
// of a cartridge.
//
// Nothing is copied, neither the input nor the result, so the caller owes the
// package exactly one thing: that `bytes` stays where it is, and stays what it
// is, for as long as the package is used. In exchange a machine whose content
// is already addressable spends none of its working memory on holding it twice.
// A cartridge is the caller this exists for: its content is already in the
// address space, and a console with kilobytes of heap cannot afford a copy.
//
// Every check the owning load makes is made here, because the owning load is
// this function plus a copy. The two cannot disagree about what a package
// means or about which packages are refused.
[[nodiscard]] LoadResult load_mock_package_in_place(
    PackageBytes bytes,
    const LoadOptions& options
);

}  // namespace grandleon::package_format
