// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  Layer,
  NO_CAMERA,
  boardDrawOrder,
  boardElevations,
  cellCentreX,
  cellCentreY,
  cellLeft,
  cellTop,
  depthKey,
  drawItem,
  elevationStepFor,
  headroom,
  lift,
  maxElevation,
  maxLiftFor,
  overlaps,
  terrainElevation,
  type Projection
} from "./board-view";
import { terrainKind } from "./terrain-presentation";
import { TERRAIN_ELEVATION, TERRAIN_KINDS } from "../generated/board-art";

// The editor's own board: hundred-pixel cells, no camera, a quarter-tile step.
const board: Projection = {
  originX: 32,
  originY: 32,
  tile: 100,
  elevationStep: elevationStepFor(100)
};

// The same board before elevation existed, which is what a client pins when it
// wants the flat picture exactly.
const flat: Projection = { ...board, elevationStep: 0 };

describe("board-view projection", () => {
  it("steps a quarter of a tile, and never less than a pixel", () => {
    expect(elevationStepFor(100)).toBe(25);
    expect(elevationStepFor(32)).toBe(8);
    expect(elevationStepFor(16)).toBe(4);
    // Tiles too small to quarter still step, because a step of zero would
    // draw raised ground on the ground.
    expect(elevationStepFor(3)).toBe(1);
    expect(elevationStepFor(1)).toBe(1);
    // A board with no cell size has nothing to lift.
    expect(elevationStepFor(0)).toBe(0);
  });

  it("places a cell on the ground where the flat board always placed it", () => {
    expect(cellLeft(board, NO_CAMERA, 0)).toBe(32);
    expect(cellLeft(board, NO_CAMERA, 3)).toBe(332);
    expect(cellTop(board, NO_CAMERA, 0, 0)).toBe(32);
    expect(cellTop(board, NO_CAMERA, 2, 0)).toBe(232);
    expect(cellCentreX(board, NO_CAMERA, 1)).toBe(182);
    expect(cellCentreY(board, NO_CAMERA, 1, 0)).toBe(182);
  });

  it("lifts a raised cell up the screen and nowhere else", () => {
    expect(lift(1, board.elevationStep, board.tile)).toBe(25);
    // Two levels of a 100px cell ask for 50, half the cell, which is the
    // centre row of the cell behind. The cap answers three eighths, 37.
    expect(lift(2, board.elevationStep, board.tile)).toBe(37);
    expect(cellTop(board, NO_CAMERA, 2, 2)).toBe(232 - 37);
    // The column is untouched: the projection is axis-aligned, so elevation is
    // a vertical offset and never a horizontal one.
    expect(cellLeft(board, NO_CAMERA, 3)).toBe(332);
    // A probe follows the art up, so it keeps sampling the cell it names.
    expect(cellCentreY(board, NO_CAMERA, 2, 2)).toBe(232 + 50 - 37);
    expect(cellCentreX(board, NO_CAMERA, 1)).toBe(182);
  });

  it("caps the lift below half a cell, however tall the terrain", () => {
    expect(maxLiftFor(100)).toBe(37);
    // Every level past the cap draws at the cap. The elevation itself is
    // untouched, being content that a renderer able to afford real relief
    // reads, but no renderer draws a cell higher than this.
    for (const elevation of [2, 3, 7, 40]) {
      expect(lift(elevation, board.elevationStep, board.tile)).toBe(37);
    }
    // The property the cap exists for: a cell in the row in front is always
    // drawn below the centre of the cell behind it, so a probe that samples a
    // centre reads the cell it names. Asserted over every pair of elevations
    // rather than over the two the art library ships.
    for (let behind = 0; behind <= 8; behind += 1) {
      for (let front = 0; front <= 8; front += 1) {
        expect(cellTop(board, NO_CAMERA, 2, front))
          .toBeGreaterThan(cellCentreY(board, NO_CAMERA, 1, behind));
      }
    }
    // And it holds for any cell size a client could choose, not only this
    // one: a lift is always strictly under the boundary at half a tile.
    for (let tile = 1; tile <= 256; tile += 1) {
      expect(maxLiftFor(tile)).toBeLessThan(tile - Math.floor(tile / 2));
    }
  });

  it("draws every cell flat when the step is pinned to zero", () => {
    for (let elevation = 0; elevation <= 3; elevation += 1) {
      expect(cellTop(flat, NO_CAMERA, 2, elevation)).toBe(232);
    }
  });

  it("clamps a negative elevation away rather than sinking the cell", () => {
    expect(lift(-1, board.elevationStep, board.tile)).toBe(0);
    expect(lift(-7, board.elevationStep, board.tile)).toBe(0);
    expect(cellTop(board, NO_CAMERA, 1, -4)).toBe(cellTop(board, NO_CAMERA, 1, 0));
    expect(headroom(-2, board.elevationStep, board.tile)).toBe(0);
  });

  it("offsets by the camera's top-left cell", () => {
    const camera = { x: 2, y: 1 };
    expect(cellLeft(board, camera, 2)).toBe(32);
    expect(cellTop(board, camera, 1, 0)).toBe(32);
    expect(cellTop(board, camera, 3, 1)).toBe(207);
  });

  it("reserves headroom only for a board that has something raised", () => {
    expect(headroom(0, board.elevationStep, board.tile)).toBe(0);
    expect(headroom(1, board.elevationStep, board.tile)).toBe(25);
    // The room reserved is the room the tallest cell is drawn at, cap and
    // all, so a layout can never hold space for a height nothing reaches.
    expect(headroom(2, board.elevationStep, board.tile)).toBe(37);
    expect(headroom(9, board.elevationStep, board.tile)).toBe(37);
  });
});

