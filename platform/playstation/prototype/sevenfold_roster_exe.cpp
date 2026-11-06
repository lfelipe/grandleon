// SPDX-License-Identifier: MIT
// Sevenfold's complete low-poly roster on the original PlayStation.
//
// The strategic view deliberately uses low-detail figures over a simple 3D
// board. The close attack view uses the high-detail pair: measurements showed
// that this hybrid keeps the recognisable models where they matter without
// pretending that a crowded, high-detail 3D board is a stable 30 fps scene on
// this hardware.

#include "psx_gpu.h"
#include "psx_pad.h"
#include "psx_runtime.h"

#include <grandleon/client/turn_client.hpp>
#include <grandleon/view/glyphs.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "n64_showcase.h"
#include "sevenfold_roster_proxies.h"

namespace {

namespace gpu = grandleon::playstation::gpu;
namespace pad = grandleon::playstation::pad;
namespace psx = grandleon::playstation;
namespace turn = grandleon::client::turn;
namespace view = grandleon::view;

constexpr int style_count = 7;
constexpr int body_count = 2;
constexpr int archetype_count = 8;
constexpr int terrain_count = 13;
constexpr int max_vertices = 512;
constexpr int max_triangles = sevenfold_high_max_triangles;
constexpr std::uint32_t animated_frame_budget_ticks =
    psx::ticks_per_1000_microseconds * 34U;
constexpr std::uint32_t comparison_frame_budget_ticks =
    psx::ticks_per_1000_microseconds * 40U;

constexpr std::uint8_t terrain_colours[terrain_count][3] = {
    {49, 130, 164}, {168, 135, 83}, {39, 97, 58}, {117, 105, 93},
    {212, 184, 107}, {220, 235, 238}, {101, 115, 59}, {98, 168, 82},
    {148, 135, 121}, {77, 145, 71}, {125, 100, 64}, {131, 194, 104},
    {91, 81, 69},
};
constexpr std::uint8_t sky_colours[terrain_count][3] = {
    {29, 68, 94}, {57, 52, 47}, {24, 54, 38}, {58, 58, 70},
    {96, 73, 46}, {83, 100, 118}, {45, 52, 35}, {52, 79, 55},
    {62, 57, 67}, {37, 72, 51}, {75, 62, 40}, {32, 76, 48},
    {51, 48, 56},
};
constexpr std::uint8_t team_colours[2][3] = {
    {65, 132, 235}, {215, 74, 68},
};
constexpr const char* style_labels[style_count] = {
    "MEDIEVAL", "SCI-FI", "MYTHICAL", "NATURE", "SENGOKU", "UNDEAD",
    "PIRATES",
};
constexpr const char* body_labels[body_count] = {"MALE", "FEMALE"};
constexpr const char* archetype_labels[archetype_count] = {
    "KNIGHT", "ARCHER", "MAGE", "STORM", "HEALER", "COMMAND", "ROGUE",
    "BEAST",
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

ProjectedVertex projected[max_vertices];
OrderedTriangle ordered[max_triangles];
std::uint16_t font_sheet[gpu::font_lines][gpu::font_halfwords_across];

#ifdef GRANDLEON_PSX_SEVENFOLD_3D_CHECK
int checks = 0;
int failures = 0;
#endif

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

#ifdef GRANDLEON_PSX_SEVENFOLD_3D_CHECK
void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) ++failures;
    psx::Line line;
    line.text(condition ? "ok  " : "FAIL ").text(description).flush();
}
#endif

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
    // A fixed 30-degree downward camera. The 218/128 pair is cos/sin in 8.8
    // fixed point, keeping the target free of floating-point support.
    const std::int32_t vertical = y * 218 + z * 128;
    std::int32_t depth = camera.distance * 256 - z * 218 + y * 128;
    if (depth < 64 * 256) depth = 64 * 256;
    const std::int32_t screen_x = 160 + x * camera.focal * 256 / depth;
    const std::int32_t screen_y =
        camera.horizon - vertical * camera.focal / depth;
    return {
        static_cast<std::int16_t>(screen_x),
        static_cast<std::int16_t>(screen_y),
        depth,
    };
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
        // Generated proxy winding is consistent. With screen Y increasing
        // downward, visible faces have a negative signed area; throwing the
        // other half away before sorting saves both CPU work and GPU packets.
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

