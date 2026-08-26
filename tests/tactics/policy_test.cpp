// SPDX-License-Identifier: MIT
#include <grandleon/tactics/policy.hpp>

#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <iostream>
#include <string_view>

namespace sim = grandleon::simulation;
namespace tac = grandleon::tactics;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

sim::UnitDefinition unit(
    sim::UnitId id,
    sim::Side side,
    sim::Position position,
    std::uint8_t movement = 1,
    std::uint8_t minimum_reach = 1,
    std::uint8_t maximum_reach = 1
) {
    sim::UnitDefinition value;
    value.id = id;
    value.unit_type_id = id * 10;
    value.side = side;
    value.position = position;
    value.health = 9;
    value.strength = 3;
    value.movement = movement;
    value.minimum_reach = minimum_reach;
    value.maximum_reach = maximum_reach;
    return value;
}

sim::EncounterSnapshot snapshot_of(const sim::EncounterDefinition& definition) {
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "policy fixture encounter is valid");
    return created.encounter.snapshot();
}

// Behaviour is policy over a snapshot, so every case below drives decide()
// against a snapshot and, where a command comes back, proves the simulation
// accepts it: the contract a client relies on.

void refuses_units_that_cannot_act() {
    sim::EncounterDefinition definition{
        6, 4, {unit(10, sim::Side::first, {0, 0}),
               unit(20, sim::Side::second, {5, 3})}, {}, {}};
    const auto snapshot = snapshot_of(definition);
    expect(
        !tac::decide(snapshot, 99, tac::Behavior::pursue, {}).actionable,
        "an unknown unit has no plan"
    );
    expect(
        !tac::decide(snapshot, 20, tac::Behavior::pursue, {}).actionable,
        "the inactive side has no plan"
    );
    auto decided = snapshot;
    decided.outcome = sim::Outcome::first_side_won;
    expect(
        !tac::decide(decided, 10, tac::Behavior::pursue, {}).actionable,
        "a decided encounter has no plan"
    );
}

void every_behavior_strikes_a_target_in_reach() {
    sim::EncounterDefinition definition{
        6, 4, {unit(10, sim::Side::first, {2, 2}),
               unit(20, sim::Side::second, {3, 2}),
               unit(21, sim::Side::second, {5, 3})}, {}, {}};
    const auto snapshot = snapshot_of(definition);
    for (const tac::Behavior behavior :
         {tac::Behavior::hold, tac::Behavior::patrol, tac::Behavior::pursue}) {
        const auto plan = tac::decide(snapshot, 10, behavior, {{5, 0}});
        expect(
            plan.actionable &&
                plan.command.type == sim::CommandType::attack &&
                plan.command.target_id == 20,
            "an adjacent enemy is struck under every behaviour"
        );
    }
}

void attacks_prefer_the_nearest_then_lowest_identifier() {
    // Proximity and identifier are the tie-breaks under the trade, so both
    // enemies here are given a band of three: neither can answer from one tile
    // or from two, both strikes therefore net exactly the same, and the case
    // measures only the ordering it names.
    //
    // Two enemies inside a reach-two band at distances two and one.
    sim::EncounterDefinition definition{
        6, 4, {unit(10, sim::Side::first, {2, 2}, 1, 1, 2),
               unit(22, sim::Side::second, {3, 2}, 1, 3, 3),
               unit(20, sim::Side::second, {4, 2}, 1, 3, 3)}, {}, {}};
    const auto nearest = tac::decide(
        snapshot_of(definition), 10, tac::Behavior::hold, {}
    );
    expect(
        nearest.actionable && nearest.command.target_id == 22,
        "the nearer enemy wins even with a higher identifier"
    );

    // Both enemies at the same distance: the lower identifier wins, whatever
    // the definition order was.
    sim::EncounterDefinition tied{
        6, 4, {unit(10, sim::Side::first, {2, 2}),
               unit(22, sim::Side::second, {3, 2}),
               unit(20, sim::Side::second, {1, 2})}, {}, {}};
    const auto broken = tac::decide(
        snapshot_of(tied), 10, tac::Behavior::hold, {}
    );
    expect(
        broken.actionable && broken.command.target_id == 20,
        "equidistant enemies resolve to the lowest identifier"
    );
}

void hold_waits_when_nothing_is_in_reach() {
    sim::EncounterDefinition definition{
        6, 4, {unit(10, sim::Side::first, {0, 0}),
               unit(20, sim::Side::second, {5, 3})}, {}, {}};
    const auto plan = tac::decide(
        snapshot_of(definition), 10, tac::Behavior::hold, {}
    );
    expect(
        plan.actionable && plan.command.type == sim::CommandType::wait,
        "hold waits rather than closing distance"
    );
}

void pursue_steps_toward_the_nearest_enemy() {
    sim::EncounterDefinition definition{
        6, 4, {unit(10, sim::Side::first, {0, 0}, 2),
               unit(20, sim::Side::second, {4, 0})}, {}, {}};
    auto created = sim::create_encounter(definition);
    const auto plan = tac::decide(
        created.encounter.snapshot(), 10, tac::Behavior::pursue, {}
    );
    expect(
        plan.actionable && plan.command.type == sim::CommandType::move &&
            plan.command.destination == sim::Position{2, 0},
        "pursue moves the full allowance toward the enemy"
    );
    expect(
        static_cast<bool>(created.encounter.apply(plan.command)),
        "the simulation accepts the pursue proposal"
    );
}

