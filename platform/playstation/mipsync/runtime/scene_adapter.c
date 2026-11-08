// SPDX-License-Identifier: MIT
#include "runtime/scene.h"
#include "runtime/textures.h"
#include "generated/grandleon_scene.h"

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

void ps1_ui_render(uint32_t* ordering_table, char** next_packet, const char* packet_end) {
    const unsigned int texture = g_grandleon_ui_texture_index;
    const uint8_t u0 = ps1_texture_u_offset(texture);
    const uint8_t v0 = ps1_texture_v_offset(texture);
    POLY_FT4* panel;
    if ((const char*)(*next_packet + sizeof(POLY_FT4)) > packet_end) return;
    panel = (POLY_FT4*)*next_packet;
    setPolyFT4(panel);
    setXY4(panel, 6, 202, 134, 202, 6, 234, 134, 234);
    setUV4(panel, u0, v0, (uint8_t)(u0 + 127), v0,
           u0, (uint8_t)(v0 + 31), (uint8_t)(u0 + 127), (uint8_t)(v0 + 31));
    setRGB0(panel, 128, 128, 128);
    setShadeTex(panel, 1);
    panel->tpage = ps1_texture_tpage(texture);
    panel->clut = 0;
    addPrim(ordering_table + 1, panel);
    *next_packet = (char*)(panel + 1);
}

