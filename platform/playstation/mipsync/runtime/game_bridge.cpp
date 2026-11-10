// SPDX-License-Identifier: MIT
#include "game_bridge.h"
#include "text_paging.h"
#include "grandleon_audio.h"

#include "generated/grandleon_scene.h"
#include "generated/grandleon_simulation.hpp"
extern "C" {
#include "runtime/textures.h"
#include "generated/audio_data.h"
}

#include <grandleon/campaign/graph.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/client/console_localization.hpp>
#include <grandleon/client/console_name_editor.hpp>
#include <grandleon/client/member_name_tokens.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/storage/byte_window_storage.hpp>
#include <grandleon/tactics/policy.hpp>
#include <grandleon/view/motion.hpp>
#include <grandleon/view/ambient_motion.hpp>
#include "psx_card.h"
#include "psx_runtime.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sim = grandleon::simulation;
namespace tac = grandleon::tactics;
namespace psx = grandleon::playstation;
namespace camp = grandleon::campaign;
namespace storage = grandleon::storage;
namespace card = grandleon::playstation::card;
namespace view = grandleon::view;
namespace console = grandleon::client::console_localization;
namespace client = grandleon::client;

namespace {

enum class InputMode : std::uint8_t {
    browse, move, action, attack, ability, talk, deployment, pause, help,
    settings
};
enum class CampaignMode : std::uint8_t {
    title, players, name, prologue, objective, battle, aftermath, save_error,
    help, settings
};

constexpr unsigned maximum_board_cells = 4096U;
// Ten 60 Hz ticks became only five distinct pictures after the renderer moved
// to 30 fps.  Give the PS1 ten presented frames per tile so the two authored
// footfall poses, facing change and arc read as a walk rather than a teleport.
constexpr unsigned board_slide_frames_per_tile =
    static_cast<unsigned>(view::slide_frames_per_tile * 2);

struct BoardMotion final {
    sim::UnitId unit{};
    sim::Position from{};
    sim::Position to{};
    std::array<view::RouteTile, maximum_board_cells> route{};
    unsigned route_count{};
    unsigned frame{};
    unsigned total_frames{};
    bool facing_left{};
    bool active{};
};

struct BattleMomentState final {
    std::vector<std::uint64_t> dialogue_ids;
    std::size_t dialogue_index{};
    std::size_t line_index{};
    std::size_t page_offset{};
    bool active{};
};

enum class ActionKind : std::uint8_t {
    attack, ability, item, talk, wait
};

struct ActionChoice final {
    ActionKind kind{ActionKind::wait};
    sim::ContentId content{};
};

struct ActionMenu final {
    std::array<ActionChoice, 32> choices{};
    unsigned count{};

