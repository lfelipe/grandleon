// SPDX-License-Identifier: MIT
// Whether the picture of a campaign can show a campaign.
//
// The layout gives every rank a fixed pitch, which is what keeps a stop
// readable and a line followable. The cost is that the picture grows with the
// campaign while the panel does not, and it grows quickly: `FLOW_NODE_WIDTH`
// plus `FLOW_COLUMN_GAP` is 300px a rank, so a campaign of twenty stops is
// about three screens across and one of a hundred and twenty is nineteen.
//
// That made the one surface whose whole purpose is showing the shape of a
// branching campaign unable to show the shape of one. These cases are about
// the control that fixes it, and about the two things it must not break: the
// geometry the lines are drawn from stays in one unit whatever the scale, and
// a stop stays a button a keyboard can reach.
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import type { CampaignFlow, SourceProject } from "../generated/source-v1";
import { createSourceProject } from "../domain/source-project-document";
import { FLOW_COLUMN_GAP, FLOW_NODE_WIDTH, layOutFlow } from "../domain/flow-graph";
import CampaignFlowGraph from "./CampaignFlowGraph.vue";

afterEach(() => document.body.replaceChildren());

/** A road of `count` stops, each leading to the next. */
function road(count: number): CampaignFlow {
  const nodes = Array.from({ length: count }, (_, index) => ({
    id: `stop_${index}`,
    name: `Stop ${index}`,
    kind: "encounter" as const,
    transitions: index + 1 < count
      ? [{ id: `to_${index}`, targetNodeId: `stop_${index + 1}`, priority: 0 }]
      : []
  }));
  return {
    contractVersion: "1.0.0",
    entryNodeId: "stop_0",
    nodes
  } as unknown as CampaignFlow;
}

function mount(flow: CampaignFlow, width = 1200) {
  const host = document.createElement("div");
  document.body.append(host);
  const app = createApp(CampaignFlowGraph, {
    flow,
    project: createSourceProject() as SourceProject
  });
  app.mount(host);
  // jsdom lays nothing out, so `clientWidth` is zero and the component's own
  // fallback is life size. The room a panel has is what this is measuring, so
  // it is stated here rather than left to a layout engine that is not present.
  // A campaign with no stops draws no picture, so there is nothing to size and
  // nothing these cases would ask about it.
  const scroller = host.querySelector<HTMLElement>(".flow-graph-scroll");
  if (scroller) Object.defineProperty(scroller, "clientWidth", { value: width });
  return { app, host, scroller: scroller ?? { clientWidth: width } };
}

const picture = (host: HTMLElement) =>
  host.querySelector<HTMLElement>(".flow-graph")!;
const wrapper = (host: HTMLElement) =>
  host.querySelector<HTMLElement>(".flow-graph-scale")!;
const chooser = (host: HTMLElement) =>
  host.querySelector<HTMLSelectElement>("#flow-zoom")!;

async function choose(host: HTMLElement, value: string) {
  const select = chooser(host);
  select.value = value;
  select.dispatchEvent(new Event("change"));
  await nextTick();
}

describe("showing the shape of a campaign", () => {
  it("draws a road wider than its panel scaled down to fit", async () => {
    // Twenty stops is 5908px of picture against 1200px of panel: the shape is
    // unreadable in a scroll box and obvious at a fifth of the size.
    const { app, host, scroller } = mount(road(20));
    // Provoke the measurement the way a resize does.
    window.dispatchEvent(new Event("resize"));
    await nextTick();

    const laidOut = layOutFlow(road(20));
    expect(laidOut.width).toBeGreaterThan(scroller.clientWidth);

    const transform = picture(host).style.transform;
    const scale = Number(/scale\(([^)]+)\)/.exec(transform)?.[1]);
    expect(scale).toBeLessThan(1);
    // The whole road, and no more than the whole road: fitting is the point,
    // and enlarging past life size would be a different feature.
    expect(laidOut.width * scale).toBeLessThanOrEqual(scroller.clientWidth);
    app.unmount();
  });

  it("never enlarges a road its panel already holds", async () => {
    // Three stops is 808px, which fits. Scaling it up to fill the panel would
    // make a short campaign look like a long one.
    const { app, host } = mount(road(3), 1200);
    window.dispatchEvent(new Event("resize"));
    await nextTick();
    expect(picture(host).style.transform).toBe("scale(1)");
    app.unmount();
  });

  it("draws life size when asked, whatever fits", async () => {
    const { app, host } = mount(road(20));
    window.dispatchEvent(new Event("resize"));
    await nextTick();
    await choose(host, "100");
    expect(picture(host).style.transform).toBe("scale(1)");
    app.unmount();
  });

  it("scrolls the drawing rather than the geometry behind it", async () => {
    // The wrapper is what has scrollbars, so it has to be the size of the
    // picture *after* the scale. Sized from the layout instead, a road drawn at
    // a quarter would scroll four times further than it is drawn.
    const { app, host } = mount(road(20));
    window.dispatchEvent(new Event("resize"));
    await nextTick();
    await choose(host, "25");

    const laidOut = layOutFlow(road(20));
    expect(picture(host).style.transform).toBe("scale(0.25)");
    expect(wrapper(host).style.width)
      .toBe(`${Math.ceil(laidOut.width * 0.25)}px`);
    // And the geometry itself has not moved: the box is still authored at its
    // own pitch, so a line and the box it touches cannot disagree at any scale.
    expect(picture(host).style.width).toBe(`${laidOut.width}px`);
    app.unmount();
  });

  it("says what it is doing, for somebody who cannot see the picture", async () => {
    const { app, host } = mount(road(20));
    window.dispatchEvent(new Event("resize"));
    await nextTick();
    await choose(host, "50");
    const said = host.querySelector<HTMLElement>(".flow-zoom span")!;
    expect(said.getAttribute("aria-live")).toBe("polite");
    expect(said.textContent).toContain("20 stops");
    expect(said.textContent).toContain("50%");
    app.unmount();
  });

  it("keeps every stop a button at any scale", async () => {
    // A scaled picture is still the thing an author drives. If zooming turned
    // the stops into a drawing, the keyboard route into the campaign would go
    // with it.
    const { app, host } = mount(road(20));
    window.dispatchEvent(new Event("resize"));
    await nextTick();
    await choose(host, "25");
    expect(host.querySelectorAll("button.flow-stop")).toHaveLength(20);
    app.unmount();
  });

  it("offers no control to a campaign with no stops", async () => {
    const { app, host } = mount(road(0));
    await nextTick();
    expect(host.querySelector("#flow-zoom")).toBeNull();
    expect(host.textContent).toContain("No stops yet");
    app.unmount();
  });

  it("is the pitch, not the count, that makes a road wide", () => {
    // Stated so the number this control exists for cannot drift silently: the
    // six-Stage campaign this repository ships is already several screens.
    expect(FLOW_NODE_WIDTH + FLOW_COLUMN_GAP).toBe(300);
    expect(layOutFlow(road(20)).width).toBe(20 * 300 - FLOW_COLUMN_GAP);
  });
});
