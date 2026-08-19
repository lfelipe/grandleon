// SPDX-License-Identifier: MIT
// The board's presentation model, in time: the frame counts four renderers
// would otherwise each invent, and the arithmetic that turns a frame number
// into an offset.
//
// `board_view.hpp` owns where a cell is. This owns when a token is between two
// of them, how far a struck token is knocked back, and how bright the cursor
// is on a given frame. Same rules as its neighbour: pure integer arithmetic,
// no libdragon, no SDL, no engine header, no allocation, no clock. The host
// suite compiles it and pins every number, so the parts of motion that can
// silently go wrong are verified off the console and out of the browser.
//
// Two properties are load-bearing rather than tidy, because every console
// check in this repository asserts framebuffer pixels at named checkpoints:
//
//   * **Everything here is frame-counted.** Nothing reads a clock, nothing
//     measures elapsed time, nothing interpolates against a refresh rate. A
//     given press sequence produces the identical frame sequence on every run
//     and on every emulator, which is the only reason a pixel assertion can
//     stay exact while the board moves.
//
//   * **Phase zero is rest.** Every periodic function here returns, at phase
//     zero, exactly what was drawn before any of it existed. A checkpoint that
//     samples at phase zero therefore sees the board it saw before, and no
//     assertion had to be loosened to admit motion.
//
// The numbers themselves are stated here and mirrored in
// `editor/src/domain/board-motion.ts`, and the two test suites pin them
// against each other. Change one here and change it there.

#pragma once

#include <cstdint>

