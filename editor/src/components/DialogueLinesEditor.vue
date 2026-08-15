<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// A dialogue's lines, edited as the ordered list they are rather than as raw
// JSON. Every change is emitted whole so the parent decides where it persists:
// the record editor saves through the project session, the cutscene editor
// routes it up through the campaign surface.
//
// **The words on screen are the draft, and the draft is this component's.** A
// keystroke lands in `draft` the moment it is typed, the controls show `draft`
// rather than the stored line, and `flush` is what a Save reaches. That is the
// contract `SchemaRecordForm` keeps, and it is kept here for the same two
// reasons.
//
// One: work in progress has to be visible to the rest of the editor. Committing
// only when the browser fires `change` means the project calls itself saved and
// the close-the-tab guard stays quiet while a line the author is halfway
// through sits in the field, and a field is not a place work is safe.
//
// Two: a control bound straight at the stored line cannot survive a redraw. The
// list is drawn again whenever anything writes the record, on a sibling save,
// an undo or another surface, and Vue re-applies `value` on every redraw, so the
// stored words go back into the control under the cursor. No `input` announces
// it, and because the field then matches what it held when it was focused, no
// `change` fires on the way out either. Bound at `draft`, the redraw writes
// what is already there and the typing stands.

import { ref, watch } from "vue";

export interface DialogueLine {
  speaker: string;
  text: string;
}

const props = defineProps<{
  lines: readonly DialogueLine[];
  /** Distinguishes control ids when several line editors share a page. */
  idPrefix: string;
}>();

const emit = defineEmits<{
  update: [lines: DialogueLine[]];
  /** A keystroke that is not in the project yet, so the header can say so. */
  dirty: [];
}>();

function copyLines(lines: readonly DialogueLine[]): DialogueLine[] {
  return lines.map((line) => ({ ...line }));
}

const draft = ref<DialogueLine[]>(copyLines(props.lines));
/** What the draft was last taken from, for telling an author's edit from the
 *  parent's answer to it. */
let adopted = JSON.stringify(props.lines);

/**
 * Takes what the parent now holds, keeping keystrokes it has not answered.
 *
 * The parent's list changes for two reasons: because this component asked it
 * to, and because something else wrote the record. The first must not be undone
 * by the second, and a field the author has typed into that the incoming list
 * says nothing new about is work that is still theirs. A change in length is a
 * structural edit, a line added, removed or moved, and the incoming order is
 * taken whole, because there is no honest way to line the two up.
 */
watch(
  () => props.lines,
  (lines) => {
    const serialized = JSON.stringify(lines);
    if (serialized === adopted) return;
    const previous = JSON.parse(adopted) as DialogueLine[];
    const incoming = copyLines(lines);
    if (incoming.length === previous.length
      && draft.value.length === previous.length) {
      incoming.forEach((line, index) => {
        const was = previous[index]!;
        const typed = draft.value[index]!;
        for (const field of ["speaker", "text"] as const) {
          const edited = typed[field] !== was[field];
          const answered = line[field] !== was[field];
          if (edited && !answered) line[field] = typed[field];
        }
      });
    }
    adopted = serialized;
    draft.value = incoming;
  },
  { deep: true }
);

function commit() {
  adopted = JSON.stringify(draft.value);
  emit("update", copyLines(draft.value));
}

/** Holds a keystroke, without spending an undo step on every letter. */
function typeInto(index: number, change: Partial<DialogueLine>) {
  const line = draft.value[index];
  if (!line) return;
  draft.value[index] = { ...line, ...change };
  emit("dirty");
}

/**
 * Leaves a field, which is one undoable edit.
 *
 * The control's value is read here as well as on the way in, rather than
 * trusting that an `input` was seen first. A commit that works only when the
 * event it expects came before it is the same fragility one layer down.
 */
function leaveField(index: number, change: Partial<DialogueLine>) {
  typeInto(index, change);
  flush();
}

/** Commits a pending edit; called when a field is left, and by a Save. */
function flush(): boolean {
  if (JSON.stringify(draft.value) !== adopted) commit();
  return true;
}

defineExpose({ flush });

function addLine() {
  // The schema requires both fields to be non-empty, so a fresh line starts
  // sayable rather than invalid.
  draft.value.push({
    speaker: draft.value.at(-1)?.speaker ?? "Narrator",
    text: "…"
  });
  commit();
}

function removeLine(index: number) {
  draft.value.splice(index, 1);
  commit();
}

function moveLine(index: number, delta: number) {
  const target = index + delta;
  if (target < 0 || target >= draft.value.length) return;
  const lines = draft.value;
  [lines[index], lines[target]] = [lines[target]!, lines[index]!];
  commit();
}
</script>

<template>
  <div class="dialogue-lines">
    <p v-if="draft.length === 0" class="field-help">
      Nobody speaks yet. Add the first line.
    </p>
    <fieldset v-for="(line, index) in draft" :key="index" class="dialogue-line">
      <legend>Line {{ index + 1 }}</legend>
      <label :for="`${idPrefix}-line-${index}-speaker`">Who speaks</label>
      <!-- `input` holds the keystroke and `change` commits it. Both, because
           they answer different questions: whether the editor knows there is
           work in progress, and when that work becomes a step an author can
           undo. -->
      <input :id="`${idPrefix}-line-${index}-speaker`" :value="line.speaker"
        maxlength="160" required
        @input="typeInto(index, {
          speaker: ($event.target as HTMLInputElement).value
        })"
        @change="leaveField(index, {
          speaker: ($event.target as HTMLInputElement).value
        })">
      <label :for="`${idPrefix}-line-${index}-text`">What they say</label>
      <textarea :id="`${idPrefix}-line-${index}-text`" :value="line.text"
        maxlength="4096" required rows="2"
        @input="typeInto(index, {
          text: ($event.target as HTMLTextAreaElement).value
        })"
        @change="leaveField(index, {
          text: ($event.target as HTMLTextAreaElement).value
        })" />
      <div class="dialogue-line-commands" role="group"
        :aria-label="`Reorder or remove line ${index + 1}`">
        <button type="button" :disabled="index === 0"
          @click="moveLine(index, -1)">
          Move line up
        </button>
        <button type="button" :disabled="index === draft.length - 1"
          @click="moveLine(index, 1)">
          Move line down
        </button>
        <button type="button" class="danger" @click="removeLine(index)">
          Remove line
        </button>
      </div>
    </fieldset>
    <button type="button" @click="addLine">Add a line</button>
  </div>
</template>

<style scoped>
.dialogue-lines {
  display: grid;
  gap: 0.5rem;
}
.dialogue-line {
  display: grid;
  gap: 0.35rem;
}
.dialogue-line-commands {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
}
.dialogue-line-commands .danger {
  margin-top: 0;
}
.dialogue-lines > button {
  justify-self: start;
}
</style>
