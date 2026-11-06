// SPDX-License-Identifier: MIT
#pragma once

#include <libdragon.h>
#include <t3d/t3d.h>

#include <grandleon/client/attack_gesture.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/view/board_view.hpp>

#include <cstdint>
#include <vector>

namespace grandleon::nintendo64 {

// Tiny3D presentation for the ordinary campaign client. It owns no rules,
// input or campaign state; draw() consumes the same snapshot and presentation
// joins as the sprite renderer beside it.
class Campaign3DRenderer final {
public:
    struct MotionPose final {
        simulation::UnitId unit{0};
        int column_256{0};
        int row_256{0};
        int height{0};
        // -1 faces left and +1 right, each held at a camera-readable 45°.
        int facing{0};
        int lean{0};
    };

    Campaign3DRenderer();

    [[nodiscard]] bool ready() const noexcept { return ready_; }

    void draw(
        surface_t* display,
        const simulation::EncounterSnapshot& snapshot,
        const std::vector<std::uint64_t>& terrain,
        const package_runtime::Presentation& shown,
        const view::Camera& camera,
        std::uint8_t theme,
        simulation::UnitId skip_unit,
        simulation::UnitId skip_other,
        const MotionPose* motion = nullptr
    );

    void draw_attack(
        surface_t* display,
        const simulation::EncounterSnapshot& snapshot,
        const std::vector<std::uint64_t>& terrain,
        const package_runtime::Presentation& shown,
        std::uint8_t theme,
        const simulation::UnitSnapshot& attacker,
        const simulation::UnitSnapshot& defender,
        view::AttackGesture gesture,
        int frame,
        int frames,
        bool resolving,
        bool landed
    );

private:
    struct Figure final {
        T3DVertPacked* vertices{nullptr};
        int triangles{0};
    };

    static constexpr int maximum_figures = 8;

    T3DVertPacked* board_{nullptr};
    int board_triangles_{0};
    Figure figures_[maximum_figures]{};
    T3DVertPacked* attack_scene_{nullptr};
    int attack_scene_triangles_{0};
    Figure attack_figures_[2]{};
    T3DMat4FP* matrix_{nullptr};
    T3DViewport viewport_{};
    T3DViewport attack_viewport_{};
    bool ready_{false};
};

}  // namespace grandleon::nintendo64
