// SPDX-License-Identifier: MIT
#include "game_bridge.h"

#include "generated/grandleon_scene.h"
#include "generated/grandleon_simulation.hpp"

#include <grandleon/simulation/encounter.hpp>
#include <grandleon/tactics/policy.hpp>
#include "psx_runtime.h"
#include <cstdint>
#include <new>

namespace sim = grandleon::simulation;
namespace tac = grandleon::tactics;
namespace psx = grandleon::playstation;

namespace {

enum class InputMode : std::uint8_t { browse, move, action, attack, ability };

sim::Encounter::CreateResult* s_created{};
sim::EncounterSnapshot s_snapshot{};
sim::UnitId s_selected{};
sim::Position s_cursor{};
InputMode s_mode{InputMode::browse};
unsigned s_menu_index{};
unsigned s_ai_delay{};
bool s_ai_active{};
bool s_close_combat_enabled{true};
unsigned s_result_frames{};
unsigned s_result_duration{};
unsigned s_camera_return_frames{};
sim::UnitId s_combat_attacker{};
sim::UnitId s_combat_target{};
sim::Position s_combat_centre{};
bool s_combat_counter{};
bool s_last_result_missed{};
bool s_last_result_restored{};
std::int16_t s_last_result_damage{};
std::int16_t s_last_result_counter_damage{};
unsigned s_select_count{};
unsigned s_move_count{};
unsigned s_wait_count{};
unsigned s_attack_count{};
unsigned s_ability_count{};
unsigned s_player_attack_count{};
unsigned s_player_ability_count{};
unsigned s_ai_command_count{};
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
constexpr std::uint16_t pad_triangle = 1U << 12U;
constexpr std::uint16_t pad_circle = 1U << 13U;
constexpr std::uint16_t pad_cross = 1U << 14U;
constexpr unsigned close_combat_frames = 120;
constexpr unsigned result_only_frames = 50;

const sim::UnitSnapshot* find_unit(
    const sim::EncounterSnapshot& snapshot, sim::UnitId id
) {
    for (const auto& unit : snapshot.units) if (unit.id == id) return &unit;
    return nullptr;
}

const sim::UnitSnapshot* unit_at(
    const sim::EncounterSnapshot& snapshot, sim::Position position
) {
    for (const auto& unit : snapshot.units) {
        if (sim::on_board(unit) && unit.position == position) return &unit;
    }
    return nullptr;
}

void place_entity(ps1_entity& entity, sim::Position position, int width, int height) {
    entity.position[0] =
        (static_cast<std::int32_t>(position.x) * 2 + 1 - width) * 65536;
    entity.position[2] =
        (height - static_cast<std::int32_t>(position.y) * 2 - 1) * 65536;
}

std::int32_t clamp_camera_focus(
    std::int32_t world, std::int32_t extent, std::int32_t visible
) {
    if (extent <= visible) return 0;
    const std::int32_t minimum = 1 - extent + visible - 1;
    const std::int32_t maximum = extent - 1 - visible + 1;
    if (world < minimum) return minimum;
    if (world > maximum) return maximum;
    return world;
}

std::int32_t approach(std::int32_t current, std::int32_t target) {
    const std::int32_t delta = target - current;
    if (delta > -1024 && delta < 1024) return target;
    return current + delta / 4;
}

void animate_combat_units(const sim::EncounterSnapshot& snapshot) {
    if (!s_result_frames || !s_close_combat_enabled) return;
    const sim::UnitSnapshot* attacker = find_unit(snapshot, s_combat_attacker);
    const sim::UnitSnapshot* target = find_unit(snapshot, s_combat_target);
    if (!attacker || !target) return;
    ps1_entity* attacker_entity = nullptr;
    ps1_entity* target_entity = nullptr;
    for (unsigned index = 0; index < g_grandleon_unit_count; ++index) {
        if (g_grandleon_unit_ids[index] == attacker->id) {
            attacker_entity = grandleon_scene_mutable_entity(
                g_grandleon_unit_entity_indices[index]
            );
        }
        if (g_grandleon_unit_ids[index] == target->id) {
            target_entity = grandleon_scene_mutable_entity(
                g_grandleon_unit_entity_indices[index]
            );
        }
    }
    if (!attacker_entity || !target_entity) return;
    place_entity(*attacker_entity, attacker->position, snapshot.width, snapshot.height);
    place_entity(*target_entity, target->position, snapshot.width, snapshot.height);
    const unsigned elapsed = s_result_duration - s_result_frames;
    unsigned phase = 0;
    bool counter = false;
    if (elapsed >= 28 && elapsed < 46) {
        phase = elapsed - 28;
    } else if (s_combat_counter && elapsed >= 68 && elapsed < 86) {
        phase = elapsed - 68;
        counter = true;
    }
    if (phase >= 9) phase = 18 - phase;
    if (phase == 0 || phase > 9) return;
    ps1_entity* mover = counter ? target_entity : attacker_entity;
    const ps1_entity* destination = counter ? attacker_entity : target_entity;
    mover->position[0] +=
        (destination->position[0] - mover->position[0]) * static_cast<int>(phase) / 27;
    mover->position[2] +=
        (destination->position[2] - mover->position[2]) * static_cast<int>(phase) / 27;
}

void tick_camera() {
    ps1_entity* camera = grandleon_scene_mutable_entity(
        g_grandleon_camera_entity_index
    );
    if (!camera || !s_created || !*s_created) return;
    const sim::EncounterSnapshot& snapshot = s_snapshot;
    std::int32_t focus_x;
    std::int32_t focus_z;
    std::int32_t desired_height = g_grandleon_camera_height;
    std::int32_t desired_trailing = g_grandleon_camera_trailing_distance;
    const bool close_combat =
        (s_result_frames || s_camera_return_frames) && s_close_combat_enabled;
    if (s_result_frames && s_close_combat_enabled) {
        const sim::UnitSnapshot* attacker = find_unit(snapshot, s_combat_attacker);
        const sim::UnitSnapshot* target = find_unit(snapshot, s_combat_target);
        const sim::Position a = attacker ? attacker->position : s_combat_centre;
        const sim::Position b = target ? target->position : s_combat_centre;
        focus_x = ((a.x * 2 + 1 - s_map_width) +
                   (b.x * 2 + 1 - s_map_width)) * 65536 / 2;
        focus_z = ((s_map_height - a.y * 2 - 1) +
                   (s_map_height - b.y * 2 - 1)) * 65536 / 2;
        desired_height = 6 * 65536;
        desired_trailing = 8 * 65536;
    } else {
        focus_x = clamp_camera_focus(
            s_cursor.x * 2 + 1 - s_map_width,
            s_map_width, g_grandleon_camera_view_tiles_x
        ) * 65536;
        focus_z = clamp_camera_focus(
            s_map_height - s_cursor.y * 2 - 1,
            s_map_height, g_grandleon_camera_view_tiles_y
        ) * 65536;
    }
    const std::int32_t previous_camera_x = camera->position[0];
    const std::int32_t previous_camera_z = camera->position[2];
    camera->position[0] = approach(camera->position[0], focus_x);
    camera->position[1] = approach(camera->position[1], desired_height);
    camera->position[2] = approach(camera->position[2], focus_z + desired_trailing);
    const bool camera_moved =
        camera->position[0] != previous_camera_x ||
        camera->position[2] != previous_camera_z;
    if (camera_moved) s_camera_settle_frames = 0;
    else if (s_camera_settle_frames < 8) ++s_camera_settle_frames;
    const std::int32_t camera_x = camera->position[0] / 65536;
    const std::int32_t camera_z = camera->position[2] / 65536;
    const std::int32_t camera_focus_z =
        (camera->position[2] - desired_trailing) / 65536;
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
        if (chunk) chunk->mesh_enabled = !close_combat && index == closest_page;
    }
    ps1_entity* combat_floor = grandleon_scene_mutable_entity(
        g_grandleon_combat_floor_entity_index
    );
    if (combat_floor) {
        combat_floor->mesh_enabled = close_combat ? 1 : 0;
        combat_floor->position[0] = focus_x;
        combat_floor->position[1] = -1024;
        combat_floor->position[2] = focus_z;
    }
    ps1_entity* cursor = grandleon_scene_mutable_entity(
        g_grandleon_cursor_entity_index
    );
    if (cursor) cursor->mesh_enabled = close_combat ? 0 : 1;
    const std::int32_t half_x = g_grandleon_camera_view_tiles_x;
    const std::int32_t half_z = g_grandleon_camera_view_tiles_y;
    for (unsigned index = 0; index < g_grandleon_tree_count; ++index) {
        ps1_entity* tree = grandleon_scene_mutable_entity(
            g_grandleon_tree_entity_indices[index]
        );
        if (!tree) continue;
        const std::int32_t tree_x = tree->position[0] / 65536;
        const std::int32_t tree_z = tree->position[2] / 65536;
        tree->mesh_enabled = !close_combat && s_camera_settle_frames >= 2 &&
            tree_x >= camera_x - half_x && tree_x <= camera_x + half_x &&
            tree_z >= camera_focus_z - half_z && tree_z <= camera_focus_z + half_z;
    }
    animate_combat_units(snapshot);
    for (unsigned index = 0; index < g_grandleon_unit_count; ++index) {
        ps1_entity* unit = grandleon_scene_mutable_entity(
            g_grandleon_unit_entity_indices[index]
        );
        if (!unit) continue;
        const std::int32_t unit_x = unit->position[0] / 65536;
        const std::int32_t unit_z = unit->position[2] / 65536;
        const bool combat_actor = close_combat &&
            (g_grandleon_unit_ids[index] == s_combat_attacker ||
             g_grandleon_unit_ids[index] == s_combat_target);
        unit->mesh_enabled = close_combat ? combat_actor :
            (!terrain_page_changed && s_unit_on_board[index] &&
             unit_x >= camera_x - half_x && unit_x <= camera_x + half_x &&
             unit_z >= camera_focus_z - half_z && unit_z <= camera_focus_z + half_z);
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
        const sim::UnitSnapshot* unit = find_unit(
            snapshot, g_grandleon_unit_ids[binding]
        );
        if (!entity || !unit) continue;
        s_unit_on_board[binding] = sim::on_board(*unit);
        entity->mesh_enabled = s_unit_on_board[binding] ? 1 : 0;
        place_entity(*entity, unit->position, snapshot.width, snapshot.height);
    }
    ps1_entity* cursor = grandleon_scene_mutable_entity(
        g_grandleon_cursor_entity_index
    );
    if (cursor) place_entity(*cursor, s_cursor, snapshot.width, snapshot.height);
}

