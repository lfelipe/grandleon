// SPDX-License-Identifier: MIT
// Fixed-camera Sevenfold 2.5D rendering and performance gate.

#include <libdragon.h>

#include <t3d/t3d.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "n64_showcase.h"
#include "sevenfold_backdrop_renderer.h"
#include "sevenfold_cutout_renderer.h"
#include "sevenfold_terrain_renderer.h"

namespace {

namespace texture =
    grandleon::nintendo64::sevenfold_terrain_renderer;
namespace backdrop =
    grandleon::nintendo64::sevenfold_backdrop_renderer;
namespace cutout =
    grandleon::nintendo64::sevenfold_cutout_renderer;

constexpr int cell_world = 64;
constexpr int board_triangles =
    sevenfold_board_columns * sevenfold_board_rows * 2;
constexpr int matched_surrounding_columns = 12;
constexpr int matched_surrounding_rows = 10;
constexpr int matched_active_origin_column = 3;
constexpr int matched_active_origin_row = 3;
constexpr int matched_cell_world = 48;
constexpr int matched_scene_triangles =
    matched_surrounding_columns * matched_surrounding_rows * 2;
constexpr std::uint8_t matched_surrounding_terrain
    [matched_surrounding_rows][matched_surrounding_columns] = {
        {2, 2, 2, 2, 2, 9, 12, 12, 2, 2, 2, 2},
        {2, 2, 2, 2, 2, 9, 12, 12, 2, 2, 2, 2},
        {2, 2, 2, 2, 2, 9, 12, 12, 2, 2, 2, 2},
        {2, 2, 2, 2, 2, 9, 12, 12, 2, 2, 2, 2},
        {2, 2, 2, 2, 9, 9, 12, 12, 2, 2, 2, 2},
        {2, 2, 2, 2, 9, 9, 12, 9, 2, 2, 2, 2},
        {2, 2, 2, 2, 2, 9, 12, 2, 2, 2, 2, 2},
        {2, 2, 2, 2, 2, 9, 12, 2, 2, 2, 2, 2},
        {2, 2, 2, 2, 2, 9, 12, 2, 2, 2, 2, 2},
        {2, 2, 2, 2, 2, 9, 12, 2, 2, 2, 2, 2},
    };
[[nodiscard]] constexpr std::uint32_t matched_layout_hash() noexcept {
    std::uint32_t result = 0x811C9DC5U;
    const int header[] = {
        matched_surrounding_columns, matched_surrounding_rows,
        matched_active_origin_column, matched_active_origin_row,
    };
    for (int value : header) {
        result ^= static_cast<std::uint32_t>(value);
        result *= 0x01000193U;
    }
    for (const auto& row : matched_surrounding_terrain) {
        for (std::uint8_t value : row) {
            result ^= value;
            result *= 0x01000193U;
        }
    }
    return result;
}
static_assert(matched_layout_hash() == 0x66909BD6U);
constexpr int board_vertices =
    (matched_scene_triangles > board_triangles
        ? matched_scene_triangles : board_triangles) * 3;
constexpr int packed_board_vertices = (board_vertices + 1) / 2;
constexpr int batch_vertices = 66;
constexpr int batch_triangles = batch_vertices / 3;
constexpr std::uint64_t frame_budget_us = 33'333;
constexpr backdrop::Descriptor matched_backdrop{
    2U, 9U, 12U, backdrop::TransitionOrientation::north_south, 2031659219U,
};

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

int checks = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) ++failures;
    std::fprintf(stderr, "%s %s\n", condition ? "ok  " : "FAIL", message);
    std::fflush(stderr);
}

void set_vertex(
    T3DVertPacked& packed, bool second, std::int16_t x,
    std::int16_t y, std::int16_t z, std::uint32_t colour,
    std::int16_t s = 0, std::int16_t t = 0
) {
    if (second) {
        packed.posB[0] = x;
        packed.posB[1] = y;
        packed.posB[2] = z;
        packed.rgbaB = colour;
        packed.normB = 0;
        packed.stB[0] = s;
        packed.stB[1] = t;
    } else {
        packed.posA[0] = x;
        packed.posA[1] = y;
        packed.posA[2] = z;
        packed.rgbaA = colour;
        packed.normA = 0;
        packed.stA[0] = s;
        packed.stA[1] = t;
    }
}

