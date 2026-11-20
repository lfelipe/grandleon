// SPDX-License-Identifier: MIT
#include "script/globalScript.h"

#include <grandleon/client/campaign_session.hpp>
#include <grandleon/client/console_localization.hpp>
#include <grandleon/client/console_name_editor.hpp>
#include <grandleon/client/session.hpp>
#include <grandleon/client/shop_controller.hpp>
#include <grandleon/client/shared_supplies.hpp>
#include <grandleon/simulation/carried_inventory.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/core/currency.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/package_runtime/localization.hpp>
#include <grandleon/package_runtime/manifest.hpp>
#include <grandleon/package_runtime/names.hpp>
#include <grandleon/package_runtime/target_budget.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/storage/byte_window_storage.hpp>
#include <libdragon.h>
#include <t3d/t3d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "debug/debugDraw.h"
#include "audio/audioManager.h"
#include "BoardShadows.hpp"
#include "StoneKeep.hpp"
#include "ModularCastle.hpp"
#include "GeneratedSettlement.hpp"
#include <grandleon/view/stage_overview.hpp>
#include "SettlementScenery.hpp"
#include "ScenerySprites.hpp"
#include "GeneratedSceneryStyle.hpp"
#include "ConsoleFont.hpp"
#include "BoardCamera.hpp"
#include "BoardSurroundings.hpp"
#include "CartridgeStorage.hpp"
#include "GameplaySlice.hpp"
#include "GeneratedProject.hpp"
#include "GpkRuntimeAdapter.hpp"
#include "scene/camera.h"
#include "renderer/renderScale.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "vi/swapChain.h"
#include <grandleon/view/motion.hpp>
#include <grandleon/view/ambient_motion.hpp>
#include <grandleon/view/brand_logo.h>
#include <grandleon/view/console_text.h>
#include <grandleon/view/conversation_menu.hpp>

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
constexpr std::size_t kMaxCells =
    grandleon::package_runtime::target_budget::nintendo64_cells_per_map;
constexpr std::size_t kBatchCells = 15U;
constexpr std::size_t kElevationVerticesPerCell = 8U;
constexpr std::size_t kGroundVertexCount = 4U;
constexpr std::size_t kMaxApronCells = 800U;
constexpr std::size_t kTreeVertexCount = 20U;
constexpr std::size_t kMaxTrees = 176U;
constexpr std::size_t kTerrainFormVertexCount = 8U;
constexpr std::size_t kMaxTerrainForms = 384U;
constexpr std::size_t kUnitVertexCount = 4U;
constexpr std::size_t kMaxUnits = 48U;
constexpr std::size_t kCursorVerticesPerPlayer = 32U;
constexpr std::size_t kReachVerticesPerCell = 20U;
constexpr std::size_t kCursorVertexCount =
    kMaxCells * kReachVerticesPerCell + kCursorVerticesPerPlayer * 2U;
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
    std::array<sprite_t*, 6> directional{};
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
    grandleon::campaign::BattleBinding battle_binding;
    std::vector<sim::Event> battle_events;
    sim::Event discovery{};
    std::uint32_t discovery_frame{};
    T3DVertPacked* cells{};
    T3DVertPacked* ground{};
    std::vector<std::uint16_t> water_quads;
    std::int16_t water_s{}, water_t{};
    T3DVertPacked* apron{};
    T3DVertPacked* elevation{};
    T3DVertPacked* terrain_forms{};
    T3DVertPacked* trees{};
    rspq_block_t* tree_commands{};
    std::uint32_t tree_command_bytes{};
    T3DVertPacked* landmark{};
    T3DVertPacked* visibility{};
    T3DVertPacked* units{};
    T3DVertPacked* cursor{};
    sprite_t* terrain{};
    sprite_t* surface_detail{};
    sprite_t* combat_backdrop{};
    sprite_t* combat_shadow{};
    std::array<UnitArt, grandleon_project_drawings.size()> art{};
    std::array<view::BoardHeading, kMaxUnits> headings{};
    std::array<bool, 4> directional_checkpoints{};
    wav64_t ambience{};
    std::array<wav64_t, 5> music{};
    std::array<wav64_t, 6> combat_cues{};
    std::array<P64::Audio::Handle, 8> effect_handles{};
    std::size_t next_effect{};
    unsigned music_track{99U};
    wav64_t ui_cue{};
    wav64_t impact_cue{};
    wav64_t victory_cue{};
    P64::Audio::Handle ambience_handle{};
    std::size_t elevation_vertex_count{};
    std::size_t terrain_form_count{};
    std::array<std::uint8_t, kMaxTerrainForms> terrain_form_shapes{};
    std::size_t apron_cell_count{};
    std::size_t tree_count{};
    std::array<std::int8_t, kMaxTrees> tree_sway{};
    std::array<bool, kMaxTrees> tree_broadleaf{};
    std::size_t landmark_vertex_count{};
    gp::StoneKeep keep{};
    gp::ModularCastle modular_castle{};
    gp::SettlementScenery settlement{};
    gp::ScenerySprites scenery_sprites{};
    std::size_t visibility_vertex_count{};
    std::array<bool, kMaxCells> visibility_revealed{};
    std::size_t unit_count{};
    std::size_t cursor_vertex_count{kCursorVerticesPerPlayer};
    std::size_t cursor_vertex_capacity{};
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
    settings,
    help,
    players,
    save,
    story,
    name,
    deployment,
    supplies,
    shop,
    aftermath,
    complete,
    battle,
};

struct ShellState final {
    ShellPhase phase{ShellPhase::title};
    std::uint8_t local_players{1U};
    std::uint8_t player_choice{};
    std::uint8_t title_row{};
    std::uint8_t audio_row{};
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
    std::size_t story_text_offset{};
    std::size_t story_text_next{};
    bool story_text_more{};
    pr::Dialogue story;
    std::vector<campaign::PersistentEntityId> pending_player_names;
    client::RosterEntry name_member;
    std::optional<client::ConsoleNameEditor> name_editor;
    client::CompanyManagement deployment;
    client::CompanyManagement shop;
    client::shop::Controller shop_ui;
    std::size_t shop_catalog_top{};
    unsigned shop_focus_player{};
    bool shop_approved{};
    bool supplies_approved{};
    client::SharedSupplies supplies;
    client::BattleAftermath aftermath;
    std::size_t aftermath_reward_offset{};
    std::vector<sim::ObjectiveResult> aftermath_objectives;
};

enum class BattleUiPhase : std::uint8_t {
    board,
    system,
    help,
    action,
    pack,
    attack_target,
    ability_target,
    talk_target,
    result,
};

enum class PackView : std::uint8_t { hub, inventory, ground, entry, loot, exchange };

struct BattleUiState final {
    BattleUiPhase phase{BattleUiPhase::board};
    sim::UnitId actor{};
    sim::ContentId ability{};
    sim::ContentId weapon{};
    std::uint64_t wallet{};
    PackView pack_view{PackView::hub};
    std::uint16_t pack_page{};
    std::uint64_t ground_item{};
    sim::ContentId carried_content{};
    grandleon::core::ContentCategory carried_category{grandleon::core::ContentCategory::item};
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
    std::size_t text_offset{};
    std::size_t text_next{};
    bool text_more{};
    std::uint32_t next_automatic_frame{};
    pr::Dialogue dialogue;
    bool active{};
};

struct ReplayState final {
    bool castle_exterior_aimed{};
    bool castle_exterior_checkpoint{};
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
    bool first_sound{};
    bool counter_sound{};
    unsigned first_cue{};
    unsigned counter_cue{};
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
    sim::UnitSnapshot attacker_art{};
    sim::UnitSnapshot defender_art{};
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
    0.0F, 6.8F, 7.78F,
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
view::ConversationMenu conversation_menu;
sim::UnitId conversation_target{};
std::string conversation_speaker;
CameraPose current_camera{kTacticalCamera};
grandleon::view::StageOverview stage_overview{};
CameraPose overview_from{}, overview_to{};
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
unsigned music_volume = 45U;
unsigned effects_volume = 75U;
std::string selected_language;
float camera_min_x = 0.0F;
float camera_max_x = 0.0F;
float camera_min_z = 0.0F;
float camera_max_z = 0.0F;

// Review-only observations: update cadence includes normal VI/GPU waits;
// submission time measures CPU command generation, not GPU completion. No
// extra fences, frame waits or simulation changes are introduced by this probe.
struct BoardTimingSample final {
    std::uint64_t interval_ticks{};
    std::uint64_t interval_max{};
    std::uint64_t submit_ticks{};
    std::uint64_t submit_max{};
    std::uint32_t intervals{};
    std::uint32_t submissions{};
    std::uint32_t intervals_over_40ms{};
};
constexpr std::array<const char*, 6> kBoardTimingContexts{
    "all", "overview", "camera", "units", "combat", "settled"
};
std::array<std::array<BoardTimingSample, kBoardTimingContexts.size()>,
    grandleon_project_encounter_count> board_timings{};
struct BoardTimingObservation final {
    std::uint64_t update_started{};
    std::uint64_t submit_started{};
    std::size_t stage{};
    std::uint8_t contexts{};
    bool pending{};
    bool update_was_battle{};
    std::size_t update_stage{};
    bool submitting{};
    bool camera_valid{};
    std::size_t camera_stage{};
    CameraPose camera{};
} board_timing;

void board_timing_begin_update() {
    if (!grandleon_project_review_driver) return;
    const auto now = get_ticks();
    if (board_timing.pending && shell.phase == ShellPhase::battle &&
        shell.stage_index == board_timing.stage) {
        const auto elapsed = now - board_timing.update_started;
        for (std::size_t context = 0; context < kBoardTimingContexts.size(); ++context) {
            if (!(board_timing.contexts & (1U << context))) continue;
            auto& sample = board_timings[board_timing.stage][context];
            sample.interval_ticks += elapsed;
            sample.interval_max = std::max(sample.interval_max, elapsed);
            ++sample.intervals;
            if (TICKS_TO_US(elapsed) > 40000U) ++sample.intervals_over_40ms;
        }
    }
    board_timing.pending = false;
    board_timing.update_started = now;
    board_timing.update_was_battle = board.loaded && shell.phase == ShellPhase::battle;
    board_timing.update_stage = shell.stage_index;
}

void board_timing_begin_submission() {
    if (!grandleon_project_review_driver) return;
    const auto& last = board_timing.camera;
    const bool camera_moved = board_timing.camera_valid &&
        board_timing.camera_stage == shell.stage_index &&
        (last.eye_x != current_camera.eye_x || last.eye_y != current_camera.eye_y ||
         last.eye_z != current_camera.eye_z || last.target_x != current_camera.target_x ||
         last.target_y != current_camera.target_y || last.target_z != current_camera.target_z ||
         last.ortho != current_camera.ortho);
    board_timing.contexts = 1U |
        (stage_overview.active() ? 2U : 0U) |
        (camera_moved ? 4U : 0U) | (motion.active ? 8U : 0U) |
        (combat.active ? 16U : 0U);
    if (board_timing.contexts == 1U) board_timing.contexts |= 32U;
    board_timing.camera = current_camera;
    board_timing.camera_stage = shell.stage_index;
    board_timing.camera_valid = true;
    board_timing.stage = shell.stage_index;
    board_timing.pending = board_timing.update_was_battle &&
        board_timing.update_stage == shell.stage_index;
    board_timing.submitting = true;
    board_timing.submit_started = get_ticks();
}

void board_timing_end_submission() {
    if (!grandleon_project_review_driver || !board_timing.submitting) return;
    const auto elapsed = get_ticks() - board_timing.submit_started;
    board_timing.submitting = false;
    for (std::size_t context = 0; context < kBoardTimingContexts.size(); ++context) {
        if (!(board_timing.contexts & (1U << context))) continue;
        auto& sample = board_timings[board_timing.stage][context];
        sample.submit_ticks += elapsed;
        sample.submit_max = std::max(sample.submit_max, elapsed);
        ++sample.submissions;
    }
}

void board_timing_report() {
    if (!grandleon_project_review_driver) return;
    for (std::size_t context = 0; context < kBoardTimingContexts.size(); ++context) {
        const auto& sample = board_timings[shell.stage_index][context];
        if (!sample.intervals && !sample.submissions) continue;
        debugf("GRANDLEON_N64_BOARD_TIMING stage=%lu context=%s intervals=%lu "
            "interval_avg_us=%llu interval_max_us=%llu intervals_over_40ms=%lu "
            "submissions=%lu submit_avg_us=%llu submit_max_us=%llu\n",
            static_cast<unsigned long>(shell.stage_index + 1U), kBoardTimingContexts[context],
            static_cast<unsigned long>(sample.intervals),
            sample.intervals ? TICKS_TO_US(sample.interval_ticks) / sample.intervals : 0ULL,
            TICKS_TO_US(sample.interval_max), static_cast<unsigned long>(sample.intervals_over_40ms),
            static_cast<unsigned long>(sample.submissions),
            sample.submissions ? TICKS_TO_US(sample.submit_ticks) / sample.submissions : 0ULL,
            TICKS_TO_US(sample.submit_max));
    }
}

constexpr std::string_view kSettingsSlot = "pyrite_settings";
constexpr std::array<std::uint8_t, 3> kSettingsMagic{'P', 'Y', 'S'};

std::string_view game_language() {
    return selected_language;
}

constexpr color_t kUiIvory{241, 234, 216, 255};
constexpr color_t kUiGold{201, 170, 103, 255};
constexpr color_t kUiFrame{126, 111, 78, 255};
constexpr color_t kUiSurface{13, 25, 37, 248};
constexpr color_t kUiInset{24, 40, 55, 255};
constexpr color_t kUiFocus{38, 61, 77, 255};

color_t player_accent(std::uint8_t player) {
    return player == 0U ? color_t{104, 207, 231, 255}
                        : color_t{235, 147, 184, 255};
}

std::string console_latin1(std::string_view utf8) {
    const std::string terminated(utf8);
    const char* cursor = terminated.c_str();
    std::string result;
    result.reserve(utf8.size());
    while (*cursor != '\0') result.push_back(grandleon_text_next(&cursor));
    return result;
}

std::size_t console_prefix_bytes(std::string_view utf8, std::size_t cells) {
    const std::string terminated(utf8);
    const char* cursor = terminated.c_str();
    for (std::size_t count = 0U; count < cells && *cursor != '\0'; ++count) {
        (void)grandleon_text_next(&cursor);
    }
    return static_cast<std::size_t>(cursor - terminated.c_str());
}

void display_glyphs(int x, int y, std::string_view latin1, color_t ink = kUiIvory, int scale = 1) {
    namespace font = gp::console_font;
    rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM, ENV, TEX0, ENV), (0, 0, 0, TEX0)));
        rdpq_mode_alphacompare(128);
    rdpq_mode_end();
    P64::Debug::setColor(ink);
    P64::Debug::setBgColor();
    constexpr unsigned glyphs_per_bank = font::bank_width / 8U;
    unsigned loaded_bank = 255U;
    for (const unsigned char encoded : latin1) {
        const unsigned code = encoded >= font::first ? encoded : '?';
        const unsigned slot = code - font::first;
        const unsigned bank = slot / glyphs_per_bank;
        if (code != ' ' && code != 160U) {
            if (bank != loaded_bank) {
                surface_t surface = surface_make(const_cast<std::uint8_t*>(font::banks[bank]),
                    FMT_IA4, font::bank_width, font::height, font::bank_width / 2U);
                rdpq_tex_upload(TILE0, &surface, nullptr);
                loaded_bank = bank;
            }
            const int texture_x = static_cast<int>(slot % glyphs_per_bank) * 8;
            rdpq_texture_rectangle_raw(TILE0, x, y, x + 8 * scale, y + font::height * scale,
                texture_x, 0, 1.0F / static_cast<float>(scale), 1.0F / static_cast<float>(scale));
        }
        x += font::advance * scale;
    }
}

