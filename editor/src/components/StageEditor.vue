<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// One Stage: a fight on a map, whole, in the one place it is set up.
//
// A map is ground and nothing else; a Stage is who fights on that ground, what
// is said around it and what winning means. Keeping them one surface cost an
// author five distinct bewilderments: a map that mentioned units only to say
// it held none, a Flow section that opened on objectives, six correct guesses
// down a campaign to reach a fight, a fresh flow whose only node was an ending,
// and a button named for the record it wrote rather than the thing it led to.
// They are two questions, so they are two sections, and this is the second one.
//
// **Everything a Stage holds is authored here and only here.** Every field
// writes the encounter node inside the campaign's flow, which is where the
// format puts it. Flow arranges how one Stage leads to the next and does not
// open the board: two doors onto one node is two places it can disagree with
// itself.
//
// **Before and after are not the same shape, and this was measured.** Both
// clients present a node's dialogues on arrival, before that node does anything
// (`platform/client/src/campaign_session.cpp:682`, and
// `campaign-playtest-session.ts` in the same order). So what is said *before*
// this Stage is this node's own `dialogueIds` and is edited here; what is said
// *after* it belongs to whatever it leads on to, and is reported here under the
// name of the node that owns it. A surface offering one symmetrical pair of
// fields would be lying about one of them.

import { computed, ref } from "vue";
import type {
  CampaignItemGrant,
  CampaignNode,
  CampaignRosterMember,
  EncounterMoment,
  EncounterPlacement,
  SourceDialogue,
  SourceObjective,
  SourceProject
} from "../generated/source-v1";
import CutsceneEditor from "./CutsceneEditor.vue";
import StageMomentsEditor from "./StageMomentsEditor.vue";
import ItemGrantEditor from "./ItemGrantEditor.vue";
import PlacementEditor from "./PlacementEditor.vue";
import RosterMemberEditor from "./RosterMemberEditor.vue";
import WinConditionEditor from "./WinConditionEditor.vue";
import { useKeystrokeDraft } from "./keystroke-draft";
import { stageAt } from "../domain/stages";
import { characterIsOnePerson } from "../domain/character-standing";
import type { CastAsk } from "../domain/stage-cast";
import {
  DEFAULT_TURN_ORDER,
  TURN_ORDERS,
  turnOrderLabel,
  type TurnOrderId
} from "../domain/game-settings";

const props = defineProps<{
  project: SourceProject;
  campaignId: string;
  nodeId: string;
}>();

const emit = defineEmits<{
  /** The whole encounter node, changed. The surface above writes the flow. */
  saveNode: [node: CampaignNode];
  /** Shared dialogue records, which belong to the project rather than the node. */
  updateDialogues: [dialogues: SourceDialogue[]];
  /** Shared objective records, the same way. */
  updateObjectives: [objectives: SourceObjective[]];
  /**
   * A way this Stage is won that the game has no record for yet. Making the
   * objective and saying this fight is decided by it are one decision, so they
   * travel as one event and are stored as one transaction.
   */
  addWayToWin: [objective: SourceObjective];
  /** Somebody a board put in the company so a placement could name them. */
  enrollMember: [member: CampaignRosterMember];
  /** A character this Stage wants and the game has not got. */
  createUnitType: [];
  /**
   * Somebody the board put on a tile who is not in this game yet. One act:
   * making them and standing them there is a single thing the author did, and
   * the surface above writes it as one.
   */
  castCharacter: [ask: Omit<CastAsk, "campaignId" | "nodeId">];
  /** An item this Stage hands over and the game has not got. */
  createItem: [];
  /** Take me to the graph, at this node or one this Stage leads to. */
  openInFlow: [nodeId: string];
  /** Draw a map, because this Stage has no ground to be fought on. */
  drawMap: [];
  /** Close this Stage and go back to the list of them. */
  close: [];
  /** A keystroke that is not in the project yet, so the header can say so. */
  dirty: [];
}>();

