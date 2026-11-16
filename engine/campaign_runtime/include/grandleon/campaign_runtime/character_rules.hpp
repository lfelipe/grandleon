// SPDX-License-Identifier: MIT
#pragma once
#include <grandleon/campaign/state.hpp>
#include <grandleon/package_runtime/member_rules.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <algorithm>
#include <utility>
#include <vector>

namespace grandleon::campaign_runtime {
// Shared by package-backed campaigns and generated console campaigns. The
// inventory remains intact; only the battle's usable equipment is selected.
inline bool apply_member_kit(
    simulation::UnitDefinition& unit,
    const campaign::PersistentUnit& record,
    const package_runtime::MemberRules* rules,
    bool carried_mode = false
) {
    if(carried_mode){
        std::vector<simulation::CarriedStack> owned;
        std::vector<simulation::ContentId> weapons,items;
        std::vector<std::uint16_t> counts;
        std::uint64_t occupied{};
        for(const auto& stack:record.carried){
            if(stack.item.package_id!=record.definition.package_id || stack.quantity==0 || stack.quantity>0xffffU ||
               (stack.item.category!=core::ContentCategory::weapon && stack.item.category!=core::ContentCategory::item)) return false;
            owned.push_back({stack.item.category,stack.item.stable_id,static_cast<std::uint16_t>(stack.quantity)});
            occupied+=stack.quantity;
            if(stack.item.category==core::ContentCategory::weapon) weapons.push_back(stack.item.stable_id);
            else {items.push_back(stack.item.stable_id);counts.push_back(static_cast<std::uint16_t>(stack.quantity));}
        }
        const bool overridden=rules && rules->states_carry_capacity;
        const bool limited=overridden || unit.states_carry_capacity;
        const auto capacity=overridden?rules->carry_capacity:unit.carry_capacity;
        if(limited && occupied>capacity) return false;
        // Owned quantities are authoritative. Authored weapons are seeded at
        // recruitment, never regenerated because a new map was loaded.
        std::vector<simulation::ContentId> ordered;
        const auto& preferred=rules && rules->states_starting_weapons?rules->starting_weapons:unit.weapon_ids;
        for(const auto id:preferred) if(std::find(weapons.begin(),weapons.end(),id)!=weapons.end()) ordered.push_back(id);
        for(const auto id:weapons) if(std::find(ordered.begin(),ordered.end(),id)==ordered.end()) ordered.push_back(id);
        unit.carried_inventory=std::move(owned);unit.weapon_ids=std::move(ordered);
        unit.item_ids=std::move(items);unit.item_counts=std::move(counts);
        unit.states_carry_capacity=limited;unit.carry_capacity=limited?capacity:0;
        return true;
    }
    std::vector<simulation::ContentId> identities;
    std::vector<std::uint16_t> counts;
    identities.reserve(record.carried.size());
    counts.reserve(record.carried.size());

    // A stack's quantity is wider than a battle pack's count. A campaign that
    // has somehow accumulated more than a battle can carry brings as many as it
    // can rather than wrapping to none.
    const auto brought = [](std::uint32_t quantity) -> std::uint16_t {
        constexpr std::uint32_t ceiling = 0xffffU;
        return static_cast<std::uint16_t>(
            quantity > ceiling ? ceiling : quantity
        );
    };

    for (const simulation::ContentId authored : unit.item_ids) {
        for (std::size_t index = 0; index < record.carried.size(); ++index) {
            const campaign::InventoryStack& stack = record.carried[index];
            if (stack.item.stable_id != authored ||
                stack.item.category != core::ContentCategory::item) {
                continue;
            }
            identities.push_back(authored);
            counts.push_back(brought(stack.quantity));
            break;
        }
    }
    // Anything else the campaign holds for them, in the order the kit is kept.
    for (std::size_t index = 0; index < record.carried.size(); ++index) {
        const campaign::InventoryStack& stack = record.carried[index];
        if (stack.item.category != core::ContentCategory::item ||
            std::find(identities.begin(), identities.end(), stack.item.stable_id) != identities.end()) continue;
        identities.push_back(stack.item.stable_id);
        counts.push_back(brought(stack.quantity));
    }

    unit.item_ids = std::move(identities);
    unit.item_counts = std::move(counts);

    // And the same pass over what they are armed with.
    //
    // A weapon carries no count into a battle: `weapon_ids` is a list of what
    // is in the pack and the first of them is what is in hand, so a campaign
    // holding three of one weapon arms the character with it once. That is not
    // a loss of information - the store still holds three - it is that a
    // second copy of the same weapon is nothing a battle can express.
    //
    // The authored order comes first for the reason it does above: a character
    // the campaign has not re-armed takes the field holding exactly what their
    // unit type says, which is what makes a board with nobody from the roster
    // on it the board the package alone produces. Anything the campaign holds
    // for them beyond that follows, and the first authored weapon stays the
    // one in hand.
    if (rules != nullptr && rules->states_starting_weapons)
        unit.weapon_ids = rules->starting_weapons;
    std::vector<simulation::ContentId> armed;
    armed.reserve(unit.weapon_ids.size());
    for (const simulation::ContentId authored : unit.weapon_ids) {
        const bool personal = rules != nullptr && rules->states_starting_weapons;
        if (!personal) armed.push_back(authored);
        for (std::size_t index = 0; index < record.carried.size(); ++index) {
            const campaign::InventoryStack& stack = record.carried[index];
            if (stack.item.stable_id != authored ||
                stack.item.category != core::ContentCategory::weapon) {
                continue;
            }
            if (personal) armed.push_back(authored);
            break;
        }
    }
    for (std::size_t index = 0; index < record.carried.size(); ++index) {
        const campaign::InventoryStack& stack = record.carried[index];
        if (stack.item.category != core::ContentCategory::weapon ||
            std::find(armed.begin(), armed.end(), stack.item.stable_id) != armed.end()) continue;
        armed.push_back(stack.item.stable_id);
    }
    unit.weapon_ids = std::move(armed);
    return true;
}

inline void apply_member_permissions(
    simulation::UnitDefinition& unit, const package_runtime::MemberRules* rules,
    const std::vector<simulation::WeaponDefinition>& weapons,
    bool carried_mode = false
) {
    if(carried_mode){
        if(rules && rules->states_weapon_affinities){
            unit.weapon_affinities=rules->weapon_affinities;unit.states_weapon_affinities=true;
        }
        if(unit.states_weapon_affinities){
            unit.weapon_ids.erase(std::remove_if(unit.weapon_ids.begin(),unit.weapon_ids.end(),[&](auto id){
                const auto found=std::find_if(weapons.begin(),weapons.end(),[id](const auto& weapon){return weapon.id==id;});
                return found==weapons.end() || std::find(unit.weapon_affinities.begin(),unit.weapon_affinities.end(),found->weapon_type)==unit.weapon_affinities.end();
            }),unit.weapon_ids.end());
        }
        if(unit.weapon_ids.empty()){unit.power=0;unit.minimum_reach=1;unit.maximum_reach=1;unit.accuracy=100;}
    }
    if (rules == nullptr) return;
    unit.defeat_ends_battle = rules->defeat_policy == package_runtime::DefeatPolicy::lose_battle;
    if (rules->states_weapon_affinities) {
        unit.weapon_ids.erase(std::remove_if(unit.weapon_ids.begin(), unit.weapon_ids.end(),
            [&](simulation::ContentId id) {
                const auto found = std::find_if(weapons.begin(), weapons.end(),
                    [id](const simulation::WeaponDefinition& weapon) { return weapon.id == id; });
                return found == weapons.end() || std::find(rules->weapon_affinities.begin(),
                    rules->weapon_affinities.end(), found->weapon_type) == rules->weapon_affinities.end();
            }), unit.weapon_ids.end());
    }
    if ((rules->states_weapon_affinities || rules->states_starting_weapons) && unit.weapon_ids.empty()) {
        unit.power = 0;
        unit.minimum_reach = 1;
        unit.maximum_reach = 1;
        unit.accuracy = 100;
    }
}
inline bool permanent_member_loss(package_runtime::DefeatPolicy policy, bool legacy_permanent) noexcept {
    return policy == package_runtime::DefeatPolicy::permanent ||
        (policy == package_runtime::DefeatPolicy::inherit && legacy_permanent);
}
}  // namespace grandleon::campaign_runtime
