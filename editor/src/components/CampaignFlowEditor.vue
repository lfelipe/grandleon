<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { computed, ref, watch } from "vue";
import type {
  CampaignFlow,
  CampaignItemGrant,
  CampaignRosterMember,
  SourceDialogue,
  SourceObjective
} from "../generated/source-v1";
import type { SpeakerPortraitProject } from "../domain/board-art";
import CutsceneEditor from "./CutsceneEditor.vue";
import ItemGrantEditor from "./ItemGrantEditor.vue";
import RosterMemberEditor from "./RosterMemberEditor.vue";
import { useKeystrokeDraft } from "./keystroke-draft";
import { nodeKindWord } from "../domain/author-words";

interface EditablePredicate {
  kind: "objectiveResult" | "inventoryAtLeast" | "worldFlagEquals";
  objectiveId?: string;
  result?: string;
  itemId?: string;
  quantity?: number;
  flagId?: string;
  value?: boolean | number | string;
}

interface EditableTransition {
  id: string;
  targetNodeId: string;
  priority: number;
  when?: EditablePredicate;
  notes?: string;
}

interface EditableNode {
  id: string;
  name: string;
  kind: "encounter" | "story" | "terminal";
  mapId?: string;
  placements?: EditablePlacement[];
  objectiveIds?: string[];
  dialogueIds?: string[];
  turnOrder?: "alternating" | "sideBlocks" | "initiative";
  transitions: EditableTransition[];
  notes?: string;
  recruits?: CampaignRosterMember[];
  grants?: CampaignItemGrant[];
  // What an encounter says about the player's own troops before the first
  // activation. The tiles are carried through the draft untouched: there is no
  // tile picker for them yet, and dropping a field this editor cannot draw
  // would delete an author's content by opening it. The capacity beside
  // them is authored below.
  deployment?: {
    id: string;
    tiles?: { x: number; y: number }[];
    capacity?: number;
    notes?: string;
  };
}

interface EditablePlacement {
  id: string;
  memberId?: string;
  unitTypeId: string;
  side: "first" | "second";
  x: number;
  y: number;
  behavior?: "hold" | "patrol" | "pursue";
  patrolPoints?: { x: number; y: number }[];
}

interface EditableFlow {
  contractVersion: "1.0.0";
  entryNodeId: string;
  nodes: EditableNode[];
}

const props = defineProps<{
  flow: CampaignFlow | undefined;
  maps: readonly {
    readonly id: string;
    readonly name: string;
    readonly width: number;
    readonly height: number;
    readonly terrain: readonly string[];
  }[];
  unitTypes: readonly {
    readonly id: string;
    readonly name: string;
    readonly classId?: string;
    readonly factionId?: string;
  }[];
  /**
   * The game itself, for the surfaces below that draw a character rather than
   * arrange one: the scene preview resolves a speaker into the same face the
   * board and both consoles draw, which needs the per-character overrides the
   * flat lists above do not carry.
   */
  project?: SpeakerPortraitProject | undefined;
  objectives: readonly SourceObjective[];
  dialogues: readonly SourceDialogue[];
  items: readonly { readonly id: string; readonly name: string }[];
  /**
   * The company this campaign begins with, owned and saved by the surface
   * around this one. The flow only reads it: a Stage here fields one of these
   * people, and a node here may hand the company somebody new.
   */
  roster?: readonly CampaignRosterMember[] | undefined;
}>();

const emit = defineEmits<{
  updateObjectives: [objectives: SourceObjective[]];
  updateDialogues: [dialogues: SourceDialogue[]];
  save: [flow: CampaignFlow];
  createUnitType: [];
  createItem: [];
  dirty: [];
  /** Take me to where this Stage is set up. Flow arranges Stages; it does not
   *  open the board, and this is the door to the one place that does. */
  openStage: [nodeId: string];
}>();

function copyFlow(flow: CampaignFlow | undefined): EditableFlow | undefined {
  return flow
    ? JSON.parse(JSON.stringify(flow)) as EditableFlow
    : undefined;
}

const draft = ref(copyFlow(props.flow));
const selectedNodeId = ref(draft.value?.entryNodeId ?? "");
const problems = ref<readonly string[]>([]);
const notice = ref("");

// Editing a shared record from inside this surface, an objective or a dialogue
// line, refreshes the whole project, which hands this component a new `flow`
// object with the same content. Resetting the draft on that identity churn
// would silently discard unsaved flow edits, so only a flow that actually
// differs from the one last loaded or saved replaces the draft.
let loadedFlow = JSON.stringify(props.flow ?? null);

watch(
  () => props.flow,
  (flow) => {
    const serialized = JSON.stringify(flow ?? null);
    if (serialized === loadedFlow) return;
    loadedFlow = serialized;
    draft.value = copyFlow(flow);
    selectedNodeId.value = draft.value?.entryNodeId ?? "";
  },
  { deep: true }
);

