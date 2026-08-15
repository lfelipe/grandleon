// SPDX-License-Identifier: MIT
/**
 * The authoring-time copy of what a reach band and an area of impact cover.
 *
 * **Not what the play path uses.** The engine now answers both questions
 * directly: `aimable_tiles` for the band a character could commit a gesture
 * in, `area_tiles` for the splash a cast would cover around a tile. Play
 * asks it: `legalCastTiles` and `legalTargets` are the engine's own answers,
 * not a band derived here. A board being played can never light a square the
 * engine will refuse, because nothing on that path derives a square at all.
 *
 * What is left is the authoring UI, and it is left for a reason no query can
 * remove: `TargetingShapeGrid` draws the shape an author is picking, before
 * there is an encounter to ask. There is no caster, no board and no character,
 * only a shape and a radius, so `character-recipe.ts` and `readArea` /
 * `readBand` have nothing to point a query at and must hold the rule. Which is
 * precisely why it is held exactly once, and why `targeting-agreement.test.ts`
 * compares this module against the running engine rather than against anyone's
 * belief about it.
 *
 * The rule itself, read out of `engine/simulation/src/encounter.cpp`:
 * `distance()` is Manhattan, the sum of the absolute coordinate differences,
 * and `covered_by()` compares it against 0 for `single`, 1 for `cross` and
 * `radius` for `diamond`. The three area names are therefore one shape family,
 * the Manhattan ball at radius 0, 1 and N, and a reach band is the same
 * geometry with a hole in it, the Manhattan annulus between the minimum and
 * maximum reach.
 */

/** A tile's position relative to an origin, in tiles. */
export interface Offset {
  readonly dx: number;
  readonly dy: number;
}

/** The area names the ability schema offers. */
export type AreaShape = "single" | "cross" | "diamond";

export const areaShapes: readonly AreaShape[] = ["single", "cross", "diamond"];

/** The Manhattan distance the engine measures between two tiles. */
export function manhattan(dx: number, dy: number): number {
  return Math.abs(dx) + Math.abs(dy);
}

/**
 * The radius `covered_by()` compares against for a shape. `single` and `cross`
 * ignore the authored radius entirely. The schema says the radius is "used
 * only by the diamond shape", and the engine's switch is where that is true.
 */
export function areaRadius(shape: AreaShape, radius: number | undefined): number {
  if (shape === "single") return 0;
  if (shape === "cross") return 1;
  return Math.max(0, Math.trunc(radius ?? 0));
}

/** Whether an area of impact covers a tile at this offset from the tile aimed at. */
export function areaCovers(
  shape: AreaShape,
  radius: number | undefined,
  offset: Offset
): boolean {
  return manhattan(offset.dx, offset.dy) <= areaRadius(shape, radius);
}

/**
 * Whether a reach band admits a tile at this offset from where the character
 * stands. A band of 2 to 4 excludes the character's own tile and its
 * neighbours: the hole a minimum reach above one leaves.
 */
export function bandCovers(
  minimumRange: number,
  maximumRange: number,
  offset: Offset
): boolean {
  const separation = manhattan(offset.dx, offset.dy);
  return separation >= minimumRange && separation <= maximumRange;
}

/**
 * Whether an offset falls in a band's hole, inside the minimum reach rather
 * than beyond the maximum. The two exclusions look identical on a grid unless
 * they are drawn apart, and telling them apart is the whole point of drawing a
 * band at all.
 */
export function bandHole(
  minimumRange: number,
  offset: Offset
): boolean {
  return manhattan(offset.dx, offset.dy) < minimumRange;
}

/**
 * Every offset within `extent` of the origin, row-major from the north-west
 * corner: the order a grid draws them in, and the order the engine's own tile
 * queries report in.
 */
export function offsetsWithin(extent: number): Offset[] {
  const offsets: Offset[] = [];
  for (let dy = -extent; dy <= extent; dy += 1) {
    for (let dx = -extent; dx <= extent; dx += 1) offsets.push({ dx, dy });
  }
  return offsets;
}

/** The offsets an area of impact covers, row-major, within a drawn extent. */
export function areaOffsets(
  shape: AreaShape,
  radius: number | undefined,
  extent: number
): Offset[] {
  return offsetsWithin(extent).filter((offset) => areaCovers(shape, radius, offset));
}

/** The offsets a reach band admits, row-major, within a drawn extent. */
export function bandOffsets(
  minimumRange: number,
  maximumRange: number,
  extent: number
): Offset[] {
  return offsetsWithin(extent)
    .filter((offset) => bandCovers(minimumRange, maximumRange, offset));
}

/** A key naming an offset, so painted sets can be compared and deduplicated. */
export function offsetKey(offset: Offset): string {
  return `${offset.dx}:${offset.dy}`;
}

/**
 * What reading a painted shape back into stored fields produced. A refusal
 * carries the plain sentence to show the author: the vocabulary cannot express
 * what they drew, and the fields are left exactly as they were rather than
 * rounded to the nearest legal shape.
 */
export type ShapeReading<Fields> =
  | { readonly expressible: true; readonly fields: Fields }
  | { readonly expressible: false; readonly reason: string };

/** The distances, ascending and deduplicated, that a painted set covers. */
function paintedDistances(painted: readonly Offset[]): number[] {
  return [...new Set(painted.map((offset) => manhattan(offset.dx, offset.dy)))]
    .sort((left, right) => left - right);
}

/**
 * Whether a painted set is exactly every tile at the distances it touches,
 * the property that makes a shape a Manhattan figure at all. A set that holds
 * some tiles at distance 2 but not others is not a ball, an annulus, or
 * anything else this vocabulary has a name for, and naming the missing tiles
 * is the most useful thing that can be said about it.
 */
