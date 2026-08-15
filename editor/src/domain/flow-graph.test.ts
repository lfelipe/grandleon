// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { CampaignFlow, CampaignNode } from "../generated/source-v1";
import {
  addWayOut,
  FLOW_NODE_HEIGHT,
  FLOW_NODE_WIDTH,
  layOutFlow,
  removeWayOut,
  retargetWayOut
} from "./flow-graph";

function node(
  id: string,
  kind: CampaignNode["kind"],
  leadsTo: readonly string[] = []
): CampaignNode {
  return {
    id,
    name: id.replace(/_/g, " "),
    kind,
    transitions: leadsTo.map((targetNodeId, index) => ({
      id: `to_${targetNodeId}`,
      targetNodeId,
      priority: index
    }))
  };
}

function flowOf(entryNodeId: string, ...nodes: CampaignNode[]): CampaignFlow {
  return {
    contractVersion: "1.0.0",
    entryNodeId,
    nodes: [nodes[0]!, ...nodes.slice(1)]
  };
}

/** The straight road every test below varies: a Stage, then an ending. */
function straightRoad(): CampaignFlow {
  return flowOf(
    "first",
    node("first", "encounter", ["last"]),
    node("last", "terminal")
  );
}

function boxOf(flow: CampaignFlow, nodeId: string) {
  return layOutFlow(flow).boxes.find((box) => box.nodeId === nodeId)!;
}

describe("laying a flow out", () => {
  it("puts distance from the start on the horizontal", () => {
    const flow = flowOf(
      "first",
      node("first", "encounter", ["middle"]),
      node("middle", "story", ["last"]),
      node("last", "terminal")
    );
    const layout = layOutFlow(flow);
    expect(layout.boxes.map((box) => box.rank)).toEqual([0, 1, 2]);
    // Every rank is one column further right, and the columns do not overlap.
    const [first, middle, last] = layout.boxes;
    expect(middle!.x).toBeGreaterThan(first!.x + FLOW_NODE_WIDTH);
    expect(last!.x).toBeGreaterThan(middle!.x + FLOW_NODE_WIDTH);
    expect(layout.width).toBe(last!.x + FLOW_NODE_WIDTH);
  });

  it("stacks a branch's two destinations without overlapping them", () => {
    const flow = flowOf(
      "fork",
      node("fork", "encounter", ["left", "right"]),
      node("left", "terminal"),
      node("right", "terminal")
    );
    const layout = layOutFlow(flow);
    const left = boxOf(flow, "left");
    const right = boxOf(flow, "right");
    expect(left.rank).toBe(right.rank);
    expect(left.row).toBe(0);
    expect(right.row).toBe(1);
    expect(right.y).toBeGreaterThan(left.y + FLOW_NODE_HEIGHT);
    // Two ways out, two lines, each naming which transition drew it.
    expect(layout.edges.map((edge) => edge.transitionId))
      .toEqual(["to_left", "to_right"]);
  });

  it("draws the same picture for the same flow, every time", () => {
    const flow = flowOf(
      "fork",
      node("fork", "encounter", ["left", "right"]),
      node("left", "story", ["join"]),
      node("right", "story", ["join"]),
      node("join", "terminal")
    );
    expect(layOutFlow(flow)).toEqual(layOutFlow(structuredClone(flow)));
    // Branches that recombine put the meeting point past both of them.
    expect(boxOf(flow, "join").rank).toBe(2);
  });

  it("marks a stop nothing reaches, and lays it out past the road", () => {
    const flow = flowOf(
      "first",
      node("first", "encounter", ["last"]),
      node("last", "terminal"),
      node("orphan", "story", ["orphan_end"]),
      node("orphan_end", "terminal")
    );
    const layout = layOutFlow(flow);
    expect(layout.stranded).toEqual(["orphan", "orphan_end"]);
    expect(boxOf(flow, "first").stranded).toBe(false);
    // Past the end of the reachable road, and keeping its own shape: the
    // stranded pair is still a chain of two rather than a stack of two.
    expect(boxOf(flow, "orphan").rank).toBe(2);
    expect(boxOf(flow, "orphan_end").rank).toBe(3);
  });

  it("survives a road that loops, and arcs the line that goes back", () => {
    const flow = flowOf(
      "first",
      node("first", "encounter", ["second"]),
      node("second", "encounter", ["first"])
    );
    const layout = layOutFlow(flow);
    expect(layout.stranded).toEqual([]);
    expect(layout.boxes.map((box) => box.rank)).toEqual([0, 1]);
    const back = layout.edges.find((edge) => edge.toNodeId === "first")!;
    expect(back.backwards).toBe(true);
    // The arc rises above the boxes rather than cutting through them.
    expect(back.path).toMatch(/^M .* C .*/);
    const forward = layout.edges.find((edge) => edge.toNodeId === "second")!;
    expect(forward.backwards).toBe(false);
  });

  it("leaves a way out pointing at nothing undrawn", () => {
    const flow = flowOf(
      "first",
      node("first", "encounter", ["gone"]),
      node("last", "terminal")
    );
    expect(layOutFlow(flow).edges).toEqual([]);
    // The stop is still placed; only the line nobody could draw is missing.
    expect(layOutFlow(flow).boxes).toHaveLength(2);
  });

  it("has nothing to draw for a campaign with no order of events", () => {
    expect(layOutFlow(undefined)).toEqual({
      boxes: [], edges: [], width: 0, height: 0, stranded: []
    });
  });
});

