<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// Win conditions stated the way an author thinks about them: "beat everyone",
// "defeat that one", "keep that one alive", rather than as a kind field, a
// side field, and a target identifier on a record three collections away.
//
// It edits the project's objective records in place and reports which ones the
// encounter uses, because an objective is a shared record but only means
// something inside an encounter that lists it.

import { computed } from "vue";
import type { SourceObjective } from "../generated/source-v1";
import { useKeystrokeDraft } from "./keystroke-draft";

type ObjectiveKind =
  | "defeatAllOpponents"
  | "defeatTarget"
  | "protectTarget"
  | "surviveRounds";

const props = defineProps<{
  objectives: readonly SourceObjective[];
  selectedIds: readonly string[];
  placements: readonly {
    readonly id: string;
    readonly memberId?: string | undefined;
    readonly side: "first" | "second";
    readonly unitTypeId: string;
  }[];
}>();

/**
 * What a targeted objective names one placement by.
 *
 * A placement fielding a member of the company is named by the *member*: the
 * character is who the objective is about, and they are the same character on
 * every board that places them, however each board names the tile. Only a
 * placement that fields nobody, on the other side, is named by the tile's own
 * identifier. This is exactly the key the content compiler resolves the target
 * against and exactly the key the runtime matches, so a menu offering anything
 * else would author a Stage no client can load.
 */
function targetKey(placement: {
  readonly id: string;
  readonly memberId?: string | undefined;
}): string {
  return placement.memberId ?? placement.id;
}

function targetLabel(placement: {
  readonly id: string;
  readonly memberId?: string | undefined;
  readonly side: "first" | "second";
}): string {
  const side = placement.side === "first" ? "yours" : "the enemy";
  return placement.memberId === undefined
    ? `${placement.id} (${side})`
    : `${placement.memberId} (${side}, standing as ${placement.id})`;
}

const emit = defineEmits<{
  updateObjectives: [objectives: SourceObjective[]];
  updateSelection: [ids: string[]];
  /**
   * A way this Stage is won that the game has not got a record for yet. One
   * event rather than a create followed by a tick: it is one decision, and the
   * surface above stores it as one.
   */
  add: [objective: SourceObjective];
  /** A keystroke that is not in the project yet, so the header can say so. */
  dirty: [];
}>();

/**
 * The one number on this page, while it is being typed.
 *
 * Every other control here is a tick or a menu, which produce one whole answer
 * per gesture and have nothing to hold. A round count does not: clearing the
 * box and typing "12" passes through nothing at all and then through "1", a
 * Stage won by surviving a single round. Half a number is not a number, and
 * each of those would also have cost an undo entry and a deep copy of the game.
 *
 * So the keystroke is held here, announced as unsaved so the header stops
 * claiming the project has it, and drawn by the control that owns it so a
 * redraw cannot write the stored number back over what is being typed.
 */
const keystrokes = useKeystrokeDraft(() => emit("dirty"));

defineExpose({ flush: keystrokes.flush });

const used = computed(() =>
  props.objectives.filter((objective) => props.selectedIds.includes(objective.id))
);

/**
 * A target this Stage has nobody to answer to, or nothing.
 *
 * A placement identifier is free text on the board next door, so renaming or
 * removing a character leaves this pointing at a name that is gone. The content
 * compiler refuses that outright: the encounter loader resolves the target
 * against the board's placements and will not load an encounter whose target
 * is not among them. An author who is not told here hears about it from a
 * device instead.
 */
function danglingTarget(objective: SourceObjective): string | undefined {
  const kind = objective.kind;
  if (kind !== "defeatTarget" && kind !== "protectTarget") return undefined;
  const target = objective.targetPlacementId;
  if (target === undefined || target === "") return undefined;
  return props.placements.some((placement) => targetKey(placement) === target)
    ? undefined
    : target;
}

