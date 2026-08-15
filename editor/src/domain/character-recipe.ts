// SPDX-License-Identifier: MIT
// Building one character means creating a weapon type, a weapon, a class, and
// a unit type, in that order, each referring to the last. That chain is the
// abstraction hierarchy standing between a child and one soldier on a map, and
// it is not depth worth removing. It is depth worth having a front door to.
//
// This module produces the whole chain from one plain-language choice, and the
// weapon half of it on its own for an author who wants a second bow rather than
// a second archer. Every record it makes is an ordinary record afterwards:
// individually listed, individually editable, individually deletable. Nothing
// here is a special kind of content, and nothing is hidden.
//
// The catalogue is copy-on-use and never a live reference. An author who
// inserts from it owns what they get, and no later change here can reach into
// a project that already exists: nothing written below records where it came
// from, so there is nothing to migrate and nothing that a repository update
// could silently change underneath an author. The `setting` tag exists only to
// filter what is offered; it is not written into any record.

import type {
  SourceAbility,
  SourceClass,
  SourceProject,
  SourceUnitType,
  SourceWeapon,
  SourceWeaponType
} from "../generated/source-v1";
import { areaOffsets, areaRadius, type AreaShape } from "./targeting-geometry";

/**
 * The roles the catalogue offers, which are the archetypes the art can draw.
 * The two lists are the same list on purpose: a role the art cannot draw would
 * be a role that inserts as somebody else's picture, and a drawing no role
 * offers is art an author cannot reach.
 */
export type CharacterRole =
  | "knight"
  | "archer"
  | "mage"
  | "stormcaller"
  | "healer"
  | "commander"
  | "rogue"
  | "beast";

/**
 * A browse filter over the catalogue, and nothing else.
 *
 * A setting is not `characterStyleId`. The style is a project's own choice and
 * decides how every character is drawn; a setting decides only which names and
 * weapons this catalogue offers. Keeping them apart is what stops "which genre
 * is this" from becoming something a rule could read.
 */
export type CatalogueSetting =
  | "medieval"
  | "scifi"
  | "mythical"
  | "nature"
  | "sengoku"
  | "undead"
  | "pirates";

export interface CharacterRecipe {
  /**
   * A stable key for one catalogue entry, for lists and test assertions. It is
   * a catalogue identity and never reaches a project.
   */
  id: string;
  role: CharacterRole;
  setting: CatalogueSetting;
  /** What a person would call this, before identifiers exist. */
  label: string;
  summary: string;
  weaponTypeName: string;
  weaponName: string;
}

/**
 * One weapon the catalogue offers on its own, rather than in a character's
 * hands.
 *
 * There is no second table behind this. A weapon entry is the armament of one
 * role in one setting: the very weapon that role arrives holding. So the bow an
 * author inserts alone is the same bow an archer comes with, and the two can
 * never drift apart. The role is carried here because the role is where the
 * numbers live, not because a weapon belongs to a role: any character whose
 * class permits the weapon's type can hold it.
 */
export interface WeaponRecipe {
  /**
   * A stable key for one catalogue entry, for lists and test assertions. It is
   * a catalogue identity and never reaches a project.
   */
  id: string;
  role: CharacterRole;
  setting: CatalogueSetting;
  /** What a person would call this weapon. */
  label: string;
  /** The name of the weapon type this weapon belongs to. */
  weaponTypeName: string;
  /** The character of this setting that fights with it. */
  carriedBy: string;
  summary: string;
}

/**
 * What the rules do, per role. Shared by every setting, because the archetypes
 * are tactical roles that happen to have medieval names: a sniper and an
 * archer are the same reach band drawn differently, and giving them different
 * numbers would make the setting a rules input.
 */
interface RoleShape {
  /** Reach band. A bow that cannot strike an adjacent enemy is authored here. */
  minimumRange: number;
  maximumRange: number;
  power: number;
  health: number;
  movement: number;
  strength: number;
  defense: number;
  speed: number;
  /**
   * Commands one activation buys, and two on every role in the table.
   *
   * The format admits one and means it: a character with a single point may
   * move or strike and never both, because walking is a command and a command
   * costs a point. That is a real archetype and an author may write it. What it
   * is not is something a shelf may hand out on an author's behalf. A card here
   * is chosen by what a character *is*, and nobody picking "knight" is picking a
   * knight who is finished by its own first step; they would find out on a board
   * they had already built, from a strike the game refused with nothing on
   * screen explaining why.
   *
   * So the number is uniform, and deliberately so rather than by coincidence:
   * a role differs from its neighbours by reach, by what it survives and by how
   * far it gets, which are differences a player can see being made. Whether a
   * turn works at all is not one of them.
   */
  actionPoints: number;
  actsAfterAttacking: boolean;
}

/**
 * What one setting calls a role, and what it puts in its hands.
 *
 * Names. In exactly one place in the whole catalogue, also a way of moving.
 * See `traversal` below for why that one exception exists and what fences it.
 */
interface RoleDress {
  label: string;
  weaponTypeName: string;
  weaponName: string;
  /**
   * How this dress crosses ground, when it crosses ground differently from
   * everybody else.
   *
   * The catalogue's standing discipline is that **numbers belong to the role
   * and names belong to the setting**: a sniper and an archer are the same
   * reach band and the same stat block, so a shelf stays comparable across
   * settings and no setting becomes something a rule reads. This field is the
   * one deliberate hole in it: flight is an authorable movement capability and
   * the stats stay the recipe's to set, because a dragon that walks is not a
   * dragon.
   *
   * It is kept as narrow as that sentence. Only movement may live here, never
   * a stat, a reach band or a power. `TRAVERSING_ENTRY` below names the single
   * dress that uses it, with a test asserting there is no second, so the
   * exception cannot spread into the next commission by copy-paste.
   */
  traversal?: NonNullable<SourceClass["traversal"]>;
}

