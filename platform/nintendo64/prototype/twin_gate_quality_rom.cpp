// SPDX-License-Identifier: MIT
// Hardware proof for a richer fixed-camera Nintendo 64 campaign view.

#include <libdragon.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "console_character_assets.h"
#include "console_terrain_assets.h"
#include "console_twin_gate_quality_assets.h"
#include "twin_gate_scene.h"

namespace {

namespace scene = grandleon::console_quality::twin_gate;

constexpr int screen_width = 320;
constexpr int screen_height = 240;
constexpr int half_width = 14;
constexpr int half_height = 7;
constexpr int camera_x = 65;
constexpr int camera_y = 20;
constexpr std::uint64_t frame_budget_us = 33'333;
constexpr int terrain_material[13] = {
    3, 2, 0, 1, 2, 3, 3, 0, 1, 0, 2, 0, 1,
};

int checks = 0;
int failures = 0;

sprite_t* terrain = nullptr;
sprite_t* keep[console_twin_gate_quality_state_count]
              [console_twin_gate_quality_tile_count]{};

struct Character final {
    sprite_t* idle{};
    sprite_t* move{};
};

Character characters[7]{};

void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) ++failures;
    std::fprintf(stderr, "%s %s\n", condition ? "ok  " : "FAIL", message);
    std::fflush(stderr);
}

[[nodiscard]] sprite_t* load_sprite(
    const unsigned char* source, std::size_t size
) {
    if (source == nullptr || size < sizeof(sprite_t)) return nullptr;
    void* copy = std::malloc(size);
    if (copy == nullptr) return nullptr;
    std::memcpy(copy, source, size);
    sprite_t* result = sprite_load_buf(copy, static_cast<int>(size));
    if (result == nullptr) std::free(copy);
    return result;
}

