// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import { createSampleProject } from "../sample-projects";
import { CHARACTER_ROLES, buildCharacterChain } from "./character-recipe";
import { createSourceProject } from "./source-project-document";
import {
  TARGET_BUDGETS,
  targetNotes,
  targetSpend,
  type TargetBudget,
  type TargetSpend
} from "./target-budget";

function budget(id: string) {
  return TARGET_BUDGETS.find((target) => target.id === id)!;
}

const nintendo64 = budget("nintendo64");

/**
 * A machine tighter than any this repository ships to, used to drive the note
 * machinery over its own thresholds.
 *
 * Not a claim about hardware and never in `TARGET_BUDGETS`: it exists so the
 * overrun, the exactly-full and the merged-colour sentences are each exercised
 * by a spend that really crosses them, rather than being asserted about a
 * console that has room to spare.
 */
const cramped: TargetBudget = {
  id: "cramped",
  label: "Cramped",
  paletteBanks: 4,
  bankEntries: 16,
  channelBits: 3
};

const ARCHETYPES = [
  "knight", "archer", "mage", "stormcaller",
  "healer", "commander", "rogue", "beast"
];
const COLOURS = ["blue", "red", "green", "violet", "amber", "bone"];
const TERRAIN = [
  "water", "road", "forest", "mountain", "sand",
  "snow", "swamp", "hills", "ruins", "grass"
];

/** A game of exactly the size asked for: sides, roles each, kinds of ground. */
function game(options: {
  sides: number;
  roles: number;
  terrain: number;
  characterStyleId?: SourceProject["characterStyleId"];
  characterFigureId?: SourceProject["characterFigureId"];
  themeId?: SourceProject["themeId"];
}): SourceProject {
  const project = createSourceProject();
  const terrain = TERRAIN.slice(0, options.terrain);
  return {
    ...project,
    ...(options.characterStyleId
      ? { characterStyleId: options.characterStyleId }
      : {}),
    ...(options.characterFigureId
      ? { characterFigureId: options.characterFigureId }
      : {}),
    ...(options.themeId ? { themeId: options.themeId } : {}),
    factions: COLOURS.slice(0, options.sides).map((colour, index) => ({
      id: `side_${index}`,
      name: `The ${colour[0]!.toUpperCase()}${colour.slice(1)} Banner`,
      color: colour as "blue"
    })),
    classes: ARCHETYPES.slice(0, options.roles).map((archetype) => ({
      id: `${archetype}_class`,
      name: `${archetype} class`,
      baseStats: { health: 10, movement: 3, strength: 3, defense: 1 }
    })),
    unitTypes: COLOURS.slice(0, options.sides).flatMap((_, side) =>
      ARCHETYPES.slice(0, options.roles).map((archetype) => ({
        id: `${archetype}_${side}`,
        name: `${archetype} of side ${side}`,
        classId: `${archetype}_class`,
        factionId: `side_${side}`
      }))
    ),
    weapons: [],
    items: [],
    maps: [{
      id: "field",
      name: "The Long Field",
      width: terrain.length,
      height: 1,
      terrain
    }]
  };
}

const groundOf = (spend: TargetSpend) =>
  spend.groups.find((group) => group.label === "the ground")!;
const blueOf = (spend: TargetSpend) =>
  spend.groups.find((group) => group.label === "the blue side")!;

