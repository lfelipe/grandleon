// SPDX-License-Identifier: MIT
import { createApp, h, nextTick, reactive } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import {
  BACKDROP_IDS,
  type SpeakerPortraitProject
} from "../domain/board-art";
import type { SourceDialogue } from "../generated/source-v1";
import CutsceneEditor from "./CutsceneEditor.vue";

afterEach(() => document.body.replaceChildren());

interface Harness {
  app: ReturnType<typeof createApp>;
  host: HTMLElement;
  state: { dialogueIds: string[]; dialogues: SourceDialogue[] };
}

/**
 * Mounts the editor under a live parent that applies its emits back into
 * props, the way ContentWorkspace and CampaignFlowEditor do, so multi-step
 * interactions see their own earlier changes.
 */
function mount(
  dialogueIds: string[],
  dialogues: SourceDialogue[],
  project?: SpeakerPortraitProject
): Harness {
  const host = document.createElement("div");
  document.body.append(host);
  const state = reactive({ dialogueIds, dialogues });
  const app = createApp({
    setup() {
      return () => h(CutsceneEditor, {
        dialogueIds: state.dialogueIds,
        dialogues: state.dialogues,
        ...(project ? { project } : {}),
        onUpdateIds: (ids: string[]) => {
          state.dialogueIds = ids;
        },
        onUpdateDialogues: (next: SourceDialogue[]) => {
          state.dialogues = next;
        }
      });
    }
  });
  app.mount(host);
  return { app, host, state };
}

function buttons(host: HTMLElement, text: string): HTMLButtonElement[] {
  return [...host.querySelectorAll("button")].filter(
    (candidate) => candidate.textContent?.trim() === text
  );
}

function button(host: HTMLElement, text: string, index = 0): HTMLButtonElement {
  const found = buttons(host, text)[index];
  if (!found) throw new Error(`button '${text}' [${index}] not found`);
  return found;
}

const twoScenes: SourceDialogue[] = [
  {
    id: "arrival",
    name: "The Arrival",
    lines: [
      { speaker: "Mirea", text: "We made it." },
      { speaker: "Halden", text: "Barely." }
    ]
  },
  {
    id: "warning",
    name: "The Warning",
    lines: [{ speaker: "Scout", text: "Riders on the ridge!" }]
  }
];

