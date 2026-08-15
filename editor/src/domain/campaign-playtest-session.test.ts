// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import type { CampaignNode, SourceCampaign } from "../generated/source-v1";
import { createDemoProject } from "../sample-projects";
import { readEngineSlot } from "./encounter-simulation";
import {
  campaignBattleLosses,
  commitCampaignBattle,
  continueCampaignPlaySession,
  endCampaignPlaySession,
  manageCampaignCompany,
  proceedFromCampaignCompany,
  startCampaignPlaySession,
  type CampaignPlaySession
} from "./campaign-playtest-session";
import {
  attackUnit,
  beginBattle,
  canAct,
  canDeploy,
  deployUnit,
  deployableTiles,
  moveUnit,
  useItem,
  waitUnit,
  type PlaytestState
} from "./playtest-session";

// Play mode running the campaign the author wrote, against the content the
// native suite runs the same claim against.
//
// `tests/campaign_runtime/demo_permadeath_test.cpp` fights the demo's
// `muster_road` headlessly: it buries a rider, levels the survivor, commits a
// drop, saves, resumes, and refuses to field the dead one on the next map. This
// is the same content, the same command sequence, and the same numbers,
// reached through the WebAssembly campaign session instead, from a source project that
// was never compiled to a package.
//
// Every asserted number is one the engine handed over. Nothing here re-derives
// a level, an award, or an exclusion, which is the whole point of the session
// living in C++: if the browser and the terminal could come to different answers
// about what a battle did, an author could not trust either.

// The names the demo's author wrote into `muster_road`'s roster. Play calls a
// member what the campaign calls them; nothing here derives a name from a unit
// type any more.
const vanguard = "Vanguard Rilla";
const outrider = "Outrider Bevan";
const ferryman = "Torvald the Ferryman";

/**
 * Take the board with the company as it stands.
 *
 * The management stage stands before every board, so every route to a battle
 * goes through it: after a commit, after a story node, on a resume, and before
 * the first board of a fresh campaign. A test that wants the fight says so.
 */
function takeTheBoard(session: CampaignPlaySession): void {
  expect(session.phase).toBe("managing");
  proceedFromCampaignCompany(session);
}

function battleOf(session: CampaignPlaySession): PlaytestState {
  const battle = session.battle;
  if (!battle) throw new Error("The session is not standing on a battle.");
  return battle;
}

/**
 * The crossing, fought nearly as the native test fights it: the outrider trades
 * with the picket and then stands its ground while the picket's next two blows
 * finish it, and the vanguard rides onto the tile its companion fell from and
 * kills the picket in the same turn. The one difference from the native script
 * is deliberate and is spelled out at the line that makes it.
 */
function fightTheCrossing(session: CampaignPlaySession): void {
  takeTheBoard(session);
  const project = session.project;
  const state = battleOf(session);
  // The crossing opens on the western bank the content authors, so the battle
  // begins because somebody begins it. Nobody is moved: the line this fight is
  // pinned to is the line the author wrote.
  expect(state.deploying).toBe(true);
  expect(beginBattle(state)).toBe(true);
  expect(state.deploying).toBe(false);
  expect(attackUnit(project, state, "muster_outrider", "muster_picket")).toBe(true);
  expect(attackUnit(project, state, "muster_picket", "muster_outrider")).toBe(true);
  // The outrider keeps its draught and falls holding it. A burial returns
  // what is left of a kit by rule, so the store these tests manage is stocked
  // by a rule rather than by the picket's three-in-five drop, which is
  // whatever this battle's seeded stream says it is. The turn it would have
  // spent drinking spends standing.
  expect(waitUnit(project, state, "muster_outrider")).toBe(true);
  expect(attackUnit(project, state, "muster_picket", "muster_outrider")).toBe(true);
  // One turn, not two: the walk leaves the vanguard a second action point, so
  // riding onto the tile the outrider fell from and finishing the picket is a
  // single activation.
  expect(moveUnit(project, state, "muster_vanguard", 2, 1)).toBe(true);
  expect(attackUnit(project, state, "muster_vanguard", "muster_picket")).toBe(true);
}

function startMusterRoad(options: { slot: string; resume?: boolean }) {
  const started = startCampaignPlaySession(createDemoProject(), {
    campaignId: "muster_road",
    slot: options.slot,
    resume: options.resume ?? false
  });
  expect(started.error).toBeUndefined();
  return started.session!;
}

/**
 * The demo's road, with one thing the author could have written on it written.
 *
 * The three capabilities this exercises are authored fields the maintained
 * demo does not use: a stocked store, a node's grant and a capacity. It must
 * go on not using them: it is the conformance reference and its canonical
 * hashes are golden. So the edit is made here, on a fresh copy, against the same
 * boards and the same company every other test in this file plays.
 */
