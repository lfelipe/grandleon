// SPDX-License-Identifier: MIT
import {
  DEFAULT_THEME,
  TERRAIN_COLORS,
  TERRAIN_GLYPHS,
  TERRAIN_KEYWORDS,
  TERRAIN_KINDS
} from "../generated/board-art";

/**
 * How an authored terrain name becomes a colour and a mark.
 *
 * The kinds, the keywords that select them, their marks, and their colours in
 * every theme are all generated from the art library's own terrain registry,
 * so adding a terrain there is what makes it appear here, and the Nintendo 64
 * applies the same table rather than a list of its own.
 */

/** A terrain kind the art library draws, or "custom" for one it does not. */
export type TerrainKind = string;

export const CUSTOM_KIND = "custom";

export function terrainKind(terrain: string): TerrainKind {
  const normalized = terrain.toLowerCase();
  for (const kind of TERRAIN_KINDS) {
    const keywords = TERRAIN_KEYWORDS[kind] ?? [];
    if (keywords.some((keyword) => normalized.includes(keyword))) return kind;
  }
  return CUSTOM_KIND;
}

/** The theme's colour table, falling back to the default theme's. */
function colorsOf(theme: string | undefined) {
  return TERRAIN_COLORS[theme ?? DEFAULT_THEME] ?? TERRAIN_COLORS[DEFAULT_THEME]!;
}

// The accepted visual-map-authoring requirement forbids conveying state by
// colour alone, so every tile keeps a glyph; a single mark carries that
// obligation without turning the map back into a wall of words.
export function terrainGlyph(terrain: string): string {
  const kind = terrainKind(terrain);
  if (kind !== CUSTOM_KIND) return TERRAIN_GLYPHS[kind] ?? "?";
  // Unknown terrain falls back to its own first letter, so two custom kinds
  // stay distinguishable from each other rather than both reading as "?".
  return terrain.trim().slice(0, 1).toUpperCase() || "?";
}

export function terrainColor(terrain: string, theme?: string): string {
  const kind = terrainKind(terrain);
  const colors = colorsOf(theme);
  const color = colors[kind];
  if (color) return color;

  let hash = 0;
  for (const character of terrain) {
    hash = (hash * 31 + character.charCodeAt(0)) | 0;
  }
  return `hsl(${Math.abs(hash) % 360} 35% 52%)`;
}
