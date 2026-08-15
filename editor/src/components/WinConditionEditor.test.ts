// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import type { SourceObjective } from "../generated/source-v1";
import WinConditionEditor from "./WinConditionEditor.vue";

afterEach(() => document.body.replaceChildren());

const objectives: SourceObjective[] = [
  { id: "clear_them_out", name: "Clear them out" },
  {
    id: "fell_the_warden",
    name: "Fell the Warden",
    kind: "defeatTarget",
    side: "first",
    targetPlacementId: "kesh"
  }
];

interface Placed {
  readonly id: string;
  readonly memberId?: string;
  readonly side: "first" | "second";
  readonly unitTypeId: string;
}

const placements: Placed[] = [
  { id: "mirea", side: "first", unitTypeId: "commander" },
  { id: "kesh", side: "second", unitTypeId: "commander" }
];

function mount(
  selected: string[] = [],
  options: {
    objectives?: SourceObjective[];
    placements?: typeof placements;
  } = {}
) {
  const host = document.createElement("div");
  document.body.append(host);
  const updates: SourceObjective[][] = [];
  const selections: string[][] = [];
  const app = createApp(WinConditionEditor, {
    objectives: options.objectives ?? objectives,
    selectedIds: selected,
    placements: options.placements ?? placements,
    onUpdateObjectives: (next: SourceObjective[]) => updates.push(next),
    onUpdateSelection: (next: string[]) => selections.push(next)
  });
  app.mount(host);
  return { app, host, updates, selections };
}

describe("WinConditionEditor", () => {
  it("states each condition in words rather than as fields", () => {
    const { app, host } = mount();
    expect(host.textContent).toContain(
      "Your side wins by defeating every opposing character."
    );
    expect(host.textContent).toContain("Your side wins by defeating kesh.");
    expect(host.textContent).toContain(
      "This Stage ends only when one side is wiped out."
    );
    app.unmount();
  });

  it("reports how many conditions the Stage actually uses", () => {
    const one = mount(["fell_the_warden"]);
    expect(one.host.textContent).toContain("1 condition decides this Stage");
    one.app.unmount();

    // Both halves of the count, because the verb agrees with it as well as the
    // noun and only the plural was ever read.
    const two = mount(["fell_the_warden", "clear_them_out"]);
    expect(two.host.textContent).toContain("2 conditions decide this Stage");
    two.app.unmount();
  });

  it("offers a target only for the targeted kinds", () => {
    const { app, host } = mount();
    // The first objective is defeat-everyone, so it has no target selector.
    expect(host.querySelector("#objective-clear_them_out-target")).toBeNull();
    expect(host.querySelector("#objective-fell_the_warden-target")).not.toBeNull();
    app.unmount();
  });

  it("names a target when switching to a targeted kind", async () => {
    const { app, host, updates } = mount();
    const kind = host.querySelector<HTMLSelectElement>(
      "#objective-clear_them_out-kind"
    )!;
    kind.value = "protectTarget";
    kind.dispatchEvent(new Event("change"));
    await nextTick();
    const changed = updates.at(-1)!.find((entry) => entry.id === "clear_them_out")!;
    expect(changed.kind).toBe("protectTarget");
    // Picking a targeted kind with no target chosen defaults to the first
    // placement rather than leaving a condition that can never be evaluated.
    expect(changed.targetPlacementId).toBe("mirea");
    app.unmount();
  });

  it("drops a stale target when switching back to defeat-everyone", async () => {
    const { app, host, updates } = mount();
    const kind = host.querySelector<HTMLSelectElement>(
      "#objective-fell_the_warden-kind"
    )!;
    kind.value = "defeatAllOpponents";
    kind.dispatchEvent(new Event("change"));
    await nextTick();
    const changed = updates.at(-1)!.find((entry) => entry.id === "fell_the_warden")!;
    expect(changed.kind).toBe("defeatAllOpponents");
    expect(changed.targetPlacementId).toBeUndefined();
    app.unmount();
  });

  it("names a target that has left the board rather than summarising a ghost",
    () => {
      // Placement identifiers are free text on the board next door, so
      // renaming or removing a character leaves this pointing at a name that
      // is gone. The content compiler refuses the encounter outright, and a
      // summary reading "wins by defeating kesh" over a Stage kesh has left
      // is the one sentence on this screen an author would trust.
      const { app, host } = mount(["fell_the_warden"], {
        placements: [
          { id: "mirea", side: "first" as const, unitTypeId: "commander" }
        ]
      });

      expect(host.textContent).toContain("Your side wins by defeating nobody on this board.");
      expect(host.textContent).toContain("does not stand on this board");
      const target = host.querySelector<HTMLSelectElement>(
        "#objective-fell_the_warden-target"
      )!;
      // The value stays selected rather than falling silently onto whoever is
      // first, and the option that carries it says what is wrong with it.
      expect(target.value).toBe("kesh");
      expect(target.querySelector("option")?.textContent)
        .toContain("nobody on this board");
      app.unmount();
    });

  it("names a target by the member standing there, as the compiler resolves it",
    () => {
      // A placement fielding a member of the company is resolved by the
      // member's identity, not the tile's: the character is who the objective
      // is about, and they are the same character on every board that places
      // them. A menu offering the tile’s identifier would author a Stage no
      // client can load.
      const { app, host } = mount(["fell_the_warden"], {
        objectives: [{
          id: "fell_the_warden",
          name: "Fell the Warden",
          kind: "defeatTarget",
          side: "second",
          targetPlacementId: "mirea_the_dawn"
        }],
        placements: [{
          id: "left_flank",
          memberId: "mirea_the_dawn",
          side: "first" as const,
          unitTypeId: "commander"
        }]
      });

      expect(host.textContent).not.toContain("does not stand on this board");
      const target = host.querySelector<HTMLSelectElement>(
        "#objective-fell_the_warden-target"
      )!;
      expect(target.value).toBe("mirea_the_dawn");
      expect(target.textContent).toContain("standing as left_flank");
      app.unmount();
    });

  it("toggles which conditions the Stage uses", async () => {
    const { app, host, selections } = mount();
    const check = host.querySelector<HTMLInputElement>(
      ".condition-toggle input"
    )!;
    check.checked = true;
    check.dispatchEvent(new Event("change"));
    await nextTick();
    expect(selections.at(-1)).toEqual(["clear_them_out"]);
    app.unmount();
  });
});
