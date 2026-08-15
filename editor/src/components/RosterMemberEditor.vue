<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
// The people a campaign keeps, edited wherever a campaign writes them down:
// the company it begins with, and the recruits a node hands over when it is
// done. Both are the same record, on purpose: a recruit is a member of the
// company from the moment they join, so both are edited by this one list.
//
// Every change is emitted whole and the parent decides where it lands: the
// workspace saves the founding company through the project session, the flow
// editor keeps a node's recruits in its own draft until the flow is saved.
import { computed, ref } from "vue";
import type {
  CampaignMemberSpecificity,
  CampaignRosterMember,
  StatDeltaBlock
} from "../generated/source-v1";
import { useKeystrokeDraft } from "./keystroke-draft";

const props = defineProps<{
  members: readonly CampaignRosterMember[];
  unitTypes: readonly { readonly id: string; readonly name: string }[];
  /** Distinguishes control ids when several member lists share a page. */
  idPrefix: string;
  heading: string;
  help: string;
  /** The word for one entry: a "member" of a company, a "recruit" at a node. */
  memberWord: string;
  /**
   * Identities the same campaign already spends elsewhere. A member's identity
   * is unique across the whole campaign, not merely across this one list, so a
   * clash with a recruit written on some other node is still a clash.
   */
  otherIds?: readonly string[];
}>();

const emit = defineEmits<{
  update: [members: CampaignRosterMember[]];
  createUnitType: [];
  /** A keystroke that is not in the project yet, so the header can say so. */
  dirty: [];
}>();

/**
 * The free text and the numbers on a member, while they are being typed.
 *
 * Every `update` this list emits is a whole new roster the surface above writes
 * into the project, and the project session deep-copies the game and pushes an
 * undo entry to take it. A character per undo step would be both slow and
 * wrong, so a keystroke is held here, announced as unsaved and drawn by the
 * control that owns it, and one edit is committed when the field is left or a
 * Save reaches `flush`.
 *
 * The numbers are held the same way and for a further reason: a difference over
 * a stat line passes through "-" and through "1" on the way to "-12", and
 * neither is something to write down. Half a number is not a number.
 */
const keystrokes = useKeystrokeDraft(() => emit("dirty"));

defineExpose({ flush: keystrokes.flush });

// One search box for the list rather than one per member: the choice is the
// same project-wide roll of characters every time, and a member's own choice
// is never filtered away, so searching cannot hide what is already chosen.
const search = ref("");

const takenIds = computed(() => props.otherIds ?? []);

type StatName = keyof StatDeltaBlock;

/**
 * The stat line a difference is written over, in the order a class writes it,
 * under the same names the class editor uses: an author reading "Actions per
 * turn" on a character is reading the field they set on the class.
 */
const statFields: readonly { readonly key: StatName; readonly label: string }[] = [
  { key: "health", label: "Health" },
  { key: "movement", label: "Movement" },
  { key: "strength", label: "Strength" },
  { key: "defense", label: "Defense" },
  { key: "resistance", label: "Resistance" },
  { key: "skill", label: "Skill" },
  { key: "luck", label: "Luck" },
  { key: "evasion", label: "Evasion" },
  { key: "magic", label: "Magic" },
  { key: "actionPoints", label: "Actions per turn" },
  { key: "speed", label: "Speed" }
];

// Which members have their differences unfolded. Held here rather than read off
// the record every render: an author who folded a member away keeps it folded
// while they type somewhere else, and one who unfolded an empty one keeps it
// open long enough to write in it.
const unfolded = ref(
  new Set(
    props.members.flatMap((member, index) => (member.specificity ? [index] : []))
  )
);

function setUnfolded(index: number, open: boolean) {
  const next = new Set(unfolded.value);
  if (open) next.add(index);
  else next.delete(index);
  unfolded.value = next;
}

/** How many differences one member carries, for the folded summary. */
function specificCount(member: CampaignRosterMember): number {
  const specificity = member.specificity;
  if (!specificity) return 0;
  return Object.keys(specificity.stats ?? {}).length +
    (specificity.rangeBonus === undefined ? 0 : 1);
}

function copyMembers(): CampaignRosterMember[] {
  return props.members.map((member) => ({ ...member }));
}

function uniqueId(ids: readonly string[]): string {
  const prefix = props.memberWord.replace(/[^a-z]/g, "") || "member";
  if (!ids.includes(prefix)) return prefix;
  let suffix = 2;
  while (ids.includes(`${prefix}_${suffix}`)) suffix += 1;
  return `${prefix}_${suffix}`;
}

function patchMember(index: number, change: Partial<CampaignRosterMember>) {
  const members = copyMembers();
  const member = members[index];
  if (!member) return;
  members[index] = { ...member, ...change };
  emit("update", members);
}