bool apply_command(
    const sim::Command& command, const char* action,
    sim::CommandResult* accepted, sim::EncounterSnapshot* after
) {
    sim::CommandResult result = s_created->encounter.apply(command);
    if (!result) {
        psx::Line().text("MIPSYNC_SIM_REJECT action=").text(action)
            .text(" error=").decimal(static_cast<unsigned>(result.error)).flush();
        return false;
    }
    s_snapshot = s_created->encounter.snapshot();
    sync_scene(s_snapshot);
    if (accepted) *accepted = result;
    if (after) *after = s_snapshot;
    return true;
}

bool apply_wait(sim::UnitId id, const char* action, sim::EncounterSnapshot* after) {
    sim::Command command{};
    command.type = sim::CommandType::wait;
    command.unit_id = id;
    return apply_command(command, action, nullptr, after);
}

const grandleon_generated_behavior* behavior_for(sim::UnitId id) {
    for (const auto& binding : grandleon_generated_behaviors()) {
        if (binding.unit_id == id) return &binding;
    }
    return nullptr;
}

void begin_result(
    const sim::Command& command, const sim::CommandResult& result,
    bool counter, sim::Position centre
) {
    s_combat_attacker = command.unit_id;
    s_combat_target = command.target_id;
    s_combat_centre = centre;
    s_combat_counter = counter;
    s_last_result_missed = false;
    s_last_result_restored = false;
    s_last_result_damage = 0;
    s_last_result_counter_damage = 0;
    for (const sim::Event& event : result.events) {
        if (event.type == sim::EventType::attack_missed &&
            event.related_unit_id == command.unit_id) {
            s_last_result_missed = true;
        } else if (event.type == sim::EventType::unit_damaged) {
            if (event.unit_id == command.unit_id) {
                s_last_result_counter_damage += event.amount;
            } else {
                s_last_result_damage += event.amount;
                if (s_combat_target == 0) s_combat_target = event.unit_id;
            }
        } else if (event.type == sim::EventType::unit_restored) {
            s_last_result_restored = true;
            s_last_result_damage += event.amount;
            if (s_combat_target == 0) s_combat_target = event.unit_id;
        }
    }
    s_result_duration = s_close_combat_enabled ? close_combat_frames : result_only_frames;
    s_result_frames = s_result_duration;
    s_camera_return_frames = 0;
}