const shapes: Readonly<Record<CharacterRole, RoleShape>> = {
  knight: {
    minimumRange: 1,
    maximumRange: 1,
    power: 4,
    health: 12,
    movement: 3,
    strength: 5,
    defense: 3,
    speed: 3,
    actionPoints: 2,
    actsAfterAttacking: false
  },
  archer: {
    minimumRange: 2,
    maximumRange: 3,
    power: 3,
    health: 8,
    movement: 4,
    strength: 4,
    defense: 1,
    speed: 6,
    actionPoints: 2,
    actsAfterAttacking: false
  },
  mage: {
    minimumRange: 1,
    maximumRange: 2,
    power: 5,
    health: 7,
    movement: 3,
    strength: 5,
    defense: 1,
    speed: 4,
    actionPoints: 2,
    actsAfterAttacking: false
  },
  stormcaller: {
    minimumRange: 2,
    maximumRange: 4,
    power: 6,
    health: 6,
    movement: 2,
    strength: 5,
    defense: 1,
    speed: 2,
    actionPoints: 2,
    actsAfterAttacking: false
  },
  healer: {
    minimumRange: 1,
    maximumRange: 2,
    power: 1,
    health: 8,
    movement: 3,
    strength: 2,
    defense: 1,
    speed: 5,
    actionPoints: 2,
    actsAfterAttacking: true
  },
  commander: {
    minimumRange: 1,
    maximumRange: 1,
    power: 5,
    health: 14,
    movement: 4,
    strength: 6,
    defense: 3,
    speed: 5,
    actionPoints: 2,
    actsAfterAttacking: false
  },
  rogue: {
    minimumRange: 1,
    maximumRange: 1,
    power: 3,
    health: 7,
    movement: 5,
    strength: 4,
    defense: 1,
    speed: 9,
    actionPoints: 2,
    actsAfterAttacking: true
  },
  beast: {
    minimumRange: 1,
    maximumRange: 1,
    power: 4,
    health: 10,
    movement: 6,
    strength: 5,
    defense: 1,
    speed: 7,
    actionPoints: 2,
    actsAfterAttacking: false
  }
};

/**
 * The settings the catalogue is browsable by, in menu order.
 *
 * These are the settings the art can dress a roster in today, and the list is
 * the same list `styles.py` holds for exactly that reason. A setting names a
 * genre, and every one of them offers all eight roles: knight, archer, mage,
 * stormcaller, healer, commander, rogue and beast, a roster closed at eight
 * because a ninth would cost one draw routine in every style. Filtering decides
 * what is easy to find, and never what an author is allowed to make.
 *
 * A setting is still not a `characterStyleId`. A project names one style and a
 * console build embeds only that style's art, so a medieval campaign cannot
 * show a dragon's *picture*. But a dragon's *records* are style-independent
 * gameplay data and any project may hold them. That separation is what the
 * gallery makes visible by previewing every card in the project's own style.
 *
 * A project is meant to be able to draw one character in another setting's
 * style, but the sentence above still describes what a build does, because the
 * mechanism for it is not built. When it is, it belongs here rather than in a
 * setting.
 */
export const CATALOGUE_SETTINGS: readonly {
  readonly id: CatalogueSetting;
  readonly label: string;
  readonly summary: string;
}[] = [
  {
    id: "medieval",
    label: "Medieval",
    summary: "Knights, longbows and staves."
  },
  {
    id: "scifi",
    label: "Sci-fi",
    summary: "The same eight roles in powered armour."
  },
  {
    id: "mythical",
    label: "Mythical",
    summary: "Dragons and the folk who live beside them."
  },
  {
    id: "nature",
    label: "Nature",
    summary: "Animal folk: bears, cats, badgers and a boar."
  },
  {
    id: "sengoku",
    label: "Sengoku Japan",
    summary: "The Warring States: lacquer, the naginata and the yumi."
  },
  {
    id: "undead",
    label: "Undead",
    summary: "The same eight roles in bone, shroud and rust."
  },
  {
    id: "pirates",
    label: "Pirates",
    summary: "A ship's crew: tar, brass, sailcloth and a parrot."
  }
];

/**
 * The shelf a project is offered first: its own style, when the catalogue has a
 * shelf for it, and the first shelf otherwise.
 *
 * A style is not a setting: the style decides how a character is drawn and the
 * setting decides only what this catalogue offers. But a game drawn in lacquer
 * and bamboo is very likely a game whose next character has a Sengoku name,
 * and guessing is free where every other shelf is one press away. Written here rather than at each surface that needs it, because two
 * doors onto the same catalogue guessing differently would be two catalogues.
 */
export function shelfSetting(styleId: string | undefined): CatalogueSetting {
  return CATALOGUE_SETTINGS.some((setting) => setting.id === styleId)
    ? (styleId as CatalogueSetting)
    : CATALOGUE_SETTINGS[0]!.id;
}

const dress: Readonly<
  Record<CatalogueSetting, Readonly<Record<CharacterRole, RoleDress>>>
