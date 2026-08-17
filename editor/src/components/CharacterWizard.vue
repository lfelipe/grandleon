<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// Making a character, in the order the decisions actually happen.
//
// A character in this format is a chain, not a record: `buildCharacterChain`
// emits a weapon type, a weapon, a class and a unit type in one press, and the
// catalogue shelf creates them that way. This is that chain made explicit and
// stepped: the same recipes, the same builder, the same ordinary records
// afterwards. What it adds is the one question the shelf does not ask and the
// format has always had an answer for: **whose side are they on**.
//
// It replaces the page rather than floating over it. A modal would need a focus
// trap, an inert background and an escape contract to be correct, and would
// still hide the list of characters the author is adding to. Replacing the page
// needs none of that and loses nothing: Back keeps every choice, Cancel creates
// nothing, and nothing at all exists until the last press.

import { computed, nextTick, onMounted, ref } from "vue";
import type { SourceProject, SourceUnitType } from "../generated/source-v1";
import {
  CHARACTER_FIGURES,
  DEFAULT_CHARACTER_FIGURE,
  unitSprite
} from "../domain/board-art";
import {
  CATALOGUE_SETTINGS,
  characterRecipes,
  type CatalogueSetting,
  type CharacterRecipe
} from "../domain/character-recipe";
import {
  SIDE_FACTIONS,
  UNALIGNED_LABEL,
  UNALIGNED_SUMMARY
} from "../domain/character-standing";
import { randomCharacter } from "../domain/random-character";

const props = defineProps<{
  /** For the drawing a role gets: this game's style, not the shelf's setting. */
  project: SourceProject;
}>();

const emit = defineEmits<{
  cancel: [];
  create: [choice: {
    role: CharacterRecipe["role"];
    setting: CatalogueSetting;
    name: string;
    /** The side faction to put them on, or "" for neither. */
    sideId: string;
    /**
     * The body they are drawn at, or undefined for the game's own. Undefined
     * rather than the default's id: a character that names no figure follows
     * the game, and writing the default as an override would be saying
     * something the author did not.
     */
    figureId?: SourceUnitType["characterFigureId"];
  }];
}>();

const STEPS = [
  { id: "side", label: "Whose side" },
  { id: "kind", label: "What kind" },
  { id: "name", label: "Their name" }
] as const;

const step = ref(0);
const sideId = ref("");
// The shelf an author is offered first is the project's own style, when the
// catalogue has a shelf for it. That is the whole of what the game-wide style
// being a *theme* means: it decides what is easy to reach and nothing about
// what an author may make, and every other shelf is one click away.
const setting = ref<CatalogueSetting>(
  CATALOGUE_SETTINGS.some((entry) => entry.id === props.project.characterStyleId)
    ? (props.project.characterStyleId as CatalogueSetting)
    : CATALOGUE_SETTINGS[0]!.id
);
const recipeId = ref(characterRecipes[0]!.id);
const name = ref("");
// Which body the drawings show. It is asked on the step that already shows a
// drawing, so it is one press against a picture rather than a question of its
// own, and the shelf redraws as it is pressed because that is the whole of
// what this choice does.
const figureId = ref<NonNullable<SourceUnitType["characterFigureId"]>>(
  DEFAULT_CHARACTER_FIGURE as NonNullable<SourceUnitType["characterFigureId"]>
);

const heading = ref<HTMLElement>();
const assetBase = import.meta.env.BASE_URL;

const shelf = computed(() =>
  characterRecipes.filter((recipe) => recipe.setting === setting.value)
);
const recipe = computed(
  () =>
    shelf.value.find((entry) => entry.id === recipeId.value) ?? shelf.value[0]!
);
const side = computed(() =>
  SIDE_FACTIONS.find((faction) => faction.id === sideId.value)
);

/**
 * The weapon this press will write, with the article that fits it.
 *
 * Six of the catalogue's fifty-six weapons start with a vowel, an officer's
 * blade, an ember staff and an acorn branch, and a fixed "a" in front of the name
 * made the one sentence an author reads before committing to five records read
 * like a machine wrote it. Nothing in the catalogue starts with a sounded vowel
 * spelt with a consonant or the reverse, so the first letter decides it.
 */
const weaponPhrase = computed(() => {
  const weapon = recipe.value.weaponName.toLocaleLowerCase();
  return `${"aeiou".includes(weapon[0] ?? "") ? "an" : "a"} ${weapon}`;
});

/** The picture a role will be drawn with: this project's style, not the shelf's
 *  setting. A sci-fi name in a medieval game shows the medieval drawing. */
function recipeSprite(entry: CharacterRecipe): string {
  return (
    assetBase +
    unitSprite(
      entry.role,
      "first",
      undefined,
      props.project.characterStyleId,
      figureId.value
    )
  );
}

