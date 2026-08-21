// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { CampaignFlow, CampaignRosterMember } from "../generated/source-v1";
import CampaignFlowEditor from "./CampaignFlowEditor.vue";

afterEach(() => document.body.replaceChildren());

// Every campaign that is played and kept marches out with somebody, so the
// company is part of the fixture rather than something each test invents.
const company: readonly CampaignRosterMember[] = [
  { id: "wren", name: "Wren", unitTypeId: "guardian" },
  { id: "kesh", name: "Kesh", unitTypeId: "outrider" }
];

function mount(
  flow: CampaignFlow | undefined,
  roster: readonly CampaignRosterMember[] = company,
  defaultTurnOrder?: "alternating" | "sideBlocks" | "initiative",
  objectives: readonly { id: string; name: string; kind?: string;
    targetPlacementId?: string }[] = [{ id: "victory", name: "Victory" }]
) {
  const host = document.createElement("div");
  document.body.append(host);
  const onSave = vi.fn();
  const app = createApp(CampaignFlowEditor, {
    flow,
    maps: [{
      id: "field",
      name: "Field",
      width: 4,
      height: 3,
      terrain: Array.from({ length: 12 }, () => "plain")
    }],
    unitTypes: [
      { id: "guardian", name: "Guardian" },
      { id: "outrider", name: "Outrider" }
    ],
    objectives,
    dialogues: [{ id: "intro", name: "Intro" }],
    items: [{ id: "key", name: "Key" }],
    roster,
    ...(defaultTurnOrder ? { defaultTurnOrder } : {}),
    onSave
  });
  app.mount(host);
  return { app, host, onSave };
}

function select(host: HTMLElement, id: string): HTMLSelectElement {
  const found = host.querySelector<HTMLSelectElement>(id);
  if (!found) throw new Error(`select '${id}' not found`);
  return found;
}

function choose(host: HTMLElement, id: string, value: string) {
  const control = select(host, id);
  control.value = value;
  control.dispatchEvent(new Event("change", { bubbles: true }));
}

function button(host: HTMLElement, text: string): HTMLButtonElement {
  const result = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim().startsWith(text)
  );
  if (!result) throw new Error(`button '${text}' not found`);
  return result;
}

