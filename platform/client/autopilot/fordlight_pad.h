// SPDX-License-Identifier: MIT
#pragma once

// The autopilot's controller script for the Fordlight Crossing: a deterministic
// sequence of button presses, one per entry, in the order a thumb would make
// them.
//
// It is only the input. Nothing here says what should happen. Every
// expectation is derived from the rules by an `*_expect` tool, which replays
// exactly this sequence against the real engine on the host and writes out the
// transcript the console is then required to reproduce. Adding a press here
// changes what is played; it can never change what passes.
//
// **It lives here rather than beside one console** because every console that
// plays this board plays it through the same client, and a second script would
// be a second session: two machines proving different things while looking
// like they proved the same one. The buttons are named in the client's
// vocabulary rather than any machine's, and
// `grandleon/client/turn_client.hpp` records which face button each console
// puts under `pad_a`, `pad_b` and `pad_c`.
//
// Timing is deliberately absent. The PlayStation's script is compiled into the
// executable and paced by counting frames, because that emulator's headless
// mode offers no pad to press. It is the Nintendo 64's arrangement as well,
// and it has the side effect that the PlayStation artifact demonstrates itself
// to anybody who runs it. A harness that can press an emulator's own pad ports
// instead reads `AWAIT` from the machine and presses only then. Both are
// correct at any emulation speed, and a slow board shows up as a run that needs
// more frames rather than as a wrong result.
//
// ---------------------------------------------------------------------------
// The shape the turn order gives it
//
// Tarnholt plays in side blocks: every character of one side acts, in whatever
// order the player picks, then every character of the other. That decides the
// script's whole structure, and it is worth stating because a script recorded
// under one order is not a script under another.
//
//   - **A gesture hands nothing over.** One accepted activation leaves the
//     block open, so a round here is four blue gestures rather than one, and
//     the Coil answers with a whole block of its own.
//   - **A walk is half a turn, until there is no other half.** Every class in
//     this campaign carries two action points, which is one walk and one
//     action, so a character that has stepped is usually still owed something
//     and the board still offers it. Where it is, the walker's turn is ended by
//     hand rather than left to the board menu to sweep up, because the point of
//     a script is that every gesture is a gesture somebody made. By hand is
//     `pad_c` to open its menu again and three steps up to the row that says
//     so.
//
//     Where it is not, there is no gesture to make and the script makes none: a
//     character with nobody in reach has nothing to strike and nothing worth
//     casting, because a damaging spell harms only the caster's opponents and a
//     band holding none of them changes nobody. The client closes such a turn
//     on the walk that made it, which is what the first two walks of round one
//     do with the Coil still six tiles off.
//   - **The board menu is the only way to hand over early.** START opens it and
//     its second row ends the side's turn, finishing everybody still owed one,
//     a `wait` at a time. Rounds that leave nobody idle close by themselves.
//   - **The line holds together.** A character walked out alone in front of the
//     guard meets four answers before its own next turn, which is a way to lose
//     a board rather than a way to photograph one. So the guard steps up
//     together and the Coil is allowed to come to it.
//
// ---------------------------------------------------------------------------
// The itinerary, for a reader following along on the Fordlight Crossing
//
// Five rounds, and each is a block of the guard's followed by a block of the
// Coil's. Everything the menus can be asked is asked somewhere in them.
//
//   1. The northern knight is hovered, picked up, and its whole sheet read out
//      of the INFO row in the tail of its menu; the menu is put down and it
//      steps onto the bridge road with the plain gesture. Its brother follows
//      through the WALK row. The archer takes ATTACK with nothing whatever in
//      reach, backs out, and ends its own turn from the row that says so. That
//      is the darker half of the aiming pair, a board that lights nothing. The
//      mage's menu is the one in either shipped campaign that offers a choice
//      of spell, so it is eight rows deep; its sheet is read too, and it ends
//      its turn. Four characters, each walked and then finished, so the block
//      closes on its own.
//   2. Nothing but the board menu. It is opened once with a character in hand
//      and once with empty hands, and then END YOUR TURN drains the rest of the
//      side. START answers a question about the battle either way, and a menu
//      that worked only with empty hands would answer it for half the board.
//      The Coil closes to three tiles.
//   3. The archer opens: ATTACK with a target this time, the forecast panel
//      read off the enemy under the cursor, and the shot. The rest of the guard
//      steps up to meet what is coming.
//   4. The line fights. Both knights strike what is now adjacent, the archer
//      shoots the wounded one again, and the mage takes CINDER ARC out of the
//      fourth row of its menu and aims it. That is an area cast, the one
//      gesture on this board that asks the cursor to be more than one tile.
//   5. The last of it: a strike the forecast calls lethal before it is thrown,
//      and the draught. The knight walks before it swings, into the square the
//      Coil's own fallen left, because the one worn down to three is standing
//      beside that square and not beside the knight. So this round is also
//      where the two-point turn is spelled out end to end: WALK from the menu,
//      and then `pad_c` for a menu whose first row is ATTACK, because a
//      character that has walked is offered no second walk. The mage's menu
//      then holds `FIELD TONIC x1` between its spells and the end of its turn,
//      and taking that row commits on the press because an item reaches the
//      hand that holds it. The mage is unhurt when it drinks, so what the row
//      proves here is that the draught is spent either way and that a heal does
//      not carry a character past its own maximum. The board menu ends the side
//      one last time.
//
// A d-pad press does not always mean one cell. While a strike or a talk is held
// the cursor rests only on tiles the engine lit for it, so taking ATTACK puts
// the cursor on the nearest character the weapon reaches and a press steps to
// another of them (see `client::gesture_names_a_character`). The steering
// presses inside a strike's block therefore choose among targets rather than
// walk the board, and on a strike that reaches exactly one character they move
// nothing at all. A walk and a cast are aimed at ground and steer one cell per
// press, as everything outside a pick does.
//
// Red's replies are `tactics::decide`'s and are not scripted at all; the
// transcript records what the policy actually did, and the ROM has to agree.
//
// Nothing below is chosen for a number. Every value the checkpoints pin is
// derived on the host by replaying exactly these presses through
// `grandleon_playstation_expect`, and re-derived rather than edited whenever
// this script or the content under it moves. That covers the bow's
// `95% HIT 3 LEFT 9`, the sword's `HIT 5`, and the `HIT 5 KO` the fifth round
// opens with.