function selectSetting(chosen: CatalogueSetting) {
  const role = recipe.value.role;
  setting.value = chosen;
  recipeId.value = `${chosen}_${role}`;
}

// A shelf is one tab stop with arrow keys inside it, so reaching the eighth
// entry never costs eight tabs. The same behaviour the shelf had on the page.
const shelfRoot = ref<HTMLElement>();
function moveShelfFocus(direction: number) {
  const next = shelf.value[
    shelf.value.findIndex((entry) => entry.id === recipe.value.id) + direction
  ];
  if (!next) return;
  shelfRoot.value?.querySelector<HTMLElement>(`[data-recipe="${next.id}"]`)?.focus();
  recipeId.value = next.id;
}

async function goTo(next: number) {
  step.value = Math.min(Math.max(next, 0), STEPS.length - 1);
  await nextTick();
  heading.value?.focus();
}

function finish() {
  emit("create", {
    role: recipe.value.role,
    setting: recipe.value.setting,
    name: name.value,
    sideId: sideId.value,
    ...(figureId.value === DEFAULT_CHARACTER_FIGURE
      ? {}
      : { figureId: figureId.value })
  });
}

/**
 * Every choice made, and the last press pressed.
 *
 * It emits the same `create` this wizard's own last button emits, so a random
 * character is built by the same builder into the same four records. Nothing
 * here writes a project, for the same reason nothing else in this file does.
 */
function makeRandom() {
  const chosen = randomCharacter(props.project);
  emit("create", {
    role: chosen.role,
    setting: chosen.setting,
    name: chosen.name,
    sideId: chosen.sideId,
    ...(figureId.value === DEFAULT_CHARACTER_FIGURE
      ? {}
      : { figureId: figureId.value })
  });
}

onMounted(() => {
  heading.value?.focus();
});
</script>

<template>
  <section class="character-wizard" aria-labelledby="character-wizard-title">
    <h3 id="character-wizard-title" ref="heading" tabindex="-1">
      New character: {{ STEPS[step]!.label }}
    </h3>

    <!-- The step in progress is marked rather than merely coloured, so it is
         the same fact for a screen reader as it is for an eye. -->
    <ol class="wizard-steps">
      <li v-for="(entry, index) in STEPS" :key="entry.id"
        :aria-current="index === step ? 'step' : undefined">
        <span class="wizard-step-number" aria-hidden="true">{{ index + 1 }}</span>
        {{ entry.label }}
      </li>
    </ol>

    <div v-if="step === 0" class="wizard-panel">
      <fieldset class="wizard-sides">
        <legend>Whose side are they on</legend>
        <label v-for="faction in SIDE_FACTIONS" :key="faction.id">
          <input v-model="sideId" type="radio" name="character-wizard-side"
            :value="faction.id">
          <span>
            <strong>{{ faction.label }}</strong>
            <small>{{ faction.summary }}</small>
          </span>
        </label>
        <label>
          <input v-model="sideId" type="radio" name="character-wizard-side"
            value="">
          <span>
            <strong>{{ UNALIGNED_LABEL }}</strong>
            <small>{{ UNALIGNED_SUMMARY }}</small>
          </span>
        </label>
      </fieldset>
    </div>

    <div v-else-if="step === 1" class="wizard-panel">
      <nav aria-label="Kinds of game" class="shelf-settings">
        <button v-for="entry in CATALOGUE_SETTINGS" :key="entry.id" type="button"
          :aria-current="setting === entry.id ? 'page' : undefined"
          @click="selectSetting(entry.id)">
          {{ entry.label }}
        </button>
      </nav>
      <p class="field-help">
        {{ CATALOGUE_SETTINGS.find((entry) => entry.id === setting)?.summary }}
      </p>
      <div ref="shelfRoot" class="shelf" role="radiogroup"
        aria-label="Kind of character"
        @keydown.left.prevent="moveShelfFocus(-1)"
        @keydown.up.prevent="moveShelfFocus(-1)"
        @keydown.right.prevent="moveShelfFocus(1)"
        @keydown.down.prevent="moveShelfFocus(1)">
        <button v-for="entry in shelf" :key="entry.id" type="button"
          class="shelf-card with-picture" role="radio" :data-recipe="entry.id"
          :aria-checked="recipe.id === entry.id ? 'true' : 'false'"
          :tabindex="recipe.id === entry.id ? 0 : -1"
          @click="recipeId = entry.id">
          <img :src="recipeSprite(entry)" alt="" width="32" height="32">
          <strong>{{ entry.label }}</strong>
          <span>{{ entry.summary }}</span>
        </button>
      </div>

      <!-- The body, on the step that already shows a drawing rather than on a
           step of its own. A game whose characters are all one figure should
           not answer this once per character, so it is one press beside the
           picture and never a fourth question. The shelf redraws as it is
           pressed, which is the whole of what the choice does. -->
      <fieldset class="wizard-figures">
        <legend>Body</legend>
        <div role="radiogroup" aria-label="Body">
          <button v-for="entry in CHARACTER_FIGURES" :key="entry.id"
            type="button" role="radio" :data-figure="entry.id"
            :aria-checked="figureId === entry.id ? 'true' : 'false'"
            :tabindex="figureId === entry.id ? 0 : -1"
            @click="figureId = entry.id as typeof figureId">
            {{ entry.label }}
          </button>
        </div>
        <p class="field-help">Only the picture changes.</p>
      </fieldset>
    </div>

    <div v-else class="wizard-panel">
      <div class="shelf-row">
        <label for="character-wizard-name">Name</label>
        <input id="character-wizard-name" v-model.trim="name"
          :placeholder="recipe.label">
      </div>
      <!-- What the press will make, said before it is pressed. Said
           afterwards, it is how an author ends up owning three records they
           never asked for. -->
      <p class="field-help">
        Makes their class and {{ weaponPhrase }} too<span v-if="side">,
        fighting for {{ side.name }}</span>. All editable afterwards.
      </p>
    </div>

    <div class="wizard-commands" role="group" aria-label="Wizard">
      <button type="button" class="secondary" @click="emit('cancel')">
        Cancel
      </button>
      <!-- For filling a board to try something out, when none of the three
           questions is the one being asked. It answers them rather than
           skipping them: the same last press, the same chain. -->
      <button type="button" class="secondary" data-testid="wizard-random"
        @click="makeRandom">
        Create random
      </button>
      <button v-if="step > 0" type="button" @click="goTo(step - 1)">
        Back
      </button>
      <button v-if="step < STEPS.length - 1" type="button" class="wizard-next"
        @click="goTo(step + 1)">
        Next
      </button>
      <button v-else type="button" class="wizard-next" @click="finish">
        Make them
      </button>
    </div>
  </section>
