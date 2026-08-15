// SPDX-License-Identifier: MIT
// Who the console thinks is standing on the board, against who the engine says
// is.
//
// `simulation::on_board` folds three facts: alive, arrived, and not talked
// away. It is the engine's answer to every question of the form "is this
// character there": who holds a tile, who may be chosen to act, who anything
// may be aimed at. Every rule inside the engine reads it. A client that spells
// the test itself instead gets a *different* answer on exactly the two boards
// where the three facts come apart, and the symptom is silent: a character
// drawn, hovered and counted as occupying a tile while the engine refuses every
// command aimed at them.
//
// The thing under test is the console's own client. This binary compiles
// `platform/client/src/turn_client.cpp`, the translation unit both consoles
// compose their board from, and reads the transcript it emits, which is the
// same transcript the Nintendo 64 and PlayStation checks are compared on. So
// what is proved here is proved about the shipped composer rather than about a
// model of it.
//
// The board is `tests/fixtures/source_projects/valid/board-predicate.json`,
// compiled at test time through the real source reader, the real compiler and
// the real loader. It is a fixture and not a golden: nothing compares its bytes
// to anything, and regenerating it is not a way to make a failing test pass. It
// authors the two states the shipped campaigns keep on separate boards:
//
//   * `outrider` is a second-side character that arrives on the third round, so
//     at the opening bell it is in the battle and not on it. Its `x`/`y` are the
//     tile the content asked for rather than a tile it holds. It stands on the
//     *second* side because a first-side placement is a fielded roster member
//     and a fielded member may not arrive: the only side that can open a battle
//     with one of its own still off the board is the side that fields nobody.
//   * `envoy` is a second-side character a talk can reach, standing next to
//     `captain`, who can therefore talk it off the board with its health
//     untouched.
//
// The three assertions below are the three ways a client that spelled the
// predicate `health > 0` would disagree with the engine. Spelling it that way
// in `turn_client.cpp` fails nine of them immediately. That is verified, not
// assumed.
//
// **`outrider`'s name is load-bearing.** Characters reach the snapshot in
// ascending identity order, an identity is the hash of the placement's own
// source key, and the cursor case needs the marching one to come first among
// its side. Otherwise the cursor opens on somebody standing anyway and the
// case passes without having asked anything. That premise is asserted rather
// than assumed, and says so out loud when it stops holding, so renaming the
// placement fails loudly instead of quietly proving nothing.

#include <grandleon/client/presenter.hpp>
#include <grandleon/client/turn_client.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace client = grandleon::client;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
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

