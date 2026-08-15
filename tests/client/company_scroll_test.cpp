// SPDX-License-Identifier: MIT
// A company larger than the screen, walked to its last row on a host.
//
// Truncation here is silent by nature: a row past the window's end is simply
// not drawn, with nothing on screen to say so. A silent failure cannot be
// caught by looking, so it is caught here instead, and
// the thing under test is the *console's own client*: this binary compiles
// `platform/client/src/turn_client.cpp` under `GRANDLEON_TURN_CLIENT_CAMPAIGN`,
// which is the same translation unit a campaign console compiles, drives it
// through the same `client::run_persistent_campaign`, and reads the same
// `ScreenView` the console paints.
//
// What it does is what a player does: it presses down until it has stood in
// front of every member of a twelve-member company, and at every step it
// requires the page the client just composed to contain the member the caret
// is on, on the row the caret is beside.
//
// It then asserts the window itself rather than only what the window let
// through, because with exactly twelve members the *incidental* symptom is not
// the missing member but the missing store. A sixteen-row page holds a
// heading, a blank and twelve roster rows with four rows to spare, so an
// unwindowed composer keeps every member and drops the company's whole store
// off the bottom instead; at fifteen members it starts dropping members too.
// Both are the same bug and both are silent, so what is pinned is the cause:
// the roster is drawn a window at a time, the window says how long the list
// behind it is, and the store always gets its own four rows. Removing the
// window fails those two assertions immediately. That is verified, not
// assumed.
//
// The content is `tests/fixtures/campaign_projects/large_company.json`, which
// is a fixture and not a golden. Nothing compares its bytes to anything, it is
// compiled at test time, and regenerating it is not a way to make a failing
// test pass. It authors twelve members and nine distinct carried items so
// that both windows on the screen have something to scroll.

#include <grandleon/client/campaign_session.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/manifest.hpp>
#include <grandleon/storage/byte_window_storage.hpp>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <grandleon/client/turn_client.hpp>

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;
namespace turn = grandleon::client::turn;
namespace view = grandleon::view;

namespace {

int failures = 0;

void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void expect(bool condition, std::string_view message) {
    if (!condition) fail(message);
}

// The campaign, and the slot it is kept in. Slot one's name, because that is
// the row the caret opens on and the row a screen with one save has always
// used.
constexpr const char* campaign_slot = "scroll";
constexpr const char* campaign_key = "the_long_company";
constexpr int expected_roster = 12;
// Nine of the twelve carry one distinct thing each, so a store that has been
// emptied into holds nine stacks and a member's GIVE list is nine rows long.
constexpr int expected_distinct_items = 9;

pf::LoadedPackage compile_the_long_company() {
    const std::string filename = std::string(GRANDLEON_SOURCE_DIR) +
                                 "/tests/fixtures/campaign_projects/"
                                 "large_company.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the large-company project opens");
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and parses");
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and loads");
    return loaded.package;
}

// Everything this client did on the screen it drew, in ASCII: the page is
// upper-cased and space-trimmed by `push_line`, so a name is compared the way
// the console draws it rather than the way the author typed it.
std::string shouted(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char raw : text) {
        char value = raw;
        if (value >= 'a' && value <= 'z') {
            value = static_cast<char>(value - ('a' - 'A'));
        }
        if (value < 0x20 || value > 0x5F) value = ' ';
        out.push_back(value);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// The client, driven. It presses nothing of its own: the walk below hands it
// one press at a time and reads the page it composed in answer.
class ScrollPilot : public turn::TurnClient {
public:
    explicit ScrollPilot(turn::ReportSink& sink) noexcept : TurnClient(sink) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay&) override {}

    void paint_screen(const turn::ScreenView& view) override { screen_ = view; }

    // A queued press when the walk has one, and otherwise the press that means
    // "go on" for whichever screen is up. That is the same shape a console's
    // expectation pilot uses, and for the same reason: a screen this test is
    // not about should not need a press written down for it.
    //
    // The ceiling is a runaway guard. A client whose caret could not reach a
    // row would otherwise press down forever, which is a hang rather than a
    // failure, and a hang says nothing.
    [[nodiscard]] std::uint16_t next_press() override {
        if (++presses_ > press_ceiling) {
            stalled_ = true;
            return turn::pad_end_of_script;
        }
        if (!queued_.empty()) {
            const std::uint16_t press = queued_.front();
            queued_.erase(queued_.begin());
            return press;
        }
        switch (screen_.screen) {
            case turn::Screen::company:
                // The handover page has nothing to choose and wants A; the
                // management stage has a caret and, with the walk spent, is
                // done.
                return screen_.choices > 0 ? turn::pad_b : turn::pad_a;
            case turn::Screen::member:
                return turn::pad_b;
            default:
                return turn::pad_a;
        }
    }

    void queue(std::uint16_t press) { queued_.push_back(press); }
    [[nodiscard]] bool stalled() const noexcept { return stalled_; }
    [[nodiscard]] int presses() const noexcept { return presses_; }

protected:
    turn::ScreenView screen_{};

private:
    static constexpr int press_ceiling = 4096;
    std::vector<std::uint16_t> queued_{};
    int presses_{0};
    bool stalled_{false};
};

