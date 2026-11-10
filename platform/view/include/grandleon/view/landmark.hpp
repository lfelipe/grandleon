// SPDX-License-Identifier: MIT
// Compact-console tactical landmarks.
//
// A landmark remains ordinary canonical terrain: its authored identity maps to
// the mountain gameplay kind, so the engine keeps it impassable. This file is
// presentation only. It recognises an exact identity supplied by a caller,
// works out which quarter of a 2x2 footprint a cell occupies, and emits a small
// set of flat rectangles. N64 and PlayStation map the same five tones to their
// own native colours, buying a recognisable house or grove without another
// texture, palette, upload or allocation.

#pragma once

#include <cstdint>

namespace grandleon::view {

enum class TacticalLandmark : std::uint8_t {
    none = 0,
    ancient_grove,
    village_house,
};

enum class LandmarkQuadrant : std::uint8_t {
    top_left = 0,
    top_right,
    bottom_left,
    bottom_right,
};

enum class LandmarkTone : std::uint8_t {
    outline = 0,
    primary,
    secondary,
    accent,
    highlight,
};

enum class CastleTile : std::uint8_t {
    none = 0,
    wall,
    door,
    floor,
    dais,
};

struct LandmarkCell final {
    TacticalLandmark landmark{TacticalLandmark::none};
    LandmarkQuadrant quadrant{LandmarkQuadrant::top_left};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return landmark != TacticalLandmark::none;
    }
};

// The caller supplies the two exact authored identities. Keeping hashing out
// of this low presentation layer preserves its engine-free contract.
[[nodiscard]] constexpr TacticalLandmark tactical_landmark(
    std::uint64_t identity,
    std::uint64_t ancient_grove_identity,
    std::uint64_t village_house_identity
) noexcept {
    if (identity == ancient_grove_identity) {
        return TacticalLandmark::ancient_grove;
    }
    if (identity == village_house_identity) {
        return TacticalLandmark::village_house;
    }
    return TacticalLandmark::none;
}

// A correctly authored footprint is exactly two cells across and two down.
// Its right cells see the same identity on their left; its bottom cells see it
// above. This remains correct when the camera cuts through the footprint,
// because callers inspect the full terrain array rather than visible cells.
[[nodiscard]] constexpr LandmarkCell landmark_cell(
    std::uint64_t identity,
    std::uint64_t left_identity,
    std::uint64_t above_identity,
    std::uint64_t ancient_grove_identity,
    std::uint64_t village_house_identity
) noexcept {
    const TacticalLandmark landmark = tactical_landmark(
        identity, ancient_grove_identity, village_house_identity
    );
    if (landmark == TacticalLandmark::none) return {};
    const bool right = left_identity == identity;
    const bool bottom = above_identity == identity;
    return {
        landmark,
        bottom
            ? (right ? LandmarkQuadrant::bottom_right
                     : LandmarkQuadrant::bottom_left)
            : (right ? LandmarkQuadrant::top_right
                     : LandmarkQuadrant::top_left),
    };
}

// Emits one cell's quarter of the composite in a 16x16 design grid. `paint`
// receives destination pixels and a semantic tone; it performs no allocation
// and may issue a native flat-rectangle command immediately.
template<typename Paint>
inline void paint_landmark_cell(
    LandmarkCell cell, int left, int top, int size, Paint paint
) {
    if (!cell || size <= 0) return;
    const auto fill = [&](int x, int y, int width, int height,
                          LandmarkTone tone) {
        int x0 = left + x * size / 16;
        int y0 = top + y * size / 16;
        int x1 = left + (x + width) * size / 16;
        int y1 = top + (y + height) * size / 16;
        if (x1 <= x0) x1 = x0 + 1;
        if (y1 <= y0) y1 = y0 + 1;
        paint(x0, y0, x1 - x0, y1 - y0, tone);
    };

    if (cell.landmark == TacticalLandmark::village_house) {
        switch (cell.quadrant) {
            case LandmarkQuadrant::top_left:
                fill(6, 2, 10, 3, LandmarkTone::outline);
                fill(3, 5, 13, 4, LandmarkTone::secondary);
                fill(0, 9, 16, 7, LandmarkTone::secondary);
                fill(6, 3, 10, 1, LandmarkTone::highlight);
                break;
            case LandmarkQuadrant::top_right:
                fill(0, 2, 10, 3, LandmarkTone::outline);
                fill(0, 5, 13, 4, LandmarkTone::secondary);
                fill(0, 9, 16, 7, LandmarkTone::secondary);
                fill(0, 3, 10, 1, LandmarkTone::highlight);
                break;
            case LandmarkQuadrant::bottom_left:
                fill(1, 0, 15, 14, LandmarkTone::outline);
                fill(3, 0, 13, 12, LandmarkTone::primary);
                fill(5, 3, 6, 6, LandmarkTone::accent);
                fill(6, 4, 4, 4, LandmarkTone::highlight);
                fill(1, 12, 15, 3, LandmarkTone::outline);
                break;
            case LandmarkQuadrant::bottom_right:
                fill(0, 0, 15, 14, LandmarkTone::outline);
                fill(0, 0, 13, 12, LandmarkTone::primary);
                fill(1, 3, 6, 10, LandmarkTone::accent);
                fill(3, 6, 1, 1, LandmarkTone::highlight);
                fill(9, 3, 4, 5, LandmarkTone::accent);
                fill(10, 4, 2, 3, LandmarkTone::highlight);
                fill(0, 12, 15, 3, LandmarkTone::outline);
                break;
        }
        return;
    }

    // One old, connected canopy above several visible trunks. The stepped
    // silhouette is deliberate: these machines can fill rectangles nearly for
    // free, while another textured sprite would consume both ROM and VRAM.
    switch (cell.quadrant) {
        case LandmarkQuadrant::top_left:
            fill(5, 1, 7, 4, LandmarkTone::highlight);
            fill(2, 4, 12, 5, LandmarkTone::primary);
            fill(0, 8, 16, 8, LandmarkTone::secondary);
            fill(8, 3, 8, 9, LandmarkTone::primary);
            break;
        case LandmarkQuadrant::top_right:
            fill(4, 1, 7, 4, LandmarkTone::highlight);
            fill(2, 4, 12, 5, LandmarkTone::primary);
            fill(0, 8, 16, 8, LandmarkTone::secondary);
            fill(0, 3, 8, 9, LandmarkTone::primary);
            break;
        case LandmarkQuadrant::bottom_left:
            fill(0, 0, 16, 5, LandmarkTone::secondary);
            fill(8, 2, 4, 12, LandmarkTone::outline);
            fill(9, 2, 2, 12, LandmarkTone::accent);
            fill(4, 11, 10, 3, LandmarkTone::outline);
            fill(1, 13, 15, 3, LandmarkTone::primary);
            break;
        case LandmarkQuadrant::bottom_right:
            fill(0, 0, 16, 5, LandmarkTone::secondary);
            fill(4, 2, 4, 12, LandmarkTone::outline);
            fill(5, 2, 2, 12, LandmarkTone::accent);
            fill(2, 11, 10, 3, LandmarkTone::outline);
            fill(0, 13, 15, 3, LandmarkTone::primary);
            break;
    }
}

