// SPDX-License-Identifier: MIT
/**
 * Whose side a character is on, and whether anything is depending on them.
 *
 * Both questions are answered from the project as it stands rather than from a
 * field somebody had to remember to set, because both already have answers the
 * format gives and neither has ever been shown to an author.
 *
 * **Whose side.** A character carries no side; a placement does. What a
 * character does carry is an optional `factionId`, and a faction already
 * decides the colour that character is drawn in on every board and in every ROM
 * (`board-art.ts`). So "one of yours" and "an enemy" are written as two
 * ordinary faction records, made on first use and reused after. Nothing here
 * adds a field, and an author who renames or deletes those factions has renamed
 * or deleted a faction. There is no second meaning hiding behind them.
 *
 * **Who matters.** The characters a game is about are set against the ones who
 * are temporary to a single map, whose identity nothing depends on. That is
 * exactly the distinction between a character something else names and a
 * character that is only ever placed. Three bandits on one board are three
 * placements of one character. That is computed here rather than declared, so
 * it cannot go stale.
 */

import type { SourceProject } from "../generated/source-v1";

/** A side an author can put a character on, as an ordinary faction record. */
export interface SideFaction {
  readonly id: string;
  readonly name: string;
  readonly color: "blue" | "red";
  /** The words an author reads while choosing it. */
  readonly label: string;
  readonly summary: string;
}

/**
 * The two sides, declared once.
 *
 * Blue and red because those are the colours the board already gives the first
 * and second sides when no faction claims a character, so a game that uses
 * these looks like the game that does not.
 */
export const SIDE_FACTIONS: readonly SideFaction[] = [
  {
    id: "your_side",
    name: "Your side",
    color: "blue",
    label: "One of yours",
    summary: "Kept between Stages, with their own wounds and experience."
  },
  {
    id: "the_enemy",
    name: "The enemy",
    color: "red",
    label: "An enemy",
    summary: "Never part of the company."
  }
];

export const UNALIGNED_LABEL = "Neither for now";
export const UNALIGNED_SUMMARY = "A Stage can still put them on either side.";

/** The side a faction identifier stands for, or nothing for any other faction. */
export function sideFaction(factionId: string | undefined): SideFaction | undefined {
  return SIDE_FACTIONS.find((faction) => faction.id === factionId);
}

/**
 * Whose side a character is on, in the words an author reads.
 *
 * A faction this module does not know is named rather than ignored: an author
 * who wrote their own factions is telling the truth about their game, and this
 * has no business overruling them.
 */
export function characterSide(
  project: SourceProject,
  factionId: string | undefined
): string {
  const known = sideFaction(factionId);
  if (known) return known.label;
  if (factionId === undefined) return "Not on a side yet";
  return (
    (project.factions ?? []).find((faction) => faction.id === factionId)?.name ??
    factionId
  );
}

/** Why something in the project depends on a character being who they are. */
export type StandingReason =
  | "company"
  | "recruit"
  | "objective"
  | "talk";

export interface CharacterStanding {
  /**
   * `named`: something depends on this character being who they are.
   * `extra`: placed on at least one board and named by nothing.
   * `unused`: on no board at all.
   */
  readonly kind: "named" | "extra" | "unused";
  /** How many placements across the whole project stand as this character. */
  readonly placements: number;
  /** How many distinct Stages place them. */
  readonly boards: number;
  /** Why they are named, in the order the reasons are checked. Empty otherwise. */
  readonly reasons: readonly StandingReason[];
}

/**
 * Each reason as a verb phrase that follows the character's name.
 *
 * They are phrased this way rather than as nouns after "is" because the
 * sentence joins several of them, and a frame of "X is ..." only reads for
 * some: loading the Tarnholt sample produced "Ashen Levy is somebody can talk
 * to them" and "Warden Kesh is somebody a Stage is won or lost over". A phrase
 * that has to work in a list has to carry its own verb.
 */
const REASON_WORDS: Record<StandingReason, string> = {
  company: "marches with a campaign's company",
  recruit: "joins the company along the way",
  objective: "is somebody a Stage is won or lost over",
  talk: "is somebody a character can talk to"
};

