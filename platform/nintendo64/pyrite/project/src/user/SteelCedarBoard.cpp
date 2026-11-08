#include "script/globalScript.h"

#include <grandleon/core/content_identity.hpp>
#include <libdragon.h>
#include <t3d/t3d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "debug/debugDraw.h"
#include "BoardShadows.hpp"
#include "GameplaySlice.hpp"
#include "GpkRuntimeAdapter.hpp"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "vi/swapChain.h"
#include <grandleon/view/motion.hpp>

namespace core = grandleon::core;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace tac = grandleon::tactics;
namespace view = grandleon::view;
namespace gp = grandleon::nintendo64::pyrite;

namespace {

constexpr std::int16_t kCell = 52;
constexpr std::int16_t kHalfCell = kCell / 2 - 1;
constexpr std::size_t kCellVertexCount = 4;
constexpr std::size_t kMaxCells = 8U * 6U;
constexpr std::size_t kBatchCells = 15U;
constexpr std::size_t kMaxElevationVertices = kMaxCells * 8U;
constexpr std::size_t kGroundVertexCount = 4U;
constexpr std::size_t kMaxApronCells = 160U;
constexpr std::size_t kTreeVertexCount = 12U;
constexpr std::size_t kMaxTrees = 56U;
constexpr std::size_t kUnitVertexCount = 4U;
constexpr std::size_t kMaxUnits = 8U;
constexpr std::size_t kCursorVertexCount = 16U;
constexpr std::size_t kMaxLandmarkVertices = 40U;

constexpr auto kBellOrchard =
    core::stable_content_id_v1("steel_and_cedar/bell_orchard");
constexpr auto kRoad = core::stable_content_id_v1("road");
constexpr auto kFarmland = core::stable_content_id_v1("farmland_orchard_tree");
constexpr auto kForest = core::stable_content_id_v1("forest_orchard_tree");
constexpr auto kPaved = core::stable_content_id_v1("paved");
constexpr auto kBell = core::stable_content_id_v1("paved_orchard_bell");
constexpr auto kHills = core::stable_content_id_v1("hills");

enum class SurroundingsMode : std::uint8_t {
    extend_edges,
    fixed_terrain,
    natural_blend,
};

// Temporary presenter policy seam. It deliberately mirrors the shape of the
// planned portable map `surroundings` setting instead of baking Bell Orchard
// decisions into the renderer. A later package revision can populate this
// adapter without changing the apron builder or tactical bounds.
struct EnvironmentPolicy final {
    SurroundingsMode mode{SurroundingsMode::natural_blend};
    std::uint64_t fixed_terrain{kForest};
    std::uint32_t seed{0x5ECEDA7U};
    std::uint8_t variation{5U};
    std::uint8_t apron_cells{3U};
};

constexpr EnvironmentPolicy kEnvironmentPolicy{};

constexpr auto kRowanMember = core::stable_content_id_v1("rowan");
constexpr auto kRowanType = core::stable_content_id_v1("rowan_vale");
constexpr auto kTomasType = core::stable_content_id_v1("tomas_reed");
constexpr auto kMarchSwordType = core::stable_content_id_v1("march_sword");
constexpr auto kRetainerType = core::stable_content_id_v1("red_retainer");
constexpr auto kArcherType = core::stable_content_id_v1("cedar_archer");

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
    T3DVertPacked* cells{};
    T3DVertPacked* ground{};
    T3DVertPacked* apron{};
    T3DVertPacked* elevation{};
    T3DVertPacked* trees{};
    T3DVertPacked* landmark{};
    T3DVertPacked* units{};
    T3DVertPacked* cursor{};
    sprite_t* terrain{};
    sprite_t* combat_backdrop{};
    sprite_t* combat_shadow{};
    UnitArt rowan{};
    UnitArt tomas{};
    UnitArt march_sword{};
    UnitArt retainer{};
    UnitArt archer{};
    std::size_t elevation_vertex_count{};
    std::size_t apron_cell_count{};
    std::size_t tree_count{};
    std::size_t landmark_vertex_count{};
    std::size_t unit_count{};
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
    complete,
};