/**
 * This Stage's own free text and numbers, while they are being typed.
 *
 * Every field here emits the whole encounter node, which the surface above
 * writes into the campaign at the cost of one undo entry, so a keystroke is
 * held, announced as unsaved and drawn by the control that owns it, and one
 * edit is committed when the field is left or a Save reaches `flush`.
 *
 * The capacity is held for a further reason. Clearing the box and typing "12"
 * passes through nothing at all, which removes the whole deployment region when
 * it has no tiles, and then through "1", a Stage that fields one person. Half a
 * number is not a number.
 */
const keystrokes = useKeystrokeDraft(() => emit("dirty"));

const placements = ref<InstanceType<typeof PlacementEditor>>();
const winConditions = ref<InstanceType<typeof WinConditionEditor>>();
const recruits = ref<InstanceType<typeof RosterMemberEditor>>();
const grants = ref<InstanceType<typeof ItemGrantEditor>>();

/**
 * Commits every keystroke this Stage is holding, its own and its lists',
 * so a Save persists what the author can see. Always true: nothing here is
 * refused, and whether the resulting node is one the format accepts is
 * answered where the project is checked.
 */
function flush(): boolean {
  let flushed = keystrokes.flush();
  flushed = (placements.value?.flush() ?? true) && flushed;
  flushed = (winConditions.value?.flush() ?? true) && flushed;
  flushed = (recruits.value?.flush() ?? true) && flushed;
  flushed = (grants.value?.flush() ?? true) && flushed;
  return flushed;
}

defineExpose({ flush });

const campaign = computed(() =>
  (props.project.campaigns ?? []).find(
    (candidate) => candidate.id === props.campaignId
  )
);

const node = computed(() =>
  campaign.value?.flow?.nodes.find((candidate) => candidate.id === props.nodeId)
);

/** The ground this Stage is fought on, while one is chosen and still here. */
const map = computed(() =>
  props.project.maps.find((candidate) => candidate.id === node.value?.mapId)
);

/**
 * Everybody this campaign can field: the company it starts with and everybody
 * any node hands it later. The same rule the flow editor uses, for the same
 * reason: a Stage may field a recruit who joins after it, because the author
 * is writing a road rather than walking one.
 */
const members = computed<readonly CampaignRosterMember[]>(() => [
  ...(campaign.value?.roster ?? []),
  ...(campaign.value?.flow?.nodes ?? []).flatMap((entry) => entry.recruits ?? [])
]);

/**
 * Identities this campaign already spends outside this node's recruit list, so
 * two people cannot be given one name. The founding company and every other
 * node's recruits are one namespace.
 */
const idsHeldElsewhere = computed<readonly string[]>(() => [
  ...(campaign.value?.roster ?? []).map((member) => member.id),
  ...(campaign.value?.flow?.nodes ?? [])
    .filter((candidate) => candidate.id !== props.nodeId)
    .flatMap((candidate) => (candidate.recruits ?? []).map((m) => m.id))
]);

/**
 * What is said once this Stage is done, and which node says it.
 *
 * Read through the same summary the list above is drawn from, so the two can
 * never disagree about what follows a Stage.
 */
const aftermath = computed(() =>
  stageAt(props.project, props.campaignId, props.nodeId)?.after ?? []
);

/**
 * This game's characters, each carrying whether they are one person.
 *
 * The board is given a Stage rather than the whole project, and "is this
 * character somebody a company holds by name" is a question about every
 * campaign at once. So it is answered here, where the project is, and handed
 * down as an answer, never as a field an author was asked to tick.
 */
const characters = computed(() =>
  props.project.unitTypes.map((unitType) => ({
    ...unitType,
    onePerson: characterIsOnePerson(props.project, unitType.id)
  }))
);

const gameTurnOrder = computed(() =>
  turnOrderLabel(props.project.defaultTurnOrder ?? DEFAULT_TURN_ORDER)
);

const recruitHelp =
  "They join when this Stage is done, and are a stranger to every earlier one.";

