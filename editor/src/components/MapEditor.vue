<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { computed, onBeforeUnmount, ref, shallowRef, watch } from "vue";
import type { SourceMap } from "../generated/source-v1";
import {
  MapEditError,
  MapEditSession,
  MAP_MAX_SIDE,
  type MapResizeRequest
} from "../domain/map-edit-session";
import {
  CUSTOM_KIND,
  terrainColor,
  terrainGlyph,
  terrainKind
} from "../domain/terrain-presentation";
import { terrainPassability } from "../domain/terrain-passability";
import { terrainSprite } from "../domain/board-art";
import { consoleFitFor } from "../domain/console-fit";
import {
  BLOB_SHEET_HEIGHT,
  BLOB_SHEET_WIDTH,
  TILE_SIZE
} from "../generated/board-art";

const props = defineProps<{
  map: SourceMap;
  /** The project's season, so the grid shows the colours the Stage will. */
  themeId?: string | undefined;
}>();
const emit = defineEmits<{ save: [map: SourceMap] }>();

// One brush per terrain the art library draws, so every kind it grows is
// reachable without an author having to guess its name.
const commonTerrain = [
  "plain", "grass", "forest", "water", "sand", "rock", "bridge",
  "snow", "swamp", "hills", "ruins", "farm", "bamboo", "paved"
];
const session = shallowRef(new MapEditSession(props.map));
const snapshot = ref(session.value.snapshot());
const selectedTerrain = ref(snapshot.value.terrain[0] ?? "plain");
const customTerrain = ref("");
const painting = ref(false);
const feedback = ref("");
const resizeWidth = ref(snapshot.value.width);
const resizeHeight = ref(snapshot.value.height);
const fillTerrain = ref(selectedTerrain.value);
const confirmClipping = ref(false);

// What the two consoles make of the size in the fields, which is the size this
// map already is until somebody types over it. An author cannot see a telly
// from here and the answer is exact, so it is shown rather than left to be
// discovered on hardware.
//
// It is guidance and never a refusal: a board past a console's window scrolls,
// which is a board with edges to travel to rather than a board that is wrong.
// Nothing here disables the resize.
const consoleFit = computed(
  () => consoleFitFor(resizeWidth.value, resizeHeight.value)
);

/** Whether two boards are the same board, field for field. */
function sameBoard(one: SourceMap, other: SourceMap): boolean {
  return one.id === other.id &&
    one.name === other.name &&
    one.notes === other.notes &&
    one.width === other.width &&
    one.height === other.height &&
    one.terrain.length === other.terrain.length &&
    one.terrain.every((cell, index) => cell === other.terrain[index]) &&
    JSON.stringify(one.extensions) === JSON.stringify(other.extensions);
}

// A change from anywhere else replaces the session, because the history behind
// it describes a board that is gone: a rename, an import, an undo taken from
// the project's own history.
//
// **This editor's own echo is not that.** Painting emits a save, the project
// stores it, and the stored board arrives straight back here as a new object,
// so a watcher that rebuilt on every arrival threw the history away on every
// stroke, and Undo and Redo sat disabled reading "Nothing to undo yet." however
// much was painted. The session's own snapshot is what this editor believes the
// board to be, so anything equal to it is the echo and anything else is news.
watch(() => props.map, (map) => {
  if (sameBoard(map, session.value.snapshot())) return;
  session.value = new MapEditSession(map);
  snapshot.value = session.value.snapshot();
  resizeWidth.value = map.width;
  resizeHeight.value = map.height;
}, { deep: true });

// One tab stop for the whole grid: a large map must not cost thousands of
// Tab presses to cross. Arrow keys move between cells instead.
const gridRoot = ref<HTMLDivElement>();
const focusIndex = ref(0);

watch(snapshot, (value) => {
  if (focusIndex.value >= value.width * value.height) focusIndex.value = 0;
});

function terrainAt(x: number, y: number): string {
  return snapshot.value.terrain[y * snapshot.value.width + x] ?? "unknown";
}

