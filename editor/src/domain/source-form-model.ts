// SPDX-License-Identifier: MIT
import { sourceV1Schemas } from "../generated/source-v1-schemas";
import { TURN_ORDERS } from "./game-settings";
import type { SourceCollectionName } from "./source-project-session";

export type SourceFieldKind =
  | "text"
  | "textarea"
  | "integer"
  | "boolean"
  | "select"
  | "string-list"
  | "json";

export interface SourceFieldOption {
  readonly value: string;
  readonly label: string;
}

export interface SourceFieldDescriptor {
  readonly path: readonly string[];
  readonly label: string;
  readonly kind: SourceFieldKind;
  readonly required: boolean;
  readonly description?: string;
  /** Choices for "select" fields, in schema order. */
  readonly options?: readonly SourceFieldOption[];
  /**
   * What the empty choice of an optional select reads as, when "Not set" would
   * be the wrong sentence. A per-record field that falls back to a game-wide
   * one is not unset, it follows the game, and a control that cannot say so
   * invites an author to pick the value they already have, which writes an
   * override the moment it is touched.
   */
  readonly unsetLabel?: string;
  /**
   * Whether this field stands behind the form's Advanced fold rather than in
   * front of it. See `advancedRoots` below for what earns the mark.
   */
  readonly advanced?: boolean;
  readonly minimum?: number;
  readonly maximum?: number;
  readonly minLength?: number;
  readonly maxLength?: number;
  readonly pattern?: string;
  // The vocabulary of typed references, shared with the editing surfaces: a
  // control offering one of these names the category so the workspace can hand
  // it the right choices and create a related record of the right collection.
  // "unit_type" is offered by the campaign company editor, which is not driven
  // by a schema-derived field, so no path maps to it below.
  readonly referenceCategory?:
    | "class"
    | "unit_type"
    | "weapon_type"
    | "item_type"
    | "weapon"
    | "item"
    | "faction"
    | "ability"
    | "objective"
    | "dialogue";
}

interface JsonSchema {
  readonly $id?: string;
  readonly $ref?: string;
  readonly type?: string;
  readonly enum?: readonly unknown[];
  readonly description?: string;
  readonly properties?: Readonly<Record<string, JsonSchema>>;
  readonly required?: readonly string[];
  readonly items?: JsonSchema;
  readonly additionalProperties?: boolean | JsonSchema;
  readonly minimum?: number;
  readonly maximum?: number;
  readonly minLength?: number;
  readonly maxLength?: number;
  readonly pattern?: string;
}

const schemas = sourceV1Schemas as readonly JsonSchema[];
const byId = new Map(
  schemas.flatMap((schema) => schema.$id ? [[schema.$id, schema] as const] : [])
);

const collectionSchemas: Record<SourceCollectionName, string> = {
  classes: "class.schema.json",
  weaponTypes: "weapon-type.schema.json",
  itemTypes: "item-type.schema.json",
  unitTypes: "unit-type.schema.json",
  weapons: "weapon.schema.json",
  items: "item.schema.json",
  maps: "map.schema.json",
  factions: "faction.schema.json",
  abilities: "ability.schema.json",
  objectives: "objective.schema.json",
  campaigns: "campaign.schema.json",
  dialogues: "dialogue.schema.json"
};

const referenceCategories = new Map<string, SourceFieldDescriptor["referenceCategory"]>([
  ["class.schema.json:allowedWeaponTypeIds", "weapon_type"],
  ["unit-type.schema.json:classId", "class"],
  ["unit-type.schema.json:startingWeaponIds", "weapon"],
  ["unit-type.schema.json:startingItemIds", "item"],
  ["unit-type.schema.json:dropItemId", "item"],
  ["weapon.schema.json:weaponTypeId", "weapon_type"],
  ["item.schema.json:itemTypeId", "item_type"],
  ["unit-type.schema.json:factionId", "faction"],
  ["unit-type.schema.json:abilityIds", "ability"],
  ["campaign.schema.json:objectiveIds", "objective"],
  ["campaign.schema.json:dialogueIds", "dialogue"],
  // Both places a campaign stocks its own store name one item: the founding
  // stock on the campaign and a node's grant in the flow. They are one shape
  // and share one field name, so one entry serves both: the grant editor and
  // the flow editor read the category from here rather than each naming it.
  ["campaign.schema.json:itemId", "item"]
]);

interface FieldPresentation {
  readonly label?: string;
  /**
   * The sentence under the control, or `""` for none at all.
   *
   * Present-and-empty is not the same as absent: absent takes the schema's own
   * `description`, and `""` says the schema's paragraph is not worth printing
   * here. Ten sibling percentages each reprinting one paragraph about how a
   * roll works is one explanation and nine copies of it.
   */
  readonly help?: string;
  readonly optionLabels?: Readonly<Record<string, string>>;
  readonly unsetLabel?: string;
}

