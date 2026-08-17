<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { computed, nextTick, onMounted, ref, toRaw, watch } from "vue";
import type {
  CampaignFlow,
  SourceMap,
  SourceProject,
  SourceUnitType
} from "../generated/source-v1";
import {
  htmlPattern,
  identifierFromName,
  sourceEditableProjectFields,
  sourceGameSettingsFields,
  sourceProjectMetadataFields,
  sourceRecordFields
} from "../domain/source-form-model";
import GameSettings from "./GameSettings.vue";
import {
  SourceProjectEditError,
  SourceProjectSession,
  type SourceCollectionName,
  type SourceEdit,
  type SourceRecord
} from "../domain/source-project-session";
import {
  CATALOGUE_SETTINGS,
  abilityRecipes,
  buildAbility,
  buildCharacterChain,
  buildWeaponPair,
  shelfSetting,
  weaponRecipes,
  type CatalogueSetting
} from "../domain/character-recipe";
import { SIDE_FACTIONS } from "../domain/character-standing";
import {
  stagesInProject,
  stagesOnMap,
  stagesDecidedBy,
  stageAt,
  sceneSentence
} from "../domain/stages";
import { planStageOnMap, type StageIntent } from "../domain/stage-setup";
import { planCharacterOnBoard, type CastAsk } from "../domain/stage-cast";
import { COLLECTION_WORD } from "../domain/author-words";
import {
  membersFieldedByNode,
  unenrolMembersNoLongerFielded,
  unfieldedMembers
} from "../domain/campaign-company";
import CharacterRoster from "./CharacterRoster.vue";
import CharacterWizard from "./CharacterWizard.vue";
import SchemaRecordForm, { type ReferenceChoice } from "./SchemaRecordForm.vue";
import CampaignFlowEditor from "./CampaignFlowEditor.vue";
import CampaignFlowGraph from "./CampaignFlowGraph.vue";
import { applyFlowGesture, type FlowGesture } from "../domain/flow-graph";
import DialogueLinesEditor, { type DialogueLine } from "./DialogueLinesEditor.vue";
import DialogueCastEditor, {
  type DialogueCastEntry
} from "./DialogueCastEditor.vue";
import DialoguePreview from "./DialoguePreview.vue";
import DiagnosticPanel, {
  type PresentedDiagnostic
} from "./DiagnosticPanel.vue";
import MapEditor from "./MapEditor.vue";
import StageEditor from "./StageEditor.vue";
import PlaytestPanel from "./PlaytestPanel.vue";
import ItemGrantEditor from "./ItemGrantEditor.vue";
import RosterMemberEditor from "./RosterMemberEditor.vue";
import { useKeystrokeDraft } from "./keystroke-draft";
import { diagnosticTarget } from "../domain/diagnostic-target";
import type { TargetNote } from "../domain/target-budget";
import {
  DEFAULT_WORKSPACE_SECTION,
  WORKSPACE_SECTIONS,
  workspaceSection
} from "../domain/workspace-sections";
import type {
  CampaignItemGrant,
  CampaignNode,
  CampaignRosterMember
} from "../generated/source-v1";

const emit = defineEmits<{
  dirty: [];
  change: [project: SourceProject];
  /** The section the workspace is on, after every change it makes or refuses.
   *  The navigation beside it draws itself from this and never from the click,
   *  so it can never claim a section the workspace declined to leave for. */
  section: [id: string];
}>();

const props = defineProps<{
  initialProject?: SourceProject;
  /** Where the workspace opens. The navigation beside it starts here too. */
  initialSection?: string;
  /** What the last validation found, shown under the Diagnostics section. */
  diagnostics?: readonly PresentedDiagnostic[];
  /** What an old console would make of the game, shown in the same place. */
  targetNotes?: readonly TargetNote[];
}>();

import {
  createSourceProject,
  gameIdFollowingTitle
} from "../domain/source-project-document";

const session = new SourceProjectSession(props.initialProject ?? createSourceProject());
const project = ref(session.snapshot());
const metadataForm = ref<InstanceType<typeof SchemaRecordForm>>();
const settingsPage = ref<InstanceType<typeof GameSettings>>();
const recordForm = ref<InstanceType<typeof SchemaRecordForm>>();
const flowEditor = ref<InstanceType<typeof CampaignFlowEditor>>();
const sceneLines = ref<InstanceType<typeof DialogueLinesEditor>>();
const campaignRoster = ref<InstanceType<typeof RosterMemberEditor>>();
const campaignStore = ref<InstanceType<typeof ItemGrantEditor>>();
const stageEditor = ref<InstanceType<typeof StageEditor>>();

/**
 * The campaign's own name, while it is being typed.
 *
 * It stands on this page rather than in a record form, so it has no draft
 * around it: every other control here belongs to a component that owns one.
 * Committing per keystroke is not the answer: a rename is one undoable step and
 * the session deep-copies the project to make it. So the keystrokes are held
 * here and the rename happens once.
 */
const keystrokes = useKeystrokeDraft(() => emit("dirty"));

/**
 * Commits pending record and flow drafts into the session, so a project Save
 * persists what the author actually typed. False when a draft with problems
 * could not be committed and is still pending.
 */
function flushDrafts(): boolean {
  let flushed = keystrokes.flush();
  flushed = (metadataForm.value?.flush() ?? true) && flushed;
  flushed = (settingsPage.value?.flush() ?? true) && flushed;
  flushed = (recordForm.value?.flush() ?? true) && flushed;
  flushed = (flowEditor.value?.flush() ?? true) && flushed;
  // The lists beside a campaign and the Stage set up beside them hold their own
  // keystrokes, for the same reason and by the same contract.
  flushed = (campaignRoster.value?.flush() ?? true) && flushed;
  flushed = (campaignStore.value?.flush() ?? true) && flushed;
  flushed = (stageEditor.value?.flush() ?? true) && flushed;
  // The scene's lines are edited beside the record form rather than inside it,
  // and a list editor holds a draft exactly as the form does: a Save that
  // skipped it would persist a scene without the line being typed when the
  // author pressed Save.
  flushed = (sceneLines.value?.flush() ?? true) && flushed;
  return flushed;
}

defineExpose({ flushDrafts, selectSection });

/**
 * Leaving an editing surface commits its pending drafts first. A draft that
 * cannot be committed keeps the author where its problems are shown, rather
 * than being unmounted and lost.
 */
function leaveEditors(): boolean {
  if (flushDrafts()) return true;
  feedback.value =
    "Fix the problems shown in the open editor first. Leaving it now would " +
    "lose that work.";
  return false;
}
const selectedId = ref("");
const query = ref("");
const feedback = ref("");
const renameId = ref("");
const renamePreview = ref<readonly string[]>([]);
const page = ref(0);
const pageSize = 100;

// The labels that head a list of records. The word for *one* of them lives in
// `domain/author-words.ts`, which is where the editor's whole vocabulary is
// written down and the only place a stored keyword becomes an author's word.
const allCollections: readonly {
  readonly id: SourceCollectionName;
  readonly label: string;
}[] = [
  { id: "classes", label: "Classes" },
  { id: "unitTypes", label: "Characters" },
  { id: "weaponTypes", label: "Weapon types" },
  { id: "weapons", label: "Weapons" },
  { id: "itemTypes", label: "Item types" },
  { id: "items", label: "Items" },
  { id: "maps", label: "Maps" },
  { id: "factions", label: "Factions" },
  { id: "abilities", label: "Abilities" },
  { id: "objectives", label: "Objectives" },
  { id: "campaigns", label: "Campaigns" },
  { id: "dialogues", label: "Scenes" }
];

/** What one record of a collection is called, from the one place that says. */
const collectionWord = COLLECTION_WORD;

// The sections themselves live in `domain/workspace-sections.ts`, because the
// navigation rail beside this workspace reads the same list and a rail that
// could disagree with the thing it navigates is worse than no rail.
const sections = WORKSPACE_SECTIONS;

/**
 * The collection a section opens on when it is entered from outside, from a
 * navigation rail or from a jump to a reported problem. It is the section's
 * own first collection, which is the one it names first for a reason.
 *
 * Only a section that draws the record columns has one. Stages and Flow are
 * the home of a collection without listing it: an objective is reached through
 * the Stage it decides, a campaign through the graph. Entering either of them
 * selects no record and leaves the one already selected alone.
 */
function openingCollection(sectionId: string): SourceCollectionName | undefined {
  const section = workspaceSection(sectionId);
  return section.kind === "collections" ? section.collections[0] : undefined;
}

const selectedSection = ref(props.initialSection ?? DEFAULT_WORKSPACE_SECTION);
const activeSection = computed(() => workspaceSection(selectedSection.value));
// In the section's own order, not this file's. Two orders are two answers to
// "which collection does this section open on", and a Characters section whose
// tab strip leads with Classes while an arrival from the rail selects
// Characters is one place opening on two different collections depending on
// how it was reached. That is a defect, not a preference.
const collections = computed(() =>
  activeSection.value.collections.flatMap((id) => {
    const found = allCollections.find((collection) => collection.id === id);
    return found ? [found] : [];
  })
);
// The collection the workspace opens on, from the one place that decides it, so
// the record list and the highlighted category agree on the very first frame
// and agree with a later arrival from the rail.
const selectedCollection = ref<SourceCollectionName>(
  openingCollection(selectedSection.value) ?? "classes"
);
const selectedCollectionLabel = computed(() =>
  (allCollections.find((collection) => collection.id === selectedCollection.value)
    ?.label ?? selectedCollection.value).toLocaleLowerCase()
);

/**
 * The shelf an author is offered first, from the style this game is drawn in.
 *
 * The rule itself is `shelfSetting`, shared with the board's own palette,
 * because two doors onto one catalogue guessing differently would be two
 * catalogues. This is what the game-wide style being a *theme* means, and it
 * is deliberately an editor affordance rather than anything the format
 * carries: the style decides how a character is drawn and decides nothing
 * about what an author may reach for. Every other shelf is one click away.
 */
function shelfForProject(): CatalogueSetting {
  return shelfSetting(project.value.characterStyleId);
}

/**
 * Whether the Characters section is showing the wizard instead of the roster.
 *
 * The character catalogue lives inside `CharacterWizard` rather than standing
 * on the page above the author's own characters: it is the same shelf with the
 * two questions the shelf never asked in front of it. Nothing exists until the
 * wizard's last press, so opening and closing it costs nothing.
 */
const makingCharacter = ref(false);
const roster = ref<InstanceType<typeof CharacterRoster>>();

/**
 * Whether the record columns are behind a door on this section.
 *
 * Only Characters, and only because Characters is the one section with a
 * whole friendlier surface in front of them: the roster draws the people this
 * game has, and the wizard is the way to make another. A record browser
 * standing open beside those is a second, greyer way to do the same thing on
 * the first screen of an empty project, with tabs onto three catalogues of
 * parts nobody has asked for yet.
 *
 * They are folded and not moved. Diagnostics was the other candidate and is
 * the wrong place: a problem reported at `/unitTypes/0/classId` is sent to the
 * section that owns the collection, and the record is edited where it is
 * owned. Filing the editor for a character under "what the editor makes of the
 * game" would make that jump leave the section it just arrived at.
 *
 * Maps and Scenes keep their columns in front, because there the record
 * browser *is* the friendly surface, and there is nothing else to lead with.
 */
const recordsBehindFold = computed(() => activeSection.value.id === "characters");
const recordsFoldOpen = ref(false);

/**
 * A shelf is one tab stop with arrow keys inside it, the same way the maps are,
 * so reaching the eighth entry never costs eight tabs. Returns the entry moved
 * to, or nothing at either end of the shelf.
 */
function stepAlongShelf(
  root: HTMLElement | undefined,
  entries: readonly { readonly id: string }[],
  currentId: string,
  step: number
): string | undefined {
  const next = entries[entries.findIndex((entry) => entry.id === currentId) + step];
  if (!next) return undefined;
  root?.querySelector<HTMLElement>(`[data-recipe="${next.id}"]`)?.focus();
  return next.id;
}

/**
 * The weapon shelf: the same library, filtered by the same settings, offering
 * the weapons those characters carry rather than the characters.
 *
 * It exists because a character is not the only thing an author wants. Somebody
 * who has a knight and wants them to also carry a bow needs the bow, not a
 * second archer, and a character may hold more than one weapon, so that is a
 * real thing to want rather than a curiosity.
 */
const weaponSetting = ref<CatalogueSetting>(shelfForProject());
const selectedWeaponId = ref(weaponRecipes[0]!.id);
const newWeaponName = ref("");

