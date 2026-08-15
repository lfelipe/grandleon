<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { ref } from "vue";

const name = ref("");
const saved = ref("");
const dialog = ref<HTMLDialogElement>();
const opener = ref<HTMLButtonElement>();

function closeDialog() {
  dialog.value?.close();
  opener.value?.focus();
}
</script>

<template>
  <a class="skip-link" href="#main">Skip to editor</a>
  <header>
    <h1>Grandleon Editor</h1>
    <p>Training Project · Unsaved changes</p>
    <nav aria-label="Project">
      <ul><li>Content</li><li>Maps</li><li>Diagnostics</li></ul>
    </nav>
  </header>
  <main id="main" tabindex="-1">
    <h2>Project settings</h2>
    <p v-if="!name" id="errors"><a href="#project-name">Project name is required</a></p>
    <form @submit.prevent="saved = 'Project saved'">
      <label for="project-name">Project name</label>
      <input id="project-name" v-model="name" :aria-invalid="!name"
        :aria-describedby="!name ? 'errors' : undefined">
      <label for="target">Target profile</label>
      <select id="target"><option>Portable</option><option>Desktop</option></select>
      <button type="submit">Save</button>
      <button type="button" ref="opener" @click="dialog?.showModal()">
        Open command help
      </button>
    </form>
    <p aria-live="polite">{{ saved }}</p>
  </main>
  <dialog ref="dialog" @cancel="closeDialog">
    <h2>Command help</h2>
    <p>Use the project navigation to choose an editor.</p>
    <button autofocus @click="closeDialog">Close</button>
  </dialog>
</template>
