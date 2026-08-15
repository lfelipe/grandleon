// SPDX-License-Identifier: MIT
import type { SourceCollectionName } from "./source-project-session";

/**
 * The places an author can be in this editor.
 *
 * Twelve flat collections is an abstraction hierarchy, not a workflow. These
 * sections group them by the question an author is actually answering, so the
 * jump from "I want an archer" to "I need a weapon type, a weapon, a class and
 * a unit type" is at least signposted.
 *
 * The list lives here rather than inside the workspace because two surfaces
 * read it, the navigation rail beside the workspace and the workspace itself,
 * and a rail that could disagree with the thing it navigates is worse than no
 * rail. Nothing here is Vue: it is data, so the property that matters most
 * (every collection has exactly one home) is an assertion rather than a hope.
 */

/**
 * What a section puts in the workspace body.
 *
 * `collections` sections show the record columns. The other four draw a page of
 * their own, because what they are about is not a list of records.
 *
 * `stages` and `flow` still **own** a collection even though they draw their
 * own page, and that is not a contradiction. Owning a collection is the answer
 * to "where does an author fix a problem reported at `/objectives/2/rounds`",
 * which is what `sectionOwning` reads, and the answer for an objective is the
 * Stage it decides, not a record column. So the ownership and the drawing are
 * two separate questions and this type answers only the second one.
 *
 * `settings` and `diagnostics` own nothing, because nothing in the project's
 * twelve collections is about either of them.
 */
export type WorkspaceSectionKind =
  | "settings"
  | "collections"
  | "stages"
  | "flow"
  | "diagnostics";

export interface WorkspaceSection {
  readonly id: string;
  /** The word an author reads on the rail. */
  readonly label: string;
  /** A line under the heading, saying what this place is for. */
  readonly hint: string;
  readonly kind: WorkspaceSectionKind;
  /**
   * The collections whose records this section is the home of, which is where
   * a reported problem about one of them sends the author. A section that draws
   * its own page still answers for the records that page is about.
   */
  readonly collections: readonly SourceCollectionName[];
}

export const WORKSPACE_SECTIONS: readonly WorkspaceSection[] = [
  // First, because an author who has just decided to make a game is answering
  // "what is this game?" before "who is in it?". The game's own name lives
  // here, where that question is asked.
  {
    id: "game",
    label: "Game",
    hint: "What it is called, how turns are ordered, how it is drawn.",
    kind: "settings",
    collections: []
  },
  {
    id: "characters",
    label: "Characters",
    hint: "Everybody in the game, and their classes, factions and abilities.",
    kind: "collections",
    collections: ["unitTypes", "classes", "factions", "abilities"]
  },
  {
    id: "equipment",
    label: "Weapons & items",
    hint: "What they carry.",
    kind: "collections",
    collections: ["weapons", "weaponTypes", "items", "itemTypes"]
  },
  // Ground, and only ground. A map holds nobody: units on a board belong to a
  // Stage, which points at the map rather than the map pointing at it. One map
  // can be fought over several times, so drawing terrain and setting up a fight
  // are two questions and this section answers exactly the first one.
  {
    id: "maps",
    label: "Maps",
    hint: "The ground they fight over. Maps can be reused in several Stages.",
    kind: "collections",
    collections: ["maps"]
  },
  // The second half of that pair, and the only place a fight is set up. It
  // draws a page of its own because a Stage is not a record: it is a node in a
  // campaign's flow, and the flow is stored on the campaign.
  //
  // It owns `objectives` because an objective is *what winning this Stage
  // means*. The record is shared and referenced by identifier, but the question
  // it answers is asked about one fight, and it is asked and answered here, on
  // the win conditions beside the board. A list of objective records on their
  // own is a list of answers with the questions taken away.
  {
    id: "stages",
    label: "Stages",
    hint: "A fight on a map: who stands where, and what winning means.",
    kind: "stages",
    collections: ["objectives"]
  },
  // What is said, on its own, because it is neither ground nor a fight nor the
  // shape of a campaign. A scene is written once and played wherever it is
  // wanted: on the way into a Stage, between two of them, or at the end. It
  // sits beside Stages because that is what most scenes are attached to.
  {
    id: "scenes",
    label: "Scenes",
    hint: "What is said, and who says it. Where each one plays is set on a Stage.",
    kind: "collections",
    collections: ["dialogues"]
  },
  // Named for what it decides rather than for the record it is stored on: a
  // campaign's flow is how one Stage leads to the next, and that is the thing
  // an author comes here to arrange. What happens *inside* a Stage is not here,
  // and neither is what winning one means nor what is said around it.
  {
    id: "flow",
    label: "Flow",
    hint:
      "Every stop on the road and the ways out of them. Drag a way out onto " +
      "another stop to send the road there.",
    kind: "flow",
    collections: ["campaigns"]
  },
  {
    id: "diagnostics",
    label: "Diagnostics",
    hint: "What the editor and an old console make of the game as it stands.",
    kind: "diagnostics",
    collections: []
  }
];

/** The section an author lands on. */
export const DEFAULT_WORKSPACE_SECTION = WORKSPACE_SECTIONS[0]!.id;

/** The section by id, falling back to the one an author lands on. */
export function workspaceSection(id: string): WorkspaceSection {
  return WORKSPACE_SECTIONS.find((section) => section.id === id) ??
    WORKSPACE_SECTIONS[0]!;
}

/**
 * The section that owns a collection, or nothing for a name that is not one.
 * This is what lets a problem reported at `/unitTypes/0/classId` know which
 * place to open.
 */
export function sectionOwning(
  collection: SourceCollectionName
): WorkspaceSection | undefined {
  return WORKSPACE_SECTIONS.find((section) =>
    section.collections.includes(collection)
  );
}
