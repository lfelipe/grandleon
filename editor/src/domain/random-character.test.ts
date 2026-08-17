// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import { archetypeForClass } from "./board-art";
import {
  CATALOGUE_SETTINGS,
  buildCharacterChain,
  characterRecipes,
  type CatalogueSetting
} from "./character-recipe";
import { SIDE_FACTIONS } from "./character-standing";
import { randomCharacter, settingFor, type Pick } from "./random-character";

function projectWith(
  style: string,
  names: readonly string[] = []
): SourceProject {
  return {
    schemaVersion: 1,
    id: "p",
    name: "P",
    characterStyleId: style,
    classes: [],
    unitTypes: names.map((name, index) => ({
      id: `u${index}`,
      name,
      classId: "c"
    })),
    weapons: [],
    weaponTypes: [],
    maps: [],
    campaigns: [],
    dialogues: []
  } as unknown as SourceProject;
}

/** Always the first thing offered, so a test can name what it expects back. */
const first: Pick = () => 0;
/** Always the last, which is also the out-of-range guard's near neighbour. */
const last: Pick = (count) => count - 1;

describe("the setting a random character is drawn from", () => {
  it("is the project's own style when the catalogue has a shelf for it", () => {
    for (const entry of CATALOGUE_SETTINGS) {
      expect(settingFor(projectWith(entry.id))).toBe(entry.id);
    }
  });

  it("falls back to the first shelf for a style the catalogue has none of", () => {
    expect(settingFor(projectWith("no_such_style")))
      .toBe(CATALOGUE_SETTINGS[0]!.id);
  });

  // The plausibility rule, and the one an author would actually notice: a
  // Sengoku name on a character drawn in medieval armour reads as a bug.
  it("never names somebody out of another setting's vocabulary", () => {
    for (const entry of CATALOGUE_SETTINGS) {
      const project = projectWith(entry.id);
      for (let index = 0; index < 12; index += 1) {
        const chosen = randomCharacter(project, () => index);
        expect(chosen.setting).toBe(entry.id);
        // The name has to belong to this setting and to no other, which is
        // what makes the previous line worth anything.
        const elsewhere = CATALOGUE_SETTINGS.filter(
          (other) => other.id !== entry.id
        ).some((other) =>
          randomNamesOf(other.id).includes(chosen.name)
        );
        expect(elsewhere).toBe(false);
      }
    }
  });
});

/** Every name a setting can produce, gathered by asking for each in turn. */
function randomNamesOf(setting: CatalogueSetting): string[] {
  const project = projectWith(setting);
  const names = new Set<string>();
  for (let index = 0; index < 64; index += 1) {
    names.add(randomCharacter(project, () => index).name);
  }
  return [...names];
}

describe("what a random character is", () => {
  it("is a role the catalogue offers and a side the game has", () => {
    const project = projectWith("medieval");
    for (let index = 0; index < 16; index += 1) {
      const chosen = randomCharacter(project, () => index);
      expect(
        characterRecipes.some(
          (entry) => entry.role === chosen.role && entry.setting === "medieval"
        )
      ).toBe(true);
      // One of the two the game already has, never "neither" and never
      // something invented here.
      expect(SIDE_FACTIONS.map((side) => side.id)).toContain(chosen.sideId);
      expect(chosen.name.length).toBeGreaterThan(0);
    }
  });

  // The consoles' font is ASCII 0x20 to 0x5F. A name outside it is drawn as
  // spaces on the machines this editor exists to make games for, which is a
  // thing no one would see until the ROM was in their hands.
  it("names nobody the console cannot draw", () => {
    for (const entry of CATALOGUE_SETTINGS) {
      for (const name of randomNamesOf(entry.id)) {
        for (const character of name.toUpperCase()) {
          const code = character.charCodeAt(0);
          expect(code >= 0x20 && code <= 0x5f).toBe(true);
        }
      }
    }
  });

  it("prefers a name this project has not used", () => {
    const plain = randomCharacter(projectWith("medieval"), first);
    // With that name taken, the same choice has to land somewhere else.
    const avoided = randomCharacter(
      projectWith("medieval", [plain.name]),
      first
    );
    expect(avoided.name).not.toBe(plain.name);
  });

  it("still names somebody when every name is taken", () => {
    const all = randomNamesOf("medieval");
    const chosen = randomCharacter(projectWith("medieval", all), first);
    expect(all).toContain(chosen.name);
  });

  // A `pick` that answers out of range would otherwise come back as an
  // undefined role and a blank name, and fail somewhere with no clue in it.
  it("holds together when asked for something out of range", () => {
    const project = projectWith("medieval");
    for (const pick of [() => -1, () => 9999, () => 1.5, () => Number.NaN]) {
      const chosen = randomCharacter(project, pick);
      expect(chosen.name.length).toBeGreaterThan(0);
      expect(SIDE_FACTIONS.map((side) => side.id)).toContain(chosen.sideId);
    }
    expect(randomCharacter(project, last).name.length).toBeGreaterThan(0);
  });
});

// ---------------------------------------------------------------------------

// The trap this feature was warned about, held rather than avoided.
//
// A character's figure is chosen by the archetype word in its class id, and
// `archetypeForClass` takes the *first* archetype in the roster's order that
// appears anywhere in it — not the first word of the id. So a role whose
// catalogue label happened to contain another role's word would draw as that
// other role, and a random character would quietly be the wrong picture.
//
// It cannot happen, because `classIdentity` puts the role in the id itself when
// the label does not carry it. That is a property of all fifty-six catalogue
// entries at once, so it is asked of all fifty-six rather than argued about.
describe("every character the catalogue can build", () => {
  it("draws as its own role, whatever it is called", () => {
    const wrong: string[] = [];
    for (const recipe of characterRecipes) {
      const chain = buildCharacterChain(
        projectWith(recipe.setting),
        recipe.role,
        "Anonymous",
        recipe.setting
      );
      const classId = chain.unitClass?.id ?? chain.unitType.classId;
      if (archetypeForClass(classId) !== recipe.role) {
        wrong.push(`${recipe.id}: ${classId} draws as ${archetypeForClass(classId)}`);
      }
    }
    expect(wrong).toEqual([]);
  });

  // And the character's own name is not what decides it, which is the half of
  // the warning that would otherwise constrain what a random name may be.
  it("draws the same whatever the character is named", () => {
    for (const name of ["Bandit", "Knight", "Mage", "Wren", "", "Beast"]) {
      const chain = buildCharacterChain(
        projectWith("medieval"),
        "rogue",
        name,
        "medieval"
      );
      const classId = chain.unitClass?.id ?? chain.unitType.classId;
      expect(archetypeForClass(classId)).toBe("rogue");
    }
  });
});
