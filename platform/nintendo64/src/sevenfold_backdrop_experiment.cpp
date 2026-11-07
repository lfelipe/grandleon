// SPDX-License-Identifier: MIT

#include "sevenfold_backdrop_experiment.h"

#include <libdragon.h>

#include <cstdlib>
#include <cstring>

#include "sevenfold_backdrop_assets.h"

namespace grandleon::nintendo64::sevenfold_backdrop_experiment {
namespace {

constexpr int tile_columns = 4;
constexpr int tile_rows = 4;
constexpr int tiles_per_variant = tile_columns * tile_rows;
constexpr int variants = 3;
constexpr int tile_count = variants * tiles_per_variant;

struct Pack final {
    std::uint8_t dominant;
    std::uint8_t secondary;
    std::uint8_t transition;
    TransitionOrientation orientation;
    int variant_count;
    int first_tile;
};

// The table, rather than the draw loop, owns terrain compatibility. More
// terrain packs add rows and generated sprites; the renderer stays unchanged.
constexpr Pack packs[] = {
    {2U, 9U, 12U, TransitionOrientation::north_south, variants, 0},
};

sprite_t* tiles[tile_count]{};
bool ready = false;

[[nodiscard]] sprite_t* load(
    const unsigned char* data, std::size_t size
) noexcept {
    void* copy = std::malloc(size);
    if (copy == nullptr) return nullptr;
    std::memcpy(copy, data, size);
    sprite_t* result = sprite_load_buf(copy, static_cast<int>(size));
    if (result == nullptr) std::free(copy);
    return result;
}

[[nodiscard]] const Pack* find_pack(const Descriptor& descriptor) noexcept {
    for (const Pack& pack : packs) {
        if (pack.dominant == descriptor.dominant_terrain &&
            pack.secondary == descriptor.secondary_terrain &&
            pack.transition == descriptor.transition_terrain &&
            pack.orientation == descriptor.orientation) {
            return &pack;
        }
    }
    return nullptr;
}

}  // namespace

bool initialize() {
    if (ready) return true;
    bool loaded = true;
    for (int index = 0; index < tile_count; ++index) {
        tiles[index] = load(
            backdrop_tile_data[index], backdrop_tile_sizes[index]
        );
        loaded = loaded && tiles[index] != nullptr;
    }
    ready = loaded;
    return ready;
}

int select_variant(const Descriptor& descriptor) noexcept {
    const Pack* pack = find_pack(descriptor);
    if (pack == nullptr || pack->variant_count <= 0) return -1;
    return static_cast<int>(
        descriptor.variant_seed % static_cast<std::uint32_t>(
            pack->variant_count
        )
    );
}

void draw(const Descriptor& descriptor) {
    if (!ready && !initialize()) return;
    const Pack* pack = find_pack(descriptor);
    const int variant = select_variant(descriptor);
    if (pack == nullptr || variant < 0) return;

    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    // Each tile already contains the source plate's high-quality downsample.
    // Point sampling at native size avoids seams between independent palettes.
    rdpq_mode_filter(FILTER_POINT);
    const int first = pack->first_tile + variant * tiles_per_variant;
    for (int row = 0; row < tile_rows; ++row) {
        for (int column = 0; column < tile_columns; ++column) {
            rdpq_sprite_upload(
                TILE0, tiles[first + row * tile_columns + column], nullptr
            );
            rdpq_texture_rectangle_scaled(
                TILE0, column * 80, row * 44,
                (column + 1) * 80, (row + 1) * 44,
                0, 0, 80, 40
            );
        }
    }
}

std::size_t embedded_bytes() noexcept {
    std::size_t result = 0;
    for (std::size_t size : backdrop_tile_sizes) result += size;
    return result;
}

}  // namespace grandleon::nintendo64::sevenfold_backdrop_experiment
