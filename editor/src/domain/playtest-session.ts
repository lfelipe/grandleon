// SPDX-License-Identifier: MIT
import type {
  CampaignNode,
  CampaignTransition,
  EncounterPlacement,
  SourceCampaign,
  SourceDialogue,
  SourceProject,
  SourceWeapon,
  SourceItem
} from "../generated/source-v1";
import {
  backdropIndex,
  factionColor,
  projectCharacterFigure,
  projectCharacterStyle,
  projectTheme,
  type FactionColor
} from "./board-art";
import { resolveTurnOrder } from "./game-settings";
import {
  createCampaign,
  createEncounter,
  isEncounterEngineReady,
  stableContentId,
  type Campaign,
  type CampaignBranchDefinition,
  type CampaignCombinator,
  type CampaignFlowDefinition,
  type CampaignItemGrantDefinition,
  type CampaignMemberDefinition,
  type CampaignNodeDefinition,
  type CampaignPredicateDefinition,
  type CharacterLoss,
  type Crossing,
  type DialogueDefinition,
  type Encounter,
  type EncounterDefinition,
  type ObjectiveKind,
  type SimulationEvent,
  type TurnOrder,
  type UnitBehavior,
  type WeaponLean
} from "./encounter-simulation";
import { terrainMovementCost, terrainPassability } from "./terrain-passability";

export type PlaytestSide = "first" | "second";
export type PlaytestOutcome = "ongoing" | "first_side_won" | "second_side_won";

export interface PlaytestUnit {
  id: string;
  name: string;
  /**
   * What talking to this character records, hashed from the flag their
   * placement authored. Absent is a character no talk may reach, which is what
   * every placement that states no `talk` means.
   */
  talkRecordId?: bigint;
  /**
   * When this character comes in, if not from the opening: the round of its
   * first arrival, the rounds between arrivals and how many it makes. Absent
   * is somebody standing on the board when the battle opens, which is what
   * every placement that states no `arrival` means.
   */
  arrivalRound?: number;
  arrivalEvery?: number;
  arrivalTimes?: number;
  /** The source class this unit renders as; presentation only. */
  classId?: string;
  /**
   * The colour this unit's faction wears; presentation only. Absent when the
   * unit type names no faction, and the board falls back to side order.
   */
  factionColor?: FactionColor;
  /**
   * The style this one character is drawn in and the body it is drawn with,
   * already resolved: the character's own choices where it made them and the
   * game's where it did not. Presentation only, and resolved here rather than
   * on the board for the reason the compiler resolves them rather than the
   * console: the fallback rule belongs in one place, and the board's job is
   * to draw what it is handed.
   */
  characterStyleId: string;
  characterFigureId: string;
  side: PlaytestSide;
  x: number;
  y: number;
  health: number;
  /**
   * Whether this character is standing on the board: alive, not talked off it,
   * and arrived. **Anything asking "is somebody there" reads this rather than
   * testing health**, and it is the engine's own answer read back off the
   * snapshot rather than composed here, the same reason `hasActed` is. A
   * client that spells the board rule itself is a client that can draw a
   * character on a tile the engine refuses every command aimed at.
   *
   * `x` and `y` still say something when this is false, but not what they say
   * when it is true: the tile a departed character left, or the one the content
   * asked a wave to land on rather than one anybody holds.
   */
  onBoard: boolean;
  maximumHealth: number;
  strength: number;
  /** Equipped-weapon power; part of attack damage like strength. */
  power: number;
  defense: number;
  resistance: number;
  /** Raises the chance this character's own strikes land. */
  skill: number;
  /** On this character's side of every hit roll, swinging or swung at. */
  luck: number;
  /** Lowers the chance a strike against this character lands. */
  evasion: number;
  /** The magical counterpart to strength, in a magical cast's damage. */
  magic: number;
  movement: number;
  actionPoints: number;
  speed: number;
  actsAfterAttacking: boolean;
  /**
   * Whether this character has already taken its turn this round. Always false
   * under alternating order, where a round is a turn for each side rather than
   * a pass over the characters. Under `sideBlocks` the engine names no actor,
   * so this is what says who may still be picked, and what draws a spent
   * character as spent rather than hiding them.
   */
  hasActed: boolean;
  /**
   * Whether this character has already spent its turn's one walk. A character
   * walks once per turn however many action points it has.
   */
  hasMoved: boolean;
  /**
   * How much of its budget this character's own turn has spent. Zero under
   * `alternating` and `initiative`, where the side spends one budget at a time
   * and `remainingActionPoints` below is it; under `sideBlocks` this is the
   * only thing that says what a half-spent character has left.
   */
  spentActionPoints: number;
  minimumReach: number;
  maximumReach: number;
  /**
   * What this character adds to the reach of whatever it is holding. Already
   * inside `maximumReach` above, which is the band of the weapon in hand; kept
   * beside it because a sheet drawing a row per carried weapon has to widen
   * each of those bands too, and the weapon records it reads are the shared
   * authored ones rather than this character's.
   *
   * Zero is a character whose reach is exactly its weapon's, which is every
   * character on every board that authors nobody's specificity.
   */
  reachBonus: number;
  /** How often the weapon in hand lands. A hundred always lands. */
  accuracy: number;
  abilityIds: readonly string[];
  /**
   * The weapons the character carries, in the order its unit type lists them,
   * resolved against the project. The first is the weapon in hand and is what
   * `power` and the reach band above describe.
   */
  weapons: readonly PlaytestWeapon[];
  /**
   * The items the character carries, in the order its unit type lists them,
   * with how many of each are left. One of each at the opening bell, which is
   * what the authored list says; the count falls as they are spent.
   */
  items: readonly PlaytestItem[];
  /**
   * What this character leaves behind when it falls, and how often as a whole
   * percentage. Empty and zero is a character that leaves nothing. The name is
   * carried beside the identity so the log can say what fell without searching
   * a pack nobody was holding it in.
   */
  drop?: { id: string; name: string; chance: number };
  /**
   * What this character may cross that open ground is not, read off its class.
   * Empty is a walker. The engine decides what that means for a tile; this is
   * only the authored fact travelling to it.
   */
  crossings: readonly Crossing[];
  behavior: UnitBehavior;
  patrol: readonly { x: number; y: number }[];
}

/**
 * The engine-side campaign cursor a battle reports its outcome to. Which
 * branch follows a battle is a rule, so it lives behind the same boundary as
 * combat; this record only remembers how to talk to it.
 */
export interface CampaignRun {
  handle: Campaign;
  campaignSourceId: string;
  /** Source nodes by the stable identity the compiler assigns them. */
  nodesByStableId: Map<bigint, CampaignNode>;
  /** Set once the cursor has moved past this battle's node. */
  advanced: boolean;
  advanceError: string | undefined;
  /** Whether endPlaytest releases the handle, or a campaign session does. */
  owned: boolean;
}

export interface PlaytestState {
  campaignId: string;
  nodeId: string;
  nodeName: string;
  mapName: string;
  /** The season this project's ground is drawn in; presentation only. */
  themeId: string;
  /** The style this project's characters are drawn in; presentation only. */
  characterStyleId: string;
  /**
   * The body this project's characters are drawn with; presentation only. Both
   * are what a character that named none of its own follows: each unit above
   * carries its own resolved pair.
   */
  characterFigureId: string;
  width: number;
  height: number;
  terrain: string[];
  activeSide: PlaytestSide;
  /**
   * The unit part-way through an activation, or "" when none is committed.
   *
   * **Always "" under `sideBlocks`**, where turns interleave freely and nobody
   * is ever committed to. `remainingActionPoints` is zero there for the same
   * reason: it is what is left of *the* activation, and that order runs no
   * single activation. Ask the character instead, via `pointsLeft`.
   */
  activeUnitId: string;
  remainingActionPoints: number;
  round: number;
  /**
   * How many rounds this battle is won by outlasting, or zero when nothing on
   * it is, which is every board that authors no survive-rounds objective. It
   * comes off the objectives the board was created with, because a snapshot
   * names an objective by identity and result and never says what it is.
   */
  roundsToSurvive: number;
  activationCount: number;
  outcome: PlaytestOutcome;
  terminalNodeName?: string;
  units: PlaytestUnit[];
  events: string[];
  /**
   * Everybody this battle has defeated, by the id of the character on the
   * board, in the order the engine reported them.
   *
   * Kept beside the log rather than read back out of it, because a sentence is
   * for a person and this is for a surface: Play draws a board rather than a
   * log, and the one thing a player must not be able to miss is that somebody
   * of theirs is gone. Scanning prose for the word would be a client parsing
   * its own narration.
   *
   * Health is not the same question. A character at zero is a character the
   * engine took off the board, and reading the list back off the units would
   * work today and stop working the day a rule lets somebody come back up.
   * This is the event the engine actually emitted, and it stays true.
   */
  defeated: string[];
  /**
   * What to call each character on this board, where something outside the
   * board knows better than the board does.
   *
   * A placement has no name of its own: `unitFromPlacement` calls a character
   * by its unit type, so a battle between four Dawn Guards narrates four
   * identical sentences and none of them is about anybody. A campaign does
   * know: the author wrote a name into its roster, and the engine publishes
   * which board unit is which member. So a campaign hands that table down
   * here and every line of the log says who it is about.
   *
   * Absent for a battle nobody brought a roster to, which is what the Browser
   * playtest panel runs and what Play runs for a game with no campaign. Those
   * keep the unit type they always had, because it is genuinely the whole of
   * what is known about the characters on such a board.
   */
  characterNames?: ReadonlyMap<string, string>;
  /**
   * What the campaign behind this board does with a character who falls, so
   * that the log can say it in the rule's own words. Under `permanent` they
   * died. Under `recoverable` they fell, they are out of this battle, and they
   * are coming back after it.
   *
   * Absent for a board nobody brought a roster to, which reads as `permanent`:
   * a battle nobody is keeping a company for has nobody to return anybody to.
   */
  characterLoss?: CharacterLoss;
  encounter: Encounter;
  unitIds: Map<string, bigint>;
  campaign?: CampaignRun | undefined;
  /**
   * Whether the board is still being arranged. While it is, no character may
   * be given an order: the engine refuses every ordinary command as
   * `wrong_phase`, and Play offers the region instead of the action row.
   */
  deploying: boolean;
  /**
   * The region the player arranges in, as board coordinates. Empty for an
   * encounter that authors none, which is every encounter with no phase.
   */
  deploymentTiles: [number, number][];
}

export interface PlaytestStart {
  state?: PlaytestState;
  error?: string;
}

function encounterNode(project: SourceProject): {
  campaignId: string;
  node: CampaignNode;
} | undefined {
  for (const campaign of project.campaigns ?? []) {
    const flow = campaign.flow;
    if (!flow) continue;
    const entry = flow.nodes.find((node) => node.id === flow.entryNodeId);
    if (entry?.kind === "encounter") {
      return { campaignId: campaign.id, node: entry };
    }
    const first = flow.nodes.find((node) => node.kind === "encounter");
    if (first) return { campaignId: campaign.id, node: first };
  }
  return undefined;
}

