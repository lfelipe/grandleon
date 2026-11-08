// SPDX-License-Identifier: MIT

#include "console_renderer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <grandleon/view/motion.hpp>

#include "console_character_assets.h"
#include "console_backdrop_assets.h"
#include "console_terrain_assets.h"

namespace grandleon::nintendo64::console_renderer {
namespace {

constexpr int pose_count = grandleon_console_pose_count;

struct LoadedDrawing final {
    sprite_t* board[pose_count]{};
    sprite_t* close[pose_count]{};
};

LoadedDrawing drawings[grandleon_console_drawing_count]{};
sprite_t* terrain_atlas = nullptr;
constexpr int backdrop_columns = 4;
constexpr int backdrop_rows = 6;
constexpr int backdrop_tiles_per_variant = backdrop_columns * backdrop_rows;
constexpr int backdrop_variants = 3;
sprite_t* backdrop_tiles[backdrop_tiles_per_variant * backdrop_variants]{};
bool ready = false;
bool close_attached = false;

[[nodiscard]] sprite_t* load(
    const unsigned char* source, std::size_t size
) noexcept {
    if (source == nullptr || size < sizeof(sprite_t)) return nullptr;
    void* copy = std::malloc(size);
    if (copy == nullptr) return nullptr;
    std::memcpy(copy, source, size);
    sprite_t* sprite = sprite_load_buf(copy, static_cast<int>(size));
    if (sprite == nullptr) std::free(copy);
    return sprite;
}

[[nodiscard]] constexpr int pose_index(Pose pose) noexcept {
    return static_cast<int>(pose);
}

struct TerrainLook final {
    std::uint8_t material;
};

struct DepthPoint final {
    int x;
    int y;
};

// Package terrain kinds are in the shared registry's order:
// water, road, forest, mountain, sand, snow, swamp, hills, ruins, grass,
// farmland, bamboo and paved.  The four material regions provide structure;
// authored colour keeps every kind immediately distinguishable.
constexpr TerrainLook terrain_looks[] = {
    {3}, {2}, {0}, {1}, {2}, {3}, {3}, {0}, {1}, {0}, {2}, {0}, {1},
};

[[nodiscard]] constexpr TerrainLook terrain_look(
    std::uint8_t kind
) noexcept {
    return kind < sizeof terrain_looks / sizeof terrain_looks[0]
        ? terrain_looks[kind] : terrain_looks[9];
}

void depth_flat_triangle(
    DepthPoint first, DepthPoint second, DepthPoint third, color_t colour
) {
    rdpq_set_prim_color(colour);
    const float a[] = {
        static_cast<float>(first.x), static_cast<float>(first.y),
    };
    const float b[] = {
        static_cast<float>(second.x), static_cast<float>(second.y),
    };
    const float c[] = {
        static_cast<float>(third.x), static_cast<float>(third.y),
    };
    rdpq_triangle(&TRIFMT_FILL, a, b, c);
}

void depth_textured_triangle(
    DepthPoint first, float first_s, float first_t,
    float first_r, float first_g, float first_b,
    DepthPoint second, float second_s, float second_t,
    float second_r, float second_g, float second_b,
    DepthPoint third, float third_s, float third_t,
    float third_r, float third_g, float third_b
) {
    const float a[] = {
        static_cast<float>(first.x), static_cast<float>(first.y),
        first_r, first_g, first_b, 1.0F, first_s, first_t, 1.0F,
    };
    const float b[] = {
        static_cast<float>(second.x), static_cast<float>(second.y),
        second_r, second_g, second_b, 1.0F, second_s, second_t, 1.0F,
    };
    const float c[] = {
        static_cast<float>(third.x), static_cast<float>(third.y),
        third_r, third_g, third_b, 1.0F, third_s, third_t, 1.0F,
    };
    rdpq_triangle(&TRIFMT_SHADE_TEX, a, b, c);
}

void configure_depth_terrain_pass() {
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
}

[[nodiscard]] constexpr std::uint8_t scaled_channel(
    std::uint8_t channel, int numerator, int denominator
) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(channel) * static_cast<unsigned>(numerator)) /
        static_cast<unsigned>(denominator)
    );
}