void step_toward_breaks_distance_ties_by_cell_index() {
    // Movement two toward an enemy on the diagonal: (2,0), (1,1), and (0,2)
    // all leave distance four. The chosen cell must be the lowest row-major
    // index among the best, which is (2,0), reproducible on replay and
    // independent of frontier ordering.
    sim::EncounterDefinition definition{
        6, 6, {unit(10, sim::Side::first, {0, 0}, 2),
               unit(20, sim::Side::second, {3, 3})}, {}, {}};
    const auto plan = tac::decide(
        snapshot_of(definition), 10, tac::Behavior::pursue, {}
    );
    expect(
        plan.actionable && plan.command.type == sim::CommandType::move &&
            plan.command.destination == sim::Position{2, 0},
        "tied step candidates resolve to the lowest cell index"
    );
}

void patrol_takes_its_leg_from_the_activation_count() {
    sim::EncounterDefinition definition{
        8, 4, {unit(10, sim::Side::first, {4, 0}, 1),
               unit(20, sim::Side::second, {7, 3})}, {}, {}};
    const std::vector<sim::Position> patrol{{0, 0}, {7, 0}};
    auto snapshot = snapshot_of(definition);

    snapshot.activation_count = 0;
    auto plan = tac::decide(snapshot, 10, tac::Behavior::patrol, patrol);
    expect(
        plan.actionable && plan.command.type == sim::CommandType::move &&
            plan.command.destination == sim::Position{3, 0},
        "an even activation count walks the first leg"
    );

    snapshot.activation_count = 1;
    plan = tac::decide(snapshot, 10, tac::Behavior::patrol, patrol);
    expect(
        plan.actionable && plan.command.type == sim::CommandType::move &&
            plan.command.destination == sim::Position{5, 0},
        "an odd activation count walks the second leg"
    );

    expect(
        tac::decide(snapshot, 10, tac::Behavior::patrol, {})
                .command.type == sim::CommandType::wait,
        "a patrol without points waits"
    );

    snapshot.activation_count = 0;
    expect(
        tac::decide(snapshot, 10, tac::Behavior::patrol, {{4, 0}})
                .command.type == sim::CommandType::wait,
        "a patrol already standing on its goal waits"
    );
}

void patrol_toward_an_occupied_goal_still_closes_or_waits() {
    // The goal tile holds a friendly unit. The patroller cannot stand there,
    // but it can still close distance; adjacent to the goal, no tile improves
    // and it waits instead of oscillating.
    sim::EncounterDefinition definition{
        8, 4, {unit(10, sim::Side::first, {4, 0}, 1),
               unit(11, sim::Side::first, {0, 0}),
               unit(20, sim::Side::second, {7, 3})}, {}, {}};
    const std::vector<sim::Position> patrol{{0, 0}};
    auto snapshot = snapshot_of(definition);
    snapshot.activation_count = 0;
    auto plan = tac::decide(snapshot, 10, tac::Behavior::patrol, patrol);
    expect(
        plan.actionable && plan.command.type == sim::CommandType::move &&
            plan.command.destination == sim::Position{3, 0},
        "an occupied goal still draws the patrol closer"
    );

    sim::EncounterDefinition adjacent{
        8, 4, {unit(10, sim::Side::first, {1, 0}, 1),
               unit(11, sim::Side::first, {0, 0}),
               unit(20, sim::Side::second, {7, 3})}, {}, {}};
    auto beside = snapshot_of(adjacent);
    beside.activation_count = 0;
    plan = tac::decide(beside, 10, tac::Behavior::patrol, patrol);
    expect(
        plan.actionable && plan.command.type == sim::CommandType::wait,
        "a patrol beside its occupied goal waits rather than circling"
    );
}

// --- The casting rule -------------------------------------------------------
//
// Casting is the same decision as striking, priced in one currency: health
// swung in the acting side's favour. These cases fix the comparison, what a
// cast is worth once it can only reach opponents, and the tie-breaks it is
// settled by.

sim::AbilityDefinition damage_ability(
    sim::ContentId id,
    std::int16_t power,
    sim::AreaShape area = sim::AreaShape::single,
    std::uint8_t radius = 0,
    std::uint8_t minimum_reach = 1,
    std::uint8_t maximum_reach = 1
) {
    sim::AbilityDefinition ability;
    ability.id = id;
    ability.kind = sim::AbilityKind::damage;
    ability.damage_type = sim::DamageType::physical;
    ability.area = area;
    ability.power = power;
    ability.minimum_reach = minimum_reach;
    ability.maximum_reach = maximum_reach;
    ability.radius = radius;
    return ability;
}

sim::AbilityDefinition restore_ability(
    sim::ContentId id,
    std::int16_t power,
    std::uint8_t maximum_reach = 2
) {
    sim::AbilityDefinition ability = damage_ability(id, power);
    ability.kind = sim::AbilityKind::restore;
    ability.maximum_reach = maximum_reach;
    return ability;
}

// Wounds a unit in the snapshot rather than in the definition: maximum health
// is taken from a unit's starting health, so a unit authored at four is at
// full health, not missing five.
void wound(
    sim::EncounterSnapshot& snapshot,
    sim::UnitId id,
    std::int16_t health
) {
    for (sim::UnitSnapshot& candidate : snapshot.units) {
        if (candidate.id == id) candidate.health = health;
    }
}

