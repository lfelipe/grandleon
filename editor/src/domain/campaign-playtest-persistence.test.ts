// SPDX-License-Identifier: MIT
import { IDBFactory } from "fake-indexeddb";
import { describe, expect, it } from "vitest";
import { createDemoProject } from "../sample-projects";
import { IndexedDbCampaignSlotStore } from "../platform/indexeddb-campaign-slots";
import {
  MemoryCampaignSlotStore,
  type CampaignSlotStore
} from "./campaign-slot-store";
import {
  commitCampaignBattle,
  endCampaignPlaySession,
  forgetKeptCampaign,
  keepCampaign,
  keptCampaignSlot,
  playableCampaignId,
  proceedFromCampaignCompany,
  restoreKeptCampaign,
  startCampaignPlaySession,
  type CampaignPlaySession
} from "./campaign-playtest-session";
import {
  attackUnit,
  beginBattle,
  moveUnit,
  useItem,
  waitUnit,
  type PlaytestState
} from "./playtest-session";
import { eraseEngineSlot, readEngineSlot } from "./encounter-simulation";

// A playtest that survives a reload.
//
// The reload is modelled honestly, and that is the whole design of this file.
// A page coming back means the WebAssembly module is instantiated again with an
// empty slot device, so every test here **erases the engine's slot** before it
// asks for the campaign back. Anything that comes out afterwards came out of
// the browser's store and nowhere else; without that erase these tests would
// pass on the module memory that already made "leave Play and come back" work,
// and prove nothing new.
//
// The content is the demo's `muster_road`, the same campaign
// `tests/campaign_runtime/demo_permadeath_test.cpp` fights natively, so what a
// resumed campaign is asserted to hold is what the engine derived rather than
// what this side hoped for.

const vanguard = "Vanguard Rilla";
const outrider = "Outrider Bevan";
const ferryman = "Torvald the Ferryman";
const campaignId = "muster_road";

function battleOf(session: CampaignPlaySession): PlaytestState {
  const battle = session.battle;
  if (!battle) throw new Error("The session is not standing on a battle.");
  return battle;
}

/**
 * The crossing, fought exactly as the native test fights it: the outrider
 * trades with the picket and drinks its tonic, the picket finishes it anyway,
 * and the vanguard rides onto the emptied tile and kills the picket in the
 * same turn.
 */
function fightTheCrossing(session: CampaignPlaySession): void {
  expect(session.phase).toBe("managing");
  proceedFromCampaignCompany(session);
  const project = session.project;
  const state = battleOf(session);
  expect(beginBattle(state)).toBe(true);
  expect(attackUnit(project, state, "muster_outrider", "muster_picket")).toBe(true);
  expect(attackUnit(project, state, "muster_picket", "muster_outrider")).toBe(true);
  expect(useItem(project, state, "muster_outrider", "field_tonic")).toBe(true);
  expect(attackUnit(project, state, "muster_picket", "muster_outrider")).toBe(true);
  // One turn, not two: the walk leaves the vanguard a second action point, so
  // riding onto the tile the outrider fell from and finishing the picket is a
  // single activation.
  expect(moveUnit(project, state, "muster_vanguard", 2, 1)).toBe(true);
  expect(attackUnit(project, state, "muster_vanguard", "muster_picket")).toBe(true);
  expect(state.outcome).toBe("first_side_won");
  commitCampaignBattle(session);
}

function play(project: ReturnType<typeof createDemoProject>, resume = false) {
  const started = startCampaignPlaySession(project, { campaignId, resume });
  expect(started.error).toBeUndefined();
  return started;
}

/**
 * Plays the crossing on a fresh company and keeps what it did.
 *
 * Returns the project it was played on, so a test that wants to edit the
 * content out from under the save has the very object the save was written
 * against to edit a copy of.
 */
async function keepACrossing(store: CampaignSlotStore) {
  const project = createDemoProject();
  const session = play(project).session!;
  try {
    fightTheCrossing(session);
    expect(await keepCampaign(session, store)).toBe(true);
  } finally {
    endCampaignPlaySession(session);
  }
  return project;
}

/** What a page reload does to the module: the slot device starts empty. */
function reload(project: ReturnType<typeof createDemoProject>): void {
  eraseEngineSlot(keptCampaignSlot(project, campaignId));
}

