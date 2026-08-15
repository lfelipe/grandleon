// SPDX-License-Identifier: MIT
import { createApp, nextTick, reactive } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type {
  CampaignNode,
  SourceCampaign,
  SourceProject
} from "../generated/source-v1";
import { createSourceProject } from "../domain/source-project-document";
import StageEditor from "./StageEditor.vue";

afterEach(() => document.body.replaceChildren());

// Every campaign that is played and kept marches out with somebody, so the
// company is part of the fixture rather than something each test invents.
const company = [
  { id: "wren", name: "Wren", unitTypeId: "guardian" },
  { id: "kesh", name: "Kesh", unitTypeId: "outrider" }
];

const ending: CampaignNode = {
  id: "end",
  name: "End",
  kind: "terminal",
  transitions: []
};

/** A Stage on the fixture's one map, with whatever else the test needs on it. */
function stageNode(patch: Partial<CampaignNode> = {}): CampaignNode {
  return {
    id: "stage",
    name: "Stage",
    kind: "encounter",
    mapId: "field",
    transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }],
    ...patch
  } as CampaignNode;
}

/**
 * Mounts the Stage surface over a project that answers back.
 *
 * The editor is stateless: every control emits the whole node and the surface
 * above stores it. So the harness stores it too, and the component sees its own
 * writes on the next frame, which is what makes a test that clears a field it
 * has just set a test of the real thing rather than of one render.
 */
function mount(
  node: CampaignNode,
  options: {
    defaultTurnOrder?: "alternating" | "sideBlocks" | "initiative";
    maps?: SourceProject["maps"];
    campaign?: Partial<SourceCampaign>;
  } = {}
) {
  const host = document.createElement("div");
  document.body.append(host);
  const project = reactive({
    ...createSourceProject(),
    classes: [{
      id: "any", name: "Any",
      baseStats: { health: 5, movement: 3, strength: 2, defense: 0 }
    }],
    unitTypes: [
      { id: "guardian", name: "Guardian", classId: "any" },
      { id: "outrider", name: "Outrider", classId: "any" }
    ],
    maps: options.maps ?? [{
      id: "field", name: "Field", width: 4, height: 3,
      terrain: Array.from({ length: 12 }, () => "plain")
    }],
    objectives: [{ id: "victory", name: "Victory" }],
    dialogues: [{ id: "intro", name: "Intro" }],
    items: [{ id: "key", name: "Key", stackLimit: 1 }],
    ...(options.defaultTurnOrder
      ? { defaultTurnOrder: options.defaultTurnOrder }
      : {}),
    campaigns: [{
      id: "war",
      name: "The War",
      roster: [...company],
      ...options.campaign,
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: node.id,
        nodes: [node, ending]
      }
    }]
  }) as SourceProject;

  // Every emission is recorded *and* applied, so a test can assert on what the
  // surface asked for and then keep authoring against the result.
  const onSaveNode = vi.fn((saved: CampaignNode) => {
    const flow = project.campaigns![0]!.flow!;
    flow.nodes = flow.nodes.map(
      (candidate) => candidate.id === saved.id ? saved : candidate
    ) as typeof flow.nodes;
  });
  const onOpenInFlow = vi.fn();
  const onDrawMap = vi.fn();

  const app = createApp(StageEditor, {
    project,
    campaignId: "war",
    nodeId: node.id,
    onSaveNode,
    onOpenInFlow,
    onDrawMap
  });
  app.mount(host);
  return { app, host, project, onSaveNode, onOpenInFlow, onDrawMap };
}

