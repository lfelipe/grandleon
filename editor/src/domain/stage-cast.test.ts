// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import { SourceProjectSession } from "./source-project-session";
import { planCharacterOnBoard } from "./stage-cast";

/** A game with ground, a campaign and a Stage on it, and nobody in it. */
function fixture(): SourceProject {
  return {
    schemaVersion: "1.2.0",
    packageId: "123e4567-e89b-12d3-a456-426614174000",
    gameId: "demo",
    title: "Demo",
    contentRevision: "1.0.0",
    classes: [],
    unitTypes: [],
    weapons: [],
    items: [],
    maps: [{
      id: "meadow",
      name: "Meadow",
      width: 4,
      height: 4,
      terrain: Array.from({ length: 16 }, () => "grass")
    }],
    campaigns: [{
      id: "march",
      name: "The march",
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "field",
        nodes: [
          {
            id: "field",
            name: "The field",
            kind: "encounter",
            mapId: "meadow",
            transitions: [{ id: "next", targetNodeId: "done", priority: 0 }]
          },
          { id: "done", name: "After", kind: "terminal", transitions: [] }
        ]
      }
    }]
  };
}

describe("planCharacterOnBoard", () => {
  it("makes a character nobody authored and stands them on the tile", () => {
    const project = fixture();
    const plan = planCharacterOnBoard(project, {
      campaignId: "march",
      nodeId: "field",
      role: "rogue",
      setting: "medieval",
      name: "Bandit",
      side: "second",
      x: 2,
      y: 1
    });
    expect(plan.kind).toBe("cast");
    if (plan.kind !== "cast") return;

    // Nothing is written by the plan itself: the project it was asked about is
    // untouched, which is what makes a refusal total.
    expect(project.unitTypes).toEqual([]);

    const session = new SourceProjectSession(project);
    const transaction = session.transact("Put Bandit on the board", plan.edits);
    const after = session.snapshot();

    // The whole chain, and the board, from one press.
    expect(after.unitTypes.map((unitType) => unitType.name)).toEqual(["Bandit"]);
    expect(after.classes).toHaveLength(1);
    expect(after.weapons).toHaveLength(1);
    expect(after.weaponTypes).toHaveLength(1);
    const placements = after.campaigns?.[0]?.flow?.nodes[0]?.placements;
    expect(placements).toEqual([{
      id: "unit",
      unitTypeId: plan.unitTypeId,
      side: "second",
      x: 2,
      y: 1
    }]);
    // Nobody joined the company: the opposing side never fields it.
    expect(after.campaigns?.[0]?.roster).toBeUndefined();

    // One act. Undoing it takes the character, the class, the weapon and the
    // placement away together, and there is nothing left underneath.
    expect(transaction.label).toBe("Put Bandit on the board");
    session.undo();
    const back = session.snapshot();
    expect(back.unitTypes).toEqual([]);
    expect(back.classes).toEqual([]);
    expect(back.weapons).toEqual([]);
    expect(back.campaigns?.[0]?.flow?.nodes[0]?.placements).toBeUndefined();
    expect(session.canUndo()).toBe(false);
  });

  it("makes the side a fact about the character rather than about the press", () => {
    const project = fixture();
    const plan = planCharacterOnBoard(project, {
      campaignId: "march",
      nodeId: "field",
      role: "knight",
      setting: "medieval",
      name: "",
      side: "second",
      x: 0,
      y: 0
    });
    if (plan.kind !== "cast") throw new Error(plan.reason);
    const session = new SourceProjectSession(project);
    session.transact("Put a knight on the board", plan.edits);
    const after = session.snapshot();

    // The faction is an ordinary record, made on first use, and the character
    // wears it, so the next board that picks them up already knows whose side
    // they are on and never asks again.
    expect(after.factions).toEqual([
      { id: "the_enemy", name: "The enemy", color: "red" }
    ]);
    expect(after.unitTypes[0]?.factionId).toBe("the_enemy");
    // An empty name takes the catalogue's own word for the role.
    expect(after.unitTypes[0]?.name).toBe("Knight");
  });

  it("puts somebody on the player's own side into the company", () => {
    const project = fixture();
    const plan = planCharacterOnBoard(project, {
      campaignId: "march",
      nodeId: "field",
      role: "healer",
      setting: "medieval",
      name: "Nel",
      side: "first",
      x: 1,
      y: 1
    });
    if (plan.kind !== "cast") throw new Error(plan.reason);
    const session = new SourceProjectSession(project);
    session.transact("Put Nel on the board", plan.edits);
    const after = session.snapshot();

    // Your side is fought by the company, so a placement there names one of
    // them, and the company gaining somebody happens in the same act.
    const roster = after.campaigns?.[0]?.roster;
    expect(roster).toEqual([{ id: "nel", name: "Nel", unitTypeId: "nel" }]);
    expect(after.campaigns?.[0]?.flow?.nodes[0]?.placements?.[0]?.memberId)
      .toBe("nel");
    expect(after.factions?.[0]?.id).toBe("your_side");

    session.undo();
    expect(session.snapshot().campaigns?.[0]?.roster).toBeUndefined();
  });

  it("shares the class and the weapon type a second character can join", () => {
    const project = fixture();
    const first = planCharacterOnBoard(project, {
      campaignId: "march",
      nodeId: "field",
      role: "rogue",
      setting: "medieval",
      name: "Bandit",
      side: "second",
      x: 0,
      y: 0
    });
    if (first.kind !== "cast") throw new Error(first.reason);
    const session = new SourceProjectSession(project);
    session.transact("Put Bandit on the board", first.edits);

    const second = planCharacterOnBoard(session.snapshot(), {
      campaignId: "march",
      nodeId: "field",
      role: "rogue",
      setting: "medieval",
      name: "Cutthroat",
      side: "second",
      x: 1,
      y: 0
    });
    if (second.kind !== "cast") throw new Error(second.reason);
    session.transact("Put Cutthroat on the board", second.edits);
    const after = session.snapshot();

    // A class is an archetype and two rogues are one of it, so the second
    // character joins rather than minting a copy. The same rule shares the
    // weapon type, without which the second bandit's dagger would be one no
    // existing rogue could hold. The faction is not made twice either.
    expect(after.classes).toHaveLength(1);
    expect(after.weaponTypes).toHaveLength(1);
    expect(after.factions).toHaveLength(1);
    expect(after.weapons).toHaveLength(2);
    expect(after.unitTypes.map((unitType) => unitType.name))
      .toEqual(["Bandit", "Cutthroat"]);
    expect(after.campaigns?.[0]?.flow?.nodes[0]?.placements)
      .toHaveLength(2);
  });

  it("refuses a tile somebody already holds, and writes nothing", () => {
    const project = fixture();
    project.campaigns![0]!.flow!.nodes[0]!.placements = [
      { id: "unit", unitTypeId: "ghost", side: "second", x: 2, y: 2 }
    ];
    const plan = planCharacterOnBoard(project, {
      campaignId: "march",
      nodeId: "field",
      role: "mage",
      setting: "medieval",
      name: "Hexer",
      side: "second",
      x: 2,
      y: 2
    });
    expect(plan).toEqual({
      kind: "refused",
      reason:
        "Somebody already stands on column 3, row 3. Choose an empty tile."
    });
  });

  it("refuses a Stage that is no longer in the campaign", () => {
    const plan = planCharacterOnBoard(fixture(), {
      campaignId: "march",
      nodeId: "gone",
      role: "mage",
      setting: "medieval",
      name: "",
      side: "second",
      x: 0,
      y: 0
    });
    expect(plan.kind).toBe("refused");
    if (plan.kind !== "refused") return;
    expect(plan.reason).toContain("no longer in The march");
  });
});