</template>

<style scoped>
.character-wizard {
  margin: 0.75rem 0;
  padding: 0.75rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.65rem;
  background: #f5f7f2;
}
.character-wizard h3 {
  margin: 0 0 0.5rem;
}
.wizard-figures {
  border: 1px solid #ccd5ce;
  border-radius: 0.5rem;
  display: grid;
  gap: 0.4rem;
  margin: 0 0 0.75rem;
  padding: 0.5rem 0.75rem;
}

.wizard-figures div {
  display: flex;
  gap: 0.5rem;
}

.wizard-figures button {
  background: #e8edf7;
  color: #172033;
}

.wizard-figures button[aria-checked="true"] {
  background: #254f9b;
  color: white;
}

.wizard-steps {
  display: flex;
  flex-wrap: wrap;
  gap: 0.75rem;
  margin: 0 0 0.75rem;
  padding: 0;
  list-style: none;
  font-size: 0.85rem;
  color: #5a6860;
}
.wizard-steps li {
  display: flex;
  gap: 0.35rem;
  align-items: center;
}
.wizard-steps li[aria-current="step"] {
  color: #1c2a20;
  font-weight: 700;
}
.wizard-step-number {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 1.4rem;
  height: 1.4rem;
  border-radius: 50%;
  background: #dde5dd;
}
.wizard-steps li[aria-current="step"] .wizard-step-number {
  background: #2e9e5b;
  color: #ffffff;
}
.wizard-sides {
  display: grid;
  gap: 0.5rem;
  margin: 0.5rem 0;
  padding: 0.5rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.5rem;
  background: #ffffff;
}
.wizard-sides label {
  display: flex;
  gap: 0.5rem;
  align-items: baseline;
}
.wizard-sides span {
  display: grid;
}
.wizard-sides small {
  font-size: 0.8rem;
  line-height: 1.3;
  color: #46524a;
}
.wizard-commands {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  margin-top: 0.75rem;
}
.wizard-next {
  background: #2e9e5b;
  color: #ffffff;
  font-weight: 700;
}
/* The shelf keeps the look it had on the page, because it is the same shelf. */
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
  grid-template-columns: 2rem 1fr;
  gap: 0.15rem 0.5rem;
  align-items: center;
  padding: 0.5rem;
  text-align: left;
  background: #ffffff;
  color: #1c2a20;
  border: 1px solid #c7d2ca;
}
.shelf-card img {
  grid-row: span 2;
  image-rendering: pixelated;
}
.shelf-card span {
  font-size: 0.8rem;
  line-height: 1.25;
  color: #46524a;
}
.shelf-card[aria-checked="true"] {
  border-color: #2e9e5b;
  border-width: 2px;
  padding: calc(0.5rem - 1px);
  background: #eaf6ee;
}
.shelf-row {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  align-items: center;
}
</style>
