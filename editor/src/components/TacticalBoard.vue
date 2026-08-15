<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { computed, ref, watch } from "vue";
import {
  projectTheme,
  terrainSheetKind,
  terrainSprite,
  unitFrameSheet,
  unitSprite
} from "../domain/board-art";
import { SEQUENCE_CELL_STAND, waterCyclePhase } from "../domain/board-motion";
import { tableValues, waterTransfer } from "../domain/water-shimmer";
import { terrainMovementCost } from "../domain/terrain-passability";
import {
  boardElevations,
  elevationStepFor,
  headroom,
  lift,
  maxElevation
} from "../domain/board-view";
import type { PlaytestUnit } from "../domain/playtest-session";
import {
  terrainColor,
  terrainKind
} from "../domain/terrain-presentation";
import {
  BLOB_SHEET_HEIGHT,
  BLOB_SHEET_WIDTH,
  SEQUENCE_SHEET_HEIGHT,
  SEQUENCE_SHEET_WIDTH,
  SHADOW_SPRITE,
  TILE_SIZE
} from "../generated/board-art";

const props = defineProps<{
  width: number;
  height: number;
  terrain: string[];
  units: PlaytestUnit[];
  selectedUnitId: string;
  legalMoveKeys: Set<string>;
  legalTargetIds: Set<string>;
  /** Tiles an ability being aimed could land on. Empty when nobody is aiming. */
  castTileKeys?: Set<string>;
  /**
   * Tiles an enemy could reach and strike: the engine's danger zone, drawn
   * the way the console draws it. Empty when nothing is selected.
   */
  dangerTileKeys?: Set<string>;
  /** The season this game's ground is drawn in. Absent means the default. */
  themeId?: string | undefined;
  /**
   * The style this game's characters are drawn in and the body they are drawn
   * with. Absent means the default. Either is only what a character that named
   * none of its own follows: a unit carrying its own resolved choice is drawn
   * by it, because the project's choice is a default and never a gate.
   */
  characterStyleId?: string | undefined;
  characterFigureId?: string | undefined;
  /**
   * Where one token is drawn relative to the cell it stands in, in cells, on
   * this frame. This is the whole of the board's motion: a slide along a move
   * route and a flinch away from a blow are both one token drawn away from
   * home for a counted number of frames, and both end at zero.
   *
   * The token stays in its own cell's group in the document, so the board's
   * order, its rows and every label on it are what they were. Absent means
   * nothing is moving, and then not one attribute of this board differs from
   * the board drawn before motion existed.
   */
  motion?: { unitId: string; cellDx: number; cellDy: number } | undefined;
  /**
   * Which cell of its sequence strip each posed unit is drawn as this frame,
   * by unit id. The consoles window the same strip in the same order and pick
   * the cell from the same arithmetic, `walkCell` and `strikeCell`, so a
   * walking token walks here exactly as it walks there.
   *
   * A unit not named here is drawn standing, which is its own sprite and not a
   * cell of the strip. Absent or empty therefore means every unit standing,
   * and then not one node of this board differs from the board drawn before
   * sequences existed.
   */
  sequenceCells?: Readonly<Record<string, number>> | undefined;
  /**
   * How far the water's colour ramp is rotated this frame, on the phase
   * `waterCyclePhase` counts. Zero, and absent, is the identity, and then
   * no filter is emitted and no water cell references one.
   */
  waterPhaseFrame?: number;
  /**
   * Whether the selected cell draws its pulse this frame. Frame-counted by the
   * caller, never timed, and false at phase zero, so a board sampled at rest
   * is the board that was drawn before the pulse existed.
   */
  cursorEmphasis?: boolean;
  /**
   * The cell a blow just missed, as an `"x:y"` key. The browser's half of the
   * Nintendo 64's amber miss flash: a strike that took nothing still has to
   * look like something happened.
   */
  missKey?: string | undefined;
  /**
   * The travelling mark this frame, in cell coordinates: an arrow or a bolt
   * crossing the board, or the flare a cast opens on the tile it resolves on.
   *
   * It is a mark rather than a sprite, and that is the point: it costs no
   * animation cell, and the four the strip holds were all spent on poses. It is
   * absent on the frame its gesture ends, so a settled board is node-for-node
   * what it was before any of this existed.
   */
  mark?:
    | { kind: "bolt" | "bloom"; x: number; y: number; size: number }
    | undefined;
}>();

