// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstdint>

namespace grandleon::core {

inline constexpr std::uint8_t maximum_unit_level = 99;

struct EnemyLevels final {
    std::uint8_t level{};
    std::uint8_t two_player_level{};

    [[nodiscard]] constexpr bool specified() const noexcept {
        return level != 0 || two_player_level != 0;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return level <= maximum_unit_level &&
               two_player_level <= maximum_unit_level;
    }
};

[[nodiscard]] constexpr std::uint8_t effective_enemy_level(
    EnemyLevels battle,
    EnemyLevels placement,
    bool two_players
) noexcept {
    if (two_players && placement.two_player_level != 0) {
        return placement.two_player_level;
    }
    if (placement.level != 0) return placement.level;
    if (two_players && battle.two_player_level != 0) {
        return battle.two_player_level;
    }
    return std::max<std::uint8_t>(1, battle.level);
}

[[nodiscard]] constexpr std::int32_t projected_stat_at_level(
    std::int32_t base, std::uint8_t level, std::uint8_t growth,
    std::int32_t maximum
) noexcept {
    const auto levels_gained = std::max<std::int32_t>(0, level - 1);
    return std::min(maximum, base + (levels_gained * growth + 50) / 100);
}

}  // namespace grandleon::core
