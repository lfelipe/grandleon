// SPDX-License-Identifier: MIT
#include <grandleon/client/turn_client.hpp>

#include <grandleon/client/attack_gesture.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/sheet/unit_sheet.hpp>

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
#include <grandleon/campaign/state.hpp>
#include <grandleon/storage/slot_storage.hpp>
#endif

#include <cstddef>

namespace grandleon::client::turn {
namespace sheet = grandleon::sheet;

namespace {

// A fixed-capacity line builder, written here rather than reached for from the
// C library, because the transcript this file emits is compared byte for byte
// between a console and a host. `snprintf` would be two implementations of the
// same format; this is one.
class Text final {
public:
    Text& text(const char* value) noexcept {
        if (value == nullptr) return *this;
        while (*value != '\0') push(*value++);
        return *this;
    }

    Text& number(std::int32_t value) noexcept {
        if (value < 0) {
            push('-');
            // Negated in unsigned arithmetic. The most negative `int32_t` has
            // no positive counterpart, so `-value` on it is signed overflow:
            // undefined, and on a console a wrong transcript line rather than
            // a caught one. Every other value comes out identical either way.
            return unsigned_number(0U - static_cast<std::uint32_t>(value));
        }
        return unsigned_number(static_cast<std::uint32_t>(value));
    }

    Text& space() noexcept { return push(' '); }

    [[nodiscard]] const char* c_str() const noexcept { return buffer_; }

private:
    Text& push(char value) noexcept {
        if (length_ + 1 < capacity) {
            buffer_[length_++] = value;
            buffer_[length_] = '\0';
        }
        return *this;
    }

    Text& unsigned_number(std::uint32_t value) noexcept {
        char digits[12];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U && count < 12);
        while (count > 0) push(digits[--count]);
        return *this;
    }

    static constexpr std::size_t capacity = 160;
    char buffer_[capacity]{'\0'};
    std::size_t length_{0};
};

// Drops from `threat` every tile that is not in `reach`, in place.
//
// **In place, and that is not a nicety.** The threat list is the biggest thing
// this client holds, most of the cells on a wide board, and a second one beside
// it is what takes a campaign build out of memory in the middle of a run.
// The list only ever shrinks here, so the storage it already has is the
// storage the answer needs.
//
// Quadratic in two short lists and deliberately so: both are tiles around one
// board, a console has no hash table worth the code it would cost, and this
// runs once per selection rather than once per frame.
// Hands a query's storage back before the next query asks for its own.
//
// A move-assignment frees the old buffer only *after* the new one has been
// built, so for the width of that statement the client is holding two whole
// danger lists. On a console that counts its heap in kilobytes that is the
// difference between an allocation and an out-of-memory abort. The list is the
// largest thing this client holds, the heap is fragmented by thousands of
// allocations by the time a battle is under way, and what runs out is the
// *largest free block* rather than the free total. Freeing first costs a query
// that was going to be recomputed anyway.
void release(std::vector<sim::Position>& tiles) {
    std::vector<sim::Position> empty;
    tiles.swap(empty);
}

void keep_only_reachable(
    std::vector<sim::Position>& threat,
    const std::vector<sim::Position>& reach
) {
    std::size_t kept = 0;
    for (std::size_t i = 0; i < threat.size(); ++i) {
        bool within = false;
        for (const sim::Position& tile : reach) {
            if (tile == threat[i]) {
                within = true;
                break;
            }
        }
        if (within) threat[kept++] = threat[i];
    }
    threat.resize(kept);
}

// One line into a fixed buffer, truncated rather than overrunning. Three sites
// wrote this loop out; the fourth is what turned it into a function.
void copy_line(char* out, std::size_t capacity, const char* text) noexcept {
    if (out == nullptr || capacity == 0U) return;
    std::size_t i = 0;
    while (text != nullptr && text[i] != '\0' && i + 1U < capacity) {
        out[i] = text[i];
        ++i;
    }
    out[i] = '\0';
}

}  // namespace

const char* reason_name(Reason reason) noexcept {
    switch (reason) {
        case Reason::none: return "none";
        case Reason::open: return "open";
        case Reason::hover: return "hover";
        case Reason::select: return "select";
        case Reason::menu: return "menu";
        case Reason::sheet: return "sheet";
        case Reason::aiming: return "aiming";
        case Reason::refused: return "refused";
        case Reason::acted: return "acted";
        case Reason::opposing: return "opposing";
    }
    return "none";
}

const char* refusal_text(sim::CommandError error) noexcept {
    switch (error) {
        case sim::CommandError::none: return "";
        case sim::CommandError::encounter_complete: return "THE BATTLE IS OVER";
        case sim::CommandError::unknown_unit: return "NO SUCH UNIT";
        case sim::CommandError::defeated_unit: return "THAT UNIT IS DOWN";
        case sim::CommandError::wrong_side: return "NOT YOUR UNIT";
        case sim::CommandError::invalid_command: return "CANNOT DO THAT";
        case sim::CommandError::invalid_destination: return "CANNOT MOVE THERE";
        case sim::CommandError::occupied_destination: return "THAT TILE IS TAKEN";
        case sim::CommandError::unknown_target: return "NO TARGET THERE";
        case sim::CommandError::target_defeated: return "TARGET ALREADY DOWN";
        case sim::CommandError::friendly_target: return "THAT IS AN ALLY";
        case sim::CommandError::target_out_of_range: return "OUT OF RANGE";
        case sim::CommandError::unknown_ability:
        case sim::CommandError::unavailable_ability: return "CANNOT USE THAT";
        case sim::CommandError::activation_in_progress: return "ANOTHER UNIT IS ACTING";
        case sim::CommandError::no_action_points: return "NO ACTIONS LEFT";
        case sim::CommandError::unknown_weapon:
        case sim::CommandError::unavailable_weapon: return "NO SUCH WEAPON";
        case sim::CommandError::unknown_item:
        case sim::CommandError::unavailable_item: return "NOT CARRYING THAT";
        case sim::CommandError::depleted_item: return "NONE LEFT";
        case sim::CommandError::unusable_item: return "NOTHING HAPPENS";
        case sim::CommandError::wrong_phase: return "NOT YET";
        case sim::CommandError::undeployable_unit: return "THEY STAND THERE";
        case sim::CommandError::outside_zone: return "NOT YOUR GROUND";
        case sim::CommandError::not_talkable: return "THEY HAVE NOTHING TO SAY";
        // Not "THAT UNIT IS DOWN", which is what this table says for a defeat.
        // Somebody who walked away did not die, and this line is where a player
        // learns the difference the rules underneath keep.
        case sim::CommandError::target_departed: return "THEY HAVE GONE";
        // And not "THEY HAVE GONE" either: somebody who has not come in yet is
        // not somebody who left, which is the distinction the refusals below
        // the surface keep and this line passes on.
        case sim::CommandError::target_unarrived: return "THEY ARE NOT HERE YET";
        case sim::CommandError::unarrived_unit: return "THEY HAVE NOT ARRIVED";
        // The actor-side half of "THEY HAVE GONE", and the same distinction
        // read the other way: the player picked somebody who is no longer on
        // the board, rather than aimed at one.
        case sim::CommandError::departed_unit: return "THEY HAVE LEFT";
        // Not "ANOTHER UNIT IS ACTING", which is a lie when the engine has
        // named nobody. Under a side block a player picks their own order, so
        // the one thing they have to be told is which of their line is already
        // spent.
        case sim::CommandError::already_acted: return "THEY HAVE ALREADY ACTED";
        case sim::CommandError::already_moved: return "THEY HAVE ALREADY MOVED";
    }
    return "CANNOT DO THAT";
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

void TurnClient::expect(bool condition, const char* name) {
    ++checks_;
    if (!condition) ++failures_;
    Text line;
    line.text("CHECK ").text(name).text(condition ? " PASS" : " FAIL");
    sink_.line(line.c_str());
}

// The transcript.
//
// Every line the harness compares is written here and nowhere else, so the ROM
// and the host expectation tool cannot disagree about a spelling and call it a
// disagreement about a machine. A fact is either the engine's answer or the
// player's cursor; there is no derived quantity in it.
void TurnClient::checkpoint(const sim::EncounterSnapshot& snapshot, Reason reason) {
    ++checkpoints_;
    {
        Text line;
        line.text("CHECKPOINT ").number(checkpoints_).text("-").text(
            reason_name(reason)
        );
        sink_.line(line.c_str());
    }
    {
        Text line;
        line.text("FACT camera ").number(camera_.x).space().number(camera_.y);
        sink_.line(line.c_str());
    }
    {
        Text line;
        line.text("FACT cursor ").number(cursor_x_).space().number(cursor_y_);
        sink_.line(line.c_str());
    }
    {
        // The selection is reported as an index into the snapshot's unit list,
        // never as an identity: identities are 64 bits and this transcript is
        // compared on a machine with no 64-bit arithmetic.
        int index = -1;
        for (std::size_t i = 0; i < snapshot.units.size(); ++i) {
            if (snapshot.units[i].id == selected_) index = static_cast<int>(i);
        }
        Text line;
        line.text("FACT selected ").number(index);
        sink_.line(line.c_str());
    }
    {
        Text line;
        line.text("FACT active ")
            .number(snapshot.active_side == sim::Side::first ? 0 : 1)
            .text(" outcome ")
            .number(static_cast<std::int32_t>(snapshot.outcome));
        sink_.line(line.c_str());
    }
    // The round the player is in and the number there are to outlast, beside
    // the fact that names whose turn it is. They are the same kind of fact
    // about the battle rather than about anybody on it.
    //
    // Emitted only on a board whose content says the battle is won by
    // outlasting rounds. Every transcript already derived is a transcript of a
    // board that says no such thing, so not one of them moves a byte, and a
    // survive map can be scripted the day one is authored.
    if (rounds_to_survive_ != 0U) {
        Text line;
        line.text("FACT round ")
            .number(static_cast<std::int32_t>(snapshot.round + 1U))
            .text(" of ")
            .number(static_cast<std::int32_t>(rounds_to_survive_));
        sink_.line(line.c_str());
    }
    if (menu_open_) {
        Text line;
        // Which of the two menus, as well as how many rows and where the caret
        // is. A transcript that could not tell the board's menu from a
        // character's would compare a run that opened the wrong one and agree
        // with itself, which is the one thing these lines exist to prevent.
        line.text("FACT menu ")
            .text(board_menu_open_ ? "board " : "unit ")
            .number(menu_rows_)
            .space()
            .number(menu_row_);
        sink_.line(line.c_str());
        for (int row = 0; row < menu_rows_; ++row) {
            Text entry;
            entry.text("FACT menurow ").number(row).space().text(menu_[row].label);
            sink_.line(entry.c_str());
        }
    }
    if (sheet_open_) {
        // Every line of the sheet, by name. This is the whole of what the
        // harness compares about the information screen, and it is worth
        // comparing line for line rather than by a count: a ROM that drew the
        // sheet of the wrong character, or dropped the row carrying the stats
        // that decide whether a blow lands, would draw an identically shaped
        // screen. The host derives these lines from `grandleon::sheet` against
        // the state `client::run_campaign` actually reaches, so the console is
        // required to reproduce text it cannot have copied.
        Text line;
        line.text("FACT sheet ").number(sheet_.count);
        sink_.line(line.c_str());
        for (int row = 0; row < sheet_.count; ++row) {
            Text entry;
            entry.text("FACT sheetline ").number(row).space().text(
                sheet_.line(row)
            );
            sink_.line(entry.c_str());
        }
    }
    if (aim_ != Aim::none) {
        // What the menu handed back and the cursor has not yet answered. It is
        // a fact rather than a flourish: a client that lost the pick between
        // two presses would send the wrong command with no other trace.
        Text line;
        const char* kind = "cast";
        if (aim_ == Aim::walk) kind = "walk";
        if (aim_ == Aim::strike) kind = "strike";
        if (aim_ == Aim::talk) kind = "talk";
        line.text("FACT aiming ")
            .text(kind)
            .space()
            .text(aim_text_);
        sink_.line(line.c_str());
    }
    if (message_[0] != '\0') {
        Text line;
        line.text("FACT message ").text(message_);
        sink_.line(line.c_str());
    }
    {
        // What the cursor is resting on, which is what a client with a status
        // bar puts in it. Reported because a panel nobody compared is a panel
        // that could be showing the wrong unit. It names whoever is under the
        // cursor rather than their kind, because that is what the bar says and
        // the two are only the same on a board fielding one of everybody.
        //
        // Three lines rather than one, and the split is about being readable
        // when it fails. A name may hold a space and, where a board fields two
        // of a kind, a digit, as in `DAWN KNIGHT 1`. A single line reading
        // `hovered DAWN KNIGHT 1 12 12 0` leaves a person diffing a console
        // mismatch unable to say where the name stopped. A line whose tail is
        // all name, and a line whose tail is all number, cannot be misread.
        const sim::UnitSnapshot* hovered = unit_at(snapshot, cursor_x_, cursor_y_);
        if (hovered != nullptr) {
            {
                Text line;
                line.text("FACT hovered ")
                    .text(character_called(snapshot, hovered->id).c_str());
                sink_.line(line.c_str());
            }
            {
                Text line;
                line.text("FACT hoverstats ")
                    .number(hovered->health)
                    .space()
                    .number(hovered->maximum_health)
                    .space()
                    .number(hovered->side == sim::Side::first ? 0 : 1);
                sink_.line(line.c_str());
            }
            // And what kind of character that is, when the package holds a
            // class for them. Absent rather than blank for a fixture that has
            // none, on the terms every optional line here keeps.
            const sheet::ContentName kind =
                sheet::class_name(package_, hovered->unit_type_id);
            if (kind.text[0] != '\0') {
                Text line;
                line.text("FACT hoverclass ").text(kind.c_str());
                sink_.line(line.c_str());
            }
        }
    }
    // Everybody standing on the board, by the engine's own predicate rather
    // than by a health test spelled here. A row carries a tile, and somebody
    // talked off the board or still marching towards it holds none. Reporting
    // one would be reporting a position the engine refuses every command aimed
    // at.
    for (std::size_t i = 0; i < snapshot.units.size(); ++i) {
        const sim::UnitSnapshot& unit = snapshot.units[i];
        if (!sim::on_board(unit)) continue;
        Text line;
        line.text("FACT unit ")
            .number(static_cast<std::int32_t>(i))
            .space()
            .number(unit.position.x)
            .space()
            .number(unit.position.y)
            .space()
            .number(unit.health)
            .space()
            .number(unit.side == sim::Side::first ? 0 : 1);
        sink_.line(line.c_str());
    }
    // Who on the board has already taken their turn, by the same index the
    // unit rows above use. Its own line rather than a column on those rows, so
    // that a board on which nobody is ever spent reports exactly what it always
    // reported, and every console expectation derived before free selection
    // existed still holds. That is every board under alternating order, where
    // nothing marks a character at all.
    for (std::size_t i = 0; i < snapshot.units.size(); ++i) {
        const sim::UnitSnapshot& unit = snapshot.units[i];
        if (!sim::on_board(unit) || !unit.has_acted) continue;
        Text line;
        line.text("FACT spent ").number(static_cast<std::int32_t>(i));
        sink_.line(line.c_str());
    }
    // The lit set, under the name of what is lighting it. `move` while the
    // player is choosing where to put a character down, `aim` while a pick is
    // waiting for a tile. One list in the client and two words here, so a
    // transcript says which board a frame was showing and the two machines are
    // compared on it rather than on a count that would match either way.
    const char* lit = aim_ != Aim::none ? "FACT aim " : "FACT move ";
    for (const sim::Position& tile : moves_) {
        Text line;
        line.text(lit).number(tile.x).space().number(tile.y);
        sink_.line(line.c_str());
    }
    for (const sim::Position& tile : danger_) {
        Text line;
        line.text("FACT danger ").number(tile.x).space().number(tile.y);
        sink_.line(line.c_str());
    }
    // And how wide the cursor is, on the one pick that makes it wider than a
    // tile.
    for (const sim::Position& tile : splash_) {
        Text line;
        line.text("FACT splash ").number(tile.x).space().number(tile.y);
        sink_.line(line.c_str());
    }
    sink_.line("FACT end");
}

// ---------------------------------------------------------------------------
// The Presenter seam
// ---------------------------------------------------------------------------

void TurnClient::present_dialogue(const package_runtime::Dialogue& dialogue) {
    // The turn ROM does not stage cutscenes: it has one 8x8 font and no
    // portrait budget, and a wave that stopped to write a text screen would not
    // have reached a playable turn. The lines are reported so a run still
    // records that the campaign flow reached them.
    //
    // A campaign build does stage them, below, because a campaign whose story
    // nodes are a count in a log is a campaign nobody is being told.
    Text line;
    line.text("DIALOGUE ").number(static_cast<std::int32_t>(dialogue.lines.size()));
    if (!dialogue.lines.empty()) {
        line.text(" first ").number(static_cast<std::int32_t>(dialogue.lines[0].text.size()));
    }
    sink_.line(line.c_str());
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // One saying to a page, because a page carries one face.
    //
    // Pages used to be packed: sayings went on until the next would not fit,
    // which put three or four speakers on most pages of the shipped scenes. A
    // page showing one face while four people talk on it would be telling the
    // player something false about three of them, and there is no room to draw
    // four — the panel is the display's full width and the band above it is one
    // portrait wide. Given the choice between a packed page that cannot say who
    // is speaking and a page per saying that can, this takes the second, which
    // is also what the cartridge's own cutscene screen has always done.
    //
    // What it costs is presses: a five-line scene is five pages where it was
    // two. What it buys is that every page names and shows exactly one person.
    for (const package_runtime::DialogueLine& spoken : dialogue.lines) {
        begin_page();
        // Set after the page is begun, because `begin_page` clears both: a
        // backdrop belongs to the scene and a speaker to the saying, and
        // neither may be inherited by whatever page comes next.
        page_backdrop_ = dialogue.backdrop;
        // Who the scene cast for this saying, if it cast anybody. A scene
        // authored before casts existed names nobody, and this stays false, and
        // the page draws exactly as it drew before.
        const std::uint64_t* who = dialogue.speaker_unit_type(spoken);
        story_has_speaker_ = who != nullptr;
        story_speaker_ = who != nullptr ? *who : 0;
        page_has_speaker_ = story_has_speaker_;
        page_speaker_ = story_speaker_;
        // A saying taller than one page is still held and continued inside
        // `push_wrapped`, which is the one break a page per saying cannot make
        // unnecessary.
        push_wrapped(
            spoken.speaker.c_str(), spoken.text.c_str(), dialogue.backdrop
        );
        hold_page(Screen::story, "A  GO ON");
    }
#endif
}

void TurnClient::battle_begins(
    const sim::EncounterSnapshot& snapshot,
    const client::Roster& roster,
    sim::Side player_side,
    const std::vector<std::uint64_t>& terrain
) {
    static_cast<void>(roster);
    player_side_ = player_side;
    terrain_ = terrain;
    cursor_x_ = 0;
    cursor_y_ = 0;
    selected_ = 0;
    deploying_ = false;
    menu_open_ = false;
    board_menu_open_ = false;
    menu_rows_ = 0;
    menu_row_ = 0;
    sheet_open_ = false;
    // Nothing carries over a board: a turn being drained and a menu waiting to
    // reopen both belong to the battle that asked for them.
    finishing_ = false;
    drain_last_ = 0;
    reopen_menu_ = 0;
    clear_aim();
    message_[0] = '\0';
    moves_.clear();
    danger_.clear();
    queried_for_ = 0;
    queried_at_ = ~std::uint64_t{0};
    last_hovered_ = 0;
    last_selected_ = 0;
    last_menu_open_ = false;
    last_menu_row_ = 0;
    last_sheet_open_ = false;
    last_aim_ = Aim::none;
    opened_ = false;
    checkpoints_ = 0;

    camera_.map_w = snapshot.width;
    camera_.map_h = snapshot.height;
    if (fit_.frame_w > 0) {
        // The platform fits each board to its screen. Both the cell size and
        // the window come from the one rule, so the derivation on the host and
        // the executable on the console reach the same numbers by running the
        // same arithmetic rather than by agreeing to.
        const view::BoardFit fit =
            view::fit_board(fit_, snapshot.width, snapshot.height);
        tile_ = fit.tile;
        camera_.view_w = fit.view_w;
        camera_.view_h = fit.view_h;
    } else {
        camera_.view_w =
            snapshot.width < viewport_w_ ? snapshot.width : viewport_w_;
        camera_.view_h =
            snapshot.height < viewport_h_ ? snapshot.height : viewport_h_;
    }
    camera_.x = 0;
    camera_.y = 0;
    camera_.clamp();

    // The cursor opens on the first of the player's characters who is actually
    // standing there rather than on the corner, because a cursor that starts on
    // nothing gives a player nothing to recognise. Somebody still marching in
    // holds no tile, so opening on them would be opening on empty ground. It is
    // the same tile on every run, which is what the transcript needs.
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (sim::on_board(unit) && unit.side == player_side_) {
            cursor_x_ = unit.position.x;
            cursor_y_ = unit.position.y;
            break;
        }
    }
    camera_.follow(cursor_x_, cursor_y_, 1);

    expect(
        terrain_.size() ==
            static_cast<std::size_t>(snapshot.width) * snapshot.height,
        "the terrain covers the board"
    );
    expect(!snapshot.units.empty(), "the encounter has units to command");

    Text line;
    line.text("BOARD ")
        .number(snapshot.width)
        .text("x")
        .number(snapshot.height)
        .text(" window ")
        .number(camera_.view_w)
        .text("x")
        .number(camera_.view_h)
        .text(" units ")
        .number(static_cast<std::int32_t>(snapshot.units.size()));
    sink_.line(line.c_str());
}