// Plain-language labels for every field, and help only where the label alone
// would not tell an author what to enter. Help is the exception and not the
// rule: a field whose label answers it carries no sentence at all, and one that
// needs a sentence gets the shortest true one. What it says must be what the
// engine does (engine/simulation/src/encounter.cpp,
// engine/package_runtime/src/encounter_loader.cpp).
const presentations: Readonly<Record<string, FieldPresentation>> = {
  // `*` is every schema. The stored identity is the one field every record
  // has, and the schema definition it borrows its rule from explains that rule
  // to whoever writes a migration: "Stable, case-sensitive source identity.
  // Renaming requires reference migration." An author is not migrating
  // anything, the editor's rename doing that for them, so what they are told
  // is what the field is and how it is changed.
  "*:id": {
    label: "Id",
    help: "The name this record is filed under. Renaming it moves everything " +
      "that points at it too, which is why it is renamed rather than typed."
  },
  "class.schema.json:baseStats.health": {
    label: "Health",
    help: "Hit points at the start of a Stage. Defeated at 0."
  },
  "class.schema.json:baseStats.movement": {
    label: "Movement",
    help: "Tiles walked in one move, never through another unit."
  },
  "class.schema.json:baseStats.strength": {
    label: "Strength",
    help: "Damage is strength plus the weapon's power minus defense, at least 1."
  },
  "class.schema.json:baseStats.defense": {
    label: "Defense",
    help: "Subtracted from physical damage. At least 1 still lands."
  },
  "class.schema.json:baseStats.resistance": {
    label: "Resistance",
    help: "Defense against magical damage. Leave empty for 0."
  },
  "class.schema.json:baseStats.skill": {
    label: "Skill",
    help: "Added to how often their own attacks land. Leave empty for 0."
  },
  "class.schema.json:baseStats.luck": {
    label: "Luck",
    help: "Their attacks land more often, attacks on them less. Leave empty for 0."
  },
  "class.schema.json:baseStats.evasion": {
    label: "Evasion",
    help: "Subtracted from how often attacks on them land. Leave empty for 0."
  },
  "class.schema.json:baseStats.magic": {
    label: "Magic",
    help: "Strength for spells: magic plus the ability's power minus " +
      "resistance. Leave empty for 0."
  },
  "class.schema.json:baseStats.actionPoints": {
    label: "Actions per turn",
    help: "2 lets it move and then attack. Leave empty for 1, where walking " +
      "is the whole turn and it cannot attack after taking a step."
  },
  "class.schema.json:baseStats.speed": {
    label: "Speed",
    help: "Higher speed acts earlier, where the Stage orders its turns. " +
      "Leave empty for 1."
  },
  "class.schema.json:actsAfterAttacking": {
    label: "Keeps acting after attacking",
    help: "When off, attacking ends its turn."
  },
  "class.schema.json:traversal.flying": {
    label: "Flies",
    help: "Crosses every kind of ground."
  },
  "class.schema.json:traversal.crossings": {
    label: "Also crosses",
    help: "Type water for rivers and lakes, or heights for mountains and rock."
  },
  "class.schema.json:allowedWeaponTypeIds": {
    label: "Allowed weapon types",
    help: "Unset allows any; an empty list allows none."
  },
  "unit-type.schema.json:classId": {
    label: "Class",
    help: "Where this character's numbers come from."
  },
  // Its own words, because the schema's are about the field's history: an
  // "optional authoring default" whose "runtime team semantics are not yet
  // defined" is a sentence for whoever writes the compiler.
  "unit-type.schema.json:factionId": {
    label: "Faction",
    help: "Who they fight for, which decides the colour they are drawn in. " +
      "Leave it empty and nobody claims them."
  },
  "unit-type.schema.json:startingWeaponIds": {
    label: "Starting weapons",
    help: "The first one decides how far they strike."
  },
  "unit-type.schema.json:startingItemIds": {
    label: "Starting items",
    help: "One of each, in the order the action menu will offer them."
  },
  "unit-type.schema.json:dropItemId": {
    label: "Drops",
    help: "Claimed by the side that felled it. Fill this in with the chance " +
      "below, or leave both empty."
  },
  "unit-type.schema.json:dropChance": {
    label: "Drop chance (%)",
    help: "Rolled at the moment of defeat. 100 always drops; empty leaves nothing."
  },
  "unit-type.schema.json:experienceAward": {
    label: "Experience for defeating one (XP)",
    help: "Goes to whoever struck the last blow."
  },
  "unit-type.schema.json:experiencePerLevel": {
    label: "Experience per level (XP)",
    help: "Level is 1 plus lifetime experience divided by this, up to 99. " +
      "Empty means 100."
  },
  "unit-type.schema.json:growthRates": {
    label: "Growth rates",
    help: "How often each stat gains a point on a level-up, rolled once each " +
      "in the order below. Empty stats never grow."
  },
  // The first of the ten carries the explanation the whole group needs, and
  // the other nine carry none. The schema states the rule once on the shared
  // `growthChance` definition, so leaving them to it printed the same
  // paragraph ten times down one column, which reads as ten different rules
  // an author has to compare rather than one they have already read.
  "unit-type.schema.json:growthRates.health": {
    label: "Health growth (%)",
    help: "A level-up rolls each of these once, in the order below, and a " +
      "success adds a point. Empty never grows."
  },
  "unit-type.schema.json:growthRates.strength": {
    label: "Strength growth (%)",
    help: ""
  },
  "unit-type.schema.json:growthRates.defense": {
    label: "Defense growth (%)",
    help: ""
  },
  "unit-type.schema.json:growthRates.resistance": {
    label: "Resistance growth (%)",
    help: ""
  },
  "unit-type.schema.json:growthRates.movement": {
    label: "Movement growth (%)",
    help: ""
  },
  "unit-type.schema.json:growthRates.actionPoints": {
    label: "Action point growth (%)",
    help: ""
  },
  "unit-type.schema.json:growthRates.skill": {
    label: "Skill growth (%)",
    help: ""
  },
  "unit-type.schema.json:growthRates.luck": {
    label: "Luck growth (%)",
    help: ""
  },
  "unit-type.schema.json:growthRates.evasion": {
    label: "Evasion growth (%)",
    help: ""
  },
  "unit-type.schema.json:growthRates.magic": {
    label: "Magic growth (%)",
    help: ""
  },
  "weapon.schema.json:power": {
    label: "Power",
    help: "Added to the wielder's strength when attacking."
  },
  "weapon.schema.json:weaponTypeId": {
    label: "Weapon type",
    help: "What kind of weapon this is. A class may allow only some kinds."
  },
  "weapon.schema.json:minimumRange": { label: "Minimum range (tiles)" },
  "weapon.schema.json:maximumRange": { label: "Maximum range (tiles)" },
  "weapon.schema.json:range": { label: "Range (legacy)" },
  "weapon.schema.json:accuracy": {
    label: "Accuracy (%)",
    help: "100, or left empty, always lands. Anything lower is rolled once " +
      "per strike, and is the number a player is shown."
  },
  "item.schema.json:itemTypeId": {
    label: "Item type",
    help: "What kind of item this is."
  },
  "item.schema.json:stackLimit": {
    label: "Stack limit",
    help: "How many share one inventory slot."
  },
  "item.schema.json:kind": {
    label: "What using it does",
    optionLabels: {
      restore: "Restores health"
    },
    help: "Left empty, it can be carried but not used in a fight."
  },
  "item.schema.json:power": {
    label: "Health restored",
    help: "Capped at the health the character is missing. Using an item " +
      "rolls nothing, so this is the number a player is shown."
  },
  "project.schema.json:title": { label: "What the game is called" },
  "project.schema.json:defaultTurnOrder": {
    label: "Turn order",
    // The same words the board's own control offers, from the one list that
    // states them: two menus for one setting must never disagree about what an
    // order is called.
    optionLabels: Object.fromEntries(
      TURN_ORDERS.map((order) => [order.id, order.label])
    ),
    help: "Unless a Stage says otherwise. Leave empty for sides taking " +
      "turns; changing it never rewrites a Stage that chose its own."
  },
  "project.schema.json:characterLoss": {
    label: "If a character falls",
    // The two options are whole sentences rather than the words 'permanent'
    // and 'recoverable', because what an author is choosing between is two
    // games and not two adjectives. Whichever they pick, they have read what
    // it does to their company.
    optionLabels: {
      permanent: "A character who falls is dead for good",
      recoverable:
        "A character who falls is carried off, and rejoins the company " +
        "after the Stage"
    },
    help: "Either way they leave the board; this decides only what the " +
      "company is left with afterwards. Leave empty for dead for good."
  },
  // Not a game rule, and never listed among them. The label says what it does
  // and the help says what it is, because the one thing an author must not do
  // is mistake it for a way to play. See `sourceTestingAidFields` below, and
  // the heading it is shown under on the settings page.
  "project.schema.json:invulnerableForTesting": {
    label: "Player side is immortal (for debugging purposes)",
    // The warning, and only the warning. The switch is not a debug flag the
    // export strips: it is compiled into the package like every other setting,
    // so the game a stranger is handed is the game it was left on for.
    help: "It is written into the file you export, so turn it off before you " +
      "share the game."
  },
  "project.schema.json:themeId": {
    label: "Season",
    optionLabels: {
      temperate: "Temperate",
      autumn: "Autumn",
      winter: "Winter",
      ashland: "Ashland"
    },
    help: "Leave empty for temperate: green fields and blue water."
  },
  "project.schema.json:characterStyleId": {
    label: "Character style",
    optionLabels: {
      medieval: "Medieval",
      scifi: "Sci-fi",
      mythical: "Mythical",
      nature: "Nature",
      sengoku: "Sengoku Japan",
      undead: "Undead",
      pirates: "Pirates"
    },
    help: "Only the picture changes: every role, number and faction colour " +
      "stays. Leave empty for medieval, and any character may name its own."
  },
  // There is deliberately no game-wide entry here. The field exists on a
  // character, where the choice is about somebody an author is looking at; as
  // a default for the whole game it asked everybody to answer a question about
  // the art library's sheet indices before they had made anyone. An absent
  // value is the broad build, which is the one every role is drawn in first.
  "unit-type.schema.json:characterStyleId": {
    label: "Character style",
    unsetLabel: "Follow the game setting",
    optionLabels: {
      medieval: "Medieval",
      scifi: "Sci-fi",
      mythical: "Mythical",
      nature: "Nature",
      sengoku: "Sengoku Japan",
      undead: "Undead",
      pirates: "Pirates"
    },
    help: "This one character alone, so a medieval campaign can raise an " +
      "undead enemy. Only the picture changes."
  },
  // "Body", not "build". In a game like this one a *character build* is a
  // stat spread and a loadout, which is the one thing this control does not
  // touch, so the old name sent an author looking for numbers and the help
  // had to spend its whole length denying them. A name that has to be
  // contradicted is the wrong name.
  //
  // The options say what they are. They were "Broad" and "Narrow", which
  // described the drawings without naming them and left an author looking for
  // a female character with nothing to search for. The art library draws these
  // two male and female and now says so, and this agrees with it rather than
  // keeping a second vocabulary for the same menu.
  "unit-type.schema.json:characterFigureId": {
    label: "Body",
    // Not "Male". A character that names no figure is not male, it *follows the
    // game*, which is what this field's own contract says an unset value means
    // and what `characterStyleId` beside it already says. Naming the default
    // here put the word twice in one menu and invited an author to pick the
    // value they already had, writing an override the moment it was touched.
    unsetLabel: "Follow the game setting",
    optionLabels: { first: "Male", second: "Female" },
    // Overriding the schema's own paragraph, which names the stored field and
    // the sheet index. Four words say the whole of it.
    help: "Only the picture changes."
  },
  "dialogue.schema.json:backgroundId": {
    label: "Set against",
    optionLabels: {
      throne_hall: "Throne hall",
      night_camp: "Night camp",
      deep_wood: "Deep wood",
      mountain_dusk: "Mountain dusk",
      open_sea: "Open sea",
      star_field: "Star field",
      crypt: "Crypt"
    },
    help: "Bands of colour rather than a picture, so it draws the same " +
      "everywhere. Leave empty for a plain screen."
  },
  "faction.schema.json:color": {
    label: "Colour",
    optionLabels: {
      blue: "Blue",
      red: "Red",
      green: "Green",
      violet: "Violet",
      amber: "Amber",
      bone: "Bone"
    },
    help: "Leave empty and the first faction is blue, the second red, and " +
      "so on down the list."
  },
  "ability.schema.json:kind": {
    label: "What it does",
    optionLabels: {
      damage: "Damage: hurts every unit in the area",
      restore: "Restore: heals every unit in the area"
    }
  },
  "ability.schema.json:damageType": {
    label: "Damage type",
    optionLabels: {
      physical: "Physical: reduced by the target's defense",
      magical: "Magical: ignores defense"
    },
    help: "Ignored when the ability restores. Leave empty for physical."
  },
  "ability.schema.json:areaShape": {
    label: "Area shape",
    optionLabels: {
      single: "Single: only the targeted tile",
      cross: "Cross: the targeted tile and its 4 neighbours",
      diamond: "Diamond: every tile within the radius"
    },
    help: "Leave empty for single."
  },
  "ability.schema.json:radius": {
    label: "Radius (tiles)",
    help: "Diamond only; other shapes ignore it."
  },
  "ability.schema.json:power": {
    label: "Power",
    help: "Damage before defense, or health restored."
  },
  "ability.schema.json:minimumRange": { label: "Minimum range (tiles)" },
  "ability.schema.json:maximumRange": { label: "Maximum range (tiles)" },
  "ability.schema.json:accuracy": {
    label: "Accuracy (%)",
    help: "100, or left empty, always lands. A damaging area rolls once per " +
      "unit it covers; a restore never misses."
  },
  "objective.schema.json:kind": {
    label: "What decides it",
    optionLabels: {
      defeatAllOpponents: "Defeat every opposing unit",
      defeatTarget: "Defeat one particular unit",
      protectTarget: "Keep one particular unit alive",
      surviveRounds: "Survive a number of rounds"
    },
    help: "Leave empty for defeat every opposing unit."
  },
  "objective.schema.json:side": {
    label: "Whose condition",
    optionLabels: {
      first: "Your side",
      second: "The enemy"
    },
    help: "Whose side it is satisfied for. Leave empty for your side."
  },
  "objective.schema.json:targetPlacementId": {
    label: "Target placement",
    help: "One placed unit, offered as a list under Stages."
  },
  "campaign.schema.json:roster": {
    label: "The company this campaign starts with",
    help: "Each member is one person the campaign keeps between Stages, " +
      "wounds and experience and all. Members who join later are written on " +
      "the node they join at."
  },
  "campaign.schema.json:specificity": {
    label: "What makes them more than their character",
    help: "This archer shoots further, this knight is tougher. It stacks " +
      "with what levelling earns. Leave it out for somebody who is exactly " +
      "their character."
  },
  "campaign.schema.json:stats": {
    label: "Differences over their character's stats",
    help: "Differences and not totals, so the character underneath can " +
      "still be rebalanced. Leave a stat empty to say nothing about it; " +
      "0 is refused."
  },
  "campaign.schema.json:rangeBonus": {
    label: "Extra reach",
    help: "Added to the far end of every weapon this person strikes with, " +
      "never to the near end and never to an ability. Leave it empty for no " +
      "bonus; 0 is refused."
  },
  "campaign.schema.json:startingStore": {
    label: "What the company's store starts with",
    help: "Owned by the company rather than by anybody in it. Say how many " +
      "of each once."
  },
  "campaign.schema.json:grants": {
    label: "What the company is given here",
    help: "Put in the company's store as this node completes. A road that " +
      "loops past here twice is given it twice."
  },
  // The label is the whole question, and the schema's paragraph beneath it
  // explains a rule the editor already enforces by offering each item once.
  "campaign.schema.json:itemId": { label: "Which item", help: "" },
  "campaign.schema.json:quantity": { label: "How many" },
  "campaign.schema.json:capacity": {
    label: "How many may take the field",
    help: "A maximum and not a quota: sending fewer is legal. Leave it " +
      "empty for no cap."
  },
  "campaign.schema.json:objectiveIds": { label: "Objectives" },
  "campaign.schema.json:dialogueIds": { label: "Scenes" },
  "project.schema.json:contentRevision": {
    label: "Content revision",
    help: "Numbers with dots, like 1.0.0. Raise it when you change content " +
      "a saved game reads."
  },
  "project.schema.json:gameId": {
    label: "Game id",
    // What it is *for* comes first, because that is the only thing that makes
    // the control worth opening the fold for; the charset comes second,
    // because it is what the browser will refuse on. Saying it follows the
    // title is what stops an author changing it here by accident and then
    // wondering why a later rename left it behind.
    help: "The name the exported file carries. It follows the title above " +
      "until you write one here. Lowercase letters, digits and separators " +
      "(. _ -), starting with a letter."
  }
};