describe("target budgets", () => {
  it("reproduces the palette arithmetic measured off the art", () => {
    // Measured off the art: one theme's ten terrain
    // sheets are 36 colours and one faction across eight archetypes is 28,
    // transparent entry included. The model must arrive at those from the art,
    // not from the table. 28 is the first figure's roster and this game draws
    // the first figure, which is why the two agree. See the figure test below
    // for what the same eight cost at the second.
    //
    // The colours are a property of the drawings and the same on every target;
    // what a target pays is those colours divided by its own bank, so the
    // ground fills three sixteen-entry palettes and a side fills two.
    const spend = targetSpend(game({ sides: 1, roles: 8, terrain: 10 }),
                              nintendo64);
    expect(groundOf(spend).entries).toBe(36);
    expect(blueOf(spend).entries).toBe(28);
    expect(groundOf(spend).banks).toBe(3);
    expect(blueOf(spend).banks).toBe(2);
  });

  it("loses no colour at the Nintendo 64's depth", () => {
    // Measured: all 124 master entries survive five bits per channel, so
    // nothing a game draws with merges.
    const whole = targetSpend(game({ sides: 6, roles: 8, terrain: 10 }),
                              nintendo64);
    expect(whole.coloursAtDepth).toBe(whole.colours);
  });

  it("says nothing about an empty project", () => {
    expect(targetNotes(createSourceProject())).toEqual([]);
  });

  it("is not a cap on the library: every drawing at once fits", () => {
    // Every faction colour, every archetype and every terrain kind in one
    // game, more than any authored game is likely to hold, is still inside
    // the sixteen CI4 palettes TMEM keeps resident, and loses no colour. So
    // nothing is said, which is the whole of what a note means by silence.
    const everything = game({ sides: 6, roles: 8, terrain: 10 });
    expect(targetSpend(everything, nintendo64).banks).toBe(15);
    expect(targetSpend(everything, nintendo64).banks)
      .toBeLessThanOrEqual(nintendo64.paletteBanks);
    expect(targetNotes(everything)).toEqual([]);
  });

  it("holds the maintained samples to a known verdict", () => {
    // Both shipped samples are ordinary two-sided games on the temperate
    // theme. They fit every shipped target with room to spare, so the editor
    // says nothing about them at all.
    for (const id of ["tarnholt", "demo"]) {
      const sample = createSampleProject(id);
      expect(targetNotes(sample)).toEqual([]);
      const spend = targetSpend(sample, nintendo64);
      expect(spend.banks).toBeLessThan(nintendo64.paletteBanks);
    }
  });

  it("still says what a console makes of a game after a library insert", () => {
    // The library makes it easy to add many distinct characters, so the model
    // has to keep counting after one. It also has to see them as the roles
    // they are: a class the drawing convention cannot read counts as a knight,
    // and eight of those would understate a game by seven roles.
    const inserted = createSampleProject("demo");
    const before = targetSpend(inserted, nintendo64);
    for (const role of CHARACTER_ROLES) {
      const chain = buildCharacterChain(inserted, role, `New ${role}`);
      if (chain.weaponType) (inserted.weaponTypes ??= []).push(chain.weaponType);
      inserted.weapons.push(chain.weapon);
      if (chain.unitClass) inserted.classes.push(chain.unitClass);
      inserted.unitTypes.push(chain.unitType);
    }
    const after = targetSpend(inserted, nintendo64);
    // The demo starts as two knight drawings, one per side; afterwards all
    // eight roles are drawn in both side colours, which is the truth about
    // what the game now holds.
    expect(before.drawings).toBe(2);
    expect(after.drawings).toBe(CHARACTER_ROLES.length * 2);
    // And the sides cost more colours for it, which is what the note would
    // report if the machine were the one running short.
    expect(blueOf(after).entries).toBeGreaterThan(blueOf(before).entries);
  });

  it("names the records a game overruns a machine's palettes with", () => {
    const spend = targetSpend(game({ sides: 3, roles: 4, terrain: 10 }),
                              cramped);
    expect(spend.banks).toBeGreaterThan(cramped.paletteBanks);
    expect(spend.groups.map((group) => group.records)).toContainEqual(
      ["The Long Field (the map)"]
    );
    expect(spend.groups.map((group) => group.records)).toContainEqual(
      ["The Blue Banner (the faction)"]
    );
  });

  it("counts what a machine of fewer bits would lose", () => {
    // Three bits a channel is coarse enough that master entries collide, and
    // the model has to say so in the target's own depth rather than the art's.
    const spend = targetSpend(game({ sides: 6, roles: 8, terrain: 10 }),
                              cramped);
    expect(spend.coloursAtDepth).toBeLessThan(spend.colours);
  });

  it("costs the style a project named, and no other", () => {
    // Measured across the shipped styles: sci-fi spends 44 colours across its
    // 48 sprites where medieval spends 47, so one side's share is smaller too.
    const side = (project: SourceProject) =>
      blueOf(targetSpend(project, nintendo64)).entries;
    expect(side(game({ sides: 1, roles: 8, terrain: 1 }))).toBe(28);
    expect(side(game({
      sides: 1, roles: 8, terrain: 1, characterStyleId: "scifi"
    }))).toBe(25);
  });

  it("costs a character drawn in its own style in that style", () => {
    // A character may name a style the project did not, and a console would
    // have to carry that drawing, so the budget has to price it. Counting the
    // whole side against the project's style would under-count a mixed game,
    // the one direction a budget may never be wrong in.
    const plain = game({ sides: 1, roles: 8, terrain: 1 });
    const mixed: SourceProject = {
      ...plain,
      unitTypes: plain.unitTypes.map((unitType, index) =>
        index === 0 ? { ...unitType, characterStyleId: "undead" } : unitType
      )
    };
    const side = (project: SourceProject) =>
      blueOf(targetSpend(project, nintendo64)).entries;
    // The eight roles are unchanged; one of them is drawn by another hand, and
    // that drawing spends colours the medieval roster does not.
    expect(side(mixed)).toBeGreaterThan(side(plain));
    // And a project nobody overrides is priced exactly as it was.
    expect(side({ ...plain })).toBe(side(plain));
  });

  it("costs a character drawn at its own figure at that figure", () => {
    // A figure is a second drawing and not a transform, so `medieval`'s second
    // figures spend master entries their first figures do not. A budget that
    // read a row without naming a figure would have to hold the union of both,
    // which over-charges every game that draws one of them, and a game always
    // draws exactly one per character, because a figure resolves from the
    // character or from the game exactly as a style does.
    //
    // Measured off tools/placeholder_art/assets/palette_usage.json: one
    // faction across eight `medieval` archetypes is 28 entries at the first
    // figure and 30 at the second, and swapping the mage alone is 29. Three
    // distinct answers is the whole point: a table keyed without the figure
    // would give 30 to all three, and one holding only the first figure would
    // give 28 to all three.
    const side = (project: SourceProject) =>
      blueOf(targetSpend(project, nintendo64)).entries;
    const plain = game({ sides: 1, roles: 8, terrain: 1 });
    expect(side(plain)).toBe(28);
    expect(side(game({
      sides: 1, roles: 8, terrain: 1, characterFigureId: "second"
    }))).toBe(30);
    // One character at the other figure, and the rest of the roster left where
    // the game put it. The mage rather than the knight because the mage is one
    // whose second figure spends an entry the whole first-figure roster does
    // not, and a swap that cost nothing would not distinguish anything.
    expect(side({
      ...plain,
      unitTypes: plain.unitTypes.map((unitType) =>
        unitType.classId === "mage_class"
          ? { ...unitType, characterFigureId: "second" as const }
          : unitType
      )
    })).toBe(29);
  });

  it("costs the season a project chose, not every season", () => {
    // Both are one theme's ten sheets: 36 colours. A project never pays for
    // the three themes it did not choose.
    const ground = (themeId?: SourceProject["themeId"]) =>
      groundOf(targetSpend(
        game({ sides: 1, roles: 1, terrain: 10, themeId }), nintendo64
      )).entries;
    expect(ground("winter")).toBe(36);
    expect(ground()).toBe(36);
  });

  it("counts a character with no side in both sides' colours", () => {
    const loose: SourceProject = {
      ...createSourceProject(),
      classes: [{
        id: "knight_class",
        name: "Knight",
        baseStats: { health: 10, movement: 3, strength: 3, defense: 1 }
      }],
      unitTypes: [{ id: "wanderer", name: "Wanderer", classId: "knight_class" }],
      weapons: [],
      items: [],
      maps: [{
        id: "field", name: "Field", width: 1, height: 1, terrain: ["grass"]
      }]
    };
    const spend = targetSpend(loose, nintendo64);
    expect(spend.groups.map((group) => group.label)).toEqual([
      "the ground", "the blue side", "the red side"
    ]);
    expect(spend.groups[1]!.records).toEqual(["Wanderer (the character)"]);
  });
});
