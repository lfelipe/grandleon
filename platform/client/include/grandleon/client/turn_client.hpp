// SPDX-License-Identifier: MIT
// A console turn client's platform-free half.
//
// Everything a player expresses with a controller, and everything the client
// believes is on the board because of it, lives here. None of it draws a
// pixel or reads a port. Every build derives from it: the PlayStation's
// executable, which paints with the GPU and polls SIO0; and the host tools
// that paint nothing and read a scripted sequence of presses.
//
// That split is the whole verification argument. The host build derives what
// every step of a scripted session should look like *from the rules*, and
// writes it out before the ROM or the executable is built or run. The rules
// are the engine's own `reachable_tiles`, `danger_tiles` and
// `forecast_attack`, applied to the state `client::run_campaign` actually
// reaches. The console then reports the same transcript from its own CPU, and
// the harness requires the two to agree line for line. Neither side can
// quietly agree with itself: the expectations are produced by a different
// compiler on a different architecture, ahead of the run that is checked
// against them.
//
// It lives here rather than beside one console because nothing in it is any
// one console's, and a second copy on a second machine would be a second
// chance for two consoles to disagree about what a turn is. What is above this
// seam is a platform: a
// `paint`, a `next_press`, and three animations. What is below it is
// `grandleon::view`, `grandleon::sheet` and `grandleon::client`, which every
// client already shared.
//
// What is deliberately *not* here: any rule. This class asks the engine what is
// reachable, what is threatened and what an attack would cost, and it asks
// `client::run_campaign` to own the turn order and the opposing side. It
// decides only which of those answers a button press is asking for.

// ---------------------------------------------------------------------------
// The campaign build
//
// `GRANDLEON_TURN_CLIENT_CAMPAIGN` adds a second half to this file: the same board,
// the same cursor, the same action menu and the same information sheet, with a
// `client::CampaignFrontEnd` around them instead of a `client::Presenter`. It is
// a variant of this file rather than a second client for the reason the
// Nintendo 64's `play_rom.cpp` is a variant of itself: everything a player
// touches on a board is identical, and a second copy of it would be a second
// chance for two ROMs to disagree about what a turn is.
//
// The PlayStation defines it: `grandleon_psx_campaign` and its sibling are this
// file with the macro on, beside a turn executable that is this file with the
// macro off. Two host tests compile it as well, so the campaign composer is
// checked whether or not a console links it:
// `tests/client/company_scroll_test.cpp` walks a company longer than the
// screen, and `tests/client/story_paging_test.cpp` a speech taller than a page.
//
// Every campaign screen it adds is a page of text composed here, where the host
// compiles it too, and placed by the platform. Nothing about a screen's content
// is worked out on the console: it receives fixed-width lines and turns them
// into whatever its hardware draws text with.
// ---------------------------------------------------------------------------

#ifndef GRANDLEON_PLATFORM_CLIENT_TURN_CLIENT_HPP
#define GRANDLEON_PLATFORM_CLIENT_TURN_CLIENT_HPP

#include <grandleon/client/presenter.hpp>
#include <grandleon/sheet/unit_sheet.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/view/board_view.hpp>
#include <grandleon/view/motion.hpp>
#include <grandleon/view/list_view.hpp>

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/view/slot_menu.hpp>
#endif

#include <cstdint>
#include <vector>

namespace grandleon::client::turn {

namespace sim = grandleon::simulation;
namespace client = grandleon::client;
namespace view = grandleon::view;
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
namespace campaign = grandleon::campaign;
namespace storage = grandleon::storage;
namespace pr = grandleon::package_runtime;
#endif

// ---------------------------------------------------------------------------
// The window
//
// How much of a board fits is a property of the screen, and both halves of this
// client have to agree about it or they are playing two different games: the
// camera scrolls when the cursor reaches the edge of the window, so a host
// derivation made against a taller window would put the camera somewhere the
// console never puts it, and every coordinate downstream would differ.
//
// At the native cell it is ten columns by six rows, and a console arrives at
// that from its own hardware rather than from this line. The PlayStation is
// 320x240 with a 32-pixel cell, less the four eight-pixel rows the message bar
// takes, which leaves 208 lines: six whole rows and sixteen lines over. The
// sixteen become the headroom a raised cell lifts into rather than a seventh
// row a shorter screen could not match.
// `platform/playstation/src/turn_exe.cpp` derives the two numbers and asserts
// they match these, so this is one definition rather than a copy per machine.
// ---------------------------------------------------------------------------
inline constexpr int viewport_cols = 10;
inline constexpr int viewport_rows = 6;

// What the PlayStation actually gives a board, once the cell is allowed to
// shrink.
//
// The window above is what ten by six of the *native* cell buys, and every map
// this repository ships is larger than it in at least one direction. So the
// cell shrinks to fit the board and the window only appears when shrinking
// would stop the art being readable, which is the Nintendo 64's arrangement
// and the reason `view::fit_board` is shared rather than copied.
//
// The four numbers, and why each is what it is:
//
//   320 x 208  the whole display across, and what the message bar leaves down.
//    32        the largest cell: the art is 32 texels, and drawing it larger
//              than it is enlarges nothing. It also means a board that already
//              fitted is drawn exactly as it was.
//    16        the smallest cell: half the texels, which on a GPU with no
//              filter is the one reduction that drops every other texel evenly
//              rather than unevenly. Below that the board scrolls.
//    16        and a scrolling board draws at that same smallest cell, which
//              buys the widest window a readable cell allows: twenty by
//              thirteen.
//
// It lives here, next to the window it replaces, because the executable and
// the host derivation both read it and a second copy is a second answer.
inline constexpr view::FitRule board_fit{320, 208, 32, 16, 16};

// ---------------------------------------------------------------------------
// The pad
//
// Edge-triggered: one bit per button that went down this poll. Three face
// buttons and a start, named A, B and C, because three is what every scripted
// session this repository checks was written against and a rename would move
// every one of them.
//
// A console keeps the names and maps its own face buttons onto them, which is
// cheaper than a vocabulary per machine. The PlayStation's mapping, decided in
// `platform/playstation/src/psx_pad.h` and stated here so a reader of a script
// can follow it:
//
//     pad_a      cross     confirm: pick a character up, put it down, take a
//                          menu row, aim a strike at the tile under the cursor
//     pad_b      circle    back out: drop a selection, close the menu, put the
//                          sheet down, abandon an aim
//     pad_c      triangle  open the unit action menu, and on the company
//                          screen open the Stage picker where a game has one
//     pad_start  start     "I am done": open the battle from the arranging
//                          stage, end an activation
//
// The company screen's use of C is the one place a button does something a row
// does not also do, and the footer names it there whenever it does anything at
// all. It is a button rather than a row because that screen's caret walks the
// company, and a row that was not a member would be a row the member menu below
// has nothing to open for.
//
// Cross confirms and circle cancels, which is the Western PlayStation
// convention and the one a player who has used this console will already have
// in their thumbs. Triangle is the menu because it is the button neither of the
// other two idioms claims, and because it is the third one.
// ---------------------------------------------------------------------------
inline constexpr std::uint16_t pad_up = 1U << 0;
inline constexpr std::uint16_t pad_down = 1U << 1;
inline constexpr std::uint16_t pad_left = 1U << 2;
inline constexpr std::uint16_t pad_right = 1U << 3;
inline constexpr std::uint16_t pad_a = 1U << 4;
inline constexpr std::uint16_t pad_b = 1U << 5;
inline constexpr std::uint16_t pad_c = 1U << 6;
inline constexpr std::uint16_t pad_start = 1U << 7;

// Not a button, and no pad can produce it. A host build that has run out of
// script says so with this, and the client ends the session; the ROM has no
// script and never sends it, because a player cannot run out of thumbs.
inline constexpr std::uint16_t pad_end_of_script = 1U << 15;

// ---------------------------------------------------------------------------
// The report
//
// One line at a time, so the ROM can send it down the KMod channel and the host
// tool can write it to a file, and neither has to know which. The format is
// defined once, in `turn_client.cpp`, and both sides emit it from that one
// place. A transcript that could be formatted two ways would be comparing two
// spellings rather than two machines.
// ---------------------------------------------------------------------------
class ReportSink {
public:
    ReportSink() = default;
    ReportSink(const ReportSink&) = delete;
    ReportSink& operator=(const ReportSink&) = delete;
    virtual ~ReportSink() = default;