function saved(onSaveNode: ReturnType<typeof vi.fn>): CampaignNode {
  return onSaveNode.mock.lastCall?.[0] as CampaignNode;
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

describe("StageEditor", () => {
  it("asks for the ground first, and takes it back when it is cleared",
    async () => {
      // A Stage is a fight on a map, so the ground is the first thing in it.
      // A Stage that names no ground has not been given any: the field goes
      // rather than being stored as an empty string the format cannot read.
      const { app, host, onSaveNode } = mount(stageNode());
      const ground = select(host, "#stage-map");
      expect(ground.value).toBe("field");
      expect([...ground.options].map((option) => option.value))
        .toEqual(["", "field"]);
      choose(host, "#stage-map", "");
      await nextTick();
      expect(saved(onSaveNode)).not.toHaveProperty("mapId");
      app.unmount();
    });

  it("sends an author to draw one when the game has no ground at all", () => {
    // The one orphan state this surface could have: a Stage that cannot be set
    // up because nothing exists to set it up on. It says so and offers the way
    // out rather than showing an empty picker.
    const { app, host, onDrawMap } = mount(stageNode(), {
      maps: []
    });
    expect(host.textContent).toContain("no ground to fight over yet");
    button(host, "Draw a map").click();
    expect(onDrawMap).toHaveBeenCalled();
    app.unmount();
  });

  it("lets a Stage follow the game's turn order, and says which that is",
    async () => {
      const { app, host, onSaveNode } = mount(stageNode(), {
        defaultTurnOrder: "initiative"
      });
      const order = select(host, "#stage-turn-order");
      // A board that states nothing shows it, rather than showing the order it
      // happens to run under: selecting a value here is an author's decision.
      expect(order.value).toBe("");
      expect(order.options[0]!.textContent).toContain(
        "Everyone mixed together, fastest first"
      );
      // Opening the Stage writes nothing, so merely looking at a board never
      // turns it into an override.
      expect(onSaveNode).not.toHaveBeenCalled();
      app.unmount();
    });

  it("states an order on the Stage, then gives it back to the game", async () => {
    const { app, host, onSaveNode } = mount(
      stageNode({ turnOrder: "sideBlocks" }),
      { defaultTurnOrder: "initiative" }
    );
    expect(select(host, "#stage-turn-order").value).toBe("sideBlocks");

    choose(host, "#stage-turn-order", "");
    await nextTick();
    // Following the game again is an absent field, not the game's current
    // value written down: the board must move when the setting does.
    expect(saved(onSaveNode)).not.toHaveProperty("turnOrder");
    app.unmount();
  });

  it("authors typed two-side placements", async () => {
    const { app, host, onSaveNode } = mount(stageNode());
    button(host, "Add character placement").click();
    await nextTick();
    button(host, "Add character placement").click();
    await nextTick();
    // Where the second one stands is said by pressing a tile, not typed: the
    // board is the control, and adding a placement selects the one just added.
    host.querySelector<HTMLElement>(
      '.placement-cell[data-cell="3"]'
    )!.click();
    await nextTick();

    // Your own side is fought by the company, so the first placement is one of
    // them and is what they are; the opposing side is nobody in particular.
    expect(saved(onSaveNode).placements).toEqual([
      {
        id: "unit",
        memberId: "wren",
        unitTypeId: "guardian",
        side: "first",
        x: 0,
        y: 0
      },
      { id: "unit_2", unitTypeId: "guardian", side: "second", x: 3, y: 0 }
    ]);
    app.unmount();
  });

  it("fields a chosen member, as who they are, on the player's side",
    async () => {
      const { app, host, onSaveNode } = mount(stageNode());
      button(host, "Add character placement").click();
      await nextTick();

      // The picker offers this campaign's company and nobody else, saying what
      // each of them is.
      const picker = select(host, "#placement-0-member");
      expect([...picker.options].map((option) => option.value))
        .toEqual(["", "wren", "kesh"]);
      expect(picker.options[2]?.textContent).toContain("Kesh (kesh)");
      expect(picker.options[2]?.textContent).toContain("Outrider");

      choose(host, "#placement-0-member", "kesh");
      await nextTick();
      // A member is one character wherever they stand: choosing them settles
      // what stands there, so the two can never disagree.
      expect(select(host, "#placement-0-unit-type").value).toBe("outrider");
      expect(saved(onSaveNode).placements?.[0]).toMatchObject({
        memberId: "kesh",
        unitTypeId: "outrider",
        side: "first"
      });
      app.unmount();
    });

  it("lets a member go when a placement changes sides", async () => {
    const { app, host, onSaveNode } = mount(stageNode({
      placements: [{
        id: "wren_here",
        memberId: "wren",
        unitTypeId: "guardian",
        side: "first",
        x: 0,
        y: 0
      }]
    }));
    choose(host, "#placement-0-side", "second");
    await nextTick();
    // The opposing side never fields the company, so the binding goes with the
    // side rather than being saved into a campaign that would refuse to open.
    expect(host.querySelector("#placement-0-member")).toBeNull();
    expect(saved(onSaveNode).placements?.[0]).toEqual({
      id: "wren_here",
      unitTypeId: "guardian",
      side: "second",
      x: 0,
      y: 0
    });
    app.unmount();
  });

  it("caps how many may take a Stage's field, and clears the cap away",
    async () => {
      const { app, host, onSaveNode } = mount(stageNode({
        placements: [
          {
            id: "wren_here", memberId: "wren", unitTypeId: "guardian",
            side: "first", x: 0, y: 0
          },
          { id: "enemy", unitTypeId: "outrider", side: "second", x: 3, y: 2 }
        ]
      }));
      const capacity = host.querySelector<HTMLInputElement>(
        "#stage-deployment-capacity"
      )!;
      // A maximum and not a quota, said where the number is written.
      expect(host.textContent).toContain("A maximum and not a quota");
      expect(host.textContent).toContain("sending fewer is legal");
      // No cap is what a Stage says by default, and the control shows nothing
      // rather than a zero nobody may author.
      expect(capacity.value).toBe("");

      capacity.value = "2";
      capacity.dispatchEvent(new Event("change", { bubbles: true }));
      await nextTick();
      // The deployment object is created around the number, with an identity a
      // diagnostic can name.
      expect(saved(onSaveNode).deployment)
        .toEqual({ id: "stage_deployment", capacity: 2 });

      const cleared = host.querySelector<HTMLInputElement>(
        "#stage-deployment-capacity"
      )!;
      cleared.value = "";
      cleared.dispatchEvent(new Event("change", { bubbles: true }));
      await nextTick();
      // A deployment that states neither a region nor a cap says nothing, so it
      // goes rather than lingering as a record the compiler refuses.
      expect(saved(onSaveNode)).not.toHaveProperty("deployment");
      app.unmount();
    });

  it("keeps a region a cleared cap stood beside", async () => {
    const { app, host, onSaveNode } = mount(stageNode({
      placements: [
        {
          id: "wren_here", memberId: "wren", unitTypeId: "guardian",
          side: "first", x: 0, y: 0
        },
        { id: "enemy", unitTypeId: "outrider", side: "second", x: 3, y: 2 }
      ],
      deployment: { id: "east_bank", tiles: [{ x: 0, y: 0 }], capacity: 2 }
    }));
    const capacity = host.querySelector<HTMLInputElement>(
      "#stage-deployment-capacity"
    )!;
    expect(capacity.value).toBe("2");
    capacity.value = "";
    capacity.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    // The region is still stated; only the cap was cleared. There is no tile
    // picker here, and an editor that could not draw a field must not delete
    // it either.
    expect(saved(onSaveNode).deployment)
      .toEqual({ id: "east_bank", tiles: [{ x: 0, y: 0 }] });
    app.unmount();
  });

  it("writes and clears the note the format holds on a deployment region",
    async () => {
      // A field the format carries and no control writes is a field an author
      // cannot use, and this is the only surface a region is reachable from.
      const { app, host, onSaveNode } = mount(stageNode({
        deployment: { id: "east_bank", tiles: [{ x: 0, y: 0 }] }
      }));
      const notes = host.querySelector<HTMLTextAreaElement>(
        "#stage-deployment-notes"
      )!;
      notes.value = "the bank is narrow here";
      notes.dispatchEvent(new Event("change", { bubbles: true }));
      await nextTick();
      expect(saved(onSaveNode).deployment).toEqual({
        id: "east_bank",
        tiles: [{ x: 0, y: 0 }],
        notes: "the bank is narrow here"
      });

      const written = host.querySelector<HTMLTextAreaElement>(
        "#stage-deployment-notes"
      )!;
      written.value = "   ";
      written.dispatchEvent(new Event("change", { bubbles: true }));
      await nextTick();
      // Emptied is removed, so a region nobody annotated reads like one that
      // never was.
      expect(saved(onSaveNode).deployment)
        .toEqual({ id: "east_bank", tiles: [{ x: 0, y: 0 }] });
      app.unmount();
    });

  it("holds everything a Stage is, so nothing sends the author elsewhere",
    async () => {
      // The whole point of the split: one place a Stage is set up. If any of
      // these moved back out, an author would be hunting for it again.
      const { app, host } = mount(stageNode());
      expect(host.querySelector("#stage-map")).not.toBeNull();
      expect(host.querySelector("#stage-name")).not.toBeNull();
      expect(host.querySelector("#stage-turn-order")).not.toBeNull();
      expect(host.querySelector("#stage-deployment-capacity")).not.toBeNull();
      // What is said on the way in, the board, what winning means, who joins
      // and what they are given.
      expect(host.querySelector("#cutscene-add-existing")).not.toBeNull();
      expect(host.querySelector(".placement-editor, .board-grid")).not.toBeNull();
      expect(host.textContent).toContain("Who joins the company here");
      expect(host.textContent).toContain("What the company is given here");
      await nextTick();
      app.unmount();
    });

  it("reports what is said after without offering to change it here",
    async () => {
      const { app, host, onOpenInFlow } = mount(stageNode(), {});
      const after = host.querySelector<HTMLElement>(".stage-after")!;
      // After belongs to whatever the Stage leads to, so it is named by the
      // node that owns it and changed there. A symmetrical pair of fields
      // would be lying about one of them.
      expect(after.textContent).toContain("End");
      expect(after.querySelector("input")).toBeNull();
      button(after, "Change what End says").click();
      expect(onOpenInFlow).toHaveBeenCalledWith("end");
      app.unmount();
    });

  it("never says 'encounter' or 'battle' anywhere an author reads", () => {
    // The format's word is `encounter` and the editor's is Stage. This surface
    // is where an author spends the most time, so it is where a stray word
    // would teach the wrong vocabulary hardest.
    const { app, host } = mount(stageNode({
      placements: [{
        id: "wren_here", memberId: "wren", unitTypeId: "guardian",
        side: "first", x: 0, y: 0
      }]
    }));
    const read = (host.textContent ?? "").toLocaleLowerCase();
    expect(read).not.toContain("encounter");
    expect(read).not.toContain("battle");
    expect(read).toContain("stage");
    app.unmount();
  });
});
