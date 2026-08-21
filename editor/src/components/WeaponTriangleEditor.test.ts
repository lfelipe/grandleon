// SPDX-License-Identifier: MIT
import { createApp, h, nextTick, reactive } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { SourceWeaponType } from "../generated/source-v1";
import WeaponTriangleEditor from "./WeaponTriangleEditor.vue";

afterEach(() => document.body.replaceChildren());

const three: SourceWeaponType[] = [
  { id: "blade", name: "Blade" },
  { id: "haft", name: "Haft" },
  { id: "point", name: "Point" }
];

function mount(
  weaponTypes: readonly SourceWeaponType[] = three,
  advantage?: { damage: number; accuracy: number }
) {
  const host = document.createElement("div");
  document.body.append(host);
  const onUpdateTypes = vi.fn();
  const onUpdateAdvantage = vi.fn();
  const app = createApp(WeaponTriangleEditor, {
    weaponTypes,
    advantage,
    onUpdateTypes,
    onUpdateAdvantage
  });
  app.mount(host);
  return { app, host, onUpdateTypes, onUpdateAdvantage };
}

/** The same panel under a parent that applies what it emits, as the page does. */
function mountLive(
  weaponTypes: SourceWeaponType[] = structuredClone(three),
  advantage?: { damage: number; accuracy: number }
) {
  const host = document.createElement("div");
  document.body.append(host);
  const state = reactive<{
    types: SourceWeaponType[];
    advantage: { damage: number; accuracy: number } | undefined;
  }>({ types: weaponTypes, advantage });
  const app = createApp({
    setup() {
      return () => h(WeaponTriangleEditor, {
        weaponTypes: state.types,
        advantage: state.advantage,
        onUpdateTypes: (next: SourceWeaponType[]) => {
          state.types = next;
        },
        onUpdateAdvantage: (
          next: { damage: number; accuracy: number } | undefined
        ) => {
          state.advantage = next;
        }
      });
    }
  });
  app.mount(host);
  return { app, host, state };
}

function box(host: HTMLElement, one: string, other: string) {
  return host.querySelector<HTMLInputElement>(`#beats-${one}-${other}`);
}

function button(host: HTMLElement, name: string) {
  return [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.includes(name)
  )!;
}

describe("WeaponTriangleEditor", () => {
  it("draws every kind against every kind, and never against itself", () => {
    const { app, host } = mount();
    expect(box(host, "blade", "haft")).not.toBeNull();
    expect(box(host, "haft", "blade")).not.toBeNull();
    // A kind cannot beat itself: the same edge would be read once in each
    // direction and price every mirror match twice.
    expect(box(host, "blade", "blade")).toBeNull();
    app.unmount();
  });

  it("builds the classic ring in one press, worth something from the off", () => {
    const { app, host, onUpdateTypes, onUpdateAdvantage } = mount();
    button(host, "Make a ring").click();
    expect(onUpdateTypes.mock.calls[0]![0]).toEqual([
      { id: "blade", name: "Blade", strongAgainst: ["haft"] },
      { id: "haft", name: "Haft", strongAgainst: ["point"] },
      { id: "point", name: "Point", strongAgainst: ["blade"] }
    ]);
    // A ring with nothing behind it is a rule that never fires, so the press
    // that draws one also says what it is worth.
    expect(onUpdateAdvantage).toHaveBeenCalledWith({ damage: 1, accuracy: 15 });
    app.unmount();
  });

  it("leaves an advantage the author already stated alone", () => {
    const { app, host, onUpdateAdvantage } = mount(three, {
      damage: 4,
      accuracy: 20
    });
    button(host, "Make a ring").click();
    expect(onUpdateAdvantage).not.toHaveBeenCalled();
    app.unmount();
  });

  it("clears the other direction when an edge is ticked", async () => {
    const { app, host, state } = mountLive([
      { id: "blade", name: "Blade" },
      { id: "haft", name: "Haft", strongAgainst: ["blade"] }
    ]);
    const blade = box(host, "blade", "haft")!;
    blade.checked = true;
    blade.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    // Both directions at once is the one shape the engine cannot read: it
    // answers at most one of the two lookups, so a pair ticked both ways would
    // silently be read one way.
    expect(state.types).toEqual([
      { id: "blade", name: "Blade", strongAgainst: ["haft"] },
      { id: "haft", name: "Haft" }
    ]);
    app.unmount();
  });

  it("says the rule back as sentences", async () => {
    const { app, host } = mountLive(structuredClone(three));
    button(host, "Make a ring").click();
    await nextTick();
    expect(host.textContent).toContain("Blade beats Haft");
    expect(host.textContent).toContain("Haft beats Point");
    expect(host.textContent).toContain("Point beats Blade");
    app.unmount();
  });

  it("names the half-written rule, in both directions", () => {
    // Ticks with nothing at stake.
    const { app, host } = mount([
      { id: "blade", name: "Blade", strongAgainst: ["haft"] },
      { id: "haft", name: "Haft" }
    ]);
    expect(host.textContent).toContain("Nothing is at stake");
    app.unmount();

    // And a number with nothing to price.
    const bare = mount(three, { damage: 2, accuracy: 10 });
    expect(bare.host.textContent).toContain("Nothing is decided");
    bare.app.unmount();
  });

  it("asks for a second kind before it draws anything", () => {
    const { app, host } = mount([{ id: "blade", name: "Blade" }]);
    expect(host.textContent).toContain("at least two kinds");
    expect(box(host, "blade", "blade")).toBeNull();
    app.unmount();
  });

  it("clears every tick without touching what one is worth", async () => {
    const { app, host, state } = mountLive(
      [
        { id: "blade", name: "Blade", strongAgainst: ["haft"] },
        { id: "haft", name: "Haft" }
      ],
      { damage: 1, accuracy: 15 }
    );
    button(host, "Clear every tick").click();
    await nextTick();
    expect(state.types).toEqual([
      { id: "blade", name: "Blade" },
      { id: "haft", name: "Haft" }
    ]);
    expect(state.advantage).toEqual({ damage: 1, accuracy: 15 });
    app.unmount();
  });
});
