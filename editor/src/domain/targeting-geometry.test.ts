// SPDX-License-Identifier: MIT
// What the geometry module does with shapes, and, the part worth reading,
// what it refuses. `targeting-agreement.test.ts` is what holds the covering
// rules to the engine; this file covers the inverse direction, reading a
// painted set of tiles back into stored fields, which the engine has no
// opinion about because no engine code ever runs backwards.
//
// Every refusal below is a shape an author can draw and the current
// vocabulary cannot store. They are recorded here as cases rather than
// described in prose so that the day a shape becomes expressible, the test
// naming it fails and has to be rewritten deliberately.

import { describe, expect, it } from "vitest";
import {
  areaOffsets,
  areaRadius,
  bandCovers,
  bandHole,
  bandOffsets,
  describeArea,
  describeBand,
  manhattan,
  offsetsWithin,
  readArea,
  readBand,
  type Offset
} from "./targeting-geometry";

const at = (dx: number, dy: number): Offset => ({ dx, dy });
const extent = 6;

/** Every offset within `extent` satisfying a predicate: a painted shape. */
function paint(covers: (offset: Offset) => boolean): Offset[] {
  return offsetsWithin(extent).filter(covers);
}

describe("the Manhattan measure", () => {
  it("sums the absolute differences rather than taking the larger", () => {
    expect(manhattan(2, 2)).toBe(4);
    expect(manhattan(-3, 0)).toBe(3);
    expect(manhattan(0, 0)).toBe(0);
  });
});

describe("an area of impact", () => {
  it("gives single and cross fixed radii and reads the radius only for a diamond", () => {
    expect(areaRadius("single", 4)).toBe(0);
    expect(areaRadius("cross", 4)).toBe(1);
    expect(areaRadius("diamond", 4)).toBe(4);
    expect(areaRadius("diamond", undefined)).toBe(0);
  });

  it("covers the tile aimed at and grows a ring per step of radius", () => {
    expect(areaOffsets("single", 0, extent)).toHaveLength(1);
    expect(areaOffsets("cross", 0, extent)).toHaveLength(5);
    expect(areaOffsets("diamond", 2, extent)).toHaveLength(13);
    expect(areaOffsets("diamond", 3, extent)).toHaveLength(25);
  });

  it("is drawn row-major from the north-west corner", () => {
    expect(areaOffsets("cross", 0, 1)).toEqual([
      at(0, -1), at(-1, 0), at(0, 0), at(1, 0), at(0, 1)
    ]);
  });
});

describe("a reach band", () => {
  it("admits the distances between its minimum and its maximum", () => {
    expect(bandCovers(2, 4, at(0, 3))).toBe(true);
    expect(bandCovers(2, 4, at(1, 1))).toBe(true);
    expect(bandCovers(2, 4, at(0, 5))).toBe(false);
  });

  it("excludes the stance and everything inside the minimum reach", () => {
    expect(bandCovers(2, 4, at(0, 0))).toBe(false);
    expect(bandCovers(2, 4, at(1, 0))).toBe(false);
    expect(bandHole(2, at(1, 0))).toBe(true);
    expect(bandHole(2, at(0, 0))).toBe(true);
    // Beyond the maximum is excluded too, but it is not the hole: the two
    // read identically on a grid unless they are drawn apart.
    expect(bandHole(2, at(0, 5))).toBe(false);
  });

  it("admits nothing when the minimum is past the maximum", () => {
    expect(bandOffsets(4, 2, extent)).toEqual([]);
  });

  it("admits a single ring when the minimum equals the maximum", () => {
    expect(bandOffsets(3, 3, extent)).toHaveLength(12);
  });
});

