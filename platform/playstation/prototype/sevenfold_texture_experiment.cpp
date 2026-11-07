// SPDX-License-Identifier: MIT
// PlayStation-only texture experiment for Sevenfold's 3D close battle view.
//
// This is deliberately a separate executable from the production campaign.
// It preserves the first 32x32-on-proxy experiment as a comparison, then puts
// CI4 reductions of the actual curated Godot frames on camera-facing cutouts
// over quiet textured 3D terrain. Matched close and tactical-map captures make
// the recognisability/performance trade-off visible before either approach is
// allowed to complicate the production renderer.

#include "psx_gpu.h"
#include "psx_runtime.h"

#include <grandleon/view/glyphs.hpp>

#include <algorithm>
#include <cstdint>

#include "playstation.h"
#include "playstation_characters_medieval.h"
#include "playstation_characters_mythical.h"
#include "generated/godot_closeup_ci4.h"
#include "generated/combat_backdrop_ci8.h"
#include "generated/quality_terrain_ci4.h"
#include "generated/renderer_comparison_scene.h"
#include "n64_showcase.h"
#include "sevenfold_roster_proxies.h"

namespace {

namespace gpu = grandleon::playstation::gpu;
namespace psx = grandleon::playstation;
namespace view = grandleon::view;

constexpr int max_vertices = 512;
constexpr int max_triangles = sevenfold_high_max_triangles;
constexpr std::uint32_t frame_budget_ticks =
    psx::ticks_per_1000_microseconds * 33U;

constexpr int terrain_a_cell = 0;
constexpr int terrain_b_cell = 1;
constexpr int left_cell = 2;
constexpr int right_cell = 3;
constexpr int terrain_a_clut = 1;
constexpr int terrain_b_clut = 2;
constexpr int left_clut = 3;
constexpr int right_clut = 4;
constexpr int cutout_texture_page = gpu::first_texture_page + 1;
constexpr int cutout_medieval_male_clut = 5;
constexpr int cutout_medieval_female_clut = 6;
constexpr int cutout_mythical_male_clut = 7;
constexpr int cutout_mythical_female_clut = 8;
constexpr int cutout_move_a_clut = 9;
constexpr int cutout_move_b_clut = 10;
constexpr int quality_terrain_cell_base = 4;
constexpr int quality_grass_clut = 29;
constexpr int quality_forest_clut = 30;
constexpr int quality_stone_clut = 31;
constexpr int backdrop_texture_page = gpu::first_texture_page + 6;
constexpr int backdrop_overlay_page = gpu::first_texture_page + 8;
constexpr int backdrop_clut_x = 0;
constexpr int backdrop_overlay_clut_x = 256;
constexpr int backdrop_clut_y = gpu::clut_y + 1;

struct Cutout final {
    int page;
    int u;
    int v;
    int clut;
    int source_size;
};

constexpr Cutout medieval_male_cutout{
    cutout_texture_page, 0, 0, cutout_medieval_male_clut,
    grandleon_psx_godot_closeup_width
};
constexpr Cutout medieval_female_cutout{
    cutout_texture_page, 128, 0, cutout_medieval_female_clut,
    grandleon_psx_godot_closeup_width
};
constexpr Cutout mythical_male_cutout{
    cutout_texture_page, 0, 128, cutout_mythical_male_clut,
    grandleon_psx_godot_closeup_width
};
constexpr Cutout mythical_female_cutout{
    cutout_texture_page, 128, 128, cutout_mythical_female_clut,
    grandleon_psx_godot_closeup_width
};
constexpr Cutout medieval_move_a_cutout{
    cutout_texture_page + 1, 0, 0, cutout_move_a_clut,
    grandleon_psx_godot_closeup_width
};
constexpr Cutout medieval_move_b_cutout{
    cutout_texture_page + 1, 128, 0, cutout_move_b_clut,
    grandleon_psx_godot_closeup_width
};

constexpr int quality_close_page = gpu::first_texture_page + 3;
constexpr int tactical_cutout_page = gpu::first_texture_page + 5;

struct ClosePoseSlots final {
    Cutout idle;
    Cutout windup;
    Cutout follow;
    Cutout hurt;
};

constexpr ClosePoseSlots quality_left_slots{
    {quality_close_page, 0, 0, 11, grandleon_psx_godot_quality_close_width},
    {quality_close_page, 128, 0, 12, grandleon_psx_godot_quality_close_width},
    {quality_close_page, 0, 128, 13, grandleon_psx_godot_quality_close_width},
    {quality_close_page, 128, 128, 14, grandleon_psx_godot_quality_close_width},
};
constexpr ClosePoseSlots quality_right_slots{
    {quality_close_page + 1, 0, 0, 15, grandleon_psx_godot_quality_close_width},
    {quality_close_page + 1, 128, 0, 16, grandleon_psx_godot_quality_close_width},
    {quality_close_page + 1, 0, 128, 17, grandleon_psx_godot_quality_close_width},
    {quality_close_page + 1, 128, 128, 18, grandleon_psx_godot_quality_close_width},
};

constexpr Cutout tactical_cutouts[] = {
    {tactical_cutout_page, 0, 0, 19, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 64, 0, 20, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 128, 0, 21, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 192, 0, 22, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 0, 64, 23, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 64, 64, 24, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 128, 64, 25, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 192, 64, 26, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 0, 128, 27, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 64, 128, 28, grandleon_psx_godot_tactical_width},
};

struct Camera final {
    int distance;
    int focal;
    int horizon;
};

// Camera v2 deliberately uses one orthographic world-to-screen contract for
// terrain, feet, shadows, cutouts and effects.  The basis can be frontal for
// the close view or isometric for the board, while the fit remains derived
// from world bounds.  This avoids the old mixture of perspective terrain and
// hand-placed screen sprites that could read as a camera below the actors.
struct ElevatedCamera final {
    int centre_x;
    int centre_y;
    int xx;
    int xz;
    int yx;
    int yz;
    int up;
    int zoom;
};

struct RawBounds final {
    std::int32_t left;
    std::int32_t right;
    std::int32_t top;
    std::int32_t bottom;
    bool valid;
};

struct ProjectedVertex final {
    std::int16_t x;
    std::int16_t y;
    std::int32_t depth;
};

struct OrderedTriangle final {
    std::uint16_t index;
    std::int32_t depth;
};

struct TextureBounds final {
    int left;
    int right;
    int bottom;
    int top;
};

struct SceneResult final {
    std::uint32_t ticks;
    int textured_triangles;
};

ProjectedVertex projected[max_vertices];
OrderedTriangle ordered[max_triangles];
std::uint16_t font_sheet[gpu::font_lines][gpu::font_halfwords_across];
std::uint16_t opaque_cell[gpu::cell_texels * gpu::cell_halfwords];
std::uint16_t refined_terrain_a[gpu::cell_texels * gpu::cell_halfwords];
std::uint16_t refined_terrain_b[gpu::cell_texels * gpu::cell_halfwords];
std::uint16_t refined_terrain_c[gpu::cell_texels * gpu::cell_halfwords];
std::uint16_t refined_terrain_a_palette[gpu::clut_entries];
std::uint16_t refined_terrain_b_palette[gpu::clut_entries];
std::uint16_t refined_terrain_c_palette[gpu::clut_entries];
int checks = 0;
int failures = 0;

constexpr std::uint8_t team_colours[2][3] = {
    {65, 132, 235}, {215, 74, 68},
};

[[nodiscard]] constexpr int clamp_channel(int value) noexcept {
    return value < 0 ? 0 : (value > 31 ? 31 : value);
}

[[nodiscard]] constexpr std::uint16_t colour15(
    const std::uint8_t* source, int light = 255
) noexcept {
    return gpu::rgb15(
        clamp_channel((static_cast<int>(source[0]) * light / 255) >> 3),
        clamp_channel((static_cast<int>(source[1]) * light / 255) >> 3),
        clamp_channel((static_cast<int>(source[2]) * light / 255) >> 3)
    );
}

void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) ++failures;
    psx::Line line;
    line.text(condition ? "ok  " : "FAIL ").text(description).flush();
}

void build_font() {
    for (auto& line : font_sheet) {
        for (std::uint16_t& word : line) word = 0;
    }
    constexpr int texels_per_halfword = 4;
    for (int slot = 0; slot < view::glyph_count && slot < gpu::font_capacity;
         ++slot) {
        const int base_line = gpu::glyph_texture_v(slot);
        const int base_word = gpu::glyph_texture_u(slot) / texels_per_halfword;
        for (int row = 0; row < gpu::glyph_texels; ++row) {
            for (int x = 0; x < gpu::glyph_texels; ++x) {
                if (!view::glyph_pixel(view::first_glyph + slot, x, row)) {
                    continue;
                }
                const int word = base_word + x / texels_per_halfword;
                const int shift = 4 * (x % texels_per_halfword);
                font_sheet[base_line + row][word] =
                    static_cast<std::uint16_t>(
                        font_sheet[base_line + row][word] |
                        static_cast<std::uint16_t>(1u << shift)
                    );
            }
        }
    }
    gpu::upload(
        gpu::font_x, gpu::font_y, gpu::font_halfwords_across, gpu::font_lines,
        &font_sheet[0][0]
    );
    unsigned short clut[gpu::clut_entries]{};
    clut[1] = gpu::rgb15(31, 31, 31);
    gpu::upload(gpu::clut_x_of(0), gpu::clut_y_of(0), gpu::clut_entries, 1, clut);
}

void text_at(int x, int y, const char* value) {
    if (value == nullptr) return;
    for (int at = 0; value[at] != '\0'; ++at) {
        const int slot = static_cast<unsigned char>(value[at]) - view::first_glyph;
        gpu::draw_glyph(x + at * gpu::glyph_texels, y, slot, 0);
    }
}

[[nodiscard]] ProjectedVertex project(
    int x, int y, int z, const Camera& camera
) noexcept {
    const std::int32_t vertical = y * 218 + z * 128;
    std::int32_t depth = camera.distance * 256 - z * 218 + y * 128;
    if (depth < 64 * 256) depth = 64 * 256;
    return {
        static_cast<std::int16_t>(160 + x * camera.focal * 256 / depth),
        static_cast<std::int16_t>(camera.horizon - vertical * camera.focal / depth),
        depth,
    };
}

[[nodiscard]] std::int32_t elevated_raw_x(
    int x, int z, const ElevatedCamera& camera
) noexcept {
    return x * camera.xx + z * camera.xz;
}

[[nodiscard]] std::int32_t elevated_raw_y(
    int x, int y, int z, const ElevatedCamera& camera
) noexcept {
    return x * camera.yx + z * camera.yz - y * camera.up;
}

[[nodiscard]] ProjectedVertex project_elevated(
    int x, int y, int z, const ElevatedCamera& camera
) noexcept {
    constexpr std::int32_t scale = 256 * 256;
    const std::int32_t raw_x = elevated_raw_x(x, z, camera);
    const std::int32_t raw_y = elevated_raw_y(x, y, z, camera);
    return {
        static_cast<std::int16_t>(
            camera.centre_x + raw_x * camera.zoom / scale
        ),
        static_cast<std::int16_t>(
            camera.centre_y + raw_y * camera.zoom / scale
        ),
        raw_y,
    };
}

void include_raw(RawBounds& bounds, std::int32_t x, std::int32_t y) noexcept {
    if (!bounds.valid) {
        bounds = {x, x, y, y, true};
        return;
    }
    if (x < bounds.left) bounds.left = x;
    if (x > bounds.right) bounds.right = x;
    if (y < bounds.top) bounds.top = y;
    if (y > bounds.bottom) bounds.bottom = y;
}

void include_world(
    RawBounds& bounds, int x, int y, int z, const ElevatedCamera& camera
) noexcept {
    include_raw(
        bounds, elevated_raw_x(x, z, camera),
        elevated_raw_y(x, y, z, camera)
    );
}

void include_billboard(
    RawBounds& bounds, int x, int z, int world_size,
    const ElevatedCamera& camera
) noexcept {
    const std::int32_t raw_x = elevated_raw_x(x, z, camera);
    const std::int32_t raw_y = elevated_raw_y(x, 0, z, camera);
    const std::int32_t half_width = world_size * 128;
    include_raw(bounds, raw_x - half_width, raw_y - world_size * 256);
    include_raw(bounds, raw_x + half_width, raw_y);
}

[[nodiscard]] ElevatedCamera fit_elevated_camera(
    ElevatedCamera camera, const RawBounds& bounds,
    int left, int top, int right, int bottom
) noexcept {
    constexpr std::int32_t scale = 256 * 256;
    const std::int32_t width = bounds.right - bounds.left;
    const std::int32_t height = bounds.bottom - bounds.top;
    const int zoom_x = width > 0
        ? static_cast<int>((right - left) * scale / width) : 256;
    const int zoom_y = height > 0
        ? static_cast<int>((bottom - top) * scale / height) : 256;
    camera.zoom = std::min(zoom_x, zoom_y);
    const std::int32_t raw_centre_x = (bounds.left + bounds.right) / 2;
    const std::int32_t raw_centre_y = (bounds.top + bounds.bottom) / 2;
    camera.centre_x = (left + right) / 2 -
        static_cast<int>(raw_centre_x * camera.zoom / scale);
    camera.centre_y = (top + bottom) / 2 -
        static_cast<int>(raw_centre_y * camera.zoom / scale);
    return camera;
}

[[nodiscard]] int elevated_billboard_size(
    int world_size, const ElevatedCamera& camera
) noexcept {
    return world_size * camera.zoom / 256;
}

void rotate_vertex(
    const SevenfoldProxyVertex& source, int quarter_turn, int& x, int& z
) noexcept {
    x = source.x;
    z = source.z;
    if (quarter_turn < 0) {
        x = source.z;
        z = -source.x;
    } else if (quarter_turn > 0) {
        x = -source.z;
        z = source.x;
    }
}

[[nodiscard]] int order_triangles(const SevenfoldProxy& proxy) {
    int count = 0;
    for (int index = 0; index < proxy.triangles_count; ++index) {
        const SevenfoldProxyTriangle& triangle = proxy.triangles[index];
        const ProjectedVertex& a = projected[triangle.a];
        const ProjectedVertex& b = projected[triangle.b];
        const ProjectedVertex& c = projected[triangle.c];
        const std::int32_t area =
            static_cast<std::int32_t>(b.x - a.x) * (c.y - a.y) -
            static_cast<std::int32_t>(b.y - a.y) * (c.x - a.x);
        if (area >= 0) continue;
        ordered[count++] = {
            static_cast<std::uint16_t>(index),
            a.depth + b.depth + c.depth,
        };
    }
    std::sort(ordered, ordered + count,
              [](const OrderedTriangle& left, const OrderedTriangle& right) {
                  return left.depth > right.depth;
              });
    return count;
}

