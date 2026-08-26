// SPDX-License-Identifier: MIT
import { beforeAll, describe, expect, it } from "vitest";
import {
  canonicalState,
  createEncounter,
  initEncounterEngine,
  isEncounterEngineReady,
  type Command,
  type EncounterDefinition
} from "./encounter-simulation";

// The native golden values in tests/simulation/encounter_test.cpp. The browser
// build is only correct if it reproduces these exactly; they are restated here
// rather than imported so that a change to either side has to be deliberate.
const nativeInitialHash = "0e41227fef2c075f";
const nativeCompletedHash = "9090072b2c0a69c5";
// The second vector, for the two things the first cannot reach: a battle that
// counts rounds and a battle a wave arrives in.
const nativeSurvivingInitialHash = "31f90d9772d39bed";
const nativeSurvivingCompletedHash = "03377a446b1b5ac3";

beforeAll(async () => {
  await initEncounterEngine();
});

function definition(): EncounterDefinition {
  return {
    width: 4,
    height: 3,
    units: [
      {
        id: 20n,
        unitTypeId: 200n,
        side: "second",
        position: { x: 2, y: 1 },
        health: 5,
        strength: 2,
        defense: 1
      },
      {
        id: 10n,
        unitTypeId: 100n,
        side: "first",
        position: { x: 0, y: 1 },
        health: 8,
        strength: 4,
        defense: 0
      }
    ]
  };
}

// The surviving vector from tests/simulation/encounter_test.cpp: a five by
// three board won by outlasting three rounds, with a wave that arrives as the
// second round begins and again as the fourth would have.
function survivingDefinition(): EncounterDefinition {
  return {
    width: 5,
    height: 3,
    units: [
      {
        id: 20n,
        unitTypeId: 200n,
        side: "second",
        position: { x: 3, y: 1 },
        health: 6,
        strength: 3,
        defense: 1
      },
      {
        id: 10n,
        unitTypeId: 100n,
        side: "first",
        position: { x: 0, y: 1 },
        health: 6,
        strength: 4,
        defense: 1
      },
      {
        id: 30n,
        unitTypeId: 200n,
        side: "second",
        position: { x: 4, y: 0 },
        health: 6,
        strength: 3,
        defense: 1,
        arrivalRound: 2,
        arrivalEvery: 2,
        arrivalTimes: 2
      }
    ],
    objectives: [
      { id: 1n, kind: "survive_rounds", side: "first", roundCount: 3 }
    ]
  };
}

function encounter() {
  const result = createEncounter(definition());
  expect(result.error).toBe("none");
  if (result.error !== "none") throw new Error(result.error);
  return result.encounter;
}

describe("WebAssembly engine loading", () => {
  it("reports readiness after initialization", () => {
    expect(isEncounterEngineReady()).toBe(true);
  });

  it("shares one instance across repeated initialization", async () => {
    await expect(initEncounterEngine()).resolves.toBeUndefined();
    expect(isEncounterEngineReady()).toBe(true);
  });
});

