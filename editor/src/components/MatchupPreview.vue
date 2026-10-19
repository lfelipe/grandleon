<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { computed, ref, watch } from "vue";
import type {
  SourceAbility,
  SourceProject,
  SourceUnitType,
  SourceWeapon,
  StatBlock
} from "../generated/source-v1";

const props = defineProps<{
  project: SourceProject;
  selectedUnitId?: string | undefined;
}>();

const unitTypes = computed(() => props.project.unitTypes ?? []);
const attackerId = ref(
  props.selectedUnitId || unitTypes.value[0]?.id || ""
);
const defenderId = ref(
  unitTypes.value[1]?.id || unitTypes.value[0]?.id || ""
);

watch(
  () => props.selectedUnitId,
  (next) => {
    if (next && unitTypes.value.some((unit) => unit.id === next)) {
      attackerId.value = next;
    }
  }
);

watch(unitTypes, (list) => {
  if (!list.some((unit) => unit.id === attackerId.value)) {
    attackerId.value = list[0]?.id ?? "";
  }
  if (!list.some((unit) => unit.id === defenderId.value)) {
    defenderId.value = list[1]?.id ?? list[0]?.id ?? "";
  }
});

const attacker = computed(() =>
  unitTypes.value.find((unit) => unit.id === attackerId.value)
);
const defender = computed(() =>
  unitTypes.value.find((unit) => unit.id === defenderId.value)
);

function statsOf(unit: SourceUnitType | undefined): StatBlock {
  const found = (props.project.classes ?? []).find(
    (entry) => entry.id === unit?.classId
  );
  return (
    found?.baseStats ?? {
      health: 1,
      movement: 1,
      strength: 0,
      defense: 0
    }
  );
}

function primaryWeapon(unit: SourceUnitType | undefined): SourceWeapon | undefined {
  const firstId = unit?.startingWeaponIds?.[0];
  if (!firstId) return undefined;
  return (props.project.weapons ?? []).find((weapon) => weapon.id === firstId);
}

function weaponBand(weapon: SourceWeapon | undefined): { min: number; max: number } {
  if (!weapon) return { min: 1, max: 1 };
  const min = weapon.minimumRange ?? 1;
  const max = weapon.maximumRange ?? weapon.range ?? 1;
  return { min, max };
}

function weaponLeanBetween(
  one: SourceWeapon | undefined,
  other: SourceWeapon | undefined
): "advantage" | "disadvantage" | "none" {
  const advantage = props.project.weaponAdvantage;
  if (!advantage || (advantage.damage === 0 && advantage.accuracy === 0)) {
    return "none";
  }
  const oneTypeId = one?.weaponTypeId;
  const otherTypeId = other?.weaponTypeId;
  if (!oneTypeId || !otherTypeId || oneTypeId === otherTypeId) return "none";
  const types = props.project.weaponTypes ?? [];
  const oneType = types.find((type) => type.id === oneTypeId);
  const otherType = types.find((type) => type.id === otherTypeId);
  const oneBeatsOther = (oneType?.strongAgainst ?? []).includes(otherTypeId);
  const otherBeatsOne = (otherType?.strongAgainst ?? []).includes(oneTypeId);
  if (oneBeatsOther && !otherBeatsOne) return "advantage";
  if (otherBeatsOne && !oneBeatsOther) return "disadvantage";
  return "none";
}

function strikeNumbers(
  actor: SourceUnitType,
  target: SourceUnitType,
  actorWeapon: SourceWeapon | undefined,
  targetWeapon: SourceWeapon | undefined
) {
  const actorStats = statsOf(actor);
  const targetStats = statsOf(target);
  const lean = weaponLeanBetween(actorWeapon, targetWeapon);
  const advantageDamage = props.project.weaponAdvantage?.damage ?? 0;
  const advantageAccuracy = props.project.weaponAdvantage?.accuracy ?? 0;
  const deltaDamage =
    lean === "advantage"
      ? advantageDamage
      : lean === "disadvantage"
        ? -advantageDamage
        : 0;
  const deltaAccuracy =
    lean === "advantage"
      ? advantageAccuracy
      : lean === "disadvantage"
        ? -advantageAccuracy
        : 0;
  const weaponPower = actorWeapon?.power ?? 0;
  const rawDamage =
    (actorStats.strength ?? 0) +
    weaponPower +
    deltaDamage -
    (targetStats.defense ?? 0);
  const damage = Math.max(1, rawDamage);
  const baseAccuracy = actorWeapon?.accuracy ?? 100;
  const rawHit =
    baseAccuracy +
    (actorStats.skill ?? 0) +
    (actorStats.luck ?? 0) +
    deltaAccuracy -
    (targetStats.evasion ?? 0) -
    (targetStats.luck ?? 0);
  const hitChance = Math.max(0, Math.min(100, rawHit));
  const targetHealth = Math.max(1, targetStats.health ?? 1);
  const blowsToDefeat = Math.ceil(targetHealth / damage);
  const band = weaponBand(actorWeapon);
  return {
    weaponName: actorWeapon?.name ?? "Unarmed",
    lean,
    damage,
    hitChance,
    blowsToDefeat,
    targetHealth,
    band
  };
}

interface AbilityPreviewEntry {
  readonly id: string;
  readonly name: string;
  readonly kind: "damage" | "restore";
  readonly amount: number;
  readonly hitChance: number;
}

