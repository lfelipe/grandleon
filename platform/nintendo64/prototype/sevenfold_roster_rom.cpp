// SPDX-License-Identifier: MIT
// Complete Sevenfold low-poly roster through the pinned Tiny3D path.
//
// This ROM is the 3D counterpart to games/sevenfold. It uses the same seven
// terrain layouts and renders all eight roles in both body variants for every
// style. It is deliberately a rendering and budget gate first; the supported
// campaign client remains the sprite ROM while this path proves the expanded
// model vocabulary on the console.

#include <libdragon.h>

#include <t3d/t3d.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "n64_showcase.h"
#include "sevenfold_roster_proxies.h"

namespace {

constexpr int cell_world = 64;
constexpr int board_triangles =
    sevenfold_board_columns * sevenfold_board_rows * 2;
constexpr int board_vertices = board_triangles * 3;
constexpr int packed_board_vertices = (board_vertices + 1) / 2;
constexpr int max_proxy_vertices = sevenfold_high_max_triangles * 3;
constexpr int packed_proxy_vertices = (max_proxy_vertices + 1) / 2;
constexpr int figure_count = 8;
constexpr int batch_vertices = 66;
constexpr int batch_triangles = batch_vertices / 3;
constexpr std::uint64_t frame_budget_us = 33'333;

constexpr std::uint8_t terrain_colours[13][3] = {
    {49, 130, 164}, {168, 135, 83}, {39, 97, 58}, {117, 105, 93},
    {212, 184, 107}, {220, 235, 238}, {101, 115, 59}, {98, 168, 82},
    {148, 135, 121}, {77, 145, 71}, {125, 100, 64}, {131, 194, 104},
    {91, 81, 69},
};
constexpr std::uint8_t terrain_sky_colours[13][3] = {
    {29, 68, 94}, {57, 52, 47}, {24, 54, 38}, {58, 58, 70},
    {96, 73, 46}, {83, 100, 118}, {45, 52, 35}, {52, 79, 55},
    {62, 57, 67}, {37, 72, 51}, {75, 62, 40}, {32, 76, 48},
    {51, 48, 56},
};
constexpr const char* terrain_names[13] = {
    "WATER", "ROAD", "FOREST", "MOUNTAIN", "SAND", "SNOW", "SWAMP",
    "HILLS", "RUINS", "GRASS", "FARMLAND", "BAMBOO", "PAVED",
};
constexpr std::uint8_t preferred_attack_terrain[7] = {
    2, 8, 3, 0, 11, 6, 4,
};
constexpr std::uint8_t team_colours[2][3] = {
    {65, 132, 235}, {215, 74, 68},
};
constexpr const char* body_names[2] = {"MALE", "FEMALE"};
constexpr const char* body_slugs[2] = {"male", "female"};
constexpr const char* lod_names[2] = {"LOW", "HIGH"};
constexpr const char* lod_slugs[2] = {"low", "high"};

int checks = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) ++failures;
    std::fprintf(stderr, "%s %s\n", condition ? "ok  " : "FAIL", message);
    std::fflush(stderr);
}

struct Figure final {
    T3DVertPacked* vertices;
    int triangles;
};

void set_vertex(
    T3DVertPacked& packed, bool second, std::int16_t x,
    std::int16_t y, std::int16_t z, std::uint32_t colour
) {
    if (second) {
        packed.posB[0] = x;
        packed.posB[1] = y;
        packed.posB[2] = z;
        packed.rgbaB = colour;
        packed.normB = 0;
        packed.stB[0] = 0;
        packed.stB[1] = 0;
    } else {
        packed.posA[0] = x;
        packed.posA[1] = y;
        packed.posA[2] = z;
        packed.rgbaA = colour;
        packed.normA = 0;
        packed.stA[0] = 0;
        packed.stA[1] = 0;
    }
}

