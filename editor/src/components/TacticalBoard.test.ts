// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import TacticalBoard from "./TacticalBoard.vue";
import {
  Layer,
  boardDrawOrder,
  elevationStepFor,
  maxLiftFor,
  overlaps,
  type Projection
} from "../domain/board-view";

afterEach(() => document.body.replaceChildren());

function mount(overrides: Record<string, unknown> = {}) {
  const host = document.createElement("div");
  document.body.append(host);
  const chooseCell = vi.fn();
  const app = createApp(TacticalBoard, {
    width: 3,
    height: 2,
    terrain: ["grass", "water", "forest", "grass", "mountain", "bridge"],
    units: [{
      id: "guard",
      name: "Blue Guard",
      classId: "healer",
      side: "first",
      x: 0,
      y: 0,
      health: 3,
      onBoard: true,
      maximumHealth: 4,
      strength: 4,
      defense: 1
    }, {
      id: "raider",
      name: "Red Raider",
      side: "second",
      x: 2,
      y: 0,
      health: 4,
      onBoard: true,
      maximumHealth: 4,
      strength: 4,
      defense: 1
    }],
    selectedUnitId: "guard",
    legalMoveKeys: new Set(["1:0"]),
    legalTargetIds: new Set(["raider"]),
    onChooseCell: chooseCell,
    ...overrides
  });
  app.mount(host);
  return { app, host, chooseCell };
}

