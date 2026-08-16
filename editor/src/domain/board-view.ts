// SPDX-License-Identifier: MIT
import { TERRAIN_ELEVATION } from "../generated/board-art";
import { terrainSheetKind } from "./board-art";

/**
 * The board's presentation model, in the editor's own words.
 *
 * This is the TypeScript half of
 * `platform/view/include/grandleon/view/board_view.hpp`: the same projection,
 * the same headroom and the same depth order, so the browser, the desktop and
 * the console agree about where a cell lands and what covers what. The C++
 * header is the specification; when the two must change, they change together.
 *
 * Everything here is arithmetic over the board a client already holds. None of
 * it is state: an elevation is a drawing offset, a depth key is a sort key, and
 * neither is ever written to a project, validated, or hashed.
 *
 * The projection is axis-aligned by decision. Elevation lifts a cell up the
 * screen and does nothing else, with no rotation and no diamond grid, because
 * the generated tiles are top-down squares and a rotated grid would re-open the art
 * generator.
 */

/**
 * Where the board's pixels go: the origin of the camera's top-left visible
 * cell, the size of a cell, and how far one level of elevation lifts it.
 *
 * `elevationStep` is carried rather than derived from `tile` so a caller can
 * pin it to zero and get, exactly, the flat board it drew before this model
 * existed.
 */
export interface Projection {
  readonly originX: number;
  readonly originY: number;
  readonly tile: number;
  readonly elevationStep: number;
}

/**
 * The top-left visible cell. The editor's board has no camera, drawing the
 * whole map and scrolling it with the browser, so it passes `{ x: 0, y: 0 }`.
 * The parameter stays anyway, so this file and the C++ header read alike and a
 * reader comparing them is never left wondering which one dropped a term.
 */
export interface ViewOrigin {
  readonly x: number;
  readonly y: number;
}

/** A board with no camera: every cell is visible at its own coordinates. */
export const NO_CAMERA: ViewOrigin = { x: 0, y: 0 };

// ---------------------------------------------------------------------------
// Fitting a board to a screen
// ---------------------------------------------------------------------------

/**
 * What a machine will spend on a board, in that machine's own pixels.
 *
 * A console arrives at these four numbers from its own hardware: how much of
 * the frame is left after the interface has taken its share, how large a cell
 * its art is worth drawing at, and how small a cell is still a cell. They are
 * properties of a screen and a sprite sheet, so they differ per machine and the
 * rule below does not. `console-fit.ts` holds the numbers each console declares
 * and is where a reader should go for them.
 */
export interface FitRule {
  /** Pixels the board may use, across and down. */
  readonly frameW: number;
  readonly frameH: number;
  /**
   * Never draw a cell larger than this. A small board would otherwise be drawn
   * at whatever size divides the frame, which on a two-by-two board is an
   * enormous cell made of a handful of texels.
   */
  readonly largestTile: number;
  /**
   * Never shrink a cell below this. Past it the board scrolls instead, because
   * a board nobody can read is worse than a board with edges.
   */
  readonly smallestTile: number;
  /**
   * The cell a board too large to fit is drawn at. A separate number from
   * `smallestTile` on purpose: the smallest readable cell is the point at which
   * fitting stops being worth it, and a machine may reasonably give a scrolling
   * board a more comfortable cell than that at the cost of a narrower window.
   */
  readonly scrollingTile: number;
}

/** A cell size and a window, in cells. */
export interface BoardFit {
  readonly tile: number;
  readonly viewW: number;
  readonly viewH: number;
  /**
   * True when the window is smaller than the board in either direction, so a
   * caller can say plainly whether this board has edges the player must travel
   * to.
   */
  readonly scrolling: boolean;
}

/**
 * Chooses the largest cell that shows the whole board, or falls back to a
 * window that scrolls.
 *
 * The order matters and is the part worth reading. The cell is the *smaller* of
 * what the two dimensions allow, because a cell is square and the tighter axis
 * decides. It is then capped, and only then compared against the floor: capping
 * first means a board small enough to want a huge cell is not also judged to
 * have failed the floor, which it obviously has not.
 *
 * Board sizes are whole cells and are expected to arrive that way; a caller
 * holding something an author is still typing should say so itself rather than
 * ask this what half a cell fits in. Zero and negative are treated as one, the
 * way the C++ does, where the reason is that an integer division by zero on a
 * console is a trap the machine does not come back from.
 */
