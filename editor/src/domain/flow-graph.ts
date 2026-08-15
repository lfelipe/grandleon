// SPDX-License-Identifier: MIT
/**
 * A campaign's flow as a picture you can see and join up.
 *
 * A flow is a graph of stops and the ways out of them, but the format stores
 * it as a list of nodes each naming the identifiers it leads to. Reading that
 * list tells an author what their game is made of and nothing at all about its
 * shape. This module turns the list into the shape: where every stop sits, and
 * the line from each way out to the stop it reaches.
 *
 * **Nowhere in here is a position stored, and that is the design.** The
 * campaign schema is closed and carries no coordinates, and the one open slot
 * in it, `extensions`, is refused outright by the content compiler, which
 * walks the whole document and rejects any non-empty extension as data the
 * native package cannot represent. So a saved position would either not
 * compile or would put a drawing convenience inside the file the game is built
 * from, on the wrong side of the line between presentation and gameplay.
 *
 * The alternative is better than the thing it replaces. A layout derived from
 * the flow's own shape is one an author never maintains, never has to tidy,
 * and never sees in a diff; two people who author the same road see the same
 * picture; and a stop that moves does so because the road changed, which is
 * the only reason it should ever move. What an author drags here is a **way
 * out**, and dragging it changes where the road goes, which is the thing they
 * were trying to say. Dragging a box would only have changed where the box was.
 *
 * Ranks come from the entry node outwards, so distance from the left is
 * distance from the start of the campaign. Anything the entry node cannot
 * reach is laid out past the end and marked stranded: it is a real problem
 * with the flow, and a picture that hid it by drawing it like everything else
 * would be a picture that lied.
 */

import type { CampaignFlow, CampaignNode } from "../generated/source-v1";

/** How big one stop is drawn. The graph's geometry is all derived from these. */
export const FLOW_NODE_WIDTH = 208;
export const FLOW_NODE_HEIGHT = 124;
/** The room between one rank and the next, which is where the lines run. */
export const FLOW_COLUMN_GAP = 92;
export const FLOW_ROW_GAP = 28;
/** Room above the top rank, so a line that loops back has somewhere to arc. */
export const FLOW_TOP_MARGIN = 44;

const COLUMN_PITCH = FLOW_NODE_WIDTH + FLOW_COLUMN_GAP;
const ROW_PITCH = FLOW_NODE_HEIGHT + FLOW_ROW_GAP;

/** One stop, and where it is drawn. */
export interface FlowNodeBox {
  readonly nodeId: string;
  /** How many stops from the entry node, which is how far right it sits. */
  readonly rank: number;
  /** Its place within that rank, which is how far down it sits. */
  readonly row: number;
  readonly x: number;
  readonly y: number;
  /**
   * Nothing the campaign can reach leads here. The flow editor reports this as
   * a problem; the graph draws it as one.
   */
  readonly stranded: boolean;
}

/** One way out, drawn from the stop that owns it to the stop it reaches. */
export interface FlowEdgeLine {
  readonly fromNodeId: string;
  readonly toNodeId: string;
  readonly transitionId: string;
  /** Where the line leaves, which is where the draggable handle sits. */
  readonly x1: number;
  readonly y1: number;
  /** Where it arrives. */
  readonly x2: number;
  readonly y2: number;
  /** An SVG path, curved so that two lines between the same ranks are told apart. */
  readonly path: string;
  /** It leads back the way the campaign came, or to the stop it left. */
  readonly backwards: boolean;
}

export interface FlowLayout {
  readonly boxes: readonly FlowNodeBox[];
  readonly edges: readonly FlowEdgeLine[];
  readonly width: number;
  readonly height: number;
  /** The stops nothing leads to, in the order the flow lists them. */
  readonly stranded: readonly string[];
}

/**
 * How far from the entry node every stop is.
 *
 * Breadth-first, so a stop sits at its shortest distance from the start and a
 * road that loops draws its loop as a line pointing back rather than as an
 * ever-growing column. Anything the walk never reaches is ranked after
 * everything it did, keeping its own internal shape: a stranded chain of three
 * still reads as a chain of three.
 */
