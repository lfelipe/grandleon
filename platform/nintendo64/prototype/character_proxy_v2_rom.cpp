// SPDX-License-Identifier: MIT
// Two topology-aware Godot character LODs through the pinned Tiny3D path.
//
// This is local experiment evidence, not a game renderer.  It deliberately
// keeps geometry, camera distance and visible-unit count as separate switches:
// full-board/low LOD, tactical/high LOD and action/high LOD.

#include <libdragon.h>

#include <t3d/t3d.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "character_proxies_v2.h"
#include "sprite_assets.h"

namespace {

constexpr int board_columns = 12;
constexpr int board_rows = 9;
constexpr int cell_world = 64;
constexpr int board_triangles = board_columns * board_rows * 2;
constexpr int board_vertices = board_triangles * 3;
constexpr int packed_board_vertices = (board_vertices + 1) / 2;
constexpr int max_proxy_triangles = rogue_high::triangle_count;
constexpr int max_proxy_vertices = max_proxy_triangles * 3;
constexpr int packed_proxy_vertices = (max_proxy_vertices + 1) / 2;
constexpr int max_figures = 16;

// Below Tiny3D's 70-entry cache, divisible by three, and even so every batch
// begins at the first half of a packed pair.
constexpr int batch_vertices = 66;
constexpr int batch_triangles = batch_vertices / 3;

int checks = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) ++failures;
    std::fprintf(stderr, "%s %s\n", condition ? "ok  " : "FAIL", message);
    std::fflush(stderr);
}

struct ProxyView final {
    const CharacterProxyV2Vertex* vertices;
    const CharacterProxyV2Triangle* triangles;
    const std::uint8_t (*materials)[3];
    int triangle_count;
};

constexpr ProxyView commander_low_view{
    commander_low::vertices, commander_low::triangles,
    commander_low::materials, commander_low::triangle_count};
constexpr ProxyView commander_high_view{
    commander_high::vertices, commander_high::triangles,
    commander_high::materials, commander_high::triangle_count};
constexpr ProxyView rogue_low_view{
    rogue_low::vertices, rogue_low::triangles,
    rogue_low::materials, rogue_low::triangle_count};
constexpr ProxyView rogue_high_view{
    rogue_high::vertices, rogue_high::triangles,
    rogue_high::materials, rogue_high::triangle_count};

struct Figure final {
    T3DVertPacked* vertices;
    int triangles;
};