/**
 * Notes are optional, and an absent field says something different from a
 * field holding nothing: an emptied note is removed rather than blanked.
 */
function patchNotes(index: number, notes: string) {
  const members = copyMembers();
  const member = members[index];
  if (!member) return;
  if (notes.trim() === "") delete member.notes;
  else member.notes = notes;
  emit("update", members);
}

/**
 * Rewrites what makes one member more than their unit type, and emits.
 *
 * The specificity and its stat block are copied before they are changed, so a
 * member's differences are never edited in place in the list this component was
 * handed. Everything that has stopped saying anything is taken back out on the
 * way: an emptied stat block is removed, and a specificity left holding nothing
 * is removed with it. An author who has cleared every difference has said the
 * character is exactly their unit type, and an absent specificity is how that
 * is said. A member carrying an empty one claims to be specific without
 * saying how.
 */
function patchSpecificity(
  index: number,
  change: (specificity: CampaignMemberSpecificity) => void
) {
  const members = copyMembers();
  const member = members[index];
  if (!member) return;
  const specificity: CampaignMemberSpecificity = { ...member.specificity };
  if (member.specificity?.stats) {
    specificity.stats = { ...member.specificity.stats };
  }
  change(specificity);
  if (specificity.stats && Object.keys(specificity.stats).length === 0) {
    delete specificity.stats;
  }
  if (Object.keys(specificity).length === 0) delete member.specificity;
  else member.specificity = specificity;
  emit("update", members);
}

/**
 * One signed difference over the unit type's stat line. A cleared box removes
 * the stat rather than writing 0 into it: 0 is not a difference, and the two
 * say different things. A zero the author types is kept, and the problem list
 * below says why it cannot stay.
 */
function patchStat(index: number, stat: StatName, raw: string) {
  patchSpecificity(index, (specificity) => {
    const stats: StatDeltaBlock = specificity.stats ?? {};
    const parsed = Number.parseInt(raw.trim(), 10);
    if (raw.trim() === "" || Number.isNaN(parsed)) delete stats[stat];
    else stats[stat] = parsed;
    specificity.stats = stats;
  });
}

/** Extra reach on every weapon this character strikes with, or none at all. */
function patchRangeBonus(index: number, raw: string) {
  patchSpecificity(index, (specificity) => {
    const parsed = Number.parseInt(raw.trim(), 10);
    if (raw.trim() === "" || Number.isNaN(parsed)) delete specificity.rangeBonus;
    else specificity.rangeBonus = parsed;
  });
}

// Adding or removing a member renumbers the ones after it, and a held
// keystroke is keyed by position, so it is committed before the list moves
// under it.
function addMember() {
  keystrokes.flush();
  const members = copyMembers();
  const unitType = props.unitTypes[0];
  // A fresh member starts as somebody rather than as an empty form: the first
  // character in the project, under that character's own name, which is the
  // one thing an author is certain to want to change.
  members.push({
    id: uniqueId([...members.map((member) => member.id), ...takenIds.value]),
    name: unitType?.name ?? "New member",
    unitTypeId: unitType?.id ?? ""
  });
  emit("update", members);
}

function removeMember(index: number) {
  keystrokes.flush();
  const members = copyMembers();
  members.splice(index, 1);
  emit("update", members);
}

function unitTypeName(id: string): string | undefined {
  return props.unitTypes.find((unitType) => unitType.id === id)?.name;
}

/** The characters offered for one member: the search, plus their own choice. */
function unitTypeChoices(
  member: CampaignRosterMember
): readonly { readonly id: string; readonly name: string }[] {
  const query = search.value.trim().toLocaleLowerCase();
  if (query === "") return props.unitTypes;
  return props.unitTypes.filter((unitType) =>
    unitType.id === member.unitTypeId ||
    unitType.id.toLocaleLowerCase().includes(query) ||
    unitType.name.toLocaleLowerCase().includes(query)
  );
}

/** A stored character this project does not hold, named rather than hidden. */
function missingUnitType(member: CampaignRosterMember): string | undefined {
  if (member.unitTypeId === "") return undefined;
  return unitTypeName(member.unitTypeId) === undefined
    ? member.unitTypeId
    : undefined;
}

