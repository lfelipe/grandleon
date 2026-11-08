// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stdio.h>
#include <hwregs_c.h>
#include <psxapi.h>
#include <psxgpu.h>

#include "runtime/render.h"
#include "runtime/scene.h"
#include "runtime/textures.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define ORDERING_TABLE_LENGTH PS1_RENDER_OT_LEN
#define PACKET_BYTES_PER_FRAME 24576

/* Artifact audit marker: release/export must fail closed if this is absent. */
__attribute__((used, section(".rodata")))
static const char k_renderer_identity[] = "GRANDLEON_PS1_RENDERER=MIPSYNC";

static DISPENV s_display[2];
static DRAWENV s_draw[2];
static uint32_t s_ordering_table[2][ORDERING_TABLE_LENGTH];
static char s_packets[2][PACKET_BYTES_PER_FRAME];
static char* s_next_packet;
static int s_buffer;
static uint16_t s_gpu_ready_tick;

static void setup_display(void) {
    ResetGraph(0);
    SetDefDispEnv(&s_display[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDispEnv(&s_display[1], 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDrawEnv(&s_draw[0], 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDrawEnv(&s_draw[1], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    setRGB0(&s_draw[0], g_ps1_scene.fog_r, g_ps1_scene.fog_g, g_ps1_scene.fog_b);
    setRGB0(&s_draw[1], g_ps1_scene.fog_r, g_ps1_scene.fog_g, g_ps1_scene.fog_b);
    s_draw[0].isbg = 1;
    s_draw[1].isbg = 1;
    ClearOTagR(s_ordering_table[0], ORDERING_TABLE_LENGTH);
    ClearOTagR(s_ordering_table[1], ORDERING_TABLE_LENGTH);
    s_next_packet = s_packets[0];
    PutDispEnv(&s_display[0]);
    PutDrawEnv(&s_draw[0]);
    SetDispMask(1);
}

static void present(void) {
    DrawSync(0);
    s_gpu_ready_tick = (uint16_t)TIMER_VALUE(1);
    VSync(0);
    s_buffer ^= 1;
    ClearOTagR(s_ordering_table[s_buffer], ORDERING_TABLE_LENGTH);
    PutDispEnv(&s_display[s_buffer]);
    PutDrawEnv(&s_draw[s_buffer]);
    SetDispMask(1);
    s_next_packet = s_packets[s_buffer];
    ps1_render_submit(
        s_ordering_table[1 - s_buffer], s_draw[s_buffer].clip.x,
        s_draw[s_buffer].clip.y, SCREEN_WIDTH, SCREEN_HEIGHT, 0
    );
}

int main(void) {
    uint32_t frame = 0;
    uint32_t measured_frames = 0;
    uint32_t render_total = 0;
    uint32_t ready_total = 0;
    uint32_t missed_vblanks = 0;
    uint16_t render_max = 0;
    uint16_t ready_max = 0;
    int last_vblank;
    int maximum_elapsed = 1;

    setup_display();
    ps1_render_init(SCREEN_WIDTH, SCREEN_HEIGHT);
    ps1_textures_init();
    printf("%s\n", k_renderer_identity);
    printf("MIPSYNC_GRANDLEON_SCENE_READY packet_bytes=%u\n", PACKET_BYTES_PER_FRAME);
    TIMER_CTRL(1) = 0x0100;
    TIMER_VALUE(1) = 0;
    last_vblank = VSync(-1);

    for (;;) {
        const uint16_t frame_start = (uint16_t)TIMER_VALUE(1);
        const int current_vblank = VSync(-1);
        const int elapsed = current_vblank - last_vblank;
        uint16_t render_start;
        uint16_t render_ticks;
        uint16_t ready_ticks;
        last_vblank = current_vblank;
        if (frame >= 60) {
            if (elapsed > 1) missed_vblanks += (uint32_t)(elapsed - 1);
            if (elapsed > maximum_elapsed) maximum_elapsed = elapsed;
        }

        render_start = (uint16_t)TIMER_VALUE(1);
        ps1_render_frame(
            s_ordering_table[s_buffer], &s_next_packet,
            s_packets[s_buffer] + PACKET_BYTES_PER_FRAME
        );
        render_ticks = (uint16_t)((uint16_t)TIMER_VALUE(1) - render_start);
        present();
        ready_ticks = (uint16_t)(s_gpu_ready_tick - frame_start);
        ++frame;
        if (frame > 60) {
            render_total += render_ticks;
            ready_total += ready_ticks;
            if (render_ticks > render_max) render_max = render_ticks;
            if (ready_ticks > ready_max) ready_max = ready_ticks;
            ++measured_frames;
        }

        if (measured_frames == 600) {
            printf(
                "MIPSYNC_GRANDLEON_METRICS warmup=60 frames=%lu render_hbl_avg=%lu "
                "render_hbl_max=%u ready_hbl_avg=%lu ready_hbl_max=%u "
                "missed_vblanks=%lu max_elapsed_vblanks=%d\n",
                (unsigned long)measured_frames,
                (unsigned long)(render_total / measured_frames),
                (unsigned)render_max,
                (unsigned long)(ready_total / measured_frames),
                (unsigned)ready_max, (unsigned long)missed_vblanks,
                maximum_elapsed
            );
            /* Serial telemetry is intentionally outside the frame budget. */
            last_vblank = VSync(-1);
        }
    }
}