/**
 * The fields a form keeps behind its Advanced fold, by the schema they belong
 * to and their top-level name. `*` is every schema.
 *
 * What earns the mark is not "hard": it is **not needed to reach a game that
 * plays**. A first author names somebody, picks what they are, gives them a
 * weapon and a side, and is done; the roll percentages that decide a level-up,
 * the experience a defeat is worth, what a body leaves behind, and the stored
 * identifier are all real controls that no first game touches. Left in front
 * of the fold they turn a roster card into five thousand pixels of form.
 *
 * A root and not a path, so a group like `growthRates` is one entry here
 * however many spinners it flattens into. Everything named here stays fully
 * editable, the fold being a door rather than a deletion, and a field with a
 * problem forces the fold open, so nothing can hide behind it.
 */
const advancedRoots: ReadonlySet<string> = new Set([
  // Machine bookkeeping, on whichever record carries it: the stored identity,
  // the format version, the durable package number nobody types.
  "*:id",
  "*:schemaVersion",
  "*:packageId",
  // The author's own scratch, which the game never reads.
  "*:notes",
  // A character. Everything a roster card does not show.
  "unit-type.schema.json:growthRates",
  "unit-type.schema.json:experienceAward",
  "unit-type.schema.json:experiencePerLevel",
  "unit-type.schema.json:dropItemId",
  "unit-type.schema.json:dropChance",
  "unit-type.schema.json:abilityIds",
  "unit-type.schema.json:startingItemIds",
  // The game's two machine-facing names. Both are questions two and three of
  // the whole product and neither means anything before a game exists, so the
  // title answers them and this is where an author goes to disagree.
  "project.schema.json:gameId",
  "project.schema.json:contentRevision"
]);

