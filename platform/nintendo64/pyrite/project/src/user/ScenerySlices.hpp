// SPDX-License-Identifier: MIT
#pragma once
#include <array>

namespace grandleon::nintendo64::pyrite::scenery_slices {
inline constexpr int side = 64;
struct Slice { int first, last, upload_first, upload_end; };
// UVs stay in the original image's coordinates. One row beyond each internal
// join gives bilinear filtering the same neighbours on both draws. Each CI8
// upload stays below the 2 KiB available alongside a 256-colour TLUT.
inline constexpr std::array<Slice, 3> strips{{
    {0, 21, 0, 23}, {21, 42, 20, 44}, {42, 63, 41, 64}
}};
inline constexpr Slice whole{0, 63, 0, 64};
inline constexpr unsigned vertices_per_house = strips.size() * 4U;
inline constexpr unsigned pairs_per_house = vertices_per_house / 2U;
constexpr float vertical(int row, float half) {
    return half - (2.0F * half * static_cast<float>(row)) / (side - 1);
}
}
