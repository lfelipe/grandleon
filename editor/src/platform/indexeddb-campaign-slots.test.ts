// SPDX-License-Identifier: MIT
import { IDBFactory } from "fake-indexeddb";
import { describe, expect, it } from "vitest";
import { IndexedDbCampaignSlotStore } from "./indexeddb-campaign-slots";

// The browser half. A reload is modelled the way it actually happens: the
// adapter that wrote the bytes is closed and thrown away, and a completely new
// one, holding no state of its own, is asked for them.

const record = {
  slot: "play-0011223344556677",
  packageId: "0".repeat(32),
  campaignId: "muster_road"
};

describe("IndexedDbCampaignSlotStore", () => {
  it("gives a kept campaign back to an adapter that never wrote it", async () => {
    const factory = new IDBFactory();
    const first = new IndexedDbCampaignSlotStore({ indexedDB: factory });
    await first.write({ ...record, bytes: new Uint8Array([71, 76, 83, 86]) });
    await first.close();

    const reloaded = new IndexedDbCampaignSlotStore({ indexedDB: factory });
    const kept = (await reloaded.read(record.slot))!;
    expect([...kept.bytes]).toEqual([71, 76, 83, 86]);
    // The two identities the slot binds are legible in the record itself, so
    // that what a browser is holding can be read without decoding a save.
    expect(kept.packageId).toBe(record.packageId);
    expect(kept.campaignId).toBe("muster_road");
  });

  it("holds one campaign per slot, not a history of them", async () => {
    const store = new IndexedDbCampaignSlotStore({ indexedDB: new IDBFactory() });
    await store.write({ ...record, bytes: new Uint8Array([1]) });
    await store.write({ ...record, bytes: new Uint8Array([2, 2, 2]) });
    expect([...(await store.read(record.slot))!.bytes]).toEqual([2, 2, 2]);
  });

  it("keeps two slots apart and forgets only the one named", async () => {
    const store = new IndexedDbCampaignSlotStore({ indexedDB: new IDBFactory() });
    await store.write({ ...record, bytes: new Uint8Array([1]) });
    await store.write({
      ...record,
      slot: "play-8899aabbccddeeff",
      bytes: new Uint8Array([2])
    });
    await store.erase(record.slot);
    expect(await store.read(record.slot)).toBeUndefined();
    expect([...(await store.read("play-8899aabbccddeeff"))!.bytes]).toEqual([2]);
  });

  it("forgetting a slot nobody wrote is not a failure", async () => {
    const store = new IndexedDbCampaignSlotStore({ indexedDB: new IDBFactory() });
    // Founding anew erases before it founds, and a first-ever Play has nothing
    // to erase. That has to be quiet rather than an error the surface reports.
    await expect(store.erase(record.slot)).resolves.toBeUndefined();
  });

  it("refuses a campaign larger than one slot without losing the one kept", async () => {
    const store = new IndexedDbCampaignSlotStore({
      indexedDB: new IDBFactory(),
      maximumBytes: 4
    });
    await store.write({ ...record, bytes: new Uint8Array([1, 2]) });
    await expect(
      store.write({ ...record, bytes: new Uint8Array(8) })
    ).rejects.toThrow(/over the 4-byte limit/);
    expect([...(await store.read(record.slot))!.bytes]).toEqual([1, 2]);
  });
});
