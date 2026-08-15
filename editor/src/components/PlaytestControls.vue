<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { computed, nextTick, ref, watch } from "vue";
import type { PlaytestState } from "../domain/playtest-session";

const props = defineProps<{
  state: PlaytestState;
  selectedUnitId: string;
  action: "move" | "attack";
  legalMoves: readonly (readonly [number, number])[];
  legalTargetIds: ReadonlySet<string>;
}>();

const emit = defineEmits<{
  selectUnit: [unitId: string];
  setAction: [action: "move" | "attack"];
  chooseCell: [x: number, y: number];
  wait: [];
  begin: [];
}>();

const controls = ref<HTMLElement>();
const selectedUnit = computed(() =>
  props.state.units.find((unit) => unit.id === props.selectedUnitId)
);
const selectableUnits = computed(() =>
  props.state.units.filter(
    (unit) => unit.onBoard && unit.side === props.state.activeSide
  )
);
const legalTargets = computed(() =>
  props.state.units.filter((unit) => props.legalTargetIds.has(unit.id))
);
// The author plays the first side, so it is theirs and the second is the
// enemy's. One name for a side across the editor and every console. Held in
// the possessive because both readers of it own something: a turn, and a set
// of characters.
const sideName = computed(() =>
  props.state.activeSide === "first" ? "Your side's" : "The enemy's"
);
const selectionStatus = computed(() => {
  if (props.state.outcome !== "ongoing") {
    return props.state.outcome === "first_side_won"
      ? "The Stage is over. Your side won."
      : "The Stage is over. The enemy won.";
  }
  if (props.state.deploying) {
    return selectedUnit.value
      ? `${selectedUnit.value.name} selected. ` +
        `${props.legalMoves.length} places to stand.`
      : "Deployment. Choose a character to stand.";
  }
  if (!selectedUnit.value) {
    return `${sideName.value} turn. Choose a character.`;
  }
  const choiceCount = props.action === "move"
    ? props.legalMoves.length
    : legalTargets.value.length;
  return `${selectedUnit.value.name} selected. ${props.action === "move" ? "Move" : "Attack"} has ${choiceCount} available ${choiceCount === 1 ? "choice" : "choices"}.`;
});

async function focusFirstChoice() {
  await nextTick();
  controls.value?.querySelector<HTMLElement>("[data-playtest-choice]")?.focus();
}

async function chooseAction(nextAction: "move" | "attack") {
  emit("setAction", nextAction);
  await focusFirstChoice();
}

watch(
  () => props.state.activationCount,
  async () => {
    await nextTick();
    controls.value
      ?.querySelector<HTMLElement>("[data-playtest-unit]:not([disabled])")
      ?.focus();
  }
);
</script>

<template>
  <section ref="controls" class="playtest-controls"
    aria-labelledby="playtest-controls-title"
    aria-describedby="playtest-controls-instructions">
    <h4 id="playtest-controls-title">Keyboard controls</h4>
    <p id="playtest-controls-instructions" class="field-help">
      Choose a unit, then an action and destination. Use Tab to move between
      controls and Enter or Space to activate one.
    </p>
    <p class="visually-hidden" role="status" aria-live="polite" aria-atomic="true">
      {{ selectionStatus }}
    </p>

    <fieldset :disabled="state.outcome !== 'ongoing'">
      <legend>{{ sideName }} characters</legend>
      <button v-for="unit in selectableUnits" :key="unit.id" type="button"
        data-playtest-unit
        :aria-pressed="selectedUnitId === unit.id"
        :aria-label="`${unit.name}, ${unit.health} of ${unit.maximumHealth} health, position ${unit.x}, ${unit.y}`"
        @click="emit('selectUnit', unit.id)">
        {{ unit.name }}: HP {{ unit.health }}/{{ unit.maximumHealth }}
        at {{ unit.x }},{{ unit.y }}
      </button>
    </fieldset>

    <template v-if="state.deploying">
      <fieldset>
        <legend>Deployment</legend>
        <p v-if="!selectedUnitId">Choose a character to stand.</p>
        <button v-for="[x, y] in legalMoves" :key="`${x}:${y}`" type="button"
          data-playtest-choice
          @click="emit('chooseCell', x, y)">
          Stand at {{ x }},{{ y }}
        </button>
        <button type="button" @click="emit('begin')">Begin the fighting</button>
      </fieldset>
    </template>

    <fieldset v-if="!state.deploying"
      :disabled="!selectedUnitId || state.outcome !== 'ongoing'">
      <legend>Action for {{ selectedUnit?.name ?? "selected character" }}</legend>
      <button type="button" :aria-pressed="action === 'move'"
        @click="chooseAction('move')">Move</button>
      <button type="button" :aria-pressed="action === 'attack'"
        @click="chooseAction('attack')">Attack</button>
      <button type="button" @click="emit('wait')">Wait and finish their turn</button>
    </fieldset>

    <fieldset v-if="!state.deploying && selectedUnitId && action === 'move'"
      :disabled="state.outcome !== 'ongoing'">
      <legend>Move destination</legend>
      <p v-if="legalMoves.length === 0">No legal move destinations.</p>
      <button v-for="[x, y] in legalMoves" :key="`${x}:${y}`" type="button"
        data-playtest-choice
        @click="emit('chooseCell', x, y)">
        Move to {{ x }},{{ y }}
      </button>
    </fieldset>

    <fieldset v-if="!state.deploying && selectedUnitId && action === 'attack'"
      :disabled="state.outcome !== 'ongoing'">
      <legend>Attack target</legend>
      <p v-if="legalTargets.length === 0">No targets are in range.</p>
      <button v-for="unit in legalTargets" :key="unit.id" type="button"
        data-playtest-choice
        :aria-label="`Attack ${unit.name}, ${unit.health} of ${unit.maximumHealth} health, position ${unit.x}, ${unit.y}`"
        @click="emit('chooseCell', unit.x, unit.y)">
        {{ unit.name }}: HP {{ unit.health }}/{{ unit.maximumHealth }}
        at {{ unit.x }},{{ unit.y }}
      </button>
    </fieldset>
  </section>
</template>

<style scoped>
.playtest-controls {
  display: grid;
  gap: .75rem;
  margin-top: 1rem;
  padding: 1rem;
  border: 1px solid #c7d2ca;
  border-radius: .65rem;
  background: #f5f7f2;
}
.playtest-controls h4,
.playtest-controls p {
  margin: 0;
}
.playtest-controls fieldset {
  display: flex;
  flex-wrap: wrap;
  gap: .5rem;
  margin: 0;
  padding: .65rem;
  border: 1px solid #ccd5ce;
  border-radius: .45rem;
}
.playtest-controls legend {
  padding: 0 .3rem;
  color: #35493e;
  font-weight: 700;
}
.playtest-controls button {
  min-height: 2.5rem;
}
.playtest-controls button[aria-pressed="true"] {
  outline: 3px solid #b78c23;
  outline-offset: 1px;
}
</style>