describe("simulation v0 encounter creation", () => {
  it.each([
    [{ ...definition(), width: 0 }, "invalid_map"],
    [{ ...definition(), width: 32_768 }, "invalid_map"],
    [
      {
        ...definition(),
        units: [{ ...definition().units[0]!, id: 0n }, definition().units[1]!]
      },
      "invalid_unit"
    ],
    [
      {
        ...definition(),
        units: [{ ...definition().units[0]!, health: 0 }, definition().units[1]!]
      },
      "invalid_unit"
    ],
    [
      {
        ...definition(),
        units: [{ ...definition().units[0]!, id: 10n }, definition().units[1]!]
      },
      "duplicate_unit"
    ],
    [
      {
        ...definition(),
        units: [
          { ...definition().units[0]!, position: { x: 0, y: 1 } },
          definition().units[1]!
        ]
      },
      "occupied_position"
    ],
    [
      {
        ...definition(),
        units: definition().units.map((unit) => ({ ...unit, side: "first" as const }))
      },
      "missing_side"
    ]
  ] as const)("rejects invalid definitions with %s", (input, expected) => {
    expect(createEncounter(input).error).toBe(expected);
  });

  it("rejects values that cannot cross the fixed-width boundary", () => {
    expect(createEncounter({ ...definition(), width: 4.5 }).error).toBe("invalid_map");
    expect(
      createEncounter({
        ...definition(),
        units: [
          { ...definition().units[0]!, health: 40_000 },
          definition().units[1]!
        ]
      }).error
    ).toBe("invalid_unit");
    expect(
      createEncounter({
        ...definition(),
        units: [
          { ...definition().units[0]!, id: 1n << 70n },
          definition().units[1]!
        ]
      }).error
    ).toBe("invalid_unit");
  });

  // A board past the shared buffer is refused, not thrown out of.
  //
  // This call arrives straight from a click with nothing catching it, and the
  // way an undercounted payload fails is a `RangeError` out of the `DataView`
  // rather than a return, so the size the check uses has to be the size the
  // writer actually emits, for every part of the record and not only the
  // units. The whole project's abilities, weapons, items and objectives ride
  // on every encounter, which is why three of the four below are not units at
  // all.
  it("refuses an oversized encounter instead of throwing", () => {
    // A board with room for everybody, so that what is under test is the size
    // of the payload and never where two of them are standing.
    const crowd = (count: number): EncounterDefinition => ({
      width: 64,
      height: 64,
      units: Array.from({ length: count }, (_unused, index) => ({
        ...definition().units[index % 2]!,
        id: BigInt(index + 1),
        side: (index === 0 ? "first" : "second") as "first" | "second",
        position: { x: index % 64, y: Math.floor(index / 64) }
      }))
    });
    // A unit record is 82 bytes and the buffer is 65,536, so the edge is at
    // 798 with the board's own tail counted. Walked from either side of it:
    // the ones that fit are created, the ones that do not are refused by name,
    // and not one of them throws.
    for (const count of [700, 798]) {
      const created = createEncounter(crowd(count));
      expect(created.error).toBe("none");
      if (created.error === "none") created.encounter.dispose();
    }
    for (const count of [799, 800, 820, 850, 900, 4000]) {
      expect(createEncounter(crowd(count)).error).toBe("invalid_unit");
    }
    // And the lists a board carries beside its units. These are the project's
    // own, whole, on every encounter it plays, so a board with two people on it
    // is refused for what its project holds rather than for its own size.
    const many = (count: number): EncounterDefinition => ({
      ...definition(),
      abilities: Array.from({ length: count }, (_unused, index) => ({
        id: BigInt(index + 1),
        kind: "damage" as const,
        damageType: "physical" as const,
        area: "single" as const,
        power: 1,
        minimumReach: 1,
        maximumReach: 1,
        radius: 0
      }))
    });
    expect(createEncounter(many(4000)).error).toBe("invalid_unit");
    expect(createEncounter(many(100_000)).error).toBe("invalid_unit");

    const objectives = (count: number): EncounterDefinition => ({
      ...definition(),
      objectives: Array.from({ length: count }, (_unused, index) => ({
        id: BigInt(index + 1),
        kind: "defeat_all_opponents" as const,
        side: "first" as const
      }))
    });
    expect(createEncounter(objectives(4000)).error).toBe("invalid_unit");
  });

  it("sorts units by unsigned 64-bit identifier and protects snapshots", () => {
    const simulation = encounter();
    const snapshot = simulation.snapshot();
    expect(snapshot.units.map((unit) => unit.id)).toEqual([10n, 20n]);
    (snapshot.units[0]!.position as { x: number }).x = 99;
    expect(simulation.snapshot().units[0]!.position.x).toBe(0);
  });

  it("keeps encounters independent of one another", () => {
    const first = encounter();
    const second = encounter();
    expect(first.apply({ type: "move", unitId: 10n, destination: { x: 1, y: 1 } }).error)
      .toBe("none");
    expect(first.canonicalHash()).not.toBe(second.canonicalHash());
    expect(second.snapshot().units[0]!.position).toEqual({ x: 0, y: 1 });
    first.dispose();
    second.dispose();
  });

  it("refuses to use a disposed encounter", () => {
    const simulation = encounter();
    simulation.dispose();
    simulation.dispose();
    expect(() => simulation.canonicalHash()).toThrow(/disposed/u);
  });
});

