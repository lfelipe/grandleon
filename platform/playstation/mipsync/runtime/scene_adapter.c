// SPDX-License-Identifier: MIT
#include "runtime/scene.h"

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
    (void)ordering_table;
    (void)next_packet;
    (void)packet_end;
}

