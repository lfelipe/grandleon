// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import tarnholtSource from "../../../games/tarnholt/source/project.json";
import { decodeSourceProject } from "./source-project-document";
import { createEncounter } from "./encounter-simulation";
import {
  endPlaytest,
  planEncounterNode,
  startPlaytest
} from "./playtest-session";

// Every board the shipped campaign fights, reached by both pipelines.
//
// `demo-conformance.test.ts` beside this does the same for the demo, and the
// demo is a six-by-four bridge with one character a side and none of the knobs
// the engine has grown since. So the conformance this repository had covered
// the simplest board it ships and nothing else.
//
// The numbers are the native pipeline's. `games/tarnholt/src/play_tarnholt.cpp`
// compiles `project.json` to a package, loads each encounter out of it and
// asserts these same hashes on the boards it gets. The browser reaches them from
// the source project directly with no compiler in between, so agreement says
// both roads derive the same identities, stats, placements and terrain.
//
// It is the *opening* arrangement in every case, before a single command. The
// two golden hashes that file already had are taken where it leaves its boards,
// so they pin a scripted sequence as much as an arrangement; these pin only the
// arrangement, which is what makes them the right thing to hold a second
// pipeline to.
//
// **What each board is here to carry.** The first two are deliberately plain.
// The rest are why this test is not one board: a package can be correct on an
// arrangement of four characters and wrong the moment somebody arrives on round
// three, and until these existed nothing compared the two roads on any of it.
//
// It cannot say anything about the weapon triangle, and that is deliberate
// rather than a gap. `tests/simulation/canonical_hash_test.cpp` states the
// reason and pins it: which kinds beat which is content a battle names rather
// than state it holds, so a package can gain a triangle while an arrangement
// stays exactly as it is. Two boards differing only in their table are one
// arrangement. That is how a browser which had dropped the table went
// unnoticed, so the table is claimed where it can be -- over a board with a
// target in reach, in `weapon-playtest.test.ts`.
const nativeOpeningHashes: ReadonlyArray<readonly [string, string, string]> = [
  ["fordlight_battle", "24291ee6496e0494", "four a side and nothing else"],
  ["harrow_burn_battle", "a69229c47344caf9", "somebody who can be talked to"],
  ["sunken_mill_battle", "fdd6c129eeae3a75", "a board won by clearing it"],
  ["emberhall_battle", "413e244a57ceaa5c", "a deployment region and two waves"],
  ["ashen_watch_battle", "d8ead446b269d0bb", "two objectives, one of them a life"],
  ["coldgate_battle", "f27ba4cec59d5bc5", "a deployment region and the Marshal"]
];

function project() {
  return decodeSourceProject(
    new TextEncoder().encode(JSON.stringify(tarnholtSource))
  );
}

describe("tarnholt native conformance", () => {
  it("starts where the campaign starts", () => {
    // The campaign opens on a story node, so a playtest skips past it. Stated
    // separately from the boards below because it is a different claim: that
    // the browser walks the flow to the same place, not only that it builds a
    // board the same way once it is there.
    const started = startPlaytest(project());
    expect(started.error).toBeUndefined();
    const state = started.state!;
    expect(state.nodeId).toBe("fordlight_battle");
    expect(
      state.encounter.canonicalHash().toString(16).padStart(16, "0")
    ).toBe(nativeOpeningHashes[0]![1]);
    endPlaytest(state);
  });

  for (const [nodeId, hash, carries] of nativeOpeningHashes) {
    it(`builds ${nodeId} the compiler builds it, ${carries}`, () => {
      const decoded = project();
      const campaign = decoded.campaigns![0]!;
      const node = campaign.flow!.nodes.find(
        (candidate) => candidate.id === nodeId
      );
      expect(node).toBeDefined();

      // The same plan the playtest builds from, so this is the board the
      // browser would actually play rather than a second reading of the file.
      const plan = planEncounterNode(decoded, campaign.id, node!);
      expect(plan.error).toBeUndefined();
      const created = createEncounter(plan.definition!);
      expect(created.error).toBe("none");
      if (created.error !== "none") return;
      expect(
        created.encounter.canonicalHash().toString(16).padStart(16, "0")
      ).toBe(hash);
      created.encounter.dispose();
    });
  }
});
