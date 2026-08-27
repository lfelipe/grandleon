<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// Full-screen play surface. No editor chrome, no field names, no identifiers:
// tap a character, then tap where to go or who to hit.
//
// Play runs the whole campaign the way the console does: the story scenes the
// engine's cursor names, the battle it points at, the branch it picks from the
// battle's objective results, and the authored ending. One verb, Continue,
// moves the story; the board works exactly as before during a battle.
//
// Unlike the playtest panel, there is no move/attack mode switch. The tapped
// cell decides: a highlighted square is a move, a marked enemy is an attack.
// One less concept for a child to hold. A character that knows an ability gets
// a button per ability, and pressing one is the single exception: an ability is
// aimed at a tile, and a bare tile means nothing until you know what is being
// aimed, so aiming is a state you enter and can put down again.
//
// A character carrying more than one weapon gets a button per weapon in the
// same row, and one of them is always held down: picking a weapon changes which
// enemies are marked, and the tap that follows is still a plain attack. A
// character carrying one weapon is offered no buttons at all, so the shortest
// path to a strike stays a single tap.
//
// A carried item gets a button too, after the spells and before "Done with
// them", exactly where the consoles put that row. It commits on the press
// rather than entering an aiming state, because an item reaches the hand that
// holds it and there is nothing to aim at; the button says what it gives back,
// from the engine's own forecast, and stays visible but disabled once it has
// been drunk so a player can see what ran out.
//
// A talkable neighbour gets a button too, after the items and before "Done
// with them", the place both consoles give the TALK row, because these rows
// are ordered by what each one costs and a talk costs an action point exactly
// as a strike does. Where a console offers one row and then asks the player to
// aim it, this offers one button per neighbour: a browser has the room to name
// them, and the name is the thing the player is choosing between. Which
// neighbours appear at all is the engine's own forecast and nothing else.
//
// That row of buttons is this client's unit action menu, and it carries the
// same rows in the same order the consoles list: what to strike with, what to
// cast, what to spend, who to talk to, then the rows that end the turn or end
// nothing at all: "Done with them", then "About them". That first row is how a
// player says they want nothing more from a character, and it finishes the
// character outright rather than only closing off its action: a walk still in
// hand is not a reason to keep offering it. The last of those opens the full
// information sheet, which is the same sheet the two consoles and the terminal
// draw, from the same fields in the same order (`platform/sheet/README.md`).

import {
  computed,
  onBeforeUnmount,
  onMounted,
  ref,
  shallowRef,
  triggerRef,
  watch
} from "vue";
import type { SourceProject } from "../generated/source-v1";
import {
  abilitiesFor,
  attackUnit,
  beginBattle,
  canAct,
  canDeploy,
  castAbility,
  dangerTiles,
  deployUnit,
  deployableTiles,
  itemRestore,
  itemsFor,
  legalCastTiles,
  legalMoves,
  legalTargets,
  moveUnit,
  pointsLeft,
  strikeChance,
  strikeLean,
  weaponsFor,
  takeAutomaticTurn,
  talk,
  talkTargets,
  unitSheet,
  useItem,
  waitUnit,
  type PlaytestState
} from "../domain/playtest-session";
import {
  campaignBattleLosses,
  continueCampaignPlaySession,
  endCampaignPlaySession,
  forgetKeptCampaign,
  keepCampaign,
  keptCampaignSlot,
  manageCampaignCompany,
  playableCampaignId,
  proceedFromCampaignCompany,
  restoreKeptCampaign,
  startCampaignPlaySession,
  type CampaignManagementVerb,
  type CampaignPlaySession
} from "../domain/campaign-playtest-session";
import {
  MISS_FRAMES,
  SEQUENCE_CELL_LUNGE,
  SEQUENCE_CELL_STAND,
  attackGesture,
  castCell,
  cursorEmphasised,
  effectBloomPeak,
  flinchOffsetCells,
  gestureFrames as gestureFramesFor,
  gestureLeadFrames,
  planRoute,
  projectileArcPeak,
  reachCovers,
  riseAndFall,
  slideBetween,
  slideFramesFor,
  slidePosition,
  strikeCell,
  walkCell,
  type AttackGesture,
  type RouteTile
} from "../domain/board-motion";
import {
  backdropByIndex,
  backdropGradient,
  speakerPortrait
} from "../domain/board-art";
import { stableContentId } from "../domain/encounter-simulation";

const assetBase = import.meta.env.BASE_URL;
import type { CampaignSlotStore } from "../domain/campaign-slot-store";
import { browserCampaignSlotStore } from "../platform/indexeddb-campaign-slots";
import PlaytestControls from "./PlaytestControls.vue";
import TacticalBoard from "./TacticalBoard.vue";

const props = withDefaults(
  defineProps<{
    project: SourceProject;
    ready: boolean;
    /** Pause between enemy activations, so a turn reads as a sequence. */
    activationDelayMs?: number;
    /**
     * Where a playtest campaign is kept between pages. The browser's IndexedDB
     * store by default, undefined in a browser that grants no persistence. In
     * that case Play still keeps a campaign between battles and simply does
     * not offer to pick one up after a reload.
     */
    keptCampaigns?: CampaignSlotStore;
  }>(),
  { activationDelayMs: 220 }
);
const emit = defineEmits<{ exit: [] }>();

// The session owns private mutable state (the engine-side cursor and
// encounter) and must not be wrapped in Vue's deep reactive proxy. Commands
// explicitly trigger a view refresh instead.
const session = shallowRef<CampaignPlaySession>();
const error = ref("");

// Where a playtest campaign lives between pages.
//
// The engine already writes the campaign to a slot after every battle and every
// management gesture; this carries those same bytes to somewhere the browser
// keeps and puts them back when the author asks for them. Nothing here decides
// when a campaign is saved, the session having decided that, and nothing here
// reads a byte of what it carries.
const kept = props.keptCampaigns ?? browserCampaignSlotStore();
// Whether there is a campaign to pick up for the game and campaign on screen.
// Asked of the store rather than assumed, because the answer is what decides
// whether the offer is there at all.
const hasKeptCampaign = ref(false);
// Why the kept campaign was not picked up, in the refusing layer's own word.
// Cleared by the next thing the author does, because it is about the press that
// just happened.
const resumeRefusal = ref("");
// Set when the browser would not take what the engine saved. The campaign is
// still correct in this tab; it is what will not be there after a reload.
const keepError = ref("");
// Which authored campaign Play runs. A game with one campaign never shows the
// choice; a game with several would otherwise only ever be playable from its
// first one, which is not a limit an authoring surface should have.
const chosenCampaignId = ref("");
const selectedUnitId = ref("");
// Board taps are mode-free, but the keyboard panel still needs a move/attack
// choice because it lists destinations and targets separately.
const keyboardAction = ref<"move" | "attack">("move");
// The player steers the first side; the second side plays itself from the
// behaviour authored on each placement.
const playerSide = "first" as const;
const thinking = ref(false);

// The battle is mirrored into its own shallow ref rather than derived with a
// computed: a computed that keeps returning the same (internally mutated)
// object never re-notifies its dependents, so highlights and hints would go
// stale. refresh() is the one way state changes reach the view.
const state = shallowRef<PlaytestState>();
const scene = computed(() =>
  session.value?.phase === "scene" ? session.value.scene : undefined
);
// What the last battle did to the campaign, between one board and the next.
// Every line of it is a number the engine handed over.
const aftermath = computed(() =>
  session.value?.phase === "aftermath" ? session.value.aftermath : undefined
);
// Who is still with the company, and what the campaign holds for each of them.
// The kit is what they will take onto the next board, the campaign's rather
// than their unit type's, so a draught drunk in the battle just fought is
// missing from it here.
const carriers = computed(() =>
  aftermath.value === undefined
    ? []
    : (session.value?.roster ?? []).filter(
        (member) => member.availability !== "dead"
      )
);
// Who the battle on screen has taken, by the name the author gave them.
//
// Play draws a board and no log, so without this a character of the player's
// simply stops being there: the token goes, nothing is said, and the first
// words about it are on the screen after the battle, which in a campaign is
// where they are told it was permanent, having never been told it happened.
// The list grows as the battle goes and stays up for the rest of it, because
// somebody who is gone is still gone three activations later.
const losses = computed(() =>
  session.value ? campaignBattleLosses(session.value) : []
);
// Whether the list above is a list of deaths. Read off the battle the session is
// running rather than off the project directly, because it is the record the
// engine was handed that decides what a fall costs, and a screen that consulted
// a second source could word a battle differently from the way it was fought.
const lossesAreForever = computed(
  () => session.value?.battle?.characterLoss !== "recoverable"
);
// The company between battles, while the player arranges it. Every stack, every
// availability and every "the next board has room for them" is read out of
// committed campaign state; this screen decides nothing and offers only what
// the engine would accept.
const company = computed(() =>
  session.value?.phase === "managing" ? session.value.company : undefined
);
// The campaigns this game has, for a project that has more than one.
const playableCampaigns = computed(() =>
  (props.project.campaigns ?? []).filter((campaign) => campaign.flow)
);
const currentDialogue = computed(() =>
  scene.value?.dialogues[scene.value.index]
);
// What the standing scene is drawn against. The index comes from the compiled
// record the engine decoded, so what a player sees here is what the console
// would draw from the same bytes. A scene naming none draws the plain screen
// it always drew.
const sceneBackdrop = computed(() =>
  backdropByIndex(currentDialogue.value?.backdrop)
);
const sceneBackdropStyle = computed(() =>
  sceneBackdrop.value
    ? { background: backdropGradient(sceneBackdrop.value) }
    : undefined
);
// The characters this scene cast, keyed the way the compiled record keys them.
// The engine hands back unit type identities, so the project's own identifiers
// are hashed once per scene to meet them rather than the identities being
// turned back into names, which cannot be done: a stable identity is a hash
// of the name and not a container for it.
const castUnitTypeIds = computed(() => {
  const byIdentity = new Map<bigint, string>();
  for (const unitType of props.project.unitTypes ?? []) {
    byIdentity.set(stableContentId(unitType.id), unitType.id);
  }
  return (currentDialogue.value?.cast ?? []).map((identity) =>
    byIdentity.get(identity)
  );
});
// The portrait for each line of the standing scene, by position. A line the
// scene named nobody for, and one naming a character this project no longer
// holds, get none rather than somebody else's.
const linePortraits = computed(() =>
  (currentDialogue.value?.lines ?? []).map((line) => {
    if (line.castEntry === 0) return undefined;
    const sprite = speakerPortrait(
      props.project,
      castUnitTypeIds.value[line.castEntry - 1]
    );
    return sprite === undefined ? undefined : assetBase + sprite;
  })
);

function refresh() {
  state.value = session.value?.battle;
  triggerRef(state);
  triggerRef(session);
}

