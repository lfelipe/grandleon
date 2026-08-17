// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import { createSourceProject } from "../domain/source-project-document";
import CharacterRoster from "./CharacterRoster.vue";

afterEach(() => document.body.replaceChildren());

function mount(project: SourceProject, selectedId = "") {
  const host = document.createElement("div");
  document.body.append(host);
  const onSelect = vi.fn();
  const onCreate = vi.fn();
  const app = createApp(CharacterRoster, {
    project,
    selectedId,
    onSelect,
    onCreate
  });
  app.mount(host);
  return { app, host, onSelect, onCreate };
}

function cards(host: HTMLElement): HTMLButtonElement[] {
  return [...host.querySelectorAll<HTMLButtonElement>(".character-card")];
}

const guard = {
  id: "guard",
  name: "Guard",
  baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
};

function populated(patch: Partial<SourceProject> = {}): SourceProject {
  return {
    ...createSourceProject(),
    classes: [guard],
    unitTypes: [
      { id: "rina", name: "Rina", classId: "guard", factionId: "your_side" },
      { id: "bandit", name: "Bandit", classId: "guard", factionId: "the_enemy" },
      { id: "spare", name: "Spare", classId: "guard" }
    ],
    factions: [
      { id: "your_side", name: "Your side", color: "blue" },
      { id: "the_enemy", name: "The enemy", color: "red" }
    ],
    ...patch
  };
}

