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
constexpr int maximum_board_triangles_per_cell = 14;
constexpr int maximum_board_triangles =
    board_cells * maximum_board_triangles_per_cell;
constexpr int packed_board_vertices =
    (maximum_board_triangles * 3 + 1) / 2;
constexpr int maximum_attack_scene_triangles = 160;
constexpr int packed_attack_scene_vertices =
    (maximum_attack_scene_triangles * 3 + 1) / 2;
constexpr int maximum_low_proxy_triangles =
    sevenfold_low_max_triangles + 4;
constexpr int maximum_high_proxy_triangles =
    sevenfold_high_max_triangles + 4;
constexpr int packed_low_proxy_vertices =
    (maximum_low_proxy_triangles * 3 + 1) / 2;
constexpr int packed_high_proxy_vertices =
    (maximum_high_proxy_triangles * 3 + 1) / 2;
constexpr int cell_world = 64;
constexpr int height_world = 12;
constexpr int batch_vertices = 66;
constexpr int batch_triangles = batch_vertices / 3;

constexpr std::uint8_t team_colours[2][3] = {
    {65, 132, 235}, {215, 74, 68},
};

[[nodiscard]] constexpr std::uint8_t channel(
    std::uint32_t value
) noexcept {
    return static_cast<std::uint8_t>(value > 255U ? 255U : value);
}

[[nodiscard]] constexpr std::uint32_t rgba(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue
) noexcept {
    return (static_cast<std::uint32_t>(red) << 24) |
           (static_cast<std::uint32_t>(green) << 16) |
           (static_cast<std::uint32_t>(blue) << 8) | 0xFFU;
}

