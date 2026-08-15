// SPDX-License-Identifier: MIT
import type { SourceMap } from "../generated/source-v1";

export interface MapCoordinate {
  readonly x: number;
  readonly y: number;
}

export interface MapResizeRequest {
  readonly width: number;
  readonly height: number;
  readonly offsetX: number;
  readonly offsetY: number;
  readonly fillTerrain: string;
}

export interface MapResizePreview {
  readonly request: MapResizeRequest;
  readonly clippedCells: readonly (MapCoordinate & { readonly terrain: string })[];
}

export interface MapTransaction {
  readonly label: string;
  readonly affectedCells: readonly MapCoordinate[];
}

export class MapEditError extends Error {
  constructor(
    readonly code:
      | "MAP_SHAPE_INVALID"
      | "MAP_COORDINATE_INVALID"
      | "MAP_RESIZE_INVALID"
      | "MAP_CLIPPING_CONFIRMATION_REQUIRED",
    message: string,
    readonly affectedCells: readonly MapCoordinate[] = []
  ) {
    super(message);
    this.name = "MapEditError";
  }
}

interface HistoryEntry {
  readonly transaction: MapTransaction;
  readonly before: SourceMap;
  readonly after: SourceMap;
}

function copy(map: SourceMap): SourceMap {
  return structuredClone(map);
}

function index(map: SourceMap, coordinate: MapCoordinate): number {
  if (
    !Number.isInteger(coordinate.x) ||
    !Number.isInteger(coordinate.y) ||
    coordinate.x < 0 ||
    coordinate.y < 0 ||
    coordinate.x >= map.width ||
    coordinate.y >= map.height
  ) {
    throw new MapEditError(
      "MAP_COORDINATE_INVALID",
      `coordinate (${coordinate.x}, ${coordinate.y}) is outside ` +
      `${map.width}x${map.height}`,
      [coordinate]
    );
  }
  return coordinate.y * map.width + coordinate.x;
}

function compareCoordinates(left: MapCoordinate, right: MapCoordinate): number {
  return left.y - right.y || left.x - right.x;
}

/**
 * How many edits stay undoable.
 *
 * Each entry holds a full before and after copy of the map, so an unbounded
 * history grows without limit while somebody paints. A child dragging a brush
 * across a large map produces one entry per cell, which is exactly the usage
 * pattern that would exhaust it. The oldest entries are dropped rather than
 * refusing the edit, because losing the ability to undo something from
 * hundreds of strokes ago is a far smaller harm than losing the stroke.
 */
export const defaultHistoryLimit = 200;

export class MapEditSession {
  #map: SourceMap;
  readonly #undo: HistoryEntry[] = [];
  readonly #redo: HistoryEntry[] = [];
  readonly #historyLimit: number;
  #discarded = 0;

  constructor(map: SourceMap, historyLimit: number = defaultHistoryLimit) {
    if (map.terrain.length !== map.width * map.height) {
      throw new MapEditError(
        "MAP_SHAPE_INVALID",
        `terrain has ${map.terrain.length} cells; expected ${map.width * map.height}`
      );
    }
    this.#map = copy(map);
    this.#historyLimit = Math.max(1, Math.floor(historyLimit));
  }

  /** Edits that scrolled out of the history and can no longer be undone. */
  discardedEdits(): number {
    return this.#discarded;
  }

  /** The number of edits currently undoable. */
  historyDepth(): number {
    return this.#undo.length;
  }