describe("CutsceneEditor", () => {
  it("lists the sequence in order with names and line counts", () => {
    const { app, host } = mount(["arrival", "warning"], twoScenes);
    const items = [...host.querySelectorAll(".cutscene-list li")];
    expect(items[0]?.textContent).toContain("The Arrival");
    expect(items[0]?.textContent).toContain("2 lines");
    expect(items[1]?.textContent).toContain("The Warning");
    expect(items[1]?.textContent).toContain("1 line");
    app.unmount();
  });

  it("reorders scenes with the up and down buttons", async () => {
    const { app, host, state } = mount(["arrival", "warning"], twoScenes);
    button(host, "Move up", 1).click();
    await nextTick();
    expect(state.dialogueIds).toEqual(["warning", "arrival"]);
    button(host, "Move down", 0).click();
    await nextTick();
    expect(state.dialogueIds).toEqual(["arrival", "warning"]);
    // The ends of the list cannot move past themselves.
    expect(button(host, "Move up", 0).disabled).toBe(true);
    expect(button(host, "Move down", 1).disabled).toBe(true);
    app.unmount();
  });

  it("adds an existing scene and removes an entry", async () => {
    const { app, host, state } = mount(["arrival"], twoScenes);
    const select = host.querySelector<HTMLSelectElement>("#cutscene-add-existing")!;
    select.value = "warning";
    select.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Add scene").click();
    await nextTick();
    expect(state.dialogueIds).toEqual(["arrival", "warning"]);

    button(host, "Remove", 0).click();
    await nextTick();
    expect(state.dialogueIds).toEqual(["warning"]);
    app.unmount();
  });

  it("never offers a scene this node already plays", async () => {
    // `dialogueIds` is an ordered play sequence, so naming a scene twice plays
    // it twice, and the lines editor finds a scene by identifier, so the two
    // fieldsets drawn for it are two views of one record that cannot be edited
    // apart. Nothing an author meant by "add this scene" was either of those,
    // so the menu does not offer what is already here.
    const { app, host, state } = mount(["arrival"], twoScenes);
    const select = host.querySelector<HTMLSelectElement>("#cutscene-add-existing")!;
    expect([...select.options].map((option) => option.value))
      .toEqual(["", "warning"]);

    select.value = "warning";
    select.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    button(host, "Add scene").click();
    await nextTick();
    expect(state.dialogueIds).toEqual(["arrival", "warning"]);

    // Nothing left to add, so the control says so instead of standing ready to
    // duplicate whatever was last chosen.
    expect([...select.options].map((option) => option.value)).toEqual([""]);
    expect(button(host, "Add scene").disabled).toBe(true);
    expect(host.textContent).toContain("already played here");
    app.unmount();
  });

  it("creates a new scene inline and opens its lines for editing", async () => {
    const { app, host, state } = mount(["arrival"], twoScenes);
    const name = host.querySelector<HTMLInputElement>("#cutscene-new-name")!;
    name.value = "The Gates Open";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    button(host, "Write a new scene").click();
    await nextTick();

    expect(state.dialogues.at(-1)).toEqual({
      id: "the_gates_open",
      name: "The Gates Open"
    });
    expect(state.dialogueIds).toEqual(["arrival", "the_gates_open"]);
    // The fresh scene opens ready to write into.
    expect(host.textContent).toContain("Nobody speaks yet");

    button(host, "Add a line").click();
    await nextTick();
    expect(state.dialogues.at(-1)?.lines).toEqual([
      { speaker: "Narrator", text: "…" }
    ]);
    app.unmount();
  });

  it("edits, reorders, and removes a scene's lines as a list", async () => {
    const { app, host, state } = mount(["arrival"], twoScenes);
    button(host, "Edit lines").click();
    await nextTick();

    const text = host.querySelector<HTMLTextAreaElement>(
      "#cutscene-0-line-0-text"
    )!;
    text.value = "We made it. All of us.";
    text.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    expect(state.dialogues[0]?.lines?.[0]).toEqual({
      speaker: "Mirea",
      text: "We made it. All of us."
    });

    button(host, "Move line down", 0).click();
    await nextTick();
    expect(state.dialogues[0]?.lines?.map((line) => line.speaker))
      .toEqual(["Halden", "Mirea"]);

    button(host, "Remove line", 0).click();
    await nextTick();
    button(host, "Remove line", 0).click();
    await nextTick();
    // A scene whose lines all went away drops the optional field entirely.
    expect(state.dialogues[0]).toEqual({ id: "arrival", name: "The Arrival" });
    app.unmount();
  });

  it("names a sequence entry whose scene no longer exists", () => {
    const { app, host } = mount(["vanished"], twoScenes);
    expect(host.querySelector('[role="alert"]')?.textContent)
      .toContain("No scene named 'vanished'");
    app.unmount();
  });

  it("previews every scene's lines in sequence, as the game presents them", async () => {
    const { app, host } = mount(["arrival", "warning"], twoScenes);
    button(host, "Preview").click();
    await nextTick();

    const stage = host.querySelector(".dialogue-preview-stage")!;
    expect(stage.textContent).toContain("Mirea");
    expect(stage.textContent).toContain("We made it.");
    expect(stage.textContent).toContain("Line 1 of 3");

    button(host, "Next").click();
    await nextTick();
    expect(stage.textContent).toContain("Halden");
    button(host, "Next").click();
    await nextTick();
    // The second scene follows the first without any seam.
    expect(stage.textContent).toContain("Scout");
    expect(stage.textContent).toContain("Riders on the ridge!");
    button(host, "Next").click();
    await nextTick();
    expect(stage.textContent).toContain("The scene ends.");

    button(host, "Close preview").click();
    await nextTick();
    expect(host.querySelector(".dialogue-preview-stage")).toBeNull();
    app.unmount();
  });

  // What a scene is drawn against. Every assertion below is about the record
  // rather than about the pixels: a backdrop is a choice, and the choice is
  // what an author's project has to end up holding.
  describe("the backdrop a scene is set against", () => {
    function backdropSelect(host: HTMLElement, index = 0): HTMLSelectElement {
      const found = host.querySelectorAll<HTMLSelectElement>(
        "select[id^='cutscene-backdrop-']"
      )[index];
      if (!found) throw new Error(`no backdrop select at ${index}`);
      return found;
    }

    it("labels one control per scene and offers the library's menu", () => {
      const { app, host } = mount(["arrival", "warning"], twoScenes);
      const select = backdropSelect(host);
      const label = host.querySelector<HTMLLabelElement>(
        `label[for='${select.id}']`
      );
      expect(label?.textContent?.trim()).toBe("Set against");
      // Every backdrop the library holds, and the empty choice above them.
      expect([...select.options].map((option) => option.value)).toEqual([
        "", ...BACKDROP_IDS
      ]);
      expect(select.value).toBe("");
      expect(host.querySelectorAll("select[id^='cutscene-backdrop-']").length)
        .toBe(2);
      app.unmount();
    });

    it("stores the chosen backdrop on the scene it belongs to", async () => {
      const { app, host, state } = mount(["arrival", "warning"], twoScenes);
      const select = backdropSelect(host);
      select.value = "throne_hall";
      select.dispatchEvent(new Event("change"));
      await nextTick();
      expect(state.dialogues[0]?.backgroundId).toBe("throne_hall");
      // The scene beside it is untouched: the choice is per scene.
      expect(state.dialogues[1]).not.toHaveProperty("backgroundId");
      app.unmount();
    });

    it("deletes the field when the choice is cleared", async () => {
      const dressed: SourceDialogue[] = [
        { ...twoScenes[0]!, backgroundId: "crypt" },
        twoScenes[1]!
      ];
      const { app, host, state } = mount(["arrival"], dressed);
      expect(backdropSelect(host).value).toBe("crypt");
      const select = backdropSelect(host);
      select.value = "";
      select.dispatchEvent(new Event("change"));
      await nextTick();
      // Absent, not empty: a scene set against nothing must read exactly like
      // one written before backdrops existed.
      expect(state.dialogues[0]).not.toHaveProperty("backgroundId");
      app.unmount();
    });

    it("keeps a backdrop the library no longer offers, and explains it", () => {
      const stale = [
        { ...twoScenes[0]!, backgroundId: "observatory" }
      ] as unknown as SourceDialogue[];
      const { app, host } = mount(["arrival"], stale);
      const select = backdropSelect(host);
      expect(select.value).toBe("observatory");
      expect(host.textContent).toContain(
        "The art library does not offer 'observatory'"
      );
      app.unmount();
    });

    it("changes the preview's backdrop where the scene changes", async () => {
      const dressed: SourceDialogue[] = [
        { ...twoScenes[0]!, backgroundId: "throne_hall" },
        { ...twoScenes[1]!, backgroundId: "open_sea" }
      ];
      const { app, host } = mount(["arrival", "warning"], dressed);
      button(host, "Preview").click();
      await nextTick();
      const stage = host.querySelector(".dialogue-preview-stage")!;
      expect(stage.textContent).toContain("Throne hall");
      button(host, "Next").click();
      await nextTick();
      button(host, "Next").click();
      await nextTick();
      // The third line is the second scene's, and the backdrop moves with it.
      expect(stage.textContent).toContain("Riders on the ridge!");
      expect(stage.textContent).toContain("Open sea");
      app.unmount();
    });
  });
});

