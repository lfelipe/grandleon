// SPDX-License-Identifier: MIT
/**
 * The browser's half of the water shimmer: a palette rotation for a client
 * that has no palette.
 *
 * A console animates water by rotating four entries of its own five-step
 * colour ramp in the lookup table it already loaded: a TLUT override on the
 * Nintendo 64, with the darkest step left fixed as an anchor, a step held eight
 * frames and the period thirty-two. It touches no texel. The browser cannot do
 * that, because it does not hold indexed art: the board draws RGBA PNGs through SVG `<image>`, and there is no table
 * between the file and the screen.
 *
 * So it builds one. An SVG `feComponentTransfer` *is* a lookup table, three of
 * them, one per channel, and this module produces its contents: for a given
 * phase, the table that maps every colour of a theme's water ramp onto the
 * colour the consoles would have rotated into its place, and every other colour
 * onto itself.
 *
 * Three properties are load-bearing, and all three are the same ones the
 * consoles' rotation has:
 *
 * - **Phase zero is the identity**, and the caller is expected to draw *no
 *   filter at all* there, so a board at rest is node-for-node the board that
 *   was drawn before the shimmer existed.
 * - **No asset byte moves.** The committed sheets are identical with the
 *   shimmer present and absent. Shipping four recoloured PNGs per theme instead
 *   would have been the "generated terrain frames" route, which both consoles
 *   measured and refused.
 * - **The colours come from the generator**, per theme, rather than being
 *   restated here.
 *
 * Why a per-channel table can express a permutation of whole colours at all:
 * because a water ramp is a *ramp*. Its steps differ in every channel, so
 * "which step is this" can be decided from any one channel alone, and the
 * generator asserts exactly that when it publishes the ramps. A theme whose
 * steps collided in a channel fails the board-art build rather than shimmering
 * the wrong colours.
 */

import { WATER_RAMPS } from "../generated/board-art";
import { WATER_CYCLE_ENTRIES, waterCycleSource } from "./board-motion";

/**
 * How many buckets each channel's transfer table is divided into.
 *
 * `feComponentTransfer type="discrete"` with `n` values puts an input channel
 * `c` into bucket `floor(c * n / 255)`, capped at `n - 1`. The table therefore
 * has to be fine enough that no two steps of a ramp share a bucket. Thirty-two
 * is the smallest power of two that separates every ramp the library ships,
 * the tightest of them needing twenty-one, and `rampBuckets` refuses a ramp it
 * cannot separate rather than emitting a table that would merge two steps.
 */
export const TRANSFER_BUCKETS = 32;

/** Which bucket of the transfer table an 8-bit channel value falls in. */
export function transferBucket(value: number): number {
  const bucket = Math.floor((value * TRANSFER_BUCKETS) / 255);
  if (bucket < 0) return 0;
  return bucket > TRANSFER_BUCKETS - 1 ? TRANSFER_BUCKETS - 1 : bucket;
}

export type Rgb = readonly [number, number, number];

/** The colours a theme's water is drawn in, dark to light. Empty if unknown. */
export function waterRamp(theme: string): readonly Rgb[] {
  return WATER_RAMPS[theme] ?? [];
}

/**
 * The colour that belongs in each step of `ramp` on a given frame.
 *
 * Only the four steps at the light end move, and they move exactly as
 * `waterCycleSource` says: the step at slot `i` is drawn in the colour of slot
 * `i + phase`. Every step below the window keeps its own colour, being the
 * anchor the eye holds the tile by, which is why the surface reads as light
 * travelling across water rather than as the whole tile changing brightness.
 */
export function rotatedRamp(ramp: readonly Rgb[], frame: number): readonly Rgb[] {
  const first = ramp.length - WATER_CYCLE_ENTRIES;
  if (first < 0) return ramp;
  return ramp.map((colour, index) => {
    if (index < first) return colour;
    const slot = index - first;
    return ramp[first + waterCycleSource(slot, frame)] ?? colour;
  });
}

/**
 * The 0..1 table value a renderer reads back as the 8-bit channel `value`.
 *
 * Not `value / 255`, and the difference is measured rather than defensive: a
 * table saying exactly `56/255` comes back out of Chromium as **55**, because
 * the filter pipeline truncates where the arithmetic that produced the number
 * rounded. Truncation accepts anything in `[v, v+1)/255` and rounding anything
 * in `[v-0.5, v+0.5)/255`, so the only interval both agree on is
 * `[v, v+0.5)/255`, and this names its middle. A quarter of a level from the
 * value it means, and therefore the same colour under either rule.
 */
export function channelValue(value: number): number {
  return (value + 0.25) / 255;
}

/**
 * One channel's transfer table for a rotation of `ramp` on `frame`, as the 0..1
 * values `feComponentTransfer` wants.
 *
 * Buckets no step of the ramp falls in are the identity as closely as a
 * discrete table can be: the bucket's own lower edge. No pixel of a water
 * sheet has such a value, because the sheet is drawn in the ramp and nothing
 * else; they exist so the table is total rather than because anything reads
 * them.
 */
export function transferTable(
  ramp: readonly Rgb[],
  channel: 0 | 1 | 2,
  frame: number
): number[] {
  const table = Array.from(
    { length: TRANSFER_BUCKETS },
    (_, bucket) => bucket / TRANSFER_BUCKETS
  );
  const rotated = rotatedRamp(ramp, frame);
  ramp.forEach((colour, index) => {
    table[transferBucket(colour[channel])] =
      channelValue((rotated[index] ?? colour)[channel]);
  });
  return table;
}

/**
 * Whether a ramp's steps can be told apart in every channel, which is what
 * makes a per-channel table able to carry a permutation of whole colours.
 *
 * The generator asserts this when it publishes a ramp, so this is the editor's
 * own restatement of the same rule rather than a second source of truth for it.
 */
export function rampBuckets(ramp: readonly Rgb[]): boolean {
  for (const channel of [0, 1, 2] as const) {
    const buckets = new Set(
      ramp.map((colour) => transferBucket(colour[channel]))
    );
    if (buckets.size !== ramp.length) return false;
  }
  return true;
}

/** The three tables, in the order `feFuncR`, `feFuncG`, `feFuncB` want them. */
export function waterTransfer(theme: string, frame: number): number[][] {
  const ramp = waterRamp(theme);
  return [0, 1, 2].map((channel) =>
    transferTable(ramp, channel as 0 | 1 | 2, frame)
  );
}

/**
 * A table as the attribute value SVG reads. Six decimal places: a level is
 * about 0.0039 wide, so six places put the written number well inside the
 * quarter-level `channelValue` aimed at.
 */
export function tableValues(table: readonly number[]): string {
  return table.map((value) => value.toFixed(6)).join(" ");
}