void finish_side_if_needed(const sim::EncounterSnapshot& snapshot) {
    s_ai_active = snapshot.active_side == sim::Side::second &&
        snapshot.outcome == sim::Outcome::ongoing;
    if (snapshot.outcome != sim::Outcome::ongoing) {
        s_ai_active = false;
        s_selected = 0;
        s_mode = InputMode::browse;
    }
}

bool update_ai() {
    if (!s_ai_active) return false;
    if (s_snapshot.active_side != sim::Side::second ||
        s_snapshot.outcome != sim::Outcome::ongoing) {
        s_ai_active = false;
        return false;
    }
    if (s_ai_delay > 0) { --s_ai_delay; return false; }
    sim::EncounterSnapshot snapshot = s_snapshot;
    const sim::UnitSnapshot* actor = snapshot.active_unit_id != 0 ?
        find_unit(snapshot, snapshot.active_unit_id) : nullptr;
    if (!actor) {
        for (const auto& unit : snapshot.units) {
            if (unit.side == sim::Side::second && sim::on_board(unit) &&
                !unit.has_acted) {
                actor = &unit;
                break;
            }
        }
    }
    if (!actor) return false;
    const grandleon_generated_behavior* binding = behavior_for(actor->id);
    const tac::Behavior behavior = binding ? binding->behavior : tac::Behavior::hold;
    static const std::vector<sim::Position> no_patrol;
    const std::vector<sim::Position>& patrol = binding ? binding->patrol : no_patrol;
    tac::Plan plan = tac::decide(
        snapshot, actor->id, behavior, patrol,
        s_created->encounter.abilities(), s_created->encounter.weapons()
    );
    if (!plan.actionable) {
        plan.actionable = true;
        plan.command.type = sim::CommandType::wait;
        plan.command.unit_id = actor->id;
    }
    sim::CommandResult result;
    const char* action = plan.command.type == sim::CommandType::move ? "ai_move" :
        plan.command.type == sim::CommandType::attack ? "ai_attack" :
        plan.command.type == sim::CommandType::ability ? "ai_ability" : "ai_wait";
    bool attack_counter = false;
    if (plan.command.type == sim::CommandType::attack) {
        const sim::AttackForecast forecast = sim::forecast_attack(
            snapshot, actor->id, plan.command.target_id,
            s_created->encounter.weapons(), plan.command.weapon_id
        );
        attack_counter = forecast && forecast.counter;
    }
    if (!apply_command(plan.command, action, &result, &snapshot)) {
        if (!apply_wait(actor->id, "ai_fallback_wait", &snapshot)) return false;
    } else if (plan.command.type == sim::CommandType::attack) {
        ++s_attack_count;
        begin_result(plan.command, result, attack_counter, actor->position);
    } else if (plan.command.type == sim::CommandType::ability) {
        ++s_ability_count;
        begin_result(plan.command, result, false, plan.command.destination);
    }
    ++s_ai_command_count;
    s_cursor = actor->position;
    s_ai_delay = 10;
    finish_side_if_needed(snapshot);
    if (!s_ai_active) {
        s_selected = 0;
        s_mode = InputMode::browse;
        psx::Line().text("MIPSYNC_SIM_PROOF select=").decimal(s_select_count)
            .text(" move=").decimal(s_move_count).text(" wait=")
            .decimal(s_wait_count).text(" attack=").decimal(s_attack_count)
            .text(" ability=").decimal(s_ability_count).text(" p_attack=")
            .decimal(s_player_attack_count).text(" p_ability=")
            .decimal(s_player_ability_count).text(" ai=")
            .decimal(s_ai_command_count).text(" hash=")
            .hex64(s_created->encounter.canonical_hash()).flush();
    }
    return true;
}

