// SPDX-License-Identifier: MIT
#pragma once

#include "console_art.h"
#include "grandleon/view/terrain_composition.hpp"

namespace grandleon::playstation::console_renderer {

// Uploads every project's idle drawing into a compact 64x64 atlas.  Other
// poses are streamed into two reserved slots during animation, so the PS1 pays
// ROM space for real motion without exhausting VRAM.
[[nodiscard]] bool initialize();
void draw_character(
    int drawing, art::Pose pose, int centre_x, int bottom, int size,
    bool facing_left = false, int stream_slot = 0
);

// A close exchange reuses the same project-specific drawings as the board.
// The approved pre-rendered terrain plate occupies the otherwise-unused high
// texture pages; the two animated poses stream through a page kept separate
// from it, so attacker and defender never swap sides or corrupt the scenery.
void prepare_close();
void draw_close_background(grandleon::view::TerrainComposition terrain);
void draw_close_character(
    int drawing, art::Pose pose, bool left_side, int stream_slot
);

}  // namespace grandleon::playstation::console_renderer
