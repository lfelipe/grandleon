// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import { createDemoProject } from "../sample-projects";
import { stableContentId } from "./encounter-simulation";
import {
  attackUnit,
  endPlaytest,
  moveUnit,
  startPlaytest,
  waitUnit
} from "./playtest-session";

// The golden value asserted natively in games/demo/src/play_demo.cpp, reached
// by compiling games/demo/source/project.json to a package, loading its
// encounter, and applying this same command sequence.
//
// The browser reaches it from the source project directly, with no package
// compiler in between. Agreement therefore says something specific: the editor
// derives the same content identities, the same stats, and the same
// authoritative state as the native pipeline.
const nativeDemoCompletedHash = "673e5a59765c94c5";

describe("demo campaign native conformance", () => {
  it("derives the content identities the compiler assigns", () => {
    expect(stableContentId("demo_campaign/bridge_encounter/dawn_guard_leader"))
      .toBe(6538129454462652190n);
    expect(stableContentId("dawn_guard_unit")).toBe(2451794598990730096n);
  });

  it("reaches the native demo hash by playing the demo", () => {
    const project = createDemoProject();
    const started = startPlaytest(project);
    expect(started.error).toBeUndefined();
    const state = started.state!;

    // Three exchanges, and the first two commands are one turn of them: the
    // rider's two action points are what let the walk onto the bridge be
    // followed by the strike without the picket being handed the board in
    // between. Nobody waits: every command here is a blow or a step, and the
    // picket's single swing is answered by the free counter that leaves it on
    // one for the rider's last.
    expect(moveUnit(project, state, "dawn_guard_leader", 1, 1)).toBe(true);
    expect(attackUnit(project, state, "dawn_guard_leader", "river_watch_leader"))
      .toBe(true);
    expect(attackUnit(project, state, "river_watch_leader", "dawn_guard_leader"))
      .toBe(true);
    expect(attackUnit(project, state, "dawn_guard_leader", "river_watch_leader"))
      .toBe(true);

    expect(state.outcome).toBe("first_side_won");
    expect(state.activationCount).toBe(4);
    expect(
      state.encounter.canonicalHash().toString(16).padStart(16, "0")
    ).toBe(nativeDemoCompletedHash);
    endPlaytest(state);
  });
});
