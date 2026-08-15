<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import type { TargetNote } from "../domain/target-budget";

export interface PresentedDiagnostic {
  readonly severity: "error" | "warning";
  readonly code: string;
  readonly sourcePath: string;
  readonly instancePath: string;
  readonly message: string;
}

// Console notes sit in this panel rather than in a surface of their own,
// because an author asking "is my game all right?" should find one place that
// answers. They are kept out of the problem list, and out of the count beside
// it, on purpose: a game that overruns a console is not a broken game, and
// nothing here has to be fixed before saving, exporting, or playing.
withDefaults(defineProps<{
  diagnostics: readonly PresentedDiagnostic[];
  targetNotes?: readonly TargetNote[];
  /** What this panel calls itself. A surface that is already titled
   *  "Diagnostics" gives it a different name rather than saying it twice. */
  heading?: string;
}>(), { heading: "Diagnostics" });

defineEmits<{
  navigate: [diagnostic: PresentedDiagnostic];
}>();
</script>

<template>
  <section id="diagnostics" aria-labelledby="diagnostics-title">
    <div class="diagnostic-heading">
      <div>
        <p class="eyebrow">Project health</p>
        <h2 id="diagnostics-title">{{ heading }}</h2>
      </div>
      <p role="status" aria-live="polite" aria-atomic="true">
        {{ diagnostics.length === 0
          ? "No problems found"
          : `${diagnostics.length} ${diagnostics.length === 1 ? "problem" : "problems"} found` }}
      </p>
    </div>

    <p v-if="diagnostics.length === 0">
      Validate the project to check schemas, identifiers, references, and map shapes.
    </p>
    <ol v-else class="diagnostic-list">
      <li v-for="diagnostic in diagnostics"
        :key="`${diagnostic.code}:${diagnostic.sourcePath}:${diagnostic.instancePath}`">
        <span class="diagnostic-severity">{{ diagnostic.severity }}</span>
        <strong>{{ diagnostic.message }}</strong>
        <code>{{ diagnostic.code }}</code>
        <button type="button" @click="$emit('navigate', diagnostic)">
          Go to {{ diagnostic.sourcePath }}{{ diagnostic.instancePath }}
        </button>
      </li>
    </ol>

    <div v-if="targetNotes && targetNotes.length > 0" class="target-notes">
      <h3>On an old console</h3>
      <p>
        Only what an old machine would make of it. Nothing here stops the game
        playing in this editor, in a browser or on the desktop.
      </p>
      <ul>
        <li v-for="note in targetNotes" :key="`${note.targetId}:${note.code}`">
          {{ note.message }}
        </li>
      </ul>
    </div>
  </section>
</template>

<style scoped>
.target-notes {
  margin-top: 1.5rem;
  padding: 0.75rem 1rem;
  border: 1px solid #b8c2cc;
  border-radius: 0.5rem;
}
.target-notes h3 {
  margin: 0 0 0.25rem;
}
.target-notes p {
  margin: 0 0 0.75rem;
}
.target-notes ul {
  display: grid;
  gap: 0.75rem;
  margin: 0;
  padding-left: 1.5rem;
}
</style>
