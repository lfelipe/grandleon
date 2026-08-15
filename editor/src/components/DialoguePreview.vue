<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// Plays lines one at a time, the way the game presents a story node: a
// speaker, their words, and a Next button. Purely presentational: nothing
// here edits or persists anything.

import { computed, ref } from "vue";
import {
  backdropGradient,
  sceneBackdrop,
  speakerPortrait,
  type SpeakerPortraitProject
} from "../domain/board-art";
import type { DialogueLine } from "./DialogueLinesEditor.vue";
import type { DialogueCastEntry } from "./DialogueCastEditor.vue";

const assetBase = import.meta.env.BASE_URL;

const props = defineProps<{
  lines: readonly DialogueLine[];
  /**
   * What each line is set against, by position, because a cutscene is several
   * scenes and each names its own backdrop: the preview changes it where the
   * runtime would. Undefined, and a name the library does not hold, is the
   * plain panel, which is what a scene naming no backdrop
   * looks like everywhere else too.
   */
  backgrounds?: readonly (string | undefined)[];
  /**
   * Who this scene's speakers are. The preview draws the character the scene
   * names, and the same drawing the board draws, so an author sees the face
   * the player will. Omitted, and a speaker no entry names, gets no portrait,
   * which is what a client with nothing to draw does too.
   */
  cast?: readonly DialogueCastEntry[];
  /**
   * Who each line's speakers are, by position, for a preview that runs several
   * scenes end to end. A cutscene is several scenes and each casts its own
   * speakers, exactly as each names its own backdrop, so one list over the
   * whole run would answer the wrong scene's question. Where this is given it
   * decides; `cast` above is the one-scene form.
   */
  casts?: readonly (readonly DialogueCastEntry[] | undefined)[];
  /** The project those characters live in, for style, figure and colour. */
  project?: SpeakerPortraitProject | undefined;
}>();

/** -1 means not playing; lines.length means the scene has ended. */
const index = ref(-1);

const current = computed(() =>
  index.value >= 0 ? props.lines[index.value] : undefined
);
const playing = computed(() => index.value >= 0);
const ended = computed(() =>
  playing.value && index.value >= props.lines.length
);
const backdrop = computed(() =>
  index.value >= 0 ? sceneBackdrop(props.backgrounds?.[index.value]) : undefined
);
const stageStyle = computed(() =>
  backdrop.value ? { background: backdropGradient(backdrop.value) } : undefined
);
const portrait = computed(() => {
  const speaker = current.value?.speaker;
  if (speaker === undefined || props.project === undefined) return undefined;
  const inForce = props.casts?.[index.value] ?? props.cast ?? [];
  const entry = inForce.find((cast) => cast.speaker === speaker);
  const sprite = speakerPortrait(props.project, entry?.unitTypeId);
  return sprite === undefined ? undefined : assetBase + sprite;
});
</script>

<template>
  <div class="dialogue-preview">
    <button v-if="!playing" type="button" :disabled="lines.length === 0"
      @click="index = 0">
      Preview
    </button>
    <p v-if="!playing && lines.length === 0" class="field-help">
      There is nothing to preview until somebody speaks.
    </p>

    <div v-if="playing" class="dialogue-preview-stage" :style="stageStyle"
      aria-live="polite">
      <p v-if="backdrop" class="dialogue-preview-backdrop">
        Set against {{ backdrop.label }}
      </p>
      <template v-if="current">
        <img v-if="portrait" class="dialogue-preview-portrait" :src="portrait"
          :alt="`${current.speaker}, as this scene casts them`">
        <p class="dialogue-preview-speaker">{{ current.speaker }}</p>
        <p class="dialogue-preview-text">{{ current.text }}</p>
        <p class="field-help">Line {{ index + 1 }} of {{ lines.length }}</p>
        <button type="button" @click="index += 1">Next</button>
      </template>
      <template v-else-if="ended">
        <p class="dialogue-preview-text">The scene ends.</p>
        <button type="button" @click="index = 0">Watch again</button>
      </template>
      <button type="button" class="secondary" @click="index = -1">
        Close preview
      </button>
    </div>
  </div>
</template>

<style scoped>
.dialogue-preview {
  margin-top: 0.75rem;
}
.dialogue-preview-stage {
  display: grid;
  gap: 0.4rem;
  justify-items: start;
  padding: 0.75rem;
  border-radius: 0.5rem;
  background: #172033;
  color: #ffffff;
}
.dialogue-preview-stage .field-help {
  color: #b8c2d9;
}
/* Named rather than only shown, so the choice is legible to a screen reader
   and to an author judging one backdrop against another. */
.dialogue-preview-backdrop {
  margin: 0;
  font-size: 0.8rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: #d6dcea;
}
/* Pixel art, so it is scaled without smoothing and never asked to be larger
   than the drawing a console holds. */
.dialogue-preview-portrait {
  width: 64px;
  height: 64px;
  image-rendering: pixelated;
}
.dialogue-preview-speaker {
  margin: 0;
  font-weight: 700;
  color: #f2c14e;
}
.dialogue-preview-text {
  margin: 0;
}
</style>