namespace grandleon::view {

// ---------------------------------------------------------------------------
// The frame counts
// ---------------------------------------------------------------------------
//
// All of them are counted in *drawn frames*, and every client that animates
// draws at its display's refresh: 60 Hz on an NTSC console, and one browser
// animation-frame tick in Play. A frame is therefore about 16.7 ms everywhere
// it matters. This repository does not build for PAL, and a PAL machine would
// run the same counts one sixth slower rather than drawing a different picture.

// How long a token spends crossing one tile. Six frames is a tenth of a second
// per tile: ten tiles a second reads as a walk rather than a slide or a jump.
// The longest move the demo content allows is four tiles, which takes 24
// frames: under half a second, and so never something to wait out.
//
// It is also divisible by two and three, so the halfway and third points of a
// step land on exact frames rather than on rounding.
inline constexpr int slide_frames_per_tile = 6;

// A landed hit: six frames, the first three with the struck token knocked away
// from whoever hit it and the last three with it back where it stands. Three
// frames out is a twentieth of a second: long enough to read as a blow taken,
// short enough that a four-hit exchange does not become a cutscene.
inline constexpr int flinch_frames = 6;
inline constexpr int flinch_knocked_frames = 3;

// A blow that did not land. Half a hit, and exactly the part of a hit in which
// something is happening: the striker is still throwing it for all three of
// these frames, and nothing is knocked anywhere, which is the whole of what a
// miss has to say.
//
// It is here because it was not. Three clients drew a miss for three frames,
// two and two, none of them reading a number from this file: the same defect
// the cell selection was moved here to fix. There is one duration and this
// is it.
inline constexpr int miss_frames = flinch_knocked_frames;

// How long a bolt, an arrow or a flare spends crossing one tile: half the six
// frames a body spends walking one, so a thing that was loosed visibly outruns
// a thing that walks and the two never read as the same speed.
//
// Three and not two because three is what `slide_frames_per_tile` halves to
// without a remainder, and not four because the longest band any shipped weapon
// authors is three tiles (a Long Bow's two-to-three), and four would put the
// longest shot at twelve frames, a fifth of a second in the air before anything
// has happened. At three it is nine, which is shorter than a two-tile walk.
inline constexpr int projectile_frames_per_tile = 3;

// How long a caster holds the pose before the spell resolves. The same six a
// landed blow takes, deliberately: the two gestures a body can make are then
// the same length of held pose, and a player learns one duration rather than
// two. It is also what makes a cast read as *deliberate* beside a swing: the
// swing's six frames contain the answer, the cast's six contain only the wait.
inline constexpr int cast_hold_frames = flinch_frames;

// The cursor's pulse period, in frames, and the part of it the emphasis is
// drawn for. Thirty-two frames is a little over half a second at 60 Hz; the
// emphasis is up for the second half, so the cursor spends as long at rest as
// it does emphasised and a photograph taken at phase zero is the cursor as it
// was drawn before this model existed.
//
// A power of two by choice: the phase is a mask rather than a division.
inline constexpr int cursor_pulse_period = 32;
inline constexpr int cursor_pulse_rest_frames = 16;

// How long the camera spends crossing one cell of an opening sweep: five
// frames, a shade under what a body spends walking one.
//
// It was three, which is what a loosed arrow costs, on the argument that a pan
// is not a body and may outrun one. Played on hardware it read as a flick
// rather than a look: twenty cells a second is faster than an eye tracks
// ground it has not seen before. Twelve is slow enough to read the board and
// still short of a body's own pace, so the sweep does not read as somebody
// walking the camera across.
inline constexpr int sweep_frames_per_cell = 5;

// And the longest a sweep may take however wide the board is. A hundred and
// fifty frames is two and a half seconds at 60 Hz.
//
// The cap is a patience limit and nothing more principled: below it a board
// twice as wide takes twice as long to cross, which is the whole of what the
// gesture says, and past it that stops being true rather than becoming a wait
// nobody asked for. It moved with the pace above so that the boards this
// repository can actually draw stay under it: thirty cells of travel is half
// again the widest window either console offers, and it is the width at which
// the cap starts to bind.
inline constexpr int sweep_frames_most = 150;

// ---------------------------------------------------------------------------
// Sliding
// ---------------------------------------------------------------------------

// How many frames a route of `steps` tiles takes. A route of no steps takes no
// frames, which is what lets a client ask this question before deciding
// whether to animate at all.
[[nodiscard]] constexpr int slide_frames_for(int steps) noexcept {
    return steps > 0 ? steps * slide_frames_per_tile : 0;
}

// Where a token is drawn part-way between two pixel positions: `frame` of
// `frames`, counted from 1 so that the last frame lands exactly on `to` rather
// than a rounding error short of it.
//
// Truncating division rather than rounding, deliberately: it is the same
// arithmetic on every client and one signed divide on each of them.
[[nodiscard]] constexpr int slide_between(
    int from, int to, int frame, int frames
) noexcept {
    if (frames <= 0) return to;
    if (frame >= frames) return to;
    if (frame <= 0) return from;
    return from + ((to - from) * frame) / frames;
}

// ---------------------------------------------------------------------------
// The opening sweep
// ---------------------------------------------------------------------------
//
// A board too wide for the screen opens at its right edge and travels to the
// left, so a player is shown how much board there is before being asked to play
// on it. A board that fits has no edges to reveal and no sweep: the whole of it
// is already on the screen.
//
// It ends where play begins, which on most boards is the left edge it was
// travelling to anyway. On a board whose player opens further in, the reveal
// reaches the left edge first and then comes back, rather than stopping short of
// the edge or cutting to the column play begins at. A reveal that cut would be
// two pictures where the gesture is meant to be one.
//
// This is the one gesture in this file with no mirror in
// `editor/src/domain/board-motion.ts`, and the reason is not an omission. The
// browser draws every board whole at whatever size the page gives it, so Play
// has no camera, no edges, and nothing to sweep. The mirror exists where both
// draw the same motion; here only the consoles draw any.
//
// It is not interruptible, which is a decision rather than a simplification. A
// press that skipped it would have to be read inside the sweep and then not
// acted on, and every recorded pad script in this repository counts its presses
// from the first one the client asks for. A gesture bounded at a second and a
// half, once when a board opens, is the cheaper side of that trade.

// How many frames one leg of `cells` takes, capped. A leg of no cells takes no
// frames, which is what lets a client ask this before deciding whether to sweep
// at all: a board whose camera cannot move horizontally answers zero.
[[nodiscard]] constexpr int sweep_frames_for(int cells) noexcept {
    if (cells <= 0) return 0;
    const int frames = cells * sweep_frames_per_cell;
    return frames < sweep_frames_most ? frames : sweep_frames_most;
}

// A reveal is up to two legs: out to the left edge, and back to the column play
// begins at. The turn is at the left edge and not at `to`, because the point is
// the width of the board rather than the way to where the player stands: a board
// whose player opens on the right would otherwise be revealed by not moving at
// all, which is the board they could already see.
//
// The second leg is empty for every board whose play begins at the left edge,
// which is the common one and is the single right-to-left travel that was asked
// for. It costs nothing to ask for when it is not there.
[[nodiscard]] constexpr int sweep_frames_total(int from, int to) noexcept {
    if (from <= 0) return 0;
    return sweep_frames_for(from) + sweep_frames_for(to);
}

// Which column the camera stands at on `frame` of that reveal, counted from
// zero so the first drawn frame is `from` itself: a sweep starts at a column
// nothing has been drawn at, unlike a slide whose token already stands where it
// starts. A frame at or past the end is `to`, so an overrun cannot drift.
[[nodiscard]] constexpr int sweep_at(int from, int to, int frame) noexcept {
    const int out = sweep_frames_for(from);
    if (frame < out) return slide_between(from, 0, frame, out);
    const int back = sweep_frames_for(to);
    return slide_between(0, to, frame - out, back);
}

// ---------------------------------------------------------------------------
// The route a slide is drawn along
// ---------------------------------------------------------------------------
//
// A move command reports where a unit landed, not how it got there: the route
// is not simulated, because nothing about the rules depends on it. It is
// drawn, exactly like elevation.
//
// It is still not the client's to invent. The engine's movement rule is a
// four-neighbour cheapest-path walk, and it exposes the result of that walk
// read-only as `reachable_tiles`. So the drawn route is a breadth-first walk
// over the tiles a client derives from that query. Every tile the token is
// drawn standing on is a tile the engine itself said this walk may be on. That
// is derivation from the query, not a second copy of the rule: this file does
// not know what terrain is, what a crossing is, what a cell charges, or how far
// a unit may move.
//
// **Crossing a tile and finishing on it are two questions, and the caller
// answers both.** A walk may pass through a character on its own side and may
// not stop on one. `reachable_tiles` lists where a walk may *end*, so it has
// an ally-shaped hole in it exactly where a route may need to go. The
// predicate handed in below is therefore the *crossable* set: the query's tiles
// plus the tiles the mover's own side is standing on. A caller that passed the
// query alone would be asking for a route through a set the walk itself is not
// confined to, and would get a straight line every time somebody filed past a
// fellow.
//
// The route's length is a count of tiles rather than of allowance spent, and
// that is right for the one job it has. A route is drawn, exactly like
// elevation, and the fewest tiles is the reading a viewer can follow; what the
// walk cost is a fact about the rule, which has already been applied.
//
// The crossable set is connected the way the route needs it to be, and not by
// luck: the cheapest way to any reported tile runs through tiles that cost
// strictly less to reach, so each of those is either reported too or is held by
// somebody the walk went past, and both are in the set. A destination in the
// set therefore always has a route through the set. When one is not found
// anyway, because a client had no query to hand or the board moved underneath
// it, the caller falls back to drawing a straight line.

// The four steps, in the engine's own order: north, east, south, west.
inline constexpr int route_neighbour_count = 4;
inline constexpr int route_neighbour_dx[route_neighbour_count] = {0, 1, 0, -1};
inline constexpr int route_neighbour_dy[route_neighbour_count] = {-1, 0, 1, 0};

// One tile of a drawn route.
struct RouteTile final {
    std::int16_t x{0};
    std::int16_t y{0};
};

// The distance a cell has not been reached at. One byte per cell, so a
// caller's scratch grid is `width * height` bytes: 300 for the largest board
// the demo content authors, and a static array on every console.
inline constexpr std::uint8_t route_unreached = 0xFFU;

// A caller-owned route: the tiles after the origin, in walking order, ending
// on the destination.
class Route final {
public:
    Route(RouteTile* storage, int capacity)
        : tiles_(storage), capacity_(capacity > 0 ? capacity : 0) {}

