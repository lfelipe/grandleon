// SPDX-License-Identifier: MIT

#include "sevenfold_texture_experiment.h"

#include <libdragon.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "sevenfold_terrain_assets.h"

namespace grandleon::nintendo64::sevenfold_texture_experiment {
namespace {

constexpr int character_atlas_side = 32;
constexpr int character_region_side = 16;
constexpr int character_atlas_bytes =
    character_atlas_side * character_atlas_side / 2;
constexpr int terrain_atlas_side = 64;
constexpr int terrain_region_side = 32;
constexpr int palette_entries = 16;

alignas(16) std::uint8_t character_pixels[character_atlas_bytes]{};
alignas(16) std::uint16_t palette[palette_entries]{};
surface_t character_surface{};
sprite_t* terrain_atlas = nullptr;
sprite_t* prop_atlas = nullptr;
bool ready = false;

void put(
    std::uint8_t* pixels, int x, int y, std::uint8_t colour
) noexcept {
    const int byte = (y * character_atlas_side + x) / 2;
    if ((x & 1) == 0) {
        pixels[byte] = static_cast<std::uint8_t>(
            (pixels[byte] & 0x0FU) | (colour << 4)
        );
    } else {
        pixels[byte] = static_cast<std::uint8_t>(
            (pixels[byte] & 0xF0U) | colour
        );
    }
}

[[nodiscard]] constexpr int absolute(int value) noexcept {
    return value < 0 ? -value : value;
}

[[nodiscard]] int interior_coordinate(int value, int span) noexcept {
    int wrapped = value % span;
    if (wrapped < 0) wrapped += span;
    return wrapped + 1;
}

[[nodiscard]] UV projected_uv(
    const std::int16_t points[3][3], int corner, int region, int divisor,
    int region_side, int variation
) noexcept {
    const int ux = points[1][0] - points[0][0];
    const int uy = points[1][1] - points[0][1];
    const int uz = points[1][2] - points[0][2];
    const int vx = points[2][0] - points[0][0];
    const int vy = points[2][1] - points[0][1];
    const int vz = points[2][2] - points[0][2];
    const int nx = uy * vz - uz * vy;
    const int ny = uz * vx - ux * vz;
    const int nz = ux * vy - uy * vx;

    int source_s = 0;
    int source_t = 0;
    if (absolute(ny) >= absolute(nx) && absolute(ny) >= absolute(nz)) {
        source_s = points[corner][0];
        source_t = points[corner][2];
    } else if (absolute(nz) >= absolute(nx)) {
        source_s = points[corner][0];
        source_t = points[corner][1];
    } else {
        source_s = points[corner][2];
        source_t = points[corner][1];
    }

    const int origin_s = (region & 1) * region_side;
    const int origin_t = ((region >> 1) & 1) * region_side;
    const int span = region_side - 2;
    // Four stable offsets make repeated cells less obvious while retaining a
    // one-texel safety inset against neighbouring atlas regions.
    constexpr int offsets[4][2] = {{0, 0}, {7, 13}, {17, 5}, {23, 19}};
    const int offset_s = offsets[variation & 3][0];
    const int offset_t = offsets[variation & 3][1];
    // Tiny3D stores texture coordinates in signed 10.5 fixed-point pixels.
    return {
        static_cast<std::int16_t>(
            (origin_s + interior_coordinate(
                source_s / divisor + offset_s, span
            )) * 32
        ),
        static_cast<std::int16_t>(
            (origin_t + interior_coordinate(
                source_t / divisor + offset_t, span
            )) * 32
        ),
    };
}

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

void build_character_atlas() noexcept {
    for (int y = 0; y < character_atlas_side; ++y) {
        for (int x = 0; x < character_atlas_side; ++x) {
            const int u = x & 15;
            const int v = y & 15;
            const int region = (x / 16) + (y / 16) * 2;
            std::uint8_t ink = 1;
            if (region == 0) {
                // Armour: staggered plates, seams and small rivet highlights.
                ink = static_cast<std::uint8_t>(
                    (v == 1 || v == 8 || u == ((v / 8) * 7 + 2)) ? 5 : 1
                );
                if ((u == 4 || u == 12) && (v == 4 || v == 12)) ink = 0;
            } else if (region == 1) {
                // Cloth/leather: broad weave with a readable bright trim.
                ink = static_cast<std::uint8_t>(
                    (((u / 3) + (v / 3)) & 1) != 0 ? 2 : 1
                );
                if (u == 1 || u == 14) ink = 0;
            } else if (region == 2) {
                // Arcane: a nested diamond remains visible on hats and robes.
                const int dx = absolute(u - 8);
                const int dy = absolute(v - 8);
                ink = static_cast<std::uint8_t>(
                    dx + dy == 4 || dx + dy == 7 ? 5 : 1
                );
                if ((u == 8 || v == 8) && dx + dy < 3) ink = 0;
            } else {
                // Team panels: a strong double chevron, multiplied by blue or
                // red vertex colour rather than replacing it.
                ink = static_cast<std::uint8_t>(
                    absolute(u - 8) == absolute(v - 5) ||
                            absolute(u - 8) == absolute(v - 10)
                        ? 5 : 0
                );
            }
            put(character_pixels, x, y, ink);
        }
    }
}

void bind(const surface_t& surface) {
    if (!ready && !initialize()) return;
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_tex_upload_tlut(palette, 0, palette_entries);
    rdpq_texparms_t parameters{};
    parameters.palette = 0;
    rdpq_tex_upload(TILE0, &surface, &parameters);
}

void begin_props() {
    if (!ready && !initialize()) return;
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_sprite_upload(TILE0, prop_atlas, nullptr);
}

void prop(
    int cell, int left, int top, int width, int height, bool flip = false
) {
    const int source_left = (cell & 1) * 32;
    const int source_top = ((cell >> 1) & 1) * 32;
    if (!flip) {
        rdpq_texture_rectangle_scaled(
            TILE0, left, top, left + width, top + height,
            source_left, source_top, source_left + 32, source_top + 32
        );
    } else {
        rdpq_texture_rectangle_scaled(
            TILE0, left, top, left + width, top + height,
            source_left + 32, source_top, source_left, source_top + 32
        );
    }
}

}  // namespace

bool initialize() {
    if (ready) return true;
    constexpr std::uint8_t values[palette_entries] = {
        255, 235, 215, 195, 170, 145, 120, 96,
        255, 235, 215, 195, 170, 145, 120, 96,
    };
    for (int index = 0; index < palette_entries; ++index) {
        palette[index] = static_cast<std::uint16_t>(
            graphics_make_color(
                values[index], values[index], values[index], 255
            ) & 0xFFFFU
        );
    }
    build_character_atlas();
    data_cache_hit_writeback(character_pixels, sizeof character_pixels);
    data_cache_hit_writeback(palette, sizeof palette);
    character_surface = surface_make_linear(
        character_pixels, FMT_CI4,
        character_atlas_side, character_atlas_side
    );
    terrain_atlas = load(
        terrain_material_atlas, terrain_material_atlas_size
    );
    prop_atlas = load(
        terrain_prop_atlas, terrain_prop_atlas_size
    );
    ready = terrain_atlas != nullptr && prop_atlas != nullptr;
    return ready;
}

void bind_terrain() {
    if (!ready && !initialize()) return;
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_sprite_upload(TILE0, terrain_atlas, nullptr);
}
void bind_character() { bind(character_surface); }

void draw_board_props(bool compact) {
    begin_props();
    if (!ready) return;
    // Kept at the perimeter so the fixed camera gains foreground depth without
    // obscuring walkable-cell boundaries, selection marks, or any unit feet.
    if (compact) {
        prop(3, 5, 73, 29, 51);
        prop(1, 285, 83, 30, 32, true);
        prop(0, 3, 162, 27, 30);
        prop(2, 288, 162, 28, 30);
    } else {
        prop(3, 4, 68, 30, 55);
        prop(1, 283, 82, 32, 34, true);
        prop(0, 2, 161, 28, 31);
        prop(2, 288, 160, 28, 32);
    }
}

void draw_close_props(std::uint8_t terrain) {
    begin_props();
    if (!ready) return;
    if (terrain == 2U || terrain == 11U) {
        prop(3, 2, 79, 42, 77);
        prop(3, 280, 92, 36, 66, true);
        prop(1, 11, 150, 38, 42);
        prop(2, 282, 160, 34, 32);
    } else if (terrain == 3U || terrain == 8U || terrain == 12U) {
        prop(2, 4, 146, 44, 48);
        prop(2, 273, 150, 43, 44, true);
        prop(0, 48, 158, 30, 34);
    } else if (terrain == 1U || terrain == 4U || terrain == 10U) {
        prop(2, 3, 151, 39, 41);
        prop(0, 43, 158, 30, 34);
        prop(0, 278, 157, 34, 36, true);
    } else {
        prop(1, 4, 143, 44, 49);
        prop(1, 272, 144, 44, 48, true);
        prop(0, 49, 159, 28, 33);
    }
}

std::size_t terrain_peak_texture_bytes() noexcept {
    // One 64x64 four-bit image plus a sixteen-entry RGBA16 lookup table.
    return terrain_atlas_side * terrain_atlas_side / 2 + 16 * 2;
}

std::size_t terrain_embedded_bytes() noexcept {
    return terrain_material_atlas_size + terrain_prop_atlas_size;
}

UV character_uv(
    const std::int16_t points[3][3], int corner, std::uint8_t material
) noexcept {
    if (material == 5U) {
        // Skin remains clean; (2,2) is an unmarked full-bright texel.
        return {2 * 32, 2 * 32};
    }
    int region = 0;
    if (material == 0U) region = 3;
    else if (material == 1U) region = 1;
    else if (material == 3U) region = 2;
    return projected_uv(
        points, corner, region, 4, character_region_side, 0
    );
}

UV terrain_uv(
    const std::int16_t points[3][3], int corner, std::uint8_t region,
    std::uint8_t variation
) noexcept {
    return projected_uv(
        points, corner, region & 3U, 4, terrain_region_side, variation
    );
}

}  // namespace grandleon::nintendo64::sevenfold_texture_experiment