// A sink that keeps nothing. The transcript is not what is under test here;
// the page is.
class QuietSink final : public turn::ReportSink {
public:
    void line(const char*) override {}
};

// What the walk found, so `main` can assert about the whole of it rather than
// only about the moment it happened.
struct Walk final {
    // One entry per member: was that member's row on the page when the caret
    // stood on them, and did the caret's own row name them.
    std::vector<bool> on_the_page;
    std::vector<bool> under_the_caret;
    // Whether any page said it was scrolling, and whether the store's heading
    // stayed on the same page row throughout.
    bool ever_scrolled{false};
    bool store_heading_moved{false};
    int store_heading_row{-1};
    int deepest_page{0};
    // The most roster rows any company page drew at once, and the longest list
    // any of them said it was a window onto. Without a window the first is
    // twelve and the second is zero, which is the regression this pins.
    int widest_roster_window{0};
    int longest_roster_list{0};
    // The fewest store rows any company page managed to draw. Without a window
    // the roster eats the page and this is zero on every screen.
    int fewest_store_rows{-1};
    int menu_rows_seen{0};
    bool menu_last_row_reachable{false};
};

// The narrator that does the walking.
//
// It is `TurnClient` that is under test, so this class is not a second client:
// it is the thing that decides which press to hand it next, which is exactly
// what a console's expectation pilot is. The difference is what it is looking
// for: reachability rather than a transcript.
class Walker final : public ScrollPilot {
public:
    Walker(turn::ReportSink& sink, const std::vector<std::string>& names)
        : ScrollPilot(sink), names_(names) {
        walk_.on_the_page.assign(names.size(), false);
        walk_.under_the_caret.assign(names.size(), false);
    }

    [[nodiscard]] const Walk& walk() const noexcept { return walk_; }

    void after_screen(const turn::ScreenView& view) override {
        if (view.items > view.choices) walk_.ever_scrolled = true;
        if (view.page != nullptr && view.page->count > walk_.deepest_page) {
            walk_.deepest_page = view.page->count;
        }
        if (view.screen == turn::Screen::company && view.caret >= 0) {
            record_company(view);
        }
        if (view.screen == turn::Screen::member) {
            record_member_menu(view);
        }
    }

    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override {
        // The presses for one pass of the walk are queued before the client is
        // asked, because the client drives its own input loop: it will not
        // return until the script says A or B.
        if (!walked_) {
            walked_ = true;
            queue_the_walk(static_cast<int>(company.roster.size()));
        }
        return ScrollPilot::next_management_intent(company);
    }

private:
    // Down to the last member, back up to the first, and then A on the last
    // one, so the member menu that opens is the *twelfth* member's, which is
    // the one a page with no window could never have reached.
    void queue_the_walk(int members) {
        for (int step = 0; step + 1 < members; ++step) queue(turn::pad_down);
        for (int step = 0; step + 1 < members; ++step) queue(turn::pad_up);
        for (int step = 0; step + 1 < members; ++step) queue(turn::pad_down);
        // Open the last member's menu, walk it to its own last row, and back
        // out of it; then leave the stage.
        queue(turn::pad_a);
        for (int step = 0; step < 32; ++step) queue(turn::pad_down);
        queue(turn::pad_b);
        queue(turn::pad_b);
    }

