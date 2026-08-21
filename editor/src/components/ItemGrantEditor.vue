<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// What a campaign puts in its own store, edited wherever a campaign says it:
// the stock it is founded with, and what passing a node hands over. Both are
// the same record, on purpose: a grant is a quantity of one item going into
// the one shared store, whichever moment causes it, so both are edited by this
// one list, exactly as RosterMemberEditor serves the founding company and a
// node's recruits.
//
// A grant is an event and not a statement about how much the store should
// hold: passing a node twice grants twice. That is what the help text says and
// it is what the engine does; nothing here computes a total.
//
// Every change is emitted whole and the parent decides where it lands: the
// workspace saves the founding stock through the project session, the flow
// editor keeps a node's grants in its own draft until the flow is saved.
import { computed, ref } from "vue";
import type { CampaignItemGrant } from "../generated/source-v1";
import { useKeystrokeDraft } from "./keystroke-draft";

const props = defineProps<{
  grants: readonly CampaignItemGrant[];
  items: readonly { readonly id: string; readonly name: string }[];
  /**
   * What this project arms people with. A store holds weapons as well as
   * items: a better sword found on the road is the point of a store, and the
   * format lets one grant name either.
   */
  weapons?: readonly { readonly id: string; readonly name: string }[];
  /** Distinguishes control ids when several grant lists share a page. */
  idPrefix: string;
  heading: string;
  help: string;
  /** The word for one entry: "starting stock" at a campaign, "grant" at a node. */
  grantWord: string;
}>();

const emit = defineEmits<{
  update: [grants: CampaignItemGrant[]];
  createItem: [];
  /** A keystroke that is not in the project yet, so the header can say so. */
  dirty: [];
}>();

/**
 * The quantity and the note on a grant, while they are being typed.
 *
 * Every `update` this list emits is a whole new list the surface above writes
 * into the project, at the cost of one undo entry, so a keystroke is held
 * here, announced as unsaved and drawn by the control that owns it, and one
 * edit is committed when the field is left or a Save reaches `flush`.
 *
 * The quantity is held for a further reason. Emptying the box and typing "12"
 * passes through nothing at all and then through "1", and both are numbers this
 * store would then claim to hold. Half a number is not a number.
 */
const keystrokes = useKeystrokeDraft(() => emit("dirty"));

defineExpose({ flush: keystrokes.flush });

// One search box for the list rather than one per grant: the choice is the same
// project-wide roll of items every time, and a grant's own choice is never
// filtered away, so searching cannot hide what is already chosen.
const search = ref("");

/**
 * One thing a store can be given, of either kind.
 *
 * A grant names an item or a weapon, so everything this list does - offering,
 * refusing a second helping, naming what is missing - is asked about a pair
 * and not about an identifier. An identity is unique only within its kind, so
 * an item and a weapon may share one and `key` is what tells them apart.
 */
interface Stockable {
  readonly kind: "item" | "weapon";
  readonly id: string;
  readonly name: string;
  readonly key: string;
}

const stockables = computed<readonly Stockable[]>(() => [
  ...props.items.map((item) => ({
    kind: "item" as const, id: item.id, name: item.name,
    key: `item:${item.id}`
  })),
  ...(props.weapons ?? []).map((weapon) => ({
    kind: "weapon" as const, id: weapon.id, name: weapon.name,
    key: `weapon:${weapon.id}`
  }))
]);

const anyItems = computed(() => stockables.value.length > 0);

/**
 * What one grant names, as a key of the same shape a stockable carries.
 *
 * A grant naming neither is a fault the problem list reports; it answers as
 * the empty string so that every caller here has one thing to compare.
 */
function subjectKey(grant: CampaignItemGrant): string {
  if (grant.itemId !== undefined) return `item:${grant.itemId}`;
  if (grant.weaponId !== undefined) return `weapon:${grant.weaponId}`;
  return "";
}

/** The identifier a grant names, of whichever kind, for a message. */
function subjectId(grant: CampaignItemGrant): string {
  return grant.itemId ?? grant.weaponId ?? "";
}

/** The record a key names, if this project still holds it. */
function stockableFor(key: string): Stockable | undefined {
  return stockables.value.find((candidate) => candidate.key === key);
}

/** A grant rewritten to name one stockable, dropping the kind it is not. */
function named(grant: CampaignItemGrant, key: string): CampaignItemGrant {
  const chosen = stockableFor(key);
  const next: CampaignItemGrant = { ...grant };
  delete next.itemId;
  delete next.weaponId;
  if (chosen === undefined) return next;
  if (chosen.kind === "weapon") next.weaponId = chosen.id;
  else next.itemId = chosen.id;
  return next;
}

function copyGrants(): CampaignItemGrant[] {
  return props.grants.map((grant) => ({ ...grant }));
}

function patchGrant(index: number, change: Partial<CampaignItemGrant>) {
  const grants = copyGrants();
  const grant = grants[index];
  if (!grant) return;
  grants[index] = { ...grant, ...change };
  emit("update", grants);
}

