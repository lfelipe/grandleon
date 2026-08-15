// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";

import {
  CURSOR_PULSE_PERIOD,
  CURSOR_PULSE_REST_FRAMES,
  FLINCH_FRAMES,
  FLINCH_KNOCKED_FRAMES,
  CAST_HOLD_FRAMES,
  MISS_FRAMES,
  PROJECTILE_FRAMES_PER_TILE,
  SEQUENCE_CELL_CAST,
  SEQUENCE_CELL_CEILING,
  SEQUENCE_CELL_COUNT,
  SEQUENCE_CELL_LUNGE,
  SEQUENCE_CELL_STAND,
  SEQUENCE_CELL_WALK_CONTACT,
  SEQUENCE_CELL_WALK_PASS,
  SLIDE_FRAMES_PER_TILE,
  WATER_CYCLE_ENTRIES,
  WATER_CYCLE_PERIOD,
  WATER_CYCLE_STEP_FRAMES,
  attackGesture,
  castCell,
  cursorEmphasised,
  effectBloomPeak,
  flinchNudgeFor,
  flinchOffset,
  gestureFrames,
  gestureLeadFrames,
  planRoute,
  projectileArcPeak,
  projectileFramesFor,
  reachCovers,
  riseAndFall,
  slideBetween,
  slideFramesFor,
  slidePosition,
  strikeCell,
  walkCell,
  waterCyclePhase,
  waterCycleSource,
} from "./board-motion";

/**
 * The browser's half of the motion model, pinned against the same numbers the
 * C++ header pins in `tests/view/motion_test.cpp`. Two clients drawing the same
 * move at two speeds would be a difference nobody could see in one screenshot
 * and everybody would feel playing both, so the numbers are asserted rather
 * than inherited.
 */
