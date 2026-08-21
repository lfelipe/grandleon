// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  SIDE_FACTIONS,
  characterSide,
  characterStanding,
  sideFaction,
  standingSentence
} from "./character-standing";
import type {
  CampaignNode,
  EncounterPlacement,
  SourceProject
} from "../generated/source-v1";

function placement(
  id: string,
  unitTypeId: string,
  extra: Partial<EncounterPlacement> = {}
): EncounterPlacement {
  return { id, unitTypeId, side: "second", x: 0, y: 0, ...extra };
}

function stage(id: string, placements: EncounterPlacement[]): CampaignNode {
  return {
    id,
    name: id,
    kind: "encounter",
    mapId: "field",
    placements,
    transitions: []
  };
}

function project(patch: Partial<SourceProject> = {}): SourceProject {
  return {
    schemaVersion: "1.2.0",
    packageId: "5f0d7b0e-8f2b-4a58-9d0f-1c2b3a4d5e6f",
    gameId: "standing.test",
    title: "Standing Test",
    contentRevision: "1.0.0",
    classes: [],
    unitTypes: [],
    weapons: [],
    items: [],
    maps: [],
    ...patch
  };
}

describe("characterStanding", () => {
  it("reports a character nothing places as unused", () => {
    expect(characterStanding(project(), "rina")).toEqual({
      kind: "unused",
      placements: 0,
      boards: 0,
      reasons: []
    });
  });

  it("counts several placements of one character on one board as one character", () => {
    const standing = characterStanding(
      project({
        campaigns: [{
          id: "war",
          name: "War",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "one",
            nodes: [stage("one", [
              placement("a", "bandit"),
              placement("b", "bandit"),
              placement("c", "bandit")
            ])]
          }
        }]
      }),
      "bandit"
    );
    expect(standing.kind).toBe("extra");
    expect(standing.placements).toBe(3);
    expect(standing.boards).toBe(1);
    expect(standing.reasons).toEqual([]);
  });

  it("names a character a campaign's company keeps", () => {
    const standing = characterStanding(
      project({
        campaigns: [{
          id: "war",
          name: "War",
          roster: [{ id: "member", name: "Rina", unitTypeId: "rina" }]
        }]
      }),
      "rina"
    );
    expect(standing.kind).toBe("named");
    expect(standing.reasons).toEqual(["company"]);
    expect(standing.boards).toBe(0);
  });

  it("names a character who joins along the way", () => {
    const standing = characterStanding(
      project({
        campaigns: [{
          id: "war",
          name: "War",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "one",
            nodes: [{
              id: "one",
              name: "one",
              kind: "story",
              transitions: [],
              recruits: [{ id: "bors", name: "Bors", unitTypeId: "bors" }]
            }]
          }
        }]
      }),
      "bors"
    );
    expect(standing.reasons).toEqual(["recruit"]);
  });

  it("names a character a Stage is won or lost over", () => {
    const standing = characterStanding(
      project({
        objectives: [{
          id: "kill",
          name: "Kill the captain",
          kind: "defeatTarget",
          targetPlacementId: "captain"
        }],
        campaigns: [{
          id: "war",
          name: "War",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "one",
            nodes: [stage("one", [placement("captain", "captain")])]
          }
        }]
      }),
      "captain"
    );
    expect(standing.kind).toBe("named");
    expect(standing.reasons).toEqual(["objective"]);
  });

  it("names a character somebody can talk to", () => {
    const standing = characterStanding(
      project({
        campaigns: [{
          id: "war",
          name: "War",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "one",
            nodes: [stage("one", [
              placement("stranger", "stranger", { talk: { flagId: "spoke" } })
            ])]
          }
        }]
      }),
      "stranger"
    );
    expect(standing.reasons).toEqual(["talk"]);
  });

  it("names a character a placement fields as a member of the company", () => {
    const standing = characterStanding(
      project({
        campaigns: [{
          id: "war",
          name: "War",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "one",
            nodes: [stage("one", [
              placement("rina", "rina", { side: "first", memberId: "member" })
            ])]
          }
        }]
      }),
      "rina"
    );
    expect(standing.reasons).toEqual(["company"]);
    expect(standing.boards).toBe(1);
  });

  it("reports every reason that applies, in a stable order", () => {
    const standing = characterStanding(
      project({
        objectives: [{
          id: "guard",
          name: "Guard",
          kind: "protectTarget",
          targetPlacementId: "rina"
        }],
        campaigns: [{
          id: "war",
          name: "War",
          roster: [{ id: "member", name: "Rina", unitTypeId: "rina" }],
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "one",
            nodes: [stage("one", [
              placement("rina", "rina", {
                side: "first",
                memberId: "member",
                talk: { flagId: "spoke" }
              })
            ])]
          }
        }]
      }),
      "rina"
    );
    expect(standing.reasons).toEqual(["company", "objective", "talk"]);
  });

  it("counts boards rather than placements", () => {
    const standing = characterStanding(
      project({
        campaigns: [{
          id: "war",
          name: "War",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "one",
            nodes: [
              stage("one", [placement("a", "wolf"), placement("b", "wolf")]),
              stage("two", [placement("c", "wolf")])
            ]
          }
        }]
      }),
      "wolf"
    );
    expect(standing.boards).toBe(2);
    expect(standing.placements).toBe(3);
  });
});

