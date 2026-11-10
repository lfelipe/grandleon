/* SPDX-License-Identifier: MIT */
#include "grandleon_audio.h"
#include "generated/audio_data.h"
#include <psxspu.h>
#include <psxcd.h>

#define SOUND_BASE 0x1000u
#define EFFECT_VOICES 8u
static unsigned s_music = 45, s_effects = 75, s_voice, s_theme, s_playing = 99;
static int s_ready, s_cd_ready;
static volatile int s_track_ended;
static CdlLOC s_toc[100];
static unsigned s_track_count;
static unsigned s_retry_ticks;

static void track_end(CdlIntrResult cause, uint8_t* result) {
    (void)cause;
    (void)result;
    s_track_ended = 1;
}

void grandleon_audio_set_volumes(unsigned music, unsigned effects) {
    s_music = music > 100 ? 100 : music;
    s_effects = effects > 100 ? 100 : effects;
    if (!s_ready) return;
    SpuSetCommonCDVolume(s_music * 0x3fff / 100, s_music * 0x3fff / 100);
    for (unsigned voice = 0; voice < EFFECT_VOICES; ++voice) {
        SpuSetVoiceVolume(voice, s_effects * 0x3fff / 100, s_effects * 0x3fff / 100);
    }
}

void grandleon_audio_init(void) {
    SpuInit();
    SpuSetCommonMasterVolume(0x3fff, 0x3fff);
    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    SpuSetTransferStartAddr(SOUND_BASE);
    SpuWrite((const uint32_t*)g_grandleon_sound_bank, g_grandleon_sound_bank_size);
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    s_ready = 1;
    grandleon_audio_set_volumes(s_music, s_effects);
    s_cd_ready = CdInit();
    if (s_cd_ready) {
        s_track_count = CdGetToc(s_toc);
        const uint8_t mode = CdlModeDA | CdlModeAP;
        CdControlB(CdlSetmode, &mode, 0);
        CdAutoPauseCallback(track_end);
        const CdlATV mix = {128, 0, 128, 0};
        CdMix(&mix);
        /* CD input is independently mixed; no voice or main-RAM stream. */
        SPU_CTRL |= 1u;
    }
}

void grandleon_audio_theme(unsigned theme) {
    if (theme < 6) s_theme = theme;
}

void grandleon_audio_update(void) {
    if (!s_cd_ready || s_theme + 2 > s_track_count) return;
    if (s_retry_ticks) { --s_retry_ticks; return; }
    if (s_playing == s_theme && !s_track_ended) return;
    s_track_ended = 0;
    /* TOC addresses work even on emulators that ignore CdlPlay's track arg. */
    if (CdControlB(CdlPause, 0, 0) &&
        CdControlB(CdlSetloc, &s_toc[s_theme + 1], 0) &&
        CdControlB(CdlPlay, 0, 0)) {
        s_playing = s_theme;
    } else {
        s_retry_ticks = 60;
    }
}

void grandleon_audio_cue(unsigned cue) {
    if (!s_ready || !s_effects || cue >= GRANDLEON_SOUND_COUNT) return;
    const unsigned voice = s_voice++ % EFFECT_VOICES;
    SpuSetKey(0, 1u << voice);
    SpuSetVoiceVolume(voice, s_effects * 0x3fff / 100, s_effects * 0x3fff / 100);
    SpuSetVoicePitch(voice, getSPUSampleRate(11025));
    SpuSetVoiceStartAddr(voice, SOUND_BASE + g_grandleon_sounds[cue].offset);
    SpuSetVoiceADSR(voice, 0x7f, 0, 0, 0x1f, 0xf);
    SpuSetKey(1, 1u << voice);
}

void grandleon_audio_shutdown(void) {
    if (s_ready) SpuSetKey(0, 0x00ffffff);
    if (s_cd_ready) {
        CdControlB(CdlPause, 0, 0);
        CdAutoPauseCallback(0);
    }
    s_ready = s_cd_ready = 0;
}