// Proves the proposal is not merely well-formed but legal: the engine accepts
// it from the same state the policy read.
void engine_accepts(
    const sim::EncounterDefinition& definition,
    const sim::Command& command,
    std::string_view message
) {
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "cast fixture encounter is valid");
    expect(static_cast<bool>(created.encounter.apply(command)), message);
}

void a_cast_that_beats_the_weapon_is_chosen() {
    // Strength 3 against defense 0 strikes for 3; the ability lands 6.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {damage_ability(70, 6)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(
        snapshot, 10, tac::Behavior::hold, {}, definition.abilities
    );
    expect(
        plan.actionable && plan.command.type == sim::CommandType::ability &&
            plan.command.ability_id == 70 &&
            plan.command.destination == sim::Position{3, 2},
        "a stronger ability is aimed at the enemy's tile"
    );
    engine_accepts(definition, plan.command, "the engine accepts the cast");
}

// These two cases are about how a cast is priced against a weapon, so the
// opponent is an archer whose band starts at two and which therefore answers
// nothing from an adjacent tile. That keeps the strike worth its full damage
// and leaves the comparison being tested the only variable in the fixture;
// the counter's effect on the same comparison is pinned separately below.
sim::UnitDefinition unanswering(sim::UnitId id, sim::Position position) {
    return unit(id, sim::Side::second, position, 1, 2, 2);
}

void a_weapon_that_beats_the_cast_keeps_striking() {
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unanswering(20, {3, 2})},
        {damage_ability(70, 2)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto snapshot = snapshot_of(definition);
    expect(
        tac::decide(snapshot, 10, tac::Behavior::hold, {}, definition.abilities)
                .command.type == sim::CommandType::attack,
        "a weaker ability leaves the basic attack in place"
    );
}

void an_equal_cast_loses_to_the_weapon() {
    // Three against three: the cheapest, most predictable option wins the tie.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unanswering(20, {3, 2})},
        {damage_ability(70, 3)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto snapshot = snapshot_of(definition);
    expect(
        tac::decide(snapshot, 10, tac::Behavior::hold, {}, definition.abilities)
                .command.type == sim::CommandType::attack,
        "an ability that only matches the weapon is not cast"
    );
}

void a_counter_can_tip_an_equal_cast_over_the_weapon() {
    // The same three against three as above, except the opponent answers. The
    // strike now nets nothing and the cast, which provokes no counter, wins.
    // That is the counterattack rule reaching the policy's decision rather
    // than only the engine's arithmetic.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {damage_ability(70, 3)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(
        snapshot, 10, tac::Behavior::hold, {}, definition.abilities
    );
    expect(
        plan.command.type == sim::CommandType::ability &&
            plan.command.ability_id == 70,
        "a cast that takes no counter beats a strike that trades evenly"
    );
    engine_accepts(definition, plan.command, "the engine accepts the safe cast");
}

void the_opponent_that_cannot_answer_is_struck_first() {
    // Two opponents, both adjacent. The lower identifier answers; the higher
    // one is an archer that cannot. Proximity and identifier both point at the
    // answering one, so only the trade can explain the choice.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}),
         unit(20, sim::Side::second, {3, 2}),
         unanswering(30, {1, 2})},
        {},
        {}};
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(snapshot, 10, tac::Behavior::hold, {});
    expect(
        plan.actionable && plan.command.type == sim::CommandType::attack &&
            plan.command.target_id == 30,
        "the opponent who cannot strike back is the one struck"
    );
    engine_accepts(definition, plan.command, "the engine accepts the safe strike");
}

void finishing_a_wounded_opponent_beats_wounding_a_fresh_one() {
    // Both opponents answer and both are adjacent, but one is one blow from
    // falling. A lethal strike provokes nothing, so it nets three where the
    // other nets nothing. The wounded one has the lower proximity claim only
    // by tying, so the trade is again the deciding term.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}),
         unit(20, sim::Side::second, {3, 2}),
         unit(30, sim::Side::second, {1, 2})},
        {},
        {}};
    definition.units[2].health = 3;
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(snapshot, 10, tac::Behavior::hold, {});
    expect(
        plan.actionable && plan.command.type == sim::CommandType::attack &&
            plan.command.target_id == 30,
        "the opponent a blow can finish is the one struck"
    );
    engine_accepts(definition, plan.command, "the engine accepts the finisher");
}

void a_target_that_cannot_be_hurt_loses_to_one_that_can() {
    // 30 is standing at the bottom of its own health floor: nothing can take
    // another point off it, so a strike against it removes nothing at all. 20 is
    // an ordinary opponent with health to lose. Both are adjacent and both
    // answer, so nothing but the trade separates them.
    //
    // Nothing in the policy knows what a floor is. The score is
    // `target.health - forecast.target_health_after`, read off the forecast the
    // engine itself would give. The forecast is where the floor lives, so the
    // right answer falls out of arithmetic that was already correct rather
    // than out of a case added for this.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}),
         unit(20, sim::Side::second, {3, 2}),
         unit(30, sim::Side::second, {1, 2})},
        {},
        {}};
    definition.units[2].health = 1;
    definition.units[2].endures = true;
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(snapshot, 10, tac::Behavior::hold, {});
    expect(
        plan.actionable && plan.command.type == sim::CommandType::attack &&
            plan.command.target_id == 20,
        "an opponent that can be hurt is struck ahead of one that cannot"
    );
    engine_accepts(
        definition, plan.command, "the engine accepts the useful strike"
    );
}

