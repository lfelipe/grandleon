// SPDX-License-Identifier: MIT
// When a character's turn ends itself.
//
// A character that has walked and can do nothing else has its turn ended for
// it, rather than being handed back a menu whose every committing row the
// engine would refuse. `client::nothing_left_to_do` is the judgement, and it is
// one function rather than one per front end because four clients each deciding
// what "nothing left to do" means is four games.
//
// The whole of the judgement is what counts as *something* to do, so that is
// what this walks:
//
//   * a walk still to take is something to do, and it is the clause that keeps
//     the turn from ending itself on a character the player has simply not
//     moved yet;
//   * a strike, with the weapon in hand or with any other carried, that reaches
//     somebody;
//   * a talk somebody beside it would answer;
//   * a cast that would change anybody, and *not* one whose band reaches only
//     empty ground, nor a restoring one that would restore nothing;
//   * an item the engine would accept spending, and not a draught at full
//     health, which the engine itself forecasts at zero.
//
// And the guards, which matter as much: a character on the side that is not
// acting, one already finished, and one locked out by somebody else's open
// activation all answer false, because there is no turn of theirs to close.

#include <grandleon/client/presenter.hpp>
#include <grandleon/client/turn_client.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace client = grandleon::client;
namespace sim = grandleon::simulation;
namespace turn = grandleon::client::turn;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const sim::ContentId blade_id = 0x7101;
const sim::ContentId bow_id = 0x7102;
const sim::ContentId spark_id = 0x7201;
const sim::ContentId mercy_id = 0x7202;
const sim::ContentId draught_id = 0x7301;
const sim::ContentId keepsake_id = 0x7302;

const sim::UnitId soldier_id = 1;
const sim::UnitId ally_id = 2;
const sim::UnitId distant_id = 10;
const sim::UnitId archer_id = 11;

// A long, nearly empty board with one soldier of the player's side and one
// opponent far enough east to be out of everything. Every case below moves one
// thing about it, so what the answer turns on is never in doubt.
//
//     y=0  S  .  .  .  .  .  .  .  .  E
//     y=1  .  .  .  .  .  .  .  .  .  .
//
// The soldier has two action points, so it can walk and then still act. That
// is the state the owner reported, and the one every case here is written
// against. It walks one tile, which is what leaves it somewhere with the whole
// question still open.
sim::EncounterDefinition lonely_board() {
    sim::EncounterDefinition definition;
    definition.width = 10;
    definition.height = 2;
    definition.turn_order = sim::TurnOrder::side_blocks;
    definition.weapons = {
        {blade_id, 4, 1, 1, 100},
        {bow_id, 4, 2, 3, 100},
    };
    definition.abilities = {
        {spark_id, sim::AbilityKind::damage, sim::DamageType::magical,
         sim::AreaShape::single, 4, 1, 2, 0, 100},
        {mercy_id, sim::AbilityKind::restore, sim::DamageType::magical,
         sim::AreaShape::single, 4, 1, 2, 0, 100},
    };
    definition.items = {
        {draught_id, sim::ItemKind::restore, 5},
        {keepsake_id, sim::ItemKind::none, 0},
    };

    sim::UnitDefinition soldier;
    soldier.id = soldier_id;
    soldier.unit_type_id = 100;
    soldier.side = sim::Side::first;
    soldier.position = {0, 0};
    soldier.health = 20;
    soldier.strength = 4;
    soldier.movement = 1;
    soldier.action_points = 2;
    soldier.weapon_ids = {blade_id};

    sim::UnitDefinition far_away;
    far_away.id = distant_id;
    far_away.unit_type_id = 200;
    far_away.side = sim::Side::second;
    far_away.position = {9, 0};
    far_away.health = 20;
    far_away.strength = 4;
    far_away.movement = 0;
    far_away.action_points = 1;
    far_away.weapon_ids = {blade_id};

    definition.units = {soldier, far_away};
    return definition;
}

