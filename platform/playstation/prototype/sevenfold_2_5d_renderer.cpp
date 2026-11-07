// SPDX-License-Identifier: MIT
// PlayStation fixed-camera 2.5D renderer gate. It exercises the curated
// Godot-derived cutouts, textured tactical terrain and full-frame combat
// backdrop that replace the retired polygon-proxy renderer.

#include "psx_gpu.h"
#include "psx_runtime.h"

#include <grandleon/view/glyphs.hpp>

#include <algorithm>
#include <cstdint>

#include "generated/godot_closeup_ci4.h"
#include "generated/combat_backdrop_ci8.h"
#include "generated/quality_terrain_ci4.h"
#include "generated/renderer_comparison_scene.h"
#include "n64_showcase.h"

namespace {

namespace gpu = grandleon::playstation::gpu;
namespace psx = grandleon::playstation;
namespace view = grandleon::view;

constexpr std::uint32_t frame_budget_ticks =
    psx::ticks_per_1000_microseconds * 33U;

constexpr int quality_terrain_cell_base = 4;
constexpr int quality_grass_clut = 29;
constexpr int quality_forest_clut = 30;
constexpr int quality_stone_clut = 31;
constexpr int backdrop_texture_page = gpu::first_texture_page + 6;
constexpr int backdrop_overlay_page = gpu::first_texture_page + 9;
constexpr int backdrop_clut_x = 0;
constexpr int backdrop_overlay_clut_x = 256;
constexpr int backdrop_clut_y = gpu::clut_y + 1;

struct Cutout final {
    int page;
    int u;
    int v;
    int clut;
    int source_size;
};

constexpr int quality_close_page = gpu::first_texture_page + 3;
constexpr int tactical_cutout_page = gpu::first_texture_page + 5;

struct ClosePoseSlots final {
    Cutout idle;
    Cutout windup;
    Cutout follow;
    Cutout hurt;
};

constexpr ClosePoseSlots quality_left_slots{
    {quality_close_page, 0, 0, 11, grandleon_psx_godot_quality_close_width},
    {quality_close_page, 128, 0, 12, grandleon_psx_godot_quality_close_width},
    {quality_close_page, 0, 128, 13, grandleon_psx_godot_quality_close_width},
    {quality_close_page, 128, 128, 14, grandleon_psx_godot_quality_close_width},
};
constexpr ClosePoseSlots quality_right_slots{
    {quality_close_page + 1, 0, 0, 15, grandleon_psx_godot_quality_close_width},
    {quality_close_page + 1, 128, 0, 16, grandleon_psx_godot_quality_close_width},
    {quality_close_page + 1, 0, 128, 17, grandleon_psx_godot_quality_close_width},
    {quality_close_page + 1, 128, 128, 18, grandleon_psx_godot_quality_close_width},
};

constexpr Cutout tactical_cutouts[] = {
    {tactical_cutout_page, 0, 0, 19, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 64, 0, 20, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 128, 0, 21, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 192, 0, 22, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 0, 64, 23, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 64, 64, 24, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 128, 64, 25, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 192, 64, 26, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 0, 128, 27, grandleon_psx_godot_tactical_width},
    {tactical_cutout_page, 64, 128, 28, grandleon_psx_godot_tactical_width},
};

// One orthographic world-to-screen contract is shared by
// terrain, feet, shadows, cutouts and effects.  The basis can be frontal for
// the close view or isometric for the board, while the fit is derived from
// world bounds.
struct ElevatedCamera final {
    int centre_x;
    int centre_y;
    int xx;
    int xz;
    int yx;
    int yz;
    int up;
    int zoom;
};

struct RawBounds final {
    std::int32_t left;
    std::int32_t right;
    std::int32_t top;
    std::int32_t bottom;
    bool valid;
};

struct ProjectedVertex final {
    std::int16_t x;
    std::int16_t y;
    std::int32_t depth;
};

struct SceneResult final {
    std::uint32_t ticks;
    int textured_triangles;
};

std::uint16_t font_sheet[gpu::font_lines][gpu::font_halfwords_across];
int checks = 0;
int failures = 0;

constexpr std::uint8_t team_colours[2][3] = {
    {65, 132, 235}, {215, 74, 68},
};

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

void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) ++failures;
    psx::Line line;
    line.text(condition ? "ok  " : "FAIL ").text(description).flush();
}

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


[[nodiscard]] std::int32_t elevated_raw_x(
    int x, int z, const ElevatedCamera& camera
) noexcept {
    return x * camera.xx + z * camera.xz;
}

[[nodiscard]] std::int32_t elevated_raw_y(
    int x, int y, int z, const ElevatedCamera& camera
) noexcept {
    return x * camera.yx + z * camera.yz - y * camera.up;
}

[[nodiscard]] ProjectedVertex project_elevated(
    int x, int y, int z, const ElevatedCamera& camera
) noexcept {
    constexpr std::int32_t scale = 256 * 256;
    const std::int32_t raw_x = elevated_raw_x(x, z, camera);
    const std::int32_t raw_y = elevated_raw_y(x, y, z, camera);
    return {
        static_cast<std::int16_t>(
            camera.centre_x + raw_x * camera.zoom / scale
        ),
        static_cast<std::int16_t>(
            camera.centre_y + raw_y * camera.zoom / scale
        ),
        raw_y,
    };
}

void include_raw(RawBounds& bounds, std::int32_t x, std::int32_t y) noexcept {
    if (!bounds.valid) {
        bounds = {x, x, y, y, true};
        return;
    }
    if (x < bounds.left) bounds.left = x;
    if (x > bounds.right) bounds.right = x;
    if (y < bounds.top) bounds.top = y;
    if (y > bounds.bottom) bounds.bottom = y;
}

void include_world(
    RawBounds& bounds, int x, int y, int z, const ElevatedCamera& camera
) noexcept {
    include_raw(
        bounds, elevated_raw_x(x, z, camera),
        elevated_raw_y(x, y, z, camera)
    );
}

void include_billboard(
    RawBounds& bounds, int x, int z, int world_size,
    const ElevatedCamera& camera
) noexcept {
    const std::int32_t raw_x = elevated_raw_x(x, z, camera);
    const std::int32_t raw_y = elevated_raw_y(x, 0, z, camera);
    const std::int32_t half_width = world_size * 128;
    include_raw(bounds, raw_x - half_width, raw_y - world_size * 256);
    include_raw(bounds, raw_x + half_width, raw_y);
}

[[nodiscard]] ElevatedCamera fit_elevated_camera(
    ElevatedCamera camera, const RawBounds& bounds,
    int left, int top, int right, int bottom
) noexcept {
    constexpr std::int32_t scale = 256 * 256;
    const std::int32_t width = bounds.right - bounds.left;
    const std::int32_t height = bounds.bottom - bounds.top;
    const int zoom_x = width > 0
        ? static_cast<int>((right - left) * scale / width) : 256;
    const int zoom_y = height > 0
        ? static_cast<int>((bottom - top) * scale / height) : 256;
    camera.zoom = std::min(zoom_x, zoom_y);
    const std::int32_t raw_centre_x = (bounds.left + bounds.right) / 2;
    const std::int32_t raw_centre_y = (bounds.top + bounds.bottom) / 2;
    camera.centre_x = (left + right) / 2 -
        static_cast<int>(raw_centre_x * camera.zoom / scale);
    camera.centre_y = (top + bottom) / 2 -
        static_cast<int>(raw_centre_y * camera.zoom / scale);
    return camera;
}

[[nodiscard]] int elevated_billboard_size(
    int world_size, const ElevatedCamera& camera
) noexcept {
    return world_size * camera.zoom / 256;
}


void upload_cell(int cell, const unsigned short* source) {
    gpu::upload(
        gpu::cell_texture_x(cell), gpu::cell_texture_y(cell),
        gpu::cell_halfwords, gpu::cell_texels, source
    );
}

void upload_clut(int clut, const unsigned short* source) {
    gpu::upload(
        gpu::clut_x_of(clut), gpu::clut_y_of(clut), gpu::clut_entries, 1,
        source
    );
}


void load_quality_terrain() {
    static_assert(grandleon_psx_quality_terrain_side == gpu::cell_texels);
    static_assert(grandleon_psx_quality_terrain_cell_count == 44);
    for (int index = 0; index < grandleon_psx_quality_terrain_cell_count;
         ++index) {
        upload_cell(quality_terrain_cell_base + index,
                    grandleon_psx_quality_terrain_cells[index]);
    }
    upload_clut(quality_grass_clut, grandleon_psx_quality_terrain_cluts[0]);
    upload_clut(quality_forest_clut, grandleon_psx_quality_terrain_cluts[1]);
    upload_clut(quality_stone_clut, grandleon_psx_quality_terrain_cluts[2]);
}

