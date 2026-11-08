// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace grandleon::view {

// The compact consoles consume the generated terrain registry in this order.
// Naming that order once lets both machines compose the same visual grammar
// without putting a second terrain-name lookup into either frame loop.
enum class ConsoleTerrainKind : std::uint8_t {
    water,
    road,
    forest,
    mountain,
    sand,
    snow,
    swamp,
    hills,
    ruins,
    grass,
    farmland,
    bamboo,
    paved,
};

enum TerrainEdge : std::uint8_t {
    terrain_north = 1U << 0,
    terrain_east = 1U << 1,
    terrain_south = 1U << 2,
    terrain_west = 1U << 3,
};

enum class TerrainTone : std::uint8_t {
    seam,
    shadow,
    accent,
};

struct TerrainColour final {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;

    friend constexpr bool operator==(
        TerrainColour left, TerrainColour right
    ) noexcept {
        return left.red == right.red && left.green == right.green &&
               left.blue == right.blue;
    }
};

struct TerrainComposition final {
    std::uint8_t kind{static_cast<std::uint8_t>(ConsoleTerrainKind::grass)};
    std::uint8_t variation{0};
    std::uint8_t same_edges{0};
    std::uint8_t lower_edges{0};
    std::uint8_t detail{0};
    std::uint8_t cluster{0};
    std::uint8_t battle_variant{0};
};

[[nodiscard]] constexpr TerrainColour terrain_base_colour(
    std::uint8_t kind
) noexcept {
    constexpr TerrainColour colours[] = {
        {43, 121, 158}, {142, 104, 66}, {58, 112, 63},
        {112, 124, 132}, {190, 150, 86}, {205, 224, 230},
        {72, 104, 82}, {108, 137, 73}, {116, 105, 96},
        {89, 151, 70}, {157, 129, 72}, {65, 127, 78},
        {144, 151, 145},
    };
    const std::size_t count = sizeof colours / sizeof colours[0];
    return colours[kind < count ? kind : 9U];
}

// Re-colours a five-bit material palette through a terrain's authored hue
// while preserving the source texel's luminance. Used when several logical
// kinds share one compact-console texture family: the detail remains the
// source art's, but water cannot read as road or snow as swamp.
[[nodiscard]] constexpr std::uint8_t console_terrain_tint_level(
    std::uint8_t luminance, std::uint8_t target_channel
) noexcept {
    const unsigned target = static_cast<unsigned>(target_channel) >> 3U;
    const unsigned scaled =
        (target * (static_cast<unsigned>(luminance) + 4U) + 12U) / 24U;
    return static_cast<std::uint8_t>(scaled < 32U ? scaled : 31U);
}

[[nodiscard]] constexpr int terrain_edge_count(std::uint8_t edges) noexcept {
    int count = 0;
    for (std::uint8_t bit = 1; bit <= terrain_west; bit <<= 1U) {
        if ((edges & bit) != 0U) ++count;
    }
    return count;
}

// Composes rather than paints: the package remains the authority on kind and
// elevation, while the renderer supplies the four neighbours it already has.
// A different-kind seam is drawn only from its south/east cell (north/west
// edges), so two adjacent cells never double-paint a heavy grid line.
[[nodiscard]] constexpr TerrainComposition compose_console_terrain(
    std::uint8_t kind, int x, int y,
    int north_kind, int east_kind, int south_kind, int west_kind,
    int elevation, int east_elevation, int south_elevation
) noexcept {
    std::uint8_t same = 0;
    if (north_kind == static_cast<int>(kind)) same |= terrain_north;
    if (east_kind == static_cast<int>(kind)) same |= terrain_east;
    if (south_kind == static_cast<int>(kind)) same |= terrain_south;
    if (west_kind == static_cast<int>(kind)) same |= terrain_west;

    std::uint8_t lower = 0;
    if (east_kind >= 0 && elevation > east_elevation) lower |= terrain_east;
    if (south_kind >= 0 && elevation > south_elevation) lower |= terrain_south;

    const std::uint32_t mixed =
        static_cast<std::uint32_t>(x * 37 + y * 71) ^
        (static_cast<std::uint32_t>(kind) * 97U) ^
        (static_cast<std::uint32_t>(same) * 13U);
    const std::uint8_t detail = static_cast<std::uint8_t>((mixed >> 2U) & 3U);
    const std::uint8_t cluster = static_cast<std::uint8_t>(
        terrain_edge_count(same)
    );
    return {
        kind,
        static_cast<std::uint8_t>(mixed & 3U),
        same,
        lower,
        detail,
        cluster,
        static_cast<std::uint8_t>(
            (static_cast<unsigned>(kind) * 5U + detail + cluster) % 3U
        ),
    };
}

