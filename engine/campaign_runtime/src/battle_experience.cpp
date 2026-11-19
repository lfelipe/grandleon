// SPDX-License-Identifier: MIT
#include <grandleon/campaign_runtime/battle_experience.hpp>
#include <grandleon/core/content_identity.hpp>
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

// Ascending by member, so the operations a batch carries, and therefore the
// order the growth stream is drawn in, depend on who earned what and never on
// what order the events happened to arrive in.
struct EarnedExperience final {
    campaign::PersistentEntityId member{};
    std::uint32_t experience{};
};

}  // namespace

void apply_authored_specificity(simulation::UnitDefinition& unit,
    const package_runtime::MemberSpecificity& specificity) noexcept {
    simulation::apply_stat_offsets(unit, specificity.stat_deltas);
    // And what the character does with whatever they are holding. The band
    // itself is resolved by `create_encounter` from the weapon in hand, so this
    // is carried onto the unit rather than added to a band here. See
    // `simulation::UnitDefinition::reach_bonus`, which is where the reasoning
    // about never touching the shared authored weapon lives.
    unit.reach_bonus = specificity.reach_bonus;
}

campaign::DefinitionRef starting_seed_key(const core::PackageId& package) noexcept {
    return {package, core::ContentCategory::world_flag,
        core::stable_content_id_v1("grandleon_starting_seed")};
}

std::uint64_t starting_seed(const campaign::CampaignState& state,
                            const core::PackageId& package) noexcept {
    const auto* value = campaign::find_world_value(state, starting_seed_key(package));
    return value != nullptr && value->type == campaign::WorldValueType::integer && value->value > 0
        ? static_cast<std::uint64_t>(value->value) : 0U;
}

namespace {

campaign::DefinitionRef starting_stat_key(const core::PackageId& package,
    campaign::PersistentEntityId member, std::size_t stat) noexcept {
    const std::uint64_t domain = core::stable_content_id_v1("grandleon_starting_stat");
    const auto salt = core::random_draw(domain, core::RandomStream::growth, member.value);
    return {package, core::ContentCategory::world_flag,
        core::random_draw(salt, core::RandomStream::growth, stat)};
}

std::array<std::uint16_t, campaign::growable_stat_count> roll_level_gains(
    core::RandomState& random, const package_runtime::UnitProgression& profile,
    std::uint16_t levels) noexcept {
    std::array<std::uint16_t, campaign::growable_stat_count> gains{};
    for (std::uint16_t level = 0; level < levels; ++level) {
        for (std::size_t stat = 0; stat < gains.size(); ++stat) {
            if (random.roll_chance(core::RandomStream::growth, profile.growth[stat], growth_chance_bound)) ++gains[stat];
        }
    }
    return gains;
}

void append_stat_gains(std::vector<campaign::CampaignOutcomeOperation>& operations,
    campaign::PersistentEntityId member,
    const std::array<std::uint16_t, campaign::growable_stat_count>& gains) {
    for (std::size_t stat = 0; stat < gains.size(); ++stat) {
        if (gains[stat] != 0) operations.push_back(campaign::grow_stat(member,
            static_cast<campaign::GrowableStat>(stat), gains[stat]));
    }
}

}  // namespace

void append_character_start(std::vector<campaign::CampaignOutcomeOperation>& operations,
    const campaign::DefinitionRef& role, campaign::PersistentEntityId member,
    const core::CharacterStart& start, const core::RoleStartingStats& stats,
    const package_runtime::UnitProgression& progression, std::uint64_t seed) {
    if (!start.fixed_stats && stats.authored()) {
        const auto offsets = core::roll_starting_stats(stats.ranges, seed, member.value);
        for (std::size_t stat = 0; stat < offsets.size(); ++stat) {
            if (!stats.ranges[stat].authored()) continue;
            operations.push_back(campaign::set_world_flag(starting_stat_key(role.package_id, member, stat),
                {campaign::WorldValueType::integer, offsets[stat]}));
        }
    }
    if (start.level <= 1) return;
    const auto levels = static_cast<std::uint16_t>(start.level - 1U);
    operations.push_back(campaign::grant_experience(member,
        static_cast<std::uint32_t>(levels) * progression.experience_per_level));
    operations.push_back(campaign::advance_level(member, levels));
    core::RandomState growth;
    growth.seed = core::random_draw(seed ^ role.stable_id, core::RandomStream::growth, member.value);
    append_stat_gains(operations, member, roll_level_gains(growth, progression, levels));
}

void apply_starting_stats(simulation::UnitDefinition& unit, const campaign::CampaignState& state,
    const campaign::DefinitionRef& role, campaign::PersistentEntityId member) noexcept {
    core::StartingStatOffsets offsets{};
    for (std::size_t stat = 0; stat < offsets.size(); ++stat) {
        const auto* value = campaign::find_world_value(state, starting_stat_key(role.package_id, member, stat));
        if (value && value->type == campaign::WorldValueType::integer &&
            value->value >= -core::maximum_starting_offset && value->value <= core::maximum_starting_offset) {
            offsets[stat] = static_cast<std::int16_t>(value->value);
        }
    }
    if (offsets != core::StartingStatOffsets{}) simulation::apply_stat_offsets(unit, offsets);
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
        gained.points = roll_level_gains(growth, rules.progression, levels);
        append_stat_gains(result.operations, entry.member, gained.points);
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
