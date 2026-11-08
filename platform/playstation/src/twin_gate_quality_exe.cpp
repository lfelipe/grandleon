// SPDX-License-Identifier: MIT
// PlayStation visual-architecture proof using Steel and Cedar's real Twin Gate
// map and the approved keep plates.
//
// This executable deliberately owns no rules. The generated scene header is
// refused unless the authored map remains 20x14 with the exact connected 6x6
// keep, and this file only projects that immutable presentation fixture. It is
// a small, measurable answer to one question: can the PS1 renderer read like a
// textured tactical RPG instead of a flat tile mock-up?

#include "console_art.h"
#include "psx_gpu.h"
#include "psx_runtime.h"
#include "themes.h"
#include "twin_gate_quality.h"
#include "twin_gate_scene.h"
#include "ui_font.h"

#include <cstddef>
#include <cstdint>

namespace psx = grandleon::playstation;
namespace gpu = grandleon::playstation::gpu;
namespace art = grandleon::playstation::art;
namespace font = grandleon::playstation::ui_font;
namespace gate = grandleon::console_quality::twin_gate;

namespace {

using psx::Line;

constexpr std::uint16_t sky = gpu::rgb15(6, 9, 12);
constexpr std::uint16_t horizon = gpu::rgb15(12, 14, 14);
constexpr std::uint16_t paper = gpu::rgb15(2, 4, 5);
constexpr std::uint16_t panel = gpu::rgb15(4, 7, 8);
constexpr std::uint16_t panel_focus = gpu::rgb15(7, 11, 11);
constexpr std::uint16_t gold = gpu::rgb15(29, 23, 9);
constexpr std::uint16_t ink = gpu::rgb15(31, 31, 30);
constexpr std::uint16_t muted = gpu::rgb15(16, 19, 18);
constexpr std::uint16_t shadow = gpu::rgb15(2, 2, 3);
constexpr std::uint16_t mountain_side = gpu::rgb15(8, 7, 7);
constexpr std::uint16_t mountain_front = gpu::rgb15(5, 5, 6);
constexpr std::uint16_t friendly_ring = gpu::rgb15(8, 24, 21);
constexpr std::uint16_t enemy_ring = gpu::rgb15(27, 10, 8);

constexpr int capture_slot = 14;
constexpr int done_slot = 15;
constexpr int frame_budget_microseconds = 16667;

constexpr int map_origin_x = 120;
constexpr int map_origin_y = 24;
constexpr int tile_half_width = 8;
constexpr int tile_half_height = 5;
constexpr int hud_top = 198;

constexpr int keep_first_page = gpu::first_texture_page + 6;
constexpr int keep_clut_x = 512;
constexpr int keep_clut_y = gpu::clut_y + gpu::clut_rows - 1;

constexpr int quality_kinds[] = {1, 3, 8, 9, 12};
constexpr int quality_kind_count =
    static_cast<int>(sizeof quality_kinds / sizeof quality_kinds[0]);
constexpr int maximum_character_cells = 7;

static_assert(gate::map_width == 20 && gate::map_height == 14,
              "this camera is framed for the authored Twin Gate map");
static_assert(gate::keep_x == 13 && gate::keep_y == 4 && gate::keep_size == 6,
              "the approved keep no longer occupies its authored footprint");
static_assert(twin_gate_quality_width == 192 &&
                  twin_gate_quality_height == 176,
              "the approved keep plate no longer fits this PS1 composition");
static_assert(keep_first_page + 1 <
                  gpu::first_texture_page + gpu::texture_page_count,
              "the streamed keep needs two CI8 texture pages");
static_assert(keep_clut_x + 256 <= gpu::vram_width,
              "the streamed keep CLUT runs out of VRAM");

int checks = 0;
int failures = 0;
std::uint32_t longest_upload = 0;
std::uint32_t longest_draw = 0;

void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) ++failures;
    Line().text("CHECK ").text(message)
        .text(condition ? " PASS" : " FAIL").flush();
}

int next_cell = 0;
int next_clut = 0;
int terrain_cell[art::terrain_kind_count][art::variant_count]{};
int terrain_clut[art::terrain_kind_count]{};
int font_clut = -1;
int font_gold_clut = -1;
int font_muted_clut = -1;
std::uint16_t font_sheet[gpu::font_lines][gpu::font_halfwords_across]{};
std::uint16_t character_cell[maximum_character_cells][art::halfwords_per_cell]{};
int character_drawing[maximum_character_cells]{};
int character_texture_cell[maximum_character_cells]{};
int character_clut[maximum_character_cells]{};
int character_shadow_clut[maximum_character_cells]{};
int character_count = 0;

