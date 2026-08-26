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

  // What each console makes of the size in the fields. `console-fit.test.ts`
  // holds the arithmetic against the C++ the machines compile; this holds that
  // an author is shown it, that it follows what they type, and — the part worth
  // guarding — that it never becomes a refusal.
  describe("what fits on a console", () => {
    function fitText(host: HTMLElement): string {
      return host.querySelector('[data-testid="console-fit"]')
        ?.textContent?.replace(/\s+/g, " ").trim() ?? "";
    }

    it("says a small board is drawn whole on both", () => {
      const { app, host } = mount();
      expect(fitText(host)).toBe("Drawn whole on both consoles.");
      app.unmount();
    });

    it("follows the fields as they are typed, before any resize is applied",
      async () => {
        const { app, host, onSave } = mount();
        const width = host.querySelector<HTMLInputElement>("#map-width")!;
        width.value = "40";
        width.dispatchEvent(new Event("input", { bubbles: true }));
        await nextTick();
        expect(fitText(host)).toContain("Scrolls on both consoles");
        expect(fitText(host)).toContain("21×14");
        expect(fitText(host)).toContain("20×13");
        // Guidance about what would happen, not a report of what has: nothing
        // was saved by typing.
        expect(onSave).not.toHaveBeenCalled();
        app.unmount();
      });

    it("names the cartridge alone in the band between the two windows",
      async () => {
        const { app, host } = mount();
        const width = host.querySelector<HTMLInputElement>("#map-width")!;
        const height = host.querySelector<HTMLInputElement>("#map-height")!;
        width.value = "21";
        width.dispatchEvent(new Event("input", { bubbles: true }));
        height.value = "14";
        height.dispatchEvent(new Event("input", { bubbles: true }));
        await nextTick();
        expect(fitText(host))
          .toContain("Drawn whole on the Nintendo 64. Scrolls on PlayStation");
        app.unmount();
      });

    // The whole point of the surface. A board that scrolls is valid and ships,
    // so nothing here may stop an author making one.
    it("never refuses a board a console has to scroll", async () => {
      const { app, host, onSave } = mount();
      const width = host.querySelector<HTMLInputElement>("#map-width")!;
      const height = host.querySelector<HTMLInputElement>("#map-height")!;
      width.value = "40";
      width.dispatchEvent(new Event("input", { bubbles: true }));
      height.value = "30";
      height.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
      expect(fitText(host)).toContain("Scrolls on both consoles");
      // Not an alert, and not styled as the crop warning beside it is.
      const notice = host.querySelector('[data-testid="console-fit"]')!;
      expect(notice.getAttribute("role")).toBeNull();
      expect(notice.classList.contains("crop-warning")).toBe(false);

      const apply = [...host.querySelectorAll<HTMLButtonElement>("button")]
        .find((candidate) => candidate.textContent === "Apply resize")!;
      expect(apply.disabled).toBe(false);
      apply.click();
      await nextTick();
      const result = onSave.mock.calls[0]![0] as SourceMap;
      expect([result.width, result.height]).toEqual([40, 30]);
      app.unmount();
    });

    it("says nothing at all about a field mid-edit", async () => {
      const { app, host } = mount();
      const width = host.querySelector<HTMLInputElement>("#map-width")!;
      width.value = "";
      width.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
      expect(host.querySelector('[data-testid="console-fit"]')).toBeNull();
      app.unmount();
    });
  });
});

