#include "BoardShadows.hpp"

#include <cassert>

namespace grandleon::nintendo64::pyrite {
namespace {

constexpr std::size_t kVerticesPerShadow = 4U;
constexpr std::int16_t kUvEnd = 32 << 5;

void set_vertex(
    T3DVertPacked* buffer,
    std::size_t index,
    std::int16_t x,
    std::int16_t y,
    std::int16_t z,
    std::int16_t u,
    std::int16_t v
) {
    auto* position = t3d_vertbuffer_get_pos(buffer, static_cast<int>(index));
    position[0] = x;
    position[1] = y;
    position[2] = z;
    auto* uv = t3d_vertbuffer_get_uv(buffer, static_cast<int>(index));
    uv[0] = u;
    uv[1] = v;
    *t3d_vertbuffer_get_color(buffer, static_cast<int>(index)) = 0xFFFFFFFFU;
}

}  // namespace

bool BoardShadows::open(const char* sprite_path) {
    vertices_ = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * capacity * kVerticesPerShadow / 2U
    ));
    texture_ = sprite_load(sprite_path);
    return vertices_ != nullptr && texture_ != nullptr;
}

void BoardShadows::begin_frame() {
    count_ = 0;
}

void BoardShadows::add(
    std::int16_t x,
    std::int16_t y,
    std::int16_t z,
    std::int16_t radius
) {
    if (count_ >= capacity) return;
    const std::size_t first = count_ * kVerticesPerShadow;
    set_vertex(vertices_, first + 0U, x - radius, y, z - radius, 0, kUvEnd);
    set_vertex(vertices_, first + 1U, x + radius, y, z - radius, kUvEnd, kUvEnd);
    set_vertex(vertices_, first + 2U, x + radius, y, z + radius, kUvEnd, 0);
    set_vertex(vertices_, first + 3U, x - radius, y, z + radius, 0, 0);
    ++count_;
}

void BoardShadows::draw() const {
    if (count_ == 0U) return;
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, false);
        rdpq_mode_tlut(TLUT_NONE);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER1((0, 0, 0, PRIM), (TEX0, 0, PRIM, 0)));
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_alphacompare(24);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    rdpq_set_prim_color({0, 0, 0, 0x42});
    rdpq_sprite_upload(TILE0, texture_, nullptr);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_DEPTH | T3D_FLAG_TEXTURED
    ));
    t3d_state_set_depth_offset(-36);
    t3d_vert_load(vertices_, 0, static_cast<std::uint32_t>(count_ * 4U));
    t3d_quad_draw_unindexed(0, static_cast<std::uint32_t>(count_));
    t3d_state_set_depth_offset(0);
}

}  // namespace grandleon::nintendo64::pyrite