// ---------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------
//
// Everything below is decoration and nothing below is state. A command commits
// the moment it is issued, and the board renders the world the engine holds;
// what animates is where one token is *drawn* relative to the cell it already
// stands in. That is deliberate rather than convenient: it means a tap during a
// slide is never swallowed, and it means every assertion about what the board
// says after a command is true on the frame the command lands.
//
// Frames are counted, not timed. One animation frame is one browser animation
// frame, the same unit the two consoles count in, so a move is the same six
// frames a tile everywhere.

/**
 * One thing the board does: a token walking a route, a token knocked away from
 * a blow, or a cell marked where a blow missed.
 *
 * They queue rather than replace each other, because a command and the enemy's
 * answer to it arrive inside one task: without a queue the player's own move
 * would be cut off by the reply to it, which is exactly the picture a queue
 * exists to stop. The consoles get the ordering for free, because an animation
 * there blocks the frames it runs for; this is the browser's version of it.
 */
type Gesture =
  | { kind: "slide"; unitId: string; origin: RouteTile; route: RouteTile[] }
  | {
      kind: "flinch";
      unitId: string;
      towardX: number;
      towardY: number;
      /** Whoever threw the blow, so the board can pose them coiled into it. */
      strikerId: string;
      /** Where they threw it from, so a bolt has somewhere to start. */
      origin: RouteTile | undefined;
      /** Where it landed, which is where the flare opens and the bolt ends. */
      target: RouteTile | undefined;
      /** Which of the three gestures, derived exactly as the consoles derive it. */
      gesture: AttackGesture;
      /** Their separation on the engine's own metric, which sets the flight. */
      separation: number;
      /** Whether anything was taken. A miss is thrown just the same. */
      landed: boolean;
    }
  | { kind: "miss"; key: string };

const gestures = ref<Gesture[]>([]);
const gestureFrame = ref(0);

// How many gestures may be waiting. A queue is a picture running behind the
// state, and a picture too far behind is worse than no picture: past this the
// board catches up in one frame and starts again from what just happened.
const gestureBacklog = 16;

/** How many frames a gesture is drawn for. */
function gestureFrames(gesture: Gesture): number {
  if (gesture.kind === "slide") return slideFramesFor(gesture.route.length);
  if (gesture.kind === "flinch") {
    return gestureFramesFor(gesture.gesture, gesture.separation, gesture.landed);
  }
  return MISS_FRAMES;
}

/**
 * Which gesture a blow between these two is drawn as, folded over the project's
 * own ability records. The browser's half of `client::gesture_for`, asking the
 * same one question: could a damaging magical ability this striker knows have
 * crossed this separation? Damaging only, because mending is not an attack, and
 * magical only, because Power Strike is an ability a body throws with its arm.
 */
function gestureBetween(
  striker:
    | { abilityIds: readonly string[]; minimumReach: number }
    | undefined,
  separation: number
): AttackGesture {
  if (!striker) return attackGesture(separation, false, false);
  const known = props.project.abilities ?? [];
  const magic = striker.abilityIds.some((abilityId) => {
    const ability = known.find((candidate) => candidate.id === abilityId);
    if (!ability) return false;
    if (ability.kind !== "damage") return false;
    if (ability.damageType !== "magical") return false;
    return reachCovers(
      separation, ability.minimumRange, ability.maximumRange
    );
  });
  // "Cannot strike an adjacent tile" is what tells a bow from a polearm, and it
  // is a number the board already carries for the weapon in hand.
  return attackGesture(separation, magic, striker.minimumReach > 1);
}

/** Orthogonal steps, which is the metric the engine measures a reach band in. */
function separationBetween(
  lhs: { x: number; y: number },
  rhs: { x: number; y: number }
): number {
  return Math.abs(lhs.x - rhs.x) + Math.abs(lhs.y - rhs.y);
}

// The cursor's pulse phase. Reset whenever the selection changes, so the pulse
// is a property of how long this tile has been chosen rather than of how long
// the tab has been open.
const pulseFrame = ref(0);
const cursorEmphasis = computed(() => cursorEmphasised(pulseFrame.value));

let frameHandle: number | null = null;

const playing = computed<Gesture | undefined>(() => gestures.value[0]);

/** Where one token is drawn away from its own cell this frame, in cells. */
const boardMotion = computed(() => {
  const gesture = playing.value;
  if (!gesture) return undefined;
  if (gesture.kind === "slide") {
    const at = slidePosition(gesture.origin, gesture.route, gestureFrame.value);
    const home = gesture.route[gesture.route.length - 1] ?? gesture.origin;
    return {
      unitId: gesture.unitId,
      cellDx: at.x - home.x,
      cellDy: at.y - home.y
    };
  }
  if (gesture.kind === "flinch") {
    // The knock belongs to the *resolution*, not to the whole gesture: a bolt
    // in the air and a pose being held have not moved anything yet, and nothing
    // is ever knocked by a blow that missed.
    const lead = gestureLeadFrames(gesture.gesture, gesture.separation);
    if (!gesture.landed) return undefined;
    return {
      unitId: gesture.unitId,
      cellDx: flinchOffsetCells(gestureFrame.value - lead, gesture.towardX),
      cellDy: flinchOffsetCells(gestureFrame.value - lead, gesture.towardY)
    };
  }
  return undefined;
});

/**
 * Which unit is posed, and in which cell of its strip, this frame.
 *
 * The same two functions the consoles call, on the same frame count: a walking
 * token alternates the two walk cells one to a tile, and a striking token is
 * coiled for exactly as long as the token it hit is knocked away. Both return
 * the standing cell at the ends of their gesture, and a standing token is drawn
 * from its own sprite, so an empty answer here, which is every frame of a
 * still board, leaves the board node-for-node what it was.
 */
const boardCells = computed<Record<string, number> | undefined>(() => {
  const gesture = playing.value;
  if (!gesture) return undefined;
  if (gesture.kind === "slide") {
    const cell = walkCell(
      gestureFrame.value, slideFramesFor(gesture.route.length)
    );
    return cell === SEQUENCE_CELL_STAND ? undefined : { [gesture.unitId]: cell };
  }
  if (gesture.kind === "flinch" && gesture.strikerId !== "") {
    // A cast holds its own pose through the hold; a shot holds the lunge for
    // the whole flight, because a bow's release is a sword's coil at a longer
    // distance; a swing throws the lunge over the frames the blow is landing.
    const lead = gestureLeadFrames(gesture.gesture, gesture.separation);
    let cell = SEQUENCE_CELL_STAND;
    if (gestureFrame.value < lead) {
      cell = gesture.gesture === "cast"
        ? castCell(gestureFrame.value)
        : SEQUENCE_CELL_LUNGE;
    } else if (gesture.gesture === "swing") {
      cell = strikeCell(gestureFrame.value - lead);
    }
    return cell === SEQUENCE_CELL_STAND
      ? undefined
      : { [gesture.strikerId]: cell };
  }
  return undefined;
});

/**
 * The travelling mark: a bolt crossing the board, or the flare a cast opens on
 * the tile it resolves on. Neither is a sprite and neither costs a sequence
 * cell. They are drawn from the board's own primitives, which is exactly what
 * left the last cell free for a pose.
 *
 * Both are absent on the frame their gesture ends, because `riseAndFall` is
 * zero at both ends, so a settled board is node-for-node what it always was.
 */
const boardMark = computed(() => {
  const gesture = playing.value;
  if (!gesture || gesture.kind !== "flinch") return undefined;
  const lead = gestureLeadFrames(gesture.gesture, gesture.separation);
  if (gestureFrame.value >= lead) return undefined;
  const target = gesture.target;
  if (!target) return undefined;
  if (gesture.gesture === "cast") {
    const width = riseAndFall(gestureFrame.value, lead, effectBloomPeak(100));
    if (width <= 0) return undefined;
    return { kind: "bloom" as const, x: target.x, y: target.y, size: width / 100 };
  }
  const origin = gesture.origin;
  if (gesture.gesture !== "shot" || !origin) return undefined;
  return {
    kind: "bolt" as const,
    // In cells, and on the same interpolation a token walks on, so the browser
    // and the consoles put the bolt in the same place on the same frame.
    x: slideBetween(origin.x * 100, target.x * 100, gestureFrame.value, lead) / 100,
    y:
      slideBetween(origin.y * 100, target.y * 100, gestureFrame.value, lead) /
        100 -
      riseAndFall(gestureFrame.value, lead, projectileArcPeak(100)) / 100,
    size: 0.16
  };
});

/** The cell a blow just missed, while that gesture is the one being drawn. */
const missKey = computed(() =>
  playing.value?.kind === "miss" ? playing.value.key : ""
);

function scheduleFrames() {
  if (frameHandle !== null) return;
  if (typeof requestAnimationFrame !== "function") return;
  if (gestures.value.length === 0 && selectedUnitId.value === "") return;
  frameHandle = requestAnimationFrame(advanceFrame);
}

function stopFrames() {
  if (frameHandle === null) return;
  if (typeof cancelAnimationFrame === "function") {
    cancelAnimationFrame(frameHandle);
  }
  frameHandle = null;
}

function advanceFrame() {
  frameHandle = null;
  const gesture = playing.value;
  if (gesture) {
    gestureFrame.value += 1;
    if (gestureFrame.value >= gestureFrames(gesture)) {
      gestures.value = gestures.value.slice(1);
      gestureFrame.value = 0;
    }
  }
  pulseFrame.value += 1;
  scheduleFrames();
}

function enqueue(gesture: Gesture) {
  if (gestures.value.length >= gestureBacklog) {
    gestures.value = [gesture];
    gestureFrame.value = 0;
  } else {
    gestures.value = [...gestures.value, gesture];
  }
  scheduleFrames();
}

/**
 * Everything a gesture needs to know about the board it is about to change:
 * where everybody stood, how much of them was left, and, for the side about
 * to act, the tiles a walk by each of its characters may be on.
 *
 * That last set is the engine's reachability answer plus the tiles the
 * character's own side holds. A walk passes through an ally and may not stop on
 * one, so the two are different sets, and a route planned over the query alone
 * would find a hole wherever somebody filed past a fellow.
 *
 * It is captured *before* the command because that is the only moment it
 * answers about the state the move is made from, and because a route derived
 * from any other answer would be a route this client invented.
 */
