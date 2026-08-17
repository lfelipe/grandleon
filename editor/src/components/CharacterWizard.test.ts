// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import { createSourceProject } from "../domain/source-project-document";
import CharacterWizard from "./CharacterWizard.vue";

afterEach(() => document.body.replaceChildren());

function mount(project: SourceProject = createSourceProject()) {
  const host = document.createElement("div");
  document.body.append(host);
  const onCancel = vi.fn();
  const onCreate = vi.fn();
  const app = createApp(CharacterWizard, { project, onCancel, onCreate });
  app.mount(host);
  return { app, host, onCancel, onCreate };
}

function button(host: HTMLElement, text: string): HTMLButtonElement {
  const found = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim().startsWith(text)
  );
  if (!found) throw new Error(`button '${text}' not found`);
  return found;
}

async function press(host: HTMLElement, text: string) {
  button(host, text).click();
  await nextTick();
}

function steps(host: HTMLElement): HTMLElement[] {
  return [...host.querySelectorAll<HTMLElement>(".wizard-steps li")];
}

describe("CharacterWizard", () => {
  it("asks three things in order and marks the one in progress", async () => {
    const { app, host } = mount();
    expect(steps(host).map((step) => step.textContent?.replace(/\s+/g, " ").trim()))
      .toEqual(["1 Whose side", "2 What kind", "3 Their name"]);

    const current = () =>
      steps(host).findIndex((step) => step.getAttribute("aria-current") === "step");
    // Exactly one step is current, and it is announced rather than coloured.
    expect(steps(host).filter(
      (step) => step.getAttribute("aria-current") === "step"
    )).toHaveLength(1);
    expect(current()).toBe(0);
    expect(host.querySelector("#character-wizard-title")?.textContent)
      .toContain("Whose side");

    await press(host, "Next");
    expect(current()).toBe(1);
    await press(host, "Next");
    expect(current()).toBe(2);
    expect(steps(host).filter(
      (step) => step.getAttribute("aria-current") === "step"
    )).toHaveLength(1);
    app.unmount();
  });

  it("moves focus to the heading of the step it opens on and steps to", async () => {
    const { app, host } = mount();
    const heading = host.querySelector("#character-wizard-title");
    expect(document.activeElement).toBe(heading);
    await press(host, "Next");
    await nextTick();
    expect(document.activeElement).toBe(heading);
    app.unmount();
  });

  it("offers two sides and neither, and keeps the choice across Back", async () => {
    const { app, host, onCreate } = mount();
    const sides = [...host.querySelectorAll<HTMLInputElement>(
      'input[name="character-wizard-side"]'
    )];
    expect(sides.map((side) => side.value)).toEqual(["your_side", "the_enemy", ""]);
    sides[1]!.click();
    await nextTick();

    await press(host, "Next");
    host.querySelector<HTMLElement>('[data-recipe="medieval_archer"]')!.click();
    await nextTick();
    await press(host, "Back");
    // The choice made on the step returned to is still made.
    expect(host.querySelector<HTMLInputElement>(
      'input[name="character-wizard-side"][value="the_enemy"]'
    )!.checked).toBe(true);
    // And so is the one made after it.
    await press(host, "Next");
    expect(host.querySelector<HTMLElement>('[aria-checked="true"]')!.dataset.recipe)
      .toBe("medieval_archer");

    await press(host, "Next");
    await press(host, "Make them");
    expect(onCreate).toHaveBeenCalledWith({
      role: "archer",
      setting: "medieval",
      name: "",
      sideId: "the_enemy"
    });
    app.unmount();
  });

  it("creates nothing when the author leaves", async () => {
    const { app, host, onCancel, onCreate } = mount();
    await press(host, "Next");
    await press(host, "Cancel");
    expect(onCancel).toHaveBeenCalledTimes(1);
    expect(onCreate).not.toHaveBeenCalled();
    app.unmount();
  });

  it("shows every role with its own picture, on one tab stop", async () => {
    const { app, host } = mount();
    await press(host, "Next");
    const library = host.querySelector<HTMLElement>('[role="radiogroup"]')!;
    const cards = [...library.querySelectorAll<HTMLElement>('[role="radio"]')];
    expect(cards).toHaveLength(8);
    expect(cards.map((card) => card.querySelector("strong")?.textContent?.trim()))
      .toEqual([
        "Knight", "Archer", "Mage", "Stormcaller",
        "Healer", "Commander", "Rogue", "Wolf"
      ]);
    // Each card previews the drawing that role will actually get, so the
    // picture in the library is the picture in the game.
    expect(cards[1]!.querySelector("img")!.getAttribute("src"))
      .toContain("archer");
    expect(cards[7]!.querySelector("img")!.getAttribute("src"))
      .toContain("beast");

    expect(cards.filter((card) => card.tabIndex === 0)).toHaveLength(1);
    expect(cards[0]!.getAttribute("aria-checked")).toBe("true");
    library.dispatchEvent(
      new KeyboardEvent("keydown", { key: "ArrowRight", bubbles: true })
    );
    await nextTick();
    expect(cards[1]!.getAttribute("aria-checked")).toBe("true");
    expect(document.activeElement).toBe(cards[1]);
    expect(cards.filter((card) => card.tabIndex === 0)).toHaveLength(1);
    app.unmount();
  });

  it("previews the library in the game's own style, not the setting's", async () => {
    // A setting supplies names, a style supplies drawings, and the drawing an
    // author sees in the library has to be the one they will get. So the
    // sci-fi shelf of a medieval game shows medieval pictures.
    const { app, host } = mount({
      ...createSourceProject(),
      characterStyleId: "scifi"
    });
    await press(host, "Next");
    const first = () =>
      host.querySelector<HTMLImageElement>('[role="radio"] img')!
        .getAttribute("src");
    expect(first()).toContain("knight_blue_scifi");
    await press(host, "Medieval");
    // Same style, different shelf: the names changed and the drawing did not.
    expect(first()).toContain("knight_blue_scifi");
    app.unmount();
  });

  it("filters the library by setting without changing what a role does", async () => {
    const { app, host } = mount();
    await press(host, "Next");
    host.querySelector<HTMLElement>('[data-recipe="medieval_archer"]')!.click();
    await nextTick();
    await press(host, "Sci-fi");

    // The same role, under the name its setting gives it.
    const chosen = host.querySelector<HTMLElement>('[aria-checked="true"]')!;
    expect(chosen.dataset.recipe).toBe("scifi_archer");
    expect(chosen.textContent).toContain("Sniper");
    expect(chosen.textContent).toContain("Range 2–3 tiles.");
    app.unmount();
  });

  it("shelves the commissioned settings, and flies the one that flies", async () => {
    const { app, host } = mount();
    await press(host, "Next");
    await press(host, "Nature");
    expect(
      host.querySelector<HTMLElement>('[data-recipe="nature_mage"]')!.textContent
    ).toContain("Mage bear");

    await press(host, "Sengoku Japan");
    const samurai = host.querySelector<HTMLElement>('[data-recipe="sengoku_knight"]')!;
    expect(samurai.textContent).toContain("Samurai");
    // A shelf added by a commission says nothing about how it crosses ground:
    // the flight exception is one dress, not a habit later settings copy.
    expect(samurai.textContent).not.toContain("Flies");

    await press(host, "Undead");
    const wraith = host.querySelector<HTMLElement>('[data-recipe="undead_mage"]')!;
    expect(wraith.textContent).toContain("Wraith");
    expect(wraith.textContent).not.toContain("Flies");

    await press(host, "Mythical");
    const dragon = host.querySelector<HTMLElement>('[data-recipe="mythical_beast"]')!;
    expect(dragon.textContent).toContain("Dragon");
    // The one entry in the catalogue that says how it crosses ground, and it
    // says so because the class it is about to write says so.
    expect(dragon.textContent).toContain("Flies.");
    // The picture is still the project's own style, which names none and is
    // therefore medieval: a setting supplies names, a style supplies drawings.
    expect(dragon.querySelector("img")!.getAttribute("src"))
      .toContain("beast_blue.png");
    app.unmount();
  });

  it("says what the press will make before it is pressed", async () => {
    const { app, host } = mount();
    host.querySelector<HTMLInputElement>(
      'input[name="character-wizard-side"][value="the_enemy"]'
    )!.click();
    await nextTick();
    await press(host, "Next");
    await press(host, "Next");
    const summary = host.querySelector(".wizard-panel .field-help")!
      .textContent!.replace(/\s+/g, " ").trim();
    // Records appearing that nobody asked for is the one surprise this screen
    // can spring, so the press still says what it writes beside itself, in a
    // line rather than in a paragraph and a five-item list.
    expect(summary).toBe(
      "Makes their class and a sword too, fighting for The enemy. " +
      "All editable afterwards."
    );
    app.unmount();
  });

  it("names the character what the author typed", async () => {
    const { app, host, onCreate } = mount();
    await press(host, "Next");
    await press(host, "Next");
    const name = host.querySelector<HTMLInputElement>("#character-wizard-name")!;
    name.value = "Wren";
    name.dispatchEvent(new Event("input"));
    await nextTick();
    await press(host, "Make them");
    expect(onCreate.mock.lastCall?.[0]).toMatchObject({
      name: "Wren",
      role: "knight",
      sideId: ""
    });
    app.unmount();
  });

  // The one press for an author who is filling a board rather than making
  // somebody in particular. It is the wizard's own last press with the three
  // questions already answered, so what comes out is an ordinary character.
  it("makes a whole character in one press, from the first step", async () => {
    const { app, host, onCreate } = mount();
    expect(host.querySelector('[data-testid="wizard-random"]')).not.toBeNull();
    await press(host, "Create random");

    const choice = onCreate.mock.lastCall?.[0] as {
      role: string;
      setting: string;
      name: string;
      sideId: string;
    };
    expect(onCreate).toHaveBeenCalledTimes(1);
    expect(choice.name.length).toBeGreaterThan(0);
    // A side, and one of the two the game has: an author filling a board wants
    // somebody who is on one, which is the question the wizard opens on and
    // the one a blank answer would leave them to go back for.
    expect(["your_side", "the_enemy"]).toContain(choice.sideId);
    expect(choice.setting).toBe("medieval");
    app.unmount();
  });

  it("is reachable from every step, and never on its own creates twice", async () => {
    const { app, host, onCreate } = mount();
    await press(host, "Next");
    await press(host, "Next");
    await press(host, "Create random");
    expect(onCreate).toHaveBeenCalledTimes(1);
    app.unmount();
  });

  it("creates nothing when the author cancels instead", async () => {
    const { app, host, onCreate, onCancel } = mount();
    await press(host, "Cancel");
    expect(onCancel).toHaveBeenCalledTimes(1);
    expect(onCreate).not.toHaveBeenCalled();
    app.unmount();
  });
});