describe("reading a painted area back into fields", () => {
  it("names the shape for each expressible radius", () => {
    for (const [radius, areaShape] of [
      [0, "single"], [1, "cross"], [2, "diamond"], [5, "diamond"]
    ] as const) {
      const reading = readArea(paint((offset) =>
        manhattan(offset.dx, offset.dy) <= radius), extent);
      expect(reading).toEqual({
        expressible: true,
        fields: { areaShape, radius }
      });
    }
  });

  it("refuses a square blast", () => {
    // Chebyshev: every tile within one step including the diagonals. The most
    // commonly wanted shape the vocabulary has no name for.
    const reading = readArea(
      paint((offset) => Math.max(Math.abs(offset.dx), Math.abs(offset.dy)) <= 1),
      extent
    );
    expect(reading.expressible).toBe(false);
    expect(reading).toHaveProperty("reason");
  });

  it("refuses a straight line", () => {
    const reading = readArea(
      paint((offset) => offset.dy === 0 && Math.abs(offset.dx) <= 3),
      extent
    );
    expect(reading.expressible).toBe(false);
  });

  it("refuses a hollow ring, naming the tile aimed at", () => {
    const reading = readArea(
      paint((offset) => manhattan(offset.dx, offset.dy) === 2),
      extent
    );
    expect(reading.expressible).toBe(false);
    if (!reading.expressible) expect(reading.reason).toContain("ring");
  });

  it("refuses a cone", () => {
    const reading = readArea(
      paint((offset) => offset.dy >= 0 && Math.abs(offset.dx) <= offset.dy &&
        offset.dy <= 3),
      extent
    );
    expect(reading.expressible).toBe(false);
  });

  it("refuses an empty painting rather than storing an area covering nothing", () => {
    const reading = readArea([], extent);
    expect(reading.expressible).toBe(false);
  });
});

describe("reading a painted band back into fields", () => {
  it("names the minimum and maximum for each unbroken run of distances", () => {
    for (const [minimumRange, maximumRange] of [
      [1, 1], [1, 3], [2, 2], [3, 5]
    ] as const) {
      const reading = readBand(
        paint((offset) => bandCovers(minimumRange, maximumRange, offset)),
        extent
      );
      expect(reading).toEqual({
        expressible: true,
        fields: { minimumRange, maximumRange }
      });
    }
  });

  it("refuses a band with a gap in the middle", () => {
    const reading = readBand(
      paint((offset) => {
        const separation = manhattan(offset.dx, offset.dy);
        return separation === 1 || separation === 3;
      }),
      extent
    );
    expect(reading.expressible).toBe(false);
    if (!reading.expressible) expect(reading.reason).toContain("unbroken");
  });

  it("refuses a band that includes the character's own tile", () => {
    const reading = readBand(
      paint((offset) => manhattan(offset.dx, offset.dy) <= 2),
      extent
    );
    expect(reading.expressible).toBe(false);
  });

  it("refuses a beam down one axis", () => {
    const reading = readBand(
      paint((offset) => offset.dy === 0 && offset.dx > 0 && offset.dx <= 4),
      extent
    );
    expect(reading.expressible).toBe(false);
  });

  it("refuses an empty painting", () => {
    expect(readBand([], extent).expressible).toBe(false);
  });
});

describe("the sentences shown beside the grid", () => {
  it("says a band without a hole plainly", () => {
    expect(describeBand(1, 3)).toBe("Strikes from 1 tile out to 3 tiles away.");
  });

  it("says the hole when the minimum reach is above one", () => {
    expect(describeBand(2, 4)).toContain("cannot strike anything closer than 2 tiles");
  });

  it("says an exact distance once rather than as a range", () => {
    expect(describeBand(3, 3)).toBe("Strikes at exactly 3 tiles away.");
  });

  it("says a band that admits nothing rather than drawing an empty grid silently", () => {
    expect(describeBand(4, 2)).toContain("admits nothing");
  });

  it("says what an area covers and counts its tiles", () => {
    expect(describeArea("single", 0)).toBe("Covers only the tile aimed at.");
    expect(describeArea("cross", 0)).toContain("5 tiles");
    expect(describeArea("diamond", 2)).toContain("13 tiles");
  });

  it("never judges what it describes", () => {
    // The editor's descriptive-not-evaluative rule: a summary says what a
    // value does, never whether it is a good one.
    const forbidden = [
      "strong", "weak", "best", "powerful", "overpowered", "balanced"
    ];
    const sentences = [
      describeBand(1, 1), describeBand(2, 5), describeBand(4, 2),
      describeArea("single", 0), describeArea("cross", 0),
      describeArea("diamond", 3)
    ];
    for (const sentence of sentences) {
      for (const word of forbidden) {
        expect(sentence.toLocaleLowerCase()).not.toContain(word);
      }
    }
  });
});