void configure_character_pass() {
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
}

void draw_loaded_character(
    sprite_t* sprite, int centre_x, int bottom, int size, bool facing_left
) {
    if (sprite == nullptr || size <= 0) return;
    const int left = centre_x - size / 2;
    const int top = bottom - size;
    rdpq_blitparms_t parameters{};
    parameters.scale_x = static_cast<float>(size) /
        static_cast<float>(sprite->width);
    parameters.scale_y = static_cast<float>(size) /
        static_cast<float>(sprite->height);
    parameters.flip_x = facing_left;
    rdpq_sprite_blit(
        sprite, static_cast<float>(left), static_cast<float>(top), &parameters
    );
}

[[nodiscard]] sprite_t* close_sprite(int drawing, Pose pose) {
    if (drawing < 0 ||
        static_cast<std::size_t>(drawing) >= grandleon_console_drawing_count) {
        return nullptr;
    }
    const int index = pose_index(pose);
    sprite_t*& cached = drawings[drawing].close[index];
    if (cached == nullptr) {
        cached = load(
            grandleon_console_drawings[drawing].close[index],
            grandleon_console_drawings[drawing].close_size[index]
        );
    }
    return cached;
}

[[nodiscard]] bool ensure_backdrop_variant(std::uint32_t variation) {
    const int first = static_cast<int>(
        variation % static_cast<std::uint32_t>(backdrop_variants)
    ) *
        backdrop_tiles_per_variant;
    bool complete = true;
    for (int offset = 0; offset < backdrop_tiles_per_variant; ++offset) {
        const int index = first + offset;
        if (backdrop_tiles[index] == nullptr) {
            backdrop_tiles[index] = load(
                console_backdrop_data[index], console_backdrop_sizes[index]
            );
        }
        complete = complete && backdrop_tiles[index] != nullptr;
    }
    return complete;
}

}  // namespace

bool initialize() {
    if (ready) return true;
    bool complete = true;
    for (std::size_t drawing = 0;
         drawing < grandleon_console_drawing_count; ++drawing) {
        for (int pose = 0; pose < pose_count; ++pose) {
            drawings[drawing].board[pose] = load(
                grandleon_console_drawings[drawing].board[pose],
                grandleon_console_drawings[drawing].board_size[pose]
            );
            // Close poses other than idle are loaded only when an exchange
            // actually uses them. Keeping every 84px pose resident at boot
            // wastes roughly a quarter of the N64's available RDRAM on art a
            // given battle may never show.
            if (pose == pose_index(Pose::idle)) {
                drawings[drawing].close[pose] = load(
                    grandleon_console_drawings[drawing].close[pose],
                    grandleon_console_drawings[drawing].close_size[pose]
                );
            }
            complete = complete && drawings[drawing].board[pose] != nullptr &&
                (pose != pose_index(Pose::idle) ||
                 drawings[drawing].close[pose] != nullptr);
        }
    }
    terrain_atlas = load(
        console_terrain_material_atlas,
        console_terrain_material_atlas_size
    );
    complete = complete && terrain_atlas != nullptr;
    ready = complete;
    return ready;
}

