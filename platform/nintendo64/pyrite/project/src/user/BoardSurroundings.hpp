// SPDX-License-Identifier: MIT
#pragma once

#include <cassert>
#include <cstddef>

namespace grandleon::nintendo64::pyrite {

[[nodiscard]] constexpr std::size_t expected_surrounding_cells(
    int width,
    int height,
    int depth
) noexcept {
    if (depth <= 0) return 0U;
    return static_cast<std::size_t>(
        (width + 2 * depth) * (height + 2 * depth) - width * height
    );
}

// Portable packages serialize the top strip, then the left/right side strips
// row by row, then the bottom strip. The playable interior is omitted.
[[nodiscard]] constexpr std::size_t surrounding_index(
    int x,
    int y,
    int width,
    int height,
    int depth
) noexcept {
    assert(depth > 0);
    assert(x >= -depth && x < width + depth);
    assert(y >= -depth && y < height + depth);
    assert(x < 0 || x >= width || y < 0 || y >= height);
    const int outer_width = width + 2 * depth;
    const int column = x + depth;
    const int row = y + depth;
    if (row < depth) {
        return static_cast<std::size_t>(row * outer_width + column);
    }
    const std::size_t top = static_cast<std::size_t>(depth * outer_width);
    if (row < depth + height) {
        const int side_column = column < depth
            ? column : depth + column - (depth + width);
        return top + static_cast<std::size_t>(
            (row - depth) * (2 * depth) + side_column
        );
    }
    const std::size_t sides = static_cast<std::size_t>(height * 2 * depth);
    return top + sides + static_cast<std::size_t>(
        (row - depth - height) * outer_width + column
    );
}

}  // namespace grandleon::nintendo64::pyrite
