// SPDX-License-Identifier: MIT
// What one authored game costs a console, and whether the console can pay.
//
// Only the art a game actually uses is exported: a console build embeds the
// styles its content draws and the one terrain theme it names, and nothing
// else. So a console's limits are spent by *this* game, and what spends them
// is this game's own variety: the characters it holds, the styles and figures
// those characters are drawn at, the colours its sides wear, and the ground its
// maps are made of. A character that names a style or a figure of its own is
// priced at that style and that figure, because that is the drawing the console
// would have to carry. The editor already holds every one of those, and a
// console's limits are a fixed table, so it can say now what would otherwise
// surface as a failed export or as art that quietly degrades.
//
// Three things this module is deliberately not.
//
// It is not a cap on the library. The menus stay whole: every style, every
// theme, every faction colour is offered to every project, and the breadth of
// the menu never reaches a ROM. What is measured here is one game's helping.
//
// It is not grounds to refuse a project. A game that overruns a console is
// still a valid game which the editor, the browser and the desktop client all
// play, and the console is one export target among several. Nothing here is
// reachable from Save, Export, Play, or the schema; it produces sentences and
// nothing else.
//
// And it is not an estimate. Every colour figure below is measured off the
// drawings themselves by tools/placeholder_art and carried here through
// ../generated/board-art: the colours each drawing spends, and, for the
// machine whose art is re-drawn rather than converted, the palettes that
// profile puts them in. So this file counts and never guesses. Where a number
// could not be measured it is absent rather than invented.

import {
  CHARACTER_PALETTE_ENTRIES,
  MASTER_PALETTE,
  TERRAIN_PALETTE_ENTRIES
} from "../generated/board-art";
import type { SourceProject } from "../generated/source-v1";
import {
  archetypeForClass,
  factionColor,
  projectCharacterFigure,
  projectCharacterStyle,
  projectTheme,
  sideColor,
  terrainSheetKind,
  type FactionColor
} from "./board-art";

/**
 * What one console imposes.
 *
 * Three numbers, because three are what the measurements support. A ROM size
 * is not among them: no ROM size budget is declared anywhere in this tree, and
 * a limit invented here would be a guess wearing a measurement's clothes.
 */
export interface TargetBudget {
  readonly id: string;
  /** What a person calls the machine. */
  readonly label: string;
  /** How many palettes the machine can show at one time. */
  readonly paletteBanks: number;
  /** How many entries one of those palettes holds. */
  readonly bankEntries: number;
  /** Bits the machine stores per colour channel. */
  readonly channelBits: number;
}

/**
 * The consoles with measured limits, and where each number came from.
 *
 * **Nintendo 64.** CI4 is sixteen colours per asset, and the `n64_ci4` profile in
 * `tools/placeholder_art` is built to it and `platform/nintendo64` feeds it to
 * `mksprite` unchanged. TMEM's upper half holds 256 palette entries, which is
 * sixteen CI4 palettes at once (libdragon's own `tmem_size`). RGBA5551 is five
 * bits per channel, and all 124 master entries survive it, measured. Its
 * resident art is not a budget at all: TMEM holds four 32x32 CI4 tiles and the
 * renderer streams past that, so residency is not a limit an authored game can
 * exceed, and the machine's memory is not a measured budget: the whole CI4
 * asset set measures 14% of base RAM.
 */
export const TARGET_BUDGETS: readonly TargetBudget[] = [
  {
    id: "nintendo64",
    label: "Nintendo 64",
    paletteBanks: 16,
    bankEntries: 16,
    channelBits: 5
  }
];

/**
 * One thing a console has to give a whole palette to.
 *
 * The grouping is the console's, not ours: a tile or a sprite draws from one
 * palette, so drawings that appear together and share a look share a palette.
 * The ground is one group because a board's terrain is drawn from one set of
 * sheets; a side is one group because every character wearing a faction's
 * colour is drawn from that faction's ramps.
 *
 * `entries` is what the drawings spend, measured off the native art and the
 * same on every target. `banks` is what the target actually pays for them: as
 * many whole palettes as those colours fill at this machine's bank size.
 */