[[nodiscard]] constexpr std::uint32_t rgba(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue
) noexcept {
    return (static_cast<std::uint32_t>(red) << 24) |
        (static_cast<std::uint32_t>(green) << 16) |
        (static_cast<std::uint32_t>(blue) << 8) | 0xFFU;
}

[[nodiscard]] std::uint32_t triangle_colour(
    const SevenfoldProxy& proxy, const SevenfoldProxyTriangle& triangle,
    int team
) noexcept {
    const std::uint8_t* source = triangle.material == 0
        ? team_colours[team]
        : sevenfold_style_materials[proxy.style][triangle.material];
    const std::uint32_t light = 128U + triangle.shade / 2U;
    return rgba(
        static_cast<std::uint8_t>(source[0] * light / 255U),
        static_cast<std::uint8_t>(source[1] * light / 255U),
        static_cast<std::uint8_t>(source[2] * light / 255U)
    );
}

[[nodiscard]] constexpr std::int16_t cell_x(int column) noexcept {
    return static_cast<std::int16_t>(
        (column - sevenfold_board_columns / 2) * cell_world + cell_world / 2
    );
}

[[nodiscard]] constexpr std::int16_t cell_z(int row) noexcept {
    return static_cast<std::int16_t>(
        (row - sevenfold_board_rows / 2) * cell_world + cell_world / 2
    );
}

