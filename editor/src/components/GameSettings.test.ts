// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import GameSettings from "./GameSettings.vue";
import { createSourceProject } from "../domain/source-project-document";
import type { CampaignNode, SourceProject } from "../generated/source-v1";

afterEach(() => document.body.replaceChildren());

function stage(id: string, turnOrder?: CampaignNode["turnOrder"]): CampaignNode {
  return {
    id,
    name: id.replace(/_/g, " "),
    kind: "encounter",
    mapId: "field",
    transitions: [{ id: `${id}_done`, targetNodeId: "end", priority: 0 }],
    ...(turnOrder === undefined ? {} : { turnOrder })
  };
}

function withStages(
  project: SourceProject,
  nodes: CampaignNode[]
): SourceProject {
  return {
    ...project,
    campaigns: [
      {
        id: "main",
        name: "The March",
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: nodes[0]!.id,
          nodes: [
            ...nodes,
            { id: "end", name: "End", kind: "terminal", transitions: [] }
          ] as unknown as [CampaignNode, ...CampaignNode[]]
        }
      }
    ]
  };
}

function mount(project: SourceProject) {
  const host = document.createElement("div");
  document.body.append(host);
  const onSubmit = vi.fn();
  const onDirty = vi.fn();
  const app = createApp(GameSettings, { project, onSubmit, onDirty });
  app.mount(host);
  return { app, host, onSubmit, onDirty };
}

function namedButton(host: HTMLElement, name: string): HTMLButtonElement {
  const found = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim() === name
  );
  if (!found) throw new Error(`button '${name}' not found`);
  return found;
}

function saveButton(host: HTMLElement): HTMLButtonElement {
  return namedButton(host, "Save game settings");
}

/** The testing section has its own save, because it is its own section. */
function saveTestingButton(host: HTMLElement): HTMLButtonElement {
  return namedButton(host, "Save testing aids");
}

