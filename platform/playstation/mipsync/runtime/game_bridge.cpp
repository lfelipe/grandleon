// SPDX-License-Identifier: MIT
#include "game_bridge.h"

#include "generated/grandleon_scene.h"
#include "generated/grandleon_simulation.hpp"

#include <grandleon/campaign/graph.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/storage/byte_window_storage.hpp>
#include <grandleon/tactics/policy.hpp>
#include <grandleon/view/motion.hpp>
#include "psx_card.h"
#include "psx_runtime.h"
#include <array>
#include <cstdint>
#include <new>
#include <type_traits>

namespace sim = grandleon::simulation;
namespace tac = grandleon::tactics;
namespace psx = grandleon::playstation;
namespace camp = grandleon::campaign;
namespace storage = grandleon::storage;
namespace card = grandleon::playstation::card;
namespace view = grandleon::view;

namespace {

enum class InputMode : std::uint8_t { browse, move, action, attack, ability };
enum class CampaignMode : std::uint8_t {
    title, players, prologue, objective, battle, aftermath, save_error
};

constexpr unsigned maximum_board_cells = 4096U;

struct BoardMotion final {
    sim::UnitId unit{};
    sim::Position from{};
    sim::Position to{};
    std::array<view::RouteTile, maximum_board_cells> route{};
    unsigned route_count{};
    unsigned frame{};
    unsigned total_frames{};
    bool active{};
};

sim::Encounter::CreateResult* s_created{};
sim::EncounterSnapshot s_snapshot{};
sim::UnitId s_selected{};
sim::Position s_cursor{};
sim::Position s_player_cursors[2]{};
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
bool s_unit_visible[64]{};
bool s_visibility_revealed[maximum_board_cells]{};
std::array<std::uint8_t, maximum_board_cells> s_motion_crossable{};
std::array<std::uint8_t, maximum_board_cells> s_motion_distance{};
BoardMotion s_motion{};
unsigned s_camera_settle_frames{};
unsigned s_active_terrain_page{0xffffffffU};
CampaignMode s_campaign_mode{CampaignMode::battle};
unsigned s_campaign_menu_index{};
unsigned s_local_players{1};
bool s_continue_available{};
bool s_card_available{};
bool s_campaign_completion_handled{};
bool s_campaign_victory{true};
camp::CampaignSave s_campaign_save{};
camp::CampaignGraph s_campaign_graph{};
using CardStorage = typename std::aligned_storage<sizeof(card::MemoryCardWindow),
                                                  alignof(card::MemoryCardWindow)>::type;
using DeviceStorage = typename std::aligned_storage<sizeof(storage::ByteWindowSlotStorage),
                                                    alignof(storage::ByteWindowSlotStorage)>::type;
CardStorage s_card_bytes;
DeviceStorage s_device_bytes;
card::MemoryCardWindow* s_card{};
storage::ByteWindowSlotStorage* s_device{};

constexpr char campaign_slot[] = "steel-cedar";

constexpr std::uint16_t pad_up = 1U << 4U;
constexpr std::uint16_t pad_right = 1U << 5U;
constexpr std::uint16_t pad_down = 1U << 6U;
constexpr std::uint16_t pad_left = 1U << 7U;
constexpr std::uint16_t pad_triangle = 1U << 12U;
constexpr std::uint16_t pad_circle = 1U << 13U;
constexpr std::uint16_t pad_cross = 1U << 14U;
constexpr std::uint16_t pad_square = 1U << 15U;
constexpr unsigned close_combat_frames = 120;
constexpr unsigned result_only_frames = 50;

camp::DefinitionRef content_ref(
    grandleon::core::ContentCategory category, std::uint64_t stable_id
) {
    camp::DefinitionRef result{};
    for (unsigned i = 0; i < 16; ++i) {
        result.package_id[i] = grandleon_generated_game_id[i];
    }
    result.category = category;
    result.stable_id = stable_id;
    return result;
}

camp::DefinitionRef keyed_ref(
    grandleon::core::ContentCategory category, const char* key
) {
    return content_ref(category, grandleon::core::stable_content_id_v1(key));
}

camp::CampaignGraph make_campaign_graph() {
    return grandleon_generated_campaign_graph();
}

camp::CampaignOutcomeBatch outcome_batch(
    const char* source, std::vector<camp::CampaignOutcomeOperation> operations
) {
    camp::OutcomeSource provenance;
    provenance.encounter = keyed_ref(
        grandleon::core::ContentCategory::encounter, source
    );
    provenance.battle_hash = s_created && *s_created
        ? s_created->encounter.canonical_hash() : 1U;
    provenance.sequence = s_campaign_save.state.progress.history.size() + 1U;
    return camp::make_outcome_batch(provenance, std::move(operations));
}

camp::CampaignOutcomeBatch outcome_batch(
    std::uint64_t source_id,
    std::vector<camp::CampaignOutcomeOperation> operations
) {
    camp::OutcomeSource provenance;
    provenance.encounter = content_ref(
        grandleon::core::ContentCategory::encounter, source_id
    );
    provenance.battle_hash = s_created && *s_created
        ? s_created->encounter.canonical_hash() : 1U;
    provenance.sequence = s_campaign_save.state.progress.history.size() + 1U;
    return camp::make_outcome_batch(provenance, std::move(operations));
}

bool save_campaign() {
    if (!s_card_available || !s_device) return false;
    const std::vector<std::uint8_t> bytes = camp::save_campaign(s_campaign_save);
    grandleon_platform_card_begin();
    const storage::StorageError result = s_device->write(campaign_slot, bytes);
    grandleon_platform_card_end();
    psx::Line().text("MIPSYNC_CAMPAIGN_SAVE bytes=")
        .decimal(static_cast<unsigned>(bytes.size())).text(" status=")
        .decimal(static_cast<unsigned>(result)).flush();
    return result == storage::StorageError::none;
}

unsigned saved_player_count() {
    const auto key = keyed_ref(
        grandleon::core::ContentCategory::world_flag, "local_players"
    );
    const camp::WorldValue* value = camp::find_world_value(
        s_campaign_save.state, key
    );
    return value && value->type == camp::WorldValueType::integer &&
           value->value == 2 ? 2U : 1U;
}

void begin_new_campaign(unsigned players) {
    s_local_players = players;
    camp::SavePackageRequirement requirement;
    for (unsigned i = 0; i < 16; ++i) {
        requirement.package[i] = grandleon_generated_game_id[i];
    }
    requirement.content_revision = grandleon_generated_content_revision;
    s_campaign_save = camp::make_campaign_save({}, {requirement});
    if (camp::begin_campaign(s_campaign_save.state, s_campaign_graph) !=
        camp::ProgressionError::none) {
        s_campaign_mode = CampaignMode::save_error;
        return;
    }
    const auto key = keyed_ref(
        grandleon::core::ContentCategory::world_flag, "local_players"
    );
    const camp::CampaignOutcomeBatch founding = outcome_batch(
        "campaign_foundation",
        {camp::set_world_flag(key, {camp::WorldValueType::integer,
                                    static_cast<std::int64_t>(players)})}
    );
    if (!camp::apply_outcome(s_campaign_save.state, founding)) {
        s_campaign_mode = CampaignMode::save_error;
        return;
    }
    s_campaign_mode = CampaignMode::prologue;
    (void)save_campaign();
}

bool load_saved_campaign() {
    if (!s_device || !s_device->contains(campaign_slot)) return false;
    const storage::StorageRead stored = s_device->read(campaign_slot);
    if (!stored) return false;
    camp::SaveLoadOptions options;
    options.limits.maximum_bytes = static_cast<std::uint32_t>(
        card::card_budget().maximum_slot_bytes
    );
    const camp::SaveLoadResult loaded = camp::load_campaign(stored.bytes, options);
    if (!loaded) return false;
    s_campaign_save = loaded.save;
    s_local_players = saved_player_count();
    psx::Line().text("MIPSYNC_CAMPAIGN_RESUME node=")
        .hex64(s_campaign_save.state.progress.active_node.stable_id)
        .text(" players=").decimal(s_local_players).flush();
    return true;
}

void hide_board() {
    for (unsigned i = 0; i < g_ps1_scene.entity_count; ++i) {
        ps1_entity* entity = grandleon_scene_mutable_entity(i);
        if (entity && static_cast<int>(i) != g_grandleon_camera_entity_index) {
            entity->mesh_enabled = 0;
        }
    }
}

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

bool visibility_hidden(std::size_t cell, unsigned* hidden_index = nullptr) {
    for (unsigned index = 0;
         index < g_grandleon_visibility_cell_count; ++index) {
        if (g_grandleon_visibility_cell_indices[index] == cell) {
            if (hidden_index) *hidden_index = index;
            return true;
        }
    }
    return false;
}

bool unit_visible(const sim::UnitSnapshot& unit, int width) {
    if (!sim::on_board(unit)) return false;
    unsigned hidden_index = 0;
    const std::size_t cell = static_cast<std::size_t>(
        unit.position.y * width + unit.position.x
    );
    return !visibility_hidden(cell, &hidden_index) ||
        s_visibility_revealed[hidden_index];
}

bool reveal_visual_visibility_at(std::size_t cell) {
    if (!visibility_hidden(cell)) return false;
    bool changed = false;
    for (unsigned index = 0;
         index < g_grandleon_visibility_cell_count; ++index) {
        if (!s_visibility_revealed[index]) {
            s_visibility_revealed[index] = true;
            changed = true;
        }
    }
    if (changed) {
        psx::Line().text("MIPSYNC_VISIBILITY_REVEAL cells=")
            .decimal(g_grandleon_visibility_cell_count).flush();
    }
    return changed;
}

bool update_visual_visibility(const sim::EncounterSnapshot& snapshot) {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (!sim::on_board(unit) || unit.side != sim::Side::first) continue;
        const std::size_t origin = static_cast<std::size_t>(
            unit.position.y * snapshot.width + unit.position.x
        );
        if (reveal_visual_visibility_at(origin)) return true;
    }
    return false;
}