// An unsaved flow draft is real work; the project header and the
// close-the-tab guard must know about it.
watch(
  draft,
  (value) => {
    if (JSON.stringify(value ?? null) !== loadedFlow) emit("dirty");
  },
  { deep: true }
);

/**
 * A node's identity, while it is being typed.
 *
 * Everything else on this page writes straight into the draft above, which is
 * both what the controls draw and what a Save flushes, so a keystroke is
 * already safe the moment it lands. An identity is not like that: renaming a
 * node re-points every branch that leads to it, and the rename is refused
 * outright when the words so far are empty or are some other node's name, both
 * of which every half-typed identity passes through. So this one is held until
 * the field is left, and the graph is rewritten once.
 */
const keystrokes = useKeystrokeDraft(() => emit("dirty"));

/** The two lists a story stop carries, which hold keystrokes of their own. */
const nodeRecruits = ref<InstanceType<typeof RosterMemberEditor>>();
const nodeGrants = ref<InstanceType<typeof ItemGrantEditor>>();

const selectedNode = computed(() =>
  draft.value?.nodes.find((node) => node.id === selectedNodeId.value)
);
/**
 * The ground a Stage is fought on, said in one sentence so the graph can name
 * it without offering to change it. Choosing the ground is part of setting the
 * Stage up, and that happens in one place.
 */
function stageGround(node: EditableNode): string {
  const map = props.maps.find((candidate) => candidate.id === node.mapId);
  if (map) return `A Stage fought on ${map.name}.`;
  return "A Stage with no ground chosen yet.";
}

/** Identities this campaign already spends outside one node's recruit list. */
function idsHeldElsewhere(node: EditableNode): readonly string[] {
  return [
    ...(props.roster ?? []).map((member) => member.id),
    ...(draft.value?.nodes ?? [])
      .filter((candidate) => candidate !== node)
      .flatMap((candidate) => (candidate.recruits ?? []).map((m) => m.id))
  ];
}

const recruitHelp =
  "They join when this node is done, and are a stranger to every earlier one.";

function updateRecruits(node: EditableNode, recruits: CampaignRosterMember[]) {
  if (recruits.length > 0) node.recruits = recruits;
  else delete node.recruits;
}

const grantHelp =
  "Put in the company's store as this node completes. A road that loops past " +
  "here twice is given it twice.";

function updateGrants(node: EditableNode, grants: CampaignItemGrant[]) {
  if (grants.length > 0) node.grants = grants;
  else delete node.grants;
}

const notesHelp = "For you, not for the game. No rule ever reads it.";

/**
 * A note the author writes to themselves, on anything in the flow that carries
 * one. The same field is edited on roster members and item grants, and the
 * three things authored here carry it too, a node, a branch and a deployment
 * region: a field the format holds and no control writes is a field an author
 * cannot use.
 *
 * An emptied note is removed rather than stored as an empty string, so a thing
 * with nothing written on it reads exactly like one that was never annotated.
 */
function updateNotes(owner: { notes?: string }, raw: string) {
  const written = raw.trim();
  if (written === "") delete owner.notes;
  else owner.notes = raw;
}

function uniqueId(prefix: string, ids: readonly string[]): string {
  if (!ids.includes(prefix)) return prefix;
  let suffix = 2;
  while (ids.includes(`${prefix}_${suffix}`)) suffix += 1;
  return `${prefix}_${suffix}`;
}

function createFlow() {
  draft.value = {
    contractVersion: "1.0.0",
    entryNodeId: "start",
    nodes: [{
      id: "start",
      name: "Start",
      kind: "terminal",
      transitions: []
    }]
  };
  selectedNodeId.value = "start";
}

function addNode() {
  if (!draft.value) return;
  keystrokes.flush();
  const id = uniqueId("new_node", draft.value.nodes.map((node) => node.id));
  draft.value.nodes.push({
    id,
    name: "New Node",
    kind: "terminal",
    transitions: []
  });
  selectedNodeId.value = id;
}

/** Appends a terminal ending node the flow can finish on, and returns it. */
function createEndingNode(): EditableNode {
  const id = uniqueId("ending", draft.value!.nodes.map((node) => node.id));
  const ending: EditableNode = {
    id,
    name: "Ending",
    kind: "terminal",
    transitions: []
  };
  draft.value!.nodes.push(ending);
  return ending;
}

// A transition that leads back to the node it leaves is almost never what an
// author wants, so the default targets some other node, creating a fresh
// ending when the flow has nowhere else to go yet.
function appendTransition(node: EditableNode) {
  if (!draft.value) return;
  const target =
    draft.value.nodes.find((candidate) => candidate.id !== node.id) ??
    createEndingNode();
  const id = uniqueId(
    "next",
    node.transitions.map((transition) => transition.id)
  );
  const priorities = node.transitions.map((transition) => transition.priority);
  node.transitions.push({
    id,
    targetNodeId: target.id,
    priority: priorities.length === 0 ? 0 : Math.max(...priorities) + 1
  });
}