describe("TacticalBoard", () => {
  it("renders authored terrain, units, health, and tactical overlays", () => {
    const { app, host } = mount();
    expect(host.querySelectorAll('[role="gridcell"]')).toHaveLength(6);
    const sheetOf = (selector: string) =>
      host.querySelector(`${selector} .terrain-image`)?.getAttribute("href");
    expect(sheetOf(".terrain-grass")).toBe("/board/terrain/grass_blob.png");
    expect(sheetOf(".terrain-water")).toBe("/board/terrain/water_blob.png");
    // A bridge draws from the road sheet: presentation kinds, not raw strings.
    expect(sheetOf(".terrain-road")).toBe("/board/terrain/road_blob.png");
    expect(sheetOf(".terrain-forest")).toBe("/board/terrain/forest_blob.png");
    expect(sheetOf(".terrain-mountain")).toBe("/board/terrain/mountain_blob.png");
    // Each sprite is one autotiled tile cropped from the sheet, and the two
    // grass cells sit in different neighbourhoods, so their crops differ.
    const grassViews = [...host.querySelectorAll(".terrain-grass .terrain-sprite")]
      .map((sprite) => sprite.getAttribute("viewBox"));
    expect(grassViews).toHaveLength(2);
    expect(grassViews[0]).toMatch(/^\d+ \d+ 32 32$/);
    expect(grassViews[0]).not.toBe(grassViews[1]);
    expect(host.querySelector(".unit-first .unit-sprite")?.getAttribute("href"))
      .toBe("/board/characters/healer_blue.png");
    expect(host.querySelector(".unit-second .unit-sprite")?.getAttribute("href"))
      .toBe("/board/characters/knight_red.png");
    expect(host.querySelector(".hp-value")?.getAttribute("fill")).toBe("#79d06e");
    expect(host.querySelectorAll(".terrain-legend li")).toHaveLength(5);
    // The key names the ground that charges more than a step, so a range that
    // stops short of a visible tile has a stated reason under the board.
    const legend = [...host.querySelectorAll(".terrain-legend li")].map(
      (entry) => entry.textContent!.trim()
    );
    expect(legend).toContain("forest (heavy going)");
    expect(legend).toContain("grass");
    expect(host.querySelector(".board-shell")?.getAttribute("style"))
      .toContain("--board-width: 14.75rem");
    expect(host.querySelector('[aria-label^="Position 0, 0"]')?.textContent)
      .toContain("Blue Guard HP 3/4");
    expect(host.querySelector('[aria-label^="Position 0, 0"]')?.classList)
      .toContain("selected");
    expect(host.querySelector('[aria-label^="Position 1, 0"]')?.classList)
      .toContain("legal");
    expect(host.querySelector('[aria-label^="Position 2, 0"]')?.classList)
      .toContain("target");
    app.unmount();
  });

  it("washes the tiles an enemy could reach, and says so out loud", () => {
    const { app, host } = mount({ dangerTileKeys: new Set(["1:0", "1:1"]) });
    expect(host.querySelectorAll(".danger-wash")).toHaveLength(2);
    // A tinted square is no use to a player who cannot see the tint.
    expect(host.querySelector('[aria-label^="Position 1, 1"]')
      ?.getAttribute("aria-label")).toContain("the enemy can reach here");
    expect(host.querySelector('[aria-label^="Position 2, 0"]')
      ?.getAttribute("aria-label")).not.toContain("the enemy can reach here");
    app.unmount();
  });

  it("draws no danger at all when nothing is selected", () => {
    const { app, host } = mount();
    expect(host.querySelectorAll(".danger-wash")).toHaveLength(0);
    app.unmount();
  });

  it("exposes rows and moves its single tab stop with the arrow keys", async () => {
    const { app, host } = mount();
    const rows = host.querySelectorAll('[role="grid"] [role="row"]');
    expect(rows).toHaveLength(2);
    expect(rows[0]!.querySelectorAll('[role="gridcell"]')).toHaveLength(3);
    const stops = [...host.querySelectorAll<HTMLButtonElement>('[role="gridcell"]')]
      .filter((candidate) => candidate.tabIndex === 0);
    expect(stops).toHaveLength(1);
    expect(stops[0]!.getAttribute("aria-label")).toContain("Position 0, 0");

    stops[0]!.dispatchEvent(new KeyboardEvent("keydown", {
      key: "ArrowDown",
      bubbles: true
    }));
    await nextTick();
    const active = document.activeElement as HTMLButtonElement;
    expect(active.getAttribute("aria-label")).toContain("Position 0, 1");
    expect(active.tabIndex).toBe(0);
    expect(
      [...host.querySelectorAll<HTMLButtonElement>('[role="gridcell"]')]
        .filter((candidate) => candidate.tabIndex === 0)
    ).toHaveLength(1);
    app.unmount();
  });

  it("draws a board with nothing raised exactly where it always drew it", () => {
    const { app, host } = mount({
      terrain: ["grass", "water", "forest", "grass", "bridge", "swamp"]
    });
    const svg = host.querySelector(".tactical-board")!;
    expect(svg.getAttribute("viewBox")).toBe("0 0 332 232");
    expect(host.querySelector(".tactical-board > g[transform]")
      ?.getAttribute("transform")).toBe("translate(32 32)");
    expect([...host.querySelectorAll(".board-cell")]
      .map((cell) => cell.getAttribute("transform"))).toEqual([
        "translate(0 0)", "translate(100 0)", "translate(200 0)",
        "translate(0 100)", "translate(100 100)", "translate(200 100)"
      ]);
    app.unmount();
  });

  it("lifts raised terrain by its own height and reserves room for it", () => {
    // The fixture's mountain stands at 1,1: two levels of a quarter-tile step
    // ask for 50 on a 100px cell, and the model's cap answers 37, three
    // eighths of the cell, which is what keeps a raised cell off the centre of
    // the cell behind it.
    const { app, host } = mount();
    const lifted = maxLiftFor(100);
    expect(lifted).toBe(37);
    const cellAt = (index: number) =>
      host.querySelectorAll(".board-cell")[index]!.getAttribute("transform");
    expect(cellAt(4)).toBe(`translate(100 ${100 - lifted})`);
    // Everything at ground level is where it was.
    expect(cellAt(0)).toBe("translate(0 0)");
    expect(cellAt(5)).toBe("translate(200 100)");
    // The frame grows by the tallest lift and the board moves down into it, so
    // a raised cell in the first row rises into empty frame rather than off it.
    expect(host.querySelector(".tactical-board")?.getAttribute("viewBox"))
      .toBe(`0 0 332 ${232 + lifted}`);
    expect(host.querySelector(".tactical-board > g[transform]")
      ?.getAttribute("transform")).toBe(`translate(32 ${32 + lifted})`);
    // The row numbers follow the board down; the column numbers stay at the
    // top of the gutter, where a raised first row cannot cover them.
    const labels = [...host.querySelectorAll(".coordinates text")];
    expect(labels.slice(0, 3).map((text) => text.getAttribute("y")))
      .toEqual(["21", "21", "21"]);
    expect(labels.slice(3).map((text) => text.getAttribute("y")))
      .toEqual([`${32 + lifted + 53}`, `${32 + lifted + 153}`]);
    app.unmount();
  });

  it("never lets a cell's group cover the centre of the cell behind it", () => {
    // The one thing the cap buys a board that cannot express a layer stack:
    // whatever a cell paints over the cell behind it, it stops short of that
    // cell's middle, so the cell behind keeps its terrain, its frame and the
    // centre its move marker sits on, however the relief is authored. Read off
    // the emitted transforms, so it is the document being checked rather than
    // the model it came from.
    const width = 3;
    const height = 3;
    const terrain = [
      "grass", "mountain", "hill",
      "mountain", "grass", "mountain",
      "hill", "mountain", "grass"
    ];
    const { app, host } = mount({ width, height, terrain, units: [] });
    const tops = [...host.querySelectorAll(".board-cell")].map((cell) => {
      const match = /translate\((-?\d+) (-?\d+)\)/.exec(
        cell.getAttribute("transform") ?? ""
      );
      return Number(match![2]);
    });
    for (let y = 0; y + 1 < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        const behindCentre = tops[y * width + x]! + 50;
        const frontTop = tops[(y + 1) * width + x]!;
        expect(frontTop).toBeGreaterThan(behindCentre);
      }
    }
    app.unmount();
  });

  it("grounds every unit on a shadow drawn under the sprite", () => {
    const { app, host } = mount();
    expect(host.querySelectorAll(".unit-shadow")).toHaveLength(2);
    for (const unit of host.querySelectorAll(".unit")) {
      const images = [...unit.querySelectorAll("image")];
      // The shadow comes first, so the figure stands on it rather than
      // behind it, and it shares the sprite's rectangle exactly so it can
      // never reach into a neighbouring cell.
      expect(images.map((image) => image.getAttribute("class")))
        .toEqual(["unit-shadow", "unit-sprite"]);
      expect(images[0]!.getAttribute("href")).toBe("/board/characters/shadow.png");
      for (const image of images) {
        expect([
          image.getAttribute("x"), image.getAttribute("y"),
          image.getAttribute("width"), image.getAttribute("height")
        ]).toEqual(["8", "0", "84", "84"]);
      }
      // It is decoration: the cell's button carries what a screen reader says.
      expect(images[0]!.getAttribute("aria-hidden")).toBe("true");
    }
    app.unmount();
  });

  it("emits the cells in an order the presentation model agrees with", () => {
    // The component draws row by row because the accessibility tree needs it
    // to, and the model sorts by elevation before row. The two orders differ,
    // and the difference is unobservable: they part only over pairs of cells
    // that do not share a pixel. So rather than pinning a hand-written list,
    // this derives the picture-relevant part of the model's order and checks
    // the document against it.
    const width = 3;
    const height = 3;
    const terrain = [
      "grass", "mountain", "grass",
      "hill", "grass", "mountain",
      "grass", "grass", "grass"
    ];
    const { app, host } = mount({ width, height, terrain, units: [] });
    const projection: Projection = {
      originX: 0, originY: 0, tile: 100, elevationStep: elevationStepFor(100)
    };
    const ground = boardDrawOrder({ terrain, width, height }, projection)
      .filter((item) => item.layer === Layer.terrain);
    const document_ = [...host.querySelectorAll(".board-cell")].map((cell) =>
      Number(cell.querySelector("[data-cell]")!.getAttribute("data-cell"))
    );
    expect(document_).toHaveLength(width * height);
    expect([...ground].map((item) => item.cellY * width + item.cellX).sort())
      .toEqual([...document_].sort());

    const position = new Map(document_.map((index, at) => [index, at]));
    let covered = 0;
    for (let behind = 0; behind < ground.length; behind += 1) {
      for (let front = behind + 1; front < ground.length; front += 1) {
        const back = ground[behind]!;
        const near = ground[front]!;
        if (!overlaps(back, near)) continue;
        covered += 1;
        expect(position.get(back.cellY * width + back.cellX))
          .toBeLessThan(position.get(near.cellY * width + near.cellX)!);
      }
    }
    // The board is not flat, so there is something to have got wrong: the
    // hill and the mountain each hang over the cell behind them.
    expect(covered).toBe(2);
    app.unmount();
  });

  it("draws a still board with no motion attribute at all", () => {
    // The property every other assertion in this file rests on: motion is
    // additive. A board nobody is animating is, node for node, the board that
    // was drawn before motion existed.
    const { app, host } = mount();
    expect(host.querySelector("g.unit")?.hasAttribute("transform")).toBe(false);
    expect(host.querySelectorAll(".cursor-pulse")).toHaveLength(0);
    expect(host.querySelectorAll(".miss-flash")).toHaveLength(0);
    app.unmount();
  });

  it("draws one token away from its cell while it is moving", () => {
    // A tenth of a cell to the left and a fifth up, on a 100px cell.
    const { app, host } = mount({
      motion: { unitId: "guard", cellDx: -0.1, cellDy: -0.2 }
    });
    const units = host.querySelectorAll<SVGGElement>("g.unit");
    expect(units[0]?.getAttribute("transform")).toBe("translate(-10 -20)");
    // And nobody else moves.
    expect(units[1]?.hasAttribute("transform")).toBe(false);
    // The cell groups are where they always were: the token moves inside its
    // own cell, so the board's rows, its order and its labels are untouched.
    const cells = host.querySelectorAll<SVGGElement>(".board-cell");
    expect(cells[0]?.getAttribute("transform")).toBe("translate(0 0)");
    expect(host.querySelectorAll('[role="gridcell"]')).toHaveLength(6);
    app.unmount();
  });

  it("pulses only the selected cell, and only when asked", () => {
    const { app, host } = mount({ cursorEmphasis: true });
    const pulses = host.querySelectorAll(".cursor-pulse");
    expect(pulses).toHaveLength(1);
    // Inside the cell's own frame, and nowhere near the centre a console probe
    // samples: the ring's box stops well short of 50,50.
    const ring = pulses[0]!;
    expect(Number(ring.getAttribute("x"))).toBeGreaterThan(0);
    expect(
      Number(ring.getAttribute("x")) + Number(ring.getAttribute("width"))
    ).toBeLessThan(100);
    app.unmount();
  });

  it("marks the cell a blow missed", () => {
    const { app, host } = mount({ missKey: "2:0" });
    const flashes = host.querySelectorAll(".miss-flash");
    expect(flashes).toHaveLength(1);
    expect(
      flashes[0]?.closest(".board-cell")?.getAttribute("transform")
    ).toBe("translate(200 0)");
    app.unmount();
  });

  it("draws a standing unit from its own sprite and never from the strip", () => {
    const { app, host } = mount();
    expect(host.querySelectorAll("svg.unit-frame")).toHaveLength(0);
    const sprites = host.querySelectorAll<SVGImageElement>("image.unit-sprite");
    expect(sprites).toHaveLength(2);
    for (const sprite of sprites) {
      expect(sprite.getAttribute("href")).not.toContain("_frames");
    }
    app.unmount();
  });

  it("windows the sequence strip for a posed unit, and only that one", () => {
    const { app, host } = mount({ sequenceCells: { guard: 1 } });
    const frames = host.querySelectorAll<SVGSVGElement>("svg.unit-frame");
    expect(frames).toHaveLength(1);
    // Cell 1 of a 32-pixel strip, drawn into the same 84-pixel box the
    // standing sprite occupies, the same window the consoles take.
    expect(frames[0]?.getAttribute("viewBox")).toBe("32 0 32 32");
    expect(frames[0]?.getAttribute("width")).toBe("84");
    const strip = frames[0]?.querySelector("image.unit-sprite");
    expect(strip?.getAttribute("href")).toContain("_frames.png");
    // Four cells of 32 pixels, walk contact, walk pass, lunge and cast, and
    // this pins the whole strip rather than the window.
    expect(strip?.getAttribute("width")).toBe("128");
    // The other unit is standing, from its own file.
    expect(
      host.querySelectorAll("g.unit > image.unit-sprite")
    ).toHaveLength(1);
    app.unmount();
  });

  it("treats the standing cell as standing rather than as cell -1", () => {
    const { app, host } = mount({ sequenceCells: { guard: -1 } });
    expect(host.querySelectorAll("svg.unit-frame")).toHaveLength(0);
    app.unmount();
  });

  it("emits no shimmer at all at phase zero", () => {
    const { app, host } = mount({ waterPhaseFrame: 0 });
    expect(host.querySelectorAll("filter")).toHaveLength(0);
    expect(
      host.querySelector(".terrain-image")?.hasAttribute("filter")
    ).toBe(false);
    app.unmount();
  });

  it("hangs the shimmer on the water cells and on nothing else", () => {
    // Eight frames a step: frame 8 is phase one, the first phase that moves.
    const { app, host } = mount({ waterPhaseFrame: 8 });
    const filters = host.querySelectorAll("filter");
    expect(filters).toHaveLength(1);
    // Three per-channel transfer tables, discrete, one value per bucket.
    const funcs = filters[0]!.querySelectorAll(
      "feFuncR, feFuncG, feFuncB"
    );
    expect(funcs).toHaveLength(3);
    for (const func of funcs) {
      expect(func.getAttribute("type")).toBe("discrete");
      expect(func.getAttribute("tableValues")?.split(" ")).toHaveLength(32);
    }
    const filtered = [...host.querySelectorAll(".terrain-image")]
      .filter((image) => image.hasAttribute("filter"));
    expect(filtered).toHaveLength(1);
    expect(filtered[0]?.getAttribute("href")).toContain("water");
    app.unmount();
  });

  it("supports pointer and keyboard cell choice", async () => {
    const { app, host, chooseCell } = mount();
    const cell = host.querySelector<SVGGElement>('[aria-label^="Position 1, 0"]')!;
    cell.dispatchEvent(new MouseEvent("click", { bubbles: true }));
    cell.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true }));
    await nextTick();
    expect(chooseCell).toHaveBeenNthCalledWith(1, 1, 0);
    expect(chooseCell).toHaveBeenNthCalledWith(2, 1, 0);
    app.unmount();
  });
});