    virtual void line(const char* text) = 0;
};

// One row of the unit action menu. Fixed storage, because a menu that
// allocated would perturb the heap census the ROM takes beside it.
//
// A row is one of eight things, and which one is readable without consulting
// the label: a walk (`move` set), a strike (`ability` zero, with `weapon`
// naming which carried weapon, or zero for the one in hand), a cast (`ability`
// set), a spent item (`item` set), a talk (`talk` set), the end of the
// character's own turn, the character's information sheet, or the way out.
struct MenuChoice final {
    // What to show. Owned by the shared display-name table, never by content.
    const char* label{nullptr};
    // The one identity this row names: the weapon a strike swings (zero for the
    // one in hand), the ability a cast throws, or the item a use spends. Which
    // of the three it is, is the flags' business below.
    //
    // **One field rather than three.** `ability`, `weapon` and `item` side by
    // side would be twenty-four bytes of 64-bit identity per row, of which at
    // most one is ever set. A menu lives in a console's `.bss` for the whole
    // run, so three quarters of it always zero is three quarters of it paid
    // for on every frame of every battle.
    sim::ContentId content{0};
    // Walks the character. Like a strike and unlike an item, the row names no
    // tile yet: it hands the player back the cursor over the tiles the engine
    // already lights, and whichever of them they land on is the destination.
    //
    // It is the first row because it is the first thing a turn does, and it is
    // a row at all because a board whose only way to walk was a press on open
    // ground never told anybody that walking was one of the character's
    // choices. The press still works and is now a shortcut for a row the menu
    // names rather than the only way to find it.
    bool move{false};
    // Strikes with `content`, which is zero for the weapon in hand. A row that
    // is none of the flags below and not this one is not a row this client
    // builds.
    bool attack{false};
    // Casts the ability in `content`.
    bool cast{false};
    // Spends the carried item in `content`. Unlike a strike or a cast, this row
    // commits on the press: an item reaches the hand that holds it, so there
    // is nothing left for the player to aim.
    bool use{false};
    // Talks to somebody standing beside the character. Unlike an item and like
    // a strike, this row names nobody yet: a talk reaches a neighbour rather
    // than the hand that holds it, so it hands the player back the cursor and
    // the engine judges whoever it lands on.
    bool talk{false};
    bool wait{false};
    // Opens the character's full information sheet over the menu, and comes
    // back to it. The only row that commits nothing at all, which is why it
    // sits below WAIT and above CANCEL rather than among the rows that spend a
    // turn.
    bool info{false};
    // Opens the Stage picker over the battle, and leaves the battle for
    // whichever Stage is chosen. A board-menu row only, and only on a campaign
    // whose author asked for the picker: the row is built when the session
    // handed this client a list of Stages, and that list is empty otherwise.
    //
    // A flag rather than a ninth thing squeezed into `content`, for the reason
    // `wait` is a flag: which row a press took has to be readable without
    // consulting the label, or the two menus that share this array come to
    // disagree about what a row means.
    bool stage{false};
    bool cancel{false};
};

// Room for what a character carries plus the four rows every menu is framed by:
// WALK above them, and the end of the character's turn, its sheet and the way
// out below. Four content rows, which is exactly what the widest shipped unit
// type asks for: the mage, at one strike row, two spells and a draught.
//
// **It did not grow when WALK arrived.** A ninth row is a few dozen bytes of
// `.bss`, which on a console with kilobytes of heap is not free even when the
// total says it is: the number that decides an allocation is the largest
// contiguous free block, and moving the `.bss` base is enough to break a block
// that had been fitting. A census that reports only the total will let a change
// like this look free right up until it is fatal.
//
// So WALK takes a content row here where the Nintendo 64 gave it a slot of its
// own. Nothing shipped loses a row, and what it would take to give the row back
// is this: three quarters of every row's bytes
// are 64-bit identities of which at most one is ever set, so a row that carried
// one identity and a kind byte would be fourteen bytes instead of thirty-two
// and would pay for two more rows out of the saving.
inline constexpr int menu_capacity = 8;

// The most item rows a menu offers, and the room one row's label needs: the
// longest shipped item name plus " x99". Item rows are the only menu text that
// is not a constant, because a row says how many are left.
inline constexpr int menu_item_labels = 4;
inline constexpr int menu_item_label_size = 24;

// What the player picked out of the menu and has not yet pointed at anything.
//
// The menu answers "what should this character do"; a strike, a cast and a talk
// all still need a tile, and the client holds the answer to the first question
// while the player gives the second. Waiting needs no tile, so it never
// reaches here, and neither does spending an item, which reaches only the hand
// that holds it.
enum class Aim : std::uint8_t {
    none = 0,
    // A walk, waiting for the tile to walk to. It carries no identity of its
    // own: where the character goes is the whole of the command, and which
    // tiles it may go to is the engine's `reachable_tiles`, which the board is
    // already lighting for the selection that took the row.
    walk,
    // A strike, with the weapon in `aim_weapon_` (zero for the one in hand),
    // waiting for the enemy to aim it at.
    strike,
    // A cast, with the ability in `aim_ability_`, waiting for its centre tile.
    cast,
    // A talk, waiting for the neighbour to aim it at. It carries no identity of
    // its own: what a talk records is authored on whoever is talked to, so the
    // cursor's answer is the whole of the command.
    talk,
};

// What the platform has to put on screen. Everything in it is either the
// engine's answer or the player's cursor; nothing here is a rule.
struct Overlay final {
    std::int16_t cursor_x{0};
    std::int16_t cursor_y{0};
    sim::UnitId selected{0};
    // The engine's answers, recomputed only when the selection or the pick
    // changes because `danger_tiles` is the most expensive query this client
    // makes.
    //
    // **`moves` is the lit set, whatever is lighting it.** With nothing picked
    // it is where this character may walk; with a pick in hand it is where that
    // pick can land, from `sim::aimable_tiles`. One list, because a lit tile
    // means one thing either way: where the next confirm goes. A second list
    // would also be a second allocation on a machine that has run out of
    // contiguous heap on tens of bytes. `aiming` below is what says which of
    // the two a frame is showing, and so which colour it is drawn in.
    const std::vector<sim::Position>* moves{nullptr};
    // Where standing would be dangerous, already narrowed to the lit set. Only
    // ever non-empty when the lit set is somewhere this character could be
    // standing: its movement range, or the walk aimed at one of them.
    const std::vector<sim::Position>* danger{nullptr};
    // The tiles an area cast under the cursor would cover, or nullptr when the
    // pick is not one. Drawn as the cursor being that size rather than in a
    // colour of its own. The cursor already means "where the next confirm
    // lands", an area cast lands on more than one tile, and a fourth overlay
    // colour is one a palette-bound console may have no entry to spare for.
    const std::vector<sim::Position>* splash{nullptr};
    // A line of text under the board: a refusal the engine gave, or the price
    // of the strike the cursor is resting on. Empty when there is nothing to
    // say.
    const char* message{nullptr};
    // The menu on screen, or nullptr when none is. Both menus this client has
    // are drawn the same way and by the same renderer; `board_menu` is what
    // says which of the two a frame is showing, because a checkpoint that could
    // not tell them apart could not say either was right.
    const MenuChoice* menu{nullptr};
    int menu_rows{0};
    int menu_row{0};
    // Whether that menu is the board's rather than a character's: the way back,
    // the end of the whole side's turn, and the way out. It holds no row that
    // commands a character, and a character's menu holds no row of its.
    bool board_menu{false};
    // The full information sheet the menu's INFO row opened, or nullptr when
    // it is not on screen. It is a screen and not a panel: ten lines of
    // forty characters is the whole of this display, so the renderer draws it
    // over everything and puts no sprite in front of it.
    const grandleon::sheet::UnitSheet* sheet{nullptr};
    // What the player picked out of the menu and is now pointing somewhere, or
    // nullptr when nothing is waiting on a tile. A player who cannot see what
    // the next press will do is a player who has lost the thread.
    const char* aiming{nullptr};
    // "ROUND 3 OF 7" on a board whose content says the battle is won by
    // outlasting a number of rounds, and nullptr on every board that says no
    // such thing. That is every board this client has been handed so far, and
    // is why nothing new is painted where nothing new is authored.
    const char* round{nullptr};
    // Who the cursor is resting on, and what kind of character they are.
    // Composed here rather than by whoever is drawing, so that a status bar and
    // a transcript cannot name the same character two things.
    //
    // Both nullptr when the cursor rests on nobody. `hovered_class` is
    // additionally nullptr when the package holds no class for that unit type,
    // so a renderer with a row to spare draws the row only when there is
    // something to put in it.
    const char* hovered_name{nullptr};
    const char* hovered_class{nullptr};
};

// Why the client settled where it did. The harness photographs a checkpoint and
// compares its facts; a settle with no reason is a cursor step across open
// ground and is not worth either.
enum class Reason : std::uint8_t {
    none = 0,
    // The board was drawn for the first time.
    open,
    // The cursor came to rest on, or left, a unit.
    hover,
    // A unit was selected, or the selection was dropped.
    select,
    // The unit action menu opened, closed, or moved its caret.
    menu,
    // The information sheet was opened out of the menu, or put down again.
    sheet,
    // The player picked a row that needs a tile, or put it down again.
    aiming,
    // The engine refused a command.
    refused,
    // The player's command was accepted and the board redrawn.
    acted,
    // The opposing side played itself and handed control back.
    opposing,
};

[[nodiscard]] const char* reason_name(Reason reason) noexcept;

// Whether a tile is one of the engine's answers. A renderer asks this per cell
// while it walks the board; the list is short and the board is smaller than the
// screen, so a linear scan is cheaper than the grid it would otherwise build.
[[nodiscard]] inline bool contains(
    const std::vector<sim::Position>* tiles, int x, int y
) noexcept {
    if (tiles == nullptr) return false;
    for (const sim::Position& tile : *tiles) {
        if (tile.x == x && tile.y == y) return true;
    }
    return false;
}

// The player-facing wording for a refusal. The engine names its errors for
// machines (`sim::error_name`); this names them for a person, and it is the
// Nintendo 64's table because a player who has seen one console's refusal
// should recognise the other's.
[[nodiscard]] const char* refusal_text(sim::CommandError error) noexcept;


#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
// ---------------------------------------------------------------------------
// The campaign's screens
//
// A campaign screen is not a board, and nothing about it wants the board's
// vocabulary: there is no camera, no projection, no sprite and no highlight.
// There are only lines of text and, on the ones that ask a question, a caret.
// So a screen is a page, composed here and placed by the platform, and the
// platform learns nothing about a company by drawing one.
//
// Thirty-eight columns, which is the forty this display holds in an eight-pixel
// font less the two the caret's own column costs: a page row and the row a
// caret can sit beside are the same row, so a line that could fill the screen
// would be a line a caret had to be drawn over. The page is a fixed block for
// the reason every other buffer in this client is one: every console here runs
// a hand-rolled allocator and prints a heap census beside the run, so a screen
// that allocated would be a screen moving the number the census reports. That
// number is the one that says whether the *content* path fits.
// ---------------------------------------------------------------------------
inline constexpr int page_columns = 38;

// Sixteen rows. The tallest page the shipped content produces is a member's
// verbs, at a name, a stat line, a blank and one row per verb; the aftermath is
// ten and the company nine, or eleven where the board caps its deployment and
// the last gesture was refused, which are the only two rows it can grow by.
//
// An authored dialogue longer than this is paged rather than truncated, and
// that holds at both scales: `present_dialogue` starts a speaker on a fresh
// page when the one in hand will not take them, and `push_story_line` holds
// and continues *inside* a speaker for the one thing that break cannot help
// with: a single saying taller than any page. So the number bounds a page and
// never a story, whatever an author writes.
//
// It is as small as it is because every byte of it is a byte of heap: this
// block lives in the campaign build's `.bss`, and on a console the heap is
// whatever `.bss` leaves.
inline constexpr int page_capacity = 16;

// How wide a footer this client will ever compose.
//
// A footer is not page text: it is drawn on its own row by the platform, so
// `push_line` never sees it and nothing cuts it to `page_columns`. What cuts it
// is the display, silently, in the middle of whichever word ran off the edge —
// and the word that runs off is the last one, which is where a footer puts the
// button a player has not met before. That is exactly how a hint for a new
// button comes to read `C S`.
//
// So the number is here and every platform asserts its own screen holds it, on
// the same terms `page_columns` is asserted, and every footer literal in
// `turn_client.cpp` is asserted against it. One character narrower than the
// widest display's row, because the narrowest platform indents the footer by
// one column to line it up with the page above.
inline constexpr int footer_columns = 39;

// How many rows `text` becomes once it is wrapped to the display, counting the
// rows a word wider than the page is broken across. `present_dialogue` asks
// this before it commits to a page, so that a speaker who would fit on a page
// of their own starts one; the pager itself does not depend on the answer,
// because a saying that fits on no page is held and continued rather than
// planned for. A text with no words in it wraps to no rows and is estimated at
// one: over-reserving breaks a page a row early, and under-reserving would put
// a speaker where they do not fit.
//
// Free rather than a member because it reads nothing but its argument, and
// because a test has to be able to ask the property that matters about it:
// that it counts exactly the rows `push_wrapped` lays down.
[[nodiscard]] int wrapped_rows(const char* text) noexcept;

struct TextPage final {
    char lines[page_capacity][page_columns + 1]{};
    int count{0};

