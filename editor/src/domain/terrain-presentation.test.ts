// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  terrainColor,
  terrainGlyph,
  terrainKind
} from "./terrain-presentation";

describe("terrain presentation", () => {
  it.each([
    ["grass", "grass", "#4d9147"],
    ["river", "water", "#3182a4"],
    ["stone_bridge", "road", "#a88753"],
    ["woods", "forest", "#27613a"],
    ["rock", "mountain", "#75695d"],
    ["desert", "sand", "#d4b86b"],
    ["ice", "snow", "#dcebee"],
    ["marsh", "swamp", "#65733b"]
  ] as const)("maps %s to %s", (source, kind, color) => {
    expect(terrainKind(source)).toBe(kind);
    expect(terrainColor(source)).toBe(color);
  });

  it("gives unknown authored terrain a stable non-black color", () => {
    expect(terrainKind("crystal")).toBe("custom");
    expect(terrainColor("crystal")).toBe(terrainColor("crystal"));
    expect(terrainColor("crystal")).not.toBe("#000000");
  });

  it("reads the terrain the library grew into", () => {
    expect(terrainKind("hills")).toBe("hills");
    expect(terrainKind("highland pass")).toBe("hills");
    expect(terrainKind("ruined keep")).toBe("ruins");
  });

  it("settles an ambiguous name by the generator's match order", () => {
    // Two keywords in one name: the match order decides, and it is not the
    // compositing order.
    expect(terrainKind("mountain road")).toBe("road");
    expect(terrainKind("snowy hills")).toBe("snow");
  });

  it("recolours a cell for the project's season", () => {
    expect(terrainColor("grass", "temperate")).toBe("#4d9147");
    expect(terrainColor("grass", "winter")).toBe("#92a499");
    expect(terrainColor("forest", "autumn")).toBe("#7e3b1b");
    // A theme the menu does not hold falls back to the default rather than
    // leaving the cell colourless.
    expect(terrainColor("grass", "monsoon")).toBe("#4d9147");
    // A theme that does not substitute a terrain's ramp leaves it alone.
    expect(terrainColor("sand", "winter")).toBe(terrainColor("sand"));
  });
});

describe("terrainGlyph", () => {
  it("gives every recognised kind its own mark", () => {
    const marks = [
      "plain",
      "deep water",
      "stone road",
      "dark woods",
      "rocky peak",
      "desert sand",
      "ice field",
      "marshland"
    ].map((terrain) => terrainGlyph(terrain));
    // The requirement is that state is never carried by colour alone, so no
    // two recognised kinds may share a mark.
    expect(new Set(marks).size).toBe(marks.length);
  });

  it("distinguishes custom terrain by its own initial", () => {
    expect(terrainGlyph("lava")).toBe("L");
    expect(terrainGlyph("void")).toBe("V");
  });

  it("falls back rather than rendering nothing", () => {
    expect(terrainGlyph("   ")).toBe("?");
  });
});
