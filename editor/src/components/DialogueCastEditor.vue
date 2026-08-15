<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// Who each speaker in a scene actually is.
//
// A speaker is a display name, what a player reads, and it says nothing
// about which character is talking. This is where that is said, once for the
// scene rather than on every line, so that a client which draws a face draws
// the character rather than guessing from the words.
//
// The speaker side is a chooser rather than a free text field, offering the
// speakers this scene's lines actually use. That is deliberate: the join is by
// exact string, so a typed speaker that differs by a letter would be a cast
// entry speaking no line, the one mistake this field makes that has no
// symptom except the wrong drawing.

import { computed } from "vue";

export interface DialogueCastEntry {
  speaker: string;
  unitTypeId: string;
}

const props = defineProps<{
  cast: readonly DialogueCastEntry[];
  /** The speakers this scene's lines use, in the order they first speak. */
  speakers: readonly string[];
  unitTypes: readonly { readonly id: string; readonly name: string }[];
  /** Distinguishes control ids when several cast editors share a page. */
  idPrefix: string;
}>();

const emit = defineEmits<{
  update: [cast: DialogueCastEntry[]];
}>();

function copyCast(): DialogueCastEntry[] {
  return props.cast.map((entry) => ({ ...entry }));
}

/** Speakers nobody has been named for yet, which is what a new entry offers. */
const uncast = computed(() =>
  props.speakers.filter(
    (speaker) => !props.cast.some((entry) => entry.speaker === speaker)
  )
);

const canAdd = computed(
  () => uncast.value.length > 0 && props.unitTypes.length > 0
);

function patchEntry(index: number, change: Partial<DialogueCastEntry>) {
  const cast = copyCast();
  const entry = cast[index];
  if (!entry) return;
  cast[index] = { ...entry, ...change };
  emit("update", cast);
}

function addEntry() {
  const speaker = uncast.value[0];
  const unitType = props.unitTypes[0];
  if (speaker === undefined || unitType === undefined) return;
  emit("update", [...copyCast(), { speaker, unitTypeId: unitType.id }]);
}

function removeEntry(index: number) {
  const cast = copyCast();
  cast.splice(index, 1);
  emit("update", cast);
}

function unitTypeName(id: string): string | undefined {
  return props.unitTypes.find((unitType) => unitType.id === id)?.name;
}

/**
 * The identity of a character this project no longer holds, or undefined. Kept
 * as a disabled option rather than dropped, so that a renamed or deleted
 * character is visible in the control instead of silently becoming nobody.
 */
function missingUnitType(entry: DialogueCastEntry): string | undefined {
  if (entry.unitTypeId === "") return undefined;
  return unitTypeName(entry.unitTypeId) === undefined
    ? entry.unitTypeId
    : undefined;
}

/**
 * The same for a speaker: an entry naming a speaker no line uses any more is
 * what renaming a speaker leaves behind, and its only other symptom is the old
 * drawing.
 */
function missingSpeaker(entry: DialogueCastEntry): boolean {
  return !props.speakers.includes(entry.speaker);
}

/** The speakers this entry may name: the free ones, plus its own. */
function speakerChoices(entry: DialogueCastEntry): readonly string[] {
  return props.speakers.filter(
    (speaker) =>
      speaker === entry.speaker ||
      !props.cast.some((other) => other.speaker === speaker)
  );
}
</script>

<template>
  <div class="dialogue-cast">
    <p v-if="unitTypes.length === 0" class="field-help" role="status">
      No characters yet, so there is nobody a speaker could be.
    </p>
    <p v-else-if="cast.length === 0" class="field-help">
      Nobody is named yet.
    </p>
    <fieldset v-for="(entry, index) in cast" :key="index" class="cast-entry">
      <legend>Speaker {{ index + 1 }}</legend>
      <label :for="`${idPrefix}-cast-${index}-speaker`">Who speaks</label>
      <select :id="`${idPrefix}-cast-${index}-speaker`" :value="entry.speaker"
        @change="patchEntry(index, {
          speaker: ($event.target as HTMLSelectElement).value
        })">
        <option v-if="missingSpeaker(entry)" :value="entry.speaker" disabled>
          {{ entry.speaker }}: speaks no line in this scene
        </option>
        <option v-for="speaker in speakerChoices(entry)" :key="speaker"
          :value="speaker">
          {{ speaker }}
        </option>
      </select>
      <label :for="`${idPrefix}-cast-${index}-unit-type`">
        Which character they are
      </label>
      <select :id="`${idPrefix}-cast-${index}-unit-type`"
        :value="entry.unitTypeId"
        @change="patchEntry(index, {
          unitTypeId: ($event.target as HTMLSelectElement).value
        })">
        <option v-if="entry.unitTypeId === ''" value="" disabled>
          Choose a character
        </option>
        <option v-if="missingUnitType(entry)" :value="entry.unitTypeId"
          disabled>
          {{ missingUnitType(entry) }}: not a character in this project
        </option>
        <option v-for="unitType in unitTypes" :key="unitType.id"
          :value="unitType.id">
          {{ unitType.name }} ({{ unitType.id }})
        </option>
      </select>
      <button type="button" class="danger" @click="removeEntry(index)">
        Remove speaker {{ index + 1 }}
      </button>
    </fieldset>
    <button type="button" :disabled="!canAdd" @click="addEntry">
      Name a speaker
    </button>
    <p v-if="!canAdd && unitTypes.length > 0" class="field-help">
      Every speaker in this scene has been named.
    </p>
  </div>
</template>

<style scoped>
.dialogue-cast {
  display: grid;
  gap: 0.5rem;
}
.cast-entry {
  display: grid;
  gap: 0.35rem;
}
.dialogue-cast > button {
  justify-self: start;
}
</style>