describe("CutsceneEditor portraits", () => {
  it("draws the face the scene casts, as the Content page beside it does",
    async () => {
      // A preview handed neither the cast nor the project returns early and
      // draws nobody, while the same scenes on the Content page draw every
      // portrait, leaving an author judging their cutscene with no faces and
      // no way to know whether that is the scene or the surface.
      const { app, host } = mount(
        ["gates"],
        [{
          id: "gates",
          name: "The gates open",
          cast: [{ speaker: "Mirea", unitTypeId: "dawn_commander" }],
          lines: [{ speaker: "Mirea", text: "Hold the line." }]
        }],
        {
          characterStyleId: "medieval",
          unitTypes: [{ id: "dawn_commander", classId: "knight" }],
          factions: []
        }
      );

      buttons(host, "Preview")[0]!.click();
      await nextTick();
      const portrait = host.querySelector<HTMLImageElement>(
        ".dialogue-preview-portrait"
      );
      expect(portrait).not.toBeNull();
      expect(portrait!.getAttribute("alt")).toContain("Mirea");
      app.unmount();
    });

  it("changes the cast where the runtime changes it, scene by scene",
    async () => {
      // A cutscene is several scenes and each casts its own speakers, exactly
      // as each names its own backdrop. One cast held over the whole run would
      // answer the wrong scene's question.
      const { app, host } = mount(
        ["first", "second"],
        [
          {
            id: "first",
            name: "First",
            cast: [{ speaker: "Voice", unitTypeId: "dawn_commander" }],
            lines: [{ speaker: "Voice", text: "One." }]
          },
          {
            id: "second",
            name: "Second",
            lines: [{ speaker: "Voice", text: "Two." }]
          }
        ],
        {
          characterStyleId: "medieval",
          unitTypes: [{ id: "dawn_commander", classId: "knight" }],
          factions: []
        }
      );

      buttons(host, "Preview")[0]!.click();
      await nextTick();
      expect(host.querySelector(".dialogue-preview-portrait")).not.toBeNull();
      buttons(host, "Next")[0]!.click();
      await nextTick();
      // The second scene casts nobody, so its line is drawn with no face
      // rather than with the first scene's.
      expect(host.querySelector(".dialogue-preview-portrait")).toBeNull();
      app.unmount();
    });
});
