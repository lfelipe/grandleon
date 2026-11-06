// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace grandleon::nintendo64::sevenfold_presentation {

// The procedural proxies put their role carriers to the character's left and
// right. An exact quarter turn hides those shields, bows, books and blades in
// depth from the board camera. These fixed-point coefficients turn either
// side only forty-five degrees: the unit still faces its opponent, while the
// camera keeps enough of the front silhouette to identify its role.
struct RotatedXZ final {
    int x;
    int z;
};

[[nodiscard]] constexpr RotatedXZ three_quarter(
    int x, int z, int facing
) noexcept {
    constexpr int sine_and_cosine_256 = 181;
    if (facing < 0) {
        return {
            (x + z) * sine_and_cosine_256 / 256,
            (-x + z) * sine_and_cosine_256 / 256,
        };
    }
    if (facing > 0) {
        return {
            (x - z) * sine_and_cosine_256 / 256,
            (x + z) * sine_and_cosine_256 / 256,
        };
    }
    return {x, z};
}

// Preserve the generator's directional light while opening the gap between
// its shadow and highlight faces. The previous 128 + shade/2 mapping confined
// every face to 65-100% brightness; this keeps highlights intact and lets the
// darkest generated face reach 56% without adding runtime lights or normals.
[[nodiscard]] constexpr std::uint32_t light_for(
    std::uint8_t baked_shade
) noexcept {
    return 96U + static_cast<std::uint32_t>(baked_shade) * 159U / 255U;
}

static_assert(
    three_quarter(0, -64, 1).x > 0 &&
        three_quarter(0, -64, -1).x < 0,
    "the two tactical sides must retain opposite headings"
);
static_assert(
    three_quarter(0, -64, 1).z == three_quarter(0, -64, -1).z,
    "the two tactical headings must remain equally camera-facing"
);
static_assert(
    light_for(76) < 128U + 76U / 2U && light_for(255) == 255U,
    "N64 proxy shading must deepen shadows without clipping highlights"
);

}  // namespace grandleon::nintendo64::sevenfold_presentation