void keep_clock_extended() noexcept {
#ifdef GRANDLEON_PSX_SEVENFOLD_3D_CHECK
    // Counter 2 wraps every 65,536 ticks (about 15.5 ms). Polling while a
    // large scene is submitted lets psx::clock_ticks() extend it correctly;
    // sampling only at the end could mistake one wrapped frame for a faster
    // one.
    static_cast<void>(psx::clock_ticks());
#endif
}

void draw_model(
    const SevenfoldProxy& proxy, int origin_x, int origin_z, int team,
    int quarter_turn, int lunge, const Camera& camera
) {
    if (proxy.vertices_count > max_vertices ||
        proxy.triangles_count > max_triangles) {
        return;
    }
    for (int index = 0; index < proxy.vertices_count; ++index) {
        const SevenfoldProxyVertex& source = proxy.vertices[index];
        int x = source.x;
        int z = source.z;
        if (quarter_turn < 0) {
            x = source.z;
            z = -source.x;
        } else if (quarter_turn > 0) {
            x = -source.z;
            z = source.x;
        }
        projected[index] = project(
            x + origin_x + lunge, source.y, z + origin_z, camera
        );
    }
    const int ordered_count = order_triangles(proxy);
    for (int order = 0; order < ordered_count; ++order) {
        const SevenfoldProxyTriangle& triangle =
            proxy.triangles[ordered[order].index];
        const ProjectedVertex& a = projected[triangle.a];
        const ProjectedVertex& b = projected[triangle.b];
        const ProjectedVertex& c = projected[triangle.c];
        const std::uint8_t* material = triangle.material == 0
            ? team_colours[team]
            : sevenfold_style_materials[proxy.style][triangle.material];
        const int light = 128 + triangle.shade / 2;
        gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y,
                           colour15(material, light));
        if ((order & 63) == 63) keep_clock_extended();
    }
    keep_clock_extended();
}

void draw_board(int stage, const Camera& camera) {
    constexpr int tile = 46;
    constexpr int half_columns = sevenfold_board_columns / 2;
    constexpr int half_rows = sevenfold_board_rows / 2;
    // Negative Z is farthest from the camera under `project`, so the board is
    // painted back-to-front before figures are placed on it.
    for (int row = 0; row < sevenfold_board_rows; ++row) {
        for (int column = 0; column < sevenfold_board_columns; ++column) {
            const int x0 = (column - half_columns) * tile;
            const int x1 = x0 + tile;
            const int z0 = (row - half_rows) * tile;
            const int z1 = z0 + tile;
            const ProjectedVertex a = project(x0, 0, z0, camera);
            const ProjectedVertex b = project(x1, 0, z0, camera);
            const ProjectedVertex c = project(x1, 0, z1, camera);
            const ProjectedVertex d = project(x0, 0, z1, camera);
            const std::uint8_t terrain = sevenfold_stage_terrain[stage]
                [row * sevenfold_board_columns + column];
            const std::uint16_t colour = colour15(terrain_colours[terrain]);
            gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y, colour);
            gpu::draw_triangle(a.x, a.y, c.x, c.y, d.x, d.y, colour);
        }
        keep_clock_extended();
    }
}

void draw_team_marker(
    int origin_x, int origin_z, int radius, int team, const Camera& camera
) {
    const ProjectedVertex left =
        project(origin_x - radius, 1, origin_z, camera);
    const ProjectedVertex far =
        project(origin_x, 1, origin_z - radius / 2, camera);
    const ProjectedVertex right =
        project(origin_x + radius, 1, origin_z, camera);
    const ProjectedVertex near =
        project(origin_x, 1, origin_z + radius / 2, camera);
    const std::uint16_t colour = colour15(team_colours[team]);
    gpu::draw_triangle(left.x, left.y, far.x, far.y, right.x, right.y, colour);
    gpu::draw_triangle(left.x, left.y, right.x, right.y, near.x, near.y, colour);
}