    [[nodiscard]] const char* line(int index) const noexcept {
        if (index < 0 || index >= count) return "";
        return lines[index];
    }
};

// Which screen is up. Reported by name in the transcript, so a run that reached
// the aftermath where the host derived a company fails on the word.
enum class Screen : std::uint8_t {
    none = 0,
    // The mark, the campaign's name, and a button.
    title,
    // What the cartridge is holding, and the one choice made before a campaign
    // begins.
    slot,
    // An authored dialogue, wrapped to the display.
    story,
    // The company: the screen the campaign opens on, and the stage between
    // battles. The caret is on it only in the second case.
    company,
    // One member's verbs, over the company.
    member,
    // What a battle did to the campaign, as the campaign committed it.
    aftermath,
    // A slot the session could not honour, in the vocabulary of whichever layer
    // refused it.
    refusal,
    // Who an authored recruitment brought in.
    joined,
    // The Stages of the campaign, offered to somebody checking the game. Only
    // ever reachable in a build that carries the picker, so it is a screen
    // most games have no way to open.
    stages,
    // The end.
    ended,
};

[[nodiscard]] const char* screen_name(Screen screen) noexcept;

// ---------------------------------------------------------------------------
// The windows a company screen is made of
//
// Three lists, three fixed windows, and the numbers are the screen's rather
// than the content's. A roster is capped at five hundred and twelve by
// `schemas/source/v1/campaign.schema.json` and a store is capped by nothing at
// all, so a page whose height followed the content would be a page that could
// not be a fixed block. A fixed block is the only kind this machine can
// afford (`page_capacity` above says why).
//
// Seven roster rows because that is what the Nintendo 64 already draws between
// its heading and its store, and this screen is that screen. Four store rows
// and eight menu rows are what is left of sixteen once the headings, the blanks
// and the capacity line have taken theirs:
//
//   heading 1 + refusal 0..1 + blank 1 + roster 7 + capacity 0..1 + blank 1
//   + store heading 1 + store 4  =  16
//
//   name 1 + stat line 1 + blank 1 + verbs 8  =  11
//
// A list shorter than its window is drawn exactly as it was before there was a
// window, which is why Tarnholt's four-member company is unmoved by any of it.
// ---------------------------------------------------------------------------
inline constexpr int company_roster_rows = 7;
inline constexpr int company_store_rows = 4;
inline constexpr int member_menu_rows = 8;
// The Stage picker's window. Twelve of sixteen, which is what is left once its
// heading, the line saying what an unseen Stage costs, and a blank have taken
// theirs:
//
//   heading 1 + warning 1 + blank 1 + stages 12  =  15
//
// One row spare rather than none, because a campaign of exactly twelve Stages
// should not be the campaign that discovers the arithmetic was tight.
inline constexpr int stage_menu_rows = 12;

// How far the caret is kept from a window's edge where the list allows it. One
// row of context: the board's camera asks for two out of eleven, and a
// seven-row window cannot honour two on both sides at once.
inline constexpr int company_scroll_margin = 1;

// One row of a member's verb menu. Fixed storage for the reason `MenuChoice` is.
//
// Twenty rather than ten because a menu that dropped rows was the other half of
// the truncation this screen's window exists to end: the rows past the tenth
// were the store's and the member's own kit, which is to say the items a
// deferral recorded as unreachable. Twenty covers the availability row, nine
// store stacks, nine carried stacks and CANCEL, and the window shows eight of
// them at a time. What it costs is `.bss`, which on a console is heap.
inline constexpr int company_menu_capacity = 20;
inline constexpr int company_menu_label_size = 28;

struct CompanyChoice final {
    char label[company_menu_label_size]{};
    client::ManagementVerb verb{client::ManagementVerb::none};
    campaign::DefinitionRef item{};
};

// What the platform has to put on the screen, and what a front end that is not
// a person needs to know about it.
struct ScreenView final {
    Screen screen{Screen::none};
    const TextPage* page{nullptr};
    // Which item the caret is on, out of `items`. -1 on a screen with nothing
    // to choose. This is the *list's* index and not the page's: on a list
    // longer than its window the two differ, and a scripted front end asking
    // "how far is the row I want" means this one.
    int caret{-1};
    // Which page rows the visible part of the list occupies, and which of them
    // the caret is beside. `caret_row` is what a renderer places a `>` against;
    // it is -1 exactly when `caret` is. On a list that fits, `caret_row` is
    // `first_choice_row + caret`, which is what it always was.
    int first_choice_row{0};
    int choices{0};
    int caret_row{-1};
    // How long the list actually is, and where its window starts. Equal to
    // `choices` and zero on every list that fits, which is every list the
    // shipped content produces.
    int items{0};
    int window_top{0};
    const char* footer{nullptr};
    // The stage this screen is standing in. Null on every screen that is not
    // the management stage, and read by nothing that draws: it is here so that
    // a scripted front end can find the row it means without matching text.
    const client::CompanyManagement* company{nullptr};
    const CompanyChoice* verbs{nullptr};
    // What the scene on this page is set against, as the art library's
    // backdrop menu index plus one; 0 on every screen that is not a story
    // page and on a scene that names none. Presentation and nothing else: no
    // rule reads it, and a front end that ignores it draws the page it always
    // drew.
    std::uint8_t backdrop{0};
    // Who is speaking on this page, as the unit type the scene cast for them.
    // Null on every screen that is not a story page and on a saying whose
    // scene cast nobody, which is every scene authored before a cast existed.
    //
    // A pointer rather than a sentinel, for the reason
    // `Dialogue::speaker_unit_type` gives: a unit type identity is a hash and
    // there is no value of it that means "nobody" and could not also be some
    // project's character.
    //
    // A front end draws the character this names by asking the package what it
    // wears, which is what the board already asks to draw the same character
    // standing on it. Presentation and nothing else, on the same terms as the
    // backdrop above: no rule reads it, and a front end that ignores it draws
    // the page it always drew.
    const std::uint64_t* speaker{nullptr};
};
#endif

// ---------------------------------------------------------------------------
// The client
// ---------------------------------------------------------------------------
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
class TurnClient : public client::CampaignFrontEnd {
#else
class TurnClient : public client::Presenter {
#endif
public:
    explicit TurnClient(ReportSink& sink) noexcept : sink_(sink) {}