/** Everything wrong with one member, beside the member, in plain words. */
function memberProblems(
  member: CampaignRosterMember,
  index: number
): string[] {
  const problems: string[] = [];
  if (member.id === "") {
    problems.push(`Give this ${props.memberWord} an identifier: it is how a Stage names them.`);
  } else if (
    props.members.some(
      (candidate, candidateIndex) =>
        candidateIndex !== index && candidate.id === member.id
    ) ||
    takenIds.value.includes(member.id)
  ) {
    problems.push(
      `Somebody else in this campaign is already '${member.id}'. Two people ` +
      "cannot share one identifier."
    );
  }
  if (member.name.trim() === "") {
    problems.push("Give them a name: it is what a player reads.");
  }
  if (member.unitTypeId === "") {
    problems.push("Choose which character they are.");
  } else if (missingUnitType(member) !== undefined) {
    problems.push(
      `'${member.unitTypeId}' is not a character in this project. Choose ` +
      "another, or create the character."
    );
  }
  // What this character is worth beyond their unit type. Only what can be
  // judged from here is judged here: whether the sum lands inside what the
  // stat itself admits depends on the unit type's own line, which this list
  // was never handed, and is answered where the project is checked as a whole.
  const specificity = member.specificity;
  if (specificity) {
    for (const field of statFields) {
      const delta = specificity.stats?.[field.key];
      if (delta === undefined) continue;
      if (delta === 0) {
        problems.push(
          `${field.label} is 0, which changes nothing. Write the difference ` +
          "this character is worth, or leave the box empty."
        );
      } else if (!Number.isInteger(delta) || Math.abs(delta) > 32767) {
        problems.push(
          `${field.label} must be a whole number between -32767 and 32767.`
        );
      }
    }
    const bonus = specificity.rangeBonus;
    if (bonus === 0) {
      problems.push(
        "A range bonus of 0 adds nothing. Say how much further they strike, " +
        "or leave the box empty."
      );
    } else if (
      bonus !== undefined &&
      (!Number.isInteger(bonus) || bonus < 1 || bonus > 32)
    ) {
      problems.push("A range bonus is a whole number from 1 to 32.");
    }
    if (specificCount(member) === 0) {
      problems.push(
        `This ${props.memberWord} is written as more than their character ` +
        "without saying how. Write a difference, or leave it out."
      );
    }
  }
  return problems;
}
</script>

