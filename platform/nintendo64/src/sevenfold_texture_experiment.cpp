// SPDX-License-Identifier: MIT

#include "sevenfold_texture_experiment.h"

#include <libdragon.h>

#include <cstddef>
#include <cstdint>

namespace grandleon::nintendo64::sevenfold_texture_experiment {
namespace {

constexpr int atlas_side = 32;
constexpr int region_side = 16;
constexpr int atlas_bytes = atlas_side * atlas_side / 2;
constexpr int palette_entries = 16;

alignas(16) std::uint8_t character_pixels[atlas_bytes]{};
alignas(16) std::uint8_t terrain_pixels[atlas_bytes]{};
alignas(16) std::uint16_t palette[palette_entries]{};
surface_t character_surface{};
surface_t terrain_surface{};
bool ready = false;

void put(
    std::uint8_t* pixels, int x, int y, std::uint8_t colour
) noexcept {
    const int byte = (y * atlas_side + x) / 2;
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

[[nodiscard]] int interior_coordinate(int value) noexcept {
    int wrapped = value % 14;
    if (wrapped < 0) wrapped += 14;
    return wrapped + 1;
}

[[nodiscard]] UV projected_uv(
    const std::int16_t points[3][3], int corner, int region, int divisor
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
    // Tiny3D stores texture coordinates in signed 10.5 fixed-point pixels.
    return {
        static_cast<std::int16_t>(
            (origin_s + interior_coordinate(source_s / divisor)) * 32
        ),
        static_cast<std::int16_t>(
            (origin_t + interior_coordinate(source_t / divisor)) * 32
        ),
    };
}

void build_character_atlas() noexcept {
    for (int y = 0; y < atlas_side; ++y) {
        for (int x = 0; x < atlas_side; ++x) {
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

void build_terrain_atlas() noexcept {
    for (int y = 0; y < atlas_side; ++y) {
        for (int x = 0; x < atlas_side; ++x) {
            const int u = x & 15;
            const int v = y & 15;
            const int region = (x / 16) + (y / 16) * 2;
            std::uint8_t ink = 1;
            if (region == 0) {
                // Grass: irregular short blades and light flecks.
                ink = static_cast<std::uint8_t>(
                    ((u * 5 + v * 3) % 17) < 3 ? 4 : 1
                );
                if (((u + 2 * v) % 19) == 0) ink = 0;
            } else if (region == 1) {
                // Stone: offset block courses with dark mortar.
                const int offset = (v / 5) & 1 ? 4 : 0;
                ink = static_cast<std::uint8_t>(
                    v % 5 == 0 || (u + offset) % 8 == 0 ? 5 : 1
                );
            } else if (region == 2) {
                // Dirt: sparse pebbles at two values.
                const int noise = (u * 11 + v * 7 + u * v) % 23;
                ink = static_cast<std::uint8_t>(
                    noise == 0 ? 5 : noise < 4 ? 3 : 1
                );
            } else {
                // Water/swamp/snow: calm horizontal ripples.
                ink = static_cast<std::uint8_t>(
                    (v == 3 || v == 10) && ((u + v) & 3) != 0 ? 4 : 1
                );
            }
            put(terrain_pixels, x, y, ink);
        }
    }
}

void bind(const surface_t& surface) {
    if (!ready) initialize();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_tex_upload_tlut(palette, 0, palette_entries);
    rdpq_texparms_t parameters{};
    parameters.palette = 0;
    rdpq_tex_upload(TILE0, &surface, &parameters);
}

}  // namespace

void initialize() {
    if (ready) return;
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
    build_terrain_atlas();
    data_cache_hit_writeback(character_pixels, sizeof character_pixels);
    data_cache_hit_writeback(terrain_pixels, sizeof terrain_pixels);
    data_cache_hit_writeback(palette, sizeof palette);
    character_surface = surface_make_linear(
        character_pixels, FMT_CI4, atlas_side, atlas_side
    );
    terrain_surface = surface_make_linear(
        terrain_pixels, FMT_CI4, atlas_side, atlas_side
    );
    ready = true;
}

void bind_terrain() { bind(terrain_surface); }
void bind_character() { bind(character_surface); }

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
    return projected_uv(points, corner, region, 4);
}

UV terrain_uv(
    const std::int16_t points[3][3], int corner, std::uint8_t region
) noexcept {
    return projected_uv(points, corner, region & 3U, 7);
}

}  // namespace grandleon::nintendo64::sevenfold_texture_experiment
