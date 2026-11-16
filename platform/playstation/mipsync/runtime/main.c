// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <hwregs_c.h>
#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <psxpad.h>

#include "game_bridge.h"
#include "grandleon_audio.h"
#include "runtime/render.h"
#include "runtime/packet_fragment.h"
#include "runtime/scene.h"
#include "runtime/textures.h"
#include "terrain_motion.h"
#include "packet_relocation.h"
#include "exact_snapshot.h"
#include "generated/grandleon_scene.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define ORDERING_TABLE_LENGTH PS1_RENDER_OT_LEN
/* One larger command arena and ordering table fit in the space previously
 * split between two smaller pairs. VRAM presentation stays double buffered. */
#define PACKET_BYTES_PER_FRAME 57344
#define MAX_PACKET_BYTES_PER_FRAME PACKET_BYTES_PER_FRAME
#define TARGET_RENDER_FPS 30

/* Artifact audit marker: release/export must fail closed if this is absent. */
__attribute__((used, section(".rodata")))
static const char k_renderer_identity[] = "GRANDLEON_PS1_RENDERER=MIPSYNC";

static DISPENV s_display[2];
static DRAWENV s_draw[2];
static uint32_t s_ordering_table[1][ORDERING_TABLE_LENGTH];
static __attribute__((aligned(4))) char grandleon_packet_arena[PACKET_BYTES_PER_FRAME];
static char* s_next_packet;
static int s_buffer;
static uint16_t s_gpu_ready_tick;
static int s_frame_pending, s_last_present_vblank;
static uint8_t s_pad_buffers[2][34];
static uint16_t s_last_buttons[2] = {0xffffu, 0xffffu};

/* Observe normal updates through an exported seam. Stage-specific code may
 * change its address; native probes resolve each executable independently. */
__attribute__((noinline)) int grandleon_runtime_update(
    uint16_t player_one, uint16_t player_two
) {
    return grandleon_game_update(player_one, player_two);
}

/* Reuse world commands only for this explicit-state Grandleon adapter.
 * Mipsync still draws every changing frame. UI is always rebuilt, and exact
 * entity snapshots (not a hash) invalidate camera, pose and material changes.
 * Templates remain in the same retained command arena. */
_Static_assert(sizeof(ps1_entity) % sizeof(uint32_t) == 0, "entity snapshots need whole words");
static uint32_t s_world_entities[GRANDLEON_WORLD_ENTITY_CAPACITY * sizeof(ps1_entity) / sizeof(uint32_t)];
static struct grandleon_exact_snapshot s_world_key;
#define WORLD_CACHE_HEAD_CAPACITY 512
static struct { uint32_t tag; uint16_t bucket; } s_world_heads[WORLD_CACHE_HEAD_CAPACITY];
static unsigned s_world_head_count;
static char* s_world_pending_start;
static uint32_t s_world_bytes;
static uint32_t s_world_hits, s_world_misses;
static uint32_t s_world_free_min = MAX_PACKET_BYTES_PER_FRAME;
static uint32_t s_world_near_limit_frames;
static int s_world_ready, s_world_recording, s_world_replaying;
static uint16_t s_world_render_started, s_world_ui_started, s_world_ui_ticks;

/* Generated meshes and draw-state descriptors are immutable. Texture pixels
 * may refresh in their fixed VRAM slots after GPU retirement. Mutable mesh data must
 * invalidate this cache; engine-driven animation and VM bindings opt out. */
static int scene_cache_supported(void) {
    unsigned i;
    if (g_ps1_scene.binding_count || g_ps1_scene.rigid_anim_frame_count ||
        g_ps1_scene.transform_anim_key_count || g_ps1_scene.background_texture_index)
        return 0;
    for (i = 0; i < g_ps1_scene.entity_count; ++i) {
        const ps1_entity* entity = &g_ps1_scene.entities[i];
        if (entity->vertex_anim_frame_count || entity->rigid_anim_frame_count ||
            entity->transform_anim_key_count || entity->prerender_occluder)
            return 0;
    }
    return 1;
}

