// SPDX-License-Identifier: MIT
#include "script/globalScript.h"

#include <grandleon/client/campaign_session.hpp>
#include <grandleon/client/console_localization.hpp>
#include <grandleon/client/console_name_editor.hpp>
#include <grandleon/client/session.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/package_runtime/localization.hpp>
#include <grandleon/package_runtime/names.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/storage/byte_window_storage.hpp>
#include <libdragon.h>
#include <t3d/t3d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "debug/debugDraw.h"
#include "audio/audioManager.h"
#include "BoardShadows.hpp"
#include "BoardCamera.hpp"
#include "BoardSurroundings.hpp"
#include "CartridgeStorage.hpp"
#include "GameplaySlice.hpp"
#include "GeneratedProject.hpp"
#include "GpkRuntimeAdapter.hpp"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "vi/swapChain.h"
#include <grandleon/view/motion.hpp>

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace console = grandleon::client::console_localization;
namespace package_format = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;
namespace tac = grandleon::tactics;
namespace view = grandleon::view;
namespace gp = grandleon::nintendo64::pyrite;

namespace {

constexpr std::int16_t kCell = 52;
// Coplanar neighbours share their edge. The former one-unit inset on both
// sides exposed the clear colour as a heavy black grid, making a connected
// landscape read as separate board-game cards. Actual height changes still
// receive the shaded elevation faces built below.
constexpr std::int16_t kHalfCell = kCell / 2;
constexpr std::size_t kCellVertexCount = 4;
constexpr std::size_t kMaxCells = 20U * 14U;
constexpr std::size_t kBatchCells = 15U;
constexpr std::size_t kMaxElevationVertices = kMaxCells * 8U;
constexpr std::size_t kGroundVertexCount = 4U;
constexpr std::size_t kMaxApronCells = 800U;
constexpr std::size_t kTreeVertexCount = 12U;
constexpr std::size_t kMaxTrees = 176U;
constexpr std::size_t kTerrainFormVertexCount = 8U;
constexpr std::size_t kMaxTerrainForms = 144U;
constexpr std::size_t kUnitVertexCount = 4U;
constexpr std::size_t kMaxUnits = 48U;
constexpr std::size_t kCursorVerticesPerPlayer = 16U;
constexpr std::size_t kCursorVertexCount =
    kMaxCells * 4U + kCursorVerticesPerPlayer * 2U;
constexpr std::size_t kMaxLandmarkVertices = 512U;

enum class VisualPose : std::uint8_t {
    idle,
    moving,
    close_idle,
    attack,
    hurt,
};

struct UnitArt final {
    sprite_t* idle{};
    sprite_t* moving{};
    sprite_t* close_idle{};
    sprite_t* attack{};
    sprite_t* hurt{};
};

struct BoardResources final {
    gp::GpkRuntimeAdapter runtime{};
    const pr::EncounterLoadResult* content{};
    gp::GameplaySlice game{};
    gp::BoardShadows shadows{};
    pr::Presentation presentation{};
    pr::EncounterLoadResult campaign_content{};
    std::optional<gp::CartridgeStorage> cartridge;
    std::optional<storage::ByteWindowSlotStorage> saves;
    std::optional<client::PackageBoards> campaign_boards;
    std::optional<client::CampaignSession> campaign;
    std::vector<sim::Event> battle_events;
    T3DVertPacked* cells{};
    T3DVertPacked* ground{};
    T3DVertPacked* apron{};
    T3DVertPacked* elevation{};
    T3DVertPacked* terrain_forms{};
    T3DVertPacked* trees{};
    T3DVertPacked* landmark{};
    T3DVertPacked* visibility{};
    T3DVertPacked* units{};
    T3DVertPacked* cursor{};
    sprite_t* terrain{};
    sprite_t* surface_detail{};
    sprite_t* combat_backdrop{};
    sprite_t* combat_shadow{};
    std::array<UnitArt, grandleon_project_drawings.size()> art{};
    wav64_t ambience{};
    wav64_t ui_cue{};
    wav64_t impact_cue{};
    wav64_t victory_cue{};
    P64::Audio::Handle ambience_handle{};
    std::size_t elevation_vertex_count{};
    std::size_t terrain_form_count{};
    std::array<std::uint8_t, kMaxTerrainForms> terrain_form_shapes{};
    std::size_t apron_cell_count{};
    std::size_t tree_count{};
    std::size_t landmark_vertex_count{};
    std::size_t visibility_vertex_count{};
    std::array<bool, kMaxCells> visibility_revealed{};
    std::size_t unit_count{};
    std::size_t cursor_vertex_count{kCursorVerticesPerPlayer};
    sim::UnitId rowan_id{};
    sim::UnitId tomas_id{};
    std::int16_t rowan_x{-1};
    std::int16_t rowan_y{-1};
    std::int16_t rowan_health{};
    bool loaded{};
};

struct Motion final {
    sim::UnitId unit{};
    sim::Position from{};
    sim::Position to{};
    std::array<sim::Position, kMaxCells> route{};
    std::size_t route_count{};
    std::uint32_t total_frames{};
    std::uint32_t started{};
    bool facing_left{};
    bool active{};
};

struct PlannedMotion final {
    sim::UnitId unit{};
    sim::Position from{};
    sim::Position to{};
    std::array<sim::Position, kMaxCells> route{};
    std::size_t route_count{};
    bool valid{};
};

enum class ReplayPhase : std::uint8_t {
    explore_northwest,
    explore_southeast,
    return_to_rowan,
    select_rowan,
    aim_rowan,
    move_rowan,
    settle_rowan,
    select_rowan_again,
    wait_rowan,
    aim_tomas,
    select_tomas,
    wait_tomas,
    opponent,
    autoplay_battle,
    complete,
};

enum class ShellPhase : std::uint8_t {
    title,
    help,
    players,
    save,
    story,
    name,
    deployment,
    aftermath,
    complete,
    battle,
};

struct ShellState final {
    ShellPhase phase{ShellPhase::title};
    std::uint8_t local_players{1U};
    std::uint8_t player_choice{};
    std::uint8_t title_row{};
    std::uint8_t save_choice{};
    std::uint32_t next_automatic_frame{75U};
    bool save_present{};
    bool resumed{};
    bool user_took_over{};
    std::size_t stage_index{};
    std::size_t completed_battles{};
    std::size_t total_commands{};
    std::size_t total_enemy_commands{};
    std::size_t total_combats{};
    bool first_battle_committed{};
    bool campaign_advanced_checkpoint{};
    bool allow_one{true};
    bool allow_two{};
    bool second_connected{};
    bool deployment_approved{};
    std::size_t deployment_row{};
    std::size_t first_battle_commands{};
    std::size_t first_battle_enemy_commands{};
    std::vector<std::uint64_t> story_ids;
    std::size_t story_id_index{};
    std::size_t story_line_index{};
    pr::Dialogue story;
    std::vector<campaign::PersistentEntityId> pending_player_names;
    client::RosterEntry name_member;
    std::optional<client::ConsoleNameEditor> name_editor;
    client::CompanyManagement deployment;
    client::BattleAftermath aftermath;
    std::vector<sim::ObjectiveResult> aftermath_objectives;
};

enum class BattleUiPhase : std::uint8_t {
    board,
    system,
    help,
    action,
    attack_target,
    ability_target,
    talk_target,
    result,
};

struct BattleUiState final {
    BattleUiPhase phase{BattleUiPhase::board};
    sim::UnitId actor{};
    sim::ContentId ability{};
    std::uint8_t player{};
    std::uint8_t row{};
    std::uint32_t result_until{};
    sim::CommandType result_command{sim::CommandType::wait};
    sim::CommandError result_error{sim::CommandError::none};
    std::int16_t result_amount{};
    bool result_missed{};
    bool result_restored{};
};

struct BattleMomentState final {
    std::vector<std::uint64_t> dialogue_ids;
    std::size_t dialogue_index{};
    std::size_t line_index{};
    std::uint32_t next_automatic_frame{};
    pr::Dialogue dialogue;
    bool active{};
};

struct ReplayState final {
    bool castle_review_aimed{};
    bool castle_review_checkpoint{};
    ReplayPhase phase{ReplayPhase::explore_northwest};
    sim::Position rowan_start{};
    sim::Position rowan_destination{};
    std::uint32_t next_frame{90U};
    std::size_t opponent_commands{};
    bool user_took_over{};
    bool selected_checkpoint{};
    bool board_checkpoint{};
    bool moved_checkpoint{};
    bool enemy_checkpoint{};
    bool round_checkpoint{};
    bool motion_checkpoint{};
    bool camera_x_checkpoint{};
    bool camera_y_checkpoint{};
    bool action_menu_checkpoint{};
    bool attack_forecast_checkpoint{};
    bool ability_forecast_checkpoint{};
    bool action_result_checkpoint{};
};

struct CameraPose final {
    float eye_x{};
    float eye_y{};
    float eye_z{};
    float target_x{};
    float target_y{};
    float target_z{};
    float ortho{};
};

enum class CombatPhase : std::uint8_t {
    inactive,
    entering,
    first_strike,
    counter,
    returning,
};

struct CombatPresentation final {
    bool enabled{true};
    bool reduced_motion{};
    bool active{};
    bool skipped{};
    bool first_missed{};
    bool counter_exists{};
    bool counter_missed{};
    bool close_checkpoint{};
    bool fly_checkpoint{};
    bool strike_checkpoint{};
    bool counter_checkpoint{};
    bool return_checkpoint{};
    bool restore_checkpoint{};
    CombatPhase phase{CombatPhase::inactive};
    std::uint32_t phase_ticks{};
    sim::UnitId attacker{};
    sim::UnitId defender{};
    sim::Position attacker_position{};
    sim::Position defender_position{};
    std::int16_t attacker_health_before{};
    std::int16_t attacker_health_after{};
    std::int16_t defender_health_before{};
    std::int16_t defender_health_after{};
    std::int16_t first_damage{};
    std::int16_t counter_damage{};
    std::uint64_t authoritative_hash{};
    CameraPose tactical_camera{};
    CameraPose close_camera{};
    std::size_t exchanges{};
};

constexpr CameraPose kTacticalCamera{
    -5.5F, 6.8F, 5.5F,
    0.0F, 0.12F, 0.0F,
    2.08F,
};

BoardResources board;
Motion motion;
PlannedMotion planned_motion;
ReplayState replay;
CombatPresentation combat;
ShellState shell;
BattleUiState battle_ui;
BattleMomentState battle_moment;
CameraPose current_camera{kTacticalCamera};
std::uint32_t frame_count = 0;
// Pyrite presents every second NTSC retrace. Timings in the shared campaign
// remain authored in 60 Hz ticks, so each 30 Hz update advances two ticks.
constexpr std::uint32_t kLogicTicksPerUpdate = 2U;
std::uint64_t sample_start = 0;
std::uint64_t previous_frame = 0;
std::uint64_t min_frame_ticks = ~std::uint64_t{0};
std::uint64_t max_frame_ticks = 0;
bool camera_configured = false;
bool performance_sample_started = false;
bool verdict_reported = false;
std::uint32_t performance_frames = 0;
std::uint8_t restore_checkpoint_delay = 0U;
bool debug_hud = false;
bool audio_enabled = true;
std::string selected_language;
float camera_min_x = 0.0F;
float camera_max_x = 0.0F;
float camera_min_z = 0.0F;
float camera_max_z = 0.0F;

constexpr std::string_view kSettingsSlot = "pyrite_settings";
constexpr std::array<std::uint8_t, 3> kSettingsMagic{'P', 'Y', 'S'};

std::string_view game_language() {
    return selected_language;
}

const char* ui(console::Message message) {
    return console::text(game_language(), message);
}

std::vector<std::string_view> game_languages() {
    return pr::available_languages(board.runtime.package());
}

void save_user_settings();

bool cycle_game_language() {
    const std::vector<std::string_view> languages = game_languages();
    if (languages.size() < 2U) return false;
    std::size_t selected = 0U;
    for (std::size_t index = 0; index < languages.size(); ++index) {
        if (languages[index] == game_language()) selected = index;
    }
    selected_language = languages[(selected + 1U) % languages.size()];
    save_user_settings();
    debugf(
        "CHECKPOINT PYRITE_LANGUAGE code=%s live=1\n",
        selected_language.c_str()
    );
    return true;
}

const char* campaign_slot() {
    return shell.local_players == 2U
        ? grandleon_project_save_2p : grandleon_project_save_1p;
}

void save_user_settings() {
    if (!board.saves) return;
    std::vector<std::uint8_t> bytes(kSettingsMagic.begin(), kSettingsMagic.end());
    bytes.push_back(2U);
    std::uint8_t flags = 0U;
    if (combat.enabled) flags |= 1U << 0U;
    if (combat.reduced_motion) flags |= 1U << 1U;
    if (debug_hud) flags |= 1U << 2U;
    if (audio_enabled) flags |= 1U << 3U;
    bytes.push_back(flags);
    const std::size_t language_size = std::min<std::size_t>(
        selected_language.size(), 15U
    );
    bytes.push_back(static_cast<std::uint8_t>(language_size));
    bytes.insert(
        bytes.end(), selected_language.begin(),
        selected_language.begin() + static_cast<std::ptrdiff_t>(language_size)
    );
    assertf(
        board.saves->write(kSettingsSlot, bytes) == storage::StorageError::none,
        "Pyrite settings could not be saved"
    );
}

void load_user_settings() {
    selected_language = std::string(pr::source_language(board.runtime.package()));
    if (!board.saves || !board.saves->contains(kSettingsSlot)) return;
    const storage::StorageRead saved = board.saves->read(kSettingsSlot);
    if (!saved || saved.bytes.size() < kSettingsMagic.size() + 2U ||
        !std::equal(
            kSettingsMagic.begin(), kSettingsMagic.end(), saved.bytes.begin()
        )) {
        return;
    }
    const std::uint8_t version = saved.bytes[3];
    const std::uint8_t flags = saved.bytes[4];
    combat.enabled = (flags & (1U << 0U)) != 0U;
    combat.reduced_motion = (flags & (1U << 1U)) != 0U;
    debug_hud = (flags & (1U << 2U)) != 0U;
    audio_enabled = (flags & (1U << 3U)) != 0U;
    if (version < 2U || saved.bytes.size() < 6U) return;
    const std::size_t language_size = saved.bytes[5];
    if (language_size == 0U || language_size > 15U ||
        saved.bytes.size() != 6U + language_size) return;
    const std::string stored(
        saved.bytes.begin() + 6, saved.bytes.end()
    );
    for (const std::string_view available : game_languages()) {
        if (available == stored) {
            selected_language = stored;
            return;
        }
    }
}

void apply_audio_setting() {
    P64::AudioManager::setMasterVolume(audio_enabled ? 0.72F : 0.0F);
}

void play_ui_cue() {
    if (audio_enabled) (void)P64::AudioManager::play2D(&board.ui_cue);
}

void play_impact_cue() {
    if (audio_enabled) (void)P64::AudioManager::play2D(&board.impact_cue);
}

void play_victory_cue() {
    if (audio_enabled) (void)P64::AudioManager::play2D(&board.victory_cue);
}

constexpr std::uint32_t pack_rgba(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha = 255U
) {
    return (static_cast<std::uint32_t>(red) << 24U) |
        (static_cast<std::uint32_t>(green) << 16U) |
        (static_cast<std::uint32_t>(blue) << 8U) |
        static_cast<std::uint32_t>(alpha);
}

void set_vertex(
    T3DVertPacked* buffer,
    std::size_t index,
    std::int16_t x,
    std::int16_t y,
    std::int16_t z,
    std::uint32_t colour,
    std::int16_t u,
    std::int16_t v,
    std::uint16_t normal
) {
    auto* packed = &buffer[index >> 1U];
    auto* position = t3d_vertbuffer_get_pos(buffer, static_cast<int>(index));
    position[0] = x;
    position[1] = y;
    position[2] = z;
    auto* uv = t3d_vertbuffer_get_uv(buffer, static_cast<int>(index));
    uv[0] = u;
    uv[1] = v;
    if (buffer == board.terrain_forms || buffer == board.trees ||
        buffer == board.landmark || buffer == board.elevation) {
        // World-anchored grain remains fixed when the camera scrolls.
        uv[0] = static_cast<std::int16_t>(((x + z + 32768) / 3 % 32) * 32);
        uv[1] = static_cast<std::int16_t>(((y + z + 32768) / 3 % 32) * 32);
    }
    if ((index & 1U) == 0U) {
        packed->rgbaA = colour;
        packed->normA = normal;
    } else {
        packed->rgbaB = colour;
        packed->normB = normal;
    }
}

std::int16_t world_x(std::int16_t x, std::uint16_t width) {
    return static_cast<std::int16_t>(
        (static_cast<int>(x) * 2 - static_cast<int>(width) + 1) * kCell / 2
    );
}

std::int16_t world_z(std::int16_t y, std::uint16_t height) {
    return static_cast<std::int16_t>(
        (static_cast<int>(y) * 2 - static_cast<int>(height) + 1) * kCell / 2
    );
}

float smoothstep(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

float mix(float from, float to, float amount) {
    return from + (to - from) * amount;
}

CameraPose interpolate_camera(
    const CameraPose& from,
    const CameraPose& to,
    float amount
) {
    const float eased = smoothstep(amount);
    return {
        mix(from.eye_x, to.eye_x, eased),
        mix(from.eye_y, to.eye_y, eased),
        mix(from.eye_z, to.eye_z, eased),
        mix(from.target_x, to.target_x, eased),
        mix(from.target_y, to.target_y, eased),
        mix(from.target_z, to.target_z, eased),
        mix(from.ortho, to.ortho, eased),
    };
}

void apply_camera(const CameraPose& pose) {
    auto* camera = P64::SceneManager::getCurrent().getCamera();
    assert(camera);
    camera->setOrthographic(pose.ortho);
    camera->setLookAt(
        {pose.eye_x, pose.eye_y, pose.eye_z},
        {pose.target_x, pose.target_y, pose.target_z},
        {0.0F, 1.0F, 0.0F}
    );
    current_camera = pose;
}

CameraPose tactical_camera_for(sim::Position requested) {
    constexpr float half_visible_x = 3.5F;
    constexpr float half_visible_y = 2.5F;
    const int width = board.content->definition.width;
    const int height = board.content->definition.height;
    const gp::BoardCameraWindow window = gp::board_camera_window(
        requested.x, requested.y, width, height,
        half_visible_x, half_visible_y
    );
    assert(gp::board_camera_target_is_playable(window, width, height));
    constexpr float render_scale = 64.0F;
    const float centre_x = static_cast<float>(width - 1) * 0.5F;
    const float centre_y = static_cast<float>(height - 1) * 0.5F;
    const float offset_x = (window.focus_x - centre_x) * kCell / render_scale;
    const float offset_z = (window.focus_y - centre_y) * kCell / render_scale;
    CameraPose result = kTacticalCamera;
    result.eye_x += offset_x;
    result.target_x += offset_x;
    result.eye_z += offset_z;
    result.target_z += offset_z;
    return result;
}

void update_tactical_camera() {
    if (combat.active) return;
    const CameraPose wanted = tactical_camera_for(
        board.game.cursor(board.game.active_player())
    );
    constexpr float follow = 0.16F;
    const CameraPose next{
        mix(current_camera.eye_x, wanted.eye_x, follow),
        mix(current_camera.eye_y, wanted.eye_y, follow),
        mix(current_camera.eye_z, wanted.eye_z, follow),
        mix(current_camera.target_x, wanted.target_x, follow),
        mix(current_camera.target_y, wanted.target_y, follow),
        mix(current_camera.target_z, wanted.target_z, follow),
        mix(current_camera.ortho, wanted.ortho, follow),
    };
    apply_camera(next);
    camera_min_x = std::min(camera_min_x, next.target_x);
    camera_max_x = std::max(camera_max_x, next.target_x);
    camera_min_z = std::min(camera_min_z, next.target_z);
    camera_max_z = std::max(camera_max_z, next.target_z);
    if (!replay.camera_x_checkpoint && camera_max_x - camera_min_x > 3.5F) {
        replay.camera_x_checkpoint = true;
        debugf("CHECKPOINT PYRITE_CAMERA_HORIZONTAL\n");
    }
    if (!replay.camera_y_checkpoint && camera_max_z - camera_min_z > 1.8F) {
        replay.camera_y_checkpoint = true;
        debugf("CHECKPOINT PYRITE_CAMERA_VERTICAL\n");
    }
}

std::uint8_t terrain_kind(std::uint64_t id) {
    const std::uint8_t kind = board.presentation.kind_of_terrain(id);
    assertf(
        kind < grandleon_project_terrain_slots.size() &&
            grandleon_project_terrain_slots[kind] != 255U,
        "Project terrain %llu has no generated Pyrite material",
        id
    );
    return kind;
}

std::int16_t terrain_lift(std::uint64_t id) {
    const std::uint8_t kind = terrain_kind(id);
    if (kind == 3U || kind == 7U) return 13;
    if (kind == 1U || kind == 8U || kind == 12U) return 4;
    if (kind == 10U) return 2;
    return 0;
}

void terrain_uv(
    std::uint64_t id,
    std::size_t index,
    std::int16_t& s0,
    std::int16_t& t0,
    std::int16_t& s1,
    std::int16_t& t1
) {
    const std::uint8_t kind = terrain_kind(id);
    const int slot = grandleon_project_terrain_slots[kind];
    const int base_s = (slot % grandleon_project_atlas_columns) *
        grandleon_project_atlas_cell;
    const int base_t = (slot / grandleon_project_atlas_columns) *
        grandleon_project_atlas_cell;
    const int variation = static_cast<int>((index * 5U + index / 8U) & 3U);
    const int low_s = base_s + 1;
    const int high_s = base_s + grandleon_project_atlas_cell - 2;
    const int low_t = base_t + 1;
    const int high_t = base_t + grandleon_project_atlas_cell - 2;
    // Flip the complete padded material for deterministic variation. The old
    // 28-texel window crossed each 20-texel atlas slot and mixed unrelated
    // terrain families under bilinear filtering.
    s0 = static_cast<std::int16_t>(
        ((variation & 1) ? high_s : low_s) << 5
    );
    s1 = static_cast<std::int16_t>(
        ((variation & 1) ? low_s : high_s) << 5
    );
    t0 = static_cast<std::int16_t>(
        ((variation & 2) ? high_t : low_t) << 5
    );
    t1 = static_cast<std::int16_t>(
        ((variation & 2) ? low_t : high_t) << 5
    );
}

std::uint32_t terrain_tint(std::uint64_t id, std::size_t index) {
    const std::uint8_t variation = static_cast<std::uint8_t>((index * 19U) & 0x0FU);
    constexpr std::uint8_t colours[13][3] = {
        {104, 166, 196}, {207, 178, 122}, {111, 164, 91},
        {169, 172, 158}, {224, 194, 126}, {225, 238, 239},
        {118, 132, 83}, {170, 181, 116}, {176, 162, 145},
        {147, 190, 105}, {199, 175, 98}, {115, 170, 85},
        {194, 197, 184},
    };
    const std::uint8_t kind = terrain_kind(id);
    return pack_rgba(
        static_cast<std::uint8_t>(colours[kind][0] - variation / 2U),
        static_cast<std::uint8_t>(colours[kind][1] - variation / 2U),
        static_cast<std::uint8_t>(colours[kind][2] - variation / 3U)
    );
}

void build_cell_vertices() {
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&up);
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        const std::int16_t x = static_cast<std::int16_t>(index % width);
        const std::int16_t z = static_cast<std::int16_t>(index / width);
        const std::int16_t cx = world_x(x, width);
        const std::int16_t cz = world_z(z, height);
        const std::int16_t lift = terrain_lift(board.content->terrain[index]);
        std::int16_t s0 = 0;
        std::int16_t t0 = 0;
        std::int16_t s1 = 0;
        std::int16_t t1 = 0;
        terrain_uv(board.content->terrain[index], index, s0, t0, s1, t1);
        const std::uint32_t tint = terrain_tint(board.content->terrain[index], index);
        const std::size_t first = index * kCellVertexCount;
        set_vertex(board.cells, first + 0U, cx - kHalfCell, lift, cz - kHalfCell, tint, s0, t0, normal);
        set_vertex(board.cells, first + 1U, cx + kHalfCell, lift, cz - kHalfCell, tint, s1, t0, normal);
        set_vertex(board.cells, first + 2U, cx + kHalfCell, lift, cz + kHalfCell, tint, s1, t1, normal);
        set_vertex(board.cells, first + 3U, cx - kHalfCell, lift, cz + kHalfCell, tint, s0, t1, normal);
    }
}

