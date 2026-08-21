// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  DEFAULT_TURN_ORDER,
  NEW_PROJECT_TURN_ORDER,
  TURN_ORDERS,
  boardsFollowingDefault,
  projectTurnOrder,
  resolveTurnOrder,
  turnOrderLabel,
  turnOrderOverrides
} from "./game-settings";
import { createSourceProject } from "./source-project-document";
import { sourceV1Schemas } from "../generated/source-v1-schemas";
import type { CampaignNode, SourceProject } from "../generated/source-v1";

function encounter(id: string, turnOrder?: CampaignNode["turnOrder"]): CampaignNode {
  return {
    id,
    name: id.replace(/_/g, " "),
    kind: "encounter",
    mapId: "field",
    transitions: [{ id: `${id}_done`, targetNodeId: "end", priority: 0 }],
    ...(turnOrder === undefined ? {} : { turnOrder })
  };
}

function project(
  defaultTurnOrder: SourceProject["defaultTurnOrder"],
  nodes: CampaignNode[]
): SourceProject {
  return {
    schemaVersion: "1.2.0",
    packageId: "0f0d7b0e-8f2b-4a58-9d0f-1c2b3a4d5e6f",
    gameId: "settings.test",
    title: "Settings Test",
    contentRevision: "1.0.0",
    ...(defaultTurnOrder === undefined ? {} : { defaultTurnOrder }),
    classes: [],
    unitTypes: [],
    weapons: [],
    items: [],
    maps: [],
    campaigns: [
      {
        id: "main",
        name: "Main",
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: nodes[0]!.id,
          nodes: [
            ...nodes,
            { id: "end", name: "End", kind: "terminal", transitions: [] }
          ] as unknown as [CampaignNode, ...CampaignNode[]]
        }
      }
    ]
  } as SourceProject;
}

describe("game settings", () => {
  it("offers exactly the turn orders the schema does, once each", () => {
    // The page and the board's control both read this list, so it drifting
    // from the schema would offer an author an order the compiler refuses.
    const schema = sourceV1Schemas.find(
      (candidate) => (candidate as { $id?: string }).$id ===
        "https://grandleon.dev/schemas/source/v1/project.schema.json"
    ) as { properties: { defaultTurnOrder: { enum: string[] } } };
    expect(TURN_ORDERS.map((order) => order.id))
      .toEqual(schema.properties.defaultTurnOrder.enum);
    expect(new Set(TURN_ORDERS.map((order) => order.label)).size)
      .toBe(TURN_ORDERS.length);
    expect(DEFAULT_TURN_ORDER).toBe("alternating");
  });

  it("starts a new game on side blocks, and writes it down", () => {
    // Two questions with two answers. What an absent field means belongs to the
    // format and is alternating, because that is what the compiler resolves and
    // what every package already compiled means. What a game started today
    // plays like is a choice, and a first battle where one activation hands the
    // turn over reads as broken, so a project made here states side blocks
    // rather than leaving the reader to the fallback.
    expect(NEW_PROJECT_TURN_ORDER).toBe("sideBlocks");
    expect(NEW_PROJECT_TURN_ORDER).not.toBe(DEFAULT_TURN_ORDER);
    const fresh = createSourceProject();
    expect(fresh.defaultTurnOrder).toBe("sideBlocks");
    expect(projectTurnOrder(fresh)).toBe("sideBlocks");
    // Stated, not implied: an author reading the file sees the setting, and the
    // settings page shows a chosen value rather than an empty menu.
    expect(Object.hasOwn(fresh, "defaultTurnOrder")).toBe(true);
  });

  it("reads a project that states no default as alternating", () => {
    // Not "the first entry of the menu", but the order a board takes when its
    // project states none, which is the compatibility claim in one line.
    expect(projectTurnOrder(project(undefined, [encounter("opening")])))
      .toBe("alternating");
  });

  it("gives a board that states nothing the game's default", () => {
    const game = project("initiative", [encounter("opening")]);
    expect(resolveTurnOrder(game, encounter("opening"))).toBe("initiative");
  });

  it("leaves a board that states its own order alone", () => {
    const game = project("initiative", [encounter("duel", "sideBlocks")]);
    expect(resolveTurnOrder(game, encounter("duel", "sideBlocks")))
      .toBe("sideBlocks");
    // Alternating is both the fallback and a real choice, so a board authoring
    // it must survive a game defaulting to something else.
    expect(resolveTurnOrder(game, encounter("duel", "alternating")))
      .toBe("alternating");
  });

  it("names every board that disagrees with the default", () => {
    const game = project("initiative", [
      encounter("opening"),
      encounter("duel", "sideBlocks"),
      encounter("siege", "alternating")
    ]);
    expect(turnOrderOverrides(game)).toEqual([
      {
        campaignId: "main",
        campaignName: "Main",
        nodeId: "duel",
        nodeName: "duel",
        turnOrder: "sideBlocks"
      },
      {
        campaignId: "main",
        campaignName: "Main",
        nodeId: "siege",
        nodeName: "siege",
        turnOrder: "alternating"
      }
    ]);
    expect(boardsFollowingDefault(game)).toBe(1);
  });

  it("counts only battles, because only a battle has a turn order", () => {
    const game = project(undefined, [encounter("opening")]);
    // The terminal node the fixture appends is not a battle and must not be
    // counted among the boards that follow the default.
    expect(boardsFollowingDefault(game)).toBe(1);
    expect(turnOrderOverrides(game)).toEqual([]);
  });

  it("words each order the same way everywhere it is offered", () => {
    expect(turnOrderLabel("initiative")).toBe("Everyone mixed together, fastest first");
    expect(turnOrderLabel(undefined)).toBe("");
  });
});
