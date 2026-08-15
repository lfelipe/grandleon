// SPDX-License-Identifier: MIT
/**
 * Making a Stage on a map.
 *
 * An author wants "a fight on this ground". The format has no such record: a
 * Stage is an encounter node, inside a campaign's flow, inside a campaign, and
 * it points at the ground rather than the ground pointing at it. So the thing
 * the author wants is four decisions deep, and made by hand it costs six
 * correct guesses through Flow, a fresh flow whose only node is an ending, and
 * a button named for the record it writes rather than the Stage it leads to.
 *
 * This module makes the four decisions. It writes nothing: it returns a plan,
 * which is either **one whole campaign record to create** or **one whole flow
 * to put on a campaign that already exists**. That is deliberate. Both are a
 * single `SourceProjectSession` call, so the four decisions land as one
 * transaction and there is no half-made Stage to be left holding. The author
 * either gets the campaign, the flow, the node and its way out, or gets a
 * sentence saying why not and a project nothing touched.
 *
 * **The fifth answer is "you already have one".** The front door finds before
 * it makes, so asking twice for a Stage on the same ground opens the one the
 * first ask made. A map really can be fought over more than once, since the
 * format allows it and campaigns want it, but that is a second decision and it is
 * asked for on a control of its own, by `intent`. A button that made a Stage
 * every time it was pressed would make three for an author who checked whether
 * the first press had landed by pressing it again.
 *
 * **What it refuses to invent.** It never guesses who fights. A fresh Stage has
 * an empty board, because who stands where is the author's question and the
 * board beside this is where they answer it. What it does guarantee is that the
 * flow around the Stage is whole: the node is reachable from the entry node and
 * it leads somewhere, because a flow failing either is refused before it can be
 * saved.
 */

import type {
  CampaignFlow,
  CampaignNode,
  SourceCampaign,
  SourceProject
} from "../generated/source-v1";
// The one rule the editor derives an identifier by, borrowed under the name
// this module reads best with. A Stage names a campaign, a node and a map,
// and all three go through it.
import { identifierFromName as idFromName } from "./source-form-model";

/** A plan that creates the campaign the Stage needs. */
export interface CreateCampaignPlan {
  readonly kind: "createCampaign";
  /** The whole record, flow and all, ready for one `create` call. */
  readonly campaign: SourceCampaign;
  readonly campaignId: string;
  readonly nodeId: string;
  readonly summary: string;
}

/** A plan that puts the Stage into a campaign the project already has. */
export interface ExtendCampaignPlan {
  readonly kind: "extendCampaign";
  readonly campaignId: string;
  readonly campaignName: string;
  /** The whole flow, ready for one `update` call that assigns it. */
  readonly flow: CampaignFlow;
  readonly nodeId: string;
  readonly summary: string;
}

/**
 * A plan that writes nothing, because the Stage the author asked for is
 * already here.
 *
 * This is what stops the front door being a duplicator. Pressing it is a
 * question, "let me set up a fight on this ground", and the honest answer to
 * asking it twice is the Stage from the first press, not a second one nobody
 * asked for.
 */
export interface OpenStagePlan {
  readonly kind: "openExisting";
  readonly campaignId: string;
  readonly nodeId: string;
  readonly summary: string;
}

/** Why no Stage can be made, in words an author reads. */
export interface StagePlanRefusal {
  readonly kind: "refused";
  readonly reason: string;
}

export type StagePlan =
  | CreateCampaignPlan
  | ExtendCampaignPlan
  | OpenStagePlan
  | StagePlanRefusal;

/**
 * What the author asked for, which is not always "make me one".
 *
 * `findOrMake` is the front door: give me a Stage on this ground, and the one
 * already here counts. `another` is the deliberate second act, for the map that
 * really is fought over twice: a different word on a different control,
 * because a press that could mean either would mean the wrong one every time
 * somebody clicked twice.
 */
export type StageIntent = "findOrMake" | "another";

