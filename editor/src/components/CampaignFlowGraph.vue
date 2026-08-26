<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// The shape of a campaign, drawn, and joined up by hand.
//
// A flow is a road with stops on it and ways out of them. Stored, it is a list
// of nodes each naming identifiers; read, that list says what a game is made of
// and nothing whatever about its shape. This draws the shape: every stop where
// the road puts it, every way out as a line to where it goes, and the way out
// itself as something the author can pick up and put somewhere else.
//
// **Where the stops sit is worked out, never stored.** `flow-graph.ts` says why
// at length; the short of it is that the campaign schema has no coordinates,
// the one open slot in it is refused by the content compiler, and a layout
// derived from the road is one nobody has to maintain. So there is no dragging
// of boxes here: what an author drags is a **way out**, and dropping it on a
// stop is how they say the road goes there. That is the gesture the picture
// exists for, and it is the only thing dragging could have meant that changes
// the game.
//
// **Everything drag does, a keyboard does.** A way out is a button: pressing it
// picks it up, and pressing a stop puts it down there. Drag is the same two
// acts with a mouse, running through the same state, so neither route can do
// something the other cannot. Escape puts it back.

import { computed, onBeforeUnmount, onMounted, ref, watch } from "vue";
import type { CampaignFlow, SourceProject } from "../generated/source-v1";
import {
  FLOW_NODE_HEIGHT,
  FLOW_NODE_WIDTH,
  layOutFlow,
  type FlowGesture
} from "../domain/flow-graph";
import { nodeKindWord } from "../domain/author-words";

const props = defineProps<{
  flow: CampaignFlow | undefined;
  project: SourceProject;
  /** The stop the surface around this one wants kept in view, if any. */
  selectedNodeId?: string | undefined;
}>();

const emit = defineEmits<{
  /**
   * What the author just did to the picture: the act, not the result.
   *
   * The surface holding the project applies it to the road as that surface
   * holds it, so a gesture can never overwrite something typed into the
   * list-and-form under this picture since the picture was last drawn.
   */
  gesture: [gesture: FlowGesture];
  /** Take me to where this Stage is set up. */
  openStage: [nodeId: string];
  /** This is the stop I am looking at. */
  select: [nodeId: string];
}>();

const layout = computed(() => layOutFlow(props.flow));

/**
 * How far out this picture is drawn.
 *
 * **A campaign is wider than a screen long before it is a large campaign.** The
 * layout gives every rank a fixed pitch, which is what keeps a stop readable and
 * a line followable; the cost is that the picture grows with the campaign and
 * the panel does not. The six-Stage campaign this repository ships is already
 * about three screens across, so the one surface whose whole purpose is showing
 * the shape of a branching campaign could not show the shape of the campaign
 * beside it.
 *
 * Zooming is the answer rather than a smaller pitch, because the two questions
 * a reader has are different questions. *Where does this branch go* is asked of
 * the whole road at once and does not need the words; *what is this stop* is
 * asked of one box and does. A fixed middle size would answer neither.
 *
 * A CSS transform rather than a second layout: the geometry the lines are drawn
 * from stays in one place and one unit, so a line and the box it touches cannot
 * come to disagree about where either of them is at some scale. The wrapper is
 * sized to the scaled picture so the scrollbars stay honest.
 */
const ZOOM_STEPS = [
  { value: "100", label: "100%", scale: 1 },
  { value: "75", label: "75%", scale: 0.75 },
  { value: "50", label: "50%", scale: 0.5 },
  { value: "33", label: "33%", scale: 1 / 3 },
  { value: "25", label: "25%", scale: 0.25 }
] as const;

const zoomChoice = ref<string>("fit");
const scroller = ref<HTMLElement | undefined>();
const availableWidth = ref(0);

/**
 * The scale that puts the whole road on screen, never enlarging past life size.
 *
 * Zero available width means nothing has been measured yet, which is the first
 * frame and every environment with no layout at all. Life size is the honest
 * answer there: it is what the panel drew before this existed.
 */
