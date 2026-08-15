// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import { THEMES, WATER_RAMPS } from "../generated/board-art";
import {
  TRANSFER_BUCKETS,
  rampBuckets,
  rotatedRamp,
  tableValues,
  transferBucket,
  transferTable,
  waterRamp,
  waterTransfer,
  type Rgb
} from "./water-shimmer";
import {
  WATER_CYCLE_ENTRIES,
  WATER_CYCLE_PERIOD,
  WATER_CYCLE_STEP_FRAMES,
  waterCycleSource
} from "./board-motion";

/**
 * The browser's shimmer has to be the consoles' shimmer: the same four
 * colours, moved the same way, on the same phase. What is different is only
 * that it has no palette to move them in, so it moves them through a transfer
 * table instead, and these are the properties that make that table a faithful
 * stand-in rather than an approximation of one.
 */
describe("the water ramp the browser is handed", () => {
  it("holds a ramp for every theme the menu offers", () => {
    for (const theme of THEMES) {
      const ramp = waterRamp(theme.id);
      expect(ramp.length).toBeGreaterThanOrEqual(WATER_CYCLE_ENTRIES + 1);
    }
  });

  it("has steps a per-channel table can tell apart, in every theme", () => {
    // The generator asserts this too, and refuses to emit a ramp that fails
    // it. Restated here because it is the property the whole mechanism rests
    // on: if two steps of a ramp shared a bucket in one channel, the table
    // would move both of them together.
    for (const theme of THEMES) {
      expect(rampBuckets(waterRamp(theme.id))).toBe(true);
    }
  });

  it("names an unknown theme's ramp as empty rather than guessing", () => {
    expect(waterRamp("no-such-theme")).toEqual([]);
  });
});

describe("the rotation", () => {
  const ramp = WATER_RAMPS.temperate!;

  it("is the identity at phase zero", () => {
    expect(rotatedRamp(ramp, 0)).toEqual(ramp);
    expect(rotatedRamp(ramp, WATER_CYCLE_PERIOD)).toEqual(ramp);
    expect(rotatedRamp(ramp, WATER_CYCLE_STEP_FRAMES - 1)).toEqual(ramp);
  });

  it("leaves the anchor alone at every phase", () => {
    const anchor = ramp[0];
    for (let frame = 0; frame < WATER_CYCLE_PERIOD; frame += 1) {
      expect(rotatedRamp(ramp, frame)[0]).toEqual(anchor);
    }
  });

  it("is a bijection of the window at every phase", () => {
    const window = ramp.slice(ramp.length - WATER_CYCLE_ENTRIES);
    for (let frame = 0; frame < WATER_CYCLE_PERIOD; frame += 1) {
      const rotated = rotatedRamp(ramp, frame)
        .slice(ramp.length - WATER_CYCLE_ENTRIES);
      expect(new Set(rotated.map(String)).size).toBe(WATER_CYCLE_ENTRIES);
      for (const colour of rotated) {
        expect(window.map(String)).toContain(String(colour));
      }
    }
  });

  it("moves each step exactly where the shared model says", () => {
    const first = ramp.length - WATER_CYCLE_ENTRIES;
    for (const frame of [8, 16, 24]) {
      const rotated = rotatedRamp(ramp, frame);
      for (let slot = 0; slot < WATER_CYCLE_ENTRIES; slot += 1) {
        expect(rotated[first + slot]).toEqual(
          ramp[first + waterCycleSource(slot, frame)]
        );
      }
    }
  });
});

describe("the transfer table", () => {
  const ramp = WATER_RAMPS.temperate!;

  it("has one value per bucket", () => {
    expect(transferTable(ramp, 0, 8)).toHaveLength(TRANSFER_BUCKETS);
  });

  it("maps every step of the ramp onto its rotated colour, exactly", () => {
    for (const theme of THEMES) {
      const steps = waterRamp(theme.id);
      for (let frame = 0; frame < WATER_CYCLE_PERIOD; frame += 1) {
        const rotated = rotatedRamp(steps, frame);
        const tables = waterTransfer(theme.id, frame);
        steps.forEach((colour: Rgb, index: number) => {
          for (const channel of [0, 1, 2] as const) {
            const read = tables[channel]![transferBucket(colour[channel])]!;
            // Back out under *both* rules a renderer might apply. Chromium was
            // measured truncating; a table that only survived rounding would
            // draw every step one level dark there.
            expect(Math.round(read * 255)).toBe(rotated[index]![channel]);
            expect(Math.floor(read * 255)).toBe(rotated[index]![channel]);
          }
        });
      }
    }
  });

  it("is the identity table at phase zero", () => {
    const steps = WATER_RAMPS.winter!;
    const tables = waterTransfer("winter", 0);
    steps.forEach((colour: Rgb, index: number) => {
      expect(index).toBeGreaterThanOrEqual(0);
      for (const channel of [0, 1, 2] as const) {
        expect(
          Math.floor(tables[channel]![transferBucket(colour[channel])]! * 255)
        ).toBe(colour[channel]);
      }
    });
  });

  it("writes an attribute a renderer can read", () => {
    const attribute = tableValues(transferTable(ramp, 0, 8));
    expect(attribute.split(" ")).toHaveLength(TRANSFER_BUCKETS);
    expect(attribute).toMatch(/^[0-9. ]+$/);
  });
});
