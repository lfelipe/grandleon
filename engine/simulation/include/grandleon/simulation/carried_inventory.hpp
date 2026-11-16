// SPDX-License-Identifier: MIT
#pragma once
#include <grandleon/simulation/encounter.hpp>
#include <algorithm>
#include <limits>

namespace grandleon::simulation {
enum class InventoryError : std::uint8_t {
    none, invalid_stack, unordered_stacks, over_capacity, unavailable,
    quantity_overflow, missing_ground_item, wrong_position, invalid_exchange,
    exhausted_identity,
};
[[nodiscard]] inline bool carried_stack_less(const CarriedStack& a, const CarriedStack& b) noexcept {
    return a.category != b.category ? a.category < b.category : a.content_id < b.content_id;
}
[[nodiscard]] inline std::uint64_t carried_count(const std::vector<CarriedStack>& stacks) noexcept {
    std::uint64_t result{};
    for (const auto& stack : stacks) result += stack.quantity;
    return result;
}
[[nodiscard]] inline InventoryError validate_carried(
    const std::vector<CarriedStack>& stacks, bool stated, std::uint16_t capacity
) noexcept {
    for (std::size_t i=0;i<stacks.size();++i) {
        const auto& stack=stacks[i];
        if ((stack.category!=core::ContentCategory::weapon && stack.category!=core::ContentCategory::item) ||
            stack.content_id==0 || stack.quantity==0) return InventoryError::invalid_stack;
        if(i && !carried_stack_less(stacks[i-1],stack)) return InventoryError::unordered_stacks;
    }
    if(stated && carried_count(stacks)>capacity) return InventoryError::over_capacity;
    return InventoryError::none;
}
[[nodiscard]] inline InventoryError add_carried(
    std::vector<CarriedStack>& stacks, CarriedStack incoming, bool stated, std::uint16_t capacity
) {
    if (validate_carried(stacks,false,0)!=InventoryError::none ||
        validate_carried({incoming},false,0)!=InventoryError::none) return InventoryError::invalid_stack;
    if (stated && carried_count(stacks)+incoming.quantity>capacity) return InventoryError::over_capacity;
    auto at=std::lower_bound(stacks.begin(),stacks.end(),incoming,carried_stack_less);
    if(at!=stacks.end() && at->category==incoming.category && at->content_id==incoming.content_id) {
        if(incoming.quantity>std::numeric_limits<std::uint16_t>::max()-at->quantity) return InventoryError::quantity_overflow;
        at->quantity=static_cast<std::uint16_t>(at->quantity+incoming.quantity);
    } else stacks.insert(at,incoming);
    return InventoryError::none;
}
[[nodiscard]] inline InventoryError remove_carried(std::vector<CarriedStack>& stacks, CarriedStack outgoing) {
    if(validate_carried({outgoing},false,0)!=InventoryError::none) return InventoryError::invalid_stack;
    auto at=std::lower_bound(stacks.begin(),stacks.end(),outgoing,carried_stack_less);
    if(at==stacks.end() || at->category!=outgoing.category || at->content_id!=outgoing.content_id ||
       at->quantity<outgoing.quantity) return InventoryError::unavailable;
    at->quantity=static_cast<std::uint16_t>(at->quantity-outgoing.quantity);
    if(!at->quantity) stacks.erase(at);
    return InventoryError::none;
}
// Caller enforces phase, actor ownership and action cost. These transactions
// enforce physical location and change the pack/ground together or neither.
[[nodiscard]] inline InventoryError collect_ground_item(
    UnitSnapshot& unit, std::vector<GroundItem>& ground, std::uint64_t identity
) {
    auto at=std::find_if(ground.begin(),ground.end(),[identity](const auto& entry){return entry.id==identity;});
    if(at==ground.end()) return InventoryError::missing_ground_item;
    if(!(unit.position==at->position)) return InventoryError::wrong_position;
    auto candidate=unit.carried_inventory;
    auto incoming = at->stack;
    incoming.quantity = 1;
    const auto result=add_carried(candidate,incoming,unit.states_carry_capacity,unit.carry_capacity);
    if(result!=InventoryError::none) return result;
    unit.carried_inventory=std::move(candidate);
    if (--at->stack.quantity == 0) ground.erase(at);
    return InventoryError::none;
}
[[nodiscard]] inline InventoryError exchange_ground_item(
    UnitSnapshot& unit, std::vector<GroundItem>& ground, std::uint64_t identity,
    std::uint64_t& next_identity, CarriedStack outgoing
) {
    auto at=std::find_if(ground.begin(),ground.end(),[identity](const auto& entry){return entry.id==identity;});
    if(at==ground.end()) return InventoryError::missing_ground_item;
    if(!(unit.position==at->position)) return InventoryError::wrong_position;
    if(next_identity==0 || next_identity==std::numeric_limits<std::uint64_t>::max() ||
       (!ground.empty() && next_identity<=ground.back().id)) return InventoryError::exhausted_identity;
    if(outgoing.quantity!=1 || at->stack.quantity==0) return InventoryError::invalid_exchange;
    auto candidate=unit.carried_inventory;
    auto result=remove_carried(candidate,outgoing);
    if(result!=InventoryError::none) return result;
    auto incoming = at->stack;
    incoming.quantity = 1;
    result=add_carried(candidate,incoming,unit.states_carry_capacity,unit.carry_capacity);
    if(result!=InventoryError::none) return result;
    unit.carried_inventory=std::move(candidate);
    if (--at->stack.quantity == 0) ground.erase(at);
    ground.push_back({next_identity,unit.position,outgoing});
    ++next_identity;
    return InventoryError::none;
}
[[nodiscard]] inline InventoryError drop_carried_item(
    UnitSnapshot& unit, std::vector<GroundItem>& ground, std::uint64_t& next_identity, CarriedStack outgoing
) {
    if(next_identity==0 || next_identity==std::numeric_limits<std::uint64_t>::max() ||
       (!ground.empty() && next_identity<=ground.back().id)) return InventoryError::exhausted_identity;
    if(outgoing.quantity!=1) return InventoryError::invalid_stack;
    auto candidate=unit.carried_inventory;
    const auto result=remove_carried(candidate,outgoing);
    if(result!=InventoryError::none) return result;
    ground.push_back({next_identity,unit.position,outgoing});
    ++next_identity;
    unit.carried_inventory=std::move(candidate);
    return InventoryError::none;
}
inline void refresh_usable_inventory(UnitSnapshot& unit,const std::vector<WeaponDefinition>& weapons) {
    const auto owned=[&](core::ContentCategory category,ContentId id) -> const CarriedStack* {
        const auto found=std::find_if(unit.carried_inventory.begin(),unit.carried_inventory.end(),[&](const auto& stack){
            return stack.category==category && stack.content_id==id;
        });
        return found==unit.carried_inventory.end()?nullptr:&*found;
    };
    const auto permitted=[&](ContentId id) {
        const auto weapon=std::find_if(weapons.begin(),weapons.end(),[id](const auto& entry){return entry.id==id;});
        return weapon!=weapons.end() && (!unit.states_weapon_affinities ||
            std::find(unit.weapon_affinities.begin(),unit.weapon_affinities.end(),weapon->weapon_type)!=unit.weapon_affinities.end());
    };
    std::vector<ContentId> choices;
    for(const auto id:unit.weapon_ids) if(owned(core::ContentCategory::weapon,id) && permitted(id)) choices.push_back(id);
    for(const auto& stack:unit.carried_inventory) {
        if(stack.category==core::ContentCategory::weapon && permitted(stack.content_id) &&
           std::find(choices.begin(),choices.end(),stack.content_id)==choices.end()) choices.push_back(stack.content_id);
    }
    unit.weapon_ids=std::move(choices);
    std::vector<ContentId> items;
    for(const auto id:unit.item_ids) if(owned(core::ContentCategory::item,id)) items.push_back(id);
    for(const auto& stack:unit.carried_inventory) {
        if(stack.category==core::ContentCategory::item && std::find(items.begin(),items.end(),stack.content_id)==items.end())
            items.push_back(stack.content_id);
    }
    unit.item_ids=std::move(items);unit.item_counts.clear();
    for(const auto id:unit.item_ids) unit.item_counts.push_back(owned(core::ContentCategory::item,id)->quantity);
    unit.power=0;unit.minimum_reach=1;unit.maximum_reach=1;unit.accuracy=100;unit.weapon_type=0;
    if(!unit.weapon_ids.empty()) {
        const auto equipped=std::find_if(weapons.begin(),weapons.end(),[&](const auto& weapon){return weapon.id==unit.weapon_ids.front();});
        if(equipped!=weapons.end()) {
            unit.power=equipped->power;unit.minimum_reach=equipped->minimum_reach;unit.maximum_reach=equipped->maximum_reach;
            unit.accuracy=equipped->accuracy;unit.weapon_type=equipped->weapon_type;
        }
    }
}

}