/** An identifier not already taken, with a numeric suffix when it is. */
function freeId(prefix: string, taken: readonly string[]): string {
  if (!taken.includes(prefix)) return prefix;
  let suffix = 2;
  while (taken.includes(`${prefix}_${suffix}`)) suffix += 1;
  return `${prefix}_${suffix}`;
}

/**
 * The node a new Stage hangs from: one reachable from the entry node that
 * leads nowhere, which is where this flow currently stops.
 *
 * Reachability is what decides it. A node the entry node cannot reach is
 * already a problem the flow editor reports, and hanging a Stage off one would
 * make the Stage unreachable too, so the walk starts at the entry node and
 * only ever considers what it finds.
 */
function endOfTheLine(flow: CampaignFlow): CampaignNode | undefined {
  const byId = new Map(flow.nodes.map((node) => [node.id, node]));
  const seen = new Set<string>();
  const pending = [flow.entryNodeId];
  const ends: CampaignNode[] = [];
  while (pending.length > 0) {
    const id = pending.shift()!;
    if (seen.has(id)) continue;
    seen.add(id);
    const node = byId.get(id);
    if (!node) continue;
    if (node.transitions.length === 0) ends.push(node);
    for (const transition of node.transitions) {
      if (byId.has(transition.targetNodeId)) pending.push(transition.targetNodeId);
    }
  }
  // Flow order among the ends, not walk order: an author reads their nodes in
  // the order they wrote them, and a plan that picked a different one each time
  // the walk happened to reorder would be a plan nobody could predict.
  return flow.nodes.find((node) => ends.some((end) => end.id === node.id));
}

/** The Stage node itself: this ground, nobody on it yet, and a way out. */
function stageNode(
  id: string,
  mapName: string,
  mapId: string,
  endingId: string
): CampaignNode {
  return {
    id,
    name: `Stage at ${mapName}`,
    kind: "encounter",
    mapId,
    transitions: [{ id: "next", targetNodeId: endingId, priority: 0 }]
  };
}

/** The ending it leads to, so the flow has somewhere to finish. */
function endingNode(id: string, mapName: string): CampaignNode {
  return {
    id,
    name: `After ${mapName}`,
    kind: "terminal",
    transitions: []
  };
}

/**
 * The plan for a Stage fought on one map.
 *
 * `campaignId` names which campaign to join when the project has more than one;
 * without it the first campaign is used, which is the only campaign in every
 * project that has exactly one.
 *
 * `intent` decides what an author who already has a Stage here is asking for.
 * The default is the safe reading: find the one that exists rather than write
 * a second. Only `another` writes when the ground is already fought over, and
 * only a control saying so in as many words should pass it.
 */
