// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import { SourceProjectSession } from "./source-project-session";
import { planCharacterOnBoard } from "./stage-cast";
import {
  membersFieldedByNode,
  unenrolMembersNoLongerFielded,
  unfieldedMembers
} from "./campaign-company";

/**
 * What repeated authoring leaves behind in a campaign's company.
 *
 * Reported from a console: a campaign whose second Stage would not open, with
 * "1-7 of 34" in the corner of the management screen. Thirty-four is the size
 * of the company, and the author's own account was that units were "added and
 * removed non stop" while the Stage was being built.
 */
function fixture(): SourceProject {
  return {
    schemaVersion: "1.1.0",
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
  } as unknown as SourceProject;
}

/** Stamps somebody onto the author's own side, the way the palette does. */
function stamp(
  session: SourceProjectSession,
  name: string,
  x: number,
  y: number
): void {
  const plan = planCharacterOnBoard(session.snapshot(), {
    campaignId: "march",
    nodeId: "field",
    role: "knight",
    setting: "medieval",
    name,
    side: "first",
    x,
    y
  });
  if (plan.kind !== "cast") throw new Error(`refused: ${plan.kind}`);
  session.transact(`Put ${name} on the board`, plan.edits);
}

/**
 * Takes every placement off the board the way the workspace does when the Stage
 * editor saves a node: replace the node, then let anybody it stopped fielding
 * leave the company. This mirrors `saveStageNode`, which is the one door a
 * placement leaves a board through.
 */
function clearTheBoard(session: SourceProjectSession): readonly string[] {
  let left: readonly string[] = [];
  session.transact("Take everybody off the board", [{
    kind: "update",
    collection: "campaigns",
    id: "march",
    update: (draft: never) => {
      const campaign = draft as unknown as {
        flow?: { nodes: { id: string; placements?: unknown[] }[] };
        roster?: { id: string; name: string }[];
      };
      const node = campaign.flow?.nodes?.[0];
      const wereFielded = membersFieldedByNode(node as never);
      if (node) delete node.placements;
      left = unenrolMembersNoLongerFielded(campaign as never, wereFielded);
    }
  }] as never);
  return left;
}

describe("a company after a Stage has been authored and re-authored", () => {
  it("enrols somebody stamped on the author's own side", () => {
    const session = new SourceProjectSession(fixture());
    stamp(session, "Wren", 0, 0);
    const campaign = session.snapshot().campaigns![0]!;
    expect(campaign.roster).toHaveLength(1);
    expect(campaign.roster![0]!.name).toBe("Wren");
  });

  // The defect. Enrolling is half a gesture and nothing performs the other
  // half, so every character an author stands on their own side and then takes
  // off again stays in the company for good, invisible on the board that made
  // them and countable only on a console.
  it("keeps them in the company after they are taken off the board", () => {
    const session = new SourceProjectSession(fixture());
    stamp(session, "Wren", 0, 0);
    clearTheBoard(session);

    const campaign = session.snapshot().campaigns![0]!;
    const nodes = campaign.flow!.nodes as { placements?: unknown[] }[];
    expect(nodes[0]!.placements ?? []).toHaveLength(0);
    // Nobody stands anywhere, so nobody is in the company.
    expect(campaign.roster ?? []).toHaveLength(0);
  });

  // What the report looks like: a Stage worked on for an afternoon.
  it("grows the company by one for every attempt that was undone by hand", () => {
    const session = new SourceProjectSession(fixture());
    for (let attempt = 0; attempt < 12; attempt += 1) {
      stamp(session, `Try ${attempt}`, attempt % 4, Math.floor(attempt / 4));
      clearTheBoard(session);
    }
    stamp(session, "The keeper", 0, 0);

    const campaign = session.snapshot().campaigns![0]!;
    const nodes = campaign.flow!.nodes as { placements?: unknown[] }[];
    expect(nodes[0]!.placements ?? []).toHaveLength(1);
    // One person stands on the board, so the company is one person.
    expect(campaign.roster ?? []).toHaveLength(1);
  });

  it("says who left, so a company never quietly shrinks", () => {
    const session = new SourceProjectSession(fixture());
    stamp(session, "Wren", 0, 0);
    expect(clearTheBoard(session)).toEqual(["Wren"]);
  });

  // The guard that stops this being too eager. A member two Stages field is
  // still fielded when one of them lets them go, so they stay in the company.
  it("keeps somebody another Stage still fields", () => {
    const session = new SourceProjectSession(fixture());
    stamp(session, "Wren", 0, 0);
    const campaign = session.snapshot().campaigns![0]!;
    const member = campaign.roster![0]!.id;

    // A second Stage fielding the same member, added the way a flow grows.
    session.transact("A second Stage fields her too", [{
      kind: "update",
      collection: "campaigns",
      id: "march",
      update: (draft: never) => {
        const c = draft as unknown as {
          flow?: { nodes: Record<string, unknown>[] };
        };
        c.flow!.nodes.push({
          id: "second",
          name: "The second field",
          kind: "encounter",
          mapId: "meadow",
          objectiveIds: ["victory"],
          placements: [{
            id: "unit",
            memberId: member,
            unitTypeId: campaign.roster![0]!.unitTypeId,
            side: "first",
            x: 1,
            y: 1
          }],
          transitions: []
        });
      }
    }] as never);

    // Clearing the first board lets go of nobody: the second still fields her.
    expect(clearTheBoard(session)).toEqual([]);
    expect(session.snapshot().campaigns![0]!.roster ?? []).toHaveLength(1);
  });

  // What an existing project is already carrying. Reported rather than
  // removed: deleting people from a company on the next save would be a second
  // surprise on top of the first.
  it("reports members no board fields, without taking them out", () => {
    const session = new SourceProjectSession(fixture());
    for (let attempt = 0; attempt < 3; attempt += 1) {
      stamp(session, `Try ${attempt}`, attempt, 0);
    }
    // Somebody stops being fielded without the save path noticing, which is
    // the state every project authored before this carries.
    session.transact("A board that lost its placements", [{
      kind: "update",
      collection: "campaigns",
      id: "march",
      update: (draft: never) => {
        const c = draft as unknown as {
          flow?: { nodes: { placements?: unknown[] }[] };
        };
        const node = c.flow!.nodes[0]!;
        node.placements = (node.placements ?? []).slice(0, 1);
      }
    }] as never);

    const campaign = session.snapshot().campaigns![0]!;
    expect(campaign.roster ?? []).toHaveLength(3);
    expect(unfieldedMembers(campaign).map((member) => member.name))
      .toEqual(["Try 1", "Try 2"]);
  });
});
