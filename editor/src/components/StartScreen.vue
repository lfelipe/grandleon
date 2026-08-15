<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// The way in.
//
// This screen asks the author what they want to do, in the words of the thing
// being chosen rather than the verb that does it: start a new game, start from
// one of the examples, open a project file you already have. It is a screen of
// choices, not a toolbar: a flat header row of buttons asks nothing, and it
// puts the command that throws your game away next to the one that plays it.

import { ref } from "vue";
import { sampleProjects } from "../sample-projects";

const props = defineProps<{
  /** True when there is already a game open behind this screen to go back to.
   *  Absent on the very first visit, where there is nothing to keep. */
  hasOpenProject?: boolean;
  /** What the open game is called, so going back names what you go back to. */
  openProjectTitle?: string;
  busy?: boolean;
}>();

const emit = defineEmits<{
  newProject: [];
  loadSample: [id: string];
  openProject: [];
  keepEditing: [];
}>();

const chosenSample = ref(sampleProjects[0]!.id);
</script>

<template>
  <section id="start" class="start-screen" aria-labelledby="start-title">
    <p class="eyebrow">Grandleon</p>
    <h2 id="start-title">Make a game</h2>

    <ul class="start-options">
      <li v-if="props.hasOpenProject" class="start-option start-option-primary">
        <h3>Keep editing</h3>
        <p>
          <strong>{{ props.openProjectTitle || "the game you have open" }}</strong>
        </p>
        <button type="button" class="start-go" :disabled="props.busy"
          @click="emit('keepEditing')">
          Keep editing
        </button>
      </li>

      <li class="start-option"
        :class="{ 'start-option-primary': !props.hasOpenProject }">
        <h3>Start a new game</h3>
        <button type="button" class="start-go" :disabled="props.busy"
          @click="emit('newProject')">
          Start a new game
        </button>
      </li>

      <li class="start-option">
        <h3>Start from an example</h3>
        <!-- The examples are one choice, so they are one group with a name:
             a set of radios sharing a `name` and nothing else leaves a screen
             reader reading four unrelated buttons. -->
        <fieldset class="sample-list">
          <legend class="visually-hidden">Which example to open</legend>
          <div v-for="sample in sampleProjects" :key="sample.id">
            <label>
              <input v-model="chosenSample" type="radio" name="start-sample"
                :value="sample.id">
              <span>
                <strong>{{ sample.title }}</strong>
                <small>{{ sample.summary }}</small>
              </span>
            </label>
          </div>
        </fieldset>
        <button type="button" class="start-go" :disabled="props.busy"
          @click="emit('loadSample', chosenSample)">
          Open this example
        </button>
      </li>

      <li class="start-option">
        <h3>Open a project file</h3>
        <button type="button" class="start-go" :disabled="props.busy"
          @click="emit('openProject')">
          Open a project file
        </button>
      </li>
    </ul>
  </section>
</template>

<style scoped>
.start-screen {
  max-width: 62rem;
  margin: 0 auto;
  padding: 1rem;
}
.start-options {
  display: grid;
  align-items: start;
  grid-template-columns: repeat(auto-fit, minmax(17rem, 1fr));
  gap: 1rem;
  margin: 1.25rem 0 0;
  padding: 0;
  list-style: none;
}
.start-option {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  padding: 1rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.65rem;
  background: #f5f7f2;
}
.start-option-primary {
  border-color: #2e9e5b;
  border-width: 2px;
  padding: calc(1rem - 1px);
  background: #eaf6ee;
}
.start-option h3 {
  margin: 0;
}
.start-option p {
  flex: 1 1 auto;
  margin: 0;
  font-size: 0.9rem;
  line-height: 1.4;
  color: #46524a;
}
.start-go {
  align-self: flex-start;
  background: #2e9e5b;
  color: #ffffff;
  font-weight: 700;
}
.sample-list {
  display: grid;
  gap: 0.35rem;
  margin: 0;
  padding: 0;
  border: 0;
}
.sample-list label {
  display: flex;
  gap: 0.5rem;
  align-items: baseline;
}
.sample-list span {
  display: grid;
}
.sample-list small {
  font-size: 0.8rem;
  color: #46524a;
}
</style>
