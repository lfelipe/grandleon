// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/simulation/encounter.hpp>

#include <limits>

namespace grandleon::simulation {

struct CampaignStatistics final {
    std::uint32_t battles{};
    std::uint32_t enemies_encountered{};
    std::uint32_t enemies_defeated{};
    std::uint32_t allies_fallen{};
    std::uint32_t hidden_items_found{};
    std::uint32_t conversations{};
    // The history length when the last battle was lost, or zero after a win.
    // If the graph still stands at that visit, there was no route out of defeat.
    std::uint32_t lost_visit{};

    [[nodiscard]] unsigned enemy_defeat_percentage() const noexcept {
        if (enemies_encountered == 0U) return 0U;
        return static_cast<unsigned>(
            (100ULL * enemies_defeated + enemies_encountered / 2U) / enemies_encountered
        );
    }
};

inline void tally_battle(
    CampaignStatistics& totals,
    const EncounterSnapshot& snapshot,
    const std::vector<Event>& events,
    std::uint32_t visit
) noexcept {
    const auto increment = [](std::uint32_t& count) {
        if (count < std::numeric_limits<std::uint32_t>::max()) ++count;
    };
    increment(totals.battles);
    totals.lost_visit = snapshot.outcome == Outcome::second_side_won ? visit : 0U;
    for (const auto& unit : snapshot.units) {
        if (!unit.arrived) continue;
        const bool fell = unit.health <= 0 && !unit.departed;
        if (unit.side == Side::second) {
            increment(totals.enemies_encountered);
            if (fell) increment(totals.enemies_defeated);
        } else if (fell) {
            increment(totals.allies_fallen);
        }
    }
    for (const auto& event : events) {
        if (event.type == EventType::hidden_item_found) increment(totals.hidden_items_found);
        if (event.type == EventType::unit_talked) increment(totals.conversations);
    }
}

}  // namespace grandleon::simulation
