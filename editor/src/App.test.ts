// SPDX-License-Identifier: MIT
import { createApp, defineComponent, h, nextTick } from "vue";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import App from "./App.vue";
import EditorErrorBoundary from "./components/EditorErrorBoundary.vue";
import { MemoryProjectStore } from "./domain/memory-project-store";
import {
  createSourceProject,
  SourceProjectDocument,
  sourceProjectPath
} from "./domain/source-project-document";
import type { SourceAnalysis } from "./analysis/source-analysis";
import { RomService, romTargets } from "./platform/rom-service";

// A ROM service that is simply not there, which is what every test that is
// not about ROMs should meet. Without it each mount would ask a real relative
// URL, happy-dom would resolve it against its own origin and try to open a
// socket, and the suite would fill with connection errors it does not care
// about, and would depend on nothing listening on that port.
// One per console, all of them answering as though nothing is listening.
const absentConsoleServices = () => Object.values(romTargets).map(
  (target) => new RomService({
    target,
    fetch: (async () => {
      throw new TypeError("Failed to fetch");
    }) as unknown as typeof globalThis.fetch
  })
);

function mountEditor(props: Record<string, unknown> = {}) {
  const host = document.createElement("div");
  document.body.append(host);
  const app = createApp(App, {
    consoleServices: absentConsoleServices(), ...props
  });
  app.mount(host);
  return { host, app };
}

function commandButton(host: HTMLElement, text: string): HTMLButtonElement {
  const found = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim() === text
  );
  if (!found) throw new Error(`button '${text}' not found`);
  return found;
}

/** An entry of the navigation rail, by the word an author reads on it. */
function railButton(host: HTMLElement, label: string): HTMLButtonElement {
  const rail = host.querySelector<HTMLElement>('nav[aria-label="Project"]');
  if (!rail) throw new Error("project navigation not found");
  const found = [...rail.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim().startsWith(label)
  );
  if (!found) throw new Error(`rail entry '${label}' not found`);
  return found;
}

/** The way in, taken. Every road past the start screen goes through one of
 *  its choices, so the tests take one rather than pretending it is not there. */
async function startNewGame(host: HTMLElement) {
  commandButton(host, "Start a new game").click();
  await nextTick();
}

async function openSection(host: HTMLElement, label: string) {
  railButton(host, label).click();
  await nextTick();
}

/** A collection inside the open section, by the word an author reads on it. */
async function openCollection(host: HTMLElement, label: string) {
  const nav = host.querySelector<HTMLElement>(".content-categories");
  if (!nav) throw new Error("category navigation not found");
  const found = [...nav.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim().startsWith(label)
  );
  if (!found) throw new Error(`collection '${label}' not found`);
  found.click();
  await nextTick();
}

const settle = () => new Promise((resolve) => setTimeout(resolve, 0));

let confirmDialog: ReturnType<typeof vi.fn>;

beforeEach(() => {
  // happy-dom has no confirm dialog; the editor treats its absence as "no".
  confirmDialog = vi.fn(() => true);
  window.confirm = confirmDialog as unknown as typeof window.confirm;
});

afterEach(() => {
  document.body.replaceChildren();
});