export function fitBoard(rule: FitRule, mapW: number, mapH: number): BoardFit {
  const width = mapW > 0 ? mapW : 1;
  const height = mapH > 0 ? mapH : 1;

  let tile = Math.floor(rule.frameW / width);
  const down = Math.floor(rule.frameH / height);
  if (down < tile) tile = down;
  if (tile > rule.largestTile) tile = rule.largestTile;
  if (tile >= rule.smallestTile) {
    return { tile, viewW: width, viewH: height, scrolling: false };
  }

  const scrolling = rule.scrollingTile;
  let viewW = width;
  let viewH = height;
  if (scrolling > 0) {
    const across = Math.floor(rule.frameW / scrolling);
    const downCells = Math.floor(rule.frameH / scrolling);
    if (across < viewW) viewW = across;
    if (downCells < viewH) viewH = downCells;
  }
  // A window of no cells would draw nothing and divide by zero downstream.
  if (viewW < 1) viewW = 1;
  if (viewH < 1) viewH = 1;
  return {
    tile: scrolling,
    viewW,
    viewH,
    scrolling: viewW < width || viewH < height
  };
}

/**
 * How far a cell rises per level of elevation, for a given cell size. A quarter
 * of the tile reads as a step without lifting a cell clear of the row behind
 * it, and it divides exactly for every tile size the clients use. A non-zero
 * tile always gets at least one pixel, so a tiny tile still steps.
 */
export function elevationStepFor(tile: number): number {
  const step = Math.floor(tile / 4);
  if (step > 0) return step;
  return tile > 0 ? 1 : 0;
}

/**
 * How high an authored terrain name stands. The name is resolved through the
 * same keyword table that chooses the tile's sheet, so the height a cell is
 * drawn at can never disagree with the art drawn there; a name the library does
 * not know draws as grass and therefore stands on the ground.
 */
export function terrainElevation(terrainName: string): number {
  return TERRAIN_ELEVATION[terrainSheetKind(terrainName)] ?? 0;
}

/**
 * The elevation of every cell, in row-major order. Cells past the end of the
 * authored terrain read as ground rather than as a gap, so a half-written board
 * still draws.
 */
export function boardElevations(
  terrain: readonly string[],
  width: number,
  height: number
): number[] {
  const cells = Math.max(0, width) * Math.max(0, height);
  return Array.from(
    { length: cells },
    (_, index) => terrainElevation(terrain[index] ?? "")
  );
}

/** The tallest cell on the board, or zero for a board with no cells. */
export function maxElevation(elevations: readonly number[]): number {
  let highest = 0;
  for (const elevation of elevations) {
    if (elevation > highest) highest = elevation;
  }
  return highest;
}

/**
 * The furthest up the screen a cell may be drawn, whatever elevation its
 * terrain declares: three eighths of a tile.
 *
 * The geometry that fixes the number. A cell drawn `L` pixels higher than the
 * cell behind it covers the bottom `L` rows of that cell's rectangle, and a
 * cell's centre row sits `tile / 2` below its own top edge, so the centre of
 * the cell behind survives exactly while `L < tile - tile / 2`. Half a tile is
 * the boundary, not a safe value, and two levels of a quarter-tile step land
 * precisely on it, which is what this cap moves away from. The bound is the
 * halfway line less half a step, `tile / 2 - tile / 8`, so a raised cell's top
 * edge stays at least an eighth of a tile below the centre behind it: 37px
 * against a boundary of 50 on the editor's 100px cell.
 *
 * It bounds how high a cell is *drawn*, never how high a terrain *is*. The
 * elevation is authored content and a renderer that can afford real relief
 * reads the level count and means it.
 */
export function maxLiftFor(tile: number): number {
  return tile > 0 ? Math.floor((tile * 3) / 8) : 0;
}

/**
 * The upward offset an elevated cell is drawn at: its elevation in steps,
 * bounded by `maxLiftFor`. A negative elevation is clamped away rather than
 * sinking the cell: a sunken cell would be covered by the row in front of it,
 * and no client draws a hole. A step of zero draws elevated content flat.
 */