// An opposing archer, standing where its bow reaches `at` and its feet reach
// nothing. Used only by the two cases that need somebody short of health, since
// a character is authored at full health and the only way to be short of it is
// to have been hit.
void add_archer(sim::EncounterDefinition& definition, sim::Position stand) {
    sim::UnitDefinition archer;
    archer.id = archer_id;
    archer.unit_type_id = 201;
    archer.side = sim::Side::second;
    archer.position = stand;
    archer.health = 20;
    archer.strength = 3;
    archer.movement = 0;
    archer.action_points = 1;
    archer.weapon_ids = {bow_id};
    definition.units.push_back(archer);
}

void apply_or_fail(
    sim::Encounter& encounter, const sim::Command& command, const char* what
) {
    const auto result = encounter.apply(command);
    if (!result) {
        std::cerr << "FAIL: " << what << ", error "
                  << static_cast<int>(result.error) << '\n';
        ++failures;
    }
}

// Ends whichever side is acting, one `wait` at a time, exactly as a front end
// draining a side does. `client::unfinished_unit` names who still owes one.
void pass_the_turn(sim::Encounter& encounter) {
    const sim::Side side = encounter.snapshot().active_side;
    for (int guard = 0; guard < 32; ++guard) {
        const sim::UnitId owed =
            client::unfinished_unit(encounter.snapshot(), side);
        if (owed == 0) return;
        sim::Command wait;
        wait.type = sim::CommandType::wait;
        wait.unit_id = owed;
        if (!encounter.apply(wait)) return;
        if (encounter.snapshot().active_side != side) return;
    }
}

// The soldier's walk, which is the state the question is actually asked in.
// Walking costs the movement point and leaves the action, exactly as it does on
// a console.
void walk_the_soldier(sim::Encounter& encounter, sim::Position to) {
    sim::Command walk;
    walk.type = sim::CommandType::move;
    walk.unit_id = soldier_id;
    walk.destination = to;
    apply_or_fail(encounter, walk, "the soldier walks");
}

bool asked(sim::Encounter& encounter, sim::UnitId unit_id) {
    return client::nothing_left_to_do(
        encounter.snapshot(), unit_id, encounter.weapons(),
        encounter.abilities(), encounter.items()
    );
}

void a_character_that_has_not_walked_is_never_finished_for() {
    auto created = sim::create_encounter(lonely_board());
    expect(static_cast<bool>(created), "the board is valid content");
    expect(
        !asked(created.encounter, soldier_id),
        "a character with its walk still in hand has something to do"
    );
}

void a_walked_character_alone_on_the_board_is_finished() {
    auto created = sim::create_encounter(lonely_board());
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        asked(created.encounter, soldier_id),
        "a character that has walked, reaches nobody and carries nothing has "
        "nothing left"
    );
}

void a_strike_in_reach_is_something_to_do() {
    auto definition = lonely_board();
    // Two tiles east, so the walk closes to adjacent and the blade reaches.
    definition.units[1].position = {2, 0};
    auto created = sim::create_encounter(definition);
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        !asked(created.encounter, soldier_id),
        "somebody the blade reaches is something to do"
    );
}

void a_second_weapon_is_asked_about_too() {
    auto definition = lonely_board();
    definition.units[0].weapon_ids = {blade_id, bow_id};
    // Out of the blade's reach from where the walk lands, and inside the bow's.
    definition.units[1].position = {4, 0};
    auto created = sim::create_encounter(definition);
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        !asked(created.encounter, soldier_id),
        "a carried weapon that reaches is something to do even when the one in "
        "hand does not"
    );
}