// The ground an author paints is the ground the game is played on.
//
// This is the same lookup `TacticalBoard.vue` uses, cropping the same
// 47-variant autotiled sheet in the same theme, and the consoles apply the same
// table, so what is painted here is what a console draws. It sits behind
// the cell's button, hidden from assistive technology, and the colour beneath
// it and the mark above it both stay: a terrain the art library has no sheet
// for has no sprite to fall back on, and the mark is what keeps terrain from
// being carried by colour alone.
const assetBase = import.meta.env.BASE_URL;
function terrainArt(x: number, y: number) {
  const sprite = terrainSprite(
    snapshot.value.terrain,
    snapshot.value.width,
    snapshot.value.height,
    x,
    y,
    props.themeId
  );
  return {
    href: assetBase + sprite.href,
    view: `${sprite.sx} ${sprite.sy} ${TILE_SIZE} ${TILE_SIZE}`
  };
}

function moveFocus(x: number, y: number, dx: number, dy: number) {
  const nextX = x + dx;
  const nextY = y + dy;
  if (
    nextX < 0 || nextY < 0 ||
    nextX >= snapshot.value.width || nextY >= snapshot.value.height
  ) return;
  focusIndex.value = nextY * snapshot.value.width + nextX;
  gridRoot.value
    ?.querySelector<HTMLButtonElement>(`[data-cell="${focusIndex.value}"]`)
    ?.focus();
}

/**
 * Every brush this board offers: the ones the art library draws, the ones
 * already painted on it, and the one just invented.
 *
 * The last of those matters because an invented terrain is on no cell yet. Off
 * this list it would have no brush button to come back to and no entry in the
 * resize menu, so the one press that made it would also be the only chance to
 * use it.
 */
const palette = computed(() =>
  [...new Set([
    ...commonTerrain,
    ...snapshot.value.terrain,
    selectedTerrain.value
  ])].sort()
);

const resizeRequest = computed<MapResizeRequest>(() => ({
  width: resizeWidth.value,
  height: resizeHeight.value,
  offsetX: 0,
  offsetY: 0,
  fillTerrain: fillTerrain.value || "plain"
}));

const clippedCount = computed(() => {
  try {
    return session.value.previewResize(resizeRequest.value).clippedCells.length;
  } catch {
    return 0;
  }
});

function publish(message: string) {
  snapshot.value = session.value.snapshot();
  feedback.value = message;
  emit("save", snapshot.value);
}

function paint(x: number, y: number) {
  const transaction = session.value.paint([{ x, y }], selectedTerrain.value);
  publish(transaction.label);
}

function startPainting(x: number, y: number, event: PointerEvent) {
  if (event.button !== 0) return;
  event.preventDefault();
  painting.value = true;
  paint(x, y);
}

function continuePainting(x: number, y: number, event: PointerEvent) {
  if (painting.value && (event.buttons & 1) === 1) paint(x, y);
}

function stopPainting() {
  painting.value = false;
}

window.addEventListener("pointerup", stopPainting);
onBeforeUnmount(() => window.removeEventListener("pointerup", stopPainting));

/**
 * Undo and redo of the painting, which the session behind this editor has
 * always been able to do and this editor has never offered.
 *
 * Painting is the most destructive gesture in the product, the pointer
 * repainting on every cell it crosses while the button is held, so one careless
 * drag across a finished map is dozens of edits. They publish through the same path
 * an edit does, so the project around this editor receives an undo exactly as it
 * receives a stroke.
 */
function undoEdit() {
  const transaction = session.value.undo();
  if (transaction) publish(`Undid: ${transaction.label}`);
}

function redoEdit() {
  const transaction = session.value.redo();
  if (transaction) publish(`Redid: ${transaction.label}`);
}

/**
 * What the history holds, said rather than discovered. The depth is bounded, so
 * an author who paints past it has strokes that are gone for good; being told
 * that is better than pressing undo and finding out.
 */
