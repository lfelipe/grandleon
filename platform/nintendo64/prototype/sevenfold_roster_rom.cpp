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
constexpr int max_proxy_vertices = sevenfold_low_max_triangles * 3;
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
constexpr std::uint8_t team_colours[2][3] = {
    {65, 132, 235}, {215, 74, 68},
};
constexpr const char* body_names[2] = {"MALE", "FEMALE"};
constexpr const char* body_slugs[2] = {"male", "female"};

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

void build_proxy(
    Figure& into, const SevenfoldProxy& proxy, int column, int row, int team
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
            set_vertex(
                into.vertices[written / 2], (written & 1) != 0,
                static_cast<std::int16_t>(source.x + cell_x(column)),
                source.y,
                static_cast<std::int16_t>(source.z + cell_z(row)),
                colour
            );
            ++written;
        }
    }
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

std::uint64_t draw_frame(
    surface_t* display, T3DViewport* viewport, T3DMat4FP* matrix,
    const T3DVertPacked* board, const Figure* figures, const char* label,
    int body
) {
    const std::uint64_t started = get_ticks_us();
    rdpq_attach(display, display_get_zbuf());
    t3d_frame_start();
    rdpq_mode_zbuf(true, true);
    t3d_viewport_attach(viewport);
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_screen_clear_color(RGBA32(20, 25, 35, 255));
    t3d_screen_clear_depth();
    t3d_light_set_count(0);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT | T3D_FLAG_DEPTH
    ));
    t3d_matrix_push(matrix);
    draw_unindexed(board, board_triangles);
    for (int index = 0; index < figure_count; ++index) {
        draw_unindexed(figures[index].vertices, figures[index].triangles);
    }
    t3d_matrix_pop(1);
    t3d_tri_sync();
    rdpq_detach_wait();
    graphics_set_color(graphics_make_color(245, 234, 210, 255), 0);
    graphics_draw_text(display, 8, 8, label);
    graphics_set_color(graphics_make_color(125, 168, 133, 255), 0);
    char roster_label[64];
    std::snprintf(
        roster_label, sizeof roster_label,
        "8 ROLES: %s - BLUE + RED TEAMS", body_names[body]
    );
    graphics_draw_text(display, 8, 220, roster_label);
    return get_ticks_us() - started;
}

int triangle_total(const Figure* figures) {
    int result = board_triangles;
    for (int index = 0; index < figure_count; ++index) {
        result += figures[index].triangles;
    }
    return result;
}

std::uint64_t show_page(
    int stage, int body, T3DViewport& viewport, T3DMat4FP* matrix,
    T3DVertPacked* board, Figure* figures
) {
    build_board(board, stage);
    constexpr int positions[8][2] = {
        {1, 1}, {3, 1}, {5, 1}, {7, 1},
        {1, 4}, {3, 4}, {5, 4}, {7, 4},
    };
    for (int archetype = 0; archetype < figure_count; ++archetype) {
        const int team = (archetype + stage + body) & 1;
        build_proxy(
            figures[archetype],
            sevenfold_proxies_low[stage][body][archetype],
            positions[archetype][0], positions[archetype][1], team
        );
    }
    char page_label[64];
    std::snprintf(
        page_label, sizeof page_label, "%s - %s",
        sevenfold_stage_names[stage], body_names[body]
    );
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 4; ++frame) {
        surface_t* display = display_get();
        elapsed = draw_frame(
            display, &viewport, matrix, board, figures,
            page_label, body
        );
        display_show(display);
    }
    std::fprintf(
        stderr,
        "sevenfold stage=%s body=%s models=8 triangles=%d us=%llu fps=%llu depth=on\n",
        sevenfold_style_names[stage], body_slugs[body], triangle_total(figures),
        static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(elapsed > 0 ? 1'000'000 / elapsed : 0)
    );
    std::fprintf(
        stderr, "CHECKPOINT sevenfold-%s-%s\n",
        sevenfold_style_names[stage], body_slugs[body]
    );
    std::fflush(stderr);
    return elapsed;
}

}  // namespace

extern "C" int main(void) {
    debug_init_isviewer();
    display_init(
        RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE
    );
    rdpq_init();
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
    expect(allocated, "board and eight maximum-size model buffers allocate");
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
    for (int stage = 0; stage < sevenfold_stage_count; ++stage) {
        for (int body = 0; body < 2; ++body) {
            const std::uint64_t elapsed =
                show_page(stage, body, viewport, matrix, board, figures);
            char message[96];
            std::snprintf(
                message, sizeof message,
                "%s %s roster frame stays at 30 fps",
                sevenfold_style_names[stage], body_names[body]
            );
            expect(elapsed <= frame_budget_us, message);
            wait_ms(1500);
        }
    }
    std::fprintf(
        stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
        checks - failures, checks
    );
    std::fflush(stderr);

    int stage = 0;
    int body = 0;
    while (true) {
        show_page(stage, body, viewport, matrix, board, figures);
        body = (body + 1) % 2;
        if (body == 0) stage = (stage + 1) % sevenfold_stage_count;
        wait_ms(1800);
    }
}