function captureBoard(current: PlaytestState, acting: string) {
  const positions = new Map<string, RouteTile>();
  const health = new Map<string, number>();
  const standing = new Set<string>();
  const crossable = new Map<string, Set<string>>();
  for (const unit of current.units) {
    positions.set(unit.id, { x: unit.x, y: unit.y });
    health.set(unit.id, unit.health);
    // Who was actually holding a tile before the command, so a wave that
    // marched in during it is not drawn walking there. Its `x` and `y`
    // beforehand were the tile the content asked for rather than one it stood
    // on, and a slide out of that tile would be a walk nobody took.
    if (unit.onBoard) standing.add(unit.id);
    if (unit.onBoard && unit.side === acting) {
      const tiles = new Set(
        legalMoves(current, unit.id).map(([x, y]) => `${x}:${y}`)
      );
      for (const other of current.units) {
        if (other.id === unit.id || !other.onBoard) continue;
        if (other.side !== unit.side) continue;
        tiles.add(`${other.x}:${other.y}`);
      }
      crossable.set(unit.id, tiles);
    }
  }
  return { positions, health, standing, crossable };
}

/**
 * Turns what changed into what is drawn: whoever moved slides along the route
 * the engine's own answer allows, and whoever lost health is knocked away from
 * whoever is standing where the blow came from.
 */
function playMotion(
  before: ReturnType<typeof captureBoard>,
  current: PlaytestState,
  actorId: string
) {
  const actor = current.units.find((unit) => unit.id === actorId);
  for (const unit of current.units) {
    const was = before.positions.get(unit.id);
    // A walk is a tile held before and a tile held after. Somebody who left the
    // board during the command, felled or talked away, is not slid to where
    // they no longer are, and somebody who was not on it beforehand did not
    // walk in, they arrived.
    if (!was || !unit.onBoard || !before.standing.has(unit.id)) continue;
    if (was.x === unit.x && was.y === unit.y) continue;
    const crossable = before.crossable.get(unit.id) ?? new Set<string>();
    const route = planRoute(
      was,
      { x: unit.x, y: unit.y },
      current.width,
      current.height,
      crossable
    );
    // No route inside the engine's own answer means the straight line: one
    // segment between the two cells, at the same speed per tile.
    enqueue({
      kind: "slide",
      unitId: unit.id,
      origin: was,
      route: route.length > 0 ? route : [{ x: unit.x, y: unit.y }]
    });
    break;
  }
  // Every unit that lost health, and not merely the first of them. Breaking
  // after the first drops two whole things on the floor: a counterattack,
  // which is the second damaged unit of the same command, and every target of
  // an area cast after the first. The engine resolves both, so both have to be
  // drawn.
  // The blow the command made comes first and the answer to it comes second,
  // which is the order the engine resolves them in. Reproducing it here takes
  // one sort and not a second rule: **the actor is the only unit a counter can
  // damage**, so putting the actor's own flinch last is exactly "the counter
  // after the attack". For an area cast, where no target is the actor, the
  // order is the ascending order this loop already walks, which is the ascending
  // identifier order the engine damages in.
  const damaged = current.units.filter((unit) => {
    const had = before.health.get(unit.id);
    return had !== undefined && unit.health < had;
  });
  const struckFirst = [
    ...damaged.filter((unit) => unit.id !== actor?.id),
    ...damaged.filter((unit) => unit.id === actor?.id)
  ];
  for (const unit of struckFirst) {
    // A counter is thrown by whoever was struck, back at whoever struck them,
    // and the actor of the command is not the thrower of it. Reading the
    // thrower off the board rather than off the command is what lets the answer
    // be drawn as the attack it is.
    const counter = actor !== undefined && unit.id === actor.id;
    const striker = counter
      ? current.units.find(
          (candidate) =>
            candidate.id !== unit.id &&
            before.health.get(candidate.id) !== undefined &&
            candidate.onBoard &&
            candidate.side !== unit.side
        )
      : actor;
    enqueueBlow(unit, striker, true);
  }
}

/**
 * One thrown blow, landed or not, with its gesture derived and its ends known.
 * Shared by the landing path and the missing one so the two cannot drift.
 */
function enqueueBlow(
  struck: { id: string; x: number; y: number },
  striker:
    | {
        id: string;
        x: number;
        y: number;
        abilityIds: readonly string[];
        minimumReach: number;
      }
    | undefined,
  landed: boolean
) {
  const separation = striker ? separationBetween(struck, striker) : 0;
  const throws = striker !== undefined && striker.id !== struck.id;
  enqueue({
    kind: "flinch",
    unitId: struck.id,
    towardX: striker ? struck.x - striker.x : 0,
    towardY: striker ? struck.y - striker.y : 0,
    strikerId: throws ? striker.id : "",
    origin: throws ? { x: striker.x, y: striker.y } : undefined,
    target: { x: struck.x, y: struck.y },
    gesture: gestureBetween(throws ? striker : undefined, separation),
    separation,
    landed
  });
}

/** A blow that took nothing still has to look like something happened. */
function playMiss(x: number, y: number) {
  enqueue({ kind: "miss", key: `${x}:${y}` });
}

/**
 * A blow aimed at a tile that took nothing off whoever was standing there.
 *
 * Drawn as the blow it was, thrown and travelled if it had distance to
 * travel, rather than as a flash on a square. Nothing is knocked, which stays
 * the whole of what a miss says.
 *
 * An empty tile falls back to the flash, because there is nobody there to have
 * been missed and a cast aimed at open ground is a legal thing to do.
 */
function playMissedBlow(
  before: ReturnType<typeof captureBoard>,
  current: PlaytestState,
  actorId: string,
  x: number,
  y: number
) {
  // Read the *aimed-at* board, not the board afterwards. Whoever the blow was
  // aimed at may have fallen, and a unit that fell is not a unit that was
  // missed. Looking them up in the state after the command would have turned
  // every killing blow into a miss flash.
  let aimed: string | undefined;
  for (const [unitId, was] of before.positions) {
    if (was.x === x && was.y === y) aimed = unitId;
  }
  const had = aimed === undefined ? undefined : before.health.get(aimed);
  if (aimed === undefined || had === undefined) {
    // Nobody was standing there. A cast aimed at open ground is a legal thing
    // to do, and the flash is all there is to say about it.
    playMiss(x, y);
    return;
  }
  const struck = current.units.find((unit) => unit.id === aimed);
  if (!struck || struck.health < had) return;
  const actor = current.units.find((unit) => unit.id === actorId);
  enqueueBlow(struck, actor, false);
}

watch(selectedUnitId, () => {
  pulseFrame.value = 0;
  scheduleFrames();
});

const selectedUnit = computed(() =>
  state.value?.units.find((unit) => unit.id === selectedUnitId.value)
);
// Whether the character under the buttons may still be given an order. A spent
// character stays selectable, because the player wants to look at their whole
// line, but every row that commits something is a row the engine would refuse,
// so it is shown disabled rather than offered and then turned down.
const selectedCanAct = computed(() => {
  const current = state.value;
  if (!current || !selectedUnitId.value) return false;
  return canAct(current, selectedUnitId.value);
});
// While the board is being arranged the glowing squares are the region rather
// than the movement range. One set of lit tiles and one gesture: pick a
// character, tap a glowing square. The player is doing the same thing either
// way and the engine is the one that knows which rule applies.
const legalMoveCoordinates = computed(() => {
  const current = state.value;
  if (!current) return [];
  return current.deploying
    ? deployableTiles(current, selectedUnitId.value)
    : legalMoves(current, selectedUnitId.value);
});
const legalMoveKeys = computed(
  () => new Set(legalMoveCoordinates.value.map(([x, y]) => `${x}:${y}`))
);
// Which carried weapon the next strike uses, or "" for the weapon in hand.
// Unlike aiming an ability this is not a mode: a weapon is always chosen, and
// choosing another only changes which enemies are within reach.
const chosenWeaponId = ref("");
const weaponChoices = computed(() =>
  state.value ? weaponsFor(state.value, selectedUnitId.value) : []
);
// The weapon in hand is the default, and stays the default whenever the
// selection changes under the choice, because a weapon another character
// carries is not a weapon this one can use.
const activeWeaponId = computed(() =>
  weaponChoices.value.some((weapon) => weapon.id === chosenWeaponId.value)
    ? chosenWeaponId.value
    : (weaponChoices.value[0]?.id ?? "")
);
const activeWeapon = computed(() =>
  weaponChoices.value.find((weapon) => weapon.id === activeWeaponId.value)
);
const legalTargetIds = computed(
  () =>
    new Set(
      state.value
        ? legalTargets(
            state.value,
            selectedUnitId.value,
            activeWeaponId.value || undefined
          )
        : []
    )
);
// Which of the places this character may go are unsafe to stand in, from the
// engine's own danger query, the same Fire Emblem reading the consoles give,
// shown at the same moment: while a character is picked up and the player
// is deciding where to put it down.
//
// **Intersected with the reach rather than laid over it.** A lit tile is a
// tile this character may step onto, tinted where something threatens it, and
// the union of the two lit sets is exactly the reachable set. Drawing the
// opposing side's whole zone puts most of a board in red with the player's own
// moves buried underneath.
const dangerTileKeys = computed(() => {
  const current = state.value;
  if (!current || !selectedUnitId.value) return new Set<string>();
  const enemy = playerSide === "first" ? "second" : "first";
  const reach = legalMoveKeys.value;
  const threatened = new Set<string>();
  for (const [x, y] of dangerTiles(current, enemy)) {
    const key = `${x}:${y}`;
    if (reach.has(key)) threatened.add(key);
  }
  return threatened;
});
// The ability the player has picked and is now aiming, or "" when they are
// moving and attacking. Aiming is a mode, unlike move-or-attack, because an
// ability is aimed at a tile and an empty tile means nothing on its own.
const aimingAbilityId = ref("");
const abilityChoices = computed(() =>
  state.value ? abilitiesFor(props.project, state.value, selectedUnitId.value) : []
);
const aimingAbility = computed(() =>
  abilityChoices.value.find((ability) => ability.id === aimingAbilityId.value)
);
const castTileKeys = computed(() => {
  if (!state.value || !aimingAbility.value) return new Set<string>();
  return new Set(
    legalCastTiles(props.project, state.value, selectedUnitId.value, aimingAbilityId.value)
      .map(([x, y]) => `${x}:${y}`)
  );
});

// The pack, in the order the unit type lists it: the console action menu's
// item row, one button per carried item. The label carries what the engine
// forecasts the use would give back, so the number on the button is the number
// the battle delivers.
const itemChoices = computed(() =>
  state.value ? itemsFor(state.value, selectedUnitId.value) : []
);

function itemLabel(item: { id: string; name: string; count: number }): string {
  const restored =
    state.value && selectedUnitId.value
      ? itemRestore(state.value, selectedUnitId.value, item.id)
      : null;
  const gain = restored === null ? "" : ` +${restored}`;
  return `${item.name}${gain} (${item.count})`;
}

// Who the selected character could talk off the board, straight from the
// engine's forecast, one button each. Presentation decides nothing about who
// is on this list, which is why a project that authors no talk draws no
// buttons here without this file knowing anything about it.
const talkChoices = computed(() =>
  state.value ? talkTargets(state.value, selectedUnitId.value) : []
);