function addTransition() {
  if (!draft.value || !selectedNode.value) return;
  const node = selectedNode.value;
  if (node.kind === "terminal") node.kind = "story";
  appendTransition(node);
}

function changeKind(node: EditableNode, kind: EditableNode["kind"]) {
  if (kind === node.kind) return;
  const wasStage = node.kind === "encounter";
  node.kind = kind;
  const notices: string[] = [];
  // Only a Stage has a board. A node that stops being one cannot carry
  // placements at all, the schema forbidding them there, and a company member
  // bound to one of those placements would be somebody standing on a Stage
  // nobody fights, so the board goes with the kind rather than lingering as a
  // save the author is refused until they clean it up by hand.
  if (wasStage && kind !== "encounter") {
    const fielded = node.placements?.length ?? 0;
    if (fielded > 0) {
      notices.push(
        `'${node.name}' is no longer a Stage, so its ${fielded} character ` +
        `placement${fielded === 1 ? "" : "s"} went with it. Make it a Stage ` +
        "again before saving if that was a mis-click."
      );
    }
    delete node.placements;
    // The region goes with the board for the same reason, and the schema
    // forbids it on anything but a Stage.
    if (node.deployment !== undefined) {
      notices.push(
        `'${node.name}' is no longer a Stage, so its deployment region ` +
        "went with it. Make it a Stage again before saving if that was a " +
        "mis-click."
      );
      delete node.deployment;
    }
  }
  // The schema requires a non-terminal node to lead somewhere. Leftover
  // transitions on a new terminal are kept and reported at save so a
  // mis-click destroys nothing an ending cannot get back.
  if (kind !== "terminal" && node.transitions.length === 0) {
    appendTransition(node);
    const target = node.transitions[0]!.targetNodeId;
    notices.push(
      `Added a transition to '${target}' so '${node.name}' leads somewhere. ` +
      "Retarget it below if that is not where the story goes."
    );
  }
  notice.value = notices.join(" ");
}

function nodeLabel(node: EditableNode): string {
  return `'${node.name}' (${node.id})`;
}

/** The plain name of one member of the company. */
function memberLabel(member: CampaignRosterMember): string {
  return `'${member.name}' (${member.id})`;
}

/**
 * Who this campaign holds, and everything wrong with them. Reports into the
 * shared list and returns the company by identity so the boards below can ask
 * who somebody is.
 *
 * The company is the campaign's, not the flow's, and its founding members are
 * saved by the surface around this one. But a flow whose Stages field a
 * company that cannot exist is a flow that will not open, so the same save is
 * refused here.
 */
function memberProblems(
  flow: EditableFlow,
  found: string[]
): Map<string, CampaignRosterMember> {
  const roster = props.roster ?? [];
  const members = new Map<string, CampaignRosterMember>();
  const written = [
    ...roster,
    ...flow.nodes.flatMap((node) => node.recruits ?? [])
  ];
  for (const member of written) {
    if (member.id === "") {
      found.push(
        `A member of this campaign's company has no identifier. Give ` +
        `'${member.name}' one: it is how a Stage names them.`
      );
      continue;
    }
    if (members.has(member.id)) {
      found.push(
        `Two people in this campaign are '${member.id}'. Rename one: a member ` +
        "is one character, and a Stage fields them by that identifier."
      );
      continue;
    }
    members.set(member.id, member);
    if (member.name.trim() === "") {
      found.push(`The member '${member.id}' has no name. Give them one.`);
    }
    if (!props.unitTypes.some((unitType) => unitType.id === member.unitTypeId)) {
      found.push(
        `${memberLabel(member)} is '${member.unitTypeId}', which is not a ` +
        "character in this project. Choose who they are."
      );
    }
  }
  if (roster.length === 0) {
    found.push(
      "This campaign starts with nobody. Add at least one member to the " +
      "company it begins with. A campaign that is played and kept has to " +
      "march out with somebody, and a recruit who joins later cannot be the " +
      "first."
    );
  }
  return members;
}

/**
 * Everything that would make this flow fail to validate or reopen, in plain
 * words. The source schema and analyzer are the authority; this keeps the
 * editor from handing them a flow they will reject.
 */
