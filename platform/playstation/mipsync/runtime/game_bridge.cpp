// SPDX-License-Identifier: MIT
#include "game_bridge.h"

#include "generated/grandleon_scene.h"
#include "generated/grandleon_simulation.hpp"

#include <grandleon/simulation/encounter.hpp>
#include "psx_runtime.h"
#include <cstdint>
#include <new>

namespace sim = grandleon::simulation;
namespace psx = grandleon::playstation;

namespace {

sim::Encounter::CreateResult* s_created{};
sim::UnitId s_selected{};
sim::Position s_cursor{};
unsigned s_ai_delay{};
bool s_ai_active{};
unsigned s_select_count{};
unsigned s_move_count{};
unsigned s_wait_count{};
unsigned s_ai_wait_count{};
std::int32_t s_camera_min_x{};
std::int32_t s_camera_max_x{};
std::int32_t s_camera_min_z{};
std::int32_t s_camera_max_z{};
std::int32_t s_map_width{};
std::int32_t s_map_height{};
unsigned s_active_terrain_chunks{};
bool s_unit_on_board[64]{};
unsigned s_camera_settle_frames{};
unsigned s_active_terrain_page{0xffffffffU};
constexpr std::uint16_t pad_up = 1U << 4U;
constexpr std::uint16_t pad_right = 1U << 5U;
constexpr std::uint16_t pad_down = 1U << 6U;
constexpr std::uint16_t pad_left = 1U << 7U;
constexpr std::uint16_t pad_circle = 1U << 13U;
constexpr std::uint16_t pad_cross = 1U << 14U;
constexpr std::uint16_t pad_square = 1U << 15U;

const sim::UnitSnapshot* find_unit(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId id
) {
    for (const auto& unit : snapshot.units) if (unit.id == id) return &unit;
    return nullptr;
}

const sim::UnitSnapshot* unit_at(
    const sim::EncounterSnapshot& snapshot,
    sim::Position position,
    sim::Side side
) {
    for (const auto& unit : snapshot.units) {
        if (unit.side == side && sim::on_board(unit) && unit.position == position)
            return &unit;
    }
    return nullptr;
}

void place_entity(ps1_entity& entity, sim::Position position, int width, int height) {
    entity.position[0] = (static_cast<std::int32_t>(position.x) * 2 + 1 - width) * 65536;
    entity.position[2] = (height - static_cast<std::int32_t>(position.y) * 2 - 1) * 65536;
}

std::int32_t clamp_camera_focus(
    std::int32_t world, std::int32_t extent, std::int32_t visible
) {
    if (extent <= visible) return 0;
    const std::int32_t world_min = 1 - extent;
    const std::int32_t world_max = extent - 1;
    const std::int32_t margin = visible - 1;
    const std::int32_t minimum = world_min + margin;
    const std::int32_t maximum = world_max - margin;
    if (world < minimum) return minimum;
    if (world > maximum) return maximum;
    return world;
}

std::int32_t approach(std::int32_t current, std::int32_t target) {
    const std::int32_t delta = target - current;
    if (delta > -1024 && delta < 1024) return target;
    return current + delta / 4;
}

void tick_camera() {
    ps1_entity* camera = grandleon_scene_mutable_entity(
        g_grandleon_camera_entity_index
    );
    if (!camera) return;
    const std::int32_t cursor_world_x =
        static_cast<std::int32_t>(s_cursor.x) * 2 + 1 - s_map_width;
    const std::int32_t cursor_world_z =
        s_map_height - static_cast<std::int32_t>(s_cursor.y) * 2 - 1;
    const std::int32_t focus_x = clamp_camera_focus(
        cursor_world_x, s_map_width, g_grandleon_camera_view_tiles_x
    );
    const std::int32_t focus_z = clamp_camera_focus(
        cursor_world_z, s_map_height, g_grandleon_camera_view_tiles_y
    );
    const std::int32_t previous_camera_x = camera->position[0];
    const std::int32_t previous_camera_z = camera->position[2];
    camera->position[0] = approach(camera->position[0], focus_x * 65536);
    camera->position[1] = g_grandleon_camera_height;
    camera->position[2] = approach(
        camera->position[2], focus_z * 65536 +
            g_grandleon_camera_trailing_distance
    );
    const bool camera_moved =
        camera->position[0] != previous_camera_x ||
        camera->position[2] != previous_camera_z;
    if (camera_moved) s_camera_settle_frames = 0;
    else if (s_camera_settle_frames < 3) ++s_camera_settle_frames;
    const std::int32_t camera_x = camera->position[0] / 65536;
    const std::int32_t camera_z = camera->position[2] / 65536;
    const std::int32_t camera_focus_z =
        (camera->position[2] - g_grandleon_camera_trailing_distance) / 65536;
    const std::int32_t half_x = g_grandleon_camera_view_tiles_x;
    const std::int32_t half_z = g_grandleon_camera_view_tiles_y;
    unsigned closest_page = 0;
    std::uint32_t closest_distance = 0xffffffffU;
    for (unsigned index = 0; index < g_grandleon_terrain_chunk_count; ++index) {
        const std::int32_t dx =
            camera_x - g_grandleon_terrain_page_focus[index][0];
        const std::int32_t dz =
            camera_focus_z - g_grandleon_terrain_page_focus[index][1];
        const std::uint32_t page_distance =
            static_cast<std::uint32_t>(dx * dx + dz * dz);
        if (page_distance < closest_distance) {
            closest_distance = page_distance;
            closest_page = index;
        }
    }
    const bool terrain_page_changed =
        g_grandleon_terrain_chunk_count && closest_page != s_active_terrain_page;
    s_active_terrain_page = closest_page;
    s_active_terrain_chunks = g_grandleon_terrain_chunk_count ? 1U : 0U;
    for (unsigned index = 0; index < g_grandleon_terrain_chunk_count; ++index) {
        ps1_entity* chunk = grandleon_scene_mutable_entity(
            g_grandleon_terrain_entity_indices[index]
        );
        if (!chunk) continue;
        chunk->mesh_enabled = index == closest_page;
    }
    const std::int32_t prop_half_x = half_x;
    const std::int32_t prop_half_z = half_z;
    for (unsigned index = 0; index < g_grandleon_tree_count; ++index) {
        ps1_entity* tree = grandleon_scene_mutable_entity(
            g_grandleon_tree_entity_indices[index]
        );
        if (!tree) continue;
        const std::int32_t tree_x = tree->position[0] / 65536;
        const std::int32_t tree_z = tree->position[2] / 65536;
        tree->mesh_enabled = s_camera_settle_frames >= 2 &&
            tree_x >= camera_x - prop_half_x &&
            tree_x <= camera_x + prop_half_x &&
            tree_z >= camera_focus_z - prop_half_z &&
            tree_z <= camera_focus_z + prop_half_z;
    }
    for (unsigned index = 0; index < g_grandleon_unit_count; ++index) {
        ps1_entity* unit = grandleon_scene_mutable_entity(
            g_grandleon_unit_entity_indices[index]
        );
        if (!unit) continue;
        const std::int32_t unit_x = unit->position[0] / 65536;
        const std::int32_t unit_z = unit->position[2] / 65536;
        /* A newly paged terrain mesh gets one uncluttered cache-fill frame.
         * The cursor remains visible, and units return on the following frame;
         * no rules or input are deferred. */
        unit->mesh_enabled = !terrain_page_changed && s_unit_on_board[index] &&
            unit_x >= camera_x - prop_half_x &&
            unit_x <= camera_x + prop_half_x &&
            unit_z >= camera_focus_z - prop_half_z &&
            unit_z <= camera_focus_z + prop_half_z;
    }
    if (camera_x < s_camera_min_x) s_camera_min_x = camera_x;
    if (camera_x > s_camera_max_x) s_camera_max_x = camera_x;
    if (camera_z < s_camera_min_z) s_camera_min_z = camera_z;
    if (camera_z > s_camera_max_z) s_camera_max_z = camera_z;
}

void sync_scene(const sim::EncounterSnapshot& snapshot) {
    for (unsigned binding = 0; binding < g_grandleon_unit_count; ++binding) {
        ps1_entity* entity = grandleon_scene_mutable_entity(
            g_grandleon_unit_entity_indices[binding]
        );
        const sim::UnitSnapshot* unit = find_unit(snapshot, g_grandleon_unit_ids[binding]);
        if (!entity || !unit) continue;
        s_unit_on_board[binding] = sim::on_board(*unit);
        entity->mesh_enabled = s_unit_on_board[binding] ? 1 : 0;
        place_entity(*entity, unit->position, snapshot.width, snapshot.height);
    }
    ps1_entity* cursor = grandleon_scene_mutable_entity(g_grandleon_cursor_entity_index);
    if (cursor) place_entity(*cursor, s_cursor, snapshot.width, snapshot.height);
}

bool apply_wait(
    sim::UnitId id, const char* action, sim::EncounterSnapshot* after
) {
    sim::Command command{};
    command.type = sim::CommandType::wait;
    command.unit_id = id;
    const sim::CommandResult result = s_created->encounter.apply(command);
    if (!result) {
        psx::Line().text("MIPSYNC_SIM_REJECT action=").text(action)
            .text(" error=").decimal(static_cast<unsigned>(result.error)).flush();
        return false;
    }
    const sim::EncounterSnapshot snapshot = s_created->encounter.snapshot();
    sync_scene(snapshot);
    if (after) *after = snapshot;
    return true;
}

std::uint32_t distance(sim::Position a, sim::Position b) {
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return static_cast<std::uint32_t>(dx + dy);
}

bool update_ai() {
    if (!s_ai_active) return false;
    sim::EncounterSnapshot snapshot = s_created->encounter.snapshot();
    if (snapshot.active_side != sim::Side::second || snapshot.outcome != sim::Outcome::ongoing) {
        s_ai_active = false;
        return false;
    }
    if (s_ai_delay > 0) { --s_ai_delay; return false; }

    const sim::UnitSnapshot* actor = nullptr;
    for (const auto& unit : snapshot.units) {
        if (unit.side == sim::Side::second && sim::on_board(unit) && !unit.has_acted) {
            actor = &unit;
            break;
        }
    }
    if (!actor) return false;

    sim::Position destination = actor->position;
    std::uint32_t best = 0xffffffffU;
    const auto reachable = sim::reachable_tiles(snapshot, actor->id);
    for (const auto tile : reachable) {
        for (const auto& target : snapshot.units) {
            if (target.side != sim::Side::first || !sim::on_board(target)) continue;
            const std::uint32_t candidate = distance(tile, target.position);
            if (candidate < best) { best = candidate; destination = tile; }
        }
    }
    if (!(destination == actor->position)) {
        sim::Command move{};
        move.type = sim::CommandType::move;
        move.unit_id = actor->id;
        move.destination = destination;
        const sim::CommandResult moved = s_created->encounter.apply(move);
        if (!moved) {
            psx::Line().text("MIPSYNC_SIM_REJECT action=ai_move error=")
                .decimal(static_cast<unsigned>(moved.error)).flush();
        }
    }
    const bool waited = apply_wait(actor->id, "ai_wait", &snapshot);
    if (waited) ++s_ai_wait_count;
    s_ai_delay = 12;
    if (snapshot.active_side == sim::Side::first) {
        s_ai_active = false;
        s_selected = 0;
        psx::Line().text("MIPSYNC_SIM_PROOF select=").decimal(s_select_count)
            .text(" move=").decimal(s_move_count).text(" wait=")
            .decimal(s_wait_count).text(" ai_wait=").decimal(s_ai_wait_count)
            .text(" hash=").hex64(s_created->encounter.canonical_hash()).flush();
    }
    return waited;
}

}  // namespace

