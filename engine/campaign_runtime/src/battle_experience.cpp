// SPDX-License-Identifier: MIT
#include <grandleon/campaign_runtime/battle_experience.hpp>
#include <grandleon/core/random.hpp>

#include <algorithm>
#include <limits>

namespace grandleon::campaign_runtime {
namespace {

constexpr auto rules_stat_ceiling = simulation::maximum_stat;

// Add one member's earned points to one board unit, saturating rather than
// wrapping. A saturating add is the honest one here: a stat that wrapped would
// hand a player a weaker character for having grown, and there is no number
// above the ceiling to show them instead.
//
// `ceiling` is where this stat actually stops. It defaults to what the field
// holds, which is right for every stat the rules do not bound; the four they do
// bound pass `rules_stat_ceiling` and stop there instead.
template <typename Value>
void add_saturating(
    Value& target,
    std::uint16_t gain,
    std::int64_t ceiling = static_cast<std::int64_t>(
        std::numeric_limits<Value>::max()
    )
) noexcept {
    const std::int64_t held = static_cast<std::int64_t>(
        std::numeric_limits<Value>::max()
    );
    const std::int64_t stop = ceiling < held ? ceiling : held;
    const std::int64_t total =
        static_cast<std::int64_t>(target) + static_cast<std::int64_t>(gain);
    target = static_cast<Value>(total > stop ? stop : total);
}

// Add one authored delta to one board unit, saturating at both ends.
//
// The floor is `floor` rather than the storage type's minimum, because the
// numbers this lands on have meanings below which they stop being numbers: a
// character with no health is a corpse and a character with no movement cannot
// take a step. The compiler already refuses a delta that would go there, so
// this is the second of two locks on the same door: content that reached a
// console through some other tool still cannot produce a unit the rules have no
// reading for.
template <typename Value>
void add_signed_saturating(
    Value& target,
    std::int16_t delta,
    std::int64_t floor,
    std::int64_t ceiling = static_cast<std::int64_t>(
        std::numeric_limits<Value>::max()
    )
) noexcept {
    const std::int64_t held = static_cast<std::int64_t>(
        std::numeric_limits<Value>::max()
    );
    const std::int64_t stop = ceiling < held ? ceiling : held;
    const std::int64_t total =
        static_cast<std::int64_t>(target) + static_cast<std::int64_t>(delta);
    const std::int64_t landed = total > stop ? stop : total;
    target = static_cast<Value>(landed < floor ? floor : landed);
}

// Ascending by member, so the operations a batch carries, and therefore the
// order the growth stream is drawn in, depend on who earned what and never on
// what order the events happened to arrive in.
struct EarnedExperience final {
    campaign::PersistentEntityId member{};
    std::uint32_t experience{};
};

}  // namespace

// What the author made of one character, before what the campaign did.
//
// The same addition `apply_roster_join` makes below with a member's earned
// points, made one step earlier over numbers that were never earned because
// they were always true. Being an addition rather than a replacement is the
// whole of why it is here: a delta stacks on the type, so a class rebalanced
// underneath a character moves that character too, and a specificity and a
// level-up compose into their sum rather than one overwriting the other.
//
// Authored first and earned second. The order is visible only when a stat
// saturates, at which point the ceiling has been reached either way. It is
// stated and fixed anyway, because two clients that added in different orders
// would be two clients that disagreed about a character somebody could reach.
void apply_authored_specificity(
    simulation::UnitDefinition& unit,
    const package_runtime::MemberSpecificity& specificity
) noexcept {
    using package_runtime::SpecificStat;
    const auto delta = [&specificity](SpecificStat stat) -> std::int16_t {
        return specificity.stat_deltas[static_cast<std::size_t>(stat)];
    };
    // The floors are the ones each stat's own authored field admits, which is
    // the rule the compiler validates against and is repeated here rather than
    // inferred, so the two cannot drift into disagreeing about what a legal
    // character is.
    add_signed_saturating(unit.health, delta(SpecificStat::health), 1);
    add_signed_saturating(
        unit.strength, delta(SpecificStat::strength), 0, rules_stat_ceiling
    );
    add_signed_saturating(
        unit.defense, delta(SpecificStat::defense), 0, rules_stat_ceiling
    );
    add_signed_saturating(
        unit.resistance, delta(SpecificStat::resistance), 0, rules_stat_ceiling
    );
    add_signed_saturating(unit.movement, delta(SpecificStat::movement), 1);
    add_signed_saturating(
        unit.action_points, delta(SpecificStat::action_points), 1
    );
    add_signed_saturating(unit.skill, delta(SpecificStat::skill), 0);
    add_signed_saturating(unit.luck, delta(SpecificStat::luck), 0);
    add_signed_saturating(unit.evasion, delta(SpecificStat::evasion), 0);
    add_signed_saturating(
        unit.magic, delta(SpecificStat::magic), 0, rules_stat_ceiling
    );
    // Speed is delta-able although it is not growable. Growth refuses it
    // because a level-up is a roll that would reshuffle turn order inside a
    // battle the player is already standing in; an authored delta draws from no
    // stream, is fixed before the campaign is founded, and is read off the info
    // sheet before anybody commits, so turn order is computed from it once,
    // exactly as it is computed from a class's own authored speed.
    add_signed_saturating(unit.speed, delta(SpecificStat::speed), 1);
    // And what the character does with whatever they are holding. The band
    // itself is resolved by `create_encounter` from the weapon in hand, so this
    // is carried onto the unit rather than added to a band here. See
    // `simulation::UnitDefinition::reach_bonus`, which is where the reasoning
    // about never touching the shared authored weapon lives.
    unit.reach_bonus = specificity.reach_bonus;
}

void apply_roster_growth(simulation::UnitDefinition& unit,
                         const campaign::Progression& progression) noexcept {
    add_saturating(
        unit.health, progression.gain_in(campaign::GrowableStat::health)
    );
    add_saturating(
        unit.strength, progression.gain_in(campaign::GrowableStat::strength),
        rules_stat_ceiling
    );
    add_saturating(
        unit.defense, progression.gain_in(campaign::GrowableStat::defense),
        rules_stat_ceiling
    );
    add_saturating(
        unit.resistance,
        progression.gain_in(campaign::GrowableStat::resistance),
        rules_stat_ceiling
    );
    add_saturating(
        unit.movement, progression.gain_in(campaign::GrowableStat::movement)
    );
    add_saturating(
        unit.action_points,
        progression.gain_in(campaign::GrowableStat::action_points)
    );
    add_saturating(
        unit.skill, progression.gain_in(campaign::GrowableStat::skill)
    );
    add_saturating(
        unit.luck, progression.gain_in(campaign::GrowableStat::luck)
    );
    add_saturating(
        unit.evasion, progression.gain_in(campaign::GrowableStat::evasion)
    );
    add_saturating(
        unit.magic, progression.gain_in(campaign::GrowableStat::magic),
        rules_stat_ceiling
    );
}

bool retain_experience_event(
    const std::vector<simulation::Event>& retained,
    const simulation::Event& event,
    bool healing_rewards
) noexcept {
    using simulation::EventType;
    if (event.type == EventType::unit_defeated) return true;
    if (!healing_rewards) return false;
    if (event.type == EventType::unit_restored) {
        return event.amount > 0 && event.related_unit_id != 0 &&
            event.unit_id != event.related_unit_id;
    }
    if (event.type != EventType::activation_ended) return false;
    for (auto earlier = retained.rbegin(); earlier != retained.rend(); ++earlier) {
        if (earlier->type == EventType::activation_ended &&
            earlier->unit_id == event.unit_id) return false;
        if (earlier->type == EventType::unit_restored &&
            earlier->related_unit_id == event.unit_id) return true;
    }
    return false;
}

BattleProgression derive_roster_experience(
    const campaign::CampaignState& state,
    const std::vector<BoardUnitType>& board,
    const std::vector<simulation::Event>& events,
    const campaign::OutcomeSource& source,
    BattleExperienceSource lookups
) {
    BattleProgression result;
    const auto fielded = [&board](simulation::UnitId id) -> const BoardUnitType* {
        for (const auto& unit : board) {
            if (unit.unit == id) return &unit;
        }
        return nullptr;
    };
    std::vector<simulation::UnitId> fallen;
    for (const auto& event : events) {
        if (event.type == simulation::EventType::unit_defeated) {
            fallen.push_back(event.unit_id);
        }
    }

    std::vector<EarnedExperience> earned;
    std::vector<simulation::UnitId> paid_for_healing;
    for (const auto& event : events) {
        if (event.type == simulation::EventType::activation_ended) {
            const auto found = std::find(
                paid_for_healing.begin(), paid_for_healing.end(), event.unit_id
            );
            if (found != paid_for_healing.end()) paid_for_healing.erase(found);
            continue;
        }
        const bool heals = event.type == simulation::EventType::unit_restored;
        if (!heals && event.type != simulation::EventType::unit_defeated) continue;
        const auto actor_id = event.related_unit_id;
        if (actor_id == 0 || std::find(fallen.begin(), fallen.end(), actor_id) != fallen.end()) continue;
        const auto member = lookups.member_of(lookups.context, actor_id);
        if (member.value == 0) continue;

        const auto* target = fielded(event.unit_id);
        const auto* actor = heals ? fielded(actor_id) : nullptr;
        if (heals &&
            (event.amount <= 0 || event.unit_id == actor_id ||
             actor == nullptr || target == nullptr || actor->side != target->side ||
             std::find(paid_for_healing.begin(), paid_for_healing.end(), actor_id) !=
                 paid_for_healing.end())) {
            continue;
        }
        const auto* role = heals ? actor : target;
        const auto profile = lookups.profile_of(
            lookups.context, role == nullptr ? 0 : role->unit_type_id
        );
        if (!profile) {
            result.error = ProgressionSourceError::unreadable_unit_type;
            return result;
        }
        const auto award = heals ? profile.progression.healing_experience
                                 : profile.progression.experience_award;
        if (award == 0) continue;
        if (heals) paid_for_healing.push_back(actor_id);

        const auto position = std::lower_bound(
            earned.begin(), earned.end(), member,
            [](const EarnedExperience& entry, campaign::PersistentEntityId id) {
                return entry.member < id;
            }
        );
        if (position != earned.end() && position->member == member) {
            const std::uint64_t total = static_cast<std::uint64_t>(position->experience) + award;
            position->experience = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(total, std::numeric_limits<std::uint32_t>::max())
            );
        } else {
            earned.insert(position, {member, award});
        }
    }

