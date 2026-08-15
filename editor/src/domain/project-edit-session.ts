// SPDX-License-Identifier: MIT
import {
  ProjectIndex,
  type DefinitionCategory,
  type DefinitionReference,
  type IndexedDefinition
} from "./project-index";
import { normalizeProjectPath, type ProjectPath } from "./project-store";

export interface EditableDefinition extends IndexedDefinition {
  readonly references: readonly DefinitionReference[];
}

export interface ProjectDocument {
  readonly path: ProjectPath;
  readonly definitions: readonly EditableDefinition[];
}

export interface EditResult {
  readonly changedDocuments: readonly ProjectPath[];
}

export class ProjectEditError extends Error {
  constructor(
    readonly code:
      | "DEFINITION_NOT_FOUND"
      | "DUPLICATE_DEFINITION"
      | "DELETE_REFERENCED",
    message: string
  ) {
    super(message);
    this.name = "ProjectEditError";
  }
}

type State = ReadonlyMap<ProjectPath, readonly EditableDefinition[]>;

function cloneDefinition(definition: EditableDefinition): EditableDefinition {
  return {
    ...definition,
    references: definition.references.map((reference) => ({ ...reference }))
  };
}

function cloneState(state: State): Map<ProjectPath, readonly EditableDefinition[]> {
  return new Map(
    [...state].map(([path, definitions]) => [
      path,
      definitions.map(cloneDefinition)
    ])
  );
}

function changedPaths(before: State, after: State): readonly ProjectPath[] {
  const paths = new Set([...before.keys(), ...after.keys()]);
  return [...paths]
    .filter((path) =>
      JSON.stringify(before.get(path)) !== JSON.stringify(after.get(path))
    )
    .sort();
}

export class ProjectEditSession {
  #state: Map<ProjectPath, readonly EditableDefinition[]>;
  #index = new ProjectIndex();
  readonly #undo: State[] = [];
  readonly #redo: State[] = [];

  constructor(documents: readonly ProjectDocument[]) {
    this.#state = new Map();
    for (const document of documents) {
      const path = normalizeProjectPath(document.path);
      if (this.#state.has(path)) {
        throw new Error(`duplicate project document '${path}'`);
      }
      this.#state.set(path, document.definitions.map(cloneDefinition));
    }
    this.#reindex();
  }

  get index(): ProjectIndex {
    return this.#index;
  }

  documents(): readonly ProjectDocument[] {
    return [...this.#state]
      .sort(([left], [right]) => left.localeCompare(right))
      .map(([path, definitions]) => ({
        path,
        definitions: definitions.map(cloneDefinition)
      }));
  }

  canUndo(): boolean {
    return this.#undo.length > 0;
  }

  canRedo(): boolean {
    return this.#redo.length > 0;
  }

  rename(
    category: DefinitionCategory,
    sourceKey: string,
    nextSourceKey: string
  ): EditResult {
    const definition = this.#requireUnique(category, sourceKey);
    if (this.#index.get(category, nextSourceKey)) {
      throw new ProjectEditError(
        "DUPLICATE_DEFINITION",
        `cannot rename '${category}:${sourceKey}' to existing '${category}:${nextSourceKey}'`
      );
    }

    const next = cloneState(this.#state);
    for (const [path, definitions] of next) {
      next.set(path, definitions.map((candidate) => ({
        ...candidate,
        sourceKey:
          candidate.category === category && candidate.sourceKey === sourceKey
            ? nextSourceKey
            : candidate.sourceKey,
        references: candidate.references.map((reference) => ({
          ...reference,
          sourceKey:
            reference.category === category && reference.sourceKey === sourceKey
              ? nextSourceKey
              : reference.sourceKey
        }))
      })));
    }
    return this.#commit(next, definition.sourcePath);
  }

  delete(category: DefinitionCategory, sourceKey: string): EditResult {
    const definition = this.#requireUnique(category, sourceKey);
    const inbound = this.#index.inboundReferences(category, sourceKey);
    if (inbound.length > 0) {
      throw new ProjectEditError(
        "DELETE_REFERENCED",
        `cannot delete '${category}:${sourceKey}'; referenced by ${inbound
          .map((item) => `${item.sourcePath}${item.semanticPath}`)
          .join(", ")}`
      );
    }

    const next = cloneState(this.#state);
    const definitions = next.get(definition.sourcePath) ?? [];
    next.set(
      definition.sourcePath,
      definitions.filter((candidate) =>
        candidate.category !== category || candidate.sourceKey !== sourceKey
      )
    );
    return this.#commit(next, definition.sourcePath);
  }

  undo(): EditResult | undefined {
    const previous = this.#undo.pop();
    if (!previous) return undefined;
    const current = cloneState(this.#state);
    this.#redo.push(current);
    const changedDocuments = changedPaths(current, previous);
    this.#state = cloneState(previous);
    this.#reindex();
    return { changedDocuments };
  }

  redo(): EditResult | undefined {
    const next = this.#redo.pop();
    if (!next) return undefined;
    const current = cloneState(this.#state);
    this.#undo.push(current);
    const changedDocuments = changedPaths(current, next);
    this.#state = cloneState(next);
    this.#reindex();
    return { changedDocuments };
  }

  #requireUnique(
    category: DefinitionCategory,
    sourceKey: string
  ): IndexedDefinition {
    const definition = this.#index.get(category, sourceKey);
    if (!definition) {
      throw new ProjectEditError(
        "DEFINITION_NOT_FOUND",
        `definition '${category}:${sourceKey}' was not found`
      );
    }
    if (this.#index.duplicates(category, sourceKey).length > 0) {
      throw new ProjectEditError(
        "DUPLICATE_DEFINITION",
        `definition '${category}:${sourceKey}' is ambiguous`
      );
    }
    return definition;
  }

  #commit(next: Map<ProjectPath, readonly EditableDefinition[]>, primary: ProjectPath) {
    const changedDocuments = changedPaths(this.#state, next);
    if (changedDocuments.length === 0) return { changedDocuments };
    this.#undo.push(cloneState(this.#state));
    this.#redo.length = 0;
    this.#state = next;
    this.#reindex();
    return {
      changedDocuments: changedDocuments.includes(primary)
        ? [primary, ...changedDocuments.filter((path) => path !== primary)]
        : changedDocuments
    };
  }

  #reindex() {
    this.#index = new ProjectIndex();
    for (const [path, definitions] of this.#state) {
      this.#index.updateDocument(path, definitions);
    }
  }
}