function speakTo(targetId: string) {
  const current = state.value;
  if (!current || !selectedUnitId.value) return;
  if (talk(props.project, current, selectedUnitId.value, targetId)) {
    refresh();
    selectedUnitId.value = "";
    aimingAbilityId.value = "";
    chosenWeaponId.value = "";
    showingSheet.value = false;
    void playOpposingSide();
  }
}

function spend(itemId: string) {
  const current = state.value;
  if (!current || !selectedUnitId.value) return;
  if (useItem(props.project, current, selectedUnitId.value, itemId)) {
    refresh();
    selectedUnitId.value = "";
    aimingAbilityId.value = "";
    chosenWeaponId.value = "";
    showingSheet.value = false;
    void playOpposingSide();
  }
}

// How often the strike the player is being offered would land, straight off
// the engine's forecast. Null when nothing is in reach or the weapon always
// lands, so a project whose weapons never miss draws exactly what it drew
// before. Presentation never derives this: the chance shown is the chance
// rolled.
const chanceToHit = computed(() => {
  const current = state.value;
  if (!current || !selectedUnitId.value || aimingAbility.value) return null;
  return strikeChance(
    current,
    selectedUnitId.value,
    activeWeaponId.value || undefined
  );
});

// Which way the weapon matchup leans on that same strike.
//
// Read separately from the chance rather than folded into it, because the two
// are not the same fact: a certain strike shows no chance at all and its blow
// can still be worth a point more or less for the weapon it meets. Both come
// off one forecast in the session; nothing here works the table out.
const weaponLean = computed(() => {
  const current = state.value;
  if (!current || !selectedUnitId.value || aimingAbility.value) return "none";
  return strikeLean(
    current,
    selectedUnitId.value,
    activeWeaponId.value || undefined
  );
});

// The full information sheet, opened deliberately out of the same row of
// actions the consoles hang it off. It is a thing to read rather than a thing
// to choose, so opening it holds no state a board tap could commit: the
// selection, the chosen weapon and any aimed ability are exactly where they
// were when it closes.
const showingSheet = ref(false);
const sheet = computed(() =>
  state.value && selectedUnitId.value
    ? unitSheet(props.project, state.value, selectedUnitId.value)
    : undefined
);
// The campaign half of the sheet: the LEVEL and EXP row the consoles and the
// terminal draw when there is a campaign, and leave off entirely when there is
// not (`platform/sheet/include/grandleon/sheet/unit_sheet.hpp`: the context is
// a pointer, and null means no row rather than level one).
//
// Which member this character is comes from the engine's own join, and the two
// numbers come from the roster it reports. Nothing here counts anything.
const sheetCampaign = computed(() => {
  const active = session.value;
  if (!active || !selectedUnitId.value) return undefined;
  const member = active.membersByPlacement.get(selectedUnitId.value);
  if (member === undefined) return undefined;
  return active.roster.find((entry) => entry.id === member);
});

function openSheet() {
  if (sheet.value) showingSheet.value = true;
}

function closeSheet() {
  showingSheet.value = false;
}

/** A reach band the way every client writes one: one number, or a range. */
function band(minimum: number, maximum: number): string {
  return minimum === maximum ? `${minimum}` : `${minimum}–${maximum}`;
}

function aim(abilityId: string) {
  aimingAbilityId.value = aimingAbilityId.value === abilityId ? "" : abilityId;
}

function choose(weaponId: string) {
  chosenWeaponId.value = weaponId;
  aimingAbilityId.value = "";
}

const finished = computed(
  () => state.value !== undefined && state.value.outcome !== "ongoing"
);
const headline = computed(() => {
  if (!props.ready) return "Getting the game ready…";
  if (error.value) return "Cannot play yet";
  const active = session.value;
  if (!active) return "Ready to play";
  if (active.phase === "scene") {
    return currentDialogue.value?.name ?? "The story so far";
  }
  if (active.phase === "ended") return active.endingName ?? "The End";
  if (active.phase === "stalled") return "The story cannot continue";
  if (active.phase === "aftermath") {
    return `After ${active.aftermath?.nodeName ?? "the Stage"}`;
  }
  // The company, between Stages. Named for what is next rather than for what
  // is past, because everything the player does here is for the Stage ahead.
  if (active.phase === "managing") {
    return `Before ${active.company?.nodeName ?? "the Stage"}`;
  }
  const current = state.value;
  if (!current) return "Ready to play";
  if (current.deploying) {
    return selectedUnitId.value
      ? `Where should ${selectedUnit.value?.name ?? "they"} stand?`
      : "Deployment. Pick someone.";
  }
  if (current.outcome === "ongoing") {
    if (thinking.value || current.activeSide !== playerSide) {
      return "Red is thinking…";
    }
    if (selectedUnitId.value) {
      const name = selectedUnit.value?.name ?? "they";
      // A character that has walked has not finished: the walk was one point of
      // its turn and the action is the other. Said out loud, and with the way
      // out named, because a player who walked and saw no move left is the
      // player who reported this board as stopped.
      if (selectedUnit.value?.hasMoved) {
        return `${name} has moved. Strike, or say you are done with them`;
      }
      const left = pointsLeft(current, selectedUnitId.value);
      return left > 1
        ? `${name} can still do ${left} things`
        : `Where should ${name} go?`;
    }
    return "Your turn. Pick someone.";
  }
  // Won by the player rather than won by the first side. The two coincide
  // while `playerSide` is the first, and asking it this way keeps the sentence
  // true if it ever is not.
  const playerWon =
    (current.outcome === "first_side_won") === (playerSide === "first");
  const winner = playerWon ? "Your side wins!" : "The enemy wins!";
  // The author wrote an ending for this moment; show its name, not only who won.
  return current.terminalNodeName
    ? `${winner} (${current.terminalNodeName})`
    : winner;
});
// Whether anything playable has been authored at all. Presentation only: it
// decides which words to show, never whether the engine will accept the game.
const hasAuthoredBattle = computed(() =>
  (props.project.campaigns ?? []).some((campaign) =>
    campaign.flow?.nodes.some((node) =>
      node.kind === "encounter" && (node.placements?.length ?? 0) > 0
    )
  )
);

/**
 * Founds a campaign, without touching the one the browser is keeping.
 *
 * Opening Play comes through here, and opening Play must not throw a campaign
 * away: the author has not asked for anything yet, and the offer to pick the
 * kept one up is right there. What replaces a kept campaign is the deliberate
 * press of `startOver` below, and the first battle this founded campaign
 * commits, which is the campaign actually being played.
 */
function start() {
  endCampaignPlaySession(session.value);
  const started = startCampaignPlaySession(props.project, {
    ...(chosenCampaignId.value ? { campaignId: chosenCampaignId.value } : {}),
    // A fresh press of Play founds a fresh campaign. Picking one up again is
    // what "Pick up where I left off" is for, below.
    resume: false
  });
  session.value = started.session;
  error.value = started.error ?? "";
  selectedUnitId.value = "";
  aimingAbilityId.value = "";
  chosenWeaponId.value = "";
  showingSheet.value = false;
  thinking.value = false;
  refresh();
  maybePlayOpposingSide();
}

/** Which campaign Play is running, for the store to be asked about. */
function currentCampaignId(): string | undefined {
  return playableCampaignId(
    props.project,
    chosenCampaignId.value || undefined
  );
}

/**
 * Asks the browser whether it is holding a campaign for what is on screen.
 *
 * The answer decides whether the offer to pick one up exists, so it is asked
 * again whenever the game or the campaign changes and after anything that could
 * have written or forgotten one.
 */
async function probeKeptCampaign() {
  const campaignId = currentCampaignId();
  if (!kept || !campaignId) {
    hasKeptCampaign.value = false;
    return;
  }
  try {
    hasKeptCampaign.value =
      (await kept.read(keptCampaignSlot(props.project, campaignId))) !==
      undefined;
  } catch {
    // A store that cannot be read is a store with nothing in it as far as this
    // surface is concerned. Play still works; it just does not offer to resume.
    hasKeptCampaign.value = false;
  }
}

/**
 * Carries what the session just saved out to the browser.
 *
 * Called after the two things that commit, a finished battle and a management
 * gesture, which are exactly the moments the session already wrote its slot.
 * This adds no save; it makes the ones already made durable.
 */
async function keepTheCampaign() {
  const active = session.value;
  if (!active || !kept) return;
  try {
    if (await keepCampaign(active, kept)) {
      hasKeptCampaign.value = true;
      keepError.value = "";
    }
  } catch (failure) {
    // The campaign in this tab is unharmed and correct; what failed is keeping
    // it for the next page. Say that rather than implying the battle was lost.
    keepError.value =
      "This browser would not keep the campaign, so it will not be here " +
      `after a reload: ${
        failure instanceof Error ? failure.message : String(failure)
      }`;
  }
}

/**
 * Starts the enemy-turn loop whenever the battle on screen is theirs. The
 * engine may open an encounter with the unattended side active, initiative
 * order deciding, so this runs after every battle start rather than only after
 * a player command.
 */
function maybePlayOpposingSide() {
  const current = state.value;
  if (!current || current.outcome !== "ongoing") return;
  if (current.activeSide !== playerSide) void playOpposingSide();
}

/**
 * Runs the opposing side's activations until control returns to the player.
 *
 * Bounded rather than looped-until-done: a behaviour that cannot act would
 * otherwise spin, and an encounter that will not hand the turn back is a bug
 * worth surfacing as a stuck board rather than a frozen tab.
 *
 * The loop is pinned to the battle it was started for. Start over, Continue,
 * and leaving Play all replace or clear that battle, so a superseded loop
 * stops instead of driving an encounter that was already released.
 */
async function playOpposingSide() {
  const active = session.value;
  const current = active?.battle;
  if (!active || !current) return;
  const live = () =>
    session.value === active && active.battle === current;
  // Read through a function so the compiler does not narrow the active side
  // across activations that deliberately change it.
  const opponentHasTurn = () =>
    current.outcome === "ongoing" && current.activeSide !== playerSide;
  thinking.value = true;
  try {
    for (let activation = 0; activation < 64; activation += 1) {
      if (!live() || !opponentHasTurn()) break;
      const before = captureBoard(current, current.activeSide);
      const acting = current.activeUnitId;
      if (!takeAutomaticTurn(props.project, current, current.activeSide)) break;
      refresh();
      playMotion(before, current, acting);
      // Pause only *between* enemy activations. A trailing pause would leave
      // the board locked after control has already returned to the player,
      // which reads as the game having frozen.
      if (!opponentHasTurn()) break;
      if (props.activationDelayMs > 0) {
        await new Promise((resolve) => setTimeout(resolve, props.activationDelayMs));
      }
    }
  } finally {
    // A superseded loop leaves the flag to whoever owns the new battle.
    if (live()) {
      thinking.value = false;
      refresh();
    }
  }
}

