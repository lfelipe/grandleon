// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import tarnholtSource from "../../../games/tarnholt/source/project.json";
import type { SourceProject } from "../generated/source-v1";
import { backdropByIndex } from "./board-art";
import { decodeSourceProject } from "./source-project-document";
import { createCampaign, stableContentId } from "./encounter-simulation";
import {
  attackUnit,
  continueCampaignPlaytest,
  endCampaignPlaytest,
  endPlaytest,
  startCampaignPlaytest,
  startPlaytest,
  takeAutomaticTurn,
  waitUnit,
  type CampaignPlaytest
} from "./playtest-session";

// A miniature campaign with everything the flow model has: a story intro, a
// battle that branches on an objective result, a victory scene, and two
// authored endings. Every flow decision below must come from the engine's
// cursor: winning and losing must reach different endings even though the
// victory transition has the lower priority.
function branchingProject(): SourceProject {
  return {
    schemaVersion: "1.1.0",
    packageId: "0e9cbb5a-3d54-4b6b-857d-52c05df6b6b6",
    gameId: "campaign.fixture",
    title: "Campaign fixture",
    contentRevision: "0.1.0",
    classes: [
      {
        id: "hero",
        name: "Hero",
        baseStats: { health: 12, movement: 3, strength: 6, defense: 1 }
      },
      {
        id: "villain",
        name: "Villain",
        baseStats: { health: 3, movement: 3, strength: 15, defense: 0 }
      }
    ],
    unitTypes: [
      { id: "hero_unit", name: "The Hero", classId: "hero" },
      { id: "villain_unit", name: "The Villain", classId: "villain" }
    ],
    weapons: [],
    items: [],
    maps: [{
      id: "yard",
      name: "Yard",
      width: 3,
      height: 1,
      terrain: ["grass", "grass", "grass"]
    }],
    objectives: [{
      id: "beat_them",
      name: "Beat them",
      kind: "defeatAllOpponents",
      side: "first"
    }],
    dialogues: [
      {
        id: "opening",
        name: "Opening",
        lines: [
          { speaker: "Narrator", text: "The yard is quiet." },
          { speaker: "Hero", text: "Not for long." }
        ]
      },
      {
        id: "cheer_lines",
        name: "Cheer",
        lines: [{ speaker: "Hero", text: "We did it!" }]
      },
      {
        id: "epilogue",
        name: "Epilogue",
        lines: [{ speaker: "Narrator", text: "Morning comes." }]
      }
    ],
    campaigns: [{
      id: "fixture",
      name: "Fixture",
      flow: {
        contractVersion: "1.0.0",
        entryNodeId: "intro",
        nodes: [
          {
            id: "intro",
            name: "Intro",
            kind: "story",
            dialogueIds: ["opening"],
            transitions: [{ id: "go", targetNodeId: "battle", priority: 0 }]
          },
          {
            id: "battle",
            name: "The Yard",
            kind: "encounter",
            mapId: "yard",
            objectiveIds: ["beat_them"],
            placements: [
              {
                id: "hero_one",
                unitTypeId: "hero_unit",
                side: "first",
                x: 0,
                y: 0
              },
              {
                id: "villain_one",
                unitTypeId: "villain_unit",
                side: "second",
                x: 1,
                y: 0,
                behavior: "pursue"
              }
            ],
            transitions: [
              {
                id: "won",
                targetNodeId: "cheer",
                priority: 0,
                when: {
                  kind: "objectiveResult",
                  objectiveId: "beat_them",
                  result: "victory"
                }
              },
              {
                id: "lost",
                targetNodeId: "nightfall",
                priority: 1,
                when: {
                  kind: "objectiveResult",
                  objectiveId: "beat_them",
                  result: "defeat"
                }
              }
            ]
          },
          {
            id: "cheer",
            name: "Cheering",
            kind: "story",
            dialogueIds: ["cheer_lines"],
            transitions: [{ id: "on", targetNodeId: "sunrise", priority: 0 }]
          },
          {
            id: "sunrise",
            name: "Sunrise",
            kind: "terminal",
            dialogueIds: ["epilogue"],
            transitions: []
          },
          {
            id: "nightfall",
            name: "Nightfall",
            kind: "terminal",
            transitions: []
          }
        ]
      }
    }]
  };
}

function requireSession(project: SourceProject): CampaignPlaytest {
  const started = startCampaignPlaytest(project);
  expect(started.error).toBeUndefined();
  return started.session!;
}