/**
 * What each character on this board is called, written onto the units.
 *
 * The same three sources in the same order as `sheet::character_name`, which is
 * what every native client asks: the author's own name for the placement, and
 * otherwise the character type with an ordinal only when the board holds more
 * than one of that type. The campaign's name for a roster member outranks both
 * and is applied above this, through `characterNames`, exactly as the join does
 * on a console.
 *
 * This is a second implementation of one rule and that is worth saying plainly:
 * the editor's simulation is TypeScript and cannot call the shared C++, so the
 * agreement is kept by writing the rule the same way and by the ordinal being
 * taken from the same numbers. Ascending *placement identity*, not authoring
 * order. The identities are the ones the content compiler assigns, derived a
 * few lines above from the same source keys, so Play numbers a crowd the way
 * the cartridge does.
 *
 * Case is the one deliberate difference. A console folds every name to
 * upper-case because the font it shares runs 0x20 to 0x5F; a browser has no
 * such limit and shows the author their own words.
 */
function nameTheCharacters(
  project: SourceProject,
  placements: readonly EncounterPlacement[],
  units: PlaytestUnit[],
  unitIds: ReadonlyMap<string, bigint>
): void {
  const kindred = new Map<string, number>();
  for (const placement of placements) {
    kindred.set(
      placement.unitTypeId,
      (kindred.get(placement.unitTypeId) ?? 0) + 1
    );
  }
  for (const unit of units) {
    const placement = placements.find((candidate) => candidate.id === unit.id);
    if (!placement) continue;
    if (placement.name !== undefined && placement.name !== "") {
      unit.name = placement.name;
      continue;
    }
    const kind = project.unitTypes.find(
      (candidate) => candidate.id === placement.unitTypeId
    )?.name ?? unit.name;
    if ((kindred.get(placement.unitTypeId) ?? 0) < 2) {
      unit.name = kind;
      continue;
    }
    const mine = unitIds.get(unit.id);
    if (mine === undefined) continue;
    let ordinal = 1;
    for (const other of placements) {
      if (other.unitTypeId !== placement.unitTypeId) continue;
      const theirs = unitIds.get(other.id);
      if (theirs !== undefined && theirs < mine) ordinal += 1;
    }
    unit.name = `${kind} ${ordinal}`;
  }
}

function unitFromPlacement(
  project: SourceProject,
  placement: EncounterPlacement
): PlaytestUnit | undefined {
  const unitType = project.unitTypes.find((unit) => unit.id === placement.unitTypeId);
  const sourceClass = project.classes.find(
    (candidate) => candidate.id === unitType?.classId
  );
  if (!unitType || !sourceClass) return undefined;
  const colour = factionColor(project.factions ?? [], unitType.factionId);
  // Every weapon the type lists, in that order, which is the same list the
  // native encounter loader carries. An identity the project does not define is
  // dropped rather than handed to the engine, which would refuse the whole
  // encounter for it.
  const weapons = (unitType.startingWeaponIds ?? []).flatMap((weaponId) => {
    const weapon = project.weapons.find(
      (candidate) => candidate.id === weaponId
    );
    return weapon ? [playtestWeapon(weapon)] : [];
  });
  // Every item the type lists, on the same terms as the weapons above. An
  // identity the project does not define is dropped rather than handed to the
  // engine, which would refuse the whole encounter for it.
  const items = (unitType.startingItemIds ?? []).flatMap((itemId) => {
    const item = (project.items ?? []).find(
      (candidate) => candidate.id === itemId
    );
    return item ? [playtestItem(item)] : [];
  });
  // What this type leaves behind. Resolved against the project the same way
  // the pack is, and dropped when the identity does not resolve or the chance
  // is missing. The engine refuses a half-authored pair, and the analyzer has
  // already told the author about it.
  const dropItem = unitType.dropItemId === undefined
    ? undefined
    : (project.items ?? []).find(
        (candidate) => candidate.id === unitType.dropItemId
      );
  const drop = dropItem && unitType.dropChance !== undefined
    ? { id: dropItem.id, name: dropItem.name, chance: unitType.dropChance }
    : undefined;
  // The first carried weapon is the weapon in hand, and its band and power are
  // what the engine resolves for this unit. Data selection only, with no
  // arithmetic, so there is no rule here that could diverge from the engine's.
  const inHand = weapons[0];
  const minimumReach = inHand?.minimumReach ?? 1;
  const maximumReach = inHand?.maximumReach ?? 1;
  const accuracy = inHand?.accuracy ?? 100;
  return {
    id: placement.id,
    name: unitType.name,
    classId: sourceClass.id,
    ...(colour ? { factionColor: colour } : {}),
    characterStyleId: projectCharacterStyle(
      unitType.characterStyleId ?? project.characterStyleId
    ),
    characterFigureId: projectCharacterFigure(
      unitType.characterFigureId ?? project.characterFigureId
    ),
    side: placement.side,
    x: placement.x,
    y: placement.y,
    health: sourceClass.baseStats.health,
    // Standing there until the engine says otherwise. Battle state like the
    // three below it, refreshed off every snapshot; a placement authoring an
    // arrival is corrected to false by the first synchronise, before anything
    // has drawn it.
    onBoard: true,
    maximumHealth: sourceClass.baseStats.health,
    strength: sourceClass.baseStats.strength,
    power: inHand?.power ?? 0,
    defense: sourceClass.baseStats.defense,
    resistance: sourceClass.baseStats.resistance ?? 0,
    skill: sourceClass.baseStats.skill ?? 0,
    luck: sourceClass.baseStats.luck ?? 0,
    evasion: sourceClass.baseStats.evasion ?? 0,
    magic: sourceClass.baseStats.magic ?? 0,
    movement: sourceClass.baseStats.movement,
    actionPoints: sourceClass.baseStats.actionPoints ?? 1,
    speed: sourceClass.baseStats.speed ?? 1,
    actsAfterAttacking: sourceClass.actsAfterAttacking ?? false,
    // Nobody has acted, nobody has walked and nobody has spent a point of
    // their own turn at the opening bell. All three are battle state, refreshed
    // off every snapshot the engine hands back.
    hasActed: false,
    hasMoved: false,
    spentActionPoints: 0,
    minimumReach: minimumReach,
    maximumReach: maximumReach,
    // Nothing the unit type itself can say makes a character reach further.
    // That is a fact about the character rather than about the class, so it
    // arrives from the engine with the rest of the line the engine is holding.
    reachBonus: 0,
    accuracy,
    abilityIds: unitType.abilityIds ?? [],
    weapons,
    items,
    ...(drop ? { drop } : {}),
    crossings: [
      ...(sourceClass.traversal?.flying ? (["flying"] as const) : []),
      ...((sourceClass.traversal?.crossings ?? []) as readonly Crossing[])
    ],
    behavior: (placement.behavior ?? "hold") as UnitBehavior,
    patrol: placement.patrolPoints ?? [],
    // Who can be talked to, off the placement rather than off the unit type,
    // because being talkable is a fact about this character on this board. The
    // flag is hashed here the way every other authored source key is hashed, so
    // the identity the browser plays with is the identity the compiler would
    // have written into the package.
    //
    // Absent is the ordinary case and stays zero, which is a character no talk
    // may reach.
    ...(placement.talk?.flagId
      ? { talkRecordId: stableContentId(placement.talk.flagId) }
      : {}),
    // And when this character comes in, off the placement for the same reason.
    // Absent is the ordinary case: somebody who is here when the battle opens.
    ...(placement.arrival
      ? {
          arrivalRound: placement.arrival.round,
          ...(placement.arrival.every !== undefined
            ? { arrivalEvery: placement.arrival.every }
            : {}),
          ...(placement.arrival.times !== undefined
            ? { arrivalTimes: placement.arrival.times }
            : {})
        }
      : {})
  };
}

function placementUnitTypeId(node: CampaignNode, placementId: string): string {
  return (node.placements ?? []).find((placement) => placement.id === placementId)
    ?.unitTypeId ?? "";
}

// --- Campaign flow mapping --------------------------------------------------
//
// Data mapping only, mirroring tools/game_content's source reader: source keys
// become stable content identities, enum spellings become their compiled
// values. Every flow decision is made by the engine's cursor: combinator
// semantics, branch priorities, which transition follows a battle.

type TransitionCondition = NonNullable<CampaignTransition["when"]>;

interface ConditionRecord {
  kind: string;
  objectiveId?: unknown;
  result?: unknown;
  flagId?: unknown;
  value?: unknown;
  condition?: unknown;
  conditions?: unknown;
}

type PredicateOutcome =
  | { predicate: CampaignPredicateDefinition }
  | { unsupported: string };

function mapPredicate(condition: ConditionRecord): PredicateOutcome {
  if (condition.kind === "worldFlagEquals") {
    // A world flag is campaign state a battle can raise by itself, and talking
    // to somebody does it. What it compares as is the same pair the compiler
    // accepts: boolean and whole number, and nothing else.
    const flagId = stableContentId(String(condition.flagId ?? ""));
    const value = condition.value;
    if (typeof value === "boolean") {
      return { predicate: { flagId, valueType: 1, value: value ? 1n : 0n } };
    }
    if (typeof value === "number" && Number.isInteger(value)) {
      return { predicate: { flagId, valueType: 2, value: BigInt(value) } };
    }
    // The same refusal the content compiler makes: a world flag compares as a
    // boolean or a whole number, and a string has no typed home in campaign
    // state.
    return { unsupported: "a world flag compared against text" };
  }
  if (condition.kind !== "objectiveResult") {
    // `inventoryAtLeast` is the one left: authorable and schema-valid, with no
    // encoding in a package. The same refusal the content compiler makes.
    return { unsupported: `a '${condition.kind}' condition` };
  }
  const objectiveId = stableContentId(String(condition.objectiveId ?? ""));
  if (condition.result === "victory") {
    return { predicate: { objectiveId, result: "satisfied" } };
  }
  if (condition.result === "defeat") {
    return { predicate: { objectiveId, result: "failed" } };
  }
  return { unsupported: `an objective result of '${String(condition.result)}'` };
}

type BranchOutcome =
  | { combinator: CampaignCombinator; predicates: CampaignPredicateDefinition[] }
  | { unsupported: string };

function mapCondition(when: TransitionCondition): BranchOutcome {
  const condition = when as ConditionRecord;
  if (
    condition.kind === "objectiveResult" ||
    condition.kind === "worldFlagEquals"
  ) {
    const mapped = mapPredicate(condition);
    if ("unsupported" in mapped) return mapped;
    return { combinator: "all", predicates: [mapped.predicate] };
  }
  if (condition.kind === "not") {
    const inner = condition.condition as ConditionRecord | undefined;
    if (!inner) return { unsupported: "an empty 'not' condition" };
    const mapped = mapPredicate(inner);
    if ("unsupported" in mapped) return mapped;
    return { combinator: "none", predicates: [mapped.predicate] };
  }
  if (condition.kind === "all" || condition.kind === "any") {
    const nested = Array.isArray(condition.conditions)
      ? (condition.conditions as ConditionRecord[])
      : [];
    const predicates: CampaignPredicateDefinition[] = [];
    for (const entry of nested) {
      const mapped = mapPredicate(entry);
      if ("unsupported" in mapped) return mapped;
      predicates.push(mapped.predicate);
    }
    if (predicates.length === 0) {
      return { unsupported: `an empty '${condition.kind}' condition` };
    }
    return { combinator: condition.kind, predicates };
  }
  return { unsupported: `a '${condition.kind}' condition` };
}

