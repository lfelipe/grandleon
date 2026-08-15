// SPDX-License-Identifier: MIT
// The editor's authoring UI draws a reach band and an area of impact from
// `targeting-geometry`, because a shape picker has no encounter to ask: the
// author is choosing a shape and a radius, and there is no caster, no board
// and no character for a query to be about. The play path derives neither of
// them: it asks `aimableTiles` and `areaTiles`, which are the engine's own
// judgement of the command it would accept.
//
// That leaves `targeting-geometry` as the one place a copy of those rules is
// still allowed to live, and it makes this file the thing that keeps the copy
// honest. Nothing here asserts what the module's author believed the rule was.
// Every expectation is read out of the running engine: the area from the units
// a cast actually damages on a board carrying a body on every tile, the band
// from the tiles the danger query names for a character that cannot take a
// step. If `covered_by()` or `distance()` in
// `engine/simulation/src/encounter.cpp` ever stopped being the Manhattan ball,
// these tests would fail rather than the drawing quietly lying to an author.

import { describe, expect, it } from "vitest";
import {
  createEncounter,
  type AbilityDefinition,
  type Encounter,
  type UnitDefinition
} from "./encounter-simulation";
import {
  areaOffsets,
  bandOffsets,
  offsetKey,
  type AreaShape,
  type Offset
} from "./targeting-geometry";

const key = (dx: number, dy: number) => offsetKey({ dx, dy });
const sortedKeys = (offsets: readonly Offset[]) => offsets.map(offsetKey).sort();

/**
 * A square board with a body on every tile but one corner, which the caster
 * holds. Every body carries enough health to survive the cast, so what the
 * area covered is read from which of them lost any, rather than from who
 * happens to have fallen.
 */
function castingBoard(
  size: number,
  ability: AbilityDefinition
): { encounter: Encounter; casterId: bigint; bodies: Map<string, bigint> } {
  const casterId = 1n;
  const units: UnitDefinition[] = [{
    id: casterId,
    unitTypeId: 100n,
    side: "first",
    position: { x: 0, y: 0 },
    health: 200,
    strength: 1,
    defense: 0,
    movement: 0,
    abilityIds: [ability.id]
  }];
  const bodies = new Map<string, bigint>();
  let next = 2n;
  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      if (x === 0 && y === 0) continue;
      bodies.set(`${x}:${y}`, next);
      units.push({
        id: next,
        unitTypeId: 200n,
        side: "second",
        position: { x, y },
        // Far more health than the cast removes, so a covered body is still
        // standing to be counted and the board never thins out mid-probe.
        health: 200,
        strength: 1,
        defense: 0,
        movement: 0
      });
      next += 1n;
    }
  }
  const created = createEncounter({
    width: size,
    height: size,
    units,
    abilities: [ability]
  });
  if (created.error !== "none") {
    throw new Error(`the engine refused the probe board: ${created.error}`);
  }
  return { encounter: created.encounter, casterId, bodies };
}

/**
 * The offsets from `centre` of every body the engine took health from when the
 * cast landed there: the area of impact as the engine itself applies it.
 */
function engineCoveredOffsets(
  size: number,
  ability: AbilityDefinition,
  centre: { x: number; y: number }
): string[] {
  const { encounter, casterId, bodies } = castingBoard(size, ability);
  const before = new Map(
    encounter.snapshot().units.map((unit) => [unit.id, unit.health])
  );
  const result = encounter.apply({
    type: "ability",
    unitId: casterId,
    abilityId: ability.id,
    destination: centre
  });
  expect(result.error).toBe("none");
  const after = new Map(
    encounter.snapshot().units.map((unit) => [unit.id, unit.health])
  );

  const covered: string[] = [];
  for (const [position, id] of bodies) {
    const [x, y] = position.split(":").map(Number);
    const lost = (before.get(id) ?? 0) - (after.get(id) ?? 0);
    if (lost > 0) covered.push(key((x ?? 0) - centre.x, (y ?? 0) - centre.y));
  }
  encounter.dispose();
  return covered.sort();
}

/**
 * The offsets the danger query names for a lone motionless carrier: the reach
 * band as the engine itself measures it. Movement zero leaves the character
 * exactly one stance, its own tile, so the zone is the band and nothing else.
 */
