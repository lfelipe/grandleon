// SPDX-License-Identifier: MIT
#include "runtime/scene.h"
#include "runtime/textures.h"
#include "generated/grandleon_scene.h"
#include "game_bridge.h"

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
    v = (uint8_t)(base_v + (slot / 8u) * 10u);
    if ((const char*)(*next_packet + sizeof(SPRT)) > packet_end) return;
    glyph = (SPRT*)*next_packet;
    setSprt(glyph);
    setXY0(glyph, x, y);
    setWH(glyph, 8, 10);
    setUV0(glyph, u, v);
    setRGB0(glyph, red, green, blue);
    setShadeTex(glyph, 0);
    setClut(glyph, 0, 0);
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
        draw_glyph(ordering_table, next_packet, packet_end, x, y, *text++, red, green, blue);
        x += 7;
    }
}

static void draw_wrapped_text(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, const char* text, uint8_t red, uint8_t green, uint8_t blue
) {
    char line[35];
    int row = 0;
    while (*text && row < 3) {
        int count = 0;
        int last_space = -1;
        while (text[count] && text[count] != '\n' && count < 34) {
            line[count] = text[count];
            if (text[count] == ' ') last_space = count;
            ++count;
        }
        if (text[count] && text[count] != '\n' && last_space > 0) count = last_space;
        line[count] = 0;
        draw_text(ordering_table, next_packet, packet_end,
                  x, y + row * 13, line, red, green, blue);
        text += count;
        while (*text == ' ' || *text == '\n') ++text;
        ++row;
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
                       0, 0, 320, 240, 7, 13, 24, 3);
        draw_panel(ordering_table, next_packet, packet_end, 22, 28, 276, 174);
        draw_text(ordering_table, next_packet, packet_end, 42, 47,
                  state.headline ? state.headline : "GRANDLEON",
                  255, 205, 68);
        if (state.body && state.mode != GRANDLEON_UI_NAME) {
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
            const char* close = grandleon_game_text(
                state.close_combat ? GRANDLEON_TEXT_ON : GRANDLEON_TEXT_OFF
            );
            const char* reduced = grandleon_game_text(
                state.reduced_motion ? GRANDLEON_TEXT_ON : GRANDLEON_TEXT_OFF
            );
            const char* fps = grandleon_game_text(
                state.show_fps ? GRANDLEON_TEXT_ON : GRANDLEON_TEXT_OFF
            );
            draw_box(ordering_table, next_packet, packet_end,
                     48, 101 + state.settings_menu_index * 22, 224, 18,
                     52, 63, 45);
            draw_text(ordering_table, next_packet, packet_end, 60, 105,
                      grandleon_game_text(GRANDLEON_TEXT_CLOSE_VIEW), 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 228, 105,
                      close, 94, 206, 227);
            draw_text(ordering_table, next_packet, packet_end, 60, 127,
                      grandleon_game_text(GRANDLEON_TEXT_REDUCED_MOTION), 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 228, 127,
                      reduced, 94, 206, 227);
            draw_text(ordering_table, next_packet, packet_end, 60, 149,
                      grandleon_game_text(GRANDLEON_TEXT_SHOW_FPS), 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 228, 149,
                      fps, 94, 206, 227);
            if (state.settings_menu_count > 3) {
                draw_text(ordering_table, next_packet, packet_end, 60, 171,
                          grandleon_game_text(GRANDLEON_TEXT_LANGUAGE), 247, 239, 209);
                draw_text(ordering_table, next_packet, packet_end, 228, 171,
                          state.language_code, 94, 206, 227);
            }
            draw_text(ordering_table, next_packet, packet_end, 95, 194,
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
        const char* close = grandleon_game_text(
            state.close_combat ? GRANDLEON_TEXT_ON : GRANDLEON_TEXT_OFF
        );
        const char* reduced = grandleon_game_text(
            state.reduced_motion ? GRANDLEON_TEXT_ON : GRANDLEON_TEXT_OFF
        );
        const char* fps = grandleon_game_text(
            state.show_fps ? GRANDLEON_TEXT_ON : GRANDLEON_TEXT_OFF
        );
        x = state.cursor_on_left ? 104 : 8;
        draw_panel(ordering_table, next_packet, packet_end, x, 42, 208,
                   state.settings_menu_count > 3 ? 142 : 116);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 51,
                  grandleon_game_text(GRANDLEON_TEXT_SETTINGS), 94, 206, 227);
        draw_box(ordering_table, next_packet, packet_end,
                 x + 8, 72 + state.settings_menu_index * 21, 192, 17,
                 52, 63, 45);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 76,
                  grandleon_game_text(GRANDLEON_TEXT_CLOSE_VIEW), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 164, 76,
                  close, 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 97,
                  grandleon_game_text(GRANDLEON_TEXT_REDUCED_MOTION), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 164, 97,
                  reduced, 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 118,
                  grandleon_game_text(GRANDLEON_TEXT_SHOW_FPS), 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 164, 118,
                  fps, 94, 206, 227);
        if (state.settings_menu_count > 3) {
            draw_text(ordering_table, next_packet, packet_end, x + 17, 139,
                      grandleon_game_text(GRANDLEON_TEXT_LANGUAGE), 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, x + 164, 139,
                      state.language_code, 94, 206, 227);
        }
        draw_text(ordering_table, next_packet, packet_end, x + 17,
                  state.settings_menu_count > 3 ? 162 : 141,
                  grandleon_game_text(GRANDLEON_TEXT_BACK), 168, 181, 196);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }

    if (state.mode == GRANDLEON_UI_BATTLE_MOMENT) {
        draw_panel(ordering_table, next_packet, packet_end, 8, 157, 304, 75);
        if (state.headline && state.headline[0]) {
            draw_text(ordering_table, next_packet, packet_end, 19, 165,
                      state.headline, 94, 206, 227);
        }
        draw_wrapped_text(ordering_table, next_packet, packet_end, 19, 180,
                          state.body ? state.body : "", 247, 239, 209);
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
        x = state.cursor_on_left ? 222 : 6;
        draw_panel(ordering_table, next_packet, packet_end, x, 54, 96, 70);
        draw_text(ordering_table, next_packet, packet_end, x + 7, 59,
                  grandleon_game_text(GRANDLEON_TEXT_ACTION), 94, 206, 227);
        if (state.menu_count > 5 && state.menu_index > 2) first = state.menu_index - 2;
        if (first + 5 > state.menu_count) first = state.menu_count - 5;
        for (row = 0; row < 5 && first + row < state.menu_count; ++row) {
            const unsigned int entry = first + row;
            const uint8_t kind = grandleon_game_action_kind((uint8_t)entry);
            if (kind == GRANDLEON_ACTION_ATTACK) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, 72 + row * 10,
                              grandleon_game_text(GRANDLEON_TEXT_ATTACK),
                              entry == state.menu_index, -1);
            } else if (kind == GRANDLEON_ACTION_WAIT) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, 72 + row * 10,
                              grandleon_game_text(GRANDLEON_TEXT_WAIT),
                              entry == state.menu_index, -1);
            } else if (kind == GRANDLEON_ACTION_ABILITY) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, 72 + row * 10,
                              grandleon_game_text(GRANDLEON_TEXT_ABILITY),
                              entry == state.menu_index, entry);
            } else if (kind == GRANDLEON_ACTION_ITEM) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, 72 + row * 10,
                              grandleon_game_text(GRANDLEON_TEXT_ITEM),
                              entry == state.menu_index, -1);
            } else {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, 72 + row * 10,
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
        x = state.cursor_on_left ? 150 : 6;
        draw_panel(ordering_table, next_packet, packet_end, x, 8, 164,
                   state.counter ? 37 : 25);
        draw_text(ordering_table, next_packet, packet_end, x + 6, 14,
                  grandleon_game_text(GRANDLEON_TEXT_HIT), 168, 181, 196);
        draw_number(ordering_table, next_packet, packet_end, x + 61, 14,
                    state.hit_chance, 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 89, 14,
                  grandleon_game_text(
                      state.restored ? GRANDLEON_TEXT_MEND : GRANDLEON_TEXT_DAMAGE
                  ), 168, 181, 196);
        draw_number(ordering_table, next_packet, packet_end, x + 146, 14,
                    state.restored ? state.restored : state.damage, 247, 239, 209);
        if (state.counter) {
            draw_text(ordering_table, next_packet, packet_end, x + 6, 27,
                      grandleon_game_text(GRANDLEON_TEXT_COUNTER), 231, 133, 104);
            draw_number(ordering_table, next_packet, packet_end, x + 111, 27,
                        state.counter_damage, 247, 239, 209);
        }
    }

    if (state.mode == GRANDLEON_UI_RESULT) {
        draw_panel(ordering_table, next_packet, packet_end, 70, 8, 180, 37);
        if (state.result_missed) {
            draw_text(ordering_table, next_packet, packet_end, 78, 13,
                      grandleon_game_text(GRANDLEON_TEXT_MISS), 231, 133, 104);
        } else {
            draw_text(ordering_table, next_packet, packet_end, 78, 13,
                      grandleon_game_text(
                          state.restored ? GRANDLEON_TEXT_RESTORED
                                         : GRANDLEON_TEXT_DAMAGE
                      ), 255, 205, 68);
        }
        if (!state.result_missed) {
            draw_number(ordering_table, next_packet, packet_end, 187, 13,
                        state.result_damage, 247, 239, 209);
        }
        if (state.result_counter_damage) {
            draw_text(ordering_table, next_packet, packet_end, 78, 26,
                      grandleon_game_text(GRANDLEON_TEXT_COUNTER), 168, 181, 196);
            draw_number(ordering_table, next_packet, packet_end, 183, 26,
                        state.result_counter_damage, 247, 239, 209);
        }
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
     * matching the player UI, this preserves a full 60 Hz hardware frame
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

