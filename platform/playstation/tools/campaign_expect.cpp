// SPDX-License-Identifier: MIT
// What the rules say the PlayStation campaign executable's run should look
// like, derived on the host before that executable is built.
//
// It is `turn_expect.cpp`'s argument applied to a whole campaign rather than
// one battle. It compiles the same `platform/client/src/turn_client.cpp` the
// R3000A compiles, under the same `GRANDLEON_TURN_CLIENT_CAMPAIGN`, drives it
// through the same `client::run_persistent_campaign`, over the same project,
// with the same viewport, replaying the same
// `platform/client/autopilot/campaign_pad.h`, and writes out the transcript
// the console is then required to reproduce. The expectations are produced by a
// different compiler for a different architecture, ahead of the run that is
// checked against them, so neither side can quietly agree with itself.
//
// It draws nothing and reads no port. `paint` and `paint_screen` are empty,
// `next_press` reads a table, and the animation hooks are left at their
// do-nothing defaults.
//
// ---------------------------------------------------------------------------
// The two passes, and why the second one is derived by playing the first
//
// A campaign executable has two scripts: one for a card with nothing on it and
// one for a card holding a save. The second pass's transcript is a function of
// what the first pass *left* on the device, so it cannot be derived from the
// project alone. This tool plays the founding pass into an in-memory device
// first, then plays the resuming pass over the result and writes that
// transcript out. Nothing of the founding pass reaches the file: the mode
// argument says which transcript is wanted, and only that one is written.
//
// The device here is `MemorySlotStorage` and on the console it is a memory
// card. That difference is deliberate and is the whole point of the storage
// seam: the *bytes* a campaign writes are the device's business, and
// `grandleon_playstation_card_check` is what proves the card keeps them.

#include <grandleon/client/campaign_session.hpp>
#include <grandleon/client/turn_client.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/storage/memory_storage.hpp>
#include <grandleon/view/slot_menu.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "campaign_pad.h"

namespace client = grandleon::client;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;
namespace turn = grandleon::client::turn;
namespace view = grandleon::view;

namespace {

// A sink that keeps nothing. The founding pass is played only to leave a save
// behind for the resuming pass to find, so its transcript is not an
// expectation and must not reach the file.
class QuietSink final : public turn::ReportSink {
public:
    void line(const char*) override {}
};

// Only the lines a machine is compared on reach the file. Everything else the
// client says goes to the standard error for a person, for the reason
// `turn_expect.cpp` gives: a transcript that carried them would be comparing
// how fast a console is rather than what it decided.
class FileSink final : public turn::ReportSink {
public:
    explicit FileSink(std::ostream& out) noexcept : out_(out) {}

    void line(const char* text) override {
        const std::string value(text);
        if (value.rfind("CHECKPOINT ", 0) == 0 || value.rfind("FACT ", 0) == 0) {
            out_ << value << '\n';
        } else {
            std::cerr << "  " << value << '\n';
        }
    }

private:
    std::ostream& out_;
};

class HostClient final : public turn::TurnClient {
public:
    HostClient(
        turn::ReportSink& sink, const std::uint16_t* presses, std::size_t count
    ) noexcept
        : TurnClient(sink), presses_(presses), count_(count) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay&) override {}
    void paint_screen(const turn::ScreenView&) override {}

    std::uint16_t next_press() override {
        if (index_ >= count_) return turn::pad_end_of_script;
        return presses_[index_++];
    }

    [[nodiscard]] std::size_t consumed() const noexcept { return index_; }

private:
    const std::uint16_t* presses_;
    std::size_t count_;
    std::size_t index_ = 0;
};

// The constant a press is spelled with, so that a recording is committed by
// pasting it rather than by transcribing numbers back into names.
[[nodiscard]] const char* pad_name(std::uint16_t press) noexcept {
    if (press == turn::pad_up) return "pad_up";
    if (press == turn::pad_down) return "pad_down";
    if (press == turn::pad_left) return "pad_left";
    if (press == turn::pad_right) return "pad_right";
    if (press == turn::pad_a) return "pad_a";
    if (press == turn::pad_b) return "pad_b";
    if (press == turn::pad_c) return "pad_c";
    if (press == turn::pad_start) return "pad_start";
    return "pad_none";
}

