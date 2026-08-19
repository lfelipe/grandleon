<script setup lang="ts">
// SPDX-License-Identifier: MIT
// What is said while a Stage is being fought, authored as the sentences it is.
//
// A node's own scenes play on arrival, before the node acts, which is around a
// battle. These are inside one, and there are three occasions because a battle
// reports three events worth speaking over: the board being drawn, a character
// talked off it, and a character defeated.
//
// **Leaving and dying are different facts**, and the engine keeps them apart -
// `unit_talked` is emitted instead of `unit_defeated`, never beside it - so
// they are offered as two occasions rather than one with a switch on it.
//
// Nothing here types an identifier. An occasion is a button that says what it
// is, the character is a menu of who stands on this board, and the scene is a
// menu of the game's own scenes with a way to make one that is missing.
import { computed, ref } from "vue";
import type {
  EncounterMoment,
  EncounterPlacement,
  SourceDialogue
} from "../generated/source-v1";

const props = defineProps<{
  moments: readonly EncounterMoment[];
  placements: readonly EncounterPlacement[];
  dialogues: readonly SourceDialogue[];
  /** Names a character by what the author called them, not by their key. */
  unitTypeName: (id: string) => string | undefined;
}>();

const emit = defineEmits<{
  update: [moments: EncounterMoment[]];
  updateDialogues: [dialogues: SourceDialogue[]];
}>();

/** A new scene's name, held while it is being typed. */
const newSceneName = ref("");

type Occasion = EncounterMoment["when"]["kind"];

/**
 * The three occasions, as the sentence each one is.
 *
 * Written as sentences rather than as a menu of field values for the reason the
 * ways to win are: what an author is choosing is when somebody speaks, and
 * "characterFalls" is not a thing anybody says.
 */
const OCCASIONS: { kind: Occasion; verb: string; about: boolean }[] = [
  { kind: "stageOpens", verb: "when the Stage opens", about: false },
  { kind: "characterTalked", verb: "when somebody is talked to", about: true },
  { kind: "characterFalls", verb: "when somebody falls", about: true }
];

/** Everybody standing on this board, for the menu of who a moment is about. */
const cast = computed(() =>
  props.placements.map((placement) => ({
    id: placement.id,
    label:
      placement.name ??
      props.unitTypeName(placement.unitTypeId) ??
      placement.unitTypeId
  }))
);

function occasionOf(moment: EncounterMoment) {
  return OCCASIONS.find((entry) => entry.kind === moment.when.kind);
}

/** What a moment says it is, in one line an author reads rather than parses. */
function sentence(moment: EncounterMoment): string {
  const occasion = occasionOf(moment);
  const scene =
    props.dialogues.find((dialogue) => dialogue.id === moment.dialogueId);
  const said = scene?.name ?? moment.dialogueId;
  if (occasion === undefined) return said;
  if (!occasion.about) return `${said}, ${occasion.verb}.`;
  const who = cast.value.find((entry) => entry.id === moment.when.placementId);
  // A moment about somebody who has left the board is the one sentence here an
  // author would otherwise trust: the board refuses to open on it, and the
  // words would simply never be heard.
  const name = who?.label ?? "nobody on this board";
  const verb = moment.when.kind === "characterTalked"
    ? "is talked to"
    : "falls";
  return `${said}, when ${name} ${verb}.`;
}

function danglingAbout(moment: EncounterMoment): boolean {
  if (occasionOf(moment)?.about !== true) return false;
  const named = moment.when.placementId;
  if (named === undefined) return true;
  return !cast.value.some((entry) => entry.id === named);
}

function uniqueId(base: string): string {
  const taken = new Set(props.moments.map((moment) => moment.id));
  if (!taken.has(base)) return base;
  let suffix = 2;
  while (taken.has(`${base}_${suffix}`)) suffix += 1;
  return `${base}_${suffix}`;
}

function slug(text: string): string {
  return text.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "")
    || "moment";
}

function plain(): EncounterMoment[] {
  return props.moments.map((moment) => ({
    ...moment,
    when: { ...moment.when }
  }));
}

function add(occasion: Occasion) {
  const scene = props.dialogues[0];
  if (scene === undefined) return;
  const next: EncounterMoment = {
    id: uniqueId(slug(`${occasion} ${props.moments.length + 1}`)),
    when: occasion === "stageOpens"
      ? { kind: occasion }
      : { kind: occasion, placementId: cast.value[0]?.id ?? "" },
    dialogueId: scene.id
  };
  emit("update", [...plain(), next]);
}

