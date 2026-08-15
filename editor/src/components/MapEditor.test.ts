// SPDX-License-Identifier: MIT
import { createApp, h, nextTick, shallowRef } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { SourceMap } from "../generated/source-v1";
import MapEditor from "./MapEditor.vue";
import { terrainSprite } from "../domain/board-art";
import { TILE_SIZE } from "../generated/board-art";

afterEach(() => document.body.replaceChildren());

const fixture: SourceMap = {
  id: "field",
  name: "Field",
  width: 3,
  height: 2,
  terrain: ["grass", "grass", "water", "grass", "rock", "water"]
};

function mount(map = fixture) {
  const host = document.createElement("div");
  document.body.append(host);
  const onSave = vi.fn();
  const app = createApp(MapEditor, { map, onSave });
  app.mount(host);
  return { app, host, onSave };
}

/**
 * Mounted the way the workspace mounts it: the save goes into the project and
 * the stored board comes straight back down as the prop.
 *
 * `mount` above leaves that loop open, which is why every test here passed
 * while Undo and Redo were dead in the product: the editor was only ever
 * asked to paint, never to survive hearing its own save. `ContentWorkspace`'s
 * `saveMap` copies width, height and a fresh terrain array onto the stored
 * record, so the object that arrives back is a new one with equal contents,
 * and that is what this reproduces.
 */
function mountWired(map = fixture) {
  const host = document.createElement("div");
  document.body.append(host);
  const stored = shallowRef<SourceMap>({ ...map, terrain: [...map.terrain] });
  const onSave = vi.fn((saved: SourceMap) => {
    stored.value = {
      ...stored.value,
      width: saved.width,
      height: saved.height,
      terrain: [...saved.terrain]
    };
  });
  const app = createApp({
    render: () => h(MapEditor, { map: stored.value, onSave })
  });
  app.mount(host);
  return { app, host, onSave, stored };
}

function terrainButton(host: HTMLElement, name: string): HTMLButtonElement {
  return [...host.querySelectorAll<HTMLButtonElement>(".terrain-palette button")]
    // Palette buttons carry a terrain glyph before the name, so match on the
    // name rather than on the whole label.
    .find((candidate) => candidate.textContent?.trim().endsWith(name))!;
}

function historyButton(host: HTMLElement, name: string): HTMLButtonElement {
  return [...host.querySelectorAll<HTMLButtonElement>(".map-history button")]
    .find((candidate) => candidate.textContent?.trim() === name)!;
}

