// SPDX-License-Identifier: MIT
#pragma once

// What the Fordlight autopilot is allowed to believe about a blow.
//
// These are the numbers the Nintendo 64 ROM asserts on the console when it
// prices the Archer's opening shot at an Ashen Knight. They are *not* read off
// a screen and they are not typed in from one:
// `tests/nintendo64/fordlight_expectations_test.cpp` links the real engine,
// compiles the same checked-in project the ROM embeds, loads the same board,
// finds the same pairing by what the two of them are holding, and asserts every
// constant below against what `simulation::forecast_attack` actually answered.
// The ROM includes this header.
//
// So there is one place this fact is written down, and it is a place the host
// gate checks in a fraction of a second. It exists because the numbers used to
// live in `play_rom.cpp` as literals, and literals go stale silently: the
// weapon triangle shipped, the Archer's shot went from `95% HIT 3` to
// `80% HIT 2`, and the console check had been failing ever since with nothing
// saying so -- because no automated build compiles either console port, and the
// only run that would have caught it is opt-in behind `--n64`.
//
// The campaign side of this ROM already worked this way
// (`campaign_expectations.h`), and its own comment states the principle these
// constants are here to borrow: the only way to change what the console checks
// is to change what the host derives.

#include <cstdint>

namespace grandleon::tarnholt {

// The Archer's opening shot at an Ashen Knight on full health.
//
// The Long Bow is power two authored at ninety accuracy and the Archer carries
// five points of skill, which would fold to ninety-five. A knight holds a
// blade, and the shipped table makes a blade strong against a bow, so the shot
// is made *into* the advantage and the game's `weaponAdvantage` comes off both
// numbers rather than going on: one off the blow and fifteen off the chance.
inline constexpr std::int16_t bow_at_knight_damage = 2;
inline constexpr std::int16_t bow_at_knight_health_after = 10;
inline constexpr std::uint8_t bow_at_knight_chance = 80;

// And it does not fell a knight on twelve, which is the other half of what the
// panel says and the reason the round goes on.
inline constexpr bool bow_at_knight_lethal = false;

// What the line has worn the last Ashen Knight down to by the end of the run,
// and what the mage's staff does to it.
//
// The other side of the same table. An Ember Staff is a staff and the knight
// holds a blade, so this blow is struck *with* the advantage where the Archer's
// was struck into it: the rule that cost the company both of its knights is the
// rule that finishes this one.
inline constexpr std::int16_t worn_knight_health = 3;
inline constexpr std::int16_t staff_at_worn_knight_damage = 6;
inline constexpr bool staff_at_worn_knight_lethal = true;

}  // namespace grandleon::tarnholt
