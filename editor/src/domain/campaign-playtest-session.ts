// SPDX-License-Identifier: MIT
import type { CampaignNode, SourceProject } from "../generated/source-v1";
import {
  createCampaign,
  createCampaignSession,
  eraseEngineSlot,
  growableStats,
  isEncounterEngineReady,
  readEngineSlot,
  rosterErrorName,
  stableContentId,
  writeEngineSlot,
  type Campaign,
  type CampaignAftermath,
  type CampaignBoardDefinition,
  type MemberSpecificity,
  type CampaignManagementVerb,
  type CampaignMember,
  type CampaignSession,
  type CampaignSlotFailure,
  type CampaignStack,
  type CharacterLoss,
  type GrowableStat,
  type UnitTypeProgression
} from "./encounter-simulation";
export type { CampaignManagementVerb } from "./encounter-simulation";
import {
  campaignSlotName,
  packageIdentityHex,
  type CampaignSlotStore
} from "./campaign-slot-store";
import {
  adoptEncounterNode,
  buildCampaignFlow,
  endPlaytest,
  planEncounterNode,
  type CampaignSceneDialogue,
  type EncounterNodePlan,
  type PlaytestState
} from "./playtest-session";

// Play mode, running the campaign the author wrote rather than a walk through
// its encounters.
//
// The difference is everything a campaign is: a roster that persists between
// battles, a character who is permanently lost, experience earned, levels
// reached, points a growth roll granted, what fell into the army's store and
// what was drunk out of it, and where the graph went afterwards. All of it is
// `client::CampaignSession`, compiled into the same WebAssembly module the
// battle runs in, and reached through the campaign session entry points in
// `platform/web/src/simulation_abi.cpp`.
//
// **Nothing in this file derives a campaign fact.** Every number a surface
// shows is read out of an answer the engine gave: the exclusions come from the
// board the session prepared, the level-ups and their per-stat points come from
// `derive_battle_progression`, the experience and the store movements are that
// same function's operations, and where the campaign went is
// `campaign::complete_node`, and who a member is, their name included, is
// what the author wrote into the campaign's roster. What this file does is
// drive the phases and turn engine answers into sentences.
//
// It is a sibling of `playtest-session.ts` rather than a replacement. The
// rosterless walk is still what the Browser playtest panel runs and still what
// Play runs for a package with no campaign, and it is unchanged.

/** How the session labels one roster member for a person reading the screen. */
export interface CampaignRosterMember {
  /** The persistent identity the engine knows them by. */
  id: bigint;
  /** The authored member identity, the same on every board that fields them. */
  memberKeyId: bigint;
  /** What the author called them. */
  name: string;
  availability: CampaignMember["availability"];
  level: number;
  experience: number;
  gained: Readonly<Record<GrowableStat, number>>;
  /**
   * What the campaign holds for them, by the author's own name for each item.
   * This is the satchel they take onto the next board, so it shrinks when they
   * spend something and never grows back on its own.
   */
  carrying: readonly CampaignHeldItem[];
}

/** One stack, named for a reader and identified for a verb. */
export interface CampaignHeldItem {
  /** The identity a management gesture names. Never shown; always sent. */
  itemId: bigint;
  itemName: string;
  quantity: number;
}

/** One character the roster kept off a board, and why a reader should care. */
export interface CampaignExclusion {
  id: bigint;
  name: string;
  availability: CampaignMember["availability"];
}

/** One level a battle granted, with what each level actually gave. */
export interface CampaignLevelReport {
  name: string;
  fromLevel: number;
  toLevel: number;
  /** Only the stats that actually gained, in the engine's roll order. */
  points: readonly { stat: GrowableStat; points: number }[];
}

/** One thing a battle did to what the campaign owns or knows. */
export interface CampaignStoreReport {
  /** "add_item" or "consume_item", named by the campaign itself. */
  kind: string;
  itemName: string;
  amount: number;
  /**
   * Whose it was. A drop belongs to the company and carries no name; a draught
   * a character drank comes out of that character's own kit and carries theirs.
   * The two owners are the campaign's, and this reports them rather than
   * flattening both into "supplies".
   */
  ownerName: string | undefined;
}

/** What a finished battle did, as a screen between battles reads it. */
export interface CampaignAftermathReport {
  nodeName: string;
  outcome: PlaytestState["outcome"];
  canonicalHash: bigint;
  /**
   * Members this battle put at zero health, by the author's own name for them.
   * Who *fell*, never who was buried: what became of them is `characterLoss`.
   */
  fallen: readonly string[];
  /**
   * What this campaign does with a character who falls, read off the engine's
   * answer rather than off the project, so that the screen and the rule the
   * battle was actually committed under cannot come apart.
   */
  characterLoss: CharacterLoss;
  /** Experience earned, per member, exactly as the engine granted it. */
  experience: readonly { name: string; amount: number }[];
  levelUps: readonly CampaignLevelReport[];
  /** What moved, and out of whose hands. */
  store: readonly CampaignStoreReport[];
  /** What the company owns now, beyond what its members are carrying. */
  supplies: readonly { itemName: string; quantity: number }[];
  /**
   * Members an authored recruitment brought into the company as this battle
   * committed, by the name the author gave them. Read off the committed batch,
   * exactly as the level-ups are.
   */
  joined: readonly string[];
  /** Where the campaign went, or undefined when it did not move. */
  nextNodeName: string | undefined;
  /** Why it did not move, when it did not. */
  blockedReason: string | undefined;
  /** Whether the campaign reached its storage slot, and what refused it. */
  saved: boolean;
  saveError: string | undefined;
}