void TurnClient::battle_definitions(
    const std::vector<sim::WeaponDefinition>& weapons,
    const std::vector<sim::AbilityDefinition>& abilities,
    const std::vector<sim::ItemDefinition>& items,
    const std::vector<sim::ObjectiveDefinition>& objectives
) {
    weapons_ = weapons;
    abilities_ = abilities;
    items_ = items;
    // How many rounds this board is won by surviving, and zero where nothing
    // on it is. That is every board this client has ever been handed, and is
    // why the status row it feeds says exactly what it always said.
    rounds_to_survive_ = sheet::rounds_to_survive(objectives);
}

void TurnClient::draw(
    const sim::EncounterSnapshot& snapshot, const client::Roster& roster
) {
    static_cast<void>(roster);
    // A character who left the board may have been the selection, and a
    // selection the engine no longer recognises would light a stale range.
    // Whether they fell or were talked away makes no difference, since neither
    // holds a tile afterwards. Nor is a character the engine has finished with
    // still the player's to hold: its turn closed behind whatever it just did,
    // and so did the other side stepping in. Asked of the engine's own
    // predicate rather than tracked here, so a client can never disagree with
    // the board about who may still be given orders.
    if (selected_ != 0) {
        const sim::UnitSnapshot* still = unit_by_id(snapshot, selected_);
        if (still == nullptr || !sim::on_board(*still) || still->has_acted ||
            still->side != snapshot.active_side) {
            selected_ = 0;
        }
    }
    // A pick belongs to a selection. Nothing is left holding a spell for a
    // character that is no longer standing there.
    if (selected_ == 0) clear_aim();
    // And the drain the board menu asked for is over the moment the turn it was
    // ending has passed to the other side. Read off the engine's own
    // `active_side` rather than counted here, for the reason the lines above
    // read the engine's own `has_acted`: a client keeping its own tally of
    // whose turn it is would be a client that could disagree with the board.
    if (finishing_ && snapshot.active_side != player_side_) {
        finishing_ = false;
        drain_last_ = 0;
    }
    refresh_queries(snapshot);
    build_overlay(snapshot);
    paint(snapshot, overlay_);

    // Whose command produced this frame, told by whose turn it was when the
    // client last settled. The session redraws after every accepted command, so
    // the side that was active a moment ago is the side that just acted. An
    // event cannot answer this, because the unit a damage event names is the
    // one that was hit rather than the one that swung.
    Reason reason = last_snapshot_.active_side == player_side_ ? Reason::acted
                                                               : Reason::opposing;
    if (!opened_) reason = Reason::open;
    opened_ = true;
    last_snapshot_ = snapshot;
    last_selected_ = selected_;
    last_menu_open_ = menu_open_;
    last_menu_row_ = menu_row_;
    last_sheet_open_ = sheet_open_;
    last_aim_ = aim_;
    const sim::UnitSnapshot* hovered = unit_at(snapshot, cursor_x_, cursor_y_);
    last_hovered_ = hovered != nullptr ? hovered->id : 0;

    hold_for_checkpoint();
    checkpoint(snapshot, reason);
    after_facts(snapshot, overlay_);
    hold_for_checkpoint();
}

// The route a slide is drawn along, out of the simulation's own answer for the
// state the unit moved from. Nothing here is a second copy of the movement
// rule: this asks `reachable_tiles` which tiles the engine returned, adds the
// tiles the mover's own side holds, and then walks between them. A walk goes
// through those tiles and never stops on them, so the query does not list them
// and a route may still need them. Every tile the token is drawn standing on is
// a tile the engine said this walk may be on, and the route is as short as that
// set allows.
//
// Zero means "no route was planned", which the platform draws as the straight
// line between the two cells. That is the honest fallback: a guessed route
// could cross ground this unit cannot, and a straight line at least never
// claims to be one.
namespace {

// One board's worth of route scratch, in `.bss` rather than in the client's
// stack frame: 512 bytes of breadth-first distance, 64 bytes saying which
// cells the engine's query returned, and room for a route of 32 tiles. There
// is one client per ROM and one route in flight at a time, so one of each is
// the honest number. A board with more cells than this, or a route longer than
// that, plans nothing and the platform draws the straight line.
constexpr int route_cells = 512;
constexpr int route_capacity = 32;
std::uint8_t route_distance[route_cells];
std::uint8_t route_member[route_cells / 8];
view::RouteTile route_storage[route_capacity];

}  // namespace

const view::RouteTile* TurnClient::route_tiles() { return route_storage; }

int TurnClient::plan_move_route(sim::UnitId unit, sim::Position destination) {
    const int width = static_cast<int>(last_snapshot_.width);
    const int height = static_cast<int>(last_snapshot_.height);
    if (width <= 0 || height <= 0) return 0;
    if (width * height > route_cells) return 0;
    const sim::UnitSnapshot* actor = unit_by_id(last_snapshot_, unit);
    if (actor == nullptr) return 0;
    const std::vector<sim::Position> reach =
        sim::reachable_tiles(last_snapshot_, unit);
    if (reach.empty()) return 0;
    for (std::uint8_t& bits : route_member) bits = 0;
    const auto mark = [width, height](sim::Position tile) {
        if (tile.x < 0 || tile.y < 0 || tile.x >= width || tile.y >= height) {
            return;
        }
        const int cell = tile.y * width + tile.x;
        route_member[cell >> 3] |=
            static_cast<std::uint8_t>(1U << (cell & 7));
    };
    for (const sim::Position& tile : reach) mark(tile);
    // And the tiles the walk was allowed to cross without stopping. `reach`
    // cannot list them, because a walk may not finish on anybody, so a route
    // through a gap in this side's own line would be a route through a hole in
    // the set and the platform would fall back to a straight line.
    for (const sim::UnitSnapshot& other : last_snapshot_.units) {
        if (other.id == actor->id || other.side != actor->side) continue;
        if (!sim::on_board(other)) continue;
        mark(other.position);
    }
    view::Route route(route_storage, route_capacity);
    view::plan_route(
        actor->position.x, actor->position.y, destination.x, destination.y,
        width, height,
        [width](int x, int y) {
            const int cell = y * width + x;
            return (route_member[cell >> 3] &
                    static_cast<std::uint8_t>(1U << (cell & 7))) != 0;
        },
        route_distance, route
    );
    return route.size();
}

int TurnClient::separation_between(
    const sim::UnitSnapshot* lhs, const sim::UnitSnapshot* rhs
) noexcept {
    if (lhs == nullptr || rhs == nullptr) return 0;
    return client::separation_between(lhs->position, rhs->position);
}

const char* TurnClient::fall_word(sim::UnitId unit) const noexcept {
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // The softer word belongs to the company and to nobody else. A campaign
    // that carries its own people off the field does not carry the bandit off
    // with them, and a log that said the bandit fell would be promising a
    // player they were going to meet him again.
    //
    // Whether this character is one of the company is the join's answer, which
    // is the same question `character_called` asks a line earlier.
    if (board_ != nullptr &&
        board_->character_loss == pr::CharacterLoss::recoverable &&
        board_->binding.persistent_of(campaign::BattleEntityId{unit}).value !=
            0U) {
        return " FELL";
    }
#endif
    static_cast<void>(unit);
    // A board played outside a campaign has no rule to read and no company to
    // lose anybody from, so it says the plain thing: somebody went down on it.
    //
    // Nine characters with its leading space, against `" DIED"`'s five. The box
    // this lands in is a console's message row, which truncates rather than
    // wraps, so on a forty-column row the name in front of it has 31 columns.
    // `message_` is 48 bytes and never the binding limit. No name in this
    // repository's games comes near it: the longest is ten characters, and
    // `BANDIT DEFEATED` is fifteen.
    //
    // The failure mode a longer name would have is worth writing down: a name
    // of 32 to 35 characters costs the *word* its tail, where `DIED` would have
    // fitted. The Nintendo 64 does not have it, because that console caps the
    // name at twenty and sizes its box to the sentence. The fix here, if a
    // project ever needs one, is to cap the name the same way rather than to
    // shorten the word.
    return " DEFEATED";
}

sheet::ContentName TurnClient::character_called(
    const sim::EncounterSnapshot& snapshot, sim::UnitId unit
) const noexcept {
    const char* member = nullptr;
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // The campaign's own name, through the join the session published. Asked
    // here because only a client holds that join; what is done with the answer,
    // and what happens when there is none, is `sheet::character_name`'s and is
    // shared with every other client.
    if (board_ != nullptr) {
        member = client::member_name_on_board(
            board_->binding, board_->roster, unit
        );
    }
#endif
    return sheet::character_name(package_, snapshot, unit, member);
}

view::AttackGesture TurnClient::gesture_for(
    const sim::UnitSnapshot* striker, int separation
) const {
    return client::gesture_for(striker, abilities_, separation);
}