const fitScale = computed(() => {
  const width = layout.value.width;
  if (availableWidth.value <= 0 || width <= 0) return 1;
  return Math.min(1, availableWidth.value / width);
});

const zoom = computed(() => {
  if (zoomChoice.value === "fit") return fitScale.value;
  const step = ZOOM_STEPS.find((entry) => entry.value === zoomChoice.value);
  return step ? step.scale : 1;
});

/** What the picture occupies once it is scaled, which is what scrolls. */
const scaledSize = computed(() => ({
  width: `${Math.ceil(layout.value.width * zoom.value)}px`,
  height: `${Math.ceil(layout.value.height * zoom.value)}px`
}));

function measure(): void {
  const element = scroller.value;
  if (!element) return;
  // The padding is the panel's own and is not part of the room a picture has.
  availableWidth.value = Math.max(0, element.clientWidth - 8);
}

let observer: ResizeObserver | undefined;
onMounted(() => {
  measure();
  // Both, rather than one or the other. The observer catches the panel being
  // resized by something other than the window -- a rail opening beside it, a
  // section changing -- and the window event catches the case where there is no
  // observer to have. Measuring twice costs a clientWidth read.
  if (typeof ResizeObserver !== "undefined" && scroller.value) {
    observer = new ResizeObserver(() => measure());
    observer.observe(scroller.value);
  }
  if (typeof window !== "undefined") {
    window.addEventListener("resize", measure);
  }
});
onBeforeUnmount(() => {
  observer?.disconnect();
  if (typeof window !== "undefined") {
    window.removeEventListener("resize", measure);
  }
});

/** The way out currently in the author's hand, if they have picked one up. */
const held = ref<{ nodeId: string; transitionId: string } | undefined>();

// A road that changes under a held way out, an undo, a Stage added elsewhere
// or another surface saving, leaves the author holding something that may no
// longer be there. Putting it down is the honest response: the alternative is a
// drop that silently refuses later for a reason nobody can see now.
watch(() => props.flow, () => { held.value = undefined; }, { deep: true });

const nodes = computed(() => props.flow?.nodes ?? []);

function nodeNamed(id: string) {
  return nodes.value.find((node) => node.id === id);
}

function nodeName(id: string): string {
  return nodeNamed(id)?.name ?? id;
}

/** The ground a Stage is fought on, in one sentence, without offering to change it. */
function ground(nodeId: string): string {
  const node = nodeNamed(nodeId);
  if (node?.kind !== "encounter") return "";
  const map = props.project.maps.find((candidate) => candidate.id === node.mapId);
  return map ? `on ${map.name}` : "no ground yet";
}

/** What arriving here says, counted rather than listed: the box is small. */
function scenesHere(nodeId: string): number {
  return nodeNamed(nodeId)?.dialogueIds?.length ?? 0;
}

const boxStyle = (box: { x: number; y: number }) => ({
  left: `${box.x}px`,
  top: `${box.y}px`,
  width: `${FLOW_NODE_WIDTH}px`,
  minHeight: `${FLOW_NODE_HEIGHT}px`
});

/** Picks a way out up, or puts back the one already in hand. */
function hold(nodeId: string, transitionId: string) {
  const holding = held.value;
  if (holding?.nodeId === nodeId && holding.transitionId === transitionId) {
    held.value = undefined;
    return;
  }
  held.value = { nodeId, transitionId };
  emit("select", nodeId);
}

function isHeld(nodeId: string, transitionId: string): boolean {
  return held.value?.nodeId === nodeId &&
    held.value.transitionId === transitionId;
}

/**
 * Puts the held way out down on a stop, which is the whole point of the
 * picture. A press on a stop with nothing in hand is just a selection.
 */
function dropOn(targetNodeId: string) {
  const holding = held.value;
  if (!holding) {
    emit("select", targetNodeId);
    return;
  }
  held.value = undefined;
  emit("gesture", {
    kind: "retarget",
    nodeId: holding.nodeId,
    transitionId: holding.transitionId,
    targetNodeId
  });
}

