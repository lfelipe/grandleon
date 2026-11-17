// SPDX-License-Identifier: MIT
#pragma once
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/core/currency.hpp>
#include <grandleon/package_runtime/shop.hpp>
#include <limits>

namespace grandleon::campaign_runtime {
struct ShopMemberOwner final {
    std::uint64_t source_id{};
    std::uint8_t player{1};
};
struct ShopParty final {
    std::uint64_t node_id{};
    std::vector<ShopMemberOwner> members;
};

// Current battle ownership wins. Reserves retain their most recently placed
// army, then their first authored army; otherwise they belong to P1's camp.
[[nodiscard]] inline std::uint8_t shop_member_player(
    const campaign::CampaignState& state, std::uint64_t source_id,
    unsigned players, const std::vector<ShopParty>& parties
) noexcept {
    if (players <= 1U) return 1U;
    const auto at = [&](std::uint64_t node) -> std::uint8_t {
        for (const auto& party : parties) if (party.node_id == node)
            for (const auto& member : party.members) if (member.source_id == source_id)
                return member.player >= 1U && member.player <= players ? member.player : 0U;
        return 0U;
    };
    if (const auto player = at(state.progress.active_node.stable_id)) return player;
    for (auto entry = state.progress.history.rbegin(); entry != state.progress.history.rend(); ++entry)
        if (const auto player = at(entry->node.stable_id)) return player;
    for (const auto& party : parties) if (const auto player = at(party.node_id)) return player;
    return 1U;
}

enum class ShopError : std::uint8_t {
    none, invalid_offer, invalid_member, invalid_stock, sold_out,
    insufficient_gold, inventory_full,
};

[[nodiscard]] inline campaign::DefinitionRef shop_stock_key(
    const core::PackageId& package, std::uint64_t campaign_id,
    std::uint64_t node, std::uint64_t offer
) noexcept {
    auto hash = core::stable_content_id_v1("grandleon.shop.purchases.v1");
    for (const auto identity : {campaign_id, node, offer}) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            hash = core::fnv1a64_step(hash, static_cast<std::uint8_t>(identity >> shift));
    }
    return {package, core::ContentCategory::world_flag, hash};
}

[[nodiscard]] inline bool shop_purchase_count(
    const campaign::CampaignState& state, const campaign::DefinitionRef& key,
    std::int64_t& count
) noexcept {
    count = 0;
    if (const auto* value = campaign::find_world_value(state, key)) {
        if (value->type != campaign::WorldValueType::integer || value->value < 0 ||
            value->value == std::numeric_limits<std::int64_t>::max()) return false;
        count = value->value;
    }
    return true;
}

struct ShopPurchase final {
    ShopError error{ShopError::none};
    std::vector<campaign::CampaignOutcomeOperation> operations;
};

// Content and effective capacity come from the current authored shop/hero,
// never from a price or quantity supplied by a front end. The caller persists
// the resulting candidate before publishing success.
[[nodiscard]] inline ShopPurchase prepare_shop_purchase(
    const campaign::CampaignState& state, const core::PackageId& package,
    std::uint64_t campaign_id, std::uint64_t node,
    const package_runtime::ShopOffer& offer, campaign::PersistentEntityId member,
    bool states_capacity, std::uint16_t capacity
) {
    ShopPurchase result;
    const auto refuse = [&result](ShopError error) {
        result.error = error;
        return result;
    };
    if (!campaign_id || !node || !offer.id || !offer.content_id || !offer.price ||
        offer.price > 32767U || (offer.category != core::ContentCategory::item &&
        offer.category != core::ContentCategory::weapon)) return refuse(ShopError::invalid_offer);
    const auto* owner = campaign::find_unit(state, member);
    if (!owner || (owner->availability != campaign::Availability::available &&
        owner->availability != campaign::Availability::retired)) return refuse(ShopError::invalid_member);
    const auto key = shop_stock_key(package, campaign_id, node, offer.id);
    std::int64_t count{};
    if (!shop_purchase_count(state, key, count)) return refuse(ShopError::invalid_stock);
    if (offer.stock && count >= offer.stock) return refuse(ShopError::sold_out);
    const campaign::DefinitionRef gold{package, core::ContentCategory::currency, core::gold_currency_id};
    if (campaign::item_quantity(state, {}, gold) < offer.price) return refuse(ShopError::insufficient_gold);
    std::uint64_t used{};
    for (const auto& stack : owner->carried) used += stack.quantity;
    if (states_capacity && used >= capacity) return refuse(ShopError::inventory_full);
    // Personal battle stacks use uint16 quantities even when an older hero
    // has no authored overall capacity. Do not sell an unplayable next kit.
    if (campaign::item_quantity(state, member, {package, offer.category, offer.content_id}) >=
        std::numeric_limits<std::uint16_t>::max()) return refuse(ShopError::inventory_full);
    result.operations = {
        campaign::consume_item({}, gold, offer.price),
        campaign::add_item(member, {package, offer.category, offer.content_id}, 1U),
        campaign::set_world_flag(key, {campaign::WorldValueType::integer, count + 1}),
    };
    return result;
}
}  // namespace grandleon::campaign_runtime
