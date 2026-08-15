// SPDX-License-Identifier: MIT
import { createApp, nextTick, reactive, ref } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { PlaytestState } from "../domain/playtest-session";
import PlaytestControls from "./PlaytestControls.vue";

afterEach(() => document.body.replaceChildren());

function state(): PlaytestState {
  return {
    campaignId: "demo",
    nodeId: "fight",
    nodeName: "Fight",
    mapName: "Bridge",
    themeId: "temperate",
    characterStyleId: "medieval",
    characterFigureId: "first",
    width: 4,
    height: 3,
    terrain: Array(12).fill("grass"),
    activeSide: "first",
    activeUnitId: "",
    remainingActionPoints: 0,
    round: 0,
  roundsToSurvive: 0,
    activationCount: 0,
    outcome: "ongoing",
    units: [
      { id: "blue", name: "Blue Guard", side: "first", x: 0, y: 1,
        health: 4, onBoard: true, maximumHealth: 4, strength: 4, power: 0, defense: 1,
        resistance: 0, skill: 0, luck: 0, evasion: 0, magic: 0,
        movement: 3, actionPoints: 1, speed: 1, actsAfterAttacking: false,
        hasActed: false, hasMoved: false, spentActionPoints: 0,
        minimumReach: 1, maximumReach: 1, reachBonus: 0, accuracy: 100,
        abilityIds: [], weapons: [], items: [],
        crossings: [], behavior: "hold" as const, patrol: [],
        characterStyleId: "medieval", characterFigureId: "first" },
      { id: "red", name: "Red Guard", side: "second", x: 1, y: 1,
        health: 3, onBoard: true, maximumHealth: 4, strength: 4, power: 0, defense: 1,
        resistance: 0, skill: 0, luck: 0, evasion: 0, magic: 0,
        movement: 3, actionPoints: 1, speed: 1, actsAfterAttacking: false,
        hasActed: false, hasMoved: false, spentActionPoints: 0,
        minimumReach: 1, maximumReach: 1, reachBonus: 0, accuracy: 100,
        abilityIds: [], weapons: [], items: [],
        crossings: [], behavior: "hold" as const, patrol: [],
        characterStyleId: "medieval", characterFigureId: "first" }
    ],
    events: [],
    defeated: [],
    encounter: {} as PlaytestState["encounter"],
    unitIds: new Map(),
    deploying: false,
    deploymentTiles: []
  };
}

function mount(options?: { action?: "move" | "attack" }) {
  const host = document.createElement("div");
  document.body.append(host);
  const current = reactive(state());
  const selected = ref("blue");
  const action = ref<"move" | "attack">(options?.action ?? "move");
  const chooseCell = vi.fn();
  const wait = vi.fn();
  const begin = vi.fn();
  const app = createApp({
    components: { PlaytestControls },
    setup: () => ({ current, selected, action, chooseCell, wait, begin }),
    template: `<PlaytestControls :state="current" :selected-unit-id="selected"
      :action="action" :legal-moves="[[0, 0], [1, 1]]"
      :legal-target-ids="new Set(['red'])"
      @select-unit="selected = $event" @set-action="action = $event"
      @choose-cell="chooseCell" @wait="wait" @begin="begin" />`
  });
  app.mount(host);
  return { app, host, current, selected, action, chooseCell, wait, begin };
}

describe("PlaytestControls", () => {
  it("names the side in one vocabulary, and never doubles the word", async () => {
    const mounted = mount();
    // The legend and the status line share one computed, so a phrase that
    // already carries "side" must not have another appended to it.
    const legend = mounted.host.querySelector("legend")!;
    expect(legend.textContent).toContain("Your side's characters");
    expect(legend.textContent).not.toContain("side's side");
    // "Blue Guard" is a character's authored name and is expected; a side
    // called Blue or Red is the vocabulary this surface no longer speaks.
    expect(mounted.host.textContent).not.toContain("Blue side");
    expect(mounted.host.textContent).not.toContain("Red side");
  });

  it("provides labelled unit, action, and destination controls", async () => {
    const mounted = mount();
    expect(mounted.host.textContent).toContain("Keyboard controls");
    expect(mounted.host.querySelector("[role='status']")?.textContent)
      .toContain("Blue Guard selected");
    expect(mounted.host.querySelector("[data-playtest-unit]")?.getAttribute("aria-label"))
      .toContain("4 of 4 health, position 0, 1");

    const destination = [...mounted.host.querySelectorAll<HTMLButtonElement>(
      "[data-playtest-choice]"
    )][1]!;
    destination.click();
    expect(mounted.chooseCell).toHaveBeenCalledWith(1, 1);

    const wait = [...mounted.host.querySelectorAll<HTMLButtonElement>("button")]
      .find((button) => button.textContent?.includes("Wait and finish their turn"))!;
    wait.click();
    expect(mounted.wait).toHaveBeenCalledOnce();
    mounted.app.unmount();
  });

  // The phase has its own two controls and neither of the board's, because the
  // engine refuses every ordinary command until the fighting begins.
  it("offers the region and a way out of it while the board is arranged", async () => {
    const mounted = mount();
    mounted.current.deploying = true;
    await nextTick();
    expect(mounted.host.querySelector("[role='status']")?.textContent)
      .toContain("Blue Guard selected. 2 places to stand.");
    const buttons = [...mounted.host.querySelectorAll<HTMLButtonElement>("button")]
      .map((button) => button.textContent?.trim());
    expect(buttons).toContain("Stand at 0,0");
    expect(buttons).toContain("Begin the fighting");
    expect(buttons).not.toContain("Move");
    expect(buttons).not.toContain("Attack");
    expect(buttons).not.toContain("Wait and finish their turn");

    [...mounted.host.querySelectorAll<HTMLButtonElement>("button")]
      .find((button) => button.textContent?.trim() === "Stand at 1,1")!
      .click();
    expect(mounted.chooseCell).toHaveBeenCalledWith(1, 1);
    [...mounted.host.querySelectorAll<HTMLButtonElement>("button")]
      .find((button) => button.textContent?.trim() === "Begin the fighting")!
      .click();
    expect(mounted.begin).toHaveBeenCalledOnce();
    mounted.app.unmount();
  });

  it("exposes targets and moves focus to the next side after activation", async () => {
    const mounted = mount();
    const attack = [...mounted.host.querySelectorAll<HTMLButtonElement>("button")]
      .find((button) => button.textContent?.trim() === "Attack")!;
    attack.click();
    await nextTick();
    await nextTick();
    const target = mounted.host.querySelector<HTMLButtonElement>(
      "[data-playtest-choice]"
    )!;
    expect(target.getAttribute("aria-label"))
      .toContain("Attack Red Guard, 3 of 4 health, position 1, 1");
    expect(document.activeElement).toBe(target);

    mounted.current.activeSide = "second";
    mounted.current.activationCount += 1;
    await nextTick();
    await nextTick();
    const secondSideUnit = mounted.host.querySelector<HTMLButtonElement>(
      "[data-playtest-unit]"
    )!;
    expect(secondSideUnit.textContent).toContain("Red Guard");
    expect(document.activeElement).toBe(secondSideUnit);
    mounted.app.unmount();
  });
});
