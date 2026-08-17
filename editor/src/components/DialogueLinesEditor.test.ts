// SPDX-License-Identifier: MIT
// The lines of a scene, and the one join that decides whether a face is drawn.
//
// A cast entry reaches a line through its speaker string, matched exactly and
// case sensitively. That join is invisible when it fails: the scene still plays,
// the words are still the author's, and the portrait is quietly the fallback. So
// what is checked here is that the editor closes the gap rather than leaving an
// author to spell the same person twice and hope.
import { createApp, h, nextTick, reactive } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import DialogueLinesEditor, { type DialogueLine } from "./DialogueLinesEditor.vue";

afterEach(() => document.body.replaceChildren());

function mount(lines: DialogueLine[], castSpeakers?: string[]) {
  const host = document.createElement("div");
  document.body.append(host);
  const state = reactive({ lines });
  const app = createApp({
    setup() {
      // Spread rather than always passing the key: the project builds with
      // `exactOptionalPropertyTypes`, so an absent cast is a prop that is not
      // there rather than one holding undefined.
      return () => h(DialogueLinesEditor, {
        lines: state.lines,
        idPrefix: "scene",
        ...(castSpeakers === undefined ? {} : { castSpeakers }),
        onUpdate: (next: DialogueLine[]) => {
          state.lines = next;
        }
      });
    }
  });
  app.mount(host);
  return { app, host, state };
}

describe("the lines of a scene", () => {
  it("offers the cast this scene holds, so both places spell one person once",
    async () => {
      const { app, host } = mount(
        [{ speaker: "Mirea", text: "Hold the ford." }],
        ["Mirea", "Nemet"]
      );
      const list = host.querySelector("#scene-cast");
      expect(list).not.toBeNull();
      expect([...list!.querySelectorAll("option")].map((o) => o.getAttribute("value")))
        .toEqual(["Mirea", "Nemet"]);

      // The field is bound to that list, which is what makes the offer reach an
      // author rather than sit in the markup.
      const speaker = host.querySelector<HTMLInputElement>("#scene-line-0-speaker")!;
      expect(speaker.getAttribute("list")).toBe("scene-cast");
      app.unmount();
    });

  it("says when a line names somebody the scene has not cast", async () => {
    const { app, host } = mount(
      [{ speaker: "mirea", text: "Hold the ford." }],
      ["Mirea"]
    );
    // Case sensitively, because that is how the package reads it. A capital
    // letter is the whole of the difference between a face and no face.
    expect(host.textContent).toContain("Nobody is cast as mirea");
    app.unmount();
  });

  it("says nothing about a line nobody has named, or a scene casting nobody",
    async () => {
      // A line being written is not a mistake, and a warning that arrives on the
      // first keystroke is one an author learns to ignore.
      const empty = mount([{ speaker: "", text: "" }], ["Mirea"]);
      expect(empty.host.textContent).not.toContain("Nobody is cast");
      empty.app.unmount();

      // And a scene that casts nobody draws no faces by choice. Every line in it
      // would otherwise be flagged.
      const uncast = mount([{ speaker: "Mirea", text: "Hold." }]);
      expect(uncast.host.textContent).not.toContain("Nobody is cast");
      expect(uncast.host.querySelector("#scene-cast")).toBeNull();
      uncast.app.unmount();
    });

  it("stops saying it once the name matches the cast", async () => {
    const { app, host } = mount(
      [{ speaker: "mirea", text: "Hold the ford." }],
      ["Mirea"]
    );
    expect(host.textContent).toContain("Nobody is cast as mirea");
    const speaker = host.querySelector<HTMLInputElement>("#scene-line-0-speaker")!;
    speaker.value = "Mirea";
    speaker.dispatchEvent(new Event("input"));
    await nextTick();
    expect(host.textContent).not.toContain("Nobody is cast");
    app.unmount();
  });
});
