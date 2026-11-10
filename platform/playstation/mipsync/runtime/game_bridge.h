// SPDX-License-Identifier: MIT
#ifndef GRANDLEON_MIPSYNC_GAME_BRIDGE_H
#define GRANDLEON_MIPSYNC_GAME_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum grandleon_ui_mode {
    GRANDLEON_UI_BROWSE = 0,
    GRANDLEON_UI_MOVE,
    GRANDLEON_UI_ACTION,
    GRANDLEON_UI_ATTACK,
    GRANDLEON_UI_ABILITY,
    GRANDLEON_UI_TALK,
    GRANDLEON_UI_DEPLOYMENT,
    GRANDLEON_UI_RESULT,
    GRANDLEON_UI_OPPONENT,
    GRANDLEON_UI_COMPLETE,
    GRANDLEON_UI_PAUSE,
    GRANDLEON_UI_BATTLE_HELP,
    GRANDLEON_UI_BATTLE_SETTINGS,
    GRANDLEON_UI_BATTLE_MOMENT,
    GRANDLEON_UI_TITLE,
    GRANDLEON_UI_PLAYERS,
    GRANDLEON_UI_STORY,
    GRANDLEON_UI_OBJECTIVE,
    GRANDLEON_UI_AFTERMATH,
    GRANDLEON_UI_SAVE_ERROR,
    GRANDLEON_UI_TITLE_HELP,
    GRANDLEON_UI_TITLE_SETTINGS,
    GRANDLEON_UI_NAME
};

enum grandleon_ui_text_id {
    GRANDLEON_TEXT_NEW_GAME = 0,
    GRANDLEON_TEXT_CONTINUE,
    GRANDLEON_TEXT_HELP,
    GRANDLEON_TEXT_SETTINGS,
    GRANDLEON_TEXT_ONE_PLAYER,
    GRANDLEON_TEXT_TWO_PLAYERS,
    GRANDLEON_TEXT_CLOSE_VIEW,
    GRANDLEON_TEXT_REDUCED_MOTION,
    GRANDLEON_TEXT_SHOW_FPS,
    GRANDLEON_TEXT_LANGUAGE,
    GRANDLEON_TEXT_MUSIC,
    GRANDLEON_TEXT_EFFECTS,
    GRANDLEON_TEXT_ON,
    GRANDLEON_TEXT_OFF,
    GRANDLEON_TEXT_BACK,
    GRANDLEON_TEXT_MENU,
    GRANDLEON_TEXT_RESUME,
    GRANDLEON_TEXT_OPPONENT_TURN,
    GRANDLEON_TEXT_CHARACTER_NAME,
    GRANDLEON_TEXT_CHOOSE_NAME,
    GRANDLEON_TEXT_REQUIRED,
    GRANDLEON_TEXT_TYPE_NAME,
    GRANDLEON_TEXT_ERASE,
    GRANDLEON_TEXT_KEEP,
    GRANDLEON_TEXT_SAVE,
    GRANDLEON_TEXT_MOVE_CURSOR,
    GRANDLEON_TEXT_CROSS_CONFIRM,
    GRANDLEON_TEXT_CIRCLE_BACK,
    GRANDLEON_TEXT_START_MENU,
    GRANDLEON_TEXT_CROSS_CIRCLE_RETURN,
    GRANDLEON_TEXT_ENCOUNTER_CLEAR,
    GRANDLEON_TEXT_ATTACK,
    GRANDLEON_TEXT_WAIT,
    GRANDLEON_TEXT_ABILITY,
    GRANDLEON_TEXT_ITEM,
    GRANDLEON_TEXT_TALK,
    GRANDLEON_TEXT_ACTION,
    GRANDLEON_TEXT_MOVE,
    GRANDLEON_TEXT_HIT,
    GRANDLEON_TEXT_MEND,
    GRANDLEON_TEXT_COUNTER,
    GRANDLEON_TEXT_MISS,
    GRANDLEON_TEXT_RESTORED,
    GRANDLEON_TEXT_DAMAGE,
    GRANDLEON_TEXT_RETURN_TO_TITLE,
    GRANDLEON_TEXT_COMPANY_REGROUP,
    GRANDLEON_TEXT_MEMORY_CARD,
    GRANDLEON_TEXT_SAVE_UNAVAILABLE,
    GRANDLEON_TEXT_CAMPAIGN_TAGLINE,
    GRANDLEON_TEXT_DEPLOYMENT,
    GRANDLEON_TEXT_SELECT,
    GRANDLEON_TEXT_DEPLOY,
    GRANDLEON_TEXT_BEGIN
};

