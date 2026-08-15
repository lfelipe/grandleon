// SPDX-License-Identifier: MIT
import {
  normalizeProjectPath,
  ProjectStoreError,
  type ProjectFile,
  type ProjectPath,
  type ProjectSnapshot,
  type ProjectStore,
  type WriteOptions
} from "./project-store";

interface StoredFile {
  bytes: Uint8Array;
  revision: number;
}

function copyFile(path: string, file: StoredFile): ProjectFile {
  return {
    path,
    bytes: file.bytes.slice(),
    revision: file.revision
  };
}

export class MemoryProjectStore implements ProjectStore {
  readonly #files = new Map<ProjectPath, StoredFile>();
  #revision = 0;

  constructor(readonly projectId: string) {
    if (projectId.length === 0) {
      throw new Error("projectId must not be empty");
    }
  }

  async read(candidate: ProjectPath): Promise<ProjectFile | undefined> {
    const path = normalizeProjectPath(candidate);
    const file = this.#files.get(path);
    return file ? copyFile(path, file) : undefined;
  }

  async write(
    candidate: ProjectPath,
    bytes: Uint8Array,
    options: WriteOptions = {}
  ): Promise<ProjectFile> {
    const path = normalizeProjectPath(candidate);
    const current = this.#files.get(path);
    this.#assertRevision(path, current, options);
    const stored = {
      bytes: bytes.slice(),
      revision: ++this.#revision
    };
    this.#files.set(path, stored);
    return copyFile(path, stored);
  }

  async delete(
    candidate: ProjectPath,
    options: WriteOptions = {}
  ): Promise<void> {
    const path = normalizeProjectPath(candidate);
    const current = this.#files.get(path);
    if (!current) {
      throw new ProjectStoreError("FILE_NOT_FOUND", `project file '${path}' is absent`);
    }
    this.#assertRevision(path, current, options);
    this.#files.delete(path);
    ++this.#revision;
  }

  async list(prefix?: ProjectPath): Promise<readonly ProjectFile[]> {
    const normalizedPrefix = prefix === undefined
      ? undefined
      : normalizeProjectPath(prefix);
    return [...this.#files.entries()]
      .filter(([path]) =>
        normalizedPrefix === undefined ||
        path === normalizedPrefix ||
        path.startsWith(`${normalizedPrefix}/`)
      )
      .sort(([left], [right]) => left.localeCompare(right))
      .map(([path, file]) => copyFile(path, file));
  }

  async snapshot(): Promise<ProjectSnapshot> {
    return {
      revision: this.#revision,
      files: await this.list()
    };
  }

  #assertRevision(
    path: ProjectPath,
    current: StoredFile | undefined,
    options: WriteOptions
  ) {
    if (
      options.expectedRevision !== undefined &&
      options.expectedRevision !== current?.revision
    ) {
      throw new ProjectStoreError(
        "REVISION_CONFLICT",
        `project file '${path}' changed since revision ${options.expectedRevision}`
      );
    }
  }
}