function flowProblems(flow: EditableFlow): string[] {
  const found: string[] = [];
  const members = memberProblems(flow, found);
  const nodeIds = new Set<string>();
  for (const node of flow.nodes) {
    if (nodeIds.has(node.id)) {
      found.push(`Two nodes share the identifier '${node.id}'. Rename one.`);
    }
    nodeIds.add(node.id);
  }
  if (!nodeIds.has(flow.entryNodeId)) {
    found.push(
      `The entry node '${flow.entryNodeId}' does not exist. Pick another entry node.`
    );
  }
  for (const node of flow.nodes) {
    if (node.kind === "terminal" && node.transitions.length > 0) {
      found.push(
        `${nodeLabel(node)} ends the campaign but still has ` +
        `${node.transitions.length} outgoing transition(s). Remove them, ` +
        "or change the node's kind back."
      );
    }
    if (node.kind !== "terminal" && node.transitions.length === 0) {
      found.push(
        `${nodeLabel(node)} needs at least one outgoing transition. ` +
        "Add one, or make the node an ending."
      );
    }
    if (node.kind !== "encounter" && (node.placements?.length ?? 0) > 0) {
      found.push(
        `${nodeLabel(node)} is not a Stage but still has character ` +
        "placements. Make it a Stage again, or remove the placements."
      );
    }
    // A capacity is a rule about who takes a board, so a node that fights
    // nothing cannot carry one. The kind change above removes the deployment
    // with the board; this catches one that arrived any other way.
    if (node.kind !== "encounter" && node.deployment !== undefined) {
      found.push(
        `${nodeLabel(node)} is not a Stage but still says how many may take ` +
        "the field. Make it a Stage again, or clear the number."
      );
    }
    const capacity = node.deployment?.capacity;
    if (capacity !== undefined &&
        (!Number.isInteger(capacity) || capacity < 1 || capacity > 4095)) {
      found.push(
        `${nodeLabel(node)} lets ${capacity} take the field, which is not a ` +
        "number of characters. Write a whole number from 1 to 4095, or clear " +
        "it for no cap."
      );
    }
    for (const grant of node.grants ?? []) {
      if (!props.items.some((item) => item.id === grant.itemId)) {
        found.push(
          `${nodeLabel(node)} grants '${grant.itemId}', which is not an item ` +
          "in this project. Choose another, or create the item."
        );
      }
      if (!Number.isInteger(grant.quantity) || grant.quantity < 1 ||
          grant.quantity > 65535) {
        found.push(
          `${nodeLabel(node)} grants ${grant.quantity} of ` +
          `'${grant.itemId}'. Say how many, as a whole number from 1 to 65535.`
        );
      }
    }
    const granted = new Set<string>();
    for (const grant of node.grants ?? []) {
      if (granted.has(grant.itemId)) {
        found.push(
          `${nodeLabel(node)} grants '${grant.itemId}' twice. Say how many ` +
          "once: two entries for one item are two answers to one question."
        );
      }
      granted.add(grant.itemId);
    }
    // What decides this Stage, checked against who is actually on it. A
    // targeted objective names a placement rather than a project record, so
    // nothing else in the editor or the analyzer follows it, and the content
    // compiler refuses the encounter outright, because the loader resolves the
    // target against the board's own placements and will not load one it
    // cannot find. The board and the conditions are authored on the same
    // screen, so the disagreement is caught on the same screen.
    //
    // A placement fielding a member of the company is named by the member: the
    // character is who the objective is about, and they are the same character
    // on every board that places them. That is the key the compiler resolves
    // and the key the runtime matches.
    const standing = new Set((node.placements ?? []).map(
      (placement) => placement.memberId ?? placement.id
    ));
    for (const objectiveId of node.objectiveIds ?? []) {
      const objective = props.objectives.find(
        (candidate) => candidate.id === objectiveId
      );
      if (!objective) continue;
      const kind = objective.kind;
      if (kind !== "defeatTarget" && kind !== "protectTarget") continue;
      const target = objective.targetPlacementId;
      if (target === undefined || target === "") {
        found.push(
          `${nodeLabel(node)} is decided by '${objective.name}', which names ` +
          "nobody to defeat or protect. Choose a character on this board."
        );
      } else if (!standing.has(target)) {
        found.push(
          `${nodeLabel(node)} is decided by '${objective.name}', which names ` +
          `'${target}': nobody who stands on this board. The Stage could ` +
          "never end. Choose somebody who is here, or put them back."
        );
      }
    }
    const transitionIds = new Set<string>();
    const priorities = new Set<number>();
    let fallbacks = 0;
    for (const transition of node.transitions) {
      if (transitionIds.has(transition.id)) {
        found.push(
          `${nodeLabel(node)} has two transitions named '${transition.id}'. ` +
          "Give each its own identifier."
        );
      }
      transitionIds.add(transition.id);
      if (!nodeIds.has(transition.targetNodeId)) {
        found.push(
          `A transition from ${nodeLabel(node)} points at ` +
          `'${transition.targetNodeId}', which does not exist.`
        );
      }
      if (priorities.has(transition.priority)) {
        found.push(
          `${nodeLabel(node)} has two branches with priority ` +
          `${transition.priority}. Give each branch its own priority.`
        );
      }
      priorities.add(transition.priority);
      if (transition.when === undefined) fallbacks += 1;
    }
    if (fallbacks > 1) {
      found.push(
        `${nodeLabel(node)} has ${fallbacks} branches without a condition. ` +
        "Only one unconditional fallback is allowed; add conditions to the rest."
      );
    }
    const map = props.maps.find((candidate) => candidate.id === node.mapId);
    const placementIds = new Set<string>();
    const tiles = new Map<string, string>();
    // Who is already standing on this board, so nobody stands twice. Boards
    // are checked one at a time: the same member fights on many of them, and
    // is the same character on all of them.
    const fielded = new Map<string, string>();
    for (const placement of node.placements ?? []) {
      if (placementIds.has(placement.id)) {
        found.push(
          `${nodeLabel(node)} has two placements named '${placement.id}'. ` +
          "Give each its own identifier."
        );
      }
      placementIds.add(placement.id);
      const tile = `${placement.x},${placement.y}`;
      const occupant = tiles.get(tile);
      if (occupant !== undefined) {
        found.push(
          `In ${nodeLabel(node)}, '${placement.id}' and '${occupant}' stand ` +
          `on the same tile (${tile}). Move one of them.`
        );
      } else {
        tiles.set(tile, placement.id);
      }
      if (map && (placement.x >= map.width || placement.y >= map.height)) {
        found.push(
          `In ${nodeLabel(node)}, '${placement.id}' stands outside map ` +
          `'${map.name}' (${map.width}×${map.height}). Move it onto the map.`
        );
      }
      // A board that is not a Stage is reported whole above; who stands on
      // it is only a question where a fight actually happens.
      if (node.kind !== "encounter") continue;
      if (placement.side === "second") {
        if (placement.memberId !== undefined) {
          found.push(
            `In ${nodeLabel(node)}, '${placement.id}' fights for the other ` +
            `side but names '${placement.memberId}' from your company. The ` +
            "opposing side never fields your people."
          );
        }
        continue;
      }
      if (placement.memberId === undefined) {
        found.push(
          `In ${nodeLabel(node)}, '${placement.id}' stands on your side but ` +
          "names nobody. Choose which member of the company stands there."
        );
        continue;
      }
      const member = members.get(placement.memberId);
      if (!member) {
        found.push(
          `In ${nodeLabel(node)}, '${placement.id}' fields ` +
          `'${placement.memberId}', who is nobody in this campaign. Choose a ` +
          "member of the company, or add them to it."
        );
        continue;
      }
      const twin = fielded.get(placement.memberId);
      if (twin !== undefined) {
        found.push(
          `In ${nodeLabel(node)}, '${twin}' and '${placement.id}' both field ` +
          `${memberLabel(member)}. One character cannot stand in two places ` +
          "at once."
        );
      } else {
        fielded.set(placement.memberId, placement.id);
      }
      if (member.unitTypeId !== placement.unitTypeId) {
        found.push(
          `In ${nodeLabel(node)}, '${placement.id}' fields ` +
          `${memberLabel(member)}, who is '${member.unitTypeId}', but stands ` +
          `as '${placement.unitTypeId}'. A member is the same character on ` +
          "every board."
        );
      }
    }
  }
  if (nodeIds.has(flow.entryNodeId)) {
    const reachable = new Set<string>();
    const pending = [flow.entryNodeId];
    while (pending.length > 0) {
      const nodeId = pending.pop()!;
      if (reachable.has(nodeId)) continue;
      reachable.add(nodeId);
      const node = flow.nodes.find((candidate) => candidate.id === nodeId);
      node?.transitions.forEach((transition) => {
        if (nodeIds.has(transition.targetNodeId)) {
          pending.push(transition.targetNodeId);
        }
      });
    }
    for (const node of flow.nodes) {
      if (!reachable.has(node.id)) {
        found.push(
          `${nodeLabel(node)} can never be reached from the entry node. ` +
          "Add a transition to it from a reachable node, or delete it."
        );
      }
    }
  }
  return found;
}

