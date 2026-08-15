// SPDX-License-Identifier: MIT
import { terrainKind } from "./terrain-presentation";

/**
 * What a cell asks of whoever would stand in it.
 *
 * An authored terrain name resolves to a kind once, by the art library's
 * keyword convention, and then three tables read that one answer: the art
 * library's, which says what the kind looks like, this one, which says who may
 * be there, and `terrainMovementCost`, which says what crossing it costs. None
 * reads another, and these two halves are rules rather than pictures: between
 * them they decide where a character may walk and how far.
 *
 * This mirrors `terrain_passability` in `tools/game_content/src/compiler.cpp`,
 * which writes the same answer into the package a native build plays. Nothing
 * checks the two by inspection: the demo campaign is played through both and
 * its canonical hash includes the board, so a disagreement here shows up as a
 * hash that does not match the native one.
 */
export type TerrainPassability = "open" | "water" | "heights";

/** The engine's own numbering, which is what crosses the boundary. */
export const TERRAIN_PASSABILITY: readonly TerrainPassability[] = [
  "open",
  "water",
  "heights"
];

export function terrainPassability(terrain: string): TerrainPassability {
  const kind = terrainKind(terrain);
  if (kind === "water") return "water";
  if (kind === "mountain") return "heights";
  // Everything else is open ground. Whether a cell takes somebody and what it
  // charges them are two questions, and ground that should be slow rather than
  // shut answers this one with "anyone" and the next one with a price. A
  // terrain the keywords do not match is open too: a game that names its own
  // ground should not silently acquire a wall.
  return "open";
}

/**
 * What every cell of an unpriced board charges, and the cheapest any cell can.
 */
export const MOVEMENT_COST_STEP = 1;

/**
 * What a cell charges whoever walks into it, in steps of a movement allowance.
 *
 * Mirrors `terrain_movement_cost` in `tools/game_content/src/compiler.cpp` on
 * exactly the terms `terrainPassability` mirrors its neighbour, and is checked
 * the same way: the board is inside the canonical hash, so a cell priced
 * differently here than natively is a hash that does not match.
 *
 * Two prices, not a scale. Ground is either ordinary or it is heavy going, and
 * a third band would be a number an author has to learn rather than a fact they
 * can see. What is heavy going is what a character has to get *through* rather
 * than walk *on*: undergrowth, water underfoot, rubble, a slope, loose footing.
 * Snow is not on the list because in a winter setting snow is the ground rather
 * than a feature of it, and a price paid on every cell of a board says nothing
 * about any cell of it; deep snow meant to bog a character down is a marsh with
 * a white sprite. A terrain the keywords do not match charges one, for the
 * reason it is open.
 */
export function terrainMovementCost(terrain: string): number {
  switch (terrainKind(terrain)) {
    case "forest":
    case "sand":
    case "swamp":
    case "hills":
    case "ruins":
    case "bamboo":
      return 2;
    default:
      return MOVEMENT_COST_STEP;
  }
}