/**
 * One member of the company, as the between-battle screen offers them.
 *
 * `fielded` and `placeable` are two different questions and the screen asks
 * both: whether the player is sending them, and whether the next board has
 * anywhere to put them. A member the board does not place is shown and not
 * offered, because fielding them would be a gesture that succeeds and changes
 * nothing.
 */
export interface CampaignCompanyMember {
  id: bigint;
  name: string;
  availability: CampaignMember["availability"];
  fielded: boolean;
  placeable: boolean;
  /** Alive enough to be handed something. The campaign refuses the rest by name. */
  present: boolean;
  carrying: readonly CampaignHeldItem[];
}

/**
 * The company between battles: what it owns, who is in it, and who is going.
 *
 * Read out of the engine's own company view after every gesture. There is no
 * pending arrangement here and none in the engine either, each gesture being
 * committed and written to the slot as it is made, so this is always what the
 * campaign holds and never what the player has in mind.
 */
export interface CampaignCompanyReport {
  nodeName: string;
  /** What the company owns beyond what its members carry. */
  store: readonly CampaignHeldItem[];
  members: readonly CampaignCompanyMember[];
  /**
   * How many of the company would actually take the next board's field as it
   * stands, and how many its author allows out. A capacity of zero is a board
   * that caps nothing, which is every board by default, and a screen says
   * nothing about a count nobody is counting.
   *
   * Both numbers are read off the engine's own company view rather than summed
   * here: they are what a `field` is refused against, and a screen that counted
   * for itself would be a second implementation of the roster's rule.
   */
  fielded: number;
  capacity: number;
  /**
   * Why the last thing the player asked for did not happen, in the engine's own
   * word for it: the campaign's when a gesture was refused, the roster's when
   * a board was. Undefined when nothing was refused.
   */
  refusal: string | undefined;
  /** Set when a gesture committed and the slot would not take it. */
  saveError: string | undefined;
}

export type CampaignPlayPhase =
  | "scene"
  | "battle"
  | "aftermath"
  | "managing"
  | "ended"
  | "stalled";

export interface CampaignPlayScene {
  nodeName: string;
  dialogues: readonly CampaignSceneDialogue[];
  index: number;
}

export interface CampaignPlaySession {
  project: SourceProject;
  campaignSourceId: string;
  campaignName: string;
  /** The engine-side campaign session. Every campaign fact comes from it. */
  session: CampaignSession;
  /** The cursor, held only to decode the dialogue records a node names. */
  dialogues: Campaign;
  nodesByStableId: Map<bigint, CampaignNode>;
  plans: Map<string, EncounterNodePlan>;
  namesById: Map<bigint, string>;
  itemNames: Map<bigint, string>;
  phase: CampaignPlayPhase;
  scene: CampaignPlayScene | undefined;
  battle: PlaytestState | undefined;
  /** Who the roster kept off the board being fought, and why. */
  excluded: readonly CampaignExclusion[];
  /**
   * Which campaign member each character on the board being fought is. The
   * engine's own join, not a guess: a board identity is encounter-local and a
   * member is not, so a sheet that wants to state a level needs this table.
   * Empty for a character no campaign member is standing in.
   */
  membersByPlacement: Map<string, bigint>;
  aftermath: CampaignAftermathReport | undefined;
  /** The company, while the player is arranging it. */
  company: CampaignCompanyReport | undefined;
  /**
   * Whether the player has taken the board at the node the campaign stands on.
   * Cleared the moment the campaign moves, which is what puts the management
   * screen before every board without a rule per way of reaching one.
   */
  proceeding: boolean;
  roster: readonly CampaignRosterMember[];
  /**
   * Who joined the company as the last story node completed, by the name the
   * author gave them. A battle's recruits are in its aftermath instead, where
   * everything else that battle did is.
   */
  joined: readonly string[];
  endingName: string | undefined;
  stallReason: string | undefined;
  /** The slot the campaign is written to after every battle. */
  slot: string;
  /** Whether this session picked a campaign up rather than founding one. */
  resumed: boolean;
  presented: Set<bigint>;
  /** Set once the battle in progress has been committed. */
  committed: boolean;
}

export interface CampaignPlayStart {
  session?: CampaignPlaySession;
  error?: string;
  /**
   * Set when a resume was asked for, a save was there, and it was refused.
   *
   * The session that comes back is a freshly founded campaign. The engine
   * founds before it loads, precisely so a refused save leaves something
   * standing, so this is the sentence that keeps it from happening silently.
   */
  refusal?: string;
}

