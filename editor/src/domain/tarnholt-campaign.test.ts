// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import tarnholtSource from "../../../games/tarnholt/source/project.json";
import { decodeSourceProject } from "./source-project-document";
import {
  endPlaytest,
  legalMoves,
  legalTargets,
  moveUnit,
  pointsLeft,
  startPlaytest,
  takeAutomaticTurn,
  waitUnit
} from "./playtest-session";

// The Tarnholt Line is the project's worked campaign: six boards, a branch that
// only a conversation opens, a middle board won by outlasting rather than by
// winning, and a last one decided by one named character. These tests read the
// shipped source the browser reads and pin the shape of it, so that the parts
// already standing cannot regress while the rest is built.

function project() {
  return decodeSourceProject(new TextEncoder().encode(JSON.stringify(tarnholtSource)));
}

function campaign() {
  return project().campaigns![0]!;
}

function node(id: string) {
  return campaign().flow!.nodes.find((entry) => entry.id === id)!;
}

describe("The Tarnholt Line sample campaign", () => {
  it("is a decodable source project with six maps and two factions", () => {
    const decoded = project();
    expect(decoded.maps.map((map) => map.id)).toEqual([
      "fordlight_crossing",
      "ashen_watch",
      "harrow_burn",
      "sunken_mill",
      "emberhall_yard",
      "the_coldgate"
    ]);
    expect(decoded.maps[0]!.terrain).toHaveLength(80);
    expect(decoded.maps[1]!.terrain).toHaveLength(108);
    expect(decoded.maps[2]!.terrain).toHaveLength(88);
    expect(decoded.maps[3]!.terrain).toHaveLength(63);
    expect(decoded.maps[4]!.terrain).toHaveLength(108);
    expect(decoded.maps[5]!.terrain).toHaveLength(117);
    expect(decoded.factions?.map((faction) => faction.id)).toEqual([
      "dawn_guard",
      "ashen_coil"
    ]);
  });

  it("routes the branch on a talk and rejoins both threads at one node", () => {
    // The mark that makes the conversation possible, the edge that reads what
    // it raises, and the fallback that is the road for everybody who did not
    // have it. Three facts, and the branch is all three or none.
    const burn = node("harrow_burn_battle");
    const levy = burn.placements!.find((entry) => entry.id === "ashen_levy_coll")!;
    expect(levy.talk?.flagId).toBe("coll_rankin_heard");
    expect(levy.behavior).toBe("hold");

    const [branch, lost, fallback] = burn.transitions!;
    expect(branch!.when).toEqual({
      kind: "worldFlagEquals",
      flagId: "coll_rankin_heard",
      value: true
    });
    expect(branch!.targetNodeId).toBe("colls_word");
    expect(branch!.priority).toBeLessThan(fallback!.priority);
    expect(lost!.targetNodeId).toBe("valley_falls");
    expect(fallback!.when).toBeUndefined();
    expect(fallback!.targetNodeId).toBe("the_long_way");

    // The talked thread recruits the man who was on the other side of the
    // board, and takes a map nobody else sees.
    expect(node("colls_word").recruits!.map((entry) => entry.id)).toEqual([
      "dawn_levy_coll"
    ]);
    expect(node("colls_word").transitions![0]!.targetNodeId).toBe(
      "sunken_mill_battle"
    );
    expect(node("mill_burned").grants).toEqual([
      { itemId: "field_tonic", quantity: 3, notes: expect.any(String) }
    ]);

    // And the threads rejoin: two edges naming one node.
    expect(node("mill_burned").transitions![0]!.targetNodeId).toBe(
      "emberhall_road"
    );
    expect(node("the_long_way").transitions![0]!.targetNodeId).toBe(
      "emberhall_road"
    );
  });

  it("holds the yard for a count, under a round everybody acts in", () => {
    const decoded = project();
    const yard = node("emberhall_battle");
    expect(yard.objectiveIds).toEqual(["hold_the_yard"]);
    const objective = decoded.objectives!.find(
      (entry) => entry.id === "hold_the_yard"
    )!;
    expect(objective.kind).toBe("surviveRounds");
    expect(objective.rounds).toBe(6);
    // A round of alternating order is one activation each, which would make
    // six rounds shorter than the first wave. The game answers that once, for
    // every board it holds, and this one takes the answer rather than
    // repeating it: a board that states nothing moves when the game's setting
    // does.
    expect(decoded.defaultTurnOrder).toBe("sideBlocks");
    expect(yard.turnOrder).toBeUndefined();
    expect(
      campaign().flow!.nodes.every((entry) => entry.turnOrder === undefined)
    ).toBe(true);

    // Two waves, both coming at the player rather than standing where they
    // entered, and the yard holds fewer than the company that reaches it.
    const waves = yard.placements!.filter((entry) => entry.arrival);
    expect(waves).toHaveLength(2);
    expect(waves.every((entry) => entry.behavior === "pursue")).toBe(true);
    expect(waves.map((entry) => entry.arrival)).toEqual([
      { round: 2, every: 2, times: 3 },
      { round: 3, every: 2, times: 2 }
    ]);
    // The muster is the middle of the yard rather than a corner, because a
    // company backed into a wall is a company only one gate can reach.
    expect(yard.deployment!.tiles).toContainEqual({ x: 5, y: 4 });
    expect(yard.deployment!.capacity).toBe(5);
    expect(yard.deployment!.tiles).toHaveLength(7);
    expect(
      yard.placements!.filter((entry) => entry.side === "first")
    ).toHaveLength(7);
  });

  it("ends on one named character who cannot be talked down", () => {
    const decoded = project();
    const gate = node("coldgate_battle");
    expect(gate.objectiveIds).toEqual(["fell_the_marshal", "keep_mirea_alive"]);
    const fell = decoded.objectives!.find(
      (entry) => entry.id === "fell_the_marshal"
    )!;
    expect(fell.kind).toBe("defeatTarget");
    expect(fell.targetPlacementId).toBe("ashen_marshal_vorne");

    const marshal = gate.placements!.find(
      (entry) => entry.id === "ashen_marshal_vorne"
    )!;
    expect(marshal.unitTypeId).toBe("ashen_marshal");
    expect(marshal.behavior).toBe("hold");
    expect(marshal.talk).toBeUndefined();

    // Written as a class of his own rather than as a Warden with more health.
    const stats = decoded.classes.find((entry) => entry.id === "marshal")!
      .baseStats;
    expect(stats.health).toBe(24);
    expect(stats.defense).toBe(5);
    expect(stats.resistance).toBe(3);
    expect(stats.actionPoints).toBe(2);
    // The campaign's only resistance, which is what stops its magic from being
    // strictly better than its steel in the fight that decides it.
    expect(
      decoded.classes.filter((entry) => (entry.baseStats.resistance ?? 0) > 0)
    ).toHaveLength(1);

    // A finale is everybody: a region and no cap.
    expect(gate.deployment!.tiles).toHaveLength(7);
    expect(gate.deployment!.capacity).toBeUndefined();
  });

  it("stocks a store, grants along the road, and names two of the company", () => {
    const authored = campaign();
    expect(authored.startingStore).toEqual([
      { itemId: "field_tonic", quantity: 2, notes: expect.any(String) }
    ]);
    // Grants on two nodes, one of them the branch's reward.
    const granting = authored.flow!.nodes.filter((entry) => entry.grants);
    expect(granting.map((entry) => entry.id)).toEqual([
      "interlude",
      "mill_burned"
    ]);
    // Recruits at two points, one of them only on the branch.
    const recruiting = authored.flow!.nodes.filter((entry) => entry.recruits);
    expect(recruiting.map((entry) => entry.id)).toEqual([
      "marching_order",
      "colls_word"
    ]);

    // A worked example: an archer who shoots one tile further than
    // the bow she carries, because the bonus is a fact about her.
    const wren = authored.roster!.find(
      (entry) => entry.id === "dawn_archer_ford"
    )!;
    expect(wren.specificity).toEqual({
      stats: { skill: 2 },
      rangeBonus: 1
    });
    const coll = node("colls_word").recruits![0]!;
    expect(coll.specificity).toEqual({ stats: { health: 2, defense: -1 } });
  });

  it("keeps two of its boards plain, carrying none of the extra knobs", () => {
    // Four of the campaign's boards carry the expanded content and these two
    // carry none of it, which is what their golden canonical hashes are taken
    // over. The order they play in is the game's rather than their own, so it
    // reaches them like it reaches every other board here. Placements,
    // objectives and the absence of every extra knob, pinned here so that a
    // change to this file has to mean it.
    const ford = node("fordlight_battle");
    expect(ford.placements!.map((entry) => entry.id)).toEqual([
      "dawn_knight_north",
      "dawn_knight_south",
      "dawn_archer_ford",
      "dawn_mage_ford",
      "ashen_knight_north",
      "ashen_knight_south",
      "ashen_archer_ford",
      "ashen_storm_ford"
    ]);
    expect(ford.objectiveIds).toEqual(["defeat_all_opponents"]);
    const watch = node("ashen_watch_battle");
    expect(watch.objectiveIds).toEqual(["fell_the_warden", "keep_mirea_alive"]);
    for (const board of [ford, watch]) {
      expect(board.deployment).toBeUndefined();
      expect(board.turnOrder).toBeUndefined();
      expect(board.placements!.some((entry) => entry.talk)).toBe(false);
      expect(board.placements!.some((entry) => entry.arrival)).toBe(false);
    }
  });

  it("covers the melee, ranged, magic, area, support and command roles", () => {
    const decoded = project();
    expect(decoded.classes.map((entry) => entry.id).sort()).toEqual([
      "archer",
      "commander",
      "healer",
      "knight",
      "mage",
      "marshal",
      "stormcaller"
    ]);
    // The branch's recruit brings the campaign's only reach-one-to-two melee.
    const hook = decoded.weapons.find((entry) => entry.id === "boat_hook")!;
    expect(hook.minimumRange).toBe(1);
    expect(hook.maximumRange).toBe(2);
    // Reach is a band per weapon. The sword is the legacy single-range
    // spelling, which means one to one; the bow declares a true band and
    // cannot strike an adjacent enemy at all.
    const weapon = (id: string) =>
      decoded.weapons.find((candidate) => candidate.id === id)!;
    expect(weapon("guard_sword").range).toBe(1);
    expect(weapon("long_bow").minimumRange).toBe(2);
    expect(weapon("long_bow").maximumRange).toBe(3);
    expect(weapon("ember_staff").maximumRange).toBe(2);
  });

  it("starts its first encounter past the story node and is playable", () => {
    const decoded = project();
    const started = startPlaytest(decoded);
    expect(started.error).toBeUndefined();
    const state = started.state!;

    // The campaign entry node is a story node, so the playtest has to skip past
    // it to the first encounter.
    expect(state.nodeId).toBe("fordlight_battle");
    expect(state.width).toBe(10);
    expect(state.height).toBe(8);
    expect(state.units).toHaveLength(8);
    expect(state.units.filter((unit) => unit.side === "first")).toHaveLength(4);
    expect(state.units.filter((unit) => unit.side === "second")).toHaveLength(4);

    // A knight on the west bank can step onto the bridge road, and the guard's
    // block stays open behind him: this game runs one whole side at a time, so
    // one accepted activation hands nothing to the Coil.
    expect(legalMoves(state, "dawn_knight_north")).toContainEqual([1, 3]);
    expect(moveUnit(decoded, state, "dawn_knight_north", 1, 3)).toBe(true);
    expect(state.activationCount).toBe(1);
    expect(state.activeSide).toBe("first");
    // The engine names the side and never the actor, so the other three are
    // free to go in whatever order the player likes.
    expect(state.activeUnitId).toBe("");
    for (const id of ["dawn_knight_south", "dawn_archer_ford", "dawn_mage_ford"]) {
      expect(legalMoves(state, id).length).toBeGreaterThan(0);
    }
    endPlaytest(state);
  });

  it("lets the enemy side take its own turn", () => {
    const decoded = project();
    const state = startPlaytest(decoded).state!;
    // Hand the turn over, which under side blocks is the whole guard finishing
    // rather than one of them moving.
    expect(moveUnit(decoded, state, "dawn_knight_north", 1, 3)).toBe(true);
    expect(state.activeSide).toBe("first");
    for (const unit of state.units.filter((entry) => entry.side === "first")) {
      waitUnit(decoded, state, unit.id);
    }
    expect(state.activeSide).toBe("second");
    // Then let the Ashen Coil act for itself, one of its four at a time.
    expect(takeAutomaticTurn(decoded, state, "second")).toBe(true);
    expect(state.activeSide).toBe("second");
    // Six: the knight's walk, then a wait from each of the guard's four,
    // including the knight, who still holds the second of its two points after
    // walking and has to be told it is done, and then the Coil's first.
    expect(state.activationCount).toBe(6);
    while (takeAutomaticTurn(decoded, state, "second")) {
      /* the Coil's block runs to its end */
    }
    expect(state.activeSide).toBe("first");
    endPlaytest(state);
  });

  it("moves a pursuing enemy toward the player and leaves a holder in place", () => {
    const decoded = project();
    const state = startPlaytest(decoded).state!;
    const before = new Map(state.units.map((unit) => [unit.id, `${unit.x}:${unit.y}`]));
    // Twelve activations is enough for the pursuers to commit to the bridge.
    // The guard's actor is picked off `hasActed` rather than off position,
    // because a block names nobody and the first character in the list is
    // finished after its own walk.
    for (let turn = 0; turn < 12; turn += 1) {
      if (state.activeSide === "first") {
        const actor = state.units.find(
          (unit) => unit.side === "first" && unit.onBoard && !unit.hasActed
        );
        if (!actor) break;
        const moves = legalMoves(state, actor.id);
        if (moves.length === 0) {
          if (!waitUnit(decoded, state, actor.id)) break;
          continue;
        }
        moveUnit(decoded, state, actor.id, moves[0]![0], moves[0]![1]);
      } else if (!takeAutomaticTurn(decoded, state, "second")) {
        break;
      }
    }
    const pursuer = state.units.find((unit) => unit.id === "ashen_knight_north")!;
    const holder = state.units.find((unit) => unit.id === "ashen_archer_ford")!;
    expect(`${pursuer.x}:${pursuer.y}`).not.toBe(before.get("ashen_knight_north"));
    expect(`${holder.x}:${holder.y}`).toBe(before.get("ashen_archer_ford"));
    endPlaytest(state);
  });

  it("runs an initiative turn fastest-first across both sides", () => {
    const decoded = project();
    // Make the archer clearly the quickest, and order this battle by speed.
    decoded.classes.find((entry) => entry.id === "archer")!.baseStats.speed = 9;
    decoded.classes.find((entry) => entry.id === "knight")!.baseStats.speed = 2;
    decoded.classes.find((entry) => entry.id === "mage")!.baseStats.speed = 4;
    decoded.classes.find((entry) => entry.id === "stormcaller")!.baseStats.speed = 3;
    const node = decoded.campaigns![0]!.flow!.nodes.find(
      (entry) => entry.id === "fordlight_battle"
    )!;
    node.turnOrder = "initiative";

    const state = startPlaytest(decoded).state!;
    // Both sides field an archer at speed nine; the lower identifier opens.
    expect(state.activeUnitId).not.toBe("");
    const opener = state.units.find((unit) => unit.id === state.activeUnitId)!;
    expect(opener.speed).toBe(9);

    // Nobody else may act while the order has named someone.
    const other = state.units.find(
      (unit) => unit.side === opener.side && unit.id !== opener.id
    )!;
    expect(legalMoves(state, other.id)).toEqual([]);

    // Turn passes to the next fastest, which may belong to either side.
    expect(waitUnit(decoded, state, opener.id)).toBe(true);
    const next = state.units.find((unit) => unit.id === state.activeUnitId)!;
    expect(next.id).not.toBe(opener.id);
    expect(next.speed).toBeLessThanOrEqual(opener.speed);
    endPlaytest(state);
  });

  it("orders a battle that states nothing by the game's own default", () => {
    // The same battle as above, ordered by speed without touching the board:
    // the project states the default and the node states nothing, which is the
    // whole point of a game-wide setting. Tarnholt itself states no default, so
    // this is a change made here and never in the shipped content.
    const decoded = project();
    decoded.classes.find((entry) => entry.id === "archer")!.baseStats.speed = 9;
    decoded.classes.find((entry) => entry.id === "knight")!.baseStats.speed = 2;
    decoded.classes.find((entry) => entry.id === "mage")!.baseStats.speed = 4;
    decoded.classes.find((entry) => entry.id === "stormcaller")!.baseStats.speed = 3;
    const node = decoded.campaigns![0]!.flow!.nodes.find(
      (entry) => entry.id === "fordlight_battle"
    )!;
    expect(node.turnOrder).toBeUndefined();
    decoded.defaultTurnOrder = "initiative";

    const state = startPlaytest(decoded).state!;
    const opener = state.units.find((unit) => unit.id === state.activeUnitId)!;
    expect(opener.speed).toBe(9);
    const other = state.units.find(
      (unit) => unit.side === opener.side && unit.id !== opener.id
    )!;
    expect(legalMoves(state, other.id)).toEqual([]);
    endPlaytest(state);
  });

  it("lets a battle that states its own order ignore the game's default", () => {
    const decoded = project();
    decoded.classes.find((entry) => entry.id === "archer")!.baseStats.speed = 9;
    decoded.classes.find((entry) => entry.id === "knight")!.baseStats.speed = 2;
    const node = decoded.campaigns![0]!.flow!.nodes.find(
      (entry) => entry.id === "fordlight_battle"
    )!;
    decoded.defaultTurnOrder = "initiative";
    node.turnOrder = "alternating";

    // Alternating is both the fallback and a real choice: a board that states
    // it keeps it, so the player still picks who acts rather than being handed
    // the quickest archer.
    const state = startPlaytest(decoded).state!;
    const own = state.units.filter((unit) => unit.side === "first");
    expect(own.length).toBeGreaterThan(1);
    expect(own.every((unit) => legalMoves(state, unit.id).length > 0)).toBe(true);
    endPlaytest(state);
  });

  it("spends action points so a unit can move and then act, but not walk twice", () => {
    const decoded = project();
    // Give the knight two points and let it keep acting after striking.
    const knight = decoded.classes.find((entry) => entry.id === "knight")!;
    knight.baseStats.actionPoints = 2;
    knight.actsAfterAttacking = true;
    // Read under alternating order, where the budget being spent is the side's
    // and `activeUnitId` names who is spending it. The game ships in side
    // blocks, which answers the same question in each character's own
    // vocabulary; the case below this one reads it that way.
    decoded.campaigns![0]!.flow!.nodes.find(
      (entry) => entry.id === "fordlight_battle"
    )!.turnOrder = "alternating";
    const state = startPlaytest(decoded).state!;

    expect(moveUnit(decoded, state, "dawn_knight_north", 1, 3)).toBe(true);
    // Still the same side's activation, with a point left.
    expect(state.activeSide).toBe("first");
    expect(state.remainingActionPoints).toBe(1);
    expect(state.activeUnitId).toBe("dawn_knight_north");
    // Nobody else may interrupt an activation in progress.
    expect(legalMoves(state, "dawn_knight_south")).toEqual([]);
    expect(moveUnit(decoded, state, "dawn_knight_south", 1, 4)).toBe(false);
    // And the point that is left is not a second walk. One move per
    // activation, whatever the points say. The browser offers no range for it
    // and the engine refuses the command.
    expect(legalMoves(state, "dawn_knight_north")).toEqual([]);
    expect(moveUnit(decoded, state, "dawn_knight_north", 2, 3)).toBe(false);
    // Spending it on an action hands the turn over, which is the shape the
    // second point is for.
    expect(waitUnit(decoded, state, "dawn_knight_north")).toBe(true);
    expect(state.activeSide).toBe("second");
    endPlaytest(state);
  });

  it("spends the same two points in a side block, per character", () => {
    const decoded = project();
    const knight = decoded.classes.find((entry) => entry.id === "knight")!;
    knight.baseStats.actionPoints = 2;
    knight.actsAfterAttacking = true;
    // No board override: the shipped order, which is the one the campaign is
    // played in.
    const state = startPlaytest(decoded).state!;

    expect(moveUnit(decoded, state, "dawn_knight_north", 1, 3)).toBe(true);
    // A block holds no exclusive activation, so the engine names no actor and
    // the side-wide budget is not the thing being spent. What the knight has
    // left is a fact about the knight.
    expect(state.activeSide).toBe("first");
    expect(state.activeUnitId).toBe("");
    expect(state.remainingActionPoints).toBe(0);
    expect(pointsLeft(state, "dawn_knight_north")).toBe(1);
    // And the second knight is not locked out by the first having started.
    expect(pointsLeft(state, "dawn_knight_south")).toBe(2);
    expect(legalMoves(state, "dawn_knight_south").length).toBeGreaterThan(0);
    expect(moveUnit(decoded, state, "dawn_knight_south", 1, 4)).toBe(true);
    // The point that is left is still not a second walk, exactly as it is not
    // under alternating order.
    expect(legalMoves(state, "dawn_knight_north")).toEqual([]);
    expect(moveUnit(decoded, state, "dawn_knight_north", 2, 3)).toBe(false);
    // Spending it finishes the character rather than the side: the guard's
    // block stays open while anybody in it is owed a turn.
    expect(waitUnit(decoded, state, "dawn_knight_north")).toBe(true);
    expect(
      state.units.find((unit) => unit.id === "dawn_knight_north")!.hasActed
    ).toBe(true);
    expect(state.activeSide).toBe("first");
    // The block closes when the last of them is finished, and only then.
    for (const unit of state.units.filter((entry) => entry.side === "first")) {
      waitUnit(decoded, state, unit.id);
    }
    expect(state.activeSide).toBe("second");
    endPlaytest(state);
  });

  it("gives the archer reach and movement the knight does not have", () => {
    const decoded = project();
    const started = startPlaytest(decoded);
    const state = started.state!;
    const archer = state.units.find((unit) => unit.id === "dawn_archer_ford")!;
    const knight = state.units.find((unit) => unit.id === "dawn_knight_north")!;
    expect(archer.minimumReach).toBe(2);
    expect(archer.maximumReach).toBe(3);
    expect(knight.maximumReach).toBe(1);
    // The bow's minimum is real: an adjacent enemy is not a legal target.
    expect(legalTargets(state, "dawn_archer_ford")).toEqual([]);
    expect(archer.movement).toBe(4);
    expect(knight.movement).toBe(3);
    // Reach and movement are authoritative, so the archer can cross more
    // ground in one activation than the knight can.
    expect(legalMoves(state, "dawn_archer_ford").length)
      .toBeGreaterThan(legalMoves(state, "dawn_knight_north").length);
    endPlaytest(state);
  });
});