[[nodiscard]] TextureBounds texture_bounds(
    const SevenfoldProxy& proxy, int quarter_turn
) noexcept {
    int first_x = 0;
    int first_z = 0;
    rotate_vertex(proxy.vertices[0], quarter_turn, first_x, first_z);
    TextureBounds bounds{first_x, first_x, proxy.vertices[0].y,
                         proxy.vertices[0].y};
    for (int index = 1; index < proxy.vertices_count; ++index) {
        int x = 0;
        int z = 0;
        rotate_vertex(proxy.vertices[index], quarter_turn, x, z);
        static_cast<void>(z);
        if (x < bounds.left) bounds.left = x;
        if (x > bounds.right) bounds.right = x;
        if (proxy.vertices[index].y < bounds.bottom) {
            bounds.bottom = proxy.vertices[index].y;
        }
        if (proxy.vertices[index].y > bounds.top) {
            bounds.top = proxy.vertices[index].y;
        }
    }
    return bounds;
}

[[nodiscard]] int texture_u(int x, const TextureBounds& bounds) noexcept {
    const int span = bounds.right - bounds.left;
    return span > 0 ? 2 + (x - bounds.left) * 27 / span : 16;
}

[[nodiscard]] int texture_v(int y, const TextureBounds& bounds) noexcept {
    const int span = bounds.top - bounds.bottom;
    return span > 0 ? 2 + (bounds.top - y) * 27 / span : 16;
}

[[nodiscard]] int draw_model(
    const SevenfoldProxy& proxy, int origin_x, int origin_z, int team,
    int quarter_turn, const Camera& camera, bool textured, int cell, int clut
) {
    if (proxy.vertices_count > max_vertices ||
        proxy.triangles_count > max_triangles) {
        return 0;
    }
    for (int index = 0; index < proxy.vertices_count; ++index) {
        int x = 0;
        int z = 0;
        rotate_vertex(proxy.vertices[index], quarter_turn, x, z);
        projected[index] = project(
            x + origin_x, proxy.vertices[index].y, z + origin_z, camera
        );
    }
    const TextureBounds bounds = texture_bounds(proxy, quarter_turn);
    const int ordered_count = order_triangles(proxy);
    int textured_count = 0;
    for (int order = 0; order < ordered_count; ++order) {
        const SevenfoldProxyTriangle& triangle =
            proxy.triangles[ordered[order].index];
        const ProjectedVertex& a = projected[triangle.a];
        const ProjectedVertex& b = projected[triangle.b];
        const ProjectedVertex& c = projected[triangle.c];

        // The current experiment projects the whole sprite over cloth, skin
        // and large body masses. The hybrid comparison keeps this unchanged so
        // its capture remains an honest record of the approach being replaced.
        const bool use_texture = textured && triangle.material <= 2;
        if (use_texture) {
            int ax = 0;
            int az = 0;
            int bx = 0;
            int bz = 0;
            int cx = 0;
            int cz = 0;
            rotate_vertex(proxy.vertices[triangle.a], quarter_turn, ax, az);
            rotate_vertex(proxy.vertices[triangle.b], quarter_turn, bx, bz);
            rotate_vertex(proxy.vertices[triangle.c], quarter_turn, cx, cz);
            static_cast<void>(az);
            static_cast<void>(bz);
            static_cast<void>(cz);
            gpu::draw_cell_triangle(
                a.x, a.y, texture_u(ax, bounds),
                texture_v(proxy.vertices[triangle.a].y, bounds),
                b.x, b.y, texture_u(bx, bounds),
                texture_v(proxy.vertices[triangle.b].y, bounds),
                c.x, c.y, texture_u(cx, bounds),
                texture_v(proxy.vertices[triangle.c].y, bounds), cell, clut
            );
            ++textured_count;
        } else {
            const std::uint8_t* material = triangle.material == 0
                ? team_colours[team]
                : sevenfold_style_materials[proxy.style][triangle.material];
            const int light = 128 + triangle.shade / 2;
            gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y,
                               colour15(material, light));
        }
        if ((order & 63) == 63) static_cast<void>(psx::clock_ticks());
    }
    return textured_count;
}

void upload_cell(int cell, const unsigned short* source, bool opaque) {
    const unsigned short* upload_source = source;
    if (opaque) {
        for (int index = 0; index < gpu::cell_texels * gpu::cell_halfwords;
             ++index) {
            std::uint16_t result = source[index];
            for (int nibble = 0; nibble < 4; ++nibble) {
                const int shift = nibble * 4;
                if (((result >> shift) & 0x0fU) == 0) {
                    result = static_cast<std::uint16_t>(
                        result | static_cast<std::uint16_t>(1U << shift)
                    );
                }
            }
            opaque_cell[index] = result;
        }
        upload_source = opaque_cell;
    }
    gpu::upload(
        gpu::cell_texture_x(cell), gpu::cell_texture_y(cell),
        gpu::cell_halfwords, gpu::cell_texels, upload_source
    );
}

void upload_clut(int clut, const unsigned short* source) {
    gpu::upload(
        gpu::clut_x_of(clut), gpu::clut_y_of(clut), gpu::clut_entries, 1,
        source
    );
}

void set_cell_texel(std::uint16_t* cell, int x, int y, int index) {
    const int word = y * gpu::cell_halfwords + x / 4;
    const int shift = (x & 3) * 4;
    const std::uint16_t mask = static_cast<std::uint16_t>(0x0fU << shift);
    cell[word] = static_cast<std::uint16_t>(
        (cell[word] & ~mask) | static_cast<std::uint16_t>(index << shift)
    );
}

void make_low_frequency_cell(std::uint16_t* cell, int pattern) {
    for (int index = 0; index < gpu::cell_texels * gpu::cell_halfwords;
         ++index) {
        cell[index] = 0;
    }
    for (int y = 0; y < gpu::cell_texels; ++y) {
        for (int x = 0; x < gpu::cell_texels; ++x) {
            int value = 2;
            if (pattern == 0) {
                // Sixteen-pixel grass/earth masses, not per-pixel speckle.
                value = 1 + ((x / 16 + y / 16) % 3);
            } else if (pattern == 1) {
                // Broad, offset road slabs.
                value = 1 + ((x / 16 + (y / 8 + 1) / 2) % 3);
            } else {
                // Large masonry blocks for forest/rock/ruin zones.
                value = 1 + ((x / 8 + y / 16) % 3);
            }
            set_cell_texel(cell, x, y, value);
        }
    }
}

void make_low_contrast_palette(
    std::uint16_t* palette, int red, int green, int blue
) {
    for (int index = 0; index < gpu::clut_entries; ++index) palette[index] = 0;
    palette[1] = gpu::rgb15(red - 1, green - 1, blue - 1);
    palette[2] = gpu::rgb15(red, green, blue);
    palette[3] = gpu::rgb15(red + 1, green + 1, blue + 1);
}

void load_refined_terrain(bool mythical) {
    make_low_frequency_cell(refined_terrain_a, 0);
    make_low_frequency_cell(refined_terrain_b, 1);
    make_low_frequency_cell(refined_terrain_c, 2);
    if (mythical) {
        make_low_contrast_palette(refined_terrain_a_palette, 7, 10, 8);
        make_low_contrast_palette(refined_terrain_b_palette, 10, 8, 11);
        make_low_contrast_palette(refined_terrain_c_palette, 8, 8, 10);
    } else {
        make_low_contrast_palette(refined_terrain_a_palette, 7, 11, 7);
        make_low_contrast_palette(refined_terrain_b_palette, 11, 9, 6);
        make_low_contrast_palette(refined_terrain_c_palette, 8, 8, 9);
    }
    upload_cell(terrain_a_cell, refined_terrain_a, false);
    upload_cell(terrain_b_cell, refined_terrain_b, false);
    upload_cell(2, refined_terrain_c, false);
    upload_clut(terrain_a_clut, refined_terrain_a_palette);
    upload_clut(terrain_b_clut, refined_terrain_b_palette);
    upload_clut(3, refined_terrain_c_palette);
}

void load_quality_terrain() {
    static_assert(grandleon_psx_quality_terrain_side == gpu::cell_texels);
    static_assert(grandleon_psx_quality_terrain_cell_count == 44);
    for (int index = 0; index < grandleon_psx_quality_terrain_cell_count;
         ++index) {
        upload_cell(
            quality_terrain_cell_base + index,
            grandleon_psx_quality_terrain_cells[index], false
        );
    }
    upload_clut(quality_grass_clut, grandleon_psx_quality_terrain_cluts[0]);
    upload_clut(quality_forest_clut, grandleon_psx_quality_terrain_cluts[1]);
    upload_clut(quality_stone_clut, grandleon_psx_quality_terrain_cluts[2]);
}

[[nodiscard]] std::uint32_t load_combat_backdrop() {
    static_assert(grandleon_psx_backdrop_seed ==
                  grandleon_psx_comparison_backdrop_seed);
    static_assert(grandleon_psx_backdrop_variant ==
                  grandleon_psx_comparison_backdrop_variant);
    static_assert(grandleon_psx_backdrop_tile_width == 64);
    static_assert(grandleon_psx_backdrop_tile_height == 80);
    static_assert(grandleon_psx_backdrop_tile_count <= 12);
    static_assert(grandleon_psx_backdrop_overlay_tile_count <= 8);
    psx::start_clock();
    constexpr int main_tiles_per_page = 6;
    for (int index = 0; index < grandleon_psx_backdrop_tile_count; ++index) {
        const int page = backdrop_texture_page + index / main_tiles_per_page;
        const int within = index % main_tiles_per_page;
        const int u = (within % 2) * grandleon_psx_backdrop_tile_width;
        const int v = (within / 2) * grandleon_psx_backdrop_tile_height;
        gpu::upload(
            page * gpu::page_halfwords + u / 2,
            gpu::texture_page_y + v,
            grandleon_psx_backdrop_tile_width / 2,
            grandleon_psx_backdrop_tile_height,
            grandleon_psx_backdrop_tiles[index]
        );
    }
    for (int index = 0;
         index < grandleon_psx_backdrop_overlay_tile_count; ++index) {
        const int u = (index % 2) *
                      grandleon_psx_backdrop_overlay_tile_width;
        const int v = (index / 2) *
                      grandleon_psx_backdrop_overlay_tile_height;
        gpu::upload(
            backdrop_overlay_page * gpu::page_halfwords + u / 2,
            gpu::texture_page_y + v,
            grandleon_psx_backdrop_overlay_tile_width / 2,
            grandleon_psx_backdrop_overlay_tile_height,
            grandleon_psx_backdrop_overlay_tiles[index]
        );
    }
    gpu::upload(backdrop_clut_x, backdrop_clut_y, 256, 1,
                grandleon_psx_backdrop_clut);
    gpu::upload(backdrop_overlay_clut_x, backdrop_clut_y, 256, 1,
                grandleon_psx_backdrop_overlay_clut);
    static_cast<void>(gpu::read_back(
        backdrop_texture_page * gpu::page_halfwords, gpu::texture_page_y
    ));
    return psx::clock_ticks();
}

void draw_combat_backdrop() {
    constexpr int columns = grandleon_psx_backdrop_width /
                            grandleon_psx_backdrop_tile_width;
    constexpr int main_tiles_per_page = 6;
    for (int index = 0; index < grandleon_psx_backdrop_tile_count; ++index) {
        const int screen_x = (index % columns) *
                             grandleon_psx_backdrop_tile_width;
        const int screen_y = (index / columns) *
                             grandleon_psx_backdrop_tile_height;
        const int width = std::min(
            grandleon_psx_backdrop_tile_width,
            grandleon_psx_backdrop_width - screen_x
        );
        const int height = std::min(
            grandleon_psx_backdrop_tile_height,
            grandleon_psx_backdrop_height - screen_y
        );
        const int page = backdrop_texture_page + index / main_tiles_per_page;
        const int within = index % main_tiles_per_page;
        const int u = (within % 2) * grandleon_psx_backdrop_tile_width;
        const int v = (within / 2) * grandleon_psx_backdrop_tile_height;
        gpu::draw_texture_quad_ci8(
            screen_x, screen_y, width, height, page,
            u, v, u + width - 1, v + height - 1,
            backdrop_clut_x, backdrop_clut_y
        );
    }
}

void draw_combat_backdrop_overlay() {
    const int columns = (grandleon_psx_backdrop_overlay_width +
                         grandleon_psx_backdrop_overlay_tile_width - 1) /
                        grandleon_psx_backdrop_overlay_tile_width;
    for (int index = 0;
         index < grandleon_psx_backdrop_overlay_tile_count; ++index) {
        const int local_x = (index % columns) *
                            grandleon_psx_backdrop_overlay_tile_width;
        const int local_y = (index / columns) *
                            grandleon_psx_backdrop_overlay_tile_height;
        const int width = std::min(
            grandleon_psx_backdrop_overlay_tile_width,
            grandleon_psx_backdrop_overlay_width - local_x
        );
        const int height = std::min(
            grandleon_psx_backdrop_overlay_tile_height,
            grandleon_psx_backdrop_overlay_height - local_y
        );
        const int u = (index % 2) *
                      grandleon_psx_backdrop_overlay_tile_width;
        const int v = (index / 2) *
                      grandleon_psx_backdrop_overlay_tile_height;
        gpu::draw_texture_quad_ci8(
            grandleon_psx_backdrop_overlay_x + local_x,
            grandleon_psx_backdrop_overlay_y + local_y,
            width, height, backdrop_overlay_page,
            u, v, u + width - 1, v + height - 1,
            backdrop_overlay_clut_x, backdrop_clut_y
        );
    }
}

void upload_cutout(const Cutout& cutout, const unsigned short* pixels) {
    gpu::upload(
        cutout.page * gpu::page_halfwords + cutout.u / 4,
        gpu::texture_page_y + cutout.v,
        cutout.source_size / 4,
        cutout.source_size,
        pixels
    );
}

void upload_cutout_and_clut(
    const Cutout& cutout, const unsigned short* pixels,
    const unsigned short* palette
) {
    upload_cutout(cutout, pixels);
    upload_clut(cutout.clut, palette);
}

