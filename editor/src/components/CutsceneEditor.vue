<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// A story node's dialogueIds is an ordered sequence: the runtime presents
// every entry in order, so a cutscene is nothing more than a story node with
// several scenes. This edits that sequence as a sequence, with named entries,
// reordering and the lines themselves, rather than as comma-separated
// identifiers.
//
// The sequence belongs to the campaign-flow draft, so ordering changes go up
// through `updateIds` and are saved with the flow. Dialogue records are shared
// project content, so edits to them go up through `updateDialogues` and are
// persisted immediately, the same split WinConditionEditor uses for
// objectives.

import { computed, ref } from "vue";
import { BACKDROPS, type SpeakerPortraitProject } from "../domain/board-art";
import type { SourceDialogue } from "../generated/source-v1";
import type { DialogueCastEntry } from "./DialogueCastEditor.vue";
import DialogueLinesEditor, { type DialogueLine } from "./DialogueLinesEditor.vue";
import DialoguePreview from "./DialoguePreview.vue";

const props = defineProps<{
  dialogueIds: readonly string[];
  dialogues: readonly SourceDialogue[];
  /**
   * The game these scenes are told in, for the faces the preview draws. Without
   * it the preview has nothing to resolve a speaker into and draws nobody,
   * while the Content page beside it, given the same scenes, draws every
   * portrait, and an author cannot tell that difference from the scene's own.
   */
  project?: SpeakerPortraitProject | undefined;
}>();

const emit = defineEmits<{
  updateIds: [ids: string[]];
  updateDialogues: [dialogues: SourceDialogue[]];
}>();

const addSelection = ref("");
const newSceneName = ref("");
/** Index of the entry whose lines are open for editing, or -1. */
const openIndex = ref(-1);

const entries = computed(() =>
  props.dialogueIds.map((id) => ({
    id,
    dialogue: props.dialogues.find((dialogue) => dialogue.id === id)
  }))
);

const previewLines = computed<DialogueLine[]>(() =>
  entries.value.flatMap((entry) => entry.dialogue?.lines ?? [])
);

// The backdrop each previewed line is set against, by position. A cutscene is
// several scenes, so this changes where the runtime would change it rather
// than holding the first scene's choice over the whole preview.
const previewBackgrounds = computed<(string | undefined)[]>(() =>
  entries.value.flatMap((entry) =>
    (entry.dialogue?.lines ?? []).map(() => entry.dialogue?.backgroundId)
  )
);

// Who each previewed line's speakers are, by position, for the same reason the
// backdrops are by position: two scenes played one after the other may answer
// the same speaker name with two different characters, and the preview draws
// whichever scene the line belongs to.
const previewCasts = computed<(readonly DialogueCastEntry[] | undefined)[]>(() =>
  entries.value.flatMap((entry) =>
    (entry.dialogue?.lines ?? []).map(() => entry.dialogue?.cast)
  )
);

/** The menu, as the art library published it: order is index order. */
const backdrops = BACKDROPS;

/** A stored backdrop the art library no longer offers, or undefined. */
function unknownBackdrop(dialogue: SourceDialogue): string | undefined {
  const chosen = dialogue.backgroundId;
  if (!chosen) return undefined;
  return backdrops.some((backdrop) => backdrop.id === chosen)
    ? undefined
    : chosen;
}

function backdropHelp(dialogue: SourceDialogue): string {
  const missing = unknownBackdrop(dialogue);
  if (missing) {
    return `The art library does not offer '${missing}', so this scene is ` +
      "drawn on a plain screen.";
  }
  const chosen = backdrops.find(
    (backdrop) => backdrop.id === dialogue.backgroundId
  );
  return chosen ? chosen.summary : "Drawn on a plain screen.";
}

/**
 * A scene's backdrop, changed. An empty choice deletes the field rather than
 * storing an empty string, so a scene set against nothing reads exactly like
 * one written before backdrops existed.
 */
function updateBackground(dialogueId: string, backgroundId: string) {
  emit("updateDialogues", plainDialogues().map((dialogue) => {
    if (dialogue.id !== dialogueId) return dialogue;
    const { backgroundId: previous, ...rest } = dialogue;
    void previous;
    return backgroundId === ""
      ? rest
      : {
          ...rest,
          backgroundId: backgroundId as NonNullable<
            SourceDialogue["backgroundId"]
          >
        };
  }));
}