describe("GameSettings", () => {
  it("gathers the whole-game choices and nothing a record owns", async () => {
    const { app, host } = mount(createSourceProject());
    const labels = [
      ...host.querySelectorAll(".game-settings > .schema-form label")
    ].map((label) => label.textContent?.trim());
    expect(labels).toEqual([
      "What the game is called *",
      "Turn order",
      "If a character falls",
      "Character style",
      "Season",
      // Behind the fold, and therefore last. See below.
      "Game id *",
      "Content revision *"
    ]);
    // The page leads with the choices that make a game and holds back the two
    // that are only names for a machine. Both are questions nobody can answer
    // before the game exists, the id following the title and the revision
    // having a number already, so a first author never opens this and a second one
    // finds it under the word every other form uses.
    const fold = host.querySelector<HTMLDetailsElement>(
      ".game-settings > .schema-form details.advanced-fields"
    )!;
    expect(fold).not.toBeNull();
    expect(fold.hasAttribute("open")).toBe(false);
    expect(fold.querySelector("#field-gameId")).not.toBeNull();
    expect(fold.querySelector("#field-contentRevision")).not.toBeNull();
    expect(fold.textContent).toContain(
      "The name the file carries, and the content revision."
    );
    expect(fold.querySelector("#field-title")).toBeNull();
    // What the author never types and what the game never reads stay off this
    // page entirely.
    expect(host.querySelector("#field-packageId")).toBeNull();
    expect(host.querySelector("#field-schemaVersion")).toBeNull();
    expect(host.querySelector("#field-notes")).toBeNull();
    expect(host.querySelector("#field-extensions")).toBeNull();
    app.unmount();
  });

  it("offers the empty choice, which is what the game was before", async () => {
    // A project written before the setting existed, which is the only project
    // that reaches the page with nothing stored: one made here states its
    // order. The empty choice still has to read as alternating, because that is
    // what the compiler resolves an absent field to.
    const before = createSourceProject();
    delete before.defaultTurnOrder;
    const { app, host } = mount(before);
    const order = host.querySelector<HTMLSelectElement>("#field-defaultTurnOrder")!;
    expect(order.value).toBe("");
    expect(host.textContent).toContain(
      "Stages are ordered Sides take turns, you pick who acts"
    );
    app.unmount();
  });

  it("shows a new game the order it was started with", async () => {
    const { app, host } = mount(createSourceProject());
    const order = host.querySelector<HTMLSelectElement>("#field-defaultTurnOrder")!;
    expect(order.value).toBe("sideBlocks");
    expect(host.textContent).toContain(
      "Stages are ordered All of one side, then all of the other"
    );
    app.unmount();
  });

  it("submits a chosen order and clears it back to absent", async () => {
    const { app, host, onSubmit } = mount(createSourceProject());
    const order = host.querySelector<HTMLSelectElement>("#field-defaultTurnOrder")!;
    order.value = "initiative";
    order.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    saveButton(host).click();
    expect(onSubmit.mock.calls[0]?.[0]).toEqual(
      expect.objectContaining({ defaultTurnOrder: "initiative" })
    );

    order.value = "";
    order.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    saveButton(host).click();
    const cleared = onSubmit.mock.calls[1]?.[0] as Record<string, unknown>;
    // Cleared is absent, not "alternating" written down. The distinction is
    // the whole compatibility claim.
    expect(cleared.defaultTurnOrder ?? "").toBe("");
    app.unmount();
  });

  it("names the Stages that keep their own order, and counts the rest", () => {
    const project = withStages(
      { ...createSourceProject(), defaultTurnOrder: "initiative" },
      [stage("opening"), stage("duel", "sideBlocks"), stage("siege", "alternating")]
    );
    const { app, host } = mount(project);
    expect(host.textContent).toContain("1 Stage follows this setting.");
    expect(host.textContent).toContain(
      "2 Stages chose their own order and keep it"
    );
    const rows = [...host.querySelectorAll(".turn-order-overrides li")].map(
      (row) => [...row.children].map(
        (cell) => cell.textContent?.replace(/\s+/g, " ").trim()
      )
    );
    // Each row says which Stage, which campaign it is in, and the order it
    // chose: enough to find it and to know what changing the setting will
    // leave alone.
    expect(rows).toEqual([
      ["duel", "The March", "All of one side, then all of the other, in any order you pick"],
      ["siege", "The March", "Sides take turns, you pick who acts"]
    ]);
    app.unmount();
  });

  it("keeps the testing aid off the list of choices about the game", () => {
    const { app, host } = mount(createSourceProject());
    // The rules form holds only rules. The testing aid is not among them, and
    // it is not a value of the loss rule either, and an author scanning either
    // control must not be able to find it there.
    const rules = host.querySelector<HTMLElement>(
      ".game-settings > .schema-form"
    )!;
    expect(rules.querySelector("#field-invulnerableForTesting")).toBeNull();
    const loss = host.querySelector<HTMLSelectElement>("#field-characterLoss")!;
    expect([...loss.options].map((option) => option.value)).toEqual([
      "",
      "permanent",
      "recoverable"
    ]);
    expect([...loss.options].map((option) => option.textContent?.trim()))
      .toEqual([
        "Not set",
        "A character who falls is dead for good",
        "A character who falls is carried off, and rejoins the company after " +
          "the Stage"
      ]);

    // It is on the page, behind its own closed lid, and it says the two things
    // an author cannot guess: that it is for debugging, and that it travels in
    // the file anyway. Both are on the control, because a checkbox is read and
    // a paragraph standing over one is not.
    const testing = host.querySelector<HTMLDetailsElement>(
      "details.testing-aids"
    )!;
    expect(testing).not.toBeNull();
    // Closed: a beginner reading down this column meets the season and stops,
    // rather than meeting a switch that makes their player immortal.
    expect(testing.hasAttribute("open")).toBe(false);
    expect(testing.querySelector("summary")?.textContent?.trim())
      .toBe("Testing");
    expect(testing.querySelector("#field-invulnerableForTesting")).not.toBeNull();
    expect(testing.textContent).toContain("for debugging purposes");
    expect(testing.textContent).toContain("written into the file you export");
    app.unmount();
  });

  it("draws the skip control as pending, and refuses to pretend otherwise", () => {
    // A control for a behaviour nothing implements yet. It is drawn so an
    // author looking for a way to skip a map learns here that there is not one
    // yet, rather than hunting the page for a setting that was never written.
    // What it must never do is look usable.
    const { app, host } = mount(createSourceProject());
    const skip = host.querySelector<HTMLInputElement>("#skip-to-next-map")!;
    expect(skip).not.toBeNull();
    expect(skip.disabled).toBe(true);
    expect(skip.checked).toBe(false);
    expect(skip.closest("label")?.textContent?.trim())
      .toContain("Player can directly skip to next map");
    // The note is tied to the control for a screen reader, and it says the
    // word that keeps the control honest.
    const note = host.querySelector<HTMLElement>(
      `#${skip.getAttribute("aria-describedby")}`
    )!;
    expect(note.textContent).toContain("Not implemented");
    expect(note.textContent).toContain("Nothing is stored");
    // It stores nothing, so it stands outside the form that saves and cannot
    // put a field into what is submitted.
    expect(skip.closest("form")).toBeNull();
    app.unmount();
  });

  it("submits the loss rule and the testing aid from their own sections", async () => {
    const { app, host, onSubmit } = mount(createSourceProject());
    const loss = host.querySelector<HTMLSelectElement>("#field-characterLoss")!;
    expect(loss.value).toBe("");
    loss.value = "recoverable";
    loss.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    saveButton(host).click();
    expect(onSubmit.mock.calls[0]?.[0]).toEqual(
      expect.objectContaining({ characterLoss: "recoverable" })
    );

    const testing = host.querySelector<HTMLInputElement>(
      "#field-invulnerableForTesting"
    )!;
    expect(testing.checked).toBe(false);
    testing.checked = true;
    testing.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    saveTestingButton(host).click();
    expect(onSubmit.mock.calls[1]?.[0]).toEqual(
      expect.objectContaining({ invulnerableForTesting: true })
    );

    // And off is absent rather than a stored false, which is what keeps a
    // project that never asked for it identical to one that asked and declined.
    testing.checked = false;
    testing.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    saveTestingButton(host).click();
    const cleared = onSubmit.mock.calls[2]?.[0] as Record<string, unknown>;
    expect(cleared.invulnerableForTesting).toBeUndefined();
    app.unmount();
  });

  it("says plainly when nothing overrides the setting", () => {
    const project = withStages(createSourceProject(), [stage("opening")]);
    const { app, host } = mount(project);
    expect(host.textContent).toContain(
      "No Stage overrides it, so changing it above changes them all."
    );
    expect(host.querySelector(".turn-order-overrides")).toBeNull();
    app.unmount();
  });
});