export function lift(
  elevation: number,
  elevationStep: number,
  tile: number
): number {
  if (elevation <= 0 || elevationStep <= 0) return 0;
  const cap = maxLiftFor(tile);
  return Math.min(elevation * elevationStep, cap);
}

export function cellLeft(
  projection: Projection,
  camera: ViewOrigin,
  cellX: number
): number {
  return projection.originX + (cellX - camera.x) * projection.tile;
}

export function cellTop(
  projection: Projection,
  camera: ViewOrigin,
  cellY: number,
  elevation: number
): number {
  return (
    projection.originY +
    (cellY - camera.y) * projection.tile -
    lift(elevation, projection.elevationStep, projection.tile)
  );
}

/**
 * Where something sampling a cell should look: the centre of the cell as
 * drawn, which is the flat centre lifted by the same offset the art was. That
 * is the pixel the renderer drew this cell's middle at, whatever height the
 * cell stands at, and nothing can have been painted over it since. A cell
 * raised above the one behind it covers that cell's rectangle from the bottom
 * up by the difference in their lifts, which `lift` bounds an eighth of a tile
 * short of the centre row.
 *
 * A whole cell rectangle is a different question: two rows at unequal heights
 * share a band as deep as the difference in their lifts, and whatever stands in
 * the higher-drawn of the two is painted across it. A caller scanning a
 * rectangle rather than a centre sets those rows aside, top and bottom. See the
 * same note on `cell_centre_y` in
 * `platform/view/include/grandleon/view/board_view.hpp`.
 */
export function cellCentreX(
  projection: Projection,
  camera: ViewOrigin,
  cellX: number
): number {
  return cellLeft(projection, camera, cellX) + Math.floor(projection.tile / 2);
}

export function cellCentreY(
  projection: Projection,
  camera: ViewOrigin,
  cellY: number,
  elevation: number
): number {
  return (
    cellTop(projection, camera, cellY, elevation) +
    Math.floor(projection.tile / 2)
  );
}

/**
 * The room a board needs above its first row so the tallest lift stays inside
 * the frame. It is that lift, through the same bounded arithmetic the board is
 * drawn with, so a layout can never reserve room for a height no cell is drawn
 * at. A board whose terrain is all at elevation zero needs none, which is what
 * keeps a flat board's geometry identical to what it was before any of this
 * existed.
 */
export function headroom(
  highestElevation: number,
  elevationStep: number,
  tile: number
): number {
  return lift(highestElevation, elevationStep, tile);
}

/**
 * What a draw item is for. Terrain is the ground, the shadow grounds a
 * billboard against it, and the billboard is the unit itself. The stack is
 * fixed, which is what lets a lifted billboard pass in front of the ground
 * behind it without anything here owning a depth buffer.
 */
export const Layer = {
  terrain: 0,
  shadow: 1,
  unit: 2
} as const;

export type Layer = (typeof Layer)[keyof typeof Layer];

/**
 * The draw order as one number, ascending: back to front.
 *
 * The terms, and why there are only these:
 *
 * - `layer` first, because the stack above is fixed.
 * - `elevation` next. In an axis-aligned top-down projection a cell's art only
 *   ever hangs over the cells *behind* it, and two cells overlap exactly when
 *   the nearer one is drawn higher. Drawing low ground before high ground is
 *   therefore not a heuristic: it is the whole of the occlusion rule. Two
 *   cells at one elevation share one offset and so cannot overlap at all.
 * - `batch` next, and only because of that last sentence: items sharing a layer
 *   and an elevation may be reordered freely, so a renderer may group them by
 *   whatever it pays for: a texture upload on the console, a binding on the
 *   desktop. Nothing about the picture depends on it.
 * - then row, then column, so the order is total and identical on every client
 *   whatever order the caller walked the board in.
 *
 * The five terms are packed into disjoint bit ranges, most significant first:
 * layer at 63..56, elevation at 55..48, batch at 47..32, row at 31..16, column
 * at 15..0. Each is clamped to the width it is given, so a caller handing over
 * a number too large for its field loses precision inside that field and can
 * never reach into the field above it, which matters most for `batch`, the
 * one term a renderer picks freely: a batch overflowing into the elevation
 * field would reorder the layers of ground, the one thing the order exists to
 * get right.
 *
 * The key is a 64-bit quantity, so it is a `bigint`: packing it into a
 * double would lose the low bits of the layer field, and packing it with
 * JavaScript's bitwise operators would lose everything above bit 31. Comparing
 * bigints is exact, which is the only property the sort needs.
 */
