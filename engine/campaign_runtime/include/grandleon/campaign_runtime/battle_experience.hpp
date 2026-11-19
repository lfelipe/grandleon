// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/outcome.hpp>
#include <grandleon/core/starting_stats.hpp>
#include <grandleon/package_runtime/progression.hpp>
#include <grandleon/package_runtime/specificity.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <array>
#include <string_view>
#include <vector>

namespace grandleon::campaign_runtime {

// The bound every growth chance is rolled against. Chances are authored as
// whole percentages and rolled as whole percentages: one number, one meaning,
// and the number an author writes is the number this divides by, the same
// honesty rule accuracy keeps (`engine/simulation/src/encounter.cpp`).
inline constexpr std::uint32_t growth_chance_bound = 100U;

// One character's level-up, as it happened. Not a rule and not state: a record
// of what the rolls said, so a surface can narrate it without re-deriving it
// and a test can assert it without a save.
struct LevelUp final {
    campaign::PersistentEntityId member{};
    std::uint16_t from_level{};
    std::uint16_t to_level{};
    // Points gained per stat across every level in this batch, indexed by
    // `campaign::GrowableStat`.
    std::array<std::uint16_t, campaign::growable_stat_count> points{};
};

struct ExperienceReward final {
    campaign::PersistentEntityId member{};
    std::uint32_t experience{};
    std::uint16_t previous_level{};
    std::uint16_t level{};
};

enum class ProgressionSourceError : std::uint8_t {
    none = 0,
    // A board unit bound to a campaign member the roster does not hold, or
    // holds as somebody who could not have been there. The batch is refused
    // rather than built, because an operation `apply_outcome` would reject is
    // worse than no operation at all.
    unknown_member,
    // A unit type the package does not carry, or whose growth block is not one.
    unreadable_unit_type,
};

[[nodiscard]] std::string_view progression_source_error_name(
    ProgressionSourceError error
) noexcept;

struct BattleProgression final {
    ProgressionSourceError error{ProgressionSourceError::none};
    // The consequences, ready to go into a `CampaignOutcomeBatch` beside
    // whatever else the battle decided.
    //
    // The progression half comes first, ascending by member, and within a
    // member: the experience, then the levels, then what the levels gave. The
    // caller may append inventory and other consequences after it.
    std::vector<campaign::CampaignOutcomeOperation> operations;
    // One entry per member who reached a new level, ascending by member.
    std::vector<LevelUp> level_ups;
    std::vector<ExperienceReward> rewards;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ProgressionSourceError::none;
    }
};

struct BoardUnitType final {
    simulation::UnitId unit{};
    std::uint64_t unit_type_id{};
    simulation::Side side{simulation::Side::first};
};

struct BattleExperienceSource final {
    const void* context{};
    campaign::PersistentEntityId (*member_of)(const void*, simulation::UnitId);
    package_runtime::UnitProgressionLoad (*profile_of)(const void*, std::uint64_t);
};

// For players that keep a compact battle log. Defeats supply both kill credit
// and the list of fallen earners. Healing only needs activation boundaries for
// units that restored health to someone during that activation.
[[nodiscard]] bool retain_experience_event(
    const std::vector<simulation::Event>& retained,
    const simulation::Event& event,
    bool healing_rewards
) noexcept;

[[nodiscard]] BattleProgression derive_roster_experience(
    const campaign::CampaignState& state,
    const std::vector<BoardUnitType>& board,
    const std::vector<simulation::Event>& events,
    const campaign::OutcomeSource& source,
    BattleExperienceSource lookups
);

void apply_roster_growth(simulation::UnitDefinition& unit,
                         const campaign::Progression& progression) noexcept;

[[nodiscard]] campaign::DefinitionRef starting_seed_key(const core::PackageId& package) noexcept;

[[nodiscard]] std::uint64_t starting_seed(const campaign::CampaignState& state,
                                          const core::PackageId& package) noexcept;

void append_character_start(std::vector<campaign::CampaignOutcomeOperation>& operations,
    const campaign::DefinitionRef& role, campaign::PersistentEntityId member,
    const core::CharacterStart& start, const core::RoleStartingStats& stats,
    const package_runtime::UnitProgression& progression, std::uint64_t seed);

void apply_starting_stats(simulation::UnitDefinition& unit, const campaign::CampaignState& state,
    const campaign::DefinitionRef& role, campaign::PersistentEntityId member) noexcept;


void apply_authored_specificity(
    simulation::UnitDefinition& unit,
    const package_runtime::MemberSpecificity& specificity
) noexcept;

}  // namespace grandleon::campaign_runtime