void build_proxy_at(
    Figure& into, const SevenfoldProxy& proxy, std::int16_t origin_x,
    std::int16_t origin_z, int team, int quarter_turn
) {
    into.triangles = proxy.triangles_count;
    for (int index = 0; index < packed_proxy_vertices; ++index) {
        into.vertices[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int index = 0; index < proxy.triangles_count; ++index) {
        const SevenfoldProxyTriangle& triangle = proxy.triangles[index];
        const std::uint16_t corners[3] = {
            triangle.a, triangle.b, triangle.c,
        };
        const std::uint32_t colour =
            triangle_colour(proxy, triangle, team);
        for (int corner = 0; corner < 3; ++corner) {
            const SevenfoldProxyVertex& source =
                proxy.vertices[corners[corner]];
            std::int16_t x = source.x;
            std::int16_t z = source.z;
            if (quarter_turn < 0) {
                x = source.z;
                z = static_cast<std::int16_t>(-source.x);
            } else if (quarter_turn > 0) {
                x = static_cast<std::int16_t>(-source.z);
                z = source.x;
            }
            set_vertex(
                into.vertices[written / 2], (written & 1) != 0,
                static_cast<std::int16_t>(x + origin_x),
                source.y,
                static_cast<std::int16_t>(z + origin_z),
                colour
            );
            ++written;
        }
    }
}

void build_proxy(
    Figure& into, const SevenfoldProxy& proxy, int column, int row, int team
) {
    build_proxy_at(
        into, proxy, cell_x(column), cell_z(row), team, 0
    );
}

void build_board(T3DVertPacked* into, int stage) {
    for (int index = 0; index < packed_board_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int row = 0; row < sevenfold_board_rows; ++row) {
        for (int column = 0; column < sevenfold_board_columns; ++column) {
            const std::int16_t x0 = static_cast<std::int16_t>(
                (column - sevenfold_board_columns / 2) * cell_world
            );
            const std::int16_t x1 = static_cast<std::int16_t>(x0 + cell_world);
            const std::int16_t z0 = static_cast<std::int16_t>(
                (row - sevenfold_board_rows / 2) * cell_world
            );
            const std::int16_t z1 = static_cast<std::int16_t>(z0 + cell_world);
            const std::uint8_t terrain = sevenfold_stage_terrain[stage]
                [row * sevenfold_board_columns + column];
            const std::uint8_t* rgb = terrain_colours[terrain];
            const std::uint32_t colour = rgba(rgb[0], rgb[1], rgb[2]);
            const std::int16_t points[6][3] = {
                {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1},
                {x0, 0, z0}, {x1, 0, z1}, {x0, 0, z1},
            };
            for (int point = 0; point < 6; ++point) {
                set_vertex(
                    into[written / 2], (written & 1) != 0,
                    points[point][0], points[point][1], points[point][2],
                    colour
                );
                ++written;
            }
        }
    }
}

void scene_triangle(
    T3DVertPacked* into, int& written, const std::int16_t points[3][3],
    std::uint32_t colour
) {
    for (int point = 0; point < 3; ++point) {
        set_vertex(
            into[written / 2], (written & 1) != 0,
            points[point][0], points[point][1], points[point][2], colour
        );
        ++written;
    }
}

void scene_quad(
    T3DVertPacked* into, int& written, const std::int16_t points[4][3],
    std::uint32_t colour
) {
    const std::int16_t first[3][3] = {
        {points[0][0], points[0][1], points[0][2]},
        {points[1][0], points[1][1], points[1][2]},
        {points[2][0], points[2][1], points[2][2]},
    };
    const std::int16_t second[3][3] = {
        {points[0][0], points[0][1], points[0][2]},
        {points[2][0], points[2][1], points[2][2]},
        {points[3][0], points[3][1], points[3][2]},
    };
    scene_triangle(into, written, first, colour);
    scene_triangle(into, written, second, colour);
}

[[nodiscard]] constexpr int terrain_silhouette_height(
    std::uint8_t terrain
) noexcept {
    constexpr int heights[13] = {
        18, 32, 112, 142, 38, 68, 44, 82, 98, 48, 62, 130, 36,
    };
    return heights[terrain];
}

[[nodiscard]] int build_attack_scene(
    T3DVertPacked* into, int stage, int selected_cell
) {
    for (int index = 0; index < packed_board_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;
    const int selected_x = selected_cell % sevenfold_board_columns;
    const int selected_y = selected_cell / sevenfold_board_columns;

    // A six-by-four patch around the combatants. It samples the authored map,
    // so changing the chosen tile changes the scene rather than merely
    // recolouring one generic floor.
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 6; ++column) {
            int source_x = selected_x + column - 3;
            int source_y = selected_y + row - 1;
            if (source_x < 0) source_x = 0;
            if (source_x >= sevenfold_board_columns) {
                source_x = sevenfold_board_columns - 1;
            }
            if (source_y < 0) source_y = 0;
            if (source_y >= sevenfold_board_rows) {
                source_y = sevenfold_board_rows - 1;
            }
            const std::uint8_t terrain = sevenfold_stage_terrain[stage]
                [source_y * sevenfold_board_columns + source_x];
            const std::uint8_t* rgb = terrain_colours[terrain];
            const std::uint32_t colour = rgba(rgb[0], rgb[1], rgb[2]);
            const std::int16_t x0 = static_cast<std::int16_t>(
                (column - 3) * 70
            );
            const std::int16_t x1 = static_cast<std::int16_t>(x0 + 70);
            const std::int16_t z0 = static_cast<std::int16_t>(
                (row - 1) * 70
            );
            const std::int16_t z1 = static_cast<std::int16_t>(z0 + 70);
            const std::int16_t floor[4][3] = {
                {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1}, {x0, 0, z1},
            };
            scene_quad(into, written, floor, colour);
        }
    }

    // The far row becomes a low-poly skyline. Terrain identity changes its
    // height and outline: mountains and hills peak, forest and bamboo get a
    // trunk/canopy profile, while ruins and open ground keep broken walls.
    for (int column = 0; column < sevenfold_board_columns; ++column) {
        const std::uint8_t terrain = sevenfold_stage_terrain[stage]
            [selected_y * sevenfold_board_columns + column];
        const std::uint8_t* rgb = terrain_colours[terrain];
        const std::uint32_t colour = rgba(
            static_cast<std::uint8_t>(rgb[0] * 3U / 4U),
            static_cast<std::uint8_t>(rgb[1] * 3U / 4U),
            static_cast<std::uint8_t>(rgb[2] * 3U / 4U)
        );
        const std::int16_t x0 = static_cast<std::int16_t>(column * 52 - 208);
        const std::int16_t x1 = static_cast<std::int16_t>(x0 + 52);
        const std::int16_t middle = static_cast<std::int16_t>((x0 + x1) / 2);
        const std::int16_t height = static_cast<std::int16_t>(
            terrain_silhouette_height(terrain)
        );
        if (terrain == 3U || terrain == 7U) {
            const std::int16_t peak[3][3] = {
                {x0, 0, 235}, {x1, 0, 235}, {middle, height, 235},
            };
            scene_triangle(into, written, peak, colour);
        } else if (terrain == 2U || terrain == 11U) {
            const std::int16_t trunk[4][3] = {
                {static_cast<std::int16_t>(middle - 5), 0, 235},
                {static_cast<std::int16_t>(middle + 5), 0, 235},
                {static_cast<std::int16_t>(middle + 5),
                 static_cast<std::int16_t>(height * 2 / 3), 235},
                {static_cast<std::int16_t>(middle - 5),
                 static_cast<std::int16_t>(height * 2 / 3), 235},
            };
            scene_quad(into, written, trunk, rgba(67, 51, 32));
            const std::int16_t canopy[3][3] = {
                {x0, static_cast<std::int16_t>(height / 3), 235},
                {x1, static_cast<std::int16_t>(height / 3), 235},
                {middle, height, 235},
            };
            scene_triangle(into, written, canopy, colour);
        } else {
            const std::int16_t wall[4][3] = {
                {x0, 0, 235}, {x1, 0, 235},
                {x1, static_cast<std::int16_t>(height * 3 / 4), 235},
                {x0, height, 235},
            };
            scene_quad(into, written, wall, colour);
        }
    }
    return written / 3;
}

