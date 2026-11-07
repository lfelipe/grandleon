// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace grandleon::nintendo64::sevenfold_texture_experiment {

struct UV final {
    std::int16_t s;
    std::int16_t t;
};

// Builds two 32x32 CI4 atlases in RDRAM. Their palettes are deliberately
// greyscale: RDPQ_COMBINER_TEX_SHADE multiplies them by the existing vertex
// colours, preserving team and style identity instead of baking either into a
// texture.
void initialize();
void bind_terrain();
void bind_character();

[[nodiscard]] UV character_uv(
    const std::int16_t points[3][3], int corner, std::uint8_t material
) noexcept;

[[nodiscard]] UV terrain_uv(
    const std::int16_t points[3][3], int corner, std::uint8_t region
) noexcept;

}  // namespace grandleon::nintendo64::sevenfold_texture_experiment
