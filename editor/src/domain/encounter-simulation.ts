// SPDX-License-Identifier: MIT
// The authoritative encounter simulation, executed in the browser.
//
// This module is a binding, not an implementation. Movement, combat, victory,
// activation order and the canonical hash are all decided by the C++ engine
// compiled to WebAssembly in platform/web. Nothing here reimplements
// a game rule; the only logic below is encoding and decoding the wire format
// documented in platform/web/README.md.
//
// The module has to be instantiated before an encounter can be created, so
// call `initEncounterEngine()` once at application start and gate the Play
// entry points on `isEncounterEngineReady()`.

import {
  simulationModuleBytes,
  simulationModuleDigest
} from "../generated/simulation-module";

export type Side = "first" | "second";
export type Outcome = "ongoing" | "first_side_won" | "second_side_won";

export interface Position {
  x: number;
  y: number;
}

export interface UnitDefinition {
  id: bigint;
  unitTypeId: bigint;
  side: Side;
  position: Position;
  health: number;
  strength: number;
  /**
   * Equipped-weapon power, resolved by the caller like reach. Basic-attack
   * damage is strength + power - defense, minimum one.
   */
  power?: number;
  defense: number;
  /** Reduces magical damage, where defense reduces physical damage. */
  resistance?: number;
  /**
   * Percentage points added to the chance a strike this unit makes lands. Zero
   * leaves the chance at the weapon's or cast's own authored accuracy.
   */
  skill?: number;
  /**
   * Percentage points on this unit's side of every hit roll: added when it
   * swings, subtracted when it is swung at.
   */
  luck?: number;
  /** Percentage points subtracted from the chance a strike against it lands. */
  evasion?: number;
  /**
   * The magical counterpart to strength. A magical cast deals
   * max(1, magic + power - resistance); a physical cast is unaffected.
   */
  magic?: number;
  /** Orthogonal steps per move command. */
  movement?: number;
  /** Commands the unit may issue in one activation. */
  actionPoints?: number;
  /** Higher acts earlier under an ordered turn. */
  speed?: number;
  /** Whether striking leaves the unit able to keep acting. */
  actsAfterAttacking?: boolean;
  minimumReach?: number;
  maximumReach?: number;
  /**
   * How often the weapon in hand lands, as a whole percentage. Omitted always
   * lands. A unit carrying a weapon has this resolved from that weapon by the
   * engine, exactly as its power and reach band are.
   */
  accuracy?: number;
  abilityIds?: readonly bigint[];
  /**
   * Every weapon the unit carries, in carried order. The first is the weapon
   * in hand, and the engine resolves this unit's power and reach band from it,
   * so a carrying unit needs nothing flattened by the caller.
   */
  weaponIds?: readonly bigint[];
  /**
   * Terrain this unit may enter beyond open ground. "flying" is every terrain
   * there is, including any added later. Omitted is a walker.
   */
  crossings?: readonly Crossing[];
  /**
   * Every item the unit carries, in carried order, and how many of each it
   * brings to this battle. Omitting the count is one of each, which is what a
   * unit type's authored list says.
   */
  items?: readonly CarriedItem[];
  /**
   * What this character leaves behind when it falls, and how often as a whole
   * percentage. Authored as a pair or not at all: either one alone is
   * `invalid_item`, so that leaving nothing has one spelling. Omitted leaves
   * nothing and rolls nothing.
   */
  dropItemId?: bigint;
  dropChance?: number;
  /**
   * Added to the maximum of the band of every weapon this character strikes
   * with, and never to the minimum. Omitted is a character whose reach is
   * exactly their weapon's, which is every character who is not written to be
   * more than their unit type. A campaign fills it from what an author wrote
   * about a roster member; it is a fact about the archer and not about the bow,
   * which is why it rides here rather than on the shared weapon record.
   */
  reachBonus?: number;
  /**
   * What talking to this character records. Omitted is somebody no talk may
   * reach.
   *
   * Opaque to the rules: they copy it into the `unit_talked` event and stop.
   * Reading it as the key of a world flag is the campaign layer's job. It sits
   * on a placement rather than on a unit type because being talkable is a fact
   * about this character on this board, and because a battle-local unit
   * identifier differs on every appearance, so this is the only identity a
   * campaign could read back.
   */
  talkRecordId?: bigint;
  /**
   * When this character enters the board, as a round in progress, one based.
   * Omitted is somebody standing on the board from the opening, which is
   * everywhere no author wrote a wave. The earliest arrival is the second
   * round, because the first is the round the battle opens in.
   *
   * `arrivalEvery` and `arrivalTimes` state the recurrence and are authored
   * together or not at all. The engine expands them, so a wave written once is
   * one wave here and several characters on the board.
   */
  arrivalRound?: number;
  arrivalEvery?: number;
  arrivalTimes?: number;
}

/** One slot in a character's pack: what it is, and how many are left. */
export interface CarriedItem {
  id: bigint;
  count: number;
}

/** What a unit may cross that open ground is not. */
export type Crossing = "water" | "heights" | "flying";

/** What a cell asks of whoever would stand in it. */
export type Terrain = "open" | "water" | "heights";

export type AbilityKind = "damage" | "restore";
/**
 * What spending an item does. "none" is an item the rules cannot apply, a key
 * or a keepsake. Using one is refused rather than silently doing nothing.
 */
export type ItemKind = "none" | "restore";
export type DamageType = "physical" | "magical";
export type AreaShape = "single" | "cross" | "diamond";

export interface AbilityDefinition {
  id: bigint;
  kind: AbilityKind;
  damageType: DamageType;
  area: AreaShape;
  power: number;
  minimumReach: number;
  maximumReach: number;
  radius: number;
  /**
   * How often the cast lands, as a whole percentage. Omitted always lands. A
   * damaging area rolls once per unit it covers; a restoring ability never
   * rolls.
   */
  accuracy?: number;
}

/**
 * The numbers the rules read off an item. No reach band: an item reaches the
 * hand that holds it, so the only distance question the rules answer needs no
 * authored number.
 */
export interface ItemDefinition {
  id: bigint;
  kind: ItemKind;
  /** How much a restoring item gives back, before the missing-health clamp. */
  power: number;
}

/**
 * The numbers the rules read off a weapon. No damage type: a weapon's damage
 * is mitigated by defence, and only an ability carries a type.
 */
export interface WeaponDefinition {
  id: bigint;
  power: number;
  minimumReach: number;
  maximumReach: number;
  /**
   * How often it lands, as a whole percentage. Omitted always lands and rolls
   * nothing at all.
   */
  accuracy?: number;
  /**
   * Which kind of weapon this is. Omitted is a weapon of no kind, which gets
   * nothing out of a triangle.
   */
  weaponType?: bigint;
}

/**
 * One kind of weapon, and what holding it is worth against the other kinds.
 *
 * The edges are directed: a kind names what it beats, and the strike coming
 * back the other way is worth as much less. The two numbers are the game's own
 * and are the same on every entry.
 */
export interface WeaponTypeDefinition {
  id: bigint;
  strongAgainst: readonly bigint[];
  damage: number;
  accuracy: number;
}

export type ObjectiveKind =
  | "defeat_all_opponents"
  | "defeat_target"
  | "protect_target"
  /**
   * Hold out until a number of rounds has *completed*. Satisfied at the moment
   * the authored round closes, and failed when the objective's own side has
   * nobody left in the battle.
   */
  | "survive_rounds";
export type ObjectiveState = "pending" | "satisfied" | "failed";

export interface ObjectiveDefinition {
  id: bigint;
  kind: ObjectiveKind;
  side: Side;
  targetUnitId?: bigint;
  /**
   * How many rounds a `survive_rounds` objective is about. Required by that
   * kind and refused on every other, which is what every objective written
   * before it says.
   */
  roundCount?: number;
}

export interface ObjectiveResult {
  id: bigint;
  state: ObjectiveState;
}

export interface EncounterDefinition {
  width: number;
  height: number;
  units: readonly UnitDefinition[];
  abilities?: readonly AbilityDefinition[];
  /** Every weapon any unit here may carry, resolved by identity. */
  weapons?: readonly WeaponDefinition[];
  /**
   * Which kinds of weapon beat which. Omitted is a battle with no triangle in
   * it, which is every battle written before one could be drawn.
   */
  weaponTypes?: readonly WeaponTypeDefinition[];
  /** Every item any unit here may carry, resolved by identity. */
  items?: readonly ItemDefinition[];
  objectives?: readonly ObjectiveDefinition[];
  turnOrder?: TurnOrder;
  /**
   * What each cell of the board asks, row-major, width x height. Omitted is an
   * all-open board, which is what a caller with no terrain to give means and
   * what content authored before terrain had a meaning plays as.
   */
  terrain?: readonly Terrain[];
  /**
   * What each cell of the board charges whoever walks into it, row-major,
   * width x height and parallel to `terrain`. Omitted is a board where every
   * step costs one, which is what a caller with no price to give means and what
   * content authored before ground had a price plays as. A cell charging
   * nothing is an invalid map, not a free one.
   */
  movementCost?: readonly number[];
  /**
   * The region the player arranges their own troops in before the first
   * activation. Omitted or empty means the encounter has no deployment phase
   * and every unit opens on the tile it was defined on, which is what an
   * encounter with no deployment region means. Its canonical hash is computed
   * from the identical bytes.
   */
  deploymentTiles?: readonly Position[];
}

/** Who acts next. See engine/simulation for the exact rules. */
export type TurnOrder = "alternating" | "side_blocks" | "initiative";

export type CreateError =
  | "none"
  | "invalid_map"
  | "invalid_unit"
  | "duplicate_unit"
  | "occupied_position"
  | "missing_side"
  | "invalid_ability"
  | "invalid_objective"
  | "invalid_weapon"
  | "invalid_item"
  | "invalid_deployment"
  /**
   * An arrival is malformed: a recurrence half-stated, a first arrival in the
   * round the battle opens in, or more arrivals than the engine will expand.
   */
  | "invalid_arrival";

export interface UnitSnapshot extends UnitDefinition {
  maximumHealth: number;
  power: number;
  resistance: number;
  skill: number;
  luck: number;
  evasion: number;
  magic: number;
  movement: number;
  actionPoints: number;
  speed: number;
  actsAfterAttacking: boolean;
  hasActed: boolean;
  /**
   * Whether this character has already spent its turn's one walk. A unit walks
   * once per turn however many action points it has, so this is what greys the
   * move row rather than offering a walk the engine will refuse by name.
   */
  hasMoved: boolean;
  /**
   * How much of its budget this character's own turn has spent.
   *
   * Zero under `alternating` and `initiative`, where the side commits to one
   * character at a time and `remainingActionPoints` on the snapshot is the
   * whole of the budget being spent. Under `sideBlocks` several characters may
   * be part-way through their turns at once, so the snapshot's field is empty
   * and this is what a sheet subtracts to show what a character has left.
   */
  spentActionPoints: number;
  minimumReach: number;
  maximumReach: number;
  abilityIds: readonly bigint[];
  weaponIds: readonly bigint[];
  /** What the unit carries and how many of each are left. */
  items: readonly CarriedItem[];
  /** What this unit leaves behind when it falls, and how often. */
  dropItemId: bigint;
  dropChance: number;
  /**
   * What this unit adds to the reach of whatever it is holding. Already inside
   * `maximumReach`, which is the band of the weapon in hand; carried separately
   * because a sheet drawing a row per carried weapon has to widen each of those
   * bands too, and the weapon records it reads are the shared authored ones.
   */
  reachBonus: number;
  /** What talking to this character records. Zero is somebody no talk reaches. */
  talkRecordId: bigint;
  /**
   * Whether this character is standing on the board yet. False for a wave that
   * has not come in: it holds no tile, is never chosen to act, can be aimed at
   * by nothing and shades no danger tile. It is still in the battle, so its
   * side is not beaten while it is marching.
   */
  arrived: boolean;
  /**
   * Whether this character has been talked off the board.
   *
   * Departed is not defeated: they keep the health they had, no defeat event
   * names them, and nobody earns anything for them. What it does mean is that
   * they occupy no tile, are never chosen to act, and are not a living
   * character of their side. A battle whose last opponent walks away is won
   * without anybody being killed.
   */
  departed: boolean;
  /**
   * Whether this character is standing on the board at all: alive, not
   * departed, and arrived. **This is the board predicate, and anything asking
   * "is this character there" reads it rather than composing it.**
   *
   * The three facts it folds are all above, so this could be spelled here
   * instead. That is precisely why it is not. It decides who holds a
   * tile, who may be aimed at, who may be given an order and which tiles a
   * walk may end on; a copy of it kept on this side of the ABI is a copy that
   * can drift from the board with nothing to catch it, and the drift shows up
   * as a character drawn on a tile every command aimed at is refused. The
   * engine answers, and this carries the answer.
   */
  onBoard: boolean;
}

/**
 * One thing a defeated unit left behind. It enters nobody's pack and lies on
 * no tile: a drop is recorded, and the campaign layer reads the record at
 * battle end.
 */
export interface DropRecord {
  /** Whose body it came off. */
  unitId: bigint;
  /** Who felled them, and therefore whose side claims it. */
  claimantId: bigint;
  /** What fell. */
  itemId: bigint;
}

export interface EncounterSnapshot {
  width: number;
  height: number;
  activeSide: Side;
  /**
   * The unit part-way through an activation, or 0n when none is committed.
   *
   * **Always 0n under `sideBlocks`**, where turns interleave freely and no
   * character is ever committed to: the order names a side and never an actor.
   * `remainingActionPoints` is empty there for the same reason: it is what is
   * left of *the* activation, and there is no single activation to count down.
   * What a given character has left is `actionPoints - spentActionPoints` on
   * that character.
   */
  activeUnitId: bigint;
  remainingActionPoints: number;
  /**
   * Rounds that have *completed*, so the round in progress is one more than
   * this. A round is one pass through the turn order: everybody still standing
   * having acted under `side_blocks` and `initiative`, and one turn for each
   * side under `alternating`. Under alternating order the count is kept only
   * where the content gives it consequence, such as a survive-rounds objective
   * or a character who arrives on a round. It is otherwise zero.
   */
  round: number;
  activationCount: bigint;
  outcome: Outcome;
  units: readonly UnitSnapshot[];
  objectives: readonly ObjectiveResult[];
  /** What has fallen, in the order the defeats that produced it resolved. */
  drops: readonly DropRecord[];
  /**
   * The region the player arranges their own troops in, sorted row-major.
   * Empty for an encounter that authors none.
   */
  deploymentTiles: readonly Position[];
  /**
   * Whether the deployment phase is open. While it is, every ordinary command
   * is refused as `wrong_phase`, no activation has begun, and nothing has
   * drawn from any random stream.
   */
  deploying: boolean;
}

export type Command =
  | { type: "move"; unitId: bigint; destination: Position }
  | {
      type: "attack";
      unitId: bigint;
      targetId: bigint;
      /** Which carried weapon to strike with. Omitted means the one in hand. */
      weaponId?: bigint;
    }
  | { type: "wait"; unitId: bigint }
  | {
      type: "ability";
      unitId: bigint;
      abilityId: bigint;
      destination: Position;
    }
  | {
      type: "use_item";
      unitId: bigint;
      itemId: bigint;
      /** Who it is spent on. Omitted, and today only, the unit itself. */
      targetId?: bigint;
    }
  /**
   * Stands one arrangeable unit on one tile of the deployment region. Costs no
   * action point, ends no activation, and may be issued any number of times in
   * any order until the phase closes.
   */
  | { type: "deploy"; unitId: bigint; destination: Position }
  /**
   * Closes the deployment phase and opens the battle. The only way it closes:
   * the engine never decides the arrangement is finished.
   */
  | { type: "begin_battle" }
  /**
   * Talks to the adjacent character `targetId` names, whose placement authored
   * a talk record. Costs an action point and closes the activation exactly as a
   * strike does, cannot be countered, and rolls nothing. What it does is take
   * the talked-to character off the board alive. See `UnitSnapshot.departed`.
   */
  | { type: "talk"; unitId: bigint; targetId: bigint };

export type CommandError =
  | "none"
  | "encounter_complete"
  | "unknown_unit"
  | "defeated_unit"
  | "wrong_side"
  | "invalid_command"
  | "invalid_destination"
  | "occupied_destination"
  | "unknown_target"
  | "target_defeated"
  | "friendly_target"
  | "target_out_of_range"
  | "unknown_ability"
  | "unavailable_ability"
  | "activation_in_progress"
  | "no_action_points"
  | "unknown_weapon"
  | "unavailable_weapon"
  | "unknown_item"
  | "unavailable_item"
  | "depleted_item"
  | "unusable_item"
  | "wrong_phase"
  | "undeployable_unit"
  | "outside_zone"
  /**
   * The talk named a character standing on the board, alive, whose placement
   * authors no talk record.
   */
  | "not_talkable"
  /**
   * The command named a character who has already been talked off the board.
   * Its own refusal rather than `target_defeated`: somebody who walked away is
   * not somebody who died, and this is where a client learns the difference.
   */
  | "target_departed"
  /**
   * The command named a character who has not arrived on the board yet. Its own
   * refusal rather than `unknown_target`: the character is very much part of
   * this battle, and is simply not here yet.
   */
  | "target_unarrived"
  /** The command asked a character who has not arrived yet to act. */
  | "unarrived_unit"
  /**
   * The command asked a character who has been talked off the board to act. The
   * actor-side sibling of `target_departed`, and its own refusal for the same
   * reason: somebody who walked away is not somebody who died.
   */
  | "departed_unit";

