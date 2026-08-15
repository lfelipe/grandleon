// SPDX-License-Identifier: MIT
// The board's presentation model: the arithmetic three renderers would
// otherwise write three times.
//
// Everything here is pure arithmetic over integers. There is no libdragon, no
// SDL, no engine header and no allocation, deliberately: the host test suite
// compiles this file and pins the projection, the ordering and the autotile
// lookup, so the parts of drawing that can silently go wrong are verified off
// the console and out of the browser. `camera.h` proved the idea; this is the
// rest of it.
//
// It sits above the three renderers and below the presenter seam. Nothing in
// platform/client knows it exists, and nothing here is canonical state: an
// elevation, a depth key and a pixel rectangle are drawing concerns, they
// never enter a snapshot and they are never hashed.
//
// The projection is axis-aligned by decision, not by omission. A rotated
// diamond grid would re-open the art generator, because the generated tiles
// are top-down squares; this model therefore offers a vertical lift and
// nothing that could grow into a camera matrix.

#pragma once

#include <cstdint>

namespace grandleon::view {

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

// A cell-grid camera for boards larger than the screen. Generalised out of the
// Nintendo 64 renderer, where the host suite already pinned the one part of
// scrolling that can silently go wrong: the edges.
struct Camera final {
    // Top-left visible cell.
    int x{0};
    int y{0};
    // Viewport size in cells; at least the map size when the map fits, in
    // which case the camera never moves and scrolling is invisible.
    int view_w{1};
    int view_h{1};
    int map_w{1};
    int map_h{1};

    // Keeps the viewport inside the map. A viewport larger than the map pins
    // to zero rather than centring, so a fitting board renders identically to
    // the pre-camera renderer.
    void clamp() {
        const int max_x = map_w > view_w ? map_w - view_w : 0;
        const int max_y = map_h > view_h ? map_h - view_h : 0;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > max_x) x = max_x;
        if (y > max_y) y = max_y;
    }

    // Scrolls so the cursor stays at least `margin` cells from the viewport
    // edge, where the map allows it.
    void follow(int cursor_x, int cursor_y, int margin) {
        if (cursor_x < x + margin) x = cursor_x - margin;
        if (cursor_x > x + view_w - 1 - margin) {
            x = cursor_x - (view_w - 1 - margin);
        }
        if (cursor_y < y + margin) y = cursor_y - margin;
        if (cursor_y > y + view_h - 1 - margin) {
            y = cursor_y - (view_h - 1 - margin);
        }
        clamp();
    }

    [[nodiscard]] bool visible(int cell_x, int cell_y) const {
        return cell_x >= x && cell_x < x + view_w &&
               cell_y >= y && cell_y < y + view_h;
    }
};

// ---------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------

// How far a cell rises up the screen per level of elevation, in pixels, for a
// given cell size. A quarter of the tile reads as a step without ever lifting
// a cell clear of the row behind it, and it is an exact division for every
// tile size the clients use.
[[nodiscard]] constexpr int elevation_step_for(int tile) noexcept {
    const int step = tile / 4;
    return step > 0 ? step : (tile > 0 ? 1 : 0);
}

// The furthest up the screen a renderer may draw a cell, whatever elevation
// its terrain declares: three eighths of a tile.
//
// The geometry that fixes the number. A cell drawn `L` pixels higher than the
// cell behind it covers the bottom `L` rows of that cell's rectangle, and a
// cell's centre row sits `tile / 2` below its own top edge, so the centre of
// the cell behind survives exactly while
//
//     L < tile - tile / 2 = ceil(tile / 2)
//
// Half a tile is therefore the *boundary*, not a safe value, and two levels
// of a quarter-tile step land precisely on it, which is what this cap exists
// to move away from. The bound is the halfway line less half a step:
//
//     tile / 2 - tile / 8 = 3 * tile / 8
//
// so the top edge of a raised cell stays at least an eighth of a tile below
// the centre of the cell behind it: at least one pixel for any tile size at
// all, four on the console's 25px cell (a cap of 9 against a boundary of 13),
// eight on the desktop's 64px cell, thirteen on the editor's 100px cell. "A
// cell's centre is always visible" is a fact of the projection rather than a
// near-miss, and every centre-sampling probe in the repository may rely on it.
//
// This bounds how high a cell is *drawn*. It does not bound how high a terrain
// *is*: a terrain's elevation is authored, generated content, and a renderer
// that can afford real relief, such as a 3D exporter or a console with a depth
// buffer, reads the level count and means it. Clamping the elevation table
// instead would look identical here and would throw that number away.
[[nodiscard]] constexpr int max_lift_for(int tile) noexcept {
    return tile > 0 ? (tile * 3) / 8 : 0;
}