> = {
  medieval: {
    knight: { label: "Knight", weaponTypeName: "Blade", weaponName: "Sword" },
    archer: { label: "Archer", weaponTypeName: "Bow", weaponName: "Longbow" },
    mage: { label: "Mage", weaponTypeName: "Staff", weaponName: "Ember Staff" },
    stormcaller: {
      label: "Stormcaller",
      weaponTypeName: "Staff",
      weaponName: "Storm Staff"
    },
    healer: {
      label: "Healer",
      weaponTypeName: "Staff",
      weaponName: "Mending Staff"
    },
    commander: {
      label: "Commander",
      weaponTypeName: "Blade",
      weaponName: "Officer's Blade"
    },
    rogue: { label: "Rogue", weaponTypeName: "Blade", weaponName: "Dagger" },
    beast: { label: "Wolf", weaponTypeName: "Claws", weaponName: "Fangs" }
  },
  scifi: {
    knight: {
      label: "Trooper",
      weaponTypeName: "Sidearm",
      weaponName: "Shock Baton"
    },
    archer: {
      label: "Sniper",
      weaponTypeName: "Rifle",
      weaponName: "Rail Rifle"
    },
    mage: { label: "Psion", weaponTypeName: "Emitter", weaponName: "Mind Lance" },
    stormcaller: {
      label: "Drone swarm",
      weaponTypeName: "Emitter",
      weaponName: "Swarm Emitter"
    },
    healer: {
      label: "Medic",
      weaponTypeName: "Emitter",
      weaponName: "Repair Beam"
    },
    commander: {
      label: "Captain",
      weaponTypeName: "Sidearm",
      weaponName: "Officer's Sidearm"
    },
    rogue: {
      label: "Infiltrator",
      weaponTypeName: "Sidearm",
      weaponName: "Vibroknife"
    },
    beast: {
      label: "Xenoform",
      weaponTypeName: "Talons",
      weaponName: "Talons"
    }
  },
  mythical: {
    knight: {
      label: "Drakeguard",
      weaponTypeName: "Lance",
      weaponName: "Drake Lance"
    },
    archer: {
      label: "Wyrm-hunter",
      weaponTypeName: "Crossbow",
      weaponName: "Horn Crossbow"
    },
    mage: { label: "Runecaster", weaponTypeName: "Rune", weaponName: "Ember Rune" },
    stormcaller: {
      label: "Stormsinger",
      weaponTypeName: "Rune",
      weaponName: "Storm Rune"
    },
    healer: {
      label: "Wyrmpriest",
      weaponTypeName: "Rune",
      weaponName: "Mending Rune"
    },
    commander: {
      label: "Dragonlord",
      weaponTypeName: "Lance",
      weaponName: "Wyrmlord's Lance"
    },
    rogue: { label: "Scalethief", weaponTypeName: "Fang", weaponName: "Serpent Fang" },
    // The one dress in the catalogue that carries a movement capability. See
    // `RoleDress.traversal`: a dragon that walks is not a dragon, and flight is
    // the one hole cut in "numbers belong to the role". Nothing else about it
    // differs from any other `beast`: same health, same movement, same reach,
    // same power.
    beast: {
      label: "Dragon",
      weaponTypeName: "Fang",
      weaponName: "Dragon Fang",
      traversal: { flying: true }
    }
  },
  nature: {
    knight: {
      label: "Badger guard",
      weaponTypeName: "Claw",
      weaponName: "Badger Claws"
    },
    archer: { label: "Archer cat", weaponTypeName: "Bow", weaponName: "Reed Bow" },
    mage: {
      label: "Mage bear",
      weaponTypeName: "Branch",
      weaponName: "Acorn Branch"
    },
    stormcaller: {
      label: "Storm stag",
      weaponTypeName: "Branch",
      weaponName: "Storm Branch"
    },
    healer: {
      label: "Healer owl",
      weaponTypeName: "Branch",
      weaponName: "Mending Branch"
    },
    commander: {
      label: "Lion warden",
      weaponTypeName: "Claw",
      weaponName: "Lion's Claws"
    },
    rogue: { label: "Stoat", weaponTypeName: "Claw", weaponName: "Bone Knives" },
    beast: { label: "Boar", weaponTypeName: "Tusk", weaponName: "Boar Tusks" }
  },
  // The Warring States. The names are romanised without macrons on purpose:
  // `slug()` below keeps only `[a-z0-9]`, so "Onmyōji" would insert records
  // identified `onmy_ji_mage_class`, and the identifier an author lives with is
  // worth more than the diacritic.
  sengoku: {
    knight: {
      label: "Samurai",
      weaponTypeName: "Naginata",
      weaponName: "Curved Naginata"
    },
    archer: { label: "Yumi archer", weaponTypeName: "Yumi", weaponName: "Bamboo Yumi" },
    mage: { label: "Onmyoji", weaponTypeName: "Ofuda", weaponName: "Ember Ofuda" },
    stormcaller: {
      label: "Kagura dancer",
      weaponTypeName: "Ofuda",
      weaponName: "Storm Ofuda"
    },
    healer: {
      label: "Temple monk",
      weaponTypeName: "Ofuda",
      weaponName: "Mending Ofuda"
    },
    commander: {
      label: "Daimyo",
      weaponTypeName: "Sword",
      weaponName: "Lord's Tachi"
    },
    rogue: { label: "Shinobi", weaponTypeName: "Sword", weaponName: "Wakizashi" },
    // A fox spirit is a `beast` that walks. `RoleDress.traversal` is a fence
    // with one occupant and this commission does not climb it: numbers belong
    // to the role, and flight is the only hole cut in that rule.
    beast: { label: "Shrine fox", weaponTypeName: "Fang", weaponName: "Fox Fangs" }
  },
  // The first setting drawn as the force a player fights rather than the one
  // they field. That reading is in the pictures and in these names, and it
  // stops there: the numbers are the roles' own, the entries are offered in
  // every faction colour like every other setting's, and not one of them
  // carries a `traversal`. The Wraith is drawn with no legs and a clear gap
  // over its own ground, which is a name rather than a number. See
  // `RoleDress.traversal` and `TRAVERSING_ENTRY` below.
  undead: {
    knight: {
      label: "Barrow knight",
      weaponTypeName: "Grave-iron",
      weaponName: "Rusted Blade"
    },
    archer: { label: "Bonepicker", weaponTypeName: "Bow", weaponName: "Bone Bow" },
    mage: { label: "Wraith", weaponTypeName: "Dirge", weaponName: "Cold Dirge" },
    stormcaller: {
      label: "Bellringer",
      weaponTypeName: "Dirge",
      weaponName: "Passing Bell"
    },
    healer: {
      label: "Mourner",
      weaponTypeName: "Dirge",
      weaponName: "Mending Dirge"
    },
    commander: {
      label: "Barrow lord",
      weaponTypeName: "Grave-iron",
      weaponName: "Barrow Blade"
    },
    rogue: {
      label: "Grave-thief",
      weaponTypeName: "Grave-iron",
      weaponName: "Rusted Picks"
    },
    beast: { label: "Bone hound", weaponTypeName: "Jaws", weaponName: "Hound's Jaws" }
  },
  // A crew. Note what is *not* here: the Parrot carries no `traversal`. A bird
  // that flies is the most tempting exception in the whole table and it does
  // not get one: the numbers belong to the role, and `TRAVERSING_ENTRY` above
  // still names exactly one dress in the entire catalogue.
  pirates: {
    knight: {
      label: "Boarder",
      weaponTypeName: "Cutlass",
      weaponName: "Boarding Cutlass"
    },
    archer: {
      label: "Musketeer",
      weaponTypeName: "Musket",
      weaponName: "Sea Musket"
    },
    mage: { label: "Hexer", weaponTypeName: "Charm", weaponName: "Bone Charm" },
    stormcaller: {
      label: "Gunner",
      weaponTypeName: "Gun",
      weaponName: "Swivel Gun"
    },
    healer: {
      label: "Surgeon",
      weaponTypeName: "Charm",
      weaponName: "Ship's Lantern"
    },
    commander: {
      label: "Captain",
      weaponTypeName: "Cutlass",
      weaponName: "Captain's Hanger"
    },
    rogue: {
      label: "Cutpurse",
      weaponTypeName: "Cutlass",
      weaponName: "Gutting Knife"
    },
    beast: { label: "Parrot", weaponTypeName: "Talons", weaponName: "Beak and Claws" }
  }
};

