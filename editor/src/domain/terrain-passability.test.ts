// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import { terrainMovementCost, terrainPassability } from "./terrain-passability";

describe("what an authored terrain name asks of a character", () => {
  it("reads water and mountain as the two things ground can refuse", () => {
    expect(terrainPassability("water")).toBe("water");
    expect(terrainPassability("river")).toBe("water");
    expect(terrainPassability("mountain")).toBe("heights");
    expect(terrainPassability("rock")).toBe("heights");
  });

  it("resolves a name by the same keyword convention the art uses", () => {
    // The convention matches on words inside the name, and settles ambiguity
    // in the art library's own order, so "mountain road" is a road here too.
    expect(terrainPassability("The Cold River")).toBe("water");
    expect(terrainPassability("mountain road")).toBe("open");
  });

  it("leaves everything else open, including ground it does not recognise", () => {
    for (const name of ["grass", "road", "bridge", "forest", "swamp", "hills", "sand", "snow", "ruins",
                        "farm", "field", "bamboo", "paved", "cobble"]) {
      expect(terrainPassability(name)).toBe("open");
    }
    // A game may name its own ground. Nothing about an unrecognised name says
    // it should become a wall.
    expect(terrainPassability("Glass Meadow")).toBe("open");
  });
});

describe("what an authored terrain name charges a character", () => {
  it("charges two for ground a character has to get through", () => {
    for (const name of ["forest", "wood", "swamp", "marsh", "hills",
                        "highland", "ruins", "rubble", "bamboo", "thicket",
                        "sand", "desert"]) {
      expect(terrainMovementCost(name)).toBe(2);
    }
  });

  it("charges one for ground a character walks on", () => {
    for (const name of ["road", "bridge", "paved", "cobble", "grass", "plain",
                        "farm", "field", "snow", "ice", "water", "river",
                        "mountain", "rock"]) {
      expect(terrainMovementCost(name)).toBe(1);
    }
    // A game may name its own ground, and nothing about an unrecognised name
    // says it should acquire a tax any more than it should acquire a wall.
    expect(terrainMovementCost("Glass Meadow")).toBe(1);
  });

  it("resolves price by the keyword convention passability uses", () => {
    // One name, one kind, two independent answers read off it: a mountain road
    // is a road for both, and a swamp takes anyone while charging them double.
    expect(terrainMovementCost("mountain road")).toBe(1);
    expect(terrainPassability("the Reeking Marsh")).toBe("open");
    expect(terrainMovementCost("the Reeking Marsh")).toBe(2);
  });
});
