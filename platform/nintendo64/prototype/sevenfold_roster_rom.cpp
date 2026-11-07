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
#include "sevenfold_backdrop_experiment.h"
#include "sevenfold_hybrid_experiment.h"
#include "sevenfold_presentation.h"
#include "sevenfold_roster_proxies.h"
#include "sevenfold_texture_experiment.h"

namespace {

namespace texture =
    grandleon::nintendo64::sevenfold_texture_experiment;
namespace backdrop =
    grandleon::nintendo64::sevenfold_backdrop_experiment;
namespace hybrid =
    grandleon::nintendo64::sevenfold_hybrid_experiment;

constexpr int cell_world = 64;
constexpr int board_triangles =
    sevenfold_board_columns * sevenfold_board_rows * 2;
constexpr int matched_board_columns = 6;
constexpr int matched_board_rows = 4;
constexpr int matched_surrounding_columns = 12;
constexpr int matched_surrounding_rows = 10;
constexpr int matched_active_origin_column = 3;
constexpr int matched_active_origin_row = 3;
constexpr int matched_cell_world = 48;
constexpr int matched_scene_triangles =
    matched_surrounding_columns * matched_surrounding_rows * 2;
constexpr std::uint8_t matched_terrain
    [matched_board_rows][matched_board_columns] = {
        {2, 2, 9, 12, 12, 2},
        {2, 9, 9, 12, 12, 2},
        {2, 9, 9, 12, 9, 2},
        {2, 2, 9, 12, 2, 2},
    };
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
constexpr int max_proxy_vertices = sevenfold_high_max_triangles * 3;
constexpr int packed_proxy_vertices = (max_proxy_vertices + 1) / 2;
constexpr int figure_count = 8;
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

[[nodiscard]] std::uint32_t triangle_colour(
    const SevenfoldProxy& proxy, const SevenfoldProxyTriangle& triangle,
    int team
) noexcept {
    const std::uint8_t* source = triangle.material == 0
        ? team_colours[team]
        : sevenfold_style_materials[proxy.style][triangle.material];
    const std::uint32_t light =
        grandleon::nintendo64::sevenfold_presentation::light_for(
            triangle.shade
        );
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
    std::int16_t origin_z, int team, int facing
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
        std::int16_t source_points[3][3]{};
        for (int corner = 0; corner < 3; ++corner) {
            const SevenfoldProxyVertex& source =
                proxy.vertices[corners[corner]];
            source_points[corner][0] = source.x;
            source_points[corner][1] = source.y;
            source_points[corner][2] = source.z;
        }
        const std::uint32_t colour =
            triangle_colour(proxy, triangle, team);
        for (int corner = 0; corner < 3; ++corner) {
            const SevenfoldProxyVertex& source =
                proxy.vertices[corners[corner]];
            const auto rotated =
                grandleon::nintendo64::sevenfold_presentation::three_quarter(
                    source.x, source.z, facing
                );
            const texture::UV uv = texture::character_uv(
                source_points, corner, triangle.material
            );
            set_vertex(
                into.vertices[written / 2], (written & 1) != 0,
                static_cast<std::int16_t>(rotated.x + origin_x),
                source.y,
                static_cast<std::int16_t>(rotated.z + origin_z),
                colour, uv.s, uv.t
            );
            ++written;
        }
    }
}

void build_proxy(
    Figure& into, const SevenfoldProxy& proxy, int column, int row, int team
) {
    build_proxy_at(
        into, proxy, cell_x(column), cell_z(row), team,
        team == 0 ? 1 : -1
    );
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

void scene_triangle(
    T3DVertPacked* into, int& written, const std::int16_t points[3][3],
    std::uint32_t colour, std::uint8_t texture_region = 1,
    std::uint8_t texture_variation = 0
) {
    for (int point = 0; point < 3; ++point) {
        const texture::UV uv = texture::terrain_uv(
            points, point, texture_region, texture_variation
        );
        set_vertex(
            into[written / 2], (written & 1) != 0,
            points[point][0], points[point][1], points[point][2], colour,
            uv.s, uv.t
        );
        ++written;
    }
}

void scene_quad(
    T3DVertPacked* into, int& written, const std::int16_t points[4][3],
    std::uint32_t colour, std::uint8_t texture_region = 1,
    std::uint8_t texture_variation = 0
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
    scene_triangle(
        into, written, first, colour, texture_region, texture_variation
    );
    scene_triangle(
        into, written, second, colour, texture_region, texture_variation
    );
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
            scene_quad(
                into, written, floor, colour,
                terrain_texture_region(terrain),
                terrain_variation(terrain, source_x, source_y)
            );
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
            scene_triangle(
                into, written, peak, colour,
                terrain_texture_region(terrain)
            );
        } else if (terrain == 2U || terrain == 11U) {
            const std::int16_t trunk[4][3] = {
                {static_cast<std::int16_t>(middle - 5), 0, 235},
                {static_cast<std::int16_t>(middle + 5), 0, 235},
                {static_cast<std::int16_t>(middle + 5),
                 static_cast<std::int16_t>(height * 2 / 3), 235},
                {static_cast<std::int16_t>(middle - 5),
                 static_cast<std::int16_t>(height * 2 / 3), 235},
            };
            scene_quad(into, written, trunk, rgba(67, 51, 32), 2);
            const std::int16_t canopy[3][3] = {
                {x0, static_cast<std::int16_t>(height / 3), 235},
                {x1, static_cast<std::int16_t>(height / 3), 235},
                {middle, height, 235},
            };
            scene_triangle(into, written, canopy, colour, 0);
        } else {
            const std::int16_t wall[4][3] = {
                {x0, 0, 235}, {x1, 0, 235},
                {x1, static_cast<std::int16_t>(height * 3 / 4), 235},
                {x0, height, 235},
            };
            scene_quad(
                into, written, wall, colour,
                terrain_texture_region(terrain)
            );
        }
    }
    return written / 3;
}

