// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace grandleon::core {

struct Version final {
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t patch;
};

[[nodiscard]] constexpr bool operator==(Version lhs, Version rhs) noexcept {
    return lhs.major == rhs.major && lhs.minor == rhs.minor && lhs.patch == rhs.patch;
}

[[nodiscard]] Version engine_version() noexcept;

}  // namespace grandleon::core