void a_talk_somebody_would_answer_is_something_to_do() {
    auto definition = lonely_board();
    sim::UnitDefinition envoy;
    envoy.id = 20;
    envoy.unit_type_id = 202;
    envoy.side = sim::Side::second;
    envoy.position = {2, 0};
    envoy.health = 20;
    envoy.strength = 1;
    envoy.movement = 0;
    envoy.action_points = 1;
    envoy.talk_record_id = 0x7401;
    definition.units.push_back(envoy);
    // And nothing in hand, so the talk is the only answer left. A bare-handed
    // character still swings, so the walk lands out of everybody's way.
    definition.units[0].weapon_ids.clear();
    auto created = sim::create_encounter(definition);
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        !asked(created.encounter, soldier_id),
        "a neighbour with something to say is something to do"
    );
}

// The clause the engine cannot answer with an empty list. A cast lights its
// whole band whether or not anybody is standing in it (deliberately, so a
// player can see where a spell would land), so reading that list alone would
// make every character who knows a spell permanently busy.
void a_cast_over_empty_ground_is_not_something_to_do() {
    auto definition = lonely_board();
    definition.units[0].ability_ids = {spark_id};
    auto created = sim::create_encounter(definition);
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        !sim::aimable_tiles(
             created.encounter.snapshot(), soldier_id,
             {sim::Gesture::cast, 0, spark_id}, created.encounter.weapons(),
             created.encounter.abilities()
        ).empty(),
        "the premise: the spell's band is lit even with nobody in it"
    );
    expect(
        asked(created.encounter, soldier_id),
        "and a spell whose band reaches only empty ground is not something to do"
    );
}

void a_cast_that_would_land_on_somebody_is_something_to_do() {
    auto definition = lonely_board();
    definition.units[0].ability_ids = {spark_id};
    definition.units[0].weapon_ids.clear();
    // Three tiles east, which is inside the spell's band once the soldier has
    // walked and outside a bare hand's reach either way.
    definition.units[1].position = {3, 0};
    auto created = sim::create_encounter(definition);
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        !asked(created.encounter, soldier_id),
        "a spell that would land on somebody is something to do"
    );
}

// The case the owner named. A restoring cast is refused nothing by the engine
// and changes nobody, and `apply`'s own loop passes over a character who is not
// short of health.
void a_mercy_nobody_needs_is_not_something_to_do() {
    auto definition = lonely_board();
    definition.units[0].ability_ids = {mercy_id};
    definition.units[0].weapon_ids.clear();
    sim::UnitDefinition ally;
    ally.id = ally_id;
    ally.unit_type_id = 101;
    ally.side = sim::Side::first;
    ally.position = {2, 0};
    ally.health = 20;
    ally.strength = 1;
    ally.movement = 0;
    ally.action_points = 1;
    definition.units.push_back(ally);
    auto created = sim::create_encounter(definition);
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        asked(created.encounter, soldier_id),
        "a restoring spell over nobody short of health is not something to do"
    );
}

void a_mercy_somebody_needs_is_something_to_do() {
    auto definition = lonely_board();
    definition.units[0].ability_ids = {mercy_id};
    definition.units[0].weapon_ids.clear();
    sim::UnitDefinition ally;
    ally.id = ally_id;
    ally.unit_type_id = 101;
    ally.side = sim::Side::first;
    ally.position = {2, 0};
    ally.health = 20;
    ally.strength = 1;
    ally.movement = 0;
    ally.action_points = 1;
    definition.units.push_back(ally);
    // A character is authored at full health, so the only way to be short of it
    // is to have been hit. An archer three tiles from the ally does that and
    // reaches nobody with its feet.
    add_archer(definition, {5, 0});
    auto created = sim::create_encounter(definition);

    pass_the_turn(created.encounter);
    sim::Command shot;
    shot.type = sim::CommandType::attack;
    shot.unit_id = archer_id;
    shot.target_id = ally_id;
    apply_or_fail(created.encounter, shot, "the archer wounds the ally");
    pass_the_turn(created.encounter);
    for (const sim::UnitSnapshot& unit : created.encounter.snapshot().units) {
        if (unit.id != ally_id) continue;
        expect(
            unit.health > 0 && unit.health < unit.maximum_health,
            "the premise: the ally is standing and short of health"
        );
    }

    walk_the_soldier(created.encounter, {1, 0});
    expect(
        !asked(created.encounter, soldier_id),
        "a restoring spell over somebody short of health is something to do"
    );
}