std::uint32_t environment_hash(std::int16_t x, std::int16_t z) {
    const std::uint32_t seed = static_cast<std::uint32_t>(
        board.content->map_id ^ (board.content->map_id >> 32U)
    );
    std::uint32_t value = seed ^ static_cast<std::uint16_t>(x);
    value = value * 0x9E3779B1U + static_cast<std::uint16_t>(z);
    value ^= value >> 16U;
    value *= 0x85EBCA6BU;
    value ^= value >> 13U;
    return value;
}

std::uint32_t environment_colour(
    std::uint64_t edge_terrain,
    std::int16_t x,
    std::int16_t z,
    int distance
) {
    int red = 55;
    int green = 83;
    int blue = 43;
    const std::uint8_t kind = terrain_kind(edge_terrain);
    if (kind == 0U) { red = 35; green = 67; blue = 85; }
    else if (kind == 1U) { red = 83; green = 77; blue = 49; }
    else if (kind == 2U || kind == 11U) { red = 39; green = 75; blue = 38; }
    else if (kind == 3U || kind == 7U) { red = 61; green = 72; blue = 44; }
    else if (kind == 10U) { red = 82; green = 101; blue = 48; }
    else if (kind == 8U || kind == 12U) { red = 74; green = 78; blue = 65; }
    constexpr int spread = 5;
    const int variation = static_cast<int>(environment_hash(x, z) % spread) -
        spread / 2;
    const int fade = std::max(0, distance - 1) * 5;
    return pack_rgba(
        static_cast<std::uint8_t>(std::clamp(red + variation - fade, 22, 110)),
        static_cast<std::uint8_t>(std::clamp(green + variation - fade, 28, 125)),
        static_cast<std::uint8_t>(std::clamp(blue + variation / 2 - fade, 20, 90))
    );
}

std::uint64_t surrounding_terrain_at(int x, int z) {
    const int width = board.content->definition.width;
    const int height = board.content->definition.height;
    const auto& surroundings = board.content->surroundings;
    if (surroundings.mode == pr::MapSurroundingMode::manual ||
        surroundings.mode == pr::MapSurroundingMode::fill ||
        surroundings.mode == pr::MapSurroundingMode::blend) {
        const std::size_t index = gp::surrounding_index(
            x, z, width, height, surroundings.depth
        );
        assert(index < surroundings.terrain.size());
        return surroundings.terrain[index];
    }
    const int nearest_x = std::clamp(x, 0, width - 1);
    const int nearest_z = std::clamp(z, 0, height - 1);
    return board.content->terrain[static_cast<std::size_t>(
        nearest_z * width + nearest_x
    )];
}

void build_environment_vertices() {
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&up);
    constexpr std::int16_t far_cells = 18;
    const std::int16_t ground_x0 = world_x(-far_cells, width) - kHalfCell;
    const std::int16_t ground_x1 = world_x(width + far_cells - 1, width) + kHalfCell;
    const std::int16_t ground_z0 = world_z(-far_cells, height) - kHalfCell;
    const std::int16_t ground_z1 = world_z(height + far_cells - 1, height) + kHalfCell;
    const auto& surroundings = board.content->surroundings;
    const std::uint32_t distant = surroundings.mode == pr::MapSurroundingMode::solid
        ? pack_rgba(
            static_cast<std::uint8_t>((surroundings.colour >> 16U) & 0xFFU),
            static_cast<std::uint8_t>((surroundings.colour >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(surroundings.colour & 0xFFU)
        )
        : pack_rgba(26U, 38U, 28U);
    set_vertex(board.ground, 0U, ground_x0, -9, ground_z0, distant, 0, 0, normal);
    set_vertex(board.ground, 1U, ground_x1, -9, ground_z0, distant, 0, 0, normal);
    set_vertex(board.ground, 2U, ground_x1, -9, ground_z1, distant, 0, 0, normal);
    set_vertex(board.ground, 3U, ground_x0, -9, ground_z1, distant, 0, 0, normal);

    board.apron_cell_count = 0U;
    const int apron = std::min<int>(8, surroundings.depth);
    if (surroundings.mode == pr::MapSurroundingMode::none ||
        surroundings.mode == pr::MapSurroundingMode::solid) {
        return;
    }
    if (surroundings.mode == pr::MapSurroundingMode::manual ||
        surroundings.mode == pr::MapSurroundingMode::fill ||
        surroundings.mode == pr::MapSurroundingMode::blend) {
        assertf(
            surroundings.terrain.size() ==
                gp::expected_surrounding_cells(width, height, apron),
            "Packaged surroundings perimeter is incomplete"
        );
    } else {
        assertf(
            surroundings.mode == pr::MapSurroundingMode::automatic,
            "Unsupported packaged surroundings mode"
        );
        assertf(surroundings.terrain.empty(),
            "Automatic surroundings must not materialize terrain");
    }
    for (int z = -apron; z < static_cast<int>(height) + apron; ++z) {
        for (int x = -apron; x < static_cast<int>(width) + apron; ++x) {
            if (x >= 0 && x < width && z >= 0 && z < height) continue;
            assert(board.apron_cell_count < kMaxApronCells);
            const int dx = x < 0 ? -x : (x >= width ? x - width + 1 : 0);
            const int dz = z < 0 ? -z : (z >= height ? z - height + 1 : 0);
            const int distance = std::max(dx, dz);
            const std::uint64_t edge_terrain = surrounding_terrain_at(x, z);
            const std::int16_t cx = world_x(static_cast<std::int16_t>(x), width);
            const std::int16_t cz = world_z(static_cast<std::int16_t>(z), height);
            constexpr std::int16_t apron_half = kCell / 2;
            const std::int16_t floor = -4;
            const std::int16_t colour_x = distance > 1
                ? static_cast<std::int16_t>(x < 0 ? (x - 1) / 2 : x / 2)
                : static_cast<std::int16_t>(x);
            const std::int16_t colour_z = distance > 1
                ? static_cast<std::int16_t>(z < 0 ? (z - 1) / 2 : z / 2)
                : static_cast<std::int16_t>(z);
            const std::uint32_t colour = environment_colour(
                edge_terrain, colour_x, colour_z, distance
            );
            const std::size_t first = board.apron_cell_count * 4U;
            set_vertex(board.apron, first + 0U, cx - apron_half, floor,
                cz - apron_half, colour, 0, 0, normal);
            set_vertex(board.apron, first + 1U, cx + apron_half, floor,
                cz - apron_half, colour, 0, 0, normal);
            set_vertex(board.apron, first + 2U, cx + apron_half, floor,
                cz + apron_half, colour, 0, 0, normal);
            set_vertex(board.apron, first + 3U, cx - apron_half, floor,
                cz + apron_half, colour, 0, 0, normal);
            ++board.apron_cell_count;
        }
    }
}

std::uint32_t darken(std::uint32_t colour, std::uint8_t numerator) {
    const auto channel = [numerator](std::uint32_t value) {
        return static_cast<std::uint8_t>((value * numerator) / 255U);
    };
    return pack_rgba(
        channel((colour >> 24U) & 0xFFU),
        channel((colour >> 16U) & 0xFFU),
        channel((colour >> 8U) & 0xFFU)
    );
}

void append_elevation_face(
    std::int16_t x0,
    std::int16_t z0,
    std::int16_t x1,
    std::int16_t z1,
    std::int16_t bottom,
    std::int16_t top,
    std::uint32_t colour,
    const fm_vec3_t& normal
) {
    assert(board.elevation_vertex_count + 4U <= kMaxElevationVertices);
    const std::uint16_t packed_normal = t3d_vert_pack_normal(&normal);
    const std::size_t first = board.elevation_vertex_count;
    set_vertex(board.elevation, first + 0U, x0, bottom, z0, darken(colour, 128U), 0, 0, packed_normal);
    set_vertex(board.elevation, first + 1U, x1, bottom, z1, darken(colour, 140U), 0, 0, packed_normal);
    set_vertex(board.elevation, first + 2U, x1, top, z1, darken(colour, 205U), 0, 0, packed_normal);
    set_vertex(board.elevation, first + 3U, x0, top, z0, darken(colour, 185U), 0, 0, packed_normal);
    board.elevation_vertex_count += 4U;
}

std::int16_t lift_at(int x, int y) {
    const int width = board.content->definition.width;
    const int height = board.content->definition.height;
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    return terrain_lift(board.content->terrain[static_cast<std::size_t>(y * width + x)]);
}

void build_elevation_vertices() {
    board.elevation_vertex_count = 0;
    const int width = board.content->definition.width;
    const int height = board.content->definition.height;
    const fm_vec3_t left = {{-1.0F, 0.0F, 0.0F}};
    const fm_vec3_t front = {{0.0F, 0.0F, 1.0F}};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            const std::int16_t top = lift_at(x, y);
            if (top <= 0) continue;
            const std::int16_t cx = world_x(static_cast<std::int16_t>(x), width);
            const std::int16_t cz = world_z(static_cast<std::int16_t>(y), height);
            const std::uint32_t colour = terrain_tint(board.content->terrain[index], index);
            const std::int16_t left_bottom = lift_at(x - 1, y);
            if (top > left_bottom) {
                append_elevation_face(
                    cx - kHalfCell, cz + kHalfCell,
                    cx - kHalfCell, cz - kHalfCell,
                    left_bottom, top, colour, left
                );
            }
            const std::int16_t front_bottom = lift_at(x, y + 1);
            if (top > front_bottom) {
                append_elevation_face(
                    cx - kHalfCell, cz + kHalfCell,
                    cx + kHalfCell, cz + kHalfCell,
                    front_bottom, top, colour, front
                );
            }
        }
    }
}

enum class TerrainFormShape : std::uint8_t {
    tapered,
    crossed,
};

void append_terrain_form(
    std::int16_t cx,
    std::int16_t floor,
    std::int16_t cz,
    std::int16_t half,
    std::int16_t height,
    std::uint32_t colour,
    TerrainFormShape shape
) {
    if (board.terrain_form_count >= kMaxTerrainForms) return;
    const std::size_t first = board.terrain_form_count * kTerrainFormVertexCount;
    const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&up);
    const std::uint32_t lower = darken(colour, 165U);
    const std::int16_t top_half = std::max<std::int16_t>(1, half / 2);
    if (shape == TerrainFormShape::crossed) {
        set_vertex(board.terrain_forms, first + 0U, cx - half, floor, cz, lower, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 1U, cx + half, floor, cz, lower, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 2U, cx, floor, cz - half, lower, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 3U, cx, floor, cz + half, lower, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 4U, cx + top_half, floor + height, cz, colour, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 5U, cx - top_half, floor + height, cz, colour, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 6U, cx, floor + height, cz + top_half, colour, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 7U, cx, floor + height, cz - top_half, colour, 0, 0, normal);
    } else {
        set_vertex(board.terrain_forms, first + 0U, cx - half, floor, cz - half, lower, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 1U, cx + half, floor, cz - half, lower, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 2U, cx + half, floor, cz + half, lower, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 3U, cx - half, floor, cz + half, lower, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 4U, cx - top_half, floor + height, cz - top_half, colour, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 5U, cx + top_half, floor + height, cz - top_half, colour, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 6U, cx + top_half, floor + height, cz + top_half, colour, 0, 0, normal);
        set_vertex(board.terrain_forms, first + 7U, cx - top_half, floor + height, cz + top_half, colour, 0, 0, normal);
    }
    board.terrain_form_shapes[board.terrain_form_count] =
        static_cast<std::uint8_t>(shape);
    ++board.terrain_form_count;
}

void build_terrain_form_vertices() {
    board.terrain_form_count = 0U;
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        const auto id = board.content->terrain[index];
        const std::uint8_t kind = terrain_kind(id);
        const auto x = static_cast<std::int16_t>(index % width);
        const auto z = static_cast<std::int16_t>(index / width);
        const std::uint32_t roll = environment_hash(x, z);
        const std::uint32_t divisor =
            (kind == 3U || kind == 7U || kind == 10U || kind == 11U) ? 1U : 3U;
        if (roll % divisor != 0U) continue;
        const std::int16_t cx = static_cast<std::int16_t>(
            world_x(x, width) + ((roll & 1U) ? 13 : -13)
        );
        const std::int16_t cz = static_cast<std::int16_t>(
            world_z(z, height) + ((roll & 2U) ? 11 : -11)
        );
        const std::int16_t floor = terrain_lift(id);
        switch (kind) {
        case 0U: // water reeds
            append_terrain_form(cx, floor, cz, 3, 13, pack_rgba(73U, 130U, 92U), TerrainFormShape::crossed);
            break;
        case 1U: // road edge stones
            append_terrain_form(cx, floor, cz, 6, 6, pack_rgba(143U, 127U, 102U), TerrainFormShape::tapered);
            break;
        case 2U: // forest undergrowth
            append_terrain_form(cx, floor, cz, 8, 11, pack_rgba(62U, 121U, 58U), TerrainFormShape::tapered);
            break;
        case 3U: // mountain outcrops
            append_terrain_form(cx, floor, cz, 10, 22, pack_rgba(154U, 159U, 155U), TerrainFormShape::tapered);
            break;
        case 4U: // sand ridges
            append_terrain_form(cx, floor, cz, 9, 8, pack_rgba(213U, 178U, 105U), TerrainFormShape::tapered);
            break;
        case 5U: // snow and ice
            append_terrain_form(cx, floor, cz, 8, 14, pack_rgba(199U, 226U, 231U), TerrainFormShape::tapered);
            break;
        case 6U: // swamp rushes
            append_terrain_form(cx, floor, cz, 4, 17, pack_rgba(91U, 117U, 62U), TerrainFormShape::crossed);
            break;
        case 7U: // hill rocks
            append_terrain_form(cx, floor, cz, 9, 16, pack_rgba(126U, 139U, 92U), TerrainFormShape::tapered);
            break;
        case 8U: // ruin rubble around the authored landmark
            append_terrain_form(cx, floor, cz, 7, 9, pack_rgba(135U, 142U, 139U), TerrainFormShape::tapered);
            break;
        case 9U: // grass shrubs
            append_terrain_form(cx, floor, cz, 7, 10, pack_rgba(75U, 143U, 63U), TerrainFormShape::tapered);
            break;
        case 10U: // crop rows
            append_terrain_form(cx, floor, cz, 4, 17, pack_rgba(202U, 167U, 69U), TerrainFormShape::crossed);
            append_terrain_form(cx - 9, floor, cz + 5, 3, 14, pack_rgba(181U, 143U, 55U), TerrainFormShape::crossed);
            break;
        case 11U: // bamboo stalk clusters
            append_terrain_form(cx, floor, cz, 4, 35, pack_rgba(83U, 151U, 68U), TerrainFormShape::tapered);
            append_terrain_form(cx - 8, floor, cz + 6, 3, 28, pack_rgba(105U, 169U, 76U), TerrainFormShape::tapered);
            break;
        case 12U: // paved chips and kerbs
            append_terrain_form(cx, floor, cz, 6, 5, pack_rgba(155U, 158U, 153U), TerrainFormShape::tapered);
            break;
        default:
            break;
        }
    }
}

void append_box(
    std::int16_t cx,
    std::int16_t bottom,
    std::int16_t cz,
    std::int16_t half_x,
    std::int16_t height,
    std::int16_t half_z,
    std::uint32_t colour
) {
    assert(board.landmark_vertex_count + 8U <= kMaxLandmarkVertices);
    const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&up);
    const std::size_t first = board.landmark_vertex_count;
    const std::uint32_t lower = darken(colour, 150U);
    set_vertex(board.landmark, first + 0U, cx - half_x, bottom, cz - half_z, lower, 0, 0, normal);
    set_vertex(board.landmark, first + 1U, cx + half_x, bottom, cz - half_z, lower, 0, 0, normal);
    set_vertex(board.landmark, first + 2U, cx + half_x, bottom, cz + half_z, lower, 0, 0, normal);
    set_vertex(board.landmark, first + 3U, cx - half_x, bottom, cz + half_z, lower, 0, 0, normal);
    set_vertex(board.landmark, first + 4U, cx - half_x, bottom + height, cz - half_z, colour, 0, 0, normal);
    set_vertex(board.landmark, first + 5U, cx + half_x, bottom + height, cz - half_z, colour, 0, 0, normal);
    set_vertex(board.landmark, first + 6U, cx + half_x, bottom + height, cz + half_z, colour, 0, 0, normal);
    set_vertex(board.landmark, first + 7U, cx - half_x, bottom + height, cz + half_z, colour, 0, 0, normal);
    board.landmark_vertex_count += 8U;
}

void build_landmark_vertices() {
    board.landmark_vertex_count = 0;
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    constexpr std::uint64_t castle_wall =
        grandleon::core::stable_content_id_v1("mountain_castle_wall");
    constexpr std::uint64_t castle_dais =
        grandleon::core::stable_content_id_v1("paved_castle_dais");
    bool built_ruin = false;
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        const std::uint64_t terrain = board.content->terrain[index];
        const auto x = static_cast<std::int16_t>(index % width);
        const auto y = static_cast<std::int16_t>(index / width);
        const std::int16_t cx = world_x(x, width);
        const std::int16_t cz = world_z(y, height);
        const std::int16_t floor = terrain_lift(terrain);
        if (terrain == castle_wall) {
            append_box(
                cx, floor, cz, kHalfCell - 2, 42, kHalfCell - 2,
                pack_rgba(143U, 132U, 119U)
            );
            // Broken-up battlements identify masonry at board scale without
            // hiding the doorway or placing an oversized building on it.
            for (const int offset : {-12, 12}) {
                append_box(cx + offset, floor + 42, cz, 5, 9,
                           kHalfCell - 3, pack_rgba(166U, 155U, 137U));
            }
        } else if (terrain == castle_dais) {
            append_box(
                cx, floor, cz, kHalfCell - 4, 8, kHalfCell - 4,
                pack_rgba(133U, 75U, 57U)
            );
        } else if (!built_ruin && terrain_kind(terrain) == 8U) {
            append_box(cx, floor, cz, 22, 10, 18, pack_rgba(145U, 157U, 161U));
            append_box(cx - 16, floor + 10, cz, 5, 58, 5, pack_rgba(132U, 143U, 148U));
            append_box(cx + 16, floor + 10, cz, 5, 58, 5, pack_rgba(132U, 143U, 148U));
            append_box(cx, floor + 64, cz, 23, 8, 6, pack_rgba(103U, 115U, 121U));
            built_ruin = true;
        }
    }
}

bool visibility_hidden(std::size_t index) {
    const auto& cells = board.content->visual_visibility.hidden_cells;
    return std::find(cells.begin(), cells.end(), index) != cells.end();
}

void update_visual_visibility() {
    if (board.content->visual_visibility.hidden_cells.empty() || motion.active) {
        return;
    }
    const std::uint32_t first_hidden =
        board.content->visual_visibility.hidden_cells.front();
    if (board.visibility_revealed[first_hidden]) return;
    const int width = board.content->definition.width;
    for (const sim::UnitSnapshot& unit : board.game.snapshot().units) {
        if (!sim::on_board(unit) || unit.side != sim::Side::first) continue;
        const std::size_t origin = static_cast<std::size_t>(
            unit.position.y * width + unit.position.x
        );
        if (!visibility_hidden(origin)) continue;
        for (const std::uint32_t candidate :
             board.content->visual_visibility.hidden_cells) {
            board.visibility_revealed[candidate] = true;
        }
        debugf(
            "CHECKPOINT PYRITE_CASTLE_REVEALED cells=%lu\n",
            static_cast<unsigned long>(
                board.content->visual_visibility.hidden_cells.size()
            )
        );
        return;
    }
}

void build_visibility_vertices() {
    board.visibility_vertex_count = 0U;
    const int width = board.content->definition.width;
    const int height = board.content->definition.height;
    const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&up);
    for (const std::uint32_t index :
         board.content->visual_visibility.hidden_cells) {
        if (index >= board.content->terrain.size() ||
            board.visibility_revealed[index]) {
            continue;
        }
        const std::int16_t x = static_cast<std::int16_t>(index % width);
        const std::int16_t y = static_cast<std::int16_t>(index / width);
        const std::int16_t cx = world_x(x, width);
        const std::int16_t cz = world_z(y, height);
        const std::int16_t roof = static_cast<std::int16_t>(
            terrain_lift(board.content->terrain[index]) + 43
        );
        const std::uint32_t colour = (index & 1U)
            ? pack_rgba(91U, 48U, 43U) : pack_rgba(109U, 59U, 49U);
        const std::size_t first = board.visibility_vertex_count;
        assert(first + 4U <= kMaxCells * kCellVertexCount);
        set_vertex(board.visibility, first + 0U, cx - kHalfCell, roof,
            cz - kHalfCell, colour, 0, 0, normal);
        set_vertex(board.visibility, first + 1U, cx + kHalfCell, roof,
            cz - kHalfCell, colour, 0, 0, normal);
        set_vertex(board.visibility, first + 2U, cx + kHalfCell, roof,
            cz + kHalfCell, colour, 0, 0, normal);
        set_vertex(board.visibility, first + 3U, cx - kHalfCell, roof,
            cz + kHalfCell, colour, 0, 0, normal);
        board.visibility_vertex_count += 4U;
    }
}

std::int16_t scaled(std::int16_t value, int percent) {
    return static_cast<std::int16_t>(value * percent / 100);
}