[[nodiscard]] int drawing_index(
    int style, int figure, int archetype, int colour
) {
    for (std::size_t index = 0; index < grandleon_console_drawing_count; ++index) {
        const auto& drawing = grandleon_console_drawings[index];
        if (drawing.style == style && drawing.figure == figure &&
            drawing.archetype == archetype && drawing.colour == colour) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

[[nodiscard]] bool load_character(int slot, const scene::Unit& unit) {
    const int drawing = drawing_index(
        unit.style, unit.figure, unit.archetype, unit.colour
    );
    if (drawing < 0) return false;
    characters[slot].idle = load_sprite(
        grandleon_console_drawings[drawing].board[0],
        grandleon_console_drawings[drawing].board_size[0]
    );
    characters[slot].move = load_sprite(
        grandleon_console_drawings[drawing].board[1],
        grandleon_console_drawings[drawing].board_size[1]
    );
    return characters[slot].idle != nullptr && characters[slot].move != nullptr;
}

[[nodiscard]] bool initialize_assets() {
    terrain = load_sprite(
        console_terrain_material_atlas, console_terrain_material_atlas_size
    );
    bool complete = terrain != nullptr;
    for (int state = 0; state < console_twin_gate_quality_state_count; ++state) {
        for (int tile = 0; tile < console_twin_gate_quality_tile_count; ++tile) {
            const int index = state * console_twin_gate_quality_tile_count + tile;
            keep[state][tile] = load_sprite(
                console_twin_gate_quality_data[index],
                console_twin_gate_quality_sizes[index]
            );
            complete = complete && keep[state][tile] != nullptr;
        }
    }
    int slot = 0;
    for (const scene::Unit& unit : scene::approach_units) {
        complete = load_character(slot++, unit) && complete;
    }
    // The revealed list repeats Rowan and Tomas, so load only the two new
    // opponents after the three approach identities.
    complete = load_character(slot++, scene::revealed_units[2]) && complete;
    complete = load_character(slot++, scene::revealed_units[3]) && complete;
    return complete;
}

[[nodiscard]] constexpr int elevation(std::uint8_t terrain_kind) noexcept {
    return terrain_kind == 3U ? 6 : (terrain_kind == 7U ? 3 : 0);
}

struct Point final { int x; int y; };

[[nodiscard]] Point projected(int x256, int y256) noexcept {
    const int difference = x256 - y256;
    const int sum = x256 + y256;
    return {
        160 + difference * half_width / 256 - camera_x,
        40 + sum * half_height / 256 - camera_y,
    };
}

[[nodiscard]] constexpr std::uint8_t material_of(std::uint8_t kind) noexcept {
    return static_cast<std::uint8_t>(
        kind < 13U ? terrain_material[kind] : terrain_material[9]
    );
}

void flat_triangle(Point a, Point b, Point c, color_t colour) {
    rdpq_set_prim_color(colour);
    const float first[] = {static_cast<float>(a.x), static_cast<float>(a.y)};
    const float second[] = {static_cast<float>(b.x), static_cast<float>(b.y)};
    const float third[] = {static_cast<float>(c.x), static_cast<float>(c.y)};
    rdpq_triangle(&TRIFMT_FILL, first, second, third);
}

void textured_triangle(
    Point a, float as, float at, float ar, float ag, float ab,
    Point b, float bs, float bt, float br, float bg, float bb,
    Point c, float cs, float ct, float cr, float cg, float cb
) {
    const float first[] = {
        static_cast<float>(a.x), static_cast<float>(a.y),
        ar, ag, ab, 1.0F, as, at, 1.0F,
    };
    const float second[] = {
        static_cast<float>(b.x), static_cast<float>(b.y),
        br, bg, bb, 1.0F, bs, bt, 1.0F,
    };
    const float third[] = {
        static_cast<float>(c.x), static_cast<float>(c.y),
        cr, cg, cb, 1.0F, cs, ct, 1.0F,
    };
    rdpq_triangle(&TRIFMT_SHADE_TEX, first, second, third);
}

struct TerrainColour final { float red; float green; float blue; };

[[nodiscard]] TerrainColour terrain_colour(std::uint8_t kind) noexcept {
    constexpr std::uint8_t colours[13][3] = {
        {49,130,164}, {168,135,83}, {39,97,58}, {117,105,93},
        {212,184,107}, {220,235,238}, {101,115,59}, {98,168,82},
        {148,135,121}, {77,145,71}, {125,100,64}, {131,194,104},
        {132,126,116},
    };
    kind = kind < 13U ? kind : 9U;
    return {
        colours[kind][0] / 255.0F,
        colours[kind][1] / 255.0F,
        colours[kind][2] / 255.0F,
    };
}

void set_terrain_texture_mode() {
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
}

void draw_terrain_cell(int x, int y, std::uint8_t kind) {
    Point centre = projected(x * 256, y * 256);
    const int lift = elevation(kind);
    centre.y -= lift;
    const Point top{centre.x, centre.y - half_height};
    const Point right{centre.x + half_width, centre.y};
    const Point bottom{centre.x, centre.y + half_height};
    const Point left{centre.x - half_width, centre.y};
    if (bottom.x < -half_width || left.x >= screen_width ||
        bottom.y < 24 || top.y >= screen_height) return;

    if (lift > 0) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_dithering(DITHER_BAYER_NONE);
        const TerrainColour colour = terrain_colour(kind);
        const color_t side = RGBA32(
            static_cast<std::uint8_t>(colour.red * 135.0F),
            static_cast<std::uint8_t>(colour.green * 135.0F),
            static_cast<std::uint8_t>(colour.blue * 135.0F), 255
        );
        flat_triangle(left, bottom, {bottom.x, bottom.y + lift}, side);
        flat_triangle(left, {bottom.x, bottom.y + lift},
                      {left.x, left.y + lift}, side);
        const color_t other = RGBA32(
            static_cast<std::uint8_t>(colour.red * 105.0F),
            static_cast<std::uint8_t>(colour.green * 105.0F),
            static_cast<std::uint8_t>(colour.blue * 105.0F), 255
        );
        flat_triangle(bottom, right, {right.x, right.y + lift}, other);
        flat_triangle(bottom, {right.x, right.y + lift},
                      {bottom.x, bottom.y + lift}, other);
        set_terrain_texture_mode();
    }

    const std::uint8_t material = material_of(kind);
    const float source_x = static_cast<float>((material & 1U) * 32U);
    const float source_y = static_cast<float>(((material >> 1U) & 1U) * 32U);
    const TerrainColour colour = terrain_colour(kind);
    const float bright =
        static_cast<float>((x * 5 + y * 7) & 3) * 0.025F + 0.90F;
    const float red = std::min(1.0F, colour.red * bright);
    const float green = std::min(1.0F, colour.green * bright);
    const float blue = std::min(1.0F, colour.blue * bright);
    textured_triangle(
        top, source_x + 16.0F, source_y, red, green, blue,
        right, source_x + 31.0F, source_y + 16.0F, red, green, blue,
        bottom, source_x + 16.0F, source_y + 31.0F,
        red * 0.82F, green * 0.82F, blue * 0.82F
    );
    textured_triangle(
        top, source_x + 16.0F, source_y, red, green, blue,
        bottom, source_x + 16.0F, source_y + 31.0F,
        red * 0.82F, green * 0.82F, blue * 0.82F,
        left, source_x, source_y + 16.0F,
        red * 0.92F, green * 0.92F, blue * 0.92F
    );
}

void draw_board() {
    // The atlas stays resident until the landmark pass. Uploading it once per
    // frame rather than once per cell is the difference between a console
    // renderer and a software-style immediate mock-up.
    set_terrain_texture_mode();
    rdpq_sprite_upload(TILE0, terrain, nullptr);
    for (int diagonal = 0; diagonal < scene::map_width + scene::map_height - 1;
         ++diagonal) {
        for (int y = 0; y < scene::map_height; ++y) {
            const int x = diagonal - y;
            if (x < 0 || x >= scene::map_width) continue;
            const std::size_t index = static_cast<std::size_t>(
                y * scene::map_width + x
            );
            draw_terrain_cell(x, y, scene::terrain[index]);
        }
    }
}

void draw_keep(int state) {
    state = std::clamp(state, 0, console_twin_gate_quality_state_count - 1);
    constexpr int left = 124;
    constexpr int top = 32;
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    for (int tile_y = 0; tile_y < console_twin_gate_quality_tile_rows; ++tile_y) {
        for (int tile_x = 0; tile_x < console_twin_gate_quality_tile_columns;
             ++tile_x) {
            const int tile = tile_y * console_twin_gate_quality_tile_columns + tile_x;
            sprite_t* sprite = keep[state][tile];
            rdpq_sprite_upload(TILE0, sprite, nullptr);
            rdpq_sprite_blit(
                sprite,
                static_cast<float>(left + tile_x * console_twin_gate_quality_tile_width),
                static_cast<float>(top + tile_y * console_twin_gate_quality_tile_height),
                nullptr
            );
        }
    }
}

void draw_shadow(Point at) {
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    flat_triangle({at.x - 15, at.y}, {at.x, at.y - 4}, {at.x + 15, at.y},
                  RGBA32(20, 20, 22, 255));
    flat_triangle({at.x - 15, at.y}, {at.x + 15, at.y}, {at.x, at.y + 4},
                  RGBA32(20, 20, 22, 255));
}

void draw_character_at(int slot, Point at, int size, bool moving) {
    draw_shadow(at);
    sprite_t* sprite = moving ? characters[slot].move : characters[slot].idle;
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_blitparms_t parameters{};
    parameters.scale_x = static_cast<float>(size) /
        static_cast<float>(sprite->width);
    parameters.scale_y = static_cast<float>(size) /
        static_cast<float>(sprite->height);
    rdpq_sprite_blit(
        sprite, static_cast<float>(at.x - size / 2),
        static_cast<float>(at.y - size),
        &parameters
    );
}

void draw_character(int slot, int x256, int y256, bool moving) {
    Point at = projected(x256, y256);
    const int x = x256 / 256;
    const int y = y256 / 256;
    if (x >= 0 && x < scene::map_width && y >= 0 && y < scene::map_height) {
        at.y -= elevation(scene::terrain[y * scene::map_width + x]);
    }
    at.y += 5;
    draw_character_at(slot, at, 48, moving);
}

void draw_hud(surface_t* display, int phase) {
    const char* heading = phase == 0 ? "TWIN GATE KEEP  /  APPROACH"
        : (phase == 4 ? "TWIN GATE KEEP  /  ROOM REVEALED"
                      : "TWIN GATE KEEP  /  THRESHOLD");
    graphics_draw_box(
        display, 8, 7, 250, 19, graphics_make_color(13, 22, 28, 235)
    );
    graphics_draw_box(
        display, 8, 7, 4, 19, graphics_make_color(211, 166, 74, 255)
    );
    graphics_set_color(graphics_make_color(241, 231, 205, 255), 0);
    graphics_draw_text(display, 18, 12, heading);
    graphics_draw_box(
        display, 8, 215, 115, 18, graphics_make_color(13, 22, 28, 235)
    );
    graphics_draw_box(
        display, 197, 215, 115, 18, graphics_make_color(13, 22, 28, 235)
    );
    graphics_set_color(graphics_make_color(85, 166, 224, 255), 0);
    graphics_draw_text(display, 16, 220, "ROWAN  HP 24");
    graphics_set_color(graphics_make_color(225, 91, 70, 255), 0);
    graphics_draw_text(display, 205, 220, "MASATORA 27");
}

void draw_baseline_hud(surface_t* display) {
    draw_hud(display, 0);
    graphics_draw_box(
        display, 8, 7, 264, 19, graphics_make_color(13, 22, 28, 235)
    );
    graphics_draw_box(
        display, 8, 7, 4, 19, graphics_make_color(211, 166, 74, 255)
    );
    graphics_set_color(graphics_make_color(241, 231, 205, 255), 0);
    graphics_draw_text(display, 18, 12, "MATCHED FLAT BOARD  /  APPROACH");
}

void draw_baseline_frame() {
    constexpr int origin_x = 40;
    constexpr int origin_y = 32;
    constexpr int side = 12;
    surface_t* display = display_get();
    rdpq_attach(display, nullptr);
    rdpq_clear(RGBA32(13, 28, 39, 255));
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_sprite_upload(TILE0, terrain, nullptr);
    for (int y = 0; y < scene::map_height; ++y) {
        for (int x = 0; x < scene::map_width; ++x) {
            const std::size_t index = static_cast<std::size_t>(
                y * scene::map_width + x
            );
            const std::uint8_t kind = scene::terrain[index];
            const std::uint8_t material = material_of(kind);
            const int source_x = (material & 1U) * 32;
            const int source_y = ((material >> 1U) & 1U) * 32;
            const TerrainColour colour = terrain_colour(kind);
            rdpq_set_prim_color(RGBA32(
                static_cast<std::uint8_t>(colour.red * 255.0F),
                static_cast<std::uint8_t>(colour.green * 255.0F),
                static_cast<std::uint8_t>(colour.blue * 255.0F), 255
            ));
            const int left = origin_x + x * side;
            const int top = origin_y + y * side;
            rdpq_texture_rectangle_scaled(
                TILE0, left, top, left + side, top + side,
                source_x, source_y, source_x + 32, source_y + 32
            );
        }
    }

    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    for (int y = 0; y < scene::map_height; ++y) {
        for (int x = 0; x < scene::map_width; ++x) {
            const std::uint8_t feature =
                scene::feature[y * scene::map_width + x];
            if (feature == 0U || feature == 3U) continue;
            const int left = origin_x + x * side + 2;
            const int top = origin_y + y * side + 2;
            const color_t colour = feature == 1U ? RGBA32(55, 61, 64, 255)
                : (feature == 2U ? RGBA32(111, 70, 41, 255)
                                 : RGBA32(190, 150, 63, 255));
            rdpq_set_prim_color(colour);
            rdpq_fill_rectangle(left, top, left + side - 4, top + side - 4);
        }
    }
    draw_character_at(
        0, {origin_x + 6 * side + side / 2,
            origin_y + 6 * side + side}, 32, false
    );
    draw_character_at(
        1, {origin_x + 5 * side + side / 2,
            origin_y + 6 * side + side}, 32, false
    );
    draw_character_at(
        2, {origin_x + 12 * side + side / 2,
            origin_y + 3 * side + side}, 32, false
    );
    rdpq_detach_wait();
    draw_baseline_hud(display);
    display_show(display);
}

std::uint64_t draw_frame(int keep_state, int phase, int progress) {
    const std::uint64_t started = get_ticks_us();
    surface_t* display = display_get();
    rdpq_attach(display, nullptr);
    rdpq_clear(RGBA32(13, 28, 39, 255));
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    for (int band = 0; band < 6; ++band) {
        rdpq_set_prim_color(RGBA32(
            static_cast<std::uint8_t>(18 + band * 3),
            static_cast<std::uint8_t>(38 + band * 4),
            static_cast<std::uint8_t>(49 + band * 4), 255
        ));
        rdpq_fill_rectangle(0, band * 40, screen_width, (band + 1) * 40);
    }
    draw_board();
    draw_keep(keep_state);
    if (phase == 0) {
        draw_character(0, 6 * 256, 6 * 256, false);
        draw_character(1, 5 * 256, 6 * 256, false);
        draw_character(2, 12 * 256, 3 * 256, false);
    } else if (phase < 4) {
        const int rowan_x = 6 * 256 + (8 * 256 * progress) / 256;
        const int tomas_x = 5 * 256 + (9 * 256 * progress) / 256;
        draw_character(0, rowan_x, 6 * 256, true);
        draw_character(1, tomas_x, 7 * 256, true);
    } else {
        draw_character(0, 14 * 256, 6 * 256, false);
        draw_character(1, 14 * 256, 7 * 256, false);
        draw_character(3, 16 * 256, 6 * 256, false);
        draw_character(4, 13 * 256, 7 * 256, false);
    }
    rdpq_detach_wait();
    draw_hud(display, phase);
    display_show(display);
    return get_ticks_us() - started;
}

std::uint64_t run_sequence(bool report) {
    std::uint64_t slowest = 0;
    for (int frame = 0; frame < 210; ++frame) {
        int state = 0;
        int phase = 0;
        int progress = 0;
        if (frame >= 60 && frame < 120) {
            const int local = frame - 60;
            state = 1 + local / 20;
            phase = state;
            progress = local * 256 / 59;
        } else if (frame >= 120) {
            state = 4;
            phase = 4;
            progress = 256;
        }
        slowest = std::max(slowest, draw_frame(state, phase, progress));
        if (report) {
            const char* checkpoint = nullptr;
            if (frame == 15) checkpoint = "twin-gate-exterior";
            else if (frame == 90) checkpoint = "twin-gate-threshold";
            else if (frame == 165) checkpoint = "twin-gate-revealed";
            if (checkpoint != nullptr) {
                // Populate every display buffer with the named state before
                // the external capture observes it. This avoids photographing
                // a buffer that still carries part of the previous phase.
                for (int settle = 0; settle < 3; ++settle) {
                    draw_frame(state, phase, progress);
                }
                std::fprintf(stderr, "CHECKPOINT %s\n", checkpoint);
                std::fflush(stderr);
                wait_ms(500);
            }
        }
        wait_ms(16);
    }
    return slowest;
}

}  // namespace

