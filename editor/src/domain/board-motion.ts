// SPDX-License-Identifier: MIT
/**
 * The board's presentation model in time, in the editor's own words.
 *
 * This is the TypeScript half of
 * `platform/view/include/grandleon/view/motion.hpp`, exactly as `board-view.ts`
 * is the TypeScript half of `board_view.hpp`: the same frame counts, the same
 * route walk, the same pulse phase and the same flinch, so a move looks like
 * the same move on a console and in a browser tab. The
 * C++ header is the specification; when the two must change, they change
 * together. The numbers are written down in those two places and nowhere else:
 * in the header, which is pure integer arithmetic with no SDK, no engine header
 * and no clock, pinned by `tests/view/motion_test.cpp`, and in this file,
 * pinned by `board-motion.test.ts`. Each half pins the other.
 *
 * Everything here is frame-counted. Nothing reads a clock and nothing
 * interpolates against a refresh rate. A browser animation frame is one
 * frame, the same unit the consoles count in, because the console checks
 * assert framebuffer pixels at named checkpoints and a picture that depended
 * on how fast the machine happened to be would be a picture nobody could pin.
 *
 * And phase zero is rest: every periodic function here returns, at phase zero,
 * exactly what was drawn before any of it existed.
 */

/**
 * How long a token spends crossing one tile. Six frames is a tenth of a second
 * per tile at 60 Hz, which reads as a walk rather than a slide or a jump.
 */
export const SLIDE_FRAMES_PER_TILE = 6;

/** A landed hit: six frames, the first three of them knocked back. */
export const FLINCH_FRAMES = 6;
export const FLINCH_KNOCKED_FRAMES = 3;

/**
 * A blow that did not land: three frames, the striker throwing it for all
 * three, and nothing knocked anywhere. It is here rather than in each client
 * because a miss every client counts for itself is a miss every client counts
 * differently.
 */
export const MISS_FRAMES = FLINCH_KNOCKED_FRAMES;

/** A bolt crosses a tile in half the frames a body walks one. */
export const PROJECTILE_FRAMES_PER_TILE = 3;

/** How long a caster holds the pose: the same six a landed blow takes. */
export const CAST_HOLD_FRAMES = FLINCH_FRAMES;

/** The cursor's pulse: thirty-two frames, at rest for the first sixteen. */
export const CURSOR_PULSE_PERIOD = 32;
export const CURSOR_PULSE_REST_FRAMES = 16;

/** How many frames a route of `steps` tiles takes. */
export function slideFramesFor(steps: number): number {
  return steps > 0 ? steps * SLIDE_FRAMES_PER_TILE : 0;
}

/**
 * Where a token is drawn part-way between two pixel positions: `frame` of
 * `frames`, counted from 1 so the last frame lands exactly on `to`. Truncating
 * division, so this is the same arithmetic the consoles do.
 */
export function slideBetween(
  from: number,
  to: number,
  frame: number,
  frames: number,
): number {
  if (frames <= 0) return to;
  if (frame >= frames) return to;
  if (frame <= 0) return from;
  return from + Math.trunc(((to - from) * frame) / frames);
}

/** One tile of a drawn route. */
export interface RouteTile {
  readonly x: number;
  readonly y: number;
}

/** The four steps, in the engine's own order: north, east, south, west. */
const NEIGHBOURS: readonly (readonly [number, number])[] = [
  [0, -1],
  [1, 0],
  [0, 1],
  [-1, 0],
];

const UNREACHED = 0xff;

/**
 * The route a slide is drawn along.
 *
 * A move command reports where a unit landed, not how it got there: the route
 * is not simulated, because nothing about the rules depends on it. It is drawn,
 * exactly like elevation. It is still not the client's to invent, though: it
 * is a breadth-first walk over the tiles the simulation says this walk may be
 * on, so every tile the token is drawn standing on is one of those, and the
 * route is as short as that set allows.
 *
 * `crossable` is that set, as `"x:y"` keys, the shape the board already holds
 * it in. It is the reachability query's answer *plus* the tiles the mover's own
 * side is standing on: a walk passes through an ally and may not stop on one,
 * so the query leaves an ally-shaped hole exactly where a route may need to go.
 * The origin is in neither, because a unit does not move to where it stands,
 * and is handled here rather than asked about.
 *
 * An empty result means "draw the straight line instead": the destination was
 * not crossable, or nothing crossable reaches it. That is the honest fallback,
 * because a guessed route could cross ground this unit cannot.
 */