void append_tree(
    std::int16_t cx,
    std::int16_t floor,
    std::int16_t cz,
    std::size_t variant,
    int scale
) {
    if (board.tree_count >= kMaxTrees) return;
    const std::size_t first = board.tree_count * kTreeVertexCount;
    const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&up);
    const std::int16_t lean = static_cast<std::int16_t>((variant % 3U) * 3U - 3);
    const std::uint32_t bark_a = pack_rgba(112U, 75U, 43U);
    const std::uint32_t bark_b = pack_rgba(82U, 57U, 37U);
    const std::uint32_t leaf_a = pack_rgba(
        54U, static_cast<std::uint8_t>(112U + (variant & 7U)), 55U);
    const std::uint32_t leaf_b = pack_rgba(
        76U, static_cast<std::uint8_t>(137U + (variant & 7U)), 63U);
    const std::int16_t trunk = scaled(5, scale);
    const std::int16_t crown = scaled(21, scale);
    const std::int16_t shoulder = scaled(43, scale);
    set_vertex(board.trees, first + 0U, cx - trunk, floor, cz - trunk, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 1U, cx + trunk, floor, cz - trunk, bark_b, 0, 0, normal);
    set_vertex(board.trees, first + 2U, cx + trunk, floor, cz + trunk, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 3U, cx - trunk, floor, cz + trunk, bark_b, 0, 0, normal);
    set_vertex(board.trees, first + 4U, cx + lean, floor + scaled(34, scale), cz, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 5U, cx, floor + scaled(24, scale), cz, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 6U, cx + lean, floor + scaled(74, scale), cz, leaf_b, 0, 0, normal);
    set_vertex(board.trees, first + 7U, cx - crown, floor + shoulder, cz, leaf_a, 0, 0, normal);
    set_vertex(board.trees, first + 8U, cx, floor + shoulder, cz - crown, leaf_b, 0, 0, normal);
    set_vertex(board.trees, first + 9U, cx + crown, floor + shoulder, cz, leaf_a, 0, 0, normal);
    set_vertex(board.trees, first + 10U, cx, floor + shoulder, cz + crown, leaf_b, 0, 0, normal);
    set_vertex(board.trees, first + 11U, cx + lean, floor + scaled(25, scale), cz, leaf_a, 0, 0, normal);
    ++board.tree_count;
}

void build_tree_vertices() {
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    board.tree_count = 0U;
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        const auto id = board.content->terrain[index];
        const std::uint8_t kind = terrain_kind(id);
        if (kind != 2U) continue;
        const auto x = static_cast<std::int16_t>(index % width);
        const auto z = static_cast<std::int16_t>(index / width);
        append_tree(
            world_x(x, width) + static_cast<std::int16_t>((index & 1U) == 0U ? -4 : 5),
            terrain_lift(id),
            world_z(z, height) + static_cast<std::int16_t>((index & 2U) == 0U ? -3 : 4),
            index,
            108 + static_cast<int>((index % 3U) * 8U)
        );
        if (index % 3U == 0U) {
            append_tree(
                world_x(x, width) + 15,
                terrain_lift(id),
                world_z(z, height) - 13,
                index + 19U,
                64 + static_cast<int>((index & 1U) * 8U)
            );
        }
    }
    // A sparse deterministic edge forest grounds the finite tactical board in
    // a larger world. These cells are decorative only: they never enter the
    // encounter dimensions, route planner, cursor or highlights.
    const int apron = std::min<int>(8, board.content->surroundings.depth);
    for (int z = -apron; z < static_cast<int>(height) + apron; ++z) {
        for (int x = -apron; x < static_cast<int>(width) + apron; ++x) {
            if (x >= 0 && x < width && z >= 0 && z < height) continue;
            const std::uint32_t hash = environment_hash(
                static_cast<std::int16_t>(x), static_cast<std::int16_t>(z)
            );
            const std::uint64_t edge_terrain = surrounding_terrain_at(x, z);
            const std::uint8_t kind = terrain_kind(edge_terrain);
            const std::uint32_t divisor =
                (kind == 2U || kind == 10U || kind == 11U) ? 2U : 5U;
            if (hash % divisor != 0U || board.tree_count >= kMaxTrees) continue;
            append_tree(
                world_x(static_cast<std::int16_t>(x), width) +
                    static_cast<std::int16_t>((hash >> 4U) % 17U) - 8,
                -4,
                world_z(static_cast<std::int16_t>(z), height) +
                    static_cast<std::int16_t>((hash >> 9U) % 17U) - 8,
                hash,
                60 + static_cast<int>((hash >> 14U) % 29U)
            );
        }
    }
}

UnitArt* art_for_unit(const sim::UnitSnapshot& unit) {
    std::uint8_t style = board.presentation.character_style_of_unit_type(
        unit.unit_type_id
    );
    std::uint8_t figure = board.presentation.character_figure_of_unit_type(
        unit.unit_type_id
    );
    const std::uint8_t archetype =
        board.presentation.archetype_of_unit_type(unit.unit_type_id);
    std::uint8_t colour =
        board.presentation.colour_of_unit_type(unit.unit_type_id);
    if (style == pr::character_style_unresolved) {
        style = grandleon_project_default_style;
    }
    if (figure == pr::character_figure_unresolved) {
        figure = grandleon_project_default_figure;
    }
    if (colour == pr::colour_unresolved) {
        colour = unit.side == sim::Side::first ? 0U : 1U;
    }
    for (std::size_t index = 0; index < grandleon_project_drawings.size(); ++index) {
        const auto& drawing = grandleon_project_drawings[index];
        if (drawing.style == style && drawing.figure == figure &&
            drawing.archetype == archetype && drawing.colour == colour) {
            return &board.art[index];
        }
    }
    assertf(
        false,
        "No generated Pyrite art for unit type %llu (%u/%u/%u/%u)",
        unit.unit_type_id, static_cast<unsigned>(style),
        static_cast<unsigned>(figure), static_cast<unsigned>(archetype),
        static_cast<unsigned>(colour)
    );
    return nullptr;
}

sprite_t* sprite_for_unit(
    const sim::UnitSnapshot& unit,
    VisualPose pose = VisualPose::idle
) {
    UnitArt* art = art_for_unit(unit);
    if (art == nullptr) return nullptr;
    switch (pose) {
        case VisualPose::idle: return art->idle;
        case VisualPose::moving: return art->moving;
        case VisualPose::close_idle: return art->close_idle;
        case VisualPose::attack: return art->attack;
        case VisualPose::hurt: return art->hurt;
    }
    return nullptr;
}

bool close_combat_phase() {
    return combat.active &&
        (combat.phase == CombatPhase::first_strike ||
         combat.phase == CombatPhase::counter);
}

constexpr std::uint32_t kCloseBlendFrames = 10U;

std::uint8_t close_overlay_alpha() {
    if (!combat.active) return 0U;
    if (combat.phase == CombatPhase::entering) {
        const std::uint32_t duration = combat.reduced_motion ? 8U : 66U;
        const std::uint32_t blend_start = duration > kCloseBlendFrames
            ? duration - kCloseBlendFrames : 0U;
        if (combat.phase_ticks <= blend_start) return 0U;
        return static_cast<std::uint8_t>(std::min<std::uint32_t>(
            255U,
            (combat.phase_ticks - blend_start) * 255U /
                std::max(std::uint32_t{1}, duration - blend_start)
        ));
    }
    if (combat.phase == CombatPhase::first_strike ||
        combat.phase == CombatPhase::counter) return 255U;
    if (combat.phase == CombatPhase::returning) {
        return static_cast<std::uint8_t>(
            255U - std::min<std::uint32_t>(
                255U, combat.phase_ticks * 255U / kCloseBlendFrames
            )
        );
    }
    return 0U;
}

VisualPose pose_for_unit(const sim::UnitSnapshot& unit) {
    if (!close_combat_phase() ||
        (unit.id != combat.attacker && unit.id != combat.defender)) {
        if (!motion.active || motion.unit != unit.id) return VisualPose::idle;
        const int cell = view::walk_cell(
            static_cast<int>(frame_count - motion.started),
            static_cast<int>(motion.total_frames)
        );
        // The exported idle image is the walk-contact pose and the move image
        // is the passing pose. Keep both halves of the shared two-pose walk;
        // using the move image for every in-between frame made the actor slide.
        return cell == view::sequence_cell_walk_pass
            ? VisualPose::moving : VisualPose::idle;
    }
    const bool counter_phase = combat.phase == CombatPhase::counter;
    const sim::UnitId striker = counter_phase ? combat.defender : combat.attacker;
    const sim::UnitId struck = counter_phase ? combat.attacker : combat.defender;
    const bool missed = counter_phase ? combat.counter_missed : combat.first_missed;
    if (unit.id == striker && combat.phase_ticks >= 16U && combat.phase_ticks < 40U) {
        return VisualPose::attack;
    }
    if (!missed && unit.id == struck &&
        combat.phase_ticks >= 30U && combat.phase_ticks < 50U) {
        return VisualPose::hurt;
    }
    return VisualPose::close_idle;
}

int lunge_percent(const sim::UnitSnapshot& unit) {
    if (!close_combat_phase()) return 0;
    const sim::UnitId striker = combat.phase == CombatPhase::counter
        ? combat.defender : combat.attacker;
    if (unit.id != striker) return 0;
    if (combat.phase_ticks < 16U || combat.phase_ticks >= 44U) return 0;
    if (combat.phase_ticks < 30U) {
        return static_cast<int>((combat.phase_ticks - 16U) * 22U / 14U);
    }
    return static_cast<int>((44U - combat.phase_ticks) * 22U / 14U);
}

void build_unit_vertices() {
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    const fm_vec3_t forward = {{0.0F, 0.0F, 1.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&forward);
    board.unit_count = 0;
    board.shadows.begin_frame();
    board.rowan_x = -1;
    board.rowan_y = -1;
    board.rowan_health = 0;
    for (const sim::UnitSnapshot& unit : board.game.snapshot().units) {
        if (!sim::on_board(unit) || board.unit_count >= kMaxUnits) continue;
        const std::size_t unit_cell = static_cast<std::size_t>(
            unit.position.y * width + unit.position.x
        );
        if (visibility_hidden(unit_cell) &&
            !board.visibility_revealed[unit_cell] &&
            !(motion.active && motion.unit == unit.id)) {
            continue;
        }
        if (sprite_for_unit(unit) == nullptr) continue;
        const std::size_t terrain_index = static_cast<std::size_t>(unit.position.y) * width +
            static_cast<std::size_t>(unit.position.x);
        std::int16_t lift = terrain_index < board.content->terrain.size()
            ? terrain_lift(board.content->terrain[terrain_index])
            : 0;
        std::int16_t cx = world_x(unit.position.x, width);
        std::int16_t cz = world_z(unit.position.y, height);
        int walk_bob = 0;
        bool facing_left = unit.id == motion.unit && motion.facing_left;
        if (motion.active && motion.unit == unit.id) {
            const std::uint32_t elapsed = frame_count - motion.started;
            const std::size_t route_index = std::min<std::size_t>(
                elapsed / view::slide_frames_per_tile,
                motion.route_count - 1U
            );
            const sim::Position segment_from = route_index == 0U
                ? motion.from : motion.route[route_index - 1U];
            const sim::Position segment_to = motion.route[route_index];
            const view::BoardHeading heading = view::board_heading_between(
                segment_from.x, segment_from.y,
                segment_to.x, segment_to.y
            );
            // The fixed isometric camera projects north toward screen-left
            // and south toward screen-right. Preserving the previous profile
            // on those legs made a diagonal route visibly walk backwards even
            // though its world-space route was correct. Until the generated
            // compass bank is resident at runtime, choose the side silhouette
            // from projected travel for all four headings.
            motion.facing_left = heading == view::BoardHeading::north
                ? true
                : heading == view::BoardHeading::south
                    ? false
                    : view::board_heading_faces_left(
                        heading, motion.facing_left
                    );
            facing_left = motion.facing_left;
            const int segment_frame = static_cast<int>(
                elapsed - route_index * view::slide_frames_per_tile
            );
            const int from_x = world_x(segment_from.x, width);
            const int from_z = world_z(segment_from.y, height);
            const int to_x = world_x(segment_to.x, width);
            const int to_z = world_z(segment_to.y, height);
            const auto route_lift = [width](sim::Position position) {
                const std::size_t index =
                    static_cast<std::size_t>(position.y) * width +
                    static_cast<std::size_t>(position.x);
                return index < board.content->terrain.size()
                    ? terrain_lift(board.content->terrain[index]) : std::int16_t{0};
            };
            const int from_lift = route_lift(segment_from);
            const int to_lift = route_lift(segment_to);
            cx = static_cast<std::int16_t>(
                view::slide_between(
                    from_x, to_x, segment_frame, view::slide_frames_per_tile
                )
            );
            cz = static_cast<std::int16_t>(
                view::slide_between(
                    from_z, to_z, segment_frame, view::slide_frames_per_tile
                )
            );
            lift = static_cast<std::int16_t>(view::slide_between(
                from_lift, to_lift, segment_frame, view::slide_frames_per_tile
            ));
            // A shallow, one-step arc gives the two authored poses a visible
            // footfall without making a tactical move read as a jump.
            walk_bob = 3 * 4 * segment_frame *
                (view::slide_frames_per_tile - segment_frame) /
                (view::slide_frames_per_tile * view::slide_frames_per_tile);
        }
        const std::int16_t right_x = 17;
        const std::int16_t right_z = 17;
        const std::int16_t sprite_height = 72;
        const std::int16_t uv_end = static_cast<std::int16_t>(
            64 << 5
        );
        const std::int16_t bottom = static_cast<std::int16_t>(
            lift + 3 + walk_bob
        );
        const std::size_t first = board.unit_count * kUnitVertexCount;
        const std::uint32_t white = pack_rgba(255U, 255U, 255U);
        const std::int16_t left_u = facing_left ? uv_end : 0;
        const std::int16_t right_u = facing_left ? 0 : uv_end;
        set_vertex(board.units, first + 0U, cx - right_x, bottom, cz - right_z, white, left_u, uv_end, normal);
        set_vertex(board.units, first + 1U, cx + right_x, bottom, cz + right_z, white, right_u, uv_end, normal);
        set_vertex(board.units, first + 2U, cx + right_x, bottom + sprite_height, cz + right_z, white, right_u, 0, normal);
        set_vertex(board.units, first + 3U, cx - right_x, bottom + sprite_height, cz - right_z, white, left_u, 0, normal);
        board.shadows.add(cx, static_cast<std::int16_t>(lift + 2), cz, 11);
        if (unit.id == board.rowan_id) {
            board.rowan_x = unit.position.x;
            board.rowan_y = unit.position.y;
            board.rowan_health = unit.health;
        }
        ++board.unit_count;
    }
}

void cursor_quad(
    std::size_t first,
    std::int16_t x0,
    std::int16_t z0,
    std::int16_t x1,
    std::int16_t z1,
    std::int16_t floor,
    std::uint32_t colour
) {
    const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&up);
    set_vertex(board.cursor, first + 0U, x0, floor, z0, colour, 0, 0, normal);
    set_vertex(board.cursor, first + 1U, x1, floor, z0, colour, 0, 0, normal);
    set_vertex(board.cursor, first + 2U, x1, floor, z1, colour, 0, 0, normal);
    set_vertex(board.cursor, first + 3U, x0, floor, z1, colour, 0, 0, normal);
}

void build_cursor_vertices() {
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    constexpr std::int16_t outer = kHalfCell - 2;
    constexpr std::int16_t inner = kHalfCell - 6;
    const sim::EncounterSnapshot& snapshot = board.game.snapshot();
    std::size_t first = 0U;
    if (snapshot.deploying) {
        for (const sim::Position tile : snapshot.deployment_tiles) {
            const std::size_t terrain_index =
                static_cast<std::size_t>(tile.y) * width +
                static_cast<std::size_t>(tile.x);
            const std::int16_t floor = static_cast<std::int16_t>(
                (terrain_index < board.content->terrain.size()
                     ? terrain_lift(board.content->terrain[terrain_index]) : 0) +
                2
            );
            const std::int16_t cx = world_x(tile.x, width);
            const std::int16_t cz = world_z(tile.y, height);
            cursor_quad(
                first, cx - 10, cz - 10, cx + 10, cz + 10, floor,
                pack_rgba(62U, 184U, 171U)
            );
            first += 4U;
        }
    }
    const std::size_t cursor_first = first;
    board.cursor_vertex_count = cursor_first +
        static_cast<std::size_t>(board.game.local_players()) *
        kCursorVerticesPerPlayer;
    for (std::uint8_t player = 0U; player < board.game.local_players(); ++player) {
        const sim::Position cursor = board.game.cursor(player);
        const std::size_t terrain_index =
            static_cast<std::size_t>(cursor.y) * width +
            static_cast<std::size_t>(cursor.x);
        const std::int16_t floor = static_cast<std::int16_t>(
            (terrain_index < board.content->terrain.size()
                 ? terrain_lift(board.content->terrain[terrain_index]) : 0) +
            3 + player
        );
        const std::int16_t cx = world_x(cursor.x, width);
        const std::int16_t cz = world_z(cursor.y, height);
        const std::size_t player_first = cursor_first +
            static_cast<std::size_t>(player) * kCursorVerticesPerPlayer;
        const bool selected = board.game.selected(player) != 0U;
        const std::uint32_t colour = player == 0U
            ? (selected ? pack_rgba(107U, 221U, 255U)
                        : pack_rgba(246U, 190U, 58U))
            : (selected ? pack_rgba(255U, 139U, 194U)
                        : pack_rgba(190U, 116U, 255U));
        cursor_quad(player_first + 0U, cx - outer, cz - outer,
            cx + outer, cz - inner, floor, colour);
        cursor_quad(player_first + 4U, cx - outer, cz + inner,
            cx + outer, cz + outer, floor, colour);
        cursor_quad(player_first + 8U, cx - outer, cz - inner,
            cx - inner, cz + inner, floor, colour);
        cursor_quad(player_first + 12U, cx + inner, cz - inner,
            cx + outer, cz + inner, floor, colour);
    }
}

void load_runtime_board() {
    const bool loaded = board.runtime.load_package("rom:/game.gpk");
    assertf(
        loaded,
        "Authored project runtime load failed: %u",
        static_cast<unsigned>(board.runtime.error())
    );
    const pr::PresentationLoadResult presentation = pr::load_presentation(
        board.runtime.package()
    );
    assertf(static_cast<bool>(presentation), "Project presentation is malformed");
    board.presentation = presentation.presentation;
    board.cells = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxCells * kCellVertexCount / 2U)));
    board.ground = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kGroundVertexCount / 2U)));
    board.apron = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxApronCells * kCellVertexCount / 2U)));
    board.elevation = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxElevationVertices / 2U)));
    board.terrain_forms = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) *
            (kMaxTerrainForms * kTerrainFormVertexCount / 2U)));
    board.trees = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxTrees * kTreeVertexCount / 2U)));
    board.landmark = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxLandmarkVertices / 2U)));
    board.visibility = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) *
            (kMaxCells * kCellVertexCount / 2U)));
    board.units = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxUnits * kUnitVertexCount / 2U)));
    board.cursor = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kCursorVertexCount / 2U)));
    assert(board.cells && board.ground && board.apron && board.elevation &&
        board.terrain_forms && board.trees && board.landmark &&
        board.visibility && board.units && board.cursor);
    board.terrain = sprite_load("rom:/material_atlas.sprite");
    board.surface_detail = sprite_load("rom:/surface_detail.sprite");
    board.combat_backdrop = sprite_load("rom:/combat_backdrop.sprite");
    board.combat_shadow = sprite_load("rom:/soft_shadow.sprite");
    char path[64]{};
    for (std::size_t index = 0; index < board.art.size(); ++index) {
        const auto load = [index, &path](const char* suffix) {
            std::snprintf(
                path, sizeof path, "rom:/unit_%02lu_%s.sprite",
                static_cast<unsigned long>(index), suffix
            );
            return sprite_load(path);
        };
        board.art[index] = {
            load("board_idle"), load("board_move"), load("close_idle"),
            load("close_attack"), load("close_hurt"),
        };
    }
    wav64_open(&board.ambience, "rom:/ambience.wav64");
    wav64_open(&board.ui_cue, "rom:/ui_cue.wav64");
    wav64_open(&board.impact_cue, "rom:/impact_cue.wav64");
    wav64_open(&board.victory_cue, "rom:/victory_cue.wav64");
    const auto complete_art = [](const UnitArt& art) {
        return art.idle && art.moving && art.close_idle && art.attack && art.hurt;
    };
    assert(board.terrain && board.surface_detail && board.combat_backdrop && board.combat_shadow);
    for (const UnitArt& art : board.art) assert(complete_art(art));
    assertf(board.shadows.open("rom:/soft_shadow.sprite"), "Soft shadow asset failed");
    board.loaded = true;
    debugf(
        "GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none "
        "surroundings=package hud_default=off\n"
    );
    debugf(
        "GRANDLEON_PYRITE_RUNTIME game=%s campaign=%s package=%lu "
        "drawings=%lu renderer=pyrite-tiny3d\n",
        grandleon_project_game_id, grandleon_project_campaign_source,
        static_cast<unsigned long>(board.runtime.package_size()),
        static_cast<unsigned long>(board.art.size())
    );
}

bool load_battle_moment_dialogue() {
    while (battle_moment.dialogue_index <
           battle_moment.dialogue_ids.size()) {
        const pr::DialogueLoadResult loaded = pr::load_dialogue(
            board.runtime.package(),
            battle_moment.dialogue_ids[battle_moment.dialogue_index]
        );
        if (loaded && !loaded.dialogue.lines.empty()) {
            battle_moment.dialogue = loaded.dialogue;
            battle_moment.line_index = 0U;
            battle_moment.active = true;
            battle_moment.next_automatic_frame = frame_count + 75U;
            debugf(
                "CHECKPOINT PYRITE_BATTLE_MOMENT dialogue=%llu lines=%lu\n",
                battle_moment.dialogue.id,
                static_cast<unsigned long>(battle_moment.dialogue.lines.size())
            );
            return true;
        }
        ++battle_moment.dialogue_index;
    }
    battle_moment = {};
    return false;
}

void queue_battle_moments(
    pr::MomentTrigger trigger,
    std::uint64_t placement_id = 0U,
    bool reset = false
) {
    if (reset) battle_moment = {};
    for (const pr::EncounterMoment& moment : board.content->moments) {
        if (moment.trigger != trigger) continue;
        if (moment.placement_id != 0U && moment.placement_id != placement_id) {
            continue;
        }
        battle_moment.dialogue_ids.push_back(moment.dialogue_id);
    }
}

bool advance_battle_moment() {
    if (!battle_moment.active) return false;
    play_ui_cue();
    ++battle_moment.line_index;
    if (battle_moment.line_index < battle_moment.dialogue.lines.size()) {
        battle_moment.next_automatic_frame = frame_count + 75U;
        return true;
    }
    ++battle_moment.dialogue_index;
    return load_battle_moment_dialogue();
}

void update_battle_moment() {
    if (!battle_moment.active && !battle_moment.dialogue_ids.empty() &&
        !combat.active) {
        (void)load_battle_moment_dialogue();
    }
    if (battle_moment.active && !replay.user_took_over &&
        frame_count >= battle_moment.next_automatic_frame) {
        (void)advance_battle_moment();
    }
}

void queue_event_moments(const std::vector<sim::Event>& events) {
    for (const sim::Event& event : events) {
        pr::MomentTrigger trigger{};
        if (event.type == sim::EventType::unit_defeated) {
            trigger = pr::MomentTrigger::character_falls;
        } else if (event.type == sim::EventType::unit_talked) {
            trigger = pr::MomentTrigger::character_talked;
        } else {
            continue;
        }
        queue_battle_moments(trigger, event.unit_id);
    }
}