extern "C" int main() {
    debug_init_isviewer();
    std::fprintf(stderr, "grandleon n64 twin gate quality prototype\n");
    display_init(
        RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE
    );
    rdpq_init();
    joypad_init();

    const bool assets = initialize_assets();
    expect(assets, "filtered terrain, keep and curated characters allocate");
    expect(scene::map_width == 20 && scene::map_height == 14,
           "prototype consumes the authored twenty-by-fourteen map");
    expect(scene::keep_x == 13 && scene::keep_y == 4 && scene::keep_size == 6,
           "prototype preserves the exact connected six-by-six keep");
    expect(console_twin_gate_quality_state_count == 5,
           "closed, three threshold and revealed keep states are packaged");
    expect(console_twin_gate_quality_tile_texels <= 2048U,
           "every streamed CI8 keep tile fits the TLUT-constrained 2 KiB bank");
    if (!assets) {
        std::fprintf(stderr, "RESULT FAIL %d/%d\n", checks - failures, checks);
        while (true) wait_ms(1000);
    }

    for (int settle = 0; settle < 3; ++settle) draw_baseline_frame();
    std::fprintf(stderr, "CHECKPOINT twin-gate-flat-baseline\n");
    std::fflush(stderr);
    wait_ms(500);
    const std::uint64_t slowest = run_sequence(true);
    expect(slowest <= frame_budget_us, "matched quality sequence sustains 30 fps");
    std::fprintf(
        stderr,
        "BENCH n64 twin-gate map=20x14 keep=6x6 states=5 terrain=ci4-bilinear-shaded-triangles keep=ci8-bilinear-tiled texels=%lu palette=%lu peak_tmem=%lu embedded_keep=%lu max_us=%llu fps=%llu\n",
        static_cast<unsigned long>(console_twin_gate_quality_tile_texels),
        static_cast<unsigned long>(console_twin_gate_quality_palette_bytes),
        static_cast<unsigned long>(console_twin_gate_quality_peak_tmem),
        static_cast<unsigned long>(console_twin_gate_quality_embedded_bytes),
        static_cast<unsigned long long>(slowest),
        static_cast<unsigned long long>(slowest > 0 ? 1'000'000 / slowest : 0)
    );
    std::fprintf(
        stderr, "RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
        checks - failures, checks
    );
    std::fflush(stderr);

    draw_frame(4, 4, 256);
    while (true) {
        joypad_poll();
        const joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (pressed.a || pressed.start) run_sequence(false);
        wait_ms(16);
    }
}
