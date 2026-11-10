#pragma once

#include <libdragon.h>
#include <t3d/t3d.h>

#include <cstddef>
#include <cstdint>

namespace grandleon::nintendo64::pyrite {

class BoardShadows final {
public:
    static constexpr std::size_t capacity = 8U;

    bool open(const char* sprite_path);
    void begin_frame();
    void add(std::int16_t x, std::int16_t y, std::int16_t z, std::int16_t radius);
    void draw() const;

private:
    T3DVertPacked* vertices_{};
    sprite_t* texture_{};
    std::size_t count_{};
};

}  // namespace grandleon::nintendo64::pyrite