void configure_campaign_board(const pr::EncounterLoadResult& content) {
    board.content = &content;
    board.rowan_id = 0U;
    board.tomas_id = 0U;
    assertf(
        content.terrain.size() <= kMaxCells &&
            content.definition.units.size() <= kMaxUnits,
        "Authored battle exceeds Pyrite base-memory board limits"
    );
    for (std::size_t index = 0; index < content.placements.size(); ++index) {
        if (index >= content.definition.units.size()) break;
        const sim::UnitDefinition& unit = content.definition.units[index];
        if (unit.side != sim::Side::first) continue;
        if (board.rowan_id == 0U) board.rowan_id = unit.id;
        if (board.tomas_id == 0U && !unit.ability_ids.empty()) {
            board.tomas_id = unit.id;
        }
    }
    assertf(board.rowan_id != 0U, "Authored battle has no player unit");
    if (board.tomas_id == board.rowan_id) board.tomas_id = 0U;
    board.game = {};
    assertf(
        board.game.open(
            content.definition, board.rowan_id, shell.local_players
        ),
        "Campaign encounter could not be created"
    );
    const sim::UnitSnapshot* rowan = nullptr;
    for (const sim::UnitSnapshot& unit : board.game.snapshot().units) {
        if (unit.id == board.rowan_id) rowan = &unit;
    }
    assert(rowan != nullptr);
    replay = {};
    replay.rowan_start = rowan->position;
    replay.rowan_destination = rowan->position;
    int best_score = -1;
    for (const sim::Position candidate :
         sim::reachable_tiles(board.game.snapshot(), board.rowan_id)) {
        if (candidate == rowan->position) continue;
        const int dx = std::abs(candidate.x - rowan->position.x);
        const int dy = std::abs(candidate.y - rowan->position.y);
        const int score = (dx > 0 && dy > 0 ? 100 : 0) + dx + dy;
        if (score > best_score) {
            best_score = score;
            replay.rowan_destination = candidate;
        }
    }
    const std::uint64_t encounter_id = board.campaign->standing().encounter_id;
    const auto stage = std::find(
        grandleon_project_encounter_ids.begin(),
        grandleon_project_encounter_ids.end(), encounter_id
    );
    assertf(
        stage != grandleon_project_encounter_ids.end(),
        "Campaign reached an encounter outside the generated export manifest"
    );
    shell.stage_index = static_cast<std::size_t>(
        stage - grandleon_project_encounter_ids.begin()
    );
    // A stage change replaces one resident plate; the campaign's complete
    // backdrop bank stays in the ROM rather than accumulating in base RAM.
    rspq_wait();
    sprite_free(board.combat_backdrop);
    board.combat_backdrop = sprite_load(
        grandleon_project_backdrop_paths[shell.stage_index]
    );
    assert(board.combat_backdrop);
    if (shell.resumed && shell.completed_battles < shell.stage_index) {
        shell.completed_battles = shell.stage_index;
    }
    replay.phase = shell.stage_index == 0U &&
        replay.rowan_destination != rowan->position
        ? ReplayPhase::select_rowan : ReplayPhase::explore_northwest;
    replay.next_frame = frame_count + 30U;
    motion = {};
    planned_motion = {};
    board.visibility_revealed.fill(false);
    const bool close_enabled = combat.enabled;
    const bool reduced_motion = combat.reduced_motion;
    combat = {};
    combat.enabled = close_enabled;
    combat.reduced_motion = reduced_motion;
    battle_ui = {};
    board.battle_events.clear();
    camera_configured = false;
    restore_checkpoint_delay = 0U;
    build_environment_vertices();
    build_cell_vertices();
    build_elevation_vertices();
    build_terrain_form_vertices();
    build_tree_vertices();
    build_landmark_vertices();
    build_visibility_vertices();
    build_unit_vertices();
    build_cursor_vertices();
    if (!board.game.snapshot().deploying) {
        queue_battle_moments(pr::MomentTrigger::stage_opens, 0U, true);
        (void)load_battle_moment_dialogue();
    }
    debugf(
        "GRANDLEON_PYRITE_CAMPAIGN_BOARD encounter=%llu map=%llu size=%ux%u "
        "players=%u stage=%lu apron=%lu\n",
        board.campaign->standing().encounter_id, content.map_id,
        content.definition.width, content.definition.height,
        static_cast<unsigned>(shell.local_players),
        static_cast<unsigned long>(shell.stage_index + 1U),
        static_cast<unsigned long>(board.apron_cell_count)
    );
    debugf(
        "CHECKPOINT PYRITE_CAMPAIGN_STAGE index=%lu map=%llu\n",
        static_cast<unsigned long>(shell.stage_index + 1U), content.map_id
    );
    if (!content.visual_visibility.hidden_cells.empty()) {
        debugf(
            "CHECKPOINT PYRITE_CASTLE_CLOSED cells=%lu\n",
            static_cast<unsigned long>(
                content.visual_visibility.hidden_cells.size()
            )
        );
    }
    if (shell.stage_index == 0U) debugf("CHECKPOINT PYRITE_CAMPAIGN_FIRST\n");
}

bool load_story_dialogue() {
    if (shell.story_id_index >= shell.story_ids.size()) return false;
    const pr::DialogueLoadResult loaded = pr::load_dialogue(
        board.runtime.package(), shell.story_ids[shell.story_id_index]
    );
    if (!loaded) return false;
    shell.story = loaded.dialogue;
    shell.story_line_index = 0U;
    return !shell.story.lines.empty();
}

bool begin_story(const client::CampaignSession::Standing& standing) {
    shell.story_ids = standing.dialogue_ids;
    shell.story_id_index = 0U;
    if (!load_story_dialogue()) return false;
    shell.phase = ShellPhase::story;
    shell.next_automatic_frame = frame_count + 75U;
    debugf(
        "CHECKPOINT PYRITE_STORY node=%llu dialogue=%llu lines=%lu\n",
        standing.node.stable_id, shell.story.id,
        static_cast<unsigned long>(shell.story.lines.size())
    );
    return true;
}

bool deployment_contains(campaign::PersistentEntityId member) {
    return std::any_of(
        shell.deployment.fielded.begin(), shell.deployment.fielded.end(),
        [member](campaign::PersistentEntityId candidate) {
            return candidate.value == member.value;
        }
    );
}

bool begin_required_deployment() {
    shell.deployment = board.campaign->management();
    if (shell.deployment.error != client::CampaignSessionError::none) {
        return false;
    }
    if (!shell.deployment.has_deployment_options) return false;
    shell.deployment_row = 0U;
    shell.phase = ShellPhase::deployment;
    shell.next_automatic_frame = frame_count + 90U;
    debugf(
        "CHECKPOINT PYRITE_DEPLOYMENT required=1 placeable=%lu fielded=%lu "
        "capacity=%u\n",
        static_cast<unsigned long>(shell.deployment.placeable.size()),
        static_cast<unsigned long>(shell.deployment.fielded.size()),
        static_cast<unsigned>(shell.deployment.capacity)
    );
    return true;
}

void queue_player_names(const std::vector<client::RosterEntry>& roster) {
    for (const client::RosterEntry& member : roster) {
        if (!member.player_nameable || member.has_player_name) continue;
        const bool queued = std::any_of(
            shell.pending_player_names.begin(),
            shell.pending_player_names.end(),
            [&member](campaign::PersistentEntityId candidate) {
                return candidate == member.member;
            }
        );
        if (!queued) shell.pending_player_names.push_back(member.member);
    }
}

bool begin_next_player_name() {
    if (!board.campaign.has_value()) return false;
    while (!shell.pending_player_names.empty()) {
        const campaign::PersistentEntityId requested =
            shell.pending_player_names.front();
        shell.pending_player_names.erase(shell.pending_player_names.begin());
        const client::CompanyManagement company = board.campaign->management();
        if (company.error != client::CampaignSessionError::none) return false;
        const auto found = std::find_if(
            company.roster.begin(), company.roster.end(),
            [requested](const client::RosterEntry& member) {
                return member.member == requested;
            }
        );
        if (found == company.roster.end() || !found->player_nameable ||
            found->has_player_name) {
            continue;
        }
        shell.name_member = *found;
        const std::string_view authored = found->authored_name.empty()
            ? std::string_view(found->name)
            : std::string_view(found->authored_name);
        shell.name_editor.emplace(found->name, authored);
        shell.phase = ShellPhase::name;
        shell.next_automatic_frame = 0U;
        debugf(
            "CHECKPOINT PYRITE_NAME_PROMPT member=%llu limit=%lu\n",
            requested.value,
            static_cast<unsigned long>(client::console_player_name_glyphs)
        );
        return true;
    }
    shell.name_editor.reset();
    shell.name_member = {};
    return false;
}

void begin_performance_sample();

bool stand_on_campaign_encounter() {
    assert(board.campaign.has_value());
    for (unsigned step = 0; step < 8U; ++step) {
        const client::CampaignSession::Standing standing =
            board.campaign->standing();
        if (standing.error != client::CampaignSessionError::none) return false;
        if (standing.kind == pr::CampaignNodeKind::encounter) {
            if (!shell.deployment_approved) {
                const client::CompanyManagement company =
                    board.campaign->management();
                if (company.error != client::CampaignSessionError::none) {
                    return false;
                }
                if (company.has_deployment_options) {
                    return begin_required_deployment();
                }
                debugf(
                    "CHECKPOINT PYRITE_DEPLOYMENT required=0 encounter=%llu\n",
                    standing.encounter_id
                );
            }
            client::CampaignSession::PreparedBoard prepared =
                board.campaign->prepare_board();
            if (prepared.error != client::CampaignSessionError::none) return false;
            board.campaign_content = std::move(prepared.encounter.encounter);
            configure_campaign_board(board.campaign_content);
            shell.deployment_approved = false;
            shell.phase = ShellPhase::battle;
            return true;
        }
        if (standing.kind == pr::CampaignNodeKind::terminal) {
            if (shell.resumed) {
                shell.completed_battles = grandleon_project_encounter_count;
                shell.stage_index = grandleon_project_encounter_count - 1U;
            }
            shell.phase = ShellPhase::complete;
            shell.campaign_advanced_checkpoint = true;
            shell.next_automatic_frame = 0U;
            begin_performance_sample();
            debugf(
                "CHECKPOINT PYRITE_CAMPAIGN_COMPLETE battles=%lu "
                "slot_bytes=%lu\n",
                static_cast<unsigned long>(shell.completed_battles),
                static_cast<unsigned long>(storage::used_bytes(*board.saves))
            );
            return true;
        }
        if (!standing.dialogue_ids.empty()) return begin_story(standing);
        std::vector<client::RosterEntry> joined;
        if (board.campaign->advance_story(joined) !=
            client::CampaignSessionError::none) return false;
        if (board.campaign->save() != storage::StorageError::none) return false;
        queue_player_names(joined);
        if (begin_next_player_name()) return true;
    }
    return false;
}

bool advance_story_beat() {
    if (shell.story_line_index + 1U < shell.story.lines.size()) {
        ++shell.story_line_index;
        shell.next_automatic_frame = frame_count + 75U;
        return true;
    }
    if (shell.story_id_index + 1U < shell.story_ids.size()) {
        ++shell.story_id_index;
        if (!load_story_dialogue()) return false;
        shell.next_automatic_frame = frame_count + 75U;
        return true;
    }
    std::vector<client::RosterEntry> joined;
    if (board.campaign->advance_story(joined) !=
        client::CampaignSessionError::none) return false;
    if (board.campaign->save() != storage::StorageError::none) return false;
    shell.story_ids.clear();
    shell.story = {};
    queue_player_names(joined);
    if (begin_next_player_name()) return true;
    if (!stand_on_campaign_encounter()) return false;
    if (shell.phase == ShellPhase::battle && shell.first_battle_committed &&
        shell.stage_index == 1U) {
        debugf(
            "CHECKPOINT PYRITE_CAMPAIGN_ADVANCE first_commands=%lu enemy=%lu "
            "slot_bytes=%lu\n",
            static_cast<unsigned long>(shell.first_battle_commands),
            static_cast<unsigned long>(shell.first_battle_enemy_commands),
            static_cast<unsigned long>(storage::used_bytes(*board.saves))
        );
    }
    return true;
}

bool begin_campaign(bool resume) {
    assert(board.saves.has_value());
    if (!resume && board.saves->contains(campaign_slot())) {
        if (board.saves->erase(campaign_slot()) != storage::StorageError::none) {
            return false;
        }
    }
    board.campaign.reset();
    board.campaign_boards.reset();
    board.campaign_boards.emplace(
        board.runtime.package(), shell.local_players
    );
    client::CampaignSessionOptions options;
    options.slot = campaign_slot();
    options.resume = resume;
    options.player_side = sim::Side::first;
    options.local_players = shell.local_players;
    options.stage_picker = false;
    board.campaign.emplace(
        board.runtime.package(), grandleon_project_campaign_id,
        *board.campaign_boards, *board.saves, options
    );
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    const client::CampaignSessionError begun =
        board.campaign->begin(failure, refused, resumed);
    if (begun != client::CampaignSessionError::none) return false;
    shell.resumed = resumed;
    shell.pending_player_names.clear();
    if (!resumed) {
        const client::CompanyManagement company = board.campaign->management();
        if (company.error != client::CampaignSessionError::none) return false;
        queue_player_names(company.roster);
        if (begin_next_player_name()) return true;
    }
    if (!stand_on_campaign_encounter()) return false;
    if (resumed && shell.phase == ShellPhase::battle) {
        debugf(
            "CHECKPOINT PYRITE_CAMPAIGN_RESUME stage=%lu map=%llu\n",
            static_cast<unsigned long>(shell.stage_index + 1U),
            board.content->map_id
        );
    }
    debugf(
        "GRANDLEON_PYRITE_CAMPAIGN_BEGIN players=%u requested_resume=%u "
        "resumed=%u refused=%u slot_bytes=%lu\n",
        static_cast<unsigned>(shell.local_players), resume ? 1U : 0U,
        resumed ? 1U : 0U, refused ? 1U : 0U,
        static_cast<unsigned long>(storage::used_bytes(*board.saves))
    );
    return true;
}

void enter_shell_phase(ShellPhase phase) {
    shell.phase = phase;
    shell.next_automatic_frame = frame_count + 75U;
    if (phase == ShellPhase::save) {
        shell.save_present = board.saves->contains(campaign_slot());
        shell.save_choice = shell.save_present ? 1U : 0U;
    }
}

bool toggle_deployment_member() {
    if (shell.deployment_row >= shell.deployment.roster.size()) return false;
    const client::RosterEntry& entry =
        shell.deployment.roster[shell.deployment_row];
    const bool fielded = deployment_contains(entry.member);
    if (fielded && shell.deployment.fielded.size() <= 1U) return false;
    const bool placeable = std::any_of(
        shell.deployment.placeable.begin(), shell.deployment.placeable.end(),
        [&entry](campaign::PersistentEntityId candidate) {
            return candidate.value == entry.member.value;
        }
    );
    if (!fielded && (!placeable ||
        (shell.deployment.capacity != 0U &&
         shell.deployment.fielded.size() >= shell.deployment.capacity))) {
        return false;
    }
    const client::ManagementCommit committed =
        board.campaign->set_fielded(entry.member, !fielded);
    if (committed.error != client::ManagementError::none || !committed.saved ||
        committed.save != storage::StorageError::none ||
        !committed.application) {
        return false;
    }
    shell.deployment = board.campaign->management();
    return shell.deployment.error == client::CampaignSessionError::none;
}

bool finish_player_name(client::ConsoleNameAnswer answer) {
    if (!shell.name_editor.has_value() ||
        answer == client::ConsoleNameAnswer::editing) {
        return false;
    }
    if (answer != client::ConsoleNameAnswer::cancel) {
        const std::string_view authored =
            shell.name_member.authored_name.empty()
                ? std::string_view(shell.name_member.name)
                : std::string_view(shell.name_member.authored_name);
        const std::string_view selected =
            answer == client::ConsoleNameAnswer::use_default
                ? authored : shell.name_editor->name();
        const client::ManagementCommit committed =
            board.campaign->set_player_name(
                shell.name_member.member, selected
            );
        if (committed.error != client::ManagementError::none ||
            !committed.saved ||
            committed.save != storage::StorageError::none) {
            return false;
        }
        debugf(
            "CHECKPOINT PYRITE_NAME_SAVED member=%llu glyphs=%lu default=%u\n",
            shell.name_member.member.value,
            static_cast<unsigned long>(selected.size()),
            answer == client::ConsoleNameAnswer::use_default ? 1U : 0U
        );
    } else {
        debugf(
            "CHECKPOINT PYRITE_NAME_CANCEL member=%llu wrote=0\n",
            shell.name_member.member.value
        );
    }
    shell.name_editor.reset();
    shell.name_member = {};
    if (begin_next_player_name()) return true;
    return stand_on_campaign_encounter();
}

bool confirm_shell_choice() {
    play_ui_cue();
    switch (shell.phase) {
        case ShellPhase::title:
            if (shell.title_row == 2U && game_languages().size() > 1U) {
                return cycle_game_language();
            }
            if (shell.title_row == 1U) {
                enter_shell_phase(ShellPhase::help);
                return true;
            }
            enter_shell_phase(ShellPhase::players);
            debugf("CHECKPOINT PYRITE_TITLE\n");
            return true;
        case ShellPhase::help:
            enter_shell_phase(ShellPhase::title);
            return true;
        case ShellPhase::players:
            shell.second_connected = joypad_is_connected(JOYPAD_PORT_2);
            if (shell.player_choice == 1U && !shell.second_connected) {
                return false;
            }
            shell.local_players = shell.player_choice == 0U ? 1U : 2U;
            enter_shell_phase(ShellPhase::save);
            debugf(
                "CHECKPOINT PYRITE_PLAYERS count=%u\n",
                static_cast<unsigned>(shell.local_players)
            );
            return true;
        case ShellPhase::save: {
            const bool resume = shell.save_choice == 1U && shell.save_present;
            const bool opened = begin_campaign(resume);
            assertf(opened, "Authored campaign could not begin");
            debugf(
                "CHECKPOINT PYRITE_SAVE mode=%s\n",
                resume ? "continue" : "new"
            );
            return opened;
        }
        case ShellPhase::story:
            return advance_story_beat();
        case ShellPhase::name:
            return false;
        case ShellPhase::deployment:
            shell.deployment_approved = true;
            return stand_on_campaign_encounter();
        case ShellPhase::aftermath:
            shell.aftermath = {};
            shell.aftermath_objectives.clear();
            return stand_on_campaign_encounter();
        case ShellPhase::complete:
            return false;
        case ShellPhase::battle:
            return false;
    }
    return false;
}

void update_shell_input() {
    if (shell.phase == ShellPhase::battle) {
        return;
    }
    const joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    shell.second_connected = joypad_is_connected(JOYPAD_PORT_2);
    const bool physical = pressed.a || pressed.start || pressed.d_up ||
        pressed.d_down || pressed.d_left || pressed.d_right || pressed.b ||
        pressed.c_up;
    if (physical) shell.user_took_over = true;
    if (shell.phase == ShellPhase::name && shell.name_editor.has_value()) {
        if (pressed.d_left || pressed.d_right ||
            pressed.d_up || pressed.d_down) {
            shell.name_editor->move(
                pressed.d_left ? -1 : pressed.d_right ? 1 : 0,
                pressed.d_up ? -1 : pressed.d_down ? 1 : 0
            );
        }
        if (pressed.b) shell.name_editor->erase();
        if (pressed.c_up) {
            (void)finish_player_name(client::ConsoleNameAnswer::cancel);
            return;
        }
        client::ConsoleNameAnswer answer =
            client::ConsoleNameAnswer::editing;
        if (pressed.a) answer = shell.name_editor->activate();
        if (pressed.start) answer = shell.name_editor->save();
        if (answer != client::ConsoleNameAnswer::editing) {
            (void)finish_player_name(answer);
        }
        return;
    }
    if (shell.phase == ShellPhase::title &&
        (pressed.d_up || pressed.d_down || pressed.d_left || pressed.d_right)) {
        const std::uint8_t rows = game_languages().size() > 1U ? 3U : 2U;
        if (pressed.d_up || pressed.d_left) {
            shell.title_row = shell.title_row == 0U
                ? static_cast<std::uint8_t>(rows - 1U)
                : static_cast<std::uint8_t>(shell.title_row - 1U);
        } else {
            shell.title_row = static_cast<std::uint8_t>(
                (shell.title_row + 1U) % rows
            );
        }
    } else if (shell.phase == ShellPhase::help && pressed.b) {
        enter_shell_phase(ShellPhase::title);
        return;
    } else if (shell.phase == ShellPhase::players &&
        (pressed.d_up || pressed.d_down || pressed.d_left || pressed.d_right)) {
        if (shell.allow_one && shell.allow_two && shell.second_connected) {
            shell.player_choice ^= 1U;
        }
    } else if (shell.phase == ShellPhase::save && shell.save_present &&
        (pressed.d_up || pressed.d_down || pressed.d_left || pressed.d_right)) {
        shell.save_choice ^= 1U;
    } else if (shell.phase == ShellPhase::deployment &&
               !shell.deployment.roster.empty()) {
        if (pressed.d_up) {
            shell.deployment_row = shell.deployment_row == 0U
                ? shell.deployment.roster.size() - 1U
                : shell.deployment_row - 1U;
        } else if (pressed.d_down) {
            shell.deployment_row =
                (shell.deployment_row + 1U) % shell.deployment.roster.size();
        }
        if (pressed.a) {
            (void)toggle_deployment_member();
            return;
        }
        if (pressed.start) {
            (void)confirm_shell_choice();
            return;
        }
    }
    if ((pressed.a || pressed.start) &&
        shell.phase != ShellPhase::deployment) {
        (void)confirm_shell_choice();
        return;
    }
    if (shell.phase != ShellPhase::complete && !shell.user_took_over &&
        frame_count >= shell.next_automatic_frame) {
        (void)confirm_shell_choice();
    }
}

void draw_environment() {
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_DEPTH
    ));
    t3d_vert_load(board.ground, 0, kGroundVertexCount);
    t3d_tri_draw(0, 1, 2);
    t3d_tri_draw(0, 2, 3);
    t3d_tri_sync();
    for (std::size_t begin = 0; begin < board.apron_cell_count;
         begin += kBatchCells) {
        const std::size_t count = std::min(
            kBatchCells, board.apron_cell_count - begin
        );
        t3d_vert_load(
            board.apron + begin * 2U, 0,
            static_cast<std::uint32_t>(count * 4U)
        );
        for (std::size_t local = 0; local < count; ++local) {
            const std::uint8_t first = static_cast<std::uint8_t>(local * 4U);
            t3d_tri_draw(first + 0U, first + 1U, first + 2U);
            t3d_tri_draw(first + 0U, first + 2U, first + 3U);
        }
        t3d_tri_sync();
    }
}

void draw_board_cells() {
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    rdpq_sprite_upload(TILE0, board.terrain, nullptr);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH));
    t3d_state_set_vertex_fx(T3D_VERTEX_FX_NONE, 0, 0);
    const std::size_t cell_count = board.content->terrain.size();
    for (std::size_t begin = 0; begin < cell_count; begin += kBatchCells) {
        const std::size_t count = std::min(kBatchCells, cell_count - begin);
        t3d_vert_load(board.cells + begin * 2U, 0, static_cast<std::uint32_t>(count * 4U));
        for (std::size_t local = 0; local < count; ++local) {
            const std::uint8_t first = static_cast<std::uint8_t>(local * 4U);
            t3d_tri_draw(first + 0U, first + 1U, first + 2U);
            t3d_tri_draw(first + 0U, first + 2U, first + 3U);
        }
        t3d_tri_sync();
    }
}

void use_surface_detail() {
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_tlut(TLUT_NONE);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    rdpq_sprite_upload(TILE0, board.surface_detail, nullptr);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH
    ));
}

