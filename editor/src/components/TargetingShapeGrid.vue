<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
/**
 * Draws a reach band or an area of impact on a Manhattan grid, and lets an
 * author set the fields by painting rather than by imagining what the numbers
 * mean.
 *
 * The same component serves both, because they are the same geometry, but
 * they are never merged into one control, because they answer different
 * questions from different origins. A reach band is measured from where the
 * character stands and says where they may aim; an area of impact is measured
 * from the tile aimed at and says what the strike covers. A record carrying
 * both can also show them composed, which is what `aim` is for.
 *
 * Every covering decision comes from `targeting-geometry`, which is the one
 * place the editor holds these rules and the one place they are pinned against
 * the running engine.
 */
import { computed, ref, watch } from "vue";
import {
  areaCovers,
  areaRadius,
  bandCovers,
  bandHole,
  describeArea,
  describeBand,
  manhattan,
  offsetKey,
  offsetsWithin,
  readArea,
  readBand,
  type AreaShape,
  type Offset
} from "../domain/targeting-geometry";

const props = defineProps<{
  kind: "band" | "area";
  /** A reach band's stored fields, when this grid draws one. */
  minimumRange?: number | undefined;
  maximumRange?: number | undefined;
  /** An area of impact's stored fields, when this grid draws one. */
  areaShape?: AreaShape | undefined;
  radius?: number | undefined;
  /**
   * The area a strike aimed inside this band would cover. Present only on a
   * band grid belonging to a record that carries both, and the only reason
   * this grid ever draws two shapes at once.
   */
  compose?: { shape: AreaShape; radius: number | undefined } | undefined;
}>();

const emit = defineEmits<{
  band: [fields: { minimumRange: number; maximumRange: number }];
  area: [fields: { areaShape: AreaShape; radius: number }];
}>();

/**
 * How far out the grid draws. Reach runs to 255 and no readable grid draws
 * that, so the grid draws what it can and says what it is drawing rather than
 * showing a clipped shape as though it were the whole one.
 */
const drawnExtent = 8;
const minimumExtent = 3;

const authoredReach = computed(() =>
  props.kind === "band"
    ? Math.max(props.minimumRange ?? 1, props.maximumRange ?? 1)
    : areaRadius(props.areaShape ?? "single", props.radius)
);

const extent = computed(() =>
  Math.min(drawnExtent, Math.max(minimumExtent, authoredReach.value + 1))
);

const clipped = computed(() => authoredReach.value > extent.value);

const rows = computed(() => {
  const size = extent.value;
  const built: Offset[][] = [];
  for (let dy = -size; dy <= size; dy += 1) {
    const row: Offset[] = [];
    for (let dx = -size; dx <= size; dx += 1) row.push({ dx, dy });
    built.push(row);
  }
  return built;
});

// Free painting is off by default: the common gesture is "reach out to here",
// and toggling one tile at a time is only useful for discovering that a shape
// cannot be expressed. Both are real ways to edit, so both are offered.
const freePainting = ref(false);
const painted = ref<Set<string>>(new Set());
const refusal = ref("");
const announcement = ref("");

/** The tile a composed preview is aimed at, in offsets from the stance. */
const aim = ref<Offset | undefined>(undefined);

/** The shape the stored fields describe, as a set of offset keys. */
const storedTiles = computed(() => {
  const keys = new Set<string>();
  for (const offset of offsetsWithin(extent.value)) {
    const covered = props.kind === "band"
      ? bandCovers(props.minimumRange ?? 1, props.maximumRange ?? 1, offset)
      : areaCovers(props.areaShape ?? "single", props.radius, offset);
    if (covered) keys.add(offsetKey(offset));
  }
  return keys;
});

// Leaving free painting, or a change arriving from the number controls,
// re-seeds the painted set from what is actually stored, so the two never show
// different shapes.
watch(
  [storedTiles, freePainting],
  ([tiles]) => {
    if (!freePainting.value) {
      painted.value = new Set(tiles);
      refusal.value = "";
    }
  },
  { immediate: true }
);

const impactTiles = computed(() => {
  const centre = aim.value;
  if (!centre || !props.compose) return new Set<string>();
  const keys = new Set<string>();
  for (const offset of offsetsWithin(extent.value)) {
    if (!areaCovers(props.compose.shape, props.compose.radius, {
      dx: offset.dx - centre.dx,
      dy: offset.dy - centre.dy
    })) continue;
    keys.add(offsetKey(offset));
  }
  return keys;
});

type CellState = "origin" | "covered" | "hole" | "outside" | "aim" | "impact";

