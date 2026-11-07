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
constexpr int material_side = 32;

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
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

// Package terrain kinds are in the shared registry's order:
// water, road, forest, mountain, sand, snow, swamp, hills, ruins, grass,
// farmland, bamboo and paved.  The four material regions provide structure;
// authored colour keeps every kind immediately distinguishable.
constexpr TerrainLook terrain_looks[] = {
    {3, 43, 121, 158}, {2, 142, 104, 66}, {0, 58, 112, 63},
    {1, 112, 124, 132}, {2, 190, 150, 86}, {3, 205, 224, 230},
    {3, 72, 104, 82}, {0, 108, 137, 73}, {1, 116, 105, 96},
    {0, 89, 151, 70}, {2, 157, 129, 72}, {0, 65, 127, 78},
    {1, 144, 151, 145},
};

[[nodiscard]] constexpr TerrainLook terrain_look(
    std::uint8_t kind
) noexcept {
    return kind < sizeof terrain_looks / sizeof terrain_looks[0]
        ? terrain_looks[kind] : terrain_looks[9];
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

void begin_board(surface_t* display) {
    if (!ready && !initialize()) return;
    rdpq_attach(display, nullptr);
    rdpq_clear(color_t{7, 19, 29, 255});
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_sprite_upload(TILE0, terrain_atlas, nullptr);
}

void draw_terrain(
    int left, int top, int width, int height,
    std::uint8_t terrain_kind, std::uint8_t variation
) {
    if (!ready || width <= 0 || height <= 0) return;
    const TerrainLook look = terrain_look(terrain_kind);
    const int source_left = (look.material & 1) * material_side;
    const int source_top = ((look.material >> 1) & 1) * material_side;
    // Stable offsets keep a repeated material from reading as wallpaper while
    // staying inside its 32-pixel atlas region.
    constexpr int offsets[][2] = {{0, 0}, {5, 9}, {11, 3}, {17, 13}};
    const int offset_s = offsets[variation & 3][0];
    const int offset_t = offsets[variation & 3][1];
    rdpq_set_prim_color(RGBA32(look.red, look.green, look.blue, 255));
    rdpq_texture_rectangle_scaled(
        TILE0, left, top, left + width, top + height,
        source_left + offset_s, source_top + offset_t,
        source_left + material_side, source_top + material_side
    );
}

void draw_landmark(
    int left, int top, int size, grandleon::view::LandmarkCell cell
) {
    if (!ready || !cell || size <= 0) return;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    grandleon::view::paint_landmark_cell(
        cell, left, top, size,
        [cell](
            int x, int y, int width, int height,
            grandleon::view::LandmarkTone tone
        ) {
            struct Colour final {
                std::uint8_t red;
                std::uint8_t green;
                std::uint8_t blue;
            };
            constexpr Colour house[] = {
                {55, 35, 25}, {220, 190, 135}, {145, 52, 44},
                {92, 58, 36}, {238, 205, 105},
            };
            constexpr Colour grove[] = {
                {48, 42, 27}, {50, 104, 55}, {34, 75, 44},
                {106, 73, 40}, {93, 145, 70},
            };
            const std::size_t index = static_cast<std::size_t>(tone);
            const Colour colour =
                cell.landmark == grandleon::view::TacticalLandmark::village_house
                    ? house[index] : grove[index];
            rdpq_set_prim_color(
                RGBA32(colour.red, colour.green, colour.blue, 255)
            );
            rdpq_fill_rectangle(x, y, x + width, y + height);
        }
    );
    // Another terrain cell can follow in the shared depth order. Return the
    // RDP to the atlas mode established by begin_board; the sprite remains in
    // TMEM, so this is state only and performs no upload.
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
}

void draw_castle_tile(
    int left, int top, int size, grandleon::view::CastleTile tile
) {
    if (!ready || tile == grandleon::view::CastleTile::none || size <= 0) {
        return;
    }
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    grandleon::view::paint_castle_tile(
        tile, left, top, size,
        [](int x, int y, int width, int height,
           grandleon::view::LandmarkTone tone) {
            struct Colour final {
                std::uint8_t red;
                std::uint8_t green;
                std::uint8_t blue;
            };
            constexpr Colour colours[] = {
                {42, 40, 38}, {146, 137, 120}, {88, 84, 78},
                {98, 49, 31}, {218, 180, 90},
            };
            const Colour colour = colours[static_cast<std::size_t>(tone)];
            rdpq_set_prim_color(
                RGBA32(colour.red, colour.green, colour.blue, 255)
            );
            rdpq_fill_rectangle(x, y, x + width, y + height);
        }
    );
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
}

void draw_character(
    int drawing, Pose pose, int centre_x, int bottom, int cell_size,
    bool facing_left
) {
    if (!ready || drawing < 0 ||
        static_cast<std::size_t>(drawing) >= grandleon_console_drawing_count) {
        return;
    }
    configure_character_pass();
    const int size = grandleon::view::console_board_unit_size(cell_size);
    draw_loaded_character(
        drawings[drawing].board[pose_index(pose)],
        centre_x, bottom, size, facing_left
    );
}

void end_board() {
    if (ready) rdpq_detach_wait();
}

void draw_character_overlay(
    surface_t* display, int drawing, Pose pose,
    int centre_x, int bottom, int cell_size, bool facing_left
) {
    if (!ready || drawing < 0 ||
        static_cast<std::size_t>(drawing) >= grandleon_console_drawing_count) {
        return;
    }
    rdpq_attach(display, nullptr);
    configure_character_pass();
    const int size = grandleon::view::console_board_unit_size(cell_size);
    draw_loaded_character(
        drawings[drawing].board[pose_index(pose)],
        centre_x, bottom, size, facing_left
    );
    rdpq_detach_wait();
}

void begin_close(surface_t* display, std::uint32_t variation) {
    close_attached = false;
    if ((!ready && !initialize()) ||
        !ensure_backdrop_variant(variation)) {
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
    const int first = static_cast<int>(variation % backdrop_variants) *
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