/* Read object representations through memcpy to preserve strict aliasing.
 * Constant-size copies become word loads instead of the CRT's byte loop. */
static int world_entities_match(void) {
    return grandleon_snapshot_matches(&s_world_key, s_world_entities,
        g_ps1_scene.entities, g_ps1_scene.entity_count, sizeof(ps1_entity) / 4u);
}

static void static_group_before_world_snapshot(void);
static void static_group_invalidate(void);

static int world_entities_capture(void) {
    static_group_before_world_snapshot();
    if (!grandleon_snapshot_capture(&s_world_key, s_world_entities, sizeof(s_world_entities),
            g_ps1_scene.entities, g_ps1_scene.entity_count, sizeof(ps1_entity) / 4u)) {
        /* Partial keys invalidate both consumers of the shared allocation. */
        static_group_invalidate();
        return 0;
    }
    return 1;
}

static uint32_t dma_address(const void* pointer) {
    return (uint32_t)(uintptr_t)pointer & 0xffffffu;
}

/* Retain immutable board and architecture commands at their original positions
 * in the submission order. Unit, cursor and foliage commands stay dynamic.
 * Source commands live in the retained arena; no separate template buffer. */
#define STATIC_PACKET_HEAD_CAPACITY 256
struct static_fragment {
    ps1_packet_fragment_bucket heads[STATIC_PACKET_HEAD_CAPACITY];
    size_t head_count;
    uint32_t entity[sizeof(ps1_entity) / 4u];
    uint32_t camera[sizeof(ps1_entity) / 4u];
    const uint32_t* packets;
    uint32_t bytes;
    int added, ready;
};
static struct static_fragment s_static_fragments[3];
static const ps1_entity* s_static_targets[3];
static uint32_t s_static_packet_hits, s_static_packet_misses, s_static_packet_rejects;
static int s_static_packets_supported;
static uint32_t s_static_packet_marks[MAX_PACKET_BYTES_PER_FRAME / 4u / 32u];

static void static_group_begin_frame(void);

static void static_packets_begin_frame(int supported) {
    /* Attached point lights need a wider cache key than these immutable,
     * unlit generated meshes. Leave them on the normal renderer path. */
    s_static_packets_supported = supported && !g_ps1_scene.light_count;
    s_static_targets[0] = g_grandleon_board_surface_count
        ? &g_ps1_scene.entities[g_grandleon_board_surface_entity_indices[0]] : 0;
    s_static_targets[1] = 0;
    for (unsigned i = 0; i < g_grandleon_terrain_chunk_count; ++i) {
        const ps1_entity* entity =
            &g_ps1_scene.entities[g_grandleon_terrain_entity_indices[i]];
        if (entity->mesh_enabled) {
            s_static_targets[1] = entity;
            break;
        }
    }
    s_static_targets[2] = !g_grandleon_visibility_per_cell && g_grandleon_visibility_cell_count
        ? &g_ps1_scene.entities[g_grandleon_visibility_entity_index] : 0;
    if (s_static_targets[2] && !s_static_targets[2]->mesh_enabled)
        s_static_targets[2] = 0;
    if (!s_static_targets[0] || !s_static_targets[0]->mesh_enabled)
        s_static_packets_supported = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!s_static_packets_supported || !s_static_targets[i])
            s_static_fragments[i].ready = 0;
    }
    static_group_begin_frame();
}

static struct static_fragment* static_packet_fragment_for(const ps1_entity* entity) {
    for (unsigned i = 0; i < 3; ++i) {
        if (entity == s_static_targets[i] && entity->mesh_enabled &&
            entity->render_unlit && entity->parent_entity_index < 0)
            return &s_static_fragments[i];
    }
    return 0;
}

static const ps1_entity* static_packet_camera(void) {
    const int index = g_ps1_scene.camera_entity_index;
    return index >= 0 && (unsigned)index < g_ps1_scene.entity_count
        ? &g_ps1_scene.entities[index] : 0;
}