    // ----- what a platform supplies -------------------------------------
    //
    // Two functions, and between them they are the whole of what a console is
    // and a host is not.

    // Puts the board and the overlay on the screen. The host build draws
    // nothing and still walks every other line of this class, which is what
    // makes its transcript comparable.
    virtual void paint(const sim::EncounterSnapshot& snapshot, const Overlay& overlay) = 0;

    // Blocks until a button goes down, and returns every button that did.
    virtual std::uint16_t next_press() = 0;

    // Called once per settled checkpoint, before and after the facts, so a
    // platform that can photograph itself has somewhere to hold still. The
    // default does nothing, which is what a host build wants.
    virtual void hold_for_checkpoint() {}

    // ----- motion -------------------------------------------------------
    //
    // Three gestures, and between them everything this client animates. Each
    // is handed the state its event happened *out of*, plus everything the
    // drawing needs already worked out. Most of all the route, which is
    // planned here rather than on the console so that both builds of this file
    // agree about it and so the plan can be reasoned about off the hardware.
    //
    // The default bodies do nothing, which is exactly what the host build
    // wants: it draws no picture, so it has no motion to draw, and its
    // transcript is byte-for-byte what it was before any of this existed.
    // That is the whole reason adding motion needed no expectation re-derived.
    //
    // Every one of them must return with the board settled: a checkpoint
    // always follows a `paint`, and a `paint` always follows these.

    // One frame drawn while this client is moving the camera: the camera has
    // already been moved, and this draws the board where it now stands and
    // waits for the display.
    //
    // Two occasions call it and they are the same act. A board too wide for its
    // screen is revealed as it opens, and the camera follows the action when
    // something happens out of view - an enemy walking somewhere the player is
    // not looking is a turn of the game the player never sees. A board that fits
    // its screen has neither, and this is never called.
    //
    // The odd one out among the four, in that this client counts the frames and
    // the three gestures below let the platform count its own. The reason is
    // ownership rather than taste: those move a token, which is the platform's
    // own pixel state, and this moves `camera_`, which is this client's and is
    // reachable from a platform only through the const `camera()`. Handing out a
    // mutable camera to save a virtual call per frame would give every platform
    // the ability to scroll the board at any other moment too.
    //
    // Handed the snapshot and the overlay because this fires *before* the first
    // `paint` of a board rather than after an event, so a platform that draws
    // from the last painted overlay has not been given one yet. The reveal has
    // to come before the settled board, not a frame after it.
    virtual void camera_frame(
        const sim::EncounterSnapshot& snapshot, const Overlay& overlay
    ) {
        static_cast<void>(snapshot);
        static_cast<void>(overlay);
    }

