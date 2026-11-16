// SPDX-License-Identifier: MIT
#include "../project/src/user/GameplaySlice.hpp"

#include <cassert>

namespace gp = grandleon::nintendo64::pyrite;
namespace sim = grandleon::simulation;

sim::UnitDefinition unit(
    sim::UnitId id,
    sim::Side side,
    sim::Position position,
    std::uint8_t local_player
) {
    sim::UnitDefinition result;
    result.id = id;
    result.unit_type_id = id + 100U;
    result.side = side;
    result.position = position;
    result.health = 10;
    result.strength = 4;
    result.defense = 1;
    result.movement = 3U;
    result.action_points = 1U;
    result.local_player = local_player;
    return result;
}

int main() {
    sim::EncounterDefinition definition;
    definition.width = 4U;
    definition.height = 3U;
    definition.units = {
        unit(1U, sim::Side::first, {0, 1}, 1U),
        unit(2U, sim::Side::first, {2, 1}, 2U),
        unit(3U, sim::Side::second, {3, 1}, 0U),
    };

    gp::GameplaySlice game;
    assert(game.open(definition, 1U, 2U));
    assert(game.local_players() == 2U);
    assert(game.cursor(0U) == sim::Position(0, 1));
    assert(game.cursor(1U) == sim::Position(2, 1));

    assert(game.input(gp::GameplaySlice::Action::confirm, 0U));
    assert(game.selected(0U) == 1U);
    assert(game.selected(1U) == 0U);
    assert(game.input(gp::GameplaySlice::Action::cancel, 0U));

    assert(game.input(gp::GameplaySlice::Action::right, 0U));
    assert(game.input(gp::GameplaySlice::Action::right, 0U));
    assert(!game.input(gp::GameplaySlice::Action::confirm, 0U));
    assert(game.selected(0U) == 0U);

    assert(game.input(gp::GameplaySlice::Action::confirm, 1U));
    assert(game.selected(1U) == 2U);
    assert(game.active_player() == 1U);
    assert(game.input(gp::GameplaySlice::Action::cancel, 1U));
    assert(game.input(gp::GameplaySlice::Action::left, 1U));
    assert(game.input(gp::GameplaySlice::Action::left, 1U));
    assert(!game.input(gp::GameplaySlice::Action::confirm, 1U));
    assert(game.selected(1U) == 0U);

    sim::EncounterDefinition arranging = definition;
    arranging.deployment_tiles = {
        {0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {2, 1},
    };
    gp::GameplaySlice deployment;
    assert(deployment.open(arranging, 1U, 2U));
    assert(deployment.snapshot().deploying);
    assert(deployment.apply({
        sim::CommandType::deploy, 1U, {0, 2}, 0U, 0U, 0U, 0U,
    }, 0U));
    assert(deployment.snapshot().units[0].position == sim::Position(0, 2));
    assert(deployment.transition().command.type == sim::CommandType::deploy);
    assert(deployment.apply({
        sim::CommandType::begin_battle, 0U, {}, 0U, 0U, 0U, 0U,
    }, 0U));
    assert(!deployment.snapshot().deploying);
    assert(deployment.transition().command.type ==
           sim::CommandType::begin_battle);
    assert(deployment.accepted_commands() == 2U);
    // Victory collection is still interactive through the real N64 input seam.
    sim::EncounterDefinition loot;
    loot.width = 4; loot.height = 4;
    loot.terrain.assign(16, sim::Terrain::open);
    loot.turn_order = sim::TurnOrder::side_blocks;
    loot.inventory_mode = sim::InventoryMode::carried;
    sim::ItemDefinition tonic; tonic.id = 30; loot.items = {tonic};
    auto collector = unit(1, sim::Side::first, {0, 0}, 1);
    collector.strength = 30; collector.action_points = 1;
    collector.item_ids = {30}; collector.item_counts = {1};
    collector.carried_inventory = {{grandleon::core::ContentCategory::item, 30, 1}};
    collector.states_carry_capacity = true; collector.carry_capacity = 1;
    auto ally = unit(2, sim::Side::first, {2, 0}, 2);
    auto defeated = unit(3, sim::Side::second, {1, 0}, 0);
    defeated.health = 1; defeated.drop_item_id = 30; defeated.drop_chance = 100;
    loot.units = {collector, ally, defeated};
    gp::GameplaySlice collecting;
    assert(collecting.open(loot, 1, 2));
    sim::Command strike; strike.type = sim::CommandType::attack; strike.unit_id = 1; strike.target_id = 3;
    assert(collecting.apply(strike));
    assert(collecting.snapshot().collecting_loot);
    assert(collecting.input(gp::GameplaySlice::Action::confirm));
    assert(collecting.selected() == 1);
    assert(collecting.input(gp::GameplaySlice::Action::right));
    assert(collecting.input(gp::GameplaySlice::Action::confirm));
    assert(collecting.snapshot().units[0].position == sim::Position(1, 0));
    assert(collecting.input(gp::GameplaySlice::Action::confirm));
    const auto ground = collecting.snapshot().ground_items.front().id;
    sim::Command pickup; pickup.type = sim::CommandType::pick_up; pickup.unit_id = 1; pickup.ground_item_id = ground;
    const auto unchanged = collecting.encounter().canonical_hash();
    assert(!collecting.apply(pickup));
    assert(collecting.transition().error == sim::CommandError::inventory_full);
    assert(collecting.encounter().canonical_hash() == unchanged);
    pickup.type = sim::CommandType::exchange_carried;
    pickup.carried_category = grandleon::core::ContentCategory::item; pickup.carried_content_id = 30;
    assert(collecting.apply(pickup));
    assert(collecting.selected() == 1);
    assert(collecting.input(gp::GameplaySlice::Action::left, 1));
    assert(!collecting.input(gp::GameplaySlice::Action::confirm, 1));
    assert(collecting.selected(1) == 0);
    sim::Command done; done.type = sim::CommandType::finish_collection;
    assert(collecting.apply(done));
    assert(!collecting.snapshot().collecting_loot);
    assert(collecting.snapshot().outcome == sim::Outcome::first_side_won);

    // A compatible looted weapon is selectable per attack, without replacing
    // the original weapon or introducing an equip/save operation.
    sim::EncounterDefinition armed;
    armed.width = 4; armed.height = 4;
    armed.terrain.assign(16, sim::Terrain::open);
    armed.inventory_mode = sim::InventoryMode::carried;
    sim::WeaponTypeDefinition blade_type; blade_type.id = 100;
    armed.weapon_types = {blade_type};
    sim::WeaponDefinition blade; blade.id = 10; blade.weapon_type = 100;
    blade.power = 1; blade.minimum_reach = 1; blade.maximum_reach = 1;
    auto acquired = blade; acquired.id = 20; acquired.power = 7; acquired.maximum_reach = 2;
    armed.weapons = {blade, acquired};
    auto owner = unit(1, sim::Side::first, {0, 0}, 1);
    owner.action_points = 8; owner.states_weapon_affinities = true;
    owner.weapon_affinities = {100}; owner.weapon_ids = {10};
    owner.carried_inventory = {{grandleon::core::ContentCategory::weapon, 10, 1}};
    owner.states_carry_capacity = true; owner.carry_capacity = 2;
    auto carrier = unit(3, sim::Side::second, {1, 0}, 0);
    carrier.health = 1; carrier.weapon_ids = {20};
    carrier.carried_inventory = {{grandleon::core::ContentCategory::weapon, 20, 1}};
    carrier.drop_inventory_on_defeat = true;
    auto distant = unit(4, sim::Side::second, {3, 0}, 0); distant.health = 20;
    armed.units = {owner, carrier, distant};
    gp::GameplaySlice weapons;
    assert(weapons.open(armed, 1, 1));
    sim::Command first; first.type = sim::CommandType::attack;
    first.unit_id = 1; first.target_id = 3;
    assert(weapons.apply(first));
    sim::Command reply; reply.type = sim::CommandType::wait; reply.unit_id = 4;
    assert(weapons.apply(reply));
    sim::Command step; step.type = sim::CommandType::move; step.unit_id = 1; step.destination = {1, 0};
    assert(weapons.apply(step));
    sim::Command collect; collect.type = sim::CommandType::pick_up; collect.unit_id = 1;
    collect.ground_item_id = weapons.snapshot().ground_items.front().id;
    assert(weapons.apply(collect));
    const auto held = weapons.snapshot().units[0].weapon_ids;
    assert(held.size() == 2 && held[0] == 10 && held[1] == 20);
    assert(!sim::forecast_attack(weapons.snapshot(), 1, 4, armed.weapons, 10));
    const auto forecast = sim::forecast_attack(weapons.snapshot(), 1, 4, armed.weapons, 20);
    assert(forecast && forecast.damage == 10);
    sim::Command chosen; chosen.type = sim::CommandType::attack;
    chosen.unit_id = 1; chosen.target_id = 4; chosen.weapon_id = 20;
    assert(weapons.apply(chosen));
    assert(weapons.transition().command.weapon_id == 20);
    assert(weapons.snapshot().units[2].health == 10);
    assert(weapons.snapshot().units[0].weapon_ids == held);

}