    void clear() { count_ = 0; }
    [[nodiscard]] int size() const { return count_; }
    [[nodiscard]] const RouteTile& operator[](int index) const {
        return tiles_[index];
    }

    // Only `plan_route` fills one, and only from the back.
    bool push_front(RouteTile tile) {
        if (count_ >= capacity_) return false;
        for (int i = count_; i > 0; --i) tiles_[i] = tiles_[i - 1];
        tiles_[0] = tile;
        ++count_;
        return true;
    }

private:
    RouteTile* tiles_{nullptr};
    int capacity_{0};
    int count_{0};
};

// Plans the drawn route from `origin` to `destination`.
//
// `crossable(x, y)` answers whether this walk may be on that tile at all: the
// tiles the simulation's reachability query listed, plus the tiles the mover's
// own side holds, which a walk goes through without stopping. See the note
// above on why those are two sets and not one. The origin is in neither set,
// since a unit does not move to where it stands, and is handled here rather
// than asked about.
//
// `distance` is caller-owned scratch of `width * height` bytes, overwritten in
// full. Nothing is allocated, so a console can hold one in `.bss` for the life
// of the ROM.
//
// Returns true when a route of at least one tile was found and fitted in the
// route's capacity. Returns false and leaves the route empty when the
// destination is not crossable, when no route through the crossable tiles
// reaches it, or when the route is longer than the caller made room for. Every
// one of those is a "draw the straight line instead", never a guess.
//
// The walk is a wavefront rather than a queue, because a queue would need a
// second caller-owned array and the boards are small: at most
// `width * height` cells are scanned once per distance, and the scan stops the
// moment the destination is reached. The route is then read back from the
// distance field by stepping to the first neighbour, in the neighbour order
// above, that stands one closer to the origin. The route is therefore a
// function of the distance field alone and is identical on every client,
// whatever order a queue would have discovered the tiles in.
template <typename Crossable>
bool plan_route(
    int origin_x,
    int origin_y,
    int destination_x,
    int destination_y,
    int width,
    int height,
    Crossable crossable,
    std::uint8_t* distance,
    Route& route
) {
    route.clear();
    if (distance == nullptr || width <= 0 || height <= 0) return false;
    const auto in_bounds = [width, height](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height;
    };
    if (!in_bounds(origin_x, origin_y)) return false;
    if (!in_bounds(destination_x, destination_y)) return false;
    if (origin_x == destination_x && origin_y == destination_y) return false;
    if (!crossable(destination_x, destination_y)) return false;

    const int cells = width * height;
    for (int i = 0; i < cells; ++i) distance[i] = route_unreached;
    const auto slot = [width](int x, int y) { return y * width + x; };
    distance[slot(origin_x, origin_y)] = 0;

    const int destination_slot = slot(destination_x, destination_y);
    bool arrived = false;
    for (int step = 0; step < cells && !arrived; ++step) {
        const auto front = static_cast<std::uint8_t>(step);
        if (front == route_unreached - 1) break;
        bool grew = false;
        for (int y = 0; y < height && !arrived; ++y) {
            for (int x = 0; x < width && !arrived; ++x) {
                if (distance[slot(x, y)] != front) continue;
                for (int n = 0; n < route_neighbour_count; ++n) {
                    const int nx = x + route_neighbour_dx[n];
                    const int ny = y + route_neighbour_dy[n];
                    if (!in_bounds(nx, ny)) continue;
                    const int here = slot(nx, ny);
                    if (distance[here] != route_unreached) continue;
                    if (!crossable(nx, ny)) continue;
                    distance[here] = static_cast<std::uint8_t>(front + 1);
                    grew = true;
                    if (here == destination_slot) arrived = true;
                }
            }
        }
        if (!grew) break;
    }
    if (!arrived) return false;

    int x = destination_x;
    int y = destination_y;
    while (distance[slot(x, y)] != 0) {
        if (!route.push_front(RouteTile{
                static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)
            })) {
            route.clear();
            return false;
        }
        const auto want = static_cast<std::uint8_t>(distance[slot(x, y)] - 1);
        int next_x = x;
        int next_y = y;
        bool stepped = false;
        for (int n = 0; n < route_neighbour_count && !stepped; ++n) {
            const int nx = x + route_neighbour_dx[n];
            const int ny = y + route_neighbour_dy[n];
            if (!in_bounds(nx, ny)) continue;
            if (distance[slot(nx, ny)] != want) continue;
            next_x = nx;
            next_y = ny;
            stepped = true;
        }
        if (!stepped) {
            route.clear();
            return false;
        }
        x = next_x;
        y = next_y;
    }
    return route.size() > 0;
}