[[nodiscard]] std::uint32_t scaled_colour(
    const std::uint8_t* source, std::uint32_t light
) noexcept {
    return rgba(
        channel(static_cast<std::uint32_t>(source[0]) * light / 255U),
        channel(static_cast<std::uint32_t>(source[1]) * light / 255U),
        channel(static_cast<std::uint32_t>(source[2]) * light / 255U)
    );
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

void emit_triangle(
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

void emit_quad(
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
    emit_triangle(into, written, first, colour);
    emit_triangle(into, written, second, colour);
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

[[nodiscard]] constexpr std::int16_t world_x_fixed(
    int column_256
) noexcept {
    return static_cast<std::int16_t>(
        (column_256 - board_columns * 128) * cell_world / 256
    );
}

[[nodiscard]] constexpr std::int16_t world_z_fixed(
    int row_256
) noexcept {
    return static_cast<std::int16_t>(
        (row_256 - board_rows * 128) * cell_world / 256
    );
}

[[nodiscard]] std::uint8_t kind_at(
    const sim::EncounterSnapshot& snapshot,
    const std::vector<std::uint64_t>& terrain,
    const pr::Presentation& shown,
    int column,
    int row
) noexcept {
    if (column < 0 || row < 0 || column >= snapshot.width ||
        row >= snapshot.height) {
        return 9;
    }
    const std::size_t index =
        static_cast<std::size_t>(row) * snapshot.width + column;
    if (index >= terrain.size()) return 9;
    const std::uint8_t resolved = shown.kind_of_terrain(terrain[index]);
    return resolved < grandleon_terrain_kind_count ? resolved : 9;
}

[[nodiscard]] std::uint32_t triangle_colour(
    const SevenfoldProxy& proxy,
    const SevenfoldProxyTriangle& triangle,
    int team,
    bool spent
) noexcept {
    const std::uint8_t* source = triangle.material == 0
        ? team_colours[team]
        : sevenfold_style_materials[proxy.style][triangle.material];
    std::uint32_t light = 128U + triangle.shade / 2U;
    if (spent) light = light * 5U / 8U;
    return scaled_colour(source, light);
}

void build_proxy_at(
    T3DVertPacked* into,
    int clear_packed_vertices,
    int& triangle_count,
    const SevenfoldProxy& proxy,
    std::int16_t origin_x,
    std::int16_t origin_y,
    std::int16_t origin_z,
    int team,
    int quarter_turn,
    int lean,
    bool spent
) {
    for (int index = 0; index < clear_packed_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;

    // A dark footprint grounds the model; the smaller team diamond remains
    // visible even when a style's own palette is close to blue or red.
    const std::int16_t shadow[4][3] = {
        {static_cast<std::int16_t>(origin_x - 19),
         static_cast<std::int16_t>(origin_y + 1),
         static_cast<std::int16_t>(origin_z - 12)},
        {static_cast<std::int16_t>(origin_x + 19),
         static_cast<std::int16_t>(origin_y + 1),
         static_cast<std::int16_t>(origin_z - 12)},
        {static_cast<std::int16_t>(origin_x + 19),
         static_cast<std::int16_t>(origin_y + 1),
         static_cast<std::int16_t>(origin_z + 12)},
        {static_cast<std::int16_t>(origin_x - 19),
         static_cast<std::int16_t>(origin_y + 1),
         static_cast<std::int16_t>(origin_z + 12)},
    };
    emit_quad(into, written, shadow, rgba(18, 20, 24));
    const std::int16_t team_mark[4][3] = {
        {origin_x, static_cast<std::int16_t>(origin_y + 2),
         static_cast<std::int16_t>(origin_z - 11)},
        {static_cast<std::int16_t>(origin_x + 12),
         static_cast<std::int16_t>(origin_y + 2), origin_z},
        {origin_x, static_cast<std::int16_t>(origin_y + 2),
         static_cast<std::int16_t>(origin_z + 11)},
        {static_cast<std::int16_t>(origin_x - 12),
         static_cast<std::int16_t>(origin_y + 2), origin_z},
    };
    emit_quad(
        into, written, team_mark,
        spent ? rgba(92, 92, 96)
              : rgba(
                    team_colours[team][0], team_colours[team][1],
                    team_colours[team][2]
                )
    );

    for (int index = 0; index < proxy.triangles_count; ++index) {
        const SevenfoldProxyTriangle& triangle = proxy.triangles[index];
        const std::uint16_t corners[3] = {
            triangle.a, triangle.b, triangle.c,
        };
        const std::uint32_t colour =
            triangle_colour(proxy, triangle, team, spent);
        std::int16_t points[3][3]{};
        for (int corner = 0; corner < 3; ++corner) {
            const SevenfoldProxyVertex& source =
                proxy.vertices[corners[corner]];
            int x = source.x;
            int z = source.z;
            if (quarter_turn < 0) {
                x = source.z;
                z = -source.x;
            } else if (quarter_turn > 0) {
                x = -source.z;
                z = source.x;
            }
            x += source.y * lean / 256;
            points[corner][0] = static_cast<std::int16_t>(x + origin_x);
            points[corner][1] =
                static_cast<std::int16_t>(source.y + origin_y);
            points[corner][2] = static_cast<std::int16_t>(z + origin_z);
        }
        emit_triangle(into, written, points, colour);
    }
    triangle_count = written / 3;
}

void add_terrain_detail(
    T3DVertPacked* into,
    int& written,
    int column,
    int row,
    std::uint8_t kind,
    std::int16_t x0,
    std::int16_t x1,
    std::int16_t z0,
    std::int16_t z1,
    std::int16_t high,
    const std::uint8_t* rgb
) {
    const std::int16_t centre_x = static_cast<std::int16_t>(
        (x0 + x1) / 2 + (((column * 5 + row * 3) % 3) - 1) * 7
    );
    const std::int16_t centre_z = static_cast<std::int16_t>(
        (z0 + z1) / 2 + (((column * 3 + row * 7) % 3) - 1) * 6
    );
    if (kind == 2U || kind == 11U) {
        const std::int16_t trunk[4][3] = {
            {static_cast<std::int16_t>(centre_x - 3), high, centre_z},
            {static_cast<std::int16_t>(centre_x + 3), high, centre_z},
            {static_cast<std::int16_t>(centre_x + 3),
             static_cast<std::int16_t>(high + 25), centre_z},
            {static_cast<std::int16_t>(centre_x - 3),
             static_cast<std::int16_t>(high + 25), centre_z},
        };
        emit_quad(into, written, trunk, rgba(78, 57, 35));
        const std::int16_t canopy_a[3][3] = {
            {static_cast<std::int16_t>(centre_x - 14),
             static_cast<std::int16_t>(high + 17), centre_z},
            {static_cast<std::int16_t>(centre_x + 14),
             static_cast<std::int16_t>(high + 17), centre_z},
            {centre_x, static_cast<std::int16_t>(high + 48), centre_z},
        };
        const std::int16_t canopy_b[3][3] = {
            {centre_x, static_cast<std::int16_t>(high + 17),
             static_cast<std::int16_t>(centre_z - 14)},
            {centre_x, static_cast<std::int16_t>(high + 17),
             static_cast<std::int16_t>(centre_z + 14)},
            {centre_x, static_cast<std::int16_t>(high + 48), centre_z},
        };
        emit_triangle(into, written, canopy_a, scaled_colour(rgb, 205));
        emit_triangle(into, written, canopy_b, scaled_colour(rgb, 235));
    } else if (kind == 3U || kind == 7U) {
        const std::int16_t base = kind == 3U ? 20 : 14;
        const std::int16_t peak = kind == 3U ? 34 : 21;
        const std::int16_t north[3][3] = {
            {static_cast<std::int16_t>(centre_x - base), high,
             static_cast<std::int16_t>(centre_z - base)},
            {static_cast<std::int16_t>(centre_x + base), high,
             static_cast<std::int16_t>(centre_z - base)},
            {centre_x, static_cast<std::int16_t>(high + peak), centre_z},
        };
        const std::int16_t east[3][3] = {
            {static_cast<std::int16_t>(centre_x + base), high,
             static_cast<std::int16_t>(centre_z - base)},
            {static_cast<std::int16_t>(centre_x + base), high,
             static_cast<std::int16_t>(centre_z + base)},
            {centre_x, static_cast<std::int16_t>(high + peak), centre_z},
        };
        const std::int16_t south[3][3] = {
            {static_cast<std::int16_t>(centre_x + base), high,
             static_cast<std::int16_t>(centre_z + base)},
            {static_cast<std::int16_t>(centre_x - base), high,
             static_cast<std::int16_t>(centre_z + base)},
            {centre_x, static_cast<std::int16_t>(high + peak), centre_z},
        };
        const std::int16_t west[3][3] = {
            {static_cast<std::int16_t>(centre_x - base), high,
             static_cast<std::int16_t>(centre_z + base)},
            {static_cast<std::int16_t>(centre_x - base), high,
             static_cast<std::int16_t>(centre_z - base)},
            {centre_x, static_cast<std::int16_t>(high + peak), centre_z},
        };
        emit_triangle(into, written, north, scaled_colour(rgb, 190));
        emit_triangle(into, written, east, scaled_colour(rgb, 225));
        emit_triangle(into, written, south, scaled_colour(rgb, 165));
        emit_triangle(into, written, west, scaled_colour(rgb, 205));
    } else if (kind == 8U) {
        const std::int16_t wall[4][3] = {
            {static_cast<std::int16_t>(centre_x - 18), high, centre_z},
            {static_cast<std::int16_t>(centre_x + 18), high, centre_z},
            {static_cast<std::int16_t>(centre_x + 18),
             static_cast<std::int16_t>(high + 20), centre_z},
            {static_cast<std::int16_t>(centre_x - 18),
             static_cast<std::int16_t>(high + 29), centre_z},
        };
        emit_quad(into, written, wall, scaled_colour(rgb, 190));
    } else if (kind == 10U) {
        const std::int16_t stripe[4][3] = {
            {static_cast<std::int16_t>(x0 + 8),
             static_cast<std::int16_t>(high + 2),
             static_cast<std::int16_t>(z0 + 27)},
            {static_cast<std::int16_t>(x1 - 8),
             static_cast<std::int16_t>(high + 2),
             static_cast<std::int16_t>(z0 + 27)},
            {static_cast<std::int16_t>(x1 - 8),
             static_cast<std::int16_t>(high + 2),
             static_cast<std::int16_t>(z0 + 33)},
            {static_cast<std::int16_t>(x0 + 8),
             static_cast<std::int16_t>(high + 2),
             static_cast<std::int16_t>(z0 + 33)},
        };
        emit_quad(into, written, stripe, scaled_colour(rgb, 175));
    }
}

[[nodiscard]] int build_board(
    T3DVertPacked* into,
    const sim::EncounterSnapshot& snapshot,
    const std::vector<std::uint64_t>& terrain,
    const pr::Presentation& shown,
    std::uint8_t theme
) {
    for (int index = 0; index < packed_board_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int row = 0; row < board_rows && row < snapshot.height; ++row) {
        for (int column = 0;
             column < board_columns && column < snapshot.width; ++column) {
            const std::uint8_t kind =
                kind_at(snapshot, terrain, shown, column, row);
            const std::uint8_t* rgb = grandleon_terrain_flat[theme][kind];
            const std::int16_t x0 = static_cast<std::int16_t>(
                (column - board_columns / 2) * cell_world
            );
            const std::int16_t x1 = static_cast<std::int16_t>(x0 + cell_world);
            const std::int16_t z0 = static_cast<std::int16_t>(
                (row - board_rows / 2) * cell_world
            );
            const std::int16_t z1 = static_cast<std::int16_t>(z0 + cell_world);
            const std::int16_t high = static_cast<std::int16_t>(
                grandleon_terrain_elevation[kind] * height_world
            );
            const std::int16_t top[4][3] = {
                {x0, high, z0}, {x1, high, z0},
                {x1, high, z1}, {x0, high, z1},
            };
            emit_quad(
                into, written, top,
                scaled_colour(rgb, ((column + row) & 1) != 0 ? 238 : 255)
            );
            if (high > 0) {
                const std::int16_t front[4][3] = {
                    {x0, 0, z1}, {x1, 0, z1},
                    {x1, high, z1}, {x0, high, z1},
                };
                const std::int16_t right[4][3] = {
                    {x1, 0, z0}, {x1, 0, z1},
                    {x1, high, z1}, {x1, high, z0},
                };
                emit_quad(into, written, front, scaled_colour(rgb, 145));
                emit_quad(into, written, right, scaled_colour(rgb, 178));
            }
            const std::int16_t inset[4][3] = {
                {static_cast<std::int16_t>(x0 + 5),
                 static_cast<std::int16_t>(high + 1),
                 static_cast<std::int16_t>(z0 + 5)},
                {static_cast<std::int16_t>(x1 - 5),
                 static_cast<std::int16_t>(high + 1),
                 static_cast<std::int16_t>(z0 + 5)},
                {static_cast<std::int16_t>(x1 - 5),
                 static_cast<std::int16_t>(high + 1),
                 static_cast<std::int16_t>(z1 - 5)},
                {static_cast<std::int16_t>(x0 + 5),
                 static_cast<std::int16_t>(high + 1),
                 static_cast<std::int16_t>(z1 - 5)},
            };
            const int variation = (column * 17 + row * 29 + kind * 11) % 3;
            emit_quad(
                into, written, inset,
                scaled_colour(rgb, variation == 0 ? 218 : 232)
            );
            add_terrain_detail(
                into, written, column, row, kind, x0, x1, z0, z1, high, rgb
            );
        }
    }
    return written / 3;
}

[[nodiscard]] constexpr int silhouette_height(
    std::uint8_t kind
) noexcept {
    constexpr int heights[13] = {
        18, 32, 112, 142, 38, 68, 44, 82, 98, 48, 62, 130, 36,
    };
    return heights[kind];
}

[[nodiscard]] int progress_256(int frame, int frames) noexcept {
    return frames > 1 ? frame * 256 / (frames - 1) : 256;
}

[[nodiscard]] int arc(int progress, int peak) noexcept {
    const int folded = progress <= 128 ? progress : 256 - progress;
    return folded * peak / 128;
}

[[nodiscard]] int build_attack_scene(
    T3DVertPacked* into,
    const sim::EncounterSnapshot& snapshot,
    const std::vector<std::uint64_t>& terrain,
    const pr::Presentation& shown,
    std::uint8_t theme,
    sim::Position centre,
    view::AttackGesture gesture,
    int frame,
    int frames,
    bool resolving,
    bool landed
) {
    for (int index = 0; index < packed_attack_scene_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 6; ++column) {
            int source_x = centre.x + column - 3;
            int source_y = centre.y + row - 1;
            if (source_x < 0) source_x = 0;
            if (source_y < 0) source_y = 0;
            if (source_x >= snapshot.width) source_x = snapshot.width - 1;
            if (source_y >= snapshot.height) source_y = snapshot.height - 1;
            const std::uint8_t kind =
                kind_at(snapshot, terrain, shown, source_x, source_y);
            const std::uint8_t* rgb = grandleon_terrain_flat[theme][kind];
            const std::int16_t x0 = static_cast<std::int16_t>(
                (column - 3) * 70
            );
            const std::int16_t x1 = static_cast<std::int16_t>(x0 + 70);
            const std::int16_t z0 = static_cast<std::int16_t>((row - 1) * 70);
            const std::int16_t z1 = static_cast<std::int16_t>(z0 + 70);
            const std::int16_t floor[4][3] = {
                {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1}, {x0, 0, z1},
            };
            emit_quad(
                into, written, floor,
                scaled_colour(rgb, ((source_x + source_y) & 1) ? 232 : 255)
            );
            const std::int16_t inset[4][3] = {
                {static_cast<std::int16_t>(x0 + 5), 1,
                 static_cast<std::int16_t>(z0 + 5)},
                {static_cast<std::int16_t>(x1 - 5), 1,
                 static_cast<std::int16_t>(z0 + 5)},
                {static_cast<std::int16_t>(x1 - 5), 1,
                 static_cast<std::int16_t>(z1 - 5)},
                {static_cast<std::int16_t>(x0 + 5), 1,
                 static_cast<std::int16_t>(z1 - 5)},
            };
            emit_quad(into, written, inset, scaled_colour(rgb, 212));
        }
    }

    // The far row turns the authored terrain into a skyline instead of a flat
    // colour band. Mountains peak, forest and bamboo branch upward, and other
    // terrain becomes an irregular wall or distant ridge.
    for (int column = 0; column < board_columns; ++column) {
        int source_x = column;
        if (source_x >= snapshot.width) source_x = snapshot.width - 1;
        int source_y = centre.y;
        if (source_y < 0) source_y = 0;
        if (source_y >= snapshot.height) source_y = snapshot.height - 1;
        const std::uint8_t kind =
            kind_at(snapshot, terrain, shown, source_x, source_y);
        const std::uint8_t* rgb = grandleon_terrain_flat[theme][kind];
        const std::uint32_t colour = scaled_colour(rgb, 175);
        const std::int16_t x0 = static_cast<std::int16_t>(column * 52 - 208);
        const std::int16_t x1 = static_cast<std::int16_t>(x0 + 52);
        const std::int16_t middle = static_cast<std::int16_t>((x0 + x1) / 2);
        const std::int16_t height =
            static_cast<std::int16_t>(silhouette_height(kind));
        if (kind == 3U || kind == 7U) {
            const std::int16_t peak[3][3] = {
                {x0, 0, 235}, {x1, 0, 235}, {middle, height, 235},
            };
            emit_triangle(into, written, peak, colour);
        } else if (kind == 2U || kind == 11U) {
            const std::int16_t trunk[4][3] = {
                {static_cast<std::int16_t>(middle - 5), 0, 235},
                {static_cast<std::int16_t>(middle + 5), 0, 235},
                {static_cast<std::int16_t>(middle + 5),
                 static_cast<std::int16_t>(height * 2 / 3), 235},
                {static_cast<std::int16_t>(middle - 5),
                 static_cast<std::int16_t>(height * 2 / 3), 235},
            };
            emit_quad(into, written, trunk, rgba(67, 51, 32));
            const std::int16_t canopy[3][3] = {
                {x0, static_cast<std::int16_t>(height / 3), 235},
                {x1, static_cast<std::int16_t>(height / 3), 235},
                {middle, height, 235},
            };
            emit_triangle(into, written, canopy, colour);
        } else {
            const std::int16_t wall[4][3] = {
                {x0, 0, 235}, {x1, 0, 235},
                {x1, static_cast<std::int16_t>(height * 3 / 4), 235},
                {x0, height, 235},
            };
            emit_quad(into, written, wall, colour);
        }
    }

    // Gesture-specific geometry keeps shots and spells from looking like a
    // translated melee model. It is real scene geometry and therefore obeys
    // the same depth buffer as the combatants.
    const int progress = progress_256(frame, frames);
    int effect_x = 0;
    int effect_y = 64;
    std::uint32_t effect_colour = rgba(245, 234, 210);
    bool effect = false;
    if (!resolving && gesture == view::AttackGesture::shot) {
        effect_x = -58 + progress * 116 / 256;
        effect_y += arc(progress, 34);
        effect = true;
    } else if (!resolving && gesture == view::AttackGesture::cast) {
        effect_x = 68;
        effect_y = 38 + arc(progress, 46);
        effect_colour = rgba(126, 220, 255);
        effect = true;
    } else if (resolving) {
        effect_x = 60;
        effect_y = 58;
        effect_colour = landed ? rgba(255, 238, 158)
                               : rgba(242, 193, 78);
        effect = (frame & 1) == 0;
    }
    if (effect) {
        const int radius = resolving ? 13 : 7;
        const std::int16_t diamond[4][3] = {
            {static_cast<std::int16_t>(effect_x - radius),
             static_cast<std::int16_t>(effect_y), -18},
            {static_cast<std::int16_t>(effect_x),
             static_cast<std::int16_t>(effect_y + radius), -18},
            {static_cast<std::int16_t>(effect_x + radius),
             static_cast<std::int16_t>(effect_y), -18},
            {static_cast<std::int16_t>(effect_x),
             static_cast<std::int16_t>(effect_y - radius), -18},
        };
        emit_quad(into, written, diamond, effect_colour);
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

template <typename FigureT>
void draw_scene(
    surface_t* display,
    T3DViewport& viewport,
    T3DMat4FP* matrix,
    const T3DVertPacked* scene,
    int scene_triangles,
    const FigureT* figures,
    int figure_count,
    color_t clear_colour
) {
    rdpq_attach(display, display_get_zbuf());
    t3d_frame_start();
    rdpq_mode_zbuf(true, true);
    t3d_viewport_attach(&viewport);
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_screen_clear_color(clear_colour);
    t3d_screen_clear_depth();
    t3d_light_set_count(0);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT | T3D_FLAG_DEPTH
    ));
    t3d_matrix_push(matrix);
    draw_unindexed(scene, scene_triangles);
    for (int index = 0; index < figure_count; ++index) {
        draw_unindexed(figures[index].vertices, figures[index].triangles);
    }
    t3d_matrix_pop(1);
    t3d_tri_sync();
    rdpq_detach_wait();
}

}  // namespace

Campaign3DRenderer::Campaign3DRenderer() {
    T3DInitParams params{};
    t3d_init(params);
    board_ = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * packed_board_vertices)
    );
    attack_scene_ = static_cast<T3DVertPacked*>(
        malloc_uncached(
            sizeof(T3DVertPacked) * packed_attack_scene_vertices
        )
    );
    bool allocated = board_ != nullptr && attack_scene_ != nullptr;
    for (Figure& figure : figures_) {
        figure.vertices = static_cast<T3DVertPacked*>(
            malloc_uncached(
                sizeof(T3DVertPacked) * packed_low_proxy_vertices
            )
        );
        allocated = allocated && figure.vertices != nullptr;
    }
    for (Figure& figure : attack_figures_) {
        figure.vertices = static_cast<T3DVertPacked*>(
            malloc_uncached(
                sizeof(T3DVertPacked) * packed_high_proxy_vertices
            )
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

    attack_viewport_ = t3d_viewport_create();
    t3d_viewport_set_projection(
        &attack_viewport_, T3D_DEG_TO_RAD(43.0F), 16.0F, 900.0F
    );
    eye.v[0] = 0.0F;
    eye.v[1] = 126.0F;
    eye.v[2] = -286.0F;
    focus.v[0] = 0.0F;
    focus.v[1] = 60.0F;
    focus.v[2] = 18.0F;
    t3d_viewport_look_at(&attack_viewport_, &eye, &focus, &up);
    ready_ = true;
}

void Campaign3DRenderer::draw(
    surface_t* display,
    const sim::EncounterSnapshot& snapshot,
    const std::vector<std::uint64_t>& terrain,
    const pr::Presentation& shown,
    const view::Camera& camera,
    std::uint8_t theme,
    sim::UnitId skip_unit,
    sim::UnitId skip_other,
    const MotionPose* motion
) {
    static_cast<void>(camera);
    if (!ready_) return;
    if (theme >= grandleon_theme_count) theme = 0;
    board_triangles_ = build_board(board_, snapshot, terrain, shown, theme);

    int visible = 0;
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (visible >= maximum_figures) break;
        const bool moving = motion != nullptr && motion->unit == unit.id;
        if (!sim::on_board(unit) ||
            (unit.id == skip_unit && !moving) || unit.id == skip_other) {
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

        int elevation = 0;
        int quarter_turn = unit.side == sim::Side::first ? 1 : -1;
        int lean = 0;
        std::int16_t origin_x = world_x(unit.position.x);
        std::int16_t origin_z = world_z(unit.position.y);
        if (moving) {
            origin_x = world_x_fixed(motion->column_256);
            origin_z = world_z_fixed(motion->row_256);
            elevation = motion->height;
            quarter_turn = motion->quarter_turn;
            lean = motion->lean;
        } else {
            const std::uint8_t kind = kind_at(
                snapshot, terrain, shown,
                unit.position.x, unit.position.y
            );
            elevation = grandleon_terrain_elevation[kind] * height_world;
        }
        build_proxy_at(
            figures_[visible].vertices, packed_low_proxy_vertices,
            figures_[visible].triangles, *proxy,
            origin_x, static_cast<std::int16_t>(elevation), origin_z,
            unit.side == sim::Side::first ? 0 : 1,
            quarter_turn, lean, unit.has_acted
        );
        ++visible;
    }

    draw_scene(
        display, viewport_, matrix_, board_, board_triangles_, figures_,
        visible, RGBA32(20, 25, 35, 255)
    );
}

void Campaign3DRenderer::draw_attack(
    surface_t* display,
    const sim::EncounterSnapshot& snapshot,
    const std::vector<std::uint64_t>& terrain,
    const pr::Presentation& shown,
    std::uint8_t theme,
    const sim::UnitSnapshot& attacker,
    const sim::UnitSnapshot& defender,
    view::AttackGesture gesture,
    int frame,
    int frames,
    bool resolving,
    bool landed
) {
    if (!ready_) return;
    if (theme >= grandleon_theme_count) theme = 0;
    attack_scene_triangles_ = build_attack_scene(
        attack_scene_, snapshot, terrain, shown, theme, defender.position,
        gesture, frame, frames, resolving, landed
    );

    const int progress = progress_256(frame, frames);
    int attacker_x = -74;
    int defender_x = 74;
    int attacker_y = 8;
    int defender_y = 8;
    int attacker_lean = 0;
    int defender_lean = 0;
    if (gesture == view::AttackGesture::swing) {
        if (resolving) {
            attacker_x += (256 - progress) * 48 / 256;
            attacker_lean = (256 - progress) * 22 / 256;
        } else {
            attacker_x += progress * 48 / 256;
            attacker_lean = progress * 22 / 256;
        }
    } else if (!resolving) {
        attacker_y += arc(progress, gesture == view::AttackGesture::cast ? 5 : 3);
        attacker_lean = arc(progress, 10);
    }
    if (resolving && landed) {
        defender_x += arc(progress, 14);
        defender_y += arc(progress, 4);
        defender_lean = -arc(progress, 22);
    }

    const std::uint8_t attacker_style =
        shown.character_style_of_unit_type(attacker.unit_type_id);
    const std::uint8_t attacker_figure =
        shown.character_figure_of_unit_type(attacker.unit_type_id);
    const std::uint8_t attacker_archetype =
        shown.archetype_of_unit_type(attacker.unit_type_id);
    const SevenfoldProxy* attacker_proxy = sevenfold_campaign_proxy(
        attacker_style, attacker_figure, attacker_archetype, true
    );
    const std::uint8_t defender_style =
        shown.character_style_of_unit_type(defender.unit_type_id);
    const std::uint8_t defender_figure =
        shown.character_figure_of_unit_type(defender.unit_type_id);
    const std::uint8_t defender_archetype =
        shown.archetype_of_unit_type(defender.unit_type_id);
    const SevenfoldProxy* defender_proxy = sevenfold_campaign_proxy(
        defender_style, defender_figure, defender_archetype, true
    );
    int visible = 0;
    if (attacker_proxy != nullptr) {
        build_proxy_at(
            attack_figures_[visible].vertices, packed_high_proxy_vertices,
            attack_figures_[visible].triangles, *attacker_proxy,
            static_cast<std::int16_t>(attacker_x),
            static_cast<std::int16_t>(attacker_y), 0,
            attacker.side == sim::Side::first ? 0 : 1,
            1, attacker_lean, false
        );
        ++visible;
    }
    if (defender_proxy != nullptr) {
        build_proxy_at(
            attack_figures_[visible].vertices, packed_high_proxy_vertices,
            attack_figures_[visible].triangles, *defender_proxy,
            static_cast<std::int16_t>(defender_x),
            static_cast<std::int16_t>(defender_y), 0,
            defender.side == sim::Side::first ? 0 : 1,
            -1, defender_lean, false
        );
        ++visible;
    }

    const std::uint8_t terrain_kind = kind_at(
        snapshot, terrain, shown, defender.position.x, defender.position.y
    );
    const std::uint8_t* ground =
        grandleon_terrain_flat[theme][terrain_kind];
    draw_scene(
        display, attack_viewport_, matrix_, attack_scene_,
        attack_scene_triangles_, attack_figures_, visible,
        RGBA32(
            static_cast<std::uint8_t>(ground[0] / 3 + 12),
            static_cast<std::uint8_t>(ground[1] / 3 + 12),
            static_cast<std::uint8_t>(ground[2] / 3 + 18),
            255
        )
    );
}

}  // namespace grandleon::nintendo64