function state(offset: Offset): CellState {
  const key = offsetKey(offset);
  if (aim.value && offsetKey(aim.value) === key) return "aim";
  if (impactTiles.value.has(key)) return "impact";
  if (offset.dx === 0 && offset.dy === 0) return "origin";
  const shown = freePainting.value ? painted.value : storedTiles.value;
  if (shown.has(key)) return "covered";
  // The hole a minimum reach above one leaves is drawn apart from "too far":
  // on a grid they look identical, and telling them apart is the single most
  // confusing thing the numbers alone ask an author to picture.
  if (props.kind === "band" && bandHole(props.minimumRange ?? 1, offset)) {
    return "hole";
  }
  return "outside";
}

const stateWords: Record<CellState, string> = {
  origin: props.kind === "band" ? "where the character stands" : "the tile aimed at",
  covered: props.kind === "band" ? "within reach" : "covered",
  hole: "too close to strike",
  outside: props.kind === "band" ? "out of reach" : "not covered",
  aim: "aimed here",
  impact: "covered by the strike aimed here"
};

function cellLabel(offset: Offset): string {
  const separation = manhattan(offset.dx, offset.dy);
  const where = separation === 0
    ? "the origin"
    : `${separation} ${separation === 1 ? "tile" : "tiles"} away`;
  return `${where}, ${stateWords[state(offset)]}`;
}

/** Writes the stored fields from a set of painted tiles, or refuses out loud. */
function commit(tiles: Set<string>) {
  const offsets = offsetsWithin(extent.value)
    .filter((offset) => tiles.has(offsetKey(offset)));
  if (props.kind === "band") {
    const reading = readBand(offsets, extent.value);
    if (!reading.expressible) {
      refusal.value = reading.reason;
      return;
    }
    refusal.value = "";
    emit("band", reading.fields);
    announcement.value = describeBand(
      reading.fields.minimumRange,
      reading.fields.maximumRange
    );
    return;
  }
  const reading = readArea(offsets, extent.value);
  if (!reading.expressible) {
    refusal.value = reading.reason;
    return;
  }
  refusal.value = "";
  emit("area", reading.fields);
  announcement.value = describeArea(reading.fields.areaShape, reading.fields.radius);
}

/**
 * The plain gesture: reaching out to a tile sets how far the shape reaches.
 * An area takes the distance as its radius. A band keeps whichever end the
 * author is not moving, so dragging from one ring to another sets both.
 */
function reachTo(offset: Offset, extending: boolean) {
  const separation = manhattan(offset.dx, offset.dy);
  if (props.kind === "area") {
    const shape: AreaShape =
      separation === 0 ? "single" : separation === 1 ? "cross" : "diamond";
    refusal.value = "";
    emit("area", { areaShape: shape, radius: separation });
    announcement.value = describeArea(shape, separation);
    return;
  }
  if (separation === 0) {
    refusal.value =
      "A reach band is measured from where the character stands, so it never " +
      "admits that tile. The closest reach a band can have is one.";
    return;
  }
  const anchor = extending ? dragAnchor.value ?? separation : separation;
  const minimumRange = Math.min(anchor, separation);
  const maximumRange = Math.max(anchor, separation);
  refusal.value = "";
  emit("band", { minimumRange, maximumRange });
  announcement.value = describeBand(minimumRange, maximumRange);
}

function togglePainted(offset: Offset) {
  const key = offsetKey(offset);
  const next = new Set(painted.value);
  if (next.has(key)) next.delete(key);
  else next.add(key);
  painted.value = next;
  commit(next);
}

function chooseAim(offset: Offset) {
  const current = aim.value;
  aim.value = current && offsetKey(current) === offsetKey(offset)
    ? undefined
    : offset;
  announcement.value = aim.value
    ? `Aimed ${manhattan(offset.dx, offset.dy)} tiles away.`
    : "Aim cleared.";
}

const aiming = ref(false);

function activate(offset: Offset, extending: boolean) {
  if (aiming.value) {
    chooseAim(offset);
    return;
  }
  if (freePainting.value) {
    togglePainted(offset);
    return;
  }
  reachTo(offset, extending);
}

// Pointer painting, following the map editor's precedent: press to begin, drag
// to continue, and a release anywhere on the page ends it.
const dragging = ref(false);
const dragAnchor = ref<number | undefined>(undefined);

function press(offset: Offset, event: PointerEvent) {
  if (event.button !== 0) return;
  event.preventDefault();
  focusKey.value = offsetKey(offset);
  dragging.value = true;
  dragAnchor.value = manhattan(offset.dx, offset.dy);
  activate(offset, false);
}