const castKeys = computed(() => props.castTileKeys ?? new Set<string>());

/**
 * The transform one token is drawn under this frame, or `undefined` for every
 * other token and for every frame of a still board, so a board at rest emits
 * no transform attribute at all and is, node for node, the board it was.
 */
function unitTransform(unitId: string): string | undefined {
  const motion = props.motion;
  if (!motion || motion.unitId !== unitId) return undefined;
  if (motion.cellDx === 0 && motion.cellDy === 0) return undefined;
  const dx = Math.round(motion.cellDx * tile);
  const dy = Math.round(motion.cellDy * tile);
  return `translate(${dx} ${dy})`;
}
/**
 * The cell of its strip a unit is posed in this frame, or `undefined` when it
 * is standing, which is every unit on a still board, and every unit but one on
 * a moving board.
 */
function unitCell(unitId: string): number | undefined {
  const cell = props.sequenceCells?.[unitId];
  if (cell === undefined || cell === SEQUENCE_CELL_STAND) return undefined;
  if (cell < 0) return undefined;
  return cell;
}

/** The window into the strip that shows one cell, as a nested-svg viewBox. */
function cellView(cell: number): string {
  return `${cell * TILE_SIZE} 0 ${TILE_SIZE} ${TILE_SIZE}`;
}

// The water shimmer. A console rotates four entries of the palette it loaded;
// the browser has no palette, so it rotates the same four *colours* through a
// generated per-channel transfer table. An SVG filter is a lookup table, and
// this is the one the board hands its water cells. Nothing is emitted at phase
// zero, which is the identity, so a board at rest is exactly the board it was.
const waterFilterId = "water-shimmer";
const waterShimmer = computed(() => {
  const frame = props.waterPhaseFrame ?? 0;
  if (waterCyclePhase(frame) === 0) return undefined;
  return waterTransfer(projectTheme(props.themeId), frame).map(tableValues);
});
/** Which cells the filter is hung on: the water, and nothing else. */
function shimmered(terrain: string): boolean {
  return !!waterShimmer.value && terrainSheetKind(terrain) === "water";
}
const dangerKeys = computed(() => props.dangerTileKeys ?? new Set<string>());

const emit = defineEmits<{
  chooseCell: [x: number, y: number];
}>();

const tile = 100;
const gutter = 32;
// The shared presentation model settles what a level of elevation is worth: a
// quarter of a tile, which is 25 here, bounded at three eighths of a tile, 37
// here, so no cell can ever cover the centre of the cell behind it. Raised
// ground is drawn, never simulated: no rule reads it, and it never reaches a
// project file.
const elevationStep = elevationStepFor(tile);
const elevations = computed(() =>
  boardElevations(props.terrain, props.width, props.height)
);
// The room the tallest cell needs above the first row. A board with nothing
// raised needs none, so its geometry is exactly what it was before elevation
// existed: same viewBox, same transforms, same everything.
const boardHeadroom = computed(() =>
  headroom(maxElevation(elevations.value), elevationStep, tile)
);
const viewWidth = computed(() => props.width * tile + gutter);
const viewHeight = computed(() =>
  props.height * tile + gutter + boardHeadroom.value
);
const boardStyle = computed(() => ({
  "--board-width": `${Math.min(64, props.width * 4.25 + 2)}rem`
}));
const assetBase = import.meta.env.BASE_URL;
// Grouped by row, so the accessibility tree carries the grid's real
// structure: grid, then rows, then cells.
const rows = computed(() =>
  Array.from({ length: props.height }, (_, y) =>
    Array.from({ length: props.width }, (_, x) => {
      const index = y * props.width + x;
      const sprite = terrainSprite(
        props.terrain, props.width, props.height, x, y, props.themeId
      );
      return {
        x,
        y,
        index,
        terrain: props.terrain[index] ?? "unknown",
        // How far up the screen this cell and everything standing on it are
        // drawn. It is part of the transform, so the button moves with the
        // art and the click target can never drift off the tile it looks like.
        lift: lift(elevations.value[index] ?? 0, elevationStep, tile),
        sheetHref: assetBase + sprite.href,
        // The nested-svg viewBox crops the 47-variant sheet to one tile.
        sheetView: `${sprite.sx} ${sprite.sy} ${TILE_SIZE} ${TILE_SIZE}`
      };
    })
  )
);

