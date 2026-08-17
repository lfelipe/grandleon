// SPDX-License-Identifier: MIT
import { createApp, nextTick, ref } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import type {
  CampaignRosterMember,
  EncounterPlacement
} from "../generated/source-v1";
import PlacementEditor from "./PlacementEditor.vue";

afterEach(() => document.body.replaceChildren());

interface MountUnitType {
  readonly id: string;
  readonly name: string;
  readonly classId?: string;
  readonly factionId?: string;
  readonly onePerson?: boolean;
}

interface MountOptions {
  readonly unitTypes?: readonly MountUnitType[];
  readonly factions?: readonly { readonly id: string; readonly color?: string }[];
}

/** What the board asked the surface above it to make and put down. */
interface CastAsk {
  readonly role: string;
  readonly name: string;
  readonly side: "first" | "second";
  readonly x: number;
  readonly y: number;
}

function mount(
  initial: EncounterPlacement[] = [],
  members?: CampaignRosterMember[],
  options: MountOptions = {}
) {
  const host = document.createElement("div");
  document.body.append(host);
  const placements = ref(initial);
  const enrolled = ref<CampaignRosterMember[]>([]);
  const asked = ref<CastAsk[]>([]);
  const unitTypes = ref<readonly MountUnitType[]>(
    options.unitTypes ?? [{ id: "guardian", name: "Guardian" }]
  );
  const factions = options.factions ?? [];
  const app = createApp({
    components: { PlacementEditor },
    setup: () => ({ placements, members, enrolled, unitTypes, factions, asked }),
    template: `
      <PlacementEditor
        :placements="placements"
        :map="{ id: 'field', name: 'Field', width: 3, height: 2, terrain: [] }"
        :unit-types="unitTypes"
        :factions="factions"
        :members="members"
        @update="placements = $event"
        @enroll="enrolled.push($event)"
        @add-character="asked.push($event)" />
    `
  });
  app.mount(host);
  return { app, host, placements, enrolled, asked, unitTypes };
}

function button(host: HTMLElement, text: string): HTMLButtonElement {
  const result = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim().startsWith(text)
  );
  if (!result) throw new Error(`button '${text}' not found`);
  return result;
}

/** The cell at a column and row, by its position in the grid. */
function cell(host: HTMLElement, x: number, y: number): HTMLButtonElement {
  const found = host.querySelector<HTMLButtonElement>(
    `[data-cell="${y * 3 + x}"]`
  );
  if (!found) throw new Error(`no cell at ${x},${y}`);
  return found;
}

