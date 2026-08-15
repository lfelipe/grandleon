// SPDX-License-Identifier: MIT
#include <grandleon/tactics/policy.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace grandleon::tactics {
namespace {

using simulation::EncounterSnapshot;
using simulation::Position;
using simulation::UnitSnapshot;

std::uint32_t distance(Position lhs, Position rhs) noexcept {
    const auto dx = static_cast<std::int32_t>(lhs.x) - rhs.x;
    const auto dy = static_cast<std::int32_t>(lhs.y) - rhs.y;
    return static_cast<std::uint32_t>(
        (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy)
    );
}

const UnitSnapshot* find(
    const EncounterSnapshot& snapshot,
    simulation::UnitId id
) noexcept {
    for (const UnitSnapshot& unit : snapshot.units) {
        if (unit.id == id) return &unit;
    }
    return nullptr;
}

// The nearest hostile unit standing on the board, ties broken by the lowest
// identifier so the choice cannot depend on snapshot ordering.
//
// `simulation::on_board` again, and here it decides where a unit walks: a
// character talked off the board leaves a stale `position` behind, and chasing
// it would march the whole side at a tile nobody is on. A wave still to come is
// worse: it is somewhere the engine will not let anybody be aimed at, and its
// authored landing is not a place it is.
const UnitSnapshot* nearest_enemy(
    const EncounterSnapshot& snapshot,
    const UnitSnapshot& actor
) noexcept {
    const UnitSnapshot* best = nullptr;
    std::uint32_t best_distance = 0;
    for (const UnitSnapshot& candidate : snapshot.units) {
        if (!simulation::on_board(candidate) ||
            candidate.side == actor.side) {
            continue;
        }
        const std::uint32_t away = distance(actor.position, candidate.position);
        if (best == nullptr || away < best_distance ||
            (away == best_distance && candidate.id < best->id)) {
            best = &candidate;
            best_distance = away;
        }
    }
    return best;
}

// The tile this unit can walk to that gets closest to `goal`.
//
// Where it may go is not decided here. `simulation::movement_field` is the
// engine's one definition of that (what the ground admits, what it charges,
// who is a wall and who is merely in the way) and this asks it rather than
// keeping a second copy that could come to a different answer than the move
// command it is about to propose. Policy proposes; the rule stays where the
// rule is. It is asked with `actor.side`, so a unit files past its own fellows
// and is stopped by the opposition, exactly as the accepted command will be.
//
// What is decided here is only which of those tiles to take: "closest" is
// straight-line separation from the goal, so a unit whose way is blocked walks
// to the near bank and stops there rather than rounding an obstacle it cannot
// see past. That is the same greedy rule this applies to a line of opponents,
// and ground can be a wall too. Its own side is neither, and it files
// through. It does not weigh price against progress: a tile one closer is
// taken whether the ground charged one to reach it or four, because the
// allowance is spent either way and nothing is saved by hoarding it.
//
// Ties go to the lowest cell index, which is row-major order, so the choice is
// a fact about the board rather than about the order it was walked in.
bool step_toward(
    const EncounterSnapshot& snapshot,
    const UnitSnapshot& actor,
    Position goal,
    Position& chosen
) {
    if (actor.movement == 0) return false;
    const std::size_t cells =
        static_cast<std::size_t>(snapshot.width) * snapshot.height;
    if (cells == 0) return false;
    const std::vector<std::uint32_t> spent = simulation::movement_field(
        snapshot, actor.position, actor.movement, actor.crossings, actor.side
    );

    bool found = false;
    std::uint32_t best_distance = distance(actor.position, goal);
    for (std::uint16_t y = 0; y < snapshot.height; ++y) {
        for (std::uint16_t x = 0; x < snapshot.width; ++x) {
            const std::size_t slot =
                static_cast<std::size_t>(y) * snapshot.width + x;
            if (spent[slot] == simulation::unreachable_cost) continue;
            const Position next{
                static_cast<std::int16_t>(x),
                static_cast<std::int16_t>(y)
            };
            // The tile it is already standing on is where it would end up by
            // not moving at all, so it is never a proposal.
            if (next == actor.position) continue;
            const std::uint32_t away = distance(next, goal);
            if (away < best_distance) {
                best_distance = away;
                chosen = next;
                found = true;
            }
        }
    }
    return found;
}

// Membership test for an enumerated area shape, mirroring the simulation's own
// so that a proposal aims where the rule will actually land.
bool covered_by(
    simulation::AreaShape shape,
    std::uint8_t radius,
    Position centre,
    Position candidate
) noexcept {
    const std::uint32_t separation = distance(centre, candidate);
    switch (shape) {
        case simulation::AreaShape::single: return separation == 0U;
        case simulation::AreaShape::cross: return separation <= 1U;
        case simulation::AreaShape::diamond: return separation <= radius;
    }
    return false;
}

const simulation::AbilityDefinition* find_ability(
    const std::vector<simulation::AbilityDefinition>& abilities,
    simulation::ContentId id
) noexcept {
    for (const simulation::AbilityDefinition& ability : abilities) {
        if (ability.id == id) return &ability;
    }
    return nullptr;
}

// Health swung in `actor`'s favour by one landed effect on `affected`, in the
// currency every candidate is compared in: what the rule would really deliver,
// so overkill and overhealing count for nothing.
std::int32_t swing(
    const simulation::AbilityDefinition& ability,
    const UnitSnapshot& actor,
    const UnitSnapshot& affected
) noexcept {
    const bool friendly = affected.side == actor.side;
    if (ability.kind == simulation::AbilityKind::restore) {
        const std::int32_t restored = std::min<std::int32_t>(
            ability.power, affected.maximum_health - affected.health
        );
        return friendly ? restored : -restored;
    }
    // A damaging cast takes nothing off the caster's own side, so an ally
    // standing in the blast is worth exactly nothing: neither a gain nor a
    // cost. The number here has to be what the rule really delivers, and the
    // rule delivers zero. Charging the cast for a splash that never touches
    // this side would have it walk the long way round its own line to avoid
    // one.
    if (friendly) return 0;
    // The caster's own contribution, exactly as the simulation prices it: a
    // magical cast adds the caster's magic against resistance, a physical one
    // adds nothing against defence. A policy that priced a magical cast by its
    // power alone would undervalue every caster that had grown, and would be a
    // second damage formula the engine never agreed to.
    const bool magical =
        ability.damage_type == simulation::DamageType::magical;
    const std::int16_t mitigation =
        magical ? affected.resistance : affected.defense;
    const std::int16_t offence = magical ? actor.magic : 0;
    const std::int32_t damage = std::max<std::int32_t>(
        1, static_cast<std::int32_t>(offence) +
               static_cast<std::int32_t>(ability.power) - mitigation
    );
    // What the cast would really take, which is never more health than the unit
    // can be brought down through. A character with a health floor of one can be
    // taken to one and no further, so a cast aimed at somebody already standing
    // there is worth nothing and the policy is not tempted to spend an
    // activation on it. `simulation::floor_of` is the same function the rule
    // itself clamps with, so this cannot come to disagree with what the cast
    // would deliver.
    const std::int32_t landed = std::min<std::int32_t>(
        damage, affected.health - simulation::floor_of(affected)
    );
    if (landed <= 0) return 0;
    return landed;
}

struct Cast final {
    simulation::ContentId ability_id{};
    Position destination{};
    std::int32_t score{0};
};

// The best cast the actor could make from where it stands, or a zero score
// when nothing it knows is worth aiming anywhere.
Cast best_cast(
    const EncounterSnapshot& snapshot,
    const UnitSnapshot& actor,
    const std::vector<simulation::AbilityDefinition>& abilities
) {
    Cast best;
    for (const simulation::ContentId id : actor.ability_ids) {
        const simulation::AbilityDefinition* ability =
            find_ability(abilities, id);
        if (ability == nullptr) continue;
        for (std::uint16_t y = 0; y < snapshot.height; ++y) {
            for (std::uint16_t x = 0; x < snapshot.width; ++x) {
                const Position centre{
                    static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)
                };
                const std::uint32_t away = distance(actor.position, centre);
                if (away < ability->minimum_reach ||
                    away > ability->maximum_reach) {
                    continue;
                }
                std::int32_t score = 0;
                for (const UnitSnapshot& affected : snapshot.units) {
                    // Exactly who `Encounter::apply` sweeps into the area: a
                    // cast names a tile, and somebody who is not on the board
                    // is not standing in it. Scoring them would price a cast by
                    // health it is never going to move.
                    if (!simulation::on_board(affected)) continue;
                    if (!covered_by(
                            ability->area,
                            ability->radius,
                            centre,
                            affected.position
                        )) {
                        continue;
                    }
                    score += swing(*ability, actor, affected);
                }
                if (score > best.score) best = {id, centre, score};
            }
        }
    }
    return best;
}