const weaponShelf = computed(() =>
  weaponRecipes.filter((recipe) => recipe.setting === weaponSetting.value)
);
const selectedWeapon = computed(
  () =>
    weaponShelf.value.find((recipe) => recipe.id === selectedWeaponId.value) ??
    weaponShelf.value[0]!
);

function selectWeaponSetting(setting: CatalogueSetting) {
  const role = selectedWeapon.value.role;
  weaponSetting.value = setting;
  selectedWeaponId.value = `${setting}_${role}_weapon`;
}

const weaponShelfRoot = ref<HTMLElement>();
function moveWeaponFocus(step: number) {
  const next = stepAlongShelf(
    weaponShelfRoot.value,
    weaponShelf.value,
    selectedWeapon.value.id,
    step
  );
  if (next) selectedWeaponId.value = next;
}

/**
 * The side faction a character joins, and the record to write if this game has
 * not got it.
 *
 * It is an ordinary faction record from the moment it exists, listed under
 * Factions, renameable and deletable. It is reused rather than duplicated,
 * because "the enemy" is one side however many enemies stand on it. Nothing is
 * written here: the faction is one step of making a character, and a step is
 * not an act.
 */
function sideFactionFor(sideId: string): {
  readonly factionId?: string;
  readonly edits: readonly SourceEdit[];
} {
  const chosen = SIDE_FACTIONS.find((faction) => faction.id === sideId);
  if (!chosen) return { edits: [] };
  const existing = (project.value.factions ?? []).some(
    (faction) => faction.id === chosen.id
  );
  return {
    factionId: chosen.id,
    edits: existing ? [] : [{
      kind: "create",
      collection: "factions",
      record: { id: chosen.id, name: chosen.name, color: chosen.color }
    }]
  };
}

/**
 * Creates a whole character, from the wizard's three answers.
 *
 * The weapon type, weapon, class, and unit type all still exist afterwards and
 * are all still individually editable. This is a front door onto the existing
 * depth, not a replacement for it. What it writes are ordinary records: they
 * remember no catalogue entry and no wizard, so nothing here can reach them
 * again and nothing else has to.
 *
 * **One press is one act.** The chain is up to five records and it lands as a
 * single transaction, so the wizard is undone by one press of Undo and a
 * refusal partway leaves nothing behind: no class standing with no character
 * in it, no weapon nobody can hold.
 */
