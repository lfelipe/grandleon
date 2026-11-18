// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/outcome.hpp>
#include <grandleon/simulation/encounter.hpp>

namespace grandleon::campaign_runtime {

[[nodiscard]] inline bool route_consequence_event(simulation::EventType type) noexcept {
    return type == simulation::EventType::hidden_item_found ||
        type == simulation::EventType::unit_talked;
}

inline void append_event_consequences(
    std::vector<campaign::CampaignOutcomeOperation>& operations,
    const core::PackageId& package,
    const std::vector<simulation::Event>& events
) {
    // Routes read the authored identities of battlefield events. Those flags
    // belong to the campaign even when the actor is a temporary ally.
    for (const simulation::Event& event : events) {
        if (event.type == simulation::EventType::hidden_item_found && event.world_flag != 0) {
            operations.push_back(campaign::set_world_flag(
                {package, core::ContentCategory::world_flag, event.world_flag},
                {campaign::WorldValueType::boolean, 1}
            ));
            continue;
        }
        if (event.type != simulation::EventType::unit_talked) continue;
        if (event.content_id == 0U) continue;
        operations.push_back(
            campaign::set_world_flag(
                {package,
                 core::ContentCategory::world_flag,
                 event.content_id},
                {campaign::WorldValueType::boolean, 1}
            )
        );
        if (event.world_flag != 0) {
            operations.push_back(campaign::set_world_flag(
                {package, core::ContentCategory::world_flag, event.world_flag},
                {campaign::WorldValueType::integer, event.reply}
            ));
        }
    }
}

}  // namespace grandleon::campaign_runtime
