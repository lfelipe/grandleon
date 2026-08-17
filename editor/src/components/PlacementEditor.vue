<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// The board an author actually thinks in.
//
// It draws the map's real terrain, which makes it the most task-shaped surface
// in the editor. Three rules keep it that way, and all three are about who can
// use it and what they can see:
//
// 1. It draws characters, not identifiers. A board covered in `unit_3` is not
//    the board the author is about to play, so it draws `unitSprite` in the
//    placement's faction colour, the same lookup `TacticalBoard` and both
//    consoles use.
// 2. It is a `role="grid"` with one tab stop, a roving tabindex and arrow
//    keys, the same contract `MapEditor` next door keeps. A board only a mouse
//    can reach is a regression rather than a feature.
// 3. It can say "three of these". A palette beside the board stamps one
//    placement per press. A stamped placement names no member, so
//    `character-standing.ts` reports that character as an **extra** without
//    anybody having declared one, which is what makes a crowd of anonymous
//    opponents authorable at all.
//
// Two things follow from the third rule and are worth saying separately,
// because between them they are what "put a bandit here" means:
//
// * **The palette offers characters this game has not got.** Otherwise the
//   first act on an empty board is to leave it, go to Characters, and build a
//   weapon type, a weapon, a class and a character before coming back: four
//   records deep to answer a question nobody asked. So the palette carries the
//   catalogue's roles as well as the game's own people, and putting one down
//   makes it. The board never writes those records itself: it says what the
//   author did and the surface above turns it into one act, because a class
//   left standing with no character holding it is what a half-finished write
//   looks like.
// * **A person stands in one place.** Three bandits are three placements of
//   one Bandit; Warden Kesh is one woman and cannot be in two places at once.
//   Which of the two a character is comes from `characterIsOnePerson`, so the
//   company answers it and nobody is asked. A person already on this board
//   greys out in the palette rather than being silently stamped twice.
import { computed, ref, watch } from "vue";
import type {
  CampaignRosterMember,
  EncounterPlacement
} from "../generated/source-v1";
import { useKeystrokeDraft } from "./keystroke-draft";
import { terrainColor, terrainGlyph } from "../domain/terrain-presentation";
import { factionColor, unitSprite } from "../domain/board-art";
import {
  characterRecipes,
  shelfSetting,
  type CatalogueSetting,
  type CharacterRole
} from "../domain/character-recipe";
import { sideFaction } from "../domain/character-standing";
import {
  htmlPattern,
  isStableId,
  stableIdPattern
} from "../domain/source-form-model";

const props = defineProps<{
  placements: readonly EncounterPlacement[];
  map: {
    readonly id: string;
    readonly name: string;
    readonly width: number;
    readonly height: number;
    readonly terrain: readonly string[];
  } | undefined;
  unitTypes: readonly {
    readonly id: string;
    readonly name: string;
    readonly classId?: string;
    readonly factionId?: string;
    /**
     * Whether this character is one person rather than a kind: somebody a
     * campaign's company holds by name. Computed by `characterIsOnePerson`
     * from the project, never authored, and passed in because that answer
     * needs the whole project and this board is only given a Stage.
     */
    readonly onePerson?: boolean;
  }[];
  /**
   * The company the campaign around this board can field, when there is one.
   * Left out entirely, this board belongs to no campaign and asks nobody who
   * stands where; an empty list is a campaign whose company is still empty,
   * which is a thing the author has to be told about rather than hidden from.
   */
  members?: readonly CampaignRosterMember[] | undefined;
  /** The project's season, so the grid shows the colours the Stage will. */
  themeId?: string | undefined;
  /** The game's factions, so a character is drawn in the colour it fights in. */
  factions?: readonly { readonly id: string; readonly color?: string }[] |
    undefined;
  /** The style the game draws its characters in. */
  characterStyleId?: string | undefined;
}>();

const emit = defineEmits<{
  update: [placements: EncounterPlacement[]];
  /**
   * Somebody the board had to put in the company for a placement on the
   * player's own side to be legal. The surface around this one owns the
   * campaign record, so it does the writing; this only ever asks.
   */
  enroll: [member: CampaignRosterMember];
  /**
   * Somebody this game has not got, put on a tile.
   *
   * The board asks and does not write. Making a character is a weapon type, a
   * weapon, a class and the character, and the placement is a fifth record on
   * a different collection again, so the only place the five can land as one
   * undoable act is the surface that owns the session. What comes back is the
   * ordinary props changing, and the palette picks the new character up so the
   * next press puts down another of them.
   */
  addCharacter: [ask: {
    readonly role: CharacterRole;
    readonly setting: CatalogueSetting;
    /** What to call them. Empty takes the catalogue's word for the role. */
    readonly name: string;
    readonly side: "first" | "second";
    readonly x: number;
    readonly y: number;
  }];
  /** A keystroke that is not in the project yet, so the header can say so. */
  dirty: [];
}>();

/**
 * When reinforcements arrive, while the numbers are being typed.
 *
 * These three are the only controls on this board an author types a value into
 * that is neither already committed per keystroke nor a menu. Clearing the box
 * and typing "12" passes through nothing at all and then through "1", and each
 * of those would be a wave the Stage claims to schedule, and an undo entry,
 * and a deep copy of the game. Half a number is not a number.
 *
 * The coordinates below are deliberately not held: they commit as they are
 * typed, because the board beside them draws the character standing where the
 * numbers say, and a coordinate that only moved somebody when the field was
 * left would be a control that does not do what it is showing.
 */
const arrivalKeystrokes = useKeystrokeDraft(() => emit("dirty"));

defineExpose({ flush: arrivalKeystrokes.flush });

const assetBase = import.meta.env.BASE_URL;

const selectedIndex = ref(0);
// Pressing a tile puts down a character from the palette, moves the one already
// selected, or extends its patrol, so the same gesture serves all three and
// the board never needs a second grid.
//
// The mode follows what the author last said they were doing rather than
// standing still: picking somebody out of the palette means putting them down,
// and adding a placement from the list below means moving that one. An empty
// board opens on putting somebody down, because a board with nobody on it has
// nothing to move and filling it is the only thing there is to do. A palette
// that looked armed while a press moved something instead would be a trap.
const gridMode = ref<"stamp" | "place" | "patrol">(
  props.placements.length === 0 ? "stamp" : "place"
);
/** What the last board gesture did, for anyone who cannot see it happen. */
const notice = ref("");
// Index of the placement currently being dragged, or -1. Dragging is an
// addition to clicking, never a replacement: a pointer that cannot drag, or a
// keyboard, still reaches every action through the list below.
const draggingIndex = ref(-1);
const draftPlacements = ref<EncounterPlacement[]>(
  props.placements.map((placement) => ({ ...placement }))
);

watch(
  () => props.placements,
  (placements) => {
    draftPlacements.value = placements.map((placement) => ({ ...placement }));
  },
  { deep: true }
);

const canDrawGrid = computed(() =>
  props.map !== undefined && props.map.width * props.map.height <= 4096
);

function uniqueId(ids: readonly string[]): string {
  if (!ids.includes("unit")) return "unit";
  let suffix = 2;
  while (ids.includes(`unit_${suffix}`)) suffix += 1;
  return `unit_${suffix}`;
}

/**
 * A change to one placement. A field named as undefined is one being cleared,
 * the member nobody stands as any more, which is different from a field the
 * change never mentions.
 */
type PlacementPatch = {
  [Field in keyof EncounterPlacement]?: EncounterPlacement[Field] | undefined
};

function replace(index: number, patch: PlacementPatch) {
  const placements = draftPlacements.value.map((placement, candidate) => {
    if (candidate !== index) return { ...placement };
    // An absent optional field and a field holding nothing say different
    // things in the source format, so a cleared choice, the member nobody
    // stands as any more, is removed rather than written as empty.
    const merged: Record<string, unknown> = { ...placement, ...patch };
    for (const [key, value] of Object.entries(patch)) {
      if (value === undefined) delete merged[key];
    }
    return merged as unknown as EncounterPlacement;
  });
  draftPlacements.value = placements;
  emit("update", placements);
}

/**
 * Turns a placement into a reinforcement, or back into an opening one.
 *
 * A reinforcement cannot field a roster member, the campaign layer having no
 * answer yet for a member who is neither fielded nor withheld, so becoming one
 * clears the member the same way choosing "nobody" does.
 */
function setArrival(index: number, arrives: boolean) {
  if (!arrives) {
    replace(index, { arrival: undefined });
    return;
  }
  replace(index, { arrival: { round: 2 }, memberId: undefined });
}