static int static_packet_entity_matches(const uint32_t* snapshot, const void* entity) {
    const unsigned char* bytes = (const unsigned char*)entity;
    for (size_t i = 0; i < sizeof(ps1_entity) / 4u; ++i) {
        uint32_t word;
        __builtin_memcpy(&word, bytes + i * 4u, 4u);
        if (word != snapshot[i]) return 0;
    }
    return 1;
}

static int individual_packet_replay(uint32_t* ot, char** next,
    const char* end, const ps1_entity* entity, int viewmodel) {
    struct static_fragment* cache;
    const ps1_entity* camera;
    uintptr_t source;
    if (!s_static_packets_supported || viewmodel) return -1;
    cache = static_packet_fragment_for(entity);
    if (!cache) return -1;
    camera = static_packet_camera();
    source = (uintptr_t)cache->packets;
    /* Earlier entities may already have overwritten a template in this arena.
     * Downward overlapping copies are safe; a source behind the write cursor
     * must be rebuilt. Camera/mesh state comparisons are exact, not hashes. */
    if (!s_static_packets_supported || !camera || camera->parent_entity_index >= 0 ||
        !cache->ready || end - *next < (ptrdiff_t)(cache->bytes + 64u) ||
        !static_packet_entity_matches(cache->entity, entity) ||
        !static_packet_entity_matches(cache->camera, camera) ||
        (source < (uintptr_t)*next &&
         source + cache->bytes > (uintptr_t)s_world_pending_start)) {
        ++s_static_packet_misses;
        return -1;
    }
    if (!grandleon_gpu_range(dma_address(*next), cache->bytes)) return -1;
    ps1_packet_fragment_copy((uint32_t*)*next, cache->packets, cache->bytes / 4u,
        dma_address(cache->packets), dma_address(*next), ot, cache->heads, cache->head_count);
    cache->packets = (const uint32_t*)*next;
    *next += cache->bytes;
    ++s_static_packet_hits;
    return cache->added;
}

static void individual_packet_capture(uint32_t* ot, const char* begin,
    const char* next, const char* end, const ps1_entity* entity, int viewmodel, int primitives) {
    struct static_fragment* cache;
    const ps1_entity* camera;
    if (!s_static_packets_supported || viewmodel) return;
    cache = static_packet_fragment_for(entity);
    if (!cache) return;
    camera = static_packet_camera();
    cache->ready = 0;
    if (!s_static_packets_supported || !camera || camera->parent_entity_index >= 0 ||
        primitives <= 0 || next <= begin || end - next < 64) return;
    cache->bytes = (uint32_t)(next - begin);
    if (!ps1_packet_fragment_capture((const uint32_t*)begin, cache->bytes / 4u,
        dma_address(begin), ot, ORDERING_TABLE_LENGTH, cache->heads,
        STATIC_PACKET_HEAD_CAPACITY, s_static_packet_marks,
        sizeof(s_static_packet_marks) / sizeof(s_static_packet_marks[0]), &cache->head_count)) {
        ++s_static_packet_rejects;
        return;
    }
    __builtin_memcpy(cache->entity, entity, sizeof(ps1_entity));
    __builtin_memcpy(cache->camera, camera, sizeof(ps1_entity));
    cache->packets = (const uint32_t*)begin;
    cache->added = primitives;
    cache->ready = 1;
}


/* Generated floors and landmarks form a contiguous custom-mesh prefix.
 * Character-preset houses are submitted before it and remain separate.
 *
 * Exact entity keys share the whole-world snapshot allocation to stay within
 * RAM. A group write invalidates the whole-world template. A whole-world write
 * invalidates this group unless the prefix keys are unchanged. Camera keys
 * remain independent. Neither writer may silently replace the other's keys. */
static unsigned s_group_count, s_group_last;
static int s_group_active, s_group_skip, s_group_record;
static int s_group_primitives;
static const char* s_group_begin;

static int group_keys_match(void) {
    return grandleon_snapshot_matches(&s_world_key, s_world_entities,
        g_ps1_scene.entities, s_group_count, sizeof(ps1_entity) / 4u);
}

static void static_group_invalidate(void) {
    s_static_fragments[0].ready = 0;
}