void sync_visibility_entities(bool close_combat) {
    if (g_grandleon_visibility_cell_count == 0U) return;
    ps1_entity* cover = grandleon_scene_mutable_entity(
        g_grandleon_visibility_entity_index
    );
    if (cover) {
        cover->mesh_enabled =
            !close_combat && !s_visibility_revealed[0] ? 1 : 0;
    }
}

void place_entity(
    ps1_entity& entity, sim::Position position, int width, int height,
    std::int32_t vertical_offset, bool follow_terrain = true
) {
    entity.position[0] =
        (static_cast<std::int32_t>(position.x) * 2 + 1 - width) * 65536;
    const unsigned terrain_index =
        static_cast<unsigned>(position.y) * static_cast<unsigned>(width) +
        static_cast<unsigned>(position.x);
    entity.position[1] = vertical_offset;
    if (follow_terrain && terrain_index < g_grandleon_terrain_surface_count) {
        entity.position[1] += g_grandleon_terrain_surface_y_q16[terrain_index];
    }
    entity.position[2] =
        (height - static_cast<std::int32_t>(position.y) * 2 - 1) * 65536;
}

std::int32_t terrain_surface(sim::Position position, int width) {
    const unsigned index = static_cast<unsigned>(position.y) *
        static_cast<unsigned>(width) + static_cast<unsigned>(position.x);
    return index < g_grandleon_terrain_surface_count
        ? g_grandleon_terrain_surface_y_q16[index] : 0;
}