    // Walks a token from where it stood to where it landed, along `route`:
    // `length` tiles, each a tile the engine's own reachability query returned.
    // An empty route means the route could not be planned inside that query's
    // tiles, and the platform draws the straight line instead.
    virtual void animate_move(
        const sim::EncounterSnapshot& before,
        sim::UnitId unit,
        const view::RouteTile* route,
        int length,
        sim::Position destination
    ) {
        static_cast<void>(before);
        static_cast<void>(unit);
        static_cast<void>(route);
        static_cast<void>(length);
        static_cast<void>(destination);
    }

    // A blow that landed: the struck token is knocked away from whoever swung
    // and stands again, and whoever swung is drawn coiled for exactly as long.
    // `toward_x`/`toward_y` point from the striker to the struck, and are both
    // zero when the blow came from nobody on the board; `striker` is zero for
    // the same reason, so a renderer that poses one never has to guess which.
    //
    // `gesture` and `separation` are what turn one picture into three. They are
    // *derived* here rather than reported by the engine (see `gesture_for`),
    // so that a renderer is handed the answer instead of four renderers each
    // working it out, and so that the host build that derives a console check's
    // expectations computes the very same answer the console draws.
    virtual void animate_hit(
        const sim::EncounterSnapshot& before,
        sim::UnitId struck,
        sim::UnitId striker,
        int toward_x,
        int toward_y,
        view::AttackGesture gesture,
        int separation
    ) {
        static_cast<void>(before);
        static_cast<void>(struck);
        static_cast<void>(striker);
        static_cast<void>(toward_x);
        static_cast<void>(toward_y);
        static_cast<void>(gesture);
        static_cast<void>(separation);
    }

    // A blow that did not land. Nothing is knocked anywhere, which is the
    // difference this gesture exists to say, but the blow was still thrown.
    // The striker the engine named is handed over here, on the same terms as a
    // landed blow, so that a renderer can draw the throw as well as the miss.
    virtual void animate_miss(
        const sim::EncounterSnapshot& before,
        sim::Position cell,
        sim::UnitId struck,
        sim::UnitId striker,
        view::AttackGesture gesture,
        int separation
    ) {
        static_cast<void>(before);
        static_cast<void>(cell);
        static_cast<void>(struck);
        static_cast<void>(striker);
        static_cast<void>(gesture);
        static_cast<void>(separation);
    }

    // Called at every checkpoint once the facts are out. The ROM answers with
    // its pixel claims: where on the 320x224 display it drew what it just said,
    // and in which colour. The host build says nothing, because it drew nothing
    // to claim about.
    virtual void after_facts(
        const sim::EncounterSnapshot& snapshot, const Overlay& overlay
    ) {
        static_cast<void>(snapshot);
        static_cast<void>(overlay);
    }

    // ----- the Presenter seam -------------------------------------------
    void present_dialogue(const package_runtime::Dialogue& dialogue) override;
    void battle_begins(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster& roster,
        sim::Side player_side,
        const std::vector<std::uint64_t>& terrain
    ) override;
    void battle_moments(
        const std::vector<package_runtime::EncounterMoment>& moments,
        const std::vector<package_runtime::PlacementIdentity>& placements
    ) override;
    void battle_definitions(
        const std::vector<sim::WeaponDefinition>& weapons,
        const std::vector<sim::AbilityDefinition>& abilities,
        const std::vector<sim::ItemDefinition>& items,
        const std::vector<sim::ObjectiveDefinition>& objectives
    ) override;
    void draw(
        const sim::EncounterSnapshot& snapshot, const client::Roster& roster
    ) override;
    void report(
        const sim::CommandResult& result, const client::Roster& roster
    ) override;
    void refused(sim::CommandError error) override;
    void show_state(
        const sim::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash,
        const std::vector<sim::ObjectiveDefinition>& objectives
    ) override;
    void battle_ended(
        const sim::EncounterSnapshot& snapshot, std::uint64_t canonical_hash
    ) override;
    void campaign_ended() override;
    [[nodiscard]] client::Intent next_intent(
        const sim::EncounterSnapshot& snapshot, const client::Roster& roster
    ) override;
    void deployment_begins(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster& roster,
        const std::vector<sim::Position>& zone
    ) override;
    [[nodiscard]] client::Intent next_deployment_intent(
        const sim::EncounterSnapshot& snapshot, const client::Roster& roster
    ) override;

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // ----- what a campaign build's platform supplies ---------------------

    // Puts a campaign screen on the display. The host build draws nothing and
    // still composes every line, which is what makes its transcript comparable.
    virtual void paint_screen(const ScreenView& view) = 0;

    // Called at every screen checkpoint once the facts are out, on the same
    // terms `after_facts` is called for a board.
    virtual void after_screen(const ScreenView& view) { static_cast<void>(view); }

    // ----- the campaign, before the session ------------------------------

    // What a player chose before a campaign begins: which of the cartridge's
    // slots the run reads and writes, and whether it is resuming what is in it
    // or founding over it.
    struct SlotChoice final {
        // The slot's name, borrowed from the menu, which outlives the call
        // because it is a member of this client.
        const char* slot{""};
        bool resume{false};
    };

    // The mark, and then the choice a player makes before a campaign begins:
    // one row per slot the cartridge reserves, `holds[n]` saying whether the
    // device answers for slot *n*.
    //
    // CONTINUE is offered for a slot that answers and not for one that does
    // not. That is the rule "a CONTINUE that does nothing is worse than no
    // CONTINUE at all", applied per row rather than per cartridge. It is the
    // whole of how this ROM decides whether it is founding or resuming: the
    // device answers, and no press can make it say otherwise.
    //
    // Here rather than in the ROM because the host derives what this screen
    // says, and a screen only one of the two machines composes is a screen
    // nothing checks. What the rows say and what the buttons do is
    // `view::SlotMenu`, which both consoles render.
    //
    // `project_title` is the name of the game on the cartridge, which the
    // platform reads off the package it is holding
    // (`grandleon/package_runtime/manifest.hpp`). It is a parameter rather than
    // something this client could look up, because `grandleon_client` does not
    // know what a package is. It is a parameter of *this* call rather than a
    // setter, so a platform cannot open a campaign without saying whose it is.
    // It is kept for the ending screen, which names the game again.
    //
    // The view is copied on the way in and need not outlive the call, and it
    // need not be NUL-terminated: the package holds its title as a counted
    // string in bytes the console reads in place.
    [[nodiscard]] SlotChoice open_campaign(
        std::string_view project_title,
        const char* slot_base,
        const bool* holds,
        int slots
    );

    // ----- the CampaignNarrator seam -------------------------------------
    void campaign_begun(
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        std::string_view slot,
        bool resumed
    ) override;
    void slot_refused(const client::SlotFailure& failure) override;
    void board_prepared(const client::CampaignBoard& board) override;
    void battle_aftermath(const client::BattleAftermath& aftermath) override;
    void members_joined(
        const std::vector<client::RosterEntry>& joined
    ) override;
    void campaign_saved(
        std::string_view slot, storage::StorageError error
    ) override;
    void management_opened(const client::CompanyManagement& company) override;
    void management_committed(const client::ManagementCommit& result) override;
    void stage_jumped(const client::StageJump& jump) override;
    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override;

    // How many campaign screens the run has settled on, and whether the
    // campaign this client narrated was read back off a device.
    [[nodiscard]] int screens() const noexcept { return screens_; }
    [[nodiscard]] bool resumed() const noexcept { return resumed_; }
    [[nodiscard]] int saves() const noexcept { return saves_; }
    [[nodiscard]] int save_failures() const noexcept { return save_failures_; }
    [[nodiscard]] int commits() const noexcept { return commits_; }
    [[nodiscard]] int battles() const noexcept { return battles_; }
    // Evidence that the campaign has a history, counted where the session hands
    // it over: a store with anything in it, or a member who is not simply
    // available. A founding can produce neither.
    [[nodiscard]] int history() const noexcept { return history_; }
#endif