void TurnClient::report(
    const sim::CommandResult& result, const client::Roster& roster
) {
    static_cast<void>(roster);
    for (const sim::Event& event : result.events) {
        if (event.type == sim::EventType::unit_moved) {
            // The one place a token is drawn between two cells. The route is
            // planned before the platform is asked to draw it, so the plan is
            // the same plan on the console and in the host build that derives
            // this run's expectations. The host simply draws nothing with it.
            const int length = plan_move_route(event.unit_id, event.position);
            animate_move(
                last_snapshot_, event.unit_id, route_tiles(), length,
                event.position
            );
            continue;
        }
        if (event.type == sim::EventType::attack_missed) {
            // A miss is reported for the same reason a refusal is: a press
            // whose whole visible consequence is nothing reads as a broken
            // button rather than as a rule.
            sink_.line("EVENT miss");
            // And it is drawn as the blow it was. The engine names whoever
            // swung, in `related_unit_id` on this very event, and the name is
            // read the same way a landed blow reads it, out of the board the
            // blow was thrown from.
            const sim::UnitSnapshot* missed_at =
                unit_by_id(last_snapshot_, event.unit_id);
            const sim::UnitSnapshot* thrower =
                event.related_unit_id != 0
                    ? unit_by_id(last_snapshot_, event.related_unit_id)
                    : nullptr;
            const int miss_separation =
                separation_between(missed_at, thrower);
            animate_miss(
                last_snapshot_, event.position, event.unit_id,
                thrower != nullptr ? thrower->id : 0,
                gesture_for(thrower, miss_separation), miss_separation
            );
            continue;
        }
        if (event.type == sim::EventType::unit_defeated) {
            // Somebody went down, said at the moment it happened and with their
            // name on it. Nothing in a battle used to mark this at all: the only
            // sentences that mentioned a defeat were the two refusals a player
            // met by trying to act on somebody already down, and the first place
            // a name and a loss appeared together was the company screen, which
            // could be several battles later.
            //
            // Which word depends on what the campaign has decided a fall costs,
            // and that is the point of asking. Under the permanent rule they are
            // gone for the rest of the campaign and the word is `DEFEATED`.
            // Under the recoverable rule they are down and out of this battle
            // and they are coming back, so `FELL`. Telling that player they
            // were lost would be a lie the next screen contradicts.
            //
            // The harder word is deliberately absent: children play these, so
            // a felled character is `DEFEATED`. That against `FELL` is a
            // narrower gap than `DIED` against `FELL` was, so the roster row
            // now carries the weight: a member the campaign will never field
            // again reads `LOST` there, and one who is coming back reads
            // `FIELD`.
            const sheet::ContentName who =
                character_called(last_snapshot_, event.unit_id);
            const char* const what = fall_word(event.unit_id);
            {
                Text line;
                line.text("EVENT fall ").text(who.c_str());
                sink_.line(line.c_str());
            }
            write_message(who.c_str(), what);
            continue;
        }
        if (event.type == sim::EventType::unit_endured) {
            // A blow that would have felled somebody was caught by their health
            // floor. Said for the reason a miss is said: the alternative is a
            // player watching a killing blow land on somebody who then keeps
            // standing, with nothing anywhere explaining it.
            const sheet::ContentName who =
                character_called(last_snapshot_, event.unit_id);
            {
                Text line;
                line.text("EVENT hold ").text(who.c_str());
                sink_.line(line.c_str());
            }
            write_message(who.c_str(), " HELD ON");
            continue;
        }
        if (event.type == sim::EventType::item_dropped) {
            // Something was left where somebody fell. Reported for the same
            // reason a miss is: it happened, and a channel that says nothing
            // about it is a channel that lost it. The thing itself is not
            // named: a drop enters nobody's pack, so this console has no
            // satchel to look a name up in, and what fell is the campaign's to
            // record.
            sink_.line("EVENT drop");
            continue;
        }
        if (event.type == sim::EventType::unit_talked) {
            // Somebody walked off the board alive. Reported with the tile they
            // left rather than with a name, on the terms every other line here
            // keeps: identities are 64 bits and this transcript is compared on
            // a machine with no 64-bit arithmetic. It is deliberately not
            // spelled like a defeat: leaving and dying are two different
            // facts, and a channel that called them one thing would undo the
            // distinction every rule underneath it keeps.
            Text left;
            left.text("EVENT talk ")
                .number(event.position.x)
                .space()
                .number(event.position.y);
            sink_.line(left.c_str());
            continue;
        }
        if (event.type == sim::EventType::unit_deployed) {
            Text stood;
            stood.text("EVENT stand ")
                .number(event.position.x)
                .space()
                .number(event.position.y);
            sink_.line(stood.c_str());
            continue;
        }
        if (event.type == sim::EventType::deployment_ended) {
            sink_.line("EVENT deployed");
            continue;
        }
        if (event.type != sim::EventType::unit_damaged) continue;
        Text line;
        line.text("EVENT damage ").number(event.amount);
        sink_.line(line.c_str());
        // Which way the blow came from, so the token is knocked away from it
        // rather than in whatever direction the renderer felt like. An area
        // effect has no striker on the board and knocks nothing.
        const sim::UnitSnapshot* struck =
            unit_by_id(last_snapshot_, event.unit_id);
        const sim::UnitSnapshot* striker =
            event.related_unit_id != 0
                ? unit_by_id(last_snapshot_, event.related_unit_id)
                : nullptr;
        int toward_x = 0;
        int toward_y = 0;
        if (struck != nullptr && striker != nullptr) {
            toward_x = struck->position.x - striker->position.x;
            toward_y = struck->position.y - striker->position.y;
        }
        const int separation = separation_between(struck, striker);
        animate_hit(
            last_snapshot_, event.unit_id,
            striker != nullptr ? striker->id : 0, toward_x, toward_y,
            gesture_for(striker, separation), separation
        );
    }
}

void TurnClient::refused(sim::CommandError error) {
    // A refusal the player cannot see is a button that appears to do nothing.
    // The engine decides that a command is illegal; this only says so.
    //
    // And it says only what the engine said. `target_out_of_range` covers both
    // too close and too far, and telling them apart here would mean measuring
    // the distance and comparing it against the actor's reach. That is the
    // engine's range rule, restated on a console where nothing could notice it
    // drifting. The day that distinction is worth drawing, the engine is where
    // it should be drawn.
    {
        Text line;
        line.text("REFUSED ").text(sim::error_name(error).data());
        sink_.line(line.c_str());
    }
    // Two of these are facts about the character the player picked rather than
    // about the battle, and under a side block picking is the whole of what a
    // player does. So they say who: "MIREA HAS ALREADY ACTED" reads as a
    // sentence about somebody, where "THEY HAVE ALREADY ACTED" leaves a player
    // holding a cursor over a line of characters wondering which one it meant.
    // Everything else stays impersonal, because everything else is about the
    // battle rather than about a person.
    //
    // `pending_` is still the command that was refused: the session applies an
    // intent and reports the refusal before asking for another, and asking for
    // another is what clears it. So the character is already here and this
    // client keeps no second copy of it. A campaign build can have single
    // digits of heap to spare, and every field on this class is paid for out
    // of them.
    if (pending_.unit_id != 0 &&
        (error == sim::CommandError::already_acted ||
         error == sim::CommandError::already_moved)) {
        const sheet::ContentName who =
            character_called(last_snapshot_, pending_.unit_id);
        write_message(
            who.c_str(),
            error == sim::CommandError::already_acted ? " HAS ALREADY ACTED"
                                                      : " HAS ALREADY MOVED"
        );
    } else {
        copy_line(message_, sizeof message_, refusal_text(error));
    }

    build_overlay(last_snapshot_);
    paint(last_snapshot_, overlay_);
    hold_for_checkpoint();
    checkpoint(last_snapshot_, Reason::refused);
    after_facts(last_snapshot_, overlay_);
    hold_for_checkpoint();
}

void TurnClient::show_state(
    const sim::EncounterSnapshot& snapshot,
    std::uint64_t canonical_hash,
    const std::vector<sim::ObjectiveDefinition>& objectives
) {
    static_cast<void>(snapshot);
    static_cast<void>(canonical_hash);
    static_cast<void>(objectives);
}

void TurnClient::battle_ended(
    const sim::EncounterSnapshot& snapshot, std::uint64_t canonical_hash
) {
    static_cast<void>(canonical_hash);
    Text line;
    line.text("BATTLE ")
        .text(
            snapshot.outcome == sim::Outcome::first_side_won ? "blue" : "red"
        );
    sink_.line(line.c_str());
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    ++battles_;
#endif
}

void TurnClient::campaign_ended() {
    sink_.line("CAMPAIGN complete");
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    begin_page();
    push_line(project_title_);
    push_line("");
    push_line("THE END");
    push_line("");
    push_line("THANKS FOR PLAYING");
    hold_page(Screen::ended, "A  GO ON");
#endif
}

client::Intent TurnClient::next_intent(
    const sim::EncounterSnapshot& snapshot, const client::Roster& roster
) {
    static_cast<void>(roster);
    pending_ = client::Intent{};
    // The turn the player asked the board menu to finish, before a press is
    // read: the side owes these commands whether or not a thumb moves.
    drain_side(snapshot);
    if (pending_.kind != client::IntentKind::none) return pending_;
    // A character that has just walked is asked what it does next, rather than
    // being left standing on a board with nothing said about it. On a board
    // where the walk closed the turn, `draw` has already let the selection go
    // and this does nothing, which is exactly right: there is nothing left to
    // ask that character.
    if (reopen_menu_ != 0) {
        const sim::UnitId walked = reopen_menu_;
        reopen_menu_ = 0;
        if (walked == selected_) {
            // And a character the board has nothing left to offer ends its turn
            // where it stands, instead of being handed a menu whose every
            // committing row the engine would refuse. `client::nothing_left_to_do`
            // is the judgement and it is shared, so no two front ends can
            // disagree about when a turn ends itself.
            if (client::nothing_left_to_do(
                    snapshot, walked, weapons_, abilities_, items_
                )) {
                selected_ = 0;
                clear_aim();
                pending_.kind = client::IntentKind::wait;
                pending_.unit_id = walked;
                return pending_;
            }
            const sim::UnitSnapshot* actor = unit_by_id(snapshot, walked);
            if (actor != nullptr) open_menu(snapshot, *actor);
        }
    }
    while (pending_.kind == client::IntentKind::none) {
        settle(snapshot);
        press(snapshot, next_press());
    }
    return pending_;
}

// The board opens arranged rather than fought. Reported like everything else
// this client does, so the harness sees the phase in the transcript rather than
// inferring it from a board that did not move.
void TurnClient::deployment_begins(
    const sim::EncounterSnapshot& snapshot,
    const client::Roster& roster,
    const std::vector<sim::Position>& zone
) {
    static_cast<void>(roster);
    deploying_ = true;
    selected_ = 0;
    message_[0] = '\0';
    Text line;
    line.text("DEPLOY zone ").number(static_cast<int>(zone.size()));
    sink_.line(line.c_str());
    static_cast<void>(snapshot);
}

// The same loop the battle runs, asking the same thumb a different question.
// A separate entry point rather than a mode flag on `next_intent`, because the
// two phases offer different presses and a client that served the wrong menu
// would be a client whose every row was refused.
client::Intent TurnClient::next_deployment_intent(
    const sim::EncounterSnapshot& snapshot, const client::Roster& roster
) {
    static_cast<void>(roster);
    pending_ = client::Intent{};
    while (pending_.kind == client::IntentKind::none) {
        settle(snapshot);
        deploy_press(snapshot, next_press());
    }
    return pending_;
}

// ---------------------------------------------------------------------------
// Settling
// ---------------------------------------------------------------------------

void TurnClient::settle(const sim::EncounterSnapshot& snapshot) {
    refresh_queries(snapshot);
    build_overlay(snapshot);
    paint(snapshot, overlay_);

    const sim::UnitSnapshot* hovered = unit_at(snapshot, cursor_x_, cursor_y_);
    const sim::UnitId hovered_id = hovered != nullptr ? hovered->id : 0;

    // Where the cursor came to rest, reported on every settle rather than only
    // on the ones worth photographing. It is not compared, because a press that
    // moves a cursor one cell cannot disagree between two machines without
    // something louder disagreeing first. A person reading a log still wants
    // to see the thumb move, and a script being written wants to see where it
    // went.
    {
        Text line;
        line.text("SETTLE ")
            .number(cursor_x_)
            .space()
            .number(cursor_y_)
            .text(hovered != nullptr ? " over" : " open")
            .text(selected_ != 0 ? " holding" : " idle");
        sink_.line(line.c_str());
    }

    Reason reason = Reason::none;
    if (selected_ != last_selected_) {
        reason = Reason::select;
    } else if (sheet_open_ != last_sheet_open_) {
        // The sheet going up or coming down, ahead of the menu, because the
        // menu did not move when it did: the whole point of the INFO row is
        // that reading is not choosing.
        reason = Reason::sheet;
    } else if (menu_open_ != last_menu_open_ ||
               (menu_open_ && menu_row_ != last_menu_row_)) {
        // A caret that moved is worth photographing. It is the only evidence
        // that the row the next A commits is the row the player is looking at.
        reason = Reason::menu;
    } else if (aim_ != last_aim_) {
        reason = Reason::aiming;
    } else if (hovered_id != last_hovered_) {
        reason = Reason::hover;
    }
    last_selected_ = selected_;
    last_menu_open_ = menu_open_;
    last_menu_row_ = menu_row_;
    last_sheet_open_ = sheet_open_;
    last_aim_ = aim_;
    last_hovered_ = hovered_id;
    last_snapshot_ = snapshot;
    if (reason == Reason::none) return;

    hold_for_checkpoint();
    checkpoint(snapshot, reason);
    after_facts(snapshot, overlay_);
    hold_for_checkpoint();
}

// The two queries this client makes of the engine, and the only two it caches.
//
// `danger_tiles` unions a movement search with a weapon band for every living
// unit on a side, which is the most expensive thing a console does between two
// button presses. Neither answer can change while the board and the selection
// stand still, so both are asked for again only when one of those moves.
//
// **What is kept of the danger answer is its overlap with where this character
// can actually go**, and that is the whole of the marking rule: a lit tile is a
// tile this character may step onto, blue if nothing threatens it and red if
// something does. The union of the two lit sets is exactly the reachable set,
// so the board answers one question instead of two laid over each other:
// *where may I go, and which of those places is safe*.
//
// The two are intersected rather than stacked. Painting the opposing side's
// whole zone over the reach would, on a board where the enemy covers most of
// the field, be most of the screen in red with the player's own moves buried
// underneath, which reads as noise on a real cartridge. Both answers come from
// the engine and neither is derived here.
//
// **And once a pick is held, `moves_` holds the pick's tiles instead**, from
// `sim::aimable_tiles`. It is the same list in the same place because a lit
// tile means the same thing either way: *this is where the next confirm lands*.
// A second list would also be a second allocation on the machine whose campaign
// ROM has run out of contiguous heap on tens of bytes.
// The renderers tell the two apart by `Overlay::aiming`, which is already
// non-null exactly while a pick is held, and draw the one in amber and the
// other in blue.
//
// The danger wash is kept only for a walk. On a strike, a cast or a talk the
// lit tiles are where the gesture *lands* rather than where this character
// will be standing, so shading them would be a warning about a move nobody is
// making.
void TurnClient::refresh_queries(const sim::EncounterSnapshot& snapshot) {
    if (selected_ == 0) {
        moves_.clear();
        danger_.clear();
        release(splash_);
        queried_for_ = 0;
        queried_at_ = ~std::uint64_t{0};
        queried_aim_ = Aim::none;
        queried_identity_ = 0;
        return;
    }
    // While the board is being arranged the tiles under the marker are the
    // region, from the engine's own judgement of it. They are re-asked on
    // every settle, because a deploy moves nobody's activation count and the
    // cache below would never notice one.
    if (snapshot.deploying) {
        moves_ = sim::deployable_tiles(snapshot, selected_);
        const sim::Side arranging_against = player_side_ == sim::Side::first
                                                ? sim::Side::second
                                                : sim::Side::first;
        release(danger_);
        danger_ = sim::danger_tiles(
            snapshot, arranging_against, weapons_, abilities_
        );
        keep_only_reachable(danger_, moves_);
        queried_for_ = 0;
        queried_at_ = ~std::uint64_t{0};
        queried_aim_ = Aim::none;
        queried_identity_ = 0;
    } else {
        // The pick is part of the key, because it is part of the answer.
        // Without it the cache would happily serve a movement range to a strike
        // that was taken between two presses, which is the one way this
        // arrangement could put a lit tile and an accepted command back into
        // disagreement.
        const sim::ContentId identity =
            aim_ == Aim::cast ? aim_ability_ : aim_weapon_;
        if (queried_for_ != selected_ ||
            queried_at_ != snapshot.activation_count ||
            queried_aim_ != aim_ || queried_identity_ != identity) {
            release(danger_);
            if (aim_ != Aim::none) {
                moves_ = sim::aimable_tiles(
                    snapshot, selected_, aimed_gesture(), weapons_, abilities_
                );
            } else {
                moves_ = sim::reachable_tiles(snapshot, selected_);
            }
            if (aim_ == Aim::none || aim_ == Aim::walk) {
                const sim::Side opposing = player_side_ == sim::Side::first
                                               ? sim::Side::second
                                               : sim::Side::first;
                danger_ =
                    sim::danger_tiles(snapshot, opposing, weapons_, abilities_);
                keep_only_reachable(danger_, moves_);
            }
            queried_for_ = selected_;
            queried_at_ = snapshot.activation_count;
            queried_aim_ = aim_;
            queried_identity_ = identity;
        }
    }
    // A pick that names a character opens on one. Every step after this is
    // `client::next_aim_tile`, which never leaves the lit set, so this is the
    // one place the cursor enters it. It is here rather than beside the menu
    // row because a pick's tiles are not known until the query above has been
    // made. A strike with nobody in reach lights nothing and moves
    // nothing, which is the dark board that answers the player without a step.
    if (aim_ != Aim::none &&
        client::gesture_names_a_character(aimed_gesture().kind) &&
        !moves_.empty() && !contains(&moves_, cursor_x_, cursor_y_)) {
        const sim::Position landed =
            client::nearest_aim_tile(moves_, {cursor_x_, cursor_y_});
        cursor_x_ = landed.x;
        cursor_y_ = landed.y;
        camera_.follow(cursor_x_, cursor_y_, 1);
    }
    // The splash of an area cast under the cursor, which is the one answer here
    // that moves with the cursor rather than with the selection. So it sits
    // outside the cache above, and is asked for again on every settle. It is
    // asked last because it follows the cursor the snap may have just moved.
    //
    // It is drawn as the cursor being the size the cast is, rather than as a
    // fourth colour, which is what lets a palette-bound console draw it at all:
    // a board's own art can leave two spare entries, and the water's shimmer
    // already contends for them.
    release(splash_);
    if (aim_ == Aim::cast) {
        splash_ = sim::area_tiles(
            snapshot, aim_ability_, {cursor_x_, cursor_y_}, abilities_
        );
    }
}

