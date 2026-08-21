<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// Which kinds of weapon beat which, and what beating them is worth.
//
// The whole rule on one page, because the point of a triangle is that it can
// be held in the head: a player who has to look it up has not got a triangle,
// they have got a table. So this draws every type against every type, ticks
// the edges, and says the rule back in a sentence an author can read aloud.
//
// One press builds the classic case. Three types with nothing between them is
// where almost every game starts, and making an author tick three boxes in the
// right rotation to get there is three chances to get it backwards.
import { computed } from "vue";
import type { SourceWeaponType } from "../generated/source-v1";
import { useKeystrokeDraft } from "./keystroke-draft";

const props = defineProps<{
  weaponTypes: readonly SourceWeaponType[];
  /**
   * The weapons this game holds, which is what decides the kinds worth
   * drawing: a kind nothing in the game *is* can never meet another hand.
   */
  weapons: readonly { readonly id: string; readonly weaponTypeId?: string }[];
  /** What the better weapon is worth, or nothing if the game states none. */
  advantage: { readonly damage: number; readonly accuracy: number } | undefined;
}>();

const emit = defineEmits<{
  updateTypes: [types: SourceWeaponType[]];
  updateAdvantage: [
    advantage: { damage: number; accuracy: number } | undefined
  ];
  dirty: [];
}>();

const keystrokes = useKeystrokeDraft(() => emit("dirty"));
defineExpose({ flush: keystrokes.flush });

/** Every kind some weapon in this game actually is. */
const inPlay = computed(
  () =>
    new Set(
      props.weapons.flatMap((weapon) =>
        weapon.weaponTypeId === undefined ? [] : [weapon.weaponTypeId]
      )
    )
);

/**
 * The kinds this page draws.
 *
 * Only the kinds some weapon in this game is, because a kind nothing is can
 * never be in anybody's hand and a row for it is a row about a fight that
 * cannot happen. A project can easily hold more: the weapon shelf mints a kind
 * beside every weapon it adds, and a weapon deleted afterwards leaves its kind
 * behind.
 *
 * A kind that is already written to beat something is drawn even when nothing
 * is one, because it is a live rule: hiding it would leave an author holding
 * an edge they can neither see nor clear. It is named as unused beneath the
 * grid rather than quietly listed.
 */
const shown = computed(() =>
  props.weaponTypes.filter(
    (type) =>
      inPlay.value.has(type.id) || (type.strongAgainst ?? []).length > 0
  )
);

/** Kinds drawn only because they carry an edge, which nothing can ever use. */
const idle = computed(() =>
  shown.value.filter((type) => !inPlay.value.has(type.id))
);

const enough = computed(() => shown.value.length >= 2);

/** Whether one kind is written to beat another. */
function beats(one: SourceWeaponType, other: SourceWeaponType): boolean {
  return (one.strongAgainst ?? []).includes(other.id);
}

function copyTypes(): SourceWeaponType[] {
  return props.weaponTypes.map((type) => ({
    ...type,
    ...(type.strongAgainst ? { strongAgainst: [...type.strongAgainst] } : {})
  }));
}

/**
 * Tick or clear one edge.
 *
 * An edge is directed and the two directions are one choice: a kind that beats
 * another cannot also lose to it, so ticking one direction clears the other
 * rather than leaving the pair contradicting itself. The engine reads at most
 * one of the two, so a pair ticked both ways would silently be read one way.
 */
function setEdge(oneId: string, otherId: string, on: boolean) {
  const types = copyTypes();
  const write = (type: SourceWeaponType, target: string, wanted: boolean) => {
    const held = new Set(type.strongAgainst ?? []);
    if (wanted) held.add(target);
    else held.delete(target);
    if (held.size === 0) delete type.strongAgainst;
    else type.strongAgainst = [...held];
  };
  const one = types.find((type) => type.id === oneId);
  const other = types.find((type) => type.id === otherId);
  if (!one || !other) return;
  write(one, otherId, on);
  if (on) write(other, oneId, false);
  emit("updateTypes", types);
}

/**
 * The classic case in one press: each kind beats the next, and the last beats
 * the first. Any number of kinds makes a ring; three of them make the triangle
 * everybody means when they say one.
 *
 * The ring runs through the kinds this game's weapons actually are, and every
 * other kind is left beating nothing. This press states the whole rule, so a
 * kind nothing is has no place in it, and an edge left over from a kind whose
 * last weapon was deleted would be a rule nobody could see firing.
 */
function makeRing() {
  keystrokes.flush();
  const types = copyTypes();
  const ring = types.filter((type) => inPlay.value.has(type.id));
  for (const type of types) delete type.strongAgainst;
  ring.forEach((type, index) => {
    const next = ring[(index + 1) % ring.length];
    if (next && next.id !== type.id) type.strongAgainst = [next.id];
  });
  emit("updateTypes", types);
  if (props.advantage === undefined) {
    // A ring with nothing behind it is a rule that never fires, so the press
    // that draws one also states what it is worth. These are Fire Emblem's own
    // numbers, and they are the author's to change the moment they are written.
    emit("updateAdvantage", { damage: 1, accuracy: 15 });
  }
}

function clearRing() {
  keystrokes.flush();
  const types = copyTypes();
  for (const type of types) delete type.strongAgainst;
  emit("updateTypes", types);
}