    // ----- what a platform reads ----------------------------------------
    [[nodiscard]] const view::Camera& camera() const noexcept { return camera_; }

    // Moves the camera until `where` is on the screen, drawing the frames it
    // takes through `camera_frame`. Does nothing at all when it is already
    // there, which is the common case: a board that fits has no elsewhere to be.
    //
    // Protected rather than private because it is the one camera gesture a
    // front end could reasonably need to ask for; `camera_` itself stays out of
    // reach, so nobody can move the board without drawing the movement.
    void bring_into_view(sim::Position where);
    [[nodiscard]] const std::vector<std::uint64_t>& terrain() const noexcept {
        return terrain_;
    }
    [[nodiscard]] int checkpoints() const noexcept { return checkpoints_; }
    [[nodiscard]] int checks() const noexcept { return checks_; }
    [[nodiscard]] int failures() const noexcept { return failures_; }
    [[nodiscard]] sim::Side player_side() const noexcept { return player_side_; }
    // The package the platform opened, for a renderer composing a line of its
    // own. Null on a client that has none.
    [[nodiscard]] const grandleon::package_format::LoadedPackage* package()
        const noexcept {
        return package_;
    }

    // Handed the package before the first battle, for the same reason the
    // viewport is handed in: what an author called a character is a fact about
    // the cartridge rather than about the client, and every name this client
    // draws is asked of it first.
    void set_package(
        const grandleon::package_format::LoadedPackage* package
    ) noexcept {
        package_ = package;
    }

    // How wide the viewport is, in cells. Set by the platform before the first
    // battle, because how much of a board fits is a property of the screen.
    //
    // A platform that sets a fit rule instead does not call this: the window
    // is then a property of the board as well as of the screen, and is worked
    // out per battle.
    void set_viewport(int cells_wide, int cells_high) noexcept {
        viewport_w_ = cells_wide;
        viewport_h_ = cells_high;
    }

    // Fit each board to the screen rather than showing a fixed window of it.
    //
    // The cell size and the window then both fall out of `view::fit_board`
    // once the board's size is known, and `tile()` reports what this battle
    // was given. A platform that sets one is saying its renderer can draw a
    // cell at whatever size comes back.
    //
    // This lives here, rather than in each platform, for the reason the window
    // always lived here: the camera scrolls when the cursor reaches the edge,
    // so a host derivation that worked the window out differently from the
    // console would put the camera somewhere the console never puts it, and
    // every coordinate downstream would differ. One rule, applied in one
    // place, cannot disagree with itself.
    void set_fit_rule(const view::FitRule& rule) noexcept { fit_ = rule; }

    // The cell size this battle is drawn at, in pixels, or zero where no fit
    // rule was set and the platform is drawing at a size of its own choosing.
    [[nodiscard]] int tile() const noexcept { return tile_; }

protected:
    // A check the client makes about itself, reported in the format the harness
    // already understands from the render ROM.
    void expect(bool condition, const char* name);

private:
    void settle(const sim::EncounterSnapshot& snapshot);
    void checkpoint(const sim::EncounterSnapshot& snapshot, Reason reason);
    void refresh_queries(const sim::EncounterSnapshot& snapshot);
    void build_overlay(const sim::EncounterSnapshot& snapshot);
    void press(const sim::EncounterSnapshot& snapshot, std::uint16_t buttons);
    // The same thumb, a different phase. A is pick-up and put-down, B puts a
    // character back down without moving it, Start opens the battle. Start is
    // also the button that ends an activation once one has begun, and it ends
    // the arranging for the same reason: it is this machine's "I am done".
    void deploy_press(
        const sim::EncounterSnapshot& snapshot, std::uint16_t buttons
    );
    // The snapshot is here for the TALK row and nothing else: whether one is
    // offered is a question about who is standing next to this character, and
    // the engine is what answers it.
    void open_menu(
        const sim::EncounterSnapshot& snapshot, const sim::UnitSnapshot& actor
    );
    // The board's own menu, on START, at any point in a battle and whether or
    // not a character is selected. Three rows, none of which commands a
    // character: the way back to the battle, the end of this side's whole turn,
    // and the way out of the board.
    void open_board_menu();
    // Whether the row the caret is on was taken out of the board menu, and what
    // taking it means. Split from `commit_menu_row` because the two menus share
    // a caret and a renderer and share nothing else: a row that ends a side's
    // turn and a row that ends a character's are different commands, and a
    // single switch over both would be the one place they could be confused.
    void commit_board_row(const sim::EncounterSnapshot& snapshot);
    // The next `wait` the side still owes the board while END <SIDE> TURN is
    // being honoured, or nothing left to owe. `client::unfinished_unit` is what
    // answers; this is only the flag around it and the guard that stops a side
    // the drain cannot finish from being drained for ever.
    void drain_side(const sim::EncounterSnapshot& snapshot);
    // Whether anybody standing beside `actor` would answer a talk from it.
    // Every candidate is put to `sim::forecast_talk`, which is the judgement
    // `apply` makes, so the row on screen and the command it sends cannot
    // disagree.
    [[nodiscard]] bool any_talkable_neighbour(
        const sim::EncounterSnapshot& snapshot, const sim::UnitSnapshot& actor
    ) const noexcept;
    void commit_menu_row(const sim::EncounterSnapshot& snapshot);
    void commit_aim(const sim::EncounterSnapshot& snapshot);
    void clear_aim() noexcept;
    // The pick this client is holding, said in the engine's words, so the
    // engine rather than this client decides what it can reach.
    [[nodiscard]] sim::AimedGesture aimed_gesture() const noexcept;
    void write_aim_text(const char* prefix, const char* label) noexcept;
    void write_message(const char* prefix, const char* label) noexcept;

    [[nodiscard]] const sim::UnitSnapshot* unit_at(
        const sim::EncounterSnapshot& snapshot, int x, int y
    ) const noexcept;
    [[nodiscard]] const sim::UnitSnapshot* unit_by_id(
        const sim::EncounterSnapshot& snapshot, sim::UnitId id
    ) const noexcept;
    // How far apart two tokens stand, on the engine's own metric: orthogonal
    // steps, `|dx| + |dy|`. A reach band read out of a record here therefore
    // means what the same band means when the engine tests it. Zero when either
    // end is missing, which is a blow from nobody and draws as one.
    [[nodiscard]] static int separation_between(
        const sim::UnitSnapshot* lhs, const sim::UnitSnapshot* rhs
    ) noexcept;

    // What to call the character standing in one board unit.
    //
    // `sheet::character_name` decides, and every client asks it, so a name is
    // one answer rather than one per machine. All this adds is the piece only a
    // client can supply: the campaign's own name for a member, through the join
    // the session published, when this build has a campaign in it at all.
    //
    // Never a number. A name is what a player recognises, and a board
    // identifier is encounter-local and means nothing the next map.
    [[nodiscard]] grandleon::sheet::ContentName character_called(
        const sim::EncounterSnapshot& snapshot, sim::UnitId unit
    ) const noexcept;

    // What this board's campaign calls this character going down. A death under
    // the permanent rule and a fall under the recoverable one: the same event,
    // two vocabularies. Which one is in force is a fact about the campaign
    // rather than a taste of this client's. Somebody the company does not hold
    // is always a death, whatever rule the company plays under.
    [[nodiscard]] const char* fall_word(sim::UnitId unit) const noexcept;

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // ----- the campaign's own half ---------------------------------------

    // Puts a page on the display, reports it, and holds it until a button that
    // means "go on". Used by every screen that asks nothing.
    void hold_page(Screen screen, const char* footer);
    // Puts a page with a caret on the display and drives it, returning the row
    // that was taken or -1 when the player backed out. `back` is the button
    // mask that backs out, which is nothing on a screen there is no way out of.
    [[nodiscard]] int choose_on_page(
        Screen screen, const char* footer, std::uint16_t back
    );
    void screen_checkpoint(const ScreenView& view);
    [[nodiscard]] ScreenView view_of(Screen screen, const char* footer) const;