/**
 * The two halves of a recurrence, which the format takes together or not at
 * all: a gap with no number of arrivals is a stream no Stage could outlast,
 * and a number with no gap is several characters landing on one round.
 */
function setRecurrence(index: number, every: number, times: number) {
  const current = draftPlacements.value[index]?.arrival;
  if (current === undefined) return;
  if (every <= 0 || times <= 1) {
    replace(index, { arrival: { round: current.round } });
    return;
  }
  replace(index, { arrival: { round: current.round, every, times } });
}

/** The schema's identifier rule, for the browser's inline hint. */
const idPattern = htmlPattern(stableIdPattern);

// Empty is not nameless: the placeholder beside this shows what a player
// would read, so the help says only the part the placeholder cannot: that
// this is the one way to name somebody on the enemy side.
const nameHelp =
  "The only way to name somebody on the enemy side, who belongs to no company.";

const talkHelp =
  "One of yours can walk up and talk instead of striking, which takes this " +
  "character off the board.";

/**
 * What a world flag is, and, the part the checkbox alone never said, where
 * one is read.
 *
 * Raising something nothing reads changes nothing, so a control that names a
 * flag and stops there has told an author only half of a mechanism. The other
 * half is a transition's `worldFlagEquals` condition, and it is named here by
 * the words the flow editor puts on screen rather than by the word the schema
 * spells, so the sentence is a route an author can walk.
 */
const talkFlagHelp =
  "The campaign remembers this name after the Stage ends. Nothing happens " +
  "until something reads it. To read it: Flow, the branch out of a node, " +
  "Conditional branch, Condition set to World flag, this same name as the " +
  "World flag identity, expected true.";

/**
 * Whether this character can be talked to, and what talking to them raises.
 *
 * The flag is authored rather than derived: a board-local character identity
 * is a different number every appearance, so a transition reading the result of
 * a conversation has to read a name the author chose.
 */
function setTalkable(index: number, talkable: boolean) {
  const placement = draftPlacements.value[index];
  if (!placement) return;
  if (!talkable) {
    replace(index, { talk: undefined });
    return;
  }
  replace(index, { talk: { flagId: placement.talk?.flagId ?? "talked_to" } });
}

function setTalkFlag(index: number, flagId: string) {
  replace(index, { talk: { flagId: flagId.trim() } });
}

/** Members of the company nobody on this board is standing as yet. */
function unfielded(placements: readonly EncounterPlacement[]) {
  const standing = new Set(
    placements.flatMap((placement) =>
      placement.memberId === undefined ? [] : [placement.memberId]
    )
  );
  return (props.members ?? []).filter((member) => !standing.has(member.id));
}

function addPlacement() {
  const placements = draftPlacements.value.map((placement) => ({ ...placement }));
  const side = placements.some((placement) => placement.side === "first")
    ? "second"
    : "first";
  // A new placement on your own side is somebody from the company, and the
  // first person not already on this board is the likeliest somebody. What
  // they are comes from them: a member is the same character wherever they
  // stand, so the placement never gets to disagree with the company.
  const member = side === "first" ? unfielded(placements)[0] : undefined;
  placements.push({
    id: uniqueId(placements.map((placement) => placement.id)),
    ...(member ? { memberId: member.id } : {}),
    unitTypeId: member?.unitTypeId ?? props.unitTypes[0]?.id ?? "",
    side,
    x: 0,
    y: 0
  });
  selectedIndex.value = placements.length - 1;
  draftPlacements.value = placements;
  gridMode.value = "place";
  notice.value =
    `Added a placement on ${sideWord(side)}. Press a tile to move it there.`;
  emit("update", placements);
}

/**
 * Fields a member, or nobody. Choosing somebody also settles what stands
 * there, because a member is one character on every board they appear on.
 */
function chooseMember(index: number, memberId: string) {
  const member = (props.members ?? []).find(
    (candidate) => candidate.id === memberId
  );
  replace(index, {
    memberId: memberId === "" ? undefined : memberId,
    ...(member ? { unitTypeId: member.unitTypeId } : {})
  });
}

/**
 * Changes which side a placement fights for. The opposing side is never the
 * company, so a member standing there is let go rather than saved into a
 * campaign that would refuse to open.
 */
function changeSide(index: number, side: "first" | "second") {
  replace(index, {
    side,
    ...(side === "second" ? { memberId: undefined } : {})
  });
}

/**
 * What this placement is called with no name on it, which is what the shown
 * placeholder promises: the character type, and an ordinal when the board holds
 * more than one of that type. It is the same rule `grandleon::sheet` derives on
 * every client, so an author looking at the empty field is reading what a
 * player will see.
 *
 * The ordinal counts in placement order rather than by identity, which is the
 * order an author sees on this board. A compiled package numbers by ascending
 * placement identity instead, so the digit an author reads here may not be the
 * digit the console draws. The placeholder promises "you will be numbered",
 * not "you will be number two". Whoever wants a particular word types one.
 */
function derivedName(index: number): string {
  const placement = draftPlacements.value[index];
  if (!placement) return "";
  const kind = unitTypeName(placement.unitTypeId) ?? placement.unitTypeId;
  const kindred = draftPlacements.value.filter(
    (candidate) => candidate.unitTypeId === placement.unitTypeId
  );
  if (kindred.length < 2) return kind;
  return `${kind} ${kindred.indexOf(placement) + 1}`;
}

/**
 * What the author typed, or nothing at all. An empty field is the absence of a
 * name rather than an empty one: a placement carrying `name: ""` would fail the
 * schema, and it means exactly what leaving it out means.
 */
function setName(index: number, value: string) {
  const trimmed = value.trim();
  replace(index, { name: trimmed === "" ? undefined : trimmed });
}

function memberName(memberId: string | undefined): string | undefined {
  return (props.members ?? []).find((member) => member.id === memberId)?.name;
}

function unitTypeName(id: string): string | undefined {
  return props.unitTypes.find((unitType) => unitType.id === id)?.name;
}

function removePlacement(index: number) {
  // Taking somebody off the board renumbers everybody after them, and a held
  // keystroke names the placement it belongs to by position, so it is committed
  // before the list moves under it.
  arrivalKeystrokes.flush();
  const placements = draftPlacements.value
    .filter((_, candidate) => candidate !== index)
    .map((placement) => ({ ...placement }));
  selectedIndex.value = Math.max(0, Math.min(selectedIndex.value, placements.length - 1));
  draftPlacements.value = placements;
  emit("update", placements);
}

function occupants(x: number, y: number) {
  return draftPlacements.value
    .map((placement, index) => ({ placement, index }))
    .filter(({ placement }) => placement.x === x && placement.y === y);
}

function placementProblems(placement: EncounterPlacement, index: number): string[] {
  const problems: string[] = [];
  if (!props.map) {
    problems.push("Choose a map to check this position.");
  } else if (
    placement.x < 0 || placement.y < 0 ||
    placement.x >= props.map.width || placement.y >= props.map.height
  ) {
    problems.push(`Outside ${props.map.width}×${props.map.height} map bounds.`);
  }
  if (draftPlacements.value.some((candidate, candidateIndex) =>
    candidateIndex !== index &&
    candidate.x === placement.x &&
    candidate.y === placement.y
  )) {
    problems.push("Another character already occupies this tile.");
  }
  // Who stands here at all, asked of every placement rather than only of the
  // player's own. The board draws a placement naming a character the project
  // does not have from the archetype lookup's default, so without this a board
  // full of enemies nobody defined looks exactly like a board that is right.
  if (!props.unitTypes.some((unitType) => unitType.id === placement.unitTypeId)) {
    problems.push(
      placement.unitTypeId === ""
        ? "Nobody is standing here. Choose which character does."
        : `'${placement.unitTypeId}' is not a character in this project. ` +
          "Choose one that is, or create it."
    );
  }
  // Two identifiers this surface authors as free text, judged by the rule the
  // format judges them by. Neither reaches the record form, so nothing else in
  // the editor ever looks at them, and either one written in prose is a project
  // that will not open again.
  if (!isStableId(placement.id)) {
    problems.push(
      `'${placement.id}' is not an identifier this format can hold. Use ` +
      "lowercase letters, digits and separators (. _ -), starting with a " +
      "letter, like 'gate_captain'."
    );
  }
  const flagId = placement.talk?.flagId;
  if (flagId !== undefined && !isStableId(flagId)) {
    problems.push(
      `The world flag '${flagId}' is not an identifier this format can hold. ` +
      "Use lowercase letters, digits and separators (. _ -), starting with a " +
      "letter, like 'talked_to_captain'."
    );
  }
  problems.push(...memberProblems(placement, index));
  return problems;
}

