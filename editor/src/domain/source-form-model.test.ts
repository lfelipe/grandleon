// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  isStableId,
  sourceEditableProjectFields,
  sourceGameRuleFields,
  sourceGameSettingsFields,
  sourceProjectFieldNames,
  withheldProjectFields,
  sourceProjectMetadataFields,
  sourceRecordFields,
  sourceTestingAidFields,
  stableIdPattern
} from "./source-form-model";
import { SourceProjectSession } from "./source-project-session";
import { createSourceProject } from "./source-project-document";
import { TURN_ORDERS } from "./game-settings";

describe("source form model", () => {
  it("derives nested class constraints from canonical schemas", () => {
    const fields = sourceRecordFields("classes");
    expect(fields.find((field) => field.path.join(".") === "baseStats.health"))
      .toEqual(expect.objectContaining({
        kind: "integer",
        required: true,
        minimum: 1,
        maximum: 32767
      }));
    expect(fields.find((field) => field.path.join(".") === "allowedWeaponTypeIds"))
      .toEqual(expect.objectContaining({
        kind: "string-list",
        referenceCategory: "weapon_type"
      }));
  });

  it("spreads an optional group of plain fields into plain controls", () => {
    // What a character crosses is two questions, does it fly and what else
    // can it walk into, and an author answers both with labelled controls
    // rather than by typing JSON. The campaign flow above stays JSON because
    // it holds a graph, which the test beside this one pins.
    const fields = sourceRecordFields("classes");
    expect(fields.find((field) => field.path.join(".") === "traversal"))
      .toBeUndefined();
    expect(fields.find((field) => field.path.join(".") === "traversal.flying"))
      .toEqual(expect.objectContaining({
        kind: "boolean",
        required: false,
        label: "Flies"
      }));
    expect(
      fields.find((field) => field.path.join(".") === "traversal.crossings")
    ).toEqual(expect.objectContaining({
      kind: "string-list",
      required: false,
      label: "Also crosses"
    }));
  });

  it("offers accuracy as a bounded percentage on weapons and abilities", () => {
    // The field an author sets to make a strike uncertain, on both records
    // that carry one, with the schema's own bounds rather than the form's.
    for (const record of ["weapons", "abilities"] as const) {
      expect(
        sourceRecordFields(record).find(
          (field) => field.path.join(".") === "accuracy"
        )
      ).toEqual(
        expect.objectContaining({
          kind: "integer",
          required: false,
          minimum: 0,
          maximum: 100,
          label: "Accuracy (%)"
        })
      );
    }
  });

  it("offers growth as one bounded percentage per stat on a unit type", () => {
    // Ten controls rather than one JSON box, in the one order a level-up rolls
    // them: the order is a rule, so an author reading the form top to bottom is
    // reading the consumption order the engine keeps, and the four the richer
    // stat line added are at the bottom, because the order is append-only.
    const fields = sourceRecordFields("unitTypes");
    expect(fields.find((field) => field.path.join(".") === "growthRates"))
      .toBeUndefined();
    const stats = [
      ["health", "Health growth (%)"],
      ["strength", "Strength growth (%)"],
      ["defense", "Defense growth (%)"],
      ["resistance", "Resistance growth (%)"],
      ["movement", "Movement growth (%)"],
      ["actionPoints", "Action point growth (%)"],
      ["skill", "Skill growth (%)"],
      ["luck", "Luck growth (%)"],
      ["evasion", "Evasion growth (%)"],
      ["magic", "Magic growth (%)"]
    ] as const;
    expect(
      fields
        .filter((field) => field.path[0] === "growthRates")
        .map((field) => field.path[1])
    ).toEqual(stats.map(([stat]) => stat));
    for (const [stat, label] of stats) {
      expect(
        fields.find((field) => field.path.join(".") === `growthRates.${stat}`)
      ).toEqual(
        expect.objectContaining({
          kind: "integer",
          required: false,
          minimum: 0,
          maximum: 100,
          label
        })
      );
    }
    expect(
      fields.find((field) => field.path.join(".") === "experienceAward")
    ).toEqual(
      expect.objectContaining({ kind: "integer", required: false, minimum: 0 })
    );
    expect(
      fields.find((field) => field.path.join(".") === "experiencePerLevel")
    ).toEqual(
      expect.objectContaining({ kind: "integer", required: false, minimum: 1 })
    );
  });

  it("offers a drop as an item reference and a bounded percentage", () => {
    // The two halves sit beside each other because neither means anything
    // alone, and the chance is bounded at one rather than nought: leaving
    // nothing is spelled by filling in neither, so a nought would be a second
    // spelling of the same statement.
    const fields = sourceRecordFields("unitTypes");
    expect(
      fields.find((field) => field.path.join(".") === "dropItemId")
    ).toEqual(
      expect.objectContaining({
        kind: "text",
        required: false,
        referenceCategory: "item",
        label: "Drops"
      })
    );
    expect(
      fields.find((field) => field.path.join(".") === "dropChance")
    ).toEqual(
      expect.objectContaining({
        kind: "integer",
        required: false,
        minimum: 1,
        maximum: 100,
        label: "Drop chance (%)"
      })
    );
  });

  it("offers a control for every field the project itself carries", () => {
    // The claim this pins is not that the three lists are these lists. It is
    // that between them they cover the schema, so a project field added
    // tomorrow and forgotten by all three fails here instead of shipping as a
    // field the format holds and no control anywhere writes.
    const offered = [
      ...sourceProjectMetadataFields(),
      ...sourceGameSettingsFields()
    ].map((field) => field.path[0]);

    // Withheld fields count as covered only because they are written down with
    // a reason; a field forgotten by everybody is in neither list and fails.
    const accounted = [...offered, ...Object.keys(withheldProjectFields)];
    expect([...accounted].sort()).toEqual([...sourceProjectFieldNames()].sort());
    // And no field is offered twice, on one page or across the two.
    expect(new Set(offered).size).toBe(offered.length);
  });

  it("lets the session store every project field a page can offer", () => {
    // The runtime allow-list in `updateMetadata` is the fourth hand-kept list,
    // and it fails silently in the other direction: a control writes, the
    // session refuses the field by name, and the refusal is a thrown error an
    // author meets as a save that did nothing. Every settable field is asked
    // for here, one at a time, so a field missing from that list is named.
    for (const field of sourceEditableProjectFields()) {
      const session = new SourceProjectSession(createSourceProject());
      const name = field.path[0]!;
      expect(
        () => session.updateMetadata({ [name]: undefined }),
        name
      ).not.toThrow();
    }
    // The two the machine owns are excluded, and stay excluded.
    expect(sourceEditableProjectFields().map((field) => field.path[0]))
      .not.toContain("packageId");
    const session = new SourceProjectSession(createSourceProject());
    expect(() => session.updateMetadata(
      { packageId: "x" } as unknown as Record<string, never>
    )).toThrow("not editable");
  });

  it("reads the stable identifier rule from the schema that judges it", () => {
    // Several surfaces author an identifier the record form never sees, a
    // rename, a placement or a world flag, and a copy of this pattern per
    // surface is a rule that can drift from the schema that judges it.
    expect(isStableId("iron_knight")).toBe(true);
    expect(isStableId("gate.house-a")).toBe(true);
    expect(isStableId("My Best Knight!!")).toBe(false);
    expect(isStableId("")).toBe(false);
    expect(
      sourceRecordFields("classes").find((field) => field.path[0] === "id")
        ?.pattern
    ).toBe(stableIdPattern);
  });

  it("derives metadata and typed-reference controls without UI-owned limits", () => {
    // The two project forms are a partition, not an overlap: what the project
    // is stays on the metadata form, what the game plays and looks like is on
    // the settings page, and no stored field is offered by both.
    expect(sourceProjectMetadataFields().map((field) => field.path[0])).toEqual([
      "schemaVersion",
      "packageId",
      "notes",
    ]);
    expect(sourceGameSettingsFields().map((field) => field.path[0])).toEqual([
      "title",
      "gameId",
      "contentRevision",
      "defaultTurnOrder",
      "characterLoss",
      "characterStyleId",
      "themeId",
      "invulnerableForTesting"
    ]);
    // The page's two lists are a partition of that one, and the testing aid is
    // in the second: a control that is not a choice about the game is never
    // offered among the ones that are.
    expect(sourceGameRuleFields().map((field) => field.path[0]))
      .not.toContain("invulnerableForTesting");
    expect(sourceTestingAidFields().map((field) => field.path[0])).toEqual([
      "invulnerableForTesting"
    ]);
    expect(sourceGameSettingsFields()).toEqual([
      ...sourceGameRuleFields(),
      ...sourceTestingAidFields()
    ]);
    const both = sourceProjectMetadataFields().filter((field) =>
      sourceGameSettingsFields().some(
        (setting) => setting.path[0] === field.path[0]
      )
    );
    expect(both).toEqual([]);
    expect(sourceRecordFields("weapons").find(
      (field) => field.path[0] === "weaponTypeId"
    )).toEqual(expect.objectContaining({
      required: false,
      referenceCategory: "weapon_type"
    }));
    expect(sourceRecordFields("items").find(
      (field) => field.path[0] === "stackLimit"
    )).toEqual(expect.objectContaining({
      kind: "integer",
      minimum: 1,
      maximum: 65535
    }));
  });

  it("offers the character style as a labelled optional choice", () => {
    const field = sourceGameSettingsFields().find(
      (candidate) => candidate.path[0] === "characterStyleId"
    );
    expect(field).toEqual(expect.objectContaining({
      kind: "select",
      required: false,
      label: "Character style"
    }));
    // Every menu entry is offered, in the menu's own order, with a
    // plain-language label rather than the identifier the schema carries.
    expect(field?.options).toEqual([
      { value: "medieval", label: "Medieval" },
      { value: "scifi", label: "Sci-fi" },
      { value: "mythical", label: "Mythical" },
      { value: "nature", label: "Nature" },
      { value: "sengoku", label: "Sengoku Japan" },
      { value: "undead", label: "Undead" },
      { value: "pirates", label: "Pirates" }
    ]);
    // The help text has to say what leaving it empty does, because that is
    // what every project written before the menu existed does.
    expect(field?.description).toContain("Leave empty");
  });

  it("never captions a field with the definition it borrowed its rule from",
    () => {
      // A field written as `$ref: stableId` beside its own `description` says
      // the reference for the rule and the sentence for the author. Dropping
      // the sibling handed every such control the definition's own paragraph,
      // "Stable, case-sensitive source identity. Renaming requires reference
      // migration.", under a Faction picker, which is the worst sentence in
      // the format to show somebody making their first game.
      const borrowed = "Renaming requires reference migration";
      const captions = (
        [
          "unitTypes", "weapons", "items", "classes", "abilities",
          "objectives", "campaigns", "maps", "factions", "dialogues",
          "weaponTypes", "itemTypes"
        ] as const
      ).flatMap((collection) =>
        sourceRecordFields(collection).map((field) => ({
          where: `${collection}.${field.path.join(".")}`,
          description: field.description ?? ""
        }))
      );
      expect(captions.filter((entry) => entry.description.includes(borrowed)))
        .toEqual([]);

      // And the picker most likely to be read by a beginner says what it is
      // for, in the words an author would use.
      const faction = sourceRecordFields("unitTypes").find(
        (field) => field.path.join(".") === "factionId"
      );
      expect(faction?.label).toBe("Faction");
      expect(faction?.referenceCategory).toBe("faction");
      expect(faction?.description).toBe(
        "Who they fight for, which decides the colour they are drawn in. " +
        "Leave it empty and nobody claims them."
      );
      // The rule the reference supplied is still in force; only the caption
      // changed. A pattern lost here is a bad identifier the browser admits.
      expect(faction?.pattern).toBe(stableIdPattern);
    });

  it("lets a character name a style and a figure the game did not", () => {
    // The project's choice is a default and never a gate, so the control on a
    // character offers the whole menu rather than a subset of it.
    const fields = sourceRecordFields("unitTypes");
    const style = fields.find(
      (candidate) => candidate.path[0] === "characterStyleId"
    );
    const figure = fields.find(
      (candidate) => candidate.path[0] === "characterFigureId"
    );
    const settings = sourceGameSettingsFields();
    expect(style?.options).toEqual(
      settings.find((f) => f.path[0] === "characterStyleId")?.options
    );
    // A body has no game-wide twin to agree with: the choice is about a
    // person's picture, so it lives only where a person does.
    expect(
      settings.find((f) => f.path[0] === "characterFigureId")
    ).toBeUndefined();
    for (const field of [style]) {
      expect(field).toEqual(expect.objectContaining({
        kind: "select",
        required: false,
        // The empty choice on a per-character control is not "Not set": the
        // character is drawn, and what it is drawn as is the game's choice.
        // A control that could not say so would invite an author to pick the
        // value they already have, which writes an override the moment it is
        // touched.
        unsetLabel: "Follow the game setting"
      }));
      // What the empty choice does is the option's own words, above, so the
      // help says the thing the label cannot: this is one character alone.
      expect(field?.description).toContain("This one character alone");
    }
    // The game-wide controls keep the plain empty choice, because there is no
    // wider setting for them to follow.
    expect(
      settings.find((field) => field.path[0] === "characterStyleId")?.unsetLabel
    ).toBeUndefined();
  });

  it("names the two bodies for what they look like, and never for stats", () => {
    // `first` and `second` are how the art library indexes its sheets, and an
    // author reading "First" against "Second" learns nothing. Nor may this be
    // called a *build*: in this genre a character build is a stat spread and a
    // loadout, which is the one thing it does not touch. The old name made
    // the help spend its whole length denying numbers it never had.
    const field = sourceRecordFields("unitTypes").find(
      (candidate) => candidate.path[0] === "characterFigureId"
    );
    expect(field).toEqual(expect.objectContaining({
      kind: "select",
      required: false,
      label: "Body"
    }));
    expect(field?.options?.map((option) => option.value))
      .toEqual(["first", "second"]);
    // Said plainly. These two are drawn male and female, and an author looking
    // for a female character has to have something to look for.
    expect(field?.options?.map((option) => option.label))
      .toEqual(["Male", "Female"]);
    expect(field?.label.toLowerCase()).not.toContain("build");
    // And a character that names none follows the game rather than being told
    // it is the default figure, which would put "Male" twice in one menu and
    // invite an override the moment it was touched.
    expect(field?.unsetLabel).toBe("Follow the game setting");
    // And with the name honest, the help is four words rather than the
    // schema's own paragraph about sheet indices.
    expect(field?.description).toBe("Only the picture changes.");
  });

  it("offers the game's turn order as a labelled optional choice", () => {
    const field = sourceGameSettingsFields().find(
      (candidate) => candidate.path[0] === "defaultTurnOrder"
    );
    expect(field).toEqual(expect.objectContaining({
      kind: "select",
      required: false,
      label: "Turn order"
    }));
    // The same three orders the schema holds, worded the way the board's own
    // control words them, from one list, so two menus cannot disagree.
    expect(field?.options).toEqual(
      TURN_ORDERS.map((order) => ({ value: order.id, label: order.label }))
    );
    // Leaving it empty is what every project written before the setting
    // existed does, so the help has to say what that means.
    expect(field?.description).toContain("Leave empty");
    // And it has to say the thing an author most needs to know before
    // changing it: the boards that chose their own are not touched.
    expect(field?.description).toContain("never rewrites");
  });

  it("asks what a fall costs in sentences rather than in adjectives", () => {
    const field = sourceGameRuleFields().find(
      (candidate) => candidate.path[0] === "characterLoss"
    );
    expect(field).toEqual(expect.objectContaining({
      kind: "select",
      required: false,
      label: "If a character falls"
    }));
    // What an author is choosing between is two games, so each option says
    // what happens to their company rather than naming a stored word.
    expect(field?.options).toEqual([
      { value: "permanent", label: "A character who falls is dead for good" },
      {
        value: "recoverable",
        label: "A character who falls is carried off, and rejoins the company " +
          "after the Stage"
      }
    ]);
    // Empty is what a fall meant before the setting existed, and the help has
    // to say so, as every other optional menu on this page does.
    expect(field?.description).toContain("Leave empty");
    // And the one thing that is true under either choice, because an author
    // reading "carried off" could otherwise expect the board to change.
    expect(field?.description).toContain("leave the board");
  });

  it("words the testing aid as a testing aid that still ships", () => {
    const field = sourceTestingAidFields().find(
      (candidate) => candidate.path[0] === "invulnerableForTesting"
    );
    expect(field).toEqual(expect.objectContaining({
      kind: "boolean",
      required: false,
      // The label says who it protects and why it is here, in the words an
      // author would use asking for it.
      label: "Player side is immortal (for debugging purposes)"
    }));
    // The help is one sentence and a warning, and the warning is the part that
    // cannot be cut: the switch is compiled into the package, so leaving it on
    // hands a stranger a different game. It says what it does in a line, and
    // never claims the export strips it.
    expect(field?.description).toContain("written into the file you export");
    expect(field?.description).toContain("before you share");
    expect(field?.description!.length).toBeLessThan(260);
  });

  it("presents booleans as switches with plain labels and gameplay help", () => {
    const field = sourceRecordFields("classes").find(
      (candidate) => candidate.path.join(".") === "actsAfterAttacking"
    );
    expect(field).toEqual(expect.objectContaining({
      kind: "boolean",
      required: false,
      label: "Keeps acting after attacking"
    }));
    expect(field?.description).toContain("attacking ");
    expect(field?.description).toContain("ends its turn");
  });

  it("presents enums as selects whose options say what each choice does", () => {
    const kind = sourceRecordFields("abilities").find(
      (candidate) => candidate.path.join(".") === "kind"
    );
    expect(kind?.kind).toBe("select");
    expect(kind?.options).toEqual([
      expect.objectContaining({ value: "damage" }),
      expect.objectContaining({ value: "restore" })
    ]);
    expect(kind?.options?.[0]?.label).toContain("hurts");

    const side = sourceRecordFields("objectives").find(
      (candidate) => candidate.path.join(".") === "side"
    );
    expect(side?.options?.map((option) => option.label))
      .toEqual(["Your side", "The enemy"]);
  });

  it("offers the faction colour menu as a select", () => {
    const colour = sourceRecordFields("factions").find(
      (candidate) => candidate.path.join(".") === "color"
    );
    expect(colour?.kind).toBe("select");
    expect(colour?.required).toBe(false);
    expect(colour?.options?.map((option) => option.value))
      .toEqual(["blue", "red", "green", "violet", "amber", "bone"]);
    // An author who never opens the menu still gets a colour, and the help
    // has to say which one rather than leaving the field looking broken.
    expect(colour?.description).toContain("first faction is blue");
  });

  it("states units and effects for the numeric stat fields", () => {
    const fields = sourceRecordFields("classes");
    const description = (path: string) =>
      fields.find((field) => field.path.join(".") === path)?.description ?? "";
    expect(description("baseStats.movement")).toContain("Tiles");
    expect(description("baseStats.health")).toContain("Hit points");
    expect(description("baseStats.actionPoints")).toContain("move and");
    expect(description("baseStats.speed")).toContain("acts earlier");
  });

  it("derives non-linear campaign flow controls from the campaign schema", () => {
    const fields = sourceRecordFields("campaigns");
    expect(fields.find((field) => field.path.join(".") === "flow"))
      .toEqual(expect.objectContaining({ kind: "json", required: false }));
  });

  it("names the company a campaign keeps, and what leaving it empty costs", () => {
    // The company is a list of records with rules of its own, so the model
    // keeps it whole, the workspace giving it a real list editor, and the
    // words here are the ones that editor introduces it with.
    const roster = sourceRecordFields("campaigns").find(
      (field) => field.path.join(".") === "roster"
    );
    expect(roster).toEqual(expect.objectContaining({
      kind: "json",
      required: false,
      label: "The company this campaign starts with"
    }));
    expect(roster?.description).toContain("keeps between Stages");
    expect(roster?.description).toContain("join later");
  });

  it("names the store a campaign is founded with, and what an entry means", () => {
    // The founding stock is a list of records with rules of its own, exactly
    // as the company is, so the model keeps it whole and the workspace gives
    // it a real list editor. These are the words that editor introduces it
    // with, and the reference category the picker is filtered by.
    const store = sourceRecordFields("campaigns").find(
      (field) => field.path.join(".") === "startingStore"
    );
    expect(store).toEqual(expect.objectContaining({
      kind: "json",
      required: false,
      label: "What the company's store starts with"
    }));
    expect(store?.description)
      .toContain("Owned by the company rather than by anybody in it");
    // What a grant names, wherever a campaign writes one: the founding stock
    // and a node's grant are the same record and share this one entry.
    const grant = sourceRecordFields("campaigns").find(
      (field) => field.path.join(".") === "startingStore.itemId"
    );
    expect(grant?.referenceCategory ?? "item").toBe("item");
  });
});