[[nodiscard]] constexpr std::uint32_t rgba(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue
) noexcept {
    return (static_cast<std::uint32_t>(red) << 24) |
        (static_cast<std::uint32_t>(green) << 16) |
        (static_cast<std::uint32_t>(blue) << 8) | 0xFFU;
}

[[nodiscard]] constexpr std::uint8_t terrain_texture_region(
    std::uint8_t terrain
) noexcept {
    if (terrain == 2U || terrain == 7U || terrain == 9U || terrain == 11U) {
        return 0;
    }
    if (terrain == 3U || terrain == 8U || terrain == 12U) return 1;
    if (terrain == 1U || terrain == 4U || terrain == 10U) return 2;
    return 3;
}

[[nodiscard]] constexpr std::uint8_t terrain_variation(
    std::uint8_t terrain, int column, int row
) noexcept {
    // Stable per cell: repeated materials vary without temporal shimmer or a
    // runtime random-number dependency.
    return static_cast<std::uint8_t>(
        (terrain * 3U + static_cast<unsigned>(column) * 5U +
         static_cast<unsigned>(row) * 7U +
         static_cast<unsigned>(column * row)) & 3U
    );
}

[[nodiscard]] std::uint32_t terrain_colour(
    const std::uint8_t* rgb, const std::uint8_t* map, int columns, int rows,
    int column, int row
) noexcept {
    const std::uint8_t terrain = map[row * columns + column];
    int neighbours = 0;
    if (column > 0 && map[row * columns + column - 1] == terrain) ++neighbours;
    if (column + 1 < columns &&
        map[row * columns + column + 1] == terrain) ++neighbours;
    if (row > 0 && map[(row - 1) * columns + column] == terrain) ++neighbours;
    if (row + 1 < rows &&
        map[(row + 1) * columns + column] == terrain) ++neighbours;
    // Connected patches lift subtly toward their centre. Edges remain darker,
    // giving neighbouring terrain clusters a transition cue while preserving
    // every tactical cell boundary.
    const unsigned tone = 245U + static_cast<unsigned>(neighbours) * 2U;
    return rgba(
        static_cast<std::uint8_t>(rgb[0] * tone / 255U),
        static_cast<std::uint8_t>(rgb[1] * tone / 255U),
        static_cast<std::uint8_t>(rgb[2] * tone / 255U)
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
            const std::uint32_t colour = terrain_colour(
                rgb, sevenfold_stage_terrain[stage],
                sevenfold_board_columns, sevenfold_board_rows, column, row
            );
            const std::int16_t points[6][3] = {
                {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1},
                {x0, 0, z0}, {x1, 0, z1}, {x0, 0, z1},
            };
            const std::uint8_t region = terrain_texture_region(terrain);
            for (int triangle = 0; triangle < 2; ++triangle) {
                std::int16_t triangle_points[3][3]{};
                for (int corner = 0; corner < 3; ++corner) {
                    const int point = triangle * 3 + corner;
                    for (int axis = 0; axis < 3; ++axis) {
                        triangle_points[corner][axis] = points[point][axis];
                    }
                }
                for (int corner = 0; corner < 3; ++corner) {
                    const texture::UV uv = texture::terrain_uv(
                        triangle_points, corner, region,
                        terrain_variation(terrain, column, row)
                    );
                    const int point = triangle * 3 + corner;
                    set_vertex(
                        into[written / 2], (written & 1) != 0,
                        points[point][0], points[point][1], points[point][2],
                        colour, uv.s, uv.t
                    );
                    ++written;
                }
            }
        }
    }
}

