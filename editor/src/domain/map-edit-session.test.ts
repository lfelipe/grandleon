// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { SourceMap } from "../generated/source-v1";
import { MapEditError, MapEditSession } from "./map-edit-session";

function fixture(): SourceMap {
  return {
    id: "field",
    name: "Field",
    width: 3,
    height: 2,
    terrain: ["grass", "grass", "water", "grass", "rock", "water"]
  };
}

function expectCode(operation: () => unknown, code: MapEditError["code"]) {
  try {
    operation();
    throw new Error("expected map edit to fail");
  } catch (error) {
    expect(error).toBeInstanceOf(MapEditError);
    expect((error as MapEditError).code).toBe(code);
  }
}

describe("MapEditSession", () => {
  it("paints deduplicated coordinates as one deterministic transaction", () => {
    const session = new MapEditSession(fixture());
    const transaction = session.paint([
      { x: 1, y: 1 },
      { x: 0, y: 0 },
      { x: 1, y: 1 }
    ], "sand");
    expect(transaction.affectedCells).toEqual([
      { x: 0, y: 0 },
      { x: 1, y: 1 }
    ]);
    expect(session.snapshot().terrain).toEqual([
      "sand", "grass", "water", "grass", "sand", "water"
    ]);
    session.undo();
    expect(session.snapshot()).toEqual(fixture());
    session.redo();
    expect(session.snapshot().terrain[0]).toBe("sand");
  });

  it("flood-fills only the connected four-directional region", () => {
    const session = new MapEditSession(fixture());
    session.fill({ x: 0, y: 0 }, "mud");
    expect(session.snapshot().terrain).toEqual([
      "mud", "mud", "water", "mud", "rock", "water"
    ]);
  });

  it("rejects the complete paint transaction when any coordinate is invalid", () => {
    const session = new MapEditSession(fixture());
    expectCode(
      () => session.paint([{ x: 0, y: 0 }, { x: 3, y: 0 }], "sand"),
      "MAP_COORDINATE_INVALID"
    );
    expect(session.snapshot()).toEqual(fixture());
    expect(session.canUndo()).toBe(false);
  });

  it("previews clipping and requires explicit confirmation before resize", () => {
    const session = new MapEditSession(fixture());
    const request = {
      width: 2,
      height: 2,
      offsetX: 0,
      offsetY: 0,
      fillTerrain: "void"
    };
    expect(session.previewResize(request).clippedCells).toEqual([
      { x: 2, y: 0, terrain: "water" },
      { x: 2, y: 1, terrain: "water" }
    ]);
    expectCode(
      () => session.resize(request),
      "MAP_CLIPPING_CONFIRMATION_REQUIRED"
    );
    session.resize(request, true);
    expect(session.snapshot()).toEqual(expect.objectContaining({
      width: 2,
      height: 2,
      terrain: ["grass", "grass", "grass", "rock"]
    }));
    session.undo();
    expect(session.snapshot()).toEqual(fixture());
  });

  it("shifts preserved cells and fills newly exposed space canonically", () => {
    const session = new MapEditSession(fixture());
    session.resize({
      width: 4,
      height: 3,
      offsetX: 1,
      offsetY: 1,
      fillTerrain: "void"
    });
    expect(session.snapshot().terrain).toEqual([
      "void", "void", "void", "void",
      "void", "grass", "grass", "water",
      "void", "grass", "rock", "water"
    ]);
  });

describe("bounded history", () => {
  function paintable(limit?: number) {
    return new MapEditSession(
      {
        id: "field",
        name: "Field",
        width: 4,
        height: 1,
        terrain: ["plain", "plain", "plain", "plain"]
      },
      limit
    );
  }

  it("keeps only the most recent edits", () => {
    const session = paintable(3);
    for (let index = 0; index < 10; index += 1) {
      session.paint([{ x: index % 4, y: 0 }], `terrain_${index}`);
    }
    expect(session.historyDepth()).toBe(3);
    expect(session.discardedEdits()).toBe(7);
  });

  it("drops the oldest edit rather than refusing the newest", () => {
    const session = paintable(2);
    session.paint([{ x: 0, y: 0 }], "grass");
    session.paint([{ x: 1, y: 0 }], "water");
    session.paint([{ x: 2, y: 0 }], "sand");
    // The third edit is applied, not rejected.
    expect(session.snapshot().terrain[2]).toBe("sand");
    // Two undos are available; the first paint is gone for good.
    expect(session.undo()).toBeDefined();
    expect(session.undo()).toBeDefined();
    expect(session.canUndo()).toBe(false);
    expect(session.snapshot().terrain[0]).toBe("grass");
  });

  it("still redoes within the retained window", () => {
    const session = paintable(4);
    session.paint([{ x: 0, y: 0 }], "grass");
    session.paint([{ x: 1, y: 0 }], "water");
    session.undo();
    expect(session.canRedo()).toBe(true);
    session.redo();
    expect(session.snapshot().terrain[1]).toBe("water");
  });

  it("defaults to a limit that survives ordinary painting", () => {
    const session = paintable();
    for (let index = 0; index < 150; index += 1) {
      session.paint([{ x: index % 4, y: 0 }], `terrain_${index}`);
    }
    expect(session.discardedEdits()).toBe(0);
  });
});
});