/**
 * Picks the campaign up where it was left, across a reload.
 *
 * The kept bytes go back onto the engine's own device and the session is begun
 * asking to resume, which is the only thing that decides whether they are a
 * campaign this content can carry on. A save that is refused leaves a freshly
 * founded campaign standing and `refusal` set. This surface never dresses one
 * up as the other.
 */
async function resume() {
  const campaignId = currentCampaignId();
  if (!campaignId) return;
  resumeRefusal.value = "";
  if (kept) await restoreKeptCampaign(props.project, campaignId, kept);
  endCampaignPlaySession(session.value);
  const started = startCampaignPlaySession(props.project, {
    campaignId,
    resume: true
  });
  session.value = started.session;
  error.value = started.error ?? "";
  resumeRefusal.value = started.refusal ?? "";
  selectedUnitId.value = "";
  aimingAbilityId.value = "";
  chosenWeaponId.value = "";
  showingSheet.value = false;
  thinking.value = false;
  refresh();
  maybePlayOpposingSide();
  await probeKeptCampaign();
}

/**
 * Founds a campaign and replaces the one the browser was keeping.
 *
 * The forgetting is the point and it is why this is its own verb: there is one
 * kept campaign per game and campaign, so the one being replaced has nowhere
 * else to be. The surface says so beside the button rather than after the press.
 */
async function startOver() {
  const campaignId = currentCampaignId();
  resumeRefusal.value = "";
  keepError.value = "";
  if (kept && campaignId) {
    await forgetKeptCampaign(props.project, campaignId, kept);
  }
  start();
  await probeKeptCampaign();
}

/**
 * One management gesture. It is a committed campaign fact by the time this
 * returns: one outcome batch, applied and written to the slot. The screen
 * redrawn afterwards is the campaign rather than an intention.
 */
function manage(verb: CampaignManagementVerb, member: bigint, itemId?: bigint) {
  const active = session.value;
  if (!active) return;
  manageCampaignCompany(active, verb, member, itemId);
  triggerRef(session);
  // The gesture is already saved to the engine's slot. Carry those bytes out,
  // so the arrangement the author just made is the one a reload comes back to.
  void keepTheCampaign();
}

/** Take the board with the company as it stands. */
function takeTheBoard() {
  const active = session.value;
  if (!active) return;
  proceedFromCampaignCompany(active);
  selectedUnitId.value = "";
  refresh();
  maybePlayOpposingSide();
}

/**
 * The one story verb: next dialogue, the aftermath of the battle that just
 * ended, the next board, or the ending.
 */
function proceed() {
  const active = session.value;
  if (!active) return;
  const wasCommitted = active.committed;
  continueCampaignPlaySession(active);
  selectedUnitId.value = "";
  aimingAbilityId.value = "";
  chosenWeaponId.value = "";
  showingSheet.value = false;
  refresh();
  maybePlayOpposingSide();
  // A battle that just committed wrote the campaign to its slot. That is the
  // moment worth carrying across a page, and the only one this verb creates.
  if (!wasCommitted && active.committed) void keepTheCampaign();
}

function chooseCell(x: number, y: number) {
  const current = state.value;
  if (!current || current.outcome !== "ongoing") return;
  if (thinking.value || current.activeSide !== playerSide) return;
  const unit = current.units.find(
    (candidate) => candidate.onBoard && candidate.x === x && candidate.y === y
  );

  // Arranging takes the tap before anything else, because nothing else is
  // available yet: the engine refuses every ordinary command until the battle
  // begins, so offering one would be offering a refusal.
  if (current.deploying) {
    if (unit && canDeploy(current, unit.id) && unit.id !== selectedUnitId.value) {
      selectedUnitId.value = unit.id;
      return;
    }
    if (!selectedUnitId.value) return;
    if (!legalMoveKeys.value.has(`${x}:${y}`)) return;
    deployUnit(current, selectedUnitId.value, x, y);
    refresh();
    return;
  }

  // Aiming takes the tap first: while an ability is picked, a marked square
  // is where it lands, whoever happens to be standing there.
  if (aimingAbilityId.value) {
    if (!castTileKeys.value.has(`${x}:${y}`)) return;
    const actor = selectedUnitId.value;
    const ability = aimingAbilityId.value;
    aimingAbilityId.value = "";
    const before = captureBoard(current, current.activeSide);
    castAbility(props.project, current, actor, ability, x, y);
    refresh();
    // A spell that misses is a spell that was cast, so it is thrown here
    // exactly as a swing that misses is thrown. Inferring the miss from a
    // health total instead would draw nothing at all, because a cast that took
    // nothing off anybody leaves every total where it was.
    playMissedBlow(before, current, actor, x, y);
    playMotion(before, current, actor);
    selectedUnitId.value = canAct(current, actor) ? actor : "";
    void playOpposingSide();
    return;
  }

  if (unit && legalTargetIds.value.has(unit.id)) {
    const actor = selectedUnitId.value;
    const before = captureBoard(current, current.activeSide);
    attackUnit(
      props.project,
      current,
      actor,
      unit.id,
      activeWeaponId.value || undefined
    );
    refresh();
    playMissedBlow(before, current, actor, x, y);
    playMotion(before, current, actor);
    // Keep the unit selected while it still has points to spend, so a
    // two-point character does not have to be picked up again mid-turn.
    selectedUnitId.value = canAct(current, actor) ? actor : "";
    void playOpposingSide();
    return;
  }
  if (legalMoveKeys.value.has(`${x}:${y}`)) {
    const actor = selectedUnitId.value;
    const before = captureBoard(current, current.activeSide);
    moveUnit(props.project, current, actor, x, y);
    refresh();
    playMotion(before, current, actor);
    selectedUnitId.value = canAct(current, actor) ? actor : "";
    void playOpposingSide();
    return;
  }
  if (unit?.side === current.activeSide) {
    selectedUnitId.value = unit.id;
    aimingAbilityId.value = "";
    chosenWeaponId.value = "";
  }
}

function begin() {
  const current = state.value;
  if (!current || !current.deploying) return;
  if (!beginBattle(current)) return;
  selectedUnitId.value = "";
  refresh();
  void playOpposingSide();
}

function wait() {
  const current = state.value;
  if (!current || !selectedUnitId.value) return;
  if (waitUnit(props.project, current, selectedUnitId.value)) {
    refresh();
    selectedUnitId.value = "";
    aimingAbilityId.value = "";
    chosenWeaponId.value = "";
    showingSheet.value = false;
    void playOpposingSide();
  }
}

/**
 * Which of this side's characters still owes the board an activation, or "".
 *
 * The same question `client::unfinished_unit` answers for the two consoles, in
 * the terms this surface has. Under `sideBlocks` a side holds every
 * character's turn at once, so the
 * answer walks the roster; under the other two the side holds one activation
 * and `activeUnitId` names whose it is, so only that character is ever offered.
 */
function unfinishedUnit(current: PlaytestState): string {
  if (current.outcome !== "ongoing" || current.deploying) return "";
  if (current.activeSide !== playerSide) return "";
  if (current.activeUnitId !== "") {
    return canAct(current, current.activeUnitId) ? current.activeUnitId : "";
  }
  const owed = current.units.find(
    (unit) => unit.side === playerSide && canAct(current, unit.id)
  );
  return owed ? owed.id : "";
}

const canEndSideTurn = computed(() => {
  const current = state.value;
  return !!current && !finished.value && unfinishedUnit(current) !== "";
});

/**
 * End the whole side's turn, spending what it has not spent.
 *
 * Every character still owing the board an activation is finished, one `wait`
 * at a time, until the engine says the side owes none, which is what "end the
 * turn" has to mean on a board where each character holds their own. It is not
 * the same order as the button below it: that one finishes the character under
 * the cursor, and the two are kept apart in wording and in place because a
 * player who mixed them up would lose a whole line's worth of turns.
 *
 * Bounded by the roster's length: a `wait` the engine refuses would otherwise
 * leave the same character owing the same activation for ever, and a refusal
 * changes nothing, so the same answer twice is the end of it.
 */
function endSideTurn() {
  const current = state.value;
  if (!current || thinking.value) return;
  selectedUnitId.value = "";
  aimingAbilityId.value = "";
  chosenWeaponId.value = "";
  showingSheet.value = false;
  let previous = "";
  for (let guard = 0; guard <= current.units.length; guard += 1) {
    const live = state.value;
    if (!live) return;
    const owed = unfinishedUnit(live);
    if (owed === "" || owed === previous) break;
    previous = owed;
    if (!waitUnit(props.project, live, owed)) break;
    refresh();
  }
  void playOpposingSide();
}

function exit() {
  emit("exit");
}

// Play covers the whole editor, so it must behave like the modal dialog it
// claims to be: it takes focus on entry, keeps Tab inside itself, and hands
// focus back on exit. The editor behind it is made inert by the shell.
const surface = ref<HTMLDivElement>();
let previouslyFocused: HTMLElement | undefined;

function focusableElements(): HTMLElement[] {
  const root = surface.value;
  if (!root) return [];
  return [...root.querySelectorAll<HTMLElement>(
    "button, [href], input, select, textarea, summary, [tabindex]"
  )].filter((element) =>
    !element.hasAttribute("disabled") && element.tabIndex >= 0
  );
}

function trapFocus(event: KeyboardEvent) {
  const elements = focusableElements();
  const root = surface.value;
  if (!root || elements.length === 0) return;
  const first = elements[0]!;
  const last = elements.at(-1)!;
  const active = document.activeElement;
  const inside = active instanceof HTMLElement && root.contains(active);
  if (event.shiftKey) {
    if (!inside || active === first) {
      event.preventDefault();
      last.focus();
    }
  } else if (!inside || active === last) {
    event.preventDefault();
    first.focus();
  }
}

function onKeydown(event: KeyboardEvent) {
  // One step back per press, the way both consoles back out: the sheet is put
  // down before Play is left, so a child reading a character's numbers does not
  // lose the whole game by pressing the key that means "back".
  if (event.key === "Escape") {
    if (showingSheet.value) closeSheet();
    else exit();
  } else if (event.key === "Tab") trapFocus(event);
}

// Play can be opened before the engine has finished loading; start as soon as
// it is available rather than making the child press anything again.
watch(
  () => props.ready,
  (ready) => {
    if (ready && !session.value) start();
  }
);

// Whether there is a campaign to pick up is a question about the game and the
// campaign on screen, so it is asked again whenever either changes.
watch(
  () => [props.project.packageId, chosenCampaignId.value],
  () => {
    resumeRefusal.value = "";
    void probeKeptCampaign();
  }
);

onMounted(() => {
  window.addEventListener("keydown", onKeydown);
  const active = document.activeElement;
  previouslyFocused = active instanceof HTMLElement ? active : undefined;
  surface.value?.focus();
  void probeKeptCampaign();
  if (props.ready) start();
});