describe("campaign playtest over the engine cursor", () => {
  it("opens on the story scene, decoded by the engine's dialogue loader", () => {
    const project = branchingProject();
    const session = requireSession(project);

    expect(session.phase).toBe("scene");
    const dialogue = session.scene!.dialogues[session.scene!.index]!;
    expect(dialogue.name).toBe("Opening");
    // A scene that casts nobody: every line lands on no cast entry, which is
    // what every line of every scene authored before a cast existed carries.
    expect(dialogue.lines).toEqual([
      { speaker: "Narrator", text: "The yard is quiet.", castEntry: 0 },
      { speaker: "Hero", text: "Not for long.", castEntry: 0 }
    ]);
    expect(dialogue.cast).toEqual([]);

    continueCampaignPlaytest(project, session);
    expect(session.phase).toBe("battle");
    expect(session.battle!.nodeId).toBe("battle");
    endCampaignPlaytest(session);
  });

  // The same scene with a cast on it, so the two records are the same bytes
  // apart from the tail under test. A scene that casts somebody writes the
  // longer tail, a backdrop byte that may be zero, the cast, then one entry
  // per line, and this is the only path on which browser Play draws a face.
  it("encodes a scene's cast onto the wire, and the entry each line speaks",
    () => {
      const authored = branchingProject();
      authored.dialogues![0] = {
        id: "opening",
        name: "Opening",
        cast: [
          { speaker: "Hero", unitTypeId: "hero_unit" },
          { speaker: "The Villain", unitTypeId: "villain_unit" }
        ],
        lines: [
          { speaker: "Narrator", text: "The yard is quiet." },
          { speaker: "Hero", text: "Not for long." },
          { speaker: "The Villain", text: "Long enough." },
          { speaker: "Hero", text: "We shall see." }
        ]
      };
      const session = requireSession(authored);
      const dialogue = session.scene!.dialogues[session.scene!.index]!;

      expect(dialogue.cast).toEqual([
        stableContentId("hero_unit"),
        stableContentId("villain_unit")
      ]);
      // Position in the cast, plus one. The narrator nobody cast keeps zero,
      // which is what every line carried before a scene could name anybody.
      expect(dialogue.lines).toEqual([
        { speaker: "Narrator", text: "The yard is quiet.", castEntry: 0 },
        { speaker: "Hero", text: "Not for long.", castEntry: 1 },
        { speaker: "The Villain", text: "Long enough.", castEntry: 2 },
        { speaker: "Hero", text: "We shall see.", castEntry: 1 }
      ]);
      // A cast and no backdrop is legal, and only in this tail: the byte is
      // written unconditionally once there is a cast behind it.
      expect(dialogue.backdrop).toBe(0);
      endCampaignPlaytest(session);
    });

  it("casts a speaker once however many entries name them", () => {
    const authored = branchingProject();
    authored.dialogues![0] = {
      id: "opening",
      name: "Opening",
      backgroundId: "night_camp",
      cast: [
        { speaker: "Hero", unitTypeId: "hero_unit" },
        // A second answer to "who is the Hero". The reader keeps the first,
        // because two answers is not a question a client may settle.
        { speaker: "Hero", unitTypeId: "villain_unit" }
      ],
      lines: [{ speaker: "Hero", text: "Not for long." }]
    };
    const session = requireSession(authored);
    const dialogue = session.scene!.dialogues[session.scene!.index]!;

    expect(dialogue.cast).toEqual([stableContentId("hero_unit")]);
    expect(dialogue.lines[0]!.castEntry).toBe(1);
    expect(backdropByIndex(dialogue.backdrop)?.id).toBe("night_camp");
    endCampaignPlaytest(session);
  });

  it("wins through the victory branch to the authored Sunrise ending", () => {
    const project = branchingProject();
    const session = requireSession(project);
    continueCampaignPlaytest(project, session);
    const battle = session.battle!;

    // One blow fells the villain: 6 strength against 0 defense, 3 health.
    expect(attackUnit(project, battle, "hero_one", "villain_one")).toBe(true);
    expect(battle.outcome).toBe("first_side_won");

    // Continue walks the branch the engine chose: the cheering scene first.
    continueCampaignPlaytest(project, session);
    expect(session.phase).toBe("scene");
    expect(session.scene!.dialogues[0]!.name).toBe("Cheer");

    // Then the terminal node's own scene, then the authored ending name.
    continueCampaignPlaytest(project, session);
    expect(session.phase).toBe("scene");
    expect(session.scene!.dialogues[0]!.name).toBe("Epilogue");

    continueCampaignPlaytest(project, session);
    expect(session.phase).toBe("ended");
    expect(session.endingName).toBe("Sunrise");
    endCampaignPlaytest(session);
  });

  it("loses through the defeat branch to Nightfall, not the victory path", () => {
    const project = branchingProject();
    const session = requireSession(project);
    continueCampaignPlaytest(project, session);
    const battle = session.battle!;

    // The hero stands still; the villain hits for 14 against 12 health.
    expect(waitUnit(project, battle, "hero_one")).toBe(true);
    expect(takeAutomaticTurn(project, battle, "second")).toBe(true);
    expect(battle.outcome).toBe("second_side_won");

    continueCampaignPlaytest(project, session);
    expect(session.phase).toBe("ended");
    expect(session.endingName).toBe("Nightfall");
    endCampaignPlaytest(session);
  });

  it("gives the skirmish panel the same engine-decided transition", () => {
    const project = branchingProject();
    const started = startPlaytest(project);
    expect(started.error).toBeUndefined();
    const state = started.state!;
    // The cursor skipped the story intro to the first reachable battle.
    expect(state.nodeId).toBe("battle");

    expect(waitUnit(project, state, "hero_one")).toBe(true);
    expect(takeAutomaticTurn(project, state, "second")).toBe(true);
    expect(state.outcome).toBe("second_side_won");
    // The defeat branch names the ending. Every transition here carries a
    // condition, so a walk that ignored conditions would have nothing to say.
    expect(state.terminalNodeName).toBe("Nightfall");
    endPlaytest(state);
  });

  it("refuses a condition the runtime cannot evaluate, like the compiler", () => {
    const project = branchingProject();
    const battle = project.campaigns![0]!.flow!.nodes.find(
      (node) => node.id === "battle"
    )!;
    battle.transitions[0]!.when = {
      kind: "inventoryAtLeast",
      itemId: "rope",
      quantity: 1
    };
    const started = startCampaignPlaytest(project);
    expect(started.session).toBeUndefined();
    expect(started.error).toContain("cannot run yet");
  });

  it("reports the campaign loader's own error for a flow it rejects", () => {
    // Two unconditional transitions from one node would make the taken edge
    // depend on authoring order; load_campaign refuses it as unsupported_flow.
    const project = branchingProject();
    const intro = project.campaigns![0]!.flow!.nodes.find(
      (node) => node.id === "intro"
    )!;
    intro.transitions.push({
      id: "also",
      targetNodeId: "nightfall",
      priority: 2
    });
    const started = startCampaignPlaytest(project);
    expect(started.session).toBeUndefined();
    expect(started.error).toContain("unsupported_flow");
  });

  it("exposes load_campaign's reference validation through the binding", () => {
    const created = createCampaign({
      id: stableContentId("orphan"),
      name: "Orphan",
      entryNodeId: stableContentId("missing"),
      nodes: [{
        id: stableContentId("somewhere"),
        kind: "terminal"
      }]
    });
    expect(created.error).toBe("missing_reference");
  });

  // A scene the engine will not take is a scene that will never play: the node
  // reaches it, the loader finds no record under that identity, and the
  // campaign carries on with a cutscene missing and nothing said. The bound is
  // the shared buffer at 64 KiB, and the dialogue schema lets an author write
  // four thousand lines of four thousand characters, so this is reachable
  // from a long cutscene rather than only from a mistake.
  it("refuses a campaign whose scene the engine cannot be given", () => {
    const enormous = {
      id: stableContentId("epic"),
      name: "Epic",
      entryNodeId: stableContentId("only"),
      nodes: [{
        id: stableContentId("only"),
        kind: "terminal" as const,
        dialogueIds: [stableContentId("long_scene")]
      }],
      dialogues: [{
        id: stableContentId("long_scene"),
        name: "A long scene",
        lines: Array.from({ length: 20 }, (_unused, index) => ({
          speaker: "Narrator",
          text: `${index}`.padEnd(4000, ".")
        }))
      }]
    };
    expect(createCampaign(enormous).error).toBe("malformed_payload");

    // The same scene, short enough to attach, is taken, so what is refused
    // above is the size and not the shape.
    expect(
      createCampaign({
        ...enormous,
        dialogues: [{
          ...enormous.dialogues[0]!,
          lines: [{ speaker: "Narrator", text: "Morning comes." }]
        }]
      }).error
    ).toBe("none");
  });

  // The other way a record is refused: the same identity twice. The engine
  // will not let a second record shadow a first, and a campaign that quietly
  // played whichever one happened to be attached second would be a campaign
  // whose scenes depend on authoring order.
  it("refuses a campaign that names one scene twice", () => {
    const twice = {
      id: stableContentId("doubled"),
      name: "Doubled",
      entryNodeId: stableContentId("only"),
      nodes: [{
        id: stableContentId("only"),
        kind: "terminal" as const,
        dialogueIds: [stableContentId("scene")]
      }],
      dialogues: [
        {
          id: stableContentId("scene"),
          name: "First",
          lines: [{ speaker: "Narrator", text: "One." }]
        },
        {
          id: stableContentId("scene"),
          name: "Second",
          lines: [{ speaker: "Narrator", text: "Two." }]
        }
      ]
    };
    expect(createCampaign(twice).error).toBe("malformed_payload");
  });
});