void draw_unindexed(const T3DVertPacked* vertices, int triangles) {
    int drawn = 0;
    while (drawn < triangles) {
        const int remaining = triangles - drawn;
        const int batch = remaining < batch_triangles
            ? remaining : batch_triangles;
        t3d_vert_load(
            vertices + (drawn * 3) / 2, 0,
            static_cast<std::uint32_t>(batch * 3)
        );
        t3d_tri_draw_unindexed(0, static_cast<std::uint32_t>(batch));
        drawn += batch;
    }
}

void set_camera(T3DViewport& viewport) {
    fm_vec3_t eye;
    eye.v[0] = 0.0F;
    eye.v[1] = 540.0F;
    eye.v[2] = -440.0F;
    fm_vec3_t focus;
    focus.v[0] = 0.0F;
    focus.v[1] = 20.0F;
    focus.v[2] = 0.0F;
    fm_vec3_t up;
    up.v[0] = 0.0F;
    up.v[1] = 1.0F;
    up.v[2] = 0.0F;
    t3d_viewport_look_at(&viewport, &eye, &focus, &up);
}

void set_attack_camera(T3DViewport& viewport) {
    fm_vec3_t eye;
    eye.v[0] = 0.0F;
    eye.v[1] = 132.0F;
    eye.v[2] = -300.0F;
    fm_vec3_t focus;
    focus.v[0] = 0.0F;
    focus.v[1] = 58.0F;
    focus.v[2] = 18.0F;
    fm_vec3_t up;
    up.v[0] = 0.0F;
    up.v[1] = 1.0F;
    up.v[2] = 0.0F;
    t3d_viewport_look_at(&viewport, &eye, &focus, &up);
}

