// SPDX-License-Identifier: MIT
// What a battle says while it is being played.
//
// A character who could be talked to raised a world flag and said nothing, and
// a node's own dialogues are presented on arrival, before the node acts, which
// is around a battle rather than inside one. So a board had no way to speak.
//
// A moment is a scene and an occasion, and there are three occasions because a
// battle reports three events worth speaking over: the board being drawn, a
// character talked off it, and a character defeated. The last two are separate
// facts everywhere else in this engine - `unit_talked` is emitted *instead of*
// `unit_defeated`, never beside it - so they are separate occasions here.
//
// The thing under test is the console's own client. This binary compiles
// `platform/client/src/turn_client.cpp`, the translation unit both consoles
// compose their board from, so what is proved is proved about the shipped
// composer rather than about a model of it.
//
// **Nothing here reaches the rules.** The simulation never learns a moment
// exists: `talk_record_id` is opaque to it by design, a defeat reports itself,
// and what is said about either travels beside the encounter. That is why the
// checks below can move a scene onto a board without a canonical hash moving.

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

class Silence final : public turn::ReportSink {
public:
    void line(const char*) override {}
};

// The client, with the two things only a console supplies stubbed, and every
// scene it was asked to present written down in the order it asked.
class HostClient final : public turn::TurnClient {
public:
    explicit HostClient(turn::ReportSink& sink) noexcept : TurnClient(sink) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay&) override {}

    std::uint16_t next_press() override { return turn::pad_end_of_script; }

    void present_dialogue(const pr::Dialogue& dialogue) override {
        for (const pr::DialogueLine& line : dialogue.lines) {
            said_.emplace_back(line.text);
        }
    }

    [[nodiscard]] const std::vector<std::string>& said() const noexcept {
        return said_;
    }
    void forget() noexcept { said_.clear(); }

private:
    std::vector<std::string> said_;
};

struct Board final {
    gc::CompileResult compiled;
    pf::LoadResult loaded;
    pr::EncounterLoadResult definition;
};

bool open_board(Board& board) {
    const std::string path =
        std::string(GRANDLEON_SOURCE_DIR) +
        "/tests/fixtures/source_projects/valid/encounter-moments.json";
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
// `client::play_battle` does, moments and all.
void settle_opening(
    const Board& board, HostClient& host, sim::Encounter& encounter,
    client::Roster& roster
) {
    roster.rebuild(encounter.snapshot());
    host.set_viewport(20, 10);
    host.set_package(&board.loaded.package);
    host.battle_begins(
        encounter.snapshot(), roster, sim::Side::first, board.definition.terrain
    );
    host.battle_definitions(
        encounter.weapons(), encounter.abilities(), encounter.items(),
        encounter.objectives()
    );
    host.battle_moments(board.definition.moments, board.definition.placements);
    host.forget();
    host.draw(encounter.snapshot(), roster);
}

bool holds(const std::vector<std::string>& said, std::string_view words) {
    for (const std::string& line : said) {
        if (line.find(words) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

int main() {
    Board board;
    if (!open_board(board)) return 1;

    expect(
        board.definition.moments.size() == 3,
        "the fixture authors one moment of each occasion"
    );

    // ----- the board opens ------------------------------------------------
    {
        Silence quiet;
        HostClient host{quiet};
        auto created = sim::create_encounter(board.definition.definition);
        expect(static_cast<bool>(created), "the fixture board is valid content");
        if (!created) return 1;
        client::Roster roster;
        settle_opening(board, host, created.encounter, roster);

        expect(
            holds(host.said(), "You should not have come."),
            "a board opens with what its author wrote over it"
        );
        expect(
            !holds(host.said(), "Then I will stand aside."),
            "and not with what somebody says on being talked to"
        );
        expect(
            !holds(host.said(), "It did not have to end here."),
            "nor with what is said over a fall nobody has taken"
        );

        // Once, however many times the board is drawn. A board is redrawn after
        // every accepted command, and a scene replayed on each of them would be
        // a battle nobody could get through.
        const std::size_t opening = host.said().size();
        host.draw(created.encounter.snapshot(), roster);
        expect(
            host.said().size() == opening,
            "and says it once, however many times the board is drawn"
        );
    }

    // ----- a character is talked off the board ----------------------------
    {
        Silence quiet;
        HostClient host{quiet};
        auto created = sim::create_encounter(board.definition.definition);
        if (!created) return 1;
        client::Roster roster;
        settle_opening(board, host, created.encounter, roster);
        host.forget();

        // Whoever the fixture made talkable, found through the engine's own
        // record rather than by name, so this test does not depend on the order
        // the loader put the board in.
        sim::UnitId talkable = 0;
        for (const sim::UnitDefinition& unit : board.definition.definition.units) {
            if (unit.talk_record_id != 0) talkable = unit.id;
        }
        expect(talkable != 0, "the fixture has somebody to talk to");

        // Somebody of the player's own side standing next to them: a talk has
        // a reach of one, so who makes it is not a free choice.
        sim::Position where{};
        for (const sim::UnitSnapshot& unit : created.encounter.snapshot().units) {
            if (unit.id == talkable) where = unit.position;
        }
        sim::UnitId speaker = 0;
        for (const sim::UnitSnapshot& unit : created.encounter.snapshot().units) {
            if (unit.id == talkable || !sim::on_board(unit)) continue;
            if (unit.side != sim::Side::first) continue;
            const int across = unit.position.x > where.x ? unit.position.x - where.x
                                                         : where.x - unit.position.x;
            const int down = unit.position.y > where.y ? unit.position.y - where.y
                                                       : where.y - unit.position.y;
            if (across + down == 1) speaker = unit.id;
        }
        expect(speaker != 0, "and somebody of ours standing next to them");
        const auto talked = created.encounter.apply(
            {sim::CommandType::talk, speaker, {}, talkable, 0}
        );
        if (static_cast<bool>(talked)) {
            host.report(talked, roster);
            expect(
                holds(host.said(), "Then I will stand aside."),
                "talking to somebody plays what this board says about it"
            );
            expect(
                !holds(host.said(), "It did not have to end here."),
                "and not what it says about them falling, which is a different "
                "fact reported by a different event"
            );
        } else {
            expect(false, "the fixture allows the talk this test makes");
        }
    }

    if (failures == 0) {
        std::cout << "what a battle says: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