export interface BuiltCampaignFlow {
  definition?: CampaignFlowDefinition;
  nodesByStableId?: Map<bigint, CampaignNode>;
  error?: string;
}

/**
 * One authored scene as the engine's dialogue loader takes it.
 *
 * Two authored names are resolved here, in the one place browser Play has to
 * resolve anything the content compiler would have: the backdrop, and the cast.
 * A scene names its speakers by the strings its lines spell, and the record the
 * engine reads names them by position, so the join between the two is made
 * once, here, exactly as `tools/game_content` makes it while reading source. No
 * client ever compares a speaker string against a cast, which is the whole
 * reason the record carries an index.
 *
 * The rules are the reader's, not this file's invention: the first entry for a
 * speaker wins, a scene casts at most 255 so that a line can name its speaker
 * in one byte, and a line no entry names keeps entry zero, which is what every
 * line of every scene that casts nobody carries.
 */
function castScene(dialogue: SourceDialogue): DialogueDefinition {
  const entryOf = new Map<string, number>();
  const cast: bigint[] = [];
  for (const member of dialogue.cast ?? []) {
    if (entryOf.has(member.speaker) || cast.length === 255) continue;
    cast.push(stableContentId(member.unitTypeId));
    entryOf.set(member.speaker, cast.length);
  }
  return {
    id: stableContentId(dialogue.id),
    name: dialogue.name,
    lines: (dialogue.lines ?? []).map((line) => ({
      speaker: line.speaker,
      text: line.text,
      castEntry: entryOf.get(line.speaker) ?? 0
    })),
    cast,
    backdrop: backdropIndex(dialogue.backgroundId)
  };
}

export function buildCampaignFlow(
  project: SourceProject,
  campaign: SourceCampaign
): BuiltCampaignFlow {
  const flow = campaign.flow;
  if (!flow) return { error: "No campaign Stage is available to play." };
  const nodesByStableId = new Map<bigint, CampaignNode>();
  const nodes: CampaignNodeDefinition[] = [];
  for (const node of flow.nodes) {
    const stableId = stableContentId(node.id);
    nodesByStableId.set(stableId, node);
    const unconditionalTargetIds: bigint[] = [];
    const branches: CampaignBranchDefinition[] = [];
    for (const transition of node.transitions ?? []) {
      const targetNodeId = stableContentId(transition.targetNodeId);
      if (transition.when === undefined) {
        unconditionalTargetIds.push(targetNodeId);
        continue;
      }
      const mapped = mapCondition(transition.when);
      if ("unsupported" in mapped) {
        return {
          error:
            `This story branches on ${mapped.unsupported}, ` +
            "which Play cannot run yet."
        };
      }
      branches.push({
        targetNodeId,
        priority: transition.priority,
        combinator: mapped.combinator,
        predicates: mapped.predicates
      });
    }
    nodes.push({
      id: stableId,
      kind: node.kind,
      encounterId:
        node.kind === "encounter"
          ? stableContentId(`${campaign.id}/${node.id}`)
          : 0n,
      dialogueIds: (node.dialogueIds ?? []).map((id) => stableContentId(id)),
      unconditionalTargetIds,
      branches
    });
  }
  // The company, in the order the compiler writes it: the founding members
  // first, then each node's recruits in flow order. The session assigns
  // one-based persistent identities in exactly this order, so a browser
  // playtest founds the same company a compiled package would.
  const members: CampaignMemberDefinition[] = (campaign.roster ?? []).map(
    (member) => ({
      id: stableContentId(member.id),
      name: member.name,
      unitTypeId: stableContentId(member.unitTypeId),
      joinNodeId: 0n
    })
  );
  for (const node of flow.nodes) {
    for (const recruit of node.recruits ?? []) {
      members.push({
        id: stableContentId(recruit.id),
        name: recruit.name,
        unitTypeId: stableContentId(recruit.unitTypeId),
        joinNodeId: stableContentId(node.id)
      });
    }
  }
  // What the company owns by authoring, in the order the compiler writes it:
  // the founding stock first, then each node's grants in flow order. One table
  // rather than two, because founding a campaign and completing a node put
  // things in the same store by the same operation. The join node is what
  // says which moment did it, and zero is the founding.
  //
  // A grant names an item or a weapon and exactly one identity is written, so
  // the far side is told which without being told how to read a tag. A grant
  // naming neither is refused by the analysis before a playtest can reach
  // here, and is skipped rather than sent as a grant of nothing.
  const grantDefinition = (
    grant: { itemId?: string; weaponId?: string; quantity: number },
    joinNodeId: bigint
  ): CampaignItemGrantDefinition | undefined => {
    if (grant.itemId !== undefined) {
      return {
        itemId: stableContentId(grant.itemId),
        quantity: grant.quantity,
        joinNodeId
      };
    }
    if (grant.weaponId !== undefined) {
      return {
        weaponId: stableContentId(grant.weaponId),
        quantity: grant.quantity,
        joinNodeId
      };
    }
    return undefined;
  };
  const grants: CampaignItemGrantDefinition[] = (campaign.startingStore ?? [])
    .map((stock) => grantDefinition(stock, 0n))
    .filter((grant): grant is CampaignItemGrantDefinition => grant !== undefined);
  for (const node of flow.nodes) {
    for (const grant of node.grants ?? []) {
      const definition = grantDefinition(grant, stableContentId(node.id));
      if (definition !== undefined) grants.push(definition);
    }
  }
  return {
    definition: {
      id: stableContentId(campaign.id),
      name: campaign.name,
      entryNodeId: stableContentId(flow.entryNodeId),
      nodes,
      members,
      grants,
      // The two rules the project states about losing people, resolved here the
      // way the compiler resolves them into every campaign record it writes.
      // Play runs source that was never compiled, so the resolution has to
      // happen somewhere; doing it here, on the record the engine is handed,
      // is what keeps browser Play and a built cartridge playing the same game.
      characterLoss: project.characterLoss ?? "permanent",
      invulnerableForTesting: project.invulnerableForTesting ?? false,
      dialogues: (project.dialogues ?? []).map(castScene)
    },
    nodesByStableId
  };
}

/**
 * Everything one campaign node's board is, before anything is started on it:
 * the characters as the author wrote them, the identities the content compiler
 * would assign them, and the definition the engine takes.
 *
 * Split out from starting a battle because there are two things to do with a
 * board. Play mode's rosterless path creates an encounter from it directly.
 * The campaign path sends it to the campaign session instead, which is the one
 * that decides who is actually on it. It must be the same board either way, or
 * the two would disagree about who the author placed.
 */
export interface EncounterNodePlan {
  error?: string;
  units?: PlaytestUnit[];
  /** Placement id to the stable identity that placement has on this board. */
  unitIds?: Map<string, bigint>;
  /** Placement id to the source-key identity, which is the same every board. */
  sourceKeyIds?: Map<string, bigint>;
  definition?: EncounterDefinition;
  /**
   * How many of the company this node lets take its field, or 0 for a node that
   * caps nothing.
   *
   * Beside the definition rather than inside it, exactly as the engine keeps
   * it: the tiles of a deployment region are a rule the simulation enforces and
   * live in the definition, and a capacity is a campaign judgement the
   * simulation never learns. A board played outside a campaign ignores it, and
   * that is not an omission: a cap on a board with no roster caps nothing.
   */
  deploymentCapacity?: number;
  mapName?: string;
  width?: number;
  height?: number;
  terrain?: string[];
}