// Everything the client said, kept whole. The console harnesses keep only the
// `CHECKPOINT` and `FACT` lines; this keeps all of them and asks about the
// facts, which is the same comparison over a superset.
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

    // How many lines open with `prefix`. Used to count the board's occupants
    // without depending on the order the loader put them in.
    [[nodiscard]] int count(std::string_view prefix) const {
        int total = 0;
        for (const std::string& held : lines_) {
            if (held.rfind(prefix, 0) == 0) ++total;
        }
        return total;
    }

    // Whether any line opening with `prefix` ends with `tail`. A `FACT unit`
    // row is `FACT unit <index> <x> <y> <health> <side>`, so the index is not
    // known here but the tile is. This asks about the tile.
    [[nodiscard]] bool any_containing(
        std::string_view prefix, std::string_view needle
    ) const {
        for (const std::string& held : lines_) {
            if (held.rfind(prefix, 0) != 0) continue;
            if (held.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    void dump() const {
        for (const std::string& held : lines_) std::cerr << "  " << held << '\n';
    }

private:
    std::vector<std::string> lines_;
};

// The client, with the two things only a console supplies stubbed. It draws
// nothing and reads no pad: every other line of the class still runs, which is
// what makes the transcript the console's own.
class HostClient final : public turn::TurnClient {
public:
    explicit HostClient(turn::ReportSink& sink) noexcept : TurnClient(sink) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay&) override {}

    std::uint16_t next_press() override { return turn::pad_end_of_script; }
};

// The authored board, compiled here rather than baked, so this test depends on
// no console build and on no checked-in package.
struct Board final {
    // The compiled bytes outlive the package that reads them: a `LoadedPackage`
    // is a view over this buffer rather than a copy of it, so letting the
    // compile result go out of scope would leave every record dangling.
    gc::CompileResult compiled;
    pf::LoadResult loaded;
    pr::EncounterLoadResult definition;
};

bool open_board(Board& board) {
    const std::string path =
        std::string(GRANDLEON_SOURCE_DIR) +
        "/tests/fixtures/source_projects/valid/board-predicate.json";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "FAIL: cannot open " << path << '\n';
        ++failures;
        return false;
    }
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    if (!parsed) {
        std::cerr << "FAIL: the fixture did not parse\n";
        ++failures;
        return false;
    }
    board.compiled = gc::compile(parsed.source);
    if (!board.compiled) {
        std::cerr << "FAIL: the fixture did not compile\n";
        ++failures;
        return false;
    }
    board.loaded = pf::load_mock_package(
        board.compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    if (board.loaded.error != pf::Error::none) {
        std::cerr << "FAIL: the compiled fixture did not open\n";
        ++failures;
        return false;
    }
    // The identity the compiler gives an authored encounter node: the campaign
    // key and the node key, joined. Asked of the same function the compiler
    // asked, so this names the board rather than guessing a number.
    board.definition = pr::load_encounter(
        board.loaded.package, core::stable_content_id_v1("main/battle")
    );
    if (!board.definition) {
        std::cerr << "FAIL: the fixture's board did not decode, error "
                  << static_cast<int>(board.definition.error) << '\n';
        ++failures;
        return false;
    }
    return true;
}

// Opens the board and settles the client on it once, exactly as
// `client::play_battle` does: rebuild the roster, announce the battle, draw.
// The transcript that comes back is the opening frame.
void settle_opening(
    const Board& board,
    sim::Side player_side,
    Transcript& sink,
    sim::Encounter& encounter,
    client::Roster& roster,
    HostClient& host
) {
    roster.rebuild(encounter.snapshot());
    host.set_viewport(20, 10);
    host.set_package(&board.loaded.package);
    host.battle_begins(
        encounter.snapshot(), roster, player_side, board.definition.terrain
    );
    host.battle_definitions(
        encounter.weapons(), encounter.abilities(), encounter.items(),
        encounter.objectives()
    );
    sink.clear();
    host.draw(encounter.snapshot(), roster);
}

// Whoever the loader put on this tile, wherever they are in its order and
// whatever the engine thinks of them. Deliberately unfiltered: this is the
// test's own way of naming a character, and filtering it by the predicate under
// test would be asking the question with the answer already in it.
sim::UnitId unit_placed_at(
    const sim::EncounterSnapshot& snapshot, int x, int y, sim::Side side
) {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.side == side && unit.position.x == x && unit.position.y == y) {
            return unit.id;
        }
    }
    return 0;
}

// A wave that has not marched in holds no tile, and the client must not report
// one for it.
//
// The opening bell of this board carries four characters and three occupants.
// A client spelling the board `health > 0` reports four, and the fourth row
// claims tile 0,1: ground a player can walk onto and the engine will let them,
// because nobody is standing there.
void an_unarrived_wave_occupies_nothing() {
    Board board;
    if (!open_board(board)) return;
    auto created = sim::create_encounter(board.definition.definition);
    expect(static_cast<bool>(created), "the authored board is valid content");
    if (!created) return;

    const sim::EncounterSnapshot opening = created.encounter.snapshot();
    expect(opening.units.size() == 4, "the board fields four characters");
    const sim::UnitId wave = unit_placed_at(opening, 0, 1, sim::Side::second);
    expect(wave != 0, "one of them is the wave, on the tile it was authored at");
    for (const sim::UnitSnapshot& unit : opening.units) {
        if (unit.id != wave) continue;
        expect(!unit.arrived, "and the engine says it has not arrived");
        expect(unit.health > 0, "though it is alive, which is the whole point");
        expect(!sim::on_board(unit), "so the engine does not put it on the board");
    }

    Transcript sink;
    HostClient host(sink);
    client::Roster roster;
    settle_opening(
        board, sim::Side::first, sink, created.encounter, roster, host
    );

    expect(
        sink.count("FACT unit ") == 3,
        "the opening frame reports three occupants, not four"
    );
    if (sink.count("FACT unit ") != 3) sink.dump();
    expect(
        !sink.any_containing("FACT unit ", " 0 1 "),
        "and none of them claims the tile the wave was authored on"
    );
}

