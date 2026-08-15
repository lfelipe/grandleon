// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace grandleon::tactics {

// Deterministic behaviour for a unit nobody is steering.
//
// This is policy, not rules. Nothing here decides whether a command is legal;
// it only decides which command to *propose*. The simulation remains the sole
// authority and will reject a proposal it does not like, so a caller must be
// prepared for that and fall back to waiting.
enum class Behavior : std::uint8_t {
    // Never moves. Attacks anything that comes into reach.
    hold = 0,
    // Walks its patrol points in order. Attacks anything in reach on the way.
    patrol = 1,
    // Closes on the nearest living enemy and attacks when it can.
    pursue = 2,
};

[[nodiscard]] std::string_view behavior_name(Behavior behavior) noexcept;

struct Plan final {
    simulation::Command command{};
    // False when the unit cannot act at all; the caller should skip it.
    bool actionable{false};
};

// Chooses a command for `unit_id` from the given snapshot, knowing nothing of
// the encounter's abilities. Equivalent to the overload below with an empty
// ability list: the unit strikes, walks, or waits, and never casts.
//
// Determinism: candidate destinations come from `simulation::movement_field`,
// which prices the whole board and whose answer does not depend on the order it
// was walked in, and ties are broken by the lowest board index, so the same
// snapshot always yields the same command on every platform.
[[nodiscard]] Plan decide(
    const simulation::EncounterSnapshot& snapshot,
    simulation::UnitId unit_id,
    Behavior behavior,
    const std::vector<simulation::Position>& patrol_points
);

// The same choice, with the encounter's ability definitions in hand so the
// unit can cast.
//
// The casting rule is one comparison in one currency: health swung in the
// acting side's favour. Every candidate cast is scored as the health it would
// actually strip from living opponents plus the health it would actually
// return to living allies, less the health a restoring cast would hand an
// opponent. An ally caught by a damaging area is worth nothing at all, self
// included: the rules spare the caster's own side, so there is no cost to
// charge the cast for. The basic attack is scored the same way. The unit casts
// only when the best cast beats the basic attack outright, so a plain strike
// stays the default.
//
// Determinism: abilities are considered in the order the unit lists them and
// target tiles in row-major order, and a candidate replaces the incumbent only
// on a strictly higher score, so the earliest-listed ability and the
// lowest-indexed tile win every tie.
[[nodiscard]] Plan decide(
    const simulation::EncounterSnapshot& snapshot,
    simulation::UnitId unit_id,
    Behavior behavior,
    const std::vector<simulation::Position>& patrol_points,
    const std::vector<simulation::AbilityDefinition>& abilities
);

// The same choice again, with the encounter's weapon definitions in hand so
// the unit can strike with something other than the weapon it holds.
//
// Weapons are priced in the currency the casting rule already uses: the health
// a strike would actually remove from a living opponent, counting nothing past
// that opponent's last point. Within one weapon's band the target is still the
// nearest opponent, ties going to the lowest identifier, so a unit carrying one
// weapon strikes exactly what it struck before. The best carried weapon becomes
// the score a candidate cast has to beat.
//
// Determinism: the weapon in hand is considered first and the rest in carried
// order, and a candidate replaces the incumbent only on a strictly higher
// score, so the earliest-carried weapon wins every tie. When the winner is the
// weapon in hand the plan names no weapon at all, so a one-weapon unit proposes
// the identical command it proposed before weapons could be chosen.
[[nodiscard]] Plan decide(
    const simulation::EncounterSnapshot& snapshot,
    simulation::UnitId unit_id,
    Behavior behavior,
    const std::vector<simulation::Position>& patrol_points,
    const std::vector<simulation::AbilityDefinition>& abilities,
    const std::vector<simulation::WeaponDefinition>& weapons
);

}  // namespace grandleon::tactics