export interface CampaignPlayOptions {
  /** Which authored campaign to play. The first with a flow when omitted. */
  campaignId?: string;
  /**
   * Where to keep it. Derived from the package and campaign identities when
   * omitted, which is what a browser wants; named explicitly by tests that
   * want two campaigns of one game kept apart on purpose.
   */
  slot?: string;
  /** Pick up what is in the slot rather than founding a new campaign. */
  resume?: boolean;
}

/**
 * The sixteen bytes that name this project's content, exactly as the content
 * compiler derives them: the `packageId` UUID, byte for byte.
 *
 * This is not bookkeeping. A growth roll is seeded from the completed battle,
 * and `campaign::derive_growth_seed` hashes the encounter's whole reference,
 * package identity included. Deriving the identity any other way here would
 * give an author a level-up in the editor that the compiled game will never
 * roll, and the editor and the cartridge have to agree.
 *
 * A project whose packageId is not a UUID identifies as sixteen zero bytes.
 * That is what the compiler's own refusal leaves behind, and it is consistent
 * with itself, so such a project still plays and still saves.
 */
function packageIdentity(project: SourceProject): Uint8Array {
  const bytes = new Uint8Array(16);
  const text = project.packageId ?? "";
  if (text.length !== 36) return bytes;
  const digits = text.replace(/-/g, "");
  if (!/^[0-9a-fA-F]{32}$/.test(digits)) return bytes;
  for (let index = 0; index < 16; index += 1) {
    bytes[index] = Number.parseInt(digits.slice(index * 2, index * 2 + 2), 16);
  }
  return bytes;
}

/**
 * The revision as the compiler packs it: three ten-bit fields out of a
 * `major.minor.patch` string. A save records it and a load compares it, so it
 * has to be the same number the native pipeline would have written.
 */
function contentRevision(project: SourceProject): number {
  const parts = (project.contentRevision ?? "").split(".");
  if (parts.length !== 3) return 0;
  const packed = parts.map((part) => Number.parseInt(part, 10));
  if (packed.some((value) => !Number.isInteger(value) || value < 0 || value > 1023)) {
    return 0;
  }
  return (packed[0]! << 20) | (packed[1]! << 10) | packed[2]!;
}

/**
 * The unit type records the campaign reads: the growth block a level-up rolls
 * against, the drop a defeat may leave, and the starting kit a member joins
 * with.
 */
function unitTypeProgressions(project: SourceProject): UnitTypeProgression[] {
  return (project.unitTypes ?? []).map((unitType) => {
    const growth: Partial<Record<GrowableStat, number>> = {};
    for (const stat of growableStats) {
      const chance = unitType.growthRates?.[stat];
      if (chance !== undefined) growth[stat] = chance;
    }
    const dropItemId = unitType.dropItemId;
    return {
      id: stableContentId(unitType.id),
      experienceAward: unitType.experienceAward ?? 0,
      // Absent means a hundred, which is what the compiler writes and what
      // `default_experience_per_level` means. Zero is not a threshold.
      experiencePerLevel: unitType.experiencePerLevel ?? 100,
      growth,
      // What a member of this type joins the company carrying. Read once, when
      // they join; from then on the campaign holds their satchel and this list
      // is never consulted for them again.
      startingItemIds: (unitType.startingItemIds ?? []).map((item) =>
        stableContentId(item)
      ),
      ...(dropItemId !== undefined
        ? {
            dropItemId: stableContentId(dropItemId),
            dropChance: unitType.dropChance ?? 0
          }
        : {})
    };
  });
}

// What to call a member the engine named by identity alone. The names are the
// author's, learned from the roster the session reports and remembered so that
// a member who is no longer on a board can still be spoken about.
function memberName(session: CampaignPlaySession, id: bigint): string {
  return session.namesById.get(id) ?? `Character ${id}`;
}

function rememberNames(
  session: CampaignPlaySession,
  members: readonly CampaignMember[]
): void {
  for (const member of members) session.namesById.set(member.id, member.name);
}

function stall(session: CampaignPlaySession, reason: string): void {
  session.phase = "stalled";
  session.stallReason = reason;
  session.scene = undefined;
}

/** One stack, named for the screen and identified for the verb that moves it. */
function held(
  session: CampaignPlaySession,
  stack: CampaignStack
): CampaignHeldItem {
  return {
    itemId: stack.itemId,
    itemName: session.itemNames.get(stack.itemId) ?? "something",
    quantity: stack.quantity
  };
}

/**
 * The company, as the between-battle screen reads it.
 *
 * Everything comes from the engine's own company view, including which members
 * the next board has a place for. `refusal` is carried in rather than derived,
 * because it is the reason the player is still looking at this screen and the
 * screen has no way to know it on its own.
 */
function reportCompany(
  session: CampaignPlaySession,
  nodeName: string,
  refusal?: string,
  saveError?: string
): CampaignCompanyReport {
  const company = session.session.company();
  const placeable = new Set(company.placeable);
  return {
    nodeName,
    store: company.store.map((stack) => held(session, stack)),
    members: company.roster.map((member) => ({
      id: member.id,
      name: member.name,
      availability: member.availability,
      fielded: member.availability === "available",
      placeable: placeable.has(member.id),
      present: member.availability !== "dead",
      carrying: member.carried.map((stack) => held(session, stack))
    })),
    fielded: company.fielded.length,
    capacity: company.capacity,
    refusal,
    saveError
  };
}