int grandleon_game_init(void) {
    s_created = new (std::nothrow) sim::Encounter::CreateResult(
        sim::create_encounter(grandleon_generated_definition())
    );
    if (!s_created || !*s_created) {
        psx::Line().text("MIPSYNC_SIM_INIT_FAIL error=")
            .decimal(s_created ? static_cast<unsigned>(s_created->error) : 255U)
            .flush();
        return 0;
    }
    const sim::EncounterSnapshot snapshot = s_created->encounter.snapshot();
    s_map_width = snapshot.width;
    s_map_height = snapshot.height;
    if (s_created->encounter.canonical_hash() != grandleon_generated_canonical_hash) {
        psx::Line().text("MIPSYNC_SIM_HASH_FAIL expected=")
            .hex64(grandleon_generated_canonical_hash).text(" actual=")
            .hex64(s_created->encounter.canonical_hash()).flush();
        return 0;
    }
    for (const auto& unit : snapshot.units) {
        if (unit.side == sim::Side::first && sim::on_board(unit)) {
            s_cursor = unit.position;
            break;
        }
    }
    sync_scene(snapshot);
    ps1_entity* camera = grandleon_scene_mutable_entity(
        g_grandleon_camera_entity_index
    );
    if (camera) {
        s_camera_min_x = s_camera_max_x = camera->position[0] / 65536;
        s_camera_min_z = s_camera_max_z = camera->position[2] / 65536;
    }
    tick_camera();
    s_ai_active = snapshot.active_side == sim::Side::second;
    psx::Line().text("MIPSYNC_SIM_READY width=").decimal(snapshot.width)
        .text(" height=").decimal(snapshot.height).text(" units=")
        .decimal(static_cast<unsigned>(snapshot.units.size())).text(" hash=")
        .hex64(s_created->encounter.canonical_hash()).flush();
    return 1;
}

