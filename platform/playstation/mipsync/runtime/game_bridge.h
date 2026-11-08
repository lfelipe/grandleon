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
    GRANDLEON_UI_RESULT,
    GRANDLEON_UI_OPPONENT,
    GRANDLEON_UI_COMPLETE
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
} grandleon_ui_state;

int grandleon_game_init(void);
/* Returns non-zero when a rules mutation consumed this frame's render slice. */
int grandleon_game_update(uint16_t pressed_buttons);
void grandleon_game_ui_state(grandleon_ui_state* output);
void grandleon_game_log_boot(unsigned int packet_bytes);
void grandleon_game_log_metrics(
    uint32_t frames, uint32_t rendered_frames, uint32_t deferred_frames,
    uint32_t render_average, uint16_t render_maximum,
    uint32_t ready_average, uint16_t ready_maximum,
    uint16_t mutation_maximum, uint32_t missed_vblanks, int maximum_elapsed,
    uint32_t first_missed_frame, uint32_t last_missed_frame
);
#ifdef __cplusplus
}
#endif
#endif
