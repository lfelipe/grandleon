// SPDX-License-Identifier: MIT
// The editor's board draws movement range and the enemy danger zone from the
// engine's own read-only queries rather than from a TypeScript copy of the
// movement rule. These tests hold that promise to the only standard that
// matters: what the board offers is exactly what the engine accepts.
//
// The move rule lives once. Two copies of it, one in the engine and one in
// playtest-session's `legalMoves`, is precisely the arrangement that drifts,
// and the first test below would fail the moment a second copy that disagreed
// appeared.

import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import {
  canAct,
  dangerTiles,
  endPlaytest,
  legalMoves,
  startPlaytest,
  waitUnit,
  type PlaytestState
} from "./playtest-session";

// A seven-by-five keep. The scout on the west edge is walled into its column by
// three motionless opponents, a walk passing through its own side and being
// stopped by the other, so its range is decided by who is in the way rather
// than by its allowance. The archer carries a bow that cannot strike closer than
// two tiles, which leaves a hole in the danger zone around itself. The statue
// cannot move at all.
function keepProject(): SourceProject {
  return {
    schemaVersion: "1.2.0",
    packageId: "1a5b0f9c-2d3e-4a5b-8c7d-9e0f1a2b3c4d",
    gameId: "reachability.fixture",
    title: "Reachability fixture",
    contentRevision: "0.1.0",
    classes: [
      {
        id: "scout",
        name: "Scout",
        baseStats: { health: 10, movement: 2, strength: 3, defense: 0 }
      },
      {
        id: "statue",
        name: "Statue",
        baseStats: { health: 10, movement: 0, strength: 1, defense: 0 }
      },
      {
        id: "archer",
        name: "Archer",
        baseStats: { health: 8, movement: 1, strength: 2, defense: 0 }
      }
    ],
    abilities: [],
    unitTypes: [
      { id: "blue_scout", name: "Blue Scout", classId: "scout" },
      { id: "blue_statue", name: "Blue Statue", classId: "statue" },
      { id: "red_wall", name: "Red Wall", classId: "statue" },
      {
        id: "red_archer",
        name: "Red Archer",
        classId: "archer",
        startingWeaponIds: ["long_bow"]
      }
    ],
    weapons: [
      {
        id: "long_bow",
        name: "Long bow",
        power: 2,
        minimumRange: 2,
        maximumRange: 3
      }
    ],
    items: [],
    maps: [{
      id: "keep",
      name: "Keep",
      width: 7,
      height: 5,
      terrain: Array(35).fill("grass")
    }],
    campaigns: [{
      id: "siege",
      name: "Siege",
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "assault",
        nodes: [
          {
            id: "assault",
            name: "The assault",
            kind: "encounter",
            mapId: "keep",
            placements: [
              { id: "scout", unitTypeId: "blue_scout", side: "first", x: 0, y: 2 },
              { id: "statue", unitTypeId: "blue_statue", side: "first", x: 6, y: 4 },
              { id: "wall_north", unitTypeId: "red_wall", side: "second", x: 1, y: 1 },
              { id: "wall_middle", unitTypeId: "red_wall", side: "second", x: 1, y: 2 },
              { id: "wall_south", unitTypeId: "red_wall", side: "second", x: 1, y: 3 },
              { id: "archer", unitTypeId: "red_archer", side: "second", x: 5, y: 2 }
            ],
            transitions: [{ id: "done", targetNodeId: "over", priority: 0 }]
          },
          { id: "over", name: "It is over", kind: "terminal", transitions: [] }
        ]
      }
    }]
  };
}