const simulation::WeaponDefinition* find_weapon(
    const std::vector<simulation::WeaponDefinition>& weapons,
    simulation::ContentId id
) noexcept {
    for (const simulation::WeaponDefinition& weapon : weapons) {
        if (weapon.id == id) return &weapon;
    }
    return nullptr;
}

// The best strike the actor could make from where it stands: every living
// opponent inside every weapon it carries, priced as a trade. `weapon_id` is
// zero when the winner is the weapon in hand, so the plan says nothing a
// caller did not need to hear.
struct Strike final {
    const UnitSnapshot* target{nullptr};
    simulation::ContentId weapon_id{};
    std::int32_t score{0};
    // Separation from the winner, kept only as the tie-break below.
    std::uint32_t range{0};
};

Strike best_strike(
    const EncounterSnapshot& snapshot,
    const UnitSnapshot& actor,
    const std::vector<simulation::WeaponDefinition>& weapons
) {
    Strike best;
    const auto consider = [&](simulation::ContentId weapon_id,
                              std::uint8_t minimum_reach,
                              std::uint8_t maximum_reach) {
        // Every opponent in the band is priced, not merely the nearest one.
        // Counters are what make that worth doing: "which of the two people
        // next to me do I hit" is the decision the counter rule creates, and
        // answering it by proximity would be answering a different question.
        // Without counters the nearest would be as good as any other and the
        // shortcut would cost nothing.
        for (const UnitSnapshot& target : snapshot.units) {
            // Who may be struck is `simulation::on_board`, asked of the engine.
            // A departed or unarrived character passes `health > 0` and is
            // refused `target_departed` or `target_unarrived` the moment the
            // proposal reaches `apply`. Both drivers answer a refusal by
            // waiting, so a policy that proposed one threw the activation away
            // and did it again next turn.
            if (!simulation::on_board(target) || target.side == actor.side) {
                continue;
            }
            const std::uint32_t away =
                distance(actor.position, target.position);
            if (away < minimum_reach || away > maximum_reach) continue;
            // And the engine gets the last word. A forecast that refuses is a
            // strike `apply` refuses for exactly the same reason, so it is not
            // a candidate at all. Scoring such a candidate zero and keeping it
            // would propose a command the engine has just called illegal: the
            // incumbent test below accepts anything while nothing has been
            // chosen, so the first refused candidate in the band would win
            // outright whenever no legal one beat a score of nothing.
            const auto forecast = simulation::forecast_attack(
                snapshot, actor.id, target.id, weapons, weapon_id
            );
            if (!forecast) continue;
            const std::int32_t removed =
                target.health - forecast.target_health_after;
            // A strike is a trade, so it is scored as one: health taken
            // off the opponent less health the counter takes back, both
            // counted as the rule would really deliver them. The counter is
            // read off the forecast rather than re-derived, so the policy
            // cannot come to disagree with the engine about what an attack
            // costs.
            //
            // Two behaviours fall out of the arithmetic rather than out of a
            // special case, and both are the ones a player would expect. A
            // lethal strike is answered by nobody, so finishing a wounded
            // opponent outscores wounding a fresh one. And a weapon that
            // outreaches the defender's band is answered by nobody either, so
            // a bow shoots from two and a dagger does not walk in beside a
            // spear.
            const std::int32_t taken =
                actor.health - forecast.attacker_health_after;
            const std::int32_t net = removed - taken;
            // Determinism, in priority order: the better trade, then the
            // nearer target, then the incumbent. Because opponents are visited
            // in ascending identifier order and the weapon in hand is offered
            // first, holding the incumbent on a full tie is what keeps the
            // lowest identifier and the earliest-carried weapon winning every
            // tie.
            const bool better =
                best.target == nullptr || net > best.score ||
                (net == best.score && away < best.range);
            if (better) best = {&target, weapon_id, net, away};
        }
    };
    // The weapon in hand is the first candidate and is named by its absence,
    // which is both the tie-break the specification states and the reason a
    // one-weapon unit's proposal is byte-for-byte what it always was.
    consider(0, actor.minimum_reach, actor.maximum_reach);
    for (std::size_t index = 1; index < actor.weapon_ids.size(); ++index) {
        const simulation::WeaponDefinition* weapon =
            find_weapon(weapons, actor.weapon_ids[index]);
        if (weapon == nullptr) continue;
        consider(weapon->id, weapon->minimum_reach, weapon->maximum_reach);
    }
    return best;
}

