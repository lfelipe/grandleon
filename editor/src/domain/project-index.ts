// SPDX-License-Identifier: MIT
import { normalizeProjectPath, type ProjectPath } from "./project-store";

export type DefinitionCategory =
  | "class"
  | "unit_type"
  | "weapon_type"
  | "item_type"
  | "weapon"
  | "item"
  | "map"
  | "faction"
  | "ability"
  | "objective"
  | "campaign"
  | "dialogue";

export interface DefinitionReference {
  readonly category: DefinitionCategory;
  readonly sourceKey: string;
  readonly semanticPath: string;
}

export interface IndexedDefinition {
  readonly category: DefinitionCategory;
  readonly sourceKey: string;
  readonly displayName: string;
  readonly sourcePath: ProjectPath;
  readonly semanticPath: string;
  readonly schemaVersion: string;
  readonly references: readonly DefinitionReference[];
}

export interface IndexDiagnostic {
  readonly code: "INDEX_DUPLICATE_DEFINITION" | "INDEX_UNRESOLVED_REFERENCE";
  readonly sourcePath: ProjectPath;
  readonly semanticPath: string;
  readonly message: string;
}

function identity(category: DefinitionCategory, sourceKey: string): string {
  return `${category}:${sourceKey}`;
}

function compareDefinitions(left: IndexedDefinition, right: IndexedDefinition) {
  return left.category.localeCompare(right.category) ||
    left.sourceKey.localeCompare(right.sourceKey) ||
    left.sourcePath.localeCompare(right.sourcePath) ||
    left.semanticPath.localeCompare(right.semanticPath);
}

export class ProjectIndex {
  readonly #documents = new Map<ProjectPath, readonly IndexedDefinition[]>();
  readonly #definitions = new Map<string, IndexedDefinition>();
  readonly #duplicates = new Map<string, readonly IndexedDefinition[]>();
  readonly #inbound = new Map<string, readonly IndexedDefinition[]>();
  #diagnostics: readonly IndexDiagnostic[] = [];

  updateDocument(
    candidatePath: ProjectPath,
    definitions: readonly IndexedDefinition[]
  ) {
    const sourcePath = normalizeProjectPath(candidatePath);
    const normalized = definitions.map((definition) => {
      if (normalizeProjectPath(definition.sourcePath) !== sourcePath) {
        throw new Error(
          `definition source '${definition.sourcePath}' does not match document '${sourcePath}'`
        );
      }
      return {
        ...definition,
        references: [...definition.references]
      };
    });
    this.#documents.set(sourcePath, normalized);
    this.#rebuild();
  }

  removeDocument(candidatePath: ProjectPath) {
    this.#documents.delete(normalizeProjectPath(candidatePath));
    this.#rebuild();
  }

  get(
    category: DefinitionCategory,
    sourceKey: string
  ): IndexedDefinition | undefined {
    return this.#definitions.get(identity(category, sourceKey));
  }

  duplicates(
    category: DefinitionCategory,
    sourceKey: string
  ): readonly IndexedDefinition[] {
    return this.#duplicates.get(identity(category, sourceKey)) ?? [];
  }

  inboundReferences(
    category: DefinitionCategory,
    sourceKey: string
  ): readonly IndexedDefinition[] {
    return this.#inbound.get(identity(category, sourceKey)) ?? [];
  }

  diagnostics(): readonly IndexDiagnostic[] {
    return this.#diagnostics;
  }

  definitions(): readonly IndexedDefinition[] {
    return [...this.#definitions.values()].sort(compareDefinitions);
  }

  #rebuild() {
    this.#definitions.clear();
    this.#duplicates.clear();
    this.#inbound.clear();
    const all = [...this.#documents.values()].flat().sort(compareDefinitions);
    const groups = new Map<string, IndexedDefinition[]>();

    for (const definition of all) {
      const key = identity(definition.category, definition.sourceKey);
      const group = groups.get(key) ?? [];
      group.push(definition);
      groups.set(key, group);
      if (!this.#definitions.has(key)) {
        this.#definitions.set(key, definition);
      }
      for (const reference of definition.references) {
        const target = identity(reference.category, reference.sourceKey);
        const inbound = [...(this.#inbound.get(target) ?? []), definition]
          .sort(compareDefinitions);
        this.#inbound.set(target, inbound);
      }
    }

    const diagnostics: IndexDiagnostic[] = [];
    for (const [key, group] of groups) {
      if (group.length > 1) {
        this.#duplicates.set(key, group);
        for (const definition of group) {
          diagnostics.push({
            code: "INDEX_DUPLICATE_DEFINITION",
            sourcePath: definition.sourcePath,
            semanticPath: definition.semanticPath,
            message: `duplicate definition '${key}'`
          });
        }
      }
    }
    for (const definition of all) {
      for (const reference of definition.references) {
        const target = identity(reference.category, reference.sourceKey);
        if (!this.#definitions.has(target)) {
          diagnostics.push({
            code: "INDEX_UNRESOLVED_REFERENCE",
            sourcePath: definition.sourcePath,
            semanticPath: reference.semanticPath,
            message: `unresolved reference '${target}'`
          });
        }
      }
    }
    this.#diagnostics = diagnostics.sort((left, right) =>
      left.sourcePath.localeCompare(right.sourcePath) ||
      left.semanticPath.localeCompare(right.semanticPath) ||
      left.code.localeCompare(right.code)
    );
  }
}