function unfilledRings(painted: readonly Offset[], extent: number): number[] {
  const held = new Set(painted.map(offsetKey));
  const distances = new Set(paintedDistances(painted));
  const missing = new Set<number>();
  for (const offset of offsetsWithin(extent)) {
    const separation = manhattan(offset.dx, offset.dy);
    if (!distances.has(separation)) continue;
    if (!held.has(offsetKey(offset))) missing.add(separation);
  }
  return [...missing].sort((left, right) => left - right);
}

/** Whether an ascending list of distances is one unbroken run. */
function unbroken(distances: readonly number[]): boolean {
  return distances.every(
    (distance, index) => index === 0 || distance === (distances[index - 1] ?? 0) + 1
  );
}

/**
 * Reads a painted set back into an `areaShape` and a `radius`, or says why no
 * pair expresses it. Every refusal here is an observation about the
 * vocabulary rather than a defect in the widget: the shapes it turns away are
 * the shapes the current enum has no name for.
 */
export function readArea(
  painted: readonly Offset[],
  extent: number
): ShapeReading<{ areaShape: AreaShape; radius: number }> {
  if (painted.length === 0) {
    return {
      expressible: false,
      reason:
        "An area of impact always covers the tile aimed at. Paint at least " +
        "that tile."
    };
  }
  if (!painted.some((offset) => offset.dx === 0 && offset.dy === 0)) {
    return {
      expressible: false,
      reason:
        "An area of impact always covers the tile aimed at, and this shape " +
        "leaves it out. A hollow ring is not something the current area " +
        "shapes can express."
    };
  }

  const unfilled = unfilledRings(painted, extent);
  if (unfilled.length > 0) {
    return {
      expressible: false,
      reason:
        `An area of impact covers every tile within a distance of the tile ` +
        `aimed at. This shape covers only part of the tiles at a distance of ` +
        `${unfilled.join(" and ")}, which the current area shapes cannot ` +
        `express. A square, a line and a cone all take this form.`
    };
  }

  const distances = paintedDistances(painted);
  if (!unbroken(distances) || distances[0] !== 0) {
    return {
      expressible: false,
      reason:
        "An area of impact reaches out from the tile aimed at without gaps. " +
        "This shape skips a distance, which the current area shapes cannot " +
        "express."
    };
  }

  const radius = distances.at(-1) ?? 0;
  if (radius === 0) return { expressible: true, fields: { areaShape: "single", radius } };
  if (radius === 1) return { expressible: true, fields: { areaShape: "cross", radius } };
  return { expressible: true, fields: { areaShape: "diamond", radius } };
}

/**
 * Reads a painted set back into a `minimumRange` and a `maximumRange`, or says
 * why no pair expresses it.
 */
export function readBand(
  painted: readonly Offset[],
  extent: number
): ShapeReading<{ minimumRange: number; maximumRange: number }> {
  if (painted.length === 0) {
    return {
      expressible: false,
      reason:
        "A reach band always admits at least one distance. Paint at least " +
        "one tile."
    };
  }
  if (painted.some((offset) => offset.dx === 0 && offset.dy === 0)) {
    return {
      expressible: false,
      reason:
        "A reach band is measured from where the character stands, so it " +
        "never admits that tile. The closest reach a band can have is one."
    };
  }

  const unfilled = unfilledRings(painted, extent);
  if (unfilled.length > 0) {
    return {
      expressible: false,
      reason:
        `A reach band admits every tile at a distance or none of them. This ` +
        `shape admits only part of the tiles at a distance of ` +
        `${unfilled.join(" and ")}, which a band cannot express. A line, a ` +
        `beam and a facing-dependent reach all take this form.`
    };
  }

  const distances = paintedDistances(painted);
  if (!unbroken(distances)) {
    return {
      expressible: false,
      reason:
        "A reach band is one unbroken run of distances. This shape skips a " +
        "distance, which would need two bands rather than one."
    };
  }

  return {
    expressible: true,
    fields: {
      minimumRange: distances[0] ?? 1,
      maximumRange: distances.at(-1) ?? 1
    }
  };
}

/**
 * A plain sentence for a reach band, for the author reading the grid. It
 * describes what the numbers mean and never judges them.
 */
export function describeBand(minimumRange: number, maximumRange: number): string {
  if (minimumRange > maximumRange) {
    return `No tile is both at least ${minimumRange} and at most ` +
      `${maximumRange} tiles away, so this band admits nothing.`;
  }
  const tiles = (count: number) => (count === 1 ? "1 tile" : `${count} tiles`);
  if (minimumRange === maximumRange) {
    return `Strikes at exactly ${tiles(maximumRange)} away.`;
  }
  if (minimumRange <= 1) {
    return `Strikes from ${tiles(minimumRange)} out to ${tiles(maximumRange)} away.`;
  }
  return `Strikes from ${tiles(minimumRange)} out to ${tiles(maximumRange)} ` +
    `away, and cannot strike anything closer than ${tiles(minimumRange)}.`;
}

/** A plain sentence for an area of impact. */
export function describeArea(shape: AreaShape, radius: number | undefined): string {
  const resolved = areaRadius(shape, radius);
  if (resolved === 0) return "Covers only the tile aimed at.";
  const tiles = resolved === 1 ? "1 tile" : `${resolved} tiles`;
  const covered = areaOffsets(shape, radius, resolved).length;
  return `Covers the tile aimed at and everything within ${tiles} of it: ` +
    `${covered} tiles.`;
}