[[nodiscard]] std::uint32_t load_combat_backdrop() {
    static_assert(grandleon_psx_backdrop_seed ==
                  grandleon_psx_comparison_backdrop_seed);
    static_assert(grandleon_psx_backdrop_variant ==
                  grandleon_psx_comparison_backdrop_variant);
    static_assert(grandleon_psx_backdrop_tile_width == 64);
    static_assert(grandleon_psx_backdrop_tile_height == 80);
    static_assert(grandleon_psx_backdrop_tile_count <= 18);
    static_assert(grandleon_psx_backdrop_overlay_tile_count <= 18);
    psx::start_clock();
    constexpr int main_tiles_per_page = 6;
    for (int index = 0; index < grandleon_psx_backdrop_tile_count; ++index) {
        const int page = backdrop_texture_page + index / main_tiles_per_page;
        const int within = index % main_tiles_per_page;
        const int u = (within % 2) * grandleon_psx_backdrop_tile_width;
        const int v = (within / 2) * grandleon_psx_backdrop_tile_height;
        gpu::upload(
            page * gpu::page_halfwords + u / 2,
            gpu::texture_page_y + v,
            grandleon_psx_backdrop_tile_width / 2,
            grandleon_psx_backdrop_tile_height,
            grandleon_psx_backdrop_tiles[index]
        );
    }
    for (int index = 0;
         index < grandleon_psx_backdrop_overlay_tile_count; ++index) {
        const int page = backdrop_overlay_page + index / main_tiles_per_page;
        const int within = index % main_tiles_per_page;
        const int u = (within % 2) *
                      grandleon_psx_backdrop_overlay_tile_width;
        const int v = (within / 2) *
                      grandleon_psx_backdrop_overlay_tile_height;
        gpu::upload(
            page * gpu::page_halfwords + u / 2,
            gpu::texture_page_y + v,
            grandleon_psx_backdrop_overlay_tile_width / 2,
            grandleon_psx_backdrop_overlay_tile_height,
            grandleon_psx_backdrop_overlay_tiles[index]
        );
    }
    gpu::upload(backdrop_clut_x, backdrop_clut_y, 256, 1,
                grandleon_psx_backdrop_clut);
    gpu::upload(backdrop_overlay_clut_x, backdrop_clut_y, 256, 1,
                grandleon_psx_backdrop_overlay_clut);
    static_cast<void>(gpu::read_back(
        backdrop_texture_page * gpu::page_halfwords, gpu::texture_page_y
    ));
    return psx::clock_ticks();
}

void draw_combat_backdrop() {
    constexpr int columns = grandleon_psx_backdrop_width /
                            grandleon_psx_backdrop_tile_width;
    constexpr int main_tiles_per_page = 6;
    for (int index = 0; index < grandleon_psx_backdrop_tile_count; ++index) {
        const int screen_x = (index % columns) *
                             grandleon_psx_backdrop_tile_width;
        const int screen_y = (index / columns) *
                             grandleon_psx_backdrop_tile_height;
        const int width = std::min(
            grandleon_psx_backdrop_tile_width,
            grandleon_psx_backdrop_width - screen_x
        );
        const int height = std::min(
            grandleon_psx_backdrop_tile_height,
            grandleon_psx_backdrop_height - screen_y
        );
        const int page = backdrop_texture_page + index / main_tiles_per_page;
        const int within = index % main_tiles_per_page;
        const int u = (within % 2) * grandleon_psx_backdrop_tile_width;
        const int v = (within / 2) * grandleon_psx_backdrop_tile_height;
        gpu::draw_texture_quad_ci8(
            screen_x, screen_y, width, height, page,
            u, v, u + width - 1, v + height - 1,
            backdrop_clut_x, backdrop_clut_y
        );
    }
}

void draw_combat_backdrop_overlay() {
    const int columns = (grandleon_psx_backdrop_overlay_width +
                         grandleon_psx_backdrop_overlay_tile_width - 1) /
                        grandleon_psx_backdrop_overlay_tile_width;
    for (int index = 0;
         index < grandleon_psx_backdrop_overlay_tile_count; ++index) {
        const int local_x = (index % columns) *
                            grandleon_psx_backdrop_overlay_tile_width;
        const int local_y = (index / columns) *
                            grandleon_psx_backdrop_overlay_tile_height;
        const int width = std::min(
            grandleon_psx_backdrop_overlay_tile_width,
            grandleon_psx_backdrop_overlay_width - local_x
        );
        const int height = std::min(
            grandleon_psx_backdrop_overlay_tile_height,
            grandleon_psx_backdrop_overlay_height - local_y
        );
        constexpr int overlay_tiles_per_page = 6;
        const int page = backdrop_overlay_page +
                         index / overlay_tiles_per_page;
        const int within = index % overlay_tiles_per_page;
        const int u = (within % 2) *
                      grandleon_psx_backdrop_overlay_tile_width;
        const int v = (within / 2) *
                      grandleon_psx_backdrop_overlay_tile_height;
        gpu::draw_texture_quad_ci8(
            grandleon_psx_backdrop_overlay_x + local_x,
            grandleon_psx_backdrop_overlay_y + local_y,
            width, height, page,
            u, v, u + width - 1, v + height - 1,
            backdrop_overlay_clut_x, backdrop_clut_y
        );
    }
}

void upload_cutout(const Cutout& cutout, const unsigned short* pixels) {
    gpu::upload(
        cutout.page * gpu::page_halfwords + cutout.u / 4,
        gpu::texture_page_y + cutout.v,
        cutout.source_size / 4,
        cutout.source_size,
        pixels
    );
}

void upload_cutout_and_clut(
    const Cutout& cutout, const unsigned short* pixels,
    const unsigned short* palette
) {
    upload_cutout(cutout, pixels);
    upload_clut(cutout.clut, palette);
}


void load_quality_close_cutouts(bool mythical) {
    if (mythical) {
        upload_cutout_and_clut(
            quality_left_slots.idle,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_idle,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_idle_clut
        );
        upload_cutout_and_clut(
            quality_left_slots.windup,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_windup,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_windup_clut
        );
        upload_cutout_and_clut(
            quality_left_slots.follow,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_follow,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_follow_clut
        );
        upload_cutout_and_clut(
            quality_left_slots.hurt,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_hurt,
            grandleon_psx_godot_closeup_quality_mythical_male_blue_hurt_clut
        );
        upload_cutout_and_clut(
            quality_right_slots.idle,
            grandleon_psx_godot_closeup_quality_mythical_female_red_idle,
            grandleon_psx_godot_closeup_quality_mythical_female_red_idle_clut
        );
        upload_cutout_and_clut(
            quality_right_slots.windup,
            grandleon_psx_godot_closeup_quality_mythical_female_red_windup,
            grandleon_psx_godot_closeup_quality_mythical_female_red_windup_clut
        );
        upload_cutout_and_clut(
            quality_right_slots.follow,
            grandleon_psx_godot_closeup_quality_mythical_female_red_follow,
            grandleon_psx_godot_closeup_quality_mythical_female_red_follow_clut
        );
        upload_cutout_and_clut(
            quality_right_slots.hurt,
            grandleon_psx_godot_closeup_quality_mythical_female_red_hurt,
            grandleon_psx_godot_closeup_quality_mythical_female_red_hurt_clut
        );
        return;
    }
    upload_cutout_and_clut(
        quality_left_slots.idle,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_idle,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        quality_left_slots.windup,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_windup,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_windup_clut
    );
    upload_cutout_and_clut(
        quality_left_slots.follow,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_follow,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_follow_clut
    );
    upload_cutout_and_clut(
        quality_left_slots.hurt,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_hurt,
        grandleon_psx_godot_closeup_quality_medieval_male_blue_hurt_clut
    );
    upload_cutout_and_clut(
        quality_right_slots.idle,
        grandleon_psx_godot_closeup_quality_medieval_female_red_idle,
        grandleon_psx_godot_closeup_quality_medieval_female_red_idle_clut
    );
    upload_cutout_and_clut(
        quality_right_slots.windup,
        grandleon_psx_godot_closeup_quality_medieval_female_red_windup,
        grandleon_psx_godot_closeup_quality_medieval_female_red_windup_clut
    );
    upload_cutout_and_clut(
        quality_right_slots.follow,
        grandleon_psx_godot_closeup_quality_medieval_female_red_follow,
        grandleon_psx_godot_closeup_quality_medieval_female_red_follow_clut
    );
    upload_cutout_and_clut(
        quality_right_slots.hurt,
        grandleon_psx_godot_closeup_quality_medieval_female_red_hurt,
        grandleon_psx_godot_closeup_quality_medieval_female_red_hurt_clut
    );
}