function refreshRoster(session: CampaignPlaySession, roster: readonly CampaignMember[]): void {
  rememberNames(session, roster);
  session.roster = roster.map((member) => ({
    id: member.id,
    memberKeyId: member.sourceKeyId,
    name: member.name,
    availability: member.availability,
    level: member.level,
    experience: member.experience,
    gained: member.gained,
    carrying: member.carried.map((stack) => held(session, stack))
  }));
}

/**
 * Walks the campaign until something needs the player: a scene to read, a board
 * to fight, or the ending. Bounded, so an authored cycle of empty story nodes
 * surfaces as a stalled campaign rather than a frozen tab.
 */
function settle(session: CampaignPlaySession): void {
  for (let guard = 0; guard < 256; guard += 1) {
    const standing = session.session.standing();
    refreshRoster(session, standing.roster);
    if (standing.error !== "none") {
      stall(
        session,
        `The campaign could not continue: ${standing.error}.`
      );
      return;
    }
    const node = session.nodesByStableId.get(standing.nodeId);
    if (!node) {
      stall(
        session,
        "The campaign reached a part of the story this game no longer contains."
      );
      return;
    }
    // Every kind of node may carry dialogue; present it once, in authored
    // order, decoded by the engine.
    if (
      standing.dialogueIds.length > 0 &&
      !session.presented.has(standing.nodeId)
    ) {
      session.presented.add(standing.nodeId);
      const dialogues: CampaignSceneDialogue[] = [];
      for (const dialogueId of standing.dialogueIds) {
        const loaded = session.dialogues.dialogue(dialogueId);
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
    if (standing.kind === "story") {
      const moved = session.session.advanceStory();
      if (moved.error !== "none") {
        stall(session, `The campaign could not continue: ${moved.error}.`);
        return;
      }
      // A story node the author wrote a recruitment onto brings people in as
      // it completes. Said here, where a reader is between scenes, because
      // there is no aftermath screen for a node that fights nothing.
      rememberNames(session, moved.joined);
      session.joined = moved.joined.map((member) => member.name);
      continue;
    }
    if (standing.kind === "terminal") {
      session.scene = undefined;
      session.endingName = node.name;
      session.phase = "ended";
      return;
    }

    const plan = session.plans.get(node.id);
    if (!plan?.definition) {
      stall(session, plan?.error ?? "The Stage could not be prepared.");
      return;
    }

    // The company, before the board. The stage stands here and nowhere else,
    // which is what puts it after a battle, after a story node, on a resume and
    // before the very first board without a rule for each. `proceeding` is the
    // player having taken it, and it is cleared the moment the campaign moves.
    if (!session.proceeding) {
      session.scene = undefined;
      session.company = reportCompany(session, node.name);
      session.phase = "managing";
      return;
    }

    const board = session.session.board();
    if (board.error !== "none" || !board.encounter) {
      // A board the roster refuses is a company the player can change, so it
      // sends them back to the company rather than out of the campaign.
      // Nothing was committed: `prepare_board` publishes nothing when it
      // refuses.
      if (board.rosterError !== "none") {
        session.proceeding = false;
        session.company = reportCompany(session, node.name, board.rosterError);
        session.phase = "managing";
        return;
      }
      stall(session, `The Stage could not start: ${board.createError}.`);
      return;
    }
    session.proceeding = false;
    session.company = undefined;
    const keep = new Set(board.placements.map((placement) => placement.unitId));
    session.excluded = board.excluded.map((id) => ({
      id,
      name: memberName(session, id),
      availability:
        standing.roster.find((member) => member.id === id)?.availability ??
        "unrecruited"
    }));
    session.membersByPlacement = new Map();
    for (const [placementId, boardId] of plan.unitIds ?? []) {
      const member = board.binding.get(boardId);
      if (member !== undefined) session.membersByPlacement.set(placementId, member);
    }
    session.scene = undefined;
    session.committed = false;
    session.battle = adoptEncounterNode(
      session.project,
      session.campaignSourceId,
      node,
      plan,
      board.encounter,
      keep,
      undefined
    );
    // What the author called each character standing on this board, handed down
    // so that every sentence the battle produces says who it is about. Without
    // it a placement is called by its unit type, and a company of four Dawn
    // Guards narrates four indistinguishable battles. The table is the engine's
    // own join and the roster's own names, the same two things the aftermath's
    // list of the dead is built from, so the log and the screen after it
    // cannot come to call one character two things.
    session.battle.characterNames = new Map(
      [...session.membersByPlacement].flatMap(([placement, member]) => {
        const name = session.namesById.get(member);
        return name === undefined ? [] : [[placement, name] as [string, string]];
      })
    );
    // And onto the characters themselves, so that every surface agrees with the
    // log rather than only the log knowing. A member's own name outranks the
    // one derived from their character type, on every client, and this is where
    // the editor applies the same order: a deployment prompt that asked where
    // `Dawn Guard 1` should stand while the log called her `Vanguard Rilla`
    // would be one battle with two casts.
    for (const unit of session.battle.units) {
      const named = session.battle.characterNames.get(unit.id);
      if (named !== undefined) unit.name = named;
    }
    // And what this project says a fall costs, handed down for the same reason
    // the names are: the log has to say the right word about the event while
    // the battle is still on, and "died" and "fell" are two different claims.
    session.battle.characterLoss = session.project.characterLoss ?? "permanent";
    if (session.excluded.length > 0) {
      session.battle.events.unshift(
        `${session.excluded
          .map((member) => member.name)
          .join(", ")} cannot take the field.`
      );
    }
    session.phase = "battle";
    return;
  }
  stall(
    session,
    "The campaign kept moving without reaching a Stage or an ending."
  );
}

/** Starts the campaign Play mode runs, founding or resuming its roster. */

// The order eleven stat deltas are indexed in, which is the order the engine
// and the compiled package both use: the ten a level-up may grow, at the same
// indices a growth roll uses them at, and then speed.
//
// Speed is last because it is the one stat the two lists differ by. Growth
// refuses it, because a roll that reshuffled turn order would change a battle
// the player is already standing in. An authored delta is fixed before the
// campaign is founded and reshuffles nothing anybody was standing in.
const specificStatOrder = [
  "health",
  "strength",
  "defense",
  "resistance",
  "movement",
  "actionPoints",
  "skill",
  "luck",
  "evasion",
  "magic",
  "speed"
] as const;

/**
 * What the campaign says about its members beyond their unit types.
 *
 * The founding company and every node's recruits are one walk, because they are
 * one table: a recruit is a member of the company from the moment they join,
 * and the two share one authored shape precisely so that this needs no second
 * case. A member who is exactly their unit type contributes nothing, so a
 * campaign in which nobody is written to be more produces an empty table and a
 * board that is the board it always was.
 */
function authoredSpecificities(
  campaign: NonNullable<SourceProject["campaigns"]>[number]
): MemberSpecificity[] {
  const members = [
    ...(campaign.roster ?? []),
    ...(campaign.flow?.nodes ?? []).flatMap((node) => node.recruits ?? [])
  ];
  const table: MemberSpecificity[] = [];
  for (const member of members) {
    const specificity = member.specificity;
    if (!specificity) continue;
    const statDeltas = specificStatOrder.map(
      (stat) => specificity.stats?.[stat] ?? 0
    );
    const reachBonus = specificity.rangeBonus ?? 0;
    if (reachBonus === 0 && statDeltas.every((delta) => delta === 0)) continue;
    table.push({
      memberId: stableContentId(member.id),
      statDeltas,
      reachBonus
    });
  }
  return table;
}
export function startCampaignPlaySession(
  project: SourceProject,
  options: CampaignPlayOptions = {}
): CampaignPlayStart {
  if (!isEncounterEngineReady()) {
    return { error: "The game engine is still loading. Try again in a moment." };
  }
  const campaigns = (project.campaigns ?? []).filter((candidate) => candidate.flow);
  const campaign =
    options.campaignId !== undefined
      ? campaigns.find((candidate) => candidate.id === options.campaignId)
      : campaigns[0];
  if (!campaign) {
    return { error: "No campaign Stage is available to play." };
  }
  const built = buildCampaignFlow(project, campaign);
  if (!built.definition || !built.nodesByStableId) {
    return { error: built.error ?? "No campaign Stage is available to play." };
  }

  // Every board the flow can reach, planned once. The campaign session founds
  // its roster by walking exactly these, so they have to be complete before it
  // begins rather than produced when a node is arrived at.
  const plans = new Map<string, EncounterNodePlan>();
  // What the author made of individual characters, read once out of the
  // campaign the same way a compiled package's campaign record carries it, and
  // handed to every board. Play mode has no package at all, so this is exactly
  // the case the roster join takes a board rather than a package for.
  const specificities = authoredSpecificities(campaign);
  const boards: CampaignBoardDefinition[] = [];
  for (const node of campaign.flow?.nodes ?? []) {
    if (node.kind !== "encounter") continue;
    const plan = planEncounterNode(project, campaign.id, node);
    plans.set(node.id, plan);
    if (!plan.definition || !plan.units || !plan.sourceKeyIds) continue;
    boards.push({
      encounterId: stableContentId(`${campaign.id}/${node.id}`),
      definition: plan.definition,
      sourceKeyIds: plan.units.map((unit) => plan.sourceKeyIds!.get(unit.id) ?? 0n),
      // The cap rides in on the board, which is what lets the same roster pass
      // enforce it here, with a board built from unsaved source and no package
      // at all, as it enforces it for a compiled game.
      deploymentCapacity: plan.deploymentCapacity ?? 0,
      memberSpecificities: specificities
    });
  }
  if (boards.length === 0) {
    return { error: "No campaign Stage is available to play." };
  }

  const created = createCampaignSession({
    packageId: packageIdentity(project),
    contentRevision: contentRevision(project),
    campaignId: stableContentId(campaign.id),
    flow: built.definition,
    boards,
    unitTypes: unitTypeProgressions(project)
  });
  if (created.error !== "none") {
    return { error: `The campaign could not be loaded: ${created.error}.` };
  }
  // A second handle over the same flow, used for one thing: decoding the
  // dialogue records a node names. The campaign session answers which
  // dialogues; this answers what they say.
  const cursor = createCampaign(built.definition);
  if (cursor.error !== "none") {
    created.session.dispose();
    return { error: `The campaign flow could not be loaded: ${cursor.error}.` };
  }

  const slot =
    options.slot ?? campaignSlotName(packageIdentity(project), campaign.id);
  const begun = created.session.begin({ slot, resume: options.resume ?? false });
  if (begun.error !== "none") {
    created.session.dispose();
    cursor.campaign.dispose();
    // The one refusal an author causes rather than suffers, said in words
    // rather than by the engine's own name for it: a campaign nobody can be
    // founded from is a campaign with no roster, and Play cannot invent one.
    if (begun.error === "roster_rejected") {
      return {
        error:
          `${campaign.name} has nobody to play it. Add the characters the ` +
          "campaign begins with to its roster, and give every character on " +
          "your side of a Stage one of them to stand in for."
      };
    }
    return { error: `The campaign could not begin: ${begun.error}.` };
  }

  const session: CampaignPlaySession = {
    project,
    campaignSourceId: campaign.id,
    campaignName: campaign.name,
    session: created.session,
    dialogues: cursor.campaign,
    nodesByStableId: built.nodesByStableId,
    plans,
    namesById: new Map<bigint, string>(),
    itemNames: new Map(
      (project.items ?? []).map((item) => [stableContentId(item.id), item.name])
    ),
    phase: "scene",
    scene: undefined,
    battle: undefined,
    excluded: [],
    membersByPlacement: new Map(),
    aftermath: undefined,
    company: undefined,
    proceeding: false,
    roster: [],
    joined: [],
    endingName: undefined,
    stallReason: undefined,
    slot,
    resumed: begun.resumed,
    presented: new Set(),
    committed: false
  };
  settle(session);
  // A resume that was refused founded a campaign instead of picking one up, and
  // the author is entitled to know which of those they are looking at.
  const refusal =
    (options.resume ?? false) && begun.refused
      ? describeSlotRefusal(begun.failure)
      : undefined;
  return refusal === undefined ? { session } : { session, refusal };
}

/**
 * Why the kept campaign was not picked up, in the refusing layer's own word.
 *
 * Every branch names the engine's own enumerator rather than paraphrasing it,
 * because these are the words `docs/` and the terminal client use, and an
 * author who searches for the one they were shown should find the thing that
 * refused them. The sentence around it is what a person needs to know: the
 * campaign they were playing is not the one on screen.
 *
 * `not_found` is not a refusal a reader should ever see. It is the ordinary
 * answer for a slot nobody has written, so this returns undefined for it and
 * the caller shows nothing.
 */
export function describeSlotRefusal(
  failure: CampaignSlotFailure
): string | undefined {
  if (failure.storage === "not_found") return undefined;
  const kept = "The campaign kept in this browser could not be picked up";
  if (failure.storage !== "none") {
    return `${kept}: the browser's copy would not read back (${failure.storage}).`;
  }
  if (failure.migration !== "none") {
    return (
      `${kept}: it was written against different content (` +
      `${failure.migration}). Start fresh to replace it.`
    );
  }
  if (failure.save !== "none") {
    return (
      `${kept}: this build cannot read what it says (${failure.save}). ` +
      "Start fresh to replace it."
    );
  }
  if (failure.wrongCampaign) {
    return `${kept}: it is a position in a different campaign.`;
  }
  return `${kept}, and the engine did not say why. Start fresh to replace it.`;
}

// ---------------------------------------------------------------------------
// Keeping a playtest across a page
// ---------------------------------------------------------------------------
//
// The engine saves a campaign into a slot on its own device after every battle
// and every management gesture, and that device is the WebAssembly module's
// memory: it outlives a Play session and not a reload. These three carry those
// same bytes to somewhere the browser keeps.
//
// **They add no cadence.** Nothing here decides when a campaign is written; the
// session already decided that, and every one of these is a mirror of a save
// that has already happened. **And they add no format.** What crosses is the
// `GLSV` envelope the engine produced, unread, which is why an author who
// edits the content underneath a kept campaign gets the engine's own refusal on
// the next resume rather than a guess made out here.

/** Which campaign Play would run, so a caller can ask about its slot first. */
export function playableCampaignId(
  project: SourceProject,
  campaignId?: string
): string | undefined {
  const campaigns = (project.campaigns ?? []).filter(
    (candidate) => candidate.flow
  );
  const campaign =
    campaignId !== undefined
      ? campaigns.find((candidate) => candidate.id === campaignId)
      : campaigns[0];
  return campaign?.id;
}

/** The slot this project's campaign is kept in. Both identities, bound. */
export function keptCampaignSlot(
  project: SourceProject,
  campaignId: string
): string {
  return campaignSlotName(packageIdentity(project), campaignId);
}

/**
 * Puts a kept campaign back on the engine's device, so the next begin can
 * resume it. Answers whether there was one.
 *
 * The bytes are not inspected on the way through. A save that belongs to
 * different content is still put back, so that beginning refuses it by name and
 * the author is told, which is the whole reason the slot key does not include
 * a content revision.
 */
export async function restoreKeptCampaign(
  project: SourceProject,
  campaignId: string,
  store: CampaignSlotStore
): Promise<boolean> {
  const slot = keptCampaignSlot(project, campaignId);
  const kept = await store.read(slot);
  if (!kept) {
    // Whatever this tab's module memory happens to hold for that slot is not
    // the kept campaign, and resuming it would be resuming something the
    // browser does not have. Clear it, so found-anew stays founded.
    //
    // `not_found` is the state this was reaching for rather than a failure, and
    // anything else is answered the same way: there is nothing kept, so the
    // caller founds anew, and founding writes its own save over the slot. The
    // device cannot be left holding a campaign nobody asked for.
    eraseEngineSlot(slot);
    return false;
  }
  return writeEngineSlot(slot, kept.bytes) === "none";
}

/**
 * Copies what the session last saved out to the browser's store.
 *
 * Called after each committed battle and each management gesture, the same
 * moments the session already wrote its slot. Answers whether anything was
 * carried across, which is false for a campaign that has not saved yet.
 */
export async function keepCampaign(
  session: CampaignPlaySession,
  store: CampaignSlotStore
): Promise<boolean> {
  const held = readEngineSlot(session.slot);
  if (held.error !== "none" || !held.bytes) return false;
  await store.write({
    slot: session.slot,
    packageId: packageIdentityHex(packageIdentity(session.project)),
    campaignId: session.campaignSourceId,
    bytes: held.bytes
  });
  return true;
}

/**
 * Forgets the kept campaign, on the device and in the browser both.
 *
 * This is what founding anew does to the campaign it replaces. It is deliberate
 * and it is destructive: one slot per (package, campaign) means the campaign
 * being replaced has nowhere else to be, which is why the surface says so
 * before the press rather than after it.
 *
 * Answers whether the device forgot it too. Both halves have to go: the browser
 * store is what survives the page, and the module's own device is what the next
 * begin in this tab reads. A device that kept it is a campaign that comes back
 * after being told it would not, so the answer is published rather than
 * assumed. `not_found` is success, because there was nothing left to forget.
 */
export async function forgetKeptCampaign(
  project: SourceProject,
  campaignId: string,
  store: CampaignSlotStore
): Promise<boolean> {
  const slot = keptCampaignSlot(project, campaignId);
  const forgotten = eraseEngineSlot(slot);
  await store.erase(slot);
  return forgotten === "none" || forgotten === "not_found";
}

/**
 * Who this battle has buried so far, by the name the author gave them.
 *
 * The screen after a battle lists the same people out of `aftermath.fallen`,
 * and by then the player has already been shown a board with a gap in it and
 * has had no word for what made the gap. This is the same fact said at the
 * moment it happens, which is the only moment the player is looking at the
 * battle, and it is said out of the same two things the aftermath is said
 * out of: the engine's own board-to-member join, and the roster's names.
 *
 * Only the company. A character on the board that no member stands in is
 * somebody the campaign never met: everybody on the opposing side, and any
 * hired sword an author placed without a roster member behind them. Their
 * loss is not permanent, not the player's, and already plain from the board
 * they are no longer standing on. Naming them here would bury the one line
 * that matters under a list of the ones that do not.
 */
export function campaignBattleLosses(
  session: CampaignPlaySession
): readonly string[] {
  const battle = session.battle;
  if (!battle) return [];
  const lost: string[] = [];
  for (const character of battle.defeated) {
    const member = session.membersByPlacement.get(character);
    if (member === undefined) continue;
    lost.push(memberName(session, member));
  }
  return lost;
}

/**
 * Turns the engine's aftermath into the sentences a screen between battles
 * shows. Every number is read; none is computed.
 */
function reportAftermath(
  session: CampaignPlaySession,
  nodeName: string,
  aftermath: CampaignAftermath
): CampaignAftermathReport {
  const experience: { name: string; amount: number }[] = [];
  const store: CampaignStoreReport[] = [];
  for (const operation of aftermath.operations) {
    if (operation.kind === "grant_experience") {
      experience.push({
        name: memberName(session, operation.subject),
        amount: Number(operation.amount)
      });
      continue;
    }
    if (operation.kind === "add_item" || operation.kind === "consume_item") {
      store.push({
        kind: operation.kind,
        itemName:
          session.itemNames.get(operation.definitionId) ?? "something",
        amount: Number(operation.amount),
        // Owner zero is the company's own store, which is where a drop lands.
        // Anybody else is the character it came out of.
        ownerName:
          operation.subject === 0n
            ? undefined
            : memberName(session, operation.subject)
      });
    }
  }
  const levelUps = aftermath.levelUps.map((levelUp) => ({
    name: memberName(session, levelUp.member),
    fromLevel: levelUp.fromLevel,
    toLevel: levelUp.toLevel,
    points: growableStats
      .filter((stat) => levelUp.points[stat] > 0)
      .map((stat) => ({ stat, points: levelUp.points[stat] }))
  }));
  const moved = aftermath.advanced || aftermath.alreadyAdvanced;
  const target = session.nodesByStableId.get(aftermath.targetNodeId);
  return {
    nodeName,
    outcome: aftermath.outcome,
    canonicalHash: aftermath.canonicalHash,
    fallen: aftermath.fallen.map((id) => memberName(session, id)),
    characterLoss: aftermath.characterLoss,
    experience,
    levelUps,
    store,
    supplies: aftermath.store.map((stack) => ({
      itemName: session.itemNames.get(stack.itemId) ?? "something",
      quantity: stack.quantity
    })),
    joined: aftermath.recruited.map((member) => member.name),
    nextNodeName: moved ? target?.name ?? "the next chapter" : undefined,
    blockedReason: moved
      ? undefined
      : `The campaign could not move on: ${aftermath.error}.`,
    saved: aftermath.saved === "none",
    saveError: aftermath.saved === "none" ? undefined : aftermath.saved
  };
}

/**
 * Commits a finished battle. Called once, when the battle ends; safe to call
 * again, which is what makes it usable from a watcher.
 */
export function commitCampaignBattle(session: CampaignPlaySession): void {
  const battle = session.battle;
  if (!battle || session.committed || battle.outcome === "ongoing") return;
  session.committed = true;
  const aftermath = session.session.commit();
  rememberNames(session, aftermath.recruited);
  refreshRoster(session, aftermath.roster);
  session.joined = [];
  session.aftermath = reportAftermath(session, battle.nodeName, aftermath);
  session.phase = "aftermath";
}

/**
 * The player's single "keep going" verb: the next dialogue of a scene, the
 * aftermath of the battle that just ended, and then whatever the campaign says
 * follows: another scene, the next board, or the ending.
 */
export function continueCampaignPlaySession(session: CampaignPlaySession): void {
  if (session.phase === "scene" && session.scene) {
    if (session.scene.index + 1 < session.scene.dialogues.length) {
      session.scene = { ...session.scene, index: session.scene.index + 1 };
      return;
    }
    session.scene = undefined;
    settle(session);
    return;
  }
  if (session.phase === "battle" && session.battle) {
    if (session.battle.outcome === "ongoing") return;
    commitCampaignBattle(session);
    return;
  }
  if (session.phase === "aftermath") {
    // The board is handed back before walking on: the engine released it when
    // the next one was prepared, and this drops the wrapper that named it.
    session.battle?.encounter.dispose();
    session.battle = undefined;
    session.aftermath = undefined;
    settle(session);
  }
}

/**
 * One management gesture: give or take a thing, field or bench somebody.
 *
 * The gesture is one outcome batch through the engine's ordinary commit
 * machinery, written to the slot as it is made. Nothing is computed here: a
 * refusal is the campaign's own word for it, and the company redrawn afterwards
 * is the company the engine returned.
 */
export function manageCampaignCompany(
  session: CampaignPlaySession,
  verb: CampaignManagementVerb,
  member: bigint,
  itemId?: bigint
): void {
  if (session.phase !== "managing" || !session.company) return;
  // A board its author capped fields no more than it says, and the gesture that
  // would break the cap is refused here rather than committed and then undone
  // by the board. This is an early copy of `join_campaign_roster`'s own gate,
  // never a substitute for it, since taking the board refuses an over-cap
  // company however this counted. So it refuses under the roster's own word,
  // and counts nothing itself: both numbers come off the engine's company view.
  const company = session.company;
  const standing = company.members.find((candidate) => candidate.id === member);
  if (
    verb === "field" &&
    standing !== undefined &&
    !standing.fielded &&
    company.capacity !== 0 &&
    company.fielded >= company.capacity
  ) {
    session.company = reportCompany(
      session,
      company.nodeName,
      rosterErrorName("over_deployment_capacity")
    );
    return;
  }
  const result = session.session.manage(verb, member, itemId);
  refreshRoster(session, result.roster);
  const refused =
    result.error !== "none"
      ? result.error
      : result.outcomeError !== "none"
        ? result.outcomeError
        : undefined;
  session.company = reportCompany(
    session,
    session.company.nodeName,
    refused,
    result.saved && result.saveError !== "none" ? result.saveError : undefined
  );
}

/**
 * The player takes the board with the company as it stands.
 *
 * A board the roster refuses leaves them here, told why, with nothing
 * committed, which is what makes benching everybody an ordinary mistake rather
 * than a lost campaign.
 */
export function proceedFromCampaignCompany(session: CampaignPlaySession): void {
  if (session.phase !== "managing") return;
  session.proceeding = true;
  settle(session);
}

/** Releases the battle, the campaign session, and the dialogue cursor. */
export function endCampaignPlaySession(
  session: CampaignPlaySession | undefined
): void {
  if (!session) return;
  endPlaytest(session.battle);
  session.battle = undefined;
  session.session.dispose();
  session.dialogues.dispose();
}
