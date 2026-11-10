// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace grandleon::view {

struct IdlePose final {
    std::int32_t stretch_q12{4096};
    std::int32_t lean_q12{};
};

// A slow smooth pulse, sampled at 30 Hz. Identity offsets keep nearby units
// from moving together. It changes their picture, never their board position.
[[nodiscard]] constexpr std::int32_t ambient_wave_q12(
    std::uint32_t tick, std::uint32_t phase
) noexcept {
    const std::uint32_t cycle = (tick + phase) & 127U;
    const std::int32_t folded = static_cast<std::int32_t>(
        cycle < 64U ? cycle : 127U - cycle
    );
    const std::int32_t t = folded << 6;
    const std::int32_t square = (t * t) >> 12;
    const std::int32_t smooth = 3 * square - ((2 * square * t) >> 12);
    return smooth * 2 - 4096;
}

[[nodiscard]] constexpr IdlePose idle_pose(
    std::uint32_t tick, std::uint64_t identity, bool enabled = true
) noexcept {
    if (!enabled) return {};
    const std::uint32_t seed = static_cast<std::uint32_t>(identity) ^
        static_cast<std::uint32_t>(identity >> 32U);
    return {
        4096 + ambient_wave_q12(tick, seed) * 96 / 4096,
        ambient_wave_q12(tick / 2U, seed ^ 53U) * 72 / 4096,
    };
}

}  // namespace grandleon::view
