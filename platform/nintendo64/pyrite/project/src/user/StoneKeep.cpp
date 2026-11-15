// SPDX-License-Identifier: MIT
#include "StoneKeep.hpp"
#include "GeneratedStoneKeep.hpp"
#include "GeneratedKeepMotion.hpp"
#include <grandleon/view/ambient_motion.hpp>

#include <algorithm>

namespace grandleon::nintendo64::pyrite {

bool StoneKeep::build(std::int16_t centre_x, std::int16_t centre_z,
                     std::int16_t wall_floor, std::int16_t gate_floor) {
    active_ = false;
    if (castle_art::vertices.empty()) return false;
    if (!vertices_) {
        vertices_ = static_cast<T3DVertPacked*>(malloc_uncached(
            sizeof(T3DVertPacked) * ((castle_art::vertices.size() + 1U) / 2U)));
        masonry_ = sprite_load("rom:/castle_masonry.sprite");
    }
    if (!vertices_ || !masonry_) return false;
    const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
    const auto normal = t3d_vert_pack_normal(&up);
    for (const auto& part : castle_art::parts) {
        std::int16_t bottom = 32767;
        for (std::size_t i = 0; i < part.count; ++i) {
            bottom = std::min(bottom, castle_art::vertices[part.first + i].y);
        }
        const std::int16_t floor = bottom <= 6 ? gate_floor : wall_floor;
        for (std::size_t i = 0; i < part.count; ++i) {
            const auto index = part.first + i;
            const auto& source = castle_art::vertices[index];
            auto* position = t3d_vertbuffer_get_pos(vertices_, static_cast<int>(index));
            position[0] = centre_x + source.x;
            position[1] = part.grounded && source.y == bottom
                ? std::min<std::int16_t>(source.y, floor - 4) : source.y;
            position[2] = centre_z + source.z;
            auto* uv = t3d_vertbuffer_get_uv(vertices_, static_cast<int>(index));
            uv[0] = source.u;
            uv[1] = source.v;
            *t3d_vertbuffer_get_color(vertices_, static_cast<int>(index)) = source.colour;
            auto& packed = vertices_[index / 2U];
            if ((index & 1U) == 0U) packed.normA = normal;
            else packed.normB = normal;
        }
    }
    centre_x_ = centre_x;
    active_ = true;
    return true;
}

void StoneKeep::animate(std::uint32_t tick, bool enabled) {
    if (!active_) return;
    // The generated rig names only the four free-edge cloth vertices. Its
    // attachment edge, pole, lanterns and stonework retain their exact poses.
    // Called after SwapChain retires the previous pass, before vertex loads.
    for (const auto& cloth : castle_motion::entrance_pennant) {
        const auto wave = enabled ? grandleon::view::ambient_wave_q12(tick, cloth.phase) : 0;
        const int scaled = wave * cloth.amplitude;
        const int offset = (scaled + (scaled < 0 ? -2048 : 2048)) / 4096;
        auto* position = t3d_vertbuffer_get_pos(vertices_, cloth.index);
        position[0] = centre_x_ + castle_art::vertices[cloth.index].x + offset;
    }
}

void StoneKeep::draw(bool exterior) const {
    if (!active_) return;
    for (const bool textured : {true, false}) {
        rdpq_mode_begin();
            rdpq_mode_zbuf(true, true);
            rdpq_mode_tlut(TLUT_NONE);
            rdpq_mode_combiner(textured ? RDPQ_COMBINER_TEX_SHADE : RDPQ_COMBINER_SHADE);
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_mode_blender(0);
            rdpq_mode_alphacompare(0);
            rdpq_mode_fog(0);
        rdpq_mode_end();
        if (textured) {
            rdpq_texparms_t repeat{};
            repeat.s.repeats = REPEAT_INFINITE;
            repeat.t.repeats = REPEAT_INFINITE;
            rdpq_sprite_upload(TILE0, masonry_, &repeat);
        }
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH |
            (textured ? T3D_FLAG_TEXTURED : 0)));
        for (const auto& part : castle_art::parts) {
            if (part.masonry != textured || (part.upper && !exterior)) continue;
            t3d_vert_load(vertices_ + part.first / 2U, 0, part.count);
            for (std::size_t i = 0; i < part.index_count; i += 3U) {
                const auto first = part.index_first + i;
                t3d_tri_draw(castle_art::indices[first], castle_art::indices[first + 1U],
                             castle_art::indices[first + 2U]);
            }
            t3d_tri_sync();
        }
    }
}
}
