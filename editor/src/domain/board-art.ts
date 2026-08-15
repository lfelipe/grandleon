// SPDX-License-Identifier: MIT
import {
  ARCHETYPES,
  BACKDROPS,
  BACKDROP_ROWS,
  BLOB_SHEET_COLUMNS,
  CHARACTER_FIGURES,
  CHARACTER_FRAME_SHEETS,
  CHARACTER_SPRITES,
  CHARACTER_STYLES,
  DEFAULT_CHARACTER_FIGURE,
  DEFAULT_CHARACTER_STYLE,
  DEFAULT_THEME,
  MASK_TO_VARIANT,
  TERRAIN_SHEETS,
  THEMES,
  TILE_SIZE
} from "../generated/board-art";
import { terrainKind } from "./terrain-presentation";

/**
 * The generated art the tactical board draws, applied through the generator's
 * own convention: an eight-bit neighbour mask indexes the manifest's
 * mask_to_variant table, which picks one of the 47 blob variants from a
 * terrain's sheet. The table itself is generated, so the lookup here cannot
 * drift from the sheets it addresses.
 */

const NEIGHBOUR_BITS: readonly (readonly [number, number, number])[] = [
  [0, -1, 1], // N
  [1, -1, 2], // NE
  [1, 0, 4], // E
  [1, 1, 8], // SE
  [0, 1, 16], // S
  [-1, 1, 32], // SW
  [-1, 0, 64], // W
  [-1, -1, 128] // NW
];

/**
 * The theme menu, in the generator's own order. It is the same list the source
 * schema's `themeId` enumerates and the same order the Nintendo 64 play ROM
 * indexes, so a project's ground looks the same everywhere.
 */
export const THEME_IDS = THEMES.map((theme) => theme.id);

export { DEFAULT_THEME, THEMES };

/**
 * The theme a project is drawn in: the one it chose, or the default theme for
 * a project that names none, or names one the library does not hold.
 */
export function projectTheme(themeId: string | undefined): string {
  return themeId && THEME_IDS.includes(themeId) ? themeId : DEFAULT_THEME;
}

function sheetsOf(theme: string | undefined): Readonly<Record<string, string>> {
  return TERRAIN_SHEETS[projectTheme(theme)]!;
}

/** The sheet a terrain draws from. Unknown terrain reads as grass. */
export function terrainSheetKind(terrain: string): string {
  const kind = terrainKind(terrain);
  return kind in TERRAIN_SHEETS[DEFAULT_THEME]! ? kind : "grass";
}

/**
 * The raw neighbour mask for a cell: a bit per neighbour drawing from the
 * same sheet, with off-board treated as different so the board edge reads as
 * a coastline. Diagonal collapsing belongs to the generated table, not here.
 */
export function neighbourMask(
  terrain: readonly string[],
  width: number,
  height: number,
  x: number,
  y: number
): number {
  const kindAt = (cx: number, cy: number) =>
    terrainSheetKind(terrain[cy * width + cx] ?? "");
  const centre = kindAt(x, y);
  let mask = 0;
  for (const [dx, dy, bit] of NEIGHBOUR_BITS) {
    const nx = x + dx;
    const ny = y + dy;
    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
    if (kindAt(nx, ny) === centre) mask |= bit;
  }
  return mask;
}

export interface SpriteRegion {
  href: string;
  /** Source rectangle origin inside the sheet, in sheet pixels. */
  sx: number;
  sy: number;
}

/**
 * The blob sheet region a cell blits, from the generated lookup table, in the
 * project's theme. The variant is chosen from the neighbour mask alone, so the
 * same map draws the same tiles in every theme and only their colours differ.
 */
export function terrainSprite(
  terrain: readonly string[],
  width: number,
  height: number,
  x: number,
  y: number,
  theme?: string
): SpriteRegion {
  const variant =
    MASK_TO_VARIANT[neighbourMask(terrain, width, height, x, y)] ?? 0;
  const sheets = sheetsOf(theme);
  // terrainSheetKind only returns keys present in the sheet table, so the
  // grass fallback is unreachable; it exists because the index signature
  // cannot carry that guarantee.
  return {
    href: sheets[terrainSheetKind(terrain[y * width + x] ?? "")] ??
      sheets.grass!,
    sx: (variant % BLOB_SHEET_COLUMNS) * TILE_SIZE,
    sy: Math.floor(variant / BLOB_SHEET_COLUMNS) * TILE_SIZE
  };
}

/**
 * The archetype roster, in the generator's own order. It is shared by every
 * character style, since a style supplies drawings and never a roster, which
 * is what lets one keyword table below resolve a class name whatever style draws it.
 */