// The opening cursor is put on somebody the player can actually see. On a board
// whose first character of the player's side is still marching, that is the
// next one, not the empty tile the wave is authored at.
//
// Played from the second side, because a first-side placement is a fielded
// roster member and a fielded member may not arrive: the only side that can
// open a battle with one of its own still off the board is the side that fields
// nobody.
void the_cursor_opens_on_somebody_who_is_there() {
    Board board;
    if (!open_board(board)) return;
    auto created = sim::create_encounter(board.definition.definition);
    expect(static_cast<bool>(created), "the authored board is valid content");
    if (!created) return;

    // The premise: the first character of the second side in the board's own
    // order is the one that has not arrived. That is what makes the cursor's
    // answer below a question about the predicate rather than about the order.
    const sim::EncounterSnapshot opening = created.encounter.snapshot();
    const sim::UnitSnapshot* first_of_side = nullptr;
    for (const sim::UnitSnapshot& unit : opening.units) {
        if (unit.side != sim::Side::second) continue;
        first_of_side = &unit;
        break;
    }
    expect(first_of_side != nullptr, "the second side fields somebody");
    if (first_of_side == nullptr) return;
    if (first_of_side->arrived) {
        std::cerr << "the board's order puts an arrived character first; "
                     "the fixture's placement order no longer holds\n";
    }
    expect(
        !first_of_side->arrived,
        "the wave is the first character of its side in the board's own order"
    );

    Transcript sink;
    HostClient host(sink);
    client::Roster roster;
    settle_opening(
        board, sim::Side::second, sink, created.encounter, roster, host
    );

    expect(
        !sink.has("FACT cursor 0 1"),
        "the cursor does not open on the tile a marching character is authored at"
    );
    expect(
        sink.has("FACT cursor 5 0"),
        "it opens on the first of that side who is actually standing there"
    );
    if (!sink.has("FACT cursor 5 0")) sink.dump();
}

// Somebody talked off the board holds no tile either, and this is the case a
// health test cannot catch at all: a departed character keeps every point of
// the health it had. It is drawn, it is hovered, it is counted, and the engine
// answers `target_departed` to every command aimed at it.
void a_talked_off_character_occupies_nothing() {
    Board board;
    if (!open_board(board)) return;
    auto created = sim::create_encounter(board.definition.definition);
    expect(static_cast<bool>(created), "the authored board is valid content");
    if (!created) return;

    const sim::EncounterSnapshot opening = created.encounter.snapshot();
    const sim::UnitId captain = unit_placed_at(opening, 4, 0, sim::Side::first);
    const sim::UnitId envoy = unit_placed_at(opening, 5, 0, sim::Side::second);
    expect(captain != 0 && envoy != 0, "the captain stands next to the envoy");
    if (captain == 0 || envoy == 0) return;

    // The client opens on the envoy's tile, because the envoy is the first
    // character of the side being played who is standing anywhere. That is what
    // makes the hover assertion below a question about `unit_at` rather than
    // about the cursor.
    Transcript sink;
    HostClient host(sink);
    client::Roster roster;
    settle_opening(
        board, sim::Side::second, sink, created.encounter, roster, host
    );
    expect(sink.has("FACT cursor 5 0"), "the cursor rests on the envoy");
    expect(
        sink.count("FACT hovered ") == 1,
        "and the client reports somebody under it"
    );
    expect(sink.count("FACT unit ") == 3, "three characters hold a tile");

    sim::Command talk;
    talk.type = sim::CommandType::talk;
    talk.unit_id = captain;
    talk.target_id = envoy;
    const sim::CommandResult result = created.encounter.apply(talk);
    expect(
        result.error == sim::CommandError::none,
        std::string("the captain talks the envoy off the board, error ") +
            std::to_string(static_cast<int>(result.error))
    );
    if (result.error != sim::CommandError::none) return;

    const sim::EncounterSnapshot after = created.encounter.snapshot();
    for (const sim::UnitSnapshot& unit : after.units) {
        if (unit.id != envoy) continue;
        expect(unit.departed, "the engine records the departure");
        expect(
            unit.health == 10,
            "and it took no health with it, which is why a health test misses it"
        );
        expect(!sim::on_board(unit), "so the engine takes it off the board");
    }

    sink.clear();
    host.draw(after, roster);

    expect(
        sink.count("FACT unit ") == 2,
        "the client now reports two occupants rather than three"
    );
    expect(
        !sink.any_containing("FACT unit ", " 5 0 "),
        "and nobody claims the tile the envoy walked away from"
    );
    expect(
        sink.count("FACT hovered ") == 0,
        "the cursor still rests on 5,0 and finds nobody standing there"
    );
    if (sink.count("FACT hovered ") != 0) sink.dump();
}

}  // namespace

int main() {
    an_unarrived_wave_occupies_nothing();
    the_cursor_opens_on_somebody_who_is_there();
    a_talked_off_character_occupies_nothing();
    if (failures == 0) {
        std::cout << "board predicate: the client agrees with the engine\n";
    }
    return failures == 0 ? 0 : 1;
}
