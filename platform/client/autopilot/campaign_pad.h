// SPDX-License-Identifier: MIT
#pragma once

// The autopilot's controller scripts for a *campaign*: a deterministic sequence
// of button presses, one per entry, in the order a thumb would make them.
//
// Two scripts per campaign, one per emulator process, because a save is the one
// claim a console cannot make from inside a single run. Which one plays is
// decided by what the memory card is holding and by nothing the executable
// carries. That is the property under test, so it must not be a flag: a card
// that forgot would take the founding script a second time and reach a screen
// the resuming expectations do not have.
//
//   `*_found`    the card holds no campaign. The title, the slot screen's only
//                offer, the company as it was founded, every authored line of
//                every story node in the way, the management stage, the battle
//                itself fought to its objective, the aftermath, and the end.
//
//   `*_resume`   a second process over the same card. The slot screen now
//                offers CONTINUE and the caret is already on it, so the same
//                press means the other thing. The campaign comes back where the
//                first run left it.
//
// It is only the input. Nothing here says what should happen. Every
// expectation is derived from the rules by `grandleon_playstation_campaign_expect`,
// which replays exactly these sequences against the real engine on the host and
// writes out the transcript the console is then required to reproduce. Adding a
// press here changes what is played; it can never change what passes.
//
// **It lives here rather than beside one console** for the reason
// `fordlight_pad.h` gives: every console that plays these campaigns plays them
// through the same client, and a second script would be a second session: two
// machines proving different things while looking like they proved the same
// one. The buttons are named in the client's vocabulary rather than any
// machine's.
//
// ---------------------------------------------------------------------------
// How the sequences were arrived at, so that a reader can arrive at them again
//
// Not by hand. A campaign is long enough that a hand-written script is a script
// that drifts the first time a board changes, so these are played out on the
// host by a policy: walk the reachable tile nearest the enemy, strike whatever
// is in reach, end the side when nobody is left to move, take the only offer on
// every screen. The presses it made are written down. What is frozen here is
// the *record* of that play, not the policy: the console replays a list, and a
// list is a thing a host can derive expectations from.
//
// The policy is `grandleon_playstation_campaign_expect`'s `record` mode, and
// re-recording is one command:
//
//     grandleon_playstation_campaign_expect <project.json>
//         tarnholt_line tarnholt record out.txt
//
// which writes the array body below, ready to paste. It steers by what the
// client itself put on screen rather than by counting presses, so a board that
// moved costs a re-record rather than an afternoon.
//
// Ending a side is the board menu's row and not a press, which is what most of
// the length here is: this game plays in side blocks, so a side is finished one
// character at a time and the turn passes only when the last of them is done.
//
// The consequence worth knowing is that a content change does not silently
// change what is proved. It changes what the script *reaches*, the host
// derivation follows it, and the console then disagrees with the derivation.
// That fails loudly, in the harness, on the first line that differs.

#include <grandleon/client/turn_client.hpp>

#include <cstddef>
#include <cstdint>

