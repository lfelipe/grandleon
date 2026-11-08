#include "script/globalScript.h"

#include <grandleon/core/content_identity.hpp>
#include <libdragon.h>
#include <t3d/t3d.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "debug/debugDraw.h"
#include "GpkRuntimeAdapter.hpp"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "vi/swapChain.h"

namespace core = grandleon::core;
namespace pr = grandleon::package_runtime;
namespace gp = grandleon::nintendo64::pyrite;

namespace {

constexpr std::int16_t kCell = 52;
constexpr std::int16_t kHalfCell = kCell / 2 - 1;
constexpr std::size_t kCellVertexCount = 4;
constexpr std::size_t kMaxCells = 8U * 6U;
constexpr std::size_t kBatchCells = 15U;
constexpr std::size_t kTreeVertexCount = 12U;
constexpr std::size_t kMaxTrees = 20U;
constexpr std::size_t kUnitVertexCount = 4U;
constexpr std::size_t kMaxUnits = 8U;

constexpr auto kBellOrchard =
    core::stable_content_id_v1("steel_and_cedar/bell_orchard");
constexpr auto kRoad = core::stable_content_id_v1("road");
constexpr auto kFarmland = core::stable_content_id_v1("farmland_orchard_tree");
constexpr auto kForest = core::stable_content_id_v1("forest_orchard_tree");
constexpr auto kPaved = core::stable_content_id_v1("paved");
constexpr auto kBell = core::stable_content_id_v1("paved_orchard_bell");
constexpr auto kHills = core::stable_content_id_v1("hills");

constexpr auto kRowanMember = core::stable_content_id_v1("rowan");
constexpr auto kRowanType = core::stable_content_id_v1("rowan_vale");
constexpr auto kTomasType = core::stable_content_id_v1("tomas_reed");
constexpr auto kMarchSwordType = core::stable_content_id_v1("march_sword");
constexpr auto kRetainerType = core::stable_content_id_v1("red_retainer");
constexpr auto kArcherType = core::stable_content_id_v1("cedar_archer");

struct BoardResources final {
    gp::GpkRuntimeAdapter runtime{};
    const pr::EncounterLoadResult* encounter{};
    T3DVertPacked* cells{};
    T3DVertPacked* trees{};
    T3DVertPacked* units{};
    sprite_t* terrain{};
    sprite_t* rowan{};
    sprite_t* tomas{};
    sprite_t* march_sword{};
    sprite_t* retainer{};
    sprite_t* archer{};
    std::size_t tree_count{};
    std::size_t unit_count{};
    std::int16_t rowan_x{-1};
    std::int16_t rowan_y{-1};
    std::int16_t rowan_health{};
    bool loaded{};
};

BoardResources board;
std::uint32_t frame_count = 0;
std::uint64_t sample_start = 0;
std::uint64_t previous_frame = 0;
std::uint64_t min_frame_ticks = ~std::uint64_t{0};
std::uint64_t max_frame_ticks = 0;
bool camera_configured = false;

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
    const auto width = board.encounter->definition.width;
    const auto height = board.encounter->definition.height;
    const fm_vec3_t up = {{0.0F, 1.0F, 0.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&up);
    for (std::size_t index = 0; index < board.encounter->terrain.size(); ++index) {
        const std::int16_t x = static_cast<std::int16_t>(index % width);
        const std::int16_t z = static_cast<std::int16_t>(index / width);
        const std::int16_t cx = world_x(x, width);
        const std::int16_t cz = world_z(z, height);
        const std::int16_t lift = terrain_lift(board.encounter->terrain[index]);
        std::int16_t s0 = 0;
        std::int16_t t0 = 0;
        std::int16_t s1 = 0;
        std::int16_t t1 = 0;
        terrain_uv(board.encounter->terrain[index], index, s0, t0, s1, t1);
        const std::uint32_t tint = terrain_tint(board.encounter->terrain[index], index);
        const std::size_t first = index * kCellVertexCount;
        set_vertex(board.cells, first + 0U, cx - kHalfCell, lift, cz - kHalfCell, tint, s0, t0, normal);
        set_vertex(board.cells, first + 1U, cx + kHalfCell, lift, cz - kHalfCell, tint, s1, t0, normal);
        set_vertex(board.cells, first + 2U, cx + kHalfCell, lift, cz + kHalfCell, tint, s1, t1, normal);
        set_vertex(board.cells, first + 3U, cx - kHalfCell, lift, cz + kHalfCell, tint, s0, t1, normal);
    }
}

void append_tree(std::int16_t cx, std::int16_t floor, std::int16_t cz, std::size_t variant) {
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
    set_vertex(board.trees, first + 0U, cx - 5, floor, cz - 5, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 1U, cx + 5, floor, cz - 5, bark_b, 0, 0, normal);
    set_vertex(board.trees, first + 2U, cx + 5, floor, cz + 5, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 3U, cx - 5, floor, cz + 5, bark_b, 0, 0, normal);
    set_vertex(board.trees, first + 4U, cx + lean, floor + 34, cz, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 5U, cx, floor + 24, cz, bark_a, 0, 0, normal);
    set_vertex(board.trees, first + 6U, cx + lean, floor + 74, cz, leaf_b, 0, 0, normal);
    set_vertex(board.trees, first + 7U, cx - 21, floor + 43, cz, leaf_a, 0, 0, normal);
    set_vertex(board.trees, first + 8U, cx, floor + 43, cz - 21, leaf_b, 0, 0, normal);
    set_vertex(board.trees, first + 9U, cx + 21, floor + 43, cz, leaf_a, 0, 0, normal);
    set_vertex(board.trees, first + 10U, cx, floor + 43, cz + 21, leaf_b, 0, 0, normal);
    set_vertex(board.trees, first + 11U, cx + lean, floor + 25, cz, leaf_a, 0, 0, normal);
    ++board.tree_count;
}

void build_tree_vertices() {
    const auto width = board.encounter->definition.width;
    const auto height = board.encounter->definition.height;
    for (std::size_t index = 0; index < board.encounter->terrain.size(); ++index) {
        const auto id = board.encounter->terrain[index];
        if (id != kForest && id != kFarmland) continue;
        const auto x = static_cast<std::int16_t>(index % width);
        const auto z = static_cast<std::int16_t>(index / width);
        append_tree(
            world_x(x, width) + static_cast<std::int16_t>((index & 1U) == 0U ? -4 : 5),
            terrain_lift(id),
            world_z(z, height) + static_cast<std::int16_t>((index & 2U) == 0U ? -3 : 4),
            index
        );
    }
}

sprite_t* sprite_for_type(std::uint64_t unit_type) {
    if (unit_type == kRowanType) return board.rowan;
    if (unit_type == kTomasType) return board.tomas;
    if (unit_type == kMarchSwordType) return board.march_sword;
    if (unit_type == kRetainerType) return board.retainer;
    if (unit_type == kArcherType) return board.archer;
    return nullptr;
}

void build_unit_vertices() {
    const auto width = board.encounter->definition.width;
    const auto height = board.encounter->definition.height;
    const fm_vec3_t forward = {{0.0F, 0.0F, 1.0F}};
    const std::uint16_t normal = t3d_vert_pack_normal(&forward);
    constexpr std::int16_t right_x = 17;
    constexpr std::int16_t right_z = 17;
    constexpr std::int16_t sprite_height = 72;
    for (std::size_t index = 0;
         index < board.encounter->definition.units.size() && index < kMaxUnits;
         ++index) {
        const auto& unit = board.encounter->definition.units[index];
        if (sprite_for_type(unit.unit_type_id) == nullptr) continue;
        const std::size_t terrain_index = static_cast<std::size_t>(unit.position.y) * width +
            static_cast<std::size_t>(unit.position.x);
        const std::int16_t lift = terrain_index < board.encounter->terrain.size()
            ? terrain_lift(board.encounter->terrain[terrain_index])
            : 0;
        const std::int16_t cx = world_x(unit.position.x, width);
        const std::int16_t cz = world_z(unit.position.y, height);
        const std::int16_t bottom = lift + 3;
        const std::size_t first = board.unit_count * kUnitVertexCount;
        const std::uint32_t white = pack_rgba(255U, 255U, 255U);
        set_vertex(board.units, first + 0U, cx - right_x, bottom, cz - right_z, white, 0, 64 << 5, normal);
        set_vertex(board.units, first + 1U, cx + right_x, bottom, cz + right_z, white, 64 << 5, 64 << 5, normal);
        set_vertex(board.units, first + 2U, cx + right_x, bottom + sprite_height, cz + right_z, white, 64 << 5, 0, normal);
        set_vertex(board.units, first + 3U, cx - right_x, bottom + sprite_height, cz - right_z, white, 0, 0, normal);
        if (index < board.encounter->placements.size() &&
            board.encounter->placements[index].source_key_id == kRowanMember) {
            board.rowan_x = unit.position.x;
            board.rowan_y = unit.position.y;
            board.rowan_health = unit.health;
        }
        ++board.unit_count;
    }
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
    board.encounter = &board.runtime.encounter();
    assertf(board.encounter->definition.width == 8 && board.encounter->definition.height == 6,
        "Unexpected Bell Orchard dimensions");
    board.cells = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxCells * kCellVertexCount / 2U)));
    board.trees = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxTrees * kTreeVertexCount / 2U)));
    board.units = static_cast<T3DVertPacked*>(malloc_uncached(
        sizeof(T3DVertPacked) * (kMaxUnits * kUnitVertexCount / 2U)));
    assert(board.cells && board.trees && board.units);
    board.terrain = sprite_load("rom:/material_atlas.sprite");
    board.rowan = sprite_load("rom:/rowan.sprite");
    board.tomas = sprite_load("rom:/tomas.sprite");
    board.march_sword = sprite_load("rom:/march_sword.sprite");
    board.retainer = sprite_load("rom:/retainer.sprite");
    board.archer = sprite_load("rom:/archer.sprite");
    assert(board.terrain && board.rowan && board.tomas && board.march_sword &&
        board.retainer && board.archer);
    build_cell_vertices();
    build_tree_vertices();
    build_unit_vertices();
    board.loaded = true;
    debugf(
        "GRANDLEON_PYRITE_RUNTIME package=%lu map=%llu size=%ux%u terrain=%lu "
        "units=%lu rowan=%d,%d hp=%d trees=%lu\n",
        static_cast<unsigned long>(board.runtime.package_size()), board.encounter->map_id,
        board.encounter->definition.width, board.encounter->definition.height,
        static_cast<unsigned long>(board.encounter->terrain.size()),
        static_cast<unsigned long>(board.encounter->definition.units.size()),
        board.rowan_x, board.rowan_y, board.rowan_health,
        static_cast<unsigned long>(board.tree_count)
    );
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
    const std::size_t cell_count = board.encounter->terrain.size();
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
    for (const auto& unit : board.encounter->definition.units) {
        sprite_t* sprite = sprite_for_type(unit.unit_type_id);
        if (sprite == nullptr || visual_index >= board.unit_count) continue;
        rdpq_sprite_upload(TILE0, sprite, nullptr);
        t3d_vert_load(board.units + visual_index * 2U, 0, 4);
        t3d_tri_draw(0, 1, 2);
        t3d_tri_draw(0, 2, 3);
        t3d_tri_sync();
        ++visual_index;
    }
}

}  // namespace

