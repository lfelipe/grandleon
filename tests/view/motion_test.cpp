// SPDX-License-Identifier: MIT
// Host-side test for the board's presentation model in time.
//
// Motion is the part of drawing that is hardest to see going wrong: a slide
// that ends a pixel short, a route that cuts a corner through a river, a pulse
// whose phase drifts, all look approximately right on a console and none of
// them can be reviewed from a screenshot. So the arithmetic is pinned here, on
// the host, with no console and no browser in the loop, and the renderers own
// only the blitting.
//
// Two properties are asserted rather than assumed, because every console check
// in this repository asserts framebuffer pixels: every animation ends exactly
// on its resting position, and every periodic function is at rest at phase
// zero.
//
// The route is checked against the simulation's own rule the only way this
// file may check it: by construction. It is handed a membership predicate
// standing in for the tiles a walk may be on, and every assertion is about the
// route staying inside that set. Nothing here knows what terrain is, and
// nothing here knows whose side anybody is on: which tiles a caller puts in the
// set is the caller's rule, and this file only holds the route to it.

#include "grandleon/view/motion.hpp"

// For one assertion only: that a flinch's nudge stays smaller than the step a
// cell rises per level of elevation, so a blow taken never reads as a climb.
#include "grandleon/view/board_view.hpp"

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

// A small board written as text, the way the engine's own fixtures read.
// '#' is a tile the reachability query did not return; anything else is a tile
// it did. The origin is 'o' and is deliberately *not* in the set, exactly as
// `sim::reachable_tiles` leaves it out.
struct Board final {
    int width{0};
    int height{0};
    std::vector<std::string_view> rows;

    [[nodiscard]] bool reachable(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        const char cell = rows[static_cast<std::size_t>(y)]
                              [static_cast<std::size_t>(x)];
        return cell != '#' && cell != 'o';
    }
};

}  // namespace

