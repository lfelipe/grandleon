// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace grandleon::core {

struct RoleMatchup final {
    std::uint64_t strong{};
    std::uint64_t weak{};

    [[nodiscard]] auto pair() const noexcept {
        return std::pair{std::min(strong, weak), std::max(strong, weak)};
    }
};

struct RoleMatchups final {
    static constexpr std::int16_t maximum_damage = 999;
    static constexpr std::size_t maximum_pairs = 1024;

    std::int16_t damage{};
    // There is one direction per pair. Sorted by the two role IDs without
    // regard to the direction, so looking up either half finds the same edge.
    std::vector<RoleMatchup> pairs;

    [[nodiscard]] bool active() const noexcept { return damage > 0 && !pairs.empty(); }

    [[nodiscard]] bool valid() const noexcept {
        if (damage < 0 || damage > maximum_damage || pairs.size() > maximum_pairs) return false;
        std::pair<std::uint64_t, std::uint64_t> previous{};
        for (const auto& entry : pairs) {
            const auto key = entry.pair();
            if (key.first == 0 || key.first == key.second || key <= previous) return false;
            previous = key;
        }
        return true;
    }

    [[nodiscard]] std::int16_t modifier(std::uint64_t attacker, std::uint64_t target) const noexcept {
        const auto key = RoleMatchup{attacker, target}.pair();
        const auto found = std::lower_bound(pairs.begin(), pairs.end(), key,
            [](const RoleMatchup& entry, const auto& pair) { return entry.pair() < pair; });
        if (found == pairs.end() || found->pair() != key) return 0;
        return found->strong == attacker ? damage : static_cast<std::int16_t>(-damage);
    }
};

}  // namespace grandleon::core