// ---------------------------------------------------------------------------
// The cursor's pulse
// ---------------------------------------------------------------------------

// Whether the cursor's emphasis is drawn on this frame. False at phase zero,
// and false for the first half of every period, so a client that pins the
// phase before asserting sees the cursor it drew before this model existed.
//
// The counter is unsigned and the period a power of two, so this is exact
// across every wrap of the counter rather than jittering once every few hours.
[[nodiscard]] constexpr bool cursor_emphasised(std::uint32_t frame) noexcept {
    return (frame % static_cast<std::uint32_t>(cursor_pulse_period)) >=
           static_cast<std::uint32_t>(cursor_pulse_rest_frames);
}

// How far inside the cursor's own rectangle the emphasis ring is drawn. Two
// pixels on the console's 25-pixel cell, one on anything smaller, always
// strictly short of the cell's centre. That is where every framebuffer probe
// in this repository samples a cell, so the pulse can never change what a
// probe reads.
//
// Zero for a cell with no room for a ring inside its own centre, which is a
// cell no client draws; a renderer reading zero draws no emphasis rather than
// drawing one over the pixel a probe is about to sample.
[[nodiscard]] constexpr int cursor_emphasis_inset(int tile) noexcept {
    if (tile <= 0) return 0;
    // The last inset that is still nearer the edge than the centre pixel.
    const int limit = (tile + 1) / 2 - 1;
    if (limit < 1) return 0;
    const int inset = tile / 12;
    if (inset < 1) return 1;
    return inset < limit ? inset : limit;
}

// ---------------------------------------------------------------------------
// The flinch
// ---------------------------------------------------------------------------

// How far a struck token is knocked back, in pixels, for a given cell size. An
// eighth of a tile (three pixels on the console's 25-pixel cell) is a nudge
// rather than a leap, and it is short of the quarter-tile elevation step, so a
// flinch never reads as a change of height.
//
// At least one pixel for any cell at all: a flinch nobody can see is a flinch
// that was not drawn.
[[nodiscard]] constexpr int flinch_nudge_for(int tile) noexcept {
    if (tile <= 0) return 0;
    const int nudge = tile / 8;
    return nudge > 0 ? nudge : 1;
}

// The offset a struck token is drawn at on `frame` of the flinch, counted from
// zero. It is knocked directly away from whoever struck it for the first half
// and stands at rest for the second, so the last frame of a flinch is the
// board at rest and a checkpoint that samples after it sees no offset at all.
//
// `toward_x`/`toward_y` point from the striker to the struck; only their signs
// are read, so a caller may hand over a whole difference vector. A strike from
// nowhere in particular, an area effect with no origin on the board, knocks
// nothing anywhere and draws the flash alone.
[[nodiscard]] constexpr int flinch_offset(
    int frame, int toward, int tile
) noexcept {
    if (frame < 0 || frame >= flinch_knocked_frames) return 0;
    if (toward == 0) return 0;
    const int nudge = flinch_nudge_for(tile);
    return toward > 0 ? nudge : -nudge;
}