void plan_board_motion(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId unit_id,
    sim::Position destination
) {
    s_motion = {};
    const sim::UnitSnapshot* actor = find_unit(snapshot, unit_id);
    if (!actor || !sim::on_board(*actor) || actor->position == destination) {
        return;
    }
    const int width = snapshot.width;
    const int height = snapshot.height;
    const unsigned cells = static_cast<unsigned>(width * height);
    if (cells == 0U || cells > maximum_board_cells) return;
    for (unsigned index = 0; index < cells; ++index) {
        s_motion_crossable[index] = 0U;
    }
    for (const sim::Position tile : sim::reachable_tiles(snapshot, unit_id)) {
        s_motion_crossable[static_cast<unsigned>(tile.y * width + tile.x)] = 1U;
    }
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == unit_id || unit.side != actor->side ||
            !sim::on_board(unit)) {
            continue;
        }
        s_motion_crossable[static_cast<unsigned>(
            unit.position.y * width + unit.position.x
        )] = 1U;
    }
    view::Route route(
        s_motion.route.data(), static_cast<int>(s_motion.route.size())
    );
    const bool found = view::plan_route(
        actor->position.x, actor->position.y,
        destination.x, destination.y,
        width, height,
        [width](int x, int y) {
            return s_motion_crossable[static_cast<unsigned>(y * width + x)] != 0U;
        },
        s_motion_distance.data(), route
    );
    s_motion.unit = unit_id;
    s_motion.from = actor->position;
    s_motion.to = destination;
    if (found) {
        s_motion.route_count = static_cast<unsigned>(route.size());
    } else {
        s_motion.route_count = 1U;
        s_motion.route[0] = {
            static_cast<std::int16_t>(destination.x),
            static_cast<std::int16_t>(destination.y),
        };
    }
    s_motion.total_frames = static_cast<unsigned>(
        view::slide_frames_for(static_cast<int>(s_motion.route_count))
    );
    s_motion.active = s_motion.total_frames != 0U;
}

