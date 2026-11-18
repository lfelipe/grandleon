// SPDX-License-Identifier: MIT
#include "runtime/scene.h"
#include "runtime/textures.h"
#include "runtime/render.h"
#include "generated/grandleon_scene.h"
#include "game_bridge.h"
#include "text_paging.h"
#include <grandleon/view/brand_logo.h>
#include <grandleon/view/console_text.h>
#include <grandleon/view/ps1_font.h>

#include <psxgpu.h>

/* The renderer's only font call is an overflow diagnostic.  The compact UI
 * is textured, so keep that optional call from pulling printf/font support. */
int FntPrint(int stream, const char* format, ...) {
    (void)stream;
    (void)format;
    return 0;
}

const ps1_entity* ps1_scene_entity(unsigned int index) {
    return index < g_ps1_scene.entity_count ? &g_ps1_scene.entities[index] : 0;
}

const ps1_entity* ps1_scene_source_entity(unsigned int index) {
    return ps1_scene_entity(index);
}

int ps1_scene_camera_index(void) {
    return g_ps1_scene.camera_entity_index;
}

static void draw_box_depth(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, int width, int height, uint8_t red, uint8_t green, uint8_t blue,
    int depth
) {
    TILE* tile;
    if ((const char*)(*next_packet + sizeof(TILE)) > packet_end) return;
    tile = (TILE*)*next_packet;
    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, width, height);
    setRGB0(tile, red, green, blue);
    addPrim(ordering_table + depth, tile);
    *next_packet = (char*)(tile + 1);
}

static void draw_box(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, int width, int height, uint8_t red, uint8_t green, uint8_t blue
) {
    draw_box_depth(
        ordering_table, next_packet, packet_end,
        x, y, width, height, red, green, blue, 2
    );
}

static void draw_panel(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, int width, int height
) {
    draw_box_depth(
        ordering_table, next_packet, packet_end,
        x, y, width, height, 14, 23, 38, 3
    );
    draw_box(ordering_table, next_packet, packet_end, x, y, width, 2, 181, 149, 86);
    draw_box(ordering_table, next_packet, packet_end, x, y + height - 1, width, 1, 113, 94, 62);
    draw_box(ordering_table, next_packet, packet_end, x, y, 1, height, 113, 94, 62);
    draw_box(ordering_table, next_packet, packet_end, x + width - 1, y, 1, height, 113, 94, 62);
    draw_box(ordering_table, next_packet, packet_end, x, y, 4, 4, 226, 190, 111);
    draw_box(ordering_table, next_packet, packet_end, x + width - 4, y + height - 4, 4, 4, 181, 149, 86);
}

/* UI fills live below all font sprites. The narrow inset and corner caps make
 * the focus legible without relying on team colour alone. */
static void draw_focus(uint32_t* ot, char** next, const char* end,
                       int x, int y, int width, int height) {
    draw_box(ot, next, end, x, y, 3, height, 226, 190, 111);
    draw_box(ot, next, end, x + 3, y, width - 3, height, 44, 58, 73);
}

static void draw_glyph(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, char character, uint8_t red, uint8_t green, uint8_t blue
) {
    const unsigned int texture = g_grandleon_ui_texture_index;
    const uint8_t base_u = ps1_texture_u_offset(texture);
    const uint8_t base_v = ps1_texture_v_offset(texture);
    unsigned int slot;
    uint8_t u;
    uint8_t v;
    SPRT* glyph;
    slot = grandleon_ps1_glyph_slot((unsigned char)character);
    u = (uint8_t)(base_u + (slot % 8u) * 8u);
    v = (uint8_t)(base_v + (slot / 8u) * 12u);
    if ((const char*)(*next_packet + sizeof(SPRT)) > packet_end) return;
    glyph = (SPRT*)*next_packet;
    setSprt(glyph);
    setXY0(glyph, x, y);
    setWH(glyph, 8, 12);
    setUV0(glyph, u, v);
    setRGB0(glyph, red, green, blue);
    setShadeTex(glyph, 0);
    glyph->clut = ps1_texture_clut(texture);
    addPrim(ordering_table + 1, glyph);
    *next_packet = (char*)(glyph + 1);
}

static void activate_ui_tpage(
    uint32_t* ordering_table, char** next_packet, const char* packet_end
) {
    DR_TPAGE* state;
    if ((const char*)(*next_packet + sizeof(DR_TPAGE)) > packet_end) return;
    state = (DR_TPAGE*)*next_packet;
    setDrawTPage(state, 0, 0, ps1_texture_tpage(g_grandleon_ui_texture_index));
    addPrim(ordering_table + 1, state);
    *next_packet = (char*)(state + 1);
}

static void draw_text(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, const char* text, uint8_t red, uint8_t green, uint8_t blue
) {
    while (*text) {
        draw_glyph(ordering_table, next_packet, packet_end, x, y, grandleon_text_next(&text), red, green, blue);
        x += 7;
    }
}

static void draw_button(uint32_t* ot, char** next, const char* end,
                        int x, int y, char button, unsigned label) {
    draw_box(ot, next, end, x, y, 12, 13, 65, 79, 94);
    draw_glyph(ot, next, end, x + 2, y, button, 247, 239, 209);
    draw_text(ot, next, end, x + 17, y, grandleon_game_text(label), 205, 211, 214);
}

/* Wrapped rows already contain decoded Latin-1 console glyphs. Sending these
 * bytes through the UTF-8 decoder again would turn ç/ã/ñ into question marks. */
static void draw_console_glyphs(
    uint32_t* ot, char** next, const char* end, int x, int y,
    const char* text, uint8_t red, uint8_t green, uint8_t blue
) {
    while (*text) {
        draw_glyph(ot, next, end, x, y, *text++, red, green, blue);
        x += 7;
    }
}

static void draw_wrapped_text_box(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, const char* text, uint8_t red, uint8_t green, uint8_t blue,
    int columns, int rows
) {
    char line[35];
    int row = 0;
    while (*text && row < rows) {
        grandleon_text_wrap_line(&text, line, (unsigned)columns);
        draw_console_glyphs(ordering_table, next_packet, packet_end,
                  x, y + row * 13, line, red, green, blue);
        ++row;
    }
}

