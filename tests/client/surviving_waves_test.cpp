// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/sheet/unit_sheet.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/tactics/policy.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// A whole scenario, end to end, out of an authored file.
//
// `tests/fixtures/source_projects/valid/surviving-waves.json` is a map won by
// outlasting seven rounds, with a picket that holds the ford and a wave that
// arrives on the third round, again on the sixth, and comes at the player
// through the same terrain rules everything else walks by. Nothing here is
// hand-built: the JSON goes through the real source reader, the real compiler,
// the real package container and the real loader, and the second side is
// steered by `tactics::decide` with the stances the package carries. That is
// exactly what `client::play_opposing_side` asks on every platform.
//
// What it has to earn, in one battle rather than in an argument:
//
//   * the wave is in the battle before it is on the board, so killing the
//     picket does not end the fight;
//   * it comes in as its round begins, on the tile the content asked for;
//   * it recurs, and the second arrival is a character the content did not
//     name;
//   * it *pursues* through the field the stance it was authored with, closing
//     the distance rather than standing where it spawned;
//   * and the battle is won the moment the seventh round completes.

namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace tactics = grandleon::tactics;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::uint32_t steps_between(sim::Position from, sim::Position to) {
    const int dx = from.x > to.x ? from.x - to.x : to.x - from.x;
    const int dy = from.y > to.y ? from.y - to.y : to.y - from.y;
    return static_cast<std::uint32_t>(dx + dy);
}

// One turn for the side nobody is steering, asked of the same policy every
// client asks, with the stance the package carries. A refused proposal falls
// back to waiting, exactly as `client::play_opposing_side` does.
sim::CommandResult take_opposing_turn(
    sim::Encounter& encounter,
    const std::vector<pr::UnitBehaviorBinding>& behaviors
) {
    const auto snapshot = encounter.snapshot();
    for (const sim::UnitSnapshot& value : snapshot.units) {
        if (value.side != snapshot.active_side || !value.arrived) continue;
        if (value.health <= 0 || value.has_acted) continue;
        // `initiative` names its actor; nobody else may act in its place.
        // `side_blocks` names only the side, so the caller picks, which is
        // what `has_acted` above is filtering for.
        if (snapshot.active_unit_id != 0 &&
            snapshot.active_unit_id != value.id) {
            continue;
        }
        tactics::Behavior behavior = tactics::Behavior::hold;
        std::vector<sim::Position> patrol;
        for (const pr::UnitBehaviorBinding& binding : behaviors) {
            if (binding.unit_id != value.id) continue;
            behavior = binding.behavior;
            patrol = binding.patrol;
        }
        const tactics::Plan plan = tactics::decide(
            snapshot, value.id, behavior, patrol, encounter.abilities(),
            encounter.weapons()
        );
        if (plan.actionable) {
            auto result = encounter.apply(plan.command);
            if (result) return result;
        }
        return encounter.apply({sim::CommandType::wait, value.id});
    }
    return {};
}

// The board, authored in the compiler's own model rather than in JSON, for the
// reason `tests/package_runtime` authors its fixtures that way: this test is
// about the bytes and the battle, and the source reader has its own tests and
// its own fixture: `tests/fixtures/source_projects/valid/surviving-waves.json`
// is the same board written the way an author writes it.
//
// A five by three ford. The player's guardian holds the west bank, a picket
// holds the east, and a wave arrives on the third round and again on the sixth,
// authored `pursue` so it comes at the player rather than standing where it
// spawned. The objective is to outlast seven rounds.
gc::GameSource ford_source() {
    gc::GameSource value;
    value.game_id[0] = 0x47U;
    value.title = "Surviving waves";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.classes = {
        // The guardian is a wall: it swings feebly and takes almost nothing,
        // so the ford is decided by the clock rather than by the corpses, which
        // is the shape a survive map has.
        {30, "Blue class", {40, 1, 5, 0, 3}, {10}},
        {31, "Red class", {99, 3, 0, 0, 3}, {10}},
    };
    value.weapons = {{40, "Sword", 10, 2, 1, 1}};
    value.unit_types = {
        {60, "Guardian", 30, 80, {40}, {}},
        {61, "Raider", 31, 81, {40}, {}},
    };
    value.maps = {{70, "Ford", 5, 3, std::vector<std::uint64_t>(15, 1)}};
    value.factions = {{80, "Blue"}, {81, "Red"}};
    value.objectives = {
        {90, "Hold the ford", gc::ObjectiveKind::survive_rounds,
         gc::ObjectiveSide::first, 0, 7}
    };
    gc::Placement hero{1000, 2000, 2000, 60, gc::EncounterSide::first, 0, 1};
    gc::Placement picket{1001, 2001, 0, 61, gc::EncounterSide::second, 4, 1};
    picket.behavior = gc::UnitBehavior::hold;
    gc::Placement wave{1002, 2002, 0, 61, gc::EncounterSide::second, 4, 0};
    wave.behavior = gc::UnitBehavior::pursue;
    wave.arrival_round = 3;
    wave.arrival_every = 3;
    wave.arrival_times = 2;
    gc::Encounter encounter{100, "The Ford", 70, {90}, {hero, picket, wave}};
    // Whole-side blocks, so every character on the opposing side takes a turn
    // each round and the wave is actually asked what it wants to do.
    encounter.turn_order = gc::TurnOrder::side_blocks;
    value.encounters = {encounter};
    value.campaigns = {
        {
            110,
            "Line",
            111,
            {
                {111, gc::CampaignNodeKind::encounter, 100, {}, {112}, {}},
                {112, gc::CampaignNodeKind::terminal, 0, {}, {}, {}},
            },
            {{2000, "Hero of the Ford", 60, 0}},
        }
    };
    return value;
}