void begin_scene(int terrain) {
    gpu::begin(colour15(sky_colours[terrain]));
    build_font();
}

void draw_strategic(int style, int body, bool high_detail, int role_page) {
    const Camera camera{570, 350, 185};
    begin_scene(9);
    draw_board(style, camera);
    constexpr int columns[2] = {-72, 72};
    constexpr int rows[2] = {-45, 62};
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const int within_page = row * 2 + column;
            const int archetype = role_page * 4 + within_page;
            const int team = archetype & 1;
            const SevenfoldProxy& proxy = high_detail
                ? sevenfold_proxies_high[style][body][archetype]
                : sevenfold_proxies_low[style][body][archetype];
            draw_team_marker(columns[column], rows[row], 28, team, camera);
            draw_model(proxy, columns[column], rows[row], team, 0, 0, camera);
        }
    }
    gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 5, "SEVENFOLD 3D");
    text_at(120, 5, style_labels[style]);
    text_at(208, 5, body_labels[body]);
    text_at(280, 5, role_page == 0 ? "1-4" : "5-8");
    gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 227, "D-PAD STYLE/BODY X PAGE TRI LOD START");
    gpu::show();
}

void draw_action(int style, int archetype, int terrain, int lunge) {
    const Camera camera{405, 400, 211};
    begin_scene(terrain);
    // Terrain bands make the close view belong to the tile it came from while
    // staying cheap enough that the figures receive the triangle budget.
    const std::uint16_t ground = colour15(terrain_colours[terrain]);
    gpu::fill(0, 173, gpu::screen_width, 67, ground);
    for (int x = 0; x < gpu::screen_width; x += 40) {
        const int height = 18 + ((x / 40 + terrain) % 4) * 8;
        gpu::fill(x, 173 - height, 30, height,
                  colour15(terrain_colours[(terrain + 2) % terrain_count], 180));
    }
    draw_team_marker(-70 + lunge, 0, 32, 0, camera);
    draw_team_marker(70 - lunge, 0, 32, 1, camera);
    draw_model(sevenfold_proxies_high[style][0][archetype], -70, 0, 0, 1,
               lunge, camera);
    draw_model(sevenfold_proxies_high[style][1][archetype], 70, 0, 1, -1,
               -lunge, camera);
    gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 5, style_labels[style]);
    text_at(112, 5, archetype_labels[archetype]);
    text_at(224, 5, "ATTACK VIEW");
    gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 227, "D-PAD STYLE/TILE X ROLE O HIT START");
    gpu::show();
}

#ifdef GRANDLEON_PSX_SEVENFOLD_3D_CHECK
[[nodiscard]] std::uint32_t finish_timed_frame() {
    // A readback command is ordered behind every primitive, so when its value
    // arrives the GPU has consumed the whole scene rather than merely accepted
    // it into the FIFO.
    static_cast<void>(gpu::read_back(0, 0));
    return psx::clock_ticks();
}