// The pick the player is holding, said in the engine's words.
//
// A translation and not a decision: this client already stores exactly the
// three values `sim::AimedGesture` carries, because it has to send them in a
// command, and handing them over is what stops it deciding for itself what its
// own pick can reach.
sim::AimedGesture TurnClient::aimed_gesture() const noexcept {
    sim::AimedGesture gesture;
    switch (aim_) {
        case Aim::walk: gesture.kind = sim::Gesture::walk; break;
        case Aim::strike: gesture.kind = sim::Gesture::strike; break;
        case Aim::cast: gesture.kind = sim::Gesture::cast; break;
        case Aim::talk: gesture.kind = sim::Gesture::talk; break;
        case Aim::none: break;
    }
    gesture.weapon_id = aim_weapon_;
    gesture.ability_id = aim_ability_;
    return gesture;
}

void TurnClient::build_overlay(const sim::EncounterSnapshot& snapshot) {
    // The round the player is in, and how many there are to outlast, on the
    // boards that say there are any.
    if (rounds_to_survive_ != 0U) {
        sheet::round_line(
            snapshot.round, rounds_to_survive_, round_line_,
            sizeof round_line_
        );
        overlay_.round = round_line_;
    } else {
        overlay_.round = nullptr;
    }
    // The message under the board: what the strike the cursor rests on would
    // cost, priced by the engine. A refusal already written there survives
    // until the cursor gives it something else to say.
    if (selected_ != 0) {
        const sim::UnitSnapshot* hovered = unit_at(snapshot, cursor_x_, cursor_y_);
        if (hovered != nullptr && hovered->side != player_side_) {
            const sim::AttackForecast forecast =
                sim::forecast_attack(snapshot, selected_, hovered->id);
            Text line;
            if (forecast) {
                // The chance leads, because the numbers after it are what
                // lands only when it does. Drawn from the forecast, never
                // derived here, and omitted entirely at a hundred so a line
                // over content that cannot miss reads as it always did.
                if (forecast.hit_chance < 100U) {
                    line.number(forecast.hit_chance).text("% ");
                }
                line.text("HIT ").number(forecast.damage);
                if (forecast.lethal) {
                    line.text(" KO");
                } else {
                    line.text(" LEFT ").number(forecast.target_health_after);
                }
            } else {
                line.text(refusal_text(forecast.error));
            }
            copy_line(message_, sizeof message_, line.c_str());
        }
    }

    // Who is under the cursor, in the words every surface says it in. Composed
    // once here so the bar, the transcript and the sheet cannot disagree.
    {
        const sim::UnitSnapshot* hovered =
            unit_at(snapshot, cursor_x_, cursor_y_);
        if (hovered == nullptr) {
            hovered_name_[0] = '\0';
            hovered_class_[0] = '\0';
        } else {
            copy_line(
                hovered_name_, sizeof hovered_name_,
                character_called(snapshot, hovered->id).c_str()
            );
            copy_line(
                hovered_class_, sizeof hovered_class_,
                sheet::class_name(package_, hovered->unit_type_id).c_str()
            );
        }
    }

    overlay_.cursor_x = cursor_x_;
    overlay_.cursor_y = cursor_y_;
    overlay_.selected = selected_;
    overlay_.moves = selected_ != 0 ? &moves_ : nullptr;
    overlay_.danger = selected_ != 0 ? &danger_ : nullptr;
    overlay_.splash = splash_.empty() ? nullptr : &splash_;
    overlay_.message = message_[0] != '\0' ? message_ : nullptr;
    overlay_.menu = menu_open_ ? menu_ : nullptr;
    overlay_.menu_rows = menu_rows_;
    overlay_.menu_row = menu_row_;
    overlay_.board_menu = menu_open_ && board_menu_open_;
    overlay_.sheet = sheet_open_ ? &sheet_ : nullptr;
    overlay_.aiming = aim_ != Aim::none ? aim_text_ : nullptr;
    overlay_.hovered_name = hovered_name_[0] != '\0' ? hovered_name_ : nullptr;
    overlay_.hovered_class =
        hovered_class_[0] != '\0' ? hovered_class_ : nullptr;
}

// ---------------------------------------------------------------------------
// The gestures
//
// The same vocabulary the Nintendo 64 offers, on the buttons this machine has:
// the d-pad steers, A selects and commits, B backs out, C opens the selected
// character's action menu, Start ends its activation. Nothing here judges
// whether a command is legal: it is sent, and the engine answers.
//
// C opens the list of everything the character can be told to do, not a list
// of its spells, and it needs nothing pointed at: waiting is not something one
// does *to* an enemy. Picking a strike or a cast out of that menu leaves the
// client holding the pick (see `Aim`), and the next A says where it lands.
//
// A carries the shortcuts: an enemy under the cursor is struck with the weapon
// in hand, an empty tile is walked to, one of yours is selected. The menu is
// the considered path, never the only one.
//
// The menu's INFO row is the one place a press does not reach any of that: the
// information sheet is on top of the menu and answers first, and any of A, B, C
// or Start puts it down. It is a thing to read, so there is nothing on it to
// choose wrongly.
// ---------------------------------------------------------------------------
void TurnClient::press(
    const sim::EncounterSnapshot& snapshot, std::uint16_t buttons
) {
    if ((buttons & pad_end_of_script) != 0U) {
        pending_.kind = client::IntentKind::quit;
        return;
    }

    // The sheet is on top of the menu, so it answers first. Any way out is the
    // way out, because the sheet is a thing to read and never a thing to
    // choose: B is the back-out this client uses everywhere, and A is what a
    // player who opened the sheet with A reaches for again. The menu is still
    // open underneath, caret still on the row that opened it.
    if (sheet_open_) {
        if ((buttons & (pad_a | pad_b | pad_c | pad_start)) != 0U) {
            sheet_open_ = false;
        }
        return;
    }

    if (menu_open_) {
        // The caret wraps at both ends. A menu whose rows are ordered by what
        // they cost puts the sheet, the way out and the end of the character's
        // turn at the bottom, which is the far end of a walk down a menu that
        // grew a row; wrapping puts them one press from the top instead of
        // five. It is also what lets a generated press table reach the tail of
        // a menu whose length it cannot know.
        if ((buttons & pad_up) != 0U && menu_rows_ > 0) {
            menu_row_ = menu_row_ > 0 ? menu_row_ - 1 : menu_rows_ - 1;
        }
        if ((buttons & pad_down) != 0U && menu_rows_ > 0) {
            menu_row_ = menu_row_ + 1 < menu_rows_ ? menu_row_ + 1 : 0;
        }
        if ((buttons & pad_b) != 0U) {
            menu_open_ = false;
            board_menu_open_ = false;
            menu_rows_ = 0;
        }
        if ((buttons & pad_a) != 0U) {
            if (board_menu_open_) {
                commit_board_row(snapshot);
            } else {
                commit_menu_row(snapshot);
            }
        }
        return;
    }

    if (aim_ != Aim::none &&
        client::gesture_names_a_character(aimed_gesture().kind)) {
        // A pick that names a character turns the d-pad into "which of these",
        // and the tiles are the engine's own aiming answer, already lit under
        // the cursor. The choice among them is `client::next_aim_tile`, shared
        // with every other front end that aims, so a press means the same thing
        // on each.
        int dx = 0;
        int dy = 0;
        if ((buttons & pad_up) != 0U) dy = -1;
        if ((buttons & pad_down) != 0U) dy = 1;
        if ((buttons & pad_left) != 0U) dx = -1;
        if ((buttons & pad_right) != 0U) dx = 1;
        // One axis per press. A pad reporting two at once steers by the
        // horizontal, which is the axis a stick pushed into a corner reads
        // strongest on.
        if (dx != 0) dy = 0;
        if (dx != 0 || dy != 0) {
            const sim::Position landed = client::next_aim_tile(
                moves_, {cursor_x_, cursor_y_}, dx, dy
            );
            cursor_x_ = landed.x;
            cursor_y_ = landed.y;
        }
    } else {
        if ((buttons & pad_up) != 0U && cursor_y_ > 0) --cursor_y_;
        if ((buttons & pad_down) != 0U &&
            cursor_y_ + 1 < static_cast<int>(snapshot.height)) {
            ++cursor_y_;
        }
        if ((buttons & pad_left) != 0U && cursor_x_ > 0) --cursor_x_;
        if ((buttons & pad_right) != 0U &&
            cursor_x_ + 1 < static_cast<int>(snapshot.width)) {
            ++cursor_x_;
        }
    }
    // A board larger than the window is why this is here at all: the Fordlight
    // is ten by eight and the screen holds ten by seven, so a cursor that could
    // not scroll could not reach the bottom row. `view::Camera::follow` is the
    // shared rule for it; nothing about scrolling is decided in this file.
    camera_.follow(cursor_x_, cursor_y_, 1);
    if ((buttons & (pad_up | pad_down | pad_left | pad_right)) != 0U) {
        // A cursor that has moved off a target has nothing left to price.
        message_[0] = '\0';
    }

    if ((buttons & pad_b) != 0U) {
        // One step back per press: a pick is put down before a selection is,
        // so backing out of a spell does not also lose the character.
        if (aim_ != Aim::none) {
            clear_aim();
            message_[0] = '\0';
            return;
        }
        if (selected_ != 0) {
            selected_ = 0;
            message_[0] = '\0';
            return;
        }
    }
    // START opens the board menu, at any point in a battle and whether or not a
    // character is selected. It answers the questions that are about the battle
    // rather than about one of its characters; what ends a character's turn has
    // a row in that character's own menu instead. A START that only sent a
    // `wait` for the selection would be an undiscoverable "I am done" that did
    // nothing at all with nobody picked up.
    if ((buttons & pad_start) != 0U) {
        open_board_menu();
        return;
    }
    if ((buttons & pad_c) != 0U && selected_ != 0) {
        const sim::UnitSnapshot* actor = unit_by_id(snapshot, selected_);
        if (actor != nullptr) open_menu(snapshot, *actor);
        return;
    }
    if ((buttons & pad_a) == 0U) return;

    if (aim_ != Aim::none) {
        commit_aim(snapshot);
        return;
    }

    const sim::UnitSnapshot* occupant = unit_at(snapshot, cursor_x_, cursor_y_);
    if (selected_ != 0 && occupant != nullptr && occupant->side != player_side_) {
        // The shortcut, and the weapon it uses is the one in hand. A character
        // that wants the other one asks for it in the menu; naming no weapon
        // is the command the engine has always been given.
        pending_.kind = client::IntentKind::attack;
        pending_.unit_id = selected_;
        pending_.target_id = occupant->id;
        selected_ = 0;
        return;
    }
    if (occupant != nullptr && occupant->side == player_side_) {
        // Spent, but not gone. A character who has already taken their turn
        // stays on the board and stays inspectable, and simply cannot be picked
        // up again. The cursor rests on them, the status line names them, and
        // the sheet opens on them. Said in words rather than by a press that
        // does nothing, because a button that silently refuses reads as a
        // broken button.
        if (occupant->has_acted) {
            const sheet::ContentName who =
                character_called(snapshot, occupant->id);
            write_message(who.c_str(), " HAS ALREADY ACTED");
            return;
        }
        selected_ = occupant->id;
        message_[0] = '\0';
        // And the menu opens on it. The front door is the press a player
        // already makes: a pick that only lit the character's reach would
        // leave everything it can be told to do behind a button nobody has
        // been told about. Every other press is a shortcut for a row the menu
        // names, walking onto a lit tile or striking whoever the cursor rests
        // on. CANCEL closes the menu and leaves the character in hand, picked
        // up with its reach lit and nothing chosen.
        open_menu(snapshot, *occupant);
        return;
    }
    if (selected_ != 0 && occupant == nullptr) {
        pending_.kind = client::IntentKind::move_to;
        pending_.unit_id = selected_;
        pending_.destination = {cursor_x_, cursor_y_};
        // And the menu opens again behind the walk, on a board where the walk
        // left the turn open. Consumed by `next_intent` once the engine has
        // taken the command, because a menu opened before then would be a menu
        // over the board the character has already left.
        reopen_menu_ = selected_;
        // And the selection stays. A walk does not finish a character: the
        // point it walked with was one of its turn's and the action is the
        // other. So the next thing the player wants is almost always this
        // character's strike or its WAIT, and dropping the selection would make
        // them find it again first. `draw` lets it go the moment the engine
        // says the character is done, which is the only authority on that.
        //
        // This is the second half of what the cartridge report was about. The
        // first half was that a walk locked out everybody else; the other was
        // that nothing said what to do with the character that had walked.
    }
}

// Arranging, on the buttons this machine has.
//
// A picks a character up and puts it down, exactly as it selects and moves in
// the battle; B puts it back down without moving it; Start opens the battle,
// which is the button that ends an activation once one has begun and ends the
// arranging for the same reason: it is this machine's "I am done". C opens
// nothing, because there is no action menu before there are actions.
void TurnClient::deploy_press(
    const sim::EncounterSnapshot& snapshot, std::uint16_t buttons
) {
    if ((buttons & pad_end_of_script) != 0U) {
        pending_.kind = client::IntentKind::quit;
        return;
    }
    if ((buttons & pad_up) != 0U && cursor_y_ > 0) --cursor_y_;
    if ((buttons & pad_down) != 0U &&
        cursor_y_ + 1 < static_cast<int>(snapshot.height)) {
        ++cursor_y_;
    }
    if ((buttons & pad_left) != 0U && cursor_x_ > 0) --cursor_x_;
    if ((buttons & pad_right) != 0U &&
        cursor_x_ + 1 < static_cast<int>(snapshot.width)) {
        ++cursor_x_;
    }
    camera_.follow(cursor_x_, cursor_y_, 1);

    if ((buttons & pad_b) != 0U && selected_ != 0) {
        selected_ = 0;
        message_[0] = '\0';
        return;
    }
    if ((buttons & pad_start) != 0U) {
        deploying_ = false;
        selected_ = 0;
        pending_.kind = client::IntentKind::begin_battle;
        return;
    }
    if ((buttons & pad_a) == 0U) return;

    const sim::UnitSnapshot* occupant = unit_at(snapshot, cursor_x_, cursor_y_);
    if (occupant != nullptr && sim::is_deployable(snapshot, *occupant) &&
        occupant->side == player_side_) {
        selected_ = occupant->id;
        message_[0] = '\0';
        return;
    }
    if (selected_ != 0 && occupant == nullptr) {
        pending_.kind = client::IntentKind::deploy_to;
        pending_.unit_id = selected_;
        pending_.destination = {cursor_x_, cursor_y_};
    }
}

