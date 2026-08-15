// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import { createDemoProject } from "../sample-projects";
import {
  MemoryCampaignSlotStore,
  type CampaignSlotStore
} from "../domain/campaign-slot-store";
import { keptCampaignSlot } from "../domain/campaign-playtest-session";
import PlayMode from "./PlayMode.vue";

afterEach(() => document.body.replaceChildren());

const project: SourceProject = {
  schemaVersion: "1.0.0",
  packageId: "f643c13e-ce8d-41d4-b6de-0d69ba5fcade",
  gameId: "playmode.fixture",
  title: "Play mode fixture",
  contentRevision: "0.1.0",
  // Blue is deliberately tougher than red, so a player who trades hits wins.
  // With the opposing side now playing itself, an even fixture would always
  // lose: the player has to close the distance first and therefore strikes
  // second.
  classes: [
    {
      id: "guard",
      name: "Guard",
      baseStats: { health: 12, movement: 3, strength: 4, defense: 1 }
    },
    {
      id: "raider",
      name: "Raider",
      baseStats: { health: 4, movement: 3, strength: 2, defense: 1 }
    }
  ],
  unitTypes: [
    { id: "blue", name: "Blue Guard", classId: "guard" },
    { id: "red", name: "Red Guard", classId: "raider" }
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
    // Who plays it. A campaign says so itself now; one that does not is one
    // no company can be founded from, and Play says exactly that.
    roster: [{ id: "blue_one", name: "Blue One", unitTypeId: "blue" }],
    flow: {
      contractVersion: "1.0.0",
      entryNodeId: "fight",
      nodes: [{
        id: "fight",
        name: "Friendly fight",
        kind: "encounter",
        mapId: "bridge",
        placements: [
          {
            id: "blue_one",
            memberId: "blue_one",
            unitTypeId: "blue",
            side: "first",
            x: 0,
            y: 1
          },
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

function mount(ready = true, source = project, activationDelayMs = 0) {
  const host = document.createElement("div");
  document.body.append(host);
  const exits: number[] = [];
  const app = createApp(PlayMode, {
    project: source,
    ready,
    activationDelayMs,
    onExit: () => exits.push(1)
  });
  app.mount(host);
  return { app, host, exits };
}

/**
 * Take the board with the company as it stands.
 *
 * The company screen stands before every board a kept campaign fights, so every
 * test below that wants a battle goes through it. Its own behaviour is asserted
 * in the campaign block at the bottom of this file; here it is a press.
 */
async function takeTheBoard(host: HTMLElement) {
  const going = [...host.querySelectorAll("button")].find((candidate) =>
    candidate.textContent?.trim().startsWith("To the Stage")
  );
  if (!going) return;
  going.click();
  await nextTick();
}

function cell(host: HTMLElement, x: number, y: number): HTMLButtonElement {
  return host.querySelector<HTMLButtonElement>(
    `[aria-label^="Position ${x}, ${y},"]`
  )!;
}

/** Lets the opposing side's activations resolve. */
async function settle() {
  for (let tick = 0; tick < 12; tick += 1) {
    await nextTick();
    await Promise.resolve();
  }
}

/** Mount, and take the board the company screen is standing in front of. */
async function mountOnTheBoard(
  ready = true,
  source = project,
  activationDelayMs = 0
) {
  const mounted = mount(ready, source, activationDelayMs);
  await nextTick();
  await takeTheBoard(mounted.host);
  return mounted;
}

/** Every stat the information sheet states, as the label a player reads. */
function stats(sheet: HTMLElement): Record<string, string> {
  const read: Record<string, string> = {};
  for (const row of sheet.querySelectorAll(".play-sheet-stats div")) {
    read[row.querySelector("dt")!.textContent!.trim()] =
      row.querySelector("dd")!.textContent!.trim();
  }
  return read;
}

function button(host: HTMLElement, text: string): HTMLButtonElement {
  const found = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim().startsWith(text)
  );
  if (!found) throw new Error(`button '${text}' not found`);
  return found;
}

describe("PlayMode", () => {
  it("starts an encounter on its own without an editor verb", async () => {
    const { app, host } = await mountOnTheBoard();
    expect(host.querySelectorAll('[role="gridcell"]')).toHaveLength(12);
    expect(host.textContent).toContain("Your turn");
    app.unmount();
  });

  it("waits for the engine and starts as soon as it is ready", async () => {
    const host = document.createElement("div");
    document.body.append(host);
    const app = createApp({
      data: () => ({ ready: false }),
      components: { PlayMode },
      template:
        '<PlayMode :project="project" :ready="ready" @exit="() => {}" />',
      computed: { project: () => project }
    });
    app.mount(host);
    await nextTick();
    expect(host.textContent).toContain("Getting the game ready");
    expect(host.querySelector('[role="gridcell"]')).toBeNull();

    (app._instance!.proxy as unknown as { ready: boolean }).ready = true;
    await nextTick();
    await nextTick();
    await takeTheBoard(host);
    expect(host.querySelectorAll('[role="gridcell"]')).toHaveLength(12);
    app.unmount();
  });

  it("offers a visible way back to editing", async () => {
    const { app, host, exits } = await mountOnTheBoard();
    button(host, "← Back to editing").click();
    expect(exits).toHaveLength(1);
    app.unmount();
  });

  it("resolves a tap as a move or an attack without a mode switch", async () => {
    const { app, host } = await mountOnTheBoard();

    // Select, then tap an empty highlighted square: that is a move.
    cell(host, 0, 1).click();
    await nextTick();
    expect(cell(host, 1, 1).classList).toContain("legal");
    cell(host, 1, 1).click();
    await settle();

    // The opposing side took its own turn and struck back, so control is the
    // player's again without anyone driving red.
    expect(host.textContent).toContain("Your turn");
    expect(cell(host, 1, 1).getAttribute("aria-label")).toContain("HP 11 of 12");

    // Select, then tap the adjacent enemy: the same gesture is an attack.
    cell(host, 1, 1).click();
    await nextTick();
    expect(cell(host, 2, 1).classList).toContain("target");
    cell(host, 2, 1).click();
    await settle();

    // Blue's blow leaves the raider on one, and its answer takes one off the
    // guard. Red then strikes back on its own turn and dies to the guard's
    // counter, so the square it stood on is empty and the fight is over, the
    // whole exchange decided by a rule neither side spent a turn on.
    expect(cell(host, 2, 1).getAttribute("aria-label")).not.toContain("HP");
    expect(host.textContent).toContain("Your side wins!");

    app.unmount();
  });

  it("walks a moved token along the route the engine allows", async () => {
    // The animated path, driven frame by frame rather than switched off: the
    // board Play ships is the board this exercises. Animation frames are taken
    // over so the test counts them instead of waiting for a browser to.
    const frames: FrameRequestCallback[] = [];
    const realRequest = globalThis.requestAnimationFrame;
    const realCancel = globalThis.cancelAnimationFrame;
    globalThis.requestAnimationFrame = ((callback: FrameRequestCallback) => {
      frames.push(callback);
      return frames.length;
    }) as typeof globalThis.requestAnimationFrame;
    globalThis.cancelAnimationFrame = (() => {}) as typeof globalThis
      .cancelAnimationFrame;
    const advance = async (count: number) => {
      for (let i = 0; i < count; i += 1) {
        const next = frames.shift();
        if (!next) break;
        next(0);
        await nextTick();
      }
    };
    try {
      const { app, host } = await mountOnTheBoard();
      const token = () =>
        host.querySelector<SVGGElement>(".board-cell.selected g.unit") ??
        host.querySelector<SVGGElement>("g.unit");

      cell(host, 0, 1).click();
      await nextTick();
      cell(host, 1, 1).click();
      await nextTick();

      // The command has already landed. The board says so on the frame it was
      // issued, and the enemy has already answered, which is why a tap during
      // a slide is never swallowed and no assertion in this file had to learn
      // to wait.
      expect(cell(host, 1, 1).getAttribute("aria-label")).toContain("Blue One");
      // And the token it moved is drawn back where it came from, not where it
      // now stands.
      const moved = host
        .querySelector<HTMLElement>('[aria-label^="Position 1, 1,"]')!
        .closest(".board-cell")!
        .querySelector("g.unit")!;
      expect(moved.getAttribute("transform")).toBe("translate(-100 0)");

      // Six frames a tile, ending exactly at home. The enemy's own answer is
      // queued behind this one rather than cutting it off, so the player's
      // move is drawn whole.
      // And it walks rather than sliding: the frame it left on and the frame
      // it arrives on draw the standing sprite, and every frame between draws
      // a cell of the strip. Same arithmetic the consoles run, so this is the
      // same walk they draw.
      expect(moved.querySelector("svg.unit-frame")).toBeNull();
      await advance(1);
      const posed = () => host
        .querySelector<HTMLElement>('[aria-label^="Position 1, 1,"]')!
        .closest(".board-cell")!
        .querySelector<SVGSVGElement>("g.unit svg.unit-frame");
      expect(posed()?.getAttribute("viewBox")).toBe("0 0 32 32");
      await advance(4);
      expect(moved.getAttribute("transform")).not.toBe("translate(-100 0)");
      await advance(1);
      expect(moved.hasAttribute("transform")).toBe(false);
      expect(posed()).toBeNull();
      expect(token()).not.toBeNull();
      app.unmount();
    } finally {
      globalThis.requestAnimationFrame = realRequest;
      globalThis.cancelAnimationFrame = realCancel;
    }
  });

  it("coils the striker for exactly as long as the blow knocks", async () => {
    // The other half of a landed hit. The console draws the striker lunging
    // for the same three frames the struck token is knocked away for, and both
    // are back at rest on the sixth, so a settled board is the board that was
    // always drawn.
    const frames: FrameRequestCallback[] = [];
    const realRequest = globalThis.requestAnimationFrame;
    const realCancel = globalThis.cancelAnimationFrame;
    globalThis.requestAnimationFrame = ((callback: FrameRequestCallback) => {
      frames.push(callback);
      return frames.length;
    }) as typeof globalThis.requestAnimationFrame;
    globalThis.cancelAnimationFrame = (() => {}) as typeof globalThis
      .cancelAnimationFrame;
    const advance = async (count: number) => {
      for (let i = 0; i < count; i += 1) {
        const next = frames.shift();
        if (!next) break;
        next(0);
        await nextTick();
      }
    };
    try {
      const { app, host } = await mountOnTheBoard();
      // Step next to the raider, let the exchange settle, then strike it.
      cell(host, 0, 1).click();
      await nextTick();
      cell(host, 1, 1).click();
      await settle();
      // Drain everything that exchange queued, so the next gesture drawn is
      // the blow this test is about.
      await advance(200);
      cell(host, 1, 1).click();
      await nextTick();
      const striker = () => host
        .querySelector<HTMLElement>('[aria-label^="Position 1, 1,"]')!
        .closest(".board-cell")!
        .querySelector<SVGSVGElement>("g.unit svg.unit-frame");
      expect(striker()).toBeNull();
      cell(host, 2, 1).click();
      await nextTick();

      // Cell 2 of the strip is the lunge, and it is the striker wearing it,
      // not the unit that took the blow.
      expect(striker()?.getAttribute("viewBox")).toBe("64 0 32 32");
      await advance(2);
      expect(striker()?.getAttribute("viewBox")).toBe("64 0 32 32");
      // Three frames knocked, then standing again for the rest of the six.
      await advance(1);
      expect(striker()).toBeNull();
      app.unmount();
    } finally {
      globalThis.requestAnimationFrame = realRequest;
      globalThis.cancelAnimationFrame = realCancel;
    }
  });

  it("shows the enemy's danger zone once a character is picked up", async () => {
    const { app, host } = await mountOnTheBoard();
    // Nothing picked up yet: no warning to give, and no red on the board.
    expect(host.querySelectorAll(".danger-wash")).toHaveLength(0);

    cell(host, 0, 1).click();
    await nextTick();
    // The raider has one action point, so its turn is one command: it may walk
    // its three steps or it may swing, never both. What it can reach next turn
    // is therefore the four tiles around it. What is *lit* is only the
    // part of that this character could actually walk into, because a lit tile
    // is a place to go rather than a fact about somebody else's reach.
    expect(host.querySelectorAll(".danger-wash")).toHaveLength(3);
    expect(host.textContent).toContain(
      "Red squares are places you can go where the enemy can reach you"
    );
    expect(cell(host, 1, 1).getAttribute("aria-label"))
      .toContain("the enemy can reach here");
    // And a tile the enemy threatens that this character cannot reach is not
    // lit at all, which is the whole of the fix: the board answers one
    // question rather than two laid over each other.
    expect(cell(host, 0, 0).getAttribute("aria-label"))
      .not.toContain("the enemy can reach here");
    app.unmount();
  });

  it("widens the danger zone for a character that can move and then strike", async () => {
    // The same raider with a point to walk with and a point to swing with:
    // three steps on a four-by-three board threaten all of it, for this
    // character and this budget, rather than for every character whatever
    // theirs.
    //
    // What is *lit* is the part of that the selected character can walk into:
    // seven of its twelve tiles, the reach it has minus the tile it stands on.
    // A lit tile is a place to go rather than a fact about somebody
    // else's arm. The warning is wider; the board says only the part of it the
    // player is choosing between.
    const marching: SourceProject = {
      ...project,
      classes: project.classes.map((sourceClass) =>
        sourceClass.id === "raider"
          ? {
              ...sourceClass,
              baseStats: { ...sourceClass.baseStats, actionPoints: 2 }
            }
          : sourceClass
      )
    };
    const { app, host } = await mountOnTheBoard(true, marching);
    cell(host, 0, 1).click();
    await nextTick();
    expect(host.querySelectorAll(".danger-wash")).toHaveLength(7);
    app.unmount();
  });

  it("plays an encounter through to a winner against the engine's own side", async () => {
    const { app, host } = await mountOnTheBoard();

    cell(host, 0, 1).click();
    await nextTick();
    cell(host, 1, 1).click();
    await settle();

    for (let round = 0; round < 2; round += 1) {
      cell(host, 1, 1).click();
      await nextTick();
      cell(host, 2, 1).click();
      await settle();
    }

    expect(host.textContent).toContain("Your side wins!");
    expect(host.textContent).toContain("See what happens next");

    // Continue commits the battle to the campaign and shows what it did.
    // Nobody died and this fixture authors no growth, so the screen has one
    // thing to say, where the campaign went, and it is the campaign that
    // decided that, not the board.
    button(host, "Continue").click();
    await nextTick();
    expect(host.textContent).toContain("After Friendly fight");
    expect(host.textContent).toContain("Next: Road opens.");
    expect(host.textContent).not.toContain("Lost for good");

    // And Continue again follows the branch the engine chose: the campaign's
    // terminal node, whose authored name becomes the ending screen.
    button(host, "Continue").click();
    await nextTick();
    expect(host.textContent).toContain("Road opens");
    expect(host.textContent).toContain("The story ends here");
    expect(button(host, "Play again")).toBeTruthy();
    app.unmount();
  });

  it("plays the enemy opening under initiative order instead of waiting forever", async () => {
    // Red is the fastest unit on the board, so the engine opens the encounter
    // with red active. Nobody has clicked anything yet: the opposing side has
    // to play itself immediately or the game soft-locks on "Red is thinking…".
    const opening = structuredClone(project);
    opening.classes.find((entry) => entry.id === "raider")!.baseStats.speed = 9;
    opening.classes.find((entry) => entry.id === "guard")!.baseStats.speed = 1;
    opening.campaigns![0]!.flow!.nodes[0]!.turnOrder = "initiative";

    const { app, host } = mount(true, opening);
    await nextTick();
    await takeTheBoard(host);
    await settle();

    expect(host.textContent).toContain("Your turn");
    expect(host.textContent).not.toContain("Red is thinking");
    app.unmount();
  });

  it("stops the enemy loop when Start over interrupts the enemy turn", async () => {
    // Two red units under sideBlocks order, with a real pause between enemy
    // activations: Start over lands inside that pause, and the superseded
    // loop must not keep driving the encounter it already released.
    const race = structuredClone(project);
    race.campaigns![0]!.flow!.nodes[0]!.turnOrder = "sideBlocks";
    race.campaigns![0]!.flow!.nodes[0]!.placements!.push(
      { id: "red_two", unitTypeId: "red", side: "second", x: 3, y: 2 }
    );

    const { app, host } = await mountOnTheBoard(true, race, 40);

    cell(host, 0, 1).click();
    await nextTick();
    cell(host, 1, 1).click();
    await nextTick();

    // Red's first activation ran synchronously; the loop is now pausing
    // before red's second unit. Restart mid-pause.
    button(host, "Start over").click();
    await new Promise((resolve) => setTimeout(resolve, 120));
    await settle();

    // Starting over founds the campaign again, so the company screen stands in
    // front of the board again, which is itself the claim that the stage is
    // before every board rather than after every battle.
    await takeTheBoard(host);
    await settle();

    // A fresh, playable battle: everyone back at start, control with blue.
    expect(host.textContent).toContain("Your turn");
    expect(cell(host, 0, 1).getAttribute("aria-label")).toContain("HP 12 of 12");
    expect(cell(host, 2, 1).getAttribute("aria-label")).toContain("HP 4 of 4");
    app.unmount();
  });

  it("stops the enemy loop when Play is closed during the enemy turn", async () => {
    const race = structuredClone(project);
    race.campaigns![0]!.flow!.nodes[0]!.turnOrder = "sideBlocks";
    race.campaigns![0]!.flow!.nodes[0]!.placements!.push(
      { id: "red_two", unitTypeId: "red", side: "second", x: 3, y: 2 }
    );

    const { app, host } = await mountOnTheBoard(true, race, 40);

    cell(host, 0, 1).click();
    await nextTick();
    cell(host, 1, 1).click();
    await nextTick();

    // Unmount mid-pause; the loop must notice its battle is gone rather than
    // command a disposed encounter.
    app.unmount();
    await new Promise((resolve) => setTimeout(resolve, 120));
    expect(host.textContent).toBe("");
  });

  it("plays the authored story scene before the first battle", async () => {
    const withStory = structuredClone(project);
    withStory.dialogues = [{
      id: "opening",
      name: "Before the fight",
      lines: [
        { speaker: "Narrator", text: "Two guards meet on the bridge." }
      ]
    }];
    withStory.campaigns![0]!.flow!.entryNodeId = "intro";
    withStory.campaigns![0]!.flow!.nodes.unshift({
      id: "intro",
      name: "Intro",
      kind: "story",
      dialogueIds: ["opening"],
      transitions: [{ id: "go", targetNodeId: "fight", priority: 0 }]
    });

    const { app, host } = await mountOnTheBoard(true, withStory);

    // The scene, decoded by the engine, renders before any board exists.
    expect(host.textContent).toContain("Before the fight");
    expect(host.textContent).toContain("Two guards meet on the bridge.");
    expect(host.querySelector('[role="gridcell"]')).toBeNull();

    button(host, "Continue").click();
    await settle();
    await takeTheBoard(host);
    await settle();
    expect(host.querySelectorAll('[role="gridcell"]')).toHaveLength(12);
    expect(host.textContent).toContain("Your turn");
    app.unmount();
  });

  it("behaves as a modal: takes focus, traps Tab, and hands focus back", async () => {
    const outside = document.createElement("button");
    outside.textContent = "outside";
    document.body.append(outside);
    outside.focus();

    const { app, host } = await mountOnTheBoard();
    const surface = host.querySelector<HTMLElement>(".play-mode")!;
    expect(document.activeElement).toBe(surface);

    const focusable = [...surface.querySelectorAll<HTMLElement>("button")]
      .filter((candidate) => candidate.tabIndex >= 0);
    const first = focusable[0]!;
    const last = focusable.at(-1)!;
    last.focus();
    window.dispatchEvent(new KeyboardEvent("keydown", {
      key: "Tab",
      cancelable: true
    }));
    expect(document.activeElement).toBe(first);
    window.dispatchEvent(new KeyboardEvent("keydown", {
      key: "Tab",
      shiftKey: true,
      cancelable: true
    }));
    expect(document.activeElement).toBe(last);

    // Closing Play returns focus to where the author left the editor.
    app.unmount();
    expect(document.activeElement).toBe(outside);
    outside.remove();
  });

  it("offers a carried weapon beside the abilities and strikes with it", async () => {
    // A dagger in hand and a bow carried: the raider stands two tiles away,
    // which only the bow answers from where the guard is standing.
    const carrying = structuredClone(project);
    carrying.weapons = [
      { id: "dagger", name: "Dagger", power: 6, range: 1 },
      { id: "bow", name: "Bow", power: 1, minimumRange: 2, maximumRange: 2 }
    ];
    carrying.unitTypes[0]!.startingWeaponIds = ["dagger", "bow"];
    // Enough health that the shot leaves a number to read rather than a
    // victory: what is being pinned here is the bow's power, not the outcome.
    carrying.classes[1]!.baseStats.health = 9;
    const { app, host } = await mountOnTheBoard(true, carrying);

    // Nothing is offered until somebody is picked.
    expect(host.querySelector(".play-weapon")).toBeNull();
    cell(host, 0, 1).click();
    await nextTick();

    const dagger = button(host, "Dagger");
    const bow = button(host, "Bow");
    expect(dagger.getAttribute("aria-pressed")).toBe("true");
    expect(bow.getAttribute("aria-pressed")).toBe("false");
    // The dagger cannot reach the raider, so nothing is marked to attack.
    expect(cell(host, 2, 1).classList).not.toContain("target");

    bow.click();
    await nextTick();
    expect(bow.getAttribute("aria-pressed")).toBe("true");
    expect(dagger.getAttribute("aria-pressed")).toBe("false");
    expect(host.textContent).toContain("attack with Bow");
    expect(cell(host, 2, 1).classList).toContain("target");

    cell(host, 2, 1).click();
    await settle();
    // Strength four plus the bow's one against defence one: four off the
    // raider's nine, which only the engine could have worked out.
    expect(host.textContent).toContain("HP 5/9");
    app.unmount();
  });

  it("spends a carried item out of the row between the spells and Stay here", async () => {
    // The guard carries a draught and starts the fight hurt, so the number on
    // the button is a number worth reading and the restore has room to work.
    const carrying = structuredClone(project);
    carrying.items = [
      {
        id: "tonic",
        name: "Field Tonic",
        stackLimit: 5,
        kind: "restore",
        power: 4
      },
      { id: "signet", name: "Signet", stackLimit: 1 }
    ];
    carrying.unitTypes[0]!.startingItemIds = ["tonic", "signet"];
    const { app, host } = await mountOnTheBoard(true, carrying);

    // Nothing is offered until somebody is picked, exactly as for a weapon.
    expect(host.querySelector(".play-item")).toBeNull();
    cell(host, 0, 1).click();
    await nextTick();

    const rows = [...host.querySelectorAll(".play-item")];
    expect(rows).toHaveLength(2);
    // The engine's own forecast is on the label, and it is the exact number
    // the use will deliver rather than an average of one.
    const tonic = button(host, "Field Tonic");
    expect(tonic.textContent).toContain("(1)");
    // An item that does nothing a battle can apply is shown and refused here
    // rather than hidden, so a player can see what they are carrying.
    expect(button(host, "Signet").disabled).toBe(true);

    // The row sits between the spells and the row that ends the turn: the
    // console order, which this client carries.
    const order = [...host.querySelectorAll(".play-actions button")].map(
      (node) => node.className
    );
    expect(order.indexOf("play-item")).toBeLessThan(order.indexOf("play-wait"));

    tonic.click();
    await settle();
    // The draught is gone and the row stayed to say so, which is the count
    // coming back off the engine's snapshot rather than being decremented here.
    cell(host, 0, 1).click();
    await nextTick();
    const spent = button(host, "Field Tonic");
    expect(spent.textContent).toContain("(0)");
    expect(spent.disabled).toBe(true);
    app.unmount();
  });

  it("offers a talk only when somebody beside you is talkable", async () => {
    // Nobody in the ordinary project authors a talk, so the row is absent even
    // with an enemy on the board, which is the case every shipped game is in.
    {
      const { app, host } = await mountOnTheBoard();
      cell(host, 0, 1).click();
      await nextTick();
      expect(host.querySelector(".play-talk")).toBeNull();
      app.unmount();
    }

    // The raider steps up beside the guard and turns out not to be a real
    // enemy. Adjacency is the reach, so moving him is half of what makes the
    // row appear; the authored mark is the other half.
    const parley = structuredClone(project);
    const placements = parley.campaigns![0]!.flow!.nodes[0]!.placements!;
    const raider = placements.find((entry) => entry.side === "second")!;
    raider.x = 1;
    raider.talk = { flagId: "raider-heard-out" };
    const { app, host } = await mountOnTheBoard(true, parley);

    // Still nothing until somebody is picked, exactly as for a weapon.
    expect(host.querySelector(".play-talk")).toBeNull();
    cell(host, 0, 1).click();
    await nextTick();

    const rows = [...host.querySelectorAll(".play-talk")];
    expect(rows).toHaveLength(1);
    // The button names who is being spoken to, which is the whole difference
    // between this and a console row the player aims afterwards.
    const talk = button(host, "Talk to Red Guard");

    // The console order, which this client carries: after the pack, before the
    // row that ends the turn.
    const order = [...host.querySelectorAll(".play-actions button")].map(
      (node) => node.className
    );
    expect(order.indexOf("play-talk")).toBeLessThan(order.indexOf("play-wait"));

    talk.click();
    await settle();

    // He left the board alive: his tile is empty, and the guard may walk onto
    // it. Nothing here reads a "defeated" flag, because there is not one to
    // read, and that is the distinction the whole gesture rests on.
    expect(host.querySelector(".play-talk")).toBeNull();
    app.unmount();
  });

  it("offers no weapon choice to a character carrying one weapon", async () => {
    const single = structuredClone(project);
    single.weapons = [{ id: "dagger", name: "Dagger", power: 6, range: 1 }];
    single.unitTypes[0]!.startingWeaponIds = ["dagger"];
    const { app, host } = await mountOnTheBoard(true, single);
    cell(host, 0, 1).click();
    await nextTick();
    expect(host.querySelector(".play-weapon")).toBeNull();
    expect(host.textContent).toContain("or a marked enemy to attack.");
    app.unmount();
  });

  it("offers a selected character's abilities and casts one at a tile", async () => {
    // Reach two, so the ability outranges the guard's own weapon: casting is
    // the only way to touch the raider without stepping up to it.
    const casting = structuredClone(project);
    casting.abilities = [{
      id: "spark",
      name: "Spark",
      kind: "damage",
      power: 5,
      minimumRange: 1,
      maximumRange: 2
    }];
    casting.unitTypes[0]!.abilityIds = ["spark"];
    const { app, host } = await mountOnTheBoard(true, casting);

    // Nothing is offered until somebody is picked.
    expect(host.querySelector(".play-ability")).toBeNull();
    cell(host, 0, 1).click();
    await nextTick();

    const spark = button(host, "Spark");
    expect(spark.getAttribute("aria-pressed")).toBe("false");
    spark.click();
    await nextTick();
    expect(spark.getAttribute("aria-pressed")).toBe("true");
    expect(host.textContent).toContain("Tap a marked square to use Spark");
    expect(cell(host, 2, 1).classList).toContain("aimed");
    expect(cell(host, 2, 1).getAttribute("aria-label"))
      .toContain("in range of the ability");
    // Out of the band, and therefore not offered.
    expect(cell(host, 3, 1).classList).not.toContain("aimed");

    cell(host, 2, 1).click();
    await settle();
    // Five past one defense against four health: the raider is gone and the
    // battle is decided, which only the engine could have concluded.
    expect(host.textContent).toContain("Your side wins!");
    app.unmount();
  });

  it("puts the aiming ability down again when it is pressed twice", async () => {
    const casting = structuredClone(project);
    casting.abilities = [{
      id: "spark",
      name: "Spark",
      kind: "damage",
      power: 5,
      minimumRange: 1,
      maximumRange: 2
    }];
    casting.unitTypes[0]!.abilityIds = ["spark"];
    const { app, host } = await mountOnTheBoard(true, casting);

    cell(host, 0, 1).click();
    await nextTick();
    button(host, "Spark").click();
    await nextTick();
    expect(cell(host, 1, 1).classList).toContain("aimed");

    button(host, "Spark").click();
    await nextTick();
    expect(cell(host, 1, 1).classList).not.toContain("aimed");
    // Back to the mode-free board: the same tap is a move again.
    expect(cell(host, 1, 1).classList).toContain("legal");
    cell(host, 1, 1).click();
    await settle();
    expect(cell(host, 1, 1).getAttribute("aria-label")).toContain("Blue One");
    app.unmount();
  });

  it("opens the full information sheet from the action row", async () => {
    // Every stat the sheet claims to show, authored to a number nothing else
    // in this fixture uses, so a sheet that read the wrong field or defaulted
    // one cannot pass by coincidence.
    const detailed = structuredClone(project);
    detailed.classes[0]! = {
      id: "guard",
      name: "Guard",
      baseStats: {
        health: 12,
        movement: 3,
        strength: 4,
        defense: 1,
        resistance: 6,
        skill: 7,
        luck: 8,
        evasion: 9,
        magic: 5,
        speed: 11,
        actionPoints: 2
      }
    };
    detailed.weapons = [
      { id: "bow", name: "Bow", power: 1, minimumRange: 2, maximumRange: 3,
        accuracy: 85 }
    ];
    detailed.abilities = [{
      id: "spark",
      name: "Spark",
      kind: "damage",
      power: 5,
      minimumRange: 1,
      maximumRange: 2
    }];
    detailed.unitTypes[0]!.startingWeaponIds = ["bow"];
    detailed.unitTypes[0]!.abilityIds = ["spark"];
    const { app, host } = await mountOnTheBoard(true, detailed);

    // Reached deliberately: nothing is on screen until a character is picked
    // and the row is pressed.
    expect(host.querySelector(".play-sheet")).toBeNull();
    cell(host, 0, 1).click();
    await nextTick();
    expect(host.querySelector(".play-sheet")).toBeNull();

    const info = button(host, "About them");
    expect(info.getAttribute("aria-expanded")).toBe("false");
    info.click();
    await nextTick();

    const sheet = host.querySelector(".play-sheet") as HTMLElement;
    expect(sheet).not.toBeNull();
    expect(info.getAttribute("aria-expanded")).toBe("true");
    // Who, then what: the campaign's own name for this member and the class
    // they belong to. Not their character type: `Blue Guard` is a kind of
    // character and `Blue One` is a person, and the sheet leads with the
    // person exactly as it does on both consoles and in the terminal.
    expect(sheet.querySelector("h2")!.textContent!.replace(/\s+/g, " ").trim())
      .toBe("Blue One Guard");
    expect(stats(sheet)).toEqual({
      // The campaign row: Play now runs the campaign the author wrote, so a
      // character the roster holds states the level and the experience the
      // roster holds for them: the same LEVEL/EXP row the two consoles and
      // the terminal draw, from the same two numbers.
      Level: "1",
      Experience: "0",
      Health: "12 / 12",
      Actions: "2",
      Movement: "3",
      Speed: "11",
      Strength: "4",
      Defense: "1",
      Resistance: "6",
      Magic: "5",
      Skill: "7",
      Luck: "8",
      Evasion: "9"
    });
    // The console draws these as three lines, `HP/AP/MOV/SPD`,
    // `STR/DEF/RES/MAG` and `SKL/LCK/EVA`, and this surface draws the same
    // three groups in the same order with the words spelled out, because it has no
    // 320-pixel budget to spend. A player who learns one reads the other. The
    // campaign row goes first, which is where the console sheet puts it: after
    // the name and before the health.
    expect(
      [...sheet.querySelectorAll(".play-sheet-stats")].map((group) =>
        [...group.querySelectorAll("dt")].map((term) => term.textContent)
      )
    ).toEqual([
      ["Level", "Experience"],
      ["Health", "Actions", "Movement", "Speed"],
      ["Strength", "Defense", "Resistance", "Magic"],
      ["Skill", "Luck", "Evasion"]
    ]);
    const text = sheet.textContent!.replace(/\s+/g, " ");
    // The carried weapon with its band and its accuracy, and the ability with
    // the band it is cast within: the two lists the console sheet heads.
    // The weapon's own accuracy, worded so it cannot be read as the folded
    // chance the hint under the board states, which needs a target.
    expect(text).toContain(
      "Bow: reach 2–3, lands 85 in 100 before skill, luck and evasion"
    );
    expect(text).toContain("Spark: reach 1–2");

    // Putting it down leaves the character exactly where the sheet found it.
    button(host, "Back").click();
    await nextTick();
    expect(host.querySelector(".play-sheet")).toBeNull();
    expect(cell(host, 1, 1).classList).toContain("legal");
    app.unmount();
  });

  it("shows the health the battle has reached rather than the authored one", async () => {
    // A sheet that read the class's base stats instead of the engine's
    // snapshot would still show twelve here, which is the whole difference
    // between a stat sheet and a content listing.
    const { app, host } = await mountOnTheBoard();
    cell(host, 0, 1).click();
    await nextTick();
    cell(host, 1, 1).click();
    await settle();

    cell(host, 1, 1).click();
    await nextTick();
    button(host, "About them").click();
    await nextTick();
    const sheet = host.querySelector(".play-sheet") as HTMLElement;
    expect(stats(sheet).Health).toBe("11 / 12");
    app.unmount();
  });

  it("closes the sheet before it closes Play when Escape is pressed", async () => {
    const { app, host, exits } = await mountOnTheBoard();
    cell(host, 0, 1).click();
    await nextTick();
    button(host, "About them").click();
    await nextTick();

    window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
    await nextTick();
    expect(host.querySelector(".play-sheet")).toBeNull();
    expect(exits).toHaveLength(0);

    window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
    await nextTick();
    expect(exits).toHaveLength(1);
    app.unmount();
  });

  it("reports an unplayable project instead of rendering an empty board", async () => {
    const invalid = structuredClone(project);
    invalid.campaigns = [];
    const { app, host } = await mountOnTheBoard(true, invalid);
    expect(host.querySelector('[role="alert"]')?.textContent).toContain(
      "No campaign Stage"
    );
    app.unmount();
  });

  it("tells a game with nothing to play what to set up next", async () => {
    const empty = structuredClone(project);
    empty.campaigns = [];
    const { app, host, exits } = await mountOnTheBoard(true, empty);

    const guidance = host.querySelector(".play-empty");
    expect(guidance?.textContent).toContain("no Stage to play yet");
    // The way out names the one place a Stage is made, because sending
    // somebody to the wrong section is worse than saying nothing.
    expect(guidance?.querySelector("button")?.textContent).toContain("Stages");
    expect(guidance?.textContent).not.toContain("Flow");
    guidance!.querySelector("button")!.click();
    expect(exits).toHaveLength(1);
    app.unmount();
  });

  // The campaign an author wrote, on the surface they wrote it for. The demo's
  // `muster_road` is the content that has all of it: two riders the player
  // keeps, one of whom can die for good, a growth block, a drop, and a second
  // map that still lists the rider who will not come back.
  describe("running the campaign the author wrote", () => {
    function musterRoad() {
      const demo = createDemoProject();
      const { app, host } = mount(true, demo);
      return { app, host, demo };
    }

    /** Picks the campaign the surface offers a choice between. */
    async function chooseMusterRoad(host: HTMLElement) {
      const picker = host.querySelector<HTMLSelectElement>(".play-campaign select")!;
      expect([...picker.options].map((option) => option.value)).toEqual([
        "demo_campaign",
        "muster_road"
      ]);
      picker.value = "muster_road";
      picker.dispatchEvent(new Event("change"));
      await nextTick();
    }

    /**
     * The company screen, and the press that leaves it.
     *
     * A kept campaign always stands here before a board: after a battle,
     * after a story node, on a resume, and before the very first one. So a
     * test that wants the fight says so, in the same words a player reads.
     */
    async function takeTheField(host: HTMLElement) {
      expect(host.querySelector(".play-manage")).not.toBeNull();
      button(host, "To the Stage").click();
      await settle();
    }

    // The crossing authors a region on the western bank, so it opens on the
    // deployment phase and nobody may be given an order until somebody says
    // the line is set. Nobody is moved here: the fights below are pinned to
    // the line the content authored.
    async function openTheBattle(host: HTMLElement) {
      expect(host.textContent).toContain("Deployment. Pick someone.");
      button(host, "Begin the fighting").click();
      await settle();
    }

    it("offers the campaigns a game has and plays the one that is picked", async () => {
      const { app, host } = musterRoad();
      await nextTick();
      // The demo opens on its conformance slice, which is one rider a side.
      await takeTheField(host);
      expect(host.querySelectorAll("g.unit")).toHaveLength(2);
      await chooseMusterRoad(host);
      await takeTheField(host);
      await openTheBattle(host);
      // The muster road is three: two riders the player keeps, and a picket.
      expect(host.querySelectorAll("g.unit")).toHaveLength(3);
      expect(host.querySelector(".play-excluded")).toBeNull();
      app.unmount();
    });

    it("says between the scenes who a story node brought in", async () => {
      // A node that fights nothing has no aftermath screen, so its recruitment
      // is said above the board the recruit first stands on. The demo recruits
      // at a battle, so the recruitment is moved onto a story node between the
      // two maps, the shape `games/tarnholt` authors at `marching_order`.
      const demo = createDemoProject();
      const campaign = demo.campaigns!.find(
        (candidate) => candidate.id === "muster_road"
      )!;
      const nodes = campaign.flow!.nodes;
      const crossing = nodes.find((node) => node.id === "river_skirmish")!;
      nodes.splice(nodes.indexOf(crossing) + 1, 0, {
        id: "the_bank",
        name: "The Far Bank",
        kind: "story",
        recruits: crossing.recruits!,
        transitions: [
          { id: "ride_on_again", targetNodeId: "road_watch", priority: 0 }
        ]
      });
      delete crossing.recruits;
      crossing.transitions[0]!.targetNodeId = "the_bank";

      const { app, host } = mount(true, demo);
      await nextTick();
      await chooseMusterRoad(host);
      await takeTheField(host);
      await openTheBattle(host);
      expect(host.querySelector(".play-joined")).toBeNull();

      cell(host, 2, 1).click();
      await nextTick();
      cell(host, 3, 1).click();
      await settle();
      // The outrider stands still with the picket on one, and the swing that
      // comes back is the one it does not survive.
      cell(host, 2, 1).click();
      await nextTick();
      host.querySelector<HTMLButtonElement>(".play-wait")!.click();
      await settle();
      // Then the vanguard rides onto the tile its companion fell from and
      // strikes in the same turn, which is what the second action point buys.
      cell(host, 0, 1).click();
      await nextTick();
      cell(host, 2, 1).click();
      await nextTick();
      cell(host, 3, 1).click();
      await settle();

      button(host, "Continue").click();
      await nextTick();
      // The battle brought nobody in; the node after it does.
      expect(host.textContent).not.toContain("joined the company.");
      button(host, "Continue").click();
      await settle();
      expect(host.querySelector(".play-joined")).toBeNull();
      // The story node completed and the campaign now stands on the road, so
      // the company screen is what a player sees, with the man it just brought
      // in already on it, and the road's placements already known.
      expect(host.querySelector(".play-manage")!.textContent).toContain(
        "Torvald the Ferryman"
      );
      await takeTheField(host);
      expect(host.querySelector(".play-joined")!.textContent).toContain(
        "Torvald the Ferryman joined the company."
      );
      app.unmount();
    });

    it("shows the aftermath and then a board the fallen are not on", async () => {
      const { app, host } = musterRoad();
      await nextTick();
      await chooseMusterRoad(host);
      await takeTheField(host);
      await openTheBattle(host);

      // Nobody has been lost yet, and nothing on the board says anybody has.
      expect(host.querySelector(".play-losses")).toBeNull();

      // The crossing: the outrider trades with the picket and both are left on
      // one, the outrider holds and the picket's next swing fells it, and the
      // vanguard rides onto the emptied tile and finishes the picket.
      cell(host, 2, 1).click();
      await nextTick();
      cell(host, 3, 1).click();
      await settle();
      // The outrider stands still with the picket on one, and the swing that
      // comes back is the one it does not survive.
      cell(host, 2, 1).click();
      await nextTick();
      host.querySelector<HTMLButtonElement>(".play-wait")!.click();
      await settle();
      // Then the vanguard rides onto the tile its companion fell from and
      // strikes in the same turn, which is what the second action point buys.
      cell(host, 0, 1).click();
      await nextTick();
      cell(host, 2, 1).click();
      await nextTick();
      cell(host, 3, 1).click();
      await settle();

      // A permanent loss is said on the surface a player is looking at while
      // it happens. Play draws a board and no log, so without this line a
      // character simply stops being on it and the first word about it waits
      // for the screen after the battle. The name is the author's own, through
      // the engine's board-to-member join, and the opposing picket that fell
      // in the same exchange is not named here. The losses that are permanent
      // and the player's are the ones this line is for.
      const lost = host.querySelector(".play-losses")!;
      expect(lost.textContent).toContain("Outrider Bevan died.");
      expect(lost.textContent).not.toContain("River Watch");

      expect(host.textContent).toContain("Your side wins!");
      button(host, "Continue").click();
      await nextTick();

      // The aftermath, in the words the terminal narrates it in and from the
      // numbers the engine derived.
      const text = host.textContent!.replace(/\s+/g, " ");
      expect(text).toContain("After The Skirmish at the Crossing");
      // The same word the board used while the battle was being fought, so
      // that a reader recognises this sentence as being about that one. What
      // this screen adds is the part only the campaign knows.
      expect(text).toContain("Outrider Bevan died, and will not come back.");
      expect(text).toContain("Vanguard Rilla earned 60.");
      expect(text).toContain("Vanguard Rilla reached level 2");
      // The recruitment the author wrote onto this node, said the way the
      // level-up beside it is said and read off the same committed batch.
      expect(text).toContain("Torvald the Ferryman joined the company.");
      // The two owners a campaign keeps, shown apart. Nobody drank in this run
      // and the picket kept its own, being authored to leave a tonic three
      // times in five with the draw off this battle's seeded drop stream not
      // coming up, so the stores hold exactly the one the fallen rider was still
      // carrying, which `record_permanent_death` returned. The survivors hold
      // their own, which is what they will take onto the next board. The line a
      // pick-up would print is asserted absent, because a screen claiming
      // something fell is as wrong as one hiding that it did.
      expect(text).not.toContain("Picked up");
      expect(text).toContain("In the stores: 1 × Field Tonic.");
      expect(text).toContain("Vanguard Rilla: 1 × Field Tonic");
      expect(text).toContain("Torvald the Ferryman: 1 × Field Tonic");
      expect(text).toContain("Next: The Watch on the Road.");

      // And then the aftermath acts. Continue opens the company rather than
      // walking straight onto the next board, because what the battle left is
      // a thing the player can do something with.
      button(host, "Continue").click();
      await settle();
      const managing = host.querySelector(".play-manage")!;
      const arranging = managing.textContent!.replace(/\s+/g, " ");
      expect(host.querySelector("#play-headline")!.textContent).toBe(
        "Before The Watch on the Road"
      );
      expect(arranging).toContain("1 × Field Tonic");

      // The tonic the crossing left the company goes into a rider's hand. It
      // commits as it is pressed, there being no Apply, so the screen redrawn
      // afterwards is the campaign rather than an intention.
      host
        .querySelectorAll<HTMLButtonElement>(".play-manage-give")[0]!
        .click();
      await nextTick();
      const armed = host.querySelector(".play-manage")!.textContent!.replace(
        /\s+/g,
        " "
      );
      expect(armed).toContain("Vanguard Rilla Carrying: 2 × Field Tonic");
      expect(armed).toContain("1 × Field Tonic");

      // And the ferryman is asked to stay behind, which is the third of the
      // three verbs: the player choosing who takes the field.
      const benches = [...host.querySelectorAll<HTMLButtonElement>(
        ".play-manage-bench"
      )];
      benches[benches.length - 1]!.click();
      await nextTick();
      expect(host.querySelector(".play-manage")!.textContent).toContain(
        "staying behind"
      );

      // The next board comes through the roster: the map lists four, and the
      // campaign fields two: the survivor and the picket. One is off it
      // because the crossing buried them and one because the player said so,
      // and the surface reports both the same way.
      button(host, "To the Stage").click();
      await settle();
      expect(host.querySelectorAll("g.unit")).toHaveLength(2);
      const missing = host.querySelector(".play-excluded")!.textContent!;
      expect(missing).toContain("Outrider Bevan");
      expect(missing).toContain("Torvald the Ferryman");
      app.unmount();
    });

    it("counts who is going against what the board allows, and refuses the rest", async () => {
      // Two of the company, and a way through that lets one of them out. The
      // fixture is a campaign of its own rather than the demo's road, because
      // the demo is the conformance reference and authors no cap.
      const narrow = JSON.parse(JSON.stringify(project)) as SourceProject;
      const campaign = narrow.campaigns![0]!;
      campaign.roster = [
        { id: "blue_one", name: "Blue One", unitTypeId: "blue" },
        { id: "blue_two", name: "Blue Two", unitTypeId: "blue" }
      ];
      const fight = campaign.flow!.nodes.find((node) => node.id === "fight")!;
      fight.deployment = { id: "narrow_way", capacity: 1 };
      fight.placements = [
        {
          id: "blue_one",
          memberId: "blue_one",
          unitTypeId: "blue",
          side: "first",
          x: 0,
          y: 1
        },
        {
          id: "blue_two",
          memberId: "blue_two",
          unitTypeId: "blue",
          side: "first",
          x: 0,
          y: 0
        },
        { id: "red_one", unitTypeId: "red", side: "second", x: 2, y: 1 }
      ];
      const { app, host } = mount(true, narrow);
      await settle();

      // Both numbers are the engine's, and the line is said plainly: a cap is
      // a maximum and a company under it is a perfectly good party.
      const managing = host.querySelector(".play-manage")!;
      expect(managing.textContent!.replace(/\s+/g, " "))
        .toContain("Going: 2 of 1.");

      // Benching one answers the cap. The engine benched nobody to make the
      // party fit; the player did.
      const benches = [...host.querySelectorAll<HTMLButtonElement>(
        ".play-manage-bench"
      )];
      benches[benches.length - 1]!.click();
      await nextTick();
      expect(host.querySelector(".play-manage")!.textContent!.replace(/\s+/g, " "))
        .toContain("Going: 1 of 1.");

      // And bringing them along again is refused before anything commits, in
      // the roster's own word for it, through the same line every other
      // refusal on this screen is shown through.
      host.querySelector<HTMLButtonElement>(".play-manage-field")!.click();
      await nextTick();
      expect(host.querySelector(".play-manage-refusal")!.textContent)
        .toContain("over_deployment_capacity");
      expect(host.querySelector(".play-manage")!.textContent!.replace(/\s+/g, " "))
        .toContain("Going: 1 of 1.");
      app.unmount();
    });
  });

  describe("ending the whole side's turn", () => {
    /**
     * A board where the side holds every character's turn at once.
     *
     * `sideBlocks` is what makes "end the side's turn" mean more than one
     * command: under it a whole company is commandable at once, and a player
     * who wants the enemy to move has to visit every one of them. Two blue
     * characters, so the drain has something to prove, and a red one that
     * closes on them, so that "the enemy moved next" is a thing the board
     * shows rather than a thing the test has to take on trust.
     */
    function twoOfOurs(): SourceProject {
      const source = structuredClone(project);
      source.defaultTurnOrder = "sideBlocks";
      const campaign = source.campaigns![0]!;
      campaign.roster = [
        { id: "blue_one", name: "Blue One", unitTypeId: "blue" },
        { id: "blue_two", name: "Blue Two", unitTypeId: "blue" }
      ];
      const fight = campaign.flow!.nodes[0]!;
      fight.placements = [
        {
          id: "blue_one",
          memberId: "blue_one",
          unitTypeId: "blue",
          side: "first",
          x: 0,
          y: 0
        },
        {
          id: "blue_two",
          memberId: "blue_two",
          unitTypeId: "blue",
          side: "first",
          x: 0,
          y: 2
        },
        {
          id: "red_one",
          unitTypeId: "red",
          side: "second",
          x: 3,
          y: 1,
          behavior: "pursue"
        }
      ];
      return source;
    }

    /** Where the one red character is standing. */
    function redAt(host: HTMLElement): string {
      const found = [...host.querySelectorAll('[role="gridcell"]')].find(
        (candidate) =>
          candidate.getAttribute("aria-label")?.includes("Red Guard")
      );
      return found?.getAttribute("aria-label")?.slice(0, 14) ?? "gone";
    }

    it("finishes one character without passing the turn", async () => {
      const { app, host } = await mountOnTheBoard(true, twoOfOurs());
      const before = redAt(host);

      // The per-character order, on a board where the side holds every
      // character's turn: finishing one leaves the other still ours, so the
      // enemy does not move. This is one half of ending a turn, and it is why
      // the other half, the test below, has to exist.
      cell(host, 0, 0).click();
      await nextTick();
      button(host, "Done with them").click();
      await settle();

      expect(redAt(host)).toBe(before);
      expect(host.textContent).toContain("Your turn");
      app.unmount();
    });

    it("spends every activation the side has not spent", async () => {
      const { app, host } = await mountOnTheBoard(true, twoOfOurs());
      const before = redAt(host);

      // The board's own order, in the board's own bar, offered with nobody
      // selected, because ending the side's turn is not a thing that needs a
      // character picked first.
      const ending = host.querySelector<HTMLButtonElement>(".play-end-turn")!;
      expect(ending.textContent).toContain("End our whole turn");
      ending.click();
      await settle();

      // Both of ours were finished by the one press. Red could not have moved
      // while either of them still held an activation, the test above being
      // that very control, so red having closed is the side's turn having ended.
      expect(redAt(host)).not.toBe(before);
      expect(host.textContent).toContain("Your turn");
      app.unmount();
    });

    it("is not offered before there is a turn to end", async () => {
      // The company screen stands before the board, and there is no side
      // holding activations there. A board-wide order offered off a board
      // would be an order the engine refuses on every press.
      const { app, host } = mount(true, twoOfOurs());
      await nextTick();
      expect(host.querySelector(".play-end-turn")).toBeNull();
      await takeTheBoard(host);
      expect(host.querySelector(".play-end-turn")).not.toBeNull();
      app.unmount();
    });
  });
});

describe("PlayMode keeping a campaign across a page", () => {
  /**
   * Mount with a store of our own, so a test can say what the browser is
   * holding. The store the component reaches for by default is IndexedDB's;
   * everything asserted below is about what the surface does with what it
   * finds there, which is the same either way.
   */
  function mountWith(kept: CampaignSlotStore, source = project) {
    const host = document.createElement("div");
    document.body.append(host);
    const app = createApp(PlayMode, {
      project: source,
      ready: true,
      activationDelayMs: 0,
      keptCampaigns: kept,
      onExit: () => {}
    });
    app.mount(host);
    return { app, host };
  }

  /** Plays the fixture battle to a winner and commits it. */
  async function winAndCommit(host: HTMLElement) {
    await takeTheBoard(host);
    cell(host, 0, 1).click();
    await nextTick();
    cell(host, 1, 1).click();
    await settle();
    for (let round = 0; round < 2; round += 1) {
      cell(host, 1, 1).click();
      await nextTick();
      cell(host, 2, 1).click();
      await settle();
    }
    expect(host.textContent).toContain("Your side wins!");
    button(host, "Continue").click();
    await settle();
  }

  it("offers nothing to pick up when the browser holds nothing", async () => {
    const { app, host } = mountWith(new MemoryCampaignSlotStore());
    await settle();
    // A button that would found a campaign while claiming to resume one is
    // worse than no button, so an author who has never played this game is not
    // shown one.
    expect(host.querySelector(".play-resume")).toBeNull();
    expect(host.querySelector(".play-kept")).toBeNull();
    app.unmount();
  });

  it("offers the campaign back once one is kept, and says what replaces it", async () => {
    const store = new MemoryCampaignSlotStore();
    const { app, host } = mountWith(store);
    await settle();
    await winAndCommit(host);

    // The battle committed, so the engine wrote its slot and the surface
    // carried those bytes to the browser. Now there is something to offer.
    expect(await store.read(keptCampaignSlot(project, "demo"))).toBeDefined();
    expect(host.querySelector(".play-resume")).not.toBeNull();
    // And the consequence of founding anew is stated on the press that does
    // it, because there is one kept campaign per game and campaign.
    expect(host.querySelector(".play-restart")!.getAttribute("title"))
      .toContain("Replaces the campaign this browser is keeping");
    app.unmount();
  });

  it("forgets the kept campaign when the author starts over", async () => {
    const store = new MemoryCampaignSlotStore();
    const { app, host } = mountWith(store);
    await settle();
    await winAndCommit(host);

    button(host, "Start over").click();
    await settle();
    expect(await store.read(keptCampaignSlot(project, "demo"))).toBeUndefined();
    expect(host.querySelector(".play-resume")).toBeNull();
    app.unmount();
  });

  it("reports a save it cannot read by name, and offers a way forward", async () => {
    const store = new MemoryCampaignSlotStore();
    const slot = keptCampaignSlot(project, "demo");
    // Bytes that are not a save. The surface hands them over unread, since
    // deciding out here whether a save is readable is exactly what this design
    // refuses to do, so the refusal below is the save format's own.
    await store.write({
      slot,
      packageId: "0".repeat(32),
      campaignId: "demo",
      // A header's worth of bytes, so the refusal is about what they say
      // rather than about how few of them there are.
      bytes: new Uint8Array(64)
    });
    const { app, host } = mountWith(store);
    await settle();

    button(host, "Pick up where I left off").click();
    await settle();
    expect(host.querySelector(".play-refused")!.textContent).toContain(
      "invalid_magic"
    );

    // Start fresh replaces what would not load, and the complaint goes with it.
    button(host, "Start fresh").click();
    await settle();
    expect(host.querySelector(".play-refused")).toBeNull();
    expect(await store.read(slot)).toBeUndefined();
    app.unmount();
  });
});