void a_losing_trade_still_beats_standing_still() {
    // A weak unit beside a strong one loses the exchange outright. It strikes
    // anyway: a unit that stands next to an enemy doing nothing reads as
    // broken, and two armies that each decline to move never finish a battle.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {},
        {}};
    definition.units[0].strength = 1;
    definition.units[1].strength = 8;
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(snapshot, 10, tac::Behavior::hold, {});
    expect(
        plan.actionable && plan.command.type == sim::CommandType::attack &&
            plan.command.target_id == 20,
        "a strike that loses the exchange is still proposed over waiting"
    );
    engine_accepts(definition, plan.command, "the engine accepts the bad trade");
}

void overkill_is_not_worth_more_than_the_health_it_removes() {
    // The target has one health left. A twenty-power ability removes exactly
    // one, the same as the weapon, so the weapon wins the tie.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {damage_ability(70, 20)},
        {}};
    definition.units[0].ability_ids = {70};
    auto snapshot = snapshot_of(definition);
    wound(snapshot, 20, 1);
    expect(
        tac::decide(snapshot, 10, tac::Behavior::hold, {}, definition.abilities)
                .command.type == sim::CommandType::attack,
        "damage past a target's last point counts for nothing"
    );
}

void an_ally_under_the_blast_costs_the_cast_nothing() {
    // A cross centred two tiles out covers the enemy and the ally standing
    // beyond it. The ally takes nothing from a cast on its own side, so it is
    // worth nothing to the score either way. The proof of that is not that the
    // cast is chosen, it is that moving the ally out from under the blast
    // changes not one field of the plan.
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {2, 2}, 1, 1, 2),
         unit(11, sim::Side::first, {5, 2}),
         unit(20, sim::Side::second, {4, 2})},
        {damage_ability(70, 6, sim::AreaShape::cross, 0, 2, 2)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto crowded = snapshot_of(definition);
    const auto under = tac::decide(
        crowded, 10, tac::Behavior::hold, {}, definition.abilities
    );
    expect(
        under.command.type == sim::CommandType::ability &&
            under.command.ability_id == 70,
        "an area is cast through an ally it cannot burn"
    );

    definition.units[1].position = {0, 0};
    const auto clear = snapshot_of(definition);
    const auto away = tac::decide(
        clear, 10, tac::Behavior::hold, {}, definition.abilities
    );
    expect(
        away.command.type == under.command.type &&
            away.command.ability_id == under.command.ability_id &&
            away.command.destination == under.command.destination,
        "and where the ally stands makes no difference to the plan at all"
    );
}

void a_caster_under_its_own_blast_costs_the_cast_nothing() {
    // Reach one and a cross: every tile the caster can aim at also covers the
    // caster. It cannot hurt itself, so the only thing the cover decides is
    // where the cast is aimed. Six through the enemy beats a swing of three,
    // which is what makes it the plan rather than the strike.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {damage_ability(70, 6, sim::AreaShape::cross)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto snapshot = snapshot_of(definition);
    const auto plan =
        tac::decide(snapshot, 10, tac::Behavior::hold, {}, definition.abilities);
    expect(
        plan.command.type == sim::CommandType::ability &&
            plan.command.ability_id == 70 &&
            plan.command.destination == sim::Position{3, 2},
        "standing in its own cross costs a caster nothing, and it casts"
    );
    // And the engine agrees, which is the whole contract a policy keeps: the
    // caster is untouched and the enemy pays.
    auto board = sim::create_encounter(definition);
    const auto applied = board.encounter.apply(plan.command);
    expect(static_cast<bool>(applied), "the engine accepts the cast");
    const auto after = board.encounter.snapshot();
    const auto health_of = [&after](sim::UnitId id) {
        for (const sim::UnitSnapshot& value : after.units) {
            if (value.id == id) return static_cast<int>(value.health);
        }
        return -1;
    };
    expect(
        health_of(10) == 9 && health_of(20) == 3,
        "and it lands on the enemy alone"
    );
}

void an_area_counts_every_enemy_it_covers() {
    // Two enemies one above the other. A cross centred on the lower one covers
    // both for eight against the weapon's three, and no tile inside the band
    // covers more, so that is where it is aimed.
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {2, 2}, 1, 1, 2),
         unit(20, sim::Side::second, {4, 2}),
         unit(21, sim::Side::second, {4, 1})},
        {damage_ability(70, 4, sim::AreaShape::cross, 0, 2, 2)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(
        snapshot, 10, tac::Behavior::hold, {}, definition.abilities
    );
    expect(
        plan.command.type == sim::CommandType::ability &&
            plan.command.destination == sim::Position{4, 2},
        "the cross is aimed where it covers both enemies"
    );
    engine_accepts(definition, plan.command, "the engine accepts the area cast");
}