void build_matched_board(T3DVertPacked* into) {
    for (int index = 0; index < packed_board_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int row = 0; row < matched_surrounding_rows; ++row) {
        for (int column = 0; column < matched_surrounding_columns; ++column) {
            const std::int16_t x0 = static_cast<std::int16_t>(
                (column - matched_surrounding_columns / 2) *
                    matched_cell_world
            );
            const std::int16_t x1 = static_cast<std::int16_t>(
                x0 + matched_cell_world
            );
            const std::int16_t z0 = static_cast<std::int16_t>(
                (row - matched_surrounding_rows / 2) * matched_cell_world
            );
            const std::int16_t z1 = static_cast<std::int16_t>(
                z0 + matched_cell_world
            );
            const std::uint8_t terrain_id =
                matched_surrounding_terrain[row][column];
            const std::uint8_t* rgb = terrain_colours[terrain_id];
            const std::uint32_t colour = terrain_colour(
                rgb, &matched_surrounding_terrain[0][0],
                matched_surrounding_columns, matched_surrounding_rows,
                column, row
            );
            const std::int16_t points[6][3] = {
                {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1},
                {x0, 0, z0}, {x1, 0, z1}, {x0, 0, z1},
            };
            const std::uint8_t region = terrain_texture_region(terrain_id);
            for (int triangle = 0; triangle < 2; ++triangle) {
                std::int16_t triangle_points[3][3]{};
                for (int corner = 0; corner < 3; ++corner) {
                    const int point = triangle * 3 + corner;
                    for (int axis = 0; axis < 3; ++axis) {
                        triangle_points[corner][axis] = points[point][axis];
                    }
                }
                for (int corner = 0; corner < 3; ++corner) {
                    const int point = triangle * 3 + corner;
                    const texture::UV uv = texture::terrain_uv(
                        triangle_points, corner, region,
                        terrain_variation(terrain_id, column, row)
                    );
                    set_vertex(
                        into[written / 2], (written & 1) != 0,
                        points[point][0], points[point][1], points[point][2],
                        colour, uv.s, uv.t
                    );
                    ++written;
                }
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

void set_matched_camera(T3DViewport& viewport) {
    fm_vec3_t eye;
    eye.v[0] = 0.0F;
    eye.v[1] = 540.0F;
    eye.v[2] = -440.0F;
    fm_vec3_t focus;
    focus.v[0] = 0.0F;
    focus.v[1] = 8.0F;
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

std::uint64_t draw_cutout_frame(
    surface_t* display, T3DViewport* viewport, T3DMat4FP* matrix,
    const T3DVertPacked* scene, int scene_triangles, std::uint8_t terrain,
    cutout::Style style, const cutout::Frame& cutout_frame, const char* label,
    const char* footer, const char* controls, color_t clear_colour,
    bool capture_mode = false
) {
    const std::uint64_t started = get_ticks_us();
    rdpq_attach(display, display_get_zbuf());
    t3d_frame_start();
    t3d_viewport_attach(viewport);
    t3d_screen_clear_color(clear_colour);
    t3d_screen_clear_depth();
    if (capture_mode) {
        backdrop::draw(matched_backdrop);
    }
    rdpq_mode_zbuf(true, true);
    t3d_light_set_count(0);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_TEXTURED | T3D_FLAG_SHADED |
        T3D_FLAG_NO_LIGHT | T3D_FLAG_DEPTH
    ));
    t3d_matrix_push(matrix);
    texture::bind_terrain();
    draw_unindexed(scene, scene_triangles);
    t3d_matrix_pop(1);
    t3d_tri_sync();
    if (!capture_mode) texture::draw_close_props(terrain);
    cutout::draw(style, cutout_frame);
    rdpq_detach_wait();
    if (!capture_mode) {
        graphics_draw_box(
            display, 0, 196, 320, 44,
            graphics_make_color(15, 22, 24, 255)
        );
        graphics_set_color(graphics_make_color(245, 234, 210, 255), 0);
        graphics_draw_text(display, 8, 8, label);
        graphics_set_color(graphics_make_color(125, 168, 133, 255), 0);
        graphics_draw_text(display, 8, 204, footer);
        graphics_draw_text(display, 8, 220, controls);
    }
    return get_ticks_us() - started;
}

std::uint64_t draw_tactical_cutout_frame(
    surface_t* display, T3DViewport* viewport, T3DMat4FP* matrix,
    const T3DVertPacked* board, const cutout::TacticalFrame& tactical_frame,
    const char* label, const char* footer, const char* controls
) {
    const std::uint64_t started = get_ticks_us();
    rdpq_attach(display, display_get_zbuf());
    t3d_frame_start();
    rdpq_mode_zbuf(true, true);
    t3d_viewport_attach(viewport);
    t3d_screen_clear_color(RGBA32(20, 25, 35, 255));
    t3d_screen_clear_depth();
    t3d_light_set_count(0);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_TEXTURED | T3D_FLAG_SHADED |
        T3D_FLAG_NO_LIGHT | T3D_FLAG_DEPTH
    ));
    t3d_matrix_push(matrix);
    texture::bind_terrain();
    draw_unindexed(board, board_triangles);
    t3d_matrix_pop(1);
    t3d_tri_sync();
    texture::draw_board_props(false);
    cutout::draw_tactical(tactical_frame);
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

std::uint64_t draw_matched_board_frame(
    surface_t* display, T3DViewport* viewport, T3DMat4FP* matrix,
    const T3DVertPacked* board, int left_health, int right_health,
    bool focus
) {
    const std::uint64_t started = get_ticks_us();
    rdpq_attach(display, display_get_zbuf());
    t3d_frame_start();
    rdpq_mode_zbuf(true, true);
    t3d_viewport_attach(viewport);
    t3d_screen_clear_color(RGBA32(20, 25, 35, 255));
    t3d_screen_clear_depth();
    t3d_light_set_count(0);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_TEXTURED | T3D_FLAG_SHADED |
        T3D_FLAG_NO_LIGHT | T3D_FLAG_DEPTH
    ));
    t3d_matrix_push(matrix);
    texture::bind_terrain();
    draw_unindexed(board, matched_scene_triangles);
    t3d_matrix_pop(1);
    t3d_tri_sync();
    texture::draw_board_props(true);
    cutout::draw_matched_board(left_health, right_health, focus);
    rdpq_detach_wait();
    return get_ticks_us() - started;
}

