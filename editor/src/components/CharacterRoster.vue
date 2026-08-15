<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// The characters this game has, drawn.
//
// This is the first thing the Characters section shows. An author arriving
// here wants their own characters, not two catalogues of parts to build one
// out of with the collection they own a page and a half further down; the
// characters come first and the way to make another is one button beside them.
//
// A card carries the three things that are true of a character and that a bare
// list of record names cannot show: the drawing the board will actually use,
// whose side they are on, and whether anything in the game depends on them
// being who they are. The last two are computed in `character-standing.ts`
// from the project itself, so nothing here can go stale and nothing had to be
// declared.

import { computed, ref } from "vue";
import type { SourceProject } from "../generated/source-v1";
import { factionColor, unitSprite } from "../domain/board-art";
import {
  characterSide,
  characterStanding,
  standingSentence
} from "../domain/character-standing";

const props = defineProps<{
  project: SourceProject;
  /** The character open in the editor below, so the list can mark it. */
  selectedId?: string;
}>();

const emit = defineEmits<{
  select: [id: string];
  create: [];
}>();

const assetBase = import.meta.env.BASE_URL;

// So a wizard that was opened from here can hand focus back to the control that
// opened it, rather than dropping the author at the top of the document.
const newButton = ref<HTMLButtonElement>();
function focusNew() {
  newButton.value?.focus();
}
defineExpose({ focusNew });

interface Card {
  readonly id: string;
  readonly name: string;
  readonly className: string;
  readonly side: string;
  readonly standing: string;
  readonly kind: "named" | "extra" | "unused";
  readonly sprite: string;
}

/**
 * How many people a grid of cards shows before it needs a way to search them.
 *
 * Below this the search box would be a control over a list already entirely on
 * screen; above it, scrolling for somebody is the only way to find them. The
 * number is the roster this grid draws in about two rows on an ordinary
 * window.
 */
const searchableFrom = 8;

const search = ref("");

const cards = computed<readonly Card[]>(() =>
  props.project.unitTypes.map((unit) => {
    const standing = characterStanding(props.project, unit.id);
    const colour = factionColor(props.project.factions ?? [], unit.factionId);
    return {
      id: unit.id,
      name: unit.name,
      className:
        props.project.classes.find((entry) => entry.id === unit.classId)?.name ??
        unit.classId,
      side: characterSide(props.project, unit.factionId),
      standing: standingSentence(unit.name, standing),
      kind: standing.kind,
      // The side argument only decides the colour when no faction claims the
      // character, and the card is not a board: a character on nobody's side is
      // drawn the way the first side is drawn, which is what the editor has
      // always drawn an unclaimed character as.
      sprite: assetBase + unitSprite(
        unit.classId,
        "first",
        colour,
        props.project.characterStyleId
      )
    };
  })
);

const searchable = computed(() => cards.value.length >= searchableFrom);

/**
 * The cards a search leaves standing. Name, class and stored identifier are
 * all matched: an author looking for somebody types their name, and an author
 * who arrived from a reported problem has an identifier in hand.
 */
const shownCards = computed(() => {
  const query = search.value.trim().toLocaleLowerCase();
  if (!searchable.value || query === "") return cards.value;
  return cards.value.filter((card) =>
    card.name.toLocaleLowerCase().includes(query) ||
    card.className.toLocaleLowerCase().includes(query) ||
    card.id.toLocaleLowerCase().includes(query)
  );
});
</script>

<template>
  <section class="character-roster" aria-labelledby="character-roster-title">
    <div class="character-roster-head">
      <div>
        <h3 id="character-roster-title">Your characters</h3>
      </div>
      <button ref="newButton" type="button" class="character-roster-new"
        @click="emit('create')">
        New character
      </button>
    </div>

    <!-- A search over a grid that is entirely on screen is a control with
         nothing to do, so it appears with the roster that needs it. -->
    <div v-if="searchable" class="character-roster-search">
      <label for="character-roster-search">Search your characters</label>
      <input id="character-roster-search" v-model="search" type="search">
      <p aria-live="polite">
        {{ shownCards.length }} of {{ cards.length }} characters
      </p>
    </div>

    <p v-if="cards.length === 0" class="field-help character-roster-empty">
      No characters yet.
    </p>
    <p v-else-if="shownCards.length === 0"
      class="field-help character-roster-empty">
      Nobody here is called that.
    </p>

    <ul v-else class="character-cards">
      <li v-for="card in shownCards" :key="card.id">
        <button type="button" class="character-card"
          :class="`standing-${card.kind}`"
          :aria-current="selectedId === card.id ? 'true' : undefined"
          @click="emit('select', card.id)">
          <img :src="card.sprite" alt="" width="32" height="32">
          <strong>{{ card.name }}</strong>
          <span class="character-card-side">{{ card.side }}</span>
          <span class="character-card-class">{{ card.className }}</span>
          <span class="character-card-standing">{{ card.standing }}</span>
        </button>
      </li>
    </ul>
  </section>
</template>

<style scoped>
.character-roster {
  margin: 0.75rem 0;
  padding: 0.75rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.65rem;
  background: #f5f7f2;
}
.character-roster-head {
  display: flex;
  flex-wrap: wrap;
  gap: 0.75rem;
  align-items: flex-start;
  justify-content: space-between;
}
.character-roster h3 {
  margin: 0 0 0.25rem;
}
.character-roster-new {
  background: #2e9e5b;
  color: #ffffff;
  font-weight: 700;
}
.character-roster-empty {
  margin-top: 0.5rem;
}
.character-roster-search {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  align-items: baseline;
  margin-top: 0.5rem;
}
.character-roster-search input {
  flex: 1 1 12rem;
}
.character-roster-search p {
  margin: 0;
  font-size: 0.8rem;
  color: #5a6860;
}
.character-cards {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(14rem, 1fr));
  gap: 0.5rem;
  margin: 0.5rem 0 0;
  padding: 0;
  list-style: none;
}
.character-card {
  display: grid;
  grid-template-columns: 2rem 1fr;
  gap: 0.1rem 0.5rem;
  align-items: center;
  width: 100%;
  padding: 0.5rem;
  text-align: left;
  background: #ffffff;
  /* Cards read as content, not as commands, so they undo the button colour. */
  color: #1c2a20;
  border: 1px solid #c7d2ca;
}
.character-card img {
  grid-row: span 2;
  image-rendering: pixelated;
}
.character-card-side {
  font-size: 0.7rem;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  color: #5a6860;
}
.character-card-class,
.character-card-standing {
  grid-column: 1 / -1;
  font-size: 0.8rem;
  line-height: 1.25;
  color: #46524a;
}
.character-card-standing {
  color: #5a6860;
}
/* A character nothing depends on is not a problem, so it is quieter rather
   than louder: the mark says "extra", not "wrong". */
.character-card.standing-extra {
  border-style: dashed;
}
.character-card[aria-current="true"] {
  border-color: #2e9e5b;
  border-width: 2px;
  padding: calc(0.5rem - 1px);
  background: #eaf6ee;
}
</style>
