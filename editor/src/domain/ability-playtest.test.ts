// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import {
  abilitiesFor,
  castAbility,
  endPlaytest,
  legalCastTiles,
  startPlaytest,
  takeAutomaticTurn,
  type PlaytestState
} from "./playtest-session";

// A one-row board with a caster who knows two abilities, so the browser's
// casting surface can be checked against the engine rather than against a
// screenshot: what is offered, where it may be aimed, and what the engine
// does with it.
function castingProject(): SourceProject {
  return {
    schemaVersion: "1.1.0",
    packageId: "6f7ad2d1-40cb-4a15-84a4-cbb1d2a26e6d",
    gameId: "ability.fixture",
    title: "Ability fixture",
    contentRevision: "0.1.0",
    classes: [
      {
        id: "caster",
        name: "Caster",
        baseStats: { health: 10, movement: 1, strength: 1, defense: 0 }
      },
      {
        id: "brute",
        name: "Brute",
        baseStats: { health: 8, movement: 1, strength: 1, defense: 0 }
      }
    ],
    abilities: [
      {
        id: "spark",
        name: "Spark",
        kind: "damage",
        power: 5,
        minimumRange: 1,
        maximumRange: 2
      },
      {
        id: "mend",
        name: "Mend",
        kind: "restore",
        power: 4,
        minimumRange: 1,
        maximumRange: 1
      }
    ],
    unitTypes: [
      {
        id: "blue_caster",
        name: "Blue Caster",
        classId: "caster",
        abilityIds: ["spark", "mend"]
      },
      { id: "red_brute", name: "Red Brute", classId: "brute" }
    ],
    weapons: [],
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
              { id: "caster_one", unitTypeId: "blue_caster", side: "first", x: 0, y: 0 },
              { id: "brute_one", unitTypeId: "red_brute", side: "second", x: 2, y: 0 }
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

describe("casting in the browser", () => {
  it("offers the abilities the character's type lists, in that order", () => {
    const project = castingProject();
    const state = started(project);
    expect(abilitiesFor(project, state, "caster_one").map((one) => one.name))
      .toEqual(["Spark", "Mend"]);
    expect(abilitiesFor(project, state, "brute_one")).toEqual([]);
    endPlaytest(state);
  });

  it("drops an ability identity the project does not define", () => {
    // The engine refuses a battle whose unit knows an undefined ability, so
    // this state cannot arise from a valid project, but the surface must
    // still never offer a command the engine would refuse.
    const project = castingProject();
    const state = started(project);
    state.units.find((unit) => unit.id === "caster_one")!.abilityIds =
      ["spark", "phantom"];
    expect(abilitiesFor(project, state, "caster_one").map((one) => one.id))
      .toEqual(["spark"]);
    endPlaytest(state);
  });

  it("aims at tiles inside the ability's own band, occupied or not", () => {
    const project = castingProject();
    const state = started(project);
    // The caster stands on (0,0); Spark reaches one to two tiles.
    expect(legalCastTiles(project, state, "caster_one", "spark"))
      .toEqual([[1, 0], [2, 0]]);
    // Mend reaches exactly one, and its own tile is outside that band.
    expect(legalCastTiles(project, state, "caster_one", "mend"))
      .toEqual([[1, 0]]);
    endPlaytest(state);
  });

  it("refuses to aim an ability the character does not know", () => {
    const project = castingProject();
    const state = started(project);
    expect(legalCastTiles(project, state, "brute_one", "spark")).toEqual([]);
    expect(castAbility(project, state, "brute_one", "spark", 1, 0)).toBe(false);
    endPlaytest(state);
  });

  it("casts through the engine and reports what the engine did", () => {
    const project = castingProject();
    const state = started(project);
    expect(castAbility(project, state, "caster_one", "spark", 2, 0)).toBe(true);
    const target = state.units.find((unit) => unit.id === "brute_one")!;
    expect(target.health).toBe(3);
    expect(state.events[1]).toContain("Blue Caster dealt 5 damage to Red Brute");
    // Striking ends the activation, so the turn is the other side's.
    expect(state.activeSide).toBe("second");
    endPlaytest(state);
  });

  it("will not cast at a tile outside the band", () => {
    const project = castingProject();
    const state = started(project);
    expect(castAbility(project, state, "caster_one", "spark", 4, 0)).toBe(false);
    expect(state.units.find((unit) => unit.id === "brute_one")!.health).toBe(8);
    endPlaytest(state);
  });

  it("offers nothing while the other side is acting", () => {
    const project = castingProject();
    const state = started(project);
    expect(castAbility(project, state, "caster_one", "spark", 2, 0)).toBe(true);
    expect(abilitiesFor(project, state, "caster_one")).toEqual([]);
    endPlaytest(state);
  });

  it("lets the unattended side cast the same abilities the player can", () => {
    // The engine's own policy chooses red's command. Spark on the caster is
    // worth five against a strike worth one, so red casts rather than swings.
    const project = castingProject();
    project.unitTypes[1]!.abilityIds = ["spark"];
    const state = started(project);
    expect(castAbility(project, state, "caster_one", "spark", 2, 0)).toBe(true);
    expect(takeAutomaticTurn(project, state, "second")).toBe(true);
    expect(state.units.find((unit) => unit.id === "caster_one")!.health).toBe(5);
    expect(state.events[1]).toContain("Red Brute dealt 5 damage to Blue Caster");
    endPlaytest(state);
  });
});
