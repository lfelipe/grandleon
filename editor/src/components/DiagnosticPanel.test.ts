// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import DiagnosticPanel, { type PresentedDiagnostic } from "./DiagnosticPanel.vue";
import type { TargetNote } from "../domain/target-budget";

afterEach(() => document.body.replaceChildren());

function mount(
  diagnostics: readonly PresentedDiagnostic[],
  onNavigate = vi.fn(),
  targetNotes: readonly TargetNote[] = []
) {
  const host = document.createElement("div");
  document.body.append(host);
  const app = createApp(DiagnosticPanel, { diagnostics, onNavigate, targetNotes });
  app.mount(host);
  return { app, host, onNavigate };
}

const overrunNote: TargetNote = {
  targetId: "nintendo64",
  code: "TARGET_PALETTES_EXCEEDED",
  message: "On a Nintendo 64 this game needs 17 palettes of 16 colours at once."
};

describe("DiagnosticPanel", () => {
  it("announces a clean project", () => {
    const { app, host } = mount([]);
    expect(host.querySelector('[role="status"]')?.textContent).toContain(
      "No problems found"
    );
    expect(host.querySelector("ol")).toBeNull();
    app.unmount();
  });

  it("summarizes diagnostics and provides keyboard-native navigation", async () => {
    const diagnostic: PresentedDiagnostic = {
      severity: "error",
      code: "SOURCE_REF_MISSING",
      sourcePath: "project.json",
      instancePath: "/unitTypes/0/classId",
      message: "missing class reference 'guardian'"
    };
    const { app, host, onNavigate } = mount([diagnostic]);

    expect(host.querySelector('[role="status"]')?.textContent).toContain(
      "1 problem found"
    );
    expect(host.querySelector("code")?.textContent).toBe("SOURCE_REF_MISSING");
    const button = host.querySelector("button")!;
    expect(button.textContent).toContain("project.json/unitTypes/0/classId");
    button.click();
    await nextTick();
    expect(onNavigate).toHaveBeenCalledWith(diagnostic);
    app.unmount();
  });

  it("keeps console notes out of the problem count", () => {
    // A game that overruns a console is not a broken game. The panel still
    // says the project is clean, and says separately what an old machine
    // would make of it.
    const { app, host } = mount([], vi.fn(), [overrunNote]);
    expect(host.querySelector('[role="status"]')?.textContent).toContain(
      "No problems found"
    );
    expect(host.querySelector("ol")).toBeNull();
    const notes = host.querySelector(".target-notes")!;
    expect(notes.querySelector("h3")?.textContent).toBe("On an old console");
    expect(notes.textContent).toContain(
      "On a Nintendo 64 this game needs 17"
    );
    expect(notes.textContent).toContain(
      "Nothing here stops the game playing in this editor"
    );
    // Nothing to navigate to and nothing to fix: notes are not diagnostics.
    expect(notes.querySelector("button")).toBeNull();
    app.unmount();
  });

  it("shows no console section for a game every console can hold", () => {
    const { app, host } = mount([], vi.fn(), []);
    expect(host.querySelector(".target-notes")).toBeNull();
    app.unmount();
  });
});