static void static_group_before_world_snapshot(void) {
    if (s_group_active && s_static_fragments[0].ready && !group_keys_match())
        s_static_fragments[0].ready = 0;
}

static void static_group_begin_frame(void) {
    unsigned count = 0, last = 0;
    int active = s_static_packets_supported;
    const int previous = s_group_active;
    for (unsigned i = 0; i < g_grandleon_tree_count; ++i) {
        const unsigned next = g_grandleon_tree_entity_indices[i] + 1u;
        if (next > count) count = next;
    }
    for (unsigned i = 0; i < g_grandleon_terrain_chunk_count; ++i) {
        const unsigned next = g_grandleon_terrain_entity_indices[i] + 1u;
        if (next > count) count = next;
    }
    for (unsigned i = 0; i < g_grandleon_board_surface_count; ++i) {
        const unsigned next = g_grandleon_board_surface_entity_indices[i] + 1u;
        if (next > count) count = next;
    }
    if (!count || count > g_ps1_scene.entity_count)
        active = 0;
    if (active) {
        for (unsigned i = 0; i < count; ++i) {
            const ps1_entity* entity = &g_ps1_scene.entities[i];
            /* Generated prefix contains only static floors/landmarks.
             * Character-preset houses are submitted earlier, never skipped. */
            if (entity->mesh != PS1_MESH_CUSTOM || !entity->render_unlit ||
                entity->parent_entity_index >= 0 ||
                (entity->render_preset != PS1_RENDER_PRESET_FLOOR &&
                 entity->render_preset != PS1_RENDER_PRESET_CHARACTER)) {
                active = 0;
                break;
            }
            if (entity->mesh_enabled && entity->render_preset == PS1_RENDER_PRESET_FLOOR)
                last = i;
        }
    }
    if (active && (!g_ps1_scene.entities[0].mesh_enabled ||
                  g_ps1_scene.entities[0].render_preset != PS1_RENDER_PRESET_FLOOR))
        active = 0;
    s_group_count = count;
    s_group_last = last;
    s_group_active = active;
    s_group_skip = s_group_record = 0;
    if (previous != active) s_static_fragments[0].ready = 0;
    if (active) {
        s_static_targets[1] = 0;
        s_static_fragments[1].ready = 0;
    }
}

int ps1_render_entity_packet_replay(uint32_t* ot, char** next, const char* end,
    const ps1_entity* entity, int viewmodel) {
    const uintptr_t offset = (uintptr_t)entity - (uintptr_t)g_ps1_scene.entities;
    const unsigned index = offset < g_ps1_scene.entity_count * sizeof(ps1_entity) &&
        offset % sizeof(ps1_entity) == 0 ? offset / sizeof(ps1_entity) : s_group_count;
    if (!viewmodel && s_group_active && index < s_group_count &&
        entity->render_preset == PS1_RENDER_PRESET_FLOOR) {
        int result;
        if (s_group_skip) return 0;
        if (index) return -1;
        s_group_begin = *next;
        s_group_record = 1;
        s_group_primitives = 0;
        if (s_static_fragments[0].ready && !group_keys_match())
            s_static_fragments[0].ready = 0;
        result = individual_packet_replay(ot, next, end, entity, viewmodel);
        if (result >= 0) {
            s_group_skip = 1;
            s_group_record = 0;
        } else {
            /* A partial rebuild cannot survive into another frame. */
            s_static_fragments[0].ready = 0;
        }
        return result;
    }
    return individual_packet_replay(ot, next, end, entity, viewmodel);
}