[[nodiscard]] int claim_clut(const unsigned short* entries) {
    if (next_clut >= gpu::clut_capacity) return -1;
    const int slot = next_clut++;
    gpu::upload(
        gpu::clut_x_of(slot), gpu::clut_y_of(slot), art::clut_size, 1, entries
    );
    return slot;
}

[[nodiscard]] int claim_cell(const unsigned short* texels) {
    if (next_cell >= gpu::cell_capacity) return -1;
    const int slot = next_cell++;
    gpu::upload(
        gpu::cell_texture_x(slot), gpu::cell_texture_y(slot),
        art::halfwords_per_row, art::cell_size, texels
    );
    return slot;
}

void build_font() {
    for (auto& row : font_sheet) {
        for (std::uint16_t& word : row) word = 0;
    }
    for (int slot = 0; slot < font::glyph_count && slot < gpu::font_capacity;
         ++slot) {
        const int base_line = gpu::glyph_texture_v(slot);
        const int base_word = gpu::glyph_texture_u(slot) /
            art::texels_per_halfword;
        for (int row = 0; row < gpu::glyph_texels; ++row) {
            for (int x = 0; x < gpu::glyph_texels; ++x) {
                const std::uint8_t texel = font::texel(slot, x, row);
                if (texel == 0U) continue;
                const int word = base_word + x / art::texels_per_halfword;
                const int shift = 4 * (x % art::texels_per_halfword);
                font_sheet[base_line + row][word] =
                    static_cast<std::uint16_t>(
                        font_sheet[base_line + row][word] |
                        static_cast<std::uint16_t>(texel << shift)
                    );
            }
        }
    }
    gpu::upload(
        gpu::font_x, gpu::font_y, gpu::font_halfwords_across, gpu::font_lines,
        &font_sheet[0][0]
    );
}

[[nodiscard]] int make_font_clut(std::uint16_t face) {
    unsigned short entries[art::clut_size]{};
    entries[1] = shadow;
    entries[2] = face;
    return claim_clut(entries);
}

[[nodiscard]] bool wanted_kind(int kind) {
    for (int candidate : quality_kinds) {
        if (kind == candidate) return true;
    }
    return false;
}

void upload_terrain() {
    for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
        terrain_clut[kind] = -1;
        for (int variant = 0; variant < art::variant_count; ++variant) {
            terrain_cell[kind][variant] = -1;
        }
        if (!wanted_kind(kind)) continue;

        const art::Asset first = art::terrain(0, kind, 0);
        unsigned short tinted[art::clut_size]{};
        const int target_red = grandleon_terrain_flat[0][kind][0] >> 3;
        const int target_green = grandleon_terrain_flat[0][kind][1] >> 3;
        const int target_blue = grandleon_terrain_flat[0][kind][2] >> 3;
        for (int entry = 1; entry < art::clut_size; ++entry) {
            const std::uint16_t source = first.clut[entry];
            const int luminance =
                (gpu::red_of(source) * 2 + gpu::green_of(source) * 3 +
                 gpu::blue_of(source)) / 6;
            tinted[entry] = gpu::rgb15(
                (luminance * 3 + target_red * 5) / 8,
                (luminance * 3 + target_green * 5) / 8,
                (luminance * 3 + target_blue * 5) / 8
            );
        }
        terrain_clut[kind] = claim_clut(tinted);
        for (int variant = 0; variant < art::variant_count; ++variant) {
            terrain_cell[kind][variant] =
                claim_cell(art::terrain(0, kind, variant).texels);
        }
    }
}

void downsample_character(int slot, const art::Asset& source) {
    for (std::uint16_t& word : character_cell[slot]) word = 0;
    for (int y = 0; y < art::cell_size; ++y) {
        for (int x = 0; x < art::cell_size; ++x) {
            const int texel = art::index_at(source, x * 2, y * 2);
            const int word = y * art::halfwords_per_row +
                x / art::texels_per_halfword;
            const int shift = 4 * (x % art::texels_per_halfword);
            character_cell[slot][word] = static_cast<std::uint16_t>(
                character_cell[slot][word] |
                static_cast<std::uint16_t>(texel << shift)
            );
        }
    }
}

[[nodiscard]] int find_character_slot(int drawing) {
    for (int slot = 0; slot < character_count; ++slot) {
        if (character_drawing[slot] == drawing) return slot;
    }
    return -1;
}

