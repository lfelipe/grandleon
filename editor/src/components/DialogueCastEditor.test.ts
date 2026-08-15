// SPDX-License-Identifier: MIT
import { createApp, h, nextTick, reactive } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import DialogueCastEditor, {
  type DialogueCastEntry
} from "./DialogueCastEditor.vue";

afterEach(() => document.body.replaceChildren());

const unitTypes = [
  { id: "dawn_commander", name: "Captain Mirea" },
  { id: "dawn_healer", name: "Sister Nemet" }
];

interface Harness {
  app: ReturnType<typeof createApp>;
  host: HTMLElement;
  state: { cast: DialogueCastEntry[] };
}

/**
 * Mounts the editor under a live parent that applies its emits back into
 * props, the way ContentWorkspace does, so multi-step interactions see their
 * own earlier changes.
 */
function mount(
  cast: DialogueCastEntry[],
  speakers: string[],
  types = unitTypes
): Harness {
  const host = document.createElement("div");
  document.body.append(host);
  const state = reactive({ cast });
  const app = createApp({
    setup() {
      return () => h(DialogueCastEditor, {
        cast: state.cast,
        speakers,
        unitTypes: types,
        idPrefix: "scene",
        onUpdate: (next: DialogueCastEntry[]) => {
          state.cast = next;
        }
      });
    }
  });
  app.mount(host);
  return { app, host, state };
}

function select(host: HTMLElement, id: string): HTMLSelectElement {
  const element = host.querySelector<HTMLSelectElement>(`#${id}`);
  if (!element) throw new Error(`no control ${id}`);
  return element;
}

function button(host: HTMLElement, label: string): HTMLButtonElement {
  const found = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim() === label
  );
  if (!found) throw new Error(`no button "${label}"`);
  return found as HTMLButtonElement;
}

describe("DialogueCastEditor", () => {
  it("names a speaker as one of the project's characters", async () => {
    const { app, host, state } = mount([], ["Captain Mirea", "Runner"]);

    button(host, "Name a speaker").click();
    await nextTick();

    // The first uncast speaker and the first character, so a fresh entry is
    // valid rather than half-authored.
    expect(state.cast).toEqual([
      { speaker: "Captain Mirea", unitTypeId: "dawn_commander" }
    ]);

    const chooser = select(host, "scene-cast-0-unit-type");
    chooser.value = "dawn_healer";
    chooser.dispatchEvent(new Event("change"));
    await nextTick();
    expect(state.cast[0]!.unitTypeId).toBe("dawn_healer");

    app.unmount();
  });

  it("offers only speakers this scene has, and only once each", async () => {
    const { app, host } = mount(
      [{ speaker: "Captain Mirea", unitTypeId: "dawn_commander" }],
      ["Captain Mirea", "Runner"]
    );

    // The entry keeps its own speaker as a choice; the other entry's is gone,
    // because two entries naming one speaker is two answers to one question.
    const chooser = select(host, "scene-cast-0-speaker");
    expect([...chooser.options].map((option) => option.value)).toEqual([
      "Captain Mirea",
      "Runner"
    ]);

    button(host, "Name a speaker").click();
    await nextTick();
    const second = select(host, "scene-cast-1-speaker");
    expect([...second.options].map((option) => option.value)).toEqual([
      "Runner"
    ]);
    expect(button(host, "Name a speaker").disabled).toBe(true);

    app.unmount();
  });

  it("explains a speaker no line uses rather than dropping it", async () => {
    // What renaming a speaker leaves behind. Its only other symptom is the old
    // drawing, so the control has to say so.
    const { app, host } = mount(
      [{ speaker: "Captian Mirea", unitTypeId: "dawn_commander" }],
      ["Captain Mirea"]
    );

    const chooser = select(host, "scene-cast-0-speaker");
    const stale = [...chooser.options].find(
      (option) => option.value === "Captian Mirea"
    );
    expect(stale?.disabled).toBe(true);
    expect(stale?.textContent).toContain("speaks no line in this scene");

    app.unmount();
  });

  it("explains a character the project no longer holds", async () => {
    const { app, host } = mount(
      [{ speaker: "Captain Mirea", unitTypeId: "departed_hero" }],
      ["Captain Mirea"]
    );

    const chooser = select(host, "scene-cast-0-unit-type");
    const missing = [...chooser.options].find(
      (option) => option.value === "departed_hero"
    );
    expect(missing?.disabled).toBe(true);
    expect(missing?.textContent).toContain("not a character in this project");

    app.unmount();
  });

  it("says nothing can be named when the project has no characters", () => {
    const { app, host } = mount([], ["Captain Mirea"], []);

    expect(host.textContent).toContain("No characters yet");
    expect(button(host, "Name a speaker").disabled).toBe(true);

    app.unmount();
  });

  it("removes a speaker without disturbing the others", async () => {
    const { app, host, state } = mount(
      [
        { speaker: "Captain Mirea", unitTypeId: "dawn_commander" },
        { speaker: "Runner", unitTypeId: "dawn_healer" }
      ],
      ["Captain Mirea", "Runner"]
    );

    button(host, "Remove speaker 1").click();
    await nextTick();
    expect(state.cast).toEqual([
      { speaker: "Runner", unitTypeId: "dawn_healer" }
    ]);

    app.unmount();
  });
});