void a_healer_mends_an_ally_instead_of_walking() {
    // Nothing is in reach, so the strike is worth nothing and any positive
    // cast wins. That is what makes an authored healer act like one.
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {2, 2}),
         unit(11, sim::Side::first, {3, 2}),
         unit(20, sim::Side::second, {7, 3})},
        {restore_ability(70, 4)},
        {}};
    definition.units[0].ability_ids = {70};
    auto snapshot = snapshot_of(definition);
    wound(snapshot, 11, 4);
    const auto plan = tac::decide(
        snapshot, 10, tac::Behavior::pursue, {}, definition.abilities
    );
    expect(
        plan.command.type == sim::CommandType::ability &&
            plan.command.destination == sim::Position{3, 2},
        "a wounded ally is mended rather than left behind"
    );
    engine_accepts(definition, plan.command, "the engine accepts the mend");
}

void a_healer_with_nobody_to_heal_falls_back_to_its_behaviour() {
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {2, 2}),
         unit(11, sim::Side::first, {3, 2}),
         unit(20, sim::Side::second, {7, 3})},
        {restore_ability(70, 4)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto snapshot = snapshot_of(definition);
    expect(
        tac::decide(snapshot, 10, tac::Behavior::pursue, {}, definition.abilities)
                .command.type == sim::CommandType::move,
        "healing a unit at full health is worth nothing"
    );
}

void healing_an_enemy_is_counted_against_the_cast() {
    // The only wounded unit within reach fights for the other side.
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {2, 2}),
         unit(20, sim::Side::second, {2, 3}, 1, 1, 1),
         unit(21, sim::Side::second, {7, 3})},
        {restore_ability(70, 4)},
        {}};
    definition.units[0].ability_ids = {70};
    auto snapshot = snapshot_of(definition);
    wound(snapshot, 20, 2);
    expect(
        tac::decide(snapshot, 10, tac::Behavior::hold, {}, definition.abilities)
                .command.type == sim::CommandType::attack,
        "a cast that would mend an enemy is never chosen"
    );
}

void an_unknown_ability_identity_is_ignored() {
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {7, 3})},
        {damage_ability(70, 6)},
        {}};
    definition.units[0].ability_ids = {70};
    auto snapshot = snapshot_of(definition);
    // The snapshot names an identity the caller's list does not define; the
    // policy must not propose a command the engine would refuse.
    expect(
        tac::decide(snapshot, 10, tac::Behavior::pursue, {}, {})
                .command.type == sim::CommandType::move,
        "an ability with no definition in hand is skipped"
    );
}

void ties_are_broken_by_listed_order_then_by_board_index() {
    // Two abilities of identical worth, and two tiles of identical worth for
    // the winner. The earliest-listed ability and the lowest board index must
    // win, so the choice cannot drift with iteration order.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}),
         unit(20, sim::Side::second, {3, 1}),
         unit(21, sim::Side::second, {3, 3})},
        {damage_ability(71, 6, sim::AreaShape::single, 0, 1, 2),
         damage_ability(70, 6, sim::AreaShape::single, 0, 1, 2)},
        {}};
    definition.units[0].ability_ids = {71, 70};
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(
        snapshot, 10, tac::Behavior::hold, {}, definition.abilities
    );
    expect(
        plan.command.ability_id == 71,
        "the earliest-listed ability wins an exact tie"
    );
    expect(
        plan.command.destination == sim::Position{3, 1},
        "the lowest board index wins an exact tie"
    );
}

void an_ability_out_of_reach_is_not_proposed() {
    // Reach two to three: the adjacent enemy is inside the weapon's band and
    // outside the ability's, so the strike stands.
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {damage_ability(70, 9, sim::AreaShape::single, 0, 2, 3)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto snapshot = snapshot_of(definition);
    expect(
        tac::decide(snapshot, 10, tac::Behavior::hold, {}, definition.abilities)
                .command.type == sim::CommandType::attack,
        "an ability whose band excludes the only enemy is not cast"
    );
}

void the_ability_free_overload_never_casts() {
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {damage_ability(70, 9)},
        {}};
    definition.units[0].ability_ids = {70};
    const auto snapshot = snapshot_of(definition);
    expect(
        tac::decide(snapshot, 10, tac::Behavior::hold, {})
                .command.type == sim::CommandType::attack,
        "the overload without abilities keeps the pre-casting behaviour"
    );
}

// --- Choosing among carried weapons -----------------------------------------
//
// A weapon is priced in the same currency an ability is: the health a strike
// would really remove. These cases fix the comparison, the reach it opens up,
// and the tie-break that keeps the choice deterministic.

sim::WeaponDefinition weapon(
    sim::ContentId id,
    std::int16_t power,
    std::uint8_t minimum_reach = 1,
    std::uint8_t maximum_reach = 1
) {
    return {id, power, minimum_reach, maximum_reach};
}

void a_second_weapon_reaches_what_the_first_cannot() {
    // A dagger in hand and a bow carried. The only opponent stands three
    // tiles away, which the dagger cannot answer and the bow can.
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {1, 1}), unit(20, sim::Side::second, {4, 1})},
        {},
        {}};
    definition.weapons = {weapon(70, 4), weapon(71, 1, 2, 3)};
    definition.units[0].weapon_ids = {70, 71};
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(
        snapshot, 10, tac::Behavior::pursue, {}, {}, definition.weapons
    );
    expect(
        plan.command.type == sim::CommandType::attack &&
            plan.command.weapon_id == 71,
        "a unit strikes with the weapon that reaches rather than walking"
    );
    engine_accepts(definition, plan.command, "the engine accepts the bow shot");

    // Without the registry there is nothing to resolve the bow against, so
    // the unit closes on the opponent exactly as it did before.
    expect(
        tac::decide(snapshot, 10, tac::Behavior::pursue, {}).command.type ==
            sim::CommandType::move,
        "the weapon-free overload keeps the pre-choice behaviour"
    );
}

