// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import { diagnosticTarget } from "./diagnostic-target";

describe("diagnostic target", () => {
  it("resolves a field of a record to its section, collection and index", () => {
    expect(diagnosticTarget("/unitTypes/0/classId")).toEqual({
      sectionId: "characters",
      collection: "unitTypes",
      index: 0
    });
    // The index diagnostics use the same shape for paths that go much deeper;
    // everything past the second segment is inside the record being opened.
    expect(diagnosticTarget("/campaigns/2/flow/nodes/0/recruits/0/unitTypeId"))
      .toEqual({ sectionId: "flow", collection: "campaigns", index: 2 });
    expect(diagnosticTarget("/maps/7")).toEqual({
      sectionId: "maps",
      collection: "maps",
      index: 7
    });
  });

  it("takes a problem about the game itself to the game", () => {
    for (const path of ["/title", "/defaultTurnOrder", "", "/"]) {
      const target = diagnosticTarget(path);
      expect(target.sectionId, path).toBe("game");
      expect(target.collection, path).toBeUndefined();
      expect(target.unresolved, path).toContain("about the game itself");
    }
  });

  it("takes a problem about a whole collection as far as the collection", () => {
    const target = diagnosticTarget("/weapons");
    expect(target.sectionId).toBe("equipment");
    expect(target.collection).toBe("weapons");
    expect(target.index).toBeUndefined();
    expect(target.unresolved).toContain("the whole collection");
  });

  it("refuses an index that is not one rather than opening record NaN", () => {
    for (const path of ["/items/all", "/items/-1", "/items/1.5"]) {
      const target = diagnosticTarget(path);
      expect(target.collection, path).toBe("items");
      expect(target.index, path).toBeUndefined();
      expect(target.unresolved, path).toBeDefined();
    }
  });
});
