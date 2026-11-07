// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

namespace grandleon::nintendo64::sevenfold_hybrid_experiment {

enum class Style {
    medieval,
    mythical,
};

struct Frame final {
    bool attacker_pose;
    bool defender_hurt;
    bool impact;
    int attacker_push;
    int defender_recoil;
};

struct TacticalFrame final {
    // 0 is the reviewed starting cell; 100 is the highlighted destination.
    int move_progress;
    // 0 uses idle, 1 and 2 select the two curated movement poses.
    int move_pose;
};

// Loads the viewer-only CI4 cutouts embedded by CMake. Each close or tactical
// character sprite is uploaded and drawn separately so no two cutouts need to
// coexist in TMEM.
[[nodiscard]] bool initialize();
void draw(Style style, const Frame& frame);
void draw_tactical(const TacticalFrame& frame);

[[nodiscard]] std::size_t embedded_bytes() noexcept;
[[nodiscard]] std::size_t tactical_embedded_bytes() noexcept;
[[nodiscard]] constexpr std::size_t peak_texture_bytes() noexcept {
    return 80U * 80U / 2U + 16U * sizeof(unsigned short);
}
[[nodiscard]] constexpr std::size_t tactical_peak_texture_bytes() noexcept {
    return 56U * 56U / 2U + 16U * sizeof(unsigned short);
}

}  // namespace grandleon::nintendo64::sevenfold_hybrid_experiment