    void record_company(const turn::ScreenView& view) {
        if (view.choices > walk_.widest_roster_window) {
            walk_.widest_roster_window = view.choices;
        }
        if (view.items > walk_.longest_roster_list) {
            walk_.longest_roster_list = view.items;
        }
        const int member = view.caret;
        if (member < 0 || member >= static_cast<int>(names_.size())) return;
        const std::string wanted = shouted(names_[static_cast<std::size_t>(member)]);

        bool anywhere = false;
        for (int row = 0; row < view.page->count; ++row) {
            if (std::string(view.page->line(row)).find(wanted) == 0) {
                anywhere = true;
                break;
            }
        }
        walk_.on_the_page[static_cast<std::size_t>(member)] = anywhere;

        const bool beside =
            view.caret_row >= 0 && view.caret_row < view.page->count &&
            std::string(view.page->line(view.caret_row)).find(wanted) == 0;
        walk_.under_the_caret[static_cast<std::size_t>(member)] = beside;

        // The store's heading, which must not move while the roster scrolls.
        for (int row = 0; row < view.page->count; ++row) {
            if (std::string(view.page->line(row)).find("THE COMPANY'S STORE") ==
                0) {
                if (walk_.store_heading_row < 0) {
                    walk_.store_heading_row = row;
                } else if (walk_.store_heading_row != row) {
                    walk_.store_heading_moved = true;
                }
                const int drawn = view.page->count - (row + 1);
                if (walk_.fewest_store_rows < 0 ||
                    drawn < walk_.fewest_store_rows) {
                    walk_.fewest_store_rows = drawn;
                }
                break;
            }
        }
    }

    void record_member_menu(const turn::ScreenView& view) {
        if (view.items > walk_.menu_rows_seen) walk_.menu_rows_seen = view.items;
        if (view.caret >= 0 && view.caret == view.items - 1 &&
            view.caret_row >= 0 && view.caret_row < view.page->count) {
            walk_.menu_last_row_reachable = true;
        }
    }

    const std::vector<std::string>& names_;
    Walk walk_{};
    bool walked_{false};
};

// One founding of the long company, with `taker` deciding what happens on the
// management stage. Returns whether the session ran clean.
template <typename Narrator>
bool run_the_company(
    const pf::LoadedPackage& package,
    storage::SlotStorage& device,
    Narrator& narrator,
    bool resume
) {
    client::CampaignSessionOptions options;
    options.slot = campaign_slot;
    options.resume = resume;
    options.player_side = sim::Side::first;
    const client::CampaignSessionError status = client::run_persistent_campaign(
        package, core::stable_content_id_v1(campaign_key), narrator, narrator,
        device, options
    );
    if (status != client::CampaignSessionError::none) {
        std::cerr << "the long company stopped: "
                  << client::campaign_session_error_name(status) << '\n';
        return false;
    }
    return true;
}

// The narrator that empties every member's satchel into the store, so that the
// *next* run's member menu is long enough to need a window of its own.
class Emptier final : public ScrollPilot {
public:
    explicit Emptier(turn::ReportSink& sink) noexcept : ScrollPilot(sink) {}

    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override {
        for (const client::RosterEntry& member : company.roster) {
            if (member.carried.empty()) continue;
            client::ManagementIntent intent;
            intent.verb = client::ManagementVerb::take;
            intent.member = member.member;
            intent.item = member.carried.front().item;
            return intent;
        }
        return {client::ManagementVerb::quit, {}, {}};
    }
};

}  // namespace