export function planEncounterNode(
  project: SourceProject,
  campaignId: string,
  node: CampaignNode
): EncounterNodePlan {
  const map = project.maps.find((candidate) => candidate.id === node.mapId);
  if (!map) return { error: "The Stage does not name a map this game has." };
  const placements = node.placements ?? [];
  const units = placements.map((placement) => unitFromPlacement(project, placement));
  if (units.some((unit) => unit === undefined)) {
    return { error: "Every placement must reference a unit type with a class." };
  }
  if (!units.some((unit) => unit?.side === "first") ||
      !units.some((unit) => unit?.side === "second")) {
    return { error: "The Stage needs at least one character on each side." };
  }
  const typedUnits = units as PlaytestUnit[];
  // Identities must match the ones the content compiler assigns, because they
  // are part of canonical state. Deriving them from the same source keys is
  // what makes the browser's canonical hash equal the native one for the same
  // encounter; synthetic indices would not.
  const encounterKey = `${campaignId}/${node.id}`;
  const unitIds = new Map(
    typedUnits.map((unit) => [unit.id, stableContentId(`${encounterKey}/${unit.id}`)])
  );
  // The identity a roster member is joined to a placement by: the member the
  // placement fields, because a member is the only identity that is the same
  // character on two different maps, which is exactly what the campaign
  // runtime needs it for. A placement that fields nobody keeps its own key,
  // which is what an opposing unit and an encounter played outside a campaign
  // both are.
  const memberIds = new Map(
    (node.placements ?? []).map((placement) => [
      placement.id,
      placement.memberId
    ])
  );
  const sourceKeyIds = new Map(
    typedUnits.map((unit) => [
      unit.id,
      stableContentId(memberIds.get(unit.id) ?? unit.id)
    ])
  );
  nameTheCharacters(project, placements, typedUnits, unitIds);
  const definition: EncounterDefinition = {
    width: map.width,
    height: map.height,
    units: typedUnits.map((unit) => ({
      id: unitIds.get(unit.id)!,
      unitTypeId: stableContentId(placementUnitTypeId(node, unit.id)),
      side: unit.side,
      position: { x: unit.x, y: unit.y },
      health: unit.health,
      strength: unit.strength,
      power: unit.power,
      defense: unit.defense,
      resistance: unit.resistance,
      skill: unit.skill,
      luck: unit.luck,
      evasion: unit.evasion,
      magic: unit.magic,
      movement: unit.movement,
      actionPoints: unit.actionPoints,
      speed: unit.speed,
      actsAfterAttacking: unit.actsAfterAttacking,
      minimumReach: unit.minimumReach,
      maximumReach: unit.maximumReach,
      accuracy: unit.accuracy,
      abilityIds: unit.abilityIds.map((ability) => stableContentId(ability)),
      weaponIds: unit.weapons.map((weapon) => stableContentId(weapon.id)),
      items: unit.items.map((item) => ({
        id: stableContentId(item.id),
        count: item.count
      })),
      ...(unit.drop
        ? {
            dropItemId: stableContentId(unit.drop.id),
            dropChance: unit.drop.chance
          }
        : {}),
      crossings: unit.crossings,
      // Carried rather than re-derived: the mark was hashed once, where the
      // placement was read, so the board the browser plays names the same
      // identity the compiler would have written into the package.
      ...(unit.talkRecordId ? { talkRecordId: unit.talkRecordId } : {}),
      // Carried as authored: the engine expands the recurrence, so the browser
      // and the consoles cannot disagree about what "every three rounds, four
      // times" means.
      ...(unit.arrivalRound !== undefined
        ? {
            arrivalRound: unit.arrivalRound,
            ...(unit.arrivalEvery !== undefined
              ? { arrivalEvery: unit.arrivalEvery }
              : {}),
            ...(unit.arrivalTimes !== undefined
              ? { arrivalTimes: unit.arrivalTimes }
              : {})
          }
        : {})
    })),
    abilities: (project.abilities ?? []).map((ability) => ({
      id: stableContentId(ability.id),
      kind: ability.kind,
      damageType: ability.damageType ?? "physical",
      area: ability.areaShape ?? "single",
      power: ability.power,
      minimumReach: ability.minimumRange,
      maximumReach: ability.maximumRange,
      radius: ability.radius ?? 0,
      accuracy: ability.accuracy ?? 100
    })),
    weapons: project.weapons.map((weapon) => {
      const band = playtestWeapon(weapon);
      return {
        id: stableContentId(weapon.id),
        power: band.power,
        minimumReach: band.minimumReach,
        maximumReach: band.maximumReach,
        accuracy: band.accuracy,
        // Which kind it is, so the triangle can fire here as it does on every
        // other surface. Without it every weapon in a playtest was a weapon of
        // no kind, and an author testing a game whose table they had just
        // written watched it do nothing.
        ...(weapon.weaponTypeId === undefined
          ? {}
          : { weaponType: stableContentId(weapon.weaponTypeId) })
      };
    }),
    // The table itself, folded the way the engine wants it.
    //
    // A project states one `weaponAdvantage` for the whole game and lets each
    // kind name what it beats; the engine carries the pair on every entry,
    // because a blow is priced from the kind in hand and never goes looking for
    // a game-wide setting. That fold is the compiler's job in a shipped
    // package, and it has to be done here too or the browser plays a different
    // game from the one it is compiling. A project with no advantage stated
    // carries no table at all, which is what every game written before there
    // was one means.
    weaponTypes:
      project.weaponAdvantage === undefined
        ? []
        : (project.weaponTypes ?? []).map((kind) => ({
            id: stableContentId(kind.id),
            strongAgainst: (kind.strongAgainst ?? []).map((beaten) =>
              stableContentId(beaten)
            ),
            damage: project.weaponAdvantage!.damage,
            accuracy: project.weaponAdvantage!.accuracy
          })),
    items: (project.items ?? []).flatMap((item) => {
      const kind = item.kind ?? "none";
      return [{
        id: stableContentId(item.id),
        kind,
        power: kind === "restore" ? (item.power ?? 0) : 0
      }];
    }),
    objectives: (node.objectiveIds ?? []).flatMap((objectiveId) => {
      const objective = (project.objectives ?? []).find(
        (candidate) => candidate.id === objectiveId
      );
      if (!objective) return [];
      const kind = objective.kind ?? "defeatAllOpponents";
      const target = objective.targetPlacementId;
      return [{
        id: stableContentId(objective.id),
        kind: (kind === "defeatTarget"
          ? "defeat_target"
          : kind === "protectTarget"
            ? "protect_target"
            : kind === "surviveRounds"
              ? "survive_rounds"
              : "defeat_all_opponents") as ObjectiveKind,
        side: (objective.side ?? "first") as PlaytestSide,
        // Matched on the key the compiler matches on, which for a placement
        // that fields a roster member is *the member's* and not the tile's.
        // `tools/game_content/src/compiler.cpp` says why: the character is who
        // the objective is about, and they are the same character on every
        // board that places them.
        //
        // Keying on the placement's own id instead was enough for every board
        // that happens to name its placement after the member standing on it,
        // and wrong for the one that does not. The Coldgate fields Captain
        // Mirea as `dawn_commander_coldgate` while `keep_mirea_alive` names her
        // by her member id, so the target resolved to nobody, the objective
        // went out with no target on it, and `create_encounter` refused the
        // whole board as `invalid_objective`. The campaign's last Stage could
        // not be played in the browser at all.
        ...(() => {
          if (target === undefined) return {};
          const stands = typedUnits.find(
            (unit) => (memberIds.get(unit.id) ?? unit.id) === target
          );
          return stands === undefined
            ? {}
            : { targetUnitId: unitIds.get(stands.id)! };
        })(),
        ...(kind === "surviveRounds" ? { roundCount: objective.rounds ?? 1 } : {})
      }];
    }),
    // The board's own order, or the game's default when it states none. That
    // is the rule the compiler applies, read from the one place stating it. The
    // source spells the ordered mode in camelCase and the engine uses its own
    // enumeration, so what remains here is a name mapping, not a rule.
    turnOrder: (() => {
      const resolved = resolveTurnOrder(project, node);
      return (resolved === "sideBlocks" ? "side_blocks" : resolved) as TurnOrder;
    })(),
    // What the ground asks, resolved from each cell's authored name by the
    // same convention the content compiler applies. The engine decides who may
    // stand where; this only tells it what the board is made of.
    terrain: map.terrain.map((cell) => terrainPassability(cell)),
    // And what the ground charges, resolved from the same name by the same
    // convention. Sent whatever it comes to, including a board priced at one
    // everywhere: the engine reads a board with no price on it as exactly that,
    // so an all-ones list and no list are the same battle and the same hash.
    movementCost: map.terrain.map((cell) => terrainMovementCost(cell)),
    // The region the player arranges in, exactly as the node states it. Omitted
    // is an empty list, which is a board with no deployment phase: every board
    // that says nothing here.
    deploymentTiles: (node.deployment?.tiles ?? []).map((tile) => ({
      x: tile.x,
      y: tile.y
    }))
  };
  return {
    units: typedUnits,
    unitIds,
    sourceKeyIds,
    definition,
    // A maximum, never a quota: the roster refuses a company that would take
    // more than this to the field, and it is the player who answers by
    // benching somebody. Nothing here trims anybody to fit.
    deploymentCapacity: node.deployment?.capacity ?? 0,
    mapName: map.name,
    width: map.width,
    height: map.height,
    terrain: map.terrain
  };
}

/**
 * Assembles the playable state around a board the engine is already holding.
 *
 * `keep` names the units actually on it. A campaign leaves the permanently
 * dead off, so a plan's character who is not on the board is dropped here
 * rather than shown standing on a square nobody occupies.
 */
export function adoptEncounterNode(
  project: SourceProject,
  campaignId: string,
  node: CampaignNode,
  plan: EncounterNodePlan,
  encounter: Encounter,
  keep: ReadonlySet<bigint> | undefined,
  campaign: CampaignRun | undefined
): PlaytestState {
  const unitIds = new Map(plan.unitIds!);
  const units = plan.units!.filter((unit) => {
    const id = unitIds.get(unit.id);
    if (id === undefined) return false;
    if (keep !== undefined && !keep.has(id)) {
      unitIds.delete(unit.id);
      return false;
    }
    return true;
  });
  const state: PlaytestState = {
    campaignId,
    nodeId: node.id,
    nodeName: node.name,
    mapName: plan.mapName!,
    themeId: projectTheme(project.themeId),
    characterStyleId: projectCharacterStyle(project.characterStyleId),
    characterFigureId: projectCharacterFigure(project.characterFigureId),
    width: plan.width!,
    height: plan.height!,
    terrain: plan.terrain!,
    activeSide: "first",
    activeUnitId: "",
    remainingActionPoints: 0,
    round: 0,
    // Off the objectives this board was created with, not off the snapshot: a
    // snapshot names an objective by identity and result and never says what it
    // is. Zero on every board that authors no survive-rounds objective, which
    // is what keeps the status line saying exactly what it always said.
    roundsToSurvive:
      (project.objectives ?? []).find(
        (objective) =>
          (node.objectiveIds ?? []).includes(objective.id) &&
          objective.kind === "surviveRounds"
      )?.rounds ?? 0,
    activationCount: 0,
    outcome: "ongoing",
    units,
    events: ["Encounter started. Your side acts."],
    defeated: [],
    encounter,
    unitIds,
    campaign,
    deploying: false,
    deploymentTiles: []
  };
  // An ordered turn names its first actor at creation, and a board with a
  // deployment region opens in the phase rather than on an activation at all,
  // so read the opening snapshot rather than assuming either.
  synchronize(state);
  if (state.deploying) {
    state.events = ["Deployment. Stand your line, then begin the fighting."];
  }
  return state;
}

/** Starts the encounter a specific campaign node authors. */
function startEncounterNode(
  project: SourceProject,
  campaignId: string,
  node: CampaignNode,
  campaign: CampaignRun | undefined
): PlaytestStart {
  const plan = planEncounterNode(project, campaignId, node);
  if (plan.error !== undefined || !plan.definition) {
    return { error: plan.error ?? "The Stage could not be prepared." };
  }
  const created = createEncounter(plan.definition);
  if (created.error !== "none") {
    return { error: `The Stage cannot start: ${created.error}.` };
  }
  return {
    state: adoptEncounterNode(
      project,
      campaignId,
      node,
      plan,
      created.encounter,
      undefined,
      campaign
    )
  };
}

/**
 * Starts the first encounter the campaign cursor can actually reach, skipping
 * story nodes the way the native client session does. The cursor stays
 * attached so the after-battle transition is the engine's decision.
 *
 * A flow the campaign loader refuses degrades to the first authored encounter
 * without a cursor: the skirmish is still playable, and rather than guessing
 * at transitions, the log says why the campaign will not advance.
 */
export function startPlaytest(project: SourceProject): PlaytestStart {
  if (!isEncounterEngineReady()) {
    return { error: "The game engine is still loading. Try again in a moment." };
  }
  let flowProblem: string | undefined;
  for (const campaign of project.campaigns ?? []) {
    if (!campaign.flow) continue;
    const built = buildCampaignFlow(project, campaign);
    if (!built.definition || !built.nodesByStableId) {
      flowProblem = built.error;
      continue;
    }
    const created = createCampaign(built.definition);
    if (created.error !== "none") {
      flowProblem = `The campaign flow could not be loaded: ${created.error}.`;
      continue;
    }
    const handle = created.campaign;
    let current = handle.state();
    for (let guard = 0; current.kind === "story" && guard < 256; guard += 1) {
      if (handle.advanceStory() !== "none") break;
      current = handle.state();
    }
    if (current.kind !== "encounter") {
      handle.dispose();
      continue;
    }
    const node = built.nodesByStableId.get(current.nodeId);
    if (!node) {
      handle.dispose();
      continue;
    }
    const started = startEncounterNode(project, campaign.id, node, {
      handle,
      campaignSourceId: campaign.id,
      nodesByStableId: built.nodesByStableId,
      advanced: false,
      advanceError: undefined,
      owned: true
    });
    if (started.error !== undefined) handle.dispose();
    return started;
  }
  const selection = encounterNode(project);
  if (!selection) {
    return { error: flowProblem ?? "No campaign Stage is available to play." };
  }
  const started = startEncounterNode(
    project,
    selection.campaignId,
    selection.node,
    undefined
  );
  if (started.state && flowProblem !== undefined) {
    started.state.events.unshift(
      `${flowProblem} The Stage can be played, but the story will not advance.`
    );
  }
  return started;
}

