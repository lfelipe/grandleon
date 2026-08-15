// SPDX-License-Identifier: MIT
// Free selection under `sideBlocks`, played through the browser's own session
// against the real engine.
//
// Three rules are pinned down here, and each one answers a way a cartridge can
// read as stopped in the player's hands:
//
//   * the player picks which of the line acts, rather than pressing A on each
//     of them until one is not refused;
//   * a character that has been spent is not selectable again;
//   * two action points buy one walk and one action, not two walks.
//
// The board below states `sideBlocks`, which is what the sample campaigns
// state at project level, and it gives one side three characters so the order
// they act in is a real choice rather than a formality.

import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import {
  attackUnit,
  canAct,
  endPlaytest,
  legalMoves,
  moveUnit,
  pointsLeft,
  startPlaytest,
  waitUnit
} from "./playtest-session";

function lineProject(): SourceProject {
  return {
    schemaVersion: "1.0.0",
    packageId: "7c1d2e3f-4a5b-4c6d-8e9f-0a1b2c3d4e5f",
    gameId: "free.selection.fixture",
    title: "Free selection fixture",
    contentRevision: "0.1.0",
    // Two points and three steps: the two-point turn the action-point
    // vocabulary describes, and the shape a second walk would fit into.
    defaultTurnOrder: "sideBlocks",
    classes: [
      {
        id: "guard",
        name: "Guard",
        baseStats: {
          health: 10,
          movement: 3,
          strength: 3,
          defense: 0,
          actionPoints: 2,
          speed: 1
        }
      },
      {
        id: "runner",
        name: "Runner",
        baseStats: {
          health: 10,
          movement: 3,
          strength: 3,
          defense: 0,
          actionPoints: 2,
          // Faster, so if the engine named one character for the side this is
          // the one it would name, and the other two would be refused.
          speed: 9
        }
      }
    ],
    abilities: [],
    unitTypes: [
      { id: "blue_guard", name: "Shield of the Line", classId: "guard" },
      { id: "blue_runner", name: "Runner of the Line", classId: "runner" },
      { id: "red_watch", name: "Coil Watch", classId: "guard" }
    ],
    weapons: [],
    items: [],
    maps: [{
      id: "field",
      name: "Field",
      width: 9,
      height: 5,
      terrain: Array(45).fill("grass")
    }],
    campaigns: [{
      id: "line",
      name: "The Line",
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "opening",
        nodes: [
          {
            id: "opening",
            name: "The opening",
            kind: "encounter",
            mapId: "field",
            placements: [
              { id: "slow_one", unitTypeId: "blue_guard", side: "first", x: 0, y: 0 },
              { id: "slow_two", unitTypeId: "blue_guard", side: "first", x: 0, y: 2 },
              { id: "quick", unitTypeId: "blue_runner", side: "first", x: 0, y: 4 },
              { id: "watch", unitTypeId: "red_watch", side: "second", x: 8, y: 2 },
              // Close enough to the line that a character can walk to 2,0 and
              // then swing, which is the sequence at issue: move one, move a
              // second, come back and strike with the first.
              { id: "picket", unitTypeId: "red_watch", side: "second", x: 2, y: 1 }
            ],
            transitions: [{ id: "done", targetNodeId: "over", priority: 0 }]
          },
          { id: "over", name: "It is over", kind: "terminal", transitions: [] }
        ]
      }
    }]
  };
}