void draw_elevation() {
    if (board.elevation_vertex_count == 0U) return;
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_DEPTH
    ));
    const std::size_t face_count = board.elevation_vertex_count / 4U;
    use_surface_detail();
    for (std::size_t begin = 0; begin < face_count; begin += kBatchCells) {
        const std::size_t count = std::min(kBatchCells, face_count - begin);
        t3d_vert_load(
            board.elevation + begin * 2U, 0,
            static_cast<std::uint32_t>(count * 4U)
        );
        for (std::size_t local = 0; local < count; ++local) {
            const std::uint8_t first = static_cast<std::uint8_t>(local * 4U);
            t3d_tri_draw(first + 0U, first + 1U, first + 2U);
            t3d_tri_draw(first + 0U, first + 2U, first + 3U);
        }
        t3d_tri_sync();
    }
}

void draw_box(std::uint8_t first) {
    t3d_tri_draw(first + 0U, first + 1U, first + 5U);
    t3d_tri_draw(first + 0U, first + 5U, first + 4U);
    t3d_tri_draw(first + 1U, first + 2U, first + 6U);
    t3d_tri_draw(first + 1U, first + 6U, first + 5U);
    t3d_tri_draw(first + 2U, first + 3U, first + 7U);
    t3d_tri_draw(first + 2U, first + 7U, first + 6U);
    t3d_tri_draw(first + 3U, first + 0U, first + 4U);
    t3d_tri_draw(first + 3U, first + 4U, first + 7U);
    t3d_tri_draw(first + 4U, first + 5U, first + 6U);
    t3d_tri_draw(first + 4U, first + 6U, first + 7U);
}

void draw_terrain_forms() {
    if (board.terrain_form_count == 0U) return;
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_DEPTH
    ));
    use_surface_detail();
    for (std::size_t index = 0; index < board.terrain_form_count; ++index) {
        t3d_vert_load(
            board.terrain_forms + index * (kTerrainFormVertexCount / 2U),
            0, kTerrainFormVertexCount
        );
        if (board.terrain_form_shapes[index] ==
            static_cast<std::uint8_t>(TerrainFormShape::crossed)) {
            t3d_tri_draw(0U, 1U, 4U);
            t3d_tri_draw(0U, 4U, 5U);
            t3d_tri_draw(4U, 1U, 0U);
            t3d_tri_draw(5U, 4U, 0U);
            t3d_tri_draw(2U, 3U, 6U);
            t3d_tri_draw(2U, 6U, 7U);
            t3d_tri_draw(6U, 3U, 2U);
            t3d_tri_draw(7U, 6U, 2U);
        } else {
            draw_box(0U);
        }
        t3d_tri_sync();
    }
}

void draw_landmark() {
    if (board.landmark_vertex_count == 0U) return;
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_DEPTH
    ));
    constexpr std::size_t boxes_per_batch = 8U;
    const std::size_t box_count = board.landmark_vertex_count / 8U;
    use_surface_detail();
    for (std::size_t begin = 0U; begin < box_count;
         begin += boxes_per_batch) {
        const std::size_t count = std::min(
            boxes_per_batch, box_count - begin
        );
        t3d_vert_load(
            board.landmark + begin * 4U, 0,
            static_cast<std::uint32_t>(count * 8U)
        );
        for (std::size_t local = 0U; local < count; ++local) {
            draw_box(static_cast<std::uint8_t>(local * 8U));
        }
        t3d_tri_sync();
    }
}

void draw_visual_visibility() {
    if (board.visibility_vertex_count == 0U) return;
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_DEPTH
    ));
    const std::size_t cell_count = board.visibility_vertex_count / 4U;
    for (std::size_t begin = 0; begin < cell_count; begin += kBatchCells) {
        const std::size_t count = std::min(kBatchCells, cell_count - begin);
        t3d_vert_load(
            board.visibility + begin * 2U, 0,
            static_cast<std::uint32_t>(count * 4U)
        );
        for (std::size_t local = 0; local < count; ++local) {
            const std::uint8_t first = static_cast<std::uint8_t>(local * 4U);
            t3d_tri_draw(first + 0U, first + 1U, first + 2U);
            t3d_tri_draw(first + 0U, first + 2U, first + 3U);
        }
        t3d_tri_sync();
    }
}

void draw_trees() {
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
    use_surface_detail();
    for (std::size_t index = 0; index < board.tree_count; ++index) {
        t3d_vert_load(board.trees + index * (kTreeVertexCount / 2U), 0, kTreeVertexCount);
        t3d_tri_draw(0, 1, 4); t3d_tri_draw(1, 2, 4);
        t3d_tri_draw(2, 3, 4); t3d_tri_draw(3, 0, 4);
        t3d_tri_draw(6, 7, 8); t3d_tri_draw(6, 8, 9);
        t3d_tri_draw(6, 9, 10); t3d_tri_draw(6, 10, 7);
        t3d_tri_draw(11, 8, 7); t3d_tri_draw(11, 9, 8);
        t3d_tri_draw(11, 10, 9); t3d_tri_draw(11, 7, 10);
        t3d_tri_sync();
    }
}

void draw_units() {
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH));
    std::size_t visual_index = 0;
    for (const sim::UnitSnapshot& unit : board.game.snapshot().units) {
        if (!sim::on_board(unit)) continue;
        // The authored 84px close poses are composed in screen space because
        // they exceed one CI4 TMEM load. Keep board sprites resident beneath
        // the eight-frame stage crossfade.
        const VisualPose board_pose = close_combat_phase()
            ? VisualPose::idle : pose_for_unit(unit);
        sprite_t* sprite = sprite_for_unit(unit, board_pose);
        if (sprite == nullptr || visual_index >= board.unit_count) continue;
        // Keep the live 3D terrain/camera, but composite the 84px
        // Godot-derived close actors in screen space. A raw 84x84 CI4 upload
        // cannot fit beside its TLUT in TMEM; sprite_blit transparently bands
        // it without lowering the authored resolution.
        const bool hidden_for_close = close_overlay_alpha() == 255U;
        if (hidden_for_close) {
            ++visual_index;
            continue;
        }
        rdpq_sprite_upload(TILE0, sprite, nullptr);
        t3d_vert_load(board.units + visual_index * 2U, 0, 4);
        t3d_tri_draw(0, 1, 2);
        t3d_tri_draw(0, 2, 3);
        t3d_tri_sync();
        ++visual_index;
    }
}

void draw_cursor() {
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(
        static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH)
    );
    constexpr std::size_t vertices_per_batch = kBatchCells * 4U;
    for (std::size_t begin = 0U; begin < board.cursor_vertex_count;
         begin += vertices_per_batch) {
        const std::size_t count = std::min(
            vertices_per_batch, board.cursor_vertex_count - begin
        );
        t3d_vert_load(
            board.cursor + begin / 2U, 0,
            static_cast<std::uint32_t>(count)
        );
        for (std::uint8_t first = 0U; first < count; first += 4U) {
            t3d_tri_draw(first + 0U, first + 1U, first + 2U);
            t3d_tri_draw(first + 0U, first + 2U, first + 3U);
        }
        t3d_tri_sync();
    }
}

const sim::UnitSnapshot* find_unit(sim::UnitId id) {
    for (const sim::UnitSnapshot& unit : board.game.snapshot().units) {
        if (unit.id == id) return &unit;
    }
    return nullptr;
}

void plan_motion(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId unit_id,
    sim::Position destination
) {
    planned_motion = {};
    const sim::UnitSnapshot* actor = nullptr;
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == unit_id) actor = &unit;
    }
    if (actor == nullptr || !sim::on_board(*actor) ||
        actor->position == destination) {
        return;
    }
    const int width = snapshot.width;
    const int height = snapshot.height;
    assert(width * height <= static_cast<int>(kMaxCells));
    std::array<std::uint8_t, kMaxCells> crossable{};
    std::array<std::uint8_t, kMaxCells> distance{};
    for (const sim::Position tile : sim::reachable_tiles(snapshot, unit_id)) {
        crossable[static_cast<std::size_t>(tile.y * width + tile.x)] = 1U;
    }
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == unit_id || unit.side != actor->side || !sim::on_board(unit)) {
            continue;
        }
        crossable[static_cast<std::size_t>(unit.position.y * width + unit.position.x)] = 1U;
    }
    std::array<view::RouteTile, kMaxCells> storage{};
    view::Route route(storage.data(), static_cast<int>(storage.size()));
    const bool found = view::plan_route(
        actor->position.x, actor->position.y,
        destination.x, destination.y,
        width, height,
        [&crossable, width](int x, int y) {
            return crossable[static_cast<std::size_t>(y * width + x)] != 0U;
        },
        distance.data(), route
    );
    planned_motion.unit = unit_id;
    planned_motion.from = actor->position;
    planned_motion.to = destination;
    if (found) {
        planned_motion.route_count = static_cast<std::size_t>(route.size());
        for (int index = 0; index < route.size(); ++index) {
            planned_motion.route[static_cast<std::size_t>(index)] = {
                route[index].x, route[index].y,
            };
        }
    } else {
        planned_motion.route_count = 1U;
        planned_motion.route[0] = destination;
    }
    planned_motion.valid = true;
}

CameraPose close_camera_for(
    sim::Position attacker,
    sim::Position defender,
    const CameraPose& tactical
) {
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    constexpr float render_scale = 64.0F;
    const float attacker_x = world_x(attacker.x, width) / render_scale;
    const float attacker_z = world_z(attacker.y, height) / render_scale;
    const float defender_x = world_x(defender.x, width) / render_scale;
    const float defender_z = world_z(defender.y, height) / render_scale;
    const float midpoint_x = (attacker_x + defender_x) * 0.5F;
    const float midpoint_z = (attacker_z + defender_z) * 0.5F;
    const std::size_t attacker_index =
        static_cast<std::size_t>(attacker.y) * width + attacker.x;
    const std::size_t defender_index =
        static_cast<std::size_t>(defender.y) * width + defender.x;
    const float surface = static_cast<float>(std::max(
        terrain_lift(board.content->terrain[attacker_index]),
        terrain_lift(board.content->terrain[defender_index])
    )) / render_scale;
    // Keep the tactical yaw and pitch. The previous pair-perpendicular orbit
    // made fixed billboards turn edge-on and exposed a giant raw tile crop.
    // This is a target/ortho dolly only; the final eight frames hand off to a
    // screen-space terrain stage with stable actor sides.
    const float direction_x = tactical.eye_x - tactical.target_x;
    const float direction_y = tactical.eye_y - tactical.target_y;
    const float direction_z = tactical.eye_z - tactical.target_z;
    const float direction_length = std::max(
        0.01F,
        std::sqrt(
            direction_x * direction_x + direction_y * direction_y +
            direction_z * direction_z
        )
    );
    constexpr float distance = 4.85F;
    const float target_y = surface + 0.52F;
    return {
        midpoint_x + direction_x / direction_length * distance,
        target_y + direction_y / direction_length * distance,
        midpoint_z + direction_z / direction_length * distance,
        midpoint_x,
        target_y,
        midpoint_z,
        1.48F,
    };
}

void finish_combat(bool skipped) {
    apply_camera(combat.tactical_camera);
    assert(board.game.encounter().canonical_hash() == combat.authoritative_hash);
    combat.active = false;
    combat.skipped = skipped;
    combat.phase = CombatPhase::inactive;
    ++combat.exchanges;
    // Hold the exact restored board long enough to read as a return, not an
    // immediate jump into the next opposing command in the deterministic film.
    replay.next_frame = frame_count + 60U;
    // Give the restored tactical view several complete redraws before the
    // capture landmark. This keeps a checkpoint grab from photographing the
    // preceding return frame still held by the emulator window.
    restore_checkpoint_delay = 4U;
    if (battle_ui.phase == BattleUiPhase::result) {
        battle_ui.result_until = frame_count + 75U;
    }
}

void start_combat(
    const sim::UnitSnapshot& attacker_before,
    const sim::UnitSnapshot& defender_before
) {
    if (!combat.enabled) return;
    const sim::UnitSnapshot* attacker_after = find_unit(attacker_before.id);
    const sim::UnitSnapshot* defender_after = find_unit(defender_before.id);
    assert(attacker_after && defender_after);
    combat.active = true;
    combat.skipped = false;
    combat.phase = CombatPhase::entering;
    combat.phase_ticks = 0;
    combat.attacker = attacker_before.id;
    combat.defender = defender_before.id;
    combat.attacker_position = attacker_before.position;
    combat.defender_position = defender_before.position;
    combat.attacker_health_before = attacker_before.health;
    combat.attacker_health_after = attacker_after->health;
    combat.defender_health_before = defender_before.health;
    combat.defender_health_after = defender_after->health;
    combat.first_damage = 0;
    combat.counter_damage = 0;
    combat.first_missed = false;
    combat.counter_exists = false;
    combat.counter_missed = false;
    combat.close_checkpoint = false;
    combat.fly_checkpoint = false;
    combat.strike_checkpoint = false;
    combat.counter_checkpoint = false;
    combat.return_checkpoint = false;
    combat.restore_checkpoint = false;
    for (const sim::Event& event : board.game.transition().events) {
        const bool first = event.unit_id == defender_before.id &&
            event.related_unit_id == attacker_before.id;
        const bool answer = event.unit_id == attacker_before.id &&
            event.related_unit_id == defender_before.id;
        if (event.type == sim::EventType::unit_damaged) {
            if (first) combat.first_damage = event.amount;
            if (answer) {
                combat.counter_exists = true;
                combat.counter_damage = event.amount;
            }
        } else if (event.type == sim::EventType::attack_missed) {
            if (first) combat.first_missed = true;
            if (answer) {
                combat.counter_exists = true;
                combat.counter_missed = true;
            }
        }
    }
    combat.tactical_camera = current_camera;
    combat.close_camera = close_camera_for(
        attacker_before.position, defender_before.position,
        combat.tactical_camera
    );
    combat.authoritative_hash = board.game.encounter().canonical_hash();
    apply_camera(combat.tactical_camera);
    debugf(
        "GRANDLEON_PYRITE_COMBAT attacker=%lu defender=%lu damage=%d counter=%d "
        "counter_damage=%d hash=%llu\n",
        static_cast<unsigned long>(combat.attacker),
        static_cast<unsigned long>(combat.defender), combat.first_damage,
        combat.counter_exists ? 1 : 0, combat.counter_damage,
        combat.authoritative_hash
    );
}

std::uint32_t phase_duration(CombatPhase phase) {
    switch (phase) {
        case CombatPhase::entering: return combat.reduced_motion ? 8U : 66U;
        case CombatPhase::first_strike: return 60U;
        case CombatPhase::counter: return 60U;
        case CombatPhase::returning: return combat.reduced_motion ? 6U : 57U;
        case CombatPhase::inactive: return 0U;
    }
    return 0U;
}

void enter_combat_phase(CombatPhase phase) {
    combat.phase = phase;
    combat.phase_ticks = 0;
}

void update_combat() {
    if (!combat.active) return;
    assert(board.game.encounter().canonical_hash() == combat.authoritative_hash);
    const joypad_buttons_t held = joypad_get_buttons(JOYPAD_PORT_1);
    combat.phase_ticks += held.r ? 3U : 1U;
    const std::uint32_t duration = phase_duration(combat.phase);
    const float progress = duration == 0U
        ? 1.0F
        : static_cast<float>(std::min(combat.phase_ticks, duration)) /
            static_cast<float>(duration);
    switch (combat.phase) {
        case CombatPhase::entering:
            apply_camera(combat.reduced_motion
                ? combat.close_camera
                : interpolate_camera(
                    combat.tactical_camera, combat.close_camera, progress
                ));
            if (combat.phase_ticks >= duration / 2U && !combat.fly_checkpoint) {
                debugf("CHECKPOINT PYRITE_COMBAT_FLY\n");
                combat.fly_checkpoint = true;
            }
            if (combat.phase_ticks >= duration) {
                apply_camera(combat.close_camera);
                enter_combat_phase(CombatPhase::first_strike);
            }
            break;
        case CombatPhase::first_strike:
            apply_camera(combat.close_camera);
            if (combat.phase_ticks >= kCloseBlendFrames + 4U &&
                !combat.close_checkpoint) {
                debugf("CHECKPOINT PYRITE_COMBAT_CLOSE\n");
                combat.close_checkpoint = true;
            }
            if (combat.phase_ticks >= 32U && !combat.strike_checkpoint) {
                debugf("CHECKPOINT PYRITE_COMBAT_STRIKE\n");
                combat.strike_checkpoint = true;
            }
            if (combat.phase_ticks >= duration) {
                enter_combat_phase(combat.counter_exists
                    ? CombatPhase::counter : CombatPhase::returning);
            }
            break;
        case CombatPhase::counter:
            apply_camera(combat.close_camera);
            if (combat.phase_ticks >= 32U && !combat.counter_checkpoint) {
                debugf("CHECKPOINT PYRITE_COMBAT_COUNTER\n");
                combat.counter_checkpoint = true;
            }
            if (combat.phase_ticks >= duration) {
                enter_combat_phase(CombatPhase::returning);
            }
            break;
        case CombatPhase::returning:
            apply_camera(combat.reduced_motion
                ? combat.tactical_camera
                : interpolate_camera(
                    combat.close_camera, combat.tactical_camera, progress
                ));
            if (combat.phase_ticks >= duration / 2U &&
                !combat.return_checkpoint) {
                debugf("CHECKPOINT PYRITE_COMBAT_RETURN\n");
                combat.return_checkpoint = true;
            }
            if (combat.phase_ticks >= duration) finish_combat(false);
            break;
        case CombatPhase::inactive:
            break;
    }
}

void remember_motion() {
    const gp::GameplaySlice::Transition& transition = board.game.transition();
    if (!transition.accepted ||
        transition.command.type != sim::CommandType::move ||
        transition.from == transition.to) {
        return;
    }
    assert(planned_motion.valid &&
        planned_motion.unit == transition.command.unit_id &&
        planned_motion.from == transition.from &&
        planned_motion.to == transition.to &&
        planned_motion.route_count > 0U);
    if (motion.unit != transition.command.unit_id) motion.facing_left = false;
    motion.unit = transition.command.unit_id;
    motion.from = transition.from;
    motion.to = transition.to;
    motion.route = planned_motion.route;
    motion.route_count = planned_motion.route_count;
    motion.total_frames = static_cast<std::uint32_t>(
        view::slide_frames_for(static_cast<int>(motion.route_count))
    );
    motion.started = frame_count;
    motion.active = true;
    planned_motion.valid = false;
}

void remember_transition_events() {
    const gp::GameplaySlice::Transition& transition = board.game.transition();
    if (!transition.accepted) return;
    queue_event_moments(transition.events);
    board.battle_events.insert(
        board.battle_events.end(), transition.events.begin(),
        transition.events.end()
    );
}

bool issue(gp::GameplaySlice::Action action, std::uint8_t player = 0U) {
    if (action == gp::GameplaySlice::Action::confirm &&
        board.game.selected(player) != 0U) {
        plan_motion(
            board.game.snapshot(), board.game.selected(player),
            board.game.cursor(player)
        );
    }
    const bool handled = board.game.input(action, player);
    remember_transition_events();
    remember_motion();
    build_cursor_vertices();
    return handled;
}

const sim::UnitSnapshot* unit_at(sim::Position position) {
    for (const sim::UnitSnapshot& unit : board.game.snapshot().units) {
        if (sim::on_board(unit) && unit.position == position) return &unit;
    }
    return nullptr;
}

const sim::AbilityDefinition* ability_definition(sim::ContentId id) {
    for (const sim::AbilityDefinition& ability :
         board.game.encounter().abilities()) {
        if (ability.id == id) return &ability;
    }
    return nullptr;
}

bool actor_has_ability() {
    const sim::UnitSnapshot* actor = find_unit(battle_ui.actor);
    return actor != nullptr && !actor->ability_ids.empty();
}

std::uint8_t action_rows();

void open_action_menu(sim::UnitId actor, std::uint8_t player = 0U) {
    const sim::UnitSnapshot* unit = find_unit(actor);
    if (unit == nullptr || !sim::on_board(*unit) || unit->has_acted) return;
    battle_ui = {};
    battle_ui.phase = BattleUiPhase::action;
    battle_ui.actor = actor;
    battle_ui.player = player;
    if (!unit->ability_ids.empty()) battle_ui.ability = unit->ability_ids.front();
}

enum class ActionChoiceKind : std::uint8_t {
    attack,
    ability,
    item,
    talk,
    wait,
    cancel,
};

struct ActionChoice final {
    ActionChoiceKind kind{ActionChoiceKind::cancel};
    sim::ContentId content{};
};

struct ActionMenu final {
    std::array<ActionChoice, 32> choices{};
    std::size_t count{};

    void append(ActionChoiceKind kind, sim::ContentId content = 0U) {
        if (count < choices.size()) choices[count++] = {kind, content};
    }
};

ActionMenu action_menu() {
    ActionMenu menu;
    const sim::UnitSnapshot* actor = find_unit(battle_ui.actor);
    if (actor == nullptr) return menu;
    menu.append(ActionChoiceKind::attack);
    for (const sim::ContentId ability : actor->ability_ids) {
        menu.append(ActionChoiceKind::ability, ability);
    }
    for (std::size_t index = 0; index < actor->item_ids.size() &&
         index < actor->item_counts.size(); ++index) {
        if (actor->item_counts[index] == 0U) continue;
        const sim::ItemForecast forecast = sim::forecast_item(
            board.game.snapshot(), actor->id, actor->id,
            board.game.encounter().items(), actor->item_ids[index]
        );
        if (forecast &&
            (forecast.kind != sim::ItemKind::restore || forecast.restored > 0)) {
            menu.append(ActionChoiceKind::item, actor->item_ids[index]);
        }
    }
    for (const sim::UnitSnapshot& target : board.game.snapshot().units) {
        if (sim::forecast_talk(board.game.snapshot(), actor->id, target.id)) {
            menu.append(ActionChoiceKind::talk);
            break;
        }
    }
    menu.append(ActionChoiceKind::wait);
    menu.append(ActionChoiceKind::cancel);
    return menu;
}

std::uint8_t action_rows() {
    return static_cast<std::uint8_t>(action_menu().count);
}

ActionChoice action_choice() {
    const ActionMenu menu = action_menu();
    return battle_ui.row < menu.count
        ? menu.choices[battle_ui.row] : ActionChoice{};
}

void close_action_menu() {
    const std::uint8_t player = battle_ui.player;
    battle_ui = {};
    (void)issue(gp::GameplaySlice::Action::cancel, player);
}