// The upward offset an elevated cell's art is drawn at: its elevation in
// steps, bounded by `max_lift_for`. Negative elevation is clamped away rather
// than sinking a cell, because a sunken cell would be occluded by the row in
// front of it and no client draws a hole.
//
// A step of zero draws elevated content flat, which is what lets a client pin
// exactly the board it drew before this model existed.
[[nodiscard]] constexpr int lift_for(
    int elevation, int elevation_step, int tile
) noexcept {
    if (elevation <= 0 || elevation_step <= 0) return 0;
    const int cap = max_lift_for(tile);
    if (cap <= 0) return 0;
    // Either factor at or above the cap already saturates it, and stopping
    // here keeps the multiplication below from growing without bound.
    if (elevation >= cap || elevation_step >= cap) return cap;
    const int raw = elevation * elevation_step;
    return raw < cap ? raw : cap;
}

// The pixel geometry of the board: where the camera's top-left visible cell
// lands, how big a cell is, and how far a level of elevation lifts it.
//
// `elevation_step` is separate from `tile` rather than derived from it so a
// client can pin a step of zero and get, exactly, the flat board it drew
// before this model existed.
struct Projection final {
    int origin_x{0};
    int origin_y{0};
    int tile{1};
    int elevation_step{0};

    // The upward offset an elevated cell's art is drawn at, bounded by
    // `max_lift_for(tile)` so the centre of the cell behind is never covered.
    // Terrain taller than the cap draws at the cap and keeps its elevation:
    // the number is content, the offset is presentation.
    [[nodiscard]] constexpr int lift(int elevation) const noexcept {
        return lift_for(elevation, elevation_step, tile);
    }

    [[nodiscard]] constexpr int cell_left(
        const Camera& camera, int cell_x
    ) const noexcept {
        return origin_x + (cell_x - camera.x) * tile;
    }

    [[nodiscard]] constexpr int cell_top(
        const Camera& camera, int cell_y, int elevation
    ) const noexcept {
        return origin_y + (cell_y - camera.y) * tile - lift(elevation);
    }

    // Where a probe samples a cell: the centre of the cell as drawn, which is
    // the centre of the flat cell lifted by the same offset the art was. A
    // sampler that follows this asks for the same pixel the renderer drew the
    // cell's middle at, whatever height that cell stands at.
    //
    // And nothing else can have been painted over it. A cell raised above the
    // one behind it covers that cell's rectangle from the bottom up by the
    // difference in their lifts, which `lift` bounds at three eighths of a
    // tile: an eighth of a tile short of the centre row, whatever the board
    // and whatever elevation its terrain declares. A probe that identifies a
    // cell by the colour of its centre therefore reads that cell, never the
    // ground or the unit in front of it.
    //
    // A whole cell *rectangle* is a different question, and a caller that
    // scans one, for a faction ramp say, needs the rest of the answer. Two
    // rows at unequal heights share a band `lift(one) - lift(other)` deep, and
    // whatever stands in the higher-drawn of the two is painted across it. So
    // such a caller must set aside `lift(here) - lift(behind)` rows at the top
    // of the rectangle and `lift(front) - lift(here)` at the bottom, each when
    // positive, to see only what was drawn of this cell. Both are bounded by
    // the cap, so what remains always contains the centre.
    [[nodiscard]] constexpr int cell_centre_x(
        const Camera& camera, int cell_x
    ) const noexcept {
        return cell_left(camera, cell_x) + tile / 2;
    }

    [[nodiscard]] constexpr int cell_centre_y(
        const Camera& camera, int cell_y, int elevation
    ) const noexcept {
        return cell_top(camera, cell_y, elevation) + tile / 2;
    }
};

// The headroom a board needs above its first row so the tallest lift stays
// inside the frame. It is that lift, through the same bounded arithmetic the
// renderer draws with, so a layout can never reserve room for a height no cell
// is drawn at. A board whose terrain is all at elevation zero needs none,
// which is what keeps flat content pixel-identical to the flat renderer.
[[nodiscard]] constexpr int headroom(
    int max_elevation, int elevation_step, int tile
) noexcept {
    return lift_for(max_elevation, elevation_step, tile);
}

// ---------------------------------------------------------------------------
// Autotile
// ---------------------------------------------------------------------------

// The eight-neighbour bit order the art generator's `mask_to_variant` table is
// indexed by (tools/placeholder_art `autotile.py`, mirrored into the editor's
// generated board-art module). A bit is set when the neighbour is the *same*
// terrain as the cell being drawn.
enum NeighbourBit : std::uint8_t {
    neighbour_north = 1,
    neighbour_north_east = 2,
    neighbour_east = 4,
    neighbour_south_east = 8,
    neighbour_south = 16,
    neighbour_south_west = 32,
    neighbour_west = 64,
    neighbour_north_west = 128,
};