[[nodiscard]] constexpr TerrainColour terrain_colour(
    std::uint8_t kind, TerrainTone tone
) noexcept {
    // The accent is the part a glance uses to distinguish terrains that share
    // one texture material. Shadow and seam remain deliberately quiet so the
    // cell art, unit and movement overlays stay above the decoration.
    constexpr TerrainColour accents[] = {
        {126, 211, 224}, {225, 181, 112}, {126, 184, 94},
        {205, 218, 224}, {244, 207, 132}, {255, 255, 255},
        {128, 166, 123}, {184, 202, 118}, {201, 179, 146},
        {151, 207, 111}, {225, 195, 106}, {125, 205, 139},
        {218, 224, 216},
    };
    if (tone == TerrainTone::seam) return {29, 39, 42};
    if (tone == TerrainTone::shadow) return {50, 54, 48};
    const std::size_t count = sizeof accents / sizeof accents[0];
    return accents[kind < count ? kind : 9U];
}

// Rectangles are intentionally the common denominator: both consoles render
// them with one native flat primitive, and a host test can prove they do not
// cover the cell centre used by selection/readback. The recipes keep their
// detail at the edges; units and walkable-cell highlights own the middle.
template <class Paint>
constexpr void paint_console_terrain(
    TerrainComposition terrain, int left, int top, int size, Paint&& paint
) {
    if (size < 6) return;
    const int thin = size >= 24 ? 2 : 1;
    const int short_mark = size >= 20 ? 5 : 3;
    const std::uint8_t transitions = static_cast<std::uint8_t>(
        (~terrain.same_edges) & (terrain_north | terrain_west)
    );
    if ((transitions & terrain_north) != 0U) {
        paint(left, top, size, thin, TerrainTone::seam);
    }
    if ((transitions & terrain_west) != 0U) {
        paint(left, top, thin, size, TerrainTone::seam);
    }
    if ((terrain.lower_edges & terrain_east) != 0U) {
        paint(
            left + size - thin, top + thin, thin, size - thin,
            TerrainTone::shadow
        );
    }
    if ((terrain.lower_edges & terrain_south) != 0U) {
        paint(
            left + thin, top + size - thin, size - thin, thin,
            TerrainTone::shadow
        );
    }

    const auto kind = static_cast<ConsoleTerrainKind>(terrain.kind);
    const int inset = thin + 1;
    const int far = left + size - inset - short_mark;
    const int low = top + size - inset - thin;
    const bool flip_x = (terrain.detail & 1U) != 0U;
    const bool flip_y = (terrain.detail & 2U) != 0U;
    const int near_x = flip_x ? far : left + inset;
    const int other_x = flip_x ? left + inset : far;
    const int upper_y = flip_y ? top + size / 3 : top + (size * 2) / 3;
    const int lower_y = flip_y ? top + (size * 2) / 3 : top + size / 3;
    switch (kind) {
        case ConsoleTerrainKind::water:
            paint(near_x, upper_y, short_mark, thin,
                  TerrainTone::accent);
            paint(other_x, lower_y, short_mark, thin,
                  TerrainTone::accent);
            break;
        case ConsoleTerrainKind::road:
            paint(near_x, flip_y ? low - short_mark : top + inset, thin,
                  short_mark,
                  TerrainTone::accent);
            paint(other_x + short_mark - thin,
                  flip_y ? top + inset : low - short_mark, thin, short_mark,
                  TerrainTone::accent);
            break;
        case ConsoleTerrainKind::forest:
        case ConsoleTerrainKind::bamboo:
            paint(near_x, top + inset, thin, short_mark,
                  TerrainTone::shadow);
            paint(near_x - thin, top + inset, short_mark, thin,
                  TerrainTone::accent);
            if (terrain.cluster >= 2U) {
                const int second_top = top + inset + (flip_y ? thin * 2 : 0);
                paint(other_x + short_mark - thin, second_top, thin, short_mark,
                      TerrainTone::shadow);
                paint(other_x, second_top, short_mark, thin,
                      TerrainTone::accent);
            }
            break;
        case ConsoleTerrainKind::mountain:
        case ConsoleTerrainKind::hills:
            paint(near_x, low - thin * 2, short_mark, thin,
                  TerrainTone::shadow);
            paint(near_x + (flip_x ? 0 : thin), low - thin * 3,
                  short_mark - thin, thin,
                  TerrainTone::accent);
            break;
        case ConsoleTerrainKind::sand:
        case ConsoleTerrainKind::snow:
            paint(near_x, flip_y ? low : top + inset, short_mark, thin,
                  TerrainTone::accent);
            paint(other_x, flip_y ? top + inset : low, short_mark, thin,
                  TerrainTone::accent);
            break;
        case ConsoleTerrainKind::swamp:
            paint(near_x, low, short_mark, thin, TerrainTone::shadow);
            paint(other_x, top + inset, short_mark, thin,
                  TerrainTone::accent);
            break;
        case ConsoleTerrainKind::ruins:
        case ConsoleTerrainKind::paved:
            paint(near_x, top + inset, short_mark, thin,
                  TerrainTone::accent);
            paint(near_x, top + inset, thin, short_mark,
                  TerrainTone::shadow);
            if (terrain.cluster >= 2U) {
                paint(other_x, low, short_mark, thin, TerrainTone::accent);
            }
            break;
        case ConsoleTerrainKind::grass:
            paint(near_x, low - short_mark / 2, thin, short_mark,
                  TerrainTone::accent);
            paint(near_x, low, short_mark, thin, TerrainTone::accent);
            break;
        case ConsoleTerrainKind::farmland:
            paint(near_x, flip_y ? low - short_mark : top + inset, thin,
                  short_mark,
                  TerrainTone::accent);
            paint(other_x + short_mark - thin,
                  flip_y ? top + inset : low - short_mark, thin, short_mark,
                  TerrainTone::accent);
            break;
    }
}