std::uint64_t draw_frame(
    surface_t* display, T3DViewport* viewport, T3DMat4FP* matrix,
    const T3DVertPacked* scene, int scene_triangles, const Figure* figures,
    int visible_figures, const char* label, const char* footer,
    const char* controls, color_t clear_colour
) {
    const std::uint64_t started = get_ticks_us();
    rdpq_attach(display, display_get_zbuf());
    t3d_frame_start();
    rdpq_mode_zbuf(true, true);
    t3d_viewport_attach(viewport);
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_screen_clear_color(clear_colour);
    t3d_screen_clear_depth();
    t3d_light_set_count(0);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT | T3D_FLAG_DEPTH
    ));
    t3d_matrix_push(matrix);
    draw_unindexed(scene, scene_triangles);
    for (int index = 0; index < visible_figures; ++index) {
        draw_unindexed(figures[index].vertices, figures[index].triangles);
    }
    t3d_matrix_pop(1);
    t3d_tri_sync();
    rdpq_detach_wait();
    graphics_draw_box(
        display, 0, 196, 320, 44, graphics_make_color(15, 22, 24, 255)
    );
    graphics_set_color(graphics_make_color(245, 234, 210, 255), 0);
    graphics_draw_text(display, 8, 8, label);
    graphics_set_color(graphics_make_color(125, 168, 133, 255), 0);
    graphics_draw_text(display, 8, 204, footer);
    graphics_draw_text(display, 8, 220, controls);
    return get_ticks_us() - started;
}

int triangle_total(
    const Figure* figures, int visible_figures = figure_count,
    int scene_triangles = board_triangles
) {
    int result = scene_triangles;
    for (int index = 0; index < visible_figures; ++index) {
        result += figures[index].triangles;
    }
    return result;
}

std::uint64_t show_page(
    int stage, int body, int lod, bool report, T3DViewport& viewport,
    T3DMat4FP* matrix, T3DVertPacked* board, Figure* figures
) {
    build_board(board, stage);
    constexpr int positions[8][2] = {
        {1, 1}, {3, 1}, {5, 1}, {7, 1},
        {1, 4}, {3, 4}, {5, 4}, {7, 4},
    };
    for (int archetype = 0; archetype < figure_count; ++archetype) {
        const int team = (archetype + stage + body) & 1;
        const SevenfoldProxy& proxy = lod == 0
            ? sevenfold_proxies_low[stage][body][archetype]
            : sevenfold_proxies_high[stage][body][archetype];
        build_proxy(
            figures[archetype], proxy,
            positions[archetype][0], positions[archetype][1], team
        );
    }
    char page_label[64];
    std::snprintf(
        page_label, sizeof page_label, "%s - %s - %s LOD",
        sevenfold_stage_names[stage], body_names[body], lod_names[lod]
    );
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 3; ++frame) {
        surface_t* display = display_get();
        elapsed = draw_frame(
            display, &viewport, matrix, board, board_triangles, figures,
            figure_count, page_label, "8 ROLES - BLUE + RED TEAMS",
            "A LOD  <> STYLE  ^V BODY  START VIEW", RGBA32(20, 25, 35, 255)
        );
        display_show(display);
    }
    if (!report) return elapsed;
    std::fprintf(
        stderr,
        "sevenfold stage=%s body=%s lod=%s models=8 triangles=%d us=%llu fps=%llu depth=on\n",
        sevenfold_style_names[stage], body_slugs[body], lod_slugs[lod],
        triangle_total(figures),
        static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(elapsed > 0 ? 1'000'000 / elapsed : 0)
    );
    std::fprintf(
        stderr, "CHECKPOINT sevenfold-%s-%s-%s\n",
        sevenfold_style_names[stage], body_slugs[body], lod_slugs[lod]
    );
    std::fflush(stderr);
    return elapsed;
}

[[nodiscard]] int preferred_attack_cell(int stage) noexcept {
    const std::uint8_t wanted = preferred_attack_terrain[stage];
    for (int cell = 0;
         cell < sevenfold_board_columns * sevenfold_board_rows; ++cell) {
        if (sevenfold_stage_terrain[stage][cell] == wanted) return cell;
    }
    return sevenfold_board_columns * sevenfold_board_rows / 2;
}

void build_attack_figures(
    Figure* figures, int stage, int archetype, int left_x, int right_x
) {
    build_proxy_at(
        figures[0], sevenfold_proxies_high[stage][0][archetype],
        static_cast<std::int16_t>(left_x), 8, 0, 1
    );
    build_proxy_at(
        figures[1], sevenfold_proxies_high[stage][1][archetype],
        static_cast<std::int16_t>(right_x), 8, 1, -1
    );
}

