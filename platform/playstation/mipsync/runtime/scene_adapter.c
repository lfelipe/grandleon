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

enum ui_label {
    UI_ATTACK, UI_ABILITY, UI_HIT, UI_DMG, UI_HEAL,
    UI_ACTION, UI_WAIT, UI_MISS, UI_COUNTER, UI_MOVE, UI_BACK
};

typedef struct ui_label_desc {
    uint8_t x;
    uint8_t y;
    uint8_t width;
} ui_label_desc;

static const ui_label_desc k_ui_labels[] = {
    {0,0,42}, {0,10,49}, {0,20,21}, {22,20,21}, {40,20,24},
    {0,30,42}, {0,40,28}, {30,40,28}, {0,50,49},
    {0,60,42}, {0,70,42},
};

static void draw_label(
    uint32_t* ordering_table, char** next_packet, const char* packet_end,
    int x, int y, enum ui_label label,
    uint8_t red, uint8_t green, uint8_t blue
) {
    const ui_label_desc* desc = &k_ui_labels[label];
    const unsigned int texture = g_grandleon_ui_label_texture_index;
    const uint8_t u = (uint8_t)(ps1_texture_u_offset(texture) + desc->x);
    const uint8_t v = (uint8_t)(ps1_texture_v_offset(texture) + desc->y);
    POLY_FT4* sprite;
    if ((const char*)(*next_packet + sizeof(POLY_FT4)) > packet_end) return;
    sprite = (POLY_FT4*)*next_packet;
    setPolyFT4(sprite);
    setXY4(sprite, x, y, x + desc->width - 1, y,
           x, y + 9, x + desc->width - 1, y + 9);
    setUV4(sprite, u, v, (uint8_t)(u + desc->width - 1), v,
           u, (uint8_t)(v + 9),
           (uint8_t)(u + desc->width - 1), (uint8_t)(v + 9));
    setRGB0(sprite, red, green, blue);
    setShadeTex(sprite, 0);
    sprite->tpage = ps1_texture_tpage(texture);
    sprite->clut = 0;
    addPrim(ordering_table + 1, sprite);
    *next_packet = (char*)(sprite + 1);
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
        draw_box(ordering_table, next_packet, packet_end, x, y - 1, 76, 11, 52, 63, 45);
        draw_glyph(ordering_table, next_packet, packet_end, x + 2, y, '>', 255, 205, 68);
    }
    if (label[0] == 'A' && label[1] == 'T') {
        draw_label(ordering_table, next_packet, packet_end, x + 11, y, UI_ATTACK,
                   selected ? 255 : 210, selected ? 239 : 218, selected ? 162 : 228);
    } else if (label[0] == 'A' && label[1] == 'B') {
        draw_label(ordering_table, next_packet, packet_end, x + 11, y, UI_ABILITY,
                   selected ? 255 : 210, selected ? 239 : 218, selected ? 162 : 228);
    } else {
        draw_label(ordering_table, next_packet, packet_end, x + 11, y, UI_WAIT,
                   selected ? 255 : 210, selected ? 239 : 218, selected ? 162 : 228);
    }
    if (suffix >= 0) {
        draw_number(ordering_table, next_packet, packet_end, x + 61, y, suffix,
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
        if (state.body) {
            draw_wrapped_text(ordering_table, next_packet, packet_end, 42, 72,
                              state.body, 168, 181, 196);
        }
        if (state.mode == GRANDLEON_UI_TITLE) {
            draw_box(ordering_table, next_packet, packet_end,
                     72, 101 + state.campaign_menu_index * 22, 176, 18,
                     52, 63, 45);
            draw_text(ordering_table, next_packet, packet_end, 91, 105,
                      "NEW GAME", 247, 239, 209);
            if (state.continue_available) {
                draw_text(ordering_table, next_packet, packet_end, 91, 127,
                          "CONTINUE", 247, 239, 209);
            }
            draw_text(ordering_table, next_packet, packet_end, 91,
                      state.continue_available ? 149 : 127,
                      "HELP", 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 91,
                      state.continue_available ? 171 : 149,
                      "SETTINGS", 247, 239, 209);
        } else if (state.mode == GRANDLEON_UI_PLAYERS) {
            draw_box(ordering_table, next_packet, packet_end,
                     72, 111 + state.campaign_menu_index * 25, 176, 19,
                     52, 63, 45);
            draw_text(ordering_table, next_packet, packet_end, 91, 116,
                      "1 PLAYER", 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 91, 141,
                      "2 PLAYERS", 247, 239, 209);
        } else if (state.mode == GRANDLEON_UI_TITLE_SETTINGS) {
            const char* close = state.close_combat ? "ON" : "OFF";
            const char* reduced = state.reduced_motion ? "ON" : "OFF";
            const char* fps = state.show_fps ? "ON" : "OFF";
            draw_box(ordering_table, next_packet, packet_end,
                     48, 101 + state.settings_menu_index * 22, 224, 18,
                     52, 63, 45);
            draw_text(ordering_table, next_packet, packet_end, 60, 105,
                      "CLOSE VIEW", 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 228, 105,
                      close, 94, 206, 227);
            draw_text(ordering_table, next_packet, packet_end, 60, 127,
                      "REDUCED MOTION", 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 228, 127,
                      reduced, 94, 206, 227);
            draw_text(ordering_table, next_packet, packet_end, 60, 149,
                      "SHOW FPS", 247, 239, 209);
            draw_text(ordering_table, next_packet, packet_end, 228, 149,
                      fps, 94, 206, 227);
            draw_text(ordering_table, next_packet, packet_end, 95, 177,
                      "CIRCLE BACK", 168, 181, 196);
        } else {
            draw_box(ordering_table, next_packet, packet_end,
                     105, 163, 110, 18, 30, 53, 66);
            draw_text(ordering_table, next_packet, packet_end, 124, 167,
                      state.mode == GRANDLEON_UI_SAVE_ERROR ? "BACK" : "CONTINUE",
                      247, 239, 209);
        }
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }

    if (state.mode == GRANDLEON_UI_PAUSE) {
        x = state.cursor_on_left ? 180 : 8;
        draw_panel(ordering_table, next_packet, packet_end, x, 48, 132, 96);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 57,
                  "MENU", 94, 206, 227);
        draw_box(ordering_table, next_packet, packet_end,
                 x + 8, 76 + state.pause_menu_index * 19, 116, 16,
                 52, 63, 45);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 79,
                  "RESUME", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 98,
                  "HELP", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 117,
                  "SETTINGS", 247, 239, 209);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_BATTLE_HELP) {
        x = state.cursor_on_left ? 104 : 8;
        draw_panel(ordering_table, next_packet, packet_end, x, 42, 208, 116);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 51,
                  "HELP", 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 72,
                  "D-PAD  MOVE CURSOR", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 88,
                  "CROSS  CONFIRM", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 104,
                  "CIRCLE BACK", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 120,
                  "START  MENU", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 139,
                  "CROSS OR CIRCLE TO RETURN", 168, 181, 196);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_BATTLE_SETTINGS) {
        const char* close = state.close_combat ? "ON" : "OFF";
        const char* reduced = state.reduced_motion ? "ON" : "OFF";
        const char* fps = state.show_fps ? "ON" : "OFF";
        x = state.cursor_on_left ? 104 : 8;
        draw_panel(ordering_table, next_packet, packet_end, x, 42, 208, 116);
        draw_text(ordering_table, next_packet, packet_end, x + 12, 51,
                  "SETTINGS", 94, 206, 227);
        draw_box(ordering_table, next_packet, packet_end,
                 x + 8, 72 + state.settings_menu_index * 21, 192, 17,
                 52, 63, 45);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 76,
                  "CLOSE VIEW", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 164, 76,
                  close, 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 97,
                  "REDUCED MOTION", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 164, 97,
                  reduced, 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 118,
                  "SHOW FPS", 247, 239, 209);
        draw_text(ordering_table, next_packet, packet_end, x + 164, 118,
                  fps, 94, 206, 227);
        draw_text(ordering_table, next_packet, packet_end, x + 17, 141,
                  "CIRCLE BACK", 168, 181, 196);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }

    if (state.mode == GRANDLEON_UI_OPPONENT) {
        draw_panel(ordering_table, next_packet, packet_end, 91, 8, 138, 25);
        draw_text(ordering_table, next_packet, packet_end, 108, 16,
                  "OPPONENT TURN", 247, 239, 209);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_COMPLETE) {
        draw_panel(ordering_table, next_packet, packet_end, 84, 8, 152, 25);
        draw_text(ordering_table, next_packet, packet_end, 102, 16,
                  "ENCOUNTER CLEAR", 255, 205, 68);
        activate_ui_tpage(ordering_table, next_packet, packet_end);
        return;
    }
    if (state.mode == GRANDLEON_UI_ACTION) {
        unsigned int first = 0;
        unsigned int row;
        x = state.cursor_on_left ? 238 : 6;
        draw_panel(ordering_table, next_packet, packet_end, x, 54, 80, 70);
        draw_label(ordering_table, next_packet, packet_end, x + 7, 59,
                   UI_ACTION, 94, 206, 227);
        if (state.menu_count > 5 && state.menu_index > 2) first = state.menu_index - 2;
        if (first + 5 > state.menu_count) first = state.menu_count - 5;
        for (row = 0; row < 5 && first + row < state.menu_count; ++row) {
            const unsigned int entry = first + row;
            if (entry == 0) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, 72 + row * 10, "ATTACK", entry == state.menu_index, -1);
            } else if (entry + 1 == state.menu_count) {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, 72 + row * 10, "WAIT", entry == state.menu_index, -1);
            } else {
                draw_menu_row(ordering_table, next_packet, packet_end,
                              x + 2, 72 + row * 10, "ABILITY", entry == state.menu_index, entry);
            }
        }
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
        x = state.cursor_on_left ? 174 : 6;
        draw_label(ordering_table, next_packet, packet_end, x + 6, 14,
                   UI_HIT, 168, 181, 196);
        draw_number(ordering_table, next_packet, packet_end, x + 34, 14,
                    state.hit_chance, 247, 239, 209);
        draw_label(ordering_table, next_packet, packet_end, x + 70, 14,
                   state.restored ? UI_HEAL : UI_DMG, 168, 181, 196);
        draw_number(ordering_table, next_packet, packet_end, x + 105, 14,
                    state.restored ? state.restored : state.damage, 247, 239, 209);
        if (state.counter) {
            draw_label(ordering_table, next_packet, packet_end, x + 6, 27,
                       UI_COUNTER, 231, 133, 104);
            draw_number(ordering_table, next_packet, packet_end, x + 62, 27,
                        state.counter_damage, 247, 239, 209);
        }
    }

    if (state.mode == GRANDLEON_UI_RESULT) {
        draw_panel(ordering_table, next_packet, packet_end, 90, 8, 140, 37);
        if (state.result_missed) {
            draw_label(ordering_table, next_packet, packet_end, 98, 13,
                       UI_MISS, 231, 133, 104);
        } else {
            draw_text(ordering_table, next_packet, packet_end, 98, 13,
                      state.restored ? "RESTORED" : "DAMAGE", 255, 205, 68);
        }
        if (!state.result_missed) {
            draw_number(ordering_table, next_packet, packet_end, 184, 13,
                        state.result_damage, 247, 239, 209);
        }
        if (state.result_counter_damage) {
            draw_label(ordering_table, next_packet, packet_end, 98, 26,
                       UI_COUNTER, 168, 181, 196);
            draw_number(ordering_table, next_packet, packet_end, 164, 26,
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
        draw_label(ordering_table, next_packet, packet_end, 13, 220,
                   UI_MOVE, 168, 181, 196);
        draw_label(ordering_table, next_packet, packet_end, 76, 220,
                   UI_BACK, 168, 181, 196);
    }
    activate_ui_tpage(ordering_table, next_packet, packet_end);
}