void an_item_that_would_do_nothing_is_not_something_to_do() {
    auto definition = lonely_board();
    definition.units[0].item_ids = {draught_id};
    definition.units[0].item_counts = {2};
    auto created = sim::create_encounter(definition);
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        asked(created.encounter, soldier_id),
        "a draught carried at full health is not something to do"
    );
}

void an_item_somebody_needs_is_something_to_do() {
    auto definition = lonely_board();
    definition.units[0].item_ids = {draught_id};
    definition.units[0].item_counts = {2};
    definition.units[0].weapon_ids.clear();
    // The archer three tiles east wounds the soldier and nothing else can be
    // done about it: the walk goes down the second row, out of every reach.
    add_archer(definition, {3, 0});
    auto created = sim::create_encounter(definition);

    pass_the_turn(created.encounter);
    sim::Command shot;
    shot.type = sim::CommandType::attack;
    shot.unit_id = archer_id;
    shot.target_id = soldier_id;
    apply_or_fail(created.encounter, shot, "the archer wounds the soldier");
    pass_the_turn(created.encounter);

    walk_the_soldier(created.encounter, {0, 1});
    for (const sim::UnitSnapshot& unit : created.encounter.snapshot().units) {
        if (unit.id != soldier_id) continue;
        expect(
            unit.health > 0 && unit.health < unit.maximum_health,
            "the premise: the soldier is standing and short of health"
        );
    }
    expect(
        !asked(created.encounter, soldier_id),
        "the same draught carried by somebody who needs it is something to do"
    );
}

void an_item_the_rules_cannot_use_is_not_something_to_do() {
    auto definition = lonely_board();
    definition.units[0].item_ids = {keepsake_id};
    definition.units[0].item_counts = {1};
    auto created = sim::create_encounter(definition);
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        asked(created.encounter, soldier_id),
        "a keepsake is carried, listed and counted, and using it is refused"
    );
}

// Nobody else's turn is ever ended by this, whatever the board looks like.
void nobody_elses_turn_is_ended() {
    auto created = sim::create_encounter(lonely_board());
    walk_the_soldier(created.encounter, {1, 0});
    expect(
        !asked(created.encounter, distant_id),
        "the side that is not acting is never finished for"
    );
    expect(
        !asked(created.encounter, 99),
        "and neither is a character this board does not carry"
    );

    // Finished already: there is no turn left to close.
    sim::Command wait;
    wait.type = sim::CommandType::wait;
    wait.unit_id = soldier_id;
    apply_or_fail(created.encounter, wait, "the soldier ends its own turn");
    expect(
        !asked(created.encounter, soldier_id),
        "and a character already finished is not finished again"
    );
}

// Under an order that names its actor, everybody else is locked out. Being
// locked out is not the same fact as having nothing to do.
void a_character_locked_out_is_not_finished_for() {
    auto definition = lonely_board();
    definition.turn_order = sim::TurnOrder::initiative;
    sim::UnitDefinition second;
    second.id = ally_id;
    second.unit_type_id = 101;
    second.side = sim::Side::first;
    second.position = {5, 0};
    second.health = 20;
    second.strength = 4;
    second.movement = 1;
    second.action_points = 2;
    definition.units.push_back(second);
    auto created = sim::create_encounter(definition);
    const auto snapshot = created.encounter.snapshot();
    if (snapshot.active_unit_id == 0) {
        std::cerr << "FAIL: the initiative order named no actor; the premise "
                     "of this case no longer holds\n";
        ++failures;
        return;
    }
    int locked_out = 0;
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == snapshot.active_unit_id) continue;
        if (unit.side != snapshot.active_side) continue;
        ++locked_out;
        expect(
            !asked(created.encounter, unit.id),
            "somebody else's open activation locks a character out, and that "
            "is not the same as having nothing to do"
        );
    }
    expect(locked_out > 0, "the premise: somebody was locked out to ask about");
}