// ---------------------------------------------------------------------------
// The travelling mark: a projectile's flight and an effect's bloom
// ---------------------------------------------------------------------------
//
// The cheapest thing in this whole slice, and deliberately so. A bolt crossing
// the board and a flare opening on a tile are **not poses of a body**: they are
// marks over it, and a mark drawn from primitives in colours the palette already
// holds costs no sprite cell, no appended palette entry, no tile of its own and
// no second drawing routine per archetype. That is what leaves the last sequence
// cell free for the one gesture only a body can make.
//
// Where a bolt is on a given frame needs no arithmetic of its own: it is
// `slide_between` called with the shooter's and the target's pixel positions
// instead of two tiles, which is the same interpolation a token walks on and
// therefore the same rounding on every client.
//
// What a bolt's height above the board and a flare's radius *do* need is a
// shape that starts at nothing, swells, and comes back to nothing. It is the
// same shape for both, so it is one function. Zero at both ends is not a
// nicety: it is the settle rule applied to a mark. A checkpoint that samples
// the frame a gesture ends sees no projectile and no flare, so a pixel
// assertion over a settled board never has one in it.

// How many frames a flight of `tiles` tiles takes. Zero tiles takes no frames,
// which is what lets a caller ask before deciding whether there is a flight at
// all. A swing has none.
[[nodiscard]] constexpr int projectile_frames_for(int tiles) noexcept {
    return tiles > 0 ? tiles * projectile_frames_per_tile : 0;
}

// A quantity that rises from nothing to `peak` at the middle of `frames` and
// falls back to nothing: a bolt's arc over the board, and the radius of the
// flare a cast resolves into.
//
// The parabola `4 * f * (frames - f) / frames^2` scaled by `peak`, in integers,
// with the multiplication done before the division so a peak of a few pixels
// does not truncate to nothing on the way up. It is exactly zero at `frame` 0
// and at `frame == frames`, by algebra rather than by clamping, which is why it
// can be trusted to settle.
[[nodiscard]] constexpr int rise_and_fall(
    int frame, int frames, int peak
) noexcept {
    if (frames <= 0 || peak <= 0) return 0;
    if (frame <= 0 || frame >= frames) return 0;
    return (4 * peak * frame * (frames - frame)) / (frames * frames);
}

// How high a bolt arcs above the straight line between shooter and target, at
// the top of its flight. A quarter of a tile is the elevation step exactly,
// six pixels on the console's 25-pixel cell, so a bolt rises as far as
// one drawn tier of height and no further. Elevation is drawn and never
// simulated; this borrows its unit for the same reason, and it means an arc can
// never be mistaken for a token climbing.
[[nodiscard]] constexpr int projectile_arc_peak(int tile) noexcept {
    if (tile <= 0) return 0;
    const int peak = tile / 4;
    return peak > 0 ? peak : 1;
}

// How wide the flare a cast resolves into is drawn, at its widest. Half a tile,
// so the mark fills the cell it is resolving on and never the cells beside it.
// An area cast is drawn as one flare per struck token, not as one flare over
// the shape, because the shape is the engine's and the tokens are what a client
// was told about.
[[nodiscard]] constexpr int effect_bloom_peak(int tile) noexcept {
    if (tile <= 0) return 0;
    const int peak = tile / 2;
    return peak > 0 ? peak : 1;
}

// ---------------------------------------------------------------------------
// Which cell of a sequence is drawn
// ---------------------------------------------------------------------------
//
// The roster has real frames: a walk cycle and an attack lunge, generated as
// *poses* applied to the body each
// archetype's own routine draws (`tools/placeholder_art/placeholder_art/
// frames.py`). Every style ships every cell, in one order, and a client indexes
// a sequence sheet by position. The cell a renderer draws is therefore
// arithmetic, and arithmetic belongs here rather than in four renderers.
//
// The standing sprite is **frame 0 of every sequence** and ships as its own
// file. The sheet beside it holds the cells below, in this order.

// Cells of a sequence sheet, in sheet order. `sequence_cell_stand` is not one
// of them: it names the standing sprite, which a renderer already holds.
inline constexpr int sequence_cell_stand = -1;
inline constexpr int sequence_cell_walk_contact = 0;
inline constexpr int sequence_cell_walk_pass = 1;
inline constexpr int sequence_cell_lunge = 2;
inline constexpr int sequence_cell_cast = 3;
inline constexpr int sequence_cell_count = 4;