    // Page composition. Every one of these reads what the session handed over
    // and nothing else.
    void begin_page();
    // One row onto the page. `length` bounds how much of `text` is read, and
    // -1 means "to the terminator": the wrapper hands slices of the author's
    // own bytes rather than copies of them, because a copy needs a buffer and
    // a buffer needs a capacity an authored word is not obliged to respect.
    void push_line(const char* text, int length);
    void push_line(const char* text);
    // One row of a story page, and a fresh page under it when this one is
    // full. This is what makes `page_capacity` bound a page and never a story.
    void push_story_line(const char* text, int length, std::uint8_t backdrop);
    // A speaker and what they said, wrapped to the display and paged when the
    // saying is longer than a page. `backdrop` is the scene's, restored onto
    // every page this spills onto, because `begin_page` clears it.
    void push_wrapped(
        const char* speaker, const char* text, std::uint8_t backdrop
    );
    // A fact about the campaign, held until the next screen's checkpoint opens.
    // Kept rather than printed where it happens because a compared line outside
    // a checkpoint block belongs to no block, and a line belonging to no block
    // is a line whose disagreement nothing reports.
    void note(const char* text);
    // The company as a page. `company` is the management stage's own view of
    // it and is null on the screen the campaign opens on, which has no next
    // board to count against and nothing to refuse: what it adds is the
    // capacity line and a pending refusal, and neither is a thing the founding
    // screen has.
    void compose_company(
        const char* heading,
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        const client::CompanyManagement* company = nullptr
    );
    void compose_member_menu(const client::CompanyManagement& company, int row);
    // The campaign's Stages as a page, one row each, marked with where the
    // campaign is and where it has been. `stages_` is what it reads and is
    // borrowed from whichever surface opened the picker.
    void compose_stages();
    // Puts the picker on the display and returns the campaign node the player
    // chose, or zero when they backed out. Shared by the two surfaces that
    // offer it — the board menu inside a battle and the company screen between
    // them — because it is one list and one choice, and two copies of a screen
    // is how two screens come to say different things about one campaign.
    [[nodiscard]] std::uint64_t choose_a_stage();
    // The slot screen as a page. Recomposed on every press, because arming a
    // row rewrites it.
    void compose_slot_page();

    // Which composer built the page the caret is walking, so that moving the
    // window can rebuild it. A page whose list fits never needs this, and a
    // page with no list at all is `none`.
    //
    // An enum and a switch rather than a stored callable: this client lives in
    // a `.bss` block on a machine whose global constructors do not run, and a
    // `std::function` would be a heap allocation per screen on a machine that
    // counts them.
    enum class ListPage : std::uint8_t { none, company, member, stages };
    void recompose_page();

    TextPage page_{};
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // The name of the game on the cartridge, as `open_campaign` was told it.
    //
    // Held raw and normalised on the way out: `push_line` already folds case,
    // replaces every byte outside the font's ASCII 0x20 to 0x5F and cuts the
    // line to the page, so a second copy of those three rules here would be a
    // second place for them to drift. A page wide enough is all the storage a
    // line of it can use.
    //
    // Empty until a campaign is opened, and drawn as nothing when it is. That
    // is the honest reading of "no game said its name" and, since
    // `open_campaign` cannot be called without one, is a state only a build
    // that reached an ending without a beginning can be in.
    char project_title_[page_columns + 1]{};
#endif
    // What the scene composed onto this page is set against, or 0 for every
    // page that is not a scene. Cleared by `begin_page`, so a screen has to
    // say it has a backdrop rather than inherit the last one's.
    std::uint8_t page_backdrop_{0};
    // Who is speaking on this page, as the unit type the scene cast for them.
    // Cleared by `begin_page` for the same reason the backdrop is: a screen
    // says who is on it rather than inheriting the last one's speaker.
    //
    // A flag beside the value rather than a reserved number, because a unit
    // type identity is a hash and no value of it means "nobody" — the same
    // reason `Dialogue::speaker_unit_type` hands back a pointer.
    std::uint64_t page_speaker_{0};
    bool page_has_speaker_{false};
    // The saying being laid down, which outlives the page it started on: a
    // saying taller than a page is continued onto the next by
    // `push_story_line`, and the speaker has to survive the `begin_page` that
    // does it, exactly as the backdrop does.
    std::uint64_t story_speaker_{0};
    bool story_has_speaker_{false};
    // Where the visible part of the list starts on the page, and how many rows
    // of it are drawn. `page_choices_` is the window's height, never the list's
    // length, which `list_` holds.
    int page_first_choice_{0};
    int page_choices_{0};
    // The window over the list the caret is walking, rebuilt by every
    // composer out of the list it was handed.
    view::ListWindow list_{};
    // Which item the caret is on and where the window starts. These two outlive
    // `begin_page`, because the thing that rebuilds a page is the caret moving
    // the window, and a rebuild that reset them would undo the move that caused
    // it. Whoever opens a *new* list puts them back.
    int list_caret_{0};
    int list_top_{0};
    ListPage list_page_{ListPage::none};
    // The heading the company page was composed under, so a rebuild says the
    // same thing. A borrowed literal, for the reason `pending_refusal_` is.
    const char* company_heading_{nullptr};
    // The screen a player chose a slot on. A member rather than a local because
    // the name it composed is borrowed by the session for the whole run.
    view::SlotMenu slots_{};
    int screens_{0};
    // Which of the buttons that back out of a screen was the one pressed, so a
    // caller can tell "I am done here" from "take me back".
    std::uint16_t back_pressed_{0};
    // Facts waiting for the next checkpoint to carry them.
    static constexpr int note_capacity = 4;
    static constexpr int note_size = 64;
    char notes_[note_capacity][note_size]{};
    int note_count_{0};
    // The stage the current screen belongs to, for `ScreenView`.
    const client::CompanyManagement* company_{nullptr};
    CompanyChoice verbs_[company_menu_capacity]{};
    int verb_count_{0};
    // Which member row the management caret is on, and where the roster's
    // window was standing, kept across gestures so a player who gave something
    // is still standing in front of the same person on the same page.
    int manage_row_{0};
    int roster_top_{0};
    // A refusal this screen decided, waiting for the company page that says it.
    // A borrowed pointer into the engine's own `roster_error_name` table, which
    // is a table of literals in cartridge ROM: copying the sentence into work
    // RAM would be a second spelling of a word only the engine gets to spell.
    // Cleared by the page that shows it, as the Nintendo 64's save toast is.
    const char* pending_refusal_{nullptr};
    // The board being fought, as the session published it: the roster, and the
    // join that says which numbered unit each member is standing in. Borrowed
    // rather than copied, for the reason `pending_refusal_` is borrowed: this
    // client keeps no allocation of its own while a battle is on screen.
    //
    // Its lifetime is exactly the battle it describes. The driver holds the
    // prepared board across the fight and hands it here before the first frame;
    // the next board replaces it through the same call, and the aftermath ends
    // it. Nothing outside that window reads it, and `battle_aftermath` clears it
    // so that nothing can.
    const client::CampaignBoard* board_{nullptr};
    // The Stages the picker is offering, borrowed from whichever surface opened
    // it: the board's list inside a battle, the company's between them. Null
    // whenever the picker is not on screen, and null for the whole of every game
    // whose author did not ask for it, because both of those lists are empty
    // then and this client offers the row only when the list it was handed is
    // not. Nothing here reads a setting.
    const std::vector<client::CampaignStage>* stages_{nullptr};
    bool resumed_{false};
    bool leaving_{false};
    int saves_{0};
    int save_failures_{0};
    int commits_{0};
    int battles_{0};
    int history_{0};
#endif