describe("CharacterRoster", () => {
  it("lists one card per character, whatever the chain behind them", () => {
    const { app, host } = mount(populated());
    expect(cards(host).map((card) => card.querySelector("strong")?.textContent))
      .toEqual(["Rina", "Bandit", "Spare"]);
    app.unmount();
  });

  it("says whose side each of them is on, in the author's own words", () => {
    const { app, host } = mount(populated({
      factions: [
        { id: "your_side", name: "The Free Companies", color: "blue" },
        { id: "the_enemy", name: "The enemy", color: "red" }
      ]
    }));
    // The two sides the wizard writes are named for what they are, whatever
    // the author renamed the record to.
    expect(cards(host)[0]!.textContent).toContain("One of yours");
    expect(cards(host)[1]!.textContent).toContain("An enemy");
    expect(cards(host)[2]!.textContent).toContain("Not on a side yet");
    app.unmount();
  });

  it("draws each of them in their faction's colour", () => {
    const { app, host } = mount(populated());
    expect(cards(host)[0]!.querySelector("img")!.getAttribute("src"))
      .toContain("knight_blue");
    expect(cards(host)[1]!.querySelector("img")!.getAttribute("src"))
      .toContain("knight_red");
    app.unmount();
  });

  it("separates the characters something depends on from the extras", () => {
    const { app, host } = mount(populated({
      campaigns: [{
        id: "war",
        name: "War",
        roster: [{ id: "member", name: "Rina", unitTypeId: "rina" }],
        flow: {
          contractVersion: "1.0.0",
          entryNodeId: "one",
          nodes: [{
            id: "one",
            name: "One",
            kind: "encounter",
            mapId: "field",
            transitions: [],
            placements: [
              {
                id: "rina", unitTypeId: "rina", side: "first",
                memberId: "member", x: 0, y: 0
              },
              { id: "a", unitTypeId: "bandit", side: "second", x: 1, y: 0 },
              { id: "b", unitTypeId: "bandit", side: "second", x: 2, y: 0 }
            ]
          }]
        }
      }]
    }));
    expect(cards(host)[0]!.textContent).toContain(
      "Rina marches with a campaign's company, and stands in one Stage."
    );
    // Two bandits on a board are two placements of one character, and the card
    // says so rather than pretending there are two Bandits.
    expect(cards(host)[1]!.textContent).toContain(
      "Bandit stands in one Stage, 2 times in all, and nothing depends on " +
      "which of them is which."
    );
    expect(cards(host)[1]!.classList.contains("standing-extra")).toBe(true);
    expect(cards(host)[0]!.classList.contains("standing-named")).toBe(true);
    expect(cards(host)[2]!.textContent).toContain("Spare is not in any Stage yet.");
    app.unmount();
  });

  it("marks the character open in the editor, and reaches for another", async () => {
    const { app, host, onSelect, onCreate } = mount(populated(), "bandit");
    expect(cards(host)[1]!.getAttribute("aria-current")).toBe("true");
    expect(cards(host)[0]!.getAttribute("aria-current")).toBeNull();
    cards(host)[0]!.click();
    await nextTick();
    expect(onSelect).toHaveBeenCalledWith("rina");

    [...host.querySelectorAll("button")]
      .find((candidate) => candidate.textContent?.trim() === "New character")!
      .click();
    expect(onCreate).toHaveBeenCalledTimes(1);
    app.unmount();
  });

  it("says what the button is for when there is nobody yet", () => {
    const { app, host } = mount(createSourceProject());
    expect(cards(host)).toHaveLength(0);
    expect(host.textContent).toContain("No characters yet.");
    app.unmount();
  });

  it("offers no search over a roster already on screen", () => {
    // A control over three cards, all of them visible, is a control with
    // nothing to do, and this page is the first screen of an empty project.
    const { app, host } = mount(populated());
    expect(cards(host)).toHaveLength(3);
    expect(host.querySelector("#character-roster-search")).toBeNull();
    app.unmount();
  });

  it("searches a roster too long to read, by name, class or identifier", async () => {
    // Somebody with forty characters cannot scroll for one of them, and this
    // grid is the only list of characters the page leads with. Hiding the
    // record columns behind a fold is only allowed because this is here.
    const many = Array.from({ length: 40 }, (unused, index) => ({
      id: `rider_${index}`,
      name: `Rider ${index}`,
      classId: "guard"
    }));
    const { app, host, onSelect } = mount(populated({
      unitTypes: [
        ...many,
        { id: "kesh", name: "Warden Kesh", classId: "guard" }
      ]
    }));
    expect(cards(host)).toHaveLength(41);
    const search = host.querySelector<HTMLInputElement>(
      "#character-roster-search"
    )!;
    expect(search).not.toBeNull();
    expect(host.textContent).toContain("41 of 41 characters");

    search.value = "kesh";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(cards(host).map((card) => card.textContent))
      .toEqual([expect.stringContaining("Warden Kesh")]);
    expect(host.textContent).toContain("1 of 41 characters");
    // And what is found is still the way in to the record.
    cards(host)[0]!.click();
    expect(onSelect).toHaveBeenCalledWith("kesh");

    // The identifier too, because an author who arrived from a reported
    // problem has one of those rather than a name.
    search.value = "rider_7";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(cards(host)).toHaveLength(1);

    // Nobody by that name is said, rather than an empty grid.
    search.value = "nobody at all";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(cards(host)).toHaveLength(0);
    expect(host.textContent).toContain("Nobody here is called that.");
    app.unmount();
  });

  // A card that drew every character at the default body would show a woman as
  // a man on her own card, which is the one screen an author looks at to see
  // who they have. Seeing which figure somebody is *is* the roster's half of
  // making the choice reachable.
  it("draws each character at the body they are, not the game's", () => {
    const { app, host } = mount(populated({
      unitTypes: [
        { id: "wren", name: "Wren", classId: "guard" },
        {
          id: "isolde",
          name: "Isolde",
          classId: "guard",
          characterFigureId: "second"
        }
      ]
    }));
    const pictures = [...host.querySelectorAll<HTMLImageElement>(
      ".character-card img"
    )].map((entry) => entry.getAttribute("src") ?? "");
    expect(pictures).toHaveLength(2);
    expect(pictures[0]).not.toContain("second");
    expect(pictures[1]).toContain("second");
    app.unmount();
  });
});