// A client that decides its own presses instead of reading them, so that a
// campaign script is played out rather than written down.
//
// A campaign is far too long to choreograph by hand, and a hand-written one
// drifts the first time a board changes. This is the policy the recorded
// scripts in `platform/client/autopilot/campaign_pad.h` are the record of:
// take the only sensible offer on every screen, and on a board pick up whoever
// still owes the board a turn, strike what is in reach, otherwise walk the
// reachable tile nearest the enemy, and end the side when nobody is left.
//
// It steers by what the client itself put on screen: `Overlay::cursor_x`,
// `selected` and the lit set. A press that did not land is therefore corrected
// on the next frame rather than compounding. What it writes out is a flat list
// of presses, which is what a console can replay and a host can derive
// expectations from; the policy does not ship.
class RecordingClient final : public turn::TurnClient {
public:
    explicit RecordingClient(turn::ReportSink& sink) noexcept
        : TurnClient(sink) {}

    void paint(
        const sim::EncounterSnapshot& snapshot, const turn::Overlay& overlay
    ) override {
        board_ = snapshot;
        cursor_x_ = overlay.cursor_x;
        cursor_y_ = overlay.cursor_y;
        held_ = overlay.selected;
        walkable_.clear();
        if (overlay.moves != nullptr) walkable_ = *overlay.moves;
        on_board_ = true;
    }

    void paint_screen(const turn::ScreenView& view) override {
        screen_ = view.screen;
        on_board_ = false;
    }

    std::uint16_t next_press() override {
        // A bound rather than a budget. The policy below always makes progress
        // on a board that can be played; this is what stops a board it cannot
        // from writing a script no console would finish.
        if (recorded_.size() >= 3000) return turn::pad_end_of_script;
        const std::uint16_t press = on_board_ ? board_press() : screen_press();
        recorded_.push_back(press);
        return press;
    }

    [[nodiscard]] const std::vector<std::uint16_t>& recorded() const noexcept {
        return recorded_;
    }

private:
    // Where a screen goes. Each of these is the only thing the screen is for:
    // the title and the stage between battles are left with START, and every
    // screen that is a page of words or a single offer is taken with A.
    [[nodiscard]] std::uint16_t screen_press() const noexcept {
        switch (screen_) {
            case turn::Screen::title: return turn::pad_start;
            case turn::Screen::company: return turn::pad_start;
            case turn::Screen::member: return turn::pad_b;
            default: return turn::pad_a;
        }
    }

    [[nodiscard]] static int apart(sim::Position from, sim::Position to) {
        const int dx = from.x - to.x;
        const int dy = from.y - to.y;
        return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    }

    // One step of the cursor towards a tile, which is how every gesture below
    // aims. Reading the cursor back off the overlay each frame is what makes a
    // press that was refused cost one frame instead of the whole activation.
    [[nodiscard]] std::uint16_t steer(sim::Position towards) const noexcept {
        if (cursor_x_ < towards.x) return turn::pad_right;
        if (cursor_x_ > towards.x) return turn::pad_left;
        if (cursor_y_ < towards.y) return turn::pad_down;
        return turn::pad_up;
    }

    [[nodiscard]] bool cursor_on(sim::Position tile) const noexcept {
        return cursor_x_ == tile.x && cursor_y_ == tile.y;
    }

    [[nodiscard]] const sim::UnitSnapshot* owing() const noexcept {
        for (const sim::UnitSnapshot& unit : board_.units) {
            if (unit.side != board_.active_side) continue;
            if (!sim::on_board(unit) || unit.has_acted) continue;
            return &unit;
        }
        return nullptr;
    }

    // Whoever this character could strike from where it stands. The band is the
    // character's own, so an archer that cannot strike an adjacent enemy is not
    // offered one.
    [[nodiscard]] const sim::UnitSnapshot* in_reach(
        const sim::UnitSnapshot& actor
    ) const noexcept {
        for (const sim::UnitSnapshot& other : board_.units) {
            if (other.side == actor.side || !sim::on_board(other)) continue;
            const int gap = apart(actor.position, other.position);
            if (gap >= actor.minimum_reach && gap <= actor.maximum_reach) {
                return &other;
            }
        }
        return nullptr;
    }