void load_godot_cutouts() {
    upload_cutout(
        medieval_male_cutout, grandleon_psx_godot_closeup_medieval_male_blue
    );
    upload_cutout(
        medieval_female_cutout,
        grandleon_psx_godot_closeup_medieval_female_red
    );
    upload_cutout(
        mythical_male_cutout, grandleon_psx_godot_closeup_mythical_male_blue
    );
    upload_cutout(
        mythical_female_cutout,
        grandleon_psx_godot_closeup_mythical_female_red
    );
    upload_cutout(
        medieval_move_a_cutout,
        grandleon_psx_godot_closeup_medieval_male_blue_move_a
    );
    upload_cutout(
        medieval_move_b_cutout,
        grandleon_psx_godot_closeup_medieval_male_blue_move_b
    );
    upload_clut(
        medieval_male_cutout.clut,
        grandleon_psx_godot_closeup_medieval_male_blue_clut
    );
    upload_clut(
        medieval_female_cutout.clut,
        grandleon_psx_godot_closeup_medieval_female_red_clut
    );
    upload_clut(
        mythical_male_cutout.clut,
        grandleon_psx_godot_closeup_mythical_male_blue_clut
    );
    upload_clut(
        mythical_female_cutout.clut,
        grandleon_psx_godot_closeup_mythical_female_red_clut
    );
    upload_clut(
        medieval_move_a_cutout.clut,
        grandleon_psx_godot_closeup_medieval_male_blue_move_a_clut
    );
    upload_clut(
        medieval_move_b_cutout.clut,
        grandleon_psx_godot_closeup_medieval_male_blue_move_b_clut
    );
}

void load_quality_close_cutouts(bool mythical) {
    if (mythical) {
        upload_cutout_and_clut(
            quality_left_slots.idle,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_idle,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_idle_clut
        );
        upload_cutout_and_clut(
            quality_left_slots.windup,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_windup,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_windup_clut
        );
        upload_cutout_and_clut(
            quality_left_slots.follow,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_follow,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_follow_clut
        );
        upload_cutout_and_clut(
            quality_left_slots.hurt,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_hurt,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_hurt_clut
        );
        upload_cutout_and_clut(
            quality_right_slots.idle,
            grandleon_psx_godot_closeup_quality_mythical_female_red_idle,
            grandleon_psx_godot_closeup_quality_mythical_female_red_idle_clut
        );
        upload_cutout_and_clut(
            quality_right_slots.windup,
            grandleon_psx_godot_closeup_quality_mythical_female_red_windup,
            grandleon_psx_godot_closeup_quality_mythical_female_red_windup_clut
        );
        upload_cutout_and_clut(
            quality_right_slots.follow,
            grandleon_psx_godot_closeup_quality_mythical_female_red_follow,
            grandleon_psx_godot_closeup_quality_mythical_female_red_follow_clut
        );
        upload_cutout_and_clut(
            quality_right_slots.hurt,
            grandleon_psx_godot_closeup_quality_mythical_female_red_hurt,
            grandleon_psx_godot_closeup_quality_mythical_female_red_hurt_clut
        );
        return;
    }
    upload_cutout_and_clut(
        quality_left_slots.idle,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_idle,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        quality_left_slots.windup,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_windup,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_windup_clut
    );
    upload_cutout_and_clut(
        quality_left_slots.follow,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_follow,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_follow_clut
    );
    upload_cutout_and_clut(
        quality_left_slots.hurt,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_hurt,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_hurt_clut
    );
    upload_cutout_and_clut(
        quality_right_slots.idle,
        grandleon_psx_godot_closeup_quality_medieval_female_red_idle,
        grandleon_psx_godot_closeup_quality_medieval_female_red_idle_clut
    );
    upload_cutout_and_clut(
        quality_right_slots.windup,
        grandleon_psx_godot_closeup_quality_medieval_female_red_windup,
        grandleon_psx_godot_closeup_quality_medieval_female_red_windup_clut
    );
    upload_cutout_and_clut(
        quality_right_slots.follow,
        grandleon_psx_godot_closeup_quality_medieval_female_red_follow,
        grandleon_psx_godot_closeup_quality_medieval_female_red_follow_clut
    );
    upload_cutout_and_clut(
        quality_right_slots.hurt,
        grandleon_psx_godot_closeup_quality_medieval_female_red_hurt,
        grandleon_psx_godot_closeup_quality_medieval_female_red_hurt_clut
    );
}

void load_tactical_cutouts() {
    upload_cutout_and_clut(
        tactical_cutouts[0],
        grandleon_psx_godot_closeup_tactical_medieval_knight_male_blue_idle,
        grandleon_psx_godot_closeup_tactical_medieval_knight_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[1],
        grandleon_psx_godot_closeup_tactical_medieval_archer_female_red_idle,
        grandleon_psx_godot_closeup_tactical_medieval_archer_female_red_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[2],
        grandleon_psx_godot_closeup_tactical_medieval_mage_male_blue_idle,
        grandleon_psx_godot_closeup_tactical_medieval_mage_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[3],
        grandleon_psx_godot_closeup_tactical_medieval_stormcaller_female_red_idle,
        grandleon_psx_godot_closeup_tactical_medieval_stormcaller_female_red_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[4],
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_idle,
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[5],
        grandleon_psx_godot_closeup_tactical_medieval_beast_female_red_idle,
        grandleon_psx_godot_closeup_tactical_medieval_beast_female_red_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[6],
        grandleon_psx_godot_closeup_tactical_medieval_healer_male_blue_idle,
        grandleon_psx_godot_closeup_tactical_medieval_healer_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[7],
        grandleon_psx_godot_closeup_tactical_medieval_commander_female_red_idle,
        grandleon_psx_godot_closeup_tactical_medieval_commander_female_red_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[8],
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_move_a,
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_move_a_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[9],
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_move_b,
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_move_b_clut
    );
}

void load_textures(
    const unsigned short* terrain_a, const unsigned short* terrain_a_palette,
    const unsigned short* terrain_b, const unsigned short* terrain_b_palette,
    const unsigned short* left, const unsigned short* left_palette,
    const unsigned short* right, const unsigned short* right_palette
) {
    upload_cell(terrain_a_cell, terrain_a, false);
    upload_cell(terrain_b_cell, terrain_b, false);
    upload_cell(left_cell, left, true);
    upload_cell(right_cell, right, true);
    upload_clut(terrain_a_clut, terrain_a_palette);
    upload_clut(terrain_b_clut, terrain_b_palette);
    upload_clut(left_clut, left_palette);
    upload_clut(right_clut, right_palette);
}

void draw_ground(const Camera& camera, int treatment) {
    constexpr int tile = 64;
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 8; ++column) {
            const int x0 = (column - 4) * tile;
            const int x1 = x0 + tile;
            const int z0 = (row - 2) * tile;
            const int z1 = z0 + tile;
            const ProjectedVertex a = project(x0, 0, z0, camera);
            const ProjectedVertex b = project(x1, 0, z0, camera);
            const ProjectedVertex c = project(x1, 0, z1, camera);
            const ProjectedVertex d = project(x0, 0, z1, camera);
            if (treatment == 1) {
                const bool alternate = ((row + column) & 3) == 0;
                const int cell = alternate ? terrain_b_cell : terrain_a_cell;
                const int clut = alternate ? terrain_b_clut : terrain_a_clut;
                gpu::draw_cell_triangle(
                    a.x, a.y, 0, 0, b.x, b.y, 31, 0,
                    c.x, c.y, 31, 31, cell, clut
                );
                gpu::draw_cell_triangle(
                    a.x, a.y, 0, 0, c.x, c.y, 31, 31,
                    d.x, d.y, 0, 31, cell, clut
                );
            } else if (treatment == 2) {
                // Terrain type changes form contiguous macro planes: a broad
                // central route, quiet field and distant rock/wood edges.
                const bool route = column == 3 || column == 4;
                const bool edge = (column == 0 || column == 7) && row < 3;
                const int cell = route ? terrain_b_cell : (edge ? 2 : terrain_a_cell);
                const int clut = route ? terrain_b_clut : (edge ? 3 : terrain_a_clut);
                gpu::draw_cell_triangle(
                    a.x, a.y, 0, 0, b.x, b.y, 31, 0,
                    c.x, c.y, 31, 31, cell, clut
                );
                gpu::draw_cell_triangle(
                    a.x, a.y, 0, 0, c.x, c.y, 31, 31,
                    d.x, d.y, 0, 31, cell, clut
                );
            } else {
                const std::uint16_t colour = ((row + column) & 3) == 0
                    ? gpu::rgb15(12, 12, 9) : gpu::rgb15(8, 15, 8);
                gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y, colour);
                gpu::draw_triangle(a.x, a.y, c.x, c.y, d.x, d.y, colour);
            }
        }
        static_cast<void>(psx::clock_ticks());
    }
}

void draw_team_marker(
    int origin_x, int origin_z, int team, const Camera& camera
) {
    const ProjectedVertex left = project(origin_x - 34, 1, origin_z, camera);
    const ProjectedVertex far = project(origin_x, 1, origin_z - 17, camera);
    const ProjectedVertex right = project(origin_x + 34, 1, origin_z, camera);
    const ProjectedVertex near = project(origin_x, 1, origin_z + 17, camera);
    const std::uint16_t colour = colour15(team_colours[team]);
    gpu::draw_triangle(left.x, left.y, far.x, far.y, right.x, right.y, colour);
    gpu::draw_triangle(left.x, left.y, right.x, right.y, near.x, near.y, colour);
}

[[nodiscard]] SceneResult draw_scene(
    const SevenfoldProxy& left, const SevenfoldProxy& right,
    bool textured, const char* title, const char* subtitle,
    std::uint16_t sky
) {
    const Camera camera{360, 420, 211};
    gpu::clear(sky);
    psx::start_clock();
    draw_ground(camera, textured ? 1 : 0);
    draw_team_marker(-70, 0, 0, camera);
    draw_team_marker(70, 0, 1, camera);
    int textured_triangles = draw_model(
        left, -70, 0, 0, 1, camera, textured, left_cell, left_clut
    );
    textured_triangles += draw_model(
        right, 70, 0, 1, -1, camera, textured, right_cell, right_clut
    );

    // A restrained native HUD keeps the comparison legible without hiding
    // the characters; the blue/red swatches reinforce team identity even
    // where a textured costume has mostly neutral colours.
    gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    gpu::fill(6, 4, 7, 10, colour15(team_colours[0]));
    text_at(18, 5, title);
    gpu::fill(306, 4, 7, 10, colour15(team_colours[1]));
    gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 227, subtitle);
    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(), textured_triangles};
}

void draw_cutout(int centre_x, int bottom, int size, const Cutout& cutout) {
    gpu::draw_texture_quad(
        centre_x - size / 2, bottom - size, size, size,
        cutout.page, cutout.u, cutout.v,
        cutout.u + cutout.source_size - 1,
        cutout.v + cutout.source_size - 1,
        cutout.clut
    );
}

void draw_screen_shadow(int centre_x, int bottom, int team, int width) {
    gpu::fill(
        centre_x - width / 2, bottom - 5, width, 5, gpu::rgb15(2, 3, 3)
    );
    gpu::fill(
        centre_x - width / 3, bottom - 2, width * 2 / 3, 2,
        colour15(team_colours[team], 220)
    );
}

void draw_textured_floor_quad(
    int far_left, int far_y, int far_right,
    int near_left, int near_y, int near_right,
    int cell, int clut
) {
    gpu::draw_cell_triangle(
        far_left, far_y, 0, 0,
        far_right, far_y, 31, 0,
        near_right, near_y, 31, 31,
        cell, clut
    );
    gpu::draw_cell_triangle(
        far_left, far_y, 0, 0,
        near_right, near_y, 31, 31,
        near_left, near_y, 0, 31,
        cell, clut
    );
}

void draw_corrected_close_stage(std::uint16_t sky) {
    constexpr int horizon = 142;
    constexpr int middle = 178;
    constexpr int front = 222;

    // The eye line is behind the units' torsos, not beneath their boots. A
    // shallow distant silhouette establishes that line before the quiet floor
    // begins; the floor then keeps twenty-four visible pixels in front of the
    // shared foot line instead of climbing all the way up the background.
    gpu::fill(0, 18, gpu::screen_width, horizon - 18, sky);
    const std::uint16_t distance = gpu::rgb15(5, 8, 8);
    gpu::draw_triangle(0, horizon, 70, 96, 148, horizon, distance);
    gpu::draw_triangle(172, horizon, 252, 100, 320, horizon, distance);
    gpu::fill(0, horizon - 4, gpu::screen_width, 5, gpu::rgb15(4, 6, 7));

    // Two depth bands are enough to read as floor without the tunnel-like
    // convergence of the old ground-level camera. The central path expands
    // gently toward the viewer while the field stays one calm macro plane.
    draw_textured_floor_quad(
        0, horizon, 139, 0, middle, 121,
        terrain_a_cell, terrain_a_clut
    );
    draw_textured_floor_quad(
        181, horizon, 320, 199, middle, 320,
        terrain_a_cell, terrain_a_clut
    );
    draw_textured_floor_quad(
        139, horizon, 181, 121, middle, 199,
        terrain_b_cell, terrain_b_clut
    );
    draw_textured_floor_quad(
        0, middle, 121, 0, front, 92,
        terrain_a_cell, terrain_a_clut
    );
    draw_textured_floor_quad(
        199, middle, 320, 228, front, 320,
        terrain_a_cell, terrain_a_clut
    );
    draw_textured_floor_quad(
        121, middle, 199, 92, front, 228,
        terrain_b_cell, terrain_b_clut
    );
}

[[nodiscard]] SceneResult draw_hybrid_close(
    bool mythical, const char* title, const char* subtitle, std::uint16_t sky
) {
    const Camera camera{360, 420, 211};
    const Cutout& left = mythical ? mythical_male_cutout : medieval_male_cutout;
    const Cutout& right = mythical ? mythical_female_cutout : medieval_female_cutout;
    gpu::clear(sky);
    psx::start_clock();
    draw_ground(camera, 2);
    draw_screen_shadow(86, 216, 0, 70);
    draw_screen_shadow(234, 216, 1, 70);
    draw_cutout(86, 216, 112, left);
    draw_cutout(234, 216, 112, right);

    // A small layered contact effect gives the still comparison some battle
    // staging without obscuring either silhouette.
    gpu::draw_triangle(151, 145, 160, 130, 169, 145, gpu::rgb15(31, 24, 8));
    gpu::draw_triangle(153, 145, 160, 157, 167, 145, gpu::rgb15(31, 30, 20));
    gpu::fill(158, 139, 5, 12, gpu::rgb15(31, 27, 13));

    gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    gpu::fill(6, 4, 7, 10, colour15(team_colours[0]));
    text_at(18, 5, title);
    gpu::fill(306, 4, 7, 10, colour15(team_colours[1]));
    gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 227, subtitle);
    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(), 82};
}

