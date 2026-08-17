// SPDX-License-Identifier: MIT
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/client/turn_client.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/storage/memory_storage.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// The Stage picker as a console offers it, driven by a thumb.
//
// The thing under test is **the console's own client**: this binary compiles
// `platform/client/src/turn_client.cpp` under `GRANDLEON_TURN_CLIENT_CAMPAIGN`,
// which is the same translation unit a PlayStation campaign compiles, drives it
// through the same `client::run_persistent_campaign`, and reads the same
// `Overlay` and `ScreenView` the console paints. What it presses is what a
// person presses. No console, no emulator: the fixture is compiled at test time
// and the whole run is a host one, exactly as `company_scroll_test.cpp` is.
//
// It walks the road a person checking Tarnholt walks, and both surfaces of the
// picker are on it:
//
//   START in the battle -> the board menu grows a fourth row -> the picker ->
//   the last Stage -> the board there refuses to open, because the character
//   its objective names joins at a cutscene the jump skipped -> the company
//   screen offers the picker again on its own button -> back to a Stage that
//   opens.
//
// The second half is not decoration. A refused board never reaches a battle, so
// if the picker lived only on the pause menu the player would be standing at a
// Stage they could not leave, in a slot the jump had already written.

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;
namespace turn = grandleon::client::turn;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const std::uint64_t tarnholt = core::stable_content_id_v1("tarnholt_line");

pf::LoadedPackage compile_tarnholt() {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) + "/games/tarnholt/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Tarnholt source opens");
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and parses");
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and loads");
    return loaded.package;
}

class QuietSink final : public turn::ReportSink {
public:
    void line(const char*) override {}
};

// What the walk saw, so the assertions are about the whole of it rather than
// about whichever frame happened to be last.
struct Seen final {
    // The board menu, the last time START opened one.
    std::vector<std::string> board_menu;
    // The picker, every time it was drawn: how many rows it offered and what
    // the caret's own row said.
    int picker_openings{0};
    int picker_choices{0};
    std::vector<std::string> picker_page;
    // Whether a company screen ever named the Stage button in its footer, and
    // whether one ever reported a refusal above its roster.
    bool company_footer_offers_a_stage{false};
    bool company_page_said_refused{false};
    int stage_jumps{0};
    int battles_opened{0};
};

// The client, driven one press at a time from a written script.
//
// It presses nothing of its own. When the script runs out it says so with
// `pad_end_of_script`, which every loop in the client already treats as "the
// player has gone", so a script that is too short ends the run instead of
// hanging it.
class Pilot final : public turn::TurnClient {
public:
    Pilot(turn::ReportSink& sink, std::vector<std::uint16_t> script)
        : TurnClient(sink), script_(std::move(script)) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay& overlay)
        override {
        // The board is on screen, so the picker is not. That is what makes the
        // opening count below a count of openings rather than of frames: the
        // picker redraws itself on every press that moves its caret.
        picker_frames_ = 0;
        if (!overlay.board_menu || overlay.menu == nullptr) return;
        seen_.board_menu.clear();
        for (int row = 0; row < overlay.menu_rows; ++row) {
            seen_.board_menu.emplace_back(
                overlay.menu[row].label != nullptr ? overlay.menu[row].label : ""
            );
        }
    }

    void paint_screen(const turn::ScreenView& view) override {
        if (view.screen == turn::Screen::stages) {
            ++picker_frames_;
            // One opening, however many frames the caret walking it produced.
            if (picker_frames_ == 1) ++seen_.picker_openings;
            seen_.picker_choices = view.items > 0 ? view.items : view.choices;
            seen_.picker_page.clear();
            if (view.page != nullptr) {
                for (int row = 0; row < view.page->count; ++row) {
                    seen_.picker_page.emplace_back(view.page->line(row));
                }
            }
        } else {
            picker_frames_ = 0;
        }
        if (view.screen == turn::Screen::company) {
            if (view.footer != nullptr &&
                std::string(view.footer).find("C GO") != std::string::npos) {
                seen_.company_footer_offers_a_stage = true;
            }
            if (view.page != nullptr) {
                for (int row = 0; row < view.page->count; ++row) {
                    if (std::string(view.page->line(row)).rfind("REFUSED", 0) ==
                        0) {
                        seen_.company_page_said_refused = true;
                    }
                }
            }
        }
    }

    void battle_begins(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster& roster,
        sim::Side side,
        const std::vector<std::uint64_t>& terrain
    ) override {
        ++seen_.battles_opened;
        TurnClient::battle_begins(snapshot, roster, side, terrain);
    }

    void stage_jumped(const client::StageJump& jump) override {
        // Through the client's own, not instead of it: dropping the borrowed
        // board and putting a refusal where the next company screen will draw
        // it are the client's obligations, and a pilot that skipped them would
        // be testing a client this console does not compile.
        TurnClient::stage_jumped(jump);
        if (static_cast<bool>(jump)) ++seen_.stage_jumps;
        landed_ = jump.target;
    }

    [[nodiscard]] std::uint16_t next_press() override {
        if (at_ >= script_.size()) return turn::pad_end_of_script;
        return script_[at_++];
    }

    [[nodiscard]] const Seen& seen() const noexcept { return seen_; }
    [[nodiscard]] std::size_t pressed() const noexcept { return at_; }
    [[nodiscard]] const campaign::DefinitionRef& landed() const noexcept {
        return landed_;
    }