/**
 * What is wrong with who stands here, when this board belongs to a campaign.
 * The same rules the campaign editor refuses to save on, said beside the
 * placement they are about.
 */
function memberProblems(placement: EncounterPlacement, index: number): string[] {
  const members = props.members;
  if (members === undefined) return [];
  const problems: string[] = [];
  if (placement.side === "second") {
    if (placement.memberId !== undefined) {
      problems.push(
        `The opposing side never fields your company, and '${placement.memberId}' ` +
        "is one of yours. Choose nobody, or move this placement to your side."
      );
    }
    return problems;
  }
  if (placement.memberId === undefined) {
    problems.push(
      members.length === 0
        ? "Nobody stands here, and this campaign's company is empty. Add a " +
          "member to the company, or recruit one at an earlier node."
        : "Nobody stands here. Choose which member of the company does."
    );
    return problems;
  }
  const member = members.find((candidate) => candidate.id === placement.memberId);
  if (!member) {
    problems.push(
      `'${placement.memberId}' is nobody in this campaign. Choose a member of ` +
      "the company, or add them to it."
    );
    return problems;
  }
  if (draftPlacements.value.some((candidate, candidateIndex) =>
    candidateIndex !== index && candidate.memberId === placement.memberId
  )) {
    problems.push(
      `${member.name} already stands somewhere else on this board. One ` +
      "character cannot be in two places at once."
    );
  }
  if (member.unitTypeId !== placement.unitTypeId) {
    problems.push(
      `${member.name} is ${unitTypeName(member.unitTypeId) ?? member.unitTypeId}, ` +
      `not ${unitTypeName(placement.unitTypeId) ?? placement.unitTypeId}. ` +
      "Choose them again to field them as who they are."
    );
  }
  return problems;
}

function moveSelected(x: number, y: number) {
  const placement = draftPlacements.value[selectedIndex.value];
  if (!placement) {
    // A press that does nothing and says nothing is the worst answer there is.
    notice.value =
      "There is nobody on this board to move. Pick a character above and " +
      "switch to putting one down.";
    return;
  }
  if (gridMode.value === "patrol") {
    const patrol = [...(placement.patrolPoints ?? [])];
    const existing = patrol.findIndex(
      (point) => point.x === x && point.y === y
    );
    // Clicking a point again removes it, so a mistake costs one click.
    if (existing >= 0) patrol.splice(existing, 1);
    else patrol.push({ x, y });
    replace(selectedIndex.value, { patrolPoints: patrol });
    return;
  }
  replace(selectedIndex.value, { x, y });
}

function startDrag(index: number, event: DragEvent) {
  draggingIndex.value = index;
  selectedIndex.value = index;
  event.dataTransfer?.setData("text/plain", String(index));
  if (event.dataTransfer) event.dataTransfer.effectAllowed = "move";
}

function dropOn(x: number, y: number, event: DragEvent) {
  event.preventDefault();
  const index = draggingIndex.value;
  draggingIndex.value = -1;
  if (index < 0 || !draftPlacements.value[index]) return;
  replace(index, { x, y });
  selectedIndex.value = index;
}

/**
 * Dragging somebody off the board takes them off it.
 *
 * The same gesture that moves a character is the one that removes them, which
 * is what an author expects of a board they can already drag on: pick a
 * character up, and put them back somewhere or put them away. Before this, a
 * removal was a button at the bottom of that character's own fieldset, which
 * meant selecting them first and reading a form to undo a placement made with
 * one drag.
 *
 * The strip this drops onto is on the page whether or not anything is being
 * dragged, and says what the gesture is when nothing is. That is deliberate on
 * two counts: a target that appears mid-drag moves the page under the pointer,
 * which some browsers take as a reason to cancel the drag, and a gesture nobody
 * is told about is a gesture nobody uses.
 *
 * It says who it took, because a drag is easy to make by accident and a board
 * that quietly lost somebody is worse than one that says so.
 */
function dropOffBoard(event: DragEvent) {
  event.preventDefault();
  const index = draggingIndex.value;
  draggingIndex.value = -1;
  const placement = draftPlacements.value[index];
  if (index < 0 || !placement) return;
  const who = placement.name?.trim() || derivedName(index);
  removePlacement(index);
  notice.value = `${who} is off the board. Put them back from the palette.`;
}

function terrainAt(x: number, y: number): string {
  if (!props.map) return "grass";
  return props.map.terrain[y * props.map.width + x] ?? "grass";
}

function cellStyle(x: number, y: number) {
  return { background: terrainColor(terrainAt(x, y), props.themeId) };
}

/** Patrol legs belonging to the selected placement, for grid annotation. */
const selectedPatrol = computed(
  () => draftPlacements.value[selectedIndex.value]?.patrolPoints ?? []
);

function patrolLeg(x: number, y: number): number {
  return selectedPatrol.value.findIndex(
    (point) => point.x === x && point.y === y
  );
}

/**
 * The drawing for a character on a side: style from the project, archetype from
 * the character's class, colour from its faction and from the side only when no
 * faction claims it. Identical to what `TacticalBoard` resolves, which is the
 * point: the author should recognise the board they are about to play.
 */
function spriteFor(
  unitTypeId: string,
  side: "first" | "second"
): string | undefined {
  const unitType = props.unitTypes.find((entry) => entry.id === unitTypeId);
  // Nothing rather than something. A character the project does not have has
  // no class to resolve, and the archetype lookup answers an absent class with
  // a knight, which would put a confident, wrong figure over a placement that
  // names nobody. The tile says so instead.
  if (unitType === undefined) return undefined;
  return assetBase + unitSprite(
    unitType.classId,
    side,
    factionColor(props.factions ?? [], unitType.factionId),
    props.characterStyleId
  );
}

// ---------------------------------------------------------------------------
// The grid: one tab stop, arrow keys, the contract `MapEditor` already keeps.
// ---------------------------------------------------------------------------

const gridRoot = ref<HTMLElement>();
const focusIndex = ref(0);

watch(
  () => [props.map?.width, props.map?.height],
  ([width, height]) => {
    if (focusIndex.value >= (width ?? 0) * (height ?? 0)) focusIndex.value = 0;
  }
);

function moveFocus(x: number, y: number, dx: number, dy: number) {
  const map = props.map;
  if (!map) return;
  const nextX = x + dx;
  const nextY = y + dy;
  if (nextX < 0 || nextY < 0 || nextX >= map.width || nextY >= map.height) return;
  focusIndex.value = nextY * map.width + nextX;
  gridRoot.value
    ?.querySelector<HTMLButtonElement>(`[data-cell="${focusIndex.value}"]`)
    ?.focus();
}

/** Whose side a placement stands on, in the words the side menu uses. */
function sideWord(side: "first" | "second"): string {
  return side === "first" ? "your side" : "the enemy";
}

/**
 * What a cell holds, as a screen reader hears it. The label describes the tile
 * rather than the gesture: what a press does is decided by the mode above the
 * board and said there, and a label that claimed otherwise would go stale the
 * moment the mode changed.
 */
function cellLabel(x: number, y: number): string {
  const here = occupants(x, y);
  const ground = `Column ${x + 1}, row ${y + 1}: ${terrainAt(x, y)}`;
  if (here.length === 0) return `${ground}, empty`;
  const who = here.map(({ placement }) => {
    const name = unitTypeName(placement.unitTypeId) ?? placement.unitTypeId;
    const member = memberName(placement.memberId);
    return `${member ? `${member}, ` : ""}${name} of ${sideWord(placement.side)}`;
  });
  return `${ground}, ${who.join("; ")}`;
}

// ---------------------------------------------------------------------------
// The palette: pick a character up, put copies of them down.
// ---------------------------------------------------------------------------

const paletteKey = ref("");
/**
 * Whose side the next character put down fights for, asked only where the
 * character does not already answer it. A character carries a faction, and two
 * of those factions *are* the sides, so most picks settle this without anybody
 * being asked. See `knownSide`.
 */
const paletteSide = ref<"first" | "second">("second");
/** What to call a character the palette is about to make, if not its own word. */
const newName = ref("");
/**
 * The name of a character this board has asked for and has not been handed
 * back yet.
 *
 * The board does not write records, so between the ask and the props changing
 * there is a moment where the character the author is putting down does not
 * exist. Holding the name across it is what lets the palette pick them up the
 * instant they arrive, so a second press puts down a second one rather than
 * making a second character.
 */