function engineBandOffsets(
  size: number,
  stance: { x: number; y: number },
  minimumReach: number,
  maximumReach: number
): string[] {
  const created = createEncounter({
    width: size,
    height: size,
    units: [
      {
        id: 1n,
        unitTypeId: 100n,
        side: "second",
        position: stance,
        health: 10,
        strength: 1,
        defense: 0,
        movement: 0,
        actionPoints: 1,
        minimumReach,
        maximumReach
      },
      // The engine refuses a battle with an empty side. This one stands in the
      // far corner and never acts; it is on the other side, so it contributes
      // nothing to the zone being read.
      {
        id: 2n,
        unitTypeId: 200n,
        side: "first",
        position: { x: size - 1, y: size - 1 },
        health: 10,
        strength: 1,
        defense: 0,
        movement: 0
      }
    ]
  });
  if (created.error !== "none") {
    throw new Error(`the engine refused the probe board: ${created.error}`);
  }
  const encounter = created.encounter;
  const tiles = encounter
    .dangerTiles("second")
    .map((tile: { x: number; y: number }) =>
      key(tile.x - stance.x, tile.y - stance.y))
    .sort();
  encounter.dispose();
  return tiles;
}

/** The drawn offsets that fall on a board of `size` around `centre`. */
function onBoard(
  offsets: readonly Offset[],
  size: number,
  centre: { x: number; y: number }
): string[] {
  return sortedKeys(
    offsets.filter((offset) => {
      const x = centre.x + offset.dx;
      const y = centre.y + offset.dy;
      return x >= 0 && y >= 0 && x < size && y < size;
    })
  );
}

describe("the drawn area of impact and the area the engine applies", () => {
  const size = 9;
  const centre = { x: 4, y: 4 };

  const cases: readonly { shape: AreaShape; radius: number }[] = [
    { shape: "single", radius: 0 },
    // A radius the shape is documented to ignore. The engine ignores it, and
    // so must the drawing: if either started honouring it they would disagree.
    { shape: "single", radius: 3 },
    { shape: "cross", radius: 0 },
    { shape: "cross", radius: 4 },
    { shape: "diamond", radius: 0 },
    { shape: "diamond", radius: 1 },
    { shape: "diamond", radius: 2 },
    { shape: "diamond", radius: 3 }
  ];

  for (const { shape, radius } of cases) {
    it(`covers what a ${shape} of radius ${radius} covers`, () => {
      const ability: AbilityDefinition = {
        id: 900n,
        kind: "damage",
        damageType: "physical",
        area: shape,
        power: 5,
        minimumReach: 1,
        maximumReach: 20,
        radius,
        accuracy: 100
      };
      const engine = engineCoveredOffsets(size, ability, centre);
      // Guarded against agreeing on nothing: the tile aimed at is always hit.
      expect(engine).toContain(key(0, 0));
      expect(onBoard(areaOffsets(shape, radius, size), size, centre))
        .toEqual(engine);
    });
  }

  it("spreads along the axes rather than into the corners", () => {
    // The property that makes this a Manhattan ball and not a square: at
    // radius two the tile two steps north is covered and the diagonal
    // neighbour-of-a-neighbour is not. Read from the engine, not asserted of
    // the drawing, so it is the engine's geometry being described.
    const ability: AbilityDefinition = {
      id: 901n,
      kind: "damage",
      damageType: "physical",
      area: "diamond",
      power: 5,
      minimumReach: 1,
      maximumReach: 20,
      radius: 2,
      accuracy: 100
    };
    const engine = new Set(engineCoveredOffsets(size, ability, centre));
    expect(engine.has(key(0, -2))).toBe(true);
    expect(engine.has(key(2, 0))).toBe(true);
    expect(engine.has(key(1, 1))).toBe(true);
    expect(engine.has(key(2, 2))).toBe(false);
    expect(engine.has(key(2, 1))).toBe(false);
  });
});