function startDrag(nodeId: string, transitionId: string, event: DragEvent) {
  held.value = { nodeId, transitionId };
  emit("select", nodeId);
  // The identifier goes on the transfer so a drop that reads it can, but the
  // drop below reads the held value: the same state the keyboard route sets,
  // so there is exactly one way a connection is made.
  event.dataTransfer?.setData("text/plain", `${nodeId}/${transitionId}`);
  if (event.dataTransfer) event.dataTransfer.effectAllowed = "move";
}

function give(nodeId: string) {
  emit("gesture", { kind: "addWayOut", nodeId });
}

function take(nodeId: string, transitionId: string) {
  if (isHeld(nodeId, transitionId)) held.value = undefined;
  emit("gesture", { kind: "removeWayOut", nodeId, transitionId });
}

/**
 * What one way out is, in a sentence.
 *
 * The handle itself reads "→ The Good End", which is right for an eye and
 * wrong for anything reading it aloud: an arrow is a picture of the idea and
 * not the idea. So the name it carries says the whole thing, and it doubles as
 * the tooltip a mouse gets.
 */
function wayOutLabel(nodeId: string, targetNodeId: string): string {
  return `Where ${nodeName(nodeId)} goes next: ${nodeName(targetNodeId)}. ` +
    "Pick it up and put it on another stop to send the road there.";
}

/** What the author is being asked to do, said once, above the picture. */
const instruction = computed(() => {
  const holding = held.value;
  if (!holding) {
    return "Every way out of a stop is a handle. Pick one up and put it on " +
      "another stop to send the road there: by dragging it, or by pressing " +
      "it and then pressing where it should go.";
  }
  const target = nodeNamed(holding.nodeId)?.transitions.find(
    (transition) => transition.id === holding.transitionId
  )?.targetNodeId;
  return `Holding the way out of ${nodeName(holding.nodeId)} that leads to ` +
    `${target ? nodeName(target) : "nowhere"}. Press a stop to send it there, ` +
    "or press Escape to put it back.";
});
</script>