    ReportSink& sink_;

    sim::Side player_side_{sim::Side::first};
    std::vector<std::uint64_t> terrain_;
    std::vector<sim::WeaponDefinition> weapons_;
    std::vector<sim::AbilityDefinition> abilities_;
    std::vector<sim::ItemDefinition> items_;
    // How many rounds this board is won by surviving; zero when nothing on it
    // is, which is every board this client has been handed so far.
    std::uint32_t rounds_to_survive_{0};
    // The line `Overlay::round` points at, held here for the reason
    // `message_` is: a fixed buffer, because this client allocates nothing
    // while a battle is on screen.
    char round_line_[24]{};
    // The two lines `Overlay::hovered_name` and `Overlay::hovered_class` point
    // at, on the same terms and for the same reason. One more than
    // `sheet::content_name_capacity`, which is the whole of what either can
    // hold, plus the terminator.
    char hovered_name_[grandleon::sheet::content_name_capacity + 1]{};
    char hovered_class_[grandleon::sheet::content_name_capacity + 1]{};

    view::Camera camera_{};
    int viewport_w_{10};
    int viewport_h_{7};
    // Zero frame width means no rule, which is what keeps every platform that
    // sets a viewport outright working exactly as it did.
    view::FitRule fit_{};
    int tile_{0};

    std::int16_t cursor_x_{0};
    std::int16_t cursor_y_{0};
    sim::UnitId selected_{0};
    // Whether the board is being arranged rather than fought. The engine is
    // the authority; this is only what the buttons are asking for.
    bool deploying_{false};

    MenuChoice menu_[menu_capacity]{};
    char item_labels_[menu_item_labels][menu_item_label_size]{};
    int menu_rows_{0};
    int menu_row_{0};
    bool menu_open_{false};
    // Whether the open menu is the board's rather than a character's.
    bool board_menu_open_{false};
    // Whether the player took END <SIDE> TURN and the side is still being
    // drained of the activations it had not spent, and who the drain waited
    // last. The same answer twice ends it rather than sending the same command
    // for ever: under the alternating order nothing marks a character as having
    // acted, and a refusal changes nothing, so in both cases the next answer is
    // the answer just given.
    bool finishing_{false};
    sim::UnitId drain_last_{0};
    // The character whose menu opens again once the engine has taken its walk,
    // or zero. A walk that leaves a turn open is a character the player is
    // about to be asked about again, and being handed a bare board instead is
    // the half of the cartridge report this answers.
    sim::UnitId reopen_menu_{0};

    // The package this platform is holding, or null. It is the first place
    // every name on screen is looked up: a class, a weapon, a spell, a draught.
    // The author's own word is in it. Borrowed: it outlives the client, which
    // is true on every platform here, where the package is opened once in main
    // and the client is built beside it.
    const grandleon::package_format::LoadedPackage* package_{nullptr};

    // The information sheet, composed by `grandleon::sheet` when the INFO row
    // is taken and held while it is on screen. The menu stays open underneath:
    // reading a character's numbers is not leaving the menu, and putting the
    // sheet down comes back to the row that opened it.
    grandleon::sheet::UnitSheet sheet_{};
    bool sheet_open_{false};

    // What the menu handed back, waiting for a tile, and the prompt that says
    // so. A fixed buffer for the same reason `message_` below is one.
    Aim aim_{Aim::none};
    sim::ContentId aim_weapon_{0};
    sim::ContentId aim_ability_{0};
    char aim_text_[40]{};

    // The engine's answers for the current selection and pick, and what they
    // were asked about, so they are asked for again only when the answer could
    // differ. The pick is part of the key because it is part of the answer: a
    // cache keyed on the selection alone would serve a movement range to a
    // strike taken between two presses.
    std::vector<sim::Position> moves_;
    std::vector<sim::Position> danger_;
    // The splash of an area cast under the cursor. Outside the cache, because
    // it is the one answer that moves with the cursor rather than with the
    // selection; its storage is handed back on every settle that does not need
    // it, for the reason `release` states.
    std::vector<sim::Position> splash_;
    sim::UnitId queried_for_{0};
    std::uint64_t queried_at_{~std::uint64_t{0}};
    Aim queried_aim_{Aim::none};
    sim::ContentId queried_identity_{0};

    // The message under the board, and how it was produced. A fixed buffer
    // because a report line that allocated would perturb the heap census.
    char message_[48]{};

    Overlay overlay_{};

    // What the presses so far have added up to. `next_intent` returns it as
    // soon as it is something, which is how a gesture spread over several
    // presses becomes one command.
    client::Intent pending_{};

    // The last state the client settled on, so a settle knows whether anything
    // a person would notice has changed.
    sim::UnitId last_hovered_{0};
    sim::UnitId last_selected_{0};
    bool last_menu_open_{false};
    int last_menu_row_{0};
    bool last_sheet_open_{false};
    Aim last_aim_{Aim::none};
    bool opened_{false};

    // What is said while this battle is on, and the join that turns the unit an
    // event names into the placement a moment is about. Both empty for a battle
    // nobody speaks during, which is every battle authored before moments.
    std::vector<package_runtime::EncounterMoment> moments_;
    std::vector<package_runtime::PlacementIdentity> moment_placements_;
    // Whether the scene a board opens with has been played. A board is drawn
    // many times and opens once.
    bool opening_moments_played_{false};

    // Plays every moment of one occasion, in the order they were authored.
    // `about` is the placement a character-shaped occasion is about, and zero
    // for the board's own.
    void play_moments(
        package_runtime::MomentTrigger when, std::uint64_t about
    );

    // The placement identity behind a battle-local unit, or zero when this
    // board's identities were never handed over.
    [[nodiscard]] std::uint64_t placement_of(sim::UnitId unit) const;

    // The opening sweep this board asked for, planned when the board opened and
    // spent on the frame it first becomes visible. Zero frames is a board with
    // nothing to reveal, which is every board that fits its screen.
    int sweep_from_{0};
    int sweep_to_{0};
    int sweep_frames_{0};

    // The last snapshot the client settled on. A refusal arrives without one:
    // the engine rejected the command, so there is no new state. The client
    // still has to repaint and report where the board actually is.
    sim::EncounterSnapshot last_snapshot_{};

    // Plans the route a slide is drawn along and answers how many tiles it
    // holds; the route itself is `route_tiles()`.
    //
    // Its scratch is deliberately not a member. This client is a local in the
    // turn ROM's `main`, running on a stack this machine sizes and measures to
    // the byte, and six hundred bytes of animation scratch in a stack frame is
    // six hundred bytes the deepest call cannot have. It lives in `.bss`
    // beside the file's other one-per-ROM tables instead, where it costs heap
    // that is counted rather than headroom that is not.
    [[nodiscard]] int plan_move_route(sim::UnitId unit, sim::Position destination);

    // Which gesture a blow between these two units is drawn as, folded over the
    // ability records `battle_definitions` handed this client before the first
    // frame. Nothing the engine reports says whether a weapon or a spell threw
    // the blow, and nothing is going to; this asks the one question a presenter
    // can answer from what it holds, and answers it the same way for the
    // player's blows and for the opponent's. The question is whether a damaging
    // magical ability the striker knows could have crossed this separation.
    [[nodiscard]] view::AttackGesture gesture_for(
        const sim::UnitSnapshot* striker, int separation
    ) const;
    [[nodiscard]] static const view::RouteTile* route_tiles();

    int checkpoints_{0};
    int checks_{0};
    int failures_{0};
};

}  // namespace grandleon::client::turn

#endif  // GRANDLEON_PLATFORM_CLIENT_TURN_CLIENT_HPP
