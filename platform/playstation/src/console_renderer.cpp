// SPDX-License-Identifier: MIT

#include "console_renderer.h"

#include <algorithm>

#include "combat_backdrop_ci8.h"
#include "psx_gpu.h"

namespace grandleon::playstation::console_renderer {
namespace {

namespace gpu = grandleon::playstation::gpu;

constexpr int first_character_page = gpu::first_texture_page + 2;
constexpr int drawings_per_page = 16;
constexpr int cutouts_across = 4;
constexpr int cutout_halfwords = art::character_side / 4;
constexpr int first_character_clut = 64;
// Page 6 is the single clear page between board terrain (page 5) and the idle
// character atlas (pages 7..10). Keeping animation here leaves pages 11..15
// available for the full-frame CI8 combat plate.
constexpr int stream_page = gpu::first_texture_page + 1;
constexpr int stream_clut = 192;
constexpr int backdrop_texture_page = gpu::first_texture_page + 6;
constexpr int backdrop_overlay_page = gpu::first_texture_page + 9;
constexpr int backdrop_clut_x = 0;
constexpr int backdrop_overlay_clut_x = 256;
constexpr int backdrop_clut_y = gpu::clut_y + gpu::clut_rows - 1;
constexpr int character_page_count =
    (art::character_count + drawings_per_page - 1) / drawings_per_page;
static_assert(
    first_character_page + character_page_count <= backdrop_texture_page,
    "the selected project's idle cutouts do not fit before the close backdrop"
);

struct Location final {
    int page;
    int u;
    int v;
    int clut;
};

Location locations[art::character_count > 0 ? art::character_count : 1]{};
bool ready = false;
int streamed_drawing[2] = {-1, -1};
art::Pose streamed_pose[2] = {art::Pose::idle, art::Pose::idle};

void upload_clut(int slot, const unsigned short* entries) {
    gpu::upload(
        gpu::clut_x_of(slot), gpu::clut_y_of(slot), art::clut_size, 1, entries
    );
}

void upload_cutout(
    int page, int u, int v, const unsigned short* texels
) {
    gpu::upload(
        page * gpu::page_halfwords + u / 4,
        gpu::texture_page_y + v,
        cutout_halfwords, art::character_side, texels
    );
}

void upload_close_backdrop() {
    constexpr int tiles_per_page = 6;
    for (int index = 0; index < grandleon_psx_backdrop_tile_count; ++index) {
        const int page = backdrop_texture_page + index / tiles_per_page;
        const int within = index % tiles_per_page;
        const int u = (within % 2) * grandleon_psx_backdrop_tile_width;
        const int v = (within / 2) * grandleon_psx_backdrop_tile_height;
        gpu::upload(
            page * gpu::page_halfwords + u / 2,
            gpu::texture_page_y + v,
            grandleon_psx_backdrop_tile_width / 2,
            grandleon_psx_backdrop_tile_height,
            grandleon_psx_backdrop_tiles[index]
        );
    }
    for (int index = 0;
         index < grandleon_psx_backdrop_overlay_tile_count; ++index) {
        const int page = backdrop_overlay_page + index / tiles_per_page;
        const int within = index % tiles_per_page;
        const int u = (within % 2) *
            grandleon_psx_backdrop_overlay_tile_width;
        const int v = (within / 2) *
            grandleon_psx_backdrop_overlay_tile_height;
        gpu::upload(
            page * gpu::page_halfwords + u / 2,
            gpu::texture_page_y + v,
            grandleon_psx_backdrop_overlay_tile_width / 2,
            grandleon_psx_backdrop_overlay_tile_height,
            grandleon_psx_backdrop_overlay_tiles[index]
        );
    }
    gpu::upload(
        backdrop_clut_x, backdrop_clut_y, 256, 1,
        grandleon_psx_backdrop_clut
    );
    gpu::upload(
        backdrop_overlay_clut_x, backdrop_clut_y, 256, 1,
        grandleon_psx_backdrop_overlay_clut
    );
}

[[nodiscard]] Location stream(
    int drawing, art::Pose pose, int slot
) {
    slot = slot != 0 ? 1 : 0;
    const art::Asset asset = art::character(drawing, pose);
    const int u = slot * art::character_side;
    const int clut = stream_clut + slot;
    if (streamed_drawing[slot] != drawing || streamed_pose[slot] != pose) {
        upload_cutout(stream_page, u, 0, asset.texels);
        upload_clut(clut, asset.clut);
        streamed_drawing[slot] = drawing;
        streamed_pose[slot] = pose;
    }
    return {stream_page, u, 0, clut};
}

}  // namespace

bool initialize() {
    if (ready) return true;
    // Idle cutouts occupy pages 7 through 10. Page 6 is reserved for streamed
    // poses and page 11 starts the close backdrop, so the exclusive upper
    // bound is the backdrop page, not the earlier stream page.
    if (first_character_page + character_page_count > backdrop_texture_page) {
        return false;
    }
    if (stream_clut + 1 >= gpu::clut_capacity) return false;
    for (int drawing = 0; drawing < art::character_count; ++drawing) {
        const int within = drawing % drawings_per_page;
        const int page = first_character_page + drawing / drawings_per_page;
        const int u = (within % cutouts_across) * art::character_side;
        const int v = (within / cutouts_across) * art::character_side;
        const int clut = first_character_clut + drawing;
        const art::Asset asset = art::character(drawing);
        if (asset.texels == nullptr || asset.clut == nullptr) return false;
        upload_cutout(page, u, v, asset.texels);
        upload_clut(clut, asset.clut);
        locations[drawing] = {page, u, v, clut};
    }
    ready = true;
    return true;
}

void draw_character(
    int drawing, art::Pose pose, int centre_x, int bottom, int size,
    bool facing_left, int stream_slot
) {
    if (!ready || drawing < 0 || drawing >= art::character_count || size <= 0) {
        return;
    }
    const Location location = pose == art::Pose::idle
        ? locations[drawing] : stream(drawing, pose, stream_slot);
    const int u0 = facing_left
        ? location.u + art::character_side - 1 : location.u;
    const int u1 = facing_left
        ? location.u : location.u + art::character_side - 1;
    gpu::draw_texture_quad(
        centre_x - size / 2, bottom - size, size, size,
        location.page, u0, location.v, u1,
        location.v + art::character_side - 1, location.clut
    );
}

void prepare_close() {
    upload_close_backdrop();
    // A previous streamed pose is still resident on its own page, but the
    // cache must not outlive a scene boundary: the next close frame owns both
    // slots explicitly.
    streamed_drawing[0] = -1;
    streamed_drawing[1] = -1;
}

void draw_close_background() {
    constexpr int columns = grandleon_psx_backdrop_width /
        grandleon_psx_backdrop_tile_width;
    constexpr int tiles_per_page = 6;
    for (int index = 0; index < grandleon_psx_backdrop_tile_count; ++index) {
        const int x = (index % columns) * grandleon_psx_backdrop_tile_width;
        const int y = (index / columns) * grandleon_psx_backdrop_tile_height;
        const int width = std::min(
            grandleon_psx_backdrop_tile_width,
            grandleon_psx_backdrop_width - x
        );
        const int height = std::min(
            grandleon_psx_backdrop_tile_height,
            grandleon_psx_backdrop_height - y
        );
        const int page = backdrop_texture_page + index / tiles_per_page;
        const int within = index % tiles_per_page;
        const int u = (within % 2) * grandleon_psx_backdrop_tile_width;
        const int v = (within / 2) * grandleon_psx_backdrop_tile_height;
        gpu::draw_texture_quad_ci8(
            x, y, width, height, page, u, v, u + width - 1, v + height - 1,
            backdrop_clut_x, backdrop_clut_y
        );
    }
    const int overlay_columns =
        (grandleon_psx_backdrop_overlay_width +
         grandleon_psx_backdrop_overlay_tile_width - 1) /
        grandleon_psx_backdrop_overlay_tile_width;
    for (int index = 0;
         index < grandleon_psx_backdrop_overlay_tile_count; ++index) {
        const int local_x = (index % overlay_columns) *
            grandleon_psx_backdrop_overlay_tile_width;
        const int local_y = (index / overlay_columns) *
            grandleon_psx_backdrop_overlay_tile_height;
        const int width = std::min(
            grandleon_psx_backdrop_overlay_tile_width,
            grandleon_psx_backdrop_overlay_width - local_x
        );
        const int height = std::min(
            grandleon_psx_backdrop_overlay_tile_height,
            grandleon_psx_backdrop_overlay_height - local_y
        );
        const int page = backdrop_overlay_page + index / tiles_per_page;
        const int within = index % tiles_per_page;
        const int u = (within % 2) *
            grandleon_psx_backdrop_overlay_tile_width;
        const int v = (within / 2) *
            grandleon_psx_backdrop_overlay_tile_height;
        gpu::draw_texture_quad_ci8(
            grandleon_psx_backdrop_overlay_x + local_x,
            grandleon_psx_backdrop_overlay_y + local_y,
            width, height, page, u, v, u + width - 1, v + height - 1,
            backdrop_overlay_clut_x, backdrop_clut_y
        );
    }
}

void draw_close_character(
    int drawing, art::Pose pose, bool left_side, int stream_slot
) {
    constexpr int close_size = 116;
    constexpr int close_bottom = 220;
    constexpr int left_x = 88;
    constexpr int right_x = 232;
    draw_character(
        drawing, pose, left_side ? left_x : right_x, close_bottom, close_size,
        !left_side, stream_slot
    );
}

}  // namespace grandleon::playstation::console_renderer
