// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import tarnholtSource from "../../../games/tarnholt/source/project.json";
import { decodeSourceProject } from "./source-project-document";
import { endPlaytest, startPlaytest } from "./playtest-session";

// The shipped campaign's first board, reached by both pipelines.
//
// `demo-conformance.test.ts` beside this does the same for the demo, and the
// demo is a six-by-four bridge with one character a side and none of the knobs
// the engine has grown since. So the conformance this repository had covered
// the simplest board it ships and nothing else, while the Fordlight carries the
// waves, the reach bonus, the member specificity and the weapon triangle.
//
// The number is the native pipeline's. `games/tarnholt/src/play_tarnholt.cpp`
// compiles `project.json` to a package, loads the encounter out of it and
// asserts this same hash on the board it gets. The browser reaches the board
// from the source project directly with no compiler in between, so agreement
// says both roads derive the same identities, stats, placements and terrain.
//
// It cannot say anything about the weapon triangle, and the reason is worth
// knowing here rather than being discovered again: which kinds beat which is
// content the battle names rather than state it holds, so
// `tests/simulation/canonical_hash_test.cpp` deliberately keeps the table off
// the hash and pins that it stays off. Two boards differing only in their table
// are one arrangement. That is exactly how a browser which dropped the table
// went unnoticed, so the table is claimed where it can be -- over a fixture
// with a target in reach, in `weapon-playtest.test.ts`. The Fordlight opens
// with the two sides too far apart to price a shot at all.
const nativeFordlightOpeningHash = "24291ee6496e0494";

describe("tarnholt native conformance", () => {
  it("builds the Fordlight the compiler builds", () => {
    const project = decodeSourceProject(
      new TextEncoder().encode(JSON.stringify(tarnholtSource))
    );
    const started = startPlaytest(project);
    expect(started.error).toBeUndefined();
    const state = started.state!;
    // The campaign opens on a story node, so a playtest skips to the battle.
    expect(state.nodeId).toBe("fordlight_battle");
    expect(
      state.encounter.canonicalHash().toString(16).padStart(16, "0")
    ).toBe(nativeFordlightOpeningHash);
    endPlaytest(state);
  });

});