void move_cursor(std::uint16_t pressed, const sim::EncounterSnapshot& snapshot) {
    if (pressed & pad_left) --s_cursor.x;
    if (pressed & pad_right) ++s_cursor.x;
    if (pressed & pad_up) --s_cursor.y;
    if (pressed & pad_down) ++s_cursor.y;
    if (s_cursor.x < 0) s_cursor.x = 0;
    if (s_cursor.y < 0) s_cursor.y = 0;
    if (s_cursor.x >= snapshot.width) s_cursor.x = snapshot.width - 1;
    if (s_cursor.y >= snapshot.height) s_cursor.y = snapshot.height - 1;
}

unsigned action_count(const sim::UnitSnapshot* actor) {
    return actor ? static_cast<unsigned>(actor->ability_ids.size()) + 2U : 0U;
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
    s_snapshot = s_created->encounter.snapshot();
    const sim::EncounterSnapshot& snapshot = s_snapshot;
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
        .decimal(static_cast<unsigned>(snapshot.units.size())).text(" cursor_x=")
        .signed_decimal(s_cursor.x).text(" cursor_y=").signed_decimal(s_cursor.y)
        .text(" hash=")
        .hex64(s_created->encounter.canonical_hash()).flush();
    return 1;
}

extern "C" void grandleon_game_log_boot(unsigned int packet_bytes) {
    psx::Line().text("GRANDLEON_PS1_RENDERER=MIPSYNC packet_bytes=")
        .decimal(packet_bytes).flush();
}

