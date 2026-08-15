// SPDX-License-Identifier: MIT
// Host-side test for the board's presentation model.
//
// The projection, the draw order and the autotile lookup are the arithmetic
// three renderers share, so this is where they are pinned: on the host, with
// no console, no window and no browser in the loop. A renderer that disagrees
// with this file is wrong about where the board is.
//
// The generated autotile table is included directly rather than copied, so the
// lookup is checked against the art library's own data.

#include "grandleon/view/board_view.hpp"

#include "sprites.h"
#include "themes.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace grandleon::view;

    // ---------------------------------------------------------------
    // Elevation step
    // ---------------------------------------------------------------
    {
        expect(elevation_step_for(32) == 8, "a 32px cell steps by 8");
        expect(elevation_step_for(24) == 6, "a 24px cell steps by 6");
        expect(elevation_step_for(64) == 16, "a 64px cell steps by 16");
        expect(elevation_step_for(100) == 25, "a 100px cell steps by 25");
        expect(
            elevation_step_for(2) == 1,
            "a cell too small to quarter still steps by a visible pixel"
        );
        expect(elevation_step_for(0) == 0, "a zero cell has no step");
    }

    // ---------------------------------------------------------------
    // Projection: a flat board is exactly the board the flat renderers drew
    // ---------------------------------------------------------------
    {
        const Camera camera{0, 0, 10, 8, 10, 8};
        const Projection flat{10, 14, 24, 6};
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 10; ++x) {
                const bool same =
                    flat.cell_left(camera, x) == 10 + x * 24 &&
                    flat.cell_top(camera, y, 0) == 14 + y * 24;
                expect(same, "an unelevated cell lands on the flat grid");
            }
        }
        expect(
            flat.cell_centre_x(camera, 3) == 10 + 3 * 24 + 12 &&
                flat.cell_centre_y(camera, 5, 0) == 14 + 5 * 24 + 12,
            "the sampled centre of an unelevated cell is the flat centre"
        );
    }

    // ---------------------------------------------------------------
    // Projection: elevation lifts, and only upward
    // ---------------------------------------------------------------
    {
        const Camera camera{0, 0, 10, 8, 10, 8};
        const Projection projection{0, 0, 24, 6};
        expect(projection.lift(0) == 0, "level ground is not lifted");
        expect(projection.lift(1) == 6, "one level lifts by one step");
        // Two levels of a 24px cell ask for 12, exactly half the cell, which
        // is the centre row of the cell behind. The cap answers 9, three
        // eighths of the cell, and every level above it answers 9 as well.
        expect(
            projection.lift(2) == 9 && projection.lift(3) == 9 &&
                projection.lift(200) == 9,
            "a lift stops at three eighths of a cell however tall the terrain"
        );
        expect(
            projection.lift(-2) == 0,
            "a negative elevation is clamped rather than sunk"
        );
        expect(
            projection.cell_top(camera, 4, 2) ==
                projection.cell_top(camera, 4, 0) - 9,
            "elevation moves a cell up the screen and nowhere else"
        );
        // And never sideways: the projection is axis-aligned. Asserted on the
        // whole placed rectangle rather than on cell_left, which cannot shear
        // because it is not handed an elevation. A shear would appear when
        // the rectangle is assembled, so that is where this looks. Every level
        // is checked, because a shear proportional to height is exactly the
        // shape an isometric projection would take.
        {
            bool axis_aligned = true;
            for (int elevation = 0; elevation <= 4; ++elevation) {
                for (int cell_x = 0; cell_x < 5; ++cell_x) {
                    const DrawItem flat = terrain_item(
                        camera, projection, cell_x, 4, 0, 0, 0, 0
                    );
                    const DrawItem raised = terrain_item(
                        camera, projection, cell_x, 4, elevation, 0, 0, 0
                    );
                    if (raised.x != flat.x) axis_aligned = false;
                    if (raised.w != flat.w || raised.h != flat.h) {
                        axis_aligned = false;
                    }
                    if (raised.y != flat.y - projection.lift(elevation)) {
                        axis_aligned = false;
                    }
                }
            }
            expect(
                axis_aligned,
                "a raised cell keeps its column and its size exactly, and "
                "moves by its lift and nothing else"
            );
        }
        // The whole of the 2.5D claim, in one line: a cell two levels up sits
        // where the cell two-thirds of a row behind it would sit, in the same
        // column.
        expect(
            projection.cell_top(camera, 4, 1) ==
                projection.cell_top(camera, 4, 0) -
                    elevation_step_for(projection.tile),
            "one level is one step of the tile's own size"
        );
    }

    // A projection with a zero step is the flat renderer, exactly.
    {
        const Camera camera{0, 0, 4, 4, 4, 4};
        const Projection stepless{0, 0, 16, 0};
        expect(
            stepless.cell_top(camera, 2, 5) == stepless.cell_top(camera, 2, 0),
            "a zero step draws elevated content flat"
        );
    }

    // The camera's scroll is subtracted before the lift is applied, so a
    // scrolled board and a still one lift by the same amount.
    {
        const Camera scrolled{3, 2, 6, 5, 20, 20};
        const Projection projection{8, 20, 20, 5};
        expect(
            projection.cell_left(scrolled, 3) == 8 &&
                projection.cell_top(scrolled, 2, 0) == 20,
            "the camera's top-left cell lands on the origin"
        );
        expect(
            projection.cell_top(scrolled, 6, 2) ==
                20 + 4 * 20 - projection.lift(2),
            "a scrolled elevated cell lifts by exactly what a still one lifts "
            "by, cap and all"
        );
        expect(
            projection.lift(2) == 7,
            "which on a 20px cell is three eighths of it, not the 10 two "
            "steps ask for"
        );
    }

    // ---------------------------------------------------------------
    // Headroom
    // ---------------------------------------------------------------
    {
        expect(headroom(0, 6, 24) == 0, "a flat board reserves no headroom");
        expect(headroom(1, 6, 24) == 6, "the tallest lift is the headroom");
        expect(
            headroom(2, 6, 24) == 9 && headroom(9, 6, 24) == 9,
            "and a board of terrain past the cap reserves the cap, not the "
            "height it asked for"
        );
        expect(
            headroom(-1, 6, 24) == 0,
            "a board whose highest terrain is below zero reserves nothing"
        );
    }

    // ---------------------------------------------------------------
    // The lift cap: a cell's centre is always visible
    // ---------------------------------------------------------------
    //
    // The property the cap exists for, asserted where it lives: over every
    // cell size a client could pick and every elevation an author could write,
    // rather than over the two levels the art library ships today.
    //
    // A cell drawn `L` higher than the cell behind it covers that cell's
    // bottom `L` rows, and the centre row sits `tile / 2` below its top, so
    // the centre survives while `L < tile - tile / 2`. The cap is three
    // eighths of a tile: strictly under that boundary for every tile size,
    // by an eighth of a tile.
    {
        bool below_the_boundary = true;
        bool bounded = true;
        bool a_step_still_reads = true;
        for (int tile = 1; tile <= 512; ++tile) {
            const int cap = max_lift_for(tile);
            const int boundary = tile - tile / 2;
            if (cap >= boundary) below_the_boundary = false;
            const int step = elevation_step_for(tile);
            for (int elevation = 0; elevation <= 300; ++elevation) {
                if (lift_for(elevation, step, tile) > cap) bounded = false;
            }
            // The cap is a bound, not a flattening: one level is still drawn
            // where one level asks to be drawn, for every cell size a client
            // could hand over, so hills and mountains keep reading as two
            // different heights.
            if (tile >= 8 && lift_for(1, step, tile) != step) {
                a_step_still_reads = false;
            }
            if (tile >= 8 && lift_for(2, step, tile) <= step) {
                a_step_still_reads = false;
            }
        }
        expect(
            below_the_boundary,
            "no cell size lets a lift reach the centre row of the cell behind"
        );
        expect(bounded, "and no elevation, however tall, gets past the cap");
        expect(
            a_step_still_reads,
            "while one level still lifts by one whole step and two levels "
            "still stand above one"
        );

        // The same property stated through the projection a renderer actually
        // calls, rather than through the arithmetic behind it: for adjacent
        // rows at any two elevations, the front cell's top edge stays below
        // the centre the probes sample. This is the assertion that fails if
        // the cap is loosened.
        bool centre_visible = true;
        for (const int tile : {8, 16, 20, 24, 25, 26, 32, 64, 100}) {
            const Camera camera{0, 0, 4, 4, 4, 4};
            const Projection projection{
                0, 0, tile, elevation_step_for(tile)
            };
            for (int behind = 0; behind <= 12; ++behind) {
                for (int front = 0; front <= 12; ++front) {
                    const int centre =
                        projection.cell_centre_y(camera, 1, behind);
                    const int front_top =
                        projection.cell_top(camera, 2, front);
                    if (front_top <= centre) centre_visible = false;
                }
            }
        }
        expect(
            centre_visible,
            "so the cell in front is always drawn below the centre of the "
            "cell behind it, at every elevation and every cell size"
        );
    }

    // ---------------------------------------------------------------
    // Autotile: the bit order, against the generator's own table
    // ---------------------------------------------------------------
    {
        // A cell surrounded by its own kind is the interior variant; a cell
        // surrounded by nothing else is the lone variant. Both come from the
        // generated table rather than from a number written here.
        const std::uint8_t all = 0xFF;
        // The bit values, against the generated table rather than against
        // numbers written here: a cell open to the north and one open to the
        // south are different tiles, and so are east and west. That fails if
        // any bit in the enumeration is assigned to the wrong direction,
        // which restating the lookup's one-line body would not.
        expect(
            autotile_variant(
                static_cast<std::uint8_t>(all & ~neighbour_north),
                grandleon_sprites_mask_to_variant
            ) != autotile_variant(
                static_cast<std::uint8_t>(all & ~neighbour_south),
                grandleon_sprites_mask_to_variant
            ),
            "a cell open to the north is not the tile open to the south"
        );
        expect(
            autotile_variant(
                static_cast<std::uint8_t>(all & ~neighbour_east),
                grandleon_sprites_mask_to_variant
            ) != autotile_variant(
                static_cast<std::uint8_t>(all & ~neighbour_west),
                grandleon_sprites_mask_to_variant
            ),
            "nor is one open to the east the tile open to the west"
        );
        expect(
            autotile_variant(0xFF, grandleon_sprites_mask_to_variant) !=
                autotile_variant(0, grandleon_sprites_mask_to_variant),
            "an interior tile and a lone tile are not the same tile"
        );
        expect(
            autotile_variant(all, nullptr) == 0,
            "a missing table falls back to the first variant"
        );

        // Every one of the 256 masks resolves to a variant the sheet holds.
        bool in_range = true;
        for (int mask = 0; mask < 256; ++mask) {
            const int variant = autotile_variant(
                static_cast<std::uint8_t>(mask),
                grandleon_sprites_mask_to_variant
            );
            const int rows = grandleon_sprites_blob_rows;
            const int columns = grandleon_sprites_blob_columns;
            if (variant < 0 || variant >= rows * columns) in_range = false;
        }
        expect(in_range, "every mask resolves inside the generated sheet");
    }

    // The mask itself: a 3x3 patch of one kind in a field of another.
    {
        const int width = 5;
        const int height = 5;
        const auto inner = [&](int x, int y) {
            return x >= 1 && x <= 3 && y >= 1 && y <= 3;
        };
        const auto same = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= width || y >= height) return false;
            return inner(x, y);
        };
        expect(
            neighbour_mask(2, 2, same) == 0xFF,
            "the centre of a 3x3 patch matches on all eight sides"
        );
        expect(
            neighbour_mask(1, 1, same) ==
                (neighbour_east | neighbour_south_east | neighbour_south),
            "the patch's top-left corner matches to the east and south only"
        );
        expect(
            neighbour_mask(3, 1, same) ==
                (neighbour_south | neighbour_south_west | neighbour_west),
            "and its top-right corner to the west and south only"
        );
        expect(
            neighbour_mask(2, 1, same) ==
                (neighbour_east | neighbour_south_east | neighbour_south |
                 neighbour_south_west | neighbour_west),
            "a patch edge matches on five sides"
        );

        // Off the board reads as a different kind, which is what makes a
        // coastline appear at the map edge instead of an interior tile.
        const auto everything = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < width && y < height;
        };
        expect(
            neighbour_mask(0, 0, everything) ==
                (neighbour_east | neighbour_south_east | neighbour_south),
            "the map's own corner is a corner tile"
        );
        expect(
            neighbour_mask(2, 2, everything) == 0xFF,
            "and its middle is interior"
        );
    }

    // Sheet coordinates of a variant.
    {
        const int columns = grandleon_sprites_blob_columns;
        expect(
            variant_source_x(0, columns, 32) == 0 &&
                variant_source_y(0, columns, 32) == 0,
            "variant zero is the sheet's first tile"
        );
        expect(
            variant_source_x(columns, columns, 32) == 0 &&
                variant_source_y(columns, columns, 32) == 32,
            "the variant after a full row starts the next row"
        );
        expect(
            variant_source_x(columns + 3, columns, 32) == 96 &&
                variant_source_y(columns + 3, columns, 32) == 32,
            "and a variant inside that row is offset along it"
        );
        expect(
            variant_source_x(5, 0, 32) == 0 && variant_source_y(5, 0, 32) == 0,
            "a sheet with no columns resolves to its origin rather than "
            "dividing by zero"
        );
    }

    // ---------------------------------------------------------------
    // Depth order
    // ---------------------------------------------------------------
    {
        // The layer stack: ground, then the shadow that grounds a billboard,
        // then the billboard.
        expect(
            depth_key(Layer::terrain, 9, 9, 9, 9) <
                depth_key(Layer::shadow, 0, 0, 0, 0),
            "every terrain cell is drawn before any shadow"
        );
        expect(
            depth_key(Layer::shadow, 9, 9, 9, 9) <
                depth_key(Layer::unit, 0, 0, 0, 0),
            "and every shadow before any billboard"
        );

        // Elevation outranks the row, because a lifted cell overhangs the
        // cells behind it and nothing else can.
        expect(
            depth_key(Layer::terrain, 0, 0, 99, 0) <
                depth_key(Layer::terrain, 1, 0, 0, 0),
            "low ground is drawn before high ground, whatever the row"
        );
        expect(
            depth_key(Layer::terrain, 0, 0, 3, 7) <
                depth_key(Layer::terrain, 0, 0, 4, 0),
            "within one elevation the further row is drawn first"
        );
        expect(
            depth_key(Layer::terrain, 0, 0, 4, 2) <
                depth_key(Layer::terrain, 0, 0, 4, 3),
            "and within one row, left before right"
        );
        expect(
            depth_key(Layer::terrain, 0, 1, 0, 0) >
                depth_key(Layer::terrain, 0, 0, 99, 99),
            "a batch groups a whole elevation band, which is free to reorder"
        );
        // The key is a function of its terms and of nothing else: two cells
        // that differ in any one term sort apart, and cells reached by
        // different routes through the board sort together. Comparing one
        // call against itself would say nothing about either.
        expect(
            depth_key(Layer::terrain, 1, 2, 5, 5) !=
                    depth_key(Layer::terrain, 1, 2, 5, 6) &&
                depth_key(Layer::terrain, 1, 2, 5, 5) !=
                    depth_key(Layer::terrain, 1, 2, 6, 5) &&
                depth_key(Layer::terrain, 1, 2, 5, 5) !=
                    depth_key(Layer::terrain, 1, 3, 5, 5) &&
                depth_key(Layer::terrain, 1, 2, 5, 5) !=
                    depth_key(Layer::terrain, 2, 2, 5, 5) &&
                depth_key(Layer::terrain, 1, 2, 5, 5) !=
                    depth_key(Layer::shadow, 1, 2, 5, 5),
            "changing any one term moves the cell in the order"
        );
        expect(
            depth_key(Layer::terrain, -3, 0, 2, 2) ==
                depth_key(Layer::terrain, 0, 0, 2, 2),
            "a negative elevation sorts as level ground, as it draws"
        );
        expect(
            depth_key(Layer::terrain, 0, 0, 0, 0) >= 0 &&
                depth_key(Layer::unit, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF) > 0,
            "the key stays a positive number at both extremes"
        );

        // The fields do not reach into each other. Each term is checked at a
        // value far past what any client passes today, because the terms a
        // renderer picks freely are exactly the ones nothing else bounds: the
        // header invites a renderer to batch by "whatever it pays for", and a
        // batch that overflowed into the elevation field would reorder the
        // layers of ground, which is the one thing this order exists to get
        // right.
        expect(
            depth_key(Layer::terrain, 0, 0xFFFF, 0xFFFF, 0xFFFF) <
                depth_key(Layer::terrain, 1, 0, 0, 0),
            "no batch, row or column outranks one level of elevation"
        );
        expect(
            depth_key(Layer::terrain, 0, 0, 0xFFFF, 0xFFFF) <
                depth_key(Layer::terrain, 0, 1, 0, 0),
            "and no row or column outranks one batch"
        );
        expect(
            depth_key(Layer::terrain, 0, 0, 0, 0xFFFF) <
                depth_key(Layer::terrain, 0, 0, 1, 0),
            "and no column outranks one row"
        );
        expect(
            depth_key(Layer::terrain, 0xFF, 0xFFFF, 0xFFFF, 0xFFFF) <
                depth_key(Layer::shadow, 0, 0, 0, 0),
            "and nothing at all outranks the layer above it"
        );

        // Past a field's width the term saturates rather than carrying, so an
        // absurd value is merely indistinguishable from the largest sensible
        // one instead of corrupting the term above it.
        expect(
            depth_key(Layer::terrain, 0xFF, 0, 0, 0) ==
                depth_key(Layer::terrain, 0xFFFF, 0, 0, 0),
            "an elevation past the field's width saturates"
        );
        expect(
            depth_key(Layer::terrain, 0, 0xFFFF, 0, 0) ==
                depth_key(Layer::terrain, 0, 0x10FFFF, 0, 0),
            "and so does a batch"
        );
    }

    // ---------------------------------------------------------------
    // The draw list
    // ---------------------------------------------------------------
    {
        const Camera camera{0, 0, 3, 3, 3, 3};
        const Projection projection{0, 0, 16, 4};
        DrawItem storage[32];
        DrawList list(storage, 32);

        // Walked backwards on purpose: the list's order must be the model's,
        // not the caller's.
        for (int y = 2; y >= 0; --y) {
            for (int x = 2; x >= 0; --x) {
                const int elevation = (x == 1 && y == 1) ? 2 : 0;
                list.add(
                    terrain_item(
                        camera, projection, x, y, elevation, 0, 0,
                        static_cast<std::uint32_t>(y * 3 + x)
                    ),
                    0
                );
            }
        }
        list.sort();
        expect(list.size() == 9, "every visible cell is in the list");
        expect(!list.overflowed(), "and the list did not overflow");

        bool ordered = true;
        for (int i = 1; i < list.size(); ++i) {
            if (list[i - 1].depth > list[i].depth) ordered = false;
        }
        expect(ordered, "the sorted list is in ascending depth order");
        expect(
            list[8].elevation == 2 && list[8].cell_x == 1 &&
                list[8].cell_y == 1,
            "the one raised cell is drawn last, over the row behind it"
        );
        expect(
            list[0].cell_x == 0 && list[0].cell_y == 0,
            "and the flat cells are drawn in reading order regardless of "
            "the order they were added in"
        );
        expect(
            list[8].y == projection.cell_top(camera, 1, 0) - 6,
            "the raised cell's rectangle carries the lift, capped at three "
            "eighths of its 16px cell"
        );
        expect(
            list[4].w == 16 && list[4].h == 16,
            "a cell's rectangle is one tile"
        );
    }

    // Batching only ever reorders items that cannot overlap.
    {
        const Camera camera{0, 0, 4, 1, 4, 1};
        const Projection projection{0, 0, 16, 4};
        DrawItem storage[8];
        DrawList list(storage, 8);
        // Two sheets alternating along one row, all at the same elevation.
        for (int x = 0; x < 4; ++x) {
            list.add(
                terrain_item(
                    camera, projection, x, 0, 0, x % 2, 0,
                    static_cast<std::uint32_t>(x)
                ),
                x % 2
            );
        }
        list.sort();
        bool grouped = list[0].sheet == 0 && list[1].sheet == 0 &&
                       list[2].sheet == 1 && list[3].sheet == 1;
        expect(
            grouped,
            "a batch collects one sheet's cells together for one upload"
        );
        bool same_offset = true;
        for (int i = 1; i < list.size(); ++i) {
            if (list[i].y != list[0].y) same_offset = false;
        }
        expect(
            same_offset,
            "and every cell it reordered is drawn at the same offset, so the "
            "picture cannot depend on the grouping"
        );
    }

    // A shadow is drawn under its own billboard and inside its own cell.
    {
        const Camera camera{0, 0, 4, 4, 4, 4};
        const Projection projection{0, 0, 20, 5};
        DrawItem storage[8];
        DrawList list(storage, 8);
        list.add(
            billboard_item(camera, projection, Layer::unit, 2, 2, 1, 0, 7)
        );
        list.add(
            billboard_item(camera, projection, Layer::shadow, 2, 2, 1, 0, 7)
        );
        list.sort();
        expect(
            list[0].layer == Layer::shadow && list[1].layer == Layer::unit,
            "the shadow is drawn before the unit standing on it"
        );
        expect(
            list[0].x == list[1].x && list[0].y == list[1].y &&
                list[0].w == list[1].w && list[0].h == list[1].h,
            "both occupy exactly the unit's cell, so neither can reach a "
            "neighbouring cell's sampled centre"
        );
        expect(
            list[1].y == projection.cell_top(camera, 2, 0) - 5,
            "and both ride the terrain's lift"
        );
    }

    // A degenerate board: no cells, no capacity, no crash.
    {
        DrawItem storage[1];
        DrawList list(storage, 0);
        const Camera camera{0, 0, 0, 0, 0, 0};
        const Projection projection{0, 0, 1, 0};
        expect(
            !list.add(terrain_item(camera, projection, 0, 0, 0, 0, 0, 0)),
            "a list with no room refuses the item"
        );
        expect(list.overflowed(), "and says so");
        expect(list.size() == 0, "and stays empty");
        list.sort();
        list.clear();
        expect(
            list.size() == 0 && !list.overflowed(),
            "clearing an overflowed empty list resets it"
        );
    }

    // A one-cell board, which is the smallest map anything can hand over.
    {
        const Camera camera{0, 0, 1, 1, 1, 1};
        const Projection projection{0, 0, 26, 6};
        DrawItem storage[2];
        DrawList list(storage, 2);
        list.add(terrain_item(camera, projection, 0, 0, 0, 0, 0, 0));
        list.sort();
        expect(
            list.size() == 1 && list[0].x == 0 && list[0].y == 0 &&
                list[0].w == 26,
            "a one-cell board draws its one cell at the origin"
        );
    }

    // ---------------------------------------------------------------
    // The generated elevation table
    // ---------------------------------------------------------------
    {
        // Elevation is art-library data, like the flat colours and the
        // keywords beside it: the clients read the generator's table rather
        // than each keeping a list of raised terrain.
        expect(
            grandleon_terrain_kind_count == 13,
            "the elevation table covers the whole terrain registry"
        );
        int raised = 0;
        int highest = 0;
        bool non_negative = true;
        for (std::size_t kind = 0; kind < grandleon_terrain_kind_count;
             ++kind) {
            const int level = grandleon_terrain_elevation[kind];
            if (level < 0) non_negative = false;
            if (level > 0) ++raised;
            if (level > highest) highest = level;
        }
        expect(non_negative, "no terrain is authored below level ground");
        expect(
            raised > 0 && raised < grandleon_terrain_kind_count,
            "some terrain is raised and most is not"
        );
        expect(
            highest <= 3,
            "and the tallest step stays inside a console frame's headroom"
        );

        // The console picks its cell size from the board and clamps it to 26,
        // and it draws its status line in the top 14 rows of the frame. The
        // cap is what keeps the tallest terrain out of that band. On a board
        // that fills the frame and can reserve no headroom at all, it also
        // keeps a first-row peak from being drawn and sampled at a negative
        // framebuffer row. The console cannot prove this itself: no map any
        // Nintendo 64 gate draws stands above the valley floor.
        {
            bool inside_the_frame = true;
            for (int tile = 14; tile <= 26; ++tile) {
                if (max_lift_for(tile) >= 14) inside_the_frame = false;
            }
            expect(
                inside_the_frame,
                "every cell size the console can choose lifts a peak less "
                "than the status line's own height"
            );
        }

        // The two the art actually draws as high ground, by name, so a
        // renaming or a reordering of the registry cannot quietly move them.
        int mountain = -1;
        int hills = -1;
        int grass = -1;
        int water = -1;
        for (std::size_t kind = 0; kind < grandleon_terrain_kind_count;
             ++kind) {
            const std::string_view name = grandleon_terrain_kind_names[kind];
            if (name == "mountain") mountain = static_cast<int>(kind);
            if (name == "hills") hills = static_cast<int>(kind);
            if (name == "grass") grass = static_cast<int>(kind);
            if (name == "water") water = static_cast<int>(kind);
        }
        expect(
            mountain >= 0 && hills >= 0 && grass >= 0 && water >= 0,
            "the registry still holds the terrain this test names"
        );
        if (mountain >= 0 && hills >= 0 && grass >= 0 && water >= 0) {
            expect(
                grandleon_terrain_elevation[mountain] == 2,
                "mountains stand two levels above the valley"
            );
            expect(
                grandleon_terrain_elevation[hills] == 1,
                "hills stand one"
            );
            expect(
                grandleon_terrain_elevation[grass] == 0 &&
                    grandleon_terrain_elevation[water] == 0,
                "open ground and water are the level the rest is measured "
                "from"
            );
        }
    }

    return failures == 0 ? 0 : 1;
}