/**
 * Releases the engine-side encounter behind a playtest state. The state must
 * not be used afterwards. Callers that start a new encounter should end the
 * previous one so the engine's handle table stays small.
 */
export function endPlaytest(state: PlaytestState | undefined): void {
  state?.encounter.dispose();
  if (state?.campaign?.owned) state.campaign.handle.dispose();
}

/**
 * Plays one activation for the side nobody is steering.
 *
 * Returns false when no unattended unit could act, which is the caller's signal
 * to stop rather than loop. The engine validates every proposal, so a rejected
 * plan falls back to waiting rather than stalling the encounter.
 */
export function takeAutomaticTurn(
  project: SourceProject,
  state: PlaytestState,
  side: PlaytestSide
): boolean {
  if (state.outcome !== "ongoing" || state.activeSide !== side) return false;
  for (const unit of state.units) {
    if (unit.side !== side || !unit.onBoard) continue;
    const id = state.unitIds.get(unit.id);
    if (id === undefined) continue;
    // Somebody who has already finished their turn this round is not a
    // candidate. It matters under `sideBlocks`, where turns interleave and a
    // character that has only walked is still owed an action: without this the
    // loop would keep proposing the block's first character and be refused.
    if (unit.hasActed) continue;
    // An ordered turn commits to one unit; nobody else may act in its place.
    // Inert under `sideBlocks`, which commits to nobody.
    if (state.activeUnitId !== "" && state.activeUnitId !== unit.id) continue;
    const plan = state.encounter.decide(id, unit.behavior, unit.patrol);
    if (!plan) continue;
    let result = state.encounter.apply(plan);
    if (result.error !== "none") {
      result = state.encounter.apply({ type: "wait", unitId: id });
    }
    if (result.error !== "none") continue;
    synchronize(state);
    narrate(state, result.events);
    advanceCampaign(project, state);
    return true;
  }
  return false;
}

function distance(lhs: PlaytestUnit, x: number, y: number): number {
  return Math.abs(lhs.x - x) + Math.abs(lhs.y - y);
}

function synchronize(state: PlaytestState) {
  const snapshot = state.encounter.snapshot();
  state.deploying = snapshot.deploying;
  state.deploymentTiles = snapshot.deploymentTiles.map(
    (tile): [number, number] => [tile.x, tile.y]
  );
  state.activeSide = snapshot.activeSide;
  state.remainingActionPoints = snapshot.remainingActionPoints;
  state.round = snapshot.round;
  state.activeUnitId = snapshot.activeUnitId === 0n
    ? ""
    : [...state.unitIds.entries()]
        .find(([, id]) => id === snapshot.activeUnitId)?.[0] ?? "";
  state.activationCount = Number(snapshot.activationCount);
  state.outcome = snapshot.outcome;
  for (const unit of state.units) {
    const id = state.unitIds.get(unit.id);
    const current = snapshot.units.find((candidate) => candidate.id === id);
    if (!current) continue;
    unit.x = current.position.x;
    unit.y = current.position.y;
    unit.health = current.health;
    // Whether this one is standing on the board, straight off the engine's own
    // predicate. Read back rather than composed from the health, departure and
    // arrival it folds, for the reason `hasActed` below is: a client keeping
    // its own copy of a board rule is a client that can disagree with the board
    // about who is there.
    unit.onBoard = current.onBoard;
    // Whether this one has already taken its turn. The engine's own state, read
    // back rather than tracked here, for the reason the pack below is: a client
    // keeping its own copy is a client that can disagree with the board about
    // who may still be given orders.
    unit.hasActed = current.hasActed;
    unit.hasMoved = current.hasMoved;
    unit.spentActionPoints = current.spentActionPoints;
    // The stat line the engine is actually holding, rather than the one the
    // author wrote. They are the same number for every battle nobody brought a
    // campaign to; they differ by exactly what a level-up granted for one that
    // somebody did, and a sheet showing the authored four while the engine
    // fights with five would be a sheet disagreeing with the battle.
    unit.maximumHealth = current.maximumHealth;
    unit.strength = current.strength;
    unit.defense = current.defense;
    unit.resistance = current.resistance;
    unit.skill = current.skill;
    unit.luck = current.luck;
    unit.evasion = current.evasion;
    unit.magic = current.magic;
    unit.movement = current.movement;
    unit.actionPoints = current.actionPoints;
    // And what this character adds to the reach of what it holds, for the same
    // reason as the stats above: it is the engine's number, composed from what
    // an author wrote about the character rather than about its class, and a
    // sheet drawing the authored bow's band while the engine strikes a tile
    // further would be a sheet disagreeing with the battle.
    unit.reachBonus = current.reachBonus;
    // The pack comes back from the engine rather than being decremented here:
    // the count is the engine's state, and a client keeping its own copy is a
    // client that can disagree about how many draughts are left.
    //
    // A slot the engine is not holding is dropped rather than kept at the
    // count the unit type would have given it. In a direct playtest the engine
    // holds every slot the type lists and nothing is ever dropped; in a
    // campaign the satchel is the campaign's, so a character who drank their
    // last draught in an earlier battle takes the field without it, and a
    // screen still offering it would be offering something the engine would
    // refuse.
    unit.items = unit.items.flatMap((slot) => {
      const held = current.items.find(
        (candidate) => candidate.id === stableContentId(slot.id)
      );
      return held === undefined ? [] : [{ ...slot, count: held.count }];
    });
  }
}

/**
 * Everything one command did, put in front of the log a reader watches, and,
 * on the way past, the record of who it buried.
 *
 * The log is newest-first, which is why the descriptions go on the front in
 * reverse: within one command the engine's own order is the order things
 * happened, so reversing it leaves the last thing that happened at the top.
 *
 * The two things this does belong together because they are one pass over one
 * list. Spelling the unshift out in every command would be nine copies of it,
 * and a tenth caller that forgot would be a command whose events nobody was
 * told about at all.
 */
function narrate(
  state: PlaytestState,
  events: readonly SimulationEvent[]
): void {
  for (const event of events) {
    if (event.type !== "unit_defeated") continue;
    const who = state.units.find(
      (unit) => state.unitIds.get(unit.id) === event.unitId
    );
    // A character the board no longer lists is one a campaign left off it, so
    // there is nobody here to have died; and a defeat is reported once, so the
    // guard against saying it twice costs nothing and rules out a duplicate no
    // reader could explain.
    if (!who || state.defeated.includes(who.id)) continue;
    state.defeated.push(who.id);
  }
  state.events.unshift(
    ...events.map((event) => describeEvent(state, event)).reverse()
  );
}

/**
 * What the campaign behind this board calls this character going down. A death
 * under the permanent rule and a fall under the recoverable one: the same
 * event, two vocabularies, and which one is in force belongs to the campaign
 * rather than to this log.
 *
 * The softer word belongs to the company and to nobody else. A campaign that
 * carries its own people off the field does not carry the bandit off with them,
 * and a log that said the bandit fell would be promising a reader they were
 * going to meet him again. `characterNames` holds exactly the placements a
 * roster member stands in, so it is also the answer to "is this one of ours".
 */
function fallWord(state: PlaytestState, unitId: bigint): string {
  if (state.characterLoss !== "recoverable") return "died.";
  const unit = state.units.find(
    (candidate) => state.unitIds.get(candidate.id) === unitId
  );
  const ours = unit !== undefined && state.characterNames?.has(unit.id) === true;
  return ours ? "fell." : "died.";
}

function describeEvent(state: PlaytestState, event: SimulationEvent): string {
  // The campaign's name for this character where there is a campaign standing
  // behind the board, and what the character is where there is not. Never both:
  // the two answer the same question, and a log that said "Outrider Bevan (Dawn
  // Guard)" on every line would be a log nobody reads twice.
  const name = (id: bigint) => {
    const unit = state.units.find(
      (candidate) => state.unitIds.get(candidate.id) === id
    );
    if (!unit) return "Unit";
    return state.characterNames?.get(unit.id) ?? unit.name;
  };
  switch (event.type) {
    case "unit_moved":
      return `${name(event.unitId)} moved to ${event.position.x}, ${event.position.y}.`;
    case "unit_waited":
      return `${name(event.unitId)} waited.`;
    case "unit_damaged":
      return `${name(event.relatedUnitId)} dealt ${event.amount} damage to ${name(event.unitId)}.`;
    case "attack_missed":
      return `${name(event.relatedUnitId)} missed ${name(event.unitId)}.`;
    // "died", not "was defeated". A defeat is what the engine calls the event
    // and this is what a person calls what happened. Which of the two things
    // happened is the campaign's to say. Under the permanent rule it is
    // a death and a player is owed the word; under the recoverable rule they
    // fell, they are out of this battle, and they are coming back, so saying
    // they died would be a lie the aftermath screen contradicts three lines
    // later.
    case "unit_defeated":
      return `${name(event.unitId)} ${fallWord(state, event.unitId)}`;
    // And the blow that did not do it, where the content asked for a floor
    // under everybody's health. It follows the damage line rather than
    // replacing it: the engine emits both, in that order, so a reader is told
    // what was taken before being told it was not enough. It is deliberately
    // not spelled like a defeat, because somebody who held on is still
    // standing there to be given an order.
    case "unit_endured":
      return `${name(event.unitId)} held on.`;
    case "unit_restored":
      return `${name(event.relatedUnitId)} restored ${event.amount} health to ${name(event.unitId)}.`;
    case "activation_ended":
      return `${name(event.unitId)} finished acting.`;
    case "item_used": {
      const item = state.units
        .flatMap((unit) => unit.items)
        .find((candidate) => stableContentId(candidate.id) === event.itemId);
      return `${name(event.unitId)} used ${item?.name ?? "an item"}.`;
    }
    // What fell is named from the fallen character's own authored drop rather
    // than from anybody's pack: a drop enters no pack, so there is no satchel
    // to look it up in.
    case "item_dropped": {
      const fallen = state.units.find(
        (candidate) => stableContentId(candidate.id) === event.unitId
      );
      const what = fallen?.drop?.name ?? "something";
      return `${name(event.unitId)} left ${what} behind, claimed by ` +
        `${name(event.relatedUnitId)}.`;
    }
    case "unit_deployed":
      return `${name(event.unitId)} took position at ${event.position.x}, ` +
        `${event.position.y}.`;
    case "deployment_ended":
      return "The line is set. The fighting begins.";
    // "left the field", never "was defeated". Departure and death are two
    // different facts, and this line is where a reader of the log is told
    // which one happened.
    case "unit_talked":
      return `${name(event.relatedUnitId)} talked ${name(event.unitId)} ` +
        "off the field.";
    // The tile is the one the wave actually took, which is the tile the content
    // asked for or the nearest one it could stand on when somebody was holding
    // that one, so a reader of the log is never told a tile nobody is on.
    case "unit_arrived":
      return `${name(event.unitId)} arrived at ${event.position.x}, ` +
        `${event.position.y} as round ${event.amount} began.`;
    case "encounter_completed":
      return `${event.outcome === "first_side_won" ? "Your side" : "The enemy"} won.`;
  }
}

