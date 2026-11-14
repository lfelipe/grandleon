// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstdint>

namespace grandleon::view {

// Presentation time is measured in 60 Hz ticks, independent of rendering at
// 30 or 60 FPS. Never advance the encounter while this sweep owns the camera.
struct StageOverview final {
    unsigned elapsed{};
    unsigned duration{};

    void begin(int width, int height) noexcept {
        elapsed = 0;
        duration = width > 12 || height > 9 ? 240U :
            width > 8 || height > 6 ? 150U : 90U;
    }
    [[nodiscard]] bool active() const noexcept { return elapsed < duration; }
    void finish() noexcept { elapsed = duration; }
    void advance(unsigned ticks) noexcept {
        elapsed += std::min(ticks, duration - elapsed);
    }
    [[nodiscard]] std::int32_t blend() const noexcept {
        if (!active()) return 65536;
        const auto t = static_cast<std::int64_t>(elapsed) * 65536 / duration;
        const auto square = t * t / 65536;
        return static_cast<std::int32_t>(square * (196608 - 2 * t) / 65536);
    }
    [[nodiscard]] static int far_edge(int extent, int home) noexcept {
        return home * 2 < extent - 1 ? extent - 1 : 0;
    }
};

} // namespace grandleon::view