void display_printf(int x, int y, const char* format, ...) {
    std::array<char, 512> formatted{};
    std::array<char, 512> latin1{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(formatted.data(), formatted.size(), format, arguments);
    va_end(arguments);
    const char* cursor = formatted.data();
    std::size_t count = 0U;
    while (*cursor != '\0' && count + 1U < latin1.size()) {
        latin1[count++] = grandleon_text_next(&cursor);
    }
    display_glyphs(x, y, std::string_view(latin1.data(), count));
}

const char* ui(console::Message message) {
    return console::text(game_language(), message);
}

void ui_flat() {
    rdpq_mode_begin();
        rdpq_mode_zbuf(false, false);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_alphacompare(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
}

void ui_rectangle(int x, int y, int width, int height, color_t colour) {
    if (width <= 0 || height <= 0) return;
    rdpq_set_prim_color(colour);
    rdpq_fill_rectangle(x, y, x + width, y + height);
}

void ui_panel(int x, int y, int width, int height, int header = 0) {
    rdpq_mode_push();
    ui_flat();
    ui_rectangle(x + 2, y + 3, width, height, {3, 8, 14, 170});
    ui_rectangle(x, y, width, height, kUiFrame);
    ui_rectangle(x + 1, y + 1, width - 2, height - 2, kUiSurface);
    ui_rectangle(x + 2, y + 2, width - 4, 1, {65, 79, 88, 255});
    if (header > 0) {
        ui_rectangle(x + 3, y + 3, width - 6, header - 3, kUiInset);
        ui_rectangle(x + 8, y + header, width - 16, 1, kUiFrame);
    }
    for (int side = 0; side < 2; ++side) {
        const int edge = side == 0 ? x + 3 : x + width - 10;
        ui_rectangle(edge, y + 3, 7, 1, kUiGold);
        ui_rectangle(side == 0 ? edge : edge + 6, y + 3, 1, 7, kUiGold);
        ui_rectangle(edge, y + height - 4, 7, 1, kUiGold);
    }
    rdpq_mode_pop();
}

void ui_focus(int x, int y, int width, int height, bool selected, color_t accent = kUiGold) {
    if (!selected) return;
    rdpq_mode_push();
    ui_flat();
    ui_rectangle(x, y, width, height, kUiFocus);
    ui_rectangle(x, y + 1, 2, height - 2, accent);
    const int middle = y + height / 2;
    ui_rectangle(x + 6, middle - 3, 2, 6, accent);
    ui_rectangle(x + 8, middle - 2, 2, 4, accent);
    ui_rectangle(x + 10, middle - 1, 2, 2, accent);
    rdpq_mode_pop();
}

void ui_button_hint(int x, int y, const char* button, const char* label, color_t accent = kUiGold) {
    const int width = static_cast<int>(std::char_traits<char>::length(button)) * 6 + 8;
    rdpq_mode_push();
    ui_flat();
    ui_rectangle(x, y, width, 14, accent);
    ui_rectangle(x + 1, y + 1, width - 2, 12, kUiInset);
    rdpq_mode_pop();
    display_glyphs(x + 4, y + 1, button, kUiIvory);
    if (*label != '\0') display_glyphs(x + width + 5, y + 1, console_latin1(label), kUiIvory);
}

void ui_health_meter(int x, int y, int width, int health, int maximum, int after) {
    const int before_width = width * std::clamp(health, 0, maximum) / std::max(1, maximum);
    const int after_width = width * std::clamp(after, 0, maximum) / std::max(1, maximum);
    rdpq_mode_push();
    ui_flat();
    ui_rectangle(x - 1, y - 1, width + 2, 6, {81, 94, 100, 255});
    ui_rectangle(x, y, width, 4, {29, 45, 52, 255});
    ui_rectangle(x, y, before_width, 4, {182, 105, 78, 255});
    ui_rectangle(x, y, after_width, 4, {130, 186, 133, 255});
    rdpq_mode_pop();
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
    bytes.push_back(3U);
    std::uint8_t flags = 0U;
    if (combat.enabled) flags |= 1U << 0U;
    if (combat.reduced_motion) flags |= 1U << 1U;
    if (debug_hud) flags |= 1U << 2U;
    if (music_volume != 0U || effects_volume != 0U) flags |= 1U << 3U;
    bytes.push_back(flags);
    const std::size_t language_size = std::min<std::size_t>(
        selected_language.size(), 15U
    );
    bytes.push_back(static_cast<std::uint8_t>(language_size));
    bytes.insert(
        bytes.end(), selected_language.begin(),
        selected_language.begin() + static_cast<std::ptrdiff_t>(language_size)
    );
    bytes.push_back(static_cast<std::uint8_t>(music_volume));
    bytes.push_back(static_cast<std::uint8_t>(effects_volume));
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
    if (version < 1U || version > 3U) return;
    const std::uint8_t flags = saved.bytes[4];
    combat.enabled = (flags & (1U << 0U)) != 0U;
    combat.reduced_motion = (flags & (1U << 1U)) != 0U;
    debug_hud = (flags & (1U << 2U)) != 0U;
    if ((flags & (1U << 3U)) == 0U) music_volume = effects_volume = 0U;
    if (version < 2U || saved.bytes.size() < 6U) return;
    const std::size_t language_size = saved.bytes[5];
    if (language_size == 0U || language_size > 15U ||
        saved.bytes.size() != 6U + language_size + (version == 3U ? 2U : 0U)) return;
    if (version == 3U) {
        music_volume = std::min<unsigned>(100U, saved.bytes[6U + language_size]);
        effects_volume = std::min<unsigned>(100U, saved.bytes[7U + language_size]);
    }
    const std::string stored(
        saved.bytes.begin() + 6, saved.bytes.begin() + 6 + language_size
    );
    for (const std::string_view available : game_languages()) {
        if (available == stored) {
            selected_language = stored;
            return;
        }
    }
}

void apply_audio_setting() {
    P64::AudioManager::setMasterVolume(1.0F);
    board.ambience_handle.setVolume(static_cast<float>(music_volume) / 100.0F);
    for (auto& handle : board.effect_handles) {
        handle.setVolume(static_cast<float>(effects_volume) / 100.0F);
    }
}

void adjust_volume(unsigned row, int direction) {
    unsigned& volume = row == 0U ? music_volume : effects_volume;
    volume = static_cast<unsigned>(std::clamp(static_cast<int>(volume) + direction * 5, 0, 100));
    apply_audio_setting();
    save_user_settings();
    debugf("PYRITE_AUDIO_SETTINGS music=%u effects=%u\n", music_volume, effects_volume);
}

void play_effect(wav64_t* cue) {
    if (effects_volume == 0U) return;
    auto& handle = board.effect_handles[board.next_effect++ % board.effect_handles.size()];
    handle = P64::AudioManager::play2D(cue);
    handle.setVolume(static_cast<float>(effects_volume) / 100.0F);
}

void update_music() {
    const unsigned track = shell.phase == ShellPhase::battle
        ? (shell.stage_index >= 4U ? 4U : shell.stage_index == 1U ? 3U : 1U)
        : shell.phase == ShellPhase::story ? 2U
        : shell.phase == ShellPhase::complete ? 5U : 0U;
    if (track == board.music_track) return;
    board.ambience_handle.stop();
    board.music_track = track;
    board.ambience_handle = P64::AudioManager::play2D(
        track == 0U ? &board.ambience : &board.music[track - 1U]);
    board.ambience_handle.setVolume(static_cast<float>(music_volume) / 100.0F);
}

void play_ui_cue() {
    play_effect(&board.ui_cue);
}

void play_impact_cue() {
    play_effect(&board.impact_cue);
}

void play_victory_cue() {
    play_effect(&board.victory_cue);
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
    if (buffer == board.terrain_forms ||
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
    const float render_scale = P64::Renderer::getRenderScale();
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
    if (stage_overview.active()) {
        apply_camera(interpolate_camera(overview_from, overview_to,
            static_cast<float>(stage_overview.elapsed) / static_cast<float>(stage_overview.duration)));
        return;
    }
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

// Crossings retain their road-kind gameplay cell. Only the rendered ground
// becomes water, with a narrower timber deck at the original road height.
int river_crossing_axis(std::size_t index) {
    const int width = board.content->definition.width;
    const int height = board.content->definition.height;
    const int x = static_cast<int>(index % width);
    const int y = static_cast<int>(index / width);
    if (terrain_kind(board.content->terrain[index]) != 1U) return 0;
    const auto water = [&](int nx, int ny) {
        return nx >= 0 && ny >= 0 && nx < width && ny < height &&
            terrain_kind(board.content->terrain[static_cast<std::size_t>(ny * width + nx)]) == 0U;
    };
    if (water(x, y - 1) && water(x, y + 1)) return 1;
    if (water(x - 1, y) && water(x + 1, y)) return 2;
    return 0;
}

std::uint64_t river_surface_id(std::size_t index) {
    const int axis = river_crossing_axis(index);
    if (axis == 1) return board.content->terrain[index - board.content->definition.width];
    if (axis == 2) return board.content->terrain[index - 1U];
    return board.content->terrain[index];
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
    const auto& colours = gp::scenery_style::ground;
    const std::uint8_t kind = terrain_kind(id);
    return pack_rgba(
        static_cast<std::uint8_t>(colours[kind][0] - variation / 2U),
        static_cast<std::uint8_t>(colours[kind][1] - variation / 2U),
        static_cast<std::uint8_t>(colours[kind][2] - variation / 3U)
    );
}

bool natural_ground(std::uint8_t kind) {
    return kind == 2U || kind == 6U || kind == 7U || kind == 9U ||
        kind == 10U || kind == 11U;
}

std::uint32_t ground_corner_tint(std::uint64_t id, std::size_t index, int gx, int gz) {
    if (!natural_ground(terrain_kind(id))) return terrain_tint(id, index);
    const int width = board.content->definition.width;
    const int height = board.content->definition.height;
    unsigned red = 0U, green = 0U, blue = 0U, count = 0U;
    for (int z = gz - 1; z <= gz; ++z) {
        for (int x = gx - 1; x <= gx; ++x) {
            if (x < 0 || z < 0 || x >= width || z >= height) continue;
            const auto neighbour = static_cast<std::size_t>(z * width + x);
            const auto terrain = board.content->terrain[neighbour];
            if (!natural_ground(terrain_kind(terrain))) continue;
            const auto colour = terrain_tint(terrain, neighbour);
            red += (colour >> 24U) & 255U;
            green += (colour >> 16U) & 255U;
            blue += (colour >> 8U) & 255U;
            ++count;
        }
    }
    // Shared corners give neighbouring natural surfaces the same colour.
    // Roads, water and masonry retain their deliberate tactical boundaries.
    return count == 0U ? terrain_tint(id, index) : pack_rgba(red / count, green / count, blue / count);
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
        const auto surface = river_surface_id(index);
        const std::int16_t lift = terrain_lift(surface);
        std::int16_t s0 = 0;
        std::int16_t t0 = 0;
        std::int16_t s1 = 0;
        std::int16_t t1 = 0;
        terrain_uv(surface, index, s0, t0, s1, t1);
        const auto tint = [&](int gx, int gz) {
            return ground_corner_tint(surface, index, gx, gz);
        };
        const std::size_t first = index * kCellVertexCount;
        if (terrain_kind(surface) == 0U)
            board.water_quads.push_back(static_cast<std::uint16_t>(index));
        set_vertex(board.cells, first + 0U, cx - kHalfCell, lift, cz - kHalfCell, tint(x, z), s0, t0, normal);
        set_vertex(board.cells, first + 1U, cx + kHalfCell, lift, cz - kHalfCell, tint(x + 1, z), s1, t0, normal);
        set_vertex(board.cells, first + 2U, cx + kHalfCell, lift, cz + kHalfCell, tint(x + 1, z + 1), s1, t1, normal);
        set_vertex(board.cells, first + 3U, cx - kHalfCell, lift, cz + kHalfCell, tint(x, z + 1), s0, t1, normal);
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
    const auto& base = gp::scenery_style::ground[terrain_kind(edge_terrain)];
    int colour[3] = {base[0], base[1], base[2]};
    if (board.content->surroundings.mode == pr::MapSurroundingMode::automatic) {
        int totals[3] = {};
        const int width = board.content->definition.width;
        const int height = board.content->definition.height;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int sx = std::clamp<int>(x + dx, 0, width - 1);
                const int sz = std::clamp<int>(z + dz, 0, height - 1);
                const auto kind = terrain_kind(board.content->terrain[sz * width + sx]);
                for (int channel = 0; channel < 3; ++channel) totals[channel] += gp::scenery_style::ground[kind][channel];
            }
        }
        for (int channel = 0; channel < 3; ++channel) colour[channel] = totals[channel] / 9;
    }
    const int variation = static_cast<int>(environment_hash(x, z) % 5U) - 2;
    // Continue the same landscape outside the tactical area. A modest fade
    // separates the boundary without surrounding it with near-black tiles.
    const int percent = std::max(58, 90 - std::max(0, distance - 1) * 3);
    return pack_rgba(
        static_cast<std::uint8_t>(std::clamp(colour[0] * percent / 100 + variation, 0, 255)),
        static_cast<std::uint8_t>(std::clamp(colour[1] * percent / 100 + variation, 0, 255)),
        static_cast<std::uint8_t>(std::clamp(colour[2] * percent / 100 + variation, 0, 255))
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
    board.water_quads.clear();
    board.water_s = board.water_t = 0;
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
        : environment_colour(surrounding_terrain_at(-1, -1), -1, -1, 14);
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
            const std::int16_t floor = terrain_lift(edge_terrain);
            const auto tint = [&](int gx, int gz) {
                return environment_colour(edge_terrain, static_cast<std::int16_t>(gx), static_cast<std::int16_t>(gz), distance);
            };
            std::int16_t s0, t0, s1, t1;
            terrain_uv(edge_terrain, static_cast<std::size_t>((z + apron) * (width + 2 * apron) + x + apron), s0, t0, s1, t1);
            const std::size_t first = board.apron_cell_count * 4U;
            if (terrain_kind(edge_terrain) == 0U)
                board.water_quads.push_back(static_cast<std::uint16_t>(0x8000U | board.apron_cell_count));
            set_vertex(board.apron, first + 0U, cx - apron_half, floor,
                cz - apron_half, tint(x, z), s0, t0, normal);
            set_vertex(board.apron, first + 1U, cx + apron_half, floor,
                cz - apron_half, tint(x + 1, z), s1, t0, normal);
            set_vertex(board.apron, first + 2U, cx + apron_half, floor,
                cz + apron_half, tint(x + 1, z + 1), s1, t1, normal);
            set_vertex(board.apron, first + 3U, cx - apron_half, floor,
                cz + apron_half, tint(x, z + 1), s0, t1, normal);
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
    assert(board.elevation_vertex_count + 4U <= grandleon_project_max_map_cells * kElevationVerticesPerCell);
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
    return terrain_lift(river_surface_id(static_cast<std::size_t>(y * width + x)));
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
    rock,
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
    } else if (shape == TerrainFormShape::rock) {
        const auto roll = environment_hash(cx, cz);
        const std::int16_t depth = half * (80 + static_cast<int>(roll % 21U)) / 100;
        const std::int16_t lean = static_cast<std::int16_t>((roll >> 7U) % 5U) - 2;
        const auto point = [&](std::size_t vertex, int x, int y, int z, std::uint32_t shade) {
            set_vertex(board.terrain_forms, first + vertex, cx + x, floor + y,
                cz + z, shade, 0, 0, normal);
        };
        point(0U, -half, 0, -depth, lower);
        point(1U, half, 0, -depth, darken(colour, 135U));
        point(2U, half, 0, depth, lower);
        point(3U, -half, 0, depth, darken(colour, 200U));
        point(4U, -half / 2 + lean, height * 4 / 5, -depth / 3, colour);
        point(5U, half / 3 + lean, height * 3 / 5, -depth / 2, darken(colour, 175U));
        point(6U, half / 2, height * 2 / 3, depth / 3, darken(colour, 210U));
        point(7U, -half / 3, height, depth / 2, colour);
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

int settlement_model(std::uint64_t id);

void build_terrain_form_vertices() {
    board.terrain_form_count = 0U;
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        const auto id = board.content->terrain[index];
        if (settlement_model(id) >= 0 || river_crossing_axis(index) ||
            id == grandleon::core::stable_content_id_v1("mountain_castle_wall")) continue;
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
            append_terrain_form(cx, floor, cz, 6, 6, pack_rgba(143U, 127U, 102U), TerrainFormShape::rock);
            break;
        case 2U: // forest undergrowth
            append_terrain_form(cx, floor, cz, 12, 14, pack_rgba(73U, 126U, 58U), TerrainFormShape::rock);
            append_terrain_form(cx + 9, floor, cz - 7, 7, 9, pack_rgba(98U, 137U, 64U), TerrainFormShape::rock);
            break;
        case 3U: // mountain outcrops
            append_terrain_form(world_x(x, width) - 4, floor, world_z(z, height),
                19, 33 + static_cast<int>((roll >> 5U) % 10U),
                pack_rgba(120U, 137U, 142U), TerrainFormShape::rock);
            append_terrain_form(world_x(x, width) + 14, floor, world_z(z, height) - 12,
                10, 17 + static_cast<int>((roll >> 11U) % 8U),
                pack_rgba(106U, 121U, 128U), TerrainFormShape::rock);
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
            append_terrain_form(cx, floor, cz, 11, 16, pack_rgba(126U, 139U, 112U), TerrainFormShape::rock);
            break;
        case 8U: // ruin rubble around the authored landmark
            append_terrain_form(cx, floor, cz, 7, 9, pack_rgba(135U, 142U, 139U), TerrainFormShape::tapered);
            break;
        case 9U: // grass shrubs
            append_terrain_form(cx, floor, cz, 10, 12, pack_rgba(82U, 140U, 65U), TerrainFormShape::rock);
            if ((roll & 4U) != 0U) append_terrain_form(cx - 10, floor, cz + 7, 6, 8,
                pack_rgba(127U, 149U, 75U), TerrainFormShape::rock);
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

int settlement_model(std::uint64_t id) {
    if (gp::scenery_style::house_family(id) >= 0) return 0;
    constexpr const char* names[] = {"mountain_village_house", "mountain_abandoned_cart", "mountain_village_well", "mountain_watchtower", "mountain_walled_garden", "mountain_town_wall"};
    for (std::size_t i=0; i<6U; ++i) if (id == grandleon::core::stable_content_id_v1(names[i])) return static_cast<int>(i);
    if (id == grandleon::core::stable_content_id_v1("paved_orchard_bell")) return 7;
    if (id == grandleon::core::stable_content_id_v1("paved_signal_beacon") ||
        id == grandleon::core::stable_content_id_v1("road_signal_beacon")) return 8;
    if (id == grandleon::core::stable_content_id_v1("ruins_watch_ruin")) return 14;
    if (id == grandleon::core::stable_content_id_v1("farmland_orchard_tree") ||
        id == grandleon::core::stable_content_id_v1("forest_orchard_tree")) return 22;
    if (id == grandleon::core::stable_content_id_v1("mountain_ancient_grove")) return 26;
    return -1;
}

void append_river_bridge(std::size_t index, int axis) {
    const int width = board.content->definition.width;
    const auto cx = world_x(static_cast<std::int16_t>(index % width), width);
    const auto cz = world_z(static_cast<std::int16_t>(index / width), board.content->definition.height);
    const auto plank = [&](int along, int across, int half_along, int half_across,
                           int bottom, int height, std::uint32_t colour) {
        append_box(cx + (axis == 1 ? along : across), bottom,
                   cz + (axis == 1 ? across : along),
                   axis == 1 ? half_along : half_across, height,
                   axis == 1 ? half_across : half_along, colour);
    };
    const int top = terrain_lift(board.content->terrain[index]);
    for (int step = 0; step < 4; ++step)
        plank(-20 + step * 13, 0, 6, 9, top - 3, 3,
              pack_rgba(139U + step * 3U, 96U + step * 2U, 52U));
    for (int side : {-1, 1})
        plank(0, side * 11, 27, 1, top, 5, pack_rgba(101U, 69U, 38U));
}

bool visibility_hidden(std::size_t index);

void build_landmark_vertices() {
    board.landmark_vertex_count = 0;
    board.settlement.begin();
    board.scenery_sprites.begin();
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    constexpr std::uint64_t castle_wall =
        grandleon::core::stable_content_id_v1("mountain_castle_wall");
    constexpr std::uint64_t castle_dais =
        grandleon::core::stable_content_id_v1("paved_castle_dais");
    board.keep.clear();
    board.modular_castle.begin();
    int min_x = width, max_x = -1, min_y = height, max_y = -1, walls = 0;
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        if (board.content->terrain[index] != castle_wall) continue;
        const int x = static_cast<int>(index % width);
        const int y = static_cast<int>(index / width);
        min_x = std::min(min_x, x); max_x = std::max(max_x, x);
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
        ++walls;
    }
    const bool modular = walls > 0 && !(walls == 18 && max_x - min_x == 5 && max_y - min_y == 5);
    if (walls > 0 && !modular) {
        assertf(board.keep.build(
            (world_x(min_x, width) + world_x(max_x, width)) / 2,
            (world_z(min_y, height) + world_z(max_y, height)) / 2, 13, 4),
            "Godot-derived stone keep failed to load");
    }
    std::array<bool, kMaxCells> landmark_cells_seen{};
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        const std::uint64_t terrain = board.content->terrain[index];
        const auto x = static_cast<std::int16_t>(index % width);
        const auto y = static_cast<std::int16_t>(index / width);
        const std::int16_t cx = world_x(x, width);
        const std::int16_t cz = world_z(y, height);
        const std::int16_t floor = terrain_lift(terrain);
        if (!board.keep.active() && visibility_hidden(index)) {
            const auto cover = terrain == castle_dais ||
                terrain == grandleon::core::stable_content_id_v1("paved_castle_floor")
                ? gp::settlement_art::model_castle_roof : gp::settlement_art::model_visual_cover;
            board.modular_castle.add(cover, index, cx, floor, cz, true);
        }
        if (const int crossing = river_crossing_axis(index)) {
            append_river_bridge(index, crossing);
            continue;
        }
        const int model = settlement_model(terrain);
        if (model >= 0) {
            const bool single = model >= 5 && model != 26;
            if (!landmark_cells_seen[index]) {
                if (!single) {
                    assertf(x+1 < width && y+1 < height && board.content->terrain[index+1U] == terrain && board.content->terrain[index+width] == terrain && board.content->terrain[index+width+1U] == terrain, "Settlement footprint is incomplete");
                }
                landmark_cells_seen[index] = true;
                if (model == 7) {
                    assertf(x+1 < width && board.content->terrain[index+1U] == terrain,
                            "Orchard bell requires two adjacent passable cells");
                    landmark_cells_seen[index+1U] = true;
                }
                if (!single) {
                    landmark_cells_seen[index+1U] = true;
                    landmark_cells_seen[index+width] = true;
                    landmark_cells_seen[index+width+1U] = true;
                }
                if (model == 26) {
                    assertf(board.scenery_sprites.add_grove(cx+kHalfCell,floor,cz+kHalfCell), "Godot ancient grove artwork could not be loaded");
                } else if (model == 0) {
                    assertf(board.scenery_sprites.add_house(x,y,cx+kHalfCell,floor,cz+kHalfCell,
                        static_cast<unsigned>(gp::scenery_style::house_family(terrain))), "Godot house artwork could not be loaded");
                } else {
                    int variant = model;
                    if (model == 5 && x > 0 && board.content->terrain[index-1U] == grandleon::core::stable_content_id_v1("mountain_watchtower")) variant = 6;
                    if (model == 8) variant += std::abs(x*17+y*31+terrain_kind(terrain)*13)%6;
                    if (model == 22) variant += std::abs(x*17+y*31+terrain_kind(terrain)*13)%4;
                    if (model == 14) {
                        const int edge = y == 3 ? 0 : y == 8 ? 1 : x == 5 ? 2 : 3;
                        variant += edge*2 + std::abs(x*17+y*31+8*13)%2;
                    }
                    board.settlement.add(static_cast<std::uint8_t>(variant),cx+(single ? 0 : kHalfCell),floor,cz+(single ? 0 : kHalfCell));
                }
            }
            continue;
        }
        if (modular && terrain == grandleon::core::stable_content_id_v1("paved_castle_door")) {
            if (y == 0 || board.content->terrain[index - width] != terrain)
                board.modular_castle.add(gp::settlement_art::model_castle_door, index, cx, floor, cz);
        }
        if (terrain == castle_wall) {
            if (modular) {
                const auto wall_at = [&](int nx, int ny) {
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) return false;
                    const auto id = board.content->terrain[ny * width + nx];
                    return id == castle_wall || id == grandleon::core::stable_content_id_v1("paved_castle_door");
                };
                const bool vertical = wall_at(x,y-1) || wall_at(x,y+1);
                const bool horizontal = wall_at(x-1,y) || wall_at(x+1,y);
                const auto model = vertical && horizontal ? gp::settlement_art::model_castle_wall_join :
                    vertical ? gp::settlement_art::model_castle_wall_vertical : gp::settlement_art::model_castle_wall_horizontal;
                board.modular_castle.add(model, index, cx, floor, cz);
            }
            continue; // Godot-derived masonry follows the authored footprint.
        } else if (terrain == castle_dais) {
            if (modular) {
                board.modular_castle.add(gp::settlement_art::model_castle_dais, index, cx, floor, cz);
                continue;
            }
            append_box(
                cx, floor, cz, kHalfCell - 4, 8, kHalfCell - 4,
                pack_rgba(133U, 75U, 57U)
            );

        }
    }
    assertf(board.settlement.finish(), "Settlement scenery exceeds available memory");
    assertf(board.modular_castle.finish(), "Modular castle exceeds available memory");
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
    if (board.keep.active() && board.visibility_revealed[first_hidden]) return;
    const int width = board.content->definition.width;
    for (const sim::UnitSnapshot& unit : board.game.snapshot().units) {
        if (!sim::on_board(unit) || unit.side != sim::Side::first) continue;
        const std::size_t origin = static_cast<std::size_t>(
            unit.position.y * width + unit.position.x
        );
        if (!visibility_hidden(origin)) continue;
        unsigned newly_revealed = 0U;
        for (const std::uint32_t candidate :
             board.content->visual_visibility.hidden_cells) {
            const int distance = std::abs(int(candidate % width) - int(unit.position.x)) +
                                 std::abs(int(candidate / width) - int(unit.position.y));
            if ((board.keep.active() || distance <= board.content->visual_visibility.reveal_radius) &&
                !board.visibility_revealed[candidate]) {
                board.visibility_revealed[candidate] = true;
                ++newly_revealed;
            }
        }
        if (!board.keep.active()) {
            if (newly_revealed) {
                const auto explored = std::count(board.visibility_revealed.begin(), board.visibility_revealed.end(), true);
                debugf("CHECKPOINT PYRITE_ROOM_REVEAL new=%u explored=%u total=%lu\n", newly_revealed,
                       unsigned(explored), static_cast<unsigned long>(board.content->visual_visibility.hidden_cells.size()));
            }
            continue;
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
    if (board.keep.active() || board.modular_castle.active()) return;
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
        assert(first + 4U <= grandleon_project_max_map_cells * kCellVertexCount);
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
    const std::int16_t lean = static_cast<std::int16_t>(static_cast<int>((variant >> 3U) % 7U) - 3);
    // Bake the season and canopy variation into the existing vertices so
    // the same forest uses the same geometry, texture slots and draw cost.
    const auto& bark = gp::scenery_style::tree_bark;
    const std::uint32_t bark_a = pack_rgba(bark[0], bark[1], bark[2]);
    const std::uint32_t bark_b = darken(bark_a, 187U);
    const auto& tone = gp::scenery_style::tree_foliage[(variant >> 8U) & 3U];
    const auto& tips = gp::scenery_style::tree_tips;
    const std::uint32_t leaf_a = pack_rgba(tone[0], tone[1], tone[2]);
    const std::uint32_t leaf_b = pack_rgba(tone[0] + tips[0], tone[1] + tips[1],
        tone[2] + tips[2]);
    const std::int16_t trunk = scaled(5, scale);
    const std::int16_t crown = scaled(18 + static_cast<int>((variant >> 12U) % 8U), scale);
    const std::int16_t depth = scaled(18 + static_cast<int>((variant >> 16U) % 8U), scale);
    const std::int16_t shoulder = scaled(38 + static_cast<int>((variant >> 20U) % 11U), scale);
    set_vertex(board.trees, first + 0U, cx - trunk, floor, cz - trunk, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 1U, cx + trunk, floor, cz - trunk, bark_b, 0, 0, normal);
    set_vertex(board.trees, first + 2U, cx + trunk, floor, cz + trunk, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 3U, cx - trunk, floor, cz + trunk, bark_b, 0, 0, normal);
    set_vertex(board.trees, first + 4U, cx + lean, floor + scaled(34, scale), cz, bark_a, 0, 0, normal);
    // Two overlapping crowns mirror the layered Godot conifers. Store the
    // sunlight and sheltered undersides in vertex colours, as a baked pass;
    // there is no per-frame lighting or additional texture allocation.
    const auto ring = [&](std::size_t base, std::int16_t radius_x,
                          std::int16_t radius_z, std::int16_t bottom,
                          std::int16_t top, std::uint32_t colour) {
        const std::int16_t xs[6] = {static_cast<std::int16_t>(-radius_x), static_cast<std::int16_t>(-radius_x / 2),
            static_cast<std::int16_t>(radius_x / 2), radius_x,
            static_cast<std::int16_t>(radius_x / 2), static_cast<std::int16_t>(-radius_x / 2)};
        const std::int16_t zs[6] = {0, static_cast<std::int16_t>(-radius_z),
            static_cast<std::int16_t>(-radius_z), 0, radius_z, radius_z};
        constexpr std::int16_t us[6] = {0, 8, 24, 31, 24, 8};
        constexpr std::int16_t vs[6] = {16, 0, 0, 16, 31, 31};
        const std::uint32_t shades[6] = {leaf_b, colour, darken(colour, 215U),
            darken(colour, 175U), darken(colour, 190U), colour};
        for (std::size_t vertex = 0; vertex < 6U; ++vertex) {
            set_vertex(board.trees, first + base + vertex, cx + xs[vertex],
                floor + bottom, cz + zs[vertex], shades[vertex],
                us[vertex] * 32, vs[vertex] * 32, normal);
        }
        set_vertex(board.trees, first + base + 6U, cx + lean, floor + top, cz,
            leaf_b, 16 * 32, 16 * 32, normal);
    };
    // Mix rounded deciduous crowns into the conifers without allocating a
    // second vertex arena. Two six-sided rings form an actual closed canopy;
    // the narrower upper ring and shaded underside give it a softer profile.
    const bool broadleaf = ((variant >> 24U) % 3U) == 0U;
    board.tree_broadleaf[board.tree_count] = broadleaf;
    if (broadleaf) {
        ring(5U, crown + scaled(5, scale), depth + scaled(3, scale),
             scaled(37, scale), scaled(70, scale), leaf_a);
        ring(12U, crown * 3 / 4, depth * 3 / 4, scaled(62, scale),
             scaled(76, scale), leaf_a);
        for (std::size_t vertex = 0; vertex < 6U; ++vertex) {
            auto* lower = t3d_vertbuffer_get_pos(board.trees,
                static_cast<int>(first + 5U + vertex));
            auto* upper = t3d_vertbuffer_get_pos(board.trees,
                static_cast<int>(first + 12U + vertex));
            const int uneven = static_cast<int>((variant >> (vertex * 3U)) & 3U) - 1;
            lower[1] += scaled(uneven, scale);
            upper[1] += scaled(uneven * 2, scale);
        }
    } else {
        ring(5U, crown, depth, scaled(28, scale), shoulder + scaled(23, scale), leaf_a);
        ring(12U, crown * 2 / 3, depth * 2 / 3, shoulder + scaled(2, scale),
             scaled(82, scale), leaf_a);
    }
    set_vertex(board.trees, first + 19U, cx, floor + scaled(24, scale), cz,
        darken(leaf_a, 130U), 16 * 32, 16 * 32, normal);
    ++board.tree_count;
}

// Restart each triangle so this compact DMA stream preserves the original
// primitive order and winding exactly.20 vertices leave ample DMEM space for
// at most84 indices; padding covers the final aligned DMA read.
alignas(16) std::int16_t tree_indices[2][88]{};
std::uint32_t tree_index_counts[2]{};
bool tree_indices_ready{};
void build_tree_indices() {
    if (tree_indices_ready) return;
    for (unsigned shape = 0; shape < 2U; ++shape) {
        auto* indices = tree_indices[shape];
        auto& count = tree_index_counts[shape];
        const auto triangle = [&](int a, int b, int c) {
            indices[count] = static_cast<std::int16_t>(a | (count ? 0x8000U : 0U));
            ++count;
            indices[count++] = static_cast<std::int16_t>(b);
            indices[count++] = static_cast<std::int16_t>(c);
        };
        triangle(0, 1, 4); triangle(1, 2, 4);
        triangle(2, 3, 4); triangle(3, 0, 4);
        for (std::uint8_t side = 0; side < 6U; ++side) {
            const std::uint8_t next = (side + 1U) % 6U;
            if (shape == 1U) {
                triangle(5 + side, 12 + side, 12 + next);
                triangle(5 + side, 12 + next, 5 + next);
            } else {
                triangle(11, 5 + side, 5 + next);
            }
            triangle(18, 12 + side, 12 + next);
            triangle(19, 5 + next, 5 + side);
        }
        assert(count == (shape == 1U ? 84U : 66U));
        t3d_indexbuffer_convert(indices, static_cast<int>(count));
    }
    data_cache_hit_writeback(tree_indices, sizeof(tree_indices));
    tree_indices_ready = true;
}

void build_tree_commands() {
    // Previous stage commands may still reference this block. Drain once at
    // the stage boundary, never during the normal animated drawing loop.
    rspq_wait();
    if (board.tree_commands) rspq_block_free(board.tree_commands);
    build_tree_indices();
    heap_stats_t before{}, after{};
    sys_get_heap_stats(&before);
    rspq_block_begin();
    for (std::size_t index = 0; index < board.tree_count; ++index) {
        t3d_vert_load(board.trees + index * (kTreeVertexCount / 2U), 0, kTreeVertexCount);
        const unsigned shape = board.tree_broadleaf[index] ? 1U : 0U;
        t3d_tri_draw_strip_and_sync(tree_indices[shape], tree_index_counts[shape]);
    }
    board.tree_commands = rspq_block_end();
    assert(board.tree_commands);
    sys_get_heap_stats(&after);
    board.tree_command_bytes = after.used - before.used;
    if (grandleon_project_review_driver) {
        debugf("GRANDLEON_N64_TREE_COMMANDS stage=%lu trees=%lu heap_bytes=%lu\n",
            static_cast<unsigned long>(shell.stage_index + 1U),
            static_cast<unsigned long>(board.tree_count),
            static_cast<unsigned long>(board.tree_command_bytes));
    }
}

void build_tree_vertices() {
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    board.tree_count = 0U;
    board.tree_sway.fill(0);
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        const auto id = board.content->terrain[index];
        const std::uint8_t kind = terrain_kind(id);
        if (kind != 2U || settlement_model(id) >= 0) continue;
        const auto x = static_cast<std::int16_t>(index % width);
        const auto z = static_cast<std::int16_t>(index / width);
        const auto variation = environment_hash(x, z);
        append_tree(
            world_x(x, width) + static_cast<std::int16_t>((index & 1U) == 0U ? -4 : 5),
            terrain_lift(id),
            world_z(z, height) + static_cast<std::int16_t>((index & 2U) == 0U ? -3 : 4),
            variation,
            92 + static_cast<int>((variation >> 4U) % 37U)
        );
        if (index % 3U == 0U) {
            append_tree(
                world_x(x, width) + 15,
                terrain_lift(id),
                world_z(z, height) - 13,
                variation ^ 0x9e3779b9U,
                62 + static_cast<int>((variation >> 17U) % 23U)
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
                terrain_lift(edge_terrain),
                world_z(static_cast<std::int16_t>(z), height) +
                    static_cast<std::int16_t>((hash >> 9U) % 17U) - 8,
                hash,
                60 + static_cast<int>((hash >> 14U) % 29U)
            );
        }
    }
    build_tree_commands();
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

view::BoardHeading& heading_for_unit(const sim::UnitSnapshot& unit) {
    const auto& units = board.game.snapshot().units;
    for (std::size_t index = 0; index < units.size(); ++index) {
        if (units[index].id == unit.id) return board.headings[index];
    }
    assertf(false, "Presented unit is absent from the authored encounter");
    return board.headings[0];
}

sprite_t* sprite_for_unit(
    const sim::UnitSnapshot& unit,
    VisualPose pose = VisualPose::idle
) {
    UnitArt* art = art_for_unit(unit);
    if (art == nullptr) return nullptr;
    if ((pose == VisualPose::idle || pose == VisualPose::moving) && art->directional[0]) {
        const auto heading = heading_for_unit(unit);
        if (heading == view::BoardHeading::north || heading == view::BoardHeading::south) {
            const std::size_t base = heading == view::BoardHeading::north ? 0U : 3U;
            const bool walking = motion.active && motion.unit == unit.id;
            const std::size_t cell = walking ? (pose == VisualPose::moving ? 2U : 1U) : 0U;
            return art->directional[base + cell];
        }
    }
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

const sim::UnitSnapshot& presented_unit(const sim::UnitSnapshot& unit) {
    if (combat.active && unit.id == combat.attacker) return combat.attacker_art;
    if (combat.active && unit.id == combat.defender) return combat.defender_art;
    return unit;
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
    for (const sim::UnitSnapshot& authoritative : board.game.snapshot().units) {
        const sim::UnitSnapshot& unit = presented_unit(authoritative);
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
        auto& facing = heading_for_unit(unit);
        bool facing_left = view::board_heading_faces_left(facing, unit.id == motion.unit && motion.facing_left);
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
                segment_to.x, segment_to.y, facing
            );
            facing = heading;
            motion.facing_left = view::board_heading_faces_left(
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
        const float yaw_x = current_camera.eye_z - current_camera.target_z;
        const float yaw_z = current_camera.target_x - current_camera.eye_x;
        const float yaw_length = std::max(0.01F, std::sqrt(yaw_x * yaw_x + yaw_z * yaw_z));
        const std::int16_t right_x = static_cast<std::int16_t>(24.0F * yaw_x / yaw_length);
        const std::int16_t right_z = static_cast<std::int16_t>(24.0F * yaw_z / yaw_length);
        const auto idle = view::idle_pose(
            frame_count / 2U, unit.id,
            !combat.reduced_motion && !combat.active &&
                !(motion.active && motion.unit == unit.id)
        );
        const std::int16_t sprite_height = static_cast<std::int16_t>(
            (72 * idle.stretch_q12 + 2048) / 4096
        );
        const std::int32_t lean = idle.lean_q12 * 72;
        const std::int16_t top_sway = static_cast<std::int16_t>(
            lean >= 0 ? (lean + 2048) / 4096 : -((-lean + 2048) / 4096)
        );
        const std::int16_t uv_end = static_cast<std::int16_t>(
            64 << 5
        );
        const std::int16_t bottom = static_cast<std::int16_t>(
            lift + 3 + walk_bob
        );
        const std::size_t first = board.unit_count * kUnitVertexCount;
        const std::uint32_t white = pack_rgba(255U, 255U, 255U);
        if (art_for_unit(unit)->directional[0] &&
            (facing == view::BoardHeading::north || facing == view::BoardHeading::south)) {
            facing_left = false;
        }
        const std::int16_t left_u = facing_left ? uv_end : 0;
        const std::int16_t right_u = facing_left ? 0 : uv_end;
        set_vertex(board.units, first + 0U, cx - right_x, bottom, cz - right_z, white, left_u, uv_end, normal);
        set_vertex(board.units, first + 1U, cx + right_x, bottom, cz + right_z, white, right_u, uv_end, normal);
        set_vertex(board.units, first + 2U, cx + right_x + top_sway, bottom + sprite_height, cz + right_z + top_sway, white, right_u, 0, normal);
        set_vertex(board.units, first + 3U, cx - right_x + top_sway, bottom + sprite_height, cz - right_z + top_sway, white, left_u, 0, normal);
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

// Movement highlights need space for the current legal range, not every cell
// in the campaign's largest map. Grow only when a larger range is selected.
void reserve_cursor_vertices(std::size_t required) {
    assert(required <= kCursorVertexCount);
    if (required <= board.cursor_vertex_capacity) return;
    const auto capacity = std::min(kCursorVertexCount, (required + 255U) / 256U * 256U);
    const auto bytes = sizeof(T3DVertPacked) * (capacity / 2U);
    auto* replacement = static_cast<T3DVertPacked*>(malloc_uncached(bytes));
    assertf(replacement, "Movement highlights exceed available Nintendo 64 memory");
    // Earlier command lists can still reference the old uncached arena.
    rspq_wait();
    if (board.cursor) free_uncached(board.cursor);
    board.cursor = replacement;
    board.cursor_vertex_capacity = capacity;
    debugf("GRANDLEON_N64_HIGHLIGHT_BUFFER vertices=%lu bytes=%lu\n",
           static_cast<unsigned long>(capacity), static_cast<unsigned long>(bytes));
}

void build_cursor_vertices() {
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    const sim::EncounterSnapshot& snapshot = board.game.snapshot();
    const std::uint8_t active = board.game.active_player();
    const sim::UnitId selected = board.game.selected(active);
    const std::vector<sim::Position> reach = !snapshot.deploying && selected != 0U
        ? sim::reachable_tiles(snapshot, selected) : std::vector<sim::Position>{};
    const auto& highlights = snapshot.deploying ? snapshot.deployment_tiles : reach;
    reserve_cursor_vertices(highlights.size() * kReachVerticesPerCell +
                            board.game.local_players() * kCursorVerticesPerPlayer);
    constexpr std::int16_t outer = kHalfCell - 2;
    constexpr std::int16_t inner = kHalfCell - 5;
    std::size_t first = 0U;
    const auto ring = [&](sim::Position tile, std::uint32_t colour, bool fill) {
        const std::size_t cell = static_cast<std::size_t>(tile.y) * width + static_cast<std::size_t>(tile.x);
        const std::int16_t floor = static_cast<std::int16_t>(
            (cell < board.content->terrain.size() ? terrain_lift(board.content->terrain[cell]) : 0) + 3);
        const std::int16_t cx = world_x(tile.x, width);
        const std::int16_t cz = world_z(tile.y, height);
        assert(first + (fill ? kReachVerticesPerCell : 16U) <= board.cursor_vertex_capacity);
        if (fill) {
            cursor_quad(first, cx - outer, cz - outer, cx + outer, cz + outer, floor,
                (colour & 0xffffff00U) | 38U);
            first += 4U;
        }
        cursor_quad(first, cx - outer, cz - outer, cx + outer, cz - inner, floor, colour);
        cursor_quad(first + 4U, cx - outer, cz + inner, cx + outer, cz + outer, floor, colour);
        cursor_quad(first + 8U, cx - outer, cz - inner, cx - inner, cz + inner, floor, colour);
        cursor_quad(first + 12U, cx + inner, cz - inner, cx + outer, cz + inner, floor, colour);
        first += 16U;
    };
    const std::uint32_t reach_colour = active == 0U
        ? pack_rgba(104U, 207U, 231U, 175U) : pack_rgba(235U, 147U, 184U, 175U);
    for (const sim::Position tile : highlights) {
        ring(tile, reach_colour, true);
    }
    for (std::uint8_t player = 0U; player < board.game.local_players(); ++player) {
        const sim::Position cursor = board.game.cursor(player);
        const bool targeting = battle_ui.player == player && (
            battle_ui.phase == BattleUiPhase::attack_target ||
            battle_ui.phase == BattleUiPhase::ability_target ||
            battle_ui.phase == BattleUiPhase::talk_target);
        const bool selected_unit = board.game.selected(player) != 0U;
        const std::uint32_t accent = player == 0U
            ? pack_rgba(104U, 207U, 231U) : pack_rgba(235U, 147U, 184U);
        ring(cursor, targeting ? pack_rgba(243U, 139U, 102U) :
            (selected_unit ? accent : pack_rgba(230U, 196U, 118U)), false);
        const sim::UnitId actor_id = selected_unit ? board.game.selected(player)
            : (battle_ui.player == player ? battle_ui.actor : 0U);
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.id == actor_id && sim::on_board(unit) && unit.position != cursor) {
                ring(unit.position, accent, false);
                break;
            }
        }
    }
    board.cursor_vertex_count = first;
}

// Reserve the largest reachable map while the heap is still contiguous.
// Every encounter reuses the same ground, elevation and roof buffers.
void allocate_terrain_vertices() {
    constexpr auto cells = grandleon_project_max_map_cells;
    static_assert(cells > 0U && cells <= kMaxCells);
    constexpr auto vertices_per_cell = kCellVertexCount * 2U + kElevationVerticesPerCell;
    constexpr auto bytes_per_cell = sizeof(T3DVertPacked) * vertices_per_cell / 2U;
    static_assert(bytes_per_cell == pr::target_budget::nintendo64_terrain_bytes_per_cell);
    const auto bytes = cells * bytes_per_cell;
    board.cells = static_cast<T3DVertPacked*>(malloc_uncached(bytes));
    assertf(board.cells, "Authored terrain exceeds available Nintendo 64 memory");
    board.elevation = board.cells + cells * kCellVertexCount / 2U;
    board.visibility = board.cells + cells * (kCellVertexCount + kElevationVerticesPerCell) / 2U;
    debugf("GRANDLEON_N64_TERRAIN_BUFFER cells=%lu bytes=%lu\n",
           static_cast<unsigned long>(cells), static_cast<unsigned long>(bytes));
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
    allocate_terrain_vertices();
    board.ground = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kGroundVertexCount / 2U)));
    board.apron = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxApronCells * kCellVertexCount / 2U)));
    board.terrain_forms = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) *
            (kMaxTerrainForms * kTerrainFormVertexCount / 2U)));
    board.trees = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxTrees * kTreeVertexCount / 2U)));
    board.landmark = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxLandmarkVertices / 2U)));
    board.units = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxUnits * kUnitVertexCount / 2U)));
    reserve_cursor_vertices(kCursorVerticesPerPlayer * 2U);
    assert(board.ground && board.apron && board.terrain_forms &&
        board.trees && board.landmark && board.units && board.cursor);
    board.terrain = sprite_load("rom:/material_atlas.sprite");
    board.surface_detail = sprite_load("rom:/surface_detail.sprite");
    // The title and first combat share the first authored plate. Keep just
    // one plate resident; entering another stage replaces it below.
    board.combat_backdrop = sprite_load(grandleon_project_backdrop_paths.front());
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
        if (grandleon_project_drawings[index].directional) {
            constexpr const char* poses[] = {"north_idle", "north_contact", "north_move", "south_idle", "south_contact", "south_move"};
            for (std::size_t pose = 0; pose < board.art[index].directional.size(); ++pose) {
                board.art[index].directional[pose] = load(poses[pose]);
                assertf(board.art[index].directional[pose], "Generated compass sprite is missing");
            }
        }
    }
    wav64_open(&board.ambience, "rom:/ambience.wav64");
    wav64_open(&board.ui_cue, "rom:/ui_cue.wav64");
    wav64_open(&board.impact_cue, "rom:/impact_cue.wav64");
    wav64_open(&board.victory_cue, "rom:/victory_cue.wav64");
    for (unsigned index = 0; index < board.music.size(); ++index) {
        std::snprintf(path, sizeof path, "rom:/music_%u.wav64", index + 1U);
        wav64_open(&board.music[index], path);
    }
    const char* cue_names[] = {"blade", "bow", "heavy", "arcane", "healing", "miss"};
    for (unsigned index = 0; index < board.combat_cues.size(); ++index) {
        std::snprintf(path, sizeof path, "rom:/%s_cue.wav64", cue_names[index]);
        wav64_open(&board.combat_cues[index], path);
    }
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
            battle_moment.text_offset = battle_moment.text_next = 0U;
            battle_moment.text_more = false;
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
    if (battle_moment.active) debugf("CHECKPOINT PYRITE_BATTLE_MOMENT_DONE\n");
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
    if (battle_moment.text_more) {
        battle_moment.text_offset = battle_moment.text_next;
        battle_moment.text_more = false;
        battle_moment.next_automatic_frame = frame_count + 75U;
        return true;
    }
    battle_moment.text_offset = battle_moment.text_next = 0U;
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
    board.headings.fill(view::BoardHeading::east);
    board.rowan_id = 0U;
    board.tomas_id = 0U;
    assertf(
        content.terrain.size() <= grandleon_project_max_map_cells &&
            content.definition.units.size() <= kMaxUnits,
        "Authored battle exceeds Pyrite base-memory board limits"
    );
    board.game = {};
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
    replay.user_took_over = !grandleon_project_review_driver || shell.user_took_over;
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
    conversation_menu = {};
    board.discovery = {};
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
    heap_stats_t board_heap{};
    sys_get_heap_stats(&board_heap);
    debugf("GRANDLEON_N64_BOARD_MEMORY cells=%lu used=%lu free=%lu rdram=%lu\n",
           static_cast<unsigned long>(content.terrain.size()),
           static_cast<unsigned long>(board_heap.used), static_cast<unsigned long>(board_heap.total - board_heap.used),
           static_cast<unsigned long>(get_memory_size()));
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
    shell.story_text_offset = shell.story_text_next = 0U;
    shell.story_text_more = false;
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