// A five-by-three valley split by a river down the middle column, with a ford
// along the bottom row. The walker and the heron start side by side on the
// west bank, so the only difference between what they can reach is what they
// can cross.
function riverProject(): SourceProject {
  return {
    schemaVersion: "1.2.0",
    packageId: "2b6c1e0d-3f4a-4b6c-9d8e-0f1a2b3c4d5e",
    gameId: "passability.fixture",
    title: "Passability fixture",
    contentRevision: "0.1.0",
    classes: [
      {
        id: "walker",
        name: "Walker",
        baseStats: { health: 10, movement: 2, strength: 3, defense: 0 }
      },
      {
        id: "heron",
        name: "Heron Rider",
        baseStats: { health: 10, movement: 2, strength: 3, defense: 0 },
        traversal: { flying: true }
      },
      {
        id: "wader",
        name: "Wader",
        baseStats: { health: 10, movement: 3, strength: 3, defense: 0 },
        traversal: { crossings: ["water"] }
      }
    ],
    abilities: [],
    unitTypes: [
      { id: "blue_walker", name: "Blue Walker", classId: "walker" },
      { id: "blue_heron", name: "Blue Heron", classId: "heron" },
      { id: "blue_wader", name: "Blue Wader", classId: "wader" },
      { id: "red_watch", name: "Red Watch", classId: "walker" }
    ],
    weapons: [],
    items: [],
    maps: [{
      id: "valley",
      name: "Valley",
      width: 5,
      height: 3,
      terrain: [
        "grass", "grass", "river", "grass", "grass",
        "grass", "grass", "river", "grass", "grass",
        "grass", "grass", "road", "grass", "grass"
      ]
    }],
    campaigns: [{
      id: "crossing",
      name: "Crossing",
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "cross",
        nodes: [
          {
            id: "cross",
            name: "The crossing",
            kind: "encounter",
            mapId: "valley",
            placements: [
              { id: "walker", unitTypeId: "blue_walker", side: "first", x: 1, y: 1 },
              { id: "heron", unitTypeId: "blue_heron", side: "first", x: 1, y: 0 },
              { id: "wader", unitTypeId: "blue_wader", side: "first", x: 0, y: 2 },
              { id: "watch", unitTypeId: "red_watch", side: "second", x: 4, y: 0 }
            ],
            transitions: [{ id: "done", targetNodeId: "over", priority: 0 }]
          },
          { id: "over", name: "It is over", kind: "terminal", transitions: [] }
        ]
      }
    }]
  };
}

function started(project: SourceProject): PlaytestState {
  const start = startPlaytest(project);
  expect(start.error).toBeUndefined();
  return start.state!;
}

/**
 * Every tile the engine would actually accept a move to, found by asking it.
 * Each candidate is tried on a fresh battle, because an accepted move changes
 * the board the next candidate would be judged against.
 */
function acceptedDestinations(
  project: SourceProject,
  unitId: string
): string[] {
  const probe = started(project);
  const { width, height } = probe;
  const engineId = probe.unitIds.get(unitId)!;
  endPlaytest(probe);

  const accepted: string[] = [];
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const attempt = started(project);
      const result = attempt.encounter.apply({
        type: "move",
        unitId: engineId,
        destination: { x, y }
      });
      if (result.error === "none") accepted.push(`${x}:${y}`);
      endPlaytest(attempt);
    }
  }
  return accepted;
}

const keys = (tiles: readonly (readonly [number, number])[]) =>
  tiles.map(([x, y]) => `${x}:${y}`);

describe("the board's movement range and the engine's move rule", () => {
  it("offers exactly the destinations the engine accepts", () => {
    const project = keepProject();
    const state = started(project);
    // Row-major, tile for tile, order included: the same list the console
    // shows for the same battle. Guarded against agreeing on nothing.
    const offered = keys(legalMoves(state, "scout"));
    expect(offered.length).toBeGreaterThan(0);
    expect(offered).toEqual(acceptedDestinations(project, "scout"));
    endPlaytest(state);
  });

  it("stops at a wall of opponents rather than counting steps", () => {
    const project = keepProject();
    const state = started(project);
    // Two steps of allowance would otherwise reach the third column; three
    // opponents in column one leave only the scout's own column open.
    expect(keys(legalMoves(state, "scout")))
      .toEqual(["0:0", "0:1", "0:3", "0:4"]);
    endPlaytest(state);
  });

  it("offers nothing to a character that cannot move", () => {
    const project = keepProject();
    const state = started(project);
    expect(legalMoves(state, "statue")).toEqual([]);
    expect(acceptedDestinations(project, "statue")).toEqual([]);
    endPlaytest(state);
  });

  it("offers nothing while it is not this character's side's turn", () => {
    const project = keepProject();
    const state = started(project);
    expect(canAct(state, "archer")).toBe(false);
    // The engine's query answers for any living unit; the board withholds the
    // offer because the character may not be given orders, not because the
    // tiles are illegal.
    expect(state.encounter.reachableTiles(state.unitIds.get("archer")!).length)
      .toBeGreaterThan(0);
    expect(legalMoves(state, "archer")).toEqual([]);
    endPlaytest(state);
  });

  it("offers nothing for a name no character answers to", () => {
    const project = keepProject();
    const state = started(project);
    expect(legalMoves(state, "nobody")).toEqual([]);
    endPlaytest(state);
  });
});