/**
 * A branch condition naming something the project no longer holds.
 *
 * The live road here is an imported archive, which the start screen invites,
 * whose objectives or items this project does not have. A select whose value
 * matches no option renders blank, so the branch reads as "unset" while the
 * condition it actually carries is still there and still refused downstream.
 * Named and disabled, the way the result menu twelve lines below already names
 * a value it does not recognise.
 */
function unknownObjective(id: string | undefined): string | undefined {
  if (!id) return undefined;
  return props.objectives.some((objective) => objective.id === id)
    ? undefined
    : id;
}

function unknownItem(id: string | undefined): string | undefined {
  if (!id) return undefined;
  return props.items.some((item) => item.id === id) ? undefined : id;
}

function removeTransition(index: number) {
  selectedNode.value?.transitions.splice(index, 1);
}

function enableCondition(transition: EditableTransition, enabled: boolean) {
  if (enabled) {
    transition.when = {
      kind: "worldFlagEquals",
      flagId: "branch_open",
      value: true
    };
  } else {
    delete transition.when;
  }
}

function changePredicate(
  transition: EditableTransition,
  kind: EditablePredicate["kind"]
) {
  if (kind === "objectiveResult") {
    // The content compiler accepts exactly "victory" and "defeat" as
    // objective results; anything else is refused at compile time.
    transition.when = {
      kind,
      objectiveId: props.objectives[0]?.id ?? "",
      result: "victory"
    };
  } else if (kind === "inventoryAtLeast") {
    transition.when = {
      kind,
      itemId: props.items[0]?.id ?? "",
      quantity: 1
    };
  } else {
    transition.when = { kind, flagId: "branch_open", value: true };
  }
}

