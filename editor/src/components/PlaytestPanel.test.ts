// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import PlaytestPanel from "./PlaytestPanel.vue";

afterEach(() => document.body.replaceChildren());

const project: SourceProject = {
  schemaVersion: "1.1.0",
  packageId: "f643c13e-ce8d-41d4-b6de-0d69ba5fcade",
  gameId: "playtest.fixture",
  title: "Playtest fixture",
  contentRevision: "0.1.0",
  classes: [{
    id: "guard",
    name: "Guard",
    baseStats: { health: 4, movement: 3, strength: 4, defense: 1 }
  }],
  unitTypes: [
    { id: "blue", name: "Blue Guard", classId: "guard" },
    { id: "red", name: "Red Guard", classId: "guard" }
  ],
  weapons: [],
  items: [],
  maps: [{
    id: "bridge",
    name: "Bridge",
    width: 4,
    height: 3,
    terrain: Array(12).fill("grass")
  }],
  campaigns: [{
    id: "demo",
    name: "Demo",
    flow: {
      contractVersion: "1.0.0",
      entryNodeId: "fight",
      nodes: [{
        id: "fight",
        name: "Friendly fight",
        kind: "encounter",
        mapId: "bridge",
        placements: [
          { id: "blue_one", unitTypeId: "blue", side: "first", x: 0, y: 1 },
          { id: "red_one", unitTypeId: "red", side: "second", x: 2, y: 1 }
        ],
        transitions: [{ id: "done", targetNodeId: "complete", priority: 0 }]
      }, {
        id: "complete",
        name: "Road opens",
        kind: "terminal",
        transitions: []
      }]
    }
  }]
};

function mount(source = project) {
  const host = document.createElement("div");
  document.body.append(host);
  const app = createApp(PlaytestPanel, { project: source });
  app.mount(host);
  return { app, host };
}

function button(host: HTMLElement, text: string): HTMLButtonElement {
  const found = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim().startsWith(text)
  );
  if (!found) throw new Error(`button '${text}' not found`);
  return found;
}

function cell(host: HTMLElement, x: number, y: number): HTMLButtonElement {
  return host.querySelector<HTMLButtonElement>(
    `[aria-label^="Position ${x}, ${y},"]`
  )!;
}

describe("PlaytestPanel", () => {
  it("renders the authored map and reports an actionable invalid-project error", async () => {
    const { app, host } = mount();
    button(host, "Run the Stage").click();
    await nextTick();
    expect(host.querySelectorAll('[role="gridcell"]')).toHaveLength(12);
    expect(host.textContent).toContain("Turn 1: Your side");
    expect(cell(host, 0, 1).textContent).toContain("Blue Guard");
    app.unmount();

    const invalid = structuredClone(project);
    invalid.campaigns = [];
    const invalidMount = mount(invalid);
    button(invalidMount.host, "Run the Stage").click();
    await nextTick();
    expect(invalidMount.host.querySelector('[role="alert"]')?.textContent)
      .toContain("No campaign Stage");
    invalidMount.app.unmount();
  });

  it("plays the reference move, wait, attack loop through campaign completion", async () => {
    const { app, host } = mount();
    button(host, "Run the Stage").click();
    await nextTick();

    cell(host, 0, 1).click();
    await nextTick();
    expect(cell(host, 1, 1).classList).toContain("legal");
    cell(host, 1, 1).click();
    await nextTick();
    expect(host.textContent).toContain("Turn 2: The enemy");

    cell(host, 2, 1).click();
    await nextTick();
    button(host, "Wait").click();
    await nextTick();

    for (let attack = 0; attack < 2; attack += 1) {
      cell(host, 1, 1).click();
      await nextTick();
      button(host, "Attack").click();
      await nextTick();
      expect(cell(host, 2, 1).classList).toContain("target");
      cell(host, 2, 1).click();
      await nextTick();
      if (attack === 0) {
        expect(cell(host, 2, 1).textContent).toContain("HP 1/4");
        cell(host, 2, 1).click();
        await nextTick();
        button(host, "Wait").click();
        await nextTick();
      }
    }

    expect(host.textContent).toContain("Your side won");
    expect(host.textContent).toContain("campaign complete: Road opens");
    expect(host.textContent).toContain("Campaign advanced to terminal");
    expect(host.textContent).toContain("Red Guard died");
    app.unmount();
  });
});