void load_tactical_cutouts() {
    upload_cutout_and_clut(
        tactical_cutouts[0],
        grandleon_psx_godot_closeup_tactical_medieval_knight_male_blue_idle,
        grandleon_psx_godot_closeup_tactical_medieval_knight_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[1],
        grandleon_psx_godot_closeup_tactical_medieval_archer_female_red_idle,
        grandleon_psx_godot_closeup_tactical_medieval_archer_female_red_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[2],
        grandleon_psx_godot_closeup_tactical_medieval_mage_male_blue_idle,
        grandleon_psx_godot_closeup_tactical_medieval_mage_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[3],
        grandleon_psx_godot_closeup_tactical_medieval_stormcaller_female_red_idle,
        grandleon_psx_godot_closeup_tactical_medieval_stormcaller_female_red_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[4],
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_idle,
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[5],
        grandleon_psx_godot_closeup_tactical_medieval_beast_female_red_idle,
        grandleon_psx_godot_closeup_tactical_medieval_beast_female_red_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[6],
        grandleon_psx_godot_closeup_tactical_medieval_healer_male_blue_idle,
        grandleon_psx_godot_closeup_tactical_medieval_healer_male_blue_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[7],
        grandleon_psx_godot_closeup_tactical_medieval_commander_female_red_idle,
        grandleon_psx_godot_closeup_tactical_medieval_commander_female_red_idle_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[8],
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_move_a,
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_move_a_clut
    );
    upload_cutout_and_clut(
        tactical_cutouts[9],
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_move_b,
        grandleon_psx_godot_closeup_tactical_medieval_rogue_male_blue_move_b_clut
    );
}


void draw_cutout(int centre_x, int bottom, int size, const Cutout& cutout) {
    gpu::draw_texture_quad(
        centre_x - size / 2, bottom - size, size, size,
        cutout.page, cutout.u, cutout.v,
        cutout.u + cutout.source_size - 1,
        cutout.v + cutout.source_size - 1,
        cutout.clut
    );
}


void draw_elevated_solid_quad(
    int x0, int z0, int x1, int z1, int y,
    const ElevatedCamera& camera, std::uint16_t colour
) {
    const ProjectedVertex a = project_elevated(x0, y, z0, camera);
    const ProjectedVertex b = project_elevated(x1, y, z0, camera);
    const ProjectedVertex c = project_elevated(x1, y, z1, camera);
    const ProjectedVertex d = project_elevated(x0, y, z1, camera);
    gpu::draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y, colour);
    gpu::draw_triangle(a.x, a.y, c.x, c.y, d.x, d.y, colour);
}

void draw_elevated_cell_quad(
    int x0, int z0, int x1, int z1, int y,
    const ElevatedCamera& camera, int cell, int clut
) {
    const ProjectedVertex a = project_elevated(x0, y, z0, camera);
    const ProjectedVertex b = project_elevated(x1, y, z0, camera);
    const ProjectedVertex c = project_elevated(x1, y, z1, camera);
    const ProjectedVertex d = project_elevated(x0, y, z1, camera);
    gpu::draw_cell_triangle(
        a.x, a.y, 0, 0, b.x, b.y, 31, 0,
        c.x, c.y, 31, 31, cell, clut
    );
    gpu::draw_cell_triangle(
        a.x, a.y, 0, 0, c.x, c.y, 31, 31,
        d.x, d.y, 0, 31, cell, clut
    );
}

[[nodiscard]] int quality_terrain_cell(
    int kind, int variant, int transition_mask
) noexcept {
    variant &= 3;
    if (kind == 0) return quality_terrain_cell_base + variant;
    if (kind == 1 && transition_mask != 0) {
        return quality_terrain_cell_base + 12 + transition_mask;
    }
    if (kind == 2 && transition_mask != 0) {
        return quality_terrain_cell_base + 28 + transition_mask;
    }
    return quality_terrain_cell_base + (kind == 1 ? 4 : 8) + variant;
}

[[nodiscard]] int quality_terrain_clut(int kind) noexcept {
    return kind == 0 ? quality_grass_clut
        : (kind == 1 ? quality_forest_clut : quality_stone_clut);
}

void draw_quality_terrain_tile(
    int x0, int z0, int x1, int z1, int kind, int variant,
    int transition_mask, const ElevatedCamera& camera
) {
    draw_elevated_cell_quad(
        x0, z0, x1, z1, 0, camera,
        quality_terrain_cell(kind, variant, transition_mask),
        quality_terrain_clut(kind)
    );
}

void draw_quality_cell_boundary(
    int x0, int z0, int x1, int z1, const ElevatedCamera& camera
) {
    const std::uint16_t colour = gpu::rgb15(4, 8, 6);
    draw_elevated_solid_quad(x1 - 2, z0, x1, z1, 2, camera, colour);
    draw_elevated_solid_quad(x0, z1 - 2, x1, z1, 2, camera, colour);
}

void draw_quality_tree(
    int x, int z, int size, const ElevatedCamera& camera
) {
    draw_elevated_solid_quad(
        x - size / 2, z - size / 5, x + size / 2, z + size / 5, 2,
        camera, gpu::rgb15(2, 5, 3)
    );
    const ProjectedVertex base = project_elevated(x, 2, z, camera);
    const ProjectedVertex trunk_top =
        project_elevated(x, size * 3 / 4, z, camera);
    const ProjectedVertex crown =
        project_elevated(x, size * 3 / 2, z, camera);
    gpu::fill(
        base.x - 2, trunk_top.y, 5, std::max(2, base.y - trunk_top.y),
        gpu::rgb15(10, 7, 4)
    );
    const int radius = std::max(6, size / 3);
    gpu::draw_triangle(
        crown.x, crown.y - radius,
        crown.x - radius, trunk_top.y + 2,
        crown.x + radius, trunk_top.y + 2,
        gpu::rgb15(4, 14, 7)
    );
    gpu::draw_triangle(
        crown.x, crown.y - radius / 3,
        crown.x - radius * 4 / 5, trunk_top.y - radius / 3,
        crown.x + radius * 4 / 5, trunk_top.y - radius / 3,
        gpu::rgb15(7, 20, 9)
    );
}

void draw_quality_rock(
    int x, int z, int size, const ElevatedCamera& camera
) {
    const ProjectedVertex base = project_elevated(x, 2, z, camera);
    const ProjectedVertex top = project_elevated(x - size / 5, size / 2, z, camera);
    gpu::draw_triangle(
        base.x - size / 2, base.y, top.x, top.y,
        base.x + size / 2, base.y, gpu::rgb15(13, 15, 15)
    );
    gpu::draw_triangle(
        base.x - size / 2, base.y, top.x, top.y,
        base.x - size / 5, base.y - size / 4, gpu::rgb15(17, 18, 17)
    );
}

void draw_quality_grass(
    int x, int z, int size, const ElevatedCamera& camera
) {
    const ProjectedVertex base = project_elevated(x, 2, z, camera);
    const ProjectedVertex tip = project_elevated(x, size, z, camera);
    const std::uint16_t colour = gpu::rgb15(10, 21, 9);
    gpu::draw_triangle(
        base.x - size / 4, base.y, tip.x - 2, tip.y,
        base.x, base.y, colour
    );
    gpu::draw_triangle(
        base.x, base.y, tip.x + 3, tip.y + 2,
        base.x + size / 4, base.y, colour
    );
}

[[nodiscard]] ElevatedCamera close_camera_v2() noexcept {
    ElevatedCamera camera{0, 0, 256, -64, 0, 180, 245, 256};
    RawBounds bounds{0, 0, 0, 0, false};
    constexpr int left = -168;
    constexpr int right = 168;
    constexpr int far = -96;
    constexpr int near = 144;
    include_world(bounds, left, 0, far, camera);
    include_world(bounds, right, 0, far, camera);
    include_world(bounds, left, 0, near, camera);
    include_world(bounds, right, 0, near, camera);
    include_billboard(bounds, -70, 0, 132, camera);
    include_billboard(bounds, 70, 0, 132, camera);
    return fit_elevated_camera(camera, bounds, 6, 22, 314, 219);
}

[[nodiscard]] int close_terrain_kind(int row, int column) noexcept {
    if (column == 3) return 2;
    if (column == 0 || column == 6 ||
        (row < 2 && (column == 1 || column == 5))) return 1;
    return 0;
}