/**
 * The one catalogue entry that crosses ground differently, named here so the
 * exception is a single fact rather than a search.
 *
 * A test asserts that this is the *only* dress in the whole table carrying a
 * `traversal`, which is what keeps `RoleDress.traversal`'s fence standing: a
 * later commission that wanted a second flier would have to move this line and
 * say why, rather than adding one quietly.
 */
export const TRAVERSING_ENTRY = "mythical_beast";

/**
 * The roles, in the order the art registers its archetypes. Stated here rather
 * than imported from the generated manifest so this module owns its own menu;
 * a test asserts the two lists stay the same list.
 */
export const CHARACTER_ROLES: readonly CharacterRole[] = [
  "knight",
  "archer",
  "mage",
  "stormcaller",
  "healer",
  "commander",
  "rogue",
  "beast"
];

/**
 * A reach band as a card reads it, from the two numbers the record gets.
 *
 * One fragment for every shelf that has a band, because a role, a weapon and a
 * cast measure reach identically: the engine calls the same Manhattan
 * `distance()` for all three. Two shelves phrasing one band differently would
 * read as two different rules.
 *
 * Reach is the whole of what a role card says about the rules, and that is the
 * point of the card: it is where a *kind* is chosen. Health, speed, actions and
 * whether striking ends a turn are a character's numbers, read where a
 * character's numbers are read.
 */
function range(minimumRange: number, maximumRange: number): string {
  return minimumRange === maximumRange
    ? `Range ${maximumRange} tile${maximumRange === 1 ? "" : "s"}.`
    : `Range ${minimumRange}–${maximumRange} tiles.`;
}

/**
 * How a dress crosses ground, stated from the record it will get.
 *
 * Derived rather than written beside the data, exactly as `range()` is, so an
 * entry that says it flies is an entry whose class flies. An entry that crosses
 * nothing special says nothing.
 */
function crossing(dressed: RoleDress): string {
  const traversal = dressed.traversal;
  if (!traversal) return "";
  const said: string[] = [];
  if (traversal.flying) said.push("Flies.");
  const crossings = traversal.crossings ?? [];
  if (crossings.length > 0) said.push(`Crosses ${[...crossings].join(" and ")}.`);
  return said.length > 0 ? ` ${said.join(" ")}` : "";
}

/**
 * The whole catalogue: every role, in every setting.
 *
 * The summary is the role's, not the setting's, because a sniper and an archer
 * are the same reach band. What a setting changes is what the role is called,
 * what it is holding, and, for the one entry that carries a `traversal`, how
 * it crosses ground.
 */
export const characterRecipes: readonly CharacterRecipe[] =
  CATALOGUE_SETTINGS.flatMap((setting) =>
    CHARACTER_ROLES.map((role) => ({
      id: `${setting.id}_${role}`,
      role,
      setting: setting.id,
      label: dress[setting.id][role].label,
      summary:
        range(shapes[role].minimumRange, shapes[role].maximumRange) +
        crossing(dress[setting.id][role]),
      weaponTypeName: dress[setting.id][role].weaponTypeName,
      weaponName: dress[setting.id][role].weaponName
    }))
  );