describe("board-view terrain elevation", () => {
  it("reads the authored name through the art's own keyword table", () => {
    expect(terrainElevation("mountain pass")).toBe(2);
    expect(terrainElevation("grassy hill")).toBe(1);
    expect(terrainElevation("river")).toBe(0);
    expect(terrainElevation("Rocky Highlands")).toBe(2);
  });

  it("stands a name the art does not know on the ground", () => {
    // It draws from the grass sheet, so standing it at grass height is the
    // only answer that cannot disagree with the tile drawn there.
    expect(terrainKind("the void")).toBe("custom");
    expect(terrainElevation("the void")).toBe(0);
    expect(terrainElevation("")).toBe(0);
  });

  it("carries an elevation for every kind the art library holds", () => {
    for (const kind of TERRAIN_KINDS) {
      expect(TERRAIN_ELEVATION[kind]).toBeTypeOf("number");
      expect(TERRAIN_ELEVATION[kind]).toBeGreaterThanOrEqual(0);
    }
  });

  it("reads a whole board, and answers for a board with no cells", () => {
    const terrain = ["grass", "mountain", "hill", "swamp"];
    expect(boardElevations(terrain, 2, 2)).toEqual([0, 2, 1, 0]);
    expect(maxElevation(boardElevations(terrain, 2, 2))).toBe(2);
    expect(boardElevations([], 0, 0)).toEqual([]);
    expect(maxElevation([])).toBe(0);
    // Cells past the end of the authored terrain read as ground, so a
    // half-written board still draws.
    expect(boardElevations(["mountain"], 2, 1)).toEqual([2, 0]);
  });
});

