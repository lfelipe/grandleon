// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import playRom from "../../../platform/nintendo64/src/play_rom.cpp?raw";
import turnClient
  from "../../../platform/client/include/grandleon/client/turn_client.hpp?raw";
import { fitBoard, type FitRule } from "./board-view";
import {
  NINTENDO_64_FIT,
  PLAYSTATION_FIT,
  consoleFitFor,
  largestWholeBoard
} from "./console-fit";

/**
 * Every `view::FitRule` a C++ file declares, in declaration order.
 *
 * The editor cannot call the consoles' own arithmetic, so it restates it, and a
 * restatement nobody checks is a way to tell an author something false with
 * confidence. This reads the two declarations the machines actually compile.
 */
function declaredFitRules(source: string): FitRule[] {
  const pattern =
    /view::FitRule\s+\w+\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}/g;
  return [...source.matchAll(pattern)].map((match) => ({
    frameW: Number(match[1]),
    frameH: Number(match[2]),
    largestTile: Number(match[3]),
    smallestTile: Number(match[4]),
    scrollingTile: Number(match[5])
  }));
}

describe("the numbers each console declares", () => {
  // One declaration per file, or the assertions below could be pinned to
  // whichever the regular expression happened to reach first.
  it("is one rule in the Nintendo 64's renderer, and it is ours", () => {
    const declared = declaredFitRules(playRom);
    expect(declared).toHaveLength(1);
    expect(declared[0]).toEqual(NINTENDO_64_FIT);
  });

  it("is one rule in the shared client, and it is ours", () => {
    const declared = declaredFitRules(turnClient);
    expect(declared).toHaveLength(1);
    expect(declared[0]).toEqual(PLAYSTATION_FIT);
  });
});

describe("fitting a board to a console", () => {
  // The same edges `tests/view/camera_test.cpp` pins, restated here so the two
  // halves of one rule fail together rather than drifting apart quietly.
  it("draws 21x14 whole on the cartridge and scrolls one cell past it", () => {
    expect(fitBoard(NINTENDO_64_FIT, 21, 14).scrolling).toBe(false);
    expect(fitBoard(NINTENDO_64_FIT, 21, 14).tile).toBe(14);
    expect(fitBoard(NINTENDO_64_FIT, 22, 14).scrolling).toBe(true);
    expect(fitBoard(NINTENDO_64_FIT, 21, 15).scrolling).toBe(true);
  });

  it("draws 20x13 whole on the disc, and windows a larger board to it", () => {
    expect(fitBoard(PLAYSTATION_FIT, 20, 13).scrolling).toBe(false);
    expect(fitBoard(PLAYSTATION_FIT, 20, 13).tile).toBe(16);
    const large = fitBoard(PLAYSTATION_FIT, 40, 40);
    expect(large).toEqual({
      tile: 16, viewW: 20, viewH: 13, scrolling: true
    });
  });

  it("lets the tighter axis choose the cell, and caps a small board", () => {
    // 300/10 is 30 and 200/8 is 25, so height decides.
    expect(fitBoard(NINTENDO_64_FIT, 10, 8).tile).toBe(25);
    expect(fitBoard(NINTENDO_64_FIT, 2, 2).tile).toBe(26);
    expect(fitBoard(PLAYSTATION_FIT, 13, 9).tile).toBe(23);
  });

  it("never windows an axis shorter than the board is", () => {
    const fit = fitBoard(NINTENDO_64_FIT, 40, 3);
    expect(fit.viewH).toBe(3);
    expect(fit.viewW).toBe(16);
    expect(fit.scrolling).toBe(true);
  });

  it("treats a board of no size as one capped cell", () => {
    expect(fitBoard(NINTENDO_64_FIT, 0, 0)).toEqual({
      tile: 26, viewW: 1, viewH: 1, scrolling: false
    });
  });
});

describe("the largest board drawn whole", () => {
  it("is 21x14 on the cartridge and 20x13 on the disc", () => {
    expect(largestWholeBoard(NINTENDO_64_FIT)).toEqual({
      width: 21, height: 14
    });
    expect(largestWholeBoard(PLAYSTATION_FIT)).toEqual({
      width: 20, height: 13
    });
  });

  // `largestWholeBoard` finds each axis on its own, and the guidance an author
  // reads treats the pair as a rectangle. Neither is obvious from the rule,
  // whose cell is the smaller of two divisions, so it is held rather than
  // argued: over every board an author would plausibly draw, scrolling is
  // exactly "past one of the two bounds".
  for (const [name, rule] of [
    ["the cartridge", NINTENDO_64_FIT],
    ["the disc", PLAYSTATION_FIT]
  ] as const) {
    it(`bounds ${name} on each axis independently`, () => {
      const largest = largestWholeBoard(rule);
      const disagreements: string[] = [];
      for (let width = 1; width <= 64; width += 1) {
        for (let height = 1; height <= 64; height += 1) {
          const past = width > largest.width || height > largest.height;
          if (fitBoard(rule, width, height).scrolling !== past) {
            disagreements.push(`${width}x${height}`);
          }
        }
      }
      expect(disagreements).toEqual([]);
    });
  }
});

describe("what an author is told", () => {
  it("says a small board is drawn whole on both", () => {
    const fit = consoleFitFor(12, 9);
    expect(fit?.scrollsAnywhere).toBe(false);
    expect(fit?.summary).toBe("Drawn whole on both consoles.");
  });

  it("names the console that scrolls, and what it draws whole", () => {
    // Inside the cartridge's 21x14 and past the disc's 20x13.
    const fit = consoleFitFor(21, 14);
    expect(fit?.nintendo64.scrolling).toBe(false);
    expect(fit?.playStation.scrolling).toBe(true);
    expect(fit?.scrollsAnywhere).toBe(true);
    expect(fit?.summary).toBe(
      "Drawn whole on the Nintendo 64. Scrolls on PlayStation, which draws "
      + "up to 20×13 whole."
    );
  });

  it("says so plainly when both scroll", () => {
    const fit = consoleFitFor(40, 30);
    expect(fit?.summary).toBe(
      "Scrolls on both consoles, which draw up to 21×14 and 20×13 whole."
    );
  });

  // The boundary an author walks across while typing, in both directions.
  it("changes its answer at the disc's last whole column", () => {
    expect(consoleFitFor(20, 13)?.scrollsAnywhere).toBe(false);
    expect(consoleFitFor(21, 13)?.scrollsAnywhere).toBe(true);
    expect(consoleFitFor(20, 14)?.scrollsAnywhere).toBe(true);
  });

  // Guidance about nothing is worse than no guidance: a field mid-edit has no
  // board in it to answer about.
  it("answers nothing about a size that is not a whole board", () => {
    expect(consoleFitFor(Number.NaN, 10)).toBeNull();
    expect(consoleFitFor(10, Number.NaN)).toBeNull();
    expect(consoleFitFor(1.5, 10)).toBeNull();
    expect(consoleFitFor(0, 10)).toBeNull();
    expect(consoleFitFor(-3, 10)).toBeNull();
  });
});