function drag(offset: Offset, event: PointerEvent) {
  if (!dragging.value || (event.buttons & 1) !== 1) return;
  activate(offset, true);
}

function release() {
  dragging.value = false;
  dragAnchor.value = undefined;
}

// One tab stop for the whole figure, arrow keys between cells: a grid this
// size must not cost an author one Tab press per tile.
const gridRoot = ref<HTMLDivElement>();
const focusKey = ref(offsetKey({ dx: 0, dy: 0 }));

watch(extent, () => {
  const [dx, dy] = focusKey.value.split(":").map(Number);
  if (Math.abs(dx ?? 0) > extent.value || Math.abs(dy ?? 0) > extent.value) {
    focusKey.value = offsetKey({ dx: 0, dy: 0 });
  }
});

function moveFocus(offset: Offset, dx: number, dy: number) {
  const next = { dx: offset.dx + dx, dy: offset.dy + dy };
  if (Math.abs(next.dx) > extent.value || Math.abs(next.dy) > extent.value) return;
  focusKey.value = offsetKey(next);
  gridRoot.value
    ?.querySelector<HTMLButtonElement>(`[data-cell="${offsetKey(next)}"]`)
    ?.focus();
}

const summary = computed(() =>
  props.kind === "band"
    ? describeBand(props.minimumRange ?? 1, props.maximumRange ?? 1)
    : describeArea(props.areaShape ?? "single", props.radius)
);

const heading = computed(() =>
  props.kind === "band" ? "Reach band" : "Area of impact"
);

const instructions = computed(() => {
  if (aiming.value) {
    return "Choose a tile to aim at. The tiles a strike aimed there would " +
      "cover are shown around it.";
  }
  if (freePainting.value) {
    return "Click a tile to add or remove it. Shapes the current fields " +
      "cannot express are refused rather than rounded.";
  }
  return props.kind === "band"
    ? "Click a tile to strike at that distance, or drag from one ring to " +
      "another to set both ends of the band."
    : "Click a tile to cover everything out to that distance.";
});
</script>

<template>
  <section class="targeting-grid" :aria-labelledby="`targeting-${kind}-title`"
    @pointerup="release" @pointerleave="release">
    <h4 :id="`targeting-${kind}-title`">{{ heading }}</h4>
    <p class="field-help">
      {{ kind === "band"
        ? "Measured from where the character stands: where they may aim."
        : "Measured from the tile aimed at: what the strike covers." }}
    </p>
    <p class="targeting-summary">{{ summary }}</p>

    <div class="targeting-modes">
      <label>
        <input v-model="freePainting" type="checkbox" :disabled="aiming">
        Paint individual tiles
      </label>
      <label v-if="compose">
        <input v-model="aiming" type="checkbox">
        Show what a strike aimed here covers
      </label>
    </div>

    <p class="field-help">{{ instructions }}</p>

    <div ref="gridRoot" class="targeting-cells" role="grid"
      :aria-label="`${heading} preview`">
      <div v-for="(row, index) in rows" :key="index" role="row"
        class="targeting-row"
        :style="{ gridTemplateColumns: `repeat(${row.length}, minmax(1.4rem, 1fr))` }">
        <button v-for="offset in row" :key="offsetKey(offset)"
          type="button" role="gridcell"
          :data-cell="offsetKey(offset)"
          :data-state="state(offset)"
          :class="`targeting-cell is-${state(offset)}`"
          :tabindex="offsetKey(offset) === focusKey ? 0 : -1"
          :aria-label="cellLabel(offset)"
          @pointerdown="press(offset, $event)"
          @pointerenter="drag(offset, $event)"
          @keydown.enter.prevent="activate(offset, false)"
          @keydown.space.prevent="activate(offset, false)"
          @keydown.left.prevent="moveFocus(offset, -1, 0)"
          @keydown.right.prevent="moveFocus(offset, 1, 0)"
          @keydown.up.prevent="moveFocus(offset, 0, -1)"
          @keydown.down.prevent="moveFocus(offset, 0, 1)">
          <span aria-hidden="true">{{
            state(offset) === "origin"
              ? "◎"
              : state(offset) === "aim"
                ? "✕"
                : ""
          }}</span>
        </button>
      </div>
    </div>

    <p v-if="clipped" class="field-help targeting-clipped">
      The stored reach is further than the {{ extent }} tiles drawn here, and
      is not clamped by them.
    </p>
    <p v-if="refusal" class="field-error" role="alert">{{ refusal }}</p>
    <p class="visually-hidden" aria-live="polite">{{ announcement }}</p>
  </section>
</template>