Plan attack(
    const UnitSnapshot& actor,
    const UnitSnapshot& target,
    simulation::ContentId weapon_id
) {
    Plan plan;
    plan.actionable = true;
    plan.command.type = simulation::CommandType::attack;
    plan.command.unit_id = actor.id;
    plan.command.target_id = target.id;
    plan.command.weapon_id = weapon_id;
    return plan;
}

Plan wait(const UnitSnapshot& actor) {
    Plan plan;
    plan.actionable = true;
    plan.command.type = simulation::CommandType::wait;
    plan.command.unit_id = actor.id;
    return plan;
}

Plan move(const UnitSnapshot& actor, Position destination) {
    Plan plan;
    // A character walks once per turn, and a proposal to walk a second time is
    // one the engine refuses by name every time it is made. Answered here
    // rather than left to the caller's fallback, because this is policy
    // deciding what to *propose*: a unit that has spent its walk and cannot
    // reach anybody has nothing to do but stop, and proposing the walk anyway
    // is what turned a greedy replay into four thousand refusals.
    //
    // It matters more under `side_blocks`, where turns interleave: a
    // character that walked stays unfinished and comes back round to the
    // driver, so this is the branch that turns it into the wait that closes it
    // rather than into a refusal.
    if (actor.has_moved) return wait(actor);
    plan.actionable = true;
    plan.command.type = simulation::CommandType::move;
    plan.command.unit_id = actor.id;
    plan.command.destination = destination;
    return plan;
}

