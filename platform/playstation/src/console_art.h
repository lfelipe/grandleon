// SPDX-License-Identifier: MIT
#pragma once

#include "terrain_ci4.h"

#ifndef GRANDLEON_PSX_CONSOLE_ART_HEADER
#error "the build must name its generated project console art header"
#endif
#include GRANDLEON_PSX_CONSOLE_ART_HEADER

namespace grandleon::playstation::art {

inline constexpr int cell_size = 32;
inline constexpr int texels_per_halfword = 4;
inline constexpr int halfwords_per_row = cell_size / texels_per_halfword;
inline constexpr int halfwords_per_cell = halfwords_per_row * cell_size;
inline constexpr int clut_size = 16;
inline constexpr int terrain_kind_count = 13;
inline constexpr int theme_count = 4;
inline constexpr int variant_count = 4;
inline constexpr int style_count = 7;
inline constexpr int figure_count = 2;
inline constexpr int archetype_count = 8;
inline constexpr int faction_colour_count = 6;
inline constexpr int project_default_style =
    grandleon_console_project_default_style;
inline constexpr int project_default_figure =
    grandleon_console_project_default_figure;
inline constexpr int character_count = grandleon_console_character_count;
inline constexpr int character_side = grandleon_console_character_side;

enum class Pose : int {
    idle,
    move,
    windup,
    attack,
    hurt,
};

struct Asset final {
    const unsigned short* texels;
    const unsigned short* clut;
    int side;
};

[[nodiscard]] inline Asset terrain(
    int theme, int kind, int variant
) noexcept {
    static_cast<void>(theme);
    const int material = kind >= 0 && kind < terrain_kind_count ? kind : 9;
    return {
        grandleon_psx_quality_terrain_cells[material * 4 + (variant & 3)],
        grandleon_psx_quality_terrain_cluts[material],
        cell_size,
    };
}

static_assert(
    grandleon_psx_quality_terrain_kind_count == terrain_kind_count,
    "the generated PS1 material pack must cover every portable terrain kind"
);

[[nodiscard]] inline int character_index(
    int style, int figure, int archetype, int colour
) noexcept {
    for (int index = 0; index < character_count; ++index) {
        const auto& drawing = grandleon_console_characters[index];
        if (drawing.style == style && drawing.figure == figure &&
            drawing.archetype == archetype && drawing.colour == colour) {
            return index;
        }
    }
    return -1;
}

[[nodiscard]] inline Asset character(
    int index, Pose pose = Pose::idle
) noexcept {
    if (index < 0 || index >= character_count) return {nullptr, nullptr, 0};
    const int frame = static_cast<int>(pose);
    return {
        grandleon_console_characters[index].texels[frame],
        grandleon_console_characters[index].cluts[frame],
        character_side,
    };
}

[[nodiscard]] inline Asset character(
    int style, int figure, int archetype, int colour,
    Pose pose = Pose::idle
) noexcept {
    return character(character_index(style, figure, archetype, colour), pose);
}

[[nodiscard]] inline int index_at(
    const Asset& asset, int x, int y
) noexcept {
    if (asset.texels == nullptr || asset.side <= 0) return 0;
    const int words = asset.side / texels_per_halfword;
    const unsigned short word =
        asset.texels[y * words + x / texels_per_halfword];
    return (word >> (4 * (x % texels_per_halfword))) & 0xF;
}

[[nodiscard]] inline unsigned short colour_at(
    const Asset& asset, int x, int y
) noexcept {
    if (asset.clut == nullptr) return 0;
    return asset.clut[index_at(asset, x, y)];
}

}  // namespace grandleon::playstation::art
