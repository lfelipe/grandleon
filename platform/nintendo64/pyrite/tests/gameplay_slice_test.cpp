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
}
