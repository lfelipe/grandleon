// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import { exerciseProjectStoreContract } from "../domain/project-store.contract";
import {
  FileSystemProjectStore,
  type ProjectDirectoryPort
} from "./filesystem-project-store";

class FakeDirectory implements ProjectDirectoryPort {
  readonly files = new Map<string, Uint8Array>();
  allowed = true;

  async requestPermission(): Promise<boolean> {
    return this.allowed;
  }
  async read(path: string): Promise<Uint8Array | undefined> {
    return this.files.get(path)?.slice();
  }
  async write(path: string, bytes: Uint8Array): Promise<void> {
    this.files.set(path, bytes.slice());
  }
  async delete(path: string): Promise<void> {
    this.files.delete(path);
  }
  async list(): Promise<readonly string[]> {
    return [...this.files.keys()];
  }
}

describe("FileSystemProjectStore", () => {
  it("satisfies the project-store contract", async () => {
    await exerciseProjectStoreContract(
      () => new FileSystemProjectStore("filesystem-contract", new FakeDirectory())
    );
  });

  it("requests permission and reports denial without touching files", async () => {
    const directory = new FakeDirectory();
    directory.allowed = false;
    const store = new FileSystemProjectStore("denied", directory);
    await expect(store.write("manifest.json", new Uint8Array([1])))
      .rejects.toMatchObject({ code: "PERMISSION_DENIED" });
    expect(directory.files.size).toBe(0);
  });

  it("detects an external edit before an optimistic write", async () => {
    const directory = new FakeDirectory();
    const store = new FileSystemProjectStore("conflict", directory);
    const original = await store.write("manifest.json", new Uint8Array([1]));
    directory.files.set("manifest.json", new Uint8Array([2]));

    await expect(
      store.write("manifest.json", new Uint8Array([3]), {
        expectedRevision: original.revision
      })
    ).rejects.toMatchObject({ code: "REVISION_CONFLICT" });
    expect([...(directory.files.get("manifest.json")!)]).toEqual([2]);
  });
});
