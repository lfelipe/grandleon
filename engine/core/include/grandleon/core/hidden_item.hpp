// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace grandleon::core {

inline constexpr std::uint16_t maximum_hidden_items = 64;

struct HiddenItem final {
    std::uint64_t flag{};
    std::uint64_t item{};
    std::int16_t x{};
    std::int16_t y{};
    bool weapon{};
    bool glint{true};
};

}  // namespace grandleon::core
