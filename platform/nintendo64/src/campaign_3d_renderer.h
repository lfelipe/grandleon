// SPDX-License-Identifier: MIT
#pragma once

#include <libdragon.h>
#include <t3d/t3d.h>

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
        simulation::UnitId skip_other
    );

private:
    struct Figure final {
        T3DVertPacked* vertices{nullptr};
        int triangles{0};
    };

    static constexpr int maximum_figures = 8;

    T3DVertPacked* board_{nullptr};
    Figure figures_[maximum_figures]{};
    T3DMat4FP* matrix_{nullptr};
    T3DViewport viewport_{};
    bool ready_{false};
};

}  // namespace grandleon::nintendo64