<template>
  <section class="roster-editor" :aria-labelledby="`${idPrefix}-title`">
    <h4 :id="`${idPrefix}-title`">{{ heading }}</h4>
    <p class="field-help">{{ help }}</p>

    <p v-if="unitTypes.length === 0" class="roster-warning" role="status">
      No characters yet, so there is nobody to be.
    </p>
    <template v-else>
      <label :for="`${idPrefix}-character-search`">Search characters</label>
      <input :id="`${idPrefix}-character-search`" v-model="search" type="search">
    </template>
    <button type="button" class="secondary" @click="emit('createUnitType')">
      Create related character
    </button>

    <p v-if="members.length === 0" class="roster-warning" role="status">
      No {{ memberWord }}s yet.
    </p>
    <!-- Keyed by position, never by the identifier being typed into the
         fieldset: a key that changes per keystroke remounts the input and
         throws the author's focus away. -->
    <fieldset v-for="(member, index) in members" :key="index" class="roster-member">
      <legend>
        {{ memberWord.charAt(0).toLocaleUpperCase() + memberWord.slice(1) }}
        {{ index + 1 }}
      </legend>
      <!-- `input` holds the keystroke and announces it, `change` commits it.
           Both, because they answer different questions: whether the editor
           knows there is work in progress, and when that work becomes a step an
           author can undo. -->
      <label :for="`${idPrefix}-${index}-name`">Name</label>
      <input :id="`${idPrefix}-${index}-name`"
        :value="keystrokes.shown(`${index}-name`, member.name)"
        maxlength="160" required
        @input="keystrokes.type(
          `${index}-name`,
          ($event.target as HTMLInputElement).value,
          (typed) => patchMember(index, { name: typed.trim() })
        )"
        @change="keystrokes.leave(
          `${index}-name`,
          ($event.target as HTMLInputElement).value,
          (typed) => patchMember(index, { name: typed.trim() })
        )">
      <label :for="`${idPrefix}-${index}-unit-type`">Which character they are</label>
      <select :id="`${idPrefix}-${index}-unit-type`" :value="member.unitTypeId"
        @change="patchMember(index, {
          unitTypeId: ($event.target as HTMLSelectElement).value
        })">
        <option v-if="member.unitTypeId === ''" value="" disabled>
          Choose a character
        </option>
        <option v-if="missingUnitType(member)" :value="member.unitTypeId" disabled>
          {{ missingUnitType(member) }}: not a character in this project
        </option>
        <option v-for="unitType in unitTypeChoices(member)" :key="unitType.id"
          :value="unitType.id">
          {{ unitType.name }} ({{ unitType.id }})
        </option>
      </select>
      <label :for="`${idPrefix}-${index}-id`">Identifier</label>
      <input :id="`${idPrefix}-${index}-id`"
        :value="keystrokes.shown(`${index}-id`, member.id)"
        pattern="^[a-z][a-z0-9]*(?:[._\-][a-z0-9]+)*$"
        @input="keystrokes.type(
          `${index}-id`,
          ($event.target as HTMLInputElement).value,
          (typed) => patchMember(index, { id: typed.trim() })
        )"
        @change="keystrokes.leave(
          `${index}-id`,
          ($event.target as HTMLInputElement).value,
          (typed) => patchMember(index, { id: typed.trim() })
        )">
      <!-- Eleven differences and a reach are more than most members ever want,
           so they are folded away behind a summary that says how many are
           written. Everything inside stays a real labelled control, reachable
           by tab and type alone. -->
      <details class="member-specificity" :open="unfolded.has(index)"
        @toggle="setUnfolded(index, ($event.target as HTMLDetailsElement).open)">
        <summary>
          What makes them more than their character
          <template v-if="specificCount(member) > 0">
            ({{ specificCount(member) }} written)
          </template>
        </summary>
        <p class="field-help">
          Differences over what their character says, not totals. Leave a box
          empty to say nothing; 0 is not a difference.
        </p>
        <div class="stat-deltas">
          <div v-for="stat in statFields" :key="stat.key" class="stat-delta">
            <label :for="`${idPrefix}-${index}-stat-${stat.key}`">
              {{ stat.label }}
            </label>
            <input :id="`${idPrefix}-${index}-stat-${stat.key}`" type="number"
              min="-32767" max="32767" step="1"
              :value="keystrokes.shown(
                `${index}-stat-${stat.key}`,
                String(member.specificity?.stats?.[stat.key] ?? '')
              )"
              @input="keystrokes.type(
                `${index}-stat-${stat.key}`,
                ($event.target as HTMLInputElement).value,
                (typed) => patchStat(index, stat.key, typed)
              )"
              @change="keystrokes.leave(
                `${index}-stat-${stat.key}`,
                ($event.target as HTMLInputElement).value,
                (typed) => patchStat(index, stat.key, typed)
              )">
          </div>
        </div>
        <label :for="`${idPrefix}-${index}-range-bonus`">Extra reach</label>
        <input :id="`${idPrefix}-${index}-range-bonus`" type="number" min="1"
          max="32" step="1"
          :value="keystrokes.shown(
            `${index}-range-bonus`,
            String(member.specificity?.rangeBonus ?? '')
          )"
          @input="keystrokes.type(
            `${index}-range-bonus`,
            ($event.target as HTMLInputElement).value,
            (typed) => patchRangeBonus(index, typed)
          )"
          @change="keystrokes.leave(
            `${index}-range-bonus`,
            ($event.target as HTMLInputElement).value,
            (typed) => patchRangeBonus(index, typed)
          )">
        <p class="field-help">
          Added to the far end of every weapon this person strikes with, never
          to the near end and never to an ability.
        </p>
      </details>
      <label :for="`${idPrefix}-${index}-notes`">Notes</label>
      <textarea :id="`${idPrefix}-${index}-notes`"
        :value="keystrokes.shown(`${index}-notes`, member.notes ?? '')"
        rows="2" maxlength="4096"
        @input="keystrokes.type(
          `${index}-notes`,
          ($event.target as HTMLTextAreaElement).value,
          (typed) => patchNotes(index, typed)
        )"
        @change="keystrokes.leave(
          `${index}-notes`,
          ($event.target as HTMLTextAreaElement).value,
          (typed) => patchNotes(index, typed)
        )" />
      <p class="field-help">A player never reads them.</p>
      <ul v-if="memberProblems(member, index).length" class="roster-warning"
        role="alert">
        <li v-for="problem in memberProblems(member, index)" :key="problem">
          {{ problem }}
        </li>
      </ul>
      <button type="button" class="danger" @click="removeMember(index)">
        Remove {{ memberWord }} {{ index + 1 }}
      </button>
    </fieldset>
    <button type="button" @click="addMember">Add {{ memberWord }}</button>
  </section>
</template>

<style scoped>
.roster-editor {
  display: grid;
  gap: 0.35rem;
  justify-items: start;
}
.roster-member {
  display: grid;
  gap: 0.35rem;
  justify-self: stretch;
}
.roster-warning {
  color: #8a2020;
}
.member-specificity {
  display: grid;
  gap: 0.35rem;
  justify-items: start;
}
.stat-deltas {
  display: grid;
  gap: 0.35rem 0.75rem;
  grid-template-columns: repeat(auto-fit, minmax(9rem, 1fr));
  justify-self: stretch;
}
.stat-delta {
  display: grid;
  gap: 0.15rem;
}
.stat-delta input {
  width: 100%;
}
</style>