int drawing_index(DrawingKey key) noexcept {
    for (std::size_t index = 0;
         index < grandleon_console_drawing_count; ++index) {
        const auto& drawing = grandleon_console_drawings[index];
        if (drawing.style == key.style && drawing.figure == key.figure &&
            drawing.archetype == key.archetype &&
            drawing.colour == key.colour) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::uint8_t default_style() noexcept {
    return static_cast<std::uint8_t>(grandleon_console_default_style);
}

std::uint8_t default_figure() noexcept {
    return static_cast<std::uint8_t>(grandleon_console_default_figure);
}

void begin_depth_board(surface_t* display) {
    if (!ready && !initialize()) return;
    rdpq_attach(display, nullptr);
    rdpq_clear(color_t{8, 21, 31, 255});
    // A quiet vertical field gives the projected board atmosphere and makes
    // its silhouette visible without spending a texture upload on scenery
    // outside the authored map.
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    for (int band = 0; band < 6; ++band) {
        rdpq_set_prim_color(RGBA32(
            static_cast<std::uint8_t>(10 + band * 3),
            static_cast<std::uint8_t>(24 + band * 4),
            static_cast<std::uint8_t>(34 + band * 5), 255
        ));
        rdpq_fill_rectangle(0, band * 40, 320, (band + 1) * 40);
    }
    configure_depth_terrain_pass();
    rdpq_sprite_upload(TILE0, terrain_atlas, nullptr);
}

void draw_depth_terrain(
    int centre_x, int centre_y, int half_width, int half_height, int lift,
    grandleon::view::TerrainComposition terrain
) {
    if (!ready || half_width <= 0 || half_height <= 0) return;
    const DepthPoint top{centre_x, centre_y - half_height - lift};
    const DepthPoint right{centre_x + half_width, centre_y - lift};
    const DepthPoint bottom{centre_x, centre_y + half_height - lift};
    const DepthPoint left{centre_x - half_width, centre_y - lift};
    const grandleon::view::TerrainColour tint =
        grandleon::view::terrain_base_colour(terrain.kind);

    if (lift > 0) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_dithering(DITHER_BAYER_NONE);
        const color_t west = RGBA32(
            scaled_channel(tint.red, 9, 16),
            scaled_channel(tint.green, 9, 16),
            scaled_channel(tint.blue, 9, 16), 255
        );
        const color_t east = RGBA32(
            scaled_channel(tint.red, 7, 16),
            scaled_channel(tint.green, 7, 16),
            scaled_channel(tint.blue, 7, 16), 255
        );
        depth_flat_triangle(left, bottom, {bottom.x, bottom.y + lift}, west);
        depth_flat_triangle(
            left, {bottom.x, bottom.y + lift}, {left.x, left.y + lift}, west
        );
        depth_flat_triangle(bottom, right, {right.x, right.y + lift}, east);
        depth_flat_triangle(
            bottom, {right.x, right.y + lift},
            {bottom.x, bottom.y + lift}, east
        );
        configure_depth_terrain_pass();
    }

    const TerrainLook look = terrain_look(terrain.kind);
    const float source_left = static_cast<float>((look.material & 1U) * 32U);
    const float source_top =
        static_cast<float>(((look.material >> 1U) & 1U) * 32U);
    constexpr float variation[][2] = {
        {0.0F, 0.0F}, {3.0F, 5.0F}, {7.0F, 2.0F}, {5.0F, 7.0F},
    };
    const float offset_s = variation[terrain.variation & 3U][0];
    const float offset_t = variation[terrain.variation & 3U][1];
    const float red = static_cast<float>(tint.red) / 255.0F;
    const float green = static_cast<float>(tint.green) / 255.0F;
    const float blue = static_cast<float>(tint.blue) / 255.0F;
    depth_textured_triangle(
        top, source_left + 16.0F + offset_s, source_top + offset_t,
        red, green, blue,
        right, source_left + 31.0F, source_top + 16.0F + offset_t,
        red * 0.96F, green * 0.96F, blue * 0.96F,
        bottom, source_left + 16.0F + offset_s, source_top + 31.0F,
        red * 0.76F, green * 0.76F, blue * 0.76F
    );
    depth_textured_triangle(
        top, source_left + 16.0F + offset_s, source_top + offset_t,
        red, green, blue,
        bottom, source_left + 16.0F + offset_s, source_top + 31.0F,
        red * 0.76F, green * 0.76F, blue * 0.76F,
        left, source_left, source_top + 16.0F + offset_t,
        red * 0.88F, green * 0.88F, blue * 0.88F
    );
}

void draw_depth_detail(
    int centre_x, int base_y, int half_width, int half_height,
    grandleon::view::TerrainComposition terrain
) {
    if (!ready || half_width < 8 || half_height < 4) return;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    const auto kind = static_cast<grandleon::view::ConsoleTerrainKind>(
        terrain.kind
    );
    const grandleon::view::TerrainColour accent =
        grandleon::view::terrain_colour(
            terrain.kind, grandleon::view::TerrainTone::accent
        );
    const color_t bright = RGBA32(accent.red, accent.green, accent.blue, 255);
    const color_t shade = RGBA32(
        scaled_channel(accent.red, 9, 16),
        scaled_channel(accent.green, 9, 16),
        scaled_channel(accent.blue, 9, 16), 255
    );
    const int flip = (terrain.detail & 1U) != 0U ? 1 : -1;
    switch (kind) {
        case grandleon::view::ConsoleTerrainKind::forest:
        case grandleon::view::ConsoleTerrainKind::bamboo: {
            if ((terrain.detail & 1U) != 0U) break;
            const int trunk_x = centre_x + flip * (half_width / 4);
            const int crown_y = base_y - half_height - 12;
            rdpq_set_prim_color(RGBA32(78, 53, 31, 255));
            rdpq_fill_rectangle(trunk_x - 2, crown_y + 8,
                                trunk_x + 2, base_y - 1);
            depth_flat_triangle(
                {trunk_x, crown_y - 7},
                {trunk_x + 9, crown_y + 9},
                {trunk_x - 9, crown_y + 9}, bright
            );
            if (terrain.cluster >= 2U) {
                const int other_x = centre_x - flip * (half_width / 3);
                depth_flat_triangle(
                    {other_x, crown_y - 1},
                    {other_x + 6, crown_y + 10},
                    {other_x - 6, crown_y + 10}, shade
                );
            }
            break;
        }
        case grandleon::view::ConsoleTerrainKind::mountain:
        case grandleon::view::ConsoleTerrainKind::hills:
            depth_flat_triangle(
                {centre_x, base_y - half_height - 10},
                {centre_x + half_width - 3, base_y - 2},
                {centre_x - half_width + 3, base_y - 2}, shade
            );
            depth_flat_triangle(
                {centre_x, base_y - half_height - 10},
                {centre_x + half_width - 3, base_y - 2},
                {centre_x + 2, base_y - 5}, bright
            );
            break;
        case grandleon::view::ConsoleTerrainKind::ruins:
        case grandleon::view::ConsoleTerrainKind::paved:
            rdpq_set_prim_color(shade);
            rdpq_fill_rectangle(
                centre_x - half_width / 2, base_y - half_height,
                centre_x - half_width / 2 + 3, base_y - 2
            );
            rdpq_fill_rectangle(
                centre_x + half_width / 4, base_y - half_height + 3,
                centre_x + half_width / 4 + 3, base_y - 2
            );
            break;
        case grandleon::view::ConsoleTerrainKind::farmland:
            rdpq_set_prim_color(bright);
            for (int row = -1; row <= 1; ++row) {
                const int y = base_y - half_height / 2 + row * 3;
                rdpq_fill_rectangle(
                    centre_x - half_width / 2, y,
                    centre_x + half_width / 2, y + 1
                );
            }
            break;
        default:
            break;
    }
}

void draw_depth_landmark(
    int centre_x, int base_y, int half_width, int half_height,
    grandleon::view::LandmarkCell cell
) {
    if (!ready || !cell) return;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    const bool house = cell.landmark ==
        grandleon::view::TacticalLandmark::village_house;
    const bool right = cell.quadrant ==
                           grandleon::view::LandmarkQuadrant::top_right ||
        cell.quadrant ==
            grandleon::view::LandmarkQuadrant::bottom_right;
    const bool bottom = cell.quadrant ==
                            grandleon::view::LandmarkQuadrant::bottom_left ||
        cell.quadrant ==
            grandleon::view::LandmarkQuadrant::bottom_right;
    const int offset_x = (right ? -1 : 1) * half_width / 3;
    if (house) {
        const int left = centre_x + offset_x - half_width / 2;
        const int top = base_y - half_height - (bottom ? 16 : 10);
        rdpq_set_prim_color(RGBA32(213, 186, 132, 255));
        rdpq_fill_rectangle(left, top + 7, left + half_width, base_y);
        depth_flat_triangle(
            {left - 2, top + 8}, {left + half_width / 2, top - 2},
            {left + half_width + 2, top + 8}, RGBA32(132, 55, 43, 255)
        );
        rdpq_set_prim_color(RGBA32(74, 47, 31, 255));
        rdpq_fill_rectangle(
            left + half_width / 2 - 2, base_y - 8,
            left + half_width / 2 + 2, base_y
        );
        return;
    }
    const int trunk_x = centre_x + offset_x;
    const int crown_y = base_y - half_height - (bottom ? 12 : 18);
    rdpq_set_prim_color(RGBA32(80, 55, 32, 255));
    rdpq_fill_rectangle(trunk_x - 2, crown_y + 8, trunk_x + 2, base_y);
    depth_flat_triangle(
        {trunk_x, crown_y - 8}, {trunk_x + half_width / 2, crown_y + 10},
        {trunk_x - half_width / 2, crown_y + 10},
        RGBA32(49, 105, 58, 255)
    );
}

void draw_depth_castle(
    int centre_x, int base_y, int half_width, int half_height,
    grandleon::view::CastleTile tile
) {
    if (!ready || tile == grandleon::view::CastleTile::none) return;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    if (tile == grandleon::view::CastleTile::floor) return;
    const int left = centre_x - half_width + 2;
    const int right = centre_x + half_width - 2;
    if (tile == grandleon::view::CastleTile::dais) {
        const int top = base_y - half_height - 5;
        rdpq_set_prim_color(RGBA32(104, 91, 78, 255));
        rdpq_fill_rectangle(left + 4, top, right - 4, base_y - 1);
        rdpq_set_prim_color(RGBA32(204, 170, 91, 255));
        rdpq_fill_rectangle(left + 7, top + 2, right - 7, top + 4);
        return;
    }
    const int top = base_y - half_height - 20;
    rdpq_set_prim_color(RGBA32(76, 74, 71, 255));
    rdpq_fill_rectangle(left, top + 3, right, base_y - 1);
    rdpq_set_prim_color(RGBA32(147, 141, 129, 255));
    rdpq_fill_rectangle(left + 2, top + 5, right - 2, base_y - 2);
    rdpq_set_prim_color(RGBA32(207, 193, 159, 255));
    rdpq_fill_rectangle(left + 2, top + 5, right - 2, top + 8);
    if (tile == grandleon::view::CastleTile::door) {
        rdpq_set_prim_color(RGBA32(67, 38, 29, 255));
        rdpq_fill_rectangle(
            centre_x - half_width / 3, top + 10,
            centre_x + half_width / 3, base_y - 1
        );
    } else {
        for (int notch = 0; notch < 3; ++notch) {
            const int x = left + 2 + notch * (half_width - 2) / 2;
            rdpq_set_prim_color(RGBA32(76, 74, 71, 255));
            rdpq_fill_rectangle(x, top, x + 4, top + 6);
        }
    }
}

void draw_depth_shadow(
    int centre_x, int base_y, int half_width, int half_height
) {
    if (!ready || half_width <= 0 || half_height <= 0) return;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_BAYER_NONE);
    const int wide = std::max(5, (half_width * 3) / 4);
    const int tall = std::max(2, half_height / 3);
    const color_t shadow = RGBA32(18, 20, 22, 255);
    depth_flat_triangle(
        {centre_x - wide, base_y}, {centre_x, base_y - tall},
        {centre_x + wide, base_y}, shadow
    );
    depth_flat_triangle(
        {centre_x - wide, base_y}, {centre_x + wide, base_y},
        {centre_x, base_y + tall}, shadow
    );
}

