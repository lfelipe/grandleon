// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <vector>

namespace grandleon::package_runtime {
enum class DefeatPolicy : std::uint8_t {
    inherit = 0, permanent = 1, withdraw = 2, lose_battle = 3,
};
struct MemberRules final {
    std::uint64_t member_id{};
    std::vector<std::uint64_t> weapon_affinities;
    std::vector<std::uint64_t> starting_weapons;
    bool states_weapon_affinities{false};
    bool states_starting_weapons{false};
    DefeatPolicy defeat_policy{DefeatPolicy::inherit};
    std::uint16_t carry_capacity{};
    bool states_carry_capacity{false};
};
}  // namespace grandleon::package_runtime
