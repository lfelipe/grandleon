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

// Loads the viewer-only CI4 cutouts embedded by CMake. Each 80x80 character
// sprite is uploaded and drawn separately so no two cutouts need to coexist in
// TMEM.
[[nodiscard]] bool initialize();
void draw(Style style, const Frame& frame);

[[nodiscard]] std::size_t embedded_bytes() noexcept;
[[nodiscard]] constexpr std::size_t peak_texture_bytes() noexcept {
    return 80U * 80U / 2U + 16U * sizeof(unsigned short);
}

}  // namespace grandleon::nintendo64::sevenfold_hybrid_experiment