/**
 * What a weapon does, stated from the same numbers the record gets.
 *
 * Derived from the reach band and the power rather than written beside them, so
 * a card cannot drift from the weapon it makes. It says what the numbers are
 * and stops there. How good they are is not something this repository can
 * check, so it is not something the catalogue claims.
 */
function reach(shape: RoleShape): string {
  return `${range(shape.minimumRange, shape.maximumRange)} Power ${shape.power}.`;
}

/**
 * Every weapon the catalogue's characters carry, offered on its own.
 *
 * One per role per setting, the same eight roles in every one of them, because
 * the shelf is exactly the armoury the character shelf draws from. A weapon a
 * character could arrive holding but an author could not add separately would
 * be a gap with no reason behind it.
 */
export const weaponRecipes: readonly WeaponRecipe[] =
  CATALOGUE_SETTINGS.flatMap((setting) =>
    CHARACTER_ROLES.map((role) => ({
      id: `${setting.id}_${role}_weapon`,
      role,
      setting: setting.id,
      label: dress[setting.id][role].weaponName,
      weaponTypeName: dress[setting.id][role].weaponTypeName,
      carriedBy: dress[setting.id][role].label,
      summary: reach(shapes[role])
    }))
  );

/**
 * The casts the catalogue offers, in shelf order.
 *
 * Six, not a grid. The ability vocabulary is four axes wide: a kind, a damage
 * type, an area and a reach band, with an accuracy on top. Every product of
 * them is a menu nobody reads. These six are chosen so that each is the only
 * entry on the shelf demonstrating something:
 *
 *   * `heavy_blow` is the only cast that can miss;
 *   * `bolt` is the same single tile as `heavy_blow`, resolved against
 *     resistance instead of defence;
 *   * `sweep` is the only physical area;
 *   * `storm` is the only radius above one, and the only band with a floor;
 *   * `mend` is the other `kind`;
 *   * `mend_all` is there to show the area axis is not tied to damage.
 *
 * Nothing here says a cast belongs to a role. An ability is a plain record
 * referenced by `abilityIds` on a character, and any character may hold any of
 * them; the shelf is a vocabulary, not an assignment.
 */
export type AbilityCast =
  | "heavy_blow"
  | "bolt"
  | "sweep"
  | "storm"
  | "mend"
  | "mend_all";

export const ABILITY_CASTS: readonly AbilityCast[] = [
  "heavy_blow",
  "bolt",
  "sweep",
  "storm",
  "mend",
  "mend_all"
];

/**
 * What the rules do, per cast. Shared by every setting, for the same reason a
 * role's stat block is: an onibi and an ember bolt are one spell drawn in two
 * alphabets, and giving them different numbers would make the setting a rules
 * input.
 */
interface AbilityShape {
  kind: NonNullable<SourceAbility["kind"]>;
  /**
   * Which defence the strike is resolved against. Written only for a damaging
   * cast: the schema says this field is ignored when the kind is `restore`, and
   * an ignored field in an author's record is a field they will one day change
   * and watch do nothing.
   */
  damageType?: NonNullable<SourceAbility["damageType"]>;
  areaShape: AreaShape;
  /** Read by the engine only for `diamond`, and written only for it. */
  radius?: number;
  power: number;
  minimumRange: number;
  maximumRange: number;
  /**
   * Omitted means the cast always lands and rolls nothing, which is the
   * schema's own default rather than a shorthand this table invented.
   */
  accuracy?: number;
}

const casts: Readonly<Record<AbilityCast, AbilityShape>> = {
  // The whole reason `accuracy` is in the vocabulary: the largest single number
  // on the shelf, bought with the only roll on it. Its numbers are the Dawn
  // Guard's Power Strike exactly, because the shipped campaign already made
  // this trade and the library's job is to make it reachable.
  heavy_blow: {
    kind: "damage",
    damageType: "physical",
    areaShape: "single",
    power: 6,
    minimumRange: 1,
    maximumRange: 1,
    accuracy: 85
  },
  bolt: {
    kind: "damage",
    damageType: "magical",
    areaShape: "single",
    power: 4,
    minimumRange: 1,
    maximumRange: 2
  },
  sweep: {
    kind: "damage",
    damageType: "physical",
    areaShape: "cross",
    power: 3,
    minimumRange: 1,
    maximumRange: 1
  },
  // The only `diamond` on the shelf, and it is at radius 2 rather than 1 on
  // purpose. `covered_by()` compares the Manhattan distance against 1 for a
  // cross and against the radius for a diamond, so a diamond of radius 1 *is* a
  // cross. Offering both would be one shape sold twice.
  storm: {
    kind: "damage",
    damageType: "magical",
    areaShape: "diamond",
    radius: 2,
    power: 2,
    minimumRange: 2,
    maximumRange: 4
  },
  mend: {
    kind: "restore",
    areaShape: "single",
    power: 4,
    minimumRange: 1,
    maximumRange: 2
  },
  mend_all: {
    kind: "restore",
    areaShape: "cross",
    power: 2,
    minimumRange: 1,
    maximumRange: 2
  }
};

/**
 * What each setting calls each cast.
 *
 * A flat name table rather than a `RoleDress`-shaped one, because a cast has
 * nothing else a setting is allowed to change. `RoleDress` carries a weapon
 * type and a weapon name beside the label, plus, in one deliberate hole, a
 * movement capability. An ability record has no equivalent field, so there is
 * nothing here for a later commission to widen, which is the cheapest way to
 * keep a fence standing: give it nothing to climb.
 *
 * Medieval's `heavy_blow`, `bolt` and `mend` are Tarnholt's Power Strike, Ember
 * Bolt and Mend, name and numbers both. That is the point of the shelf: those
 * three are shipped, hand-authored inside one campaign, and invisible to
 * anyone browsing the library unless the shelf names them. The three that
 * Tarnholt has no equivalent for are named freshly, so a name never promises
 * numbers it does not carry.
 */