bool begin_shop(client::CompanyManagement company) {
    if (company.shop_offers.empty()) return false;
    shell.shop = std::move(company);
    shell.shop_ui.reset(shell.shop);
    shell.shop_catalog_top = 0U;
    shell.shop_focus_player = 0U;
    shell.phase = ShellPhase::shop;
    shell.next_automatic_frame = 0U;
    debugf("CHECKPOINT PYRITE_SHOP offers=%lu gold=%lu players=%u\n",
        static_cast<unsigned long>(shell.shop.shop_offers.size()),
        static_cast<unsigned long>(shell.shop.gold), shell.shop_ui.players());
    return true;
}

bool stand_on_campaign_encounter();

bool continue_from_shop() {
    if (!shell.shop_ui.all_ready()) return false;
    shell.shop_approved = true;
    shell.shop = {};
    return stand_on_campaign_encounter();
}

void shop_input(unsigned player, const joypad_buttons_t& pressed) {
    const bool physical = pressed.a || pressed.b || pressed.start || pressed.d_up ||
        pressed.d_down || pressed.d_left || pressed.d_right;
    if (!physical) return;
    shell.user_took_over = true;
    shell.shop_focus_player = player;
    if (pressed.start) shell.shop_ui.toggle_ready(shell.shop, player);
    else if (pressed.b) shell.shop_ui.cancel(player);
    else if (pressed.d_up || pressed.d_down) shell.shop_ui.move_offer(shell.shop, player, pressed.d_up ? -1 : 1);
    else if (pressed.d_left || pressed.d_right) shell.shop_ui.cycle_member(shell.shop, player, pressed.d_left ? -1 : 1);
    else if (pressed.a) {
        play_ui_cue();
        const auto intent = shell.shop_ui.confirm(shell.shop, player);
        if (intent.verb == client::ManagementVerb::buy) {
            const auto purchase = board.campaign->buy_shop_offer(intent.member, intent.shop_offer, intent.shop_player);
            shell.shop = board.campaign->management();
            shell.shop_ui.committed(shell.shop, player, purchase);
            debugf("CHECKPOINT PYRITE_SHOP_PURCHASE accepted=%u player=%u member=%llu offer=%llu gold=%lu error=%s\n",
                purchase ? 1U : 0U, intent.shop_player, intent.member.value, intent.shop_offer,
                static_cast<unsigned long>(shell.shop.gold), client::management_error_name(purchase.error).data());
        }
    }
}