extern "C" void grandleon_game_log_boot(unsigned int packet_bytes) {
    psx::Line line;
    line.text("GRANDLEON_PS1_RENDERER=MIPSYNC packet_bytes=")
        .decimal(packet_bytes).flush();
}

extern "C" void grandleon_game_log_metrics(
    uint32_t frames, uint32_t rendered_frames, uint32_t deferred_frames,
    uint32_t render_average, uint16_t render_maximum,
    uint32_t ready_average, uint16_t ready_maximum,
    uint16_t mutation_maximum, uint32_t missed_vblanks, int maximum_elapsed
) {
    psx::Line().text("MIPSYNC_GRANDLEON_METRICS frames=").decimal(frames)
        .text(" rendered_frames=").decimal(rendered_frames)
        .text(" deferred_frames=").decimal(deferred_frames)
        .text(" render_hbl_avg=").decimal(render_average)
        .text(" render_hbl_max=").decimal(render_maximum)
        .text(" ready_hbl_avg=").decimal(ready_average)
        .text(" ready_hbl_max=").decimal(ready_maximum)
        .text(" mutation_hbl_max=").decimal(mutation_maximum)
        .text(" missed_vblanks=").decimal(missed_vblanks)
        .text(" max_elapsed_vblanks=").signed_decimal(maximum_elapsed).flush();
    ps1_entity* camera = grandleon_scene_mutable_entity(
        g_grandleon_camera_entity_index
    );
    if (camera) {
        psx::Line().text("MIPSYNC_CAMERA_PROOF min_x=")
            .signed_decimal(s_camera_min_x).text(" max_x=")
            .signed_decimal(s_camera_max_x).text(" min_z=")
            .signed_decimal(s_camera_min_z).text(" max_z=")
            .signed_decimal(s_camera_max_z).text(" current_x=")
            .signed_decimal(camera->position[0] / 65536).text(" current_z=")
            .signed_decimal(camera->position[2] / 65536).text(" chunks=")
            .decimal(s_active_terrain_chunks).flush();
    }
}

