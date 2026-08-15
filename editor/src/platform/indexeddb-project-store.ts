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

const databaseVersion = 2;
const filesStore = "files";
const metadataStore = "metadata";
const revisionKey = "project-revision";

interface StoredFile {
  path: string;
  bytes: Uint8Array;
  revision: number;
}

function requestResult<T>(request: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    request.addEventListener("success", () => resolve(request.result));
    request.addEventListener("error", () =>
      reject(request.error ?? new Error("IndexedDB request failed"))
    );
  });
}

function transactionComplete(transaction: IDBTransaction): Promise<void> {
  return new Promise((resolve, reject) => {
    transaction.addEventListener("complete", () => resolve());
    transaction.addEventListener("abort", () =>
      reject(transaction.error ?? new Error("IndexedDB transaction aborted"))
    );
    transaction.addEventListener("error", () =>
      reject(transaction.error ?? new Error("IndexedDB transaction failed"))
    );
  });
}

function abortIfActive(transaction: IDBTransaction) {
  try {
    transaction.abort();
  } catch (error) {
    if (!(error instanceof DOMException) || error.name !== "InvalidStateError") {
      throw error;
    }
  }
}

function publicFile(file: StoredFile): ProjectFile {
  return {
    path: file.path,
    bytes: file.bytes.slice(),
    revision: file.revision
  };
}

export interface IndexedDbProjectStoreOptions {
  readonly indexedDB?: IDBFactory;
  readonly maximumBytes?: number;
}

export class IndexedDbProjectStore implements ProjectStore {
  readonly #factory: IDBFactory;
  readonly #databaseName: string;
  readonly #maximumBytes: number;
  #database: Promise<IDBDatabase> | undefined;

  constructor(
    readonly projectId: string,
    options: IndexedDbProjectStoreOptions = {}
  ) {
    if (projectId.length === 0) {
      throw new Error("projectId must not be empty");
    }
    this.#factory = options.indexedDB ?? globalThis.indexedDB;
    this.#databaseName = `grandleon-editor:${projectId}`;
    this.#maximumBytes = options.maximumBytes ?? 32 * 1024 * 1024;
  }

  async read(candidate: ProjectPath): Promise<ProjectFile | undefined> {
    const path = normalizeProjectPath(candidate);
    const database = await this.#open();
    const transaction = database.transaction(filesStore, "readonly");
    const result = await requestResult<StoredFile | undefined>(
      transaction.objectStore(filesStore).get(path)
    );
    await transactionComplete(transaction);
    return result ? publicFile(result) : undefined;
  }

  async write(
    candidate: ProjectPath,
    bytes: Uint8Array,
    options: WriteOptions = {}
  ): Promise<ProjectFile> {
    const path = normalizeProjectPath(candidate);
    const database = await this.#open();
    const transaction = database.transaction(
      [filesStore, metadataStore],
      "readwrite"
    );
    const done = transactionComplete(transaction);
    try {
      const files = transaction.objectStore(filesStore);
      const metadata = transaction.objectStore(metadataStore);
      const current = await requestResult<StoredFile | undefined>(files.get(path));
      this.#assertRevision(path, current, options);

      const all = await requestResult<StoredFile[]>(files.getAll());
      const usedBytes = all.reduce((sum, file) => sum + file.bytes.byteLength, 0);
      const candidateBytes = usedBytes - (current?.bytes.byteLength ?? 0) + bytes.byteLength;
      if (candidateBytes > this.#maximumBytes) {
        transaction.abort();
        await done.catch(() => {});
        throw new ProjectStoreError(
          "QUOTA_EXCEEDED",
          `project draft would use ${candidateBytes} bytes; limit is ${this.#maximumBytes}`
        );
      }

      const revision = (await requestResult<number | undefined>(
        metadata.get(revisionKey)
      ) ?? 0) + 1;
      const stored: StoredFile = { path, bytes: bytes.slice(), revision };
      files.put(stored);
      metadata.put(revision, revisionKey);
      await done;
      return publicFile(stored);
    } catch (error) {
      abortIfActive(transaction);
      await done.catch(() => {});
      throw this.#storageError(error);
    }
  }