const spellbook: Readonly<
  Record<CatalogueSetting, Readonly<Record<AbilityCast, string>>>
> = {
  medieval: {
    heavy_blow: "Power Strike",
    bolt: "Ember Bolt",
    sweep: "Sweeping Blow",
    storm: "Storm Circle",
    mend: "Mend",
    mend_all: "Circle of Mending"
  },
  scifi: {
    heavy_blow: "Overload Strike",
    bolt: "Neural Bolt",
    sweep: "Shock Sweep",
    storm: "Ion Storm",
    mend: "Nanite Patch",
    mend_all: "Repair Field"
  },
  mythical: {
    heavy_blow: "Drake Strike",
    bolt: "Runebolt",
    sweep: "Tail Sweep",
    storm: "Stormsong",
    mend: "Scale-mend",
    mend_all: "Mending Chorus"
  },
  nature: {
    heavy_blow: "Antler Charge",
    bolt: "Acorn Bolt",
    sweep: "Claw Sweep",
    storm: "Rootstorm",
    mend: "Bark Balm",
    mend_all: "Grove Balm"
  },
  // Romanised without macrons, for the reason the dress table already gives:
  // `slug()` keeps only `[a-z0-9]`, and the identifier an author lives with is
  // worth more than the diacritic.
  sengoku: {
    heavy_blow: "Iai Strike",
    bolt: "Onibi",
    sweep: "Naginata Sweep",
    storm: "Thunder Drum",
    mend: "Healing Prayer",
    mend_all: "Shrine Blessing"
  },
  undead: {
    heavy_blow: "Grave Blow",
    bolt: "Chill Bolt",
    sweep: "Scythe Sweep",
    storm: "Wailing Storm",
    mend: "Bone-knit",
    mend_all: "Grave Vigil"
  },
  pirates: {
    heavy_blow: "Cutlass Cleave",
    bolt: "Hex Bolt",
    sweep: "Boarding Sweep",
    storm: "Squall",
    mend: "Surgeon's Stitch",
    mend_all: "Surgeon's Round"
  }
};

export interface AbilityRecipe {
  /**
   * A stable key for one catalogue entry, for lists and test assertions. It is
   * a catalogue identity and never reaches a project.
   */
  id: string;
  cast: AbilityCast;
  setting: CatalogueSetting;
  /** What this setting calls it. */
  label: string;
  summary: string;
}

/**
 * What a cast does to whoever it catches, and which defence answers.
 *
 * That an area covers everyone standing in it while a damaging one hurts only
 * the other side is one fact about the whole vocabulary, so it is stated once on
 * the shelf's surface rather than on forty-two cards.
 */
function effect(cast: AbilityShape): string {
  if (cast.kind === "restore") return "Restores health.";
  return cast.damageType === "magical"
    ? "Damage, against resistance."
    : "Damage, against defence.";
}

/**
 * What the area covers, counted rather than claimed.
 *
 * The tile count comes from `targeting-geometry`, which is the one place this
 * editor holds the covering rule and the place `targeting-agreement.test.ts`
 * pins against the running engine. A shelf that counted its own tiles could
 * promise five and deliver something else the moment either changed.
 *
 * A cross and a diamond come out as the same sentence with a different number,
 * which is not a shortcut: `covered_by()` is one Manhattan-ball test at radius
 * 0, 1 and N, so three names in the schema are one shape family on the board,
 * and a summary implying three different shapes would be a summary teaching an
 * author something untrue.
 */
function coverage(cast: AbilityShape): string {
  const radius = areaRadius(cast.areaShape, cast.radius);
  // Silence is the single tile, which is what most of the shelf does. Only a
  // cast that catches more than the tile it was aimed at has anything to say.
  if (radius === 0) return "";
  const covered = areaOffsets(cast.areaShape, cast.radius, radius).length;
  return ` Covers ${covered} tiles.`;
}

/**
 * Whether the cast rolls, and how often it lands when it does.
 *
 * Silent for a restoring cast, because the schema is: a restore never rolls, so
 * an accuracy written on one is a number that does nothing. A damaging cast
 * always says, including when it says "always". An ability that cannot miss is
 * the most surprising thing about this vocabulary, and leaving it unsaid would
 * make the one entry that can miss look like the only one with an accuracy
 * rather than the only one below a hundred.
 */
function rolls(cast: AbilityShape): string {
  if (cast.kind === "restore") return "";
  return cast.accuracy === undefined
    ? " Always lands."
    : ` Lands ${cast.accuracy} in 100.`;
}

/**
 * The whole ability shelf: every cast, in every setting.
 *
 * The summary is the cast's, not the setting's, for the reason the other two
 * shelves give: what a setting changes is what a thing is called.
 *
 * Every clause is derived from the shape the record will carry, exactly as
 * `reach()` is, so a description cannot drift from its numbers. What it does
 * not do is rank: this repository has no balance harness, so "covers 5 tiles at
 * power 2" is said and "wider but weaker" is not, because the second half is a
 * claim nothing here could check.
 */
export const abilityRecipes: readonly AbilityRecipe[] =
  CATALOGUE_SETTINGS.flatMap((setting) =>
    ABILITY_CASTS.map((cast) => ({
      id: `${setting.id}_${cast}`,
      cast,
      setting: setting.id,
      label: spellbook[setting.id][cast],
      summary:
        effect(casts[cast]) + coverage(casts[cast]) +
        ` Power ${casts[cast].power}. ` +
        range(casts[cast].minimumRange, casts[cast].maximumRange) +
        rolls(casts[cast])
    }))
  );