function isAdvancedField(filename: string, path: readonly string[]): boolean {
  const root = path[0] ?? "";
  return advancedRoots.has(`*:${root}`) ||
    advancedRoots.has(`${filename}:${root}`);
}

/**
 * What one advanced root is, in an author's words: the pieces the fold's own
 * sentence is built from. Two roots may share a phrase where they are one
 * thing said twice, and the sentence says it once.
 */
const advancedPhrases: Readonly<Record<string, string>> = {
  id: "the stored identifier",
  schemaVersion: "the format version",
  packageId: "the package identity",
  notes: "your own notes",
  growthRates: "how stats grow on a level-up",
  experienceAward: "experience",
  experiencePerLevel: "experience",
  dropItemId: "what it leaves behind",
  dropChance: "what it leaves behind",
  abilityIds: "abilities",
  startingItemIds: "items carried",
  gameId: "the name the file carries",
  contentRevision: "the content revision"
};

/**
 * The sentence under a form's Advanced fold, naming what is actually inside
 * this one.
 *
 * It is derived rather than written because a fold whose description outlived
 * its contents is worse than none: one standing paragraph about "identifiers,
 * script bindings and machine bookkeeping" described a fold holding a single
 * field, and said nothing at all about the eleven growth spinners that stand
 * behind it now. A description read off the contents cannot go stale.
 */
