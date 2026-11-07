// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

namespace grandleon::nintendo64::sevenfold_cutout_renderer {

enum class Style {
    medieval,
    mythical,
};

enum class Pose {
    idle_a,
    idle_b,
    windup,
    follow_through,
    hurt,
};

struct Frame final {
    Pose left_pose;
    Pose right_pose;
    // -1 draws no effect, 0 marks an impact on the left, 1 on the right.
    int impact_target;
    int left_push;
    int right_push;
    int left_recoil;
    int right_recoil;
};

struct TacticalFrame final {
    // 0 is the reviewed starting cell; 100 is the highlighted destination.
    int move_progress;
    // 0 uses idle, 1 and 2 select the two curated movement poses.
    int move_pose;
    int selected_idle_pose;
    int ambient_idle_pose;
};

// Loads the viewer-only CI4 cutouts embedded by CMake. Each close or tactical
// character sprite is uploaded and drawn separately so no two cutouts need to
// coexist in TMEM.
[[nodiscard]] bool initialize();
void draw(Style style, const Frame& frame);
void draw_tactical(const TacticalFrame& frame);
void draw_matched_board(int left_health, int right_health, bool focus);

[[nodiscard]] std::size_t embedded_bytes() noexcept;
[[nodiscard]] std::size_t tactical_embedded_bytes() noexcept;
[[nodiscard]] constexpr std::size_t peak_texture_bytes() noexcept {
    // CI4 texture lines occupy whole 64-bit TMEM words.
    return ((84U + 15U) / 16U * 8U) * 84U +
        16U * sizeof(unsigned short);
}
[[nodiscard]] constexpr std::size_t tactical_peak_texture_bytes() noexcept {
    // CI8 uses a 256-entry RGBA16 palette.
    return 56U * 56U + 256U * sizeof(unsigned short);
}

}  // namespace grandleon::nintendo64::sevenfold_cutout_renderer
