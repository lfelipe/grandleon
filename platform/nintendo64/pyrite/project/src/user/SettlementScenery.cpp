// SPDX-License-Identifier: MIT
#include "SettlementScenery.hpp"
#include "GeneratedSettlement.hpp"
#include <limits>

namespace grandleon::nintendo64::pyrite {
void SettlementScenery::begin() {
    instances_.clear();
    batches_.clear();
}
void SettlementScenery::add(std::uint8_t model, std::int16_t x, std::int16_t y, std::int16_t z) {
    instances_.push_back({model,x,y,z});
}
bool SettlementScenery::finish() {
    std::size_t required = 0;
    for (const auto& instance : instances_) {
        if (instance.model >= settlement_art::models.size()) return false;
        const auto& model = settlement_art::models[instance.model];
        for (std::size_t p=0; p<model.count; ++p) required += settlement_art::batches[model.first+p].count;
    }
    if (required > std::numeric_limits<std::uint16_t>::max()) return false;
    if (required > capacity_) {
        rspq_wait();
        if (vertices_) free_uncached(vertices_);
        vertices_ = static_cast<T3DVertPacked*>(malloc_uncached(sizeof(T3DVertPacked) * ((required+1U)/2U)));
        if (!vertices_) { capacity_ = 0; return false; }
        capacity_ = required;
    }
    if (required && !masonry_) masonry_ = sprite_load("rom:/castle_masonry.sprite");
    if (required && !masonry_) return false;
    const fm_vec3_t up = {{0.0F,1.0F,0.0F}};
    const auto normal = t3d_vert_pack_normal(&up);
    std::size_t next = 0;
    for (const auto& instance : instances_) {
        const auto& model = settlement_art::models[instance.model];
        for (std::size_t p=0; p<model.count; ++p) {
            const std::size_t source_index = model.first+p;
            const auto& part = settlement_art::batches[source_index];
            batches_.push_back({static_cast<std::uint16_t>(source_index),static_cast<std::uint16_t>(next)});
            for (std::size_t i=0; i<part.count; ++i) {
                const auto& source = settlement_art::vertices[part.first+i];
                const auto at = next+i;
                auto* pos = t3d_vertbuffer_get_pos(vertices_, static_cast<int>(at));
                pos[0]=instance.x+source.x; pos[1]=instance.y+source.y; pos[2]=instance.z+source.z;
                auto* uv = t3d_vertbuffer_get_uv(vertices_, static_cast<int>(at));
                uv[0]=source.u; uv[1]=source.v;
                *t3d_vertbuffer_get_color(vertices_, static_cast<int>(at)) = source.colour;
                auto& packed = vertices_[at/2U];
                if (at&1U) packed.normB=normal; else packed.normA=normal;
            }
            next += part.count;
        }
    }
    return true;
}
void SettlementScenery::draw() const {
    if (batches_.empty()) return;
    for (const bool textured : {true,false}) {
        rdpq_mode_begin();
            rdpq_mode_zbuf(true,true);
            rdpq_mode_tlut(TLUT_NONE);
            rdpq_mode_combiner(textured ? RDPQ_COMBINER_TEX_SHADE : RDPQ_COMBINER_SHADE);
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_mode_blender(0);
            rdpq_mode_alphacompare(0);
            rdpq_mode_fog(0);
        rdpq_mode_end();
        if (textured) {
            rdpq_texparms_t repeat{};
            repeat.s.repeats=REPEAT_INFINITE; repeat.t.repeats=REPEAT_INFINITE;
            rdpq_sprite_upload(TILE0,masonry_,&repeat);
        }
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED|T3D_FLAG_DEPTH|(textured ? T3D_FLAG_TEXTURED : 0)));
        for (const auto& batch : batches_) {
            const auto& part=settlement_art::batches[batch.source];
            if (part.masonry != textured) continue;
            t3d_vert_load(vertices_+batch.first/2U,0,part.count);
            for (std::size_t i=0; i<part.index_count; i+=3U) {
                const auto first=part.index_first+i;
                t3d_tri_draw(settlement_art::indices[first],settlement_art::indices[first+1U],settlement_art::indices[first+2U]);
            }
            t3d_tri_sync();
        }
    }
}
}