export function depthKey(
  layer: Layer,
  elevation: number,
  batch: number,
  cellY: number,
  cellX: number
): bigint {
  const field = (value: number, limit: number): bigint => {
    if (!(value > 0)) return 0n;
    return BigInt(Math.min(Math.floor(value), limit));
  };
  return (
    (BigInt(layer) << 56n) |
    (field(elevation, 0xff) << 48n) |
    (field(batch, 0xffff) << 32n) |
    (field(cellY, 0xffff) << 16n) |
    field(cellX, 0xffff)
  );
}

/** One thing to draw: what it is, which cell it belongs to, and where it goes. */
export interface DrawItem {
  readonly layer: Layer;
  readonly cellX: number;
  readonly cellY: number;
  readonly elevation: number;
  /** The destination rectangle, in board pixels. */
  readonly x: number;
  readonly y: number;
  readonly w: number;
  readonly h: number;
  /** The caller's own name for the item, carried through untouched. */
  readonly subject: string;
  readonly depth: bigint;
}

/**
 * One item, placed by the projection. A billboard occupies its cell exactly,
 * like the ground under it, so neither a unit nor its shadow can reach into a
 * neighbouring cell's centre, which is what keeps a centre-sampling probe
 * meaning what it meant before any of this was lifted.
 */
export function drawItem(
  projection: Projection,
  camera: ViewOrigin,
  layer: Layer,
  cellX: number,
  cellY: number,
  elevation: number,
  subject: string,
  batch = 0
): DrawItem {
  return {
    layer,
    cellX,
    cellY,
    elevation,
    x: cellLeft(projection, camera, cellX),
    y: cellTop(projection, camera, cellY, elevation),
    w: projection.tile,
    h: projection.tile,
    subject,
    depth: depthKey(layer, elevation, batch, cellY, cellX)
  };
}

/** A unit as the draw order sees it: a name, and the cell it stands on. */
export interface BoardUnit {
  readonly id: string;
  readonly x: number;
  readonly y: number;
}

export interface BoardContents {
  readonly terrain: readonly string[];
  readonly width: number;
  readonly height: number;
  readonly units?: readonly BoardUnit[];
}

/**
 * Everything a board draws, back to front: the ground, then a shadow under each
 * unit, then the units. Sorted by the depth key alone, so a caller may hand the
 * cells over in whatever order it happens to hold them.
 */
export function boardDrawOrder(
  board: BoardContents,
  projection: Projection,
  camera: ViewOrigin = NO_CAMERA
): DrawItem[] {
  const elevations = boardElevations(board.terrain, board.width, board.height);
  const elevationAt = (x: number, y: number) =>
    elevations[y * board.width + x] ?? 0;
  const items: DrawItem[] = [];
  for (let y = 0; y < board.height; y += 1) {
    for (let x = 0; x < board.width; x += 1) {
      const index = y * board.width + x;
      items.push(drawItem(
        projection,
        camera,
        Layer.terrain,
        x,
        y,
        elevations[index] ?? 0,
        board.terrain[index] ?? ""
      ));
    }
  }
  for (const unit of board.units ?? []) {
    const elevation = elevationAt(unit.x, unit.y);
    for (const layer of [Layer.shadow, Layer.unit] as const) {
      items.push(drawItem(
        projection, camera, layer, unit.x, unit.y, elevation, unit.id
      ));
    }
  }
  // A comparator rather than a subtraction: the keys are bigints, and the
  // difference of two of them is not a number the sort would accept.
  return items.sort((left, right) => {
    if (left.depth < right.depth) return -1;
    return left.depth > right.depth ? 1 : 0;
  });
}

/**
 * Whether two drawn items cover any of the same pixels. This is the predicate
 * the occlusion rule is stated in: where it is false, the order of the two
 * items cannot change the picture, and a renderer walking the board in any
 * order it likes is free to disagree with the depth key about them.
 */
export function overlaps(left: DrawItem, right: DrawItem): boolean {
  return (
    left.x < right.x + right.w &&
    right.x < left.x + left.w &&
    left.y < right.y + right.h &&
    right.y < left.y + left.h
  );
}
