<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { computed, onBeforeUnmount, ref, shallowRef, triggerRef, watch } from "vue";
import type { SourceProject } from "../generated/source-v1";
import {
  attackUnit,
  beginBattle,
  canDeploy,
  deployUnit,
  deployableTiles,
  endPlaytest,
  legalMoves,
  legalTargets,
  moveUnit,
  startPlaytest,
  waitUnit,
  type PlaytestState
} from "../domain/playtest-session";
import PlaytestControls from "./PlaytestControls.vue";
import TacticalBoard from "./TacticalBoard.vue";

const props = defineProps<{ project: SourceProject }>();
// The encounter owns private mutable state and must not be wrapped in Vue's
// deep reactive proxy. Commands explicitly trigger a view refresh instead.
const state = shallowRef<PlaytestState>();
const error = ref("");
const selectedUnitId = ref("");
const action = ref<"move" | "attack">("move");

// While a board is being arranged the offered tiles are the region rather than
// the movement range, so the one control the panel has says the right thing in
// both phases and never offers a command the engine would refuse.
const legalMoveCoordinates = computed(() => {
  const current = state.value;
  if (!current) return [];
  return current.deploying
    ? deployableTiles(current, selectedUnitId.value)
    : legalMoves(current, selectedUnitId.value);
});
const legalMoveKeys = computed(() =>
  new Set(legalMoveCoordinates.value.map(([x, y]) => `${x}:${y}`))
);
const legalTargetIds = computed(() =>
  new Set(state.value ? legalTargets(state.value, selectedUnitId.value) : [])
);
// The canonical hash is the engine's own fingerprint of authoritative state.
// It is the value the native test suite pins, so showing it here makes
// browser/native agreement observable rather than merely asserted in CI.
const canonicalHash = computed(() =>
  state.value?.encounter.canonicalHash().toString(16).padStart(16, "0") ?? ""
);

function restart() {
  endPlaytest(state.value);
  const started = startPlaytest(props.project);
  state.value = started.state;
  error.value = started.error ?? "";
  selectedUnitId.value = "";
  action.value = "move";
}

function chooseCell(x: number, y: number) {
  const current = state.value;
  if (!current || current.outcome !== "ongoing") return;
  const unit = current.units.find(
    (candidate) => candidate.onBoard && candidate.x === x && candidate.y === y
  );
  if (current.deploying) {
    if (unit && canDeploy(current, unit.id) && unit.id !== selectedUnitId.value) {
      selectedUnitId.value = unit.id;
      return;
    }
    if (!selectedUnitId.value) return;
    if (!legalMoveKeys.value.has(`${x}:${y}`)) return;
    deployUnit(current, selectedUnitId.value, x, y);
    triggerRef(state);
    return;
  }
  if (action.value === "attack" && unit && legalTargetIds.value.has(unit.id)) {
    attackUnit(props.project, current, selectedUnitId.value, unit.id);
    triggerRef(state);
    selectedUnitId.value = "";
    return;
  }
  if (action.value === "move" && legalMoveKeys.value.has(`${x}:${y}`)) {
    moveUnit(props.project, current, selectedUnitId.value, x, y);
    triggerRef(state);
    selectedUnitId.value = "";
    return;
  }
  if (unit?.side === current.activeSide) selectedUnitId.value = unit.id;
}

function begin() {
  if (!state.value || !state.value.deploying) return;
  if (beginBattle(state.value)) {
    triggerRef(state);
    selectedUnitId.value = "";
  }
}

function wait() {
  if (!state.value || !selectedUnitId.value) return;
  if (waitUnit(props.project, state.value, selectedUnitId.value)) {
    triggerRef(state);
    selectedUnitId.value = "";
  }
}

watch(() => props.project, () => {
  endPlaytest(state.value);
  state.value = undefined;
  selectedUnitId.value = "";
}, { deep: true });

onBeforeUnmount(() => {
  endPlaytest(state.value);
  state.value = undefined;
});
</script>

<template>
  <section class="playtest-panel" aria-labelledby="playtest-title">
    <p class="eyebrow">Immediate feedback</p>
    <h2 id="playtest-title">Browser playtest</h2>
    <p>Run the campaign's first Stage directly from the unsaved editor state.</p>
    <button type="button" @click="restart">
      {{ state ? "Restart the Stage" : "Run the Stage" }}
    </button>
    <p v-if="error" role="alert">{{ error }}</p>

    <div v-if="state" class="playtest-layout">
      <div>
        <h3>{{ state.nodeName }}: {{ state.mapName }}</h3>
        <p class="playtest-status" role="status">
          <template v-if="state.deploying">
            Deployment: stand your line, then begin the fighting
          </template>
          <template v-else-if="state.outcome === 'ongoing'">
            Turn {{ state.activationCount + 1 }}:
            {{ state.activeSide === "first" ? "Your side" : "The enemy" }}
            <!--
              The round the player is in and the number there are to outlast,
              on the boards whose content says the Stage is won by outlasting
              any. A board that authors no such objective shows nothing here.
            -->
            <template v-if="state.roundsToSurvive > 0">
              (round {{ Math.min(state.round + 1, state.roundsToSurvive) }}
              of {{ state.roundsToSurvive }})
            </template>
          </template>
          <template v-else>
            {{ state.outcome === "first_side_won" ? "Your side" : "The enemy" }} won
            <span v-if="state.terminalNodeName">
              (campaign complete: {{ state.terminalNodeName }})
            </span>
          </template>
        </p>
        <TacticalBoard
          :width="state.width"
          :height="state.height"
          :terrain="state.terrain"
          :theme-id="state.themeId"
          :character-style-id="state.characterStyleId"
          :character-figure-id="state.characterFigureId"
          :units="state.units"
          :selected-unit-id="selectedUnitId"
          :legal-move-keys="legalMoveKeys"
          :legal-target-ids="legalTargetIds"
          @choose-cell="chooseCell"
        />
        <PlaytestControls
          :state="state"
          :selected-unit-id="selectedUnitId"
          :action="action"
          :legal-moves="legalMoveCoordinates"
          :legal-target-ids="legalTargetIds"
          @select-unit="selectedUnitId = $event"
          @set-action="action = $event"
          @choose-cell="chooseCell"
          @wait="wait"
          @begin="begin"
        />
      </div>
      <aside>
        <p class="canonical-hash">
          Engine state
          <code data-canonical-hash aria-label="Canonical state hash">{{ canonicalHash }}</code>
        </p>
        <h3>What happened</h3>
        <ol aria-live="polite">
          <li v-for="(event, index) in state.events" :key="`${index}:${event}`">
            {{ event }}
          </li>
        </ol>
      </aside>
    </div>
  </section>
</template>

<style scoped>
.canonical-hash {
  margin: 0 0 0.5rem;
  color: #4a5a52;
  font-size: 0.8rem;
}
.canonical-hash code {
  font-family: ui-monospace, monospace;
  user-select: all;
}
</style>
