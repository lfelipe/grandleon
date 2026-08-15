// SPDX-License-Identifier: MIT
// Which gesture an attack is drawn as, folded over the records a presenter
// already holds.
//
// `platform/view/include/grandleon/view/motion.hpp` owns the *rule*. A blow
// magic could have thrown is a cast; otherwise a weapon threw it, and a weapon
// that cannot strike an adjacent tile threw a shot. It owns that rule in pure
// integers, because that header may not include an engine header and must
// compile for a renderer that has never heard of an ability. This is the other
// half: the fold that turns a striker's own snapshot and the ability records
// into the two booleans that rule asks for.
//
// It lives here, in one file, for the reason the cell selection lives in one
// file. Two presenters in this repository draw attacks from engine events: the
// shared `TurnClient` a console compiles, and the Nintendo 64's own. A fold
// copied into both is two chances for the same blow to be drawn as two
// different gestures on two machines. It is also what the host derivations
// call, so the expectation a console check is measured against is computed by
// the same code the console draws with.
//
// Nothing here reads a command, a decision, or anything the simulation added to
// say what should be drawn. The engine reports an attack as who struck, who was
// struck and where; it does not say whether a weapon or a spell threw it, and
// this is the derivation that exists so it never has to.

#pragma once

#include <grandleon/simulation/encounter.hpp>
#include <grandleon/view/motion.hpp>

#include <vector>

namespace grandleon::client {

// How far apart two tokens stand, on the engine's own metric: orthogonal steps,
// `|dx| + |dy|`. Restated rather than shared because the engine's `distance` is
// internal to its translation unit. It is pinned against the reach bands the
// shipped content authors in `tests/view/motion_test.cpp`, so a band read out
// of a record here means what the same band means when the engine tests it.
[[nodiscard]] inline int separation_between(
    simulation::Position lhs, simulation::Position rhs
) noexcept {
    const int dx = lhs.x - rhs.x;
    const int dy = lhs.y - rhs.y;
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

// Whether any damaging magical ability `striker` knows has a reach band
// covering `separation`.
//
// Two filters, and both are deliberate:
//
//   * **Damaging only.** Mending somebody is not an attack and never reaches a
//     gesture, but a unit's ability list holds restoring abilities beside
//     damaging ones and the distinction is the record's to make.
//   * **Magical only.** Power Strike and Iron Vow are abilities a body throws
//     with its arm. Drawing the hardest swing in the guard's book as a spell
//     would be exactly the error this derivation exists to avoid.
[[nodiscard]] inline bool magic_reaches(
    const simulation::UnitSnapshot& striker,
    const std::vector<simulation::AbilityDefinition>& abilities,
    int separation
) {
    for (const simulation::ContentId ability_id : striker.ability_ids) {
        for (const simulation::AbilityDefinition& ability : abilities) {
            if (ability.id != ability_id) continue;
            if (ability.kind != simulation::AbilityKind::damage) break;
            if (ability.damage_type != simulation::DamageType::magical) break;
            if (view::reach_covers(
                    separation, ability.minimum_reach, ability.maximum_reach
                )) {
                return true;
            }
            break;
        }
    }
    return false;
}

// The gesture a blow between these two is drawn as. A null striker is a blow
// from nobody on the board, an area effect with no origin. Slice 1 already
// draws it as a flash alone, and this answers for it rather than leaving a
// renderer to guess.
[[nodiscard]] inline view::AttackGesture gesture_for(
    const simulation::UnitSnapshot* striker,
    const std::vector<simulation::AbilityDefinition>& abilities,
    int separation
) {
    if (striker == nullptr) {
        return view::attack_gesture(separation, false, false);
    }
    // `minimum_reach` is the engine's own resolution of the weapon in hand, so
    // "cannot strike an adjacent tile" is read rather than recomputed. No
    // weapon record is looked up here at all: the one number this question needs
    // is already on the snapshot.
    return view::attack_gesture(
        separation, magic_reaches(*striker, abilities, separation),
        striker->minimum_reach > 1
    );
}

}  // namespace grandleon::client