describe("board motion", () => {
  it("keeps the frame counts the console header states", () => {
    expect(SLIDE_FRAMES_PER_TILE).toBe(6);
    expect(FLINCH_FRAMES).toBe(6);
    expect(FLINCH_KNOCKED_FRAMES).toBe(3);
    expect(CURSOR_PULSE_PERIOD).toBe(32);
    expect(CURSOR_PULSE_REST_FRAMES).toBe(16);
    expect(slideFramesFor(4)).toBe(24);
    expect(slideFramesFor(0)).toBe(0);
  });

  it("lands the last frame of a slide exactly on its destination", () => {
    for (let frames = 1; frames <= 12; frames += 1) {
      expect(slideBetween(0, 97, frames, frames)).toBe(97);
      expect(slideBetween(0, 97, 0, frames)).toBe(0);
      expect(slideBetween(0, 97, frames + 4, frames)).toBe(97);
    }
    expect(slideBetween(0, 100, 3, 6)).toBe(50);
  });

  it("walks around water rather than through it", () => {
    // The reachable set the engine would return for a unit at 0,0 on a board
    // whose middle row is a river it cannot cross.
    const reachable = new Set([
      "1:0",
      "2:0",
      "3:0",
      "4:0",
      "2:1",
      "0:2",
      "1:2",
      "2:2",
      "3:2",
      "4:2",
    ]);
    const route = planRoute({ x: 0, y: 0 }, { x: 0, y: 2 }, 5, 3, reachable);
    expect(route).toHaveLength(6);
    expect(route[route.length - 1]).toEqual({ x: 0, y: 2 });
    for (const tile of route) {
      expect(reachable.has(`${tile.x}:${tile.y}`)).toBe(true);
    }
    // One orthogonal step at a time, from the origin onward.
    let previous = { x: 0, y: 0 };
    for (const tile of route) {
      const dx = tile.x - previous.x;
      const dy = tile.y - previous.y;
      expect(dx * dx + dy * dy).toBe(1);
      previous = tile;
    }
  });

  it("steps over a tile it may cross but not stop on", () => {
    // Three tiles in a row: the origin, an ally, and the tile beyond. The
    // engine's reachability answer lists where a walk may *end*, so the ally's
    // tile is a hole in it, and over that alone there is no route at all.
    expect(
      planRoute({ x: 0, y: 0 }, { x: 2, y: 0 }, 3, 1, new Set(["2:0"])),
    ).toEqual([]);
    // The crossable set is that answer plus the tiles the mover's own side
    // holds, and the route walks straight over the one in the middle.
    expect(
      planRoute({ x: 0, y: 0 }, { x: 2, y: 0 }, 3, 1, new Set(["1:0", "2:0"])),
    ).toEqual([
      { x: 1, y: 0 },
      { x: 2, y: 0 },
    ]);
  });

  it("plans nothing rather than guessing", () => {
    const open = new Set(["1:0", "2:0"]);
    expect(planRoute({ x: 0, y: 0 }, { x: 0, y: 0 }, 3, 1, open)).toEqual([]);
    expect(planRoute({ x: 0, y: 0 }, { x: 9, y: 0 }, 3, 1, open)).toEqual([]);
    // A destination the query never returned.
    expect(planRoute({ x: 0, y: 0 }, { x: 2, y: 1 }, 3, 2, open)).toEqual([]);
    // A destination in the set but walled off from the origin inside it.
    expect(
      planRoute({ x: 0, y: 0 }, { x: 2, y: 1 }, 3, 2, new Set(["2:1"])),
    ).toEqual([]);
  });

  it("is the same route the console would walk", () => {
    // An open board: the shortest route is as long as the Manhattan distance,
    // which is the step count the engine's own walk counted.
    const open = new Set<string>();
    for (let y = 0; y < 6; y += 1) {
      for (let x = 0; x < 6; x += 1) {
        if (x !== 0 || y !== 0) open.add(`${x}:${y}`);
      }
    }
    for (let dx = 0; dx <= 4; dx += 1) {
      for (let dy = 0; dy <= 4; dy += 1) {
        if (dx === 0 && dy === 0) continue;
        expect(planRoute({ x: 0, y: 0 }, { x: dx, y: dy }, 6, 6, open)).toHaveLength(
          dx + dy,
        );
      }
    }
  });

  it("walks the route one tile at a time and ends at rest", () => {
    const route = [
      { x: 1, y: 0 },
      { x: 2, y: 0 },
    ];
    const origin = { x: 0, y: 0 };
    expect(slidePosition(origin, route, 0)).toEqual({ x: 0, y: 0 });
    expect(slidePosition(origin, route, SLIDE_FRAMES_PER_TILE)).toEqual({
      x: 1,
      y: 0,
    });
    expect(slidePosition(origin, route, 2 * SLIDE_FRAMES_PER_TILE)).toEqual({
      x: 2,
      y: 0,
    });
    expect(slidePosition(origin, route, 999)).toEqual({ x: 2, y: 0 });
    // Monotone, so nothing appears to step backwards mid-slide.
    let previous = -1;
    for (let frame = 0; frame <= 2 * SLIDE_FRAMES_PER_TILE; frame += 1) {
      const here = slidePosition(origin, route, frame).x;
      expect(here).toBeGreaterThanOrEqual(previous);
      previous = here;
    }
    expect(slidePosition(origin, [], 3)).toEqual({ x: 0, y: 0 });
  });

  it("rests the cursor at phase zero", () => {
    expect(cursorEmphasised(0)).toBe(false);
    expect(cursorEmphasised(CURSOR_PULSE_REST_FRAMES - 1)).toBe(false);
    expect(cursorEmphasised(CURSOR_PULSE_REST_FRAMES)).toBe(true);
    expect(cursorEmphasised(CURSOR_PULSE_PERIOD - 1)).toBe(true);
    expect(cursorEmphasised(CURSOR_PULSE_PERIOD)).toBe(false);
  });

  it("knocks a struck token away and stands it up again", () => {
    expect(flinchNudgeFor(100)).toBe(12);
    expect(flinchNudgeFor(0)).toBe(0);
    expect(flinchOffset(0, 1, 100)).toBe(12);
    expect(flinchOffset(FLINCH_KNOCKED_FRAMES - 1, 1, 100)).toBe(12);
    expect(flinchOffset(FLINCH_KNOCKED_FRAMES, 1, 100)).toBe(0);
    expect(flinchOffset(FLINCH_FRAMES - 1, 1, 100)).toBe(0);
    expect(flinchOffset(0, -3, 100)).toBe(-12);
    expect(flinchOffset(0, 0, 100)).toBe(0);
  });

  it("begins and ends a walk standing, at every length", () => {
    expect(SEQUENCE_CELL_COUNT).toBe(4);
    for (let tiles = 1; tiles <= 8; tiles += 1) {
      const frames = slideFramesFor(tiles);
      expect(walkCell(0, frames)).toBe(SEQUENCE_CELL_STAND);
      expect(walkCell(frames, frames)).toBe(SEQUENCE_CELL_STAND);
      expect(walkCell(frames + 1, frames)).toBe(SEQUENCE_CELL_STAND);
      for (let frame = 1; frame < frames; frame += 1) {
        expect([SEQUENCE_CELL_WALK_CONTACT, SEQUENCE_CELL_WALK_PASS]).toContain(
          walkCell(frame, frames),
        );
      }
    }
  });

  it("takes one step a tile, alternating", () => {
    const frames = slideFramesFor(4);
    expect(walkCell(1, frames)).toBe(SEQUENCE_CELL_WALK_CONTACT);
    expect(walkCell(SLIDE_FRAMES_PER_TILE, frames)).toBe(
      SEQUENCE_CELL_WALK_CONTACT,
    );
    expect(walkCell(SLIDE_FRAMES_PER_TILE + 1, frames)).toBe(
      SEQUENCE_CELL_WALK_PASS,
    );
    let changes = 0;
    for (let frame = 2; frame < frames; frame += 1) {
      if (walkCell(frame, frames) !== walkCell(frame - 1, frames)) changes += 1;
    }
    expect(changes).toBe(3);
  });

  it("coils a strike for exactly as long as the blow lands", () => {
    expect(strikeCell(-1)).toBe(SEQUENCE_CELL_STAND);
    expect(strikeCell(0)).toBe(SEQUENCE_CELL_LUNGE);
    expect(strikeCell(FLINCH_KNOCKED_FRAMES - 1)).toBe(SEQUENCE_CELL_LUNGE);
    expect(strikeCell(FLINCH_KNOCKED_FRAMES)).toBe(SEQUENCE_CELL_STAND);
    expect(strikeCell(FLINCH_FRAMES - 1)).toBe(SEQUENCE_CELL_STAND);
  });

  it("holds a cast, and ends it standing", () => {
    expect(castCell(-1)).toBe(SEQUENCE_CELL_STAND);
    expect(castCell(0)).toBe(SEQUENCE_CELL_CAST);
    expect(castCell(CAST_HOLD_FRAMES - 1)).toBe(SEQUENCE_CELL_CAST);
    expect(castCell(CAST_HOLD_FRAMES)).toBe(SEQUENCE_CELL_STAND);
    // The two poses a body can take are different cells, and no frame of a
    // gesture draws both.
    expect(SEQUENCE_CELL_CAST).not.toBe(SEQUENCE_CELL_LUNGE);
    for (let frame = 0; frame < FLINCH_KNOCKED_FRAMES; frame += 1) {
      expect(strikeCell(frame)).not.toBe(castCell(frame));
    }
  });

  it("stops the sequence at the ceiling the console's texture memory sets", () => {
    // 512 bytes a 32x32 CI4 cell against 2,048 texel bytes of TMEM is four,
    // and all four are now spent. The C++ header refuses a fifth outright;
    // this is the browser refusing to index past one.
    expect(SEQUENCE_CELL_CEILING).toBe(4);
    expect(SEQUENCE_CELL_COUNT).toBe(SEQUENCE_CELL_CEILING);
  });

  it("draws a blow only magic could have thrown as a cast", () => {
    // The reach bands are the ones `games/tarnholt` authors: a Guard Sword is
    // 1..1, a Long Bow 2..3, an Ember Staff 1..2, and the Dawn Mage's two
    // spells are Ember Bolt 1..2 and Cinder Arc 2..3.
    expect(reachCovers(1, 1, 1)).toBe(true);
    expect(reachCovers(2, 1, 1)).toBe(false);
    expect(reachCovers(1, 2, 3)).toBe(false);
    expect(reachCovers(2, 2, 3)).toBe(true);
    expect(reachCovers(3, 2, 3)).toBe(true);
    expect(reachCovers(4, 2, 3)).toBe(false);

    expect(attackGesture(1, false, false)).toBe("swing");
    expect(attackGesture(2, false, true)).toBe("shot");
    expect(attackGesture(3, false, true)).toBe("shot");
    expect(attackGesture(0, false, false)).toBe("swing");
    // Reaching further is not being loosed: a Vow Glaive answers from two
    // tiles and is still a thrust.
    expect(attackGesture(2, false, false)).toBe("swing");
    expect(attackGesture(3, true, true)).toBe("cast");

    // Both spells read as casts over their whole bands, which is the point.
    for (let separation = 1; separation <= 3; separation += 1) {
      const emberBolt = reachCovers(separation, 1, 2);
      const cinderArc = reachCovers(separation, 2, 3);
      expect(attackGesture(separation, emberBolt || cinderArc, false)).toBe(
        "cast",
      );
    }
  });

  it("counts a whole gesture the way the console header counts it", () => {
    expect(MISS_FRAMES).toBe(3);
    expect(MISS_FRAMES).toBeLessThan(FLINCH_FRAMES);
    expect(CAST_HOLD_FRAMES).toBe(6);
    expect(PROJECTILE_FRAMES_PER_TILE * 2).toBe(SLIDE_FRAMES_PER_TILE);
    expect(projectileFramesFor(0)).toBe(0);
    expect(projectileFramesFor(3)).toBe(9);
    expect(gestureLeadFrames("swing", 1)).toBe(0);
    expect(gestureLeadFrames("shot", 3)).toBe(9);
    expect(gestureLeadFrames("cast", 1)).toBe(CAST_HOLD_FRAMES);
    expect(gestureFrames("swing", 1, true)).toBe(FLINCH_FRAMES);
    expect(gestureFrames("swing", 1, false)).toBe(MISS_FRAMES);
    expect(gestureFrames("shot", 3, true)).toBe(15);
    expect(gestureFrames("cast", 2, true)).toBe(12);
    expect(gestureFrames("cast", 2, false)).toBe(9);
  });

  it("begins and ends a travelling mark at nothing", () => {
    for (let frames = 1; frames <= 12; frames += 1) {
      expect(riseAndFall(0, frames, 8)).toBe(0);
      expect(riseAndFall(frames, frames, 8)).toBe(0);
      expect(riseAndFall(frames + 1, frames, 8)).toBe(0);
      for (let frame = 0; frame <= frames; frame += 1) {
        const rise = riseAndFall(frame, frames, 8);
        expect(rise).toBeGreaterThanOrEqual(0);
        expect(rise).toBeLessThanOrEqual(8);
      }
    }
    expect(riseAndFall(3, 6, 8)).toBe(8);
    expect(riseAndFall(1, 6, 8)).toBeLessThan(riseAndFall(2, 6, 8));
    expect(riseAndFall(4, 6, 8)).toBeGreaterThan(riseAndFall(5, 6, 8));
    expect(riseAndFall(2, 6, 0)).toBe(0);
    expect(riseAndFall(2, 0, 8)).toBe(0);

    expect(projectileArcPeak(25)).toBe(6);
    expect(projectileArcPeak(0)).toBe(0);
    expect(projectileArcPeak(2)).toBe(1);
    expect(effectBloomPeak(25)).toBe(12);
    expect(effectBloomPeak(25)).toBeLessThan(25);
    expect(effectBloomPeak(0)).toBe(0);
  });

  it("rotates the water as a permutation, and not at phase zero", () => {
    expect(WATER_CYCLE_PERIOD).toBe(32);
    expect(waterCyclePhase(0)).toBe(0);
    for (let slot = 0; slot < WATER_CYCLE_ENTRIES; slot += 1) {
      expect(waterCycleSource(slot, 0)).toBe(slot);
    }
    for (let frame = 0; frame < 256; frame += 1) {
      const seen = new Set<number>();
      for (let slot = 0; slot < WATER_CYCLE_ENTRIES; slot += 1) {
        const source = waterCycleSource(slot, frame);
        expect(source).toBeGreaterThanOrEqual(0);
        expect(source).toBeLessThan(WATER_CYCLE_ENTRIES);
        expect(seen.has(source)).toBe(false);
        seen.add(source);
      }
    }
  });

  it("holds each step of the shimmer for a whole step", () => {
    for (let step = 0; step < WATER_CYCLE_ENTRIES; step += 1) {
      for (let within = 0; within < WATER_CYCLE_STEP_FRAMES; within += 1) {
        expect(waterCyclePhase(step * WATER_CYCLE_STEP_FRAMES + within)).toBe(
          step,
        );
      }
    }
    expect(waterCyclePhase(WATER_CYCLE_PERIOD)).toBe(0);
  });
});
