// SPDX-License-Identifier: MIT
#include "../project/src/user/BoardCamera.hpp"
#include "../project/src/user/BoardSurroundings.hpp"

#include <cassert>

namespace gp = grandleon::nintendo64::pyrite;

int main() {
    constexpr auto small_left = gp::board_camera_window(0, 0, 8, 6, 4.0F, 3.0F);
    constexpr auto small_right = gp::board_camera_window(99, 99, 8, 6, 4.0F, 3.0F);
    static_assert(small_left.focus_x == 3.5F && small_left.focus_y == 2.5F);
    static_assert(small_right.focus_x == 3.5F && small_right.focus_y == 2.5F);

    constexpr auto northwest = gp::board_camera_window(-5, -4, 14, 9, 3.5F, 2.5F);
    constexpr auto southeast = gp::board_camera_window(30, 20, 14, 9, 3.5F, 2.5F);
    static_assert(northwest.focus_x == 3.5F && northwest.focus_y == 2.5F);
    static_assert(southeast.focus_x == 9.5F && southeast.focus_y == 5.5F);
    static_assert(gp::board_camera_target_is_playable(northwest, 14, 9));
    static_assert(gp::board_camera_target_is_playable(southeast, 14, 9));

    // Decorative apron coordinates clamp to the nearest legal camera focus;
    // they never expand the camera's navigable rectangle.
    const auto apron = gp::board_camera_window(-8, 16, 14, 9, 3.5F, 2.5F);
    assert(apron.focus_x == northwest.focus_x);
    assert(apron.focus_y == southeast.focus_y);
    assert(gp::board_camera_target_is_playable(apron, 14, 9));

    static_assert(gp::expected_surrounding_cells(14, 9, 5) == 330U);
    static_assert(gp::surrounding_index(-5, -5, 14, 9, 5) == 0U);
    static_assert(gp::surrounding_index(18, -5, 14, 9, 5) == 23U);
    static_assert(gp::surrounding_index(-5, 0, 14, 9, 5) == 120U);
    static_assert(gp::surrounding_index(18, 8, 14, 9, 5) == 209U);
    static_assert(gp::surrounding_index(-5, 13, 14, 9, 5) == 306U);
    static_assert(gp::surrounding_index(18, 13, 14, 9, 5) == 329U);
}