void set_vertex(T3DVertPacked& packed, bool second, std::int16_t x,
                std::int16_t y, std::int16_t z, std::uint32_t colour) {
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

[[nodiscard]] std::uint32_t triangle_colour(
    const ProxyView& view, const CharacterProxyV2Triangle& triangle
) noexcept {
    const std::uint8_t* rgb = view.materials[triangle.material];
    const std::uint32_t light = 128u + triangle.shade / 2u;
    const std::uint32_t red = rgb[0] * light / 255u;
    const std::uint32_t green = rgb[1] * light / 255u;
    const std::uint32_t blue = rgb[2] * light / 255u;
    return (red << 24) | (green << 16) | (blue << 8) | 0xFFu;
}

[[nodiscard]] constexpr std::int16_t cell_x(int column) noexcept {
    return static_cast<std::int16_t>(
        (column - board_columns / 2) * cell_world + cell_world / 2
    );
}

[[nodiscard]] constexpr std::int16_t cell_z(int row) noexcept {
    return static_cast<std::int16_t>(
        (row - board_rows / 2) * cell_world
    );
}

void build_proxy(Figure& into, const ProxyView& view, int column, int row,
                 int yaw_degrees = 0) {
    into.triangles = view.triangle_count;
    constexpr int rotation_one = 4096;
    constexpr int rotation_diagonal = 2896;
    int yaw_sine = 0;
    int yaw_cosine = rotation_one;
    if (yaw_degrees == 45) {
        yaw_sine = rotation_diagonal;
        yaw_cosine = rotation_diagonal;
    } else if (yaw_degrees == 90) {
        yaw_sine = rotation_one;
        yaw_cosine = 0;
    } else if (yaw_degrees == 180) {
        yaw_sine = 0;
        yaw_cosine = -rotation_one;
    } else if (yaw_degrees == 270) {
        yaw_sine = -rotation_one;
        yaw_cosine = 0;
    } else if (yaw_degrees == 315) {
        yaw_sine = -rotation_diagonal;
        yaw_cosine = rotation_diagonal;
    }
    const std::int16_t offset_x = cell_x(column);
    const std::int16_t offset_z = cell_z(row);
    const int written_vertices = view.triangle_count * 3;
    for (int index = 0; index < (written_vertices + 1) / 2; ++index) {
        into.vertices[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int index = 0; index < view.triangle_count; ++index) {
        const CharacterProxyV2Triangle& triangle = view.triangles[index];
        const std::uint16_t corners[3] = {triangle.a, triangle.b, triangle.c};
        const std::uint32_t colour = triangle_colour(view, triangle);
        for (int corner = 0; corner < 3; ++corner) {
            const CharacterProxyV2Vertex& source = view.vertices[corners[corner]];
            const int x = (source.x * yaw_cosine + source.z * yaw_sine) /
                rotation_one;
            const int z = (-source.x * yaw_sine + source.z * yaw_cosine) /
                rotation_one;
            set_vertex(
                into.vertices[written / 2], (written & 1) != 0,
                static_cast<std::int16_t>(x + offset_x), source.y,
                static_cast<std::int16_t>(z + offset_z), colour
            );
            ++written;
        }
    }
}

void build_board(T3DVertPacked* into) {
    for (int index = 0; index < packed_board_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int row = 0; row < board_rows; ++row) {
        for (int column = 0; column < board_columns; ++column) {
            const std::int16_t x0 = static_cast<std::int16_t>(
                (column - board_columns / 2) * cell_world
            );
            const std::int16_t x1 = static_cast<std::int16_t>(x0 + cell_world);
            const std::int16_t z0 = static_cast<std::int16_t>(
                (row - board_rows / 2) * cell_world - cell_world / 2
            );
            const std::int16_t z1 = static_cast<std::int16_t>(z0 + cell_world);
            const std::uint32_t colour = ((column + row) & 1) != 0
                ? 0x56714BFFu : 0x425F3BFFu;
            const std::int16_t corners[6][3] = {
                {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1},
                {x0, 0, z0}, {x1, 0, z1}, {x0, 0, z1},
            };
            for (int corner = 0; corner < 6; ++corner) {
                set_vertex(
                    into[written / 2], (written & 1) != 0,
                    corners[corner][0], corners[corner][1], corners[corner][2],
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

sprite_t* load_sprite(const unsigned char* data, std::size_t size) {
    void* buffer = std::malloc(size);
    if (buffer == nullptr) return nullptr;
    std::memcpy(buffer, data, size);
    return sprite_load_buf(buffer, static_cast<int>(size));
}

void set_camera(T3DViewport& viewport, float distance) {
    fm_vec3_t eye;
    eye.v[0] = 0.0f;
    eye.v[1] = distance * 0.839f;
    eye.v[2] = -distance * 0.547f;
    fm_vec3_t focus;
    focus.v[0] = 0.0f;
    focus.v[1] = 0.0f;
    focus.v[2] = 0.0f;
    fm_vec3_t up;
    up.v[0] = 0.0f;
    up.v[1] = 1.0f;
    up.v[2] = 0.0f;
    t3d_viewport_look_at(&viewport, &eye, &focus, &up);
}

std::uint64_t draw_frame(surface_t* display, T3DViewport* viewport,
                         T3DMat4FP* matrix, const T3DVertPacked* board,
                         const Figure* figures, int figure_count,
                         sprite_t* commander_reference,
                         sprite_t* rogue_reference, bool references) {
    const std::uint64_t started = get_ticks_us();
    rdpq_attach(display, display_get_zbuf());
    t3d_frame_start();
    rdpq_mode_zbuf(true, true);
    t3d_viewport_attach(viewport);
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_screen_clear_color(RGBA32(25, 32, 46, 255));
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

    if (references) {
        rdpq_set_mode_standard();
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_mode_alphacompare(1);
        rdpq_mode_filter(FILTER_POINT);
        rdpq_sprite_blit(commander_reference, 8.0f, 8.0f, nullptr);
        rdpq_sprite_blit(rogue_reference, 44.0f, 8.0f, nullptr);
    }
    rdpq_detach_wait();
    return get_ticks_us() - started;
}

int triangle_total(const Figure* figures, int count) {
    int result = board_triangles;
    for (int index = 0; index < count; ++index) {
        result += figures[index].triangles;
    }
    return result;
}

std::uint64_t show_scene(T3DViewport& viewport, T3DMat4FP* matrix,
                         const T3DVertPacked* board, const Figure* figures,
                         int count, sprite_t* commander_reference,
                         sprite_t* rogue_reference, bool references) {
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 4; ++frame) {
        surface_t* display = display_get();
        elapsed = draw_frame(
            display, &viewport, matrix, board, figures, count,
            commander_reference, rogue_reference, references
        );
        display_show(display);
    }
    return elapsed;
}

void log_scene(const char* name, const Figure* figures, int count,
               std::uint64_t elapsed) {
    std::fprintf(
        stderr, "%s triangles=%d us=%llu fps=%llu depth=on\n", name,
        triangle_total(figures, count),
        static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(
            elapsed > 0 ? 1000000 / elapsed : 0
        )
    );
    std::fflush(stderr);
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

    static_assert(commander_low::triangle_count == 342,
                  "the reviewed commander low LOD changed");
    static_assert(rogue_low::triangle_count == 330,
                  "the reviewed rogue low LOD changed");
    static_assert(commander_high::triangle_count == 524,
                  "the reviewed commander high LOD changed");
    static_assert(rogue_high::triangle_count == 528,
                  "the reviewed rogue high LOD changed");

    T3DVertPacked* board = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * packed_board_vertices)
    );
    Figure figures[max_figures]{};
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
    sprite_t* commander_reference = load_sprite(
        sprite_scifi_first_commander_amber,
        sprite_scifi_first_commander_amber_size
    );
    sprite_t* rogue_reference = load_sprite(
        sprite_scifi_first_rogue_amber,
        sprite_scifi_first_rogue_amber_size
    );
    allocated = allocated && commander_reference != nullptr &&
        rogue_reference != nullptr;
    expect(allocated, "board, proxy buffers and two reference sprites allocate");
    if (!allocated) {
        std::fprintf(stderr, "RESULT FAIL %d/%d\n", checks - failures, checks);
        while (true) wait_ms(1000);
    }

    build_board(board);
    const float unit[3] = {1.0f, 1.0f, 1.0f};
    const float none[3] = {0.0f, 0.0f, 0.0f};
    t3d_mat4fp_from_srt_euler(matrix, unit, none, none);
    T3DViewport viewport = t3d_viewport_create();
    t3d_viewport_set_projection(
        &viewport, T3D_DEG_TO_RAD(50.0f), 16.0f, 1400.0f
    );

    // Same near/mid/far cells as the PS1 capture, alternating roles.
    const int strategic_cells[6][2] = {
        {3, 1}, {7, 1}, {3, 4}, {7, 4}, {3, 7}, {7, 7},
    };
    for (int index = 0; index < 6; ++index) {
        build_proxy(
            figures[index], (index & 1) == 0 ? commander_low_view : rogue_low_view,
            strategic_cells[index][0], strategic_cells[index][1]
        );
    }
    set_camera(viewport, 744.0f);
    std::uint64_t elapsed = show_scene(
        viewport, matrix, board, figures, 6, commander_reference,
        rogue_reference, true
    );
    log_scene("v2 strategic six low-LOD", figures, 6, elapsed);
    std::fprintf(stderr, "CHECKPOINT v2-strategic\n");
    std::fflush(stderr);
    wait_ms(1800);

    const int battle_cells[max_figures][2] = {
        {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1}, {9, 1},
        {2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {7, 6}, {8, 6}, {9, 6},
    };
    for (int index = 0; index < max_figures; ++index) {
        build_proxy(
            figures[index], (index & 1) == 0 ? commander_low_view : rogue_low_view,
            battle_cells[index][0], battle_cells[index][1]
        );
    }
    elapsed = show_scene(
        viewport, matrix, board, figures, max_figures, commander_reference,
        rogue_reference, false
    );
    log_scene("v2 strategic sixteen low-LOD", figures, max_figures, elapsed);
    expect(elapsed < 33333,
           "the full board and sixteen low-LOD figures fit inside 30 fps");
    std::fprintf(stderr, "CHECKPOINT v2-strategic-sixteen\n");
    std::fflush(stderr);
    wait_ms(1800);

    const int tactical_cells[6][2] = {
        {4, 2}, {7, 3}, {3, 5}, {6, 5}, {8, 6}, {5, 7},
    };
    for (int index = 0; index < 6; ++index) {
        build_proxy(
            figures[index],
            (index & 1) == 0 ? commander_high_view : rogue_high_view,
            tactical_cells[index][0], tactical_cells[index][1]
        );
    }
    set_camera(viewport, 520.0f);
    elapsed = show_scene(
        viewport, matrix, board, figures, 6, commander_reference,
        rogue_reference, true
    );
    log_scene("v2 tactical six high-LOD", figures, 6, elapsed);
    expect(elapsed < 33333,
           "six high-LOD figures at tactical distance fit inside 30 fps");
    std::fprintf(stderr, "CHECKPOINT v2-tactical\n");
    std::fflush(stderr);
    wait_ms(1800);

    build_proxy(figures[0], commander_high_view, 4, 4, 315);
    build_proxy(figures[1], rogue_high_view, 7, 4, 45);
    std::fprintf(stderr, "CHECKPOINT v2-action-zoom\n");
    std::fflush(stderr);
    wait_ms(500);
    for (int frame = 0; frame < 80; ++frame) {
        const int step = frame < 60 ? frame : 59;
        set_camera(
            viewport,
            520.0f - 160.0f * static_cast<float>(step) / 59.0f
        );
        surface_t* display = display_get();
        (void)draw_frame(
            display, &viewport, matrix, board, figures, 2,
            commander_reference, rogue_reference, false
        );
        display_show(display);
    }
    set_camera(viewport, 360.0f);
    elapsed = show_scene(
        viewport, matrix, board, figures, 2, commander_reference,
        rogue_reference, true
    );
    log_scene("v2 action two high-LOD", figures, 2, elapsed);
    expect(elapsed < 33333,
           "two high-LOD figures at action distance fit inside 30 fps");
    std::fprintf(stderr, "CHECKPOINT v2-action\n");
    std::fprintf(
        stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
        checks - failures, checks
    );
    std::fflush(stderr);

    while (true) {
        wait_ms(1000);
        std::fprintf(
            stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
            checks - failures, checks
        );
        std::fflush(stderr);
    }
}