void pose_board_motion() {
    if (!s_motion.active || s_motion.route_count == 0U) return;
    ps1_entity* entity = nullptr;
    unsigned binding = 0U;
    for (; binding < g_grandleon_unit_count; ++binding) {
        if (g_grandleon_unit_ids[binding] == s_motion.unit) {
            entity = grandleon_scene_mutable_entity(
                g_grandleon_unit_entity_indices[binding]
            );
            break;
        }
    }
    if (!entity) return;
    const unsigned route_index = s_motion.frame / view::slide_frames_per_tile <
        s_motion.route_count
        ? s_motion.frame / view::slide_frames_per_tile
        : s_motion.route_count - 1U;
    const sim::Position from = route_index == 0U
        ? s_motion.from
        : sim::Position{
            s_motion.route[route_index - 1U].x,
            s_motion.route[route_index - 1U].y,
        };
    const sim::Position to{
        s_motion.route[route_index].x,
        s_motion.route[route_index].y,
    };
    const int segment_frame = static_cast<int>(
        s_motion.frame - route_index * view::slide_frames_per_tile
    );
    const int from_x = (from.x * 2 + 1 - s_map_width) * 65536;
    const int to_x = (to.x * 2 + 1 - s_map_width) * 65536;
    const int from_z = (s_map_height - from.y * 2 - 1) * 65536;
    const int to_z = (s_map_height - to.y * 2 - 1) * 65536;
    entity->position[0] = view::slide_between(
        from_x, to_x, segment_frame, view::slide_frames_per_tile
    );
    entity->position[1] = 3072 + view::slide_between(
        terrain_surface(from, s_map_width), terrain_surface(to, s_map_width),
        segment_frame, view::slide_frames_per_tile
    );
    entity->position[1] += 4096 * 4 * segment_frame *
        (view::slide_frames_per_tile - segment_frame) /
        (view::slide_frames_per_tile * view::slide_frames_per_tile);
    entity->position[2] = view::slide_between(
        from_z, to_z, segment_frame, view::slide_frames_per_tile
    );
    entity->mesh_enabled = 1;
    s_unit_on_board[binding] = true;
    s_unit_visible[binding] = true;
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
    place_entity(
        *attacker_entity, attacker->position, snapshot.width, snapshot.height,
        3072, false
    );
    place_entity(
        *target_entity, target->position, snapshot.width, snapshot.height,
        3072, false
    );
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
    for (unsigned player = 0; player < 2; ++player) {
        ps1_entity* cursor = grandleon_scene_mutable_entity(
            g_grandleon_cursor_entity_indices[player]
        );
        if (cursor) {
            cursor->mesh_enabled =
                !close_combat && player < s_local_players ? 1 : 0;
        }
    }
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
    sync_visibility_entities(close_combat);
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
             s_unit_visible[index] &&
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
        s_unit_visible[binding] = unit_visible(*unit, snapshot.width);
        entity->mesh_enabled =
            s_unit_on_board[binding] && s_unit_visible[binding] ? 1 : 0;
        place_entity(
            *entity, unit->position, snapshot.width, snapshot.height, 3072
        );
    }
    for (unsigned player = 0; player < 2; ++player) {
        ps1_entity* cursor = grandleon_scene_mutable_entity(
            g_grandleon_cursor_entity_indices[player]
        );
        if (cursor) {
            cursor->mesh_enabled = player < s_local_players ? 1 : 0;
            place_entity(
                *cursor, s_player_cursors[player],
                snapshot.width, snapshot.height, 0
            );
        }
    }
    sync_visibility_entities(false);
}

