// SPDX-License-Identifier: MIT
import type { CampaignNode, SourceProject } from "../generated/source-v1";

/**
 * The settings that shape a whole game rather than one board.
 *
 * The turn order is the one of them a board may disagree with, so the rule for
 * reading it lives here and is read by everything that needs it: the settings
 * page, the board's own control, and the browser playtest. The compiler
 * resolves the same rule in `tools/game_content/src/source_project.cpp`, where
 * the encounter's one turn-order byte is decided; nothing here
 * writes a resolved value back into a project.
 */

export type TurnOrderId = NonNullable<SourceProject["defaultTurnOrder"]>;

/**
 * The order a Stage takes when nothing states one, which is what a board
 * whose project states no default plays as.
 */
export const DEFAULT_TURN_ORDER: TurnOrderId = "alternating";

/**
 * The order a project made in this editor is given, written into the file
 * rather than left to the fallback above.
 *
 * These are two different questions and they have two different answers. What
 * an absent field means is a fact about the format, decided in
 * `tools/game_content/src/source_project.cpp` and shared with every package
 * ever compiled; the editor is not free to answer it differently. What a game
 * an author starts today should play like is a choice, and alternating is the
 * wrong one: one activation hands the turn straight to the other side, so a
 * company of six moves one character and waits, which reads as a broken game
 * rather than as a rule. Side blocks lets a side move everybody it has before
 * the other side answers, which is what an author expects the first time they
 * press Play. It is stated in the project, so nothing about it is implicit and
 * an author can change it on the settings page like any other setting.
 */
export const NEW_PROJECT_TURN_ORDER: TurnOrderId = "sideBlocks";

/**
 * The menu, in the schema's order, worded the way an author reads it rather
 * than the way the enum spells it. One list, so the page and the board's own
 * control cannot offer different words for the same order.
 */
export const TURN_ORDERS: readonly {
  readonly id: TurnOrderId;
  readonly label: string;
}[] = [
  { id: "alternating", label: "Sides take turns, you pick who acts" },
  { id: "sideBlocks", label: "All of one side, then all of the other, in any order you pick" },
  { id: "initiative", label: "Everyone mixed together, fastest first" }
];

export function turnOrderLabel(id: TurnOrderId | undefined): string {
  return TURN_ORDERS.find((order) => order.id === id)?.label ?? id ?? "";
}

/**
 * The game's default, which is alternating when the project states none. An
 * absent field is never the same as a written one: this reads, and does not
 * write.
 */
export function projectTurnOrder(project: SourceProject): TurnOrderId {
  return project.defaultTurnOrder ?? DEFAULT_TURN_ORDER;
}

/** What a board actually runs under: its own order, or the game's. */
export function resolveTurnOrder(
  project: SourceProject,
  node: Pick<CampaignNode, "turnOrder">
): TurnOrderId {
  return node.turnOrder ?? projectTurnOrder(project);
}

/** One board that states a turn order of its own, and the one it states. */
export interface TurnOrderOverride {
  readonly campaignId: string;
  readonly campaignName: string;
  readonly nodeId: string;
  readonly nodeName: string;
  readonly turnOrder: TurnOrderId;
}

/**
 * Every Stage that states its own turn order, in the order an author reads
 * them. Changing the game's default moves none of these, so a page that offers
 * the change owes the author this list.
 */
export function turnOrderOverrides(
  project: SourceProject
): readonly TurnOrderOverride[] {
  const overrides: TurnOrderOverride[] = [];
  for (const campaign of project.campaigns ?? []) {
    for (const node of campaign.flow?.nodes ?? []) {
      if (node.kind !== "encounter" || node.turnOrder === undefined) continue;
      overrides.push({
        campaignId: campaign.id,
        campaignName: campaign.name,
        nodeId: node.id,
        nodeName: node.name,
        turnOrder: node.turnOrder
      });
    }
  }
  return overrides;
}

/** How many Stages follow the game's default rather than stating their own. */
export function boardsFollowingDefault(project: SourceProject): number {
  let following = 0;
  for (const campaign of project.campaigns ?? []) {
    for (const node of campaign.flow?.nodes ?? []) {
      if (node.kind === "encounter" && node.turnOrder === undefined) following += 1;
    }
  }
  return following;
}