describe("CampaignFlowEditor", () => {
  it("creates an explicit entry and adds a conditional branch", async () => {
    const { app, host, onSave } = mount(undefined);
    button(host, "Give this campaign an order of events").click();
    await nextTick();
    button(host, "Add node").click();
    await nextTick();
    button(host, "Start").click();
    button(host, "Add transition").click();
    await nextTick();

    const target = host.querySelector<HTMLSelectElement>("#transition-0-target")!;
    target.value = "new_node";
    target.dispatchEvent(new Event("change", { bubbles: true }));
    const conditional = host.querySelector<HTMLInputElement>(
      'fieldset input[type="checkbox"]'
    )!;
    conditional.click();
    await nextTick();
    const condition = host.querySelector<HTMLSelectElement>(
      "#transition-0-condition"
    )!;
    condition.value = "inventoryAtLeast";
    condition.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "New Node").click();
    await nextTick();
    const nodeId = host.querySelector<HTMLInputElement>("#campaign-node-id")!;
    nodeId.value = "rejoin";
    nodeId.dispatchEvent(new Event("change", { bubbles: true }));
    button(host, "Save the order of events").click();

    const saved = onSave.mock.calls[0]?.[0] as CampaignFlow;
    expect(saved.entryNodeId).toBe("start");
    expect(saved.nodes[0].transitions[0]).toEqual(expect.objectContaining({
      targetNodeId: "rejoin",
      priority: 0,
      when: expect.objectContaining({
        kind: "inventoryAtLeast",
        itemId: "key",
        quantity: 1
      })
    }));
    app.unmount();
  });

  it("authors objective-result branches with compiler-recognised results", async () => {
    const { app, host, onSave } = mount(undefined);
    button(host, "Give this campaign an order of events").click();
    await nextTick();
    button(host, "Add node").click();
    await nextTick();
    button(host, "Start").click();
    button(host, "Add transition").click();
    await nextTick();
    host.querySelector<HTMLInputElement>(
      'fieldset input[type="checkbox"]'
    )!.click();
    await nextTick();
    const condition = host.querySelector<HTMLSelectElement>(
      "#transition-0-condition"
    )!;
    condition.value = "objectiveResult";
    condition.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();

    const result = host.querySelector<HTMLSelectElement>("#transition-0-result")!;
    // The content compiler accepts exactly these two values.
    expect([...result.options].map((option) => option.value))
      .toEqual(["victory", "defeat"]);
    expect(result.value).toBe("victory");
    result.value = "defeat";
    result.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save the order of events").click();
    const saved = onSave.mock.calls[0]?.[0] as CampaignFlow;
    expect(saved.nodes[0]?.transitions[0]?.when).toEqual({
      kind: "objectiveResult",
      objectiveId: "victory",
      result: "defeat"
    });
    app.unmount();
  });

  it("preserves a diamond graph whose branches recombine", () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "start",
      nodes: [
        {
          id: "start",
          name: "Start",
          kind: "story",
          transitions: [
            {
              id: "left",
              targetNodeId: "left",
              priority: 0,
              when: { kind: "inventoryAtLeast", itemId: "key", quantity: 1 }
            },
            { id: "right", targetNodeId: "right", priority: 1 }
          ]
        },
        {
          id: "left",
          name: "Left route",
          kind: "story",
          transitions: [{ id: "merge", targetNodeId: "rejoin", priority: 0 }]
        },
        {
          id: "right",
          name: "Right route",
          kind: "story",
          transitions: [{ id: "merge", targetNodeId: "rejoin", priority: 0 }]
        },
        {
          id: "rejoin",
          name: "Rejoined route",
          kind: "terminal",
          transitions: []
        }
      ]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    button(host, "Save the order of events").click();
    const saved = onSave.mock.calls[0]?.[0] as CampaignFlow;
    expect(saved.nodes.filter((node) =>
      node.transitions.some((transition) => transition.targetNodeId === "rejoin")
    )).toHaveLength(2);
    app.unmount();
  });

  it("authors a story node's dialogue sequence and saves it in order", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);

    const select = host.querySelector<HTMLSelectElement>("#cutscene-add-existing")!;
    select.value = "intro";
    select.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Add scene").click();
    await nextTick();
    expect(host.querySelector(".cutscene-list")?.textContent).toContain("Intro");

    button(host, "Save the order of events").click();
    const saved = onSave.mock.calls[0]?.[0] as CampaignFlow;
    expect(saved.nodes[0]?.dialogueIds).toEqual(["intro"]);
    app.unmount();
  });

  it("recruits somebody at the node they join, and keeps them out of earlier boards", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    button(host, "Add recruit").click();
    await nextTick();

    const name = host.querySelector<HTMLInputElement>("#recruit-0-name")!;
    name.value = "Mirea";
    name.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    choose(host, "#recruit-0-unit-type", "outrider");
    await nextTick();

    button(host, "Save the order of events").click();
    const saved = onSave.mock.calls[0]?.[0] as CampaignFlow;
    expect(saved.nodes[0]?.recruits).toEqual([
      { id: "recruit", name: "Mirea", unitTypeId: "outrider" }
    ]);
    app.unmount();
  });

  it("writes a recruit who is more than their character, and saves it with them", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        recruits: [{ id: "mirea", name: "Mirea", unitTypeId: "outrider" }],
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    // A recruit is a member of the company from the moment they join, so the
    // same two knobs the founding company has are here, under this list's own
    // control names.
    const skill = host.querySelector<HTMLInputElement>("#recruit-0-stat-skill")!;
    skill.value = "4";
    skill.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    const reach = host.querySelector<HTMLInputElement>("#recruit-0-range-bonus")!;
    reach.value = "1";
    reach.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();

    button(host, "Save the order of events").click();
    const saved = onSave.mock.calls[0]?.[0] as CampaignFlow;
    expect(saved.nodes[0]?.recruits).toEqual([{
      id: "mirea",
      name: "Mirea",
      unitTypeId: "outrider",
      specificity: { stats: { skill: 4 }, rangeBonus: 1 }
    }]);
    app.unmount();
  });

  it("refuses a recruit who takes an identity the company already spends", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        recruits: [{ id: "wren", name: "Another Wren", unitTypeId: "guardian" }],
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    // Said beside the recruit as it is typed…
    expect(host.querySelector(".roster-editor")?.textContent)
      .toContain("Somebody else in this campaign is already 'wren'");
    button(host, "Save the order of events").click();
    await nextTick();
    // …and again as the reason the flow cannot be saved.
    expect(onSave).not.toHaveBeenCalled();
    expect(host.querySelector(".flow-problems")?.textContent)
      .toContain("Two people in this campaign are 'wren'");
    app.unmount();
  });

  it("grants a node's items into the store, on any kind of node", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    // A story node is the obvious author of a gift, so the list is offered
    // here rather than only on a Stage.
    expect(host.textContent).toContain("What the company is given here");
    expect(host.textContent).toContain("Nothing here yet.");

    button(host, "Add grant").click();
    await nextTick();
    // The picker offers the item category and nothing else.
    const chooser = select(host, "#node-grant-0-item");
    expect([...chooser.options].map((option) => option.value)).toEqual(["item:key"]);

    const quantity = host.querySelector<HTMLInputElement>(
      "#node-grant-0-quantity"
    )!;
    quantity.value = "3";
    quantity.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();

    button(host, "Save the order of events").click();
    const saved = onSave.mock.calls[0]?.[0] as CampaignFlow;
    expect(saved.nodes[0]?.grants).toEqual([{ itemId: "key", quantity: 3 }]);
    app.unmount();
  });

  it("removes a node's last grant rather than saving an empty list", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        grants: [{ itemId: "key", quantity: 1 }],
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    button(host, "Remove grant 1").click();
    await nextTick();
    button(host, "Save the order of events").click();
    expect(onSave.mock.calls[0]?.[0]).toBeDefined();
    expect((onSave.mock.calls[0]?.[0] as CampaignFlow).nodes[0])
      .not.toHaveProperty("grants");
    app.unmount();
  });

  it("refuses a grant of something the project does not hold", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        grants: [{ itemId: "ghost", quantity: 1 }, { itemId: "key", quantity: 0 }],
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    // Said beside the grant as it is edited…
    const grants = host.querySelector<HTMLElement>(
      'section[aria-labelledby="node-grant-title"]'
    )!;
    expect(grants.textContent).toContain(
      "'ghost' is not an item in this project"
    );
    expect(grants.textContent).toContain(
      "Say how many, as a whole number of at least 1."
    );
    button(host, "Save the order of events").click();
    await nextTick();
    // …and again as the reason the flow cannot be saved.
    expect(onSave).not.toHaveBeenCalled();
    const problems = host.querySelector(".flow-problems")?.textContent ?? "";
    expect(problems).toContain("grants 'ghost', which is not an item");
    expect(problems).toContain("Say how many, as a whole number from 1 to 65535");
    app.unmount();
  });

  it("takes the cap away with the board when a Stage stops being one", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "battle",
      nodes: [{
        id: "battle",
        name: "Battle",
        kind: "encounter",
        mapId: "field",
        placements: [
          {
            id: "wren_here",
            memberId: "wren",
            unitTypeId: "guardian",
            side: "first",
            x: 0,
            y: 0
          },
          {
            id: "enemy",
            unitTypeId: "outrider",
            side: "second",
            x: 3,
            y: 2
          }
        ],
        deployment: { id: "east_bank", capacity: 2 },
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    choose(host, "#campaign-node-kind", "story");
    await nextTick();
    // A capacity is a rule about who takes a board, so a node that fights
    // nothing cannot carry one, and the control it was written in is gone.
    expect(host.querySelector("#node-deployment-capacity")).toBeNull();
    button(host, "Save the order of events").click();
    expect((onSave.mock.calls[0]?.[0] as CampaignFlow).nodes[0])
      .not.toHaveProperty("deployment");
    app.unmount();
  });

  it("refuses a board nobody the campaign holds could fight", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "battle",
      nodes: [{
        id: "battle",
        name: "Battle",
        kind: "encounter",
        mapId: "field",
        placements: [
          {
            id: "empty_seat",
            unitTypeId: "guardian",
            side: "first",
            x: 0,
            y: 0
          },
          {
            id: "wren_here",
            memberId: "wren",
            unitTypeId: "outrider",
            side: "first",
            x: 1,
            y: 0
          },
          {
            id: "wren_again",
            memberId: "wren",
            unitTypeId: "guardian",
            side: "first",
            x: 2,
            y: 0
          },
          {
            id: "ghost",
            memberId: "nobody",
            unitTypeId: "guardian",
            side: "first",
            x: 3,
            y: 0
          },
          {
            id: "enemy",
            memberId: "kesh",
            unitTypeId: "outrider",
            side: "second",
            x: 0,
            y: 1
          }
        ],
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    button(host, "Save the order of events").click();
    await nextTick();

    expect(onSave).not.toHaveBeenCalled();
    const problems = host.querySelector(".flow-problems")!.textContent!;
    expect(problems).toContain("'empty_seat' stands on your side but names nobody");
    expect(problems).toContain("'nobody', who is nobody in this campaign");
    expect(problems).toContain("both field 'Wren' (wren)");
    expect(problems).toContain("but stands as 'outrider'");
    expect(problems).toContain("'enemy' fights for the other side but names 'kesh'");
    app.unmount();
  });

  it("refuses to save a campaign that marches out with nobody", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow, []);
    button(host, "Save the order of events").click();
    await nextTick();

    expect(onSave).not.toHaveBeenCalled();
    expect(host.querySelector(".flow-problems")?.textContent)
      .toContain("This campaign starts with nobody");
    app.unmount();
  });

  it("sends an author to Stages rather than opening the board here", async () => {
    // One entry point per thing. A Stage's contents are authored under Stages
    // and nowhere else: its ground, its board, what winning means, what is
    // said, who joins, what they are given. Two doors onto one node
    // is two places it can disagree with itself. What Flow decides about a
    // Stage is where it comes and what it leads to, and those are still here.
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "stage",
      nodes: [{
        id: "stage",
        name: "The Crossing",
        kind: "encounter",
        mapId: "field",
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const host = document.createElement("div");
    document.body.append(host);
    const onOpenStage = vi.fn();
    const app = createApp(CampaignFlowEditor, {
      flow,
      maps: [{
        id: "field", name: "Field", width: 4, height: 3,
        terrain: Array.from({ length: 12 }, () => "plain")
      }],
      unitTypes: [{ id: "guardian", name: "Guardian" }],
      objectives: [{ id: "victory", name: "Victory" }],
      dialogues: [{ id: "intro", name: "Intro" }],
      items: [{ id: "key", name: "Key" }],
      roster: company,
      onOpenStage
    });
    app.mount(host);

    // Not one control that writes the Stage.
    expect(host.querySelector("#node-turn-order")).toBeNull();
    expect(host.querySelector("#node-deployment-capacity")).toBeNull();
    expect(host.querySelector("#campaign-node-map")).toBeNull();
    expect(host.querySelector("#cutscene-add-existing")).toBeNull();
    expect(host.textContent).not.toContain("Add character placement");
    expect(host.textContent).not.toContain("Who joins the company here");
    expect(host.textContent).not.toContain("What the company is given here");

    // It names the ground it is fought on and offers the one road to the place
    // that changes it.
    expect(host.textContent).toContain("A Stage fought on Field.");
    button(host, "Set this Stage up").click();
    expect(onOpenStage).toHaveBeenCalledWith("stage");

    // And where it goes next is still authored right here.
    expect(host.querySelector("#transition-0-target")).not.toBeNull();
    await nextTick();
    app.unmount();
  });

  it("never says 'encounter' or 'battle' anywhere an author reads", () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "stage",
      nodes: [{
        id: "stage", name: "The Crossing", kind: "encounter",
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, { id: "end", name: "End", kind: "terminal", transitions: [] }]
    } as CampaignFlow;
    const { app, host } = mount(flow);
    const read = (host.textContent ?? "").toLocaleLowerCase();
    expect(read).not.toContain("encounter");
    expect(read).not.toContain("battle");
    expect(read).toContain("stage");
    app.unmount();
  });

  it("takes the board away when a node stops being a Stage, and says so", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "battle",
      nodes: [{
        id: "battle",
        name: "Battle",
        kind: "encounter",
        mapId: "field",
        placements: [{
          id: "wren_here",
          memberId: "wren",
          unitTypeId: "guardian",
          side: "first",
          x: 0,
          y: 0
        }],
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    choose(host, "#campaign-node-kind", "story");
    await nextTick();

    expect(host.textContent).toContain("no longer a Stage");
    button(host, "Save the order of events").click();
    const saved = onSave.mock.calls[0]?.[0] as CampaignFlow;
    expect(saved.nodes[0]?.kind).toBe("story");
    expect(saved.nodes[0]?.placements).toBeUndefined();
    app.unmount();
  });

  it("refuses to save a flow its own loader would reject, in plain words", async () => {
    // The exact shape the loader rejects: a story node going nowhere.
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{ id: "opening", name: "Opening", kind: "story", transitions: [] }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    button(host, "Save the order of events").click();
    await nextTick();

    expect(onSave).not.toHaveBeenCalled();
    const problems = host.querySelector(".flow-problems");
    expect(problems?.textContent).toContain("cannot be saved yet");
    expect(problems?.textContent).toContain("at least one outgoing transition");
    app.unmount();
  });

  it("defaults a new transition to another node, never a self-loop", async () => {
    const { app, host } = mount(undefined);
    button(host, "Give this campaign an order of events").click();
    await nextTick();
    // With no other node to lead to, an ending is created and targeted.
    button(host, "Add transition").click();
    await nextTick();
    const target = host.querySelector<HTMLSelectElement>("#transition-0-target")!;
    expect(target.value).toBe("ending");
    expect(() => button(host, "Ending")).not.toThrow();
    app.unmount();
  });

  it("wires a node somewhere when it stops being an ending", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host, onSave } = mount(flow);
    button(host, "End").click();
    await nextTick();
    const kind = host.querySelector<HTMLSelectElement>("#campaign-node-kind")!;
    kind.value = "story";
    kind.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();

    expect(host.textContent).toContain("Added a transition");
    button(host, "Save the order of events").click();
    const saved = onSave.mock.calls[0]?.[0] as CampaignFlow;
    const end = saved.nodes.find((node) => node.id === "end");
    expect(end?.kind).toBe("story");
    expect(end?.transitions.length).toBeGreaterThan(0);
    app.unmount();
  });

  it("announces an unsaved flow draft as dirty", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const host = document.createElement("div");
    document.body.append(host);
    const onDirty = vi.fn();
    const app = createApp(CampaignFlowEditor, {
      flow,
      maps: [],
      unitTypes: [],
      objectives: [],
      dialogues: [],
      items: [],
      onDirty
    });
    app.mount(host);
    const name = host.querySelector<HTMLInputElement>("#campaign-node-name")!;
    name.value = "The Approach";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(onDirty).toHaveBeenCalled();
    app.unmount();
  });

  it("keeps focus while a multi-character transition identifier is typed", async () => {
    const flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [{
        id: "opening",
        name: "Opening",
        kind: "story",
        transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
      }, {
        id: "end",
        name: "End",
        kind: "terminal",
        transitions: []
      }]
    } as CampaignFlow;
    const { app, host } = mount(flow);
    const input = host.querySelector<HTMLInputElement>("#transition-0-id")!;
    input.focus();
    for (const partial of ["t", "to", "to_", "to_e", "to_en", "to_end"]) {
      input.value = partial;
      input.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
    }
    // The fieldset must not remount per keystroke: same element, still focused.
    expect(host.querySelector("#transition-0-id")).toBe(input);
    expect(document.activeElement).toBe(input);
    expect(input.value).toBe("to_end");
    app.unmount();
  });
});