export function advancedFieldsNote(
  fields: readonly SourceFieldDescriptor[]
): string {
  const phrases: string[] = [];
  for (const field of fields) {
    const phrase = advancedPhrases[field.path[0] ?? ""];
    if (phrase && !phrases.includes(phrase)) phrases.push(phrase);
  }
  if (phrases.length === 0) return "";
  const listed = phrases.length === 1
    ? phrases[0]!
    : `${phrases.slice(0, -1).join(", ")}, and ${phrases.at(-1)}`;
  // The second sentence is the measured claim and not a reassurance: a first
  // author reached a playable Nintendo 64 ROM without touching any of this.
  // It says "gets by without" rather than "never needs", because a required
  // field behind the fold does have an answer; the author simply did not have
  // to write it.
  return `${listed.charAt(0).toLocaleUpperCase()}${listed.slice(1)}. ` +
    "A first game gets by without any of it.";
}

/**
 * The rule every stable identifier in the format follows, read from the schema
 * that judges them rather than written out a second time here.
 *
 * The surfaces that author an identifier the record form does not reach need
 * it: a placement on a board, a node in a flow, the world flag a conversation
 * raises. An identifier the format cannot hold is not one bad field, it
 * is a project that will not open again.
 */
export const stableIdPattern: string = (() => {
  const rule = resolve({ $ref: "common.schema.json#/$defs/stableId" }).pattern;
  if (rule === undefined) {
    throw new Error("the source schema states no stable identifier pattern");
  }
  return rule;
})();