void claim_character(int style, int figure, int archetype, int colour) {
    const int drawing = art::character_index(style, figure, archetype, colour);
    expect(drawing >= 0, "the Steel and Cedar unit drawing is in the atlas");
    if (drawing < 0 || find_character_slot(drawing) >= 0) return;
    expect(character_count < maximum_character_cells,
           "the representative unit atlas stays bounded");
    if (character_count >= maximum_character_cells) return;
    const art::Asset source = art::character(drawing);
    const int slot = character_count++;
    character_drawing[slot] = drawing;
    downsample_character(slot, source);
    character_texture_cell[slot] = claim_cell(character_cell[slot]);
    character_clut[slot] = claim_clut(source.clut);
    unsigned short outline[art::clut_size]{};
    for (int entry = 1; entry < art::clut_size; ++entry) {
        outline[entry] = shadow;
    }
    character_shadow_clut[slot] = claim_clut(outline);
}

void upload_characters() {
    for (const gate::Unit& unit : gate::approach_units) {
        claim_character(unit.style, unit.figure, unit.archetype, unit.colour);
    }
    for (const gate::Unit& unit : gate::revealed_units) {
        claim_character(unit.style, unit.figure, unit.archetype, unit.colour);
    }
}

using KeepTiles = const unsigned short (*)
    [twin_gate_quality_tile_width / 2 * twin_gate_quality_tile_height];

struct KeepState final {
    const unsigned short* clut;
    KeepTiles tiles;
    const char* phase;
    const char* objective;
};

[[nodiscard]] KeepState keep_state(int state) {
    switch (state) {
        case 0:
            return {twin_gate_quality_clut_exterior,
                    twin_gate_quality_tiles_exterior,
                    "APPROACH", "OPEN THE KEEP"};
        case 1:
            return {twin_gate_quality_clut_threshold_1,
                    twin_gate_quality_tiles_threshold_1,
                    "AT THE THRESHOLD", "OPEN THE KEEP"};
        case 2:
            return {twin_gate_quality_clut_threshold_2,
                    twin_gate_quality_tiles_threshold_2,
                    "THE GATES PART", "HOLD THE THRESHOLD"};
        case 3:
            return {twin_gate_quality_clut_threshold_3,
                    twin_gate_quality_tiles_threshold_3,
                    "INNER COURT", "ENTER THE KEEP"};
        default:
            return {twin_gate_quality_clut_cutaway,
                    twin_gate_quality_tiles_cutaway,
                    "REVEALED INTERIOR", "DEFEAT MASATORA"};
    }
}

void upload_keep(const KeepState& state) {
    const std::uint32_t opened = psx::clock_ticks();
    gpu::upload(keep_clut_x, keep_clut_y, 256, 1, state.clut);
    for (int index = 0; index < twin_gate_quality_tile_count; ++index) {
        const int column = index % twin_gate_quality_tile_columns;
        const int row = index / twin_gate_quality_tile_columns;
        const int page = keep_first_page + (column >= 2 ? 1 : 0);
        const int u = (column & 1) * twin_gate_quality_tile_width;
        const int v = row * twin_gate_quality_tile_height;
        gpu::upload(
            page * gpu::page_halfwords + u / 2,
            gpu::texture_page_y + v,
            twin_gate_quality_tile_width / 2,
            twin_gate_quality_tile_height,
            state.tiles[index]
        );
    }
    const std::uint32_t microseconds =
        (psx::clock_ticks() - opened) * 1000U /
        psx::ticks_per_1000_microseconds;
    if (microseconds > longest_upload) longest_upload = microseconds;
}

[[nodiscard]] int terrain_variation(int x, int y, int kind) {
    return (x * 5 + y * 3 + kind) & (art::variant_count - 1);
}

[[nodiscard]] int elevation_pixels(int kind) {
    if (kind < 0 || kind >= grandleon_terrain_kind_count) return 0;
    return static_cast<int>(grandleon_terrain_elevation[kind]) * 5;
}

[[nodiscard]] int map_centre_x(int x, int y) {
    return map_origin_x + (x - y) * tile_half_width;
}

[[nodiscard]] int map_centre_y(int x, int y, int kind) {
    return map_origin_y + (x + y) * tile_half_height -
        elevation_pixels(kind);
}