// One tab stop for the whole board; arrow keys move between cells. Without
// this, every cell is a tab stop and a keyboard user crosses the entire map
// to reach the button after it.
const shell = ref<HTMLDivElement>();
const focusIndex = ref(0);

watch(
  () => props.width * props.height,
  (count) => {
    if (focusIndex.value >= count) focusIndex.value = 0;
  }
);

function moveFocus(x: number, y: number, dx: number, dy: number) {
  const nextX = x + dx;
  const nextY = y + dy;
  if (nextX < 0 || nextY < 0 || nextX >= props.width || nextY >= props.height) {
    return;
  }
  focusIndex.value = nextY * props.width + nextX;
  shell.value
    ?.querySelector<HTMLButtonElement>(`[data-cell="${focusIndex.value}"]`)
    ?.focus();
}

// Whoever holds the tile, by the engine's own predicate rather than by a health
// test spelled here. Somebody talked off the board or still marching towards it
// keeps coordinates, but they are not a tile anybody holds.
function unitAt(x: number, y: number) {
  return props.units.find((unit) => unit.onBoard && unit.x === x && unit.y === y);
}

function terrainClass(terrain: string) {
  const kind = terrainKind(terrain);
  return kind === "custom" ? "grass" : kind;
}

// The key under the board, and the one place a player can be told *why* a
// range stopped short of a tile they can plainly see. Ground that charges more
// than a step says so in words rather than only in the shape of the lit tiles.
const terrainLegend = computed(() => {
  const entries = new Map<string, string>();
  for (const terrain of props.terrain) {
    entries.set(terrainClass(terrain), terrain);
  }
  return [...entries].map(([kind, label]) => ({
    kind,
    label: terrainMovementCost(label) > 1 ? `${label} (heavy going)` : label,
    color: terrainColor(label, props.themeId)
  }));
});

function cellLabel(x: number, y: number, terrain: string) {
  const unit = unitAt(x, y);
  // Whose the character is, in the words the rest of the surface uses. The
  // stored value is `first`/`second`, and speaking it would make this label
  // the one place a player is read the format's own vocabulary.
  const whose = unit?.side === "first" ? "your side" : "the enemy";
  const details = unit
    ? `, ${unit.name}, ${whose}, HP ${unit.health} of ${unit.maximumHealth}`
    : "";
  // Announced so neither the aiming marker nor the danger tint is a
  // sighted-only affordance.
  const aim = castKeys.value.has(`${x}:${y}`) ? ", in range of the ability" : "";
  const danger = dangerKeys.value.has(`${x}:${y}`) ? ", the enemy can reach here" : "";
  return `Position ${x}, ${y}, ${terrain}${details}${aim}${danger}`;
}
</script>