void attack_labels(
    int stage, int archetype, std::uint8_t terrain,
    char* page_label, std::size_t page_size,
    char* footer, std::size_t footer_size
) {
    std::snprintf(
        page_label, page_size, "%s - %s DUEL",
        sevenfold_stage_names[stage], sevenfold_archetype_names[archetype]
    );
    std::snprintf(
        footer, footer_size, "HIGH LOD DUEL - %s TERRAIN",
        terrain_names[terrain]
    );
}

std::uint64_t show_attack(
    int stage, int archetype, int selected_cell, bool report,
    T3DViewport& viewport, T3DMat4FP* matrix, T3DVertPacked* scene,
    Figure* figures
) {
    const int scene_triangles =
        build_attack_scene(scene, stage, selected_cell);
    build_attack_figures(figures, stage, archetype, -68, 68);
    const std::uint8_t terrain = sevenfold_stage_terrain[stage][selected_cell];
    char page_label[64];
    char footer[64];
    attack_labels(
        stage, archetype, terrain, page_label, sizeof page_label,
        footer, sizeof footer
    );
    const std::uint8_t* sky = terrain_sky_colours[terrain];
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 3; ++frame) {
        surface_t* display = display_get();
        elapsed = draw_frame(
            display, &viewport, matrix, scene, scene_triangles, figures, 2,
            page_label, footer,
            "A ROLE B ATTACK <> STYLE ^V TILE START",
            RGBA32(sky[0], sky[1], sky[2], 255)
        );
        display_show(display);
    }
    if (!report) return elapsed;
    std::fprintf(
        stderr,
        "sevenfold attack stage=%s role=%s terrain=%s models=2 triangles=%d us=%llu fps=%llu depth=on\n",
        sevenfold_style_names[stage], sevenfold_archetype_names[archetype],
        terrain_names[terrain],
        triangle_total(figures, 2, scene_triangles),
        static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(elapsed > 0 ? 1'000'000 / elapsed : 0)
    );
    std::fprintf(
        stderr, "CHECKPOINT sevenfold-attack-%s-%s\n",
        sevenfold_style_names[stage], terrain_names[terrain]
    );
    std::fflush(stderr);
    return elapsed;
}

void draw_damage_callout(surface_t* display, int target, int damage) {
    const int left = target == 0 ? 34 : 222;
    graphics_draw_box(
        display, left, 42, 64, 16, graphics_make_color(15, 22, 24, 255)
    );
    graphics_set_color(graphics_make_color(242, 193, 78, 255), 0);
    char label[16];
    std::snprintf(label, sizeof label, "HIT -%d", damage);
    graphics_draw_text(display, left + 8, 46, label);
}

