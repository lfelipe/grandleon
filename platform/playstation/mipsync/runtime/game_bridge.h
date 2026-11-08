// SPDX-License-Identifier: MIT
#ifndef GRANDLEON_MIPSYNC_GAME_BRIDGE_H
#define GRANDLEON_MIPSYNC_GAME_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int grandleon_game_init(void);
/* Returns non-zero when a rules mutation consumed this frame's render slice. */
int grandleon_game_update(uint16_t pressed_buttons);
void grandleon_game_log_boot(unsigned int packet_bytes);
void grandleon_game_log_metrics(
    uint32_t frames, uint32_t rendered_frames, uint32_t deferred_frames,
    uint32_t render_average, uint16_t render_maximum,
    uint32_t ready_average, uint16_t ready_maximum,
    uint16_t mutation_maximum, uint32_t missed_vblanks, int maximum_elapsed
);
#ifdef __cplusplus
}
#endif
#endif