describe("standingSentence", () => {
  it("says nothing depends on an extra, and how many there are", () => {
    expect(standingSentence("Bandit", {
      kind: "extra",
      placements: 3,
      boards: 1,
      reasons: []
    })).toBe(
      "Bandit stands in one Stage, 3 times in all, and nothing depends on " +
      "which of them is which."
    );
  });

  it("says why a named character is named", () => {
    expect(standingSentence("Rina", {
      kind: "named",
      placements: 1,
      boards: 1,
      reasons: ["company", "talk"]
    })).toBe(
      "Rina marches with a campaign's company, is somebody a character can " +
      "talk to, and stands in one Stage."
    );
  });

  it("reads as a sentence however many reasons there are", () => {
    // Loading the Tarnholt sample is what catches this. A reason cannot be a
    // bare noun after "is", or the sentence reads "Ashen Levy is somebody can
    // talk to them" and "Warden Kesh is somebody a Stage is won or lost over".
    expect(standingSentence("Kesh", {
      kind: "named", placements: 1, boards: 1, reasons: ["objective"]
    })).toBe(
      "Kesh is somebody a Stage is won or lost over, and stands in one Stage."
    );
    expect(standingSentence("Mirea", {
      kind: "named",
      placements: 5,
      boards: 5,
      reasons: ["company", "recruit", "objective"]
    })).toBe(
      "Mirea marches with a campaign's company, joins the company along the " +
      "way, is somebody a Stage is won or lost over, and stands in 5 Stages."
    );
    expect(standingSentence("Vorne", {
      kind: "named", placements: 0, boards: 0, reasons: ["company"]
    })).toBe(
      "Vorne marches with a campaign's company, and no Stage places them yet."
    );
  });

  it("says a character is in no Stage yet", () => {
    expect(standingSentence("Bors", {
      kind: "unused",
      placements: 0,
      boards: 0,
      reasons: []
    })).toBe("Bors is not in any Stage yet.");
  });
});

describe("sides", () => {
  it("offers exactly two sides, in the colours the board already uses", () => {
    expect(SIDE_FACTIONS.map((faction) => [faction.id, faction.color])).toEqual([
      ["your_side", "blue"],
      ["the_enemy", "red"]
    ]);
  });

  it("recognises its own two factions and nothing else", () => {
    expect(sideFaction("your_side")?.label).toBe("One of yours");
    expect(sideFaction("the_enemy")?.label).toBe("An enemy");
    expect(sideFaction("ashen_legion")).toBeUndefined();
    expect(sideFaction(undefined)).toBeUndefined();
  });

  it("names a faction of the author's own rather than overruling it", () => {
    const authored = project({
      factions: [{ id: "ashen_legion", name: "The Ashen Legion" }]
    });
    expect(characterSide(authored, "ashen_legion")).toBe("The Ashen Legion");
  });

  it("says a character on no side is on no side", () => {
    expect(characterSide(project(), undefined)).toBe("Not on a side yet");
  });

  it("falls back to the identifier for a faction that is not there", () => {
    expect(characterSide(project(), "gone")).toBe("gone");
  });
});