describe("the Tarnholt campaign plays its authored story", () => {
  function project() {
    return decodeSourceProject(
      new TextEncoder().encode(JSON.stringify(tarnholtSource))
    );
  }

  it("walks prologue, valley, and muster scenes into the first battle", () => {
    const decoded = project();
    const session = requireSession(decoded);

    const sceneNames: string[] = [];
    for (let guard = 0; guard < 8 && session.phase === "scene"; guard += 1) {
      sceneNames.push(session.scene!.dialogues[session.scene!.index]!.name);
      continueCampaignPlaytest(decoded, session);
    }
    expect(sceneNames).toEqual([
      "Prologue",
      "The Valley",
      "Waking the Guard"
    ]);
    expect(session.phase).toBe("battle");
    expect(session.battle!.nodeId).toBe("fordlight_battle");
    expect(session.battle!.units).toHaveLength(9);
    endCampaignPlaytest(session);
  });

  // The backdrop survives the whole round trip that matters: the authored
  // name is resolved to a menu index, encoded into the dialogue record, and
  // decoded back by the engine's own loader. Nothing here reads the project's
  // `backgroundId`; what is asserted is what came back out of the bytes.
  it("carries each scene's backdrop out of the compiled record", () => {
    const decoded = project();
    const session = requireSession(decoded);

    const backdrops: (string | undefined)[] = [];
    for (let guard = 0; guard < 8 && session.phase === "scene"; guard += 1) {
      const scene = session.scene!.dialogues[session.scene!.index]!;
      backdrops.push(backdropByIndex(scene.backdrop)?.id);
      continueCampaignPlaytest(decoded, session);
    }
    expect(backdrops).toEqual(["throne_hall", "night_camp", "throne_hall"]);
    endCampaignPlaytest(session);
  });

  // The other half of the same round trip, and the one an author sees as a
  // face beside a line. The scene names its speakers by the strings its lines
  // spell; the record names them by position; the join is made once while the
  // source is read. Nothing below reads the project's `cast`; what is
  // asserted is what came back out of the bytes the engine decoded.
  it("carries each scene's cast out of the compiled record", () => {
    const decoded = project();
    const session = requireSession(decoded);
    const scene = session.scene!.dialogues[session.scene!.index]!;

    expect(scene.name).toBe("Prologue");
    expect(scene.cast).toEqual([
      stableContentId("dawn_levy"),
      stableContentId("dawn_commander")
    ]);
    // One entry per line, plus one, in the cast's authored order, so the
    // Runner speaks first and the Captain twice after her.
    expect(scene.lines.map((line) => line.castEntry)).toEqual([1, 2, 2]);
    // And the words are untouched by any of it: a cast says who a speaker is,
    // never what the line reads.
    expect(scene.lines[0]!.speaker).toBe("Runner");
    endCampaignPlaytest(session);
  });

  it("carries no backdrop for a scene that names none", () => {
    const source = JSON.parse(JSON.stringify(tarnholtSource)) as {
      dialogues: { id: string; backgroundId?: string }[];
    };
    for (const dialogue of source.dialogues) delete dialogue.backgroundId;
    const decoded = decodeSourceProject(
      new TextEncoder().encode(JSON.stringify(source))
    );
    const session = requireSession(decoded);
    const scene = session.scene!.dialogues[session.scene!.index]!;
    // Zero, not a menu entry: there is no default backdrop, and a record that
    // carries none is the record every campaign wrote before backdrops.
    expect(scene.backdrop).toBe(0);
    expect(backdropByIndex(scene.backdrop)).toBeUndefined();
    endCampaignPlaytest(session);
  });
});
