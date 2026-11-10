// SPDX-License-Identifier: MIT
#include "runtime/scene.h"
#include "runtime/textures.h"
#include "runtime/render.h"
#include "generated/grandleon_scene.h"
#include "game_bridge.h"
#include "text_paging.h"
#include <grandleon/view/brand_logo.h>
#include <grandleon/view/console_text.h>

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
        x, y, width, height, 10, 17, 28, 3
    );
    draw_box(ordering_table, next_packet, packet_end, x, y, width, 2, 72, 191, 211);
    draw_box(ordering_table, next_packet, packet_end, x, y + height - 1, width, 1, 41, 77, 89);
    draw_box(ordering_table, next_packet, packet_end, x, y, 1, height, 41, 77, 89);
    draw_box(ordering_table, next_packet, packet_end, x + width - 1, y, 1, height, 41, 77, 89);
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
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if (character < 32 || character > 95) character = '?';
    slot = (unsigned int)(character - 32);
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

static void draw_wrapped_text_box(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, const char* text, uint8_t red, uint8_t green, uint8_t blue,
    int columns, int rows
) {
    char line[35];
    int row = 0;
    while (*text && row < rows) {
        grandleon_text_wrap_line(&text, line, (unsigned)columns);
        draw_text(ordering_table, next_packet, packet_end,
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

static void draw_settings_rows(uint32_t* ot, char** next, const char* end,
                               const grandleon_ui_state* state, int x, int y) {
    const unsigned labels[] = {GRANDLEON_TEXT_CLOSE_VIEW, GRANDLEON_TEXT_SHOW_FPS,
        GRANDLEON_TEXT_MUSIC, GRANDLEON_TEXT_EFFECTS, GRANDLEON_TEXT_LANGUAGE};
    for (unsigned row = 0; row < state->settings_menu_count && row < 5; ++row) {
        const int line = y + (row < 2 ? (int)row * 21 : row == 2 ? 42 : row == 3 ? 77 : 112);
        const int audio = row == 2 || row == 3;
        if (row == state->settings_menu_index) draw_box(ot, next, end, x - 5, line - 3, 198, audio ? 32 : 19, 52, 63, 45);
        draw_text(ot, next, end, x, line, grandleon_game_text(labels[row]), 247, 239, 209);
        if (row == 2 || row == 3) {
            const unsigned value = row == 2 ? state->music_volume : state->effects_volume;
            draw_box_depth(ot, next, end, x + 5 + (int)(value * 120 / 100), line + 15, 3, 10, 247, 239, 209, 1);
            if (value) draw_box_depth(ot, next, end, x + 5, line + 18, (int)(value * 120 / 100), 4, 94, 206, 227, 1);
            draw_box_depth(ot, next, end, x + 5, line + 18, 123, 4, 31, 46, 57, 1);
            draw_number(ot, next, end, x + 147, line + 12, value, 94, 206, 227);
            draw_text(ot, next, end, x + 169, line + 12, "%", 94, 206, 227);
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
        draw_box(ordering_table, next_packet, packet_end, x, y - 1, 92, 11, 52, 63, 45);
        draw_glyph(ordering_table, next_packet, packet_end, x + 2, y, '>', 255, 205, 68);
    }
    draw_text(ordering_table, next_packet, packet_end, x + 11, y, label,
              selected ? 255 : 210, selected ? 239 : 218, selected ? 162 : 228);
    if (suffix >= 0) {
        draw_number(ordering_table, next_packet, packet_end, x + 83, y, suffix,
                    94, 206, 227);
    }
}

void ps1_ui_render(uint32_t* ordering_table, char** next_packet, const char* packet_end) {
    grandleon_ui_state state;
    int x;
    grandleon_game_ui_state(&state);
    draw_movement_range(ordering_table, next_packet, packet_end, &state);

    if (state.combat_view_active && g_grandleon_combat_plate_indices[0]) {
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

    if (state.mode >= GRANDLEON_UI_TITLE) {
        draw_box_depth(ordering_table, next_packet, packet_end,
                       0, 0, 320, 240, 7, 13, 24, 4);
        if (state.mode == GRANDLEON_UI_TITLE) {
            draw_logo(ordering_table, next_packet, packet_end, 12, 49, 2);
            draw_panel(ordering_table, next_packet, packet_end, 153, 65, 157, 123);
            draw_box(ordering_table, next_packet, packet_end, 160,
                79 + state.campaign_menu_index * 25, 142, 20, 52, 63, 45);
            unsigned row = 0;
            const unsigned labels[] = {GRANDLEON_TEXT_NEW_GAME, GRANDLEON_TEXT_CONTINUE,
                GRANDLEON_TEXT_HELP, GRANDLEON_TEXT_SETTINGS};
            for (unsigned entry = 0; entry < 4; ++entry) {
                if (entry == 1 && !state.continue_available) continue;
                draw_text(ordering_table, next_packet, packet_end, 168, 83 + row++ * 25,
                    grandleon_game_text(labels[entry]), 247, 239, 209);
            }
            draw_text(ordering_table, next_packet, packet_end, 16, 211,
                state.body ? state.body : "STEEL AND CEDAR", 168, 181, 196);
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
        x = state.cursor_on_left ? 180 : 8;
        draw_panel(ordering_table, next_packet, packet_end, x, 48, 132, 96);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 57,
                  grandleon_game_text(GRANDLEON_TEXT_MENU), 94, 206, 227);
        draw_box(ordering_table, next_packet, packet_end,
                 x + 8, 76 + state.pause_menu_index * 19, 116, 16,
                 52, 63, 45);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 79,
                  grandleon_game_text(GRANDLEON_TEXT_RESUME), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 98,
                  grandleon_game_text(GRANDLEON_TEXT_HELP), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 117,
                  grandleon_game_text(GRANDLEON_TEXT_SETTINGS), 247, 239, 209);
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
    if (state.mode == GRANDLEON_UI_ACTION) {
        unsigned int first = 0;
        unsigned int row;
        int panel_y;
        panel_position(96, 78, &x, &panel_y);
        draw_panel(ordering_table, next_packet, packet_end, x, panel_y, 96, 78);
        draw_text(ordering_table, next_packet, packet_end, x + 7, panel_y + 5,
                  grandleon_game_text(GRANDLEON_TEXT_ACTION), 94, 206, 227);
        if (state.menu_count > 5 && state.menu_index > 2) first = state.menu_index - 2;
        if (state.menu_count > 5 && first + 5 > state.menu_count)
            first = state.menu_count - 5;
        for (row = 0; row < 5 && first + row < state.menu_count; ++row) {
            const unsigned int entry = first + row;
            const uint8_t kind = grandleon_game_action_kind((uint8_t)entry);
            if (kind == GRANDLEON_ACTION_ATTACK) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, panel_y + 22 + row * 10,
                              grandleon_game_text(GRANDLEON_TEXT_ATTACK),
                              entry == state.menu_index, -1);
            } else if (kind == GRANDLEON_ACTION_WAIT) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, panel_y + 22 + row * 10,
                              grandleon_game_text(GRANDLEON_TEXT_WAIT),
                              entry == state.menu_index, -1);
            } else if (kind == GRANDLEON_ACTION_ABILITY) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, panel_y + 22 + row * 10,
                              grandleon_game_text(GRANDLEON_TEXT_ABILITY),
                              entry == state.menu_index, entry);
            } else if (kind == GRANDLEON_ACTION_ITEM) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, panel_y + 22 + row * 10,
                              grandleon_game_text(GRANDLEON_TEXT_ITEM),
                              entry == state.menu_index, -1);
            } else {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, panel_y + 22 + row * 10,
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
        panel_position(164, state.counter ? 53 : 40, &x, &panel_y);
        draw_panel(ordering_table, next_packet, packet_end, x, panel_y, 164,
                   state.counter ? 53 : 40);
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
        }
        if (state.counter) {
            draw_text(ordering_table, next_packet, packet_end, x + 6, panel_y + 32,
                      grandleon_game_text(GRANDLEON_TEXT_COUNTER), 231, 133, 104);
            draw_number(ordering_table, next_packet, packet_end, x + 111, panel_y + 32,
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
        if (state.show_fps) {
            activate_ui_tpage(ordering_table, next_packet, packet_end);
        }
        return;
    }

    if (state.mode == GRANDLEON_UI_MOVE) {
        int health_width = 0;
        if (state.actor_maximum > 0) {
            health_width = state.actor_health * 60 / state.actor_maximum;
            if (health_width < 0) health_width = 0;
            if (health_width > 60) health_width = 60;
        }
        draw_box_depth(
            ordering_table, next_packet, packet_end,
            6, 205, 126, 28, 10, 17, 28, 3
        );
        draw_box_depth(
            ordering_table, next_packet, packet_end,
            13, 210, 60, 6, 30, 53, 66, 2
        );
        if (health_width) {
            draw_box_depth(
                ordering_table, next_packet, packet_end,
                13, 210, health_width, 6, 72, 191, 211, 1
            );
        }
        draw_text(ordering_table, next_packet, packet_end, 13, 220,
                  grandleon_game_text(GRANDLEON_TEXT_MOVE), 168, 181, 196);
        draw_text(ordering_table, next_packet, packet_end, 76, 220,
                  grandleon_game_text(GRANDLEON_TEXT_BACK), 168, 181, 196);
    }
    activate_ui_tpage(ordering_table, next_packet, packet_end);
}

