// SPDX-License-Identifier: MIT
#include "ModularCastle.hpp"
#include "GeneratedSettlement.hpp"
#include <t3d/t3dmath.h>
#include "scene/sceneManager.h"
#include "scene/scene.h"
#include "scene/camera.h"
#include "renderer/renderScale.h"
#include <algorithm>
#include <cmath>

namespace grandleon::nintendo64::pyrite {
namespace {
const auto& first_model = settlement_art::models[settlement_art::model_castle_wall_horizontal];
constexpr auto last_model = settlement_art::models[settlement_art::model_visual_cover];
const auto first_vertex = settlement_art::batches[first_model.first].first;
constexpr auto last_batch = settlement_art::batches[last_model.first + last_model.count - 1U];
const auto vertex_count = last_batch.first + last_batch.count - first_vertex;
}
void ModularCastle::begin() {
    rspq_wait();
    instances_.clear();
}
void ModularCastle::add(std::uint8_t model, std::uint16_t cell,
                      std::int16_t x, std::int16_t y, std::int16_t z, bool roof) {
    instances_.push_back({model, cell, x, y, z, roof});
}
bool ModularCastle::finish() {
    if (instances_.empty()) return true;
    visible_.assign(instances_.size(), 1U);
    if (bounds_.empty()) {
        for (unsigned model_id = settlement_art::model_castle_wall_horizontal;
             model_id <= settlement_art::model_visual_cover; ++model_id) {
            const auto& model = settlement_art::models[model_id];
            float minimum[3] = {32767,32767,32767}, maximum[3] = {-32768,-32768,-32768};
            for (unsigned p = 0; p < model.count; ++p) {
                const auto& part = settlement_art::batches[model.first+p];
                for (unsigned v = 0; v < part.count; ++v) {
                    const auto& point = settlement_art::vertices[part.first+v];
                    const float at[3] = {float(point.x),float(point.y),float(point.z)};
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        minimum[axis] = std::min(minimum[axis],at[axis]);
                        maximum[axis] = std::max(maximum[axis],at[axis]);
                    }
                }
            }
            const float x = (maximum[0]-minimum[0])*0.5F;
            const float y = (maximum[1]-minimum[1])*0.5F;
            const float z = (maximum[2]-minimum[2])*0.5F;
            bounds_.push_back({(maximum[0]+minimum[0])*0.5F,
                               (maximum[1]+minimum[1])*0.5F,
                               (maximum[2]+minimum[2])*0.5F,
                               std::sqrt(x*x+y*y+z*z)+1.0F});
        }
    }
    if (!vertices_) {
        vertices_ = static_cast<T3DVertPacked*>(malloc_uncached(
            sizeof(T3DVertPacked) * ((vertex_count + 1U) / 2U)));
        masonry_ = sprite_load("rom:/castle_masonry.sprite");
        if (!vertices_ || !masonry_) return false;
        const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
        const auto normal = t3d_vert_pack_normal(&up);
        for (unsigned i = 0; i < vertex_count; ++i) {
            const auto& source = settlement_art::vertices[first_vertex + i];
            auto* pos = t3d_vertbuffer_get_pos(vertices_, i);
            pos[0] = source.x; pos[1] = source.y; pos[2] = source.z;
            auto* uv = t3d_vertbuffer_get_uv(vertices_, i);
            uv[0] = source.u; uv[1] = source.v;
            *t3d_vertbuffer_get_color(vertices_, i) = source.colour;
            if (i & 1U) vertices_[i / 2U].normB = normal;
            else vertices_[i / 2U].normA = normal;
        }
    }
    if (instances_.size() + 1U > matrix_capacity_) {
        rspq_wait();
        if (matrices_) free_uncached(matrices_);
        matrices_ = static_cast<T3DMat4FP*>(malloc_uncached_aligned(
            16, sizeof(T3DMat4FP) * (instances_.size() + 1U)));
        if (!matrices_) { matrix_capacity_ = 0; return false; }
        matrix_capacity_ = instances_.size() + 1U;
    }
    for (unsigned i = 0; i < instances_.size(); ++i) {
        const auto& instance = instances_[i];
        if (instance.model < settlement_art::model_castle_wall_horizontal ||
            instance.model > settlement_art::model_visual_cover) return false;
        t3d_mat4fp_identity(&matrices_[i]);
        const float at[3] = {float(instance.x), float(instance.y), float(instance.z)};
        t3d_mat4fp_set_pos(&matrices_[i], at);
    }
    t3d_mat4fp_identity(&matrices_[instances_.size()]);
    debugf("GRANDLEON_N64_MODULAR_CASTLE instances=%lu template_vertices=%u matrix_bytes=%lu\n",
           static_cast<unsigned long>(instances_.size()), unsigned(vertex_count),
           static_cast<unsigned long>((instances_.size() + 1U) * sizeof(T3DMat4FP)));
    return true;
}
void ModularCastle::draw(const bool* revealed, std::size_t cells) const {
    if (instances_.empty()) return;
    auto* camera = P64::SceneManager::getCurrent().getCamera();
    const float render_scale = P64::Renderer::getRenderScale();
    for (unsigned i = 0; i < instances_.size(); ++i) {
        const auto& instance = instances_[i];
        const auto& bound = bounds_[instance.model-settlement_art::model_castle_wall_horizontal];
        visible_[i] = 1U;
        if (camera && camera->getProjection() == P64::Camera::Projection::ORTHOGRAPHIC) {
            const fm_vec3_t world = {{(instance.x+bound.x)/render_scale,
                                     (instance.y+bound.y)/render_scale,
                                     (instance.z+bound.z)/render_scale}};
            const auto screen = camera->getScreenPos(world);
            const float radius = bound.radius/render_scale*120.0F/camera->orthoSize;
            // An enclosing sphere remains conservative through camera pans,
            // elevation and yaw. No partly visible wall or roof is removed.
            visible_[i] = screen.v[0]+radius >= 0 && screen.v[0]-radius <= 320 &&
                          screen.v[1]+radius >= 0 && screen.v[1]-radius <= 240;
        }
    }
    // Pyrite has already reserved the model slot above the view matrix.
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
            repeat.s.repeats = REPEAT_INFINITE; repeat.t.repeats = REPEAT_INFINITE;
            rdpq_sprite_upload(TILE0, masonry_, &repeat);
        }
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH |
            (textured ? T3D_FLAG_TEXTURED : 0)));
        for (unsigned i = 0; i < instances_.size(); ++i) {
            const auto& instance = instances_[i];
            if (!visible_[i]) continue;
            if (instance.roof && instance.cell < cells && revealed[instance.cell]) continue;
            const auto& model = settlement_art::models[instance.model];
            t3d_matrix_set(&matrices_[i], true);
            for (unsigned p = 0; p < model.count; ++p) {
                const auto& part = settlement_art::batches[model.first + p];
                if (part.masonry != textured) continue;
                t3d_vert_load(vertices_ + (part.first - first_vertex) / 2U, 0, part.count);
                for (unsigned j = 0; j < part.index_count; j += 3U) {
                    const auto at = part.index_first + j;
                    t3d_tri_draw(settlement_art::indices[at], settlement_art::indices[at + 1U], settlement_art::indices[at + 2U]);
                }
                t3d_tri_sync();
            }
        }
    }
    // Restore world-space drawing for units, trees and overlays in this hook.
    t3d_matrix_set(&matrices_[instances_.size()], true);
}
}