function makeCharacter(choice: {
  role: Parameters<typeof buildCharacterChain>[1];
  setting: CatalogueSetting;
  name: string;
  sideId: string;
  figureId?: SourceUnitType["characterFigureId"];
}) {
  try {
    const chain = buildCharacterChain(
      project.value,
      choice.role,
      choice.name,
      choice.setting
    );
    const side = sideFactionFor(choice.sideId);
    session.transact(`Make ${chain.unitType.name}`, [
      ...side.edits,
      ...(chain.weaponType
        ? [{
          kind: "create" as const,
          collection: "weaponTypes" as const,
          record: chain.weaponType
        }]
        : []),
      { kind: "create", collection: "weapons", record: chain.weapon },
      ...(chain.unitClass
        ? [{
          kind: "create" as const,
          collection: "classes" as const,
          record: chain.unitClass
        }]
        : []),
      {
        kind: "create",
        collection: "unitTypes",
        record: {
          ...chain.unitType,
          ...(side.factionId ? { factionId: side.factionId } : {}),
          // Written only when the author chose the one the game does not
          // already give them. A character that names no figure follows the
          // game, and an override equal to the default says nothing while
          // looking like a decision.
          ...(choice.figureId ? { characterFigureId: choice.figureId } : {})
        }
      }
    ]);
    makingCharacter.value = false;
    selectedSection.value = "characters";
    selectedCollection.value = "unitTypes";
    selectedId.value = chain.unitType.id;
    // A class is an archetype, so the second healer joins the first one's
    // class rather than getting a copy of it. Which of the two happened is
    // said, because "their class" would otherwise read as a record that was
    // made here, and editing it would then change every character in it.
    const joinedClass = project.value.classes.find(
      (entry) => entry.id === chain.unitType.classId
    );
    refresh(
      `Made ${chain.unitType.name}. ` +
      (chain.unitClass
        ? "Their class, their weapon and its weapon type were made with " +
          "them, and each is editable on its own under Advanced."
        : `They join ${joinedClass?.name ?? chain.unitType.classId}, which ` +
          "this game already had, so changing it changes everyone in it. " +
          "Their weapon is their own, under Advanced.")
    );
  } catch (error) {
    makingCharacter.value = false;
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/** Leaves the wizard having made nothing, and puts focus back on the button
 *  that opened it. */
async function cancelCharacter() {
  makingCharacter.value = false;
  await nextTick();
  roster.value?.focusNew();
}

/**
 * The characters whose class permits a weapon type, by name.
 *
 * A class that lists no allowed types permits everything, which is what the
 * source format says an absent list means, so it counts here too.
 */
function charactersAllowing(weaponTypeId: string): readonly string[] {
  const source = toRaw(project.value);
  return source.unitTypes
    .filter((unit) => {
      const allowed = source.classes.find(
        (candidate) => candidate.id === unit.classId
      )?.allowedWeaponTypeIds;
      return allowed === undefined || allowed.includes(weaponTypeId);
    })
    .map((unit) => unit.name);
}

function nameList(names: readonly string[]): string {
  if (names.length <= 3) {
    return names.length < 2
      ? names.join("")
      : `${names.slice(0, -1).join(", ")} and ${names.at(-1)}`;
  }
  return `${names.slice(0, 3).join(", ")} and ${names.length - 3} more`;
}

/**
 * Adds one weapon, with the weapon type it needs when the project has none.
 *
 * Both are ordinary records afterwards, listed and editable beside every other
 * weapon; nothing remembers the shelf they came from. The message names who can
 * already carry it, or the one edit that would let somebody, because a weapon
 * whose type no class permits looks exactly like a weapon that works.
 */
function addWeapon() {
  try {
    const chosen = selectedWeapon.value;
    const pair = buildWeaponPair(
      project.value,
      chosen.role,
      newWeaponName.value,
      chosen.setting
    );
    const carriers = charactersAllowing(pair.weapon.weaponTypeId);
    if (pair.weaponType) session.create("weaponTypes", pair.weaponType);
    session.create("weapons", pair.weapon);
    selectedSection.value = "equipment";
    selectedCollection.value = "weapons";
    selectedId.value = pair.weapon.id;
    newWeaponName.value = "";
    refresh(
      `Added ${pair.weapon.name}, a ${chosen.weaponTypeName} weapon. ` +
      (carriers.length > 0
        ? `${nameList(carriers)} can carry it: open a character and add it ` +
          "to their weapons."
        : `No character's class allows ${chosen.weaponTypeName} yet: open ` +
          `their class and add ${chosen.weaponTypeName} to the weapon types ` +
          "it allows.")
    );
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/**
 * The ability shelf: the same library, filtered by the same settings, offering
 * what a character does rather than who they are or what they hold.
 *
 * It stands in the Characters section rather than beside the weapons for the
 * reason `workspace-sections.ts` already gives: the `abilities` collection lives
 * here, and an ability reaches the board only through a character's abilities.
 * Putting the shelf where the records it makes are listed is what keeps "where
 * did that go" from being a question.
 */
const abilitySetting = ref<CatalogueSetting>(shelfForProject());
const selectedAbilityId = ref(abilityRecipes[0]!.id);
const newAbilityName = ref("");

const abilityShelf = computed(() =>
  abilityRecipes.filter((recipe) => recipe.setting === abilitySetting.value)
);
const selectedAbility = computed(
  () =>
    abilityShelf.value.find((recipe) => recipe.id === selectedAbilityId.value) ??
    abilityShelf.value[0]!
);

function selectAbilitySetting(setting: CatalogueSetting) {
  const cast = selectedAbility.value.cast;
  abilitySetting.value = setting;
  selectedAbilityId.value = `${setting}_${cast}`;
}

const abilityShelfRoot = ref<HTMLElement>();
function moveAbilityFocus(step: number) {
  const next = stepAlongShelf(
    abilityShelfRoot.value,
    abilityShelf.value,
    selectedAbility.value.id,
    step
  );
  if (next) selectedAbilityId.value = next;
}

/**
 * Adds one ability, as an ordinary record.
 *
 * Nothing is attached to anybody. An ability is reached through a character's
 * own `abilityIds`, which is a field an author edits like any other, and a shelf
 * that quietly handed its output to whoever happened to be selected would be a
 * shelf whose effect could not be predicted from what was clicked. The message
 * therefore says the one edit that makes the new record do something, the same
 * way the weapon shelf names who can carry what it just made.
 */
function addAbility() {
  try {
    const chosen = selectedAbility.value;
    const ability = buildAbility(
      project.value,
      chosen.cast,
      newAbilityName.value,
      chosen.setting
    );
    session.create("abilities", ability);
    selectedSection.value = "characters";
    selectedCollection.value = "abilities";
    selectedId.value = ability.id;
    newAbilityName.value = "";
    refresh(
      `Added ${ability.name}. ${chosen.summary} Nobody has it yet: open a ` +
      "character and add it to their abilities."
    );
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/**
 * Moves to a section, or stays put and says so.
 *
 * The section is announced either way, because the navigation beside this
 * workspace draws itself from the announcement rather than from the click: a
 * refused departure must leave the rail naming the section the author is still
 * on, not the one they asked for.
 */
function selectSection(id: string) {
  if (!leaveEditors()) {
    emit("section", selectedSection.value);
    return;
  }
  selectedSection.value = id;
  // Arriving at a section is arriving at the front of it. A fold left open by
  // an earlier visit would mean the page an author lands on depends on what
  // they did ten minutes ago.
  recordsFoldOpen.value = false;
  const first = openingCollection(id);
  // Function declarations hoist, so calling the selector defined below is safe.
  if (first) selectCollection(first);
  // Flow shows the shape of a game, so it has to have something to show. The
  // record that holds a shape is made here rather than asked for.
  if (id === "flow") ensureCampaign();
  // A trip from a map to the graph and back comes back to the map. Without
  // this, "show it in the campaign" is a one-way door: returning lands on an
  // empty Maps page and the author has to find their own ground again.
  if (id === "maps" && returnToMap.value !== undefined) {
    const returning = returnToMap.value;
    returnToMap.value = undefined;
    if (toRaw(project.value).maps.some((map) => map.id === returning)) {
      selectRecord(returning);
    }
  }
  emit("section", id);
}

/**
 * Sends the author to the Stages section with a Stage already open.
 *
 * Every road into a Stage goes through here: the list under a map, the graph,
 * the signpost on a character. There is exactly one arrival and it always
 * lands on the fight rather than on the section's front page.
 */
function openStageIn(campaignId: string, nodeId: string) {
  selectSection("stages");
  if (selectedSection.value !== "stages") return;
  openStage.value = { campaignId, nodeId };
  feedback.value = "";
}

/**
 * Goes to Stages with the open map already picked, having written nothing.
 *
 * A signpost rather than a second front door: the map an author is looking at
 * is the one they meant, so carrying it across saves them choosing it again,
 * but the press that actually makes the Stage is the one under Stages. Two
 * buttons that both make a Stage would be two places it could be made
 * differently.
 */
function startStageOnOpenMap() {
  const map = selectedCollection.value === "maps" ? selectedId.value : "";
  selectSection("stages");
  if (selectedSection.value !== "stages") return;
  openStage.value = undefined;
  if (map !== "") newStageMapId.value = map;
  feedback.value = "";
}

const selectedRawMap = computed(() =>
  toRaw(project.value).maps.find((map) => map.id === selectedId.value)
);
const selectedRawDialogue = computed(() =>
  toRaw(project.value).dialogues?.find(
    (dialogue) => dialogue.id === selectedId.value
  )
);
// One scene, so every previewed line is set against the same thing. The
// preview takes a backdrop per line because a cutscene under Flow is several
// scenes and changes it between them.
const selectedDialogueBackgrounds = computed(() =>
  (selectedRawDialogue.value?.lines ?? []).map(
    () => selectedRawDialogue.value?.backgroundId
  )
);
// The speakers this scene's lines use, in the order they first speak. The cast
// editor offers exactly these, because the join to a line is by exact string
// and a typed speaker off by a letter would be an entry speaking no line.
const selectedDialogueSpeakers = computed(() => {
  const seen: string[] = [];
  for (const line of selectedRawDialogue.value?.lines ?? []) {
    if (line.speaker !== "" && !seen.includes(line.speaker)) {
      seen.push(line.speaker);
    }
  }
  return seen;
});
const records = computed(() =>
  (project.value[selectedCollection.value] ?? []) as readonly {
    readonly id: string;
    readonly name: string;
  }[]
);
const filteredRecords = computed(() => {
  const search = query.value.trim().toLocaleLowerCase();
  return search
    ? records.value.filter((record) =>
      record.id.toLocaleLowerCase().includes(search) ||
      record.name.toLocaleLowerCase().includes(search)
    )
    : records.value;
});
const pageCount = computed(() =>
  Math.max(1, Math.ceil(filteredRecords.value.length / pageSize))
);
const visibleRecords = computed(() =>
  filteredRecords.value.slice(page.value * pageSize, (page.value + 1) * pageSize)
);
const selectedRecord = computed(() =>
  records.value.find((record) => record.id === selectedId.value)
);
const visibleRecordFields = computed(() =>
  sourceRecordFields(selectedCollection.value).filter((field) =>
    // A campaign's flow, its company and its founding stock are all lists of
    // records with rules of their own; each gets a real editor below the form
    // instead of a box of JSON.
    (selectedCollection.value !== "campaigns" ||
      !["flow", "roster", "startingStore"].includes(field.path[0] ?? "")) &&
    (selectedCollection.value !== "maps" ||
      !["width", "height", "terrain"].includes(field.path[0] ?? "")) &&
    // Lines get a real list editor below the form instead of raw JSON.
    (selectedCollection.value !== "dialogues" || field.path[0] !== "lines")
  )
);

function refresh(message: string) {
  project.value = session.snapshot();
  feedback.value = message;
  emit("change", project.value);
  emit("dirty");
}

/**
 * Persists objective records edited from inside the campaign surface.
 *
 * Objectives are a shared collection, but an author only ever thinks about them
 * while looking at the Stage that uses them, so the campaign editor is allowed
 * to write them back through the same session as any other record.
 */
// The session clones what it is given, and structuredClone cannot copy a Vue
// reactive proxy. Everything that crosses from template state into the session
// must be plain data first; this is that boundary for objectives, and
// selectedRawMap below is the same boundary for the map editor.
function saveObjectives(objectives: readonly SourceRecord<"objectives">[]) {
  const plain = JSON.parse(
    JSON.stringify(objectives)
  ) as SourceRecord<"objectives">[];
  try {
    for (const objective of plain) {
      session.update("objectives", objective.id, (draft) => {
        Object.assign(draft, objective);
      });
    }
    refresh("Saved winning and losing conditions");
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/**
 * States a new way one Stage is won: the record, and the Stage that uses it.
 *
 * **One transaction, because it is one decision.** An author pressing "beat
 * everyone on the other side" did a single thing; the format needs an objective
 * record and a reference to it from the encounter node, which is two writes.
 * Committed separately that would be two presses of undo to get back from, and
 * a throw between them would leave a record nothing uses. Grouped, the author
 * either gets both or gets a sentence and a project nothing touched.
 */
function addWayToWin(
  campaignId: string,
  nodeId: string,
  objective: SourceRecord<"objectives">
) {
  const plain = JSON.parse(
    JSON.stringify(objective)
  ) as SourceRecord<"objectives">;
  const stage = stageAt(toRaw(project.value), campaignId, nodeId);
  try {
    session.transact(
      `Win ${stage?.nodeName ?? nodeId} by ${plain.name.toLocaleLowerCase()}`,
      [
        { kind: "create", collection: "objectives", record: plain },
        {
          kind: "update",
          collection: "campaigns",
          id: campaignId,
          update: (campaign) => {
            const node = campaign.flow?.nodes.find(
              (candidate) => candidate.id === nodeId
            );
            if (!node) return;
            node.objectiveIds = [...(node.objectiveIds ?? []), plain.id];
          }
        }
      ]
    );
    refresh(`${stage?.nodeName ?? nodeId} is won by ${plain.name}.`);
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? `${error.message}. Nothing was added to this game.`
      : String(error);
  }
}

/**
 * Persists dialogue records edited from inside the campaign surface.
 *
 * Same boundary as saveObjectives above: dialogues are a shared collection,
 * but a cutscene is where an author actually thinks about them. The campaign
 * editor may also create new ones inline, so absent records are created rather
 * than refused.
 */
function saveDialogues(dialogues: readonly SourceRecord<"dialogues">[]) {
  const plain = JSON.parse(
    JSON.stringify(dialogues)
  ) as SourceRecord<"dialogues">[];
  try {
    const existing = new Set(
      (toRaw(project.value).dialogues ?? []).map((dialogue) => dialogue.id)
    );
    for (const dialogue of plain) {
      if (existing.has(dialogue.id)) {
        session.update("dialogues", dialogue.id, (draft) => {
          // Assigning cannot drop a field, so removals have to be applied
          // explicitly, and generally rather than one field at a time. The
          // editor hands over whole records, so a key this record does not
          // carry is a key the author cleared: a scene whose lines all went
          // away, or one no longer set against anything. Written as a sweep
          // because the special case for `lines` alone silently kept a
          // backdrop an author had just removed.
          for (const key of Object.keys(draft)) {
            if (!(key in dialogue)) {
              delete draft[key as keyof typeof draft];
            }
          }
          Object.assign(draft, dialogue);
        });
      } else {
        session.create("dialogues", dialogue);
      }
    }
    refresh("Saved scenes");
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/** Saves who the selected scene's speakers are. */
function saveDialogueCast(cast: DialogueCastEntry[]) {
  if (selectedCollection.value !== "dialogues" || !selectedId.value) return;
  const plain = JSON.parse(JSON.stringify(cast)) as DialogueCastEntry[];
  try {
    session.update("dialogues", selectedId.value, (draft) => {
      // An empty cast is the absent field, never an empty array: omitting it
      // is what "drawn the way a speaker was always drawn" means, and an empty
      // array would be a project claiming to have answered.
      if (plain.length > 0) draft.cast = plain;
      else delete draft.cast;
    });
    refresh("Saved who speaks");
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/** Saves the selected dialogue record's lines, edited as a list. */
function saveDialogueLines(lines: DialogueLine[]) {
  if (selectedCollection.value !== "dialogues" || !selectedId.value) return;
  const plain = JSON.parse(JSON.stringify(lines)) as DialogueLine[];
  try {
    session.update("dialogues", selectedId.value, (draft) => {
      if (plain.length > 0) draft.lines = plain;
      else delete draft.lines;
    });
    refresh("Saved scene lines");
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

function selectCollection(collection: SourceCollectionName) {
  if (!leaveEditors()) return;
  selectedCollection.value = collection;
  selectedId.value = "";
  query.value = "";
  page.value = 0;
  feedback.value = "";
  renamePreview.value = [];
}

/**
 * Opens a record for editing. Asking to edit one is asking to see the form,
 * so where the columns are behind a fold this opens it: a roster card that
 * visibly did nothing when pressed would be worse than no card.
 *
 * The wizard is deliberately not a caller. It selects its new character so the
 * roster marks them, and lands the author on the roster with the person they
 * just made on it, not on the form the roster exists to stand in front of.
 */
function selectRecord(id: string) {
  if (id !== selectedId.value && !leaveEditors()) return;
  selectedId.value = id;
  recordsFoldOpen.value = true;
}

/**
 * Every Stage in the game, which is what the Stages section lists.
 *
 * A Stage is an encounter node inside a campaign's flow rather than a record of
 * its own, so there is no collection to page through and no search box: the
 * list is computed from the campaigns and is as long as the game is.
 */
const stages = computed(() => stagesInProject(toRaw(project.value)));

/**
 * Every way a Stage can be won in this game, and the fights each one decides.
 *
 * This is the whole of what Stages shows about objectives, and it is a report
 * rather than an editor: what winning a Stage means is stated on that Stage,
 * beside the board and the people it names, because a target is a placement
 * and a condition read away from the board it is about cannot be checked
 * against anything. What this list is for is the other direction: seeing at a
 * glance how this game's fights end, and finding one that decides nothing.
 *
 * An objective no Stage lists is not a problem the format will report: it
 * compiles, it just never happens. So it is named here, where it can be got
 * rid of.
 */
const waysToWin = computed(() => {
  const raw = toRaw(project.value);
  return (raw.objectives ?? []).map((objective) => ({
    id: objective.id,
    name: objective.name,
    stages: stagesDecidedBy(raw, objective.id)
  }));
});

/**
 * The Stages fought on the map that is open, as a way to reach them.
 *
 * This list is a signpost and nothing else. Who stands where, what is said and
 * what winning means all belong to the Stage and are authored under Stages; a
 * second copy of them here would be a second place they could disagree.
 */
const mapStages = computed(() =>
  selectedCollection.value === "maps" && selectedId.value
    ? stagesOnMap(toRaw(project.value), selectedId.value)
    : []
);

/** Opens a stop where the graph is: its campaign, and the stop itself. */
async function openNodeInFlow(campaignId: string, nodeId: string) {
  if (!leaveEditors()) return;
  const map = selectedCollection.value === "maps" ? selectedId.value : "";
  selectedSection.value = "flow";
  emit("section", "flow");
  flowCampaignId.value = campaignId;
  flowNodeId.value = nodeId;
  // Coming back from the graph should come back to the map, not to the top of
  // Maps, when the map is where the trip started.
  returnToMap.value = map === "" ? undefined : map;
  // The list-and-form under the graph is the keyboard route through the same
  // road, so an arrival lands on the stop in both places rather than in one.
  await nextTick();
  flowEditor.value?.selectNode(nodeId);
}

/**
 * The Stage the Stages section has open, when one is.
 *
 * The identity is kept rather than the record, so a Stage deleted elsewhere
 * closes instead of being edited into a campaign that no longer holds it.
 */
const openStage = ref<{ campaignId: string; nodeId: string } | undefined>();
/** The map to return to after a trip to the graph. */
const returnToMap = ref<string | undefined>();
/** The ground a new Stage would be fought on. */
const newStageMapId = ref("");
/** Which campaign a new Stage joins, when the game has more than one. */
const stageCampaignId = ref("");

/**
 * Which campaign Flow is showing, and which stop on it is in view.
 *
 * A choice the project no longer holds falls back to the first campaign, the
 * same way the Stages section's does and for the same reason: an author who
 * deleted a campaign should not be told there is no campaign 'x'.
 */
const flowCampaignId = ref("");
const flowNodeId = ref("");

const flowCampaign = computed(() => {
  const campaigns = toRaw(project.value).campaigns ?? [];
  return campaigns.find((campaign) => campaign.id === flowCampaignId.value) ??
    campaigns[0];
});

/**
 * Company members no board fields, which is what a project authored before a
 * placement took its member away with it is already carrying.
 */
const unfieldedCompany = computed(() =>
  flowCampaign.value === undefined ? [] : unfieldedMembers(flowCampaign.value)
);

/**
 * Makes sure there is a campaign to show, without asking.
 *
 * **Flow does not open on "create a campaign".** A campaign is the record the
 * format needs in order to hold a road; it is not a decision an author arrives
 * wanting to make. Somebody opening Flow has come to look at the shape of their
 * game, and the honest response to "you have not got one of the records that
 * would hold a shape" is to make it, exactly as putting a bandit on a board
 * makes the character it needs.
 *
 * It is silent because there is nothing to say: the campaign carries the game's
 * own name, holds no road yet, and every part of it stays editable. Several
 * campaigns remain a real thing the format supports and this never gets in
 * their way: it only ever writes when there are none at all.
 */
function ensureCampaign() {
  const raw = toRaw(project.value);
  const campaigns = raw.campaigns ?? [];
  if (campaigns.length > 0) return;
  const name = raw.title.trim() === "" ? "Campaign" : raw.title.trim();
  const id = freeCampaignId(name);
  // The company of one, for the same reason a fresh campaign made anywhere
  // else gets one: a campaign has to march out with somebody, and meeting that
  // as a refusal later is worse than meeting it as a member you can rename.
  const first = raw.unitTypes[0];
  try {
    session.create("campaigns", {
      id,
      name,
      ...(first
        ? { roster: [{ id: "member", name: first.name, unitTypeId: first.id }] }
        : {})
    });
    flowCampaignId.value = id;
    refresh(`${name} is where this game's road is kept.`);
  } catch (error) {
    feedback.value = error instanceof Error ? error.message : String(error);
  }
}

/** An identifier for a campaign that nothing in the project has taken. */
function freeCampaignId(name: string): string {
  const taken = (toRaw(project.value).campaigns ?? []).map(
    (campaign) => campaign.id
  );
  const stem = name
    .toLocaleLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "")
    .replace(/^([^a-z])/, "campaign_$1") || "campaign";
  if (!taken.includes(stem)) return stem;
  let suffix = 2;
  while (taken.includes(`${stem}_${suffix}`)) suffix += 1;
  return `${stem}_${suffix}`;
}

/**
 * A second campaign, because the format holds several and some games want
 * them. It is a deliberate verb on a control of its own, well behind the one
 * campaign an author who has one is looking at. The opposite of a page that
 * opens by asking which record to make.
 */
/** Opens another campaign, committing what is being typed into this one. */
function openCampaign(id: string) {
  if (!leaveEditors()) return;
  flowCampaignId.value = id;
  flowNodeId.value = "";
}

function startAnotherCampaign() {
  if (!leaveEditors()) return;
  const id = freeCampaignId("campaign");
  try {
    session.create("campaigns", { id, name: "Another campaign" });
    flowCampaignId.value = id;
    flowNodeId.value = "";
    refresh("Started another campaign. Give it a name and a first Stage.");
  } catch (error) {
    feedback.value = error instanceof Error ? error.message : String(error);
  }
}

// Landing on Flow directly has to make the campaign too, and a reopened
// project remembers where its author was. The rail's own route goes through
// `selectSection`; this is the other one, and both end in the same call.
onMounted(() => {
  if (activeSection.value.kind === "flow") ensureCampaign();
});

/**
 * Takes a campaign out of the game.
 *
 * Offered only while the game has another one, and that is not squeamishness:
 * arriving at Flow makes a campaign when there are none, so a button that
 * removed the last one would put a fresh empty campaign in its place and read
 * as a button that did nothing. Emptying a campaign out is done by emptying
 * its road, which is a different act with different controls.
 */
function removeCampaign() {
  const campaign = flowCampaign.value;
  if (!campaign || !leaveEditors()) return;
  try {
    session.delete("campaigns", campaign.id);
    flowCampaignId.value = "";
    flowNodeId.value = "";
    refresh(`Removed ${campaign.name}.`);
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/** What this campaign is called, which is what a player reads at the top of it. */
function renameFlowCampaign(name: string) {
  const campaignId = flowCampaign.value?.id;
  if (!campaignId || name.trim() === "") return;
  session.update("campaigns", campaignId, (campaign) => {
    campaign.name = name.trim();
  });
  refresh(`Named it ${name.trim()}`);
}

/** The map a new Stage would use: the author's pick, or the first map. */
const newStageMapChoice = computed(() => {
  const maps = project.value.maps;
  return maps.find((map) => map.id === newStageMapId.value)?.id ?? maps[0]?.id;
});

/**
 * The campaign a new Stage would actually join.
 *
 * A choice the project no longer holds falls back to the first campaign rather
 * than refusing: the author picked a campaign and then deleted it, which is a
 * thing that happens, and answering "there is no campaign 'x'" to the next
 * press would be blaming them for it.
 */
const stageCampaignChoice = computed(() => {
  const campaigns = project.value.campaigns ?? [];
  return campaigns.find((campaign) => campaign.id === stageCampaignId.value)?.id
    ?? campaigns[0]?.id;
});

/**
 * How many Stages the campaign a new one would join already fights on the
 * chosen ground.
 *
 * It counts one campaign rather than the whole project, because that is the
 * campaign the button below writes into: a Stage on this ground in some other
 * campaign is not something the next press would be adding to.
 */
const stagesHereInChosenCampaign = computed(() =>
  stages.value.filter((stage) =>
    stage.mapId === newStageMapChoice.value &&
    stage.campaignId === stageCampaignChoice.value
  ).length
);

const stagesHereSentence = computed(() =>
  stagesHereInChosenCampaign.value === 1
    ? "This ground is fought over once in that campaign."
    : `This ground is fought over ${stagesHereInChosenCampaign.value} times ` +
      "in that campaign."
);

const openStageNode = computed(() => {
  const open = openStage.value;
  if (!open || activeSection.value.kind !== "stages") return undefined;
  return stages.value.some((stage) =>
    stage.campaignId === open.campaignId && stage.nodeId === open.nodeId
  ) ? open : undefined;
});

function selectStage(campaignId: string, nodeId: string) {
  if (!leaveEditors()) return;
  openStage.value = { campaignId, nodeId };
  feedback.value = "";
}

/**
 * Makes a Stage on the chosen ground, and opens it.
 *
 * The four decisions behind this are made by `planStageOnMap`, which writes
 * nothing: find or make a campaign, find or make its flow, append an encounter
 * node carrying this map, and wire it so the flow is still whole. What comes
 * back is either a whole campaign record or a whole flow, so the press is
 * **one** session call: it commits entirely or it throws and the project is
 * exactly as it was. There is no half-made Stage for a failure to leave behind,
 * which is the only honest answer to "what if one of the four fails".
 *
 * It can also come back saying the Stage is already here, in which case nothing
 * is written and the one that exists is opened. That is why the front door can
 * be pressed twice: the second press is the same question, and it gets the same
 * Stage rather than a copy of it. `intent` is how the control that really does
 * mean "one more, on this same ground" says so.
 */
function makeStage(intent: StageIntent = "findOrMake") {
  if (!leaveEditors()) return;
  const mapId = newStageMapChoice.value;
  if (!mapId) {
    feedback.value =
      "There is no ground to fight over yet. Draw a map under Maps, then " +
      "come back and make a Stage on it.";
    return;
  }
  const plan = planStageOnMap(
    toRaw(project.value),
    mapId,
    stageCampaignChoice.value,
    intent
  );
  if (plan.kind === "refused") {
    feedback.value = plan.reason;
    return;
  }
  if (plan.kind === "openExisting") {
    openStage.value = { campaignId: plan.campaignId, nodeId: plan.nodeId };
    stageCampaignId.value = plan.campaignId;
    feedback.value = plan.summary;
    return;
  }
  try {
    if (plan.kind === "createCampaign") {
      session.create("campaigns", plan.campaign);
    } else {
      const flow = plan.flow;
      session.update("campaigns", plan.campaignId, (campaign) => {
        campaign.flow = structuredClone(flow);
      });
    }
    openStage.value = { campaignId: plan.campaignId, nodeId: plan.nodeId };
    stageCampaignId.value = plan.campaignId;
    refresh(plan.summary);
  } catch (error) {
    // The session commits or throws; nothing partial can have landed.
    openStage.value = undefined;
    feedback.value = error instanceof SourceProjectEditError
      ? `${error.message}. Nothing was added to this game.`
      : String(error);
  }
}

/** Stores one changed Stage back into the flow it belongs to. */
function saveStageNode(campaignId: string, node: CampaignNode) {
  const plain = JSON.parse(JSON.stringify(node)) as CampaignNode;
  try {
    let left: readonly string[] = [];
    session.update("campaigns", campaignId, (campaign) => {
      const flow = campaign.flow;
      if (!flow) return;
      // Whom this Stage was fielding before the save, so that anybody it stops
      // fielding can leave the company with their placement. Standing somebody
      // on the author's own side enrols them; this is the other half of that
      // gesture, and without it every arrangement an author tried and cleared
      // left a person behind. `campaign-company.ts` says why it is safe to be
      // this direct.
      const wereFielded = membersFieldedByNode(
        flow.nodes.find((candidate) => candidate.id === plain.id)
      );
      flow.nodes = flow.nodes.map(
        (candidate) => candidate.id === plain.id ? plain : candidate
      ) as typeof flow.nodes;
      left = unenrolMembersNoLongerFielded(campaign, wereFielded);
    });
    // Said out loud, for the same reason enrolling is: a company that quietly
    // shrank is as much a surprise as one that quietly grew.
    refresh(
      left.length === 0
        ? `Saved ${plain.name}`
        : `Saved ${plain.name}. ${left.join(", ")} left the company.`
    );
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/**
 * Puts a character this game has not got onto a Stage's board.
 *
 * The whole of it is **one press and one undo entry**: the weapon type, the
 * weapon, the class, the character, the side's faction, the company member on
 * the player's own side, and the placement itself. `planCharacterOnBoard`
 * decides what to write and writes nothing; `transact` lands the lot or lands
 * none of it. That is the difference between this and the character wizard's
 * Finish, which commits its chain a record at a time and leaves a class and a
 * weapon standing behind a character that never got made.
 */
function castCharacter(
  campaignId: string,
  nodeId: string,
  ask: Omit<CastAsk, "campaignId" | "nodeId">
) {
  const plan = planCharacterOnBoard(toRaw(project.value), {
    ...ask,
    campaignId,
    nodeId
  });
  if (plan.kind === "refused") {
    feedback.value = plan.reason;
    return;
  }
  try {
    session.transact(`Put ${plan.unitTypeName} on the board`, plan.edits);
    refresh(plan.summary);
  } catch (error) {
    // One transaction, so there is nothing half-written to clear up.
    feedback.value = error instanceof SourceProjectEditError
      ? `${error.message}. Nothing was added to this game.`
      : String(error);
  }
}

/**
 * Puts somebody a board enrolled into the campaign's company.
 *
 * A placement on the player's own side fields a member, so stamping a character
 * there means the company gains them. It is written where the company lives,
 * on the campaign record, rather than being invented on the board, and the
 * board says out loud that it happened.
 */
function enrollMember(campaignId: string, member: CampaignRosterMember) {
  const plain = JSON.parse(JSON.stringify(member)) as CampaignRosterMember;
  try {
    session.update("campaigns", campaignId, (campaign) => {
      if ((campaign.roster ?? []).some((entry) => entry.id === plain.id)) return;
      campaign.roster = [...(campaign.roster ?? []), plain];
    });
    refresh(`${plain.name} joined the company.`);
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/**
 * Opens a character from the roster in the ordinary record form.
 *
 * The roster is a way to reach a record and nothing more: it goes through the
 * same two selectors an author's own clicks go through, so it inherits the rule
 * that an editing surface with an uncommittable draft is not abandoned, and
 * what opens is the same form every other record opens in.
 */
function selectCharacter(id: string) {
  // Read through a function so the compiler does not narrow the collection
  // across the selector that deliberately changes it.
  const on = () => selectedCollection.value === "unitTypes";
  if (!on()) {
    selectCollection("unitTypes");
    if (!on()) return;
  }
  selectRecord(id);
}

/**
 * Takes the author to the record a reported problem is about.
 *
 * It travels through the same three selectors an author's own clicks go
 * through, so it inherits the rule that an editing surface with an
 * uncommittable draft is not abandoned. A location it cannot fully resolve
 * still moves as far as it can and says what it could not find: this button
 * promises to take you somewhere, and doing nothing quietly is the one outcome
 * it may not have.
 */
function goToDiagnostic(diagnostic: PresentedDiagnostic) {
  const target = diagnosticTarget(diagnostic.instancePath);
  if (!leaveEditors()) return;
  if (target.collection) selectCollection(target.collection);
  selectedSection.value = target.sectionId;
  emit("section", target.sectionId);
  if (!target.collection) {
    feedback.value = `${diagnostic.message}. ${target.unresolved}`;
    return;
  }
  const found = target.unresolved
    ? undefined
    : ((project.value[target.collection] ?? []) as readonly {
      readonly id: string;
      readonly name: string;
    }[])[target.index!];
  if (!found) {
    feedback.value = `${diagnostic.message}. ${
      target.unresolved ??
      "The record it names is no longer in this project; validate again."}`;
    return;
  }
  selectedId.value = found.id;
  // The jump promised to take the author to the record; a fold between them
  // and it would be the quiet nothing this button may not do.
  recordsFoldOpen.value = true;
  // An objective is not a record an author browses: it is what winning one
  // fight means, and it is read and written on that fight's win conditions. So
  // a problem about one opens the Stage it decides rather than leaving the
  // author on a page with no objective anywhere on it.
  if (target.collection === "objectives") {
    const decided = stagesDecidedBy(toRaw(project.value), found.id)[0];
    if (decided) {
      openStage.value = {
        campaignId: decided.campaignId,
        nodeId: decided.nodeId
      };
      feedback.value =
        `${decided.nodeName} is won by ${found.name}: ${diagnostic.message}`;
      return;
    }
    openStage.value = undefined;
    feedback.value =
      `${found.name}: ${diagnostic.message}. No Stage is decided by it yet, ` +
      "so it is listed below with the other ways a Stage can be won.";
    return;
  }
  feedback.value = `${found.name}: ${diagnostic.message}`;
}

function uniqueId(prefix: string): string {
  const ids = new Set(records.value.map((record) => record.id));
  if (!ids.has(prefix)) return prefix;
  let suffix = 2;
  while (ids.has(`${prefix}_${suffix}`)) suffix += 1;
  return `${prefix}_${suffix}`;
}

/**
 * The identifier a freshly made record is filed under, from what the record is
 * called.
 *
 * From the name and not from the collection. A collection name is the
 * format's word for the drawer and it is plural, so building an identifier out
 * of it files a record called "New Map" under `new_maps`: a name and an
 * identifier that disagree about how many of something this is, which reads as
 * a typo the author has to go and find.
 *
 * It is derived once, here. Renaming afterwards is the rename tool's job: a
 * derivation that kept chasing the name would move an identifier that other
 * records point at.
 */
function idForNewRecord(name: string, collection: SourceCollectionName): string {
  // The fallback stem is the author's word for one of these, with the space
  // taken out of the two that have one. A stem is an identifier too, and
  // "weapon type" is not one.
  const stem = collectionWord[collection].replace(/\W+/g, "_");
  return uniqueId(identifierFromName(name, stem));
}

function defaultRecord(collection: SourceCollectionName): SourceRecord<typeof collection> {
  const named = <R extends { readonly name: string }>(record: R) =>
    ({ ...record, id: idForNewRecord(record.name, collection) });
  switch (collection) {
    case "classes":
      return named({
        name: "New Class",
        baseStats: { health: 1, movement: 0, strength: 0, defense: 0 }
      });
    case "unitTypes":
      return named({
        name: "New Character",
        classId: project.value.classes[0]?.id ?? ""
      });
    case "weaponTypes":
      return named({ name: "New Weapon Type" });
    case "weapons":
      return named({
        name: "New Weapon",
        ...(project.value.weaponTypes?.[0]
          ? { weaponTypeId: project.value.weaponTypes[0].id }
          : {}),
        power: 0,
        range: 1
      });
    case "itemTypes":
      return named({ name: "New Item Type" });
    case "items":
      return named({
        name: "New Item",
        ...(project.value.itemTypes?.[0]
          ? { itemTypeId: project.value.itemTypes[0].id }
          : {}),
        stackLimit: 1
      });
    case "maps":
      // A 1×1 board is not a map anyone wants; start at a small but real
      // battlefield instead of making the author grow it cell by cell.
      return named({
        name: "New Map",
        width: 8,
        height: 6,
        terrain: Array.from({ length: 48 }, () => "plain")
      });
    case "factions":
      return named({ name: "New Faction" });
    case "abilities":
      // The schema requires these, so a fresh ability starts valid: a
      // one-tile physical strike rather than an unfillable form.
      return named({
        name: "New Ability",
        kind: "damage",
        power: 1,
        minimumRange: 1,
        maximumRange: 1
      });
    case "objectives":
      return named({ name: "New Objective" });
    case "campaigns": {
      // A campaign has to march out with somebody, so a fresh one starts with
      // a company of one rather than with a rule the author meets later as a
      // refusal. With no characters in the project yet there is nobody to be,
      // and the company editor says so instead of inventing a member.
      const first = project.value.unitTypes[0];
      return named({
        name: "New Campaign",
        ...(first
          ? { roster: [{ id: "member", name: first.name, unitTypeId: first.id }] }
          : {})
      });
    }
    case "dialogues":
      return named({ name: "New Scene" });
  }
}

function createRecord() {
  if (!leaveEditors()) return;
  try {
    const record = defaultRecord(selectedCollection.value);
    session.create(selectedCollection.value, record);
    selectedId.value = record.id;
    refresh(`Created ${record.name}`);
  } catch (error) {
    feedback.value = error instanceof Error ? error.message : String(error);
  }
}

const categoryCollections: Record<string, SourceCollectionName> = {
  class: "classes",
  unit_type: "unitTypes",
  weapon_type: "weaponTypes",
  item_type: "itemTypes",
  weapon: "weapons",
  item: "items",
  faction: "factions",
  ability: "abilities",
  objective: "objectives",
  dialogue: "dialogues"
};

/**
 * Makes a record of another collection without leaving this one: the button a
 * reference field offers when there is nothing for it to point at.
 *
 * The collection is switched and switched back because `defaultRecord` and the
 * session both read it, and the author must not be moved off the record they
 * were editing. That makes the restore load-bearing: it happens whatever the
 * create does, because a throw that left the workspace showing some other
 * collection would strand an author with no way back and no sentence saying
 * what happened.
 */
function createRelated(category: string) {
  const collection = categoryCollections[category];
  if (!collection) return;
  if (!leaveEditors()) return;
  const previousCollection = selectedCollection.value;
  selectedCollection.value = collection;
  try {
    const record = defaultRecord(collection);
    session.create(collection, record);
    refresh(`Created related ${collectionWord[collection]} '${record.id}'`);
  } catch (error) {
    feedback.value = error instanceof Error ? error.message : String(error);
  } finally {
    selectedCollection.value = previousCollection;
  }
}

function saveRecord(record: Record<string, unknown>) {
  if (!selectedId.value) return;
  try {
    session.update(
      selectedCollection.value,
      selectedId.value,
      (draft) => {
        // The form submits the whole record, so a field it dropped (an
        // unchecked optional boolean, a cleared text field) must be removed;
        // assignment alone can never delete.
        for (const key of Object.keys(draft)) {
          if (!(key in record)) {
            delete (draft as unknown as Record<string, unknown>)[key];
          }
        }
        Object.assign(draft, record);
      }
    );
    refresh(`Saved ${record.name ?? selectedId.value}`);
  } catch (error) {
    feedback.value = error instanceof Error ? error.message : String(error);
  }
}

const rosterHelp =
  "Each member is one person the campaign keeps between Stages, wounds and " +
  "experience and all. Anybody who joins later is written on the node they " +
  "join at.";

/**
 * Identities this campaign already spends on somebody who joins later.
 *
 * A member's identity is unique across the whole campaign rather than across
 * the founding company alone, so the same clash the flow editor reports when a
 * recruit takes a founder's identity is reported here when a founder takes a
 * recruit's. The two lists are edited on different screens and are one
 * namespace.
 */
const recruitedIds = computed<readonly string[]>(() =>
  (flowCampaign.value?.flow?.nodes ?? []).flatMap((node) =>
    (node.recruits ?? []).map((member) => member.id)
  )
);

/**
 * Persists the company a campaign starts with, edited as a list beside the
 * campaign's own fields.
 *
 * The same boundary as the dialogue lines above: the list editor hands back
 * plain data, which the session clones, and a company that has emptied out is
 * removed rather than saved as an empty list, because an absent roster and an
 * empty one say different things about a campaign.
 */
function saveCampaignRoster(members: CampaignRosterMember[]) {
  const campaignId = flowCampaign.value?.id;
  if (!campaignId) return;
  const plain = JSON.parse(JSON.stringify(members)) as CampaignRosterMember[];
  try {
    session.update("campaigns", campaignId, (campaign) => {
      if (plain.length > 0) campaign.roster = plain;
      else delete campaign.roster;
    });
    refresh("Saved the campaign's company");
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

const startingStoreHelp =
  "Owned by the company rather than by anybody in it. Say how many of each " +
  "once.";

/**
 * Persists the stock a campaign is founded with, edited as a list beside the
 * campaign's own fields.
 *
 * The same boundary as the company above: the list editor hands back plain
 * data, which the session clones, and a store that has emptied out is removed
 * rather than saved as an empty list. Omitted and empty do mean the same thing
 * here, since there is no third state for a list of things a company owns, but
 * the record is still written the way every other campaign writes it.
 */
function saveCampaignStartingStore(grants: CampaignItemGrant[]) {
  const campaignId = flowCampaign.value?.id;
  if (!campaignId) return;
  const plain = JSON.parse(JSON.stringify(grants)) as CampaignItemGrant[];
  try {
    session.update("campaigns", campaignId, (campaign) => {
      if (plain.length > 0) campaign.startingStore = plain;
      else delete campaign.startingStore;
    });
    refresh("Saved the campaign's starting store");
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

/** The whole road, from the form under the graph. */
function saveCampaignFlow(flow: CampaignFlow) {
  const campaignId = flowCampaign.value?.id;
  if (!campaignId) return;
  session.update("campaigns", campaignId, (campaign) => {
    campaign.flow = flow;
  });
  refresh("Saved the order of events");
}

/**
 * The road, changed by one gesture on the graph.
 *
 * **The gesture is applied here rather than in the picture**, against the road
 * as the project holds it at this instant. The same road is also edited by the
 * list-and-form under the picture, and that form works on a draft; a graph that
 * handed over a whole flow computed from the copy it was drawn with would throw
 * away everything typed below since it was drawn. So the form is committed
 * first, and the gesture lands on the result.
 *
 * Joining two stops is **one thing the author did**, so it is one transaction
 * and one undo entry, labelled with the sentence the act itself is: "Send The
 * Crossing on to The Watch on the Road" rather than "Edit campaign". An undo
 * list an author can read is an undo list they will use.
 */
function applyGraphGesture(gesture: FlowGesture) {
  const campaignId = flowCampaign.value?.id;
  if (!campaignId || !leaveEditors()) return;
  const result = applyFlowGesture(flowCampaign.value?.flow, gesture);
  if (result.kind === "refused") {
    feedback.value = result.reason;
    return;
  }
  const plain = JSON.parse(JSON.stringify(result.flow)) as CampaignFlow;
  try {
    session.transact(result.summary, [{
      kind: "update",
      collection: "campaigns",
      id: campaignId,
      update: (campaign) => { campaign.flow = plain; }
    }]);
    refresh(result.summary);
  } catch (error) {
    feedback.value = error instanceof SourceProjectEditError
      ? error.message
      : String(error);
  }
}

function saveMap(map: SourceMap) {
  if (selectedCollection.value !== "maps" || !selectedId.value) return;
  session.update("maps", selectedId.value, (draft) => {
    draft.width = map.width;
    draft.height = map.height;
    draft.terrain = [...map.terrain];
  });
  refresh("Saved terrain map");
}

/**
 * The optional project-level fields the game settings page renders controls
 * for, taken from the page's own field list so the two cannot drift.
 *
 * A form submits a copy of the project with the fields it cleared *deleted*
 * rather than set to undefined, so "absent from this submission" is ambiguous
 * on its own: it means either "cleared here" or "not this form's business".
 * Naming the page's own fields is what tells the two apart, and it is why
 * clearing a season or a turn order on the settings page stores the clearing
 * instead of quietly leaving the previous value in place.
 */
const gameSettingsOwnedFields = new Set(
  sourceGameSettingsFields().map((field) => field.path[0] ?? "")
);

/**
 * Every project-level field a form on either page may write, and whether the
 * format requires it. Derived from the schema rather than listed here, because
 * a list written here would be one more thing that has to agree with the type
 * union, the runtime allow-list and the page's own fields. The failure
 * when one of them misses a field is silent: the page draws the control, the
 * form submits it, the pick list drops it, and the author is told "Saved game
 * settings" over a value that was never stored.
 */
const editableProjectFields = sourceEditableProjectFields();

/**
 * Commits either project-level form: the metadata one, which holds identity and
 * the author's own fields, or the game settings page. Both submit a copy of the
 * project, so a field is taken from a submission that carries it, and a field
 * the submitting form owns and did not carry is one the author cleared. A form
 * can never clear a field it does not own.
 */
function saveMetadata(
  record: Record<string, unknown>,
  message = "Saved project metadata",
  owned: ReadonlySet<string> = new Set<string>()
) {
  const changes: Record<string, unknown> = {};
  for (const field of editableProjectFields) {
    const name = field.path[0]!;
    if (name in record) {
      // An empty choice is the absent one, for every optional control: a
      // project that names no season is drawn in the default theme and one
      // that names no turn order orders its Stages the way they ran before
      // the setting existed. The default is never written into the project by
      // opening a page. A required field is passed through as written, so that
      // what is wrong with it is judged by the format rather than guessed at
      // here.
      const value = record[name];
      changes[name] = !field.required && (value === undefined || value === "")
        ? undefined
        : value;
    } else if (owned.has(name)) {
      changes[name] = undefined;
    }
  }
  session.updateMetadata(
    changes as Parameters<SourceProjectSession["updateMetadata"]>[0]
  );
  refresh(message);
}

/**
 * The game's settings, with the name the file carries kept level with the name
 * a player reads.
 *
 * The rule itself is `gameIdFollowingTitle`; this is only where the before and
 * the after meet. It runs on every settings save rather than on a title change
 * alone, because "did the title change" and "was this id following it" are the
 * same question asked once.
 */
function saveGameSettings(record: Record<string, unknown>) {
  const stored = toRaw(project.value);
  const gameId = gameIdFollowingTitle(
    { title: stored.title, gameId: stored.gameId },
    {
      title: typeof record.title === "string" ? record.title : stored.title,
      gameId: typeof record.gameId === "string" ? record.gameId : stored.gameId
    }
  );
  saveMetadata(
    { ...record, gameId },
    "Saved game settings",
    gameSettingsOwnedFields
  );
}

/**
 * Names the records behind reference paths like `/unitTypes/3/classId`, so a
 * refusal can say who still uses the record rather than quoting JSON pointers.
 * A path that names no known record falls back to itself rather than hiding.
 */
function describeReferences(paths: readonly string[]): string[] {
  const named = new Set<string>();
  const source = toRaw(project.value);
  for (const path of paths) {
    const [, collection, index] = path.split("/");
    const word = collectionWord[collection as SourceCollectionName];
    const record = word
      ? (source[collection as SourceCollectionName] ?? [])[Number(index)]
      : undefined;
    named.add(record?.name ? `${record.name} (the ${word})` : path);
  }
  return [...named];
}

/**
 * Gets rid of a way to win that decides nothing.
 *
 * Only offered for an objective no Stage lists, so this can never be the act
 * that breaks a fight: the session refuses a delete anything still refers to,
 * and here there is nothing to refer to it.
 */
function removeUnusedObjective(id: string, name: string) {
  try {
    session.delete("objectives", id);
    refresh(`Removed ${name}, which decided no Stage.`);
  } catch (error) {
    feedback.value = error instanceof Error ? error.message : String(error);
  }
}

function deleteRecord() {
  if (!selectedId.value) return;
  try {
    const deleted = selectedRecord.value?.name ?? selectedId.value;
    session.delete(selectedCollection.value, selectedId.value);
    selectedId.value = "";
    refresh(`Deleted ${deleted}`);
  } catch (error) {
    if (
      error instanceof SourceProjectEditError &&
      error.code === "DELETE_REFERENCED"
    ) {
      const users = describeReferences(error.affectedPaths);
      const name = selectedRecord.value?.name ?? selectedId.value;
      feedback.value =
        `${users.join(" and ")} still ${users.length === 1 ? "uses" : "use"} ` +
        `${name}. Point ${users.length === 1 ? "it" : "them"} somewhere else, ` +
        `or remove ${users.length === 1 ? "it" : "them"}, and then delete ` +
        `${name}.`;
    } else {
      feedback.value = error instanceof Error ? error.message : String(error);
    }
  }
}

/**
 * The rule a stable identifier is judged by, taken from the schema that judges
 * it rather than written out again here.
 *
 * A rename rewrites every reference to a record across the whole project, so an
 * identifier the format cannot hold is not one bad field: it is a game that
 * will not open, spread over a dozen records. The control below carries this as
 * a `pattern` attribute, but the attribute is decoration: the input is not in
 * a form and the button beside it is not a submit button, so nothing in the
 * browser ever compiles it. The refusal has to be here.
 */
const idRule = computed(() =>
  sourceRecordFields(selectedCollection.value)
    .find((field) => field.path.join(".") === "id")?.pattern
);
const idPattern = computed(() => htmlPattern(idRule.value));

/** Why this identifier cannot be used, or nothing. */
function idRefusal(candidate: string): string | undefined {
  const rule = idRule.value;
  if (rule === undefined || new RegExp(rule, "u").test(candidate)) {
    return undefined;
  }
  return `'${candidate}' is not an identifier this format can hold. ` +
    "Identifiers use lowercase letters, digits and separators (. _ -) and " +
    "start with a letter, like 'iron_knight'. Nothing was renamed.";
}

function previewRename() {
  if (!selectedId.value || !renameId.value) return;
  const refusal = idRefusal(renameId.value);
  if (refusal) {
    renamePreview.value = [];
    feedback.value = refusal;
    return;
  }
  try {
    renamePreview.value = session.previewRename(
      selectedCollection.value,
      selectedId.value,
      renameId.value
    );
    feedback.value =
      `Rename will update ${renamePreview.value.length} source locations.`;
  } catch (error) {
    renamePreview.value = [];
    feedback.value = error instanceof Error ? error.message : String(error);
  }
}

function confirmRename() {
  if (!selectedId.value || !renameId.value || renamePreview.value.length === 0) {
    return;
  }
  // Asked again rather than trusted from the preview: the field stays editable
  // while the preview list is on screen, so the identifier being confirmed is
  // not necessarily the one that was previewed.
  const refusal = idRefusal(renameId.value);
  if (refusal) {
    renamePreview.value = [];
    feedback.value = refusal;
    return;
  }
  // Renaming remounts the record form, so its pending edits must land first.
  if (!leaveEditors()) return;
  const previousId = selectedId.value;
  session.rename(selectedCollection.value, previousId, renameId.value);
  selectedId.value = renameId.value;
  renameId.value = "";
  renamePreview.value = [];
  refresh(`Renamed ${previousId} to ${selectedId.value}`);
}

function undo() {
  const transaction = session.undo();
  if (transaction) refresh(`Undid: ${transaction.label}`);
}

function redo() {
  const transaction = session.redo();
  if (transaction) refresh(`Redid: ${transaction.label}`);
}

function basicChoices(
  values: readonly { readonly id: string; readonly name: string }[] | undefined
): readonly ReferenceChoice[] {
  return (values ?? []).map(({ id, name }) => ({ id, name }));
}

const referenceChoices = computed<Readonly<Record<string, readonly ReferenceChoice[]>>>(() => {
  const choices: Record<string, readonly ReferenceChoice[]> = {
    class: basicChoices(project.value.classes),
    unit_type: basicChoices(project.value.unitTypes),
    weapon_type: basicChoices(project.value.weaponTypes),
    item_type: basicChoices(project.value.itemTypes),
    item: basicChoices(project.value.items),
    weapon: basicChoices(project.value.weapons),
    faction: basicChoices(project.value.factions),
    ability: basicChoices(project.value.abilities),
    objective: basicChoices(project.value.objectives),
    dialogue: basicChoices(project.value.dialogues)
  };
  if (selectedCollection.value !== "unitTypes") return choices;
  const unit = project.value.unitTypes.find((candidate) =>
    candidate.id === selectedId.value
  );
  const sourceClass = project.value.classes.find((candidate) =>
    candidate.id === unit?.classId
  );
  choices.weapon = project.value.weapons.map((weapon) => {
    if (!weapon.weaponTypeId) {
      return {
        id: weapon.id,
        name: weapon.name,
        compatibility: "unknown",
        explanation: "Weapon type is not assigned, so compatibility is unknown."
      };
    }
    if (sourceClass?.allowedWeaponTypeIds === undefined) {
      return { id: weapon.id, name: weapon.name, compatibility: "compatible" };
    }
    const compatible = sourceClass.allowedWeaponTypeIds.includes(weapon.weaponTypeId);
    return {
      id: weapon.id,
      name: weapon.name,
      compatibility: compatible ? "compatible" : "incompatible",
      ...(compatible ? {} : {
        explanation: `${sourceClass.name} does not permit weapon type '${weapon.weaponTypeId}'.`
      })
    };
  });
  return choices;
});
</script>

<template>
  <!-- Flow is drawn wider than the rest. Every other page here is a column of
       fields, which a reading measure suits; a graph is a picture of a whole
       campaign, and a picture squeezed into a text column is not one. -->
  <section id="content" aria-labelledby="content-title"
    :class="{
      'content-wide':
        activeSection.kind === 'flow' || activeSection.kind === 'stages'
    }">
    <h2 id="content-title">{{ activeSection.label }}</h2>
    <p class="section-hint">{{ activeSection.hint }}</p>
    <div class="history-commands" role="group" aria-label="Edit history">
      <button type="button" :disabled="!session.canUndo()" @click="undo">Undo</button>
      <button type="button" :disabled="!session.canRedo()" @click="redo">Redo</button>
    </div>

    <GameSettings v-if="activeSection.kind === 'settings'" ref="settingsPage"
      :project="project"
      @dirty="emit('dirty')"
      @submit="saveGameSettings" />

    <CharacterWizard v-if="activeSection.id === 'characters' && makingCharacter"
      :project="project"
      @cancel="cancelCharacter"
      @create="makeCharacter" />
    <CharacterRoster v-else-if="activeSection.id === 'characters'" ref="roster"
      :project="project"
      :selected-id="selectedCollection === 'unitTypes' ? selectedId : ''"
      @create="makingCharacter = true"
      @select="selectCharacter" />

    <section
      v-if="activeSection.id === 'characters' && selectedCollection === 'abilities'"
      class="shelf-panel" aria-labelledby="add-ability-title">
      <h3 id="add-ability-title">Add an ability</h3>
      <p class="field-help">
        Give it to somebody afterwards, under their abilities. An area covers
        everyone standing in it, but one that does damage only ever hurts the
        other side.
      </p>

      <nav aria-label="Kinds of game for abilities" class="shelf-settings">
        <button v-for="setting in CATALOGUE_SETTINGS" :key="setting.id"
          type="button"
          :aria-current="abilitySetting === setting.id ? 'page' : undefined"
          @click="selectAbilitySetting(setting.id)">
          {{ setting.label }}
        </button>
      </nav>
      <div ref="abilityShelfRoot" class="shelf" role="radiogroup"
        aria-label="Kind of ability"
        @keydown.left.prevent="moveAbilityFocus(-1)"
        @keydown.up.prevent="moveAbilityFocus(-1)"
        @keydown.right.prevent="moveAbilityFocus(1)"
        @keydown.down.prevent="moveAbilityFocus(1)">
        <button v-for="recipe in abilityShelf" :key="recipe.id" type="button"
          class="shelf-card" role="radio" :data-recipe="recipe.id"
          :aria-checked="selectedAbility.id === recipe.id ? 'true' : 'false'"
          :tabindex="selectedAbility.id === recipe.id ? 0 : -1"
          @click="selectedAbilityId = recipe.id">
          <strong>{{ recipe.label }}</strong>
          <span>{{ recipe.summary }}</span>
        </button>
      </div>

      <div class="shelf-row">
        <label for="new-ability-name">Name</label>
        <input id="new-ability-name" v-model.trim="newAbilityName"
          :placeholder="selectedAbility.label">
        <button type="button" class="shelf-go" @click="addAbility">
          Add it
        </button>
      </div>
      <p class="field-help" role="status">
        {{ selectedAbility.label }}: {{ selectedAbility.summary }}
      </p>
    </section>

    <section
      v-if="activeSection.id === 'equipment' && selectedCollection === 'weapons'"
      class="shelf-panel" aria-labelledby="add-weapon-title">
      <h3 id="add-weapon-title">Add a weapon</h3>
      <p class="field-help">
        Makes the weapon and its weapon type. A character can carry more than
        one.
      </p>

      <nav aria-label="Kinds of game for weapons" class="shelf-settings">
        <button v-for="setting in CATALOGUE_SETTINGS" :key="setting.id"
          type="button"
          :aria-current="weaponSetting === setting.id ? 'page' : undefined"
          @click="selectWeaponSetting(setting.id)">
          {{ setting.label }}
        </button>
      </nav>
      <p class="field-help">
        Anyone whose class allows the weapon type can carry one.
      </p>
      <div ref="weaponShelfRoot" class="shelf" role="radiogroup"
        aria-label="Kind of weapon"
        @keydown.left.prevent="moveWeaponFocus(-1)"
        @keydown.up.prevent="moveWeaponFocus(-1)"
        @keydown.right.prevent="moveWeaponFocus(1)"
        @keydown.down.prevent="moveWeaponFocus(1)">
        <button v-for="recipe in weaponShelf" :key="recipe.id" type="button"
          class="shelf-card" role="radio" :data-recipe="recipe.id"
          :aria-checked="selectedWeapon.id === recipe.id ? 'true' : 'false'"
          :tabindex="selectedWeapon.id === recipe.id ? 0 : -1"
          @click="selectedWeaponId = recipe.id">
          <strong>{{ recipe.label }}</strong>
          <em>Weapon type: {{ recipe.weaponTypeName }}</em>
          <span>{{ recipe.summary }}</span>
        </button>
      </div>

      <div class="shelf-row">
        <label for="new-weapon-name">Name</label>
        <input id="new-weapon-name" v-model.trim="newWeaponName"
          :placeholder="selectedWeapon.label">
        <button type="button" class="shelf-go" @click="addWeapon">
          Add it
        </button>
      </div>
      <p class="field-help" role="status">
        {{ selectedWeapon.label }}: weapon type
        {{ selectedWeapon.weaponTypeName }}. {{ selectedWeapon.summary }}
        Carried by the {{ selectedWeapon.carriedBy }}.
      </p>
    </section>

    <!-- The problem list is a place rather than a footnote, so a problem can
         be jumped from: the jump moves this workspace to the record, which is
         only possible because the panel and the record columns are siblings
         inside the one surface that owns the selection. -->
    <DiagnosticPanel v-if="activeSection.kind === 'diagnostics'"
      heading="What validation found"
      :diagnostics="diagnostics ?? []" :target-notes="targetNotes ?? []"
      @navigate="goToDiagnostic" />

    <!-- The record columns, behind a door where something friendlier already
         leads the page. `recordsBehindFold` says which sections those are and
         why; here the fold is only drawn. -->
    <component v-if="activeSection.kind === 'collections'"
      :is="recordsBehindFold ? 'details' : 'div'"
      :class="recordsBehindFold ? 'records-fold' : undefined"
      :open="recordsBehindFold && recordsFoldOpen ? true : undefined"
      @toggle="recordsBehindFold &&
        (recordsFoldOpen = ($event.target as HTMLDetailsElement).open)">
      <summary v-if="recordsBehindFold">Advanced</summary>
      <p v-if="recordsBehindFold" class="field-help">
        Every character as the file stores one, and the classes, factions and
        abilities they are built out of. This is where a game with forty
        characters searches them, and where a character made by the wizard is
        changed afterwards.
      </p>
    <div class="content-layout"
      :class="{ 'one-collection': collections.length < 2 }">
      <!-- A navigation between one thing is not a navigation. Maps owns a
           single collection, and drawing the strip for it put a full-height
           column on the page whose entire content was the word "Maps". -->
      <nav v-if="collections.length > 1" aria-label="Content categories"
        class="content-categories">
        <button v-for="collection in collections" :key="collection.id" type="button"
          :aria-current="selectedCollection === collection.id ? 'page' : undefined"
          @click="selectCollection(collection.id)">
          {{ collection.label }}
          <span>{{ (project[collection.id] ?? []).length }}</span>
        </button>
      </nav>

      <div class="record-list">
        <label for="record-search">Search {{ selectedCollectionLabel }}</label>
        <input id="record-search" v-model="query" type="search" @input="page = 0">
        <button type="button" @click="createRecord">
          Create {{ collectionWord[selectedCollection] }}
        </button>
        <p aria-live="polite">
          {{ filteredRecords.length }} matching
          {{ filteredRecords.length === 1 ? "record" : "records" }}
        </p>
        <ul>
          <li v-for="record in visibleRecords" :key="record.id">
            <button type="button"
              :aria-current="selectedId === record.id ? 'true' : undefined"
              @click="selectRecord(record.id)">
              <strong>{{ record.name }}</strong>
              <small>{{ record.id }}</small>
            </button>
          </li>
        </ul>
        <div v-if="pageCount > 1" class="pagination" role="group"
          aria-label="Record pages">
          <button type="button" :disabled="page === 0" @click="page -= 1">
            Previous
          </button>
          <span>Page {{ page + 1 }} of {{ pageCount }}</span>
          <button type="button" :disabled="page + 1 >= pageCount" @click="page += 1">
            Next
          </button>
        </div>
      </div>

      <div class="record-editor">
        <SchemaRecordForm v-if="selectedRecord" ref="recordForm"
          :key="`${selectedCollection}:${selectedRecord.id}`"
          :heading="selectedRecord.name"
          :fields="visibleRecordFields"
          :model-value="selectedRecord"
          :reference-choices="referenceChoices"
          :readonly-paths="['id']"
          @create-related="createRelated"
          @dirty="emit('dirty')"
          @submit="saveRecord" />
        <p v-if="selectedCollection === 'unitTypes' && selectedRecord"
          class="signpost">
          A character fights when a Stage places them.
          <button type="button" class="secondary" @click="selectSection('stages')">
            Go to Stages
          </button>
        </p>
        <MapEditor
          v-if="selectedCollection === 'maps' && selectedRecord"
          :map="selectedRawMap!"
          :theme-id="project.themeId"
          @save="saveMap" />
        <!-- What this ground is used for, as a way to get there and nothing
             more. A map holds no units and knows no Stages: the tie runs the
             other way, from an encounter node's `mapId`. Setting one up
             is the Stages section's whole job. -->
        <section v-if="selectedCollection === 'maps' && selectedRecord"
          class="map-stages" aria-labelledby="map-stages-title">
          <h4 id="map-stages-title">Stages fought here</h4>
          <p v-if="mapStages.length === 0" class="field-help">
            No Stage uses this ground yet.
          </p>
          <ul v-else class="map-stage-list">
            <li v-for="stage in mapStages"
              :key="`${stage.campaignId}/${stage.nodeId}`">
              <strong>{{ stage.nodeName }}</strong>
              <small>in {{ stage.campaignName }}</small>
              <span>
                {{ stage.yours }} of yours against {{ stage.theirs }}.
                {{ sceneSentence(stage.saidBefore, "before") }}
                {{ sceneSentence(stage.saidAfter, "after") }}
                <template v-if="stage.winning.length">
                  Winning means {{ stage.winning.join(", ") }}.
                </template>
              </span>
              <button type="button" class="secondary"
                @click="openStageIn(stage.campaignId, stage.nodeId)">
                Open it under Stages
              </button>
            </li>
          </ul>
          <p class="signpost">
            <button type="button" class="secondary"
              @click="startStageOnOpenMap">
              Make a Stage on this ground
            </button>
          </p>
        </section>
        <section
          v-if="selectedCollection === 'dialogues' && selectedRecord"
          class="dialogue-record-lines" aria-labelledby="dialogue-lines-title">
          <h4 id="dialogue-lines-title">What is said</h4>
          <p class="field-help">
            Lines play top to bottom. Changes here save immediately.
          </p>
          <DialogueLinesEditor ref="sceneLines" id-prefix="dialogue"
            :lines="selectedRawDialogue?.lines ?? []"
            :cast-speakers="(selectedRawDialogue?.cast ?? [])
              .map((entry) => entry.speaker)"
            @update="saveDialogueLines" @dirty="emit('dirty')" />
          <h4 id="dialogue-cast-title">Who is speaking</h4>
          <p class="field-help">
            Name the character behind a speaker and their face is drawn.
          </p>
          <DialogueCastEditor id-prefix="dialogue"
            :cast="selectedRawDialogue?.cast ?? []"
            :speakers="selectedDialogueSpeakers"
            :unit-types="project.unitTypes ?? []"
            @update="saveDialogueCast" />
          <DialoguePreview :lines="selectedRawDialogue?.lines ?? []"
            :backgrounds="selectedDialogueBackgrounds"
            :cast="selectedRawDialogue?.cast ?? []"
            :project="project" />
        </section>
        <details v-if="selectedRecord" class="rename-record">
          <summary>Rename stable identifier</summary>
          <label for="rename-id">New identifier</label>
          <!-- The pattern is the schema's own, and it is here for the browser's
               inline hint alone: what actually refuses a bad identifier is
               `idRefusal`, because this input is in no form and neither button
               below submits one. -->
          <input id="rename-id" v-model.trim="renameId" :pattern="idPattern">
          <button type="button" @click="previewRename">Preview rename</button>
          <div v-if="renamePreview.length">
            <p>The following source locations will change:</p>
            <ul>
              <li v-for="path in renamePreview" :key="path"><code>{{ path }}</code></li>
            </ul>
            <button type="button" @click="confirmRename">Confirm atomic rename</button>
          </div>
        </details>
        <p v-else>Select or create a record to edit it.</p>
        <button v-if="selectedRecord" type="button" class="danger"
          @click="deleteRecord">
          Delete {{ selectedRecord.name }}
        </button>
      </div>
    </div>
    </component>
    <!-- Stages. A Stage is a node inside a campaign's flow rather than a
         record, so this draws two columns of its own rather than the record
         ones: the fights this game has on the left, and on the right the one
         that is open, or the one being made. The objectives this section is
         the home of are not a third column either; what winning means is
         stated on the fight it decides, and reported under it. -->
    <div v-if="activeSection.kind === 'stages'"
      class="content-layout one-collection">
      <div class="record-list">
        <button type="button" class="make-stage" @click="openStage = undefined">
          New Stage
        </button>
        <p aria-live="polite">
          {{ stages.length === 0
            ? "No Stages yet. Set the first one up beside this list."
            : `${stages.length} ${stages.length === 1 ? "Stage" : "Stages"} in this game` }}
        </p>
        <ul>
          <li v-for="stage in stages" :key="`${stage.campaignId}/${stage.nodeId}`">
            <button type="button"
              :aria-current="openStageNode &&
                openStageNode.campaignId === stage.campaignId &&
                openStageNode.nodeId === stage.nodeId ? 'true' : undefined"
              @click="selectStage(stage.campaignId, stage.nodeId)">
              <strong>{{ stage.nodeName }}</strong>
              <small>
                {{ stage.mapName ?? "no ground yet" }} · {{ stage.campaignName }}
              </small>
            </button>
          </li>
        </ul>
      </div>

      <div class="record-editor">
        <!-- Making one. The ground comes first because a Stage is a fight on
             ground: everything else in it stands on the map. -->
        <section v-if="!openStageNode" class="stage-make"
          aria-labelledby="stage-make-title">
          <h3 id="stage-make-title">Set up a Stage</h3>
          <p v-if="project.maps.length === 0" class="field-help">
            A Stage is a fight on a map, and this game has no map yet.
            <button type="button" class="secondary" @click="selectSection('maps')">
              Go to Maps
            </button>
          </p>
          <template v-else>
            <label for="stage-ground">The ground it is fought on</label>
            <select id="stage-ground" :value="newStageMapChoice"
              @change="newStageMapId = ($event.target as HTMLSelectElement).value">
              <option v-for="map in project.maps" :key="map.id" :value="map.id">
                {{ map.name }} ({{ map.width }}×{{ map.height }})
              </option>
            </select>
            <template v-if="(project.campaigns ?? []).length > 1">
              <label for="stage-campaign">Which campaign it joins</label>
              <select id="stage-campaign" :value="stageCampaignChoice"
                @change="stageCampaignId =
                  ($event.target as HTMLSelectElement).value">
                <option v-for="campaign in project.campaigns" :key="campaign.id"
                  :value="campaign.id">
                  {{ campaign.name }}
                </option>
              </select>
            </template>
            <div class="stage-make-verbs">
              <button type="button" class="make-stage"
                @click="makeStage('findOrMake')">
                Make the Stage
              </button>
              <p class="field-help">
                One Stage per map, not one per press: pressing again opens the
                same one.
              </p>
              <!-- The second decision, on a control of its own. A map may be
                   fought over more than once, and the format says so, but that
                   is not what somebody pressing the button above twice meant.
                   It appears only once there is something to be another of, so
                   fresh ground offers one verb rather than two. -->
              <template v-if="stagesHereInChosenCampaign > 0">
                <button type="button" class="secondary"
                  @click="makeStage('another')">
                  Add another Stage on this ground
                </button>
                <p class="field-help">
                  {{ stagesHereSentence }} This adds one more, after the last
                  thing that campaign does.
                </p>
              </template>
            </div>
          </template>
        </section>

        <!-- How this game's fights end, as a report. What winning a Stage
             means is stated on that Stage, because the condition names somebody
             on its board and cannot be judged anywhere else. This is the
             other direction: every way to win at once, and which fights use
             it. An objective nothing uses compiles perfectly and never
             happens, which no validator will ever mention, so it is named
             here where it can be got rid of. -->
        <section v-if="!openStageNode && waysToWin.length > 0"
          class="ways-to-win" aria-labelledby="ways-to-win-title">
          <h3 id="ways-to-win-title">How these Stages are won</h3>
          <p class="field-help">Open a Stage to change one.</p>
          <ul class="ways-to-win-list">
            <li v-for="way in waysToWin" :key="way.id">
              <strong>{{ way.name }}</strong>
              <template v-if="way.stages.length">
                <span>
                  Decides
                  {{ way.stages.map((stage) => stage.nodeName).join(", ") }}.
                </span>
                <button type="button" class="secondary"
                  @click="selectStage(
                    way.stages[0]!.campaignId, way.stages[0]!.nodeId
                  )">
                  Open {{ way.stages[0]!.nodeName }}
                </button>
              </template>
              <template v-else>
                <span class="ways-to-win-unused">
                  No Stage is won this way, so it never happens.
                </span>
                <button type="button" class="danger"
                  @click="removeUnusedObjective(way.id, way.name)">
                  Remove it
                </button>
              </template>
            </li>
          </ul>
        </section>

        <StageEditor v-if="openStageNode"
          ref="stageEditor"
          :key="`${openStageNode.campaignId}/${openStageNode.nodeId}`"
          :project="project"
          :campaign-id="openStageNode.campaignId"
          :node-id="openStageNode.nodeId"
          @save-node="saveStageNode(openStageNode.campaignId, $event)"
          @dirty="emit('dirty')"
          @update-dialogues="saveDialogues"
          @update-objectives="saveObjectives"
          @add-way-to-win="addWayToWin(
            openStageNode.campaignId, openStageNode.nodeId, $event
          )"
          @enroll-member="enrollMember(openStageNode.campaignId, $event)"
          @cast-character="castCharacter(
            openStageNode.campaignId, openStageNode.nodeId, $event
          )"
          @create-unit-type="createRelated('unit_type')"
          @create-item="createRelated('item')"
          @open-in-flow="openNodeInFlow(openStageNode.campaignId, $event)"
          @draw-map="selectSection('maps')"
          @close="openStage = undefined" />
      </div>
    </div>

    <!-- Flow. The shape of a game, which is a graph and is drawn as one. There
         is no record list and no "create a campaign": arriving here makes the
         record that holds a road if the game has not got one, because an
         author came to see their game's shape and the record is the format's
         business, not theirs. -->
    <div v-if="activeSection.kind === 'flow'" class="flow-page">
      <div v-if="flowCampaign" class="flow-campaign-bar">
        <label for="flow-campaign-name">What this campaign is called</label>
        <!-- Held while it is being typed, and one undoable rename when the
             field is left. `input` is what tells the header there are words
             here the project has not got. -->
        <input id="flow-campaign-name" :key="flowCampaign.id"
          :value="keystrokes.shown('flow-campaign-name', flowCampaign.name)"
          @input="keystrokes.type(
            'flow-campaign-name',
            ($event.target as HTMLInputElement).value,
            renameFlowCampaign
          )"
          @change="keystrokes.leave(
            'flow-campaign-name',
            ($event.target as HTMLInputElement).value,
            renameFlowCampaign
          )">
        <!-- Several campaigns are a real thing the format holds, and they stay
             reachable, behind the one an author with a single campaign is
             looking at, never in front of it. -->
        <template v-if="(project.campaigns ?? []).length > 1">
          <label for="flow-campaign">Which campaign</label>
          <!-- Through `leaveEditors`, because the fields above and beside this
               menu are about to be drawing a different campaign, and a name
               half typed into one of them must not land on the other. -->
          <select id="flow-campaign" :value="flowCampaign.id"
            @change="openCampaign(($event.target as HTMLSelectElement).value)">
            <option v-for="campaign in project.campaigns" :key="campaign.id"
              :value="campaign.id">
              {{ campaign.name }}
            </option>
          </select>
        </template>
        <button type="button" class="secondary" @click="startAnotherCampaign">
          Start another campaign
        </button>
        <button v-if="(project.campaigns ?? []).length > 1" type="button"
          class="danger" @click="removeCampaign">
          Remove {{ flowCampaign.name }}
        </button>
      </div>

      <CampaignFlowGraph v-if="flowCampaign"
        :key="flowCampaign.id"
        :flow="flowCampaign.flow"
        :project="project"
        :selected-node-id="flowNodeId"
        @gesture="applyGraphGesture"
        @select="flowNodeId = $event"
        @open-stage="openStageIn(flowCampaign.id, $event)" />

      <p v-if="flowCampaign && !flowCampaign.flow" class="signpost">
        No road yet. The first Stage you make puts itself on it.
        <button type="button" class="secondary" @click="selectSection('stages')">
          Go to Stages
        </button>
      </p>

      <!-- People the company holds that no board fields. Reported and not
           removed: a project authored before a placement took its member away
           with it is already carrying them, and deleting somebody from a
           company without being asked would be a second surprise on top of the
           first. Taking a placement off now takes its member too, so this only
           ever names what is already there. -->
      <p v-if="flowCampaign && unfieldedCompany.length" class="unfielded-company"
        data-testid="unfielded-company" role="status">
        {{ unfieldedCompany.length }} in this company
        {{ unfieldedCompany.length === 1 ? "stands" : "stand" }} on no board:
        {{ unfieldedCompany.map((member) => member.name).join(", ") }}.
        They are carried into the game and shown on the company screen. Remove
        any you did not mean to keep.
      </p>

      <RosterMemberEditor v-if="flowCampaign"
        ref="campaignRoster"
        id-prefix="roster"
        heading="The company this campaign starts with"
        :help="rosterHelp"
        member-word="member"
        :members="flowCampaign.roster ?? []"
        :unit-types="project.unitTypes"
        :other-ids="recruitedIds"
        @update="saveCampaignRoster"
        @dirty="emit('dirty')"
        @create-unit-type="createRelated('unit_type')" />
      <ItemGrantEditor v-if="flowCampaign"
        ref="campaignStore"
        id-prefix="starting-store"
        heading="What the company's store starts with"
        :help="startingStoreHelp"
        grant-word="starting stock"
        :grants="flowCampaign.startingStore ?? []"
        :items="project.items"
        @update="saveCampaignStartingStore"
        @dirty="emit('dirty')"
        @create-item="createRelated('item')" />

      <!-- The same road, as a list and a form, underneath the picture of it.
           This is not a duplicate surface but the depth behind the simple one:
           a branch's condition, the order branches are tried in, and what a
           story stop says are real things an author needs and are not
           gestures.

           It is **closed**. The graph above it is what a first game needs, and
           it already says the same road in the form an author thinks in; this
           opened underneath it as a second, longer telling of a picture they
           had just read. The fold is not a hiding place: it holds the only
           control for several things, a story stop's scenes among them, so it
           is one press away and its summary names what is inside rather than
           saying "more". -->
      <details v-if="flowCampaign" class="flow-detail">
        <summary>Every stop in words, with branch conditions</summary>
        <CampaignFlowEditor
          ref="flowEditor"
          :key="flowCampaign.id"
          :flow="flowCampaign.flow"
          :maps="project.maps"
          :unit-types="project.unitTypes"
          :objectives="project.objectives ?? []"
          :dialogues="project.dialogues ?? []"
          :items="project.items"
          :roster="flowCampaign.roster ?? []"
          :project="project"
          @update-objectives="saveObjectives"
          @update-dialogues="saveDialogues"
          @create-unit-type="createRelated('unit_type')"
          @create-item="createRelated('item')"
          @open-stage="openStageIn(flowCampaign.id, $event)"
          @dirty="emit('dirty')"
          @save="saveCampaignFlow" />
      </details>
    </div>

    <p class="save-status" aria-live="polite">{{ feedback }}</p>

    <!-- Both of these were on the page an author lands on, and neither one
         changes how the game plays. The playtest is a smaller game than ▶ Play:
         one Stage, a mode switch, no story. The one thing it has that
         Play has not is the engine's own fingerprint of authoritative state,
         which is a diagnostic. The file fields below it are read-only
         machinery and two boxes the author writes to themselves in. -->
    <PlaytestPanel v-if="activeSection.kind === 'diagnostics'"
      :project="project" />

    <section v-if="activeSection.kind === 'diagnostics'" class="project-file"
      aria-labelledby="project-file-title">
      <h3 id="project-file-title">This project's file</h3>
      <SchemaRecordForm ref="metadataForm" heading="project metadata"
        heading-already-given
        :fields="sourceProjectMetadataFields()" :model-value="project"
        :readonly-paths="['schemaVersion', 'packageId']"
        @dirty="emit('dirty')"
        @submit="saveMetadata" />
    </section>
  </section>
</template>

<style scoped>
.shelf-panel {
  margin: 0.75rem 0;
  padding: 0.75rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.65rem;
  background: #f5f7f2;
}
.shelf-panel h3 {
  margin: 0 0 0.25rem;
}
.shelf-row {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  align-items: center;
}
.shelf-go {
  background: #2e9e5b;
  color: #ffffff;
  font-weight: 700;
}
.shelf-settings {
  display: flex;
  flex-wrap: wrap;
  gap: 0.4rem;
  margin: 0.4rem 0;
}
.shelf-settings button[aria-current="page"] {
  background: #2f4f3a;
  color: #ffffff;
}
.shelf {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(11rem, 1fr));
  gap: 0.5rem;
  margin: 0.5rem 0 0.75rem;
}
.shelf-card {
  display: grid;
  gap: 0.15rem 0.5rem;
  align-items: center;
  padding: 0.5rem;
  text-align: left;
  background: #ffffff;
  /* Cards read as content, not as commands, so they undo the button colour. */
  color: #1c2a20;
  border: 1px solid #c7d2ca;
}
.shelf-card span {
  font-size: 0.8rem;
  line-height: 1.25;
  color: #46524a;
}
.shelf-card em {
  font-size: 0.7rem;
  font-style: normal;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  color: #5a6860;
}
.shelf-card[aria-checked="true"] {
  border-color: #2e9e5b;
  border-width: 2px;
  padding: calc(0.5rem - 1px);
  background: #eaf6ee;
}
/* One line, not a panel: the accordion it replaces asked an author to hold
   four steps while they were answering one question. */
.project-file {
  margin-top: 1.5rem;
}
.map-stages {
  margin: 0.75rem 0 0;
  padding: 0.5rem 0.75rem;
  border-radius: 0.5rem;
  background: #f4f7fd;
  color: #38445c;
  font-size: 0.875rem;
}
.map-stages h4 {
  margin: 0 0 0.25rem;
}
.stage-make-verbs {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  align-items: center;
  margin-top: 0.75rem;
}
.stage-make-verbs .field-help {
  flex: 1 1 100%;
  margin: 0;
}
.make-stage {
  background: #2e9e5b;
  color: #ffffff;
  font-weight: 700;
}
.map-stage-list {
  display: grid;
  gap: 0.5rem;
  margin: 0;
  padding: 0;
  list-style: none;
}
.map-stage-list li {
  display: grid;
  gap: 0.15rem;
  justify-items: start;
  padding: 0.4rem 0.5rem;
  border: 1px solid #c9d4e8;
  border-radius: 0.4rem;
  background: #ffffff;
}
.map-stage-list small {
  color: #5a6860;
}
.signpost {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  align-items: center;
  margin: 0.75rem 0 0;
  padding: 0.5rem 0.75rem;
  border-radius: 0.5rem;
  background: #f4f7fd;
  color: #38445c;
  font-size: 0.875rem;
}
.section-hint {
  margin: 0 0 0.75rem;
  color: #4a5a52;
  font-size: 0.85rem;
}
.dialogue-record-lines {
  max-width: none;
  margin-top: 1.5rem;
  padding: 1rem;
  background: #f7f9fd;
}
/* Flow and Stages are the two sections holding a picture rather than a form.
 * A picture put in a record column is a picture nobody can see, and a Stage's
 * board is a picture: held to the reading width every other section takes, the
 * board, the palette and the panel about the selected character had nowhere to
 * go but under one another, and the column ran to thousands of pixels. */
#content.content-wide {
  max-width: none;
}
/* Flow is one column rather than two: what it shows is a picture of the whole
   campaign, and a picture put in a record column is a picture nobody can see. */
.flow-page {
  display: grid;
  gap: 0.75rem;
}
.flow-campaign-bar {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  align-items: center;
}
.flow-campaign-bar label {
  margin: 0;
}
.flow-detail {
  padding: 0.5rem 0.75rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.5rem;
  background: #fbfcfa;
}
.flow-detail summary {
  cursor: pointer;
  font-weight: 700;
}
.ways-to-win {
  margin-top: 1rem;
}
.ways-to-win-list {
  display: grid;
  gap: 0.35rem;
  margin: 0.5rem 0 0;
  padding: 0;
  list-style: none;
}
.ways-to-win-list li {
  display: flex;
  flex-wrap: wrap;
  gap: 0.4rem;
  align-items: baseline;
  padding: 0.4rem 0.5rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.4rem;
  background: #ffffff;
}
.ways-to-win-unused {
  color: #8a6a20;
}
</style>