void the_weapon_that_removes_more_health_is_chosen() {
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {},
        {}};
    // Both reach the adjacent opponent; the second hits harder.
    definition.weapons = {weapon(70, 1), weapon(71, 5)};
    definition.units[0].weapon_ids = {70, 71};
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(
        snapshot, 10, tac::Behavior::hold, {}, {}, definition.weapons
    );
    expect(
        plan.command.weapon_id == 71,
        "the carried weapon that removes more health is chosen"
    );
    engine_accepts(
        definition, plan.command, "the engine accepts the stronger weapon"
    );
}

void overkill_does_not_make_a_weapon_worth_more() {
    // Both weapons would fell the opponent outright, so neither removes more
    // health than the other and the weapon in hand keeps the strike.
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {},
        {}};
    definition.weapons = {weapon(70, 9), weapon(71, 40)};
    definition.units[0].weapon_ids = {70, 71};
    sim::EncounterSnapshot snapshot = snapshot_of(definition);
    wound(snapshot, 20, 2);
    expect(
        tac::decide(
            snapshot, 10, tac::Behavior::hold, {}, {}, definition.weapons
        ).command.weapon_id == 0,
        "health the opponent does not have is worth nothing"
    );
}

void weapon_ties_go_to_the_weapon_in_hand() {
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {},
        {}};
    definition.weapons = {weapon(70, 4), weapon(71, 4)};
    definition.units[0].weapon_ids = {70, 71};
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(
        snapshot, 10, tac::Behavior::hold, {}, {}, definition.weapons
    );
    expect(
        plan.command.weapon_id == 0,
        "an exact tie leaves the weapon in hand, named by its absence"
    );

    // Carried the other way round, the tie still goes to whichever is first.
    definition.units[0].weapon_ids = {71, 70};
    const auto reversed = snapshot_of(definition);
    expect(
        tac::decide(
            reversed, 10, tac::Behavior::hold, {}, {}, definition.weapons
        ).command.weapon_id == 0,
        "the earliest-carried weapon wins the tie whichever it is"
    );
}

void a_unit_carrying_one_weapon_proposes_the_same_command() {
    sim::EncounterDefinition definition{
        6, 4,
        {unit(10, sim::Side::first, {2, 2}), unit(20, sim::Side::second, {3, 2})},
        {},
        {}};
    definition.weapons = {weapon(70, 4)};
    definition.units[0].weapon_ids = {70};
    const auto snapshot = snapshot_of(definition);
    const auto chosen = tac::decide(
        snapshot, 10, tac::Behavior::hold, {}, {}, definition.weapons
    );
    const auto plain = tac::decide(snapshot, 10, tac::Behavior::hold, {});
    expect(
        chosen.command.weapon_id == 0 &&
            chosen.command.type == plain.command.type &&
            chosen.command.target_id == plain.command.target_id,
        "one carried weapon proposes exactly the command it always did"
    );
}

void an_unresolvable_carried_weapon_is_skipped() {
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {1, 1}), unit(20, sim::Side::second, {4, 1})},
        {},
        {}};
    definition.weapons = {weapon(70, 4), weapon(71, 1, 2, 3)};
    definition.units[0].weapon_ids = {70, 71};
    const auto snapshot = snapshot_of(definition);
    // The caller resolves only the dagger, so the bow is a name and nothing
    // more: the unit closes rather than proposing a strike it cannot price.
    const std::vector<sim::WeaponDefinition> partial = {weapon(70, 4)};
    expect(
        tac::decide(
            snapshot, 10, tac::Behavior::pursue, {}, {}, partial
        ).command.type == sim::CommandType::move,
        "a carried identity the caller cannot resolve is skipped"
    );
}

void a_cast_must_still_beat_the_best_weapon() {
    // The bow removes 4; the ability would remove 4 as well. An equal cast
    // loses to the strike, and the strike is the bow rather than the dagger.
    sim::EncounterDefinition definition{
        8, 4,
        {unit(10, sim::Side::first, {1, 1}), unit(20, sim::Side::second, {4, 1})},
        {damage_ability(80, 4, sim::AreaShape::single, 0, 1, 4)},
        {}};
    definition.weapons = {weapon(70, 4), weapon(71, 1, 2, 3)};
    definition.units[0].weapon_ids = {70, 71};
    definition.units[0].ability_ids = {80};
    const auto snapshot = snapshot_of(definition);
    const auto plan = tac::decide(
        snapshot, 10, tac::Behavior::hold, {}, definition.abilities,
        definition.weapons
    );
    expect(
        plan.command.type == sim::CommandType::attack &&
            plan.command.weapon_id == 71,
        "an ability that only matches the best weapon leaves the strike"
    );
    engine_accepts(
        definition, plan.command, "the engine accepts the contested strike"
    );
}

