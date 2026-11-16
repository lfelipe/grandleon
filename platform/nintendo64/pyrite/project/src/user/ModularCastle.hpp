// SPDX-License-Identifier: MIT
#pragma once
#include <libdragon.h>
#include <t3d/t3d.h>
#include <cstdint>
#include <vector>

namespace grandleon::nintendo64::pyrite {
// Reuse Godot's cell pieces; each authored wall/roof needs only a placement
// matrix, rather than another complete copy of its vertices in base RDRAM.
class ModularCastle final {
public:
    void begin();
    void add(std::uint8_t model, std::uint16_t cell,
             std::int16_t x, std::int16_t y, std::int16_t z, bool roof = false);
    bool finish();
    bool active() const { return !instances_.empty(); }
    void draw(const bool* revealed, std::size_t cells) const;
private:
    struct Instance {
        std::uint8_t model;
        std::uint16_t cell;
        std::int16_t x, y, z;
        bool roof;
    };
    struct Bounds { float x,y,z,radius; };
    std::vector<Instance> instances_;
    std::vector<Bounds> bounds_;
    mutable std::vector<std::uint8_t> visible_;
    T3DVertPacked* vertices_{};
    T3DMat4FP* matrices_{};
    std::size_t matrix_capacity_{};
    sprite_t* masonry_{};
};
}