void apply_tactical_highlights(T3DVertPacked* board) {
    constexpr int move_cells[] = {18, 19, 20, 26, 28, 34, 35, 36};
    for (int cell : move_cells) {
        for (int packed = 0; packed < 3; ++packed) {
            T3DVertPacked& vertices = board[cell * 3 + packed];
            vertices.rgbaA = rgba(52, 112, 178);
            vertices.rgbaB = rgba(52, 112, 178);
        }
    }
    constexpr int selected_cell = 27;
    for (int packed = 0; packed < 3; ++packed) {
        T3DVertPacked& vertices = board[selected_cell * 3 + packed];
        vertices.rgbaA = rgba(224, 190, 84);
        vertices.rgbaB = rgba(224, 190, 84);
    }
}

std::uint64_t show_tactical_cutout(
    bool report, T3DViewport& viewport, T3DMat4FP* matrix,
    T3DVertPacked* board
) {
    constexpr int stage = 0;
    build_board(board, stage);
    apply_tactical_highlights(board);
    const cutout::TacticalFrame tactical_frame{0, 0, 0, 0};
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 3; ++frame) {
        surface_t* display = display_get();
        elapsed = draw_tactical_cutout_frame(
            display, &viewport, matrix, board, tactical_frame,
            "Medieval Gate TACTICAL - 2.5D",
            "8 CURATED ROLES - HEALTH + TEAMS",
            "Z VIEW B MOVE - BLUE + GOLD"
        );
        display_show(display);
    }
    if (!report) return elapsed;
    std::fprintf(
        stderr,
        "sevenfold tactical-cutout stage=medieval models=8 roles=8 bodies=mixed teams=blue-red scene_triangles=%d us=%llu fps=%llu tmem_peak=%lu embedded=%lu terrain_tmem=%lu terrain_embedded=%lu props=edge-sparse variation=deterministic cluster=transition-tone ordering=foot-depth highlights=blue-and-gold\n",
        board_triangles, static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(elapsed > 0 ? 1'000'000 / elapsed : 0),
        static_cast<unsigned long>(cutout::tactical_peak_texture_bytes()),
        static_cast<unsigned long>(cutout::tactical_embedded_bytes()),
        static_cast<unsigned long>(texture::terrain_peak_texture_bytes()),
        static_cast<unsigned long>(texture::terrain_embedded_bytes())
    );
    std::fprintf(stderr, "CHECKPOINT sevenfold-tactical-cutout-medieval\n");
    std::fflush(stderr);
    return elapsed;
}

