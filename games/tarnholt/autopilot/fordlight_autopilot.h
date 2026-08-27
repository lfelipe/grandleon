// SPDX-License-Identifier: MIT
#pragma once

// The Tarnholt campaign's autopilot: synthetic controller input that drives
// the interactive ROM from power-on through five rounds of the Fordlight
// Crossing (title, the controls screen, all twelve cutscene lines, and the
// battle itself), with named checkpoints where the harness samples the
// framebuffer and captures the screenshot trail.
//
// ---------------------------------------------------------------------------
// It plays the same board as `platform/client/autopilot/fordlight_pad.h`
//
// That file is the same itinerary in the shared client's own button
// vocabulary, and it is the one this console's numbers are derived from. This
// console has no expectation tool of its own, so `grandleon_playstation_expect`
// links the same engine, the same `client::run_battle` and the same
// `turn_client.cpp`, replays that script press for press, and writes out what
// the rules say every gesture adds up to. Every value the checkpoints below
// pin is read off that derivation and never off a screen.
//
// The two differ only in how the cursor is steered. This script names tiles
// outright with `Op::cursor_to`, because a ROM that has to walk the cursor
// there one press at a time would spend most of its steps doing it; the pad
// script has only a d-pad and walks. What reaches the engine is identical, and
// that is what makes one derivation answer for both.
//
// ---------------------------------------------------------------------------
// The shape the turn order gives it
//
// Tarnholt plays in side blocks: every character of one side acts, in whatever
// order the player picks, then every character of the other. Three consequences
// shape every press below.
//
//   - **A gesture hands nothing over.** One accepted activation leaves the
//     block open, so a round is four of the guard's gestures rather than one,
//     and the Coil answers with a whole block of its own.
//   - **A walk is half a turn.** Every class here carries two action points,
//     which is one walk and one action, so a character that has stepped is
//     still owed something. Every walk below is therefore followed by Z and
//     three steps up to END CHARACTER TURN, because a character left holding a
//     point is a character the next press would have picked up again.
//   - **The board menu is the only way to hand over early.** START opens it and
//     its second row ends the side's turn, finishing everybody still owed one a
//     `wait` at a time. A round that leaves nobody idle closes by itself.
//   - **The line holds together.** A character walked out alone in front of the
//     guard meets four answers before its own next turn. So the guard steps up
//     together and the Coil is allowed to come to it, which is why the first
//     two rounds spend no arrows at all.
//
// A character that has taken its turn keeps standing on the board, drawn
// through a greyed copy of its own palette, while its side keeps playing. That
// is the picture this game spends most of its time on, and `after-move` is
// where it is photographed and counted.
//
// ---------------------------------------------------------------------------
// What the five rounds are for
//
//   1. Everything a character's own menu can be asked. The northern knight is
//      hovered, picked up, and its whole sheet read out of the INFO row in the
//      menu's tail; the menu is put down and the bridge road taken with the
//      plain gesture, over a board still lighting its reach. Its brother
//      follows through the WALK row. The archer takes ATTACK with nothing in
//      reach, so the board lights nothing: the answer given without the player
//      sweeping for it. It then ends its own turn from the row that says so,
//      reached by walking the caret up off the top, which wraps.
//      The mage's menu is the only one in either shipped campaign that offers a
//      choice of spell, so it is eight rows where the others are six.
//   2. The board menu, and nothing else. START answers a question about the
//      battle whether or not a character is in hand, so it is asked both ways,
//      and END YOUR TURN then drains the rest of the side.
//   3. The Coil has closed to three tiles, so the same gesture on the same bow
//      now lights exactly one square. The forecast is read off the enemy under
//      the cursor and the arrow loosed; the rest of the guard steps up.
//   4. The line fights. Both knights strike what is adjacent, the archer shoots
//      the wounded one again, and the mage takes CINDER ARC out of the fourth
//      row of its menu, the one gesture here that asks the cursor to cover
//      more than a single tile.
//   5. A strike the forecast calls lethal before it is thrown, and the draught.
//      The mage's menu holds `FIELD TONIC x1` between its spells and the end of
//      its turn, and taking that row commits on the press because an item
//      reaches the hand that holds it. The mage is unhurt when it drinks, so
//      what the row proves is that the draught is spent either way and that a
//      heal does not carry a character past its own maximum.
//
// Red's replies are `tactics::decide`'s and are not scripted at all. They are
// deterministic for the reason every run of this ROM is: the session animates
// the first actionable red unit in identifier order and the policy decides for
// it, so the whole exchange replays identically.
//
// ---------------------------------------------------------------------------
// The numbers, and where each came from
//
// All of these are `grandleon_playstation_expect`'s output over this itinerary,
// and the way to challenge one is to run it rather than to read a screen:
//
//   - the bow forecasts `80% HIT 2 LEFT 10` against an Ashen Knight on twelve.
//     The Long Bow is power two authored at 90 and the Archer carries five
//     points of skill, so the folded chance would be 90 + 5 against a knight
//     that dodges nothing -- except that a knight holds a blade, and the
//     shipped table makes a blade strong against a bow. The archer is striking
//     into the advantage, so the game's `weaponAdvantage` comes off both
//     numbers rather than going on: one off the blow and fifteen off the
//     chance. It read `95% HIT 3 LEFT 9` before there was a table.
//   - a Guard Sword forecasts `HIT 5`, and against the knight the line has worn
//     down to three it forecasts `HIT 5 KO`: lethal, and therefore unanswered.
//   - Cinder Arc takes the knight the archer had already left on one, so the
//     board after it is three a side: the Coil took the southern knight in the
//     block before.
//   - the mage ends the run on seven of seven with an empty draught row, which
//     is a clamp rather than a roll: spending an item draws from no stream.
//
// The multi-weapon vocabulary costs this script nothing, and that is worth
// recording because it looks like it would not. Every Tarnholt character
// carries one weapon, so the policy's weapon choice has a single candidate and
// proposes the identical command with no weapon named; the action menu reads
// ATTACK for the same reason.