onBeforeUnmount(() => {
  window.removeEventListener("keydown", onKeydown);
  // The board's frame loop dies with the board. Nothing here outlives the
  // screen it draws.
  stopFrames();
  endCampaignPlaySession(session.value);
  session.value = undefined;
  state.value = undefined;
  previouslyFocused?.focus();
});
</script>

<template>
  <div ref="surface" class="play-mode" role="dialog" aria-modal="true"
    aria-labelledby="play-headline" tabindex="-1">
    <header class="play-bar">
      <button type="button" class="play-exit" @click="exit">
        <span aria-hidden="true">←</span> Back to editing
      </button>
      <!--
        The board's own gesture, in the board's own bar. It sits beside the way
        out rather than among the character buttons below for the same reason
        the consoles put it behind START and not in the unit action menu: "I am
        done with this one" and "I am done with all of them" are different
        orders, and a player who has to tell them apart should not have to do it
        by reading two similar labels side by side. The one below says "Done
        with them"; this one names the side.
      -->
      <button
        v-if="canEndSideTurn"
        type="button"
        class="play-end-turn"
        :disabled="thinking"
        @click="endSideTurn"
      >
        End our whole turn
      </button>
      <h1 id="play-headline" role="status" aria-live="polite">{{ headline }}</h1>
      <!--
        Only for a game that has more than one campaign. A game with one is
        offered nothing to choose between, which keeps pressing Play a single
        press for almost everybody.
      -->
      <label v-if="playableCampaigns.length > 1" class="play-campaign">
        <span>Campaign</span>
        <select
          :value="chosenCampaignId || playableCampaigns[0]?.id"
          :disabled="!ready"
          @change="
            chosenCampaignId = ($event.target as HTMLSelectElement).value;
            start();
          "
        >
          <option
            v-for="campaign in playableCampaigns"
            :key="campaign.id"
            :value="campaign.id"
          >
            {{ campaign.name }}
          </option>
        </select>
      </label>
      <!--
        The campaign is written to a slot after every battle and every gesture
        between them, and this browser keeps those bytes. The offer only exists
        when there is something to pick up: an author who has never played this
        game is not shown a button that would do nothing.
      -->
      <button
        v-if="hasKeptCampaign"
        type="button"
        class="play-resume"
        :disabled="!ready"
        @click="resume"
      >
        Pick up where I left off
      </button>
      <button
        type="button"
        class="play-restart"
        :disabled="!ready"
        :title="hasKeptCampaign
          ? 'Replaces the campaign this browser is keeping.'
          : undefined"
        @click="startOver"
      >
        Start over
      </button>
    </header>

    <p v-if="resumeRefusal" class="play-refused" role="alert">
      {{ resumeRefusal }}
      <button type="button" class="play-fresh" @click="startOver">
        Start fresh
      </button>
    </p>
    <p v-if="keepError" class="play-keep-error" role="alert">{{ keepError }}</p>

    <p v-if="error" class="play-error" role="alert">{{ error }}</p>
    <div v-if="error && !hasAuthoredBattle" class="play-empty">
      <p>This game has no Stage to play yet.</p>
      <button type="button" @click="exit">Go to Stages</button>
    </div>

    <section
      v-if="scene && currentDialogue"
      class="play-scene"
      :class="{ 'play-scene-dressed': sceneBackdrop !== undefined }"
      :style="sceneBackdropStyle"
      aria-label="Story scene"
    >
      <!-- Named as well as drawn: a backdrop is a setting, and a player using
           a screen reader is being told where the scene is, not decorated. -->
      <p v-if="sceneBackdrop" class="play-scene-setting">
        {{ sceneBackdrop.label }}
      </p>
      <ul class="play-scene-lines">
        <li v-for="(line, index) in currentDialogue.lines" :key="index">
          <img v-if="linePortraits[index]" class="play-scene-portrait"
            :src="linePortraits[index]"
            :alt="`${line.speaker}, as this scene casts them`">
          <strong v-if="line.speaker">{{ line.speaker }}</strong>
          <span>{{ line.text }}</span>
        </li>
      </ul>
      <button type="button" class="play-continue" @click="proceed">
        Continue
      </button>
    </section>

    <section
      v-else-if="aftermath"
      class="play-aftermath"
      aria-label="After the Stage"
    >
      <!--
        Between one board and the next: what the Stage cost, what it taught,
        and where the campaign went. This is the screen an author presses Play
        to see, since a permadeath rule nobody sees is a rule nobody can
        playtest, and every number on it was handed over by the engine's
        campaign session.
      -->
      <p class="play-aftermath-outcome">
        {{ aftermath.outcome === "first_side_won" ? "Your side wins!" : "The enemy wins!" }}
      </p>

      <!--
        The same people the board named while it was being fought, and the same
        word for it, so that a reader recognises the second sentence as being
        about the first. What this screen adds is the part only the campaign
        knows: whether it was permanent.

        Which word that is comes off the aftermath the engine returned rather
        than off the project, so the screen cannot disagree with the rule the
        battle was actually committed under.
      -->
      <template v-if="aftermath.fallen.length">
        <h2>
          {{ aftermath.characterLoss === "recoverable"
            ? "Carried off the field"
            : "Lost for good" }}
        </h2>
        <ul class="play-aftermath-fallen">
          <li v-for="name in aftermath.fallen" :key="name">
            {{ aftermath.characterLoss === "recoverable"
              ? `${name} fell, and rejoins the company with everything they were carrying.`
              : `${name} died, and will not come back.` }}
          </li>
        </ul>
      </template>

      <template v-if="aftermath.experience.length">
        <h2>Experience</h2>
        <ul class="play-aftermath-experience">
          <li v-for="earned in aftermath.experience" :key="earned.name">
            {{ earned.name }} earned {{ earned.amount }}.
          </li>
        </ul>
      </template>

      <template v-if="aftermath.levelUps.length">
        <h2>Levels</h2>
        <ul class="play-aftermath-levels">
          <li v-for="levelUp in aftermath.levelUps" :key="levelUp.name">
            {{ levelUp.name }} reached level {{ levelUp.toLevel }}
            (from {{ levelUp.fromLevel }}).
            <ul>
              <li v-for="gain in levelUp.points" :key="gain.stat">
                +{{ gain.points }} {{ gain.stat }}
              </li>
              <li v-if="levelUp.points.length === 0">
                The rolls granted nothing this time.
              </li>
            </ul>
          </li>
        </ul>
      </template>

      <template v-if="aftermath.joined.length">
        <h2>Joined the company</h2>
        <ul class="play-aftermath-joined">
          <li v-for="name in aftermath.joined" :key="name">
            {{ name }} joined the company.
          </li>
        </ul>
      </template>

      <template v-if="aftermath.store.length || aftermath.supplies.length">
        <h2>The company's supplies</h2>
        <ul class="play-aftermath-store">
          <li
            v-for="(movement, index) in aftermath.store"
            :key="`${movement.kind}-${movement.itemName}-${index}`"
          >
            <template v-if="movement.kind === 'add_item'">
              Picked up {{ movement.amount }} × {{ movement.itemName }}.
            </template>
            <template v-else-if="movement.ownerName">
              {{ movement.ownerName }} used {{ movement.amount }} ×
              {{ movement.itemName }}.
            </template>
            <template v-else>
              Used up {{ movement.amount }} × {{ movement.itemName }}.
            </template>
          </li>
        </ul>
        <p class="play-aftermath-supplies">
          <template v-if="aftermath.supplies.length">
            In the stores:
            <template
              v-for="(stack, index) in aftermath.supplies"
              :key="stack.itemName"
            >
              <template v-if="index">, </template>{{ stack.quantity }} ×
              {{ stack.itemName }}</template
            >.
          </template>
          <template v-else>The stores are empty.</template>
        </p>
      </template>

      <template v-if="carriers.length">
        <h2>What the company carries</h2>
        <ul class="play-aftermath-carrying">
          <li v-for="member in carriers" :key="String(member.id)">
            {{ member.name }}:
            <template v-if="member.carrying.length">
              <template
                v-for="(stack, index) in member.carrying"
                :key="stack.itemName"
              >
                <template v-if="index">, </template>{{ stack.quantity }} ×
                {{ stack.itemName }}</template
              >
            </template>
            <template v-else>nothing</template>
          </li>
        </ul>
      </template>

      <p class="play-aftermath-next">
        <template v-if="aftermath.nextNodeName">
          Next: {{ aftermath.nextNodeName }}.
        </template>
        <template v-else>{{ aftermath.blockedReason }}</template>
      </p>
      <p v-if="!aftermath.saved" class="play-aftermath-save" role="alert">
        The campaign could not be kept: {{ aftermath.saveError }}.
      </p>

      <button type="button" class="play-continue" @click="proceed">
        Continue
      </button>
    </section>

    <!--
      The company, between battles.

      This is where the campaign's last verbs are: what the company owns, what
      each member is carrying, and who is going. Nothing below is computed: a
      quantity is the campaign's, "the next board has no place for them" is the
      board's, and a refusal is the engine's own word for it, shown rather than
      paraphrased.

      Every button commits before the screen redraws, so there is no Apply and
      nothing to lose: the campaign is written to its slot as each gesture is
      made.
    -->
    <section
      v-else-if="company"
      class="play-manage"
      aria-label="The company"
    >
      <p v-if="company.refusal" class="play-manage-refusal" role="alert">
        That could not be done: {{ company.refusal }}.
      </p>
      <p v-if="company.saveError" class="play-manage-refusal" role="alert">
        The campaign could not be kept: {{ company.saveError }}.
      </p>

      <h3>The stores</h3>
      <ul v-if="company.store.length" class="play-manage-store">
        <li v-for="stack in company.store" :key="String(stack.itemId)">
          {{ stack.quantity }} × {{ stack.itemName }}
        </li>
      </ul>
      <p v-else class="play-manage-empty">The stores are empty.</p>

      <!--
        How many are going, when this Stage says how many may. Both numbers
        are the engine's: `fielded` is who would actually take the field as the
        company stands, and the cap is what the Stage's author allows. A
        Stage that caps nothing says nothing, because there is no count to
        keep. Fewer than the cap is a perfectly good party, a maximum rather
        than a quota, so this is said plainly rather than as a warning.
      -->
      <p v-if="company.capacity" class="play-manage-capacity">
        Going: {{ company.fielded }} of {{ company.capacity }}.
      </p>

      <ul class="play-manage-members">
        <li v-for="member in company.members" :key="String(member.id)">
          <p class="play-manage-name">
            {{ member.name }}
            <span v-if="!member.placeable" class="play-manage-note">
              (no room on this board)
            </span>
            <span v-else-if="!member.fielded" class="play-manage-note">
              (staying behind)
            </span>
          </p>
          <p class="play-manage-carrying">
            <template v-if="member.carrying.length">
              Carrying:
              <template
                v-for="(stack, index) in member.carrying"
                :key="String(stack.itemId)"
              >
                <template v-if="index > 0">, </template>
                {{ stack.quantity }} × {{ stack.itemName }}
              </template>
            </template>
            <template v-else>Carrying nothing.</template>
          </p>
          <p v-if="member.present" class="play-manage-actions">
            <button
              v-for="stack in company.store"
              :key="'give-' + String(stack.itemId)"
              type="button"
              class="play-manage-give"
              @click="manage('give', member.id, stack.itemId)"
            >
              Give {{ stack.itemName }}
            </button>
            <button
              v-for="stack in member.carrying"
              :key="'take-' + String(stack.itemId)"
              type="button"
              class="play-manage-take"
              @click="manage('take', member.id, stack.itemId)"
            >
              Put back {{ stack.itemName }}
            </button>
            <button
              v-if="member.placeable && member.fielded"
              type="button"
              class="play-manage-bench"
              @click="manage('bench', member.id)"
            >
              Leave behind
            </button>
            <button
              v-else-if="member.placeable"
              type="button"
              class="play-manage-field"
              @click="manage('field', member.id)"
            >
              Bring along
            </button>
          </p>
        </li>
      </ul>

      <button type="button" class="play-continue" @click="takeTheBoard">
        To the Stage
      </button>
    </section>

    <section
      v-else-if="session?.phase === 'ended'"
      class="play-ending"
      aria-label="Ending"
    >
      <p>The story ends here.</p>
      <button type="button" class="play-continue" @click="start">
        Play again
      </button>
    </section>

    <section
      v-else-if="session?.phase === 'stalled'"
      class="play-ending"
      aria-label="Story stopped"
    >
      <p>{{ session.stallReason }}</p>
      <button type="button" class="play-continue" @click="start">
        Start over
      </button>
    </section>

    <div v-else-if="state" class="play-stage">
      <!--
        Who is not here, and said before the first tap rather than discovered
        by looking for somebody who is gone. The roster decided this, not the
        map: the map still lists them.
      -->
      <p
        v-if="session && session.excluded.length"
        class="play-excluded"
        role="status"
      >
        Not on this board:
        {{ session.excluded.map((member) => member.name).join(", ") }}.
      </p>

      <!--
        Who joined between battles. A recruitment authored onto a story node
        has no aftermath screen to be reported on, so it is said here, before
        the board they first stand on.
      -->
      <p
        v-if="session && session.joined.length"
        class="play-joined"
        role="status"
      >
        {{ session.joined.join(", ") }} joined the company.
      </p>

      <!--
        And who this battle has taken, said as it happens rather than left for
        the screen afterwards. An alert rather than a status, because it is the
        one thing on this surface a player must not be allowed to miss: under
        the permanent rule there is no press that undoes it.

        The word follows the rule the project states. Telling somebody playing a
        recoverable campaign that a character died would be a claim the aftermath
        screen contradicts a minute later, and a player who learns once that the
        word cannot be trusted stops reading it.
      -->
      <ul v-if="losses.length" class="play-losses" role="alert">
        <li v-for="name in losses" :key="name">
          {{ name }} {{ lossesAreForever ? "died." : "fell." }}
        </li>
      </ul>

      <TacticalBoard
        :width="state.width"
        :height="state.height"
        :terrain="state.terrain"
        :theme-id="state.themeId"
        :character-style-id="state.characterStyleId"
        :character-figure-id="state.characterFigureId"
        :units="state.units"
        :selected-unit-id="selectedUnitId"
        :legal-move-keys="legalMoveKeys"
        :legal-target-ids="legalTargetIds"
        :cast-tile-keys="castTileKeys"
        :danger-tile-keys="dangerTileKeys"
        :motion="boardMotion"
        :sequence-cells="boardCells"
        :water-phase-frame="pulseFrame"
        :cursor-emphasis="cursorEmphasis"
        :miss-key="missKey"
        :mark="boardMark"
        @choose-cell="chooseCell"
      />

      <div class="play-actions">
        <p class="play-hint">
          <template v-if="state.deploying">
            Tap one of your characters, then a glowing square. Press
            <strong>Begin the fighting</strong> when your line is set.
            <template v-if="dangerTileKeys.size">
              Red squares are places you can go where the enemy can reach you.
            </template>
          </template>
          <template v-else-if="finished">
            See what happens next.
          </template>
          <template v-else-if="thinking || state.activeSide !== playerSide">
            Watch what they do.
          </template>
          <template v-else-if="!selectedUnitId">
            Tap one of your characters.
          </template>
          <template v-else-if="aimingAbility">
            Tap a marked square to use {{ aimingAbility.name }}.
          </template>
          <template v-else>
            <template v-if="activeWeapon">
              Tap a glowing square to move, or a marked enemy to attack with
              {{ activeWeapon.name }}.
            </template>
            <template v-else>
              Tap a glowing square to move, or a marked enemy to attack.
            </template>
            <template v-if="chanceToHit !== null">
              The shot lands {{ chanceToHit }} times in 100.
            </template>
            <template v-if="weaponLean === 'advantage'">
              Your weapon beats theirs, so the blow is bigger and surer than the
              weapon on its own would make it.
            </template>
            <template v-else-if="weaponLean === 'disadvantage'">
              Theirs beats yours, so the blow is smaller and less sure than the
              weapon on its own would make it.
            </template>
            <template v-if="dangerTileKeys.size">
              Red squares are places you can go where the enemy can reach you.
            </template>
          </template>
          <template v-if="!finished && state.remainingActionPoints > 0">
            <span class="play-points" aria-hidden="true">
              {{ "●".repeat(state.remainingActionPoints) }}
            </span>
          </template>
        </p>
        <button
          v-if="finished"
          type="button"
          class="play-continue"
          @click="proceed"
        >
          Continue
        </button>
        <!--
          The one row the deployment phase offers, and the only way out of it:
          the engine never decides an arrangement is finished, because every
          character is already standing somewhere.
        -->
        <button
          v-else-if="state.deploying"
          type="button"
          class="play-begin"
          @click="begin"
        >
          Begin the fighting
        </button>
        <template v-else>
          <button
            v-for="weapon in weaponChoices"
            :key="weapon.id"
            type="button"
            class="play-weapon"
            :class="{ chosen: weapon.id === activeWeaponId }"
            :aria-pressed="weapon.id === activeWeaponId"
            :disabled="thinking"
            @click="choose(weapon.id)"
          >
            {{ weapon.name }}
          </button>
          <button
            v-for="ability in abilityChoices"
            :key="ability.id"
            type="button"
            class="play-ability"
            :class="{ aiming: ability.id === aimingAbilityId }"
            :aria-pressed="ability.id === aimingAbilityId"
            :disabled="thinking"
            @click="aim(ability.id)"
          >
            {{ ability.name }}
          </button>
          <!--
            The item row, between the spells and the row that ends the turn:
            the position the consoles reserved for it. It commits on the press
            because an item reaches the hand that holds it and there is nothing
            to aim at, and a spent one stays as a disabled row so a player can
            see what ran out.
          -->
          <button
            v-for="item in itemChoices"
            :key="item.id"
            type="button"
            class="play-item"
            :disabled="thinking || item.count === 0 || item.kind === 'none'"
            @click="spend(item.id)"
          >
            {{ itemLabel(item) }}
          </button>
          <!--
            The talk row, after the pack and before the row that ends the turn:
            the position both consoles give it, because a talk costs an action
            point exactly as a strike does. One button per neighbour rather than
            one row to aim afterwards, since a browser can name who is being
            spoken to. The engine says who belongs here.
          -->
          <button
            v-for="target in talkChoices"
            :key="target.id"
            type="button"
            class="play-talk"
            :disabled="thinking"
            @click="speakTo(target.id)"
          >
            Talk to {{ target.name }}
          </button>
          <button
            type="button"
            class="play-wait"
            :disabled="!selectedCanAct || thinking"
            @click="wait"
          >
            Done with them
          </button>
          <!--
            After the row that ends the character's turn and before the way out,
            exactly where the consoles put it: the last rows of the action menu
            are the ones that commit nothing.
          -->
          <button
            type="button"
            class="play-info"
            :disabled="!selectedUnitId"
            :aria-expanded="showingSheet"
            @click="openSheet"
          >
            About them
          </button>
        </template>
      </div>

      <!--
        The full sheet: everything this character is, on one surface, reached
        deliberately. The same fields the two consoles and the terminal draw,
        in the same order, as platform/sheet/README.md sets out, because a
        player who learns to read one client's sheet should be able to read
        another's.
      -->
      <div
        v-if="showingSheet && sheet"
        class="play-sheet"
        role="dialog"
        aria-modal="false"
        aria-labelledby="play-sheet-name"
      >
        <h2 id="play-sheet-name">
          {{ sheet.name }}
          <small>{{ sheet.className }}</small>
        </h2>
        <!--
          The console sheet's LEVEL/EXP row, immediately after the name and
          before the health, and absent altogether for a character no campaign
          is holding: an opponent, a bystander, a battle with no roster behind
          it. The two numbers are the roster's own.
        -->
        <dl
          v-if="sheetCampaign"
          class="play-sheet-stats"
          data-group="campaign"
        >
          <div><dt>Level</dt><dd>{{ sheetCampaign.level }}</dd></div>
          <div><dt>Experience</dt><dd>{{ sheetCampaign.experience }}</dd></div>
        </dl>
        <!--
          The three groups the console sheet draws as its three stat lines, in
          the same order, with the words spelled out because this surface has no
          320-pixel budget to spend. `HP 12/12  AP 2  MOV 3  SPD 1` is this
          list's first group; a player who learns one reads the other.
        -->
        <dl class="play-sheet-stats" data-group="spend">
          <div><dt>Health</dt><dd>{{ sheet.health }} / {{ sheet.maximumHealth }}</dd></div>
          <div><dt>Actions</dt><dd>{{ sheet.actionPoints }}</dd></div>
          <div><dt>Movement</dt><dd>{{ sheet.movement }}</dd></div>
          <div><dt>Speed</dt><dd>{{ sheet.speed }}</dd></div>
        </dl>
        <dl class="play-sheet-stats" data-group="deal">
          <div><dt>Strength</dt><dd>{{ sheet.strength }}</dd></div>
          <div><dt>Defense</dt><dd>{{ sheet.defense }}</dd></div>
          <div><dt>Resistance</dt><dd>{{ sheet.resistance }}</dd></div>
          <div><dt>Magic</dt><dd>{{ sheet.magic }}</dd></div>
        </dl>
        <dl class="play-sheet-stats" data-group="land">
          <div><dt>Skill</dt><dd>{{ sheet.skill }}</dd></div>
          <div><dt>Luck</dt><dd>{{ sheet.luck }}</dd></div>
          <div><dt>Evasion</dt><dd>{{ sheet.evasion }}</dd></div>
        </dl>
        <h3>Weapons</h3>
        <ul class="play-sheet-list">
          <!--
            "Before skill, luck and evasion" is load-bearing. The hint under
            the board says "The shot lands N times in 100" and means the folded
            chance the engine will roll against; this is the weapon's own
            authored number, which is where that fold starts. Two different
            numbers must not wear one sentence.
          -->
          <li v-for="weapon in sheet.weapons" :key="weapon.name">
            {{ weapon.name }}: reach {{ band(weapon.minimumReach, weapon.maximumReach) }},
            lands {{ weapon.accuracy }} in 100 before skill, luck and evasion
          </li>
          <li v-if="sheet.weapons.length === 0">Nothing carried</li>
        </ul>
        <h3>Abilities</h3>
        <ul class="play-sheet-list">
          <li v-for="ability in sheet.abilities" :key="ability.name">
            {{ ability.name }}: reach {{ band(ability.minimumReach, ability.maximumReach) }}
          </li>
          <li v-if="sheet.abilities.length === 0">None known</li>
        </ul>
        <button type="button" class="play-sheet-close" @click="closeSheet">
          Back
        </button>
      </div>

      <details class="play-keyboard">
        <summary>Keyboard controls</summary>
        <PlaytestControls
          :state="state"
          :selected-unit-id="selectedUnitId"
          :action="keyboardAction"
          :legal-moves="legalMoveCoordinates"
          :legal-target-ids="legalTargetIds"
          @select-unit="selectedUnitId = $event"
          @set-action="keyboardAction = $event"
          @choose-cell="chooseCell"
          @wait="wait"
          @begin="begin"
        />
      </details>
    </div>
  </div>
