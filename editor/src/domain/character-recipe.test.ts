// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import {
  ARCHETYPES,
  CHARACTER_STYLE_IDS,
  archetypeForClass
} from "./board-art";
import {
  ABILITY_CASTS,
  CATALOGUE_SETTINGS,
  CHARACTER_ROLES,
  TRAVERSING_ENTRY,
  abilityRecipes,
  buildAbility,
  buildCharacterChain,
  buildWeaponPair,
  characterRecipes,
  weaponRecipes
} from "./character-recipe";
import { areaOffsets, areaRadius } from "./targeting-geometry";

function project(): SourceProject {
  return {
    schemaVersion: "1.2.0",
    packageId: "8a1f4c2e-9b3d-4f61-a8c2-5d0e7b419af3",
    gameId: "recipe.fixture",
    title: "Recipe fixture",
    contentRevision: "0.1.0",
    classes: [],
    unitTypes: [],
    weapons: [],
    items: [],
    maps: []
  };
}

describe("character recipes", () => {
  it("offers roles named in words and described by their reach alone", () => {
    expect(characterRecipes.map((recipe) => recipe.role)).toContain("archer");
    for (const recipe of characterRecipes) {
      expect(recipe.label.length).toBeGreaterThan(0);
      expect(recipe.summary.length).toBeGreaterThan(0);
    }
  });

  it("offers every role the art can draw, and no role it cannot", () => {
    // A role the art does not hold would insert as somebody else's picture.
    expect([...CHARACTER_ROLES]).toEqual([...ARCHETYPES]);
  });

  it("offers all eight roles in every setting", () => {
    expect(characterRecipes).toHaveLength(
      CHARACTER_ROLES.length * CATALOGUE_SETTINGS.length
    );
    for (const setting of CATALOGUE_SETTINGS) {
      const offered = characterRecipes
        .filter((recipe) => recipe.setting === setting.id)
        .map((recipe) => recipe.role);
      expect(offered).toEqual([...CHARACTER_ROLES]);
    }
    // Every entry is separately addressable, so a browse surface can key on it.
    expect(new Set(characterRecipes.map((recipe) => recipe.id)).size)
      .toBe(characterRecipes.length);
  });

  it("gives each setting its own words for the same numbers", () => {
    const medieval = characterRecipes.find((recipe) => recipe.id === "medieval_knight")!;
    const scifi = characterRecipes.find((recipe) => recipe.id === "scifi_knight")!;
    expect(medieval.label).toBe("Knight");
    expect(scifi.label).toBe("Trooper");
    expect(scifi.summary).toBe(medieval.summary);

    const knight = buildCharacterChain(project(), "knight", "Rue");
    const trooper = buildCharacterChain(project(), "knight", "Rue", "scifi");
    expect(trooper.weaponType?.name).toBe("Sidearm");
    expect(knight.weaponType?.name).toBe("Blade");
    expect(trooper.unitClass!.baseStats).toEqual(knight.unitClass!.baseStats);
    expect(trooper.weapon.power).toBe(knight.weapon.power);
    expect(trooper.weapon.minimumRange).toBe(knight.weapon.minimumRange);
    expect(trooper.weapon.maximumRange).toBe(knight.weapon.maximumRange);
  });

  it("offers a shelf for every style the art can dress a roster in", () => {
    // The catalogue's settings and the art's styles are one menu in one order.
    // A setting the art cannot dress would offer eight cards drawn as somebody
    // else's genre; a style no setting names would be art an author can choose
    // and never find units for.
    expect(CATALOGUE_SETTINGS.map((setting) => setting.id))
      .toEqual([...CHARACTER_STYLE_IDS]);
  });

  it("names the same numbers once for every setting", () => {
    const knights = CATALOGUE_SETTINGS.map(
      (setting) =>
        characterRecipes.find(
          (recipe) => recipe.id === `${setting.id}_knight`
        )!
    );
    expect(knights.map((recipe) => recipe.label)).toEqual([
      "Knight",
      "Trooper",
      "Drakeguard",
      "Badger guard",
      "Samurai",
      "Barrow knight",
      "Boarder"
    ]);
    // Every label is somebody's own word, and every summary is the same
    // sentence, because the numbers behind them are the same numbers.
    expect(new Set(knights.map((recipe) => recipe.label)).size)
      .toBe(CATALOGUE_SETTINGS.length);
    for (const knight of knights) {
      expect(knight.summary).toBe(knights[0]!.summary);
    }

    const bear = characterRecipes.find((r) => r.id === "nature_mage")!;
    const cat = characterRecipes.find((r) => r.id === "nature_archer")!;
    expect(bear.label).toBe("Mage bear");
    expect(cat.label).toBe("Archer cat");
    const mage = buildCharacterChain(project(), "mage", "Bram", "nature");
    const staff = buildCharacterChain(project(), "mage", "Bram");
    expect(mage.unitClass!.baseStats).toEqual(staff.unitClass!.baseStats);
    expect(mage.weaponType?.name).toBe("Branch");

    // The Warring States shelf, and the reason its words are romanised
    // without macrons: a label is slugged into the identifiers an author
    // lives with, and `slug()` keeps only [a-z0-9].
    const onmyoji = characterRecipes.find((r) => r.id === "sengoku_mage")!;
    expect(onmyoji.label).toBe("Onmyoji");
    const diviner = buildCharacterChain(project(), "mage", "", "sengoku");
    expect(diviner.unitType.id).toBe("onmyoji");
    expect(diviner.unitClass!.baseStats).toEqual(staff.unitClass!.baseStats);
    expect(diviner.weaponType?.name).toBe("Ofuda");
  });

  it("dresses a setting as the force a player fights, and nothing else", () => {
    // The first setting drawn as an opposing force. Everything that makes it
    // one is a picture and a name; nothing about it is a side, and nothing
    // about it is a number.
    const wraith = characterRecipes.find((r) => r.id === "undead_mage")!;
    const staffMage = characterRecipes.find((r) => r.id === "medieval_mage")!;
    expect(wraith.label).toBe("Wraith");
    expect(wraith.summary).toBe(staffMage.summary);

    // Drawn with no legs and a clear gap over its own ground, and it walks
    // anyway. This is the entry that would have tempted a second `traversal`.
    const raised = buildCharacterChain(project(), "mage", "Bram", "undead");
    const hired = buildCharacterChain(project(), "mage", "Bram");
    expect(raised.unitClass!.traversal).toBeUndefined();
    expect(raised.unitClass!.baseStats).toEqual(hired.unitClass!.baseStats);
    expect(raised.weaponType?.name).toBe("Dirge");

    // All eight roles are offered, in the roster's own order, so an author
    // fielding this setting has the same shelf as an author fielding any
    // other.
    const roster = characterRecipes.filter((r) => r.setting === "undead");
    expect(roster.map((recipe) => recipe.role)).toEqual([...CHARACTER_ROLES]);
    expect(roster.map((recipe) => recipe.label)).toEqual([
      "Barrow knight",
      "Bonepicker",
      "Wraith",
      "Bellringer",
      "Mourner",
      "Barrow lord",
      "Grave-thief",
      "Bone hound"
    ]);
  });

  it("lets exactly one entry cross ground its role does not", () => {
    // The one hole in "numbers belong to the role": a dragon flies, because a
    // dragon that walks is not a dragon. It is a movement capability and
    // nothing else, the dragon's health, movement, reach and power being the
    // wolf's and the boar's, and it is the only one, so a later commission
    // cannot add a second by habit.
    const dragon = characterRecipes.find((r) => r.id === TRAVERSING_ENTRY)!;
    expect(dragon.label).toBe("Dragon");
    expect(dragon.summary).toContain("Flies.");

    const flying = buildCharacterChain(project(), "beast", "Ash", "mythical");
    const walking = buildCharacterChain(project(), "beast", "Ash");
    expect(flying.unitClass!.traversal).toEqual({ flying: true });
    expect(walking.unitClass!.traversal).toBeUndefined();
    expect(flying.unitClass!.baseStats).toEqual(walking.unitClass!.baseStats);
    expect(flying.weapon.power).toBe(walking.weapon.power);

    // The pirate Parrot is the entry this fence was most likely to lose: a bird
    // that walks looks like an oversight rather than a rule. It walks.
    const parrot = characterRecipes.find((r) => r.id === "pirates_beast")!;
    const wolf = characterRecipes.find((r) => r.id === "medieval_beast")!;
    expect(parrot.label).toBe("Parrot");
    expect(parrot.summary).toBe(wolf.summary);

    // No second flier anywhere, and every other summary is silent about ground.
    const crossing = characterRecipes.filter((recipe) =>
      recipe.summary.includes("Flies")
    );
    expect(crossing.map((recipe) => recipe.id)).toEqual([TRAVERSING_ENTRY]);
    for (const recipe of characterRecipes) {
      const chain = buildCharacterChain(
        project(),
        recipe.role,
        "Probe",
        recipe.setting
      );
      expect(chain.unitClass!.traversal === undefined)
        .toBe(recipe.id !== TRAVERSING_ENTRY);
    }

    // A shelf added later cannot cut a second hole. An ability record has no
    // field a movement capability could hide in, and this asserts it stayed
    // that way rather than trusting that it did.
    for (const recipe of abilityRecipes) {
      const written = JSON.stringify(
        buildAbility(project(), recipe.cast, "Probe", recipe.setting)
      );
      expect(written).not.toContain("traversal");
      expect(written).not.toContain("flying");
    }
  });

  it("hands out a traversal the author owns rather than one it still holds", () => {
    // Copy-on-use has to reach inside a nested object too: two dragons that
    // shared one `traversal` would be two records an edit to either changed.
    const first = buildCharacterChain(project(), "beast", "Ash", "mythical");
    const second = buildCharacterChain(project(), "beast", "Ember", "mythical");
    expect(first.unitClass!.traversal).not.toBe(second.unitClass!.traversal);
    first.unitClass!.traversal!.flying = false;
    expect(second.unitClass!.traversal).toEqual({ flying: true });
    const third = buildCharacterChain(project(), "beast", "Rue", "mythical");
    expect(third.unitClass!.traversal).toEqual({ flying: true });
  });

  it("describes what an entry does, never how good it is", () => {
    // The repository has no balance harness, so the catalogue does not make
    // claims one would be needed to check. Every shelf is held to it, so
    // adding one cannot be the way the claim sneaks back in.
    const evaluative =
      /\b(strong|strongest|weak|weakest|best|worst|better|worse|powerful|overpowered|balanced)\b/iu;
    // The ability shelf is the hardest case for this rule and therefore the
    // reason to keep one guard rather than three: an ability's whole interest
    // is a trade, and "wider but weaker" is the sentence it invites. The shelf
    // states the tiles and the power and lets the author judge.
    const shelved = [...characterRecipes, ...weaponRecipes, ...abilityRecipes];
    expect(shelved).toHaveLength(
      characterRecipes.length + weaponRecipes.length + abilityRecipes.length
    );
    for (const recipe of shelved) {
      expect(recipe.summary).not.toMatch(evaluative);
      expect(recipe.label).not.toMatch(evaluative);
    }
  });

  it("leaves no trace of the catalogue in what it builds", () => {
    // Copy-on-use: an author owns the records, so nothing may carry a setting,
    // a catalogue entry identifier, or anything else to migrate later. Both
    // doors into the catalogue are held to it.
    const built = [
      ...characterRecipes.map((recipe) => ({
        recipe,
        records: buildCharacterChain(
          project(),
          recipe.role,
          "Probe",
          recipe.setting
        )
      })),
      ...weaponRecipes.map((recipe) => ({
        recipe,
        records: buildWeaponPair(project(), recipe.role, "Probe", recipe.setting)
      })),
      ...abilityRecipes.map((recipe) => ({
        recipe,
        records: buildAbility(project(), recipe.cast, "Probe", recipe.setting)
      }))
    ];
    for (const { recipe, records } of built) {
      const written = JSON.stringify(records);
      expect(written).not.toContain(recipe.id);
      expect(written).not.toContain(recipe.setting);
      expect(written).not.toContain("catalogue");
      expect(written).not.toContain("recipe");
    }
  });

  it("draws the character the author picked, not a knight by default", () => {
    // Every client resolves the drawing from the class name by keyword, so the
    // class a character joins has to say somewhere that it is an archer. The
    // identifier is built from the setting's own word for the role, "Samurai",
    // "Bonepicker" or "Storm stag", so every setting is checked and not just
    // the one whose words happen to be the archetype names.
    // What has to be unique is the key a class is found by, which is its name
    // together with the weapon type it permits, not the name alone. Two
    // settings really do call the commander a Captain (see the roster table in
    // tools/placeholder_art/README.md), and that is the catalogue's word rather than
    // this module's; what keeps them two classes is that a sidearm is not a
    // cutlass. A collision in the pair would silently merge two archetypes.
    const found = new Set<string>();
    for (const setting of CATALOGUE_SETTINGS) {
      for (const role of CHARACTER_ROLES) {
        const chain = buildCharacterChain(project(), role, "Wren", setting.id);
        expect(archetypeForClass(chain.unitClass!.id)).toBe(role);
        found.add(`${chain.unitClass!.name}|${chain.weapon.weaponTypeId}`);
      }
    }
    expect(found.size).toBe(CATALOGUE_SETTINGS.length * CHARACTER_ROLES.length);
  });

  it("keeps two settings' captains two classes, not one", () => {
    // The one label the catalogue uses twice, and the case that proves the
    // finding rule is the pair rather than the name: a sci-fi Captain carries a
    // sidearm and a pirate Captain a cutlass, so the second does not join the
    // first and end up unable to draw the weapon it arrived with.
    const source = project();
    const officer = buildCharacterChain(source, "commander", "Vane", "scifi");
    source.weaponTypes = [officer.weaponType!];
    source.classes = [officer.unitClass!];
    const pirate = buildCharacterChain(source, "commander", "Roake", "pirates");
    expect(pirate.unitClass).toBeDefined();
    expect(pirate.unitClass!.id).not.toBe(officer.unitClass!.id);
    expect(pirate.unitClass!.allowedWeaponTypeIds)
      .toEqual([pirate.weapon.weaponTypeId]);
  });

  it("states each role's reach, and nothing else about its numbers", () => {
    // A role card is where a *kind* is chosen, so it carries the one fact that
    // changes that choice. Speed, action count and whether striking ends the
    // turn are a character's numbers and are read where numbers are read; a
    // card claiming them is a card an author has to wade through.
    for (const recipe of characterRecipes) {
      const chain = buildCharacterChain(project(), recipe.role, "Probe");
      const weapon = chain.weapon;
      expect(recipe.summary).toContain(
        weapon.minimumRange === weapon.maximumRange
          ? `Range ${weapon.maximumRange} tile.`
          : `Range ${weapon.minimumRange}–${weapon.maximumRange} tiles.`
      );
      // The reach band, and, for the one entry that flies, how it crosses
      // ground. Anchored at both ends, so a stat added back to a card is a
      // failing test rather than a paragraph nobody notices growing.
      expect(recipe.summary).toMatch(/^Range \d+(–\d+)? tiles?\.( Flies\.)?$/u);
    }
    const archer = characterRecipes.find((r) => r.id === "medieval_archer")!;
    expect(archer.summary).toBe("Range 2–3 tiles.");
  });

  it("builds the whole chain from one choice", () => {
    const chain = buildCharacterChain(project(), "archer", "Wren");
    expect(chain.weaponType?.name).toBe("Bow");
    expect(chain.weapon.weaponTypeId).toBe(chain.weaponType!.id);
    expect(chain.unitClass!.allowedWeaponTypeIds).toEqual([chain.weaponType!.id]);
    expect(chain.unitType.classId).toBe(chain.unitClass!.id);
    expect(chain.unitType.startingWeaponIds).toEqual([chain.weapon.id]);
    expect(chain.unitType.name).toBe("Wren");
  });

  it("gives an archer a reach band it cannot use up close", () => {
    const chain = buildCharacterChain(project(), "archer", "Wren");
    expect(chain.weapon.minimumRange).toBe(2);
    expect(chain.weapon.maximumRange).toBe(3);
    // A knight, by contrast, only reaches the next tile.
    const knight = buildCharacterChain(project(), "knight", "Rue");
    expect(knight.weapon.minimumRange).toBe(1);
    expect(knight.weapon.maximumRange).toBe(1);
  });

  it("reuses a weapon type rather than leaving duplicates behind", () => {
    const source = project();
    const first = buildCharacterChain(source, "archer", "Wren");
    source.weaponTypes = [first.weaponType!];
    source.weapons = [first.weapon];
    source.classes = [first.unitClass!];
    source.unitTypes = [first.unitType];

    const second = buildCharacterChain(source, "archer", "Fen");
    // The second archer shares the bow type and joins the archer class that is
    // already there. Only the weapon and the unit type are its own, and those
    // are where two archers are free to diverge.
    expect(second.weaponType).toBeUndefined();
    expect(second.weapon.weaponTypeId).toBe(first.weaponType!.id);
    expect(second.weapon.id).not.toBe(first.weapon.id);
    expect(second.unitClass).toBeUndefined();
    expect(second.unitType.classId).toBe(first.unitClass!.id);
  });

  it("makes one class per archetype rather than one per character", () => {
    // A class is the archetype and the unit type is the character. Naming and
    // identifying the class after the character makes ten healers into ten
    // identical classes, which is ten copies of one decision: every later
    // change to what a healer is becomes ten changes, and nine of them get
    // forgotten.
    const source = project();
    const healers = ["Bram", "Wren", "Fen", "Rue", "Pip"];
    const built = healers.map((name) => {
      const chain = buildCharacterChain(source, "healer", name);
      if (chain.weaponType) source.weaponTypes = [
        ...(source.weaponTypes ?? []),
        chain.weaponType
      ];
      if (chain.unitClass) source.classes = [...source.classes, chain.unitClass];
      source.weapons = [...source.weapons, chain.weapon];
      source.unitTypes = [...source.unitTypes, chain.unitType];
      return chain;
    });

    expect(source.classes).toHaveLength(1);
    expect(source.unitTypes).toHaveLength(5);
    // The one class is named for the role, not for whoever was made first.
    expect(source.classes[0]!.name).toBe("Healer class");
    expect(source.classes[0]!.id).toBe("healer_class");
    for (const [index, chain] of built.entries()) {
      expect(chain.unitType.classId).toBe(source.classes[0]!.id);
      // Only the first press wrote a class; the rest joined it.
      expect(chain.unitClass === undefined).toBe(index > 0);
      // And each of them is still their own character, with their own weapon.
      expect(chain.unitType.name).toBe(healers[index]);
    }
    expect(new Set(source.weapons.map((entry) => entry.id)).size).toBe(5);
  });

  it("keeps a class per archetype per setting, and never across them", () => {
    // A setting changes what the role is called and what it carries, so the
    // Wyrmpriest and the Healer are two archetypes and get two classes. Two
    // characters of the same role in the same setting are one archetype.
    const source = project();
    const healer = buildCharacterChain(source, "healer", "Bram");
    source.weaponTypes = [healer.weaponType!];
    source.classes = [healer.unitClass!];
    const priest = buildCharacterChain(source, "healer", "Wren", "mythical");
    expect(priest.unitClass).toBeDefined();
    expect(priest.unitClass!.name).toBe("Wyrmpriest class");
    expect(priest.unitClass!.id).not.toBe(healer.unitClass!.id);

    source.weaponTypes = [...source.weaponTypes, priest.weaponType!];
    source.classes = [...source.classes, priest.unitClass!];
    const secondPriest = buildCharacterChain(source, "healer", "Fen", "mythical");
    expect(secondPriest.unitClass).toBeUndefined();
    expect(secondPriest.unitType.classId).toBe(priest.unitClass!.id);
  });

  it("will not join a class edited to forbid the weapon it hands over", () => {
    // Reuse is only right while the class is still this role's class. One an
    // author has edited to forbid the weapon this character arrives holding is
    // a different thing under the same name, and joining it would hand
    // somebody a weapon they cannot draw.
    const source = project();
    const first = buildCharacterChain(source, "archer", "Wren");
    source.weaponTypes = [first.weaponType!];
    source.classes = [{ ...first.unitClass!, allowedWeaponTypeIds: [] }];
    source.unitTypes = [first.unitType];

    const second = buildCharacterChain(source, "archer", "Fen");
    expect(second.unitClass).toBeDefined();
    expect(second.unitClass!.id).not.toBe(first.unitClass!.id);
    expect(second.unitClass!.allowedWeaponTypeIds)
      .toEqual([second.weapon.weaponTypeId]);
  });

  it("names a class for its archetype, so renaming a character renames one", () => {
    // A class carries no character's name. A character called Wren in a class
    // called "Wren class" is one archetype nobody else can join without the
    // name reading as a mistake, and renaming Wren would then look as though
    // it should rename a record other characters share.
    const source = project();
    const chain = buildCharacterChain(source, "healer", "Wren");
    expect(chain.unitType.name).toBe("Wren");
    expect(chain.unitClass!.name).not.toContain("Wren");
    expect(chain.unitClass!.id).not.toContain("wren");
  });

  it("does not collide when the same name is used twice", () => {
    const source = project();
    const first = buildCharacterChain(source, "knight", "Rue");
    source.weaponTypes = [first.weaponType!];
    source.weapons = [first.weapon];
    source.classes = [first.unitClass!];
    source.unitTypes = [first.unitType];

    const second = buildCharacterChain(source, "knight", "Rue");
    expect(second.unitType.id).not.toBe(first.unitType.id);
    expect(second.weapon.id).not.toBe(first.weapon.id);
    // Two characters of one name are two unit types and two weapons, and one
    // knight class, which is what a class is.
    expect(second.unitClass).toBeUndefined();
    expect(second.unitType.classId).toBe(first.unitClass!.id);
  });

  it("produces schema-legal identifiers from awkward names", () => {
    const pattern = /^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$/u;
    for (const name of ["Sir Reginald III", "  ", "42", "Zoë!!"]) {
      const chain = buildCharacterChain(project(), "mage", name);
      expect(chain.unitType.id).toMatch(pattern);
      expect(chain.unitClass!.id).toMatch(pattern);
      expect(chain.weapon.id).toMatch(pattern);
    }
  });

  it("offers every weapon its characters carry, in every setting", () => {
    expect(weaponRecipes).toHaveLength(
      CHARACTER_ROLES.length * CATALOGUE_SETTINGS.length
    );
    expect(new Set(weaponRecipes.map((recipe) => recipe.id)).size)
      .toBe(weaponRecipes.length);
    // One armoury, not two: every weapon a character arrives holding is a
    // weapon an author can add on its own, under the same name.
    for (const character of characterRecipes) {
      const weapon = weaponRecipes.find(
        (candidate) =>
          candidate.setting === character.setting &&
          candidate.role === character.role
      )!;
      expect(weapon.label).toBe(character.weaponName);
      expect(weapon.weaponTypeName).toBe(character.weaponTypeName);
      expect(weapon.carriedBy).toBe(character.label);
    }
  });

  it("states each weapon's reach and power from the record it will get", () => {
    // Derived, not written alongside: a summary cannot drift from its weapon.
    for (const recipe of weaponRecipes) {
      const { weapon } = buildWeaponPair(
        project(),
        recipe.role,
        "",
        recipe.setting
      );
      expect(recipe.summary).toContain(`Power ${weapon.power}.`);
      // The floor of the band is the fact an author needs before handing a
      // weapon to somebody who will stand next to an enemy, so both numbers are
      // shown whenever they differ.
      expect(recipe.summary).toContain(
        weapon.minimumRange === weapon.maximumRange
          ? `Range ${weapon.maximumRange} tile${weapon.maximumRange === 1 ? "" : "s"}.`
          : `Range ${weapon.minimumRange}–${weapon.maximumRange} tiles.`
      );
    }
    const bow = weaponRecipes.find((recipe) => recipe.id === "medieval_archer_weapon")!;
    expect(bow.label).toBe("Longbow");
    expect(bow.summary).toBe("Range 2–3 tiles. Power 3.");
  });

  it("gives each setting its own weapons for the same numbers", () => {
    const longbow = weaponRecipes.find((r) => r.id === "medieval_archer_weapon")!;
    const rifle = weaponRecipes.find((r) => r.id === "scifi_archer_weapon")!;
    expect(rifle.label).toBe("Rail Rifle");
    expect(rifle.weaponTypeName).toBe("Rifle");
    expect(rifle.summary).toBe(longbow.summary);
  });

  it("adds a weapon with the weapon type that makes it usable", () => {
    const source = project();
    const { weaponType, weapon } = buildWeaponPair(source, "archer", "");
    expect(weaponType?.name).toBe("Bow");
    expect(weapon.name).toBe("Longbow");
    expect(weapon.weaponTypeId).toBe(weaponType!.id);
    expect(weapon.power).toBe(3);
    expect(weapon.minimumRange).toBe(2);
    expect(weapon.maximumRange).toBe(3);
  });

  it("hands a loose weapon to a character the catalogue already made", () => {
    // The point of sharing the weapon type: an archer made from the catalogue
    // permits "Bow", so a bow added separately is a bow that archer can hold
    // without the author working out why it could not.
    const source = project();
    const archer = buildCharacterChain(source, "archer", "Wren");
    source.weaponTypes = [archer.weaponType!];
    source.weapons = [archer.weapon];
    source.classes = [archer.unitClass!];
    source.unitTypes = [archer.unitType];

    const spare = buildWeaponPair(source, "archer", "Spare Bow");
    expect(spare.weaponType).toBeUndefined();
    expect(spare.weapon.weaponTypeId).toBe(archer.weaponType!.id);
    expect(archer.unitClass!.allowedWeaponTypeIds)
      .toContain(spare.weapon.weaponTypeId);
    expect(spare.weapon.id).not.toBe(archer.weapon.id);
  });

  it("shares one weapon type between weapons that name the same one", () => {
    const source = project();
    const sword = buildWeaponPair(source, "knight", "");
    source.weaponTypes = [sword.weaponType!];
    source.weapons = [sword.weapon];

    // A dagger is a Blade too, so the shelf must not mint a second one.
    const dagger = buildWeaponPair(source, "rogue", "");
    expect(dagger.weaponType).toBeUndefined();
    expect(dagger.weapon.weaponTypeId).toBe(sword.weaponType!.id);
    expect(dagger.weapon.power).toBe(3);
  });

  it("does not collide when the same weapon is added twice", () => {
    const source = project();
    const first = buildWeaponPair(source, "archer", "");
    source.weaponTypes = [first.weaponType!];
    source.weapons = [first.weapon];

    const second = buildWeaponPair(source, "archer", "");
    expect(second.weapon.id).not.toBe(first.weapon.id);
    expect(second.weapon.name).toBe(first.weapon.name);
  });

  it("produces schema-legal weapon identifiers from awkward names", () => {
    const pattern = /^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$/u;
    for (const name of ["Officer's Blade", "  ", "42", "Zoë's!!"]) {
      const { weapon } = buildWeaponPair(project(), "commander", name);
      expect(weapon.id).toMatch(pattern);
    }
  });

  it("offers every cast in every setting, with a name of that setting's own", () => {
    expect(abilityRecipes).toHaveLength(
      ABILITY_CASTS.length * CATALOGUE_SETTINGS.length
    );
    expect(new Set(abilityRecipes.map((recipe) => recipe.id)).size)
      .toBe(abilityRecipes.length);
    for (const cast of ABILITY_CASTS) {
      const named = abilityRecipes.filter((recipe) => recipe.cast === cast);
      expect(named).toHaveLength(CATALOGUE_SETTINGS.length);
      // Every setting has its own word for it, and they are all different
      // words: a shelf that repeated one neutral name in seven places would be
      // one list pretending to be seven.
      expect(new Set(named.map((recipe) => recipe.label)).size)
        .toBe(CATALOGUE_SETTINGS.length);
      // And the numbers are the cast's, so a setting is never a rules input.
      expect(new Set(named.map((recipe) => recipe.summary)).size).toBe(1);
    }
  });

  it("demonstrates each axis the ability vocabulary has", () => {
    // The shelf is chosen to show the axes rather than to fill a grid, so the
    // property worth pinning is coverage of the vocabulary, not a count.
    const built = ABILITY_CASTS.map((cast) => buildAbility(project(), cast, ""));
    expect(new Set(built.map((ability) => ability.kind)))
      .toEqual(new Set(["damage", "restore"]));
    expect(
      new Set(
        built
          .filter((ability) => ability.kind === "damage")
          .map((ability) => ability.damageType)
      )
    ).toEqual(new Set(["physical", "magical"]));
    expect(new Set(built.map((ability) => ability.areaShape)))
      .toEqual(new Set(["single", "cross", "diamond"]));
    // A physical area and a restoring area both exist, so neither the damage
    // type nor the kind reads as tied to the shape.
    expect(
      built.some(
        (ability) =>
          ability.damageType === "physical" && ability.areaShape !== "single"
      )
    ).toBe(true);
    expect(
      built.some(
        (ability) =>
          ability.kind === "restore" && ability.areaShape !== "single"
      )
    ).toBe(true);
    // A band with a floor, which is the reach fact an author most needs.
    expect(built.some((ability) => ability.minimumRange > 1)).toBe(true);
    // Accuracy is spent once. It is the only field that makes a cast a gamble,
    // and a shelf where everything gambled would say nothing about the choice.
    const rolling = built.filter((ability) => ability.accuracy !== undefined);
    expect(rolling).toHaveLength(1);
    expect(rolling[0]!.accuracy).toBeLessThan(100);
    // Never on a restoring cast: the schema says a restore rolls nothing, so an
    // accuracy there would be a number that does nothing.
    for (const ability of built) {
      if (ability.kind === "restore") {
        expect(ability.accuracy).toBeUndefined();
        expect(ability.damageType).toBeUndefined();
      }
    }
  });

  it("offers one shape family under three names, and never one twice", () => {
    // `covered_by()` in the engine is the Manhattan ball at radius 0 for
    // `single`, 1 for `cross` and the authored radius for `diamond`. A diamond
    // of radius 1 is therefore a cross spelled differently, and offering both
    // would sell one shape twice.
    for (const cast of ABILITY_CASTS) {
      const ability = buildAbility(project(), cast, "");
      if (ability.areaShape !== "diamond") {
        expect(ability.radius).toBeUndefined();
        continue;
      }
      expect(ability.radius).toBeGreaterThan(1);
    }
  });

  it("states each cast from the record it will get, tiles counted not claimed", () => {
    for (const recipe of abilityRecipes) {
      const ability = buildAbility(project(), recipe.cast, "", recipe.setting);
      expect(recipe.label).toBe(ability.name);
      expect(recipe.summary).toContain(`Power ${ability.power}.`);

      // Which defence answers, taken from the record rather than from the name.
      if (ability.kind === "restore") {
        expect(recipe.summary).toContain("Restores health.");
      } else {
        expect(recipe.summary).toContain(
          ability.damageType === "magical"
            ? "against resistance"
            : "against defence"
        );
      }

      // The covered tiles are the geometry module's count, the module
      // `targeting-agreement.test.ts` pins against the running engine, so the
      // sentence cannot promise a shape the board would not draw.
      const radius = areaRadius(ability.areaShape ?? "single", ability.radius);
      if (radius === 0) {
        // A cast that catches only what it was aimed at is what most of the
        // shelf does, so it says nothing rather than saying the default.
        expect(recipe.summary).not.toContain("Covers");
      } else {
        const covered = areaOffsets(
          ability.areaShape ?? "single",
          ability.radius,
          radius
        ).length;
        expect(recipe.summary).toContain(`Covers ${covered} tiles.`);
      }

      // The reach band, both numbers whenever they differ.
      expect(recipe.summary).toContain(
        ability.minimumRange === ability.maximumRange
          ? `Range ${ability.maximumRange} tile${ability.maximumRange === 1 ? "" : "s"}.`
          : `Range ${ability.minimumRange}–${ability.maximumRange} tiles.`
      );

      // Whether it rolls, said out loud for every damaging cast: an ability
      // that cannot miss is the most surprising thing about this vocabulary.
      if (ability.kind === "restore") {
        expect(recipe.summary).not.toContain("lands");
        expect(recipe.summary).not.toContain("Lands");
      } else if (ability.accuracy === undefined) {
        expect(recipe.summary).toContain("Always lands.");
      } else {
        expect(recipe.summary)
          .toContain(`Lands ${ability.accuracy} in 100.`);
      }
    }

    const arc = abilityRecipes.find((recipe) => recipe.id === "medieval_sweep")!;
    expect(arc.summary).toBe(
      "Damage, against defence. Covers 5 tiles. Power 3. Range 1 tile. " +
      "Always lands."
    );
  });

  it("brings the shipped campaign's own spells onto the shelf", () => {
    // The gap this shelf exists to close: Power Strike, Ember Bolt and Mend are
    // hand-authored inside one campaign, and an author browsing the library
    // could not see that the rules could express them. These are those three,
    // name and numbers both.
    const strike = buildAbility(project(), "heavy_blow", "", "medieval");
    expect(strike).toEqual({
      id: "power_strike",
      name: "Power Strike",
      kind: "damage",
      damageType: "physical",
      areaShape: "single",
      power: 6,
      minimumRange: 1,
      maximumRange: 1,
      accuracy: 85
    });
    const bolt = buildAbility(project(), "bolt", "", "medieval");
    expect(bolt.name).toBe("Ember Bolt");
    expect(bolt.power).toBe(4);
    expect(bolt.maximumRange).toBe(2);
    const mend = buildAbility(project(), "mend", "", "medieval");
    expect(mend).toEqual({
      id: "mend",
      name: "Mend",
      kind: "restore",
      areaShape: "single",
      power: 4,
      minimumRange: 1,
      maximumRange: 2
    });
  });

  it("does not collide when the same ability is added twice", () => {
    const source = project();
    const first = buildAbility(source, "bolt", "");
    source.abilities = [first];
    const second = buildAbility(source, "bolt", "");
    expect(second.id).not.toBe(first.id);
    expect(second.name).toBe(first.name);
  });

  it("produces schema-legal ability identifiers from awkward names", () => {
    const pattern = /^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$/u;
    for (const name of ["Surgeon's Stitch", "  ", "42", "Zoë's!!"]) {
      expect(buildAbility(project(), "mend", name).id).toMatch(pattern);
    }
    // Every offered name, in every setting, under its own default.
    for (const recipe of abilityRecipes) {
      expect(buildAbility(project(), recipe.cast, "", recipe.setting).id)
        .toMatch(pattern);
    }
  });

  it("carries action points and speed so roles feel different", () => {
    const rogue = buildCharacterChain(project(), "rogue", "Pip");
    const knight = buildCharacterChain(project(), "knight", "Rue");
    expect(rogue.unitClass!.baseStats.speed).toBeGreaterThan(
      knight.unitClass!.baseStats.speed!
    );
    expect(rogue.unitClass!.baseStats.actionPoints).toBe(2);
    // A rogue keeps moving after it strikes; a knight does not.
    expect(rogue.unitClass!.actsAfterAttacking).toBe(true);
    expect(knight.unitClass!.actsAfterAttacking).toBeUndefined();
  });

  // The one number no card is allowed to differ on.
  //
  // A single action point is authorable and means what it says: move or
  // strike, never both. A shelf that hands one out has chosen for the author,
  // and the choice only shows up as a refused strike on a finished board. Written per role rather than as a loop over `shapes` so that adding
  // a ninth role, or a setting that dresses one differently, has to come past
  // this list.
  it("gives every role a turn it can move and then strike in", () => {
    for (const role of CHARACTER_ROLES) {
      for (const setting of CATALOGUE_SETTINGS) {
        const made = buildCharacterChain(project(), role, "", setting.id);
        expect(
          `${setting.id}/${role}: ${made.unitClass!.baseStats.actionPoints}`
        ).toBe(`${setting.id}/${role}: 2`);
      }
    }
  });
});