#include "autopilot.h"

namespace grandleon::n64autopilot {

inline constexpr Step fordlight_autopilot[] = {
    // Title screen: let it paint and blink, photograph it, press start.
    {Op::wait, 120, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::title), 0, 0,
     "title"},
    {Op::press, button_start, 0, 0, nullptr},

    // The controls screen.
    {Op::wait, 60, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::controls), 0, 0,
     "controls"},
    {Op::press, button_a, 0, 0, nullptr},

    // Three cutscenes, twelve lines. Photograph the first, advance them all.
    {Op::wait, 60, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::cutscene), 0, 0,
     "cutscene"},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},

    // The Fordlight opens with words: two sayings, a press each, before the
    // board is settled. They are said *over* the board rather than on a page of
    // their own, so the checkpoint here is about a surface no other one covers:
    // the words, and the fight still visible underneath them.
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    // Photographed between the two, where a bubble is certainly on the screen.
    // Before the first press it is not: the ops are consumed by whichever wait
    // polls next, and the cutscene's own last press is still ahead of the board
    // being drawn at all.
    {Op::checkpoint, static_cast<std::uint16_t>(Check::saying), 0, 0,
     "saying"},
    {Op::press, button_a, 0, 0, nullptr},

    // The battle opens behind its banner. Verify the authored board.
    {Op::wait, 30, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::battle_open), 0, 0,
     "battle-open"},

    // Hover the northern knight: the information panel.
    {Op::cursor_to, 0, 0, 3, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::unit_panel), 0, 0,
     "unit-panel"},

    // Pick it up. The menu opens on the press that picks a character up, which
    // is the whole of how this ROM introduces the board: every row is derived
    // from the character (where it may walk, what it strikes with, its one
    // spell, the end of its own turn, its sheet, the way out), so a player who
    // has read nothing has been shown everything the character can be told to
    // do.
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::action_menu), 0, 0,
     "action-menu"},

    // Round one. Four gestures, so the guard's block closes on its own: this
    // game runs a whole side at a time and one accepted activation hands
    // nothing over.
    //
    // The northern knight first: its menu, then the whole of what it is out of
    // the INFO row in that menu's tail, then the menu down and the bridge road
    // taken with the plain gesture.
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::info_sheet), 0, 0,
     "info-sheet"},
    {Op::press, button_b, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},

    // The sheet stood down and the menu is still up behind it, caret still on
    // INFO, which is the point of that row: reading is not choosing. Four steps
    // back up reach WALK, and taking it hands the board over with the reach
    // still lit and a walk in hand, which is what the next checkpoint is of.
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},

    // The reach is still lit with the menu gone, so the board can be
    // photographed tile for tile against the engine's own queries before a
    // single step is taken.
    {Op::cursor_to, 0, 2, 3, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::move_range), 0, 0,
     "move-range"},
    {Op::cursor_to, 0, 1, 3, nullptr},
    {Op::wait, 0, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},

    // Two action points is one walk and one action, so the knight is not
    // finished by having stepped: it is still holding a point and the board is
    // still offering it something. Z opens its menu again, the one thing the
    // controls screen says about Z, and three steps up from the top wrap onto
    // END CHARACTER TURN, which is the same idiom the archer's turn ends with
    // further down. It is done before the photograph rather than after, so
    // `after-move` is the frame it has always been: a character standing spent
    // and grey while its own side keeps playing.
    {Op::press, button_z, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::after_move), 0, 0,
     "after-move"},

    // The southern knight follows through the WALK row. Its walk leaves the
    // first one standing and spent beside it, which is what most of a side
    // block looks like.
    {Op::cursor_to, 0, 0, 4, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::cursor_to, 0, 1, 4, nullptr},
    {Op::wait, 0, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::press, button_z, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},

    // The archer, and a bow with nothing whatever in reach. ATTACK is taken
    // out of its menu and the board lights nothing at all: the dark half of
    // the pair, the answer given without the player sweeping the board for
    // it. Then the row that ends its own turn, reached by walking the caret
    // up off the top, which wraps: three steps up are the last three rows of
    // every menu this client draws.
    {Op::cursor_to, 0, 1, 2, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::aim_with_no_target), 0, 0,
     "aim-no-target"},
    {Op::press, button_b, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::character_turn_row), 0, 0,
     "character-turn-row"},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},

    // And the mage, whose menu is the only one in either shipped campaign that
    // offers a choice of spell: eight rows where every other character's is
    // six. Its sheet is read out of the same tail, and it ends its turn: the
    // fourth gesture, so the block closes and the Coil answers with a whole
    // block of its own.
    {Op::cursor_to, 0, 1, 5, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::press, button_b, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::wait, 60, 0, 0, nullptr},

    // Round two is the board menu and nothing else. START answers a question
    // about the battle whether or not a character is in hand, so it is asked
    // both ways, and END YOUR TURN then drains everybody still owed a turn,
    // a `wait` each, which is what ending a side has to mean where each of
    // them holds their own.
    {Op::cursor_to, 0, 1, 3, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_b, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_start, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::board_menu_in_hand), 0, 0,
     "board-menu-in-hand"},
    {Op::press, button_b, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_b, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_start, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::board_menu), 0, 0,
     "board-menu"},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 60, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::side_turn_passed), 0, 0,
     "side-turn-passed"},

    // Round three. The Coil has closed to three tiles, so the same gesture on
    // the same bow now lights exactly one square: the bright half of the pair.
    // The forecast is read off the enemy under the cursor before the arrow is
    // loosed.
    {Op::cursor_to, 0, 1, 2, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::aim_with_a_target), 0, 0,
     "aim-with-target"},
    {Op::cursor_to, 0, 3, 3, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::forecast), 0, 0,
     "forecast"},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},

    // The rest of the guard steps up to meet what is coming rather than
    // waiting to be reached one at a time.
    {Op::cursor_to, 0, 1, 3, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::cursor_to, 0, 2, 3, nullptr},
    {Op::wait, 0, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::press, button_z, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::cursor_to, 0, 1, 4, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::cursor_to, 0, 2, 4, nullptr},
    {Op::wait, 0, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::press, button_z, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::cursor_to, 0, 1, 5, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::cursor_to, 0, 2, 5, nullptr},
    {Op::wait, 0, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 60, 0, 0, nullptr},
    {Op::press, button_z, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},

    // Round four, and the line fights. Both knights strike what is adjacent,
    // the archer shoots the wounded one again, and the mage takes CINDER ARC
    // out of the fourth row of its menu and aims it, the one gesture on this
    // board that asks the cursor to cover more than a single tile.
    {Op::cursor_to, 0, 2, 3, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::cursor_to, 0, 3, 3, nullptr},
    {Op::wait, 0, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::cursor_to, 0, 1, 2, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::cursor_to, 0, 3, 3, nullptr},
    {Op::wait, 0, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::cursor_to, 0, 2, 4, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::cursor_to, 0, 3, 4, nullptr},
    {Op::wait, 0, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::cursor_to, 0, 2, 5, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::spell_choice), 0, 0,
     "spell-choice"},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::aiming), 0, 0,
     "aiming"},
    {Op::cursor_to, 0, 3, 4, nullptr},
    {Op::wait, 0, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::after_ability), 0, 0,
     "after-ability"},
    {Op::wait, 60, 0, 0, nullptr},

    // Round five. A strike the forecast calls lethal before it is thrown, and
    // then the draught: the mage's menu holds it between the spells and the
    // end of its turn, and taking that row commits on the press because an
    // item reaches the hand that holds it.
    //
    // The knight at (1,2) walks first, to (2,3), because the one the line has
    // worn down to three is standing at (2,4) and (2,3) is the empty square
    // above it -- empty because one of the company fell there. So this is the
    // two-point turn spelled out in full: WALK from the menu, then Z to open
    // the menu again, where ATTACK is now the first row because a character
    // that has walked is offered no second walk.
    //
    // The round used to be the same shape a few squares over: a knight at
    // (2,3) walked into (3,3), which the Coil's own fallen had left empty, and
    // struck the worn one at (3,4). The weapon triangle moved every square of
    // it. The archer's shots into a blade take two rather than three now, so a
    // knight the line used to fell stays up long enough to take one of the
    // company with it, and the survivors and the worn one all ended up
    // somewhere else. Pressing the old buttons on the new board selected an
    // empty square and aimed at another one, which is why the checkpoint could
    // not find so much as a pairing to price.
    //
    // Re-planned rather than re-aimed. The point of the round is a strike the
    // forecast calls lethal before it is thrown, and a cursor nudged onto
    // whatever happens to be nearby would have kept the checkpoint green while
    // quietly making it about something else.
    {Op::cursor_to, 0, 1, 2, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::cursor_to, 0, 2, 3, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    // Sixty rather than the thirty the old walk was given. This one crosses two
    // squares where that one crossed one, and the token walks its route a tile
    // at a time: a menu press that lands while it is still walking is a press
    // the client is not listening for, and the round arrives at its checkpoint
    // with nobody selected.
    {Op::wait, 60, 0, 0, nullptr},
    {Op::press, button_z, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::cursor_to, 0, 2, 4, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::forecast_lethal), 0, 0,
     "forecast-lethal"},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::cursor_to, 0, 2, 5, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::item_row), 0, 0,
     "item-row"},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::after_item), 0, 0,
     "after-item"},

    // And the side is ended from the board menu one last time, over a board
    // neither company is finished with.
    {Op::cursor_to, 0, 1, 2, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::press, button_start, 0, 0, nullptr},
    {Op::wait, 30, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 60, 0, 0, nullptr},
    {Op::finish, 0, 0, 0, nullptr},
};

}  // namespace grandleon::n64autopilot