// ---------------------------------------------------------------------------
// And the client that has to act on it
//
// The judgement above is the answer; this is the gesture. The translation unit
// both consoles compose their board from is driven over the same board with a
// scripted thumb, and what it must produce after the walk is the `wait` rather
// than a menu.
// ---------------------------------------------------------------------------

class Silence final : public turn::ReportSink {
public:
    void line(const char*) override {}
};

class ScriptedClient final : public turn::TurnClient {
public:
    ScriptedClient(turn::ReportSink& sink, std::vector<std::uint16_t> script)
        : TurnClient(sink), script_(std::move(script)) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay&) override {}

    std::uint16_t next_press() override {
        if (at_ >= script_.size()) return turn::pad_end_of_script;
        return script_[at_++];
    }

private:
    std::vector<std::uint16_t> script_;
    std::size_t at_{0};
};

void the_console_ends_the_turn_rather_than_reopening_the_menu() {
    auto created = sim::create_encounter(lonely_board());
    expect(static_cast<bool>(created), "the board is valid content");
    if (!created) return;

    Silence sink;
    // A picks the soldier up and opens its menu on WALK; A takes that row; the
    // press right steps onto the tile beside it; A commits the walk.
    ScriptedClient host(
        sink, {turn::pad_a, turn::pad_a, turn::pad_right, turn::pad_a}
    );
    client::Roster roster;
    roster.rebuild(created.encounter.snapshot());
    host.set_viewport(20, 10);
    host.battle_begins(
        created.encounter.snapshot(), roster, sim::Side::first, {}
    );
    host.battle_definitions(
        created.encounter.weapons(), created.encounter.abilities(),
        created.encounter.items(), created.encounter.objectives()
    );
    host.draw(created.encounter.snapshot(), roster);

    const client::Intent walk =
        host.next_intent(created.encounter.snapshot(), roster);
    expect(
        walk.kind == client::IntentKind::move_to &&
            walk.unit_id == soldier_id,
        "the script walks the soldier"
    );
    if (walk.kind != client::IntentKind::move_to) return;

    sim::Command command;
    command.type = sim::CommandType::move;
    command.unit_id = walk.unit_id;
    command.destination = walk.destination;
    const auto result = created.encounter.apply(command);
    expect(static_cast<bool>(result), "and the engine takes it");
    host.report(result, roster);
    host.draw(created.encounter.snapshot(), roster);

    // No press is left in the script, so anything but the `wait` would read the
    // end of it and ask to quit. That is what makes this a question about the
    // judgement rather than about the thumb.
    const client::Intent next =
        host.next_intent(created.encounter.snapshot(), roster);
    expect(
        next.kind == client::IntentKind::wait && next.unit_id == soldier_id,
        "and the character with nothing left ends its own turn without being "
        "asked"
    );
}

}  // namespace

int main() {
    a_character_that_has_not_walked_is_never_finished_for();
    a_walked_character_alone_on_the_board_is_finished();
    a_strike_in_reach_is_something_to_do();
    a_second_weapon_is_asked_about_too();
    a_talk_somebody_would_answer_is_something_to_do();
    a_cast_over_empty_ground_is_not_something_to_do();
    a_cast_that_would_land_on_somebody_is_something_to_do();
    a_mercy_nobody_needs_is_not_something_to_do();
    a_mercy_somebody_needs_is_something_to_do();
    an_item_that_would_do_nothing_is_not_something_to_do();
    an_item_somebody_needs_is_something_to_do();
    an_item_the_rules_cannot_use_is_not_something_to_do();
    nobody_elses_turn_is_ended();
    a_character_locked_out_is_not_finished_for();
    the_console_ends_the_turn_rather_than_reopening_the_menu();
    if (failures == 0) {
        std::cout << "spent turn: a character with nothing to do ends it\n";
    }
    return failures == 0 ? 0 : 1;
}
