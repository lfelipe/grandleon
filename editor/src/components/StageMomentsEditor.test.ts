// SPDX-License-Identifier: MIT
// What a Stage says while it is being fought, authored.
//
// The surface is three sentences and two menus, and what is checked here is
// that it authors the shape the compiler reads: an occasion, who it is about
// where the occasion is about somebody, and the scene to play. Nothing types an
// identifier, so nothing here does either.
import { createApp, h, nextTick, reactive } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import StageMomentsEditor from "./StageMomentsEditor.vue";
import type {
  EncounterMoment,
  EncounterPlacement,
  SourceDialogue
} from "../generated/source-v1";

afterEach(() => document.body.replaceChildren());

const placements: EncounterPlacement[] = [
  { id: "captain", unitTypeId: "dawn_commander", side: "first", x: 0, y: 0 },
  { id: "envoy", unitTypeId: "ashen_archer", side: "second", x: 1, y: 1 }
];

const dialogues: SourceDialogue[] = [
  { id: "the-bell", name: "As the bell rings" },
  { id: "heard-out", name: "Heard out" }
];

function mount(moments: EncounterMoment[] = [], scenes = dialogues) {
  const host = document.createElement("div");
  document.body.append(host);
  const state = reactive({ moments, scenes });
  const app = createApp({
    setup() {
      return () => h(StageMomentsEditor, {
        moments: state.moments,
        placements,
        dialogues: state.scenes,
        unitTypeName: (id: string) =>
          id === "dawn_commander" ? "Captain Mirea" : undefined,
        onUpdate: (next: EncounterMoment[]) => {
          state.moments = next;
        },
        onUpdateDialogues: (next: SourceDialogue[]) => {
          state.scenes = next;
        }
      });
    }
  });
  app.mount(host);
  return { app, host, state };
}

function press(host: HTMLElement, label: string) {
  const found = [...host.querySelectorAll("button")].find(
    (button) => button.textContent?.trim() === label
  );
  if (found === undefined) throw new Error(`no button reading "${label}"`);
  found.click();
}

describe("what a Stage says while it is being fought", () => {
  it("offers the three occasions a fight actually reports", () => {
    const { app, host } = mount();
    const offered = [...host.querySelectorAll("[data-occasion]")].map(
      (button) => button.getAttribute("data-occasion")
    );
    expect(offered).toEqual(["stageOpens", "characterTalked", "characterFalls"]);
    app.unmount();
  });

  it("authors a moment about the board with nobody attached to it", async () => {
    const { app, host, state } = mount();
    press(host, "Say something when the Stage opens");
    await nextTick();
    expect(state.moments).toHaveLength(1);
    expect(state.moments[0]!.when.kind).toBe("stageOpens");
    // Refused by the schema on this occasion, so it must not be authored.
    expect(state.moments[0]!.when.placementId).toBeUndefined();
    expect(state.moments[0]!.dialogueId).toBe("the-bell");
    app.unmount();
  });

  it("authors a moment about somebody, and says who in words", async () => {
    const { app, host, state } = mount();
    press(host, "Say something when somebody is talked to");
    await nextTick();
    expect(state.moments[0]!.when.kind).toBe("characterTalked");
    expect(state.moments[0]!.when.placementId).toBe("captain");

    // Named the way an author named them, never by a key.
    expect(host.textContent).toContain("Captain Mirea");
    expect(host.textContent).toContain("is talked to");
    app.unmount();
  });

  it("keeps talking and falling apart, because the engine does", async () => {
    const { app, host, state } = mount();
    press(host, "Say something when somebody falls");
    await nextTick();
    expect(state.moments[0]!.when.kind).toBe("characterFalls");
    // The moment's own sentence, not the page: the buttons that offer the other
    // occasions say their own words and always will.
    const said = host.querySelector(".moment-summary")!.textContent ?? "";
    expect(said).toContain("falls");
    expect(said).not.toContain("is talked to");
    app.unmount();
  });

  it("says when a moment is about nobody on this Stage", async () => {
    // The board refuses to open on one of these, so an author should meet it
    // here rather than as a Stage that will not start.
    const { app, host } = mount([{
      id: "ghost",
      when: { kind: "characterFalls", placementId: "somebody_else" },
      dialogueId: "the-bell"
    }]);
    expect(host.textContent).toContain("Nobody by that name stands on this Stage");
    app.unmount();
  });

  it("offers no occasion at all until the game has a scene to play", () => {
    const { app, host } = mount([], []);
    for (const button of host.querySelectorAll("[data-occasion]")) {
      expect((button as HTMLButtonElement).disabled).toBe(true);
    }
    expect(host.textContent).toContain("no scenes yet");
    app.unmount();
  });

  it("makes a scene from here and says that one instead", async () => {
    const { app, host, state } = mount();
    press(host, "Say something when the Stage opens");
    await nextTick();
    const name = host.querySelector<HTMLInputElement>("#moment-0-new-scene")!;
    name.value = "The bell answers";
    name.dispatchEvent(new Event("input"));
    await nextTick();
    press(host, "Make a scene and say that instead");
    await nextTick();
    expect(state.scenes.map((scene) => scene.id)).toContain("the_bell_answers");
    expect(state.moments[0]!.dialogueId).toBe("the_bell_answers");
    app.unmount();
  });

  it("takes one out and leaves the rest", async () => {
    const { app, host, state } = mount();
    press(host, "Say something when the Stage opens");
    await nextTick();
    press(host, "Say something when somebody falls");
    await nextTick();
    expect(state.moments).toHaveLength(2);
    press(host, "Take this out");
    await nextTick();
    expect(state.moments).toHaveLength(1);
    expect(state.moments[0]!.when.kind).toBe("characterFalls");
    app.unmount();
  });
});
