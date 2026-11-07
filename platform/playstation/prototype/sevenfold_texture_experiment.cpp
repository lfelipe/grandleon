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

struct Cutout final {
    int page;
    int u;
    int v;
    int clut;
};

constexpr Cutout medieval_male_cutout{
    cutout_texture_page, 0, 0, cutout_medieval_male_clut
};
constexpr Cutout medieval_female_cutout{
    cutout_texture_page, 128, 0, cutout_medieval_female_clut
};
constexpr Cutout mythical_male_cutout{
    cutout_texture_page, 0, 128, cutout_mythical_male_clut
};
constexpr Cutout mythical_female_cutout{
    cutout_texture_page, 128, 128, cutout_mythical_female_clut
};
constexpr Cutout medieval_move_a_cutout{
    cutout_texture_page + 1, 0, 0, cutout_move_a_clut
};
constexpr Cutout medieval_move_b_cutout{
    cutout_texture_page + 1, 128, 0, cutout_move_b_clut
};

struct Camera final {
    int distance;
    int focal;
    int horizon;
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

void upload_cutout(const Cutout& cutout, const unsigned short* pixels) {
    gpu::upload(
        cutout.page * gpu::page_halfwords + cutout.u / 4,
        gpu::texture_page_y + cutout.v,
        grandleon_psx_godot_closeup_width / 4,
        grandleon_psx_godot_closeup_height,
        pixels
    );
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
        cutout.u + grandleon_psx_godot_closeup_width - 1,
        cutout.v + grandleon_psx_godot_closeup_height - 1,
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
    constexpr int idle_cutout_vram_bytes =
        4 * grandleon_psx_godot_closeup_width *
        grandleon_psx_godot_closeup_height / 2;
    constexpr int movement_cutout_vram_bytes =
        2 * grandleon_psx_godot_closeup_width *
        grandleon_psx_godot_closeup_height / 2;
    constexpr int cutout_vram_bytes =
        idle_cutout_vram_bytes + movement_cutout_vram_bytes;
    constexpr int texture_page_bytes =
        gpu::page_halfwords * gpu::page_lines * 2;
    expect(idle_cutout_vram_bytes == 18432,
           "four Godot-derived CI4 cutouts use 18 KiB of VRAM");
    expect(idle_cutout_vram_bytes <= texture_page_bytes,
           "all four close-up cutouts fit one 32 KiB texture page");
    expect(cutout_vram_bytes == 27648 &&
               cutout_vram_bytes <= texture_page_bytes * 2,
           "idle plus two movement poses use 27 KiB across two pages");

    // Each style keeps the flat proxy and first planar texture pass, then
    // captures both the rejected low-angle hybrid and corrected torso-height
    // stage. That makes the camera correction reviewable rather than asserted.
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
        false, "MEDIEVAL CORRECTED", "TORSO EYE LINE  SHARED FEET  FOREGROUND",
        gpu::rgb15(7, 13, 17)
    );
    print_benchmark("medieval-corrected", medieval_corrected);
    psx::signal_host(13);

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
        true, "MYTHICAL CORRECTED", "TORSO EYE LINE  SHARED FEET  FOREGROUND",
        gpu::rgb15(12, 8, 17)
    );
    print_benchmark("mythical-corrected", mythical_corrected);
    psx::signal_host(17);

    const SceneResult overview_flat = draw_overview(false);
    print_benchmark("overview-flat", overview_flat);
    psx::signal_host(18);

    load_refined_terrain(false);
    const SceneResult overview_hybrid = draw_overview(true);
    print_benchmark("overview-hybrid", overview_hybrid);
    psx::signal_host(19);

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

    const SceneResult measured[] = {
        medieval_flat, medieval_current, medieval_low_angle,
        medieval_corrected, mythical_flat, mythical_current,
        mythical_low_angle, mythical_corrected, overview_flat, overview_hybrid,
    };
    for (const SceneResult& scene : measured) {
        expect(scene.ticks > 0 && scene.ticks <= frame_budget_ticks,
               "captured comparison stays inside locked 30 fps budget");
    }
    for (const SceneResult& scene : movement_frames) {
        expect(scene.ticks > 0 && scene.ticks <= frame_budget_ticks,
               "animated hybrid overview stays inside locked 30 fps budget");
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
           "corrected close views use shallow floor and two CI4 cutouts");
    expect(overview_hybrid.textured_triangles == 104,
           "hybrid overview uses textured macro terrain and eight cutouts");

    psx::Line result;
    result.text(failures == 0 ? "RESULT PASS " : "RESULT FAIL ")
        .decimal(static_cast<std::uint32_t>(checks - failures))
        .text("/").decimal(static_cast<std::uint32_t>(checks)).flush();
    psx::signal_host(20);
    return failures == 0 ? 0 : 1;
}