bool apply_battle_command(
    const sim::Command& command,
    bool present_close = true
) {
    const sim::UnitSnapshot* actor = find_unit(command.unit_id);
    const sim::UnitSnapshot* target = command.target_id != 0U
        ? find_unit(command.target_id) : unit_at(command.destination);
    const sim::UnitSnapshot actor_before = actor != nullptr
        ? *actor : sim::UnitSnapshot{};
    const sim::UnitSnapshot target_before = target != nullptr
        ? *target : sim::UnitSnapshot{};
    const bool accepted = board.game.apply(command, battle_ui.player);
    battle_ui.phase = BattleUiPhase::result;
    battle_ui.result_command = command.type;
    battle_ui.result_error = board.game.transition().error;
    battle_ui.result_amount = 0;
    battle_ui.result_missed = false;
    battle_ui.result_restored = false;
    battle_ui.result_until = frame_count + 75U;
    if (!accepted) return false;
    if (command.type == sim::CommandType::attack ||
        command.type == sim::CommandType::ability) {
        play_impact_cue();
    }
    remember_transition_events();
    remember_motion();
    for (const sim::Event& event : board.game.transition().events) {
        const bool primary = target_before.id == 0U ||
            (event.unit_id == target_before.id &&
             event.related_unit_id == actor_before.id);
        if (!primary) continue;
        if (event.type == sim::EventType::attack_missed) {
            battle_ui.result_missed = true;
        } else if (event.type == sim::EventType::unit_damaged) {
            battle_ui.result_amount = std::max(
                battle_ui.result_amount, event.amount
            );
        } else if (event.type == sim::EventType::unit_restored) {
            battle_ui.result_amount = std::max(
                battle_ui.result_amount, event.amount
            );
            battle_ui.result_restored = true;
        }
    }
    bool close = command.type == sim::CommandType::attack;
    if (command.type == sim::CommandType::ability) {
        const sim::AbilityDefinition* ability =
            ability_definition(command.ability_id);
        close = ability != nullptr && ability->kind == sim::AbilityKind::damage &&
            target != nullptr &&
            (battle_ui.result_amount > 0 || battle_ui.result_missed);
    }
    if (command.type == sim::CommandType::attack &&
        !replay.action_result_checkpoint) {
        debugf(
            "CHECKPOINT PYRITE_ACTION_RESULT missed=%u damage=%d\n",
            battle_ui.result_missed ? 1U : 0U, battle_ui.result_amount
        );
        replay.action_result_checkpoint = true;
    }
    if (close && present_close && combat.enabled && actor_before.id != 0U &&
        target_before.id != 0U) {
        start_combat(actor_before, target_before);
    }
    build_cursor_vertices();
    return true;
}

bool confirm_action_choice() {
    const sim::UnitSnapshot* actor = find_unit(battle_ui.actor);
    if (actor == nullptr) {
        close_action_menu();
        return false;
    }
    const ActionChoice choice = action_choice();
    switch (choice.kind) {
        case ActionChoiceKind::attack:
            battle_ui.phase = BattleUiPhase::attack_target;
            return true;
        case ActionChoiceKind::ability:
            battle_ui.ability = choice.content;
            battle_ui.phase = BattleUiPhase::ability_target;
            return true;
        case ActionChoiceKind::item: {
            sim::Command command{};
            command.type = sim::CommandType::use_item;
            command.unit_id = actor->id;
            command.target_id = actor->id;
            command.item_id = choice.content;
            return apply_battle_command(command, false);
        }
        case ActionChoiceKind::talk:
            battle_ui.phase = BattleUiPhase::talk_target;
            return true;
        case ActionChoiceKind::wait:
            return apply_battle_command({
                sim::CommandType::wait, actor->id, {}, 0U, 0U, 0U, 0U,
            });
        case ActionChoiceKind::cancel:
            close_action_menu();
            return true;
    }
    return false;
}

bool confirm_target() {
    const sim::UnitSnapshot* actor = find_unit(battle_ui.actor);
    if (actor == nullptr) return false;
    if (battle_ui.phase == BattleUiPhase::attack_target) {
        const sim::UnitSnapshot* target = unit_at(
            board.game.cursor(battle_ui.player)
        );
        if (target == nullptr) return false;
        const sim::AttackForecast forecast = sim::forecast_attack(
            board.game.snapshot(), actor->id, target->id
        );
        if (!forecast) return false;
        return apply_battle_command({
            sim::CommandType::attack, actor->id, {}, target->id,
            0U, 0U, 0U,
        });
    }
    if (battle_ui.phase == BattleUiPhase::ability_target) {
        const sim::UnitSnapshot* target = unit_at(
            board.game.cursor(battle_ui.player)
        );
        const sim::AbilityForecast forecast = sim::forecast_ability(
            board.game.snapshot(), actor->id, battle_ui.ability,
            board.game.cursor(battle_ui.player),
            target != nullptr ? target->id : 0U,
            board.game.encounter().abilities()
        );
        if (!forecast) return false;
        return apply_battle_command({
            sim::CommandType::ability, actor->id,
            board.game.cursor(battle_ui.player), 0U,
            battle_ui.ability, 0U, 0U,
        });
    }
    if (battle_ui.phase == BattleUiPhase::talk_target) {
        const sim::UnitSnapshot* target = unit_at(
            board.game.cursor(battle_ui.player)
        );
        if (target == nullptr ||
            !sim::forecast_talk(board.game.snapshot(), actor->id, target->id)) {
            return false;
        }
        sim::Command command{};
        command.type = sim::CommandType::talk;
        command.unit_id = actor->id;
        command.target_id = target->id;
        return apply_battle_command(command, false);
    }
    return false;
}

bool nudge_toward(sim::Position destination) {
    const sim::Position cursor = board.game.cursor();
    if (cursor.x < destination.x) return issue(gp::GameplaySlice::Action::right);
    if (cursor.x > destination.x) return issue(gp::GameplaySlice::Action::left);
    if (cursor.y < destination.y) return issue(gp::GameplaySlice::Action::down);
    if (cursor.y > destination.y) return issue(gp::GameplaySlice::Action::up);
    return false;
}

std::uint8_t system_rows() {
    return game_languages().size() > 1U ? 7U : 6U;
}

void open_system_menu(std::uint8_t player) {
    battle_ui = {};
    battle_ui.phase = BattleUiPhase::system;
    battle_ui.player = player;
}

bool confirm_system_choice() {
    play_ui_cue();
    switch (battle_ui.row) {
        case 0U:
            battle_ui = {};
            return true;
        case 1U:
            combat.enabled = !combat.enabled;
            save_user_settings();
            return true;
        case 2U:
            combat.reduced_motion = !combat.reduced_motion;
            save_user_settings();
            return true;
        case 3U:
            audio_enabled = !audio_enabled;
            apply_audio_setting();
            save_user_settings();
            return true;
        case 4U:
            debug_hud = !debug_hud;
            save_user_settings();
            return true;
        case 5U:
            if (game_languages().size() > 1U) {
                (void)cycle_game_language();
            } else {
                battle_ui.phase = BattleUiPhase::help;
            }
            return true;
        case 6U:
            battle_ui.phase = BattleUiPhase::help;
            return true;
        default:
            return false;
    }
}

bool physical_input_for(
    std::uint8_t player,
    const joypad_buttons_t& pressed
) {
    if (board.game.snapshot().deploying) {
        if (player >= board.game.local_players()) return false;
        if (!pressed.d_left && !pressed.d_right && !pressed.d_up &&
            !pressed.d_down && !pressed.a && !pressed.b && !pressed.start) {
            return false;
        }
        replay.user_took_over = true;
        if (pressed.start) {
            const bool begun = board.game.apply({
                sim::CommandType::begin_battle, 0U, {}, 0U, 0U, 0U, 0U,
            }, player);
            if (!begun) return false;
            remember_transition_events();
            for (std::uint8_t seat = 0U; seat < board.game.local_players();
                 ++seat) {
                (void)board.game.input(gp::GameplaySlice::Action::cancel, seat);
            }
            build_cursor_vertices();
            queue_battle_moments(pr::MomentTrigger::stage_opens, 0U, true);
            (void)load_battle_moment_dialogue();
            debugf("CHECKPOINT PYRITE_DEPLOYMENT_END\n");
            return true;
        }
        if (pressed.d_left) return issue(gp::GameplaySlice::Action::left, player);
        if (pressed.d_right) return issue(gp::GameplaySlice::Action::right, player);
        if (pressed.d_up) return issue(gp::GameplaySlice::Action::up, player);
        if (pressed.d_down) return issue(gp::GameplaySlice::Action::down, player);
        if (pressed.b) return issue(gp::GameplaySlice::Action::cancel, player);
        const sim::Position cursor = board.game.cursor(player);
        const sim::UnitSnapshot* occupant = unit_at(cursor);
        if (occupant != nullptr && sim::is_deployable(
                board.game.snapshot(), *occupant
            ) && occupant->local_player == player + 1U) {
            if (board.game.selected(player) != 0U) {
                (void)issue(gp::GameplaySlice::Action::cancel, player);
            }
            const bool selected = issue(gp::GameplaySlice::Action::confirm, player);
            if (selected) {
                debugf(
                    "CHECKPOINT PYRITE_DEPLOYMENT_SELECT unit=%llu\n",
                    occupant->id
                );
            }
            return selected;
        }
        const sim::UnitId selected = board.game.selected(player);
        if (selected == 0U || occupant != nullptr) return false;
        const bool deployed = board.game.apply({
            sim::CommandType::deploy, selected, cursor, 0U, 0U, 0U, 0U,
        }, player);
        if (!deployed) return false;
        remember_transition_events();
        build_unit_vertices();
        build_cursor_vertices();
        debugf(
            "CHECKPOINT PYRITE_DEPLOYMENT_PLACE unit=%llu x=%d y=%d\n",
            selected, cursor.x, cursor.y
        );
        return true;
    }
    if (battle_moment.active) {
        if (pressed.a || pressed.start) {
            replay.user_took_over = true;
            return advance_battle_moment();
        }
        return false;
    }
    if (combat.active) {
        if (pressed.b) finish_combat(true);
        if (pressed.l) {
            combat.reduced_motion = !combat.reduced_motion;
            save_user_settings();
        }
        return pressed.b || pressed.l || pressed.r;
    }
    if (battle_ui.phase == BattleUiPhase::result) {
        if (player != battle_ui.player) return false;
        if (pressed.a || pressed.b) battle_ui = {};
        return pressed.a || pressed.b;
    }
    if (battle_ui.phase == BattleUiPhase::help) {
        if (player != battle_ui.player) return false;
        if (pressed.a || pressed.b || pressed.start) {
            battle_ui.phase = BattleUiPhase::system;
            return true;
        }
        return false;
    }
    if (battle_ui.phase == BattleUiPhase::system) {
        if (player != battle_ui.player) return false;
        if (!pressed.d_up && !pressed.d_down && !pressed.a && !pressed.b &&
            !pressed.start) {
            return false;
        }
        replay.user_took_over = true;
        if (pressed.d_up) {
            battle_ui.row = battle_ui.row == 0U
                ? static_cast<std::uint8_t>(system_rows() - 1U)
                : static_cast<std::uint8_t>(battle_ui.row - 1U);
        } else if (pressed.d_down) {
            battle_ui.row = static_cast<std::uint8_t>(
                (battle_ui.row + 1U) % system_rows()
            );
        } else if (pressed.b || pressed.start) {
            battle_ui = {};
        } else if (pressed.a) {
            return confirm_system_choice();
        }
        return true;
    }
    if (battle_ui.phase == BattleUiPhase::action) {
        if (player != battle_ui.player) return false;
        if (!pressed.d_up && !pressed.d_down && !pressed.a && !pressed.b) {
            return false;
        }
        replay.user_took_over = true;
        if (pressed.d_up) {
            battle_ui.row = battle_ui.row == 0U
                ? static_cast<std::uint8_t>(action_rows() - 1U)
                : static_cast<std::uint8_t>(battle_ui.row - 1U);
        } else if (pressed.d_down) {
            battle_ui.row = static_cast<std::uint8_t>(
                (battle_ui.row + 1U) % action_rows()
            );
        } else if (pressed.a) {
            return confirm_action_choice();
        } else if (pressed.b) {
            close_action_menu();
        }
        return pressed.d_up || pressed.d_down || pressed.a || pressed.b;
    }
    if (battle_ui.phase == BattleUiPhase::attack_target ||
        battle_ui.phase == BattleUiPhase::ability_target ||
        battle_ui.phase == BattleUiPhase::talk_target) {
        if (player != battle_ui.player) return false;
        if (!pressed.b && !pressed.a && !pressed.d_left && !pressed.d_right &&
            !pressed.d_up && !pressed.d_down) {
            return false;
        }
        replay.user_took_over = true;
        if (pressed.b) {
            battle_ui.phase = BattleUiPhase::action;
            return true;
        }
        if (pressed.a) return confirm_target();
        if (pressed.d_left) return issue(gp::GameplaySlice::Action::left, player);
        if (pressed.d_right) return issue(gp::GameplaySlice::Action::right, player);
        if (pressed.d_up) return issue(gp::GameplaySlice::Action::up, player);
        if (pressed.d_down) return issue(gp::GameplaySlice::Action::down, player);
        return false;
    }
    if (motion.active) return false;
    if (pressed.start) {
        replay.user_took_over = true;
        open_system_menu(player);
        return true;
    }
    if (pressed.c_up) {
        debug_hud = !debug_hud;
        save_user_settings();
        replay.user_took_over = true;
        return true;
    }
    if (pressed.l) {
        combat.reduced_motion = !combat.reduced_motion;
        save_user_settings();
        replay.user_took_over = true;
        return true;
    }
    if (!pressed.d_left && !pressed.d_right && !pressed.d_up &&
        !pressed.d_down && !pressed.a && !pressed.b && !pressed.z) {
        return false;
    }
    replay.user_took_over = true;
    if (pressed.d_left) return issue(gp::GameplaySlice::Action::left, player);
    if (pressed.d_right) return issue(gp::GameplaySlice::Action::right, player);
    if (pressed.d_up) return issue(gp::GameplaySlice::Action::up, player);
    if (pressed.d_down) return issue(gp::GameplaySlice::Action::down, player);
    if (pressed.b) return issue(gp::GameplaySlice::Action::cancel, player);
    if (pressed.z && board.game.selected(player) != 0U) {
        open_action_menu(board.game.selected(player), player);
        return true;
    }
    if (pressed.a) {
        const sim::UnitId selected = board.game.selected(player);
        if (selected == 0U) {
            return issue(gp::GameplaySlice::Action::confirm, player);
        }
        const sim::UnitSnapshot* actor = find_unit(selected);
        if (actor != nullptr && actor->position == board.game.cursor(player)) {
            open_action_menu(selected, player);
            return true;
        }
        const bool moved = issue(gp::GameplaySlice::Action::confirm, player);
        if (moved && board.game.transition().accepted &&
            board.game.transition().command.type == sim::CommandType::move) {
            open_action_menu(selected, player);
        }
        return moved;
    }
    return false;
}

bool physical_input() {
    constexpr joypad_port_t ports[2] = {JOYPAD_PORT_1, JOYPAD_PORT_2};
    bool handled = false;
    for (std::uint8_t player = 0U; player < board.game.local_players(); ++player) {
        const joypad_buttons_t pressed = joypad_get_buttons_pressed(ports[player]);
        handled = physical_input_for(player, pressed) || handled;
    }
    return handled;
}

tac::Behavior behavior_for(sim::UnitId unit) {
    for (const pr::UnitBehaviorBinding& binding : board.content->behaviors) {
        if (binding.unit_id == unit) return binding.behavior;
    }
    return tac::Behavior::hold;
}

const std::vector<sim::Position>& patrol_for(sim::UnitId unit) {
    for (const pr::UnitBehaviorBinding& binding : board.content->behaviors) {
        if (binding.unit_id == unit) return binding.patrol;
    }
    static const std::vector<sim::Position> empty;
    return empty;
}

bool advance_ai() {
    const sim::EncounterSnapshot& snapshot = board.game.snapshot();
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.side != snapshot.active_side || !sim::on_board(unit) ||
            unit.has_acted) {
            continue;
        }
        const bool friendly = unit.side == sim::Side::first;
        const tac::Behavior behavior = friendly
            ? tac::Behavior::pursue : behavior_for(unit.id);
        const tac::Plan plan = tac::decide(
            snapshot, unit.id, behavior, patrol_for(unit.id),
            board.game.encounter().abilities(),
            board.game.encounter().weapons()
        );
        sim::Command command = plan.actionable
            ? plan.command
            : sim::Command{sim::CommandType::wait, unit.id, {}, 0, 0, 0, 0};
        if (command.type == sim::CommandType::move) {
            plan_motion(snapshot, command.unit_id, command.destination);
        }
        bool accepted = false;
        if (command.type == sim::CommandType::attack) {
            const sim::AttackForecast forecast = sim::forecast_attack(
                snapshot, command.unit_id, command.target_id
            );
            assertf(static_cast<bool>(forecast), "Tactics emitted a refused attack");
            if (!replay.attack_forecast_checkpoint) {
                debugf(
                    "CHECKPOINT PYRITE_ACTION_FORECAST hit=%d damage=%d "
                    "counter=%d counter_damage=%d\n",
                    forecast.hit_chance, forecast.damage,
                    forecast.counter ? 1 : 0, forecast.counter_damage
                );
                replay.attack_forecast_checkpoint = true;
            }
            // The bounded review driver proves one complete optional handoff;
            // physical player commands keep the default and present every
            // attack while the setting is enabled.
            accepted = apply_battle_command(
                command, combat.exchanges == 0U
            );
        } else {
            accepted = board.game.apply(command);
            if (accepted) {
                remember_transition_events();
                remember_motion();
            }
        }
        if (!accepted) {
            command = {sim::CommandType::wait, unit.id, {}, 0, 0, 0, 0};
            assertf(board.game.apply(command), "Opponent fallback was refused");
            battle_ui = {};
            remember_transition_events();
            remember_motion();
        }
        ++replay.opponent_commands;
        if (!friendly) {
            ++shell.total_enemy_commands;
            if (shell.stage_index == 0U) ++shell.first_battle_enemy_commands;
        }
        if (!replay.enemy_checkpoint) {
            debugf("CHECKPOINT PYRITE_ENEMY\n");
            replay.enemy_checkpoint = true;
        }
        return true;
    }
    return false;
}

bool finish_battle_and_advance() {
    assert(board.campaign.has_value());
    const sim::EncounterSnapshot& snapshot = board.game.snapshot();
    if (snapshot.outcome == sim::Outcome::ongoing) {
        return false;
    }
    client::BattleReport report;
    report.outcome = snapshot.outcome;
    report.canonical_hash = board.game.encounter().canonical_hash();
    report.final_snapshot = snapshot;
    report.objectives = snapshot.objectives;
    report.events = board.battle_events;
    shell.aftermath_objectives = report.objectives;
    const auto committed = board.campaign->commit_battle(
        report, shell.aftermath
    );
    if (committed != client::CampaignSessionError::none) return false;
    if (board.campaign->save() != storage::StorageError::none) return false;
    ++shell.completed_battles;
    shell.total_commands += board.game.accepted_commands();
    shell.total_combats += combat.exchanges;
    if (shell.stage_index == 0U) {
        shell.first_battle_commands = board.game.accepted_commands();
        shell.first_battle_committed = true;
    }
    assert(board.saves->contains(campaign_slot()));
    shell.phase = ShellPhase::aftermath;
    shell.next_automatic_frame = frame_count + 90U;
    play_victory_cue();
    debugf(
        "CHECKPOINT PYRITE_AFTERMATH outcome=%u objectives=%lu "
        "fallen=%lu recruited=%lu stage=%lu commands=%lu enemy=%lu "
        "events=%lu slot_bytes=%lu\n",
        static_cast<unsigned>(shell.aftermath.outcome),
        static_cast<unsigned long>(shell.aftermath_objectives.size()),
        static_cast<unsigned long>(shell.aftermath.fallen.size()),
        static_cast<unsigned long>(shell.aftermath.recruited.size()),
        static_cast<unsigned long>(shell.stage_index + 1U),
        static_cast<unsigned long>(board.game.accepted_commands()),
        static_cast<unsigned long>(shell.total_enemy_commands),
        static_cast<unsigned long>(report.events.size()),
        static_cast<unsigned long>(storage::used_bytes(*board.saves))
    );
    return true;
}

void update_manual_battle() {
    if (!replay.user_took_over || combat.active || motion.active ||
        battle_moment.active || !battle_moment.dialogue_ids.empty() ||
        battle_ui.phase != BattleUiPhase::board ||
        frame_count < replay.next_frame) {
        return;
    }
    const sim::EncounterSnapshot& snapshot = board.game.snapshot();
    if (snapshot.outcome != sim::Outcome::ongoing) {
        (void)finish_battle_and_advance();
        return;
    }
    // Local players always own the first side. Once all of their units have
    // acted, the portable encounter advances to the opposing side and the
    // authored tactics policy takes over one ordinary command at a time.
    if (snapshot.active_side == sim::Side::second) {
        assertf(advance_ai(), "No opposing command was available");
        replay.next_frame = frame_count +
            (motion.active ? motion.total_frames + 2U : 12U);
    }
}

void begin_performance_sample() {
    performance_sample_started = true;
    performance_frames = 0;
    sample_start = 0;
    previous_frame = 0;
    min_frame_ticks = ~std::uint64_t{0};
    max_frame_ticks = 0;
}

void update_performance_sample() {
    if (!performance_sample_started || verdict_reported) return;
    const std::uint64_t now = get_ticks();
    if (previous_frame == 0U) {
        sample_start = now;
    } else {
        const std::uint64_t interval = now - previous_frame;
        min_frame_ticks = std::min(min_frame_ticks, interval);
        max_frame_ticks = std::max(max_frame_ticks, interval);
        ++performance_frames;
    }
    previous_frame = now;
    if (performance_frames != 120U) return;

    verdict_reported = true;
    heap_stats_t heap{};
    sys_get_heap_stats(&heap);
    const std::uint64_t elapsed = now - sample_start;
    assert(shell.phase == ShellPhase::complete);
    assert(shell.campaign_advanced_checkpoint);
    assert(shell.completed_battles == grandleon_project_encounter_count);
    assert(shell.stage_index + 1U == grandleon_project_encounter_count);
    assert(board.saves->contains(campaign_slot()));
    assert(!combat.active);
    if (!board.content->visual_visibility.hidden_cells.empty()) {
        assert(board.visibility_revealed[
            board.content->visual_visibility.hidden_cells.front()
        ]);
    }
    if (!shell.resumed) {
        assert(shell.total_commands > 0U && shell.total_enemy_commands > 0U);
        assert(shell.total_combats > 0U);
        if (grandleon_project_requires_scroll) {
            assert(replay.camera_x_checkpoint || replay.camera_y_checkpoint);
        }
    }
    assertf(
        get_memory_size() <= 4 * 1024 * 1024,
        "Pyrite campaign parity requires more than base N64 memory"
    );
    debugf(
        "GRANDLEON_PYRITE_GAMEPLAY frames=120 avg_frame_us=%llu "
        "min_frame_us=%llu max_frame_us=%llu fps_x100=%lu heap_used=%lu "
        "heap_total=%lu heap_free=%lu rdram=%d battles=%lu commands=%lu "
        "enemy_commands=%lu combat=%lu trees=%lu forms=%lu "
        "slot_bytes=%lu hash=%llu\n",
        TICKS_TO_US(elapsed) / 120U, TICKS_TO_US(min_frame_ticks),
        TICKS_TO_US(max_frame_ticks),
        static_cast<unsigned long>(P64::VI::SwapChain::getFPS() * 100.0F),
        static_cast<unsigned long>(heap.used),
        static_cast<unsigned long>(heap.total),
        static_cast<unsigned long>(heap.total - heap.used), get_memory_size(),
        static_cast<unsigned long>(shell.completed_battles),
        static_cast<unsigned long>(shell.total_commands),
        static_cast<unsigned long>(shell.total_enemy_commands),
        static_cast<unsigned long>(shell.total_combats),
        static_cast<unsigned long>(board.tree_count),
        static_cast<unsigned long>(board.terrain_form_count),
        static_cast<unsigned long>(storage::used_bytes(*board.saves)),
        board.game.encounter().canonical_hash()
    );
    debugf("RESULT PASS PYRITE_GAMEPLAY\n");
}