extern "C" void grandleon_game_log_metrics(
    uint32_t frames, uint32_t rendered_frames, uint32_t deferred_frames,
    uint32_t render_average, uint16_t render_maximum,
    uint32_t ready_average, uint16_t ready_maximum,
    uint16_t mutation_maximum, uint32_t missed_vblanks, int maximum_elapsed,
    uint32_t first_missed_frame, uint32_t last_missed_frame
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
    psx::Line().text("MIPSYNC_MISS_RANGE first=").decimal(first_missed_frame)
        .text(" last=").decimal(last_missed_frame).flush();
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

extern "C" void grandleon_game_ui_state(grandleon_ui_state* output) {
    if (!output) return;
    *output = {};
    output->close_combat = s_close_combat_enabled ? 1 : 0;
    output->cursor_on_left = s_cursor.x < s_map_width / 2 ? 1 : 0;
    output->camera_settled = s_camera_settle_frames >= 8 ? 1 : 0;
    if (!s_created || !*s_created) return;
    const sim::EncounterSnapshot& snapshot = s_snapshot;
    if (snapshot.outcome != sim::Outcome::ongoing) {
        output->mode = GRANDLEON_UI_COMPLETE;
        return;
    }
    if (s_result_frames || s_camera_return_frames) output->mode = GRANDLEON_UI_RESULT;
    else if (s_ai_active) output->mode = GRANDLEON_UI_OPPONENT;
    else if (s_mode == InputMode::move) output->mode = GRANDLEON_UI_MOVE;
    else if (s_mode == InputMode::action) output->mode = GRANDLEON_UI_ACTION;
    else if (s_mode == InputMode::attack) output->mode = GRANDLEON_UI_ATTACK;
    else if (s_mode == InputMode::ability) output->mode = GRANDLEON_UI_ABILITY;
    else output->mode = GRANDLEON_UI_BROWSE;

    const sim::UnitSnapshot* actor = find_unit(snapshot, s_selected);
    if (!actor) actor = unit_at(snapshot, s_cursor);
    if (actor) {
        output->actor_health = actor->health;
        output->actor_maximum = actor->maximum_health;
        output->ability_count = static_cast<std::uint8_t>(actor->ability_ids.size());
        output->local_player = actor->local_player;
        output->menu_count = static_cast<std::uint8_t>(action_count(actor));
    }
    output->menu_index = static_cast<std::uint8_t>(s_menu_index);
    output->result_missed = s_last_result_missed ? 1 : 0;
    output->result_damage = s_last_result_damage;
    output->result_counter_damage = s_last_result_counter_damage;
    if (s_mode == InputMode::attack && actor) {
        const sim::UnitSnapshot* target = unit_at(snapshot, s_cursor);
        if (target) {
            const sim::AttackForecast forecast = sim::forecast_attack(
                snapshot, actor->id, target->id,
                s_created->encounter.weapons(), 0
            );
            if (forecast) {
                output->forecast_valid = 1;
                output->target_health = target->health;
                output->target_maximum = target->maximum_health;
                output->hit_chance = forecast.hit_chance;
                output->damage = forecast.damage;
                output->counter = forecast.counter ? 1 : 0;
                output->counter_chance = forecast.counter_chance;
                output->counter_damage = forecast.counter_damage;
            }
        }
    } else if (s_mode == InputMode::ability && actor && s_menu_index > 0 &&
               s_menu_index <= actor->ability_ids.size()) {
        output->ability_index = static_cast<std::uint8_t>(s_menu_index - 1);
        const sim::UnitSnapshot* target = unit_at(snapshot, s_cursor);
        const sim::UnitId affected = target ? target->id : actor->id;
        const sim::AbilityForecast forecast = sim::forecast_ability(
            snapshot, actor->id, actor->ability_ids[s_menu_index - 1],
            s_cursor, affected, s_created->encounter.abilities()
        );
        if (forecast && target && forecast.covered && !forecast.spared) {
            output->forecast_valid = 1;
            output->target_health = target->health;
            output->target_maximum = target->maximum_health;
            output->hit_chance = forecast.hit_chance;
            output->damage = forecast.damage;
            output->restored = forecast.restored;
        }
    }
    if (s_last_result_restored) output->restored = s_last_result_damage;
}

int grandleon_game_update(uint16_t pressed) {
    if (!s_created || !*s_created) return 0;
    tick_camera();
    if (s_result_frames) {
        --s_result_frames;
        if (s_result_frames == 0) {
            if (s_close_combat_enabled) {
                s_camera_return_frames = 30;
                return 0;
            }
            sync_scene(s_snapshot);
            finish_side_if_needed(s_snapshot);
            s_selected = 0;
            s_mode = InputMode::browse;
        }
        return 0;
    }
    if (s_camera_return_frames) {
        --s_camera_return_frames;
        if (s_camera_return_frames == 0) {
            sync_scene(s_snapshot);
            finish_side_if_needed(s_snapshot);
            s_selected = 0;
            s_mode = InputMode::browse;
        }
        return 0;
    }
    if (s_ai_active) return update_ai() ? 1 : 0;
    if (pressed == 0) return 0;
    sim::EncounterSnapshot snapshot = s_snapshot;
    if (snapshot.active_side != sim::Side::first ||
        snapshot.outcome != sim::Outcome::ongoing) return 0;

    if (s_mode != InputMode::action) move_cursor(pressed, snapshot);
    if ((pressed & pad_triangle) && s_mode == InputMode::browse) {
        s_close_combat_enabled = !s_close_combat_enabled;
    }
    if (pressed & pad_circle) {
        if (s_mode == InputMode::attack || s_mode == InputMode::ability) {
            s_mode = InputMode::action;
        } else if (s_mode == InputMode::action) {
            const sim::UnitSnapshot* actor = find_unit(snapshot, s_selected);
            if (actor && !actor->has_moved) s_mode = InputMode::move;
        } else if (s_mode == InputMode::move) {
            const sim::UnitSnapshot* actor = find_unit(snapshot, s_selected);
            if (actor && actor->has_moved) s_mode = InputMode::action;
            else { s_selected = 0; s_mode = InputMode::browse; }
        }
    }
    bool mutated = false;
    if ((pressed & pad_cross) && s_mode == InputMode::browse) {
        const sim::UnitSnapshot* unit = unit_at(snapshot, s_cursor);
        if (unit && unit->side == sim::Side::first && !unit->has_acted) {
            s_selected = unit->id;
            s_mode = InputMode::move;
            s_menu_index = 0;
            ++s_select_count;
        }
    } else if ((pressed & pad_cross) && s_mode == InputMode::move) {
        const sim::UnitSnapshot* actor = find_unit(snapshot, s_selected);
        if (actor && actor->position == s_cursor) {
            s_mode = InputMode::action;
        } else if (actor) {
            const auto reachable = sim::reachable_tiles(snapshot, s_selected);
            bool allowed = false;
            for (const auto tile : reachable) if (tile == s_cursor) allowed = true;
            if (allowed) {
                sim::Command command{};
                command.type = sim::CommandType::move;
                command.unit_id = s_selected;
                command.destination = s_cursor;
                if (apply_command(command, "move", nullptr, &snapshot)) {
                    ++s_move_count;
                    s_mode = InputMode::action;
                    mutated = true;
                }
            }
        }
    } else if (s_mode == InputMode::action) {
        const sim::UnitSnapshot* actor = find_unit(snapshot, s_selected);
        const unsigned count = action_count(actor);
        if ((pressed & pad_up) && count) {
            s_menu_index = s_menu_index == 0 ? count - 1 : s_menu_index - 1;
        }
        if ((pressed & pad_down) && count) {
            s_menu_index = (s_menu_index + 1) % count;
        }
        if ((pressed & pad_cross) && actor && count) {
            if (s_menu_index == 0) {
                s_mode = InputMode::attack;
                s_cursor = actor->position;
            } else if (s_menu_index + 1 == count) {
                if (apply_wait(actor->id, "wait", &snapshot)) {
                    ++s_wait_count;
                    s_selected = 0;
                    s_mode = InputMode::browse;
                    mutated = true;
                    finish_side_if_needed(snapshot);
                }
            } else {
                s_mode = InputMode::ability;
                s_cursor = actor->position;
            }
        }
    } else if ((pressed & pad_cross) && s_mode == InputMode::attack) {
        const sim::UnitSnapshot* actor = find_unit(snapshot, s_selected);
        const sim::UnitSnapshot* target = unit_at(snapshot, s_cursor);
        if (actor && target) {
            const sim::AttackForecast forecast = sim::forecast_attack(
                snapshot, actor->id, target->id,
                s_created->encounter.weapons(), 0
            );
            if (forecast) {
                sim::Command command{};
                command.type = sim::CommandType::attack;
                command.unit_id = actor->id;
                command.target_id = target->id;
                sim::CommandResult result;
                if (apply_command(command, "attack", &result, &snapshot)) {
                    ++s_attack_count;
                    ++s_player_attack_count;
                    begin_result(command, result, forecast.counter, target->position);
                    mutated = true;
                }
            }
        }
    } else if ((pressed & pad_cross) && s_mode == InputMode::ability) {
        const sim::UnitSnapshot* actor = find_unit(snapshot, s_selected);
        if (actor && s_menu_index > 0 && s_menu_index <= actor->ability_ids.size()) {
            const sim::ContentId ability = actor->ability_ids[s_menu_index - 1];
            const sim::AbilityForecast gate = sim::forecast_ability(
                snapshot, actor->id, ability, s_cursor, actor->id,
                s_created->encounter.abilities()
            );
            if (gate) {
                sim::Command command{};
                command.type = sim::CommandType::ability;
                command.unit_id = actor->id;
                command.destination = s_cursor;
                command.ability_id = ability;
                sim::CommandResult result;
                if (apply_command(command, "ability", &result, &snapshot)) {
                    ++s_ability_count;
                    ++s_player_ability_count;
                    begin_result(command, result, false, s_cursor);
                    mutated = true;
                }
            }
        }
    }
    ps1_entity* cursor = grandleon_scene_mutable_entity(
        g_grandleon_cursor_entity_index
    );
    if (cursor) place_entity(*cursor, s_cursor, snapshot.width, snapshot.height);
    return mutated ? 1 : 0;
}
