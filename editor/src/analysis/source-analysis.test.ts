// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import { analyzeSourceProject, statDeltaBounds } from "./source-analysis";
import type {
  AnalysisWorkerRequest,
  AnalysisWorkerResponse
} from "./analysis-worker-protocol";
import {
  AnalysisWorkerClient,
  type AnalysisWorkerPort
} from "./analysis-worker-client";

const validProject = {
  schemaVersion: "1.2.0",
  packageId: "123e4567-e89b-12d3-a456-426614174000",
  gameId: "demo",
  title: "Demo",
  contentRevision: "1.0.0",
  classes: [{
    id: "vanguard",
    name: "Vanguard",
    baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
  }],
  unitTypes: [{
    id: "soldier",
    name: "Soldier",
    classId: "vanguard",
    startingWeaponIds: ["sword"]
  }],
  weapons: [{ id: "sword", name: "Sword", power: 3, range: 1 }],
  items: [],
  maps: [{ id: "field", name: "Field", width: 2, height: 1, terrain: ["grass", "grass"] }]
};

const typedProject = {
  ...validProject,
  weaponTypes: [{ id: "blade", name: "Blade" }],
  itemTypes: [{ id: "healing", name: "Healing" }],
  classes: [{
    ...validProject.classes[0],
    allowedWeaponTypeIds: ["blade"]
  }],
  unitTypes: [{
    ...validProject.unitTypes[0],
    startingItemIds: ["tonic"]
  }],
  weapons: [{
    ...validProject.weapons[0],
    weaponTypeId: "blade"
  }],
  items: [{
    id: "tonic",
    name: "Tonic",
    itemTypeId: "healing",
    stackLimit: 5
  }]
};

type PortListener = (event: MessageEvent<AnalysisWorkerResponse> | ErrorEvent) => void;

class LoopbackWorker implements AnalysisWorkerPort {
  readonly listeners = new Map<string, PortListener>();
  terminated = false;

  postMessage(message: AnalysisWorkerRequest): void {
    queueMicrotask(() => this.listeners.get("message")?.({
      data: {
        id: message.id,
        analysis: analyzeSourceProject(message.sourcePath, message.text)
      }
    } as MessageEvent<AnalysisWorkerResponse>));
  }

  addEventListener(
    type: "message" | "error" | "messageerror",
    listener: PortListener
  ): void {
    this.listeners.set(type, listener);
  }

  removeEventListener(type: "message" | "error" | "messageerror"): void {
    this.listeners.delete(type);
  }

  terminate(): void {
    this.terminated = true;
  }
}

/** A worker that never answers: the shape of a script that failed to load. */
class SilentWorker extends LoopbackWorker {
  override postMessage(): void {}
}