    void append(ActionKind kind, sim::ContentId content = 0U) {
        if (count < choices.size()) choices[count++] = {kind, content};
    }
};

sim::Encounter::CreateResult* s_created{};
sim::EncounterSnapshot s_snapshot{};
sim::UnitId s_selected{};
std::vector<sim::Position> s_movement_tiles;
std::vector<std::uint8_t> s_movement_edges;
sim::UnitId s_movement_tiles_unit{};
sim::Position s_cursor{};
sim::Position s_player_cursors[2]{};
unsigned s_focus_player{};
std::uint32_t s_ambient_ticks{};
InputMode s_mode{InputMode::browse};
unsigned s_menu_index{};
unsigned s_pause_menu_index{};
unsigned s_settings_menu_index{};
unsigned s_music_volume{45U};
unsigned s_effects_volume{75U};
unsigned s_language_index{};
unsigned s_ai_delay{};
bool s_ai_active{};
bool s_close_combat_enabled{true};
bool s_reduced_motion{};
view::ConsoleDisplayOptions s_display_options{};
std::uint16_t s_measured_fps{30U};
unsigned s_result_frames{};
unsigned s_result_duration{};
unsigned s_attack_sound_cue{GRANDLEON_SOUND_BLADE};
unsigned s_counter_sound_cue{GRANDLEON_SOUND_BLADE};
bool s_counter_missed{};
bool s_result_combat_audio{};
unsigned s_camera_return_frames{};
sim::UnitId s_combat_attacker{};
sim::UnitId s_combat_target{};
sim::Position s_combat_centre{};
sim::Position s_combat_attacker_position{};
sim::Position s_combat_target_position{};
bool s_combat_counter{};
bool s_last_result_missed{};
bool s_last_result_restored{};
bool s_result_close_combat{};
std::int16_t s_last_result_damage{};
std::int16_t s_last_result_counter_damage{};
unsigned s_select_count{};
unsigned s_move_count{};
unsigned s_wait_count{};
unsigned s_attack_count{};
unsigned s_ability_count{};
unsigned s_player_attack_count{};
unsigned s_player_ability_count{};
unsigned s_player_item_count{};
unsigned s_player_talk_count{};
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
bool s_unit_facing_left[64]{};
bool s_visibility_revealed[maximum_board_cells]{};
std::array<std::uint8_t, maximum_board_cells> s_motion_crossable{};
std::array<std::uint8_t, maximum_board_cells> s_motion_distance{};
BoardMotion s_motion{};
BattleMomentState s_battle_moment{};
sim::ContentId s_action_content{};
unsigned s_camera_settle_frames{};
CampaignMode s_campaign_mode{CampaignMode::battle};
unsigned s_campaign_menu_index{};
unsigned s_local_players{1};
std::vector<std::uint64_t> s_pending_player_names;
std::uint64_t s_name_member{};
std::optional<client::ConsoleNameEditor> s_name_editor;
CampaignMode s_name_return_mode{CampaignMode::prologue};
bool s_name_route_after{};
std::array<char, 192U> s_ui_headline{};
std::vector<char> s_story_body;
std::vector<char> s_battle_body;
std::size_t s_story_page_offset{};
CampaignMode s_story_page_mode{CampaignMode::title};
std::uint64_t s_story_page_node{};
unsigned s_story_page_language{};
std::uint64_t s_logged_member_text_node{};
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
constexpr char settings_slot[] = "steel-settings";

constexpr std::uint16_t pad_start = 1U << 3U;
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

const char* game_language() {
    return grandleon_generated_language_code(s_language_index);
}

const char* ui(console::Message message) {
    return console::text(game_language(), message);
}

unsigned settings_menu_count() {
    return grandleon_generated_language_count() > 1U ? 5U : 4U;
}

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

bool save_display_settings() {
    if (!s_card_available || !s_device) return false;
    const std::string_view language = game_language();
    std::vector<std::uint8_t> bytes{
        'G', 'S', 3U,
        static_cast<std::uint8_t>(
            (s_close_combat_enabled ? 1U : 0U) |
            (s_reduced_motion ? 2U : 0U) |
            (s_display_options.show_fps ? 4U : 0U)
        ),
        static_cast<std::uint8_t>(language.size()),
    };
    bytes.insert(bytes.end(), language.begin(), language.end());
    bytes.push_back(static_cast<std::uint8_t>(s_music_volume));
    bytes.push_back(static_cast<std::uint8_t>(s_effects_volume));
    grandleon_platform_card_begin();
    const storage::StorageError result = s_device->write(settings_slot, bytes);
    grandleon_platform_card_end();
    return result == storage::StorageError::none;
}

void load_display_settings() {
    if (!s_device || !s_device->contains(settings_slot)) return;
    const storage::StorageRead stored = s_device->read(settings_slot);
    if (!stored || stored.bytes.size() < 4U || stored.bytes[0] != 'G' ||
        stored.bytes[1] != 'S' ||
        (stored.bytes[2] < 1U || stored.bytes[2] > 3U)) {
        return;
    }
    if (stored.bytes[2] == 1U && stored.bytes.size() != 4U) return;
    if (stored.bytes[2] >= 2U) {
        if (stored.bytes.size() < 5U ||
            stored.bytes.size() != 5U + stored.bytes[4] + (stored.bytes[2] == 3U ? 2U : 0U)) {
            return;
        }
        const std::string_view selected(
            reinterpret_cast<const char*>(stored.bytes.data() + 5U),
            stored.bytes[4]
        );
        for (unsigned index = 0U;
             index < grandleon_generated_language_count(); ++index) {
            if (selected == grandleon_generated_language_code(index)) {
                s_language_index = index;
                break;
            }
        }
    }
    s_close_combat_enabled = (stored.bytes[3] & 1U) != 0U;
    s_reduced_motion = (stored.bytes[3] & 2U) != 0U;
    s_display_options.show_fps = (stored.bytes[3] & 4U) != 0U;
    if (stored.bytes[2] == 3U) {
        s_music_volume = std::min<unsigned>(100U, stored.bytes[5U + stored.bytes[4]]);
        s_effects_volume = std::min<unsigned>(100U, stored.bytes[6U + stored.bytes[4]]);
    }
    grandleon_audio_set_volumes(s_music_volume, s_effects_volume);
    psx::Line().text("MIPSYNC_SETTINGS_LOAD close=")
        .decimal(s_close_combat_enabled ? 1U : 0U).text(" reduced=")
        .decimal(s_reduced_motion ? 1U : 0U).text(" fps=")
        .decimal(s_display_options.show_fps ? 1U : 0U).text(" language=")
        .text(game_language()).text(" music=").decimal(s_music_volume)
        .text(" effects=").decimal(s_effects_volume).flush();
}

void toggle_setting(unsigned row, int direction = 1) {
    const char* name = "close";
    bool value = false;
    if (row == 0U) {
        s_close_combat_enabled = !s_close_combat_enabled;
        value = s_close_combat_enabled;
    } else if (row == 1U) {
        s_display_options.toggle_fps();
        value = s_display_options.show_fps;
        name = "fps";
    } else if (row == 2U || row == 3U) {
        unsigned& volume = row == 2U ? s_music_volume : s_effects_volume;
        volume = static_cast<unsigned>(std::clamp(static_cast<int>(volume) + direction * 5, 0, 100));
        grandleon_audio_set_volumes(s_music_volume, s_effects_volume);
        const bool saved = save_display_settings();
        psx::Line().text("MIPSYNC_SETTING name=").text(row == 2U ? "music" : "effects")
            .text(" value=").decimal(volume).text(" saved=").decimal(saved ? 1U : 0U).flush();
        return;
    } else {
        const unsigned count = grandleon_generated_language_count();
        if (count > 1U) s_language_index = (s_language_index + 1U) % count;
        name = "language";
        value = true;
    }
    const bool saved = save_display_settings();
    psx::Line().text("MIPSYNC_SETTING name=").text(name).text(" value=")
        .decimal(value ? 1U : 0U).text(" saved=")
        .decimal(saved ? 1U : 0U).text(" language=")
        .text(game_language()).flush();
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

void route_active_campaign_node();

const char* authored_member_name(
    const grandleon_generated_campaign_member& member
) {
    const char* localized = grandleon_generated_campaign_member_name_for(
        s_language_index, member.persistent_id
    );
    return localized && localized[0] ? localized : member.authored_name;
}

const char* effective_member_name(
    const grandleon_generated_campaign_member& member
) {
    const camp::PlayerName* chosen = camp::find_player_name(
        s_campaign_save.state, camp::PersistentEntityId{member.persistent_id}
    );
    return chosen ? chosen->value.c_str() : authored_member_name(member);
}

std::string substitute_player_names(std::string_view text) {
    return client::substitute_member_name_tokens(
        text,
        [](std::string_view source_key) -> std::string_view {
            const std::uint64_t source_id =
                grandleon::core::stable_content_id_v1(source_key);
            for (const auto& member : grandleon_generated_campaign_members()) {
                if (member.source_id != source_id ||
                    camp::find_unit(
                        s_campaign_save.state,
                        camp::PersistentEntityId{member.persistent_id}
                    ) == nullptr) {
                    continue;
                }
                return effective_member_name(member);
            }
            return {};
        }
    );
}

template <std::size_t Capacity>
void keep_ui_text(
    std::array<char, Capacity>& target,
    const std::string& source
) {
    const std::size_t length = std::min(
        source.size(), Capacity > 0U ? Capacity - 1U : 0U
    );
    for (std::size_t index = 0U; index < length; ++index) {
        target[index] = source[index];
    }
    target[length] = '\0';
}

void queue_player_names(const std::vector<std::uint64_t>& members) {
    for (const std::uint64_t persistent_id : members) {
        const auto* member = grandleon_generated_campaign_member_for(
            persistent_id
        );
        if (!member || !member->player_nameable ||
            camp::find_player_name(
                s_campaign_save.state,
                camp::PersistentEntityId{persistent_id}
            )) {
            continue;
        }
        if (std::find(
                s_pending_player_names.begin(),
                s_pending_player_names.end(), persistent_id
            ) == s_pending_player_names.end()) {
            s_pending_player_names.push_back(persistent_id);
        }
    }
}

std::vector<std::uint64_t> append_recruitment_at(
    std::uint64_t node_id,
    std::vector<camp::CampaignOutcomeOperation>& operations
) {
    std::vector<std::uint64_t> joined;
    for (const auto& member : grandleon_generated_campaign_members()) {
        if (member.join_node_id != node_id) continue;
        operations.push_back(camp::recruit_unit(
            camp::PersistentEntityId{member.persistent_id},
            content_ref(
                grandleon::core::ContentCategory::unit_type,
                member.unit_type_id
            )
        ));
        joined.push_back(member.persistent_id);
    }
    return joined;
}

bool begin_next_player_name() {
    while (!s_pending_player_names.empty()) {
        const std::uint64_t persistent_id = s_pending_player_names.front();
        s_pending_player_names.erase(s_pending_player_names.begin());
        const auto* member = grandleon_generated_campaign_member_for(
            persistent_id
        );
        if (!member || !member->player_nameable ||
            camp::find_unit(
                s_campaign_save.state,
                camp::PersistentEntityId{persistent_id}
            ) == nullptr) {
            continue;
        }
        s_name_member = persistent_id;
        s_name_editor.emplace(
            effective_member_name(*member), authored_member_name(*member)
        );
        s_campaign_mode = CampaignMode::name;
        psx::Line().text("MIPSYNC_NAME_PROMPT member=")
            .hex64(persistent_id).text(" limit=")
            .decimal(static_cast<unsigned>(client::console_player_name_glyphs))
            .flush();
        return true;
    }
    return false;
}

void enter_after_player_names(CampaignMode mode) {
    s_name_route_after = false;
    s_name_return_mode = mode;
    if (!begin_next_player_name()) s_campaign_mode = mode;
}

void route_after_player_names() {
    s_name_route_after = true;
    if (!begin_next_player_name()) route_active_campaign_node();
}

bool store_player_name(client::ConsoleNameAnswer answer) {
    if (!s_name_editor || s_name_member == 0U ||
        answer == client::ConsoleNameAnswer::editing ||
        answer == client::ConsoleNameAnswer::cancel) {
        return false;
    }
    const auto* authored = grandleon_generated_campaign_member_for(
        s_name_member
    );
    if (!authored || !authored->player_nameable ||
        camp::find_unit(
            s_campaign_save.state, camp::PersistentEntityId{s_name_member}
        ) == nullptr) {
        return false;
    }
    camp::CampaignState candidate = s_campaign_save.state;
    const camp::PersistentEntityId member{s_name_member};
    auto position = std::lower_bound(
        candidate.player_names.begin(), candidate.player_names.end(), member,
        [](const camp::PlayerName& current, camp::PersistentEntityId value) {
            return current.member < value;
        }
    );
    bool used_default = answer == client::ConsoleNameAnswer::use_default;
    if (used_default) {
        if (position != candidate.player_names.end() &&
            position->member == member) {
            candidate.player_names.erase(position);
        }
    } else {
        const std::string name{s_name_editor->name()};
        if (!camp::player_name_is_valid(name)) return false;
        if (position != candidate.player_names.end() &&
            position->member == member) {
            position->value = name;
        } else {
            candidate.player_names.insert(position, {member, name});
        }
    }
    if (camp::validate(candidate) != camp::StateError::none) return false;
    s_campaign_save.state = std::move(candidate);
    const bool saved = save_campaign();
    psx::Line().text("MIPSYNC_NAME_SAVED member=").hex64(s_name_member)
        .text(" default=").decimal(used_default ? 1U : 0U)
        .text(" saved=").decimal(saved ? 1U : 0U).flush();
    return saved;
}

void finish_player_name(client::ConsoleNameAnswer answer) {
    if (answer == client::ConsoleNameAnswer::cancel) {
        psx::Line().text("MIPSYNC_NAME_CANCEL member=")
            .hex64(s_name_member).text(" wrote=0").flush();
    } else if (!store_player_name(answer)) {
        s_campaign_mode = CampaignMode::save_error;
        return;
    }
    s_name_editor.reset();
    s_name_member = 0U;
    if (begin_next_player_name()) return;
    if (s_name_route_after) route_active_campaign_node();
    else s_campaign_mode = s_name_return_mode;
}

void begin_new_campaign(unsigned players) {
    s_local_players = players;
    camp::SavePackageRequirement requirement;
    for (unsigned i = 0; i < 16; ++i) {
        requirement.package[i] = grandleon_generated_game_id[i];
    }
    requirement.content_revision = grandleon_generated_content_revision;
    s_campaign_save = camp::make_campaign_save({}, {requirement});
    const auto key = keyed_ref(
        grandleon::core::ContentCategory::world_flag, "local_players"
    );
    std::vector<camp::CampaignOutcomeOperation> operations;
    const std::vector<std::uint64_t> founders = append_recruitment_at(
        0U, operations
    );
    operations.push_back(camp::set_world_flag(
        key, {camp::WorldValueType::integer,
              static_cast<std::int64_t>(players)}
    ));
    const camp::CampaignOutcomeBatch founding = outcome_batch(
        "campaign_foundation", std::move(operations)
    );
    if (!camp::apply_outcome(s_campaign_save.state, founding)) {
        s_campaign_mode = CampaignMode::save_error;
        return;
    }
    if (camp::begin_campaign(s_campaign_save.state, s_campaign_graph) !=
        camp::ProgressionError::none) {
        s_campaign_mode = CampaignMode::save_error;
        return;
    }
    s_pending_player_names.clear();
    queue_player_names(founders);
    if (!save_campaign()) {
        s_campaign_mode = CampaignMode::save_error;
        return;
    }
    route_after_player_names();
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
    for (const camp::PlayerName& name : loaded.save.state.player_names) {
        const auto* member = grandleon_generated_campaign_member_for(
            name.member.value
        );
        if (!member || !member->player_nameable) return false;
    }
    s_campaign_save = loaded.save;
    s_pending_player_names.clear();
    s_name_editor.reset();
    s_name_member = 0U;
    s_local_players = saved_player_count();
    psx::Line().text("MIPSYNC_CAMPAIGN_RESUME node=")
        .hex64(s_campaign_save.state.progress.active_node.stable_id)
        .text(" players=").decimal(s_local_players).text(" names=")
        .decimal(static_cast<unsigned>(
            s_campaign_save.state.player_names.size()
        )).flush();
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

bool activate_pending_battle_moment() {
    while (s_battle_moment.dialogue_index <
           s_battle_moment.dialogue_ids.size()) {
        const std::uint64_t dialogue_id =
            s_battle_moment.dialogue_ids[s_battle_moment.dialogue_index];
        const auto& lines = grandleon_generated_moment_lines(
            s_language_index, dialogue_id
        );
        if (!lines.empty()) {
            s_battle_moment.line_index = 0U;
            s_battle_moment.page_offset = 0U;
            s_battle_moment.active = true;
            psx::Line().text("MIPSYNC_BATTLE_MOMENT dialogue=")
                .hex64(dialogue_id).text(" lines=")
                .decimal(static_cast<unsigned>(lines.size())).flush();
            return true;
        }
        ++s_battle_moment.dialogue_index;
    }
    s_battle_moment = {};
    return false;
}

void queue_battle_moments(
    unsigned trigger, std::uint64_t placement_id = 0U, bool reset = false
) {
    if (reset) s_battle_moment = {};
    for (const auto& moment : grandleon_generated_moments(s_local_players)) {
        if (moment.trigger != trigger) continue;
        if (moment.placement_id != 0U &&
            moment.placement_id != placement_id) {
            continue;
        }
        s_battle_moment.dialogue_ids.push_back(moment.dialogue_id);
    }
}

void queue_event_moments(const std::vector<sim::Event>& events) {
    for (const sim::Event& event : events) {
        unsigned trigger = 0U;
        if (event.type == sim::EventType::unit_talked) trigger = 2U;
        else if (event.type == sim::EventType::unit_defeated) trigger = 3U;
        else continue;
        queue_battle_moments(trigger, event.unit_id);
    }
}

void advance_battle_moment() {
    if (!s_battle_moment.active ||
        s_battle_moment.dialogue_index >=
            s_battle_moment.dialogue_ids.size()) {
        return;
    }
    const std::uint64_t dialogue_id =
        s_battle_moment.dialogue_ids[s_battle_moment.dialogue_index];
    const auto& lines = grandleon_generated_moment_lines(
        s_language_index, dialogue_id
    );
    if (s_battle_moment.line_index < lines.size()) {
        const std::string body = substitute_player_names(lines[s_battle_moment.line_index].text);
        const char* start = body.c_str() + s_battle_moment.page_offset;
        const char* next = grandleon_text_page_end(start, 34, 3);
        if (*next) {
            s_battle_moment.page_offset += static_cast<std::size_t>(next - start);
            psx::Line().text("MIPSYNC_DIALOGUE_PAGE offset=")
                .decimal(static_cast<unsigned>(s_battle_moment.page_offset)).flush();
            return;
        }
    }
    s_battle_moment.page_offset = 0;
    ++s_battle_moment.line_index;
    if (s_battle_moment.line_index < lines.size()) return;
    s_battle_moment.active = false;
    ++s_battle_moment.dialogue_index;
    (void)activate_pending_battle_moment();
}

const grandleon_generated_dialogue_line* current_battle_moment_line() {
    if (!s_battle_moment.active ||
        s_battle_moment.dialogue_index >=
            s_battle_moment.dialogue_ids.size()) {
        return nullptr;
    }
    const auto& lines = grandleon_generated_moment_lines(
        s_language_index,
        s_battle_moment.dialogue_ids[s_battle_moment.dialogue_index]
    );
    return s_battle_moment.line_index < lines.size()
        ? &lines[s_battle_moment.line_index] : nullptr;
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

enum UnitPose : unsigned {
    unit_pose_idle = 0U,
    unit_pose_move_contact = 1U,
    unit_pose_move_pass = 2U,
    unit_pose_move_north_contact = 3U,
    unit_pose_move_north_pass = 4U,
    unit_pose_move_south_contact = 5U,
    unit_pose_move_south_pass = 6U,
    unit_pose_attack = 7U,
    unit_pose_hurt = 8U,
};

UnitPose walking_pose(view::BoardHeading heading, unsigned frame) {
    // The source art is authored at seven frames per second.  At a 30 Hz
    // presentation, holding each drawing for four rendered frames is the
    // closest integer cadence. `frame` advances at 60 Hz, hence eight ticks.
    const bool contact = ((frame / 8U) & 1U) == 0U;
    if (heading == view::BoardHeading::north) {
        return contact ? unit_pose_move_north_contact
                       : unit_pose_move_north_pass;
    }
    if (heading == view::BoardHeading::south) {
        return contact ? unit_pose_move_south_contact
                       : unit_pose_move_south_pass;
    }
    return contact ? unit_pose_move_contact : unit_pose_move_pass;
}

void apply_unit_art(
    unsigned binding, ps1_entity& entity, UnitPose pose,
    bool facing_left, bool close_view = false
) {
    if (binding >= g_grandleon_unit_count) return;
    entity.texture_index = static_cast<std::int32_t>(
        g_grandleon_unit_pose_texture_indices[binding][pose]
    );
    ps1_texture_ensure_resident(static_cast<unsigned>(entity.texture_index));
    const bool directional = pose >= unit_pose_move_north_contact &&
        pose <= unit_pose_move_south_pass;
    entity.mesh_index = static_cast<std::int32_t>(directional
        ? g_grandleon_directional_billboard_mesh_index
        : (facing_left ? g_grandleon_mirrored_billboard_mesh_index
                       : g_grandleon_board_billboard_mesh_index));
    const bool plate = close_view && g_grandleon_combat_plate_indices[0];
    entity.scale[0] = plate ? 235930 : close_view ? 157286 : 88474;
    entity.scale[1] = plate ? 334234 : close_view ? 222822 : 124518;
    entity.scale[2] = 65536;
}

UnitPose combat_pose_for(sim::UnitId unit) {
    if (!s_result_frames || !s_result_close_combat) return unit_pose_idle;
    const unsigned elapsed = s_result_duration - s_result_frames;
    const auto* actor = find_unit(s_snapshot, unit);
    if (actor && actor->health <= 0 &&
        ((unit == s_combat_target && elapsed >= 37U) ||
         (unit == s_combat_attacker && elapsed >= 77U))) return unit_pose_hurt;
    if (elapsed >= 28U && elapsed < 46U && unit == s_combat_attacker) {
        return unit_pose_attack;
    }
    if (!s_last_result_missed && elapsed >= 36U && elapsed < 54U &&
        unit == s_combat_target) {
        return unit_pose_hurt;
    }
    if (s_combat_counter && elapsed >= 68U && elapsed < 86U &&
        unit == s_combat_target) {
        return unit_pose_attack;
    }
    if (s_combat_counter && s_last_result_counter_damage > 0 &&
        elapsed >= 76U && elapsed < 94U && unit == s_combat_attacker) {
        return unit_pose_hurt;
    }
    return unit_pose_idle;
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
    for (unsigned binding = 0; binding < g_grandleon_unit_count; ++binding) {
        if (g_grandleon_unit_ids[binding] == unit_id) {
            s_motion.facing_left = s_unit_facing_left[binding];
            break;
        }
    }
    if (found) {
        s_motion.route_count = static_cast<unsigned>(route.size());
    } else {
        s_motion.route_count = 1U;
        s_motion.route[0] = {
            static_cast<std::int16_t>(destination.x),
            static_cast<std::int16_t>(destination.y),
        };
    }
    s_motion.total_frames = s_motion.route_count * board_slide_frames_per_tile;
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
    const unsigned route_index = s_motion.frame / board_slide_frames_per_tile <
        s_motion.route_count
        ? s_motion.frame / board_slide_frames_per_tile
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
    const view::BoardHeading heading = view::board_heading_between(
        from.x, from.y, to.x, to.y
    );
    s_motion.facing_left = view::board_heading_faces_left(
        heading, s_motion.facing_left
    );
    s_unit_facing_left[binding] = s_motion.facing_left;
    const int segment_frame = static_cast<int>(
        s_motion.frame - route_index * board_slide_frames_per_tile
    );
    const int from_x = (from.x * 2 + 1 - s_map_width) * 65536;
    const int to_x = (to.x * 2 + 1 - s_map_width) * 65536;
    const int from_z = (s_map_height - from.y * 2 - 1) * 65536;
    const int to_z = (s_map_height - to.y * 2 - 1) * 65536;
    entity->position[0] = view::slide_between(
        from_x, to_x, segment_frame, board_slide_frames_per_tile
    );
    entity->position[1] = 3072 + view::slide_between(
        terrain_surface(from, s_map_width), terrain_surface(to, s_map_width),
        segment_frame, board_slide_frames_per_tile
    );
    entity->position[1] += 4096 * 4 * segment_frame *
        (board_slide_frames_per_tile - segment_frame) /
        (board_slide_frames_per_tile * board_slide_frames_per_tile);
    entity->position[2] = view::slide_between(
        from_z, to_z, segment_frame, board_slide_frames_per_tile
    );
    apply_unit_art(
        binding, *entity, walking_pose(heading, s_motion.frame - 1U),
        s_motion.facing_left
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
    if ((!s_result_frames && !s_camera_return_frames) || !s_result_close_combat) return;
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
        *attacker_entity, s_combat_attacker_position, snapshot.width, snapshot.height,
        3072, false
    );
    place_entity(
        *target_entity, s_combat_target_position, snapshot.width, snapshot.height,
        3072, false
    );
    // Stage the exchange like the N64 close view: both actors keep fixed
    // screen sides for the whole attack/counter sequence.  The old path left
    // them at tactical-cell spacing, which made the close view look like a
    // distant board crop and could swap their apparent sides with map layout.
    const std::int32_t centre_x =
        (attacker_entity->position[0] + target_entity->position[0]) / 2;
    const std::int32_t centre_z =
        (attacker_entity->position[2] + target_entity->position[2]) / 2;
    attacker_entity->position[0] = centre_x - 2 * 65536;
    attacker_entity->position[2] = centre_z;
    target_entity->position[0] = centre_x + 2 * 65536;
    target_entity->position[2] = centre_z;
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
        (s_result_frames || s_camera_return_frames) && s_result_close_combat;
    if (s_result_frames && s_result_close_combat) {
        const sim::Position a = s_combat_attacker_position;
        const sim::Position b = s_combat_target_position;
        focus_x = ((a.x * 2 + 1 - s_map_width) +
                   (b.x * 2 + 1 - s_map_width)) * 65536 / 2;
        focus_z = ((s_map_height - a.y * 2 - 1) +
                   (s_map_height - b.y * 2 - 1)) * 65536 / 2;
        // Match the authored plate's foreground baseline. The previous
        // camera put feet near the horizon, making actors float over trees.
        desired_height = (g_grandleon_combat_plate_indices[0] ? 11 : 6) * 65536;
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
    camera->position[0] = s_reduced_motion
        ? focus_x : approach(camera->position[0], focus_x);
    camera->position[1] = s_reduced_motion
        ? desired_height : approach(camera->position[1], desired_height);
    camera->position[2] = s_reduced_motion
        ? focus_z + desired_trailing
        : approach(camera->position[2], focus_z + desired_trailing);
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
        combat_floor->mesh_enabled = close_combat && !g_grandleon_combat_plate_indices[0] ? 1 : 0;
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
            (s_unit_on_board[index] &&
             s_unit_visible[index] &&
             unit_x >= camera_x - half_x && unit_x <= camera_x + half_x &&
             unit_z >= camera_focus_z - half_z && unit_z <= camera_focus_z + half_z);
        if (!unit->mesh_enabled) continue;
        unit->rotation[2] = 0;
        if (combat_actor) {
            const bool defender =
                g_grandleon_unit_ids[index] == s_combat_target;
            apply_unit_art(
                index, *unit, combat_pose_for(g_grandleon_unit_ids[index]),
                defender, true
            );
        } else if (!s_motion.active || g_grandleon_unit_ids[index] != s_motion.unit) {
            apply_unit_art(
                index, *unit, unit_pose_idle, s_unit_facing_left[index]
            );
        }
        if (!close_combat && !s_result_frames && !s_camera_return_frames &&
            !(s_motion.active && g_grandleon_unit_ids[index] == s_motion.unit)) {
            // A few small pose updates per second preserve the cached draws
            // between visible changes. The mesh's foot origin remains fixed.
            const auto idle = view::idle_pose(
                (s_ambient_ticks / 2U) & ~3U, g_grandleon_unit_ids[index],
                !s_reduced_motion
            );
            unit->scale[1] = unit->scale[1] * idle.stretch_q12 / 4096;
            unit->rotation[2] = idle.lean_q12 * 917;
        }
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
    for (unsigned index = 0; index < g_grandleon_deployment_entity_count;
         ++index) {
        ps1_entity* marker = grandleon_scene_mutable_entity(
            g_grandleon_deployment_entity_indices[index]
        );
        if (marker) marker->mesh_enabled = snapshot.deploying ? 1 : 0;
    }
    sync_visibility_entities(false);
}

bool advance_board_motion() {
    if (!s_motion.active) return false;
    if (s_motion.frame < s_motion.total_frames) ++s_motion.frame;
    const unsigned completed_steps = s_motion.frame /
        board_slide_frames_per_tile;
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
    // Rules may remove a dying unit from the board immediately. Preserve the
    // authored cells for the entire exchange, including counterattacks.
    if (command.type == sim::CommandType::attack || command.type == sim::CommandType::ability) {
        const auto* actor = find_unit(s_snapshot, command.unit_id);
        const auto* target = find_unit(s_snapshot, command.target_id);
        s_combat_attacker_position = actor ? actor->position : command.destination;
        s_combat_target_position = target ? target->position : command.destination;
    }
    sim::CommandResult result = s_created->encounter.apply(command);
    if (!result) {
        s_motion = {};
        psx::Line().text("MIPSYNC_SIM_REJECT action=").text(action)
            .text(" error=").decimal(static_cast<unsigned>(result.error)).flush();
        return false;
    }
    if (command.type == sim::CommandType::attack || command.type == sim::CommandType::ability) {
        unsigned cue = GRANDLEON_SOUND_BLADE;
        if (command.type == sim::CommandType::ability) {
            cue = GRANDLEON_SOUND_HEAVY;
            for (const auto& ability : s_created->encounter.abilities()) {
                if (ability.id != command.ability_id) continue;
                cue = ability.kind == sim::AbilityKind::restore ? GRANDLEON_SOUND_HEALING
                    : ability.damage_type == sim::DamageType::magical ? GRANDLEON_SOUND_ARCANE : GRANDLEON_SOUND_HEAVY;
                break;
            }
        } else {
            for (const auto& weapon : s_created->encounter.weapons()) {
                if (weapon.id == command.weapon_id) {
                    cue = weapon.minimum_reach > 1U ? GRANDLEON_SOUND_BOW : GRANDLEON_SOUND_BLADE;
                    break;
                }
            }
            if (command.weapon_id == 0U) {
                for (const auto& unit : s_snapshot.units) {
                    if (unit.id == command.unit_id && unit.minimum_reach > 1U) cue = GRANDLEON_SOUND_BOW;
                }
            }
        }
        s_attack_sound_cue = cue;
    } else if (command.type == sim::CommandType::move) grandleon_audio_cue(GRANDLEON_SOUND_MOVE);
    else if (command.type == sim::CommandType::use_item) grandleon_audio_cue(GRANDLEON_SOUND_ITEM);
    else if (command.type == sim::CommandType::talk) grandleon_audio_cue(GRANDLEON_SOUND_TALK);
    queue_event_moments(result.events);
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
    for (const auto& binding : grandleon_generated_behaviors(s_local_players)) {
        if (binding.unit_id == id) return &binding;
    }
    return nullptr;
}

void begin_result(
    const sim::Command& command, const sim::CommandResult& result,
    bool counter, sim::Position centre, bool allow_close_combat = true
) {
    s_combat_attacker = command.unit_id;
    s_combat_target = command.target_id;
    s_combat_centre = centre;
    s_combat_counter = counter;
    s_result_combat_audio = command.type == sim::CommandType::attack ||
        command.type == sim::CommandType::ability;
    const sim::UnitSnapshot* defender = find_unit(s_snapshot, s_combat_target);
    s_counter_sound_cue = defender && defender->minimum_reach > 1U
        ? GRANDLEON_SOUND_BOW : GRANDLEON_SOUND_BLADE;
    s_counter_missed = false;
    s_last_result_missed = false;
    s_last_result_restored = false;
    s_last_result_damage = 0;
    s_last_result_counter_damage = 0;
    for (const sim::Event& event : result.events) {
        if (event.type == sim::EventType::attack_missed &&
            event.related_unit_id == s_combat_target) s_counter_missed = true;
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
    s_result_close_combat = allow_close_combat && s_close_combat_enabled;
    s_result_duration = s_result_close_combat
        ? close_combat_frames : result_only_frames;
    s_result_frames = s_result_duration;
    s_camera_return_frames = 0;
}

void finish_side_if_needed(const sim::EncounterSnapshot& snapshot) {
    const bool was_ai_active = s_ai_active;
    s_ai_active = snapshot.active_side == sim::Side::second &&
        snapshot.outcome == sim::Outcome::ongoing;
    if (was_ai_active && !s_ai_active &&
        snapshot.outcome == sim::Outcome::ongoing) {
        s_cursor = s_player_cursors[s_focus_player];
        psx::Line().text("MIPSYNC_CAMERA_RETURN player=")
            .decimal(s_focus_player + 1U).text(" x=").signed_decimal(s_cursor.x)
            .text(" y=").signed_decimal(s_cursor.y).flush();
    }
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
    const sim::Position actor_position = actor->position;
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
        begin_result(plan.command, result, attack_counter, actor_position);
    } else if (plan.command.type == sim::CommandType::ability) {
        ++s_ability_count;
        begin_result(plan.command, result, false, plan.command.destination);
    }
    ++s_ai_command_count;
    s_cursor = actor_position;
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
            .decimal(s_player_ability_count).text(" p_item=")
            .decimal(s_player_item_count).text(" p_talk=")
            .decimal(s_player_talk_count).text(" ai=")
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

ActionMenu action_menu(
    const sim::UnitSnapshot* actor, const sim::EncounterSnapshot& snapshot
) {
    ActionMenu menu;
    if (!actor) return menu;
    for (const auto& target : snapshot.units) {
        if (sim::forecast_attack(snapshot, actor->id, target.id,
                                 s_created->encounter.weapons(), 0)) {
            menu.append(ActionKind::attack);
            break;
        }
    }
    for (const sim::ContentId ability : actor->ability_ids) {
        menu.append(ActionKind::ability, ability);
    }
    for (std::size_t index = 0; index < actor->item_ids.size() &&
         index < actor->item_counts.size(); ++index) {
        if (actor->item_counts[index] == 0U) continue;
        const sim::ItemForecast forecast = sim::forecast_item(
            snapshot, actor->id, actor->id,
            s_created->encounter.items(), actor->item_ids[index]
        );
        if (forecast &&
            (forecast.kind != sim::ItemKind::restore || forecast.restored > 0)) {
            menu.append(ActionKind::item, actor->item_ids[index]);
        }
    }
    for (const sim::UnitSnapshot& target : snapshot.units) {
        if (sim::forecast_talk(snapshot, actor->id, target.id)) {
            menu.append(ActionKind::talk);
            break;
        }
    }
    menu.append(ActionKind::wait);
    return menu;
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
    std::vector<camp::CampaignOutcomeOperation> operations;
    const std::vector<std::uint64_t> joined = append_recruitment_at(
        source, operations
    );
    const camp::NodeCompletion completion = camp::complete_node(
        s_campaign_save.state, s_campaign_graph,
        outcome_batch(source, std::move(operations))
    );
    if (!completion) {
        psx::Line().text("MIPSYNC_CAMPAIGN_ADVANCE_FAIL error=")
            .decimal(static_cast<unsigned>(completion.error)).flush();
        return false;
    }
    queue_player_names(joined);
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
    const std::vector<std::uint64_t> joined = append_recruitment_at(
        grandleon_generated_current_encounter_id, operations
    );
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
    queue_player_names(joined);
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
    s_battle_moment = {};
    s_action_content = 0U;
    s_result_close_combat = false;
    s_mode = snapshot.deploying ? InputMode::deployment : InputMode::browse;
    s_selected = 0;
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
    s_focus_player = 0U;
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
    if (!snapshot.deploying) {
        queue_battle_moments(1U, 0U, true);
        (void)activate_pending_battle_moment();
    }
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
    load_display_settings();
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
    uint32_t frames, uint32_t rendered_frames, uint32_t scheduled_frames,
    uint32_t deferred_frames,
    uint32_t render_average, uint16_t render_maximum,
    uint32_t ready_average, uint16_t ready_maximum,
    uint16_t mutation_maximum, uint32_t missed_vblanks, int maximum_elapsed,
    uint32_t first_missed_frame, uint32_t last_missed_frame
) {
    psx::Line().text("MIPSYNC_GRANDLEON_METRICS frames=").decimal(frames)
        .text(" rendered_frames=").decimal(rendered_frames)
        .text(" scheduled_frames=").decimal(scheduled_frames)
        .text(" deferred_frames=").decimal(deferred_frames)
        .text(" missed_vblanks=").decimal(missed_vblanks)
        .text(" max_elapsed_vblanks=").signed_decimal(maximum_elapsed).flush();
    psx::Line().text("MIPSYNC_FRAME_BUDGET render_hbl_avg=")
        .decimal(render_average)
        .text(" render_hbl_max=").decimal(render_maximum)
        .text(" ready_hbl_avg=").decimal(ready_average)
        .text(" ready_hbl_max=").decimal(ready_maximum)
        .text(" mutation_hbl_max=").decimal(mutation_maximum).flush();
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

extern "C" int grandleon_game_focus_point(uint8_t index, int32_t* x, int32_t* y, int32_t* z) {
    if (!x || !y || !z || !s_created || !*s_created) return 0;
    const auto* unit = index == 0 ? find_unit(s_snapshot, s_selected) : unit_at(s_snapshot, s_cursor);
    if (index == 1 && s_mode == InputMode::action && s_selected) {
        for (const auto& target : s_snapshot.units) {
            if (sim::forecast_attack(s_snapshot, s_selected, target.id,
                                     s_created->encounter.weapons(), 0)) {
                unit = &target;
                break;
            }
        }
    }
    if (!unit || !sim::on_board(*unit)) return 0;
    *x = (unit->position.x * 2 + 1 - s_map_width) * 65536;
    *y = terrain_surface(unit->position, s_map_width) + 65536;
    *z = (s_map_height - unit->position.y * 2 - 1) * 65536;
    return 1;
}

extern "C" uint8_t grandleon_game_movement_edges(uint16_t index) {
    return index < s_movement_edges.size() ? s_movement_edges[index] : 0;
}

extern "C" int grandleon_game_movement_tile(uint16_t index, int32_t* x, int32_t* y, int32_t* z) {
    if (index >= s_movement_tiles.size() || !x || !y || !z) return 0;
    const auto tile = s_movement_tiles[index];
    *x = (tile.x * 2 + 1 - s_map_width) * 65536;
    *y = terrain_surface(tile, s_map_width) + 4096;
    *z = (s_map_height - tile.y * 2 - 1) * 65536;
    return 1;
}

extern "C" uint16_t grandleon_game_map_controller(uint16_t pressed) {
    // Hardware directions follow the projected board. Recorded/replay commands
    // enter grandleon_runtime_update directly and retain logical coordinates.
    if (s_campaign_mode == CampaignMode::battle &&
        (s_mode == InputMode::browse || s_mode == InputMode::move ||
         s_mode == InputMode::attack || s_mode == InputMode::ability ||
         s_mode == InputMode::talk || s_mode == InputMode::deployment)) {
        const auto vertical = pressed & (pad_up | pad_down);
        pressed &= ~(pad_up | pad_down);
        if (vertical & pad_up) pressed |= pad_down;
        if (vertical & pad_down) pressed |= pad_up;
    }
    return pressed;
}

static const char* story_page_body(std::string body) {
    const auto* node = active_campaign_node();
    const std::uint64_t id = node ? node->id : 0;
    if (s_story_page_mode != s_campaign_mode || s_story_page_node != id ||
        s_story_page_language != s_language_index || std::string_view{s_story_body.empty() ? "" : s_story_body.data()} != body) {
        s_story_page_offset = 0;
        s_story_page_mode = s_campaign_mode;
        s_story_page_node = id;
        s_story_page_language = s_language_index;
        s_story_body.assign(body.begin(), body.end());
        s_story_body.push_back(0);
    }
    return s_story_body.data() + s_story_page_offset;
}

extern "C" void grandleon_game_ui_state(grandleon_ui_state* output) {
    if (!output) return;
    *output = {};
    if (s_campaign_mode != CampaignMode::prologue && s_campaign_mode != CampaignMode::objective &&
        s_campaign_mode != CampaignMode::aftermath) {
        s_story_page_offset = 0;
        s_story_page_mode = s_campaign_mode;
    }
    output->talking_phase = static_cast<std::uint8_t>((s_ambient_ticks / 12U) & 1U);
    if (s_mode == InputMode::move && s_selected && !s_motion.active) {
        if (s_movement_tiles_unit != s_selected) {
            s_movement_tiles = sim::reachable_tiles(s_snapshot, s_selected);
            s_movement_edges.assign(s_movement_tiles.size(), 0);
            // Draw the boundary of the exact reachable set, including holes.
            // Shared interior edges cost packets without conveying more range.
            static const int adjacent[4][2] = {{0,1},{1,0},{0,-1},{-1,0}};
            for (std::size_t index = 0; index < s_movement_tiles.size(); ++index) {
                for (unsigned edge = 0; edge < 4; ++edge) {
                    const sim::Position neighbour{
                        static_cast<std::int16_t>(s_movement_tiles[index].x + adjacent[edge][0]),
                        static_cast<std::int16_t>(s_movement_tiles[index].y + adjacent[edge][1])
                    };
                    if (std::find(s_movement_tiles.begin(), s_movement_tiles.end(), neighbour) == s_movement_tiles.end())
                        s_movement_edges[index] |= 1U << edge;
                }
            }
            s_movement_tiles_unit = s_selected;
        }
        output->movement_tile_count = static_cast<std::uint16_t>(s_movement_tiles.size());
    } else {
        s_movement_tiles_unit = 0;
    }
    output->close_combat = s_close_combat_enabled ? 1 : 0;
    output->combat_view_active = (s_result_frames || s_camera_return_frames) &&
        s_result_close_combat ? 1 : 0;
    output->cursor_on_left = s_cursor.x < s_map_width / 2 ? 1 : 0;
    output->camera_settled = s_camera_settle_frames >= 8 ? 1 : 0;
    output->campaign_menu_index = static_cast<std::uint8_t>(s_campaign_menu_index);
    output->continue_available = s_continue_available ? 1 : 0;
    output->pause_menu_index = static_cast<std::uint8_t>(s_pause_menu_index);
    output->settings_menu_index = static_cast<std::uint8_t>(s_settings_menu_index);
    output->settings_menu_count = static_cast<std::uint8_t>(settings_menu_count());
    output->reduced_motion = s_reduced_motion ? 1 : 0;
    output->show_fps = s_display_options.show_fps ? 1 : 0;
    output->music_volume = static_cast<std::uint8_t>(s_music_volume);
    output->effects_volume = static_cast<std::uint8_t>(s_effects_volume);
    output->measured_fps = s_measured_fps;
    output->language_code = console::language_code(game_language());
    if (s_campaign_mode != CampaignMode::battle) {
        if (g_grandleon_unit_count) output->portrait_texture = static_cast<std::uint16_t>(g_grandleon_unit_pose_texture_indices[0][unit_pose_idle]);
        const grandleon_generated_campaign_node* node = active_campaign_node();
        const char* node_name = node
            ? grandleon_generated_campaign_node_name_for(
                  s_language_index, node->id
              )
            : "";
        const char* node_dialogue = node
            ? grandleon_generated_campaign_node_dialogue_for(
                  s_language_index, node->id
              )
            : "";
        output->headline = grandleon_generated_project_title_for(
            s_language_index
        );
        switch (s_campaign_mode) {
            case CampaignMode::title:
                output->mode = GRANDLEON_UI_TITLE;
                output->body = ui(console::Message::campaign_tagline);
                output->campaign_menu_count = s_continue_available ? 4 : 3;
                break;
            case CampaignMode::players:
                output->mode = GRANDLEON_UI_PLAYERS;
                output->body = ui(console::Message::choose_how_to_play);
                output->campaign_menu_count = 2;
                break;
            case CampaignMode::name: {
                output->mode = GRANDLEON_UI_NAME;
                const auto* member = grandleon_generated_campaign_member_for(
                    s_name_member
                );
                output->headline = member
                    ? authored_member_name(*member)
                    : ui(console::Message::character_name);
                output->body = ui(console::Message::choose_name);
                output->name_value = s_name_editor
                    ? s_name_editor->c_str() : "";
                output->name_default = member
                    ? authored_member_name(*member) : "";
                output->name_row = s_name_editor
                    ? static_cast<std::uint8_t>(s_name_editor->row()) : 0U;
                output->name_column = s_name_editor
                    ? static_cast<std::uint8_t>(s_name_editor->column()) : 0U;
                output->name_invalid = s_name_editor && s_name_editor->invalid()
                    ? 1U : 0U;
                output->name_limit = static_cast<std::uint8_t>(
                    client::console_player_name_glyphs
                );
                break;
            }
            case CampaignMode::prologue:
                output->mode = GRANDLEON_UI_STORY;
                keep_ui_text(s_ui_headline, substitute_player_names(
                    node_name[0] ? node_name : ui(console::Message::story)
                ));
                output->headline = s_ui_headline.data();
                output->body = story_page_body(substitute_player_names(node_dialogue));
                if (node && node->id != s_logged_member_text_node &&
                    std::string_view{s_story_body.data()} !=
                        std::string_view{node_dialogue}) {
                    s_logged_member_text_node = node->id;
                    psx::Line().text("MIPSYNC_MEMBER_TEXT node=")
                        .hex64(node->id).text(" value=")
                        .text(s_story_body.data()).flush();
                }
                break;
            case CampaignMode::objective:
                output->mode = GRANDLEON_UI_OBJECTIVE;
                keep_ui_text(s_ui_headline, substitute_player_names(
                    node_name[0]
                        ? node_name : ui(console::Message::battle_heading)
                ));
                output->headline = s_ui_headline.data();
                output->body = story_page_body(substitute_player_names(
                    grandleon_generated_objective_for(s_language_index)
                ));
                break;
            case CampaignMode::aftermath:
                output->mode = GRANDLEON_UI_AFTERMATH;
                keep_ui_text(s_ui_headline, substitute_player_names(
                    node && node->kind == 2U
                    ? ui(console::Message::campaign_ended)
                    : node_name[0] ? node_name
                    : s_campaign_victory ? ui(console::Message::story)
                    : ui(console::Message::defeat)
                ));
                output->body = story_page_body(substitute_player_names(
                    node && node->kind == 2U
                        ? ui(console::Message::return_to_title)
                    : node_dialogue[0] ? node_dialogue
                        : (s_campaign_victory
                              ? ui(console::Message::campaign_complete)
                              : ui(console::Message::company_regroup))
                ));
                output->headline = s_ui_headline.data();
                break;
            case CampaignMode::save_error:
                output->mode = GRANDLEON_UI_SAVE_ERROR;
                output->headline = ui(console::Message::memory_card);
                output->body = ui(console::Message::save_unavailable);
                break;
            case CampaignMode::help:
                output->mode = GRANDLEON_UI_TITLE_HELP;
                output->headline = ui(console::Message::help);
                output->body = ui(console::Message::how_to_play);
                break;
            case CampaignMode::settings:
                output->mode = GRANDLEON_UI_TITLE_SETTINGS;
                output->headline = ui(console::Message::settings);
                output->body = ui(console::Message::presentation_options);
                break;
            case CampaignMode::battle: break;
        }
        if (output->mode == GRANDLEON_UI_STORY || output->mode == GRANDLEON_UI_AFTERMATH ||
            output->mode == GRANDLEON_UI_OBJECTIVE)
            output->has_more_text = *grandleon_text_page_end(output->body, 32, 10) != 0;
        return;
    }
    if (!s_created || !*s_created) return;
    const sim::EncounterSnapshot& snapshot = s_snapshot;
    if (const auto* line = current_battle_moment_line()) {
        output->mode = GRANDLEON_UI_BATTLE_MOMENT;
        const std::string_view speaker = line->speaker;
        const auto speaker_matches = [speaker](const char* name) {
            if (speaker.empty()) return false;
            const std::string_view full = name;
            const auto position = full.find(speaker);
            return position != std::string_view::npos &&
                (position == 0 || full[position - 1] == ' ') &&
                (position + speaker.size() == full.size() || full[position + speaker.size()] == ' ');
        };
        for (const auto& member : grandleon_generated_campaign_members()) {
            if (!speaker_matches(authored_member_name(member)) &&
                !speaker_matches(effective_member_name(member))) continue;
            for (const auto& unit : snapshot.units) {
                if (unit.unit_type_id != member.unit_type_id) continue;
                for (unsigned binding = 0; binding < g_grandleon_unit_count; ++binding) {
                    if (g_grandleon_unit_ids[binding] == unit.id) output->portrait_texture = static_cast<std::uint16_t>(g_grandleon_unit_pose_texture_indices[binding][unit_pose_idle]);
                }
                break;
            }
        }
        keep_ui_text(s_ui_headline, substitute_player_names(line->speaker));
        const auto body = substitute_player_names(line->text);
        s_battle_body.assign(body.begin(), body.end());
        s_battle_body.push_back(0);
        output->headline = s_ui_headline.data();
        output->body = s_battle_body.data() + s_battle_moment.page_offset;
        output->has_more_text = *grandleon_text_page_end(output->body, 34, 3) != 0;
    } else if (s_mode == InputMode::pause) output->mode = GRANDLEON_UI_PAUSE;
    else if (s_mode == InputMode::help) output->mode = GRANDLEON_UI_BATTLE_HELP;
    else if (s_mode == InputMode::settings) {
        output->mode = GRANDLEON_UI_BATTLE_SETTINGS;
    } else if (snapshot.deploying) {
        output->mode = GRANDLEON_UI_DEPLOYMENT;
    } else if (snapshot.outcome != sim::Outcome::ongoing) {
        output->mode = GRANDLEON_UI_COMPLETE;
    } else if (s_result_frames || s_camera_return_frames) output->mode = GRANDLEON_UI_RESULT;
    else if (s_ai_active) output->mode = GRANDLEON_UI_OPPONENT;
    // The rules commit the destination before its presentation finishes, so
    // `s_mode` has already advanced to the action menu while the actor is
    // still crossing the board.  Do not cover that movement with a menu the
    // player cannot use yet; reveal it when the last footfall settles.
    else if (s_motion.active) output->mode = GRANDLEON_UI_BROWSE;
    else if (s_mode == InputMode::move) output->mode = GRANDLEON_UI_MOVE;
    else if (s_mode == InputMode::action) output->mode = GRANDLEON_UI_ACTION;
    else if (s_mode == InputMode::attack) output->mode = GRANDLEON_UI_ATTACK;
    else if (s_mode == InputMode::ability) output->mode = GRANDLEON_UI_ABILITY;
    else if (s_mode == InputMode::talk) output->mode = GRANDLEON_UI_TALK;
    else output->mode = GRANDLEON_UI_BROWSE;

    const sim::UnitSnapshot* actor = find_unit(snapshot, s_selected);
    if (!actor) actor = unit_at(snapshot, s_cursor);
    if (actor) {
        output->actor_health = actor->health;
        output->actor_maximum = actor->maximum_health;
        output->ability_count = static_cast<std::uint8_t>(actor->ability_ids.size());
        output->local_player = actor->local_player;
        output->menu_count = static_cast<std::uint8_t>(
            action_menu(actor, snapshot).count
        );
    }
    output->menu_index = static_cast<std::uint8_t>(s_menu_index);
    output->deployment_selected = s_selected != 0U ? 1U : 0U;
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
    } else if (s_mode == InputMode::ability && actor && s_action_content != 0U) {
        const auto ability = std::find(
            actor->ability_ids.begin(), actor->ability_ids.end(), s_action_content
        );
        if (ability != actor->ability_ids.end()) {
            output->ability_index = static_cast<std::uint8_t>(
                std::distance(actor->ability_ids.begin(), ability)
            );
        }
        const sim::UnitSnapshot* target = unit_at(snapshot, s_cursor);
        const sim::UnitId affected = target ? target->id : actor->id;
        const sim::AbilityForecast forecast = sim::forecast_ability(
            snapshot, actor->id, s_action_content,
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

extern "C" uint8_t grandleon_game_action_kind(uint8_t index) {
    if (!s_created || !*s_created) return GRANDLEON_ACTION_WAIT;
    const sim::UnitSnapshot* actor = find_unit(s_snapshot, s_selected);
    const ActionMenu menu = action_menu(actor, s_snapshot);
    if (index >= menu.count) return GRANDLEON_ACTION_WAIT;
    switch (menu.choices[index].kind) {
        case ActionKind::attack: return GRANDLEON_ACTION_ATTACK;
        case ActionKind::ability: return GRANDLEON_ACTION_ABILITY;
        case ActionKind::item: return GRANDLEON_ACTION_ITEM;
        case ActionKind::talk: return GRANDLEON_ACTION_TALK;
        case ActionKind::wait: return GRANDLEON_ACTION_WAIT;
    }
    return GRANDLEON_ACTION_WAIT;
}

extern "C" const char* grandleon_game_text(uint8_t text_id) {
    switch (text_id) {
        case GRANDLEON_TEXT_NEW_GAME:
            return ui(console::Message::new_game);
        case GRANDLEON_TEXT_CONTINUE:
            return ui(console::Message::continue_game);
        case GRANDLEON_TEXT_HELP:
            return ui(console::Message::help);
        case GRANDLEON_TEXT_SETTINGS:
            return ui(console::Message::settings);
        case GRANDLEON_TEXT_ONE_PLAYER:
            return ui(console::Message::one_player);
        case GRANDLEON_TEXT_TWO_PLAYERS:
            return ui(console::Message::two_players);
        case GRANDLEON_TEXT_CLOSE_VIEW:
            return ui(console::Message::close_view);
        case GRANDLEON_TEXT_REDUCED_MOTION:
            return ui(console::Message::reduced_motion);
        case GRANDLEON_TEXT_SHOW_FPS:
            return ui(console::Message::show_fps);
        case GRANDLEON_TEXT_LANGUAGE:
            return ui(console::Message::language);
        case GRANDLEON_TEXT_MUSIC:
            return ui(console::Message::music_volume);
        case GRANDLEON_TEXT_EFFECTS:
            return ui(console::Message::effects_volume);
        case GRANDLEON_TEXT_ON:
            return ui(console::Message::on);
        case GRANDLEON_TEXT_OFF:
            return ui(console::Message::off);
        case GRANDLEON_TEXT_BACK:
            return ui(console::Message::back_button);
        case GRANDLEON_TEXT_MENU:
            return ui(console::Message::menu);
        case GRANDLEON_TEXT_RESUME:
            return ui(console::Message::resume);
        case GRANDLEON_TEXT_OPPONENT_TURN:
            return ui(console::Message::opponent_turn);
        case GRANDLEON_TEXT_CHARACTER_NAME:
            return ui(console::Message::character_name);
        case GRANDLEON_TEXT_CHOOSE_NAME:
            return ui(console::Message::choose_name);
        case GRANDLEON_TEXT_REQUIRED:
            return ui(console::Message::required);
        case GRANDLEON_TEXT_TYPE_NAME:
            return ui(console::Message::type_name);
        case GRANDLEON_TEXT_ERASE:
            return ui(console::Message::erase);
        case GRANDLEON_TEXT_KEEP:
            return ui(console::Message::keep);
        case GRANDLEON_TEXT_SAVE:
            return ui(console::Message::save_action);
        case GRANDLEON_TEXT_MOVE_CURSOR:
            return ui(console::Message::move_cursor);
        case GRANDLEON_TEXT_CROSS_CONFIRM:
            return ui(console::Message::cross_confirm);
        case GRANDLEON_TEXT_CIRCLE_BACK:
            return ui(console::Message::circle_back);
        case GRANDLEON_TEXT_START_MENU:
            return ui(console::Message::start_menu);
        case GRANDLEON_TEXT_CROSS_CIRCLE_RETURN:
            return ui(console::Message::cross_circle_return);
        case GRANDLEON_TEXT_ENCOUNTER_CLEAR:
            return ui(console::Message::encounter_clear);
        case GRANDLEON_TEXT_ATTACK:
            return ui(console::Message::attack);
        case GRANDLEON_TEXT_WAIT:
            return ui(console::Message::wait);
        case GRANDLEON_TEXT_ABILITY:
            return ui(console::Message::ability);
        case GRANDLEON_TEXT_ITEM:
            return ui(console::Message::inventory);
        case GRANDLEON_TEXT_TALK:
            return ui(console::Message::talk);
        case GRANDLEON_TEXT_ACTION:
            return ui(console::Message::action_heading);
        case GRANDLEON_TEXT_MOVE:
            return ui(console::Message::walk);
        case GRANDLEON_TEXT_HIT:
            return ui(console::Message::hit);
        case GRANDLEON_TEXT_MEND:
            return ui(console::Message::mend);
        case GRANDLEON_TEXT_COUNTER:
            return ui(console::Message::counter);
        case GRANDLEON_TEXT_MISS:
            return ui(console::Message::miss);
        case GRANDLEON_TEXT_RESTORED:
            return ui(console::Message::restored);
        case GRANDLEON_TEXT_DAMAGE:
            return ui(console::Message::damage);
        case GRANDLEON_TEXT_RETURN_TO_TITLE:
            return ui(console::Message::return_to_title);
        case GRANDLEON_TEXT_COMPANY_REGROUP:
            return ui(console::Message::company_regroup);
        case GRANDLEON_TEXT_MEMORY_CARD:
            return ui(console::Message::memory_card);
        case GRANDLEON_TEXT_SAVE_UNAVAILABLE:
            return ui(console::Message::save_unavailable);
        case GRANDLEON_TEXT_CAMPAIGN_TAGLINE:
            return ui(console::Message::campaign_tagline);
        case GRANDLEON_TEXT_DEPLOYMENT:
            return ui(console::Message::deployment);
        case GRANDLEON_TEXT_SELECT:
            return ui(console::Message::select);
        case GRANDLEON_TEXT_DEPLOY:
            return ui(console::Message::deploy);
        case GRANDLEON_TEXT_BEGIN:
            return ui(console::Message::begin);
    }
    return "";
}

extern "C" const char* grandleon_game_name_key(
    uint8_t row, uint8_t column
) {
    return client::ConsoleNameEditor::key(row, column).label;
}

extern "C" void grandleon_game_set_fps(uint16_t fps) {
    s_measured_fps = fps;
}

int grandleon_game_update(uint16_t player_one, uint16_t player_two) {
    ++s_ambient_ticks;
    std::uint16_t pressed = player_one;
    grandleon_audio_theme(s_campaign_mode == CampaignMode::battle ? 1U
        : s_campaign_mode == CampaignMode::prologue || s_campaign_mode == CampaignMode::aftermath ? 2U : 0U);
    if ((player_one | player_two) & pad_cross) grandleon_audio_cue(GRANDLEON_SOUND_CONFIRM);
    if (s_campaign_mode != CampaignMode::battle) {
        if (!pressed) return 0;
        if (s_campaign_mode == CampaignMode::title) {
            const unsigned count = s_continue_available ? 4U : 3U;
            if (pressed & pad_up) {
                s_campaign_menu_index = s_campaign_menu_index == 0U
                    ? count - 1U : s_campaign_menu_index - 1U;
            }
            if (pressed & pad_down) {
                s_campaign_menu_index = (s_campaign_menu_index + 1U) % count;
            }
            if (pressed & pad_cross) {
                if (s_campaign_menu_index == 0) {
                    s_campaign_menu_index = 0;
                    if (grandleon_generated_player_mode == 1U) {
                        begin_new_campaign(1U);
                    } else if (grandleon_generated_player_mode == 2U) {
                        begin_new_campaign(2U);
                    } else {
                        s_campaign_mode = CampaignMode::players;
                    }
                } else if (s_continue_available && s_campaign_menu_index == 1U) {
                    route_active_campaign_node();
                } else if (s_campaign_menu_index ==
                           (s_continue_available ? 2U : 1U)) {
                    s_campaign_mode = CampaignMode::help;
                } else {
                    s_settings_menu_index = 0U;
                    s_campaign_mode = CampaignMode::settings;
                }
            }
            return 0;
        }
        if (s_campaign_mode == CampaignMode::help) {
            if (pressed & (pad_cross | pad_circle | pad_start)) {
                s_campaign_menu_index = 0U;
                s_campaign_mode = CampaignMode::title;
            }
            return 0;
        }
        if (s_campaign_mode == CampaignMode::settings) {
            const unsigned count = settings_menu_count();
            if (pressed & pad_up) {
                s_settings_menu_index = s_settings_menu_index == 0U
                    ? count - 1U : s_settings_menu_index - 1U;
            }
            if (pressed & pad_down) {
                s_settings_menu_index = (s_settings_menu_index + 1U) % count;
            }
            if (pressed & pad_cross) toggle_setting(s_settings_menu_index);
            if ((s_settings_menu_index == 2U || s_settings_menu_index == 3U) &&
                (pressed & (pad_left | pad_right))) {
                toggle_setting(s_settings_menu_index, (pressed & pad_left) ? -1 : 1);
            }
            if (pressed & (pad_circle | pad_start)) {
                s_campaign_menu_index = 0U;
                s_campaign_mode = CampaignMode::title;
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
        if (s_campaign_mode == CampaignMode::name) {
            if (!s_name_editor) {
                s_campaign_mode = CampaignMode::save_error;
                return 0;
            }
            if (pressed & pad_triangle) {
                finish_player_name(client::ConsoleNameAnswer::cancel);
            } else if (pressed & pad_circle) {
                s_name_editor->erase();
            } else if (pressed & pad_start) {
                const client::ConsoleNameAnswer answer = s_name_editor->save();
                if (answer != client::ConsoleNameAnswer::editing) {
                    finish_player_name(answer);
                }
            } else if (pressed & pad_cross) {
                const client::ConsoleNameAnswer answer =
                    s_name_editor->activate();
                if (answer == client::ConsoleNameAnswer::use_default) {
                    finish_player_name(answer);
                }
            } else {
                s_name_editor->move(
                    (pressed & pad_right) ? 1 : (pressed & pad_left) ? -1 : 0,
                    (pressed & pad_down) ? 1 : (pressed & pad_up) ? -1 : 0
                );
            }
            return 0;
        }
        if ((pressed & pad_cross) &&
            (s_campaign_mode == CampaignMode::prologue || s_campaign_mode == CampaignMode::objective ||
             s_campaign_mode == CampaignMode::aftermath)) {
            grandleon_ui_state page{};
            grandleon_game_ui_state(&page);
            if (page.has_more_text) {
                const char* next = grandleon_text_page_end(page.body, 32, 10);
                s_story_page_offset += static_cast<std::size_t>(next - page.body);
                psx::Line().text("MIPSYNC_STORY_PAGE offset=")
                    .decimal(static_cast<unsigned>(s_story_page_offset)).flush();
                return 0;
            }
        }
        if (s_campaign_mode == CampaignMode::prologue && (pressed & pad_cross)) {
            if (advance_story()) {
                s_continue_available = true;
                route_after_player_names();
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
                    enter_after_player_names(CampaignMode::aftermath);
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
                route_after_player_names();
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
    if (s_battle_moment.active) {
        if (player_one & (pad_cross | pad_start)) advance_battle_moment();
        return 0;
    }
    if (s_snapshot.deploying) {
        std::uint8_t input_player = 0U;
        if (s_selected != 0U) {
            const sim::UnitSnapshot* selected = find_unit(s_snapshot, s_selected);
            if (selected && selected->local_player == 2U && s_local_players > 1U) {
                input_player = 1U;
                pressed = player_two;
            }
        } else if (s_local_players > 1U && player_two != 0U) {
            input_player = 1U;
            pressed = player_two;
        }
        if (pressed == 0U) return 0;
        s_focus_player = input_player;
        s_cursor = s_player_cursors[input_player];
        move_cursor(pressed, s_snapshot);
        bool mutated = false;
        if (pressed & pad_circle) s_selected = 0U;
        if (pressed & pad_start) {
            sim::Command command{};
            command.type = sim::CommandType::begin_battle;
            if (apply_command(command, "begin_battle", nullptr, nullptr)) {
                s_selected = 0U;
                s_mode = InputMode::browse;
                queue_battle_moments(1U, 0U, true);
                (void)activate_pending_battle_moment();
                psx::Line().text("MIPSYNC_DEPLOYMENT_END").flush();
                mutated = true;
            }
        } else if (pressed & pad_cross) {
            const sim::UnitSnapshot* occupant = unit_at(s_snapshot, s_cursor);
            if (occupant && sim::is_deployable(s_snapshot, *occupant) &&
                occupant->local_player == input_player + 1U) {
                s_selected = occupant->id;
                psx::Line().text("MIPSYNC_DEPLOYMENT_SELECT unit=")
                    .hex64(occupant->id).flush();
            } else if (s_selected != 0U && occupant == nullptr) {
                sim::Command command{};
                command.type = sim::CommandType::deploy;
                command.unit_id = s_selected;
                command.destination = s_cursor;
                if (apply_command(command, "deploy", nullptr, nullptr)) {
                    psx::Line().text("MIPSYNC_DEPLOYMENT_PLACE unit=")
                        .hex64(s_selected).text(" x=")
                        .signed_decimal(s_cursor.x).text(" y=")
                        .signed_decimal(s_cursor.y).flush();
                    s_selected = 0U;
                    mutated = true;
                }
            }
        }
        s_player_cursors[input_player] = s_cursor;
        sync_scene(s_snapshot);
        return mutated ? 1 : 0;
    }
    if (s_mode == InputMode::pause) {
        if (player_one & pad_up) {
            s_pause_menu_index = s_pause_menu_index == 0U
                ? 2U : s_pause_menu_index - 1U;
        }
        if (player_one & pad_down) {
            s_pause_menu_index = (s_pause_menu_index + 1U) % 3U;
        }
        if (player_one & (pad_circle | pad_start)) s_mode = InputMode::browse;
        else if (player_one & pad_cross) {
            if (s_pause_menu_index == 0U) s_mode = InputMode::browse;
            else if (s_pause_menu_index == 1U) {
                s_mode = InputMode::help;
                psx::Line().text("MIPSYNC_BATTLE_MENU screen=help").flush();
            }
            else {
                s_settings_menu_index = 0U;
                s_mode = InputMode::settings;
                psx::Line().text("MIPSYNC_BATTLE_MENU screen=settings").flush();
            }
        }
        return 0;
    }
    if (s_mode == InputMode::help) {
        if (player_one & (pad_cross | pad_circle | pad_start)) {
            s_mode = InputMode::pause;
        }
        return 0;
    }
    if (s_mode == InputMode::settings) {
        const unsigned count = settings_menu_count();
        if (player_one & pad_up) {
            s_settings_menu_index = s_settings_menu_index == 0U
                ? count - 1U : s_settings_menu_index - 1U;
        }
        if (player_one & pad_down) {
            s_settings_menu_index = (s_settings_menu_index + 1U) % count;
        }
        if (player_one & pad_cross) toggle_setting(s_settings_menu_index);
        if ((s_settings_menu_index == 2U || s_settings_menu_index == 3U) &&
            (player_one & (pad_left | pad_right))) {
            toggle_setting(s_settings_menu_index, (player_one & pad_left) ? -1 : 1);
        }
        if (player_one & (pad_circle | pad_start)) s_mode = InputMode::pause;
        return 0;
    }
    if ((player_one & pad_start) && s_mode == InputMode::browse &&
        !s_result_frames && !s_camera_return_frames && !s_ai_active) {
        s_pause_menu_index = 0U;
        s_mode = InputMode::pause;
        psx::Line().text("MIPSYNC_BATTLE_MENU screen=pause").flush();
        return 0;
    }
    if (s_snapshot.outcome != sim::Outcome::ongoing &&
        !s_campaign_completion_handled && !s_result_frames &&
        !s_camera_return_frames && s_battle_moment.dialogue_ids.empty()) {
        s_campaign_completion_handled = true;
        s_campaign_victory = s_snapshot.outcome == sim::Outcome::first_side_won;
        if (complete_battle(s_campaign_victory)) {
            hide_board();
            enter_after_player_names(CampaignMode::aftermath);
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
            enter_after_player_names(CampaignMode::aftermath);
        } else {
            s_campaign_mode = CampaignMode::save_error;
        }
        return 1;
    }
#endif
    if (s_result_frames) {
        const unsigned elapsed = s_result_duration - s_result_frames;
        const unsigned launch = s_result_close_combat ? 28U : 0U;
        const unsigned impact = s_result_close_combat ? 37U : 8U;
        if (s_result_combat_audio && elapsed == launch) grandleon_audio_cue(s_attack_sound_cue);
        if ((s_result_combat_audio || s_last_result_restored) && elapsed == impact) grandleon_audio_cue(s_last_result_restored
            ? GRANDLEON_SOUND_RESTORE : s_last_result_missed ? GRANDLEON_SOUND_MISS : GRANDLEON_SOUND_IMPACT);
        if (s_combat_counter && elapsed == (s_result_close_combat ? 68U : 24U)) grandleon_audio_cue(s_counter_sound_cue);
        if (s_combat_counter && elapsed == (s_result_close_combat ? 77U : 32U)) grandleon_audio_cue(s_counter_missed
            ? GRANDLEON_SOUND_MISS : GRANDLEON_SOUND_IMPACT);
        --s_result_frames;
        if (s_result_frames == 0) {
            // Restore tactical actors and camera together. Moving the camera
            // while actors remain on the close-view stage made them drift away.
            sync_scene(s_snapshot);
            finish_side_if_needed(s_snapshot);
            s_selected = 0;
            s_mode = InputMode::browse;
            if (s_result_close_combat) {
                const bool reduced = s_reduced_motion;
                s_reduced_motion = true;
                tick_camera();
                s_reduced_motion = reduced;
            }
            (void)activate_pending_battle_moment();
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
            if (s_result_close_combat) {
                const bool reduced = s_reduced_motion;
                s_reduced_motion = true;
                tick_camera();
                s_reduced_motion = reduced;
            }
            (void)activate_pending_battle_moment();
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
    s_focus_player = input_player;
    s_cursor = s_player_cursors[input_player];
    sim::EncounterSnapshot snapshot = s_snapshot;
    if (snapshot.active_side != sim::Side::first ||
        snapshot.outcome != sim::Outcome::ongoing) return 0;

    if (s_mode != InputMode::action) move_cursor(pressed, snapshot);
    if ((pressed & pad_triangle) && s_mode == InputMode::browse) {
        toggle_setting(0U);
    }
    if (pressed & pad_circle) {
        if (s_mode == InputMode::attack || s_mode == InputMode::ability ||
            s_mode == InputMode::talk) {
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
            s_action_content = 0U;
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
        const ActionMenu menu = action_menu(actor, snapshot);
        const unsigned count = menu.count;
        if ((pressed & pad_up) && count) {
            s_menu_index = s_menu_index == 0 ? count - 1 : s_menu_index - 1;
        }
        if ((pressed & pad_down) && count) {
            s_menu_index = (s_menu_index + 1) % count;
        }
        if (pressed & (pad_up | pad_down)) {
            psx::Line().text("MIPSYNC_ACTION_NAV row=")
                .decimal(s_menu_index).text(" count=").decimal(count).flush();
        }
        if ((pressed & pad_cross) && actor && count) {
            const ActionChoice choice = menu.choices[s_menu_index];
            const char* kind = choice.kind == ActionKind::attack ? "attack"
                : choice.kind == ActionKind::ability ? "ability"
                : choice.kind == ActionKind::item ? "item"
                : choice.kind == ActionKind::talk ? "talk" : "wait";
            psx::Line().text("MIPSYNC_ACTION_CHOICE kind=").text(kind)
                .text(" row=").decimal(s_menu_index).flush();
            if (choice.kind == ActionKind::attack) {
                s_mode = InputMode::attack;
                for (const auto& target : snapshot.units) {
                    if (sim::forecast_attack(snapshot, actor->id, target.id,
                                             s_created->encounter.weapons(), 0)) {
                        s_cursor = target.position;
                        break;
                    }
                }
            } else if (choice.kind == ActionKind::ability) {
                s_action_content = choice.content;
                s_mode = InputMode::ability;
                s_cursor = actor->position;
            } else if (choice.kind == ActionKind::item) {
                const sim::ItemForecast forecast = sim::forecast_item(
                    snapshot, actor->id, actor->id,
                    s_created->encounter.items(), choice.content
                );
                if (forecast) {
                    sim::Command command{};
                    command.type = sim::CommandType::use_item;
                    command.unit_id = actor->id;
                    command.target_id = actor->id;
                    command.item_id = choice.content;
                    sim::CommandResult result;
                    if (apply_command(command, "item", &result, &snapshot)) {
                        ++s_player_item_count;
                        begin_result(
                            command, result, false, actor->position, false
                        );
                        mutated = true;
                    }
                }
            } else if (choice.kind == ActionKind::talk) {
                s_mode = InputMode::talk;
                s_cursor = actor->position;
            } else if (apply_wait(actor->id, "wait", &snapshot)) {
                ++s_wait_count;
                s_selected = 0;
                s_mode = InputMode::browse;
                mutated = true;
                finish_side_if_needed(snapshot);
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
        if (actor && s_action_content != 0U) {
            const sim::ContentId ability = s_action_content;
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
    } else if ((pressed & pad_cross) && s_mode == InputMode::talk) {
        const sim::UnitSnapshot* actor = find_unit(snapshot, s_selected);
        const sim::UnitSnapshot* target = unit_at(snapshot, s_cursor);
        if (actor && target && sim::forecast_talk(snapshot, actor->id, target->id)) {
            sim::Command command{};
            command.type = sim::CommandType::talk;
            command.unit_id = actor->id;
            command.target_id = target->id;
            if (apply_command(command, "talk", nullptr, &snapshot)) {
                ++s_player_talk_count;
                s_selected = 0;
                s_mode = InputMode::browse;
                mutated = true;
                finish_side_if_needed(snapshot);
                (void)activate_pending_battle_moment();
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
