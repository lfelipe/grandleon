// SPDX-License-Identifier: MIT
#pragma once

#include <libdragon.h>

#include "grandleon/view/landmark.hpp"

#include <cstddef>
#include <cstdint>

namespace grandleon::nintendo64::console_renderer {

enum class Pose : std::uint8_t {
    idle,
    move,
    windup,
    attack,
    hurt,
};

struct DrawingKey final {
    std::uint8_t style;
    std::uint8_t figure;
    std::uint8_t archetype;
    std::uint8_t colour;
};

// Loads only the drawings selected by the project-specific art compiler and
// the production terrain atlases. A missing required drawing is an error.
[[nodiscard]] bool initialize();
[[nodiscard]] int drawing_index(DrawingKey key) noexcept;
[[nodiscard]] std::uint8_t default_style() noexcept;
[[nodiscard]] std::uint8_t default_figure() noexcept;

// A complete tactical frame.  Terrain is tinted from one material atlas;
// characters are larger, bottom-anchored Godot cutouts so their silhouettes
// stay readable even when a board is fitted below native cell size.
void begin_board(surface_t* display);
void draw_terrain(
    int left, int top, int width, int height,
    std::uint8_t terrain_kind, std::uint8_t variation
);
// Draws one quarter of a 2x2 authored landmark over its mountain base. Flat
// primitives keep this free of extra texture memory and uploads.
void draw_landmark(
    int left, int top, int size, grandleon::view::LandmarkCell cell
);
void draw_character(
    int drawing, Pose pose, int centre_x, int bottom, int cell_size,
    bool facing_left = false
);
void end_board();

// Used by movement and attack animation frames after the settled board has
// been drawn.  It attaches, draws one bottom-anchored cutout, and waits before
// returning, so the CPU overlays may safely continue on the same surface.
void draw_character_overlay(
    surface_t* display, int drawing, Pose pose,
    int centre_x, int bottom, int cell_size, bool facing_left = false
);

// Full-screen combat presentation. Variation is stable for the exchange;
// characters keep faction side positions so a counterattack changes poses,
// never places.
void begin_close(surface_t* display, std::uint32_t variation);
void draw_close_character(
    int drawing, Pose pose, bool left_side, bool facing_left
);
void end_close();

// Dialogue portraits use the same selected drawing as the board.  The caller
// owns no separate console portrait library.
[[nodiscard]] sprite_t* portrait(int drawing) noexcept;
[[nodiscard]] sprite_t* board_sprite(
    int drawing, Pose pose = Pose::idle
) noexcept;

[[nodiscard]] std::size_t embedded_character_bytes() noexcept;

}  // namespace grandleon::nintendo64::console_renderer
