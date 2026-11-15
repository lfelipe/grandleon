// SPDX-License-Identifier: MIT
#pragma once
#include <libdragon.h>
#include <t3d/t3d.h>
#include <cstdint>

namespace grandleon::nintendo64::pyrite {
class StoneKeep final {
public:
    bool build(std::int16_t centre_x, std::int16_t centre_z,
               std::int16_t wall_floor, std::int16_t gate_floor);
    void clear() { active_ = false; }
    bool active() const { return active_; }
    void draw(bool exterior) const;
    void animate(std::uint32_t tick, bool enabled);
private:
    T3DVertPacked* vertices_{};
    sprite_t* masonry_{};
    bool active_{};
    std::int16_t centre_x_{};
};
}