const awaitingName = ref("");
const paletteRoot = ref<HTMLElement>();
/**
 * Members this board asked for and the campaign has not handed back yet.
 *
 * The ask goes up and the answer comes down as a prop, so between the two there
 * is a moment where somebody exists on this board and nowhere else. They are
 * held here so a second stamp does not hand out the same identifier twice, and
 * dropped the moment the campaign says it has them. Otherwise this list is a
 * second, stale copy of the company that only ever grows.
 */
const enrolled = ref<CampaignRosterMember[]>([]);

watch(
  () => props.members,
  (members) => {
    if (members === undefined) return;
    const known = new Set(members.map((member) => member.id));
    enrolled.value = enrolled.value.filter((member) => !known.has(member.id));
  },
  { deep: true }
);

/**
 * Whose side a character fights for, when the character already says.
 *
 * `character-standing.ts` writes "one of yours" and "an enemy" as two ordinary
 * faction records, and a faction already decides the colour a character is
 * drawn in everywhere. So a character wearing one of them has answered the
 * side question, and asking it again on every press is asking an author to
 * repeat themselves, and to contradict themselves, since nothing stopped the
 * two answers disagreeing. A character wearing somebody's own faction, or
 * none, genuinely has not answered, and only then is there a picker.
 */
function knownSide(
  factionId: string | undefined
): "first" | "second" | undefined {
  const faction = sideFaction(factionId);
  if (!faction) return undefined;
  // Blue is the player's side and red the opposing one, on every board and in
  // every ROM. Read from the faction rather than from its identifier so there
  // is one place the sides are listed.
  return faction.color === "blue" ? "first" : "second";
}

/** The catalogue shelf this game is offered, from the style it is drawn in. */
const shelf = computed(() => shelfSetting(props.characterStyleId));

/** One thing the palette can put down. */
interface PaletteEntry {
  readonly key: string;
  readonly label: string;
  readonly sprite: string;
  /** The character in this game, when this entry is one of them. */
  readonly unitTypeId?: string;
  /** The role to make, when this game has not got them yet. */
  readonly role?: CharacterRole;
  /** Why they may not be put down, in words an author reads, or nothing. */
  readonly blocked: string;
}

/**
 * Why a character cannot be put down again, if they cannot.
 *
 * A person stands in one place. A second placement of one person is that
 * person standing twice, which is the thing that cannot happen, and it does not
 * become possible by being on the author's own side.
 *
 * **It used to.** A placement on the player's own side, on a board belonging to
 * a campaign, was exempt, and the argument was that each such placement fields
 * a *different* member of the company, so a second one is a second person. The
 * argument does not survive contact with the palette: what an author stamps is
 * a unit *type*, so a second stamp of Warden Kesh is Warden Kesh again, and the
 * board enrolled a numbered second body for her. The owner has ruled that a
 * mistake — main units are unique to a Stage.
 *
 * What that rule does not touch is the case the exemption was defending:
 * temporary bodies on the player's side for one map. Those are placements that
 * name nobody, so they are a kind rather than a person, `onePerson` is false for
 * them, and an author may still stand as many as they like. The rule is about
 * who somebody is and never about which side they are on.
 */
function blockedReason(unitType: {
  readonly id: string;
  readonly name: string;
  readonly factionId?: string;
  readonly onePerson?: boolean;
}): string {
  if (!unitType.onePerson) return "";
  if (!draftPlacements.value.some(
    (placement) => placement.unitTypeId === unitType.id
  )) {
    return "";
  }
  return (
    `${unitType.name} already stands on this board. A member of the company ` +
    "is one person and cannot be in two places at once. Move the one who is " +
    "here, or put somebody else down."
  );
}

/**
 * Everybody the palette offers: this game's own characters first, then the
 * roles it has not got.
 *
 * A role whose name this game already uses is left off the shelf. The offer is
 * "you have no Knight, press here and you will", and beside a Knight that
 * already exists it would be an offer to make a second one nobody asked for.
 */
const paletteEntries = computed<PaletteEntry[]>(() => {
  const mine = props.unitTypes.map((unitType) => ({
    key: `unit:${unitType.id}`,
    label: unitType.name,
    sprite: assetBase + unitSprite(
      unitType.classId,
      knownSide(unitType.factionId) ?? paletteSide.value,
      factionColor(props.factions ?? [], unitType.factionId),
      props.characterStyleId
    ),
    unitTypeId: unitType.id,
    blocked: blockedReason(unitType)
  }));
  const taken = new Set(props.unitTypes.map((unitType) => unitType.name));
  const offers = characterRecipes
    .filter((recipe) => recipe.setting === shelf.value && !taken.has(recipe.label))
    .map((recipe) => ({
      key: `new:${recipe.id}`,
      label: recipe.label,
      // The role is the archetype the art draws, so it resolves a drawing on
      // its own, the character having no class yet to look one up from.
      sprite: assetBase + unitSprite(
        recipe.role,
        paletteSide.value,
        undefined,
        props.characterStyleId
      ),
      role: recipe.role,
      blocked: ""
    }));
  return [...mine, ...offers];
});

/** The entry a press would put down: the one picked, or the first offered. */
const picked = computed<PaletteEntry | undefined>(() =>
  paletteEntries.value.find((entry) => entry.key === paletteKey.value) ??
  paletteEntries.value[0]
);

const paletteUnit = computed(() => {
  const id = picked.value?.unitTypeId;
  return id === undefined
    ? undefined
    : props.unitTypes.find((unitType) => unitType.id === id);
});

/** Whose side the next press puts somebody on. */
const stampSide = computed<"first" | "second">(
  () => knownSide(paletteUnit.value?.factionId) ?? paletteSide.value
);

/** Whether the picked character has already answered the side question. */
const sideIsSettled = computed(
  () => knownSide(paletteUnit.value?.factionId) !== undefined
);

/** What the character about to be made is called. */
const newCharacterName = computed(
  () => newName.value.trim() || picked.value?.label || ""
);

/** Picking a character up is what arms the stamp; nothing else has to be set. */
function pickUp(key: string) {
  paletteKey.value = key;
  gridMode.value = "stamp";
}

function movePaletteFocus(step: number) {
  const entries = paletteEntries.value;
  const next = entries[
    entries.findIndex((entry) => entry.key === picked.value?.key) + step
  ];
  if (!next) return;
  pickUp(next.key);
  paletteRoot.value
    ?.querySelector<HTMLElement>(`[data-palette="${next.key}"]`)
    ?.focus();
}

// The character this board asked for, arriving. Picked up rather than left
// behind, so pressing three tiles puts down three bandits instead of making
// three Bandits.
watch(
  () => props.unitTypes,
  (unitTypes) => {
    if (awaitingName.value === "") return;
    const made = unitTypes.find(
      (unitType) => unitType.name === awaitingName.value
    );
    if (!made) return;
    awaitingName.value = "";
    newName.value = "";
    paletteKey.value = `unit:${made.id}`;
  },
  { deep: true }
);

/** An identifier for a new member that nothing in the campaign is using. */
function freeMemberId(stem: string): string {
  const taken = new Set([
    ...(props.members ?? []).map((member) => member.id),
    ...enrolled.value.map((member) => member.id)
  ]);
  if (!taken.has(stem)) return stem;
  let suffix = 2;
  while (taken.has(`${stem}_${suffix}`)) suffix += 1;
  return `${stem}_${suffix}`;
}

/**
 * A name for a new member that nobody in the campaign already answers to.
 *
 * Two members called "Warden" is the company reading as though the same person
 * were in it twice, which is what an author sees, because the roster shows
 * names and not identifiers. They are two people, so they get two names, and
 * either can be renamed to something better afterwards.
 */
function freeMemberName(stem: string): string {
  const taken = new Set([
    ...(props.members ?? []).map((member) => member.name),
    ...enrolled.value.map((member) => member.name)
  ]);
  if (!taken.has(stem)) return stem;
  let suffix = 2;
  while (taken.has(`${stem} ${suffix}`)) suffix += 1;
  return `${stem} ${suffix}`;
}

/**
 * Puts a copy of the picked-up character on a tile.
 *
 * Picked up off the shelf rather than out of the game, they do not exist yet,
 * and the whole of this is an ask that goes upward. See the branch below.
 * Everything after it is about somebody this game already has.
 *
 * On the opposing side that is the whole of it, and the placement names
 * nobody. That is precisely what makes the character an *extra*: three bandits
 * are three placements of one Bandit and nothing in the game depends on any of
 * them being who they are.
 *
 * On the player's own side the format is stricter: a placement there fields a
 * member of the company, because your people carry what they learned between
 * Stages and a placement naming nobody has nobody to carry it. So the board
 * fields somebody who is already in the company and free, and enrols the
 * character when nobody is, and says which it did. The company editor asks
 * "which character they are" and the board asks who stands on a tile; a
 * company that quietly grew is the surprise those two questions breed when
 * neither mentions the other.
 */