export type SimulationEvent =
  | { type: "unit_moved"; unitId: bigint; position: Position }
  | { type: "unit_waited"; unitId: bigint; position: Position }
  | {
      type: "unit_damaged";
      unitId: bigint;
      relatedUnitId: bigint;
      position: Position;
      amount: number;
    }
  | {
      type: "unit_defeated";
      unitId: bigint;
      relatedUnitId: bigint;
      position: Position;
    }
  | {
      type: "encounter_completed";
      unitId: bigint;
      position: Position;
      outcome: Exclude<Outcome, "ongoing">;
    }
  | {
      type: "unit_restored";
      unitId: bigint;
      relatedUnitId: bigint;
      position: Position;
      amount: number;
    }
  | { type: "activation_ended"; unitId: bigint; position: Position }
  /**
   * A strike that was legal, was resolved, and did not land. `unitId` is
   * whoever was swung at and `relatedUnitId` whoever swung.
   */
  | {
      type: "attack_missed";
      unitId: bigint;
      relatedUnitId: bigint;
      position: Position;
    }
  /**
   * One of a carried item was spent. `unitId` is who spent it,
   * `relatedUnitId` who it was spent on, `itemId` names the item, and `amount`
   * is how many of it the spender still carries. Exactly one is consumed per
   * event, which is what makes a battle's inventory consequence derivable from
   * its events. What a restoring item gave back arrives as the `unit_restored`
   * event after this one.
   */
  | {
      type: "item_used";
      unitId: bigint;
      relatedUnitId: bigint;
      position: Position;
      itemId: bigint;
      amount: number;
    }
  /**
   * A defeated unit left something behind. `unitId` is who fell,
   * `relatedUnitId` who felled them and therefore whose side claims it,
   * `itemId` names what fell, and `amount` is one. It follows the
   * `unit_defeated` event that caused it.
   */
  | {
      type: "item_dropped";
      unitId: bigint;
      relatedUnitId: bigint;
      position: Position;
      itemId: bigint;
      amount: number;
    }
  /** A unit was stood on a deployment tile. */
  | { type: "unit_deployed"; unitId: bigint; position: Position }
  /** The deployment phase closed and the battle opened. */
  | { type: "deployment_ended"; unitId: bigint; position: Position }
  /**
   * A character was talked off the board. `unitId` is who was talked to and has
   * now departed, `relatedUnitId` who talked to them, `position` where they
   * were standing, and `recordId` the talk record their placement authored.
   * That record is the only identity stable enough to cross the battle
   * boundary, so it is what a campaign reads. Emitted instead of
   * `unit_defeated`, never beside it: a
   * departure is not a defeat.
   */
  | {
      type: "unit_talked";
      unitId: bigint;
      relatedUnitId: bigint;
      position: Position;
      recordId: bigint;
    }
  /**
   * An authored character entered the board as a round began. `unitId` is who,
   * `position` the tile it actually took, and `amount` the round in progress,
   * one based. The tile taken is the one the content asked for, or the nearest
   * one it could stand on when that tile was held.
   */
  | {
      type: "unit_arrived";
      unitId: bigint;
      position: Position;
      amount: number;
    }
  /**
   * A blow that would have felled a character was caught by that character's
   * health floor, so they are still standing at one health. `unitId` is who held
   * on and `relatedUnitId` who struck: the same two identities, the same way
   * round, as the `unit_damaged` event this one follows.
   *
   * Emitted immediately after that damage event, and only where the floor
   * actually caught something. It exists so that a surface is told rather than
   * left to subtract a previous health total from an amount and notice the two
   * do not add up, which is the derivation this engine refuses to make anybody
   * do. A miss is an event rather than the absence of damage, and holding on is
   * an event for the same reason.
   */
  | {
      type: "unit_endured";
      unitId: bigint;
      relatedUnitId: bigint;
      position: Position;
    };

export interface CommandResult {
  error: CommandError;
  events: readonly SimulationEvent[];
}

export type CreateResult =
  | { error: "none"; encounter: Encounter }
  | { error: Exclude<CreateError, "none"> };

export type UnitBehavior = "hold" | "patrol" | "pursue";

/**
 * What one attack would do, before it is committed. The error is the refusal
 * apply() would return for the same attack in the same state; when it is
 * "none", the numbers are exactly what apply() would inflict.
 */
export interface AttackForecast {
  error: CommandError;
  /**
   * How often this strike lands, as a whole percentage: exactly the chance the
   * engine rolls against, unrounded and unaveraged. 100 is certain, and a
   * certain strike rolls nothing. Every number below says what happens when it
   * lands; a miss takes nothing at all.
   */
  hitChance: number;
  damage: number;
  targetHealthAfter: number;
  lethal: boolean;
  /**
   * Whether the target answers, given it is still standing when the attack
   * resolves: inside its own reach band, and either surviving the blow or able
   * to be missed by it.
   */
  counter: boolean;
  /** How often that answer lands, on the same terms as `hitChance`. */
  counterChance: number;
  /** What the counter strikes for, raw, exactly as `damage` is. Zero if none. */
  counterDamage: number;
  /** The attacker's health once the exchange is over, counter or not. */
  attackerHealthAfter: number;
  /** Whether the counter fells the attacker. */
  counterLethal: boolean;
}

/**
 * What spending one carried item would do, before it is committed. The error is
 * the refusal apply() would return for the same use in the same state.
 */
export interface ItemForecast {
  error: CommandError;
  kind: ItemKind;
  /** Health gained, already clamped to the health the target is missing. */
  restored: number;
  targetHealthAfter: number;
  /** How many of the item the user holds once the use is done. */
  remainingAfter: number;
}

/**
 * What one talk would do, before it is committed.
 *
 * The easiest promise in the engine to keep, by design: a talk that is accepted
 * cannot fail. Everything it could have gone wrong at is decided here and
 * refused: aimed at nobody, at somebody dead, at somebody already gone, at
 * somebody with nothing to say, at somebody too far away. So `error` being "none"
 * is the entire forecast: this character will leave the board, and that record
 * is what the battle will report.
 */
export interface TalkForecast {
  error: CommandError;
  /** Who would leave. Zero whenever `error` is anything but "none". */
  departingId: bigint;
  /** What leaving would record. Zero on the same terms. */
  recordId: bigint;
}

/**
 * One aim, said in the engine's words: the gesture a player has chosen and
 * whatever identity they chose it by.
 *
 * This is a client's own aiming state and nothing more. A surface holds exactly
 * these values between picking a menu row and pressing confirm, so it hands
 * them over rather than turning them into a rule of its own, which is the
 * whole reason the engine takes an aim at all rather than four queries. A
 * strike naming no weapon means the weapon in hand, exactly as an attack
 * command means it.
 */
export type AimedGesture =
  | { kind: "walk" }
  | { kind: "strike"; weaponId?: bigint }
  | { kind: "cast"; abilityId: bigint }
  | { kind: "talk" };

/** The `Gesture` values the boundary carries. Order is the engine's. */
const gestureCodes: Readonly<Record<AimedGesture["kind"], number>> = {
  walk: 0,
  strike: 1,
  cast: 2,
  talk: 3
};

/**
 * The two identities an aim carries, flattened for the boundary, or undefined
 * when one of them could not cross it.
 *
 * Zero is what the engine reads as "the weapon in hand" and as "no ability
 * named", so a walk and a talk send two zeros rather than being given a
 * separate shape: the engine reads neither field for them, and a client that
 * had to know which fields its gesture used would be holding the piece of the
 * rule the engine was never asked for.
 */
function gestureIdentities(
  gesture: AimedGesture
): { weaponId: bigint; abilityId: bigint } | undefined {
  const weaponId = gesture.kind === "strike" ? gesture.weaponId ?? 0n : 0n;
  const abilityId = gesture.kind === "cast" ? gesture.abilityId : 0n;
  if (!representableId(weaponId) || !representableId(abilityId)) {
    return undefined;
  }
  return { weaponId, abilityId };
}

export interface Encounter {
  apply(command: Command): CommandResult;
  /** Prices one attack without changing any state. */
  /**
   * Prices one strike without committing it, both halves of it: what the
   * target loses, and what the counter takes back when the target survives
   * inside its own reach band. `weaponId` names which carried weapon to
   * price; zero, the default, is the weapon in hand.
   */
  forecastAttack(
    attackerId: bigint,
    targetId: bigint,
    weaponId?: bigint
  ): AttackForecast;
  /**
   * Prices spending one carried item without committing it. Shorter promise
   * than a strike's, and a stricter one: nothing rolls, so `restored` is the
   * number apply() will deliver rather than the number it delivers when a roll
   * lands. `targetId` omitted is the unit itself.
   */
  forecastItem(
    unitId: bigint,
    itemId: bigint,
    targetId?: bigint
  ): ItemForecast;
  /**
   * Prices one talk without committing it. A talk rolls nothing and clamps
   * nothing, so there is no number to show and this is asked for its refusal: a
   * client offers the gesture exactly where the answer is "none", which is how
   * the row's legality stays the engine's and not a rule the client keeps a
   * second copy of.
   */
  forecastTalk(unitId: bigint, targetId: bigint): TalkForecast;
  /**
   * Every tile the unit could occupy after one accepted move command, from
   * the engine's own traversal, the same one apply() judges a move against.
   * A client lights these rather than re-deriving the movement rule, so the
   * board it draws can never disagree with what the engine will accept. An
   * unknown or defeated unit reaches nothing. Row-major order.
   */
  reachableTiles(unitId: bigint): Position[];
  /**
   * Every tile the named unit could be deployed to, on exactly the terms
   * `reachableTiles` answers the move rule on: a tile is in the result
   * precisely when a `deploy` command naming it would be accepted. Empty once
   * the phase closes and empty for anybody who is not arrangeable. Row-major.
   */
  deployableTiles(unitId: bigint): Position[];
  /**
   * Every tile at least one living unit on the side could reach and strike:
   * movement plus the band of every weapon it carries and every damaging
   * ability it knows, honouring minimum reach and terrain. The Fire Emblem
   * danger zone, budgeted: a unit that has spent its turn contributes
   * nothing, and one part-way through an activation contributes only what its
   * remaining action points can still buy. So this answers "who could reach me
   * before I act again", not "who could ever reach me", and it is still not a
   * claim that all of them will. Row-major order.
   */
  dangerTiles(side: Side): Position[];
  /**
   * Every tile the unit could aim that gesture at: the aiming counterpart of
   * `reachableTiles`, and the same promise: a tile is in the result exactly
   * when the command committing the gesture there would be accepted. So a
   * board that lights these can never offer a strike or a cast the engine will
   * refuse, and no client keeps a second copy of a reach band.
   *
   * An empty list is never a refusal on its own. A strike with nobody in reach
   * lights nothing and is still a gesture this character may make; that is
   * `gestureAvailable`'s question, and the two are told apart deliberately.
   * Row-major order.
   */
  aimableTiles(unitId: bigint, gesture: AimedGesture): Position[];
  /**
   * Whether the unit could make that gesture at all right now, whatever it
   * were aimed at. This is what decides whether a menu offers the row: false
   * means every command carrying the gesture is refused before the engine
   * looks at what it named: a character who has already walked, one whose
   * turn is over, a weapon it is not carrying, a spell it does not know. True
   * means only the aim is left to judge.
   */
  gestureAvailable(unitId: bigint, gesture: AimedGesture): boolean;
  /**
   * Every tile an area cast aimed at one tile would cover, from the same
   * membership test apply() walks the units against. Clipped to the board and
   * including the tile aimed at; empty for an ability the encounter does not
   * define and empty for a single-tile one, whose splash is the tile aimed at
   * and would be drawn twice.
   *
   * Asks nothing about whether the cast may be aimed there; that is
   * `aimableTiles`. A surface drawing a splash outside the band would be
   * drawing a cast the engine will refuse. Row-major order.
   */
  areaTiles(abilityId: bigint, centre: Position): Position[];
  /**
   * Proposes a command for an unattended unit. Behaviour is policy, so the
   * engine still validates whatever comes back; a caller applies it like any
   * other command and handles rejection.
   */
  decide(
    unitId: bigint,
    behavior: UnitBehavior,
    patrol?: readonly Position[]
  ): Command | undefined;
  snapshot(): EncounterSnapshot;
  canonicalHash(): bigint;
  /**
   * Releases the encounter held by the engine. Safe to call more than once.
   * Forgotten encounters are also reclaimed when garbage collected, but an
   * explicit release keeps the engine's handle table small and predictable.
   */
  dispose(): void;
}

interface SimulationExports {
  readonly memory: WebAssembly.Memory;
  readonly _initialize: () => void;
  readonly gl_sim_io_buffer: () => number;
  readonly gl_sim_io_capacity: () => number;
  readonly gl_content_buffer: () => number;
  readonly gl_content_capacity: () => number;
  readonly gl_content_compile: (sourceSize: number) => number;
  readonly gl_sim_create: (payloadSize: number) => number;
  readonly gl_sim_destroy: (handle: number) => void;
  readonly gl_sim_apply: (handle: number, payloadSize: number) => number;
  readonly gl_sim_snapshot: (handle: number) => number;
  readonly gl_sim_canonical_hash: (handle: number) => bigint;
  readonly gl_sim_forecast_attack: (
    handle: number,
    attackerId: bigint,
    targetId: bigint,
    weaponId: bigint
  ) => number;
  readonly gl_sim_forecast_item: (
    handle: number,
    unitId: bigint,
    targetId: bigint,
    itemId: bigint
  ) => number;
  readonly gl_sim_forecast_talk: (
    handle: number,
    unitId: bigint,
    targetId: bigint
  ) => number;
  readonly gl_sim_reachable_tiles: (handle: number, unitId: bigint) => number;
  readonly gl_sim_deployable_tiles: (handle: number, unitId: bigint) => number;
  readonly gl_sim_danger_tiles: (handle: number, side: number) => number;
  readonly gl_sim_aimable_tiles: (
    handle: number,
    unitId: bigint,
    kind: number,
    weaponId: bigint,
    abilityId: bigint
  ) => number;
  readonly gl_sim_gesture_available: (
    handle: number,
    unitId: bigint,
    kind: number,
    weaponId: bigint,
    abilityId: bigint
  ) => number;
  readonly gl_sim_area_tiles: (
    handle: number,
    abilityId: bigint,
    x: number,
    y: number
  ) => number;
  readonly gl_core_stable_content_id: (length: number) => bigint;
  readonly gl_ai_decide: (handle: number, payloadSize: number) => number;
  readonly gl_sim_create_error_name: (error: number) => number;
  readonly gl_sim_command_error_name: (error: number) => number;
  readonly gl_campaign_create: (payloadSize: number) => number;
  readonly gl_campaign_destroy: (handle: number) => void;
  readonly gl_campaign_add_dialogue: (
    handle: number,
    payloadSize: number
  ) => number;
  readonly gl_campaign_state: (handle: number) => number;
  readonly gl_campaign_advance: (handle: number, payloadSize: number) => number;
  readonly gl_campaign_advance_story: (handle: number) => number;
  readonly gl_campaign_dialogue: (handle: number, dialogueId: bigint) => number;
  readonly gl_campaign_error_name: (error: number) => number;
  readonly gl_campaign_dialogue_error_name: (error: number) => number;
  readonly gl_campaign_session_create: (payloadSize: number) => number;
  readonly gl_campaign_session_destroy: (handle: number) => void;
  readonly gl_campaign_session_add_unit_type: (
    handle: number,
    payloadSize: number
  ) => number;
  readonly gl_campaign_session_add_board: (
    handle: number,
    payloadSize: number
  ) => number;
  readonly gl_campaign_session_begin: (
    handle: number,
    payloadSize: number
  ) => number;
  readonly gl_campaign_session_state: (handle: number) => number;
  readonly gl_campaign_session_advance_story: (handle: number) => number;
  readonly gl_campaign_session_board: (handle: number) => number;
  readonly gl_campaign_session_commit: (handle: number) => number;
  readonly gl_campaign_session_company: (handle: number) => number;
  readonly gl_campaign_session_manage: (
    handle: number,
    payloadSize: number
  ) => number;
  readonly gl_campaign_session_error_name: (error: number) => number;
  readonly gl_campaign_outcome_error_name: (error: number) => number;
  readonly gl_campaign_roster_error_name: (error: number) => number;
  readonly gl_campaign_save_error_name: (error: number) => number;
  readonly gl_campaign_migration_error_name: (error: number) => number;
  readonly gl_storage_read: (payloadSize: number) => number;
  readonly gl_storage_write: (payloadSize: number) => number;
  readonly gl_storage_erase: (payloadSize: number) => number;
  readonly gl_storage_error_name: (error: number) => number;
  readonly gl_campaign_operation_name: (kind: number) => number;
}

