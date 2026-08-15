// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import {
  SourceProjectEditError,
  SourceProjectSession
} from "./source-project-session";

function fixture(): SourceProject {
  return {
    schemaVersion: "1.0.0",
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
    maps: []
  };
}

function expectCode(operation: () => unknown, code: SourceProjectEditError["code"]) {
  try {
    operation();
    throw new Error("expected edit to fail");
  } catch (error) {
    expect(error).toBeInstanceOf(SourceProjectEditError);
    expect((error as SourceProjectEditError).code).toBe(code);
  }
}

describe("SourceProjectSession", () => {
  it("groups multiple field changes into one reversible transaction", () => {
    const session = new SourceProjectSession(fixture());
    const transaction = session.update("classes", "vanguard", (draft) => {
      draft.name = "Guardian";
      draft.baseStats.health = 20;
      draft.baseStats.defense = 8;
    });

    expect(transaction.affectedPaths).toEqual(["/classes/0"]);
    expect(session.snapshot().classes[0]?.baseStats.health).toBe(20);
    session.undo();
    expect(session.snapshot().classes[0]?.name).toBe("Vanguard");
    session.redo();
    expect(session.snapshot().classes[0]?.baseStats.defense).toBe(8);
  });

  it("creates, updates, and deletes unreferenced records", () => {
    const session = new SourceProjectSession(fixture());
    session.create("items", { id: "tonic", name: "Tonic", stackLimit: 5 });
    session.update("items", "tonic", (draft) => {
      draft.stackLimit = 8;
    });
    expect(session.snapshot().items[0]?.stackLimit).toBe(8);
    session.delete("items", "tonic");
    expect(session.snapshot().items).toEqual([]);
    session.undo();
    expect(session.snapshot().items[0]?.id).toBe("tonic");
  });

  it("renames class and weapon references atomically", () => {
    const session = new SourceProjectSession(fixture());
    expect(session.previewRename("classes", "vanguard", "guardian")).toEqual([
      "/classes/0/id",
      "/unitTypes/0/classId"
    ]);
    expect(session.snapshot().classes[0]?.id).toBe("vanguard");
    const classRename = session.rename("classes", "vanguard", "guardian");
    expect(classRename.affectedPaths).toEqual([
      "/classes/0/id",
      "/unitTypes/0/classId"
    ]);
    expect(session.snapshot().unitTypes[0]?.classId).toBe("guardian");

    session.rename("weapons", "sword", "blade");
    expect(session.snapshot().unitTypes[0]?.startingWeaponIds).toEqual(["blade"]);
    session.undo();
    expect(session.snapshot().unitTypes[0]?.startingWeaponIds).toEqual(["sword"]);
  });

  it("guards referenced deletes and direct identifier edits", () => {
    const session = new SourceProjectSession(fixture());
    expectCode(
      () => session.delete("classes", "vanguard"),
      "DELETE_REFERENCED"
    );
    expectCode(
      () => session.update("classes", "vanguard", (draft) => {
        draft.id = "guardian";
      }),
      "INVALID_PROJECT_FIELD"
    );
    expect(session.canUndo()).toBe(false);
  });

  it("defensively owns input and returned snapshots", () => {
    const source = fixture();
    const session = new SourceProjectSession(source);
    source.title = "Mutated outside";
    const snapshot = session.snapshot();
    snapshot.title = "Also outside";
    expect(session.snapshot().title).toBe("Demo");
  });

  it("preserves omitted legacy type collections without synthesizing defaults", () => {
    const session = new SourceProjectSession(fixture());
    session.updateMetadata({ title: "Renamed Demo" });
    expect(session.snapshot().weaponTypes).toBeUndefined();
    expect(session.snapshot().itemTypes).toBeUndefined();
  });

  it("edits and clears the character style like any other project choice", () => {
    const session = new SourceProjectSession(fixture());
    expect(session.snapshot().characterStyleId).toBeUndefined();

    session.updateMetadata({ characterStyleId: "medieval" });
    expect(session.snapshot().characterStyleId).toBe("medieval");

    // Clearing is explicit, and returns the project to the state a project
    // written before the menu existed is already in.
    session.updateMetadata({ characterStyleId: undefined });
    expect(session.snapshot().characterStyleId).toBeUndefined();
  });

  it("renames typed content through every dependent reference", () => {
    const project = fixture();
    project.weaponTypes = [{ id: "blade", name: "Blade" }];
    project.itemTypes = [{ id: "healing", name: "Healing" }];
    project.classes[0]!.allowedWeaponTypeIds = ["blade"];
    project.weapons[0]!.weaponTypeId = "blade";
    project.items = [{
      id: "tonic",
      name: "Tonic",
      itemTypeId: "healing",
      stackLimit: 5
    }];
    project.unitTypes[0]!.startingItemIds = ["tonic"];
    const session = new SourceProjectSession(project);

    session.rename("weaponTypes", "blade", "sword");
    expect(session.snapshot().classes[0]?.allowedWeaponTypeIds).toEqual(["sword"]);
    expect(session.snapshot().weapons[0]?.weaponTypeId).toBe("sword");
    session.rename("itemTypes", "healing", "recovery");
    expect(session.snapshot().items[0]?.itemTypeId).toBe("recovery");
    session.rename("items", "tonic", "potion");
    expect(session.snapshot().unitTypes[0]?.startingItemIds).toEqual(["potion"]);

    expectCode(
      () => session.delete("weaponTypes", "sword"),
      "DELETE_REFERENCED"
    );
  });

  it("migrates authoring-registry references and guards their deletion", () => {
    const project = fixture();
    project.factions = [{ id: "allies", name: "Allies" }];
    project.abilities = [{
      id: "rally",
      name: "Rally",
      kind: "damage",
      power: 3,
      minimumRange: 1,
      maximumRange: 1
    }];
    project.objectives = [{ id: "survive", name: "Survive" }];
    project.dialogues = [{ id: "opening", name: "Opening" }];
    project.campaigns = [{
      id: "campaign",
      name: "Campaign",
      objectiveIds: ["survive"],
      dialogueIds: ["opening"]
    }];
    project.unitTypes[0]!.factionId = "allies";
    project.unitTypes[0]!.abilityIds = ["rally"];
    const session = new SourceProjectSession(project);

    session.rename("factions", "allies", "heroes");
    session.rename("abilities", "rally", "inspire");
    session.rename("objectives", "survive", "escape");
    session.rename("dialogues", "opening", "intro");
    const snapshot = session.snapshot();
    expect(snapshot.unitTypes[0]?.factionId).toBe("heroes");
    expect(snapshot.unitTypes[0]?.abilityIds).toEqual(["inspire"]);
    expect(snapshot.campaigns?.[0]?.objectiveIds).toEqual(["escape"]);
    expect(snapshot.campaigns?.[0]?.dialogueIds).toEqual(["intro"]);
    expectCode(
      () => session.delete("dialogues", "intro"),
      "DELETE_REFERENCED"
    );
  });

  it("migrates and guards typed content references stored in inert bindings", () => {
    const project = fixture();
    project.abilities = [{
      id: "rally",
      name: "Rally",
      kind: "damage",
      power: 3,
      minimumRange: 1,
      maximumRange: 1,
      scriptBindings: [{
        slot: "activate",
        apiVersion: "1.0.0",
        scriptPath: "scripts/rally.lua",
        entryPoint: "run",
        parameters: [{
          name: "target_class",
          value: {
            kind: "contentRef",
            category: "class",
            sourceKey: "vanguard"
          }
        }]
      }]
    }];
    const session = new SourceProjectSession(project);

    expect(session.previewRename("classes", "vanguard", "guardian")).toContain(
      "/abilities/0/scriptBindings/0/parameters/0/value/sourceKey"
    );
    session.rename("classes", "vanguard", "guardian");
    expect(
      session.snapshot().abilities?.[0]?.scriptBindings?.[0]
        ?.parameters[0]?.value
    ).toEqual(expect.objectContaining({ sourceKey: "guardian" }));
    expectCode(
      () => session.delete("classes", "guardian"),
      "DELETE_REFERENCED"
    );
  });

  it("renames and guards project content referenced by non-linear campaign flows", () => {
    const project = fixture();
    project.maps = [{
      id: "crossroads",
      name: "Crossroads",
      width: 2,
      height: 2,
      terrain: ["grass", "grass", "grass", "grass"]
    }];
    project.objectives = [{ id: "rescue", name: "Rescue" }];
    project.dialogues = [{ id: "reunion", name: "Reunion" }];
    project.items = [{ id: "sigil", name: "Sigil", stackLimit: 1 }];
    project.campaigns = [{
      id: "main",
      name: "Main campaign",
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "opening",
        nodes: [{
          id: "opening",
          name: "Opening",
          kind: "encounter",
          mapId: "crossroads",
          placements: [{
            id: "hero",
            unitTypeId: "soldier",
            side: "first",
            x: 0,
            y: 0
          }],
          objectiveIds: ["rescue"],
          dialogueIds: ["reunion"],
          transitions: [{
            id: "secret-route",
            targetNodeId: "ending",
            priority: 0,
            when: {
              kind: "all",
              conditions: [{
                kind: "objectiveResult",
                objectiveId: "rescue",
                result: "complete"
              }, {
                kind: "inventoryAtLeast",
                itemId: "sigil",
                quantity: 1
              }]
            }
          }]
        }, {
          id: "ending",
          name: "Ending",
          kind: "terminal",
          transitions: []
        }]
      }
    }];
    const session = new SourceProjectSession(project);

    expect(session.previewRename("maps", "crossroads", "ruins")).toContain(
      "/campaigns/0/flow/nodes/0/mapId"
    );
    session.rename("maps", "crossroads", "ruins");
    session.rename("objectives", "rescue", "escape");
    session.rename("dialogues", "reunion", "farewell");
    session.rename("items", "sigil", "key");
    expect(session.previewRename("unitTypes", "soldier", "guardian")).toContain(
      "/campaigns/0/flow/nodes/0/placements/0/unitTypeId"
    );
    session.rename("unitTypes", "soldier", "guardian");

    const flow = session.snapshot().campaigns?.[0]?.flow;
    expect(flow?.nodes[0]?.mapId).toBe("ruins");
    expect(flow?.nodes[0]?.objectiveIds).toEqual(["escape"]);
    expect(flow?.nodes[0]?.dialogueIds).toEqual(["farewell"]);
    expect(flow?.nodes[0]?.placements?.[0]?.unitTypeId).toBe("guardian");
    expect(flow?.nodes[0]?.transitions[0]?.targetNodeId).toBe("ending");
    expect(flow?.nodes[0]?.transitions[0]?.when).toEqual({
      kind: "all",
      conditions: [{
        kind: "objectiveResult",
        objectiveId: "escape",
        result: "complete"
      }, { kind: "inventoryAtLeast", itemId: "key", quantity: 1 }]
    });

    for (const [collection, id] of [
      ["maps", "ruins"],
      ["objectives", "escape"],
      ["dialogues", "farewell"],
      ["items", "key"],
      ["unitTypes", "guardian"]
    ] as const) {
      expectCode(
        () => session.delete(collection, id),
        "DELETE_REFERENCED"
      );
    }
  });

  // A campaign's company is authored in two places, the members it is founded
  // with and the members a node recruits, and both say what a member is made
  // of. A rename that reached the placements and not the members would leave a
  // board fielding a member as something the member is not.
  it("renames and guards the unit types a campaign's company is made of", () => {
    const project = fixture();
    project.maps = [{
      id: "ford",
      name: "Ford",
      width: 2,
      height: 2,
      terrain: ["grass", "grass", "grass", "grass"]
    }];
    project.campaigns = [{
      id: "muster",
      name: "Muster",
      roster: [{ id: "rilla", name: "Vanguard Rilla", unitTypeId: "soldier" }],
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "opening",
        nodes: [{
          id: "opening",
          name: "Opening",
          kind: "encounter",
          mapId: "ford",
          recruits: [{
            id: "torvald",
            name: "Torvald the Ferryman",
            unitTypeId: "soldier"
          }],
          placements: [{
            id: "rilla-tile",
            memberId: "rilla",
            unitTypeId: "soldier",
            side: "first",
            x: 0,
            y: 0
          }],
          transitions: [{ id: "finish", targetNodeId: "ending", priority: 0 }]
        }, {
          id: "ending",
          name: "Ending",
          kind: "terminal",
          transitions: []
        }]
      }
    }];
    const session = new SourceProjectSession(project);

    const affected = session.previewRename("unitTypes", "soldier", "guardian");
    expect(affected).toContain("/campaigns/0/roster/0/unitTypeId");
    expect(affected).toContain(
      "/campaigns/0/flow/nodes/0/recruits/0/unitTypeId"
    );
    session.rename("unitTypes", "soldier", "guardian");

    const campaign = session.snapshot().campaigns?.[0];
    expect(campaign?.roster?.[0]?.unitTypeId).toBe("guardian");
    expect(campaign?.flow?.nodes[0]?.recruits?.[0]?.unitTypeId)
      .toBe("guardian");
    expect(campaign?.flow?.nodes[0]?.placements?.[0]?.unitTypeId)
      .toBe("guardian");
    expectCode(
      () => session.delete("unitTypes", "guardian"),
      "DELETE_REFERENCED"
    );
  });

  // A campaign stocks its own store in two places, the stock it is founded
  // with and the grants a node hands over, and both name an item. Deleting an
  // item a campaign already owns would leave the company holding a name nothing
  // answers to, and a rename that reached one list and not the other would
  // leave half the campaign giving out something that no longer exists.
  it("renames and guards the items a campaign's store is stocked with", () => {
    const project = fixture();
    project.items = [{ id: "tonic", name: "Tonic", stackLimit: 9 }];
    project.campaigns = [{
      id: "road",
      name: "The Road",
      startingStore: [{ itemId: "tonic", quantity: 3 }],
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "opening",
        nodes: [{
          id: "opening",
          name: "Opening",
          kind: "story",
          grants: [{ itemId: "tonic", quantity: 2 }],
          transitions: [{ id: "finish", targetNodeId: "ending", priority: 0 }]
        }, {
          id: "ending",
          name: "Ending",
          kind: "terminal",
          transitions: []
        }]
      }
    }];
    const session = new SourceProjectSession(project);

    const affected = session.previewRename("items", "tonic", "elixir");
    expect(affected).toContain("/campaigns/0/startingStore/0/itemId");
    expect(affected).toContain("/campaigns/0/flow/nodes/0/grants/0/itemId");
    session.rename("items", "tonic", "elixir");

    const campaign = session.snapshot().campaigns?.[0];
    expect(campaign?.startingStore).toEqual([{ itemId: "elixir", quantity: 3 }]);
    expect(campaign?.flow?.nodes[0]?.grants)
      .toEqual([{ itemId: "elixir", quantity: 2 }]);
    expectCode(() => session.delete("items", "elixir"), "DELETE_REFERENCED");
  });

  it("follows a character into the scenes that cast them", () => {
    // A scene's cast is the only place a unit type is named from a record of
    // another kind, and it was the one reference this session did not know
    // about: deleting a character a scene cast was allowed and left the scene
    // speaking for somebody gone, and renaming one left the same hole with no
    // complaint from anywhere. The portrait an author sees in the preview came
    // from exactly this field.
    const project = fixture();
    project.dialogues = [{
      id: "gates",
      name: "The gates open",
      cast: [{ speaker: "Mirea", unitTypeId: "soldier" }],
      lines: [{ speaker: "Mirea", text: "Hold the line." }]
    }];
    const session = new SourceProjectSession(project);

    expect(session.previewRename("unitTypes", "soldier", "veteran"))
      .toContain("/dialogues/0/cast/0/unitTypeId");
    session.rename("unitTypes", "soldier", "veteran");
    expect(session.snapshot().dialogues?.[0]?.cast)
      .toEqual([{ speaker: "Mirea", unitTypeId: "veteran" }]);

    expectCode(
      () => session.delete("unitTypes", "veteran"),
      "DELETE_REFERENCED"
    );
  });

  it("writes several records as one act, undone in one press", () => {
    // An author's act and the records it takes are not the same count. Making
    // a character and standing them on a board is one thing a person did, and
    // it writes a class, a character and the campaign that places them.
    const project = fixture();
    project.campaigns = [{
      id: "march",
      name: "The march",
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "field",
        nodes: [{
          id: "field",
          name: "The field",
          kind: "encounter",
          mapId: "meadow",
          transitions: []
        }]
      }
    }];
    const session = new SourceProjectSession(project);

    const transaction = session.transact("Put Bandit on the board", [
      {
        kind: "create",
        collection: "classes",
        record: {
          id: "rogue_class",
          name: "Rogue class",
          baseStats: { health: 7, movement: 5, strength: 4, defense: 1 }
        }
      },
      {
        kind: "create",
        collection: "unitTypes",
        record: { id: "bandit", name: "Bandit", classId: "rogue_class" }
      },
      {
        kind: "update",
        collection: "campaigns",
        id: "march",
        update: (draft) => {
          draft.flow!.nodes[0]!.placements = [
            { id: "unit", unitTypeId: "bandit", side: "second", x: 1, y: 2 }
          ];
        }
      }
    ]);

    expect(transaction.label).toBe("Put Bandit on the board");
    expect(transaction.affectedPaths).toEqual([
      "/classes/1",
      "/unitTypes/1",
      "/campaigns/0"
    ]);
    const after = session.snapshot();
    expect(after.unitTypes.map((unitType) => unitType.id))
      .toEqual(["soldier", "bandit"]);
    expect(after.campaigns?.[0]?.flow?.nodes[0]?.placements).toHaveLength(1);

    // One press, not three: the whole act goes away together, which is what
    // "one act" has to mean to be worth anything.
    session.undo();
    const back = session.snapshot();
    expect(back.classes.map((entry) => entry.id)).toEqual(["vanguard"]);
    expect(back.unitTypes.map((entry) => entry.id)).toEqual(["soldier"]);
    expect(back.campaigns?.[0]?.flow?.nodes[0]?.placements).toBeUndefined();
    expect(session.canUndo()).toBe(false);
  });

  it("leaves nothing behind when one step of an act refuses", () => {
    // The failure a chain committed a record at a time has and this does not:
    // a weapon left standing with no character holding it, because a later
    // write threw after the earlier ones had already landed.
    const session = new SourceProjectSession(fixture());
    expectCode(
      () => session.transact("Make a soldier", [
        {
          kind: "create",
          collection: "weapons",
          record: { id: "axe", name: "Axe", power: 4, range: 1 }
        },
        {
          kind: "create",
          collection: "unitTypes",
          // Already taken, so this step refuses and the axe above must not
          // survive it.
          record: { id: "soldier", name: "Soldier", classId: "vanguard" }
        }
      ]),
      "DUPLICATE_ID"
    );
    expect(session.snapshot().weapons.map((weapon) => weapon.id))
      .toEqual(["sword"]);
    expect(session.canUndo()).toBe(false);
  });

  it("lets a step read what an earlier step in the same act wrote", () => {
    // A record created and then updated inside one act has to be findable, and
    // one identifier cannot be created twice. Both only work if the steps run
    // against the same draft rather than each against the stored project.
    const session = new SourceProjectSession(fixture());
    session.transact("Make a scout", [
      {
        kind: "create",
        collection: "unitTypes",
        record: { id: "scout", name: "Scout", classId: "vanguard" }
      },
      {
        kind: "update",
        collection: "unitTypes",
        id: "scout",
        update: (draft) => {
          draft.name = "Outrider";
        }
      }
    ]);
    expect(session.snapshot().unitTypes[1]).toMatchObject({
      id: "scout",
      name: "Outrider"
    });

    expectCode(
      () => session.transact("Two of one", [
        {
          kind: "create",
          collection: "weapons",
          record: { id: "bow", name: "Bow", power: 2, range: 3 }
        },
        {
          kind: "create",
          collection: "weapons",
          record: { id: "bow", name: "Bow again", power: 2, range: 3 }
        }
      ]),
      "DUPLICATE_ID"
    );
    expect(session.snapshot().weapons.map((weapon) => weapon.id))
      .toEqual(["sword"]);
  });
});