int run_check() {
    psx::start_clock();
    expect(max_vertices >= 429, "projection storage covers the largest model");
    expect(max_triangles >= 750, "triangle ordering covers the high LOD");

    std::uint32_t slowest_board = 0;
    std::uint32_t slowest_high_board = 0;
    std::uint32_t slowest_close = 0;
    for (int style = 0; style < style_count; ++style) {
        for (int body = 0; body < body_count; ++body) {
            for (int role_page = 0; role_page < 2; ++role_page) {
                psx::start_clock();
                draw_strategic(style, body, false, role_page);
                const std::uint32_t board_ticks = finish_timed_frame();
                if (board_ticks > slowest_board) slowest_board = board_ticks;
                expect(board_ticks > 0 &&
                           board_ticks <= animated_frame_budget_ticks,
                       "four-role low-LOD page stays at 30 fps");

                psx::start_clock();
                draw_strategic(style, body, true, role_page);
                const std::uint32_t high_board_ticks = finish_timed_frame();
                if (high_board_ticks > slowest_high_board) {
                    slowest_high_board = high_board_ticks;
                }
                expect(high_board_ticks > 0 &&
                           high_board_ticks <= comparison_frame_budget_ticks,
                       "four-role high-LOD comparison stays above 25 fps");
            }

            psx::start_clock();
            draw_action(style, (style + body) % archetype_count,
                        style % terrain_count, 10);
            const std::uint32_t close_ticks = finish_timed_frame();
            if (close_ticks > slowest_close) slowest_close = close_ticks;
            expect(close_ticks > 0 &&
                       close_ticks <= animated_frame_budget_ticks,
                   "high-LOD attack pair stays at 30 fps");
        }
    }
    for (int style = 0; style < style_count; ++style) {
        for (int body = 0; body < body_count; ++body) {
            for (int archetype = 0; archetype < archetype_count; ++archetype) {
                const SevenfoldProxy& low =
                    sevenfold_proxies_low[style][body][archetype];
                const SevenfoldProxy& high =
                    sevenfold_proxies_high[style][body][archetype];
                expect(low.style == style && low.figure == body &&
                           low.archetype == archetype &&
                           low.vertices_count <= max_vertices &&
                           low.triangles_count <= max_triangles,
                       "low-LOD roster entry is complete");
                expect(high.style == style && high.figure == body &&
                           high.archetype == archetype &&
                           high.vertices_count <= max_vertices &&
                           high.triangles_count <= max_triangles,
                       "high-LOD roster entry is complete");
            }
        }
    }

    psx::Line board;
    board.text("BENCH strategic slowest ticks=").decimal(slowest_board).flush();
    psx::Line high_board;
    high_board.text("BENCH strategic-high slowest ticks=")
        .decimal(slowest_high_board).flush();
    psx::Line close;
    close.text("BENCH attack slowest ticks=").decimal(slowest_close).flush();

    // Leave a representative action frame on screen for the host capture.
    draw_action(2, 2, 3, 12);
    static_cast<void>(gpu::read_back(0, 0));
    psx::Line result;
    result.text(failures == 0 ? "RESULT PASS " : "RESULT FAIL ")
        .decimal(static_cast<std::uint32_t>(checks - failures))
        .text("/").decimal(static_cast<std::uint32_t>(checks)).flush();
    psx::signal_host(13);
    return failures == 0 ? 0 : 1;
}
#endif

#ifndef GRANDLEON_PSX_SEVENFOLD_3D_CHECK
int run_interactive() {
    pad::begin();
    int style = 0;
    int body = 0;
    int archetype = 0;
    int terrain = 2;
    bool high_detail = false;
    bool action = false;
    int role_page = 0;
    int strike_frame = 0;
    while (true) {
        int lunge = 0;
        if (strike_frame > 0) {
            const int phase = strike_frame < 10 ? strike_frame : 20 - strike_frame;
            lunge = phase * 2;
            ++strike_frame;
            if (strike_frame >= 20) strike_frame = 0;
        }
        if (action) {
            draw_action(style, archetype, terrain, lunge);
        } else {
            draw_strategic(style, body, high_detail, role_page);
        }
        psx::wait_vblank();
        const std::uint16_t pressed = pad::pressed();
        if ((pressed & turn::pad_left) != 0) {
            style = (style + style_count - 1) % style_count;
        }
        if ((pressed & turn::pad_right) != 0) {
            style = (style + 1) % style_count;
        }
        if ((pressed & turn::pad_up) != 0) {
            if (action) terrain = (terrain + 1) % terrain_count;
            else body = (body + 1) % body_count;
        }
        if ((pressed & turn::pad_down) != 0) {
            if (action) terrain = (terrain + terrain_count - 1) % terrain_count;
            else body = (body + 1) % body_count;
        }
        if ((pressed & turn::pad_a) != 0) {
            if (action) archetype = (archetype + 1) % archetype_count;
            else role_page = 1 - role_page;
        }
        if ((pressed & turn::pad_c) != 0 && !action) {
            high_detail = !high_detail;
        }
        if ((pressed & turn::pad_b) != 0 && action) strike_frame = 1;
        if ((pressed & turn::pad_start) != 0) action = !action;
    }
}
#endif

}  // namespace

int main() {
#ifdef GRANDLEON_PSX_SEVENFOLD_3D_CHECK
    return run_check();
#else
    return run_interactive();
#endif
}
