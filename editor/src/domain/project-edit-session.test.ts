// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  ProjectEditError,
  ProjectEditSession,
  type EditableDefinition,
  type ProjectDocument
} from "./project-edit-session";

function definition(
  sourcePath: string,
  category: EditableDefinition["category"],
  sourceKey: string,
  references: EditableDefinition["references"] = []
): EditableDefinition {
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

function fixture(): readonly ProjectDocument[] {
  return [
    {
      path: "classes.json",
      definitions: [definition("classes.json", "class", "vanguard")]
    },
    {
      path: "units.json",
      definitions: [
        definition("units.json", "unit_type", "soldier", [{
          category: "class",
          sourceKey: "vanguard",
          semanticPath: "/unit_type/soldier/classId"
        }])
      ]
    }
  ];
}

describe("ProjectEditSession", () => {
  function expectEditError(operation: () => unknown, code: ProjectEditError["code"]) {
    try {
      operation();
      throw new Error("expected project edit to fail");
    } catch (error) {
      expect(error).toBeInstanceOf(ProjectEditError);
      expect((error as ProjectEditError).code).toBe(code);
    }
  }

  it("renames a definition and every typed cross-file reference atomically", () => {
    const session = new ProjectEditSession(fixture());

    expect(session.rename("class", "vanguard", "guardian").changedDocuments)
      .toEqual(["classes.json", "units.json"]);
    expect(session.index.get("class", "vanguard")).toBeUndefined();
    expect(session.index.get("class", "guardian")).toBeDefined();
    expect(session.index.inboundReferences("class", "guardian")
      .map((item) => item.sourceKey)).toEqual(["soldier"]);
    expect(session.index.diagnostics()).toEqual([]);
  });

  it("undoes and redoes the complete multi-document rename", () => {
    const session = new ProjectEditSession(fixture());
    session.rename("class", "vanguard", "guardian");

    expect(session.undo()?.changedDocuments).toEqual([
      "classes.json",
      "units.json"
    ]);
    expect(session.index.get("class", "vanguard")).toBeDefined();
    expect(session.index.get("class", "guardian")).toBeUndefined();
    expect(session.redo()?.changedDocuments).toEqual([
      "classes.json",
      "units.json"
    ]);
    expect(session.index.get("class", "guardian")).toBeDefined();
  });

  it("guards referenced deletion without changing state or history", () => {
    const session = new ProjectEditSession(fixture());

    expectEditError(
      () => session.delete("class", "vanguard"),
      "DELETE_REFERENCED"
    );
    expect(session.index.get("class", "vanguard")).toBeDefined();
    expect(session.canUndo()).toBe(false);
  });

  it("deletes an unreferenced definition and restores it through undo", () => {
    const documents = fixture().map((document) => ({
      ...document,
      definitions: [...document.definitions]
    }));
    documents[0]!.definitions.push(
      definition("classes.json", "class", "ranger")
    );
    const session = new ProjectEditSession(documents);

    expect(session.delete("class", "ranger").changedDocuments)
      .toEqual(["classes.json"]);
    expect(session.index.get("class", "ranger")).toBeUndefined();
    session.undo();
    expect(session.index.get("class", "ranger")).toBeDefined();
  });

  it("rejects ambiguous or colliding renames without partial edits", () => {
    const session = new ProjectEditSession([
      ...fixture(),
      {
        path: "more-classes.json",
        definitions: [definition("more-classes.json", "class", "guardian")]
      }
    ]);

    expectEditError(
      () => session.rename("class", "vanguard", "guardian"),
      "DUPLICATE_DEFINITION"
    );
    expect(session.index.get("class", "vanguard")).toBeDefined();
    expect(session.index.inboundReferences("class", "vanguard")).toHaveLength(1);
  });
});