/**
 * What the project makes of one character.
 *
 * Every campaign, every node, every placement and every objective is walked
 * once. The project is small by construction, the format capping campaigns at
 * ten thousand and placements at four thousand a board, and this is asked once per
 * card, so a walk is cheaper than an index that could fall behind an edit.
 */
export function characterStanding(
  project: SourceProject,
  unitTypeId: string
): CharacterStanding {
  const reasons = new Set<StandingReason>();
  let placements = 0;
  let boards = 0;

  for (const campaign of project.campaigns ?? []) {
    // A member of the founding company is a person the campaign keeps.
    if ((campaign.roster ?? []).some((member) => member.unitTypeId === unitTypeId)) {
      reasons.add("company");
    }
    for (const node of campaign.flow?.nodes ?? []) {
      if ((node.recruits ?? []).some((member) => member.unitTypeId === unitTypeId)) {
        reasons.add("recruit");
      }
      const standing = (node.placements ?? []).filter(
        (placement) => placement.unitTypeId === unitTypeId
      );
      if (standing.length === 0) continue;
      placements += standing.length;
      boards += 1;
      for (const placement of standing) {
        // Fielding a member of the company is the placement saying "this is
        // that person", which is the whole of what makes them not an extra.
        if (placement.memberId !== undefined) reasons.add("company");
        if (placement.talk !== undefined) reasons.add("talk");
        if ((project.objectives ?? []).some(
          (objective) => objective.targetPlacementId === placement.id
        )) {
          reasons.add("objective");
        }
      }
    }
  }

  const ordered = (["company", "recruit", "objective", "talk"] as const).filter(
    (reason) => reasons.has(reason)
  );
  return {
    kind: ordered.length > 0 ? "named" : placements > 0 ? "extra" : "unused",
    placements,
    boards,
    reasons: ordered
  };
}

/**
 * Whether this character is one person rather than a kind.
 *
 * The company is what decides it, and only the company: somebody a campaign
 * holds by name, in the roster it is founded with or in the recruits a node
 * hands it, is a person, and a person is in one place at a time. Everybody
 * else is a kind. Three bandits are three placements of one Bandit, and a
 * board may hold as many as the author likes.
 *
 * This is deliberately narrower than `characterStanding`'s `named`. Being
 * talked to and being what a Stage is won over are facts about **one placement
 * on one board**, so a second placement of the same character is a different
 * character in the fiction and nothing about it is a contradiction. Being in
 * the company is a fact about the character, and two of them is one person
 * standing in two places, which is the thing that cannot happen.
 *
 * Nothing is asked and nothing is stored: an author who adds somebody to a
 * company has said this, and an author who deletes them has unsaid it.
 */
export function characterIsOnePerson(
  project: SourceProject,
  unitTypeId: string
): boolean {
  return (project.campaigns ?? []).some((campaign) =>
    (campaign.roster ?? []).some((member) => member.unitTypeId === unitTypeId) ||
    (campaign.flow?.nodes ?? []).some((node) =>
      (node.recruits ?? []).some((member) => member.unitTypeId === unitTypeId)
    )
  );
}

/** How many boards, said as a phrase rather than a number and a noun. */
function boardCount(boards: number): string {
  return boards === 1 ? "one Stage" : `${boards} Stages`;
}

/**
 * The standing as a sentence an author reads.
 *
 * Written about the character rather than about the data: "nobody depends on
 * them" is the thing an author wants to know before deleting somebody, and
 * "extra" is a word for the editor's own reasoning rather than for a card.
 */
export function standingSentence(
  name: string,
  standing: CharacterStanding
): string {
  if (standing.kind === "unused") {
    return `${name} is not in any Stage yet.`;
  }
  if (standing.kind === "extra") {
    const many = standing.placements === 1 ? "once" : `${standing.placements} times`;
    return (
      `${name} stands in ${boardCount(standing.boards)}, ${many} in all, and ` +
      "nothing depends on which of them is which."
    );
  }
  const because = standing.reasons.map((reason) => REASON_WORDS[reason]);
  const where = standing.boards === 0
    ? "and no Stage places them yet"
    : `and stands in ${boardCount(standing.boards)}`;
  return `${name} ${[...because, where].join(", ")}.`;
}
