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

// A cast says what it would do, which is the one gesture that used to say
// nothing.
//
// The board prices a strike under it: the chance, the damage, and whether the
// blow fells. A cast showed a splash and no numbers at all, because the engine
// had no forecast to ask -- so the gesture a player is least able to work out
// in their head was the gesture the board was quietest about.
//
// The client keeps its message in the overlay, so that is what these read: the
// line a person sees, taken off the same structure a renderer draws.
class WatchingClient final : public turn::TurnClient {
public:
    WatchingClient(turn::ReportSink& sink, std::vector<std::uint16_t> script)
        : TurnClient(sink), script_(std::move(script)) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay& overlay)
        override {
        said = overlay.message == nullptr ? std::string{} : overlay.message;
    }

    std::uint16_t next_press() override {
        if (at_ >= script_.size()) return turn::pad_end_of_script;
        return script_[at_++];
    }

    std::string said;

private:
    std::vector<std::uint16_t> script_;
    std::size_t at_{0};
};

// A mage at 1,1 who knows one damaging blast of a single tile, an opponent at
// 3,1 inside its band, and an ally at 1,2 who is also inside it.
//
//     y=0  .  .  .  .  .
//     y=1  .  M  .  E  .      M the mage, E the opponent
//     y=2  .  A  .  .  .      A an ally of the mage
const sim::ContentId blast_id = 0x6c01;

sim::EncounterDefinition a_board_with_a_cast() {
    sim::EncounterDefinition definition;
    definition.width = 5;
    definition.height = 3;
    definition.turn_order = sim::TurnOrder::side_blocks;

    sim::AbilityDefinition blast;
    blast.id = blast_id;
    blast.kind = sim::AbilityKind::damage;
    blast.damage_type = sim::DamageType::magical;
    blast.area = sim::AreaShape::single;
    blast.power = 5;
    blast.minimum_reach = 1;
    blast.maximum_reach = 3;
    blast.accuracy = 100;
    definition.abilities = {blast};

    sim::UnitDefinition mage;
    mage.id = 10;
    mage.unit_type_id = 1;
    mage.side = sim::Side::first;
    mage.position = {1, 1};
    mage.health = 12;
    mage.magic = 2;
    mage.action_points = 2;
    mage.movement = 1;
    mage.ability_ids = {blast_id};

    sim::UnitDefinition ally;
    ally.id = 11;
    ally.unit_type_id = 1;
    ally.side = sim::Side::first;
    ally.position = {1, 2};
    ally.health = 12;
    ally.action_points = 2;
    ally.movement = 1;

    sim::UnitDefinition foe;
    foe.id = 20;
    foe.unit_type_id = 2;
    foe.side = sim::Side::second;
    foe.position = {3, 1};
    foe.health = 12;
    foe.action_points = 2;
    foe.movement = 1;

    definition.units = {mage, ally, foe};
    return definition;
}

// Drives the mage into aiming its cast, then applies `after` and reports what
// the board says.
std::string what_the_board_says(std::vector<std::uint16_t> after) {
    auto created = sim::create_encounter(a_board_with_a_cast());
    expect(static_cast<bool>(created), "the casting board is valid content");
    if (!created) return {};
    Transcript sink;
    // A opens the menu on the mage. The caret opens on WALK; one press down is
    // ATTACK and a second is CAST, which A then takes.
    std::vector<std::uint16_t> script{
        turn::pad_a, turn::pad_down, turn::pad_down, turn::pad_a
    };
    script.insert(script.end(), after.begin(), after.end());
    WatchingClient host(sink, std::move(script));
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
    (void)host.next_intent(created.encounter.snapshot(), roster);
    return host.said;
}

void a_cast_prices_itself_under_the_board() {
    // Taking the row leaves the cursor on the caster, which is ground the cast
    // may not be aimed at: a cast names a place rather than a character, so the
    // cursor is handed back where it stood and the player walks it. The board
    // says so meanwhile, which is the refusal the engine would give.
    expect(
        what_the_board_says({}).find("OUT OF RANGE") != std::string::npos,
        "a cast still resting on its own caster says it cannot be aimed there"
    );

    // Two squares right is the opponent at 3,1. Magic 2 plus power 5 against no
    // resistance is seven, off twelve health.
    const std::string over_a_foe =
        what_the_board_says({turn::pad_right, turn::pad_right});
    expect(
        over_a_foe.find("CAST 7") != std::string::npos,
        "a cast aimed at an opponent says what it would take off them"
    );
    expect(
        over_a_foe.find("LEFT 5") != std::string::npos,
        "and what they would be left standing on"
    );
    if (over_a_foe.find("CAST 7") == std::string::npos) {
        std::cerr << "  said: " << over_a_foe << "\n";
    }
}

void a_cast_over_an_ally_says_it_costs_them_nothing() {
    // A damaging cast covers the caster's own side and takes nothing from it.
    // That is a rule a player cannot see from the splash alone, so the board
    // has to say it rather than leave the tile looking dangerous.
    //
    // One square down is the ally at 1,2, inside the cast's own band.
    const std::string over_an_ally = what_the_board_says({turn::pad_down});
    expect(
        over_an_ally.find("SAFE") != std::string::npos,
        "a cast over one of your own says standing there costs nothing"
    );
    if (over_an_ally.find("SAFE") == std::string::npos) {
        std::cerr << "  said: " << over_an_ally << "\n";
    }

    // And empty ground inside the band is aimable and costs nobody anything,
    // which is a different fact again from being spared.
    const std::string over_ground =
        what_the_board_says({turn::pad_right, turn::pad_down});
    expect(
        over_ground == "CAST",
        "a cast over empty ground says only that it may be aimed there"
    );
    if (over_ground != "CAST") {
        std::cerr << "  said: " << over_ground << "\n";
    }
}

