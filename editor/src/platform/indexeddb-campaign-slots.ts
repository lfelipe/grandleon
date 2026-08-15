// SPDX-License-Identifier: MIT
import type {
  CampaignSlotStore,
  KeptCampaign
} from "../domain/campaign-slot-store";

// The browser half of "a playtest survives a reload".
//
// One database, one object store, one record per (package, campaign), keyed by
// the slot name `campaignSlotName` derives, which is what binds those two
// identities. The value is the `GLSV` envelope verbatim. Nothing here reads a
// byte of it: the save format carries its own magic, its own checksums, its own
// versioning and its own package requirements, and wrapping it in a second
// format would be a second thing to keep in step with the engine.
//
// Separate from the project draft database on purpose. A draft is filed under
// the project the author is editing; a kept campaign is filed under the package
// its save declares a requirement on, and the two are not the same identity.
// Putting them in one database would also mean bumping the draft's version
// every time this store learns anything, which is a migration risk taken for
// nothing.

const databaseName = "grandleon-play-slots";
const databaseVersion = 1;
const slotsStore = "slots";

interface StoredSlot {
  slot: string;
  packageId: string;
  campaignId: string;
  bytes: Uint8Array;
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

export interface IndexedDbCampaignSlotStoreOptions {
  readonly indexedDB?: IDBFactory;
  /**
   * The largest campaign this store will keep. Ten times what a whole demo
   * campaign encodes to, and below the sixty-four kilobytes the engine's own
   * boundary can carry in one piece, so a refusal here is a refusal about disk
   * rather than a truncation the engine would discover later.
   */
  readonly maximumBytes?: number;
}

export class IndexedDbCampaignSlotStore implements CampaignSlotStore {
  readonly #factory: IDBFactory;
  readonly #maximumBytes: number;
  #database: Promise<IDBDatabase> | undefined;

  constructor(options: IndexedDbCampaignSlotStoreOptions = {}) {
    this.#factory = options.indexedDB ?? globalThis.indexedDB;
    this.#maximumBytes = options.maximumBytes ?? 64 * 1024;
  }

  async read(slot: string): Promise<KeptCampaign | undefined> {
    const database = await this.#open();
    const transaction = database.transaction(slotsStore, "readonly");
    const stored = await requestResult<StoredSlot | undefined>(
      transaction.objectStore(slotsStore).get(slot)
    );
    await transactionComplete(transaction);
    if (!stored) return undefined;
    return {
      slot: stored.slot,
      packageId: stored.packageId,
      campaignId: stored.campaignId,
      bytes: stored.bytes.slice()
    };
  }

  async write(kept: KeptCampaign): Promise<void> {
    if (kept.bytes.length > this.#maximumBytes) {
      throw new Error(
        `a kept campaign of ${kept.bytes.length} bytes is over the ` +
          `${this.#maximumBytes}-byte limit for one slot`
      );
    }
    const database = await this.#open();
    const transaction = database.transaction(slotsStore, "readwrite");
    const done = transactionComplete(transaction);
    // One record replaces the one before it. There is exactly one kept campaign
    // per (package, campaign), so a write is a replacement and never a history.
    transaction.objectStore(slotsStore).put({
      slot: kept.slot,
      packageId: kept.packageId,
      campaignId: kept.campaignId,
      bytes: kept.bytes.slice()
    } satisfies StoredSlot);
    await done;
  }

  async erase(slot: string): Promise<void> {
    const database = await this.#open();
    const transaction = database.transaction(slotsStore, "readwrite");
    const done = transactionComplete(transaction);
    transaction.objectStore(slotsStore).delete(slot);
    await done;
  }

  async close(): Promise<void> {
    if (this.#database) {
      (await this.#database).close();
      this.#database = undefined;
    }
  }

  #open(): Promise<IDBDatabase> {
    this.#database ??= new Promise((resolve, reject) => {
      const request = this.#factory.open(databaseName, databaseVersion);
      request.addEventListener("upgradeneeded", () => {
        const database = request.result;
        if (!database.objectStoreNames.contains(slotsStore)) {
          database.createObjectStore(slotsStore, { keyPath: "slot" });
        }
      });
      request.addEventListener("success", () => {
        const database = request.result;
        // A second tab on a newer build cannot upgrade this database while
        // this connection is open; without stepping aside both tabs wait
        // forever. The next operation opens again, at whatever version now
        // exists, the same handling the project draft store does.
        database.addEventListener("versionchange", () => {
          database.close();
          this.#database = undefined;
        });
        resolve(database);
      });
      request.addEventListener("error", () => {
        this.#database = undefined;
        reject(request.error ?? new Error("could not open IndexedDB"));
      });
      request.addEventListener("blocked", () => {
        this.#database = undefined;
        reject(
          new Error("another editor tab is blocking the kept-campaign upgrade")
        );
      });
    });
    return this.#database;
  }
}

/**
 * The store a browser tab should use, or the forgetful one when it has no
 * IndexedDB at all.
 *
 * Private browsing and disabled storage are the cases. Play still runs and
 * still keeps a campaign between battles there; it just does not outlive the
 * page, which is exactly where this surface stood before.
 */
export function browserCampaignSlotStore(): CampaignSlotStore | undefined {
  return globalThis.indexedDB ? new IndexedDbCampaignSlotStore() : undefined;
}