namespace grandleon::client::turn {

// ---------------------------------------------------------------------------
// Tarnholt: the kept campaign
//
// Founding reads the three story nodes' authored lines, stands the company at
// the Fordlight, fights the crossing to its end, reads the aftermath the
// campaign committed, and follows the campaign to the terminal that outcome
// leads to. Resuming comes back to a company that has already fought.
//
// **What it reaches is a fact about the content and not a length anybody
// chose**, and that cuts both ways: the recorded policy walks the reachable
// tile nearest the enemy and strikes whatever is in reach, and against the
// Fordlight's Ashen Coil that is not enough to take the crossing. A walk may
// pass through a character on its own side, so the tile nearest the enemy is
// often past this side's own front and a character that takes it stands alone
// in front of four answers; and the Coil's Stormcaller stands in its own
// cluster and casts into the guard, because a damaging cast harms the caster's
// opponents alone. The script records whatever that run produces, and
// re-recording is one command whenever the board underneath it moves. The
// transcript is derived from it, so a losing pass is checked exactly as tightly
// as a winning one.
// ---------------------------------------------------------------------------

inline constexpr std::uint16_t tarnholt_campaign_found[] = {
    pad_start, pad_a, pad_start, pad_a, pad_a, pad_a,
    pad_a, pad_a, pad_a, pad_a, pad_a, pad_a,
    pad_a, pad_a, pad_a, pad_start, pad_a, pad_b,
    pad_right, pad_right, pad_a, pad_left, pad_left, pad_left,
    pad_down, pad_a, pad_b, pad_right, pad_right, pad_up,
    pad_a, pad_left, pad_left, pad_down, pad_down, pad_a,
    pad_b, pad_right, pad_right, pad_right, pad_a, pad_left,
    pad_left, pad_down, pad_a, pad_b, pad_right, pad_right,
    pad_a, pad_up, pad_up, pad_up, pad_a, pad_b,
    pad_right, pad_right, pad_right, pad_right, pad_a, pad_left,
    pad_left, pad_left, pad_left, pad_left, pad_a, pad_b,
    pad_right, pad_right, pad_down, pad_a, pad_left, pad_down,
    pad_a, pad_b, pad_right, pad_right, pad_a, pad_a,
    pad_b, pad_a, pad_left, pad_left, pad_left, pad_down,
    pad_a, pad_b, pad_right, pad_up, pad_a, pad_a,
    pad_b, pad_a, pad_left, pad_left, pad_left, pad_up,
    pad_up, pad_a, pad_b, pad_right, pad_right, pad_right,
    pad_a, pad_left, pad_left, pad_down, pad_a, pad_b,
    pad_right, pad_right, pad_a, pad_a, pad_b, pad_a,
    pad_left, pad_left, pad_down, pad_down, pad_a, pad_b,
    pad_right, pad_right, pad_a, pad_left, pad_left, pad_left,
    pad_up, pad_up, pad_a, pad_b, pad_right, pad_down,
    pad_a, pad_left, pad_up, pad_a, pad_b, pad_right,
    pad_right, pad_right, pad_down, pad_a, pad_a, pad_b,
    pad_down, pad_down, pad_down, pad_a, pad_a, pad_a,
    pad_a, pad_a,
};

inline constexpr std::size_t tarnholt_campaign_found_count =
    sizeof tarnholt_campaign_found / sizeof tarnholt_campaign_found[0];

inline constexpr std::uint16_t tarnholt_campaign_resume[] = {
    pad_start, pad_a, pad_a, pad_a, pad_a,
};

inline constexpr std::size_t tarnholt_campaign_resume_count =
    sizeof tarnholt_campaign_resume / sizeof tarnholt_campaign_resume[0];

// ---------------------------------------------------------------------------
// The demo: the teaching board
//
// One member against one opponent on a six-by-four bridge. It is here because
// it is the other campaign this repository ships, and a save path that works on
// one campaign and not the other is a save path nobody has tested.
// ---------------------------------------------------------------------------

inline constexpr std::uint16_t demo_campaign_found[] = {
    pad_start, pad_a, pad_start, pad_start, pad_a, pad_b,
    pad_right, pad_right, pad_up, pad_a, pad_a, pad_b,
    pad_a, pad_up, pad_a, pad_b, pad_down, pad_a,
    pad_a, pad_a,
};

inline constexpr std::size_t demo_campaign_found_count =
    sizeof demo_campaign_found / sizeof demo_campaign_found[0];

inline constexpr std::uint16_t demo_campaign_resume[] = {
    pad_start, pad_a, pad_a, pad_a,
};

inline constexpr std::size_t demo_campaign_resume_count =
    sizeof demo_campaign_resume / sizeof demo_campaign_resume[0];

}  // namespace grandleon::client::turn