// A flow whose one Stage is decided by a targeted objective, and whose board
// holds one enemy and one member of the company.
const targetedFlow: CampaignFlow = {
  contractVersion: "1.0.0",
  entryNodeId: "battle",
  nodes: [{
    id: "battle",
    name: "The Gate",
    kind: "encounter",
    mapId: "field",
    objectiveIds: ["fell_the_warden"],
    placements: [
      {
        id: "left_flank",
        memberId: "wren",
        unitTypeId: "guardian",
        side: "first",
        x: 0,
        y: 0
      },
      { id: "warden", unitTypeId: "outrider", side: "second", x: 3, y: 2 }
    ],
    transitions: [{ id: "onward", targetNodeId: "battle", priority: 0 }]
  }]
};

describe("CampaignFlowEditor references", () => {
  it("refuses a Stage decided by somebody who is not on it", async () => {
    // The content compiler resolves the target against the board's placements
    // and refuses the encounter when it cannot find it, so a flow saved with a
    // dangling target is a Stage no client can load. Nothing else in the
    // editor or the analyzer follows this reference.
    const { app, host, onSave } = mount(
      targetedFlow,
      company,
      undefined,
      [{
        id: "fell_the_warden",
        name: "Fell the Warden",
        kind: "defeatTarget",
        targetPlacementId: "somebody_who_left"
      }]
    );
    button(host, "Save the order of events").click();
    await nextTick();

    expect(onSave).not.toHaveBeenCalled();
    expect(host.querySelector(".flow-problems")?.textContent)
      .toContain("nobody who stands on this board");
    app.unmount();
  });

  it("accepts a target named by the member standing there", async () => {
    // A placement fielding a member is resolved by the member's identity, and
    // that is the key the compiler and the runtime both match.
    const { app, host, onSave } = mount(
      targetedFlow,
      company,
      undefined,
      [{
        id: "fell_the_warden",
        name: "Fell the Warden",
        kind: "protectTarget",
        targetPlacementId: "wren"
      }]
    );
    button(host, "Save the order of events").click();
    await nextTick();

    expect(onSave).toHaveBeenCalledTimes(1);
    app.unmount();
  });

  it("names a branch condition the project no longer holds", async () => {
    // The live road is an imported archive whose objectives this project does
    // not have. A select whose value matches no option renders blank, so an
    // unnamed branch reads as unset while the condition it carries is still
    // there and still refused downstream.
    const { app, host } = mount({
      contractVersion: "1.0.0",
      entryNodeId: "start",
      nodes: [{
        id: "start",
        name: "Start",
        kind: "story",
        transitions: [{
          id: "onward",
          targetNodeId: "start",
          priority: 0,
          when: {
            kind: "objectiveResult",
            objectiveId: "an_objective_that_left",
            result: "victory"
          }
        }]
      }]
    });
    await nextTick();

    const objective = select(host, "#transition-0-objective");
    expect(objective.value).toBe("an_objective_that_left");
    expect(objective.textContent).toContain("not an objective in this project");
    expect(host.textContent).toContain(
      "This branch is decided by an objective this project does not have"
    );
    app.unmount();
  });

  it("names a branch item the project no longer holds", async () => {
    const { app, host } = mount({
      contractVersion: "1.0.0",
      entryNodeId: "start",
      nodes: [{
        id: "start",
        name: "Start",
        kind: "story",
        transitions: [{
          id: "onward",
          targetNodeId: "start",
          priority: 0,
          when: {
            kind: "inventoryAtLeast",
            itemId: "an_item_that_left",
            quantity: 1
          }
        }]
      }]
    });
    await nextTick();

    const item = select(host, "#transition-0-item");
    expect(item.value).toBe("an_item_that_left");
    expect(item.textContent).toContain("not an item in this project");
    app.unmount();
  });

  it("writes the notes the format holds on a node and on a branch", async () => {
    // The same field is edited on roster members and item grants, and the
    // three things authored here carry it too: a field the format holds and no
    // control writes is a field an author cannot use.
    const { app, host, onSave } = mount({
      contractVersion: "1.0.0",
      entryNodeId: "start",
      nodes: [{
        id: "start",
        name: "Start",
        kind: "story",
        transitions: [{ id: "onward", targetNodeId: "start", priority: 0 }]
      }]
    });
    await nextTick();

    const nodeNotes = host.querySelector<HTMLTextAreaElement>(
      "#campaign-node-notes"
    )!;
    nodeNotes.value = "The gates are shut here.";
    nodeNotes.dispatchEvent(new Event("change", { bubbles: true }));
    const branchNotes = host.querySelector<HTMLTextAreaElement>(
      "#transition-0-notes"
    )!;
    branchNotes.value = "Always taken for now.";
    branchNotes.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save the order of events").click();
    await nextTick();

    const saved = onSave.mock.calls.at(-1)?.[0] as CampaignFlow;
    expect(saved.nodes[0]!.notes).toBe("The gates are shut here.");
    expect(saved.nodes[0]!.transitions[0]!.notes).toBe("Always taken for now.");

    // Emptied, the field goes rather than being stored as nothing.
    nodeNotes.value = "   ";
    nodeNotes.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save the order of events").click();
    await nextTick();
    expect("notes" in (onSave.mock.calls.at(-1)![0] as CampaignFlow).nodes[0]!)
      .toBe(false);
    app.unmount();
  });
});