function describe(objective: SourceObjective): string {
  const kind = objective.kind ?? "defeatAllOpponents";
  const owner = objective.side === "second" ? "The enemy" : "Your side";
  // Named only while the name means somebody. "Wins by defeating kesh" over a
  // Stage kesh has left is the one sentence on this screen an author would
  // trust, so it is not written.
  const target = danglingTarget(objective)
    ? "nobody on this board"
    : objective.targetPlacementId ?? "nobody";
  switch (kind) {
    case "defeatTarget":
      return `${owner} wins by defeating ${target}.`;
    case "protectTarget":
      return `${owner} loses if ${target} is defeated.`;
    case "surviveRounds":
      return `${owner} wins the moment round ${objective.rounds ?? 1} ends.`;
    default:
      return `${owner} wins by defeating every opposing character.`;
  }
}

function patch(id: string, change: Partial<SourceObjective>) {
  emit(
    "updateObjectives",
    props.objectives.map((objective) =>
      objective.id === id ? { ...objective, ...change } : { ...objective }
    )
  );
}

function setKind(objective: SourceObjective, kind: ObjectiveKind) {
  // A target only means something for the two targeted kinds, and a round
  // count only for the one that outlasts them. Dropping the field the new kind
  // cannot read keeps the record honest rather than leaving a stale reference
  // or a number nothing will consult, both of which the compiler and both
  // analyzers refuse, so leaving one behind would make this editor author a
  // project it could not build.
  if (kind !== "defeatTarget" && kind !== "protectTarget") {
    const { targetPlacementId, rounds, ...rest } = { ...objective, kind };
    void targetPlacementId;
    void rounds;
    emit(
      "updateObjectives",
      props.objectives.map((candidate) =>
        candidate.id === objective.id
          ? kind === "surviveRounds"
            ? { ...rest, rounds: objective.rounds ?? 7 }
            : rest
          : { ...candidate }
      )
    );
    return;
  }
  const { rounds, ...withoutCount } = { ...objective, kind };
  void rounds;
  const first = props.placements[0];
  const target = objective.targetPlacementId ??
    (first === undefined ? undefined : targetKey(first));
  emit(
    "updateObjectives",
    props.objectives.map((candidate) =>
      candidate.id === objective.id
        ? target === undefined
          ? withoutCount
          : { ...withoutCount, targetPlacementId: target }
        : { ...candidate }
    )
  );
}

function toggle(id: string, include: boolean) {
  const next = include
    ? [...new Set([...props.selectedIds, id])]
    : props.selectedIds.filter((candidate) => candidate !== id);
  emit("updateSelection", next);
}

/**
 * The four ways a fight can end, offered as the sentence each one is.
 *
 * Stating how a Stage is won is the same act as making the record that says
 * so, and this is the only place either happens. An author does not go
 * somewhere else to make an objective and come back to tick it: the format
 * stores a shared record, and the author says "this one is won by beating
 * everyone", which is a thing about this fight.
 *
 * Nothing here needs a name typed into it. The sentence is the name, and a
 * second Stage won the same way says the same sentence, which is exactly the
 * one the list beside it will show.
 */
const WAYS_TO_WIN: readonly {
  readonly stem: string;
  readonly label: string;
  readonly kind: ObjectiveKind;
}[] = [
  {
    stem: "defeat_all_opponents",
    label: "Beat everyone on the other side",
    kind: "defeatAllOpponents"
  },
  {
    stem: "defeat_target",
    label: "Beat one particular character",
    kind: "defeatTarget"
  },
  {
    stem: "protect_target",
    label: "Keep one particular character alive",
    kind: "protectTarget"
  },
  {
    stem: "survive_rounds",
    label: "Last a number of rounds",
    kind: "surviveRounds"
  }
];

/** An identifier no objective in this project has taken. */
function freeObjectiveId(stem: string): string {
  const taken = props.objectives.map((objective) => objective.id);
  if (!taken.includes(stem)) return stem;
  let suffix = 2;
  while (taken.includes(`${stem}_${suffix}`)) suffix += 1;
  return `${stem}_${suffix}`;
}