<template>
  <section class="flow-graph-panel" aria-labelledby="flow-graph-title"
    @keydown.escape="held = undefined">
    <h3 id="flow-graph-title">The shape of this campaign</h3>
    <p class="field-help" aria-live="polite">{{ instruction }}</p>

    <p v-if="layout.boxes.length === 0" class="field-help">
      No stops yet. The first Stage you make puts itself on the road.
    </p>

    <template v-else>
      <div class="flow-zoom">
        <label for="flow-zoom">Show</label>
        <select id="flow-zoom" v-model="zoomChoice">
          <option value="fit">The whole road</option>
          <option v-for="step in ZOOM_STEPS" :key="step.value" :value="step.value">
            {{ step.label }}
          </option>
        </select>
        <span aria-live="polite">
          {{ layout.boxes.length }}
          {{ layout.boxes.length === 1 ? "stop" : "stops" }}<template
            v-if="zoom < 1">, drawn at {{ Math.round(zoom * 100) }}%</template>
        </span>
      </div>

      <div ref="scroller" class="flow-graph-scroll">
        <div class="flow-graph-scale" :style="scaledSize">
          <div class="flow-graph" :style="{
            width: `${layout.width}px`, height: `${layout.height}px`,
            transform: `scale(${zoom})`
          }">
        <!-- The lines, behind the stops. They are drawn from the geometry the
             layout worked out, so a line and the box it touches can never
             disagree about where either of them is. -->
        <svg class="flow-graph-lines" :viewBox="`0 0 ${layout.width} ${layout.height}`"
          :width="layout.width" :height="layout.height" aria-hidden="true">
          <path v-for="edge in layout.edges"
            :key="`${edge.fromNodeId}/${edge.transitionId}`"
            :class="['flow-line', { 'flow-line-back': edge.backwards }]"
            :d="edge.path" />
        </svg>

        <div v-for="box in layout.boxes" :key="box.nodeId"
          class="flow-stop-slot" :style="boxStyle(box)">
          <!-- The stop itself is the drop target, and a button so that the
               keyboard reaches it. Dropping a held way out here is the same
               act as pressing it while holding one. -->
          <button type="button"
            :class="['flow-stop', `flow-stop-${nodeNamed(box.nodeId)?.kind}`, {
              'flow-stop-stranded': box.stranded,
              'flow-stop-open': selectedNodeId === box.nodeId,
              'flow-stop-targetable': held !== undefined
            }]"
            :data-stop="box.nodeId"
            :aria-current="selectedNodeId === box.nodeId ? 'true' : undefined"
            @dragover.prevent
            @dragenter.prevent
            @drop.prevent="dropOn(box.nodeId)"
            @click="dropOn(box.nodeId)">
            <strong>{{ nodeName(box.nodeId) }}</strong>
            <small>
              {{ nodeKindWord(nodeNamed(box.nodeId)!.kind) }}
              <template v-if="nodeNamed(box.nodeId)?.kind === 'encounter'">
                · {{ ground(box.nodeId) }}
              </template>
              <template v-if="scenesHere(box.nodeId) > 0">
                · says {{ scenesHere(box.nodeId) }}
              </template>
            </small>
            <em v-if="flow && box.nodeId === flow.entryNodeId">
              The campaign starts here
            </em>
            <em v-else-if="box.stranded" class="flow-stop-warning">
              Nothing leads here
            </em>
          </button>

          <div class="flow-stop-verbs">
            <button v-if="nodeNamed(box.nodeId)?.kind === 'encounter'"
              type="button" class="secondary flow-stop-setup"
              :data-setup="box.nodeId"
              @click="emit('openStage', box.nodeId)">
              Set it up
            </button>
          </div>

          <!-- The ways out. One handle each, named for where it goes, which is
               the "X different outputs" a stop has. -->
          <ul class="flow-ways-out">
            <li v-for="transition in nodeNamed(box.nodeId)!.transitions"
              :key="transition.id">
              <button type="button"
                :class="['flow-way-out', { 'flow-way-out-held':
                  isHeld(box.nodeId, transition.id) }]"
                draggable="true"
                :data-way-out="`${box.nodeId}/${transition.id}`"
                :aria-pressed="isHeld(box.nodeId, transition.id)"
                :aria-label="wayOutLabel(box.nodeId, transition.targetNodeId)"
                :title="wayOutLabel(box.nodeId, transition.targetNodeId)"
                @dragstart="startDrag(box.nodeId, transition.id, $event)"
                @dragend="held = undefined"
                @click.stop="hold(box.nodeId, transition.id)">
                → {{ nodeName(transition.targetNodeId) }}
                <span v-if="transition.when" class="flow-way-out-when">if</span>
              </button>
              <button type="button" class="flow-way-out-drop"
                :data-remove-way-out="`${box.nodeId}/${transition.id}`"
                :aria-label="`Stop ${nodeName(box.nodeId)} leading to ` +
                  nodeName(transition.targetNodeId)"
                @click.stop="take(box.nodeId, transition.id)">×</button>
            </li>
            <li>
              <button type="button" class="flow-way-out flow-way-out-new"
                :data-add-way-out="box.nodeId"
                @click.stop="give(box.nodeId)">
                + another way out
              </button>
            </li>
          </ul>
            </div>
          </div>
        </div>
      </div>
    </template>

    <p v-if="layout.stranded.length" class="flow-graph-warning" role="status">
      Nothing on this road reaches
      {{ layout.stranded.map((id) => nodeName(id)).join(", ") }}. Put a way out
      on one of them, or take them off the road.
    </p>
  </section>
</template>

<style scoped>
/* A picture of a whole campaign, so it takes the width it is given rather than
   the reading measure every other panel in this editor is held to. */
.flow-graph-panel {
  max-width: none;
  margin: 0.75rem 0;
}
.flow-graph-scroll {
  overflow: auto;
  padding: 0.25rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.65rem;
  background: #f7f9f5;
}
/* What actually scrolls: the picture's size *after* the scale, so a scrollbar
   measures the drawing on screen rather than the geometry behind it. */