std::uint64_t play_attack_preview(
    int stage, int archetype, int selected_cell, T3DViewport& viewport,
    T3DMat4FP* matrix, T3DVertPacked* scene, Figure* figures, bool report
) {
    const int scene_triangles =
        build_attack_scene(scene, stage, selected_cell);
    const std::uint8_t terrain = sevenfold_stage_terrain[stage][selected_cell];
    const std::uint8_t* sky = terrain_sky_colours[terrain];
    char page_label[64];
    char footer[64];
    attack_labels(
        stage, archetype, terrain, page_label, sizeof page_label,
        footer, sizeof footer
    );

    // A compact stand-in for the presentation contract: attacker advances,
    // damage is called out, both return to their marks, then the defender
    // counters. It moves only these review models; no combat result is being
    // simulated or invented by the renderer.
    std::uint64_t slowest = 0;
    for (int frame = 0; frame < 96; ++frame) {
        int left_x = -68;
        int right_x = 68;
        int damage_target = -1;
        int damage = 0;
        if (frame >= 12 && frame < 24) {
            left_x = -68 + (frame - 11) * 68 / 12;
        } else if (frame >= 24 && frame < 36) {
            left_x = 0;
            damage_target = 1;
            damage = 4;
        } else if (frame >= 36 && frame < 48) {
            left_x = -(frame - 35) * 68 / 12;
        } else if (frame >= 60 && frame < 72) {
            right_x = 68 - (frame - 59) * 68 / 12;
        } else if (frame >= 72 && frame < 84) {
            right_x = 0;
            damage_target = 0;
            damage = 2;
        } else if (frame >= 84) {
            right_x = (frame - 83) * 68 / 12;
        }
        build_attack_figures(
            figures, stage, archetype, left_x, right_x
        );
        surface_t* display = display_get();
        const std::uint64_t elapsed = draw_frame(
            display, &viewport, matrix, scene, scene_triangles, figures, 2,
            page_label, footer,
            "A ROLE B ATTACK <> STYLE ^V TILE START",
            RGBA32(sky[0], sky[1], sky[2], 255)
        );
        if (damage_target >= 0) {
            draw_damage_callout(display, damage_target, damage);
        }
        display_show(display);
        if (elapsed > slowest) slowest = elapsed;
        if (report && frame == 24) {
            std::fprintf(
                stderr, "CHECKPOINT sevenfold-attack-animation-hit\n"
            );
            std::fflush(stderr);
            wait_ms(1200);
        } else {
            wait_ms(16);
        }
    }
    if (report) {
        std::fprintf(
            stderr,
            "sevenfold attack-animation stage=%s frames=96 max_us=%llu fps=%llu depth=on\n",
            sevenfold_style_names[stage],
            static_cast<unsigned long long>(slowest),
            static_cast<unsigned long long>(
                slowest > 0 ? 1'000'000 / slowest : 0
            )
        );
        std::fflush(stderr);
    }
    return slowest;
}

}  // namespace