std::uint64_t play_tactical_cutout_move(
    bool report, T3DViewport& viewport, T3DMat4FP* matrix,
    T3DVertPacked* board
) {
    constexpr int stage = 0;
    build_board(board, stage);
    apply_tactical_highlights(board);
    if (report) {
        surface_t* display = display_get();
        draw_tactical_cutout_frame(
            display, &viewport, matrix, board,
            cutout::TacticalFrame{0, 0, 0, 0},
            "Medieval Gate TACTICAL - 2.5D MOVE",
            "ROGUE MOVES INTO SELECTED TILE",
            "ANTICIPATE - WALK - SETTLE"
        );
        display_show(display);
        std::fprintf(
            stderr, "CHECKPOINT sevenfold-tactical-cutout-move-start\n"
        );
        std::fflush(stderr);
        wait_ms(700);
    }
    std::uint64_t slowest = 0;
    for (int frame = 0; frame < 72; ++frame) {
        cutout::TacticalFrame tactical_frame{0, 0, 0, 0};
        if (frame >= 8 && frame < 16) {
            tactical_frame.selected_idle_pose = 1;
        } else if (frame >= 16 && frame < 48) {
            tactical_frame.move_progress = (frame - 15) * 100 / 32;
            tactical_frame.move_pose = 1 + (((frame - 16) / 4) & 1);
            tactical_frame.ambient_idle_pose = frame >= 32 ? 1 : 0;
        } else if (frame >= 48) {
            tactical_frame.move_progress = 100;
            tactical_frame.selected_idle_pose = frame < 56 ? 1 : 0;
            tactical_frame.ambient_idle_pose = frame < 56 ? 1 : 0;
        }
        surface_t* display = display_get();
        const std::uint64_t elapsed = draw_tactical_cutout_frame(
            display, &viewport, matrix, board, tactical_frame,
            "Medieval Gate TACTICAL - 2.5D MOVE",
            "ROGUE MOVES INTO SELECTED TILE",
            "ANTICIPATE - EASED WALK - SETTLE"
        );
        display_show(display);
        if (elapsed > slowest) slowest = elapsed;
        if (report && frame == 64) {
            std::fprintf(
                stderr, "CHECKPOINT sevenfold-tactical-cutout-move-end\n"
            );
            std::fflush(stderr);
            wait_ms(900);
        } else {
            wait_ms(16);
        }
    }
    if (report) {
        std::fprintf(
            stderr,
            "sevenfold tactical-cutout-animation stage=medieval frames=72 max_us=%llu fps=%llu motion=tile-move poses=idle2-walk2 easing=smoothstep beats=anticipation-move-settle ordering=foot-depth\n",
            static_cast<unsigned long long>(slowest),
            static_cast<unsigned long long>(
                slowest > 0 ? 1'000'000 / slowest : 0
            )
        );
        std::fflush(stderr);
    }
    return slowest;
}

[[nodiscard]] int eased_distance(
    int step, int duration, int distance
) noexcept {
    if (step <= 0) return 0;
    if (step >= duration) return distance;
    const int amount = step * 256 / duration;
    const int eased = amount * amount * (768 - 2 * amount) / (256 * 256);
    return distance * eased / 256;
}