    [[nodiscard]] sim::Position closing_tile(
        const sim::UnitSnapshot& actor
    ) const noexcept {
        sim::Position best = actor.position;
        int nearest = 0x7fff;
        for (const sim::UnitSnapshot& other : board_.units) {
            if (other.side == actor.side || !sim::on_board(other)) continue;
            for (const sim::Position tile : walkable_) {
                const int gap = apart(tile, other.position);
                if (gap < nearest) {
                    nearest = gap;
                    best = tile;
                }
            }
        }
        return best;
    }

    std::uint16_t board_press() noexcept {
        // The side is finished one character at a time, so ending it is the
        // board menu's row rather than a press: START opens the menu, one step
        // down reaches END TURN, and A drains whoever is left.
        if (drain_ > 0) {
            const std::uint16_t press =
                drain_ == 3 ? turn::pad_start
                            : (drain_ == 2 ? turn::pad_down : turn::pad_a);
            --drain_;
            return press;
        }
        const sim::UnitSnapshot* const actor = owing();
        if (actor == nullptr) {
            drain_ = 3;
            return board_press();
        }
        // A character that has been in hand too long is a character the policy
        // cannot play. Finish the side rather than press at it forever.
        if (++spent_frames_ > 60) {
            spent_frames_ = 0;
            stage_ = Stage::select;
            drain_ = 3;
            return board_press();
        }
        switch (stage_) {
            case Stage::select:
                if (!cursor_on(actor->position)) return steer(actor->position);
                stage_ = Stage::menu;
                return turn::pad_a;
            case Stage::menu:
                // A on one of your own opens its menu. Putting the menu down
                // leaves the character in hand over a board lighting where it
                // may walk, which is the state both gestures below aim from.
                stage_ = Stage::aim;
                return turn::pad_b;
            case Stage::aim: {
                const sim::UnitSnapshot* const mark = in_reach(*actor);
                // Two action points is one walk and one action, so a character
                // that has walked and has nothing in reach still owes the board
                // a turn and cannot spend it on a second walk, which the engine
                // refuses by name. Ending it is the third-to-last row of
                // its own menu, three steps up from the top, which wrap.
                if (mark == nullptr && actor->has_moved) {
                    stage_ = Stage::finish;
                    finish_ = 5;
                    return board_press();
                }
                const sim::Position goal =
                    mark != nullptr ? mark->position : closing_tile(*actor);
                if (!cursor_on(goal)) return steer(goal);
                stage_ = Stage::select;
                spent_frames_ = 0;
                return turn::pad_a;
            }
            case Stage::finish: {
                // The menu was put down to aim from, so it is opened again
                // before the caret is walked: Z, three steps up onto END
                // CHARACTER TURN, and the press that takes it.
                const std::uint16_t press =
                    finish_ == 5 ? turn::pad_c
                                 : (finish_ > 1 ? turn::pad_up : turn::pad_a);
                if (--finish_ == 0) {
                    stage_ = Stage::select;
                    spent_frames_ = 0;
                }
                return press;
            }
        }
        return turn::pad_a;
    }

    enum class Stage : std::uint8_t { select, menu, aim, finish };

    sim::EncounterSnapshot board_{};
    std::vector<sim::Position> walkable_{};
    std::vector<std::uint16_t> recorded_{};
    turn::Screen screen_{turn::Screen::none};
    sim::UnitId held_{0};
    std::int16_t cursor_x_{0};
    std::int16_t cursor_y_{0};
    Stage stage_{Stage::select};
    int finish_ = 0;
    int drain_ = 0;
    int spent_frames_ = 0;
    bool on_board_ = false;
};