describe("joining stops up", () => {
  it("points a way out at another stop and says what it did", () => {
    const flow = flowOf(
      "first",
      node("first", "encounter", ["last"]),
      node("second", "encounter", ["last"]),
      node("last", "terminal")
    );
    const result = retargetWayOut(flow, "first", "to_last", "second");
    expect(result.kind).toBe("changed");
    if (result.kind !== "changed") return;
    expect(result.flow.nodes[0]!.transitions[0]!.targetNodeId).toBe("second");
    expect(result.summary).toBe("Send first on to second");
    // The flow handed in is untouched: the caller decides whether to store it.
    expect(flow.nodes[0]!.transitions[0]!.targetNodeId).toBe("last");
    // Everything else about the way out survives the move.
    expect(result.flow.nodes[0]!.transitions[0]!.priority).toBe(0);
    expect(result.flow.nodes[0]!.transitions[0]!.id).toBe("to_last");
  });

  it("keeps a condition on a branch that is pointed somewhere else", () => {
    const flow = straightRoad();
    flow.nodes[0]!.transitions[0]!.when = {
      kind: "worldFlagEquals",
      flagId: "branch_open",
      value: true
    };
    flow.nodes = [
      flow.nodes[0]!, flow.nodes[1]!, node("other", "terminal")
    ];
    const result = retargetWayOut(flow, "first", "to_last", "other");
    expect(result.kind).toBe("changed");
    if (result.kind !== "changed") return;
    expect(result.flow.nodes[0]!.transitions[0]!.when).toEqual({
      kind: "worldFlagEquals", flagId: "branch_open", value: true
    });
  });

  it("writes nothing when the road already goes there", () => {
    const result = retargetWayOut(straightRoad(), "first", "to_last", "last");
    expect(result.kind).toBe("refused");
    if (result.kind !== "refused") return;
    expect(result.reason).toContain("already leads to");
  });

  it("refuses a stop or a way out that is gone, by name", () => {
    const flow = straightRoad();
    expect(retargetWayOut(flow, "nobody", "to_last", "last")).toMatchObject({
      kind: "refused", reason: expect.stringContaining("'nobody'")
    });
    expect(retargetWayOut(flow, "first", "to_last", "nowhere")).toMatchObject({
      kind: "refused", reason: expect.stringContaining("'nowhere'")
    });
    expect(retargetWayOut(flow, "first", "no_such", "last")).toMatchObject({
      kind: "refused", reason: expect.stringContaining("no longer has")
    });
  });
});

describe("giving a stop another way out", () => {
  it("adds one leading to a stop that already exists", () => {
    const flow = flowOf(
      "first",
      node("first", "encounter", ["last"]),
      node("last", "terminal")
    );
    const result = addWayOut(flow, "first");
    expect(result.kind).toBe("changed");
    if (result.kind !== "changed") return;
    const ways = result.flow.nodes[0]!.transitions;
    expect(ways).toHaveLength(2);
    // Distinct identifiers and distinct priorities: the format refuses either
    // being shared, and the flow editor reports both.
    expect(new Set(ways.map((way) => way.id)).size).toBe(2);
    expect(new Set(ways.map((way) => way.priority)).size).toBe(2);
    expect(ways[1]!.priority).toBe(1);
  });

  it("stops being an ending when it is given somewhere to go", () => {
    const flow = flowOf(
      "first",
      node("first", "encounter", ["last"]),
      node("last", "terminal")
    );
    const result = addWayOut(flow, "last");
    expect(result.kind).toBe("changed");
    if (result.kind !== "changed") return;
    // The format refuses transitions on a terminal, so the kind follows the
    // exit rather than the save being refused for it.
    expect(result.flow.nodes[1]!.kind).toBe("story");
    expect(result.flow.nodes[1]!.transitions[0]!.targetNodeId).toBe("first");
  });

  it("makes an ending to reach when the campaign has nowhere else", () => {
    const flow = flowOf("only", node("only", "encounter"));
    const result = addWayOut(flow, "only");
    expect(result.kind).toBe("changed");
    if (result.kind !== "changed") return;
    expect(result.flow.nodes).toHaveLength(2);
    expect(result.flow.nodes[1]).toMatchObject({
      kind: "terminal", transitions: []
    });
    expect(result.summary).toContain("an ending for it to reach");
    // And the campaign is whole: the new ending is reachable from the entry.
    expect(layOutFlow(result.flow).stranded).toEqual([]);
  });
});

describe("taking a way out away", () => {
  it("becomes an ending once it leads nowhere at all", () => {
    const result = removeWayOut(straightRoad(), "first", "to_last");
    expect(result.kind).toBe("changed");
    if (result.kind !== "changed") return;
    expect(result.flow.nodes[0]!.kind).toBe("terminal");
    expect(result.flow.nodes[0]!.transitions).toEqual([]);
    expect(result.summary).toBe("Stop first leading to last");
    // And the stop it used to reach is now visibly stranded rather than
    // quietly unreachable.
    expect(layOutFlow(result.flow).stranded).toEqual(["last"]);
  });

  it("keeps the kind when there is still another way out", () => {
    const flow = flowOf(
      "fork",
      node("fork", "encounter", ["left", "right"]),
      node("left", "terminal"),
      node("right", "terminal")
    );
    const result = removeWayOut(flow, "fork", "to_left");
    expect(result.kind).toBe("changed");
    if (result.kind !== "changed") return;
    expect(result.flow.nodes[0]!.kind).toBe("encounter");
    expect(result.flow.nodes[0]!.transitions).toHaveLength(1);
  });

  it("refuses a way out that is already gone", () => {
    expect(removeWayOut(straightRoad(), "first", "no_such"))
      .toMatchObject({ kind: "refused" });
  });
});
