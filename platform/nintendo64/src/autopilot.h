// SPDX-License-Identifier: MIT
#pragma once

// The autopilot vocabulary for the interactive Nintendo 64 ROMs.
//
// An autopilot build replaces the controller with a deterministic script of
// synthetic input. The real interactive code paths run exactly as they do for a
// person (title screen, cutscene advance, cursor movement, the action menu,
// refusal banners), and a headless emulator run can assert what they drew. The
// script is data: a table of the steps below, checked in beside the campaign it
// drives (games/tarnholt/autopilot/), so a new game carries its own sequence
// without touching the ROM. The ROM owns the checkpoint assertions; a script
// only names which checkpoint fires where.
//
// No libdragon dependency, deliberately: scripts are plain tables and the
// host suite can include this header to sanity-check one.

#include <cstdint>

namespace grandleon::n64autopilot {

enum class Op : std::uint8_t {
    // Press the buttons in `value` for exactly one poll.
    press = 0,
    // Return no input for `value` polls, letting the screen settle.
    wait,
    // Walk the board cursor to (x, y), one d-pad press per poll, column
    // first and then row. Only meaningful once a battle is on screen.
    cursor_to,
    // Report "CHECKPOINT <name>" on the debug channel, run the assertions
    // `value` names (a Check below), and hold still long enough for the
    // harness to photograph the frame.
    checkpoint,
    // Report the RESULT verdict and go idle. Every script ends here.
    finish,
};

// Button bits for Op::press.
inline constexpr std::uint16_t button_a = 1U << 0;
inline constexpr std::uint16_t button_b = 1U << 1;
inline constexpr std::uint16_t button_start = 1U << 2;
inline constexpr std::uint16_t button_z = 1U << 3;
inline constexpr std::uint16_t button_d_up = 1U << 4;
inline constexpr std::uint16_t button_d_down = 1U << 5;
inline constexpr std::uint16_t button_d_left = 1U << 6;
inline constexpr std::uint16_t button_d_right = 1U << 7;

// The checkpoints the ROM knows how to verify. Banner and refusal
// checkpoints are not listed: the presenter reports those on its own
// whenever the real code path shows one.
enum class Check : std::uint16_t {
    title = 1,
    controls,
    cutscene,
    battle_open,
    unit_panel,
    move_range,
    after_move,
    action_menu,
    // The board's own menu, on START, over a battle. A different menu from the
    // one above and asserted as such: it holds the way back, the end of the
    // whole side's turn and the way out, which are about the battle rather than
    // about one of its characters, and none of the rows a character's menu
    // holds.
    board_menu,
    // The same menu, opened with a character in hand. START answers a question
    // about the battle whether or not one is selected, and this is the half
    // where one is: a START that worked only with empty hands would answer it
    // for half the board. The selection has to survive the menu, because
    // opening a menu to read it is not putting a character down.
    board_menu_in_hand,
    // The character's own end of turn, under the caret. A row asserted to exist
    // is not a row asserted to be reachable, and a row that cannot be reached
    // from the button a player presses is exactly the defect this catches.
    character_turn_row,
    // The board after END YOUR TURN was taken out of that menu: the side was
    // drained of what it had not spent, the opposing side answered, and control
    // came back with nothing left standing from either.
    side_turn_passed,
    forecast,
    forecast_lethal,
    after_ability,
    // The mage's menu, which is the first in either shipped campaign to offer
    // a choice of spell, and the pick it hands back to the cursor.
    spell_choice,
    aiming,
    // The full information sheet, taken out of the action menu's INFO row: the
    // whole of what a character is, on a screen of its own, with every number
    // on it composed by `grandleon::sheet` from the snapshot and the
    // encounter's registries.
    info_sheet,
    // The mage's menu again, with the item row on it: the carried draught,
    // sitting between the spells and WAIT where the menu holds the place open
    // for it, saying how many are left.
    item_row,
    // What drinking it did. Deterministic and exact: no stream is drawn from,
    // so the number the forecast showed is the number the board shows.
    after_item,
    // The kept campaign's own screens. What each of them asserts about the
    // *campaign* is asserted by the narrator when the session hands it over,
    // which is where the roster and the store actually arrive. So these three
    // assert what only a frame can be wrong about: that the screen was drawn.
    //
    // The slot screen, before anything is founded: what the cartridge holds,
    // and the choice between beginning and continuing.
    campaign_slot,
    // The company as the campaign was founded or resumed with it.
    campaign_roster,
    // The management stage: the company, its store, and what the next board
    // has room for.
    campaign_management,
    // A board whose content says the battle is won by outlasting a number of
    // rounds: the status line carries the round the player is in and the
    // number there are to outlast, and the assertion is that the two agree with
    // the snapshot and with the objective the encounter was created with.
    //
    // No shipped script names it, because no shipped board authors one. It is
    // here so that the campaign which does can be scripted without inventing a
    // verb first. The ROM owns the assertion, so a script cannot make a wrong
    // frame pass.
    survive_status,
    // A scene line whose speaker the scene cast, photographed to say that the
    // portrait beside the words is the character the package names rather than
    // the one the speaker's display name happens to spell. It is a separate
    // checkpoint from `cutscene` because it has to land on a line where the two
    // answers differ: a line whose speaker the keyword convention would get
    // right by accident proves nothing about which of the two drew it.
    cutscene_cast,
    // A strike taken out of the menu by a character with an opponent inside
    // its band: the board lights that opponent's tile in amber and lights no
    // other, before the cursor has been moved at all.
    aim_with_a_target,
    // The same gesture by a character with nobody inside its band, which is the
    // half no forecast panel can tell you: the board lights nothing. It is the
    // answer a player would otherwise have to sweep the whole board to find,
    // and it is the reason the aiming overlay exists.
    aim_with_no_target,
};

struct Step final {
    Op op{Op::wait};
    // Buttons for press, polls for wait, the Check for checkpoint.
    std::uint16_t value{};
    std::int16_t x{};
    std::int16_t y{};
    // The checkpoint's name on the wire and in the screenshot trail.
    const char* name{nullptr};
};

}  // namespace grandleon::n64autopilot