<template>
  <div ref="shell" class="board-shell" :style="boardStyle">
    <svg
      class="tactical-board"
      role="grid"
      aria-label="The board"
      :viewBox="`0 0 ${viewWidth} ${viewHeight}`"
      preserveAspectRatio="xMidYMid meet"
    >
      <!-- The browser's lookup table for the water shimmer: three discrete
           per-channel transfers that map every step of the theme's water ramp
           onto the step the consoles would have rotated into its place. It is
           emitted only when the phase is not zero, so a board at rest carries
           no defs at all. -->
      <defs v-if="waterShimmer">
        <filter
          :id="waterFilterId" color-interpolation-filters="sRGB"
          x="0" y="0" width="100%" height="100%"
        >
          <feComponentTransfer>
            <feFuncR type="discrete" :tableValues="waterShimmer[0]" />
            <feFuncG type="discrete" :tableValues="waterShimmer[1]" />
            <feFuncB type="discrete" :tableValues="waterShimmer[2]" />
          </feComponentTransfer>
        </filter>
      </defs>
      <g class="coordinates" aria-hidden="true">
        <text v-for="x in width" :key="`x-${x}`" :x="gutter + (x - .5) * tile"
          y="21" fill="#a7b6ad">
          {{ x - 1 }}
        </text>
        <text v-for="y in height" :key="`y-${y}`" x="15"
          :y="gutter + boardHeadroom + (y - .47) * tile" fill="#a7b6ad">
          {{ y - 1 }}
        </text>
      </g>

      <!-- The board sits below the headroom the tallest cell needs, so a
           raised cell in the first row rises into empty frame instead of being
           clipped. The column numbers stay at the top of the gutter rather
           than following the board down, where a raised first row would cover
           them. -->
      <g :transform="`translate(${gutter} ${gutter + boardHeadroom})`">
        <!-- Row-major document order, and it stays correct once cells are
             lifted. A cell in row y raised by e covers screen rows
             [base(y) - e*step, base(y) - e*step + tile); the cell behind it in
             row y-1 raised by e' ends at base(y) - e'*step. The two overlap
             only when e > e', and then the nearer cell must be painted last,
             which is what row-major order already does. When the cell behind
             is the higher one there is a gap between them and no overlap at
             all. So elevation changes the offsets and nothing about the order,
             and the grid's rows keep carrying the accessibility structure. -->
        <g v-for="(row, rowIndex) in rows" :key="`row-${rowIndex}`" role="row">
        <g
          v-for="cell in row"
          :key="`${cell.x}:${cell.y}`"
          :class="[
            'board-cell',
            `terrain-${terrainClass(cell.terrain)}`,
            {
              legal: legalMoveKeys.has(`${cell.x}:${cell.y}`),
              aimed: castKeys.has(`${cell.x}:${cell.y}`),
              target: !!unitAt(cell.x, cell.y) &&
                legalTargetIds.has(unitAt(cell.x, cell.y)!.id),
              selected: unitAt(cell.x, cell.y)?.id === selectedUnitId
            }
          ]"
          :transform="`translate(${cell.x * tile} ${cell.y * tile - cell.lift})`"
        >
          <title>
            {{ unitAt(cell.x, cell.y)
              ? `${unitAt(cell.x, cell.y)!.name} HP ${unitAt(cell.x, cell.y)!.health}/${unitAt(cell.x, cell.y)!.maximumHealth}`
              : `${cell.x},${cell.y}` }}
          </title>
          <svg
            class="terrain-sprite" :width="tile" :height="tile"
            :viewBox="cell.sheetView" preserveAspectRatio="none"
            aria-hidden="true"
          >
            <image
              class="terrain-image" :href="cell.sheetHref"
              :width="BLOB_SHEET_WIDTH" :height="BLOB_SHEET_HEIGHT"
              :filter="shimmered(cell.terrain)
                ? `url(#${waterFilterId})` : undefined"
            />
          </svg>
          <!-- The danger wash sits over the ground and under everything the
               player acts on, so a threatened square reads at a glance
               without burying the move dot standing on it. -->
          <rect
            v-if="dangerKeys.has(`${cell.x}:${cell.y}`)"
            class="danger-wash" x="1.5" y="1.5" :width="tile - 3" :height="tile - 3"
            rx="5" fill="#d84034" aria-hidden="true"
          />
          <rect
            class="cell-frame" x="1.5" y="1.5" :width="tile - 3" :height="tile - 3"
            rx="5" fill="none" stroke="#172b28" stroke-width="3"
          />

          <circle v-if="legalMoveKeys.has(`${cell.x}:${cell.y}`) && !castKeys.has(`${cell.x}:${cell.y}`)"
            class="move-marker" cx="50" cy="50" r="13"
            fill="#bff4ee" stroke="#174e51" stroke-width="4" />
          <path v-if="castKeys.has(`${cell.x}:${cell.y}`)"
            class="cast-marker" d="M50 30 70 50 50 70 30 50Z"
            fill="#f7c948" stroke="#5a3d00" stroke-width="5"
            stroke-linejoin="round" />
          <path
            v-if="unitAt(cell.x, cell.y) && legalTargetIds.has(unitAt(cell.x, cell.y)!.id)"
            class="target-marker" d="M12 12h20M12 12v20M88 12H68M88 12v20M12 88h20M12 88V68M88 88H68M88 88V68"
            fill="none" stroke="#f7c948" stroke-width="7" stroke-linecap="round"
          />

          <!-- The pulse: a second ring inside the selected cell's own frame,
               up for half of every period and drawn nowhere near the cell's
               centre. Nothing at rest, which is what keeps a still board
               identical to the board drawn before it existed. -->
          <rect
            v-if="cursorEmphasis && unitAt(cell.x, cell.y)?.id === selectedUnitId"
            class="cursor-pulse" x="9" y="9" :width="tile - 18" :height="tile - 18"
            rx="4" fill="none" stroke="#f9d66b" stroke-width="3" aria-hidden="true"
          />
          <!-- A blow that took nothing still happened. The console says so in
               amber for two frames; so does this. -->
          <rect
            v-if="missKey === `${cell.x}:${cell.y}`"
            class="miss-flash" x="1.5" y="1.5" :width="tile - 3" :height="tile - 3"
            rx="5" fill="#f2c14e" opacity="0.55" aria-hidden="true"
          />

          <g
            v-if="unitAt(cell.x, cell.y)" class="unit"
            :class="[
              `unit-${unitAt(cell.x, cell.y)!.side}`,
              { spent: unitAt(cell.x, cell.y)!.hasActed }
            ]"
            :transform="unitTransform(unitAt(cell.x, cell.y)!.id)"
          >
            <!-- The shadow grounds the figure against the tile: without it a
                 sprite standing on raised ground reads as floating. It shares
                 the sprite's rectangle exactly, so it can never spill into a
                 neighbour, and it is drawn first because it belongs under
                 everyone. -->
            <image
              class="unit-shadow" x="8" y="0" width="84" height="84"
              :href="assetBase + SHADOW_SPRITE" aria-hidden="true"
            />
            <!-- Standing is the sprite's own file and a pose is a window into
                 the strip beside it, which is exactly how the consoles draw
                 the same two things: the standing sprite is frame 0 of every
                 sequence and is deliberately not a cell of the strip. -->
            <image
              v-if="unitCell(unitAt(cell.x, cell.y)!.id) === undefined"
              class="unit-sprite" x="8" y="0" width="84" height="84"
              :href="assetBase + unitSprite(
                unitAt(cell.x, cell.y)!.classId,
                unitAt(cell.x, cell.y)!.side,
                unitAt(cell.x, cell.y)!.factionColor,
                unitAt(cell.x, cell.y)!.characterStyleId
                  ?? props.characterStyleId,
                unitAt(cell.x, cell.y)!.characterFigureId
                  ?? props.characterFigureId
              )"
            />
            <svg
              v-else class="unit-frame" x="8" y="0" width="84" height="84"
              :viewBox="cellView(unitCell(unitAt(cell.x, cell.y)!.id)!)"
              preserveAspectRatio="none"
            >
              <image
                class="unit-sprite" :width="SEQUENCE_SHEET_WIDTH"
                :height="SEQUENCE_SHEET_HEIGHT"
                :href="assetBase + unitFrameSheet(
                  unitAt(cell.x, cell.y)!.classId,
                  unitAt(cell.x, cell.y)!.side,
                  unitAt(cell.x, cell.y)!.factionColor,
                  unitAt(cell.x, cell.y)!.characterStyleId
                    ?? props.characterStyleId,
                  unitAt(cell.x, cell.y)!.characterFigureId
                    ?? props.characterFigureId
                )"
              />
            </svg>
            <rect class="hp-track" x="18" y="82" width="64" height="9" rx="4"
              fill="#151c1b" stroke="#e9e6d6" stroke-width="2" />
            <rect
              class="hp-value" x="18" y="82" height="9" rx="4"
              fill="#79d06e"
              :width="64 * unitAt(cell.x, cell.y)!.health / unitAt(cell.x, cell.y)!.maximumHealth"
            />
          </g>
          <foreignObject x="1.5" y="1.5" :width="tile - 3" :height="tile - 3">
            <button
              xmlns="http://www.w3.org/1999/xhtml"
              type="button"
              role="gridcell"
              :data-cell="cell.index"
              :tabindex="cell.index === focusIndex ? 0 : -1"
              :aria-label="cellLabel(cell.x, cell.y, cell.terrain)"
              :class="[
                'interaction-surface',
                {
                  legal: legalMoveKeys.has(`${cell.x}:${cell.y}`),
                  aimed: castKeys.has(`${cell.x}:${cell.y}`),
                  target: !!unitAt(cell.x, cell.y) &&
                    legalTargetIds.has(unitAt(cell.x, cell.y)!.id),
                  selected: unitAt(cell.x, cell.y)?.id === selectedUnitId
                }
              ]"
              @click="focusIndex = cell.index; emit('chooseCell', cell.x, cell.y)"
              @keydown.enter.prevent="emit('chooseCell', cell.x, cell.y)"
              @keydown.space.prevent="emit('chooseCell', cell.x, cell.y)"
              @keydown.left.prevent="moveFocus(cell.x, cell.y, -1, 0)"
              @keydown.right.prevent="moveFocus(cell.x, cell.y, 1, 0)"
              @keydown.up.prevent="moveFocus(cell.x, cell.y, 0, -1)"
              @keydown.down.prevent="moveFocus(cell.x, cell.y, 0, 1)"
            >
              <span>
                {{ unitAt(cell.x, cell.y)
                  ? `${unitAt(cell.x, cell.y)!.name} HP ${unitAt(cell.x, cell.y)!.health}/${unitAt(cell.x, cell.y)!.maximumHealth}`
                  : `${cell.x},${cell.y}` }}
              </span>
            </button>
          </foreignObject>
        </g>
        </g>

        <!-- The travelling mark, drawn once over the whole board rather than
             inside a cell, because a bolt in flight belongs to no cell: it is
             between two of them. Bone white for a bolt, as the consoles draw
             it, and the same for a flare: the shape carries the difference, so
             no colour had to be invented for either. -->
        <circle
          v-if="mark"
          class="board-mark"
          :cx="(mark.x + 0.5) * tile"
          :cy="(mark.y + 0.5) * tile"
          :r="Math.max(1, (mark.size * tile) / 2)"
          :fill="mark.kind === 'bolt' ? '#f5ead2' : '#f5ead2'"
          :opacity="mark.kind === 'bolt' ? 0.95 : 0.45"
          aria-hidden="true"
        />
      </g>
    </svg>
    <ul class="terrain-legend" aria-label="Terrain legend">
      <li v-for="entry in terrainLegend" :key="entry.kind">
        <span class="terrain-swatch" :style="{ background: entry.color }" aria-hidden="true" />
        {{ entry.label }}
      </li>
    </ul>
  </div>