[[nodiscard]] SceneResult draw_corrected_hybrid_close(
    bool mythical, const char* title, const char* subtitle, std::uint16_t sky
) {
    const Cutout& left = mythical ? mythical_male_cutout : medieval_male_cutout;
    const Cutout& right = mythical ? mythical_female_cutout : medieval_female_cutout;
    gpu::clear(sky);
    psx::start_clock();
    draw_corrected_close_stage(sky);

    constexpr int ground_line = 198;
    draw_screen_shadow(86, ground_line, 0, 68);
    draw_screen_shadow(234, ground_line, 1, 68);
    draw_cutout(86, ground_line, 108, left);
    draw_cutout(234, ground_line, 108, right);

    // Contact sits between the upper bodies at the new eye line. It leaves
    // both faces and both weapons readable and has space to grow for a recoil
    // or damage-number pass later.
    gpu::draw_triangle(151, 140, 160, 126, 169, 140, gpu::rgb15(31, 24, 8));
    gpu::draw_triangle(153, 140, 160, 152, 167, 140, gpu::rgb15(31, 30, 20));
    gpu::fill(158, 134, 5, 12, gpu::rgb15(31, 27, 13));

    gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    gpu::fill(6, 4, 7, 10, colour15(team_colours[0]));
    text_at(18, 5, title);
    gpu::fill(306, 4, 7, 10, colour15(team_colours[1]));
    gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 227, subtitle);
    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(), 14};
}

void draw_elevated_solid_quad(
    int x0, int z0, int x1, int z1, int y,
    const ElevatedCamera& camera, std::uint16_t colour
) {
    const ProjectedVertex a = project_elevated(x0, y, z0, camera);
    const ProjectedVertex b = project_elevated(x1, y, z0, camera);
    const ProjectedVertex c = project_elevated(x1, y, z1, camera);
    const ProjectedVertex d = project_elevated(x0, y, z1, camera);
    gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y, colour);
    gpu::draw_triangle(a.x, a.y, c.x, c.y, d.x, d.y, colour);
}

void draw_elevated_cell_quad(
    int x0, int z0, int x1, int z1, int y,
    const ElevatedCamera& camera, int cell, int clut
) {
    const ProjectedVertex a = project_elevated(x0, y, z0, camera);
    const ProjectedVertex b = project_elevated(x1, y, z0, camera);
    const ProjectedVertex c = project_elevated(x1, y, z1, camera);
    const ProjectedVertex d = project_elevated(x0, y, z1, camera);
    gpu::draw_cell_triangle(
        a.x, a.y, 0, 0, b.x, b.y, 31, 0,
        c.x, c.y, 31, 31, cell, clut
    );
    gpu::draw_cell_triangle(
        a.x, a.y, 0, 0, c.x, c.y, 31, 31,
        d.x, d.y, 0, 31, cell, clut
    );
}

[[nodiscard]] int quality_terrain_cell(
    int kind, int variant, int transition_mask
) noexcept {
    variant &= 3;
    if (kind == 0) return quality_terrain_cell_base + variant;
    if (kind == 1 && transition_mask != 0) {
        return quality_terrain_cell_base + 12 + transition_mask;
    }
    if (kind == 2 && transition_mask != 0) {
        return quality_terrain_cell_base + 28 + transition_mask;
    }
    return quality_terrain_cell_base + (kind == 1 ? 4 : 8) + variant;
}

[[nodiscard]] int quality_terrain_clut(int kind) noexcept {
    return kind == 0 ? quality_grass_clut
        : (kind == 1 ? quality_forest_clut : quality_stone_clut);
}

void draw_quality_terrain_tile(
    int x0, int z0, int x1, int z1, int kind, int variant,
    int transition_mask, const ElevatedCamera& camera
) {
    draw_elevated_cell_quad(
        x0, z0, x1, z1, 0, camera,
        quality_terrain_cell(kind, variant, transition_mask),
        quality_terrain_clut(kind)
    );
}

void draw_quality_cell_boundary(
    int x0, int z0, int x1, int z1, const ElevatedCamera& camera
) {
    const std::uint16_t colour = gpu::rgb15(4, 8, 6);
    draw_elevated_solid_quad(x1 - 2, z0, x1, z1, 2, camera, colour);
    draw_elevated_solid_quad(x0, z1 - 2, x1, z1, 2, camera, colour);
}

void draw_quality_tree(
    int x, int z, int size, const ElevatedCamera& camera
) {
    draw_elevated_solid_quad(
        x - size / 2, z - size / 5, x + size / 2, z + size / 5, 2,
        camera, gpu::rgb15(2, 5, 3)
    );
    const ProjectedVertex base = project_elevated(x, 2, z, camera);
    const ProjectedVertex trunk_top =
        project_elevated(x, size * 3 / 4, z, camera);
    const ProjectedVertex crown =
        project_elevated(x, size * 3 / 2, z, camera);
    gpu::fill(
        base.x - 2, trunk_top.y, 5, std::max(2, base.y - trunk_top.y),
        gpu::rgb15(10, 7, 4)
    );
    const int radius = std::max(6, size / 3);
    gpu::draw_triangle(
        crown.x, crown.y - radius,
        crown.x - radius, trunk_top.y + 2,
        crown.x + radius, trunk_top.y + 2,
        gpu::rgb15(4, 14, 7)
    );
    gpu::draw_triangle(
        crown.x, crown.y - radius / 3,
        crown.x - radius * 4 / 5, trunk_top.y - radius / 3,
        crown.x + radius * 4 / 5, trunk_top.y - radius / 3,
        gpu::rgb15(7, 20, 9)
    );
}

void draw_quality_rock(
    int x, int z, int size, const ElevatedCamera& camera
) {
    const ProjectedVertex base = project_elevated(x, 2, z, camera);
    const ProjectedVertex top = project_elevated(x - size / 5, size / 2, z, camera);
    gpu::draw_triangle(
        base.x - size / 2, base.y, top.x, top.y,
        base.x + size / 2, base.y, gpu::rgb15(13, 15, 15)
    );
    gpu::draw_triangle(
        base.x - size / 2, base.y, top.x, top.y,
        base.x - size / 5, base.y - size / 4, gpu::rgb15(17, 18, 17)
    );
}

void draw_quality_grass(
    int x, int z, int size, const ElevatedCamera& camera
) {
    const ProjectedVertex base = project_elevated(x, 2, z, camera);
    const ProjectedVertex tip = project_elevated(x, size, z, camera);
    const std::uint16_t colour = gpu::rgb15(10, 21, 9);
    gpu::draw_triangle(
        base.x - size / 4, base.y, tip.x - 2, tip.y,
        base.x, base.y, colour
    );
    gpu::draw_triangle(
        base.x, base.y, tip.x + 3, tip.y + 2,
        base.x + size / 4, base.y, colour
    );
}

[[nodiscard]] ElevatedCamera close_camera_v2() noexcept {
    ElevatedCamera camera{0, 0, 256, -64, 0, 180, 245, 256};
    RawBounds bounds{0, 0, 0, 0, false};
    constexpr int left = -168;
    constexpr int right = 168;
    constexpr int far = -96;
    constexpr int near = 144;
    include_world(bounds, left, 0, far, camera);
    include_world(bounds, right, 0, far, camera);
    include_world(bounds, left, 0, near, camera);
    include_world(bounds, right, 0, near, camera);
    include_billboard(bounds, -70, 0, 132, camera);
    include_billboard(bounds, 70, 0, 132, camera);
    return fit_elevated_camera(camera, bounds, 6, 22, 314, 219);
}

[[nodiscard]] int close_terrain_kind(int row, int column) noexcept {
    if (column == 3) return 2;
    if (column == 0 || column == 6 ||
        (row < 2 && (column == 1 || column == 5))) return 1;
    return 0;
}

[[nodiscard]] int close_transition_mask(int row, int column, int kind) noexcept {
    if (kind == 0) return 0;
    int mask = 0;
    if (row > 0 && close_terrain_kind(row - 1, column) != kind) mask |= 1;
    if (column < 6 && close_terrain_kind(row, column + 1) != kind) mask |= 2;
    if (row < 4 && close_terrain_kind(row + 1, column) != kind) mask |= 4;
    if (column > 0 && close_terrain_kind(row, column - 1) != kind) mask |= 8;
    return mask;
}

void draw_close_stage_v2(const ElevatedCamera& camera, bool rich_terrain) {
    constexpr int tile = 48;
    constexpr int left = -168;
    constexpr int far = -96;
    // Every visible terrain primitive is a tile top in the same projection as
    // the combatants' feet. There are no vertical walls or fake horizons that
    // can rise behind the boots and invert the camera read.
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 7; ++column) {
            const int x0 = left + column * tile;
            const int z0 = far + row * tile;
            if (rich_terrain) {
                const int kind = close_terrain_kind(row, column);
                draw_quality_terrain_tile(
                    x0, z0, x0 + tile, z0 + tile, kind,
                    (row * 7 + column * 3) & 3,
                    close_transition_mask(row, column, kind), camera
                );
            } else {
                const bool route = column == 3;
                const bool edge = column == 0 || column == 6;
                const int cell = route ? terrain_b_cell
                    : (edge ? 2 : terrain_a_cell);
                const int clut = route ? terrain_b_clut
                    : (edge ? 3 : terrain_a_clut);
                draw_elevated_cell_quad(
                    x0, z0, x0 + tile, z0 + tile, 0, camera, cell, clut
                );
            }
        }
    }
}

void draw_capture_floor_strip(const ElevatedCamera& camera) {
    constexpr int tile = 48;
    constexpr int left = -240;
    constexpr int far = 0;
    // Three rows overlap the distant plate at its contract floor line and run
    // past every lower-frame edge. The north/south transition is preserved as
    // a grass-left / paved-right split, matching the selected Godot plate.
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 10; ++column) {
            const int x0 = left + column * tile;
            const int z0 = far + row * tile;
            const int kind = column < 5 ? 0 : 2;  // grass / paved
            draw_quality_terrain_tile(
                x0, z0, x0 + tile, z0 + tile, kind,
                (row * 7 + column * 3) & 3,
                0, camera
            );
        }
    }
}

void draw_close_far_decorations(const ElevatedCamera& camera) {
    draw_quality_tree(-143, -64, 32, camera);
    draw_quality_tree(137, -70, 38, camera);
    draw_quality_tree(-124, -34, 24, camera);
    draw_quality_rock(-18, -65, 13, camera);
    draw_quality_rock(25, -42, 10, camera);
    draw_quality_grass(-90, -18, 12, camera);
}

void draw_close_near_decorations(const ElevatedCamera& camera) {
    draw_quality_grass(-145, 108, 13, camera);
    draw_quality_grass(132, 117, 11, camera);
    draw_quality_rock(18, 104, 12, camera);
}

void draw_elevated_marker(
    int x, int z, int team, int radius, const ElevatedCamera& camera
) {
    draw_elevated_solid_quad(
        x - radius, z - radius / 3, x + radius, z + radius / 3, 2,
        camera, gpu::rgb15(2, 3, 3)
    );
    draw_elevated_solid_quad(
        x - radius * 2 / 3, z - 2, x + radius * 2 / 3, z + 3, 3,
        camera, colour15(team_colours[team], 220)
    );
}

constexpr int close_exchange_frames = 40;
constexpr int close_left_positions[close_exchange_frames] = {
    -70, -70, -70, -70, -71, -73, -74, -72, -67, -61,
    -54, -48, -45, -46, -50, -56, -62, -67, -69, -70,
    -70, -70, -70, -70, -70, -70, -70, -70, -70, -70,
    -70, -70, -78, -77, -76, -74, -72, -71, -70, -70,
};
constexpr int close_right_positions[close_exchange_frames] = {
    70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
    70, 70, 78, 77, 76, 74, 72, 71, 70, 70,
    70, 70, 70, 70, 71, 73, 74, 72, 67, 61,
    54, 48, 45, 46, 50, 56, 62, 67, 69, 70,
};

[[nodiscard]] constexpr int close_left_x(int phase) noexcept {
    return phase >= 0 && phase < close_exchange_frames
        ? close_left_positions[phase] : -70;
}

[[nodiscard]] constexpr int close_right_x(int phase) noexcept {
    return phase >= 0 && phase < close_exchange_frames
        ? close_right_positions[phase] : 70;
}

[[nodiscard]] const Cutout& quality_left_pose(int phase) noexcept {
    if (phase >= 32 && phase <= 36) return quality_left_slots.hurt;
    if (phase >= 12 && phase <= 16) return quality_left_slots.follow;
    if (phase >= 4 && phase <= 11) return quality_left_slots.windup;
    return quality_left_slots.idle;
}

[[nodiscard]] const Cutout& quality_right_pose(int phase) noexcept {
    if (phase >= 12 && phase <= 16) return quality_right_slots.hurt;
    if (phase >= 32 && phase <= 36) return quality_right_slots.follow;
    if (phase >= 24 && phase <= 31) return quality_right_slots.windup;
    return quality_right_slots.idle;
}

void draw_world_impact(
    int x, int y, int z, const ElevatedCamera& camera
) {
    const ProjectedVertex centre = project_elevated(x, y, z, camera);
    gpu::draw_triangle(
        centre.x - 11, centre.y, centre.x, centre.y - 17,
        centre.x + 11, centre.y, gpu::rgb15(31, 22, 5)
    );
    gpu::draw_triangle(
        centre.x - 8, centre.y, centre.x, centre.y + 13,
        centre.x + 8, centre.y, gpu::rgb15(31, 30, 18)
    );
    gpu::fill(centre.x - 2, centre.y - 10, 5, 20, gpu::rgb15(31, 27, 10));
}

