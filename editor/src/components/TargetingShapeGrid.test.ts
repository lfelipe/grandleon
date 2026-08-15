// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import TargetingShapeGrid from "./TargetingShapeGrid.vue";

afterEach(() => document.body.replaceChildren());

function mount(props: Record<string, unknown>) {
  const host = document.createElement("div");
  document.body.append(host);
  const onBand = vi.fn();
  const onArea = vi.fn();
  const app = createApp(TargetingShapeGrid, { ...props, onBand, onArea });
  app.mount(host);
  return { app, host, onBand, onArea };
}

const cell = (host: HTMLElement, dx: number, dy: number) =>
  host.querySelector<HTMLButtonElement>(`[data-cell="${dx}:${dy}"]`)!;

const stateOf = (host: HTMLElement, dx: number, dy: number) =>
  cell(host, dx, dy).dataset.state;

const press = (button: HTMLButtonElement) =>
  button.dispatchEvent(new MouseEvent("pointerdown", { bubbles: true, button: 0 }));

const key = (button: HTMLButtonElement, name: string) =>
  button.dispatchEvent(new KeyboardEvent("keydown", { key: name, bubbles: true }));

/** Turns on one of the grid's mode checkboxes by its visible label. */
function toggleMode(host: HTMLElement, label: string) {
  const control = [...host.querySelectorAll<HTMLLabelElement>(".targeting-modes label")]
    .find((candidate) => candidate.textContent?.includes(label))!
    .querySelector<HTMLInputElement>("input")!;
  control.checked = true;
  control.dispatchEvent(new Event("change", { bubbles: true }));
  return control;
}

describe("the reach band grid", () => {
  it("draws the origin, the band and the hole a minimum reach makes", async () => {
    const { app, host } = mount({
      kind: "band", minimumRange: 2, maximumRange: 3
    });
    await nextTick();
    expect(stateOf(host, 0, 0)).toBe("origin");
    // One tile away is inside the minimum reach: a bow that cannot hit an
    // adjacent enemy. It is drawn as its own state, not as "out of reach".
    expect(stateOf(host, 1, 0)).toBe("hole");
    expect(stateOf(host, 0, -1)).toBe("hole");
    expect(stateOf(host, 2, 0)).toBe("covered");
    expect(stateOf(host, 1, 1)).toBe("covered");
    expect(stateOf(host, 3, 0)).toBe("covered");
    expect(stateOf(host, 4, 0)).toBe("outside");
    app.unmount();
  });

  it("draws no hole when a band starts at one", async () => {
    const { app, host } = mount({
      kind: "band", minimumRange: 1, maximumRange: 2
    });
    await nextTick();
    expect(stateOf(host, 1, 0)).toBe("covered");
    expect(host.querySelectorAll('[data-state="hole"]')).toHaveLength(0);
    app.unmount();
  });

  it("writes both ends when a tile is clicked", async () => {
    const { app, host, onBand } = mount({
      kind: "band", minimumRange: 1, maximumRange: 1
    });
    press(cell(host, 3, 0));
    await nextTick();
    expect(onBand).toHaveBeenCalledWith({ minimumRange: 3, maximumRange: 3 });
    app.unmount();
  });

  it("sets both ends of the band when dragged from one ring to another", async () => {
    const { app, host, onBand } = mount({
      kind: "band", minimumRange: 1, maximumRange: 4
    });
    press(cell(host, 2, 0));
    cell(host, 4, 0).dispatchEvent(
      new MouseEvent("pointerenter", { bubbles: true, buttons: 1 })
    );
    await nextTick();
    expect(onBand).toHaveBeenLastCalledWith({ minimumRange: 2, maximumRange: 4 });
    app.unmount();
  });

  it("refuses the character's own tile rather than storing a band of zero", async () => {
    const { app, host, onBand } = mount({
      kind: "band", minimumRange: 1, maximumRange: 2
    });
    press(cell(host, 0, 0));
    await nextTick();
    expect(onBand).not.toHaveBeenCalled();
    expect(host.querySelector('[role="alert"]')?.textContent)
      .toContain("measured from where the character stands");
    app.unmount();
  });

  it("redraws when the stored numbers change beneath it", async () => {
    const { app, host } = mount({
      kind: "band", minimumRange: 1, maximumRange: 1
    });
    await nextTick();
    expect(stateOf(host, 2, 0)).toBe("outside");
    // What typing in the generated number control does: the props change and
    // the grid follows, with the author never touching the grid.
    app._instance!.props.maximumRange = 3;
    await nextTick();
    expect(stateOf(host, 2, 0)).toBe("covered");
    expect(stateOf(host, 3, 0)).toBe("covered");
    app.unmount();
  });

  it("says what extent it draws when the stored reach runs past it", async () => {
    const { app, host } = mount({
      kind: "band", minimumRange: 1, maximumRange: 40
    });
    await nextTick();
    expect(host.querySelector(".targeting-clipped")?.textContent)
      .toContain("The stored reach is further than the 8 tiles drawn here");
    app.unmount();
  });

  it("draws no such note when the whole shape fits", async () => {
    const { app, host } = mount({
      kind: "band", minimumRange: 1, maximumRange: 3
    });
    await nextTick();
    expect(host.querySelector(".targeting-clipped")).toBeNull();
    app.unmount();
  });
});

