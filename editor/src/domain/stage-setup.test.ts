// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { CampaignFlow, SourceProject } from "../generated/source-v1";
import { planStageOnMap } from "./stage-setup";

function project(overrides: Partial<SourceProject> = {}): SourceProject {
  return {
    schemaVersion: "1.1.0",
    packageId: "1a5b0f9c-2d3e-4a5b-8c7d-9e0f1a2b3c4d",
    gameId: "stage.setup.fixture",
    title: "The Long Road",
    contentRevision: "0.1.0",
    classes: [],
    abilities: [],
    unitTypes: [],
    weaponTypes: [],
    weapons: [],
    itemTypes: [],
    items: [],
    maps: [
      { id: "ford", name: "Fordlight Crossing", width: 4, height: 3,
        terrain: Array.from({ length: 12 }, () => "plain") }
    ],
    ...overrides
  } as SourceProject;
}

/** Every node the entry node can reach, the way the flow editor counts it. */
function reachable(flow: CampaignFlow): Set<string> {
  const byId = new Map(flow.nodes.map((node) => [node.id, node]));
  const seen = new Set<string>();
  const pending = [flow.entryNodeId];
  while (pending.length > 0) {
    const id = pending.pop()!;
    if (seen.has(id)) continue;
    seen.add(id);
    for (const transition of byId.get(id)?.transitions ?? []) {
      pending.push(transition.targetNodeId);
    }
  }
  return seen;
}