export { ARCHETYPES };

/**
 * The character style menu, in the generator's own order. It is the same list
 * the source schema's `characterStyleId` enumerates and the same order the
 * native content reader indexes, so a project's characters look the same
 * everywhere.
 */
export const CHARACTER_STYLE_IDS = CHARACTER_STYLES.map((style) => style.id);

export { CHARACTER_STYLES, DEFAULT_CHARACTER_STYLE };

/**
 * The figure menu, in the generator's own order. The second axis of the same
 * choice: a style says whose hand drew a role and a figure says at what build,
 * and every figure draws every archetype in every style, so the two combine
 * freely.
 */
export const CHARACTER_FIGURE_IDS = CHARACTER_FIGURES.map(
  (figure) => figure.id
);

export { CHARACTER_FIGURES, DEFAULT_CHARACTER_FIGURE };

/**
 * The figure a project's characters are drawn with: the one it chose, or the
 * default figure for a project that names none, or names one the library does
 * not draw.
 */
export function projectCharacterFigure(figureId: string | undefined): string {
  return figureId && CHARACTER_FIGURE_IDS.includes(figureId)
    ? figureId
    : DEFAULT_CHARACTER_FIGURE;
}

/**
 * The style a project's characters are drawn in: the one it chose, or the
 * default style for a project that names none, or names one the library does
 * not hold.
 */
export function projectCharacterStyle(styleId: string | undefined): string {
  return styleId && CHARACTER_STYLE_IDS.includes(styleId)
    ? styleId
    : DEFAULT_CHARACTER_STYLE;
}

/**
 * The scene backdrop menu, in the generator's own order. It is the same list
 * the source schema's `backgroundId` enumerates and the same order the native
 * content reader indexes, so a scene is set against the same thing everywhere.
 */
export const BACKDROP_IDS = BACKDROPS.map((backdrop) => backdrop.id);

export { BACKDROPS, BACKDROP_ROWS };

/**
 * The bands a scene is drawn against, or `undefined` for a scene that names no
 * backdrop, which is what a caller draws its own flat fill for. A name the
 * library does not hold resolves the
 * same way: presentation nobody can draw is nothing drawn, never a neighbour.
 */
export function sceneBackdrop(
  backgroundId: string | undefined
): (typeof BACKDROPS)[number] | undefined {
  if (!backgroundId) return undefined;
  return BACKDROPS.find((backdrop) => backdrop.id === backgroundId);
}

/**
 * The same backdrop, addressed the way a compiled package carries it: the menu
 * index plus one, with 0 meaning a scene that names none. The engine holds no
 * names, so this is the join between what it hands back and what is drawn.
 */
export function backdropByIndex(
  index: number | undefined
): (typeof BACKDROPS)[number] | undefined {
  if (index === undefined || index <= 0 || index > BACKDROPS.length) {
    return undefined;
  }
  return BACKDROPS[index - 1];
}

/** The index a compiled package carries for a name, or 0 for none. */
export function backdropIndex(backgroundId: string | undefined): number {
  if (!backgroundId) return 0;
  const at = BACKDROP_IDS.indexOf(backgroundId);
  return at < 0 ? 0 : at + 1;
}

/**
 * A backdrop as one CSS background, with hard stops so the bands stay bands.
 * Percentages rather than pixels, because the row count is a proportion of
 * whatever height the scene area happens to have, the same arithmetic every
 * console does against its own frame.
 */
export function backdropGradient(
  backdrop: (typeof BACKDROPS)[number]
): string {
  const stops = backdrop.bands.flatMap((band) => {
    const from = (band.top / BACKDROP_ROWS) * 100;
    const to = ((band.top + band.rows) / BACKDROP_ROWS) * 100;
    return [`${band.color} ${from}%`, `${band.color} ${to}%`];
  });
  return `linear-gradient(to bottom, ${stops.join(", ")})`;
}

/**
 * The archetype a class renders as, by the same keyword convention
 * terrainKind uses. A class the art does not know reads as a knight rather
 * than as nothing.
 */
export function archetypeForClass(classId: string | undefined): string {
  const normalized = (classId ?? "").toLowerCase();
  return ARCHETYPES.find((archetype) => normalized.includes(archetype)) ?? "knight";
}

/**
 * The faction colour menu, in the generator's own order. It is the same list
 * the source schema's `faction.color` enumerates and the same order the
 * Nintendo 64 play ROM indexes, so a faction looks the same everywhere.
 */
export const FACTION_COLORS = [
  "blue",
  "red",
  "green",
  "violet",
  "amber",
  "bone"
] as const;

export type FactionColor = (typeof FACTION_COLORS)[number];