function abilityExchange(
  actor: SourceUnitType,
  target: SourceUnitType
): AbilityPreviewEntry[] {
  const actorStats = statsOf(actor);
  const targetStats = statsOf(target);
  const abilities = props.project.abilities ?? [];
  return (actor.abilityIds ?? []).flatMap((abilityId): AbilityPreviewEntry[] => {
    const ability: SourceAbility | undefined = abilities.find(
      (candidate) => candidate.id === abilityId
    );
    if (!ability) return [];
    const baseHit = ability.accuracy ?? 100;
    if (ability.kind === "restore") {
      const amount = Math.max(1, (actorStats.magic ?? 0) + ability.power);
      return [{
        id: ability.id,
        name: ability.name,
        kind: "restore",
        amount,
        hitChance: 100
      }];
    }
    const isPhysical = (ability.damageType ?? "magical") === "physical";
    const amount = isPhysical
      ? Math.max(1, ability.power - (targetStats.defense ?? 0))
      : Math.max(
          1,
          (actorStats.magic ?? 0) + ability.power - (targetStats.resistance ?? 0)
        );
    const hitChance = Math.max(
      0,
      Math.min(
        100,
        baseHit +
          (actorStats.skill ?? 0) +
          (actorStats.luck ?? 0) -
          (targetStats.evasion ?? 0) -
          (targetStats.luck ?? 0)
      )
    );
    return [{
      id: ability.id,
      name: ability.name,
      kind: "damage",
      amount,
      hitChance
    }];
  });
}

const preview = computed(() => {
  if (!attacker.value || !defender.value) return undefined;
  const attackerWeapon = primaryWeapon(attacker.value);
  const defenderWeapon = primaryWeapon(defender.value);
  const forward = strikeNumbers(
    attacker.value,
    defender.value,
    attackerWeapon,
    defenderWeapon
  );
  const reply = strikeNumbers(
    defender.value,
    attacker.value,
    defenderWeapon,
    attackerWeapon
  );
  const abilities = abilityExchange(attacker.value, defender.value);
  return { forward, reply, abilities };
});
</script>

<template>
  <section v-if="unitTypes.length >= 1" class="matchup-preview"
    aria-labelledby="matchup-preview-title">
    <h3 id="matchup-preview-title">Matchup preview</h3>
    <p class="field-help">
      Compare any two characters with the weapon in their hand to see how many
      blows a fight between them takes.
    </p>
    <div class="matchup-pickers">
      <div>
        <label for="matchup-attacker">Striking character</label>
        <select id="matchup-attacker" v-model="attackerId">
          <option v-for="unit in unitTypes" :key="unit.id" :value="unit.id">
            {{ unit.name }}
          </option>
        </select>
      </div>
      <div>
        <label for="matchup-defender">Target character</label>
        <select id="matchup-defender" v-model="defenderId">
          <option v-for="unit in unitTypes" :key="unit.id" :value="unit.id">
            {{ unit.name }}
          </option>
        </select>
      </div>
    </div>

    <div v-if="preview && attacker && defender" class="matchup-results"
      role="status" aria-live="polite">
      <div class="matchup-card">
        <strong>{{ attacker.name }} → {{ defender.name }}</strong>
        <span>
          With <em>{{ preview.forward.weaponName }}</em>
          (reach {{ preview.forward.band.min }}–{{ preview.forward.band.max }}):
          <strong>{{ preview.forward.damage }} damage</strong> per hit
          ({{ preview.forward.hitChance }}% hit,
          {{ preview.forward.blowsToDefeat }}
          {{ preview.forward.blowsToDefeat === 1 ? "hit" : "hits" }} to defeat
          {{ preview.forward.targetHealth }} health).
        </span>
        <span v-if="preview.forward.lean !== 'none'" class="matchup-lean">
          Weapon triangle: {{ preview.forward.lean }}.
        </span>
        <ul v-if="preview.abilities.length > 0" class="matchup-abilities">
          <li v-for="ability in preview.abilities" :key="ability.id">
            <em>{{ ability.name }}</em>:
            <template v-if="ability.kind === 'restore'">
              restores <strong>{{ ability.amount }} health</strong>
            </template>
            <template v-else>
              <strong>{{ ability.amount }} damage</strong>
              ({{ ability.hitChance }}% hit)
            </template>
          </li>
        </ul>
      </div>

      <div class="matchup-card">
        <strong>{{ defender.name }} → {{ attacker.name }}</strong>
        <span>
          With <em>{{ preview.reply.weaponName }}</em>
          (reach {{ preview.reply.band.min }}–{{ preview.reply.band.max }}):
          <strong>{{ preview.reply.damage }} damage</strong> per hit
          ({{ preview.reply.hitChance }}% hit,
          {{ preview.reply.blowsToDefeat }}
          {{ preview.reply.blowsToDefeat === 1 ? "hit" : "hits" }} to defeat
          {{ preview.reply.targetHealth }} health).
        </span>
        <span v-if="preview.reply.lean !== 'none'" class="matchup-lean">
          Weapon triangle: {{ preview.reply.lean }}.
        </span>
      </div>
    </div>
  </section>
</template>

<style scoped>
.matchup-preview {
  margin: 0.75rem 0;
  padding: 0.75rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.65rem;
  background: #f5f7f2;
}
.matchup-preview h3 {
  margin: 0 0 0.25rem;
}
.matchup-pickers {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(12rem, 1fr));
  gap: 0.75rem;
  margin: 0.5rem 0;
}
.matchup-results {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(14rem, 1fr));
  gap: 0.75rem;
  margin-top: 0.5rem;
}
.matchup-card {
  display: grid;
  gap: 0.25rem;
  padding: 0.6rem;
  border: 1px solid #c7d2ca;
  border-radius: 0.45rem;
  background: #ffffff;
  font-size: 0.9rem;
}
.matchup-lean {
  color: #2f5d3e;
  font-weight: 600;
}
.matchup-abilities {
  margin: 0.25rem 0 0;
  padding-left: 1.1rem;
}
</style>