bool advance_board_motion() {
    if (!s_motion.active) return false;
    if (s_motion.frame < s_motion.total_frames) ++s_motion.frame;
    const unsigned completed_steps = s_motion.frame /
        static_cast<unsigned>(view::slide_frames_per_tile);
    for (unsigned index = 0;
         index < completed_steps && index < s_motion.route_count; ++index) {
        const sim::Position reached{
            s_motion.route[index].x, s_motion.route[index].y,
        };
        const std::size_t cell = static_cast<std::size_t>(
            reached.y * s_map_width + reached.x
        );
        (void)reveal_visual_visibility_at(cell);
    }
    if (s_motion.frame >= s_motion.total_frames) {
        psx::Line().text("MIPSYNC_MOVE_SETTLED unit=")
            .hex64(s_motion.unit).text(" steps=")
            .decimal(s_motion.route_count).flush();
        s_motion.active = false;
        (void)update_visual_visibility(s_snapshot);
        sync_scene(s_snapshot);
    } else {
        pose_board_motion();
    }
    return true;
}

bool apply_command(
    const sim::Command& command, const char* action,
    sim::CommandResult* accepted, sim::EncounterSnapshot* after
) {
    if (command.type == sim::CommandType::move) {
        plan_board_motion(s_snapshot, command.unit_id, command.destination);
    }
    sim::CommandResult result = s_created->encounter.apply(command);
    if (!result) {
        s_motion = {};
        psx::Line().text("MIPSYNC_SIM_REJECT action=").text(action)
            .text(" error=").decimal(static_cast<unsigned>(result.error)).flush();
        return false;
    }
    s_snapshot = s_created->encounter.snapshot();
    if (!s_motion.active) (void)update_visual_visibility(s_snapshot);
    sync_scene(s_snapshot);
    pose_board_motion();
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

const grandleon_generated_campaign_node* active_campaign_node() {
    if (!s_campaign_save.state.progress.active) return nullptr;
    return grandleon_generated_campaign_node_for(
        s_campaign_save.state.progress.active_node.stable_id
    );
}

bool active_node_is(std::uint64_t id) {
    return s_campaign_save.state.progress.active &&
        s_campaign_save.state.progress.active_node.stable_id == id;
}

bool advance_story() {
    const std::uint64_t source = s_campaign_save.state.progress.active
        ? s_campaign_save.state.progress.active_node.stable_id
        : grandleon_generated_current_encounter_id;
    const camp::NodeCompletion completion = camp::complete_node(
        s_campaign_save.state, s_campaign_graph, outcome_batch(source, {})
    );
    if (!completion) {
        psx::Line().text("MIPSYNC_CAMPAIGN_ADVANCE_FAIL error=")
            .decimal(static_cast<unsigned>(completion.error)).flush();
        return false;
    }
    return save_campaign();
}

bool complete_battle(bool victory, bool force_pending = false) {
    std::vector<camp::CampaignOutcomeOperation> operations;
    if (force_pending) {
        for (const std::uint64_t objective : grandleon_generated_objective_ids()) {
            operations.push_back(camp::record_objective(
                content_ref(grandleon::core::ContentCategory::objective,
                            objective),
                victory ? camp::ObjectiveOutcome::satisfied
                        : camp::ObjectiveOutcome::failed
            ));
        }
    } else for (const sim::ObjectiveResult& objective : s_snapshot.objectives) {
        if (objective.state == sim::ObjectiveState::pending) continue;
        operations.push_back(camp::record_objective(
            content_ref(grandleon::core::ContentCategory::objective,
                        objective.id),
            objective.state == sim::ObjectiveState::satisfied
                ? camp::ObjectiveOutcome::satisfied
                : camp::ObjectiveOutcome::failed
        ));
    }
    const camp::NodeCompletion completion = camp::complete_node(
        s_campaign_save.state, s_campaign_graph,
        outcome_batch(grandleon_generated_current_encounter_id,
                      std::move(operations))
    );
    if (!completion) {
        psx::Line().text("MIPSYNC_CAMPAIGN_BATTLE_FAIL error=")
            .decimal(static_cast<unsigned>(completion.error)).flush();
        return false;
    }
    return save_campaign();
}

void route_active_campaign_node() {
    const grandleon_generated_campaign_node* node = active_campaign_node();
    if (!node) {
        s_campaign_mode = CampaignMode::save_error;
        return;
    }
    if (node->kind == 1U) {
        if (node->id == grandleon_generated_current_node_id) {
            s_campaign_mode = CampaignMode::objective;
            return;
        }
        if (!node->disc_path || !node->disc_path[0]) {
            s_campaign_mode = CampaignMode::save_error;
            return;
        }
        grandleon_platform_load_stage(node->disc_path);
        s_campaign_mode = CampaignMode::save_error;
        return;
    }
    if (node->kind == 3U) {
        s_campaign_mode = active_node_is(s_campaign_graph.entry.stable_id)
            ? CampaignMode::prologue : CampaignMode::aftermath;
        return;
    }
    if (node->kind == 2U) {
        s_campaign_mode = CampaignMode::aftermath;
        return;
    }
    s_campaign_mode = CampaignMode::save_error;
}

bool start_encounter() {
    if (s_created) return static_cast<bool>(*s_created);
    s_created = new (std::nothrow) sim::Encounter::CreateResult(
        sim::create_encounter(grandleon_generated_definition(s_local_players))
    );
    if (!s_created || !*s_created) {
        psx::Line().text("MIPSYNC_SIM_INIT_FAIL error=")
            .decimal(s_created ? static_cast<unsigned>(s_created->error) : 255U)
            .flush();
        return false;
    }
    s_snapshot = s_created->encounter.snapshot();
    const sim::EncounterSnapshot& snapshot = s_snapshot;
    s_motion = {};
    for (unsigned index = 0; index < maximum_board_cells; ++index) {
        s_visibility_revealed[index] = false;
    }
    (void)update_visual_visibility(snapshot);
    s_map_width = snapshot.width;
    s_map_height = snapshot.height;
    if (s_created->encounter.canonical_hash() !=
        grandleon_generated_canonical_hash(s_local_players)) {
        psx::Line().text("MIPSYNC_SIM_HASH_FAIL expected=")
            .hex64(grandleon_generated_canonical_hash(s_local_players)).text(" actual=")
            .hex64(s_created->encounter.canonical_hash()).flush();
        return false;
    }
    bool cursor_found[2]{};
    for (const auto& unit : snapshot.units) {
        if (unit.side != sim::Side::first || !sim::on_board(unit)) continue;
        const unsigned owner = unit.local_player > 1U ? 1U : 0U;
        if (!cursor_found[owner]) {
            s_player_cursors[owner] = unit.position;
            cursor_found[owner] = true;
        }
    }
    if (!cursor_found[0]) return false;
    if (!cursor_found[1]) s_player_cursors[1] = s_player_cursors[0];
    s_cursor = s_player_cursors[0];
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
    s_campaign_mode = CampaignMode::battle;
    psx::Line().text("MIPSYNC_SIM_READY width=").decimal(snapshot.width)
        .text(" height=").decimal(snapshot.height).text(" units=")
        .decimal(static_cast<unsigned>(snapshot.units.size())).text(" cursor_x=")
        .signed_decimal(s_cursor.x).text(" cursor_y=").signed_decimal(s_cursor.y)
        .text(" players=").decimal(s_local_players).text(" hash=")
        .hex64(s_created->encounter.canonical_hash()).flush();
    return true;
}

}  // namespace