void draw_capture_health(
    const ProjectedVertex& foot, int size, int health, std::uint16_t team
) {
    const int top = std::max(4, foot.y - size + 1);
    gpu::fill(foot.x - 14, top, 28, 5, gpu::rgb15(2, 2, 3));
    gpu::fill(foot.x - 13, top + 1, health * 2, 3,
              health > 8 ? gpu::rgb15(7, 24, 9) : gpu::rgb15(27, 17, 5));
    gpu::fill(foot.x - 18, top, 4, 5, team);
    text_at(foot.x - 5, top - 9,
            health == 12 ? "12" : (health == 11 ? "11" : "8"));
}

[[nodiscard]] ElevatedCamera comparison_close_camera() noexcept {
    ElevatedCamera camera = close_camera_v2();
    camera.centre_x = gpu::screen_width / 2;
    const ProjectedVertex current_ground = project_elevated(-70, 3, 0, camera);
    const int wanted_ground = gpu::screen_height *
        grandleon_psx_comparison_close_layout_milli[2] / 1000;
    camera.centre_y += wanted_ground - current_ground.y;
    return camera;
}

[[nodiscard]] SceneResult draw_hybrid_close_v2(
    bool mythical, int phase, const char* title, const char* subtitle,
    std::uint16_t sky, bool quality = true, bool rich_terrain = true,
    bool capture_mode = false, bool backdrop_mode = true,
    int left_health = 12, int right_health = 11
) {
    const ElevatedCamera camera = capture_mode
        ? comparison_close_camera() : close_camera_v2();
    const Cutout& left = quality ? quality_left_pose(phase)
        : (mythical ? mythical_male_cutout : medieval_male_cutout);
    const Cutout& right = quality ? quality_right_pose(phase)
        : (mythical ? mythical_female_cutout : medieval_female_cutout);
    const int left_x = close_left_x(phase) - (capture_mode ? 14 : 0);
    const int right_x = close_right_x(phase) + (capture_mode ? 14 : 0);
    const int unit_world_size = capture_mode ? 170 : 132;
    const int unit_size = elevated_billboard_size(unit_world_size, camera);

    gpu::clear(sky);
    psx::start_clock();
    if (capture_mode && backdrop_mode) {
        draw_combat_backdrop();
        draw_combat_backdrop_overlay();
        draw_capture_floor_strip(camera);
    } else {
        draw_close_stage_v2(camera, rich_terrain);
        if (rich_terrain) draw_close_far_decorations(camera);
    }
    draw_elevated_marker(left_x, 0, 0, 40, camera);
    draw_elevated_marker(right_x, 0, 1, 40, camera);
    const ProjectedVertex left_foot = project_elevated(left_x, 3, 0, camera);
    const ProjectedVertex right_foot = project_elevated(right_x, 3, 0, camera);
    // The curated square has transparent breathing room below the painted
    // shoes. Move only that transparent margin through the projected foot
    // line so the visible soles meet their world-space grounding marks.
    const int transparent_sole_margin = unit_size *
        (quality ? 2 : 8) / left.source_size;
    draw_cutout(
        left_foot.x, left_foot.y + transparent_sole_margin, unit_size, left
    );
    draw_cutout(
        right_foot.x, right_foot.y + transparent_sole_margin, unit_size, right
    );
    if (phase == 12) {
        draw_world_impact(right_x - 13, 68, 0, camera);
        const ProjectedVertex damage = project_elevated(right_x, 102, 0, camera);
        text_at(damage.x - 3, damage.y,
                grandleon_psx_comparison_damage[0] == 3 ? "3" : "?");
    }
    if (phase == 32) {
        draw_world_impact(left_x + 13, 68, 0, camera);
        const ProjectedVertex damage = project_elevated(left_x, 102, 0, camera);
        text_at(damage.x - 3, damage.y,
                grandleon_psx_comparison_damage[1] == 4 ? "4" : "?");
    }
    if (rich_terrain && (!capture_mode || !backdrop_mode)) {
        draw_close_near_decorations(camera);
    }

    if (capture_mode) {
        draw_capture_health(
            left_foot, unit_size, left_health,
            grandleon_psx_comparison_team_colours[0]
        );
        draw_capture_health(
            right_foot, unit_size, right_health,
            grandleon_psx_comparison_team_colours[1]
        );
    } else {
        gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
        gpu::fill(6, 4, 7, 10, colour15(team_colours[0]));
        text_at(18, 5, title);
        gpu::fill(306, 4, 7, 10, colour15(team_colours[1]));
        gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
        text_at(8, 227, subtitle);
    }
    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(),
            capture_mode && backdrop_mode
                ? grandleon_psx_backdrop_tile_count +
                  grandleon_psx_backdrop_overlay_tile_count + 44
                : 72};
}

constexpr std::uint8_t overview_terrain_colours[4][3] = {
    {77, 145, 71}, {168, 135, 83}, {39, 97, 58}, {117, 105, 93},
};

[[nodiscard]] int overview_kind(int row, int column) noexcept {
    if (column == 3 || column == 4) return 1;          // broad road
    if (row < 2 && column < 3) return 2;               // forest mass
    if (row < 2 && column > 5) return 3;               // mountain mass
    if (row > 3 && column < 2) return 3;               // ruin/stone mass
    return 0;                                          // open field
}

void draw_world_quad(
    int x0, int z0, int x1, int z1, int y, const Camera& camera,
    std::uint16_t colour
) {
    const ProjectedVertex a = project(x0, y, z0, camera);
    const ProjectedVertex b = project(x1, y, z0, camera);
    const ProjectedVertex c = project(x1, y, z1, camera);
    const ProjectedVertex d = project(x0, y, z1, camera);
    gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y, colour);
    gpu::draw_triangle(a.x, a.y, c.x, c.y, d.x, d.y, colour);
}

void draw_overview_ground(const Camera& camera, bool refined) {
    constexpr int tile = 46;
    for (int row = 0; row < sevenfold_board_rows; ++row) {
        for (int column = 0; column < sevenfold_board_columns; ++column) {
            const int x0 = (column - sevenfold_board_columns / 2) * tile;
            const int x1 = x0 + tile;
            const int z0 = (row - sevenfold_board_rows / 2) * tile;
            const int z1 = z0 + tile;
            const int kind = overview_kind(row, column);
            const ProjectedVertex a = project(x0, 0, z0, camera);
            const ProjectedVertex b = project(x1, 0, z0, camera);
            const ProjectedVertex c = project(x1, 0, z1, camera);
            const ProjectedVertex d = project(x0, 0, z1, camera);
            if (refined) {
                const int cell = kind == 0 ? terrain_a_cell
                    : (kind == 1 ? terrain_b_cell : 2);
                const int clut = kind == 0 ? terrain_a_clut
                    : (kind == 1 ? terrain_b_clut : 3);
                gpu::draw_cell_triangle(
                    a.x, a.y, 0, 0, b.x, b.y, 31, 0,
                    c.x, c.y, 31, 31, cell, clut
                );
                gpu::draw_cell_triangle(
                    a.x, a.y, 0, 0, c.x, c.y, 31, 31,
                    d.x, d.y, 0, 31, cell, clut
                );
            } else {
                const std::uint16_t colour = colour15(overview_terrain_colours[kind]);
                gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y, colour);
                gpu::draw_triangle(a.x, a.y, c.x, c.y, d.x, d.y, colour);
            }
        }
        static_cast<void>(psx::clock_ticks());
    }
}

void draw_movement_highlights(const Camera& camera) {
    constexpr int tile = 46;
    constexpr int cells[][2] = {{3, 0}, {2, 0}, {4, 0}, {3, 1}, {2, 1}};
    for (int index = 0; index < 5; ++index) {
        const int row = cells[index][0];
        const int column = cells[index][1];
        const int x0 = (column - sevenfold_board_columns / 2) * tile + 5;
        const int z0 = (row - sevenfold_board_rows / 2) * tile + 5;
        const std::uint16_t colour = index == 0
            ? gpu::rgb15(8, 22, 31) : gpu::rgb15(6, 16, 24);
        draw_world_quad(x0, z0, x0 + tile - 10, z0 + tile - 10, 2, camera, colour);
    }
}

struct OverviewUnit final {
    int column;
    int row;
    int team;
    int style;
    int body;
    int archetype;
    const Cutout* cutout;
};

constexpr OverviewUnit overview_units[] = {
    {0, 1, 0, 0, 0, 0, &medieval_male_cutout},
    {7, 1, 1, 0, 1, 0, &medieval_female_cutout},
    {2, 2, 0, 2, 0, 2, &mythical_male_cutout},
    {5, 2, 1, 2, 1, 2, &mythical_female_cutout},
    {0, 3, 0, 0, 0, 0, &medieval_male_cutout},
    {7, 3, 1, 0, 1, 0, &medieval_female_cutout},
    {2, 4, 0, 2, 0, 2, &mythical_male_cutout},
    {5, 4, 1, 2, 1, 2, &mythical_female_cutout},
};

constexpr OverviewUnit quality_overview_units[] = {
    {0, 1, 0, 0, 0, 0, &tactical_cutouts[0]},
    {7, 1, 1, 0, 1, 1, &tactical_cutouts[1]},
    {2, 2, 0, 0, 0, 2, &tactical_cutouts[2]},
    {5, 2, 1, 0, 1, 3, &tactical_cutouts[3]},
    {0, 3, 0, 0, 0, 5, &tactical_cutouts[4]},
    {7, 3, 1, 0, 1, 6, &tactical_cutouts[5]},
    {2, 4, 0, 0, 0, 4, &tactical_cutouts[6]},
    {5, 4, 1, 0, 1, 7, &tactical_cutouts[7]},
};

constexpr int quality_movement_frames = 16;
constexpr int quality_move_progress[quality_movement_frames] = {
    0, 1, 2, 4, 7, 11, 16, 22, 28, 34, 39, 42, 44, 45, 46, 46,
};

void draw_unit_status(
    const ProjectedVertex& anchor, int size, int team, int health,
    bool selected
) {
    const int top = anchor.y - size + 2;
    gpu::fill(anchor.x - 11, top, 22, 4, gpu::rgb15(2, 2, 3));
    const std::uint16_t health_colour = health >= 60
        ? gpu::rgb15(7, 24, 9) : gpu::rgb15(27, 17, 5);
    gpu::fill(anchor.x - 10, top + 1, health / 5, 2, health_colour);
    if (selected) {
        gpu::draw_triangle(
            anchor.x - 5, top - 5,
            anchor.x + 5, top - 5,
            anchor.x, top, colour15(team_colours[team])
        );
    }
}

[[nodiscard]] SceneResult draw_overview_frame(bool refined, int move_step) {
    constexpr int tile = 46;
    const Camera camera{570, 350, 185};
    gpu::clear(gpu::rgb15(5, 9, 11));
    psx::start_clock();
    draw_overview_ground(camera, refined);
    draw_movement_highlights(camera);
    constexpr int overview_unit_count =
        static_cast<int>(sizeof(overview_units) / sizeof(overview_units[0]));
    for (int index = 0; index < overview_unit_count; ++index) {
        const OverviewUnit& unit = overview_units[index];
        const int x = (unit.column - sevenfold_board_columns / 2) * tile + tile / 2;
        int z = (unit.row - sevenfold_board_rows / 2) * tile + tile / 2;
        const bool moving = refined && move_step >= 0 && index == 4;
        if (moving) z += tile * move_step / 7;
        if (refined) {
            const ProjectedVertex anchor = project(x, 2, z, camera);
            draw_screen_shadow(anchor.x, anchor.y + 2, unit.team, 24);
            const Cutout& art = moving
                ? ((move_step & 1) == 0
                    ? medieval_move_a_cutout : medieval_move_b_cutout)
                : *unit.cutout;
            draw_cutout(anchor.x, anchor.y + 2, 38, art);
            draw_unit_status(
                anchor, 38, unit.team, 92 - index * 5,
                index == 4
            );
        } else {
            draw_team_marker(x, z, unit.team, camera);
            const SevenfoldProxy& proxy =
                sevenfold_proxies_low[unit.style][unit.body][unit.archetype];
            static_cast<void>(draw_model(
                proxy, x, z, unit.team, 0, camera, false, 0, 0
            ));
        }
    }
    gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 5, move_step >= 0 ? "TACTICAL MOVE 2.5D"
        : (refined ? "TACTICAL MAP HYBRID" : "TACTICAL MAP FLAT 3D"));
    gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 227, move_step >= 0
        ? "MOVE POSE  DEPTH ORDER  HEALTH  TEAM"
        : (refined ? "MACRO TERRAIN  STRONG REACH  CI4 UNITS"
        : "MATCHED CAMERA  UNITS  TERRAIN  REACH"));
    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(), refined ? 104 : 0};
}

[[nodiscard]] SceneResult draw_overview(bool refined) {
    return draw_overview_frame(refined, -1);
}

[[nodiscard]] ElevatedCamera overview_camera_v2() noexcept {
    ElevatedCamera camera{0, 0, 220, -220, 105, 105, 260, 256};
    RawBounds bounds{0, 0, 0, 0, false};
    constexpr int tile = 46;
    constexpr int left = -sevenfold_board_columns * tile / 2;
    constexpr int right = left + sevenfold_board_columns * tile;
    constexpr int far = -sevenfold_board_rows * tile / 2;
    constexpr int near = far + sevenfold_board_rows * tile;
    include_world(bounds, left, 0, far, camera);
    include_world(bounds, right, 0, far, camera);
    include_world(bounds, left, 0, near, camera);
    include_world(bounds, right, 0, near, camera);
    for (const OverviewUnit& unit : quality_overview_units) {
        const int x = left + unit.column * tile + tile / 2;
        const int z = far + unit.row * tile + tile / 2;
        include_billboard(bounds, x, z, 80, camera);
    }
    // The selected unit's final movement destination is part of the fitted
    // scene bounds too, so a real animation never escapes the camera contract.
    include_billboard(bounds, left + tile / 2, far + 4 * tile + tile / 2,
                      80, camera);
    return fit_elevated_camera(camera, bounds, 7, 22, 313, 218);
}

struct OverviewTileOrder final {
    int row;
    int column;
    std::int32_t depth;
};

struct OverviewUnitOrder final {
    int index;
    int x;
    int z;
    std::int32_t depth;
};

[[nodiscard]] int overview_quality_kind(int row, int column) noexcept {
    const int kind = overview_kind(row, column);
    if (kind == 0) return 0;
    if (kind == 2) return 1;
    return 2;
}

