// SPDX-License-Identifier: MIT
#pragma once
#include <libdragon.h>
#include <t3d/t3d.h>
#include <cstdint>
#include <vector>

namespace grandleon::nintendo64::pyrite {
// World-space instances avoid per-frame model transformation and retain the
// engine's existing board projection. Geometry is regenerated only per map.
class SettlementScenery final {
public:
    void begin();
    void add(std::uint8_t model, std::int16_t x, std::int16_t y, std::int16_t z);
    bool finish();
    void draw() const;
private:
    struct Instance { std::uint8_t model; std::int16_t x,y,z; };
    struct DrawBatch { std::uint16_t source, first; };
    std::vector<Instance> instances_;
    std::vector<DrawBatch> batches_;
    T3DVertPacked* vertices_{};
    std::size_t capacity_{};
    sprite_t* masonry_{};
};
}