describe("the area of impact grid", () => {
  it("draws a diamond around the tile aimed at", async () => {
    const { app, host } = mount({
      kind: "area", areaShape: "diamond", radius: 2
    });
    await nextTick();
    expect(stateOf(host, 0, 0)).toBe("origin");
    expect(stateOf(host, 2, 0)).toBe("covered");
    expect(stateOf(host, 1, 1)).toBe("covered");
    // Manhattan, not Chebyshev: the far corner is outside a radius of two.
    expect(stateOf(host, 2, 2)).toBe("outside");
    app.unmount();
  });

  it("names the shape a clicked distance implies and clears an unread radius", async () => {
    const { app, host, onArea } = mount({
      kind: "area", areaShape: "single", radius: 0
    });
    press(cell(host, 1, 0));
    await nextTick();
    expect(onArea).toHaveBeenLastCalledWith({ areaShape: "cross", radius: 1 });
    press(cell(host, 3, 0));
    await nextTick();
    expect(onArea).toHaveBeenLastCalledWith({ areaShape: "diamond", radius: 3 });
    press(cell(host, 0, 0));
    await nextTick();
    expect(onArea).toHaveBeenLastCalledWith({ areaShape: "single", radius: 0 });
    app.unmount();
  });

  it("ignores a radius on a shape that does not read one", async () => {
    // The schema says the radius is used only by the diamond, and the engine's
    // `covered_by` is where that is true. A cross drawn at radius four would
    // be the grid disagreeing with the rules.
    const { app, host } = mount({ kind: "area", areaShape: "cross", radius: 4 });
    await nextTick();
    expect(stateOf(host, 1, 0)).toBe("covered");
    expect(stateOf(host, 2, 0)).toBe("outside");
    app.unmount();
  });
});

describe("painting a shape the vocabulary cannot express", () => {
  it("refuses a square blast and leaves the fields alone", async () => {
    const { app, host, onArea } = mount({
      kind: "area", areaShape: "cross", radius: 0
    });
    await nextTick();
    toggleMode(host, "Paint individual tiles");
    await nextTick();
    // A cross plus its diagonals is the square blast: the shape an author
    // reaches for first and the one the current enum has no name for.
    press(cell(host, 1, 1));
    await nextTick();
    expect(onArea).not.toHaveBeenCalled();
    expect(host.querySelector('[role="alert"]')?.textContent)
      .toContain("cannot");
    app.unmount();
  });

  it("refuses a band with a gap in it", async () => {
    const { app, host, onBand } = mount({
      kind: "band", minimumRange: 1, maximumRange: 1
    });
    await nextTick();
    toggleMode(host, "Paint individual tiles");
    await nextTick();
    // Adding one tile three steps out leaves distance two unpainted, which is
    // two bands rather than one.
    press(cell(host, 3, 0));
    await nextTick();
    expect(onBand).not.toHaveBeenCalled();
    expect(host.querySelector('[role="alert"]')).not.toBeNull();
    app.unmount();
  });

  it("accepts a freely painted shape that does happen to be expressible", async () => {
    const { app, host, onArea } = mount({
      kind: "area", areaShape: "single", radius: 0
    });
    await nextTick();
    toggleMode(host, "Paint individual tiles");
    await nextTick();
    // Completing the whole ring at distance one is a cross, and is stored.
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      press(cell(host, dx!, dy!));
      await nextTick();
    }
    expect(onArea).toHaveBeenLastCalledWith({ areaShape: "cross", radius: 1 });
    expect(host.querySelector('[role="alert"]')).toBeNull();
    app.unmount();
  });
});