describe("editor shell", () => {
  it("exposes semantic navigation and a skip target", async () => {
    const { host, app } = mountEditor();
    expect(host.querySelector('a[href="#workspace"]')?.textContent).toBe(
      "Skip to workspace"
    );
    expect(host.querySelector('nav[aria-label="Project"]')).not.toBeNull();
    expect(host.querySelector("main#workspace")?.getAttribute("tabindex")).toBe("-1");
    await startNewGame(host);
    // Eight destinations, in the order an author meets them. Drawing ground
    // and setting up a fight on it are two questions, so they are two entries
    // and they stand next to each other; what is said around a fight is a
    // third, and Flow is left with the shape of the game and nothing else.
    const rail = host.querySelector<HTMLElement>('nav[aria-label="Project"]')!;
    expect([...rail.querySelectorAll("button")].map(
      (entry) => entry.textContent?.trim().split("\n")[0]?.trim()
    )).toEqual([
      "Game", "Characters", "Weapons & items", "Maps", "Stages", "Scenes",
      "Flow", "Diagnostics"
    ]);
    app.unmount();
  });

  it("opens on the start screen and offers the ways to begin", () => {
    const { host, app } = mountEditor({
      projectStore: new MemoryProjectStore("first-visit")
    });
    const start = host.querySelector("#start")!;
    expect(start).not.toBeNull();
    for (const choice of [
      "Start a new game", "Open this example", "Open a project file"
    ]) {
      expect(commandButton(host, choice)).toBeTruthy();
    }
    // Nothing that acts on an open game, because none is open yet.
    expect([...host.querySelectorAll("button")].map(
      (candidate) => candidate.textContent?.trim()
    )).not.toContain("▶ Play");
    // And no empty record list standing in for a question.
    expect(host.querySelector(".record-list")).toBeNull();
    app.unmount();
  });

  it("keeps the project-lifecycle commands off the header while a game is open",
    async () => {
      const { host, app } = mountEditor({
        projectStore: new MemoryProjectStore("header-verbs")
      });
      await startNewGame(host);
      const header = host.querySelector<HTMLElement>(".project-commands")!;
      expect([...header.querySelectorAll("button")].map(
        (candidate) => candidate.textContent?.trim()
      )).toEqual([
        "▶ Play",
        "Save",
        "Validate",
        "Export",
        "Nintendo 64 ROM",
        "PlayStation disc",
        "Start screen"
      ]);
      app.unmount();
    });

  it("keeps unsaved work and its history across a trip to the start screen",
    async () => {
      const { host, app } = mountEditor({
        projectStore: new MemoryProjectStore("round-trip")
      });
      await startNewGame(host);
      await openSection(host, "Characters");
      await openCollection(host, "Classes");
      commandButton(host, "Create class").click();
      await nextTick();
      expect(host.textContent).toContain("Created New Class");

      commandButton(host, "Start screen").click();
      await nextTick();
      expect(host.querySelector("#start")).not.toBeNull();
      // Going back destroys nothing, so it asks nothing.
      expect(confirmDialog).not.toHaveBeenCalled();

      commandButton(host, "Keep editing").click();
      await nextTick();
      // The record, the section it was made on, and the undo behind it.
      expect(host.textContent).toContain("new_class");
      expect(railButton(host, "Characters").getAttribute("aria-current"))
        .toBe("page");
      expect(commandButton(host, "Undo").disabled).toBe(false);
      app.unmount();
    });

  it("goes straight to the workspace when a stored draft is recovered", async () => {
    const store = new MemoryProjectStore("returning-author");
    await new SourceProjectDocument(store).save({
      ...createSourceProject(),
      title: "Half A Game"
    });
    const { host, app } = mountEditor({ projectStore: store });
    await settle();
    // An author with work in progress asked for the work, not for a menu.
    expect(host.querySelector("#start")).toBeNull();
    expect(host.querySelector<HTMLInputElement>("#field-title")?.value)
      .toBe("Half A Game");
    app.unmount();
  });

  it("makes the editor beneath Play mode inert until it closes", async () => {
    const { host, app } = mountEditor();
    await startNewGame(host);
    commandButton(host, "▶ Play").click();
    await nextTick();
    expect(host.querySelector(".play-mode")).not.toBeNull();
    // Play claims to be a modal dialog, so the editor behind it must be
    // unreachable: not focusable, not clickable, hidden from assistive tech.
    expect(host.querySelector(".app-layout")?.hasAttribute("inert")).toBe(true);
    expect(host.querySelector(".app-header")?.hasAttribute("inert")).toBe(true);
    expect(
      host.querySelector('a[href="#workspace"]')?.hasAttribute("inert")
    ).toBe(true);

    commandButton(host, "← Back to editing").click();
    await nextTick();
    expect(host.querySelector(".play-mode")).toBeNull();
    expect(host.querySelector(".app-layout")?.hasAttribute("inert")).toBe(false);
    app.unmount();
  });

  it("takes the author to every section its rail names", async () => {
    const { host, app } = mountEditor({
      projectStore: new MemoryProjectStore("rail")
    });
    await startNewGame(host);
    const heading = () => host.querySelector("#content-title")?.textContent;
    // An author lands on the game itself, not on a list of characters.
    expect(heading()).toBe("Game");
    expect(railButton(host, "Game").getAttribute("aria-current")).toBe("page");

    for (const label of [
      "Characters", "Weapons & items", "Maps", "Stages", "Flow", "Diagnostics",
      "Game"
    ]) {
      await openSection(host, label);
      expect(heading(), label).toBe(label);
      expect(railButton(host, label).getAttribute("aria-current"), label)
        .toBe("page");
      // Exactly one entry claims to be the current one.
      expect(host.querySelectorAll(
        'nav[aria-label="Project"] button[aria-current="page"]'
      ), label).toHaveLength(1);
    }
    app.unmount();
  });

  // Two tests stood here. Both drove the editor into a state it would not
  // commit by typing malformed JSON into a project's `extensions`, and both
  // then asserted the good behaviour that follows: the rail does not steal a
  // section away from an editor holding unsaved work, and a save that cannot
  // be represented writes nothing at all.
  //
  // No form offers a JSON field any more, the compiler refusing every value of
  // the only two there were, so there is no longer a way to type something a
  // form cannot hold. The behaviour is still implemented and still right; it
  // simply has nothing left to trigger it. Reaching it again needs either a
  // browser test against a schema constraint (jsdom answers `checkValidity()`
  // but never matches `:invalid`) or a new field the form can genuinely fail
  // to parse.

  it("shows the project mark as decoration beside the heading", () => {
    const { host, app } = mountEditor();
    const logo = host.querySelector<HTMLImageElement>("header .app-logo");
    expect(logo).not.toBeNull();
    // Decorative: the heading names the application, the mark repeats it.
    expect(logo?.getAttribute("alt")).toBe("");
    expect(logo?.getAttribute("src")).toBe("/logo.png");
    app.unmount();
  });

  it("creates a schema-backed record from the content workspace", async () => {
    const { host, app } = mountEditor();
    await startNewGame(host);
    await openSection(host, "Characters");
    await openCollection(host, "Classes");
    const create = [...host.querySelectorAll("button")].find(
      (button) => button.textContent?.trim() === "Create class"
    );
    create!.click();
    await nextTick();
    expect(host.textContent).toContain("Created New Class");
    expect(host.textContent).toContain("new_class");
    app.unmount();
  });

  it("tracks structured edits as unsaved project changes", async () => {
    const { host, app } = mountEditor();
    await startNewGame(host);
    await openSection(host, "Characters");
    await openCollection(host, "Classes");
    const create = [...host.querySelectorAll("button")].find(
      (button) => button.textContent?.trim() === "Create class"
    );
    create!.click();
    await nextTick();
    expect(host.querySelector('[aria-label="Project status"]')?.textContent).toContain(
      "Unsaved changes"
    );
    app.unmount();
  });

  it("loads a fresh copy of a bundled sample without a network request", async () => {
    const { host, app } = mountEditor({
      projectStore: new MemoryProjectStore("load-demo")
    });
    await settle();
    const picker = [...host.querySelectorAll<HTMLInputElement>(
      'input[name="start-sample"]'
    )].find((option) => option.value === "demo")!;
    picker.click();
    await nextTick();
    commandButton(host, "Open this example").click();
    await nextTick();

    expect(host.textContent)
      .toContain("The Bridge at Dawn loaded. Save it to keep your changes");
    expect(host.textContent).toContain("Unsaved changes");
    // What the game is called is a game-wide setting, and the game is where an
    // author lands, so it is the first thing they can read.
    expect(host.querySelector<HTMLInputElement>("#field-title")?.value)
      .toBe("The Bridge at Dawn");
    await openSection(host, "Characters");
    expect(host.textContent).toContain("Characters 2");
    expect(host.textContent).toContain("Factions 2");
    app.unmount();
  });

  it("says nothing about a console the loaded game fits, and blocks nothing", async () => {
    const { host, app } = mountEditor({
      projectStore: new MemoryProjectStore("console-notes")
    });
    await settle();
    commandButton(host, "Open this example").click();
    await nextTick();
    await openSection(host, "Diagnostics");

    // The sample is an ordinary two-sided game and is inside every shipped
    // target's measured limits, so the console section is absent rather than
    // reassuring: a note is only ever a thing worth saying.
    expect(host.querySelector(".target-notes")).toBeNull();
    // Nothing about it enters validation, the save status, or the problem
    // count, and every command stays live.
    expect(host.querySelector('#diagnostics [role="status"]')?.textContent)
      .toContain("No problems found");
    expect(host.querySelector('[aria-label="Project status"]')?.textContent)
      .not.toContain("validation found");
    for (const label of ["Save", "Validate", "Export", "▶ Play"]) {
      expect(commandButton(host, label).disabled).toBe(false);
    }
    app.unmount();
  });

  it("saves, recovers, validates, and exports the active project", async () => {
    const store = new MemoryProjectStore("app-lifecycle");
    await new SourceProjectDocument(store).save({
      ...createSourceProject(),
      title: "Recovered Project"
    });
    const analyzeProject = vi.fn(async (): Promise<SourceAnalysis> => ({
      definitions: [],
      diagnostics: [],
      indexDiagnostics: []
    }));
    const downloadArchive = vi.fn();
    const { host, app } = mountEditor({
      projectStore: store,
      analyzeProject,
      downloadArchive
    });
    await settle();
    await nextTick();
    expect(host.querySelector<HTMLInputElement>("#field-title")?.value)
      .toBe("Recovered Project");

    commandButton(host, "Validate").click();
    await settle();
    expect(analyzeProject).toHaveBeenCalledWith(
      "project.json",
      expect.stringContaining("Recovered Project")
    );
    expect(host.textContent).toContain("Validation passed");
    // And said as an answer rather than as four grey words on the end of the
    // save line: its own live region, in the colour the answer has.
    const result = host.querySelector<HTMLElement>(
      '[data-testid="validation-result"]'
    )!;
    expect(result).not.toBeNull();
    expect(result.getAttribute("aria-live")).toBe("polite");
    expect(result.classList.contains("validation-clean")).toBe(true);
    expect(result.textContent).toContain(
      "Validation passed. Nothing is wrong with this game."
    );

    commandButton(host, "Export").click();
    await settle();
    // Named for the game and not for the format: `gameId` is derived from the
    // title, and this project has never been renamed through the settings
    // page, so it still files under what a new game is called.
    expect(downloadArchive).toHaveBeenCalledWith(
      expect.any(Uint8Array),
      "untitled_game.grandleon.zip"
    );
    app.unmount();
  });

  it("asks before a start-screen choice discards unsaved changes", async () => {
    const { host, app } = mountEditor({
      projectStore: new MemoryProjectStore("dirty-guard")
    });
    await settle();
    await startNewGame(host);
    await openSection(host, "Characters");
    await openCollection(host, "Classes");
    commandButton(host, "Create class").click();
    await nextTick();
    expect(host.textContent).toContain("new_class");

    confirmDialog.mockReturnValue(false);
    commandButton(host, "Start screen").click();
    await nextTick();
    commandButton(host, "Start a new game").click();
    await nextTick();
    expect(confirmDialog).toHaveBeenCalled();
    // Refused: still on the start screen, with the work still behind it.
    expect(host.querySelector("#start")).not.toBeNull();
    commandButton(host, "Open this example").click();
    await nextTick();
    expect(host.textContent).not.toContain("Save it to keep your changes");

    commandButton(host, "Keep editing").click();
    await nextTick();
    expect(host.textContent).toContain("new_class");

    confirmDialog.mockReturnValue(true);
    commandButton(host, "Start screen").click();
    await nextTick();
    commandButton(host, "Start a new game").click();
    await nextTick();
    expect(host.textContent).toContain("New project ready");
    expect(host.textContent).not.toContain("new_class");
    app.unmount();
  });

  it("warns before the tab closes over a draft still sitting in a form", async () => {
    const { host, app } = mountEditor({
      projectStore: new MemoryProjectStore("unload-guard")
    });
    await settle();
    await startNewGame(host);
    await openSection(host, "Characters");
    await openCollection(host, "Classes");
    commandButton(host, "Create class").click();
    await nextTick();
    commandButton(host, "Save").click();
    await settle();
    expect(host.textContent).toContain("Saved in this browser");

    const name = host.querySelector<HTMLInputElement>("#field-name")!;
    name.value = "Guardian";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();

    const closing = new Event("beforeunload", { cancelable: true });
    window.dispatchEvent(closing);
    expect(closing.defaultPrevented).toBe(true);
    app.unmount();
  });

  it("saves the text still sitting in an open form", async () => {
    const store = new MemoryProjectStore("flush-on-save");
    const { host, app } = mountEditor({ projectStore: store });
    await settle();
    await startNewGame(host);
    await openSection(host, "Characters");
    await openCollection(host, "Classes");
    commandButton(host, "Create class").click();
    await nextTick();
    const name = host.querySelector<HTMLInputElement>("#field-name")!;
    name.value = "Guardian";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    commandButton(host, "Save").click();
    await settle();

    const stored = await new SourceProjectDocument(store).load();
    if (!stored || "unreadable" in stored || "otherVersion" in stored) {
      throw new Error("expected a saved project");
    }
    expect(stored.project.classes[0]?.name).toBe("Guardian");
    app.unmount();
  });

  it("saves an invalid project but says so immediately", async () => {
    const store = new MemoryProjectStore("validate-on-save");
    const analyzeProject = vi.fn(async (): Promise<SourceAnalysis> => ({
      definitions: [],
      diagnostics: [{
        severity: "error",
        code: "SOURCE_REF_MISSING",
        sourcePath: "project.json",
        instancePath: "/unitTypes/0/classId",
        message: "the class is missing"
      }],
      indexDiagnostics: []
    }));
    const { host, app } = mountEditor({ projectStore: store, analyzeProject });
    await settle();
    await startNewGame(host);
    await openSection(host, "Characters");
    await openCollection(host, "Classes");
    commandButton(host, "Create class").click();
    await nextTick();
    commandButton(host, "Save").click();
    await settle();

    // Still saved: losing work is worse than storing an imperfect draft.
    const stored = await new SourceProjectDocument(store).load();
    if (!stored || "unreadable" in stored || "otherVersion" in stored) {
      throw new Error("expected a saved project");
    }
    expect(stored.project.classes).toHaveLength(1);
    // …but the author hears about the problems now, not at the next reload,
    // in the status line and as a count on the rail.
    expect(host.textContent).toContain("validation found 1 problems");
    expect(railButton(host, "Diagnostics").textContent).toContain("1");
    expect(railButton(host, "Diagnostics").textContent).toContain("problem");
    await openSection(host, "Diagnostics");
    expect(host.textContent).toContain("the class is missing");
    app.unmount();
  });

  it("takes the author to the record a reported problem is about", async () => {
    const store = new MemoryProjectStore("jump-to-problem");
    await new SourceProjectDocument(store).save({
      ...createSourceProject(),
      classes: [{
        id: "wayfarer",
        name: "Wayfarer",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      }],
      unitTypes: [
        { id: "scout", name: "Scout", classId: "wayfarer" },
        { id: "herald", name: "Herald", classId: "wayfarer" }
      ]
    });
    const analyzeProject = vi.fn(async (): Promise<SourceAnalysis> => ({
      definitions: [],
      diagnostics: [{
        severity: "error",
        code: "SOURCE_REF_MISSING",
        sourcePath: "project.json",
        instancePath: "/unitTypes/1/classId",
        message: "this character needs a second look"
      }],
      indexDiagnostics: []
    }));
    const { host, app } = mountEditor({ projectStore: store, analyzeProject });
    await settle();
    commandButton(host, "Validate").click();
    await settle();
    await openSection(host, "Diagnostics");

    const jump = [...host.querySelectorAll("button")].find(
      (candidate) => candidate.textContent?.trim().startsWith("Go to ")
    )!;
    jump.click();
    await nextTick();

    // The section that owns characters, the characters themselves, and the
    // one the problem is about, open for editing.
    expect(railButton(host, "Characters").getAttribute("aria-current"))
      .toBe("page");
    expect(host.querySelector<HTMLInputElement>("#field-name")?.value)
      .toBe("Herald");
    expect(host.textContent).toContain("this character needs a second look");
    app.unmount();
  });

  it("says what it could not find rather than jumping nowhere", async () => {
    const store = new MemoryProjectStore("jump-to-the-game");
    const analyzeProject = vi.fn(async (): Promise<SourceAnalysis> => ({
      definitions: [],
      diagnostics: [{
        severity: "error",
        code: "SOURCE_SCHEMA_INVALID",
        sourcePath: "project.json",
        instancePath: "/title",
        message: "title must be a string"
      }],
      indexDiagnostics: []
    }));
    const { host, app } = mountEditor({ projectStore: store, analyzeProject });
    await settle();
    await startNewGame(host);
    commandButton(host, "Validate").click();
    await settle();
    await openSection(host, "Diagnostics");
    const jump = [...host.querySelectorAll("button")].find(
      (candidate) => candidate.textContent?.trim().startsWith("Go to ")
    )!;
    jump.click();
    await nextTick();

    // A field of the game itself: the game is as close as the path allows,
    // and the workspace says why it went no further.
    expect(railButton(host, "Game").getAttribute("aria-current")).toBe("page");
    expect(host.textContent).toContain(
      "This problem is about the game itself rather than about one record."
    );
    app.unmount();
  });

  it("leaves a second tab a way to save and a way out", async () => {
    // Two tabs on one browser draft. The second one to open holds a revision
    // the first has already moved past, so its save is refused. If Export
    // and the ROM button began by saving, every road out of that tab would be
    // closed and the work could only leave through the clipboard.
    const store = new MemoryProjectStore("two-tabs");
    await new SourceProjectDocument(store).save(createSourceProject());
    const downloadArchive = vi.fn();
    const first = mountEditor({ projectStore: store });
    const second = mountEditor({ projectStore: store, downloadArchive });
    await settle();

    // The other tab writes, moving the revision underneath this one.
    await new SourceProjectDocument(store).save(
      { ...createSourceProject(), title: "Written By The Other Tab" },
      (await store.read(sourceProjectPath))!.revision
    );

    await openSection(second.host, "Characters");
    await openCollection(second.host, "Classes");
    commandButton(second.host, "Create class").click();
    await nextTick();
    commandButton(second.host, "Save").click();
    await settle();
    expect(second.host.textContent).toContain("Another tab or window saved");

    // The way out is open: the archive carries what is on screen, not the
    // copy the other tab left in the store.
    commandButton(second.host, "Export").click();
    await settle();
    expect(downloadArchive).toHaveBeenCalledTimes(1);

    // And a second Save is now a decision the author can actually make.
    commandButton(second.host, "Save").click();
    await settle();
    const stored = await new SourceProjectDocument(store).load();
    if (!stored || "unreadable" in stored || "otherVersion" in stored) {
      throw new Error("expected a saved project");
    }
    expect(stored.project.classes).toHaveLength(1);
    first.app.unmount();
    second.app.unmount();
  });

  it("refuses a game identifier the format cannot hold, at the field",
    async () => {
      // The field is labelled "Game id *" and an author writes the game's name
      // in it, which is a plausible thing to do in a box called Game. The
      // control has carried the schema's own pattern all along; what was
      // missing was anything that ran it. It stands behind the Advanced fold,
      // which changes where an author finds it and not what it refuses.
      const store = new MemoryProjectStore("bad-game-id");
      await new SourceProjectDocument(store).save({
        ...createSourceProject(),
        title: "Nine Months Of Work"
      });
      const { host, app } = mountEditor({ projectStore: store });
      await settle();
      await nextTick();

      const gameId = host.querySelector<HTMLInputElement>("#field-gameId")!;
      gameId.value = "The Tarnholt Line";
      gameId.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
      commandButton(host, "Save game settings").click();
      await nextTick();

      expect(host.querySelector(".schema-form .field-error")?.textContent)
        .toContain("Nothing was saved");
      commandButton(host, "Save").click();
      await settle();

      const stored = await new SourceProjectDocument(store).load();
      if (!stored || "unreadable" in stored || "otherVersion" in stored) {
        throw new Error("expected a project");
      }
      expect(stored.project.title).toBe("Nine Months Of Work");
      // Refused whole: neither the prose nor the title's own derivation
      // reached the stored id, because nothing about that save was committed.
      expect(stored.project.gameId).toBe("untitled_game");
      app.unmount();
    });


  it("plays what is on screen rather than the last committed draft", async () => {
    // Save, Export, the ROM button and every section change commit an open
    // form's pending edits, and Play has to for the same reason: an author who
    // raises a weapon's power and presses Play would otherwise watch a battle
    // fought with the stored number, with nothing on screen saying which of the
    // two they are looking at.
    const { host, app } = mountEditor({
      projectStore: new MemoryProjectStore("play-flush")
    });
    await startNewGame(host);
    await openSection(host, "Game");
    const title = host.querySelector<HTMLInputElement>("#field-title")!;
    title.value = "Edited And Not Committed";
    title.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();

    commandButton(host, "▶ Play").click();
    await nextTick();
    commandButton(host, "← Back to editing").click();
    await nextTick();
    // The start screen names the game that is open, from the committed
    // project, so it can only read the new title if Play committed the form.
    commandButton(host, "Start screen").click();
    await nextTick();

    expect(host.querySelector("#start")?.textContent)
      .toContain("Edited And Not Committed");
    app.unmount();
  });

  it("keeps an unreadable stored draft downloadable and never overwrites it", async () => {
    const store = new MemoryProjectStore("recovery");
    const garbage = new TextEncoder().encode('{"schemaVersion": "1.0.0", "title"');
    await store.write(sourceProjectPath, garbage);
    const downloadArchive = vi.fn();
    const { host, app } = mountEditor({ projectStore: store, downloadArchive });
    await settle();

    const banner = host.querySelector('[role="alert"]');
    expect(banner?.textContent).toContain("could not be opened");
    commandButton(host, "Download the stored draft").click();
    expect(downloadArchive).toHaveBeenCalledWith(
      garbage,
      "grandleon-recovered-draft.json",
      "application/json"
    );

    // Editing the blank workspace must not let Save destroy the draft.
    await openSection(host, "Characters");
    await openCollection(host, "Classes");
    commandButton(host, "Create class").click();
    await nextTick();
    commandButton(host, "Save").click();
    await settle();
    expect(host.textContent).toContain("Saving is paused");
    expect((await store.read(sourceProjectPath))?.bytes).toEqual(garbage);

    // Only an explicit, confirmed setting-aside clears the way, and it copies
    // the bytes somewhere nothing writes first, so the blank project that Save
    // then stores cannot be the last thing left of the author's game.
    commandButton(host, "Set it aside and keep going").click();
    await settle();
    expect(confirmDialog).toHaveBeenCalled();
    commandButton(host, "Save").click();
    await settle();
    const stored = await new SourceProjectDocument(store).load();
    if (!stored || "unreadable" in stored || "otherVersion" in stored) {
      throw new Error("expected a saved project");
    }
    expect(stored.project.classes).toHaveLength(1);
    const rescued = (await store.list("recovered")).map((file) => file.bytes);
    expect(rescued).toEqual([garbage]);
    app.unmount();
  });

  it("takes back the recovered draft it handed out", async () => {
    // The banner's other button downloads a bare project.json. A draft that
    // can be downloaded and never opened again is the author's work in a
    // format only the author can read.
    const store = new MemoryProjectStore("recovery-round-trip");
    const repaired = new TextEncoder().encode(JSON.stringify({
      ...createSourceProject(),
      title: "Repaired By Hand"
    }));
    await store.write(
      sourceProjectPath,
      new TextEncoder().encode('{"schemaVersion": "1.0.0", "title"')
    );
    const { host, app } = mountEditor({ projectStore: store });
    await settle();

    const input = host.querySelector<HTMLInputElement>('input[type="file"]')!;
    expect(input.getAttribute("accept")).toContain(".json");
    Object.defineProperty(input, "files", {
      value: [new File(
        [repaired as BlobPart],
        "grandleon-recovered-draft.json"
      )],
      configurable: true
    });
    input.dispatchEvent(new Event("change"));
    await settle();

    expect(host.textContent).toContain("Opened grandleon-recovered-draft.json");
    // The pause is over, because the thing it was protecting has been set
    // aside and the author is holding a project that opens.
    expect(host.querySelector(".draft-recovery")).toBeNull();
    commandButton(host, "Save").click();
    await settle();
    const stored = await new SourceProjectDocument(store).load();
    if (!stored || "unreadable" in stored || "otherVersion" in stored) {
      throw new Error("expected a saved project");
    }
    expect(stored.project.title).toBe("Repaired By Hand");
    app.unmount();
  });

  it("opens an archive without touching the stored draft until save", async () => {
    const store = new MemoryProjectStore("look-dont-touch");
    await new SourceProjectDocument(store).save({
      ...createSourceProject(),
      title: "Keep Me"
    });
    const { host, app } = mountEditor({ projectStore: store });
    await settle();
    expect(host.textContent).toContain("Recovered local browser draft");

    const visitor = new SourceProjectDocument(new MemoryProjectStore("visitor"));
    await visitor.save({ ...createSourceProject(), title: "Visitor" });
    const visitorProject = { ...createSourceProject(), title: "Visitor" };
    const archive = visitor.exportSnapshot(
      await visitor.store.snapshot(),
      visitorProject
    );
    const input = host.querySelector<HTMLInputElement>('input[type="file"]')!;
    Object.defineProperty(input, "files", {
      value: [new File([archive as BlobPart], "visitor.grandleon.zip")],
      configurable: true
    });
    input.dispatchEvent(new Event("change"));
    await settle();

    expect(host.textContent)
      .toContain("Opened visitor.grandleon.zip. Save it to keep it");
    expect(host.textContent).toContain("Unsaved changes");
    // Looking at an archive is not consenting to overwrite the stored draft.
    let stored = await new SourceProjectDocument(store).load();
    if (!stored || "unreadable" in stored || "otherVersion" in stored) {
      throw new Error("expected a stored project");
    }
    expect(stored.project.title).toBe("Keep Me");

    commandButton(host, "Save").click();
    await settle();
    stored = await new SourceProjectDocument(store).load();
    if (!stored || "unreadable" in stored || "otherVersion" in stored) {
      throw new Error("expected a stored project");
    }
    expect(stored.project.title).toBe("Visitor");
    app.unmount();
  });

  it("replaces a failed workspace child with an actionable error", async () => {
    const BrokenWorkspace = defineComponent({
      setup() {
        throw new Error("fixture workspace failed");
      },
      template: "<div />"
    });
    const host = document.createElement("div");
    document.body.append(host);
    const app = createApp({
      render: () => h(
        EditorErrorBoundary,
        null,
        { default: () => h(BrokenWorkspace) }
      )
    });
    app.config.errorHandler = () => {};
    app.mount(host);
    await nextTick();
    expect(host.querySelector('[role="alert"]')?.textContent).toContain(
      "fixture workspace failed"
    );
    expect(host.querySelector("button")?.textContent).toBe("Try workspace again");
    app.unmount();
  });
  it("offers every console disabled, with a reason, when no service is running",
    async () => {
      // The state the browser gate runs in, and the one an author who has
      // never started the service will meet: the control is present and says
      // why it cannot be used, rather than being absent or failing on a press.
      const { host, app } = mountEditor({
        projectStore: new MemoryProjectStore("rom-unavailable")
      });
      await startNewGame(host);
      await nextTick();
      await nextTick();

      // Both consoles, not just the first: a second console that quietly had
      // no button would look exactly like a service that was not running.
      expect(Object.keys(romTargets)).toHaveLength(2);
      for (const target of Object.values(romTargets)) {
        const button = host.querySelector<HTMLButtonElement>(
          `[data-testid="build-${target.route}"]`
        );
        expect(button, `no button for ${target.platform}`).not.toBeNull();
        expect(button!.disabled).toBe(true);
        expect(button!.textContent).toContain(target.platform);
        expect(button!.title).toMatch(/serve\.mjs/);
        expect(
          host.querySelector(
            `[data-testid="build-status-${target.route}"]`
          )?.textContent
        ).toMatch(/not running/);
      }
      app.unmount();
    });

  it("hands the built ROM to the download surface with a .z64 name", async () => {
    const bytes = new Uint8Array([0x80, 0x37, 0x12, 0x40]);
    const romService = {
      target: romTargets.nintendo64,
      health: async () => ({ ready: true }),
      build: async (
        _json: string,
        onProgress: (status: unknown) => void
      ) => {
        // Two phases, a render apart, so the surface is observed in each of
        // them rather than only in whichever one happened to be last.
        onProgress({ state: "queued", position: 2 });
        await nextTick();
        onProgress({ state: "building", position: 0 });
        await nextTick();
        return {
          files: [{ name: "a-game.z64", bytes }],
          status: {
            campaign: "a_campaign",
            artifacts: [{ name: "a-game.z64", bytes: 4, md5: "ab" }]
          }
        };
      }
    } as unknown as RomService;
    const downloaded: Array<[Uint8Array, string, string | undefined]> = [];
    const { host, app } = mountEditor({
      projectStore: new MemoryProjectStore("rom-download"),
      consoleServices: [romService],
      downloadArchive: (
        data: Uint8Array, filename: string, contentType?: string
      ) => { downloaded.push([data, filename, contentType]); }
    });
    await startNewGame(host);
    await nextTick();
    await nextTick();

    const button = host.querySelector<HTMLButtonElement>(
      '[data-testid="build-n64"]'
    )!;
    expect(button.disabled).toBe(false);
    const progress: string[] = [];
    button.click();
    // The press saves first and then builds, so several promise turns pass
    // before the bytes arrive. Wait for the surface to say it is ready rather
    // than counting ticks.
    for (let turn = 0; turn < 30 && downloaded.length === 0; turn += 1) {
      await nextTick();
      const said = host.querySelector(
        '[data-testid="build-status-n64"]'
      )?.textContent;
      if (said) progress.push(said.trim());
    }

    expect(downloaded).toHaveLength(1);
    expect(downloaded[0]![0]).toEqual(bytes);
    // A cartridge image, named for the game and not for an archive.
    expect(downloaded[0]![1]).toMatch(/\.z64$/);
    expect(downloaded[0]![2]).toBe("application/octet-stream");
    // What the wait said. The duration is the measured one: a cold container
    // build was 1 m 55 s and 1 m 59 s, so "about a minute" was the warm number
    // told to somebody who has the cold one. Every line carries a clock,
    // because the one thing an author cannot tell from outside is slow from
    // stuck.
    const said = progress.join(" ");
    expect(said).not.toContain("about a minute");
    expect(said).toContain("Waiting behind 2 other builds.");
    expect(said).toContain(
      "Building the Nintendo 64 ROM. The first one takes about two minutes; " +
      "later ones are quicker."
    );
    expect(said).toMatch(/\d+:\d\d so far\./);
    app.unmount();
  });
});
