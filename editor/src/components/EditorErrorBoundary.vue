<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { onErrorCaptured, ref } from "vue";

const errorMessage = ref("");

onErrorCaptured((error) => {
  errorMessage.value = error instanceof Error
    ? error.message
    : "An unknown editor error occurred.";
  return false;
});

function retry() {
  errorMessage.value = "";
}
</script>

<template>
  <section v-if="errorMessage" class="error-boundary" role="alert"
    aria-labelledby="editor-error-title">
    <h2 id="editor-error-title">The workspace could not be displayed</h2>
    <p>{{ errorMessage }}</p>
    <button type="button" @click="retry">Try workspace again</button>
  </section>
  <slot v-else />
</template>