void ps1_render_entity_packet_capture(uint32_t* ot, const char* begin, const char* next,
    const char* end, const ps1_entity* entity, int viewmodel, int primitives) {
    const uintptr_t offset = (uintptr_t)entity - (uintptr_t)g_ps1_scene.entities;
    const unsigned index = offset < g_ps1_scene.entity_count * sizeof(ps1_entity) &&
        offset % sizeof(ps1_entity) == 0 ? offset / sizeof(ps1_entity) : s_group_count;
    if (!viewmodel && s_group_active && index < s_group_count &&
        entity->render_preset == PS1_RENDER_PRESET_FLOOR) {
        if (!s_group_record) return;
        s_group_primitives += primitives;
        if (index != s_group_last) return;
        s_group_record = 0;
        individual_packet_capture(ot, s_group_begin, next, end,
            &g_ps1_scene.entities[0], 0, s_group_primitives);
        if (s_static_fragments[0].ready) {
            s_world_ready = 0;
            if (!grandleon_snapshot_capture(&s_world_key, s_world_entities, sizeof(s_world_entities),
                    g_ps1_scene.entities, s_group_count, sizeof(ps1_entity) / 4u))
                static_group_invalidate();
        }
        return;
    }
    individual_packet_capture(ot, begin, next, end, entity, viewmodel, primitives);
}

static void capture_world_packets(uint32_t* ot, char** next_packet) {
    grandleon_packet_ranges ranges;
    unsigned i;
    if (s_world_replaying || !s_world_recording) return;
    s_world_recording = 0;
    s_world_ready = 0;
    /* Avoid adding cache construction to an already full frame. The normal
     * renderer warms its own projection caches first; retry on a cheap frame.
     * Allow for the previous UI cost and an estimated snapshot/link-scan cost. */
    {
        const unsigned world_ticks = (uint16_t)((uint16_t)TIMER_VALUE(1) - s_world_render_started);
        const unsigned ui_budget = s_world_ui_ticks > 64u ? s_world_ui_ticks : 64u;
        if (world_ticks + ui_budget + 56u > 526u) return;
    }
    if (s_world_pending_start != grandleon_packet_arena ||
        *next_packet < s_world_pending_start ||
        *next_packet - s_world_pending_start > PACKET_BYTES_PER_FRAME)
        return;
    ranges.source_packets = ranges.target_packets = dma_address(s_world_pending_start);
    ranges.packet_bytes = (uint32_t)(*next_packet - s_world_pending_start);
    /* A nearly full arena may have stopped a draw at the packet boundary.
     * Do not retain a potentially partial world as a persistent template. */
    if (ranges.packet_bytes > PACKET_BYTES_PER_FRAME - PS1_UI_PACKET_RESERVE - 64u) return;
    ranges.source_ot = ranges.target_ot = dma_address(ot);
    ranges.ot_bytes = sizeof(s_ordering_table[0]);
    if (!grandleon_gpu_stream_valid((const uint32_t*)s_world_pending_start,
            ranges.packet_bytes / sizeof(uint32_t), &ranges)) return;
    s_world_head_count = 0;
    for (i = 0; i < ORDERING_TABLE_LENGTH; ++i) {
        const uint32_t empty = i ? ranges.source_ot + (i - 1u) * 4u : 0xffffffu;
        const uint32_t address = ot[i] & 0xffffffu;
        if (ot[i] == empty) continue;
        if ((address & 3u) || address < ranges.source_packets ||
            address - ranges.source_packets >= ranges.packet_bytes ||
            s_world_head_count == WORLD_CACHE_HEAD_CAPACITY) return;
        s_world_heads[s_world_head_count].tag = ot[i];
        s_world_heads[s_world_head_count++].bucket = (uint16_t)i;
    }
    if (!world_entities_capture()) return;
    s_world_bytes = ranges.packet_bytes;
    s_world_ready = 1;
}

void grandleon_world_packet_cache_capture(uint32_t* ot, char** next_packet) {
    const ptrdiff_t remaining = grandleon_packet_arena + PACKET_BYTES_PER_FRAME
        - PS1_UI_PACKET_RESERVE - *next_packet;
    const uint32_t free_bytes = remaining > 0 ? (uint32_t)remaining : 0;
    if (free_bytes < s_world_free_min) s_world_free_min = free_bytes;
    if (free_bytes < 256u) ++s_world_near_limit_frames;
    capture_world_packets(ot, next_packet);
    s_world_ui_started = (uint16_t)TIMER_VALUE(1);
}