const historyNote = computed(() => {
  // Reading the snapshot is what ties this to the render: the session is a
  // plain object, and every edit replaces the snapshot.
  void snapshot.value;
  const undoable = session.value.historyDepth();
  const forgotten = session.value.discardedEdits();
  if (undoable === 0) {
    return "Nothing to undo yet.";
  }
  const held = `${undoable} ${undoable === 1 ? "edit" : "edits"} can be undone.`;
  return forgotten === 0
    ? held
    : `${held} ${forgotten} older ${forgotten === 1 ? "edit is" : "edits are"} ` +
      "past undoing.";
});

/**
 * What a typed name will actually be, said before it is used.
 *
 * The art library draws thirteen kinds and matches a name to one of them by
 * keyword, and the rules read passability off the same words. A name that
 * matches nothing is not refused, since an author may want `obsidian shelf`,
 * but it is drawn flat in a colour hashed from its letters and walked over like
 * open ground, and the one moment to learn that is before painting it.
 */
const inventedTerrainNote = computed(() => {
  const terrain = customTerrain.value.trim();
  if (terrain === "") return "Type a name to see how it will be drawn.";
  if (palette.value.includes(terrain)) {
    return `This board already has a ${terrain} brush, above.`;
  }
  const walk = terrainPassability(terrain);
  const walked = walk === "open"
    ? "walked by anyone"
    : walk === "water"
      ? "crossed only by fliers and swimmers"
      : "crossed only by fliers and climbers";
  return terrainKind(terrain) === CUSTOM_KIND
    ? `'${terrain}' matches nothing the art library draws, so it is drawn ` +
      `flat and is ${walked}. Name it after water, forest, mountain, sand, ` +
      "snow, swamp, hills, ruins, grass, farmland, bamboo, a road or paving " +
      "to be drawn as one of those."
    : `'${terrain}' is drawn as ${terrainKind(terrain)} and is ${walked}.`;
});

function addTerrain() {
  const terrain = customTerrain.value.trim();
  if (!terrain) return;
  selectedTerrain.value = terrain;
  fillTerrain.value = terrain;
  customTerrain.value = "";
}

function applyResize() {
  try {
    const transaction = session.value.resize(
      resizeRequest.value,
      confirmClipping.value
    );
    confirmClipping.value = false;
    publish(transaction.label);
  } catch (error) {
    if (
      error instanceof MapEditError &&
      error.code === "MAP_CLIPPING_CONFIRMATION_REQUIRED"
    ) {
      feedback.value =
        `${error.message}. Check “Allow cropping” to apply this resize.`;
    } else {
      feedback.value = error instanceof Error ? error.message : String(error);
    }
  }
}

</script>

