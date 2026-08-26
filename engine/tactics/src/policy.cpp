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

// Cannot be reached from the goal through ground this character crosses.
constexpr std::uint32_t no_way_round = 0xFFFFFFFFU;

// How far every cell is from `goal`, counted in tiles through ground this
// character can actually cross.
//
// **This is a heuristic and not a rule, and the distinction is the whole reason
// it may live here.** It answers "how far round is it", which is a question
// about the shape of the ground; it says nothing about where a character may
// go, what a step costs, or who is in the way. Those are
// `simulation::movement_field`'s and the caller still asks it for all three, so
// nothing here can propose a move the engine would refuse.
//
// It is deliberately blind to who is standing where. Occupancy is a fact about
// this instant and this is a fact about the board: routing a march around a
// character who will have moved by the time anybody gets there would make the
// path jitter every turn. The engine still refuses a walk that ends on
// somebody, because the caller still asks it.
//
// Unpriced, for the reason the caller does not weigh price against progress:
// the allowance is spent either way, and a marsh two tiles wide is still the
// short way round a mountain ten tiles long. Breadth-first in the same fixed
// neighbour order everything else here uses, so the field is a fact about the
// board rather than about the order it was walked in.
void terrain_distance_from(
    const EncounterSnapshot& snapshot,
    Position goal,
    std::uint8_t crossings,
    std::vector<std::uint32_t>& field
) {
    const std::size_t cells =
        static_cast<std::size_t>(snapshot.width) * snapshot.height;
    field.assign(cells, no_way_round);
    if (cells == 0) return;
    const auto index_of = [&snapshot](Position position) {
        return static_cast<std::size_t>(position.y) * snapshot.width +
               static_cast<std::size_t>(position.x);
    };
    const auto inside = [&snapshot](Position position) {
        return position.x >= 0 && position.y >= 0 &&
               static_cast<std::uint16_t>(position.x) < snapshot.width &&
               static_cast<std::uint16_t>(position.y) < snapshot.height;
    };
    if (!inside(goal)) return;
    // The goal itself is entered whatever stands on it: it is where the march
    // is going, and a character is standing there by definition.
    field[index_of(goal)] = 0U;
    std::vector<Position> frontier{goal};
    std::vector<Position> next_frontier;
    constexpr std::pair<std::int16_t, std::int16_t> steps[] = {
        {0, -1}, {1, 0}, {0, 1}, {-1, 0}
    };
    for (std::uint32_t away = 1; !frontier.empty(); ++away) {
        next_frontier.clear();
        for (const Position position : frontier) {
            for (const auto& [dx, dy] : steps) {
                const Position neighbour{
                    static_cast<std::int16_t>(position.x + dx),
                    static_cast<std::int16_t>(position.y + dy)
                };
                if (!inside(neighbour)) continue;
                const std::size_t slot = index_of(neighbour);
                if (field[slot] != no_way_round) continue;
                const std::size_t terrain_slot = slot;
                if (!snapshot.terrain.empty() &&
                    (terrain_slot >= snapshot.terrain.size() ||
                     !simulation::can_enter(
                         snapshot.terrain[terrain_slot], crossings
                     ))) {
                    continue;
                }
                field[slot] = away;
                next_frontier.push_back(neighbour);
            }
        }
        frontier.swap(next_frontier);
    }
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
// What is decided here is only which of those tiles to take, and "closest"
// means closest **through the ground** rather than across it.
//
// **It used to mean straight-line separation, and that was a march that could
// stop for good.** A character whose goal lay behind a wall walked up to the
// wall, and from there no tile it could reach was any nearer as the crow flies,
// so `pursue` fell through to `wait` and did so again every turn for the rest
// of the battle. Both shipped campaigns dodge it by construction: every gap in
// every wall they draw is wide and sits on the line of approach, so the detour
// always fitted inside one allowance. That is board discipline standing in for
// a pathfinder, and it protects only the boards somebody already drew carefully.
//
// Measuring round the wall instead costs one breadth-first sweep of the board
// per decision, next to the sweep `movement_field` already does, and it makes
// the greedy step honest: a tile that is nearer through the ground really is
// progress, so a character walks to the gap, through it, and on.
//
// The straight line is still the answer where there is genuinely no way round.
// A goal walled off entirely leaves every cell unreachable in the field, and
// then closing the distance as far as the ground allows is the most sensible
// thing left to do, which is exactly what the old rule did everywhere.
//
// It still does not weigh price against progress: a tile one nearer is taken
// whether the ground charged one to reach it or four, because the allowance is
// spent either way and nothing is saved by hoarding it.
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
    std::vector<std::uint32_t> round_the_ground;
    terrain_distance_from(snapshot, goal, actor.crossings, round_the_ground);
    const auto index_of = [&snapshot](Position position) {
        return static_cast<std::size_t>(position.y) * snapshot.width +
               static_cast<std::size_t>(position.x);
    };
    // Where the character stands now, measured the same way the candidates are,
    // so "nearer" compares like with like. A character standing somewhere the
    // goal cannot be reached from at all falls back to the straight line.
    const std::uint32_t standing = round_the_ground[index_of(actor.position)];
    const bool through_the_ground = standing != no_way_round;

    bool found = false;
    std::uint32_t best_distance =
        through_the_ground ? standing : distance(actor.position, goal);
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
            const std::uint32_t away =
                through_the_ground ? round_the_ground[slot]
                                   : distance(next, goal);
            if (away == no_way_round) continue;
            if (away < best_distance) {
                best_distance = away;
                chosen = next;
                found = true;
            }
        }
    }
    return found;
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
//
// **Read off the engine's own forecast, exactly as `best_strike` below reads
// `forecast_attack`.** This used to re-derive the cast formula, then to ask the
// engine for the two numbers behind it and do the clamping itself. Both were a
// policy holding a piece of a rule. Now it asks what the cast would do and
// subtracts: health removed is health the character had less the health the
// forecast says it ends on, which is the same subtraction `best_strike` makes
// and is right without this file knowing why. Overkill, a health floor, a
// spared ally and a full-health character drinking a heal all fall out of the
// forecast rather than being cases here.
//
// A cast the engine would refuse scores nothing. `best_cast` only ever asks
// about tiles inside the ability's band, so that is a belt-and-braces zero
// rather than a live path, and it is the same shape `best_strike` gives a
// refused strike.
std::int32_t swing(
    const simulation::AbilityForecast& forecast,
    const UnitSnapshot& actor,
    const UnitSnapshot& affected
) noexcept {
    if (!forecast || !forecast.covered || forecast.spared) return 0;
    const std::int32_t moved = forecast.kind == simulation::AbilityKind::restore
        ? forecast.target_health_after - affected.health
        : affected.health - forecast.target_health_after;
    // **Whose health moved is the policy's question, not the engine's.** A
    // restoring cast mends whoever is standing in it and asks no side, which is
    // the rule and is right; whether mending *that* character is worth doing is
    // exactly the judgement this file exists to make. So the forecast supplies
    // the number and the sign is applied here: health put back into the other
    // side is health this side has to take off again.
    //
    // The damaging half needs no such clause, because the rule already spares
    // the caster's own side and the forecast says so.
    if (forecast.kind == simulation::AbilityKind::restore &&
        affected.side != actor.side) {
        return -moved;
    }
    return moved;
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
                    // Who the area catches, whether they are spared and what it
                    // costs them are all the forecast's answers now. A cast
                    // names a tile, so somebody not on the board is simply not
                    // covered, and the forecast says so rather than this loop
                    // testing it.
                    score += swing(
                        simulation::forecast_ability(
                            snapshot, actor.id, id, centre, affected.id,
                            abilities
                        ),
                        actor,
                        affected
                    );
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
