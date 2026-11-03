// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace grandleon::package_runtime::target_budget {

// How many characters one encounter may field on each fixed-budget target.
// These are target policy, not portable game rules: PC has no corresponding
// cap, and a project over either number remains valid and playable there.
//
// Both values come from the same engaged 20x14 stress shape and the same pair
// of queries picking a character up starts: `reachable_tiles`, followed by the
// opposing side's `danger_tiles` over that reach. Recurring arrivals count as
// the characters they expand into, which is what the package loader enforces.
//
// Nintendo 64: 48 characters take 34.5 ms on the console's COP0 clock under
// the pinned ares gate. The measurement and its neighbouring points live next
// to the load in platform/nintendo64/src/play_rom.cpp.
inline constexpr std::uint32_t nintendo64_units_per_encounter = 48U;

// PlayStation: 48 characters take 21.8 ms on the R3000A root counter under the
// pinned PCSX-Redux gate. platform/playstation/src/conformance_exe.cpp reports
// the 20, 32 and 48-character points on every conformance run.
inline constexpr std::uint32_t playstation_units_per_encounter = 48U;

}  // namespace grandleon::package_runtime::target_budget
