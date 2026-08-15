// SPDX-License-Identifier: MIT
// What the player sees while the side nobody is steering plays its turn.
//
// A front end animates an event out of the state it happened in, and then
// repaints the board the engine now holds. Both halves are the session's to
// order, and it orders them the same way for every accepted command in
// `platform/client/src/session.cpp`: report, then draw. The opposing side's
// commands are commands, and this pins that they are drawn like every other.
//
// The failure it exists to catch is exactly what a cartridge shows without the
// draw: a token slides to its new tile, the next frame is painted from the
// board the client last drew, and the token snaps back to where it started.
// Every enemy stays visibly where it was until the player's own next command
// paints the current state. That is a whole turn of the game happening
// somewhere the player cannot see it.
//
// So the recorder below keeps the *order* of the calls and the board each draw
// was handed, and asks three things of them: that a draw follows every report,
// that the last draw before control comes back is the board control comes back
// to, and that the killing blow's own board is painted rather than skipped.

#include <grandleon/client/presenter.hpp>
#include <grandleon/client/session.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace client = grandleon::client;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Every call the session made, in order, with what the board looked like when
// it made it. A presenter that draws nothing still records everything, which is
// what lets this ask about the sequence rather than about pixels.
struct Landing final {
    sim::UnitId unit{};
    sim::Position tile{};
};

struct Frame final {
    bool drawn{false};
    // Where every unit stood, in the snapshot handed to a draw. Empty on a
    // report, which is handed no snapshot at all.
    std::vector<Landing> places;
    sim::Side active{sim::Side::first};
    sim::Outcome outcome{sim::Outcome::ongoing};
    // Where a report said somebody landed: one entry per `unit_moved` event.
    // This is what the draw after it has to be showing, and it is the whole of
    // the defect: a walk animated out of one board and then repainted from
    // another puts the token back where it started.
    std::vector<Landing> landings;
};

std::vector<Landing> places_in(const sim::EncounterSnapshot& snapshot) {
    std::vector<Landing> places;
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        places.push_back({unit.id, unit.position});
    }
    return places;
}

bool stands_at(const std::vector<Landing>& places, const Landing& wanted) {
    for (const Landing& held : places) {
        if (held.unit == wanted.unit) return held.tile == wanted.tile;
    }
    return false;
}

class Recorder final : public client::Presenter {
public:
    std::vector<Frame> frames;

    void present_dialogue(const grandleon::package_runtime::Dialogue&) override {
    }

    void battle_begins(
        const sim::EncounterSnapshot&,
        const client::Roster&,
        sim::Side,
        const std::vector<std::uint64_t>&
    ) override {}

    void draw(
        const sim::EncounterSnapshot& snapshot, const client::Roster&
    ) override {
        frames.push_back(
            {true, places_in(snapshot), snapshot.active_side, snapshot.outcome}
        );
    }

    void report(
        const sim::CommandResult& result, const client::Roster&
    ) override {
        // A report carries no snapshot, deliberately: it describes events out
        // of the state they happened in. What it does carry is where the walk
        // it is about ended, which is exactly what the frame after it has to be
        // showing.
        Frame frame;
        frame.drawn = false;
        for (const sim::Event& event : result.events) {
            if (event.type != sim::EventType::unit_moved) continue;
            frame.landings.push_back({event.unit_id, event.position});
        }
        frames.push_back(frame);
    }

    void refused(sim::CommandError) override {}

    void show_state(
        const sim::EncounterSnapshot&,
        std::uint64_t,
        const std::vector<sim::ObjectiveDefinition>&
    ) override {}

    void battle_ended(const sim::EncounterSnapshot&, std::uint64_t) override {}

    void campaign_ended() override {}

    // The player never touches anything: this battle is about the other side.
    // Waiting hands the turn straight back, which is the shortest possible way
    // to reach the opposing side's block again.
    client::Intent next_intent(
        const sim::EncounterSnapshot& snapshot, const client::Roster&
    ) override {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.side != snapshot.active_side) continue;
            if (!sim::on_board(unit) || unit.has_acted) continue;
            client::Intent intent;
            intent.kind = client::IntentKind::wait;
            intent.unit_id = unit.id;
            return intent;
        }
        return {client::IntentKind::quit};
    }
};

