// SPDX-License-Identifier: MIT
/**
 * Putting somebody on a Stage's board when the game has not got them yet.
 *
 * "Put a bandit here" is one thing a person does. Underneath, it can be five
 * records: a weapon type, a weapon, a class, the character, and the Stage that
 * now places them, plus a faction, and a member of the company when the tile
 * is on the player's own side. An author who has to make four of those on other
 * screens before the fifth one is possible has been handed the format's problem
 * to solve, which is the one thing the editor is for not doing.
 *
 * So this module makes the whole decision and **writes nothing**. It returns
 * the edits, in dependency order, for one `SourceProjectSession.transact` call.
 * That is not tidiness: it is what makes the act undoable in one press and
 * makes a refusal total. Either the author gets a bandit standing on the tile,
 * or they get a sentence and a project nothing touched, never a class and a
 * weapon left behind with no character holding them.
 *
 * **Find before make**, the same rule `planStageOnMap` keeps next door. The
 * weapon type and the class are shared with whatever already answers to them,
 * because a class is an archetype and ten bandits are one; that decision lives
 * in `buildCharacterChain` and this module does not second-guess it. What is
 * always new is the character, because the character is the thing the author
 * asked for.
 *
 * **The side becomes a fact about the character, not about the press.** A
 * character put down as an enemy joins the enemy faction, so the next board
 * that picks them up already knows whose side they are on and never asks
 * again. The faction is an ordinary record from the moment it exists.
 */

import type {
  CampaignNode,
  CampaignRosterMember,
  EncounterPlacement,
  SourceFaction,
  SourceProject
} from "../generated/source-v1";
import {
  buildCharacterChain,
  type CatalogueSetting,
  type CharacterRole
} from "./character-recipe";
import { SIDE_FACTIONS } from "./character-standing";
import type { SourceEdit } from "./source-project-session";

/** What the author asked for: this role, under this name, on this tile. */
export interface CastAsk {
  readonly campaignId: string;
  readonly nodeId: string;
  readonly role: CharacterRole;
  readonly setting: CatalogueSetting;
  /** What to call them. Empty takes the catalogue's own word for the role. */
  readonly name: string;
  readonly side: "first" | "second";
  readonly x: number;
  readonly y: number;
}

export interface CastPlan {
  readonly kind: "cast";
  /** Everything the act writes, in dependency order, for one transaction. */
  readonly edits: readonly SourceEdit[];
  /** The character now standing there. */
  readonly unitTypeId: string;
  readonly unitTypeName: string;
  readonly placementId: string;
  readonly summary: string;
}

/** Why nobody can be put there, in words an author reads. */
export interface CastRefusal {
  readonly kind: "refused";
  readonly reason: string;
}

/** Whose side, in the words the board uses. */
function sideWord(side: "first" | "second"): string {
  return side === "first" ? "your side" : "the enemy";
}

/** An identifier not already taken, with a numeric suffix when it is. */
function freeId(stem: string, taken: readonly string[]): string {
  if (!taken.includes(stem)) return stem;
  let suffix = 2;
  while (taken.includes(`${stem}_${suffix}`)) suffix += 1;
  return `${stem}_${suffix}`;
}

/**
 * Plans putting a character the game has not got onto a Stage's board.
 *
 * Everything it needs to refuse over is checked before anything is built, so a
 * refusal names one reason rather than the first of several.
 */
export function planCharacterOnBoard(
  project: SourceProject,
  ask: CastAsk
): CastPlan | CastRefusal {
  const campaign = (project.campaigns ?? []).find(
    (candidate) => candidate.id === ask.campaignId
  );
  if (!campaign) {
    return {
      kind: "refused",
      reason:
        `There is no campaign '${ask.campaignId}' in this game any more, so ` +
        "this Stage has nowhere to put anybody."
    };
  }
  const node = (campaign.flow?.nodes ?? []).find(
    (candidate) => candidate.id === ask.nodeId
  );
  if (!node) {
    return {
      kind: "refused",
      reason: `That Stage is no longer in ${campaign.name}.`
    };
  }
  const placements = node.placements ?? [];
  if (placements.some((placement) =>
    placement.x === ask.x && placement.y === ask.y
  )) {
    return {
      kind: "refused",
      reason:
        `Somebody already stands on column ${ask.x + 1}, row ${ask.y + 1}. ` +
        "Choose an empty tile."
    };
  }

  const chain = buildCharacterChain(project, ask.role, ask.name, ask.setting);
  const edits: SourceEdit[] = [];

  // The side, as an ordinary faction. Made on first use and reused after,
  // exactly as the character wizard makes it: "the enemy" is one side however
  // many enemies stand on it.
  const side = SIDE_FACTIONS.find(
    (faction) => faction.id === (ask.side === "first" ? "your_side" : "the_enemy")
  )!;
  if (!(project.factions ?? []).some((faction) => faction.id === side.id)) {
    const record: SourceFaction = {
      id: side.id,
      name: side.name,
      color: side.color
    };
    edits.push({ kind: "create", collection: "factions", record });
  }

  if (chain.weaponType) {
    edits.push({
      kind: "create",
      collection: "weaponTypes",
      record: chain.weaponType
    });
  }
  edits.push({ kind: "create", collection: "weapons", record: chain.weapon });
  if (chain.unitClass) {
    edits.push({
      kind: "create",
      collection: "classes",
      record: chain.unitClass
    });
  }
  edits.push({
    kind: "create",
    collection: "unitTypes",
    record: { ...chain.unitType, factionId: side.id }
  });

  // A placement on the player's own side fields a member of the company: your
  // people carry what they learned between Stages, and a placement naming
  // nobody has nobody to carry it. The character is new, so there is nobody in
  // the company to find and one joins, which the summary says out loud,
  // because a company that quietly grew is a surprise.
  const enrolled: CampaignRosterMember | undefined = ask.side === "first"
    ? {
      id: freeId(chain.unitType.id, [
        ...(campaign.roster ?? []).map((member) => member.id),
        ...(campaign.flow?.nodes ?? []).flatMap(
          (candidate) => (candidate.recruits ?? []).map((member) => member.id)
        )
      ]),
      name: chain.unitType.name,
      unitTypeId: chain.unitType.id
    }
    : undefined;

  const placement: EncounterPlacement = {
    id: freeId("unit", placements.map((entry) => entry.id)),
    ...(enrolled ? { memberId: enrolled.id } : {}),
    unitTypeId: chain.unitType.id,
    side: ask.side,
    x: ask.x,
    y: ask.y
  };

  const nextNode: CampaignNode = {
    ...structuredClone(node),
    placements: [...placements.map((entry) => structuredClone(entry)), placement]
  };

  edits.push({
    kind: "update",
    collection: "campaigns",
    id: campaign.id,
    update: (draft) => {
      if (enrolled) {
        draft.roster = [...(draft.roster ?? []), structuredClone(enrolled)];
      }
      const flow = draft.flow;
      if (!flow) return;
      flow.nodes = flow.nodes.map(
        (candidate) => candidate.id === nextNode.id ? nextNode : candidate
      ) as typeof flow.nodes;
    }
  });

  const joined = enrolled
    ? ` Your side is fought by the company, so ${enrolled.name} joined it. ` +
      "They are an ordinary member you can rename or remove."
    : "";
  return {
    kind: "cast",
    edits,
    unitTypeId: chain.unitType.id,
    unitTypeName: chain.unitType.name,
    placementId: placement.id,
    summary:
      `Made ${chain.unitType.name} and put them on column ${ask.x + 1}, row ` +
      `${ask.y + 1}, fighting for ${sideWord(ask.side)}.${joined}`
  };
}