[[nodiscard, maybe_unused]] int build_matched_attack_scene(
    T3DVertPacked* into
) {
    for (int index = 0; index < packed_board_vertices; ++index) {
        into[index] = T3DVertPacked{};
    }
    int written = 0;
    for (int row = 0; row < matched_board_rows; ++row) {
        for (int column = 0; column < matched_board_columns; ++column) {
            const std::uint8_t terrain = matched_terrain[row][column];
            const std::uint8_t* rgb = terrain_colours[terrain];
            const std::int16_t x0 = static_cast<std::int16_t>(
                (column - 3) * 70
            );
            const std::int16_t x1 = static_cast<std::int16_t>(x0 + 70);
            const std::int16_t z0 = static_cast<std::int16_t>((row - 1) * 70);
            const std::int16_t z1 = static_cast<std::int16_t>(z0 + 70);
            const std::int16_t floor[4][3] = {
                {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1}, {x0, 0, z1},
            };
            scene_quad(
                into, written, floor, rgba(rgb[0], rgb[1], rgb[2]),
                terrain_texture_region(terrain),
                terrain_variation(terrain, column, row)
            );
        }
    }
    for (int column = 0; column < matched_board_columns; ++column) {
        const std::uint8_t terrain = matched_terrain[0][column];
        const std::uint8_t* rgb = terrain_colours[terrain];
        const std::uint32_t colour = rgba(
            static_cast<std::uint8_t>(rgb[0] * 3U / 4U),
            static_cast<std::uint8_t>(rgb[1] * 3U / 4U),
            static_cast<std::uint8_t>(rgb[2] * 3U / 4U)
        );
        const std::int16_t x0 = static_cast<std::int16_t>(column * 70 - 210);
        const std::int16_t x1 = static_cast<std::int16_t>(x0 + 70);
        const std::int16_t middle = static_cast<std::int16_t>((x0 + x1) / 2);
        const std::int16_t height = static_cast<std::int16_t>(
            terrain_silhouette_height(terrain)
        );
        if (terrain == 2U) {
            const std::int16_t trunk[4][3] = {
                {static_cast<std::int16_t>(middle - 5), 0, 235},
                {static_cast<std::int16_t>(middle + 5), 0, 235},
                {static_cast<std::int16_t>(middle + 5),
                 static_cast<std::int16_t>(height * 2 / 3), 235},
                {static_cast<std::int16_t>(middle - 5),
                 static_cast<std::int16_t>(height * 2 / 3), 235},
            };
            scene_quad(into, written, trunk, rgba(67, 51, 32), 2);
            const std::int16_t canopy[3][3] = {
                {x0, static_cast<std::int16_t>(height / 3), 235},
                {x1, static_cast<std::int16_t>(height / 3), 235},
                {middle, height, 235},
            };
            scene_triangle(into, written, canopy, colour, 0);
        } else {
            const std::int16_t wall[4][3] = {
                {x0, 0, 235}, {x1, 0, 235},
                {x1, static_cast<std::int16_t>(height * 3 / 4), 235},
                {x0, height, 235},
            };
            scene_quad(
                into, written, wall, colour,
                terrain_texture_region(terrain)
            );
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

std::uint64_t draw_frame_textured(
    surface_t* display, T3DViewport* viewport, T3DMat4FP* matrix,
    const T3DVertPacked* scene, int scene_triangles, const Figure* figures,
    int visible_figures, const char* label, const char* footer,
    const char* controls, color_t clear_colour
) {
    const std::uint64_t started = get_ticks_us();
    rdpq_attach(display, display_get_zbuf());
    t3d_frame_start();
    t3d_viewport_attach(viewport);
    t3d_screen_clear_color(clear_colour);
    t3d_screen_clear_depth();
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
    t3d_tri_sync();
    texture::bind_character();
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

std::uint64_t draw_hybrid_frame(
    surface_t* display, T3DViewport* viewport, T3DMat4FP* matrix,
    const T3DVertPacked* scene, int scene_triangles, std::uint8_t terrain,
    hybrid::Style style, const hybrid::Frame& hybrid_frame, const char* label,
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
    hybrid::draw(style, hybrid_frame);
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

std::uint64_t draw_tactical_hybrid_frame(
    surface_t* display, T3DViewport* viewport, T3DMat4FP* matrix,
    const T3DVertPacked* board, const hybrid::TacticalFrame& tactical_frame,
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
    hybrid::draw_tactical(tactical_frame);
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
    hybrid::draw_matched_board(left_health, right_health, focus);
    rdpq_detach_wait();
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
            "A LOD <> STYLE ^V BODY Z MATERIAL",
            RGBA32(20, 25, 35, 255)
        );
        display_show(display);
    }
    if (!report) return elapsed;
    std::fprintf(
        stderr,
        "sevenfold stage=%s body=%s lod=%s models=8 triangles=%d us=%llu fps=%llu heading=three-quarter depth=on\n",
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

std::uint64_t show_tactical_comparison(
    int stage, bool textured, bool report, T3DViewport& viewport,
    T3DMat4FP* matrix, T3DVertPacked* board, Figure* figures
) {
    build_board(board, stage);
    apply_tactical_highlights(board);
    constexpr int positions[8][2] = {
        {1, 1}, {3, 1}, {5, 1}, {7, 1},
        {1, 4}, {3, 4}, {5, 4}, {7, 4},
    };
    for (int archetype = 0; archetype < figure_count; ++archetype) {
        const int body = archetype & 1;
        const int team = (archetype + stage) & 1;
        build_proxy(
            figures[archetype],
            sevenfold_proxies_low[stage][body][archetype],
            positions[archetype][0], positions[archetype][1], team
        );
    }
    char page_label[64];
    std::snprintf(
        page_label, sizeof page_label, "%s TACTICAL - %s",
        sevenfold_stage_names[stage], textured ? "CI4 MATERIAL" : "FLAT"
    );
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 3; ++frame) {
        surface_t* display = display_get();
        if (textured) {
            elapsed = draw_frame_textured(
                display, &viewport, matrix, board, board_triangles, figures,
                figure_count, page_label, "8 ROLES + MIXED BODIES/TERRAIN",
                "Z FLAT - BLUE + GOLD HIGHLIGHTS",
                RGBA32(20, 25, 35, 255)
            );
        } else {
            elapsed = draw_frame(
                display, &viewport, matrix, board, board_triangles, figures,
                figure_count, page_label, "8 ROLES + MIXED BODIES/TERRAIN",
                "Z MATERIAL - BLUE + GOLD HIGHLIGHTS",
                RGBA32(20, 25, 35, 255)
            );
        }
        display_show(display);
    }
    if (!report) return elapsed;
    std::fprintf(
        stderr,
        "sevenfold tactical stage=%s mode=%s models=8 bodies=mixed triangles=%d us=%llu fps=%llu highlights=blue-and-gold depth=on\n",
        sevenfold_style_names[stage], textured ? "ci4" : "flat",
        triangle_total(figures),
        static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(elapsed > 0 ? 1'000'000 / elapsed : 0)
    );
    std::fprintf(
        stderr, "CHECKPOINT sevenfold-tactical-%s-%s\n",
        sevenfold_style_names[stage], textured ? "after" : "before"
    );
    std::fflush(stderr);
    return elapsed;
}

std::uint64_t show_tactical_hybrid(
    bool report, T3DViewport& viewport, T3DMat4FP* matrix,
    T3DVertPacked* board
) {
    constexpr int stage = 0;
    build_board(board, stage);
    apply_tactical_highlights(board);
    const hybrid::TacticalFrame tactical_frame{0, 0, 0, 0};
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 3; ++frame) {
        surface_t* display = display_get();
        elapsed = draw_tactical_hybrid_frame(
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
        "sevenfold tactical-hybrid stage=medieval models=8 roles=8 bodies=mixed teams=blue-red scene_triangles=%d us=%llu fps=%llu tmem_peak=%lu embedded=%lu terrain_tmem=%lu terrain_embedded=%lu props=edge-sparse variation=deterministic cluster=transition-tone ordering=foot-depth highlights=blue-and-gold\n",
        board_triangles, static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(elapsed > 0 ? 1'000'000 / elapsed : 0),
        static_cast<unsigned long>(hybrid::tactical_peak_texture_bytes()),
        static_cast<unsigned long>(hybrid::tactical_embedded_bytes()),
        static_cast<unsigned long>(texture::terrain_peak_texture_bytes()),
        static_cast<unsigned long>(texture::terrain_embedded_bytes())
    );
    std::fprintf(stderr, "CHECKPOINT sevenfold-tactical-hybrid-medieval\n");
    std::fflush(stderr);
    return elapsed;
}

std::uint64_t play_tactical_hybrid_move(
    bool report, T3DViewport& viewport, T3DMat4FP* matrix,
    T3DVertPacked* board
) {
    constexpr int stage = 0;
    build_board(board, stage);
    apply_tactical_highlights(board);
    if (report) {
        surface_t* display = display_get();
        draw_tactical_hybrid_frame(
            display, &viewport, matrix, board,
            hybrid::TacticalFrame{0, 0, 0, 0},
            "Medieval Gate TACTICAL - 2.5D MOVE",
            "ROGUE MOVES INTO SELECTED TILE",
            "ANTICIPATE - WALK - SETTLE"
        );
        display_show(display);
        std::fprintf(
            stderr, "CHECKPOINT sevenfold-tactical-hybrid-move-start\n"
        );
        std::fflush(stderr);
        wait_ms(700);
    }
    std::uint64_t slowest = 0;
    for (int frame = 0; frame < 72; ++frame) {
        hybrid::TacticalFrame tactical_frame{0, 0, 0, 0};
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
        const std::uint64_t elapsed = draw_tactical_hybrid_frame(
            display, &viewport, matrix, board, tactical_frame,
            "Medieval Gate TACTICAL - 2.5D MOVE",
            "ROGUE MOVES INTO SELECTED TILE",
            "ANTICIPATE - EASED WALK - SETTLE"
        );
        display_show(display);
        if (elapsed > slowest) slowest = elapsed;
        if (report && frame == 64) {
            std::fprintf(
                stderr, "CHECKPOINT sevenfold-tactical-hybrid-move-end\n"
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
            "sevenfold tactical-hybrid-animation stage=medieval frames=72 max_us=%llu fps=%llu motion=tile-move poses=idle2-walk2 easing=smoothstep beats=anticipation-move-settle ordering=foot-depth\n",
            static_cast<unsigned long long>(slowest),
            static_cast<unsigned long long>(
                slowest > 0 ? 1'000'000 / slowest : 0
            )
        );
        std::fflush(stderr);
    }
    return slowest;
}

std::uint64_t show_heading_page(
    int team, bool report, T3DViewport& viewport, T3DMat4FP* matrix,
    T3DVertPacked* board, Figure* figures
) {
    constexpr int positions[8][2] = {
        {1, 1}, {3, 1}, {5, 1}, {7, 1},
        {1, 4}, {3, 4}, {5, 4}, {7, 4},
    };
    constexpr int comparison_stage = 0;
    constexpr int comparison_body = 0;
    build_board(board, comparison_stage);
    for (int archetype = 0; archetype < figure_count; ++archetype) {
        build_proxy(
            figures[archetype],
            sevenfold_proxies_low[comparison_stage][comparison_body]
                                     [archetype],
            positions[archetype][0], positions[archetype][1], team
        );
    }

    const char* side = team == 0 ? "BLUE" : "RED";
    const char* direction = team == 0 ? "RIGHT" : "LEFT";
    char page_label[64];
    char footer[64];
    std::snprintf(
        page_label, sizeof page_label,
        "MEDIEVAL GATE - %s THREE-QUARTER", side
    );
    std::snprintf(
        footer, sizeof footer, "8 LOW-LOD ROLES - %s FACING", direction
    );
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 3; ++frame) {
        surface_t* display = display_get();
        elapsed = draw_frame(
            display, &viewport, matrix, board, board_triangles, figures,
            figure_count, page_label, footer, "ROLE READABILITY CAPTURE",
            RGBA32(20, 25, 35, 255)
        );
        display_show(display);
    }
    if (!report) return elapsed;
    std::fprintf(
        stderr,
        "sevenfold heading=%s team=%s models=8 triangles=%d us=%llu fps=%llu heading=three-quarter depth=on\n",
        team == 0 ? "right" : "left", team == 0 ? "blue" : "red",
        triangle_total(figures),
        static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(elapsed > 0 ? 1'000'000 / elapsed : 0)
    );
    std::fprintf(
        stderr, "CHECKPOINT sevenfold-heading-%s\n",
        team == 0 ? "blue" : "red"
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
            "A ROLE B ATTACK Z VIEW <> STYLE ^V TILE",
            RGBA32(sky[0], sky[1], sky[2], 255)
        );
        display_show(display);
    }
    if (!report) return elapsed;
    std::fprintf(
        stderr,
        "sevenfold attack stage=%s role=%s terrain=%s models=2 triangles=%d us=%llu fps=%llu heading=three-quarter depth=on\n",
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

std::uint64_t show_texture_comparison(
    int stage, int archetype, int selected_cell, bool textured, bool report,
    T3DViewport& viewport, T3DMat4FP* matrix, T3DVertPacked* scene,
    Figure* figures
) {
    const int scene_triangles =
        build_attack_scene(scene, stage, selected_cell);
    build_attack_figures(figures, stage, archetype, -68, 68);
    const std::uint8_t terrain = sevenfold_stage_terrain[stage][selected_cell];
    const std::uint8_t* sky = terrain_sky_colours[terrain];
    char page_label[64];
    char footer[64];
    std::snprintf(
        page_label, sizeof page_label, "%s %s - %s",
        sevenfold_stage_names[stage], sevenfold_archetype_names[archetype],
        textured ? "CI4 AFTER" : "FLAT BEFORE"
    );
    std::snprintf(
        footer, sizeof footer, "%s - %s",
        terrain_names[terrain],
        textured ? "TEXTURED TERRAIN + FIGURES" : "VERTEX COLOUR BASELINE"
    );
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 3; ++frame) {
        surface_t* display = display_get();
        if (textured) {
            elapsed = draw_frame_textured(
                display, &viewport, matrix, scene, scene_triangles,
                figures, 2, page_label, footer,
                "Z HYBRID B ATTACK - TEAM COLOUR KEPT",
                RGBA32(sky[0], sky[1], sky[2], 255)
            );
        } else {
            elapsed = draw_frame(
                display, &viewport, matrix, scene, scene_triangles,
                figures, 2, page_label, footer,
                "SAME CAMERA + MODELS + GEOMETRY",
                RGBA32(sky[0], sky[1], sky[2], 255)
            );
        }
        display_show(display);
    }
    if (!report) return elapsed;
    const char* mode = textured ? "ci4" : "flat";
    std::fprintf(
        stderr,
        "sevenfold texture stage=%s role=%s terrain=%s mode=%s models=2 triangles=%d us=%llu fps=%llu depth=on\n",
        sevenfold_style_names[stage], sevenfold_archetype_names[archetype],
        terrain_names[terrain], mode,
        triangle_total(figures, 2, scene_triangles),
        static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(elapsed > 0 ? 1'000'000 / elapsed : 0)
    );
    std::fprintf(
        stderr, "CHECKPOINT sevenfold-texture-%s-%s\n",
        sevenfold_style_names[stage], textured ? "after" : "before"
    );
    std::fflush(stderr);
    return elapsed;
}

[[nodiscard]] hybrid::Style hybrid_style(int stage) noexcept {
    return stage == 2 ? hybrid::Style::mythical : hybrid::Style::medieval;
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

[[nodiscard]] hybrid::Frame neutral_hybrid_frame(bool alternate = false) {
    const hybrid::Pose pose = alternate
        ? hybrid::Pose::idle_b : hybrid::Pose::idle_a;
    return {pose, pose, -1, 0, 0, 0, 0};
}

[[nodiscard]] hybrid::Frame strike_hybrid_frame(
    int frame, bool counter
) {
    hybrid::Frame pose = neutral_hybrid_frame(frame >= 108);
    hybrid::Pose& attacker = counter ? pose.right_pose : pose.left_pose;
    hybrid::Pose& defender = counter ? pose.left_pose : pose.right_pose;
    int& push = counter ? pose.right_push : pose.left_push;
    int& recoil = counter ? pose.left_recoil : pose.right_recoil;
    const int target = counter ? 0 : 1;
    if (frame >= 24 && frame < 48) {
        attacker = hybrid::Pose::windup;
        push = -eased_distance(frame - 24, 23, 4);
    } else if (frame >= 48 && frame < 72) {
        attacker = hybrid::Pose::follow_through;
        push = -4 + eased_distance(frame - 48, 23, 46);
    } else if (frame >= 72 && frame < 84) {
        attacker = hybrid::Pose::follow_through;
        defender = hybrid::Pose::hurt;
        push = 42;
        recoil = eased_distance(frame - 72, 11, 8);
        // Keep the impact mark for the full hurt beat. The matched 30 fps
        // showcase samples this 123-frame move at 2x speed, so a six-frame
        // source flash could otherwise collapse to only three presented
        // frames and disappear between native capture samples.
        pose.impact_target = target;
    } else if (frame >= 84 && frame < 108) {
        attacker = frame < 96
            ? hybrid::Pose::follow_through : hybrid::Pose::idle_b;
        defender = frame < 100
            ? hybrid::Pose::hurt : hybrid::Pose::idle_b;
        push = 42 - eased_distance(frame - 84, 23, 42);
        recoil = 8 - eased_distance(frame - 84, 23, 8);
    }
    return pose;
}

std::uint64_t show_hybrid_close(
    int stage, bool impact, bool report, T3DViewport& viewport,
    T3DMat4FP* matrix, T3DVertPacked* scene
) {
    const int selected_cell = preferred_attack_cell(stage);
    const int scene_triangles =
        build_attack_scene(scene, stage, selected_cell);
    const std::uint8_t terrain =
        sevenfold_stage_terrain[stage][selected_cell];
    const std::uint8_t* sky = terrain_sky_colours[terrain];
    const hybrid::Frame hybrid_frame = impact
        ? hybrid::Frame{
            hybrid::Pose::follow_through, hybrid::Pose::hurt, 1,
            42, 0, 0, 8,
        }
        : neutral_hybrid_frame();
    char page_label[64];
    std::snprintf(
        page_label, sizeof page_label, "%s - CURATED CI4 %s",
        sevenfold_stage_names[stage], impact ? "IMPACT" : "IDLE"
    );
    std::uint64_t elapsed = 0;
    for (int frame = 0; frame < 3; ++frame) {
        surface_t* display = display_get();
        elapsed = draw_hybrid_frame(
            display, &viewport, matrix, scene, scene_triangles,
            terrain, hybrid_style(stage), hybrid_frame, page_label,
            "2.5D CUTOUTS + TEXTURED 3D TERRAIN",
            "BLUE/RED MASKS - SHADOWS + EFFECT",
            RGBA32(sky[0], sky[1], sky[2], 255)
        );
        display_show(display);
    }
    if (!report) return elapsed;
    std::fprintf(
        stderr,
        "sevenfold hybrid stage=%s pose=%s terrain=%s sprites=2 scene_triangles=%d us=%llu fps=%llu tmem_peak=%lu embedded=%lu terrain_tmem=%lu terrain_embedded=%lu props=terrain-specific anchors=locked team=blue-red\n",
        sevenfold_style_names[stage], impact ? "impact" : "idle",
        terrain_names[terrain], scene_triangles,
        static_cast<unsigned long long>(elapsed),
        static_cast<unsigned long long>(elapsed > 0 ? 1'000'000 / elapsed : 0),
        static_cast<unsigned long>(hybrid::peak_texture_bytes()),
        static_cast<unsigned long>(hybrid::embedded_bytes()),
        static_cast<unsigned long>(texture::terrain_peak_texture_bytes()),
        static_cast<unsigned long>(texture::terrain_embedded_bytes())
    );
    std::fprintf(
        stderr, "CHECKPOINT sevenfold-hybrid-%s-%s\n",
        sevenfold_style_names[stage], impact ? "impact" : "idle"
    );
    std::fflush(stderr);
    return elapsed;
}

std::uint64_t play_hybrid_preview(
    int stage, T3DViewport& viewport, T3DMat4FP* matrix,
    T3DVertPacked* scene, bool report
) {
    const int selected_cell = preferred_attack_cell(stage);
    const int scene_triangles =
        build_attack_scene(scene, stage, selected_cell);
    const std::uint8_t terrain =
        sevenfold_stage_terrain[stage][selected_cell];
    const std::uint8_t* sky = terrain_sky_colours[terrain];
    char page_label[64];
    std::snprintf(
        page_label, sizeof page_label, "%s - FRONTAL ATTACK",
        sevenfold_stage_names[stage]
    );
    if (report) {
        surface_t* display = display_get();
        draw_hybrid_frame(
            display, &viewport, matrix, scene, scene_triangles,
            terrain, hybrid_style(stage), neutral_hybrid_frame(), page_label,
            "ANTICIPATION - IMPACT - COUNTER - SETTLE",
            "CURATED POSES ON TEXTURED 3D TERRAIN",
            RGBA32(sky[0], sky[1], sky[2], 255)
        );
        display_show(display);
        std::fprintf(stderr, "CHECKPOINT sevenfold-hybrid-animation-start\n");
        std::fflush(stderr);
        wait_ms(700);
    }
    std::uint64_t slowest = 0;
    for (int frame = 0; frame < 246; ++frame) {
        const bool counter = frame >= 123;
        const hybrid::Frame pose = strike_hybrid_frame(
            counter ? frame - 123 : frame, counter
        );
        surface_t* display = display_get();
        const std::uint64_t elapsed = draw_hybrid_frame(
            display, &viewport, matrix, scene, scene_triangles,
            terrain, hybrid_style(stage), pose, page_label,
            "ANTICIPATION - IMPACT - COUNTER - SETTLE",
            "5 CURATED POSES EACH - FIXED SIDES",
            RGBA32(sky[0], sky[1], sky[2], 255)
        );
        display_show(display);
        if (elapsed > slowest) slowest = elapsed;
        if (report && frame == 72) {
            std::fprintf(stderr, "CHECKPOINT sevenfold-hybrid-animation-impact\n");
            std::fflush(stderr);
            wait_ms(700);
        } else if (report && frame == 195) {
            std::fprintf(
                stderr, "CHECKPOINT sevenfold-hybrid-animation-counter\n"
            );
            std::fflush(stderr);
            wait_ms(700);
        } else {
            wait_ms(16);
        }
    }
    if (report) {
        std::fprintf(
            stderr,
            "sevenfold hybrid-animation stage=%s frames=246 max_us=%llu fps=%llu anchors=locked counter=right-to-left poses=5 easing=smoothstep motion=anticipation-impact-recoil-settle camera=frontal\n",
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
            const hybrid::Frame pose = strike_hybrid_frame(
                local * 2, counter
            );
            constexpr int close_triangles = 0;
            elapsed = draw_hybrid_frame(
                display, &attack_viewport, matrix, scene, close_triangles,
                2U, hybrid::Style::medieval, pose,
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
                elapsed = draw_hybrid_frame(
                    display, &attack_viewport, matrix, scene, close_triangles,
                    2U, hybrid::Style::medieval, neutral_hybrid_frame(true),
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
    bool anchors_locked = true;
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
        anchors_locked = anchors_locked && left_x <= 0 && right_x >= 0;
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
            "sevenfold attack-animation stage=%s frames=96 max_us=%llu fps=%llu anchors=%s counter=right-to-left heading=three-quarter depth=on\n",
            sevenfold_style_names[stage],
            static_cast<unsigned long long>(slowest),
            static_cast<unsigned long long>(
                slowest > 0 ? 1'000'000 / slowest : 0
            ), anchors_locked ? "locked" : "VIOLATED"
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
    const bool terrain_ready = texture::initialize();
    const bool backdrop_ready = backdrop::initialize();
    const bool hybrid_ready = hybrid::initialize();
    expect(
        terrain_ready,
        "original CI4 material and prop atlases allocate"
    );
    expect(
        texture::terrain_peak_texture_bytes() <= 4096U,
        "each sixty-four-pixel terrain atlas fits four KiB of TMEM"
    );
    std::fprintf(
        stderr,
        "sevenfold terrain-quality material=64x64-ci4 regions=4 prop=64x64-ci4 cells=4 tmem_peak=%lu embedded=%lu variation=deterministic transitions=cluster-tone provenance=repo-generated\n",
        static_cast<unsigned long>(texture::terrain_peak_texture_bytes()),
        static_cast<unsigned long>(texture::terrain_embedded_bytes())
    );
    std::fflush(stderr);
    expect(
        backdrop_ready && backdrop::select_variant(matched_backdrop) == 2,
        "three shared-plate backdrop variants allocate and select deterministically"
    );
    expect(
        backdrop::peak_texture_bytes() <= 4096U,
        "each streamed backdrop tile fits four KiB of TMEM"
    );
    std::fprintf(
        stderr,
        "sevenfold backdrop pack=forest-grass-paved orientation=north-south variants=3 selected=2 tiles=24 tile=80x40-ci4 composite=320x240 tmem_peak=%lu embedded=%lu rdram=%lu frozen=yes floor=pre-rendered-full-frame source=godot-plates\n",
        static_cast<unsigned long>(backdrop::peak_texture_bytes()),
        static_cast<unsigned long>(backdrop::embedded_bytes()),
        static_cast<unsigned long>(backdrop::rdram_bytes())
    );
    std::fflush(stderr);
    expect(
        matched_layout_hash() == 0x66909BD6U,
        "schema-two twelve-by-ten surroundings and active origin match"
    );
    std::fprintf(
        stderr,
        "sevenfold showcase-contract schema=2 surroundings=12x10 active-origin=3,3 layout_hash=66909bd6 diagnostics=none\n"
    );
    std::fflush(stderr);
    expect(
        hybrid_ready,
        "close and eight-role tactical cutouts plus effects allocate"
    );
    expect(
        hybrid::peak_texture_bytes() <= 4096U &&
            hybrid::tactical_peak_texture_bytes() <= 4096U,
        "CI4 close and CI8 tactical cutouts each fit four KiB of TMEM"
    );

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
    if (!allocated || !hybrid_ready || !terrain_ready || !backdrop_ready) {
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
    T3DViewport matched_viewport = t3d_viewport_create();
    t3d_viewport_set_projection(
        &matched_viewport, T3D_DEG_TO_RAD(35.0F), 16.0F, 1300.0F
    );
    set_matched_camera(matched_viewport);
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
    for (int team = 0; team < 2; ++team) {
        const std::uint64_t elapsed = show_heading_page(
            team, true, viewport, matrix, board, figures
        );
        char message[112];
        std::snprintf(
            message, sizeof message,
            "%s three-quarter eight-role comparison stays at 30 fps",
            team == 0 ? "blue/right-facing" : "red/left-facing"
        );
        expect(elapsed <= frame_budget_us, message);
        wait_ms(1500);
    }
    constexpr int tactical_stages[] = {0, 6};
    for (int tactical_stage : tactical_stages) {
        for (int mode = 0; mode < 2; ++mode) {
            const bool textured = mode != 0;
            const std::uint64_t elapsed = show_tactical_comparison(
                tactical_stage, textured, true, viewport, matrix, board,
                figures
            );
            char message[112];
            std::snprintf(
                message, sizeof message,
                "%s tactical %s overview stays at 30 fps",
                sevenfold_style_names[tactical_stage],
                textured ? "CI4" : "flat"
            );
            expect(elapsed <= frame_budget_us, message);
            wait_ms(1500);
        }
    }
    const std::uint64_t tactical_hybrid_elapsed = show_tactical_hybrid(
        true, viewport, matrix, board
    );
    expect(
        tactical_hybrid_elapsed <= frame_budget_us,
        "the eight-role tactical 2.5D board stays at 30 fps"
    );
    wait_ms(1500);
    const std::uint64_t tactical_move_elapsed = play_tactical_hybrid_move(
        true, viewport, matrix, board
    );
    expect(
        tactical_move_elapsed <= frame_budget_us,
        "the tactical 2.5D tile movement stays at 30 fps"
    );
    constexpr int texture_comparisons[2][2] = {
        {0, 0},  // Medieval knight on forest.
        {2, 2},  // Mythical mage on mountain.
    };
    for (const auto& comparison : texture_comparisons) {
        const int comparison_stage = comparison[0];
        const int comparison_archetype = comparison[1];
        for (int mode = 0; mode < 2; ++mode) {
            const bool textured = mode != 0;
            const std::uint64_t elapsed = show_texture_comparison(
                comparison_stage, comparison_archetype,
                preferred_attack_cell(comparison_stage), textured, true,
                attack_viewport, matrix, board, figures
            );
            char message[112];
            std::snprintf(
                message, sizeof message,
                "%s %s texture comparison stays at 30 fps",
                sevenfold_style_names[comparison_stage],
                textured ? "CI4" : "flat"
            );
            expect(elapsed <= frame_budget_us, message);
            wait_ms(1500);
        }
    }
    constexpr int hybrid_stages[] = {0, 2};
    for (int hybrid_stage : hybrid_stages) {
        for (int pose = 0; pose < 2; ++pose) {
            const bool impact = pose != 0;
            const std::uint64_t elapsed = show_hybrid_close(
                hybrid_stage, impact, true, attack_viewport, matrix, board
            );
            char message[112];
            std::snprintf(
                message, sizeof message,
                "%s curated CI4 %s close view stays at 30 fps",
                sevenfold_style_names[hybrid_stage],
                impact ? "impact" : "idle"
            );
            expect(elapsed <= frame_budget_us, message);
            wait_ms(1500);
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
    const std::uint64_t hybrid_animation_elapsed = play_hybrid_preview(
        0, attack_viewport, matrix, board, true
    );
    expect(
        hybrid_animation_elapsed <= frame_budget_us,
        "the frontal hybrid push, impact and recoil stays at 30 fps"
    );
    const std::uint64_t matched_sequence_elapsed = play_matched_readme_sequence(
        matched_viewport, attack_viewport, matrix, board, true
    );
    expect(
        matched_sequence_elapsed <= frame_budget_us,
        "the matched 8.15-second renderer-comparison scene stays at 30 fps"
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
    int close_mode = 0;
    int tactical_mode = 0;
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
            if (stage != 0 && stage != 2 && close_mode == 2) close_mode = 1;
            if (stage != 0 && tactical_mode == 2) tactical_mode = 1;
            changed = true;
        }
        if (pressed.d_right) {
            stage = (stage + 1) % sevenfold_stage_count;
            if (attack_mode) selected_cell = preferred_attack_cell(stage);
            if (stage != 0 && stage != 2 && close_mode == 2) close_mode = 1;
            if (stage != 0 && tactical_mode == 2) tactical_mode = 1;
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
        if (attack_mode && pressed.z) {
            const int modes = stage == 0 || stage == 2 ? 3 : 2;
            close_mode = (close_mode + 1) % modes;
            changed = true;
        } else if (!attack_mode && pressed.z) {
            const int modes = stage == 0 ? 3 : 2;
            tactical_mode = (tactical_mode + 1) % modes;
            changed = true;
        }
        if (attack_mode && pressed.b) {
            if (close_mode == 2) {
                play_hybrid_preview(
                    stage, attack_viewport, matrix, board, false
                );
            } else {
                play_attack_preview(
                    stage, archetype, selected_cell, attack_viewport,
                    matrix, board, figures, false
                );
            }
            changed = true;
        } else if (!attack_mode && pressed.b && tactical_mode == 2) {
            play_tactical_hybrid_move(false, viewport, matrix, board);
            changed = true;
        }
        if (changed) {
            if (attack_mode) {
                if (close_mode == 2) {
                    show_hybrid_close(
                        stage, false, false, attack_viewport, matrix, board
                    );
                } else if (close_mode == 1) {
                    show_texture_comparison(
                        stage, archetype, selected_cell, true, false,
                        attack_viewport, matrix, board, figures
                    );
                } else {
                    show_attack(
                        stage, archetype, selected_cell, false,
                        attack_viewport, matrix, board, figures
                    );
                }
            } else {
                if (tactical_mode == 2) {
                    show_tactical_hybrid(false, viewport, matrix, board);
                } else if (tactical_mode == 1) {
                    show_tactical_comparison(
                        stage, true, false, viewport, matrix, board, figures
                    );
                } else {
                    show_page(
                        stage, body, lod, false, viewport, matrix, board,
                        figures
                    );
                }
            }
        }
        wait_ms(16);
    }
}