<template>
  <section class="map-editor" aria-labelledby="map-editor-title">
    <h3 id="map-editor-title">Terrain map</h3>

    <div class="map-history" role="group" aria-label="Map edit history">
      <button type="button" :disabled="!session.canUndo()" @click="undoEdit">
        Undo
      </button>
      <button type="button" :disabled="!session.canRedo()" @click="redoEdit">
        Redo
      </button>
      <p class="field-help" aria-live="polite">{{ historyNote }}</p>
    </div>

    <fieldset class="terrain-palette">
      <legend>Terrain brush</legend>
      <button v-for="terrain in palette" :key="terrain" type="button"
        :aria-pressed="selectedTerrain === terrain"
        :style="{ '--terrain-color': terrainColor(terrain, props.themeId) }"
        @click="selectedTerrain = terrain">
        <span aria-hidden="true">{{ terrainGlyph(terrain) }}</span>{{ terrain }}
      </button>
      <!-- Inventing one is a deliberate act behind a lid, and every brush an
           author will normally reach for is a button above it. A typed name is
           the one way a typo becomes data: `obsidian shelf` and `obsidan
           shelf` are two terrains, drawn the same flat way and walked over the
           same way, and nothing anywhere will ever say they were meant to be
           one. So the box says what the name will actually become before it is
           pressed, rather than after it is painted across a board. -->
      <details class="invent-terrain">
        <summary>Invent a terrain</summary>
        <label for="custom-terrain">New terrain name</label>
        <input id="custom-terrain" v-model.trim="customTerrain">
        <p class="field-help" aria-live="polite">{{ inventedTerrainNote }}</p>
        <button type="button" class="secondary" :disabled="customTerrain === ''"
          @click="addTerrain">
          Use terrain
        </button>
      </details>
    </fieldset>

    <p>
      Selected brush: <strong>{{ selectedTerrain }}</strong>. Click or drag to
      paint.
    </p>
    <div ref="gridRoot" class="terrain-grid" role="grid" aria-label="Terrain map">
      <div v-for="row in snapshot.height" :key="row" class="terrain-row"
        role="row"
        :style="{ gridTemplateColumns: `repeat(${snapshot.width}, minmax(2.5rem, 1fr))` }">
        <button v-for="column in snapshot.width"
          :key="(row - 1) * snapshot.width + (column - 1)"
          type="button" role="gridcell"
          :data-cell="(row - 1) * snapshot.width + (column - 1)"
          :tabindex="(row - 1) * snapshot.width + (column - 1) === focusIndex ? 0 : -1"
          :aria-label="`Column ${column}, row ${row}: ${terrainAt(column - 1, row - 1)}`"
          :title="terrainAt(column - 1, row - 1)"
          :style="{ backgroundColor: terrainColor(terrainAt(column - 1, row - 1), props.themeId) }"
          @pointerdown="focusIndex = (row - 1) * snapshot.width + (column - 1);
            startPainting(column - 1, row - 1, $event)"
          @pointerenter="continuePainting(column - 1, row - 1, $event)"
          @keydown.enter.prevent="paint(column - 1, row - 1)"
          @keydown.space.prevent="paint(column - 1, row - 1)"
          @keydown.left.prevent="moveFocus(column - 1, row - 1, -1, 0)"
          @keydown.right.prevent="moveFocus(column - 1, row - 1, 1, 0)"
          @keydown.up.prevent="moveFocus(column - 1, row - 1, 0, -1)"
          @keydown.down.prevent="moveFocus(column - 1, row - 1, 0, 1)">
          <svg class="terrain-art" aria-hidden="true"
            :viewBox="terrainArt(column - 1, row - 1).view"
            preserveAspectRatio="none">
            <image class="terrain-image"
              :href="terrainArt(column - 1, row - 1).href"
              :width="BLOB_SHEET_WIDTH" :height="BLOB_SHEET_HEIGHT" />
          </svg>
          <span class="terrain-glyph" aria-hidden="true">
            {{ terrainGlyph(terrainAt(column - 1, row - 1)) }}
          </span>
        </button>
      </div>
    </div>

    <fieldset class="map-resize">
      <legend>Resize map</legend>
      <label for="map-width">Width</label>
      <input id="map-width" v-model.number="resizeWidth" type="number"
        min="1" :max="MAP_MAX_SIDE">
      <label for="map-height">Height</label>
      <input id="map-height" v-model.number="resizeHeight" type="number"
        min="1" :max="MAP_MAX_SIDE">
      <p v-if="consoleFit" class="console-fit" data-testid="console-fit"
        aria-live="polite">
        {{ consoleFit.summary }}
        <span v-if="consoleFit.scrollsAnywhere" class="console-fit-note">
          A board that scrolls is fine — the player travels to its edges.
        </span>
      </p>
      <!-- The brushes this board already has, and nothing typed: a name typed
           here is painted across every new cell at once, so a slip fills the
           board with a terrain nothing else in the game shares. Inventing one
           is still possible and is done once, in the brush box above; it joins
           this list the moment it exists. -->
      <label for="resize-fill">New-cell terrain</label>
      <select id="resize-fill" v-model="fillTerrain">
        <option v-for="terrain in palette" :key="terrain" :value="terrain">
          {{ terrain }}
        </option>
      </select>
      <p v-if="clippedCount" class="crop-warning" role="alert">
        This resize crops {{ clippedCount }} existing
        {{ clippedCount === 1 ? "cell" : "cells" }} from the right or bottom.
      </p>
      <label v-if="clippedCount" class="crop-confirm">
        <input v-model="confirmClipping" type="checkbox">
        Allow cropping
      </label>
      <button type="button" @click="applyResize">Apply resize</button>
    </fieldset>
    <p class="field-help" aria-live="polite">{{ feedback }}</p>
  </section>
</template>