void draw_depth_character(
    int drawing, Pose pose, int centre_x, int bottom, int size,
    bool facing_left
) {
    if (!ready || drawing < 0 || size <= 0 ||
        static_cast<std::size_t>(drawing) >= grandleon_console_drawing_count) {
        return;
    }
    configure_character_pass();
    draw_loaded_character(
        drawings[drawing].board[pose_index(pose)], centre_x, bottom, size,
        facing_left
    );
}

void draw_depth_character_overlay(
    surface_t* display, int drawing, Pose pose, int centre_x, int bottom,
    int size, bool facing_left
) {
    if (!ready || drawing < 0 || size <= 0 ||
        static_cast<std::size_t>(drawing) >= grandleon_console_drawing_count) {
        return;
    }
    rdpq_attach(display, nullptr);
    configure_character_pass();
    draw_loaded_character(
        drawings[drawing].board[pose_index(pose)], centre_x, bottom, size,
        facing_left
    );
    rdpq_detach_wait();
}

void end_board() {
    if (ready) rdpq_detach_wait();
}

void begin_close(
    surface_t* display, grandleon::view::TerrainComposition terrain
) {
    close_attached = false;
    if ((!ready && !initialize()) ||
        !ensure_backdrop_variant(terrain.battle_variant)) {
        return;
    }
    rdpq_attach(display, nullptr);
    close_attached = true;
    rdpq_clear(color_t{7, 19, 29, 255});
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    const int first = static_cast<int>(terrain.battle_variant % backdrop_variants) *
        backdrop_tiles_per_variant;
    for (int row = 0; row < backdrop_rows; ++row) {
        for (int column = 0; column < backdrop_columns; ++column) {
            sprite_t* tile = backdrop_tiles[
                first + row * backdrop_columns + column
            ];
            rdpq_sprite_upload(TILE0, tile, nullptr);
            // Four pixels of vertical overlap hide independent CI4 palette
            // seams; the final row is clipped by the 240-line framebuffer.
            rdpq_texture_rectangle_scaled(
                TILE0, column * 80, row * 44,
                (column + 1) * 80, (row + 1) * 44,
                0, 0, 80, 40
            );
        }
    }
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    grandleon::view::paint_console_close_terrain(
        terrain, 320, 240,
        [terrain](int x, int y, int width, int height,
                  grandleon::view::TerrainTone tone) {
            const grandleon::view::TerrainColour colour =
                grandleon::view::terrain_colour(terrain.kind, tone);
            rdpq_set_prim_color(
                RGBA32(colour.red, colour.green, colour.blue, 255)
            );
            rdpq_fill_rectangle(x, y, x + width, y + height);
        }
    );
}

