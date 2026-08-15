// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import { sceneSentence, stagesInProject, stagesOnMap } from "./stages";
import { createSourceProject } from "./source-project-document";
import type { SourceProject } from "../generated/source-v1";

function project(patch: Partial<SourceProject> = {}): SourceProject {
  return {
    ...createSourceProject(),
    maps: [{
      id: "ford", name: "The Ford", width: 2, height: 2,
      terrain: ["plain", "plain", "plain", "plain"]
    }],
    ...patch
  };
}

const fought = project({
  dialogues: [
    { id: "muster", name: "The Muster" },
    { id: "aftermath", name: "What the River Took" },
    { id: "elsewhere", name: "Elsewhere" }
  ],
  objectives: [{ id: "hold", name: "Hold the ford" }],
  campaigns: [{
    id: "war",
    name: "The War",
    flow: {
      contractVersion: "1.0.0",
      entryNodeId: "crossing",
      nodes: [
        {
          id: "crossing",
          name: "The Crossing",
          kind: "encounter",
          mapId: "ford",
          dialogueIds: ["muster"],
          objectiveIds: ["hold"],
          placements: [
            { id: "a", unitTypeId: "rina", side: "first", x: 0, y: 0 },
            { id: "b", unitTypeId: "bandit", side: "second", x: 1, y: 0 },
            { id: "c", unitTypeId: "bandit", side: "second", x: 1, y: 1 }
          ],
          transitions: [
            { id: "won", targetNodeId: "after", priority: 0 },
            { id: "also", targetNodeId: "after", priority: 1 }
          ]
        },
        {
          id: "after",
          name: "After",
          kind: "story",
          dialogueIds: ["aftermath"],
          transitions: []
        },
        {
          id: "somewhere_else",
          name: "Somewhere Else",
          kind: "encounter",
          mapId: "other",
          dialogueIds: ["elsewhere"],
          transitions: []
        }
      ]
    }
  }]
});

describe("stagesOnMap", () => {
  it("finds nothing for a map nobody fights over", () => {
    expect(stagesOnMap(project(), "ford")).toEqual([]);
  });

  it("names the Stage, its campaign, and who stands on each side", () => {
    const [stage] = stagesOnMap(fought, "ford");
    expect(stage).toMatchObject({
      campaignId: "war",
      campaignName: "The War",
      nodeId: "crossing",
      nodeName: "The Crossing",
      yours: 1,
      theirs: 2
    });
  });

  it("separates what is said before the Stage from what is said after", () => {
    // Before is the encounter node's own dialogues, which both clients present
    // on arrival; after is the dialogues of whatever it leads to.
    const [stage] = stagesOnMap(fought, "ford");
    expect(stage!.saidBefore).toEqual(["The Muster"]);
    expect(stage!.saidAfter).toEqual(["What the River Took"]);
  });

  it("counts two ways to one node as one conversation", () => {
    const [stage] = stagesOnMap(fought, "ford");
    expect(stage!.saidAfter).toHaveLength(1);
    expect(stage!.after).toHaveLength(1);
  });

  it("keeps what is said after beside the node that owns it", () => {
    // A summary can read the scenes as one sentence. A surface that lets an
    // author change them cannot: there is nothing on this Stage to change,
    // because the scenes belong to the next node and are edited there. Naming
    // that node is the difference between reporting and stranding.
    const [stage] = stagesOnMap(fought, "ford");
    expect(stage!.after).toEqual([
      { nodeId: "after", nodeName: "After", scenes: ["What the River Took"] }
    ]);
  });

  it("names what winning means", () => {
    expect(stagesOnMap(fought, "ford")[0]!.winning).toEqual(["Hold the ford"]);
  });

  it("leaves out Stages fought on other ground", () => {
    expect(stagesOnMap(fought, "ford").map((stage) => stage.nodeId))
      .toEqual(["crossing"]);
    expect(stagesOnMap(fought, "other").map((stage) => stage.nodeId))
      .toEqual(["somewhere_else"]);
  });

  it("reports one map fought over by two campaigns as two Stages", () => {
    const twice = project({
      campaigns: [
        {
          id: "one", name: "One",
          flow: {
            contractVersion: "1.0.0", entryNodeId: "a",
            nodes: [{
              id: "a", name: "A", kind: "encounter", mapId: "ford",
              transitions: []
            }]
          }
        },
        {
          id: "two", name: "Two",
          flow: {
            contractVersion: "1.0.0", entryNodeId: "b",
            nodes: [{
              id: "b", name: "B", kind: "encounter", mapId: "ford",
              transitions: []
            }]
          }
        }
      ]
    });
    expect(stagesOnMap(twice, "ford").map((stage) => stage.campaignName))
      .toEqual(["One", "Two"]);
  });

  it("skips a scene the project no longer has rather than naming its id", () => {
    const dangling = project({
      campaigns: [{
        id: "war", name: "War",
        flow: {
          contractVersion: "1.0.0", entryNodeId: "a",
          nodes: [{
            id: "a", name: "A", kind: "encounter", mapId: "ford",
            dialogueIds: ["gone"], transitions: []
          }]
        }
      }]
    });
    expect(stagesOnMap(dangling, "ford")[0]!.saidBefore).toEqual([]);
  });

  it("ignores a transition that leads back to the Stage itself", () => {
    const loop = project({
      dialogues: [{ id: "muster", name: "The Muster" }],
      campaigns: [{
        id: "war", name: "War",
        flow: {
          contractVersion: "1.0.0", entryNodeId: "a",
          nodes: [{
            id: "a", name: "A", kind: "encounter", mapId: "ford",
            dialogueIds: ["muster"],
            transitions: [{ id: "again", targetNodeId: "a", priority: 0 }]
          }]
        }
      }]
    });
    expect(stagesOnMap(loop, "ford")[0]!.saidAfter).toEqual([]);
  });
});