const grantHelp =
  "Put in the company's store as this Stage completes. A road that loops past " +
  "here twice is given it twice.";

/**
 * The largest cap that can ever refuse anybody.
 *
 * A cap at or above the number of characters this Stage stands on the field
 * can never bind, because the board has nobody else to send. The compiler
 * refuses one, and a child's first campaign hit it: a cap of thirty over eight
 * characters. So the control states the real bound rather than the format's
 * 4095, which is the shape the store's add gesture was given for the same
 * reason - a surface that offers what the format refuses is a surface that
 * needs the validator to apologise for it.
 *
 * Zero when a Stage fields one character or none, since no cap can bind there.
 * The control is still reachable in that case if a cap is already written, or
 * an author who arrived at one by deleting placements could never clear it.
 */
const capacityMost = computed(() => {
  const fieldable = (node.value?.placements ?? []).filter(
    (placement) => placement.side === "first"
  ).length;
  return Math.max(0, fieldable - 1);
});

const capacityHelp = computed(() =>
  capacityMost.value === 0
    ? "This Stage fields too few for a cap to mean anything. Leave it empty."
    : "A maximum and not a quota: sending fewer is legal. Leave it empty for " +
      `no cap. At most ${capacityMost.value}, since this Stage fields ` +
      `${capacityMost.value + 1}.`
);

const notesHelp = "For you, not for the game. No rule ever reads it.";

const turnOrderHelp =
  "Choosing one here states it for this Stage alone, and the game setting no " +
  "longer reaches it.";

/**
 * The node as plain data.
 *
 * Everything here reads through the project the workspace hands down, which is
 * a Vue reactive proxy, and a structured clone cannot copy one. This is that
 * boundary, the same one `ContentWorkspace` crosses before every session
 * write, and crossing it here keeps the emitted record something the session
 * can store without knowing where it came from.
 */
function plainNode(): CampaignNode | undefined {
  const current = node.value;
  return current
    ? JSON.parse(JSON.stringify(current)) as CampaignNode
    : undefined;
}

/** A change to the node, as one whole record for the surface above to store. */
function change(patch: Partial<CampaignNode>) {
  const current = plainNode();
  if (!current) return;
  emit("saveNode", { ...current, ...patch });
}

function rename(name: string) {
  if (name.trim() === "") return;
  change({ name });
}

/**
 * The ground this Stage is fought on.
 *
 * An empty choice removes the field rather than storing an empty string: a
 * Stage that names no map has not been given ground yet, and the format reads
 * an absent `mapId` that way. Everything below that needs a board says so
 * instead of drawing an imaginary one.
 */
function chooseMap(mapId: string) {
  const next = plainNode();
  if (!next) return;
  if (mapId === "") delete next.mapId;
  else next.mapId = mapId;
  emit("saveNode", next);
}

function savePlacements(placements: EncounterPlacement[]) {
  const next = plainNode();
  if (!next) return;
  // An absent list and an empty one say different things about a board, and
  // the format takes the absent one for a Stage nobody stands on yet.
  if (placements.length > 0) next.placements = placements;
  else delete next.placements;
  emit("saveNode", next);
}

function saveDialogueIds(ids: string[]) {
  const next = plainNode();
  if (!next) return;
  if (ids.length > 0) next.dialogueIds = ids;
  else delete next.dialogueIds;
  emit("saveNode", next);
}

function saveMoments(moments: EncounterMoment[]) {
  const next = plainNode();
  if (!next) return;
  // An absent field rather than an empty list, because a battle nobody speaks
  // during is what every Stage authored before moments existed was, and the two
  // should compile to the same bytes.
  if (moments.length > 0) next.moments = moments;
  else delete next.moments;
  emit("saveNode", next);
}

/** What the author called a character, for a sentence that names one. */
function nameOfUnitType(id: string): string | undefined {
  return (props.project.unitTypes ?? []).find((type) => type.id === id)?.name;
}