function setScene(index: number, dialogueId: string) {
  const next = plain();
  const moment = next[index];
  if (moment === undefined) return;
  moment.dialogueId = dialogueId;
  emit("update", next);
}

function setAbout(index: number, placementId: string) {
  const next = plain();
  const moment = next[index];
  if (moment === undefined) return;
  moment.when = { ...moment.when, placementId };
  emit("update", next);
}

function remove(index: number) {
  const next = plain();
  next.splice(index, 1);
  emit("update", next);
}

/**
 * A scene made from here, because the alternative is leaving the Stage to go
 * and make one and coming back. It is added to the game and chosen at once.
 */
function createScene(index: number) {
  const name = newSceneName.value.trim() || "New scene";
  const taken = new Set(props.dialogues.map((dialogue) => dialogue.id));
  let id = slug(name);
  let suffix = 2;
  while (taken.has(id)) id = `${slug(name)}_${suffix++}`;
  emit("updateDialogues", [
    ...props.dialogues.map((dialogue) => ({ ...dialogue })),
    { id, name }
  ]);
  setScene(index, id);
  newSceneName.value = "";
}
</script>

<template>
  <section class="stage-moments" aria-labelledby="stage-moments-title">
    <h5 id="stage-moments-title">While the fighting is on</h5>
    <p class="field-help">
      What is said while the fighting is on, rather than on the way in or out.
    </p>

    <p v-if="dialogues.length === 0" class="field-help">
      This game has no scenes yet. Make one above and it can be said here.
    </p>

    <ol v-if="moments.length" class="moment-list">
      <li v-for="(moment, index) in moments" :key="moment.id">
        <p class="moment-summary" role="status">{{ sentence(moment) }}</p>
        <p v-if="danglingAbout(moment)" class="moment-warning" role="alert">
          Nobody by that name stands on this Stage, so this is never heard and
          the board will not open.
        </p>
        <div class="moment-fields">
          <label :for="`moment-${index}-scene`">What is said</label>
          <select :id="`moment-${index}-scene`" :value="moment.dialogueId"
            @change="setScene(
              index, ($event.target as HTMLSelectElement).value
            )">
            <option v-for="scene in dialogues" :key="scene.id" :value="scene.id">
              {{ scene.name ?? scene.id }}
            </option>
          </select>

          <template v-if="occasionOf(moment)?.about">
            <label :for="`moment-${index}-about`">Who it is about</label>
            <select :id="`moment-${index}-about`"
              :value="moment.when.placementId ?? ''"
              @change="setAbout(
                index, ($event.target as HTMLSelectElement).value
              )">
              <option v-for="who in cast" :key="who.id" :value="who.id">
                {{ who.label }}
              </option>
            </select>
          </template>

          <div class="moment-verbs">
            <input :id="`moment-${index}-new-scene`" v-model="newSceneName"
              maxlength="80" placeholder="A name for a new scene">
            <button type="button" class="secondary" @click="createScene(index)">
              Make a scene and say that instead
            </button>
            <button type="button" class="danger" @click="remove(index)">
              Take this out
            </button>
          </div>
        </div>
      </li>
    </ol>

    <div class="moment-add" role="group" aria-label="Say something while the fighting is on">
      <button v-for="occasion in OCCASIONS" :key="occasion.kind" type="button"
        class="secondary" :disabled="dialogues.length === 0"
        :data-occasion="occasion.kind" @click="add(occasion.kind)">
        Say something {{ occasion.verb }}
      </button>
    </div>
  </section>
</template>

<style scoped>
.stage-moments {
  margin: 0.75rem 0;
  padding: 0.75rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.65rem;
  background: #f5f7f2;
}

.moment-list {
  display: grid;
  gap: 0.6rem;
  margin: 0.5rem 0;
  padding-left: 1.4rem;
}

.moment-summary {
  margin: 0 0 0.25rem;
  font-weight: 600;
}

.moment-warning {
  margin: 0 0 0.25rem;
  color: #8a2b2b;
}

.moment-fields {
  display: grid;
  gap: 0.3rem;
  justify-items: start;
}

.moment-fields > select {
  justify-self: stretch;
  max-width: 28rem;
}

.moment-verbs,
.moment-add {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
}
</style>
