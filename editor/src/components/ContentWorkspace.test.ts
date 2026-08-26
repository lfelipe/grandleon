// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import { createSourceProject } from "../domain/source-project-document";
import ContentWorkspace from "./ContentWorkspace.vue";

afterEach(() => document.body.replaceChildren());

// The navigation between sections lives beside this workspace rather than
// inside it, so these tests move between sections the way the rail does: through
// the selector the workspace exposes, which is the one the rail calls. The rail
// itself is tested in App.test.ts, where it lives: its marking, and its
// refusal to claim a section the workspace would not leave.
interface WorkspaceHandle {
  selectSection(id: string): void;
}

function mount(initialProject?: SourceProject, initialSection = "characters") {
  const host = document.createElement("div");
  document.body.append(host);
  const onDirty = vi.fn();
  const onChange = vi.fn();
  const onSection = vi.fn();
  const app = createApp(ContentWorkspace, {
    onDirty,
    onChange,
    onSection,
    initialSection,
    ...(initialProject ? { initialProject } : {})
  });
  const workspace = app.mount(host) as unknown as WorkspaceHandle;
  return { app, host, onDirty, onChange, onSection, workspace };
}

function button(host: HTMLElement, text: string): HTMLButtonElement {
  const found = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim().startsWith(text)
  );
  if (!found) throw new Error(`button '${text}' not found`);
  return found;
}

// Sections and collections may share a label (both say "Characters"), so
// collection clicks are scoped to the category navigation.
function categoryButton(host: HTMLElement, text: string): HTMLButtonElement {
  const nav = host.querySelector<HTMLElement>(".content-categories");
  if (!nav) throw new Error("category navigation not found");
  return button(nav, text);
}

/**
 * The button that makes a new record of whatever collection is open.
 *
 * It is found by position rather than by its words, because its words are the
 * collection's own: "Create map" on Maps, "Create character" on Characters.
 * The verb itself is pinned in one test below, which is where a wrong word
 * should fail rather than in every test that happens to press it.
 */
function createButton(host: HTMLElement): HTMLButtonElement {
  return button(recordList(host), "Create ");
}

function recordList(host: HTMLElement): HTMLElement {
  const list = host.querySelector<HTMLElement>(".record-list");
  if (!list) throw new Error("record list not found");
  return list;
}

// A shelf stands with the collection it fills, so at most one is on screen at
// a time; it is still looked up by name, because a shelf that moved sections
// should fail loudly rather than silently match something else.
function shelfPanel(host: HTMLElement, titleId: string): HTMLElement {
  const panel = host.querySelector<HTMLElement>(
    `section[aria-labelledby="${titleId}"]`
  );
  if (!panel) throw new Error(`shelf '${titleId}' not found`);
  return panel;
}

function weaponShelfPanel(host: HTMLElement): HTMLElement {
  return shelfPanel(host, "add-weapon-title");
}

function abilityShelfPanel(host: HTMLElement): HTMLElement {
  return shelfPanel(host, "add-ability-title");
}

/** The abilities shelf stands with the abilities, so a test wanting it says so. */
async function openAbilities(host: HTMLElement) {
  categoryButton(host, "Abilities").click();
  await nextTick();
}

/** The class list is one of four in the Characters section, and not the one
 *  the section opens on. */
async function openClasses(host: HTMLElement) {
  categoryButton(host, "Classes").click();
  await nextTick();
}

/**
 * Makes a character the way an author does: the wizard, all three steps.
 *
 * Nothing exists until the final press, so a test that only wants a character
 * in the project pays exactly the presses an author pays and no fixture.
 */
async function makeCharacter(
  host: HTMLElement,
  name: string,
  options: { recipe?: string; side?: string; setting?: string } = {}
) {
  button(host, "New character").click();
  await nextTick();
  const side = host.querySelector<HTMLInputElement>(
    `input[name="character-wizard-side"][value="${options.side ?? ""}"]`
  )!;
  side.click();
  await nextTick();
  button(host, "Next").click();
  await nextTick();
  if (options.setting) {
    button(host, options.setting).click();
    await nextTick();
  }
  if (options.recipe) {
    host.querySelector<HTMLElement>(`[data-recipe="${options.recipe}"]`)!.click();
    await nextTick();
  }
  button(host, "Next").click();
  await nextTick();
  const field = host.querySelector<HTMLInputElement>("#character-wizard-name")!;
  field.value = name;
  field.dispatchEvent(new Event("input"));
  await nextTick();
  button(host, "Make them").click();
  await nextTick();
}

