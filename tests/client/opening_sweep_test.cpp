// SPDX-License-Identifier: MIT
// The reveal a board too wide for its screen opens with.
//
// A player handed a board with edges cannot see how much board there is, and
// finding out by walking into it is a poor way to be told. So a board whose
// window is narrower than it opens at the column showing its right edge and
// travels once to the column play begins at, before anything is played.
//
// The thing under test is the console's own client. This binary compiles
// `platform/client/src/turn_client.cpp`, the translation unit both consoles
// compose their board from, so what is proved is proved about the shipped
// composer: where the sweep starts, that it travels one way, that it arrives
// exactly on the opening column, and that it happens once when a board opens and
// on no later frame.
//
// **The narrow viewport is the whole of the setup.** Every fixture board in this
// repository fits every console screen, and so does every shipped one, which is
// why nothing else in the gate has ever drawn a sweep. `set_viewport` is what a
// console's own screen decides, so asking for one narrower than the board is
// asking for exactly the condition the sweep exists for, and the arithmetic it
// then runs is the arithmetic a forty-column board would run.

#include <grandleon/client/presenter.hpp>
#include <grandleon/client/turn_client.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/view/motion.hpp>

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
namespace view = grandleon::view;

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

// The client, with the two things only a console supplies stubbed, plus a record
// of every sweep frame it was asked to draw. The column is read back through the
// same const `camera()` a console reads it through, so what is recorded is what
// a renderer would have drawn.
class HostClient final : public turn::TurnClient {
public:
    explicit HostClient(turn::ReportSink& sink) noexcept : TurnClient(sink) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay&) override {
        painted_at_.push_back(camera().x);
    }

    void sweep_frame(
        const sim::EncounterSnapshot&, const turn::Overlay&
    ) override {
        swept_at_.push_back(camera().x);
    }

    std::uint16_t next_press() override { return turn::pad_end_of_script; }

    [[nodiscard]] const std::vector<int>& swept_at() const noexcept {
        return swept_at_;
    }
    [[nodiscard]] const std::vector<int>& painted_at() const noexcept {
        return painted_at_;
    }
    void forget() noexcept {
        swept_at_.clear();
        painted_at_.clear();
    }

private:
    std::vector<int> swept_at_;
    std::vector<int> painted_at_;
};

struct Board final {
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
    board.definition = pr::load_encounter(
        board.loaded.package, core::stable_content_id_v1("main/battle")
    );
    if (!board.definition) {
        std::cerr << "FAIL: the fixture's board did not decode\n";
        ++failures;
        return false;
    }
    return true;
}

// Opens the board on a screen `cols` wide and settles the client on it once,
// exactly as `client::play_battle` does.
void settle_opening(
    const Board& board, HostClient& host, sim::Encounter& encounter,
    client::Roster& roster, int cols, int rows
) {
    roster.rebuild(encounter.snapshot());
    host.set_viewport(cols, rows);
    host.set_package(&board.loaded.package);
    host.forget();
    host.battle_begins(
        encounter.snapshot(), roster, sim::Side::first, board.definition.terrain
    );
    host.battle_definitions(
        encounter.weapons(), encounter.abilities(), encounter.items(),
        encounter.objectives()
    );
    host.draw(encounter.snapshot(), roster);
}

}  // namespace