describe("the drawn reach band and the band the engine threatens", () => {
  const size = 11;
  const stance = { x: 5, y: 5 };

  const bands: readonly [number, number][] = [
    [1, 1],
    [1, 2],
    [1, 4],
    [2, 2],
    [2, 3],
    [3, 5],
    [4, 4]
  ];

  for (const [minimum, maximum] of bands) {
    it(`admits what a band of ${minimum} to ${maximum} admits`, () => {
      const engine = engineBandOffsets(size, stance, minimum, maximum);
      expect(engine.length).toBeGreaterThan(0);
      expect(onBoard(bandOffsets(minimum, maximum, size), size, stance))
        .toEqual(engine);
    });
  }

  it("leaves the hole a minimum reach above one makes", () => {
    // A bow that cannot hit an adjacent enemy: the engine's own zone omits the
    // character's tile and its neighbours, and keeps the ring beyond them.
    const engine = new Set(engineBandOffsets(size, stance, 3, 4));
    expect(engine.has(key(0, 0))).toBe(false);
    expect(engine.has(key(1, 0))).toBe(false);
    expect(engine.has(key(0, 2))).toBe(false);
    expect(engine.has(key(0, 3))).toBe(true);
    expect(engine.has(key(2, 2))).toBe(true);
    expect(engine.has(key(0, 5))).toBe(false);
  });

  it("never admits the tile the character stands on, at any band", () => {
    // A reach band is measured from the stance, so the stance is outside every
    // band the vocabulary can express, which is why the grid refuses a
    // painted shape that includes the origin.
    for (const [minimum, maximum] of bands) {
      expect(engineBandOffsets(size, stance, minimum, maximum))
        .not.toContain(key(0, 0));
    }
  });
});

// The two probes above read a band and an area sideways, out of a danger zone
// and out of who lost health, because those were the only read-only queries
// that touched either rule. The engine answers both head on, and the play
// path asks it directly. So these tie the two together: what the direct query
// draws is what the sideways probe has been proving the drawing against, which
// is what keeps this whole file one standard rather than two.
describe("the aiming queries the engine now answers directly", () => {
  const size = 9;
  const centre = { x: 4, y: 4 };
  const ability: AbilityDefinition = {
    id: 902n,
    kind: "damage",
    damageType: "physical",
    area: "diamond",
    power: 5,
    minimumReach: 1,
    maximumReach: 20,
    radius: 2,
    accuracy: 100
  };

  it("splashes exactly the bodies the cast takes health from", () => {
    const { encounter } = castingBoard(size, ability);
    const drawn = encounter
      .areaTiles(ability.id, centre)
      .map((tile: { x: number; y: number }) =>
        key(tile.x - centre.x, tile.y - centre.y))
      .sort();
    encounter.dispose();
    expect(drawn.length).toBeGreaterThan(1);
    expect(drawn).toEqual(engineCoveredOffsets(size, ability, centre));
  });

  it("aims a cast at every tile of its band, occupied or not", () => {
    // The caster stands in a corner of a board with a body on every other
    // tile, so a query that quietly answered "the tiles somebody is standing
    // on" would pass here, which is why the band reaches past the far corner
    // and the caster's own tile is the one thing left out.
    const { encounter, casterId } = castingBoard(size, ability);
    const aimed = encounter
      .aimableTiles(casterId, { kind: "cast", abilityId: ability.id })
      .map((tile: { x: number; y: number }) => key(tile.x, tile.y))
      .sort();
    encounter.dispose();
    expect(aimed).not.toContain(key(0, 0));
    expect(aimed).toEqual(
      onBoard(
        bandOffsets(ability.minimumReach, ability.maximumReach, size),
        size,
        { x: 0, y: 0 }
      )
    );
  });

  it("offers a gesture on the character rather than on what is in reach", () => {
    const { encounter, casterId, bodies } = castingBoard(size, ability);
    expect(
      encounter.gestureAvailable(casterId, {
        kind: "cast",
        abilityId: ability.id
      })
    ).toBe(true);
    // A spell nothing defines is refused before its aim is looked at, and
    // lighting no tile follows from that rather than standing in for it.
    expect(
      encounter.gestureAvailable(casterId, { kind: "cast", abilityId: 7777n })
    ).toBe(false);
    expect(
      encounter.aimableTiles(casterId, { kind: "cast", abilityId: 7777n })
    ).toEqual([]);
    // Somebody on the side that is not acting is offered nothing at all, which
    // is a fact about whose turn it is and not about the board around them.
    const body = bodies.get("1:0");
    expect(body).toBeDefined();
    expect(encounter.gestureAvailable(body!, { kind: "strike" })).toBe(false);
    encounter.dispose();
  });
});