void draw_terrain_cell(int x, int y, int kind) {
    const int centre_x = map_centre_x(x, y);
    const int centre_y = map_centre_y(x, y, kind);
    const int elevation = elevation_pixels(kind);
    if (elevation > 0) {
        gpu::draw_triangle(
            centre_x, centre_y + tile_half_height * 2,
            centre_x + tile_half_width, centre_y + tile_half_height,
            centre_x + tile_half_width,
            centre_y + tile_half_height + elevation, mountain_side
        );
        gpu::draw_triangle(
            centre_x, centre_y + tile_half_height * 2,
            centre_x + tile_half_width,
            centre_y + tile_half_height + elevation,
            centre_x, centre_y + tile_half_height * 2 + elevation,
            mountain_side
        );
        gpu::draw_triangle(
            centre_x - tile_half_width, centre_y + tile_half_height,
            centre_x, centre_y + tile_half_height * 2,
            centre_x, centre_y + tile_half_height * 2 + elevation,
            mountain_front
        );
        gpu::draw_triangle(
            centre_x - tile_half_width, centre_y + tile_half_height,
            centre_x, centre_y + tile_half_height * 2 + elevation,
            centre_x - tile_half_width,
            centre_y + tile_half_height + elevation, mountain_front
        );
    }

    const int variant = terrain_variation(x, y, kind);
    const int cell = terrain_cell[kind][variant];
    const int clut = terrain_clut[kind];
    if (cell < 0 || clut < 0) return;
    gpu::draw_cell_triangle(
        centre_x, centre_y, 16, 0,
        centre_x + tile_half_width, centre_y + tile_half_height, 31, 16,
        centre_x, centre_y + tile_half_height * 2, 16, 31,
        cell, clut
    );
    gpu::draw_cell_triangle(
        centre_x, centre_y, 16, 0,
        centre_x, centre_y + tile_half_height * 2, 16, 31,
        centre_x - tile_half_width, centre_y + tile_half_height, 0, 16,
        cell, clut
    );
}

void draw_map() {
    for (int diagonal = 0;
         diagonal < gate::map_width + gate::map_height - 1; ++diagonal) {
        for (int y = 0; y < gate::map_height; ++y) {
            const int x = diagonal - y;
            if (x < 0 || x >= gate::map_width) continue;
            const int index = y * gate::map_width + x;
            draw_terrain_cell(x, y, gate::terrain[index]);
        }
    }
}

struct KeepPlacement final {
    int left;
    int top;
    int tile_width;
    int tile_height;
};

[[nodiscard]] KeepPlacement keep_placement(int state) {
    // The shared 192x176 plate is padded to six 32-pixel tile rows so every
    // upload is exactly one PS1-friendly 2 KiB block. Scale the complete
    // 192-pixel texture here, including its transparent final 16 rows, rather
    // than stretching the visible art into the HUD.
    if (state == 0) return {120, 46, 48, 24};
    if (state == 1) return {108, 28, 56, 28};
    return {96, 8, twin_gate_quality_tile_width,
            twin_gate_quality_tile_height};
}

void draw_keep(int state) {
    const KeepPlacement placement = keep_placement(state);
    for (int index = 0; index < twin_gate_quality_tile_count; ++index) {
        const int column = index % twin_gate_quality_tile_columns;
        const int row = index / twin_gate_quality_tile_columns;
        const int page = keep_first_page + (column >= 2 ? 1 : 0);
        const int u = (column & 1) * twin_gate_quality_tile_width;
        const int v = row * twin_gate_quality_tile_height;
        gpu::draw_texture_quad_ci8(
            placement.left + column * placement.tile_width,
            placement.top + row * placement.tile_height,
            placement.tile_width, placement.tile_height,
            page, u, v,
            u + twin_gate_quality_tile_width - 1,
            v + twin_gate_quality_tile_height - 1,
            keep_clut_x, keep_clut_y
        );
    }
}

