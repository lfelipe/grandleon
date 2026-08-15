// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

// What a unit type says about growing, decoded out of a package.
//
// This is content and only content: three authored numbers per unit type, read
// back exactly as they were written. No rule lives here. Who earns experience,
// when a level is reached, and what a level-up rolls are campaign rules, and
// they live in `engine/campaign_runtime` where a roster and a package are both
// in scope.
//
// It is separate from `encounter_loader.hpp` on purpose. A board never reads
// these numbers: a battle is battle-local, and the day the simulation learns
// what a level is, is the day a canonical hash depends on a save file. The
// encounter loader skips this block; this header is what decodes it, for the
// one caller above both that is allowed to care.

namespace grandleon::package_runtime {

// Which stats a level-up may add a point to, and the order it rolls them in.
//
// The order is a rule, not an implementation detail: it fixes the growth
// stream's consumption order, so it is written down in exactly three places
// that must agree (here, `game_content::GrowableStat` which encodes it, and
// `campaign::GrowableStat` which persists it) and the tests check them
// against each other.
//
// `speed` is deliberately not on it: it orders a whole turn rather than pricing
// one blow, so growing it silently reshuffles who acts when, and it deserves
// its own decision. Everything else the stat line holds is here.
//
// The list is append-only. `skill`, `luck`, `evasion` and `magic` were added at
// the end rather than beside the stats they read most like, because these
// values are indexed into and persisted: an existing growth seed's first six
// draws per level have to stay the six draws they always were.
enum class GrowableStat : std::uint8_t {
    health = 0,
    strength = 1,
    defense = 2,
    resistance = 3,
    movement = 4,
    action_points = 5,
    skill = 6,
    luck = 7,
    evasion = 8,
    magic = 9,
};

inline constexpr std::size_t growable_stat_count = 10;

// How many chances the block carried before the richer stat line appended four
// more. A record of that length is a unit type from an older package, and the
// four it does not carry are zero, which is what "never grows that stat" has
// always encoded, so the shorter block is read rather than refused.
inline constexpr std::size_t growable_stat_count_before_richer_line = 6;

// The bytes the growth block occupies at the end of a unit type record: the
// award, the per-level threshold, and one chance per growable stat.
inline constexpr std::size_t unit_type_progression_size =
    2U + 2U + growable_stat_count;

inline constexpr std::size_t unit_type_progression_size_before_richer_line =
    2U + 2U + growable_stat_count_before_richer_line;

// The bytes that follow the growth block: what the unit type leaves behind
// when it falls, and how often. Nothing on this side of the boundary reads
// them (a drop is a battle rule and reaches the board through the encounter
// loader), but both decoders of a unit type record have to agree about how
// long the record is, so the length is stated once, here, beside the other
// two.
//
// A record that ends before these nine bytes is a unit type from a package
// written before a defeated character left anything behind, which is exactly
// what a zero identity and a zero chance say.
inline constexpr std::size_t unit_type_drop_size = 8U + 1U;

// What every unit type says when its author said nothing, and therefore what
// every package written before growth existed says.
inline constexpr std::uint16_t default_experience_per_level = 100;

struct UnitProgression final {
    // The experience defeating one of these grants to whoever felled it. Zero
    // is worth nothing, which is what an unauthored unit type is worth.
    std::uint16_t experience_award{};
    // Lifetime experience per level for a character of this type. Never zero:
    // a threshold no experience reaches is not a threshold, and the compiler
    // refuses one.
    std::uint16_t experience_per_level{default_experience_per_level};
    // How often each stat gains a point on a level-up, as whole percentages in
    // [0, 100], indexed by `GrowableStat`. The authored number is the rolled
    // number.
    std::array<std::uint8_t, growable_stat_count> growth{};

    [[nodiscard]] std::uint8_t chance_of(GrowableStat stat) const noexcept {
        return growth[static_cast<std::size_t>(stat)];
    }

    // Whether this unit type grows at all. A type that cannot grow draws no
    // number on any level-up, which is what keeps the growth stream's
    // consumption order checkable against the content.
    [[nodiscard]] bool grows() const noexcept {
        for (std::uint8_t chance : growth) {
            if (chance != 0U) return true;
        }
        return false;
    }
};

struct UnitProgressionLoad final {
    // False when the section, the record, or the bytes were not there or not
    // readable. A record with no growth block is a success carrying the
    // defaults: absence is what a package written before growth looks like,
    // and it is not an error.
    bool found{false};
    UnitProgression progression;

    [[nodiscard]] explicit operator bool() const noexcept { return found; }
};

// Decode one unit type's growth block.
//
// The record's layout is written by `tools/game_content/src/compiler.cpp` and
// read in two places: the encounter loader, which skips this block because a
// board has no use for it, and here.
[[nodiscard]] UnitProgressionLoad load_unit_progression(
    const package_format::LoadedPackage& package,
    std::uint64_t unit_type_id
);

}  // namespace grandleon::package_runtime