describe("what the ground allows", () => {
  it("keeps a walker out of the river and offers it the ford", () => {
    const project = riverProject();
    const state = started(project);
    const offered = keys(legalMoves(state, "walker"));
    expect(offered).toEqual(acceptedDestinations(project, "walker"));
    expect(offered).not.toContain("2:1");
    expect(offered).not.toContain("2:0");
    expect(offered).toContain("2:2");
    endPlaytest(state);
  });

  it("lets a flier stand over the water and reach the far bank", () => {
    const project = riverProject();
    const state = started(project);
    const offered = keys(legalMoves(state, "heron"));
    expect(offered).toEqual(acceptedDestinations(project, "heron"));
    expect(offered).toContain("2:0");
    expect(offered).toContain("3:0");
    endPlaytest(state);
  });

  it("lets a character authored to cross water do exactly that and no more", () => {
    const project = riverProject();
    const state = started(project);
    const offered = keys(legalMoves(state, "wader"));
    expect(offered).toEqual(acceptedDestinations(project, "wader"));
    // Over the ford and up into the river, which it alone among the walkers
    // may enter, and it is still a walker everywhere else.
    expect(offered).toContain("2:1");
    expect(offered).toEqual(acceptedDestinations(project, "wader"));
    endPlaytest(state);
  });

  it("narrows the danger zone by what the enemy cannot cross", () => {
    const project = riverProject();
    const state = started(project);
    const threatened = new Set(keys(dangerTiles(state, "second")));
    // The watch stands on the east bank with one action point: it threatens
    // the tiles beside it and nothing across the water.
    expect(threatened.has("3:0")).toBe(true);
    expect(threatened.has("1:0")).toBe(false);
    endPlaytest(state);
  });
});

describe("the board's danger zone and the engine's danger query", () => {
  it("carries the engine's tiles through unchanged", () => {
    const project = keepProject();
    const state = started(project);
    expect(keys(dangerTiles(state, "second"))).toEqual(
      state.encounter.dangerTiles("second").map((tile) => `${tile.x}:${tile.y}`)
    );
    endPlaytest(state);
  });

  it("leaves the archer's minimum-reach hole unmarked", () => {
    const project = keepProject();
    const state = started(project);
    const threatened = new Set(keys(dangerTiles(state, "second")));
    // The bow strikes from two to three tiles and the archer has one step, so
    // no stance it can take puts its own square far enough away.
    expect(threatened.has("5:2")).toBe(false);
    // Two tiles west is inside the band from where it already stands.
    expect(threatened.has("3:2")).toBe(true);
    // Two tiles north likewise, and nothing else on the board reaches there.
    expect(threatened.has("5:0")).toBe(true);
    // The far north-west corner is outside every reach the enemy has.
    expect(threatened.has("0:0")).toBe(false);
    endPlaytest(state);
  });

  it("counts the motionless walls' own reach as well as the archer's", () => {
    const project = keepProject();
    const state = started(project);
    const threatened = new Set(keys(dangerTiles(state, "second")));
    // A wall that cannot take a step still threatens the tiles beside it,
    // including the square the scout is standing on.
    expect(threatened.has("1:0")).toBe(true);
    expect(threatened.has("0:2")).toBe(true);
    expect(threatened.has("0:4")).toBe(false);
    endPlaytest(state);
  });

  it("does not change with whose turn it is", () => {
    const project = keepProject();
    const state = started(project);
    expect(dangerTiles(state, "first").length).toBeGreaterThan(0);
    const before = keys(dangerTiles(state, "second"));
    // Under alternating turns a side's turn is one activation by whichever
    // character its player picks, so no red character is ever spent and the
    // warning is the union over all of them however the turn passes.
    expect(waitUnit(project, state, "scout")).toBe(true);
    expect(keys(dangerTiles(state, "second"))).toEqual(before);
    endPlaytest(state);
  });
});