/**
 * What this character has left of its own turn.
 *
 * The engine's own rule, restated on the terms the engine states it: the
 * side-wide budget while this character is the one holding an activation, none
 * once it has finished, and its own budget less what its own turn has spent
 * otherwise. Under `sideBlocks` nobody ever holds an activation, so the last
 * clause is the whole of the answer and several characters can each be
 * part-way through a turn at once.
 */
export function pointsLeft(state: PlaytestState, unitId: string): number {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit) return 0;
  if (state.activeUnitId === unitId) return state.remainingActionPoints;
  if (unit.hasActed) return 0;
  const budget = unit.actionPoints === 0 ? 1 : unit.actionPoints;
  return Math.max(0, budget - unit.spentActionPoints);
}

/** Whether this unit may issue a command right now. */
export function canAct(state: PlaytestState, unitId: string): boolean {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit || !unit.onBoard || unit.side !== state.activeSide) return false;
  if (state.outcome !== "ongoing") return false;
  // Nobody acts before the battle begins. The engine says the same thing with
  // `wrong_phase`; saying it here keeps Play from offering a menu that would
  // be refused on every row.
  if (state.deploying) return false;
  // Somebody who has already finished their turn this round may not take
  // another. The engine says the same thing with `already_acted`; saying it
  // here is what lets Play draw a spent character as spent rather than offering
  // a menu whose every row would be refused.
  if (unit.hasActed) return false;
  // Under `alternating` and `initiative` a side commits to one character per
  // activation and nobody else may interrupt it. Under `sideBlocks` there is no
  // such commitment, and `activeUnitId` is always "" there, so a character that
  // has walked with a point still in hand stays commandable, and so does
  // everybody else on its side. Reading the commitment as unconditional is
  // what greys out a whole company because one of them has taken a step.
  if (state.activeUnitId !== "" && state.activeUnitId !== unitId) return false;
  return pointsLeft(state, unitId) > 0;
}

/**
 * Where the board offers to send this character.
 *
 * The tiles are the engine's own answer: `reachable_tiles`, the very traversal
 * `apply` judges a move against. A lit square and an accepted move can
 * never disagree. A TypeScript re-implementation of the movement rule
 * here would be the kind of duplicate that drifts.
 *
 * `canAct` stays here because it is a different question: not *which* tiles
 * are legal, but whether this character may be given orders at all. The
 * engine would refuse the move anyway; offering none is the honest board.
 */
export function legalMoves(state: PlaytestState, unitId: string): [number, number][] {
  if (!canAct(state, unitId)) return [];
  const engineId = state.unitIds.get(unitId);
  if (engineId === undefined) return [];
  return state.encounter
    .reachableTiles(engineId)
    .map((tile): [number, number] => [tile.x, tile.y]);
}

/**
 * Where the board offers to stand this character before the battle begins.
 *
 * The engine's own answer: `deployable_tiles`, the very judgement `apply` makes
 * of a deploy, for the reason `legalMoves` is. A lit square and an
 * accepted command can never disagree. Empty once the phase closes, and empty
 * for a character the author pinned outside the region.
 */
export function deployableTiles(
  state: PlaytestState,
  unitId: string
): [number, number][] {
  if (!state.deploying) return [];
  const engineId = state.unitIds.get(unitId);
  if (engineId === undefined) return [];
  return state.encounter
    .deployableTiles(engineId)
    .map((tile): [number, number] => [tile.x, tile.y]);
}

/** Whether the player arranges this character before the battle begins. */
export function canDeploy(state: PlaytestState, unitId: string): boolean {
  return deployableTiles(state, unitId).length > 0;
}

/**
 * Stands one character on one tile of the region. Costs no action point and
 * may be done as many times as the player likes; only `beginBattle` is
 * irreversible.
 */
export function deployUnit(
  state: PlaytestState,
  unitId: string,
  x: number,
  y: number
): boolean {
  const engineId = state.unitIds.get(unitId);
  if (engineId === undefined) return false;
  const result = state.encounter.apply({
    type: "deploy",
    unitId: engineId,
    destination: { x, y }
  });
  if (result.error !== "none") return false;
  synchronize(state);
  narrate(state, result.events);
  return true;
}

/** Closes the deployment phase and opens the battle. */
export function beginBattle(state: PlaytestState): boolean {
  const result = state.encounter.apply({ type: "begin_battle" });
  if (result.error !== "none") return false;
  synchronize(state);
  narrate(state, result.events);
  return true;
}

/**
 * Every tile a side could reach and strike this turn: the engine's danger
 * zone, movement plus weapon band, honouring minimum reach. Shown so a player
 * can see where it is unsafe to stand, the way the console shows it.
 *
 * A warning about capability rather than a promise about the next activation:
 * turn order and action points are deliberately ignored, and the query says
 * nothing about which of those strikes anyone will actually choose.
 */
export function dangerTiles(
  state: PlaytestState,
  side: PlaytestSide
): [number, number][] {
  return state.encounter
    .dangerTiles(side)
    .map((tile): [number, number] => [tile.x, tile.y]);
}

/**
 * One ability a selected character could cast right now, named the way the
 * author named it. Presentation and selection only: the engine still decides
 * whether the cast is accepted.
 */
export interface PlaytestAbility {
  id: string;
  name: string;
  kind: "damage" | "restore";
  minimumReach: number;
  maximumReach: number;
}

/**
 * The abilities a character knows, in the order its unit type lists them,
 * the same order the console's action menu offers them in, so a player who
 * learns one client can read the other. An ability the project no longer
 * defines is dropped rather than offered as a command the engine would refuse.
 */
export function abilitiesFor(
  project: SourceProject,
  state: PlaytestState,
  unitId: string
): PlaytestAbility[] {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit || !canAct(state, unitId)) return [];
  return unit.abilityIds.flatMap((abilityId) => {
    const ability = (project.abilities ?? []).find(
      (candidate) => candidate.id === abilityId
    );
    if (!ability) return [];
    return [{
      id: ability.id,
      name: ability.name,
      kind: ability.kind,
      minimumReach: ability.minimumRange,
      maximumReach: ability.maximumRange
    }];
  });
}

/**
 * Every tile a cast could be aimed at.
 *
 * The engine's own answer: `aimable_tiles` for a cast, which is the very
 * judgement `apply` makes of an ability command, for the reason `legalMoves`
 * is the engine's. A lit square and an accepted command can never disagree.
 * Walking the board here in TypeScript, filtering on a reach band copied out
 * of the engine, would be the kind of duplicate that drifts, and it would
 * copy the band alone: a spell the caster does not know, a turn already spent
 * and a character on the side that is not acting are all rules such a copy
 * would have to keep a second time or get wrong.
 *
 * An ability is aimed at a tile, not at a character, so an empty square is a
 * valid aim; that is what makes an area worth having, and the engine lights
 * empty ground for exactly that reason.
 *
 * `project` is not read. It stays in the signature because every caller
 * holds one and the sibling helpers beside this take it, so dropping it would
 * churn Play's call sites to say nothing new.
 */
export function legalCastTiles(
  project: SourceProject,
  state: PlaytestState,
  unitId: string,
  abilityId: string
): [number, number][] {
  void project;
  const engineId = state.unitIds.get(unitId);
  if (engineId === undefined) return [];
  return state.encounter
    .aimableTiles(engineId, {
      kind: "cast",
      abilityId: stableContentId(abilityId)
    })
    .map((tile): [number, number] => [tile.x, tile.y]);
}

/**
 * One item a character carries, named the way the author named it, with how
 * many are left. Presentation and selection only: the engine still decides
 * whether spending it is accepted.
 */
export interface PlaytestItem {
  id: string;
  name: string;
  kind: "none" | "restore";
  /** How much a restoring item gives back, before the clamp. */
  power: number;
  /** How many are left. Zero is spent, and the row stays to say so. */
  count: number;
}

/** One item as authored, at the count a character opens a battle carrying. */
function playtestItem(item: SourceItem): PlaytestItem {
  const kind = item.kind ?? "none";
  return {
    id: item.id,
    name: item.name,
    kind,
    power: kind === "restore" ? item.power ?? 0 : 0,
    count: 1
  };
}

/**
 * The items a character can spend right now, in the order its unit type lists
 * them: the row the console action menu keeps between the spells and WAIT. An
 * item the project no longer defines never reaches this list, and one that has
 * run out or does nothing is offered as a row the client disables rather than
 * hidden, so a player can see what was drunk.
 */
export function itemsFor(
  state: PlaytestState,
  unitId: string
): PlaytestItem[] {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit || !canAct(state, unitId)) return [];
  return [...unit.items];
}

/** Somebody this character could talk off the board right now. */
export interface PlaytestTalkTarget {
  id: string;
  name: string;
}

/**
 * Who this character could talk to right now: the row the console action menu
 * keeps between the pack and WAIT, one entry per neighbour rather than one row
 * the player then aims, because a browser has the room to name them.
 *
 * Every candidate is put to the engine's own `forecastTalk`, which is the
 * judgement `apply` makes: adjacency, an authored record, still standing, not
 * already departed. Nothing about who is talkable is decided here, so the
 * buttons on screen and the commands they send cannot disagree. No shipped
 * project authors a talk, so this is empty everywhere today.
 */
export function talkTargets(
  state: PlaytestState,
  unitId: string
): PlaytestTalkTarget[] {
  const engineUnit = state.unitIds.get(unitId);
  if (engineUnit === undefined || !canAct(state, unitId)) return [];
  const targets: PlaytestTalkTarget[] = [];
  for (const candidate of state.units) {
    if (candidate.id === unitId) continue;
    const engineOther = state.unitIds.get(candidate.id);
    if (engineOther === undefined) continue;
    const forecast = state.encounter.forecastTalk(engineUnit, engineOther);
    if (forecast.error !== "none") continue;
    // Named from `departingId` rather than from the candidate the loop is
    // holding: who would actually leave is the forecast's answer, and a button
    // labelled with anything else would be promising something the engine did
    // not say.
    const departing = state.units.find(
      (other) => state.unitIds.get(other.id) === forecast.departingId
    );
    if (departing === undefined) continue;
    targets.push({ id: departing.id, name: departing.name });
  }
  return targets;
}

/**
 * Talks the named neighbour off the board. Costs an action point and closes the
 * activation exactly as a strike does, and takes the other character away
 * alive. A departure, not a defeat, and the events say which.
 */
export function talk(
  project: SourceProject,
  state: PlaytestState,
  unitId: string,
  targetId: string
): boolean {
  const engineUnit = state.unitIds.get(unitId);
  const engineTarget = state.unitIds.get(targetId);
  if (engineUnit === undefined || engineTarget === undefined) return false;
  const result = state.encounter.apply({
    type: "talk",
    unitId: engineUnit,
    targetId: engineTarget
  });
  if (result.error !== "none") return false;
  synchronize(state);
  narrate(state, result.events);
  advanceCampaign(project, state);
  return true;
}

