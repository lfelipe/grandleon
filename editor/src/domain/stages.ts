// SPDX-License-Identifier: MIT
/**
 * The Stages of a game: every fight an author has set up, and the ground each
 * is fought on.
 *
 * A `SourceMap` is `id`, `name`, `width`, `height`, `terrain` and knows nothing
 * about anybody: units on a board are `EncounterPlacement`s on an encounter node
 * inside a campaign's flow, and the only tie back to the ground is that node's
 * `mapId`. So a game's Stages cannot be read off its maps; they are found by
 * asking every campaign. That is this module. (`encounter` is the format's word
 * for the node an author calls a Stage; see `author-words.ts`, which owns that
 * translation.)
 *
 * A map is reusable ground: one map may be fought over several times, and each
 * of those is a Stage of its own. That is why a map has a list of Stages rather
 * than being one.
 *
 * **What is said before and what is said after are not the same shape**, and
 * this is the fact any surface for them has to be built on. Both clients
 * present a node's dialogues on arrival, before anything that node does:
 * `platform/client/src/campaign_session.cpp:682` calls
 * `present_dialogue_sequence` before the board is fought, and the browser's
 * `campaign-playtest-session.ts` does the same in the same order. So:
 *
 * - **before the Stage** is the encounter node's own `dialogueIds`;
 * - **after the Stage** is the `dialogueIds` of whatever node its transitions
 *   lead to, because the cursor only reaches those once the board is done.
 *
 * There is no third slot and none is needed.
 */

import type { SourceProject } from "../generated/source-v1";

/** One node a Stage leads to, and what arriving there says. */
export interface StageAftermath {
  readonly nodeId: string;
  readonly nodeName: string;
  readonly scenes: readonly string[];
}

export interface Stage {
  readonly campaignId: string;
  readonly campaignName: string;
  readonly nodeId: string;
  readonly nodeName: string;
  /** The ground it is fought on, or nothing while the author has not said. */
  readonly mapId: string | undefined;
  /** That ground's name, or nothing when no map is chosen or the map is gone. */
  readonly mapName: string | undefined;
  /** Scenes the node itself names: what is said on arriving, before the board. */
  readonly saidBefore: readonly string[];
  /** Scenes on the nodes this Stage leads to: what is said once it is done. */
  readonly saidAfter: readonly string[];
  /**
   * The same scenes, kept beside the node that owns them.
   *
   * A summary can read them as one sentence; a surface that lets an author
   * *change* what is said after cannot, because there is nothing here to
   * change: the scenes belong to the next node and are edited there. Naming
   * that node is the difference between reporting and stranding.
   */
  readonly after: readonly StageAftermath[];
  /** What winning and losing mean here, by name. */
  readonly winning: readonly string[];
  /** How many stand on the player's side, and how many against. */
  readonly yours: number;
  readonly theirs: number;
}

/** The names of dialogue records, in authored order, skipping any that are gone. */
function dialogueNames(
  project: SourceProject,
  ids: readonly string[] | undefined
): string[] {
  return (ids ?? []).flatMap((id) => {
    const found = (project.dialogues ?? []).find(
      (dialogue) => dialogue.id === id
    );
    return found ? [found.name] : [];
  });
}

/**
 * Every Stage in the game, in campaign then flow order.
 *
 * This is the list the Stages section is drawn from, and it deliberately
 * includes a Stage whose ground has not been chosen yet: a node can become a
 * Stage from the flow before anybody picks a map for it, and a list that hid
 * those would strand them somewhere no surface shows.
 *
 * A missing record is skipped rather than named by identifier: this is a
 * summary an author reads, and a dangling reference is a thing validation
 * reports rather than a thing a list should shout about.
 */
export function stagesInProject(project: SourceProject): readonly Stage[] {
  const stages: Stage[] = [];
  for (const campaign of project.campaigns ?? []) {
    const nodes = campaign.flow?.nodes ?? [];
    for (const node of nodes) {
      if (node.kind !== "encounter") continue;
      const placements = node.placements ?? [];
      // Every node this Stage can lead to, deduplicated: two transitions to
      // one node are two ways to the same conversation, not two conversations.
      const onward = new Set(
        node.transitions.map((transition) => transition.targetNodeId)
      );
      const after: StageAftermath[] = [];
      for (const target of onward) {
        const next = nodes.find((candidate) => candidate.id === target);
        if (!next || next.id === node.id) continue;
        after.push({
          nodeId: next.id,
          nodeName: next.name,
          scenes: dialogueNames(project, next.dialogueIds)
        });
      }
      stages.push({
        campaignId: campaign.id,
        campaignName: campaign.name,
        nodeId: node.id,
        nodeName: node.name,
        mapId: node.mapId,
        mapName: project.maps.find((map) => map.id === node.mapId)?.name,
        saidBefore: dialogueNames(project, node.dialogueIds),
        saidAfter: after.flatMap((entry) => entry.scenes),
        after,
        winning: (node.objectiveIds ?? []).flatMap((id) => {
          const found = (project.objectives ?? []).find(
            (objective) => objective.id === id
          );
          return found ? [found.name] : [];
        }),
        yours: placements.filter((placement) => placement.side === "first").length,
        theirs: placements.filter((placement) => placement.side === "second").length
      });
    }
  }
  return stages;
}

/** The Stages fought on one map, in the same order. */
export function stagesOnMap(
  project: SourceProject,
  mapId: string
): readonly Stage[] {
  return stagesInProject(project).filter((stage) => stage.mapId === mapId);
}

/** One Stage, by the campaign and node that hold it. */
export function stageAt(
  project: SourceProject,
  campaignId: string,
  nodeId: string
): Stage | undefined {
  return stagesInProject(project).find(
    (stage) => stage.campaignId === campaignId && stage.nodeId === nodeId
  );
}

/**
 * The Stages one objective decides, in flow order.
 *
 * An objective is a shared record referenced by identifier, but what it *means*
 * is "this is how that fight is won", and it means nothing at all away from a
 * fight that lists it. So this is how a problem reported at `/objectives/2` is
 * turned into somewhere an author can be taken: the board the condition is
 * about, where the win conditions are edited beside the people they name.
 *
 * More than one Stage may be decided by the same objective, since the format
 * allows it and a campaign that ends two fights the same way wants it, so this
 * is a list, and an empty one means an objective nothing uses.
 */
export function stagesDecidedBy(
  project: SourceProject,
  objectiveId: string
): readonly Stage[] {
  return stagesInProject(project).filter((stage) =>
    (project.campaigns ?? [])
      .find((campaign) => campaign.id === stage.campaignId)
      ?.flow?.nodes.find((node) => node.id === stage.nodeId)
      ?.objectiveIds?.includes(objectiveId) ?? false
  );
}

/** What a list of scene names reads as, or nothing when there are none. */
export function sceneSentence(
  names: readonly string[],
  when: "before" | "after"
): string {
  if (names.length === 0) {
    return when === "before"
      ? "Nothing is said before it."
      : "Nothing is said after it.";
  }
  const said = names.length === 1
    ? names[0]!
    : `${names.slice(0, -1).join(", ")} and ${names.at(-1)}`;
  return when === "before" ? `Before it: ${said}.` : `After it: ${said}.`;
}