function saveObjectiveIds(ids: string[]) {
  const next = plainNode();
  if (!next) return;
  if (ids.length > 0) next.objectiveIds = ids;
  else delete next.objectiveIds;
  emit("saveNode", next);
}

function saveRecruits(recruits: CampaignRosterMember[]) {
  const next = plainNode();
  if (!next) return;
  if (recruits.length > 0) next.recruits = recruits;
  else delete next.recruits;
  emit("saveNode", next);
}

function saveGrants(grants: CampaignItemGrant[]) {
  const next = plainNode();
  if (!next) return;
  if (grants.length > 0) next.grants = grants;
  else delete next.grants;
  emit("saveNode", next);
}

/**
 * The board's own turn order, or nothing at all.
 *
 * The empty choice deletes the field rather than writing today's default into
 * it. That distinction is the whole feature: a Stage that states nothing
 * follows the game and moves when the game's setting does, while one that
 * states an order keeps it, and a control that wrote `alternating` merely
 * because it was showing it would turn every opened Stage into an override.
 */
function saveTurnOrder(raw: string) {
  const next = plainNode();
  if (!next) return;
  if (raw === "") delete next.turnOrder;
  else next.turnOrder = raw as TurnOrderId;
  emit("saveNode", next);
}

/**
 * How many of the company this Stage lets take the field.
 *
 * The deployment object is created around the number when there is none, since
 * it needs an identity so that a diagnostic can name it and one derived from
 * the node is stable across edits. It is removed entirely when the number is cleared
 * and there is no region left in it. That is not tidiness: a deployment that
 * states neither tiles nor a capacity says nothing, and the compiler refuses it
 * rather than reading it as an omitted one.
 */
function saveCapacity(raw: string) {
  const next = plainNode();
  if (!next) return;
  const parsed = Number.parseInt(raw.trim(), 10);
  if (raw.trim() === "" || Number.isNaN(parsed)) {
    if (!next.deployment) return;
    if ((next.deployment.tiles?.length ?? 0) > 0) delete next.deployment.capacity;
    else delete next.deployment;
  } else {
    next.deployment = { ...(next.deployment ?? { id: `${next.id}_deployment` }) };
    next.deployment.capacity = parsed;
  }
  emit("saveNode", next);
}

/**
 * A note about where the author's own side stands, on the region that holds
 * it. An emptied note is removed rather than stored as an empty string, so a
 * region with nothing written on it reads exactly like one never annotated.
 */
function saveDeploymentNotes(raw: string) {
  const next = plainNode();
  if (!next?.deployment) return;
  next.deployment = { ...next.deployment };
  if (raw.trim() === "") delete next.deployment.notes;
  else next.deployment.notes = raw;
  emit("saveNode", next);
}
</script>