private:
    std::vector<std::uint16_t> script_;
    std::size_t at_{0};
    int picker_frames_{0};
    Seen seen_{};
    campaign::DefinitionRef landed_{};
};

// Presses, in the order a thumb makes them. Named rather than written as bare
// constants so the script below reads as the walk it is.
constexpr std::uint16_t a = turn::pad_a;
constexpr std::uint16_t b = turn::pad_b;
constexpr std::uint16_t c = turn::pad_c;
constexpr std::uint16_t start = turn::pad_start;
constexpr std::uint16_t down = turn::pad_down;

void push(std::vector<std::uint16_t>& script, std::uint16_t press, int times) {
    for (int at = 0; at < times; ++at) script.push_back(press);
}

// The whole road, on a campaign that asked for it.
void a_thumb_reaches_a_stage_and_comes_back() {
    const pf::LoadedPackage package = compile_tarnholt();
    storage::MemorySlotStorage device;
    QuietSink sink;

    std::vector<std::uint16_t> script;
    // The handover screen and the opening cutscenes, then the company screen,
    // then the board.
    push(script, a, 13);
    script.push_back(start);
    // START opens the board menu. The picker is its last row, under the way
    // out, so three presses down reach it from BACK TO BATTLE.
    script.push_back(start);
    push(script, down, 3);
    script.push_back(a);
    // Backing out of the picker is backing out of nothing: the battle is still
    // there, with whatever was in hand still in hand, exactly as backing out of
    // the menu itself is. Only a Stage actually chosen leaves.
    script.push_back(b);
    script.push_back(start);
    push(script, down, 3);
    script.push_back(a);
    // The picker opens on the first Stage. Five down is the sixth and last,
    // which is the one no amount of playing this session would have reached.
    push(script, down, 5);
    script.push_back(a);
    // The campaign is standing at that Stage now, and the company screen comes
    // up in front of its board as it does in front of every board. Nothing is
    // wrong yet as far as the screen knows, so START asks for the battle — and
    // that is the press that finds out the board will not open.
    script.push_back(start);
    // Now the company screen says why, and C is the way off a Stage that cannot
    // be played. The picker opens at the top, on a Stage this playthrough has
    // stood on.
    script.push_back(c);
    script.push_back(a);
    // A board opens there. Leave through the board menu's own way out rather
    // than running the script dry, so the run ends the way a player ends it.
    script.push_back(start);
    script.push_back(start);
    push(script, down, 2);
    script.push_back(a);

    Pilot pilot{sink, script};
    client::CampaignSessionOptions options;
    // Asked for explicitly rather than left to the build: this executable
    // checks the picker, so it says so, and the same source checks it on a
    // machine that never configured the define.
    options.stage_picker = true;
    options.slot = "campaign";
    const client::CampaignSessionError ran = client::run_persistent_campaign(
        package, tarnholt, pilot, pilot, device, options
    );
    expect(
        ran == client::CampaignSessionError::none,
        "the run ends by being left rather than by an error"
    );

    // The row, on the menu START opens, under the way out and saying what it is.
    expect(
        pilot.seen().board_menu.size() == 4U,
        "the board menu has a fourth row in a build that carries the picker"
    );
    expect(
        pilot.seen().board_menu.size() == 4U &&
            pilot.seen().board_menu[0] == "BACK TO BATTLE" &&
            pilot.seen().board_menu[1] == "END YOUR TURN" &&
            pilot.seen().board_menu[2] == "LEAVE - THIS BATTLE IS NOT KEPT" &&
            pilot.seen().board_menu[3] == "GO TO ANOTHER STAGE - TESTING",
        "and it is last, under the way out, because it is an aid and not one of "
        "the questions this menu exists to answer"
    );

    // The picker itself, opened twice: once out of the battle and once off the
    // company screen the refused board sent the player to.
    expect(
        pilot.seen().picker_openings == 3,
        "the picker was opened three times: twice out of the battle, once off "
        "the company screen, which is both surfaces that offer it"
    );
    expect(
        pilot.seen().picker_choices == 6,
        "and it offered every one of Tarnholt's six Stages"
    );
    expect(
        !pilot.seen().picker_page.empty() &&
            pilot.seen().picker_page[0].rfind("GO TO ANOTHER STAGE", 0) == 0,
        "under a heading that says what it is"
    );
    expect(
        pilot.seen().picker_page.size() > 1U &&
            pilot.seen().picker_page[1].find("MAY NOT OPEN") != std::string::npos,
        "and a line saying what an unseen Stage costs, because the player finds "
        "out here or by being stuck"
    );
    // The rows carry the author's own names, numbered by their place in the
    // flow, with the campaign's position marked.
    bool named = false;
    bool marked_here = false;
    for (const std::string& row : pilot.seen().picker_page) {
        if (row.find("THE COLDGATE") != std::string::npos) named = true;
        if (row.find("HERE") != std::string::npos) marked_here = true;
    }
    expect(named, "each row is the board's authored name");
    expect(
        marked_here,
        "and the Stage the campaign is standing on says so, which is what "
        "separates a jump a player has made from one they are about to"
    );

    expect(
        pilot.seen().stage_jumps == 2,
        "two jumps were taken and both moved the campaign"
    );
    expect(
        pilot.seen().company_page_said_refused,
        "with the roster's word for the refused board said on the screen it "
        "sent the player to"
    );
    expect(
        pilot.seen().company_footer_offers_a_stage,
        "and that screen naming the button that gets them off it"
    );
    expect(
        pilot.seen().battles_opened == 2,
        "two boards opened: the one left, and the one the second jump came back "
        "to"
    );
    expect(
        pilot.pressed() == 39U,
        "and every press of the script was used, so nothing above happened for "
        "want of a button rather than because it was asked for"
    );

    // Where the slot was left. The battles committed nothing, so the only
    // things this campaign ever recorded are the two jumps.
    const client::PackageBoards boards{package};
    client::CampaignSessionOptions resuming = options;
    resuming.resume = true;
    client::CampaignSession reopened{
        package, tarnholt, boards, device, resuming
    };
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        reopened.begin(failure, refused, resumed) ==
                client::CampaignSessionError::none &&
            resumed,
        "the slot the run wrote reads back"
    );
    expect(
        reopened.standing().encounter_id ==
            core::stable_content_id_v1("tarnholt_line/fordlight_battle"),
        "standing on the Stage the second jump went to"
    );
}

}  // namespace

int main() {
    a_thumb_reaches_a_stage_and_comes_back();
    if (failures == 0) std::cout << "stage menu checks passed\n";
    return failures == 0 ? 0 : 1;
}