void draw_unit(const gate::Unit& unit, bool revealed) {
    const int drawing = art::character_index(
        unit.style, unit.figure, unit.archetype, unit.colour
    );
    const int slot = find_character_slot(drawing);
    if (slot < 0) return;
    const int kind = gate::terrain[unit.y * gate::map_width + unit.x];
    const int centre_x = revealed
        ? 184 + (unit.x - 15) * 12 - (unit.y - 7) * 12
        : map_centre_x(unit.x, unit.y);
    const int base_y = revealed
        ? 125 + (unit.x - 15 + unit.y - 7) * 7
        : map_centre_y(unit.x, unit.y, kind) + tile_half_height * 2;
    const int size = revealed ? 36 : 32;
    const std::uint16_t ring = unit.colour == 0 ? friendly_ring : enemy_ring;
    gpu::draw_triangle(
        centre_x - 7, base_y - 2, centre_x + 7, base_y - 2,
        centre_x, base_y + 3, shadow
    );
    gpu::fill(centre_x - 6, base_y - 2, 12, 2, ring);
    // A one-pixel hard silhouette is a cheap PS1-era substitute for alpha
    // outlining: the same transparent texels, through a dark CLUT, four times
    // behind the real drawing. It keeps a detailed cutout readable over stone
    // and costs no additional texture residency.
    gpu::draw_cell_scaled(
        centre_x - size / 2 - 1, base_y - size,
        size, character_texture_cell[slot], character_shadow_clut[slot]
    );
    gpu::draw_cell_scaled(
        centre_x - size / 2 + 1, base_y - size,
        size, character_texture_cell[slot], character_shadow_clut[slot]
    );
    gpu::draw_cell_scaled(
        centre_x - size / 2, base_y - size - 1,
        size, character_texture_cell[slot], character_shadow_clut[slot]
    );
    gpu::draw_cell_scaled(
        centre_x - size / 2, base_y - size + 1,
        size, character_texture_cell[slot], character_shadow_clut[slot]
    );
    gpu::draw_cell_scaled(
        centre_x - size / 2, base_y - size, size,
        character_texture_cell[slot], character_clut[slot]
    );
}

void draw_units(bool revealed) {
    if (revealed) {
        for (const gate::Unit& unit : gate::revealed_units) {
            draw_unit(unit, true);
        }
    } else {
        for (const gate::Unit& unit : gate::approach_units) {
            draw_unit(unit, false);
        }
    }
}

void draw_panel(int x, int y, int width, int height, bool focused = false) {
    gpu::fill(x + 2, y + 2, width, height, shadow);
    gpu::fill(x, y, width, height, gold);
    gpu::fill(x + 1, y + 1, width - 2, height - 2,
              focused ? panel_focus : panel);
    gpu::fill(x + 1, y + 1, width - 2, 2, gold);
}

void write_text(int x, int y, const char* text, int clut, int size = 8) {
    if (text == nullptr || size <= 0) return;
    int used = 0;
    for (std::size_t at = 0U; text[at] != '\0';) {
        const font::Decoded decoded = font::decode(text + at);
        if (decoded.bytes == 0U) break;
        const int advance =
            (font::advance(decoded.codepoint) * size + 7) / 8;
        if (decoded.codepoint != ' ') {
            gpu::draw_glyph_scaled(
                x + used, y, size, font::slot_for(decoded.codepoint), clut
            );
        }
        used += advance;
        if (x + used >= gpu::screen_width) break;
        at += decoded.bytes;
    }
}

void draw_hud(const KeepState& state, int index) {
    gpu::fill(0, hud_top, gpu::screen_width,
              gpu::screen_height - hud_top, paper);
    draw_panel(6, hud_top + 4, 154, 34, true);
    draw_panel(164, hud_top + 4, 150, 34);
    write_text(13, hud_top + 8, "ROWAN", font_gold_clut, 10);
    write_text(13, hud_top + 21, "LV 7   HP 24/24", font_clut);
    gpu::fill(98, hud_top + 23, 50, 5, shadow);
    gpu::fill(99, hud_top + 24, 48, 3, friendly_ring);
    write_text(171, hud_top + 8, "OBJECTIVE", font_gold_clut);
    write_text(171, hud_top + 20, state.objective, font_clut);

    draw_panel(8, 8, 154, 30);
    write_text(15, 12, "V  TWIN GATE KEEP", font_gold_clut, 10);
    write_text(15, 26, state.phase, font_muted_clut);
    gpu::fill(168, 9, 9, 4, index >= 1 ? gold : muted);
    gpu::fill(179, 9, 9, 4, index >= 2 ? gold : muted);
    gpu::fill(190, 9, 9, 4, index >= 3 ? gold : muted);
    gpu::fill(201, 9, 9, 4, index >= 4 ? gold : muted);
}