/**
 * Notes are optional, and an absent field says something different from a
 * field holding nothing: an emptied note is removed rather than blanked.
 */
function patchNotes(index: number, notes: string) {
  const grants = copyGrants();
  const grant = grants[index];
  if (!grant) return;
  if (notes.trim() === "") delete grant.notes;
  else grant.notes = notes;
  emit("update", grants);
}

/**
 * A quantity is a whole number the schema bounds at 1..65535, and it has no
 * default: a list of things a company owns must never hold an omitted number.
 * Anything unreadable is kept out of the record rather than silently rounded,
 * and the problem list below says so beside the field.
 */
function patchQuantity(index: number, raw: string) {
  const parsed = Number.parseInt(raw, 10);
  patchGrant(index, { quantity: Number.isNaN(parsed) ? 0 : parsed });
}

// Adding or removing a grant renumbers the ones after it, and a held keystroke
// is keyed by position, so it is committed before the list moves under it.
/** Whether there is anything left to stock, for the add button to ask. */
const everythingStocked = computed(
  () => stockables.value.length > 0 && unstocked().length === 0
);

/** Everything this list does not already stock, of either kind. */
function unstocked(): readonly Stockable[] {
  const stocked = new Set(props.grants.map(subjectKey));
  return stockables.value.filter((candidate) => !stocked.has(candidate.key));
}

function addGrant() {
  keystrokes.flush();
  const grants = copyGrants();
  // A fresh grant starts as the first item this list does *not* already stock,
  // rather than as the first item in the project. Pressing add twice used to
  // stock the same thing twice, which the format refuses and a validator then
  // reported - so the surface offered a gesture whose only outcome was a
  // mistake. One is the quantity an author is most likely to keep and the one
  // they will most obviously change.
  const next = unstocked()[0];
  if (next === undefined) return;
  grants.push(
    next.kind === "weapon"
      ? { weaponId: next.id, quantity: 1 }
      : { itemId: next.id, quantity: 1 }
  );
  emit("update", grants);
}

/**
 * What one grant hands over, replaced whole.
 *
 * Whole rather than patched, because the two identity fields are exclusive:
 * writing the new one without removing the old would leave a grant naming an
 * item and a weapon at once, which is the one shape the format refuses.
 */
function replaceGrant(index: number, key: string) {
  const grants = copyGrants();
  const grant = grants[index];
  if (!grant) return;
  grants[index] = named(grant, key);
  emit("update", grants);
}

function removeGrant(index: number) {
  keystrokes.flush();
  const grants = copyGrants();
  grants.splice(index, 1);
  emit("update", grants);
}

/**
 * What is offered for one grant: the search, plus its own choice, minus
 * whatever another row already stocks.
 *
 * Its own choice always survives the filter. A row showing a menu its own value
 * is not in is a row that appears to have changed by itself, and an author who
 * opens it has no way back to what they had.
 */
function itemChoices(grant: CampaignItemGrant): readonly Stockable[] {
  const mine = subjectKey(grant);
  const elsewhere = new Set(
    props.grants.filter((other) => other !== grant).map(subjectKey)
  );
  const offered = stockables.value.filter(
    (candidate) => candidate.key === mine || !elsewhere.has(candidate.key)
  );
  const query = search.value.trim().toLocaleLowerCase();
  if (query === "") return offered;
  return offered.filter((candidate) =>
    candidate.key === mine ||
    candidate.id.toLocaleLowerCase().includes(query) ||
    candidate.name.toLocaleLowerCase().includes(query)
  );
}

/** A stored thing this project does not hold, named rather than hidden. */
function missingItem(grant: CampaignItemGrant): string | undefined {
  const key = subjectKey(grant);
  if (key === "" || subjectId(grant) === "") return undefined;
  return stockableFor(key) === undefined ? subjectId(grant) : undefined;
}

/** The word for what one grant hands over, for a message about it. */
function subjectWord(grant: CampaignItemGrant): string {
  return grant.weaponId !== undefined ? "weapon" : "item";
}

/** Everything wrong with one grant, beside the grant, in plain words. */
function grantProblems(grant: CampaignItemGrant, index: number): string[] {
  const problems: string[] = [];
  const key = subjectKey(grant);
  if (grant.itemId !== undefined && grant.weaponId !== undefined) {
    problems.push(
      "This names an item and a weapon. One entry hands over one thing."
    );
  } else if (key === "" || subjectId(grant) === "") {
    problems.push("Choose what this puts in the store.");
  } else if (missingItem(grant) !== undefined) {
    problems.push(
      `'${subjectId(grant)}' is not ${
        grant.weaponId !== undefined ? "a weapon" : "an item"
      } in this project. Choose another, or create the ${subjectWord(grant)}.`
    );
  } else if (
    props.grants.some(
      (candidate, candidateIndex) =>
        candidateIndex !== index && subjectKey(candidate) === key
    )
  ) {
    problems.push(
      `This list already stocks '${subjectId(grant)}'. Say how many once: two ` +
      `entries for one ${subjectWord(grant)} are two different answers to one ` +
      "question."
    );
  }
  if (!Number.isInteger(grant.quantity) || grant.quantity < 1) {
    problems.push("Say how many, as a whole number of at least 1.");
  } else if (grant.quantity > 65535) {
    problems.push("65535 is the most of one thing a store can be given at once.");
  }
  return problems;
}
</script>