describe("the composed view", () => {
  it("draws what a strike aimed at a chosen tile would cover", async () => {
    const { app, host } = mount({
      kind: "band",
      minimumRange: 2,
      maximumRange: 4,
      compose: { shape: "diamond", radius: 1 }
    });
    await nextTick();
    toggleMode(host, "Show what a strike aimed here covers");
    await nextTick();
    press(cell(host, 3, 0));
    await nextTick();
    expect(stateOf(host, 3, 0)).toBe("aim");
    expect(stateOf(host, 4, 0)).toBe("impact");
    expect(stateOf(host, 3, 1)).toBe("impact");
    // Beyond the area the band's own drawing is still what it was.
    expect(stateOf(host, 0, 2)).toBe("covered");
    app.unmount();
  });

  it("offers no composed view on a record carrying only a band", async () => {
    const { app, host } = mount({
      kind: "band", minimumRange: 1, maximumRange: 2
    });
    await nextTick();
    expect(host.querySelectorAll(".targeting-modes input")).toHaveLength(1);
    app.unmount();
  });
});

describe("reaching the grid without a pointer", () => {
  it("takes one tab stop and moves with the arrow keys", async () => {
    const { app, host } = mount({
      kind: "area", areaShape: "cross", radius: 0
    });
    await nextTick();
    const reachable = [...host.querySelectorAll<HTMLButtonElement>(".targeting-cell")]
      .filter((candidate) => candidate.tabIndex === 0);
    expect(reachable).toHaveLength(1);
    expect(reachable[0]!.dataset.cell).toBe("0:0");

    key(cell(host, 0, 0), "ArrowRight");
    await nextTick();
    expect(cell(host, 1, 0).tabIndex).toBe(0);
    expect(cell(host, 0, 0).tabIndex).toBe(-1);
    expect(document.activeElement).toBe(cell(host, 1, 0));
    app.unmount();
  });

  it("does not walk the focus off the drawn grid", async () => {
    const { app, host } = mount({
      kind: "area", areaShape: "single", radius: 0
    });
    await nextTick();
    // A radius of zero draws the smallest grid, three tiles out.
    for (let step = 0; step < 6; step += 1) {
      key(host.querySelector<HTMLButtonElement>('[tabindex="0"]')!, "ArrowUp");
      await nextTick();
    }
    expect(host.querySelector<HTMLButtonElement>('[tabindex="0"]')!.dataset.cell)
      .toBe("0:-3");
    app.unmount();
  });

  it("paints the focused tile from the keyboard and announces the change", async () => {
    const { app, host, onArea } = mount({
      kind: "area", areaShape: "single", radius: 0
    });
    await nextTick();
    key(cell(host, 0, 0), "ArrowRight");
    await nextTick();
    key(cell(host, 1, 0), "Enter");
    await nextTick();
    expect(onArea).toHaveBeenCalledWith({ areaShape: "cross", radius: 1 });
    expect(host.querySelector('[aria-live="polite"]')?.textContent)
      .toContain("Covers the tile aimed at");
    app.unmount();
  });

  it("labels every tile with its distance and its state", async () => {
    const { app, host } = mount({
      kind: "band", minimumRange: 2, maximumRange: 3
    });
    await nextTick();
    expect(cell(host, 0, 0).getAttribute("aria-label"))
      .toBe("the origin, where the character stands");
    expect(cell(host, 1, 0).getAttribute("aria-label"))
      .toBe("1 tile away, too close to strike");
    expect(cell(host, 2, 0).getAttribute("aria-label"))
      .toBe("2 tiles away, within reach");
    expect(cell(host, 4, 0).getAttribute("aria-label"))
      .toBe("4 tiles away, out of reach");
    app.unmount();
  });
});