void draw_frame(const KeepState& state, int index) {
    const std::uint32_t opened = psx::clock_ticks();
    gpu::clear(sky);
    gpu::fill(0, 74, gpu::screen_width, hud_top - 74, horizon);
    // Sparse one-pixel bands keep the sky unmistakably 15-bit and PS1-era,
    // without turning the tactical cells into visual noise.
    for (int y = 82; y < hud_top; y += 8) {
        gpu::fill(0, y, gpu::screen_width, 1,
                  (y & 8) != 0 ? gpu::rgb15(10, 12, 12)
                               : gpu::rgb15(11, 13, 13));
    }
    draw_map();
    if (index != twin_gate_quality_state_count - 1) {
        // The outer guard is behind the keep's wall plane at this camera
        // angle; draw-world order, rather than a hand offset, makes that
        // occlusion read correctly while Rowan's approach remains visible.
        draw_units(false);
    }
    draw_keep(index);
    if (index == twin_gate_quality_state_count - 1) draw_units(true);
    draw_hud(state, index);
    const std::uint32_t microseconds =
        (psx::clock_ticks() - opened) * 1000U /
        psx::ticks_per_1000_microseconds;
    if (microseconds > longest_draw) longest_draw = microseconds;
}

}  // namespace

int main() {
    Line().text("GRANDLEON PLAYSTATION TWIN GATE QUALITY").flush();
    psx::start_clock();
    gpu::begin(sky);

    for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
        terrain_clut[kind] = -1;
        for (int variant = 0; variant < art::variant_count; ++variant) {
            terrain_cell[kind][variant] = -1;
        }
    }
    for (int slot = 0; slot < maximum_character_cells; ++slot) {
        character_drawing[slot] = -1;
        character_texture_cell[slot] = -1;
        character_clut[slot] = -1;
        character_shadow_clut[slot] = -1;
    }

    upload_terrain();
    upload_characters();
    build_font();
    font_clut = make_font_clut(ink);
    font_gold_clut = make_font_clut(gold);
    font_muted_clut = make_font_clut(muted);

    expect(next_cell <= gpu::cells_per_page,
           "terrain and representative units share one CI4 page");
    expect(next_clut < gpu::clut_capacity,
           "terrain, units and interface CLUTs fit the shared band");
    expect(font_clut >= 0 && font_gold_clut >= 0 && font_muted_clut >= 0,
           "the hierarchy font palettes are resident");

    gpu::show();
    for (int index = 0; index < twin_gate_quality_state_count; ++index) {
        const KeepState state = keep_state(index);
        upload_keep(state);
        draw_frame(state, index);
        psx::wait_vblank();
        psx::signal_host(capture_slot);
        // A human capture sees the reveal as a deliberate beat instead of five
        // frames flickering past. Upload and drawing are outside this dwell.
        for (int held = 0; held < 10; ++held) psx::wait_vblank();
    }

    expect(longest_upload < static_cast<std::uint32_t>(frame_budget_microseconds),
           "one streamed keep state fits a 60 Hz frame");
    expect(longest_draw < static_cast<std::uint32_t>(frame_budget_microseconds),
           "one complete quality frame fits a 60 Hz frame");

    const std::uint32_t keep_bytes =
        static_cast<std::uint32_t>(twin_gate_quality_width *
                                   twin_gate_quality_height + 512);
    const std::uint32_t cell_bytes =
        static_cast<std::uint32_t>(next_cell * art::halfwords_per_cell * 2);
    const std::uint32_t clut_bytes =
        static_cast<std::uint32_t>(next_clut * art::clut_size * 2);
    const std::uint32_t font_bytes = static_cast<std::uint32_t>(
        gpu::font_halfwords_across * gpu::font_lines * 2
    );
    Line().text("VRAM keep ").decimal(keep_bytes)
        .text(" cells ").decimal(cell_bytes)
        .text(" cluts ").decimal(clut_bytes)
        .text(" font ").decimal(font_bytes)
        .text(" total ").decimal(keep_bytes + cell_bytes + clut_bytes + font_bytes)
        .flush();
    Line().text("FRAME upload-us ").decimal(longest_upload)
        .text(" draw-us ").decimal(longest_draw)
        .text(" budget ").decimal(frame_budget_microseconds).flush();

    const psx::HeapCensus heap = psx::heap_census();
    Line().text("HEAP capacity ").decimal(psx::heap_capacity())
        .text(" free ").decimal(heap.free_bytes)
        .text(" peak ").decimal(heap.peak_allocated_bytes).flush();
    Line().text("RESULT ").text(failures == 0 ? "PASS " : "FAIL ")
        .decimal(static_cast<std::uint32_t>(checks - failures)).text("/")
        .decimal(static_cast<std::uint32_t>(checks)).flush();
    psx::signal_host(done_slot);
    return failures == 0 ? 0 : 1;
}