struct ReplayState final {
    ReplayPhase phase{ReplayPhase::select_rowan};
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
CameraPose current_camera{kTacticalCamera};
std::uint32_t frame_count = 0;
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

std::int16_t terrain_lift(std::uint64_t id) {
    if (id == kHills) return 13;
    if (id == kPaved || id == kBell) return 4;
    if (id == kRoad) return 2;
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
    int base_s = 0;
    int base_t = 0;
    if (id == kPaved || id == kBell || id == kHills) {
        base_s = 32;
    } else if (id == kRoad || id == kFarmland) {
        base_t = 32;
    }
    constexpr int offsets[4][2] = {{1, 1}, {3, 1}, {1, 3}, {3, 3}};
    const int variation = static_cast<int>((index * 5U + index / 8U) & 3U);
    const int offset_s = offsets[variation][0];
    const int offset_t = offsets[variation][1];
    s0 = static_cast<std::int16_t>((base_s + offset_s) << 5);
    t0 = static_cast<std::int16_t>((base_t + offset_t) << 5);
    s1 = static_cast<std::int16_t>((base_s + offset_s + 27) << 5);
    t1 = static_cast<std::int16_t>((base_t + offset_t + 27) << 5);
}

std::uint32_t terrain_tint(std::uint64_t id, std::size_t index) {
    const std::uint8_t variation = static_cast<std::uint8_t>((index * 19U) & 0x0FU);
    if (id == kRoad) return pack_rgba(
        static_cast<std::uint8_t>(222U - variation),
        static_cast<std::uint8_t>(194U - variation), 135U);
    if (id == kFarmland) return pack_rgba(
        194U, static_cast<std::uint8_t>(172U - variation / 2U), 105U);
    if (id == kForest) return pack_rgba(
        121U, static_cast<std::uint8_t>(157U - variation), 96U);
    if (id == kPaved || id == kBell) return pack_rgba(
        static_cast<std::uint8_t>(202U - variation),
        static_cast<std::uint8_t>(202U - variation), 187U);
    if (id == kHills) return pack_rgba(
        static_cast<std::uint8_t>(174U - variation),
        static_cast<std::uint8_t>(183U - variation / 2U), 135U);
    return pack_rgba(
        150U, static_cast<std::uint8_t>(184U - variation / 2U), 112U);
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
    std::uint32_t value = kEnvironmentPolicy.seed ^ static_cast<std::uint16_t>(x);
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
    if (edge_terrain == kForest) {
        red = 39; green = 75; blue = 38;
    } else if (edge_terrain == kFarmland) {
        red = 82; green = 101; blue = 48;
    } else if (edge_terrain == kPaved) {
        red = 74; green = 78; blue = 65;
    } else if (edge_terrain == kRoad) {
        red = 83; green = 77; blue = 49;
    } else if (edge_terrain == kHills) {
        red = 61; green = 72; blue = 44;
    }
    const int spread = std::max(1, static_cast<int>(kEnvironmentPolicy.variation));
    const int variation = static_cast<int>(environment_hash(x, z) % spread) -
        spread / 2;
    const int fade = std::max(0, distance - 1) * 5;
    return pack_rgba(
        static_cast<std::uint8_t>(std::clamp(red + variation - fade, 22, 110)),
        static_cast<std::uint8_t>(std::clamp(green + variation - fade, 28, 125)),
        static_cast<std::uint8_t>(std::clamp(blue + variation / 2 - fade, 20, 90))
    );
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
    const std::uint32_t distant = pack_rgba(26U, 38U, 28U);
    set_vertex(board.ground, 0U, ground_x0, -9, ground_z0, distant, 0, 0, normal);
    set_vertex(board.ground, 1U, ground_x1, -9, ground_z0, distant, 0, 0, normal);
    set_vertex(board.ground, 2U, ground_x1, -9, ground_z1, distant, 0, 0, normal);
    set_vertex(board.ground, 3U, ground_x0, -9, ground_z1, distant, 0, 0, normal);

    board.apron_cell_count = 0U;
    const int apron = kEnvironmentPolicy.apron_cells;
    for (int z = -apron; z < static_cast<int>(height) + apron; ++z) {
        for (int x = -apron; x < static_cast<int>(width) + apron; ++x) {
            if (x >= 0 && x < width && z >= 0 && z < height) continue;
            assert(board.apron_cell_count < kMaxApronCells);
            const int nearest_x = std::clamp(x, 0, static_cast<int>(width) - 1);
            const int nearest_z = std::clamp(z, 0, static_cast<int>(height) - 1);
            const std::uint64_t nearest_terrain = board.content->terrain[
                static_cast<std::size_t>(nearest_z * width + nearest_x)
            ];
            const int dx = x < 0 ? -x : (x >= width ? x - width + 1 : 0);
            const int dz = z < 0 ? -z : (z >= height ? z - height + 1 : 0);
            const int distance = std::max(dx, dz);
            std::uint64_t edge_terrain = nearest_terrain;
            if (kEnvironmentPolicy.mode == SurroundingsMode::fixed_terrain) {
                edge_terrain = kEnvironmentPolicy.fixed_terrain;
            } else if (kEnvironmentPolicy.mode == SurroundingsMode::natural_blend &&
                distance > 1 && nearest_terrain != kForest &&
                nearest_terrain != kFarmland) {
                edge_terrain = environment_hash(
                    static_cast<std::int16_t>(x), static_cast<std::int16_t>(z)
                ) % 3U == 0U ? kForest : kFarmland;
            }
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
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        if (board.content->terrain[index] != kBell) continue;
        const auto x = static_cast<std::int16_t>(index % width);
        const auto y = static_cast<std::int16_t>(index / width);
        const std::int16_t cx = world_x(x, width);
        const std::int16_t cz = world_z(y, height);
        const std::int16_t floor = terrain_lift(kBell);
        append_box(cx, floor, cz, 22, 10, 18, pack_rgba(145U, 157U, 161U));
        append_box(cx - 16, floor + 10, cz, 5, 58, 5, pack_rgba(132U, 143U, 148U));
        append_box(cx + 16, floor + 10, cz, 5, 58, 5, pack_rgba(132U, 143U, 148U));
        append_box(cx, floor + 64, cz, 23, 8, 6, pack_rgba(103U, 115U, 121U));

        assert(board.landmark_vertex_count + 6U <= kMaxLandmarkVertices);
        const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
        const std::uint16_t normal = t3d_vert_pack_normal(&up);
        const std::size_t first = board.landmark_vertex_count;
        const std::uint32_t gold = pack_rgba(224U, 171U, 55U);
        const std::uint32_t bronze = pack_rgba(151U, 92U, 31U);
        set_vertex(board.landmark, first + 0U, cx, floor + 62, cz, gold, 0, 0, normal);
        set_vertex(board.landmark, first + 1U, cx - 11, floor + 48, cz, bronze, 0, 0, normal);
        set_vertex(board.landmark, first + 2U, cx, floor + 48, cz - 11, gold, 0, 0, normal);
        set_vertex(board.landmark, first + 3U, cx + 11, floor + 48, cz, gold, 0, 0, normal);
        set_vertex(board.landmark, first + 4U, cx, floor + 48, cz + 11, bronze, 0, 0, normal);
        set_vertex(board.landmark, first + 5U, cx, floor + 34, cz, bronze, 0, 0, normal);
        board.landmark_vertex_count += 6U;
        return;
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
    for (std::size_t index = 0; index < board.content->terrain.size(); ++index) {
        const auto id = board.content->terrain[index];
        if (id != kForest && id != kFarmland) continue;
        const auto x = static_cast<std::int16_t>(index % width);
        const auto z = static_cast<std::int16_t>(index / width);
        append_tree(
            world_x(x, width) + static_cast<std::int16_t>((index & 1U) == 0U ? -4 : 5),
            terrain_lift(id),
            world_z(z, height) + static_cast<std::int16_t>((index & 2U) == 0U ? -3 : 4),
            index,
            id == kForest ? 108 + static_cast<int>((index % 3U) * 8U) : 82
        );
        if (id == kForest && index % 3U == 0U) {
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
    for (int z = -2; z < static_cast<int>(height) + 2; ++z) {
        for (int x = -2; x < static_cast<int>(width) + 2; ++x) {
            if (x >= 0 && x < width && z >= 0 && z < height) continue;
            const std::uint32_t hash = environment_hash(
                static_cast<std::int16_t>(x), static_cast<std::int16_t>(z)
            );
            const int nearest_x = std::clamp(x, 0, static_cast<int>(width) - 1);
            const int nearest_z = std::clamp(z, 0, static_cast<int>(height) - 1);
            const std::uint64_t edge_terrain = board.content->terrain[
                static_cast<std::size_t>(nearest_z * width + nearest_x)
            ];
            const std::uint32_t divisor =
                (edge_terrain == kForest || edge_terrain == kFarmland) ? 3U : 7U;
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

UnitArt* art_for_type(std::uint64_t unit_type) {
    if (unit_type == kRowanType) return &board.rowan;
    if (unit_type == kTomasType) return &board.tomas;
    if (unit_type == kMarchSwordType) return &board.march_sword;
    if (unit_type == kRetainerType) return &board.retainer;
    if (unit_type == kArcherType) return &board.archer;
    return nullptr;
}

sprite_t* sprite_for_type(std::uint64_t unit_type, VisualPose pose = VisualPose::idle) {
    UnitArt* art = art_for_type(unit_type);
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
        const bool walking = motion.active && motion.unit == unit.id &&
            view::walk_cell(
                static_cast<int>(frame_count - motion.started),
                static_cast<int>(motion.total_frames)
            ) != view::sequence_cell_stand;
        return walking ? VisualPose::moving : VisualPose::idle;
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
        if (sprite_for_type(unit.unit_type_id) == nullptr) continue;
        const std::size_t terrain_index = static_cast<std::size_t>(unit.position.y) * width +
            static_cast<std::size_t>(unit.position.x);
        std::int16_t lift = terrain_index < board.content->terrain.size()
            ? terrain_lift(board.content->terrain[terrain_index])
            : 0;
        std::int16_t cx = world_x(unit.position.x, width);
        std::int16_t cz = world_z(unit.position.y, height);
        if (motion.active && motion.unit == unit.id) {
            const std::uint32_t elapsed = frame_count - motion.started;
            const std::size_t route_index = std::min<std::size_t>(
                elapsed / view::slide_frames_per_tile,
                motion.route_count - 1U
            );
            const sim::Position segment_from = route_index == 0U
                ? motion.from : motion.route[route_index - 1U];
            const sim::Position segment_to = motion.route[route_index];
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
        }
        const std::int16_t right_x = 17;
        const std::int16_t right_z = 17;
        const std::int16_t sprite_height = 72;
        const std::int16_t uv_end = static_cast<std::int16_t>(
            64 << 5
        );
        const std::int16_t bottom = lift + 3;
        const std::size_t first = board.unit_count * kUnitVertexCount;
        const std::uint32_t white = pack_rgba(255U, 255U, 255U);
        set_vertex(board.units, first + 0U, cx - right_x, bottom, cz - right_z, white, 0, uv_end, normal);
        set_vertex(board.units, first + 1U, cx + right_x, bottom, cz + right_z, white, uv_end, uv_end, normal);
        set_vertex(board.units, first + 2U, cx + right_x, bottom + sprite_height, cz + right_z, white, uv_end, 0, normal);
        set_vertex(board.units, first + 3U, cx - right_x, bottom + sprite_height, cz - right_z, white, 0, 0, normal);
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
    const sim::Position cursor = board.game.cursor();
    const auto width = board.content->definition.width;
    const auto height = board.content->definition.height;
    const std::size_t terrain_index =
        static_cast<std::size_t>(cursor.y) * width +
        static_cast<std::size_t>(cursor.x);
    const std::int16_t floor = static_cast<std::int16_t>(
        (terrain_index < board.content->terrain.size()
             ? terrain_lift(board.content->terrain[terrain_index]) : 0) + 3
    );
    const std::int16_t cx = world_x(cursor.x, width);
    const std::int16_t cz = world_z(cursor.y, height);
    constexpr std::int16_t outer = kHalfCell - 2;
    constexpr std::int16_t inner = kHalfCell - 6;
    const std::uint32_t colour = board.game.selected() == 0
        ? pack_rgba(246U, 190U, 58U)
        : pack_rgba(107U, 221U, 255U);
    cursor_quad(0U, cx - outer, cz - outer, cx + outer, cz - inner, floor, colour);
    cursor_quad(4U, cx - outer, cz + inner, cx + outer, cz + outer, floor, colour);
    cursor_quad(8U, cx - outer, cz - inner, cx - inner, cz + inner, floor, colour);
    cursor_quad(12U, cx + inner, cz - inner, cx + outer, cz + inner, floor, colour);
}

void load_runtime_board() {
    const bool loaded = board.runtime.load(
        "rom:/steel-and-cedar.gpk",
        kBellOrchard,
        1
    );
    assertf(
        loaded,
        "Steel and Cedar runtime load failed: %u",
        static_cast<unsigned>(board.runtime.error())
    );
    board.content = &board.runtime.encounter();
    assertf(board.content->definition.width == 8 && board.content->definition.height == 6,
        "Unexpected Bell Orchard dimensions");
    for (std::size_t index = 0; index < board.content->placements.size(); ++index) {
        const auto source = board.content->placements[index].source_key_id;
        const auto unit = board.content->placements[index].unit_id;
        if (source == kRowanMember) board.rowan_id = unit;
        if (index < board.content->definition.units.size() &&
            board.content->definition.units[index].unit_type_id == kTomasType) {
            board.tomas_id = unit;
        }
    }
    assert(board.rowan_id != 0 && board.tomas_id != 0);
    assertf(
        board.game.open(board.content->definition, board.rowan_id),
        "Steel and Cedar simulation could not be created"
    );
    const sim::UnitSnapshot& rowan = *std::find_if(
        board.game.snapshot().units.begin(), board.game.snapshot().units.end(),
        [](const sim::UnitSnapshot& unit) { return unit.id == board.rowan_id; }
    );
    replay.rowan_destination = rowan.position;
    for (const sim::Position candidate :
         sim::reachable_tiles(board.game.snapshot(), board.rowan_id)) {
        if (candidate == rowan.position) continue;
        if (candidate.x > replay.rowan_destination.x ||
            (candidate.x == replay.rowan_destination.x &&
             candidate.y < replay.rowan_destination.y)) {
            replay.rowan_destination = candidate;
        }
    }
    assertf(
        replay.rowan_destination != rowan.position,
        "Bell Orchard must give Rowan a legal movement destination"
    );
    board.cells = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxCells * kCellVertexCount / 2U)));
    board.ground = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kGroundVertexCount / 2U)));
    board.apron = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxApronCells * kCellVertexCount / 2U)));
    board.elevation = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxElevationVertices / 2U)));
    board.trees = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxTrees * kTreeVertexCount / 2U)));
    board.landmark = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxLandmarkVertices / 2U)));
    board.units = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxUnits * kUnitVertexCount / 2U)));
    board.cursor = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kCursorVertexCount / 2U)));
    assert(board.cells && board.ground && board.apron && board.elevation &&
        board.trees && board.landmark && board.units && board.cursor);
    board.terrain = sprite_load("rom:/material_atlas.sprite");
    board.combat_backdrop = sprite_load("rom:/combat_backdrop.sprite");
    board.combat_shadow = sprite_load("rom:/soft_shadow.sprite");
    board.rowan = {
        sprite_load("rom:/rowan.sprite"),
        sprite_load("rom:/rowan_move.sprite"),
        sprite_load("rom:/rowan_close_idle.sprite"),
        sprite_load("rom:/rowan_close_attack.sprite"),
        sprite_load("rom:/rowan_close_hurt.sprite"),
    };
    board.tomas = {
        sprite_load("rom:/tomas.sprite"),
        sprite_load("rom:/tomas_move.sprite"),
        sprite_load("rom:/tomas_close_idle.sprite"),
        sprite_load("rom:/tomas_close_attack.sprite"),
        sprite_load("rom:/tomas_close_hurt.sprite"),
    };
    board.march_sword = {
        sprite_load("rom:/march_sword.sprite"),
        sprite_load("rom:/march_sword_move.sprite"),
        sprite_load("rom:/march_sword_close_idle.sprite"),
        sprite_load("rom:/march_sword_close_attack.sprite"),
        sprite_load("rom:/march_sword_close_hurt.sprite"),
    };
    board.retainer = {
        sprite_load("rom:/retainer.sprite"),
        sprite_load("rom:/retainer_move.sprite"),
        sprite_load("rom:/retainer_close_idle.sprite"),
        sprite_load("rom:/retainer_close_attack.sprite"),
        sprite_load("rom:/retainer_close_hurt.sprite"),
    };
    board.archer = {
        sprite_load("rom:/archer.sprite"),
        sprite_load("rom:/archer_move.sprite"),
        sprite_load("rom:/archer_close_idle.sprite"),
        sprite_load("rom:/archer_close_attack.sprite"),
        sprite_load("rom:/archer_close_hurt.sprite"),
    };
    const auto complete_art = [](const UnitArt& art) {
        return art.idle && art.moving && art.close_idle && art.attack && art.hurt;
    };
    assert(board.terrain && board.combat_backdrop && board.combat_shadow &&
        complete_art(board.rowan) && complete_art(board.tomas) &&
        complete_art(board.march_sword) && complete_art(board.retainer) &&
        complete_art(board.archer));
    assertf(board.shadows.open("rom:/soft_shadow.sprite"), "Soft shadow asset failed");
    build_environment_vertices();
    build_cell_vertices();
    build_elevation_vertices();
    build_tree_vertices();
    build_landmark_vertices();
    build_unit_vertices();
    build_cursor_vertices();
    board.loaded = true;
    debugf(
        "GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none "
        "surroundings=natural_blend hud_default=off\n"
    );
    debugf(
        "GRANDLEON_PYRITE_RUNTIME package=%lu map=%llu size=%ux%u terrain=%lu "
        "units=%lu rowan=%d,%d hp=%d apron=%lu trees=%lu elevation=%lu "
        "landmark=%lu renderer=pyrite-tiny3d\n",
        static_cast<unsigned long>(board.runtime.package_size()), board.content->map_id,
        board.content->definition.width, board.content->definition.height,
        static_cast<unsigned long>(board.content->terrain.size()),
        static_cast<unsigned long>(board.content->definition.units.size()),
        board.rowan_x, board.rowan_y, board.rowan_health,
        static_cast<unsigned long>(board.apron_cell_count),
        static_cast<unsigned long>(board.tree_count),
        static_cast<unsigned long>(board.elevation_vertex_count),
        static_cast<unsigned long>(board.landmark_vertex_count)
    );
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
    t3d_vert_load(
        board.landmark, 0,
        static_cast<std::uint32_t>(board.landmark_vertex_count)
    );
    draw_box(0U);
    draw_box(8U);
    draw_box(16U);
    draw_box(24U);
    constexpr std::uint8_t bell = 32U;
    t3d_tri_draw(bell + 0U, bell + 1U, bell + 2U);
    t3d_tri_draw(bell + 0U, bell + 2U, bell + 3U);
    t3d_tri_draw(bell + 0U, bell + 3U, bell + 4U);
    t3d_tri_draw(bell + 0U, bell + 4U, bell + 1U);
    t3d_tri_draw(bell + 5U, bell + 2U, bell + 1U);
    t3d_tri_draw(bell + 5U, bell + 3U, bell + 2U);
    t3d_tri_draw(bell + 5U, bell + 4U, bell + 3U);
    t3d_tri_draw(bell + 5U, bell + 1U, bell + 4U);
    t3d_tri_sync();
}