/**
 * One weapon a character carries, named the way the author named it.
 * Presentation and selection only: the engine still decides whether a strike
 * with it is accepted.
 */
export interface PlaytestWeapon {
  id: string;
  name: string;
  power: number;
  minimumReach: number;
  maximumReach: number;
  /** How often it lands, as a whole percentage. A hundred always lands. */
  accuracy: number;
}

/**
 * The band precedence the content compiler applies: an explicit band wins, and
 * `range` alone is the legacy spelling for one to range.
 */
function playtestWeapon(weapon: SourceWeapon): PlaytestWeapon {
  const banded =
    weapon.minimumRange !== undefined || weapon.maximumRange !== undefined;
  const minimumReach = banded ? weapon.minimumRange ?? 1 : 1;
  return {
    id: weapon.id,
    name: weapon.name,
    power: weapon.power,
    minimumReach,
    maximumReach: banded
      ? weapon.maximumRange ?? minimumReach
      : weapon.range ?? 1,
    // Absent means certain, which is the one default a missing accuracy could
    // safely have: zero would be a weapon that never connects.
    accuracy: weapon.accuracy ?? 100
  };
}

/**
 * The weapons a character carries, in the order its unit type lists them, the
 * same order the console's action menu offers them in. A character carrying
 * one weapon is offered nothing to choose between, which is what keeps a plain
 * attack a single tap.
 */
export function weaponsFor(
  state: PlaytestState,
  unitId: string
): PlaytestWeapon[] {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit || !canAct(state, unitId)) return [];
  return unit.weapons.length > 1 ? [...unit.weapons] : [];
}

/** One carried weapon, as the information sheet states it. */
export interface PlaytestSheetWeapon {
  name: string;
  minimumReach: number;
  maximumReach: number;
  /** The weapon's own accuracy, which is where a hit roll starts. */
  accuracy: number;
}

/** One carried item, as the information sheet states it. */
export interface PlaytestSheetItem {
  name: string;
  /** How many are left; the number the menu row shows. */
  count: number;
  /** What spending it gives back, or zero when it does nothing in a battle. */
  restores: number;
}

/** One known ability, as the information sheet states it. */
export interface PlaytestSheetAbility {
  name: string;
  minimumReach: number;
  maximumReach: number;
}

/**
 * Everything a character is, in one place: the sheet the unit action menu's
 * INFO row opens on every client.
 *
 * The fields are the console sheet's fields, in the console sheet's order.
 * See `platform/sheet/README.md`, which is where the same sheet is composed for
 * the two consoles and the terminal. A player who learns to read one client's
 * sheet can read another's.
 *
 * Nothing here is computed. Every number is the one the engine gave this unit
 * or the one the project authored on the weapon or the ability. The one number
 * a reader might expect and will not find is the chance a strike lands: that
 * folds this character's skill and luck against the *target's* evasion and
 * luck, so it needs a target, and it is what `strikeChance` reports when there
 * is one.
 */
export interface PlaytestUnitSheet {
  name: string;
  className: string;
  health: number;
  maximumHealth: number;
  /** What is left this activation for the character that is acting. */
  actionPoints: number;
  movement: number;
  speed: number;
  strength: number;
  defense: number;
  resistance: number;
  magic: number;
  skill: number;
  luck: number;
  evasion: number;
  weapons: PlaytestSheetWeapon[];
  abilities: PlaytestSheetAbility[];
  items: PlaytestSheetItem[];
}

/**
 * The sheet for one character, or undefined when the battle does not have it.
 *
 * Unlike `abilitiesFor` and `weaponsFor`, this does not ask whether the
 * character can act: a sheet is something to read, and a character that has
 * already spent its turn is exactly the one a player wants to read about
 * before deciding what the next one does.
 */
export function unitSheet(
  project: SourceProject,
  state: PlaytestState,
  unitId: string
): PlaytestUnitSheet | undefined {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit) return undefined;
  const sourceClass = (project.classes ?? []).find(
    (candidate) => candidate.id === unit.classId
  );
  return {
    name: unit.name,
    className: sourceClass?.name ?? unit.name,
    health: unit.health,
    maximumHealth: unit.maximumHealth,
    // The console rule, so the two agree: what this character has left of its
    // own turn, which under `sideBlocks` is its budget less what its own turn
    // has spent rather than anything the side is holding.
    actionPoints: pointsLeft(state, unitId),
    movement: unit.movement,
    speed: unit.speed,
    strength: unit.strength,
    defense: unit.defense,
    resistance: unit.resistance,
    magic: unit.magic,
    skill: unit.skill,
    luck: unit.luck,
    evasion: unit.evasion,
    // The band this character strikes at with each weapon, which is the
    // weapon's band with this character's own reach bonus on its ceiling. The
    // weapon record is the authored bow, shared by everybody carrying one; the
    // bonus is the fact about the archer, and a row that printed the record
    // alone would tell a player written to shoot three tiles that she shoots
    // two. The floor is the weapon's own: the bonus raises a ceiling and never
    // lowers a floor, so an archer written to outrange a swordsman still
    // cannot answer one standing on top of her.
    //
    // The same widening the console sheet does, in the same place, saturating
    // at the widest band a byte can hold. See `platform/sheet/src/unit_sheet.cpp`
    // and `widened_reach` in `engine/simulation/src/encounter.cpp`, which is
    // where the rule lives.
    weapons: unit.weapons.map((weapon) => ({
      name: weapon.name,
      minimumReach: weapon.minimumReach,
      maximumReach: Math.min(255, weapon.maximumReach + unit.reachBonus),
      accuracy: weapon.accuracy
    })),
    abilities: unit.abilityIds.flatMap((abilityId) => {
      const ability = (project.abilities ?? []).find(
        (candidate) => candidate.id === abilityId
      );
      // An ability the project no longer defines is dropped for the same
      // reason the menu drops it: it is not something this character can be
      // told to do, so it is not something the sheet should claim it knows.
      if (!ability) return [];
      // Both ends are the ability's own, and the character's reach bonus is
      // deliberately not on either: an ability's power and shape come from the
      // ability rather than from the caster, and the bonus is about the arm
      // that swings a weapon. The engine leaves ability reach alone at every
      // site that resolves one, so a row widened here would offer a cast the
      // engine would refuse.
      return [{
        name: ability.name,
        minimumReach: ability.minimumRange,
        maximumReach: ability.maximumRange
      }];
    }),
    items: unit.items.map((item) => ({
      name: item.name,
      count: item.count,
      restores: item.kind === "restore" ? item.power : 0
    }))
  };
}

/**
 * The opponents a strike could land on. `weaponId` names which carried weapon
 * to measure the band from; omitting it measures the weapon in hand, which is
 * what a character carrying one weapon always uses. A weapon the character
 * does not carry offers nobody rather than falling back to another band.
 *
 * The engine's own answer, `aimable_tiles` for a strike, read back off the
 * board: the engine lights the tile of every opponent the strike could be
 * committed at, and whoever stands on a lit tile is the character a confirm
 * there would name. The widening is the engine's rather than a band re-derived
 * here with the character's reach bonus re-added to it, which would be a copy
 * of `resolve_strike`'s arithmetic kept in step by hand; so a strike the
 * player is never shown is a strike the engine would also refuse.
 *
 * Row-major, because the tiles are, rather than in the order the board lists
 * its characters. Nothing reads it as an order: one caller takes the first
 * name to price a swing, the rest ask whether a name is in it.
 */
export function legalTargets(
  state: PlaytestState,
  unitId: string,
  weaponId?: string
): string[] {
  const engineId = state.unitIds.get(unitId);
  if (engineId === undefined) return [];
  const aimed = state.encounter.aimableTiles(engineId, {
    kind: "strike",
    // Absence is the weapon in hand, exactly as an attack command says it.
    ...(weaponId === undefined ? {} : { weaponId: stableContentId(weaponId) })
  });
  return aimed.flatMap((tile) => {
    const target = state.units.find(
      (candidate) =>
        candidate.onBoard && candidate.x === tile.x && candidate.y === tile.y
    );
    // A lit tile always has somebody on it, since that is what makes it lit, so
    // this only guards the board's own copy of where everybody stands falling
    // a synchronisation behind the engine's.
    return target ? [target.id] : [];
  });
}

/**
 * Reports a completed battle to the campaign cursor, once. The branch taken is
 * the engine's decision over the encounter's own objective results: conditions,
 * priorities, the unconditional fallback.
 */
function advanceCampaign(
  project: SourceProject,
  state: PlaytestState
) {
  void project;
  const run = state.campaign;
  const outcome = state.outcome;
  if (outcome === "ongoing" || !run || run.advanced) return;
  run.advanced = true;
  const objectives = state.encounter.snapshot().objectives;
  const error = run.handle.advanceAfter(outcome, objectives);
  if (error !== "none") {
    run.advanceError = `The campaign could not continue: ${error}.`;
    state.events.unshift(run.advanceError);
    return;
  }
  const next = run.handle.state();
  if (next.kind === "terminal") {
    const target = run.nodesByStableId.get(next.nodeId);
    state.terminalNodeName = target?.name ?? "The End";
    state.events.unshift(
      `Campaign advanced to terminal: ${state.terminalNodeName}.`
    );
  }
}

export function moveUnit(
  project: SourceProject,
  state: PlaytestState,
  unitId: string,
  x: number,
  y: number
): boolean {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit || !legalMoves(state, unitId).some(([mx, my]) => mx === x && my === y)) {
    return false;
  }
  const result = state.encounter.apply({
    type: "move",
    unitId: state.unitIds.get(unitId)!,
    destination: { x, y }
  });
  if (result.error !== "none") return false;
  synchronize(state);
  narrate(state, result.events);
  advanceCampaign(project, state);
  return true;
}

/**
 * How often the next strike would land, as a whole percentage, or null when
 * there is no strike to price or it is certain.
 *
 * The number is the engine's own: it comes off `forecast_attack`, which is the
 * same chance `apply` rolls against, so a screen showing this cannot disagree
 * with what the battle does. Nothing here computes it, and nothing here rounds
 * it. Certainty answers null so that a project whose weapons all land shows
 * exactly what it always showed.
 */
export function strikeChance(
  state: PlaytestState,
  unitId: string,
  weaponId?: string
): number | null {
  const targets = legalTargets(state, unitId, weaponId);
  const first = targets[0];
  if (first === undefined) return null;
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit) return null;
  const named =
    weaponId !== undefined && weaponId !== unit.weapons[0]?.id
      ? stableContentId(weaponId)
      : 0n;
  const forecast = state.encounter.forecastAttack(
    state.unitIds.get(unitId)!,
    state.unitIds.get(first)!,
    named
  );
  if (forecast.error !== "none" || forecast.hitChance >= 100) return null;
  return forecast.hitChance;
}

/**
 * Which way the weapon matchup leans on the next strike, or "none" when the
 * table does not name this pairing and when there is no strike to price.
 *
 * The same forecast `strikeChance` reads, and for the same reason: the triangle
 * moves that chance and the damage behind it, silently. An author playtesting a
 * game they wrote the table for should see the rule fire, and until this they
 * saw a percentage change with nothing saying why -- which is the gap the
 * consoles' own bars had before they grew a mark for it.
 *
 * Asked of the engine rather than worked out from the two weapons here. Which
 * kind beats which is a rule, and a second place it lived would be a second
 * rule.
 */