[[nodiscard]] int overview_transition_mask(
    int row, int column, int kind
) noexcept {
    if (kind == 0) return 0;
    int mask = 0;
    if (row > 0 && overview_quality_kind(row - 1, column) != kind) mask |= 1;
    if (column + 1 < sevenfold_board_columns &&
        overview_quality_kind(row, column + 1) != kind) mask |= 2;
    if (row + 1 < sevenfold_board_rows &&
        overview_quality_kind(row + 1, column) != kind) mask |= 4;
    if (column > 0 && overview_quality_kind(row, column - 1) != kind) mask |= 8;
    return mask;
}

void draw_overview_ground_v2(
    const ElevatedCamera& camera, bool rich_terrain
) {
    constexpr int tile = 46;
    constexpr int left = -sevenfold_board_columns * tile / 2;
    constexpr int far = -sevenfold_board_rows * tile / 2;
    OverviewTileOrder order[sevenfold_board_rows * sevenfold_board_columns]{};
    int count = 0;
    for (int row = 0; row < sevenfold_board_rows; ++row) {
        for (int column = 0; column < sevenfold_board_columns; ++column) {
            const int x = left + column * tile + tile / 2;
            const int z = far + row * tile + tile / 2;
            order[count++] = {row, column, elevated_raw_y(x, 0, z, camera)};
        }
    }
    std::sort(order, order + count,
              [](const OverviewTileOrder& a, const OverviewTileOrder& b) {
                  return a.depth < b.depth;
              });
    for (int index = 0; index < count; ++index) {
        const int row = order[index].row;
        const int column = order[index].column;
        const int x0 = left + column * tile;
        const int z0 = far + row * tile;
        const int kind = overview_kind(row, column);
        if (rich_terrain) {
            const int quality_kind = overview_quality_kind(row, column);
            draw_quality_terrain_tile(
                x0, z0, x0 + tile, z0 + tile, quality_kind,
                (row * 5 + column * 3) & 3,
                overview_transition_mask(row, column, quality_kind), camera
            );
            draw_quality_cell_boundary(
                x0, z0, x0 + tile, z0 + tile, camera
            );
        } else if (kind == 3) {
            draw_elevated_solid_quad(
                x0, z0, x0 + tile, z0 + tile, 0, camera,
                colour15(overview_terrain_colours[kind])
            );
        } else {
            const int cell = kind == 0 ? terrain_a_cell
                : (kind == 1 ? terrain_b_cell : 2);
            const int clut = kind == 0 ? terrain_a_clut
                : (kind == 1 ? terrain_b_clut : 3);
            draw_elevated_cell_quad(
                x0, z0, x0 + tile, z0 + tile, 0, camera, cell, clut
            );
        }
    }
}

struct QualityProp final {
    int x;
    int z;
    int kind;
    int size;
    std::int32_t depth;
};

void draw_quality_prop(const QualityProp& prop, const ElevatedCamera& camera) {
    if (prop.kind == 0) draw_quality_tree(prop.x, prop.z, prop.size, camera);
    else if (prop.kind == 1) draw_quality_rock(prop.x, prop.z, prop.size, camera);
    else draw_quality_grass(prop.x, prop.z, prop.size, camera);
}

void make_overview_props(QualityProp* props, const ElevatedCamera& camera) {
    constexpr int definitions[9][4] = {
        {-164, -116, 0, 20}, {-120, -108, 0, 25}, {-78, -104, 0, 18},
        {118, -112, 1, 13}, {162, -70, 1, 16}, {-160, 112, 1, 12},
        {-115, 105, 1, 10}, {115, 88, 2, 11}, {158, 104, 2, 9},
    };
    for (int index = 0; index < 9; ++index) {
        props[index] = {
            definitions[index][0], definitions[index][1], definitions[index][2],
            definitions[index][3], elevated_raw_y(
                definitions[index][0], 0, definitions[index][1], camera
            ),
        };
    }
    std::sort(props, props + 9,
              [](const QualityProp& a, const QualityProp& b) {
                  return a.depth < b.depth;
              });
}

void draw_movement_highlights_v2(const ElevatedCamera& camera) {
    constexpr int tile = 46;
    constexpr int left = -sevenfold_board_columns * tile / 2;
    constexpr int far = -sevenfold_board_rows * tile / 2;
    constexpr int cells[][2] = {{3, 0}, {2, 0}, {4, 0}, {3, 1}, {2, 1}};
    for (int index = 0; index < 5; ++index) {
        const int row = cells[index][0];
        const int column = cells[index][1];
        const int x0 = left + column * tile + 5;
        const int z0 = far + row * tile + 5;
        const std::uint16_t colour = index == 0
            ? gpu::rgb15(10, 24, 31) : gpu::rgb15(6, 17, 24);
        draw_elevated_solid_quad(
            x0, z0, x0 + tile - 10, z0 + tile - 10, 2, camera, colour
        );
    }
}

[[nodiscard]] SceneResult draw_overview_v2(
    int move_step, bool rich_terrain = true
) {
    constexpr int tile = 46;
    constexpr int left = -sevenfold_board_columns * tile / 2;
    constexpr int far = -sevenfold_board_rows * tile / 2;
    constexpr int unit_count = static_cast<int>(
        sizeof(quality_overview_units) / sizeof(quality_overview_units[0])
    );
    const ElevatedCamera camera = overview_camera_v2();
    OverviewUnitOrder order[unit_count]{};
    for (int index = 0; index < unit_count; ++index) {
        const OverviewUnit& unit = quality_overview_units[index];
        const int x = left + unit.column * tile + tile / 2;
        int z = far + unit.row * tile + tile / 2;
        if (move_step >= 0 && index == 4) {
            const int frame = std::min(move_step, quality_movement_frames - 1);
            z += quality_move_progress[frame];
        }
        order[index] = {index, x, z, elevated_raw_y(x, 0, z, camera)};
    }
    std::sort(order, order + unit_count,
              [](const OverviewUnitOrder& a, const OverviewUnitOrder& b) {
                  return a.depth < b.depth;
              });

    gpu::clear(gpu::rgb15(4, 8, 11));
    psx::start_clock();
    draw_overview_ground_v2(camera, rich_terrain);
    draw_movement_highlights_v2(camera);
    QualityProp props[9]{};
    int next_prop = 0;
    if (rich_terrain) make_overview_props(props, camera);
    const int unit_size = elevated_billboard_size(80, camera);
    for (int sorted = 0; sorted < unit_count; ++sorted) {
        const OverviewUnitOrder& placed = order[sorted];
        while (rich_terrain && next_prop < 9 &&
               props[next_prop].depth <= placed.depth) {
            draw_quality_prop(props[next_prop++], camera);
        }
        const OverviewUnit& unit = quality_overview_units[placed.index];
        const bool moving = move_step >= 0 && placed.index == 4;
        draw_elevated_marker(placed.x, placed.z, unit.team, 17, camera);
        const ProjectedVertex foot =
            project_elevated(placed.x, 3, placed.z, camera);
        const Cutout& art = moving
            ? ((move_step & 1) == 0
                ? tactical_cutouts[8] : tactical_cutouts[9])
            : *unit.cutout;
        draw_cutout(
            foot.x, foot.y + unit_size * 2 / art.source_size, unit_size, art
        );
        draw_unit_status(
            foot, unit_size, unit.team, 92 - placed.index * 5,
            placed.index == 4
        );
    }
    while (rich_terrain && next_prop < 9) {
        draw_quality_prop(props[next_prop++], camera);
    }
    gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 5, move_step >= 0
        ? "TACTICAL CAMERA V2 MOVE" : "TACTICAL CAMERA V2");
    gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 227, "HIGH ISO  8 UNITS  REACH  HEALTH  TEAM");
    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(), rich_terrain ? 104 : 86};
}

constexpr int comparison_tile = 40;
constexpr int comparison_grass = 0;
constexpr int comparison_forest = 1;
constexpr int comparison_paved = 2;

[[nodiscard]] int comparison_kind(int row, int column) noexcept {
    return grandleon_psx_comparison_surroundings[
        row * grandleon_psx_comparison_surrounding_width + column
    ];
}

[[nodiscard]] ElevatedCamera comparison_board_camera(int focus = 0) noexcept {
    // The steeper basis mirrors the schema-2 Godot camera: the complete 12x10
    // diorama reads from above and fills the native frame without title bands.
    ElevatedCamera camera{0, 0, 220, -220, 150, 150, 270, 256};
    RawBounds bounds{0, 0, 0, 0, false};
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int right = left + grandleon_psx_comparison_surrounding_width *
                          comparison_tile;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    constexpr int near = far + grandleon_psx_comparison_surrounding_height *
                         comparison_tile;
    include_world(bounds, left, 0, far, camera);
    include_world(bounds, right, 0, far, camera);
    include_world(bounds, left, 0, near, camera);
    include_world(bounds, right, 0, near, camera);
    camera = fit_elevated_camera(
        camera, bounds,
        grandleon_psx_comparison_safe_margin[0],
        grandleon_psx_comparison_safe_margin[1],
        gpu::screen_width - grandleon_psx_comparison_safe_margin[0],
        gpu::screen_height - grandleon_psx_comparison_safe_margin[1]
    );
    if (focus > 0) {
        const int step = std::min(focus, 3);
        camera.zoom = camera.zoom * (100 + step * 7) / 100;
    }
    return camera;
}

[[nodiscard]] int comparison_transition_mask(
    int row, int column, int kind
) noexcept {
    if (kind == comparison_grass) return 0;
    int mask = 0;
    if (row > 0 && comparison_kind(row - 1, column) != kind) mask |= 1;
    if (column + 1 < grandleon_psx_comparison_surrounding_width &&
        comparison_kind(row, column + 1) != kind) mask |= 2;
    if (row + 1 < grandleon_psx_comparison_surrounding_height &&
        comparison_kind(row + 1, column) != kind) mask |= 4;
    if (column > 0 && comparison_kind(row, column - 1) != kind) mask |= 8;
    return mask;
}

[[nodiscard]] bool comparison_active_cell(int row, int column) noexcept {
    return column >= grandleon_psx_comparison_active_origin_x &&
           column < grandleon_psx_comparison_active_origin_x +
                    grandleon_psx_comparison_board_width &&
           row >= grandleon_psx_comparison_active_origin_y &&
           row < grandleon_psx_comparison_active_origin_y +
                 grandleon_psx_comparison_board_height;
}

void draw_comparison_marker(
    int row, int column, std::uint16_t colour, const ElevatedCamera& camera
) {
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    const int x0 = left + column * comparison_tile + 4;
    const int z0 = far + row * comparison_tile + 4;
    const int x1 = x0 + comparison_tile - 8;
    const int z1 = z0 + comparison_tile - 8;
    draw_elevated_solid_quad(x0, z0, x1, z0 + 3, 3, camera, colour);
    draw_elevated_solid_quad(x0, z1 - 3, x1, z1, 3, camera, colour);
    draw_elevated_solid_quad(x0, z0, x0 + 3, z1, 3, camera, colour);
    draw_elevated_solid_quad(x1 - 3, z0, x1, z1, 3, camera, colour);
}

int make_comparison_props(QualityProp* props, const ElevatedCamera& camera) {
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    int count = 0;
    for (int row = 0; row < grandleon_psx_comparison_surrounding_height;
         ++row) {
        for (int column = 0;
             column < grandleon_psx_comparison_surrounding_width; ++column) {
            const int kind = comparison_kind(row, column);
            const bool active = comparison_active_cell(row, column);
            const int centre_x = left + column * comparison_tile +
                                 comparison_tile / 2;
            const int centre_z = far + row * comparison_tile +
                                 comparison_tile / 2;
            if (kind == comparison_forest) {
                const short* offsets = active
                    ? grandleon_psx_comparison_active_tree_xz_milli
                    : grandleon_psx_comparison_surrounding_tree_xz_milli;
                const unsigned short* scales = active
                    ? grandleon_psx_comparison_active_tree_scale_milli
                    : grandleon_psx_comparison_surrounding_tree_scale_milli;
                for (int tree = 0; tree < 2; ++tree) {
                    const int x = centre_x +
                        offsets[tree * 2] * comparison_tile / 1000;
                    const int z = centre_z +
                        offsets[tree * 2 + 1] * comparison_tile / 1000;
                    const int variation =
                        grandleon_psx_comparison_cluster_height_milli[0] +
                        ((row * 17 + column * 11 + tree * 23) % 401);
                    const int size = comparison_tile * 2 * scales[tree] *
                        variation / 3000000;
                    props[count++] = {
                        x, z, 0, std::max(12, size),
                        elevated_raw_y(x, 0, z, camera),
                    };
                }
                if (active) {
                    props[count++] = {
                        centre_x, centre_z + 9, 2, 8,
                        elevated_raw_y(centre_x, 0, centre_z + 9, camera),
                    };
                }
            } else if (active && kind == comparison_grass) {
                constexpr int offsets[6][2] = {
                    {-11, -9}, {10, -7}, {-7, 10}, {12, 9}, {-13, 3}, {5, 13},
                };
                for (int detail = 0; detail < 6; ++detail) {
                    const int x = centre_x + offsets[detail][0];
                    const int z = centre_z + offsets[detail][1];
                    props[count++] = {
                        x, z, detail < 4 ? 2 : 1, detail < 4 ? 7 : 5,
                        elevated_raw_y(x, 0, z, camera),
                    };
                }
            }
        }
    }
    std::sort(props, props + count,
              [](const QualityProp& a, const QualityProp& b) {
                  return a.depth < b.depth;
              });
    return count;
}

void draw_comparison_prop(
    const QualityProp& prop, const ElevatedCamera& camera
) {
    if (prop.kind != 0) {
        draw_quality_prop(prop, camera);
        return;
    }
    draw_elevated_solid_quad(
        prop.x - prop.size / 3, prop.z - 2,
        prop.x + prop.size / 3, prop.z + 2, 2, camera,
        gpu::rgb15(2, 5, 3)
    );
    const ProjectedVertex base = project_elevated(prop.x, 2, prop.z, camera);
    const ProjectedVertex trunk =
        project_elevated(prop.x, prop.size * 3 / 4, prop.z, camera);
    const ProjectedVertex crown =
        project_elevated(prop.x, prop.size * 3 / 2, prop.z, camera);
    gpu::fill(base.x - 1, trunk.y, 3, std::max(2, base.y - trunk.y),
              gpu::rgb15(9, 6, 3));
    const int radius = std::max(3, (base.y - crown.y) / 3);
    const int colour_shift = (prop.x / 7 + prop.z / 11) & 3;
    gpu::draw_triangle(
        crown.x, crown.y - radius,
        crown.x - radius, trunk.y + 1,
        crown.x + radius, trunk.y + 1,
        gpu::rgb15(4, 13 + colour_shift, 7)
    );
    gpu::draw_triangle(
        crown.x, crown.y,
        crown.x - radius * 3 / 4, trunk.y - radius / 2,
        crown.x + radius * 3 / 4, trunk.y - radius / 2,
        gpu::rgb15(7, 19 + colour_shift, 9)
    );
}