/** Boundary status codes, mirroring AbiStatus in simulation_abi.cpp. */
const abiOk = 0;
/**
 * A battle whose event log is full. The engine holds every event a campaign
 * battle emits so that the commit can derive what it meant, and it will not
 * take a command it cannot record. This is a runaway board rather than a bad
 * payload, and the session it belongs to is still alive.
 */
const abiEventLogFull = 4;

const INT16_MIN = -32_768;
const INT16_MAX = 32_767;
const UINT16_MAX = 65_535;
const UINT64_MAX = (1n << 64n) - 1n;

const sides: readonly Side[] = ["first", "second"];
const outcomes: readonly Outcome[] = [
  "ongoing",
  "first_side_won",
  "second_side_won"
];
const commandDiscriminators: Readonly<Record<string, number>> = {
  move: 0,
  attack: 1,
  wait: 2,
  ability: 3,
  use_item: 4,
  deploy: 5,
  begin_battle: 6,
  talk: 7
};
const itemKinds: readonly ItemKind[] = ["none", "restore"];
const abilityKinds: readonly AbilityKind[] = ["damage", "restore"];
const damageTypes: readonly DamageType[] = ["physical", "magical"];
const areaShapes: readonly AreaShape[] = ["single", "cross", "diamond"];
const objectiveKinds: readonly ObjectiveKind[] = [
  "defeat_all_opponents",
  "defeat_target",
  "protect_target",
  "survive_rounds"
];
const behaviors: readonly UnitBehavior[] = ["hold", "patrol", "pursue"];
const turnOrders: readonly TurnOrder[] = [
  "alternating",
  "side_blocks",
  "initiative"
];
// The engine's own numbering for both halves of the terrain vocabulary. The
// bits are `crossing_water`, `crossing_heights` and `crossing_every` in
// engine/simulation/include/grandleon/simulation/encounter.hpp; flight is the
// high bit so a terrain added later is crossed by everything that flies.
const crossingBits: Readonly<Record<Crossing, number>> = {
  water: 1,
  heights: 2,
  flying: 128
};
const terrains: readonly Terrain[] = ["open", "water", "heights"];
const objectiveStates: readonly ObjectiveState[] = [
  "pending",
  "satisfied",
  "failed"
];
const eventTypes = [
  "unit_moved",
  "unit_waited",
  "unit_damaged",
  "unit_defeated",
  "encounter_completed",
  "unit_restored",
  "activation_ended",
  "attack_missed",
  "item_used",
  "item_dropped",
  "unit_deployed",
  "deployment_ended",
  "unit_talked",
  "unit_arrived",
  "unit_endured"
] as const;

/**
 * What a campaign does with a character who falls in battle, as
 * `package_runtime::CharacterLoss` spells it. Indexed by the byte the engine
 * writes, which is one-based, so the zeroth entry is the value no package ever
 * carries.
 */
export type CharacterLoss = "permanent" | "recoverable";
const characterLosses: readonly CharacterLoss[] = [
  "permanent",
  "permanent",
  "recoverable"
];

/**
 * A byte the engine wrote, as a word. Anything outside the menu is read as
 * `permanent`, which is the rule every campaign that states nothing plays
 * under. It is the safe reading, because it does not quietly promise a player
 * their characters are coming back.
 */
function characterLossName(value: number): CharacterLoss {
  return characterLosses[value] ?? "permanent";
}

interface Engine {
  readonly exports: SimulationExports;
  readonly bufferOffset: number;
  readonly bufferCapacity: number;
  readonly createErrors: readonly string[];
  readonly commandErrors: readonly string[];
  readonly campaignErrors: readonly string[];
  readonly dialogueErrors: readonly string[];
  readonly sessionErrors: readonly string[];
  readonly rosterErrors: readonly string[];
  readonly storageErrors: readonly string[];
  readonly saveErrors: readonly string[];
  readonly migrationErrors: readonly string[];
  readonly operationNames: readonly string[];
  readonly outcomeErrors: readonly string[];
}

let engine: Engine | undefined;
let loading: Promise<void> | undefined;

/**
 * Instantiates the WebAssembly simulation. Repeated calls share one instance
 * and one in-flight instantiation.
 */
export function initEncounterEngine(): Promise<void> {
  if (engine) return Promise.resolve();
  loading ??= instantiate().then((ready) => {
    engine = ready;
  });
  return loading;
}

/** Whether the engine is instantiated and encounters can be created. */
export function isEncounterEngineReady(): boolean {
  return engine !== undefined;
}

/** SHA-256 of the WebAssembly module actually loaded, for diagnostics. */
export function encounterEngineDigest(): string {
  return simulationModuleDigest;
}

async function instantiate(): Promise<Engine> {
  // The module is self-contained: it declares no imports at all, so there is no
  // host surface through which browser state could reach the simulation.
  const { instance } = await WebAssembly.instantiate(
    simulationModuleBytes() as BufferSource,
    {}
  );
  const exports = instance.exports as unknown as SimulationExports;
  exports._initialize();
  const bufferOffset = exports.gl_sim_io_buffer();
  const bufferCapacity = exports.gl_sim_io_capacity();
  const ready: Engine = {
    exports,
    bufferOffset,
    bufferCapacity,
    // Error vocabularies are read back out of the engine rather than restated
    // here, so a new enumerator cannot silently mean something else in the UI.
    createErrors: readNames(exports, bufferOffset, exports.gl_sim_create_error_name),
    commandErrors: readNames(exports, bufferOffset, exports.gl_sim_command_error_name),
    campaignErrors: readNames(exports, bufferOffset, exports.gl_campaign_error_name),
    dialogueErrors: readNames(
      exports,
      bufferOffset,
      exports.gl_campaign_dialogue_error_name
    ),
    sessionErrors: readNames(
      exports,
      bufferOffset,
      exports.gl_campaign_session_error_name
    ),
    rosterErrors: readNames(
      exports,
      bufferOffset,
      exports.gl_campaign_roster_error_name
    ),
    storageErrors: readNames(exports, bufferOffset, exports.gl_storage_error_name),
    saveErrors: readNames(
      exports,
      bufferOffset,
      exports.gl_campaign_save_error_name
    ),
    migrationErrors: readNames(
      exports,
      bufferOffset,
      exports.gl_campaign_migration_error_name
    ),
    outcomeErrors: readNames(
      exports,
      bufferOffset,
      exports.gl_campaign_outcome_error_name
    ),
    operationNames: readNames(
      exports,
      bufferOffset,
      exports.gl_campaign_operation_name,
      1
    )
  };
  return ready;
}

function readNames(
  exports: SimulationExports,
  bufferOffset: number,
  read: (error: number) => number,
  // Where the vocabulary starts. Zero for every error enumeration, because
  // zero is `none`; one for the outcome operation kinds, which have no zero and
  // whose array is padded so a code still indexes its own name.
  start = 0
): readonly string[] {
  const names: string[] = [];
  for (let pad = 0; pad < start; pad += 1) names.push("none");
  const decoder = new TextDecoder();
  for (let code = start; ; code += 1) {
    const length = read(code);
    if (length === 0) break;
    const bytes = new Uint8Array(exports.memory.buffer, bufferOffset, length);
    names.push(decoder.decode(bytes));
  }
  return names;
}

function required(): Engine {
  if (!engine) {
    throw new Error(
      "The simulation engine is not loaded yet; await initEncounterEngine()."
    );
  }
  return engine;
}

function view(active: Engine): DataView {
  return new DataView(
    active.exports.memory.buffer,
    active.bufferOffset,
    active.bufferCapacity
  );
}

function integerIn(value: number, minimum: number, maximum: number): boolean {
  return Number.isInteger(value) && value >= minimum && value <= maximum;
}

function representableId(value: bigint): boolean {
  return typeof value === "bigint" && value >= 0n && value <= UINT64_MAX;
}

/**
 * Maps a source key to the stable content identity the content compiler would
 * assign it.
 *
 * These identifiers are part of canonical state, so the browser must derive
 * them exactly as the compiler does. The mapping is performed by the engine
 * itself rather than reimplemented here.
 */
export function stableContentId(sourceKey: string): bigint {
  const active = required();
  const encoded = new TextEncoder().encode(sourceKey);
  if (encoded.length > active.bufferCapacity) {
    throw new Error(`The source key '${sourceKey}' is too long to identify.`);
  }
  new Uint8Array(
    active.exports.memory.buffer,
    active.bufferOffset,
    active.bufferCapacity
  ).set(encoded);
  return BigInt.asUintN(
    64,
    active.exports.gl_core_stable_content_id(encoded.length)
  );
}

/** One thing the content compiler would not accept, in its own words. */
export interface ContentDiagnostic {
  /** The compiler's own name for the code, read back out of the module. */
  readonly code: string;
  /** The semantic path it names. */
  readonly path: string;
  /** What the source parser added; empty for a semantic diagnostic. */
  readonly detail: string;
}

/** What the content compiler made of a project. */
export type CompiledProject =
  | {
      readonly compiled: true;
      /** The package bytes, byte-identical to what `tools/game_content` writes. */
      readonly package: Uint8Array;
      /** The character style the project names, as a menu index. */
      readonly characterStyle: number;
      /** Every encounter's stable identity, in declaration order. */
      readonly encounterIds: readonly bigint[];
      /** Every campaign's stable identity, in declaration order. */
      readonly campaignIds: readonly bigint[];
    }
  | {
      readonly compiled: false;
      /** Which stage refused: the source parser, or the semantic compiler. */
      readonly stage: "source" | "content";
      readonly diagnostics: readonly ContentDiagnostic[];
    };

const contentStatusCompiled = 0;
const contentStatusSourceRejected = 1;
const contentStatusContentRejected = 2;
const contentStatusSourceTooLarge = 3;

/**
 * Compiles an authored project's canonical source into a compiled package.
 *
 * This is `tools/game_content` itself, the same `parse_source_project_json`
 * and `compile` the command line runs and a Nintendo 64 cartridge runs on the
 * console, rather than a second implementation of it in TypeScript. That is
 * the whole point: a package the browser compiled has to be the package the
 * toolchain would have compiled, byte for byte, or a ROM built from it is not
 * the program this repository checks.
 *
 * The project's source travels as the canonical `project.json` text, which is
 * what `SourceProjectDocument` already encodes for the portable archive.
 */
export function compileProject(source: string): CompiledProject {
  const active = required();
  const encoded = new TextEncoder().encode(source);
  const capacity = active.exports.gl_content_capacity();
  if (encoded.length > capacity) {
    throw new Error(
      `This project's source is ${encoded.length} bytes and the compiler ` +
        `accepts ${capacity}.`
    );
  }
  const offset = active.exports.gl_content_buffer();
  new Uint8Array(active.exports.memory.buffer, offset, capacity).set(encoded);
  const written = active.exports.gl_content_compile(encoded.length);
  if (written === 0) {
    throw new Error("The content compiler produced more than its buffer holds.");
  }

  const view = new DataView(active.exports.memory.buffer, offset, capacity);
  let cursor = 0;
  const status = view.getUint8(cursor);
  cursor += 1;
  const count = view.getUint16(cursor, true);
  cursor += 2;
  if (status === contentStatusSourceTooLarge) {
    throw new Error("The content compiler refused this project's source size.");
  }

  const decoder = new TextDecoder();
  const readString = (): string => {
    const length = view.getUint16(cursor, true);
    cursor += 2;
    const text = decoder.decode(
      new Uint8Array(active.exports.memory.buffer, offset + cursor, length)
    );
    cursor += length;
    return text;
  };

  if (status !== contentStatusCompiled) {
    const diagnostics: ContentDiagnostic[] = [];
    for (let index = 0; index < count; index += 1) {
      diagnostics.push({
        code: readString(),
        path: readString(),
        detail: readString()
      });
    }
    return {
      compiled: false,
      stage: status === contentStatusSourceRejected ? "source" : "content",
      diagnostics
    };
  }

  const characterStyle = view.getUint8(cursor);
  cursor += 1;
  const encounterIds: bigint[] = [];
  const encounterCount = view.getUint16(cursor, true);
  cursor += 2;
  for (let index = 0; index < encounterCount; index += 1) {
    encounterIds.push(view.getBigUint64(cursor, true));
    cursor += 8;
  }
  const campaignIds: bigint[] = [];
  const campaignCount = view.getUint16(cursor, true);
  cursor += 2;
  for (let index = 0; index < campaignCount; index += 1) {
    campaignIds.push(view.getBigUint64(cursor, true));
    cursor += 8;
  }
  const size = view.getUint32(cursor, true);
  cursor += 4;
  // Copied out of linear memory rather than viewed into it: the next call
  // through this boundary overwrites the buffer, and a package that changed
  // underneath its holder would be the worst kind of bug to find.
  const bytes = new Uint8Array(
    active.exports.memory.buffer.slice(offset + cursor, offset + cursor + size)
  );
  return {
    compiled: true,
    package: bytes,
    characterStyle,
    encounterIds,
    campaignIds
  };
}

/**
 * The write half of `Cursor`, so that a payload's size can be asked of the
 * writer that produces it rather than restated beside it.
 */
interface FixedWidthWriter {
  u8(value: number): void;
  u16(value: number): void;
  u32(value: number): void;
  u64(value: bigint): void;
  i16(value: number): void;
}

/**
 * A writer that writes nowhere and counts.
 *
 * Every payload here is fixed-width, so how many bytes a record takes is
 * decided entirely by the code that emits it. A second place stating the same
 * number is a place that goes stale the first time a field is appended.
 * Running the real writer against this is the only way of measuring that cannot
 * disagree with what is then written.
 */
class Tally implements FixedWidthWriter {
  #offset = 0;

  get length(): number {
    return this.#offset;
  }

  u8(): void {
    this.#offset += 1;
  }

  u16(): void {
    this.#offset += 2;
  }

  u32(): void {
    this.#offset += 4;
  }

  u64(): void {
    this.#offset += 8;
  }

  i16(): void {
    this.#offset += 2;
  }
}

/**
 * Fixed-width encoder over the shared buffer. Bounds are enforced on the
 * WebAssembly side as well; this side refuses to emit a value it would have to
 * truncate, because a truncated identifier could silently name a different unit.
 */
class Cursor implements FixedWidthWriter {
  #view: DataView;
  #offset = 0;

  constructor(target: DataView) {
    this.#view = target;
  }

  get length(): number {
    return this.#offset;
  }

  u8(value: number): void {
    this.#view.setUint8(this.#offset, value);
    this.#offset += 1;
  }

  u16(value: number): void {
    this.#view.setUint16(this.#offset, value, true);
    this.#offset += 2;
  }

  u32(value: number): void {
    this.#view.setUint32(this.#offset, value, true);
    this.#offset += 4;
  }

  u64(value: bigint): void {
    this.#view.setBigUint64(this.#offset, value, true);
    this.#offset += 8;
  }

  i16(value: number): void {
    this.#view.setInt16(this.#offset, value, true);
    this.#offset += 2;
  }

  readU8(): number {
    const value = this.#view.getUint8(this.#offset);
    this.#offset += 1;
    return value;
  }

  readU16(): number {
    const value = this.#view.getUint16(this.#offset, true);
    this.#offset += 2;
    return value;
  }

  readU32(): number {
    const value = this.#view.getUint32(this.#offset, true);
    this.#offset += 4;
    return value;
  }

  readU64(): bigint {
    const value = this.#view.getBigUint64(this.#offset, true);
    this.#offset += 8;
    return value;
  }

  readI16(): number {
    const value = this.#view.getInt16(this.#offset, true);
    this.#offset += 2;
    return value;
  }

  bytes(encoded: Uint8Array): void {
    for (let index = 0; index < encoded.length; index += 1) {
      this.#view.setUint8(this.#offset + index, encoded[index]!);
    }
    this.#offset += encoded.length;
  }

  /** A u16 length prefix followed by UTF-8 bytes, the package string shape. */
  string(value: string): void {
    const encoded = new TextEncoder().encode(value);
    this.u16(encoded.length);
    this.bytes(encoded);
  }

