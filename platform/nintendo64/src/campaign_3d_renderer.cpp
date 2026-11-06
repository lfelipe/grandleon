// SPDX-License-Identifier: MIT

#include "campaign_3d_renderer.h"

#include "sevenfold_campaign_proxies.h"
#include "themes.h"

#include <cstddef>
#include <cstdio>

namespace grandleon::nintendo64 {
namespace {

namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

constexpr int board_columns = 8;
constexpr int board_rows = 6;
constexpr int board_cells = board_columns * board_rows;
constexpr int board_triangles = board_cells * 2;
constexpr int board_vertices = board_triangles * 3;
constexpr int packed_board_vertices = (board_vertices + 1) / 2;
constexpr int maximum_proxy_vertices = sevenfold_low_max_triangles * 3;
constexpr int packed_proxy_vertices = (maximum_proxy_vertices + 1) / 2;
constexpr int cell_world = 64;
constexpr int batch_vertices = 66;
constexpr int batch_triangles = batch_vertices / 3;

constexpr std::uint8_t team_colours[2][3] = {
    {65, 132, 235}, {215, 74, 68},
};

[[nodiscard]] constexpr std::uint32_t rgba(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue
) noexcept {
    return (static_cast<std::uint32_t>(red) << 24) |
           (static_cast<std::uint32_t>(green) << 16) |
           (static_cast<std::uint32_t>(blue) << 8) | 0xFFU;
}

void set_vertex(
    T3DVertPacked& packed, bool second, std::int16_t x, std::int16_t y,
    std::int16_t z, std::uint32_t colour
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

[[nodiscard]] constexpr std::int16_t world_x(int column) noexcept {
    return static_cast<std::int16_t>(
        (column - board_columns / 2) * cell_world + cell_world / 2
    );
}

[[nodiscard]] constexpr std::int16_t world_z(int row) noexcept {
    return static_cast<std::int16_t>(
        (row - board_rows / 2) * cell_world + cell_world / 2
    );
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

void build_proxy(
    T3DVertPacked* into, int& triangle_count, const SevenfoldProxy& proxy,
    int column, int row, int elevation, int team, int quarter_turn
) {
    triangle_count = proxy.triangles_count;
    for (int index = 0; index < packed_proxy_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int index = 0; index < proxy.triangles_count; ++index) {
        const SevenfoldProxyTriangle& triangle = proxy.triangles[index];
        const std::uint16_t corners[3] = {
            triangle.a, triangle.b, triangle.c,
        };
        const std::uint32_t colour = triangle_colour(proxy, triangle, team);
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
                into[written / 2], (written & 1) != 0,
                static_cast<std::int16_t>(x + world_x(column)),
                static_cast<std::int16_t>(source.y + elevation * 12),
                static_cast<std::int16_t>(z + world_z(row)), colour
            );
            ++written;
        }
    }
}

void build_board(
    T3DVertPacked* into, const sim::EncounterSnapshot& snapshot,
    const std::vector<std::uint64_t>& terrain, const pr::Presentation& shown,
    std::uint8_t theme
) {
    for (int index = 0; index < packed_board_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int row = 0; row < board_rows; ++row) {
        for (int column = 0; column < board_columns; ++column) {
            std::uint8_t kind = 9;
            const std::size_t index =
                static_cast<std::size_t>(row) * snapshot.width + column;
            if (column < snapshot.width && row < snapshot.height &&
                index < terrain.size()) {
                const std::uint8_t resolved = shown.kind_of_terrain(terrain[index]);
                if (resolved < grandleon_terrain_kind_count) kind = resolved;
            }
            const std::uint8_t* rgb = grandleon_terrain_flat[theme][kind];
            const std::uint32_t colour = rgba(rgb[0], rgb[1], rgb[2]);
            const std::int16_t x0 = static_cast<std::int16_t>(
                (column - board_columns / 2) * cell_world
            );
            const std::int16_t x1 = static_cast<std::int16_t>(x0 + cell_world);
            const std::int16_t z0 = static_cast<std::int16_t>(
                (row - board_rows / 2) * cell_world
            );
            const std::int16_t z1 = static_cast<std::int16_t>(z0 + cell_world);
            const std::int16_t high = static_cast<std::int16_t>(
                grandleon_terrain_elevation[kind] * 12
            );
            const std::int16_t points[6][3] = {
                {x0, high, z0}, {x1, high, z0}, {x1, high, z1},
                {x0, high, z0}, {x1, high, z1}, {x0, high, z1},
            };
            for (int point = 0; point < 6; ++point) {
                set_vertex(
                    into[written / 2], (written & 1) != 0,
                    points[point][0], points[point][1], points[point][2], colour
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

}  // namespace

Campaign3DRenderer::Campaign3DRenderer() {
    T3DInitParams params{};
    t3d_init(params);
    board_ = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * packed_board_vertices)
    );
    bool allocated = board_ != nullptr;
    for (Figure& figure : figures_) {
        figure.vertices = static_cast<T3DVertPacked*>(
            malloc_uncached(sizeof(T3DVertPacked) * packed_proxy_vertices)
        );
        allocated = allocated && figure.vertices != nullptr;
    }
    matrix_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    allocated = allocated && matrix_ != nullptr;
    if (!allocated) {
        std::fprintf(stderr, "FAIL N64 3D campaign buffers allocate\n");
        return;
    }
    t3d_mat4fp_identity(matrix_);
    viewport_ = t3d_viewport_create();
    t3d_viewport_set_projection(
        &viewport_, T3D_DEG_TO_RAD(50.0F), 16.0F, 1200.0F
    );
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
    t3d_viewport_look_at(&viewport_, &eye, &focus, &up);
    ready_ = true;
}

void Campaign3DRenderer::draw(
    surface_t* display, const sim::EncounterSnapshot& snapshot,
    const std::vector<std::uint64_t>& terrain, const pr::Presentation& shown,
    const view::Camera& camera, std::uint8_t theme, sim::UnitId skip_unit,
    sim::UnitId skip_other
) {
    static_cast<void>(camera);
    if (!ready_) return;
    if (theme >= grandleon_theme_count) theme = 0;
    build_board(board_, snapshot, terrain, shown, theme);

    int visible = 0;
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (visible >= maximum_figures) break;
        if (!sim::on_board(unit) || unit.id == skip_unit ||
            unit.id == skip_other) {
            continue;
        }
        const std::uint8_t style =
            shown.character_style_of_unit_type(unit.unit_type_id);
        const std::uint8_t figure =
            shown.character_figure_of_unit_type(unit.unit_type_id);
        const std::uint8_t archetype =
            shown.archetype_of_unit_type(unit.unit_type_id);
        const SevenfoldProxy* proxy = sevenfold_campaign_proxy(
            style, figure, archetype, false
        );
        if (proxy == nullptr) continue;
        const std::size_t terrain_index =
            static_cast<std::size_t>(unit.position.y) * snapshot.width +
            unit.position.x;
        int elevation = 0;
        if (terrain_index < terrain.size()) {
            const std::uint8_t kind = shown.kind_of_terrain(terrain[terrain_index]);
            if (kind < grandleon_terrain_kind_count) {
                elevation = grandleon_terrain_elevation[kind];
            }
        }
        build_proxy(
            figures_[visible].vertices, figures_[visible].triangles, *proxy,
            unit.position.x, unit.position.y, elevation,
            unit.side == sim::Side::first ? 0 : 1,
            unit.side == sim::Side::first ? 1 : -1
        );
        ++visible;
    }

    rdpq_attach(display, display_get_zbuf());
    t3d_frame_start();
    rdpq_mode_zbuf(true, true);
    t3d_viewport_attach(&viewport_);
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_screen_clear_color(RGBA32(20, 25, 35, 255));
    t3d_screen_clear_depth();
    t3d_light_set_count(0);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT | T3D_FLAG_DEPTH
    ));
    t3d_matrix_push(matrix_);
    draw_unindexed(board_, board_triangles);
    for (int index = 0; index < visible; ++index) {
        draw_unindexed(figures_[index].vertices, figures_[index].triangles);
    }
    t3d_matrix_pop(1);
    t3d_tri_sync();
    rdpq_detach_wait();
}

}  // namespace grandleon::nintendo64