// ---------------------------------------------------------------------------
// Why the count above may not grow again
// ---------------------------------------------------------------------------
//
// The four cells above spend the ceiling exactly, so the arithmetic is written
// here as a refusal rather than as prose. Prose refuses nothing:
// `sequence_cell_count` could be raised to five and every build, every ROM and
// every check would go on passing, because each renderer blits one cell's
// sub-rectangle at a time. What would break is the property that a client *may*
// upload a strip whole, and it would break silently, on the machine that could
// least afford it.
//
// The wall is the Nintendo 64's texture memory. TMEM is 4,096 bytes; a
// colour-indexed format spends the upper half on the palette bank, leaving
// `tmem_texel_bytes` for texels. A cell is `sequence_cell_pixels` square at
// `sequence_cell_bits` bits a texel, which is 512 bytes, and 2,048 divided by
// 512 is four. The terrain sheet is already a 128x32 CI4 at exactly that size,
// which is the independent check that the number is the machine's and not a
// guess.
//
// A fifth cell is therefore not a matter of adding a pose. It costs a narrower
// cell, a second upload, or a format change, and whichever of those is chosen
// belongs written out beside this arithmetic rather than in a commit that
// quietly raised a constant.
inline constexpr int sequence_cell_pixels = 32;
inline constexpr int sequence_cell_bits = 4;
inline constexpr int tmem_texel_bytes = 2048;
inline constexpr int sequence_cell_bytes =
    sequence_cell_pixels * sequence_cell_pixels * sequence_cell_bits / 8;
inline constexpr int sequence_cell_ceiling =
    tmem_texel_bytes / sequence_cell_bytes;

static_assert(
    sequence_cell_bytes == 512,
    "a 32x32 CI4 cell is 512 bytes of TMEM; if this moved, the cell size or "
    "the texture format did, and the ceiling below moved with it"
);
static_assert(
    sequence_cell_count <= sequence_cell_ceiling,
    "a sequence sheet does not fit the texture memory of the client that can "
    "least afford it: 2,048 texel bytes of the Nintendo 64's TMEM divided by "
    "512 bytes a cell is four cells, and the sheet asks for more. Narrow "
    "the cell, upload the strip in pieces, or change the format, and write "
    "which beside this arithmetic. Do not raise this count alone"
);

// How long one cell of the walk is held: a whole tile. Two cells and not four
// is arithmetic rather than taste. At `slide_frames_per_tile` frames a tile, a
// four-pose cycle holds each pose for a frame and a half, and a pose held for a
// fraction of a frame is a pose no console draws. One cell a tile also makes
// the cycle mean what a tile means: one step.
inline constexpr int walk_cells = 2;

// The cell a walking token is drawn as, `frame` frames into a slide counted
// from zero, where frame zero is the moment before it has moved at all.
//
// The two ends are the load-bearing part. At frame zero, and at the frame a
// slide of `frames` frames arrives, this returns the standing cell. A walk
// *begins and ends standing*, so the rule that every animation ends exactly at
// rest holds of the pose as well as of the offset. A checkpoint that samples a
// settled board therefore sees the standing sprite.
[[nodiscard]] constexpr int walk_cell(int frame, int frames) noexcept {
    if (frame <= 0) return sequence_cell_stand;
    if (frames > 0 && frame >= frames) return sequence_cell_stand;
    const int step = (frame - 1) / slide_frames_per_tile;
    return (step % walk_cells) == 0 ? sequence_cell_walk_contact
                                    : sequence_cell_walk_pass;
}

// The cell a striking token is drawn as, `frame` frames into a hit counted from
// zero. It coils for exactly as long as the struck token is knocked away, and
// stands for the rest. The striker and its target are two halves of one
// gesture, and the last frame of it is the board at rest.
[[nodiscard]] constexpr int strike_cell(int frame) noexcept {
    if (frame < 0 || frame >= flinch_knocked_frames) return sequence_cell_stand;
    return sequence_cell_lunge;
}

// The cell a casting token is drawn as, `frame` frames into the hold counted
// from zero. A cast is a pose *held* rather than a pose thrown, which is the
// difference between the two gestures a body can make, so it is the same
// arithmetic as a strike over a longer count and it ends standing for the same
// reason.
[[nodiscard]] constexpr int cast_cell(int frame) noexcept {
    if (frame < 0 || frame >= cast_hold_frames) return sequence_cell_stand;
    return sequence_cell_cast;
}

// ---------------------------------------------------------------------------
// Which gesture an attack is drawn as
// ---------------------------------------------------------------------------
//
// The engine reports an attack as a damage event or a miss event carrying who
// struck, who was struck, and where. It does not say whether a weapon or an
// ability made the blow, and it is not going to: an event field whose purpose
// is to name an animation would be gameplay state added to serve presentation,
// which is the boundary this repository has defended since the first client.
//
// So the gesture is *derived*, from what a presenter already holds. Every
// client is handed the weapon and ability records before the first frame
// (`Presenter::battle_definitions`), and the snapshot names what each unit
// carries and knows. Between them the presenter can ask the only question that
// matters here, and it is one question:
//
//   **Could a damaging magical ability this striker knows have crossed this
//   separation?**
//
// If it could, the blow is drawn as a cast. If it could not, a weapon threw it:
// a shot when it crossed at least one tile, and a swing when it did not.
//
// The rule's cost is named rather than hidden, because it is a real one. A mage
// whose staff reaches as far as its spell has both its staff blows and its spell
// blows drawn as casts, since to a presenter they are the same event. That is
// the right way round and was chosen rather than tolerated: a mage does not
// fence, and drawing a spell as a staff-poke would be the larger lie. The
// converse rule, preferring the weapon wherever a weapon reaches, was rejected
// on the shipped content, where it drew both of the Dawn Mage's spells as staff
// jabs at every range but one.
//
// Two properties follow, and both are the reason this lives here:
//
//   * It is a pure function of integers, so the host derivation that produces a
//     console check's expectations computes the same gesture the console draws.
//   * It is the same function on four renderers, so a blow does not change
//     picture when a player changes machine.

