// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  ARCHETYPES,
  CHARACTER_FIGURE_IDS,
  CHARACTER_STYLE_IDS,
  DEFAULT_CHARACTER_FIGURE,
  DEFAULT_CHARACTER_STYLE,
  DEFAULT_THEME,
  FACTION_COLORS,
  THEME_IDS,
  archetypeForClass,
  factionColor,
  neighbourMask,
  projectCharacterFigure,
  projectCharacterStyle,
  projectTheme,
  terrainSprite,
  speakerPortrait,
  terrainSheetKind,
  unitSprite
} from "./board-art";
import {
  BLOB_SHEET_COLUMNS,
  CHARACTER_SPRITES,
  INTERIOR_VARIANT,
  MASK_TO_VARIANT,
  TERRAIN_KINDS,
  TERRAIN_SHEETS,
  TILE_SIZE
} from "../generated/board-art";
import { sourceV1Schemas } from "../generated/source-v1-schemas";

const grassField = (width: number, height: number) =>
  Array.from({ length: width * height }, () => "grass");

describe("board-art", () => {
  it("has a generated sprite for every archetype in every faction colour", () => {
    // unitSprite falls back to the knight rather than failing, so this is
    // the check that keeps the fallback unreachable in practice. Every style
    // holds every archetype at every figure, which is what lets the key
    // factor.
    for (const style of CHARACTER_STYLE_IDS) {
      for (const figure of CHARACTER_FIGURE_IDS) {
        for (const archetype of ARCHETYPES) {
          for (const color of FACTION_COLORS) {
            expect(
              CHARACTER_SPRITES[`${style}_${figure}_${archetype}_${color}`]
            ).toBeDefined();
          }
        }
      }
    }
  });

  it("carries the generator's full 256-entry autotile table", () => {
    expect(MASK_TO_VARIANT).toHaveLength(256);
    expect(MASK_TO_VARIANT[255]).toBe(INTERIOR_VARIANT);
    expect(new Set(MASK_TO_VARIANT).size).toBe(47);
    for (const variant of MASK_TO_VARIANT) {
      expect(variant).toBeGreaterThanOrEqual(0);
      expect(variant).toBeLessThanOrEqual(INTERIOR_VARIANT);
    }
  });

  it("collapses diagonals whose cardinals disagree, per the convention", () => {
    // NE alone cannot shape a corner: N (1) or E (4) is different terrain.
    expect(MASK_TO_VARIANT[2]).toBe(MASK_TO_VARIANT[0]);
    expect(MASK_TO_VARIANT[1 | 2]).toBe(MASK_TO_VARIANT[1]);
    // With both cardinals set, the diagonal is meaningful.
    expect(MASK_TO_VARIANT[1 | 2 | 4]).not.toBe(MASK_TO_VARIANT[1 | 4]);
  });

  it("masks a surrounded cell as interior and the board edge as coastline", () => {
    const terrain = grassField(3, 3);
    expect(neighbourMask(terrain, 3, 3, 1, 1)).toBe(255);
    // The corner keeps only E, SE, and S.
    expect(neighbourMask(terrain, 3, 3, 0, 0)).toBe(4 | 8 | 16);
  });

  it("treats presentation-equivalent terrain as the same sheet", () => {
    // A bridge draws from the road sheet, so road|bridge|road is one body.
    const terrain = ["road", "bridge", "road"];
    expect(neighbourMask(terrain, 3, 1, 1, 0)).toBe(4 | 64);
    expect(terrainSheetKind("bridge")).toBe("road");
    expect(terrainSheetKind("anything else")).toBe("grass");
  });

  it("addresses the blob sheet by variant, eight per row", () => {
    const sprite = terrainSprite(grassField(3, 3), 3, 3, 1, 1);
    expect(sprite.href).toBe("board/terrain/grass_blob.png");
    expect(sprite.sx).toBe((INTERIOR_VARIANT % BLOB_SHEET_COLUMNS) * TILE_SIZE);
    expect(sprite.sy).toBe(
      Math.floor(INTERIOR_VARIANT / BLOB_SHEET_COLUMNS) * TILE_SIZE
    );
    const corner = terrainSprite(grassField(3, 3), 3, 3, 0, 0);
    expect(corner.sx).not.toBe(sprite.sx);
  });

  it("chooses unit sprites by class keyword and side", () => {
    expect(archetypeForClass("stormcaller")).toBe("stormcaller");
    expect(archetypeForClass("veteran_knight")).toBe("knight");
    expect(archetypeForClass("guard")).toBe("knight");
    expect(archetypeForClass(undefined)).toBe("knight");
    expect(unitSprite("healer", "first"))
      .toBe("board/characters/healer_blue.png");
    expect(unitSprite("guard", "second"))
      .toBe("board/characters/knight_red.png");
  });

  it("draws a unit in its faction's chosen colour", () => {
    expect(unitSprite("mage", "second", "violet"))
      .toBe("board/characters/mage_violet.png");
    // The side is only the fallback, so a first-side unit in a faction that
    // picked amber is amber, not blue.
    expect(unitSprite("archer", "first", "amber"))
      .toBe("board/characters/archer_amber.png");
  });

  it("falls back to faction order when a faction picks no colour", () => {
    const factions = [
      { id: "dawn_guard" },
      { id: "ashen_coil" },
      { id: "wardens" }
    ];
    expect(factionColor(factions, "dawn_guard")).toBe("blue");
    expect(factionColor(factions, "ashen_coil")).toBe("red");
    expect(factionColor(factions, "wardens")).toBe("green");
    expect(factionColor(factions, "nobody")).toBeUndefined();
  });

  it("prefers a faction's own colour over its position", () => {
    const factions = [
      { id: "dawn_guard", color: "bone" },
      { id: "ashen_coil" }
    ];
    expect(factionColor(factions, "dawn_guard")).toBe("bone");
    expect(factionColor(factions, "ashen_coil")).toBe("red");
  });

  it("keeps every faction drawable past the end of the menu", () => {
    // Seven factions, six colours: the seventh wraps rather than resolving to
    // nothing, because a unit with no sprite is worse than a repeated colour.
    const factions = Array.from({ length: 7 }, (_, index) => ({
      id: `faction_${index}`
    }));
    expect(factionColor(factions, "faction_6")).toBe("blue");
  });

  it("offers the theme menu the schema and the console index", () => {
    expect(THEME_IDS).toEqual(["temperate", "autumn", "winter", "ashland"]);
    expect(DEFAULT_THEME).toBe("temperate");
    // Every terrain the library draws exists in every theme, so choosing a
    // season can never leave a cell without a sheet.
    for (const theme of THEME_IDS) {
      for (const kind of TERRAIN_KINDS) {
        expect(TERRAIN_SHEETS[theme]?.[kind]).toBeDefined();
      }
    }
  });

  it("draws a project's chosen theme, and the default for anything else", () => {
    expect(projectTheme("winter")).toBe("winter");
    expect(projectTheme(undefined)).toBe(DEFAULT_THEME);
    // Schema validation rejects it, but the board still has to draw a project
    // that reached it another way.
    expect(projectTheme("monsoon")).toBe(DEFAULT_THEME);
  });

  it("keeps the tile the same and changes only the sheet under a theme", () => {
    const field = grassField(3, 3);
    const temperate = terrainSprite(field, 3, 3, 1, 1);
    const winter = terrainSprite(field, 3, 3, 1, 1, "winter");
    expect(temperate.href).toBe("board/terrain/grass_blob.png");
    expect(winter.href).toBe("board/terrain/grass_blob_winter.png");
    // The variant is chosen from the neighbour mask alone: a theme is a
    // recolour, so the same cell blits the same region of its sheet.
    expect(winter.sx).toBe(temperate.sx);
    expect(winter.sy).toBe(temperate.sy);
  });

  it("draws terrain the library grew into, in every theme", () => {
    expect(terrainSheetKind("hillside")).toBe("hills");
    expect(terrainSheetKind("old ruins")).toBe("ruins");
    expect(terrainSprite(["ruins"], 1, 1, 0, 0, "ashland").href)
      .toBe("board/terrain/ruins_blob_ashland.png");
  });

  it("offers the character style menu the schema and the reader index", () => {
    expect(CHARACTER_STYLE_IDS)
      .toEqual(["medieval", "scifi", "mythical", "nature", "sengoku", "undead", "pirates"]);
    expect(DEFAULT_CHARACTER_STYLE).toBe("medieval");
    // The menu is one list, in one order, in all four places. The schema's own
    // enum is read here rather than restated, so the editor cannot drift from
    // the contract it validates against; the native reader's copy is asserted
    // against the same generated header in tests/game_content.
    const project = sourceV1Schemas.find(
      (schema) =>
        (schema as { $id?: string }).$id ===
        "https://grandleon.dev/schemas/source/v1/project.schema.json"
    ) as {
      properties: { characterStyleId: { enum: readonly string[] } };
    };
    expect(project.properties.characterStyleId.enum).toEqual(
      CHARACTER_STYLE_IDS
    );
  });

  it("draws a project's chosen style, and the default for anything else", () => {
    expect(projectCharacterStyle("medieval")).toBe("medieval");
    expect(projectCharacterStyle(undefined)).toBe(DEFAULT_CHARACTER_STYLE);
    // Schema validation rejects it, but the board still has to draw a project
    // that reached it another way.
    expect(projectCharacterStyle("gothic")).toBe(DEFAULT_CHARACTER_STYLE);
  });

  it("draws every commissioned style from its own files, leaving the first alone", () => {
    // A commissioned style is a real set of art, not the default recoloured:
    // every archetype resolves to a suffixed file of its own, and none of them
    // is the file the default style resolves to.
    for (const style of ["scifi", "mythical", "nature", "sengoku"]) {
      expect(projectCharacterStyle(style)).toBe(style);
    }
    for (const style of ["scifi", "mythical", "nature", "sengoku"]) {
      for (const archetype of ARCHETYPES) {
        const drawn = unitSprite(archetype, "first", "blue", style);
        expect(drawn).toBe(`board/characters/${archetype}_blue_${style}.png`);
        expect(drawn).not.toBe(
          unitSprite(archetype, "first", "blue", "medieval")
        );
      }
    }
    // The default style carries no suffix, because its files are the ones
    // that existed before the menu did.
    expect(unitSprite("knight", "first", "blue", "medieval")).toBe(
      "board/characters/knight_blue.png"
    );
  });

  it("keeps a project written before the menu drawn as it always was", () => {
    // The absent-value rule, at the one place it is visible: naming no style
    // and naming the default style select the same file.
    expect(unitSprite("healer", "first", undefined, undefined)).toBe(
      unitSprite("healer", "first", undefined, DEFAULT_CHARACTER_STYLE)
    );
    expect(unitSprite("healer", "first")).toBe(
      "board/characters/healer_blue.png"
    );
  });

  it("picks a sprite by style, figure, archetype and faction colour", () => {
    // The four dimensions are independent: changing one moves that part of
    // the key and nothing else.
    for (const style of CHARACTER_STYLE_IDS) {
      for (const figure of CHARACTER_FIGURE_IDS) {
        expect(unitSprite("mage", "second", "violet", style, figure)).toBe(
          CHARACTER_SPRITES[`${style}_${figure}_mage_violet`]
        );
        // The archetype a class selects depends on neither of them.
        expect(
          unitSprite("veteran_knight", "first", "amber", style, figure)
        ).toBe(CHARACTER_SPRITES[`${style}_${figure}_knight_amber`]);
        // Nor does the faction colour it is drawn in.
        expect(unitSprite("archer", "first", "bone", style, figure)).toBe(
          CHARACTER_SPRITES[`${style}_${figure}_archer_bone`]
        );
      }
      // And a figure combines with every style: a role at the second build is
      // the same role, drawn by the same hand.
      expect(unitSprite("mage", "first", "blue", style, "second")).not.toBe(
        unitSprite("mage", "first", "blue", style, "first")
      );
    }
  });

  it("offers the figure menu the schema and the reader index", () => {
    expect(CHARACTER_FIGURE_IDS).toEqual(["first", "second"]);
    expect(DEFAULT_CHARACTER_FIGURE).toBe("first");
    const project = sourceV1Schemas.find(
      (schema) =>
        (schema as { $id?: string }).$id ===
        "https://grandleon.dev/schemas/source/v1/project.schema.json"
    ) as {
      properties: { characterFigureId: { enum: readonly string[] } };
    };
    expect(project.properties.characterFigureId.enum).toEqual(
      CHARACTER_FIGURE_IDS
    );
    // A character names a figure from the same menu the game does, because the
    // game's choice is a default and never a gate.
    const unitType = sourceV1Schemas.find(
      (schema) =>
        (schema as { $id?: string }).$id ===
        "https://grandleon.dev/schemas/source/v1/unit-type.schema.json"
    ) as {
      properties: {
        characterFigureId: { enum: readonly string[] };
        characterStyleId: { enum: readonly string[] };
      };
    };
    expect(unitType.properties.characterFigureId.enum).toEqual(
      CHARACTER_FIGURE_IDS
    );
    expect(unitType.properties.characterStyleId.enum).toEqual(
      CHARACTER_STYLE_IDS
    );
  });

  it("draws a project written before figures with the figure it always had", () => {
    expect(projectCharacterFigure(undefined)).toBe(DEFAULT_CHARACTER_FIGURE);
    // Schema validation rejects it, but the board still has to draw a project
    // that reached it another way.
    expect(projectCharacterFigure("stooped")).toBe(DEFAULT_CHARACTER_FIGURE);
    expect(unitSprite("healer", "first", undefined, undefined, undefined))
      .toBe(unitSprite("healer", "first", undefined, undefined, "first"));
  });

  it("draws a class naming no archetype as a knight under any style", () => {
    for (const style of CHARACTER_STYLE_IDS) {
      expect(unitSprite("quartermaster", "first", "green", style)).toBe(
        CHARACTER_SPRITES[`${style}_${DEFAULT_CHARACTER_FIGURE}_knight_green`]
      );
    }
  });

  it("keeps the style and the season independent", () => {
    // A style supplies character art and nothing else: it may not disturb the
    // ground, and the season may not disturb the characters.
    const field = grassField(3, 3);
    expect(terrainSprite(field, 3, 3, 1, 1, "winter").href).toBe(
      "board/terrain/grass_blob_winter.png"
    );
    for (const style of CHARACTER_STYLE_IDS) {
      expect(unitSprite("mage", "first", "violet", style)).toBe(
        CHARACTER_SPRITES[`${style}_${DEFAULT_CHARACTER_FIGURE}_mage_violet`]
      );
    }
  });

  it("ignores a colour the menu does not offer", () => {
    // Schema validation rejects it, but the board still has to draw a project
    // that reached it another way.
    expect(factionColor([{ id: "gilded", color: "chartreuse" }], "gilded"))
      .toBe("blue");
  });

  it("draws a cast speaker as the character the board draws", () => {
    // The whole point of a cast: none of these three names spells its own
    // archetype, and two of them are drawn by another hand at another build.
    const project = {
      characterStyleId: "medieval",
      characterFigureId: "first",
      unitTypes: [
        { id: "house_knight", classId: "knight", factionId: "company" },
        {
          id: "house_mage",
          classId: "mage",
          factionId: "company",
          characterFigureId: "second"
        },
        {
          id: "wood_archer",
          classId: "archer",
          factionId: "company",
          characterStyleId: "nature"
        }
      ],
      factions: [{ id: "company", color: "blue" as const }]
    };

    expect(speakerPortrait(project, "house_knight")).toBe(
      CHARACTER_SPRITES.medieval_first_knight_blue
    );
    // Its own figure, the game's style.
    expect(speakerPortrait(project, "house_mage")).toBe(
      CHARACTER_SPRITES.medieval_second_mage_blue
    );
    // Its own style, the game's figure.
    expect(speakerPortrait(project, "wood_archer")).toBe(
      CHARACTER_SPRITES.nature_first_archer_blue
    );
  });

  it("draws no portrait for a speaker nobody was named for", () => {
    // A client with nothing to draw draws nothing, rather than the first
    // character it can find.
    const project = {
      unitTypes: [{ id: "house_knight", classId: "knight" }],
      factions: []
    };
    expect(speakerPortrait(project, undefined)).toBeUndefined();
    expect(speakerPortrait(project, "")).toBeUndefined();
    expect(speakerPortrait(project, "departed_hero")).toBeUndefined();
  });
});