typedef struct grandleon_ui_state {
    uint8_t mode;
    uint8_t menu_index;
    uint8_t menu_count;
    uint8_t ability_count;
    uint8_t ability_index;
    uint8_t hit_chance;
    uint8_t counter_chance;
    uint8_t counter;
    uint8_t close_combat;
    uint8_t combat_view_active;
    uint8_t local_player;
    uint8_t cursor_on_left;
    uint8_t camera_settled;
    uint8_t forecast_valid;
    uint8_t result_missed;
    int16_t actor_health;
    int16_t actor_maximum;
    int16_t target_health;
    int16_t target_maximum;
    int16_t damage;
    int16_t counter_damage;
    int16_t restored;
    int16_t result_damage;
    int16_t result_counter_damage;
    const char* headline;
    const char* body;
    uint8_t campaign_menu_index;
    uint8_t campaign_menu_count;
    uint8_t continue_available;
    uint8_t pause_menu_index;
    uint8_t settings_menu_index;
    uint8_t settings_menu_count;
    uint8_t reduced_motion;
    uint8_t show_fps;
    uint8_t music_volume;
    uint8_t effects_volume;
    uint16_t measured_fps;
    const char* language_code;
    const char* name_value;
    const char* name_default;
    uint8_t name_row;
    uint8_t name_column;
    uint8_t name_invalid;
    uint8_t name_limit;
    uint16_t movement_tile_count;
    uint16_t portrait_texture;
    uint8_t talking_phase;
    uint8_t deployment_selected;
} grandleon_ui_state;

enum grandleon_action_kind {
    GRANDLEON_ACTION_ATTACK = 0,
    GRANDLEON_ACTION_ABILITY,
    GRANDLEON_ACTION_ITEM,
    GRANDLEON_ACTION_TALK,
    GRANDLEON_ACTION_WAIT
};

int grandleon_game_init(void);
/* 1 defers rendering for a rules mutation; 2 begins a fresh gameplay timing window. */
int grandleon_game_update(
    uint16_t player_one_buttons, uint16_t player_two_buttons
);
int grandleon_game_focus_point(uint8_t index, int32_t* x, int32_t* y, int32_t* z);
int grandleon_game_movement_tile(uint16_t index, int32_t* x, int32_t* y, int32_t* z);
uint16_t grandleon_game_map_controller(uint16_t pressed);
void grandleon_game_ui_state(grandleon_ui_state* output);
const char* grandleon_game_text(uint8_t text_id);
uint8_t grandleon_game_action_kind(uint8_t index);
const char* grandleon_game_name_key(uint8_t row, uint8_t column);
void grandleon_game_set_fps(uint16_t fps);
void grandleon_game_log_boot(unsigned int packet_bytes);
void grandleon_platform_card_begin(void);
void grandleon_platform_card_end(void);
void grandleon_platform_load_stage(const char* disc_path);
void grandleon_game_log_metrics(
    uint32_t frames, uint32_t rendered_frames, uint32_t scheduled_frames,
    uint32_t deferred_frames,
    uint32_t render_average, uint16_t render_maximum,
    uint32_t ready_average, uint16_t ready_maximum,
    uint16_t mutation_maximum, uint32_t missed_vblanks, int maximum_elapsed,
    uint32_t first_missed_frame, uint32_t last_missed_frame
);
#ifdef __cplusplus
}
#endif
#endif