/** The colour a side wears when nothing in the project has chosen one. */
export function sideColor(side: "first" | "second"): FactionColor {
  return side === "first" ? "blue" : "red";
}

/**
 * The colour a faction wears: the one it chose, or, for a faction nobody has
 * chosen one for, the menu
 * colour at the faction's own position in the list. Positions past the end of
 * the menu wrap, so every faction always has a colour to draw.
 */
export function factionColor(
  factions: readonly { readonly id: string; readonly color?: string }[],
  factionId: string | undefined
): FactionColor | undefined {
  const index = factions.findIndex((faction) => faction.id === factionId);
  if (index < 0) return undefined;
  const chosen = factions[index]!.color;
  if (chosen && (FACTION_COLORS as readonly string[]).includes(chosen)) {
    return chosen as FactionColor;
  }
  return FACTION_COLORS[index % FACTION_COLORS.length]!;
}

/**
 * The sprite for a unit: style and figure from the character or the game it is
 * in, archetype from its class, colour from its faction, and from its side
 * only when no faction claims it. The four are independent: changing the
 * style or the figure changes only the drawing, and the archetype a class
 * selects is the same under every one of them.
 */
export function unitSprite(
  classId: string | undefined,
  side: "first" | "second",
  color?: FactionColor,
  characterStyleId?: string,
  characterFigureId?: string
): string {
  const faction = color ?? sideColor(side);
  const style = projectCharacterStyle(characterStyleId);
  const figure = projectCharacterFigure(characterFigureId);
  // Every style holds every archetype in every colour at every figure in the
  // generated set; the knight fallback exists because the index signature
  // cannot carry that guarantee.
  return (
    CHARACTER_SPRITES[
      `${style}_${figure}_${archetypeForClass(classId)}_${faction}`
    ] ?? CHARACTER_SPRITES[`${style}_${figure}_knight_${faction}`]!
  );
}

/** What `speakerPortrait` needs of a project, and nothing else. */
export interface SpeakerPortraitProject {
  readonly characterStyleId?: string | undefined;
  readonly characterFigureId?: string | undefined;
  readonly unitTypes?: readonly {
    readonly id: string;
    readonly classId?: string | undefined;
    readonly factionId?: string | undefined;
    readonly characterStyleId?: string | undefined;
    readonly characterFigureId?: string | undefined;
  }[];
  readonly factions?: readonly {
    readonly id: string;
    readonly color?: string;
  }[];
}

/**
 * The portrait for a speaker a scene has cast: the drawing the board draws for
 * that character.
 *
 * It resolves the same four things a board resolves and in the same order: the
 * archetype from the character's class, the colour from its faction, and the
 * style and figure from the character's own if it names them and the game's if
 * it does not. Then it asks `unitSprite`, so a portrait and a board sprite of
 * one character are one drawing rather than two that agree.
 *
 * A speaker the scene cast nobody for, and one cast as a character the project
 * no longer holds, get no portrait rather than somebody else's.
 */
export function speakerPortrait(
  project: SpeakerPortraitProject,
  unitTypeId: string | undefined
): string | undefined {
  if (!unitTypeId) return undefined;
  const unitType = (project.unitTypes ?? []).find(
    (candidate) => candidate.id === unitTypeId
  );
  if (unitType === undefined) return undefined;
  return unitSprite(
    unitType.classId,
    "first",
    factionColor(project.factions ?? [], unitType.factionId),
    unitType.characterStyleId ?? project.characterStyleId,
    unitType.characterFigureId ?? project.characterFigureId
  );
}

/**
 * The sequence strip beside that sprite: one row of animation cells, resolved
 * by exactly the same four keys, because the library ships one strip per
 * standing sprite and a unit the board can draw standing it can draw moving.
 * A caller windows the strip by cell position; see `SEQUENCE_CELLS` for the
 * order. Position is the contract and the name is not: the strip is one row of
 * four 32x32 cells in the fixed order `walk_contact`, `walk_pass`, `lunge`,
 * `cast`, every style ships every cell, and a client indexes by cell number.
 */
export function unitFrameSheet(
  classId: string | undefined,
  side: "first" | "second",
  color?: FactionColor,
  characterStyleId?: string,
  characterFigureId?: string
): string {
  const faction = color ?? sideColor(side);
  const style = projectCharacterStyle(characterStyleId);
  const figure = projectCharacterFigure(characterFigureId);
  return (
    CHARACTER_FRAME_SHEETS[
      `${style}_${figure}_${archetypeForClass(classId)}_${faction}`
    ] ?? CHARACTER_FRAME_SHEETS[`${style}_${figure}_knight_${faction}`]!
  );
}
