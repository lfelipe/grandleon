// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>

namespace grandleon::nintendo64::pyrite {

// Presentation-only camera policy. The focus is always clamped to authored
// cells; decorative surroundings can be visible but can never become a pan
// target or change simulation bounds.
struct BoardCameraWindow final {
    float focus_x{};
    float focus_y{};
};

[[nodiscard]] constexpr float board_camera_axis(
    int requested,
    int cell_count,
    float half_visible_cells
) noexcept {
    if (cell_count <= 0) return 0.0F;
    const float centre = static_cast<float>(cell_count - 1) * 0.5F;
    const float max_coordinate = static_cast<float>(cell_count - 1);
    if (max_coordinate <= half_visible_cells * 2.0F) return centre;
    return std::clamp(
        static_cast<float>(std::clamp(requested, 0, cell_count - 1)),
        half_visible_cells,
        max_coordinate - half_visible_cells
    );
}

[[nodiscard]] constexpr BoardCameraWindow board_camera_window(
    int requested_x,
    int requested_y,
    int width,
    int height,
    float half_visible_x,
    float half_visible_y
) noexcept {
    return {
        board_camera_axis(requested_x, width, half_visible_x),
        board_camera_axis(requested_y, height, half_visible_y),
    };
}

[[nodiscard]] constexpr bool board_camera_target_is_playable(
    const BoardCameraWindow& window,
    int width,
    int height
) noexcept {
    return width > 0 && height > 0 &&
        window.focus_x >= 0.0F &&
        window.focus_x <= static_cast<float>(width - 1) &&
        window.focus_y >= 0.0F &&
        window.focus_y <= static_cast<float>(height - 1);
}

}  // namespace grandleon::nintendo64::pyrite