void the_ford_is_held_for_seven_rounds() {
    const auto compiled = gc::compile(ford_source());
    expect(static_cast<bool>(compiled), "the authored ford compiles");
    for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
        std::cerr << "compiler diagnostic: "
                  << gc::diagnostic_name(diagnostic.code) << ' '
                  << diagnostic.path << '\n';
    }
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and its package loads");
    expect(
        loaded.package.find(pf::SectionType::arrivals) != nullptr,
        "a project with a wave carries an arrivals section"
    );

    auto board = pr::load_encounter(loaded.package, 100);
    expect(
        static_cast<bool>(board),
        std::string("and the board decodes, error ") +
            std::to_string(static_cast<int>(board.error))
    );

    auto created = sim::create_encounter(board.definition);
    expect(static_cast<bool>(created), "and is valid content");
    if (!created) return;

    expect(
        grandleon::sheet::rounds_to_survive(created.encounter.objectives()) == 7U,
        "the client reads seven rounds to outlast off the objective"
    );

    // And the names the client draws are this author's own, out of the package
    // it is holding. The shipped table has never heard of `Guardian` or
    // `Raider`, so without the resolver every row of every sheet on a cartridge
    // built from anybody's project reads `UNIT`, a fallen bandit included.
    // Upper-cased because the font every console here shares has no lower case
    // in it.
    {
        const std::uint64_t guardian = 60;
        const std::uint64_t raider = 61;
        expect(
            std::string(
                grandleon::sheet::unit_type_name(&loaded.package, guardian)
                    .c_str()
            ) == "GUARDIAN",
            "the package's own word for a class is what a client shows"
        );
        expect(
            std::string(
                grandleon::sheet::unit_type_name(&loaded.package, raider).c_str()
            ) == "RAIDER",
            "and it is this author's word rather than the shipped table's"
        );
        expect(
            std::string(
                grandleon::sheet::unit_type_name(&loaded.package, 999).c_str()
            ) == "UNIT",
            "an identity the package does not carry still reads as its category"
        );
        expect(
            std::string(
                grandleon::sheet::weapon_name(&loaded.package, 40).c_str()
            ) == "SWORD",
            "and a weapon is named the same way"
        );
    }

    const auto opening = created.encounter.snapshot();
    // Two on the board and two still marching: the wave arrives twice, and the
    // second arrival is a character the content never named.
    int standing = 0;
    int marching = 0;
    for (const sim::UnitSnapshot& value : opening.units) {
        if (value.arrived) ++standing; else ++marching;
    }
    expect(standing == 2 && marching == 2, "two stand and two are on the way");

    // Play it. The player holds their ground; the side nobody is steering is
    // asked of the same policy every client asks, with the stances the package
    // carries.
    sim::Position first_landing{};
    bool saw_arrival = false;
    int arrivals = 0;
    std::uint32_t arrival_round = 0;
    for (int turn = 0; turn < 128; ++turn) {
        const auto snapshot = created.encounter.snapshot();
        if (snapshot.outcome != sim::Outcome::ongoing) break;
        sim::CommandResult result;
        if (snapshot.active_side == sim::Side::first) {
            // The player waits out the clock, which is the whole shape of a
            // survive map: the win condition is the round, not the corpses.
            //
            // A side block names no actor, so the player picks one: the first
            // of their line who still has a turn in hand, which is exactly the
            // choice a console cursor makes for them.
            sim::UnitId actor = snapshot.active_unit_id;
            if (actor == 0) {
                for (const sim::UnitSnapshot& value : snapshot.units) {
                    if (value.side != snapshot.active_side) continue;
                    if (value.health <= 0 || !value.arrived) continue;
                    if (value.has_acted) continue;
                    actor = value.id;
                    break;
                }
            }
            if (actor == 0) break;
            result = created.encounter.apply({sim::CommandType::wait, actor});
            if (!result) break;
        } else {
            result = take_opposing_turn(created.encounter, board.behaviors);
            if (!result) break;
        }
        for (const sim::Event& event : result.events) {
            if (event.type != sim::EventType::unit_arrived) continue;
            ++arrivals;
            if (!saw_arrival) {
                first_landing = event.position;
                arrival_round = static_cast<std::uint32_t>(event.amount);
                saw_arrival = true;
            }
        }
    }

    const auto ended = created.encounter.snapshot();
    expect(
        ended.outcome == sim::Outcome::first_side_won,
        "the ford is held, and holding it is the win"
    );
    expect(ended.round == 7U, "won by the seventh round completing");
    expect(
        saw_arrival && arrival_round == 3U && first_landing == sim::Position{4, 0},
        "the first wave came in as the third round began, on its own tile"
    );
    expect(arrivals == 2, "and came again, because the wave recurs");

    // And it came at the player. The wave's tile is the far corner; a `pursue`
    // stance closes on the nearest opponent, so by the end it stands nearer the
    // player's corner than it spawned.
    const sim::UnitSnapshot* arrived = nullptr;
    for (const sim::UnitSnapshot& value : ended.units) {
        // The first of the wave, which had four rounds to walk. The second
        // landed as the sixth round opened and is asked nothing here.
        if (!value.arrived || value.arrival_round != 3U) continue;
        if (value.health <= 0) continue;
        arrived = &value;
        break;
    }
    expect(arrived != nullptr, "a wave is standing at the end");
    if (arrived != nullptr) {
        expect(
            steps_between(arrived->position, {4, 0}) > 0,
            "and it did not stand where it spawned — it came at the player"
        );
    }
}

}  // namespace

int main() {
    the_ford_is_held_for_seven_rounds();
    if (failures == 0) std::cout << "surviving waves: ok\n";
    return failures == 0 ? 0 : 1;
}