// The unit action menu: one row per thing this character can be told to do.
//
// Its order is the order of the question a player is answering: strike with
// what I carry, cast what I know, or stop here. Every row is derived from
// the character rather than from the tile the cursor happens to be on. A menu
// opened by aiming at an enemy would put WAIT under a cursor pointed at
// somebody, which reads as nonsense.
//
// A character carrying one weapon has one strike row and no choice to make
// about it, so that row carries no weapon identity at all. Absence is how the
// engine is told to use the weapon in hand.
//
// Room is reserved for WAIT, INFO and CANCEL before anything else is offered,
// so a character carrying more than the menu holds loses a spell rather than
// the three rows every menu has to end with. The rows that spend a carried
// item are built by a third loop between the spells and WAIT, out of the same
// room, and nothing above this line moved when they arrived.
//
// The tail's order is how much each row commits. WAIT spends the character's
// activation and is the last row that decides anything at all. INFO opens the
// character's own sheet and puts the player back in this menu; CANCEL puts
// them back on the board. Neither is a command, which is why INFO joined the
// tail rather than taking the item's row.
void TurnClient::open_menu(
    const sim::EncounterSnapshot& snapshot, const sim::UnitSnapshot& actor
) {
    menu_rows_ = 0;
    menu_row_ = 0;
    board_menu_open_ = false;
    // The cap on everything above the tail: the three rows every menu ends with
    // are reserved before anything is offered against this. `menu_capacity`
    // says why the number is what it is here and what it is on the Nintendo 64.
    // A character that may still walk spends the first of these on WALK; one
    // that has already walked has the whole of it for its own rows, which is
    // the menu getting shorter rather than narrower.
    const int offered = menu_capacity - 3;
    // The first thing a turn does, and so the first row. It names no tile:
    // taking it hands the player back the board with the engine's own aimable
    // set already lit under the cursor.
    //
    // **Offered only while the engine would take it**, through
    // `gesture_available`, which for a walk is "this character has not already
    // walked this turn".
    // Asked rather than tracked here, so a row the menu offers and a command
    // the engine refuses cannot disagree.
    //
    // Only this row moves, and the line it draws is between the gesture and its
    // aim. A walk after walking is refused before the engine looks at a
    // destination. ATTACK with nobody in reach is a fact about the board rather
    // than about the gesture, and it is what the amber highlight is there to
    // show; a row that came and went for a reason the board does not draw would
    // be teaching a rule nobody can see. An item row already prints its own
    // count, TALK is already offered only where `forecast_talk` says one could
    // land, and the three tail rows are never refused.
    if (sim::gesture_available(
            snapshot, actor.id, {sim::Gesture::walk, 0, 0}, weapons_,
            abilities_
        )) {
        MenuChoice& walk = menu_[menu_rows_++];
        walk = MenuChoice{};
        walk.label = "WALK";
        walk.move = true;
    }
    if (actor.weapon_ids.size() >= 2) {
        for (std::size_t i = 0;
             i < actor.weapon_ids.size() && menu_rows_ < offered; ++i) {
            MenuChoice& row = menu_[menu_rows_++];
            row = MenuChoice{};
            row.label = sheet::weapon_name(actor.weapon_ids[i]);
            row.attack = true;
        row.content = i == 0 ? 0 : actor.weapon_ids[i];
        }
    } else {
        MenuChoice& row = menu_[menu_rows_++];
        row = MenuChoice{};
        row.label = "ATTACK";
        row.attack = true;
    }
    for (std::size_t i = 0;
         i < actor.ability_ids.size() && menu_rows_ < offered; ++i) {
        MenuChoice& row = menu_[menu_rows_++];
        row = MenuChoice{};
        row.label = sheet::ability_name(actor.ability_ids[i]);
        row.cast = true;
        row.content = actor.ability_ids[i];
    }
    // The pack. A row that has run out is still offered and still says so:
    // the engine is what refuses it, exactly as aiming a strike at empty
    // ground sends the command and earns its refusal rather than being
    // swallowed here.
    for (std::size_t i = 0;
         i < actor.item_ids.size() && menu_rows_ < offered &&
         i < static_cast<std::size_t>(menu_item_labels);
         ++i) {
        MenuChoice& row = menu_[menu_rows_++];
        row = MenuChoice{};
        char* label = item_labels_[i];
        Text text;
        text.text(sheet::item_name(actor.item_ids[i]))
            .text(" x")
            .number(static_cast<std::int32_t>(actor.item_counts[i]));
        std::size_t j = 0;
        const char* source = text.c_str();
        while (source[j] != '\0' &&
               j + 1 < static_cast<std::size_t>(menu_item_label_size)) {
            label[j] = source[j];
            ++j;
        }
        label[j] = '\0';
        row.label = label;
        row.use = true;
        row.content = actor.item_ids[i];
    }
    // TALK, after the pack and before WAIT. The rows of this menu are ordered
    // by what each one costs, and a talk costs exactly what a strike costs: one
    // action point, and the activation closes behind it. So it belongs among
    // the rows that spend the turn rather than below WAIT with the rows that
    // spend nothing. It sits below the items because an item is the older
    // gesture, and a menu that reshuffles its familiar rows to make room for a
    // new one is a menu the player has to relearn. The Nintendo 64 puts it in
    // the same place for the same reason.
    //
    // Whether it is offered at all is the engine's answer, not this file's. No
    // shipped board authors a talk, so no shipped menu grows this row.
    if (menu_rows_ < offered && any_talkable_neighbour(snapshot, actor)) {
        MenuChoice& talk = menu_[menu_rows_++];
        talk = MenuChoice{};
        talk.label = "TALK";
        talk.talk = true;
    }
    // It says END CHARACTER TURN and not WAIT. WAIT is the genre's word and it
    // is the wrong one to meet first: it says what the character does and not
    // what it costs. The word it must not be confused with is the board menu's
    // end of the side's turn, which finishes every character at once. So each
    // row names the scope it ends, and the two differ on exactly that word.
    MenuChoice& wait = menu_[menu_rows_++];
    wait = MenuChoice{};
    wait.label = "END CHARACTER TURN";
    wait.wait = true;
    MenuChoice& info = menu_[menu_rows_++];
    info = MenuChoice{};
    info.label = "INFO";
    info.info = true;
    MenuChoice& cancel = menu_[menu_rows_++];
    cancel = MenuChoice{};
    cancel.label = "CANCEL";
    cancel.cancel = true;
    menu_open_ = true;
}

// Adjacency, an authored record, still standing, not already gone: every one of
// those is `forecast_talk`'s question, asked on exactly the terms `apply` would
// ask it. Spelling the same test out here would be a second copy of the rule,
// and the first copy would go on changing without it.
bool TurnClient::any_talkable_neighbour(
    const sim::EncounterSnapshot& snapshot, const sim::UnitSnapshot& actor
) const noexcept {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == actor.id) continue;
        if (sim::forecast_talk(snapshot, actor.id, unit.id)) return true;
    }
    return false;
}

// What a row means. Waiting is the only row that is a command on its own;
// striking and casting both close the menu and hand the player back the
// cursor, because the character has said what it will do and not yet where.
//
// INFO is the one row that leaves the menu standing: it opens the character's
// sheet on top and comes back to the same caret, because a player who looked
// something up is in the middle of choosing rather than finished.
// The board's own menu, on START, at any point in a battle.
//
// It answers the questions that are about the battle rather than about one of
// its characters: end this side's turn, and leave. A character's own questions
// are the unit action menu's: walk, strike, cast, drink, finish. They are one
// press away on a character the player has picked up, and neither menu holds a
// row belonging to the other.
//
// The first row is the way out of it. A menu opened by a button a player
// pressed to find out what it did should say how to undo that before it says
// anything else; B backs out of it too, as B backs out of everything here, but
// a row a player can read is not the same as a button they have to know.
void TurnClient::open_board_menu() {
    // The selection is deliberately left standing. Opening this menu is a
    // question about the battle, and a player who asks one and backs out
    // should find the board exactly as they left it, with the character they
    // had in hand still in hand. Only the row that ends the side's turn puts
    // it down, because that row finishes the character holding it.
    menu_rows_ = 0;
    menu_row_ = 0;
    MenuChoice& back = menu_[menu_rows_++];
    back = MenuChoice{};
    back.label = "BACK TO BATTLE";
    back.cancel = true;
    // The side, by the name its own status line spells across the top of the
    // screen. A player reading YOUR TURN there is being offered the end of that
    // same turn in the same word. That word is what keeps this row from
    // reading as the character menu's END CHARACTER TURN, which ends one
    // activation and not the side's.
    //
    // This menu only ever ends the side the player holds, so the row does not
    // ask which side that is: it is theirs either way.
    MenuChoice& side = menu_[menu_rows_++];
    side = MenuChoice{};
    side.label = "END YOUR TURN";
    side.wait = true;
    // What leaving costs, in the row rather than behind it. A battle in
    // progress is not written anywhere. A save on these machines holds a
    // *campaign*: the company, its store, what every battle did to it. And
    // `client::run_persistent_campaign` is explicit that an unfinished fight is
    // not an outcome and commits nothing. So there is no SAVE row here, because
    // the only honest one would say "your campaign is already saved and this
    // board is not", which is what this row says in the words it has.
    MenuChoice& leave = menu_[menu_rows_++];
    leave = MenuChoice{};
    leave.label = "LEAVE - THIS BATTLE IS NOT KEPT";
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    // And the Stage picker, when there is one. The row exists exactly when the
    // session handed this board a list of Stages, which it does only in a build
    // that carries the picker: nothing here reads the define, and an ordinary
    // image never draws this row.
    //
    // Last, under the way out, because it is a testing aid and not one of the
    // two questions this menu exists to answer. Its label says what it costs in
    // the row rather than behind it, exactly as the row above it does: leaving
    // for another Stage abandons this battle the same way leaving does.
    if (board_ != nullptr && !board_->stages.empty()) {
        MenuChoice& jump = menu_[menu_rows_++];
        jump = MenuChoice{};
        jump.label = "GO TO ANOTHER STAGE - TESTING";
        jump.stage = true;
    }
#endif
    menu_open_ = true;
    board_menu_open_ = true;
}

// The row the caret was on when A was pressed, out of the board menu.
//
// Deliberately not folded into `commit_menu_row`: the two menus share a caret
// and a renderer and share nothing else, and a row that ends a side's turn
// beside a row that ends a character's is the one place the two could be
// confused. `wait` on a board row means the side; on a character row it means
// the character; and neither switch can reach the other's rows.
void TurnClient::commit_board_row(const sim::EncounterSnapshot& snapshot) {
    if (menu_row_ < 0 || menu_row_ >= menu_rows_) return;
    const MenuChoice row = menu_[menu_row_];
    menu_open_ = false;
    board_menu_open_ = false;
    menu_rows_ = 0;
    if (row.cancel) return;
    if (row.wait) {
        // Every character on this side that still owes the board an activation
        // is finished, one `wait` at a time, by `drain_side`. Nothing is
        // decided here beyond that the player asked for it.
        finishing_ = true;
        drain_last_ = 0;
        // And the first of them at once. `next_intent` drains before it reads a
        // press, which serves every command after this one; this press is
        // already inside that loop, so without draining here the flag would sit
        // set while the loop went on asking for buttons.
        drain_side(snapshot);
        return;
    }
#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN
    if (row.stage) {
        stages_ = board_ != nullptr ? &board_->stages : nullptr;
        const std::uint64_t chosen = choose_a_stage();
        stages_ = nullptr;
        // Backing out of the picker puts the player back on the board with
        // whatever they had in hand, exactly as backing out of this menu does.
        // Only a Stage actually chosen leaves the battle.
        if (chosen == 0U) return;
        pending_.kind = client::IntentKind::jump_to_stage;
        pending_.stage_id = chosen;
        return;
    }
#endif
    pending_.kind = client::IntentKind::quit;
}

// One character of the side the player asked to finish, per call.
//
// `client::unfinished_unit` names whoever still owes the board an activation
// and answers zero when nobody does, so the drain stops of its own accord
// rather than on a count kept here. That is what makes it right under all
// three turn orders. Under `alternating` a side holds one activation, so this
// sends one `wait` and the turn passes; under `side_blocks` it sends one per
// character still standing idle, which is what "end the side's turn" has to
// mean on a board where each of them holds their own.
void TurnClient::drain_side(const sim::EncounterSnapshot& snapshot) {
    if (!finishing_) return;
    const sim::UnitId owed = client::unfinished_unit(snapshot, player_side_);
    if (owed == 0 || owed == drain_last_) {
        finishing_ = false;
        drain_last_ = 0;
        return;
    }
    drain_last_ = owed;
    selected_ = 0;
    clear_aim();
    reopen_menu_ = 0;
    pending_.kind = client::IntentKind::wait;
    pending_.unit_id = owed;
}

void TurnClient::commit_menu_row(const sim::EncounterSnapshot& snapshot) {
    if (menu_row_ < 0 || menu_row_ >= menu_rows_) return;
    const MenuChoice row = menu_[menu_row_];
    if (row.info) {
        const sim::UnitSnapshot* actor = unit_by_id(snapshot, selected_);
        if (actor == nullptr) return;
        // Composed from the snapshot and the registries this encounter was
        // created with, in the one place every client composes it. Nothing
        // about the sheet is worked out on the console, its first row least of
        // all: who this is comes from the one resolver every client asks.
        sheet_ = sheet::build(
            snapshot, *actor, character_called(snapshot, selected_).c_str(),
            weapons_, abilities_, items_, nullptr, package_
        );
        sheet_open_ = true;
        return;
    }
    menu_open_ = false;
    menu_rows_ = 0;
    if (row.cancel) return;
    if (row.wait) {
        pending_.kind = client::IntentKind::wait;
        pending_.unit_id = selected_;
        selected_ = 0;
        clear_aim();
        return;
    }
    if (row.use) {
        pending_.kind = client::IntentKind::use_item;
        pending_.unit_id = selected_;
        pending_.item_id = row.content;
        selected_ = 0;
        clear_aim();
        return;
    }
    if (row.move) {
        // Handed back to the cursor, over tiles the board is already lighting.
        // The renderer paints the engine's own `reachable_tiles` for whoever is
        // selected, so taking this row adds no highlight and invents no rule:
        // it renames what the next A press means and says so on the prompt.
        aim_ = Aim::walk;
        aim_ability_ = 0;
        aim_weapon_ = 0;
        write_aim_text("WALK WHERE", nullptr);
        return;
    }
    if (row.talk) {
        // Aimed rather than committed, even when only one neighbour could
        // answer. A talk names somebody, and the gesture that names somebody on
        // this machine is the cursor. So the row hands the player back the
        // board exactly as a strike does, and whoever the cursor lands on is
        // the engine's to judge. Committing straight at a single candidate
        // would have made one row mean two different gestures depending on how
        // many people happened to be standing nearby, which is a rule a player
        // would have to be told. The Nintendo 64 makes the same choice.
        aim_ = Aim::talk;
        aim_ability_ = 0;
        aim_weapon_ = 0;
        write_aim_text("TALK TO WHOM", nullptr);
        return;
    }
    if (row.cast) {
        aim_ = Aim::cast;
        aim_ability_ = row.content;
        aim_weapon_ = 0;
        write_aim_text("AIM ", row.label);
        return;
    }
    aim_ = Aim::strike;
    aim_ability_ = 0;
    aim_weapon_ = row.content;
    // A named weapon is worth repeating back, because the player picked it out
    // of several and the cursor is about to be somewhere else. The weapon in
    // hand needs no name: it is the strike the plain gesture already makes.
    if (row.content != 0) {
        write_aim_text("STRIKE WITH ", row.label);
    } else {
        write_aim_text("PICK A TARGET", nullptr);
    }
}

// The sentence under the board, written the way `write_aim_text` writes the
// prompt above it: two pieces, one buffer, truncated rather than allocated.
// Survives until the cursor gives the line something else to say, which is the
// rule a refusal already keeps.
void TurnClient::write_message(const char* prefix, const char* label) noexcept {
    std::size_t i = 0;
    for (const char* source : {prefix, label}) {
        while (source != nullptr && *source != '\0' &&
               i + 1 < sizeof message_) {
            message_[i++] = *source++;
        }
    }
    message_[i] = '\0';
}

void TurnClient::write_aim_text(const char* prefix, const char* label) noexcept {
    std::size_t i = 0;
    for (const char* source : {prefix, label}) {
        while (source != nullptr && *source != '\0' &&
               i + 1 < sizeof aim_text_) {
            aim_text_[i++] = *source++;
        }
    }
    aim_text_[i] = '\0';
}