<template>
  <section class="grant-editor" :aria-labelledby="`${idPrefix}-title`">
    <h4 :id="`${idPrefix}-title`">{{ heading }}</h4>
    <p class="field-help">{{ help }}</p>

    <p v-if="!anyItems" class="grant-warning" role="status">
      This project has no items or weapons yet, so there is nothing to give.
      Create one first.
    </p>
    <template v-else>
      <label :for="`${idPrefix}-item-search`">Search what it can give</label>
      <input :id="`${idPrefix}-item-search`" v-model="search" type="search">
    </template>
    <button type="button" class="secondary" @click="emit('createItem')">
      Create related item
    </button>

    <p v-if="grants.length === 0" class="grant-warning" role="status">
      Nothing here yet.
    </p>
    <!-- Keyed by position, never by the item being chosen into the fieldset: a
         key that changes with the choice remounts the controls and throws the
         author's focus away. -->
    <fieldset v-for="(grant, index) in grants" :key="index" class="grant-entry">
      <legend>
        {{ grantWord.charAt(0).toLocaleUpperCase() + grantWord.slice(1) }}
        {{ index + 1 }}
      </legend>
      <label :for="`${idPrefix}-${index}-item`">What this gives</label>
      <!-- The value is the kind and the identity together, because an identity
           is unique only within its kind: a project may hold an item and a
           weapon that answer to one name, and a menu keyed on the name alone
           could not tell an author which one they had chosen. -->
      <select :id="`${idPrefix}-${index}-item`" :value="subjectKey(grant)"
        @change="replaceGrant(index, ($event.target as HTMLSelectElement).value)">
        <option v-if="subjectKey(grant) === ''" value="" disabled>
          Choose what this gives
        </option>
        <option v-if="missingItem(grant)" :value="subjectKey(grant)" disabled>
          {{ missingItem(grant) }}: not
          {{ grant.weaponId !== undefined ? "a weapon" : "an item" }}
          in this project
        </option>
        <option v-for="choice in itemChoices(grant)" :key="choice.key"
          :value="choice.key">
          {{ choice.name }} ({{ choice.id }}){{
            choice.kind === "weapon" ? " - weapon" : ""
          }}
        </option>
      </select>
      <label :for="`${idPrefix}-${index}-quantity`">How many</label>
      <!-- `input` holds the keystroke and announces it, `change` commits it.
           Both, because they answer different questions: whether the editor
           knows there is work in progress, and when that work becomes a step an
           author can undo. -->
      <input :id="`${idPrefix}-${index}-quantity`" type="number" min="1"
        max="65535" step="1"
        :value="keystrokes.shown(`${index}-quantity`, String(grant.quantity))"
        @input="keystrokes.type(
          `${index}-quantity`,
          ($event.target as HTMLInputElement).value,
          (typed) => patchQuantity(index, typed)
        )"
        @change="keystrokes.leave(
          `${index}-quantity`,
          ($event.target as HTMLInputElement).value,
          (typed) => patchQuantity(index, typed)
        )">
      <label :for="`${idPrefix}-${index}-notes`">Notes</label>
      <textarea :id="`${idPrefix}-${index}-notes`"
        :value="keystrokes.shown(`${index}-notes`, grant.notes ?? '')"
        rows="2" maxlength="4096"
        @input="keystrokes.type(
          `${index}-notes`,
          ($event.target as HTMLTextAreaElement).value,
          (typed) => patchNotes(index, typed)
        )"
        @change="keystrokes.leave(
          `${index}-notes`,
          ($event.target as HTMLTextAreaElement).value,
          (typed) => patchNotes(index, typed)
        )" />
      <p class="field-help">
        Notes are for whoever edits this campaign; a player never reads them.
      </p>
      <ul v-if="grantProblems(grant, index).length" class="grant-warning"
        role="alert">
        <li v-for="problem in grantProblems(grant, index)" :key="problem">
          {{ problem }}
        </li>
      </ul>
      <button type="button" class="danger" @click="removeGrant(index)">
        Remove {{ grantWord }} {{ index + 1 }}
      </button>
    </fieldset>
    <!-- Offered only while there is something left to stock. A button whose
         only possible outcome is a list the format refuses is a button that
         should not be pressable, and saying why beats going quietly grey. -->
    <button type="button" :disabled="everythingStocked" @click="addGrant">
      Add {{ grantWord }}
    </button>
    <p v-if="everythingStocked" class="field-help">
      Every item this game has is already stocked here. Make another item, or
      change how many of one of these.
    </p>
  </section>
</template>

<style scoped>
.grant-editor {
  display: grid;
  gap: 0.35rem;
  justify-items: start;
}
.grant-entry {
  display: grid;
  gap: 0.35rem;
  justify-self: stretch;
}
.grant-warning {
  color: #8a2020;
}
</style>
