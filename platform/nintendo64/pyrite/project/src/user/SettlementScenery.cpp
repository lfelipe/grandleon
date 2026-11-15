// SPDX-License-Identifier: MIT
#include "SettlementScenery.hpp"
#include "GeneratedSettlement.hpp"
#include <limits>
#include <array>
#include <algorithm>

namespace grandleon::nintendo64::pyrite {
void SettlementScenery::begin() {
    // Recorded draws read the world-space vertex and index arenas. Retire
    // them before either arena or the instance list is changed for a new map.
    if (commands_[0] || commands_[1]) rspq_wait();
    for (auto& block : commands_) {
        if (block) rspq_block_free(block);
        block = nullptr;
    }
    if (indices_) free_uncached(indices_);
    indices_ = nullptr;
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
            batches_.push_back({static_cast<std::uint16_t>(source_index),static_cast<std::uint16_t>(next),0});
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
    // Keep one encoded index list per used source part, sharing it between
    // repeated instances. Restart each triangle to retain its original order
    // and winding, rather than optimizing or simplifying the source mesh.
    constexpr auto absent = std::numeric_limits<std::uint16_t>::max();
    std::array<std::uint16_t, settlement_art::batches.size()> offsets{};
    offsets.fill(absent);
    std::size_t words = 0;
    for (auto& batch : batches_) {
        const auto& part = settlement_art::batches[batch.source];
        const std::size_t padded_words = (part.index_count + 3U) & ~std::size_t{3};
        // The pinned Tiny3D stores a transformed vertex in 36 DMEM bytes.
        // Index DMA shares the unoccupied end of that vertex cache.
        if (part.count > T3D_VERTEX_CACHE_SIZE || part.index_count % 3U ||
            padded_words * 2U > (T3D_VERTEX_CACHE_SIZE - part.count) * 36U)
            return false;
        if (offsets[batch.source] == absent) {
            if (words + padded_words >= absent) return false;
            offsets[batch.source] = static_cast<std::uint16_t>(words);
            words += padded_words;
        }
        batch.index_word = offsets[batch.source];
    }
    if (words) {
        indices_ = static_cast<std::int16_t*>(malloc_uncached_aligned(16, words * 2U));
        if (!indices_) return false;
        // Padding is initialized as well as the actual indices because the
        // RSP's final DMA read is aligned to eight bytes.
        for (std::size_t word = 0; word < words; ++word) indices_[word] = 0;
        for (std::size_t source = 0; source < offsets.size(); ++source) {
            if (offsets[source] == absent) continue;
            const auto& part = settlement_art::batches[source];
            auto* target = indices_ + offsets[source];
            for (unsigned i = 0; i < part.index_count; ++i) {
                const auto index = settlement_art::indices[part.index_first + i];
                if (index >= part.count) return false;
                target[i] = static_cast<std::int16_t>(index | (i && i % 3U == 0U ? 0x8000U : 0U));
            }
            if (part.index_count) t3d_indexbuffer_convert(target, part.index_count);
        }
    }
    for (unsigned pass = 0; pass < 2U; ++pass) {
        const bool textured = pass == 0U;
        const bool present = std::any_of(batches_.begin(), batches_.end(), [&](const auto& batch) {
            return settlement_art::batches[batch.source].masonry == textured;
        });
        if (!present) continue;
        rspq_block_begin();
        for (const auto& batch : batches_) {
            const auto& part = settlement_art::batches[batch.source];
            if (part.masonry != textured) continue;
            t3d_vert_load(vertices_ + batch.first / 2U, 0, part.count);
            if (part.index_count)
                t3d_tri_draw_strip_and_sync(indices_ + batch.index_word, part.index_count);
            else t3d_tri_sync();
        }
        commands_[pass] = rspq_block_end();
        if (!commands_[pass]) return false;
    }
    return true;
}
void SettlementScenery::draw() const {
    if (batches_.empty()) return;
    for (const bool textured : {true,false}) {
        // There is no material consumer when the textured command block is
        // absent. The final untextured pass still establishes the same state.
        if (textured && !commands_[0]) continue;
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
        if (commands_[textured ? 0U : 1U]) rspq_block_run(commands_[textured ? 0U : 1U]);
    }
}
}