function updateNodeDialogues(node: EditableNode, ids: string[]) {
  if (ids.length > 0) node.dialogueIds = ids;
  else delete node.dialogueIds;
}

function renameNode(node: EditableNode, nextId: string) {
  if (!draft.value || nextId === node.id || nextId === "") return;
  if (draft.value.nodes.some((candidate) => candidate.id === nextId)) return;
  const previousId = node.id;
  node.id = nextId;
  if (draft.value.entryNodeId === previousId) {
    draft.value.entryNodeId = nextId;
  }
  draft.value.nodes.forEach((candidate) => {
    candidate.transitions.forEach((transition) => {
      if (transition.targetNodeId === previousId) {
        transition.targetNodeId = nextId;
      }
    });
  });
  selectedNodeId.value = nextId;
}

function save(): boolean {
  if (!draft.value) return true;
  notice.value = "";
  problems.value = flowProblems(draft.value);
  if (problems.value.length > 0) return false;
  const flow = JSON.parse(JSON.stringify(draft.value)) as CampaignFlow;
  // The refreshed project will hand back this exact flow; remember it so the
  // watcher above does not treat the round-trip as an external change.
  loadedFlow = JSON.stringify(flow);
  emit("save", flow);
  return true;
}

/** Commits a pending draft; false when problems kept it out of the project. */
function flush(): boolean {
  // Everything being typed reaches the draft first: the identity above, and
  // whatever the two lists on a story stop are holding.
  keystrokes.flush();
  nodeRecruits.value?.flush();
  nodeGrants.value?.flush();
  if (!draft.value || JSON.stringify(draft.value) === loadedFlow) return true;
  return save();
}

/**
 * Opens one node of this flow, for an arrival from outside.
 *
 * The Stages section sends an author here to see where a Stage comes in the
 * campaign; without this it could only send them to the campaign and leave
 * them to find the node again. A node this flow does not have is ignored
 * rather than clearing the selection, because the caller is naming a node it
 * read out of the project and a mismatch is the project having moved on.
 *
 * Opening another node commits an identity being typed into this one: the field
 * it was typed in is about to be drawing a different node.
 */
function selectNode(id: string) {
  if (!draft.value?.nodes.some((node) => node.id === id)) return;
  keystrokes.flush();
  selectedNodeId.value = id;
}

defineExpose({ flush, selectNode });
</script>