  readString(): string {
    const declared = this.readU16();
    // Clamped to this cursor's own window, which is the shared buffer and not
    // the module's whole linear memory. The two are the same `ArrayBuffer`, so
    // a view built against the declared length alone would happily decode
    // whatever sits after the buffer, and a string is where that would land
    // on screen, under somebody's name. Every other read here is bounded by
    // construction; this is the one carrying a length off the wire.
    const length = Math.min(declared, this.#view.byteLength - this.#offset);
    const bytes = new Uint8Array(
      this.#view.buffer,
      this.#view.byteOffset + this.#offset,
      Math.max(0, length)
    );
    this.#offset += declared;
    return new TextDecoder().decode(bytes);
  }
}

/**
 * Creates an encounter on the authoritative engine.
 *
 * Rule validation belongs to the engine. The checks here only reject values
 * that cannot cross the fixed-width boundary at all, and report them with the
 * error the engine itself would have produced for an out-of-domain value.
 */
export function createEncounter(definition: EncounterDefinition): CreateResult {
  const active = required();

  if (
    !integerIn(definition.width, 0, UINT16_MAX) ||
    !integerIn(definition.height, 0, UINT16_MAX)
  ) {
    return { error: "invalid_map" };
  }
  for (const unit of definition.units) {
    if (
      !representableId(unit.id) ||
      !representableId(unit.unitTypeId) ||
      !sides.includes(unit.side) ||
      !integerIn(unit.position.x, INT16_MIN, INT16_MAX) ||
      !integerIn(unit.position.y, INT16_MIN, INT16_MAX) ||
      !integerIn(unit.health, INT16_MIN, INT16_MAX) ||
      !integerIn(unit.strength, INT16_MIN, INT16_MAX) ||
      !integerIn(unit.power ?? 0, INT16_MIN, INT16_MAX) ||
      !integerIn(unit.defense, INT16_MIN, INT16_MAX)
    ) {
      return { error: "invalid_unit" };
    }
  }
  // A board too big for the shared buffer is refused rather than written.
  //
  // The size comes from the writer itself, and it has to: this call arrives
  // from a click, an over-long payload throws out of the `DataView` rather than
  // returning, and a budget stated here in numbers would answer for the record
  // as it was the day somebody wrote them down. The whole project's abilities,
  // weapons, items and objectives ride on every encounter, so they count too.
  const terrain = definition.terrain ?? [];
  if (encounterDefinitionSize(definition) > active.bufferCapacity) {
    return { error: "invalid_unit" };
  }
  if (terrain.length !== 0 && terrain.length !== definition.width * definition.height) {
    return { error: "invalid_map" };
  }
  const movementCost = definition.movementCost ?? [];
  if (
    movementCost.length !== 0 &&
    movementCost.length !== definition.width * definition.height
  ) {
    return { error: "invalid_map" };
  }

  const cursor = new Cursor(view(active));
  writeEncounterDefinition(cursor, definition);

  const handle = active.exports.gl_sim_create(cursor.length);
  if (handle === 0) {
    const result = new Cursor(view(active));
    const status = result.readU8();
    const code = result.readU8();
    if (status !== abiOk) {
      throw new Error(
        `The simulation rejected an encounter payload (status ${status}).`
      );
    }
    return { error: createErrorName(active, code) as Exclude<CreateError, "none"> };
  }
  return { error: "none", encounter: new WasmEncounter(active, handle) };
}

/**
 * Writes one encounter definition in the wire order `platform/web/README.md`
 * documents.
 *
 * Factored out rather than inlined because a campaign session sends the very
 * same board. The C++ side reads both with one parser, and two writers of one
 * format is the drift the whole fixed-width discipline exists to prevent.
 */
function writeEncounterDefinition(
  cursor: FixedWidthWriter,
  definition: EncounterDefinition
): void {
  const terrain = definition.terrain ?? [];
  cursor.u16(definition.width);
  cursor.u16(definition.height);
  cursor.u32(definition.units.length);
  for (const unit of definition.units) {
    cursor.u64(unit.id);
    cursor.u64(unit.unitTypeId);
    cursor.u8(unit.side === "first" ? 0 : 1);
    cursor.i16(unit.position.x);
    cursor.i16(unit.position.y);
    cursor.i16(unit.health);
    cursor.i16(unit.strength);
    cursor.i16(unit.power ?? 0);
    cursor.i16(unit.defense);
    cursor.i16(unit.resistance ?? 0);
    cursor.i16(unit.skill ?? 0);
    cursor.i16(unit.luck ?? 0);
    cursor.i16(unit.evasion ?? 0);
    cursor.i16(unit.magic ?? 0);
    cursor.u8(unit.movement ?? 1);
    cursor.u8(unit.actionPoints ?? 1);
    cursor.u8(unit.speed ?? 1);
    cursor.u8(unit.actsAfterAttacking ? 1 : 0);
    cursor.u8(unit.minimumReach ?? 1);
    cursor.u8(unit.maximumReach ?? 1);
    const abilities = unit.abilityIds ?? [];
    cursor.u32(abilities.length);
    for (const ability of abilities) cursor.u64(ability);
    const carried = unit.weaponIds ?? [];
    cursor.u32(carried.length);
    for (const weapon of carried) cursor.u64(weapon);
    cursor.u8(
      (unit.crossings ?? []).reduce(
        (bits, crossing) => bits | (crossingBits[crossing] ?? 0),
        0
      )
    );
    cursor.u8(unit.accuracy ?? 100);
    const pack = unit.items ?? [];
    cursor.u32(pack.length);
    for (const slot of pack) {
      cursor.u64(slot.id);
      cursor.u16(slot.count);
    }
    cursor.u64(unit.dropItemId ?? 0n);
    cursor.u8(unit.dropChance ?? 0);
    cursor.u8(unit.reachBonus ?? 0);
    // What talking to this character records, at the tail. The zero is written
    // rather than left off, on this format's standing convention: a caller with
    // nobody to talk to and a caller whose payload was truncated must not be
    // the same bytes.
    cursor.u64(unit.talkRecordId ?? 0n);
    // And when this character comes in, on the same terms: the round of its
    // first arrival, the rounds between arrivals and how many it makes. Three
    // zeroes is somebody standing on the board from the opening. Narrow on the
    // wire because these carry what an author wrote, and the source contract
    // bounds a round at 4095, a gap at 255 and a count of arrivals at 64.
    cursor.u16(unit.arrivalRound ?? 0);
    cursor.u16(unit.arrivalEvery ?? 0);
    cursor.u8(unit.arrivalTimes ?? 0);
  }
  const abilities = definition.abilities ?? [];
  cursor.u32(abilities.length);
  for (const ability of abilities) {
    cursor.u64(ability.id);
    cursor.u8(abilityKinds.indexOf(ability.kind));
    cursor.u8(damageTypes.indexOf(ability.damageType));
    cursor.u8(areaShapes.indexOf(ability.area));
    cursor.i16(ability.power);
    cursor.u8(ability.minimumReach);
    cursor.u8(ability.maximumReach);
    cursor.u8(ability.radius);
    cursor.u8(ability.accuracy ?? 100);
  }
  const weapons = definition.weapons ?? [];
  cursor.u32(weapons.length);
  for (const weapon of weapons) {
    cursor.u64(weapon.id);
    cursor.i16(weapon.power);
    cursor.u8(weapon.minimumReach);
    cursor.u8(weapon.maximumReach);
    cursor.u8(weapon.accuracy ?? 100);
    cursor.u64(weapon.weaponType ?? 0n);
  }
  // Which kinds of weapon beat which, and what beating them is worth. A board
  // with no triangle in it writes a count of zero.
  const weaponTypes = definition.weaponTypes ?? [];
  cursor.u32(weaponTypes.length);
  for (const kind of weaponTypes) {
    cursor.u64(kind.id);
    cursor.u32(kind.strongAgainst.length);
    for (const beaten of kind.strongAgainst) cursor.u64(beaten);
    cursor.i16(kind.damage);
    cursor.u8(kind.accuracy);
  }
  const items = definition.items ?? [];
  cursor.u32(items.length);
  for (const item of items) {
    cursor.u64(item.id);
    cursor.u8(Math.max(0, itemKinds.indexOf(item.kind)));
    cursor.i16(item.power);
  }
  const objectives = definition.objectives ?? [];
  cursor.u32(objectives.length);
  for (const objective of objectives) {
    cursor.u64(objective.id);
    cursor.u8(objectiveKinds.indexOf(objective.kind));
    cursor.u8(objective.side === "first" ? 0 : 1);
    cursor.u64(objective.targetUnitId ?? 0n);
    // The count, at the tail and written for every kind. Zero is an objective
    // that reads no round, which is every kind but the last; the engine refuses
    // the halves, so nothing here has to.
    cursor.u32(objective.roundCount ?? 0);
  }
  cursor.u8(Math.max(0, turnOrders.indexOf(definition.turnOrder ?? "alternating")));
  cursor.u32(terrain.length);
  for (const cell of terrain) cursor.u8(Math.max(0, terrains.indexOf(cell)));
  // What the ground charges, counted the same way and read by the same parser.
  // A count of zero is a board where every step costs one, which is what a
  // caller with no price to give sends.
  const movementCost = definition.movementCost ?? [];
  cursor.u32(movementCost.length);
  for (const cell of movementCost) cursor.u8(cell);
  // The deployment region, counted rather than optional: a campaign session
  // appends its own placement table after this record, so an absent tail and a
  // present one would be indistinguishable there. A count of zero is an
  // encounter with no phase.
  const zone = definition.deploymentTiles ?? [];
  cursor.u32(zone.length);
  for (const tile of zone) {
    cursor.i16(tile.x);
    cursor.i16(tile.y);
  }
}

/**
 * How many bytes `writeEncounterDefinition` will emit for this board.
 *
 * Measured by writing it, not by adding up a table of field widths. A record
 * gains a field about twice a year and every restatement of its size is a
 * separate thing to remember; this one cannot be wrong, because it is the same
 * code path that produces the bytes.
 */
function encounterDefinitionSize(definition: EncounterDefinition): number {
  const tally = new Tally();
  writeEncounterDefinition(tally, definition);
  return tally.length;
}

function createErrorName(active: Engine, code: number): CreateError {
  return (active.createErrors[code] ?? "invalid_unit") as CreateError;
}

function commandErrorName(active: Engine, code: number): CommandError {
  return (active.commandErrors[code] ?? "invalid_command") as CommandError;
}

// Reclaims handles for encounters that were dropped without dispose(). The
// engine's handle table is small and reused, so a leaked handle is a slow leak
// rather than a correctness problem, but it is cheap to close.
const finalizers =
  typeof FinalizationRegistry === "function"
    ? new FinalizationRegistry<{ engine: Engine; handle: number }>((held) => {
        held.engine.exports.gl_sim_destroy(held.handle);
      })
    : undefined;

class WasmEncounter implements Encounter {
  readonly #engine: Engine;
  #handle: number;
  // Unregistration token. Held weakly by the registry, and deliberately not
  // the encounter itself, which would keep it alive forever.
  readonly #token: object = {};
  // Whether releasing this wrapper releases the engine's handle. False for the
  // battle a campaign session is holding: the session opened it, the session
  // closes it, and a wrapper that closed it early would pull the board out from
  // under the commit that still has to read its events.
  readonly #owned: boolean;

  constructor(active: Engine, handle: number, owned = true) {
    this.#engine = active;
    this.#handle = handle;
    this.#owned = owned;
    if (owned) {
      finalizers?.register(this, { engine: active, handle }, this.#token);
    }
  }

  apply(command: Command): CommandResult {
    const active = this.#live();
    // A begin names no character at all: it is a statement about the battle.
    if (command.type !== "begin_battle" && !representableId(command.unitId)) {
      return { error: "unknown_unit", events: [] };
    }
    if (
      (command.type === "attack" || command.type === "talk") &&
      !representableId(command.targetId)
    ) {
      return { error: "unknown_target", events: [] };
    }
    if (
      (command.type === "move" || command.type === "ability") &&
      (!integerIn(command.destination.x, INT16_MIN, INT16_MAX) ||
        !integerIn(command.destination.y, INT16_MIN, INT16_MAX))
    ) {
      return { error: "invalid_destination", events: [] };
    }
    if (command.type === "ability" && !representableId(command.abilityId)) {
      return { error: "unknown_ability", events: [] };
    }
    if (
      command.type === "attack" &&
      command.weaponId !== undefined &&
      !representableId(command.weaponId)
    ) {
      return { error: "unknown_weapon", events: [] };
    }
    if (command.type === "use_item" && !representableId(command.itemId)) {
      return { error: "unknown_item", events: [] };
    }
    if (
      command.type === "use_item" &&
      command.targetId !== undefined &&
      !representableId(command.targetId)
    ) {
      return { error: "unknown_target", events: [] };
    }

    if (
      command.type === "deploy" &&
      (!integerIn(command.destination.x, INT16_MIN, INT16_MAX) ||
        !integerIn(command.destination.y, INT16_MIN, INT16_MAX))
    ) {
      return { error: "invalid_destination", events: [] };
    }

    const cursor = new Cursor(view(active));
    // An unrecognised discriminator is sent as an out-of-range byte so that the
    // engine, not this binding, decides the error and its precedence against
    // the unit, health, and side checks that come first.
    cursor.u8(commandDiscriminators[command.type] ?? 0xff);
    cursor.u64(command.type === "begin_battle" ? 0n : command.unitId);
    const targeted =
      command.type === "move" ||
      command.type === "ability" ||
      command.type === "deploy";
    cursor.i16(targeted ? command.destination.x : 0);
    cursor.i16(targeted ? command.destination.y : 0);
    cursor.u64(
      command.type === "attack" || command.type === "talk"
        ? command.targetId
        : command.type === "use_item"
          ? (command.targetId ?? 0n)
          : 0n
    );
    cursor.u64(command.type === "ability" ? command.abilityId : 0n);
    cursor.u64(command.type === "attack" ? (command.weaponId ?? 0n) : 0n);
    cursor.u64(command.type === "use_item" ? command.itemId : 0n);

    const written = active.exports.gl_sim_apply(this.#handle, cursor.length);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (status === abiEventLogFull) {
      throw new Error(
        "This battle has emitted more events than a campaign session keeps, " +
        "so the engine will not take another command on it."
      );
    }
    if (written === 0 || status !== abiOk) {
      throw new Error(`The simulation rejected a command payload (status ${status}).`);
    }
    const error = commandErrorName(active, result.readU8());
    const count = result.readU32();
    const events: SimulationEvent[] = [];
    for (let index = 0; index < count; index += 1) {
      events.push(readEvent(result));
    }
    return { error, events };
  }

  forecastItem(
    unitId: bigint,
    itemId: bigint,
    targetId: bigint = 0n
  ): ItemForecast {
    const active = this.#live();
    const refused = {
      kind: "none" as ItemKind,
      restored: 0,
      targetHealthAfter: 0,
      remainingAfter: 0
    };
    if (!representableId(unitId)) return { error: "unknown_unit", ...refused };
    if (!representableId(itemId)) return { error: "unknown_item", ...refused };
    if (!representableId(targetId)) {
      return { error: "unknown_target", ...refused };
    }
    const written = active.exports.gl_sim_forecast_item(
      this.#handle,
      unitId,
      targetId,
      itemId
    );
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The simulation rejected a forecast request (status ${status}).`
      );
    }
    const error = commandErrorName(active, result.readU8());
    const kind = itemKinds[result.readU8()] ?? "none";
    const restored = result.readI16();
    const targetHealthAfter = result.readI16();
    const remainingAfter = result.readU16();
    return { error, kind, restored, targetHealthAfter, remainingAfter };
  }

  forecastTalk(unitId: bigint, targetId: bigint): TalkForecast {
    const active = this.#live();
    // A refused talk describes no consequence, so both identities stay zero on
    // every path out of here. That is the boundary's own promise, kept on this
    // side of it too rather than left to a caller to notice.
    const refused = { departingId: 0n, recordId: 0n };
    if (!representableId(unitId)) return { error: "unknown_unit", ...refused };
    if (!representableId(targetId)) {
      return { error: "unknown_target", ...refused };
    }
    const written = active.exports.gl_sim_forecast_talk(
      this.#handle,
      unitId,
      targetId
    );
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The simulation rejected a forecast request (status ${status}).`
      );
    }
    const error = commandErrorName(active, result.readU8());
    const departingId = result.readU64();
    const recordId = result.readU64();
    return { error, departingId, recordId };
  }