describe("source analysis", () => {
  // Where a delta may land, read out of the schema rather than written here a
  // second time. The four that stand in the damage arithmetic stop at the
  // bound the rules refuse a unit for passing. `tools/source_schema/test.mjs`
  // checks that number against the engine header, and this checks that the
  // table reaches it. Asserted directly because every fixture that exercises
  // the rule does it on health, movement or speed, none of which would notice
  // a `$ref` this stopped following.
  it("lands a delta inside the bounds the stat's own field admits", () => {
    expect(statDeltaBounds.strength).toEqual(
      { minimum: 0, maximum: 16383, whenOmitted: 0 }
    );
    expect(statDeltaBounds.defense.maximum).toBe(16383);
    expect(statDeltaBounds.resistance.maximum).toBe(16383);
    expect(statDeltaBounds.magic.maximum).toBe(16383);
    expect(statDeltaBounds.skill).toEqual(
      { minimum: 0, maximum: 32767, whenOmitted: 0 }
    );
    expect(statDeltaBounds.health).toEqual(
      { minimum: 1, maximum: 32767, whenOmitted: 1 }
    );
    expect(statDeltaBounds.movement).toEqual(
      { minimum: 1, maximum: 255, whenOmitted: 1 }
    );
  });

  it("parses, validates, and indexes a project with typed references", () => {
    const result = analyzeSourceProject(
      "project.json",
      JSON.stringify(validProject)
    );

    expect(result.diagnostics).toEqual([]);
    expect(result.definitions).toHaveLength(4);
    expect(result.definitions.find((item) => item.sourceKey === "soldier")
      ?.references.map((reference) => `${reference.category}:${reference.sourceKey}`))
      .toEqual(["class:vanguard", "weapon:sword"]);
  });

  it("reports syntax, schema, semantic-reference, and map-shape failures", () => {
    expect(analyzeSourceProject("project.json", "{").diagnostics[0]?.code)
      .toBe("SOURCE_JSON_INVALID");

    const schemaInvalid = { ...validProject, title: "" };
    expect(analyzeSourceProject("project.json", JSON.stringify(schemaInvalid))
      .diagnostics.map((item) => item.code)).toContain("SOURCE_SCHEMA_INVALID");

    const semanticInvalid = {
      ...validProject,
      unitTypes: [{ ...validProject.unitTypes[0], classId: "missing" }],
      maps: [{ ...validProject.maps[0], terrain: ["grass"] }]
    };
    expect(analyzeSourceProject("project.json", JSON.stringify(semanticInvalid))
      .diagnostics.map((item) => item.code)).toEqual([
        "SOURCE_REF_MISSING",
        "SOURCE_MAP_SHAPE_INVALID"
      ]);
  });

  it("restates schema failures as what the author should do", () => {
    const messages = (project: unknown) =>
      analyzeSourceProject("project.json", JSON.stringify(project))
        .diagnostics.map((diagnostic) => diagnostic.message);

    expect(messages({ ...validProject, title: "" }))
      .toContain("this text must not be empty");
    expect(messages({
      ...validProject,
      classes: [{ ...validProject.classes[0], actsAfterAttacking: "yes" }]
    })).toContain("this value must be true or false");
    expect(messages({
      ...validProject,
      classes: [{ ...validProject.classes[0], id: "Vanguard" }]
    }).join(" ")).toContain("lowercase letters");
    expect(messages({
      ...validProject,
      classes: [{ id: "vanguard", name: "Vanguard" }]
    })).toContain(
      "missing required field 'baseStats': fill it in and save again"
    );
    expect(messages({
      ...validProject,
      classes: [{ ...validProject.classes[0], armour: 3 }]
    })).toContain("unknown field 'armour': remove it");
    expect(messages({
      ...validProject,
      classes: [{
        ...validProject.classes[0],
        baseStats: { ...validProject.classes[0]!.baseStats, health: 0 }
      }]
    })).toContain("this number must be at least 1");
  });

  it("indexes typed weapon and item relationships", () => {
    const result = analyzeSourceProject(
      "typed.json",
      JSON.stringify(typedProject)
    );
    expect(result.diagnostics).toEqual([]);
    expect(result.definitions.find((item) => item.sourceKey === "blade")
      ?.category).toBe("weapon_type");
    expect(result.definitions.find((item) => item.sourceKey === "sword")
      ?.references).toContainEqual(expect.objectContaining({
        category: "weapon_type",
        sourceKey: "blade"
      }));
  });

  it("indexes inert script content references and reports binding conflicts", () => {
    const scriptedProject = {
      ...validProject,
      abilities: [{
        id: "reinforce",
        name: "Reinforce",
        kind: "damage",
        power: 3,
        minimumRange: 1,
        maximumRange: 1,
        scriptBindings: [
          {
            slot: "on_use",
            apiVersion: "1.0.0",
            scriptPath: "scripts/reinforce.js",
            entryPoint: "activate",
            parameters: [
              {
                name: "unit",
                value: {
                  kind: "contentRef",
                  category: "unit_type",
                  sourceKey: "soldier"
                }
              },
              { name: "unit", value: { kind: "integer", value: 2 } }
            ]
          },
          {
            slot: "on_use",
            apiVersion: "1.0.0",
            scriptPath: "scripts/alternate.js",
            entryPoint: "activate",
            parameters: [{
              name: "target",
              value: {
                kind: "contentRef",
                category: "map",
                sourceKey: "missing-map"
              }
            }]
          }
        ]
      }]
    };

    const result = analyzeSourceProject(
      "scripted.json",
      JSON.stringify(scriptedProject)
    );

    const ability = result.definitions.find(
      (item) => item.category === "ability" && item.sourceKey === "reinforce"
    );
    expect(ability?.references).toEqual([
      expect.objectContaining({
        category: "unit_type",
        sourceKey: "soldier",
        semanticPath:
          "/abilities/0/scriptBindings/0/parameters/0/value/sourceKey"
      }),
      expect.objectContaining({
        category: "map",
        sourceKey: "missing-map",
        semanticPath:
          "/abilities/0/scriptBindings/1/parameters/0/value/sourceKey"
      })
    ]);
    expect(result.diagnostics.map((item) => item.code)).toEqual([
      "SOURCE_SCRIPT_PARAMETER_DUPLICATE",
      "SOURCE_SCRIPT_SLOT_DUPLICATE",
      "SOURCE_REF_MISSING"
    ]);
    expect(result.indexDiagnostics).toContainEqual(expect.objectContaining({
      code: "INDEX_UNRESOLVED_REFERENCE",
      semanticPath:
        "/abilities/0/scriptBindings/1/parameters/0/value/sourceKey"
    }));
  });

  it("indexes nonlinear campaign references and accepts cycles and recombination", () => {
    const project = {
      ...validProject,
      items: [{ id: "key", name: "Key", stackLimit: 1 }],
      objectives: [{ id: "victory", name: "Victory" }],
      dialogues: [{ id: "reunion", name: "Reunion" }],
      campaigns: [{
        id: "main",
        name: "Main",
        roster: [{ id: "captain", name: "Captain", unitTypeId: "soldier" }],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "start",
          nodes: [
            {
              id: "start",
              name: "Start",
              kind: "encounter",
              mapId: "field",
              objectiveIds: ["victory"],
              transitions: [
                {
                  id: "start-left",
                  targetNodeId: "left",
                  priority: 0,
                  when: {
                    kind: "all",
                    conditions: [
                      {
                        kind: "objectiveResult",
                        objectiveId: "victory",
                        result: "victory"
                      },
                      {
                        kind: "inventoryAtLeast",
                        itemId: "key",
                        quantity: 1
                      }
                    ]
                  }
                },
                { id: "start-right", targetNodeId: "right", priority: 1 }
              ]
            },
            {
              id: "left",
              name: "Left",
              kind: "story",
              transitions: [{ id: "left-join", targetNodeId: "join", priority: 0 }]
            },
            {
              id: "right",
              name: "Right",
              kind: "story",
              transitions: [{ id: "right-join", targetNodeId: "join", priority: 0 }]
            },
            {
              id: "join",
              name: "Join",
              kind: "story",
              dialogueIds: ["reunion"],
              transitions: [{ id: "cycle", targetNodeId: "start", priority: 0 }]
            }
          ]
        }
      }]
    };

    const result = analyzeSourceProject("campaign.json", JSON.stringify(project));

    expect(result.diagnostics).toEqual([]);
    const campaign = result.definitions.find(
      (item) => item.category === "campaign" && item.sourceKey === "main"
    );
    expect(campaign?.references).toEqual(expect.arrayContaining([
      expect.objectContaining({ category: "map", sourceKey: "field" }),
      expect.objectContaining({ category: "objective", sourceKey: "victory" }),
      expect.objectContaining({ category: "item", sourceKey: "key" }),
      expect.objectContaining({ category: "dialogue", sourceKey: "reunion" })
    ]));
  });

  it("reports malformed campaign graph identities, routing, and reachability", () => {
    const project = {
      ...validProject,
      objectives: [{ id: "victory", name: "Victory" }],
      campaigns: [{
        id: "broken",
        name: "Broken",
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "start",
          nodes: [
            {
              id: "start",
              name: "Start",
              kind: "story",
              transitions: [
                { id: "route", targetNodeId: "missing", priority: 4 },
                { id: "route", targetNodeId: "start", priority: 4 }
              ]
            },
            {
              id: "start",
              name: "Duplicate",
              kind: "terminal",
              transitions: []
            },
            {
              id: "orphan",
              name: "Orphan",
              kind: "terminal",
              transitions: []
            }
          ]
        }
      }]
    };

    const diagnostics = analyzeSourceProject(
      "campaign.json",
      JSON.stringify(project)
    ).diagnostics;
    expect(diagnostics).toEqual(expect.arrayContaining([
      expect.objectContaining({
        code: "SOURCE_CAMPAIGN_NODE_ID_DUPLICATE",
        instancePath: "/campaigns/0/flow/nodes/1/id"
      }),
      expect.objectContaining({
        code: "SOURCE_CAMPAIGN_TRANSITION_ID_DUPLICATE",
        instancePath: "/campaigns/0/flow/nodes/0/transitions/1/id"
      }),
      expect.objectContaining({
        code: "SOURCE_CAMPAIGN_TRANSITION_TARGET_MISSING",
        instancePath: "/campaigns/0/flow/nodes/0/transitions/0/targetNodeId"
      }),
      expect.objectContaining({
        code: "SOURCE_CAMPAIGN_TRANSITION_PRIORITY_DUPLICATE",
        instancePath: "/campaigns/0/flow/nodes/0/transitions/1/priority"
      }),
      expect.objectContaining({
        code: "SOURCE_CAMPAIGN_FALLBACK_DUPLICATE",
        instancePath: "/campaigns/0/flow/nodes/0/transitions/1"
      }),
      expect.objectContaining({
        code: "SOURCE_CAMPAIGN_NODE_UNREACHABLE",
        instancePath: "/campaigns/0/flow/nodes/2/id"
      })
    ]));
  });

  it("rejects encounter placements that occupy the same map tile", () => {
    const project = {
      ...validProject,
      objectives: [{ id: "victory", name: "Victory" }],
      campaigns: [{
        id: "overlap",
        name: "Overlap",
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "battle",
          nodes: [{
            id: "battle",
            name: "Battle",
            kind: "encounter",
            objectiveIds: ["victory"],
            mapId: "field",
            placements: [
              { id: "left", unitTypeId: "soldier", side: "first", x: 0, y: 0 },
              { id: "right", unitTypeId: "soldier", side: "second", x: 0, y: 0 }
            ],
            transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
          }, {
            id: "end",
            name: "End",
            kind: "terminal",
            transitions: []
          }]
        }
      }]
    };

    expect(analyzeSourceProject(
      "campaign.json",
      JSON.stringify(project)
    ).diagnostics).toContainEqual(expect.objectContaining({
      code: "SOURCE_CAMPAIGN_PLACEMENT_TILE_OCCUPIED",
      instancePath: "/campaigns/0/flow/nodes/0/placements/1/x"
    }));
  });

  it("indexes the company a campaign is founded with and the members it recruits", () => {
    const project = {
      ...validProject,
      unitTypes: [
        ...validProject.unitTypes,
        { id: "medic", name: "Medic", classId: "vanguard" }
      ],
      objectives: [{ id: "victory", name: "Victory" }],
      campaigns: [{
        id: "muster",
        name: "Muster",
        roster: [{
          id: "rilla",
          name: "Vanguard Rilla",
          unitTypeId: "soldier"
        }],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "battle",
          nodes: [
            {
              id: "battle",
              name: "Battle",
              kind: "encounter",
              objectiveIds: ["victory"],
              mapId: "field",
              recruits: [{
                id: "torvald",
                name: "Torvald the Ferryman",
                unitTypeId: "medic"
              }],
              placements: [
                {
                  id: "rilla-tile",
                  memberId: "rilla",
                  unitTypeId: "soldier",
                  side: "first",
                  x: 0,
                  y: 0
                },
                {
                  id: "raider-tile",
                  unitTypeId: "soldier",
                  side: "second",
                  x: 1,
                  y: 0
                }
              ],
              transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
            },
            { id: "end", name: "End", kind: "terminal", transitions: [] }
          ]
        }
      }]
    };

    const result = analyzeSourceProject("campaign.json", JSON.stringify(project));

    expect(result.diagnostics).toEqual([]);
    const campaign = result.definitions.find(
      (item) => item.category === "campaign" && item.sourceKey === "muster"
    );
    expect(campaign?.references).toEqual(expect.arrayContaining([
      expect.objectContaining({
        category: "unit_type",
        sourceKey: "soldier",
        semanticPath: "/campaigns/0/roster/0/unitTypeId"
      }),
      expect.objectContaining({
        category: "unit_type",
        sourceKey: "medic",
        semanticPath: "/campaigns/0/flow/nodes/0/recruits/0/unitTypeId"
      })
    ]));
  });

  it("reports a campaign nobody can be founded from at its roster", () => {
    const project = {
      ...validProject,
      campaigns: [{
        id: "unfounded",
        name: "Unfounded",
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "prologue",
          nodes: [{
            id: "prologue",
            name: "Prologue",
            kind: "terminal",
            transitions: []
          }]
        }
      }]
    };

    expect(analyzeSourceProject("campaign.json", JSON.stringify(project))
      .diagnostics).toContainEqual(expect.objectContaining({
        code: "SOURCE_CAMPAIGN_ROSTER_EMPTY",
        instancePath: "/campaigns/0/roster"
      }));
  });

  it("reports a member identity claimed twice and a member made of nothing", () => {
    const project = {
      ...validProject,
      objectives: [{ id: "victory", name: "Victory" }],
      campaigns: [{
        id: "muster",
        name: "Muster",
        roster: [
          { id: "rilla", name: "Vanguard Rilla", unitTypeId: "soldier" },
          { id: "ghost", name: "Nobody's Shape", unitTypeId: "missing" }
        ],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "road",
          nodes: [{
            id: "road",
            name: "Road",
            kind: "terminal",
            recruits: [{
              id: "rilla",
              name: "Rilla Again",
              unitTypeId: "soldier"
            }],
            transitions: []
          }]
        }
      }]
    };

    expect(analyzeSourceProject("campaign.json", JSON.stringify(project))
      .diagnostics).toEqual([
        expect.objectContaining({
          code: "SOURCE_REF_MISSING",
          instancePath: "/campaigns/0/roster/1/unitTypeId"
        }),
        expect.objectContaining({
          code: "SOURCE_CAMPAIGN_MEMBER_ID_DUPLICATE",
          instancePath: "/campaigns/0/flow/nodes/0/recruits/0/id"
        })
      ]);
  });

  it("reports placements that field nobody, a stranger, an enemy, one member twice, or the wrong shape", () => {
    const project = {
      ...validProject,
      unitTypes: [
        ...validProject.unitTypes,
        { id: "medic", name: "Medic", classId: "vanguard" }
      ],
      maps: [{
        id: "yard",
        name: "Yard",
        width: 3,
        height: 2,
        terrain: ["grass", "grass", "grass", "grass", "grass", "grass"]
      }],
      objectives: [{ id: "victory", name: "Victory" }],
      campaigns: [{
        id: "misfielded",
        name: "Misfielded",
        roster: [
          { id: "rilla", name: "Vanguard Rilla", unitTypeId: "soldier" },
          { id: "kel", name: "Kel the Mender", unitTypeId: "medic" }
        ],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "battle",
          nodes: [
            {
              id: "battle",
              name: "Battle",
              kind: "encounter",
              objectiveIds: ["victory"],
              mapId: "yard",
              placements: [
                {
                  id: "rilla-tile",
                  memberId: "rilla",
                  unitTypeId: "soldier",
                  side: "first",
                  x: 0,
                  y: 0
                },
                {
                  id: "nameless-tile",
                  unitTypeId: "soldier",
                  side: "first",
                  x: 1,
                  y: 0
                },
                {
                  id: "stranger-tile",
                  memberId: "stranger",
                  unitTypeId: "soldier",
                  side: "first",
                  x: 2,
                  y: 0
                },
                {
                  id: "enemy-tile",
                  memberId: "rilla",
                  unitTypeId: "soldier",
                  side: "second",
                  x: 0,
                  y: 1
                },
                {
                  id: "twice-tile",
                  memberId: "rilla",
                  unitTypeId: "soldier",
                  side: "first",
                  x: 1,
                  y: 1
                },
                {
                  id: "mismatch-tile",
                  memberId: "kel",
                  unitTypeId: "soldier",
                  side: "first",
                  x: 2,
                  y: 1
                }
              ],
              transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
            },
            { id: "end", name: "End", kind: "terminal", transitions: [] }
          ]
        }
      }]
    };

    expect(analyzeSourceProject("campaign.json", JSON.stringify(project))
      .diagnostics).toEqual([
        expect.objectContaining({
          code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_MISSING",
          instancePath: "/campaigns/0/flow/nodes/0/placements/1"
        }),
        expect.objectContaining({
          code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_UNKNOWN",
          instancePath: "/campaigns/0/flow/nodes/0/placements/2/memberId"
        }),
        expect.objectContaining({
          code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_FORBIDDEN",
          instancePath: "/campaigns/0/flow/nodes/0/placements/3/memberId"
        }),
        expect.objectContaining({
          code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_DUPLICATE",
          instancePath: "/campaigns/0/flow/nodes/0/placements/4/memberId"
        }),
        expect.objectContaining({
          code: "SOURCE_CAMPAIGN_PLACEMENT_MEMBER_TYPE_MISMATCH",
          instancePath: "/campaigns/0/flow/nodes/0/placements/5/unitTypeId"
        })
      ]);
  });

  it("reports a missing campaign entry node at the entry field", () => {
    const project = {
      ...validProject,
      campaigns: [{
        id: "broken-entry",
        name: "Broken Entry",
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "missing",
          nodes: [{
            id: "ending",
            name: "Ending",
            kind: "terminal",
            transitions: []
          }]
        }
      }]
    };

    expect(analyzeSourceProject("campaign.json", JSON.stringify(project))
      .diagnostics).toContainEqual(expect.objectContaining({
        code: "SOURCE_CAMPAIGN_ENTRY_MISSING",
        instancePath: "/campaigns/0/flow/entryNodeId"
      }));
  });

  it("correlates concurrent worker responses and rejects pending work on close", async () => {
    const worker = new LoopbackWorker();
    const client = new AnalysisWorkerClient(worker);
    const first = client.analyze("first.json", JSON.stringify(validProject));
    const second = client.analyze("second.json", JSON.stringify(validProject));

    expect((await first).diagnostics).toEqual([]);
    expect((await second).definitions[0]?.sourcePath).toBe("second.json");
    const pending = client.analyze("third.json", JSON.stringify(validProject));
    client.close();
    await expect(pending).rejects.toThrow("analysis worker closed");
    expect(worker.terminated).toBe(true);
  });

  // A worker script that fails to load never answers anything. Every pending
  // and future analyze() must still settle, or the toolbar's busy flag stays
  // up forever and Save is disabled with edits pending.
  it("settles pending and future analyses when the worker itself fails", async () => {
    const worker = new SilentWorker();
    const client = new AnalysisWorkerClient(worker);
    const pending = client.analyze("first.json", JSON.stringify(validProject));
    worker.listeners.get("error")?.(
      { message: "worker script failed to load" } as ErrorEvent
    );
    await expect(pending).rejects.toThrow("worker script failed to load");
    await expect(
      client.analyze("second.json", JSON.stringify(validProject))
    ).rejects.toThrow("worker script failed to load");
    client.close();
  });

  it("times out an analysis whose reply never arrives", async () => {
    const client = new AnalysisWorkerClient(new SilentWorker(), { timeoutMs: 10 });
    await expect(
      client.analyze("lost.json", JSON.stringify(validProject))
    ).rejects.toThrow("analysis timed out after 10ms");
    client.close();
  });
});