<template>
  <section v-if="node" class="stage-editor" aria-labelledby="stage-title">
    <div class="stage-head">
      <div>
        <h4 id="stage-title">{{ node.name }}</h4>
        <p class="field-help">
          <template v-if="map">
            A Stage fought on {{ map.name }}, in
          </template>
          <template v-else>
            A Stage with no ground chosen yet, in
          </template>
          <strong>{{ campaign?.name ?? campaignId }}</strong>.
        </p>
      </div>
      <div class="stage-verbs">
        <button type="button" class="secondary" @click="emit('openInFlow', nodeId)">
          Show where it comes in the campaign
        </button>
        <button type="button" class="secondary" @click="emit('close')">
          Back to the Stages
        </button>
      </div>
    </div>

    <!-- `input` holds the keystroke and announces it, `change` commits it.
         Both, because they answer different questions: whether the editor knows
         there is work in progress, and when that work becomes a step an author
         can undo. -->
    <label for="stage-name">What this Stage is called</label>
    <input id="stage-name" :value="keystrokes.shown('stage-name', node.name)"
      @input="keystrokes.type(
        'stage-name', ($event.target as HTMLInputElement).value, rename
      )"
      @change="keystrokes.leave(
        'stage-name', ($event.target as HTMLInputElement).value, rename
      )">

    <!-- The ground, first, because everything below stands on it. -->
    <label for="stage-map">The ground it is fought on</label>
    <select id="stage-map" :value="node.mapId ?? ''"
      @change="chooseMap(($event.target as HTMLSelectElement).value)">
      <option value="">No ground chosen yet</option>
      <option v-for="candidate in project.maps" :key="candidate.id"
        :value="candidate.id">
        {{ candidate.name }} ({{ candidate.width }}×{{ candidate.height }})
      </option>
    </select>
    <p v-if="project.maps.length === 0" class="field-help">
      This game has no ground to fight over yet.
      <button type="button" class="secondary" @click="emit('drawMap')">
        Draw a map
      </button>
    </p>
    <p v-else class="field-help">
      Maps can be reused in several Stages.
    </p>

    <!-- Before. This node's own scenes, edited here, because arriving at a node
         is what plays them and the arrival happens before the board. -->
    <section class="stage-before" aria-labelledby="stage-before-title">
      <h5 id="stage-before-title">Before the fighting</h5>
      <p class="field-help">What is said on the way in.</p>
      <CutsceneEditor
        :dialogue-ids="node.dialogueIds ?? []"
        :dialogues="project.dialogues ?? []"
        :project="project"
        @update-ids="saveDialogueIds"
        @update-dialogues="emit('updateDialogues', $event)" />
    </section>


    <PlacementEditor
      ref="placements"
      @dirty="emit('dirty')"
      :placements="node.placements ?? []"
      :map="map"
      :unit-types="characters"
      :factions="project.factions ?? []"
      :character-style-id="project.characterStyleId"
      :members="members"
      :theme-id="project.themeId"
      @update="savePlacements"
      @enroll="emit('enrollMember', $event)"
      @add-character="emit('castCharacter', $event)" />

    <!-- During. A node's own scenes play on arrival, before the node acts,
         which is around a battle; these are inside one. Authored after the
         board, because every occasion but one is about somebody standing on
         it. -->
    <StageMomentsEditor
      :moments="node.moments ?? []"
      :placements="node.placements ?? []"
      :dialogues="project.dialogues ?? []"
      :unit-type-name="nameOfUnitType"
      @update="saveMoments"
      @update-dialogues="emit('updateDialogues', $event)" />

    <WinConditionEditor
      ref="winConditions"
      @dirty="emit('dirty')"
      :objectives="project.objectives ?? []"
      :selected-ids="node.objectiveIds ?? []"
      :placements="node.placements ?? []"
      @update-objectives="emit('updateObjectives', $event)"
      @update-selection="saveObjectiveIds"
      @add="emit('addWayToWin', $event)" />

    <label for="stage-deployment-capacity">How many may take the field</label>
    <input id="stage-deployment-capacity" type="number" min="1"
      :max="capacityMost"
      :disabled="capacityMost === 0 && node.deployment?.capacity === undefined"
      step="1"
      :value="keystrokes.shown(
        'stage-capacity', String(node.deployment?.capacity ?? '')
      )"
      @input="keystrokes.type(
        'stage-capacity', ($event.target as HTMLInputElement).value, saveCapacity
      )"
      @change="keystrokes.leave(
        'stage-capacity', ($event.target as HTMLInputElement).value, saveCapacity
      )">
    <p class="field-help">{{ capacityHelp }}</p>

    <!-- The region itself has no tile picker yet, so its tiles are carried
         through untouched; the note beside them is a field the format holds
         and it would otherwise be a field no control writes. -->
    <template v-if="node.deployment">
      <label for="stage-deployment-notes">
        Notes on where your side stands
      </label>
      <textarea id="stage-deployment-notes"
        :value="keystrokes.shown(
          'stage-deployment-notes', node.deployment.notes ?? ''
        )"
        @input="keystrokes.type(
          'stage-deployment-notes',
          ($event.target as HTMLTextAreaElement).value,
          saveDeploymentNotes
        )"
        @change="keystrokes.leave(
          'stage-deployment-notes',
          ($event.target as HTMLTextAreaElement).value,
          saveDeploymentNotes
        )"></textarea>
      <p class="field-help">{{ notesHelp }}</p>
    </template>

    <label for="stage-turn-order">Turn order</label>
    <select id="stage-turn-order" :value="node.turnOrder ?? ''"
      @change="saveTurnOrder(($event.target as HTMLSelectElement).value)">
      <option value="">Follow the game setting ({{ gameTurnOrder }})</option>
      <option v-for="order in TURN_ORDERS" :key="order.id" :value="order.id">
        {{ order.label }}
      </option>
    </select>
    <p class="field-help">{{ turnOrderHelp }}</p>

    <RosterMemberEditor
      ref="recruits"
      @dirty="emit('dirty')"
      id-prefix="recruit"
      heading="Who joins the company here"
      :help="recruitHelp"
      member-word="recruit"
      :members="node.recruits ?? []"
      :unit-types="project.unitTypes"
      :other-ids="idsHeldElsewhere"
      @update="saveRecruits"
      @create-unit-type="emit('createUnitType')" />

    <ItemGrantEditor
      ref="grants"
      @dirty="emit('dirty')"
      id-prefix="node-grant"
      heading="What the company is given here"
      :help="grantHelp"
      grant-word="grant"
      :grants="node.grants ?? []"
      :items="project.items"
      @update="saveGrants"
      @create-item="emit('createItem')" />

    <!-- After. Not a field: the scenes belong to whatever this leads to, and
         are edited on that node. Reported by name so the author can go there
         rather than wonder where it went. -->
    <section class="stage-after" aria-labelledby="stage-after-title">
      <h5 id="stage-after-title">After the fighting</h5>
      <p class="field-help">
        Said by whatever this leads to, and changed there.
      </p>
      <p v-if="aftermath.length === 0" class="field-help">
        This Stage leads nowhere yet.
      </p>
      <ul v-else class="stage-after-list">
        <li v-for="entry in aftermath" :key="entry.nodeId">
          <strong>{{ entry.nodeName }}</strong>
          <span v-if="entry.scenes.length">
            says {{ entry.scenes.join(", ") }}.
          </span>
          <span v-else>says nothing.</span>
          <button type="button" class="secondary"
            @click="emit('openInFlow', entry.nodeId)">
            Change what {{ entry.nodeName }} says
          </button>
        </li>
      </ul>
    </section>
  </section>
  <p v-else class="field-help" role="status">
    That Stage is no longer in this game.
  </p>
</template>

<style scoped>
.stage-editor {
  /* A Stage holds a board, and a board is the one thing here worth the window's
   * width. Held to the reading width every other section takes, the board, the
   * palette and the panel about the selected character could only stack. */
  max-width: none;
  margin: 0.75rem 0;
  padding: 0.75rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.65rem;
  background: #f5f7f2;
}
.stage-head {
  display: flex;
  flex-wrap: wrap;
  gap: 0.75rem;
  align-items: flex-start;
  justify-content: space-between;
}
.stage-head h4 {
  margin: 0 0 0.25rem;
}
.stage-verbs {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
}
.stage-before,
.stage-after {
  margin: 0.75rem 0;
}
.stage-before h5,
.stage-after h5 {
  margin: 0 0 0.25rem;
  font-size: 1rem;
}
.stage-after-list {
  display: grid;
  gap: 0.35rem;
  margin: 0.5rem 0 0;
  padding: 0;
  list-style: none;
}
.stage-after-list li {
  display: flex;
  flex-wrap: wrap;
  gap: 0.4rem;
  align-items: baseline;
  padding: 0.4rem 0.5rem;
  background: #ffffff;
  border: 1px solid #c7d2ca;
  border-radius: 0.4rem;
}
</style>