function rankNodes(flow: CampaignFlow): Map<string, number> {
  const byId = new Map(flow.nodes.map((node) => [node.id, node]));
  const ranks = new Map<string, number>();

  function walk(from: string, base: number) {
    if (ranks.has(from) || !byId.has(from)) return;
    ranks.set(from, base);
    const pending = [from];
    while (pending.length > 0) {
      const id = pending.shift()!;
      const rank = ranks.get(id)!;
      for (const transition of byId.get(id)!.transitions) {
        const target = transition.targetNodeId;
        if (!byId.has(target) || ranks.has(target)) continue;
        ranks.set(target, rank + 1);
        pending.push(target);
      }
    }
  }

  walk(flow.entryNodeId, 0);
  const reached = ranks.size;
  let base = reached === 0 ? 0 : Math.max(...ranks.values()) + 1;
  // Stranded stops, in the order the author wrote them, each seeding a walk of
  // its own so a stranded chain keeps its shape instead of stacking in one
  // column.
  for (const node of flow.nodes) {
    if (ranks.has(node.id)) continue;
    const before = ranks.size;
    walk(node.id, base);
    if (ranks.size > before) {
      base = Math.max(...ranks.values()) + 1;
    }
  }
  return ranks;
}

/** Where a line leaves a box: the right-hand edge, on its middle line. */
function exitPoint(box: FlowNodeBox): { x: number; y: number } {
  return { x: box.x + FLOW_NODE_WIDTH, y: box.y + FLOW_NODE_HEIGHT / 2 };
}

/** Where a line arrives: the left-hand edge, on its middle line. */
function entryPoint(box: FlowNodeBox): { x: number; y: number } {
  return { x: box.x, y: box.y + FLOW_NODE_HEIGHT / 2 };
}

/**
 * The curve between two stops.
 *
 * Forward lines bow horizontally, which keeps two ways out of the same stop
 * apart where they would otherwise overlap. A line that goes back the way the
 * campaign came, including one leading to the stop it left, arcs over the
 * top instead, because drawn straight it would run through every box between
 * the two ends and read as a line to each of them.
 */
function curve(
  from: { x: number; y: number },
  to: { x: number; y: number },
  backwards: boolean
): string {
  if (!backwards) {
    const reach = Math.max(36, Math.abs(to.x - from.x) / 2);
    return `M ${from.x} ${from.y} C ${from.x + reach} ${from.y}, ` +
      `${to.x - reach} ${to.y}, ${to.x} ${to.y}`;
  }
  const peak = Math.min(from.y, to.y) - FLOW_TOP_MARGIN;
  return `M ${from.x} ${from.y} C ${from.x + 56} ${peak}, ` +
    `${to.x - 56} ${peak}, ${to.x} ${to.y}`;
}

/**
 * The whole picture: every stop placed, and every way out drawn to where it
 * goes. A way out naming a stop the flow has not got is left undrawn. The
 * flow editor names that as a problem in words, and half a line to nowhere
 * would be a second, worse way of saying it.
 */
export function layOutFlow(flow: CampaignFlow | undefined): FlowLayout {
  if (!flow || flow.nodes.length === 0) {
    return { boxes: [], edges: [], width: 0, height: 0, stranded: [] };
  }
  const ranks = rankNodes(flow);
  const reachable = new Set<string>();
  {
    const byId = new Map(flow.nodes.map((node) => [node.id, node]));
    const pending = [flow.entryNodeId];
    while (pending.length > 0) {
      const id = pending.shift()!;
      if (reachable.has(id) || !byId.has(id)) continue;
      reachable.add(id);
      for (const transition of byId.get(id)!.transitions) {
        pending.push(transition.targetNodeId);
      }
    }
  }

  const rowsUsed = new Map<number, number>();
  const boxes: FlowNodeBox[] = [];
  for (const node of flow.nodes) {
    const rank = ranks.get(node.id) ?? 0;
    const row = rowsUsed.get(rank) ?? 0;
    rowsUsed.set(rank, row + 1);
    boxes.push({
      nodeId: node.id,
      rank,
      row,
      x: rank * COLUMN_PITCH,
      y: FLOW_TOP_MARGIN + row * ROW_PITCH,
      stranded: !reachable.has(node.id)
    });
  }

  const boxById = new Map(boxes.map((box) => [box.nodeId, box]));
  const edges: FlowEdgeLine[] = [];
  for (const node of flow.nodes) {
    const from = boxById.get(node.id)!;
    for (const transition of node.transitions) {
      const to = boxById.get(transition.targetNodeId);
      if (!to) continue;
      const start = exitPoint(from);
      const end = entryPoint(to);
      const backwards = to.rank <= from.rank;
      edges.push({
        fromNodeId: node.id,
        toNodeId: to.nodeId,
        transitionId: transition.id,
        x1: start.x,
        y1: start.y,
        x2: end.x,
        y2: end.y,
        path: curve(start, end, backwards),
        backwards
      });
    }
  }

  return {
    boxes,
    edges,
    width: Math.max(...boxes.map((box) => box.x)) + FLOW_NODE_WIDTH,
    height: Math.max(...boxes.map((box) => box.y)) + FLOW_NODE_HEIGHT +
      FLOW_ROW_GAP,
    stranded: boxes.filter((box) => box.stranded).map((box) => box.nodeId)
  };
}

