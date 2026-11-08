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

void sync_scene(const sim::EncounterSnapshot& snapshot) {
    for (unsigned binding = 0; binding < g_grandleon_unit_count; ++binding) {
        ps1_entity* entity = grandleon_scene_mutable_entity(
            g_grandleon_unit_entity_indices[binding]
        );
        const sim::UnitSnapshot* unit = find_unit(snapshot, g_grandleon_unit_ids[binding]);
        if (!entity || !unit) continue;
        entity->mesh_enabled = sim::on_board(*unit) ? 1 : 0;
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

void update_ai() {
    if (!s_ai_active) return;
    sim::EncounterSnapshot snapshot = s_created->encounter.snapshot();
    if (snapshot.active_side != sim::Side::second || snapshot.outcome != sim::Outcome::ongoing) {
        s_ai_active = false;
        return;
    }
    if (s_ai_delay > 0) { --s_ai_delay; return; }

    const sim::UnitSnapshot* actor = nullptr;
    for (const auto& unit : snapshot.units) {
        if (unit.side == sim::Side::second && sim::on_board(unit) && !unit.has_acted) {
            actor = &unit;
            break;
        }
    }
    if (!actor) return;

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
    if (apply_wait(actor->id, "ai_wait", &snapshot)) ++s_ai_wait_count;
    s_ai_delay = 12;
    if (snapshot.active_side == sim::Side::first) {
        s_ai_active = false;
        s_selected = 0;
        psx::Line().text("MIPSYNC_SIM_PROOF select=").decimal(s_select_count)
            .text(" move=").decimal(s_move_count).text(" wait=")
            .decimal(s_wait_count).text(" ai_wait=").decimal(s_ai_wait_count)
            .text(" hash=").hex64(s_created->encounter.canonical_hash()).flush();
    }
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
    uint32_t frames, uint32_t render_average, uint16_t render_maximum,
    uint32_t ready_average, uint16_t ready_maximum, uint32_t missed_vblanks,
    int maximum_elapsed
) {
    psx::Line().text("MIPSYNC_GRANDLEON_METRICS frames=").decimal(frames)
        .text(" render_hbl_avg=").decimal(render_average)
        .text(" render_hbl_max=").decimal(render_maximum)
        .text(" ready_hbl_avg=").decimal(ready_average)
        .text(" ready_hbl_max=").decimal(ready_maximum)
        .text(" missed_vblanks=").decimal(missed_vblanks)
        .text(" max_elapsed_vblanks=").signed_decimal(maximum_elapsed).flush();
}

void grandleon_game_update(uint16_t pressed) {
    if (!s_created || !*s_created) return;
    if (s_ai_active) {
        update_ai();
        return;
    }
    if (pressed == 0) return;
    sim::EncounterSnapshot snapshot = s_created->encounter.snapshot();
    if (snapshot.active_side != sim::Side::first) return;

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
                    snapshot = s_created->encounter.snapshot();
                    sync_scene(snapshot);
                }
            }
        }
    }
    if ((pressed & pad_square) && s_selected != 0) {
        if (apply_wait(s_selected, "wait", &snapshot)) {
            ++s_wait_count;
            s_selected = 0;
        }
        s_ai_active = snapshot.active_side == sim::Side::second;
    }
    ps1_entity* cursor = grandleon_scene_mutable_entity(g_grandleon_cursor_entity_index);
    if (cursor) place_entity(*cursor, s_cursor, snapshot.width, snapshot.height);
}
