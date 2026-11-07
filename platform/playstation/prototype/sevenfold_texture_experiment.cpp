// SPDX-License-Identifier: MIT
// PlayStation-only texture experiment for Sevenfold's 3D close battle view.
//
// This is deliberately a separate executable from the production campaign.
// It projects the existing 32x32 character paintings onto the existing high
// LOD proxy geometry, using only native 4bpp texture pages and CLUTs. The
// experiment can therefore be judged from native captures and timing before
// its asset contract is allowed to complicate the production renderer.

#include "psx_gpu.h"
#include "psx_runtime.h"

#include <grandleon/view/glyphs.hpp>

#include <algorithm>
#include <cstdint>

#include "playstation.h"
#include "playstation_characters_medieval.h"
#include "playstation_characters_mythical.h"
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

        // The sprite projection is used for cloth, skin and the large body
        // masses. Metal and small props retain their generated flat material,
        // so silhouette-defining weapons do not dissolve into a decal.
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

void draw_ground(const Camera& camera, bool textured) {
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
            if (textured) {
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
    draw_ground(camera, textured);
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

    // The baseline is the same medieval geometry and camera, before the
    // texture experiment. The host captures this slot at native 320x240.
    const SceneResult baseline = draw_scene(
        medieval_left, medieval_right, false, "FLAT BASELINE",
        "SAME HIGH LOD GEOMETRY AND CAMERA", gpu::rgb15(7, 13, 17)
    );
    print_benchmark("baseline", baseline);
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
    const SceneResult medieval = draw_scene(
        medieval_left, medieval_right, true, "MEDIEVAL KNIGHTS",
        "CI4 CHARACTER AND TERRAIN TEXTURES", gpu::rgb15(7, 13, 17)
    );
    print_benchmark("medieval-textured", medieval);
    psx::signal_host(11);

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
    const SceneResult mythical = draw_scene(
        mythical_left, mythical_right, true, "MYTHICAL MAGES",
        "TEAM COLOURS RETAINED ON TEXTURED PROXIES", gpu::rgb15(12, 8, 17)
    );
    print_benchmark("mythical-textured", mythical);
    psx::signal_host(12);

    expect(baseline.ticks > 0 && baseline.ticks <= frame_budget_ticks,
           "flat comparison stays inside locked 30 fps budget");
    expect(medieval.ticks > 0 && medieval.ticks <= frame_budget_ticks,
           "textured medieval close view stays inside locked 30 fps budget");
    expect(mythical.ticks > 0 && mythical.ticks <= frame_budget_ticks,
           "textured mythical close view stays inside locked 30 fps budget");
    expect(medieval.textured_triangles >= 100,
           "medieval pair uses textures across major body surfaces");
    expect(mythical.textured_triangles >= 100,
           "mythical pair uses textures across major body surfaces");

    psx::Line result;
    result.text(failures == 0 ? "RESULT PASS " : "RESULT FAIL ")
        .decimal(static_cast<std::uint32_t>(checks - failures))
        .text("/").decimal(static_cast<std::uint32_t>(checks)).flush();
    psx::signal_host(13);
    return failures == 0 ? 0 : 1;
}
