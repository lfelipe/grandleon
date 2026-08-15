// SPDX-License-Identifier: MIT
export type ProjectPath = string;
export type ProjectRevision = number;

export interface ProjectFile {
  readonly path: ProjectPath;
  readonly bytes: Uint8Array;
  readonly revision: ProjectRevision;
}

export interface ProjectSnapshot {
  readonly revision: ProjectRevision;
  readonly files: readonly ProjectFile[];
}

export interface WriteOptions {
  readonly expectedRevision?: ProjectRevision;
}

export interface ProjectStore {
  readonly projectId: string;
  read(path: ProjectPath): Promise<ProjectFile | undefined>;
  write(
    path: ProjectPath,
    bytes: Uint8Array,
    options?: WriteOptions
  ): Promise<ProjectFile>;
  delete(path: ProjectPath, options?: WriteOptions): Promise<void>;
  list(prefix?: ProjectPath): Promise<readonly ProjectFile[]>;
  snapshot(): Promise<ProjectSnapshot>;
}

export type ProjectStoreErrorCode =
  | "INVALID_PATH"
  | "REVISION_CONFLICT"
  | "FILE_NOT_FOUND"
  | "QUOTA_EXCEEDED"
  | "STORAGE_FAILURE"
  | "PERMISSION_DENIED";

export class ProjectStoreError extends Error {
  constructor(
    readonly code: ProjectStoreErrorCode,
    message: string
  ) {
    super(message);
    this.name = "ProjectStoreError";
  }
}

export function normalizeProjectPath(candidate: string): ProjectPath {
  if (
    candidate.length === 0 ||
    candidate.startsWith("/") ||
    candidate.includes("\\") ||
    candidate.includes("\0")
  ) {
    throw new ProjectStoreError("INVALID_PATH", `invalid project path '${candidate}'`);
  }

  const segments = candidate.split("/");
  if (segments.some((segment) => segment === "" || segment === "." || segment === "..")) {
    throw new ProjectStoreError("INVALID_PATH", `invalid project path '${candidate}'`);
  }
  return segments.join("/");
}