void draw_comparison_paved_details(const ElevatedCamera& camera) {
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    const std::uint16_t cyan = gpu::rgb15(5, 20, 23);
    for (int active_row = 0;
         active_row < grandleon_psx_comparison_board_height; ++active_row) {
        for (int active_column = 0;
             active_column < grandleon_psx_comparison_board_width;
             ++active_column) {
            const int row = active_row + grandleon_psx_comparison_active_origin_y;
            const int column = active_column +
                               grandleon_psx_comparison_active_origin_x;
            if (comparison_kind(row, column) != comparison_paved) continue;
            const int x0 = left + column * comparison_tile;
            const int z0 = far + row * comparison_tile;
            draw_elevated_solid_quad(
                x0 + 8, z0 + 10, x0 + 18, z0 + 12, 2, camera, cyan
            );
            draw_elevated_solid_quad(
                x0 + 24, z0 + 27, x0 + 34, z0 + 29, 2, camera, cyan
            );
        }
    }
}

struct ComparisonUnitOrder final {
    int index;
    int x;
    int z;
    std::int32_t depth;
};

[[nodiscard]] SceneResult draw_comparison_board(
    int focus, int blue_hp, int red_hp, bool rich_terrain = true
) {
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    const ElevatedCamera camera = comparison_board_camera(focus);
    constexpr int cell_count = grandleon_psx_comparison_surrounding_width *
                               grandleon_psx_comparison_surrounding_height;
    OverviewTileOrder order[cell_count]{};
    int count = 0;
    for (int row = 0; row < grandleon_psx_comparison_surrounding_height;
         ++row) {
        for (int column = 0;
             column < grandleon_psx_comparison_surrounding_width; ++column) {
            const int x = left + column * comparison_tile + comparison_tile / 2;
            const int z = far + row * comparison_tile + comparison_tile / 2;
            order[count++] = {row, column, elevated_raw_y(x, 0, z, camera)};
        }
    }
    std::sort(order, order + count,
              [](const OverviewTileOrder& a, const OverviewTileOrder& b) {
                  return a.depth < b.depth;
              });

    gpu::clear(gpu::rgb15(4, 8, 11));
    psx::start_clock();
    for (int index = 0; index < count; ++index) {
        const int row = order[index].row;
        const int column = order[index].column;
        const int x0 = left + column * comparison_tile;
        const int z0 = far + row * comparison_tile;
        const int kind = comparison_kind(row, column);
        if (rich_terrain) {
            draw_quality_terrain_tile(
                x0, z0, x0 + comparison_tile, z0 + comparison_tile, kind,
                (row * 5 + column * 3) & 3,
                comparison_transition_mask(row, column, kind), camera
            );
            if (comparison_active_cell(row, column)) {
                draw_quality_cell_boundary(
                    x0, z0, x0 + comparison_tile, z0 + comparison_tile, camera
                );
            }
        } else {
            const std::uint16_t colours[3] = {
                gpu::rgb15(7, 17, 8), gpu::rgb15(4, 11, 7),
                gpu::rgb15(14, 14, 13),
            };
            draw_elevated_solid_quad(
                x0, z0, x0 + comparison_tile, z0 + comparison_tile,
                0, camera, colours[kind]
            );
        }
    }
    if (rich_terrain) draw_comparison_paved_details(camera);

    // The forest extension frames the active board, but never wins the final
    // painter pass over tactical information. This retains the explicit two
    // trees per forest cell while keeping the compact units and grid legible.
    QualityProp props[240]{};
    const int prop_count = rich_terrain ? make_comparison_props(props, camera) : 0;
    for (int index = 0; index < prop_count; ++index) {
        draw_comparison_prop(props[index], camera);
    }
    if (rich_terrain) {
        for (int row = grandleon_psx_comparison_active_origin_y;
             row < grandleon_psx_comparison_active_origin_y +
                   grandleon_psx_comparison_board_height; ++row) {
            for (int column = grandleon_psx_comparison_active_origin_x;
                 column < grandleon_psx_comparison_active_origin_x +
                          grandleon_psx_comparison_board_width; ++column) {
                const int x0 = left + column * comparison_tile;
                const int z0 = far + row * comparison_tile;
                draw_quality_cell_boundary(
                    x0, z0, x0 + comparison_tile, z0 + comparison_tile, camera
                );
            }
        }
    }

    const int selected_column = grandleon_psx_comparison_active_origin_x +
                                grandleon_psx_comparison_unit_cells[0];
    const int selected_row = grandleon_psx_comparison_active_origin_y +
                             grandleon_psx_comparison_unit_cells[1];
    const int target_column = grandleon_psx_comparison_active_origin_x +
                              grandleon_psx_comparison_unit_cells[2];
    const int target_row = grandleon_psx_comparison_active_origin_y +
                           grandleon_psx_comparison_unit_cells[3];
    draw_comparison_marker(
        selected_row, selected_column,
        grandleon_psx_comparison_marker_colours[0], camera
    );
    draw_comparison_marker(
        target_row, target_column,
        grandleon_psx_comparison_marker_colours[1], camera
    );

    ComparisonUnitOrder units[2]{};
    const int unit_columns[2] = {selected_column, target_column};
    const int unit_rows[2] = {selected_row, target_row};
    for (int index = 0; index < 2; ++index) {
        const int x = left + unit_columns[index] * comparison_tile +
                      comparison_tile / 2;
        const int z = far + unit_rows[index] * comparison_tile +
                      comparison_tile / 2;
        units[index] = {index, x, z, elevated_raw_y(x, 0, z, camera)};
    }
    std::sort(units, units + 2,
              [](const ComparisonUnitOrder& a, const ComparisonUnitOrder& b) {
                  return a.depth < b.depth;
              });

    const int unit_size = elevated_billboard_size(118, camera);
    for (const ComparisonUnitOrder& unit : units) {
        const ProjectedVertex foot = project_elevated(unit.x, 3, unit.z, camera);
        const Cutout& art = unit.index == 0
            ? quality_left_slots.idle : quality_right_slots.idle;
        draw_elevated_marker(unit.x, unit.z, unit.index, 16, camera);
        draw_cutout(
            foot.x, foot.y + unit_size * 2 / art.source_size, unit_size, art
        );
        draw_capture_health(
            foot, unit_size, unit.index == 0 ? blue_hp : red_hp,
            grandleon_psx_comparison_team_colours[unit.index]
        );
    }

    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(), rich_terrain ? cell_count * 2 : 0};
}

void print_benchmark(const char* label, const SceneResult& result) {
    psx::Line ticks;
    ticks.text("BENCH ").text(label).text(" ticks=").decimal(result.ticks)
        .text(" us=")
        .decimal(result.ticks * 1000U / psx::ticks_per_1000_microseconds)
        .text(" textured=")
        .decimal(static_cast<std::uint32_t>(result.textured_triangles))
        .flush();
}

}  // namespace