namespace P64::GlobalScript::C47524E444C454F4 {

void onGameInit() {
    load_runtime_board();
}

void onSceneUpdate() {
    if (!camera_configured) {
        auto* camera = SceneManager::getCurrent().getCamera();
        assert(camera);
        camera->setOrthographic(2.65F);
        camera->setLookAt({-5.5F, 6.8F, 5.5F}, {0.0F, 0.20F, 0.0F}, {0.0F, 1.0F, 0.0F});
        camera_configured = true;
    }
    const std::uint64_t now = get_ticks();
    if (frame_count == 60U) {
        sample_start = now;
        previous_frame = now;
    } else if (frame_count > 60U && frame_count <= 180U) {
        const std::uint64_t interval = now - previous_frame;
        min_frame_ticks = std::min(min_frame_ticks, interval);
        max_frame_ticks = std::max(max_frame_ticks, interval);
        previous_frame = now;
    }
    ++frame_count;
    if (frame_count == 181U) {
        heap_stats_t heap{};
        sys_get_heap_stats(&heap);
        const std::uint64_t elapsed = now - sample_start;
        debugf(
            "GRANDLEON_PYRITE_COMPARE frames=120 avg_frame_us=%llu min_frame_us=%llu "
            "max_frame_us=%llu fps_x100=%lu heap_used=%lu heap_total=%lu heap_free=%lu\n",
            TICKS_TO_US(elapsed) / 120U, TICKS_TO_US(min_frame_ticks),
            TICKS_TO_US(max_frame_ticks),
            static_cast<unsigned long>(VI::SwapChain::getFPS() * 100.0F),
            static_cast<unsigned long>(heap.used), static_cast<unsigned long>(heap.total),
            static_cast<unsigned long>(heap.total - heap.used)
        );
        debugf("RESULT PASS PYRITE_COMPARE\n");
    }
}

void onScenePreDraw3D() {
    if (!board.loaded) return;
    draw_board_cells();
    draw_trees();
    draw_units();
}

void onSceneDraw2D() {
    Debug::printStart();
    Debug::printf(8, 8, "STEEL + CEDAR / BELL ORCHARD");
    Debug::printf(
        8, 20, "REAL GPK  ROWAN %dHP  %ux%u  FPS %d", board.rowan_health,
        board.encounter->definition.width, board.encounter->definition.height,
        static_cast<int>(VI::SwapChain::getFPS())
    );
}

}  // namespace P64::GlobalScript::C47524E444C454F4