    // One state for the whole batch, seeded from the completed battle and
    // nothing else. See `campaign::derive_growth_seed`, and the consumption
    // order in `engine/campaign_runtime/README.md`.
    core::RandomState growth;
    growth.seed = campaign::derive_growth_seed(source);

    for (const EarnedExperience& entry : earned) {
        const campaign::PersistentUnit* const member =
            campaign::find_unit(state, entry.member);
        if (member == nullptr ||
            member->availability != campaign::Availability::available) {
            // Not on the roster, or not somebody the campaign can act on. The
            // board should never have carried them; saying so beats emitting an
            // operation `apply_outcome` would refuse.
            result.error = ProgressionSourceError::unknown_member;
            return result;
        }
        const package_runtime::UnitProgressionLoad rules =
            lookups.profile_of(lookups.context, member->definition.stable_id);
        if (!rules) {
            result.error = ProgressionSourceError::unreadable_unit_type;
            return result;
        }
        // A character at the ceiling earns nothing at all. The batch simply
        // contains nothing about them, which is more honest than recording a
        // number that buys nothing.
        if (member->progression.level >= campaign::maximum_progression_level) {
            continue;
        }
        const std::uint64_t total =
            static_cast<std::uint64_t>(member->progression.experience) +
            static_cast<std::uint64_t>(entry.experience);
        const std::uint32_t lifetime =
            total > 0xffffffffULL ? 0xffffffffU
                                  : static_cast<std::uint32_t>(total);
        result.operations.push_back(
            campaign::grant_experience(entry.member, entry.experience)
        );

        // The level a lifetime total reaches. Stated once, here, and nowhere
        // else: one plus the total divided by what this character's type
        // charges, capped.
        const std::uint64_t reached =
            1U + static_cast<std::uint64_t>(lifetime) /
                     static_cast<std::uint64_t>(rules.progression.experience_per_level);
        const auto capped = static_cast<std::uint16_t>(
            reached > campaign::maximum_progression_level
                ? campaign::maximum_progression_level
                : reached
        );
        result.rewards.push_back({entry.member, lifetime - member->progression.experience,
            member->progression.level, std::max(capped, member->progression.level)});
        if (capped <= member->progression.level) {
            continue;
        }
        const auto levels =
            static_cast<std::uint16_t>(capped - member->progression.level);
        result.operations.push_back(campaign::advance_level(entry.member, levels));

        LevelUp gained;
        gained.member = entry.member;
        gained.from_level = member->progression.level;
        gained.to_level = capped;
        for (std::uint16_t level = 0; level < levels; ++level) {
            for (std::size_t stat = 0; stat < campaign::growable_stat_count;
                 ++stat) {
                if (growth.roll_chance(
                        core::RandomStream::growth,
                        static_cast<std::uint32_t>(
                            rules.progression.growth[stat]
                        ),
                        growth_chance_bound
                    )) {
                    ++gained.points[stat];
                }
            }
        }
        for (std::size_t stat = 0; stat < campaign::growable_stat_count;
             ++stat) {
            if (gained.points[stat] != 0U) {
                result.operations.push_back(campaign::grow_stat(
                    entry.member,
                    static_cast<campaign::GrowableStat>(stat),
                    gained.points[stat]
                ));
            }
        }
        result.level_ups.push_back(gained);
    }

    return result;
}

std::string_view progression_source_error_name(
    ProgressionSourceError error
) noexcept {
    switch (error) {
        case ProgressionSourceError::none:
            return "none";
        case ProgressionSourceError::unknown_member:
            return "unknown_member";
        case ProgressionSourceError::unreadable_unit_type:
            return "unreadable_unit_type";
    }
    return "unknown";
}

}  // namespace grandleon::campaign_runtime