describe("board-view depth order", () => {
  it("keeps the layers in their fixed stack", () => {
    const at = (layer: Layer) => depthKey(layer, 0, 0, 0, 0);
    expect(at(Layer.terrain)).toBeLessThan(at(Layer.shadow));
    expect(at(Layer.shadow)).toBeLessThan(at(Layer.unit));
    // A layer outranks everything below it: the highest ground in the world is
    // still drawn before the first shadow.
    expect(depthKey(Layer.terrain, 0xffff, 0, 0xffff, 0xffff))
      .toBeLessThan(depthKey(Layer.shadow, 0, 0, 0, 0));
  });

  it("puts elevation above the row, which is the occlusion rule itself", () => {
    // Low ground before high ground, whatever row each is in: the only cells
    // that can overlap are a raised cell and the lower ground behind it.
    expect(depthKey(Layer.terrain, 0, 0, 9, 0))
      .toBeLessThan(depthKey(Layer.terrain, 1, 0, 0, 0));
    expect(depthKey(Layer.terrain, 1, 0, 0, 0))
      .toBeLessThan(depthKey(Layer.terrain, 2, 0, 0, 0));
  });

  it("orders a row before a column, and a batch before both", () => {
    expect(depthKey(Layer.terrain, 0, 0, 0, 9))
      .toBeLessThan(depthKey(Layer.terrain, 0, 0, 1, 0));
    expect(depthKey(Layer.terrain, 0, 0, 1, 0))
      .toBeLessThan(depthKey(Layer.terrain, 0, 0, 1, 1));
    // Grouping is free within one layer and one elevation, because nothing
    // there can overlap anything else there.
    expect(depthKey(Layer.terrain, 0, 0, 5, 5))
      .toBeLessThan(depthKey(Layer.terrain, 0, 1, 0, 0));
  });

  it("keeps each term inside its own field", () => {
    // Every term is checked at a value far past what the board passes today,
    // because the terms a renderer picks freely are the ones nothing else
    // bounds. A batch reaching into the elevation field would reorder the
    // layers of ground, which is the one thing this order exists to get right.
    expect(depthKey(Layer.terrain, 0, 0xffff, 0xffff, 0xffff))
      .toBeLessThan(depthKey(Layer.terrain, 1, 0, 0, 0));
    expect(depthKey(Layer.terrain, 0, 0, 0xffff, 0xffff))
      .toBeLessThan(depthKey(Layer.terrain, 0, 1, 0, 0));
    expect(depthKey(Layer.terrain, 0, 0, 0, 0xffff))
      .toBeLessThan(depthKey(Layer.terrain, 0, 0, 1, 0));
    expect(depthKey(Layer.terrain, 0xff, 0xffff, 0xffff, 0xffff))
      .toBeLessThan(depthKey(Layer.shadow, 0, 0, 0, 0));
    // Past a field's width a term saturates rather than carrying, so an
    // absurd value is indistinguishable from the largest sensible one instead
    // of corrupting the term above it.
    expect(depthKey(Layer.terrain, 0xffff, 0, 0, 0))
      .toBe(depthKey(Layer.terrain, 0xff, 0, 0, 0));
    expect(depthKey(Layer.terrain, 0, 0x10ffff, 0, 0))
      .toBe(depthKey(Layer.terrain, 0, 0xffff, 0, 0));
  });

  it("gives one answer per set of terms, so ties never wobble", () => {
    // A function of its terms and of nothing else: differing in any one term
    // sorts two cells apart. Comparing one call against itself would say
    // nothing about either.
    const base = depthKey(Layer.unit, 2, 1, 3, 4);
    for (const other of [
      depthKey(Layer.terrain, 2, 1, 3, 4),
      depthKey(Layer.unit, 3, 1, 3, 4),
      depthKey(Layer.unit, 2, 2, 3, 4),
      depthKey(Layer.unit, 2, 1, 4, 4),
      depthKey(Layer.unit, 2, 1, 3, 5)
    ]) {
      expect(other).not.toBe(base);
    }
    // Negative terms are floored to zero rather than wrapping into the field
    // above them.
    expect(depthKey(Layer.terrain, -1, 0, -1, -1))
      .toBe(depthKey(Layer.terrain, 0, 0, 0, 0));
  });

  it("sorts a whole board back to front, ground first and units last", () => {
    const order = boardDrawOrder(
      {
        terrain: ["grass", "mountain", "grass", "grass"],
        width: 2,
        height: 2,
        units: [{ id: "scout", x: 1, y: 0 }]
      },
      board
    );
    expect(order.map((item) => item.layer)).toEqual([
      Layer.terrain, Layer.terrain, Layer.terrain, Layer.terrain,
      Layer.shadow, Layer.unit
    ]);
    // The mountain is drawn after the flat ground around it, and the scout
    // standing on it inherits its height.
    expect(order[3]).toMatchObject({ cellX: 1, cellY: 0, elevation: 2 });
    // 32 above the board's origin less the capped lift of 37.
    expect(order[5]).toMatchObject({ subject: "scout", elevation: 2, y: -5 });
    for (let index = 1; index < order.length; index += 1) {
      expect(order[index - 1]!.depth < order[index]!.depth).toBe(true);
    }
  });

  it("draws nothing for a board with no cells, and one cell for one", () => {
    expect(boardDrawOrder({ terrain: [], width: 0, height: 0 }, board))
      .toEqual([]);
    // A board with no rows still has no cells, whatever its width claims.
    expect(boardDrawOrder({ terrain: ["grass"], width: 1, height: 0 }, board))
      .toEqual([]);
    const single = boardDrawOrder(
      { terrain: ["mountain"], width: 1, height: 1 }, board
    );
    expect(single).toHaveLength(1);
    expect(single[0]).toMatchObject({
      layer: Layer.terrain, cellX: 0, cellY: 0, elevation: 2, x: 32, y: -5,
      w: 100, h: 100
    });
  });

  it("knows which cells can cover each other and which cannot", () => {
    const raised = drawItem(board, NO_CAMERA, Layer.terrain, 1, 1, 2, "mountain");
    const behind = drawItem(board, NO_CAMERA, Layer.terrain, 1, 0, 0, "grass");
    const beside = drawItem(board, NO_CAMERA, Layer.terrain, 0, 1, 0, "grass");
    const ahead = drawItem(board, NO_CAMERA, Layer.terrain, 1, 2, 0, "grass");
    // A raised cell hangs over the cell behind it, and over nothing else.
    expect(overlaps(raised, behind)).toBe(true);
    expect(overlaps(raised, beside)).toBe(false);
    expect(overlaps(raised, ahead)).toBe(false);
    // Two cells at one elevation share one offset and so cannot overlap.
    expect(overlaps(behind, beside)).toBe(false);
  });
});