</template>

<style scoped>
.play-mode:focus {
  outline: none;
}
.play-mode {
  position: fixed;
  inset: 0;
  z-index: 100;
  display: flex;
  flex-direction: column;
  gap: 1rem;
  padding: 1rem;
  overflow: auto;
  background: #0d1a1b;
  color: #f2f6f1;
}
.play-bar {
  display: flex;
  flex-wrap: wrap;
  gap: 1rem;
  align-items: center;
  justify-content: space-between;
}
.play-bar h1 {
  flex: 1 1 12rem;
  margin: 0;
  font-size: clamp(1.3rem, 4vw, 2.2rem);
  text-align: center;
}
/* Large enough to hit with a child's fingertip rather than a mouse pointer. */
.play-mode button {
  min-height: 3.5rem;
  padding: 0.75rem 1.4rem;
  border-radius: 0.75rem;
  font-size: 1.15rem;
  font-weight: 700;
}
.play-exit {
  background: #f2f6f1;
  color: #16211f;
}
.play-restart {
  background: #f2c14e;
  color: #172033;
}
/*
  Deliberately not the colour of the character buttons below. This one ends
  every character's turn at once, and a board-wide order that looked like the
  per-character one is an order a player would give by accident.
*/
.play-end-turn {
  background: #2d3f3b;
  color: #f2f6f1;
}
.play-scene,
.play-ending {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
  align-items: center;
  max-width: 34rem;
  margin: 2rem auto 0;
  text-align: center;
}
.play-aftermath {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
  max-width: 36rem;
  margin: 2rem auto 0;
}
.play-aftermath h2 {
  margin: 0.75rem 0 0;
  font-size: 1rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  opacity: 0.75;
}
.play-aftermath ul {
  margin: 0;
  padding-left: 1.25rem;
}
.play-aftermath-outcome {
  margin: 0;
  font-size: 1.5rem;
  font-weight: 700;
  text-align: center;
}
.play-aftermath-fallen {
  font-weight: 600;
}
.play-aftermath-next {
  margin-top: 1rem;
  font-weight: 600;
}
.play-aftermath .play-continue {
  align-self: center;
}
/* The company between battles: the same column the aftermath uses, because it
   is the same moment and reads as one screen. */