enum class AttackGesture : std::uint8_t {
    // A blow at arm's length: the striker coils and the target is knocked.
    swing = 0,
    // A blow that crossed a tile: something travels before anything is knocked.
    shot = 1,
    // A blow only magic could have thrown: a pose held, and a mark on a tile.
    cast = 2,
};

// Whether a reach band covers a separation. The band is inclusive at both ends,
// exactly as the engine's own `within_reach` reads it: a bow of two to three
// answers neither an adjacent enemy nor one four tiles away. Restated here in
// integers rather than shared, because sharing it would mean this header
// including an engine header, which is the one thing it may not do; it is two
// comparisons, and `tests/view/motion_test.cpp` pins them against the bands the
// shipped content actually authors.
[[nodiscard]] constexpr bool reach_covers(
    int separation, int minimum, int maximum
) noexcept {
    return separation >= minimum && separation <= maximum;
}

// The gesture an attack is drawn as.
//
//   `separation`     how far apart the two stood, on the engine's own metric.
//   `magic_reaches`  whether any damaging magical ability the striker knows has
//                    a band covering that separation.
//   `launches`       whether the weapon in the striker's hand **cannot strike an
//                    adjacent tile**: its minimum reach is more than one.
//
// The third argument is the one that had to be learned by measurement rather
// than reasoned out. "The blow crossed a tile, so something flew" was the first
// rule written here, and a scripted session over the shipped content refuted
// it: the Warden's Vow Glaive and the mill hand's Boat Hook both reach one tile
// *and* two, and drawing a pole with an iron beak as a loosed arrow is exactly
// as wrong as drawing a spell as a staff-poke. A thrust that reaches further is
// still a thrust.
//
// What tells a launcher from a long arm is not how far it reaches but whether
// it can be used up close. A true bow has a minimum of two. `long_bow` in
// `games/tarnholt` says so, and its own note says why: "it cannot strike an
// adjacent enemy, so an archer caught in melee has to step back first". That
// number is already resolved onto the snapshot by the engine, from the weapon
// actually in hand, so this costs a caller one field and no record lookup.
//
// A separation of zero is a blow from nobody on the board, drawn as a flash
// alone. It is a swing, so a renderer that poses nothing still asks this and
// still gets an answer it can draw.
[[nodiscard]] constexpr AttackGesture attack_gesture(
    int separation, bool magic_reaches, bool launches
) noexcept {
    if (magic_reaches) return AttackGesture::cast;
    if (separation > 1 && launches) return AttackGesture::shot;
    return AttackGesture::swing;
}

// How long the striker's own half of a gesture lasts, before anything is
// knocked. A swing throws the blow and the flinch answers it in the same six
// frames, so it opens nothing; a shot opens for as long as the bolt is in the
// air; a cast opens for as long as the pose is held.
[[nodiscard]] constexpr int gesture_lead_frames(
    AttackGesture gesture, int separation
) noexcept {
    if (gesture == AttackGesture::cast) return cast_hold_frames;
    if (gesture == AttackGesture::shot) {
        return projectile_frames_for(separation);
    }
    return 0;
}

// The whole gesture, lead and resolution together: what a client counts down
// and what a host derivation predicts. `landed` is whether the blow took
// anything, which is the one fact the engine does report.
[[nodiscard]] constexpr int gesture_frames(
    AttackGesture gesture, int separation, bool landed
) noexcept {
    return gesture_lead_frames(gesture, separation) +
           (landed ? flinch_frames : miss_frames);
}

// ---------------------------------------------------------------------------
// Animated terrain: the water shimmer's phase
// ---------------------------------------------------------------------------
//
// Water moves by **palette cycling**: a few entries of its ramp are rotated in
// the display palette, by a TLUT override on the Nintendo 64's rdpq path and a
// lookup substitution in the browser. Which entries is a property of the art
// and is published by the generator (`grandleon_water_cycle_*` in `themes.h`).
// *When* is here, because it is timing.
//
// This is the first animation in the repository that never settles, and that is
// the whole design problem. Every gesture above is bounded and a shimmer is
// not. So its phase is pinned to the same counter the cursor's pulse reads:
// the board's own frame counter, which starts at zero on entry to the input
// loop, advances only while a player is holding the board, and is put back to
// rest on the way out and by every checkpoint. Two things follow:
//
//   * **Phase zero is the identity permutation.** A checkpoint photographs the
//     palette exactly as the console loaded it, so every pixel expectation
//     stands unloosened for the same reason a bounded gesture's does.
//   * **A checkpoint samples a predicted phase, never a lucky one.** The phase
//     is a pure function of a counter the checks already control.
//
// The free-running alternative, a timer incremented from boot by the vertical
// interrupt, was rejected precisely because it is unrelated to any press: a
// checkpoint would photograph whichever phase the run happened to reach.