describe("a board too big to draw all of", () => {
  /** A square board of `side` cells a side, all one terrain. */
  function board(side: number): SourceMap {
    return {
      id: "field", name: "Field", width: side, height: side,
      terrain: Array.from({ length: side * side }, () => "grass")
    } as SourceMap;
  }

  const cells = (host: HTMLElement) =>
    host.querySelectorAll('[role="gridcell"]').length;
  const grid = (host: HTMLElement) =>
    host.querySelector<HTMLElement>(".terrain-grid")!;

  /** jsdom lays nothing out, so the room the grid has is stated. */
  function withViewport(host: HTMLElement, width = 800, height = 600) {
    const element = grid(host);
    Object.defineProperty(element, "clientWidth", { value: width, configurable: true });
    Object.defineProperty(element, "clientHeight", { value: height, configurable: true });
    element.dispatchEvent(new Event("scroll"));
    return element;
  }

  it("draws a small board whole, exactly as it always did", async () => {
    // Under the threshold nothing changes: every cell is present and the
    // stretch-to-fit sizing a small map has is kept.
    const { app, host } = mount(board(16));
    await nextTick();
    expect(cells(host)).toBe(16 * 16);
    const row = host.querySelector<HTMLElement>(".terrain-row")!;
    expect(row.style.gridTemplateColumns).toContain("minmax(2.5rem, 1fr)");
    app.unmount();
  });

  it("draws only what fits, once a board is past the threshold", async () => {
    // 64x64 is 4096 cells and four DOM nodes each. Drawn whole that is what
    // made a big map stop a tab.
    const { app, host } = mount(board(64));
    await nextTick();
    withViewport(host);
    await nextTick();
    const drawn = cells(host);
    expect(drawn).toBeGreaterThan(0);
    expect(drawn).toBeLessThan(64 * 64 / 4);
    app.unmount();
  });

  it("stays bounded as the board grows, not proportional to it", async () => {
    // The point of a window: the same viewport draws about the same number of
    // cells whether the board is 64 a side or the format's own ceiling.
    const { app: smallApp, host: smallHost } = mount(board(64));
    await nextTick();
    withViewport(smallHost);
    await nextTick();
    const small = cells(smallHost);
    smallApp.unmount();

    const { app: bigApp, host: bigHost } = mount(board(256));
    await nextTick();
    withViewport(bigHost);
    await nextTick();
    const big = cells(bigHost);
    bigApp.unmount();

    expect(big).toBe(small);
    // And the ceiling of the format is drawn at all, which it was not before:
    // 256 a side is 65,536 cells and a quarter of a million nodes.
    expect(big).toBeLessThan(2000);
  });

  it("declares every track, so the board is the size it says it is", async () => {
    // The window fills only the visible tracks. The grid still declares them
    // all, which is what keeps the scrollbars honest with no spacer element.
    const { app, host } = mount(board(64));
    await nextTick();
    withViewport(host);
    await nextTick();
    expect(grid(host).style.gridTemplateRows).toBe("repeat(64, 40px)");
    const row = host.querySelector<HTMLElement>(".terrain-row")!;
    expect(row.style.gridTemplateColumns).toBe("repeat(64, 40px)");
    app.unmount();
  });

  it("puts a drawn cell in its own column, not in the order it was drawn",
     async () => {
    // A windowed row holds a slice of its cells. Placed by order they would sit
    // at the left edge; placed by column they sit where the board says.
    const { app, host } = mount(board(64));
    await nextTick();
    const element = withViewport(host);
    element.scrollLeft = 40 * 20;
    element.dispatchEvent(new Event("scroll"));
    await nextTick();
    const first = host.querySelector<HTMLElement>('[role="gridcell"]')!;
    expect(Number(first.style.gridColumn)).toBeGreaterThan(1);
    app.unmount();
  });

  it("follows the keyboard past the edge of what is drawn", async () => {
    // The case a window breaks if it is careless: an arrow key steps to a cell
    // that has not been drawn yet, and focusing nothing would strand the
    // keyboard at the edge of the viewport.
    const { app, host } = mount(board(64));
    await nextTick();
    withViewport(host, 400, 400);
    await nextTick();

    const start = host.querySelector<HTMLButtonElement>('[data-cell="0"]')!;
    start.focus();
    // Walk right well past the drawn window.
    for (let step = 0; step < 30; step += 1) {
      const at = document.activeElement as HTMLElement | null;
      at?.dispatchEvent(new KeyboardEvent("keydown", {
        key: "ArrowRight", bubbles: true, cancelable: true
      }));
      await nextTick();
    }
    const landed = document.activeElement as HTMLElement | null;
    expect(landed?.getAttribute("role")).toBe("gridcell");
    expect(Number(landed?.getAttribute("data-cell"))).toBe(30);
    app.unmount();
  });
});
