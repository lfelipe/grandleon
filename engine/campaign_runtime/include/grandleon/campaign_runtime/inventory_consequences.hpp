// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/outcome.hpp>
#include <grandleon/core/currency.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/simulation/encounter.hpp>

namespace grandleon::campaign_runtime {

inline void append_item_grants(
    std::vector<campaign::CampaignOutcomeOperation>& operations,
    const core::PackageId& package,
    const std::vector<package_runtime::CampaignItemGrant>& grants,
    std::uint64_t node_id
) {
    for (const auto& grant : grants) {
        if (grant.join_node_id != node_id || grant.quantity == 0U) continue;
        const bool weapon = grant.grants_a_weapon();
        const auto id = weapon ? grant.weapon_id : grant.item_id;
        if (id == 0U) continue;
        operations.push_back(campaign::add_item({}, {package,
            weapon ? core::ContentCategory::weapon : core::ContentCategory::item, id},
            grant.quantity));
    }
}

// A compact console client may retain only these events rather than a full
// combat journal. Ownership still uses exactly the same settlement rules.
[[nodiscard]] inline bool inventory_consequence_event(simulation::EventType type) noexcept {
    return type == simulation::EventType::item_used ||
        type == simulation::EventType::item_dropped ||
        type == simulation::EventType::inventory_picked_up ||
        type == simulation::EventType::inventory_dropped ||
        type == simulation::EventType::gold_gained;
}

template<class OwnerOf>
inline void append_inventory_consequences(
    std::vector<campaign::CampaignOutcomeOperation>& operations,
    const core::PackageId& package,
    const std::vector<simulation::Event>& events,
    simulation::InventoryMode inventory_mode,
    OwnerOf claimed_by_a_member
) {
    const auto item_ref =
        [&package](simulation::ContentId item) -> campaign::DefinitionRef {
        return {package, core::ContentCategory::item, item};
    };
    for (const auto& event : events) {
        if (event.type != simulation::EventType::gold_gained || event.amount <= 0 ||
            event.content_id != core::gold_currency_id ||
            event.content_category != core::ContentCategory::currency ||
            claimed_by_a_member(event.related_unit_id).value == 0U) continue;
        operations.push_back(campaign::add_item({},
            {package, core::ContentCategory::currency, core::gold_currency_id},
            static_cast<std::uint32_t>(event.amount)));
    }
    if (inventory_mode == simulation::InventoryMode::carried) {
        // Keep the battle's transaction order: a full-bag exchange releases
        // the chosen copy before taking the replacement. Nothing here awards
        // equipment to the shared store or to a distant ally.
        for (const simulation::Event& event : events) {
            const bool used = event.type == simulation::EventType::item_used;
            const bool picked_up = event.type == simulation::EventType::inventory_picked_up;
            const bool dropped = event.type == simulation::EventType::inventory_dropped;
            if (!used && !picked_up && !dropped) continue;
            const auto owner = claimed_by_a_member(event.unit_id);
            if (owner.value == 0U || event.content_id == 0U) continue;
            if (!used && event.amount <= 0) continue;
            const auto category = used ? core::ContentCategory::item : event.content_category;
            if (category != core::ContentCategory::item &&
                category != core::ContentCategory::weapon) continue;
            const campaign::DefinitionRef equipment{package, category, event.content_id};
            operations.push_back(picked_up
                ? campaign::add_item(owner, equipment, static_cast<std::uint32_t>(event.amount))
                : campaign::consume_item(owner, equipment, used ? 1U : static_cast<std::uint32_t>(event.amount)));
        }
    } else {
    // What fell, first, so a batch that both gains and spends one identity is
    // never refused for an ordering.
    for (const simulation::Event& event : events) {
        if (event.type != simulation::EventType::item_dropped) continue;
        if (event.content_id == 0U) continue;
        if (claimed_by_a_member(event.related_unit_id).value == 0U) continue;
        operations.push_back(
            campaign::add_item({}, item_ref(event.content_id), 1U)
        );
    }
    // Then what was spent, out of the kit of whoever spent it. Exactly one per
    // event, in the order the battle recorded them.
    for (const simulation::Event& event : events) {
        if (event.type != simulation::EventType::item_used) continue;
        if (event.content_id == 0U) continue;
        const campaign::PersistentEntityId spender =
            claimed_by_a_member(event.unit_id);
        if (spender.value == 0U) continue;
        operations.push_back(
            campaign::consume_item(spender, item_ref(event.content_id), 1U)
        );
    }
    }

}

}  // namespace grandleon::campaign_runtime