.flow-graph-scale {
  position: relative;
  overflow: hidden;
}
.flow-graph {
  position: relative;
  /* Scaled from the corner the layout's own origin is, so a box's position on
     screen is its authored position times the scale and nothing else. */
  transform-origin: top left;
}
/* The control sits above the picture rather than inside it: it is about how the
   road is drawn, not a place on the road. */
.flow-zoom {
  display: flex;
  align-items: baseline;
  gap: 0.5rem;
  margin-bottom: 0.4rem;
  font-size: 0.9rem;
}
.flow-zoom span {
  color: #55605a;
}
.flow-graph-lines {
  position: absolute;
  inset: 0;
  pointer-events: none;
}
.flow-line {
  fill: none;
  stroke: #7d8ba0;
  stroke-width: 2;
}
/* A line that goes back the way the campaign came is drawn differently,
   because "this road loops" and "this road carries on" are different facts. */
.flow-line-back {
  stroke: #b08430;
  stroke-dasharray: 5 4;
}
.flow-stop-slot {
  position: absolute;
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}
/* The colour is as load-bearing as the background. A button is white on dark
   blue by default, so a rule that pales the background of one and says nothing
   about its text writes white on white, which is what every stop's name and
   every way out's label were. Set here on the two base rules rather than on
   each modifier, because the modifiers only ever change the background. */
.flow-stop {
  display: flex;
  flex-direction: column;
  gap: 0.1rem;
  padding: 0.4rem 0.5rem;
  text-align: left;
  border: 1px solid #9aa8b8;
  border-radius: 0.5rem;
  background: #ffffff;
  color: #172033;
  cursor: pointer;
}
.flow-stop-encounter {
  border-color: #4a7ab8;
  background: #eef4fc;
}
.flow-stop-terminal {
  border-style: dashed;
}
.flow-stop-stranded {
  border-color: #a02c2c;
  background: #fdf1f1;
}
.flow-stop-open {
  outline: 2px solid #2f6f3f;
  outline-offset: 1px;
}
/* Only while something is in the author's hand, so a stop looks like a place
   to put things exactly when it is one. */
.flow-stop-targetable {
  border-style: solid;
  box-shadow: 0 0 0 2px #cfe0c6;
}
.flow-stop small {
  color: #55606f;
}
.flow-stop em {
  font-size: 0.75rem;
  color: #45607a;
}
.flow-stop-warning {
  color: #8a2020;
  font-weight: 700;
}
.flow-stop-verbs:empty {
  display: none;
}
.flow-ways-out {
  display: flex;
  flex-wrap: wrap;
  gap: 0.2rem;
  margin: 0;
  padding: 0;
  list-style: none;
}
.flow-ways-out li {
  display: flex;
  align-items: stretch;
}
.flow-way-out {
  padding: 0.15rem 0.35rem;
  font-size: 0.75rem;
  border: 1px solid #7d8ba0;
  border-radius: 0.35rem 0 0 0.35rem;
  background: #ffffff;
  color: #172033;
  cursor: grab;
}
.flow-way-out-held {
  background: #ffe9b8;
  border-color: #b08430;
  cursor: grabbing;
}
.flow-way-out-when {
  font-style: italic;
  color: #7a5a12;
}
.flow-way-out-new {
  border-radius: 0.35rem;
  border-style: dashed;
  cursor: pointer;
}
.flow-way-out-drop {
  padding: 0.15rem 0.3rem;
  font-size: 0.75rem;
  border: 1px solid #7d8ba0;
  border-left: none;
  border-radius: 0 0.35rem 0.35rem 0;
  background: #f2f4f7;
  color: #172033;
}
.flow-graph-warning {
  margin: 0.5rem 0;
  padding: 0.4rem 0.6rem;
  border: 1px solid #a02c2c;
  border-radius: 0.5rem;
  color: #8a2020;
}
</style>
