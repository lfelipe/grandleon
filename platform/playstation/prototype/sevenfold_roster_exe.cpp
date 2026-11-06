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
// NTSC presentation is locked to every second retrace: approximately 30 fps.
// Thirty-three milliseconds is a useful emulator regression ceiling and is a
// real cadence target; an unsynchronised "32 fps" is not.
constexpr std::uint32_t animated_frame_budget_ticks =
    psx::ticks_per_1000_microseconds * 33U;
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

struct ScreenBounds final {
    int left = gpu::screen_width;
    int top = gpu::screen_height;
    int right = 0;
    int bottom = 0;
    bool any = false;
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

void include_model_bounds(
    ScreenBounds& bounds, const SevenfoldProxy& proxy,
    int origin_x, int origin_y, int origin_z, int quarter_turn, int lean,
    const Camera& camera
) {
    int source_left = proxy.vertices[0].x;
    int source_bottom = proxy.vertices[0].y;
    int source_back = proxy.vertices[0].z;
    int source_right = source_left;
    int source_top = source_bottom;
    int source_front = source_back;
    for (int index = 0; index < proxy.vertices_count; ++index) {
        const SevenfoldProxyVertex& source = proxy.vertices[index];
        if (source.x < source_left) source_left = source.x;
        if (source.x > source_right) source_right = source.x;
        if (source.y < source_bottom) source_bottom = source.y;
        if (source.y > source_top) source_top = source.y;
        if (source.z < source_back) source_back = source.z;
        if (source.z > source_front) source_front = source.z;
    }
    for (int xi = 0; xi < 2; ++xi) {
        for (int yi = 0; yi < 2; ++yi) {
            for (int zi = 0; zi < 2; ++zi) {
                int x = xi == 0 ? source_left : source_right;
                const int y = yi == 0 ? source_bottom : source_top;
                int z = zi == 0 ? source_back : source_front;
                if (quarter_turn < 0) {
                    const int turned_x = z;
                    z = -x;
                    x = turned_x;
                } else if (quarter_turn > 0) {
                    const int turned_x = -z;
                    z = x;
                    x = turned_x;
                }
                x += y * lean / 256;
                const ProjectedVertex point = project(
                    x + origin_x, y + origin_y, z + origin_z, camera
                );
                if (!bounds.any) {
                    bounds.left = point.x;
                    bounds.right = point.x;
                    bounds.top = point.y;
                    bounds.bottom = point.y;
                    bounds.any = true;
                } else {
                    if (point.x < bounds.left) bounds.left = point.x;
                    if (point.x > bounds.right) bounds.right = point.x;
                    if (point.y < bounds.top) bounds.top = point.y;
                    if (point.y > bounds.bottom) bounds.bottom = point.y;
                }
            }
        }
    }
}

[[nodiscard]] ScreenBounds action_pair_bounds(
    const SevenfoldProxy& left, const SevenfoldProxy& right,
    const Camera& camera
) {
    ScreenBounds bounds;
    include_model_bounds(bounds, left, -70, 0, 0, 1, 0, camera);
    include_model_bounds(bounds, left, -50, 0, 0, 1, 0, camera);
    include_model_bounds(bounds, right, 70, 0, 0, -1, 0, camera);
    include_model_bounds(bounds, right, 50, 0, 0, -1, 0, camera);
    return bounds;
}

[[nodiscard]] Camera action_camera(
    const SevenfoldProxy& left, const SevenfoldProxy& right
) {
    constexpr int safe_left = 10;
    constexpr int safe_top = 24;
    constexpr int safe_right = gpu::screen_width - 10;
    constexpr int safe_bottom = 220;
    constexpr int distance = 360;
    constexpr int minimum_focal = 300;
    constexpr int maximum_focal = 560;
    for (int focal = maximum_focal; focal >= minimum_focal; focal -= 10) {
        const Camera probe{distance, focal, 0};
        const ScreenBounds bounds = action_pair_bounds(left, right, probe);
        if (!bounds.any || bounds.left < safe_left ||
            bounds.right > safe_right ||
            bounds.bottom - bounds.top > safe_bottom - safe_top) {
            continue;
        }
        const int horizon = safe_bottom - bounds.bottom;
        if (bounds.top + horizon >= safe_top) {
            return {distance, focal, horizon};
        }
    }
    return {distance, minimum_focal, 211};
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
    static bool started = false;
    if (!started) {
        gpu::begin(colour15(sky_colours[terrain]));
        build_font();
        started = true;
    } else {
        gpu::clear(colour15(sky_colours[terrain]));
    }
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
    const SevenfoldProxy& left = sevenfold_proxies_high[style][0][archetype];
    const SevenfoldProxy& right = sevenfold_proxies_high[style][1][archetype];
    const Camera camera = action_camera(left, right);
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
    draw_model(left, -70, 0, 0, 1, lunge, camera);
    draw_model(right, 70, 0, 1, -1, -lunge, camera);
    if (lunge >= 14) {
        const ProjectedVertex contact = project(0, 63, -12, camera);
        const int radius = 7 + (lunge - 14) / 2;
        const std::uint16_t flash = gpu::rgb15(31, 29, 18);
        gpu::draw_triangle(
            contact.x - radius, contact.y,
            contact.x, contact.y - radius,
            contact.x + radius, contact.y, flash
        );
        gpu::draw_triangle(
            contact.x - radius, contact.y,
            contact.x + radius, contact.y,
            contact.x, contact.y + radius, flash
        );
    }
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
    int narrowest_action_focal = 10000;
    int widest_action_focal = 0;
    for (int style = 0; style < style_count; ++style) {
        for (int archetype = 0; archetype < archetype_count; ++archetype) {
            const SevenfoldProxy& left =
                sevenfold_proxies_high[style][0][archetype];
            const SevenfoldProxy& right =
                sevenfold_proxies_high[style][1][archetype];
            const Camera camera = action_camera(left, right);
            const ScreenBounds bounds = action_pair_bounds(left, right, camera);
            if (camera.focal < narrowest_action_focal) {
                narrowest_action_focal = camera.focal;
            }
            if (camera.focal > widest_action_focal) {
                widest_action_focal = camera.focal;
            }
            expect(
                bounds.any && bounds.left >= 10 && bounds.right <= 310 &&
                    bounds.top >= 24 && bounds.bottom <= 220,
                "auto-framed attack pair stays inside the safe picture"
            );
        }
    }

    psx::Line board;
    board.text("BENCH strategic slowest ticks=").decimal(slowest_board).flush();
    psx::Line high_board;
    high_board.text("BENCH strategic-high slowest ticks=")
        .decimal(slowest_high_board).flush();
    psx::Line close;
    close.text("BENCH attack slowest ticks=").decimal(slowest_close).flush();
    psx::Line framing;
    framing.text("BENCH attack fitted focal=")
        .decimal(static_cast<std::uint32_t>(narrowest_action_focal))
        .text("..")
        .decimal(static_cast<std::uint32_t>(widest_action_focal))
        .text(" safe=10,24..310,220")
        .flush();

    // Leave a representative action frame on screen for the host capture.
    draw_action(2, 2, 3, 18);
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
    draw_strategic(style, body, high_detail, role_page);
    while (true) {
        psx::wait_vblank();
        const std::uint16_t pressed = pad::pressed();
        bool changed = false;
        if ((pressed & turn::pad_left) != 0) {
            style = (style + style_count - 1) % style_count;
            changed = true;
        }
        if ((pressed & turn::pad_right) != 0) {
            style = (style + 1) % style_count;
            changed = true;
        }
        if ((pressed & turn::pad_up) != 0) {
            if (action) terrain = (terrain + 1) % terrain_count;
            else body = (body + 1) % body_count;
            changed = true;
        }
        if ((pressed & turn::pad_down) != 0) {
            if (action) terrain = (terrain + terrain_count - 1) % terrain_count;
            else body = (body + 1) % body_count;
            changed = true;
        }
        if ((pressed & turn::pad_a) != 0) {
            if (action) archetype = (archetype + 1) % archetype_count;
            else role_page = 1 - role_page;
            changed = true;
        }
        if ((pressed & turn::pad_c) != 0 && !action) {
            high_detail = !high_detail;
            changed = true;
        }
        if ((pressed & turn::pad_b) != 0 && action) {
            strike_frame = 1;
        }
        if ((pressed & turn::pad_start) != 0) {
            action = !action;
            changed = true;
        }
        if (strike_frame > 0) {
            const int phase =
                strike_frame < 10 ? strike_frame : 20 - strike_frame;
            draw_action(style, archetype, terrain, phase * 2);
            ++strike_frame;
            if (strike_frame >= 20) strike_frame = 0;
        } else if (changed) {
            if (action) draw_action(style, archetype, terrain, 0);
            else draw_strategic(style, body, high_detail, role_page);
        }
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