describe("simulation v0 commands", () => {
  it("runs the reference move/wait/attack loop and computes native hashes", () => {
    const simulation = encounter();
    expect(simulation.canonicalHash().toString(16).padStart(16, "0")).toBe(
      nativeInitialHash
    );

    // One action point, so the move also closes the activation.
    expect(simulation.apply({ type: "move", unitId: 10n, destination: { x: 1, y: 1 } })).toEqual({
      error: "none",
      events: [
        { type: "unit_moved", unitId: 10n, position: { x: 1, y: 1 } },
        { type: "activation_ended", unitId: 10n, position: { x: 1, y: 1 } }
      ]
    });
    expect(simulation.apply({ type: "wait", unitId: 20n }).error).toBe("none");

    const firstAttack = simulation.apply({ type: "attack", unitId: 10n, targetId: 20n });
    expect(firstAttack.error).toBe("none");
    // Two damage events, not one: the defender survives at two, stands inside
    // its own one-tile band, and answers inside the same command.
    expect(firstAttack.events.map((event) => event.type)).toEqual([
      "unit_damaged",
      "unit_damaged",
      "activation_ended"
    ]);
    expect(firstAttack.events[0]).toEqual({
      type: "unit_damaged",
      unitId: 20n,
      relatedUnitId: 10n,
      position: { x: 2, y: 1 },
      amount: 3
    });
    expect(firstAttack.events[1]).toEqual({
      type: "unit_damaged",
      unitId: 10n,
      relatedUnitId: 20n,
      position: { x: 1, y: 1 },
      amount: 2
    });
    expect(simulation.apply({ type: "wait", unitId: 20n }).error).toBe("none");
    const victory = simulation.apply({ type: "attack", unitId: 10n, targetId: 20n });
    expect(victory.events.map((event) => event.type)).toEqual([
      "unit_damaged",
      "unit_defeated",
      "encounter_completed"
    ]);
    expect(victory.events.at(-1)).toMatchObject({ outcome: "first_side_won" });
    expect(canonicalState(simulation.snapshot())).toEqual({
      width: 4,
      height: 3,
      activeSide: "first",
      activationCount: "5",
      outcome: "first_side_won",
      units: [
        {
          id: "10",
          unitTypeId: "100",
          side: "first",
          x: 1,
          y: 1,
          // Eight less the one counter it took: the second strike was lethal
          // and therefore went unanswered.
          health: 6,
          maximumHealth: 8,
          strength: 4,
          defense: 0
        },
        {
          id: "20",
          unitTypeId: "200",
          side: "second",
          x: 2,
          y: 1,
          health: 0,
          maximumHealth: 5,
          strength: 2,
          defense: 1
        }
      ]
    });
    expect(simulation.canonicalHash().toString(16).padStart(16, "0")).toBe(
      nativeCompletedHash
    );
  });

  it("counts rounds and lands waves exactly as the native build does", () => {
    const created = createEncounter(survivingDefinition());
    expect(created.error).toBe("none");
    if (created.error !== "none") throw new Error(created.error);
    const simulation = created.encounter;
    expect(simulation.canonicalHash().toString(16).padStart(16, "0")).toBe(
      nativeSurvivingInitialHash
    );
    // The wave is in the battle before it is on the board.
    const opening = simulation.snapshot();
    const waiting = opening.units.filter((unit) => !unit.arrived);
    expect(waiting).toHaveLength(2);
    // And the wire says so in its own byte rather than leaving the browser to
    // work it out. `onBoard` is `simulation::on_board`, sent because a client
    // that composed it from the three fields it folds is a client free to
    // disagree with the board about who is standing where. A character still
    // marching in is alive and undeparted, so a health test would put it on a
    // tile it does not hold.
    for (const unit of waiting) {
      expect(unit.health).toBeGreaterThan(0);
      expect(unit.departed).toBe(false);
      expect(unit.onBoard).toBe(false);
    }
    for (const unit of opening.units.filter((candidate) => candidate.arrived)) {
      expect(unit.onBoard).toBe(true);
    }

    // One turn for each side is one round under alternating order, and the
    // objective is satisfied the moment the third completes.
    let sawArrival = false;
    for (let round = 0; round < 3; round += 1) {
      for (const unitId of [10n, 20n]) {
        const result = simulation.apply({ type: "wait", unitId });
        expect(result.error).toBe("none");
        sawArrival =
          sawArrival ||
          result.events.some((event) => event.type === "unit_arrived");
      }
    }
    expect(sawArrival).toBe(true);
    expect(simulation.snapshot().round).toBe(3);
    expect(simulation.snapshot().outcome).toBe("first_side_won");
    expect(simulation.canonicalHash().toString(16).padStart(16, "0")).toBe(
      nativeSurvivingCompletedHash
    );
    simulation.dispose();
  });

  it("takes a talked-off character off the board with its health intact", () => {
    // The other half of the board predicate, and the half a health test cannot
    // see at all: a departed character keeps every point it had. The byte the
    // wire carries is the engine's own `simulation::on_board`, so the browser
    // is told rather than left to infer, which is what stops it drawing
    // somebody on a tile every command aimed at is refused.
    const created = createEncounter({
      width: 4,
      height: 3,
      units: [
        {
          id: 10n,
          unitTypeId: 100n,
          side: "first",
          position: { x: 0, y: 1 },
          health: 8,
          strength: 4,
          defense: 0
        },
        {
          id: 20n,
          unitTypeId: 200n,
          side: "second",
          position: { x: 1, y: 1 },
          health: 5,
          strength: 2,
          defense: 1,
          talkRecordId: 4242n
        },
        // A second opponent, so the board does not end the moment the first
        // one walks away: a battle whose last opponent departs is won by the
        // same elimination backstop that ends one whose last opponent falls,
        // and this test is about the tile rather than about the outcome.
        {
          id: 30n,
          unitTypeId: 200n,
          side: "second",
          position: { x: 3, y: 1 },
          health: 5,
          strength: 2,
          defense: 1
        }
      ]
    });
    expect(created.error).toBe("none");
    if (created.error !== "none") throw new Error(created.error);
    const simulation = created.encounter;

    const before = simulation.snapshot().units.find((unit) => unit.id === 20n)!;
    expect(before.onBoard).toBe(true);

    const result = simulation.apply({
      type: "talk",
      unitId: 10n,
      targetId: 20n
    });
    expect(result.error).toBe("none");

    const after = simulation.snapshot().units.find((unit) => unit.id === 20n)!;
    expect(after.departed).toBe(true);
    expect(after.arrived).toBe(true);
    expect(after.health).toBe(5);
    expect(after.onBoard).toBe(false);

    // And the engine refuses what a board drawing them would be offering. The
    // talk spent the first side's turn, so the second side takes one and hands
    // it back before the strike is aimed.
    expect(simulation.apply({ type: "wait", unitId: 30n }).error).toBe("none");
    const refused = simulation.apply({
      type: "attack",
      unitId: 10n,
      targetId: 20n
    });
    expect(refused.error).toBe("target_departed");
    simulation.dispose();
  });

  it("prices a cast the way the engine does, per character it covers", () => {
    // The gesture the browser could not price at all: it could light a splash
    // and say nothing about what standing in one cost. Three characters under
    // one blast, and each has to come back with its own answer.
    const board = createEncounter({
      width: 6,
      height: 3,
      abilities: [{
        id: 700n,
        kind: "damage",
        damageType: "magical",
        area: "diamond",
        radius: 1,
        power: 6,
        minimumReach: 1,
        maximumReach: 3,
        accuracy: 100
      }],
      units: [
        {
          id: 10n, unitTypeId: 100n, side: "first",
          position: { x: 0, y: 1 }, health: 12,
          strength: 2, defense: 0, magic: 3,
          abilityIds: [700n]
        },
        // An ally standing in the blast: covered and untouched.
        {
          id: 11n, unitTypeId: 100n, side: "first",
          position: { x: 3, y: 0 }, health: 12, strength: 2, defense: 0
        },
        // Two opponents who differ in what the cast has to price separately.
        {
          id: 20n, unitTypeId: 200n, side: "second",
          position: { x: 3, y: 1 }, health: 12,
          strength: 2, defense: 0, resistance: 4
        },
        {
          id: 21n, unitTypeId: 200n, side: "second",
          position: { x: 3, y: 2 }, health: 5, strength: 2, defense: 0
        }
      ]
    });
    expect(board.error).toBe("none");
    if (board.error !== "none") throw new Error(board.error);
    const simulation = board.encounter;
    const centre = { x: 3, y: 1 };

    // magic 3 + power 6 - resistance 4 is five.
    const onSturdy = simulation.forecastAbility(10n, 700n, centre, 20n);
    expect(onSturdy.error).toBe("none");
    expect(onSturdy.covered).toBe(true);
    expect(onSturdy.spared).toBe(false);
    expect(onSturdy.damage).toBe(5);
    expect(onSturdy.targetHealthAfter).toBe(7);
    expect(onSturdy.lethal).toBe(false);

    // Against nothing it is nine, which is past five health.
    const onFrail = simulation.forecastAbility(10n, 700n, centre, 21n);
    expect(onFrail.damage).toBe(9);
    expect(onFrail.targetHealthAfter).toBe(0);
    expect(onFrail.lethal).toBe(true);

    // Covered and spared, which the splash alone cannot show.
    const onAlly = simulation.forecastAbility(10n, 700n, centre, 11n);
    expect(onAlly.covered).toBe(true);
    expect(onAlly.spared).toBe(true);
    expect(onAlly.damage).toBe(0);
    expect(onAlly.targetHealthAfter).toBe(12);

    // The caster stands outside this one: uncovered, which is a different fact
    // from being spared, and reported as ending on the health it has.
    const onCaster = simulation.forecastAbility(10n, 700n, centre, 10n);
    expect(onCaster.covered).toBe(false);
    expect(onCaster.spared).toBe(false);
    expect(onCaster.targetHealthAfter).toBe(12);

    // Ground alone is a legal question and carries no character.
    const onGround = simulation.forecastAbility(10n, 700n, centre, 0n);
    expect(onGround.error).toBe("none");
    expect(onGround.covered).toBe(false);

    // And the refusals are the cast's own, in apply's own words.
    expect(simulation.forecastAbility(10n, 999n, centre, 20n).error)
      .toBe("unknown_ability");
    expect(simulation.forecastAbility(20n, 700n, centre, 10n).error)
      .toBe("wrong_side");
    expect(simulation.forecastAbility(10n, 700n, { x: 0, y: 1 }, 10n).error)
      .toBe("target_out_of_range");

    // The promise: committing the cast leaves every character where its
    // forecast said it would.
    const applied = simulation.apply({
      type: "ability", unitId: 10n, destination: centre, abilityId: 700n
    });
    expect(applied.error).toBe("none");
    const after = simulation.snapshot();
    const healthOf = (id: bigint) =>
      after.units.find((unit) => unit.id === id)?.health;
    expect(healthOf(20n)).toBe(onSturdy.targetHealthAfter);
    expect(healthOf(11n)).toBe(onAlly.targetHealthAfter);
    expect(healthOf(10n)).toBe(onCaster.targetHealthAfter);
  });

  it("carries a sub-certain chance across the boundary and rolls it", () => {
    // Accuracy crosses the ABI on the weapon, on the ability and on the unit,
    // and the browser rolls it with the same generator the native build does.
    // Two units adjacent, each able to answer the other, both at half.
    const chancy = createEncounter({
      width: 4,
      height: 3,
      units: [
        {
          id: 20n,
          unitTypeId: 200n,
          side: "second",
          position: { x: 1, y: 1 },
          health: 5,
          strength: 2,
          defense: 1,
          accuracy: 50
        },
        {
          id: 10n,
          unitTypeId: 100n,
          side: "first",
          position: { x: 0, y: 1 },
          health: 6,
          strength: 4,
          defense: 0,
          accuracy: 50
        }
      ]
    });
    expect(chancy.error).toBe("none");
    if (chancy.error !== "none") throw new Error(chancy.error);
    const simulation = chancy.encounter;

    const promised = simulation.forecastAttack(10n, 20n);
    // The chance is stated, and with neither unit carrying skill, luck or
    // evasion it folds to the weapon's own number, which is the escape hatch
    // holding across the boundary as well as inside it.
    expect(promised.hitChance).toBe(50);
    expect(promised.counterChance).toBe(50);
    expect(promised.damage).toBe(3);
    expect(promised.targetHealthAfter).toBe(2);

    // And it is rolled. This encounter seeds itself from its own opening
    // board, so the outcome is fixed: the strike lands for exactly the damage
    // forecast, and so does the answer.
    const struck = simulation.apply({
      type: "attack",
      unitId: 10n,
      targetId: 20n
    });
    expect(struck.error).toBe("none");
    expect(struck.events[0]).toMatchObject({
      type: "unit_damaged",
      unitId: 20n,
      amount: promised.damage
    });
    expect(struck.events[1]).toMatchObject({
      type: "unit_damaged",
      unitId: 10n,
      relatedUnitId: 20n,
      amount: promised.counterDamage
    });
    const units = simulation.snapshot().units;
    expect(units.find((unit) => unit.id === 20n)?.health).toBe(
      promised.targetHealthAfter
    );
    expect(units.find((unit) => unit.id === 10n)?.health).toBe(
      promised.attackerHealthAfter
    );
  });

  it("takes exactly nothing when the same strike misses", () => {
    // The same board with the attacker one point heavier, which is a different
    // opening state and therefore a different seed: here the strike misses and
    // the answer lands. Both outcomes are pinned rather than sampled, because
    // both are decided by state the encounter carries.
    const chancy = createEncounter({
      width: 4,
      height: 3,
      units: [
        {
          id: 20n,
          unitTypeId: 200n,
          side: "second",
          position: { x: 1, y: 1 },
          health: 5,
          strength: 2,
          defense: 1,
          accuracy: 50
        },
        {
          id: 10n,
          unitTypeId: 100n,
          side: "first",
          position: { x: 0, y: 1 },
          health: 7,
          strength: 4,
          defense: 0,
          accuracy: 50
        }
      ]
    });
    expect(chancy.error).toBe("none");
    if (chancy.error !== "none") throw new Error(chancy.error);
    const simulation = chancy.encounter;
    const promised = simulation.forecastAttack(10n, 20n);
    expect(promised.hitChance).toBe(50);

    const swung = simulation.apply({
      type: "attack",
      unitId: 10n,
      targetId: 20n
    });
    expect(swung.error).toBe("none");
    expect(swung.events[0]).toMatchObject({
      type: "attack_missed",
      unitId: 20n,
      relatedUnitId: 10n
    });
    const units = simulation.snapshot().units;
    // Nothing taken, and the answer still comes: a missed blow is a blow you
    // were in range of.
    expect(units.find((unit) => unit.id === 20n)?.health).toBe(5);
    expect(swung.events[1]).toMatchObject({
      type: "unit_damaged",
      unitId: 10n,
      relatedUnitId: 20n,
      amount: promised.counterDamage
    });
    expect(units.find((unit) => unit.id === 10n)?.health).toBe(
      promised.attackerHealthAfter
    );
  });

  it("forecasts an attack with exactly what apply then delivers", () => {
    const simulation = encounter();
    expect(simulation.forecastAttack(10n, 20n).error).toBe("target_out_of_range");
    expect(simulation.forecastAttack(20n, 10n).error).toBe("wrong_side");
    expect(simulation.forecastAttack(10n, 999n).error).toBe("unknown_target");

    expect(
      simulation.apply({ type: "move", unitId: 10n, destination: { x: 1, y: 1 } }).error
    ).toBe("none");
    expect(simulation.apply({ type: "wait", unitId: 20n }).error).toBe("none");

    const before = simulation.canonicalHash();
    const promised = simulation.forecastAttack(10n, 20n);
    // Both halves of the promise: the target drops to two and, surviving one
    // tile away inside its own reach band, takes two back off the attacker.
    expect(promised).toEqual({
      error: "none",
      hitChance: 100,
      counterChance: 100,
      damage: 3,
      targetHealthAfter: 2,
      lethal: false,
      counter: true,
      counterDamage: 2,
      attackerHealthAfter: 6,
      counterLethal: false,
      // This board authors no kinds of weapon, so neither hand holds an edge
      // and nothing leans. A surface reading this draws no arrow, which is the
      // answer for every board in a game that never wrote a triangle.
      lean: "none",
      counterLean: "none"
    });
    expect(simulation.canonicalHash()).toBe(before);

    const struck = simulation.apply({ type: "attack", unitId: 10n, targetId: 20n });
    expect(struck.events[0]).toMatchObject({ amount: promised.damage });
    const target = simulation.snapshot().units.find((unit) => unit.id === 20n);
    expect(target?.health).toBe(promised.targetHealthAfter);
    // The counter is delivered by the same command, exactly as priced.
    expect(struck.events[1]).toMatchObject({
      unitId: 10n,
      relatedUnitId: 20n,
      amount: promised.counterDamage
    });
    const attacker = simulation.snapshot().units.find((unit) => unit.id === 10n);
    expect(attacker?.health).toBe(promised.attackerHealthAfter);

    // The finishing blow is announced as such before it is committed, and
    // announced as unanswered, because a felled defender does not counter.
    expect(simulation.apply({ type: "wait", unitId: 20n }).error).toBe("none");
    expect(simulation.forecastAttack(10n, 20n)).toEqual({
      error: "none",
      hitChance: 100,
      counterChance: 100,
      damage: 3,
      targetHealthAfter: 0,
      lethal: true,
      counter: false,
      counterDamage: 0,
      attackerHealthAfter: 6,
      counterLethal: false,
      // No kinds here either, and the answering half leans nowhere for the
      // second reason as well: there is no answer to lean.
      lean: "none",
      counterLean: "none"
    });
  });

  it("is insensitive to source unit ordering", () => {
    const forward = createEncounter(definition());
    const reversed = createEncounter({
      ...definition(),
      units: [...definition().units].reverse()
    });
    if (forward.error !== "none" || reversed.error !== "none") {
      throw new Error("both encounters should be creatable");
    }
    expect(forward.encounter.canonicalHash()).toBe(reversed.encounter.canonicalHash());
  });

  it.each([
    [{ type: "wait", unitId: 999n }, "unknown_unit"],
    [{ type: "wait", unitId: 20n }, "wrong_side"],
    [{ type: "move", unitId: 10n, destination: { x: -1, y: 1 } }, "invalid_destination"],
    // Two tiles away and occupied by the enemy: the engine now reports the more
    // specific occupancy failure before it considers reachability.
    [{ type: "move", unitId: 10n, destination: { x: 2, y: 1 } }, "occupied_destination"],
    [{ type: "move", unitId: 10n, destination: { x: 1, y: 1 } }, "none"]
  ] as const)("applies command validation precedence for %s", (command, expected) => {
    expect(encounter().apply(command).error).toBe(expected);
  });

  it("rejects commands atomically without advancing or changing the canonical hash", () => {
    const simulation = encounter();
    const before = simulation.canonicalHash();
    const beforeState = canonicalState(simulation.snapshot());
    const result = simulation.apply({
      type: "attack",
      unitId: 10n,
      targetId: 20n
    });
    expect(result).toEqual({ error: "target_out_of_range", events: [] });
    expect(simulation.canonicalHash()).toBe(before);
    expect(canonicalState(simulation.snapshot())).toEqual(beforeState);
  });

  it("uses a minimum of one damage and rejects friendly, defeated, and completed targets", () => {
    const tough = definition();
    tough.units = [
      { ...tough.units[0]!, health: 1, defense: 99, position: { x: 1, y: 1 } },
      tough.units[1]!
    ];
    const created = createEncounter(tough);
    if (created.error !== "none") throw new Error(created.error);
    const result = created.encounter.apply({ type: "attack", unitId: 10n, targetId: 20n });
    expect(result.events[0]).toMatchObject({ type: "unit_damaged", amount: 1 });
    const completeHash = created.encounter.canonicalHash();
    expect(created.encounter.apply({ type: "wait", unitId: 10n }).error).toBe(
      "encounter_complete"
    );
    expect(created.encounter.canonicalHash()).toBe(completeHash);
  });

  it("returns invalid_command for an unrecognized command discriminator", () => {
    const invalid = { type: "dance", unitId: 10n } as unknown as Command;
    expect(encounter().apply(invalid).error).toBe("invalid_command");
  });

  it("checks the acting unit before the command discriminator", () => {
    const invalid = { type: "dance", unitId: 999n } as unknown as Command;
    expect(encounter().apply(invalid).error).toBe("unknown_unit");
  });

  it("covers target and occupancy errors without changing turns", () => {
    const input = definition();
    const created = createEncounter({
      ...input,
      units: [
        input.units[0]!,
        input.units[1]!,
        {
          id: 11n,
          unitTypeId: 100n,
          side: "first",
          position: { x: 0, y: 0 },
          health: 2,
          strength: 1,
          defense: 0
        }
      ]
    });
    if (created.error !== "none") throw new Error(created.error);
    const simulation = created.encounter;
    const initial = simulation.canonicalHash();
    expect(
      simulation.apply({ type: "move", unitId: 10n, destination: { x: 0, y: 0 } }).error
    ).toBe("occupied_destination");
    expect(simulation.apply({ type: "attack", unitId: 10n, targetId: 999n }).error).toBe(
      "unknown_target"
    );
    expect(simulation.apply({ type: "attack", unitId: 10n, targetId: 11n }).error).toBe(
      "friendly_target"
    );
    expect(simulation.canonicalHash()).toBe(initial);
  });

  it("distinguishes defeated actors and targets while an encounter continues", () => {
    const created = createEncounter({
      width: 3,
      height: 2,
      units: [
        {
          id: 1n,
          unitTypeId: 1n,
          side: "first",
          position: { x: 0, y: 0 },
          health: 2,
          strength: 2,
          defense: 0
        },
        {
          id: 2n,
          unitTypeId: 1n,
          side: "first",
          position: { x: 0, y: 1 },
          health: 2,
          strength: 2,
          defense: 0
        },
        {
          id: 3n,
          unitTypeId: 2n,
          side: "second",
          position: { x: 1, y: 0 },
          health: 1,
          strength: 3,
          defense: 0
        },
        {
          id: 4n,
          unitTypeId: 2n,
          side: "second",
          position: { x: 1, y: 1 },
          health: 2,
          strength: 3,
          defense: 0
        }
      ]
    });
    if (created.error !== "none") throw new Error(created.error);
    const simulation = created.encounter;
    expect(simulation.apply({ type: "attack", unitId: 1n, targetId: 3n }).error).toBe("none");
    expect(simulation.apply({ type: "attack", unitId: 3n, targetId: 1n }).error).toBe(
      "defeated_unit"
    );
    expect(simulation.apply({ type: "attack", unitId: 4n, targetId: 3n }).error).toBe(
      "target_defeated"
    );
    expect(simulation.snapshot().activeSide).toBe("second");
  });

  it("allows the second side to complete the encounter", () => {
    const input = definition();
    const created = createEncounter({
      ...input,
      units: [
        { ...input.units[0]!, position: { x: 1, y: 1 }, strength: 20 },
        input.units[1]!
      ]
    });
    if (created.error !== "none") throw new Error(created.error);
    expect(created.encounter.apply({ type: "wait", unitId: 10n }).error).toBe("none");
    expect(created.encounter.apply({ type: "attack", unitId: 20n, targetId: 10n }).error).toBe(
      "none"
    );
    expect(created.encounter.snapshot().outcome).toBe("second_side_won");
  });
});