void draw_trees() {
    rdpq_mode_begin();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
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
        sprite_t* sprite = sprite_for_type(unit.unit_type_id, board_pose);
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
    t3d_vert_load(board.cursor, 0, kCursorVertexCount);
    for (std::uint8_t first = 0; first < kCursorVertexCount; first += 4U) {
        t3d_tri_draw(first + 0U, first + 1U, first + 2U);
        t3d_tri_draw(first + 0U, first + 2U, first + 3U);
    }
    t3d_tri_sync();
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

bool issue(gp::GameplaySlice::Action action) {
    if (action == gp::GameplaySlice::Action::confirm &&
        board.game.selected() != 0U) {
        plan_motion(
            board.game.snapshot(), board.game.selected(), board.game.cursor()
        );
    }
    const bool handled = board.game.input(action);
    remember_motion();
    build_cursor_vertices();
    return handled;
}

bool nudge_toward(sim::Position destination) {
    const sim::Position cursor = board.game.cursor();
    if (cursor.x < destination.x) return issue(gp::GameplaySlice::Action::right);
    if (cursor.x > destination.x) return issue(gp::GameplaySlice::Action::left);
    if (cursor.y < destination.y) return issue(gp::GameplaySlice::Action::down);
    if (cursor.y > destination.y) return issue(gp::GameplaySlice::Action::up);
    return false;
}

bool physical_input() {
    const joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (combat.active) {
        if (pressed.b) finish_combat(true);
        if (pressed.l) combat.reduced_motion = !combat.reduced_motion;
        return pressed.b || pressed.l || pressed.r;
    }
    if (pressed.start) {
        combat.enabled = !combat.enabled;
        replay.user_took_over = true;
        return true;
    }
    if (pressed.c_up) {
        debug_hud = !debug_hud;
        replay.user_took_over = true;
        return true;
    }
    if (pressed.l) {
        combat.reduced_motion = !combat.reduced_motion;
        replay.user_took_over = true;
        return true;
    }
    gp::GameplaySlice::Action action{};
    bool found = true;
    if (pressed.d_left) action = gp::GameplaySlice::Action::left;
    else if (pressed.d_right) action = gp::GameplaySlice::Action::right;
    else if (pressed.d_up) action = gp::GameplaySlice::Action::up;
    else if (pressed.d_down) action = gp::GameplaySlice::Action::down;
    else if (pressed.a) action = gp::GameplaySlice::Action::confirm;
    else if (pressed.b) action = gp::GameplaySlice::Action::cancel;
    else if (pressed.z) action = gp::GameplaySlice::Action::wait;
    else found = false;
    if (!found) return false;
    replay.user_took_over = true;
    issue(action);
    return true;
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

bool advance_opponent() {
    const sim::EncounterSnapshot& snapshot = board.game.snapshot();
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.side != snapshot.active_side || !sim::on_board(unit) ||
            unit.has_acted) {
            continue;
        }
        const tac::Plan plan = tac::decide(
            snapshot, unit.id, behavior_for(unit.id), patrol_for(unit.id),
            board.game.encounter().abilities(),
            board.game.encounter().weapons()
        );
        sim::Command command = plan.actionable
            ? plan.command
            : sim::Command{sim::CommandType::wait, unit.id, {}, 0, 0, 0, 0};
        const sim::UnitSnapshot actor_before = unit;
        const sim::UnitSnapshot* target = command.type == sim::CommandType::attack
            ? find_unit(command.target_id) : nullptr;
        const sim::UnitSnapshot target_before = target != nullptr
            ? *target : sim::UnitSnapshot{};
        if (command.type == sim::CommandType::move) {
            plan_motion(snapshot, command.unit_id, command.destination);
        }
        if (!board.game.apply(command)) {
            command = {sim::CommandType::wait, unit.id, {}, 0, 0, 0, 0};
            assertf(board.game.apply(command), "Opponent fallback was refused");
        }
        remember_motion();
        if (command.type == sim::CommandType::attack) {
            assert(target != nullptr);
            start_combat(actor_before, target_before);
        }
        ++replay.opponent_commands;
        if (!replay.enemy_checkpoint) {
            debugf("CHECKPOINT PYRITE_ENEMY\n");
            replay.enemy_checkpoint = true;
        }
        return true;
    }
    return false;
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
    switch (replay.phase) {
        case ReplayPhase::select_rowan:
            assertf(issue(gp::GameplaySlice::Action::confirm), "Rowan select failed");
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
            assertf(issue(gp::GameplaySlice::Action::confirm), "Rowan move failed");
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
            assertf(issue(gp::GameplaySlice::Action::confirm), "Moved Rowan select failed");
            replay.phase = ReplayPhase::wait_rowan;
            replay.next_frame = frame_count + 6U;
            break;
        case ReplayPhase::wait_rowan:
            assertf(issue(gp::GameplaySlice::Action::wait), "Rowan wait failed");
            replay.phase = ReplayPhase::aim_tomas;
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
            assertf(issue(gp::GameplaySlice::Action::confirm), "Tomas select failed");
            assert(board.game.selected() == board.tomas_id);
            replay.phase = ReplayPhase::wait_tomas;
            replay.next_frame = frame_count + 6U;
            break;
        case ReplayPhase::wait_tomas:
            assertf(issue(gp::GameplaySlice::Action::wait), "Tomas wait failed");
            replay.phase = ReplayPhase::opponent;
            replay.next_frame = frame_count + 12U;
            break;
        case ReplayPhase::opponent:
            if (board.game.snapshot().active_side == sim::Side::first &&
                replay.opponent_commands > 0U) {
                replay.phase = ReplayPhase::complete;
                if (!replay.round_checkpoint) {
                    debugf("CHECKPOINT PYRITE_ROUND\n");
                    replay.round_checkpoint = true;
                }
                performance_sample_started = true;
                performance_frames = 0;
                sample_start = 0;
                previous_frame = 0;
                min_frame_ticks = ~std::uint64_t{0};
                max_frame_ticks = 0;
            } else {
                assertf(advance_opponent(), "No opposing command was available");
                replay.next_frame = frame_count +
                    (motion.active ? motion.total_frames + 2U : 12U);
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
    sprite_t* left_sprite = sprite_for_type(left->unit_type_id, left_pose);
    sprite_t* right_sprite = sprite_for_type(right->unit_type_id, right_pose);
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
                result = combat.first_missed ? "MISS" : "HIT";
                amount = combat.first_damage;
            }
        } else if (combat.phase == CombatPhase::counter) {
            right_health = combat.defender_health_after;
            if (combat.phase_ticks >= 30U) {
                left_health = combat.attacker_health_after;
                result = combat.counter_missed ? "MISS" : "COUNTER";
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

}  // namespace

namespace P64::GlobalScript::C47524E444C454F4 {

void onGameInit() {
    load_runtime_board();
}

void onSceneUpdate() {
    if (!camera_configured) {
        apply_camera(kTacticalCamera);
        camera_configured = true;
    }
    physical_input();
    update_combat();
    update_replay();
    if (motion.active && frame_count - motion.started >= motion.total_frames) {
        motion.active = false;
    }
    if (restore_checkpoint_delay > 0U && --restore_checkpoint_delay == 0U &&
        !combat.restore_checkpoint) {
        debugf("CHECKPOINT PYRITE_COMBAT_RESTORE\n");
        combat.restore_checkpoint = true;
    }
    build_unit_vertices();
    build_cursor_vertices();

    const std::uint64_t now = get_ticks();
    if (performance_sample_started && !verdict_reported) {
        if (previous_frame == 0U) {
            sample_start = now;
        } else {
            const std::uint64_t interval = now - previous_frame;
            min_frame_ticks = std::min(min_frame_ticks, interval);
            max_frame_ticks = std::max(max_frame_ticks, interval);
            ++performance_frames;
        }
        previous_frame = now;
    }
    ++frame_count;
    if (performance_sample_started && performance_frames == 120U &&
        !verdict_reported) {
        verdict_reported = true;
        heap_stats_t heap{};
        sys_get_heap_stats(&heap);
        const std::uint64_t elapsed = now - sample_start;
        const sim::UnitSnapshot* rowan = find_unit(board.rowan_id);
        assert(rowan && rowan->position == replay.rowan_destination);
        assert(board.game.encounter().canonical_hash() != board.game.opening_hash());
        assert(board.game.accepted_commands() >= 4U);
        assert(replay.opponent_commands > 0U);
        assert(board.game.snapshot().round >= 1U);
        assert(!combat.active && combat.exchanges > 0U);
        assertf(
            get_memory_size() <= 4 * 1024 * 1024,
            "Pyrite gameplay slice requires more than base N64 memory"
        );
        debugf(
            "GRANDLEON_PYRITE_GAMEPLAY frames=120 avg_frame_us=%llu min_frame_us=%llu "
            "max_frame_us=%llu fps_x100=%lu heap_used=%lu heap_total=%lu heap_free=%lu "
            "rdram=%d commands=%lu enemy_commands=%lu round=%lu combat=%lu "
            "rowan=%d,%d hash=%llu\n",
            TICKS_TO_US(elapsed) / 120U, TICKS_TO_US(min_frame_ticks),
            TICKS_TO_US(max_frame_ticks),
            static_cast<unsigned long>(VI::SwapChain::getFPS() * 100.0F),
            static_cast<unsigned long>(heap.used), static_cast<unsigned long>(heap.total),
            static_cast<unsigned long>(heap.total - heap.used), get_memory_size(),
            static_cast<unsigned long>(board.game.accepted_commands()),
            static_cast<unsigned long>(replay.opponent_commands),
            static_cast<unsigned long>(board.game.snapshot().round),
            static_cast<unsigned long>(combat.exchanges),
            rowan->position.x, rowan->position.y,
            board.game.encounter().canonical_hash()
        );
        debugf("RESULT PASS PYRITE_GAMEPLAY\n");
    }
}

void onScenePreDraw3D() {
    if (!board.loaded) return;
    draw_environment();
    draw_elevation();
    draw_board_cells();
    draw_landmark();
    draw_trees();
    board.shadows.draw();
    draw_cursor();
    draw_units();
}

void onSceneDraw2D() {
    draw_combat_stage_2d();
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
        6, 7, "BELL ORCHARD  R%lu  ROWAN %dHP  %dFPS",
        static_cast<unsigned long>(board.game.snapshot().round + 1U),
        board.rowan_health,
        static_cast<int>(VI::SwapChain::getFPS())
    );
    Debug::printf(
        6, 19, "%d,%d %s  A MOVE Z WAIT  CAM %s",
        board.game.cursor().x, board.game.cursor().y,
        board.game.selected() == 0 ? "READY" : "SELECTED",
        combat.enabled ? "ON" : "OFF"
    );
}

}  // namespace P64::GlobalScript::C47524E444C454F4