/**
 * What the author did to the picture, said as the act rather than the result.
 *
 * The graph reports the gesture and the surface holding the project applies it.
 * That is not indirection for its own sake: the same road is also edited by a
 * list-and-form under the picture, and a graph that computed a whole new flow
 * from the copy it was handed would overwrite anything typed there since. A
 * gesture is applied to whatever the project holds at the moment it lands, so
 * there is one truth and the picture never has to be the freshest copy of it.
 */
export type FlowGesture =
  | {
    readonly kind: "retarget";
    readonly nodeId: string;
    readonly transitionId: string;
    readonly targetNodeId: string;
  }
  | { readonly kind: "addWayOut"; readonly nodeId: string }
  | {
    readonly kind: "removeWayOut";
    readonly nodeId: string;
    readonly transitionId: string;
  };

/** Why a change to the graph cannot be made, in words an author reads. */
export interface FlowGraphRefusal {
  readonly kind: "refused";
  readonly reason: string;
}

/** A flow to store, and the sentence describing what changed. */
export interface FlowGraphChange {
  readonly kind: "changed";
  readonly flow: CampaignFlow;
  /** What this act was, for the undo entry and for the status line. */
  readonly summary: string;
  /** The way out this act made or moved, so the surface can keep it in view. */
  readonly transitionId: string;
}

export type FlowGraphResult = FlowGraphChange | FlowGraphRefusal;

function nodeNamed(flow: CampaignFlow, id: string): CampaignNode | undefined {
  return flow.nodes.find((node) => node.id === id);
}

/** An identifier not already taken, with a numeric suffix when it is. */
function freeId(prefix: string, taken: readonly string[]): string {
  if (!taken.includes(prefix)) return prefix;
  let suffix = 2;
  while (taken.includes(`${prefix}_${suffix}`)) suffix += 1;
  return `${prefix}_${suffix}`;
}

/**
 * Points one way out at a different stop.
 *
 * This is the whole gesture the graph exists for: a stop shows the ways out of
 * it, and putting one of them on another stop is how an author says where the
 * road goes. It is a single change to a single transition, so it is a single
 * thing to undo.
 *
 * A way out that already leads there is not an error and not a write: the
 * author said something that was already true, and storing it would spend an
 * undo entry on nothing.
 */
export function retargetWayOut(
  flow: CampaignFlow,
  fromNodeId: string,
  transitionId: string,
  targetNodeId: string
): FlowGraphResult {
  const from = nodeNamed(flow, fromNodeId);
  const target = nodeNamed(flow, targetNodeId);
  if (!from) {
    return {
      kind: "refused",
      reason: `'${fromNodeId}' is no longer in this campaign.`
    };
  }
  if (!target) {
    return {
      kind: "refused",
      reason: `'${targetNodeId}' is no longer in this campaign.`
    };
  }
  const transition = from.transitions.find(
    (candidate) => candidate.id === transitionId
  );
  if (!transition) {
    return {
      kind: "refused",
      reason: `'${from.name}' no longer has that way out.`
    };
  }
  if (transition.targetNodeId === targetNodeId) {
    return {
      kind: "refused",
      reason: `'${from.name}' already leads to '${target.name}'.`
    };
  }
  const next = structuredClone(flow) as CampaignFlow;
  const changed = nodeNamed(next, fromNodeId)!;
  changed.transitions = changed.transitions.map((candidate) =>
    candidate.id === transitionId
      ? { ...candidate, targetNodeId }
      : candidate
  );
  return {
    kind: "changed",
    flow: next,
    transitionId,
    summary: `Send ${from.name} on to ${target.name}`
  };
}