  forecastAttack(
    attackerId: bigint,
    targetId: bigint,
    weaponId: bigint = 0n
  ): AttackForecast {
    const active = this.#live();
    const refused = {
      hitChance: 100,
      counterChance: 100,
      damage: 0,
      targetHealthAfter: 0,
      lethal: false,
      counter: false,
      counterDamage: 0,
      attackerHealthAfter: 0,
      counterLethal: false
    };
    if (!representableId(attackerId)) {
      return { error: "unknown_unit", ...refused };
    }
    if (!representableId(targetId)) {
      return { error: "unknown_target", ...refused };
    }
    if (!representableId(weaponId)) {
      return { error: "unknown_weapon", ...refused };
    }
    const written = active.exports.gl_sim_forecast_attack(
      this.#handle,
      attackerId,
      targetId,
      weaponId
    );
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(`The simulation could not forecast an attack (status ${status}).`);
    }
    const error = commandErrorName(active, result.readU8());
    const damage = result.readI16();
    const targetHealthAfter = result.readI16();
    const lethal = result.readU8() !== 0;
    const counter = result.readU8() !== 0;
    const counterDamage = result.readI16();
    const attackerHealthAfter = result.readI16();
    const counterLethal = result.readU8() !== 0;
    return {
      error,
      hitChance: result.readU8(),
      damage,
      targetHealthAfter,
      lethal,
      counter,
      counterChance: result.readU8(),
      counterDamage,
      attackerHealthAfter,
      counterLethal
    };
  }

  reachableTiles(unitId: bigint): Position[] {
    // An identifier the boundary cannot carry names no unit, which reaches
    // nothing, the same answer the engine gives for an unknown unit.
    if (!representableId(unitId)) return [];
    const active = this.#live();
    return this.#tiles(
      active,
      active.exports.gl_sim_reachable_tiles(this.#handle, unitId),
      "reach"
    );
  }

  deployableTiles(unitId: bigint): Position[] {
    if (!representableId(unitId)) return [];
    const active = this.#live();
    return this.#tiles(
      active,
      active.exports.gl_sim_deployable_tiles(this.#handle, unitId),
      "deployable"
    );
  }

  dangerTiles(side: Side): Position[] {
    const active = this.#live();
    return this.#tiles(
      active,
      active.exports.gl_sim_danger_tiles(this.#handle, side === "first" ? 0 : 1),
      "danger"
    );
  }

  aimableTiles(unitId: bigint, gesture: AimedGesture): Position[] {
    // An identifier the boundary cannot carry names nothing, and a gesture
    // naming nothing is aimed nowhere, the same answer the engine gives for a
    // weapon or a spell it cannot resolve.
    if (!representableId(unitId)) return [];
    const aim = gestureIdentities(gesture);
    if (!aim) return [];
    const active = this.#live();
    return this.#tiles(
      active,
      active.exports.gl_sim_aimable_tiles(
        this.#handle,
        unitId,
        gestureCodes[gesture.kind],
        aim.weaponId,
        aim.abilityId
      ),
      "aimable"
    );
  }

  gestureAvailable(unitId: bigint, gesture: AimedGesture): boolean {
    if (!representableId(unitId)) return false;
    const aim = gestureIdentities(gesture);
    if (!aim) return false;
    const active = this.#live();
    const written = active.exports.gl_sim_gesture_available(
      this.#handle,
      unitId,
      gestureCodes[gesture.kind],
      aim.weaponId,
      aim.abilityId
    );
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The simulation could not judge a gesture (status ${status}).`
      );
    }
    return result.readU8() !== 0;
  }

  areaTiles(abilityId: bigint, centre: Position): Position[] {
    // An identity the boundary cannot carry names no ability, and a centre no
    // board coordinate could hold names no tile. Both answer empty here rather
    // than being sent: the first would arrive as some other identity and be
    // answered about, and the second is what the boundary refuses as
    // malformed. Empty is the same answer the engine gives for an ability it
    // cannot resolve, which is what both of these are.
    if (!representableId(abilityId)) return [];
    if (
      !integerIn(centre.x, INT16_MIN, INT16_MAX) ||
      !integerIn(centre.y, INT16_MIN, INT16_MAX)
    ) {
      return [];
    }
    const active = this.#live();
    return this.#tiles(
      active,
      active.exports.gl_sim_area_tiles(
        this.#handle,
        abilityId,
        centre.x,
        centre.y
      ),
      "area"
    );
  }

  /** Reads the tile-list record every spatial query writes. */
  #tiles(active: Engine, written: number, what: string): Position[] {
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The simulation could not list ${what} tiles (status ${status}).`
      );
    }
    const count = result.readU32();
    const tiles: Position[] = [];
    for (let index = 0; index < count; index += 1) {
      tiles.push({ x: result.readI16(), y: result.readI16() });
    }
    return tiles;
  }

  snapshot(): EncounterSnapshot {
    const active = this.#live();
    const written = active.exports.gl_sim_snapshot(this.#handle);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(`The simulation could not produce a snapshot (status ${status}).`);
    }
    const width = result.readU16();
    const height = result.readU16();
    const activeSide = sides[result.readU8()] ?? "first";
    const outcome = outcomes[result.readU8()] ?? "ongoing";
    const activeUnitId = result.readU64();
    const remainingActionPoints = result.readU8();
    const round = result.readU32();
    const activationCount = result.readU64();
    const count = result.readU32();
    const units: UnitSnapshot[] = [];
    for (let index = 0; index < count; index += 1) {
      const id = result.readU64();
      const unitTypeId = result.readU64();
      const side = sides[result.readU8()] ?? "first";
      const x = result.readI16();
      const y = result.readI16();
      const health = result.readI16();
      const maximumHealth = result.readI16();
      const strength = result.readI16();
      const power = result.readI16();
      const defense = result.readI16();
      const resistance = result.readI16();
      const skill = result.readI16();
      const luck = result.readI16();
      const evasion = result.readI16();
      const magic = result.readI16();
      const movement = result.readU8();
      const actionPoints = result.readU8();
      const speed = result.readU8();
      const actsAfterAttacking = result.readU8() !== 0;
      const hasActed = result.readU8() !== 0;
      const minimumReach = result.readU8();
      const maximumReach = result.readU8();
      const abilityCount = result.readU32();
      const abilityIds: bigint[] = [];
      for (let slot = 0; slot < abilityCount; slot += 1) {
        abilityIds.push(result.readU64());
      }
      const weaponCount = result.readU32();
      const weaponIds: bigint[] = [];
      for (let slot = 0; slot < weaponCount; slot += 1) {
        weaponIds.push(result.readU64());
      }
      const packCount = result.readU32();
      const items: CarriedItem[] = [];
      for (let slot = 0; slot < packCount; slot += 1) {
        items.push({ id: result.readU64(), count: result.readU16() });
      }
      const dropItemId = result.readU64();
      const dropChance = result.readU8();
      const reachBonus = result.readU8();
      const talkRecordId = result.readU64();
      const departed = result.readU8() !== 0;
      const arrivalRound = result.readU32();
      const arrived = result.readU8() !== 0;
      const hasMoved = result.readU8() !== 0;
      const spentActionPoints = result.readU8();
      const onBoard = result.readU8() !== 0;
      units.push({
        id,
        unitTypeId,
        side,
        position: { x, y },
        health,
        maximumHealth,
        strength,
        power,
        defense,
        resistance,
        skill,
        luck,
        evasion,
        magic,
        movement,
        actionPoints,
        speed,
        actsAfterAttacking,
        hasActed,
        minimumReach,
        maximumReach,
        abilityIds,
        weaponIds,
        items,
        dropItemId,
        dropChance,
        reachBonus,
        talkRecordId,
        departed,
        arrivalRound,
        arrived,
        hasMoved,
        spentActionPoints,
        onBoard
      });
    }
    const objectiveCount = result.readU32();
    const objectives: ObjectiveResult[] = [];
    for (let index = 0; index < objectiveCount; index += 1) {
      const id = result.readU64();
      const state = objectiveStates[result.readU8()] ?? "pending";
      objectives.push({ id, state });
    }
    const dropCount = result.readU32();
    const drops: DropRecord[] = [];
    for (let index = 0; index < dropCount; index += 1) {
      drops.push({
        unitId: result.readU64(),
        claimantId: result.readU64(),
        itemId: result.readU64()
      });
    }
    const deploying = result.readU8() !== 0;
    const zoneCount = result.readU32();
    const deploymentTiles: Position[] = [];
    for (let index = 0; index < zoneCount; index += 1) {
      deploymentTiles.push({ x: result.readI16(), y: result.readI16() });
    }
    return {
      width,
      height,
      activeSide,
      activeUnitId,
      remainingActionPoints,
      round,
      activationCount,
      outcome,
      units,
      objectives,
      drops,
      deploymentTiles,
      deploying
    };
  }

  decide(
    unitId: bigint,
    behavior: UnitBehavior,
    patrol: readonly Position[] = []
  ): Command | undefined {
    const active = this.#live();
    const cursor = new Cursor(view(active));
    cursor.u64(unitId);
    cursor.u8(Math.max(0, behaviors.indexOf(behavior)));
    cursor.u16(patrol.length);
    for (const point of patrol) {
      cursor.i16(point.x);
      cursor.i16(point.y);
    }
    const written = active.exports.gl_ai_decide(this.#handle, cursor.length);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(`The simulation could not plan a turn (status ${status}).`);
    }
    if (result.readU8() === 0) return undefined;
    const type = result.readU8();
    const actor = result.readU64();
    const destination = { x: result.readI16(), y: result.readI16() };
    const targetId = result.readU64();
    const abilityId = result.readU64();
    const weaponId = result.readU64();
    switch (type) {
      case 0:
        return { type: "move", unitId: actor, destination };
      case 1:
        return weaponId === 0n
          ? { type: "attack", unitId: actor, targetId }
          : { type: "attack", unitId: actor, targetId, weaponId };
      case 3:
        return { type: "ability", unitId: actor, abilityId, destination };
      default:
        return { type: "wait", unitId: actor };
    }
  }

  canonicalHash(): bigint {
    const active = this.#live();
    // WebAssembly i64 results arrive as signed BigInts. The canonical hash is
    // unsigned, and its top bit is set for the reference vector, so this
    // reinterpretation is what makes the browser value equal the native one.
    return BigInt.asUintN(64, active.exports.gl_sim_canonical_hash(this.#handle));
  }

  dispose(): void {
    if (this.#handle === 0) return;
    if (this.#owned) {
      this.#engine.exports.gl_sim_destroy(this.#handle);
      finalizers?.unregister(this.#token);
    }
    this.#handle = 0;
  }

  #live(): Engine {
    if (this.#handle === 0) {
      throw new Error("This encounter has been disposed.");
    }
    return this.#engine;
  }
}

function readEvent(cursor: Cursor): SimulationEvent {
  const type = eventTypes[cursor.readU8()] ?? "unit_waited";
  const unitId = cursor.readU64();
  const relatedUnitId = cursor.readU64();
  const position = { x: cursor.readI16(), y: cursor.readI16() };
  const amount = cursor.readI16();
  const outcome = outcomes[cursor.readU8()] ?? "ongoing";
  // The content identity the event is about: an item for a use or a drop, a
  // talk record for a departure. One field, because the event's own type says
  // which namespace to read it in.
  const itemId = cursor.readU64();
  switch (type) {
    case "unit_moved":
      return { type, unitId, position };
    case "unit_waited":
      return { type, unitId, position };
    case "unit_damaged":
      return { type, unitId, relatedUnitId, position, amount };
    case "unit_defeated":
      return { type, unitId, relatedUnitId, position };
    case "unit_restored":
      return { type, unitId, relatedUnitId, position, amount };
    case "activation_ended":
      return { type, unitId, position };
    case "attack_missed":
      return { type, unitId, relatedUnitId, position };
    case "item_used":
      return { type, unitId, relatedUnitId, position, itemId, amount };
    case "item_dropped":
      return { type, unitId, relatedUnitId, position, itemId, amount };
    case "unit_deployed":
      return { type, unitId, position };
    case "deployment_ended":
      return { type, unitId, position };
    case "unit_talked":
      return { type, unitId, relatedUnitId, position, recordId: itemId };
    case "unit_arrived":
      return { type, unitId, position, amount };
    case "unit_endured":
      return { type, unitId, relatedUnitId, position };
    case "encounter_completed":
      return {
        type,
        unitId,
        position,
        outcome: (outcome === "ongoing"
          ? "first_side_won"
          : outcome) as Exclude<Outcome, "ongoing">
      };
  }
}

// --- Campaign flow ----------------------------------------------------------
//
// Campaign flow is a rule surface exactly like combat: which branch follows a
// battle, when a scene plays, which ending is reached. The bindings below hand
// the compiled campaign-record and dialogue-record encodings, the same bytes
// tools/game_content emits, to package_runtime's loader, cursor, and dialogue
// decoder inside the module, so flow decisions are never re-derived here.

export type CampaignError =
  | "none"
  | "missing_section"
  | "missing_record"
  | "malformed_payload"
  | "missing_reference"
  | "unsupported_flow"
  | "already_complete"
  | "outcome_incomplete";

export type DialogueError =
  | "none"
  | "missing_section"
  | "missing_record"
  | "malformed_payload";

export type CampaignNodeKind = "encounter" | "terminal" | "story";
export type CampaignCombinator = "all" | "any" | "none";
export type CampaignPredicateResult = "satisfied" | "failed";

/**
 * One question a transition asks. Two kinds, mirroring the package record: an
 * objective's result, and a world flag's value.
 *
 * A world flag is the only campaign state a battle can raise by itself, and a
 * talk raises it. So it is what an edge reads when the road forks on something
 * the player did rather than on whether they won.
 */
export type CampaignPredicateDefinition =
  | { objectiveId: bigint; result: CampaignPredicateResult }
  | {
      flagId: bigint;
      /** `campaign::WorldValueType`: 1 boolean, 2 integer. */
      valueType: 1 | 2;
      value: bigint;
    };

export interface CampaignBranchDefinition {
  targetNodeId: bigint;
  priority: number;
  combinator: CampaignCombinator;
  predicates: readonly CampaignPredicateDefinition[];
}

export interface CampaignNodeDefinition {
  id: bigint;
  kind: CampaignNodeKind;
  encounterId?: bigint;
  /** Presented in authored order when the node is entered. */
  dialogueIds?: readonly bigint[];
  unconditionalTargetIds?: readonly bigint[];
  branches?: readonly CampaignBranchDefinition[];
}

export interface DialogueDefinition {
  id: bigint;
  name: string;
  /**
   * `castEntry` is which of `cast` speaks this line, plus one; 0 or undefined
   * is a line the scene named nobody for. Resolved before it gets here, as the
   * content compiler resolves it: the join between a line's speaker string and
   * the cast is made once, by whoever read the source, and never by a client.
   */
  lines: readonly { speaker: string; text: string; castEntry?: number }[];
  /**
   * The unit type identity of each speaker this scene cast, in authored order
   * and at most 255 of them, so that a line can name its speaker in one byte.
   * Empty or absent for a scene that cast nobody.
   */
  cast?: readonly bigint[];
  /**
   * The art library's backdrop menu index plus one, or 0/undefined for a
   * scene that names none. Encoded exactly as `tools/game_content` encodes it:
   * one byte after the lines, and no byte at all when there is no backdrop, so
   * a scene without one produces the record it produced before backdrops
   * existed.
   */
  backdrop?: number;
}

/**
 * One authored member of the company, exactly as the campaign record carries
 * them. `joinNodeId` is 0 for a member the campaign is founded with, and
 * otherwise the node whose completion brings them in.
 */
export interface CampaignMemberDefinition {
  id: bigint;
  name: string;
  unitTypeId: bigint;
  joinNodeId?: bigint;
}

/**
 * One quantity of one item or one weapon the campaign puts in its own store by
 * authoring, exactly as the campaign record carries it.
 *
 * `joinNodeId` is 0 for the founding stock and otherwise the node whose
 * completion grants it, on the same convention a founding member carries. A
 * grant is an event rather than an assertion about the store: passing the node
 * a second time grants a second time.
 *
 * Exactly one of `itemId` and `weaponId` is set, which is the shape the source
 * has and the shape the package writes: two identity fields with one of them
 * zero, rather than one field and a tag saying how to read it. A grant written
 * before a weapon could be granted sets `itemId` alone and means what it
 * always meant.
 */
export interface CampaignItemGrantDefinition {
  itemId?: bigint;
  weaponId?: bigint;
  quantity: number;
  joinNodeId?: bigint;
}

export interface CampaignFlowDefinition {
  id: bigint;
  name: string;
  entryNodeId: bigint;
  nodes: readonly CampaignNodeDefinition[];
  dialogues?: readonly DialogueDefinition[];
  /**
   * The founding members in authored order, then each node's recruits in flow
   * order. The session assigns one-based persistent identities in exactly this
   * order, so it is content rather than a detail.
   */
  members?: readonly CampaignMemberDefinition[];
  /**
   * The founding stock in authored order, then each node's grants in flow
   * order. One table, because the founding and a node's completion are the
   * same operation at two moments.
   */
  grants?: readonly CampaignItemGrantDefinition[];
  /**
   * What this campaign does with a character who falls. Omitted means
   * `permanent`, which is what every campaign meant before a project could say
   * otherwise, and a campaign that says nothing encodes to the bytes it always
   * encoded to.
   *
   * The compiler resolves it out of the project, so a browser session running
   * uncompiled source has to resolve it the same way. This is where the answer
   * arrives, and `encodeCampaignRecord` writes it into the same tail the
   * compiler writes it into.
   */
  characterLoss?: CharacterLoss;
  /**
   * Whether this campaign's company cannot be reduced below one health. A
   * testing aid rather than a way to play; see `characterLoss` for why it
   * travels the same road, and `package_runtime::CampaignDefinition` for why it
   * is declared in the package at all rather than switched on by a client.
   */
  invulnerableForTesting?: boolean;
}

export interface CampaignNodeState {
  complete: boolean;
  nodeId: bigint;
  kind: CampaignNodeKind;
  encounterId: bigint;
  dialogueIds: readonly bigint[];
}

export interface CampaignDialogue {
  id: bigint;
  name: string;
  /**
   * `castEntry` is which of `cast` speaks this line, plus one; zero is a line
   * the scene named nobody for. Resolved by the compiler, so nothing here ever
   * matches a speaker string against a table.
   */
  lines: readonly { speaker: string; text: string; castEntry: number }[];
  /**
   * The unit type identity of each speaker this scene cast, in authored order.
   * Empty for a scene that cast nobody, which is every scene authored before a
   * scene could.
   */
  cast: readonly bigint[];
  /**
   * What the scene is drawn against: the art library's backdrop menu index
   * plus one, or 0 for a scene that names none. Carried as the engine's own
   * number rather than as a name because the engine holds no names. The menu
   * lives in the generated board art, which is where it is resolved.
   */
  backdrop: number;
}

export type DialogueResult =
  | { error: "none"; dialogue: CampaignDialogue }
  | { error: Exclude<DialogueError, "none"> };

export interface Campaign {
  /** The cursor's current node, exactly as the engine sees it. */
  state(): CampaignNodeState;
  /**
   * Advances past the current encounter node from its outcome and the
   * engine-reported objective results. Branch predicates are evaluated by the
   * engine alone.
   */
  advanceAfter(
    outcome: Exclude<Outcome, "ongoing">,
    objectives: readonly ObjectiveResult[]
  ): CampaignError;
  /** Advances past a story node, which has no outcome to evaluate. */
  advanceStory(): CampaignError;
  /** Decodes one attached dialogue with the engine's dialogue loader. */
  dialogue(id: bigint): DialogueResult;
  /** Releases the campaign held by the engine. Safe to call more than once. */
  dispose(): void;
}

export type CampaignCreateResult =
  | { error: "none"; campaign: Campaign }
  | { error: Exclude<CampaignError, "none"> };

const campaignNodeKinds: Readonly<Record<number, CampaignNodeKind>> = {
  1: "encounter",
  2: "terminal",
  3: "story"
};
const campaignNodeKindCodes: Readonly<Record<CampaignNodeKind, number>> = {
  encounter: 1,
  terminal: 2,
  story: 3
};
const combinatorCodes: Readonly<Record<CampaignCombinator, number>> = {
  all: 1,
  any: 2,
  none: 3
};
const predicateResultCodes: Readonly<Record<CampaignPredicateResult, number>> = {
  satisfied: 1,
  failed: 2
};
/** `package_runtime::CampaignPredicateKind::world_flag_equals`. */
const worldFlagPredicateCode = 3;

function campaignErrorName(active: Engine, code: number): CampaignError {
  return (active.campaignErrors[code] ?? "malformed_payload") as CampaignError;
}

function dialogueErrorName(active: Engine, code: number): DialogueError {
  return (active.dialogueErrors[code] ?? "malformed_payload") as DialogueError;
}

/**
 * Growable little-endian encoder for record payloads that are sized before
 * they are copied into the shared buffer.
 */
class ByteWriter {
  #bytes: number[] = [];

  get length(): number {
    return this.#bytes.length;
  }

  u8(value: number): void {
    this.#bytes.push(value & 0xff);
  }

  u16(value: number): void {
    this.u8(value);
    this.u8(value >>> 8);
  }

  u32(value: number): void {
    this.u16(value);
    this.u16(value >>> 16);
  }

  u64(value: bigint): void {
    for (let shift = 0n; shift < 64n; shift += 8n) {
      this.u8(Number((value >> shift) & 0xffn));
    }
  }

  string(value: string): void {
    const encoded = new TextEncoder().encode(value);
    if (encoded.length > UINT16_MAX) {
      throw new Error("A package string cannot exceed 65535 bytes.");
    }
    this.u16(encoded.length);
    for (const byte of encoded) this.#bytes.push(byte);
  }

  toBytes(): Uint8Array {
    return Uint8Array.from(this.#bytes);
  }
}

/** The campaign record payload, exactly as tools/game_content encodes it. */
function encodeCampaignRecord(definition: CampaignFlowDefinition): Uint8Array {
  const writer = new ByteWriter();
  writer.string(definition.name);
  writer.u64(definition.entryNodeId);
  writer.u16(definition.nodes.length);
  for (const node of definition.nodes) {
    writer.u64(node.id);
    writer.u8(campaignNodeKindCodes[node.kind]);
    writer.u64(node.encounterId ?? 0n);
    const dialogues = node.dialogueIds ?? [];
    writer.u16(dialogues.length);
    for (const dialogue of dialogues) writer.u64(dialogue);
    const unconditional = node.unconditionalTargetIds ?? [];
    writer.u16(unconditional.length);
    for (const target of unconditional) writer.u64(target);
    const branches = node.branches ?? [];
    writer.u16(branches.length);
    for (const branch of branches) {
      writer.u64(branch.targetNodeId);
      writer.u16(branch.priority);
      writer.u8(combinatorCodes[branch.combinator]);
      writer.u16(branch.predicates.length);
      for (const predicate of branch.predicates) {
        // The subject, then the tag. The tag is the byte that used to be
        // nothing but a result, and its two existing values still mean what
        // they meant. A campaign whose every predicate asks about an objective
        // writes the bytes it always wrote, down to the last one, and only a
        // world-flag predicate pays for the tail.
        if ("flagId" in predicate) {
          writer.u64(predicate.flagId);
          writer.u8(worldFlagPredicateCode);
          writer.u8(predicate.valueType);
          writer.u64(predicate.value);
          continue;
        }
        writer.u64(predicate.objectiveId);
        writer.u8(predicateResultCodes[predicate.result]);
      }
    }
  }
  // The company, after the flow, exactly where the compiler writes it: a
  // reader that stops at the last node reads the campaign and never sees them.
  const members = definition.members ?? [];
  writer.u16(members.length);
  for (const member of members) {
    writer.u64(member.id);
    writer.string(member.name);
    writer.u64(member.unitTypeId);
    writer.u64(member.joinNodeId ?? 0n);
  }
  // What the campaign puts in its store by authoring, after the company for the
  // same reason the company came after the flow. Written only when there is
  // any, so a campaign that grants nothing writes exactly the bytes it wrote
  // before grants existed. The loader reads a record that ends at its last
  // member as exactly that.
  const grants = definition.grants ?? [];
  // And what this campaign does when somebody falls, last of all. Written only
  // when the project states something, so a campaign that states nothing writes
  // exactly the bytes it wrote before either setting existed.
  //
  // The tails are positional and nested, and "the bytes ran out" is how each
  // one says it is absent. Reaching this one means writing the counts in front
  // of it even where they are zero. That is the same rule the compiler follows
  // and the loader documents: a campaign with a rule and no grants writes a
  // grant count of zero to hold the place, and one with no specificities writes
  // a specificity count of zero for the same reason. This encoder has never had
  // specificities to write, so its count here is always zero.
  const loss = definition.characterLoss ?? "permanent";
  const testing = definition.invulnerableForTesting === true;
  const statesARule = loss !== "permanent" || testing;
  // Item grants and weapon grants are two tables, exactly as the compiler
  // writes them: the store's own table holds the items and is the table it has
  // always been, and weapons take a tail of their own past the loss rule. That
  // way a campaign handing over no weapon writes the bytes it always wrote.
  const itemGrants = grants.filter((grant) => grant.itemId !== undefined);
  const weaponGrants = grants.filter((grant) => grant.weaponId !== undefined);
  if (itemGrants.length > 0 || statesARule || weaponGrants.length > 0) {
    writer.u16(itemGrants.length);
    for (const grant of itemGrants) {
      writer.u64(grant.joinNodeId ?? 0n);
      writer.u64(grant.itemId as bigint);
      writer.u32(grant.quantity);
    }
  }
  if (statesARule || weaponGrants.length > 0) {
    writer.u16(0);
    writer.u8(loss === "recoverable" ? 2 : 1);
    writer.u8(testing ? 1 : 0);
  }
  if (weaponGrants.length > 0) {
    writer.u16(weaponGrants.length);
    for (const grant of weaponGrants) {
      writer.u64(grant.joinNodeId ?? 0n);
      writer.u64(grant.weaponId as bigint);
      writer.u32(grant.quantity);
    }
  }
  return writer.toBytes();
}

/** The dialogue record payload, exactly as tools/game_content encodes it. */
function encodeDialogueRecord(dialogue: DialogueDefinition): Uint8Array {
  const writer = new ByteWriter();
  writer.string(dialogue.name);
  writer.u16(dialogue.lines.length);
  for (const line of dialogue.lines) {
    writer.string(line.speaker);
    writer.string(line.text);
  }
  // Two optional tails, and the shape of them is what lets one loader read
  // every record ever written. The engine tells the three cases apart by how
  // much of the record is left: nothing, exactly one byte, or more. What is
  // written here has to be exactly what `tools/game_content` writes.
  //
  // A scene that casts nobody writes the backdrop byte, and only when it names
  // a backdrop: nothing at all for a scene naming neither.
  const cast = dialogue.cast ?? [];
  if (cast.length === 0) {
    if (dialogue.backdrop !== undefined && dialogue.backdrop !== 0) {
      writer.u8(dialogue.backdrop);
    }
    return writer.toBytes();
  }
  if (cast.length > 255) {
    throw new Error("A scene may cast at most 255 speakers.");
  }
  // A scene that casts somebody writes the longer tail: the backdrop byte
  // unconditionally, then the cast, then one byte per line saying which entry
  // speaks it. The backdrop is zero for a scene naming none, legal only here.
  writer.u8(dialogue.backdrop ?? 0);
  writer.u8(cast.length);
  for (const unitTypeId of cast) writer.u64(unitTypeId);
  for (const line of dialogue.lines) writer.u8(line.castEntry ?? 0);
  return writer.toBytes();
}

const campaignFinalizers =
  typeof FinalizationRegistry === "function"
    ? new FinalizationRegistry<{ engine: Engine; handle: number }>((held) => {
        held.engine.exports.gl_campaign_destroy(held.handle);
      })
    : undefined;

class WasmCampaign implements Campaign {
  readonly #engine: Engine;
  #handle: number;
  readonly #token: object = {};

  constructor(active: Engine, handle: number) {
    this.#engine = active;
    this.#handle = handle;
    campaignFinalizers?.register(this, { engine: active, handle }, this.#token);
  }

  state(): CampaignNodeState {
    const active = this.#live();
    const written = active.exports.gl_campaign_state(this.#handle);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not read the campaign state (status ${status}).`
      );
    }
    const complete = result.readU8() !== 0;
    const nodeId = result.readU64();
    const kind = campaignNodeKinds[result.readU8()] ?? "terminal";
    const encounterId = result.readU64();
    const count = result.readU16();
    const dialogueIds: bigint[] = [];
    for (let index = 0; index < count; index += 1) {
      dialogueIds.push(result.readU64());
    }
    return { complete, nodeId, kind, encounterId, dialogueIds };
  }

  advanceAfter(
    outcome: Exclude<Outcome, "ongoing">,
    objectives: readonly ObjectiveResult[]
  ): CampaignError {
    const active = this.#live();
    const cursor = new Cursor(view(active));
    cursor.u8(Math.max(0, outcomes.indexOf(outcome)));
    cursor.u32(objectives.length);
    for (const objective of objectives) {
      cursor.u64(objective.id);
      cursor.u8(Math.max(0, objectiveStates.indexOf(objective.state)));
    }
    const written = active.exports.gl_campaign_advance(
      this.#handle,
      cursor.length
    );
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not advance the campaign (status ${status}).`
      );
    }
    return campaignErrorName(active, result.readU8());
  }

  advanceStory(): CampaignError {
    const active = this.#live();
    const written = active.exports.gl_campaign_advance_story(this.#handle);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not advance the campaign (status ${status}).`
      );
    }
    return campaignErrorName(active, result.readU8());
  }

  dialogue(id: bigint): DialogueResult {
    const active = this.#live();
    const written = active.exports.gl_campaign_dialogue(this.#handle, id);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not decode a dialogue (status ${status}).`
      );
    }
    const error = dialogueErrorName(active, result.readU8());
    if (error !== "none") return { error };
    const name = result.readString();
    const count = result.readU16();
    const lines: { speaker: string; text: string; castEntry: number }[] = [];
    for (let index = 0; index < count; index += 1) {
      const speaker = result.readString();
      const text = result.readString();
      lines.push({ speaker, text, castEntry: 0 });
    }
    const backdrop = result.readU8();
    // Who the scene cast, and which entry speaks each line. The join was made
    // by the compiler, so what arrives here is a resolved index into the cast
    // rather than a speaker string to match: entry zero is a line the scene
    // named nobody for.
    const castSize = result.readU8();
    const cast: bigint[] = [];
    for (let index = 0; index < castSize; index += 1) {
      cast.push(result.readU64());
    }
    for (let index = 0; index < count; index += 1) {
      lines[index]!.castEntry = result.readU8();
    }
    return { error: "none", dialogue: { id, name, lines, cast, backdrop } };
  }

  dispose(): void {
    if (this.#handle === 0) return;
    this.#engine.exports.gl_campaign_destroy(this.#handle);
    campaignFinalizers?.unregister(this.#token);
    this.#handle = 0;
  }

  #live(): Engine {
    if (this.#handle === 0) {
      throw new Error("This campaign has been disposed.");
    }
    return this.#engine;
  }
}

/**
 * Creates a campaign cursor on the authoritative engine.
 *
 * Structural validation belongs to package_runtime::load_campaign, which runs
 * unmodified inside the module: combinators, reference existence, the one
 * unconditional transition. Dialogues are attached one record at a
 * time; a record that cannot cross the boundary is skipped, exactly as the
 * native client skips a dialogue it cannot load.
 */
export function createCampaign(
  definition: CampaignFlowDefinition
): CampaignCreateResult {
  const active = required();
  let record: Uint8Array;
  try {
    record = encodeCampaignRecord(definition);
  } catch {
    return { error: "malformed_payload" };
  }
  const encounterIds = [
    ...new Set(
      definition.nodes
        .filter((node) => node.kind === "encounter")
        .map((node) => node.encounterId ?? 0n)
    )
  ];
  const payloadSize = 8 + 4 + record.length + 2 + encounterIds.length * 8;
  if (payloadSize > active.bufferCapacity) {
    return { error: "malformed_payload" };
  }
  const cursor = new Cursor(view(active));
  cursor.u64(definition.id);
  cursor.u32(record.length);
  cursor.bytes(record);
  cursor.u16(encounterIds.length);
  for (const id of encounterIds) cursor.u64(id);

  const handle = active.exports.gl_campaign_create(cursor.length);
  if (handle === 0) {
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (status !== abiOk) {
      throw new Error(
        `The engine rejected a campaign payload (status ${status}).`
      );
    }
    return {
      error: campaignErrorName(active, result.readU8()) as Exclude<
        CampaignError,
        "none"
      >
    };
  }

  // Each scene is attached one call at a time, and each attachment is answered.
  //
  // A record the engine did not take is a scene that will never play: the node
  // reaches it, the loader finds nothing, and the campaign carries on with a
  // cutscene silently missing. A scene past the shared buffer is well inside
  // what the dialogue schema allows an author to write, so this is reachable
  // from ordinary content rather than only from a mistake.
  const campaign = new WasmCampaign(active, handle);
  for (const dialogue of definition.dialogues ?? []) {
    let encoded: Uint8Array;
    try {
      encoded = encodeDialogueRecord(dialogue);
    } catch {
      campaign.dispose();
      return { error: "malformed_payload" };
    }
    if (8 + 4 + encoded.length > active.bufferCapacity) {
      campaign.dispose();
      return { error: "malformed_payload" };
    }
    const attach = new Cursor(view(active));
    attach.u64(dialogue.id);
    attach.u32(encoded.length);
    attach.bytes(encoded);
    const written = active.exports.gl_campaign_add_dialogue(
      handle,
      attach.length
    );
    if (written === 0 || new Cursor(view(active)).readU8() !== abiOk) {
      campaign.dispose();
      return { error: "malformed_payload" };
    }
  }
  return { error: "none", campaign };
}

// --- The campaign session ---------------------------------------------------
//
// The persistent campaign a client keeps, driven step by step across the
// boundary. Everything below is transport: a payload written, a payload read.
// Not one campaign fact is decided here. Who is left off a board, who earned
// what, what a level granted, what fell into the store, whether the campaign
// moved and where to are all `client::CampaignSession`, which is the same C++
// the terminal runs, compiled into the same module.

/** Which stats a level-up may grow, in the order the engine rolls them. */
export const growableStats = [
  "health",
  "strength",
  "defense",
  "resistance",
  "movement",
  "actionPoints",
  "skill",
  "luck",
  "evasion",
  "magic"
] as const;

export type GrowableStat = (typeof growableStats)[number];

export type CampaignAvailability =
  | "unrecruited"
  | "available"
  | "retired"
  | "dead";

const availabilities: Readonly<Record<number, CampaignAvailability>> = {
  1: "unrecruited",
  2: "available",
  3: "retired",
  4: "dead"
};

/** One member of the company the campaign authored, as it holds them now. */
export interface CampaignMember {
  /** The persistent identity, which is the same character across battles. */
  id: bigint;
  /** The authored member identity every placement fielding them carries. */
  sourceKeyId: bigint;
  /** What the author called them. Never derived from a unit type. */
  name: string;
  unitTypeId: bigint;
  availability: CampaignAvailability;
  level: number;
  experience: number;
  /** Permanent points this member has earned, by stat. */
  gained: Readonly<Record<GrowableStat, number>>;
  /**
   * What the campaign holds for this member, ascending by item identity.
   *
   * This is the satchel they will take onto the next board, not the list their
   * unit type names, so a draught spent in one battle is missing from here in
   * the next.
   */
  carried: readonly CampaignStack[];
}

/** A quantity of one item identity, held by a member or by the company. */
export interface CampaignStack {
  itemId: bigint;
  quantity: number;
}

/** The growth block one unit type authors, as the compiler encodes it. */
export interface UnitTypeProgression {
  id: bigint;
  /** What defeating one of these grants whoever felled it. */
  experienceAward: number;
  /** Lifetime experience per level for a character of this type. */
  experiencePerLevel: number;
  /** Whole-percent chance each stat gains a point on a level-up. */
  growth: Partial<Record<GrowableStat, number>>;
  /**
   * What a character of this type starts out carrying, in the authored order.
   *
   * The campaign reads this once, when a member joins, and puts one of each in
   * that member's hands; from then on what they carry into a battle is what the
   * campaign holds for them. A record that leaves it empty founds a company that
   * fights empty-handed, which is why it travels even though no board on this
   * side of the boundary reads it.
   */
  startingItemIds?: readonly bigint[];
  dropItemId?: bigint;
  dropChance?: number;
}

/** One board a campaign node is fought on, before any roster touches it. */
export interface CampaignBoardDefinition {
  encounterId: bigint;
  definition: EncounterDefinition;
  /**
   * The authored placement identity of each unit, in the definition's own unit
   * order. Zero for a unit no campaign member could ever be.
   */
  sourceKeyIds: readonly bigint[];
  /**
   * How many of the company this board's author lets take its field. Omitted or
   * zero is a board that caps nothing, which is every board by default.
   *
   * A maximum and not a quota: fewer is legal, and the engine never benches
   * anybody to make a party fit. It travels beside the definition rather than
   * inside it because the simulation never learns it. A canonical hash that
   * depended on how many of a company were allowed out would be a hash that
   * depended on a save file.
   */
  deploymentCapacity?: number;
  /**
   * What the author wrote about the characters this board fields, beyond their
   * unit types, keyed by the authored member identity, the same identity
   * `sourceKeyIds` carries. Omitted or empty is a company of
   * characters who are exactly their classes, which is every campaign that
   * authors no specificity.
   *
   * It travels beside the board for the reason the capacity does, and it is
   * attached by whoever knows the campaign: the roster join reads it off the
   * board precisely so that a front end playing content which was never
   * compiled can hand over its own table and go through the same pass as
   * everything else.
   */
  memberSpecificities?: readonly MemberSpecificity[];
}

/**
 * What one authored character is, beyond their unit type.
 *
 * `statDeltas` is added to whatever the class says and is indexed in one fixed
 * order: health, strength, defense, resistance, movement, actionPoints, skill,
 * luck, evasion, magic, speed. The first ten are the ten a level-up may grow,
 * at the same indices; speed is eleventh because it is the one stat the two
 * lists differ by. Growth refuses it because a roll would reshuffle a turn
 * order mid-battle, and an authored number is fixed before anybody plays.
 *
 * These are deltas and not totals, which is what lets an author rebalance the
 * class underneath the character, and what lets a specificity and an earned
 * level-up compose into their sum.
 */
export interface MemberSpecificity {
  memberId: bigint;
  /** Eleven signed adjustments, in the order above. Zero says nothing. */
  statDeltas: readonly number[];
  /** Added to the maximum of the band of every weapon they strike with. */
  reachBonus: number;
}

/** How many stats a specificity may adjust, and the length of `statDeltas`. */
export const specificStatCount = 11;

export interface CampaignSessionDefinition {
  /** Sixteen bytes naming the package this campaign belongs to. */
  packageId: Uint8Array;
  contentRevision: number;
  campaignId: bigint;
  flow: CampaignFlowDefinition;
  boards: readonly CampaignBoardDefinition[];
  unitTypes: readonly UnitTypeProgression[];
}

export type CampaignSessionError =
  | "none"
  | "graph_rejected"
  | "board_rejected"
  | "invalid_slot"
  | "roster_rejected"
  | "progression_rejected"
  | "flow_stalled";

export type RosterError =
  | "none"
  | "encounter_rejected"
  | "duplicate_assignment"
  | "reserved_identity"
  | "side_emptied"
  | "unavailable_objective_target"
  | "over_deployment_capacity";

/**
 * The roster's own vocabulary, in its own order, read back out of the engine.
 *
 * A screen that refuses a gesture the roster would have refused shows the word
 * the roster would have used, and gets it from here rather than spelling it
 * out, the same reason every other error vocabulary is read back rather than
 * restated. Before the engine is instantiated there is nothing to read and the
 * list is empty; a caller that has a session has an engine.
 */
export function rosterErrorNames(): readonly string[] {
  return engine?.rosterErrors ?? [];
}

/**
 * The engine's own word for one roster refusal.
 *
 * Falls back to this module's spelling when the engine has not been loaded,
 * which is the same spelling: the union above is the enumerator list, in the
 * enumerator order, and this is what keeps the two from drifting apart
 * silently.
 */
export function rosterErrorName(error: RosterError): string {
  const ordinal = rosterErrors.indexOf(error);
  return rosterErrorNames()[ordinal] ?? error;
}

const rosterErrors: readonly RosterError[] = [
  "none",
  "encounter_rejected",
  "duplicate_assignment",
  "reserved_identity",
  "side_emptied",
  "unavailable_objective_target",
  "over_deployment_capacity"
];

export type StorageError =
  | "none"
  | "invalid_slot_name"
  | "not_found"
  | "too_large"
  | "out_of_space"
  | "unavailable"
  | "io_failure";

/**
 * Why a named slot did not become a campaign, in each layer's own words.
 *
 * Four layers and four vocabularies, because they answer different questions
 * and a client that flattened them would have to invent a fifth. The device
 * says whether the bytes were there; the migration registry says whether the
 * content the save names is the content that is loaded; the save format says
 * whether this build can read what they say; the campaign state says whether
 * what they say is a campaign. Each is `"none"` when that layer had nothing to
 * refuse, and `wrongCampaign` is the client's own last check: a perfectly good
 * save of a different campaign.
 */
export interface CampaignSlotFailure {
  storage: StorageError;
  migration: string;
  save: string;
  /** The state invariant that was broken, by number; zero when none was. */
  state: number;
  wrongCampaign: boolean;
}

export interface CampaignSessionBegun {
  error: CampaignSessionError;
  /** A slot was asked for and refused; the founded campaign is what stands. */
  refused: boolean;
  resumed: boolean;
  failure: CampaignSlotFailure;
}

export interface CampaignStanding {
  error: CampaignSessionError;
  nodeId: bigint;
  kind: CampaignNodeKind;
  encounterId: bigint;
  dialogueIds: readonly bigint[];
  roster: readonly CampaignMember[];
}

export interface CampaignBattleBoard {
  error: CampaignSessionError;
  rosterError: RosterError;
  loadError: number;
  createError: CreateError;
  encounterId: bigint;
  /** Members the roster kept off this board, however plainly it lists them. */
  excluded: readonly bigint[];
  /** Which board unit is which member. Only members appear. */
  binding: ReadonlyMap<bigint, bigint>;
  /** Which authored placement each board unit came from, in board order. */
  placements: readonly { unitId: bigint; sourceKeyId: bigint }[];
  encounter?: Encounter;
}

/** One consequence a battle had, exactly as the campaign recorded it. */
export interface CampaignOperation {
  kind: string;
  selector: number;
  /** The member it is about, or zero for the campaign's shared store. */
  subject: bigint;
  category: number;
  definitionId: bigint;
  amount: bigint;
}

export interface CampaignLevelUp {
  member: bigint;
  fromLevel: number;
  toLevel: number;
  /** Points gained per stat across every level in this batch. */
  points: Readonly<Record<GrowableStat, number>>;
}

export interface CampaignAftermath {
  error: CampaignSessionError;
  saved: StorageError;
  outcome: Outcome;
  progressionError: number;
  canonicalHash: bigint;
  /**
   * Members the battle put at zero health. Who *fell*, never who was buried:
   * a character at zero health left the battlefield either way, and what became
   * of them afterwards is `characterLoss` below.
   */
  fallen: readonly bigint[];
  /**
   * What this campaign does with a character who falls. Under `permanent` they
   * are dead and the company never sees them again. Under `recoverable` they
   * were carried off and rejoin the company, still holding what the battle left
   * them with. A surface naming the fallen reads this before it chooses a word.
   */
  characterLoss: CharacterLoss;
  levelUps: readonly CampaignLevelUp[];
  operations: readonly CampaignOperation[];
  advanced: boolean;
  alreadyAdvanced: boolean;
  progressionCode: number;
  targetNodeId: bigint;
  /**
   * Members an authored recruitment brought into the company as this node
   * completed, read off the committed batch and derived from nothing.
   */
  recruited: readonly CampaignMember[];
  roster: readonly CampaignMember[];
  /**
   * What the company owns beyond what its members carry, as the commit left it.
   * A drop lands here; a draught a character drank comes out of their own kit,
   * which is why the two are reported apart.
   */
  store: readonly CampaignStack[];
}

/** What completing a story node did: where it went, and who it brought in. */
export interface CampaignStoryAdvance {
  error: CampaignSessionError;
  joined: readonly CampaignMember[];
}

/**
 * The company between battles, and what the next board has room for.
 *
 * Read out of committed campaign state and off the authored board. There is no
 * pending arrangement in it and none anywhere else: every management gesture
 * commits and is written to the slot before the next one of these can be asked
 * for, so two taken either side of a gesture differ by exactly what committed.
 */
export interface CampaignCompany {
  error: CampaignSessionError;
  nodeId: bigint;
  encounterId: bigint;
  /**
   * Which members the next board has a placement for.
   *
   * A member who is not here cannot be put on this board by any availability,
   * so a screen offers no choice about them: fielding them would be a gesture
   * that succeeds and changes nothing.
   */
  placeable: readonly bigint[];
  /**
   * Which of those members would actually take the field as the company
   * stands: the placeable members the campaign says are deployable.
   *
   * This is the set `capacity` is counted against, and it is published by the
   * engine rather than worked out here so that two clients cannot count
   * differently and offer gestures the engine refuses.
   */
  fielded: readonly bigint[];
  /**
   * How many of the company this board's author lets take its field, or zero
   * for a board that caps nothing, which is every board by default.
   *
   * A maximum and not a quota: fewer is legal, and the engine never benches
   * anybody to make a party fit. A screen refuses a `field` that would carry
   * `fielded` past this under the roster's own word for it, and that check is
   * an early copy of the engine's rather than a substitute. Taking the board
   * refuses an over-cap company however a screen counted.
   */
  capacity: number;
  roster: readonly CampaignMember[];
  store: readonly CampaignStack[];
}

/** Which way one management gesture moves. */
export type CampaignManagementVerb = "give" | "take" | "field" | "bench";

/** The campaign's own reasons for refusing an outcome batch. */
export type CampaignOutcomeError = string;

/**
 * One management gesture, committed or refused, with the company as it left it.
 *
 * `outcomeError` is `"none"` when the campaign accepted it. Every other value is
 * the campaign layer's own word, such as `insufficient_items`, `unit_is_dead`
 * or `unknown_unit`, and a client shows that word rather than a paraphrase.
 */
export interface CampaignManagementResult {
  /** The session's own refusal: there was no company to manage. */
  error: "none" | "not_managing";
  outcomeError: CampaignOutcomeError;
  stateError: number;
  /** The batch was already committed. Not an error; the right answer to a retry. */
  alreadyApplied: boolean;
  /** The slot was written, because the gesture committed. */
  saved: boolean;
  saveError: StorageError;
  /** The very operations that were committed, so a screen can say what moved. */
  operations: readonly CampaignOperation[];
  roster: readonly CampaignMember[];
  store: readonly CampaignStack[];
}

export interface CampaignSession {
  begin(options: {
    slot: string;
    resume: boolean;
    playerSide?: Side;
  }): CampaignSessionBegun;
  standing(): CampaignStanding;
  advanceStory(): CampaignStoryAdvance;
  /** Takes the standing node's board through the roster and starts the battle. */
  board(): CampaignBattleBoard;
  /** Commits what the battle did and writes the campaign to its slot. */
  commit(): CampaignAftermath;
  /** The company between battles, and what the next board has room for. */
  company(): CampaignCompany;
  /**
   * One management gesture: one outcome batch, committed and saved, or refused
   * whole. `item` is read for `give` and `take` and ignored for the other two.
   */
  manage(
    verb: CampaignManagementVerb,
    member: bigint,
    itemId?: bigint
  ): CampaignManagementResult;
  dispose(): void;
}

export type CampaignSessionResult =
  | { error: "none"; session: CampaignSession }
  | { error: Exclude<CampaignSessionError, "none"> | "malformed_payload" };

/**
 * The unit type record payload, exactly as tools/game_content encodes it.
 *
 * Three of the fields are read back on the other side and the rest are not.
 * `load_unit_progression` skips the name, the class, the faction and the three
 * identity lists on its way to the growth block, so those stay empty. The one
 * exception is the starting item list, which `load_unit_starting_items` reads
 * to stock a member's kit the day they join.
 *
 * The bytes still have to be in the compiler's
 * own order and shape, because the compiler's own decoders are what read them.
 */
function encodeUnitTypeProgressionRecord(
  progression: UnitTypeProgression
): Uint8Array {
  const writer = new ByteWriter();
  writer.string("");
  writer.u64(0n);
  writer.u64(0n);
  writer.u16(0);
  const startingItems = progression.startingItemIds ?? [];
  writer.u16(startingItems.length);
  for (const item of startingItems) writer.u64(item);
  writer.u16(0);
  writer.u16(progression.experienceAward);
  writer.u16(progression.experiencePerLevel);
  for (const stat of growableStats) writer.u8(progression.growth[stat] ?? 0);
  writer.u64(progression.dropItemId ?? 0n);
  writer.u8(progression.dropChance ?? 0);
  return writer.toBytes();
}

function readMembers(cursor: Cursor): CampaignMember[] {
  const count = cursor.readU32();
  const members: CampaignMember[] = [];
  for (let index = 0; index < count; index += 1) {
    const id = cursor.readU64();
    const sourceKeyId = cursor.readU64();
    const name = cursor.readString();
    const unitTypeId = cursor.readU64();
    const availability = availabilities[cursor.readU8()] ?? "unrecruited";
    const level = cursor.readU16();
    const experience = cursor.readU32();
    const gained: Record<GrowableStat, number> = {} as Record<
      GrowableStat,
      number
    >;
    for (const stat of growableStats) gained[stat] = cursor.readU16();
    members.push({
      id,
      sourceKeyId,
      name,
      unitTypeId,
      availability,
      level,
      experience,
      gained,
      carried: readStacks(cursor)
    });
  }
  return members;
}

/** A list of stacks, in the one shape both of a campaign's owners use. */
function readStacks(cursor: Cursor): CampaignStack[] {
  const count = cursor.readU32();
  const stacks: CampaignStack[] = [];
  for (let index = 0; index < count; index += 1) {
    stacks.push({ itemId: cursor.readU64(), quantity: cursor.readU32() });
  }
  return stacks;
}

/** The verb codes `gl_campaign_session_manage` reads, in its own order. */
const managementVerbs: Readonly<Record<CampaignManagementVerb, number>> = {
  give: 0,
  take: 1,
  field: 2,
  bench: 3
};

function sessionErrorName(active: Engine, code: number): CampaignSessionError {
  return (active.sessionErrors[code] ?? "flow_stalled") as CampaignSessionError;
}

const campaignSessionFinalizers =
  typeof FinalizationRegistry === "function"
    ? new FinalizationRegistry<{ engine: Engine; handle: number }>((held) => {
        held.engine.exports.gl_campaign_session_destroy(held.handle);
      })
    : undefined;

class WasmCampaignSession implements CampaignSession {
  readonly #engine: Engine;
  #handle: number;
  #battle: Encounter | undefined;
  readonly #token: object = {};

  constructor(active: Engine, handle: number) {
    this.#engine = active;
    this.#handle = handle;
    campaignSessionFinalizers?.register(
      this,
      { engine: active, handle },
      this.#token
    );
  }

  begin(options: {
    slot: string;
    resume: boolean;
    playerSide?: Side;
  }): CampaignSessionBegun {
    const active = this.#live();
    const cursor = new Cursor(view(active));
    cursor.u8(options.resume ? 1 : 0);
    cursor.u8((options.playerSide ?? "first") === "first" ? 0 : 1);
    cursor.string(options.slot);
    const written = active.exports.gl_campaign_session_begin(
      this.#handle,
      cursor.length
    );
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not begin the campaign (status ${status}).`
      );
    }
    const error = sessionErrorName(active, result.readU8());
    const refused = result.readU8() !== 0;
    const resumed = result.readU8() !== 0;
    const failure: CampaignSlotFailure = {
      storage: (active.storageErrors[result.readU8()] ?? "io_failure") as
        StorageError,
      migration: active.migrationErrors[result.readU8()] ?? "invalid_result",
      save: active.saveErrors[result.readU8()] ?? "invalid_state",
      state: result.readU8(),
      wrongCampaign: result.readU8() !== 0
    };
    return { error, refused, resumed, failure };
  }

  standing(): CampaignStanding {
    const active = this.#live();
    const written = active.exports.gl_campaign_session_state(this.#handle);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not read the campaign session (status ${status}).`
      );
    }
    const error = sessionErrorName(active, result.readU8());
    const nodeId = result.readU64();
    const kind = campaignNodeKinds[result.readU8()] ?? "terminal";
    const encounterId = result.readU64();
    const dialogueCount = result.readU16();
    const dialogueIds: bigint[] = [];
    for (let index = 0; index < dialogueCount; index += 1) {
      dialogueIds.push(result.readU64());
    }
    return {
      error,
      nodeId,
      kind,
      encounterId,
      dialogueIds,
      roster: readMembers(result)
    };
  }

  advanceStory(): CampaignStoryAdvance {
    const active = this.#live();
    const written = active.exports.gl_campaign_session_advance_story(
      this.#handle
    );
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not advance the campaign (status ${status}).`
      );
    }
    const error = sessionErrorName(active, result.readU8());
    return { error, joined: readMembers(result) };
  }

  board(): CampaignBattleBoard {
    const active = this.#live();
    this.#battle = undefined;
    const handle = active.exports.gl_campaign_session_board(this.#handle);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (status !== abiOk) {
      throw new Error(
        `The engine could not prepare the board (status ${status}).`
      );
    }
    const error = sessionErrorName(active, result.readU8());
    const rosterError = (active.rosterErrors[result.readU8()] ??
      "encounter_rejected") as RosterError;
    const loadError = result.readU8();
    const createError = createErrorName(active, result.readU8());
    const empty: CampaignBattleBoard = {
      error,
      rosterError,
      loadError,
      createError,
      encounterId: 0n,
      excluded: [],
      binding: new Map(),
      placements: []
    };
    if (handle === 0) return empty;
    const encounterId = result.readU64();
    const excludedCount = result.readU32();
    const excluded: bigint[] = [];
    for (let index = 0; index < excludedCount; index += 1) {
      excluded.push(result.readU64());
    }
    const bindingCount = result.readU32();
    const binding = new Map<bigint, bigint>();
    for (let index = 0; index < bindingCount; index += 1) {
      const unitId = result.readU64();
      binding.set(unitId, result.readU64());
    }
    const placementCount = result.readU32();
    const placements: { unitId: bigint; sourceKeyId: bigint }[] = [];
    for (let index = 0; index < placementCount; index += 1) {
      const unitId = result.readU64();
      placements.push({ unitId, sourceKeyId: result.readU64() });
    }
    // The session owns the battle: it is released when the next board is
    // prepared or the session is disposed, so this wrapper never releases it.
    const encounter = new WasmEncounter(active, handle, false);
    this.#battle = encounter;
    return {
      error,
      rosterError,
      loadError,
      createError,
      encounterId,
      excluded,
      binding,
      placements,
      encounter
    };
  }

  commit(): CampaignAftermath {
    const active = this.#live();
    const written = active.exports.gl_campaign_session_commit(this.#handle);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not commit the battle (status ${status}).`
      );
    }
    const error = sessionErrorName(active, result.readU8());
    const saved = (active.storageErrors[result.readU8()] ??
      "io_failure") as StorageError;
    const outcome = outcomes[result.readU8()] ?? "ongoing";
    const progressionError = result.readU8();
    const canonicalHash = result.readU64();
    const fallenCount = result.readU32();
    const fallen: bigint[] = [];
    for (let index = 0; index < fallenCount; index += 1) {
      fallen.push(result.readU64());
    }
    const levelUpCount = result.readU32();
    const levelUps: CampaignLevelUp[] = [];
    for (let index = 0; index < levelUpCount; index += 1) {
      const member = result.readU64();
      const fromLevel = result.readU16();
      const toLevel = result.readU16();
      const points: Record<GrowableStat, number> = {} as Record<
        GrowableStat,
        number
      >;
      for (const stat of growableStats) points[stat] = result.readU16();
      levelUps.push({ member, fromLevel, toLevel, points });
    }
    const operationCount = result.readU32();
    const operations: CampaignOperation[] = [];
    for (let index = 0; index < operationCount; index += 1) {
      const kind = active.operationNames[result.readU8()] ?? "unknown";
      const selector = result.readU8();
      const subject = result.readU64();
      const category = result.readU8();
      const definitionId = result.readU64();
      operations.push({
        kind,
        selector,
        subject,
        category,
        definitionId,
        // The engine writes a signed amount as its own bit pattern; this is
        // the same number, read back with its sign.
        amount: BigInt.asIntN(64, result.readU64())
      });
    }
    const advanced = result.readU8() !== 0;
    const alreadyAdvanced = result.readU8() !== 0;
    const progressionCode = result.readU8();
    const targetNodeId = result.readU64();
    const recruited = readMembers(result);
    const roster = readMembers(result);
    const store = readStacks(result);
    // The rule that decided what became of the fallen, read last because the
    // engine writes it last. Named locals rather than trailing properties in the
    // literal below, so that the order these are read in is stated by the code
    // rather than left to the order an object literal happens to evaluate in.
    const characterLoss = characterLossName(result.readU8());
    this.#battle = undefined;
    return {
      error,
      saved,
      outcome,
      progressionError,
      canonicalHash,
      fallen,
      characterLoss,
      levelUps,
      operations,
      advanced,
      alreadyAdvanced,
      progressionCode,
      targetNodeId,
      recruited,
      roster,
      store
    };
  }

  company(): CampaignCompany {
    const active = this.#live();
    const written = active.exports.gl_campaign_session_company(this.#handle);
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not read the company (status ${status}).`
      );
    }
    const error = sessionErrorName(active, result.readU8());
    const nodeId = result.readU64();
    const encounterId = result.readU64();
    const placeableCount = result.readU32();
    const placeable: bigint[] = [];
    for (let index = 0; index < placeableCount; index += 1) {
      placeable.push(result.readU64());
    }
    const fieldedCount = result.readU32();
    const fielded: bigint[] = [];
    for (let index = 0; index < fieldedCount; index += 1) {
      fielded.push(result.readU64());
    }
    const capacity = result.readU16();
    return {
      error,
      nodeId,
      encounterId,
      placeable,
      fielded,
      capacity,
      roster: readMembers(result),
      store: readStacks(result)
    };
  }

  manage(
    verb: CampaignManagementVerb,
    member: bigint,
    itemId = 0n
  ): CampaignManagementResult {
    const active = this.#live();
    const cursor = new Cursor(view(active));
    cursor.u8(managementVerbs[verb]);
    cursor.u64(member);
    cursor.u64(itemId);
    const written = active.exports.gl_campaign_session_manage(
      this.#handle,
      cursor.length
    );
    const result = new Cursor(view(active));
    const status = result.readU8();
    if (written === 0 || status !== abiOk) {
      throw new Error(
        `The engine could not manage the company (status ${status}).`
      );
    }
    const error = result.readU8() === 0 ? "none" : "not_managing";
    const outcomeError = active.outcomeErrors[result.readU8()] ?? "unknown";
    const stateError = result.readU8();
    const alreadyApplied = result.readU8() !== 0;
    const saved = result.readU8() !== 0;
    const saveError = (active.storageErrors[result.readU8()] ??
      "io_failure") as StorageError;
    const operationCount = result.readU32();
    const operations: CampaignOperation[] = [];
    for (let index = 0; index < operationCount; index += 1) {
      const kind = active.operationNames[result.readU8()] ?? "unknown";
      const selector = result.readU8();
      const subject = result.readU64();
      const definitionId = result.readU64();
      operations.push({
        kind,
        selector,
        subject,
        category: 0,
        definitionId,
        amount: BigInt.asIntN(64, result.readU64())
      });
    }
    return {
      error,
      outcomeError,
      stateError,
      alreadyApplied,
      saved,
      saveError,
      operations,
      roster: readMembers(result),
      store: readStacks(result)
    };
  }

  dispose(): void {
    if (this.#handle === 0) return;
    this.#battle = undefined;
    this.#engine.exports.gl_campaign_session_destroy(this.#handle);
    campaignSessionFinalizers?.unregister(this.#token);
    this.#handle = 0;
  }

  #live(): Engine {
    if (this.#handle === 0) {
      throw new Error("This campaign session has been disposed.");
    }
    return this.#engine;
  }
}

/**
 * Creates the campaign session Play mode drives.
 *
 * The authored flow, every board it can reach, and every unit type's growth
 * block cross the boundary once, at creation. Afterwards the session is asked
 * questions and told what happened; it is never told an answer.
 */
export function createCampaignSession(
  definition: CampaignSessionDefinition
): CampaignSessionResult {
  const active = required();
  if (definition.packageId.length !== 16) {
    return { error: "malformed_payload" };
  }
  let record: Uint8Array;
  try {
    record = encodeCampaignRecord(definition.flow);
  } catch {
    return { error: "malformed_payload" };
  }
  const encounterIds = definition.flow.nodes
    .filter((node) => node.kind === "encounter")
    .map((node) => node.encounterId ?? 0n)
    .filter((id) => id !== 0n);
  const payloadSize =
    16 + 4 + 8 + 4 + record.length + 2 + encounterIds.length * 8;
  if (payloadSize > active.bufferCapacity) return { error: "malformed_payload" };

  const cursor = new Cursor(view(active));
  cursor.bytes(definition.packageId);
  cursor.u32(definition.contentRevision);
  cursor.u64(definition.campaignId);
  cursor.u32(record.length);
  cursor.bytes(record);
  cursor.u16(encounterIds.length);
  for (const id of encounterIds) cursor.u64(id);

  const handle = active.exports.gl_campaign_session_create(cursor.length);
  if (handle === 0) return { error: "malformed_payload" };
  const session = new WasmCampaignSession(active, handle);

  // Every attachment is answered, and a refused one ends the session rather
  // than being skipped. A growth block the engine did not take is a character
  // who levels up by somebody else's rule; a board it did not take is a battle
  // the campaign reaches and cannot start. Neither has a symptom at the moment
  // it happens, which is precisely why the status is read.
  for (const unitType of definition.unitTypes) {
    const encoded = encodeUnitTypeProgressionRecord(unitType);
    if (8 + 4 + encoded.length > active.bufferCapacity) {
      session.dispose();
      return { error: "malformed_payload" };
    }
    const attach = new Cursor(view(active));
    attach.u64(unitType.id);
    attach.u32(encoded.length);
    attach.bytes(encoded);
    const written = active.exports.gl_campaign_session_add_unit_type(
      handle,
      attach.length
    );
    if (written === 0 || new Cursor(view(active)).readU8() !== abiOk) {
      session.dispose();
      return { error: "malformed_payload" };
    }
  }
  for (const board of definition.boards) {
    if (boardAttachmentSize(board) > active.bufferCapacity) {
      session.dispose();
      return { error: "malformed_payload" };
    }
    const attach = new Cursor(view(active));
    writeBoardAttachment(attach, board);
    const written = active.exports.gl_campaign_session_add_board(
      handle,
      attach.length
    );
    if (written === 0 || new Cursor(view(active)).readU8() !== abiOk) {
      session.dispose();
      return { error: "malformed_payload" };
    }
  }
  return { error: "none", session };
}

/** One authored board, in the order `gl_campaign_session_add_board` reads it. */
function writeBoardAttachment(
  cursor: FixedWidthWriter,
  board: CampaignBoardDefinition
): void {
  cursor.u64(board.encounterId);
  writeEncounterDefinition(cursor, board.definition);
  cursor.u32(board.sourceKeyIds.length);
  for (const key of board.sourceKeyIds) cursor.u64(key);
  // Counted rather than optional, exactly as the deployment region above is:
  // a board with no cap writes a zero rather than nothing, so that a caller
  // with nothing to say and a truncated payload are not the same bytes.
  cursor.u16(board.deploymentCapacity ?? 0);
  // And on the same convention, what the author made of the characters. Dense
  // rather than sparse: the package packs its tail because it is read on two
  // consoles, and this crosses one boundary inside one process, where eleven
  // numbers written flat is a writer with no branch in it.
  const specificities = board.memberSpecificities ?? [];
  cursor.u32(specificities.length);
  for (const specificity of specificities) {
    cursor.u64(specificity.memberId);
    for (let stat = 0; stat < specificStatCount; stat += 1) {
      cursor.i16(specificity.statDeltas[stat] ?? 0);
    }
    cursor.u8(specificity.reachBonus);
  }
}

/** How many bytes that attachment will be, measured by writing it. */
function boardAttachmentSize(board: CampaignBoardDefinition): number {
  const tally = new Tally();
  writeBoardAttachment(tally, board);
  return tally.length;
}

// ---------------------------------------------------------------------------
// The slot device
// ---------------------------------------------------------------------------
//
// The module keeps one slot device, and every campaign session in it saves
// there. It is the module's own memory: it outlives a session and not a page.
//
// These three read, replace and forget a slot's bytes on that device, and they
// are the whole of what makes a playtest durable. Something with somewhere
// durable to keep bytes reads a slot after a save and puts it back before the
// next begin. The bytes are the `GLSV` envelope verbatim: nothing here reads a
// field of it, because the save format already carries its own integrity, its
// own versioning and its own package requirements, and a second wrapper would
// be a second thing to keep in step.

/** What a slot holds, or the device's own reason there is nothing to hold. */
export interface SlotBytes {
  error: StorageError;
  bytes: Uint8Array | undefined;
}

/** Reads one slot off the engine's own device. */
export function readEngineSlot(slot: string): SlotBytes {
  const active = required();
  const cursor = new Cursor(view(active));
  cursor.string(slot);
  const written = active.exports.gl_storage_read(cursor.length);
  const result = new Cursor(view(active));
  const status = result.readU8();
  if (written === 0 || status !== abiOk) {
    throw new Error(`The engine could not read a slot (status ${status}).`);
  }
  const error = (active.storageErrors[result.readU8()] ?? "io_failure") as
    StorageError;
  const size = result.readU32();
  if (error !== "none") return { error, bytes: undefined };
  // Copied out of the scratch buffer rather than viewed into it: the next call
  // through this boundary overwrites those bytes.
  const bytes = new Uint8Array(size);
  for (let index = 0; index < size; index += 1) bytes[index] = result.readU8();
  return { error, bytes };
}

/**
 * Puts bytes into one slot on the engine's own device, so that a session
 * beginning afterwards finds them there.
 *
 * The bytes are handed over unread. A payload that is not a save is refused
 * later, by the save format, with the save format's own name for it.
 */
export function writeEngineSlot(slot: string, bytes: Uint8Array): StorageError {
  const active = required();
  const encodedSlot = new TextEncoder().encode(slot).length;
  if (2 + encodedSlot + 4 + bytes.length > active.bufferCapacity) {
    return "too_large";
  }
  const cursor = new Cursor(view(active));
  cursor.string(slot);
  cursor.u32(bytes.length);
  cursor.bytes(bytes);
  const written = active.exports.gl_storage_write(cursor.length);
  const result = new Cursor(view(active));
  const status = result.readU8();
  if (written === 0 || status !== abiOk) {
    throw new Error(`The engine could not write a slot (status ${status}).`);
  }
  return (active.storageErrors[result.readU8()] ?? "io_failure") as StorageError;
}

/** Forgets one slot on the engine's own device. */
export function eraseEngineSlot(slot: string): StorageError {
  const active = required();
  const cursor = new Cursor(view(active));
  cursor.string(slot);
  const written = active.exports.gl_storage_erase(cursor.length);
  const result = new Cursor(view(active));
  const status = result.readU8();
  if (written === 0 || status !== abiOk) {
    throw new Error(`The engine could not erase a slot (status ${status}).`);
  }
  return (active.storageErrors[result.readU8()] ?? "io_failure") as StorageError;
}

/** Stable, JSON-safe representation for logs, fixtures, and cross-runtime diffs. */
export function canonicalState(snapshot: EncounterSnapshot): object {
  return {
    width: snapshot.width,
    height: snapshot.height,
    activeSide: snapshot.activeSide,
    activationCount: snapshot.activationCount.toString(),
    outcome: snapshot.outcome,
    units: [...snapshot.units]
      .sort((left, right) => (left.id < right.id ? -1 : left.id > right.id ? 1 : 0))
      .map((unit) => ({
        id: unit.id.toString(),
        unitTypeId: unit.unitTypeId.toString(),
        side: unit.side,
        x: unit.position.x,
        y: unit.position.y,
        health: unit.health,
        maximumHealth: unit.maximumHealth,
        strength: unit.strength,
        defense: unit.defense
      }))
  };
}