describe("PlacementEditor", () => {
  it("adds a character and places it through the map overview", async () => {
    const { app, host, placements } = mount();
    button(host, "Add character placement").click();
    await nextTick();
    cell(host, 2, 1).click();
    await nextTick();

    expect(placements.value).toEqual([{
      id: "unit",
      unitTypeId: "guardian",
      side: "first",
      x: 2,
      y: 1
    }]);
    expect(host.textContent).not.toContain("Outside");
    app.unmount();
  });

  it("shows both occupancy collisions and map bounds errors", () => {
    const { app, host } = mount([
      { id: "first", unitTypeId: "guardian", side: "first", x: 1, y: 1 },
      { id: "second", unitTypeId: "guardian", side: "second", x: 1, y: 1 },
      { id: "lost", unitTypeId: "guardian", side: "second", x: 3, y: 0 }
    ]);

    expect(host.textContent).toContain("Another character already occupies this tile.");
    expect(host.textContent).toContain("Outside 3×2 map bounds.");
    expect(host.querySelector(".placement-cell.collision")).not.toBeNull();
    app.unmount();
  });

  it("moves a placement by dragging it onto another tile", async () => {
    const { app, host, placements } = mount();
    button(host, "Add character placement").click();
    await nextTick();
    const unit = host.querySelector<HTMLElement>(".placed-unit")!;
    const transfer = { setData: () => {}, effectAllowed: "" };
    const dragStart = new Event("dragstart", { bubbles: true });
    Object.defineProperty(dragStart, "dataTransfer", { value: transfer });
    unit.dispatchEvent(dragStart);
    await nextTick();

    const drop = new Event("drop", { bubbles: true });
    Object.defineProperty(drop, "dataTransfer", { value: transfer });
    cell(host, 2, 1).dispatchEvent(drop);
    await nextTick();

    expect(placements.value[0]).toMatchObject({ x: 2, y: 1 });
    app.unmount();
  });

  it("keeps clicking as a route for anyone who cannot drag", async () => {
    const { app, host, placements } = mount();
    button(host, "Add character placement").click();
    await nextTick();
    cell(host, 1, 0).click();
    await nextTick();
    expect(placements.value[0]).toMatchObject({ x: 1, y: 0 });
    app.unmount();
  });

  it("keeps focus while a multi-character world flag is typed", async () => {
    // The panel's one remaining free-text field. A panel that remounted per
    // keystroke would throw the author's focus away mid-word, and the flag is
    // the field long enough for that to be noticed.
    const { app, host, placements } = mount();
    button(host, "Add character placement").click();
    await nextTick();
    const talkable = host.querySelector<HTMLInputElement>(
      "#placement-0-talkable"
    )!;
    talkable.checked = true;
    talkable.dispatchEvent(new Event("change"));
    await nextTick();
    const input = host.querySelector<HTMLInputElement>(
      "#placement-0-talk-flag"
    )!;
    input.focus();
    for (const partial of ["g", "ga", "gat", "gate", "gate_a"]) {
      input.value = partial;
      input.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
    }
    expect(host.querySelector("#placement-0-talk-flag")).toBe(input);
    expect(document.activeElement).toBe(input);
    expect(placements.value[0]?.talk?.flagId).toBe("gate_a");
    app.unmount();
  });

  it("does not ask an author to type where somebody stands, or what to file "
    + "them under", async () => {
    // Both restate what the board already draws. Pressing a tile is how an
    // author says where somebody stands, and the identifier is the format's
    // name for the token rather than anybody's name, and "What to call them"
    // is where a person is named. Both are still read back on the panel.
    const { app, host, placements } = mount();
    button(host, "Add character placement").click();
    await nextTick();
    expect(host.querySelector("#placement-0-id")).toBeNull();
    expect(host.querySelector("#placement-0-x")).toBeNull();
    expect(host.querySelector("#placement-0-y")).toBeNull();
    cell(host, 2, 1).click();
    await nextTick();
    expect(placements.value[0]).toMatchObject({ x: 2, y: 1 });
    const panel = host.querySelector<HTMLElement>(".placement-fields")!;
    expect(panel.textContent).toContain("standing at column 3, row 2");
    expect(panel.textContent).toContain(`Filed as '${placements.value[0]!.id}'`);
    app.unmount();
  });

  it("asks who stands here only where a company exists to ask about", async () => {
    // A board with no campaign around it, such as the map editor's own
    // preview, has nobody to field, so it is never asked the question.
    const { app, host } = mount();
    button(host, "Add character placement").click();
    await nextTick();
    expect(host.querySelector("#placement-0-member")).toBeNull();
    app.unmount();
  });

  it("names a member the campaign no longer holds rather than dropping them", async () => {
    const { app, host } = mount(
      [{
        id: "ghost",
        memberId: "departed",
        unitTypeId: "guardian",
        side: "first",
        x: 0,
        y: 0
      }],
      [{ id: "wren", name: "Wren", unitTypeId: "guardian" }]
    );
    const picker = host.querySelector<HTMLSelectElement>("#placement-0-member")!;
    // The stored choice is still shown, and still says what is wrong with it.
    expect(picker.value).toBe("departed");
    expect(picker.textContent).toContain("departed: nobody in this campaign");
    expect(host.textContent).toContain("'departed' is nobody in this campaign");
    app.unmount();
  });

  it("explains a company with nobody in it to field", async () => {
    const { app, host } = mount([], []);
    button(host, "Add character placement").click();
    await nextTick();
    expect(host.textContent).toContain("This campaign's company is empty");
    expect(host.textContent).toContain("Nobody stands here");
    app.unmount();
  });

  it("renders an identifier pattern the browser will actually enforce", async () => {
    const { app, host } = mount();
    button(host, "Add character placement").click();
    await nextTick();
    const talkable = host.querySelector<HTMLInputElement>(
      "#placement-0-talkable"
    )!;
    talkable.checked = true;
    talkable.dispatchEvent(new Event("change"));
    await nextTick();
    const pattern = host.querySelector<HTMLInputElement>(
      "#placement-0-talk-flag"
    )!.getAttribute("pattern")!;
    // Browsers compile `pattern` with the RegExp v flag; a pattern that fails
    // to compile silently disables validation.
    expect(() => new RegExp(pattern, "v")).not.toThrow();
    expect(new RegExp(pattern, "v").test("gate_house.a-b")).toBe(true);
    expect(new RegExp(pattern, "v").test("Bad Id")).toBe(false);
    app.unmount();
  });

  it("is a grid with one tab stop and arrow keys, as the map editor is", async () => {
    const { app, host } = mount();
    const grid = host.querySelector<HTMLElement>('[role="grid"]')!;
    expect(grid.getAttribute("aria-label")).toBe("Who stands where on Field");
    // Rows and cells, not a flat list of buttons.
    expect(host.querySelectorAll('[role="row"]')).toHaveLength(2);
    const cells = [...host.querySelectorAll<HTMLElement>('[role="gridcell"]')];
    expect(cells).toHaveLength(6);
    expect(cells.filter((candidate) => candidate.tabIndex === 0)).toHaveLength(1);
    expect(cells[0]!.tabIndex).toBe(0);

    cells[0]!.dispatchEvent(
      new KeyboardEvent("keydown", { key: "ArrowRight", bubbles: true })
    );
    await nextTick();
    expect(document.activeElement).toBe(cells[1]);
    expect(cells.filter((candidate) => candidate.tabIndex === 0)).toHaveLength(1);

    cells[1]!.dispatchEvent(
      new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true })
    );
    await nextTick();
    expect(document.activeElement).toBe(cells[4]);

    // The board's edge holds: an arrow off it moves nothing.
    cells[4]!.dispatchEvent(
      new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true })
    );
    await nextTick();
    expect(document.activeElement).toBe(cells[4]);
    app.unmount();
  });

  it("labels a cell by its ground and who stands on it", async () => {
    const { app, host } = mount(
      [{ id: "unit", unitTypeId: "guardian", side: "second", x: 1, y: 0 }],
      undefined,
      { unitTypes: [{ id: "guardian", name: "Guardian" }] }
    );
    expect(cell(host, 0, 0).getAttribute("aria-label"))
      .toBe("Column 1, row 1: grass, empty");
    expect(cell(host, 1, 0).getAttribute("aria-label"))
      .toBe("Column 2, row 1: grass, Guardian of the enemy");
    app.unmount();
  });

  it("draws the character rather than its placement identifier", async () => {
    const { app, host } = mount(
      [{ id: "unit", unitTypeId: "guardian", side: "second", x: 0, y: 0 }],
      undefined,
      {
        unitTypes: [{ id: "guardian", name: "Guardian", classId: "archer",
          factionId: "legion" }],
        factions: [{ id: "legion", color: "violet" }]
      }
    );
    const drawn = host.querySelector<HTMLImageElement>(".placed-unit")!;
    expect(drawn.tagName).toBe("IMG");
    // The class picks the archetype and the faction picks the colour, exactly
    // as the tactical board and the consoles resolve them.
    expect(drawn.getAttribute("src")).toContain("archer");
    expect(drawn.getAttribute("src")).toContain("violet");
    // The identifier is still reachable, as a hint rather than as the drawing.
    expect(drawn.getAttribute("title")).toBe("unit");
    app.unmount();
  });

  it("stamps a character onto the opposing side as many times as pressed",
    async () => {
      const { app, host, placements, enrolled } = mount([], [], {
        unitTypes: [
          { id: "guardian", name: "Guardian" },
          { id: "bandit", name: "Bandit" }
        ]
      });
      host.querySelector<HTMLButtonElement>('[data-unit-type="bandit"]')!.click();
      await nextTick();
      button(host, "The enemy").click();
      await nextTick();
      cell(host, 0, 0).click();
      cell(host, 1, 0).click();
      cell(host, 2, 1).click();
      await nextTick();

      expect(placements.value).toHaveLength(3);
      expect(placements.value.every(
        (placement) => placement.unitTypeId === "bandit" &&
          placement.side === "second"
      )).toBe(true);
      // Nobody stamped names a member: that is what makes them an extra rather
      // than somebody the game depends on.
      expect(placements.value.some(
        (placement) => placement.memberId !== undefined
      )).toBe(false);
      expect(new Set(placements.value.map((placement) => placement.id)).size)
        .toBe(3);
      expect(enrolled.value).toEqual([]);
      app.unmount();
    });

  it("refuses to stamp onto a tile somebody already holds, and says so",
    async () => {
      const { app, host, placements } = mount(
        [{ id: "unit", unitTypeId: "guardian", side: "second", x: 0, y: 0 }],
        []
      );
      host.querySelector<HTMLButtonElement>('[data-unit-type="guardian"]')!
        .click();
      await nextTick();
      button(host, "The enemy").click();
      await nextTick();
      cell(host, 0, 0).click();
      await nextTick();
      expect(placements.value).toHaveLength(1);
      expect(host.textContent).toContain(
        "Somebody already stands on column 1, row 1"
      );
      app.unmount();
    });

  it("enrols a stamped character into the company when the side is yours",
    async () => {
      const { app, host, placements, enrolled } = mount([], []);
      host.querySelector<HTMLButtonElement>('[data-unit-type="guardian"]')!
        .click();
      await nextTick();
      button(host, "Your side").click();
      await nextTick();
      cell(host, 0, 0).click();
      await nextTick();

      // Your side is fought by the company, so a placement there names one of
      // them, and the company gaining somebody is said rather than silent.
      expect(enrolled.value).toEqual([
        { id: "guardian", name: "Guardian", unitTypeId: "guardian" }
      ]);
      expect(placements.value[0]!.memberId).toBe("guardian");
      expect(host.textContent).toContain("Guardian joined it");

      // A second press cannot field the same person twice, so it enrols
      // another one rather than writing a board the campaign would refuse.
      cell(host, 1, 0).click();
      await nextTick();
      expect(enrolled.value).toHaveLength(2);
      expect(enrolled.value[1]!.id).toBe("guardian_2");
      expect(placements.value[1]!.memberId).toBe("guardian_2");
      app.unmount();
    });

  it("fields a member the company already has before enrolling another",
    async () => {
      const { app, host, placements, enrolled } = mount(
        [],
        [{ id: "wren", name: "Wren", unitTypeId: "guardian" }]
      );
      host.querySelector<HTMLButtonElement>('[data-unit-type="guardian"]')!
        .click();
      await nextTick();
      button(host, "Your side").click();
      await nextTick();
      cell(host, 0, 0).click();
      await nextTick();
      expect(enrolled.value).toEqual([]);
      expect(placements.value[0]!.memberId).toBe("wren");
      expect(host.textContent).toContain("Wren of the company stands there");
      app.unmount();
    });

  it("stamps without a company where no campaign asks who stands where",
    async () => {
      // The board a map shows outside any campaign names nobody, on either
      // side, because there is no company for it to name anybody out of.
      const { app, host, placements, enrolled } = mount();
      host.querySelector<HTMLButtonElement>('[data-unit-type="guardian"]')!
        .click();
      await nextTick();
      button(host, "Your side").click();
      await nextTick();
      cell(host, 0, 0).click();
      await nextTick();
      expect(placements.value[0]!.memberId).toBeUndefined();
      expect(enrolled.value).toEqual([]);
      app.unmount();
    });

  it("opens on putting somebody down, and follows what the author last did",
    async () => {
      // An empty board has nothing to move, and a palette that looked armed
      // while a press moved something instead would be a trap.
      const { app, host, placements } = mount();
      expect(button(host, "Puts a character down").getAttribute("aria-pressed"))
        .toBe("true");
      cell(host, 1, 1).click();
      await nextTick();
      expect(placements.value).toHaveLength(1);

      // Adding a placement from the list below is a statement that this one is
      // the one being arranged, so the next press moves it.
      button(host, "Add character placement").click();
      await nextTick();
      expect(button(host, "Moves the selected character")
        .getAttribute("aria-pressed")).toBe("true");
      cell(host, 2, 0).click();
      await nextTick();
      expect(placements.value).toHaveLength(2);
      expect(placements.value[1]).toMatchObject({ x: 2, y: 0 });

      // And picking somebody out of the palette says the opposite again.
      host.querySelector<HTMLButtonElement>('[data-unit-type="guardian"]')!
        .click();
      await nextTick();
      expect(button(host, "Puts a character down").getAttribute("aria-pressed"))
        .toBe("true");
      app.unmount();
    });

  it("puts a bandit down without a bandit having been made first",
    async () => {
      // The whole complaint, in one gesture: a game with nobody in it, and
      // "put a bandit here" answered by a bandit rather than by a trip to
      // Characters to build a weapon type, a weapon, a class and a character.
      const { app, host, asked } = mount([], undefined, { unitTypes: [] });
      const shelf = [...host.querySelectorAll<HTMLElement>(".palette-new")];
      expect(shelf.length).toBeGreaterThan(0);
      host.querySelector<HTMLButtonElement>(
        '[data-palette="new:medieval_rogue"]'
      )!.click();
      await nextTick();
      const name = host.querySelector<HTMLInputElement>("#palette-new-name")!;
      name.value = "Bandit";
      name.dispatchEvent(new Event("input"));
      await nextTick();
      cell(host, 1, 1).click();
      await nextTick();

      // The board asks and does not write: making a character is four records
      // on three collections, and only the surface holding the session can
      // land them and the placement as one undoable act.
      expect(asked.value).toEqual([{
        role: "rogue",
        setting: "medieval",
        name: "Bandit",
        side: "second",
        x: 1,
        y: 1
      }]);
      expect(host.textContent).toContain("Making Bandit and putting them on");
      app.unmount();
    });

  it("picks up the character it asked for, so the next press puts down another",
    async () => {
      const { app, host, asked, unitTypes, placements } = mount(
        [], undefined, { unitTypes: [] }
      );
      host.querySelector<HTMLButtonElement>(
        '[data-palette="new:medieval_rogue"]'
      )!.click();
      await nextTick();
      const name = host.querySelector<HTMLInputElement>("#palette-new-name")!;
      name.value = "Bandit";
      name.dispatchEvent(new Event("input"));
      await nextTick();
      cell(host, 0, 0).click();
      await nextTick();

      // The surface above answers: the character now exists and the board is
      // handed the ordinary props back.
      unitTypes.value = [
        { id: "bandit", name: "Bandit", factionId: "the_enemy" }
      ];
      placements.value = [
        { id: "unit", unitTypeId: "bandit", side: "second", x: 0, y: 0 }
      ];
      await nextTick();

      // Three bandits are three placements of one Bandit, so the palette holds
      // the one it just asked for rather than offering to make a second.
      expect(
        host.querySelector('[data-palette="unit:bandit"]')
          ?.getAttribute("aria-checked")
      ).toBe("true");
      cell(host, 1, 0).click();
      await nextTick();
      cell(host, 2, 0).click();
      await nextTick();
      expect(asked.value).toHaveLength(1);
      expect(placements.value.map((placement) => placement.unitTypeId))
        .toEqual(["bandit", "bandit", "bandit"]);
      app.unmount();
    });

  it("refuses to stand one person on the same board twice", async () => {
    // Warden Kesh marches with the company, so she is one woman rather than a
    // kind, and the board must not let her be in two places at once. Which of
    // the two she is comes from the project, never from a field somebody had
    // to tick.
    const { app, host, placements } = mount(
      [],
      [{ id: "kesh", name: "Warden Kesh", unitTypeId: "warden" }],
      { unitTypes: [{ id: "warden", name: "Warden", onePerson: true }] }
    );
    host.querySelector<HTMLButtonElement>('[data-unit-type="warden"]')!.click();
    await nextTick();
    button(host, "The enemy").click();
    await nextTick();
    cell(host, 0, 0).click();
    await nextTick();
    expect(placements.value).toHaveLength(1);

    const entry = host.querySelector('[data-palette="unit:warden"]')!;
    expect(entry.getAttribute("aria-disabled")).toBe("true");
    expect(entry.className).toContain("blocked");
    cell(host, 1, 0).click();
    await nextTick();
    expect(placements.value).toHaveLength(1);
    expect(host.textContent).toContain("Warden already stands on this board");
    app.unmount();
  });

  it("refuses one person twice on the author's own side too", async () => {
    // The same rule, on the side the last one dodged. A placement on the
    // player's own side used to be exempt: the argument was that each such
    // placement fields a *different* member of the company, so a second one is
    // a second person. It is not - the palette stamps one unit type, and a
    // second stamp of Warden Kesh is Warden Kesh in two places, numbered.
    const { app, host, placements } = mount(
      [],
      [{ id: "kesh", name: "Warden Kesh", unitTypeId: "warden" }],
      { unitTypes: [{ id: "warden", name: "Warden", onePerson: true }] }
    );
    host.querySelector<HTMLButtonElement>('[data-unit-type="warden"]')!.click();
    await nextTick();
    button(host, "Your side").click();
    await nextTick();
    cell(host, 0, 0).click();
    await nextTick();
    expect(placements.value).toHaveLength(1);

    const entry = host.querySelector('[data-palette="unit:warden"]')!;
    expect(entry.getAttribute("aria-disabled")).toBe("true");
    cell(host, 1, 0).click();
    await nextTick();
    expect(placements.value).toHaveLength(1);
    expect(host.textContent).toContain("Warden already stands on this board");
    app.unmount();
  });

  it("stands as many of a kind as the author likes", async () => {
    // The other half of the same rule: nothing in this game depends on which
    // bandit is which, so there is no reason to stop at one.
    const { app, host, placements } = mount(
      [], undefined, { unitTypes: [{ id: "bandit", name: "Bandit" }] }
    );
    host.querySelector<HTMLButtonElement>('[data-unit-type="bandit"]')!.click();
    await nextTick();
    cell(host, 0, 0).click();
    await nextTick();
    cell(host, 1, 0).click();
    await nextTick();
    cell(host, 2, 0).click();
    await nextTick();
    expect(placements.value).toHaveLength(3);
    expect(host.querySelector('[data-palette="unit:bandit"]')
      ?.getAttribute("aria-disabled")).toBeNull();
    app.unmount();
  });

  it("still stands many of a kind on the author's own side", async () => {
    // The case the removed exemption was defending, asked directly. Temporary
    // bodies on the player's side for one map name nobody, so they are a kind
    // and not a person, and the rule about one person in one place has nothing
    // to say about them.
    const { app, host, placements } = mount(
      [],
      [{ id: "kesh", name: "Warden Kesh", unitTypeId: "warden" }],
      { unitTypes: [{ id: "levy", name: "Levy" }] }
    );
    host.querySelector<HTMLButtonElement>('[data-unit-type="levy"]')!.click();
    await nextTick();
    button(host, "Your side").click();
    await nextTick();
    cell(host, 0, 0).click();
    await nextTick();
    cell(host, 1, 0).click();
    await nextTick();
    cell(host, 2, 0).click();
    await nextTick();
    expect(placements.value).toHaveLength(3);
    expect(host.querySelector('[data-palette="unit:levy"]')
      ?.getAttribute("aria-disabled")).toBeNull();
    app.unmount();
  });

  it("takes the side from the character where the character knows it",
    async () => {
      // A character that fights for a faction has already answered whose side
      // it is on, so there is nothing to pick and no way for the two answers
      // to disagree.
      const { app, host, placements } = mount([], undefined, {
        unitTypes: [
          { id: "raider", name: "Raider", factionId: "the_enemy" },
          { id: "wren", name: "Wren", factionId: "your_side" }
        ],
        factions: [
          { id: "your_side", color: "blue" },
          { id: "the_enemy", color: "red" }
        ]
      });
      expect(host.querySelector(".palette-sides")).toBeNull();
      expect(host.textContent).toContain("Raider always fights for the enemy");
      cell(host, 0, 0).click();
      await nextTick();
      expect(placements.value[0]).toMatchObject({
        unitTypeId: "raider",
        side: "second"
      });

      host.querySelector<HTMLButtonElement>('[data-unit-type="wren"]')!.click();
      await nextTick();
      cell(host, 1, 0).click();
      await nextTick();
      expect(placements.value[1]).toMatchObject({
        unitTypeId: "wren",
        side: "first"
      });
      // Still nothing asked, on either of them.
      expect(host.querySelector(".palette-sides")).toBeNull();
      app.unmount();
    });

  it("asks for a side only where the character has not answered", async () => {
    const { app, host } = mount([], undefined, {
      unitTypes: [{ id: "guardian", name: "Guardian" }]
    });
    expect(host.querySelector(".palette-sides")).not.toBeNull();
    app.unmount();
  });

  it("names a second body in the company rather than repeating the first",
    async () => {
      // Two members both called "Warden" is the company reading as though one
      // person were in it twice, which is exactly what an author sees: the
      // roster shows names and not identifiers.
      const { app, host, enrolled } = mount([], []);
      host.querySelector<HTMLButtonElement>('[data-unit-type="guardian"]')!
        .click();
      await nextTick();
      button(host, "Your side").click();
      await nextTick();
      cell(host, 0, 0).click();
      await nextTick();
      cell(host, 1, 0).click();
      await nextTick();
      expect(enrolled.value.map((member) => member.name))
        .toEqual(["Guardian", "Guardian 2"]);
      app.unmount();
    });

  it("says why a press did nothing rather than doing nothing quietly",
    async () => {
      const { app, host, placements } = mount([], undefined, { unitTypes: [] });
      button(host, "Moves the selected character").click();
      await nextTick();
      // Nobody on the board, so there is nobody for a press to move, and a
      // press that changes nothing and says nothing is the worst answer there
      // is.
      cell(host, 0, 0).click();
      await nextTick();
      expect(placements.value).toEqual([]);
      expect(host.textContent).toContain("There is nobody on this board to move");
      app.unmount();
    });

  it("opens on moving when the board already has somebody on it", () => {
    const { app, host } = mount(
      [{ id: "unit", unitTypeId: "guardian", side: "second", x: 0, y: 0 }]
    );
    expect(button(host, "Moves the selected character")
      .getAttribute("aria-pressed")).toBe("true");
    app.unmount();
  });

  it("draws a question rather than a knight for a character nobody defined",
    async () => {
      // `unitSprite` answers an absent class with the knight archetype, so a
      // placement naming a character this project does not have would be drawn
      // as a confident, wrong figure, and `memberProblems` returns early for
      // second-side placements, which leaves every enemy unexamined.
      const { app, host } = mount([{
        id: "bandit_one",
        unitTypeId: "bandit",
        side: "second",
        x: 0,
        y: 0
      }]);

      expect(host.querySelector(".placed-unit")).toBeNull();
      expect(host.querySelector(".placed-unknown")?.textContent).toBe("?");
      expect(host.querySelector<HTMLElement>(".placement-warning")?.textContent)
        .toContain("'bandit' is not a character in this project");
      // And the menu keeps the stored value visible instead of rendering blank.
      const chooser = host.querySelector<HTMLSelectElement>(
        "#placement-0-unit-type"
      )!;
      expect(chooser.value).toBe("bandit");
      expect(chooser.textContent).toContain("not a character in this project");
      await nextTick();
      app.unmount();
    });

  it("authors who a character may talk to, and the flag it raises", async () => {
    // A real gameplay capability: the engine plays it and a campaign branch
    // reads the flag it raises.
    const { app, host, placements } = mount([{
      id: "captain",
      unitTypeId: "guardian",
      side: "second",
      x: 0,
      y: 0
    }]);

    const talkable = host.querySelector<HTMLInputElement>(
      "#placement-0-talkable"
    )!;
    talkable.checked = true;
    talkable.dispatchEvent(new Event("change"));
    await nextTick();
    expect(placements.value[0]!.talk).toEqual({ flagId: "talked_to" });

    const flag = host.querySelector<HTMLInputElement>("#placement-0-talk-flag")!;
    flag.value = "captain_stood_down";
    flag.dispatchEvent(new Event("input"));
    await nextTick();
    expect(placements.value[0]!.talk).toEqual({ flagId: "captain_stood_down" });

    // An unticked box removes the field rather than storing an empty object,
    // so a placement nobody may talk to reads as one written before talking
    // existed.
    talkable.checked = false;
    talkable.dispatchEvent(new Event("change"));
    await nextTick();
    expect("talk" in placements.value[0]!).toBe(false);
    app.unmount();
  });

  it("says what raising a flag does and what reads one", async () => {
    // The control names a world flag, and naming one is half a mechanism: a
    // flag nothing reads changes nothing, so an author told only that
    // `talked_to` is raised has been told nothing they can use. The other half
    // is a transition's condition, and the help has to name it in the words the
    // flow editor puts on screen: a route, not a term.
    const { app, host } = mount([{
      id: "captain",
      unitTypeId: "guardian",
      side: "second",
      x: 0,
      y: 0
    }]);

    // Before the box is ticked there is no flag, so there is nothing to
    // explain and the long explanation is not in the way.
    expect(host.textContent).not.toContain("A world flag is a name");

    const talkable = host.querySelector<HTMLInputElement>(
      "#placement-0-talkable"
    )!;
    talkable.checked = true;
    talkable.dispatchEvent(new Event("change"));
    await nextTick();

    const flag = host.querySelector<HTMLInputElement>("#placement-0-talk-flag")!;
    // The explanation belongs to the flag field, not to whatever field the
    // page happens to draw next.
    const help = host.querySelector<HTMLElement>(
      `#${flag.getAttribute("aria-describedby")}`
    )!;
    expect(help).not.toBeNull();
    expect(help.textContent).toContain("remembers this name after the Stage ends");
    // Every step of the route, by the words the flow editor shows.
    for (const step of [
      "Flow",
      "Conditional branch",
      "Condition",
      "World flag",
      "World flag identity"
    ]) {
      expect(help.textContent).toContain(step);
    }
    // And the fact that decides whether any of it works.
    expect(help.textContent).toContain("Nothing happens until something reads it");
    app.unmount();
  });

  it("stands each label over the control it names", async () => {
    // The fieldset is a grid of single-column rows. Left as a plain block a
    // label is inline, so every one of them runs up beside the previous
    // control: read down the column, the words and the controls are off by
    // one, and the talk checkbox, whose words live inside its label, reads
    // as part of the row above.
    const { app, host } = mount([{
      id: "captain",
      unitTypeId: "guardian",
      side: "second",
      x: 0,
      y: 0
    }]);
    // Its own class, so the rule cannot reach the palette or the tile-mode
    // row, whose rows of buttons are laid out side by side on purpose.
    const fieldset = host.querySelector<HTMLElement>(".placement-fields")!;
    expect(fieldset).not.toBeNull();
    expect(fieldset.querySelector("legend")?.textContent)
      .toContain("About this placement");
    expect(host.querySelector(".placement-palette")!.className)
      .not.toContain("placement-fields");
    // The checkbox and its words are one control, so the words are inside the
    // label rather than in a sibling that could drift away from it.
    const talkable = host.querySelector<HTMLInputElement>(
      "#placement-0-talkable"
    )!;
    const label = talkable.closest("label")!;
    expect(label.classList.contains("boolean-field")).toBe(true);
    expect(label.textContent).toContain("Somebody a character can talk to");
    // What the checkbox means is the next thing after it, not the next thing
    // after some other field.
    expect(label.nextElementSibling?.textContent)
      .toContain("walk up and talk instead of striking");
    app.unmount();
  });

  it("refuses an identifier the format cannot hold on the field that takes one",
    async () => {
      // One free-text identifier is left on this panel: the world flag talking
      // to somebody raises. The placement's own identifier is no longer typed,
      // so prose can no longer reach it at all, but a project written
      // elsewhere can still carry a bad one, and the board still says so.
      const { app, host } = mount([{
        id: "captain",
        unitTypeId: "guardian",
        side: "second",
        x: 0,
        y: 0
      }]);
      const talkable = host.querySelector<HTMLInputElement>(
        "#placement-0-talkable"
      )!;
      talkable.checked = true;
      talkable.dispatchEvent(new Event("change"));
      await nextTick();
      const flag = host.querySelector<HTMLInputElement>(
        "#placement-0-talk-flag"
      )!;
      flag.value = "Stood Down";
      flag.dispatchEvent(new Event("input"));
      await nextTick();
      expect(host.querySelector(".placement-warning")?.textContent)
        .toContain("The world flag 'Stood Down' is not an identifier");
      app.unmount();
    });

  it("moves the palette selection with the arrow keys, on one tab stop",
    async () => {
      const { app, host } = mount([], [], {
        unitTypes: [
          { id: "guardian", name: "Guardian" },
          { id: "bandit", name: "Bandit" }
        ]
      });
      const palette = [...host.querySelectorAll<HTMLElement>(
        '.palette-units [role="radio"]'
      )];
      // This game's two characters, and then the eight roles it has not got:
      // one list, one tab stop, so the arrow keys cross from the game's own
      // people into the ones a press would make without a second gesture.
      expect(palette).toHaveLength(10);
      expect(palette.slice(0, 2).map((entry) => entry.dataset.unitType))
        .toEqual(["guardian", "bandit"]);
      expect(palette.slice(2).every(
        (entry) => entry.classList.contains("palette-new")
      )).toBe(true);
      expect(palette.filter((entry) => entry.tabIndex === 0)).toHaveLength(1);
      expect(palette[0]!.getAttribute("aria-checked")).toBe("true");
      host.querySelector<HTMLElement>('[role="radiogroup"].palette-units')!
        .dispatchEvent(
          new KeyboardEvent("keydown", { key: "ArrowRight", bubbles: true })
        );
      await nextTick();
      expect(palette[1]!.getAttribute("aria-checked")).toBe("true");
      expect(document.activeElement).toBe(palette[1]);
      expect(palette.filter((entry) => entry.tabIndex === 0)).toHaveLength(1);
      app.unmount();
    });
});