[[nodiscard]] cutout::Frame neutral_cutout_frame(bool alternate = false) {
    const cutout::Pose pose = alternate
        ? cutout::Pose::idle_b : cutout::Pose::idle_a;
    return {pose, pose, -1, 0, 0, 0, 0};
}

[[nodiscard]] cutout::Frame strike_cutout_frame(
    int frame, bool counter
) {
    cutout::Frame pose = neutral_cutout_frame(frame >= 108);
    cutout::Pose& attacker = counter ? pose.right_pose : pose.left_pose;
    cutout::Pose& defender = counter ? pose.left_pose : pose.right_pose;
    int& push = counter ? pose.right_push : pose.left_push;
    int& recoil = counter ? pose.left_recoil : pose.right_recoil;
    const int target = counter ? 0 : 1;
    if (frame >= 24 && frame < 48) {
        attacker = cutout::Pose::windup;
        push = -eased_distance(frame - 24, 23, 4);
    } else if (frame >= 48 && frame < 72) {
        attacker = cutout::Pose::follow_through;
        push = -4 + eased_distance(frame - 48, 23, 46);
    } else if (frame >= 72 && frame < 84) {
        attacker = cutout::Pose::follow_through;
        defender = cutout::Pose::hurt;
        push = 42;
        recoil = eased_distance(frame - 72, 11, 8);
        // Keep the impact mark for the full hurt beat. The matched 30 fps
        // showcase samples this 123-frame move at 2x speed, so a six-frame
        // source flash could otherwise collapse to only three presented
        // frames and disappear between native capture samples.
        pose.impact_target = target;
    } else if (frame >= 84 && frame < 108) {
        attacker = frame < 96
            ? cutout::Pose::follow_through : cutout::Pose::idle_b;
        defender = frame < 100
            ? cutout::Pose::hurt : cutout::Pose::idle_b;
        push = 42 - eased_distance(frame - 84, 23, 42);
        recoil = 8 - eased_distance(frame - 84, 23, 8);
    }
    return pose;
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

void draw_capture_health(
    surface_t* display, int left_health, int right_health
) {
    constexpr int width = 42;
    graphics_draw_box(
        display, 8, 8, width + 8, 7,
        graphics_make_color(9, 14, 17, 235)
    );
    graphics_draw_box(
        display, 9, 9, width * left_health / 12, 5,
        graphics_make_color(35, 117, 169, 255)
    );
    graphics_draw_box(
        display, 270, 8, width, 7,
        graphics_make_color(9, 14, 17, 235)
    );
    graphics_draw_box(
        display, 271, 9, (width - 2) * right_health / 11, 5,
        graphics_make_color(179, 72, 63, 255)
    );
}

std::uint64_t play_matched_readme_sequence(
    T3DViewport& board_viewport, T3DViewport& attack_viewport,
    T3DMat4FP* matrix, T3DVertPacked* scene, bool report
) {
    build_matched_board(scene);
    surface_t* opening = display_get();
    draw_matched_board_frame(
        opening, &board_viewport, matrix, scene, 12, 11, false
    );
    display_show(opening);
    if (report) {
        std::fprintf(stderr, "CHECKPOINT sevenfold-readme-sequence-start\n");
        std::fflush(stderr);
        // Give the external ares film grabbers time to start. This is capture
        // pre-roll; the canonical 245-frame sequence begins after it.
        wait_ms(1200);
    }

    std::uint64_t slowest = 0;
    std::uint64_t backdrop_transition_us = 0;
    for (int frame = 0; frame < 245; ++frame) {
        surface_t* display = display_get();
        std::uint64_t elapsed = 0;
        if (frame < 68) {
            build_matched_board(scene);
            elapsed = draw_matched_board_frame(
                display, &board_viewport, matrix, scene, 12, 11,
                frame >= 35
            );
        } else if (frame < 191) {
            const bool counter = frame >= 129;
            const int local = counter ? frame - 129 : frame - 68;
            const cutout::Frame pose = strike_cutout_frame(
                local * 2, counter
            );
            constexpr int close_triangles = 0;
            elapsed = draw_cutout_frame(
                display, &attack_viewport, matrix, scene, close_triangles,
                2U, cutout::Style::medieval, pose,
                counter ? "MATCHED SCENE - COUNTER" :
                    "MATCHED SCENE - ATTACK",
                counter ? "RED FEMALE RIGHT TO LEFT" :
                    "BLUE MALE LEFT TO RIGHT",
                "FIXED SIDES - FOREST + GRASS + PAVED",
                RGBA32(
                    terrain_sky_colours[2][0],
                    terrain_sky_colours[2][1],
                    terrain_sky_colours[2][2], 255
                ), true
            );
            if (frame == 68) backdrop_transition_us = elapsed;
            const int close_left_health =
                counter && local >= 36 ? 8 : 12;
            const int close_right_health =
                !counter && local >= 36 ? 8 : 11;
            draw_capture_health(
                display, close_left_health, close_right_health
            );
            if (!counter && local >= 36 && local < 44) {
                draw_damage_callout(display, 1, 3);
            } else if (counter && local >= 36 && local < 44) {
                draw_damage_callout(display, 0, 4);
            }
        } else if (frame < 219) {
            if (frame < 205) {
                constexpr int close_triangles = 0;
                elapsed = draw_cutout_frame(
                    display, &attack_viewport, matrix, scene, close_triangles,
                    2U, cutout::Style::medieval, neutral_cutout_frame(true),
                    "MATCHED SCENE - RETURN",
                    "BLUE HP8 - RED HP8", "FIXED SIDES RESTORED",
                    RGBA32(
                        terrain_sky_colours[2][0],
                        terrain_sky_colours[2][1],
                        terrain_sky_colours[2][2], 255
                    ), true
                );
                draw_capture_health(display, 8, 8);
            } else {
                build_matched_board(scene);
                elapsed = draw_matched_board_frame(
                    display, &board_viewport, matrix, scene, 8, 8, true
                );
            }
        } else {
            build_matched_board(scene);
            elapsed = draw_matched_board_frame(
                display, &board_viewport, matrix, scene, 8, 8, false
            );
        }
        display_show(display);
        if (elapsed > slowest) slowest = elapsed;
        if (report) {
            const char* checkpoint = nullptr;
            if (frame == 15) checkpoint = "sevenfold-readme-board";
            else if (frame == 51) checkpoint = "sevenfold-readme-focus";
            else if (frame == 104) checkpoint = "sevenfold-readme-attack";
            else if (frame == 165) checkpoint = "sevenfold-readme-counter";
            else if (frame == 227) checkpoint = "sevenfold-readme-restored";
            if (checkpoint != nullptr) {
                std::fprintf(stderr, "CHECKPOINT %s\n", checkpoint);
                std::fflush(stderr);
            }
        }
        wait_ms(16);
    }
    wait_ms(1200);
    if (report) {
        std::fprintf(
            stderr,
            "sevenfold readme-sequence id=medieval_forest_renderer_showcase_v2 frames=245 fps=30 seconds=8.15 board=6x4 surroundings=12x10 terrain=2-9-12 hud=compact diagnostics=none left=medieval-knight-male-blue-hp12-to8 right=medieval-knight-female-red-hp11-to8 damage=3-4 impacts=3.45-5.50 anchors=locked backdrop_first_us=%llu max_us=%llu fps_throughput=%llu\n",
            static_cast<unsigned long long>(backdrop_transition_us),
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
    std::fprintf(stderr, "grandleon n64 2.5d renderer\n");
    display_init(
        RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE
    );
    rdpq_init();
    joypad_init();
    T3DInitParams params{};
    t3d_init(params);

    const bool terrain_ready = texture::initialize();
    const bool backdrop_ready = backdrop::initialize();
    const bool cutout_ready = cutout::initialize();
    expect(terrain_ready, "2.5D terrain and prop atlases allocate");
    expect(
        texture::terrain_peak_texture_bytes() <= 4096U,
        "each terrain atlas fits four KiB of TMEM"
    );
    expect(
        backdrop_ready && backdrop::select_variant(matched_backdrop) == 2,
        "shared full-frame backdrop variants allocate and select deterministically"
    );
    expect(
        backdrop::peak_texture_bytes() <= 4096U,
        "each streamed backdrop tile fits four KiB of TMEM"
    );
    expect(
        matched_layout_hash() == 0x66909BD6U,
        "matched twelve-by-ten surroundings and active origin agree"
    );
    expect(cutout_ready, "tactical and close cutouts plus effects allocate");
    expect(
        cutout::peak_texture_bytes() <= 4096U &&
            cutout::tactical_peak_texture_bytes() <= 4096U,
        "close and tactical cutouts each fit four KiB of TMEM"
    );
    std::fprintf(
        stderr,
        "sevenfold 2.5d terrain=64x64-ci4 backdrop=320x240-ci4 tiles=24 cutouts=godot-derived floor=pre-rendered-full-frame terrain_embedded=%lu backdrop_embedded=%lu close_embedded=%lu tactical_embedded=%lu\n",
        static_cast<unsigned long>(texture::terrain_embedded_bytes()),
        static_cast<unsigned long>(backdrop::embedded_bytes()),
        static_cast<unsigned long>(cutout::embedded_bytes()),
        static_cast<unsigned long>(cutout::tactical_embedded_bytes())
    );

    T3DVertPacked* board = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * packed_board_vertices)
    );
    T3DMat4FP* matrix = static_cast<T3DMat4FP*>(
        malloc_uncached(sizeof(T3DMat4FP))
    );
    const bool allocated = board != nullptr && matrix != nullptr;
    expect(allocated, "2.5D board and matrix buffers allocate");
    if (!allocated || !terrain_ready || !backdrop_ready || !cutout_ready) {
        std::fprintf(stderr, "RESULT FAIL %d/%d\n", checks - failures, checks);
        while (true) wait_ms(1000);
    }

    t3d_mat4fp_identity(matrix);
    T3DViewport tactical_viewport = t3d_viewport_create();
    t3d_viewport_set_projection(
        &tactical_viewport, T3D_DEG_TO_RAD(50.0F), 16.0F, 1200.0F
    );
    set_camera(tactical_viewport);
    T3DViewport close_viewport = t3d_viewport_create();
    t3d_viewport_set_projection(
        &close_viewport, T3D_DEG_TO_RAD(45.0F), 16.0F, 900.0F
    );
    set_attack_camera(close_viewport);
    T3DViewport matched_viewport = t3d_viewport_create();
    t3d_viewport_set_projection(
        &matched_viewport, T3D_DEG_TO_RAD(35.0F), 16.0F, 1300.0F
    );
    set_matched_camera(matched_viewport);

    const std::uint64_t tactical_us = show_tactical_cutout(
        true, tactical_viewport, matrix, board
    );
    expect(tactical_us <= frame_budget_us, "2.5D tactical board stays at 30 fps");
    const std::uint64_t movement_us = play_tactical_cutout_move(
        true, tactical_viewport, matrix, board
    );
    expect(movement_us <= frame_budget_us, "2.5D tile movement stays at 30 fps");
    const std::uint64_t comparison_us = play_matched_readme_sequence(
        matched_viewport, close_viewport, matrix, board, true
    );
    expect(
        comparison_us <= frame_budget_us,
        "matched board, attack and counter sequence stays at 30 fps"
    );

    std::fprintf(
        stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
        checks - failures, checks
    );
    std::fflush(stderr);

    show_tactical_cutout(false, tactical_viewport, matrix, board);
    while (true) {
        joypad_poll();
        const joypad_buttons_t pressed =
            joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (pressed.a || pressed.start) {
            play_matched_readme_sequence(
                matched_viewport, close_viewport, matrix, board, false
            );
            show_tactical_cutout(false, tactical_viewport, matrix, board);
        } else if (pressed.b) {
            play_tactical_cutout_move(
                false, tactical_viewport, matrix, board
            );
            show_tactical_cutout(false, tactical_viewport, matrix, board);
        }
        wait_ms(16);
    }
}
