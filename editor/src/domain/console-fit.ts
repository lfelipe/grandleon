// SPDX-License-Identifier: MIT
import { fitBoard, type BoardFit, type FitRule } from "./board-view";

/**
 * How much of a board each console draws at once, and how to say so to an
 * author.
 *
 * An author sizing a map cannot see either console from here, and the question
 * they are actually asking — "will this fit on the telly, or will it scroll?" —
 * has an exact answer that `fitBoard` already computes. This module supplies
 * the two machines' own numbers and turns the answer into a sentence.
 *
 * **Guidance, never a refusal.** A board that scrolls is a board with edges the
 * player travels to. It is entirely valid, it ships, and every map the Tarnholt
 * Line carries was authored without anybody being asked. Nothing here may
 * disable a control, block a resize, or word itself as a warning.
 *
 * **These numbers are the consoles' own, restated.** They are declared in C++,
 * one per machine, and `console-fit.test.ts` reads those two declarations and
 * requires them to agree with what is here — because an editor that is
 * confidently wrong about what fits is worse than an editor that says nothing.
 */

/**
 * 300 by 200 pixels of a 320x240 frame, a cell never larger than 26, never
 * smaller than 14, and 18 when the board scrolls.
 *
 * Declared at `platform/nintendo64/src/play_rom.cpp`.
 */
export const NINTENDO_64_FIT: FitRule = {
  frameW: 300,
  frameH: 200,
  largestTile: 26,
  smallestTile: 14,
  scrollingTile: 18
};

/**
 * A whole 320 across, 208 once the message bar has taken four eight-pixel rows,
 * a cell never larger than the 32 texels the art is drawn at, and never smaller
 * than half of that.
 *
 * Declared at `platform/client/include/grandleon/client/turn_client.hpp`.
 */
export const PLAYSTATION_FIT: FitRule = {
  frameW: 320,
  frameH: 208,
  largestTile: 32,
  smallestTile: 16,
  scrollingTile: 16
};

/** The largest board a rule draws whole, in cells. */
export interface WholeBoard {
  readonly width: number;
  readonly height: number;
}

/**
 * The largest board a console draws without scrolling, found by asking
 * `fitBoard` rather than by solving it.
 *
 * The closed form is available — a cell is square, so each axis needs
 * `frame / smallestTile` cells — but deriving it here would be a second
 * statement of the rule, and the whole point of this module is that there is
 * one. Walking outwards until the answer changes costs a few dozen integer
 * divisions and cannot disagree with the function it is asking.
 *
 * The two axes are found independently, which is sound because a cell is square
 * and each axis's division stands alone: a board is drawn whole exactly when
 * both are. `console-fit.test.ts` holds that jointly rather than trusting it.
 */
export function largestWholeBoard(rule: FitRule): WholeBoard {
  let width = 1;
  while (!fitBoard(rule, width + 1, 1).scrolling) width += 1;
  let height = 1;
  while (!fitBoard(rule, 1, height + 1).scrolling) height += 1;
  return { width, height };
}

/** What the two consoles make of one board. */
export interface ConsoleFit {
  readonly nintendo64: BoardFit;
  readonly playStation: BoardFit;
  /** True when at least one console has to scroll this board. */
  readonly scrollsAnywhere: boolean;
  /** One sentence, for an author standing on the map. */
  readonly summary: string;
}

function whole(rule: FitRule): string {
  const largest = largestWholeBoard(rule);
  return `${largest.width}×${largest.height}`;
}

/**
 * What each console does with a board this size, and how to say it.
 *
 * Returns `null` for a size that is not a whole board — a half-typed field, an
 * empty one, a fraction — because the honest answer to "what fits in 1.5 cells"
 * is nothing at all, and a guess printed confidently is the failure this module
 * exists to avoid.
 *
 * The sentence is assembled from the two answers rather than written out per
 * case, so it stays true if a console's numbers move. Today the PlayStation's
 * window is the smaller of the two and a board it draws whole is always drawn
 * whole on the cartridge as well, which makes one of the four cases
 * unreachable; it is still written, because that is a fact about today's
 * numbers and not about the rule.
 */
export function consoleFitFor(width: number, height: number): ConsoleFit | null {
  if (!Number.isInteger(width) || !Number.isInteger(height)) return null;
  if (width < 1 || height < 1) return null;

  const nintendo64 = fitBoard(NINTENDO_64_FIT, width, height);
  const playStation = fitBoard(PLAYSTATION_FIT, width, height);
  const scrollsAnywhere = nintendo64.scrolling || playStation.scrolling;

  let summary: string;
  if (!nintendo64.scrolling && !playStation.scrolling) {
    summary = "Drawn whole on both consoles.";
  } else if (!nintendo64.scrolling) {
    summary =
      `Drawn whole on the Nintendo 64. Scrolls on PlayStation, which draws `
      + `up to ${whole(PLAYSTATION_FIT)} whole.`;
  } else if (!playStation.scrolling) {
    summary =
      `Drawn whole on the PlayStation. Scrolls on Nintendo 64, which draws `
      + `up to ${whole(NINTENDO_64_FIT)} whole.`;
  } else {
    summary =
      `Scrolls on both consoles, which draw up to ${whole(NINTENDO_64_FIT)} `
      + `and ${whole(PLAYSTATION_FIT)} whole.`;
  }

  return { nintendo64, playStation, scrollsAnywhere, summary };
}