// The neighbour mask of a cell. `same(x, y)` answers whether the cell at those
// coordinates draws as the same terrain; off-board coordinates are the
// caller's to answer, and every client answers "no" so a coastline forms at
// the map edge.
//
// Diagonals are reported as they are, not collapsed against their cardinals:
// the collapsing is the generator's table's business, and a client that
// collapsed them here as well would disagree with every client that did not.
template <typename Same>
[[nodiscard]] std::uint8_t neighbour_mask(int x, int y, Same same) {
    std::uint8_t mask = 0;
    if (same(x, y - 1)) mask |= neighbour_north;
    if (same(x + 1, y - 1)) mask |= neighbour_north_east;
    if (same(x + 1, y)) mask |= neighbour_east;
    if (same(x + 1, y + 1)) mask |= neighbour_south_east;
    if (same(x, y + 1)) mask |= neighbour_south;
    if (same(x - 1, y + 1)) mask |= neighbour_south_west;
    if (same(x - 1, y)) mask |= neighbour_west;
    if (same(x - 1, y - 1)) mask |= neighbour_north_west;
    return mask;
}

// The variant a mask draws, through the generator's own 256-entry table. The
// table is the authority; this is the one lookup.
[[nodiscard]] inline int autotile_variant(
    std::uint8_t mask, const unsigned char* mask_to_variant
) {
    return mask_to_variant == nullptr
               ? 0
               : static_cast<int>(mask_to_variant[mask]);
}

// Where a variant lives in a sheet laid out in `columns` tiles per row.
[[nodiscard]] constexpr int variant_source_x(
    int variant, int columns, int tile_size
) noexcept {
    return columns > 0 ? (variant % columns) * tile_size : 0;
}

[[nodiscard]] constexpr int variant_source_y(
    int variant, int columns, int tile_size
) noexcept {
    return columns > 0 ? (variant / columns) * tile_size : 0;
}

// ---------------------------------------------------------------------------
// Depth-ordered draw list
// ---------------------------------------------------------------------------

// What a draw item is for. Terrain is the ground the board stands on, the
// shadow grounds a billboard against it, and the billboard itself is the unit.
// Layers never interleave: every renderer draws all of one before any of the
// next, which is what lets a lifted unit pass in front of the ground behind it
// without any renderer owning a Z buffer.
enum class Layer : std::uint8_t {
    terrain = 0,
    shadow = 1,
    unit = 2,
};

// One thing to draw, in the vocabulary every renderer already speaks: which
// sheet, which sub-image of it, and where on the screen.
//
// `subject` is the caller's own handle for the item, an index into its own
// units or a terrain kind, carried through untouched so a renderer or a probe
// can recognise what came back out of the sort.
struct DrawItem final {
    Layer layer{Layer::terrain};
    std::int16_t sheet{0};
    std::int16_t variant{0};
    std::int16_t cell_x{0};
    std::int16_t cell_y{0};
    std::int16_t elevation{0};
    // The destination rectangle, in screen pixels.
    std::int16_t x{0};
    std::int16_t y{0};
    std::int16_t w{0};
    std::int16_t h{0};
    std::uint32_t subject{0};
    std::int64_t depth{0};
};

// The draw order, as one number, ascending: back to front.
//
// The rule, and why it is only these terms:
//
//   * `layer` first, because the layers are a fixed stack.
//   * `elevation` next. In an axis-aligned top-down projection a cell's art
//     only ever overhangs the cells *behind* it, and two cells overlap exactly
//     when the nearer one is drawn higher, so drawing the low ground before
//     the high ground is not a heuristic, it is the whole of the occlusion
//     rule. Everything at one elevation is drawn at one offset and therefore
//     cannot overlap anything else at that elevation. Two elevations that both
//     saturate the lift cap share one offset and so cannot overlap either;
//     the key still sorts them apart, which changes nothing about the picture.
//   * `batch` next, and only because of the previous sentence: items sharing a
//     layer and an elevation may be reordered freely, so a renderer is free to
//     group them by whatever it pays for: a texture upload on the console, a
//     texture binding on the desktop. Nothing about the picture depends on it.
//   * then row, then column, so the order is total and identical on every
//     client, whatever order the caller happened to walk the board in.
//
// The five terms are packed into disjoint bit ranges, most significant first:
//
//     63..56  layer      8 bits
//     55..48  elevation  8 bits   (255 levels; the art library uses 2)
//     47..32  batch     16 bits
//     31..16  row       16 bits
//     15..0   column    16 bits
//
// Each is clamped to the width it is given, so a caller that hands over a
// number too large for its field loses precision inside that field and can
// never reach into the field above it. That matters most for `batch`, which
// is the one term a renderer picks freely: a batch that overflowed into the
// elevation field would reorder the layers of ground, which is the one thing
// the order exists to get right.
[[nodiscard]] constexpr std::int64_t depth_key(
    Layer layer, int elevation, int batch, int cell_y, int cell_x
) noexcept {
    const auto field = [](int value, int limit) -> std::int64_t {
        if (value < 0) return 0;
        if (value > limit) return limit;
        return static_cast<std::int64_t>(value);
    };
    return (static_cast<std::int64_t>(static_cast<std::uint8_t>(layer)) << 56) |
           (field(elevation, 0xFF) << 48) | (field(batch, 0xFFFF) << 32) |
           (field(cell_y, 0xFFFF) << 16) | field(cell_x, 0xFFFF);
}