describe("stagesInProject", () => {
  it("lists every Stage in the game, whatever ground it is on", () => {
    // The Stages section is drawn from this, so a Stage missing here is a
    // Stage an author cannot reach at all.
    expect(stagesInProject(fought).map((stage) => stage.nodeId))
      .toEqual(["crossing", "somewhere_else"]);
  });

  it("names the ground, and says so plainly when there is none", () => {
    const [crossing, elsewhere] = stagesInProject(fought);
    expect(crossing).toMatchObject({ mapId: "ford", mapName: "The Ford" });
    // Ground the project does not have is a Stage with no name to show, not a
    // Stage that reads as fought on 'other'.
    expect(elsewhere).toMatchObject({ mapId: "other", mapName: undefined });
  });

  it("keeps a Stage whose ground has not been chosen yet", () => {
    // A node can become a Stage from the flow before anybody picks a map for
    // it. A list that hid those would strand them somewhere no surface shows.
    const groundless = project({
      campaigns: [{
        id: "war", name: "War",
        flow: {
          contractVersion: "1.0.0", entryNodeId: "a",
          nodes: [{ id: "a", name: "A", kind: "encounter", transitions: [] }]
        }
      }]
    });
    expect(stagesInProject(groundless)).toHaveLength(1);
    expect(stagesInProject(groundless)[0]).toMatchObject({
      mapId: undefined,
      mapName: undefined
    });
    // And it is on no map, so no map's list claims it.
    expect(stagesOnMap(groundless, "ford")).toEqual([]);
  });

  it("counts nothing that is not a fight", () => {
    expect(stagesInProject(fought).every((stage) => stage.nodeId !== "after"))
      .toBe(true);
  });
});

describe("sceneSentence", () => {
  it("says plainly when nothing is said", () => {
    expect(sceneSentence([], "before")).toBe("Nothing is said before it.");
    expect(sceneSentence([], "after")).toBe("Nothing is said after it.");
  });

  it("names one scene and several", () => {
    expect(sceneSentence(["The Muster"], "before"))
      .toBe("Before it: The Muster.");
    expect(sceneSentence(["One", "Two", "Three"], "after"))
      .toBe("After it: One, Two and Three.");
  });
});