[[nodiscard]] int close_transition_mask(int row, int column, int kind) noexcept {
    if (kind == 0) return 0;
    int mask = 0;
    if (row > 0 && close_terrain_kind(row - 1, column) != kind) mask |= 1;
    if (column < 6 && close_terrain_kind(row, column + 1) != kind) mask |= 2;
    if (row < 4 && close_terrain_kind(row + 1, column) != kind) mask |= 4;
    if (column > 0 && close_terrain_kind(row, column - 1) != kind) mask |= 8;
    return mask;
}

void draw_close_stage_v2(const ElevatedCamera& camera) {
    constexpr int tile = 48;
    constexpr int left = -168;
    constexpr int far = -96;
    // Every visible terrain primitive is a tile top in the same projection as
    // the combatants' feet. There are no vertical walls or fake horizons that
    // can rise behind the boots and invert the camera read.
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 7; ++column) {
            const int x0 = left + column * tile;
            const int z0 = far + row * tile;
            const int kind = close_terrain_kind(row, column);
            draw_quality_terrain_tile(
                x0, z0, x0 + tile, z0 + tile, kind,
                (row * 7 + column * 3) & 3,
                close_transition_mask(row, column, kind), camera
            );
        }
    }
}

void draw_close_far_decorations(const ElevatedCamera& camera) {
    draw_quality_tree(-143, -64, 32, camera);
    draw_quality_tree(137, -70, 38, camera);
    draw_quality_tree(-124, -34, 24, camera);
    draw_quality_rock(-18, -65, 13, camera);
    draw_quality_rock(25, -42, 10, camera);
    draw_quality_grass(-90, -18, 12, camera);
}

void draw_close_near_decorations(const ElevatedCamera& camera) {
    draw_quality_grass(-145, 108, 13, camera);
    draw_quality_grass(132, 117, 11, camera);
    draw_quality_rock(18, 104, 12, camera);
}

void draw_elevated_marker(
    int x, int z, int team, int radius, const ElevatedCamera& camera
) {
    draw_elevated_solid_quad(
        x - radius, z - radius / 3, x + radius, z + radius / 3, 2,
        camera, gpu::rgb15(2, 3, 3)
    );
    draw_elevated_solid_quad(
        x - radius * 2 / 3, z - 2, x + radius * 2 / 3, z + 3, 3,
        camera, colour15(team_colours[team], 220)
    );
}

constexpr int close_exchange_frames = 40;
constexpr int close_left_positions[close_exchange_frames] = {
    -70, -70, -70, -70, -71, -73, -74, -72, -67, -61,
    -54, -48, -45, -46, -50, -56, -62, -67, -69, -70,
    -70, -70, -70, -70, -70, -70, -70, -70, -70, -70,
    -70, -70, -78, -77, -76, -74, -72, -71, -70, -70,
};
constexpr int close_right_positions[close_exchange_frames] = {
    70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
    70, 70, 78, 77, 76, 74, 72, 71, 70, 70,
    70, 70, 70, 70, 71, 73, 74, 72, 67, 61,
    54, 48, 45, 46, 50, 56, 62, 67, 69, 70,
};

[[nodiscard]] constexpr int close_left_x(int phase) noexcept {
    return phase >= 0 && phase < close_exchange_frames
        ? close_left_positions[phase] : -70;
}

[[nodiscard]] constexpr int close_right_x(int phase) noexcept {
    return phase >= 0 && phase < close_exchange_frames
        ? close_right_positions[phase] : 70;
}

[[nodiscard]] const Cutout& quality_left_pose(int phase) noexcept {
    if (phase >= 32 && phase <= 36) return quality_left_slots.hurt;
    if (phase >= 12 && phase <= 16) return quality_left_slots.follow;
    if (phase >= 4 && phase <= 11) return quality_left_slots.windup;
    return quality_left_slots.idle;
}

[[nodiscard]] const Cutout& quality_right_pose(int phase) noexcept {
    if (phase >= 12 && phase <= 16) return quality_right_slots.hurt;
    if (phase >= 32 && phase <= 36) return quality_right_slots.follow;
    if (phase >= 24 && phase <= 31) return quality_right_slots.windup;
    return quality_right_slots.idle;
}

void draw_world_impact(
    int x, int y, int z, const ElevatedCamera& camera
) {
    const ProjectedVertex centre = project_elevated(x, y, z, camera);
    gpu::draw_triangle(
        centre.x - 11, centre.y, centre.x, centre.y - 17,
        centre.x + 11, centre.y, gpu::rgb15(31, 22, 5)
    );
    gpu::draw_triangle(
        centre.x - 8, centre.y, centre.x, centre.y + 13,
        centre.x + 8, centre.y, gpu::rgb15(31, 30, 18)
    );
    gpu::fill(centre.x - 2, centre.y - 10, 5, 20, gpu::rgb15(31, 27, 10));
}

void draw_capture_health(
    const ProjectedVertex& foot, int size, int health, std::uint16_t team
) {
    const int top = std::max(4, foot.y - size + 1);
    gpu::fill(foot.x - 14, top, 28, 5, gpu::rgb15(2, 2, 3));
    gpu::fill(foot.x - 13, top + 1, health * 2, 3,
              health > 8 ? gpu::rgb15(7, 24, 9) : gpu::rgb15(27, 17, 5));
    gpu::fill(foot.x - 18, top, 4, 5, team);
    text_at(foot.x - 5, top - 9,
            health == 12 ? "12" : (health == 11 ? "11" : "8"));
}

[[nodiscard]] ElevatedCamera comparison_close_camera() noexcept {
    ElevatedCamera camera = close_camera_v2();
    camera.centre_x = gpu::screen_width / 2;
    const ProjectedVertex current_ground = project_elevated(-70, 3, 0, camera);
    const int wanted_ground = gpu::screen_height *
        grandleon_psx_comparison_close_layout_milli[2] / 1000;
    camera.centre_y += wanted_ground - current_ground.y;
    return camera;
}

[[nodiscard]] SceneResult draw_hybrid_close_v2(
    int phase, const char* title, const char* subtitle, std::uint16_t sky,
    bool capture_mode = false, bool backdrop_mode = true,
    int left_health = 12, int right_health = 11
) {
    const ElevatedCamera camera = capture_mode
        ? comparison_close_camera() : close_camera_v2();
    const Cutout& left = quality_left_pose(phase);
    const Cutout& right = quality_right_pose(phase);
    const int left_x = close_left_x(phase) - (capture_mode ? 14 : 0);
    const int right_x = close_right_x(phase) + (capture_mode ? 14 : 0);
    const int unit_world_size = capture_mode ? 170 : 132;
    const int unit_size = elevated_billboard_size(unit_world_size, camera);

    gpu::clear(sky);
    psx::start_clock();
    if (capture_mode && backdrop_mode) {
        draw_combat_backdrop();
        draw_combat_backdrop_overlay();
    } else {
        draw_close_stage_v2(camera);
        draw_close_far_decorations(camera);
    }
    draw_elevated_marker(left_x, 0, 0, 40, camera);
    draw_elevated_marker(right_x, 0, 1, 40, camera);
    const ProjectedVertex left_foot = project_elevated(left_x, 3, 0, camera);
    const ProjectedVertex right_foot = project_elevated(right_x, 3, 0, camera);
    // The curated square has transparent breathing room below the painted
    // shoes. Move only that transparent margin through the projected foot
    // line so the visible soles meet their world-space grounding marks.
    const int transparent_sole_margin = unit_size * 2 / left.source_size;
    draw_cutout(
        left_foot.x, left_foot.y + transparent_sole_margin, unit_size, left
    );
    draw_cutout(
        right_foot.x, right_foot.y + transparent_sole_margin, unit_size, right
    );
    if (phase == 12) {
        draw_world_impact(right_x - 13, 68, 0, camera);
        const ProjectedVertex damage = project_elevated(right_x, 102, 0, camera);
        text_at(damage.x - 3, damage.y,
                grandleon_psx_comparison_damage[0] == 3 ? "3" : "?");
    }
    if (phase == 32) {
        draw_world_impact(left_x + 13, 68, 0, camera);
        const ProjectedVertex damage = project_elevated(left_x, 102, 0, camera);
        text_at(damage.x - 3, damage.y,
                grandleon_psx_comparison_damage[1] == 4 ? "4" : "?");
    }
    if (!capture_mode || !backdrop_mode) {
        draw_close_near_decorations(camera);
    }

    if (capture_mode) {
        draw_capture_health(
            left_foot, unit_size, left_health,
            grandleon_psx_comparison_team_colours[0]
        );
        draw_capture_health(
            right_foot, unit_size, right_health,
            grandleon_psx_comparison_team_colours[1]
        );
    } else {
        gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
        gpu::fill(6, 4, 7, 10, colour15(team_colours[0]));
        text_at(18, 5, title);
        gpu::fill(306, 4, 7, 10, colour15(team_colours[1]));
        gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
        text_at(8, 227, subtitle);
    }
    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(),
            capture_mode && backdrop_mode
                ? grandleon_psx_backdrop_tile_count +
                  grandleon_psx_backdrop_overlay_tile_count + 4
                : 72};
}