function saveAdvantage(part: "damage" | "accuracy", raw: string) {
  const parsed = Number.parseInt(raw.trim(), 10);
  const held = props.advantage ?? { damage: 0, accuracy: 0 };
  const next = {
    damage: part === "damage" ? parsed : held.damage,
    accuracy: part === "accuracy" ? parsed : held.accuracy
  };
  if (Number.isNaN(next.damage) || Number.isNaN(next.accuracy)) return;
  emit("updateAdvantage", next);
}

/** The rule as a sentence, which is the thing an author is really writing. */
const spelled = computed(() => {
  const named = (id: string) =>
    props.weaponTypes.find((type) => type.id === id)?.name ?? id;
  const edges = props.weaponTypes.flatMap((type) =>
    (type.strongAgainst ?? []).map(
      (other) => `${type.name} beats ${named(other)}`
    )
  );
  return edges;
});

/**
 * What is wrong with the rule as it stands, in the author's words.
 *
 * Both halves are needed for anything to happen, and stating one without the
 * other is the mistake this panel exists to make visible: the compiler accepts
 * both, and a rule that never fires looks exactly like a rule that works until
 * somebody plays the game.
 */
const problems = computed(() => {
  const found: string[] = [];
  const anyEdges = spelled.value.length > 0;
  const worthSomething =
    props.advantage !== undefined &&
    (props.advantage.damage !== 0 || props.advantage.accuracy !== 0);
  if (anyEdges && !worthSomething) {
    found.push(
      "Nothing is at stake: one kind beats another, but the better weapon is " +
      "worth nothing. Say what it is worth, or clear the ticks."
    );
  }
  if (!anyEdges && worthSomething) {
    found.push(
      "Nothing is decided: the better weapon is worth something, but no kind " +
      "beats any other. Tick what beats what, or clear the numbers."
    );
  }
  return found;
});
</script>

<template>
  <section class="triangle-editor" aria-labelledby="weapon-triangle-title">
    <h3 id="weapon-triangle-title">Weapon triangle</h3>
    <p class="field-help">
      Which kinds of weapon beat which, and what holding the better one is
      worth. One rule for the whole game, so a player can hold it in their head.
    </p>

    <p v-if="!enough" class="triangle-warning" role="status">
      A triangle needs weapons of at least two different kinds. Add another
      weapon of a kind this game does not have yet and this fills in.
    </p>

    <template v-else>
      <div class="triangle-presets">
        <button type="button" @click="makeRing">
          Make a ring: each beats the next
        </button>
        <button type="button" class="secondary" @click="clearRing">
          Clear every tick
        </button>
      </div>

      <table class="triangle-grid">
        <caption class="field-help">
          Tick a box to say the kind on the left beats the kind above.
        </caption>
        <thead>
          <tr>
            <td />
            <th v-for="column in shown" :key="column.id" scope="col">
              {{ column.name }}
            </th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in shown" :key="row.id">
            <th scope="row">{{ row.name }}</th>
            <td v-for="column in shown" :key="column.id">
              <!-- A kind cannot beat itself: the same edge would be read once
                   in each direction and price every mirror match twice. -->
              <span v-if="row.id === column.id" class="triangle-self">—</span>
              <input v-else type="checkbox"
                :id="`beats-${row.id}-${column.id}`"
                :aria-label="`${row.name} beats ${column.name}`"
                :checked="beats(row, column)"
                @change="setEdge(
                  row.id, column.id,
                  ($event.target as HTMLInputElement).checked
                )">
            </td>
          </tr>
        </tbody>
      </table>

      <label for="triangle-damage">Extra damage for the better weapon</label>
      <input id="triangle-damage" type="number" min="0" max="999" step="1"
        :value="keystrokes.shown('damage', String(advantage?.damage ?? ''))"
        @input="keystrokes.type(
          'damage', ($event.target as HTMLInputElement).value,
          (typed) => saveAdvantage('damage', typed)
        )"
        @change="keystrokes.leave(
          'damage', ($event.target as HTMLInputElement).value,
          (typed) => saveAdvantage('damage', typed)
        )">
      <label for="triangle-accuracy">Extra accuracy, in percent</label>
      <input id="triangle-accuracy" type="number" min="0" max="100" step="1"
        :value="keystrokes.shown('accuracy', String(advantage?.accuracy ?? ''))"
        @input="keystrokes.type(
          'accuracy', ($event.target as HTMLInputElement).value,
          (typed) => saveAdvantage('accuracy', typed)
        )"
        @change="keystrokes.leave(
          'accuracy', ($event.target as HTMLInputElement).value,
          (typed) => saveAdvantage('accuracy', typed)
        )">
      <p class="field-help">
        Taken off a strike made the other way round, so a bad match-up costs
        exactly what a good one pays.
      </p>

      <p v-if="spelled.length === 0" class="field-help" role="status">
        Nothing beats anything yet, so every weapon is as good as every other.
      </p>
      <ul v-else class="triangle-spelled" role="status">
        <li v-for="line in spelled" :key="line">{{ line }}</li>
      </ul>

      <ul v-if="idle.length" class="triangle-warning" role="status">
        <li v-for="type in idle" :key="type.id">
          Nothing in this game is {{ type.name }}, so nothing it beats can ever
          happen. It is drawn because it is already written to beat something;
          untick that, or give some weapon this kind.
        </li>
      </ul>

      <ul v-if="problems.length" class="triangle-warning" role="alert">
        <li v-for="problem in problems" :key="problem">{{ problem }}</li>
      </ul>
    </template>
  </section>
</template>