</template>

<style scoped>
.board-shell {
  width: min(100%, var(--board-width));
  padding: .45rem;
  overflow: auto;
  border: 1px solid #273a3b;
  border-radius: .7rem;
  background: #101c1d;
  box-shadow: inset 0 0 0 1px #ffffff12, 0 .7rem 1.8rem #0710103d;
}
.tactical-board { display: block; width: 100%; height: auto; max-height: 72vh; }
.coordinates text {
  fill: #a7b6ad;
  font: 600 13px ui-monospace, monospace;
  text-anchor: middle;
}
.board-cell { cursor: pointer; outline: none; }
/* Pixel art must scale by texel duplication; smoothing melts a 32-pixel
   sprite into mush. */
.terrain-image, .unit-sprite, .unit-shadow { image-rendering: pixelated; }
.terrain-sprite { transition: filter .12s; }
.cell-frame { pointer-events: none; }
.interaction-surface {
  width: 100%; height: 100%; padding: 0; border: 6px solid transparent;
  border-radius: 5px; background: transparent; color: transparent; cursor: pointer;
}
.interaction-surface span {
  position: absolute; width: 1px; height: 1px; overflow: hidden; clip-path: inset(50%);
}
.board-cell:hover .terrain-sprite { filter: brightness(1.15); }
/* Aim before focus and selection, so neither ring is swallowed by an aiming
   board: the keyboard user has to keep seeing where they are. */
