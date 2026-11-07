// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace grandleon::nintendo64::sevenfold_backdrop_renderer {

enum class TransitionOrientation : std::uint8_t {
    north_south = 0,
};

struct Descriptor final {
    std::uint8_t dominant_terrain;
    std::uint8_t secondary_terrain;
    std::uint8_t transition_terrain;
    TransitionOrientation orientation;
    std::uint32_t variant_seed;
};

[[nodiscard]] bool initialize();
[[nodiscard]] int select_variant(const Descriptor& descriptor) noexcept;
void draw(const Descriptor& descriptor);

[[nodiscard]] std::size_t embedded_bytes() noexcept;
[[nodiscard]] inline std::size_t rdram_bytes() noexcept {
    return embedded_bytes();
}
[[nodiscard]] constexpr std::size_t peak_texture_bytes() noexcept {
    // One 80x40 four-bit tile plus a sixteen-entry RGBA16 lookup table.
    return 80U * 40U / 2U + 16U * 2U;
}

}  // namespace grandleon::nintendo64::sevenfold_backdrop_renderer