/**
 * Builds one ability, as an ordinary record.
 *
 * One record and no chain: an ability references nothing, so unlike a character
 * it needs nothing built beside it. What it needs is a *carrier*, and that is
 * deliberately not done here. `abilityIds` is a field on a character an author
 * edits like any other, and a shelf that silently attached its output to
 * somebody would be a shelf whose effects an author could not predict from what
 * they clicked.
 *
 * Only the fields the shape carries are written. A restoring cast gets no
 * `damageType` and no `accuracy`, and a `single` or `cross` gets no `radius`,
 * because the schema says the engine ignores each of those, and a field that
 * is ignored but present is a field an author will edit and watch do nothing.
 */
export function buildAbility(
  project: SourceProject,
  cast: AbilityCast,
  displayName: string,
  setting: CatalogueSetting = "medieval"
): SourceAbility {
  const shape = casts[cast];
  const name = displayName.trim() || spellbook[setting][cast];
  const abilityIds = new Set((project.abilities ?? []).map((entry) => entry.id));
  return {
    id: unique(slug(name, "ability"), abilityIds),
    name,
    kind: shape.kind,
    ...(shape.kind === "damage" && shape.damageType !== undefined
      ? { damageType: shape.damageType }
      : {}),
    areaShape: shape.areaShape,
    ...(shape.areaShape === "diamond" && shape.radius !== undefined
      ? { radius: shape.radius }
      : {}),
    power: shape.power,
    minimumRange: shape.minimumRange,
    maximumRange: shape.maximumRange,
    ...(shape.accuracy === undefined ? {} : { accuracy: shape.accuracy })
  };
}

/**
 * A weapon and, when the project did not already hold a matching one, the
 * weapon type it references.
 *
 * The two travel together because separately they are not usable. A weapon
 * whose `weaponTypeId` points at nothing is a weapon no class can be told to
 * permit, and the editor's own compatibility notes would have nothing to name.
 */
export interface WeaponPair {
  /** Present only when the project held no matching type and one was made. */
  weaponType?: SourceWeaponType;
  /**
   * Always references a weapon type, whether that type is new or one the
   * project already held. The type is stated in the type so callers need no
   * assertion to ask which one, and cannot skip the question.
   */
  weapon: SourceWeapon & { weaponTypeId: string };
}

export interface CharacterChain extends WeaponPair {
  /**
   * Present only when the project held no class for this role in this setting
   * and one was made. Absent means the character joins a class that is already
   * there. Which class it joins is on the unit type, where it always was.
   *
   * The same shape as `weaponType` above, for the same reason: a class is a
   * shared archetype, so the honest answer to "what does this character need
   * written" is often "nothing, it joins the Healer class you already have".
   */
  unitClass?: SourceClass;
  /** Always names its class, whether that class is new or already there. */
  unitType: SourceUnitType & { classId: string };
}

function copyTraversal(
  traversal: NonNullable<SourceClass["traversal"]>
): NonNullable<SourceClass["traversal"]> {
  const copy: NonNullable<SourceClass["traversal"]> = {};
  if (traversal.flying !== undefined) copy.flying = traversal.flying;
  const crossings = traversal.crossings;
  if (crossings !== undefined) {
    copy.crossings = crossings.slice() as typeof crossings;
  }
  return copy;
}

function slug(value: string, fallback: string): string {
  const cleaned = value
    .toLocaleLowerCase()
    .replace(/[^a-z0-9]+/gu, "_")
    .replace(/^_+|_+$/gu, "");
  // Identifiers must start with a letter, so a name of only digits or symbols
  // still produces something the schema accepts.
  return /^[a-z]/u.test(cleaned) ? cleaned : `${fallback}_${cleaned || "1"}`;
}

function unique(base: string, taken: ReadonlySet<string>): string {
  if (!taken.has(base)) return base;
  let suffix = 2;
  while (taken.has(`${base}_${suffix}`)) suffix += 1;
  return `${base}_${suffix}`;
}

/**
 * What a class of this role in this setting is called, and identified.
 *
 * Both are made of the role and the setting rather than of the character's
 * name, because that is what a class *is*: ten healers are ten characters and
 * one archetype, and a project holding ten identical classes named after ten
 * people has ten copies of one decision to keep in step. It also keeps the two
 * records apart: renaming a character renames a character, and the archetype
 * other characters belong to is untouched.
 *
 * The identifier still carries the role's own word, because that is how every
 * client picks the drawing: the first archetype name found in a lowered class
 * name wins, on the console and in the editor alike. Where the setting's own
 * word for the role already contains it, as "Healer" and "Archer cat" do, it
 * is not said twice.
 */
function classIdentity(
  role: CharacterRole,
  named: RoleDress
): { readonly id: string; readonly name: string } {
  const stem = slug(named.label, "unit");
  return {
    id: stem.includes(role) ? `${stem}_class` : `${stem}_${role}_class`,
    name: `${named.label} class`
  };
}

/**
 * Builds a weapon and pairs it with its weapon type, reusing what already fits.
 *
 * One rule, in one place, for both ways into the catalogue. A weapon type is
 * matched by the name the catalogue gives it and shared rather than duplicated:
 * asking for a second archer, or adding a second bow, should not leave two
 * indistinguishable "Bow" records behind. The weapon itself is always new,
 * because two bows are allowed to diverge afterwards.
 *
 * Sharing is what keeps an inserted weapon usable. A class permits weapon
 * *types*, so a bow that quietly minted "Bow 2" would be a bow no existing
 * archer could hold, and the reason would be invisible in a list where the two
 * records read identically.
 */