struct OverviewUnit final {
    int column;
    int row;
    int team;
    const Cutout* cutout;
};


constexpr OverviewUnit quality_overview_units[] = {
    {0, 1, 0, &tactical_cutouts[0]},
    {7, 1, 1, &tactical_cutouts[1]},
    {2, 2, 0, &tactical_cutouts[2]},
    {5, 2, 1, &tactical_cutouts[3]},
    {0, 3, 0, &tactical_cutouts[4]},
    {7, 3, 1, &tactical_cutouts[5]},
    {2, 4, 0, &tactical_cutouts[6]},
    {5, 4, 1, &tactical_cutouts[7]},
};

constexpr int quality_movement_frames = 16;
constexpr int quality_move_progress[quality_movement_frames] = {
    0, 1, 2, 4, 7, 11, 16, 22, 28, 34, 39, 42, 44, 45, 46, 46,
};


[[nodiscard]] ElevatedCamera overview_camera_v2() noexcept {
    ElevatedCamera camera{0, 0, 220, -220, 105, 105, 260, 256};
    RawBounds bounds{0, 0, 0, 0, false};
    constexpr int tile = 46;
    constexpr int left = -sevenfold_board_columns * tile / 2;
    constexpr int right = left + sevenfold_board_columns * tile;
    constexpr int far = -sevenfold_board_rows * tile / 2;
    constexpr int near = far + sevenfold_board_rows * tile;
    include_world(bounds, left, 0, far, camera);
    include_world(bounds, right, 0, far, camera);
    include_world(bounds, left, 0, near, camera);
    include_world(bounds, right, 0, near, camera);
    for (const OverviewUnit& unit : quality_overview_units) {
        const int x = left + unit.column * tile + tile / 2;
        const int z = far + unit.row * tile + tile / 2;
        include_billboard(bounds, x, z, 80, camera);
    }
    // The selected unit's final movement destination is part of the fitted
    // scene bounds too, so a real animation never escapes the camera contract.
    include_billboard(bounds, left + tile / 2, far + 4 * tile + tile / 2,
                      80, camera);
    return fit_elevated_camera(camera, bounds, 7, 22, 313, 218);
}

struct OverviewTileOrder final {
    int row;
    int column;
    std::int32_t depth;
};

struct OverviewUnitOrder final {
    int index;
    int x;
    int z;
    std::int32_t depth;
};

[[nodiscard]] int overview_quality_kind(int row, int column) noexcept {
    if (row < 2 && column < 3) return 1;
    if (column == 3 || column == 4 ||
        (row < 2 && column > 5) || (row > 3 && column < 2)) return 2;
    return 0;
}

[[nodiscard]] int overview_transition_mask(
    int row, int column, int kind
) noexcept {
    if (kind == 0) return 0;
    int mask = 0;
    if (row > 0 && overview_quality_kind(row - 1, column) != kind) mask |= 1;
    if (column + 1 < sevenfold_board_columns &&
        overview_quality_kind(row, column + 1) != kind) mask |= 2;
    if (row + 1 < sevenfold_board_rows &&
        overview_quality_kind(row + 1, column) != kind) mask |= 4;
    if (column > 0 && overview_quality_kind(row, column - 1) != kind) mask |= 8;
    return mask;
}

void draw_overview_ground_v2(const ElevatedCamera& camera) {
    constexpr int tile = 46;
    constexpr int left = -sevenfold_board_columns * tile / 2;
    constexpr int far = -sevenfold_board_rows * tile / 2;
    OverviewTileOrder order[sevenfold_board_rows * sevenfold_board_columns]{};
    int count = 0;
    for (int row = 0; row < sevenfold_board_rows; ++row) {
        for (int column = 0; column < sevenfold_board_columns; ++column) {
            const int x = left + column * tile + tile / 2;
            const int z = far + row * tile + tile / 2;
            order[count++] = {row, column, elevated_raw_y(x, 0, z, camera)};
        }
    }
    std::sort(order, order + count,
              [](const OverviewTileOrder& a, const OverviewTileOrder& b) {
                  return a.depth < b.depth;
              });
    for (int index = 0; index < count; ++index) {
        const int row = order[index].row;
        const int column = order[index].column;
        const int x0 = left + column * tile;
        const int z0 = far + row * tile;
        const int kind = overview_quality_kind(row, column);
        draw_quality_terrain_tile(
            x0, z0, x0 + tile, z0 + tile, kind,
            (row * 5 + column * 3) & 3,
            overview_transition_mask(row, column, kind), camera
        );
        draw_quality_cell_boundary(
            x0, z0, x0 + tile, z0 + tile, camera
        );
    }
}

struct QualityProp final {
    int x;
    int z;
    int kind;
    int size;
    std::int32_t depth;
};

void draw_quality_prop(const QualityProp& prop, const ElevatedCamera& camera) {
    if (prop.kind == 0) draw_quality_tree(prop.x, prop.z, prop.size, camera);
    else if (prop.kind == 1) draw_quality_rock(prop.x, prop.z, prop.size, camera);
    else draw_quality_grass(prop.x, prop.z, prop.size, camera);
}

void make_overview_props(QualityProp* props, const ElevatedCamera& camera) {
    constexpr int definitions[9][4] = {
        {-164, -116, 0, 20}, {-120, -108, 0, 25}, {-78, -104, 0, 18},
        {118, -112, 1, 13}, {162, -70, 1, 16}, {-160, 112, 1, 12},
        {-115, 105, 1, 10}, {115, 88, 2, 11}, {158, 104, 2, 9},
    };
    for (int index = 0; index < 9; ++index) {
        props[index] = {
            definitions[index][0], definitions[index][1], definitions[index][2],
            definitions[index][3], elevated_raw_y(
                definitions[index][0], 0, definitions[index][1], camera
            ),
        };
    }
    std::sort(props, props + 9,
              [](const QualityProp& a, const QualityProp& b) {
                  return a.depth < b.depth;
              });
}

void draw_movement_highlights_v2(const ElevatedCamera& camera) {
    constexpr int tile = 46;
    constexpr int left = -sevenfold_board_columns * tile / 2;
    constexpr int far = -sevenfold_board_rows * tile / 2;
    constexpr int cells[][2] = {{3, 0}, {2, 0}, {4, 0}, {3, 1}, {2, 1}};
    for (int index = 0; index < 5; ++index) {
        const int row = cells[index][0];
        const int column = cells[index][1];
        const int x0 = left + column * tile + 5;
        const int z0 = far + row * tile + 5;
        const std::uint16_t colour = index == 0
            ? gpu::rgb15(10, 24, 31) : gpu::rgb15(6, 17, 24);
        draw_elevated_solid_quad(
            x0, z0, x0 + tile - 10, z0 + tile - 10, 2, camera, colour
        );
    }
}

void draw_unit_status(
    const ProjectedVertex& anchor, int size, int team, int health,
    bool selected
) {
    const int top = anchor.y - size + 2;
    gpu::fill(anchor.x - 11, top, 22, 4, gpu::rgb15(2, 2, 3));
    const std::uint16_t health_colour = health >= 60
        ? gpu::rgb15(7, 24, 9) : gpu::rgb15(27, 17, 5);
    gpu::fill(anchor.x - 10, top + 1, health / 5, 2, health_colour);
    if (selected) {
        gpu::draw_triangle(
            anchor.x - 5, top - 5, anchor.x + 5, top - 5, anchor.x, top,
            colour15(team_colours[team])
        );
    }
}