#include <grandleon/client/turn_client.hpp>

namespace grandleon::client::turn {

// One press. `pad_none` is a press of nothing, which no script needs and which
// is spelled out so an empty entry is a mistake rather than a pause.
inline constexpr std::uint16_t pad_none = 0;

inline constexpr std::uint16_t fordlight_presses[] = {
    pad_down, pad_left,
    pad_a,
    pad_down, pad_down, pad_down, pad_down,
    pad_a, pad_b, pad_b,
    pad_right, pad_right,
    pad_left,
    pad_a,

    pad_left, pad_down,
    pad_a, pad_a, pad_right, pad_a,

    pad_up, pad_up,
    pad_a, pad_down, pad_a, pad_b,
    pad_a, pad_up, pad_up, pad_up, pad_a,

    pad_down, pad_down, pad_down,
    pad_a,
    pad_down, pad_down, pad_down, pad_down, pad_down, pad_down,
    pad_a, pad_b,
    pad_up, pad_a,

    pad_up, pad_up,
    pad_a, pad_b,
    pad_start, pad_b, pad_b,
    pad_start, pad_down, pad_a,

    pad_up,
    pad_a, pad_down, pad_a,
    pad_right, pad_right, pad_down,
    pad_a,

    pad_left, pad_left,
    pad_a, pad_a, pad_right, pad_a,
    pad_c, pad_up, pad_up, pad_up, pad_a,

    pad_left, pad_down,
    pad_a, pad_a, pad_right, pad_a,
    pad_c, pad_up, pad_up, pad_up, pad_a,

    pad_left, pad_down,
    pad_a, pad_a, pad_right, pad_a,
    pad_c, pad_up, pad_up, pad_up, pad_a,

    pad_up, pad_up,
    pad_a, pad_down, pad_a, pad_right, pad_a,

    pad_left, pad_left, pad_up,
    pad_a, pad_down, pad_a,
    pad_right, pad_right, pad_down,
    pad_a,

    pad_left, pad_down,
    pad_a, pad_down, pad_a, pad_right, pad_a,

    pad_left, pad_down,
    pad_a, pad_down, pad_down, pad_down, pad_a,
    pad_right, pad_up,
    pad_a,

    pad_left, pad_up,
    pad_a, pad_a, pad_right, pad_a,
    pad_c, pad_a, pad_a,

    pad_left, pad_down,
    pad_a, pad_down, pad_down, pad_down, pad_down, pad_a,

    pad_left, pad_up, pad_up, pad_up,
    pad_a, pad_up, pad_up, pad_up, pad_a,

    pad_start, pad_down, pad_a,
};

inline constexpr std::size_t fordlight_press_count =
    sizeof fordlight_presses / sizeof fordlight_presses[0];

}  // namespace grandleon::client::turn