export interface PaletteGroup {
  /** What the group is, in the words an author reads. */
  readonly label: string;
  /** The records that put it there, named as the delete refusals name them. */
  readonly records: readonly string[];
  /** Distinct master palette entries the group's drawings spend. */
  readonly entries: number;
  /** Whole palettes the group costs this target. */
  readonly banks: number;
}

export interface TargetSpend {
  readonly target: TargetBudget;
  readonly groups: readonly PaletteGroup[];
  /** Palettes the whole game needs at once. */
  readonly banks: number;
  /** Distinct drawings the game's characters resolve to. */
  readonly drawings: number;
  /** Terrain kinds the game's maps name. */
  readonly terrainKinds: number;
  /** Distinct colours the game draws with. */
  readonly colours: number;
  /** How many of those are still distinct at the target's colour depth. */
  readonly coloursAtDepth: number;
  /** Characters behind the drawings, named as the delete refusals name them. */
  readonly characterRecords: readonly string[];
  /** Maps behind the ground, named the same way. */
  readonly mapRecords: readonly string[];
}

/**
 * Something the editor can tell an author about a console, in their words.
 *
 * Not a diagnostic in the validator's sense and deliberately not shaped like
 * one: no severity, no source path, no instance path, nothing to navigate to
 * and nothing that has to be fixed. A note says what a machine would make of
 * the game as it stands.
 */
export interface TargetNote {
  readonly targetId: string;
  readonly code:
    | "TARGET_PALETTES_EXCEEDED"
    | "TARGET_PALETTES_FULL"
    | "TARGET_COLOURS_MERGE";
  readonly message: string;
}

function quantized(entry: number, channelBits: number): string {
  const shift = 8 - channelBits;
  const colour = MASTER_PALETTE[entry];
  if (colour === undefined) return `?${entry}`;
  return `${colour[0] >> shift},${colour[1] >> shift},${colour[2] >> shift}`;
}

/**
 * Names a handful of records and then stops counting out loud.
 *
 * A game with forty maps should not produce a sentence with forty names in it,
 * and an author does not need all forty to know which part of the game a note
 * is about.
 */
function listed(names: readonly string[]): string {
  const shown = names.slice(0, 3);
  const rest = names.length - shown.length;
  const joined = shown.length > 1
    ? `${shown.slice(0, -1).join(", ")} and ${shown[shown.length - 1]}`
    : shown[0] ?? "";
  return rest > 0 ? `${joined} and ${rest} more` : joined;
}

interface CharacterUse {
  readonly colour: FactionColor;
  /**
   * The drawings this colour is spent on, as `<style>_<figure>_<archetype>`:
   * the key `palette_usage.json` is written with, minus the colour this bucket
   * already is.
   *
   * The style and the figure are part of the key rather than the project's two
   * answers because a character may name either of its own. A game that raises
   * one enemy in another setting genuinely puts two rosters on screen, and a
   * budget that priced them both against the project's style would under-count
   * it, which is the one direction a budget may never be wrong in.
   *
   * And they are resolved rather than bounded. Reading the table without the
   * figure would make every row the union of a role's figures and charge a
   * game for a body it never draws; a game names its figure exactly as it
   * names its style, so the budget asks for the drawing and is answered with
   * that drawing's own colours.
   */
  readonly drawings: ReadonlySet<string>;
  readonly records: readonly string[];
}

/**
 * The sprites a game draws, gathered by the colour they are drawn in.
 *
 * A character with no faction is drawn in whichever side's colour it is placed
 * on, and a battle has two sides, so it is counted in both. That is not
 * pessimism: a two-sided game genuinely puts the same character on screen in
 * blue and in red.
 */