/**
 * Gives a stop another way out.
 *
 * A way out has to lead somewhere the moment it exists, the format having no
 * transition to nowhere, so this one leads to the first other stop there is,
 * and the author moves it by dragging it. Where there is no other stop, an
 * ending is made for it to reach: a campaign whose only stop is the one being
 * given an exit has nowhere to go, and offering the author a refusal instead
 * of an ending would be refusing to do the obvious thing.
 *
 * A stop that ends the campaign cannot have one. `terminal` is the format's
 * word for "this is where it stops", and a stop that leads on is not that any
 * more, so the kind changes with the exit rather than the save being refused
 * for a contradiction the author never typed.
 */
export function addWayOut(
  flow: CampaignFlow,
  fromNodeId: string
): FlowGraphResult {
  const from = nodeNamed(flow, fromNodeId);
  if (!from) {
    return {
      kind: "refused",
      reason: `'${fromNodeId}' is no longer in this campaign.`
    };
  }
  const next = structuredClone(flow) as CampaignFlow;
  const changed = nodeNamed(next, fromNodeId)!;
  let target = next.nodes.find((node) => node.id !== fromNodeId);
  let made = "";
  if (!target) {
    const ending: CampaignNode = {
      id: freeId("ending", next.nodes.map((node) => node.id)),
      name: "Ending",
      kind: "terminal",
      transitions: []
    };
    next.nodes = [next.nodes[0], ...next.nodes.slice(1), ending];
    target = ending;
    made = ", and an ending for it to reach";
  }
  // The kind goes with the exit. Nothing else about the stop changes: a Stage
  // being given a second way out is still a Stage.
  if (changed.kind === "terminal") changed.kind = "story";
  const transitionId = freeId(
    "next",
    changed.transitions.map((transition) => transition.id)
  );
  const priority = changed.transitions.reduce(
    (highest, transition) => Math.max(highest, transition.priority + 1),
    0
  );
  changed.transitions = [
    ...changed.transitions,
    { id: transitionId, targetNodeId: target.id, priority }
  ];
  return {
    kind: "changed",
    flow: next,
    transitionId,
    summary: `Give ${from.name} another way out${made}`
  };
}

/**
 * Takes a way out away.
 *
 * The stop it led to may be left with nothing reaching it, and that is not
 * this function's business to prevent: it is what the author asked for, the
 * graph draws the result as stranded, and the flow editor says so in words.
 * A stop with no way out at all is an ending, so the kind follows the last
 * exit off the same way it followed the first one on.
 */
export function removeWayOut(
  flow: CampaignFlow,
  fromNodeId: string,
  transitionId: string
): FlowGraphResult {
  const from = nodeNamed(flow, fromNodeId);
  const transition = from?.transitions.find(
    (candidate) => candidate.id === transitionId
  );
  if (!from || !transition) {
    return {
      kind: "refused",
      reason: "That way out is no longer in this campaign."
    };
  }
  const next = structuredClone(flow) as CampaignFlow;
  const changed = nodeNamed(next, fromNodeId)!;
  changed.transitions = changed.transitions.filter(
    (candidate) => candidate.id !== transitionId
  );
  if (changed.transitions.length === 0) changed.kind = "terminal";
  const target = nodeNamed(flow, transition.targetNodeId);
  return {
    kind: "changed",
    flow: next,
    transitionId,
    summary: target
      ? `Stop ${from.name} leading to ${target.name}`
      : `Take a way out off ${from.name}`
  };
}

/**
 * One gesture on the picture, applied to the road as it stands right now.
 *
 * The single door between what the author did and what is stored, so the
 * surface holding the project never has to know which of the three acts it is
 * committing, only that it is one act, and that it either changed the road or
 * has a sentence saying why not.
 */
export function applyFlowGesture(
  flow: CampaignFlow | undefined,
  gesture: FlowGesture
): FlowGraphResult {
  if (!flow) {
    return {
      kind: "refused",
      reason: "This campaign has no order of events to change."
    };
  }
  switch (gesture.kind) {
    case "retarget":
      return retargetWayOut(
        flow, gesture.nodeId, gesture.transitionId, gesture.targetNodeId
      );
    case "addWayOut":
      return addWayOut(flow, gesture.nodeId);
    case "removeWayOut":
      return removeWayOut(flow, gesture.nodeId, gesture.transitionId);
  }
}