extern "C" int main(void) {
    debug_init_isviewer();
    display_init(
        RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE
    );
    rdpq_init();
    joypad_init();
    T3DInitParams params{};
    t3d_init(params);

    bool roster_complete = true;
    int combinations = 0;
    for (int style = 0; style < 7; ++style) {
        for (int figure = 0; figure < 2; ++figure) {
            for (int archetype = 0; archetype < 8; ++archetype) {
                const SevenfoldProxy& proxy =
                    sevenfold_proxies_low[style][figure][archetype];
                roster_complete = roster_complete && proxy.vertices != nullptr &&
                    proxy.triangles != nullptr && proxy.vertices_count > 0 &&
                    proxy.triangles_count > 0 && proxy.style == style &&
                    proxy.figure == figure && proxy.archetype == archetype &&
                    proxy.triangles_count <= sevenfold_low_max_triangles;
                ++combinations;
            }
        }
    }
    expect(
        roster_complete && combinations == 112,
        "all seven styles, eight roles and two body variants are present"
    );

    T3DVertPacked* board = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * packed_board_vertices)
    );
    Figure figures[figure_count]{};
    bool allocated = board != nullptr;
    for (Figure& figure : figures) {
        figure.vertices = static_cast<T3DVertPacked*>(
            malloc_uncached(sizeof(T3DVertPacked) * packed_proxy_vertices)
        );
        allocated = allocated && figure.vertices != nullptr;
    }
    T3DMat4FP* matrix = static_cast<T3DMat4FP*>(
        malloc_uncached(sizeof(T3DMat4FP))
    );
    allocated = allocated && matrix != nullptr;
    expect(
        allocated, "board and eight maximum-size high-LOD model buffers allocate"
    );
    if (!allocated) {
        std::fprintf(stderr, "RESULT FAIL %d/%d\n", checks - failures, checks);
        while (true) wait_ms(1000);
    }

    t3d_mat4fp_identity(matrix);
    T3DViewport viewport = t3d_viewport_create();
    t3d_viewport_set_projection(
        &viewport, T3D_DEG_TO_RAD(50.0F), 16.0F, 1200.0F
    );
    set_camera(viewport);
    T3DViewport attack_viewport = t3d_viewport_create();
    t3d_viewport_set_projection(
        &attack_viewport, T3D_DEG_TO_RAD(45.0F), 16.0F, 900.0F
    );
    set_attack_camera(attack_viewport);
    for (int stage = 0; stage < sevenfold_stage_count; ++stage) {
        for (int body = 0; body < 2; ++body) {
            for (int lod = 0; lod < 2; ++lod) {
                const std::uint64_t elapsed = show_page(
                    stage, body, lod, true, viewport, matrix, board, figures
                );
                char message[112];
                std::snprintf(
                    message, sizeof message,
                    "%s %s %s-LOD roster frame stays at 30 fps",
                    sevenfold_style_names[stage], body_names[body],
                    lod_names[lod]
                );
                expect(elapsed <= frame_budget_us, message);
                wait_ms(1500);
            }
        }
    }
    for (int stage = 0; stage < sevenfold_stage_count; ++stage) {
        const int archetype = stage % 8;
        const std::uint64_t elapsed = show_attack(
            stage, archetype, preferred_attack_cell(stage), true,
            attack_viewport, matrix, board, figures
        );
        char message[112];
        std::snprintf(
            message, sizeof message,
            "%s terrain-aware close attack frame stays at 30 fps",
            sevenfold_style_names[stage]
        );
        expect(elapsed <= frame_budget_us, message);
        wait_ms(1500);
    }
    const std::uint64_t attack_animation_elapsed = play_attack_preview(
        0, 0, preferred_attack_cell(0), attack_viewport,
        matrix, board, figures, true
    );
    expect(
        attack_animation_elapsed <= frame_budget_us,
        "the attack-and-counter preview stays at 30 fps"
    );
    std::fprintf(
        stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
        checks - failures, checks
    );
    std::fflush(stderr);

    int stage = 0;
    int body = 0;
    int lod = 1;
    int archetype = 0;
    int selected_cell = preferred_attack_cell(stage);
    bool attack_mode = false;
    show_page(stage, body, lod, false, viewport, matrix, board, figures);
    while (true) {
        joypad_poll();
        const joypad_buttons_t pressed =
            joypad_get_buttons_pressed(JOYPAD_PORT_1);
        bool changed = false;
        if (pressed.start) {
            attack_mode = !attack_mode;
            if (attack_mode) selected_cell = preferred_attack_cell(stage);
            changed = true;
        }
        if (pressed.d_left) {
            stage = (stage + sevenfold_stage_count - 1) %
                sevenfold_stage_count;
            if (attack_mode) selected_cell = preferred_attack_cell(stage);
            changed = true;
        }
        if (pressed.d_right) {
            stage = (stage + 1) % sevenfold_stage_count;
            if (attack_mode) selected_cell = preferred_attack_cell(stage);
            changed = true;
        }
        if (pressed.d_up || pressed.d_down) {
            if (attack_mode) {
                const int direction = pressed.d_up ? 1 : -1;
                constexpr int cell_count =
                    sevenfold_board_columns * sevenfold_board_rows;
                selected_cell = (selected_cell + direction + cell_count) %
                    cell_count;
            } else {
                body = 1 - body;
            }
            changed = true;
        }
        if (pressed.a) {
            if (attack_mode) {
                archetype = (archetype + 1) % 8;
            } else {
                lod = 1 - lod;
            }
            changed = true;
        }
        if (attack_mode && pressed.b) {
            play_attack_preview(
                stage, archetype, selected_cell, attack_viewport,
                matrix, board, figures, false
            );
            changed = true;
        }
        if (changed) {
            if (attack_mode) {
                show_attack(
                    stage, archetype, selected_cell, false, attack_viewport,
                    matrix, board, figures
                );
            } else {
                show_page(
                    stage, body, lod, false, viewport, matrix, board, figures
                );
            }
        }
        wait_ms(16);
    }
}