int main() {
    Board board;
    if (!open_board(board)) return 1;

    // The board is six columns across. Its height is three, so no screen asked
    // for below scrolls downward and every sweep here is the sideways one.
    expect(
        board.definition.definition.width == 6,
        "the fixture board is six columns across, which the numbers below assume"
    );

    // ----- a board that fits is not swept ---------------------------------
    {
        Silence quiet;
        HostClient host{quiet};
        auto created = sim::create_encounter(board.definition.definition);
        expect(static_cast<bool>(created), "the fixture board is valid content");
        if (!created) return 1;
        sim::Encounter& encounter = created.encounter;
        client::Roster roster;
        settle_opening(board, host, encounter, roster, 20, 10);
        expect(
            host.swept_at().empty(),
            "a board smaller than the screen has no edges and is not swept"
        );
        expect(
            host.painted_at().size() == 1 && host.painted_at().front() == 0,
            "it is painted once, at the only column it has"
        );
    }

    // ----- the reveal itself ----------------------------------------------
    {
        Silence quiet;
        HostClient host{quiet};
        auto created = sim::create_encounter(board.definition.definition);
        expect(static_cast<bool>(created), "the fixture board is valid content");
        if (!created) return 1;
        sim::Encounter& encounter = created.encounter;
        client::Roster roster;
        // Three columns of a six-column board, so the camera has three to
        // travel and the opening column is the left edge.
        settle_opening(board, host, encounter, roster, 3, 3);

        const std::vector<int>& swept = host.swept_at();
        expect(!swept.empty(), "a board wider than the screen is swept");
        if (swept.empty()) {
            std::cerr << failures << " check(s) failed\n";
            return 1;
        }

        // Where play begins on this board is read off the client rather than
        // assumed: the cursor opens on the first of the player's characters
        // standing, and which tile that is belongs to the fixture.
        const int opens_at = host.painted_at().empty() ? -1
                                                       : host.painted_at().front();
        expect(
            static_cast<int>(swept.size()) == view::sweep_frames_total(3, opens_at),
            "for as many frames as the motion model says this board costs"
        );
        expect(swept.front() == 3, "opening on the column that shows the right edge");
        expect(
            host.painted_at().size() == 1,
            "and painting exactly once, which is the arrival"
        );

        // The left edge is reached whatever column play begins at. This is the
        // property the whole gesture exists for: a player is shown the width.
        int lowest = 3;
        bool stayed_on_the_board = true;
        for (const int at : swept) {
            if (at < lowest) lowest = at;
            if (at < 0 || at > 3) stayed_on_the_board = false;
        }
        expect(lowest == 0, "and reaching the left edge, however far in play begins");
        expect(stayed_on_the_board, "never past either edge of the board");

        // One turn at most: the column falls, then rises, and never falls again.
        // A sweep that wandered would pass every check above and read as a drift.
        bool rising = false;
        bool turned_once = true;
        for (std::size_t at = 1; at < swept.size(); ++at) {
            if (swept[at] > swept[at - 1]) rising = true;
            else if (swept[at] < swept[at - 1] && rising) turned_once = false;
        }
        expect(turned_once, "turning at the left edge at most once and never again");
    }

    // ----- once, when the board opens -------------------------------------
    {
        Silence quiet;
        HostClient host{quiet};
        auto created = sim::create_encounter(board.definition.definition);
        expect(static_cast<bool>(created), "the fixture board is valid content");
        if (!created) return 1;
        sim::Encounter& encounter = created.encounter;
        client::Roster roster;
        settle_opening(board, host, encounter, roster, 3, 3);
        const std::size_t opening = host.swept_at().size();
        expect(opening > 0, "the opening frame swept");

        // A second settled frame on the same board. Nothing about it is an
        // opening, so nothing about it is a reveal: a board redrawn after every
        // accepted command would otherwise sweep on every one of them.
        host.draw(encounter.snapshot(), roster);
        expect(
            host.swept_at().size() == opening,
            "a board redrawn on the same visit is not swept again"
        );
        expect(
            host.painted_at().size() == 2,
            "though it is painted again, which is what makes that a real redraw"
        );

        // And a fresh visit to the same board is an opening again, which is what
        // a Stage reopened by the picker or replayed after a loss is.
        auto reopened = sim::create_encounter(board.definition.definition);
        expect(static_cast<bool>(reopened), "and is still valid content reopened");
        if (!reopened) return 1;
        sim::Encounter& revisited = reopened.encounter;
        client::Roster again;
        settle_opening(board, host, revisited, again, 3, 3);
        expect(
            host.swept_at().size() == opening,
            "opening the board again sweeps it again, for the same frames"
        );
    }

    if (failures == 0) {
        std::cout << "the opening sweep: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