// Where the pick lands. Nothing here asks whether it may: a cast goes to the
// tile under the cursor and a strike goes at whoever is standing there, and
// the engine answers both, including the empty tile, which is refused as
// having no target rather than swallowed as a button that did nothing.
void TurnClient::commit_aim(const sim::EncounterSnapshot& snapshot) {
    const Aim aim = aim_;
    pending_.unit_id = selected_;
    if (aim == Aim::walk) {
        // The one aim that keeps its character. A walk does not finish anybody:
        // the point it spent was the turn's walk and the action is still in
        // hand. So the selection stands and the menu opens again on the far
        // side of it, which is the whole reason the row exists: the player is
        // asked what this character does next instead of being handed a board
        // and left to guess.
        pending_.kind = client::IntentKind::move_to;
        pending_.destination = {cursor_x_, cursor_y_};
        clear_aim();
        reopen_menu_ = selected_;
        return;
    }
    if (aim == Aim::cast) {
        // An ability is aimed at a tile, never at a unit: which tiles an area
        // covers is the engine's business.
        pending_.kind = client::IntentKind::ability;
        pending_.destination = {cursor_x_, cursor_y_};
        pending_.ability_id = aim_ability_;
    } else if (aim == Aim::talk) {
        // Whoever is standing there, named and handed over. Empty ground names
        // nobody and earns `unknown_target`; somebody with nothing to say earns
        // `not_talkable`; somebody who already walked away earns
        // `target_departed`. All three are the engine's to say.
        const sim::UnitSnapshot* occupant =
            unit_at(snapshot, cursor_x_, cursor_y_);
        pending_.kind = client::IntentKind::talk;
        pending_.target_id = occupant != nullptr ? occupant->id : 0;
    } else {
        const sim::UnitSnapshot* occupant =
            unit_at(snapshot, cursor_x_, cursor_y_);
        pending_.kind = client::IntentKind::attack;
        pending_.target_id = occupant != nullptr ? occupant->id : 0;
        pending_.weapon_id = aim_weapon_;
    }
    selected_ = 0;
    clear_aim();
}

void TurnClient::clear_aim() noexcept {
    aim_ = Aim::none;
    aim_weapon_ = 0;
    aim_ability_ = 0;
    aim_text_[0] = '\0';
}

// Whoever holds the tile, by the engine's own predicate. Occupancy is
// `sim::on_board` and nothing else: a character talked off the board or one
// still marching towards it keeps a `position`, but it is the tile content
// asked for rather than a tile anybody holds, and the engine refuses every
// command aimed at them. Spelling the test as `health > 0` here would hand back
// a target the board then refuses, which is the same drift that makes reach the
// engine's question to answer rather than this client's.
const sim::UnitSnapshot* TurnClient::unit_at(
    const sim::EncounterSnapshot& snapshot, int x, int y
) const noexcept {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (sim::on_board(unit) && unit.position.x == x &&
            unit.position.y == y) {
            return &unit;
        }
    }
    return nullptr;
}

const sim::UnitSnapshot* TurnClient::unit_by_id(
    const sim::EncounterSnapshot& snapshot, sim::UnitId id
) const noexcept {
    if (id == 0) return nullptr;
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == id) return &unit;
    }
    return nullptr;
}

#ifdef GRANDLEON_TURN_CLIENT_CAMPAIGN

// ---------------------------------------------------------------------------
// The campaign
//
// Everything below is `client::CampaignNarrator`. Not one number on any of
// these screens is worked out here: the roster, the kits, the store, the levels
// and every consequence of a battle arrive from the session exactly as
// `campaign_runtime` derived them, and this arranges them into forty-column
// lines. That is the same division the board already keeps, and it is why the
// terminal, the browser, the Nintendo 64 and this machine can never disagree
// about what a campaign holds.
// ---------------------------------------------------------------------------

namespace {

// The word this console uses for an availability. The Nintendo 64's table,
// because a player who has seen one console's company should recognise the
// other's.
//
// `dead` says LOST and not FALLEN, because the two words are two different
// facts: somebody who *fell* is down and out of the battle and is coming back,
// and somebody who is *lost* is not. `Availability::dead` is
// reachable only through `record_permanent_death`, which only a campaign playing
// under the permanent rule ever commits, so this row is never the softer of the
// two and must never be worded as though it might be.
[[nodiscard]] const char* availability_word(campaign::Availability where) noexcept {
    switch (where) {
        case campaign::Availability::unrecruited: return "AWAY";
        case campaign::Availability::available: return "FIELD";
        case campaign::Availability::retired: return "BENCH";
        case campaign::Availability::dead: return "LOST";
    }
    return "?";
}

// Which layer refused the slot, in that layer's own word for it. Four channels
// rather than one string is `SlotFailure`'s design, and this reads whichever is
// set rather than flattening them into a house sentence.
[[nodiscard]] const char* refusal_reason(const client::SlotFailure& failure) noexcept {
    if (failure.wrong_campaign) return "IT BELONGS TO ANOTHER STORY";
    if (failure.storage != storage::StorageError::none) {
        return "THE CARTRIDGE COULD NOT BE READ";
    }
    if (failure.save != campaign::SaveError::none) {
        return "THE SAVE IS NOT ONE THIS GAME READS";
    }
    if (failure.migration != campaign::MigrationError::none) {
        return "IT IS FROM AN OLDER VERSION";
    }
    if (failure.state != campaign::StateError::none) {
        return "THE CAMPAIGN IN IT DOES NOT ADD UP";
    }
    return "IT COULD NOT BE READ";
}

// What the campaign calls a member, out of a roster it has already been handed.
//
// The roster a commit leaves behind still holds everybody the campaign knows,
// the buried included, so the name of somebody who fell is read out of the same
// list as the name of somebody who did not. A member no roster holds is named by
// a word rather than by a number: the identity is 64 bits wide and this
// transcript is compared on a machine with no 64-bit arithmetic.
[[nodiscard]] const char* member_name(
    const std::vector<client::RosterEntry>& roster,
    campaign::PersistentEntityId member
) noexcept {
    for (const client::RosterEntry& entry : roster) {
        if (entry.member.value == member.value) return entry.name.c_str();
    }
    return "SOMEBODY";
}

// Whether one more member would carry this board past its authored capacity.
// An early copy of `join_campaign_roster`'s own gate and never a substitute for
// it: the engine refuses an over-cap company however this screen counted.
[[nodiscard]] bool field_is_full(
    const client::CompanyManagement& company
) noexcept {
    return company.capacity != 0U &&
           company.fielded.size() >=
               static_cast<std::size_t>(company.capacity);
}

[[nodiscard]] std::uint32_t carried_total(
    const std::vector<campaign::InventoryStack>& held
) noexcept {
    std::uint32_t total = 0;
    for (const campaign::InventoryStack& stack : held) total += stack.quantity;
    return total;
}

// Writes at most `width` characters of `text` and then spaces out to `width`,
// so a company row's fields line up under each other without a format string.
// `snprintf` would be a second implementation of a line the host and the
// console have to agree about character for character.
void push_padded(Text& into, const char* text, int width) noexcept {
    int written = 0;
    for (; text != nullptr && text[written] != '\0' && written < width;
         ++written) {
        const char one[2] = {text[written], '\0'};
        into.text(one);
    }
    for (int i = written; i < width; ++i) into.space();
}

}  // namespace