  snapshot(): SourceMap {
    return copy(this.#map);
  }

  canUndo(): boolean {
    return this.#undo.length > 0;
  }

  canRedo(): boolean {
    return this.#redo.length > 0;
  }

  paint(
    coordinates: readonly MapCoordinate[],
    terrain: string
  ): MapTransaction {
    const unique = new Map<string, MapCoordinate>();
    for (const coordinate of coordinates) {
      index(this.#map, coordinate);
      unique.set(`${coordinate.x}:${coordinate.y}`, { ...coordinate });
    }
    const affectedCells = [...unique.values()].sort(compareCoordinates);
    return this.#commit(
      `Paint ${affectedCells.length} terrain cells`,
      affectedCells,
      (map) => {
        for (const coordinate of affectedCells) {
          map.terrain[index(map, coordinate)] = terrain;
        }
      }
    );
  }

  fill(start: MapCoordinate, terrain: string): MapTransaction {
    const startIndex = index(this.#map, start);
    const replaced = this.#map.terrain[startIndex]!;
    if (replaced === terrain) {
      return { label: "Fill 0 terrain cells", affectedCells: [] };
    }

    const pending = [{ ...start }];
    const visited = new Set<string>();
    const affected: MapCoordinate[] = [];
    while (pending.length > 0) {
      const coordinate = pending.pop()!;
      const key = `${coordinate.x}:${coordinate.y}`;
      if (visited.has(key)) continue;
      visited.add(key);
      if (this.#map.terrain[index(this.#map, coordinate)] !== replaced) continue;
      affected.push(coordinate);
      for (const neighbor of [
        { x: coordinate.x - 1, y: coordinate.y },
        { x: coordinate.x + 1, y: coordinate.y },
        { x: coordinate.x, y: coordinate.y - 1 },
        { x: coordinate.x, y: coordinate.y + 1 }
      ]) {
        if (
          neighbor.x >= 0 &&
          neighbor.y >= 0 &&
          neighbor.x < this.#map.width &&
          neighbor.y < this.#map.height
        ) {
          pending.push(neighbor);
        }
      }
    }
    return this.paint(affected, terrain);
  }

  previewResize(request: MapResizeRequest): MapResizePreview {
    if (
      !Number.isInteger(request.width) ||
      !Number.isInteger(request.height) ||
      request.width < 1 ||
      request.height < 1 ||
      request.width > 4096 ||
      request.height > 4096 ||
      request.width * request.height > 1_048_576 ||
      !Number.isInteger(request.offsetX) ||
      !Number.isInteger(request.offsetY)
    ) {
      throw new MapEditError(
        "MAP_RESIZE_INVALID",
        "resize dimensions, area, and offsets must remain within source limits"
      );
    }

    const clippedCells: (MapCoordinate & { terrain: string })[] = [];
    for (let y = 0; y < this.#map.height; y += 1) {
      for (let x = 0; x < this.#map.width; x += 1) {
        const nextX = x + request.offsetX;
        const nextY = y + request.offsetY;
        if (
          nextX < 0 ||
          nextY < 0 ||
          nextX >= request.width ||
          nextY >= request.height
        ) {
          clippedCells.push({
            x,
            y,
            terrain: this.#map.terrain[y * this.#map.width + x]!
          });
        }
      }
    }
    return { request: { ...request }, clippedCells };
  }

  resize(
    request: MapResizeRequest,
    confirmClipping = false
  ): MapTransaction {
    const preview = this.previewResize(request);
    if (preview.clippedCells.length > 0 && !confirmClipping) {
      throw new MapEditError(
        "MAP_CLIPPING_CONFIRMATION_REQUIRED",
        `resize would clip ${preview.clippedCells.length} terrain cells`,
        preview.clippedCells
      );
    }

    const affectedCells = Array.from(
      { length: request.width * request.height },
      (_, cellIndex) => ({
        x: cellIndex % request.width,
        y: Math.floor(cellIndex / request.width)
      })
    );
    return this.#commit(
      `Resize map to ${request.width}x${request.height}`,
      affectedCells,
      (map) => {
        const previous = copy(map);
        const terrain = Array<string>(
          request.width * request.height
        ).fill(request.fillTerrain);
        for (let y = 0; y < previous.height; y += 1) {
          for (let x = 0; x < previous.width; x += 1) {
            const nextX = x + request.offsetX;
            const nextY = y + request.offsetY;
            if (
              nextX >= 0 &&
              nextY >= 0 &&
              nextX < request.width &&
              nextY < request.height
            ) {
              terrain[nextY * request.width + nextX] =
                previous.terrain[y * previous.width + x]!;
            }
          }
        }
        map.width = request.width;
        map.height = request.height;
        map.terrain = terrain;
      }
    );
  }

  undo(): MapTransaction | undefined {
    const entry = this.#undo.pop();
    if (!entry) return undefined;
    this.#map = copy(entry.before);
    this.#redo.push(entry);
    return entry.transaction;
  }

  redo(): MapTransaction | undefined {
    const entry = this.#redo.pop();
    if (!entry) return undefined;
    this.#map = copy(entry.after);
    this.#undo.push(entry);
    return entry.transaction;
  }

  #commit(
    label: string,
    affectedCells: readonly MapCoordinate[],
    mutate: (map: SourceMap) => void
  ): MapTransaction {
    const before = copy(this.#map);
    const after = copy(this.#map);
    mutate(after);
    const transaction = {
      label,
      affectedCells: affectedCells.map((coordinate) => ({ ...coordinate }))
    };
    this.#map = after;
    this.#undo.push({ transaction, before, after: copy(after) });
    while (this.#undo.length > this.#historyLimit) {
      this.#undo.shift();
      this.#discarded += 1;
    }
    this.#redo.length = 0;
    return transaction;
  }
}