// ---------------------------------------------------------------------------
// Who is on the board. Policy proposes and the engine judges, so every board
// predicate here has to be the engine's own: a proposal aimed at somebody the
// engine will refuse is an activation thrown away, and both drivers answer a
// refusal by waiting, so the character does it again next turn and the turn
// after that.
// ---------------------------------------------------------------------------

// A wave is not a target and is not standing in anybody's way.
void a_wave_still_marching_is_neither_struck_nor_chased() {
    sim::EncounterDefinition definition{
        6, 3,
        {unit(10, sim::Side::first, {0, 1}),
         unit(11, sim::Side::first, {1, 1}),
         unit(30, sim::Side::second, {2, 1}, 2)},
        {},
        {}};
    definition.units[1].arrival_round = 5;
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "a board with a wave on it is valid");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 10, {}, 0})
        ),
        "the first side passes"
    );
    const auto snapshot = created.encounter.snapshot();
    expect(
        !snapshot.units[1].arrived,
        "and the wave is still marching when the brute is asked"
    );
    const auto plan = tac::decide(snapshot, 30, tac::Behavior::pursue, {});
    expect(
        plan.actionable && plan.command.type == sim::CommandType::move,
        "the brute walks rather than swinging at somebody who is not there"
    );
    // Its authored landing tile holds nobody, so it is a tile the brute may
    // walk onto, and walking onto it is the shortest way to the one opponent
    // that is standing.
    expect(
        plan.command.destination == sim::Position{1, 1},
        "and it walks through the tile the wave was authored onto"
    );
    expect(
        static_cast<bool>(created.encounter.apply(plan.command)),
        "the simulation accepts the proposal"
    );
}

// A character talked off the board is neither a target nor a destination, and
// the live opponent behind them is what the side goes for instead.
void a_departed_character_is_neither_struck_nor_chased() {
    sim::EncounterDefinition definition;
    definition.width = 7;
    definition.height = 3;
    sim::UnitDefinition leaving = unit(10, sim::Side::first, {1, 1});
    leaving.talk_record_id = 7100U;
    definition.units = {
        leaving,
        unit(11, sim::Side::first, {5, 1}),
        unit(20, sim::Side::second, {0, 1}),
        unit(30, sim::Side::second, {2, 1})
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the parley board is valid");

    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 11, {}, 0})
        ),
        "the first side passes"
    );
    sim::Command talk;
    talk.type = sim::CommandType::talk;
    talk.unit_id = 20;
    talk.target_id = 10;
    expect(
        static_cast<bool>(created.encounter.apply(talk)),
        "and the second side talks the nearer opponent off the board"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 11, {}, 0})
        ),
        "the first side passes again"
    );

    const auto snapshot = created.encounter.snapshot();
    expect(
        snapshot.units[0].departed && snapshot.units[0].health > 0,
        "the character who left is off the board and still alive"
    );
    const auto plan = tac::decide(snapshot, 30, tac::Behavior::pursue, {});
    expect(
        plan.actionable && plan.command.type == sim::CommandType::move &&
            plan.command.destination == sim::Position{3, 1},
        "the brute walks at the opponent who is standing rather than swinging "
        "at the one who walked away"
    );
    expect(
        static_cast<bool>(created.encounter.apply(plan.command)),
        "the simulation accepts the proposal"
    );
}

// And the actor side of the same rule: a driver walking the roster by health
// alone will hand `decide` somebody who is not on the board, and the honest
// answer is no plan rather than one the engine throws away.
// A wall longer than one allowance, with a gap at one end: the shape both
// shipped campaigns draw and both are careful to draw wide.
//
// Measuring "closer" as the crow flies, the pursuer walked up to the wall and
// stopped there for good. From against it no reachable tile was any nearer in a
// straight line, so `pursue` fell through to `wait`, and did so again every
// round for the rest of the battle. The board deadlocked.
//
// Measured through the ground, the tile towards the gap really is nearer, so
// the march goes round. Walked out over several turns rather than asserted one
// step at a time, because the whole failure was that the *next* step did not
// exist: one step in a promising direction proves nothing on its own.
void a_wall_is_walked_around_rather_than_stared_at() {
    const std::uint16_t width = 9;
    const std::uint16_t height = 7;
    sim::EncounterDefinition definition{
        width, height,
        {unit(10, sim::Side::first, {8, 6}, 3),
         unit(20, sim::Side::second, {0, 6}, 3)},
        {},
        {}};
    definition.terrain.assign(
        static_cast<std::size_t>(width) * height, sim::Terrain::open
    );
    // A spur down the middle, broken only at the very top, six rows from where
    // the pursuer stands and well beyond one allowance.
    for (std::uint16_t y = 1; y < height; ++y) {
        definition.terrain[static_cast<std::size_t>(y) * width + 4] =
            sim::Terrain::heights;
    }

    const auto opening = tac::decide(
        snapshot_of(definition), 10, tac::Behavior::pursue, {}
    );
    expect(
        opening.actionable && opening.command.type == sim::CommandType::move,
        "a pursuer with a wall in the way still has somewhere to go"
    );
    engine_accepts(definition, opening.command, "the engine accepts the detour");

    // And it keeps going, turn after turn, until it is through the gap.
    sim::EncounterDefinition walked = definition;
    bool crossed = false;
    for (int turn = 0; turn < 12 && !crossed; ++turn) {
        const auto plan = tac::decide(
            snapshot_of(walked), 10, tac::Behavior::pursue, {}
        );
        if (!plan.actionable ||
            plan.command.type != sim::CommandType::move) {
            break;
        }
        walked.units[0].position = plan.command.destination;
        crossed = walked.units[0].position.x < 4;
    }
    expect(
        crossed,
        "and the march carries on through the gap instead of stalling at the wall"
    );
}