describe("a browser playtest that outlives the page", () => {
  it("gives back the campaign a reloaded module has no memory of", async () => {
    const store = new IndexedDbCampaignSlotStore({ indexedDB: new IDBFactory() });
    const project = await keepACrossing(store);
    reload(project);
    // The module has forgotten it. Nothing but the browser's store knows the
    // crossing was ever fought.
    expect(readEngineSlot(keptCampaignSlot(project, campaignId)).error).toBe(
      "not_found"
    );

    expect(await restoreKeptCampaign(project, campaignId, store)).toBe(true);
    const resumed = play(createDemoProject(), true);
    const session = resumed.session!;
    try {
      expect(resumed.refusal).toBeUndefined();
      expect(session.resumed).toBe(true);

      // The dead stay dead. The rider the crossing buried is off the next
      // board, named as unfieldable, and the survivor carries the level the
      // battle taught them.
      proceedFromCampaignCompany(session);
      expect(battleOf(session).nodeId).toBe("road_watch");
      expect(session.excluded.map((member) => member.name)).toEqual([outrider]);
      const survivor = session.roster.find((member) => member.name === vanguard)!;
      expect(survivor.level).toBe(2);
      expect(survivor.experience).toBe(60);
      // And the store holds what it held: the picket's drop and the tonic the
      // fallen rider left behind, less the one the outrider drank.
      expect(
        session.roster.find((member) => member.name === ferryman)!.availability
      ).toBe("available");
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("carries the envelope across untouched, byte for byte", async () => {
    const store = new MemoryCampaignSlotStore();
    const project = await keepACrossing(store);
    const slot = keptCampaignSlot(project, campaignId);
    const saved = readEngineSlot(slot).bytes!;
    const kept = (await store.read(slot))!;
    // No second format around the save. The `GLSV` magic is the first four
    // bytes of what the browser holds, because what the browser holds is what
    // `campaign::save_campaign` wrote.
    expect([...kept.bytes]).toEqual([...saved]);
    expect([...kept.bytes.subarray(0, 4)]).toEqual([0x47, 0x4c, 0x53, 0x56]);
    expect(kept.campaignId).toBe(campaignId);
  });

  it("does not offer one game's campaign to another game", async () => {
    const store = new MemoryCampaignSlotStore();
    const played = await keepACrossing(store);
    reload(played);

    // The same campaign id, in a different game. This is the case the key has
    // to separate: keyed by campaign alone, this tab would be handed a campaign
    // this content never wrote and the engine would spend its life refusing it.
    const otherGame = createDemoProject();
    otherGame.packageId = "9f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0";
    expect(keptCampaignSlot(otherGame, campaignId)).not.toBe(
      keptCampaignSlot(played, campaignId)
    );
    expect(await restoreKeptCampaign(otherGame, campaignId, store)).toBe(false);

    const started = play(otherGame, true);
    try {
      // Nothing to pick up, so nothing was picked up, and no refusal was
      // reported: a slot that was never written is not a failure to report.
      expect(started.session!.resumed).toBe(false);
      expect(started.refusal).toBeUndefined();
    } finally {
      endCampaignPlaySession(started.session);
    }
  });

  it("refuses a save whose content moved, by the registry's own name", async () => {
    const store = new MemoryCampaignSlotStore();
    const played = await keepACrossing(store);
    reload(played);

    // The author edits the game and says so, which is what a content revision
    // is for. The kept campaign was written against 0.1.0 and no step is
    // registered that carries a save from that revision to the next, so the
    // migration registry refuses it by name, and the slot key deliberately
    // does not include the revision, which is what makes this a refusal the
    // author is told about rather than a slot that quietly is not found.
    const edited = createDemoProject();
    edited.contentRevision = "0.1.1";
    expect(keptCampaignSlot(edited, campaignId)).toBe(
      keptCampaignSlot(played, campaignId)
    );
    expect(await restoreKeptCampaign(edited, campaignId, store)).toBe(true);

    const started = play(edited, true);
    try {
      expect(started.session!.resumed).toBe(false);
      expect(started.refusal).toContain("missing_step");
      expect(started.refusal).toContain("Start fresh to replace it.");
      // And there is a campaign standing there to play: the engine founds
      // before it loads, so a refused save is never an empty screen.
      expect(started.session!.phase).toBe("managing");
    } finally {
      endCampaignPlaySession(started.session);
    }
  });

  it("refuses a save belonging to a campaign this flow is not", async () => {
    const store = new MemoryCampaignSlotStore();
    const played = await keepACrossing(store);
    reload(played);
    const slot = keptCampaignSlot(played, campaignId);

    // The same bytes, put into the slot the demo's *other* campaign reads. A
    // key cannot prevent this, only somebody putting bytes where they do not
    // belong can cause it, and the point is that the engine still names it
    // rather than standing the player on a node this flow does not contain.
    const kept = (await store.read(slot))!;
    const other = playableCampaignId(createDemoProject())!;
    expect(other).not.toBe(campaignId);
    const otherSlot = keptCampaignSlot(played, other);
    await store.write({ ...kept, slot: otherSlot, campaignId: other });
    expect(await restoreKeptCampaign(played, other, store)).toBe(true);

    const started = startCampaignPlaySession(createDemoProject(), {
      campaignId: other,
      resume: true
    });
    try {
      expect(started.session!.resumed).toBe(false);
      expect(started.refusal).toContain("a different campaign");
    } finally {
      endCampaignPlaySession(started.session);
    }
  });

  it("forgets the kept campaign when the author founds a new one", async () => {
    const store = new IndexedDbCampaignSlotStore({ indexedDB: new IDBFactory() });
    const project = await keepACrossing(store);
    const slot = keptCampaignSlot(project, campaignId);
    expect(await store.read(slot)).toBeDefined();

    // Founding anew replaces what was kept, on the device and in the browser
    // both. One slot per (package, campaign) means the campaign being replaced
    // has nowhere else to be, which is why the surface says so before the
    // press rather than after it.
    await forgetKeptCampaign(project, campaignId, store);
    expect(await store.read(slot)).toBeUndefined();
    expect(readEngineSlot(slot).error).toBe("not_found");

    const founded = play(createDemoProject(), true);
    try {
      expect(founded.session!.resumed).toBe(false);
      expect(founded.session!.roster.map((member) => member.name)).toEqual([
        vanguard,
        outrider
      ]);
    } finally {
      endCampaignPlaySession(founded.session);
    }
  });

  it("keeps nothing for a campaign that has not saved yet", async () => {
    const store = new MemoryCampaignSlotStore();
    const project = createDemoProject();
    reload(project);
    const started = play(project);
    try {
      // Founding does not write a slot; the first commit does. A mirror of a
      // save that has not happened must carry nothing rather than an empty
      // record the next resume would be refused for.
      expect(await keepCampaign(started.session!, store)).toBe(false);
      expect(await store.read(keptCampaignSlot(project, campaignId))).toBe(
        undefined
      );
    } finally {
      endCampaignPlaySession(started.session);
    }
  });
});