export function strikeLean(
  state: PlaytestState,
  unitId: string,
  weaponId?: string
): WeaponLean {
  const targets = legalTargets(state, unitId, weaponId);
  const first = targets[0];
  if (first === undefined) return "none";
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit) return "none";
  const named =
    weaponId !== undefined && weaponId !== unit.weapons[0]?.id
      ? stableContentId(weaponId)
      : 0n;
  const forecast = state.encounter.forecastAttack(
    state.unitIds.get(unitId)!,
    state.unitIds.get(first)!,
    named
  );
  if (forecast.error !== "none") return "none";
  return forecast.lean;
}

export function attackUnit(
  project: SourceProject,
  state: PlaytestState,
  unitId: string,
  targetId: string,
  weaponId?: string
): boolean {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  const target = state.units.find((candidate) => candidate.id === targetId);
  if (
    !unit ||
    !target ||
    !legalTargets(state, unitId, weaponId).includes(targetId)
  ) {
    return false;
  }
  // The weapon in hand is named by its absence, so a character carrying one
  // weapon sends the identical command it always sent.
  const named =
    weaponId !== undefined && weaponId !== unit.weapons[0]?.id
      ? { weaponId: stableContentId(weaponId) }
      : {};
  const result = state.encounter.apply({
    type: "attack",
    unitId: state.unitIds.get(unitId)!,
    targetId: state.unitIds.get(targetId)!,
    ...named
  });
  if (result.error !== "none") return false;
  synchronize(state);
  narrate(state, result.events);
  advanceCampaign(project, state);
  return true;
}

/**
 * What spending one carried item would give back, or null when there is
 * nothing to show: the item does nothing, has run out, or the engine would
 * refuse the use. The number is the engine's own, off `forecast_item`, and it
 * is exact rather than expected: nothing about using an item rolls.
 */
export function itemRestore(
  state: PlaytestState,
  unitId: string,
  itemId: string
): number | null {
  const engineUnit = state.unitIds.get(unitId);
  if (engineUnit === undefined) return null;
  const forecast = state.encounter.forecastItem(
    engineUnit,
    stableContentId(itemId)
  );
  if (forecast.error !== "none" || forecast.kind !== "restore") return null;
  return forecast.restored;
}

/**
 * Spends one of the character's carried items on itself. The identity handed
 * to the engine is the same stable content identity the compiler assigns, so
 * the browser drinks exactly what the console drinks.
 */
export function useItem(
  project: SourceProject,
  state: PlaytestState,
  unitId: string,
  itemId: string
): boolean {
  const engineUnit = state.unitIds.get(unitId);
  if (engineUnit === undefined) return false;
  const result = state.encounter.apply({
    type: "use_item",
    unitId: engineUnit,
    itemId: stableContentId(itemId)
  });
  if (result.error !== "none") return false;
  synchronize(state);
  narrate(state, result.events);
  advanceCampaign(project, state);
  return true;
}

/**
 * Casts one of the character's abilities at a tile. The identity handed to the
 * engine is the same stable content identity the compiler assigns, so the
 * browser aims at exactly what the console aims at.
 */
export function castAbility(
  project: SourceProject,
  state: PlaytestState,
  unitId: string,
  abilityId: string,
  x: number,
  y: number
): boolean {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  const aimable = legalCastTiles(project, state, unitId, abilityId)
    .some(([tx, ty]) => tx === x && ty === y);
  if (!unit || !aimable) return false;
  const result = state.encounter.apply({
    type: "ability",
    unitId: state.unitIds.get(unitId)!,
    abilityId: stableContentId(abilityId),
    destination: { x, y }
  });
  if (result.error !== "none") return false;
  synchronize(state);
  narrate(state, result.events);
  advanceCampaign(project, state);
  return true;
}

export function waitUnit(
  project: SourceProject,
  state: PlaytestState,
  unitId: string
): boolean {
  const unit = state.units.find((candidate) => candidate.id === unitId);
  if (!unit || !canAct(state, unitId)) return false;
  const result = state.encounter.apply({
    type: "wait",
    unitId: state.unitIds.get(unitId)!
  });
  if (result.error !== "none") return false;
  synchronize(state);
  narrate(state, result.events);
  advanceCampaign(project, state);
  return true;
}

// --- Campaign play session --------------------------------------------------
//
// Play mode runs the whole campaign, not one skirmish: scenes before and
// between battles, the battle the cursor names, the branch the engine picks
// from the outcome, and the authored ending. The session is a small phase
// machine over the engine-side cursor; it never inspects a transition itself.

export interface CampaignSceneDialogue {
  name: string;
  /**
   * `castEntry` is which of `cast` speaks this line, plus one; zero is a line
   * the scene named nobody for.
   */
  lines: readonly { speaker: string; text: string; castEntry: number }[];
  /**
   * The unit type identity of each speaker this scene cast, in authored order,
   * as the engine handed it back. A client that draws a face resolves it
   * through the same records the board does, so the picture a player sees
   * comes from the compiled record rather than from the project beside it.
   */
  cast: readonly bigint[];
  /**
   * What the scene is drawn against, as the engine handed it back: the art
   * library's backdrop menu index plus one, or 0 for a scene that names none.
   * Resolved to bands by `backdropByIndex`, so the picture a player sees comes
   * from the compiled record rather than from the project beside it.
   */
  backdrop: number;
}

export interface CampaignScene {
  nodeName: string;
  dialogues: readonly CampaignSceneDialogue[];
  index: number;
}

export type CampaignPhase = "scene" | "battle" | "ended" | "stalled";

export interface CampaignPlaytest {
  campaignSourceId: string;
  handle: Campaign;
  nodesByStableId: Map<bigint, CampaignNode>;
  phase: CampaignPhase;
  scene: CampaignScene | undefined;
  battle: PlaytestState | undefined;
  endingName: string | undefined;
  stallReason: string | undefined;
  /** Nodes whose dialogues have already been shown, by stable identity. */
  presented: Set<bigint>;
}

export interface CampaignPlaytestStart {
  session?: CampaignPlaytest;
  error?: string;
}

function stallCampaign(session: CampaignPlaytest, reason: string): void {
  session.phase = "stalled";
  session.stallReason = reason;
  session.scene = undefined;
}

/**
 * Walks the cursor until something needs the player: a scene to read, a
 * battle to fight, or the ending. Bounded so an authored cycle of empty story
 * nodes surfaces as a stalled campaign rather than a frozen tab.
 */
function settleCampaign(project: SourceProject, session: CampaignPlaytest): void {
  for (let guard = 0; guard < 256; guard += 1) {
    const current = session.handle.state();
    const node = session.nodesByStableId.get(current.nodeId);
    if (!node) {
      stallCampaign(
        session,
        "The campaign reached a part of the story this game no longer contains."
      );
      return;
    }
    // Every kind of node may carry dialogue; present it once, in authored
    // order, decoded by the engine. An unloadable dialogue is skipped, which
    // is what the native client does.
    if (current.dialogueIds.length > 0 && !session.presented.has(current.nodeId)) {
      session.presented.add(current.nodeId);
      const dialogues: CampaignSceneDialogue[] = [];
      for (const dialogueId of current.dialogueIds) {
        const loaded = session.handle.dialogue(dialogueId);
        if (loaded.error !== "none") continue;
        dialogues.push({
          name: loaded.dialogue.name,
          lines: loaded.dialogue.lines,
          cast: loaded.dialogue.cast,
          backdrop: loaded.dialogue.backdrop
        });
      }
      if (dialogues.length > 0) {
        session.scene = { nodeName: node.name, dialogues, index: 0 };
        session.phase = "scene";
        return;
      }
    }
    if (current.kind === "story") {
      const error = session.handle.advanceStory();
      if (error !== "none") {
        stallCampaign(session, `The campaign could not continue: ${error}.`);
        return;
      }
      continue;
    }
    if (current.kind === "encounter") {
      const started = startEncounterNode(project, session.campaignSourceId, node, {
        handle: session.handle,
        campaignSourceId: session.campaignSourceId,
        nodesByStableId: session.nodesByStableId,
        advanced: false,
        advanceError: undefined,
        owned: false
      });
      if (started.error !== undefined || !started.state) {
        stallCampaign(session, started.error ?? "The Stage could not start.");
        return;
      }
      session.scene = undefined;
      session.battle = started.state;
      session.phase = "battle";
      return;
    }
    session.scene = undefined;
    session.endingName = node.name;
    session.phase = "ended";
    return;
  }
  stallCampaign(session, "The campaign kept moving without reaching a Stage or an ending.");
}

/** Starts the campaign Play mode runs: entry node first, exactly like the console. */
export function startCampaignPlaytest(
  project: SourceProject
): CampaignPlaytestStart {
  if (!isEncounterEngineReady()) {
    return { error: "The game engine is still loading. Try again in a moment." };
  }
  const campaign = (project.campaigns ?? []).find((candidate) => candidate.flow);
  if (!campaign) {
    return { error: "No campaign Stage is available to play." };
  }
  const built = buildCampaignFlow(project, campaign);
  if (!built.definition || !built.nodesByStableId) {
    return { error: built.error ?? "No campaign Stage is available to play." };
  }
  const created = createCampaign(built.definition);
  if (created.error !== "none") {
    return { error: `The campaign flow could not be loaded: ${created.error}.` };
  }
  const session: CampaignPlaytest = {
    campaignSourceId: campaign.id,
    handle: created.campaign,
    nodesByStableId: built.nodesByStableId,
    phase: "scene",
    scene: undefined,
    battle: undefined,
    endingName: undefined,
    stallReason: undefined,
    presented: new Set()
  };
  settleCampaign(project, session);
  return { session };
}

/**
 * The player's single "keep going" verb: the next dialogue of a scene, then
 * whatever the cursor says follows: another scene, the next battle, or the
 * ending. After a finished battle it also releases that battle's encounter.
 */
export function continueCampaignPlaytest(
  project: SourceProject,
  session: CampaignPlaytest
): void {
  if (session.phase === "scene" && session.scene) {
    if (session.scene.index + 1 < session.scene.dialogues.length) {
      session.scene = { ...session.scene, index: session.scene.index + 1 };
      return;
    }
    session.scene = undefined;
    settleCampaign(project, session);
    return;
  }
  if (session.phase === "battle" && session.battle) {
    const battle = session.battle;
    if (battle.outcome === "ongoing") return;
    // The cursor already advanced when the battle completed; this hands the
    // board back before walking to what follows.
    const stalled = battle.campaign?.advanceError;
    endPlaytest(battle);
    session.battle = undefined;
    if (stalled !== undefined) {
      stallCampaign(session, stalled);
      return;
    }
    settleCampaign(project, session);
  }
}

/** Releases the battle and the cursor behind a campaign session. */
export function endCampaignPlaytest(session: CampaignPlaytest | undefined): void {
  if (!session) return;
  endPlaytest(session.battle);
  session.battle = undefined;
  session.handle.dispose();
}
