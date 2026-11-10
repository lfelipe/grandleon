/* SPDX-License-Identifier: MIT */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void grandleon_audio_init(void);
void grandleon_audio_update(void);
void grandleon_audio_shutdown(void);
void grandleon_audio_set_volumes(unsigned music, unsigned effects);
void grandleon_audio_theme(unsigned theme);
void grandleon_audio_cue(unsigned cue);
#ifdef __cplusplus
}
#endif