static void draw_wrapped_text(
    uint32_t* ot, char** next, const char* end, int x, int y,
    const char* text, uint8_t r, uint8_t g, uint8_t b
) {
    draw_wrapped_text_box(ot, next, end, x, y, text, r, g, b, 34, 3);
}

static void draw_logo(uint32_t* ot, char** next, const char* end, int x, int y, int scale) {
    for (unsigned index = 0; index < GRANDLEON_LOGO_RECT_COUNT; ++index) {
        const grandleon_logo_rect* run = &grandleon_logo_rects[index];
        draw_box_depth(ot, next, end, x + run->x * scale, y + run->y * scale,
            run->width * scale, run->height * scale, run->r, run->g, run->b, 1);
    }
}

static void draw_number(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, int value, uint8_t red, uint8_t green, uint8_t blue
) {
    char digits[7];
    unsigned int count = 0;
    unsigned int number;
    if (value < 0) {
        draw_glyph(ordering_table, next_packet, packet_end, x, y, '-', red, green, blue);
        x += 7;
        number = (unsigned int)(-value);
    } else {
        number = (unsigned int)value;
    }
    do {
        digits[count++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number && count < sizeof(digits));
    while (count) {
        draw_glyph(ordering_table, next_packet, packet_end, x, y, digits[--count], red, green, blue);
        x += 7;
    }
}

static void draw_portrait(uint32_t* ot, char** next, const char* end,
                          const grandleon_ui_state* state, int x, int y) {
    const unsigned texture = state->portrait_texture;
    if (!texture || (const char*)(*next + sizeof(POLY_FT4)) > end) return;
    ps1_texture_ensure_resident(texture);
    POLY_FT4* portrait = (POLY_FT4*)*next;
    const int u = ps1_texture_u_offset(texture);
    const int v = ps1_texture_v_offset(texture);
    const int w = ps1_texture_width(texture) - 1;
    const int h = ps1_texture_height(texture) - 1;
    const int lift = state->talking_phase;
    setPolyFT4(portrait);
    setXY4(portrait, x, y - lift, x + 48, y - lift, x, y + 64, x + 48, y + 64);
    setUV4(portrait, u, v, u + w, v, u, v + h, u + w, v + h);
    setRGB0(portrait, 128, 128, 128);
    portrait->clut = ps1_texture_clut(texture);
    portrait->tpage = ps1_texture_tpage(texture);
    // Draw before the font tpage at depth one. A textured portrait changes
    // GPU page state and must not split earlier/later text in the same list.
    addPrim(ot + 2, portrait);
    *next = (char*)(portrait + 1);
}

static void panel_position(int width, int height, int* x, int* y) {
    int best = 0x7fffffff;
    for (int candidate = 0; candidate < 4; ++candidate) {
        const int px = candidate & 1 ? 314 - width : 6;
        const int py = candidate & 2 ? 232 - height : 8;
        int score = 0;
        for (unsigned focus = 0; focus < 2; ++focus) {
            int32_t wx, wy, wz, depth;
            int16_t sx, sy;
            if (!grandleon_game_focus_point(focus, &wx, &wy, &wz) ||
                !ps1_render_project_world(wx, wy, wz, &sx, &sy, &depth)) continue;
            // Reserve the full unit silhouette, including weapons and the cursor.
            if (sx + 24 >= px && sx - 24 <= px + width &&
                sy + 32 >= py && sy - 48 <= py + height) score += 100;
            if (sx >= px && sx <= px + width) score += 2;
        }
        if (score < best) { best = score; *x = px; *y = py; }
    }
}

static void draw_movement_range(uint32_t* ot, char** next, const char* end,
                                const grandleon_ui_state* state) {
    for (unsigned tile = 0; tile < state->movement_tile_count; ++tile) {
        int32_t wx, wy, wz, depth;
        int16_t sx[4], sy[4];
        static const int offsets[4][2] = {{-1,-1},{1,-1},{-1,1},{1,1}};
        int visible = 1;
        if (!grandleon_game_movement_tile((uint16_t)tile, &wx, &wy, &wz)) continue;
        for (unsigned corner = 0; corner < 4; ++corner) {
            if (!ps1_render_project_world(wx + offsets[corner][0] * 65536,
                wy, wz + offsets[corner][1] * 65536, &sx[corner], &sy[corner], &depth)) visible = 0;
        }
        if (!visible) continue;
        // Thin outlines preserve the terrain and never cover unit artwork.
        const unsigned edges = grandleon_game_movement_edges((uint16_t)tile);
        for (unsigned edge = 0; edge < 4; ++edge) {
            if (!(edges & (1U << edge))) continue;
            static const unsigned order[4] = {0,1,3,2};
            const unsigned a = order[edge], b = order[(edge + 1) & 3];
            if ((const char*)(*next + sizeof(LINE_F2)) > end) return;
            LINE_F2* line = (LINE_F2*)*next;
            setLineF2(line);
            setXY2(line, sx[a], sy[a], sx[b], sy[b]);
            if (state->local_player == 2) setRGB0(line, 72, 220, 255);
            else setRGB0(line, 255, 225, 48);
            addPrim(ot + 3, line);
            *next = (char*)(line + 1);
        }
    }
}

/* Grounded corner brackets identify the actor and legal target even when their
 * team colours are difficult to distinguish. They use the live board camera. */
static void draw_unit_brackets(uint32_t* ot, char** next, const char* end,
                               unsigned focus, unsigned player, int target) {
    int32_t x, y, z, depth;
    int16_t sx[4], sy[4];
    static const int dx[] = {-1, 1, 1, -1};
    static const int dz[] = {-1, -1, 1, 1};
    if (!grandleon_game_focus_point(focus, &x, &y, &z)) return;
    y -= 61440;
    for (unsigned c = 0; c < 4; ++c)
        if (!ps1_render_project_world(x + dx[c] * 57344, y, z + dz[c] * 57344,
                                      &sx[c], &sy[c], &depth)) return;
    for (unsigned c = 0; c < 4; ++c) {
        for (unsigned side = 0; side < 2; ++side) {
            unsigned neighbour = (c + (side ? 3 : 1)) % 4;
            if ((const char*)(*next + sizeof(LINE_F2)) > end) return;
            LINE_F2* line = (LINE_F2*)*next;
            setLineF2(line);
            setXY2(line, sx[c], sy[c], sx[c] + (sx[neighbour] - sx[c]) / 3,
                       sy[c] + (sy[neighbour] - sy[c]) / 3);
            if (target) setRGB0(line, 244, 137, 109);
            else if (player == 2) setRGB0(line, 72, 220, 255);
            else setRGB0(line, 255, 225, 48);
            addPrim(ot + 4, line);
            *next = (char*)(line + 1);
        }
    }
}

static void draw_settings_rows(uint32_t* ot, char** next, const char* end,
                               const grandleon_ui_state* state, int x, int y) {
    const unsigned labels[] = {GRANDLEON_TEXT_CLOSE_VIEW, GRANDLEON_TEXT_SHOW_FPS,
        GRANDLEON_TEXT_MUSIC, GRANDLEON_TEXT_EFFECTS, GRANDLEON_TEXT_LANGUAGE};
    for (unsigned row = 0; row < state->settings_menu_count && row < 5; ++row) {
        const int line = y + (row < 2 ? (int)row * 21 : row == 2 ? 42 : row == 3 ? 77 : 112);
        const int audio = row == 2 || row == 3;
        if (row == state->settings_menu_index) {
            draw_focus(ot, next, end, x - 12, line - 3, 205, audio ? 32 : 19);
            draw_glyph(ot, next, end, x - 8, line, '>', 226, 190, 111);
        }
        draw_text(ot, next, end, x, line, grandleon_game_text(labels[row]), 247, 239, 209);
        if (row == 2 || row == 3) {
            const unsigned value = row == 2 ? state->music_volume : state->effects_volume;
            draw_box_depth(ot, next, end, x + 5 + (int)(value * 120 / 100), line + 15, 3, 10, 247, 239, 209, 1);
            if (value) draw_box_depth(ot, next, end, x + 5, line + 18, (int)(value * 120 / 100), 4, 226, 190, 111, 1);
            draw_box_depth(ot, next, end, x + 5, line + 18, 123, 4, 31, 46, 57, 1);
            draw_number(ot, next, end, x + 147, line + 12, value, 226, 190, 111);
            draw_text(ot, next, end, x + 169, line + 12, "%", 226, 190, 111);
        } else {
            const int enabled = row == 0 ? state->close_combat : state->show_fps;
            draw_text(ot, next, end, x + 143, line, row == 4 ? state->language_code
                : grandleon_game_text(enabled ? GRANDLEON_TEXT_ON : GRANDLEON_TEXT_OFF), 94, 206, 227);
        }
    }
}

static void draw_menu_row(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, const char* label, int selected, int suffix
) {
    if (selected) {
        draw_focus(ordering_table, next_packet, packet_end, x, y - 2, 124, 16);
        draw_glyph(ordering_table, next_packet, packet_end, x + 2, y, '>', 255, 205, 68);
    }
    draw_text(ordering_table, next_packet, packet_end, x + 11, y, label,
              selected ? 255 : 210, selected ? 239 : 218, selected ? 162 : 228);
    if (suffix >= 0) {
        draw_number(ordering_table, next_packet, packet_end, x + 113, y, suffix,
                    94, 206, 227);
    }
}

static void draw_ground_equipment(uint32_t* ot, char** next, const char* end,
                                  const grandleon_ui_state* state) {
    if (state->combat_view_active || state->mode == GRANDLEON_UI_STAGE_OVERVIEW) return;
    for (unsigned index = 0; index < grandleon_game_ground_count(); ++index) {
        int32_t x, y, z, depth;
        int16_t sx, sy;
        if (!grandleon_game_ground_tile((uint16_t)index, &x, &y, &z) ||
            !ps1_render_project_world(x, y, z, &sx, &sy, &depth) ||
            sx < 0 || sx > 320 || sy < 0 || sy > 240) continue;
        /* A small closed chest marks a tile, never an unseen room. */
        draw_box(ot, next, end, sx - 5, sy - 7, 11, 8, 56, 37, 20);
        draw_box(ot, next, end, sx - 4, sy - 6, 9, 3, 222, 179, 79);
        draw_box(ot, next, end, sx - 1, sy - 3, 3, 3, 255, 221, 115);
    }
    for (unsigned index = 0; index < grandleon_game_glint_count(); ++index) {
        int32_t x, y, z, depth;
        int16_t sx, sy;
        if (!grandleon_game_glint_tile((uint16_t)index, &x, &y, &z) ||
            !ps1_render_project_world(x, y, z, &sx, &sy, &depth) ||
            sx < 5 || sx > 315 || sy < 5 || sy > 235) continue;
        draw_box(ot, next, end, sx - 3, sy, 7, 1, 242, 207, 119);
        draw_box(ot, next, end, sx, sy - 3, 1, 7, 255, 235, 179);
    }
}

static void draw_discovery_notice(uint32_t* ot, char** next, const char* end, int x) {
    const char* notice = grandleon_game_discovery_notice();
    if (!*notice) return;
    const char* pack_full = grandleon_game_discovery_pack_full();
    draw_panel(ot, next, end, x, 8, 168, *pack_full ? 49 : 34);
    draw_text(ot, next, end, x + 8, 13, grandleon_game_discovery_heading(), 94, 206, 227);
    draw_text(ot, next, end, x + 8, 27, notice, 255, 222, 139);
    if (*pack_full) draw_text(ot, next, end, x + 8, 41, pack_full, 230, 215, 185);
}

void ps1_ui_render(uint32_t* ordering_table, char** next_packet, const char* packet_end) {
    grandleon_ui_state state;
    int x;
    grandleon_world_packet_cache_capture(ordering_table, next_packet);
    grandleon_game_ui_state(&state);
    draw_movement_range(ordering_table, next_packet, packet_end, &state);
    draw_ground_equipment(ordering_table, next_packet, packet_end, &state);
    if (!state.combat_view_active && (state.mode == GRANDLEON_UI_BROWSE ||
        state.mode == GRANDLEON_UI_MOVE))
        draw_discovery_notice(ordering_table, next_packet, packet_end, 6);
    if (state.mode == GRANDLEON_UI_MOVE || state.mode == GRANDLEON_UI_ACTION ||
        state.mode == GRANDLEON_UI_ATTACK || state.mode == GRANDLEON_UI_ABILITY) {
        draw_unit_brackets(ordering_table, next_packet, packet_end, 0, state.local_player, 0);
        if (state.forecast_valid && state.camera_settled &&
            (state.mode == GRANDLEON_UI_ATTACK || state.mode == GRANDLEON_UI_ABILITY))
            draw_unit_brackets(ordering_table, next_packet, packet_end, 1, state.local_player, 1);
    }

    if ((state.combat_view_active || state.mode == GRANDLEON_UI_TITLE || state.mode == GRANDLEON_UI_SHOP) && g_grandleon_combat_plate_indices[0]) {
        unsigned int strip;
        for (strip = 0; strip < 4; ++strip) {
            const unsigned int texture = g_grandleon_combat_plate_indices[strip];
            POLY_FT4* plate;
            int u = ps1_texture_u_offset(texture);
            int v = ps1_texture_v_offset(texture);
            if ((const char*)(*next_packet + sizeof(POLY_FT4)) > packet_end) break;
            plate = (POLY_FT4*)*next_packet;
            setPolyFT4(plate);
            setXY4(plate, strip * 80, 0, (strip + 1) * 80, 0,
                   strip * 80, 240, (strip + 1) * 80, 240);
            setUV4(plate, u, v, u + 79, v, u, v + 239, u + 79, v + 239);
            setRGB0(plate, 128, 128, 128);
            setShadeTex(plate, 1);
            plate->tpage = ps1_texture_tpage(texture);
            plate->clut = ps1_texture_clut(texture);
            addPrim(ordering_table + PS1_RENDER_OT_LEN - 1, plate);
            *next_packet = (char*)(plate + 1);
        }
    }

    if (state.show_fps) {
        draw_box_depth(ordering_table, next_packet, packet_end,
                       267, 3, 50, 14, 10, 17, 28, 3);
        draw_text(ordering_table, next_packet, packet_end, 271, 5,
                  "FPS", 168, 181, 196);
        draw_number(ordering_table, next_packet, packet_end, 295, 5,
                    state.measured_fps, 247, 239, 209);
    }

    if (state.mode == GRANDLEON_UI_STAGE_OVERVIEW) {
        draw_panel(ordering_table, next_packet, packet_end, 210, 210, 104, 25);
        draw_button(ordering_table, next_packet, packet_end, 218, 215, 'X', GRANDLEON_TEXT_BEGIN);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode >= GRANDLEON_UI_TITLE && state.mode <= GRANDLEON_UI_NAME) {
        if (state.mode != GRANDLEON_UI_TITLE || !g_grandleon_combat_plate_indices[0])
            draw_box_depth(ordering_table, next_packet, packet_end,
                           0, 0, 320, 240, 7, 13, 24, 4);
        if (state.mode == GRANDLEON_UI_TITLE) {
            draw_panel(ordering_table, next_packet, packet_end, 10, 25, 134, 165);
            draw_logo(ordering_table, next_packet, packet_end, 12, 32, 2);
            draw_panel(ordering_table, next_packet, packet_end, 154, 25, 156, 43);
            draw_wrapped_text_box(ordering_table, next_packet, packet_end, 164, 33,
                state.headline ? state.headline : "STEEL AND CEDAR", 226, 190, 111, 19, 2);
            draw_panel(ordering_table, next_packet, packet_end, 154, 78, 156, 112);
            draw_focus(ordering_table, next_packet, packet_end, 161,
                86 + state.campaign_menu_index * 24, 142, 21);
            draw_glyph(ordering_table, next_packet, packet_end, 167,
                91 + state.campaign_menu_index * 24, '>', 226, 190, 111);
            unsigned row = 0;
            const unsigned labels[] = {GRANDLEON_TEXT_NEW_GAME, GRANDLEON_TEXT_CONTINUE,
                GRANDLEON_TEXT_HELP, GRANDLEON_TEXT_SETTINGS};
            for (unsigned entry = 0; entry < 4; ++entry) {
                if (entry == 1 && !state.continue_available) continue;
                draw_text(ordering_table, next_packet, packet_end, 181, 91 + row++ * 24,
                    grandleon_game_text(labels[entry]), 247, 239, 209);
            }
            draw_panel(ordering_table, next_packet, packet_end, 10, 202, 300, 26);
            draw_button(ordering_table, next_packet, packet_end, 20, 209, 'X', GRANDLEON_TEXT_SELECT);
            draw_wrapped_text_box(ordering_table, next_packet, packet_end, 20, 159,
                state.body ? state.body : "", 181, 193, 205, 16, 2);
            activate_ui_tpage(ordering_table, next_packet, packet_end);
            return;
        }
        if (state.mode == GRANDLEON_UI_STORY || state.mode == GRANDLEON_UI_AFTERMATH ||
            state.mode == GRANDLEON_UI_OBJECTIVE) {
            draw_panel(ordering_table, next_packet, packet_end, 8, 24, 304, 208);
            draw_wrapped_text_box(ordering_table, next_packet, packet_end, 20, 36,
                state.headline ? state.headline : "", 255, 205, 68, 34, 2);
            draw_portrait(ordering_table, next_packet, packet_end, &state, 17, 83);
            if (state.speaker && state.speaker[0])
                draw_text(ordering_table, next_packet, packet_end, 76, 62, state.speaker, 94, 206, 227);
            draw_wrapped_text_box(ordering_table, next_packet, packet_end, 76, 75,
                state.body ? state.body : "", 247, 239, 209, 32, 10);
            draw_text(ordering_table, next_packet, packet_end, 184, 214,
                grandleon_game_text(GRANDLEON_TEXT_CONTINUE), 94, 206, 227);
            activate_ui_tpage(ordering_table, next_packet, packet_end);
            return;
        }
        draw_panel(ordering_table, next_packet, packet_end, 22, 28, 276,
                   state.mode == GRANDLEON_UI_TITLE_SETTINGS ? 196 : 174);
        draw_text(ordering_table, next_packet, packet_end, 42, 47,
                  state.headline ? state.headline : "GRANDLEON",
                  255, 205, 68);
        if (state.body && state.mode != GRANDLEON_UI_NAME &&
            state.mode != GRANDLEON_UI_TITLE_SETTINGS) {
            draw_wrapped_text(ordering_table, next_packet, packet_end, 42, 72,
                              state.body, 168, 181, 196);
        }
        if (state.mode == GRANDLEON_UI_TITLE) {
            draw_box(ordering_table, next_packet, packet_end,
                     72, 101 + state.campaign_menu_index * 22, 176, 18,
                     52, 63, 45);
            draw_text(ordering_table, next_packet, packet_end, 91, 105,
                      grandleon_game_text(GRANDLEON_TEXT_NEW_GAME), 247, 239, 209);
            if (state.continue_available) {
                draw_text(ordering_table, next_packet, packet_end, 91, 127,
                          grandleon_game_text(GRANDLEON_TEXT_CONTINUE), 247, 239, 209);
            }
            draw_text(ordering_table, next_packet, packet_end, 91,
                      state.continue_available ? 149 : 127,
                      grandleon_game_text(GRANDLEON_TEXT_HELP), 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 91,
                      state.continue_available ? 171 : 149,
                      grandleon_game_text(GRANDLEON_TEXT_SETTINGS), 247, 239, 209);
        } else if (state.mode == GRANDLEON_UI_PLAYERS) {
            draw_box(ordering_table, next_packet, packet_end,
                     72, 111 + state.campaign_menu_index * 25, 176, 19,
                     52, 63, 45);
            draw_text(ordering_table, next_packet, packet_end, 91, 116,
                      grandleon_game_text(GRANDLEON_TEXT_ONE_PLAYER), 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 91, 141,
                      grandleon_game_text(GRANDLEON_TEXT_TWO_PLAYERS), 247, 239, 209);
        } else if (state.mode == GRANDLEON_UI_NAME) {
            int row;
            int column;
            draw_text(ordering_table, next_packet, packet_end, 42, 59,
                      grandleon_game_text(GRANDLEON_TEXT_CHARACTER_NAME), 168, 181, 196);
            draw_box(ordering_table, next_packet, packet_end,
                     42, 71, 228, 18, 30, 53, 66);
            draw_text(ordering_table, next_packet, packet_end, 50, 75,
                      state.name_value ? state.name_value : "", 247, 239, 209);
            if (state.name_invalid) {
                draw_text(ordering_table, next_packet, packet_end, 190, 75,
                          grandleon_game_text(GRANDLEON_TEXT_REQUIRED), 255, 112, 92);
            }
            for (row = 0; row < 5; ++row) {
                const int columns = row < 4 ? 9 : 4;
                for (column = 0; column < columns; ++column) {
                    int key_x;
                    int key_width;
                    const int key_y = 92 + row * 19;
                    const char* label = grandleon_game_name_key(
                        (uint8_t)row, (uint8_t)column
                    );
                    if (row < 4) {
                        key_x = 47 + column * 25;
                        key_width = 21;
                    } else {
                        static const int last_x[4] = {47, 74, 101, 163};
                        static const int last_width[4] = {21, 21, 56, 91};
                        key_x = last_x[column];
                        key_width = last_width[column];
                    }
                    if (state.name_row == row &&
                        state.name_column == column) {
                        draw_box(ordering_table, next_packet, packet_end,
                                 key_x, key_y, key_width, 15, 52, 63, 45);
                    }
                    draw_text(ordering_table, next_packet, packet_end,
                              key_x + 6, key_y + 3, label,
                              247, 239, 209);
                }
            }
            draw_text(ordering_table, next_packet, packet_end, 42, 180,
                      "X", 94, 206, 227);
            draw_text(ordering_table, next_packet, packet_end, 56, 180,
                      grandleon_game_text(GRANDLEON_TEXT_TYPE_NAME), 168, 181, 196);
            draw_text(ordering_table, next_packet, packet_end, 150, 180,
                      "O", 94, 206, 227);
            draw_text(ordering_table, next_packet, packet_end, 164, 180,
                      grandleon_game_text(GRANDLEON_TEXT_ERASE), 168, 181, 196);
            draw_text(ordering_table, next_packet, packet_end, 42, 192,
                      "TRI", 94, 206, 227);
            draw_text(ordering_table, next_packet, packet_end, 70, 192,
                      grandleon_game_text(GRANDLEON_TEXT_KEEP), 168, 181, 196);
            draw_text(ordering_table, next_packet, packet_end, 170, 192,
                      "START", 94, 206, 227);
            draw_text(ordering_table, next_packet, packet_end, 212, 192,
                      grandleon_game_text(GRANDLEON_TEXT_SAVE), 168, 181, 196);
        } else if (state.mode == GRANDLEON_UI_TITLE_SETTINGS) {
            draw_settings_rows(ordering_table, next_packet, packet_end, &state, 60, 84);
            draw_text(ordering_table, next_packet, packet_end, 60, 212,
                      grandleon_game_text(GRANDLEON_TEXT_BACK), 168, 181, 196);
        } else {
            draw_box(ordering_table, next_packet, packet_end,
                     105, 163, 110, 18, 30, 53, 66);
            draw_text(ordering_table, next_packet, packet_end, 124, 167,
                      grandleon_game_text(
                          state.mode == GRANDLEON_UI_SAVE_ERROR
                              ? GRANDLEON_TEXT_BACK : GRANDLEON_TEXT_CONTINUE
                      ),
                      247, 239, 209);
        }
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }

    if (state.mode == GRANDLEON_UI_PAUSE) {
        const char* gold = grandleon_game_gold();
        const int wallet_height = gold[0] ? 21 : 0;
        int panel_y;
        panel_position(148, 122 + wallet_height, &x, &panel_y);
        draw_panel(ordering_table, next_packet, packet_end, x, panel_y, 148, 122 + wallet_height);
        draw_text(ordering_table, next_packet, packet_end, x + 12, panel_y + 9,
                  grandleon_game_text(GRANDLEON_TEXT_MENU), 226, 190, 111);
        draw_focus(ordering_table, next_packet, packet_end,
                   x + 8, panel_y + 29 + state.pause_menu_index * 21, 132, 19);
        draw_glyph(ordering_table, next_packet, packet_end, x + 14,
                   panel_y + 33 + state.pause_menu_index * 21, '>', 226, 190, 111);
        const unsigned labels[] = {state.collecting_loot ? GRANDLEON_TEXT_CONTINUE : GRANDLEON_TEXT_RESUME,
                                   GRANDLEON_TEXT_HELP, GRANDLEON_TEXT_SETTINGS};
        for (unsigned row = 0; row < 3; ++row)
            draw_text(ordering_table, next_packet, packet_end, x + 27, panel_y + 33 + row * 21,
                      grandleon_game_text(labels[row]), 247, 239, 209);
        if (wallet_height) draw_text(ordering_table, next_packet, packet_end, x + 12, panel_y + 100,
                                     gold, 226, 190, 111);
        draw_button(ordering_table, next_packet, packet_end, x + 12, panel_y + 100 + wallet_height, 'O', GRANDLEON_TEXT_BACK);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_BATTLE_HELP) {
        x = state.cursor_on_left ? 104 : 8;
        draw_panel(ordering_table, next_packet, packet_end, x, 42, 208, 116);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 51,
                  grandleon_game_text(GRANDLEON_TEXT_HELP), 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 72,
                  grandleon_game_text(GRANDLEON_TEXT_MOVE_CURSOR), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 88,
                  grandleon_game_text(GRANDLEON_TEXT_CROSS_CONFIRM), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 104,
                  grandleon_game_text(GRANDLEON_TEXT_CIRCLE_BACK), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 120,
                  grandleon_game_text(GRANDLEON_TEXT_START_MENU), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 139,
                  grandleon_game_text(GRANDLEON_TEXT_CROSS_CIRCLE_RETURN), 168, 181, 196);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_BATTLE_SETTINGS) {
        x = state.cursor_on_left ? 104 : 8;
        draw_panel(ordering_table, next_packet, packet_end, x, 32, 208, 200);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 41,
                  grandleon_game_text(GRANDLEON_TEXT_SETTINGS), 94, 206, 227);
        draw_settings_rows(ordering_table, next_packet, packet_end, &state, x + 12, 60);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 212,
                  grandleon_game_text(GRANDLEON_TEXT_BACK), 168, 181, 196);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }

    if (state.mode == GRANDLEON_UI_CONVERSATION) {
        draw_panel(ordering_table, next_packet, packet_end, 8, 72, 304, 160);
        draw_text(ordering_table, next_packet, packet_end, 18, 80,
                  state.headline ? state.headline : "", 94, 206, 227);
        for (unsigned row = 0; row < 3; ++row)
            draw_text(ordering_table, next_packet, packet_end, 18, 101 + (int)row * 12,
                      grandleon_game_conversation_line((uint8_t)row), 247, 239, 209);
        for (unsigned row = 0; row < state.menu_count; ++row) {
            const int y = 139 + (int)row * 16;
            if (row == state.menu_index)
                draw_focus(ordering_table, next_packet, packet_end, 12, y - 2, 296, 15);
            draw_text(ordering_table, next_packet, packet_end, 24, y,
                      grandleon_game_conversation_answer((uint8_t)row), 247, 239, 209);
        }
        draw_text(ordering_table, next_packet, packet_end, 18, 212, "X", 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, 32, 212, state.body, 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, 196, 212,
                  grandleon_game_text(GRANDLEON_TEXT_CIRCLE_BACK), 247, 239, 209);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_BATTLE_MOMENT) {
        draw_panel(ordering_table, next_packet, packet_end, 8, 157, 304, 75);
        draw_portrait(ordering_table, next_packet, packet_end, &state, 15, 164);
        if (state.headline && state.headline[0]) {
            draw_text(ordering_table, next_packet, packet_end, state.portrait_texture ? 72 : 19, 165,
                      state.headline, 94, 206, 227);
        }
        draw_wrapped_text(ordering_table, next_packet, packet_end, state.portrait_texture ? 72 : 19, 180,
                          state.body ? state.body : "", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, 282, 220,
                  state.has_more_text ? "X >" : "X", 94, 206, 227);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }

    if (state.mode == GRANDLEON_UI_DEPLOYMENT) {
        draw_panel(ordering_table, next_packet, packet_end, 64, 8, 192, 42);
        draw_text(ordering_table, next_packet, packet_end, 76, 14,
                  grandleon_game_text(GRANDLEON_TEXT_DEPLOYMENT), 255, 205, 68);
        draw_text(ordering_table, next_packet, packet_end, 76, 29,
                  "X", 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, 90, 29,
                  grandleon_game_text(state.deployment_selected
                      ? GRANDLEON_TEXT_DEPLOY : GRANDLEON_TEXT_SELECT),
                  247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, 154, 29,
                  "START", 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, 196, 29,
                  grandleon_game_text(GRANDLEON_TEXT_BEGIN), 247, 239, 209);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }

    if (state.mode == GRANDLEON_UI_OPPONENT) {
        draw_panel(ordering_table, next_packet, packet_end, 91, 8, 138, 25);
        draw_text(ordering_table, next_packet, packet_end, 108, 16,
                  grandleon_game_text(GRANDLEON_TEXT_OPPONENT_TURN), 247, 239, 209);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_COMPLETE) {
        draw_panel(ordering_table, next_packet, packet_end, 84, 8, 152, 25);
        draw_text(ordering_table, next_packet, packet_end, 102, 16,
                  grandleon_game_text(GRANDLEON_TEXT_ENCOUNTER_CLEAR), 255, 205, 68);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_SHOP) {
        unsigned row, player;
        unsigned first = state.shop_offers[state.shop_focus] > 1 ? state.shop_offers[state.shop_focus] - 1 : 0;
        if (state.menu_count <= 4) first = 0;
        else if (first + 4 > state.menu_count) first = state.menu_count - 4;
        draw_panel(ordering_table, next_packet, packet_end, 8, 8, 304, 121);
        draw_text(ordering_table, next_packet, packet_end, 20, 16, grandleon_game_text(GRANDLEON_TEXT_SHOP), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, 172, 16, grandleon_game_gold(), 255, 205, 68);
        draw_text(ordering_table, next_packet, packet_end, 214, 40, grandleon_game_text(GRANDLEON_TEXT_PRICE), 169, 186, 197);
        draw_text(ordering_table, next_packet, packet_end, 263, 40, grandleon_game_text(GRANDLEON_TEXT_STOCK), 169, 186, 197);
        for (row = 0; row < 4 && first + row < state.menu_count; ++row) {
            unsigned item = first + row;
            int y = 60 + row * 16;
            if (item == state.shop_offers[state.shop_focus]) draw_focus(ordering_table, next_packet, packet_end, 14, y - 2, 292, 16);
            draw_text(ordering_table, next_packet, packet_end, 42, y, grandleon_game_shop_offer(item, 0), 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 221, y, grandleon_game_shop_offer(item, 1), 255, 205, 68);
            draw_text(ordering_table, next_packet, packet_end, 277, y, grandleon_game_shop_offer(item, 2), 205, 211, 214);
            if (state.shop_offers[0] == item) draw_text(ordering_table, next_packet, packet_end, 17, y, "1", 94, 206, 227);
            if (state.shop_players == 2 && state.shop_offers[1] == item) draw_text(ordering_table, next_packet, packet_end, 27, y, "2", 230, 90, 125);
        }
        for (player = 0; player < state.shop_players; ++player) {
            int px = player ? 164 : 8;
            int width = state.shop_players == 2 ? 148 : 304;
            draw_panel(ordering_table, next_packet, packet_end, px, 135, width, 83);
            draw_text(ordering_table, next_packet, packet_end, px + 8, 142, player ? "P2" : "P1", player ? 230 : 94, player ? 90 : 206, player ? 125 : 227);
            draw_text(ordering_table, next_packet, packet_end, px + 30, 142, grandleon_game_shop_player(player, 0), 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, px + 8, 159, grandleon_game_shop_player(player, 1), 205, 211, 214);
            if (state.shop_confirming[player]) {
                draw_focus(ordering_table, next_packet, packet_end, px + (state.shop_buy_selected[player] ? 5 : 76), 193, 65, 17);
                draw_button(ordering_table, next_packet, packet_end, px + 8, 195, 'X', GRANDLEON_TEXT_BUY);
                draw_button(ordering_table, next_packet, packet_end, px + 79, 195, 'O', GRANDLEON_TEXT_BACK);
            } else {
                draw_text(ordering_table, next_packet, packet_end, px + 8, 177, grandleon_game_shop_player(player, 2), 255, 205, 68);
                draw_button(ordering_table, next_packet, packet_end, px + 8, 195, 'X', GRANDLEON_TEXT_BUY);
            }
        }
        draw_text(ordering_table, next_packet, packet_end, 14, 224, grandleon_game_text(GRANDLEON_TEXT_SHOP_NAVIGATION), 205, 211, 214);
        draw_text(ordering_table, next_packet, packet_end, 175, 224, "START", 205, 211, 214);
        draw_text(ordering_table, next_packet, packet_end, 217, 224, grandleon_game_text(GRANDLEON_TEXT_READY), 205, 211, 214);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_ACTION || state.mode == GRANDLEON_UI_PACK) {
        const int pack = state.mode == GRANDLEON_UI_PACK;
        const int width = pack ? 232 : 136;
        const int row_top = pack ? 44 : 25;
        unsigned int first = 0;
        unsigned int row;
        int panel_y;
        const int height = (pack ? 65 : 46) + (state.menu_count < 5 ? state.menu_count : 5) * 17;
        panel_position(width, height, &x, &panel_y);
        if (!pack) draw_discovery_notice(ordering_table, next_packet, packet_end, x < 160 ? 146 : 6);
        draw_panel(ordering_table, next_packet, packet_end, x, panel_y, width, height);
        draw_button(ordering_table, next_packet, packet_end, x + 9, panel_y + height - 19, 'X', GRANDLEON_TEXT_SELECT);
        draw_text(ordering_table, next_packet, packet_end, x + 7, panel_y + 5,
                  pack ? grandleon_game_pack_heading() : grandleon_game_text(GRANDLEON_TEXT_ACTION), 94, 206, 227);
        if (pack) draw_text(ordering_table, next_packet, packet_end, x + 7, panel_y + 23,
                            grandleon_game_pack_incoming(), 247, 239, 209);
        if (state.menu_count > 5 && state.menu_index > 2) first = state.menu_index - 2;
        if (state.menu_count > 5 && first + 5 > state.menu_count)
            first = state.menu_count - 5;
        for (row = 0; row < 5 && first + row < state.menu_count; ++row) {
            const unsigned int entry = first + row;
            const uint8_t kind = grandleon_game_action_kind((uint16_t)entry);
            if (kind == GRANDLEON_ACTION_EQUIPMENT) {
                const int row_y = panel_y + row_top + row * 17;
                if (pack) {
                    if (entry == state.menu_index) draw_focus(ordering_table, next_packet, packet_end, x + 6, row_y - 2, width - 12, 16);
                    draw_text(ordering_table, next_packet, packet_end, x + 17, row_y,
                              grandleon_game_action_label((uint16_t)entry), 247, 239, 209);
                } else draw_menu_row(ordering_table, next_packet, packet_end, x + 6, row_y,
                                    grandleon_game_action_label((uint16_t)entry), entry == state.menu_index, -1);
            } else if (kind == GRANDLEON_ACTION_ATTACK) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 6, panel_y + 25 + row * 17,
                              grandleon_game_action_label((uint16_t)entry),
                              entry == state.menu_index, -1);
            } else if (kind == GRANDLEON_ACTION_WAIT) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 6, panel_y + 25 + row * 17,
                              grandleon_game_text(GRANDLEON_TEXT_WAIT),
                              entry == state.menu_index, -1);
            } else if (kind == GRANDLEON_ACTION_ABILITY) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 6, panel_y + 25 + row * 17,
                              grandleon_game_text(GRANDLEON_TEXT_ABILITY),
                              entry == state.menu_index, entry);
            } else if (kind == GRANDLEON_ACTION_ITEM) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 6, panel_y + 25 + row * 17,
                              grandleon_game_action_label((uint16_t)entry),
                              entry == state.menu_index, -1);
            } else {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 6, panel_y + 25 + row * 17,
                              grandleon_game_text(GRANDLEON_TEXT_TALK),
                              entry == state.menu_index, -1);
            }
        }
    }

    if (state.mode == GRANDLEON_UI_TALK) {
        x = state.cursor_on_left ? 184 : 6;
        draw_panel(ordering_table, next_packet, packet_end, x, 8, 130, 25);
        draw_text(ordering_table, next_packet, packet_end, x + 7, 14,
                  grandleon_game_text(GRANDLEON_TEXT_TALK), 94, 206, 227);
    }

    if (state.mode == GRANDLEON_UI_ATTACK || state.mode == GRANDLEON_UI_ABILITY) {
        /* Let the board finish its short camera glide before adding the live
         * forecast. This keeps targeting readable instead of pinning a popup
         * over moving units, and leaves the PS1 GPU inside its frame budget. */
        if (!state.camera_settled || !state.forecast_valid) {
            if (state.show_fps) {
                activate_ui_tpage(ordering_table, next_packet, packet_end);
            }
            return;
        }
        int panel_y;
        panel_position(180, state.counter ? 78 : 65, &x, &panel_y);
        draw_panel(ordering_table, next_packet, packet_end, x, panel_y, 180,
                   state.counter ? 78 : 65);
        draw_text(ordering_table, next_packet, packet_end, x + 6, panel_y + 6,
                  grandleon_game_text(GRANDLEON_TEXT_HIT), 168, 181, 196);
        draw_number(ordering_table, next_packet, packet_end, x + 61, panel_y + 6,
                    state.hit_chance, 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 89, panel_y + 6,
                  grandleon_game_text(
                      state.restored ? GRANDLEON_TEXT_MEND : GRANDLEON_TEXT_DAMAGE
                  ), 168, 181, 196);
        draw_number(ordering_table, next_packet, packet_end, x + 146, panel_y + 6,
                    state.restored ? state.restored : state.damage, 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 6, panel_y + 19, "HP", 168, 181, 196);
        draw_number(ordering_table, next_packet, packet_end, x + 30, panel_y + 19, state.target_health, 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 52, panel_y + 19, ">", 94, 206, 227);
        {
            int remaining = state.target_health - state.damage + state.restored;
            if (remaining < 0) remaining = 0;
            if (remaining > state.target_maximum) remaining = state.target_maximum;
            draw_number(ordering_table, next_packet, packet_end, x + 68, panel_y + 19, remaining, 247, 239, 209);
            if (state.target_maximum > 0) {
                int current = state.target_health * 164 / state.target_maximum;
                int after = remaining * 164 / state.target_maximum;
                if (after > 0) draw_box_depth(ordering_table, next_packet, packet_end,
                    x + 8, panel_y + 39, after, 6, 105, 194, 167, 1);
                if (current > after) draw_box_depth(ordering_table, next_packet, packet_end,
                    x + 8 + after, panel_y + 39, current - after, 6, 220, 127, 101, 1);
                draw_box(ordering_table, next_packet, packet_end, x + 8, panel_y + 38, 164, 8, 38, 53, 67);
            }

        }
        if (state.counter) {
            draw_text(ordering_table, next_packet, packet_end, x + 6, panel_y + 53,
                      grandleon_game_text(GRANDLEON_TEXT_COUNTER), 231, 133, 104);
            draw_number(ordering_table, next_packet, packet_end, x + 111, panel_y + 53,
                        state.counter_damage, 247, 239, 209);
        }
    }


    if (state.mode == GRANDLEON_UI_RESULT && state.result_missed) {
        draw_panel(ordering_table, next_packet, packet_end, 119, 209, 82, 23);
        draw_text(ordering_table, next_packet, packet_end, 137, 214,
                  grandleon_game_text(GRANDLEON_TEXT_MISS), 231, 133, 104);
    }

    /* Context panels already carry the current decision or result. Keeping a
     * second instruction strip under them costs enough textured primitives to
     * push a busy PS1 frame over VBlank, and repeats information the standard
     * Cross/Circle convention already gives. */
    if (state.mode == GRANDLEON_UI_ACTION ||
        state.mode == GRANDLEON_UI_ATTACK ||
        state.mode == GRANDLEON_UI_ABILITY ||
        state.mode == GRANDLEON_UI_TALK ||
        state.mode == GRANDLEON_UI_RESULT) {
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }

    /* Browsing is the renderer's heaviest full-board view. Keep it clear and
     * reserve controls and unit stats for the selected-unit popup. Besides
     * matching the player UI, this preserves the measured 30 fps frame budget
     * instead of spending the last scanlines on redundant persistent help. */
    if (state.mode == GRANDLEON_UI_BROWSE) {
        if (state.collecting_loot) {
            int panel_y;
            panel_position(208, 25, &x, &panel_y);
            draw_panel(ordering_table, next_packet, packet_end, x, panel_y, 208, 25);
            draw_text(ordering_table, next_packet, packet_end, x + 8, panel_y + 7,
                      grandleon_game_text(GRANDLEON_TEXT_COLLECTION), 226, 190, 111);
        }
        if (state.show_fps || state.collecting_loot) {
            activate_ui_tpage(ordering_table, next_packet, packet_end);
        }
        return;
    }

    if (state.mode == GRANDLEON_UI_MOVE) {
        int panel_y;
        int health_width = state.actor_maximum > 0 ? state.actor_health * 126 / state.actor_maximum : 0;
        if (health_width < 0) health_width = 0;
        if (health_width > 126) health_width = 126;
        panel_position(146, 57, &x, &panel_y);
        draw_panel(ordering_table, next_packet, packet_end, x, panel_y, 146, 57);
        draw_text(ordering_table, next_packet, packet_end, x + 10, panel_y + 7, "HP", 181, 193, 205);
        draw_number(ordering_table, next_packet, packet_end, x + 35, panel_y + 7,
                    state.actor_health, 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 55, panel_y + 7, "/", 181, 193, 205);
        draw_number(ordering_table, next_packet, packet_end, x + 68, panel_y + 7,
                    state.actor_maximum, 247, 239, 209);
        draw_box(ordering_table, next_packet, packet_end, x + 10, panel_y + 23, 126, 6, 38, 53, 67);
        if (health_width) draw_box_depth(ordering_table, next_packet, packet_end,
            x + 10, panel_y + 23, health_width, 6,
            state.local_player == 2 ? 72 : 255, state.local_player == 2 ? 220 : 225,
            state.local_player == 2 ? 255 : 48, 1);
        draw_button(ordering_table, next_packet, packet_end, x + 10, panel_y + 36, 'X', GRANDLEON_TEXT_MOVE);
        draw_button(ordering_table, next_packet, packet_end, x + 79, panel_y + 36, 'O', GRANDLEON_TEXT_BACK);
    }
    activate_ui_tpage(ordering_table, next_packet, packet_end);
}