.play-manage {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  max-width: 36rem;
  margin: 2rem auto 0;
}
.play-manage h2 {
  margin: 0;
  font-size: 1.5rem;
  text-align: center;
}
.play-manage h3 {
  margin: 0.75rem 0 0;
  font-size: 1rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  opacity: 0.75;
}
.play-manage ul {
  margin: 0;
  padding-left: 1.25rem;
}
.play-manage-members {
  list-style: none;
  padding-left: 0;
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}
.play-manage-name {
  margin: 0;
  font-weight: 600;
}
.play-manage-note {
  font-weight: 400;
  font-style: italic;
  opacity: 0.75;
}
.play-manage-carrying,
.play-manage-empty {
  margin: 0.15rem 0;
  opacity: 0.85;
}
.play-manage-capacity {
  margin: 0.35rem 0 0;
  font-weight: 600;
}
.play-manage-actions {
  margin: 0.25rem 0 0;
  display: flex;
  flex-wrap: wrap;
  gap: 0.35rem;
}
.play-manage-actions button {
  font: inherit;
  padding: 0.25rem 0.6rem;
  border-radius: 0.4rem;
  border: 1px solid currentColor;
  background: transparent;
  color: inherit;
  cursor: pointer;
}
.play-manage-refusal {
  margin: 0;
  font-weight: 600;
}
.play-manage .play-continue {
  align-self: center;
  margin-top: 1rem;
}
.play-excluded,
.play-joined {
  margin: 0 0 0.5rem;
  font-style: italic;
  opacity: 0.85;
}
/* Not italic and not faded, unlike the two lines above it: those say who was
   never coming, and this says who is not coming back. It sits at full weight
   and full contrast for the same reason it is an alert. */
.play-losses {
  margin: 0 0 0.5rem;
  padding-left: 1.25rem;
  font-weight: 600;
}
.play-campaign {
  display: inline-flex;
  align-items: center;
  gap: 0.4rem;
  font-size: 0.9rem;
}
/* A dressed scene fills its own frame rather than floating on the page, so the
   bands read as a place rather than as a panel. The words keep the colour they
   always had: every band in the library is measured against it. */
.play-scene-dressed {
  max-width: 44rem;
  padding: 2rem 1.5rem;
  border-radius: 0.75rem;
  min-height: 22rem;
  justify-content: center;
}
.play-scene-setting {
  margin: 0;
  font-size: 0.8rem;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  opacity: 0.8;
}
.play-scene-lines {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  margin: 0;
  padding: 0;
  list-style: none;
  text-align: left;
}
.play-scene-lines li {
  display: flex;
  flex-direction: column;
  gap: 0.15rem;
  font-size: 1.15rem;
}
.play-scene-lines strong {
  color: #f2c14e;
}
/* Pixel art, so it is scaled without smoothing and never asked to be larger
   than the drawing a console holds. */
.play-scene-portrait {
  width: 64px;
  height: 64px;
  image-rendering: pixelated;
}
.play-ending p {
  margin: 0;
  font-size: 1.15rem;
}
.play-continue {
  background: #2e9e5b;
  color: #ffffff;
}
.play-stage {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  align-items: center;
}
.play-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 1rem;
  align-items: center;
  justify-content: center;
}
.play-hint {
  margin: 0;
  font-size: 1.1rem;
}
.play-points {
  margin-left: 0.5rem;
  color: #f2c14e;
  letter-spacing: 0.15em;
}
.play-wait {
  background: #2f6f8f;
  color: #ffffff;
}
.play-begin {
  background: #f2c14e;
  color: #172033;
  font-weight: 700;
}
.play-ability {
  background: #7a4bd0;
  color: #ffffff;
}
.play-ability.aiming {
  background: #f2c14e;
  color: #172033;
  outline: 3px solid #ffffff;
}
.play-item {
  background: #3f8f5f;
  color: #ffffff;
}
/* Talking is not fighting, and the colour says so: the amber this surface
   already reserves for a thing being offered rather than a thing being done
   to somebody. */
.play-talk {
  background: #b8862b;
  color: #ffffff;
}
.play-info {
  background: #f2f6f1;
  color: #16211f;
}
.play-sheet {
  width: min(100%, 34rem);
  padding: 1rem 1.25rem;
  border: 3px solid #f2c14e;
  border-radius: 0.75rem;
  background: #16211f;
}
.play-sheet h2 {
  display: flex;
  flex-wrap: wrap;
  gap: 0.6rem;
  align-items: baseline;
  margin: 0 0 0.75rem;
  color: #f2c14e;
  font-size: 1.4rem;
}
.play-sheet h2 small {
  color: #f2f6f1;
  font-size: 1rem;
  font-weight: 400;
}
.play-sheet h3 {
  margin: 1rem 0 0.35rem;
  color: #f2c14e;
  font-size: 1rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
}
.play-sheet-stats {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(8rem, 1fr));
  gap: 0.35rem 1rem;
  margin: 0 0 0.6rem;
}
.play-sheet-stats:last-of-type {
  margin-bottom: 0;
}
.play-sheet-stats div {
  display: flex;
  gap: 0.4rem;
  justify-content: space-between;
}
.play-sheet-stats dt {
  color: #b9c6bd;
}
.play-sheet-stats dd {
  margin: 0;
  font-weight: 700;
}
.play-sheet-list {
  margin: 0;
  padding-left: 1.2rem;
}
.play-sheet-close {
  margin-top: 1rem;
  background: #f2c14e;
  color: #172033;
}
.play-weapon {
  background: #3f5f3a;
  color: #ffffff;
}
.play-weapon.chosen {
  background: #8fd06a;
  color: #172033;
  outline: 3px solid #ffffff;
}
.play-mode button:disabled {
  cursor: not-allowed;
  opacity: 0.55;
}
.play-error {
  margin: 0;
  padding: 0.75rem 1rem;
  border-radius: 0.5rem;
  background: #5b1f1c;
}
/* A statement about what the browser is holding, not a warning. */
.play-kept {
  margin: 0;
  padding: 0.5rem 1rem;
  font-size: 0.95rem;
  opacity: 0.8;
}
/* A refusal is louder, and carries the way forward inside it. */
.play-refused,
.play-keep-error {
  display: flex;
  flex-wrap: wrap;
  gap: 0.75rem;
  align-items: center;
  margin: 0;
  padding: 0.75rem 1rem;
  border-radius: 0.5rem;
  background: #5b3a1c;
}
.play-fresh {
  background: #f2c14e;
  color: #172033;
}
.play-empty {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  align-items: center;
  max-width: 34rem;
  margin: 0 auto;
  text-align: center;
}
.play-empty p {
  margin: 0;
  font-size: 1.15rem;
}
.play-empty button {
  background: #2e9e5b;
  color: #ffffff;
}
.play-keyboard {
  width: min(100%, 46rem);
  color: #16211f;
}
.play-keyboard summary {
  padding: 0.5rem;
  color: #f2f6f1;
  cursor: pointer;
}
</style>
