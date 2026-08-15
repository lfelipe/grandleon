// SPDX-License-Identifier: MIT
/**
 * The words an author reads, in one place.
 *
 * The editor is used by somebody making a game, not by somebody who has read
 * the source format. So the surface has a vocabulary of its own, one word per
 * concept chosen for that person, and the format keeps its own words
 * underneath. A unit type is a **character**, a dialogue is a **scene**, and an
 * encounter node is a **Stage**.
 *
 * This module is the only place that rule is written down, and the only place
 * a stored keyword is turned into an author's word. Three copies of one
 * vocabulary is how a vocabulary drifts: a heading renamed in one file and a
 * refusal left saying something else is two words for one thing, which is
 * exactly the confusion the rule exists to prevent.
 *
 * **Storage keeps `encounter`.** The node kind is a value inside every authored
 * file, so renaming it would rewrite content and move package bytes. Surface
 * says Stage, storage says encounter, and `nodeKindWord` below is the seam
 * between them.
 */

import type { SourceCollectionName } from "./source-project-session";

/**
 * The word an author reads for one record of each collection.
 *
 * It is what a create button, a transaction label and a refusal are all named
 * from, so an author standing on Maps presses "Create map" and never "Create
 * record". "record" is what twelve collections have in common, which is to
 * say nothing anybody was thinking about. The type is exhaustive on purpose: a
 * thirteenth collection does not compile until it says what one of it is
 * called, so no collection can fall back to the generic word by being
 * forgotten.
 */
export const COLLECTION_WORD: Record<SourceCollectionName, string> = {
  classes: "class",
  unitTypes: "character",
  weaponTypes: "weapon type",
  weapons: "weapon",
  itemTypes: "item type",
  items: "item",
  maps: "map",
  factions: "faction",
  abilities: "ability",
  objectives: "objective",
  campaigns: "campaign",
  dialogues: "scene"
};

/** The kinds a campaign node can be, as the format stores them. */
export type NodeKind = "encounter" | "story" | "terminal";

/**
 * The author's word for a node kind: the one seam between the stored keyword
 * and the surface.
 *
 * `encounter` is the fight an author sets up on a map: a **Stage**. Nothing
 * else in the editor may spell that translation out; everything that needs the
 * word asks here.
 */
export function nodeKindWord(kind: NodeKind): string {
  return kind === "encounter" ? "Stage" : kind === "terminal" ? "ending" : "story";
}