describe("a side's block, taken in the player's own order", () => {
  it("offers every character of the block and names none of them", () => {
    const project = lineProject();
    const state = startPlaytest(project).state!;

    // The block is the first side's and the engine has named nobody:
    // `activeUnitId` is empty rather than the fastest character's identity, so
    // every character of the line answers true below.
    expect(state.activeSide).toBe("first");
    expect(state.activeUnitId).toBe("");
    expect(canAct(state, "slow_one")).toBe(true);
    expect(canAct(state, "slow_two")).toBe(true);
    expect(canAct(state, "quick")).toBe(true);
    // And not the other side's, which is what the block still means.
    expect(canAct(state, "watch")).toBe(false);

    endPlaytest(state);
  });

  it("lets the line act in any order and greys each one as it goes", () => {
    const project = lineProject();
    const state = startPlaytest(project).state!;

    // Deliberately not the fastest, and deliberately not the first placement:
    // the middle of the line, which is the pick a rule that names one
    // character for the side would refuse.
    expect(waitUnit(project, state, "slow_two")).toBe(true);
    // Spent: still on the board, still there to look at, and not selectable.
    expect(state.units.find((unit) => unit.id === "slow_two")!.hasActed).toBe(
      true
    );
    expect(canAct(state, "slow_two")).toBe(false);
    expect(waitUnit(project, state, "slow_two")).toBe(false);
    // The block is still open and still the first side's.
    expect(state.activeSide).toBe("first");
    expect(state.activeUnitId).toBe("");
    expect(canAct(state, "slow_one")).toBe(true);
    expect(canAct(state, "quick")).toBe(true);

    // Now the fastest, then the first: three characters, three orders, and the
    // block ends only when all of them have gone.
    expect(waitUnit(project, state, "quick")).toBe(true);
    expect(state.activeSide).toBe("first");
    expect(waitUnit(project, state, "slow_one")).toBe(true);
    expect(state.activeSide).toBe("second");

    endPlaytest(state);
  });

  it("gives a character one walk and keeps the other point for acting", () => {
    const project = lineProject();
    const state = startPlaytest(project).state!;

    expect(legalMoves(state, "slow_one").length).toBeGreaterThan(0);
    expect(moveUnit(project, state, "slow_one", 2, 0)).toBe(true);
    // The side holds nothing. Under `sideBlocks` there is no one activation to
    // count down, so the point that is left is the character's own.
    expect(state.activeUnitId).toBe("");
    expect(state.remainingActionPoints).toBe(0);
    expect(pointsLeft(state, "slow_one")).toBe(1);
    expect(state.units.find((unit) => unit.id === "slow_one")!.hasMoved).toBe(
      true
    );

    // The second point is not a second walk. The board offers no range for it
    // and the engine refuses the command.
    expect(legalMoves(state, "slow_one")).toEqual([]);
    expect(moveUnit(project, state, "slow_one", 3, 0)).toBe(false);
    expect(state.units.find((unit) => unit.id === "slow_one")!.x).toBe(2);

    // It still buys an action, which is what the two-point turn is for, and
    // this is what the wait command is for: a visible way to say there is
    // nothing more to do with this character.
    expect(waitUnit(project, state, "slow_one")).toBe(true);
    // Waiting finishes the character outright rather than only closing off its
    // action, so it greys out and cannot be picked up again.
    expect(state.units.find((unit) => unit.id === "slow_one")!.hasActed).toBe(
      true
    );
    expect(canAct(state, "slow_one")).toBe(false);
    // And the walk comes back with the next turn: this character's is over, so
    // the flag is clear again.
    expect(state.units.find((unit) => unit.id === "slow_one")!.hasMoved).toBe(
      false
    );
    // Meanwhile the rest of the block still has its turns in hand.
    expect(canAct(state, "slow_two")).toBe(true);
    expect(canAct(state, "quick")).toBe(true);

    endPlaytest(state);
  });

  // An activation belongs to a character and not to a side. If it belonged to
  // the side, moving one character would refuse a move to every other one
  // beside it, because the first accepted command claims the side's activation
  // and the mover still has a point in hand, which is the shape a cartridge
  // reads as a stopped board.
  it("lets one character walk, a second walk, and the first come back", () => {
    const project = lineProject();
    const state = startPlaytest(project).state!;

    expect(moveUnit(project, state, "slow_one", 2, 0)).toBe(true);
    // The command a side-wide activation would refuse `activation_in_progress`.
    expect(canAct(state, "slow_two")).toBe(true);
    expect(moveUnit(project, state, "slow_two", 2, 2)).toBe(true);
    // Both are part-way through their own turns at once, which the side-wide
    // fields could never have said.
    expect(state.units.find((unit) => unit.id === "slow_one")!.hasMoved).toBe(
      true
    );
    expect(state.units.find((unit) => unit.id === "slow_two")!.hasMoved).toBe(
      true
    );
    expect(state.activeUnitId).toBe("");

    // And the half that makes a board read as stopped: coming back to the
    // first character afterwards. Here it says it is done, which is the gesture
    // that has to stay reachable after a walk.
    expect(canAct(state, "slow_one")).toBe(true);
    expect(waitUnit(project, state, "slow_one")).toBe(true);
    expect(state.units.find((unit) => unit.id === "slow_one")!.hasActed).toBe(
      true
    );
    // The block is still the first side's, because one of its three has done
    // nothing at all yet.
    expect(state.activeSide).toBe("first");
    expect(canAct(state, "quick")).toBe(true);

    endPlaytest(state);
  });

  // Move, move, strike: the same sequence with the strike in it, on a board
  // where the third character can walk into reach of the watch and swing.
  it("lets a character that walked come back and strike", () => {
    const project = lineProject();
    const state = startPlaytest(project).state!;

    // One character walks to within a tile of the picket at 2,1.
    expect(moveUnit(project, state, "slow_one", 2, 0)).toBe(true);
    // A second walks while the first still holds its action.
    expect(moveUnit(project, state, "slow_two", 2, 2)).toBe(true);
    // And the first comes back and strikes.
    expect(canAct(state, "slow_one")).toBe(true);
    expect(attackUnit(project, state, "slow_one", "picket")).toBe(true);
    // A strike finishes the character, so it may not then walk.
    expect(state.units.find((unit) => unit.id === "slow_one")!.hasActed).toBe(
      true
    );
    expect(canAct(state, "slow_one")).toBe(false);
    expect(legalMoves(state, "slow_one")).toEqual([]);

    endPlaytest(state);
  });
});