// The other half of the same rule: where there is genuinely no way round, the
// straight line is still the answer. A goal walled off completely leaves the
// character closing what distance the ground allows, which is what it always
// did and the most sensible thing left to do.
void a_goal_with_no_way_round_is_still_approached() {
    const std::uint16_t width = 7;
    const std::uint16_t height = 3;
    sim::EncounterDefinition definition{
        width, height,
        {unit(10, sim::Side::first, {6, 1}, 3),
         unit(20, sim::Side::second, {0, 1}, 3)},
        {},
        {}};
    definition.terrain.assign(
        static_cast<std::size_t>(width) * height, sim::Terrain::open
    );
    // A wall clean across the board: nothing can cross it anywhere.
    for (std::uint16_t y = 0; y < height; ++y) {
        definition.terrain[static_cast<std::size_t>(y) * width + 3] =
            sim::Terrain::heights;
    }
    const auto plan = tac::decide(
        snapshot_of(definition), 10, tac::Behavior::pursue, {}
    );
    expect(
        plan.actionable && plan.command.type == sim::CommandType::move &&
            plan.command.destination.x < 6 &&
            plan.command.destination.x > 3,
        "a walled-off goal is still approached as far as the ground allows"
    );
    engine_accepts(definition, plan.command, "the engine accepts the approach");
}

void a_character_off_the_board_gets_no_plan() {
    sim::EncounterDefinition definition{
        6, 3,
        {unit(10, sim::Side::first, {0, 1}),
         unit(11, sim::Side::first, {3, 1}),
         unit(30, sim::Side::second, {1, 1}),
         // A second opponent, well out of the way: without one the talk empties
         // his side and the battle ends before anybody can be asked for a plan.
         unit(31, sim::Side::second, {5, 1})},
        {},
        {}};
    definition.units[1].arrival_round = 5;
    definition.units[2].talk_record_id = 7101U;
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the fixture is valid");
    expect(
        !tac::decide(
             created.encounter.snapshot(), 11, tac::Behavior::pursue, {}
        ).actionable,
        "a character still marching has no plan"
    );

    sim::Command talk;
    talk.type = sim::CommandType::talk;
    talk.unit_id = 10;
    talk.target_id = 30;
    expect(
        static_cast<bool>(created.encounter.apply(talk)),
        "the brute is talked off the board"
    );
    const auto snapshot = created.encounter.snapshot();
    expect(
        snapshot.active_side == sim::Side::second,
        "and the turn is his side's"
    );
    expect(
        !tac::decide(snapshot, 30, tac::Behavior::pursue, {}).actionable,
        "and a character who has left has no plan either"
    );
}

}  // namespace

int main() {
    refuses_units_that_cannot_act();
    every_behavior_strikes_a_target_in_reach();
    attacks_prefer_the_nearest_then_lowest_identifier();
    hold_waits_when_nothing_is_in_reach();
    pursue_steps_toward_the_nearest_enemy();
    step_toward_breaks_distance_ties_by_cell_index();
    patrol_takes_its_leg_from_the_activation_count();
    patrol_toward_an_occupied_goal_still_closes_or_waits();
    a_cast_that_beats_the_weapon_is_chosen();
    a_weapon_that_beats_the_cast_keeps_striking();
    an_equal_cast_loses_to_the_weapon();
    a_counter_can_tip_an_equal_cast_over_the_weapon();
    the_opponent_that_cannot_answer_is_struck_first();
    finishing_a_wounded_opponent_beats_wounding_a_fresh_one();
    a_losing_trade_still_beats_standing_still();
    a_target_that_cannot_be_hurt_loses_to_one_that_can();
    overkill_is_not_worth_more_than_the_health_it_removes();
    an_ally_under_the_blast_costs_the_cast_nothing();
    a_caster_under_its_own_blast_costs_the_cast_nothing();
    an_area_counts_every_enemy_it_covers();
    a_healer_mends_an_ally_instead_of_walking();
    a_healer_with_nobody_to_heal_falls_back_to_its_behaviour();
    healing_an_enemy_is_counted_against_the_cast();
    an_unknown_ability_identity_is_ignored();
    ties_are_broken_by_listed_order_then_by_board_index();
    an_ability_out_of_reach_is_not_proposed();
    the_ability_free_overload_never_casts();
    a_second_weapon_reaches_what_the_first_cannot();
    the_weapon_that_removes_more_health_is_chosen();
    overkill_does_not_make_a_weapon_worth_more();
    weapon_ties_go_to_the_weapon_in_hand();
    a_unit_carrying_one_weapon_proposes_the_same_command();
    an_unresolvable_carried_weapon_is_skipped();
    a_cast_must_still_beat_the_best_weapon();
    a_wave_still_marching_is_neither_struck_nor_chased();
    a_departed_character_is_neither_struck_nor_chased();
    a_wall_is_walked_around_rather_than_stared_at();
    a_goal_with_no_way_round_is_still_approached();
    a_character_off_the_board_gets_no_plan();
    return failures == 0 ? 0 : 1;
}
