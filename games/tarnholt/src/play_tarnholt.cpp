// SPDX-License-Identifier: MIT
// Native playthrough of The Tarnholt Line.
//
// Unlike the demo, this campaign exercises the whole authoring vocabulary: a
// story entry node, weapon reach, a multi-tile move, a magical area ability, a
// map whose outcome is decided by a defeat-target objective while a
// protect-target objective is still pending, a conversation that takes a
// character off a board alive, a map won by outlasting rounds while waves of
// reinforcements keep arriving, and a last map decided by one named character.
//
// Six boards, of which this walk plays five: the sixth is reached only by
// talking rather than killing, and the branch that opens it is read from a
// world flag that `CampaignCursor` cannot evaluate: the light cursor this file
// drives holds no campaign state. It therefore takes the unconditional thread
// every time, deliberately, and the branch is proved through the real campaign
// runtime by `tests/client/tarnholt_branch_test.cpp`.
// The conversation itself is played here, on the board it happens on, because
// what a talk does to a battle is the simulation's business rather than the
// campaign's.

#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::uint64_t placement(const std::string& encounter, const std::string& key) {
    return core::stable_content_id_v1(encounter + "/" + key);
}

// A canonical hash written the way the goldens beside it are written, so a
// deliberate rule change reads the new value straight out of the failure
// instead of hunting for a way to print it.
std::string hex(std::uint64_t value) {
    std::string digits(16, '0');
    for (int index = 15; index >= 0; --index) {
        digits[static_cast<std::size_t>(index)] =
            "0123456789abcdef"[value & 0xFULL];
        value >>= 4U;
    }
    return "0x" + digits;
}

const sim::UnitSnapshot* unit_in(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId id
) {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == id) return &unit;
    }
    return nullptr;
}

int separation_of(sim::Position from, sim::Position to) {
    const int dx = from.x - to.x;
    const int dy = from.y - to.y;
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

// How far a tile sits outside a band: zero once it is inside.
int outside_of_band(int away, int minimum, int maximum) {
    if (away > maximum) return away - maximum;
    if (away < minimum) return minimum - away;
    return 0;
}

int living_on_board(const sim::EncounterSnapshot& snapshot, sim::Side side) {
    int count = 0;
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.side != side) continue;
        if (unit.health <= 0 || unit.departed || !unit.arrived) continue;
        ++count;
    }
    return count;
}

// One activation for `hunter` against `quarry`: spend the ability if it will
// land, otherwise strike, otherwise take the reachable tile that gets furthest
// into the weapon's band. The walk is chosen out of the engine's own reachable
// set rather than by stepping east and hoping, which matters on every board in
// this campaign: each of them has ground somebody cannot cross.
//
// The ability is tried first on purpose. An ability provokes no counterattack,
// and the Marshal's answer is the hardest blow in the campaign.
bool press_towards(
    sim::Encounter& encounter,
    sim::UnitId hunter,
    sim::UnitId quarry,
    sim::ContentId ability
) {
    const auto snapshot = encounter.snapshot();
    const sim::UnitSnapshot* const walker = unit_in(snapshot, hunter);
    const sim::UnitSnapshot* const target = unit_in(snapshot, quarry);
    if (walker == nullptr || target == nullptr) return false;
    if (walker->health <= 0 || target->health <= 0) return false;
    if (ability != 0 &&
        static_cast<bool>(encounter.apply(
            {sim::CommandType::ability, hunter, target->position, 0, ability}
        ))) {
        return true;
    }
    if (static_cast<bool>(encounter.apply(
            {sim::CommandType::attack, hunter, {}, quarry, 0}
        ))) {
        return true;
    }
    const int minimum = static_cast<int>(walker->minimum_reach);
    const int maximum = static_cast<int>(walker->maximum_reach);
    int closest = outside_of_band(
        separation_of(walker->position, target->position), minimum, maximum
    );
    sim::Position chosen = walker->position;
    for (const sim::Position tile : sim::reachable_tiles(snapshot, hunter)) {
        const int away = outside_of_band(
            separation_of(tile, target->position), minimum, maximum
        );
        if (away < closest) {
            closest = away;
            chosen = tile;
        }
    }
    if (chosen == walker->position) return false;
    return static_cast<bool>(encounter.apply(
        {sim::CommandType::move, hunter, chosen, 0, 0}
    ));
}

// A board that authors a deployment region opens in the deployment phase, and
// nothing else may be commanded until it closes. This walk takes the
// arrangement the content authored rather than rearranging it, which is what
// makes the region's own tiles the thing under test rather than this file's
// taste in tactics.
bool begin_the_battle(sim::Encounter& encounter) {
    if (!encounter.snapshot().deploying) return true;
    return static_cast<bool>(
        encounter.apply({sim::CommandType::begin_battle, 0, {}, 0, 0})
    );
}

// Everybody on the active side waits, one at a time, respecting an ordered
// turn order's right to name who acts next.
bool wait_out_a_turn(sim::Encounter& encounter) {
    const auto snapshot = encounter.snapshot();
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.side != snapshot.active_side) continue;
        if (unit.health <= 0 || unit.departed || !unit.arrived) continue;
        if (snapshot.active_unit_id != 0 &&
            snapshot.active_unit_id != unit.id) {
            continue;
        }
        if (static_cast<bool>(
                encounter.apply({sim::CommandType::wait, unit.id, {}, 0, 0})
            )) {
            return true;
        }
    }
    return false;
}