void update_replay() {
    if (!replay.board_checkpoint && frame_count >= 60U) {
        debugf("CHECKPOINT PYRITE_BOARD\n");
        replay.board_checkpoint = true;
    }
    if (motion.active && !replay.motion_checkpoint &&
        frame_count >= motion.started + 8U) {
        debugf("CHECKPOINT PYRITE_MOTION\n");
        replay.motion_checkpoint = true;
    }
    if (combat.active || replay.user_took_over || replay.phase == ReplayPhase::complete ||
        frame_count < replay.next_frame) {
        return;
    }
    // The capture driver must look at the revealed room, rather than leave
    // its cursor at a previous fight. Use the same cursor navigation as play
    // and wait for camera follow to settle; no visibility state is changed.
    const auto& covered = board.content->visual_visibility.hidden_cells;
    if (!covered.empty() && board.visibility_revealed[covered.front()] &&
        !replay.castle_review_checkpoint) {
        const int width = board.content->definition.width;
        const auto cell = covered[covered.size() / 2U];
        const sim::Position focus{static_cast<std::int16_t>(cell % width),
                                  static_cast<std::int16_t>(cell / width)};
        if (!replay.castle_review_aimed) {
            if (board.game.cursor() == focus) {
                replay.castle_review_aimed = true;
                replay.next_frame = frame_count + 90U;
            } else {
                nudge_toward(focus);
                replay.next_frame = frame_count + 3U;
            }
        } else {
            replay.castle_review_checkpoint = true;
            debugf("CHECKPOINT PYRITE_CASTLE_ROOM_REVIEW\n");
            replay.next_frame = frame_count + 90U;
        }
        return;
    }
    switch (replay.phase) {
        case ReplayPhase::explore_northwest:
            if (board.game.cursor() == sim::Position{0, 0}) {
                replay.phase = ReplayPhase::explore_southeast;
                debugf("CHECKPOINT PYRITE_CAMERA_NORTHWEST\n");
                replay.next_frame = frame_count + 30U;
            } else {
                nudge_toward({0, 0});
                replay.next_frame = frame_count + 3U;
            }
            break;
        case ReplayPhase::explore_southeast: {
            const sim::Position edge{
                static_cast<std::int16_t>(board.content->definition.width - 1),
                static_cast<std::int16_t>(board.content->definition.height - 1),
            };
            if (board.game.cursor() == edge) {
                replay.phase = ReplayPhase::return_to_rowan;
                debugf("CHECKPOINT PYRITE_CAMERA_SOUTHEAST\n");
                replay.next_frame = frame_count + 30U;
            } else {
                nudge_toward(edge);
                replay.next_frame = frame_count + 3U;
            }
            break;
        }
        case ReplayPhase::return_to_rowan:
            if (board.game.cursor() == replay.rowan_start) {
                if (shell.stage_index == 0U) {
                    replay.phase = ReplayPhase::select_rowan;
                } else {
                    replay.phase = ReplayPhase::autoplay_battle;
                    debugf(
                        "CHECKPOINT PYRITE_STAGE_SETTLED index=%lu\n",
                        static_cast<unsigned long>(shell.stage_index + 1U)
                    );
                }
            } else {
                nudge_toward(replay.rowan_start);
            }
            replay.next_frame = frame_count + 3U;
            break;
        case ReplayPhase::select_rowan:
            assertf(issue(gp::GameplaySlice::Action::confirm), "Primary unit select failed");
            assert(board.game.selected() == board.rowan_id);
            if (!replay.selected_checkpoint) {
                debugf("CHECKPOINT PYRITE_SELECTED\n");
                replay.selected_checkpoint = true;
            }
            replay.phase = ReplayPhase::aim_rowan;
            replay.next_frame = frame_count + 6U;
            break;
        case ReplayPhase::aim_rowan:
            if (board.game.cursor() == replay.rowan_destination) {
                replay.phase = ReplayPhase::move_rowan;
            } else {
                nudge_toward(replay.rowan_destination);
            }
            replay.next_frame = frame_count + 4U;
            break;
        case ReplayPhase::move_rowan:
            assertf(issue(gp::GameplaySlice::Action::confirm), "Primary unit move failed");
            assert(board.game.transition().accepted);
            if (!replay.moved_checkpoint) {
                debugf("CHECKPOINT PYRITE_MOVED\n");
                replay.moved_checkpoint = true;
            }
            replay.phase = ReplayPhase::settle_rowan;
            replay.next_frame = frame_count + motion.total_frames;
            break;
        case ReplayPhase::settle_rowan:
            replay.phase = ReplayPhase::select_rowan_again;
            replay.next_frame = frame_count + 2U;
            break;
        case ReplayPhase::select_rowan_again:
            assertf(issue(gp::GameplaySlice::Action::confirm), "Moved primary unit select failed");
            open_action_menu(board.rowan_id);
            assert(battle_ui.phase == BattleUiPhase::action);
            if (!replay.action_menu_checkpoint) {
                debugf(
                    "CHECKPOINT PYRITE_ACTION_MENU rows=%u attack=1 ability=%u "
                    "wait=1 cancel=1\n",
                    static_cast<unsigned>(action_rows()),
                    actor_has_ability() ? 1U : 0U
                );
                replay.action_menu_checkpoint = true;
            }
            replay.phase = ReplayPhase::wait_rowan;
            replay.next_frame = frame_count + 6U;
            break;
        case ReplayPhase::wait_rowan:
            assertf(
                apply_battle_command({
                    sim::CommandType::wait, board.rowan_id, {},
                    0U, 0U, 0U, 0U,
                }),
                "Primary unit wait failed"
            );
            replay.phase = board.tomas_id != 0U
                ? ReplayPhase::aim_tomas : ReplayPhase::opponent;
            replay.next_frame = frame_count + 6U;
            break;
        case ReplayPhase::aim_tomas: {
            const sim::UnitSnapshot* tomas = find_unit(board.tomas_id);
            assert(tomas && sim::on_board(*tomas));
            if (board.game.cursor() == tomas->position) {
                replay.phase = ReplayPhase::select_tomas;
            } else {
                nudge_toward(tomas->position);
            }
            replay.next_frame = frame_count + 4U;
            break;
        }
        case ReplayPhase::select_tomas:
            assertf(issue(gp::GameplaySlice::Action::confirm), "Ability unit select failed");
            assert(board.game.selected() == board.tomas_id);
            open_action_menu(board.tomas_id);
            assert(battle_ui.phase == BattleUiPhase::action);
            if (!replay.ability_forecast_checkpoint) {
                const sim::UnitSnapshot* tomas = find_unit(board.tomas_id);
                assert(tomas && !tomas->ability_ids.empty());
                sim::AbilityForecast forecast;
                for (const sim::Position offset : std::array<sim::Position, 4>{
                         sim::Position{1, 0}, sim::Position{-1, 0},
                         sim::Position{0, 1}, sim::Position{0, -1},
                     }) {
                    const sim::Position destination{
                        static_cast<std::int16_t>(tomas->position.x + offset.x),
                        static_cast<std::int16_t>(tomas->position.y + offset.y),
                    };
                    const sim::UnitSnapshot* target = unit_at(destination);
                    forecast = sim::forecast_ability(
                        board.game.snapshot(), tomas->id,
                        tomas->ability_ids.front(), destination,
                        target != nullptr ? target->id : 0U,
                        board.game.encounter().abilities()
                    );
                    if (forecast) break;
                }
                if (forecast) {
                    debugf(
                        "CHECKPOINT PYRITE_ABILITY_FORECAST rows=%u kind=%u "
                        "covered=%u restored=%d\n",
                        static_cast<unsigned>(action_rows()),
                        static_cast<unsigned>(forecast.kind),
                        forecast.covered ? 1U : 0U, forecast.restored
                    );
                }
                replay.ability_forecast_checkpoint = true;
            }
            replay.phase = ReplayPhase::wait_tomas;
            replay.next_frame = frame_count + 6U;
            break;
        case ReplayPhase::wait_tomas:
            assertf(
                apply_battle_command({
                    sim::CommandType::wait, board.tomas_id, {},
                    0U, 0U, 0U, 0U,
                }),
                "Ability unit wait failed"
            );
            replay.phase = ReplayPhase::opponent;
            replay.next_frame = frame_count + 12U;
            break;
        case ReplayPhase::opponent:
            if (board.game.snapshot().active_side == sim::Side::first &&
                replay.opponent_commands > 0U) {
                replay.phase = ReplayPhase::autoplay_battle;
                if (!replay.round_checkpoint) {
                    debugf("CHECKPOINT PYRITE_ROUND\n");
                    replay.round_checkpoint = true;
                }
                replay.next_frame = frame_count + 12U;
            } else {
                assertf(advance_ai(), "No opposing command was available");
                replay.next_frame = frame_count +
                    (motion.active ? motion.total_frames + 2U : 12U);
            }
            break;
        case ReplayPhase::autoplay_battle:
            if (board.game.snapshot().outcome != sim::Outcome::ongoing) {
                assertf(
                    finish_battle_and_advance(),
                    "Authored campaign result did not advance"
                );
            } else {
                assertf(advance_ai(), "No campaign autoplay command was available");
                replay.next_frame = frame_count +
                    (motion.active ? motion.total_frames + 2U : 8U);
            }
            break;
        case ReplayPhase::complete:
            break;
    }
}

void draw_combat_stage_2d() {
    const std::uint8_t alpha = close_overlay_alpha();
    if (alpha == 0U) return;
    const sim::UnitSnapshot* left = find_unit(combat.attacker);
    const sim::UnitSnapshot* right = find_unit(combat.defender);
    assert(left && right);
    const VisualPose left_pose = close_combat_phase()
        ? pose_for_unit(*left) : VisualPose::close_idle;
    const VisualPose right_pose = close_combat_phase()
        ? pose_for_unit(*right) : VisualPose::close_idle;
    sprite_t* left_sprite = sprite_for_unit(*left, left_pose);
    sprite_t* right_sprite = sprite_for_unit(*right, right_pose);
    assert(left_sprite && right_sprite);

    int left_x = 44;
    int right_x = 192;
    const int lunge = std::max(lunge_percent(*left), lunge_percent(*right)) / 2;
    if (combat.phase == CombatPhase::first_strike) left_x += lunge;
    if (combat.phase == CombatPhase::counter) right_x -= lunge;

    rdpq_mode_push();
    rdpq_mode_begin();
        rdpq_mode_zbuf(false, false);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    rdpq_set_prim_color({255, 255, 255, alpha});
    rdpq_blitparms_t backdrop_parms{};
    backdrop_parms.filtering = true;
    rdpq_sprite_blit(board.combat_backdrop, 0.0F, 0.0F, &backdrop_parms);

    // Soft screen-space contact shadows anchor both cutouts to one baseline.
    // They deliberately reuse the tiny I8 radial sprite used on the board.
    rdpq_mode_begin();
        rdpq_mode_zbuf(false, false);
        rdpq_mode_tlut(TLUT_NONE);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(
            RDPQ_COMBINER1((0, 0, 0, PRIM), (TEX0, 0, PRIM, 0))
        );
        rdpq_mode_alphacompare(8);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    rdpq_set_prim_color({0, 0, 0, static_cast<std::uint8_t>(alpha * 88U / 255U)});
    rdpq_blitparms_t shadow_parms{};
    shadow_parms.scale_x = 2.0F;
    shadow_parms.scale_y = 0.42F;
    shadow_parms.filtering = true;
    rdpq_sprite_blit(
        board.combat_shadow, static_cast<float>(left_x + 10), 207.0F,
        &shadow_parms
    );
    rdpq_sprite_blit(
        board.combat_shadow, static_cast<float>(right_x + 10), 207.0F,
        &shadow_parms
    );

    rdpq_mode_begin();
        rdpq_mode_zbuf(false, false);
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    rdpq_set_prim_color({255, 255, 255, alpha});
    rdpq_blitparms_t left_parms{};
    left_parms.filtering = true;
    rdpq_sprite_blit(left_sprite, static_cast<float>(left_x), 136.0F, &left_parms);
    rdpq_blitparms_t right_parms{};
    right_parms.flip_x = true;
    right_parms.filtering = true;
    rdpq_sprite_blit(right_sprite, static_cast<float>(right_x), 136.0F, &right_parms);

    // A short impact glint supplements the authored slash/hurt frames without
    // obscuring either silhouette.
    const bool impact = close_combat_phase() &&
        combat.phase_ticks >= 30U && combat.phase_ticks < 38U;
    if (impact) {
        const bool counter = combat.phase == CombatPhase::counter;
        const int impact_x = counter ? left_x + 70 : right_x + 14;
        constexpr int impact_y = 158;
        rdpq_mode_begin();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_fog(0);
        rdpq_mode_end();
        rdpq_set_prim_color({255, 218, 104, alpha});
        rdpq_fill_rectangle(impact_x - 8, impact_y - 2, impact_x + 9, impact_y + 3);
        rdpq_fill_rectangle(impact_x - 2, impact_y - 8, impact_x + 3, impact_y + 9);
    }

    // Compact framed HP widgets live inside the close stage; the development
    // proof/FPS overlay remains independently off by default.
    if (alpha >= 224U) {
        std::int16_t left_health = combat.attacker_health_before;
        std::int16_t right_health = combat.defender_health_before;
        const char* result = nullptr;
        int amount = 0;
        if (combat.phase == CombatPhase::first_strike) {
            if (combat.phase_ticks >= 30U) {
                right_health = combat.defender_health_after;
                result = ui(
                    combat.first_missed
                        ? console::Message::miss : console::Message::hit
                );
                amount = combat.first_damage;
            }
        } else if (combat.phase == CombatPhase::counter) {
            right_health = combat.defender_health_after;
            if (combat.phase_ticks >= 30U) {
                left_health = combat.attacker_health_after;
                result = ui(
                    combat.counter_missed
                        ? console::Message::miss : console::Message::counter
                );
                amount = combat.counter_damage;
            }
        } else if (combat.phase == CombatPhase::returning) {
            left_health = combat.attacker_health_after;
            right_health = combat.defender_health_after;
        }
        rdpq_mode_begin();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_fog(0);
        rdpq_mode_end();
        const color_t left_team = left->side == sim::Side::first
            ? color_t{47, 116, 190, 235} : color_t{166, 66, 55, 235};
        const color_t right_team = right->side == sim::Side::first
            ? color_t{47, 116, 190, 235} : color_t{166, 66, 55, 235};
        rdpq_set_prim_color(left_team);
        rdpq_fill_rectangle(7, 7, 125, 31);
        rdpq_set_prim_color(right_team);
        rdpq_fill_rectangle(195, 7, 313, 31);
        rdpq_set_prim_color({22, 25, 30, 235});
        rdpq_fill_rectangle(10, 10, 122, 28);
        rdpq_fill_rectangle(198, 10, 310, 28);
        const int left_bar = std::clamp(
            96 * static_cast<int>(left_health) /
                std::max(1, static_cast<int>(left->maximum_health)), 0, 96
        );
        const int right_bar = std::clamp(
            96 * static_cast<int>(right_health) /
                std::max(1, static_cast<int>(right->maximum_health)), 0, 96
        );
        rdpq_set_prim_color({80, 191, 111, 255});
        rdpq_fill_rectangle(14, 23, 14 + left_bar, 27);
        rdpq_fill_rectangle(202, 23, 202 + right_bar, 27);
        if (result != nullptr) {
            rdpq_set_prim_color({22, 25, 30, 235});
            rdpq_fill_rectangle(116, 36, 204, 54);
        }
        P64::Debug::printStart();
        P64::Debug::printf(14, 11, "ATK %d/%d", left_health, left->maximum_health);
        P64::Debug::printf(202, 11, "DEF %d/%d", right_health, right->maximum_health);
        if (result != nullptr) {
            if (amount > 0) P64::Debug::printf(130, 40, "%s -%d", result, amount);
            else P64::Debug::printf(142, 40, "%s", result);
        }
    }
    rdpq_mode_pop();
}

const char* command_error_label(sim::CommandError error) {
    switch (error) {
        case sim::CommandError::none:
            return ui(console::Message::ready);
        case sim::CommandError::target_out_of_range:
            return ui(console::Message::out_of_range);
        case sim::CommandError::friendly_target:
            return ui(console::Message::choose_enemy);
        case sim::CommandError::unknown_target:
            return ui(console::Message::choose_target);
        case sim::CommandError::target_defeated:
            return ui(console::Message::target_down);
        case sim::CommandError::unavailable_ability:
            return ui(console::Message::ability_spent);
        case sim::CommandError::activation_in_progress:
            return ui(console::Message::finish_turn);
        default:
            return ui(console::Message::action_refused);
    }
}

std::vector<client::RosterEntry> localized_campaign_roster();

void draw_battle_ui() {
    if (battle_moment.active &&
        battle_moment.line_index < battle_moment.dialogue.lines.size()) {
        const pr::DialogueLine& line =
            battle_moment.dialogue.lines[battle_moment.line_index];
        const std::string_view localized_speaker = pr::localized_dialogue_line(
            board.runtime.package(), game_language(),
            battle_moment.dialogue.id, battle_moment.line_index,
            "speaker", line.speaker
        );
        const std::string_view localized_text = pr::localized_dialogue_line(
            board.runtime.package(), game_language(),
            battle_moment.dialogue.id, battle_moment.line_index,
            "text", line.text
        );
        const std::vector<client::RosterEntry> roster =
            localized_campaign_roster();
        const std::string speaker = client::substitute_campaign_member_names(
            localized_speaker, roster
        );
        const std::string dialogue_text =
            client::substitute_campaign_member_names(localized_text, roster);
        rdpq_mode_push();
        rdpq_mode_begin();
            rdpq_mode_zbuf(false, false);
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_fog(0);
        rdpq_mode_end();
        rdpq_set_prim_color({45, 73, 92, 245});
        rdpq_fill_rectangle(15, 157, 305, 235);
        rdpq_set_prim_color({12, 22, 33, 248});
        rdpq_fill_rectangle(19, 161, 301, 231);
        P64::Debug::printStart();
        P64::Debug::printf(
            29, 169, "%.*s",
            static_cast<int>(std::min<std::size_t>(speaker.size(), 30U)),
            speaker.data()
        );
        constexpr std::size_t width = 42U;
        std::size_t start = 0U;
        for (int row = 0; row < 2 && start < dialogue_text.size(); ++row) {
            std::size_t end = std::min(start + width, dialogue_text.size());
            if (end < dialogue_text.size()) {
                const std::size_t space = dialogue_text.rfind(' ', end);
                if (space != std::string_view::npos && space > start) end = space;
            }
            P64::Debug::printf(
                29, 189 + row * 17, "%.*s",
                static_cast<int>(end - start), dialogue_text.data() + start
            );
            start = end;
            while (start < dialogue_text.size() && dialogue_text[start] == ' ') ++start;
        }
        P64::Debug::printf(238, 215, "A %s", ui(console::Message::next));
        rdpq_mode_pop();
        return;
    }
    if (board.game.snapshot().deploying) {
        rdpq_mode_push();
        rdpq_mode_begin();
            rdpq_mode_zbuf(false, false);
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_fog(0);
        rdpq_mode_end();
        rdpq_set_prim_color({45, 73, 92, 245});
        rdpq_fill_rectangle(65, 12, 255, 63);
        rdpq_set_prim_color({12, 22, 33, 248});
        rdpq_fill_rectangle(69, 16, 251, 59);
        P64::Debug::printStart();
        P64::Debug::printf(
            80, 22, "%s", ui(console::Message::deployment)
        );
        P64::Debug::printf(
            80, 42, "A %s   START %s",
            board.game.selected(board.game.active_player()) == 0U
                ? ui(console::Message::select)
                : ui(console::Message::deploy),
            ui(console::Message::begin)
        );
        rdpq_mode_pop();
        return;
    }
    if (battle_ui.phase == BattleUiPhase::board || combat.active) return;
    if (battle_ui.phase == BattleUiPhase::action && motion.active) return;
    rdpq_mode_push();
    rdpq_mode_begin();
        rdpq_mode_zbuf(false, false);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    if (battle_ui.phase == BattleUiPhase::system ||
        battle_ui.phase == BattleUiPhase::help) {
        rdpq_set_prim_color({45, 73, 92, 245});
        rdpq_fill_rectangle(45, 32, 275, 226);
        rdpq_set_prim_color({12, 22, 33, 248});
        rdpq_fill_rectangle(49, 36, 271, 222);
        P64::Debug::printStart();
        if (battle_ui.phase == BattleUiPhase::help) {
            P64::Debug::printf(132, 48, "%s", ui(console::Message::help));
            P64::Debug::printf(63, 76, "%s", ui(console::Message::move_cursor));
            P64::Debug::printf(63, 98, "%s", ui(console::Message::select_confirm));
            P64::Debug::printf(
                63, 120, "Z      %s", ui(console::Message::character_actions)
            );
            P64::Debug::printf(63, 142, "START  %s", ui(console::Message::settings));
            P64::Debug::printf(63, 164, "%s", ui(console::Message::back));
            P64::Debug::printf(
                63, 186, "%s", ui(console::Message::close_view_help)
            );
            P64::Debug::printf(91, 208, "A / B  %s", ui(console::Message::settings));
        } else {
            const bool languages = game_languages().size() > 1U;
            const auto y = [](unsigned row) {
                return 64 + static_cast<int>(row) * 20;
            };
            P64::Debug::printf(94, 44, "%s P%u",
                ui(console::Message::settings),
                static_cast<unsigned>(battle_ui.player + 1U));
            P64::Debug::printf(69, y(0), "%s %s",
                battle_ui.row == 0U ? ">" : " ",
                ui(console::Message::resume));
            P64::Debug::printf(69, y(1), "%s %s: %s",
                battle_ui.row == 1U ? ">" : " ",
                ui(console::Message::close_view),
                ui(combat.enabled ? console::Message::on : console::Message::off));
            P64::Debug::printf(69, y(2), "%s %s: %s",
                battle_ui.row == 2U ? ">" : " ",
                ui(console::Message::reduced_motion),
                ui(combat.reduced_motion ? console::Message::on : console::Message::off));
            P64::Debug::printf(69, y(3), "%s %s: %s",
                battle_ui.row == 3U ? ">" : " ",
                ui(console::Message::audio),
                ui(audio_enabled ? console::Message::on : console::Message::off));
            P64::Debug::printf(69, y(4), "%s %s",
                battle_ui.row == 4U ? ">" : " ",
                ui(debug_hud ? console::Message::show_fps_on
                             : console::Message::show_fps_off));
            if (languages) {
                P64::Debug::printf(69, y(5), "%s %s: %s",
                    battle_ui.row == 5U ? ">" : " ",
                    ui(console::Message::language),
                    console::language_code(game_language()));
            }
            const unsigned help_row = languages ? 6U : 5U;
            P64::Debug::printf(69, y(help_row), "%s %s",
                battle_ui.row == help_row ? ">" : " ",
                ui(console::Message::help));
            P64::Debug::printf(76, 210, "A %s   B %s",
                ui(console::Message::confirm), ui(console::Message::resume));
        }
        rdpq_mode_pop();
        return;
    }
    rdpq_set_prim_color({45, 73, 92, 242});
    rdpq_fill_rectangle(176, 130, 315, 235);
    rdpq_set_prim_color({12, 22, 33, 246});
    rdpq_fill_rectangle(180, 134, 311, 231);
    P64::Debug::printStart();
    if (battle_ui.phase == BattleUiPhase::action) {
        P64::Debug::printf(
            190, 140, "%s P%u", ui(console::Message::action_heading),
            static_cast<unsigned>(battle_ui.player + 1U)
        );
        const sim::UnitSnapshot* menu_actor = find_unit(battle_ui.actor);
        const std::size_t total = action_rows();
        const std::size_t selected = battle_ui.row;
        const std::size_t first = selected >= 4U ? selected - 3U : 0U;
        const std::size_t last = std::min(first + 4U, total);
        const ActionMenu menu = action_menu();
        for (std::size_t row = first; row < last; ++row) {
            const int y = 158 + static_cast<int>(row - first) * 17;
            const char* marker = selected == row ? ">" : " ";
            const ActionChoice choice = menu.choices[row];
            if (choice.kind == ActionChoiceKind::attack) {
                P64::Debug::printf(
                    190, y, "%s %s", marker, ui(console::Message::attack)
                );
            } else if (choice.kind == ActionChoiceKind::ability &&
                       menu_actor != nullptr) {
                const std::string_view name = pr::content_name(
                    board.runtime.package(),
                    package_format::SectionType::abilities, choice.content
                );
                const std::string_view localized_name =
                    pr::localized_content_name(
                        board.runtime.package(), game_language(),
                        package_format::SectionType::abilities, choice.content,
                        name
                    );
                P64::Debug::printf(
                    190, y, "%s %.*s", marker,
                    static_cast<int>(std::min<std::size_t>(
                        localized_name.size(), 14U
                    )), localized_name.data()
                );
            } else if (choice.kind == ActionChoiceKind::item) {
                const std::string_view name = pr::content_name(
                    board.runtime.package(),
                    package_format::SectionType::items, choice.content
                );
                const std::string_view localized_name =
                    pr::localized_content_name(
                        board.runtime.package(), game_language(),
                        package_format::SectionType::items, choice.content, name
                    );
                P64::Debug::printf(
                    190, y, "%s %.*s", marker,
                    static_cast<int>(std::min<std::size_t>(
                        localized_name.size(), 14U
                    )), localized_name.data()
                );
            } else if (choice.kind == ActionChoiceKind::talk) {
                P64::Debug::printf(
                    190, y, "%s %s", marker, ui(console::Message::talk)
                );
            } else if (choice.kind == ActionChoiceKind::wait) {
                P64::Debug::printf(
                    190, y, "%s %s", marker, ui(console::Message::wait)
                );
            } else {
                P64::Debug::printf(
                    190, y, "%s %s", marker, ui(console::Message::cancel)
                );
            }
        }
        rdpq_mode_pop();
        return;
    }
    const sim::UnitSnapshot* actor = find_unit(battle_ui.actor);
    const sim::UnitSnapshot* target = unit_at(
        board.game.cursor(battle_ui.player)
    );
    if (battle_ui.phase == BattleUiPhase::attack_target && actor != nullptr) {
        const sim::AttackForecast forecast = target != nullptr
            ? sim::forecast_attack(board.game.snapshot(), actor->id, target->id)
            : sim::AttackForecast{sim::CommandError::unknown_target};
        P64::Debug::printf(
            190, 140, "%s", ui(console::Message::attack_target)
        );
        if (forecast) {
            P64::Debug::printf(
                190, 164, "%d%%  %s %d", forecast.hit_chance,
                ui(console::Message::hit),
                forecast.damage
            );
            if (forecast.counter) {
                P64::Debug::printf(
                    190, 182, "%s %d", ui(console::Message::counter),
                    forecast.counter_damage
                );
            } else {
                P64::Debug::printf(
                    190, 182, "%s", ui(console::Message::no_counter)
                );
            }
            P64::Debug::printf(
                190, 208, "A %s  B %s", ui(console::Message::confirm),
                ui(console::Message::back_button)
            );
        } else {
            P64::Debug::printf(
                190, 170, "%s", command_error_label(forecast.error)
            );
        }
    } else if (battle_ui.phase == BattleUiPhase::ability_target &&
               actor != nullptr) {
        const sim::AbilityForecast forecast = sim::forecast_ability(
            board.game.snapshot(), actor->id, battle_ui.ability,
            board.game.cursor(battle_ui.player),
            target != nullptr ? target->id : 0U,
            board.game.encounter().abilities()
        );
        P64::Debug::printf(
            190, 140, "%s", ui(console::Message::ability_target)
        );
        if (forecast) {
            if (!forecast.covered) {
                P64::Debug::printf(
                    190, 170, "%s", ui(console::Message::clear_ground)
                );
            } else if (forecast.spared) {
                P64::Debug::printf(
                    190, 170, "%s", ui(console::Message::target_safe)
                );
            } else if (forecast.kind == sim::AbilityKind::restore) {
                P64::Debug::printf(
                    190, 170, "%s %d/%d", ui(console::Message::mend),
                    forecast.restored,
                    forecast.target_health_after
                );
            } else {
                P64::Debug::printf(
                    190, 170, "%d%%  %s %d", forecast.hit_chance,
                    ui(console::Message::cast),
                    forecast.damage
                );
            }
            P64::Debug::printf(
                190, 208, "A %s  B %s", ui(console::Message::confirm),
                ui(console::Message::back_button)
            );
        } else {
            P64::Debug::printf(
                190, 170, "%s", command_error_label(forecast.error)
            );
        }
    } else if (battle_ui.phase == BattleUiPhase::talk_target &&
               actor != nullptr) {
        sim::TalkForecast forecast;
        if (target != nullptr) {
            forecast = sim::forecast_talk(
                board.game.snapshot(), actor->id, target->id
            );
        } else {
            forecast.error = sim::CommandError::unknown_target;
        }
        P64::Debug::printf(
            190, 140, "%s", ui(console::Message::talk_to_whom)
        );
        if (forecast) {
            P64::Debug::printf(
                190, 170, "%s", ui(console::Message::confirm)
            );
            P64::Debug::printf(
                190, 208, "A %s  B %s", ui(console::Message::confirm),
                ui(console::Message::back_button)
            );
        } else {
            P64::Debug::printf(
                190, 170, "%s", command_error_label(forecast.error)
            );
        }
    } else if (battle_ui.phase == BattleUiPhase::result) {
        P64::Debug::printf(
            190, 140, "%s", ui(console::Message::result)
        );
        if (battle_ui.result_error != sim::CommandError::none) {
            P64::Debug::printf(
                190, 170, "%s",
                command_error_label(battle_ui.result_error)
            );
        } else if (battle_ui.result_command == sim::CommandType::wait ||
                   battle_ui.result_command == sim::CommandType::talk) {
            P64::Debug::printf(
                190, 170, "%s", ui(console::Message::turn_complete)
            );
        } else if (battle_ui.result_missed) {
            P64::Debug::printf(
                190, 170, "%s", ui(console::Message::miss)
            );
        } else if (battle_ui.result_restored) {
            P64::Debug::printf(
                190, 170, "%s %d", ui(console::Message::mended),
                battle_ui.result_amount
            );
        } else {
            P64::Debug::printf(
                190, 170, "%s %d", ui(console::Message::hit),
                battle_ui.result_amount
            );
        }
    }
    rdpq_mode_pop();
}

