// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  DEFAULT_WORKSPACE_SECTION,
  sectionOwning,
  WORKSPACE_SECTIONS,
  workspaceSection
} from "./workspace-sections";
import type { SourceCollectionName } from "./source-project-session";

// Every collection the editor can edit. Written out rather than derived, so
// adding one to the schema and forgetting to give it a home is a failure here
// rather than a collection an author can never reach.
const EVERY_COLLECTION: readonly SourceCollectionName[] = [
  "classes", "unitTypes", "weaponTypes", "weapons", "itemTypes", "items",
  "maps", "factions", "abilities", "objectives", "campaigns", "dialogues"
];

describe("workspace sections", () => {
  it("gives every collection exactly one home", () => {
    for (const collection of EVERY_COLLECTION) {
      const homes = WORKSPACE_SECTIONS.filter((section) =>
        section.collections.includes(collection)
      );
      expect(homes.map((home) => home.id), collection).toHaveLength(1);
    }
  });

  it("lists no collection that is not one, and no empty record section", () => {
    for (const section of WORKSPACE_SECTIONS) {
      for (const collection of section.collections) {
        expect(EVERY_COLLECTION, section.id).toContain(collection);
      }
      // Owning a collection and drawing the record columns are two different
      // questions. `settings` and `diagnostics` are about no record at all, so
      // they own nothing; every other section is the home of at least one, and
      // that is what tells a reported problem which place to open.
      const ownsNothing =
        section.kind === "settings" || section.kind === "diagnostics";
      expect(section.collections.length === 0, section.id).toBe(ownsNothing);
    }
  });

  it("gives ground, fights, what is said and the road one entry each", () => {
    expect(WORKSPACE_SECTIONS.map((section) => section.label)).toEqual([
      "Game", "Characters", "Weapons & items", "Maps", "Stages", "Scenes",
      "Flow", "Diagnostics"
    ]);
    // Stages comes straight after Maps, because the second question an author
    // asks about a map is who fights on it; Scenes straight after Stages,
    // because what is said is said around a fight.
    const ids = WORKSPACE_SECTIONS.map((section) => section.id);
    expect(ids.indexOf("stages")).toBe(ids.indexOf("maps") + 1);
    expect(ids.indexOf("scenes")).toBe(ids.indexOf("stages") + 1);
    expect(ids.indexOf("flow")).toBe(ids.indexOf("scenes") + 1);
    // A Stage is a node inside a campaign's flow, not a record, so its section
    // draws a page of its own rather than the record columns, and a campaign's
    // shape is a graph rather than a form, so Flow does the same.
    expect(workspaceSection("stages").kind).toBe("stages");
    expect(workspaceSection("flow").kind).toBe("flow");
    // An author lands on what the game is, not on a list of characters.
    expect(DEFAULT_WORKSPACE_SECTION).toBe("game");
    expect(workspaceSection(DEFAULT_WORKSPACE_SECTION).kind).toBe("settings");
  });

  it("sends what is said and what winning means out of Flow", () => {
    // The three complaints this answers, as assertions: scenes are their own
    // place, an objective belongs to the fight it decides, and Flow is left
    // with the one thing it is named for.
    expect(sectionOwning("dialogues")?.id).toBe("scenes");
    expect(sectionOwning("objectives")?.id).toBe("stages");
    expect(workspaceSection("flow").collections).toEqual(["campaigns"]);
  });

  it("never says 'encounter' or 'battle' where an author reads it", () => {
    // The format's word for a fight is `encounter` and the editor's is Stage.
    // The rail is the first surface anybody meets, so it is the first place
    // either of the other two words would teach the wrong vocabulary.
    for (const section of WORKSPACE_SECTIONS) {
      const read = `${section.label} ${section.hint}`.toLocaleLowerCase();
      expect(read, section.id).not.toContain("encounter");
      expect(read, section.id).not.toContain("battle");
    }
  });

  it("says which section owns a collection, and refuses a name that is not one", () => {
    expect(sectionOwning("maps")?.id).toBe("maps");
    expect(sectionOwning("campaigns")?.id).toBe("flow");
    expect(sectionOwning("unitTypes")?.id).toBe("characters");
    expect(sectionOwning("title" as SourceCollectionName)).toBeUndefined();
  });

  it("falls back to the section an author lands on rather than to nothing", () => {
    expect(workspaceSection("no-such-section").id)
      .toBe(DEFAULT_WORKSPACE_SECTION);
  });
});