Plan cast(const UnitSnapshot& actor, const Cast& chosen) {
    Plan plan;
    plan.actionable = true;
    plan.command.type = simulation::CommandType::ability;
    plan.command.unit_id = actor.id;
    plan.command.destination = chosen.destination;
    plan.command.ability_id = chosen.ability_id;
    return plan;
}

}  // namespace

std::string_view behavior_name(Behavior behavior) noexcept {
    switch (behavior) {
        case Behavior::hold: return "hold";
        case Behavior::patrol: return "patrol";
        case Behavior::pursue: return "pursue";
    }
    return "unknown";
}

Plan decide(
    const EncounterSnapshot& snapshot,
    simulation::UnitId unit_id,
    Behavior behavior,
    const std::vector<Position>& patrol_points
) {
    return decide(snapshot, unit_id, behavior, patrol_points, {});
}

Plan decide(
    const EncounterSnapshot& snapshot,
    simulation::UnitId unit_id,
    Behavior behavior,
    const std::vector<Position>& patrol_points,
    const std::vector<simulation::AbilityDefinition>& abilities
) {
    return decide(snapshot, unit_id, behavior, patrol_points, abilities, {});
}

Plan decide(
    const EncounterSnapshot& snapshot,
    simulation::UnitId unit_id,
    Behavior behavior,
    const std::vector<Position>& patrol_points,
    const std::vector<simulation::AbilityDefinition>& abilities,
    const std::vector<simulation::WeaponDefinition>& weapons
) {
    Plan plan;
    if (snapshot.outcome != simulation::Outcome::ongoing) return plan;
    const UnitSnapshot* actor = find(snapshot, unit_id);
    // Who may act is `simulation::on_board`, which is `apply`'s own actor gate.
    // A driver that walks the roster by health alone will hand this a character
    // talked off the board or a wave that has not landed; every command either
    // could make is refused by name, so the honest answer is no plan rather
    // than a plan the engine throws away.
    if (actor == nullptr || !simulation::on_board(*actor)) return plan;
    if (actor->side != snapshot.active_side) return plan;

    // Every behaviour strikes what it can already reach. A unit that walks past
    // an enemy it could have hit reads as broken rather than as characterful,
    // and that stays true even where a strike can be a losing trade: standing
    // still to avoid a counter deadlocks two armies that can each only lose by
    // moving. So the worst strike still beats waiting, and what the counter
    // decides is *which* strike, and how often a cast wins instead.
    //
    // The strike is priced first so a cast has something to beat: an ability is
    // worth spending the activation on only when it swings more health than the
    // best weapon nets. Abilities provoke no counter, so a cast is compared
    // against a number that can be negative, hence the guard that the winning
    // cast is a real ability rather than the empty one a zero score would have
    // let through.
    const Strike strike = best_strike(snapshot, *actor, weapons);
    const Cast chosen = best_cast(snapshot, *actor, abilities);
    if (chosen.ability_id != 0 && chosen.score > strike.score) {
        return cast(*actor, chosen);
    }
    if (strike.target != nullptr) {
        return attack(*actor, *strike.target, strike.weapon_id);
    }

    Position destination{};
    switch (behavior) {
        case Behavior::hold:
            break;
        case Behavior::pursue: {
            const UnitSnapshot* enemy = nearest_enemy(snapshot, *actor);
            if (enemy != nullptr &&
                step_toward(snapshot, *actor, enemy->position, destination)) {
                return move(*actor, destination);
            }
            break;
        }
        case Behavior::patrol: {
            if (patrol_points.empty()) break;
            // The leg is chosen from the activation count rather than from
            // stored progress, so the behaviour needs no mutable state and
            // replays identically.
            const std::size_t leg = static_cast<std::size_t>(
                snapshot.activation_count % patrol_points.size()
            );
            const Position goal = patrol_points[leg];
            if (actor->position == goal) break;
            if (step_toward(snapshot, *actor, goal, destination)) {
                return move(*actor, destination);
            }
            break;
        }
    }
    return wait(*actor);
}

}  // namespace grandleon::tactics