function stamp(x: number, y: number) {
  const entry = picked.value;
  if (!entry) {
    notice.value = "There is nobody to put on the board.";
    return;
  }
  if (occupants(x, y).length > 0) {
    notice.value =
      `Somebody already stands on column ${x + 1}, row ${y + 1}. ` +
      "Choose an empty tile.";
    return;
  }
  if (entry.blocked !== "") {
    notice.value = entry.blocked;
    return;
  }
  // Somebody this game has not got. The board asks and does not write: the
  // character, its class, its weapon and this placement are one act, and the
  // only surface that can land all four at once is the one holding the
  // session. Nothing is put in `draftPlacements` here, so a refusal up there
  // leaves this board saying exactly what the project says.
  if (entry.role !== undefined) {
    const name = newCharacterName.value;
    const side = paletteSide.value;
    awaitingName.value = name;
    emit("addCharacter", {
      role: entry.role,
      setting: shelf.value,
      name,
      side,
      x,
      y
    });
    notice.value =
      `Making ${name} and putting them on column ${x + 1}, row ${y + 1}, ` +
      `fighting for ${sideWord(side)}.`;
    return;
  }
  const unitType = paletteUnit.value!;
  const placements = draftPlacements.value.map((placement) => ({ ...placement }));
  const side = stampSide.value;
  let memberId: string | undefined;
  let said = `Put ${unitType.name} on column ${x + 1}, row ${y + 1}, ` +
    `fighting for ${sideWord(side)}.`;
  if (side === "first" && props.members !== undefined) {
    const standing = new Set(
      placements.flatMap((placement) =>
        placement.memberId === undefined ? [] : [placement.memberId]
      )
    );
    const free = [...(props.members ?? []), ...enrolled.value].find(
      (member) => member.unitTypeId === unitType.id && !standing.has(member.id)
    );
    if (free) {
      memberId = free.id;
      said += ` ${free.name} of the company stands there.`;
    } else {
      const member: CampaignRosterMember = {
        id: freeMemberId(unitType.id),
        name: freeMemberName(unitType.name),
        unitTypeId: unitType.id
      };
      enrolled.value = [...enrolled.value, member];
      emit("enroll", member);
      memberId = member.id;
      said +=
        ` Your side is fought by the company, so ${member.name} joined it. ` +
        "They are an ordinary member you can rename or remove.";
    }
  }
  placements.push({
    id: uniqueId(placements.map((placement) => placement.id)),
    ...(memberId ? { memberId } : {}),
    unitTypeId: unitType.id,
    side,
    x,
    y
  });
  selectedIndex.value = placements.length - 1;
  draftPlacements.value = placements;
  notice.value = said;
  emit("update", placements);
}

/** What pressing a tile does now, said once above the board and in the modes. */
const gestureHelp = computed(() => {
  if (gridMode.value === "stamp") {
    const entry = picked.value;
    if (!entry) return "Pressing a tile does nothing: nobody is picked.";
    if (entry.blocked !== "") return entry.blocked;
    if (entry.role !== undefined) {
      return (
        `Pressing a tile makes ${newCharacterName.value} and puts them on ` +
        `it, fighting for ${sideWord(paletteSide.value)}.`
      );
    }
    return (
      `Pressing a tile puts ${entry.label} on it, fighting for ` +
      `${sideWord(stampSide.value)}.`
    );
  }
  if (gridMode.value === "patrol") {
    return "Pressing a tile adds it to the selected character's patrol, or " +
      "removes it when it is already one.";
  }
  const selected = draftPlacements.value[selectedIndex.value];
  return selected
    ? `Pressing a tile moves ${
      unitTypeName(selected.unitTypeId) ?? selected.unitTypeId} to it.`
    : "Pressing a tile moves the selected character to it. Nobody is on " +
      "this board yet.";
});

/**
 * Everything wrong anywhere on this board, each line naming who it is about.
 *
 * The panel below shows one placement, so this is what keeps a problem on any
 * of the others from going quiet: it is the whole board's report, above the
 * one token's form, and it is the only place these problems are printed.
 */
const boardProblems = computed(() =>
  draftPlacements.value.flatMap((placement, index) =>
    placementProblems(placement, index).map((problem) => ({
      index,
      problem,
      who: placement.name ??
        unitTypeName(placement.unitTypeId) ??
        placement.id
    }))
  )
);

/** The one placement the panel below is about, or nothing on an empty board. */
const selectedPlacement = computed(
  () => draftPlacements.value[selectedIndex.value]
);

/**
 * The line at the top of the panel: which token this is, where it stands, and
 * the identifier the format files it under.
 *
 * All three are read back rather than asked for. The position is said because
 * a panel about somebody standing somewhere that never says where reads as an
 * omission, and because it is what somebody using a screen reader has instead
 * of the board. The identifier is said because a Stage's win conditions offer
 * placements by it, so an author matching one up has to be able to see it.
 */
const placementSummary = computed(() => {
  const placement = selectedPlacement.value;
  if (!placement) return "";
  const who = placement.name ??
    unitTypeName(placement.unitTypeId) ??
    placement.unitTypeId;
  return `${who}, standing at column ${placement.x + 1}, row ${
    placement.y + 1}. Filed as '${placement.id}'.`;
});

/**
 * One press on a tile, whichever of the three things the board is set to do.
 *
 * Moving has one more rule than the other two: pressing somebody who is not
 * the character being moved picks *them* up instead. The board is the only way
 * to choose whose panel is open below, so a press on a token has to mean
 * "this one". Moving a character onto a tile somebody else already holds
 * is the collision the board draws in red, never a thing an author meant.
 */
/**
 * What a press means, decided by what is under it rather than by a mode.
 *
 * There used to be a switch with "Puts a character down" and "Moves the
 * selected character" on it, and an author had to set it before every gesture.
 * It is gone: pressing somebody picks them up and pressing empty ground puts
 * down or moves, which is what the two buttons were asking about anyway.
 *
 * Pressing an occupied tile therefore selects whoever is on it, even with a
 * character armed from the palette. What that costs is stamping a second body
 * onto an occupied tile by pressing, and that is a collision this editor
 * already draws in red and reports as a problem: it was never a thing to make
 * on purpose. Everything else the palette can do it still does, and dragging
 * does the same two jobs for whoever would rather drag.
 *
 * Patrol keeps a mode of its own, because dropping a marker on a tile is a
 * third meaning that nothing about the tile can tell you.
 */
function pressTile(x: number, y: number) {
  if (gridMode.value !== "patrol") {
    // Anybody at all, not merely somebody other than the selection: pressing
    // the tile a character already stands on is picking that character up,
    // whether or not they were the one in hand. Stamping refused an occupied
    // tile anyway, so nothing that used to work stops working.
    const here = occupants(x, y);
    const standing = here.find(({ index }) => index !== selectedIndex.value)
      ?? here[0];
    if (standing) {
      selectedIndex.value = standing.index;
      gridMode.value = "place";
      notice.value = `${
        unitTypeName(standing.placement.unitTypeId) ?? standing.placement.id
      } is selected. Press an empty tile to move them, or drag them off the ` +
        "board to take them out.";
      return;
    }
  }
  if (gridMode.value === "stamp") {
    stamp(x, y);
    return;
  }
  moveSelected(x, y);
}
</script>