function characterUse(project: SourceProject): readonly CharacterUse[] {
  const byColour = new Map<FactionColor, {
    drawings: Set<string>;
    records: Set<string>;
  }>();
  const factions = project.factions ?? [];
  const record = (colour: FactionColor, drawing: string, name: string) => {
    const bucket = byColour.get(colour)
      ?? { drawings: new Set<string>(), records: new Set<string>() };
    bucket.drawings.add(drawing);
    bucket.records.add(name);
    byColour.set(colour, bucket);
  };
  for (const unitType of project.unitTypes) {
    // The character's own style and figure where it names them, and the game's
    // where it does not: the same resolution the compiler and every console
    // make.
    const style = projectCharacterStyle(
      unitType.characterStyleId ?? project.characterStyleId
    );
    const figure = projectCharacterFigure(
      unitType.characterFigureId ?? project.characterFigureId
    );
    const drawing =
      `${style}_${figure}_${archetypeForClass(unitType.classId)}`;
    const chosen = factionColor(factions, unitType.factionId);
    if (chosen === undefined) {
      const unnamed = `${unitType.name} (the character)`;
      record(sideColor("first"), drawing, unnamed);
      record(sideColor("second"), drawing, unnamed);
      continue;
    }
    const faction = factions.find((entry) => entry.id === unitType.factionId)!;
    record(chosen, drawing, `${faction.name} (the faction)`);
  }
  return [...byColour].map(([colour, bucket]) => ({
    colour,
    drawings: bucket.drawings,
    records: [...bucket.records]
  }));
}

/** The ground a game is fought on: every terrain kind any of its maps names. */
function terrainUse(project: SourceProject): {
  kinds: ReadonlySet<string>;
  records: readonly string[];
} {
  const kinds = new Set<string>();
  const records: string[] = [];
  for (const map of project.maps) {
    const before = kinds.size;
    for (const cell of map.terrain) kinds.add(terrainSheetKind(cell));
    if (kinds.size > before) records.push(`${map.name} (the map)`);
  }
  return { kinds, records };
}

/** What one game costs one console, group by group. */
export function targetSpend(
  project: SourceProject,
  target: TargetBudget
): TargetSpend {
  const theme = projectTheme(project.themeId);
  const groups: PaletteGroup[] = [];
  const everything = new Set<number>();

  const ground = terrainUse(project);
  if (ground.kinds.size > 0) {
    const entries = new Set<number>();
    for (const kind of ground.kinds) {
      for (const entry of TERRAIN_PALETTE_ENTRIES[theme]?.[kind] ?? []) {
        entries.add(entry);
      }
    }
    for (const entry of entries) everything.add(entry);
    groups.push({
      label: "the ground",
      records: ground.records,
      entries: entries.size,
      banks: Math.ceil(entries.size / target.bankEntries)
    });
  }

  const characterRecords = new Set<string>();
  let drawings = 0;
  for (const use of characterUse(project)) {
    const entries = new Set<number>();
    for (const drawing of use.drawings) {
      const key = `${drawing}_${use.colour}`;
      const spent = CHARACTER_PALETTE_ENTRIES[key];
      if (spent === undefined) continue;
      drawings += 1;
      for (const entry of spent) entries.add(entry);
    }
    if (entries.size === 0) continue;
    for (const entry of entries) everything.add(entry);
    for (const name of use.records) characterRecords.add(name);
    const label = `the ${use.colour} side`;
    groups.push({
      label,
      records: use.records,
      entries: entries.size,
      banks: Math.ceil(entries.size / target.bankEntries)
    });
  }

  const distinct = new Set<string>();
  for (const entry of everything) {
    distinct.add(quantized(entry, target.channelBits));
  }
  return {
    target,
    groups,
    banks: groups.reduce((total, group) => total + group.banks, 0),
    drawings,
    terrainKinds: ground.kinds.size,
    colours: everything.size,
    coloursAtDepth: distinct.size,
    characterRecords: [...characterRecords],
    mapRecords: ground.records
  };
}

/** Where each palette goes, in one clause per group of art. */
function palettesSpent(spend: TargetSpend): string {
  return spend.groups
    .filter((group) => group.banks > 0)
    .map((group) => {
      const label = group.label.charAt(0).toUpperCase() + group.label.slice(1);
      return `${label} needs ${group.banks}, from ${listed(group.records)}.`;
    })
    .join(" ");
}

