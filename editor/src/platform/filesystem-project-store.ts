// SPDX-License-Identifier: MIT
import {
  normalizeProjectPath,
  ProjectStoreError,
  type ProjectFile,
  type ProjectPath,
  type ProjectSnapshot,
  type ProjectStore,
  type WriteOptions
} from "../domain/project-store";

export interface ProjectDirectoryPort {
  requestPermission(mode: "read" | "readwrite"): Promise<boolean>;
  read(path: ProjectPath): Promise<Uint8Array | undefined>;
  write(path: ProjectPath, bytes: Uint8Array): Promise<void>;
  delete(path: ProjectPath): Promise<void>;
  list(): Promise<readonly ProjectPath[]>;
}

interface Observation {
  fingerprint: number;
  revision: number;
}

function fingerprint(bytes: Uint8Array): number {
  let value = 2166136261;
  for (const byte of bytes) {
    value ^= byte;
    value = Math.imul(value, 16777619);
  }
  return value >>> 0;
}

export class FileSystemProjectStore implements ProjectStore {
  readonly #observations = new Map<ProjectPath, Observation>();
  readonly #directory: ProjectDirectoryPort;
  #revision = 0;

  constructor(
    readonly projectId: string,
    directory: ProjectDirectoryPort
  ) {
    if (projectId.length === 0) {
      throw new Error("projectId must not be empty");
    }
    this.#directory = directory;
  }

  async read(candidate: ProjectPath): Promise<ProjectFile | undefined> {
    await this.#permission("read");
    const path = normalizeProjectPath(candidate);
    const bytes = await this.#directory.read(path);
    return bytes ? this.#observe(path, bytes) : undefined;
  }

  async write(
    candidate: ProjectPath,
    bytes: Uint8Array,
    options: WriteOptions = {}
  ): Promise<ProjectFile> {
    await this.#permission("readwrite");
    const path = normalizeProjectPath(candidate);
    const currentBytes = await this.#directory.read(path);
    const current = currentBytes ? this.#observe(path, currentBytes) : undefined;
    this.#assertRevision(path, current, options);
    await this.#directory.write(path, bytes.slice());
    const observation = {
      fingerprint: fingerprint(bytes),
      revision: ++this.#revision
    };
    this.#observations.set(path, observation);
    return { path, bytes: bytes.slice(), revision: observation.revision };
  }

  async delete(
    candidate: ProjectPath,
    options: WriteOptions = {}
  ): Promise<void> {
    await this.#permission("readwrite");
    const path = normalizeProjectPath(candidate);
    const bytes = await this.#directory.read(path);
    if (!bytes) {
      throw new ProjectStoreError("FILE_NOT_FOUND", `project file '${path}' is absent`);
    }
    const current = this.#observe(path, bytes);
    this.#assertRevision(path, current, options);
    await this.#directory.delete(path);
    this.#observations.delete(path);
    ++this.#revision;
  }

  async list(prefix?: ProjectPath): Promise<readonly ProjectFile[]> {
    await this.#permission("read");
    const normalizedPrefix = prefix === undefined
      ? undefined
      : normalizeProjectPath(prefix);
    const paths = (await this.#directory.list())
      .map(normalizeProjectPath)
      .filter((path) =>
        normalizedPrefix === undefined ||
        path === normalizedPrefix ||
        path.startsWith(`${normalizedPrefix}/`)
      )
      .sort();
    const files: ProjectFile[] = [];
    for (const path of paths) {
      const bytes = await this.#directory.read(path);
      if (bytes) {
        files.push(this.#observe(path, bytes));
      }
    }
    return files;
  }

  async snapshot(): Promise<ProjectSnapshot> {
    const files = await this.list();
    return { revision: this.#revision, files };
  }

  #observe(path: ProjectPath, bytes: Uint8Array): ProjectFile {
    const nextFingerprint = fingerprint(bytes);
    let observation = this.#observations.get(path);
    if (!observation || observation.fingerprint !== nextFingerprint) {
      observation = {
        fingerprint: nextFingerprint,
        revision: ++this.#revision
      };
      this.#observations.set(path, observation);
    }
    return { path, bytes: bytes.slice(), revision: observation.revision };
  }

  async #permission(mode: "read" | "readwrite") {
    if (!(await this.#directory.requestPermission(mode))) {
      throw new ProjectStoreError(
        "PERMISSION_DENIED",
        `filesystem ${mode} permission was not granted`
      );
    }
  }

  #assertRevision(
    path: ProjectPath,
    current: ProjectFile | undefined,
    options: WriteOptions
  ) {
    if (
      options.expectedRevision !== undefined &&
      options.expectedRevision !== current?.revision
    ) {
      throw new ProjectStoreError(
        "REVISION_CONFLICT",
        `filesystem file '${path}' changed outside the editor`
      );
    }
  }
}

interface PermissionHandle extends FileSystemHandle {
  queryPermission(options: { mode: "read" | "readwrite" }): Promise<PermissionState>;
  requestPermission(options: { mode: "read" | "readwrite" }): Promise<PermissionState>;
}

type IterableDirectoryHandle = FileSystemDirectoryHandle & {
  entries(): AsyncIterableIterator<[string, FileSystemHandle]>;
};

export class BrowserFileSystemDirectoryPort implements ProjectDirectoryPort {
  readonly #root: FileSystemDirectoryHandle;

  constructor(root: FileSystemDirectoryHandle) {
    this.#root = root;
  }

  async requestPermission(mode: "read" | "readwrite"): Promise<boolean> {
    const handle = this.#root as unknown as PermissionHandle;
    const current = await handle.queryPermission({ mode });
    return current === "granted" ||
      (current === "prompt" && await handle.requestPermission({ mode }) === "granted");
  }

  async read(path: ProjectPath): Promise<Uint8Array | undefined> {
    try {
      const [directory, name] = await this.#parent(path, false);
      const handle = await directory.getFileHandle(name);
      return new Uint8Array(await (await handle.getFile()).arrayBuffer());
    } catch (error) {
      if (error instanceof DOMException && error.name === "NotFoundError") {
        return undefined;
      }
      throw error;
    }
  }

  async write(path: ProjectPath, bytes: Uint8Array): Promise<void> {
    const [directory, name] = await this.#parent(path, true);
    const handle = await directory.getFileHandle(name, { create: true });
    const writable = await handle.createWritable();
    await writable.write(bytes as Uint8Array<ArrayBuffer>);
    await writable.close();
  }

  async delete(path: ProjectPath): Promise<void> {
    const [directory, name] = await this.#parent(path, false);
    await directory.removeEntry(name);
  }

  async list(): Promise<readonly ProjectPath[]> {
    const paths: string[] = [];
    const visit = async (directory: FileSystemDirectoryHandle, prefix: string) => {
      for await (const [name, handle] of (directory as IterableDirectoryHandle).entries()) {
        const path = prefix ? `${prefix}/${name}` : name;
        if (handle.kind === "file") {
          paths.push(normalizeProjectPath(path));
        } else {
          await visit(handle as FileSystemDirectoryHandle, path);
        }
      }
    };
    await visit(this.#root, "");
    return paths.sort();
  }

  async #parent(
    candidate: ProjectPath,
    create: boolean
  ): Promise<[FileSystemDirectoryHandle, string]> {
    const segments = normalizeProjectPath(candidate).split("/");
    const name = segments.pop()!;
    let directory = this.#root;
    for (const segment of segments) {
      directory = await directory.getDirectoryHandle(segment, { create });
    }
    return [directory, name];
  }
}