// How many entries the rotation moves, how long it holds each step, and the
// period that follows. Both numbers are powers of two, so the phase is a shift
// and a mask rather than a division, and is exact across every wrap of the
// counter.
//
// Eight frames a step is a little over a seventh of a second: slow enough to
// read as light moving on water rather than as a flicker, and the four steps
// come to the cursor pulse's own thirty-two-frame period, so the two periodic
// things on the board agree rather than beating against each other.
inline constexpr int water_cycle_entries = 4;
inline constexpr int water_cycle_step_frames = 8;
inline constexpr int water_cycle_period =
    water_cycle_entries * water_cycle_step_frames;

// How far the water ramp is rotated on a given frame: 0, 1, 2 or 3. Zero at
// phase zero, which is the palette the console loaded.
[[nodiscard]] constexpr int water_cycle_phase(std::uint32_t frame) noexcept {
    return static_cast<int>(
        (frame / static_cast<std::uint32_t>(water_cycle_step_frames)) %
        static_cast<std::uint32_t>(water_cycle_entries));
}

// Which entry of the cycled window a client should write into slot `slot` on a
// given frame. The rotation is a permutation of the window and nothing else: at
// phase zero it is the identity, and at every phase it is a bijection, so no
// colour is ever written twice and none is ever dropped.
//
// It rotates *forward*: the colour that was in the slot before moves along, so
// the highlight travels from the deep water toward the crest, which is the
// direction a swell travels.
[[nodiscard]] constexpr int water_cycle_source(
    int slot, std::uint32_t frame
) noexcept {
    if (slot < 0 || slot >= water_cycle_entries) return slot;
    return (slot + water_cycle_phase(frame)) % water_cycle_entries;
}

// Whether the frame after `frame` draws a different palette from `frame`. A
// client repaints only when this is true, once every `water_cycle_step_frames`
// rather than every frame. That is the same economy the cursor pulse makes,
// and the reason the shimmer costs almost nothing.
[[nodiscard]] constexpr bool water_cycle_changes(std::uint32_t frame) noexcept {
    return water_cycle_phase(frame) != water_cycle_phase(frame + 1U);
}

// ---------------------------------------------------------------------------
// A character that has already taken its turn
//
// Drawn in grey rather than in its own colours. A player planning a turn is
// reading a line of characters and asking which of them they still have; a
// marker on the edge of a token answers that only for whoever already knows to
// look at the edge, and playing this on a cartridge shows that a player does
// not.
//
// Greyed and not hidden, and greyed in place: the character stays on its tile,
// at full position, and everything a plan needs off it (where it is, how much
// health it has, what it is called) stays exactly where it was. What goes is
// the colour, which is the one thing on a token that is about *whose turn it
// is* rather than about the character.
//
// **This is a function of the pixel and of nothing else, and that is what makes
// the picture checkable.** A console draws a spent character by pushing every
// entry of its drawing's palette through here; a framebuffer probe recomputes
// the same value from the same drawing's texel and asserts equality. So the
// check does not learn a second vocabulary of pinned grey colours, and it says
// something *stronger* than a check against a token drawn in its own colours
// can: that the cell holds this character's own drawing **and** that the
// drawing was greyed.
// A console that forgot to grey a spent character, or greyed an unspent one,
// fails on the pixel rather than on a screenshot nobody reads.
//
// Rec. 601 luma, which is the weighting that keeps a red token and a blue token
// of equal lightness equally light. The naive average would darken the blue
// side more than the red and read as a difference between the sides rather than
// between spent and unspent. Then two thirds of it, because a spent character
// at full luma is a *lighter* token than an unspent one on this palette's dark
// ground and reads as emphasis rather than as absence.
//
// Integer throughout: every console this draws on has the arithmetic, and the
// host that derives the expectation must get the identical answer.
inline constexpr int spent_luma_red = 77;
inline constexpr int spent_luma_green = 150;
inline constexpr int spent_luma_blue = 29;
inline constexpr int spent_dim_numerator = 2;
inline constexpr int spent_dim_denominator = 3;

// The grey a spent character's pixel is drawn in, from that pixel's own eight-
// bit channels. Answers 0 to 255, clamped at both ends because a caller may
// hand in a channel that has already been expanded from five bits.
[[nodiscard]] constexpr int spent_grey(int red, int green, int blue) noexcept {
    const auto clamp8 = [](int value) {
        return value < 0 ? 0 : (value > 255 ? 255 : value);
    };
    const int luma = (clamp8(red) * spent_luma_red +
                      clamp8(green) * spent_luma_green +
                      clamp8(blue) * spent_luma_blue) >>
                     8;
    return clamp8((luma * spent_dim_numerator) / spent_dim_denominator);
}

}  // namespace grandleon::view
