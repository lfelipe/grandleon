// SPDX-License-Identifier: MIT
import type { SourceCollectionName } from "./source-project-session";
import { sectionOwning, WORKSPACE_SECTIONS } from "./workspace-sections";

/**
 * Where a reported problem lives, as somewhere an author can be taken.
 *
 * Every diagnostic this editor raises carries a JSON-pointer-shaped path into
 * the project document: `/unitTypes/0/classId` from the schema analyzer,
 * `/campaigns/0/flow/nodes/0/recruits/0/unitTypeId` from the index. The first
 * two segments are all a jump needs: a collection, and which record of it.
 * Everything after them is inside the record the author is being taken to.
 *
 * A path this cannot resolve is not a reason to do nothing. It resolves as far
 * as it can and says what it could not find, because a button that promises to
 * take you somewhere and silently does not is worse than no button.
 */
export interface DiagnosticTarget {
  /** The section to open. Always one that exists. */
  readonly sectionId: string;
  /** The collection to select, when the path named one. */
  readonly collection?: SourceCollectionName;
  /** The record's position in that collection, when the path named one. */
  readonly index?: number;
  /** What the path did not resolve to, in the author's words. Absent on a
   *  path that resolved completely. */
  readonly unresolved?: string;
}

const GAME_SECTION = WORKSPACE_SECTIONS[0]!.id;

/** Whether a path segment names a collection this editor edits. */
function collectionNamed(segment: string): SourceCollectionName | undefined {
  const candidate = segment as SourceCollectionName;
  return sectionOwning(candidate) ? candidate : undefined;
}

export function diagnosticTarget(instancePath: string): DiagnosticTarget {
  const segments = instancePath.split("/").filter((segment) => segment !== "");
  const collection = collectionNamed(segments[0] ?? "");
  if (!collection) {
    // A project-level field, the title, the theme or the turn-order default,
    // or a name from a schema this editor does not lay out. Either way the
    // game itself is the closest place there is.
    return {
      sectionId: GAME_SECTION,
      unresolved:
        "This problem is about the game itself rather than about one record."
    };
  }
  const sectionId = sectionOwning(collection)!.id;
  const index = Number(segments[1]);
  if (segments.length < 2 || !Number.isInteger(index) || index < 0) {
    return {
      sectionId,
      collection,
      unresolved: "This problem names the whole collection rather than one record."
    };
  }
  return { sectionId, collection, index };
}
