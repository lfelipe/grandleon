// SPDX-License-Identifier: MIT
#pragma once
#include <libdragon.h>
#include <t3d/t3d.h>
#include <array>

namespace grandleon::nintendo64::pyrite {
class ScenerySprites final {
public:
    void begin() { count_ = 0; dirty_ = true; }
    bool add_house(int tile_x, int tile_y, int x, int floor, int z);
    bool add_grove(int x, int floor, int z);
    void draw(float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z);
private:
    struct House { int x,y,z; unsigned drawing; bool mirror; float side; };
    std::array<House,70> houses_{};
    std::array<sprite_t*,5> sprites_{};
    T3DVertPacked* vertices_{};
    unsigned count_{};
    bool dirty_{true};
    float last_x_{}, last_y_{}, last_z_{};
};
}