[[nodiscard]] SceneResult draw_overview_v2(int move_step) {
    constexpr int tile = 46;
    constexpr int left = -sevenfold_board_columns * tile / 2;
    constexpr int far = -sevenfold_board_rows * tile / 2;
    constexpr int unit_count = static_cast<int>(
        sizeof(quality_overview_units) / sizeof(quality_overview_units[0])
    );
    const ElevatedCamera camera = overview_camera_v2();
    OverviewUnitOrder order[unit_count]{};
    for (int index = 0; index < unit_count; ++index) {
        const OverviewUnit& unit = quality_overview_units[index];
        const int x = left + unit.column * tile + tile / 2;
        int z = far + unit.row * tile + tile / 2;
        if (move_step >= 0 && index == 4) {
            const int frame = std::min(move_step, quality_movement_frames - 1);
            z += quality_move_progress[frame];
        }
        order[index] = {index, x, z, elevated_raw_y(x, 0, z, camera)};
    }
    std::sort(order, order + unit_count,
              [](const OverviewUnitOrder& a, const OverviewUnitOrder& b) {
                  return a.depth < b.depth;
              });

    gpu::clear(gpu::rgb15(4, 8, 11));
    psx::start_clock();
    draw_overview_ground_v2(camera);
    draw_movement_highlights_v2(camera);
    QualityProp props[9]{};
    int next_prop = 0;
    make_overview_props(props, camera);
    const int unit_size = elevated_billboard_size(80, camera);
    for (int sorted = 0; sorted < unit_count; ++sorted) {
        const OverviewUnitOrder& placed = order[sorted];
        while (next_prop < 9 && props[next_prop].depth <= placed.depth) {
            draw_quality_prop(props[next_prop++], camera);
        }
        const OverviewUnit& unit = quality_overview_units[placed.index];
        const bool moving = move_step >= 0 && placed.index == 4;
        draw_elevated_marker(placed.x, placed.z, unit.team, 17, camera);
        const ProjectedVertex foot =
            project_elevated(placed.x, 3, placed.z, camera);
        const Cutout& art = moving
            ? ((move_step & 1) == 0
                ? tactical_cutouts[8] : tactical_cutouts[9])
            : *unit.cutout;
        draw_cutout(
            foot.x, foot.y + unit_size * 2 / art.source_size, unit_size, art
        );
        draw_unit_status(
            foot, unit_size, unit.team, 92 - placed.index * 5,
            placed.index == 4
        );
    }
    while (next_prop < 9) {
        draw_quality_prop(props[next_prop++], camera);
    }
    gpu::fill(0, 0, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 5, move_step >= 0
        ? "TACTICAL CAMERA V2 MOVE" : "TACTICAL CAMERA V2");
    gpu::fill(0, 222, gpu::screen_width, 18, gpu::rgb15(2, 3, 5));
    text_at(8, 227, "HIGH ISO  8 UNITS  REACH  HEALTH  TEAM");
    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(), 104};
}

constexpr int comparison_tile = 40;
constexpr int comparison_grass = 0;
constexpr int comparison_forest = 1;
constexpr int comparison_paved = 2;

[[nodiscard]] int comparison_kind(int row, int column) noexcept {
    return grandleon_psx_comparison_surroundings[
        row * grandleon_psx_comparison_surrounding_width + column
    ];
}

[[nodiscard]] ElevatedCamera comparison_board_camera(int focus = 0) noexcept {
    // The steeper basis mirrors the schema-2 Godot camera: the complete 12x10
    // diorama reads from above and fills the native frame without title bands.
    ElevatedCamera camera{0, 0, 220, -220, 150, 150, 270, 256};
    RawBounds bounds{0, 0, 0, 0, false};
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int right = left + grandleon_psx_comparison_surrounding_width *
                          comparison_tile;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    constexpr int near = far + grandleon_psx_comparison_surrounding_height *
                         comparison_tile;
    include_world(bounds, left, 0, far, camera);
    include_world(bounds, right, 0, far, camera);
    include_world(bounds, left, 0, near, camera);
    include_world(bounds, right, 0, near, camera);
    camera = fit_elevated_camera(
        camera, bounds,
        grandleon_psx_comparison_safe_margin[0],
        grandleon_psx_comparison_safe_margin[1],
        gpu::screen_width - grandleon_psx_comparison_safe_margin[0],
        gpu::screen_height - grandleon_psx_comparison_safe_margin[1]
    );
    if (focus > 0) {
        const int step = std::min(focus, 3);
        camera.zoom = camera.zoom * (100 + step * 7) / 100;
    }
    return camera;
}

[[nodiscard]] int comparison_transition_mask(
    int row, int column, int kind
) noexcept {
    if (kind == comparison_grass) return 0;
    int mask = 0;
    if (row > 0 && comparison_kind(row - 1, column) != kind) mask |= 1;
    if (column + 1 < grandleon_psx_comparison_surrounding_width &&
        comparison_kind(row, column + 1) != kind) mask |= 2;
    if (row + 1 < grandleon_psx_comparison_surrounding_height &&
        comparison_kind(row + 1, column) != kind) mask |= 4;
    if (column > 0 && comparison_kind(row, column - 1) != kind) mask |= 8;
    return mask;
}

[[nodiscard]] bool comparison_active_cell(int row, int column) noexcept {
    return column >= grandleon_psx_comparison_active_origin_x &&
           column < grandleon_psx_comparison_active_origin_x +
                    grandleon_psx_comparison_board_width &&
           row >= grandleon_psx_comparison_active_origin_y &&
           row < grandleon_psx_comparison_active_origin_y +
                 grandleon_psx_comparison_board_height;
}

void draw_comparison_marker(
    int row, int column, std::uint16_t colour, const ElevatedCamera& camera
) {
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    const int x0 = left + column * comparison_tile + 4;
    const int z0 = far + row * comparison_tile + 4;
    const int x1 = x0 + comparison_tile - 8;
    const int z1 = z0 + comparison_tile - 8;
    draw_elevated_solid_quad(x0, z0, x1, z0 + 3, 3, camera, colour);
    draw_elevated_solid_quad(x0, z1 - 3, x1, z1, 3, camera, colour);
    draw_elevated_solid_quad(x0, z0, x0 + 3, z1, 3, camera, colour);
    draw_elevated_solid_quad(x1 - 3, z0, x1, z1, 3, camera, colour);
}

int make_comparison_props(QualityProp* props, const ElevatedCamera& camera) {
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    int count = 0;
    for (int row = 0; row < grandleon_psx_comparison_surrounding_height;
         ++row) {
        for (int column = 0;
             column < grandleon_psx_comparison_surrounding_width; ++column) {
            const int kind = comparison_kind(row, column);
            const bool active = comparison_active_cell(row, column);
            const int centre_x = left + column * comparison_tile +
                                 comparison_tile / 2;
            const int centre_z = far + row * comparison_tile +
                                 comparison_tile / 2;
            if (kind == comparison_forest) {
                const short* offsets = active
                    ? grandleon_psx_comparison_active_tree_xz_milli
                    : grandleon_psx_comparison_surrounding_tree_xz_milli;
                const unsigned short* scales = active
                    ? grandleon_psx_comparison_active_tree_scale_milli
                    : grandleon_psx_comparison_surrounding_tree_scale_milli;
                for (int tree = 0; tree < 2; ++tree) {
                    const int x = centre_x +
                        offsets[tree * 2] * comparison_tile / 1000;
                    const int z = centre_z +
                        offsets[tree * 2 + 1] * comparison_tile / 1000;
                    const int variation =
                        grandleon_psx_comparison_cluster_height_milli[0] +
                        ((row * 17 + column * 11 + tree * 23) % 401);
                    const int size = comparison_tile * 2 * scales[tree] *
                        variation / 3000000;
                    props[count++] = {
                        x, z, 0, std::max(12, size),
                        elevated_raw_y(x, 0, z, camera),
                    };
                }
                if (active) {
                    props[count++] = {
                        centre_x, centre_z + 9, 2, 8,
                        elevated_raw_y(centre_x, 0, centre_z + 9, camera),
                    };
                }
            } else if (active && kind == comparison_grass) {
                constexpr int offsets[6][2] = {
                    {-11, -9}, {10, -7}, {-7, 10}, {12, 9}, {-13, 3}, {5, 13},
                };
                for (int detail = 0; detail < 6; ++detail) {
                    const int x = centre_x + offsets[detail][0];
                    const int z = centre_z + offsets[detail][1];
                    props[count++] = {
                        x, z, detail < 4 ? 2 : 1, detail < 4 ? 7 : 5,
                        elevated_raw_y(x, 0, z, camera),
                    };
                }
            }
        }
    }
    std::sort(props, props + count,
              [](const QualityProp& a, const QualityProp& b) {
                  return a.depth < b.depth;
              });
    return count;
}