static void render_world_and_ui(uint32_t* ot, char** next_packet, const char* end) {
    const int scene_supported = scene_cache_supported();
    /* Moving actors invalidate a whole-world template. Static fragments have
     * independent exact entity/camera keys and can survive those movements. */
    const int supported = scene_supported && grandleon_game_world_packets_stable();
    if (supported && s_world_ready && *next_packet == grandleon_packet_arena &&
        s_world_bytes <= (uint32_t)(end - *next_packet) && world_entities_match()) {
        /* The retained prefix and table always have the same addresses. Only
         * UI commands append beyond it. Restore the validated world heads;
         * previous conversation and menu commands are never retained. */
        for (unsigned i = 0; i < s_world_head_count; ++i)
            ot[s_world_heads[i].bucket] = s_world_heads[i].tag;
        *next_packet += s_world_bytes;
        ++s_world_hits;
        s_world_replaying = 1;
        ps1_ui_render(ot, next_packet, end);
        s_world_ui_ticks = (uint16_t)((uint16_t)TIMER_VALUE(1) - s_world_ui_started);
        s_world_replaying = 0;
        return;
    }
    ++s_world_misses;
    s_world_ready = 0;
    s_world_recording = supported;
    s_world_pending_start = *next_packet;
    s_world_render_started = (uint16_t)TIMER_VALUE(1);
    static_packets_begin_frame(scene_supported && grandleon_game_static_packets_stable());
    ps1_render_frame(ot, next_packet, end);
    s_world_ui_ticks = (uint16_t)((uint16_t)TIMER_VALUE(1) - s_world_ui_started);
}

void grandleon_platform_card_begin(void) {
    StopPAD();
}

void grandleon_platform_card_end(void) {
    StartPAD();
    ChangeClearPAD(0);
    /* Keep the sampled button state across card I/O. Resetting it here makes
     * a held Confirm or direction look newly pressed after every save. */
}

void grandleon_platform_load_stage(const char* disc_path) {
    if (!disc_path || !disc_path[0]) return;
    DrawSync(0);
    grandleon_audio_shutdown();
    StopPAD();
    StopCallback();
    _96_remove();
    _96_init();
    LoadExec(disc_path, 0, 0);
    RestartCallback();
    StartPAD();
}

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
    s_next_packet = grandleon_packet_arena;
    PutDispEnv(&s_display[0]);
    PutDrawEnv(&s_draw[0]);
    SetDispMask(1);
}

#if GRANDLEON_WATER_TEXTURE_INDEX
static uint32_t s_water_words[512];
static unsigned s_water_phase = 32u;
static void update_water_texture(uint32_t frame) {
    TIM_IMAGE tim;
    const unsigned phase = (frame / 8u) & 31u;
    const unsigned index = GRANDLEON_WATER_TEXTURE_INDEX;
    if (phase == s_water_phase || index > g_ps1_texture_count ||
        GetTimInfo(g_ps1_textures[index - 1u].tim, &tim) ||
        (tim.mode & 3u) != 0u || !tim.prect || !tim.paddr ||
        tim.prect->w != 32 || tim.prect->h != 32) return;
    grandleon_water_rows(s_water_words, (const uint32_t*)tim.paddr, phase);
    /* Called after retirement of the preceding command arena. Keep the
     * staging words alive until upload completes, then flush sampled texels. */
    LoadImage(tim.prect, s_water_words);
    DrawSync(0);
    DR_TPAGE flush;
    setlen(&flush, 1);
    flush.code[0] = 0x01000000u;
    DrawPrim((const uint32_t*)&flush);
    DrawSync(0);
    s_water_phase = phase;
}
#else
static void update_water_texture(uint32_t frame) { (void)frame; }
#endif

static void present(void) {
    /* Finish the back image now; expose it only on its display deadline. */
    ps1_render_submit(
        s_ordering_table[0], s_draw[s_buffer].clip.x,
        s_draw[s_buffer].clip.y, SCREEN_WIDTH, SCREEN_HEIGHT, 0
    );
    DrawSync(0);
    s_gpu_ready_tick = (uint16_t)TIMER_VALUE(1);
    s_next_packet = grandleon_packet_arena;
    s_frame_pending = 1;
}

