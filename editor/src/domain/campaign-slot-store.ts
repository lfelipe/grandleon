// SPDX-License-Identifier: MIT
import { stableContentId } from "./encounter-simulation";

// Where a playtest campaign is kept, and, the harder half, whose it is.
//
// The engine already keeps a campaign in a named slot after every battle and
// every management gesture. What it cannot answer on its own is which slot a
// browser tab should be looking in, because a tab is not a game: an author has
// one project open and many projects saved, loads a different game into the
// same tab, and edits the content between one playtest and the next.
//
// This module answers that, and only that. It names the slot, it says what a
// kept slot record holds, and it says what a store of them must be able to do.
// It does not read a save byte, and nothing downstream of it does either.

/**
 * The sixteen bytes that name a package, as sixteen-hex-digit-pairs.
 *
 * The same bytes the content compiler derives from `packageId` and the same
 * bytes `campaign::SavePackageRequirement` records, so a slot name and a save's
 * own idea of what content it belongs to are derived from one value.
 */
export function packageIdentityHex(packageIdentity: Uint8Array): string {
  let text = "";
  for (const byte of packageIdentity) {
    text += byte.toString(16).padStart(2, "0");
  }
  return text;
}

/**
 * What a slot is keyed by: the package the campaign belongs to, and the
 * campaign within it.
 *
 * **Both, or the persistence is wrong.** A slot keyed by campaign identity
 * alone would offer a resumed campaign to content that never wrote it: two
 * games in one tab whose campaigns happen to share an id, or the same author's
 * two drafts of one game. The engine refuses that (`wrong_campaign`, and the
 * package requirement the envelope carries), so it is safe rather than silent;
 * but a persistence layer that routinely refuses itself is not one to ship.
 *
 * **The package half is what the *save* means by a package**, not what the
 * editor means by a project. A project id would be the thing the author's draft
 * is filed under; the package identity is the thing the campaign's saved bytes
 * declare a requirement on. Keying by the latter means the slot a tab looks in
 * and the requirement the envelope carries cannot disagree, and it means an
 * author who exports a draft and re-imports it under a new project id finds the
 * campaign they were playing, because it is the same game.
 *
 * **The content revision is deliberately not part of the key.** It is the
 * answer to "is this save stale", and the save carries it already: an envelope
 * written against content newer than what is loaded is refused by the migration
 * registry as `downgrade_refused`, and one whose package is not the loaded one
 * as `unmounted_package`. Putting the revision in the key would turn every one
 * of those refusals into a slot that is simply not found, and the author would
 * be silently founding a fresh campaign and never told why. The key binds
 * identity; the envelope answers compatibility.
 *
 * The name is `play-` and sixteen hex digits of the two identities' stable
 * content id, which is twenty-one characters of lowercase ASCII: inside
 * `storage::maximum_slot_name_length`, and made of the characters
 * `storage::is_valid_slot_name` allows on every platform at once.
 */
export function campaignSlotName(
  packageIdentity: Uint8Array,
  campaignId: string
): string {
  const identity = stableContentId(
    `${packageIdentityHex(packageIdentity)}/${campaignId}`
  );
  return `play-${identity.toString(16).padStart(16, "0")}`;
}

/**
 * One kept campaign, as a store holds it.
 *
 * The bytes are the `GLSV` envelope exactly as the engine wrote it. The two
 * identities beside them are not read back on the way in, the slot name
 * already binding them, but they are what makes a stored record legible to
 * somebody looking at it, and what lets a store answer "what does this browser
 * hold" without decoding a save.
 */
export interface KeptCampaign {
  /** The slot name, and the store's own key. */
  readonly slot: string;
  /** The package identity the save declares a requirement on, in hex. */
  readonly packageId: string;
  /** The authored campaign the save is a position in. */
  readonly campaignId: string;
  readonly bytes: Uint8Array;
}

/**
 * Somewhere a browser can keep one campaign per (package, campaign).
 *
 * One slot per pair is the floor and the ceiling. There is no browsing, no
 * naming, no previewing: those are a save-slot menu, which
 * `storage::SlotStorage::slots()` could already answer and which nothing has
 * asked for. What an authoring playtest needs is that the campaign it was
 * playing is there when the page comes back.
 */
export interface CampaignSlotStore {
  read(slot: string): Promise<KeptCampaign | undefined>;
  write(kept: KeptCampaign): Promise<void>;
  erase(slot: string): Promise<void>;
}

/**
 * A store that forgets when the page does.
 *
 * What a tab uses when the browser will not give it persistence, under private
 * browsing, storage disabled or a quota refusal, so that Play still works and
 * still keeps a campaign between battles for as long as the page lives.
 * Keeping a campaign in memory alone is a better answer than a Play mode that
 * will not start.
 */
export class MemoryCampaignSlotStore implements CampaignSlotStore {
  readonly #kept = new Map<string, KeptCampaign>();

  read(slot: string): Promise<KeptCampaign | undefined> {
    const held = this.#kept.get(slot);
    return Promise.resolve(
      held ? { ...held, bytes: held.bytes.slice() } : undefined
    );
  }

  write(kept: KeptCampaign): Promise<void> {
    this.#kept.set(kept.slot, { ...kept, bytes: kept.bytes.slice() });
    return Promise.resolve();
  }

  erase(slot: string): Promise<void> {
    this.#kept.delete(slot);
    return Promise.resolve();
  }
}