void draw_comparison_prop(
    const QualityProp& prop, const ElevatedCamera& camera
) {
    if (prop.kind != 0) {
        draw_quality_prop(prop, camera);
        return;
    }
    draw_elevated_solid_quad(
        prop.x - prop.size / 3, prop.z - 2,
        prop.x + prop.size / 3, prop.z + 2, 2, camera,
        gpu::rgb15(2, 5, 3)
    );
    const ProjectedVertex base = project_elevated(prop.x, 2, prop.z, camera);
    const ProjectedVertex trunk =
        project_elevated(prop.x, prop.size * 3 / 4, prop.z, camera);
    const ProjectedVertex crown =
        project_elevated(prop.x, prop.size * 3 / 2, prop.z, camera);
    gpu::fill(base.x - 1, trunk.y, 3, std::max(2, base.y - trunk.y),
              gpu::rgb15(9, 6, 3));
    const int radius = std::max(3, (base.y - crown.y) / 3);
    const int colour_shift = (prop.x / 7 + prop.z / 11) & 3;
    gpu::draw_triangle(
        crown.x, crown.y - radius,
        crown.x - radius, trunk.y + 1,
        crown.x + radius, trunk.y + 1,
        gpu::rgb15(4, 13 + colour_shift, 7)
    );
    gpu::draw_triangle(
        crown.x, crown.y,
        crown.x - radius * 3 / 4, trunk.y - radius / 2,
        crown.x + radius * 3 / 4, trunk.y - radius / 2,
        gpu::rgb15(7, 19 + colour_shift, 9)
    );
}

void draw_comparison_paved_details(const ElevatedCamera& camera) {
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    const std::uint16_t cyan = gpu::rgb15(5, 20, 23);
    for (int active_row = 0;
         active_row < grandleon_psx_comparison_board_height; ++active_row) {
        for (int active_column = 0;
             active_column < grandleon_psx_comparison_board_width;
             ++active_column) {
            const int row = active_row + grandleon_psx_comparison_active_origin_y;
            const int column = active_column +
                               grandleon_psx_comparison_active_origin_x;
            if (comparison_kind(row, column) != comparison_paved) continue;
            const int x0 = left + column * comparison_tile;
            const int z0 = far + row * comparison_tile;
            draw_elevated_solid_quad(
                x0 + 8, z0 + 10, x0 + 18, z0 + 12, 2, camera, cyan
            );
            draw_elevated_solid_quad(
                x0 + 24, z0 + 27, x0 + 34, z0 + 29, 2, camera, cyan
            );
        }
    }
}

struct ComparisonUnitOrder final {
    int index;
    int x;
    int z;
    std::int32_t depth;
};

[[nodiscard]] SceneResult draw_comparison_board(
    int focus, int blue_hp, int red_hp
) {
    constexpr int left = -grandleon_psx_comparison_surrounding_width *
                         comparison_tile / 2;
    constexpr int far = -grandleon_psx_comparison_surrounding_height *
                        comparison_tile / 2;
    const ElevatedCamera camera = comparison_board_camera(focus);
    constexpr int cell_count = grandleon_psx_comparison_surrounding_width *
                               grandleon_psx_comparison_surrounding_height;
    OverviewTileOrder order[cell_count]{};
    int count = 0;
    for (int row = 0; row < grandleon_psx_comparison_surrounding_height;
         ++row) {
        for (int column = 0;
             column < grandleon_psx_comparison_surrounding_width; ++column) {
            const int x = left + column * comparison_tile + comparison_tile / 2;
            const int z = far + row * comparison_tile + comparison_tile / 2;
            order[count++] = {row, column, elevated_raw_y(x, 0, z, camera)};
        }
    }
    std::sort(order, order + count,
              [](const OverviewTileOrder& a, const OverviewTileOrder& b) {
                  return a.depth < b.depth;
              });

    gpu::clear(gpu::rgb15(4, 8, 11));
    psx::start_clock();
    for (int index = 0; index < count; ++index) {
        const int row = order[index].row;
        const int column = order[index].column;
        const int x0 = left + column * comparison_tile;
        const int z0 = far + row * comparison_tile;
        const int kind = comparison_kind(row, column);
        draw_quality_terrain_tile(
            x0, z0, x0 + comparison_tile, z0 + comparison_tile, kind,
            (row * 5 + column * 3) & 3,
            comparison_transition_mask(row, column, kind), camera
        );
        if (comparison_active_cell(row, column)) {
            draw_quality_cell_boundary(
                x0, z0, x0 + comparison_tile, z0 + comparison_tile, camera
            );
        }
    }
    draw_comparison_paved_details(camera);

    // The forest extension frames the active board, but never wins the final
    // painter pass over tactical information. This retains the explicit two
    // trees per forest cell while keeping the compact units and grid legible.
    QualityProp props[240]{};
    const int prop_count = make_comparison_props(props, camera);
    for (int index = 0; index < prop_count; ++index) {
        draw_comparison_prop(props[index], camera);
    }
    for (int row = grandleon_psx_comparison_active_origin_y;
         row < grandleon_psx_comparison_active_origin_y +
               grandleon_psx_comparison_board_height; ++row) {
        for (int column = grandleon_psx_comparison_active_origin_x;
             column < grandleon_psx_comparison_active_origin_x +
                      grandleon_psx_comparison_board_width; ++column) {
            const int x0 = left + column * comparison_tile;
            const int z0 = far + row * comparison_tile;
            draw_quality_cell_boundary(
                x0, z0, x0 + comparison_tile, z0 + comparison_tile, camera
            );
        }
    }

    const int selected_column = grandleon_psx_comparison_active_origin_x +
                                grandleon_psx_comparison_unit_cells[0];
    const int selected_row = grandleon_psx_comparison_active_origin_y +
                             grandleon_psx_comparison_unit_cells[1];
    const int target_column = grandleon_psx_comparison_active_origin_x +
                              grandleon_psx_comparison_unit_cells[2];
    const int target_row = grandleon_psx_comparison_active_origin_y +
                           grandleon_psx_comparison_unit_cells[3];
    draw_comparison_marker(
        selected_row, selected_column,
        grandleon_psx_comparison_marker_colours[0], camera
    );
    draw_comparison_marker(
        target_row, target_column,
        grandleon_psx_comparison_marker_colours[1], camera
    );

    ComparisonUnitOrder units[2]{};
    const int unit_columns[2] = {selected_column, target_column};
    const int unit_rows[2] = {selected_row, target_row};
    for (int index = 0; index < 2; ++index) {
        const int x = left + unit_columns[index] * comparison_tile +
                      comparison_tile / 2;
        const int z = far + unit_rows[index] * comparison_tile +
                      comparison_tile / 2;
        units[index] = {index, x, z, elevated_raw_y(x, 0, z, camera)};
    }
    std::sort(units, units + 2,
              [](const ComparisonUnitOrder& a, const ComparisonUnitOrder& b) {
                  return a.depth < b.depth;
              });

    const int unit_size = elevated_billboard_size(118, camera);
    for (const ComparisonUnitOrder& unit : units) {
        const ProjectedVertex foot = project_elevated(unit.x, 3, unit.z, camera);
        const Cutout& art = unit.index == 0
            ? quality_left_slots.idle : quality_right_slots.idle;
        draw_elevated_marker(unit.x, unit.z, unit.index, 16, camera);
        draw_cutout(
            foot.x, foot.y + unit_size * 2 / art.source_size, unit_size, art
        );
        draw_capture_health(
            foot, unit_size, unit.index == 0 ? blue_hp : red_hp,
            grandleon_psx_comparison_team_colours[unit.index]
        );
    }

    gpu::show();
    static_cast<void>(gpu::read_back(0, 0));
    return {psx::clock_ticks(), cell_count * 2};
}

void print_benchmark(const char* label, const SceneResult& result) {
    psx::Line ticks;
    ticks.text("BENCH ").text(label).text(" ticks=").decimal(result.ticks)
        .text(" us=")
        .decimal(result.ticks * 1000U / psx::ticks_per_1000_microseconds)
        .text(" textured=")
        .decimal(static_cast<std::uint32_t>(result.textured_triangles))
        .flush();
}

}  // namespace