export function planRoute(
  origin: RouteTile,
  destination: RouteTile,
  width: number,
  height: number,
  crossable: ReadonlySet<string>,
): RouteTile[] {
  if (width <= 0 || height <= 0) return [];
  const inBounds = (x: number, y: number) =>
    x >= 0 && y >= 0 && x < width && y < height;
  if (!inBounds(origin.x, origin.y)) return [];
  if (!inBounds(destination.x, destination.y)) return [];
  if (origin.x === destination.x && origin.y === destination.y) return [];
  if (!crossable.has(`${destination.x}:${destination.y}`)) return [];

  const cells = width * height;
  const distance = new Uint8Array(cells).fill(UNREACHED);
  const slot = (x: number, y: number) => y * width + x;
  distance[slot(origin.x, origin.y)] = 0;
  const destinationSlot = slot(destination.x, destination.y);

  let arrived = false;
  for (let step = 0; step < cells && !arrived; step += 1) {
    let grew = false;
    for (let y = 0; y < height && !arrived; y += 1) {
      for (let x = 0; x < width && !arrived; x += 1) {
        if (distance[slot(x, y)] !== step) continue;
        for (const [dx, dy] of NEIGHBOURS) {
          const nx = x + dx;
          const ny = y + dy;
          if (!inBounds(nx, ny)) continue;
          const here = slot(nx, ny);
          if (distance[here] !== UNREACHED) continue;
          if (!crossable.has(`${nx}:${ny}`)) continue;
          distance[here] = step + 1;
          grew = true;
          if (here === destinationSlot) arrived = true;
        }
      }
    }
    if (!grew) break;
  }
  if (!arrived) return [];

  const route: RouteTile[] = [];
  let x = destination.x;
  let y = destination.y;
  while (distance[slot(x, y)] !== 0) {
    route.unshift({ x, y });
    const want = (distance[slot(x, y)] ?? 0) - 1;
    let stepped = false;
    for (const [dx, dy] of NEIGHBOURS) {
      const nx = x + dx;
      const ny = y + dy;
      if (!inBounds(nx, ny)) continue;
      if (distance[slot(nx, ny)] !== want) continue;
      x = nx;
      y = ny;
      stepped = true;
      break;
    }
    if (!stepped) return [];
  }
  return route;
}

/**
 * Where a token is drawn, in cell units, on a given frame of a slide along
 * `route` from `origin`. Fractional between tiles; the caller multiplies by its
 * own cell size. A frame past the end of the route is the destination.
 */
export function slidePosition(
  origin: RouteTile,
  route: readonly RouteTile[],
  frame: number,
): { x: number; y: number } {
  if (route.length === 0) return { x: origin.x, y: origin.y };
  const total = slideFramesFor(route.length);
  const last = route[route.length - 1] ?? origin;
  if (frame >= total) return { x: last.x, y: last.y };
  const leg = Math.floor(frame / SLIDE_FRAMES_PER_TILE);
  const within = frame - leg * SLIDE_FRAMES_PER_TILE;
  const from = (leg === 0 ? origin : route[leg - 1]) ?? origin;
  const to = route[leg] ?? last;
  const scale = 1000;
  return {
    x:
      slideBetween(
        from.x * scale,
        to.x * scale,
        within,
        SLIDE_FRAMES_PER_TILE,
      ) / scale,
    y:
      slideBetween(
        from.y * scale,
        to.y * scale,
        within,
        SLIDE_FRAMES_PER_TILE,
      ) / scale,
  };
}

/**
 * Whether the cursor's emphasis is drawn on this frame. False at phase zero and
 * for the first half of every period, so a board photographed at rest is the
 * board that was drawn before this model existed.
 */
export function cursorEmphasised(frame: number): boolean {
  const phase = ((frame % CURSOR_PULSE_PERIOD) + CURSOR_PULSE_PERIOD) %
    CURSOR_PULSE_PERIOD;
  return phase >= CURSOR_PULSE_REST_FRAMES;
}

/** How far a struck token is knocked back, in pixels, for a given cell size. */
export function flinchNudgeFor(tile: number): number {
  if (tile <= 0) return 0;
  const nudge = Math.floor(tile / 8);
  return nudge > 0 ? nudge : 1;
}

/**
 * The offset a struck token is drawn at on `frame` of the flinch, counted from
 * zero: knocked directly away from whoever struck it for the first half, back
 * at rest for the second, so the last frame of a flinch is the board at rest.
 * Only the sign of `toward` is read.
 */