void begin_performance_sample();

bool show_campaign_complete() {
    if (shell.resumed) {
        shell.stage_index = grandleon_project_encounter_count - 1U;
    }
    shell.phase = ShellPhase::complete;
    shell.campaign_advanced_checkpoint = true;
    shell.next_automatic_frame = 0U;
    begin_performance_sample();
    debugf(
        "CHECKPOINT PYRITE_CAMPAIGN_COMPLETE battles=%lu slot_bytes=%lu\n",
        static_cast<unsigned long>(shell.completed_battles),
        static_cast<unsigned long>(storage::used_bytes(*board.saves))
    );
    return true;
}

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
                if (!shell.shop_approved && !company.shop_offers.empty()) {
                    return begin_shop(company);
                }
                if (!shell.supplies_approved && company.has_shared_supplies) {
                    shell.supplies = {};
                    shell.supplies.refresh(company);
                    if (!shell.supplies.empty()) {
                        shell.phase = ShellPhase::supplies;
                        shell.next_automatic_frame = frame_count + 75U;
                        debugf("CHECKPOINT PYRITE_SUPPLIES\n");
                        return true;
                    }
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
            board.battle_binding = std::move(prepared.encounter.binding);
            configure_campaign_board(board.campaign_content);
            shell.deployment_approved = false;
            shell.shop_approved = false;
            shell.supplies_approved = false;
            shell.supplies = {};
            shell.phase = ShellPhase::battle;
            return true;
        }
        if (standing.kind == pr::CampaignNodeKind::terminal) {
            return standing.dialogue_ids.empty() ? show_campaign_complete() : begin_story(standing);
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
    if (shell.story_text_more) {
        shell.story_text_offset = shell.story_text_next;
        shell.story_text_more = false;
        shell.next_automatic_frame = frame_count + 75U;
        return true;
    }
    shell.story_text_offset = shell.story_text_next = 0U;
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
    if (board.campaign->standing().kind == pr::CampaignNodeKind::terminal) {
        shell.story_ids.clear();
        shell.story = {};
        return show_campaign_complete();
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
    options.starting_stat_seed = get_ticks();
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
    shell.completed_battles = board.campaign->completed_encounters();
    shell.shop_approved = false;
    shell.supplies_approved = false;
    shell.shop = {};

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
            if (shell.title_row == (game_languages().size() > 1U ? 3U : 2U)) {
                enter_shell_phase(ShellPhase::settings);
                return true;
            }
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
        case ShellPhase::settings:
            if (shell.audio_row == 2U && game_languages().size() > 1U) return cycle_game_language();
            adjust_volume(shell.audio_row, 1);
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
        case ShellPhase::shop:
            return false;
        case ShellPhase::supplies:
            shell.supplies_approved = true;
            return stand_on_campaign_encounter();
        case ShellPhase::deployment:
            if (shell.deployment.fielded.empty() ||
                (shell.deployment.capacity != 0U &&
                 shell.deployment.fielded.size() > shell.deployment.capacity)) return false;
            shell.deployment_approved = true;
            if (stand_on_campaign_encounter()) return true;
            shell.deployment_approved = false;
            return false;
        case ShellPhase::aftermath:
            if (shell.aftermath_reward_offset + 3U < shell.aftermath.progression.rewards.size()) {
                shell.aftermath_reward_offset += 3U;
                return true;
            }
            shell.aftermath_reward_offset = 0U;
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

joypad_buttons_t directional_input(joypad_port_t port, unsigned seat) {
    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);
    const joypad_inputs_t input = joypad_get_inputs(port);
    static std::array<int, 2> previous_direction{};
    static std::array<std::uint32_t, 2> next_repeat{};
    const int x = input.stick_x;
    const int y = input.stick_y;
    const int direction = std::abs(x) < 32 && std::abs(y) < 32 ? 0
        : std::abs(x) > std::abs(y) ? (x < 0 ? 1 : 2) : (y > 0 ? 3 : 4);
    if (direction != 0 && (direction != previous_direction[seat] || frame_count >= next_repeat[seat])) {
        pressed.d_left |= direction == 1;
        pressed.d_right |= direction == 2;
        pressed.d_up |= direction == 3;
        pressed.d_down |= direction == 4;
        next_repeat[seat] = frame_count + (direction != previous_direction[seat] ? 16U : 8U);
    }
    previous_direction[seat] = direction;
    return pressed;
}

void update_shell_input() {
    if (shell.phase == ShellPhase::battle) {
        return;
    }
    const joypad_buttons_t pressed = directional_input(JOYPAD_PORT_1, 0U);
    shell.second_connected = joypad_is_connected(JOYPAD_PORT_2);
    const bool physical = pressed.a || pressed.start || pressed.d_up ||
        pressed.d_down || pressed.d_left || pressed.d_right || pressed.b ||
        pressed.c_up;
    if (physical) shell.user_took_over = true;
    if (shell.phase == ShellPhase::supplies) {
        const auto controls = shell.local_players == 2U
            ? pressed.raw | directional_input(JOYPAD_PORT_2, 1U).raw : pressed.raw;
        const joypad_buttons_t input{.raw = static_cast<std::uint16_t>(controls)};
        if (input.start || (grandleon_project_review_driver &&
            !shell.user_took_over && frame_count >= shell.next_automatic_frame)) {
            (void)confirm_shell_choice();
            return;
        }
        if (input.d_left || input.d_right) shell.supplies.move_member(input.d_left ? -1 : 1);
        if (input.d_up || input.d_down) shell.supplies.move_row(input.d_up ? -1 : 1);
        const auto& rows = shell.supplies.rows();
        if (rows.empty() || (!input.a && !input.b)) return;
        const auto& row = rows[shell.supplies.selected()];
        const auto member = shell.supplies.member().member;
        if ((input.a && row.stored == 0) || (!input.a && row.held == 0)) return;
        const auto committed = input.a ? board.campaign->give_item(member, row.item)
                                       : board.campaign->take_item(member, row.item);
        if (committed && committed.save == storage::StorageError::none) {
            play_ui_cue();
            debugf("CHECKPOINT PYRITE_SUPPLY_TRANSFER give=%u member=%llu item=%llu\n",
                input.a ? 1U : 0U, member.value, row.item.stable_id);
        }
        shell.supplies.refresh(board.campaign->management());
        return;
    }
    if (shell.phase == ShellPhase::shop) {
        shop_input(0U, pressed);
        if (shell.shop_ui.players() > 1U) shop_input(1U, directional_input(JOYPAD_PORT_2, 1U));
        if (grandleon_project_review_driver && !shell.user_took_over) {
            for (unsigned player = 0; player < shell.shop_ui.players(); ++player) {
                if (!shell.shop_ui.state(player).ready) shell.shop_ui.toggle_ready(shell.shop, player);
            }
        }
        if (shell.shop_ui.all_ready()) (void)continue_from_shop();
        return;
    }
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
        const std::uint8_t rows = game_languages().size() > 1U ? 4U : 3U;
        if (pressed.d_up || pressed.d_left) {
            shell.title_row = shell.title_row == 0U
                ? static_cast<std::uint8_t>(rows - 1U)
                : static_cast<std::uint8_t>(shell.title_row - 1U);
        } else {
            shell.title_row = static_cast<std::uint8_t>(
                (shell.title_row + 1U) % rows
            );
        }
    } else if (shell.phase == ShellPhase::settings) {
        if (pressed.b || pressed.start) enter_shell_phase(ShellPhase::title);
        else if (pressed.d_up || pressed.d_down) {
            shell.audio_row = static_cast<std::uint8_t>((shell.audio_row + (pressed.d_up ? (game_languages().size() > 1U ? 2U : 1U) : 1U)) % (game_languages().size() > 1U ? 3U : 2U));
        }
        else if (pressed.d_left || pressed.d_right) {
            if (shell.audio_row == 2U) (void)cycle_game_language();
            else adjust_volume(shell.audio_row, pressed.d_left ? -1 : 1);
        } else if (pressed.a) {
            if (shell.audio_row == 2U) (void)cycle_game_language();
            else adjust_volume(shell.audio_row, 1);
        }
        return;
    } else if (shell.phase == ShellPhase::complete && (pressed.a || pressed.b)) {
        performance_sample_started = false;
        enter_shell_phase(ShellPhase::title);
        return;
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
    if (grandleon_project_review_driver &&
        shell.phase != ShellPhase::complete && !shell.user_took_over &&
        frame_count >= shell.next_automatic_frame) {
        (void)confirm_shell_choice();
    }
}

// Animate only the existing water UVs, after SwapChain has retired the
// previous draw pass. The padded material window has a full texel of guard;
// offsets stay within 3/8 texel horizontally and 1/4 vertically, preserving
// bilinear filtering without sampling a neighbouring atlas material.
void animate_water_vertices() {
    if (board.water_quads.empty()) return;
    const auto s = static_cast<std::int16_t>(combat.reduced_motion ? 0 :
        view::ambient_wave_q12(frame_count / 2U, 17U) * 12 / 4096);
    const auto t = static_cast<std::int16_t>(combat.reduced_motion ? 0 :
        view::ambient_wave_q12(frame_count / 2U, 49U) * 8 / 4096);
    const int ds = s - board.water_s, dt = t - board.water_t;
    if (ds == 0 && dt == 0) return;
    for (const auto quad : board.water_quads) {
        auto* arena = (quad & 0x8000U) ? board.apron : board.cells;
        const unsigned first = (quad & 0x7fffU) * 4U;
        const auto* uv0 = t3d_vertbuffer_get_uv(arena, first);
        const auto* uv1 = t3d_vertbuffer_get_uv(arena, first + 1U);
        const auto* uv3 = t3d_vertbuffer_get_uv(arena, first + 3U);
        // Material flips must not reverse the water's world-space motion.
        const int sign_s = uv1[0] > uv0[0] ? 1 : -1;
        const int sign_t = uv3[1] > uv0[1] ? 1 : -1;
        for (unsigned vertex = 0; vertex < 4U; ++vertex) {
            auto* uv = t3d_vertbuffer_get_uv(arena, first + vertex);
            uv[0] += ds * sign_s;
            uv[1] += dt * sign_t;
        }
    }
    board.water_s = s;
    board.water_t = t;
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
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_sprite_upload(TILE0, board.terrain, nullptr);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH));
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
    use_surface_detail();
    // Each shape uses eight vertices. Load a batch together so the RSP
    // can emit the same faces with one vertex load and sync per 64 vertices.
    constexpr std::size_t forms_per_batch = 64U / kTerrainFormVertexCount;
    constexpr auto crossed = static_cast<std::uint8_t>(TerrainFormShape::crossed);
    for (std::size_t begin = 0; begin < board.terrain_form_count; begin += forms_per_batch) {
        const std::size_t count = std::min(forms_per_batch, board.terrain_form_count - begin);
        t3d_vert_load(
            board.terrain_forms + begin * (kTerrainFormVertexCount / 2U),
            0, static_cast<std::uint32_t>(count * kTerrainFormVertexCount)
        );
        for (std::size_t local = 0; local < count; ++local) {
            const auto first = static_cast<std::uint8_t>(local * kTerrainFormVertexCount);
            if (board.terrain_form_shapes[begin + local] == crossed) {
                t3d_tri_draw(first + 0U, first + 1U, first + 4U);
                t3d_tri_draw(first + 0U, first + 4U, first + 5U);
                t3d_tri_draw(first + 4U, first + 1U, first + 0U);
                t3d_tri_draw(first + 5U, first + 4U, first + 0U);
                t3d_tri_draw(first + 2U, first + 3U, first + 6U);
                t3d_tri_draw(first + 2U, first + 6U, first + 7U);
                t3d_tri_draw(first + 6U, first + 3U, first + 2U);
                t3d_tri_draw(first + 7U, first + 6U, first + 2U);
            } else {
                draw_box(first);
            }
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
        // Move the existing crown, keeping its trunk and footprint fixed.
        // Store only the previous offset, rather than another vertex buffer.
        const auto breeze = combat.reduced_motion ? 0 :
            view::ambient_wave_q12(frame_count / 2U,
                static_cast<std::uint32_t>(index * 37U));
        const int amplitude = 2 + static_cast<int>(index % 3U);
        const auto sway = static_cast<std::int8_t>(breeze * amplitude / 4096);
        const int shift = sway - board.tree_sway[index];
        if (shift != 0) {
            for (std::size_t crown = 5U; crown < kTreeVertexCount; ++crown) {
                auto* position = t3d_vertbuffer_get_pos(board.trees,
                    static_cast<int>(index * kTreeVertexCount + crown));
                // The apex catches more wind; the lower crown stays rooted.
                position[0] += (crown >= 12U && crown <= 18U) ? shift * 2 : shift;
            }
            board.tree_sway[index] = sway;
        }

    }
    if (board.tree_commands) rspq_block_run(board.tree_commands);
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
    for (const sim::UnitSnapshot& authoritative : board.game.snapshot().units) {
        const sim::UnitSnapshot& unit = presented_unit(authoritative);
        if (!sim::on_board(unit)) continue;
        // The authored 84px close poses are composed in screen space because
        // they exceed one CI4 TMEM load. Keep board sprites resident beneath
        // the eight-frame stage crossfade.
        const VisualPose board_pose = close_combat_phase()
            ? VisualPose::idle : pose_for_unit(unit);
        sprite_t* sprite = sprite_for_unit(unit, board_pose);
        if (sprite == nullptr || visual_index >= board.unit_count) continue;
        const auto heading = heading_for_unit(unit);
        if (grandleon_project_review_driver && motion.active && motion.unit == unit.id &&
            art_for_unit(unit)->directional[0] &&
            (heading == view::BoardHeading::north || heading == view::BoardHeading::south)) {
            const bool north = heading == view::BoardHeading::north;
            const bool passing = board_pose == VisualPose::moving;
            const std::size_t checkpoint = (north ? 0U : 2U) + (passing ? 1U : 0U);
            if (!board.directional_checkpoints[checkpoint]) {
                board.directional_checkpoints[checkpoint] = true;
                debugf("CHECKPOINT PYRITE_WALK_%s_%s\n", north ? "NORTH" : "SOUTH", passing ? "PASS" : "CONTACT");
            }
        }
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
        rdpq_mode_zbuf(true, false);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
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
    const float render_scale = P64::Renderer::getRenderScale();
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
    if (battle_ui.phase == BattleUiPhase::result &&
        battle_ui.result_error == sim::CommandError::none) {
        battle_ui = {};
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
    combat.attacker_art = attacker_before;
    combat.defender_art = defender_before;
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
    combat.first_sound = combat.counter_sound = false;
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
            if (combat.phase_ticks >= 16U && !combat.first_sound) {
                play_effect(&board.combat_cues[combat.first_cue]);
                combat.first_sound = true;
            }
            if (combat.phase_ticks >= kCloseBlendFrames + 4U &&
                !combat.close_checkpoint) {
                debugf("CHECKPOINT PYRITE_COMBAT_CLOSE\n");
                combat.close_checkpoint = true;
            }
            if (combat.phase_ticks >= 32U && !combat.strike_checkpoint) {
                if (combat.first_missed) play_effect(&board.combat_cues[5]);
                else play_impact_cue();
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
            if (combat.phase_ticks >= 16U && !combat.counter_sound) {
                play_effect(&board.combat_cues[combat.counter_cue]);
                combat.counter_sound = true;
            }
            if (combat.phase_ticks >= 32U && !combat.counter_checkpoint) {
                if (combat.counter_missed) play_effect(&board.combat_cues[5]);
                else play_impact_cue();
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
    for (const auto& event : transition.events) {
        if (event.type != sim::EventType::hidden_item_found) continue;
        board.discovery = event;
        board.discovery_frame = frame_count;
        debugf("GRANDLEON_PYRITE_HIDDEN_ITEM x=%d y=%d on_ground=%u\n",
            event.position.x, event.position.y, event.ground_item_id != 0U ? 1U : 0U);
    }
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
    if (unit == nullptr || !sim::on_board(*unit) ||
        (!board.game.snapshot().collecting_loot && unit->has_acted) ||
        unit->local_player != player + 1U) return;
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
    pack, pack_inventory, pack_ground, pack_entry, pack_loot,
    pack_pickup, pack_drop, pack_exchange, pack_next, pack_back,
};

struct ActionChoice final {
    sim::ContentId content{};
    grandleon::core::ContentCategory category{grandleon::core::ContentCategory::item};
    std::uint16_t quantity{};
    ActionChoiceKind kind{ActionChoiceKind::cancel};
};
static_assert(sizeof(ActionChoice) == 16U, "Keep the console action catalog compact");

struct ActionMenu final {
    std::array<ActionChoice, 32> choices{};
    std::size_t count{};

    void append(ActionChoiceKind kind, sim::ContentId content = 0U,
                grandleon::core::ContentCategory category = grandleon::core::ContentCategory::item,
                std::uint16_t quantity = 0U) {
        if (count < choices.size()) choices[count++] = {content, category, quantity, kind};
    }
};

console::Message pack_choice_message(ActionChoiceKind kind) {
    switch (kind) {
        case ActionChoiceKind::pack: return console::Message::pack;
        case ActionChoiceKind::pack_inventory: return console::Message::inventory;
        case ActionChoiceKind::pack_ground: case ActionChoiceKind::pack_pickup: return console::Message::pick_up;
        case ActionChoiceKind::pack_drop: return console::Message::drop;
        case ActionChoiceKind::pack_exchange: return console::Message::exchange;
        case ActionChoiceKind::pack_next: return console::Message::next;
        case ActionChoiceKind::pack_back: return console::Message::back_button;
        default: return console::Message::leave;
    }
}

ActionMenu pack_menu() {
    ActionMenu menu;
    const auto* actor = find_unit(battle_ui.actor);
    if (actor == nullptr || actor->side != sim::Side::first || actor->local_player != battle_ui.player + 1U) return menu;
    const auto& snapshot = board.game.snapshot();
    const auto here = [&](const sim::GroundItem& pile) { return pile.position == actor->position && pile.stack.quantity != 0U; };
    if (battle_ui.pack_view == PackView::hub) {
        menu.append(ActionChoiceKind::pack_inventory);
        if (std::any_of(snapshot.ground_items.begin(), snapshot.ground_items.end(), here)) menu.append(ActionChoiceKind::pack_ground);
    } else if (battle_ui.pack_view == PackView::entry) {
        if (battle_ui.carried_category == grandleon::core::ContentCategory::item && !snapshot.collecting_loot) {
            const auto forecast = sim::forecast_item(snapshot, actor->id, actor->id, board.game.encounter().items(), battle_ui.carried_content);
            if (forecast && (forecast.kind != sim::ItemKind::restore || forecast.restored > 0)) menu.append(ActionChoiceKind::item, battle_ui.carried_content);
        }
        menu.append(ActionChoiceKind::pack_drop, battle_ui.carried_content, battle_ui.carried_category);
    } else if (battle_ui.pack_view == PackView::loot) {
        const auto pile = std::find_if(snapshot.ground_items.begin(), snapshot.ground_items.end(), [&](const auto& entry) { return entry.id == battle_ui.ground_item && here(entry); });
        if (pile != snapshot.ground_items.end()) {
            if (!actor->states_carry_capacity || sim::carried_count(actor->carried_inventory) < actor->carry_capacity) menu.append(ActionChoiceKind::pack_pickup);
            else if (!actor->carried_inventory.empty()) menu.append(ActionChoiceKind::pack_exchange);
        }
        menu.append(ActionChoiceKind::pack_back);
        return menu;
    } else {
        constexpr std::size_t page_size = 6U;
        const bool piles = battle_ui.pack_view == PackView::ground;
        const auto total = piles ? static_cast<std::size_t>(std::count_if(snapshot.ground_items.begin(), snapshot.ground_items.end(), here)) : actor->carried_inventory.size();
        if (static_cast<std::size_t>(battle_ui.pack_page) * page_size >= total) battle_ui.pack_page = 0U;
        const std::size_t begin = battle_ui.pack_page * page_size;
        for (std::size_t i = begin; i < total && i < begin + page_size; ++i) {
            if (piles) {
                std::size_t ordinal = 0;
                for (const auto& pile : snapshot.ground_items) {
                    if (!here(pile)) continue;
                    if (ordinal++ == i) { menu.append(ActionChoiceKind::pack_loot, pile.id, pile.stack.category, pile.stack.quantity); break; }
                }
            } else {
                const auto& stack = actor->carried_inventory[i];
                menu.append(ActionChoiceKind::pack_entry, stack.content_id, stack.category, stack.quantity);
            }
        }
        if (total > page_size) menu.append(ActionChoiceKind::pack_next);
    }
    menu.append(ActionChoiceKind::pack_back);
    return menu;
}


ActionMenu action_menu() {
    if (battle_ui.phase == BattleUiPhase::pack) return pack_menu();
    ActionMenu menu;
    const sim::UnitSnapshot* actor = find_unit(battle_ui.actor);
    if (actor == nullptr) return menu;
    if (board.game.snapshot().inventory_mode == sim::InventoryMode::carried) menu.append(ActionChoiceKind::pack);
    if (board.game.snapshot().collecting_loot) { menu.append(ActionChoiceKind::cancel); return menu; }
    const bool named_weapons = actor->weapon_ids.size() > 1U;
    const std::size_t weapon_count = named_weapons ? actor->weapon_ids.size() : 1U;
    for (std::size_t index = 0; index < weapon_count; ++index) {
        const sim::ContentId weapon = named_weapons ? actor->weapon_ids[index] : 0U;
        for (const sim::UnitSnapshot& target : board.game.snapshot().units) {
            if (sim::forecast_attack(board.game.snapshot(), actor->id, target.id,
                    board.game.encounter().weapons(), weapon)) {
                menu.append(ActionChoiceKind::attack, weapon);
                break;
            }
        }
    }
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
    if (actor != nullptr && actor->side == sim::Side::first &&
        actor->local_player != battle_ui.player + 1U) return false;
    const bool accepted = board.game.apply(command, battle_ui.player);
    battle_ui.phase = BattleUiPhase::result;
    battle_ui.result_command = command.type;
    battle_ui.result_error = board.game.transition().error;
    battle_ui.result_amount = 0;
    battle_ui.result_missed = false;
    battle_ui.result_restored = false;
    battle_ui.result_until = frame_count + 75U;
    if (!accepted) return false;
    unsigned cue = actor_before.minimum_reach > 1U ? 1U : 0U;
    if (command.type == sim::CommandType::attack ||
        command.type == sim::CommandType::ability) {
        if (command.type == sim::CommandType::ability) {
            cue = 2U;
            for (const auto& ability : board.game.encounter().abilities()) {
                if (ability.id != command.ability_id) continue;
                cue = ability.kind == sim::AbilityKind::restore ? 4U
                    : ability.damage_type == sim::DamageType::magical ? 3U : 2U;
                break;
            }
        }
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
        combat.first_cue = cue;
        combat.counter_cue = target_before.minimum_reach > 1U ? 1U : 0U;
    } else if (command.type == sim::CommandType::attack || command.type == sim::CommandType::ability) {
        play_effect(&board.combat_cues[cue]);
        if (!battle_ui.result_restored) {
            if (battle_ui.result_missed) play_effect(&board.combat_cues[5]);
            else play_impact_cue();
        }
    }
    if (command.type == sim::CommandType::attack && !combat.active) battle_ui = {};
    build_cursor_vertices();
    return true;
}

void back_pack_menu() {
    switch (battle_ui.pack_view) {
        case PackView::hub: battle_ui.phase = BattleUiPhase::action; break;
        case PackView::entry: battle_ui.pack_view = PackView::inventory; break;
        case PackView::loot: battle_ui.pack_view = PackView::ground; battle_ui.pack_page = 0; break;
        case PackView::exchange: battle_ui.pack_view = PackView::loot; battle_ui.pack_page = 0; break;
        default: battle_ui.pack_view = PackView::hub; battle_ui.pack_page = 0; break;
    }
    battle_ui.row = 0U;
}

bool confirm_pack_choice() {
    const auto* actor = find_unit(battle_ui.actor);
    if (actor == nullptr || actor->side != sim::Side::first || actor->local_player != battle_ui.player + 1U) return false;
    const auto choice = action_choice();
    switch (choice.kind) {
        case ActionChoiceKind::pack_inventory: battle_ui.pack_view = PackView::inventory; battle_ui.pack_page = 0; break;
        case ActionChoiceKind::pack_ground: battle_ui.pack_view = PackView::ground; battle_ui.pack_page = 0; break;
        case ActionChoiceKind::pack_loot: battle_ui.ground_item = choice.content; battle_ui.pack_view = PackView::loot; battle_ui.row = static_cast<std::uint8_t>(pack_menu().count - 1U); return true;
        case ActionChoiceKind::pack_exchange: battle_ui.pack_view = PackView::exchange; battle_ui.pack_page = 0; battle_ui.row = static_cast<std::uint8_t>(pack_menu().count - 1U); return true;
        case ActionChoiceKind::pack_next: ++battle_ui.pack_page; break;
        case ActionChoiceKind::pack_back: back_pack_menu(); return true;
        case ActionChoiceKind::pack_entry:
            if (battle_ui.pack_view != PackView::exchange) {
                battle_ui.carried_content = choice.content; battle_ui.carried_category = choice.category; battle_ui.pack_view = PackView::entry; battle_ui.row = static_cast<std::uint8_t>(pack_menu().count - 1U); return true;
            }
            [[fallthrough]];
        case ActionChoiceKind::pack_pickup:
        case ActionChoiceKind::pack_drop:
        case ActionChoiceKind::item: {
            sim::Command command{};
            command.type = choice.kind == ActionChoiceKind::pack_pickup ? sim::CommandType::pick_up :
                choice.kind == ActionChoiceKind::pack_drop ? sim::CommandType::drop_carried :
                choice.kind == ActionChoiceKind::item ? sim::CommandType::use_item : sim::CommandType::exchange_carried;
            command.unit_id = actor->id; command.target_id = actor->id;
            command.ground_item_id = battle_ui.ground_item;
            command.carried_category = choice.category; command.carried_content_id = choice.content;
            command.item_id = choice.content;
            const bool accepted = apply_battle_command(command, false);
            if (accepted) { battle_ui.phase = BattleUiPhase::pack; battle_ui.pack_view = PackView::hub; battle_ui.pack_page = 0; battle_ui.row = 0; }
            return accepted;
        }
        default: return false;
    }
    battle_ui.row = 0U;
    return true;
}


bool confirm_action_choice() {
    if (battle_ui.phase == BattleUiPhase::pack) return confirm_pack_choice();
    const sim::UnitSnapshot* actor = find_unit(battle_ui.actor);
    if (actor == nullptr) {
        close_action_menu();
        return false;
    }
    const ActionChoice choice = action_choice();
    switch (choice.kind) {
        case ActionChoiceKind::pack:
            battle_ui.phase = BattleUiPhase::pack; battle_ui.pack_view = PackView::hub; battle_ui.pack_page = 0; battle_ui.row = 0;
            return true;
        case ActionChoiceKind::attack:
            battle_ui.weapon = choice.content;
            for (const sim::UnitSnapshot& target : board.game.snapshot().units) {
                if (!sim::forecast_attack(board.game.snapshot(), actor->id, target.id,
                        board.game.encounter().weapons(), battle_ui.weapon)) continue;
                // Cursor focus is presentation state; use directional gestures
                // to preserve the gameplay boundary and multiplayer ownership.
                for (unsigned step = 0; step < kMaxCells; ++step) {
                    const sim::Position cursor = board.game.cursor(battle_ui.player);
                    if (cursor == target.position) break;
                    const auto direction = cursor.x < target.position.x ? gp::GameplaySlice::Action::right
                        : cursor.x > target.position.x ? gp::GameplaySlice::Action::left
                        : cursor.y < target.position.y ? gp::GameplaySlice::Action::down
                        : gp::GameplaySlice::Action::up;
                    (void)issue(direction, battle_ui.player);
                }
                break;
            }
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
        default: return false;
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
            board.game.snapshot(), actor->id, target->id,
            board.game.encounter().weapons(), battle_ui.weapon
        );
        if (!forecast) return false;
        return apply_battle_command({
            sim::CommandType::attack, actor->id, {}, target->id,
            0U, battle_ui.weapon, 0U,
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
        auto offer = pr::localized_map_conversation(board.runtime.package(), target->id, game_language());
        if (!offer.questions.empty()) {
            const auto& package = board.runtime.package();
            const auto placement_name = pr::content_name(package, package_format::SectionType::placement_names, target->id);
            const auto type_name = pr::content_name(package, package_format::SectionType::unit_types, target->unit_type_id);
            conversation_speaker = placement_name.empty()
                ? pr::localized_content_name(package, game_language(), package_format::SectionType::unit_types, target->unit_type_id, type_name)
                : pr::localized_placement_name(package, game_language(), target->id, placement_name);
            conversation_target = target->id;
            conversation_menu.start(std::move(offer));
            debugf("PYRITE_CONVERSATION_QUESTION id=%u player=%u\n",
                conversation_menu.question_id(), battle_ui.player + 1U);
            return true;
        }
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
    if (board.campaign) {
        for (const auto& stack : board.campaign->store()) {
            if (stack.item.category == grandleon::core::ContentCategory::currency &&
                stack.item.stable_id == grandleon::core::gold_currency_id) {
                battle_ui.wallet += stack.quantity;
            }
        }
    }
    // Current-board earnings enter the saved store only when the battle is
    // settled; this event list is cleared before the next board is configured.
    for (const sim::Event& event : board.battle_events) {
        if (event.type == sim::EventType::gold_gained && event.amount > 0 &&
            event.content_category == grandleon::core::ContentCategory::currency &&
            event.content_id == grandleon::core::gold_currency_id &&
            board.battle_binding.persistent_of({event.related_unit_id}).value != 0U) {
            battle_ui.wallet += static_cast<unsigned>(event.amount);
        }
    }
}

bool confirm_system_choice() {
    play_ui_cue();
    switch (battle_ui.row) {
        case 0U:
            if (board.game.snapshot().collecting_loot) {
                sim::Command done{}; done.type = sim::CommandType::finish_collection;
                if (!board.game.apply(done, battle_ui.player)) return false;
                remember_transition_events();
            }
            battle_ui = {};
            return true;
        case 1U:
            combat.enabled = !combat.enabled;
            save_user_settings();
            return true;
        case 2U:
            adjust_volume(0U, 1);
            return true;
        case 3U:
            debug_hud = !debug_hud;
            save_user_settings();
            return true;
        case 4U:
            if (game_languages().size() > 1U) (void)cycle_game_language();
            else adjust_volume(1U, 1);
            return true;
        case 5U:
            if (game_languages().size() > 1U) adjust_volume(1U, 1);
            else battle_ui.phase = BattleUiPhase::help;
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
    if (conversation_menu.active()) {
        if (player != battle_ui.player) return false;
        if (pressed.b) {
            conversation_menu = {};
            battle_ui.phase = BattleUiPhase::action;
            return true;
        }
        if (pressed.d_up || pressed.d_down) {
            conversation_menu.move(pressed.d_up ? -1 : 1);
            return true;
        }
        if (!pressed.a) return false;
        const auto previous = conversation_menu.question_id();
        const auto reply = conversation_menu.confirm();
        if (reply == 0) {
            if (conversation_menu.question_id() != previous)
                debugf("PYRITE_CONVERSATION_QUESTION id=%u player=%u\n",
                    conversation_menu.question_id(), battle_ui.player + 1U);
            return true;
        }
        sim::Command command{};
        command.type = sim::CommandType::talk;
        command.unit_id = battle_ui.actor;
        command.target_id = conversation_target;
        command.ability_id = reply;
        conversation_menu = {};
        if (apply_battle_command(command, false))
            debugf("PYRITE_CONVERSATION_REPLY id=%u\n", reply);
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
        return pressed.b || pressed.r;
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
        if (!pressed.d_up && !pressed.d_down && !pressed.d_left && !pressed.d_right && !pressed.a && !pressed.b &&
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
        } else if ((pressed.d_left || pressed.d_right) &&
                   (battle_ui.row == 2U || battle_ui.row == (game_languages().size() > 1U ? 5U : 4U))) {
            adjust_volume(battle_ui.row == 2U ? 0U : 1U, pressed.d_left ? -1 : 1);
        } else if (pressed.b || pressed.start) {
            battle_ui = {};
        } else if (pressed.a) {
            return confirm_system_choice();
        }
        return true;
    }
    if (battle_ui.phase == BattleUiPhase::action || battle_ui.phase == BattleUiPhase::pack) {
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
            if (battle_ui.phase == BattleUiPhase::pack) back_pack_menu();
            else close_action_menu();
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
        const joypad_buttons_t pressed = directional_input(ports[player], player);
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
        if (command.type == sim::CommandType::attack || command.type == sim::CommandType::ability) {
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
            }
            // The bounded review driver proves one complete optional handoff;
            // physical player commands keep the default and present every
            // attack while the setting is enabled.
            accepted = apply_battle_command(
                command, !grandleon_project_review_driver || combat.exchanges == 0U
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
    if (snapshot.outcome == sim::Outcome::ongoing || snapshot.collecting_loot) {
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
    board_timing_report();
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
        if (snapshot.collecting_loot) return;
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
    assert(board.saves->contains(campaign_slot()));
    assert(!combat.active);
    // These describe the authored review route, not every legal player route:
    // a short victory may precede the enemy turn, and optional rooms or campaign
    // branches need not all be visited before a terminal node.
    if (grandleon_project_review_driver) {
        assert(shell.completed_battles == board.campaign->completed_encounters());
        assert(shell.completed_battles <= grandleon_project_encounter_count);
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
    }
    if (grandleon_project_review_driver) {
        assertf(
            static_cast<unsigned>(get_memory_size()) <= pr::target_budget::nintendo64_main_ram_bytes,
            "Pyrite campaign parity requires more than base N64 memory"
        );
    }
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
    const auto review_focus = [&]() -> sim::Position {
        const int width = board.content->definition.width;
        int left = width, right = 0, top = board.content->definition.height, bottom = 0;
        for (const auto cell : covered) {
            left = std::min(left, static_cast<int>(cell % width));
            right = std::max(right, static_cast<int>(cell % width));
            top = std::min(top, static_cast<int>(cell / width));
            bottom = std::max(bottom, static_cast<int>(cell / width));
        }
        return {static_cast<std::int16_t>((left + right + 1) / 2),
                static_cast<std::int16_t>((top + bottom + 1) / 2)};
    };
    if (!covered.empty() && !board.visibility_revealed[covered.front()] &&
        !replay.castle_exterior_checkpoint) {
        const auto focus = review_focus();
        if (!replay.castle_exterior_aimed) {
            if (board.game.cursor() == focus) {
                replay.castle_exterior_aimed = true;
                replay.next_frame = frame_count + 90U;
            } else {
                nudge_toward(focus);
                replay.next_frame = frame_count + 3U;
            }
        } else {
            replay.castle_exterior_checkpoint = true;
            debugf("CHECKPOINT PYRITE_CASTLE_EXTERIOR_REVIEW\n");
            replay.next_frame = frame_count + 90U;
        }
        return;
    }
    if (!covered.empty() && board.visibility_revealed[covered.front()] &&
        !replay.castle_review_checkpoint) {
        const auto focus = review_focus();
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
                const ActionMenu menu = action_menu();
                const bool attack = std::any_of(menu.choices.begin(), menu.choices.begin() + menu.count,
                    [](const ActionChoice& choice) { return choice.kind == ActionChoiceKind::attack; });
                debugf(
                    "CHECKPOINT PYRITE_ACTION_MENU rows=%u attack=%u ability=%u "
                    "wait=1 cancel=1\n",
                    static_cast<unsigned>(action_rows()), attack ? 1U : 0U,
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
            if (board.game.snapshot().collecting_loot) {
                sim::Command done{};
                done.type = sim::CommandType::finish_collection;
                assertf(board.game.apply(done), "Review could not leave the loot collection phase");
                remember_transition_events();
            } else if (board.game.snapshot().outcome != sim::Outcome::ongoing) {
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
    const sim::UnitSnapshot* left = &combat.attacker_art;
    const sim::UnitSnapshot* right = &combat.defender_art;
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
        display_printf(14, 11, "ATK %d/%d", left_health, left->maximum_health);
        display_printf(202, 11, "DEF %d/%d", right_health, right->maximum_health);
        if (result != nullptr) {
            if (amount > 0) display_printf(130, 40, "%s -%d", result, amount);
            else display_printf(142, 40, "%s", result);
        }
    }
    rdpq_mode_pop();
}

const char* command_error_label(sim::CommandError error) {
    switch (error) {
        case sim::CommandError::inventory_full: return ui(console::Message::pack_full);
        case sim::CommandError::unknown_ground_item: return ui(console::Message::leave);
        case sim::CommandError::ground_out_of_reach: return ui(console::Message::walk_where);
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

bool speaker_name_matches(std::string_view name, std::string_view speaker) {
    if (speaker.empty()) return false;
    for (std::size_t at = name.find(speaker); at != std::string_view::npos;
         at = name.find(speaker, at + 1U)) {
        const std::size_t end = at + speaker.size();
        if ((at == 0U || name[at - 1U] == ' ') &&
            (end == name.size() || name[end] == ' ')) return true;
    }
    return false;
}

sim::ContentId speaker_unit_type(
    std::string_view speaker,
    std::string_view authored_speaker,
    const std::vector<client::RosterEntry>& roster
) {
    for (const client::RosterEntry& member : roster) {
        if (speaker_name_matches(member.name, speaker) ||
            speaker_name_matches(member.authored_name, authored_speaker)) {
            return member.unit_type.stable_id;
        }
    }
    // Enemy and other non-company speakers still name authored unit types.
    // Presentation holds the whole package bank, including off-board actors.
    for (const pr::UnitTypeColour& type : board.presentation.unit_types) {
        const std::string_view name = pr::content_name(
            board.runtime.package(), package_format::SectionType::unit_types,
            type.unit_type_id
        );
        if (speaker_name_matches(name, authored_speaker) ||
            speaker_name_matches(name, speaker)) return type.unit_type_id;
    }
    return 0U;
}

bool draw_speaker_portrait(
    std::string_view speaker,
    std::string_view authored_speaker,
    const std::vector<client::RosterEntry>& roster,
    float x, float y, float scale
) {
    const sim::ContentId type = speaker_unit_type(speaker, authored_speaker, roster);
    if (type == 0U) return false;
    sim::UnitSnapshot represented{};
    represented.unit_type_id = type;
    sprite_t* sprite = sprite_for_unit(represented, VisualPose::close_idle);
    if (sprite == nullptr) return false;
    rdpq_mode_push();
    rdpq_mode_begin();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_end();
    rdpq_blitparms_t params{};
    params.filtering = true;
    params.scale_x = params.scale_y = scale;
    rdpq_sprite_blit(sprite, x, y + (frame_count / 24U % 2U), &params);
    rdpq_mode_pop();
    P64::Debug::printStart();
    return true;
}

void draw_ground_equipment() {
    const auto& snapshot = board.game.snapshot();
    if (snapshot.ground_items.empty() && snapshot.hidden_items.empty()) return;
    rdpq_mode_push();
    rdpq_mode_begin();
        rdpq_mode_zbuf(false, false);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(0);
        rdpq_mode_alphacompare(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    std::array<bool, kMaxCells> marked{};
    const float render_scale = P64::Renderer::getRenderScale();
    const float scale = 120.0F / current_camera.ortho;
    const float dy = current_camera.eye_y - current_camera.target_y;
    const float depth = current_camera.eye_z - current_camera.target_z;
    const float length = std::max(0.01F, std::sqrt(dy * dy + depth * depth));
    const auto draw_marker = [&](sim::Position tile, bool glint) {
        const auto cell = static_cast<std::size_t>(tile.y) * snapshot.width + tile.x;
        if (cell >= board.content->terrain.size() || marked[cell] ||
            (visibility_hidden(cell) && !board.visibility_revealed[cell])) return;
        marked[cell] = true;
        const float dx = (world_x(tile.x, snapshot.width) + 20) / render_scale - current_camera.target_x;
        const float dz = (world_z(tile.y, snapshot.height) - 20) / render_scale - current_camera.target_z;
        const float floor = (terrain_lift(board.content->terrain[cell]) + 3) / render_scale;
        const int x = static_cast<int>(160.0F + dx * scale);
        const int y = static_cast<int>(120.0F + (dz * dy + (current_camera.target_y - floor) * depth) / length * scale);
        if (x < 4 || y < 4 || x > 315 || y > 235) return;
        if (glint) {
            rdpq_set_prim_color({242, 207, 119, 255});
            rdpq_fill_rectangle(x - 3, y, x + 4, y + 1);
            rdpq_set_prim_color({255, 235, 179, 255});
            rdpq_fill_rectangle(x, y - 3, x + 1, y + 4);
            return;
        }
        rdpq_set_prim_color({26, 32, 35, 255}); rdpq_fill_rectangle(x-3, y-3, x+4, y+4);
        rdpq_set_prim_color({228, 188, 99, 255}); rdpq_fill_rectangle(x-2, y-1, x+3, y+3);
        rdpq_fill_rectangle(x-1, y-2, x+2, y-1);
    };
    for (const auto& pile : snapshot.ground_items) draw_marker(pile.position, false);
    for (const auto& discovery : snapshot.hidden_items) {
        if (discovery.glint && view::discovery_glint(frame_count, discovery.flag))
            draw_marker({discovery.x, discovery.y}, true);
    }
    rdpq_mode_pop();
}

void draw_discovery_notice(int x) {
    const auto& discovery = board.discovery;
    if (discovery.content_id == 0U || motion.active || combat.active ||
        frame_count - board.discovery_frame >= 105U) return;
    const auto section = discovery.content_category == grandleon::core::ContentCategory::weapon
        ? package_format::SectionType::weapons : package_format::SectionType::items;
    const auto& package = board.runtime.package();
    const auto original = pr::content_name(package, section, discovery.content_id);
    const auto name = pr::localized_content_name(package, game_language(), section, discovery.content_id, original);
    const bool grounded = discovery.ground_item_id != 0U;
    ui_panel(x, 13, 174, grounded ? 55 : 41, 22);
    P64::Debug::printStart();
    display_printf(x + 8, 19, "%s", ui(console::Message::found_equipment));
    display_printf(x + 8, 34, "%.*s",
        static_cast<int>(console_prefix_bytes(name, 22U)), name.data());
    if (grounded) display_printf(x + 8, 51, "%s", ui(console::Message::pack_full));
}

void draw_battle_ui() {
    if (conversation_menu.active()) {
        ui_panel(8, 72, 304, 160, 24);
        display_printf(18, 80, "%.*s", static_cast<int>(console_prefix_bytes(conversation_speaker, 37U)),
            conversation_speaker.c_str());
        for (std::size_t index = 0; index < view::ConversationMenu::lines_per_page; ++index)
            display_glyphs(18, 102 + static_cast<int>(index) * 12,
                console_latin1(conversation_menu.line(index)), kUiIvory);
        if (conversation_menu.choices_visible()) {
            for (std::size_t index = 0; index < conversation_menu.answer_count(); ++index) {
                const int y = 139 + static_cast<int>(index) * 17;
                ui_focus(13, y - 1, 294, 16, index == conversation_menu.selected(), player_accent(battle_ui.player));
                display_glyphs(28, y, console_latin1(conversation_menu.label(index)), kUiIvory);
            }
        }
        ui_button_hint(18, 212, "A", ui(conversation_menu.more() ? console::Message::next : console::Message::confirm));
        ui_button_hint(189, 212, "B", ui(console::Message::back_button));
        return;
    }
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
        const std::string dialogue_text = console_latin1(
            client::substitute_campaign_member_names(localized_text, roster));
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
        (void)draw_speaker_portrait(speaker, line.speaker, roster, 23.0F, 164.0F, 0.64F);
        P64::Debug::printStart();
        display_printf(
            90, 169, "%.*s",
            static_cast<int>(console_prefix_bytes(speaker, 30U)),
            speaker.data()
        );
        constexpr std::size_t width = 31U;
        std::size_t start = std::min(battle_moment.text_offset, dialogue_text.size());
        for (int row = 0; row < 2 && start < dialogue_text.size(); ++row) {
            std::size_t end = std::min(start + width, dialogue_text.size());
            if (end < dialogue_text.size()) {
                const std::size_t space = dialogue_text.rfind(' ', end);
                if (space != std::string_view::npos && space > start) end = space;
            }
            display_glyphs(90, 189 + row * 17,
                std::string_view(dialogue_text).substr(start, end - start));
            start = end;
            while (start < dialogue_text.size() && dialogue_text[start] == ' ') ++start;
        }
        battle_moment.text_next = start;
        battle_moment.text_more = start < dialogue_text.size();
        display_printf(215, 215, "A %s", ui(console::Message::next));
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
        display_printf(
            80, 22, "%s", ui(console::Message::deployment)
        );
        display_printf(
            80, 42, "A %s   START %s",
            board.game.selected(board.game.active_player()) == 0U
                ? ui(console::Message::select)
                : ui(console::Message::deploy),
            ui(console::Message::begin)
        );
        rdpq_mode_pop();
        return;
    }
    if (!combat.active) draw_ground_equipment();
    if (battle_ui.phase == BattleUiPhase::board) draw_discovery_notice(8);
    if (board.game.snapshot().collecting_loot && !combat.active && battle_ui.phase == BattleUiPhase::board) {
        ui_panel(8, 211, 304, 23, 23);
        display_printf(17, 218, "%s  START", ui(console::Message::collect_equipment));
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
        ui_panel(45, 32, 230, 194, 25);
        P64::Debug::printStart();
        if (battle_ui.phase == BattleUiPhase::help) {
            display_printf(132, 48, "%s", ui(console::Message::help));
            display_printf(63, 76, "%s", ui(console::Message::move_cursor));
            display_printf(63, 98, "%s", ui(console::Message::select_confirm));
            display_printf(
                63, 120, "Z      %s", ui(console::Message::character_actions)
            );
            display_printf(63, 142, "START  %s", ui(console::Message::settings));
            display_printf(63, 164, "%s", ui(console::Message::back));
            display_printf(
                63, 186, "%s", ui(console::Message::close_view_help)
            );
            display_printf(91, 208, "A / B  %s", ui(console::Message::settings));
        } else {
            const bool languages = game_languages().size() > 1U;
            const auto y = [](unsigned row) {
                return 62 + static_cast<int>(row) * 18;
            };
            for (unsigned row = 0U; row < (languages ? 7U : 6U); ++row) {
                ui_focus(56, y(row) - 2, 208, 18, battle_ui.row == row, player_accent(battle_ui.player));
            }
            display_printf(94, 42, "%s P%u",
                ui(console::Message::settings),
                static_cast<unsigned>(battle_ui.player + 1U));
            display_printf(69, y(0), "%s %s",
                " ",
                ui(board.game.snapshot().collecting_loot ? console::Message::finish_collection : console::Message::resume));
            display_printf(69, y(1), "%s %s: %s",
                " ",
                ui(console::Message::close_view),
                ui(combat.enabled ? console::Message::on : console::Message::off));
            display_printf(69, y(2), "%s %s: %u%%",
                " ",
                ui(console::Message::music_volume), music_volume);
            display_printf(69, y(3), "%s %s",
                " ",
                ui(debug_hud ? console::Message::show_fps_on
                             : console::Message::show_fps_off));
            if (languages) {
                display_printf(69, y(4), "%s %s: %s",
                    " ",
                    ui(console::Message::language),
                    console::language_code(game_language()));
            }
            const unsigned effects_row = languages ? 5U : 4U;
            display_printf(69, y(effects_row), "%s %s: %u%%",
                " ",
                ui(console::Message::effects_volume), effects_volume);
            rdpq_mode_push();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_alphacompare(0);
            rdpq_set_prim_color({39, 59, 71, 255});
            rdpq_fill_rectangle(69, y(2) + 13, 251, y(2) + 16);
            rdpq_fill_rectangle(69, y(effects_row) + 13, 251, y(effects_row) + 16);
            rdpq_set_prim_color({206, 170, 83, 255});
            rdpq_fill_rectangle(69, y(2) + 13, 69 + music_volume * 182 / 100, y(2) + 16);
            rdpq_fill_rectangle(69, y(effects_row) + 13, 69 + effects_volume * 182 / 100, y(effects_row) + 16);
            rdpq_mode_pop();
            P64::Debug::printStart();
            const unsigned help_row = languages ? 6U : 5U;
            display_printf(69, y(help_row), "%s %s",
                " ",
                ui(console::Message::help));
            display_printf(69, 188, "%s %llu", ui(console::Message::gold),
                static_cast<unsigned long long>(battle_ui.wallet));
            ui_button_hint(63, 207, "A", ui(console::Message::confirm));
            ui_button_hint(172, 207, "B", ui(console::Message::resume));
        }
        rdpq_mode_pop();
        return;
    }
    struct PanelCorner { int x; int y; };
    constexpr int menu_width = 127;
    const bool show_roles = battle_ui.phase == BattleUiPhase::attack_target &&
        board.game.snapshot().role_matchups.active();
    const int menu_height = (battle_ui.phase == BattleUiPhase::action || battle_ui.phase == BattleUiPhase::pack)
        ? 35 + static_cast<int>(std::min<std::size_t>(4U, action_rows())) * 17 : 104 + (show_roles ? 14 : 0);
    const std::array<PanelCorner, 4> corners{{
        {5, 12}, {315 - menu_width, 12},
        {5, 235 - menu_height}, {315 - menu_width, 235 - menu_height}}};
    unsigned best_score = ~0U;
    PanelCorner panel = corners[0];
    const auto projected = [](sim::Position tile) {
        const float scale = 120.0F / current_camera.ortho;
        const float dx = world_x(tile.x, board.content->definition.width) / P64::Renderer::getRenderScale() - current_camera.target_x;
        const float dz = world_z(tile.y, board.content->definition.height) / P64::Renderer::getRenderScale() - current_camera.target_z;
        const std::size_t cell = static_cast<std::size_t>(tile.y) * board.content->definition.width + tile.x;
        const float floor = cell < board.content->terrain.size()
            ? terrain_lift(board.content->terrain[cell]) / P64::Renderer::getRenderScale() : 0.0F;
        const float dy = current_camera.eye_y - current_camera.target_y;
        const float depth = current_camera.eye_z - current_camera.target_z;
        const float length = std::max(0.01F, std::sqrt(dy * dy + depth * depth));
        return PanelCorner{static_cast<int>(160.0F + dx * scale),
            static_cast<int>(120.0F + (dz * dy + (current_camera.target_y - floor) * depth) / length * scale)};
    };
    for (const PanelCorner corner : corners) {
        unsigned score = 0U;
        const auto overlaps = [corner, menu_height](PanelCorner point) {
            return point.x + 16 > corner.x && point.x - 16 < corner.x + menu_width &&
                point.y > corner.y && point.y - 52 < corner.y + menu_height;
        };
        for (const sim::UnitSnapshot& unit : board.game.snapshot().units) {
            if (!sim::on_board(unit)) continue;
            if (overlaps(projected(unit.position))) score += unit.id == battle_ui.actor ? 20U : 4U;
        }
        if (overlaps(projected(board.game.cursor(battle_ui.player)))) score += 30U;
        if (score < best_score) { best_score = score; panel = corner; }
    }
    const int menu_x = panel.x;
    const int menu_y = panel.y;
    if (battle_ui.phase == BattleUiPhase::action)
        draw_discovery_notice(menu_x < 160 ? 139 : 8);
    ui_panel(menu_x, menu_y, menu_width, menu_height,
        battle_ui.phase == BattleUiPhase::action ? 23 : 19);
    P64::Debug::printStart();
    if (battle_ui.phase == BattleUiPhase::action || battle_ui.phase == BattleUiPhase::pack) {
        const auto* pack_actor = find_unit(battle_ui.actor);
        if (battle_ui.phase == BattleUiPhase::pack && pack_actor != nullptr) {
            const auto used = static_cast<unsigned long>(sim::carried_count(pack_actor->carried_inventory));
            if (pack_actor->states_carry_capacity)
                display_printf(menu_x + 10, menu_y + 10, "%s %lu/%u", ui(console::Message::pack), used, pack_actor->carry_capacity);
            else display_printf(menu_x + 10, menu_y + 10, "%s %lu", ui(console::Message::pack), used);
        } else display_printf(menu_x + 14, menu_y + 10, "%s P%u", ui(console::Message::action_heading), static_cast<unsigned>(battle_ui.player + 1U));
        ui_button_hint(menu_x + 105, menu_y + 6, "A", "", player_accent(battle_ui.player));
        const sim::UnitSnapshot* menu_actor = find_unit(battle_ui.actor);
        const std::size_t total = action_rows();
        const std::size_t selected = battle_ui.row;
        const std::size_t first = selected >= 4U ? selected - 3U : 0U;
        const std::size_t last = std::min(first + 4U, total);
        const ActionMenu menu = action_menu();
        for (std::size_t row = first; row < last; ++row) {
            const int y = menu_y + 28 + static_cast<int>(row - first) * 17;
            ui_focus(menu_x + 6, y - 2, menu_width - 12, 17, selected == row, player_accent(battle_ui.player));
            const char* marker = " ";
            const ActionChoice choice = menu.choices[row];
            if (choice.kind >= ActionChoiceKind::pack) {
                if (choice.kind == ActionChoiceKind::pack_entry || choice.kind == ActionChoiceKind::pack_loot) {
                    const auto section = choice.category == grandleon::core::ContentCategory::weapon
                        ? package_format::SectionType::weapons : package_format::SectionType::items;
                    sim::ContentId equipment = choice.content;
                    if (choice.kind == ActionChoiceKind::pack_loot) {
                        for (const auto& pile : board.game.snapshot().ground_items) if (pile.id == choice.content) equipment = pile.stack.content_id;
                    }
                    const auto name = pr::content_name(board.runtime.package(), section, equipment);
                    const auto localized = pr::localized_content_name(board.runtime.package(), game_language(), section, equipment, name);
                    display_printf(menu_x + 14, y, "%u %.*s", choice.quantity,
                        static_cast<int>(console_prefix_bytes(localized, 12U)), localized.data());
                } else display_printf(menu_x + 14, y, "%s", ui(choice.kind == ActionChoiceKind::pack_back && battle_ui.pack_view == PackView::loot ? console::Message::leave : pack_choice_message(choice.kind)));
            } else if (choice.kind == ActionChoiceKind::attack) {
                if (choice.content == 0U) {
                    display_printf(menu_x + 14, y, "%s %s", marker, ui(console::Message::attack));
                } else {
                    const auto name = pr::content_name(board.runtime.package(),
                        package_format::SectionType::weapons, choice.content);
                    const auto localized = pr::localized_content_name(board.runtime.package(),
                        game_language(), package_format::SectionType::weapons, choice.content, name);
                    display_printf(menu_x + 14, y, "%s %.*s", marker,
                        static_cast<int>(console_prefix_bytes(localized, 14U)), localized.data());
                }
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
                display_printf(
                    menu_x + 14, y, "%s %.*s", marker,
                    static_cast<int>(console_prefix_bytes(localized_name, 14U)), localized_name.data()
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
                display_printf(
                    menu_x + 14, y, "%s %.*s", marker,
                    static_cast<int>(console_prefix_bytes(localized_name, 14U)), localized_name.data()
                );
            } else if (choice.kind == ActionChoiceKind::talk) {
                display_printf(
                    menu_x + 14, y, "%s %s", marker, ui(console::Message::talk)
                );
            } else if (choice.kind == ActionChoiceKind::wait) {
                display_printf(
                    menu_x + 14, y, "%s %s", marker, ui(console::Message::wait)
                );
            } else {
                display_printf(
                    menu_x + 14, y, "%s %s", marker, ui(console::Message::cancel)
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
            ? sim::forecast_attack(board.game.snapshot(), actor->id, target->id,
                board.game.encounter().weapons(), battle_ui.weapon)
            : sim::AttackForecast{sim::CommandError::unknown_target};
        display_printf(
            menu_x + 12, menu_y + 5, "%s", ui(console::Message::attack_target)
        );
        if (forecast) {
            display_printf(menu_x + 14, menu_y + 22, "HP %d > %d", target->health, forecast.target_health_after);
            ui_health_meter(menu_x + 14, menu_y + 37, menu_width - 28,
                target->health, target->maximum_health, forecast.target_health_after);
            display_printf(
                menu_x + 14, menu_y + 44, "%d%%  %s %d", forecast.hit_chance,
                ui(console::Message::hit),
                forecast.damage
            );
            if (forecast.counter) {
                display_printf(
                    menu_x + 14, menu_y + 58, "%s %d", ui(console::Message::counter),
                    forecast.counter_damage
                );
            } else {
                display_printf(
                    menu_x + 14, menu_y + 58, "%s", ui(console::Message::no_counter)
                );
            }
            int button_y = menu_y + 72;
            if (forecast.role_damage != 0) {
                if (forecast.counter_role_damage != 0) {
                    display_printf(menu_x + 14, button_y, "%s %+d / %+d", ui(console::Message::role_modifier),
                        forecast.role_damage, forecast.counter_role_damage);
                } else {
                    display_printf(menu_x + 14, button_y, "%s %+d", ui(console::Message::role_modifier),
                        forecast.role_damage);
                }
                button_y += 14;
            }
            ui_button_hint(menu_x + 14, button_y, "A", ui(console::Message::confirm), player_accent(battle_ui.player));
            ui_button_hint(menu_x + 14, button_y + 15, "B", ui(console::Message::back_button));
        } else {
            display_printf(
                menu_x + 14, menu_y + 40, "%s", command_error_label(forecast.error)
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
        display_printf(
            menu_x + 14, menu_y + 10, "%s", ui(console::Message::ability_target)
        );
        if (forecast) {
            if (!forecast.covered) {
                display_printf(
                    menu_x + 14, menu_y + 40, "%s", ui(console::Message::clear_ground)
                );
            } else if (forecast.spared) {
                display_printf(
                    menu_x + 14, menu_y + 40, "%s", ui(console::Message::target_safe)
                );
            } else if (forecast.kind == sim::AbilityKind::restore) {
                display_printf(
                    menu_x + 14, menu_y + 40, "%s %d/%d", ui(console::Message::mend),
                    forecast.restored,
                    forecast.target_health_after
                );
            } else {
                display_printf(
                    menu_x + 14, menu_y + 40, "%d%%  %s %d", forecast.hit_chance,
                    ui(console::Message::cast),
                    forecast.damage
                );
            }
            display_printf(menu_x + 14, menu_y + 72, "A %s", ui(console::Message::confirm));
            display_printf(menu_x + 14, menu_y + 88, "B %s", ui(console::Message::back_button));
        } else {
            display_printf(
                menu_x + 14, menu_y + 40, "%s", command_error_label(forecast.error)
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
        display_printf(
            menu_x + 14, menu_y + 10, "%s", ui(console::Message::talk_to_whom)
        );
        if (forecast) {
            display_printf(
                menu_x + 14, menu_y + 40, "%s", ui(console::Message::confirm)
            );
            display_printf(menu_x + 14, menu_y + 72, "A %s", ui(console::Message::confirm));
            display_printf(menu_x + 14, menu_y + 88, "B %s", ui(console::Message::back_button));
        } else {
            display_printf(
                menu_x + 14, menu_y + 40, "%s", command_error_label(forecast.error)
            );
        }
    } else if (battle_ui.phase == BattleUiPhase::result) {
        display_printf(
            menu_x + 14, menu_y + 10, "%s", ui(console::Message::result)
        );
        if (battle_ui.result_error != sim::CommandError::none) {
            display_printf(
                menu_x + 14, menu_y + 40, "%s",
                command_error_label(battle_ui.result_error)
            );
        } else if (battle_ui.result_command == sim::CommandType::wait ||
                   battle_ui.result_command == sim::CommandType::talk) {
            display_printf(
                menu_x + 14, menu_y + 40, "%s", ui(console::Message::turn_complete)
            );
        } else if (battle_ui.result_missed) {
            display_printf(
                menu_x + 14, menu_y + 40, "%s", ui(console::Message::miss)
            );
        } else if (battle_ui.result_restored) {
            display_printf(
                menu_x + 14, menu_y + 40, "%s %d", ui(console::Message::mended),
                battle_ui.result_amount
            );
        } else {
            display_printf(
                menu_x + 14, menu_y + 40, "%s %d", ui(console::Message::hit),
                battle_ui.result_amount
            );
        }
    }
    rdpq_mode_pop();
}

void draw_wrapped_story(std::string_view utf8) {
    const std::string glyphs = console_latin1(utf8);
    const std::string_view text = glyphs;
    constexpr std::size_t width = 27U;
    std::size_t start = std::min(shell.story_text_offset, text.size());
    int line = 0;
    while (start < text.size() && line < 5) {
        std::size_t end = std::min(start + width, text.size());
        if (end < text.size()) {
            const std::size_t space = text.rfind(' ', end);
            if (space != std::string_view::npos && space > start) end = space;
        }
        display_glyphs(102, 109 + line * 16, text.substr(start, end - start));
        start = end;
        while (start < text.size() && text[start] == ' ') ++start;
        ++line;
    }
    shell.story_text_next = start;
    shell.story_text_more = start < text.size();
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
    display_printf(
        42, 84, "%s", ui(console::Message::character_name)
    );
    display_printf(
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
            display_printf(
                x, 119 + row * 15, "%s%s%s",
                selected ? "[" : " ", key.label,
                selected ? "]" : " "
            );
        }
    }
    display_printf(
        31, 192, "A %s  B %s",
        ui(console::Message::type_name), ui(console::Message::erase)
    );
    display_printf(
        31, 204, "C-UP %s  START %s",
        ui(console::Message::keep), ui(console::Message::save_action)
    );
}

void draw_brand_mark(int x, int y) {
    rdpq_mode_push();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_alphacompare(0);
    for (const grandleon_logo_rect& rect : grandleon_logo_rects) {
        rdpq_set_prim_color({rect.r, rect.g, rect.b, 255});
        rdpq_fill_rectangle(x + rect.x, y + rect.y,
            x + rect.x + rect.width, y + rect.y + rect.height);
    }
    rdpq_mode_pop();
    P64::Debug::printStart();
}

void draw_volume_slider(int x, int y, unsigned volume, int width = 180) {
    rdpq_mode_push();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_alphacompare(0);
    rdpq_set_prim_color({39, 59, 71, 255});
    rdpq_fill_rectangle(x, y, x + width, y + 3);
    rdpq_set_prim_color({206, 170, 83, 255});
    rdpq_fill_rectangle(x, y, x + static_cast<int>(volume) * width / 100, y + 3);
    rdpq_mode_pop();
    P64::Debug::printStart();
}

void draw_title_shell() {
    rdpq_mode_push();
    if (board.combat_backdrop != nullptr) {
        rdpq_mode_begin();
            rdpq_mode_zbuf(false, false);
            rdpq_mode_combiner(RDPQ_COMBINER_TEX);
            rdpq_mode_alphacompare(1);
            rdpq_mode_blender(0);
            rdpq_mode_fog(0);
        rdpq_mode_end();
        rdpq_blitparms_t params{};
        params.filtering = true;
        rdpq_sprite_blit(board.combat_backdrop, 0.0F, 0.0F, &params);
    }
    ui_flat();
    ui_rectangle(0, 0, 320, 240, {6, 16, 27, 95});
    ui_panel(15, 12, 290, 73);
    draw_brand_mark(24, 18);
    const std::string_view title = pr::localized_project_title(
        board.runtime.package(), game_language(), grandleon_project_title);
    const std::string name = console_latin1(title);
    const int scale = name.size() <= 16U ? 2 : 1;
    display_glyphs(101, scale == 2 ? 24 : 31,
        name.substr(0U, scale == 2 ? 16U : 31U), kUiIvory, scale);
    ui_flat();
    ui_rectangle(101, 53, 187, 1, kUiGold);
    P64::Debug::printStart();
    display_printf(101, 62, "%s  %s", ui(console::Message::players),
        shell.allow_one && shell.allow_two ? "1P / 2P" : (shell.allow_one ? "1P" : "2P"));
    ui_panel(162, 94, 143, 133);
    const bool languages = game_languages().size() > 1U;
    const unsigned settings_row = languages ? 3U : 2U;
    for (unsigned row = 0; row <= settings_row; ++row) {
        const int y = 104 + static_cast<int>(row) * 24;
        ui_focus(169, y, 129, 21, shell.title_row == row);
        if (row == 2U && languages) {
            display_printf(190, y + 4, "%s: %s", ui(console::Message::language),
                console::language_code(game_language()));
        } else {
            const console::Message label = row == 0U ? console::Message::begin
                : (row == 1U ? console::Message::help : console::Message::settings);
            display_printf(190, y + 4, "%s", ui(label));
        }
    }
    const int hint_width = 19 + static_cast<int>(console_latin1(ui(console::Message::confirm)).size()) * 6;
    ui_button_hint(162 + (143 - hint_width) / 2, 208, "A", ui(console::Message::confirm));
    rdpq_mode_pop();
}

std::string_view shop_equipment_name(const pr::ShopOffer& offer) {
    const auto section = offer.category == grandleon::core::ContentCategory::weapon
        ? package_format::SectionType::weapons : package_format::SectionType::items;
    const auto name = pr::content_name(board.runtime.package(), section, offer.content_id);
    return pr::localized_content_name(board.runtime.package(), game_language(), section, offer.content_id, name);
}

std::string_view shop_member_name(const client::RosterEntry& member) {
    if (member.has_player_name) return member.name;
    const auto authored = member.authored_name.empty() ? std::string_view(member.name) : std::string_view(member.authored_name);
    return pr::localized_campaign_member_name(board.runtime.package(), game_language(),
        grandleon_project_campaign_id, member.placement_source_key, authored);
}

std::uint32_t shop_carried_count(const client::RosterEntry& member) {
    std::uint32_t count = 0U;
    for (const auto& stack : member.carried) {
        if (stack.item.category == grandleon::core::ContentCategory::weapon ||
            stack.item.category == grandleon::core::ContentCategory::item) count += stack.quantity;
    }
    return count;
}

console::Message shop_error_message(client::ManagementError error) {
    return error == client::ManagementError::inventory_full ? console::Message::pack_full
        : error == client::ManagementError::sold_out ? console::Message::sold_out
        : error == client::ManagementError::insufficient_gold ? console::Message::insufficient_gold
        : console::Message::shop_unavailable;
}

void draw_shop_error(int x, int y, int columns, client::ManagementError error) {
    std::string_view message = ui(shop_error_message(error));
    auto first = console_prefix_bytes(message, static_cast<unsigned>(columns));
    if (first < message.size()) {
        const auto space = message.rfind(' ', first);
        if (space != std::string_view::npos && space > 0U) first = space;
    }
    display_printf(x, y, "%.*s", static_cast<int>(first), message.data());
    message.remove_prefix(first);
    while (!message.empty() && message.front() == ' ') message.remove_prefix(1U);
    if (!message.empty()) display_printf(x, y + 12, "%.*s",
        static_cast<int>(console_prefix_bytes(message, static_cast<unsigned>(columns))), message.data());
}

void draw_shop() {
    rdpq_mode_push();
    rdpq_mode_begin();
    rdpq_mode_zbuf(false, false);
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(0);
    rdpq_mode_fog(0);
    rdpq_mode_end();
    rdpq_set_prim_color({8, 16, 27, 255});
    rdpq_fill_rectangle(0, 0, 320, 240);
    ui_panel(4, 4, 312, 104, 23);
    P64::Debug::printStart();
    display_printf(12, 12, "%s", ui(console::Message::shop));
    display_printf(184, 12, "%s %lu", ui(console::Message::gold), static_cast<unsigned long>(shell.shop.gold));
    display_printf(42, 32, "* %s", ui(console::Message::unlimited_stock));
    display_printf(200, 32, "%s", ui(console::Message::price));
    display_printf(256, 32, "%s", ui(console::Message::stock));
    const auto* focused = shell.shop_ui.offer(shell.shop, shell.shop_focus_player);
    std::size_t selected = 0U;
    for (std::size_t index = 0; index < shell.shop.shop_offers.size(); ++index) {
        if (focused && shell.shop.shop_offers[index].offer.id == focused->offer.id) selected = index;
    }
    if (selected < shell.shop_catalog_top) shell.shop_catalog_top = selected;
    if (selected >= shell.shop_catalog_top + 4U) shell.shop_catalog_top = selected - 3U;
    for (std::size_t index = shell.shop_catalog_top;
         index < std::min(shell.shop.shop_offers.size(), shell.shop_catalog_top + 4U); ++index) {
        const auto& stock = shell.shop.shop_offers[index];
        const int y = 46 + static_cast<int>(index - shell.shop_catalog_top) * 14;
        bool selected_by_either = false;
        for (unsigned player = 0; player < shell.shop_ui.players(); ++player) {
            if (shell.shop_ui.state(player).offer == stock.offer.id) selected_by_either = true;
        }
        ui_focus(38, y - 2, 272, 14, selected_by_either, player_accent(shell.shop_focus_player));
        for (unsigned player = 0; player < shell.shop_ui.players(); ++player) {
            if (shell.shop_ui.state(player).offer != stock.offer.id) continue;
            rdpq_mode_push();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_set_prim_color(player_accent(player));
            rdpq_fill_rectangle(10 + static_cast<int>(player) * 14, y - 1, 21 + static_cast<int>(player) * 14, y + 10);
            rdpq_mode_pop();
            P64::Debug::printStart();
            display_printf(11 + static_cast<int>(player) * 14, y, "%u", player + 1U);
        }
        const auto name = shop_equipment_name(stock.offer);
        display_printf(42, y, "%.*s", static_cast<int>(console_prefix_bytes(name, 18U)), name.data());
        display_printf(200, y, "%uG", stock.offer.price);
        if (stock.limited) display_printf(256, y, "x%lu", static_cast<unsigned long>(stock.remaining));
        else display_printf(256, y, "*");
    }
    for (unsigned player = 0; player < shell.shop_ui.players(); ++player) {
        const int x = shell.shop_ui.players() == 1U ? 8 : 8 + static_cast<int>(player) * 156;
        const int width = shell.shop_ui.players() == 1U ? 304 : 148;
        const int columns = (width - 16) / 8;
        const auto& state = shell.shop_ui.state(player);
        const auto* hero = shell.shop_ui.member(shell.shop, player);
        const auto* stock = shell.shop_ui.offer(shell.shop, player);
        ui_panel(x, 112, width, 96, 19);
        rdpq_mode_push();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_set_prim_color(player_accent(player));
        rdpq_fill_rectangle(x + 8, 131, x + width - 8, 133);
        rdpq_mode_pop();
        P64::Debug::printStart();
        const auto name = hero ? shop_member_name(*hero) : std::string_view{""};
        display_printf(x + 8, 120, "P%u %.*s", player + 1U,
            static_cast<int>(console_prefix_bytes(name, static_cast<unsigned>(columns - 3))), name.data());
        if (hero) {
            if (hero->states_carry_capacity) display_printf(x + 8, 138, "%s %lu/%u", ui(console::Message::pack),
                static_cast<unsigned long>(shop_carried_count(*hero)), hero->carry_capacity);
            else display_printf(x + 8, 138, "%s %lu", ui(console::Message::pack), static_cast<unsigned long>(shop_carried_count(*hero)));
            if (std::find(shell.shop.fielded.begin(), shell.shop.fielded.end(), hero->member) == shell.shop.fielded.end()) {
                display_printf(x + 80, 138, "%s", ui(console::Message::shop_reserve));
            }
        } else display_printf(x + 8, 150, "%s", ui(console::Message::shop_no_heroes));
        if (stock && hero) {
            const auto equipment = shop_equipment_name(stock->offer);
            display_printf(x + 8, 150, "%.*s", static_cast<int>(console_prefix_bytes(equipment, static_cast<unsigned>(columns))), equipment.data());
            if (stock->limited) display_printf(x + 8, 164, "%uG  x%lu", stock->offer.price, static_cast<unsigned long>(stock->remaining));
            else display_printf(x + 8, 164, "%uG  *", stock->offer.price);
        }
        if (state.error != client::ManagementError::none) draw_shop_error(x + 8, 180, columns, state.error);
        else if (state.ready) {
            display_printf(x + 8, 180, "%s", ui(console::Message::ready));
            if (hero) display_printf(x + 8, 194, "START %s", ui(console::Message::shop_edit));
        } else if (state.confirming) {
            const int half = (width - 16) / 2;
            ui_focus(x + 6, 178, half, 14, state.buy_selected, player_accent(player));
            ui_focus(x + 8 + half, 178, half, 14, !state.buy_selected, player_accent(player));
            display_printf(x + 8, 180, "%s", ui(console::Message::shop_buy));
            display_printf(x + 10 + half, 180, "%s", ui(console::Message::back_button));
            display_printf(x + 8, 194, "A %s", ui(console::Message::confirm));
        } else {
            display_printf(x + 8, 180, "A %s", ui(console::Message::shop_buy));
            display_printf(x + 8, 194, "START %s", ui(console::Message::ready));
        }
    }
    display_printf(12, 212, "%s", ui(console::Message::shop_navigation));
    display_printf(12, 224, "%s", ui(console::Message::shop_actions));
    rdpq_mode_pop();
}

void draw_shell() {
    if (shell.phase == ShellPhase::shop) { draw_shop(); return; }
    if (shell.phase == ShellPhase::title) { draw_title_shell(); return; }
    rdpq_mode_push();
    rdpq_mode_begin();
        rdpq_mode_zbuf(false, false);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    rdpq_set_prim_color({8, 16, 27, 255});
    rdpq_fill_rectangle(0, 0, 320, 240);
    ui_panel(18, 24, 284, 192, 37);
    P64::Debug::printStart();
    const std::string_view project_title = pr::localized_project_title(
        board.runtime.package(), game_language(), grandleon_project_title
    );
    display_printf(
        43, 38, "%.*s",
        static_cast<int>(console_prefix_bytes(project_title, 29U)),
        project_title.data()
    );
    switch (shell.phase) {
        case ShellPhase::title:
            break;
        case ShellPhase::settings:
            ui_focus(54, 96, 212, 27, shell.audio_row == 0U);
            ui_focus(54, 126, 212, 27, shell.audio_row == 1U);
            ui_focus(54, 156, 212, 20, shell.audio_row == 2U);
            display_printf(91, 76, "%s", ui(console::Message::settings));
            display_printf(65, 100, "%s %s: %u%%", " ",
                ui(console::Message::music_volume), music_volume);
            display_printf(65, 130, "%s %s: %u%%", " ",
                ui(console::Message::effects_volume), effects_volume);
            draw_volume_slider(65, 116, music_volume);
            draw_volume_slider(65, 146, effects_volume);
            if (game_languages().size() > 1U) {
                display_printf(65, 160, "%s %s: %s", " ",
                    ui(console::Message::language), console::language_code(game_language()));
            }
            display_printf(43, 180, "%s", ui(console::Message::volume_controls));
            ui_button_hint(65, 194, "B", ui(console::Message::back_button));
            break;
        case ShellPhase::help:
            display_printf(119, 88, "%s", ui(console::Message::help));
            display_printf(43, 112, "%s", ui(console::Message::move_cursor));
            display_printf(43, 132, "%s", ui(console::Message::select_confirm));
            display_printf(43, 152, "Z      %s", ui(console::Message::action_heading));
            display_printf(43, 172, "START  %s", ui(console::Message::settings));
            display_printf(43, 192, "%s", ui(console::Message::back));
            break;
        case ShellPhase::players:
            ui_focus(60, 124, 200, 21, shell.player_choice == 0U);
            ui_focus(60, 146, 200, 21, shell.player_choice == 1U);
            display_printf(78, 100, "%s", ui(console::Message::players));
            if (shell.allow_one) {
                display_printf(
                    91, 128, "%s %s",
                    " ",
                    ui(console::Message::one_player)
                );
            }
            if (shell.allow_two) {
                display_printf(
                    76, 150, "%s %s%s",
                    " ",
                    ui(console::Message::two_players),
                    shell.second_connected ? "" : " (PAD 2)"
                );
            }
            break;
        case ShellPhase::save:
            ui_focus(60, 124, 200, 21, shell.save_choice == 0U);
            ui_focus(60, 146, 200, 21, shell.save_present && shell.save_choice == 1U);
            display_printf(84, 100, "%s", ui(console::Message::start_campaign));
            display_printf(
                96, 128, "%s %s",
                " ",
                ui(console::Message::new_game)
            );
            if (shell.save_present) {
                display_printf(
                    96, 150, "%s %s",
                    " ",
                    ui(console::Message::continue_game)
                );
            } else {
                display_printf(75, 178, "%s", ui(console::Message::no_save));
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
                if (!draw_speaker_portrait(speaker, line.speaker, roster, 30.0F, 75.0F, 0.8F)) {
                    draw_brand_mark(31, 76);
                }
                display_printf(
                    102, 86, "%.*s", static_cast<int>(console_prefix_bytes(speaker, 23U)),
                    speaker.data()
                );
                draw_wrapped_story(text);
                display_printf(
                    220, 194, "A %s", ui(console::Message::next)
                );
            }
            break;
        }
        case ShellPhase::name:
            draw_name_editor();
            break;
        case ShellPhase::shop:
            break;
        case ShellPhase::supplies: {
            display_printf(42, 78, "%s", ui(console::Message::company_store));
            const auto& member = shell.supplies.member();
            const std::string_view name = member.has_player_name ? std::string_view(member.name)
                : pr::localized_campaign_member_name(board.runtime.package(), game_language(),
                    grandleon_project_campaign_id, member.placement_source_key, member.name);
            display_printf(42, 97, "< %.*s >",
                static_cast<int>(console_prefix_bytes(name, 23U)), name.data());
            const auto& rows = shell.supplies.rows();
            const auto selected = shell.supplies.selected();
            const auto first = selected >= 4U ? selected - 3U : 0U;
            display_printf(193, 107, "%s", ui(console::Message::stock));
            display_printf(246, 107, "%s", ui(console::Message::pack));
            for (std::size_t row = first; row < std::min(first + 4U, rows.size()); ++row) {
                const auto& entry = rows[row];
                const auto section = entry.item.category == grandleon::core::ContentCategory::weapon
                    ? package_format::SectionType::weapons : package_format::SectionType::items;
                const auto name = pr::content_name(board.runtime.package(), section, entry.item.stable_id);
                const auto label = pr::localized_content_name(board.runtime.package(), game_language(),
                    section, entry.item.stable_id, name);
                const int y = 120 + static_cast<int>(row - first) * 15;
                display_printf(42, y, "%c %.*s",
                    row == selected ? '>' : ' ', static_cast<int>(console_prefix_bytes(label, 17U)),
                    label.data());
                display_printf(200, y, "%lu%s", static_cast<unsigned long>(std::min<std::uint32_t>(entry.stored, 99999U)),
                    entry.stored > 99999U ? "+" : "");
                display_printf(253, y, "%lu%s", static_cast<unsigned long>(std::min<std::uint32_t>(entry.held, 99999U)),
                    entry.held > 99999U ? "+" : "");
            }
            display_printf(42, 187, "A %s  B %s", ui(console::Message::give), ui(console::Message::return_supply));
            display_printf(42, 201, "START %s", ui(console::Message::next));
            break;
        }
        case ShellPhase::deployment: {
            display_printf(76, 88, "%s %lu/%u",
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
                display_printf(
                    48, 116 + static_cast<int>(row - first) * 18,
                    "%s %s %.*s",
                    shell.deployment_row == row ? ">" : " ",
                    deployment_contains(entry.member) ? "[X]" : "[ ]",
                    static_cast<int>(console_prefix_bytes(name, 22U)), name.data()
                );
            }
            display_printf(
                55, 194, "A %s  START %s",
                ui(console::Message::toggle), ui(console::Message::deploy)
            );
            break;
        }
        case ShellPhase::aftermath: {
            display_printf(
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
            const auto& rewards = shell.aftermath.progression.rewards;
            for (std::size_t row = 0; rewards.empty() && row < count; ++row) {
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
                display_printf(
                    42, 116 + static_cast<int>(row) * 18,
                    "%.*s  %s",
                    static_cast<int>(console_prefix_bytes(displayed_name, 21U)), displayed_name.data(), result
                );
            }
            const auto end = std::min(shell.aftermath_reward_offset + 3U, rewards.size());
            for (std::size_t index = shell.aftermath_reward_offset; index < end; ++index) {
                const auto& reward = rewards[index];
                const auto member = std::find_if(roster.begin(), roster.end(),
                    [&reward](const auto& entry) { return entry.member == reward.member; });
                if (member == roster.end()) continue;
                const int y = 116 + static_cast<int>(index - shell.aftermath_reward_offset) * 18;
                display_printf(42, y, "%.*s +%lu XP",
                    static_cast<int>(console_prefix_bytes(member->name, 16U)), member->name.c_str(),
                    static_cast<unsigned long>(reward.experience));
                if (reward.level != reward.previous_level) {
                    display_printf(211, y, "%s %u", ui(console::Message::level_up),
                        static_cast<unsigned>(reward.level));
                }
            }
            display_printf(
                42, 176, "%s %lu  %s %lu",
                ui(console::Message::fell),
                static_cast<unsigned long>(shell.aftermath.fallen.size()),
                ui(console::Message::joined),
                static_cast<unsigned long>(shell.aftermath.recruited.size())
            );
            display_printf(
                202, 194, "A %s", ui(console::Message::next)
            );
            break;
        }
        case ShellPhase::complete:
            if (pr::show_end_statistics(board.runtime.package())) {
                const auto totals = board.campaign->statistics();
                display_printf(43, 69, "%s", ui(totals.lost_visit
                    ? console::Message::defeat : console::Message::campaign_complete));
                const std::array messages{
                    console::Message::battles_played, console::Message::enemies_defeated,
                    console::Message::allies_fallen, console::Message::hidden_finds,
                    console::Message::conversations,
                };
                const std::array<std::uint32_t, 5> values{
                    totals.battles, totals.enemy_defeat_percentage(), totals.allies_fallen,
                    totals.hidden_items_found, totals.conversations,
                };
                for (std::size_t row = 0; row < messages.size(); ++row) {
                    const int y = 88 + static_cast<int>(row) * 19;
                    display_printf(43, y, "%s", ui(messages[row]));
                    if (row == 1U && totals.enemies_encountered == 0U) display_printf(238, y, "-");
                    else display_printf(230, y, row == 1U ? "%lu%%" : "%lu",
                        static_cast<unsigned long>(values[row]));
                }
                ui_button_hint(43, 190, "A", ui(console::Message::return_to_title));
                break;
            }
            display_printf(91, 112, "%s", ui(board.campaign->defeated()
                ? console::Message::defeat : console::Message::campaign_ended));
            display_printf(
                72, 142, "%lu %s",
                static_cast<unsigned long>(shell.completed_battles),
                ui(console::Message::battles_played)
            );
            ui_button_hint(72, 178, "A", ui(console::Message::return_to_title));
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
    debugf("PYRITE_AUDIO_LOADED music=%u effects=%u\n", music_volume, effects_volume);
    update_music();
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
        "save=%u renderer=pyrite-tiny3d fallback=none review_driver=%u\n",
        grandleon_project_campaign_source,
        pr::local_player_mode_name(campaign.definition.player_mode).data(),
        shell.save_present ? 1U : 0U, grandleon_project_review_driver ? 1U : 0U
    );
}

void onSceneUpdate() {
    board_timing_begin_update();
    update_music();
    if (shell.phase != ShellPhase::battle) {
        update_shell_input();
        frame_count += kLogicTicksPerUpdate;
        update_performance_sample();
        return;
    }
    if (!camera_configured) {
        const auto home = board.game.cursor();
        overview_to = tactical_camera_for(home);
        overview_from = tactical_camera_for({
            static_cast<std::int16_t>(grandleon::view::StageOverview::far_edge(board.content->definition.width, home.x)),
            static_cast<std::int16_t>(grandleon::view::StageOverview::far_edge(board.content->definition.height, home.y))});
        stage_overview.begin(board.content->definition.width, board.content->definition.height);
        if (combat.reduced_motion) stage_overview.finish();
        apply_camera(stage_overview.active() ? overview_from : overview_to);
        debugf("CHECKPOINT PYRITE_STAGE_OVERVIEW_BEGIN_%lu stage=%lu\n",
            static_cast<unsigned long>(shell.stage_index + 1U), static_cast<unsigned long>(shell.stage_index + 1U));
        camera_min_x = current_camera.target_x;
        camera_max_x = current_camera.target_x;
        camera_min_z = current_camera.target_z;
        camera_max_z = current_camera.target_z;
        camera_configured = true;
    }
    if (stage_overview.active()) {
        bool skipped = false;
        for (const auto port : {JOYPAD_PORT_1, JOYPAD_PORT_2}) {
            const auto pressed = joypad_get_buttons_pressed(port);
            skipped = skipped || pressed.a || pressed.b || pressed.start;
        }
        if (skipped) stage_overview.finish();
        else stage_overview.advance(kLogicTicksPerUpdate);
        if (stage_overview.active()) update_tactical_camera();
        else {
            apply_camera(overview_to);
            // Opening motion is not evidence of player-driven scrolling.
            camera_min_x = camera_max_x = current_camera.target_x;
            camera_min_z = camera_max_z = current_camera.target_z;
            debugf("CHECKPOINT PYRITE_STAGE_OVERVIEW_END skipped=%u\n", skipped ? 1U : 0U);
        }
        replay.next_frame += kLogicTicksPerUpdate;
        battle_moment.next_automatic_frame += kLogicTicksPerUpdate;
        build_unit_vertices();
        frame_count += kLogicTicksPerUpdate;
        update_performance_sample();
        return;
    }
    if (battle_ui.phase == BattleUiPhase::result && !combat.active &&
        frame_count >= battle_ui.result_until) {
        battle_ui = {};
    }
    physical_input();
    update_combat();
    update_battle_moment();
    if (!conversation_menu.active() && !battle_moment.active && battle_moment.dialogue_ids.empty()) {
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
    board_timing_begin_submission();
    animate_water_vertices();
    board.keep.animate(frame_count / 2U, !combat.reduced_motion);
    draw_environment();
    draw_elevation();
    draw_board_cells();
    draw_terrain_forms();
    draw_landmark();
    board.settlement.draw();
    const auto& hidden = board.content->visual_visibility.hidden_cells;
    board.keep.draw(!hidden.empty() && !board.visibility_revealed[hidden.front()]);
    board.modular_castle.draw(board.visibility_revealed.data(), board.content->terrain.size());
    draw_trees();
    board.scenery_sprites.draw(current_camera.eye_x,current_camera.eye_y,current_camera.eye_z,current_camera.target_x,current_camera.target_y,current_camera.target_z);
    draw_visual_visibility();
    board.shadows.draw();
    if (!stage_overview.active()) draw_cursor();
    draw_units();
}

void onSceneDraw2D() {
    board_timing_end_submission();
    if (shell.phase != ShellPhase::battle) {
        draw_shell();
        return;
    }
    if (stage_overview.active()) {
        ui_button_hint(232, 216, "A", ui(console::Message::begin));
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