// A fixed-capacity, caller-owned draw list. Storage comes from the caller so
// the console can hold one for the life of the presenter and never allocate
// during a frame.
class DrawList final {
public:
    DrawList(DrawItem* storage, int capacity)
        : items_(storage), capacity_(capacity > 0 ? capacity : 0) {}

    void clear() {
        count_ = 0;
        overflowed_ = false;
    }

    // Adds an item, filling in its depth from the fields already on it. The
    // caller supplies `batch`, which only ever reorders items that cannot
    // overlap.
    bool add(const DrawItem& item, int batch = 0) {
        if (count_ >= capacity_) {
            overflowed_ = true;
            return false;
        }
        DrawItem stored = item;
        stored.depth = depth_key(
            item.layer, item.elevation, batch, item.cell_y, item.cell_x
        );
        items_[count_++] = stored;
        return true;
    }

    // Puts the list in draw order. Insertion sort: stable, allocation-free,
    // dependency-free, and near-linear on the nearly-ordered input every
    // renderer produces by walking the board.
    void sort() {
        for (int i = 1; i < count_; ++i) {
            DrawItem key = items_[i];
            int j = i - 1;
            while (j >= 0 && items_[j].depth > key.depth) {
                items_[j + 1] = items_[j];
                --j;
            }
            items_[j + 1] = key;
        }
    }

    [[nodiscard]] int size() const { return count_; }
    [[nodiscard]] bool overflowed() const { return overflowed_; }
    [[nodiscard]] const DrawItem& operator[](int index) const {
        return items_[index];
    }

private:
    DrawItem* items_{nullptr};
    int capacity_{0};
    int count_{0};
    bool overflowed_{false};
};

// Fills in a terrain item's screen rectangle from the projection. Kept beside
// the projection rather than inside each renderer so "where does this cell go"
// has exactly one answer.
[[nodiscard]] inline DrawItem terrain_item(
    const Camera& camera,
    const Projection& projection,
    int cell_x,
    int cell_y,
    int elevation,
    int sheet,
    int variant,
    std::uint32_t subject
) {
    DrawItem item{};
    item.layer = Layer::terrain;
    item.sheet = static_cast<std::int16_t>(sheet);
    item.variant = static_cast<std::int16_t>(variant);
    item.cell_x = static_cast<std::int16_t>(cell_x);
    item.cell_y = static_cast<std::int16_t>(cell_y);
    item.elevation = static_cast<std::int16_t>(elevation);
    item.x = static_cast<std::int16_t>(projection.cell_left(camera, cell_x));
    item.y = static_cast<std::int16_t>(
        projection.cell_top(camera, cell_y, elevation)
    );
    item.w = static_cast<std::int16_t>(projection.tile);
    item.h = static_cast<std::int16_t>(projection.tile);
    item.subject = subject;
    return item;
}

// A billboard standing on a cell, and the shadow that grounds it. Both occupy
// the cell exactly, so on level ground neither reaches a neighbour at all,
// which is what keeps the clients' centre-sampling probes reading the cell
// they name. Standing on ground raised above the cell behind it, a billboard
// reaches into the bottom of that cell's rectangle by the difference in their
// lifts, exactly as the ground it stands on does, and no further than the same
// cap, so it can never reach that cell's centre; see `cell_centre_y`.
[[nodiscard]] inline DrawItem billboard_item(
    const Camera& camera,
    const Projection& projection,
    Layer layer,
    int cell_x,
    int cell_y,
    int elevation,
    int sheet,
    std::uint32_t subject
) {
    DrawItem item = terrain_item(
        camera, projection, cell_x, cell_y, elevation, sheet, 0, subject
    );
    item.layer = layer;
    return item;
}

}  // namespace grandleon::view