  async delete(
    candidate: ProjectPath,
    options: WriteOptions = {}
  ): Promise<void> {
    const path = normalizeProjectPath(candidate);
    const database = await this.#open();
    const transaction = database.transaction(
      [filesStore, metadataStore],
      "readwrite"
    );
    const done = transactionComplete(transaction);
    try {
      const files = transaction.objectStore(filesStore);
      const metadata = transaction.objectStore(metadataStore);
      const current = await requestResult<StoredFile | undefined>(files.get(path));
      if (!current) {
        throw new ProjectStoreError(
          "FILE_NOT_FOUND",
          `project file '${path}' is absent`
        );
      }
      this.#assertRevision(path, current, options);
      const revision = (await requestResult<number | undefined>(
        metadata.get(revisionKey)
      ) ?? 0) + 1;
      files.delete(path);
      metadata.put(revision, revisionKey);
      await done;
    } catch (error) {
      abortIfActive(transaction);
      await done.catch(() => {});
      throw this.#storageError(error);
    }
  }

  async list(prefix?: ProjectPath): Promise<readonly ProjectFile[]> {
    const normalizedPrefix = prefix === undefined
      ? undefined
      : normalizeProjectPath(prefix);
    const database = await this.#open();
    const transaction = database.transaction(filesStore, "readonly");
    const files = await requestResult<StoredFile[]>(
      transaction.objectStore(filesStore).getAll()
    );
    await transactionComplete(transaction);
    return files
      .filter((file) =>
        normalizedPrefix === undefined ||
        file.path === normalizedPrefix ||
        file.path.startsWith(`${normalizedPrefix}/`)
      )
      .sort((left, right) => left.path.localeCompare(right.path))
      .map(publicFile);
  }

  async snapshot(): Promise<ProjectSnapshot> {
    const database = await this.#open();
    const transaction = database.transaction(
      [filesStore, metadataStore],
      "readonly"
    );
    const files = await requestResult<StoredFile[]>(
      transaction.objectStore(filesStore).getAll()
    );
    const revision = await requestResult<number | undefined>(
      transaction.objectStore(metadataStore).get(revisionKey)
    );
    await transactionComplete(transaction);
    return {
      revision: revision ?? 0,
      files: files.sort((left, right) => left.path.localeCompare(right.path))
        .map(publicFile)
    };
  }

  async close(): Promise<void> {
    if (this.#database) {
      (await this.#database).close();
      this.#database = undefined;
    }
  }

  #open(): Promise<IDBDatabase> {
    this.#database ??= new Promise((resolve, reject) => {
      const request = this.#factory.open(this.#databaseName, databaseVersion);
      request.addEventListener("upgradeneeded", () => {
        const database = request.result;
        if (!database.objectStoreNames.contains(filesStore)) {
          database.createObjectStore(filesStore, { keyPath: "path" });
        }
        if (!database.objectStoreNames.contains(metadataStore)) {
          database.createObjectStore(metadataStore);
        }
      });
      request.addEventListener("success", () => {
        const database = request.result;
        // A second tab running a newer editor build cannot upgrade the draft
        // database while this connection is open; without this handler it
        // waits forever and both tabs appear to hang. Step aside instead:
        // the next operation opens again, at whatever version now exists.
        database.addEventListener("versionchange", () => {
          database.close();
          this.#database = undefined;
        });
        resolve(database);
      });
      request.addEventListener("error", () => {
        // The other tab won: the draft database is now at a version this
        // build does not know. Say so in words, because the platform's own
        // VersionError reads as a bug in the editor rather than as two tabs
        // on two builds.
        if (request.error?.name === "VersionError") {
          reject(new ProjectStoreError(
            "STORAGE_FAILURE",
            "another editor tab upgraded this draft to a newer format; " +
              "reload this tab to keep working on it"
          ));
          return;
        }
        reject(request.error ?? new Error("could not open IndexedDB"));
      });
      request.addEventListener("blocked", () =>
        reject(new ProjectStoreError(
          "STORAGE_FAILURE",
          "another editor tab is blocking the draft database upgrade"
        ))
      );
    });
    return this.#database;
  }

  #assertRevision(
    path: string,
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

  #storageError(error: unknown): Error {
    if (error instanceof ProjectStoreError) {
      return error;
    }
    if (error instanceof DOMException && error.name === "QuotaExceededError") {
      return new ProjectStoreError("QUOTA_EXCEEDED", error.message);
    }
    return error instanceof Error
      ? error
      : new ProjectStoreError("STORAGE_FAILURE", "unknown IndexedDB failure");
  }
}
