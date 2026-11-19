// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/outcome.hpp>
#include <grandleon/simulation/battle_statistics.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace grandleon::campaign_runtime {

namespace detail {

struct StatisticField final {
    // Slashes reserve these keys from authored world flags.
    std::string_view key;
    std::uint32_t simulation::CampaignStatistics::*value;
};

inline constexpr std::array<StatisticField, 7> campaign_statistics_fields{{
    {"grandleon.stats/battles", &simulation::CampaignStatistics::battles},
    {"grandleon.stats/enemies_encountered", &simulation::CampaignStatistics::enemies_encountered},
    {"grandleon.stats/enemies_defeated", &simulation::CampaignStatistics::enemies_defeated},
    {"grandleon.stats/allies_fallen", &simulation::CampaignStatistics::allies_fallen},
    {"grandleon.stats/hidden_items_found", &simulation::CampaignStatistics::hidden_items_found},
    {"grandleon.stats/conversations", &simulation::CampaignStatistics::conversations},
    {"grandleon.stats/lost_visit", &simulation::CampaignStatistics::lost_visit},
}};

inline campaign::DefinitionRef statistic_key(
    const campaign::CampaignState& state, const StatisticField& field
) noexcept {
    return {state.progress.campaign.package_id, core::ContentCategory::world_flag,
        core::stable_content_id_v1(field.key)};
}

}  // namespace detail

[[nodiscard]] inline simulation::CampaignStatistics campaign_statistics(const campaign::CampaignState& state) noexcept {
    simulation::CampaignStatistics totals;
    for (const auto& field : detail::campaign_statistics_fields) {
        const auto* value = campaign::find_world_value(state, detail::statistic_key(state, field));
        if (value != nullptr && value->type == campaign::WorldValueType::integer && value->value >= 0) {
            totals.*(field.value) = static_cast<std::uint32_t>(std::min<std::int64_t>(
                value->value, std::numeric_limits<std::uint32_t>::max()
            ));
        }
    }
    return totals;
}

[[nodiscard]] inline bool defeated_without_route(const campaign::CampaignState& state) noexcept {
    const auto* lost = campaign::find_world_value(
        state, detail::statistic_key(state, detail::campaign_statistics_fields.back())
    );
    return lost != nullptr && lost->type == campaign::WorldValueType::integer &&
        lost->value > 0 && static_cast<std::uint64_t>(lost->value) == state.progress.history.size();
}

inline void append_defeat_marker(
    std::vector<campaign::CampaignOutcomeOperation>& operations,
    const campaign::CampaignState& state,
    simulation::Outcome outcome
) {
    if (outcome == simulation::Outcome::second_side_won) {
        operations.push_back(campaign::set_world_flag(
            detail::statistic_key(state, detail::campaign_statistics_fields.back()),
            {campaign::WorldValueType::integer,
                static_cast<std::int64_t>(state.progress.history.size())}
        ));
    }
}

inline void append_battle_statistics(
    std::vector<campaign::CampaignOutcomeOperation>& operations,
    const campaign::CampaignState& state,
    const simulation::EncounterSnapshot& snapshot,
    const std::vector<simulation::Event>& events
) {
    const auto before = campaign_statistics(state);
    auto after = before;
    simulation::tally_battle(after, snapshot, events, static_cast<std::uint32_t>(state.progress.history.size()));
    for (const auto& field : detail::campaign_statistics_fields) {
        if (before.*(field.value) != after.*(field.value)) {
            operations.push_back(campaign::set_world_flag(detail::statistic_key(state, field),
                {campaign::WorldValueType::integer, after.*(field.value)}));
        }
    }
}

}  // namespace grandleon::campaign_runtime