int main() {
    gpu::begin(gpu::rgb15(6, 10, 14));
    build_font();

    const SevenfoldProxy& medieval_left = sevenfold_proxies_high[0][0][0];
    const SevenfoldProxy& medieval_right = sevenfold_proxies_high[0][1][0];
    const SevenfoldProxy& mythical_left = sevenfold_proxies_high[2][0][2];
    const SevenfoldProxy& mythical_right = sevenfold_proxies_high[2][1][2];

    expect(medieval_left.style == 0 && medieval_left.figure == 0 &&
               medieval_left.archetype == 0,
           "medieval male knight proxy is selected");
    expect(medieval_right.style == 0 && medieval_right.figure == 1 &&
               medieval_right.archetype == 0,
           "medieval female knight proxy is selected");
    expect(mythical_left.style == 2 && mythical_left.figure == 0 &&
               mythical_left.archetype == 2,
           "mythical male mage proxy is selected");
    expect(mythical_right.style == 2 && mythical_right.figure == 1 &&
               mythical_right.archetype == 2,
           "mythical female mage proxy is selected");

    load_godot_cutouts();
    load_quality_terrain();
    const std::uint32_t backdrop_upload_ticks = load_combat_backdrop();
    print_benchmark("combat-backdrop-upload", {backdrop_upload_ticks, 0});
    constexpr int idle_cutout_vram_bytes =
        4 * grandleon_psx_godot_closeup_width *
        grandleon_psx_godot_closeup_height / 2;
    constexpr int movement_cutout_vram_bytes =
        2 * grandleon_psx_godot_closeup_width *
        grandleon_psx_godot_closeup_height / 2;
    constexpr int cutout_vram_bytes =
        idle_cutout_vram_bytes + movement_cutout_vram_bytes;
    constexpr int quality_close_vram_bytes =
        8 * grandleon_psx_godot_quality_close_width *
        grandleon_psx_godot_quality_close_height / 2;
    constexpr int tactical_vram_bytes =
        10 * grandleon_psx_godot_tactical_width *
        grandleon_psx_godot_tactical_height / 2;
    constexpr int quality_close_ci8_estimate =
        8 * grandleon_psx_godot_quality_close_width *
        grandleon_psx_godot_quality_close_height;
    constexpr int quality_terrain_vram_bytes =
        grandleon_psx_quality_terrain_cell_count * gpu::cell_texels *
        gpu::cell_texels / 2;
    constexpr int backdrop_vram_bytes =
        grandleon_psx_backdrop_width * grandleon_psx_backdrop_height +
        grandleon_psx_backdrop_overlay_width *
        grandleon_psx_backdrop_overlay_height + 2 * 256 * 2;
    constexpr int texture_page_bytes =
        gpu::page_halfwords * gpu::page_lines * 2;
    expect(idle_cutout_vram_bytes == 18432,
           "four Godot-derived CI4 cutouts use 18 KiB of VRAM");
    expect(idle_cutout_vram_bytes <= texture_page_bytes,
           "all four close-up cutouts fit one 32 KiB texture page");
    expect(cutout_vram_bytes == 27648 &&
               cutout_vram_bytes <= texture_page_bytes * 2,
           "idle plus two movement poses use 27 KiB across two pages");
    expect(quality_close_vram_bytes == 65536 &&
               quality_close_vram_bytes == texture_page_bytes * 2,
           "eight 128px CI4 close poses fill two 32 KiB pages");
    expect(tactical_vram_bytes == 20480 &&
               tactical_vram_bytes < texture_page_bytes,
           "ten exact 64px tactical poses fit one 32 KiB page");
    expect(quality_close_ci8_estimate == 131072,
           "the equivalent 128px CI8 pose set would require four pages");
    expect(quality_terrain_vram_bytes == 22528,
           "44 deterministic CI4 terrain cells use 22 KiB of one page");
    expect(backdrop_vram_bytes <= 3 * 32768,
           "CI8 distant plate, modular props and CLUTs fit three pages");
    expect(backdrop_upload_ticks > 0,
           "combat backdrop upload transition is measured");
    expect(grandleon_psx_backdrop_dominant_terrain_id == 2 &&
               grandleon_psx_backdrop_secondary_terrain_id == 9 &&
               grandleon_psx_backdrop_transition_terrain_id == 12,
           "backdrop keeps the contract forest grass paved terrain IDs");
    expect(grandleon_psx_backdrop_height == 160,
           "backdrop meets the live floor in the 144 to 160 pixel target");
    const ElevatedCamera capture_close = comparison_close_camera();
    const ProjectedVertex capture_left =
        project_elevated(-84, 3, 0, capture_close);
    const ProjectedVertex capture_right =
        project_elevated(84, 3, 0, capture_close);
    const int expected_left = gpu::screen_width *
        grandleon_psx_comparison_close_layout_milli[0] / 1000;
    const int expected_right = gpu::screen_width *
        grandleon_psx_comparison_close_layout_milli[1] / 1000;
    const int expected_ground = gpu::screen_height *
        grandleon_psx_comparison_close_layout_milli[2] / 1000;
    expect(capture_left.x >= expected_left - 2 &&
               capture_left.x <= expected_left + 2 &&
               capture_right.x >= expected_right - 2 &&
               capture_right.x <= expected_right + 2 &&
               capture_left.y == expected_ground &&
               capture_right.y == expected_ground,
           "capture fighters keep contract slots on one ground line");
    expect(gpu::read_back(
               gpu::cell_texture_x(quality_terrain_cell_base),
               gpu::cell_texture_y(quality_terrain_cell_base)) != 0,
           "quality grass texture reached VRAM");

    // Each style keeps the flat proxy and first planar texture pass, then
    // captures both rejected hybrid stages before camera v2. That makes the
    // above-ground projection reviewable rather than merely asserted.
    const SceneResult medieval_flat = draw_scene(
        medieval_left, medieval_right, false, "MEDIEVAL FLAT 3D",
        "BEFORE  SAME IDENTITIES AND CAMERA", gpu::rgb15(7, 13, 17)
    );
    print_benchmark("medieval-flat", medieval_flat);
    psx::signal_host(10);

    load_textures(
        grandleon_playstation_terrain_temperate_grass_0,
        grandleon_playstation_terrain_temperate_grass_clut,
        grandleon_playstation_terrain_temperate_road_0,
        grandleon_playstation_terrain_temperate_road_clut,
        grandleon_playstation_character_medieval_first_knight_blue,
        grandleon_playstation_character_medieval_first_knight_blue_clut,
        grandleon_playstation_character_medieval_second_knight_red,
        grandleon_playstation_character_medieval_second_knight_red_clut
    );
    expect(gpu::read_back(gpu::cell_texture_x(left_cell),
                          gpu::cell_texture_y(left_cell)) != 0,
           "opaque medieval character texture reached VRAM");
    const SceneResult medieval_current = draw_scene(
        medieval_left, medieval_right, true, "MEDIEVAL PLANAR 3D",
        "CURRENT  WHOLE-SPRITE PROJECTION", gpu::rgb15(7, 13, 17)
    );
    print_benchmark("medieval-current", medieval_current);
    psx::signal_host(11);

    load_refined_terrain(false);
    const SceneResult medieval_low_angle = draw_hybrid_close(
        false, "MEDIEVAL LOW ANGLE", "OLD HYBRID  EYE BELOW THE BOOTS",
        gpu::rgb15(7, 13, 17)
    );
    print_benchmark("medieval-low-angle", medieval_low_angle);
    psx::signal_host(12);

    const SceneResult medieval_corrected = draw_corrected_hybrid_close(
        false, "MEDIEVAL SCREEN STAGE", "REJECTED  FLOOR STILL READS FROM BELOW",
        gpu::rgb15(7, 13, 17)
    );
    print_benchmark("medieval-screen-stage-rejected", medieval_corrected);
    psx::signal_host(13);

    const SceneResult medieval_v2_legacy = draw_hybrid_close_v2(
        false, -1, "MEDIEVAL CAMERA V2 96", "MATCHED CI4 RESOLUTION BASELINE",
        gpu::rgb15(7, 13, 17), false, false
    );
    print_benchmark("medieval-camera-v2-96", medieval_v2_legacy);
    psx::signal_host(27);

    load_quality_close_cutouts(false);
    const SceneResult medieval_terrain_before = draw_hybrid_close_v2(
        false, -1, "MEDIEVAL TERRAIN BEFORE",
        "SAME 128 ART  LOW-FREQUENCY FLOOR",
        gpu::rgb15(7, 13, 17), true, false
    );
    print_benchmark("medieval-terrain-before", medieval_terrain_before);
    psx::signal_host(30);

    const SceneResult medieval_v2 = draw_hybrid_close_v2(
        false, -1, "MEDIEVAL TERRAIN AFTER", "FOREST  GRASS  STONE  CLUSTERS",
        gpu::rgb15(7, 13, 17)
    );
    print_benchmark("medieval-camera-v2", medieval_v2);
    psx::signal_host(22);

    const SceneResult mythical_flat = draw_scene(
        mythical_left, mythical_right, false, "MYTHICAL FLAT 3D",
        "BEFORE  SAME IDENTITIES AND CAMERA", gpu::rgb15(12, 8, 17)
    );
    print_benchmark("mythical-flat", mythical_flat);
    psx::signal_host(14);

    load_textures(
        grandleon_playstation_terrain_autumn_grass_1,
        grandleon_playstation_terrain_autumn_grass_clut,
        grandleon_playstation_terrain_autumn_ruins_0,
        grandleon_playstation_terrain_autumn_ruins_clut,
        grandleon_playstation_character_mythical_first_mage_blue,
        grandleon_playstation_character_mythical_first_mage_blue_clut,
        grandleon_playstation_character_mythical_second_mage_red,
        grandleon_playstation_character_mythical_second_mage_red_clut
    );
    expect(gpu::read_back(gpu::cell_texture_x(right_cell),
                          gpu::cell_texture_y(right_cell)) != 0,
           "opaque mythical character texture reached VRAM");
    const SceneResult mythical_current = draw_scene(
        mythical_left, mythical_right, true, "MYTHICAL PLANAR 3D",
        "CURRENT  WHOLE-SPRITE PROJECTION", gpu::rgb15(12, 8, 17)
    );
    print_benchmark("mythical-current", mythical_current);
    psx::signal_host(15);

    load_refined_terrain(true);
    const SceneResult mythical_low_angle = draw_hybrid_close(
        true, "MYTHICAL LOW ANGLE", "OLD HYBRID  EYE BELOW THE BOOTS",
        gpu::rgb15(12, 8, 17)
    );
    print_benchmark("mythical-low-angle", mythical_low_angle);
    psx::signal_host(16);

    const SceneResult mythical_corrected = draw_corrected_hybrid_close(
        true, "MYTHICAL SCREEN STAGE", "REJECTED  FLOOR STILL READS FROM BELOW",
        gpu::rgb15(12, 8, 17)
    );
    print_benchmark("mythical-screen-stage-rejected", mythical_corrected);
    psx::signal_host(17);

    load_quality_close_cutouts(true);
    const SceneResult mythical_v2 = draw_hybrid_close_v2(
        true, -1, "MYTHICAL QUALITY 128", "EXACT GODOT ART  FIT FROM BOUNDS",
        gpu::rgb15(12, 8, 17)
    );
    print_benchmark("mythical-camera-v2", mythical_v2);
    psx::signal_host(23);

    const SceneResult overview_flat = draw_overview(false);
    print_benchmark("overview-flat", overview_flat);
    psx::signal_host(18);

    load_refined_terrain(false);
    const SceneResult overview_hybrid = draw_overview(true);
    print_benchmark("overview-hybrid", overview_hybrid);
    psx::signal_host(19);

    load_tactical_cutouts();
    const SceneResult overview_terrain_before = draw_overview_v2(-1, false);
    print_benchmark("overview-terrain-before", overview_terrain_before);
    psx::signal_host(31);

    const SceneResult overview_v2 = draw_overview_v2(-1);
    print_benchmark("overview-camera-v2", overview_v2);
    psx::signal_host(24);

    SceneResult movement_frames[8]{};
    std::uint32_t slowest_movement = 0;
    for (int frame = 0; frame < 8; ++frame) {
        movement_frames[frame] = draw_overview_frame(true, frame);
        if (movement_frames[frame].ticks > slowest_movement) {
            slowest_movement = movement_frames[frame].ticks;
        }
        psx::signal_host(21);
    }
    print_benchmark("overview-move-slowest", {slowest_movement, 104});

    SceneResult movement_v2_frames[quality_movement_frames]{};
    std::uint32_t slowest_movement_v2 = 0;
    for (int frame = 0; frame < quality_movement_frames; ++frame) {
        movement_v2_frames[frame] = draw_overview_v2(frame);
        if (movement_v2_frames[frame].ticks > slowest_movement_v2) {
            slowest_movement_v2 = movement_v2_frames[frame].ticks;
        }
        psx::signal_host(25);
    }
    print_benchmark("overview-camera-v2-move-slowest",
                    {slowest_movement_v2, 104});

    load_quality_close_cutouts(false);
    const SceneResult backdrop_before = draw_hybrid_close_v2(
        false, -1, "BACKDROP BEFORE", "LIVE TERRAIN ONLY",
        gpu::rgb15(7, 13, 17), true, true, true, false,
        grandleon_psx_comparison_unit_initial_hp[0],
        grandleon_psx_comparison_unit_initial_hp[1]
    );
    print_benchmark("combat-backdrop-before", backdrop_before);
    psx::signal_host(32);
    const SceneResult backdrop_after = draw_hybrid_close_v2(
        false, -1, "BACKDROP AFTER", "GODOT PLATE + LIVE FLOOR",
        gpu::rgb15(7, 13, 17), true, true, true, true,
        grandleon_psx_comparison_unit_initial_hp[0],
        grandleon_psx_comparison_unit_initial_hp[1]
    );
    print_benchmark("combat-backdrop-after", backdrop_after);
    psx::signal_host(33);
    SceneResult exchange_frames[close_exchange_frames]{};
    std::uint32_t slowest_exchange = 0;
    for (int frame = 0; frame < close_exchange_frames; ++frame) {
        exchange_frames[frame] = draw_hybrid_close_v2(
            false, frame, "CAMERA V2 ATTACK + COUNTER",
            "FIXED SIDES  WORLD EFFECT  TILE-TOP FEET",
            gpu::rgb15(7, 13, 17)
        );
        if (exchange_frames[frame].ticks > slowest_exchange) {
            slowest_exchange = exchange_frames[frame].ticks;
        }
        psx::signal_host(26);
    }
    print_benchmark("camera-v2-exchange-slowest", {slowest_exchange, 72});

    // Deterministic README comparison sequence: the schema-2 contract's exact
    // 6x4 active cells inside its explicit 12x10 forest extension, compact
    // capture HUD, fixed close slots and authoritative health result.
    SceneResult sequence_frames[52]{};
    int sequence_at = 0;
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_comparison_board(
            0, grandleon_psx_comparison_unit_initial_hp[0],
            grandleon_psx_comparison_unit_initial_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_comparison_board(
            frame + 1, grandleon_psx_comparison_unit_initial_hp[0],
            grandleon_psx_comparison_unit_initial_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_hybrid_close_v2(
            false, -1, "MATCH CLOSE  TARGET 8",
            "BLUE 12  RED 11  FIXED SIDES", gpu::rgb15(7, 13, 17),
            true, true, true, true,
            grandleon_psx_comparison_unit_initial_hp[0],
            grandleon_psx_comparison_unit_initial_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < close_exchange_frames; ++frame) {
        const int left_hp = frame >= 32
            ? grandleon_psx_comparison_unit_final_hp[0]
            : grandleon_psx_comparison_unit_initial_hp[0];
        const int right_hp = frame >= 12
            ? grandleon_psx_comparison_unit_final_hp[1]
            : grandleon_psx_comparison_unit_initial_hp[1];
        sequence_frames[sequence_at++] = draw_hybrid_close_v2(
            false, frame, "MATCH CLOSE  TARGET 8",
            frame < 20 ? "BLUE HITS 3  RED 11 TO 8"
                       : "RED COUNTERS 4  BLUE 12 TO 8",
            gpu::rgb15(7, 13, 17), true, true, true, true,
            left_hp, right_hp
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_hybrid_close_v2(
            false, -1, "MATCH CLOSE  TARGET 8",
            "RETURN  BLUE 8  RED 8", gpu::rgb15(7, 13, 17),
            true, true, true, true,
            grandleon_psx_comparison_unit_final_hp[0],
            grandleon_psx_comparison_unit_final_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_comparison_board(
            2 - frame, grandleon_psx_comparison_unit_final_hp[0],
            grandleon_psx_comparison_unit_final_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_comparison_board(
            0, grandleon_psx_comparison_unit_final_hp[0],
            grandleon_psx_comparison_unit_final_hp[1]
        );
        psx::signal_host(29);
    }
    expect(sequence_at == 52,
           "matched README sequence emits all 52 timed landmarks");
    std::uint32_t slowest_sequence = 0;
    for (const SceneResult& frame : sequence_frames) {
        if (frame.ticks > slowest_sequence) slowest_sequence = frame.ticks;
    }
    print_benchmark("schema2-full-map", sequence_frames[0]);
    print_benchmark("schema2-sequence-slowest", {slowest_sequence, 0});

    const SceneResult measured[] = {
        medieval_flat, medieval_current, medieval_low_angle,
        medieval_corrected, medieval_v2_legacy, medieval_terrain_before,
        medieval_v2,
        mythical_flat, mythical_current,
        mythical_low_angle, mythical_corrected, mythical_v2, overview_flat,
        overview_hybrid, overview_terrain_before, overview_v2,
        backdrop_before, backdrop_after,
    };
    for (const SceneResult& scene : measured) {
        expect(scene.ticks > 0 && scene.ticks <= frame_budget_ticks,
               "captured comparison stays inside locked 30 fps budget");
    }
    for (const SceneResult& scene : movement_frames) {
        expect(scene.ticks > 0 && scene.ticks <= frame_budget_ticks,
               "animated hybrid overview stays inside locked 30 fps budget");
    }
    for (const SceneResult& scene : movement_v2_frames) {
        expect(scene.ticks > 0 && scene.ticks <= frame_budget_ticks,
               "camera-v2 tactical movement stays inside locked 30 fps budget");
    }
    for (const SceneResult& scene : exchange_frames) {
        expect(scene.ticks > 0 && scene.ticks <= frame_budget_ticks,
               "camera-v2 attack exchange stays inside locked 30 fps budget");
    }
    for (const SceneResult& scene : sequence_frames) {
        expect(scene.ticks > 0 && scene.ticks <= frame_budget_ticks,
               "matched README sequence stays inside locked 30 fps budget");
    }
    expect(medieval_current.textured_triangles >= 100,
           "current medieval pair preserves its planar texture coverage");
    expect(mythical_current.textured_triangles >= 100,
           "current mythical pair preserves its planar texture coverage");
    expect(medieval_low_angle.textured_triangles == 82 &&
               mythical_low_angle.textured_triangles == 82,
           "old hybrid captures retain their low-angle terrain treatment");
    expect(medieval_corrected.textured_triangles == 14 &&
               mythical_corrected.textured_triangles == 14,
           "rejected screen stages remain available for comparison");
    expect(medieval_v2.textured_triangles == 72 &&
               mythical_v2.textured_triangles == 72,
           "camera-v2 close views use 35 tile tops and two CI4 cutouts");
    expect(overview_hybrid.textured_triangles == 104,
           "hybrid overview uses textured macro terrain and eight cutouts");
    expect(overview_terrain_before.textured_triangles == 86 &&
               overview_v2.textured_triangles == 104,
           "quality overview textures every tile and adds ordered props");
    expect(quality_terrain_cell(1, 0, 0) !=
               quality_terrain_cell(1, 0, 2),
           "forest transition masks select distinct offline textures");

    const ElevatedCamera close_v2 = close_camera_v2();
    const ProjectedVertex far_top = project_elevated(0, 0, -96, close_v2);
    const ProjectedVertex near_top = project_elevated(0, 0, 144, close_v2);
    const ProjectedVertex left_foot = project_elevated(-70, 0, 0, close_v2);
    const ProjectedVertex right_foot = project_elevated(70, 0, 0, close_v2);
    const ProjectedVertex left_head = project_elevated(-70, 100, 0, close_v2);
    expect(near_top.y - far_top.y >= 80,
           "camera-v2 close stage exposes substantial tile-top depth");
    expect(left_head.y < left_foot.y,
           "camera-v2 world up projects above the shared ground line");
    expect(left_foot.y == right_foot.y,
           "camera-v2 close slots share one projected foot line");
    for (int phase = 0; phase < close_exchange_frames; ++phase) {
        expect(close_left_x(phase) < 0 && close_right_x(phase) > 0 &&
                   close_left_x(phase) < close_right_x(phase),
               "attacker and defender retain screen sides through counter");
    }

    psx::Line result;
    result.text(failures == 0 ? "RESULT PASS " : "RESULT FAIL ")
        .decimal(static_cast<std::uint32_t>(checks - failures))
        .text("/").decimal(static_cast<std::uint32_t>(checks)).flush();
    psx::signal_host(20);
    return failures == 0 ? 0 : 1;
}
