// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import {
  attackUnit,
  endPlaytest,
  legalTargets,
  startPlaytest,
  strikeChance,
  takeAutomaticTurn,
  weaponsFor,
  type PlaytestState
} from "./playtest-session";

// A one-row board with a duellist carrying a dagger and a bow, so the
// browser's weapon surface can be checked against the engine rather than
// against a screenshot: what is offered, who each weapon can reach, and what
// the engine does with the strike.
function carryingProject(): SourceProject {
  return {
    schemaVersion: "1.0.0",
    packageId: "0f0a5f60-2a1f-4a34-9f1c-7f43a4c15b71",
    gameId: "weapon.fixture",
    title: "Weapon fixture",
    contentRevision: "0.1.0",
    classes: [
      {
        id: "duellist",
        name: "Duellist",
        baseStats: { health: 10, movement: 1, strength: 2, defense: 0 }
      },
      {
        id: "brute",
        name: "Brute",
        baseStats: { health: 20, movement: 1, strength: 1, defense: 0 }
      }
    ],
    weapons: [
      { id: "dagger", name: "Dagger", power: 6, range: 1 },
      { id: "bow", name: "Bow", power: 1, minimumRange: 2, maximumRange: 3 }
    ],
    unitTypes: [
      {
        id: "blue_duellist",
        name: "Blue Duellist",
        classId: "duellist",
        startingWeaponIds: ["dagger", "bow"]
      },
      {
        id: "red_brute",
        name: "Red Brute",
        classId: "brute",
        startingWeaponIds: ["dagger"]
      }
    ],
    items: [],
    maps: [{
      id: "lane",
      name: "Lane",
      width: 5,
      height: 1,
      terrain: Array(5).fill("grass")
    }],
    campaigns: [{
      id: "duel",
      name: "Duel",
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "fight",
        nodes: [
          {
            id: "fight",
            name: "The duel",
            kind: "encounter",
            mapId: "lane",
            placements: [
              {
                id: "blue_one",
                unitTypeId: "blue_duellist",
                side: "first",
                x: 0,
                y: 0
              },
              {
                id: "red_one",
                unitTypeId: "red_brute",
                side: "second",
                x: 3,
                y: 0
              }
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

describe("choosing a weapon in the browser", () => {
  it("offers the weapons the character's type lists, in that order", () => {
    const project = carryingProject();
    const state = started(project);
    expect(weaponsFor(state, "blue_one").map((weapon) => weapon.id)).toEqual([
      "dagger",
      "bow"
    ]);
    expect(weaponsFor(state, "blue_one")[1]).toMatchObject({
      name: "Bow",
      power: 1,
      minimumReach: 2,
      maximumReach: 3
    });
    endPlaytest(state);
  });

  it("offers no choice to a character carrying one weapon", () => {
    const project = carryingProject();
    const state = started(project);
    // The opponent carries the dagger alone, so there is nothing to choose
    // between and a plain attack stays a single tap.
    expect(weaponsFor(state, "red_one")).toEqual([]);
    endPlaytest(state);
  });

  it("drops a weapon identity the project does not define", () => {
    const project = carryingProject();
    project.unitTypes[0]!.startingWeaponIds = ["dagger", "halberd", "bow"];
    const state = started(project);
    // The undefined identity is dropped rather than offered, and rather than
    // handed to the engine, which would refuse the whole encounter for it.
    expect(weaponsFor(state, "blue_one").map((weapon) => weapon.id)).toEqual([
      "dagger",
      "bow"
    ]);
    endPlaytest(state);
  });

  it("marks the enemies each weapon can reach, and only those", () => {
    const project = carryingProject();
    const state = started(project);
    // Three tiles apart: the dagger cannot answer, the bow can.
    expect(legalTargets(state, "blue_one", "dagger")).toEqual([]);
    expect(legalTargets(state, "blue_one", "bow")).toEqual(["red_one"]);
    // Naming nothing measures the weapon in hand, which is the dagger.
    expect(legalTargets(state, "blue_one")).toEqual([]);
    endPlaytest(state);
  });

  it("reports the chance of the strike it is offering, and only when it is not certain", () => {
    const project = carryingProject();
    // The bow can miss; the dagger always lands. Both are carried by the same
    // character, so the answer follows the weapon the player has chosen.
    project.weapons[1]!.accuracy = 85;
    const state = started(project);
    expect(strikeChance(state, "blue_one", "bow")).toBe(85);
    // The dagger cannot reach from here, and a chance for a strike that cannot
    // be made would be a number about nothing.
    expect(strikeChance(state, "blue_one", "dagger")).toBeNull();
    endPlaytest(state);
  });

  it("says nothing about a chance when every weapon always lands", () => {
    const project = carryingProject();
    const state = started(project);
    // The screen over content that cannot miss is exactly the screen it was.
    expect(strikeChance(state, "blue_one", "bow")).toBeNull();
    endPlaytest(state);
  });

  it("refuses to measure a weapon the character does not carry", () => {
    const project = carryingProject();
    const state = started(project);
    expect(legalTargets(state, "red_one", "bow")).toEqual([]);
    endPlaytest(state);
  });

  it("strikes through the engine and reports what the engine did", () => {
    const project = carryingProject();
    const state = started(project);
    expect(attackUnit(project, state, "blue_one", "red_one", "bow")).toBe(true);
    const target = state.units.find((unit) => unit.id === "red_one")!;
    // Strength 2 plus the bow's power 1, against no defence.
    expect(target.health).toBe(17);
    expect(state.events.some((line) => line.includes("3"))).toBe(true);
    endPlaytest(state);
  });

  it("will not strike with a weapon that cannot reach", () => {
    const project = carryingProject();
    const state = started(project);
    expect(attackUnit(project, state, "blue_one", "red_one", "dagger")).toBe(
      false
    );
    const target = state.units.find((unit) => unit.id === "red_one")!;
    expect(target.health).toBe(20);
    endPlaytest(state);
  });

  it("offers nothing while the other side is acting", () => {
    const project = carryingProject();
    const state = started(project);
    expect(attackUnit(project, state, "blue_one", "red_one", "bow")).toBe(true);
    expect(weaponsFor(state, "blue_one")).toEqual([]);
    endPlaytest(state);
  });

  it("lets the unattended side reach with a weapon it carries", () => {
    const project = carryingProject();
    // Give red the same pair and let it act: it should shoot rather than walk,
    // which is the same choice the console's unattended side makes.
    project.unitTypes[1]!.startingWeaponIds = ["dagger", "bow"];
    const fight = project.campaigns![0]!.flow!.nodes[0]!;
    fight.placements![1]!.behavior = "pursue";
    const state = started(project);
    expect(attackUnit(project, state, "blue_one", "red_one", "bow")).toBe(true);
    expect(takeAutomaticTurn(project, state, "second")).toBe(true);
    const actor = state.units.find((unit) => unit.id === "red_one")!;
    expect(actor.x).toBe(3);
    const struck = state.units.find((unit) => unit.id === "blue_one")!;
    expect(struck.health).toBeLessThan(struck.maximumHealth);
    endPlaytest(state);
  });
});