// The castle is a connected 6x6 tactical room rather than a 2x2 blocked
// landmark. Each exact identity therefore supplies its own tile treatment;
// adjacency comes from the shared stone edges and the authored footprint.
[[nodiscard]] constexpr CastleTile castle_tile(
    std::uint64_t identity,
    std::uint64_t wall_identity,
    std::uint64_t door_identity,
    std::uint64_t floor_identity,
    std::uint64_t dais_identity
) noexcept {
    if (identity == wall_identity) return CastleTile::wall;
    if (identity == door_identity) return CastleTile::door;
    if (identity == floor_identity) return CastleTile::floor;
    if (identity == dais_identity) return CastleTile::dais;
    return CastleTile::none;
}

template<typename Paint>
inline void paint_castle_tile(
    CastleTile tile, int left, int top, int size, Paint paint
) {
    if (tile == CastleTile::none || size <= 0) return;
    const auto fill = [&](int x, int y, int width, int height,
                          LandmarkTone tone) {
        int x0 = left + x * size / 16;
        int y0 = top + y * size / 16;
        int x1 = left + (x + width) * size / 16;
        int y1 = top + (y + height) * size / 16;
        if (x1 <= x0) x1 = x0 + 1;
        if (y1 <= y0) y1 = y0 + 1;
        paint(x0, y0, x1 - x0, y1 - y0, tone);
    };
    switch (tile) {
        case CastleTile::wall:
            fill(0, 2, 16, 14, LandmarkTone::outline);
            fill(1, 3, 14, 13, LandmarkTone::primary);
            fill(1, 3, 14, 3, LandmarkTone::highlight);
            fill(1, 0, 3, 5, LandmarkTone::secondary);
            fill(7, 0, 3, 5, LandmarkTone::secondary);
            fill(13, 0, 3, 5, LandmarkTone::secondary);
            fill(3, 9, 10, 2, LandmarkTone::secondary);
            break;
        case CastleTile::door:
            fill(0, 0, 16, 16, LandmarkTone::primary);
            fill(1, 1, 14, 15, LandmarkTone::outline);
            fill(3, 2, 10, 14, LandmarkTone::accent);
            fill(7, 2, 2, 14, LandmarkTone::outline);
            fill(5, 8, 1, 1, LandmarkTone::highlight);
            fill(10, 8, 1, 1, LandmarkTone::highlight);
            break;
        case CastleTile::floor:
            fill(0, 0, 16, 16, LandmarkTone::primary);
            fill(0, 0, 16, 1, LandmarkTone::highlight);
            fill(0, 8, 16, 1, LandmarkTone::secondary);
            fill(7, 0, 1, 8, LandmarkTone::secondary);
            fill(3, 8, 1, 8, LandmarkTone::secondary);
            fill(11, 8, 1, 8, LandmarkTone::secondary);
            break;
        case CastleTile::dais:
            fill(0, 0, 16, 16, LandmarkTone::outline);
            fill(1, 1, 14, 15, LandmarkTone::secondary);
            fill(2, 3, 12, 12, LandmarkTone::accent);
            fill(2, 3, 12, 2, LandmarkTone::highlight);
            fill(6, 6, 4, 8, LandmarkTone::primary);
            break;
        case CastleTile::none: break;
    }
}

}  // namespace grandleon::view