int grandleon_game_init(void) {
#ifndef GRANDLEON_MIPSYNC_CAMPAIGN
    s_campaign_mode = CampaignMode::battle;
    s_local_players = grandleon_generated_local_players;
    return start_encounter() ? 1 : 0;
#else
    s_campaign_graph = make_campaign_graph();
    grandleon_platform_card_begin();
    s_card = new (&s_card_bytes) card::MemoryCardWindow();
    if (s_card->present()) {
        s_device = new (&s_device_bytes) storage::ByteWindowSlotStorage(
            *s_card, card::card_budget()
        );
        s_card_available = s_device->available();
    }
    grandleon_platform_card_end();
    s_continue_available = s_card_available && load_saved_campaign();
    hide_board();
    if (grandleon_generated_current_node_id == grandleon_generated_boot_node_id) {
        s_campaign_mode = CampaignMode::title;
    } else if (!s_continue_available) {
        s_campaign_mode = CampaignMode::save_error;
    } else {
        route_active_campaign_node();
    }
    psx::Line().text("MIPSYNC_CAMPAIGN_READY card=")
        .decimal(s_card_available ? 1U : 0U).text(" continue=")
        .decimal(s_continue_available ? 1U : 0U).flush();
    return 1;
#endif
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
    output->campaign_menu_index = static_cast<std::uint8_t>(s_campaign_menu_index);
    output->continue_available = s_continue_available ? 1 : 0;
    if (s_campaign_mode != CampaignMode::battle) {
        const grandleon_generated_campaign_node* node = active_campaign_node();
        output->headline = grandleon_generated_project_title;
        switch (s_campaign_mode) {
            case CampaignMode::title:
                output->mode = GRANDLEON_UI_TITLE;
                output->body = "THE ORCHARD BELL";
                output->campaign_menu_count = s_continue_available ? 2 : 1;
                break;
            case CampaignMode::players:
                output->mode = GRANDLEON_UI_PLAYERS;
                output->body = "CHOOSE YOUR COMPANY";
                output->campaign_menu_count = 2;
                break;
            case CampaignMode::prologue:
                output->mode = GRANDLEON_UI_STORY;
                output->headline = node && node->name ? node->name : "STORY";
                output->body = node && node->dialogue ? node->dialogue : "";
                break;
            case CampaignMode::objective:
                output->mode = GRANDLEON_UI_OBJECTIVE;
                output->headline = node && node->name ? node->name : "BATTLE";
                output->body = grandleon_generated_objective;
                break;
            case CampaignMode::aftermath:
                output->mode = GRANDLEON_UI_AFTERMATH;
                output->headline = node && node->kind == 2U ? "CAMPAIGN ENDED"
                    : node && node->name ? node->name
                    : (s_campaign_victory ? "STORY" : "DEFEAT");
                output->body = node && node->kind == 2U ? "RETURN TO TITLE"
                    : node && node->dialogue && node->dialogue[0]
                        ? node->dialogue
                        : (s_campaign_victory ? "CAMPAIGN COMPLETE"
                                              : "THE COMPANY MUST REGROUP");
                break;
            case CampaignMode::save_error:
                output->mode = GRANDLEON_UI_SAVE_ERROR;
                output->headline = "MEMORY CARD";
                output->body = "SAVE DATA IS NOT AVAILABLE";
                break;
            case CampaignMode::battle: break;
        }
        return;
    }
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

int grandleon_game_update(uint16_t player_one, uint16_t player_two) {
    std::uint16_t pressed = player_one;
    if (s_campaign_mode != CampaignMode::battle) {
        if (!pressed) return 0;
        if (s_campaign_mode == CampaignMode::title) {
            const unsigned count = s_continue_available ? 2U : 1U;
            if (pressed & (pad_up | pad_down)) {
                s_campaign_menu_index = (s_campaign_menu_index + 1U) % count;
            }
            if (pressed & pad_cross) {
                if (s_campaign_menu_index == 0) {
                    s_campaign_menu_index = 0;
                    s_campaign_mode = CampaignMode::players;
                } else {
                    route_active_campaign_node();
                }
            }
            return 0;
        }
        if (s_campaign_mode == CampaignMode::players) {
            if (pressed & (pad_up | pad_down)) {
                s_campaign_menu_index ^= 1U;
            }
            if (pressed & pad_circle) {
                s_campaign_menu_index = 0;
                s_campaign_mode = CampaignMode::title;
            } else if (pressed & pad_cross) {
                begin_new_campaign(s_campaign_menu_index + 1U);
            }
            return 0;
        }
        if (s_campaign_mode == CampaignMode::prologue && (pressed & pad_cross)) {
            if (advance_story()) {
                s_continue_available = true;
                route_active_campaign_node();
            } else {
                s_campaign_mode = CampaignMode::save_error;
            }
            return 0;
        }
        if (s_campaign_mode == CampaignMode::objective) {
#if defined(GRANDLEON_MIPSYNC_CAMPAIGN_PROBE)
            if ((pressed & pad_square) &&
                active_node_is(grandleon_generated_current_node_id)) {
                if (complete_battle(true, true)) {
                    s_campaign_victory = true;
                    s_campaign_mode = CampaignMode::aftermath;
                } else {
                    s_campaign_mode = CampaignMode::save_error;
                }
                return 0;
            }
#endif
            if (!(pressed & pad_cross)) return 0;
            if (!start_encounter()) {
                s_campaign_mode = CampaignMode::save_error;
                return 0;
            }
            return 2;
        }
        if (s_campaign_mode == CampaignMode::aftermath && (pressed & pad_cross)) {
            const grandleon_generated_campaign_node* node = active_campaign_node();
            if (node && node->kind == 2U) {
                s_campaign_mode = CampaignMode::title;
                return 0;
            }
            if (node && node->kind == 3U && advance_story()) {
                route_active_campaign_node();
            } else {
                s_campaign_mode = CampaignMode::save_error;
            }
            return 0;
        }
        return 0;
    }
    if (!s_created || !*s_created) return 0;
    const bool motion_was_active = advance_board_motion();
    tick_camera();
    if (motion_was_active) return 0;
    if (s_snapshot.outcome != sim::Outcome::ongoing &&
        !s_campaign_completion_handled) {
        s_campaign_completion_handled = true;
        s_campaign_victory = s_snapshot.outcome == sim::Outcome::first_side_won;
        if (complete_battle(s_campaign_victory)) {
            hide_board();
            s_campaign_mode = CampaignMode::aftermath;
        } else {
            s_campaign_mode = CampaignMode::save_error;
        }
        return 1;
    }
#if defined(GRANDLEON_MIPSYNC_CAMPAIGN_PROBE)
    if ((pressed & pad_square) && s_mode == InputMode::browse &&
        !s_result_frames && !s_camera_return_frames) {
        s_campaign_victory = true;
        s_campaign_completion_handled = true;
        if (complete_battle(true, true)) {
            hide_board();
            s_campaign_mode = CampaignMode::aftermath;
        } else {
            s_campaign_mode = CampaignMode::save_error;
        }
        return 1;
    }
#endif
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
    std::uint8_t input_player = 0;
    if (s_selected != 0) {
        const sim::UnitSnapshot* selected = find_unit(s_snapshot, s_selected);
        if (selected && selected->local_player == 2U && s_local_players > 1U) {
            input_player = 1;
            pressed = player_two;
        }
    } else if (s_local_players > 1U && player_two != 0) {
        input_player = 1;
        pressed = player_two;
    }
    if (pressed == 0) return 0;
    s_cursor = s_player_cursors[input_player];
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
        if (unit && unit->side == sim::Side::first && !unit->has_acted &&
            unit->local_player == input_player + 1U) {
            s_selected = unit->id;
            s_mode = InputMode::move;
            s_menu_index = 0;
            ++s_select_count;
            psx::Line().text("MIPSYNC_LOCAL_INPUT player=")
                .decimal(input_player + 1U).text(" unit=")
                .hex64(unit->id).flush();
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
    s_player_cursors[input_player] = s_cursor;
    for (unsigned player = 0; player < 2; ++player) {
        ps1_entity* cursor = grandleon_scene_mutable_entity(
            g_grandleon_cursor_entity_indices[player]
        );
        if (cursor) {
            cursor->mesh_enabled = player < s_local_players ? 1 : 0;
            place_entity(
                *cursor, s_player_cursors[player],
                snapshot.width, snapshot.height, 0
            );
        }
    }
    return mutated ? 1 : 0;
}