/**
 * States a new way this Stage is won.
 *
 * One emit, because it is one thing the author did. Making the record and
 * saying this fight is decided by it are two writes and a single decision, and
 * the surface above turns them into a single transaction so that undoing it
 * once puts everything back.
 */
function addWayToWin(recipe: typeof WAYS_TO_WIN[number]) {
  const first = props.placements[0];
  const target = recipe.kind === "defeatTarget" ||
    recipe.kind === "protectTarget"
    ? (first === undefined ? undefined : targetKey(first))
    : undefined;
  emit("add", {
    id: freeObjectiveId(recipe.stem),
    name: recipe.label,
    kind: recipe.kind,
    side: "first",
    ...(recipe.kind === "surviveRounds" ? { rounds: 7 } : {}),
    ...(target === undefined ? {} : { targetPlacementId: target })
  });
}
</script>

<template>
  <section class="win-conditions" aria-labelledby="win-conditions-title">
    <h4 id="win-conditions-title">Winning and losing</h4>
    <p class="field-help">
      Anything ticked decides this fight; anything unticked belongs to another
      Stage.
    </p>
    <!-- Not a note about a long battle. A Stage with nothing ticked cannot be
         opened at all: the package format writes the count of these first and
         every client refuses a board that declares none, so the campaign stops
         dead when it reaches this Stage. That was a real afternoon on a real
         console, so the wording says what actually happens and is an alert
         rather than a status. -->
    <p v-if="objectives.length === 0" class="win-conditions-undecided"
      data-testid="win-conditions-undecided" role="alert">
      This Stage cannot be played until something here is ticked. A Stage with
      no way to win or lose is refused by the game, so a campaign that reaches
      it stops there.
    </p>

    <!-- Stating how a Stage is won and making the record that says so are the
         same act, and this is the one place either happens. An author is never
         sent somewhere else to make an objective and back again to tick it. -->
    <div class="win-conditions-add" role="group"
      aria-label="Add a way this Stage is won">
      <button v-for="recipe in WAYS_TO_WIN" :key="recipe.stem" type="button"
        class="secondary" :data-way-to-win="recipe.stem"
        @click="addWayToWin(recipe)">
        {{ recipe.label }}
      </button>
    </div>

    <ul class="condition-list">
      <li v-for="objective in objectives" :key="objective.id">
        <label class="condition-toggle">
          <input type="checkbox" :checked="selectedIds.includes(objective.id)"
            @change="toggle(
              objective.id,
              ($event.target as HTMLInputElement).checked
            )">
          <strong>{{ objective.name }}</strong>
        </label>

        <!-- The fields belong to a condition this Stage actually uses. Every
             objective in the campaign is offered here, because switching one on
             is how a Stage takes it, but a condition this Stage has not taken is
             a line to read rather than a form to fill: seven objectives drew
             seven forms, which was over two thousand pixels of a Stage and the
             largest single thing on it. The same rule the board's own panel
             follows, where one selected character has fields and the others are
             tokens. -->
        <div v-if="selectedIds.includes(objective.id)" class="condition-fields">
          <label :for="`objective-${objective.id}-kind`">What decides it</label>
          <select :id="`objective-${objective.id}-kind`"
            :value="objective.kind ?? 'defeatAllOpponents'"
            @change="setKind(
              objective,
              ($event.target as HTMLSelectElement).value as ObjectiveKind
            )">
            <option value="defeatAllOpponents">Defeat every opposing character</option>
            <option value="defeatTarget">Defeat one particular character</option>
            <option value="protectTarget">Keep one particular character alive</option>
            <option value="surviveRounds">Survive a number of rounds</option>
          </select>

          <label :for="`objective-${objective.id}-side`">Whose condition</label>
          <select :id="`objective-${objective.id}-side`"
            :value="objective.side ?? 'first'"
            @change="patch(objective.id, {
              side: ($event.target as HTMLSelectElement).value as
                'first' | 'second'
            })">
            <option value="first">Your side</option>
            <option value="second">The enemy</option>
          </select>

          <template v-if="objective.kind === 'surviveRounds'">
            <label :for="`objective-${objective.id}-rounds`">How many rounds</label>
            <input :id="`objective-${objective.id}-rounds`" type="number"
              min="1" max="65535"
              :value="keystrokes.shown(
                `${objective.id}-rounds`, String(objective.rounds ?? 7)
              )"
              @input="keystrokes.type(
                `${objective.id}-rounds`,
                ($event.target as HTMLInputElement).value,
                (typed) => patch(objective.id, { rounds: Number(typed) })
              )"
              @change="keystrokes.leave(
                `${objective.id}-rounds`,
                ($event.target as HTMLInputElement).value,
                (typed) => patch(objective.id, { rounds: Number(typed) })
              )">
            <p class="field-help">
              A round is one pass through the turn order.
            </p>
          </template>

          <template v-if="objective.kind === 'defeatTarget'
            || objective.kind === 'protectTarget'">
            <label :for="`objective-${objective.id}-target`">Which character</label>
            <select :id="`objective-${objective.id}-target`"
              :value="objective.targetPlacementId ?? ''"
              @change="patch(objective.id, {
                targetPlacementId: ($event.target as HTMLSelectElement).value
              })">
              <!-- A stored target nobody on this board answers to stays
                   visible and selected, so an author is told rather than
                   silently moved onto whoever happens to be first. -->
              <option v-if="danglingTarget(objective)"
                :value="danglingTarget(objective)" disabled>
                {{ danglingTarget(objective) }}: nobody on this board
              </option>
              <option v-for="placement in placements" :key="placement.id"
                :value="targetKey(placement)">
                {{ targetLabel(placement) }}
              </option>
            </select>
            <p v-if="danglingTarget(objective)" class="condition-warning"
              role="alert">
              '{{ danglingTarget(objective) }}' does not stand on this board,
              so the Stage would never end.
            </p>
            <p v-if="placements.length === 0" class="condition-warning" role="alert">
              Nobody is placed on this Stage yet.
            </p>
          </template>

        </div>

        <!-- The sentence stays whether or not this Stage has taken the
             condition: it is what the condition *is*, and a list of names with
             no words on it would be a list nobody could read. It is the fields
             that follow the tick. -->
        <p class="condition-summary" role="status">{{ describe(objective) }}</p>
      </li>
    </ul>

    <p class="condition-summary" role="status">
      <template v-if="used.length === 0">
        This Stage ends only when one side is wiped out.
      </template>
      <template v-else>
        {{ used.length }} condition{{ used.length === 1 ? " decides" : "s decide" }}
        this Stage.
      </template>
    </p>
  </section>