int grandleon_game_update(uint16_t pressed) {
    if (!s_created || !*s_created) return 0;
    tick_camera();
    if (s_ai_active) {
        return update_ai() ? 1 : 0;
    }
    if (pressed == 0) return 0;
    sim::EncounterSnapshot snapshot = s_created->encounter.snapshot();
    if (snapshot.active_side != sim::Side::first) return 0;
    bool mutated = false;

    if (pressed & pad_left) --s_cursor.x;
    if (pressed & pad_right) ++s_cursor.x;
    if (pressed & pad_up) --s_cursor.y;
    if (pressed & pad_down) ++s_cursor.y;
    if (s_cursor.x < 0) s_cursor.x = 0;
    if (s_cursor.y < 0) s_cursor.y = 0;
    if (s_cursor.x >= snapshot.width) s_cursor.x = snapshot.width - 1;
    if (s_cursor.y >= snapshot.height) s_cursor.y = snapshot.height - 1;

    if (pressed & pad_circle) s_selected = 0;
    if (pressed & pad_cross) {
        if (s_selected == 0) {
            const sim::UnitSnapshot* unit = unit_at(snapshot, s_cursor, sim::Side::first);
            if (unit && !unit->has_acted) {
                s_selected = unit->id;
                ++s_select_count;
            }
        } else {
            const auto reachable = sim::reachable_tiles(snapshot, s_selected);
            bool allowed = false;
            for (const auto tile : reachable) if (tile == s_cursor) allowed = true;
            if (allowed) {
                sim::Command command{};
                command.type = sim::CommandType::move;
                command.unit_id = s_selected;
                command.destination = s_cursor;
                const sim::CommandResult result = s_created->encounter.apply(command);
                if (result) {
                    ++s_move_count;
                    mutated = true;
                    snapshot = s_created->encounter.snapshot();
                    sync_scene(snapshot);
                }
            }
        }
    }
    if ((pressed & pad_square) && s_selected != 0) {
        if (apply_wait(s_selected, "wait", &snapshot)) {
            ++s_wait_count;
            mutated = true;
            s_selected = 0;
        }
        s_ai_active = snapshot.active_side == sim::Side::second;
    }
    ps1_entity* cursor = grandleon_scene_mutable_entity(g_grandleon_cursor_entity_index);
    if (cursor) place_entity(*cursor, s_cursor, snapshot.width, snapshot.height);
    return mutated ? 1 : 0;
}