// The close plate remains pre-rendered; these sparse foreground marks make it
// belong to the actual battle cell. They are drawn before the characters, so
// silhouettes and combat chrome always remain authoritative.
template <class Paint>
constexpr void paint_console_close_terrain(
    TerrainComposition terrain, int width, int height, Paint&& paint
) {
    if (width < 160 || height < 120) return;
    const int horizon = (height * 3) / 5;
    const int floor = (height * 4) / 5;
    const auto kind = static_cast<ConsoleTerrainKind>(terrain.kind);
    switch (kind) {
        case ConsoleTerrainKind::water:
            paint(8, horizon + 9, 54, 2, TerrainTone::accent);
            paint(width - 72, floor, 64, 2, TerrainTone::accent);
            paint(width / 2 - 18, height - 18, 36, 2, TerrainTone::accent);
            break;
        case ConsoleTerrainKind::road:
        case ConsoleTerrainKind::farmland:
            for (int step = 0; step < 4; ++step) {
                paint(
                    width / 2 - 18 - step * 10, horizon + step * 18,
                    18 + step * 8, 2, TerrainTone::accent
                );
                paint(
                    width / 2 + step * 10, horizon + step * 18,
                    18 + step * 8, 2, TerrainTone::accent
                );
            }
            break;
        case ConsoleTerrainKind::forest:
        case ConsoleTerrainKind::bamboo:
            paint(10, horizon - 34, 5, 68, TerrainTone::shadow);
            paint(3, horizon - 35, 28, 8, TerrainTone::accent);
            paint(width - 15, horizon - 26, 5, 66, TerrainTone::shadow);
            paint(width - 31, horizon - 28, 28, 8, TerrainTone::accent);
            break;
        case ConsoleTerrainKind::mountain:
        case ConsoleTerrainKind::hills:
            paint(0, horizon + 8, width / 5, 5, TerrainTone::shadow);
            paint(width / 5, horizon, width / 5, 5, TerrainTone::accent);
            paint((width * 3) / 5, horizon + 2, width / 5, 5,
                  TerrainTone::accent);
            paint((width * 4) / 5, horizon + 10, width / 5, 5,
                  TerrainTone::shadow);
            break;
        case ConsoleTerrainKind::sand:
        case ConsoleTerrainKind::snow:
            paint(8, horizon + 12, 54, 2, TerrainTone::accent);
            paint(width - 76, horizon + 28, 68, 2, TerrainTone::accent);
            paint(width / 2 - 24, height - 16, 48, 2, TerrainTone::accent);
            break;
        case ConsoleTerrainKind::swamp:
            paint(6, floor, 60, 4, TerrainTone::shadow);
            paint(width - 70, floor + 10, 64, 4, TerrainTone::accent);
            break;
        case ConsoleTerrainKind::ruins:
        case ConsoleTerrainKind::paved:
            paint(4, horizon - 18, 48, 6, TerrainTone::shadow);
            paint(4, horizon - 38, 8, 26, TerrainTone::accent);
            paint(width - 52, horizon - 10, 48, 6, TerrainTone::shadow);
            paint(width - 12, horizon - 30, 8, 26, TerrainTone::accent);
            break;
        case ConsoleTerrainKind::grass:
            paint(6, floor, 48, 2, TerrainTone::accent);
            paint(width - 54, floor + 6, 48, 2, TerrainTone::accent);
            break;
    }
}

}  // namespace grandleon::view