// One pass of a campaign over a device, exactly as the console makes it: ask
// the device which slots answer, open the campaign, and run it.
client::CampaignSessionError play(
    const pf::LoadedPackage& package, std::uint64_t campaign_id,
    const char* title, const char* slot_base, storage::SlotStorage& device,
    turn::TurnClient& host
) {
    bool holds[view::slot_menu_rows] = {};
    for (int row = 0; row < view::slot_menu_rows; ++row) {
        char name[view::slot_menu_name_size] = {};
        view::slot_name_at(slot_base, row, name, sizeof name);
        holds[row] = device.contains(name);
    }
    const turn::TurnClient::SlotChoice chosen =
        host.open_campaign(title, slot_base, holds, view::slot_menu_rows);
    client::CampaignSessionOptions options;
    options.slot = chosen.slot;
    options.resume = chosen.resume;
    options.player_side = sim::Side::first;
    return client::run_persistent_campaign(
        package, campaign_id, host, host, device, options
    );
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 7) {
        std::cerr
            << "usage: "
            << (argc > 0 ? argv[0] : "grandleon_playstation_campaign_expect")
            << " <project.json> <campaign-path> <title> <slot-base>"
               " <found|resume> <expectations.txt>\n";
        return 64;
    }
    const std::string campaign_path = argv[2];
    const std::string title = argv[3];
    const std::string slot_base = argv[4];
    const std::string mode = argv[5];
    // `record` plays the founding pass with the policy above instead of with a
    // script and writes the presses it made, which is how the arrays in
    // `platform/client/autopilot/campaign_pad.h` are arrived at. It is a
    // separate invocation rather than a step of the derivation on purpose: what
    // a console replays has to be a list somebody committed, so that the
    // derivation and the run are reading the same frozen input.
    if (mode != "found" && mode != "resume" && mode != "record") {
        std::cerr << "the mode is 'found', 'resume' or 'record', not '" << mode
                  << "'\n";
        return 64;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open " << argv[1] << '\n';
        return 66;
    }
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    if (!parsed) {
        std::cerr << "the project did not parse\n";
        return 65;
    }
    const gc::CompileResult compiled = gc::compile(parsed.source);
    if (!compiled) {
        std::cerr << "the project did not compile\n";
        return 65;
    }
    pf::LoadOptions options{};
    options.engine_version = {0, 1, 0};
    options.target = pf::TargetProfile::desktop;
    options.supported_features = 0;
    options.maximum_sections = 32;
    options.maximum_records_per_section = 4096;
    const pf::LoadResult loaded = pf::load_mock_package(compiled.package, options);
    if (loaded.error != pf::Error::none) {
        std::cerr << "the compiled package did not open\n";
        return 65;
    }

    // Which script this campaign plays in which pass, chosen by the same two
    // names the executable chooses by.
    const bool tarnholt = campaign_path == "tarnholt_line";
    const std::uint16_t* const founding =
        tarnholt ? turn::tarnholt_campaign_found : turn::demo_campaign_found;
    const std::size_t founding_count = tarnholt
        ? turn::tarnholt_campaign_found_count
        : turn::demo_campaign_found_count;
    const std::uint16_t* const resuming =
        tarnholt ? turn::tarnholt_campaign_resume : turn::demo_campaign_resume;
    const std::size_t resuming_count = tarnholt
        ? turn::tarnholt_campaign_resume_count
        : turn::demo_campaign_resume_count;

    const std::uint64_t campaign_id =
        core::stable_content_id_v1(campaign_path.c_str());
    storage::MemorySlotStorage device;

    // The founding pass. Its transcript is an expectation only in `found`
    // mode; in `resume` mode it is played for its *effect* on the device and
    // discarded, because a resuming run's screens are a function of what a
    // founding run left behind.
    std::ofstream out(argv[6], std::ios::trunc);
    if (!out) {
        std::cerr << "cannot open " << argv[6] << '\n';
        return 73;
    }
    out << "# Generated by grandleon_playstation_campaign_expect. Do not edit.\n"
           "#\n"
           "# The autopilot's controller script for the '"
        << mode
        << "' pass of a\n"
           "# campaign, and what the rules say every press adds up to. Derived\n"
           "# on the host from the engine's own queries over the state the\n"
           "# shared session actually reaches. The script is compiled into\n"
           "# grandleon_psx_campaign.ps-exe and paced by counting frames;\n"
           "# platform/playstation/harness joins the executable's transcript,\n"
           "# its pixel claims, the GPU's readback and the emulator's own\n"
           "# frames against this file.\n";

    QuietSink quiet;
    if (mode == "record") {
        RecordingClient recorder(quiet);
        recorder.set_viewport(turn::viewport_cols, turn::viewport_rows);
        recorder.set_package(&loaded.package);
        const client::CampaignSessionError played = play(
            loaded.package, campaign_id, title.c_str(), slot_base.c_str(),
            device, recorder
        );
        if (played != client::CampaignSessionError::none) {
            std::cerr << "the policy could not play the campaign: "
                      << client::campaign_session_error_name(played) << '\n';
            return 70;
        }
        if (recorder.saves() == 0) {
            std::cerr << "the policy reached no save, so the script it played "
                         "is not a founding pass\n";
            return 70;
        }
        // Written as the array body it becomes, six to a line, so that what is
        // committed is a paste rather than a transcription.
        const std::vector<std::uint16_t>& presses = recorder.recorded();
        for (std::size_t i = 0; i < presses.size(); ++i) {
            if (i % 6 == 0) out << "    ";
            out << pad_name(presses[i]) << ",";
            out << ((i % 6 == 5 || i + 1 == presses.size()) ? "\n" : " ");
        }
        std::cout << "recorded " << presses.size() << " presses over "
                  << recorder.battles() << " battles and " << recorder.screens()
                  << " screens -> " << argv[6] << '\n';
        return 0;
    }
    if (mode == "resume") {
        HostClient warm(quiet, founding, founding_count);
        warm.set_viewport(turn::viewport_cols, turn::viewport_rows);
        warm.set_package(&loaded.package);
        const client::CampaignSessionError first = play(
            loaded.package, campaign_id, title.c_str(), slot_base.c_str(),
            device, warm
        );
        if (first != client::CampaignSessionError::none) {
            std::cerr << "the founding pass stopped before it saved\n";
            return 70;
        }
        if (warm.saves() == 0) {
            std::cerr << "the founding pass left nothing on the device, so a "
                         "resuming pass has nothing to resume\n";
            return 70;
        }
    }

    const std::uint16_t* const presses = mode == "found" ? founding : resuming;
    const std::size_t count = mode == "found" ? founding_count : resuming_count;

    out << "SCRIPT\n";
    for (std::size_t i = 0; i < count; ++i) out << "PRESS " << presses[i] << '\n';
    out << "TRANSCRIPT\n";

    FileSink sink(out);
    HostClient host(sink, presses, count);
    // The same window the executable has, from the one place both read it.
    host.set_viewport(turn::viewport_cols, turn::viewport_rows);
    // And the package, from the same place the executable gets it: every name
    // this client draws is the author's own word, so a derivation that did not
    // hand one over would derive the shipped table's word and disagree with the
    // machine about what a character is called.
    host.set_package(&loaded.package);

    const client::CampaignSessionError status = play(
        loaded.package, campaign_id, title.c_str(), slot_base.c_str(), device,
        host
    );
    if (status != client::CampaignSessionError::none) {
        std::cerr << "the campaign stopped: "
                  << client::campaign_session_error_name(status) << '\n';
        return 70;
    }
    if (host.consumed() != count) {
        std::cerr << "the script was not played out: " << host.consumed()
                  << " of " << count << " presses\n";
        return 70;
    }
    if (host.screens() == 0) {
        std::cerr << "the script reached no screen worth photographing\n";
        return 70;
    }
    if (mode == "found" && host.saves() == 0) {
        std::cerr << "a founding pass that saved nothing is a campaign no "
                     "second pass can resume\n";
        return 70;
    }
    if (mode == "resume" && !host.resumed()) {
        std::cerr << "a resuming pass that founded a campaign is a pass that "
                     "did not find the save it was written for\n";
        return 70;
    }

    std::cout << "derived " << host.screens() << " screens and "
              << host.checkpoints() << " board checkpoints from " << count
              << " presses -> " << argv[6] << '\n';
    return 0;
}