export function planStageOnMap(
  project: SourceProject,
  mapId: string,
  campaignId?: string,
  intent: StageIntent = "findOrMake"
): StagePlan {
  const map = project.maps.find((candidate) => candidate.id === mapId);
  if (!map) {
    return {
      kind: "refused",
      reason:
        `There is no map '${mapId}' in this game, so there is no ground to ` +
        "fight over. Choose a map that exists."
    };
  }

  const campaigns = project.campaigns ?? [];
  const campaign = campaignId === undefined
    ? campaigns[0]
    : campaigns.find((candidate) => candidate.id === campaignId);
  if (campaignId !== undefined && !campaign) {
    return {
      kind: "refused",
      reason:
        `There is no campaign '${campaignId}' in this game. Choose one that ` +
        "is still here, or let a new one be made."
    };
  }

  // Already fought over. Nothing is written and the Stage that exists is
  // named back, so a second press of the front door lands the author on the
  // board the first press made rather than on a copy of it.
  if (campaign && intent === "findOrMake") {
    const fought = (campaign.flow?.nodes ?? []).find(
      (node) => node.kind === "encounter" && node.mapId === mapId
    );
    if (fought) {
      return {
        kind: "openExisting",
        campaignId: campaign.id,
        nodeId: fought.id,
        summary:
          `${campaign.name} already fights at ${map.name}, so ${fought.name} ` +
          "is open below rather than a second Stage being made. To fight " +
          "here twice, add another Stage."
      };
    }
  }

  // Nothing to join: the Stage becomes the campaign's first node, which is
  // what a campaign made for a Stage should open on. `CampaignFlowEditor`'s
  // own `createFlow` writes a lone ending instead, and this is the
  // difference between a front door and a dropdown.
  if (!campaign) {
    const name = project.title.trim() === ""
      ? "Campaign"
      : project.title.trim();
    const id = freeId(
      idFromName(name, "campaign"),
      campaigns.map((candidate) => candidate.id)
    );
    return {
      kind: "createCampaign",
      campaignId: id,
      nodeId: "stage",
      campaign: {
        id,
        name,
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "stage",
          nodes: [
            stageNode("stage", map.name, map.id, "ending"),
            endingNode("ending", map.name)
          ]
        }
      },
      summary:
        `Made ${name}, a campaign that opens on a Stage at ${map.name} and ` +
        "finishes after it. Both are ordinary records you can change."
    };
  }

  const existing = campaign.flow;
  // A campaign without a flow is the same shape as no campaign at all, except
  // that the company and the store it already holds are kept.
  if (!existing) {
    return {
      kind: "extendCampaign",
      campaignId: campaign.id,
      campaignName: campaign.name,
      nodeId: "stage",
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "stage",
        nodes: [
          stageNode("stage", map.name, map.id, "ending"),
          endingNode("ending", map.name)
        ]
      },
      summary:
        `${campaign.name} had no order of events yet, so it now opens on a ` +
        `Stage at ${map.name} and finishes after it.`
    };
  }

  // Hanging it on the end of the line is the one change this plan makes to
  // what an author already wrote, and it is the only way a new node can be
  // reached at all. A flow that never stops, where every node the entry
  // reaches leads on to another, has no end to hang from, and inventing one
  // would mean choosing which of the author's branches to cut into.
  const tail = endOfTheLine(existing);
  if (!tail) {
    return {
      kind: "refused",
      reason:
        `${campaign.name} has no point where it stops, so there is nowhere ` +
        "to add a Stage after. Open it under Flow and give it an ending, or " +
        "make the Stage in a campaign of its own."
    };
  }

  const ids = existing.nodes.map((node) => node.id);
  const nodeId = freeId(idFromName(`stage_${map.name}`, "stage"), ids);
  const endingId = freeId(idFromName(`after_${map.name}`, "after"), [
    ...ids,
    nodeId
  ]);
  const stage = stageNode(nodeId, map.name, map.id, endingId);
  const ending = endingNode(endingId, map.name);

  // The change to the tail is said in the summary rather than done quietly.
  const nodes = existing.nodes.map((node) => {
    if (node.id !== tail.id) return structuredClone(node);
    const changed = structuredClone(node);
    // A node that leads somewhere is not an ending any more. `terminal` is the
    // only kind the format refuses transitions on, so it is the only one that
    // has to change, and a Stage keeps being a Stage.
    if (changed.kind === "terminal") changed.kind = "story";
    changed.transitions = [
      ...changed.transitions,
      {
        id: freeId(
          "next",
          changed.transitions.map((transition) => transition.id)
        ),
        targetNodeId: nodeId,
        priority: changed.transitions.reduce(
          (highest, transition) => Math.max(highest, transition.priority + 1),
          0
        )
      }
    ];
    return changed;
  });

  const followed =
    ` It follows ${tail.name}, which used to be where ${campaign.name} stopped.`;
  return {
    kind: "extendCampaign",
    campaignId: campaign.id,
    campaignName: campaign.name,
    nodeId,
    flow: {
      contractVersion: "1.0.0",
      entryNodeId: existing.entryNodeId,
      // The list is never empty, the flow it extends already holding the tail
      // this Stage hangs from, but the type says "at least one" by position
      // and a spread cannot prove that.
      nodes: [nodes[0]!, ...nodes.slice(1), stage, ending]
    },
    summary:
      `Added a Stage at ${map.name} to ${campaign.name}.${followed}`
  };
}
