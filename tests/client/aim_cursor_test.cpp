// SPDX-License-Identifier: MIT
// Where an aiming cursor may rest, and where a press moves it.
//
// A player who takes ATTACK out of a menu is answering "which of these", and
// the engine has already said which: `aimable_tiles` lights the tile of every
// character the strike can reach and nothing else. So the cursor is bound to
// that answer: a d-pad that walked the whole board would make the player sweep
// it across open ground to find the one square the engine would accept.
//
// The rule is `client::nearest_aim_tile` and `client::next_aim_tile`, and it is
// two free functions rather than one per console for the reason every shared
// answer in `platform/client` is: the same rule written twice is two rules that
// have not disagreed *yet*. They are pure arithmetic over a tile list, so this
// test pins them off any hardware, then drives the shared client itself over a
// real board, so what is proved is proved about the composer both consoles
// link rather than about a model of it.
//
// `client::gesture_names_a_character` is the third answer, and it is the line
// the engine itself draws. A strike and a talk light the tiles somebody is
// standing on; a walk and a cast light ground. So the first two are a list to
// choose from and the last two are a place to point at, and only the first two
// take the cursor away from the player.

#include <grandleon/client/presenter.hpp>
#include <grandleon/client/turn_client.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
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

std::string said(sim::Position tile) {
    return std::to_string(tile.x) + "," + std::to_string(tile.y);
}

// ---------------------------------------------------------------------------
// The rule, on its own
// ---------------------------------------------------------------------------

void an_aim_opens_on_the_nearest_lit_tile() {
    const std::vector<sim::Position> tiles{{2, 0}, {5, 4}, {6, 1}};
    expect(
        client::nearest_aim_tile(tiles, {6, 0}) == sim::Position{6, 1},
        "the aim opens on the nearest lit tile, got " +
            said(client::nearest_aim_tile(tiles, {6, 0}))
    );
    // Two equally near, and the engine's row-major order breaks the tie
    // without this function restating it.
    const std::vector<sim::Position> level{{1, 0}, {3, 0}};
    expect(
        client::nearest_aim_tile(level, {2, 0}) == sim::Position{1, 0},
        "and the first of two equally near, in the order the engine listed them"
    );
    expect(
        client::nearest_aim_tile({}, {4, 4}) == sim::Position{4, 4},
        "a gesture that lights nothing leaves the cursor where it was"
    );
}

void a_press_moves_to_the_next_lit_tile() {
    // Three targets on one row, and a cursor on the first of them.
    const std::vector<sim::Position> row{{1, 2}, {4, 2}, {6, 2}};
    expect(
        client::next_aim_tile(row, {1, 2}, 1, 0) == sim::Position{4, 2},
        "right steps to the next lit tile along, not to the next square"
    );
    expect(
        client::next_aim_tile(row, {4, 2}, 1, 0) == sim::Position{6, 2},
        "and again"
    );
    expect(
        client::next_aim_tile(row, {6, 2}, 1, 0) == sim::Position{1, 2},
        "and past the last one it comes round to the first, which is the cycle"
    );
    expect(
        client::next_aim_tile(row, {1, 2}, -1, 0) == sim::Position{6, 2},
        "and left off the front comes round to the back"
    );
    // Off the axis: the nearer of two the same distance along.
    const std::vector<sim::Position> spread{{4, 0}, {4, 3}};
    expect(
        client::next_aim_tile(spread, {1, 3}, 1, 0) == sim::Position{4, 3},
        "two the same distance along are separated by which is nearer across"
    );
    // Nothing on this axis at all, so the press does nothing and the other
    // axis is what the player reaches for.
    expect(
        client::next_aim_tile(row, {4, 2}, 0, 1) == sim::Position{4, 2},
        "a press across a row every target shares moves nothing"
    );
    expect(
        client::next_aim_tile({}, {4, 2}, 1, 0) == sim::Position{4, 2},
        "and a gesture that lights nothing moves nothing at all"
    );
    // A diagonal is not a press this client has, and is not guessed at.
    expect(
        client::next_aim_tile(row, {1, 2}, 1, 1) == sim::Position{1, 2},
        "two axes at once move nothing rather than being guessed at"
    );
}

void only_a_gesture_that_names_somebody_takes_the_cursor() {
    expect(
        client::gesture_names_a_character(sim::Gesture::strike),
        "a strike names a character"
    );
    expect(
        client::gesture_names_a_character(sim::Gesture::talk),
        "and so does a talk"
    );
    expect(
        !client::gesture_names_a_character(sim::Gesture::walk),
        "a walk names a place"
    );
    expect(
        !client::gesture_names_a_character(sim::Gesture::cast),
        "and a cast names a place too, occupied or not"
    );
}

// ---------------------------------------------------------------------------
// The rule, inside the client both consoles link
// ---------------------------------------------------------------------------

// Everything the client said, kept whole, exactly as the console harnesses read
// it.
class Transcript final : public turn::ReportSink {
public:
    void line(const char* text) override { lines_.emplace_back(text); }
    void clear() noexcept { lines_.clear(); }

    [[nodiscard]] bool has(std::string_view line) const {
        for (const std::string& held : lines_) {
            if (held == line) return true;
        }
        return false;
    }

