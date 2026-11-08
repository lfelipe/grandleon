// SPDX-License-Identifier: MIT

#include "console_renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <grandleon/view/motion.hpp>

#include "console_character_assets.h"
#include "console_backdrop_assets.h"
#include "console_terrain_assets.h"
#include "console_twin_gate_runtime.h"

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
sprite_t* twin_gate_full_tiles[console_twin_gate_runtime_state_count]
                              [console_twin_gate_runtime_tile_count]{};
sprite_t* twin_gate_board_tiles[console_twin_gate_runtime_state_count]
                               [console_twin_gate_board_tile_count]{};
bool twin_gate_full_ready[console_twin_gate_runtime_state_count]{};
bool twin_gate_board_ready[console_twin_gate_runtime_state_count]{};
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

[[nodiscard]] bool ensure_twin_gate_state(int state, bool board_pack) {
    if (state < 0 || state >= console_twin_gate_runtime_state_count) {
        return false;
    }
    bool& state_ready = board_pack
        ? twin_gate_board_ready[state] : twin_gate_full_ready[state];
    if (state_ready) return true;
    constexpr const char* names[] = {
        "exterior", "threshold_1", "threshold_2", "threshold_3", "cutaway",
    };
    const int tile_rows = board_pack
        ? console_twin_gate_board_tile_rows
        : console_twin_gate_runtime_tile_rows;
    const int tile_columns = board_pack
        ? console_twin_gate_board_tile_columns
        : console_twin_gate_runtime_tile_columns;
    bool complete = true;
    for (int row = 0; row < tile_rows; ++row) {
        for (int column = 0; column < tile_columns; ++column) {
            const int tile = row * tile_columns + column;
            sprite_t*& loaded = board_pack
                ? twin_gate_board_tiles[state][tile]
                : twin_gate_full_tiles[state][tile];
            if (loaded != nullptr) continue;
            char path[96]{};
            std::snprintf(
                path, sizeof path,
                board_pack
                    ? "rom:/art/twin_gate_board/%s_r%d_c%d.sprite"
                    : "rom:/art/twin_gate/%s_r%d_c%d.sprite",
                names[state], row, column
            );
            loaded = sprite_load(path);
            complete = complete && loaded != nullptr;
        }
    }
    state_ready = complete;
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
    grandleon::view::TerrainComposition terrain
) {
    if (!ready || width <= 0 || height <= 0) return;
    const TerrainLook look = terrain_look(terrain.kind);
    const grandleon::view::TerrainColour colour =
        grandleon::view::terrain_base_colour(terrain.kind);
    const int source_left = (look.material & 1) * material_side;
    const int source_top = ((look.material >> 1) & 1) * material_side;
    // Stable offsets keep a repeated material from reading as wallpaper while
    // staying inside its 32-pixel atlas region.
    constexpr int offsets[][2] = {{0, 0}, {5, 9}, {11, 3}, {17, 13}};
    const int offset_s = offsets[terrain.variation & 3][0];
    const int offset_t = offsets[terrain.variation & 3][1];
    rdpq_set_prim_color(RGBA32(colour.red, colour.green, colour.blue, 255));
    rdpq_texture_rectangle_scaled(
        TILE0, left, top, left + width, top + height,
        source_left + offset_s, source_top + offset_t,
        source_left + material_side, source_top + material_side
    );
}

void begin_terrain_details() {
    if (!ready) return;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
}

void draw_terrain_detail(
    int left, int top, int size,
    grandleon::view::TerrainComposition terrain
) {
    if (!ready || size <= 0) return;
    grandleon::view::paint_console_terrain(
        terrain, left, top, size,
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

void draw_landmark(
    int left, int top, int size, grandleon::view::LandmarkCell cell
) {
    if (!ready || !cell || size <= 0) return;
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
}

void draw_castle_tile(
    int left, int top, int size, grandleon::view::CastleTile tile
) {
    if (!ready || tile == grandleon::view::CastleTile::none || size <= 0) {
        return;
    }
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
}

bool draw_twin_gate_keep(
    int left, int top, int width, int height, int state
) {
    // The authored keep spans 108 pixels in Steel and Cedar's 16x11
    // scrolling view. A small amount of filtered enlargement is cheaper and
    // visually equivalent there; the full pack begins with close framing.
    const bool board_pack = width <= 128 && height <= 128;
    if (!ready || width <= 0 || height <= 0 ||
        !ensure_twin_gate_state(state, board_pack)) {
        return false;
    }
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    const int source_width = board_pack
        ? console_twin_gate_board_width : console_twin_gate_runtime_width;
    const int source_height = board_pack
        ? console_twin_gate_board_height : console_twin_gate_runtime_height;
    const int tile_width = board_pack
        ? console_twin_gate_board_tile_width
        : console_twin_gate_runtime_tile_width;
    const int tile_height = board_pack
        ? console_twin_gate_board_tile_height
        : console_twin_gate_runtime_tile_height;
    const int tile_columns = board_pack
        ? console_twin_gate_board_tile_columns
        : console_twin_gate_runtime_tile_columns;
    const int tile_rows = board_pack
        ? console_twin_gate_board_tile_rows
        : console_twin_gate_runtime_tile_rows;
    const float scale_x = static_cast<float>(width) /
        static_cast<float>(source_width);
    const float scale_y = static_cast<float>(height) /
        static_cast<float>(source_height);
    rdpq_blitparms_t parameters{};
    parameters.scale_x = scale_x;
    parameters.scale_y = scale_y;
    for (int row = 0; row < tile_rows; ++row) {
        for (int column = 0; column < tile_columns; ++column) {
            const int tile = row * tile_columns + column;
            sprite_t* sprite = board_pack
                ? twin_gate_board_tiles[state][tile]
                : twin_gate_full_tiles[state][tile];
            rdpq_sprite_upload(TILE0, sprite, nullptr);
            rdpq_sprite_blit(
                sprite,
                static_cast<float>(left) +
                    static_cast<float>(column * tile_width) * scale_x,
                static_cast<float>(top) +
                    static_cast<float>(row * tile_height) * scale_y,
                &parameters
            );
        }
    }
    return true;
}

bool prepare_twin_gate_keep() {
    bool complete = true;
    for (int state = 0; state < console_twin_gate_runtime_state_count; ++state) {
        complete = ensure_twin_gate_state(state, true) && complete;
    }
    return complete;
}

std::size_t twin_gate_loaded_bytes() noexcept {
    std::size_t total = 0;
    for (int state = 0; state < console_twin_gate_runtime_state_count; ++state) {
        if (twin_gate_board_ready[state]) {
            total += console_twin_gate_board_state_bytes[state];
        }
        if (twin_gate_full_ready[state]) {
            total += console_twin_gate_runtime_state_bytes[state];
        }
    }
    return total;
}

std::size_t twin_gate_pack_bytes() noexcept {
    return console_twin_gate_packaged_bytes;
}

std::size_t twin_gate_board_pack_bytes() noexcept {
    return console_twin_gate_board_total_bytes;
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
