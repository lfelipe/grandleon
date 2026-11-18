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
// to the load in the Nintendo 64 project's GPK runtime adapter.
inline constexpr std::uint32_t nintendo64_units_per_encounter = 48U;

// Largest board supported by the base-4-MiB renderer arenas. Individual asset
// and frame-time budgets still need export-time checks.
inline constexpr std::uint32_t nintendo64_cells_per_map = 32U * 24U;
inline constexpr std::uint32_t nintendo64_terrain_bytes_per_cell = 256U;
inline constexpr std::uint32_t nintendo64_terrain_kinds_per_campaign = 9U;
inline constexpr std::uint32_t nintendo64_main_ram_bytes = 4U * 1024U * 1024U;

// PlayStation: the production Mipsync exporter validates this cap before it
// writes any stage. Its build manifest also records the resulting RAM and VRAM
// budgets so a release artifact can be audited without a second build.
inline constexpr std::uint32_t playstation_units_per_encounter = 48U;

// Scene and linked-build limits in the production Mipsync exporter.
inline constexpr std::uint32_t playstation_map_side = 64U;
inline constexpr std::uint32_t playstation_vertex_pool = 4352U;
inline constexpr std::uint32_t playstation_triangle_pool = 4096U;
inline constexpr std::uint32_t playstation_main_ram_bytes = 2U * 1024U * 1024U;
inline constexpr std::uint32_t playstation_texture_vram_bytes = 704U * 512U * 2U;

}  // namespace grandleon::package_runtime::target_budget
