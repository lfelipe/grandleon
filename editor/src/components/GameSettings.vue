<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// The choices that shape a whole game rather than one record: what it is
// called, which revision it is, how its Stages are ordered, what a fall costs
// the company, and the style and season it is drawn in. They belong together on
// one page, because a decision about the whole game has nowhere of its own to
// live in a record editor: folded into a metadata accordion beside the schema
// version and the package identifier, or sitting on the one board it happens to
// apply to, each of them is a game-wide choice an author has to go hunting for.
//
// The fields keep the storage the format gives them; this is where they are
// shown, not a new way of holding them. The form is the same schema-driven one
// every record uses, so a menu here is still "one menu entry".
//
// The page also owes the author the truth about what it does not change: a
// Stage that states its own turn order keeps it, and changing the default
// never rewrites one. So the boards that disagree are named, with the order
// each of them chose.
//
// And it owes them one more truth, which is why there are two forms below
// rather than one. Everything in the first is a choice about what kind of game
// this is. What is in the second is not: it is an aid for testing a game, it is
// worded as one, and it stands behind its own closed lid with its own save, so
// that an author reading down a single column of controls cannot take it for
// one more decision about their world. That it still ships in the package is
// said on the control itself in a sentence, because a switch that quietly
// rewrote a stranger's game would be worse than no switch at all, and a
// paragraph of warning above a single checkbox is a paragraph nobody reads.

import { computed, ref } from "vue";
import SchemaRecordForm from "./SchemaRecordForm.vue";
import {
  sourceGameRuleFields,
  sourceTestingAidFields
} from "../domain/source-form-model";
import {
  boardsFollowingDefault,
  projectTurnOrder,
  turnOrderLabel,
  turnOrderOverrides
} from "../domain/game-settings";
import type { SourceProject } from "../generated/source-v1";

const props = defineProps<{ project: SourceProject }>();

const emit = defineEmits<{
  submit: [record: Record<string, unknown>];
  dirty: [];
}>();

const form = ref<InstanceType<typeof SchemaRecordForm>>();
const testingForm = ref<InstanceType<typeof SchemaRecordForm>>();

const defaultOrder = computed(() => turnOrderLabel(projectTurnOrder(props.project)));
const overrides = computed(() => turnOrderOverrides(props.project));
const following = computed(() => boardsFollowingDefault(props.project));

const followingSentence = computed(() => {
  const count = following.value;
  if (count === 0 && overrides.value.length === 0) {
    return "No Stages yet. The first one you make follows this setting.";
  }
  return count === 1
    ? "1 Stage follows this setting."
    : `${count} Stages follow this setting.`;
});

// Both forms, and both of them tried: a pending edit in one must not be thrown
// away because the other had nothing to commit, and `&&` would stop at the
// first `false`.
function flush(): boolean {
  const rules = form.value?.flush() ?? true;
  const testing = testingForm.value?.flush() ?? true;
  return rules && testing;
}

defineExpose({ flush });
</script>

<template>
  <section class="game-settings" aria-labelledby="game-settings-title">
    <h3 id="game-settings-title">Game settings</h3>

    <SchemaRecordForm ref="form" heading="game settings" heading-already-given
      :fields="sourceGameRuleFields()"
      :model-value="(project as unknown as Readonly<Record<string, unknown>>)"
      @dirty="emit('dirty')"
      @submit="emit('submit', $event)" />

    <section class="turn-order-consequences"
      aria-labelledby="turn-order-consequences-title">
      <h4 id="turn-order-consequences-title">Which Stages this turn order reaches</h4>
      <p class="field-help">
        Stages are ordered <strong>{{ defaultOrder }}</strong> unless they say
        otherwise. {{ followingSentence }}
      </p>
      <p v-if="overrides.length === 0" class="field-help">
        No Stage overrides it, so changing it above changes them all.
      </p>
      <template v-else>
        <p class="field-help">
          {{ overrides.length === 1
            ? "One Stage chose its own order and keeps it"
            : `${overrides.length} Stages chose their own order and keep it` }}.
          Changing the setting above leaves these as they are.
        </p>
        <ul class="turn-order-overrides">
          <li v-for="override in overrides"
            :key="`${override.campaignId}/${override.nodeId}`">
            <strong>{{ override.nodeName }}</strong>
            <small>{{ override.campaignName }}</small>
            <span>{{ turnOrderLabel(override.turnOrder) }}</span>
          </li>
        </ul>
      </template>
    </section>

    <!-- Closed, and on this page. An author who wants to walk their own game
         through without dying comes looking for it here, beside the other
         choices about the whole game, but nothing in it is a choice about
         what the game *is*, so a beginner reading down this column meets the
         season and then stops, rather than meeting a switch that makes their
         player immortal. The lid is what makes both true at once. -->
    <details class="testing-aids">
      <summary id="testing-aids-title">Testing</summary>
      <p class="field-help">
        Aids for checking a game, not choices about it. Everything here ships
        in the file you export.
      </p>
      <!-- The lid above is already called Testing, so the form draws no
           heading of its own: the words name the form and its save button
           instead of standing in the outline a second time. -->
      <SchemaRecordForm ref="testingForm" heading="testing aids"
        heading-already-given
        :fields="sourceTestingAidFields()"
        :model-value="(project as unknown as Readonly<Record<string, unknown>>)"
        @dirty="emit('dirty')"
        @submit="emit('submit', $event)" />

      <!-- A control for something that does not work yet, and it says so
           rather than implying it. It is drawn because an author who wants to
           skip a map should find out here that skipping is coming and not yet
           possible, instead of hunting for a setting that was never written.
           It is disabled, it stores nothing, and the note under it names what
           is missing: a disabled control that promises a behaviour it has no
           code for would be worse than leaving it out. -->
      <div class="pending-aid">
        <label class="boolean-field" for="skip-to-next-map">
          <input id="skip-to-next-map" type="checkbox" disabled
            aria-describedby="skip-to-next-map-note">
          Player can directly skip to next map
        </label>
        <p id="skip-to-next-map-note" class="field-help">
          Not implemented. Nothing is stored and no game changes.
        </p>
      </div>
    </details>
  </section>
</template>

<style scoped>
.turn-order-consequences {
  margin-top: 1.5rem;
}

.turn-order-overrides {
  list-style: none;
  margin: 0;
  padding: 0;
  display: grid;
  gap: 0.35rem;
}

.turn-order-overrides li {
  display: grid;
  grid-template-columns: minmax(8rem, 1fr) minmax(6rem, 1fr) 2fr;
  gap: 0.5rem;
  align-items: baseline;
  padding: 0.35rem 0.5rem;
  border: 1px solid var(--line, #d0d0d0);
  border-radius: 0.25rem;
}

.turn-order-overrides small {
  opacity: 0.75;
}

/* The rule that separates a testing aid from a choice about the game. It is a
   line on the page because it is a line in the meaning: what is above it makes
   a game, what is below it only helps somebody check one. */
.testing-aids {
  margin-top: 2rem;
  padding-top: 1.25rem;
  border-top: 2px solid var(--line, #d0d0d0);
}

/* The lid reads as the heading it replaced, so closing the box did not demote
   what is in it: it is still the second half of this page. */
.testing-aids > summary {
  font-size: 1.05rem;
  font-weight: 700;
  cursor: pointer;
}

/* Clear of the save button above it, because it is not something that button
   saves: it stores nothing at all. */
.pending-aid {
  margin-top: 1.25rem;
}
</style>