function buildWeapon(
  project: SourceProject,
  role: CharacterRole,
  setting: CatalogueSetting,
  weaponId: string,
  weaponName: string
): WeaponPair {
  const shape = shapes[role];
  const named = dress[setting][role];
  const weaponTypeIds = new Set((project.weaponTypes ?? []).map((entry) => entry.id));
  const weaponIds = new Set(project.weapons.map((entry) => entry.id));

  const existingWeaponType = (project.weaponTypes ?? []).find(
    (entry) => entry.name === named.weaponTypeName
  );
  const weaponTypeId = existingWeaponType
    ? existingWeaponType.id
    : unique(slug(named.weaponTypeName, "weapon"), weaponTypeIds);

  return {
    ...(existingWeaponType
      ? {}
      : { weaponType: { id: weaponTypeId, name: named.weaponTypeName } }),
    weapon: {
      id: unique(weaponId, weaponIds),
      name: weaponName,
      weaponTypeId,
      power: shape.power,
      minimumRange: shape.minimumRange,
      maximumRange: shape.maximumRange
    }
  };
}

/**
 * Builds one weapon on its own, with the weapon type that makes it usable.
 *
 * Deliberately the same mechanism a character's weapon goes through, and not a
 * second one. An author who adds a bow and an author who adds an archer both
 * end up pointing at the same "Bow" record, so the archer can be handed the
 * loose bow with no archaeology: the class already permits that type. Two
 * mechanisms would have made whether that works depend on which door the author
 * came through, which is not a distinction anyone could be expected to hold.
 *
 * What comes back is a plain weapon record. It is not reserved for the role
 * whose numbers it carries, nothing marks it as catalogue content, and nothing
 * decides how a character that holds two of them uses them: that is the
 * engine's to say.
 */
export function buildWeaponPair(
  project: SourceProject,
  role: CharacterRole,
  displayName: string,
  setting: CatalogueSetting = "medieval"
): WeaponPair {
  const named = dress[setting][role];
  const name = displayName.trim() || named.weaponName;
  return buildWeapon(project, role, setting, slug(name, "weapon"), name);
}

/**
 * Builds the records one character needs, reusing what already fits.
 *
 * The weapon half goes through `buildWeapon` above, so a weapon type is shared
 * rather than duplicated. **The class is shared the same way**, and by the same
 * rule: it is found by the name a class of this role in this setting carries,
 * and only made when nothing answers to it. A class is the archetype a
 * character belongs to, so ten healers are one Healer class and ten unit
 * types. Minting a tenth identical class named after the tenth healer would
 * make every later change to what a healer is into ten changes.
 *
 * The unit type is always new. That is the record the character *is*, and two
 * characters of the same role are still free to diverge on it: their own
 * numbers, their own weapon, their own style and build.
 *
 * A class is only reused if it permits the weapon type this character arrives
 * holding. A class that has been edited to forbid it is not this role's class
 * any more, whatever it is called, and joining it would hand the character a
 * weapon they cannot draw.
 *
 * The setting decides the names, and, for the one dress that carries a
 * `traversal`, how the class crosses ground. Every stat, reach band and power
 * comes from the role, and the records that come back carry no trace of the
 * catalogue they came from: an author owns them the moment they exist.
 */
export function buildCharacterChain(
  project: SourceProject,
  role: CharacterRole,
  displayName: string,
  setting: CatalogueSetting = "medieval"
): CharacterChain {
  const shape = shapes[role];
  const named = dress[setting][role];
  const name = displayName.trim() || named.label;
  const base = slug(name, "unit");

  const classIds = new Set(project.classes.map((entry) => entry.id));
  const unitTypeIds = new Set(project.unitTypes.map((entry) => entry.id));

  const { weaponType, weapon } = buildWeapon(
    project,
    role,
    setting,
    `${base}_weapon`,
    `${name}'s ${named.weaponName}`
  );
  const weaponTypeId = weapon.weaponTypeId;

  // The archetype this character belongs to, found before it is made. What
  // decides the match is the name a class of this role in this setting
  // carries, the same rule the weapon type above is shared by, plus the one
  // thing that would make joining it a mistake: a class edited to forbid the
  // weapon this character arrives holding is not this role's class any more.
  const identity = classIdentity(role, named);
  const joined = project.classes.find(
    (entry) =>
      entry.name === identity.name &&
      (entry.allowedWeaponTypeIds === undefined ||
        entry.allowedWeaponTypeIds.includes(weaponTypeId))
  );

  const unitClass: SourceClass | undefined = joined ? undefined : {
    id: unique(identity.id, classIds),
    name: identity.name,
    baseStats: {
      health: shape.health,
      movement: shape.movement,
      strength: shape.strength,
      defense: shape.defense,
      speed: shape.speed,
      actionPoints: shape.actionPoints
    },
    allowedWeaponTypeIds: [weaponTypeId],
    ...(shape.actsAfterAttacking ? { actsAfterAttacking: true } : {}),
    // Copied, never shared: what an author receives has to be theirs to edit,
    // and a nested object handed out by reference would be one the catalogue
    // could still be holding. A `traversal` is an ordinary class field the
    // moment it exists: renameable, deletable, and carrying no mark of where
    // it came from.
    ...(named.traversal ? { traversal: copyTraversal(named.traversal) } : {})
  };

  const unitType: SourceUnitType & { classId: string } = {
    id: unique(base, unitTypeIds),
    name,
    classId: joined ? joined.id : unitClass!.id,
    startingWeaponIds: [weapon.id]
  };

  return {
    ...(weaponType ? { weaponType } : {}),
    weapon,
    ...(unitClass ? { unitClass } : {}),
    unitType
  };
}