int main(void) {
    uint32_t frame = 0;
    uint32_t measured_frames = 0;
    uint32_t rendered_frames = 0;
    uint32_t scheduled_frames = 0;
    uint32_t deferred_frames = 0;
    uint32_t render_total = 0;
    uint32_t ready_total = 0;
    uint32_t missed_vblanks = 0;
    uint32_t first_missed_frame = 0;
    uint32_t last_missed_frame = 0;
    uint16_t render_max = 0;
    uint16_t ready_max = 0;
    uint16_t mutation_max = 0;
    uint32_t fps_window_start;
    uint32_t fps_frames = 0;
    int last_vblank;
    int next_update_vblank;
    int maximum_elapsed = 1;

    setup_display();
    ps1_render_init(SCREEN_WIDTH, SCREEN_HEIGHT);
    ps1_textures_init();
    InitPAD(s_pad_buffers[0], 34, s_pad_buffers[1], 34);
    StartPAD();
    ChangeClearPAD(0);
    grandleon_game_log_boot(PACKET_BYTES_PER_FRAME);
    TIMER_CTRL(1) = 0x0100;
    TIMER_VALUE(1) = 0;
    last_vblank = VSync(-1);
    fps_window_start = (uint32_t)last_vblank;
    grandleon_game_set_fps(TARGET_RENDER_FPS);
    if (!grandleon_game_init()) {
        grandleon_game_log_boot(0);
    }
    grandleon_audio_init();
    next_update_vblank = VSync(-1) + 1;
    s_last_present_vblank = next_update_vblank - 1;
    fps_window_start = (uint32_t)s_last_present_vblank;

    for (;;) {
        int current_vblank = VSync(-1);
        int fresh_vblank = 0;
        if (current_vblank < next_update_vblank ||
            (s_frame_pending && current_vblank - s_last_present_vblank >= 2)) {
            VSync(0);
            current_vblank = VSync(-1);
            fresh_vblank = 1;
        }
        if (fresh_vblank && s_frame_pending &&
            current_vblank - s_last_present_vblank >= 2) {
            const int held_vblanks = current_vblank - s_last_present_vblank;
            s_buffer ^= 1;
            PutDispEnv(&s_display[s_buffer]);
            PutDrawEnv(&s_draw[s_buffer]);
            SetDispMask(1);
            s_frame_pending = 0;
            s_last_present_vblank = current_vblank;
            ++fps_frames;
            if (frame >= 60) {
                if (held_vblanks > 2) {
                    missed_vblanks += (uint32_t)(held_vblanks - 2);
                    if (!first_missed_frame) first_missed_frame = frame;
                    last_missed_frame = frame;
                }
                if (held_vblanks > maximum_elapsed) maximum_elapsed = held_vblanks;
            }
        }
        ++next_update_vblank;
        const uint16_t frame_start = (uint16_t)TIMER_VALUE(1);
        uint16_t render_start;
        uint16_t render_ticks;
        uint16_t ready_ticks;
        uint16_t pressed[2] = {0, 0};
        int deferred;
        /* Catch up elapsed game ticks before building the next image. A
         * late image must not queue another already-obsolete game state. */
        const int render_due = !s_frame_pending &&
            next_update_vblank > current_vblank;
        unsigned pad_index;
        last_vblank = current_vblank;
        if ((uint32_t)current_vblank - fps_window_start >= 60u) {
            const uint32_t elapsed_vblanks =
                (uint32_t)current_vblank - fps_window_start;
            uint32_t measured_fps = fps_frames * 60u / elapsed_vblanks;
            if (measured_fps > 999u) measured_fps = 999u;
            grandleon_game_set_fps((uint16_t)measured_fps);
            fps_frames = 0;
            fps_window_start = (uint32_t)current_vblank;
        }

        for (pad_index = 0; pad_index < 2; ++pad_index) {
            PADTYPE* pad = (PADTYPE*)s_pad_buffers[pad_index];
            if (!pad->stat && (
                    pad->type == PAD_ID_DIGITAL ||
                    pad->type == PAD_ID_ANALOG_STICK ||
                    pad->type == PAD_ID_ANALOG)) {
                uint16_t buttons = pad->btn;
                if ((pad->type == PAD_ID_ANALOG_STICK || pad->type == PAD_ID_ANALOG) && pad->len >= 3) {
                    if (pad->ls_x < 80) buttons &= (uint16_t)~PAD_LEFT;
                    if (pad->ls_x > 176) buttons &= (uint16_t)~PAD_RIGHT;
                    if (pad->ls_y < 80) buttons &= (uint16_t)~PAD_UP;
                    if (pad->ls_y > 176) buttons &= (uint16_t)~PAD_DOWN;
                }
                pressed[pad_index] = grandleon_game_map_controller((uint16_t)(
                    s_last_buttons[pad_index] & (uint16_t)~buttons
                ));
                s_last_buttons[pad_index] = buttons;
            } else {
                s_last_buttons[pad_index] = 0xffffu;
            }
        }
        deferred = grandleon_runtime_update(pressed[0], pressed[1]);
        grandleon_audio_update();

        if (deferred) {
            const uint16_t mutation_ticks =
                (uint16_t)((uint16_t)TIMER_VALUE(1) - frame_start);
            if (mutation_ticks > mutation_max) mutation_max = mutation_ticks;
            if (deferred > 1) {
                frame = 0;
                measured_frames = 0;
                rendered_frames = 0;
                scheduled_frames = 0;
                deferred_frames = 0;
                render_total = 0;
                ready_total = 0;
                missed_vblanks = 0;
                first_missed_frame = 0;
                last_missed_frame = 0;
                render_max = 0;
                ready_max = 0;
                mutation_max = 0;
                maximum_elapsed = 1;
                last_vblank = VSync(-1);
                next_update_vblank = last_vblank + 1;
                fps_frames = 0;
                fps_window_start = (uint32_t)last_vblank;
                continue;
            }
            ++frame;
            if (frame > 60) {
                ++measured_frames;
                if (render_due) ++deferred_frames;
                else ++scheduled_frames;
            }
            goto report_metrics;
        }

        if (!render_due || s_frame_pending) {
            ++frame;
            if (frame > 60) {
                ++measured_frames;
                if (render_due) ++deferred_frames;
                else ++scheduled_frames;
            }
            goto report_metrics;
        }

        render_start = (uint16_t)TIMER_VALUE(1);
        /* GPU DMA still reads the previous commands until completion. Retire
         * those reads before clearing the shared table or overwriting packets;
         * the front/back VRAM display buffers remain independent. */
        DrawSync(0);
        update_water_texture(frame);
        ClearOTagR(s_ordering_table[0], ORDERING_TABLE_LENGTH);
        render_world_and_ui(
            s_ordering_table[0], &s_next_packet,
            grandleon_packet_arena + PACKET_BYTES_PER_FRAME
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
            ++rendered_frames;
        }

report_metrics:
        if (measured_frames == 600) {
            grandleon_game_log_world_packets(s_world_hits, s_world_misses, s_world_bytes);
            grandleon_game_log_static_packets(
                s_static_packet_hits, s_static_packet_misses,
                s_static_fragments[0].bytes + s_static_fragments[1].bytes + s_static_fragments[2].bytes,
                (unsigned)(s_static_fragments[0].head_count + s_static_fragments[1].head_count + s_static_fragments[2].head_count),
                s_static_packet_rejects, s_world_free_min, s_world_near_limit_frames);
            grandleon_game_log_metrics(
                measured_frames, rendered_frames, scheduled_frames,
                deferred_frames,
                render_total / rendered_frames, render_max,
                ready_total / rendered_frames, ready_max, mutation_max,
                missed_vblanks, maximum_elapsed,
                first_missed_frame, last_missed_frame
            );
            /* Serial telemetry is intentionally outside the frame budget. */
            last_vblank = VSync(-1);
            next_update_vblank = last_vblank + 1;
        }
    }
}