// Hands the battle to the other side.
//
// This campaign plays in side blocks, where that is not one gesture: the engine
// names no actor and the block stays open until nobody on the side is left to
// act, so handing over means waiting out everybody who has not finished. Under
// alternating and initiative the first wait does it and the loop stops on the
// same condition. The bound is a guard against a side that cannot finish rather
// than a budget: no board here fields sixty-four characters.
bool hand_the_turn_over(sim::Encounter& encounter) {
    const sim::Side opened = encounter.snapshot().active_side;
    for (int guard = 0; guard < 64; ++guard) {
        if (encounter.snapshot().active_side != opened) return true;
        if (!wait_out_a_turn(encounter)) return false;
    }
    return encounter.snapshot().active_side != opened;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: grandleon_tarnholt_playthrough <tarnholt.gpk>\n";
        return 64;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open package\n";
        return 66;
    }
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    const auto loaded = pf::load_mock_package(
        bytes,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    if (!loaded) {
        std::cerr << "package rejected: " << pf::error_name(loaded.error) << '\n';
        return 65;
    }

    auto campaign = pr::load_campaign(
        loaded.package, core::stable_content_id_v1("tarnholt_line")
    );
    if (!campaign) {
        std::cerr << "campaign rejected: "
                  << pr::error_name(campaign.error) << '\n';
        return 65;
    }
    pr::CampaignCursor cursor(std::move(campaign.definition));

    // The campaign opens on a chained prologue: three story nodes, each
    // carrying its own dialogue, before the first battle. The v0 runtime could
    // not represent a story node at all.
    for (int beat = 0; beat < 3; ++beat) {
        expect(
            cursor.current().kind == pr::CampaignNodeKind::story,
            "prologue beat " + std::to_string(beat) + " is a story node"
        );
        expect(
            !cursor.current().dialogue_ids.empty(),
            "prologue beat " + std::to_string(beat) + " carries dialogue"
        );
        expect(
            cursor.advance_story() == pr::CampaignError::none,
            "prologue beat " + std::to_string(beat) + " advances"
        );
    }

    // ---- Map one: Fordlight Crossing, decided by wiping out the opposition.
    const std::string ford = "tarnholt_line/fordlight_battle";
    expect(
        cursor.current().encounter_id == core::stable_content_id_v1(ford),
        "story node leads to the ford"
    );
    auto ford_load = pr::load_encounter(loaded.package, cursor.current().encounter_id);
    if (!ford_load) {
        std::cerr << "ford encounter rejected: "
                  << pr::error_name(ford_load.error) << '\n';
        return 65;
    }
    auto ford_created = sim::create_encounter(ford_load.definition);
    if (!ford_created) {
        std::cerr << "ford encounter invalid: "
                  << sim::error_name(ford_created.error) << '\n';
        return 65;
    }

    {
        const auto snapshot = ford_created.encounter.snapshot();
        expect(snapshot.units.size() == 9, "the ford deploys nine units");
        const auto archer = placement(ford, "dawn_archer_ford");
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.id != archer) continue;
            // The long bow is authored with range three and is now honoured.
            // A true bow band: it cannot strike an adjacent enemy.
            expect(unit.minimum_reach == 2, "the archer cannot strike closer than two");
            expect(unit.maximum_reach == 3, "the archer reaches three tiles");
            expect(unit.movement == 4, "the archer moves four tiles");
            expect(!unit.ability_ids.empty(), "the archer owns an ability");
        }
    }

    // A four-tile move in one activation, which the one-step v0 rule forbade.
    expect(
        static_cast<bool>(ford_created.encounter.apply(
            {sim::CommandType::move, placement(ford, "dawn_archer_ford"),
             {1, 1}, 0, 0}
        )),
        "the archer crosses four tiles in one activation"
    );
    // The guard's block is still open behind her: a walk hands nothing over in
    // side blocks. So the rest of the line waits before the Coil may answer.
    expect(
        hand_the_turn_over(ford_created.encounter),
        "the guard's block closes and the ford passes to the Coil"
    );
    // An area magical ability, aimed where it can only catch its own line.
    // Centred at (8,3) its diamond covers the Ashen archer at (8,2) and the
    // Ashen knight at (9,3), and the Stormcaller stands with them on the second
    // side, so the cast is accepted, the ground is covered, and not one point
    // of health moves. A damaging cast harms the caster's opponents and nobody
    // else; here there are none of those under it, and the Coil has spent an
    // activation on a spell that could never have paid.
    //
    // The other half of the same rule, what the diamond does to opponents, is
    // pinned in `tests/simulation`, where a board can be built to put both
    // sides under one blast. It cannot be pinned here: the Stormcaller's band
    // is one to two tiles and the whole Dawn Guard is seven tiles west of it.
    const auto health_at_ford = [&](const char* placement_id) {
        const auto snapshot = ford_created.encounter.snapshot();
        const sim::UnitId id = placement(ford, placement_id);
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.id == id) return static_cast<int>(unit.health);
        }
        return -1;
    };
    const int archer_before = health_at_ford("ashen_archer_ford");
    const int knight_before = health_at_ford("ashen_knight_north");
    const auto storm = ford_created.encounter.apply(
        {sim::CommandType::ability, placement(ford, "ashen_storm_ford"),
         {8, 3}, 0, core::stable_content_id_v1("chain_storm")}
    );
    expect(static_cast<bool>(storm), "the stormcaller resolves chain storm");
    bool touched_anybody = false;
    for (const sim::Event& event : storm.events) {
        if (event.type == sim::EventType::unit_damaged ||
            event.type == sim::EventType::unit_defeated ||
            event.type == sim::EventType::attack_missed) {
            touched_anybody = true;
        }
    }
    expect(
        !touched_anybody,
        "chain storm over the Coil's own line reports no blow at all"
    );
    expect(
        health_at_ford("ashen_archer_ford") == archer_before &&
            health_at_ford("ashen_knight_north") == knight_before,
        "and both Ashen units inside its diamond keep every point"
    );

    // Advance the campaign off the ford. The ford's own outcome is exercised by
    // the mechanics above; what matters here is that a decided encounter routes
    // through the campaign graph, so the transition is driven with the ford's
    // objective satisfied.
    {
        std::vector<sim::ObjectiveResult> satisfied{
            {core::stable_content_id_v1("defeat_all_opponents"),
             sim::ObjectiveState::satisfied}
        };
        expect(
            cursor.advance_after(sim::Outcome::first_side_won, satisfied) ==
                pr::CampaignError::none,
            "a won ford advances the campaign"
        );
    }

    // An interlude sequence sits after the ford: the cost of the crossing, the
    // marching order that names a Warden and recruits two more of the guard,
    // and the road to a village that was burned in the spring.
    for (int beat = 0; beat < 3; ++beat) {
        expect(
            cursor.current().kind == pr::CampaignNodeKind::story,
            "interlude beat " + std::to_string(beat) + " is a story node"
        );
        expect(
            !cursor.current().dialogue_ids.empty(),
            "interlude beat " + std::to_string(beat) + " carries dialogue"
        );
        expect(
            cursor.advance_story() == pr::CampaignError::none,
            "interlude beat " + std::to_string(beat) + " advances"
        );
    }

    // ---- Map two: the Harrow Burn, and the man in it who is not an enemy.
    //
    // What is proved here is the conversation, not the clearance: a talk aimed
    // at somebody two tiles away is refused by band, a talk aimed at an
    // ordinary Ashen Coil soldier is refused by name, and a talk that lands
    // takes the levy off the board *alive*: departed rather than defeated, so
    // no defeat event is emitted and nobody earns anything for it.
    const std::string burn = "tarnholt_line/harrow_burn_battle";
    expect(
        cursor.current().encounter_id == core::stable_content_id_v1(burn),
        "the road to Harrow leads to the burn"
    );
    auto burn_load = pr::load_encounter(loaded.package, cursor.current().encounter_id);
    if (!burn_load) {
        std::cerr << "burn encounter rejected: "
                  << pr::error_name(burn_load.error) << '\n';
        return 65;
    }
    auto burn_created = sim::create_encounter(burn_load.definition);
    if (!burn_created) {
        std::cerr << "burn encounter invalid: "
                  << sim::error_name(burn_created.error) << '\n';
        return 65;
    }
    {
        const auto levy = placement(burn, "ashen_levy_coll");
        const auto picket = placement(burn, "ashen_knight_harrow_north");
        const auto captain = placement(burn, "dawn_commander_harrow");
        const auto opening = burn_created.encounter.snapshot();
        expect(opening.units.size() == 10, "the burn deploys ten characters");
        const sim::UnitSnapshot* const conscript = unit_in(opening, levy);
        expect(
            conscript != nullptr && conscript->talk_record_id != 0,
            "the levy is somebody a talk may reach"
        );
        // The boat hook is the only reach-one-to-two weapon in the campaign,
        // and it is what makes him worth recruiting rather than only worth
        // sparing.
        expect(
            conscript != nullptr && conscript->minimum_reach == 1 &&
                conscript->maximum_reach == 2,
            "the levy carries a hook that reaches two tiles"
        );
        expect(
            burn_created.encounter
                    .apply({sim::CommandType::talk, captain, {}, levy, 0})
                    .error == sim::CommandError::target_out_of_range,
            "a talk does not carry across the village"
        );
        // Walk the Captain up to him. `press_towards` closes to the walker's
        // own weapon band, which for a Guard Sword is the one tile a talk
        // reaches, so the same walk that would bring her into a fight brings
        // her into a conversation.
        for (int step = 0; step < 24; ++step) {
            const auto standing = burn_created.encounter.snapshot();
            const sim::UnitSnapshot* const her = unit_in(standing, captain);
            const sim::UnitSnapshot* const him = unit_in(standing, levy);
            if (her == nullptr || him == nullptr) break;
            if (separation_of(her->position, him->position) <= 1) break;
            if (standing.active_side != sim::Side::first) {
                if (!hand_the_turn_over(burn_created.encounter)) break;
                continue;
            }
            if (press_towards(burn_created.encounter, captain, levy, 0)) {
                continue;
            }
            // She has spent her walk and the burn is wider than one stride, so
            // the guard's block has to close and come round again before she
            // can take another.
            if (!hand_the_turn_over(burn_created.encounter)) break;
        }
        // The crossing finished her. A Guard Sword's bearer has one action
        // point and the walk spent it, so being adjacent is not yet being able
        // to speak: the guard's block has to close, the Coil's after it, and
        // the round come back round to a Captain who is standing next to him
        // with her turn untouched.
        for (int handover = 0; handover < 4; ++handover) {
            const auto standing = burn_created.encounter.snapshot();
            const sim::UnitSnapshot* const her = unit_in(standing, captain);
            if (her != nullptr && !her->has_acted &&
                standing.active_side == sim::Side::first) {
                break;
            }
            if (!hand_the_turn_over(burn_created.encounter)) break;
        }
        // An ordinary Ashen Coil soldier authors no talk, and the refusal says
        // exactly that rather than going quiet. It is asked before the range
        // rule gets a chance to answer, which is the order the engine promises.
        expect(
            burn_created.encounter
                    .apply({sim::CommandType::talk, captain, {}, picket, 0})
                    .error == sim::CommandError::not_talkable,
            "an Ashen Coil knight is not somebody to talk to"
        );
        const auto heard = burn_created.encounter.apply(
            {sim::CommandType::talk, captain, {}, levy, 0}
        );
        expect(static_cast<bool>(heard), "the Captain hears the levy out");
        bool talked = false;
        bool anybody_fell = false;
        for (const sim::Event& event : heard.events) {
            if (event.type == sim::EventType::unit_talked &&
                event.content_id ==
                    core::stable_content_id_v1("coll_rankin_heard")) {
                talked = true;
            }
            if (event.type == sim::EventType::unit_defeated) anybody_fell = true;
        }
        expect(talked, "the talk names the flag the campaign reads back");
        expect(!anybody_fell, "and nobody was defeated by being listened to");
        const auto after = burn_created.encounter.snapshot();
        const sim::UnitSnapshot* const gone = unit_in(after, levy);
        expect(
            gone != nullptr && gone->departed && gone->health > 0,
            "the levy leaves the board alive"
        );
        expect(
            after.outcome == sim::Outcome::ongoing,
            "and the rest of the Coil is still standing in the burn"
        );
    }

    // Advance off the burn with its objective satisfied. The talked branch is
    // a world flag this cursor cannot hold, so the walk takes the fallback:
    // the long way round, with the company it set out with.
    {
        std::vector<sim::ObjectiveResult> satisfied{
            {core::stable_content_id_v1("clear_the_burn"),
             sim::ObjectiveState::satisfied}
        };
        expect(
            cursor.advance_after(sim::Outcome::first_side_won, satisfied) ==
                pr::CampaignError::none,
            "a cleared burn advances the campaign"
        );
        expect(
            cursor.current().id == core::stable_content_id_v1("the_long_way"),
            "and a cursor that cannot read a world flag takes the long way"
        );
    }
    for (int beat = 0; beat < 2; ++beat) {
        expect(
            cursor.current().kind == pr::CampaignNodeKind::story,
            "long-way beat " + std::to_string(beat) + " is a story node"
        );
        expect(
            cursor.advance_story() == pr::CampaignError::none,
            "long-way beat " + std::to_string(beat) + " advances"
        );
    }

    // ---- Map three: the yard at Emberhall, won by outlasting rather than by
    // beating anybody, with waves that keep arriving while the count runs.
    const std::string yard = "tarnholt_line/emberhall_battle";
    expect(
        cursor.current().encounter_id == core::stable_content_id_v1(yard),
        "both threads reach the yard at Emberhall"
    );
    auto yard_load = pr::load_encounter(loaded.package, cursor.current().encounter_id);
    if (!yard_load) {
        std::cerr << "yard encounter rejected: "
                  << pr::error_name(yard_load.error) << '\n';
        return 65;
    }
    auto yard_created = sim::create_encounter(yard_load.definition);
    if (!yard_created) {
        std::cerr << "yard encounter invalid: "
                  << sim::error_name(yard_created.error) << '\n';
        return 65;
    }
    expect(
        begin_the_battle(yard_created.encounter),
        "the yard's deployment phase closes on the authored arrangement"
    );
    {
        expect(
            yard_load.definition.objectives.size() == 1 &&
                yard_load.definition.objectives[0].kind ==
                    sim::ObjectiveKind::survive_rounds &&
                yard_load.definition.objectives[0].round_count == 6,
            "the yard is won by surviving six rounds and nothing else"
        );
        expect(
            !yard_load.definition.deployment_tiles.empty() &&
                yard_load.deployment_capacity == 5,
            "the yard authors a muster region and holds five of the company"
        );
        const auto opening = yard_created.encounter.snapshot();
        // Seven placements for the company, three of the Coil standing in the
        // yard, and five more of it expanded out of two authored recurrences.
        expect(opening.units.size() == 15, "the yard knows about fifteen");
        expect(
            living_on_board(opening, sim::Side::second) == 3,
            "three of them are on the board when it opens"
        );
        expect(opening.round == 0, "and no round has completed yet");
    }
    {
        int arrivals = 0;
        std::uint32_t reached = 0;
        for (int turn = 0; turn < 600; ++turn) {
            const auto standing = yard_created.encounter.snapshot();
            if (standing.outcome != sim::Outcome::ongoing) break;
            reached = standing.round;
            const auto before = living_on_board(standing, sim::Side::second);
            if (!wait_out_a_turn(yard_created.encounter)) break;
            const auto now = yard_created.encounter.snapshot();
            arrivals += living_on_board(now, sim::Side::second) - before;
        }
        const auto ended = yard_created.encounter.snapshot();
        expect(
            arrivals == 5,
            "five reinforcements walked into the yard while the count ran"
        );
        expect(
            reached >= 5,
            "the count reached its sixth round"
        );
        expect(
            ended.outcome == sim::Outcome::first_side_won,
            "outlasting the sixth round wins the yard"
        );
        expect(
            ended.objectives.size() == 1 &&
                ended.objectives[0].state == sim::ObjectiveState::satisfied,
            "and the survive objective is what won it"
        );
        expect(
            cursor.advance_after(ended.outcome, ended.objectives) ==
                pr::CampaignError::none,
            "a held yard advances the campaign"
        );
    }
    for (int beat = 0; beat < 2; ++beat) {
        expect(
            cursor.current().kind == pr::CampaignNodeKind::story,
            "climb beat " + std::to_string(beat) + " is a story node"
        );
        expect(
            !cursor.current().dialogue_ids.empty(),
            "climb beat " + std::to_string(beat) + " carries dialogue"
        );
        expect(
            cursor.advance_story() == pr::CampaignError::none,
            "climb beat " + std::to_string(beat) + " advances"
        );
    }

    // ---- Map four: decided by a defeat-target objective, not by elimination.
    const std::string watch = "tarnholt_line/ashen_watch_battle";
    expect(
        cursor.current().encounter_id == core::stable_content_id_v1(watch),
        "the climb leads to the watch"
    );
    auto watch_load = pr::load_encounter(loaded.package, cursor.current().encounter_id);
    if (!watch_load) {
        std::cerr << "watch encounter rejected: "
                  << pr::error_name(watch_load.error) << '\n';
        return 65;
    }
    expect(
        watch_load.definition.objectives.size() == 2,
        "the watch carries two objectives"
    );
    auto watch_created = sim::create_encounter(watch_load.definition);
    if (!watch_created) {
        std::cerr << "watch encounter invalid: "
                  << sim::error_name(watch_created.error) << '\n';
        return 65;
    }

    // Take the Warden down and leave every other Ashen Coil unit standing, so
    // a win here can only come from the defeat-target objective.
    //
    // The order the Dawn Guard does it in is the whole lesson of the map, and
    // it is a lesson counterattacks taught. Captain Mirea and Warden Kesh have
    // the same fourteen health and the same three defence, but his Warden Blade
    // strikes for seven where her Guard Sword strikes for six. Walked up alone
    // she loses the duel exactly: two blows leave him on two and both of his
    // answers leave her on nothing. Soften him by a single point first and the
    // arithmetic inverts: two blows finish him, the second is lethal and
    // therefore unanswered, and she walks away with seven.
    //
    // So the archer opens. Her Long Bow's band starts at two and the Warden's
    // blade ends at one, so a shot from two tiles is a shot he cannot answer,
    // and the three health it takes off him is the difference between the
    // Captain winning and dying. That is the reach band being a trade rather
    // than a targeting parameter, played out in shipped content.
    const auto mirea = placement(watch, "dawn_commander_mirea");
    const auto archer = placement(watch, "dawn_archer_watch");
    const auto kesh = placement(watch, "ashen_commander_kesh");


    const auto separation = [](sim::Position from, sim::Position to) {
        const int dx = from.x - to.x;
        const int dy = from.y - to.y;
        return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    };
    const auto find_unit = [](const sim::EncounterSnapshot& snapshot,
                              sim::UnitId id) -> const sim::UnitSnapshot* {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.id == id) return &unit;
        }
        return nullptr;
    };
    // How far a tile sits outside a band: zero once it is inside. Walking by
    // this rather than by raw proximity is what stops the archer from closing
    // to a tile she cannot shoot from: the same `target_out_of_range` the
    // clients print as OUT OF RANGE, avoided rather than earned.
    const auto outside_band = [](int away, int minimum, int maximum) {
        if (away > maximum) return away - maximum;
        if (away < minimum) return minimum - away;
        return 0;
    };
    // One activation for `hunter`: strike the Warden if `strike` allows it and
    // the band reaches, otherwise take the reachable tile that gets furthest
    // into the band `minimum`..`maximum`. The walk is chosen out of the
    // engine's own reachable set rather than by stepping east and hoping. The
    // Watch has a mountain across the middle of its road, swamp along both
    // flanks, and the swamp is heavy going, so a straight line east stops at
    // the first cliff and a detour around it is not always affordable.
    //
    // Striking is a parameter rather than a thing every press does, and that is
    // the lesson of the map written as a rule: the Captain walks up to the
    // Warden and *waits* there until the arrow has landed. Standing next to him
    // costs her nothing, because in this engine only a blow is answered. It is
    // her own first strike that would buy his counter, and buying it before the
    // shot has taken three health off him is what loses her the duel.
    const auto press = [&](
        sim::UnitId hunter, int minimum, int maximum, bool strike
    ) {
        const auto snapshot = watch_created.encounter.snapshot();
        if (strike && static_cast<bool>(watch_created.encounter.apply(
                {sim::CommandType::attack, hunter, {}, kesh, 0}
            ))) {
            return true;
        }
        const sim::UnitSnapshot* walker = find_unit(snapshot, hunter);
        const sim::UnitSnapshot* warden = find_unit(snapshot, kesh);
        if (walker == nullptr || warden == nullptr) return false;
        int closest = outside_band(
            separation(walker->position, warden->position), minimum, maximum
        );
        sim::Position chosen = walker->position;
        // Row-major order out of the query, and a strict improvement to take a
        // tile, so the walk is one sequence however the board is stored.
        for (const sim::Position tile :
             sim::reachable_tiles(snapshot, hunter)) {
            const int away = outside_band(
                separation(tile, warden->position), minimum, maximum
            );
            if (away < closest) {
                closest = away;
                chosen = tile;
            }
        }
        if (chosen == walker->position) return false;
        return static_cast<bool>(watch_created.encounter.apply(
            {sim::CommandType::move, hunter, chosen, 0, 0}
        ));
    };

    bool decided = false;
    bool bow_spent = false;
    for (int round = 0; round < 400 && !decided; ++round) {
        const auto snapshot = watch_created.encounter.snapshot();
        if (snapshot.outcome != sim::Outcome::ongoing) {
            decided = true;
            break;
        }
        bool acted = false;
        if (snapshot.active_side == sim::Side::first) {
            // The Captain leads and the archer follows her up the road, which
            // is not an ordering choice so much as the road's: the Watch is a
            // ring of swamp around one lane of hard ground, the swamp is heavy
            // going, and whoever is behind on that lane cannot get past.
            const sim::UnitSnapshot* mover = find_unit(snapshot, mirea);
            if (mover != nullptr && mover->health > 0) {
                acted = press(
                    mirea,
                    static_cast<int>(mover->minimum_reach),
                    static_cast<int>(mover->maximum_reach),
                    bow_spent
                );
            }
            if (!acted && !bow_spent) {
                const sim::UnitSnapshot* warden = find_unit(snapshot, kesh);
                const std::int16_t before =
                    warden == nullptr ? 0 : warden->health;
                const sim::UnitSnapshot* shooter = find_unit(snapshot, archer);
                acted = shooter != nullptr &&
                        press(
                            archer,
                            static_cast<int>(shooter->minimum_reach),
                            static_cast<int>(shooter->maximum_reach),
                            true
                        );
                const auto after = watch_created.encounter.snapshot();
                const sim::UnitSnapshot* hurt = find_unit(after, kesh);
                if (hurt != nullptr && hurt->health < before) bow_spent = true;
            }
        }
        if (!acted) {
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (unit.side != snapshot.active_side || unit.health <= 0) {
                    continue;
                }
                acted = static_cast<bool>(watch_created.encounter.apply(
                    {sim::CommandType::wait, unit.id, {}, 0, 0}
                ));
                if (acted) break;
            }
        }
        if (!acted) break;
    }

    const auto watch_snapshot = watch_created.encounter.snapshot();
    expect(
        watch_snapshot.outcome == sim::Outcome::first_side_won,
        "felling the Warden wins the watch"
    );
    bool hostiles_remain = false;
    for (const sim::UnitSnapshot& unit : watch_snapshot.units) {
        if (unit.side == sim::Side::second && unit.health > 0) {
            hostiles_remain = true;
        }
    }
    expect(
        hostiles_remain,
        "the watch is won while Ashen Coil units are still standing"
    );
    expect(
        cursor.advance_after(
            watch_snapshot.outcome, watch_snapshot.objectives
        ) == pr::CampaignError::none,
        "the watch advances the campaign"
    );
    // The Watch's transition is a compound `all`: the Warden must be felled
    // AND Mirea must survive. Both hold here, so the victory branch is taken
    // rather than the unconditional fallback.
    expect(
        watch_snapshot.objectives.size() == 2,
        "both watch objectives are reported"
    );
    // A won watch does not end the campaign. It opens onto the tower taken and
    // then onto the second seal on the Warden's table, which is the turn: Kesh
    // was clearing a road for somebody, and the somebody arrives tomorrow.
    expect(
        cursor.current().id == core::stable_content_id_v1("watch_falls"),
        "a won watch reaches the tower's own beat"
    );
    for (int beat = 0; beat < 2; ++beat) {
        expect(
            cursor.current().kind == pr::CampaignNodeKind::story,
            "tower beat " + std::to_string(beat) + " is a story node"
        );
        expect(
            !cursor.current().dialogue_ids.empty(),
            "tower beat " + std::to_string(beat) + " carries dialogue"
        );
        expect(
            cursor.advance_story() == pr::CampaignError::none,
            "tower beat " + std::to_string(beat) + " advances"
        );
    }

    // ---- Map five: the Coldgate, and the one character it is about.
    const std::string gate = "tarnholt_line/coldgate_battle";
    expect(
        cursor.current().encounter_id == core::stable_content_id_v1(gate),
        "the second seal leads to the Coldgate"
    );
    auto gate_load = pr::load_encounter(loaded.package, cursor.current().encounter_id);
    if (!gate_load) {
        std::cerr << "gate encounter rejected: "
                  << pr::error_name(gate_load.error) << '\n';
        return 65;
    }
    auto gate_created = sim::create_encounter(gate_load.definition);
    if (!gate_created) {
        std::cerr << "gate encounter invalid: "
                  << sim::error_name(gate_created.error) << '\n';
        return 65;
    }
    const auto vorne = placement(gate, "ashen_marshal_vorne");
    expect(
        begin_the_battle(gate_created.encounter),
        "the gate's deployment phase closes on the authored arrangement"
    );
    {
        expect(
            gate_load.definition.objectives.size() == 2,
            "the gate carries a defeat-target and a protect-target"
        );
        expect(
            !gate_load.definition.deployment_tiles.empty() &&
                gate_load.deployment_capacity == 0,
            "the gate authors a region and no cap: a finale is everybody"
        );
        const auto opening = gate_created.encounter.snapshot();
        const sim::UnitSnapshot* const marshal = unit_in(opening, vorne);
        // The stat line that makes the last board unlike the five before it.
        // Five defence against a sword that swings for three; three resistance
        // against a campaign whose magic has never met any; a glaive that
        // answers from two tiles as well as from one.
        expect(
            marshal != nullptr && marshal->health == 24 &&
                marshal->maximum_health == 24,
            "the Marshal is twenty-four health"
        );
        expect(
            marshal != nullptr && marshal->defense == 5 &&
                marshal->resistance == 3,
            "and the first character in this campaign to resist a spell"
        );
        expect(
            marshal != nullptr && marshal->minimum_reach == 1 &&
                marshal->maximum_reach == 2,
            "his glaive reaches a tile beyond a sword"
        );
        expect(
            marshal != nullptr && marshal->talk_record_id == 0,
            "and nobody talks the Marshal down"
        );
        expect(
            gate_created.encounter
                    .apply({sim::CommandType::talk,
                            placement(gate, "dawn_commander_coldgate"), {},
                            vorne, 0})
                    .error != sim::CommandError::none,
            "a talk aimed at him is refused rather than quietly ignored"
        );
    }
    {
        // Four of the guard press the gate, each with the ability its class
        // brings, because an ability provokes no counterattack and the Iron
        // Vow's answer is the hardest blow in the campaign. The escort is left
        // standing so that a win here can only have come from the
        // defeat-target objective.
        struct Attacker final {
            const char* key;
            const char* ability;
        };
        const Attacker attackers[] = {
            {"dawn_knight_coldgate", "power_strike"},
            {"dawn_knight_coldgate_south", "power_strike"},
            {"dawn_commander_coldgate", "rally"},
            {"dawn_archer_coldgate", "volley"},
        };
        bool decided = false;
        for (int turn = 0; turn < 600 && !decided; ++turn) {
            const auto standing = gate_created.encounter.snapshot();
            if (standing.outcome != sim::Outcome::ongoing) {
                decided = true;
                break;
            }
            bool acted = false;
            if (standing.active_side == sim::Side::first) {
                for (const Attacker& attacker : attackers) {
                    acted = press_towards(
                        gate_created.encounter, placement(gate, attacker.key),
                        vorne, core::stable_content_id_v1(attacker.ability)
                    );
                    if (acted) break;
                }
            }
            if (!acted) acted = wait_out_a_turn(gate_created.encounter);
            if (!acted) break;
        }
        const auto ended = gate_created.encounter.snapshot();
        expect(
            ended.outcome == sim::Outcome::first_side_won,
            "felling the Marshal wins the Coldgate"
        );
        const sim::UnitSnapshot* const fallen = unit_in(ended, vorne);
        expect(
            fallen != nullptr && fallen->health <= 0,
            "and it was the Marshal who fell"
        );
        expect(
            living_on_board(ended, sim::Side::second) > 0,
            "with his escort still standing in the gate"
        );
        expect(
            ended.objectives.size() == 2,
            "both gate objectives are reported"
        );
        expect(
            cursor.advance_after(ended.outcome, ended.objectives) ==
                pr::CampaignError::none,
            "a won gate advances the campaign"
        );
    }
    expect(
        cursor.current().id == core::stable_content_id_v1("tower_cost"),
        "a won gate reaches the campaign's closing count"
    );
    expect(
        cursor.advance_story() == pr::CampaignError::none,
        "the closing count advances"
    );
    expect(cursor.complete(), "the campaign reaches a terminal node");
    expect(
        cursor.current().id == core::stable_content_id_v1("valley_held"),
        "the campaign reaches the victory terminal"
    );
    expect(
        !cursor.current().dialogue_ids.empty(),
        "the victory terminal carries dialogue"
    );

    // Golden values for this exact scripted sequence, pinned so that a change
    // to reach, area resolution, who a cast may harm, what a walk may cross,
    // action points, turn order, weapon power, counterattacks, or objective
    // evaluation has to be deliberate.
    //
    // Reading a moved value is the hard part, because two very different
    // causes look identical from the outside: a unit's *encoding* can grow, or
    // a *rule* can change, and either shifts every hash in the repository at
    // once. What tells them apart is which of these two boards moved and why,
    // so the taxonomy is written out here rather than rediscovered every time
    // one of these numbers has to be regenerated.
    //
    //   *The encoding grew.* Skill, luck, evasion and magic are canonical
    //   state; so is the pack a character carries; so is what a defeated
    //   character leaves behind and how often. Widening the encoding by any of
    //   them shifts every opening hash at once, including a package whose
    //   classes author none of the new fields and whose every number is
    //   therefore unchanged. Nothing about the battle moved; only its encoding
    //   did.
    //
    //   *A rule changed.* How often the weapon in hand lands is canonical
    //   state, so accuracy reaches every encounter's opening hash (the ford's
    //   and the watch's alike, and the reference vector's with them), even on
    //   a board where no unit has swung by the time the hash is taken. The
    //   encounter's random seed is canonical too and is derived from the
    //   opening board, so it moves every hash while no rule changed and no
    //   unit did anything different. Nothing in this sequence draws a random
    //   number.
    //
    //   A rule can also change *and reach only some of these boards*, which is
    //   the case worth being ready for because it looks at first like content
    //   having moved. Who acts next is canonical state and so is what a
    //   character adds to the reach of what it holds: two boards laid out
    //   identically under different turn orders accept opposite commands, and
    //   two archers whose in-hand band has saturated can still strike
    //   different tiles with a second weapon. Turn order reaches both of these
    //   boards, because this game sets one for every battle in it: the project
    //   states `sideBlocks` and no board here overrides it, so the order is
    //   resolved onto the record before the package is written and both values
    //   below move when it changes. Reach travels the other way. A bonus is a
    //   fact about a person, and both boards are played straight off the
    //   package with no member's specificity riding in on them, so the boards a
    //   campaign puts the Fordlight archer on move where these two do not. What
    //   the ground charges is the same shape again and the clearest example of
    //   it: a price is canonical only where something is priced, so a board of
    //   road and grass moves not at all while these two (the ford has forest,
    //   the watch has marsh) move together. A rule of this shape moves the
    //   derived seed of the boards it reaches, and therefore their dice, and
    //   leaves every other board exactly where it was.
    //
    //   *Content moved underneath the encoding.* The Dawn Mage knows Cinder
    //   Arc, the Dawn Mage and the Dawn Healer carry a Field Tonic, and the
    //   Ashen Archer leaves a Field Tonic three times in five. Known
    //   abilities, the pack and the drop table are all canonical state, so
    //   authoring any of them moves the hash of the board that unit deploys on
    //   and no other. The Dawn Mage deploys on the ford and not on the watch:
    //   a change to its abilities moves the ford alone, and one that moved
    //   both would mean something else entirely.
    //
    //   A class stat is the widest version of that, because a class stands on
    //   every board its people are placed on. How many action points a
    //   character has is canonical, and every class in this campaign carries
    //   two, so a change to that number moves both values below and every
    //   other board in the game with them, which is what a stat shared by
    //   everybody looks like when it moves.
    //
    //   *Nothing that matters moved.* A hash is a fact about a battle rather
    //   than about the package it travels in. The campaign around these two
    //   boards runs to six maps, a talkable character, a survive objective,
    //   recurring waves, two deployment regions, a capacity, a store, grants,
    //   recruits, member specificities and a class written for one Marshal.
    //   Not one of them is written onto *these two boards*. A package can
    //   double in size while these two arrangements of characters stay exactly
    //   as they are, and hash them exactly the same.
    //
    //   *Somebody was placed on it.* Where a character stands is canonical and
    //   so is that they are there at all, so adding the levy on the east road
    //   moved this value and no other. It is the same shape as the widening
    //   below and for the same reason.
    //
    //   *The board itself was widened.* A map's terrain is canonical, so
    //   growing the ford's own board moves its hash and touches nothing else.
    //   The Fordlight was drawn ten columns across, which fits every console
    //   screen, and now runs to thirty-two: the crossing is exactly where it
    //   was and every character stands on the tile it always did, with open
    //   country added east of the far bank. The watch did not move, which is
    //   what a change to one board should look like.
    //
    // Two more facts about where these particular values are taken. The ford's
    // is insensitive to counterattacks, because that sequence walks and casts
    // and never issues a basic attack, and an ability provokes no counter. And
    // neither is an opening hash: each is taken where this file leaves its
    // board, so it pins the whole scripted sequence over that board and not
    // only the arrangement the package deployed. What has already happened is
    // canonical: who has fallen, who has walked, whose turn is spent. So a
    // fifth reason to read a moved value is that the script above reached a
    // different place, which is what a changed turn order does to it.
    expect(
        ford_created.encounter.canonical_hash() == 0x6e5193aeb71f1033ULL,
        "the ford reaches its golden canonical hash, got " +
            hex(ford_created.encounter.canonical_hash())
    );
    expect(
        watch_created.encounter.canonical_hash() == 0xbc4cee0e403c3366ULL,
        "the watch reaches its golden canonical hash, got " +
            hex(watch_created.encounter.canonical_hash())
    );

    // The losing routes. Four boards can end this campaign badly, and each one
    // has to reach the losing terminal by its own condition rather than by
    // anything being treated as satisfied because it was not reported.
    {
        const auto satisfied = [](const char* id) {
            return sim::ObjectiveResult{
                core::stable_content_id_v1(id), sim::ObjectiveState::satisfied
            };
        };
        const auto failed = [](const char* id) {
            return sim::ObjectiveResult{
                core::stable_content_id_v1(id), sim::ObjectiveState::failed
            };
        };
        // A fresh cursor walked as far as the named node along the thread this
        // file takes, so each losing route is exercised from the same road.
        const auto walk_to = [&](const char* stop) {
            auto replay = pr::load_campaign(
                loaded.package, core::stable_content_id_v1("tarnholt_line")
            );
            pr::CampaignCursor at(std::move(replay.definition));
            struct Step final {
                int story;
                const char* objective;
                const char* node;
            };
            const Step road[] = {
                {3, "defeat_all_opponents", "fordlight_battle"},
                {3, "clear_the_burn", "harrow_burn_battle"},
                {2, "hold_the_yard", "emberhall_battle"},
                {2, "fell_the_warden", "ashen_watch_battle"},
                {2, "keep_mirea_alive", "coldgate_battle"},
            };
            for (const Step& step : road) {
                for (int beat = 0; beat < step.story; ++beat) {
                    (void)at.advance_story();
                }
                if (std::string(step.node) == std::string(stop)) return at;
                std::vector<sim::ObjectiveResult> won{satisfied(step.objective)};
                if (std::string(step.node) == "ashen_watch_battle") {
                    won.push_back(satisfied("keep_mirea_alive"));
                }
                (void)at.advance_after(sim::Outcome::first_side_won, won);
            }
            return at;
        };

        // The ford, lost outright.
        {
            pr::CampaignCursor at = walk_to("fordlight_battle");
            expect(
                at.current().id ==
                    core::stable_content_id_v1("fordlight_battle"),
                "the replay reaches the ford"
            );
            const std::vector<sim::ObjectiveResult> lost{
                failed("defeat_all_opponents")
            };
            expect(
                at.advance_after(sim::Outcome::second_side_won, lost) ==
                    pr::CampaignError::none,
                "a lost ford still advances the campaign"
            );
            expect(
                at.current().id == core::stable_content_id_v1("valley_falls"),
                "losing the ford reaches the losing terminal"
            );
        }

        // The yard, outlasted the other way round.
        {
            pr::CampaignCursor at = walk_to("emberhall_battle");
            expect(
                at.current().id ==
                    core::stable_content_id_v1("emberhall_battle"),
                "the replay reaches the yard"
            );
            const std::vector<sim::ObjectiveResult> lost{
                failed("hold_the_yard")
            };
            expect(
                at.advance_after(sim::Outcome::second_side_won, lost) ==
                    pr::CampaignError::none,
                "a lost yard still advances the campaign"
            );
            expect(
                at.current().id == core::stable_content_id_v1("valley_falls"),
                "a company that did not last the night reaches the losing "
                "terminal"
            );
        }

        // A compound condition that does not hold must route elsewhere rather
        // than being treated as satisfied: the Warden felled, the Captain lost.
        {
            pr::CampaignCursor at = walk_to("ashen_watch_battle");
            expect(
                at.current().id ==
                    core::stable_content_id_v1("ashen_watch_battle"),
                "the replay reaches the watch"
            );
            const std::vector<sim::ObjectiveResult> commander_lost{
                satisfied("fell_the_warden"), failed("keep_mirea_alive")
            };
            expect(
                at.advance_after(
                    sim::Outcome::second_side_won, commander_lost
                ) == pr::CampaignError::none,
                "a lost commander still advances the campaign"
            );
            expect(
                at.current().id == core::stable_content_id_v1("valley_falls"),
                "felling the Warden but losing Mirea reaches the losing "
                "terminal"
            );
            expect(
                !at.current().dialogue_ids.empty(),
                "the losing terminal carries its own beat"
            );
        }

        // And the gate, where the same compound shape decides the campaign.
        {
            pr::CampaignCursor at = walk_to("coldgate_battle");
            expect(
                at.current().id ==
                    core::stable_content_id_v1("coldgate_battle"),
                "the replay reaches the gate"
            );
            const std::vector<sim::ObjectiveResult> captain_lost{
                satisfied("fell_the_marshal"),
                failed("keep_mirea_alive")
            };
            expect(
                at.advance_after(
                    sim::Outcome::second_side_won, captain_lost
                ) == pr::CampaignError::none,
                "a lost captain still advances the campaign"
            );
            expect(
                at.current().id == core::stable_content_id_v1("valley_falls"),
                "felling the Marshal but losing Mirea reaches the losing "
                "terminal"
            );
        }
    }

    if (failures != 0) return 1;
    std::cout << "tarnholt ford_hash=" << ford_created.encounter.canonical_hash()
              << " watch_hash=" << watch_created.encounter.canonical_hash()
              << " campaign=valley_held\n";
    return 0;
}
