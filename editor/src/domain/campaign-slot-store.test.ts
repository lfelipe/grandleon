// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  campaignSlotName,
  packageIdentityHex,
  MemoryCampaignSlotStore
} from "./campaign-slot-store";

// The identity half of "a playtest survives a reload".
//
// A slot name is the whole of what decides whose kept campaign a tab is offered,
// so these are the assertions that keep the answer from drifting: it binds both
// identities, it changes when either changes, and it is a name every platform's
// storage device would accept.

function identity(byte: number): Uint8Array {
  return new Uint8Array(16).fill(byte);
}

const oneGame = identity(0x11);
const anotherGame = identity(0x22);

describe("the slot a kept campaign lives in", () => {
  it("is a name every platform's device would take", () => {
    const slot = campaignSlotName(oneGame, "muster_road");
    // `storage::is_valid_slot_name`: lowercase ASCII, digits, `_` and `-`,
    // between one and thirty-one characters. A name this side invents that the
    // device would refuse is a campaign that silently never saves.
    expect(slot).toMatch(/^[a-z0-9_-]{1,31}$/);
    expect(slot.length).toBeLessThanOrEqual(31);
  });

  it("is the same name for the same package and campaign", () => {
    expect(campaignSlotName(oneGame, "muster_road")).toBe(
      campaignSlotName(identity(0x11), "muster_road")
    );
  });

  it("keeps two campaigns of one game apart", () => {
    expect(campaignSlotName(oneGame, "muster_road")).not.toBe(
      campaignSlotName(oneGame, "tarnholt")
    );
  });

  it("keeps one campaign id in two games apart", () => {
    // The case the key has to separate: an author loads a different game into
    // the same tab, and both games happen to call their campaign the same thing.
    // Keyed by campaign alone these would collide and the engine would spend
    // its life refusing itself.
    expect(campaignSlotName(oneGame, "main")).not.toBe(
      campaignSlotName(anotherGame, "main")
    );
  });

  it("names a package by the sixteen bytes a save's requirement records", () => {
    expect(packageIdentityHex(new Uint8Array([0x0a, 0xff]))).toBe("0aff");
    expect(packageIdentityHex(identity(0))).toBe("0".repeat(32));
  });
});

describe("the store a browser without persistence falls back to", () => {
  it("keeps one campaign per slot and hands back a copy", async () => {
    const store = new MemoryCampaignSlotStore();
    const bytes = new Uint8Array([1, 2, 3]);
    await store.write({
      slot: "play-1",
      packageId: "aa",
      campaignId: "muster_road",
      bytes
    });
    bytes[0] = 9;
    const kept = (await store.read("play-1"))!;
    // Mutating what was handed in must not reach what was kept, and mutating
    // what comes back must not reach it either: a save is bytes the engine
    // owns, and a store that aliased them would corrupt one by editing another.
    expect([...kept.bytes]).toEqual([1, 2, 3]);
    kept.bytes[0] = 7;
    expect([...(await store.read("play-1"))!.bytes]).toEqual([1, 2, 3]);
  });

  it("replaces rather than accumulates, and forgets when told", async () => {
    const store = new MemoryCampaignSlotStore();
    const record = { slot: "play-1", packageId: "aa", campaignId: "c" };
    await store.write({ ...record, bytes: new Uint8Array([1]) });
    await store.write({ ...record, bytes: new Uint8Array([2, 2]) });
    expect([...(await store.read("play-1"))!.bytes]).toEqual([2, 2]);
    await store.erase("play-1");
    expect(await store.read("play-1")).toBeUndefined();
  });
});
