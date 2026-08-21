// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  ProjectIndex,
  type IndexedDefinition
} from "./project-index";

function definition(
  sourcePath: string,
  category: IndexedDefinition["category"],
  sourceKey: string,
  references: IndexedDefinition["references"] = []
): IndexedDefinition {
  return {
    category,
    sourceKey,
    displayName: sourceKey,
    sourcePath,
    semanticPath: `/${category}/${sourceKey}`,
    schemaVersion: "1.2.0",
    references
  };
}

describe("ProjectIndex", () => {
  it("indexes definitions and navigable inbound references", () => {
    const index = new ProjectIndex();
    index.updateDocument("classes.json", [
      definition("classes.json", "class", "vanguard")
    ]);
    index.updateDocument("units.json", [
      definition("units.json", "unit_type", "soldier", [{
        category: "class",
        sourceKey: "vanguard",
        semanticPath: "/unit_type/soldier/classId"
      }])
    ]);

    expect(index.get("class", "vanguard")?.sourcePath).toBe("classes.json");
    expect(index.inboundReferences("class", "vanguard").map(
      (item) => item.sourceKey
    )).toEqual(["soldier"]);
    expect(index.diagnostics()).toEqual([]);
  });

  it("updates one document and refreshes dependent diagnostics", () => {
    const index = new ProjectIndex();
    index.updateDocument("classes.json", [
      definition("classes.json", "class", "vanguard")
    ]);
    index.updateDocument("units.json", [
      definition("units.json", "unit_type", "soldier", [{
        category: "class",
        sourceKey: "vanguard",
        semanticPath: "/unit_type/soldier/classId"
      }])
    ]);

    index.updateDocument("classes.json", [
      definition("classes.json", "class", "guardian")
    ]);
    expect(index.get("class", "guardian")).toBeDefined();
    expect(index.get("class", "vanguard")).toBeUndefined();
    expect(index.diagnostics()).toEqual([
      expect.objectContaining({
        code: "INDEX_UNRESOLVED_REFERENCE",
        sourcePath: "units.json",
        semanticPath: "/unit_type/soldier/classId"
      })
    ]);
  });

  it("diagnoses duplicates deterministically without merging categories", () => {
    const index = new ProjectIndex();
    index.updateDocument("first.json", [
      definition("first.json", "class", "shared"),
      definition("first.json", "item", "shared")
    ]);
    index.updateDocument("second.json", [
      definition("second.json", "class", "shared")
    ]);

    expect(index.duplicates("class", "shared")).toHaveLength(2);
    expect(index.duplicates("item", "shared")).toHaveLength(0);
    expect(index.diagnostics().map((item) => item.code)).toEqual([
      "INDEX_DUPLICATE_DEFINITION",
      "INDEX_DUPLICATE_DEFINITION"
    ]);
  });
});
