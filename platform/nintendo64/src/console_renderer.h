// SPDX-License-Identifier: MIT
#pragma once

#include <libdragon.h>

#include "grandleon/view/landmark.hpp"
#include "grandleon/view/terrain_composition.hpp"

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

// The production board uses the N64's RDP as a spatial renderer rather than
// stretching the same square-tile picture. Each cell is
// a filtered, lit diamond with real vertical sides; character cutouts remain
// the curated Godot-derived art, grounded by a projected shadow.  The caller
// supplies screen-space geometry so simulation coordinates and camera policy
// stay in the campaign presenter.
void begin_depth_board(surface_t* display);
void draw_depth_terrain(
    int centre_x, int centre_y, int half_width, int half_height, int lift,
    grandleon::view::TerrainComposition terrain
);
void draw_depth_detail(
    int centre_x, int base_y, int half_width, int half_height,
    grandleon::view::TerrainComposition terrain
);
void draw_depth_landmark(
    int centre_x, int base_y, int half_width, int half_height,
    grandleon::view::LandmarkCell cell
);
void draw_depth_castle(
    int centre_x, int base_y, int half_width, int half_height,
    grandleon::view::CastleTile tile
);
void draw_depth_shadow(
    int centre_x, int base_y, int half_width, int half_height
);
void draw_depth_character(
    int drawing, Pose pose, int centre_x, int bottom, int size,
    bool facing_left = false
);
void draw_depth_character_overlay(
    surface_t* display, int drawing, Pose pose, int centre_x, int bottom,
    int size, bool facing_left = false
);
void end_board();

// Used by movement and attack animation frames after the settled board has
// been drawn.  It attaches, draws one bottom-anchored cutout, and waits before
// returning, so the CPU overlays may safely continue on the same surface.
// Full-screen combat presentation. The actual cell selects the approved plate
// variation and adds sparse terrain-specific foreground composition;
// characters keep faction side positions so a counterattack changes poses,
// never places.
void begin_close(
    surface_t* display, grandleon::view::TerrainComposition terrain
);
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