/**
 * The longest identifier the format holds, read from the same schema as the
 * pattern. A derived identifier is built from a name the author typed, and a
 * name may be longer than an identifier may be.
 */
const stableIdMaxLength: number = (() => {
  const limit = resolve({ $ref: "common.schema.json#/$defs/stableId" }).maxLength;
  if (limit === undefined) {
    throw new Error("the source schema states no stable identifier length");
  }
  return limit;
})();

/** Whether this is an identifier the format can hold. */
export function isStableId(candidate: string): boolean {
  return new RegExp(stableIdPattern, "u").test(candidate) &&
    candidate.length <= stableIdMaxLength;
}

/**
 * An identifier built out of a name, which is how the editor derives one
 * everywhere, falling back to a fixed stem for a name with nothing usable
 * in it.
 *
 * Deriving is what keeps an author from being asked the same question twice in
 * two vocabularies: they name the thing, and the name the machine files it
 * under follows. What comes back always satisfies `isStableId`, because a
 * derivation that could produce something the format refuses would turn a
 * typed name into a project that will not save.
 */
export function identifierFromName(name: string, stem: string): string {
  const derived = name
    .toLocaleLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "")
    .replace(/^([^a-z])/, `${stem}_$1`)
    .slice(0, stableIdMaxLength)
    // Slicing can leave the separator the cut fell on hanging off the end.
    .replace(/_+$/g, "");
  return isStableId(derived) ? derived : stem;
}

/**
 * A schema pattern as an HTML `pattern` attribute, or nothing.
 *
 * Browsers compile the attribute with the RegExp v flag, which requires '-' and
 * '/' to be escaped inside character classes; JSON Schema regexes do not. An
 * attribute that fails to compile disables validation silently, so patterns are
 * re-escaped and, failing that, dropped rather than left as a guard that looks
 * like one and is not.
 */
export function htmlPattern(pattern: string | undefined): string | undefined {
  if (pattern === undefined) return undefined;
  const escaped = pattern.replaceAll("-]", "\\-]").replaceAll("/", "\\/");
  try {
    new RegExp(escaped, "v");
    return escaped;
  } catch {
    return undefined;
  }
}

function label(name: string): string {
  return name
    .replace(/Id$/, "")
    .replace(/Ids$/, "")
    .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
    .replace(/^./, (character) => character.toUpperCase());
}

/**
 * A schema with its `$ref` followed.
 *
 * Keywords written *beside* the `$ref` win over the ones the target supplies.
 * That is what JSON Schema means by them: a reference is a starting point and
 * the siblings are what this use of it says for itself. Dropping them
 * made every plain `$ref` to `stableId` inherit the definition's own
 * paragraph: "Stable, case-sensitive source identity. Renaming requires
 * reference migration." A picker for which faction somebody fights for was
 * captioned with a warning about identifier migration, and so was every other
 * reference field that had not been given words of its own.
 */
function resolve(schema: JsonSchema): JsonSchema {
  if (!schema.$ref) return schema;
  const { $ref, ...own } = schema;
  return Object.keys(own).length > 0
    ? { ...target($ref), ...own }
    : target($ref);
}

function target(reference: string): JsonSchema {
  const [document, pointer] = reference.split("#");
  const bases = document
    ? [byId.get(`https://grandleon.dev/schemas/source/v1/${document}`)]
    : schemas;
  for (const candidate of bases) {
    if (!candidate) continue;
    if (!pointer) return candidate;
    let current: JsonSchema | undefined = candidate;
    for (const segment of pointer.split("/").slice(1)) {
      const value: unknown = current &&
        (current as Record<string, unknown>)[segment];
      current = value && typeof value === "object"
        ? value as JsonSchema
        : undefined;
    }
    if (current) return current;
  }
  throw new Error(`unresolved source schema reference '${reference}'`);
}

// Whether an optional closed object is worth spreading into plain controls
// rather than offered as JSON. One of plain fields, a boolean and a list of
// words like a class's traversal, is a form; one holding further structure,
// like a campaign's flow graph, is not, and is edited as JSON by its own
// surface.
function flattenable(schema: JsonSchema): boolean {
  return Object.values(schema.properties ?? {}).every((property) => {
    const resolved = resolve(property);
    if (resolved.type === "object") return false;
    if (resolved.type !== "array") return true;
    return !resolved.items || resolve(resolved.items).type !== "object";
  });
}