/**
 * What a game over the machine's palettes is asked to change.
 *
 * Its own variety, because that is what spends the palettes: a side more than
 * the machine has room for, or maps made of both halves of a theme's ground
 * when one would do. The remedy named is the one the author can act on, and it
 * is offered rather than imposed. The game runs everywhere else exactly as it
 * is.
 */
function paletteSentence(spend: TargetSpend): string {
  const { target } = spend;
  // The ground is only worth offering as a remedy where it costs more than one
  // palette, which is where the game's maps use both halves of a theme. A game
  // already fought on open country alone would be told to change something it
  // has not done.
  const split = spend.groups.some(
    (group) => group.label === "the ground" && group.banks > 1
  );
  return (
    `On a ${target.label} this game needs ${spend.banks} palettes of ` +
    `${target.bankEntries} colours at once, and the machine shows ` +
    `${target.paletteBanks}. ${palettesSpent(spend)} Fewer sides in one game` +
    (split
      ? ", or maps made of open country alone rather than of country and " +
        "water together,"
      : "") +
    ` would fit a ${target.label}. Everywhere else the game runs exactly as ` +
    "it is: the editor, the browser and the desktop client all play it."
  );
}

/**
 * A game that fits, with nothing to spare.
 *
 * Worth saying because of what the machine has left, which is nothing. A side
 * more, or anything drawn over the board in colours of its own, is the palette
 * the machine does not have, and an author who learns that while writing has
 * somewhere to go, where one who learns it at export does not.
 */
function fullSentence(spend: TargetSpend): string {
  const { target } = spend;
  return (
    `On a ${target.label} this game fills all ${target.paletteBanks} palettes ` +
    `of ${target.bankEntries} colours exactly. ${palettesSpent(spend)} It ` +
    "fits, with nothing left over: a third side, or a cursor or a panel drawn " +
    "over the board in colours of its own, would be one palette more than the " +
    "machine has."
  );
}

function mergeSentence(spend: TargetSpend): string {
  const merged = spend.colours - spend.coloursAtDepth;
  return (
    `On a ${spend.target.label} ${merged} of the ${spend.colours} colours ` +
    "this game draws with become the same colour as another one, because the " +
    `machine stores ${spend.target.channelBits} bits of each of red, green ` +
    "and blue. Every shape stays as it is; some shades merge."
  );
}

/**
 * What each console would make of this game, said while it is being written
 * rather than at export.
 *
 * A game inside every measured limit produces nothing at all, and so does an
 * empty project. Nothing here blocks Save, Export or Play, nothing is written
 * to the project, and none of it enters the schema. The console is one export
 * target among several, and a game that overruns one is a game, not a mistake.
 */
export function targetNotes(project: SourceProject): readonly TargetNote[] {
  const notes: TargetNote[] = [];
  for (const target of TARGET_BUDGETS) {
    const spend = targetSpend(project, target);
    if (spend.groups.length === 0) continue;
    const over: TargetNote[] = [];
    if (spend.banks > target.paletteBanks) {
      over.push({
        targetId: target.id,
        code: "TARGET_PALETTES_EXCEEDED",
        message: paletteSentence(spend)
      });
    }
    notes.push(...over);
    // Said instead of the overrun, never beside it, and deliberately not
    // counted as one: the game fits. It is here because of what the machine
    // has left, which is nothing, and an author who hears that while writing
    // has somewhere to go.
    if (spend.banks === target.paletteBanks) {
      notes.push({
        targetId: target.id,
        code: "TARGET_PALETTES_FULL",
        message: fullSentence(spend)
      });
    }
    // Said only beside an overrun, and only when it is true. On its own,
    // colours merging is what a machine of that age does to any picture; it is
    // worth an author's attention when they are already being told this game
    // is more than the machine holds.
    if (over.length > 0 && spend.coloursAtDepth < spend.colours) {
      notes.push({
        targetId: target.id,
        code: "TARGET_COLOURS_MERGE",
        message: mergeSentence(spend)
      });
    }
  }
  return notes;
}