int main() {
    gpu::begin(gpu::rgb15(6, 10, 14));
    build_font();
    load_quality_terrain();
    load_tactical_cutouts();
    load_quality_close_cutouts(false);
    const std::uint32_t backdrop_upload_ticks = load_combat_backdrop();
    print_benchmark("combat-backdrop-upload", {backdrop_upload_ticks, 0});

    constexpr int quality_close_vram_bytes =
        8 * grandleon_psx_godot_quality_close_width *
        grandleon_psx_godot_quality_close_height / 2;
    constexpr int tactical_vram_bytes =
        10 * grandleon_psx_godot_tactical_width *
        grandleon_psx_godot_tactical_height / 2;
    constexpr int quality_terrain_vram_bytes =
        grandleon_psx_quality_terrain_cell_count * gpu::cell_texels *
        gpu::cell_texels / 2;
    constexpr int backdrop_vram_bytes =
        grandleon_psx_backdrop_width * grandleon_psx_backdrop_height +
        grandleon_psx_backdrop_overlay_width *
        grandleon_psx_backdrop_overlay_height + 2 * 256 * 2;
    constexpr int texture_page_bytes =
        gpu::page_halfwords * gpu::page_lines * 2;
    expect(quality_close_vram_bytes == texture_page_bytes * 2,
           "eight close poses fill two texture pages");
    expect(tactical_vram_bytes == 20480 &&
               tactical_vram_bytes < texture_page_bytes,
           "ten tactical poses fit one texture page");
    expect(quality_terrain_vram_bytes == 22528,
           "forty-four terrain cells use twenty-two KiB");
    expect(backdrop_vram_bytes <= 6 * texture_page_bytes,
           "full-frame backdrop and overlays fit six pages");
    expect(backdrop_upload_ticks > 0,
           "combat backdrop upload transition is measured");
    expect(grandleon_psx_backdrop_dominant_terrain_id == 2 &&
               grandleon_psx_backdrop_secondary_terrain_id == 9 &&
               grandleon_psx_backdrop_transition_terrain_id == 12,
           "backdrop keeps the shared forest grass paved selector");
    expect(grandleon_psx_backdrop_height == gpu::screen_height,
           "one continuous backdrop owns the full close view");
    expect(gpu::read_back(
               gpu::cell_texture_x(quality_terrain_cell_base),
               gpu::cell_texture_y(quality_terrain_cell_base)) != 0,
           "quality terrain reached VRAM");

    const ElevatedCamera capture_close = comparison_close_camera();
    const ProjectedVertex capture_left =
        project_elevated(-84, 3, 0, capture_close);
    const ProjectedVertex capture_right =
        project_elevated(84, 3, 0, capture_close);
    const int expected_left = gpu::screen_width *
        grandleon_psx_comparison_close_layout_milli[0] / 1000;
    const int expected_right = gpu::screen_width *
        grandleon_psx_comparison_close_layout_milli[1] / 1000;
    const int expected_ground = gpu::screen_height *
        grandleon_psx_comparison_close_layout_milli[2] / 1000;
    expect(capture_left.x >= expected_left - 2 &&
               capture_left.x <= expected_left + 2 &&
               capture_right.x >= expected_right - 2 &&
               capture_right.x <= expected_right + 2 &&
               capture_left.y == expected_ground &&
               capture_right.y == expected_ground,
           "fighters keep the shared slots and ground line");

    const SceneResult overview = draw_overview_v2(-1);
    print_benchmark("tactical-board", overview);
    psx::signal_host(24);

    std::uint32_t slowest_movement = 0;
    for (int frame = 0; frame < quality_movement_frames; ++frame) {
        const SceneResult result = draw_overview_v2(frame);
        slowest_movement = std::max(slowest_movement, result.ticks);
        psx::signal_host(25);
    }
    print_benchmark("tactical-movement-slowest", {slowest_movement, 104});

    const SceneResult backdrop = draw_hybrid_close_v2(
        -1, "CLOSE BATTLE", "GODOT CUTOUTS + TERRAIN BACKDROP",
        gpu::rgb15(7, 13, 17), true, true,
        grandleon_psx_comparison_unit_initial_hp[0],
        grandleon_psx_comparison_unit_initial_hp[1]
    );
    print_benchmark("combat-backdrop", backdrop);
    psx::signal_host(33);

    std::uint32_t slowest_exchange = 0;
    for (int frame = 0; frame < close_exchange_frames; ++frame) {
        const SceneResult result = draw_hybrid_close_v2(
            frame, "ATTACK + COUNTER", "FIXED SIDES  TERRAIN BACKDROP",
            gpu::rgb15(7, 13, 17), true, true
        );
        slowest_exchange = std::max(slowest_exchange, result.ticks);
        psx::signal_host(26);
    }
    print_benchmark("attack-exchange-slowest", {slowest_exchange, 0});

    SceneResult sequence_frames[52]{};
    int sequence_at = 0;
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_comparison_board(
            0, grandleon_psx_comparison_unit_initial_hp[0],
            grandleon_psx_comparison_unit_initial_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_comparison_board(
            frame + 1, grandleon_psx_comparison_unit_initial_hp[0],
            grandleon_psx_comparison_unit_initial_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_hybrid_close_v2(
            -1, "MATCH CLOSE", "BLUE 12  RED 11", gpu::rgb15(7, 13, 17),
            true, true, grandleon_psx_comparison_unit_initial_hp[0],
            grandleon_psx_comparison_unit_initial_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < close_exchange_frames; ++frame) {
        const int left_hp = frame >= 32
            ? grandleon_psx_comparison_unit_final_hp[0]
            : grandleon_psx_comparison_unit_initial_hp[0];
        const int right_hp = frame >= 12
            ? grandleon_psx_comparison_unit_final_hp[1]
            : grandleon_psx_comparison_unit_initial_hp[1];
        sequence_frames[sequence_at++] = draw_hybrid_close_v2(
            frame, "MATCH CLOSE", frame < 20 ? "BLUE HITS 3" : "RED COUNTERS 4",
            gpu::rgb15(7, 13, 17), true, true, left_hp, right_hp
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_hybrid_close_v2(
            -1, "MATCH CLOSE", "BLUE 8  RED 8", gpu::rgb15(7, 13, 17),
            true, true, grandleon_psx_comparison_unit_final_hp[0],
            grandleon_psx_comparison_unit_final_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_comparison_board(
            2 - frame, grandleon_psx_comparison_unit_final_hp[0],
            grandleon_psx_comparison_unit_final_hp[1]
        );
        psx::signal_host(29);
    }
    for (int frame = 0; frame < 2; ++frame) {
        sequence_frames[sequence_at++] = draw_comparison_board(
            0, grandleon_psx_comparison_unit_final_hp[0],
            grandleon_psx_comparison_unit_final_hp[1]
        );
        psx::signal_host(29);
    }

    std::uint32_t slowest_sequence = 0;
    for (const SceneResult& result : sequence_frames) {
        slowest_sequence = std::max(slowest_sequence, result.ticks);
    }
    print_benchmark("comparison-full-map", sequence_frames[0]);
    print_benchmark("comparison-sequence-slowest", {slowest_sequence, 0});
    expect(sequence_at == 52, "comparison sequence emits all timed frames");
    expect(overview.ticks > 0 && overview.ticks <= frame_budget_ticks,
           "tactical board stays inside the thirty fps budget");
    expect(slowest_movement > 0 && slowest_movement <= frame_budget_ticks,
           "tactical movement stays inside the thirty fps budget");
    expect(backdrop.ticks > 0 && backdrop.ticks <= frame_budget_ticks,
           "combat backdrop stays inside the thirty fps budget");
    expect(slowest_exchange > 0 && slowest_exchange <= frame_budget_ticks,
           "attack exchange stays inside the thirty fps budget");
    expect(slowest_sequence > 0 && slowest_sequence <= frame_budget_ticks,
           "matched sequence stays inside the thirty fps budget");
    expect(quality_terrain_cell(1, 0, 0) !=
               quality_terrain_cell(1, 0, 2),
           "terrain transition masks select distinct textures");

    const ElevatedCamera close = close_camera_v2();
    const ProjectedVertex far_top = project_elevated(0, 0, -96, close);
    const ProjectedVertex near_top = project_elevated(0, 0, 144, close);
    const ProjectedVertex left_foot = project_elevated(-70, 0, 0, close);
    const ProjectedVertex right_foot = project_elevated(70, 0, 0, close);
    const ProjectedVertex left_head = project_elevated(-70, 100, 0, close);
    expect(near_top.y - far_top.y >= 80 && left_head.y < left_foot.y &&
               left_foot.y == right_foot.y,
           "close camera keeps depth, world up and one foot line");
    bool fixed_sides = true;
    for (int phase = 0; phase < close_exchange_frames; ++phase) {
        fixed_sides = fixed_sides && close_left_x(phase) < 0 &&
            close_right_x(phase) > 0 &&
            close_left_x(phase) < close_right_x(phase);
    }
    expect(fixed_sides, "attack and counter retain screen sides");

    psx::Line result;
    result.text(failures == 0 ? "RESULT PASS " : "RESULT FAIL ")
        .decimal(static_cast<std::uint32_t>(checks - failures))
        .text("/").decimal(static_cast<std::uint32_t>(checks)).flush();
    psx::signal_host(20);
    return failures == 0 ? 0 : 1;
}