export function flinchOffset(
  frame: number,
  toward: number,
  tile: number,
): number {
  if (frame < 0 || frame >= FLINCH_KNOCKED_FRAMES) return 0;
  if (toward === 0) return 0;
  const nudge = flinchNudgeFor(tile);
  return toward > 0 ? nudge : -nudge;
}

/**
 * The same nudge as a fraction of a cell, for a client whose cell size belongs
 * to the component that draws it rather than to the caller that counts frames.
 * An eighth of a cell: the console's `tile / 8` with the division done once.
 */
export function flinchOffsetCells(frame: number, toward: number): number {
  return flinchOffset(frame, toward, 8) / 8;
}

// ---------------------------------------------------------------------------
// Which cell of a sequence is drawn
// ---------------------------------------------------------------------------
//
// The TypeScript half of `motion.hpp`'s sequence arithmetic. The generated
// roster ships a walk cycle and an attack lunge as one strip beside each
// standing sprite, in the order below, and a client indexes that strip by
// position. The sheet itself is one row of four 32x32 cells in that same
// order, beside a separate one-cell standing sprite, and every style ships
// every cell of it.

/** The standing sprite: frame 0 of every sequence, and not a cell of the strip. */
export const SEQUENCE_CELL_STAND = -1;
export const SEQUENCE_CELL_WALK_CONTACT = 0;
export const SEQUENCE_CELL_WALK_PASS = 1;
export const SEQUENCE_CELL_LUNGE = 2;
export const SEQUENCE_CELL_CAST = 3;
export const SEQUENCE_CELL_COUNT = 4;

/**
 * And that is the last one. A cell is 32x32 at four bits a texel, or 512 bytes,
 * and a colour-indexed texture holds 2,048 bytes of the Nintendo 64's texture
 * memory, so a strip a client may upload whole stops at four. The C++ header
 * refuses a fifth with a `static_assert`; this mirror states the same number so
 * the browser cannot quietly index past a cell no console can load.
 */
export const SEQUENCE_CELL_CEILING = 4;

/** Cells of the walk cycle. One a tile, which is what makes a tile a step. */
export const WALK_CELLS = 2;

/**
 * The cell a walking token is drawn as, `frame` frames into a slide of
 * `frames`, counted from zero. Standing at both ends: a walk begins standing
 * and arrives standing, so a settled board draws the sprite it always drew.
 */
export function walkCell(frame: number, frames: number): number {
  if (frame <= 0) return SEQUENCE_CELL_STAND;
  if (frames > 0 && frame >= frames) return SEQUENCE_CELL_STAND;
  const step = Math.floor((frame - 1) / SLIDE_FRAMES_PER_TILE);
  return step % WALK_CELLS === 0
    ? SEQUENCE_CELL_WALK_CONTACT
    : SEQUENCE_CELL_WALK_PASS;
}

/**
 * The cell a striking token is drawn as, `frame` frames into a hit counted from
 * zero. It coils for exactly as long as the struck token is knocked away, so
 * the striker and its target are two halves of one gesture.
 */
export function strikeCell(frame: number): number {
  if (frame < 0 || frame >= FLINCH_KNOCKED_FRAMES) return SEQUENCE_CELL_STAND;
  return SEQUENCE_CELL_LUNGE;
}

/**
 * The cell a casting token is drawn as, `frame` frames into the hold. A cast is
 * a pose held rather than a pose thrown, which is the difference between the
 * two gestures a body can make; it ends standing for the same reason a strike
 * does.
 */
export function castCell(frame: number): number {
  if (frame < 0 || frame >= CAST_HOLD_FRAMES) return SEQUENCE_CELL_STAND;
  return SEQUENCE_CELL_CAST;
}

// ---------------------------------------------------------------------------
// Which gesture an attack is drawn as
// ---------------------------------------------------------------------------
//
// The TypeScript half of `motion.hpp`'s derivation, and the whole of it is one
// question: could a damaging magical ability this striker knows have crossed
// this separation? If it could, the blow is a cast. If it could not, a weapon
// threw it: a shot when it crossed a tile, a swing when it did not.
//
// Nothing the simulation reports says which. This is derived from the reach
// bands the striker's own records carry, which every client already holds, and
// deliberately not from a field added to an event to name an animation.

export type AttackGesture = "swing" | "shot" | "cast";

