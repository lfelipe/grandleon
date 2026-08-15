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

const anyItems = computed(() => props.items.length > 0);

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
function addGrant() {
  keystrokes.flush();
  const grants = copyGrants();
  // A fresh grant starts as one of the first item in the project rather than as
  // an empty form. One is the quantity an author is most likely to keep and the
  // one they will most obviously change.
  grants.push({ itemId: props.items[0]?.id ?? "", quantity: 1 });
  emit("update", grants);
}

function removeGrant(index: number) {
  keystrokes.flush();
  const grants = copyGrants();
  grants.splice(index, 1);
  emit("update", grants);
}

function itemName(id: string): string | undefined {
  return props.items.find((item) => item.id === id)?.name;
}

/** The items offered for one grant: the search, plus its own choice. */
function itemChoices(
  grant: CampaignItemGrant
): readonly { readonly id: string; readonly name: string }[] {
  const query = search.value.trim().toLocaleLowerCase();
  if (query === "") return props.items;
  return props.items.filter((item) =>
    item.id === grant.itemId ||
    item.id.toLocaleLowerCase().includes(query) ||
    item.name.toLocaleLowerCase().includes(query)
  );
}

/** A stored item this project does not hold, named rather than hidden. */
function missingItem(grant: CampaignItemGrant): string | undefined {
  if (grant.itemId === "") return undefined;
  return itemName(grant.itemId) === undefined ? grant.itemId : undefined;
}

/** Everything wrong with one grant, beside the grant, in plain words. */
function grantProblems(grant: CampaignItemGrant, index: number): string[] {
  const problems: string[] = [];
  if (grant.itemId === "") {
    problems.push("Choose which item this puts in the store.");
  } else if (missingItem(grant) !== undefined) {
    problems.push(
      `'${grant.itemId}' is not an item in this project. Choose another, or ` +
      "create the item."
    );
  } else if (
    props.grants.some(
      (candidate, candidateIndex) =>
        candidateIndex !== index && candidate.itemId === grant.itemId
    )
  ) {
    problems.push(
      `This list already stocks '${grant.itemId}'. Say how many once: two ` +
      "entries for one item are two different answers to one question."
    );
  }
  if (!Number.isInteger(grant.quantity) || grant.quantity < 1) {
    problems.push("Say how many, as a whole number of at least 1.");
  } else if (grant.quantity > 65535) {
    problems.push("65535 is the most of one item a store can be given at once.");
  }
  return problems;
}
</script>

<template>
  <section class="grant-editor" :aria-labelledby="`${idPrefix}-title`">
    <h4 :id="`${idPrefix}-title`">{{ heading }}</h4>
    <p class="field-help">{{ help }}</p>

    <p v-if="!anyItems" class="grant-warning" role="status">
      This project has no items yet, so there is nothing to give. Create an item
      first.
    </p>
    <template v-else>
      <label :for="`${idPrefix}-item-search`">Search items</label>
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
      <label :for="`${idPrefix}-${index}-item`">Which item</label>
      <select :id="`${idPrefix}-${index}-item`" :value="grant.itemId"
        @change="patchGrant(index, {
          itemId: ($event.target as HTMLSelectElement).value
        })">
        <option v-if="grant.itemId === ''" value="" disabled>
          Choose an item
        </option>
        <option v-if="missingItem(grant)" :value="grant.itemId" disabled>
          {{ missingItem(grant) }}: not an item in this project
        </option>
        <option v-for="item in itemChoices(grant)" :key="item.id"
          :value="item.id">
          {{ item.name }} ({{ item.id }})
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
    <button type="button" @click="addGrant">Add {{ grantWord }}</button>
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
