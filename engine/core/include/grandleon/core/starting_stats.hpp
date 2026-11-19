// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/core/random.hpp>

#include <array>
#include <cstdint>

namespace grandleon::core {

enum class StartingStat : std::uint8_t {
    health, strength, defense, resistance, movement, action_points,
    skill, luck, evasion, magic, speed,
};

inline constexpr std::size_t starting_stat_count = 11;
inline constexpr std::int16_t maximum_starting_offset = 999;
inline constexpr std::uint8_t maximum_starting_level = 99;

// Campaign save integers are signed. Reserve zero for saves predating rolls.
[[nodiscard]] constexpr std::uint64_t saved_starting_seed(std::uint64_t seed) noexcept {
    const auto positive = seed & 0x7fffffffffffffffULL;
    return positive == 0 ? 1U : positive;
}

struct StartingStatRange final {
    std::int16_t low{};
    std::int16_t high{};

    [[nodiscard]] bool authored() const noexcept { return low != 0 || high != 0; }
    bool operator==(const StartingStatRange& other) const noexcept {
        return low == other.low && high == other.high;
    }

    [[nodiscard]] bool valid() const noexcept {
        return low >= -maximum_starting_offset && high <= maximum_starting_offset && low <= high;
    }
};

using StartingStatRanges = std::array<StartingStatRange, starting_stat_count>;
using StartingStatOffsets = std::array<std::int16_t, starting_stat_count>;

struct RoleStartingStats final {
    StartingStatRanges ranges{};
    bool vary_enemies{};

    [[nodiscard]] bool authored() const noexcept {
        if (vary_enemies) return true;
        for (const auto& range : ranges) if (range.authored()) return true;
        return false;
    }
};

struct CharacterStart final {
    std::uint8_t level{1};
    bool fixed_stats{};

    [[nodiscard]] bool authored() const noexcept { return level != 1 || fixed_stats; }
};

// The starting-stat domain is separate from the combat and advancement rolls.
// A new range never moves another stat's draw.
[[nodiscard]] inline StartingStatOffsets roll_starting_stats(
    const StartingStatRanges& ranges, std::uint64_t seed, std::uint64_t character
) noexcept {
    constexpr std::uint64_t domain = 0x6db7b49fac68b361ULL;
    const auto member_seed = random_draw(seed ^ domain, RandomStream::growth, character);
    StartingStatOffsets result{};
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        const auto& range = ranges[index];
        const auto span = static_cast<std::uint32_t>(range.high - range.low + 1);
        result[index] = range.low;
        if (span > 1U) {
            result[index] += static_cast<std::int16_t>(
                random_below(random_draw(member_seed, RandomStream::growth, index), span));
        }
    }
    return result;
}

}  // namespace grandleon::core