<template>
  <section class="campaign-flow" aria-labelledby="campaign-flow-title">
    <h3 id="campaign-flow-title">The order things happen in</h3>
    <p>
      Each stop is a Stage, a scene, or an ending. Conditions on an objective's
      result run in the game; conditions on inventory and world flags are
      refused when it is built.
    </p>
    <button v-if="!draft" type="button" @click="createFlow">
      Give this campaign an order of events
    </button>

    <template v-else>
      <div class="campaign-flow-layout">
        <div>
          <label for="campaign-entry">Entry node</label>
          <select id="campaign-entry" v-model="draft.entryNodeId">
            <option v-for="node in draft.nodes" :key="node.id" :value="node.id">
              {{ node.name }} ({{ node.id }})
            </option>
          </select>
          <button type="button" @click="addNode">Add node</button>
          <ul class="campaign-node-list">
            <li v-for="node in draft.nodes" :key="node.id">
              <button type="button"
                :aria-current="selectedNodeId === node.id ? 'true' : undefined"
                @click="selectNode(node.id)">
                {{ node.name }} <small>{{ nodeKindWord(node.kind) }} · {{ node.id }}</small>
              </button>
            </li>
          </ul>
        </div>

        <div v-if="selectedNode" class="campaign-node-editor">
          <!-- Held while it is being typed and applied once, because a rename
               rewrites every branch that leads here. `input` is what says the
               words are not in the project yet. -->
          <label for="campaign-node-id">Node identifier</label>
          <input id="campaign-node-id"
            :value="keystrokes.shown('node-id', selectedNode.id)"
            pattern="^[a-z][a-z0-9]*(?:[._\-][a-z0-9]+)*$"
            @input="keystrokes.type(
              'node-id',
              ($event.target as HTMLInputElement).value,
              (typed) => renameNode(selectedNode!, typed.trim())
            )"
            @change="keystrokes.leave(
              'node-id',
              ($event.target as HTMLInputElement).value,
              (typed) => renameNode(selectedNode!, typed.trim())
            )">
          <label for="campaign-node-name">Node name</label>
          <input id="campaign-node-name" v-model.trim="selectedNode.name">
          <label for="campaign-node-kind">What happens here</label>
          <select id="campaign-node-kind" :value="selectedNode.kind"
            @change="changeKind(
              selectedNode,
              ($event.target as HTMLSelectElement).value as EditableNode['kind']
            )">
            <option value="encounter">Stage: a fight happens here</option>
            <option value="story">Story: scenes play, then move on</option>
            <option value="terminal">Ending: the campaign ends here</option>
          </select>
          <!-- `input` as well as `change`: the draft above is what this
               control draws and what a Save flushes, so a note that only
               reached it when the field was left was a note the header called
               saved and a Save could not find. Both events are read, rather
               than trusting that an `input` was seen first: a control that
               works only when the event it expects came before it is the same
               fragility one layer down. -->
          <label for="campaign-node-notes">Notes on this node</label>
          <textarea id="campaign-node-notes" :value="selectedNode.notes ?? ''"
            @input="updateNotes(
              selectedNode, ($event.target as HTMLTextAreaElement).value
            )"
            @change="updateNotes(
              selectedNode, ($event.target as HTMLTextAreaElement).value
            )"></textarea>
          <p class="field-help">{{ notesHelp }}</p>

          <!-- A Stage's contents are authored under Stages and nowhere else.
               This is a door to that one place, not a second copy of it: the
               ground, the board, what winning means, what is said, who joins
               and what they are given all live on the Stage, and a form here
               that wrote any of them would be a second place they could
               disagree. What Flow decides about a Stage is where it comes and
               what it leads to, and those are on this page. -->
          <p v-if="selectedNode.kind === 'encounter'" class="flow-signpost">
            {{ stageGround(selectedNode) }}
            <button type="button" class="secondary"
              @click="emit('openStage', selectedNode.id)">
              Set this Stage up
            </button>
          </p>

          <template v-else>
            <CutsceneEditor
              :dialogue-ids="selectedNode.dialogueIds ?? []"
              :dialogues="dialogues"
              :project="project"
              @update-ids="updateNodeDialogues(selectedNode, $event)"
              @update-dialogues="emit('updateDialogues', $event)" />

            <RosterMemberEditor
              ref="nodeRecruits"
              id-prefix="recruit"
              heading="Who joins the company here"
              :help="recruitHelp"
              member-word="recruit"
              :members="selectedNode.recruits ?? []"
              :unit-types="unitTypes"
              :other-ids="idsHeldElsewhere(selectedNode)"
              @update="updateRecruits(selectedNode, $event)"
              @dirty="emit('dirty')"
              @create-unit-type="emit('createUnitType')" />

            <ItemGrantEditor
              ref="nodeGrants"
              id-prefix="node-grant"
              heading="What the company is given here"
              :help="grantHelp"
              grant-word="grant"
              :grants="selectedNode.grants ?? []"
              :items="items"
              @update="updateGrants(selectedNode, $event)"
              @dirty="emit('dirty')"
              @create-item="emit('createItem')" />
          </template>

          <h4>Where it goes next</h4>
          <button type="button" @click="addTransition">Add transition</button>
          <!-- Keyed by position, never by the identifier being typed into the
               fieldset: a key that changes per keystroke remounts the input
               and throws the author's focus away. -->
          <fieldset v-for="(transition, index) in selectedNode.transitions"
            :key="index">
            <legend>Transition {{ index + 1 }}</legend>
            <label :for="`transition-${index}-id`">Identifier</label>
            <input :id="`transition-${index}-id`" v-model.trim="transition.id">
            <label :for="`transition-${index}-target`">Target node</label>
            <select :id="`transition-${index}-target`"
              v-model="transition.targetNodeId">
              <option v-for="node in draft.nodes" :key="node.id" :value="node.id">
                {{ node.name }} ({{ node.id }})
              </option>
            </select>
            <label :for="`transition-${index}-priority`">Priority</label>
            <input :id="`transition-${index}-priority`"
              v-model.number="transition.priority" type="number" min="0" max="65535">
            <p class="field-help">
              Branches with lower numbers are checked first.
            </p>
            <label>
              <input type="checkbox" :checked="transition.when !== undefined"
                @change="enableCondition(
                  transition,
                  ($event.target as HTMLInputElement).checked
                )">
              Conditional branch
            </label>
            <template v-if="transition.when">
              <label :for="`transition-${index}-condition`">Condition</label>
              <select :id="`transition-${index}-condition`"
                :value="transition.when.kind"
                @change="changePredicate(
                  transition,
                  ($event.target as HTMLSelectElement).value as EditablePredicate['kind']
                )">
                <option value="objectiveResult">Objective result</option>
                <option value="inventoryAtLeast">Inventory quantity</option>
                <option value="worldFlagEquals">World flag</option>
              </select>
              <template v-if="transition.when.kind === 'objectiveResult'">
                <label :for="`transition-${index}-objective`">Objective</label>
                <select :id="`transition-${index}-objective`"
                  v-model="transition.when.objectiveId">
                  <option v-if="unknownObjective(transition.when.objectiveId)"
                    :value="transition.when.objectiveId" disabled>
                    {{ transition.when.objectiveId }}: not an objective in
                    this project
                  </option>
                  <option v-for="objective in objectives" :key="objective.id"
                    :value="objective.id">
                    {{ objective.name }} ({{ objective.id }})
                  </option>
                </select>
                <p v-if="unknownObjective(transition.when.objectiveId)"
                  class="field-error" role="alert">
                  This branch is decided by an objective this project does not
                  have. Choose one it does, or create it.
                </p>
                <label :for="`transition-${index}-result`">
                  Take this branch when the objective ends in
                </label>
                <select :id="`transition-${index}-result`"
                  v-model="transition.when.result">
                  <option
                    v-if="transition.when.result !== 'victory' &&
                      transition.when.result !== 'defeat'"
                    :value="transition.when.result" disabled>
                    {{ transition.when.result }}: not recognised; choose
                    victory or defeat
                  </option>
                  <option value="victory">Victory: the objective was won</option>
                  <option value="defeat">Defeat: the objective was lost</option>
                </select>
              </template>
              <template v-else-if="transition.when.kind === 'inventoryAtLeast'">
                <label :for="`transition-${index}-item`">Required item</label>
                <select :id="`transition-${index}-item`"
                  v-model="transition.when.itemId">
                  <option v-if="unknownItem(transition.when.itemId)"
                    :value="transition.when.itemId" disabled>
                    {{ transition.when.itemId }}: not an item in this project
                  </option>
                  <option v-for="item in items" :key="item.id" :value="item.id">
                    {{ item.name }} ({{ item.id }})
                  </option>
                </select>
                <p v-if="unknownItem(transition.when.itemId)"
                  class="field-error" role="alert">
                  This branch is decided by an item this project does not have.
                  Choose one it does, or create it.
                </p>
                <label :for="`transition-${index}-quantity`">Quantity</label>
                <input :id="`transition-${index}-quantity`"
                  v-model.number="transition.when.quantity" type="number" min="1">
              </template>
              <template v-else>
                <label :for="`transition-${index}-flag`">World flag identity</label>
                <input :id="`transition-${index}-flag`"
                  v-model.trim="transition.when.flagId">
                <label :for="`transition-${index}-flag-value`">Expected value</label>
                <input :id="`transition-${index}-flag-value`"
                  :value="String(transition.when.value ?? '')"
                  @input="transition.when.value =
                    ($event.target as HTMLInputElement).value">
              </template>
            </template>
            <label :for="`transition-${index}-notes`">
              Notes on this branch
            </label>
            <textarea :id="`transition-${index}-notes`"
              :value="transition.notes ?? ''"
              @input="updateNotes(
                transition, ($event.target as HTMLTextAreaElement).value
              )"
              @change="updateNotes(
                transition, ($event.target as HTMLTextAreaElement).value
              )"></textarea>
            <button type="button" class="danger" @click="removeTransition(index)">
              Remove transition
            </button>
          </fieldset>
        </div>
      </div>
      <p v-if="notice" class="field-help" role="status">{{ notice }}</p>
      <div v-if="problems.length" class="flow-problems" role="alert">
        <p>This campaign cannot be saved yet:</p>
        <ul>
          <li v-for="problem in problems" :key="problem">{{ problem }}</li>
        </ul>
      </div>
      <button type="button" @click="save">Save the order of events</button>
    </template>
  </section>
</template>

<style scoped>
.flow-problems {
  margin: 0.5rem 0;
  padding: 0.5rem 0.75rem;
  border: 1px solid #a02c2c;
  border-radius: 0.5rem;
  color: #8a2020;
}
.flow-problems p {
  margin: 0;
  font-weight: 700;
}
/* The door to the one place a Stage is set up. It reads as a sentence with a
   way out of it, not as a control: nothing on this page edits a Stage. */
.flow-signpost {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  align-items: center;
  margin: 0.75rem 0;
  padding: 0.5rem 0.75rem;
  border-radius: 0.5rem;
  background: #f4f7fd;
  color: #38445c;
  font-size: 0.875rem;
}
</style>