describe("planStageOnMap", () => {
  it("makes a campaign that opens on the Stage when there is none", () => {
    const plan = planStageOnMap(project(), "ford");
    expect(plan.kind).toBe("createCampaign");
    if (plan.kind !== "createCampaign") return;
    const flow = plan.campaign.flow!;
    expect(flow.entryNodeId).toBe(plan.nodeId);
    const stage = flow.nodes.find((node) => node.id === plan.nodeId)!;
    expect(stage.kind).toBe("encounter");
    expect(stage.mapId).toBe("ford");
    // A Stage that leads nowhere is a flow the editor refuses to save, so the
    // way out is part of the same plan rather than the author's next problem.
    expect(stage.transitions).toHaveLength(1);
    const after = flow.nodes.find(
      (node) => node.id === stage.transitions[0]!.targetNodeId
    )!;
    expect(after.kind).toBe("terminal");
    expect(reachable(flow)).toEqual(new Set(flow.nodes.map((node) => node.id)));
    // The campaign is named for the game, not for the record type.
    expect(plan.campaign.name).toBe("The Long Road");
    expect(plan.summary).toContain("Fordlight Crossing");
  });

  it("names a campaign for the record type only when the game is unnamed", () => {
    const plan = planStageOnMap(project({ title: "   " }), "ford");
    expect(plan.kind).toBe("createCampaign");
    if (plan.kind !== "createCampaign") return;
    expect(plan.campaign.name).toBe("Campaign");
    expect(plan.campaign.id).toBe("campaign");
  });

  it("gives a campaign with no flow one that opens on the Stage", () => {
    const plan = planStageOnMap(
      project({
        campaigns: [{
          id: "march",
          name: "The March",
          roster: [{ id: "rina", name: "Rina", unitTypeId: "knight" }]
        }]
      }),
      "ford"
    );
    expect(plan.kind).toBe("extendCampaign");
    if (plan.kind !== "extendCampaign") return;
    expect(plan.campaignId).toBe("march");
    expect(plan.flow.entryNodeId).toBe(plan.nodeId);
    // The plan is a flow, so nothing else on the campaign, the company it
    // already holds most of all, is in a position to be lost by applying it.
    expect(plan.summary).toContain("The March");
  });

  it("hangs a Stage off the node where the campaign currently stops", () => {
    const plan = planStageOnMap(
      project({
        campaigns: [{
          id: "march",
          name: "The March",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "start",
            nodes: [
              { id: "start", name: "Start", kind: "story", transitions: [
                { id: "next", targetNodeId: "ending", priority: 0 }
              ] },
              { id: "ending", name: "Ending", kind: "terminal", transitions: [] }
            ]
          }
        }]
      }),
      "ford"
    );
    expect(plan.kind).toBe("extendCampaign");
    if (plan.kind !== "extendCampaign") return;
    const ending = plan.flow.nodes.find((node) => node.id === "ending")!;
    // An ending that leads on is not an ending, and `terminal` is the one kind
    // the format refuses transitions on.
    expect(ending.kind).toBe("story");
    expect(ending.transitions.map((transition) => transition.targetNodeId))
      .toEqual([plan.nodeId]);
    expect(plan.flow.entryNodeId).toBe("start");
    expect(reachable(plan.flow))
      .toEqual(new Set(plan.flow.nodes.map((node) => node.id)));
    // The one change to what the author wrote is stated, not made quietly.
    expect(plan.summary).toContain("It follows Ending");
  });

  it("keeps a Stage a Stage when the campaign stops on one", () => {
    const plan = planStageOnMap(
      project({
        campaigns: [{
          id: "march",
          name: "The March",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "first",
            nodes: [
              { id: "first", name: "First", kind: "encounter", mapId: "ford",
                transitions: [] }
            ]
          }
        }]
      }),
      "ford",
      // Deliberately another: this campaign already fights at the ford, and
      // what is under test is the tail it hangs from rather than the finding.
      undefined,
      "another"
    );
    expect(plan.kind).toBe("extendCampaign");
    if (plan.kind !== "extendCampaign") return;
    const first = plan.flow.nodes.find((node) => node.id === "first")!;
    expect(first.kind).toBe("encounter");
    expect(first.mapId).toBe("ford");
    expect(first.transitions).toHaveLength(1);
  });

  it("opens the Stage already here rather than making a second", () => {
    // Asking twice is the same question, so it gets the same answer, and
    // nothing is written for the second press to undo. A front door that made
    // a Stage per press would make three for an author who was not sure the
    // first press had landed and checked by pressing again.
    const once = planStageOnMap(
      project({ campaigns: [{ id: "march", name: "The March" }] }),
      "ford"
    );
    expect(once.kind).toBe("extendCampaign");
    if (once.kind !== "extendCampaign") return;
    const source = project({
      campaigns: [{ id: "march", name: "The March", flow: once.flow }]
    });
    const twice = planStageOnMap(source, "ford");
    expect(twice.kind).toBe("openExisting");
    if (twice.kind !== "openExisting") return;
    expect(twice.campaignId).toBe("march");
    expect(twice.nodeId).toBe(once.nodeId);
    expect(twice.summary).toContain("already fights at Fordlight Crossing");
    // Pressing on and on is pressing once: the tenth answer is the first one.
    for (let press = 0; press < 8; press += 1) {
      expect(planStageOnMap(source, "ford")).toEqual(twice);
    }
  });

  it("finds the Stage here even when the campaign fights elsewhere first", () => {
    const plan = planStageOnMap(
      project({
        maps: [
          { id: "ford", name: "Fordlight Crossing", width: 4, height: 3,
            terrain: Array.from({ length: 12 }, () => "plain") },
          { id: "keep", name: "The Keep", width: 4, height: 3,
            terrain: Array.from({ length: 12 }, () => "plain") }
        ],
        campaigns: [{
          id: "march",
          name: "The March",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "keep_fight",
            nodes: [
              { id: "keep_fight", name: "At the Keep", kind: "encounter",
                mapId: "keep", transitions: [
                  { id: "next", targetNodeId: "ford_fight", priority: 0 }
                ] },
              { id: "ford_fight", name: "At the Ford", kind: "encounter",
                mapId: "ford", transitions: [] }
            ]
          }
        }]
      }),
      "ford"
    );
    expect(plan.kind).toBe("openExisting");
    if (plan.kind !== "openExisting") return;
    expect(plan.nodeId).toBe("ford_fight");
  });

  it("adds a second Stage to the same map when asked for another", () => {
    const once = planStageOnMap(
      project({ campaigns: [{ id: "march", name: "The March" }] }),
      "ford"
    );
    expect(once.kind).toBe("extendCampaign");
    if (once.kind !== "extendCampaign") return;
    // A map fought over twice is legal and wanted. It is a different question,
    // asked by a different control, and never by pressing the first one again.
    const twice = planStageOnMap(
      project({
        campaigns: [{ id: "march", name: "The March", flow: once.flow }]
      }),
      "ford",
      undefined,
      "another"
    );
    expect(twice.kind).toBe("extendCampaign");
    if (twice.kind !== "extendCampaign") return;
    expect(twice.nodeId).not.toBe(once.nodeId);
    const ids = twice.flow.nodes.map((node) => node.id);
    expect(new Set(ids).size).toBe(ids.length);
    expect(
      twice.flow.nodes.filter(
        (node) => node.kind === "encounter" && node.mapId === "ford"
      )
    ).toHaveLength(2);
    expect(reachable(twice.flow)).toEqual(new Set(ids));
  });

  it("joins the campaign it is told to when the game has several", () => {
    const several = project({
      campaigns: [
        { id: "march", name: "The March" },
        { id: "siege", name: "The Siege" }
      ]
    });
    const chosen = planStageOnMap(several, "ford", "siege");
    expect(chosen.kind).toBe("extendCampaign");
    if (chosen.kind !== "extendCampaign") return;
    expect(chosen.campaignId).toBe("siege");
    // Told nothing, it joins the first, which is the only one whenever there
    // is only one.
    const unchosen = planStageOnMap(several, "ford");
    expect(unchosen.kind).toBe("extendCampaign");
    if (unchosen.kind !== "extendCampaign") return;
    expect(unchosen.campaignId).toBe("march");
  });

  it("refuses a map the game has not got", () => {
    const plan = planStageOnMap(project(), "nowhere");
    expect(plan.kind).toBe("refused");
    if (plan.kind !== "refused") return;
    expect(plan.reason).toContain("no map 'nowhere'");
  });

  it("refuses a campaign the game has not got", () => {
    const plan = planStageOnMap(project(), "ford", "gone");
    expect(plan.kind).toBe("refused");
    if (plan.kind !== "refused") return;
    expect(plan.reason).toContain("no campaign 'gone'");
  });

  it("refuses a flow that never stops rather than orphaning the Stage", () => {
    const plan = planStageOnMap(
      project({
        campaigns: [{
          id: "loop",
          name: "The Loop",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "a",
            nodes: [
              { id: "a", name: "A", kind: "story", transitions: [
                { id: "next", targetNodeId: "b", priority: 0 }
              ] },
              { id: "b", name: "B", kind: "story", transitions: [
                { id: "next", targetNodeId: "a", priority: 0 }
              ] }
            ]
          }
        }]
      }),
      "ford"
    );
    expect(plan.kind).toBe("refused");
    if (plan.kind !== "refused") return;
    expect(plan.reason).toContain("no point where it stops");
  });

  it("ignores an end the entry node can never reach", () => {
    const plan = planStageOnMap(
      project({
        campaigns: [{
          id: "march",
          name: "The March",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "start",
            nodes: [
              { id: "start", name: "Start", kind: "story", transitions: [
                { id: "next", targetNodeId: "ending", priority: 0 }
              ] },
              { id: "ending", name: "Ending", kind: "terminal", transitions: [] },
              // Already unreachable, and already reported as a problem. Hanging
              // the Stage here would make the Stage unreachable too.
              { id: "orphan", name: "Orphan", kind: "terminal", transitions: [] }
            ]
          }
        }]
      }),
      "ford"
    );
    expect(plan.kind).toBe("extendCampaign");
    if (plan.kind !== "extendCampaign") return;
    expect(plan.flow.nodes.find((node) => node.id === "orphan")!.transitions)
      .toEqual([]);
    expect(plan.flow.nodes.find((node) => node.id === "ending")!.transitions)
      .toHaveLength(1);
  });
});