int main() {
    const pf::LoadedPackage package = compile_the_long_company();
    if (failures != 0) {
        std::cout << "RESULT company_scroll FAIL " << failures << '\n';
        return 1;
    }

    // The same 32 KiB window and the same four slots a cartridge has, because
    // the point of driving the real client is that nothing about it is a
    // stand-in.
    storage::VectorByteWindow cartridge(32U * 1024U, 0xFF);
    storage::ByteWindowSlotStorage device(
        cartridge, storage::ByteWindowSlotStorage::budget_for(32U * 1024U, 4)
    );

    // Run one: every satchel into the store, so the company owns nine distinct
    // stacks and a member's menu is fourteen rows long.
    {
        QuietSink sink;
        Emptier emptier(sink);
        emptier.set_viewport(turn::viewport_cols, turn::viewport_rows);
        bool holds[view::slot_menu_rows] = {};
        const turn::TurnClient::SlotChoice chosen =
            emptier.open_campaign(
                pr::project_title(package), campaign_slot, holds,
                view::slot_menu_rows
            );
        expect(!chosen.resume, "an empty cartridge founds");
        expect(
            std::string_view(chosen.slot) == campaign_slot,
            "and the first row is the slot the flow has always used"
        );
        expect(
            run_the_company(package, device, emptier, false),
            "the founding run reaches the end"
        );
    }

    // Run two: the walk.
    QuietSink sink;
    // The authored order, which is the order founding assigns identities in and
    // therefore the order the roster comes back in. Every one of them is short
    // enough for the fifteen columns a company row gives a name, so a row can
    // be matched against the whole of it rather than against a prefix. The
    // fixture is authored to make this assertion the strong one.
    const std::vector<std::string> names = {
        "Adair Coldwell", "Brannoch Vale",  "Cerian Duskrow",
        "Dolan Ward",     "Eirlys Fenn",    "Faelan Grove",
        "Gwenna Thistle", "Hollis Reed",    "Isolde Wray",
        "Jarreth Combe",  "Kerra Linden",   "Lowry Penhale",
    };
    expect(
        static_cast<int>(names.size()) == expected_roster,
        "the fixture authors the company this test is about"
    );

    Walker walker(sink, names);
    walker.set_viewport(turn::viewport_cols, turn::viewport_rows);
    {
        bool holds[view::slot_menu_rows] = {};
        for (int row = 0; row < view::slot_menu_rows; ++row) {
            char name[view::slot_menu_name_size];
            view::slot_name_at(campaign_slot, row, name, sizeof name);
            holds[row] = device.contains(name);
        }
        expect(holds[0], "the founding run wrote slot one");
        expect(
            !holds[1] && !holds[2] && !holds[3],
            "and wrote nothing into the other three"
        );
        const turn::TurnClient::SlotChoice chosen =
            walker.open_campaign(
                pr::project_title(package), campaign_slot, holds,
                view::slot_menu_rows
            );
        expect(chosen.resume, "a cartridge that answers offers to continue");
        expect(
            run_the_company(package, device, walker, chosen.resume),
            "the walking run reaches the end"
        );
    }

    const Walk& walk = walker.walk();

    // The claim this file exists to make.
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (!walk.on_the_page[index]) {
            fail("a member was not on the page when the caret stood on them: " +
                 names[index]);
        }
        if (!walk.under_the_caret[index]) {
            fail("the caret was not beside the member it was on: " +
                 names[index]);
        }
    }

    expect(
        walk.ever_scrolled,
        "a twelve-member company in a seven-row window scrolls"
    );
    // The window itself, rather than what it happened to let through. A page
    // with no window draws all twelve roster rows and reports no list at all,
    // so these two are what fails first if the window is removed, before any
    // member goes missing and before the store is pushed off the page.
    expect(
        walk.widest_roster_window == turn::company_roster_rows,
        "the roster is drawn a window at a time and never more"
    );
    expect(
        walk.longest_roster_list == expected_roster,
        "and the window says how long the list behind it is"
    );
    expect(
        walk.deepest_page <= turn::page_capacity,
        "and never composes more rows than the page holds"
    );
    expect(
        !walk.store_heading_moved,
        "the store's heading keeps its page row while the roster scrolls"
    );
    expect(
        walk.store_heading_row >= 0,
        "and the store is on the screen at all"
    );
    expect(
        walk.fewest_store_rows == turn::company_store_rows,
        "with a full window of its stacks under the heading, on every screen"
    );

    // The member menu, on the same terms. Fourteen rows in a window of eight:
    // the availability row, nine GIVE rows and CANCEL.
    expect(
        walk.menu_rows_seen >= expected_distinct_items + 2,
        "a member's menu offers a row per stack the store holds"
    );
    expect(
        walk.menu_rows_seen > turn::member_menu_rows,
        "which is more rows than the menu's window shows at once"
    );
    expect(
        walk.menu_last_row_reachable,
        "and its last row is reachable under the caret"
    );

    expect(
        walker.save_failures() == 0, "every write to the host cartridge took"
    );
    expect(
        !walker.stalled(),
        "the walk finished rather than pressing down forever"
    );

    if (failures == 0) {
        std::cout << "RESULT company_scroll PASS roster " << names.size()
                  << " menu " << walk.menu_rows_seen << " page "
                  << walk.deepest_page << '\n';
        return 0;
    }
    std::cout << "RESULT company_scroll FAIL " << failures << '\n';
    return 1;
}