    void dump() const {
        for (const std::string& held : lines_) std::cerr << "  " << held << '\n';
    }

private:
    std::vector<std::string> lines_;
};

// The client with a scripted thumb. Presses are handed over one at a time and
// the board is never painted, which is what makes every other line of the class
// the console's own.
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

// Two opponents inside one archer's band and a stretch of open ground between
// them. The archer stands at 1,1; the opponents at 4,1 and 5,2, three and five
// orthogonal steps away, both inside a bow that reaches five. Nothing else is
// within reach of anything, so the battle cannot resolve itself out from under
// the presses.
//
//     y=0  .  .  .  .  .  .  .
//     y=1  .  H  .  .  E  .  .      H the archer, side one
//     y=2  .  .  .  .  .  E  .      E   at 4,1 and 5,2
//     y=3  .  .  .  .  .  .  .
const sim::ContentId bow_id = 0x5b01;

sim::EncounterDefinition two_targets() {
    sim::EncounterDefinition definition;
    definition.width = 7;
    definition.height = 4;
    definition.turn_order = sim::TurnOrder::side_blocks;
    definition.weapons = {{bow_id, 2, 1, 5, 100}};

    sim::UnitDefinition archer;
    archer.id = 1;
    archer.unit_type_id = 100;
    archer.side = sim::Side::first;
    archer.position = {1, 1};
    archer.health = 20;
    archer.strength = 3;
    archer.movement = 2;
    archer.action_points = 2;
    archer.weapon_ids = {bow_id};
    definition.units = {archer};

    const sim::Position stands[2] = {{4, 1}, {5, 2}};
    for (int index = 0; index < 2; ++index) {
        sim::UnitDefinition opponent;
        opponent.id = static_cast<sim::UnitId>(10 + index);
        opponent.unit_type_id = 200;
        opponent.side = sim::Side::second;
        opponent.position = stands[index];
        opponent.health = 30;
        opponent.strength = 3;
        opponent.movement = 1;
        opponent.action_points = 1;
        opponent.weapon_ids = {bow_id};
        definition.units.push_back(opponent);
    }
    return definition;
}

// The itinerary: pick the archer up (which opens its menu), take ATTACK, and
// then push the stick right twice. Without the rule those two presses walk the
// cursor two squares over open ground; with it they choose between the two
// characters the bow can reach.
void the_cursor_only_rests_on_a_target() {
    auto created = sim::create_encounter(two_targets());
    expect(static_cast<bool>(created), "the board is valid content");
    if (!created) return;

    Transcript sink;
    // A on the archer opens its menu; the caret opens on WALK, so one press
    // down reaches ATTACK and A takes it. Then two presses right.
    ScriptedClient host(
        sink,
        {turn::pad_a, turn::pad_down, turn::pad_a, turn::pad_right,
         turn::pad_right}
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
    sink.clear();
    host.draw(created.encounter.snapshot(), roster);
    const client::Intent intent =
        host.next_intent(created.encounter.snapshot(), roster);

    // The cursor opens on the archer, at 1,1. Taking ATTACK must put it on a
    // character the bow reaches rather than leaving it on the archer's own
    // tile, which is not one of them.
    expect(
        sink.has("FACT cursor 4 1"),
        "taking the strike puts the cursor on the nearest character it reaches"
    );
    expect(
        !sink.has("FACT cursor 2 1") && !sink.has("FACT cursor 3 1"),
        "and the cursor never rests on the open ground between them"
    );
    expect(
        sink.has("FACT cursor 5 2"),
        "a press right steps to the other one rather than one square along"
    );
    // Two presses right over two targets: the second comes back round to the
    // first, which is the cycle a player uses to compare them. The script runs
    // out after it, so the client asks to leave and nothing was committed.
    expect(
        intent.kind == client::IntentKind::quit,
        "and no press committed anything before the script ran out"
    );
    if (failures != 0) sink.dump();
}

// The other half, and the one that must not change: a walk still points at
// ground, one square per press, because a destination is somewhere rather than
// someone.
void a_walk_still_moves_one_square_at_a_time() {
    auto created = sim::create_encounter(two_targets());
    if (!created) return;

    Transcript sink;
    // A opens the menu on WALK, A takes it, then one press right.
    ScriptedClient host(
        sink, {turn::pad_a, turn::pad_a, turn::pad_right}
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
    sink.clear();
    host.draw(created.encounter.snapshot(), roster);
    (void)host.next_intent(created.encounter.snapshot(), roster);

    expect(
        sink.has("FACT cursor 1 1"),
        "the walk leaves the cursor where the character is standing"
    );
    expect(
        sink.has("FACT cursor 2 1"),
        "and a press right moves it one square onto the ground beside it"
    );
    if (failures != 0) sink.dump();
}

}  // namespace

int main() {
    an_aim_opens_on_the_nearest_lit_tile();
    a_press_moves_to_the_next_lit_tile();
    only_a_gesture_that_names_somebody_takes_the_cursor();
    the_cursor_only_rests_on_a_target();
    a_walk_still_moves_one_square_at_a_time();
    if (failures == 0) {
        std::cout << "aim cursor: a strike chooses between targets\n";
    }
    return failures == 0 ? 0 : 1;
}