int main() {
    using namespace grandleon::view;

    // ---------------------------------------------------------------
    // The frame counts themselves
    // ---------------------------------------------------------------
    //
    // Pinned as numbers here against the header, because the header is where
    // an author reads them and this is what holds it to them. Changing one is
    // a decision, not a refactor, and this is where the decision is recorded.
    {
        expect(slide_frames_per_tile == 6, "a tile takes six frames to cross");
        expect(flinch_frames == 6, "a flinch is six frames");
        expect(
            flinch_knocked_frames == 3,
            "a flinch is knocked back for three of them"
        );
        expect(miss_frames == 3, "a miss is three frames");
        expect(
            miss_frames < flinch_frames,
            "always shorter than a hit, which is half of how the two are told "
            "apart on a machine with no colour to spare for the other half"
        );
        expect(
            miss_frames >= flinch_knocked_frames,
            "and long enough to hold the striker's whole pose, so a missed "
            "blow is still visibly thrown"
        );
        expect(cast_hold_frames == 6, "a cast is held for six frames");
        expect(cursor_pulse_period == 32, "the cursor pulses every 32 frames");
        expect(
            cursor_pulse_rest_frames == 16,
            "the cursor rests for half of its period"
        );
        expect(
            cursor_pulse_period % cursor_pulse_rest_frames == 0,
            "the pulse divides evenly, so no phase is a rounding error"
        );
        expect(
            (cursor_pulse_period & (cursor_pulse_period - 1)) == 0,
            "the period is a power of two, so the phase is a mask"
        );
        expect(slide_frames_per_tile % 2 == 0, "half a tile lands on a frame");
        expect(slide_frames_per_tile % 3 == 0, "a third of a tile does too");
    }

    // ---------------------------------------------------------------
    // Sliding
    // ---------------------------------------------------------------
    {
        expect(slide_frames_for(0) == 0, "a route of no tiles takes no frames");
        expect(slide_frames_for(1) == 6, "one tile takes six frames");
        expect(slide_frames_for(4) == 24, "four tiles take 24 frames");
        expect(slide_frames_for(-3) == 0, "a negative route takes no frames");

        // The property every pixel assertion downstream depends on: the last
        // frame is the destination exactly, not a rounding error short of it.
        for (int frames = 1; frames <= 12; ++frames) {
            for (int from = -40; from <= 40; from += 7) {
                for (int to = -40; to <= 40; to += 11) {
                    expect(
                        slide_between(from, to, frames, frames) == to,
                        "the last frame of a slide lands exactly"
                    );
                    expect(
                        slide_between(from, to, 0, frames) == from,
                        "frame zero of a slide is where it started"
                    );
                    expect(
                        slide_between(from, to, frames + 5, frames) == to,
                        "a frame past the end stays at the end"
                    );
                }
            }
        }
        expect(
            slide_between(0, 32, 3, 6) == 16, "half way is half the distance"
        );
        expect(
            slide_between(100, 68, 3, 6) == 84, "and backwards, symmetrically"
        );
        expect(slide_between(5, 9, 2, 0) == 9, "no frames means arrived");

        // Monotone, so nothing ever appears to step backwards mid-slide.
        for (int frame = 1; frame <= 6; ++frame) {
            expect(
                slide_between(0, 25, frame, 6) >=
                    slide_between(0, 25, frame - 1, 6),
                "a slide never steps backwards"
            );
        }
    }

    // ---------------------------------------------------------------
    // The route
    // ---------------------------------------------------------------
    RouteTile route_storage[64];
    std::uint8_t distance[64 * 64];

    // A straight walk east.
    {
        const Board board{
            5, 1, {"o...."}
        };
        Route route(route_storage, 64);
        const bool found = plan_route(
            0, 0, 3, 0, board.width, board.height,
            [&board](int x, int y) { return board.reachable(x, y); },
            distance, route
        );
        expect(found, "a clear line is a route");
        expect(route.size() == 3, "three tiles east is three route tiles");
        expect(route[0].x == 1 && route[0].y == 0, "the first step is adjacent");
        expect(
            route[2].x == 3 && route[2].y == 0,
            "the last route tile is the destination"
        );
    }

    // The whole point: a route around water, not through it. The straight line
    // from the origin to the destination crosses two tiles the query never
    // returned; the route crosses none.
    {
        const Board board{
            5, 3,
            {
                "o....",
                "##.##",
                ".....",
            }
        };
        Route route(route_storage, 64);
        const bool found = plan_route(
            0, 0, 0, 2, board.width, board.height,
            [&board](int x, int y) { return board.reachable(x, y); },
            distance, route
        );
        expect(found, "a route exists around the water");
        expect(route.size() == 6, "and it is the six-tile way round");
        bool all_reachable = true;
        for (int i = 0; i < route.size(); ++i) {
            if (!board.reachable(route[i].x, route[i].y)) all_reachable = false;
        }
        expect(
            all_reachable,
            "every tile the token is drawn on is a tile the engine returned"
        );
        bool adjacent = true;
        int previous_x = 0;
        int previous_y = 0;
        for (int i = 0; i < route.size(); ++i) {
            const int dx = route[i].x - previous_x;
            const int dy = route[i].y - previous_y;
            if (dx * dx + dy * dy != 1) adjacent = false;
            previous_x = route[i].x;
            previous_y = route[i].y;
        }
        expect(adjacent, "the route steps one orthogonal tile at a time");
        expect(
            route[route.size() - 1].x == 0 && route[route.size() - 1].y == 2,
            "and it ends on the destination"
        );
    }

    // A tile the walk crosses without stopping on it. `sim::reachable_tiles`
    // lists where a walk may *end*, so an ally the walk filed past is a hole in
    // it, and a route planned over the query alone finds no way through and
    // hands the caller a straight line. The crossable set is the query plus
    // that ally's tile, and the route goes straight over it.
    //
    // Three tiles in a row: the origin, an ally, and the tile beyond.
    {
        const auto landable = [](int x, int y) {
            return y == 0 && x == 2;
        };
        const auto crossable = [](int x, int y) {
            return y == 0 && (x == 1 || x == 2);
        };
        Route stuck(route_storage, 64);
        expect(
            !plan_route(0, 0, 2, 0, 3, 1, landable, distance, stuck) &&
                stuck.size() == 0,
            "the landable tiles alone leave a hole and plan no route"
        );
        Route route(route_storage, 64);
        const bool found =
            plan_route(0, 0, 2, 0, 3, 1, crossable, distance, route);
        expect(found, "the crossable tiles carry the route through");
        expect(
            route.size() == 2 && route[0].x == 1 && route[0].y == 0 &&
                route[1].x == 2 && route[1].y == 0,
            "and the token is drawn stepping over the tile it may not stop on"
        );
    }

    // A destination the query did not return is not routed to at all: the
    // caller falls back to the straight line rather than being handed a guess.
    {
        const Board board{
            3, 1, {"o#."}
        };
        Route route(route_storage, 64);
        const bool found = plan_route(
            0, 0, 2, 0, board.width, board.height,
            [&board](int x, int y) { return board.reachable(x, y); },
            distance, route
        );
        expect(!found, "an unreturned destination plans no route");
        expect(route.size() == 0, "and leaves the route empty");
    }

    // A destination in the set but walled off from the origin inside it. The
    // engine's own walk cannot produce this (it only reports what it expanded
    // into), but a client whose board moved underneath it can ask, and the
    // answer has to be "draw the straight line".
    {
        const Board board{
            3, 1, {"o#."}
        };
        Route route(route_storage, 64);
        const bool found = plan_route(
            0, 0, 2, 0, 3, 1,
            [](int x, int y) { return y == 0 && (x == 2); }, distance, route
        );
        (void)board;
        expect(!found, "an unreachable destination plans no route");
    }

    // Standing still is not a route, and neither is anything off the board.
    {
        Route route(route_storage, 64);
        expect(
            !plan_route(
                2, 2, 2, 2, 8, 8, [](int, int) { return true; }, distance, route
            ),
            "a move to where you stand is not animated"
        );
        expect(
            !plan_route(
                -1, 0, 2, 0, 8, 8, [](int, int) { return true; }, distance,
                route
            ),
            "an origin off the board plans nothing"
        );
        expect(
            !plan_route(
                0, 0, 9, 0, 8, 8, [](int, int) { return true; }, distance, route
            ),
            "a destination off the board plans nothing"
        );
        expect(
            !plan_route(
                0, 0, 2, 0, 8, 8, [](int, int) { return true; }, nullptr, route
            ),
            "no scratch means no route"
        );
    }

    // A route longer than the caller made room for is refused rather than
    // truncated: half a route drawn is a token that teleports at the end.
    {
        RouteTile small_storage[2];
        Route small(small_storage, 2);
        const bool found = plan_route(
            0, 0, 5, 0, 8, 1, [](int, int) { return true; }, distance, small
        );
        expect(!found, "a route that does not fit is not drawn");
        expect(small.size() == 0, "and nothing is left half-planned");
    }

    // The route is the shortest one inside the set, so its length is the step
    // count the engine's own walk counted. Checked across a plain board, where
    // the shortest length is the Manhattan distance.
    {
        for (int dx = 0; dx <= 5; ++dx) {
            for (int dy = 0; dy <= 5; ++dy) {
                if (dx == 0 && dy == 0) continue;
                Route route(route_storage, 64);
                const bool found = plan_route(
                    0, 0, dx, dy, 8, 8, [](int, int) { return true; }, distance,
                    route
                );
                expect(found, "an open board always routes");
                expect(
                    route.size() == dx + dy,
                    "and the route is as long as the walk was"
                );
            }
        }
    }

    // The same question asked twice gets the same answer, on every client.
    {
        const Board board{
            5, 3,
            {
                "o....",
                "##.##",
                ".....",
            }
        };
        Route first(route_storage, 64);
        RouteTile second_storage[64];
        Route second(second_storage, 64);
        std::uint8_t other_distance[64 * 64];
        plan_route(
            0, 0, 4, 2, board.width, board.height,
            [&board](int x, int y) { return board.reachable(x, y); },
            distance, first
        );
        plan_route(
            0, 0, 4, 2, board.width, board.height,
            [&board](int x, int y) { return board.reachable(x, y); },
            other_distance, second
        );
        expect(first.size() == second.size(), "the route is deterministic");
        bool identical = first.size() == second.size();
        for (int i = 0; i < first.size() && identical; ++i) {
            if (first[i].x != second[i].x || first[i].y != second[i].y) {
                identical = false;
            }
        }
        expect(identical, "tile for tile, whatever scratch it was given");
    }

    // ---------------------------------------------------------------
    // The cursor's pulse
    // ---------------------------------------------------------------
    {
        expect(!cursor_emphasised(0), "phase zero is the cursor at rest");
        expect(
            !cursor_emphasised(cursor_pulse_rest_frames - 1),
            "and stays at rest for the first half"
        );
        expect(
            cursor_emphasised(cursor_pulse_rest_frames),
            "the emphasis comes up at the halfway frame"
        );
        expect(
            cursor_emphasised(cursor_pulse_period - 1),
            "and stays up to the end of the period"
        );
        expect(
            !cursor_emphasised(cursor_pulse_period),
            "the next period starts at rest again"
        );
        // Exact across the counter's whole range, because the period divides
        // 2^32. A pulse that stuttered once an hour would be a bug nobody
        // could reproduce.
        expect(
            !cursor_emphasised(0xFFFFFFE0U), "the phase is exact near the wrap"
        );
        expect(
            cursor_emphasised(0xFFFFFFFFU), "right up to the last frame"
        );

        // The ring is always short of the cell's centre, whatever the cell
        // size, because every framebuffer probe in this repository identifies
        // a cell by its centre pixel.
        for (int tile = 1; tile <= 128; ++tile) {
            const int inset = cursor_emphasis_inset(tile);
            expect(
                inset < (tile + 1) / 2,
                "the ring never reaches the centre a probe samples"
            );
            expect(inset >= 0, "and is never negative");
            if (tile >= 4) {
                expect(inset >= 1, "every cell a client draws gets a ring");
            }
        }
        expect(cursor_emphasis_inset(25) == 2, "the console's cell insets by 2");
        expect(cursor_emphasis_inset(32) == 2, "a 32px cell insets by 2");
        expect(cursor_emphasis_inset(0) == 0, "no cell, no ring");
    }

    // ---------------------------------------------------------------
    // The flinch
    // ---------------------------------------------------------------
    {
        expect(flinch_nudge_for(25) == 3, "the console's cell nudges by 3");
        expect(flinch_nudge_for(32) == 4, "a 32px cell nudges by 4");
        expect(flinch_nudge_for(100) == 12, "the editor's cell nudges by 12");
        expect(flinch_nudge_for(4) == 1, "a tiny cell still nudges");
        expect(flinch_nudge_for(0) == 0, "no cell, no nudge");

        // Short of an elevation step, so a flinch never reads as a climb.
        for (int tile = 8; tile <= 128; ++tile) {
            expect(
                flinch_nudge_for(tile) < elevation_step_for(tile),
                "a nudge is smaller than a step up"
            );
        }

        expect(flinch_offset(0, 1, 25) == 3, "the first frame is knocked away");
        expect(flinch_offset(2, 1, 25) == 3, "and so is the third");
        expect(
            flinch_offset(flinch_knocked_frames, 1, 25) == 0,
            "the fourth is back at rest"
        );
        expect(
            flinch_offset(flinch_frames - 1, 1, 25) == 0,
            "and so is the last frame of the flinch"
        );
        expect(flinch_offset(0, -4, 25) == -3, "a blow from the east knocks west");
        expect(flinch_offset(0, 0, 25) == 0, "a blow from nowhere knocks nowhere");
        expect(flinch_offset(-1, 1, 25) == 0, "before the flinch, nothing");
        expect(
            flinch_frames > flinch_knocked_frames,
            "a flinch always ends at rest"
        );
    }

    // ---------------------------------------------------------------
    // Which cell of a sequence is drawn
    // ---------------------------------------------------------------
    {
        expect(sequence_cell_count == 4, "a sequence sheet is four cells");
        expect(
            sequence_cell_stand < 0,
            "the standing sprite is not a cell of the sheet"
        );

        // The two ends. This is the property every checkpoint depends on: a
        // walk begins standing and ends standing, so a settled board is drawn
        // with exactly the sprite it was drawn with before frames existed.
        for (int tiles = 1; tiles <= 8; ++tiles) {
            const int frames = slide_frames_for(tiles);
            expect(
                walk_cell(0, frames) == sequence_cell_stand,
                "a walk begins standing"
            );
            expect(
                walk_cell(frames, frames) == sequence_cell_stand,
                "a walk ends standing, at every length"
            );
            expect(
                walk_cell(frames + 1, frames) == sequence_cell_stand,
                "and stays standing afterwards"
            );
            for (int frame = 1; frame < frames; ++frame) {
                const int cell = walk_cell(frame, frames);
                expect(
                    cell == sequence_cell_walk_contact ||
                        cell == sequence_cell_walk_pass,
                    "every frame between draws a walk cell and nothing else"
                );
            }
        }

        // One step a tile, alternating: the cell changes exactly at a tile
        // boundary and never inside one.
        expect(walk_cell(1, 24) == sequence_cell_walk_contact, "tile 1 contacts");
        expect(walk_cell(6, 24) == sequence_cell_walk_contact, "…for all six frames");
        expect(walk_cell(7, 24) == sequence_cell_walk_pass, "tile 2 passes");
        expect(walk_cell(12, 24) == sequence_cell_walk_pass, "…for all six frames");
        expect(walk_cell(13, 24) == sequence_cell_walk_contact, "tile 3 contacts again");
        expect(walk_cell(19, 24) == sequence_cell_walk_pass, "tile 4 passes");
        {
            int changes = 0;
            for (int frame = 2; frame < 24; ++frame) {
                if (walk_cell(frame, 24) != walk_cell(frame - 1, 24)) ++changes;
            }
            expect(changes == 3, "a four-tile walk changes cell three times");
        }

        // The strike is the flinch seen from the other end.
        expect(strike_cell(0) == sequence_cell_lunge, "a strike coils at once");
        expect(
            strike_cell(flinch_knocked_frames - 1) == sequence_cell_lunge,
            "and stays coiled as long as the blow is landing"
        );
        expect(
            strike_cell(flinch_knocked_frames) == sequence_cell_stand,
            "then stands, exactly as the struck token comes back"
        );
        expect(
            strike_cell(flinch_frames - 1) == sequence_cell_stand,
            "so the last frame of a hit is the board at rest"
        );
        expect(strike_cell(-1) == sequence_cell_stand, "before a strike, standing");

        // A cast is a pose held rather than thrown, and it settles for the same
        // reason a strike does.
        expect(cast_cell(0) == sequence_cell_cast, "a cast takes its pose at once");
        expect(
            cast_cell(cast_hold_frames - 1) == sequence_cell_cast,
            "and holds it for the whole hold"
        );
        expect(
            cast_cell(cast_hold_frames) == sequence_cell_stand,
            "then stands, so a cast ends at rest like every other gesture"
        );
        expect(cast_cell(-1) == sequence_cell_stand, "before a cast, standing");

        // The two poses a body can take are different cells. If they were ever
        // folded together the whole fourth cell would have been spent on
        // nothing, so it is pinned rather than assumed.
        expect(
            sequence_cell_cast != sequence_cell_lunge,
            "a cast and a lunge are different cells"
        );
        for (int frame = 0; frame < flinch_knocked_frames; ++frame) {
            expect(
                strike_cell(frame) != cast_cell(frame),
                "and no frame of a gesture draws both"
            );
        }
    }

    // ---------------------------------------------------------------
    // The cell ceiling
    // ---------------------------------------------------------------
    //
    // The arithmetic that closes the sheet, pinned here as well as asserted in
    // the header, so that the number a person reads beside
    // `sequence_cell_count` has a test behind it and not only a static_assert.
    {
        expect(sequence_cell_bytes == 512, "a 32x32 CI4 cell is 512 bytes");
        expect(tmem_texel_bytes == 2048, "a CI4 texture holds 2,048 texel bytes");
        expect(sequence_cell_ceiling == 4, "which is exactly four cells");
        expect(
            sequence_cell_count == sequence_cell_ceiling,
            "and all four are now spent: the next pose costs a narrower cell, "
            "a second upload or another format, never a raised constant"
        );
    }

    // ---------------------------------------------------------------
    // Which gesture an attack is drawn as
    // ---------------------------------------------------------------
    //
    // The derivation, against the reach bands `games/tarnholt` actually
    // authors. These are not invented numbers: a Guard Sword is 1..1, a Long
    // Bow is 2..3, an Ember Staff is 1..2, and the Dawn Mage's two spells are
    // Ember Bolt 1..2 and Cinder Arc 2..3. If the content moves, this test is
    // where the presentation notices.
    {
        expect(reach_covers(1, 1, 1), "a sword reaches an adjacent enemy");
        expect(!reach_covers(2, 1, 1), "and nothing further");
        expect(!reach_covers(1, 2, 3), "a true bow cannot answer at arm's length");
        expect(reach_covers(2, 2, 3), "but reaches two");
        expect(reach_covers(3, 2, 3), "and three");
        expect(!reach_covers(4, 2, 3), "and no further");

        // A blow no magic could have thrown is a weapon blow, and a weapon blow
        // is a shot only when the weapon that threw it cannot be used up close.
        expect(
            attack_gesture(1, false, false) == AttackGesture::swing,
            "a sword at arm's length is a swing"
        );
        expect(
            attack_gesture(2, false, true) == AttackGesture::shot,
            "an arrow from a bow that cannot answer up close is a shot"
        );
        expect(
            attack_gesture(3, false, true) == AttackGesture::shot,
            "and so is one that crossed three"
        );
        expect(
            attack_gesture(0, false, false) == AttackGesture::swing,
            "a blow from nobody on the board is still something a client draws"
        );

        // The distinction a scripted session on a real board forced. A Vow
        // Glaive and a Boat Hook both reach one tile and two; reaching further
        // is not being loosed, and a thrust that reaches further is a thrust.
        expect(
            attack_gesture(2, false, false) == AttackGesture::swing,
            "a polearm answering from two tiles is still a thrust"
        );
        expect(
            !reach_covers(1, 2, 3),
            "and the number that tells them apart is the bow's own floor"
        );
        expect(
            reach_covers(1, 1, 2),
            "which a polearm does not have"
        );

        // A blow magic could have thrown is a cast, at every distance. That is
        // the point. Both of the Dawn Mage's spells therefore read as casts
        // over their whole bands rather than only where the staff cannot reach.
        for (int separation = 1; separation <= 3; ++separation) {
            const bool ember_bolt = reach_covers(separation, 1, 2);
            const bool cinder_arc = reach_covers(separation, 2, 3);
            expect(
                attack_gesture(separation, ember_bolt || cinder_arc, false) ==
                    AttackGesture::cast,
                "the Dawn Mage casts at every range either spell reaches"
            );
        }
        // And the knight beside it does not, because Power Strike is physical
        // and contributes nothing to the fold.
        expect(
            attack_gesture(1, false, false) == AttackGesture::swing,
            "a knight who knows a physical ability still swings"
        );
        // Magic wins over a launcher, so an archer who somehow knew a spell
        // would cast it rather than shoot it. Stated rather than left to the
        // order of two ifs.
        expect(
            attack_gesture(3, true, true) == AttackGesture::cast,
            "a blow magic could have thrown is a cast whatever is in hand"
        );

        // The whole gesture's length, lead and resolution together.
        expect(
            gesture_lead_frames(AttackGesture::swing, 1) == 0,
            "a swing opens with nothing: the blow and the flinch are one"
        );
        expect(
            gesture_lead_frames(AttackGesture::shot, 3) == 9,
            "a three-tile shot spends nine frames in the air"
        );
        expect(
            gesture_lead_frames(AttackGesture::cast, 1) == cast_hold_frames,
            "a cast holds its pose whatever the distance"
        );
        expect(
            gesture_frames(AttackGesture::swing, 1, true) == flinch_frames,
            "a landed swing is exactly the gesture it always was"
        );
        expect(
            gesture_frames(AttackGesture::swing, 1, false) == miss_frames,
            "and a missed one is exactly as long as a miss always was"
        );
        expect(
            gesture_frames(AttackGesture::shot, 3, true) == 15,
            "a landed three-tile shot is nine in the air and six landing"
        );
        expect(
            gesture_frames(AttackGesture::cast, 2, true) == 12,
            "a landed cast is six held and six landing"
        );
        expect(
            gesture_frames(AttackGesture::cast, 2, false) == 9,
            "and a missed cast is held just as long, then takes nothing"
        );
    }

    // ---------------------------------------------------------------
    // The travelling mark
    // ---------------------------------------------------------------
    {
        expect(projectile_frames_per_tile == 3, "a bolt crosses a tile in three");
        expect(
            projectile_frames_per_tile * 2 == slide_frames_per_tile,
            "which is half a walk, so a loosed thing outruns a walking one"
        );
        expect(projectile_frames_for(0) == 0, "no distance, no flight");
        expect(projectile_frames_for(-1) == 0, "and no negative one either");
        expect(projectile_frames_for(3) == 9, "the longest shipped band is nine");

        // The settle rule, applied to a mark. This is the property that lets a
        // checkpoint photograph a gesture's last frame and see the board it saw
        // before projectiles existed.
        for (int frames = 1; frames <= 12; ++frames) {
            expect(rise_and_fall(0, frames, 8) == 0, "a mark begins at nothing");
            expect(
                rise_and_fall(frames, frames, 8) == 0,
                "and ends at nothing, at every length"
            );
            expect(
                rise_and_fall(frames + 1, frames, 8) == 0,
                "and stays at nothing afterwards"
            );
            for (int frame = 0; frame <= frames; ++frame) {
                const int rise = rise_and_fall(frame, frames, 8);
                expect(rise >= 0 && rise <= 8, "and never exceeds its peak");
            }
        }
        expect(rise_and_fall(3, 6, 8) == 8, "and reaches it in the middle");
        expect(rise_and_fall(1, 6, 8) < rise_and_fall(2, 6, 8), "rising on the way up");
        expect(rise_and_fall(4, 6, 8) > rise_and_fall(5, 6, 8), "falling on the way down");
        expect(rise_and_fall(2, 6, 0) == 0, "no peak, no mark");
        expect(rise_and_fall(2, 0, 8) == 0, "no frames, no mark");

        // The peaks, against the cell sizes the clients actually draw.
        expect(projectile_arc_peak(25) == 6, "a bolt arcs a quarter of a console cell");
        expect(
            projectile_arc_peak(25) == elevation_step_for(25),
            "which is one drawn tier of height, so an arc is never a climb"
        );
        expect(projectile_arc_peak(0) == 0, "no cell, no arc");
        expect(projectile_arc_peak(2) == 1, "a tiny cell still arcs by a pixel");
        expect(effect_bloom_peak(25) == 12, "a flare fills half a console cell");
        expect(
            effect_bloom_peak(25) < 25,
            "and never reaches the cells beside it"
        );
        expect(effect_bloom_peak(0) == 0, "no cell, no flare");
    }

    // ---------------------------------------------------------------
    // The water shimmer's phase
    // ---------------------------------------------------------------
    {
        expect(water_cycle_entries == 4, "four entries are rotated");
        expect(water_cycle_period == 32, "a full cycle is the pulse's own period");

        // Phase zero is the palette the console loaded. This is the whole
        // reason no pixel expectation moved.
        expect(water_cycle_phase(0) == 0, "phase zero at frame zero");
        for (int slot = 0; slot < water_cycle_entries; ++slot) {
            expect(
                water_cycle_source(slot, 0) == slot,
                "phase zero is the identity permutation"
            );
        }

        // It is a permutation at every phase: nothing written twice, nothing
        // dropped. A rotation that repeated a colour would quietly lose one.
        for (std::uint32_t frame = 0; frame < 256U; ++frame) {
            bool seen[water_cycle_entries] = {};
            for (int slot = 0; slot < water_cycle_entries; ++slot) {
                const int source = water_cycle_source(slot, frame);
                expect(
                    source >= 0 && source < water_cycle_entries,
                    "a rotation only ever names an entry of its own window"
                );
                expect(!seen[source], "and names each of them exactly once");
                seen[source] = true;
            }
        }

        // Held for a whole step, then moved. A client repaints on the change
        // and not on the frame.
        for (int step = 0; step < water_cycle_entries; ++step) {
            for (int within = 0; within < water_cycle_step_frames; ++within) {
                const auto frame = static_cast<std::uint32_t>(
                    step * water_cycle_step_frames + within);
                expect(water_cycle_phase(frame) == step, "the phase holds a step");
                expect(
                    water_cycle_changes(frame) ==
                        (within == water_cycle_step_frames - 1),
                    "and changes on the last frame of it and no other"
                );
            }
        }

        // Exact across every wrap of the counter, because the period divides
        // 2^32: the same argument the cursor's pulse makes.
        expect(
            water_cycle_phase(0xFFFFFFFFU) == water_cycle_entries - 1,
            "the last frame before the wrap is the last phase"
        );
        expect(water_cycle_phase(0U) == 0, "and the wrap lands back on phase zero");
        for (std::uint32_t frame = 0; frame < 64U; ++frame) {
            expect(
                water_cycle_phase(frame) ==
                    water_cycle_phase(frame + static_cast<std::uint32_t>(
                        water_cycle_period)),
                "the phase is periodic with no drift"
            );
        }
    }

    // ----- the opening sweep ------------------------------------------
    //
    // A board wider than the screen is shown across once before it is played
    // on. What has to hold is that a board with nothing to reveal asks for no
    // frames at all, that the duration carries the board's width until the cap
    // takes over, and that the travel itself lands exactly on where play begins.
    {
        expect(sweep_frames_for(0) == 0, "a board with nowhere to travel is not swept");
        expect(sweep_frames_for(-4) == 0, "and neither is a negative width");
        expect(
            sweep_frames_for(1) == sweep_frames_per_cell,
            "one cell of travel costs one cell's frames"
        );
        expect(
            sweep_frames_for(10) == 10 * sweep_frames_per_cell,
            "and ten cost ten of them, so a wider board takes visibly longer"
        );
        expect(
            sweep_frames_for(20) == 2 * sweep_frames_for(10),
            "twice the board is twice the sweep, which is what the gesture says"
        );

        // The cap, from both sides of it.
        const int at_cap = sweep_frames_most / sweep_frames_per_cell;
        expect(
            sweep_frames_for(at_cap) == sweep_frames_most,
            "the widest board that fits under the cap reaches it exactly"
        );
        expect(
            sweep_frames_for(at_cap * 4) == sweep_frames_most,
            "and no board, however wide, is swept for longer"
        );
        expect(
            sweep_frames_most == 90 && sweep_frames_per_cell == 3,
            "the numbers themselves, so a change to either is a change here"
        );

        // ----- the common board: play begins at the left edge -------------
        //
        // One leg, right edge to left, which is the reveal as asked for. The
        // second leg is empty because there is nowhere to come back to.
        {
            const int frames = sweep_frames_total(19, 0);
            expect(frames == 57, "nineteen columns of board is under a second");
            expect(frames == sweep_frames_for(19), "and is one leg, not two");
            expect(sweep_at(19, 0, 0) == 19, "it opens at the right edge");
            expect(
                sweep_at(19, 0, frames) == 0,
                "and the frame past its last is the left one, which is where play "
                "begins and so is the frame the board is painted at"
            );
            expect(
                sweep_at(19, 0, frames + 40) == 0,
                "and it stays there, so an overrun cannot drift"
            );
            int previous = 20;
            for (int frame = 0; frame < frames; ++frame) {
                const int at = sweep_at(19, 0, frame);
                expect(at <= previous, "travelling one way and never back");
                expect(at >= 0 && at <= 19, "and never leaving the board");
                previous = at;
            }
        }

        // ----- a board whose player opens further in ----------------------
        //
        // The turn is at the left edge rather than at the opening column,
        // because the width is the thing being shown. Without the second leg a
        // board like this would be revealed by barely moving.
        {
            const int out = sweep_frames_for(19);
            const int back = sweep_frames_for(6);
            expect(
                sweep_frames_total(19, 6) == out + back,
                "two legs: out to the edge and back to where play begins"
            );
            expect(sweep_at(19, 6, 0) == 19, "still opening at the right edge");
            expect(sweep_at(19, 6, out) == 0, "reaching the left edge on the turn");
            expect(
                sweep_at(19, 6, out + back) == 6,
                "and arriving at the opening column, which the paint draws"
            );
            int lowest = 19;
            for (int frame = 0; frame < out; ++frame) {
                lowest = sweep_at(19, 6, frame) < lowest ? sweep_at(19, 6, frame)
                                                         : lowest;
            }
            expect(
                lowest < 6,
                "the first leg goes past the opening column, which is the whole "
                "reason it exists"
            );
            for (int frame = out; frame <= out + back; ++frame) {
                const int at = sweep_at(19, 6, frame);
                expect(at >= 0 && at <= 6, "and the return leg stays between them");
            }
        }

        // A board that cannot scroll sideways is never swept, whatever column
        // its play begins at. This is the guard every fitting board takes.
        expect(sweep_frames_total(0, 0) == 0, "a board that fits asks for nothing");
        expect(
            sweep_frames_total(0, 4) == 0,
            "and so does one whose camera cannot move sideways at all"
        );
    }

    if (failures == 0) {
        std::cout << "board motion model: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