function plainDialogues(): SourceDialogue[] {
  return JSON.parse(JSON.stringify(props.dialogues)) as SourceDialogue[];
}

function moveEntry(index: number, delta: number) {
  const target = index + delta;
  if (target < 0 || target >= props.dialogueIds.length) return;
  const ids = [...props.dialogueIds];
  [ids[index], ids[target]] = [ids[target]!, ids[index]!];
  if (openIndex.value === index) openIndex.value = target;
  else if (openIndex.value === target) openIndex.value = index;
  emit("updateIds", ids);
}

function removeEntry(index: number) {
  const ids = props.dialogueIds.filter((_, candidate) => candidate !== index);
  if (openIndex.value === index) openIndex.value = -1;
  else if (openIndex.value > index) openIndex.value -= 1;
  emit("updateIds", ids);
}

/**
 * Scenes this node does not already play, which is all a menu of scenes to add
 * may offer.
 *
 * A node's `dialogueIds` is an ordered sequence and the runtime plays every
 * entry, so naming one twice plays it twice. Worse, `updateLines` finds
 * the scene by identifier, so the two fieldsets drawn for it are two views of
 * one record that cannot be edited apart. There is no reading of "add this
 * scene" that meant either of those.
 */
const unplayed = computed(() =>
  props.dialogues.filter(
    (dialogue) => !props.dialogueIds.includes(dialogue.id)
  )
);

function addExisting() {
  if (addSelection.value === "") return;
  if (props.dialogueIds.includes(addSelection.value)) return;
  emit("updateIds", [...props.dialogueIds, addSelection.value]);
  addSelection.value = "";
}

function slug(value: string): string {
  const cleaned = value
    .toLocaleLowerCase()
    .replace(/[^a-z0-9]+/gu, "_")
    .replace(/^_+|_+$/gu, "");
  return /^[a-z]/u.test(cleaned) ? cleaned : `scene_${cleaned || "1"}`;
}

function uniqueId(base: string): string {
  const taken = new Set(props.dialogues.map((dialogue) => dialogue.id));
  if (!taken.has(base)) return base;
  let suffix = 2;
  while (taken.has(`${base}_${suffix}`)) suffix += 1;
  return `${base}_${suffix}`;
}

function createScene() {
  const name = newSceneName.value.trim() || "New scene";
  const id = uniqueId(slug(name));
  emit("updateDialogues", [...plainDialogues(), { id, name }]);
  emit("updateIds", [...props.dialogueIds, id]);
  newSceneName.value = "";
  openIndex.value = props.dialogueIds.length;
}

function updateLines(dialogueId: string, lines: DialogueLine[]) {
  emit("updateDialogues", plainDialogues().map((dialogue) => {
    if (dialogue.id !== dialogueId) return dialogue;
    const { lines: previous, ...rest } = dialogue;
    void previous;
    // An empty list drops the optional field instead of leaving `lines: []`
    // behind, so the record reads the same as one that never spoke.
    return lines.length > 0 ? { ...rest, lines } : rest;
  }));
}
</script>