describe("ContentWorkspace", () => {
  it("names the create button after the thing it creates", async () => {
    // "Record" is the word twelve collections have in common, which makes it
    // the word for none of them: an author standing on Maps wanting a map does
    // not recognise it. Every collection the workspace can open is listed here,
    // because one collection falling back to the generic word is the whole
    // defect and eleven out of twelve is not a fix.
    const { app, host, workspace } = mount(undefined, "characters");
    const expected: readonly (readonly [string, string, string])[] = [
      ["characters", "Classes", "Create class"],
      ["characters", "Characters", "Create character"],
      ["characters", "Factions", "Create faction"],
      ["characters", "Abilities", "Create ability"],
      ["equipment", "Weapon types", "Create weapon type"],
      ["equipment", "Weapons", "Create weapon"],
      ["equipment", "Item types", "Create item type"],
      ["equipment", "Items", "Create item"],
      ["maps", "Maps", "Create map"],
      ["scenes", "Scenes", "Create scene"]
    ];
    for (const [section, category, verb] of expected) {
      workspace.selectSection(section);
      await nextTick();
      // Maps owns one collection, so it draws no category strip to click.
      if (host.querySelector(".content-categories")) {
        categoryButton(host, category).click();
        await nextTick();
      }
      expect(createButton(host).textContent?.trim()).toBe(verb);
    }
    app.unmount();
  });

  it("creates, edits, and undoes a schema-backed record", async () => {
    const { app, host, onDirty } = mount();
    await openClasses(host);
    createButton(host).click();
    await nextTick();
    const name = host.querySelector<HTMLInputElement>("#field-name")!;
    name.value = "Guardian";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    button(host, "Save New Class").click();
    await nextTick();
    expect(host.textContent).toContain("Saved Guardian");
    button(host, "Undo").click();
    await nextTick();
    expect(host.textContent).toContain("New Class");
    expect(onDirty).toHaveBeenCalled();
    app.unmount();
  });

  it("removes an optional field that the form turned off", async () => {
    const { app, host, onChange } = mount();
    await openClasses(host);
    createButton(host).click();
    await nextTick();
    const toggle = host.querySelector<HTMLInputElement>(
      "#field-actsAfterAttacking"
    )!;
    toggle.click();
    await nextTick();
    button(host, "Save New Class").click();
    await nextTick();
    let project = onChange.mock.calls.at(-1)?.[0] as SourceProject;
    expect(project.classes[0]?.actsAfterAttacking).toBe(true);

    toggle.click();
    await nextTick();
    button(host, "Save New Class").click();
    await nextTick();
    // Object.assign alone cannot delete; the workspace must drop the field.
    project = onChange.mock.calls.at(-1)?.[0] as SourceProject;
    expect("actsAfterAttacking" in project.classes[0]!).toBe(false);
    app.unmount();
  });

  it("leads Characters with the roster and keeps the record columns behind a "
    + "door that opens itself", async () => {
    // The wizard is the way in; the record columns are the way back to one
    // character out of forty. Folded, never removed, and a press that asks
    // for a record opens the fold, because a card that visibly did nothing
    // would be worse than no card.
    const { app, host, workspace } = mount({
      ...createSourceProject(),
      classes: [{
        id: "guard",
        name: "Guard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      }],
      unitTypes: [{ id: "rina", name: "Rina", classId: "guard" }]
    });
    workspace.selectSection("characters");
    await nextTick();
    const fold = host.querySelector<HTMLDetailsElement>("details.records-fold")!;
    expect(fold).not.toBeNull();
    expect(fold.hasAttribute("open")).toBe(false);
    // Everything the browser holds is inside it, and reachable.
    expect(fold.querySelector(".content-categories")).not.toBeNull();
    expect(fold.querySelector("#record-search")).not.toBeNull();
    expect(fold.querySelector(".record-editor")).not.toBeNull();
    // What leads the page is the roster and its one green button.
    const roster = host.querySelector<HTMLElement>(".character-roster")!;
    expect(roster).not.toBeNull();
    expect(roster.compareDocumentPosition(fold) &
      Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();

    host.querySelector<HTMLButtonElement>(".character-card")!.click();
    await nextTick();
    expect(fold.hasAttribute("open")).toBe(true);
    expect(host.querySelector<HTMLInputElement>("#field-name")).not.toBeNull();

    // Leaving and coming back lands on the front of the page again.
    workspace.selectSection("maps");
    await nextTick();
    workspace.selectSection("characters");
    await nextTick();
    expect(
      host.querySelector<HTMLDetailsElement>("details.records-fold")!
        .hasAttribute("open")
    ).toBe(false);
    app.unmount();
  });

  it("keeps every other section's record columns in front of the author",
    async () => {
      // Only Characters has something friendlier to lead with. On Maps the
      // record browser is the friendly surface, and a fold there would be a
      // door in front of the only way to draw ground.
      const { app, host, workspace } = mount();
      for (const section of ["maps", "scenes", "equipment"]) {
        workspace.selectSection(section);
        await nextTick();
        expect(host.querySelector("details.records-fold"), section).toBeNull();
        expect(host.querySelector(".content-layout"), section).not.toBeNull();
      }
      app.unmount();
    });

  it("files a new record under its own name, in the singular", async () => {
    // The identifier comes from what the record is called, never from the
    // collection it lives in. A collection name is plural, so "New Map" was
    // filed as `new_maps` and the two disagreed about how many of it there is.
    const { app, host, workspace } = mount();
    for (const [section, category, expected] of [
      ["characters", "Characters", "new_character"],
      ["characters", "Classes", "new_class"],
      ["characters", "Factions", "new_faction"],
      ["maps", "Maps", "new_map"],
      ["scenes", "Scenes", "new_scene"]
    ] as const) {
      workspace.selectSection(section);
      await nextTick();
      const nav = host.querySelector<HTMLElement>(".content-categories");
      if (nav) {
        [...nav.querySelectorAll("button")]
          .find((entry) => entry.textContent?.trim().startsWith(category))!
          .click();
        await nextTick();
      }
      createButton(host).click();
      await nextTick();
      const list = host.querySelector<HTMLElement>(".record-list")!;
      expect(list.textContent, category).toContain(expected);
    }
    app.unmount();
  });

  it("previews stable-identifier changes before applying them", async () => {
    const { app, host } = mount();
    await openClasses(host);
    createButton(host).click();
    await nextTick();
    const rename = host.querySelector<HTMLInputElement>("#rename-id")!;
    rename.value = "guardian";
    rename.dispatchEvent(new Event("input", { bubbles: true }));
    button(host, "Preview rename").click();
    await nextTick();
    expect(host.textContent).toContain("/classes/0/id");
    expect(host.textContent).toContain("new_class");
    button(host, "Confirm atomic rename").click();
    await nextTick();
    expect(host.textContent).toContain("guardian");
    expect(host.textContent).toContain("Renamed new_class to guardian");
    app.unmount();
  });

  it("refuses to rename a record to an identifier the format cannot hold",
    async () => {
      // The gesture: an author renames Dawn Knight to "My Best Knight!!".
      // A rename rewrites every reference across the whole project, so what
      // this refusal prevents is not one bad field: it is a game that no
      // longer opens, spread over a dozen records. The `pattern` attribute on
      // the control cannot do it: the input is in no form and neither button
      // beside it submits one.
      const { app, host, onChange } = mount();
      await openClasses(host);
      createButton(host).click();
      await nextTick();

      const rename = host.querySelector<HTMLInputElement>("#rename-id")!;
      rename.value = "My Best Knight!!";
      rename.dispatchEvent(new Event("input", { bubbles: true }));
      button(host, "Preview rename").click();
      await nextTick();

      expect(host.textContent).toContain("is not an identifier this format can hold");
      expect(host.querySelector(".rename-record ul")).toBeNull();
      const project = onChange.mock.calls.at(-1)?.[0] as SourceProject;
      expect(project.classes[0]?.id).toBe("new_class");
      app.unmount();
    });

  it("refuses a bad identifier confirmed after a good one was previewed",
    async () => {
      // The preview list stays on screen while the field stays editable, so
      // "previewed" is not "confirmed": the identifier being applied has to be
      // judged again at the moment it is applied.
      const { app, host, onChange } = mount();
      await openClasses(host);
      createButton(host).click();
      await nextTick();

      const rename = host.querySelector<HTMLInputElement>("#rename-id")!;
      rename.value = "guardian";
      rename.dispatchEvent(new Event("input", { bubbles: true }));
      button(host, "Preview rename").click();
      await nextTick();
      rename.value = "Guardian Of The Gate";
      rename.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
      button(host, "Confirm atomic rename").click();
      await nextTick();

      expect(host.textContent).toContain("Nothing was renamed");
      const project = onChange.mock.calls.at(-1)?.[0] as SourceProject;
      expect(project.classes[0]?.id).toBe("new_class");
      app.unmount();
    });

  it("switches categories and filters without rendering unrelated records", async () => {
    const { app, host, workspace } = mount();
    // Categories live under task-oriented sections now.
    workspace.selectSection("equipment");
    await nextTick();
    button(host, "Weapon types").click();
    await nextTick();
    createButton(host).click();
    await nextTick();
    const search = host.querySelector<HTMLInputElement>("#record-search")!;
    search.value = "missing";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(host.textContent).toContain("0 matching records");
    search.value = "weapon";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(host.textContent).toContain("1 matching record");
    app.unmount();
  });

  it("creates related definitions from a typed reference field", async () => {
    const { app, host } = mount();
    categoryButton(host, "Characters").click();
    await nextTick();
    createButton(host).click();
    await nextTick();
    button(host, "Create related class").click();
    await nextTick();
    expect(host.textContent).toContain("Created related class 'new_class'");
    button(host, "Classes").click();
    await nextTick();
    expect(host.textContent).toContain("New Class");
    app.unmount();
  });

  it("keeps a 10,000-record library bounded and searchable", async () => {
    const largeProject: SourceProject = {
      schemaVersion: "1.2.0",
      packageId: "d05f4dc5-592f-4c6a-9093-f4090a722ffc",
      gameId: "large.fixture",
      title: "Large Fixture",
      contentRevision: "1.0.0",
      classes: [],
      unitTypes: [],
      weapons: [],
      items: Array.from({ length: 10_000 }, (_, index) => ({
        id: `item_${String(index).padStart(5, "0")}`,
        name: `Item ${index}`,
        stackLimit: 1
      })),
      maps: []
    };
    const { app, host, workspace } = mount(largeProject);
    workspace.selectSection("equipment");
    await nextTick();
    button(host, "Items").click();
    await nextTick();
    expect(host.querySelectorAll(".record-list li")).toHaveLength(100);
    expect(host.textContent).toContain("Page 1 of 100");

    const search = host.querySelector<HTMLInputElement>("#record-search")!;
    search.value = "item_09999";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(host.querySelectorAll(".record-list li")).toHaveLength(1);
    expect(host.textContent).toContain("Item 9999");
    app.unmount();
  });

  it("edits a scene's lines as a list rather than raw JSON", async () => {
    const { app, host, workspace } = mount();
    workspace.selectSection("scenes");
    await nextTick();
    createButton(host).click();
    await nextTick();

    // The raw JSON textarea for `lines` is gone; the list editor stands in.
    expect(host.querySelector("#field-lines")).toBeNull();
    expect(host.textContent).toContain("What is said");

    button(host, "Add a line").click();
    await nextTick();
    expect(host.textContent).toContain("Saved scene lines");
    const speaker = host.querySelector<HTMLInputElement>(
      "#dialogue-line-0-speaker"
    )!;
    expect(speaker.value).toBe("Narrator");
    speaker.value = "Mirea";
    speaker.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();

    button(host, "Preview").click();
    await nextTick();
    expect(host.querySelector(".dialogue-preview-stage")?.textContent)
      .toContain("Mirea");

    // The list editor writes through the same undoable session as any edit.
    button(host, "Undo").click();
    await nextTick();
    expect(host.textContent).toContain("Undid: Edit scene");
    app.unmount();
  });

  it("creates a scene inline from the campaign editor without losing unsaved flow edits", async () => {
    const project: SourceProject = {
      schemaVersion: "1.2.0",
      packageId: "d05f4dc5-592f-4c6a-9093-f4090a722ffc",
      gameId: "cutscene.fixture",
      title: "Cutscene Fixture",
      contentRevision: "1.0.0",
      classes: [{
        id: "guard",
        name: "Guard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      }],
      unitTypes: [{ id: "wren", name: "Wren", classId: "guard" }],
      weapons: [],
      items: [],
      maps: [],
      campaigns: [{
        id: "main",
        name: "Main",
        // A campaign that is played and kept marches out with somebody, and
        // the flow editor refuses to save one that does not.
        roster: [{ id: "wren", name: "Wren", unitTypeId: "wren" }],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "opening",
          nodes: [
            {
              id: "opening",
              name: "Opening",
              kind: "story",
              transitions: [{ id: "finish", targetNodeId: "end", priority: 0 }]
            },
            { id: "end", name: "End", kind: "terminal", transitions: [] }
          ]
        }
      }],
      dialogues: []
    };
    const { app, host, onChange, workspace } = mount(project);
    // Flow opens on the campaign this project already has: no category to
    // pick and no record to select first.
    workspace.selectSection("flow");
    await nextTick();

    // An unsaved flow edit that a project refresh must not throw away. Scoped
    // to the words-and-forms half of the page, because the graph above it
    // draws a stop of the same name and pressing that one selects rather
    // than opens.
    button(host.querySelector<HTMLElement>(".campaign-flow")!, "Opening").click();
    await nextTick();
    const nodeName = host.querySelector<HTMLInputElement>("#campaign-node-name")!;
    nodeName.value = "The Approach";
    nodeName.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();

    const name = host.querySelector<HTMLInputElement>("#cutscene-new-name")!;
    name.value = "The Gates Open";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    button(host, "Write a new scene").click();
    await nextTick();

    // The scene is real shared content immediately…
    expect(host.textContent).toContain("Saved scenes");
    const afterScene = onChange.mock.lastCall?.[0] as SourceProject;
    expect(afterScene.dialogues).toEqual([
      { id: "the_gates_open", name: "The Gates Open" }
    ]);
    // …and the refresh it caused did not clobber the unsaved flow draft.
    expect(host.querySelector<HTMLInputElement>("#campaign-node-name")?.value)
      .toBe("The Approach");
    expect(host.querySelector(".cutscene-list")?.textContent)
      .toContain("The Gates Open");

    button(host, "Save the order of events").click();
    await nextTick();
    const saved = onChange.mock.lastCall?.[0] as SourceProject;
    const opening = saved.campaigns?.[0]?.flow?.nodes.find(
      (node) => node.id === "opening"
    );
    expect(opening?.dialogueIds).toEqual(["the_gates_open"]);
    expect(opening?.name).toBe("The Approach");
    app.unmount();
  });

  it("starts a new campaign with somebody to march out with", async () => {
    const { app, host, onChange, workspace } = mount();
    await makeCharacter(host, "Wren");

    // No press at all: arriving at Flow is what makes the campaign, because a
    // campaign is a record the format needs and not a decision the author came
    // to make.
    workspace.selectSection("flow");
    await nextTick();

    // Not an empty form and a rule met later as a refusal: the company starts
    // with the one character the project has.
    const project = onChange.mock.lastCall?.[0] as SourceProject;
    const character = project.unitTypes[0]!;
    expect(project.campaigns?.[0]?.roster).toEqual([
      { id: "member", name: character.name, unitTypeId: character.id }
    ]);
    app.unmount();
  });

  it("edits a campaign's company as a list rather than raw JSON", async () => {
    const { app, host, onChange, workspace } = mount();
    await makeCharacter(host, "Wren");
    workspace.selectSection("flow");
    await nextTick();

    // The raw JSON textarea for `roster` is gone; the list editor stands in.
    expect(host.querySelector("#field-roster")).toBeNull();
    expect(host.textContent).toContain("The company this campaign starts with");

    const memberName = host.querySelector<HTMLInputElement>("#roster-0-name")!;
    memberName.value = "Wren of Tarnholt";
    memberName.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    expect(host.textContent).toContain("Saved the campaign's company");
    expect((onChange.mock.lastCall?.[0] as SourceProject).campaigns?.[0]?.roster)
      .toEqual([expect.objectContaining({ name: "Wren of Tarnholt" })]);

    button(host, "Add member").click();
    await nextTick();
    let roster = (onChange.mock.lastCall?.[0] as SourceProject)
      .campaigns?.[0]?.roster;
    // Two members may be the same kind of character and are still two people.
    expect(roster).toHaveLength(2);
    expect(roster?.[1]?.id).toBe("member_2");

    button(host, "Remove member 2").click();
    await nextTick();
    roster = (onChange.mock.lastCall?.[0] as SourceProject).campaigns?.[0]?.roster;
    expect(roster).toHaveLength(1);

    // Every one of those edits went through the same undoable session as any
    // other record edit.
    button(host, "Undo").click();
    await nextTick();
    expect(host.textContent).toContain("Undid: Edit campaign");
    app.unmount();
  });

  it("catches a founder taking the identity of somebody who joins later", async () => {
    // The founding company and a node's recruits are edited on two screens and
    // are one namespace, so the clash has to be reported on whichever screen
    // the author is standing on.
    const source: SourceProject = {
      ...createSourceProject(),
      classes: [{
        id: "wayfarer",
        name: "Wayfarer",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      }],
      unitTypes: [{ id: "scout", name: "Scout", classId: "wayfarer" }],
      campaigns: [{
        id: "road",
        name: "The Road",
        roster: [{ id: "lead", name: "Lead", unitTypeId: "scout" }],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "opening",
          nodes: [{
            id: "opening",
            name: "Opening",
            kind: "story",
            recruits: [{ id: "ferryman", name: "Ferryman", unitTypeId: "scout" }],
            transitions: [{ id: "onward", targetNodeId: "opening", priority: 0 }]
          }]
        }
      }]
    };
    const { app, host, workspace } = mount(source);
    workspace.selectSection("flow");
    await nextTick();

    const identifier = host.querySelector<HTMLInputElement>("#roster-0-id")!;
    identifier.value = "ferryman";
    identifier.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    // Said beside the founder, on the screen the founder is edited on. The
    // recruit list on the flow editor says its own half of the same clash, so
    // the assertion is scoped to the company rather than to the page.
    const company = host.querySelector<HTMLElement>(
      'section[aria-labelledby="roster-title"]'
    )!;
    expect(company.textContent).toContain(
      "Somebody else in this campaign is already 'ferryman'"
    );
    app.unmount();
  });

  it("explains an empty company and offers the character it needs", async () => {
    const { app, host, workspace } = mount();
    workspace.selectSection("flow");
    await nextTick();

    // Nothing to be a member of, and it says which edit fixes that rather
    // than inventing a member out of nothing.
    expect(host.textContent).toContain("No characters yet");
    expect(host.textContent).toContain("No members yet.");
    button(host, "Create related character").click();
    await nextTick();
    expect(host.textContent).toContain("Created related character 'new_character'");
    app.unmount();
  });

  it("stocks a campaign's store as a list rather than raw JSON", async () => {
    const source: SourceProject = {
      ...createSourceProject(),
      items: [
        { id: "tonic", name: "Tonic", stackLimit: 9 },
        { id: "torch", name: "Torch", stackLimit: 9 }
      ],
      campaigns: [{ id: "road", name: "The Road" }]
    };
    const { app, host, onChange, workspace } = mount(source);
    workspace.selectSection("flow");
    await nextTick();

    // The raw JSON textarea for `startingStore` is gone; the list stands in.
    expect(host.querySelector("#field-startingStore")).toBeNull();
    expect(host.textContent).toContain("What the company's store starts with");
    // A store that says nothing is not a store holding nothing said badly:
    // the list is empty and says so.
    expect(host.textContent).toContain("Nothing here yet.");

    button(host, "Add starting stock").click();
    await nextTick();
    expect(host.textContent).toContain("Saved the campaign's starting store");
    // Not an empty form: the first item in the project, one of it.
    expect((onChange.mock.lastCall?.[0] as SourceProject).campaigns?.[0]
      ?.startingStore).toEqual([{ itemId: "tonic", quantity: 1 }]);

    // The reference picker offers the item category and nothing else, and a
    // search narrows it without ever hiding the choice already made.
    const chooser = host.querySelector<HTMLSelectElement>(
      "#starting-store-0-item"
    )!;
    // The value carries the kind as well as the identity, a store holding
    // weapons as well as items and an identity being unique only within one.
    expect([...chooser.options].map((option) => option.value))
      .toEqual(["item:tonic", "item:torch"]);
    const search = host.querySelector<HTMLInputElement>(
      "#starting-store-item-search"
    )!;
    search.value = "torch";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect([...chooser.options].map((option) => option.value))
      .toEqual(["item:tonic", "item:torch"]);

    chooser.value = "item:torch";
    chooser.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    const quantity = host.querySelector<HTMLInputElement>(
      "#starting-store-0-quantity"
    )!;
    quantity.value = "3";
    quantity.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    expect((onChange.mock.lastCall?.[0] as SourceProject).campaigns?.[0]
      ?.startingStore).toEqual([{ itemId: "torch", quantity: 3 }]);

    // Every one of those edits went through the same undoable session as any
    // other record edit, and comes back the same way.
    button(host, "Undo").click();
    await nextTick();
    expect((onChange.mock.lastCall?.[0] as SourceProject).campaigns?.[0]
      ?.startingStore).toEqual([{ itemId: "torch", quantity: 1 }]);
    button(host, "Redo").click();
    await nextTick();
    expect((onChange.mock.lastCall?.[0] as SourceProject).campaigns?.[0]
      ?.startingStore).toEqual([{ itemId: "torch", quantity: 3 }]);

    button(host, "Remove starting stock 1").click();
    await nextTick();
    // Omitted and empty say the same thing about a store, and the record is
    // written the way every campaign before this wrote it.
    expect((onChange.mock.lastCall?.[0] as SourceProject).campaigns?.[0])
      .not.toHaveProperty("startingStore");
    app.unmount();
  });

  it("explains a store with nothing to stock, and every way one is wrong", async () => {
    const source: SourceProject = {
      ...createSourceProject(),
      campaigns: [{ id: "road", name: "The Road" }]
    };
    const { app, host, workspace } = mount(source);
    workspace.selectSection("flow");
    await nextTick();

    // Nothing to give, and it says which edit fixes that rather than
    // inventing a grant of nothing.
    const store = host.querySelector<HTMLElement>(
      'section[aria-labelledby="starting-store-title"]'
    )!;
    expect(store.textContent).toContain("no items or weapons yet");
    button(store, "Create related item").click();
    await nextTick();
    expect(host.textContent).toContain("Created related item");
    app.unmount();
  });

  it("names a stocked item this project does not hold, and a quantity that is not one", async () => {
    const source: SourceProject = {
      ...createSourceProject(),
      items: [{ id: "tonic", name: "Tonic", stackLimit: 9 }],
      campaigns: [{
        id: "road",
        name: "The Road",
        startingStore: [
          { itemId: "ghost", quantity: 1 },
          { itemId: "tonic", quantity: 0 }
        ]
      }]
    };
    const { app, host, workspace } = mount(source);
    workspace.selectSection("flow");
    await nextTick();

    const store = host.querySelector<HTMLElement>(
      'section[aria-labelledby="starting-store-title"]'
    )!;
    // A stored choice this project does not hold is named rather than hidden,
    // and it is still the option the control shows.
    expect(store.textContent).toContain(
      "'ghost' is not an item in this project"
    );
    expect(
      host.querySelector<HTMLSelectElement>("#starting-store-0-item")?.value
    ).toBe("item:ghost");
    expect(store.textContent).toContain(
      "Say how many, as a whole number of at least 1."
    );
    app.unmount();
  });

  it("says the same thing twice about one item once", async () => {
    const source: SourceProject = {
      ...createSourceProject(),
      items: [{ id: "tonic", name: "Tonic", stackLimit: 9 }],
      campaigns: [{
        id: "road",
        name: "The Road",
        startingStore: [
          { itemId: "tonic", quantity: 3 },
          { itemId: "tonic", quantity: 5 }
        ]
      }]
    };
    const { app, host, workspace } = mount(source);
    workspace.selectSection("flow");
    await nextTick();

    // Two entries for one item are two different answers to one question, and
    // the editor says so where the compiler would refuse it.
    expect(host.querySelector<HTMLElement>(
      'section[aria-labelledby="starting-store-title"]'
    )!.textContent).toContain("This list already stocks 'tonic'");
    app.unmount();
  });

  it("reports a typed-but-unsaved form draft as dirty work", async () => {
    const { app, host, onDirty } = mount();
    createButton(host).click();
    await nextTick();
    onDirty.mockClear();
    const name = host.querySelector<HTMLInputElement>("#field-name")!;
    name.value = "Guardian";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    // No save button pressed: the keystrokes alone are unsaved work.
    expect(onDirty).toHaveBeenCalled();
    app.unmount();
  });

  it("commits the open form draft when the author navigates away", async () => {
    const { app, host, onChange, workspace } = mount();
    await openClasses(host);
    createButton(host).click();
    await nextTick();
    const name = host.querySelector<HTMLInputElement>("#field-name")!;
    name.value = "Guardian";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();

    workspace.selectSection("maps");
    await nextTick();
    const project = onChange.mock.lastCall?.[0] as SourceProject;
    expect(project.classes[0]?.name).toBe("Guardian");
    app.unmount();
  });

  it("flushes typed drafts into the session for a project save", async () => {
    const { app, host, onChange } = mount();
    await openClasses(host);
    createButton(host).click();
    await nextTick();
    const name = host.querySelector<HTMLInputElement>("#field-name")!;
    name.value = "Guardian";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();

    const workspace = host.querySelector("#content");
    expect(workspace).not.toBeNull();
    // The exposed hook the shell's Save uses.
    interface Flushable { flushDrafts(): boolean }
    const exposed = (app._instance?.exposed ?? {}) as Partial<Flushable>;
    expect(exposed.flushDrafts?.()).toBe(true);
    const project = onChange.mock.lastCall?.[0] as SourceProject;
    expect(project.classes[0]?.name).toBe("Guardian");
    app.unmount();
  });

  it("refuses a delete by naming who still uses the record, in plain words", async () => {
    const project: SourceProject = {
      schemaVersion: "1.2.0",
      packageId: "d05f4dc5-592f-4c6a-9093-f4090a722ffc",
      gameId: "delete.fixture",
      title: "Delete Fixture",
      contentRevision: "1.0.0",
      classes: [{
        id: "guard",
        name: "Guard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      }],
      unitTypes: [
        { id: "wren", name: "Wren", classId: "guard" },
        { id: "kesh", name: "Kesh", classId: "guard" }
      ],
      weapons: [],
      items: [],
      maps: []
    };
    const { app, host } = mount(project);
    await openClasses(host);
    button(host, "Guard").click();
    await nextTick();
    button(host, "Delete Guard").click();
    await nextTick();
    expect(host.textContent).toContain(
      "Wren (the character) and Kesh (the character) still use Guard."
    );
    expect(host.textContent).toContain("then delete Guard");
    // No JSON pointers in the author's face.
    expect(host.textContent).not.toContain("/unitTypes/");
    app.unmount();
  });

  it("tells a map what is fought on it, and points at where that is set up",
    async () => {
      const fought: SourceProject = {
        ...createSourceProject(),
        classes: [{
          id: "guard", name: "Guard",
          baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
        }],
        unitTypes: [{ id: "scout", name: "Scout", classId: "guard" }],
        maps: [{
          id: "ford", name: "The Ford", width: 2, height: 2,
          terrain: ["plain", "plain", "plain", "plain"]
        }],
        dialogues: [
          { id: "muster", name: "The Muster" },
          { id: "after", name: "What the River Took" }
        ],
        objectives: [{ id: "hold", name: "Hold the ford" }],
        campaigns: [{
          id: "war",
          name: "The War",
          roster: [{ id: "lead", name: "Lead", unitTypeId: "scout" }],
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "crossing",
            nodes: [
              {
                id: "crossing", name: "The Crossing", kind: "encounter",
                mapId: "ford",
                dialogueIds: ["muster"],
                objectiveIds: ["hold"],
                placements: [
                  {
                    id: "ours", unitTypeId: "scout", side: "first",
                    memberId: "lead", x: 0, y: 0
                  },
                  { id: "theirs", unitTypeId: "scout", side: "second", x: 1, y: 1 }
                ],
                transitions: [{ id: "won", targetNodeId: "done", priority: 0 }]
              },
              {
                id: "done", name: "Done", kind: "story",
                dialogueIds: ["after"], transitions: []
              }
            ]
          }
        }]
      };
      const { app, host, onSection, workspace } = mount(fought, "maps");
      button(recordList(host), "The Ford").click();
      await nextTick();

      const panel = host.querySelector(".map-stages")!;
      expect(panel.textContent).toContain("The Crossing");
      expect(panel.textContent).toContain("in The War");
      expect(panel.textContent).toContain("1 of yours against 1.");
      // The two are different shapes and the panel says both: a scene on the
      // Stage plays on arriving, a scene on what it leads to plays once it is
      // done.
      expect(panel.textContent).toContain("Before it: The Muster.");
      expect(panel.textContent).toContain("After it: What the River Took.");
      expect(panel.textContent).toContain("Winning means Hold the ford.");

      // Maps is ground and only ground: the list is a way to reach a Stage,
      // and nothing here authors one. A second door onto the same node is a
      // second place it could disagree with itself.
      expect(panel.querySelector("input")).toBeNull();
      expect(panel.querySelector("select")).toBeNull();
      expect(host.querySelector(".stage-editor")).toBeNull();

      // The road out is a real departure, announced to the rail, landing on
      // the Stage rather than merely on the section that holds it.
      button(panel as HTMLElement, "Open it under Stages").click();
      await nextTick();
      expect(onSection.mock.lastCall?.[0]).toBe("stages");
      expect(host.querySelector("#content-title")?.textContent).toBe("Stages");
      expect(host.querySelector<HTMLInputElement>("#stage-name")?.value)
        .toBe("The Crossing");

      // A map nothing is fought on says so without scolding: ground with no
      // Stage on it is a normal thing to have.
      workspace.selectSection("maps");
      await nextTick();
      createButton(host).click();
      await nextTick();
      expect(host.querySelector(".map-stages")!.textContent)
        .toContain("No Stage uses this ground yet");
      app.unmount();
    });

  it("makes a Stage on chosen ground, and everything it needs to exist",
    async () => {
      // The four decisions behind a Stage are six correct guesses deep through
      // Flow when an author makes them by hand: a campaign, its flow, the node,
      // and a way out of it. One press pays all four.
      const { app, host, onChange, workspace } = mount(undefined, "maps");
      createButton(host).click();
      await nextTick();
      workspace.selectSection("stages");
      await nextTick();
      button(host, "Make the Stage").click();
      await nextTick();

      const project = onChange.mock.lastCall?.[0] as SourceProject;
      const campaign = project.campaigns![0]!;
      const flow = campaign.flow!;
      const stage = flow.nodes.find((node) => node.kind === "encounter")!;
      expect(stage.mapId).toBe(project.maps[0]!.id);
      expect(flow.entryNodeId).toBe(stage.id);
      // A Stage that leads nowhere is a flow the editor will not save, so the
      // way out came with it rather than being the author's next problem.
      expect(stage.transitions).toHaveLength(1);
      expect(
        flow.nodes.find((node) => node.id === stage.transitions[0]!.targetNodeId)
      ).toMatchObject({ kind: "terminal" });
      // And it says what it made rather than leaving it to be discovered.
      expect(host.textContent).toContain("a campaign that opens on a Stage");

      // It opened here, in the section that owns Stages.
      expect(host.querySelector("#content-title")?.textContent).toBe("Stages");
      expect(host.querySelector(".stage-editor")).not.toBeNull();
      expect(host.querySelector(".stage-editor")!.textContent)
        .toContain("A Stage fought on New Map");

      // A second press of the same button is the same question, so it opens
      // the Stage the first press made. It writes nothing: an author unsure
      // whether the press had landed must not be able to make three Stages by
      // checking.
      button(host, "Back to the Stages").click();
      await nextTick();
      const writes = onChange.mock.calls.length;
      button(host, "Make the Stage").click();
      await nextTick();
      expect(host.textContent).toContain("already fights at New Map");
      button(host, "Back to the Stages").click();
      await nextTick();
      button(host, "Make the Stage").click();
      await nextTick();
      expect(onChange.mock.calls.length).toBe(writes);
      expect(host.querySelectorAll(".record-list ul li")).toHaveLength(1);
      // And it is the first Stage that is open, not a copy of it.
      expect(host.querySelector(".stage-editor")).not.toBeNull();

      // One map may still be fought over more than once, on the control that
      // says so and only there.
      button(host, "Back to the Stages").click();
      await nextTick();
      button(host, "Add another Stage on this ground").click();
      await nextTick();
      const twice = onChange.mock.lastCall?.[0] as SourceProject;
      expect(twice.campaigns).toHaveLength(1);
      expect(
        twice.campaigns![0]!.flow!.nodes.filter(
          (node) => node.kind === "encounter"
        )
      ).toHaveLength(2);
      expect(host.querySelectorAll(".record-list ul li")).toHaveLength(2);
      app.unmount();
    });

  it("offers no second Stage on ground nothing is fought over", async () => {
    // The deliberate control appears only once there is something to be
    // another of: fresh ground offers one verb, so there is nothing to pick
    // wrong.
    const { app, host, workspace } = mount(undefined, "maps");
    createButton(host).click();
    await nextTick();
    workspace.selectSection("stages");
    await nextTick();
    expect(host.textContent).not.toContain("Add another Stage on this ground");
    button(host, "Make the Stage").click();
    await nextTick();
    button(host, "Back to the Stages").click();
    await nextTick();
    expect(host.textContent).toContain("Add another Stage on this ground");
    app.unmount();
  });

  it("sends an author to draw ground before they can make a Stage on it",
    async () => {
      // No orphan states: a cold project has no map, so the section that makes
      // Stages says what is missing and offers the road to it rather than
      // showing a picker with nothing in it.
      const { app, host, onSection } = mount(undefined, "stages");
      expect(host.textContent).toContain("this game has no map yet");
      expect(host.querySelector("#stage-ground")).toBeNull();
      button(host, "Go to Maps").click();
      await nextTick();
      expect(onSection.mock.lastCall?.[0]).toBe("maps");
      app.unmount();
    });

  it("writes the board onto the Stage's own node", async () => {
    const { app, host, onChange, workspace } = mount(
      {
        ...createSourceProject(),
        classes: [{
          id: "scout", name: "Scout",
          baseStats: { health: 5, movement: 3, strength: 2, defense: 0 }
        }],
        unitTypes: [{ id: "raider", name: "Raider", classId: "scout" }],
        maps: [{
          id: "ford", name: "The Ford", width: 3, height: 2,
          terrain: Array.from({ length: 6 }, () => "plain")
        }]
      },
      "stages"
    );
    button(host, "Make the Stage").click();
    await nextTick();

    const board = host.querySelector<HTMLElement>(".stage-editor")!;
    // Put an enemy down: the palette stamps, and a stamped placement names
    // nobody, which is what makes that character an extra.
    board.querySelector<HTMLButtonElement>('[data-unit-type="raider"]')!.click();
    await nextTick();
    button(board, "The enemy").click();
    await nextTick();
    board.querySelector<HTMLButtonElement>('[data-cell="4"]')!.click();
    await nextTick();

    const project = onChange.mock.lastCall?.[0] as SourceProject;
    const stage = project.campaigns![0]!.flow!.nodes
      .find((node) => node.kind === "encounter")!;
    expect(stage.placements).toEqual([
      { id: "unit", unitTypeId: "raider", side: "second", x: 1, y: 1 }
    ]);

    // Flow shows where the same Stage comes in the campaign, and does not
    // offer the board a second time.
    button(host, "Show where it comes in the campaign").click();
    await nextTick();
    await nextTick();
    expect(host.querySelector("#content-title")?.textContent).toBe("Flow");
    expect(host.querySelector<HTMLInputElement>("#campaign-node-id")?.value)
      .toBe(stage.id);
    expect(host.querySelector("#placement-0-unit-type")).toBeNull();
    void workspace;
    app.unmount();
  });

  it("enrols a stamped character into the company for the player's own side",
    async () => {
      const { app, host, onChange } = mount(
        {
          ...createSourceProject(),
          classes: [{
            id: "scout", name: "Scout",
            baseStats: { health: 5, movement: 3, strength: 2, defense: 0 }
          }],
          unitTypes: [{ id: "rina", name: "Rina", classId: "scout" }],
          maps: [{
            id: "ford", name: "The Ford", width: 3, height: 2,
            terrain: Array.from({ length: 6 }, () => "plain")
          }]
        },
        "stages"
      );
      button(host, "Make the Stage").click();
      await nextTick();
      const board = host.querySelector<HTMLElement>(".stage-editor")!;
      board.querySelector<HTMLButtonElement>('[data-unit-type="rina"]')!.click();
      await nextTick();
      button(board, "Your side").click();
      await nextTick();
      board.querySelector<HTMLButtonElement>('[data-cell="0"]')!.click();
      await nextTick();

      const project = onChange.mock.lastCall?.[0] as SourceProject;
      const campaign = project.campaigns![0]!;
      // Your side is fought by the company, so the company gained them, and
      // the placement names them, which is what keeps the campaign saveable.
      expect(campaign.roster).toEqual([
        { id: "rina", name: "Rina", unitTypeId: "rina" }
      ]);
      const stage = campaign.flow!.nodes
        .find((node) => node.kind === "encounter")!;
      expect(stage.placements![0]!.memberId).toBe("rina");
      expect(host.textContent).toContain("Rina joined it");
      app.unmount();
    });

  it("makes a character the game has not got and stands them on the board",
    async () => {
      // A game with ground and nobody in it. The author's act is "put a bandit
      // here", and the answer is a bandit, not a trip to Characters to build
      // a weapon type, a weapon, a class and a character before coming back.
      const { app, host, onChange } = mount(
        {
          ...createSourceProject(),
          maps: [{
            id: "ford", name: "The Ford", width: 3, height: 2,
            terrain: Array.from({ length: 6 }, () => "plain")
          }]
        },
        "stages"
      );
      button(host, "Make the Stage").click();
      await nextTick();

      const board = host.querySelector<HTMLElement>(".stage-editor")!;
      board.querySelector<HTMLButtonElement>(
        '[data-palette="new:medieval_rogue"]'
      )!.click();
      await nextTick();
      const name = board.querySelector<HTMLInputElement>("#palette-new-name")!;
      name.value = "Bandit";
      name.dispatchEvent(new Event("input"));
      await nextTick();
      board.querySelector<HTMLButtonElement>('[data-cell="4"]')!.click();
      await nextTick();

      const made = onChange.mock.lastCall?.[0] as SourceProject;
      expect(made.unitTypes.map((unitType) => unitType.name)).toEqual(["Bandit"]);
      expect(made.classes).toHaveLength(1);
      expect(made.weapons).toHaveLength(1);
      expect(made.weaponTypes).toHaveLength(1);
      expect(made.factions).toEqual([
        { id: "the_enemy", name: "The enemy", color: "red" }
      ]);
      const stage = made.campaigns![0]!.flow!.nodes
        .find((node) => node.kind === "encounter")!;
      expect(stage.placements).toEqual([
        { id: "unit", unitTypeId: "bandit", side: "second", x: 1, y: 1 }
      ]);

      // One press of Undo, not five. The character, its class, its weapon, its
      // weapon type, the faction and the placement are one thing the author
      // did, and they go away together, with nothing left standing behind a
      // character that no longer exists.
      button(host, "Undo").click();
      await nextTick();
      const back = onChange.mock.lastCall?.[0] as SourceProject;
      expect(back.unitTypes).toEqual([]);
      expect(back.classes).toEqual([]);
      expect(back.weapons).toEqual([]);
      expect(back.weaponTypes ?? []).toEqual([]);
      expect(back.factions ?? []).toEqual([]);
      expect(
        back.campaigns![0]!.flow!.nodes
          .find((node) => node.kind === "encounter")!.placements
      ).toBeUndefined();
      app.unmount();
    });

  it("will not stand a member of the company on one board twice", async () => {
    // Warden Kesh marches with the company, so she is one woman and not a
    // kind. Nothing was authored to say so: `characterIsOnePerson` reads it
    // off the campaign that holds her.
    const { app, host, onChange } = mount(
      {
        ...createSourceProject(),
        classes: [{
          id: "scout", name: "Scout",
          baseStats: { health: 5, movement: 3, strength: 2, defense: 0 }
        }],
        unitTypes: [{ id: "warden", name: "Warden", classId: "scout" }],
        maps: [{
          id: "ford", name: "The Ford", width: 3, height: 2,
          terrain: Array.from({ length: 6 }, () => "plain")
        }],
        campaigns: [{
          id: "march",
          name: "The march",
          roster: [{ id: "kesh", name: "Warden Kesh", unitTypeId: "warden" }]
        }]
      },
      "stages"
    );
    button(host, "Make the Stage").click();
    await nextTick();

    const board = host.querySelector<HTMLElement>(".stage-editor")!;
    board.querySelector<HTMLButtonElement>('[data-unit-type="warden"]')!.click();
    await nextTick();
    button(board, "The enemy").click();
    await nextTick();
    board.querySelector<HTMLButtonElement>('[data-cell="0"]')!.click();
    await nextTick();
    board.querySelector<HTMLButtonElement>('[data-cell="1"]')!.click();
    await nextTick();

    const project = onChange.mock.lastCall?.[0] as SourceProject;
    const stage = project.campaigns![0]!.flow!.nodes
      .find((node) => node.kind === "encounter")!;
    expect(stage.placements).toHaveLength(1);
    expect(host.textContent).toContain("Warden already stands on this board");
    app.unmount();
  });

  it("edits what is said before a Stage and only reports what is said after",
    async () => {
      const fought: SourceProject = {
        ...createSourceProject(),
        maps: [{
          id: "ford", name: "The Ford", width: 2, height: 2,
          terrain: ["plain", "plain", "plain", "plain"]
        }],
        dialogues: [{ id: "after", name: "What the River Took" }],
        campaigns: [{
          id: "war", name: "The War",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "crossing",
            nodes: [
              {
                id: "crossing", name: "The Crossing", kind: "encounter",
                mapId: "ford",
                transitions: [{ id: "won", targetNodeId: "done", priority: 0 }]
              },
              {
                id: "done", name: "Done", kind: "story",
                dialogueIds: ["after"], transitions: []
              }
            ]
          }
        }]
      };
      const { app, host } = mount(fought, "stages");
      button(recordList(host), "The Crossing").click();
      await nextTick();
      const surface = host.querySelector<HTMLElement>(".stage-editor")!;

      // Before is this node's own scenes, so it is edited here.
      const before = surface.querySelector<HTMLElement>(".stage-before")!;
      expect(before.querySelector("#cutscene-new-name")).not.toBeNull();

      // After belongs to whatever the Stage leads to, so it is named by the
      // node that owns it and changed there. A symmetrical pair of fields
      // would be lying about one of them.
      const after = surface.querySelector<HTMLElement>(".stage-after")!;
      expect(after.textContent).toContain("Done");
      expect(after.textContent).toContain("says What the River Took.");
      expect(after.querySelector("input")).toBeNull();
      button(after, "Change what Done says").click();
      await nextTick();
      await nextTick();
      expect(host.querySelector("#content-title")?.textContent).toBe("Flow");
      expect(host.querySelector<HTMLInputElement>("#campaign-node-id")?.value)
        .toBe("done");
      app.unmount();
    });

  it("asks which campaign a Stage joins only when the game has a choice",
    async () => {
      const two: SourceProject = {
        ...createSourceProject(),
        maps: [{
          id: "ford", name: "The Ford", width: 2, height: 2,
          terrain: ["plain", "plain", "plain", "plain"]
        }],
        campaigns: [
          { id: "march", name: "The March" },
          { id: "siege", name: "The Siege" }
        ]
      };
      const { app, host, onChange } = mount(two, "stages");
      const chooser = host.querySelector<HTMLSelectElement>("#stage-campaign")!;
      // Every campaign, once: the first is the default rather than a second
      // entry in the list saying the same thing.
      expect([...chooser.options].map((option) => option.value))
        .toEqual(["march", "siege"]);
      expect(chooser.value).toBe("march");

      chooser.value = "siege";
      chooser.dispatchEvent(new Event("change"));
      await nextTick();
      button(host, "Make the Stage").click();
      await nextTick();
      const project = onChange.mock.lastCall?.[0] as SourceProject;
      expect(project.campaigns![0]!.flow).toBeUndefined();
      expect(project.campaigns![1]!.flow?.nodes.some(
        (node) => node.kind === "encounter"
      )).toBe(true);
      app.unmount();
    });

  it("refuses a Stage it cannot make and leaves the game exactly as it was",
    async () => {
      const looping: SourceProject = {
        ...createSourceProject(),
        maps: [{
          id: "ford", name: "The Ford", width: 2, height: 2,
          terrain: ["plain", "plain", "plain", "plain"]
        }],
        campaigns: [{
          id: "loop", name: "The Loop",
          flow: {
            contractVersion: "1.0.0",
            entryNodeId: "a",
            nodes: [
              { id: "a", name: "A", kind: "story", transitions: [
                { id: "next", targetNodeId: "b", priority: 0 }
              ] },
              { id: "b", name: "B", kind: "story", transitions: [
                { id: "next", targetNodeId: "a", priority: 0 }
              ] }
            ]
          }
        }]
      };
      const { app, host, onChange } = mount(looping, "stages");
      button(host, "Make the Stage").click();
      await nextTick();

      expect(host.textContent).toContain("no point where it stops");
      // Nothing was written, so there is no half-made Stage to clean up.
      expect(onChange).not.toHaveBeenCalled();
      expect(host.querySelector(".stage-editor")).toBeNull();
      app.unmount();
    });

  it("starts a new map as a small battlefield rather than one tile", async () => {
    const { app, host, onChange, workspace } = mount();
    workspace.selectSection("maps");
    await nextTick();
    createButton(host).click();
    await nextTick();
    const project = onChange.mock.lastCall?.[0] as SourceProject;
    expect(project.maps[0]).toMatchObject({ width: 8, height: 6 });
    expect(project.maps[0]?.terrain).toHaveLength(48);
    expect(project.maps[0]?.terrain.every((cell) => cell === "plain")).toBe(true);
    app.unmount();
  });

  it("never signposts a step, in any room", async () => {
    // There was a "Next" banner here, telling an author on Characters to make
    // a character and an author on Weapons & items to go and draw a map. It
    // was wrong in every room: it named a job the author was either already
    // doing or had not asked about, and offered to send them somewhere they
    // would immediately leave. A signpost that is wrong everywhere is not a
    // signpost.
    for (const section of ["characters", "equipment", "maps", "stages", "flow"]) {
      const { app, host } = mount(undefined, section);
      expect(host.querySelector(".next-step"), section).toBeNull();
      expect(host.textContent, section).not.toContain("Make a character, or two");
      expect(host.textContent, section).not.toContain("Draw a map for them");
      app.unmount();
    }
  });

  it("holds the settings, and nothing that belongs to a record", async () => {
    const { app, host } = mount(undefined, "game");
    // The landing section is configuration and only configuration.
    expect(host.querySelector(".next-step")).toBeNull();
    expect(host.querySelector(".playtest-panel")).toBeNull();
    expect(host.querySelector("#field-notes")).toBeNull();
    expect(host.querySelector("#field-packageId")).toBeNull();
    expect(host.textContent).not.toContain("Your first battle");
    // What is there is the settings form, whole.
    expect(host.querySelector("#field-title")).not.toBeNull();
    expect(host.querySelector("#field-gameId")).not.toBeNull();
    expect(host.querySelector("#field-defaultTurnOrder")).not.toBeNull();
    expect(host.querySelector("#field-characterLoss")).not.toBeNull();
    // No game-wide build: the choice is about a person's picture, and there
    // is nobody to look at until a character exists. It lives on the character.
    expect(host.querySelector("#field-characterFigureId")).toBeNull();
    // Including the testing aid, which is on this page and under its own
    // heading rather than folded in among the choices about the game.
    expect(host.querySelector("#field-invulnerableForTesting")).not.toBeNull();
    expect(host.querySelector(".testing-aids")).not.toBeNull();
    app.unmount();
  });

  it("says nothing about a next step once the game can be played", async () => {
    // Both sides placed on a real board is exactly what the third step checks,
    // so a project that has it has nothing outstanding to be told about.
    const playable: SourceProject = {
      ...createSourceProject(),
      classes: [{
        id: "guard",
        name: "Guard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      }],
      unitTypes: [{ id: "scout", name: "Scout", classId: "guard" }],
      maps: [{
        id: "field", name: "Field", width: 2, height: 2,
        terrain: ["plain", "plain", "plain", "plain"]
      }],
      campaigns: [{
        id: "road",
        name: "The Road",
        roster: [{ id: "lead", name: "Lead", unitTypeId: "scout" }],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "opening",
          nodes: [{
            id: "opening",
            name: "Opening",
            kind: "encounter",
            mapId: "field",
            transitions: [],
            placements: [
              {
                id: "ours", unitTypeId: "scout", side: "first",
                memberId: "lead", x: 0, y: 0
              },
              { id: "theirs", unitTypeId: "scout", side: "second", x: 1, y: 1 }
            ]
          }]
        }
      }]
    };
    const { app, host, workspace } = mount(playable);
    expect(host.querySelector(".next-step")).toBeNull();
    for (const section of ["maps", "flow", "equipment", "diagnostics"]) {
      workspace.selectSection(section);
      await nextTick();
      expect(host.querySelector(".next-step"), section).toBeNull();
    }
    app.unmount();
  });

  it("holds the playtest and the project's own file under Diagnostics",
    async () => {
      const { app, host } = mount(undefined, "diagnostics");
      // A smaller game than Play, kept for the engine fingerprint it shows.
      expect(host.querySelector(".playtest-panel")).not.toBeNull();
      expect(host.textContent).toContain("Browser playtest");
      // And the fields that describe the project rather than the game, out in
      // the open rather than behind a disclosure.
      expect(host.textContent).toContain("This project's file");
      expect(host.querySelector("#field-notes")).not.toBeNull();
      expect(host.querySelector("#field-packageId")).not.toBeNull();
      expect(host.querySelector("#field-notes")!.closest("details")).toBeNull();
      app.unmount();
    });

  it("signposts Stages from a character, and from the ground it is fought on",
    async () => {
      const { app, host, onSection, workspace } = mount();
      await makeCharacter(host, "Wren");
      button(recordList(host), "Wren").click();
      await nextTick();
      expect(host.textContent).toContain(
        "A character fights when a Stage places them."
      );
      button(host, "Go to Stages").click();
      await nextTick();
      // The signpost is a real departure, announced to the rail beside the
      // workspace so it marks the place the author has actually arrived at.
      expect(onSection.mock.lastCall?.[0]).toBe("stages");
      expect(host.querySelector("#content-title")?.textContent).toBe("Stages");

      // A map signposts the same place, and carries the ground across so the
      // author is not asked to choose it twice. It writes nothing on the way:
      // one press makes a Stage, and it is the one under Stages.
      workspace.selectSection("maps");
      await nextTick();
      createButton(host).click();
      await nextTick();
      button(host, "Make a Stage on this ground").click();
      await nextTick();
      expect(onSection.mock.lastCall?.[0]).toBe("stages");
      expect(host.querySelector(".stage-editor")).toBeNull();
      expect(host.querySelector<HTMLSelectElement>("#stage-ground")?.value)
        .toBe("new_map");
      app.unmount();
    });

  it("makes a whole character from the wizard's three answers", async () => {
    const { app, host, workspace } = mount();
    await makeCharacter(host, "Wren", {
      recipe: "medieval_archer",
      side: "the_enemy"
    });

    expect(host.textContent).toContain("Made Wren");
    // The chain is real content, not a special record: every part of it is
    // listed in its own collection afterwards.
    workspace.selectSection("equipment");
    await nextTick();
    button(host, "Weapons").click();
    await nextTick();
    // Scoped to the record list: the weapon shelf below offers a Longbow at
    // all times, so an unscoped match would pass without a record existing.
    expect(recordList(host).textContent).toContain("Longbow");
    workspace.selectSection("characters");
    await nextTick();
    await openClasses(host);
    // The class is named for the archetype, not for the character: a class is
    // what several characters share, and "Wren class" would be a class nobody
    // but Wren could ever be in without reading like a mistake.
    expect(recordList(host).textContent).toContain("Archer class");
    expect(recordList(host).textContent).not.toContain("Wren class");
    // The side is a faction, and the faction is an ordinary record too.
    categoryButton(host, "Factions").click();
    await nextTick();
    expect(recordList(host).textContent).toContain("The enemy");
    app.unmount();
  });

  it("undoes a whole character in one press", async () => {
    // The wizard's last press is one thing the author did, whatever the format
    // needs written for it: a faction, a weapon type, a weapon, a class and
    // the character. Five undo entries would make "I did not mean that" cost
    // five presses, and a refusal partway would leave a class standing with
    // nobody in it.
    const { app, host, onChange } = mount();
    await makeCharacter(host, "Wren", {
      recipe: "medieval_archer",
      side: "the_enemy"
    });
    const made = onChange.mock.lastCall?.[0] as SourceProject;
    expect(made.unitTypes).toHaveLength(1);
    expect(made.classes).toHaveLength(1);
    expect(made.weapons).toHaveLength(1);
    expect(made.weaponTypes).toHaveLength(1);
    expect(made.factions).toHaveLength(1);

    button(host, "Undo").click();
    await nextTick();
    const back = onChange.mock.lastCall?.[0] as SourceProject;
    expect(back.unitTypes).toEqual([]);
    expect(back.classes).toEqual([]);
    expect(back.weapons).toEqual([]);
    expect(back.weaponTypes ?? []).toEqual([]);
    expect(back.factions ?? []).toEqual([]);
    app.unmount();
  });

  it("puts a second character of a role into the class the first made", async () => {
    const { app, host, onChange } = mount();
    await makeCharacter(host, "Wren", { recipe: "medieval_archer" });
    await makeCharacter(host, "Fen", { recipe: "medieval_archer" });
    const saved = onChange.mock.lastCall?.[0] as SourceProject;
    // Two archers, two unit types, two weapons, one archer class. A class per
    // character would make ten archers into ten identical classes, and every
    // later change to what an archer is into ten changes.
    expect(saved.classes).toHaveLength(1);
    expect(saved.classes[0]!.name).toBe("Archer class");
    expect(saved.unitTypes.map((unit) => unit.classId))
      .toEqual([saved.classes[0]!.id, saved.classes[0]!.id]);
    expect(new Set(saved.weapons.map((weapon) => weapon.id)).size).toBe(2);
    // And the author is told they joined something shared rather than left to
    // discover it by editing the class and changing somebody else's character.
    expect(host.textContent).toContain("They join Archer class");
    expect(host.textContent).toContain("changing it changes everyone in it");
    app.unmount();
  });

  it("reuses one side faction however many characters join it", async () => {
    const { app, host, onChange } = mount();
    await makeCharacter(host, "Wren", { side: "the_enemy" });
    await makeCharacter(host, "Kesh", { side: "the_enemy" });
    const saved = onChange.mock.lastCall?.[0] as SourceProject;
    expect(saved.factions).toEqual([
      { id: "the_enemy", name: "The enemy", color: "red" }
    ]);
    expect(saved.unitTypes.map((unit) => unit.factionId))
      .toEqual(["the_enemy", "the_enemy"]);
    app.unmount();
  });

  it("leaves a character on no side when the author chose neither", async () => {
    const { app, host, onChange } = mount();
    await makeCharacter(host, "Wren");
    const saved = onChange.mock.lastCall?.[0] as SourceProject;
    expect(saved.factions ?? []).toEqual([]);
    expect("factionId" in saved.unitTypes[0]!).toBe(false);
    app.unmount();
  });

  it("shows the characters first, with their side and who depends on them",
    async () => {
      const { app, host } = mount();
      // An empty project says what the button is for rather than nothing.
      expect(host.textContent).toContain("Your characters");
      expect(host.textContent).toContain("No characters yet.");

      await makeCharacter(host, "Wren", { side: "your_side" });
      const cards = [...host.querySelectorAll<HTMLButtonElement>(".character-card")];
      expect(cards).toHaveLength(1);
      expect(cards[0]!.textContent).toContain("Wren");
      expect(cards[0]!.textContent).toContain("One of yours");
      expect(cards[0]!.textContent).toContain("Wren is not in any Stage yet.");
      // Drawn with the picture the board will use for them.
      expect(cards[0]!.querySelector("img")!.getAttribute("src"))
        .toContain("knight_blue");

      // The characters come before the collections they are assembled from.
      const roster = host.querySelector(".character-roster")!;
      const categories = host.querySelector(".content-categories")!;
      expect(
        roster.compareDocumentPosition(categories) &
        Node.DOCUMENT_POSITION_FOLLOWING
      ).toBeTruthy();

      // And selecting one opens the same record form everything else opens in.
      cards[0]!.click();
      await nextTick();
      expect(host.querySelector<HTMLInputElement>("#field-name")!.value)
        .toBe("Wren");
      app.unmount();
    });

  it("keeps a shelf's setting to itself when the wizard writes a record",
    async () => {
      const { app, host, workspace } = mount();
      // The sci-fi shelf, chosen inside the wizard: the same role under the
      // name its setting gives it, and nothing of the setting left behind.
      await makeCharacter(host, "Wren", {
        setting: "Sci-fi",
        recipe: "scifi_archer"
      });
      workspace.selectSection("equipment");
      await nextTick();
      button(host, "Weapons").click();
      await nextTick();
      expect(recordList(host).textContent).toContain("Wren's Rail Rifle");
      expect(host.textContent).not.toContain("scifi");
      app.unmount();
    });

  it("writes what a commissioned shelf promised, and nothing of the shelf",
    async () => {
      const { app, host, onChange } = mount();
      await makeCharacter(host, "Ash", {
        setting: "Mythical",
        recipe: "mythical_beast"
      });

      const saved = onChange.mock.lastCall?.[0] as SourceProject;
      const unitClass = saved.classes.at(-1)!;
      // The one entry in the catalogue that says how it crosses ground says so
      // because the class it writes says so.
      expect(unitClass.traversal).toEqual({ flying: true });
      expect(saved.weapons.at(-1)!.name).toBe("Ash's Dragon Fang");
      // Copy-on-use, at the surface as well as in the module: nothing an author
      // receives names the shelf it came from.
      expect(JSON.stringify(saved)).not.toContain("mythical");
      app.unmount();
    });

  it("shelves every weapon those characters carry, on one tab stop", async () => {
    const { app, host } = mount(undefined, "equipment");
    const shelf = weaponShelfPanel(host).querySelector<HTMLElement>(
      '[role="radiogroup"]'
    )!;
    expect(shelf.getAttribute("aria-label")).toBe("Kind of weapon");
    const cards = [...shelf.querySelectorAll<HTMLElement>('[role="radio"]')];
    expect(cards.map((card) => card.querySelector("strong")?.textContent?.trim()))
      .toEqual([
        "Sword", "Longbow", "Ember Staff", "Storm Staff",
        "Mending Staff", "Officer's Blade", "Dagger", "Fangs"
      ]);
    // Each says which weapon type it is, because that is what decides who may
    // carry it, and what it does in the words the reach band gives.
    expect(cards[1]!.textContent).toContain("Weapon type: Bow");
    expect(cards[1]!.textContent).toContain("Range 2–3 tiles. Power 3.");

    // Reachable without a mouse: one tab stop, arrow keys inside it.
    expect(cards.filter((card) => card.tabIndex === 0)).toHaveLength(1);
    expect(cards[0]!.getAttribute("aria-checked")).toBe("true");
    shelf.dispatchEvent(
      new KeyboardEvent("keydown", { key: "ArrowRight", bubbles: true })
    );
    await nextTick();
    expect(cards[1]!.getAttribute("aria-checked")).toBe("true");
    expect(document.activeElement).toBe(cards[1]);
    expect(cards.filter((card) => card.tabIndex === 0)).toHaveLength(1);
    app.unmount();
  });

  it("adds a weapon and the weapon type it belongs to, as ordinary records", async () => {
    const { app, host } = mount(undefined, "equipment");
    weaponShelfPanel(host)
      .querySelector<HTMLElement>('[data-recipe="medieval_archer_weapon"]')!
      .click();
    await nextTick();
    button(host, "Add it").click();
    await nextTick();

    expect(host.textContent).toContain("Added Longbow, a Bow weapon.");
    // Both are listed in their own collections, editable like anything else.
    expect(recordList(host).textContent).toContain("Longbow");
    categoryButton(host, "Weapon types").click();
    await nextTick();
    expect(recordList(host).textContent).toContain("Bow");
    app.unmount();
  });

  it("says who can carry a weapon it just added", async () => {
    const { app, host, workspace } = mount(undefined, "equipment");
    // Nobody exists yet, so the message is the one edit that would fix that.
    weaponShelfPanel(host)
      .querySelector<HTMLElement>('[data-recipe="medieval_archer_weapon"]')!
      .click();
    await nextTick();
    button(host, "Add it").click();
    await nextTick();
    expect(host.textContent).toContain("No character's class allows Bow yet");

    // An archer made in the wizard permits Bow, so the next loose bow is one
    // that character can hold: no archaeology, and it says so.
    workspace.selectSection("characters");
    await nextTick();
    await makeCharacter(host, "Wren", { recipe: "medieval_archer" });

    workspace.selectSection("equipment");
    await nextTick();
    button(host, "Add it").click();
    await nextTick();
    expect(host.textContent).toContain("Wren can carry it");
    app.unmount();
  });

  it("reuses one weapon type rather than shelving a second of the same name", async () => {
    const { app, host } = mount(undefined, "equipment");
    const shelf = weaponShelfPanel(host);
    shelf.querySelector<HTMLElement>('[data-recipe="medieval_knight_weapon"]')!.click();
    await nextTick();
    button(host, "Add it").click();
    await nextTick();
    // A dagger is a Blade too. Two Blade records would read identically and
    // only one of them would be the one a class permits.
    shelf.querySelector<HTMLElement>('[data-recipe="medieval_rogue_weapon"]')!.click();
    await nextTick();
    button(host, "Add it").click();
    await nextTick();

    categoryButton(host, "Weapon types").click();
    await nextTick();
    expect(recordList(host).textContent).toContain("1 matching record");
    app.unmount();
  });

  it("filters the weapon shelf by setting and leaves nothing of itself behind", async () => {
    const { app, host, workspace } = mount(undefined, "equipment");
    const shelf = weaponShelfPanel(host);
    shelf.querySelector<HTMLElement>('[data-recipe="medieval_archer_weapon"]')!.click();
    await nextTick();
    button(shelf, "Sci-fi").click();
    await nextTick();

    // The same armament, under the name its setting gives it.
    const chosen = shelf.querySelector<HTMLElement>('[aria-checked="true"]')!;
    expect(chosen.dataset.recipe).toBe("scifi_archer_weapon");
    expect(chosen.textContent).toContain("Rail Rifle");
    expect(chosen.textContent).toContain("Weapon type: Rifle");

    button(host, "Add it").click();
    await nextTick();
    expect(recordList(host).textContent).toContain("Rail Rifle");
    expect(host.textContent).not.toContain("scifi");

    // The wizard's shelf is a separate shelf and did not move with it: a
    // setting chosen for weapons decides nothing about who is offered.
    workspace.selectSection("characters");
    await nextTick();
    button(host, "New character").click();
    await nextTick();
    button(host, "Next").click();
    await nextTick();
    expect(host.querySelector<HTMLElement>('[aria-checked="true"]')!.dataset.recipe)
      .toBe("medieval_knight");
    app.unmount();
  });

  it("opens a section on the collection it declares first, however reached",
    async () => {
      // One order, from one place. A strip ordered by this file and an opening
      // collection ordered by the section would make Characters lead with
      // Classes on the first render and with Characters when the rail is used.
      const { app, host, workspace } = mount();
      const strip = () => [...host.querySelectorAll<HTMLElement>(
        ".content-categories button"
      )].map((entry) => entry.textContent!.trim().split(/\s+/)[0]);
      const current = () => host.querySelector(
        '.content-categories button[aria-current="page"]'
      )!.textContent!.trim().split(/\s+/)[0];

      expect(strip()).toEqual(["Characters", "Classes", "Factions", "Abilities"]);
      expect(current()).toBe("Characters");
      workspace.selectSection("flow");
      await nextTick();
      workspace.selectSection("characters");
      await nextTick();
      expect(current()).toBe("Characters");
      app.unmount();
    });

  it("draws no collection navigation for a section that owns one", async () => {
    const { app, host, workspace } = mount(undefined, "maps");
    // A navigation between one thing is not a navigation, and drawing it put a
    // full-height column on the page whose whole content was the word "Maps".
    expect(host.querySelector(".content-categories")).toBeNull();
    expect(host.querySelector(".content-layout")!.classList
      .contains("one-collection")).toBe(true);
    // The records are still there and still the section's own.
    createButton(host).click();
    await nextTick();
    expect(recordList(host).textContent).toContain("New Map");

    // A section that owns four of them does draw the strip.
    workspace.selectSection("characters");
    await nextTick();
    expect(host.querySelector(".content-categories")).not.toBeNull();
    expect(host.querySelector(".content-layout")!.classList
      .contains("one-collection")).toBe(false);

    // Flow draws neither, because it is the home of the campaigns without
    // listing them: what it shows is the shape of the one being authored.
    workspace.selectSection("flow");
    await nextTick();
    expect(host.querySelector(".content-categories")).toBeNull();
    expect(host.querySelector(".content-layout")).toBeNull();
    app.unmount();
  });

  it("shelves the abilities the rules can express, on one tab stop", async () => {
    const { app, host } = mount();
    await openAbilities(host);
    const shelf = abilityShelfPanel(host).querySelector<HTMLElement>(
      '[role="radiogroup"]'
    )!;
    expect(shelf.getAttribute("aria-label")).toBe("Kind of ability");
    const cards = [...shelf.querySelectorAll<HTMLElement>('[role="radio"]')];
    expect(cards.map((card) => card.querySelector("strong")?.textContent?.trim()))
      .toEqual([
        "Power Strike", "Ember Bolt", "Sweeping Blow",
        "Storm Circle", "Mend", "Circle of Mending"
      ]);
    // Each says what its record carries, and never how good it is: the tiles
    // it covers, the power, the reach, and whether it can miss.
    expect(cards[2]!.textContent).toContain("Covers 5 tiles. Power 3.");
    expect(cards[0]!.textContent).toContain("Lands 85 in 100.");
    expect(cards[4]!.textContent).toContain("Restores health.");

    // Reachable without a mouse: one tab stop, arrow keys inside it, the same
    // model the two shelves before it use.
    expect(cards.filter((card) => card.tabIndex === 0)).toHaveLength(1);
    expect(cards[0]!.getAttribute("aria-checked")).toBe("true");
    shelf.dispatchEvent(
      new KeyboardEvent("keydown", { key: "ArrowRight", bubbles: true })
    );
    await nextTick();
    expect(cards[1]!.getAttribute("aria-checked")).toBe("true");
    expect(document.activeElement).toBe(cards[1]);
    expect(cards.filter((card) => card.tabIndex === 0)).toHaveLength(1);
    app.unmount();
  });

  it("keeps element identifiers unique on every surface a section shows",
    async () => {
      // A repeated `id` points a `<label for>` at the wrong control and leaves
      // one of them unlabelled to a screen reader, which nothing else in this
      // suite would notice. Every state the sections have is checked, because
      // the ones that share a page change under the author.
      const { app, host, workspace } = mount();
      const check = (where: string) => {
        const identifiers = [...host.querySelectorAll("[id]")].map(
          (element) => element.id
        );
        expect(identifiers.length, where).toBeGreaterThan(0);
        expect(new Set(identifiers).size, where).toBe(identifiers.length);
        for (const label of host.querySelectorAll("label[for]")) {
          expect(
            host.querySelector(`#${label.getAttribute("for")}`), `${where}: ${label.getAttribute("for")}`
          ).not.toBeNull();
        }
      };

      check("characters");
      button(host, "New character").click();
      await nextTick();
      check("the wizard, whose side");
      button(host, "Next").click();
      await nextTick();
      check("the wizard, what kind");
      button(host, "Next").click();
      await nextTick();
      check("the wizard, their name");
      button(host, "Cancel").click();
      await nextTick();
      await openAbilities(host);
      check("abilities");

      for (const section of [
        "game", "equipment", "maps", "stages", "flow", "diagnostics"
      ]) {
        workspace.selectSection(section);
        await nextTick();
        check(section);
      }

      // Stages is the surface with the most in it: the board, the scenes
      // before it, the win conditions, the recruits, the grants. Every
      // one of those is a component that labels its own controls. A section is
      // only checked in the state it is left in, so the state that has the
      // most in it is reached rather than assumed.
      workspace.selectSection("maps");
      await nextTick();
      createButton(host).click();
      await nextTick();
      check("maps with a map open");
      workspace.selectSection("stages");
      await nextTick();
      check("stages with ground to pick");
      button(host, "Make the Stage").click();
      await nextTick();
      check("stages with a Stage open");
      app.unmount();
    });

  it("adds an ability as an ordinary record, and says what to do with it", async () => {
    const { app, host } = mount();
    await openAbilities(host);
    abilityShelfPanel(host)
      .querySelector<HTMLElement>('[data-recipe="medieval_storm"]')!
      .click();
    await nextTick();
    button(host, "Add it").click();
    await nextTick();

    expect(host.textContent).toContain("Added Storm Circle.");
    // An ability nobody carries does nothing, so the message names the edit
    // that changes that rather than leaving it to be discovered.
    expect(host.textContent).toContain(
      "Nobody has it yet: open a character and add it to their abilities."
    );
    // Listed in its own collection, editable like anything else.
    expect(recordList(host).textContent).toContain("Storm Circle");
    app.unmount();
  });

  it("filters the ability shelf by setting and leaves nothing of itself behind",
    async () => {
      const { app, host } = mount();
      await openAbilities(host);
      const shelf = abilityShelfPanel(host);
      shelf.querySelector<HTMLElement>('[data-recipe="medieval_mend_all"]')!.click();
      await nextTick();
      button(shelf, "Pirates").click();
      await nextTick();

      // The same cast, under the name its setting gives it.
      const chosen = shelf.querySelector<HTMLElement>('[aria-checked="true"]')!;
      expect(chosen.dataset.recipe).toBe("pirates_mend_all");
      expect(chosen.textContent).toContain("Surgeon's Round");

      button(host, "Add it").click();
      await nextTick();
      expect(recordList(host).textContent).toContain("Surgeon's Round");
      expect(host.textContent).not.toContain("pirates");

      // The wizard's own shelf is a separate shelf: a setting chosen for
      // abilities decides nothing about who is offered.
      button(host, "New character").click();
      await nextTick();
      button(host, "Next").click();
      await nextTick();
      expect(
        host.querySelector<HTMLElement>('[aria-checked="true"]')!.dataset.recipe
      ).toBe("medieval_knight");
      app.unmount();
    });

  it("saves the game's turn order through the one project session", async () => {
    const { app, host, onChange, workspace } = mount();
    workspace.selectSection("game");
    await nextTick();
    const order = host.querySelector<HTMLSelectElement>("#field-defaultTurnOrder")!;
    // A new game already states side blocks, so the change under test is a
    // change away from it and the undo has a written value to come back to.
    expect(order.value).toBe("sideBlocks");
    order.value = "initiative";
    order.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save game settings").click();
    await nextTick();

    const saved = onChange.mock.calls.at(-1)?.[0] as SourceProject;
    expect(saved.defaultTurnOrder).toBe("initiative");
    // One session, one undo stack: the settings page is not a second document.
    expect(host.textContent).toContain("Saved game settings");
    button(host, "Undo").click();
    await nextTick();
    const undone = onChange.mock.calls.at(-1)?.[0] as SourceProject;
    expect(undone.defaultTurnOrder).toBe("sideBlocks");
    app.unmount();
  });

  it("names the file after the game when the game is named", async () => {
    // Nobody is asked what the game is called to a machine. Naming it is one
    // answer, and the export stops being called `untitled_game.z64` without
    // the author having opened the fold the id lives behind.
    const { app, host, onChange, workspace } = mount();
    workspace.selectSection("game");
    await nextTick();
    const title = host.querySelector<HTMLInputElement>("#field-title")!;
    title.value = "The Salt Road";
    title.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    button(host, "Save game settings").click();
    await nextTick();

    const saved = onChange.mock.calls.at(-1)?.[0] as SourceProject;
    expect(saved.title).toBe("The Salt Road");
    expect(saved.gameId).toBe("the_salt_road");
    app.unmount();
  });

  it("keeps an id the author wrote, however often the game is renamed",
    async () => {
      const { app, host, onChange, workspace } = mount();
      workspace.selectSection("game");
      await nextTick();
      const gameId = host.querySelector<HTMLInputElement>("#field-gameId")!;
      gameId.value = "saltroad";
      gameId.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
      button(host, "Save game settings").click();
      await nextTick();
      expect((onChange.mock.calls.at(-1)?.[0] as SourceProject).gameId)
        .toBe("saltroad");

      const title = host.querySelector<HTMLInputElement>("#field-title")!;
      title.value = "The Salt Road";
      title.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
      button(host, "Save game settings").click();
      await nextTick();
      const saved = onChange.mock.calls.at(-1)?.[0] as SourceProject;
      expect(saved.title).toBe("The Salt Road");
      expect(saved.gameId).toBe("saltroad");
      app.unmount();
    });

  it("saves what a fall costs the company, and takes it back", async () => {
    const { app, host, onChange, workspace } = mount();
    workspace.selectSection("game");
    await nextTick();
    const loss = host.querySelector<HTMLSelectElement>("#field-characterLoss")!;
    loss.value = "recoverable";
    loss.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save game settings").click();
    await nextTick();

    const saved = onChange.mock.calls.at(-1)?.[0] as SourceProject;
    expect(saved.characterLoss).toBe("recoverable");
    expect(host.textContent).toContain("Saved game settings");
    button(host, "Undo").click();
    await nextTick();
    const undone = onChange.mock.calls.at(-1)?.[0] as SourceProject;
    expect(undone.characterLoss).toBeUndefined();
    app.unmount();
  });

  it("saves the testing aid, and lets it be turned back off", async () => {
    const { app, host, onChange, workspace } = mount();
    workspace.selectSection("game");
    await nextTick();
    const testing = host.querySelector<HTMLInputElement>(
      "#field-invulnerableForTesting"
    )!;
    testing.checked = true;
    testing.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save testing aids").click();
    await nextTick();
    expect(
      (onChange.mock.calls.at(-1)?.[0] as SourceProject).invulnerableForTesting
    ).toBe(true);

    // Turning it off has to reach the project. A switch that could be turned
    // on and not off would ship somebody an unlosable game.
    testing.checked = false;
    testing.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save testing aids").click();
    await nextTick();
    expect(
      (onChange.mock.calls.at(-1)?.[0] as SourceProject).invulnerableForTesting
    ).toBeUndefined();
    app.unmount();
  });

  // The settings page renders a control, the form submits it, and the pick
  // list in `saveMetadata` decides whether it is stored. A field missing from
  // that list fails silently and cheerfully: the author is told the settings
  // were saved, and the value is gone. A style the author picked was exactly
  // that once, and so was every cleared menu.
  it("stores every choice the settings page offers, and every clearing", async () => {
    const { app, host, onChange } = mount({
      ...createSourceProject(),
      themeId: "winter"
    }, "game");
    await nextTick();

    const style = host.querySelector<HTMLSelectElement>(
      "#field-characterStyleId"
    )!;
    style.value = "pirates";
    style.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save game settings").click();
    await nextTick();
    expect(
      (onChange.mock.calls.at(-1)?.[0] as SourceProject).characterStyleId
    ).toBe("pirates");

    // And an author who empties a menu means it: absent is the value, not a
    // request to leave what was there alone.
    const season = host.querySelector<HTMLSelectElement>("#field-themeId")!;
    expect(season.value).toBe("winter");
    season.value = "";
    season.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save game settings").click();
    await nextTick();
    expect(
      (onChange.mock.calls.at(-1)?.[0] as SourceProject).themeId
    ).toBeUndefined();
    app.unmount();
  });

  it("tells the board's turn order what the game's setting is", async () => {
    const source: SourceProject = {
      ...createSourceProject(),
      classes: [{
        id: "wayfarer",
        name: "Wayfarer",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      }],
      unitTypes: [{ id: "scout", name: "Scout", classId: "wayfarer" }],
      maps: [{
        id: "field",
        name: "Field",
        width: 2,
        height: 2,
        terrain: ["plain", "plain", "plain", "plain"]
      }],
      campaigns: [{
        id: "road",
        name: "The Road",
        roster: [{ id: "lead", name: "Lead", unitTypeId: "scout" }],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "opening",
          nodes: [{
            id: "opening",
            name: "Opening",
            kind: "encounter",
            mapId: "field",
            transitions: [{ id: "onward", targetNodeId: "opening", priority: 0 }]
          }]
        }
      }]
    };
    const { app, host, workspace } = mount(source);
    workspace.selectSection("game");
    await nextTick();
    const order = host.querySelector<HTMLSelectElement>("#field-defaultTurnOrder")!;
    order.value = "initiative";
    order.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Save game settings").click();
    await nextTick();

    workspace.selectSection("stages");
    await nextTick();
    button(recordList(host), "Opening").click();
    await nextTick();
    const boardOrder = host.querySelector<HTMLSelectElement>("#stage-turn-order");
    // The Stage offers to follow the game, and names the order it would take.
    expect(boardOrder?.value).toBe("");
    expect(boardOrder?.options[0]!.textContent).toContain(
      "Everyone mixed together, fastest first"
    );
    app.unmount();
  });

  it("names the weapon what the author called it", async () => {
    const { app, host } = mount(undefined, "equipment");
    const name = host.querySelector<HTMLInputElement>("#new-weapon-name")!;
    name.value = "Wren's spare";
    name.dispatchEvent(new Event("input"));
    await nextTick();
    button(host, "Add it").click();
    await nextTick();
    expect(recordList(host).textContent).toContain("Wren's spare");
    app.unmount();
  });
});

/**
 * Flow, which is a picture of a game rather than a list of its records.
 *
 * A road that forks: an opening Stage leading to one of two endings, with a
 * third stop nothing reaches. Every test below varies it.
 */
function forkingRoad(): SourceProject {
  return {
    ...createSourceProject(),
    title: "The Long Road",
    classes: [{
      id: "guard",
      name: "Guard",
      baseStats: { health: 4, movement: 3, strength: 2, defense: 1 }
    }],
    unitTypes: [{ id: "warden", name: "Warden", classId: "guard" }],
    maps: [{
      id: "ford", name: "The Ford", width: 4, height: 4,
      terrain: Array.from({ length: 16 }, () => "plain")
    }],
    campaigns: [{
      id: "road",
      name: "The Road",
      // A company to march out with. Without one the road below is a road the
      // flow editor refuses to save, which would make every test here a test
      // of that refusal rather than of the picture.
      roster: [{ id: "warden", name: "Warden", unitTypeId: "warden" }],
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "opening",
        nodes: [
          {
            id: "opening", name: "The Ford", kind: "encounter", mapId: "ford",
            transitions: [{ id: "onward", targetNodeId: "good", priority: 0 }]
          },
          { id: "good", name: "The Good End", kind: "terminal", transitions: [] },
          { id: "bad", name: "The Bad End", kind: "terminal", transitions: [] }
        ]
      }
    }]
  };
}

function stop(host: HTMLElement, nodeId: string): HTMLElement {
  const found = host.querySelector<HTMLElement>(`[data-stop="${nodeId}"]`);
  if (!found) throw new Error(`no stop '${nodeId}' on the graph`);
  return found;
}

function wayOut(host: HTMLElement, from: string, id: string): HTMLElement {
  const found = host.querySelector<HTMLElement>(`[data-way-out="${from}/${id}"]`);
  if (!found) throw new Error(`no way out '${from}/${id}' on the graph`);
  return found;
}

/** A drag, the way a browser fires one. */
function drag(from: HTMLElement, onto: HTMLElement) {
  const transfer = { setData: () => {}, effectAllowed: "" };
  const start = new Event("dragstart", { bubbles: true });
  Object.defineProperty(start, "dataTransfer", { value: transfer });
  from.dispatchEvent(start);
  const drop = new Event("drop", { bubbles: true, cancelable: true });
  Object.defineProperty(drop, "dataTransfer", { value: transfer });
  onto.dispatchEvent(drop);
}

/** Where the flow of the project's one campaign ended up. */
function roadOf(project: SourceProject) {
  return project.campaigns![0]!.flow!;
}

/**
 * The workspace opened on Diagnostics with one problem already reported, so
 * the jump from a problem to the place it is about can be pressed.
 */
function mountWithProblem(project: SourceProject, instancePath: string) {
  const host = document.createElement("div");
  document.body.append(host);
  const app = createApp(ContentWorkspace, {
    initialProject: project,
    initialSection: "diagnostics",
    diagnostics: [{
      severity: "error" as const,
      code: "invalid_value",
      sourcePath: "project.json",
      instancePath,
      message: "This is not a thing an objective can be."
    }]
  });
  app.mount(host);
  return { app, host };
}

describe("Flow as a graph", () => {
  it("makes the campaign a road needs rather than asking for one", async () => {
    const { app, host, onChange, workspace } = mount({
      ...createSourceProject(), title: "The Long Road"
    });
    expect((onChange.mock.lastCall?.[0] as SourceProject | undefined)).toBeUndefined();

    workspace.selectSection("flow");
    await nextTick();

    // No button was pressed and no dialog was answered. The record the format
    // needs exists, carrying the game's own name.
    const project = onChange.mock.lastCall?.[0] as SourceProject;
    expect(project.campaigns).toHaveLength(1);
    expect(project.campaigns?.[0]).toMatchObject({
      id: "the_long_road", name: "The Long Road"
    });
    // And the page says what to do next rather than offering a second record
    // to make.
    expect(host.textContent).toContain("No road yet");
    expect(host.textContent).not.toContain("Create campaign");
    app.unmount();
  });

  it("leaves the campaigns a game already has exactly alone", async () => {
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();
    expect(onChange).not.toHaveBeenCalled();
    expect(host.querySelector('[data-stop="opening"]')).not.toBeNull();
    app.unmount();
  });

  it("sends the road somewhere else when a way out is dragged onto a stop", async () => {
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();

    drag(wayOut(host, "opening", "onward"), stop(host, "bad"));
    await nextTick();

    const road = roadOf(onChange.mock.lastCall?.[0] as SourceProject);
    expect(road.nodes[0]!.transitions).toEqual([
      { id: "onward", targetNodeId: "bad", priority: 0 }
    ]);
    // One act, said in the author's own words rather than as "edit campaign".
    expect(host.textContent).toContain("Send The Ford on to The Bad End");
    // And the ending it used to reach is now visibly stranded, because it is.
    expect(host.textContent).toContain("Nothing on this road reaches");
    app.unmount();
  });

  it("joins two stops from the keyboard, with the gesture drag uses", async () => {
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();

    // Pick the way out up…
    const handle = wayOut(host, "opening", "onward");
    handle.click();
    await nextTick();
    expect(handle.getAttribute("aria-pressed")).toBe("true");
    expect(host.textContent).toContain("Holding the way out of The Ford");

    // …and put it down. A graph a mouse can build and a keyboard cannot is a
    // regression, so both routes run through the same state.
    stop(host, "bad").click();
    await nextTick();
    expect(roadOf(onChange.mock.lastCall?.[0] as SourceProject).nodes[0]!
      .transitions[0]!.targetNodeId).toBe("bad");
    app.unmount();
  });

  it("keeps what was typed below the picture when the picture is dragged", async () => {
    // A whole road, so the form under the graph has nothing to refuse: the
    // Ford, a scene between, and an ending, every stop reachable.
    const source = forkingRoad();
    source.campaigns![0]!.flow = {
      contractVersion: "1.0.0",
      entryNodeId: "opening",
      nodes: [
        {
          id: "opening", name: "The Ford", kind: "encounter", mapId: "ford",
          transitions: [{ id: "onward", targetNodeId: "middle", priority: 0 }]
        },
        {
          id: "middle", name: "The Crossing", kind: "story",
          transitions: [{ id: "on", targetNodeId: "good", priority: 0 }]
        },
        { id: "good", name: "The Good End", kind: "terminal", transitions: [] }
      ]
    };
    const { app, host, onChange, workspace } = mount(source);
    workspace.selectSection("flow");
    await nextTick();

    // Work in progress in the list-and-form under the graph, uncommitted.
    button(host.querySelector<HTMLElement>(".campaign-flow")!, "The Ford")
      .click();
    await nextTick();
    const nodeName = host.querySelector<HTMLInputElement>("#campaign-node-name")!;
    nodeName.value = "The Ford at Dusk";
    nodeName.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();

    // A gesture on the picture. It is applied to the road as the project holds
    // it once that work has landed, never to the copy the picture was drawn
    // from, since otherwise the rename above would be thrown away by a drag
    // nobody connected to it.
    drag(wayOut(host, "opening", "onward"), stop(host, "good"));
    await nextTick();

    const road = roadOf(onChange.mock.lastCall?.[0] as SourceProject);
    expect(road.nodes[0]!.name).toBe("The Ford at Dusk");
    expect(road.nodes[0]!.transitions[0]!.targetNodeId).toBe("good");
    app.unmount();
  });

  it("refuses a gesture rather than losing typing the road cannot take", async () => {
    // The form below holds work that will not save, this road already leaving
    // a stop nothing reaches, so leaving it would lose what was typed. The
    // same rule every other move between surfaces here follows: say so, and
    // stay put, rather than quietly picking one of the two edits.
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();
    button(host.querySelector<HTMLElement>(".campaign-flow")!, "The Ford")
      .click();
    await nextTick();
    const nodeName = host.querySelector<HTMLInputElement>("#campaign-node-name")!;
    nodeName.value = "The Ford at Dusk";
    nodeName.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    onChange.mockClear();

    drag(wayOut(host, "opening", "onward"), stop(host, "bad"));
    await nextTick();
    expect(onChange).not.toHaveBeenCalled();
    expect(host.querySelector(".save-status")?.textContent)
      .toContain("Fix the problems shown in the open editor first");
    // And the typing is still there to be fixed or finished.
    expect(host.querySelector<HTMLInputElement>("#campaign-node-name")?.value)
      .toBe("The Ford at Dusk");
    app.unmount();
  });

  it("writes nothing when a way out is put back where it already went", async () => {
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();
    onChange.mockClear();

    drag(wayOut(host, "opening", "onward"), stop(host, "good"));
    await nextTick();
    expect(onChange).not.toHaveBeenCalled();
    expect(host.textContent).toContain("already leads to");
    app.unmount();
  });

  it("gives a stop another way out, and takes one away again", async () => {
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();

    host.querySelector<HTMLElement>('[data-add-way-out="opening"]')!.click();
    await nextTick();
    let road = roadOf(onChange.mock.lastCall?.[0] as SourceProject);
    expect(road.nodes[0]!.transitions).toHaveLength(2);
    // The second one reaches the stop nothing else did, so the road is whole.
    expect(road.nodes.every((node) => node.kind !== "encounter" ||
      node.transitions.length > 0)).toBe(true);

    host.querySelector<HTMLElement>(
      '[data-remove-way-out="opening/onward"]'
    )!.click();
    await nextTick();
    road = roadOf(onChange.mock.lastCall?.[0] as SourceProject);
    expect(road.nodes[0]!.transitions).toHaveLength(1);
    expect(road.nodes[0]!.kind).toBe("encounter");
    app.unmount();
  });

  it("says a stop is a Stage without ever saying encounter", async () => {
    const { app, host, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();
    const graph = host.querySelector<HTMLElement>(".flow-graph-panel")!;
    expect(stop(host, "opening").textContent).toContain("Stage");
    expect(stop(host, "opening").textContent).toContain("on The Ford");
    expect(stop(host, "good").textContent).toContain("ending");
    expect(graph.textContent).not.toContain("encounter");
    expect(graph.textContent).not.toContain("terminal");
    app.unmount();
  });

  it("takes the author from a stop to where that Stage is set up", async () => {
    const { app, host, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();
    host.querySelector<HTMLElement>('[data-setup="opening"]')!.click();
    await nextTick();
    // Flow arranges Stages and never opens a board; this is the door to the
    // one place that does.
    expect(host.querySelector(".stage-editor")).not.toBeNull();
    expect(host.querySelector("#stage-title")?.textContent).toContain("The Ford");
    app.unmount();
  });

  it("keeps several campaigns reachable, behind the one being authored", async () => {
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();
    // One campaign, so no chooser: a menu between one thing is not a menu.
    expect(host.querySelector("#flow-campaign")).toBeNull();

    button(host, "Start another campaign").click();
    await nextTick();
    expect((onChange.mock.lastCall?.[0] as SourceProject).campaigns)
      .toHaveLength(2);
    const chooser = host.querySelector<HTMLSelectElement>("#flow-campaign")!;
    expect([...chooser.options].map((option) => option.textContent?.trim()))
      .toEqual(["The Road", "Another campaign"]);
    app.unmount();
  });

  it("removes a campaign, but never the last one there is", async () => {
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();
    // One campaign and no remove: arriving here would make another the moment
    // it went, so the button would read as one that did nothing.
    expect(host.textContent).not.toContain("Remove The Road");

    button(host, "Start another campaign").click();
    await nextTick();
    button(host, "Remove Another campaign").click();
    await nextTick();
    const project = onChange.mock.lastCall?.[0] as SourceProject;
    expect(project.campaigns?.map((campaign) => campaign.id)).toEqual(["road"]);
    // And the page falls back to the one that is left rather than to nothing.
    expect(host.querySelector<HTMLInputElement>("#flow-campaign-name")?.value)
      .toBe("The Road");
    app.unmount();
  });

  it("renames the campaign from the name a player reads", async () => {
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("flow");
    await nextTick();
    const name = host.querySelector<HTMLInputElement>("#flow-campaign-name")!;
    expect(name.value).toBe("The Road");
    name.value = "The Long Way Round";
    name.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    expect((onChange.mock.lastCall?.[0] as SourceProject).campaigns?.[0]?.name)
      .toBe("The Long Way Round");
    app.unmount();
  });
});

describe("what winning means, where it is decided", () => {
  it("reports every way a Stage is won, on Stages", async () => {
    const source = forkingRoad();
    source.objectives = [
      { id: "rout", name: "Rout them" },
      { id: "unused", name: "Hold for ten rounds" }
    ];
    source.campaigns![0]!.flow!.nodes[0]!.objectiveIds = ["rout"];
    const { app, host, workspace } = mount(source);
    workspace.selectSection("stages");
    await nextTick();

    const ways = host.querySelector<HTMLElement>(".ways-to-win")!;
    expect(ways.textContent).toContain("Rout them");
    expect(ways.textContent).toContain("Decides The Ford");
    // An objective nothing lists compiles and never happens, which no
    // validator will mention, so this is where it is named.
    expect(ways.textContent).toContain("No Stage is won this way");
    app.unmount();
  });

  it("states how a Stage is won without going to make a record first", async () => {
    // The whole point of moving objectives out of Flow: saying how this fight
    // ends is one press on the fight, not a trip to a record column.
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("stages");
    await nextTick();
    button(recordList(host), "The Ford").click();
    await nextTick();

    host.querySelector<HTMLElement>('[data-way-to-win="defeat_all_opponents"]')!
      .click();
    await nextTick();

    const project = onChange.mock.lastCall?.[0] as SourceProject;
    expect(project.objectives).toEqual([{
      id: "defeat_all_opponents",
      name: "Beat everyone on the other side",
      kind: "defeatAllOpponents",
      side: "first"
    }]);
    // And the fight is decided by it, which is the other half of the same act.
    expect(project.campaigns?.[0]?.flow?.nodes[0]?.objectiveIds)
      .toEqual(["defeat_all_opponents"]);
    app.unmount();
  });

  it("undoes making a way to win and using it in one press", async () => {
    const { app, host, onChange, workspace } = mount(forkingRoad());
    workspace.selectSection("stages");
    await nextTick();
    button(recordList(host), "The Ford").click();
    await nextTick();
    host.querySelector<HTMLElement>('[data-way-to-win="survive_rounds"]')!
      .click();
    await nextTick();

    // Two records were written and one decision was made, so one press of undo
    // puts both back. A record left behind with nothing using it would be the
    // author's project quietly growing.
    button(host, "Undo").click();
    await nextTick();
    const project = onChange.mock.lastCall?.[0] as SourceProject;
    expect(project.objectives ?? []).toEqual([]);
    expect(project.campaigns?.[0]?.flow?.nodes[0])
      .not.toHaveProperty("objectiveIds");
    app.unmount();
  });

  it("gets rid of a way to win that decides nothing", async () => {
    const source = forkingRoad();
    source.objectives = [{ id: "unused", name: "Hold for ten rounds" }];
    const { app, host, onChange, workspace } = mount(source);
    workspace.selectSection("stages");
    await nextTick();
    button(host.querySelector<HTMLElement>(".ways-to-win")!, "Remove it").click();
    await nextTick();
    expect((onChange.mock.lastCall?.[0] as SourceProject).objectives)
      .toEqual([]);
    app.unmount();
  });

  it("sends a problem about an objective to the Stage it decides", async () => {
    const source = forkingRoad();
    source.objectives = [{ id: "rout", name: "Rout them" }];
    source.campaigns![0]!.flow!.nodes[0]!.objectiveIds = ["rout"];
    const { app, host } = mountWithProblem(source, "/objectives/0/kind");

    button(host, "Go to project.json/objectives/0/kind").click();
    await nextTick();

    // Not a page with no objective anywhere on it. An objective is what
    // winning one fight means, so the jump lands on that fight, where the
    // condition is stated beside the board it names.
    expect(host.querySelector("#content-title")?.textContent).toBe("Stages");
    expect(host.querySelector("#stage-title")?.textContent).toContain("The Ford");
    expect(host.textContent).toContain("The Ford is won by Rout them");
    app.unmount();
  });

  it("says so plainly when the objective decides no Stage at all", async () => {
    const source = forkingRoad();
    source.objectives = [{ id: "spare", name: "Hold for ten rounds" }];
    const { app, host } = mountWithProblem(source, "/objectives/0/rounds");

    button(host, "Go to project.json/objectives/0/rounds").click();
    await nextTick();
    expect(host.querySelector("#content-title")?.textContent).toBe("Stages");
    expect(host.textContent).toContain("No Stage is decided by it yet");
    // And it is in the list below, which is the one place it can be got rid of.
    expect(host.querySelector(".ways-to-win")?.textContent)
      .toContain("Hold for ten rounds");
    app.unmount();
  });
});

describe("finding a Stage in a game that has many", () => {
  /** A game of `count` Stages, named so a search can tell them apart. */
  function manyStages(count: number): SourceProject {
    const nodes = Array.from({ length: count }, (_, index) => ({
      id: `stage_${index}`,
      name: index === count - 1 ? "The Coldgate" : `Skirmish ${index}`,
      kind: "encounter" as const,
      mapId: "field",
      transitions: [],
      placements: []
    }));
    return {
      ...createSourceProject(),
      maps: [{ id: "field", name: "The Long Field", width: 4, height: 4,
               terrain: Array.from({ length: 16 }, () => "grass") }],
      campaigns: [{
        id: "main",
        name: "The March",
        roster: [],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "stage_0",
          nodes: [...nodes,
                  { id: "end", name: "After", kind: "terminal" as const,
                    transitions: [] }]
        }
      }]
    } as unknown as SourceProject;
  }

  const rows = (host: HTMLElement) =>
    [...host.querySelectorAll(".record-list li button strong")]
      .map((node) => node.textContent?.trim());

  it("windows a long list rather than drawing all of it", async () => {
    // A hundred and twenty Stages against a hundred-a-page window. The rail was
    // the one list in this workspace that drew every row it had.
    const { app, host } = mount(manyStages(120), "stages");
    await nextTick();
    expect(rows(host)).toHaveLength(100);
    expect(host.textContent).toContain("120 Stages in this game");
    expect(host.textContent).toContain("Page 1 of 2");

    button(host, "Next").click();
    await nextTick();
    expect(rows(host)).toHaveLength(20);
    // The last Stage is reachable, which is the whole point of the window.
    expect(rows(host)).toContain("The Coldgate");
    app.unmount();
  });

  it("narrows to what an author typed, and says how many matched", async () => {
    const { app, host } = mount(manyStages(120), "stages");
    await nextTick();
    const search = host.querySelector<HTMLInputElement>("#stage-search")!;
    search.value = "coldgate";
    search.dispatchEvent(new Event("input"));
    await nextTick();
    expect(rows(host)).toEqual(["The Coldgate"]);
    expect(host.textContent).toContain("1 of 120 Stages match");
    app.unmount();
  });

  it("finds a Stage by the ground it is fought on", async () => {
    // The row prints the map's name, so a search that could not find it would
    // be a search that fails on what is in front of somebody.
    const { app, host } = mount(manyStages(3), "stages");
    await nextTick();
    const search = host.querySelector<HTMLInputElement>("#stage-search")!;
    search.value = "long field";
    search.dispatchEvent(new Event("input"));
    await nextTick();
    expect(rows(host)).toHaveLength(3);
    app.unmount();
  });

  it("does not strand an author on a page a narrowed list no longer has",
     async () => {
    const { app, host } = mount(manyStages(120), "stages");
    await nextTick();
    button(host, "Next").click();
    await nextTick();
    expect(host.textContent).toContain("Page 2 of 2");

    const search = host.querySelector<HTMLInputElement>("#stage-search")!;
    search.value = "coldgate";
    search.dispatchEvent(new Event("input"));
    await nextTick();
    // One match is one page, and the row is on screen rather than behind a
    // page that no longer exists.
    expect(rows(host)).toEqual(["The Coldgate"]);
    app.unmount();
  });

  it("offers neither control to a game with no Stages", async () => {
    const { app, host } = mount(manyStages(0), "stages");
    await nextTick();
    expect(host.querySelector("#stage-search")).toBeNull();
    expect(host.textContent).toContain("No Stages yet");
    app.unmount();
  });
});
