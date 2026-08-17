// SPDX-License-Identifier: MIT
import type { SourceProject } from "../generated/source-v1";
import {
  CATALOGUE_SETTINGS,
  characterRecipes,
  type CatalogueSetting,
  type CharacterRole
} from "./character-recipe";
import { SIDE_FACTIONS } from "./character-standing";

/**
 * A whole character's worth of choices, made for an author who does not care
 * which ones they are.
 *
 * The wizard asks three questions and an author filling a board to try
 * something out has an answer to none of them: they want somebody to stand on a
 * tile. This makes every choice the wizard would have asked for, and the wizard
 * then presses its own last button, so a random character is built by exactly
 * the same `buildCharacterChain` as one somebody walked the steps for. There is
 * no second way to make a character here, which is the point.
 *
 * **Random but plausible**, which is the owner's phrase and rules out the
 * simple thing. Uniform noise over every field would offer a Sengoku name in a
 * medieval game and a class the drawing does not match. So: the setting is the
 * project's own, the name comes from that setting's own vocabulary, and the
 * role is drawn from the catalogue shelf rather than invented.
 *
 * The one thing it does *not* have to guard is the drawing. A character's
 * figure is chosen by the archetype word in its class id, and `classIdentity`
 * puts the role there itself when the catalogue's label does not carry it, so
 * every character the catalogue can build already draws as its own role
 * whatever it is called. `random-character.test.ts` holds that over all
 * fifty-six of them rather than trusting it.
 */
export interface RandomCharacter {
  readonly role: CharacterRole;
  readonly setting: CatalogueSetting;
  readonly name: string;
  readonly sideId: string;
}

/**
 * Given names, one list per catalogue setting, in that setting's own register.
 *
 * A name has to read as a person's rather than as a role's — "Outrider" is what
 * the class is called and nobody is called it — so these are not derived from
 * the catalogue and cannot be. They are plain ASCII on purpose: the consoles'
 * font is 0x20 to 0x5F, so a name with an accent in it would be drawn as spaces
 * on the machines this editor is for.
 */
const GIVEN_NAMES: Record<CatalogueSetting, readonly string[]> = {
  medieval: [
    "Wren", "Alder", "Bryn", "Cade", "Elowen", "Garrick", "Hale", "Isolde",
    "Merrick", "Rowan", "Sefton", "Thea"
  ],
  scifi: [
    "Vega", "Okonkwo", "Reyes", "Sato", "Kessler", "Ilves", "Mbeki", "Nadeau",
    "Petrov", "Quill", "Vance", "Zheng"
  ],
  mythical: [
    "Aeliana", "Bramblewick", "Caladwen", "Draven", "Emrys", "Faelin",
    "Gwyther", "Lysandra", "Orin", "Sylvaine", "Torvald", "Ysolde"
  ],
  nature: [
    "Bramble", "Clover", "Dunnock", "Fernhollow", "Hazel", "Juniper", "Mossfoot",
    "Nettle", "Pipit", "Sorrel", "Thistle", "Willow"
  ],
  sengoku: [
    "Akemi", "Daichi", "Hinata", "Kenshin", "Michiko", "Nobuo", "Rin", "Sakura",
    "Takeshi", "Umeko", "Yori", "Zenjiro"
  ],
  undead: [
    "Ashen", "Cinder", "Dolorous", "Ebbet", "Grimhold", "Hollis", "Lament",
    "Morrow", "Pallid", "Sorrow", "Vesper", "Wither"
  ],
  pirates: [
    "Bess", "Calico", "Drake", "Fentin", "Gully", "Hooke", "Jorun", "Kestrel",
    "Marlin", "Redd", "Salt", "Teague"
  ]
};

/**
 * Chooses one of `count` things. Injected so that a test can ask for a
 * particular character rather than for whatever came up, and so that the one
 * call to `Math.random` in this feature is in a place a test never reaches.
 */
export type Pick = (count: number) => number;

const defaultPick: Pick = (count) => Math.floor(Math.random() * count);

function chooseFrom<T>(items: readonly T[], pick: Pick): T {
  if (items.length === 0) {
    throw new Error("nothing to choose from");
  }
  const index = pick(items.length);
  // A `pick` that answers out of range would otherwise return undefined and
  // fail somewhere further away as a missing role or a blank name.
  const safe = Number.isInteger(index) && index >= 0 && index < items.length
    ? index
    : 0;
  return items[safe]!;
}

/**
 * The setting a random character is drawn from: the project's own style when
 * the catalogue has a shelf for it.
 *
 * The same choice the wizard makes for its opening shelf, and for the same
 * reason. A style decides how every character in this game is drawn, so a name
 * from another setting is a name that will not match its own picture — which is
 * the difference between random and random-but-plausible.
 */
export function settingFor(project: SourceProject): CatalogueSetting {
  const style = project.characterStyleId;
  return CATALOGUE_SETTINGS.some((entry) => entry.id === style)
    ? (style as CatalogueSetting)
    : CATALOGUE_SETTINGS[0]!.id;
}

/**
 * Every choice the wizard would have asked for, made.
 *
 * The name avoids the ones this project has already used where it can, because
 * two characters called Wren is the one outcome an author would read as a bug
 * rather than as a roll. It is a preference and not a rule: a project that has
 * used every name in the list gets a repeat, and the chain builder gives it a
 * distinct id anyway.
 */
export function randomCharacter(
  project: SourceProject,
  pick: Pick = defaultPick
): RandomCharacter {
  const setting = settingFor(project);
  const shelf = characterRecipes.filter((entry) => entry.setting === setting);
  const role = chooseFrom(shelf, pick).role;

  const taken = new Set(
    (project.unitTypes ?? []).map((entry) => entry.name.trim().toLowerCase())
  );
  const names = GIVEN_NAMES[setting];
  const free = names.filter((entry) => !taken.has(entry.toLowerCase()));
  const name = chooseFrom(free.length > 0 ? free : names, pick);

  const sideId = chooseFrom(SIDE_FACTIONS, pick).id;

  return { role, setting, name, sideId };
}