void draw_close_character(
    int drawing, Pose pose, bool left_side, bool facing_left
) {
    if (!ready || !close_attached) return;
    configure_character_pass();
    draw_loaded_character(
        close_sprite(drawing, pose), left_side ? 88 : 232, 220, 112,
        facing_left
    );
}

void end_close() {
    if (close_attached) rdpq_detach_wait();
    close_attached = false;
}

sprite_t* portrait(int drawing) noexcept {
    if (!ready || drawing < 0 ||
        static_cast<std::size_t>(drawing) >= grandleon_console_drawing_count) {
        return nullptr;
    }
    return drawings[drawing].board[pose_index(Pose::idle)];
}

sprite_t* board_sprite(int drawing, Pose pose) noexcept {
    if (!ready || drawing < 0 ||
        static_cast<std::size_t>(drawing) >= grandleon_console_drawing_count) {
        return nullptr;
    }
    return drawings[drawing].board[pose_index(pose)];
}

std::size_t embedded_character_bytes() noexcept {
    std::size_t result = 0;
    for (const auto& drawing : grandleon_console_drawings) {
        for (int pose = 0; pose < pose_count; ++pose) {
            result += drawing.board_size[pose] + drawing.close_size[pose];
        }
    }
    return result;
}

}  // namespace grandleon::nintendo64::console_renderer