// A board the opposing side must walk across before it can strike: two hunters
// three tiles east of one lightly-armoured target, both authored to close.
// Walking is what the defect is visible in. A strike leaves the striker where
// it stood, so a board whose enemies only ever swing would hide it.
sim::EncounterDefinition chase_definition() {
    sim::EncounterDefinition definition;
    definition.width = 8;
    definition.height = 3;
    definition.turn_order = sim::TurnOrder::side_blocks;
    definition.weapons = {{0x9001, 6, 1, 1, 100}};

    sim::UnitDefinition quarry;
    quarry.id = 1;
    quarry.unit_type_id = 100;
    quarry.side = sim::Side::first;
    quarry.position = {0, 1};
    quarry.health = 12;
    quarry.strength = 1;
    quarry.movement = 1;
    quarry.action_points = 1;
    quarry.weapon_ids = {0x9001};
    definition.units = {quarry};

    for (int index = 0; index < 2; ++index) {
        sim::UnitDefinition hunter;
        hunter.id = static_cast<sim::UnitId>(10 + index);
        hunter.unit_type_id = 200;
        hunter.side = sim::Side::second;
        hunter.position = {4, static_cast<std::int16_t>(index)};
        hunter.health = 30;
        hunter.strength = 6;
        hunter.movement = 2;
        hunter.action_points = 2;
        hunter.weapon_ids = {0x9001};
        definition.units.push_back(hunter);
    }
    return definition;
}

pr::EncounterLoadResult chase_board() {
    pr::EncounterLoadResult loaded;
    loaded.definition = chase_definition();
    // Both hunters come at the quarry. The behaviour is the package's to carry
    // and the policy's to read; naming it here is the same binding the loader
    // would have handed over.
    for (int index = 0; index < 2; ++index) {
        pr::UnitBehaviorBinding binding;
        binding.unit_id = static_cast<sim::UnitId>(10 + index);
        binding.behavior = grandleon::tactics::Behavior::pursue;
        loaded.behaviors.push_back(binding);
    }
    return loaded;
}

void every_opposing_command_is_drawn() {
    Recorder recorder;
    client::BattleReport report;
    const client::SessionError status = client::play_battle(
        chase_board(), sim::Side::first, recorder, report
    );
    expect(status == client::SessionError::none, "the battle ran");
    expect(
        report.outcome != sim::Outcome::ongoing,
        "and it ran to a finish rather than stopping half way"
    );

    int reports = 0;
    int undrawn = 0;
    for (std::size_t index = 0; index < recorder.frames.size(); ++index) {
        if (recorder.frames[index].drawn) continue;
        ++reports;
        const bool drawn_after = index + 1 < recorder.frames.size() &&
                                 recorder.frames[index + 1].drawn;
        if (!drawn_after) ++undrawn;
    }
    expect(reports > 0, "the battle actually reported something");
    expect(
        undrawn == 0,
        "every accepted command is followed by the board it produced, " +
            std::to_string(undrawn) + " were not"
    );
}

// The half the defect is actually about: a walk the opposing side takes must be
// painted where it landed, before control comes back.
//
// Without the draw the frame after a walk is the next character's report, and
// by the time anything is painted the intermediate boards are gone. So this
// asks the sharpest form of the question: for every walk the engine reported,
// was the very next thing the front end was handed a board with that character
// standing on the tile the walk ended on.
void every_walk_lands_on_screen_where_it_landed() {
    Recorder recorder;
    client::BattleReport report;
    (void)client::play_battle(chase_board(), sim::Side::first, recorder, report);

    int walks = 0;
    int unpainted = 0;
    for (std::size_t index = 0; index < recorder.frames.size(); ++index) {
        const Frame& frame = recorder.frames[index];
        if (frame.drawn || frame.landings.empty()) continue;
        ++walks;
        const bool painted = index + 1 < recorder.frames.size() &&
                             recorder.frames[index + 1].drawn;
        if (!painted) {
            ++unpainted;
            continue;
        }
        for (const Landing& landing : frame.landings) {
            if (!stands_at(recorder.frames[index + 1].places, landing)) {
                ++unpainted;
            }
        }
    }
    expect(walks > 0, "the opposing side actually walked somewhere");
    expect(
        unpainted == 0,
        "every walk is on screen where it landed, " +
            std::to_string(unpainted) + " were not"
    );
}

// The last thing an opposing side does is the easiest one for a front end to
// miss: the session breaks out of its loop on the finished outcome, so a draw
// that came after the loop would come after the break and the blow that ended
// the battle would be reported and never painted.
void the_last_blow_is_painted() {
    Recorder recorder;
    client::BattleReport report;
    (void)client::play_battle(chase_board(), sim::Side::first, recorder, report);
    expect(
        report.outcome == sim::Outcome::second_side_won,
        "the hunters win, which is what makes the last blow theirs"
    );
    bool finished_frame = false;
    for (const Frame& frame : recorder.frames) {
        if (frame.drawn && frame.outcome != sim::Outcome::ongoing) {
            finished_frame = true;
        }
    }
    expect(
        finished_frame,
        "the board the battle ended on was drawn at least once"
    );
}

}  // namespace

int main() {
    every_opposing_command_is_drawn();
    every_walk_lands_on_screen_where_it_landed();
    the_last_blow_is_painted();
    if (failures == 0) {
        std::cout << "opposing side: every command it takes is on screen\n";
    }
    return failures == 0 ? 0 : 1;
}