<template>
  <section class="placement-editor" aria-labelledby="placement-title">
    <h4 id="placement-title">Who fights here, and where they start</h4>

    <!-- The palette. Picking somebody up is what arms the stamp, so there is
         no separate "now start placing" control to find, and it offers the
         characters this game has not got as well as the ones it has: the
         answer to "put a bandit here" is a bandit, never a trip to another
         screen to make one first. -->
    <fieldset class="placement-palette">
      <legend>Who to put down</legend>
      <div ref="paletteRoot" class="palette-units" role="radiogroup"
        aria-label="Which character to put down"
        @keydown.left.prevent="movePaletteFocus(-1)"
        @keydown.up.prevent="movePaletteFocus(-1)"
        @keydown.right.prevent="movePaletteFocus(1)"
        @keydown.down.prevent="movePaletteFocus(1)">
        <button v-for="entry in paletteEntries" :key="entry.key" type="button"
          class="palette-unit" role="radio"
          :class="{ 'palette-new': entry.role !== undefined, blocked: entry.blocked }"
          :data-palette="entry.key"
          :data-unit-type="entry.unitTypeId"
          :aria-checked="picked?.key === entry.key ? 'true' : 'false'"
          :aria-disabled="entry.blocked ? 'true' : undefined"
          :tabindex="picked?.key === entry.key ? 0 : -1"
          @click="pickUp(entry.key)">
          <img :src="entry.sprite" alt="" width="24" height="24">
          <span>{{ entry.label }}</span>
          <!-- Said in the button rather than only in colour, because "this one
               does not exist yet" is the whole difference between the two
               halves of this list. -->
          <span v-if="entry.role !== undefined" class="visually-hidden">:
            not in this game yet
          </span>
          <span v-if="entry.blocked" class="visually-hidden">:
            {{ entry.blocked }}
          </span>
        </button>
      </div>

      <!-- Only for somebody about to be made. A character that exists has a
           name already, and a box repeating it would be a second place to
           change it. -->
      <template v-if="picked?.role !== undefined">
        <label for="palette-new-name">What to call them</label>
        <input id="palette-new-name" v-model="newName"
          :placeholder="picked.label"
          aria-describedby="palette-new-help">
        <p id="palette-new-help" class="field-help">
          Putting them down makes them, their class and their weapon.
        </p>
      </template>

      <!-- The side, asked only where the character has not answered it. -->
      <div v-if="!sideIsSettled" class="palette-sides" role="group"
        aria-label="Which side they fight for">
        <button type="button" :aria-pressed="paletteSide === 'first'"
          @click="paletteSide = 'first'">
          Your side
        </button>
        <button type="button" :aria-pressed="paletteSide === 'second'"
          @click="paletteSide = 'second'">
          The enemy
        </button>
      </div>
      <p v-else class="field-help">
        {{ picked?.label }} always fights for {{ sideWord(stampSide) }}.
      </p>

      <p class="field-help">Every press puts down another one.</p>
    </fieldset>

    <!-- One button, not three. Putting somebody down and moving them are
         decided by what is under the press, so neither needs asking about;
         dropping a patrol marker is a third meaning nothing on the tile can
         tell you, so it keeps a switch. `pressTile` says the whole rule. -->
    <fieldset class="grid-mode" role="group" aria-label="What pressing a tile does">
      <legend>Pressing a tile</legend>
      <button type="button" :aria-pressed="gridMode === 'patrol'"
        @click="gridMode = gridMode === 'patrol' ? 'place' : 'patrol'">
        {{ gridMode === "patrol" ? "Stop adding patrol points" : "Add patrol points" }}
      </button>
    </fieldset>
    <p id="placement-gesture" class="field-help">{{ gestureHelp }}</p>
    <p v-if="map" class="map-summary">
      {{ map.name }} · {{ map.width }}×{{ map.height }}
    </p>
    <p v-else class="placement-warning" role="status">
      Choose a map before placing characters.
    </p>

    <!-- The board, and beside it the one panel that is about what the board has
         selected. Side by side rather than stacked, because a panel underneath a
         board is a panel an author scrolls away from the thing it describes: the
         column this editor used to be ran to some four thousand pixels, and the
         board and this panel were most of it. On a window too narrow to seat
         them side by side the panel drops below, which is the same order it
         always had. -->
    <div class="placement-work" :class="{ 'with-panel': selectedPlacement }">
    <div class="placement-board-side">
    <div v-if="canDrawGrid && map" class="placement-grid-wrap">
      <div ref="gridRoot" class="placement-grid" role="grid"
        aria-describedby="placement-gesture"
        :aria-label="`Who stands where on ${map.name}`">
        <div v-for="row in map.height" :key="row" class="placement-row" role="row"
          :style="{ gridTemplateColumns: `repeat(${map.width}, minmax(2.25rem, 1fr))` }">
          <button v-for="column in map.width"
            :key="(row - 1) * map.width + (column - 1)"
            type="button" role="gridcell"
            class="placement-cell"
            :data-cell="(row - 1) * map.width + (column - 1)"
            :tabindex="(row - 1) * map.width + (column - 1) === focusIndex ? 0 : -1"
            :class="{
              occupied: occupants(column - 1, row - 1).length,
              collision: occupants(column - 1, row - 1).length > 1
            }"
            :style="cellStyle(column - 1, row - 1)"
            :aria-label="cellLabel(column - 1, row - 1)"
            @dragover.prevent
            @drop="dropOn(column - 1, row - 1, $event)"
            @pointerdown="focusIndex = (row - 1) * map.width + (column - 1)"
            @click="pressTile(column - 1, row - 1)"
            @keydown.enter.prevent="pressTile(column - 1, row - 1)"
            @keydown.space.prevent="pressTile(column - 1, row - 1)"
            @keydown.left.prevent="moveFocus(column - 1, row - 1, -1, 0)"
            @keydown.right.prevent="moveFocus(column - 1, row - 1, 1, 0)"
            @keydown.up.prevent="moveFocus(column - 1, row - 1, 0, -1)"
            @keydown.down.prevent="moveFocus(column - 1, row - 1, 0, 1)">
            <!-- The drawing the board will use, so the author recognises what
                 they are arranging. It carries no alternative text of its own:
                 the cell's label already names who stands here, and a second
                 voice saying it again is noise. -->
            <template v-for="{ placement, index } in occupants(column - 1, row - 1)"
              :key="`${placement.id}:${index}`">
              <img v-if="spriteFor(placement.unitTypeId, placement.side)"
                class="placed-unit"
                :class="{ selected: selectedIndex === index }"
                :src="spriteFor(placement.unitTypeId, placement.side)"
                :title="placement.id"
                alt="" width="24" height="24"
                draggable="true"
                @dragstart="startDrag(index, $event)"
                @dragend="draggingIndex = -1">
              <!-- A placement naming a character this project does not have.
                   Drawn as a question rather than as somebody, because the one
                   thing the board must not do is look right. -->
              <span v-else class="placed-unknown"
                :class="{ selected: selectedIndex === index }"
                :title="`${placement.id}: '${placement.unitTypeId}' is not a
                  character in this project`"
                draggable="true"
                @dragstart="startDrag(index, $event)"
                @dragend="draggingIndex = -1">?</span>
            </template>
            <span class="terrain-glyph" aria-hidden="true">
              {{ terrainGlyph(terrainAt(column - 1, row - 1)) }}
            </span>
            <span v-if="patrolLeg(column - 1, row - 1) >= 0" class="patrol-marker">
              {{ patrolLeg(column - 1, row - 1) + 1 }}
            </span>
          </button>
        </div>
      </div>
    </div>
    <p v-else-if="map" class="map-summary">
      The map is too large for the overview; use coordinates below.
    </p>
    <!-- Where a character goes to stop being on the board. It is here when
         nothing is being dragged as well, saying what the gesture is: a target
         that appeared mid-drag would move the page under the pointer, and a
         gesture nobody is told about is one nobody finds. -->
    <p v-if="map" class="placement-bin" :class="{ armed: draggingIndex >= 0 }"
      data-testid="placement-bin"
      @dragover.prevent
      @drop="dropOffBoard">
      {{ draggingIndex >= 0
        ? "Drop here to take them off the board"
        : "Drag somebody off the board to take them out." }}
    </p>
    <button type="button" @click="addPlacement">Add character placement</button>

    <p class="placement-notice" role="status">{{ notice }}</p>

    <!-- Every problem on this board, not only the selected token's. One panel
         at a time is what makes the surface readable; it must not also make a
         problem invisible until somebody happens to press the right tile. Each
         line names who it is about and opens them. -->
    <ul v-if="boardProblems.length" class="placement-warning" role="alert">
      <li v-for="entry in boardProblems" :key="`${entry.index}:${entry.problem}`">
        <button type="button" class="secondary"
          @click="selectedIndex = entry.index">
          {{ entry.who }}
        </button>
        {{ entry.problem }}
      </li>
    </ul>

    </div>

    <!-- One panel, about the one token the board has selected.
         A fieldset per placement stacked a form for every person on the
         board, each of them restating the two things the board already draws:
         where they stand, and which of them this is. What is left here is
         everything a board cannot show: who they are, whose side, what they
         do on their turn, whether they can be talked to, and which round they
         arrive on.

         Their position is not typed. Pressing a tile is how an author says
         where somebody stands, so the coordinates are read back rather than
         asked for, except on a map too large for the overview to be drawn at
         all, where there is no board to press and the numbers are the only
         way. Their identifier is not typed either: it is the format's name for
         this token, the editor makes one, and "What to call them" below is
         where an author names somebody. -->
    <fieldset v-if="selectedPlacement" class="placement-fields selected">
      <legend>About this placement</legend>
      <p class="field-help">
        {{ placementSummary }}
      </p>
      <template v-if="members !== undefined && selectedPlacement.side === 'first'">
        <label :for="`placement-${selectedIndex}-member`">Who stands here</label>
        <select :id="`placement-${selectedIndex}-member`" :value="selectedPlacement.memberId ?? ''"
          @change="chooseMember(selectedIndex, ($event.target as HTMLSelectElement).value)">
          <option value="">Nobody yet: choose a member of the company</option>
          <option v-if="selectedPlacement.memberId !== undefined &&
            memberName(selectedPlacement.memberId) === undefined"
            :value="selectedPlacement.memberId" disabled>
            {{ selectedPlacement.memberId }}: nobody in this campaign
          </option>
          <option v-for="member in members" :key="member.id" :value="member.id">
            {{ member.name }} ({{ member.id }}):
            {{ unitTypeName(member.unitTypeId) ?? member.unitTypeId }}
          </option>
        </select>
        <p v-if="members.length === 0" class="placement-warning" role="status">
          This campaign's company is empty, so there is nobody to choose.
        </p>
      </template>
      <label :for="`placement-${selectedIndex}-unit-type`">Character</label>
      <select :id="`placement-${selectedIndex}-unit-type`" :value="selectedPlacement.unitTypeId"
        @change="replace(selectedIndex, {
          unitTypeId: ($event.target as HTMLSelectElement).value
        })">
        <!-- A stored character the project no longer has stays visible and
             selected. A select whose value matches no option renders blank,
             which reads as "not chosen" over a placement that has in fact
             chosen somebody who is gone. -->
        <option v-if="unitTypeName(selectedPlacement.unitTypeId) === undefined"
          :value="selectedPlacement.unitTypeId" disabled>
          {{ selectedPlacement.unitTypeId || "nobody" }}: not a character in this project
        </option>
        <option v-for="unitType in unitTypes" :key="unitType.id"
          :value="unitType.id">
          {{ unitType.name }} ({{ unitType.id }})
        </option>
      </select>
      <p v-if="selectedPlacement.memberId !== undefined" class="field-help">
        A member is the same character on every board, so
        {{ memberName(selectedPlacement.memberId) ?? selectedPlacement.memberId }} decides this.
      </p>
      <template v-if="selectedPlacement.memberId === undefined">
        <label :for="`placement-${selectedIndex}-name`">What to call them</label>
        <input :id="`placement-${selectedIndex}-name`"
          :value="selectedPlacement.name ?? ''"
          :placeholder="derivedName(selectedIndex)"
          @input="setName(selectedIndex, ($event.target as HTMLInputElement).value)">
        <p class="field-help">{{ nameHelp }}</p>
      </template>
      <label :for="`placement-${selectedIndex}-side`">Side</label>
      <select :id="`placement-${selectedIndex}-side`" :value="selectedPlacement.side"
        @change="changeSide(
          selectedIndex,
          ($event.target as HTMLSelectElement).value as 'first' | 'second'
        )">
        <option value="first">Your side: you move these</option>
        <option value="second">The enemy: plays by its own behaviour</option>
      </select>
      <label :for="`placement-${selectedIndex}-behavior`">
        What it does on its own turn
      </label>
      <select :id="`placement-${selectedIndex}-behavior`"
        :value="selectedPlacement.behavior ?? 'hold'"
        @change="replace(selectedIndex, {
          behavior: ($event.target as HTMLSelectElement).value as
            'hold' | 'patrol' | 'pursue'
        })">
        <option value="hold">Stays put, attacks anything in reach</option>
        <option value="patrol">Walks its patrol points</option>
        <option value="pursue">Chases the nearest enemy</option>
      </select>
      <p v-if="(selectedPlacement.behavior ?? 'hold') === 'patrol'" class="field-help">
        {{ (selectedPlacement.patrolPoints ?? []).length }} patrol points. Add them
        with the tile mode above.
      </p>
      <!-- The checkbox, what it means, and, only once it is on, the flag it
           raises with the one thing the checkbox could never say: what reads a
           flag. The explanation sits under the control it explains rather than
           under the field below it, so neither paragraph is read as belonging
           to the other. -->
      <label class="boolean-field" :for="`placement-${selectedIndex}-talkable`">
        <input :id="`placement-${selectedIndex}-talkable`" type="checkbox"
          :checked="selectedPlacement.talk !== undefined"
          @change="setTalkable(
            selectedIndex, ($event.target as HTMLInputElement).checked
          )">
        Somebody a character can talk to
      </label>
      <p class="field-help">{{ talkHelp }}</p>
      <template v-if="selectedPlacement.talk">
        <label :for="`placement-${selectedIndex}-talk-flag`">
          World flag talking to them raises
        </label>
        <input :id="`placement-${selectedIndex}-talk-flag`"
          :value="selectedPlacement.talk.flagId"
          :pattern="idPattern"
          :aria-describedby="`placement-${selectedIndex}-talk-flag-help`"
          @input="setTalkFlag(
            selectedIndex, ($event.target as HTMLInputElement).value
          )">
        <p :id="`placement-${selectedIndex}-talk-flag-help`" class="field-help">
          {{ talkFlagHelp }}
        </p>
      </template>
      <label :for="`placement-${selectedIndex}-arrival`">When it arrives</label>
      <select :id="`placement-${selectedIndex}-arrival`"
        :value="selectedPlacement.arrival ? 'wave' : 'opening'"
        @change="setArrival(
          selectedIndex,
          ($event.target as HTMLSelectElement).value === 'wave'
        )">
        <option value="opening">On the board when the Stage opens</option>
        <option value="wave">Arrives as a later round begins</option>
      </select>
      <template v-if="selectedPlacement.arrival">
        <label :for="`placement-${selectedIndex}-arrival-round`">First arrival</label>
        <input :id="`placement-${selectedIndex}-arrival-round`" type="number"
          min="2" max="4095"
          :value="arrivalKeystrokes.shown(
            `${selectedIndex}-round`, String(selectedPlacement.arrival.round)
          )"
          @input="arrivalKeystrokes.type(
            `${selectedIndex}-round`,
            ($event.target as HTMLInputElement).value,
            (typed) => replace(selectedIndex, {
              arrival: { ...selectedPlacement!.arrival!, round: Number(typed) }
            })
          )"
          @change="arrivalKeystrokes.leave(
            `${selectedIndex}-round`,
            ($event.target as HTMLInputElement).value,
            (typed) => replace(selectedIndex, {
              arrival: { ...selectedPlacement!.arrival!, round: Number(typed) }
            })
          )">
        <label :for="`placement-${selectedIndex}-arrival-every`">
          Rounds between arrivals
        </label>
        <input :id="`placement-${selectedIndex}-arrival-every`" type="number"
          min="0" max="4095"
          :value="arrivalKeystrokes.shown(
            `${selectedIndex}-every`,
            String(selectedPlacement.arrival.every ?? 0)
          )"
          @input="arrivalKeystrokes.type(
            `${selectedIndex}-every`,
            ($event.target as HTMLInputElement).value,
            (typed) => setRecurrence(
              selectedIndex, Number(typed), selectedPlacement?.arrival?.times ?? 0
            )
          )"
          @change="arrivalKeystrokes.leave(
            `${selectedIndex}-every`,
            ($event.target as HTMLInputElement).value,
            (typed) => setRecurrence(
              selectedIndex, Number(typed), selectedPlacement?.arrival?.times ?? 0
            )
          )">
        <label :for="`placement-${selectedIndex}-arrival-times`">
          How many arrivals
        </label>
        <input :id="`placement-${selectedIndex}-arrival-times`" type="number"
          min="0" max="64"
          :value="arrivalKeystrokes.shown(
            `${selectedIndex}-times`,
            String(selectedPlacement.arrival.times ?? 0)
          )"
          @input="arrivalKeystrokes.type(
            `${selectedIndex}-times`,
            ($event.target as HTMLInputElement).value,
            (typed) => setRecurrence(
              selectedIndex, selectedPlacement?.arrival?.every ?? 0, Number(typed)
            )
          )"
          @change="arrivalKeystrokes.leave(
            `${selectedIndex}-times`,
            ($event.target as HTMLInputElement).value,
            (typed) => setRecurrence(
              selectedIndex, selectedPlacement?.arrival?.every ?? 0, Number(typed)
            )
          )">
        <p class="field-help">
          Zero both for a single arrival, or set both for a wave.
        </p>
      </template>
      <!-- Only where there is no board to press. A map larger than the
           overview can draw is the one case an author cannot say "here" with
           a gesture, and a panel that then offered no way at all to move
           somebody would be a board editor that cannot edit that board. -->
      <template v-if="!canDrawGrid">
        <label :for="`placement-${selectedIndex}-x`">X coordinate</label>
        <input :id="`placement-${selectedIndex}-x`" :value="selectedPlacement.x"
          type="number" min="0" :max="map ? map.width - 1 : 4095"
          @input="replace(selectedIndex, {
            x: Number(($event.target as HTMLInputElement).value)
          })">
        <label :for="`placement-${selectedIndex}-y`">Y coordinate</label>
        <input :id="`placement-${selectedIndex}-y`" :value="selectedPlacement.y"
          type="number" min="0" :max="map ? map.height - 1 : 4095"
          @input="replace(selectedIndex, {
            y: Number(($event.target as HTMLInputElement).value)
          })">
      </template>
      <button type="button" class="danger"
        @click="removePlacement(selectedIndex)">
        Remove unit placement
      </button>
    </fieldset>
    </div>
  </section>
</template>

<style scoped>
/*
 * One placement's fields, each label standing over the control it names.
 *
 * Without this the fieldset is an ordinary block and a label is inline, so
 * every label runs up beside the previous control instead of standing over its
 * own: "Side" to the left of the behaviour menu, "X coordinate" to the right of
 * the arrival one. Read down the column, the words and the controls are then
 * off by one, and the talk checkbox, whose words live inside its label rather
 * than beside it, reads as part of the line above.
 *
 * A grid of single-column rows is the same shape `.campaign-node-editor
 * fieldset` already uses, and the same shape a `form` uses; this fieldset is
 * inside neither.
 *
 * Named for these fieldsets alone, not for every fieldset here: the palette
 * and the tile-mode row are laid out by rules of their own, and a rule reaching
 * them would win on specificity and stack two rows of buttons into a column.
 */
.placement-fields {
  display: grid;
  gap: 0.4rem;
  justify-items: start;
}

.placement-fields > input,
.placement-fields > select {
  justify-self: stretch;
  max-width: 28rem;
}

.grid-mode {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  margin: 0.5rem 0;
  padding: 0.5rem;
}
.grid-mode button[aria-pressed="true"] {
  outline: 3px solid #b78c23;
  outline-offset: 1px;
}
.placement-palette {
  margin: 0.5rem 0;
  padding: 0.5rem;
}
.palette-sides {
  display: flex;
  gap: 0.5rem;
  margin-bottom: 0.5rem;
}
.palette-sides button[aria-pressed="true"] {
  outline: 3px solid #b78c23;
  outline-offset: 1px;
}
/* A board is the one thing in this editor that is worth the window's width, so
 * this section opts out of the 48rem every other section is held to. Held at
 * that width the board, the palette and the panel had nowhere to go but under
 * one another, which is the whole of why the column ran to four thousand
 * pixels. Nothing else here is any wider than it was: the panel is capped, the
 * fields inside it are capped, and prose is still measured in a readable line.
 */
.placement-editor {
  max-width: none;
}

/* One row that scrolls sideways, not a wrapping block that grows downward.
 * Eight characters wrapped into a column of eight before the board began, so
 * the first thing this editor showed was a list and the board was below the
 * fold. A strip is as tall as one character however many there are. */
.palette-units {
  display: flex;
  flex-wrap: nowrap;
  gap: 0.35rem;
  overflow-x: auto;
  padding-bottom: 0.25rem;
}
.palette-unit {
  flex: 0 0 auto;
}

/* The board and the panel about what it has selected, side by side wherever
 * there is room for both. Below 60rem there is not, and the panel goes back
 * under the board, which is where it always was. */
.placement-work {
  display: grid;
  gap: 1rem;
  align-items: start;
}

/* Two columns when there is something to put in the second one and room to put
 * it, and the room asked about is this editor's own rather than the window's.
 * A window is a poor proxy here: this surface sits inside a rail, a record list
 * and a section, so a 1280-wide window had left it 519 px, and a media query
 * that read the window happily seated a panel that squeezed the board to two
 * hundred. The strip a character is dragged to came out a hundred and fifty
 * pixels wide and below the fold, which is a gesture nobody can make.
 *
 * A column is also only reserved when a panel is open, because width taken for
 * a panel nobody has asked for is width taken from the board. */
.placement-editor {
  container-type: inline-size;
}

@container (min-width: 46rem) {
  .placement-work.with-panel {
    grid-template-columns: minmax(0, 1fr) minmax(16rem, 20rem);
  }
}

.placement-board-side {
  min-width: 0;
}
.palette-unit {
  display: flex;
  gap: 0.35rem;
  align-items: center;
  padding: 0.25rem 0.4rem;
  background: #ffffff;
  color: #1c2a20;
  border: 1px solid #c7d2ca;
}
.palette-unit img {
  image-rendering: pixelated;
}
.palette-unit[aria-checked="true"] {
  border-color: #2e9e5b;
  border-width: 2px;
  padding: calc(0.25rem - 1px) calc(0.4rem - 1px);
  background: #eaf6ee;
}
/* A character this game has not got yet. Dashed, because the drawing is a
   promise of what pressing a tile would make rather than something that is
   already here. */
.palette-new {
  border-style: dashed;
  background: #f6f4ec;
}
/* Somebody already standing on this board who is one person. Not `disabled`:
   the button still takes focus and still answers, because "why can I not press
   this" is a question the palette has to be able to hear. */
.palette-unit.blocked {
  opacity: 0.55;
}
.palette-unit.blocked img {
  filter: grayscale(1);
}
.placed-unit {
  cursor: grab;
  image-rendering: pixelated;
}
.placed-unit:active {
  cursor: grabbing;
}
/* A placement naming nobody the project has. Deliberately not a figure: a
   drawing here would be a guess, and the archetype default makes that guess a
   knight. */
.placed-unknown {
  display: inline-block;
  width: 24px;
  height: 24px;
  border: 2px dashed #a02c2c;
  border-radius: 0.2rem;
  color: #a02c2c;
  font-weight: 700;
  line-height: 20px;
  text-align: center;
  cursor: grab;
}
.placed-unknown.selected {
  outline: 2px solid #b78c23;
  outline-offset: -1px;
}
/* Quiet until something is being dragged, because it is an instruction most of
   the time and a target only while one is in the air. It never moves or
   resizes between those two states: a target that changed the layout under a
   pointer mid-drag is one some browsers cancel the drag over. */
.placement-bin {
  margin: 0.35rem 0;
  padding: 0.35rem 0.5rem;
  border: 1px dashed #b8c2b8;
  border-radius: 0.35rem;
  color: #4b566d;
  font-size: 0.875rem;
  text-align: center;
}
.placement-bin.armed {
  border-color: #a02c2c;
  border-style: solid;
  color: #a02c2c;
  font-weight: 700;
}

.placement-notice {
  min-height: 1.2rem;
  margin: 0.25rem 0;
  font-size: 0.85rem;
  color: #46524a;
}
.terrain-glyph {
  position: absolute;
  top: 0.05rem;
  left: 0.15rem;
  color: #1c2a26;
  font-size: 0.7rem;
  opacity: 0.65;
}
.patrol-marker {
  position: absolute;
  right: 0.1rem;
  bottom: 0.1rem;
  padding: 0 0.2rem;
  border-radius: 0.2rem;
  background: #17201f;
  color: #f2c14e;
  font-size: 0.7rem;
}
.placement-cell {
  position: relative;
}
.placement-grid-wrap {
  max-height: 28rem;
  margin: 0.75rem 0;
  overflow: auto;
}


.placement-grid {
  display: grid;
  width: max-content;
  min-width: 100%;
  gap: 2px;
}

.placement-row {
  display: grid;
  gap: 2px;
}

.placement-cell {
  min-width: 2.25rem;
  min-height: 2.25rem;
  padding: 0.15rem;
  overflow: hidden;
  font-size: 0.7rem;
}

.placement-cell.occupied {
  border-color: #426c46;
  background: #dff0df;
}

.placement-cell.collision {
  border-color: #a02c2c;
  background: #f7dada;
}

.placement-cell span {
  display: block;
}

/* The selected placement is the one the coordinate fields and the patrol mode
   act on, so it is marked on the board as well as in the list below. */
.placed-unit.selected {
  outline: 2px solid #b78c23;
  outline-offset: -1px;
}

fieldset.selected {
  border-color: #426c46;
}

.placement-warning {
  color: #8a2020;
}

.map-summary {
  color: #555;
}
</style>
