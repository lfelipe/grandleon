// SPDX-License-Identifier: MIT
import { IDBFactory } from "fake-indexeddb";
import { describe, expect, it } from "vitest";
import { exerciseProjectStoreContract } from "../domain/project-store.contract";
import { IndexedDbProjectStore } from "./indexeddb-project-store";

describe("IndexedDbProjectStore", () => {
  it("satisfies the project-store contract", async () => {
    const factory = new IDBFactory();
    await exerciseProjectStoreContract(
      () => new IndexedDbProjectStore("contract", { indexedDB: factory })
    );
  });

  it("recovers committed drafts after the adapter is reopened", async () => {
    const factory = new IDBFactory();
    const first = new IndexedDbProjectStore("recovery", { indexedDB: factory });
    await first.write("manifest.json", new Uint8Array([1, 2, 3]));
    await first.close();

    const reopened = new IndexedDbProjectStore("recovery", { indexedDB: factory });
    expect([...(await reopened.read("manifest.json"))!.bytes]).toEqual([1, 2, 3]);
  });

  it("rejects over-budget drafts without replacing committed content", async () => {
    const store = new IndexedDbProjectStore("quota", {
      indexedDB: new IDBFactory(),
      maximumBytes: 4
    });
    const original = await store.write("manifest.json", new Uint8Array([1, 2]));
    await expect(
      store.write("manifest.json", new Uint8Array([1, 2, 3, 4, 5]), {
        expectedRevision: original.revision
      })
    ).rejects.toMatchObject({ code: "QUOTA_EXCEEDED" });
    expect([...(await store.read("manifest.json"))!.bytes]).toEqual([1, 2]);
  });

  it("upgrades a legacy file store without losing its files", async () => {
    const factory = new IDBFactory();
    await new Promise<void>((resolve, reject) => {
      const request = factory.open("grandleon-editor:legacy", 1);
      request.onupgradeneeded = () =>
        request.result.createObjectStore("files", { keyPath: "path" });
      request.onerror = () => reject(request.error);
      request.onsuccess = () => {
        const database = request.result;
        const transaction = database.transaction("files", "readwrite");
        transaction.objectStore("files").put({
          path: "legacy.json",
          bytes: new Uint8Array([7]),
          revision: 1
        });
        transaction.oncomplete = () => {
          database.close();
          resolve();
        };
      };
    });

    const upgraded = new IndexedDbProjectStore("legacy", { indexedDB: factory });
    expect([...(await upgraded.read("legacy.json"))!.bytes]).toEqual([7]);
    expect((await upgraded.snapshot()).revision).toBe(0);
  });

  it("steps aside when another tab upgrades the draft database", async () => {
    const factory = new IDBFactory();
    const store = new IndexedDbProjectStore("shared", { indexedDB: factory });
    await store.write("manifest.json", new Uint8Array([1]));

    // A second tab on a newer build opens the same database at a higher
    // version. Without a versionchange handler this open is blocked by the
    // connection above and never completes.
    const upgraded = await new Promise<IDBDatabase>((resolve, reject) => {
      const request = factory.open("grandleon-editor:shared", 99);
      request.onerror = () => reject(request.error);
      request.onblocked = () => reject(new Error("the first tab blocked the upgrade"));
      request.onsuccess = () => resolve(request.result);
    });
    expect(upgraded.version).toBe(99);
    upgraded.close();

    // The first tab let go of its connection, and now says in words why it
    // cannot go on rather than failing with the platform's VersionError.
    await expect(store.read("manifest.json")).rejects.toMatchObject({
      code: "STORAGE_FAILURE",
      message: expect.stringContaining("reload this tab")
    });
  });

  it("reports a revision conflict when a second tab has written first", async () => {
    // Two tabs on the same draft. Both read the same file, both edit it, and
    // the second save must be refused rather than silently overwriting the
    // first. That is the guard the editor's save path relies on.
    const factory = new IDBFactory();
    const tabOne = new IndexedDbProjectStore("two-tabs", { indexedDB: factory });
    const tabTwo = new IndexedDbProjectStore("two-tabs", { indexedDB: factory });

    const seen = await tabOne.write("project.json", new Uint8Array([1]));
    expect((await tabTwo.read("project.json"))!.revision).toBe(seen.revision);

    const written = await tabTwo.write("project.json", new Uint8Array([2]), {
      expectedRevision: seen.revision
    });
    await expect(
      tabOne.write("project.json", new Uint8Array([3]), {
        expectedRevision: seen.revision
      })
    ).rejects.toMatchObject({ code: "REVISION_CONFLICT" });

    // The stale tab did not overwrite the fresh one, and can save once it has
    // caught up with what is actually stored.
    expect([...(await tabOne.read("project.json"))!.bytes]).toEqual([2]);
    await tabOne.write("project.json", new Uint8Array([3]), {
      expectedRevision: written.revision
    });
    expect([...(await tabTwo.read("project.json"))!.bytes]).toEqual([3]);

    // Deleting from a stale revision is refused the same way.
    await expect(
      tabTwo.delete("project.json", { expectedRevision: seen.revision })
    ).rejects.toMatchObject({ code: "REVISION_CONFLICT" });
    expect(await tabOne.read("project.json")).toBeDefined();
  });
});
