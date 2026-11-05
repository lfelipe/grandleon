// SPDX-License-Identifier: MIT
#pragma once

// A short played-console proof of Emberbound's two-controller opening. Each
// synthetic edge below names the physical seat it stands in for. The script
// lets Player 2 spend Senna's turn, verifies that Auren remains available,
// then lets Player 1 spend Auren's turn and requires the battle to reach the
// next allied side block with both controller states still attached.

#include "autopilot.h"

namespace grandleon::n64autopilot {

inline constexpr Step emberbound_coop_autopilot[] = {
    {Op::wait, 120},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::title), 0, 0,
     "coop-title"},
    {Op::press, button_start},
    {Op::wait, 60},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::controls), 0, 0,
     "coop-controls"},
    {Op::press, button_a},

    // Six prologue lines.
    {Op::wait, 30},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::cutscene), 0, 0,
     "coop-prologue"},
    {Op::press, button_a}, {Op::wait, 8},
    {Op::press, button_a}, {Op::wait, 8},
    {Op::press, button_a}, {Op::wait, 8},
    {Op::press, button_a}, {Op::wait, 8},
    {Op::press, button_a}, {Op::wait, 8},
    {Op::press, button_a},

    // Four opening-board orders. The first order wraps onto two of the N64's
    // three-row speech pages, so this scene needs five acknowledgements.
    {Op::wait, 45},
    {Op::press, button_a}, {Op::wait, 8},
    {Op::press, button_a}, {Op::wait, 8},
    {Op::press, button_a}, {Op::wait, 8},
    {Op::press, button_a}, {Op::wait, 8},
    {Op::press, button_a},
    {Op::wait, 90},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::coop_open), 0, 0,
     "coop-open"},

    // A zero-button edge changes only the active local seat. Player 2's
    // cursor was seeded on Senna by the real presenter.
    {Op::press, 0, 0, 0, nullptr, 1},
    {Op::wait, 10, 0, 0, nullptr, 1},
    {Op::press, button_a, 0, 0, nullptr, 1},
    {Op::wait, 20, 0, 0, nullptr, 1},
    {Op::checkpoint,
     static_cast<std::uint16_t>(Check::coop_player_two_menu), 0, 0,
     "coop-player-two-menu", 1},
    // From WALK, three steps up wrap to END CHARACTER TURN.
    {Op::press, button_d_up, 0, 0, nullptr, 1},
    {Op::press, button_d_up, 0, 0, nullptr, 1},
    {Op::press, button_d_up, 0, 0, nullptr, 1},
    {Op::wait, 8, 0, 0, nullptr, 1},
    {Op::press, button_a, 0, 0, nullptr, 1},
    {Op::wait, 25, 0, 0, nullptr, 1},
    {Op::checkpoint,
     static_cast<std::uint16_t>(Check::coop_player_two_acted), 0, 0,
     "coop-player-two-acted", 1},

    // Player 1's cursor is still on Auren and its selection state is its own.
    {Op::press, 0, 0, 0, nullptr, 0},
    {Op::wait, 10},
    {Op::press, button_a},
    {Op::wait, 20},
    {Op::checkpoint,
     static_cast<std::uint16_t>(Check::coop_player_one_menu), 0, 0,
     "coop-player-one-menu"},
    {Op::press, button_d_up},
    {Op::press, button_d_up},
    {Op::press, button_d_up},
    {Op::wait, 8},
    {Op::press, button_a},

    // The deterministic enemy block answers before the script is polled
    // again. The next input opportunity must therefore be round two.
    {Op::wait, 90},
    {Op::checkpoint,
     static_cast<std::uint16_t>(Check::coop_round_advanced), 0, 0,
     "coop-round-two"},
    {Op::finish},
};

}  // namespace grandleon::n64autopilot