/**
 * Whether a reach band covers a separation, inclusive at both ends, exactly as
 * the engine's own reach rule reads it. A bow of two to three answers neither
 * an adjacent enemy nor one four tiles away.
 */
export function reachCovers(
  separation: number,
  minimum: number,
  maximum: number
): boolean {
  return separation >= minimum && separation <= maximum;
}

/**
 * The gesture an attack is drawn as.
 *
 * `magicReaches` is whether any damaging magical ability the striker knows has a
 * band covering `separation`. `launches` is whether the weapon in its hand
 * cannot strike an adjacent tile: a minimum reach above one, which is what
 * tells a bow from a polearm. Reaching further is not the same as being loosed:
 * a Vow Glaive answers from two tiles and is still a thrust.
 */
export function attackGesture(
  separation: number,
  magicReaches: boolean,
  launches: boolean
): AttackGesture {
  if (magicReaches) return "cast";
  if (separation > 1 && launches) return "shot";
  return "swing";
}

/** How many frames a flight of `tiles` tiles takes. */
export function projectileFramesFor(tiles: number): number {
  return tiles > 0 ? tiles * PROJECTILE_FRAMES_PER_TILE : 0;
}

/** The striker's own half of a gesture, before anything is knocked. */
export function gestureLeadFrames(
  gesture: AttackGesture,
  separation: number
): number {
  if (gesture === "cast") return CAST_HOLD_FRAMES;
  if (gesture === "shot") return projectileFramesFor(separation);
  return 0;
}

/** The whole gesture: the lead, then the landing or the missing. */
export function gestureFrames(
  gesture: AttackGesture,
  separation: number,
  landed: boolean
): number {
  return (
    gestureLeadFrames(gesture, separation) +
    (landed ? FLINCH_FRAMES : MISS_FRAMES)
  );
}

/**
 * A quantity that rises from nothing to `peak` at the middle of `frames` and
 * falls back to nothing: a bolt's arc over the board and the radius of a cast's
 * flare are the same shape, so they are the same arithmetic. Exactly zero at
 * both ends by algebra rather than by clamping, which is what lets a mark be
 * trusted to leave the board settled.
 *
 * Truncating, and multiplying before dividing, so this is the integer the
 * consoles compute and not a browser's float.
 */
export function riseAndFall(
  frame: number,
  frames: number,
  peak: number
): number {
  if (frames <= 0 || peak <= 0) return 0;
  if (frame <= 0 || frame >= frames) return 0;
  return Math.trunc((4 * peak * frame * (frames - frame)) / (frames * frames));
}

/** How high a bolt arcs at the top of its flight: a quarter of a tile. */
export function projectileArcPeak(tile: number): number {
  if (tile <= 0) return 0;
  const peak = Math.trunc(tile / 4);
  return peak > 0 ? peak : 1;
}

/** How wide a cast's flare is drawn at its widest: half a tile, never more. */
export function effectBloomPeak(tile: number): number {
  if (tile <= 0) return 0;
  const peak = Math.trunc(tile / 2);
  return peak > 0 ? peak : 1;
}

// ---------------------------------------------------------------------------
// Animated terrain: the water shimmer's phase
// ---------------------------------------------------------------------------
//
// Water moves by rotating a few entries of its palette ramp at display time.
// Which entries is a property of the art and comes from the generator; when is
// here, because it is timing. Phase zero is the identity permutation, which is
// what lets a board photographed at rest be the board that was always
// photographed.

export const WATER_CYCLE_ENTRIES = 4;
export const WATER_CYCLE_STEP_FRAMES = 8;
export const WATER_CYCLE_PERIOD = WATER_CYCLE_ENTRIES * WATER_CYCLE_STEP_FRAMES;

/** How far the water ramp is rotated on a given frame: 0, 1, 2 or 3. */
export function waterCyclePhase(frame: number): number {
  const steps = Math.floor(frame / WATER_CYCLE_STEP_FRAMES);
  return ((steps % WATER_CYCLE_ENTRIES) + WATER_CYCLE_ENTRIES) %
    WATER_CYCLE_ENTRIES;
}

/**
 * Which entry of the cycled window belongs in slot `slot` on a given frame. A
 * bijection at every phase and the identity at phase zero, so no colour is ever
 * written twice and none is ever dropped.
 */
export function waterCycleSource(slot: number, frame: number): number {
  if (slot < 0 || slot >= WATER_CYCLE_ENTRIES) return slot;
  return (slot + waterCyclePhase(frame)) % WATER_CYCLE_ENTRIES;
}