describe("MapEditor", () => {
  it("renders an accessible grid and paints a cell with the selected terrain", async () => {
    const { app, host, onSave } = mount();
    expect(host.querySelectorAll('[role="gridcell"]')).toHaveLength(6);
    terrainButton(host, "sand").click();
    const cell = host.querySelector<HTMLButtonElement>(
      '[aria-label="Column 2, row 1: grass"]'
    )!;
    cell.dispatchEvent(new MouseEvent("pointerdown", { bubbles: true, button: 0 }));
    await nextTick();
    expect(onSave).toHaveBeenCalledTimes(1);
    expect(onSave.mock.calls[0]![0].terrain).toEqual([
      "grass", "sand", "water", "grass", "rock", "water"
    ]);
    app.unmount();
  });

  it("supports keyboard painting", async () => {
    const { app, host, onSave } = mount();
    terrainButton(host, "forest").click();
    const cell = host.querySelector<HTMLButtonElement>('[role="gridcell"]')!;
    cell.dispatchEvent(new KeyboardEvent("keydown", {
      key: "Enter",
      bubbles: true
    }));
    await nextTick();
    expect(onSave.mock.calls[0]![0].terrain[0]).toBe("forest");
    app.unmount();
  });

  it("structures the grid as rows with one roving tab stop", async () => {
    const { app, host } = mount();
    const rows = host.querySelectorAll('[role="grid"] > [role="row"]');
    expect(rows).toHaveLength(2);
    expect(rows[0]!.querySelectorAll('[role="gridcell"]')).toHaveLength(3);
    // A large map must not cost one Tab press per cell: exactly one cell is
    // tabbable and the arrow keys move between them.
    const stops = [...host.querySelectorAll<HTMLButtonElement>('[role="gridcell"]')]
      .filter((candidate) => candidate.tabIndex === 0);
    expect(stops).toHaveLength(1);
    expect(stops[0]!.getAttribute("aria-label")).toBe("Column 1, row 1: grass");

    stops[0]!.dispatchEvent(new KeyboardEvent("keydown", {
      key: "ArrowRight",
      bubbles: true
    }));
    await nextTick();
    const next = host.querySelector<HTMLButtonElement>(
      '[aria-label="Column 2, row 1: grass"]'
    )!;
    expect(document.activeElement).toBe(next);
    expect(next.tabIndex).toBe(0);
    expect(
      [...host.querySelectorAll<HTMLButtonElement>('[role="gridcell"]')]
        .filter((candidate) => candidate.tabIndex === 0)
    ).toHaveLength(1);

    next.dispatchEvent(new KeyboardEvent("keydown", {
      key: "ArrowDown",
      bubbles: true
    }));
    await nextTick();
    expect(document.activeElement?.getAttribute("aria-label"))
      .toBe("Column 2, row 2: rock");
    // The edges stay put instead of wrapping to the far side.
    document.activeElement!.dispatchEvent(new KeyboardEvent("keydown", {
      key: "ArrowDown",
      bubbles: true
    }));
    await nextTick();
    expect(document.activeElement?.getAttribute("aria-label"))
      .toBe("Column 2, row 2: rock");
    app.unmount();
  });

  it("fills new cells deterministically when growing from the bottom-right", async () => {
    const { app, host, onSave } = mount();
    const width = host.querySelector<HTMLInputElement>("#map-width")!;
    const height = host.querySelector<HTMLInputElement>("#map-height")!;
    // The fill is chosen from the brushes this board has, never typed: a slip
    // here paints a whole row and column at once.
    const fill = host.querySelector<HTMLSelectElement>("#resize-fill")!;
    expect(fill.tagName).toBe("SELECT");
    width.value = "4";
    width.dispatchEvent(new Event("input", { bubbles: true }));
    height.value = "3";
    height.dispatchEvent(new Event("input", { bubbles: true }));
    fill.value = "sand";
    fill.dispatchEvent(new Event("change", { bubbles: true }));
    [...host.querySelectorAll<HTMLButtonElement>("button")]
      .find((candidate) => candidate.textContent === "Apply resize")!.click();
    await nextTick();
    const result = onSave.mock.calls[0]![0] as SourceMap;
    expect([result.width, result.height]).toEqual([4, 3]);
    expect(result.terrain).toEqual([
      "grass", "grass", "water", "sand",
      "grass", "rock", "water", "sand",
      "sand", "sand", "sand", "sand"
    ]);
    app.unmount();
  });

  it("requires explicit confirmation before cropping", async () => {
    const { app, host, onSave } = mount();
    const width = host.querySelector<HTMLInputElement>("#map-width")!;
    width.value = "2";
    width.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(host.textContent).toContain("crops 2 existing cells");
    const apply = [...host.querySelectorAll<HTMLButtonElement>("button")]
      .find((candidate) => candidate.textContent === "Apply resize")!;
    apply.click();
    await nextTick();
    expect(onSave).not.toHaveBeenCalled();
    expect(host.textContent).toContain("Allow cropping");

    const confirm = host.querySelector<HTMLInputElement>(
      ".crop-confirm input"
    )!;
    confirm.click();
    apply.click();
    await nextTick();
    expect(onSave.mock.calls[0]![0].terrain).toEqual([
      "grass", "grass", "grass", "rock"
    ]);
    app.unmount();
  });

  it("draws each cell in the artwork the game is played in", async () => {
    const { app, host } = mount();
    const cells = [...host.querySelectorAll<SVGSVGElement>(
      '[role="gridcell"] .terrain-art'
    )];
    expect(cells).toHaveLength(6);

    // The same lookup the play board and the consoles apply: the sheet for the
    // cell's own terrain, cropped to the variant its neighbours choose.
    for (const [index, cell] of cells.entries()) {
      const expected = terrainSprite(
        fixture.terrain, fixture.width, fixture.height,
        index % fixture.width, Math.floor(index / fixture.width)
      );
      expect(
        cell.querySelector("image")?.getAttribute("href"), String(index)
      ).toContain(expected.href);
      expect(cell.getAttribute("viewBox"), String(index))
        .toBe(`${expected.sx} ${expected.sy} ${TILE_SIZE} ${TILE_SIZE}`);
    }
    // The artwork is decoration over controls that already say everything: the
    // cell keeps its coordinates, its terrain and its mark.
    expect(cells[2]!.getAttribute("aria-hidden")).toBe("true");
    const water = host.querySelector('[aria-label="Column 3, row 1: water"]')!;
    expect(water.querySelector(".terrain-glyph")?.textContent?.trim()).toBe("≈");
    app.unmount();
  });

  it("keeps inventing a terrain deliberate, and says what a name will become",
    async () => {
      const { app, host } = mount();
      // Behind a lid, closed, under fourteen brushes that cover everything the
      // art library draws. A typo is the one way a mistake becomes data here,
      // so the way to type one is a place you go rather than a box you land in.
      const invent = host.querySelector<HTMLDetailsElement>(".invent-terrain")!;
      expect(invent).not.toBeNull();
      expect(invent.hasAttribute("open")).toBe(false);
      const name = invent.querySelector<HTMLInputElement>("#custom-terrain")!;
      const use = [...invent.querySelectorAll<HTMLButtonElement>("button")]
        .find((candidate) => candidate.textContent?.trim() === "Use terrain")!;
      // Nothing typed is nothing to press.
      expect(use.disabled).toBe(true);

      // A name the library does recognise says which of its kinds it will be.
      name.value = "deep river";
      name.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
      expect(invent.textContent).toContain("'deep river' is drawn as water");
      expect(invent.textContent).toContain("crossed only by fliers");

      // And one it does not says so, and says what the game will do with it,
      // before it is painted across a board rather than after.
      name.value = "obsidian shelf";
      name.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
      expect(invent.textContent).toContain(
        "'obsidian shelf' matches nothing the art library draws, so it is " +
        "drawn flat and is walked by anyone."
      );
      expect(use.disabled).toBe(false);

      // Pressing it makes a brush of it, on a board no cell of which has it
      // yet, since otherwise the press that invented it would be the last
      // chance to use it.
      use.click();
      await nextTick();
      const brushes = [
        ...host.querySelectorAll<HTMLButtonElement>(".terrain-palette button")
      ].map((brush) => brush.textContent?.trim());
      expect(brushes).toContain("Oobsidian shelf");
      const chosen = host.querySelector<HTMLButtonElement>(
        '.terrain-palette button[aria-pressed="true"]'
      );
      expect(chosen?.textContent?.trim()).toBe("Oobsidian shelf");
      // And the resize menu offers it too, rather than only the fourteen.
      const fill = host.querySelector<HTMLSelectElement>("#resize-fill")!;
      expect([...fill.options].map((option) => option.value))
        .toContain("obsidian shelf");
      app.unmount();
    });

  it("draws an invented terrain the way the game will draw it, and marks it",
    async () => {
      const { app, host } = mount({
        id: "strange", name: "Strange", width: 1, height: 1,
        terrain: ["obsidian shelf"]
      });
      const cell = host.querySelector<HTMLButtonElement>('[role="gridcell"]')!;
      expect(cell.getAttribute("aria-label"))
        .toBe("Column 1, row 1: obsidian shelf");
      // Neither the art library nor the game has a sheet for it, and both fall
      // back to the same one, so the editor shows the author what they will
      // get rather than what they might have hoped for.
      expect(
        cell.querySelector(".terrain-art image")?.getAttribute("href")
      ).toContain(terrainSprite(["obsidian shelf"], 1, 1, 0, 0).href);
      // The mark is what keeps it from reading as ordinary grass: an invented
      // terrain falls back to its own initial.
      expect(cell.querySelector(".terrain-glyph")?.textContent?.trim()).toBe("O");
      app.unmount();
    });

  it("undoes and redoes painting, and says how much can still be undone", async () => {
    const { app, host, onSave } = mount();
    const undo = historyButton(host, "Undo");
    const redo = historyButton(host, "Redo");
    // Offered as unavailable rather than absent, so an author knows it exists.
    expect(undo.disabled).toBe(true);
    expect(redo.disabled).toBe(true);
    expect(host.querySelector(".map-history p")?.textContent)
      .toContain("Nothing to undo yet");

    terrainButton(host, "sand").click();
    const cell = host.querySelector<HTMLButtonElement>(
      '[aria-label="Column 2, row 1: grass"]'
    )!;
    cell.dispatchEvent(new MouseEvent("pointerdown", { bubbles: true, button: 0 }));
    await nextTick();
    expect(undo.disabled).toBe(false);
    expect(host.querySelector(".map-history p")?.textContent)
      .toContain("1 edit can be undone");

    undo.click();
    await nextTick();
    // The map is what it was, and the surrounding project hears about it the
    // same way it hears about a stroke.
    expect(onSave.mock.lastCall?.[0].terrain).toEqual(fixture.terrain);
    expect(host.querySelector('[aria-label="Column 2, row 1: grass"]'))
      .not.toBeNull();
    expect(redo.disabled).toBe(false);

    redo.click();
    await nextTick();
    expect(onSave.mock.lastCall?.[0].terrain[1]).toBe("sand");
    app.unmount();
  });

  it("keeps its history when the project echoes each stroke back", async () => {
    // The board arriving back after a save is this editor's own work returning,
    // not somebody else's change, and rebuilding the session on it is what left
    // Undo permanently disabled: every stroke destroyed the history the stroke
    // had just added to.
    const { app, host, stored } = mountWired();
    const undo = historyButton(host, "Undo");
    terrainButton(host, "sand").click();

    for (const label of ["Column 2, row 1", "Column 3, row 1", "Column 1, row 2"]) {
      host.querySelector<HTMLButtonElement>(`[aria-label^="${label}:"]`)!
        .dispatchEvent(new MouseEvent("pointerdown", { bubbles: true, button: 0 }));
      await nextTick();
    }
    expect(undo.disabled).toBe(false);
    expect(host.querySelector(".map-history p")?.textContent)
      .toContain("3 edits can be undone");

    // And each one comes back off, in order, all the way to the board it began
    // as. The depth is real rather than a number beside a dead button.
    for (let remaining = 2; remaining >= 0; remaining -= 1) {
      historyButton(host, "Undo").click();
      await nextTick();
    }
    expect(stored.value.terrain).toEqual(fixture.terrain);
    expect(historyButton(host, "Undo").disabled).toBe(true);
    app.unmount();
  });

  it("takes a change made anywhere else, and drops a history that outlived it", async () => {
    // The other half of the same judgement. An import, a rename, or an undo
    // taken from the project's own history describes a board this editor's
    // history no longer applies to, so that history has to go.
    const { app, host, stored } = mountWired();
    terrainButton(host, "sand").click();
    host.querySelector<HTMLButtonElement>('[aria-label^="Column 2, row 1:"]')!
      .dispatchEvent(new MouseEvent("pointerdown", { bubbles: true, button: 0 }));
    await nextTick();
    expect(historyButton(host, "Undo").disabled).toBe(false);

    stored.value = { ...fixture, name: "Somewhere else", terrain: [...fixture.terrain] };
    await nextTick();
    expect(historyButton(host, "Undo").disabled).toBe(true);
    expect(host.querySelector('[aria-label="Column 2, row 1: grass"]')).not.toBeNull();
    app.unmount();
  });
});