</template>

<style scoped>
.win-conditions {
  margin-top: 1rem;
  padding: 0.75rem;
  border: 1px solid #ccd5ce;
  border-radius: 0.5rem;
}

/* Drawn as the refusal it is, and not as the field help it used to be: this
   Stage will not open, which is a different fact from a battle running long. */
.win-conditions-undecided {
  margin: 0.5rem 0;
  padding: 0.5rem 0.6rem;
  border-left: 4px solid #8d0b1d;
  background: #fdf2f3;
  color: #8d0b1d;
  font-size: 0.875rem;
  font-weight: 700;
}
.win-conditions-add {
  display: flex;
  flex-wrap: wrap;
  gap: 0.35rem;
  margin: 0.5rem 0;
}
.condition-list {
  margin: 0;
  padding: 0;
  list-style: none;
}
.condition-list > li {
  padding: 0.5rem 0;
  border-top: 1px solid #e2e8e3;
}
.condition-toggle {
  display: flex;
  gap: 0.4rem;
  align-items: center;
}
.condition-fields {
  display: grid;
  gap: 0.25rem;
  margin-left: 1.5rem;
}
.condition-summary {
  margin: 0.35rem 0 0;
  color: #4a5a52;
  font-size: 0.85rem;
}
.condition-warning {
  margin: 0.25rem 0 0;
  color: #8a2f27;
  font-size: 0.85rem;
}
</style>