.interaction-surface.aimed { border-color: #c79bff; }
.interaction-surface.selected { border-color: #f9d66b; }
.interaction-surface:focus { border-color: #fff0af; outline: none; }
/* The board says three things with colour, and every client this game runs on
   says them the same way: blue-cyan is where you may walk, red is where the
   enemy can hurt you, and amber-gold is the gesture you are aiming right
   now: the cast diamond and the ring around a character your strike could
   land on.

   Amber because it is the one hue neither of the other two occupies, so no
   overlay can be mistaken for another by hue alone; and because it is far
   brighter than the danger red, so the pair also separates by luminance. That
   second half is what makes it readable for a red-blind player, to whom a
   salmon target ring and the red wash are close to the same colour. A
   ring in that colour is the worse mistake of the two, since it draws *your*
   targets in the colour that everywhere else on the board means danger to
   you. */
.danger-wash { fill: #d84034; opacity: .34; pointer-events: none; }
.move-marker { fill: #bff4ee; stroke: #174e51; stroke-width: 4; opacity: .82; }
.cast-marker { fill: #f7c948; stroke: #5a3d00; stroke-width: 5; opacity: .88; }
.target-marker { fill: none; stroke: #f7c948; stroke-width: 7; stroke-linecap: round; }
/* Somebody who has already taken their turn: drained of colour and dimmed,
   and still standing exactly where they are. Greyed rather than hidden: a
   player reads their whole line to decide who goes next, so taking a spent
   character off the board would take that away. The health bar keeps its own
   colour, because how badly somebody is hurt is not a thing their turn spends.
   Ignored where the viewer has asked for less motion is not a concern here:
   this is a static filter and not an animation. */
.unit.spent .unit-sprite, .unit.spent .unit-frame, .unit.spent .unit-shadow {
  filter: grayscale(1) brightness(.62);
}
.hp-track { fill: #151c1b; stroke: #e9e6d6; stroke-width: 2; }
.hp-value { fill: #79d06e; }
.terrain-legend {
  display: flex; flex-wrap: wrap; gap: .35rem .8rem; margin: .5rem .25rem .1rem;
  padding: 0; list-style: none; color: #d5dfda; font-size: .78rem;
}
.terrain-legend li { display: inline-flex; align-items: center; gap: .3rem; }
.terrain-swatch {
  width: .9rem; height: .9rem; border: 1px solid #d8e0d5; border-radius: .2rem;
  background: #4d9147;
}
</style>