function flatten(
  filename: string,
  schema: JsonSchema,
  path: readonly string[],
  required: boolean
): SourceFieldDescriptor[] {
  const resolved = resolve(schema);
  if (
    resolved.type === "object" &&
    resolved.properties &&
    resolved.additionalProperties === false &&
    (required || path.length === 0 || flattenable(resolved))
  ) {
    const requiredNames = new Set(resolved.required ?? []);
    return Object.entries(resolved.properties).flatMap(([name, property]) =>
      flatten(filename, property, [...path, name], requiredNames.has(name))
    );
  }

  const name = path.at(-1) ?? "";
  const referenceCategory = referenceCategories.get(`${filename}:${name}`);
  const presentation = presentations[`${filename}:${path.join(".")}`] ??
    presentations[`*:${path.join(".")}`];
  let kind: SourceFieldKind = "text";
  if (name === "notes") kind = "textarea";
  else if (resolved.enum?.every((value) => typeof value === "string")) {
    kind = "select";
  }
  else if (resolved.type === "boolean") kind = "boolean";
  else if (resolved.type === "integer") kind = "integer";
  else if (resolved.type === "array") {
    kind = resolved.items && resolve(resolved.items).type === "object"
      ? "json"
      : "string-list";
  }
  else if (resolved.type === "object") kind = "json";

  const options = kind === "select"
    ? (resolved.enum as readonly string[]).map((value) => ({
      value,
      label: presentation?.optionLabels?.[value] ?? value
    }))
    : undefined;
  const description = presentation && "help" in presentation
    ? presentation.help
    : resolved.description;

  return [{
    path,
    label: presentation?.label ?? label(name),
    kind,
    required,
    ...(options ? { options } : {}),
    ...(presentation?.unsetLabel ? { unsetLabel: presentation.unsetLabel } : {}),
    ...(isAdvancedField(filename, path) ? { advanced: true } : {}),
    ...(description ? { description } : {}),
    ...(resolved.minimum === undefined ? {} : { minimum: resolved.minimum }),
    ...(resolved.maximum === undefined ? {} : { maximum: resolved.maximum }),
    ...(resolved.minLength === undefined ? {} : { minLength: resolved.minLength }),
    ...(resolved.maxLength === undefined ? {} : { maxLength: resolved.maxLength }),
    ...(resolved.pattern === undefined ? {} : { pattern: resolved.pattern }),
    ...(referenceCategory ? { referenceCategory } : {})
  }];
}

/**
 * The two slots the format keeps for data a later runtime might understand,
 * and which **no form offers**.
 *
 * They are not advanced, they are unusable: `reject_unmapped_runtime_data` in
 * `tools/game_content/src/source_project.cpp` walks the whole document and
 * refuses any non-empty `extensions` or `scriptBindings` with
 * `unsupported_content`. So the only thing an author could do with a control
 * over either is write something that stops their game compiling: a field
 * whose every use is a refusal.
 *
 * The schema keeps them, because that is where the forward compatibility
 * lives and taking them out would be a versioned format change buying nothing.
 * The editor simply does not draw a door onto them.
 */
const unofferedFields = new Set(["extensions", "scriptBindings"]);

export function sourceRecordFields(
  collection: SourceCollectionName
): readonly SourceFieldDescriptor[] {
  const filename = collectionSchemas[collection];
  const schema = byId.get(
    `https://grandleon.dev/schemas/source/v1/${filename}`
  );
  if (!schema) throw new Error(`source schema '${filename}' was not generated`);
  return flatten(filename, schema, [], true)
    .filter((field) => !unofferedFields.has(field.path[0] ?? ""));
}

function projectFields(
  names: readonly string[]
): readonly SourceFieldDescriptor[] {
  const schema = byId.get(
    "https://grandleon.dev/schemas/source/v1/project.schema.json"
  );
  if (!schema?.properties) throw new Error("source project schema was not generated");
  const required = new Set(schema.required ?? []);
  return names.flatMap((name) => {
    const property = schema.properties?.[name];
    return property ? flatten("project.schema.json", property, [name], required.has(name)) : [];
  });
}

/**
 * Every field the project itself carries, as opposed to the collections of
 * records inside it, in schema order.
 *
 * This is the list the three page-level lists below have to add up to. Whether
 * a project field can be authored at all otherwise rests on hand-kept lists
 * agreeing with each other and with a schema none of them reads, and the
 * failure when one drifts is silent: a field the format holds and no control
 * anywhere writes. Deriving the whole from the schema is what lets a test say
 * that the parts cover it.
 */
/**
 * Project fields no page offers, on purpose, each with its reason.
 *
 * Named rather than merely absent, so the completeness check stays sharp: a
 * field added to the schema tomorrow and forgotten by every page still fails,
 * because the only way out is to write it down here and say why.
 */
