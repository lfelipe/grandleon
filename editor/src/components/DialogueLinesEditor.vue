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
  /**
   * Who this scene has cast, if anybody, so a line can be given one of them
   * rather than typed at.
   *
   * A cast entry is joined to a line by its speaker string, exactly and case
   * sensitively, and that join is what puts a face on the screen. Two places
   * spelling the same person is a face lost to a capital letter, and nothing
   * says so: the scene still plays, the words are still right, and the portrait
   * is simply the fallback. Offering the names the cast already holds is how
   * the two agree by construction rather than by careful typing.
   */
  castSpeakers?: readonly string[];
}>();

const emit = defineEmits<{
  update: [lines: DialogueLine[]];
  /** A keystroke that is not in the project yet, so the header can say so. */
  dirty: [];
}>();

/**
 * Whether this line names somebody the scene has not cast.
 *
 * Silent about a line nobody has named yet: a line being written is not a
 * mistake, and a warning that appears on the first keystroke is a warning an
 * author learns to ignore. Silent too when the scene casts nobody at all, which
 * is a scene that shows no faces by choice rather than by accident.
 */
function uncast(speaker: string): boolean {
  const cast = props.castSpeakers;
  if (cast === undefined || cast.length === 0) return false;
  const named = speaker.trim();
  if (named.length === 0) return false;
  return !cast.includes(named);
}

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
        maxlength="160" required :list="`${idPrefix}-cast`"
        :aria-describedby="uncast(line.speaker)
          ? `${idPrefix}-line-${index}-uncast` : undefined"
        @input="typeInto(index, {
          speaker: ($event.target as HTMLInputElement).value
        })"
        @change="leaveField(index, {
          speaker: ($event.target as HTMLInputElement).value
        })">
      <!-- Said where the name was typed, and only once a name has been. A face
           is drawn for a speaker this scene has cast and not for one it has
           not, which is a thing an author should learn here rather than on a
           console. -->
      <p v-if="uncast(line.speaker)" :id="`${idPrefix}-line-${index}-uncast`"
        class="field-help line-uncast">
        Nobody is cast as {{ line.speaker }}, so this line shows no face.
      </p>
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

    <!-- The cast, offered rather than remembered. A datalist and not a select,
         because a name this scene has not cast is a legitimate thing to type:
         it is how somebody new arrives, and casting them is the next gesture
         rather than a precondition for this one. -->
    <datalist v-if="castSpeakers && castSpeakers.length" :id="`${idPrefix}-cast`">
      <option v-for="speaker in castSpeakers" :key="speaker" :value="speaker" />
    </datalist>
  </div>
</template>

<style scoped>
.dialogue-lines {
  display: grid;
  gap: 0.5rem;
}
/* A conversation should read like one. Each line laid its two fields and its
 * three buttons out one under another, so ten lines of dialogue photographed as
 * ten forms and an author scrolled a page to read a scene they could have read
 * at a glance. Wide enough, the name sits beside the words it belongs to; too
 * narrow for that, it goes back to stacking, which is the only thing that fits.
 *
 * The container is asked, not the window: this editor sits inside a rail, a
 * record list and a section, and what the window is wide enough for says
 * nothing about what is left over here.
 */
.dialogue-lines {
  container-type: inline-size;
}

.dialogue-line {
  display: grid;
  gap: 0.35rem;
}

@container (min-width: 32rem) {
  .dialogue-line {
    grid-template-columns: 11rem minmax(0, 1fr);
    align-items: start;
  }

  /* The label of a field on the right belongs over that field, not over the
     name to its left, so each label takes the column its control does. */
  .dialogue-line > label:first-of-type {
    grid-column: 1;
  }

  .dialogue-line > input {
    grid-column: 1;
  }

  .dialogue-line > label:nth-of-type(2),
  .dialogue-line > textarea,
  .dialogue-line > .line-uncast,
  .dialogue-line > .dialogue-line-commands {
    grid-column: 2;
  }
}

/* Said quietly. A face that will not be drawn is worth knowing and is not an
   error: the scene plays, and the words are still the words. */
.line-uncast {
  margin: 0;
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