function musterRoadWith(
  edit: (campaign: SourceCampaign) => void,
  options: { slot: string }
) {
  const project = createDemoProject();
  const campaign = (project.campaigns ?? []).find(
    (candidate) => candidate.id === "muster_road"
  )!;
  edit(campaign);
  const started = startCampaignPlaySession(project, {
    campaignId: "muster_road",
    slot: options.slot
  });
  expect(started.error).toBeUndefined();
  return started.session!;
}

function nodeOf(campaign: SourceCampaign, id: string): CampaignNode {
  const node = campaign.flow?.nodes.find((candidate) => candidate.id === id);
  if (!node) throw new Error(`the road has no node '${id}'`);
  return node;
}

/** What the company owns, as one number per item, for a store assertion. */
function stockOf(session: CampaignPlaySession) {
  return (session.company?.store ?? []).map((stack) => ({
    itemName: stack.itemName,
    quantity: stack.quantity
  }));
}

describe("Play mode running a kept campaign", () => {
  it("opens on the region the content authors and arranges within it", () => {
    const session = startMusterRoad({ slot: "arranges" });
    try {
      takeTheBoard(session);
      const state = battleOf(session);
      // The phase is open, the region is the western bank the author wrote,
      // and nobody may be given an order until somebody opens the battle.
      expect(state.deploying).toBe(true);
      expect(state.deploymentTiles).toEqual([
        [0, 0], [0, 1], [1, 1], [2, 1], [0, 2], [1, 2]
      ]);
      expect(canAct(state, "muster_vanguard")).toBe(false);
      expect(canDeploy(state, "muster_vanguard")).toBe(true);
      expect(canDeploy(state, "muster_picket")).toBe(false);

      // The tiles the engine offers are the tiles it accepts, and the one the
      // other rider is standing on is not among them.
      expect(deployableTiles(state, "muster_vanguard")).toEqual([
        [0, 0], [0, 1], [1, 1], [0, 2], [1, 2]
      ]);
      expect(deployUnit(state, "muster_vanguard", 3, 3)).toBe(false);
      expect(deployUnit(state, "muster_vanguard", 1, 2)).toBe(true);
      const moved = battleOf(session).units.find(
        (unit) => unit.id === "muster_vanguard"
      )!;
      expect([moved.x, moved.y]).toEqual([1, 2]);
      expect(state.activationCount).toBe(0);

      // And the phase ends because the player ended it, after which the
      // ordinary vocabulary applies again.
      expect(beginBattle(state)).toBe(true);
      expect(state.deploying).toBe(false);
      expect(deployableTiles(state, "muster_vanguard")).toEqual([]);
      expect(canAct(state, "muster_vanguard")).toBe(true);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("opens on the authored board with the whole roster on it", () => {
    const session = startMusterRoad({ slot: "opens" });
    try {
      takeTheBoard(session);
      expect(session.phase).toBe("battle");
      expect(session.resumed).toBe(false);
      expect(session.excluded).toEqual([]);
      expect(battleOf(session).units.map((unit) => unit.id)).toEqual([
        "muster_vanguard",
        "muster_outrider",
        "muster_picket"
      ]);
      // The company the campaign authored, founded from the package and named
      // by the author. The ferryman is authored onto the crossing rather than
      // onto the founding, so he is not on it yet.
      expect(session.roster.map((member) => member.name)).toEqual([
        vanguard,
        outrider
      ]);
      expect(
        session.roster.every(
          (member) =>
            member.availability === "available" &&
            member.level === 1 &&
            member.experience === 0
        )
      ).toBe(true);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  // A loss said while the battle is still being fought, rather than only on the
  // screen after it.
  //
  // The screen after the battle names the dead, and on its own it is the first
  // place a name and a loss appear together, which in a campaign can be many
  // activations after the token stopped being on the board. What is asserted
  // here is the earlier sentence: that as the blow lands, the character it
  // landed on has a name.
  it("names the character a battle takes, as it takes them", () => {
    const session = startMusterRoad({ slot: "losses" });
    try {
      takeTheBoard(session);
      const project = session.project;
      const state = battleOf(session);
      expect(beginBattle(state)).toBe(true);
      // Nobody has fallen, so there is nothing to say and nothing is said.
      expect(campaignBattleLosses(session)).toEqual([]);

      expect(useItem(project, state, "muster_vanguard", "field_tonic")).toBe(true);
      expect(attackUnit(project, state, "muster_picket", "muster_outrider")).toBe(
        true
      );
      expect(useItem(project, state, "muster_outrider", "field_tonic")).toBe(true);
      expect(attackUnit(project, state, "muster_picket", "muster_outrider")).toBe(
        true
      );
      // Still nothing to say: the draught bought the outrider a swing, and
      // three health is not none.
      expect(campaignBattleLosses(session)).toEqual([]);
      expect(waitUnit(project, state, "muster_outrider")).toBe(true);
      // The blow the outrider does not survive. The name is the author's own,
      // reached through the engine's board-to-member join, the same join the
      // aftermath's `fallen` is reached through, and not through the unit type
      // the board would otherwise have offered, which every rider on this side
      // shares.
      expect(attackUnit(project, state, "muster_picket", "muster_outrider")).toBe(
        true
      );
      expect(campaignBattleLosses(session)).toEqual([outrider]);
      // And the log says it in the same word the console and the terminal say
      // it in, and under the same name. A battle with no campaign behind it has
      // no name to use and calls a character by its unit type, which on this
      // board would have said "Dawn Guard died." of a rider two other riders
      // share a type with.
      expect(state.events).toContain(`${outrider} died.`);

      expect(moveUnit(project, state, "muster_vanguard", 2, 1)).toBe(true);
      expect(attackUnit(project, state, "muster_vanguard", "muster_picket")).toBe(
        true
      );
      expect(state.outcome).toBe("first_side_won");

      // The picket fell to the blow that ended the battle, and the board
      // records it, but nobody in the company was standing in it, so it is not
      // one of the company's losses. Which is exactly the list the aftermath
      // reports, and it agrees.
      expect(state.defeated).toHaveLength(2);
      expect(campaignBattleLosses(session)).toEqual([outrider]);
      commitCampaignBattle(session);
      expect(session.aftermath!.fallen).toEqual(campaignBattleLosses(session));
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("narrates what the engine derived when the crossing is won", () => {
    const session = startMusterRoad({ slot: "narrates" });
    try {
      fightTheCrossing(session);
      expect(battleOf(session).outcome).toBe("first_side_won");
      commitCampaignBattle(session);
      expect(session.phase).toBe("aftermath");
      const aftermath = session.aftermath!;

      // Who fell forever. Derived from the board through the binding, as a
      // unit at zero health that a roster member was standing in, and from
      // nothing this file knows.
      expect(aftermath.fallen).toEqual([outrider]);

      // The sixty experience the River Watch is authored to be worth, granted
      // to the rider who felled it. The rider the battle buried earns nothing,
      // so there is one award and not two.
      expect(aftermath.experience).toEqual([{ name: vanguard, amount: 60 }]);

      // Fifty a level makes that exactly one level, and the growth stream
      // granted a point of health, a point of strength and a point of defence.
      //
      // Those points are a golden in the same sense a canonical hash is: not
      // numbers anybody chose, and not numbers that may move without somebody
      // meaning it. They are re-derived here rather than copied from
      // `tests/campaign_runtime/demo_permadeath_test.cpp`, and the distinction
      // matters even though the two currently agree: the seed is the
      // encounter's whole reference, this battle's canonical hash, and *which
      // completion within the campaign this is*. That headless slice commits
      // at sequence zero because it never entered a graph, and a session that
      // did is somewhere else in its own history. Landing on the same three
      // points is the streams agreeing, not the streams being the same one, so
      // a change that moved only this side would be a real answer and not a
      // copying mistake.
      expect(aftermath.levelUps).toEqual([
        {
          name: vanguard,
          fromLevel: 1,
          toLevel: 2,
          points: [
            { stat: "health", points: 1 },
            { stat: "strength", points: 1 },
            { stat: "defense", points: 1 }
          ]
        }
      ]);

      // What the fighting itself moved between owners: nothing. Nobody drank,
      // and the picket kept its tonic, being authored to leave one three times
      // in five with the draw off this battle's seeded drop stream not coming
      // up. Pinned as an empty list rather than left unasserted, because an
      // operation appearing here is exactly as much a change as one going
      // missing.
      expect(aftermath.store).toEqual([]);

      // And so the stores are up by the one thing the crossing did leave the
      // company: the draught the outrider was still carrying, which the burial
      // returned. A battle can give the company something without any battle
      // operation saying so.
      expect(aftermath.supplies).toEqual([
        { itemName: "Field Tonic", quantity: 1 }
      ]);

      // The surviving rider never drank, so their kit still holds the draught
      // the founding put in it, read off the campaign and not off the unit type.
      const carrying = session.roster.find(
        (member) => member.name === vanguard
      )!;
      expect(
        carrying.carrying.map((stack) => ({
          itemName: stack.itemName,
          quantity: stack.quantity
        }))
      ).toEqual([{ itemName: "Field Tonic", quantity: 1 }]);
      const emptied = session.roster.find(
        (member) => member.name === outrider
      )!;
      expect(emptied.carrying).toEqual([]);

      // And who the crossing brought in, read off the same committed batch the
      // level-ups were read off.
      expect(aftermath.joined).toEqual([ferryman]);
      const recruit = session.roster.find((member) => member.name === ferryman)!;
      expect(recruit.availability).toBe("available");
      expect(recruit.level).toBe(1);

      // Where the campaign went, and that it reached its slot.
      expect(aftermath.nextNodeName).toBe("The Watch on the Road");
      expect(aftermath.blockedReason).toBeUndefined();
      expect(aftermath.saved).toBe(true);
      expect(aftermath.saveError).toBeUndefined();
      expect(aftermath.canonicalHash).not.toBe(0n);

      // And the roster afterwards says the same thing the narration did,
      // without anything here adding a level up.
      const grown = session.roster.find((member) => member.name === vanguard)!;
      expect(grown.level).toBe(2);
      expect(grown.experience).toBe(60);
      expect(grown.gained.health).toBe(1);
      expect(grown.gained.strength).toBe(1);
      expect(grown.gained.defense).toBe(1);
      const lost = session.roster.find((member) => member.name === outrider)!;
      expect(lost.availability).toBe("dead");
      expect(lost.level).toBe(1);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("moves a thing between the store and a character's hands", () => {
    const session = startMusterRoad({ slot: "moves" });
    try {
      fightTheCrossing(session);
      commitCampaignBattle(session);
      continueCampaignPlaySession(session);

      // The road's management stage. The company owns the draught the picket
      // left on the field, and it is in nobody's hands: this is the tonic that
      // is worthless until somebody can be handed it.
      expect(session.phase).toBe("managing");
      const opened = session.company!;
      expect(opened.nodeName).toBe("The Watch on the Road");
      expect(
        opened.store.map((stack) => ({
          itemName: stack.itemName,
          quantity: stack.quantity
        }))
      ).toEqual([{ itemName: "Field Tonic", quantity: 1 }]);

      const tonic = opened.store[0]!.itemId;
      const rider = opened.members.find((member) => member.name === vanguard)!;
      expect(rider.carrying[0]!.quantity).toBe(1);
      manageCampaignCompany(session, "give", rider.id, tonic);
      const armed = session.company!;
      expect(armed.refusal).toBeUndefined();
      expect(armed.store).toEqual([]);
      expect(
        armed.members.find((member) => member.name === vanguard)!.carrying[0]!
          .quantity
      ).toBe(2);

      // And back again, which is a second batch rather than the first one
      // undone: the campaign committed both.
      manageCampaignCompany(session, "take", rider.id, tonic);
      expect(session.company!.store[0]!.quantity).toBe(1);
      expect(
        session.company!.members.find((member) => member.name === vanguard)!
          .carrying[0]!.quantity
      ).toBe(1);

      // The campaign refuses what it should refuse, and says so in its own
      // word. The outrider is the rider the crossing buried.
      const buried = opened.members.find((member) => member.name === outrider)!;
      expect(buried.present).toBe(false);
      manageCampaignCompany(session, "give", buried.id, tonic);
      expect(session.company!.refusal).toBe("unit_is_dead");
      expect(session.company!.store[0]!.quantity).toBe(1);

      // And a store that cannot pay is refused by the campaign too, not by a
      // screen deciding what to offer.
      manageCampaignCompany(session, "give", rider.id, tonic);
      manageCampaignCompany(session, "give", rider.id, tonic);
      expect(session.company!.refusal).toBe("insufficient_items");

      // The rider takes the road carrying what the player put in their hand.
      proceedFromCampaignCompany(session);
      expect(session.phase).toBe("battle");
      const carrier = battleOf(session).units.find(
        (unit) => unit.id === "muster_vanguard"
      )!;
      expect(
        carrier.items.map((item) => ({ id: item.id, count: item.count }))
      ).toEqual([{ id: "field_tonic", count: 2 }]);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("founds the company with the stock the campaign was written to own", () => {
    const session = musterRoadWith(
      (campaign) => {
        campaign.startingStore = [{ itemId: "field_tonic", quantity: 3 }];
      },
      { slot: "founding-stock" }
    );
    try {
      // Before the first board, before anything has been fought or dropped:
      // the store holds what the author said it holds. It is in nobody's
      // hands, which is the point of the store: it needs no living owner.
      expect(session.phase).toBe("managing");
      expect(stockOf(session))
        .toEqual([{ itemName: "Field Tonic", quantity: 3 }]);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("puts a node's grant in the store as the node completes", () => {
    const session = musterRoadWith(
      (campaign) => {
        nodeOf(campaign, "river_skirmish").grants = [
          { itemId: "field_tonic", quantity: 2 }
        ];
      },
      { slot: "node-grant" }
    );
    try {
      // Nothing yet: this campaign stocks nothing at its founding, and the
      // grant is the node's, not the campaign's.
      expect(stockOf(session)).toEqual([]);

      fightTheCrossing(session);
      commitCampaignBattle(session);
      continueCampaignPlaySession(session);
      expect(session.phase).toBe("managing");
      // Two granted by passing the crossing, and the one the picket dropped
      // on it. One store, one stack, in the same batch the rest of the node's
      // consequences committed in.
      expect(stockOf(session))
        .toEqual([{ itemName: "Field Tonic", quantity: 3 }]);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("grants again when the road comes back past the same node", () => {
    const session = musterRoadWith(
      (campaign) => {
        const crossing = nodeOf(campaign, "river_skirmish");
        // A blessing on the road, given by an abbot the road passes twice. The
        // crossing's recruitment goes with the cycle: a member is defined
        // where they join, and joining twice is a different question from
        // being given something twice.
        delete crossing.recruits;
        crossing.transitions = [
          { id: "back_to_the_abbot", targetNodeId: "blessing", priority: 0 }
        ];
        campaign.flow!.entryNodeId = "blessing";
        campaign.flow!.nodes.push({
          id: "blessing",
          name: "The Abbot's Blessing",
          kind: "story",
          grants: [{ itemId: "field_tonic", quantity: 2 }],
          transitions: [
            { id: "to_the_ford", targetNodeId: "river_skirmish", priority: 0 }
          ]
        });
      },
      { slot: "cyclic-grant" }
    );
    try {
      // First pass.
      expect(session.phase).toBe("managing");
      expect(stockOf(session))
        .toEqual([{ itemName: "Field Tonic", quantity: 2 }]);

      fightTheCrossing(session);
      commitCampaignBattle(session);
      continueCampaignPlaySession(session);

      // Second pass, under a sequence that has moved: a grant is an event and
      // not a statement about how full the store should be, so a road that
      // loops past the abbot twice is blessed twice. Two more, plus the one
      // the picket dropped on the ford.
      expect(session.phase).toBe("managing");
      expect(session.company!.nodeName).toBe("The Skirmish at the Crossing");
      expect(stockOf(session))
        .toEqual([{ itemName: "Field Tonic", quantity: 5 }]);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("refuses a field that would take more than the board allows", () => {
    const session = musterRoadWith(
      (campaign) => {
        // The road watch, down a narrow way: two may stand on it, and the
        // company that reaches it is three.
        nodeOf(campaign, "road_watch").deployment = {
          id: "road_watch_line",
          capacity: 1
        };
      },
      { slot: "over-cap" }
    );
    try {
      fightTheCrossing(session);
      commitCampaignBattle(session);
      continueCampaignPlaySession(session);
      expect(session.phase).toBe("managing");

      // Both numbers are the engine's. Two of the three would go, the third
      // being the rider the crossing buried, and the watch allows one.
      const company = session.company!;
      expect(company.capacity).toBe(1);
      expect(company.fielded).toBe(2);

      // The engine's own gate first: taking the board over its cap publishes
      // nothing and says so in the roster's own word, so the player is still
      // standing here with a company they can change.
      proceedFromCampaignCompany(session);
      expect(session.phase).toBe("managing");
      expect(session.company!.refusal).toBe("over_deployment_capacity");

      // The player answers a cap by benching somebody. Nothing was benched for
      // them: a cap never chooses.
      const recruit = session.company!.members.find(
        (member) => member.name === ferryman
      )!;
      manageCampaignCompany(session, "bench", recruit.id);
      expect(session.company!.fielded).toBe(1);
      expect(session.company!.refusal).toBeUndefined();

      // And the gesture that would break the cap again is refused a gesture
      // earlier, by the same name, before anything is committed: the store and
      // the benching both stand exactly where they stood.
      manageCampaignCompany(session, "field", recruit.id);
      expect(session.company!.refusal).toBe("over_deployment_capacity");
      expect(session.company!.fielded).toBe(1);
      expect(
        session.company!.members.find((member) => member.name === ferryman)!
          .fielded
      ).toBe(false);

      // A company inside its cap takes the board, and the one left behind is
      // reported exactly as the buried rider is.
      proceedFromCampaignCompany(session);
      expect(session.phase).toBe("battle");
      expect(session.excluded.map((member) => member.name).sort()).toEqual(
        [ferryman, outrider].sort()
      );
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("says nothing about a count when the board counts nothing", () => {
    const session = startMusterRoad({ slot: "uncapped" });
    try {
      // Every board is uncapped by default, and an uncapped board publishes a
      // zero rather than a number a screen would have to explain.
      expect(session.phase).toBe("managing");
      expect(session.company!.capacity).toBe(0);
      expect(session.company!.fielded).toBe(2);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("leaves behind whoever the player does not send", () => {
    const session = startMusterRoad({ slot: "benches" });
    try {
      fightTheCrossing(session);
      commitCampaignBattle(session);
      continueCampaignPlaySession(session);

      expect(session.phase).toBe("managing");
      const company = session.company!;
      // The road places all three members, so all three are a choice, even
      // the one the crossing buried, whom the campaign refuses on its own.
      expect(company.members.map((member) => member.placeable)).toEqual([
        true,
        true,
        true
      ]);
      const recruit = company.members.find(
        (member) => member.name === ferryman
      )!;
      expect(recruit.fielded).toBe(true);
      manageCampaignCompany(session, "bench", recruit.id);
      expect(
        session.company!.members.find((member) => member.name === ferryman)!
          .fielded
      ).toBe(false);

      // A company with nobody left to send is refused by the roster's own name
      // and stands where it stood, with nothing committed.
      const survivor = session.company!.members.find(
        (member) => member.name === vanguard
      )!;
      manageCampaignCompany(session, "bench", survivor.id);
      proceedFromCampaignCompany(session);
      expect(session.phase).toBe("managing");
      expect(session.company!.refusal).toBe("side_emptied");

      manageCampaignCompany(session, "field", survivor.id);
      proceedFromCampaignCompany(session);
      expect(session.phase).toBe("battle");
      // Two off the board: the rider the crossing buried, and the ferryman the
      // player left behind. The roster reports both the same way.
      expect(session.excluded.map((member) => member.name).sort()).toEqual(
        [ferryman, outrider].sort()
      );
      expect(battleOf(session).units.map((unit) => unit.id)).toEqual([
        "muster_vanguard",
        "muster_picket"
      ]);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("leaves the permanently dead off the next board, and says who", () => {
    const session = startMusterRoad({ slot: "excludes" });
    try {
      fightTheCrossing(session);
      commitCampaignBattle(session);
      continueCampaignPlaySession(session);

      takeTheBoard(session);
      expect(session.phase).toBe("battle");
      const road = battleOf(session);
      expect(road.nodeId).toBe("road_watch");
      // The authored road lists four. The roster fields three: the survivor,
      // the recruit the crossing brought in, and the picket.
      expect(road.units.map((unit) => unit.id)).toEqual([
        "muster_vanguard",
        "muster_ferryman_post",
        "muster_picket"
      ]);
      expect(session.excluded.map((member) => member.name)).toEqual([outrider]);
      expect(session.excluded[0]!.availability).toBe("dead");
      // The newest line first, as the log always reads.
      expect(road.events[0]).toContain(`${outrider} cannot take the field`);

      // And the survivor stands on the second map carrying what the first one
      // earned them: the author wrote seven health, four strength and one
      // defence, and the rider who crossed the river has a point more of each.
      // The board a package alone would produce is untouched. Growth is the
      // campaign's, added on the way to the board.
      const survivor = road.units.find((unit) => unit.id === "muster_vanguard")!;
      expect(survivor.health).toBe(8);
      expect(survivor.maximumHealth).toBe(8);
      expect(survivor.strength).toBe(5);
      expect(survivor.defense).toBe(2);

      // And carrying what the campaign holds for them rather than what their
      // unit type lists. This rider never drank, so the draught the founding
      // put in their hands is still there and the screen offers it; the
      // ferryman, who joined at the crossing, arrived with his own.
      expect(
        survivor.items.map((item) => [item.name, item.count])
      ).toEqual([["Field Tonic", 1]]);
      const recruited = road.units.find(
        (unit) => unit.id === "muster_ferryman_post"
      )!;
      expect(recruited.items.map((item) => [item.name, item.count])).toEqual([
        ["Field Tonic", 1]
      ]);

      // And the ferryman, who was nobody when this campaign was founded, is
      // standing on the road as the character the author wrote: the authored
      // Dawn Guard, with nothing added, because he has fought nothing yet.
      const joined = road.units.find(
        (unit) => unit.id === "muster_ferryman_post"
      )!;
      expect(joined.health).toBe(7);
      expect(joined.strength).toBe(4);
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("resumes the campaign a previous session left in the slot", () => {
    const first = startMusterRoad({ slot: "resumes" });
    try {
      fightTheCrossing(first);
      commitCampaignBattle(first);
      expect(first.aftermath!.saved).toBe(true);
    } finally {
      endCampaignPlaySession(first);
    }

    const second = startMusterRoad({ slot: "resumes", resume: true });
    try {
      expect(second.resumed).toBe(true);
      // A resumed campaign lands on the management stage, which is the whole of
      // "resuming lands where the save left off" once a stage stands there.
      takeTheBoard(second);
      // Standing where the graph left it, with the outrider exactly as dead as
      // they went in and the vanguard exactly as levelled.
      expect(battleOf(second).nodeId).toBe("road_watch");
      expect(second.excluded.map((member) => member.name)).toEqual([outrider]);
      const grown = second.roster.find((member) => member.name === vanguard)!;
      expect(grown.level).toBe(2);
      expect(grown.experience).toBe(60);
      // The recruit came out of the slot a member of the company, which is the
      // whole of what recruitment has to survive.
      const kept = second.roster.find((member) => member.name === ferryman)!;
      expect(kept.availability).toBe("available");
    } finally {
      endCampaignPlaySession(second);
    }
  });

  it("founds a fresh campaign when the slot holds nothing", () => {
    const session = startMusterRoad({ slot: "empty-slot", resume: true });
    try {
      // A slot that was never written is refused by the device, by name, and
      // the freshly founded campaign is what gets played.
      expect(session.resumed).toBe(false);
      takeTheBoard(session);
      expect(session.phase).toBe("battle");
      expect(battleOf(session).nodeId).toBe("river_skirmish");
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("says who a story node brought in, before the board they first stand on", () => {
    // A node that fights nothing has no aftermath screen to be reported on, so
    // its recruitment is said between the scenes instead. The demo recruits at
    // a battle, so the same recruitment is moved onto a story node standing
    // between the two maps, the shape `games/tarnholt` authors at
    // `marching_order`, reached here without fighting the ford first.
    const project = createDemoProject();
    const campaign = project.campaigns!.find(
      (candidate) => candidate.id === "muster_road"
    )!;
    const nodes = campaign.flow!.nodes;
    const crossing = nodes.find((node) => node.id === "river_skirmish")!;
    const bank = {
      id: "the_bank",
      name: "The Far Bank",
      kind: "story" as const,
      recruits: crossing.recruits!,
      transitions: [
        { id: "ride_on_again", targetNodeId: "road_watch", priority: 0 }
      ]
    };
    delete crossing.recruits;
    crossing.transitions[0]!.targetNodeId = "the_bank";
    nodes.splice(nodes.indexOf(crossing) + 1, 0, bank);

    const started = startCampaignPlaySession(project, {
      campaignId: "muster_road",
      slot: "story-recruit"
    });
    expect(started.error).toBeUndefined();
    const session = started.session!;
    try {
      // Nobody has joined on the way in: the founding company is the two the
      // campaign begins with, and the ferryman is nobody until the bank.
      expect(session.joined).toEqual([]);
      expect(session.roster.map((member) => member.name)).toEqual([
        vanguard,
        outrider
      ]);

      fightTheCrossing(session);
      commitCampaignBattle(session);
      // The battle recruited nobody now; the node that does is the next one.
      expect(session.aftermath!.joined).toEqual([]);
      expect(session.aftermath!.nextNodeName).toBe("The Far Bank");

      continueCampaignPlaySession(session);
      // Walking through the story node committed its batch, and the sentence
      // said between the scenes is read off that batch.
      expect(session.joined).toEqual([ferryman]);
      takeTheBoard(session);
      expect(session.phase).toBe("battle");
      const road = battleOf(session);
      expect(road.nodeId).toBe("road_watch");
      // And the man who was nobody a node ago stands on the board.
      expect(road.units.map((unit) => unit.id)).toContain("muster_ferryman_post");
      const recruit = session.roster.find((member) => member.name === ferryman)!;
      expect(recruit.availability).toBe("available");
    } finally {
      endCampaignPlaySession(session);
    }
  });

  it("plays a campaign with no roster to keep, untouched by the roster", () => {
    // The demo's conformance case: one rider a side, no permadeath to show, and
    // the campaign every other Play test walks. A campaign that keeps no roster
    // must be untouched by the roster machinery.
    const session = startCampaignPlaySession(createDemoProject(), {
      campaignId: "demo_campaign",
      slot: "conformance"
    }).session!;
    try {
      takeTheBoard(session);
      expect(session.phase).toBe("battle");
      expect(session.excluded).toEqual([]);
      const battle = battleOf(session);
      expect(battle.units.map((unit) => unit.id)).toEqual([
        "dawn_guard_leader",
        "river_watch_leader"
      ]);
      const project = session.project;
      // The reference stream, whose first two commands are one turn: the walk
      // does not hand the picket the board, because the rider has a second
      // action point to strike with.
      expect(moveUnit(project, battle, "dawn_guard_leader", 1, 1)).toBe(true);
      expect(
        attackUnit(project, battle, "dawn_guard_leader", "river_watch_leader")
      ).toBe(true);
      expect(
        attackUnit(project, battle, "river_watch_leader", "dawn_guard_leader")
      ).toBe(true);
      expect(
        attackUnit(project, battle, "dawn_guard_leader", "river_watch_leader")
      ).toBe(true);
      expect(battle.outcome).toBe("first_side_won");
      // The golden the demo, the browser, the terminal and the PlayStation all
      // pin. A campaign attached to a battle must not move it, and this is the
      // assertion that says so from inside the campaign session.
      expect(battle.encounter.canonicalHash().toString(16)).toBe(
        "673e5a59765c94c5"
      );
      commitCampaignBattle(session);
      expect(session.aftermath!.fallen).toEqual([]);
      expect(session.aftermath!.nextNodeName).toBe("The Road Opens");
      continueCampaignPlaySession(session);
      expect(session.phase).toBe("ended");
      expect(session.endingName).toBe("The Road Opens");
    } finally {
      endCampaignPlaySession(session);
    }
  });
});

// The commit reply is the largest thing this boundary sends: two full rosters,
// every level-up, and every operation the battle produced. It is also the one
// call that writes the campaign to its slot, and the order of those two is the
// whole of this test. Compose first and the campaign is where the screen thinks
// it is; save first and a company too large to report on is advanced, written
// down, and then answered with a boundary failure, and nothing that reads the
// slot afterwards has any way of knowing the screen was left behind.
describe("a company too large to report on", () => {
  it("is refused before the campaign is written to its slot", () => {
    const session = musterRoadWith(
      (campaign) => {
        const crossing = nodeOf(campaign, "river_skirmish");
        crossing.recruits = Array.from({ length: 600 }, (_unused, index) => ({
          id: `recruit_${index}`,
          name: "a",
          unitTypeId: "dawn_guard_unit"
        }));
      },
      { slot: "grand-recruitment" }
    );
    try {
      fightTheCrossing(session);
      // A campaign is written to its slot by the commit and by nothing before
      // it, so there is nothing there yet.
      expect(readEngineSlot(session.slot).error).toBe("not_found");

      let refusal = "";
      try {
        commitCampaignBattle(session);
      } catch (error) {
        refusal = String(error);
      }
      expect(refusal).toContain("could not commit");

      // And there is still nothing there. The battle was committed in memory,
      // the engine being the authority on that and unable to be made to
      // un-know it, but nothing durable moved, so the campaign that comes back
      // is the one the screen last agreed with.
      expect(readEngineSlot(session.slot).error).toBe("not_found");
    } finally {
      endCampaignPlaySession(session);
    }
  });
});

// The roster a node's recruits make is variable-length and goes out through the
// same 64 KiB buffer everything else does. A writer that reports what it could
// not fit costs one branch; one that does not hands back a payload declaring
// more members than it carries, and the reader on the other side then takes
// whatever is past the buffer as the next character's name.
describe("a story node with more recruits than the buffer carries", () => {
  it("is refused as a boundary failure rather than half-written", () => {
    const project = createDemoProject();
    const campaign = (project.campaigns ?? []).find(
      (candidate) => candidate.id === "muster_road"
    )!;
    const unitTypeId = project.unitTypes![0]!.id;
    // A roster entry costs more than the campaign record's own member does, so
    // a muster this size fits in the record that declares it and not in the
    // answer that reports it, which is exactly the shape the check is for.
    campaign.flow!.nodes.unshift({
      id: "grand_muster",
      name: "The Grand Muster",
      kind: "story",
      recruits: Array.from({ length: 1400 }, (_unused, index) => ({
        id: `recruit_${index}`,
        name: "a",
        unitTypeId
      })),
      transitions: [
        { id: "onward", targetNodeId: "river_skirmish", priority: 0 }
      ]
    });
    campaign.flow!.entryNodeId = "grand_muster";

    // The session settles onto its first battle by walking the story nodes in
    // front of it, so the muster is completed on the way in. It is named as a
    // boundary failure, not a `RangeError` from a reader that ran off the end
    // of its own view believing a count it was handed, and not a roster of
    // characters whose names came from whatever was next in memory.
    let refusal = "";
    try {
      startCampaignPlaySession(project, {
        campaignId: "muster_road",
        slot: "grand-muster"
      });
    } catch (error) {
      refusal = String(error);
    }
    expect(refusal).toContain("could not advance the campaign");
    expect(refusal).not.toContain("RangeError");
  });
});

// A campaign battle keeps every event it emits, because the commit derives what
// the battle meant from them. That log is the one structure on this side that
// grows with how long somebody plays rather than with how large their project
// is, and the module runs on a fixed heap with no exceptions, so left
// unbounded the allocation that cannot be served does not raise, it traps.
// After that the module goes on answering while the battle is unplayable, and
// nothing says so: every attempt to carry on re-traps on the same log.
describe("a battle nobody can finish", () => {
  it("is refused by name rather than trapping the module", () => {
    const project = createDemoProject();
    const started = startCampaignPlaySession(project, {
      campaignId: "muster_road",
      slot: "runaway"
    });
    expect(started.error).toBeUndefined();
    const session = started.session!;
    try {
      takeTheBoard(session);
      const state = battleOf(session);
      expect(beginBattle(state)).toBe(true);

      // Nobody attacks; everybody waits. The board cannot resolve, and the log
      // grows by two events an activation for as long as anybody keeps going.
      let applied = 0;
      let refusal: unknown;
      try {
        for (let guard = 0; guard < 200_000; guard += 1) {
          const actor = state.units.find((unit) => canAct(state, unit.id));
          if (!actor) break;
          if (!waitUnit(project, state, actor.id)) break;
          applied += 1;
        }
      } catch (error) {
        refusal = error;
      }

      // It stopped because the engine said so, not because the board resolved
      // and not because the loop ran out.
      expect(state.outcome).toBe("ongoing");
      expect(applied).toBeLessThan(200_000);
      expect(String(refusal)).toContain("more events than a campaign session");

      // And the module is not merely alive: this session is. The board still
      // answers, the hash still comes back, and the campaign behind it is
      // still standing on its battle. A trap leaves all three unreachable.
      expect(state.encounter.snapshot().outcome).toBe("ongoing");
      expect(typeof state.encounter.canonicalHash()).toBe("bigint");
      expect(session.phase).toBe("battle");
    } finally {
      endCampaignPlaySession(session);
    }
  });
});