export const withheldProjectFields: Readonly<Record<string, string>> = {
  // Refused by the compiler wherever it appears, so a control over it could
  // only ever produce a project that will not build.
  extensions: "the compiler refuses any non-empty value",
  // A picture choice about a person, asked before the author has made one.
  // It lives on the character, where there is somebody to look at.
  characterFigureId: "belongs to a character, not to the game",
  // The format carries it and the compiler reads it, so a project may ask to be
  // drawn with models today. Nothing draws them yet: no shipped ROM has a mesh
  // path, and the roster of solids is not good enough to offer. A control for a
  // setting that changes nothing an author can see is worse than no control --
  // it reads as a feature and behaves as a no-op. It goes on the settings page
  // the day a build honours it.
  characterGeometry: "nothing draws models yet; the control would do nothing",
  // Half of one rule, and the half on its own is meaningless: what the better
  // weapon is worth says nothing until some kind of weapon beats another. So
  // it is authored where the edges are, on the weapon triangle beside the
  // weapon types, where an author sets both halves in one place and is told
  // when they have written one without the other. A number here on the
  // settings page, a long way from the ticks that give it something to price,
  // would be a rule an author had to assemble from two rooms.
  weaponAdvantage: "authored on the weapon triangle, beside the kinds it prices"
};

export function sourceProjectFieldNames(): readonly string[] {
  const schema = byId.get(
    "https://grandleon.dev/schemas/source/v1/project.schema.json"
  );
  if (!schema?.properties) {
    throw new Error("source project schema was not generated");
  }
  return Object.entries(schema.properties).flatMap(([name, property]) =>
    resolve(property).type === "array" ? [] : [name]
  );
}

/**
 * The project fields an author may change, which is all of them but the two the
 * machine owns: the schema version this project is written in, and the durable
 * package identity nobody types and nothing may renumber.
 */
export function sourceEditableProjectFields(): readonly SourceFieldDescriptor[] {
  return projectFields(
    sourceProjectFieldNames().filter(
      (name) => name !== "schemaVersion" && name !== "packageId"
    )
  );
}

/**
 * What the project is, rather than what it plays like: the version the machine
 * owns, the durable package identity nobody types, and the author's own scratch
 * fields, which the game never reads. None of it changes how a game plays,
 * which is why none of it stands on the page an author lands on.
 *
 * The choices that shape the whole game are on the settings page below, and
 * they are moved rather than copied, so there is one control per stored field.
 * `gameId` went with them, behind that page's Advanced fold: it is a name for
 * the game, so it belongs beside the title, and it is only ever the name an
 * export downloads under. Nothing else is made of it: a kept campaign is
 * filed under `packageId` (`campaign-slot-store.ts`) and the compiler never
 * reads it, which is what makes it safe to derive from the title.
 */
export function sourceProjectMetadataFields(): readonly SourceFieldDescriptor[] {
  return projectFields([
    "schemaVersion",
    "packageId",
    "notes"
  ]);
}

/**
 * The settings that shape a whole game rather than one record: what it is
 * called, what it is called to the machine, which revision it is, how its
 * Stages are ordered, what a fall costs the company, and the style, figure
 * and season it is drawn in. The three drawing choices are defaults every
 * record follows and none of them is a restriction: a character may name a
 * style and a figure of its own, and the terrain a board is offered first is
 * ordered by the style rather than limited by it.
 *
 * `gameId` and `contentRevision` are on this page and behind its fold. They
 * are questions two and three of the whole product and neither means anything
 * before a game exists. The id is one decision said twice, the name a player
 * reads and the name a file carries, so the title answers it, and the
 * revision starts at a number. An author who wants to disagree with either
 * finds them where the game is named, which is here.
 *
 * Every entry here is a choice about the game. The testing aid below is not,
 * which is why it has a list of its own rather than a place at the end of this
 * one.
 */
export function sourceGameRuleFields(): readonly SourceFieldDescriptor[] {
  return projectFields([
    "title",
    "gameId",
    "contentRevision",
    "defaultTurnOrder",
    "characterLoss",
    "characterStyleId",
    "themeId"
  ]);
}

/**
 * The settings on this page that are aids for testing a game rather than
 * statements about what the game is.
 *
 * They are a separate list because they are a separate kind of thing, and an
 * author reading down one column of controls would otherwise take the last of
 * them for one more choice about their game. The settings page shows them
 * under their own heading, with prose saying what they are, and nothing here
 * ever appears among the rules above.
 *
 * They are not, however, a debug switch that stays behind: each of them is
 * compiled into the package and changes what the Stages do, so the game a
 * player is handed is the game the author left the switch on for. The help
 * text on each field is where that is said to the author.
 */
export function sourceTestingAidFields(): readonly SourceFieldDescriptor[] {
  return projectFields(["invulnerableForTesting"]);
}

/**
 * Everything the game settings page owns, rules and testing aids together.
 *
 * This is the partition claim rather than the page layout: no stored field is
 * offered both here and on the project metadata form. The page itself renders
 * the two lists above separately, because what they mean to an author is not
 * the same thing.
 */
export function sourceGameSettingsFields(): readonly SourceFieldDescriptor[] {
  return [...sourceGameRuleFields(), ...sourceTestingAidFields()];
}