void draw_wrapped_story(std::string_view text) {
    constexpr std::size_t width = 37U;
    std::size_t start = 0U;
    int line = 0;
    while (start < text.size() && line < 4) {
        std::size_t end = std::min(start + width, text.size());
        if (end < text.size()) {
            const std::size_t space = text.rfind(' ', end);
            if (space != std::string_view::npos && space > start) end = space;
        }
        P64::Debug::printf(
            40, 126 + line * 18, "%.*s",
            static_cast<int>(end - start), text.data() + start
        );
        start = end;
        while (start < text.size() && text[start] == ' ') ++start;
        ++line;
    }
}

std::vector<client::RosterEntry> localized_campaign_roster() {
    if (!board.campaign.has_value()) return {};
    client::CompanyManagement company = board.campaign->management();
    if (company.error != client::CampaignSessionError::none) return {};
    for (client::RosterEntry& member : company.roster) {
        if (member.has_player_name) continue;
        const std::string_view authored = member.authored_name.empty()
            ? std::string_view(member.name)
            : std::string_view(member.authored_name);
        const std::string_view localized =
            pr::localized_campaign_member_name(
                board.runtime.package(), game_language(),
                grandleon_project_campaign_id,
                member.placement_source_key, authored
            );
        member.name.assign(localized.data(), localized.size());
    }
    return company.roster;
}

void draw_name_editor() {
    if (!shell.name_editor.has_value()) return;
    const client::ConsoleNameEditor& editor = *shell.name_editor;
    P64::Debug::printf(
        42, 84, "%s", ui(console::Message::character_name)
    );
    P64::Debug::printf(
        42, 101, "%s  %s",
        editor.name().empty() ? "_" : editor.c_str(),
        editor.invalid() ? ui(console::Message::required) : ""
    );
    for (int row = 0; row < client::ConsoleNameEditor::rows; ++row) {
        for (int column = 0;
             column < client::ConsoleNameEditor::columns(row); ++column) {
            const client::ConsoleNameKey key =
                client::ConsoleNameEditor::key(row, column);
            const bool selected = row == editor.row() &&
                                  column == editor.column();
            int x = 27 + column * 30;
            if (row == 4 && column >= 2) x = column == 2 ? 87 : 151;
            P64::Debug::printf(
                x, 119 + row * 15, "%s%s%s",
                selected ? "[" : " ", key.label,
                selected ? "]" : " "
            );
        }
    }
    P64::Debug::printf(
        31, 192, "A %s  B %s",
        ui(console::Message::type_name), ui(console::Message::erase)
    );
    P64::Debug::printf(
        31, 204, "C-UP %s  START %s",
        ui(console::Message::keep), ui(console::Message::save_action)
    );
}

void draw_shell() {
    rdpq_mode_push();
    rdpq_mode_begin();
        rdpq_mode_zbuf(false, false);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    rdpq_set_prim_color({8, 16, 27, 255});
    rdpq_fill_rectangle(0, 0, 320, 240);
    rdpq_set_prim_color({58, 84, 104, 255});
    rdpq_fill_rectangle(18, 24, 302, 216);
    rdpq_set_prim_color({14, 26, 39, 255});
    rdpq_fill_rectangle(22, 28, 298, 212);
    rdpq_set_prim_color({184, 145, 66, 255});
    rdpq_fill_rectangle(42, 56, 278, 59);
    P64::Debug::printStart();
    const std::string_view project_title = pr::localized_project_title(
        board.runtime.package(), game_language(), grandleon_project_title
    );
    P64::Debug::printf(
        43, 38, "%.*s",
        static_cast<int>(std::min<std::size_t>(project_title.size(), 29U)),
        project_title.data()
    );
    P64::Debug::printf(74, 66, "PYRITE / TINY3D");
    switch (shell.phase) {
        case ShellPhase::title:
            P64::Debug::printf(
                91, 116, "%s %s",
                shell.title_row == 0U ? ">" : " ",
                ui(console::Message::begin)
            );
            P64::Debug::printf(
                91, 140, "%s %s",
                shell.title_row == 1U ? ">" : " ",
                ui(console::Message::help)
            );
            if (game_languages().size() > 1U) {
                P64::Debug::printf(
                    91, 164, "%s %s: %s",
                    shell.title_row == 2U ? ">" : " ",
                    ui(console::Message::language),
                    console::language_code(game_language())
                );
            }
            P64::Debug::printf(61, 194, "RENDERER: PYRITE");
            break;
        case ShellPhase::help:
            P64::Debug::printf(119, 88, "%s", ui(console::Message::help));
            P64::Debug::printf(43, 112, "%s", ui(console::Message::move_cursor));
            P64::Debug::printf(43, 132, "%s", ui(console::Message::select_confirm));
            P64::Debug::printf(43, 152, "Z      %s", ui(console::Message::action_heading));
            P64::Debug::printf(43, 172, "START  %s", ui(console::Message::settings));
            P64::Debug::printf(43, 192, "%s", ui(console::Message::back));
            break;
        case ShellPhase::players:
            P64::Debug::printf(78, 100, "%s", ui(console::Message::players));
            if (shell.allow_one) {
                P64::Debug::printf(
                    91, 128, "%s %s",
                    shell.player_choice == 0U ? ">" : " ",
                    ui(console::Message::one_player)
                );
            }
            if (shell.allow_two) {
                P64::Debug::printf(
                    76, 150, "%s %s%s",
                    shell.player_choice == 1U ? ">" : " ",
                    ui(console::Message::two_players),
                    shell.second_connected ? "" : " (PAD 2)"
                );
            }
            break;
        case ShellPhase::save:
            P64::Debug::printf(84, 100, "%s", ui(console::Message::start_campaign));
            P64::Debug::printf(
                96, 128, "%s %s",
                shell.save_choice == 0U ? ">" : " ",
                ui(console::Message::new_game)
            );
            if (shell.save_present) {
                P64::Debug::printf(
                    96, 150, "%s %s",
                    shell.save_choice == 1U ? ">" : " ",
                    ui(console::Message::continue_game)
                );
            } else {
                P64::Debug::printf(75, 178, "%s", ui(console::Message::no_save));
            }
            break;
        case ShellPhase::story: {
            if (shell.story_line_index < shell.story.lines.size()) {
                const pr::DialogueLine& line =
                    shell.story.lines[shell.story_line_index];
                const std::string_view localized_speaker =
                    pr::localized_dialogue_line(
                    board.runtime.package(), game_language(), shell.story.id,
                    shell.story_line_index, "speaker", line.speaker
                );
                const std::string_view localized_text =
                    pr::localized_dialogue_line(
                    board.runtime.package(), game_language(), shell.story.id,
                    shell.story_line_index, "text", line.text
                );
                const std::vector<client::RosterEntry> roster =
                    localized_campaign_roster();
                const std::string speaker =
                    client::substitute_campaign_member_names(
                        localized_speaker, roster
                    );
                const std::string text =
                    client::substitute_campaign_member_names(
                        localized_text, roster
                    );
                P64::Debug::printf(
                    40, 96, "%.*s", static_cast<int>(speaker.size()),
                    speaker.data()
                );
                draw_wrapped_story(text);
                P64::Debug::printf(
                    220, 194, "A %s", ui(console::Message::next)
                );
            }
            break;
        }
        case ShellPhase::name:
            draw_name_editor();
            break;
        case ShellPhase::deployment: {
            P64::Debug::printf(76, 88, "%s %lu/%u",
                ui(console::Message::deployment),
                static_cast<unsigned long>(shell.deployment.fielded.size()),
                static_cast<unsigned>(shell.deployment.capacity));
            const std::size_t first = shell.deployment_row >= 4U
                ? shell.deployment_row - 3U : 0U;
            const std::size_t last = std::min(
                first + 4U, shell.deployment.roster.size()
            );
            for (std::size_t row = first; row < last; ++row) {
                const client::RosterEntry& entry =
                    shell.deployment.roster[row];
                const std::string_view authored_name =
                    entry.authored_name.empty()
                        ? std::string_view(entry.name)
                        : std::string_view(entry.authored_name);
                const std::string_view name = entry.has_player_name
                    ? std::string_view(entry.name)
                    : pr::localized_campaign_member_name(
                          board.runtime.package(), game_language(),
                          grandleon_project_campaign_id,
                          entry.placement_source_key, authored_name
                      );
                P64::Debug::printf(
                    48, 116 + static_cast<int>(row - first) * 18,
                    "%s %s %.*s",
                    shell.deployment_row == row ? ">" : " ",
                    deployment_contains(entry.member) ? "[X]" : "[ ]",
                    static_cast<int>(std::min<std::size_t>(
                        name.size(), 22U
                    )), name.data()
                );
            }
            P64::Debug::printf(
                55, 194, "A %s  START %s",
                ui(console::Message::toggle), ui(console::Message::deploy)
            );
            break;
        }
        case ShellPhase::aftermath: {
            P64::Debug::printf(
                91, 88, "%s",
                shell.aftermath.outcome == sim::Outcome::first_side_won
                    ? ui(console::Message::victory)
                    : ui(console::Message::defeat)
            );
            const std::size_t count = std::min<std::size_t>(
                shell.aftermath_objectives.size(), 3U
            );
            const std::vector<client::RosterEntry> roster =
                localized_campaign_roster();
            for (std::size_t row = 0; row < count; ++row) {
                const sim::ObjectiveResult& objective =
                    shell.aftermath_objectives[row];
                const std::string_view name = pr::content_name(
                    board.runtime.package(),
                    package_format::SectionType::objectives, objective.id
                );
                const std::string_view localized_name =
                    pr::localized_objective_name(
                        board.runtime.package(), game_language(),
                        objective.id, name
                    );
                const std::string displayed_name =
                    client::substitute_campaign_member_names(
                        localized_name, roster
                    );
                const char* result = ui(
                    objective.state == sim::ObjectiveState::satisfied
                        ? console::Message::clear
                        : objective.state == sim::ObjectiveState::failed
                            ? console::Message::failed
                            : console::Message::pending
                );
                P64::Debug::printf(
                    42, 116 + static_cast<int>(row) * 18,
                    "%.*s  %s",
                    static_cast<int>(std::min<std::size_t>(
                        displayed_name.size(), 21U
                    )), displayed_name.data(), result
                );
            }
            P64::Debug::printf(
                42, 176, "%s %lu  %s %lu",
                ui(console::Message::fell),
                static_cast<unsigned long>(shell.aftermath.fallen.size()),
                ui(console::Message::joined),
                static_cast<unsigned long>(shell.aftermath.recruited.size())
            );
            P64::Debug::printf(
                202, 194, "A %s", ui(console::Message::next)
            );
            break;
        }
        case ShellPhase::complete:
            P64::Debug::printf(91, 112, "%s", ui(console::Message::campaign_ended));
            P64::Debug::printf(
                72, 142, "%lu %s",
                static_cast<unsigned long>(shell.completed_battles),
                ui(console::Message::battles_played)
            );
            P64::Debug::printf(72, 178, "%s", ui(console::Message::progress_saved));
            break;
        case ShellPhase::battle:
            break;
    }
    rdpq_mode_pop();
}

}  // namespace

namespace P64::GlobalScript::C47524E444C454F4 {

void onGameInit() {
    load_runtime_board();
    board.cartridge.emplace();
    board.saves.emplace(*board.cartridge, gp::CartridgeStorage::budget());
    assertf(board.saves->available(), "Cartridge save storage is unavailable");
    load_user_settings();
    apply_audio_setting();
    board.ambience_handle = P64::AudioManager::play2D(&board.ambience);
    board.ambience_handle.setVolume(0.34F);
    const pr::CampaignLoadResult campaign = pr::load_campaign(
        board.runtime.package(), grandleon_project_campaign_id
    );
    assertf(static_cast<bool>(campaign), "Authored campaign did not load");
    shell.allow_one = campaign.definition.player_mode == pr::LocalPlayerMode::one ||
        campaign.definition.player_mode == pr::LocalPlayerMode::choice;
    shell.allow_two = campaign.definition.player_mode == pr::LocalPlayerMode::two ||
        campaign.definition.player_mode == pr::LocalPlayerMode::choice;
    shell.second_connected = joypad_is_connected(JOYPAD_PORT_2);
    assert(shell.allow_one || shell.allow_two);
    // A connected second pad advertises the option; it must not silently turn
    // a choice campaign into co-op. Keep the normal launch on 1P unless the
    // campaign requires two local players.
    shell.player_choice = shell.allow_one ? 0U : 1U;
    shell.save_present = board.saves->contains(campaign_slot());
    debugf(
        "GRANDLEON_PYRITE_LAUNCH campaign=%s player_mode=%s "
        "save=%u renderer=pyrite-tiny3d fallback=none\n",
        grandleon_project_campaign_source,
        pr::local_player_mode_name(campaign.definition.player_mode).data(),
        shell.save_present ? 1U : 0U
    );
}

void onSceneUpdate() {
    if (shell.phase != ShellPhase::battle) {
        update_shell_input();
        frame_count += kLogicTicksPerUpdate;
        update_performance_sample();
        return;
    }
    if (!camera_configured) {
        apply_camera(tactical_camera_for(board.game.cursor()));
        camera_min_x = current_camera.target_x;
        camera_max_x = current_camera.target_x;
        camera_min_z = current_camera.target_z;
        camera_max_z = current_camera.target_z;
        camera_configured = true;
    }
    if (battle_ui.phase == BattleUiPhase::result && !combat.active &&
        frame_count >= battle_ui.result_until) {
        battle_ui = {};
    }
    physical_input();
    update_combat();
    update_battle_moment();
    if (!battle_moment.active && battle_moment.dialogue_ids.empty()) {
        if (replay.user_took_over) update_manual_battle();
        else update_replay();
    }
    update_tactical_camera();
    if (motion.active && frame_count - motion.started >= motion.total_frames) {
        motion.active = false;
    }
    update_visual_visibility();
    if (restore_checkpoint_delay > 0U && --restore_checkpoint_delay == 0U &&
        !combat.restore_checkpoint) {
        debugf("CHECKPOINT PYRITE_COMBAT_RESTORE\n");
        combat.restore_checkpoint = true;
    }
    build_unit_vertices();
    build_visibility_vertices();
    build_cursor_vertices();

    frame_count += kLogicTicksPerUpdate;
    update_performance_sample();
}

void onScenePreDraw3D() {
    if (!board.loaded || shell.phase != ShellPhase::battle) return;
    draw_environment();
    draw_elevation();
    draw_board_cells();
    draw_terrain_forms();
    draw_landmark();
    draw_trees();
    draw_visual_visibility();
    board.shadows.draw();
    draw_cursor();
    draw_units();
}

void onSceneDraw2D() {
    if (shell.phase != ShellPhase::battle) {
        draw_shell();
        return;
    }
    draw_combat_stage_2d();
    draw_battle_ui();
    if (!debug_hud) return;
    Debug::printStart();
    if (combat.active) {
        std::int16_t left_health = combat.attacker_health_before;
        std::int16_t right_health = combat.defender_health_before;
        const char* result = "DRONE CAMERA";
        int amount = 0;
        if (combat.phase == CombatPhase::first_strike) {
            if (combat.phase_ticks >= 30U) right_health = combat.defender_health_after;
            result = combat.first_missed ? "MISS" : "HIT";
            amount = combat.first_damage;
        } else if (combat.phase == CombatPhase::counter) {
            right_health = combat.defender_health_after;
            if (combat.phase_ticks >= 30U) left_health = combat.attacker_health_after;
            result = combat.counter_missed ? "COUNTER MISS" : "COUNTER HIT";
            amount = combat.counter_damage;
        } else if (combat.phase == CombatPhase::returning) {
            left_health = combat.attacker_health_after;
            right_health = combat.defender_health_after;
            result = "RETURN";
        }
        Debug::printf(
            6, 7, "LEFT %dHP        RIGHT %dHP", left_health,
            right_health
        );
        if (amount > 0) {
            Debug::printf(6, 19, "%s -%d   B SKIP  R FAST", result, amount);
        } else {
            Debug::printf(6, 19, "%s   B SKIP  R FAST", result);
        }
        return;
    }
    Debug::printf(
        6, 7, "STAGE %lu  R%lu  LEAD %dHP  %dFPS",
        static_cast<unsigned long>(shell.stage_index + 1U),
        static_cast<unsigned long>(board.game.snapshot().round + 1U),
        board.rowan_health,
        static_cast<int>(VI::SwapChain::getFPS())
    );
    Debug::printf(
        6, 19, "P%u %d,%d %s  A MOVE Z ACTION",
        static_cast<unsigned>(board.game.active_player() + 1U),
        board.game.cursor(board.game.active_player()).x,
        board.game.cursor(board.game.active_player()).y,
        board.game.selected(board.game.active_player()) == 0
            ? "READY" : "SELECTED"
    );
}

}  // namespace P64::GlobalScript::C47524E444C454F4