// The triangle, on the bar a player reads before they commit.
//
// A weapon kind that beats another moves both numbers the bar already shows,
// and moves them silently: the same archer reads one blow against a staff and
// a smaller one against a blade, with nothing saying the difference is a rule
// rather than a sturdier target. These cases are about the one mark that says
// so, and about the thing it must not do -- appear over a pairing the table
// never touched.
namespace {

const sim::ContentId blade_kind = 0x7b01;
const sim::ContentId bow_kind = 0x7b02;

// Two characters at arm's length, each holding a weapon of the kind named.
// `blade_kind` beats `bow_kind` and nothing else does, so which hand holds
// which is the whole of what changes between the boards below.
sim::EncounterDefinition a_board_of_two_kinds(
    sim::ContentId ours, sim::ContentId theirs
) {
    sim::EncounterDefinition definition;
    definition.width = 5;
    definition.height = 3;
    definition.turn_order = sim::TurnOrder::side_blocks;
    definition.weapon_types = {{blade_kind, {bow_kind}, 1, 15}};
    definition.weapons = {
        {0x7c01, 3, 1, 1, 80, ours},
        {0x7c02, 3, 1, 1, 80, theirs},
    };

    sim::UnitDefinition mine;
    mine.id = 10;
    mine.unit_type_id = 1;
    mine.side = sim::Side::first;
    mine.position = {1, 1};
    mine.health = 12;
    mine.strength = 4;
    mine.action_points = 2;
    mine.movement = 1;
    mine.weapon_ids = {0x7c01};

    sim::UnitDefinition theirs_unit;
    theirs_unit.id = 20;
    theirs_unit.unit_type_id = 2;
    theirs_unit.side = sim::Side::second;
    theirs_unit.position = {2, 1};
    theirs_unit.health = 12;
    theirs_unit.strength = 4;
    theirs_unit.action_points = 2;
    theirs_unit.movement = 1;
    theirs_unit.weapon_ids = {0x7c02};

    definition.units = {mine, theirs_unit};
    return definition;
}

// Opens the menu and takes ATTACK, which lands the cursor on the only target,
// then reports the line under the board.
std::string what_a_strike_says(sim::ContentId ours, sim::ContentId theirs) {
    auto created = sim::create_encounter(a_board_of_two_kinds(ours, theirs));
    expect(static_cast<bool>(created), "the two-kind board is valid content");
    if (!created) return {};
    Transcript sink;
    // The caret opens on WALK, so one press down is ATTACK.
    WatchingClient host(
        sink, {turn::pad_a, turn::pad_down, turn::pad_a}
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
    (void)host.next_intent(created.encounter.snapshot(), roster);
    return host.said;
}

}  // namespace

void the_bar_says_which_way_the_weapons_lean() {
    // Holding the kind that beats theirs. The mark leads the line, ahead of the
    // chance, because it is the reason the chance is what it is.
    const std::string holding = what_a_strike_says(blade_kind, bow_kind);
    expect(
        holding.rfind("^ ", 0) == 0,
        "a strike made with the better weapon says so before its numbers"
    );
    if (holding.rfind("^ ", 0) != 0) {
        std::cerr << "  said: " << holding << "\n";
    }

    // Striking into it. The same table read from the other end, and the other
    // mark: a capital vee, because the console font this line is drawn in
    // carries no lowercase at all.
    const std::string into = what_a_strike_says(bow_kind, blade_kind);
    expect(
        into.rfind("V ", 0) == 0,
        "and a strike made into one says that before its numbers"
    );
    if (into.rfind("V ", 0) != 0) {
        std::cerr << "  said: " << into << "\n";
    }

    // And the numbers really did move, so the mark is reporting a rule that
    // fired rather than decorating a pairing the table left alone.
    expect(
        holding.find("HIT 8") != std::string::npos &&
            into.find("HIT 6") != std::string::npos,
        "and the two are worth the advantage apart"
    );

    // A board whose weapons share a kind is a board the table never touches,
    // and it reads exactly as it did before there was a table.
    const std::string even = what_a_strike_says(bow_kind, bow_kind);
    expect(
        even.rfind("^ ", 0) != 0 && even.rfind("V ", 0) != 0,
        "a pairing the table does not name carries no mark at all"
    );
    if (even.rfind("^ ", 0) == 0 || even.rfind("V ", 0) == 0) {
        std::cerr << "  said: " << even << "\n";
    }
}

int main() {
    an_aim_opens_on_the_nearest_lit_tile();
    the_bar_says_which_way_the_weapons_lean();
    a_press_moves_to_the_next_lit_tile();
    only_a_gesture_that_names_somebody_takes_the_cursor();
    the_cursor_only_rests_on_a_target();
    a_cast_prices_itself_under_the_board();
    a_cast_over_an_ally_says_it_costs_them_nothing();
    a_walk_still_moves_one_square_at_a_time();
    if (failures == 0) {
        std::cout << "aim cursor: a strike chooses between targets\n";
    }
    return failures == 0 ? 0 : 1;
}