const char* screen_name(Screen screen) noexcept {
    switch (screen) {
        case Screen::none: return "none";
        case Screen::title: return "title";
        case Screen::slot: return "slot";
        case Screen::story: return "story";
        case Screen::company: return "company";
        case Screen::member: return "member";
        case Screen::aftermath: return "aftermath";
        case Screen::refusal: return "refusal";
        case Screen::joined: return "joined";
        case Screen::stages: return "stages";
        case Screen::ended: return "ended";
    }
    return "none";
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void TurnClient::begin_page() {
    page_.count = 0;
    page_backdrop_ = 0;
    page_speaker_ = 0;
    page_has_speaker_ = false;
    page_first_choice_ = 0;
    page_choices_ = 0;
    verb_count_ = 0;
    // The window, but not the caret and not where the window starts: a page is
    // rebuilt *because* the caret moved the window, so a rebuild that put those
    // back would undo the move it was answering. Whoever opens a new list is
    // the one that resets them.
    list_ = {};
    list_page_ = ListPage::none;
    for (int row = 0; row < page_capacity; ++row) page_.lines[row][0] = '\0';
}

// One row, in the only characters this display has.
//
// The font is ASCII 0x20 to 0x5F, which is exactly the range
// `grandleon/view/glyphs.hpp` holds, so a lower-case letter is drawn as its
// capital and anything outside the range is drawn as a space. Doing that here
// rather than in the renderer is what makes the transcript the text a player
// sees: a line the host wrote in mixed case and the console drew in capitals
// would be two spellings of one screen.
void TurnClient::push_line(const char* text, int limit) {
    if (page_.count >= page_capacity) return;
    char* into = page_.lines[page_.count];
    int length = 0;
    for (int i = 0; text != nullptr && (limit < 0 || i < limit) &&
                    text[i] != '\0' && length < page_columns;
         ++i) {
        char value = text[i];
        if (value >= 'a' && value <= 'z') {
            value = static_cast<char>(value - ('a' - 'A'));
        }
        if (value < 0x20 || value > 0x5F) value = ' ';
        into[length++] = value;
    }
    // Trailing spaces are trimmed, because a line's width is the box the
    // renderer draws and never the text's own: a page whose rows differed by
    // invisible padding would be a page two machines could disagree about
    // without anything being visibly wrong.
    while (length > 0 && into[length - 1] == ' ') --length;
    into[length] = '\0';
    ++page_.count;
}

void TurnClient::push_line(const char* text) { push_line(text, -1); }

// A page fills, is read, and the next row starts the page after it.
//
// Without this, a page were the ceiling of what could be *said* rather than of
// what could be shown at once, and an authored line past the ceiling would lose
// its tail with nothing on screen to admit it. `hold_page` is the same wait the
// screen between two speakers already makes the reader do, so a long saying
// reads as several pages of one speech rather than as a truncated one.
void TurnClient::push_story_line(
    const char* text, int length, std::uint8_t backdrop
) {
    if (page_.count >= page_capacity) {
        hold_page(Screen::story, "A  GO ON");
        begin_page();
        // A backdrop belongs to the scene, and `begin_page` cleared it. A
        // continuation page that dropped it would change the picture halfway
        // through a sentence.
        page_backdrop_ = backdrop;
        // And the speaker belongs to the saying, which is what is being
        // continued. A continuation page that dropped it would take the face
        // away mid-sentence, which reads as somebody else finishing it.
        page_speaker_ = story_speaker_;
        page_has_speaker_ = story_has_speaker_;
    }
    push_line(text, length);
}

int wrapped_rows(const char* text) noexcept {
    int rows = 0;
    int column = 0;
    int word = 0;
    for (int i = 0;; ++i) {
        const char value = text != nullptr ? text[i] : '\0';
        if (value != ' ' && value != '\0') {
            ++word;
            continue;
        }
        if (word > 0) {
            // A word that will not follow what is already on this row closes
            // it and starts one of its own.
            if (column > 0 && column + 1 + word > page_columns) {
                ++rows;
                column = 0;
            }
            if (column == 0) {
                // At the head of a row, a word wider than the page fills whole
                // rows until what is left of it fits on one. `push_wrapped`
                // breaks it in exactly these places, which is why this count
                // is still the number of rows that will be emitted.
                while (word > page_columns) {
                    ++rows;
                    word -= page_columns;
                }
                column = word;
            } else {
                column += 1 + word;
            }
            word = 0;
        }
        if (value == '\0') break;
    }
    if (column > 0) ++rows;
    return rows == 0 ? 1 : rows;
}

// A speaker and what they said, wrapped on spaces to the display's width and
// paged when the saying is taller than a page.
//
// Wrapped rather than truncated because an authored line is content: a console
// that showed the first forty characters of it would be a console showing a
// different story than the terminal does. And a word wider than the whole page
// is broken across rows for the same reason, rather than laid on a row it
// overhangs and clipped there: a link, an identifier, a compound nobody thought
// to hyphenate. There is nowhere better to break it: this display has no
// hyphenation and no proportional metrics, only thirty-eight cells.
//
// The word is measured where the author wrote it and copied straight out of
// `text`, never accumulated into a builder. A builder has a capacity, and the
// length of a word is the author's business.
void TurnClient::push_wrapped(
    const char* speaker, const char* text, std::uint8_t backdrop
) {
    if (speaker != nullptr && speaker[0] != '\0') {
        Text who;
        who.text(speaker).text(":");
        push_story_line(who.c_str(), -1, backdrop);
    }
    // The row under construction. It never holds more than a row, because
    // nothing is put in it until it is known to fit.
    char row[page_columns + 1];
    int row_length = 0;
    int word_start = 0;
    int word_length = 0;
    for (int i = 0;; ++i) {
        const char value = text != nullptr ? text[i] : '\0';
        if (value != ' ' && value != '\0') {
            if (word_length == 0) word_start = i;
            ++word_length;
            continue;
        }
        if (word_length > 0) {
            if (row_length > 0 && row_length + 1 + word_length > page_columns) {
                row[row_length] = '\0';
                push_story_line(row, -1, backdrop);
                row_length = 0;
            }
            if (row_length == 0) {
                // At the head of a row, and still too wide for one: whole rows
                // come off the front of the word until the rest fits, and the
                // rest becomes the row this one continues on. `wrapped_rows`
                // counts exactly these.
                while (word_length > page_columns) {
                    push_story_line(text + word_start, page_columns, backdrop);
                    word_start += page_columns;
                    word_length -= page_columns;
                }
            } else {
                // One space between two words, whatever run of them the author
                // typed: a row's width is the box the renderer draws.
                row[row_length++] = ' ';
            }
            for (int j = 0; j < word_length; ++j) {
                row[row_length++] = text[word_start + j];
            }
            word_length = 0;
        }
        if (value == '\0') break;
    }
    if (row_length > 0) {
        row[row_length] = '\0';
        push_story_line(row, -1, backdrop);
    }
}

// The company, in the one shape both the screen it opens on and the stage
// between battles use. Which of the two it is shows in the heading and in
// whether a caret is on it, and in nothing else, because they are the same
// company.
void TurnClient::compose_company(
    const char* heading,
    const std::vector<client::RosterEntry>& roster,
    const std::vector<campaign::InventoryStack>& store,
    const client::CompanyManagement* company
) {
    begin_page();
    company_heading_ = heading;
    // The roster's window, and the caret's, when there is a caret. The
    // management stage is the only screen with one, because the page the
    // campaign is handed over on is read rather than steered. So the screen the
    // campaign opens on pins its window to the top and names what is past it
    // rather than following anything.
    list_.rows = company_roster_rows;
    list_.total = static_cast<int>(roster.size());
    if (company != nullptr) {
        list_page_ = ListPage::company;
        list_.top = list_top_;
        list_.follow(list_caret_, company_scroll_margin);
        list_top_ = list_.top;
    } else {
        list_.top = 0;
        list_.clamp();
    }
    {
        // The legend rides the heading rather than taking a row of its own: a
        // row is a member on a sixteen-row page, and the heading has thirty-
        // eight columns and uses seventeen of them. A window that is not
        // scrolling writes nothing, so a company that fits gets the heading it
        // always had, to the byte.
        Text line;
        line.text(heading);
        char legend[view::scroll_legend_size];
        if (view::scroll_legend(list_, legend, sizeof legend) > 0) {
            line.text("   ").text(legend);
        }
        push_line(line.c_str());
    }
    // A refusal this screen decided goes under the heading rather than at the
    // foot, because the foot is where the store runs to and a page that fills
    // is a page whose last row is the one dropped. Said once and then gone, so
    // the next gesture is not answered by the last one's refusal.
    if (pending_refusal_ != nullptr) {
        Text refused;
        refused.text("REFUSED ").text(pending_refusal_);
        push_line(refused.c_str());
        pending_refusal_ = nullptr;
    } else if (company != nullptr &&
               company->refused != campaign_runtime::RosterError::none) {
        // And a refusal the *session* decided, in the same place and the same
        // words. `prepare_board` publishes nothing when the roster refuses a
        // board and says why in the roster's own vocabulary; until there was a
        // way to reach a board the company cannot be arranged into, the only
        // refusals a player could cause were ones this screen caught first, and
        // this line had nothing to draw. A Stage picker makes the session's
        // refusal ordinary, and a screen that showed a player nothing but the
        // board failing to open would be telling them their game was broken.
        Text refused;
        refused.text("REFUSED ").text(
            campaign_runtime::roster_error_name(company->refused).data()
        );
        push_line(refused.c_str());
    }
    push_line("");
    page_first_choice_ = page_.count;
    page_choices_ = list_.shown();
    for (int index = list_.top; index < list_.end(); ++index) {
        const client::RosterEntry& member =
            roster[static_cast<std::size_t>(index)];
        Text row;
        push_padded(row, member.name.c_str(), 15);
        row.text("L").number(static_cast<std::int32_t>(member.progression.level));
        row.space();
        push_padded(row, availability_word(member.availability), 7);
        row.text("KIT ").number(
            static_cast<std::int32_t>(carried_total(member.carried))
        );
        push_line(row.c_str());
    }
    // How many of the company this board's author lets out, against how many
    // would go as it stands. A line only a capped board has: a board that caps
    // nothing is not counting anything, so its page is the page it always was
    // rather than one that says NO LIMIT.
    if (company != nullptr && company->capacity != 0U) {
        Text counted;
        counted.text("FIELDED ")
            .number(static_cast<std::int32_t>(company->fielded.size()))
            .text(" OF ")
            .number(static_cast<std::int32_t>(company->capacity));
        push_line(counted.c_str());
    }
    push_line("");
    // The store keeps its place. It does not scroll with the roster, is never
    // displaced by it, and has a window of its own with nothing steering it.
    // The caret is on the roster, and a second thing moving under a thumb
    // steering the first is a screen a player cannot read. The roster's
    // window being a fixed seven rows is what makes "its place" a place: the
    // store's heading lands on the same page row whether the company is four
    // or four hundred.
    //
    // Its own overflow is named rather than dropped: a row past the limit that
    // was simply not drawn would leave nothing on screen to say so. The stacks
    // themselves are reached on a member's menu, which scrolls under its own
    // caret for the same reason the roster does.
    view::ListWindow shelf;
    shelf.rows = company_store_rows;
    shelf.total = static_cast<int>(store.size());
    shelf.top = 0;
    {
        Text line;
        line.text("THE COMPANY'S STORE");
        char legend[view::scroll_legend_size];
        if (view::scroll_legend(shelf, legend, sizeof legend) > 0) {
            line.text("  ").text(legend);
        }
        push_line(line.c_str());
    }
    if (store.empty()) {
        push_line("NOTHING BUT WHAT THEY CARRY");
        return;
    }
    for (int index = shelf.top; index < shelf.end(); ++index) {
        const campaign::InventoryStack& stack =
            store[static_cast<std::size_t>(index)];
        Text row;
        push_padded(row, sheet::item_name(stack.item.stable_id), 20);
        row.text("X").number(static_cast<std::int32_t>(stack.quantity));
        push_line(row.c_str());
    }
}

// One member's verbs, aimed at nothing, in the order that taking a row costs
// something: the availability row first because it is about the next board, the
// moves after it because they are about a satchel, CANCEL last because it
// spends nothing.
//
// A row for an item the store cannot pay is not offered, because unlike a
// strike at empty ground there is nothing to refuse: the store either holds one
// or does not, and the screen showing what it holds is the same screen.
void TurnClient::compose_member_menu(
    const client::CompanyManagement& company, int row
) {
    const client::RosterEntry& member =
        company.roster[static_cast<std::size_t>(row)];
    // The page first, because `begin_page` is also what empties the verb list:
    // building the rows and then clearing them would be a menu with nothing in
    // it and a caret that could still be moved.
    begin_page();

    const bool fielded =
        member.availability == campaign::Availability::available;
    // A member the campaign has buried is on no board and takes no gift; the
    // campaign refuses both by name, and offering the rows would be offering a
    // press whose only outcome is a refusal.
    const bool actionable =
        member.availability != campaign::Availability::dead &&
        member.availability != campaign::Availability::unrecruited;

    const auto add = [this](
        const char* label, client::ManagementVerb verb,
        const campaign::DefinitionRef& item
    ) {
        if (verb_count_ >= company_menu_capacity) return;
        CompanyChoice& choice = verbs_[verb_count_++];
        choice = CompanyChoice{};
        int length = 0;
        while (label[length] != '\0' && length + 1 < company_menu_label_size) {
            choice.label[length] = label[length];
            ++length;
        }
        choice.label[length] = '\0';
        choice.verb = verb;
        choice.item = item;
    };

    if (actionable) {
        // The row stays offered when the board is full, because taking it is
        // how a player is told why, and a row that vanished would leave the
        // page saying nothing about the one member it concerns.
        const bool full = !fielded && field_is_full(company);
        add(fielded ? "SIT THIS ONE OUT"
            : full  ? "TAKE THE FIELD (FULL)"
                    : "TAKE THE FIELD",
            fielded ? client::ManagementVerb::bench
                    : client::ManagementVerb::field,
            {});
        for (const campaign::InventoryStack& stack : company.store) {
            if (verb_count_ + 1 >= company_menu_capacity) break;
            Text label;
            label.text("GIVE ")
                .text(sheet::item_name(stack.item.stable_id))
                .text(" X")
                .number(static_cast<std::int32_t>(stack.quantity));
            add(label.c_str(), client::ManagementVerb::give, stack.item);
        }
        for (const campaign::InventoryStack& stack : member.carried) {
            if (verb_count_ + 1 >= company_menu_capacity) break;
            Text label;
            label.text("TAKE ")
                .text(sheet::item_name(stack.item.stable_id))
                .text(" X")
                .number(static_cast<std::int32_t>(stack.quantity));
            add(label.c_str(), client::ManagementVerb::take, stack.item);
        }
    }
    add("CANCEL", client::ManagementVerb::none, {});

    // The window over the verbs, and then the page. The rows are built before
    // any of the page is written because the legend that says how many there
    // are rides the stat line, and a legend cannot be written before the count
    // it names.
    list_page_ = ListPage::member;
    list_.rows = member_menu_rows;
    list_.total = verb_count_;
    list_.top = list_top_;
    list_.follow(list_caret_, company_scroll_margin);
    list_top_ = list_.top;

    push_line(member.name.c_str());
    {
        Text line;
        line.text("L")
            .number(static_cast<std::int32_t>(member.progression.level))
            .text("  XP ")
            .number(static_cast<std::int32_t>(member.progression.experience))
            .text("  ")
            .text(availability_word(member.availability))
            .text("  KIT ")
            .number(static_cast<std::int32_t>(carried_total(member.carried)));
        char legend[view::scroll_legend_size];
        if (view::scroll_legend(list_, legend, sizeof legend) > 0) {
            line.text("  ").text(legend);
        }
        push_line(line.c_str());
    }
    push_line("");
    page_first_choice_ = page_.count;
    page_choices_ = list_.shown();

    // The rows themselves, on the page, so a screen is one thing to draw and
    // the caret's row is the page's row.
    for (int index = list_.top; index < list_.end(); ++index) {
        push_line(verbs_[index].label);
    }
}

// A page the caret moved the window on. The composer is asked again with what
// it was asked with before, because the window it builds is a function of where
// the caret now is. A page nobody rebuilt would be a page whose rows
// disagreed with its caret.
// The campaign's Stages, one row each, in the order the session published them.
//
// Two words carry the whole of what a player needs before they choose. HERE is
// where the campaign is standing; SEEN is somewhere this playthrough has
// actually stood. A Stage with neither is one the campaign has never reached,
// and jumping to it is the risky move: nothing that would have happened on the
// way has happened, so its board may name a character the company has not got
// and refuse to open at all. The line under the heading says that once, in the
// words an author was shown the setting under, rather than leaving a player to
// find it out by being stuck.
void TurnClient::compose_stages() {
    begin_page();
    list_page_ = ListPage::stages;
    const int total =
        stages_ == nullptr ? 0 : static_cast<int>(stages_->size());
    list_.rows = stage_menu_rows;
    list_.total = total;
    list_.top = list_top_;
    list_.follow(list_caret_, company_scroll_margin);
    list_top_ = list_.top;
    {
        Text line;
        line.text("GO TO ANOTHER STAGE");
        char legend[view::scroll_legend_size];
        if (view::scroll_legend(list_, legend, sizeof legend) > 0) {
            line.text("   ").text(legend);
        }
        push_line(line.c_str());
    }
    push_line("A STAGE YOU HAVE NOT SEEN MAY NOT OPEN");
    push_line("");
    page_first_choice_ = page_.count;
    page_choices_ = list_.shown();
    for (int index = list_.top; index < list_.end(); ++index) {
        const client::CampaignStage& stage =
            (*stages_)[static_cast<std::size_t>(index)];
        Text row;
        row.number(index + 1).space();
        // The author's name for the board, and the board's number when the
        // package has no name for it. A number is not much, but it is the
        // Stage's place in the flow and it is never nothing.
        push_padded(
            row,
            stage.name.empty() ? "STAGE" : stage.name.c_str(),
            28
        );
        if (stage.standing) {
            row.text("HERE");
        } else if (stage.reached) {
            row.text("SEEN");
        }
        push_line(row.c_str());
    }
}

std::uint64_t TurnClient::choose_a_stage() {
    if (stages_ == nullptr || stages_->empty()) return 0U;
    // A new list, so the caret starts at the top of it and so does the window.
    // Deliberately not where it was left: this screen is opened rarely and from
    // two places, and a caret remembering a row from the last time it was open
    // would be pointing at a Stage the player did not choose to look at.
    list_caret_ = 0;
    list_top_ = 0;
    compose_stages();
    const int chosen = choose_on_page(Screen::stages, "A GO  B BACK", pad_b);
    if (chosen < 0 || chosen >= static_cast<int>(stages_->size())) return 0U;
    return (*stages_)[static_cast<std::size_t>(chosen)].node_id;
}

void TurnClient::recompose_page() {
    switch (list_page_) {
        case ListPage::stages:
            compose_stages();
            break;
        case ListPage::company:
            if (company_ != nullptr) {
                compose_company(
                    company_heading_ != nullptr ? company_heading_ : "",
                    company_->roster, company_->store, company_
                );
            }
            break;
        case ListPage::member:
            if (company_ != nullptr) {
                compose_member_menu(*company_, manage_row_);
            }
            break;
        case ListPage::none:
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

ScreenView TurnClient::view_of(Screen screen, const char* footer) const {
    ScreenView view;
    view.screen = screen;
    view.page = &page_;
    const bool steerable = page_choices_ > 0;
    view.caret = steerable ? list_caret_ : -1;
    view.first_choice_row = page_first_choice_;
    view.choices = page_choices_;
    // Where the `>` goes: the page row the caret's item landed on. On a list
    // that fits this is `first_choice_row + caret`, which is what a renderer
    // computed for itself before there were windows.
    view.caret_row =
        steerable ? page_first_choice_ + list_.row_of(list_caret_) : -1;
    view.items = steerable ? list_.total : 0;
    view.window_top = steerable ? list_.top : 0;
    view.footer = footer;
    view.company = company_;
    view.verbs = verb_count_ > 0 ? verbs_ : nullptr;
    view.backdrop = page_backdrop_;
    view.speaker = page_has_speaker_ ? &page_speaker_ : nullptr;
    return view;
}

void TurnClient::note(const char* text) {
    if (note_count_ >= note_capacity) return;
    char* into = notes_[note_count_++];
    int length = 0;
    while (text[length] != '\0' && length + 1 < note_size) {
        into[length] = text[length];
        ++length;
    }
    into[length] = '\0';
}

// The transcript, for a screen. Same shape as a board's: a checkpoint, the
// facts, and an end. So the harness compares one kind of thing, and a screen
// that showed the wrong company fails on a line rather than on a count.
void TurnClient::screen_checkpoint(const ScreenView& view) {
    ++screens_;
    {
        Text line;
        line.text("CHECKPOINT s")
            .number(screens_)
            .text("-")
            .text(screen_name(view.screen));
        sink_.line(line.c_str());
    }
    for (int index = 0; index < note_count_; ++index) {
        Text line;
        line.text("FACT ").text(notes_[index]);
        sink_.line(line.c_str());
    }
    note_count_ = 0;
    {
        Text line;
        line.text("FACT screen ")
            .text(screen_name(view.screen))
            .text(" caret ")
            .number(view.caret)
            .text(" choices ")
            .number(view.choices)
            .text(" from ")
            .number(view.first_choice_row);
        sink_.line(line.c_str());
    }
    // The window, said only when there is one. A list that fits reports exactly
    // what it always reported, which is what makes a transcript derived before
    // this screen learned to scroll comparable with one derived after it.
    if (view.items > view.choices) {
        Text line;
        line.text("FACT window top ")
            .number(view.window_top)
            .text(" shown ")
            .number(view.choices)
            .text(" of ")
            .number(view.items);
        sink_.line(line.c_str());
    }
    for (int row = 0; row < page_.count; ++row) {
        Text line;
        line.text("FACT pageline ").number(row).space().text(page_.line(row));
        sink_.line(line.c_str());
    }
    sink_.line("FACT end");
}

void TurnClient::hold_page(Screen screen, const char* footer) {
    const ScreenView view = view_of(screen, footer);
    paint_screen(view);
    hold_for_checkpoint();
    screen_checkpoint(view);
    after_screen(view);
    hold_for_checkpoint();
    if (leaving_) return;
    for (;;) {
        const std::uint16_t buttons = next_press();
        if ((buttons & pad_end_of_script) != 0U) {
            leaving_ = true;
            return;
        }
        if ((buttons & (pad_a | pad_b | pad_c | pad_start)) != 0U) return;
    }
}

int TurnClient::choose_on_page(
    Screen screen, const char* footer, std::uint16_t back
) {
    back_pressed_ = 0;
    if (page_choices_ <= 0) {
        hold_page(screen, footer);
        return -1;
    }
    if (list_caret_ < 0) list_caret_ = 0;
    if (list_caret_ >= list_.total) list_caret_ = list_.total - 1;
    bool dirty = true;
    for (;;) {
        if (dirty) {
            const ScreenView view = view_of(screen, footer);
            paint_screen(view);
            hold_for_checkpoint();
            screen_checkpoint(view);
            after_screen(view);
            hold_for_checkpoint();
            dirty = false;
        }
        if (leaving_) return -1;
        const std::uint16_t buttons = next_press();
        if ((buttons & pad_end_of_script) != 0U) {
            leaving_ = true;
            back_pressed_ = pad_end_of_script;
            return -1;
        }
        // The caret walks the *list*, not the page, and the page is rebuilt
        // when it does, because on a list longer than its window the rows
        // under the caret are a function of where the caret is. A rebuild is
        // one composer call and no allocation, and it happens only on a press
        // that moved something.
        if ((buttons & pad_up) != 0U && list_caret_ > 0) {
            --list_caret_;
            recompose_page();
            dirty = true;
        }
        if ((buttons & pad_down) != 0U && list_caret_ + 1 < list_.total) {
            ++list_caret_;
            recompose_page();
            dirty = true;
        }
        if (back != 0U && (buttons & back) != 0U) {
            back_pressed_ = static_cast<std::uint16_t>(buttons & back);
            return -1;
        }
        // Start chooses on a screen that does not use it for something else,
        // because a player who has just been told to press it at a title has
        // their thumb there.
        if ((buttons & pad_a) != 0U ||
            ((back & pad_start) == 0U && (buttons & pad_start) != 0U)) {
            return list_caret_;
        }
    }
}

// ---------------------------------------------------------------------------
// Before the session
// ---------------------------------------------------------------------------

TurnClient::SlotChoice TurnClient::open_campaign(
    std::string_view project_title,
    const char* slot_base,
    const bool* holds,
    int slots
) {
    // The name, kept before it is drawn: the title screen says it here and the
    // ending screen says it again, and the campaign in between hands this
    // client no second chance to be told.
    {
        std::size_t length = 0;
        while (length < sizeof project_title_ - 1 &&
               length < project_title.size()) {
            project_title_[length] = project_title[length];
            ++length;
        }
        project_title_[length] = '\0';
    }
    begin_page();
    push_line("GRANDLEON");
    push_line("");
    push_line(project_title_);
    push_line("");
    push_line("A CAMPAIGN THIS CARTRIDGE KEEPS");
    hold_page(Screen::title, "PRESS START");

    slots_.open(slot_base, holds, slots);
    // The screen is driven here rather than through `choose_on_page`, because
    // its rows change under the caret: arming a row rewrites it, and a page
    // composed once and steered afterwards cannot say so. It is the same
    // vocabulary all the same, with a caret, A to take a row and B to back out
    // of what a press started, and it reports itself through the same
    // checkpoint.
    view::SlotMenu::Answer answer = view::SlotMenu::Answer::none;
    for (;;) {
        compose_slot_page();
        const ScreenView view = view_of(Screen::slot, slots_.footer());
        paint_screen(view);
        hold_for_checkpoint();
        screen_checkpoint(view);
        after_screen(view);
        hold_for_checkpoint();
        if (leaving_) break;

        const std::uint16_t buttons = next_press();
        if ((buttons & pad_end_of_script) != 0U) {
            leaving_ = true;
            break;
        }
        if ((buttons & pad_up) != 0U) slots_.move(-1);
        if ((buttons & pad_down) != 0U) slots_.move(1);
        if ((buttons & pad_b) != 0U) slots_.cancel();
        // C is the button this machine's action menu already opens with, so a
        // player who has met one of this campaign's screens has met this one's
        // second verb. On a held row it arms; on an armed row it founds; on an
        // empty row there is nothing to protect and it simply founds.
        if ((buttons & pad_c) != 0U) answer = slots_.over();
        if (answer == view::SlotMenu::Answer::none &&
            (buttons & (pad_a | pad_start)) != 0U) {
            answer = slots_.choose();
        }
        if (answer != view::SlotMenu::Answer::none) break;
    }

    SlotChoice chosen;
    chosen.slot = slots_.slot_name(slots_.caret());
    chosen.resume = answer == view::SlotMenu::Answer::resume;
    {
        Text line;
        line.text("slot ")
            .text(chosen.slot)
            .text(chosen.resume ? " RESUMING" : " FOUNDING");
        note(line.c_str());
    }
    return chosen;
}

// The slot screen as a page: one row per slot, and nothing else on it that the
// footer does not say. Recomposed on every press because arming a row changes
// what the row reads.
void TurnClient::compose_slot_page() {
    begin_page();
    push_line(project_title_);
    push_line("");
    page_first_choice_ = page_.count;
    page_choices_ = slots_.rows();
    list_.rows = slots_.rows() > 0 ? slots_.rows() : 1;
    list_.total = slots_.rows();
    list_.top = 0;
    list_caret_ = slots_.caret();
    for (int row = 0; row < slots_.rows(); ++row) {
        push_line(slots_.row_label(row));
    }
    push_line("");
    push_line("THE GAME SAVES ITSELF");
}

// ---------------------------------------------------------------------------
// The CampaignNarrator seam
// ---------------------------------------------------------------------------

void TurnClient::campaign_begun(
    const std::vector<client::RosterEntry>& roster,
    const std::vector<campaign::InventoryStack>& store,
    std::string_view slot,
    bool resumed
) {
    static_cast<void>(slot);
    resumed_ = resumed;
    // Evidence that this campaign has a history, counted where the campaign is
    // handed over rather than reconstructed later. A founding always has an
    // empty store, because the founding batch puts a unit type's starting kit
    // into its member's hands and never into the company's, and every member it
    // recruits is `available`. So either of these being true is something no
    // founding can produce, which is exactly what a second emulator process has
    // to find on the cartridge for the save to have meant anything.
    history_ = static_cast<int>(store.size());
    for (const client::RosterEntry& member : roster) {
        if (member.availability != campaign::Availability::available) {
            ++history_;
        }
    }
    {
        Text line;
        line.text("campaign ")
            .text(resumed ? "RESUMED" : "FOUNDED")
            .text(" roster ")
            .number(static_cast<std::int32_t>(roster.size()))
            .text(" store ")
            .number(static_cast<std::int32_t>(store.size()))
            .text(" history ")
            .number(history_);
        note(line.c_str());
    }
    compose_company(
        resumed ? "THE COMPANY STANDS AS YOU LEFT IT"
                : "THE COMPANY MUSTERS",
        roster, store
    );
    // Read rather than steered: this is the campaign being handed over, and
    // there is nothing on it to choose yet.
    page_choices_ = 0;
    hold_page(Screen::company, "A  GO ON");
}

void TurnClient::slot_refused(const client::SlotFailure& failure) {
    {
        Text line;
        line.text("refused storage ")
            .number(static_cast<std::int32_t>(failure.storage))
            .text(" migration ")
            .number(static_cast<std::int32_t>(failure.migration))
            .text(" save ")
            .number(static_cast<std::int32_t>(failure.save))
            .text(" state ")
            .number(static_cast<std::int32_t>(failure.state))
            .text(" wrong ")
            .number(failure.wrong_campaign ? 1 : 0);
        note(line.c_str());
    }
    begin_page();
    push_line("THE SAVED CAMPAIGN WAS NOT TAKEN");
    push_line("");
    push_line(refusal_reason(failure));
    push_line("");
    push_line("A NEW COMPANY TAKES THE FIELD");
    hold_page(Screen::refusal, "A  GO ON");
}

void TurnClient::board_prepared(const client::CampaignBoard& board) {
    // Kept for the length of the battle, so that a character who falls during it
    // can be named. The board holds both halves of the answer, the roster and
    // the join saying which numbered unit each member is standing in, and this
    // is the only moment a client is handed them together.
    board_ = &board;
    Text line;
    line.text("board roster ")
        .number(static_cast<std::int32_t>(board.roster.size()))
        .text(" excluded ")
        .number(static_cast<std::int32_t>(board.excluded.size()))
        .text(" store ")
        .number(static_cast<std::int32_t>(board.store.size()));
    note(line.c_str());
}

// What the battle did to the company, as the campaign committed it. Every line
// is read out of `campaign_runtime::BattleProgression` and out of the roster the
// commit left behind; nothing here adds anything up.
void TurnClient::battle_aftermath(const client::BattleAftermath& aftermath) {
    // The battle the borrowed board described is over. Dropped here rather than
    // left to be replaced by the next `board_prepared`, so that the window it is
    // readable in is the window it is valid in.
    board_ = nullptr;
    {
        Text line;
        line.text("aftermath outcome ")
            .number(static_cast<std::int32_t>(aftermath.outcome))
            .text(" fallen ")
            .number(static_cast<std::int32_t>(aftermath.fallen.size()))
            .text(" levels ")
            .number(
                static_cast<std::int32_t>(aftermath.progression.level_ups.size())
            )
            .text(" operations ")
            .number(static_cast<std::int32_t>(
                aftermath.progression.operations.size()
            ))
            .text(" advanced ")
            .number(aftermath.completion.advanced ? 1 : 0);
        note(line.c_str());
    }
    begin_page();
    push_line("AFTER THE BATTLE");
    push_line("");
    // Who is gone, by name, before anything else on the page. The list has been
    // on `BattleAftermath::fallen` since there was an aftermath and this screen
    // printed its `.size()`, which left a player to learn from the digit `1`
    // that somebody they had been steering for six battles was dead.
    //
    // First rather than last because the page holds sixteen lines and the store
    // is the half worth losing. A player who has to scroll to find out who died
    // has been told badly.
    //
    // And in the rule's own words. Under the permanent rule these people are
    // dead and the company buries them; under the recoverable rule they were
    // carried off and they are back, which is a very different screen to read
    // and must not be worded like the first one.
    if (!aftermath.fallen.empty()) {
        const bool buried =
            aftermath.character_loss == pr::CharacterLoss::permanent;
        push_line(buried ? "THE COMPANY BURIES" : "CARRIED OFF, AND BACK");
        for (const campaign::PersistentEntityId member : aftermath.fallen) {
            Text row;
            row.text("  ").text(member_name(aftermath.roster, member));
            push_line(row.c_str());
        }
        push_line("");
    }
    for (const client::RosterEntry& member : aftermath.roster) {
        Text row;
        push_padded(row, member.name.c_str(), 15);
        row.text("L").number(
            static_cast<std::int32_t>(member.progression.level)
        );
        row.text(" XP ").number(
            static_cast<std::int32_t>(member.progression.experience)
        );
        row.space().text(availability_word(member.availability));
        push_line(row.c_str());
    }
    push_line("");
    {
        Text totals;
        totals.text("LEVELS ")
            .number(
                static_cast<std::int32_t>(aftermath.progression.level_ups.size())
            )
            .text(
                aftermath.character_loss == pr::CharacterLoss::permanent
                    ? "  LOST "
                    : "  FELL "
            )
            .number(static_cast<std::int32_t>(aftermath.fallen.size()))
            .text("  JOINED ")
            .number(static_cast<std::int32_t>(aftermath.recruited.size()));
        push_line(totals.c_str());
    }
    push_line("THE COMPANY'S STORE");
    if (aftermath.store.empty()) {
        push_line("NOTHING BUT WHAT THEY CARRY");
    }
    for (const campaign::InventoryStack& stack : aftermath.store) {
        Text row;
        push_padded(row, sheet::item_name(stack.item.stable_id), 20);
        row.text("X").number(static_cast<std::int32_t>(stack.quantity));
        push_line(row.c_str());
    }
    hold_page(Screen::aftermath, "A  GO ON");
}

void TurnClient::members_joined(
    const std::vector<client::RosterEntry>& joined
) {
    if (joined.empty()) return;
    begin_page();
    push_line("JOINS THE COMPANY");
    push_line("");
    for (const client::RosterEntry& member : joined) {
        push_line(member.name.c_str());
    }
    hold_page(Screen::joined, "A  GO ON");
}

void TurnClient::campaign_saved(
    std::string_view slot, storage::StorageError error
) {
    static_cast<void>(slot);
    ++saves_;
    if (error != storage::StorageError::none) ++save_failures_;
    Text line;
    line.text("saved ").text(storage::storage_error_name(error).data());
    note(line.c_str());
}

void TurnClient::management_opened(const client::CompanyManagement& company) {
    Text line;
    line.text("manage roster ")
        .number(static_cast<std::int32_t>(company.roster.size()))
        .text(" store ")
        .number(static_cast<std::int32_t>(company.store.size()))
        .text(" placeable ")
        .number(static_cast<std::int32_t>(company.placeable.size()));
    note(line.c_str());
    manage_row_ = 0;
    roster_top_ = 0;
}

void TurnClient::management_committed(const client::ManagementCommit& result) {
    if (static_cast<bool>(result)) ++commits_;
    Text line;
    line.text("gesture ")
        .text(campaign::outcome_error_name(result.application.error).data())
        .text(" saved ")
        .number(result.saved ? 1 : 0);
    note(line.c_str());
}

// A jump, taken or refused.
//
// Two things happen here and both are obligations rather than reporting. The
// borrowed board is dropped, because a jump ends the board it came out of
// without an aftermath and this is the far end of that window: leaving the
// pointer set would leave it dangling across every screen between here and the
// next board. And a refusal is put where the next company screen will draw it,
// in the campaign's own word, because nothing else happens after a refused jump
// — the battle is gone and the player is standing where they stood, and with
// nothing said that reads as the console having lost their game.
void TurnClient::stage_jumped(const client::StageJump& jump) {
    board_ = nullptr;
    Text line;
    line.text("jumped to ")
        .number(static_cast<std::int32_t>(jump.target.stable_id))
        .text(" moved ")
        .number(jump.completion.advanced ? 1 : 0)
        .text(" error ")
        .text(campaign::progression_error_name(jump.completion.error).data());
    note(line.c_str());
    if (static_cast<bool>(jump)) return;
    // Whichever layer refused it, in that layer's own word. The campaign has
    // one for every way a move through a graph can fail; the session has its
    // own for the one refusal that is not the campaign's, which is a Stage this
    // game does not offer.
    pending_refusal_ =
        jump.completion.error != campaign::ProgressionError::none
            ? campaign::progression_error_name(jump.completion.error).data()
            : client::campaign_session_error_name(jump.error).data();
}

// The company, between battles. One screen, one caret, and a menu per member
// holding every verb the stage has. That is the same shape the unit action
// menu already taught: nothing is aimed at, the rows say what taking them
// costs, and B backs out one step.
client::ManagementIntent TurnClient::next_management_intent(
    const client::CompanyManagement& company
) {
    company_ = &company;
    const int rows = static_cast<int>(company.roster.size());
    if (rows == 0 || leaving_) {
        company_ = nullptr;
        return {client::ManagementVerb::quit, {}, {}};
    }
    if (manage_row_ >= rows) manage_row_ = rows - 1;
    if (manage_row_ < 0) manage_row_ = 0;

    // The roster is the list the caret is on. `manage_row_` is where the caret
    // was left after the last gesture, and `roster_top_` is where the window
    // was, so a player who gave something away is still standing in front of
    // the same person on the same page.
    list_caret_ = manage_row_;
    list_top_ = roster_top_;
    compose_company(
        "BEFORE THE BATTLE", company.roster, company.store, &company
    );
    // The Stage picker is on this screen too, and on a button rather than a row
    // because the caret here walks the company: a row that was not a member
    // would be a row the member menu below has nothing to open for.
    //
    // **It has to be reachable from here, and that is not a convenience.** A
    // jump recruits nobody on behalf of the Stages it passed over, so a board
    // whose objective names a late-joining character refuses to open — and a
    // refused board is exactly what sends a player to this screen. There is
    // nothing they could do here to recruit anybody, and the jump has already
    // written the slot. Without this the aid could leave a saved campaign
    // standing at a Stage nothing can open.
    //
    // Both footers are named and measured. A footer is cut by the display
    // rather than by `push_line`, silently, in the middle of the last word —
    // which is where the button a player has not met yet is written. `START
    // BATTLE` says what `START TO BATTLE` said in two characters fewer,
    // because those two characters are what the hint needs; `C GO` is the verb
    // beside `A CHOOSE` and `B LEAVE`, and it reads into the title of the
    // screen it opens.
    static constexpr char company_footer[] =
        "A CHOOSE  START BATTLE  B LEAVE";
    static constexpr char company_footer_with_stages[] =
        "A CHOOSE  START BATTLE  B LEAVE  C GO";
    static_assert(
        sizeof(company_footer) - 1U <= footer_columns,
        "the company screen's footer has to fit on the narrowest display"
    );
    static_assert(
        sizeof(company_footer_with_stages) - 1U <= footer_columns,
        "and so does the one that names the Stage picker's button, which is "
        "the whole of the point: a hint cut in half is a hint that has taught "
        "a player nothing"
    );
    const bool offers_stages = !company.stages.empty();
    const std::uint16_t leaves = static_cast<std::uint16_t>(
        offers_stages ? (pad_b | pad_start | pad_c) : (pad_b | pad_start)
    );
    const int chosen = choose_on_page(
        Screen::company,
        offers_stages ? company_footer_with_stages : company_footer,
        leaves
    );
    roster_top_ = list_top_;
    if (chosen < 0) {
        if ((back_pressed_ & pad_c) != 0U) {
            stages_ = &company.stages;
            const std::uint64_t going = choose_a_stage();
            stages_ = nullptr;
            company_ = nullptr;
            if (going == 0U) return {client::ManagementVerb::none, {}, {}, 0U};
            client::ManagementIntent jump;
            jump.verb = client::ManagementVerb::jump;
            jump.stage = going;
            return jump;
        }
        company_ = nullptr;
        // Leaving loses nothing and the footer says so: every gesture committed
        // and saved when it was made, so there is no state here for a
        // confirmation screen to protect.
        if ((back_pressed_ & pad_start) != 0U) {
            return {client::ManagementVerb::proceed, {}, {}};
        }
        return {client::ManagementVerb::quit, {}, {}};
    }
    manage_row_ = chosen;

    // A new list: the caret starts at the top of it and so does the window.
    list_caret_ = 0;
    list_top_ = 0;
    compose_member_menu(company, manage_row_);
    const int verb =
        choose_on_page(Screen::member, "A CHOOSE  B BACK", pad_b);
    company_ = nullptr;
    if (verb < 0 || verbs_[verb].verb == client::ManagementVerb::none) {
        // Not a verb, so the driver asks again and the company is drawn again.
        // `none` is the session's own word for exactly this.
        return {client::ManagementVerb::none, {}, {}};
    }
    client::ManagementIntent intent;
    intent.verb = verbs_[verb].verb;
    intent.member = company.roster[static_cast<std::size_t>(manage_row_)].member;
    intent.item = verbs_[verb].item;
    // A board its author capped fields no more than it says, so the press is
    // refused here rather than committed and undone. The sentence is the
    // roster's own name for it, never one written on this console, because it
    // is the very refusal `join_campaign_roster` would have given a gesture
    // later. It is the Nintendo 64's wording for the same reason the refusal
    // table above is: a player who has seen one console's refusal should
    // recognise the other's. `none` sends the driver round again, so
    // nothing is committed and the company page is what says why.
    if (intent.verb == client::ManagementVerb::field &&
        field_is_full(company)) {
        const std::string_view name = campaign_runtime::roster_error_name(
            campaign_runtime::RosterError::over_deployment_capacity
        );
        pending_refusal_ = name.data();
        Text line;
        line.text("refused ").text(pending_refusal_);
        note(line.c_str());
        return {client::ManagementVerb::none, {}, {}};
    }
    return intent;
}

#endif  // GRANDLEON_TURN_CLIENT_CAMPAIGN

}  // namespace grandleon::client::turn
