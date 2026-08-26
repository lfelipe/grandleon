<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, shallowRef, watch } from "vue";
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

/**
 * How large a board has to get before this grid stops drawing all of it.
 *
 * **A cell is four DOM nodes** -- a button, an SVG, its image and a glyph -- so
 * a board at the format's ceiling of 256 a side is a quarter of a million
 * nodes. That is not a slow grid; it is a tab that stops. Under this a board is
 * drawn whole, exactly as it always was, and the stretch-to-fit sizing every
 * small map has is kept.
 *
 * A thousand cells is about a 32x32 board, which is four times the largest map
 * either shipped game draws and comfortably more than a browser minds.
 */
const WINDOWED_ABOVE_CELLS = 1024;

/**
 * The pitch a windowed board is drawn at, in pixels.
 *
 * Fixed rather than measured, and stated here rather than in the stylesheet,
 * because the window arithmetic and the drawing have to agree exactly: a cell
 * whose size the stylesheet decides and this file guesses would put the tiles
 * under the cursor one row off the tiles under the pointer. The stretch sizing
 * is what a small board keeps and a windowed one gives up; a board this size
 * scrolls either way.
 */
const CELL_PX = 40;
const CELL_GAP_PX = 2;
const CELL_PITCH_PX = CELL_PX + CELL_GAP_PX;

/** Rows and columns drawn beyond the viewport, so a scroll is not a redraw. */
const OVERSCAN = 3;

const windowed = computed(
  () => snapshot.value.width * snapshot.value.height > WINDOWED_ABOVE_CELLS
);

const viewport = ref({ top: 0, left: 0, height: 0, width: 0 });

onMounted(() => {
  readViewport();
  if (typeof window !== "undefined") window.addEventListener("resize", readViewport);
});
onBeforeUnmount(() => {
  if (typeof window !== "undefined") window.removeEventListener("resize", readViewport);
});

function readViewport(): void {
  const element = gridRoot.value;
  if (!element) return;
  viewport.value = {
    top: element.scrollTop,
    left: element.scrollLeft,
    height: element.clientHeight,
    width: element.clientWidth
  };
}

/**
 * How much of a windowed board to draw before anything has been measured.
 *
 * A first paint happens before layout, and so does every environment with no
 * layout engine. Falling back to the whole board there would draw the quarter
 * of a million nodes this window exists to avoid, at the one moment nothing has
 * asked for them yet -- which is how the fallback was first written and what it
 * did to a 256-cell-square board. A screenful is the safe direction to be wrong
 * in: too few cells are drawn for an instant, and the first scroll or resize
 * corrects it.
 */
const UNMEASURED_TRACKS = 24;

/** A half-open range of tracks, clamped to the board. */
function visibleRange(offset: number, extent: number, total: number) {
  const room = extent > 0 ? extent : UNMEASURED_TRACKS * CELL_PITCH_PX;
  const first = Math.max(0, Math.floor(offset / CELL_PITCH_PX) - OVERSCAN);
  const last = Math.min(
    total, Math.ceil((offset + room) / CELL_PITCH_PX) + OVERSCAN
  );
  return { first, last };
}

const visibleRows = computed(() => {
  if (!windowed.value) return { first: 0, last: snapshot.value.height };
  return visibleRange(
    viewport.value.top, viewport.value.height, snapshot.value.height
  );
});

const visibleColumns = computed(() => {
  if (!windowed.value) return { first: 0, last: snapshot.value.width };
  return visibleRange(
    viewport.value.left, viewport.value.width, snapshot.value.width
  );
});

/**
 * The grid declares every track and only the visible ones are filled.
 *
 * That is what keeps the scrollbars honest without a spacer element: an empty
 * track of an explicit size occupies its space and costs no nodes, so the board
 * is the size it says it is and the cursor lands where the pointer is.
 */
const gridStyle = computed(() => windowed.value ? {
  gap: `${CELL_GAP_PX}px`,
  gridTemplateRows: `repeat(${snapshot.value.height}, ${CELL_PX}px)`,
  maxWidth: "none"
} : {});

const rowStyle = (row: number) => windowed.value ? {
  gap: `${CELL_GAP_PX}px`,
  gridRow: `${row}`,
  gridTemplateColumns: `repeat(${snapshot.value.width}, ${CELL_PX}px)`
} : {
  gridTemplateColumns: `repeat(${snapshot.value.width}, minmax(2.5rem, 1fr))`
};

const cellStyle = (column: number, terrain: string) => ({
  backgroundColor: terrainColor(terrain, props.themeId),
  ...(windowed.value ? { gridColumn: `${column}` } : {})
});

/** Brings a cell into view before anything tries to focus it. */
function revealCell(index: number): void {
  const element = gridRoot.value;
  if (!element || !windowed.value) return;
  const x = index % snapshot.value.width;
  const y = Math.floor(index / snapshot.value.width);
  const left = x * CELL_PITCH_PX;
  const top = y * CELL_PITCH_PX;
  if (left < element.scrollLeft) element.scrollLeft = left;
  if (left + CELL_PITCH_PX > element.scrollLeft + element.clientWidth) {
    element.scrollLeft = left + CELL_PITCH_PX - element.clientWidth;
  }
  if (top < element.scrollTop) element.scrollTop = top;
  if (top + CELL_PITCH_PX > element.scrollTop + element.clientHeight) {
    element.scrollTop = top + CELL_PITCH_PX - element.clientHeight;
  }
  readViewport();
}

function moveFocus(x: number, y: number, dx: number, dy: number) {
  const nextX = x + dx;
  const nextY = y + dy;
  if (
    nextX < 0 || nextY < 0 ||
    nextX >= snapshot.value.width || nextY >= snapshot.value.height
  ) return;
  focusIndex.value = nextY * snapshot.value.width + nextX;
  // Scrolled into view first: on a windowed board the cell an arrow key just
  // moved to may not be drawn yet, and focusing nothing would strand the
  // keyboard at the edge of the window.
  revealCell(focusIndex.value);
  const focusCell = () => gridRoot.value
    ?.querySelector<HTMLButtonElement>(`[data-cell="${focusIndex.value}"]`)
    ?.focus();
  if (gridRoot.value?.querySelector(`[data-cell="${focusIndex.value}"]`)) {
    focusCell();
  } else {
    void nextTick(focusCell);
  }
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
    <div ref="gridRoot" class="terrain-grid" role="grid" aria-label="Terrain map"
      :style="gridStyle" @scroll.passive="readViewport">
      <template v-for="row in snapshot.height" :key="row">
      <div v-if="row > visibleRows.first && row <= visibleRows.last"
        class="terrain-row" role="row" :style="rowStyle(row)">
        <template v-for="column in snapshot.width" :key="(row - 1) * snapshot.width + (column - 1)">
        <button v-if="column > visibleColumns.first && column <= visibleColumns.last"
          type="button" role="gridcell"
          :data-cell="(row - 1) * snapshot.width + (column - 1)"
          :tabindex="(row - 1) * snapshot.width + (column - 1) === focusIndex ? 0 : -1"
          :aria-label="`Column ${column}, row ${row}: ${terrainAt(column - 1, row - 1)}`"
          :title="terrainAt(column - 1, row - 1)"
          :style="cellStyle(column, terrainAt(column - 1, row - 1))"
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
        </template>
      </div>
      </template>
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