<template>
  <section class="cutscene-editor" aria-labelledby="cutscene-title">
    <h4 id="cutscene-title">Scenes played here</h4>
    <p class="field-help">Scenes play top to bottom.</p>
    <p v-if="entries.length === 0" class="field-help" role="status">
      Nothing plays here yet.
    </p>

    <ol class="cutscene-list">
      <li v-for="(entry, index) in entries" :key="`${entry.id}:${index}`">
        <div class="cutscene-row">
          <span class="cutscene-name">
            <strong>{{ entry.dialogue?.name ?? entry.id }}</strong>
            <small v-if="entry.dialogue">
              {{ entry.dialogue.lines?.length ?? 0 }}
              line{{ (entry.dialogue.lines?.length ?? 0) === 1 ? "" : "s" }}
            </small>
          </span>
          <button type="button" :disabled="index === 0"
            @click="moveEntry(index, -1)">
            Move up
          </button>
          <button type="button" :disabled="index === entries.length - 1"
            @click="moveEntry(index, 1)">
            Move down
          </button>
          <button v-if="entry.dialogue" type="button"
            :aria-expanded="openIndex === index"
            @click="openIndex = openIndex === index ? -1 : index">
            {{ openIndex === index ? "Close lines" : "Edit lines" }}
          </button>
          <button type="button" class="danger" @click="removeEntry(index)">
            Remove
          </button>
        </div>
        <p v-if="!entry.dialogue" class="cutscene-warning" role="alert">
          No scene named '{{ entry.id }}' exists in this project. Remove this
          entry, or write the scene under Scenes.
        </p>
        <div v-if="entry.dialogue" class="cutscene-backdrop">
          <label :for="`cutscene-backdrop-${index}`">Set against</label>
          <select :id="`cutscene-backdrop-${index}`"
            :value="entry.dialogue.backgroundId ?? ''"
            @change="updateBackground(
              entry.id, ($event.target as HTMLSelectElement).value
            )">
            <option value="">Nothing: a plain screen</option>
            <option v-for="backdrop in backdrops" :key="backdrop.id"
              :value="backdrop.id">
              {{ backdrop.label }}
            </option>
            <!-- A choice the art library no longer offers stays visible and
                 selected, so an author is told rather than silently moved. -->
            <option v-if="unknownBackdrop(entry.dialogue)"
              :value="entry.dialogue.backgroundId">
              {{ entry.dialogue.backgroundId }}: no longer in the art library
            </option>
          </select>
          <p class="field-help">{{ backdropHelp(entry.dialogue) }}</p>
        </div>
        <DialogueLinesEditor v-if="openIndex === index && entry.dialogue"
          :id-prefix="`cutscene-${index}`"
          :lines="entry.dialogue.lines ?? []"
          :cast-speakers="(entry.dialogue.cast ?? []).map((cast) => cast.speaker)"
          @update="updateLines(entry.id, $event)" />
      </li>
    </ol>

    <div class="cutscene-add">
      <label for="cutscene-add-existing">Add an existing scene</label>
      <select id="cutscene-add-existing" v-model="addSelection">
        <option value="">Choose a scene</option>
        <option v-for="dialogue in unplayed" :key="dialogue.id"
          :value="dialogue.id">
          {{ dialogue.name }} ({{ dialogue.id }})
        </option>
      </select>
      <button type="button"
        :disabled="addSelection === '' || unplayed.length === 0"
        @click="addExisting">
        Add scene
      </button>
      <p v-if="unplayed.length === 0 && dialogues.length > 0" class="field-help">
        Every scene in this game is already played here.
      </p>
    </div>

    <div class="cutscene-add">
      <label for="cutscene-new-name">Or start a fresh one</label>
      <input id="cutscene-new-name" v-model.trim="newSceneName"
        placeholder="The gates open">
      <button type="button" @click="createScene">Write a new scene</button>
    </div>

    <DialoguePreview :lines="previewLines" :backgrounds="previewBackgrounds"
      :casts="previewCasts" :project="project" />
  </section>
</template>

<style scoped>
.cutscene-editor {
  max-width: none;
  margin-top: 0.5rem;
  padding: 0.75rem;
  border: 1px solid #ccd0dc;
  border-radius: 0.5rem;
}
.cutscene-list {
  display: grid;
  gap: 0.5rem;
  margin: 0.5rem 0;
  padding-left: 1.25rem;
}
.cutscene-row {
  display: flex;
  flex-wrap: wrap;
  gap: 0.4rem;
  align-items: center;
}
.cutscene-row .danger {
  margin-top: 0;
}
.cutscene-name {
  display: grid;
  min-width: 8rem;
}
.cutscene-backdrop {
  display: flex;
  flex-wrap: wrap;
  gap: 0.4rem;
  align-items: center;
  margin-top: 0.35rem;
}
.cutscene-backdrop .field-help {
  flex-basis: 100%;
  margin: 0;
}
.cutscene-warning {
  margin: 0.25rem 0 0;
  color: #8d0b1d;
  font-size: 0.85rem;
}
.cutscene-add {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  align-items: center;
  margin-top: 0.5rem;
}
</style>
