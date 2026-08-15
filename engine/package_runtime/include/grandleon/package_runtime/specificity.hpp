// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// What an author wrote about one character beyond their unit type, decoded out
// of a package.
//
// This is content and only content: numbers read back exactly as they were
// written. No rule lives here. *Applying* a specificity is a campaign rule and
// lives in `engine/campaign_runtime`, in the same pass and one step before the
// rule that applies what a character earned. That is the whole design: an
// authored specificity is a gain that was never earned.
//
// It has its own header rather than sharing `campaign.hpp` because two
// unrelated places need the shape: the campaign record decodes it, and
// `EncounterLoadResult` carries it to the join. A board is not a campaign and
// must not have to include one to be handed a table.

namespace grandleon::package_runtime {

// Which stats an author may write a delta over, and the order they are indexed
// in.
//
// The first ten are `GrowableStat`'s ten, at `GrowableStat`'s own indices, so a
// reader who knows one vector knows the other and neither can drift into
// meaning the other's numbers. `speed` is eleventh because it is the one stat
// the two lists differ by.
//
// `speed` is here and is deliberately not growable, and the difference is not
// an oversight. Growth refuses it because a level-up is a roll, and a roll that
// reshuffles turn order changes the shape of a battle the player is already
// standing in, at a moment they did not choose, differently on different
// playthroughs. An authored delta is none of those things: it draws from no
// stream, it is fixed before the campaign is founded, it is identical on every
// playthrough and every platform, and it is on the info sheet before the player
// commits to anything. It is exactly as surprising as a unit type authoring a
// different speed, which is to say not at all, and which is the same fast
// knight an author can already have by writing a second class.
//
// `power`, `accuracy` and the reach band are absent because a unit type does
// not author them either: they are resolved from the weapon in hand.
// `MemberSpecificity::reach_bonus` below is how a character reaches the band.
//
// The list is append-only, because the values are indexed into and encoded.
enum class SpecificStat : std::uint8_t {
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
    speed = 10,
};

inline constexpr std::size_t specific_stat_count = 11;

// Everything one member's record says about them beyond their unit type.
//
// A fixed array rather than a sparse list, because eleven signed numbers are
// twenty-two bytes and a console that decodes this must not allocate to hold
// it. The *encoding* is sparse: only the stats an author named are written.
// This is what the sparse form decodes into. Zero means the author said
// nothing about that stat, which is the only thing zero can mean: a stated
// zero is refused by the compiler, because an author who writes a number meant
// to change something.
struct MemberSpecificity final {
    // The authored member identity, which is also the placement source key a
    // board carries beside every unit that member stands in. One identity, one
    // lookup, and nothing that could disagree with the exclusion pass about who
    // a unit is.
    std::uint64_t member_id{};
    // Added to whatever the unit type says, indexed by `SpecificStat`.
    std::array<std::int16_t, specific_stat_count> stat_deltas{};
    // Added to the maximum of the band of every weapon this member strikes
    // with. See `simulation::UnitDefinition::reach_bonus`, which is where it
    // ends up and where the reasoning about the band lives.
    std::uint8_t reach_bonus{};

    // Whether this record says anything at all. A member who authors nothing
    // is never written into a package, so this is false only for a table a
    // caller built by hand.
    [[nodiscard]] bool empty() const noexcept {
        if (reach_bonus != 0U) return false;
        for (const std::int16_t delta : stat_deltas) {
            if (delta != 0) return false;
        }
        return true;
    }
};

}  // namespace grandleon::package_runtime
