// SPDX-License-Identifier: MIT
#include <grandleon/campaign_runtime/campaign_runtime.hpp>

#include <grandleon/core/random.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace grandleon::campaign_runtime {

namespace {

[[nodiscard]] campaign::ObjectiveOutcome translate_outcome(
    package_runtime::ObjectiveOutcome outcome
) noexcept {
    return outcome == package_runtime::ObjectiveOutcome::failed
               ? campaign::ObjectiveOutcome::failed
               : campaign::ObjectiveOutcome::satisfied;
}

[[nodiscard]] campaign::ConditionCombinator translate_combinator(
    package_runtime::ConditionCombinator combinator
) noexcept {
    switch (combinator) {
        case package_runtime::ConditionCombinator::any:
            return campaign::ConditionCombinator::any;
        case package_runtime::ConditionCombinator::none:
            return campaign::ConditionCombinator::none;
        case package_runtime::ConditionCombinator::all:
            break;
    }
    return campaign::ConditionCombinator::all;
}

}  // namespace

std::string_view graph_source_error_name(GraphSourceError error) noexcept {
    switch (error) {
        case GraphSourceError::none:
            return "none";
        case GraphSourceError::unidentified_campaign:
            return "unidentified_campaign";
        case GraphSourceError::invalid_graph:
            return "invalid_graph";
    }
    return "unknown";
}

std::string_view roster_error_name(RosterError error) noexcept {
    switch (error) {
        case RosterError::none:
            return "none";
        case RosterError::encounter_rejected:
            return "encounter_rejected";
        case RosterError::duplicate_assignment:
            return "duplicate_assignment";
        case RosterError::reserved_identity:
            return "reserved_identity";
        case RosterError::side_emptied:
            return "side_emptied";
        case RosterError::unavailable_objective_target:
            return "unavailable_objective_target";
        case RosterError::over_deployment_capacity:
            return "over_deployment_capacity";
    }
    return "unknown";
}

campaign::DefinitionRef campaign_node_ref(
    const core::PackageId& package,
    std::uint64_t node_id
) noexcept {
    return {package, core::ContentCategory::campaign_node, node_id};
}

CampaignGraphSource build_campaign_graph(
    const core::PackageId& package,
    std::uint64_t campaign_id,
    const package_runtime::CampaignDefinition& definition
) {
    CampaignGraphSource result;
    if (campaign_id == 0U) {
        result.error = GraphSourceError::unidentified_campaign;
        return result;
    }

    std::vector<campaign::CampaignGraphNode> nodes;
    nodes.reserve(definition.nodes.size());
    for (const package_runtime::CampaignNode& source : definition.nodes) {
        campaign::CampaignGraphNode node;
        node.node = campaign_node_ref(package, source.id);
        node.terminal =
            source.kind == package_runtime::CampaignNodeKind::terminal;
        node.has_fallback = source.has_unconditional_target;
        if (node.has_fallback) {
            node.fallback =
                campaign_node_ref(package, source.unconditional_target_id);
        }
        for (const package_runtime::CampaignBranch& branch : source.branches) {
            campaign::CampaignTransition transition;
            transition.target = campaign_node_ref(package, branch.target_id);
            transition.priority = branch.priority;
            transition.combinator = translate_combinator(branch.combinator);
            for (const package_runtime::CampaignPredicate& predicate :
                 branch.predicates) {
                // Two of the schema's three kinds now have bytes in a package
                // and are translated here. `inventoryAtLeast` is the one left:
                // `campaign::predicate_holds` evaluates it, and the compiler
                // still has no encoding for it, so it cannot arrive.
                if (predicate.kind ==
                    package_runtime::CampaignPredicateKind::world_flag_equals) {
                    transition.predicates.push_back(campaign::world_flag_equals(
                        {package, core::ContentCategory::world_flag,
                         predicate.subject},
                        {predicate.value_type == 2U
                             ? campaign::WorldValueType::integer
                             : campaign::WorldValueType::boolean,
                         predicate.value}
                    ));
                    continue;
                }
                transition.predicates.push_back(campaign::objective_result_is(
                    {package, core::ContentCategory::objective,
                     predicate.subject},
                    translate_outcome(predicate.result)
                ));
            }
            node.transitions.push_back(std::move(transition));
        }
        nodes.push_back(std::move(node));
    }

    result.graph = campaign::make_campaign_graph(
        {package, core::ContentCategory::campaign, campaign_id},
        campaign_node_ref(package, definition.entry_node_id),
        std::move(nodes)
    );
    result.graph_error = campaign::validate_graph(result.graph);
    if (result.graph_error != campaign::GraphError::none) {
        result.error = GraphSourceError::invalid_graph;
        result.graph = campaign::CampaignGraph{};
    }
    return result;
}

CampaignGraphLoad load_campaign_graph(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id
) {
    CampaignGraphLoad result;
    const package_runtime::CampaignLoadResult loaded =
        package_runtime::load_campaign(package, campaign_id);
    if (!loaded) {
        result.load_error = loaded.error;
        return result;
    }
    result.source =
        build_campaign_graph(package.game_id, campaign_id, loaded.definition);
    return result;
}

std::uint64_t encounter_of_node(
    const package_runtime::CampaignDefinition& definition,
    const campaign::DefinitionRef& node
) noexcept {
    if (node.category != core::ContentCategory::campaign_node) {
        return 0U;
    }
    for (const package_runtime::CampaignNode& candidate : definition.nodes) {
        if (candidate.id != node.stable_id) {
            continue;
        }
        return candidate.kind == package_runtime::CampaignNodeKind::encounter
                   ? candidate.encounter_id
                   : 0U;
    }
    return 0U;
}

namespace {

// Add one member's earned points to one board unit, saturating rather than
// wrapping. A saturating add is the honest one here: a stat that wrapped would
// hand a player a weaker character for having grown, and there is no number
// above the ceiling to show them instead.
template <typename Value>
void add_saturating(Value& target, std::uint16_t gain) noexcept {
    constexpr auto ceiling = static_cast<std::int64_t>(
        std::numeric_limits<Value>::max()
    );
    const std::int64_t total =
        static_cast<std::int64_t>(target) + static_cast<std::int64_t>(gain);
    target = static_cast<Value>(total > ceiling ? ceiling : total);
}

// Add one authored delta to one board unit, saturating at both ends.
//
// The signed sibling of `add_saturating` above, and it saturates for the same
// reason: a stat that wrapped would hand a player a weaker character for having
// been written stronger, and there is no number outside the range to show them
// instead.
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
    std::int64_t floor
) noexcept {
    constexpr auto ceiling = static_cast<std::int64_t>(
        std::numeric_limits<Value>::max()
    );
    const std::int64_t total =
        static_cast<std::int64_t>(target) + static_cast<std::int64_t>(delta);
    const std::int64_t landed = total > ceiling ? ceiling : total;
    target = static_cast<Value>(landed < floor ? floor : landed);
}

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
    add_signed_saturating(unit.strength, delta(SpecificStat::strength), 0);
    add_signed_saturating(unit.defense, delta(SpecificStat::defense), 0);
    add_signed_saturating(unit.resistance, delta(SpecificStat::resistance), 0);
    add_signed_saturating(unit.movement, delta(SpecificStat::movement), 1);
    add_signed_saturating(
        unit.action_points, delta(SpecificStat::action_points), 1
    );
    add_signed_saturating(unit.skill, delta(SpecificStat::skill), 0);
    add_signed_saturating(unit.luck, delta(SpecificStat::luck), 0);
    add_signed_saturating(unit.evasion, delta(SpecificStat::evasion), 0);
    add_signed_saturating(unit.magic, delta(SpecificStat::magic), 0);
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

// What the roster puts in a character's hands.
//
// The other half of the same pass `apply_roster_join` makes, and for the same
// reason: a campaign character takes the field as the character the campaign
// has made of them, which is their earned points and the kit the campaign holds
// for them. Everything a member does not stand in is untouched, so a board with
// nobody on it from the roster is the board the package alone produces.
//
// The order is the authored order first. `campaign::InventoryStack`s are sorted
// by item identity (a campaign's collections are sorted so two campaigns told
// the same things are the same bytes) and `item_ids` is hashed in the order it
// is written. Walking the unit type's own list first, and appending anything
// else the kit holds after it, is what makes a member holding one of each
// authored item field a board identical to the authored one.
void apply_roster_kit(
    simulation::UnitDefinition& unit,
    const campaign::PersistentUnit& record
) {
    std::vector<simulation::ContentId> identities;
    std::vector<std::uint16_t> counts;
    identities.reserve(record.carried.size());
    counts.reserve(record.carried.size());

    // A stack's quantity is wider than a battle pack's count. A campaign that
    // has somehow accumulated more than a battle can carry brings as many as it
    // can rather than wrapping to none.
    const auto brought = [](std::uint32_t quantity) -> std::uint16_t {
        constexpr std::uint32_t ceiling = 0xffffU;
        return static_cast<std::uint16_t>(
            quantity > ceiling ? ceiling : quantity
        );
    };

    std::vector<bool> taken(record.carried.size(), false);
    for (const simulation::ContentId authored : unit.item_ids) {
        for (std::size_t index = 0; index < record.carried.size(); ++index) {
            const campaign::InventoryStack& stack = record.carried[index];
            if (taken[index] || stack.item.stable_id != authored ||
                stack.item.category != core::ContentCategory::item) {
                continue;
            }
            identities.push_back(authored);
            counts.push_back(brought(stack.quantity));
            taken[index] = true;
            break;
        }
    }
    // Anything else the campaign holds for them, in the order the kit is kept.
    // Unreachable while kit movement is deferred, and carried through rather
    // than dropped so that the day it is reachable the board says so.
    for (std::size_t index = 0; index < record.carried.size(); ++index) {
        if (taken[index]) continue;
        const campaign::InventoryStack& stack = record.carried[index];
        if (stack.item.category != core::ContentCategory::item) continue;
        identities.push_back(stack.item.stable_id);
        counts.push_back(brought(stack.quantity));
    }

    unit.item_ids = std::move(identities);
    unit.item_counts = std::move(counts);
}

// What the roster brings to the board: what a member has become, and what the
// campaign holds for them.
//
// The first is the second half of growth, and the half that makes the first
// half mean anything: the points a level-up granted are stored on the roster
// member and added to the authored unit type each time that member takes the
// field.
//
// Adding *gains* rather than storing totals is why an author may still
// rebalance a class: the character keeps what they earned and inherits
// everything the author changed underneath it. And it is why this is a
// campaign-runtime rule rather than a simulation one: the board the package
// alone produces is untouched, and a roster with nothing to add produces
// exactly the bytes it always produced, which is the property every golden
// depends on.
//
// The second is `apply_roster_kit` above, in the same pass and for the same
// reason.
void apply_roster_join(
    CampaignEncounter& result,
    const campaign::CampaignState& state
) {
    // What the author wrote about the character standing in one placement, or
    // nothing for a character who is exactly their unit type. That is every
    // character in every piece of content that authors no specificity, and is
    // why that content's boards are byte for byte the boards they always were.
    //
    // Looked up by the placement's source key, which *is* the authored member
    // identity: the same key the assignment table is joined on, so a
    // specificity cannot land on somebody the exclusion pass thought was
    // somebody else.
    const auto specificity_of =
        [&result](simulation::UnitId unit_id
        ) -> const package_runtime::MemberSpecificity* {
        if (result.encounter.member_specificities.empty()) return nullptr;
        for (const package_runtime::PlacementIdentity& placement :
             result.encounter.placements) {
            if (placement.unit_id != unit_id) continue;
            for (const package_runtime::MemberSpecificity& specificity :
                 result.encounter.member_specificities) {
                if (specificity.member_id == placement.source_key_id) {
                    return &specificity;
                }
            }
            return nullptr;
        }
        return nullptr;
    };

    for (simulation::UnitDefinition& unit : result.encounter.definition.units) {
        const campaign::PersistentEntityId member =
            result.binding.persistent_of(campaign::BattleEntityId{unit.id});
        if (member.value == 0U) {
            continue;
        }
        const campaign::PersistentUnit* const record =
            campaign::find_unit(state, member);
        if (record == nullptr) {
            continue;
        }
        // Whether this character can be felled at all. The one thing on this
        // pass that comes from what a *project* declared rather than from what
        // the campaign has made of the character, and it lands here for the same
        // reason everything else on this pass does: it is a campaign judgement,
        // and the simulation is only ever handed the result.
        //
        // It reaches the members and nobody else: this loop has already skipped
        // every unit no member stands in. A board on which the opposition could
        // not be killed either is a board no objective can be satisfied on, and
        // the aid is for walking a campaign through rather than for freezing one.
        unit.endures = result.encounter.company_endures;
        // What the author made of them, then what the campaign did.
        const package_runtime::MemberSpecificity* const authored =
            specificity_of(unit.id);
        if (authored != nullptr) {
            apply_authored_specificity(unit, *authored);
        }
        const campaign::Progression& progression = record->progression;
        add_saturating(
            unit.health, progression.gain_in(campaign::GrowableStat::health)
        );
        add_saturating(
            unit.strength, progression.gain_in(campaign::GrowableStat::strength)
        );
        add_saturating(
            unit.defense, progression.gain_in(campaign::GrowableStat::defense)
        );
        add_saturating(
            unit.resistance,
            progression.gain_in(campaign::GrowableStat::resistance)
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
            unit.magic, progression.gain_in(campaign::GrowableStat::magic)
        );
        apply_roster_kit(unit, *record);
    }
}

// Whether the assignment table can be believed at all. Checked before a byte of
// any board is read, because a table that cannot be believed cannot be applied
// to one, and checked in one place, so the package path and the board path
// refuse the same tables for the same reasons and in the same order.
[[nodiscard]] RosterError assignments_rejected(
    const std::vector<RosterAssignment>& roster
) noexcept {
    for (std::size_t index = 0; index < roster.size(); ++index) {
        if (roster[index].member.value == 0U ||
            roster[index].placement_source_key == 0U) {
            return RosterError::reserved_identity;
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (roster[other].placement_source_key ==
                    roster[index].placement_source_key ||
                roster[other].member == roster[index].member) {
                return RosterError::duplicate_assignment;
            }
        }
    }
    return RosterError::none;
}

}  // namespace

std::vector<package_runtime::MemberSpecificity> member_specificities(
    const package_runtime::CampaignDefinition& definition
) {
    std::vector<package_runtime::MemberSpecificity> table;
    table.reserve(definition.specificities.size());
    for (const package_runtime::MemberSpecificity& specificity :
         definition.specificities) {
        // A member who authors nothing is never written into a package, so
        // this only ever skips a table a caller built by hand, and skipping
        // it keeps "empty means nobody is specific" true of the board as well
        // as of the record.
        if (specificity.empty()) continue;
        table.push_back(specificity);
    }
    return table;
}

CampaignEncounter load_encounter_for_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t encounter_id,
    const campaign::CampaignState& state,
    const std::vector<RosterAssignment>& roster
) {
    return load_encounter_for_campaign(package, 0U, encounter_id, state, roster);
}

CampaignEncounter load_encounter_for_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id,
    std::uint64_t encounter_id,
    const campaign::CampaignState& state,
    const std::vector<RosterAssignment>& roster
) {
    CampaignEncounter result;
    result.error = assignments_rejected(roster);
    if (result.error != RosterError::none) {
        return result;
    }

    package_runtime::EncounterLoadResult loaded =
        package_runtime::load_encounter(package, encounter_id);
    if (!loaded) {
        result.error = RosterError::encounter_rejected;
        result.load_error = loaded.error;
        return result;
    }
    // What the author wrote about the characters, attached here because this is
    // the layer that knows both a package and a campaign. A campaign the
    // package does not hold leaves the board with none, which is the board the
    // package alone produces.
    if (campaign_id != 0U) {
        const package_runtime::CampaignLoadResult authored =
            package_runtime::load_campaign(package, campaign_id);
        if (authored) {
            loaded.member_specificities =
                member_specificities(authored.definition);
        }
    }
    return join_campaign_roster(std::move(loaded), state, roster);
}

CampaignEncounter join_campaign_roster(
    package_runtime::EncounterLoadResult&& loaded,
    const campaign::CampaignState& state,
    const std::vector<RosterAssignment>& roster
) {
    CampaignEncounter result;
    result.error = assignments_rejected(roster);
    if (result.error != RosterError::none) {
        return result;
    }

    const auto assigned_member =
        [&roster](std::uint64_t source_key) -> campaign::PersistentEntityId {
        for (const RosterAssignment& assignment : roster) {
            if (assignment.placement_source_key == source_key) {
                return assignment.member;
            }
        }
        return campaign::PersistentEntityId{};
    };

    // One pass over the placements decides who stays. The roster decides, and
    // the map only proposes: `DESIGN.md` §5 puts it as "a permanently dead
    // character cannot reappear merely because a later map lists that
    // character as available".
    std::vector<simulation::UnitId> removed;
    for (const package_runtime::PlacementIdentity& placement :
         loaded.placements) {
        const campaign::PersistentEntityId member =
            assigned_member(placement.source_key_id);
        if (member.value == 0U) {
            // No campaign identity: a summon, a bystander, or the opposing
            // side. Not the roster's business, and it stays.
            continue;
        }
        if (campaign::is_deployable(state, member)) {
            const campaign::IdentityError bound = result.binding.bind(
                campaign::BattleEntityId{placement.unit_id}, member
            );
            if (bound != campaign::IdentityError::none) {
                result.error = RosterError::duplicate_assignment;
                return result;
            }
            continue;
        }
        removed.push_back(placement.unit_id);
        result.excluded.push_back(member);
    }

    // How many of the company would actually take this field, against the cap
    // the encounter authored. Decided here, after the assignment table is
    // believed and before a single unit of a board is published, so that a
    // published board is always a board within its cap and no caller below
    // this one ever has to ask.
    //
    // A cap never empties a placement: a member the cap keeps in is a member
    // whose placement is dropped by the pass above, exactly as a dead or
    // benched member's is. What it refuses is a *state* in which too many
    // would stand in theirs, and it refuses rather than trimming because
    // choosing who fights is the player's decision and not the engine's.
    if (loaded.deployment_capacity != 0U &&
        result.binding.size() >
            static_cast<std::size_t>(loaded.deployment_capacity)) {
        result.error = RosterError::over_deployment_capacity;
        return result;
    }

    if (removed.empty()) {
        // Nothing to leave off, so the board is the board the package holds,
        // byte for byte. This is the path every existing piece of content
        // takes, and it must stay indistinguishable from no campaign at all.
        result.encounter = std::move(loaded);
        std::sort(result.excluded.begin(), result.excluded.end());
        apply_roster_join(result, state);
        return result;
    }

    const auto is_removed = [&removed](simulation::UnitId id) {
        return std::find(removed.begin(), removed.end(), id) != removed.end();
    };

    // An objective that names somebody who is not there has no answer. The
    // spec's other branch, an authored unavailable-character route, is
    // content the campaign schema does not carry, so this is a refusal rather
    // than a guess at what the author meant.
    for (const simulation::ObjectiveDefinition& objective :
         loaded.definition.objectives) {
        if (objective.target_unit_id != 0U && is_removed(objective.target_unit_id)) {
            result.error = RosterError::unavailable_objective_target;
            return result;
        }
    }

    std::size_t first_side_before = 0;
    std::size_t first_side_after = 0;
    std::size_t second_side_before = 0;
    std::size_t second_side_after = 0;
    for (const simulation::UnitDefinition& unit : loaded.definition.units) {
        const bool first = unit.side == simulation::Side::first;
        (first ? first_side_before : second_side_before)++;
        if (!is_removed(unit.id)) {
            (first ? first_side_after : second_side_after)++;
        }
    }
    if ((first_side_before > 0U && first_side_after == 0U) ||
        (second_side_before > 0U && second_side_after == 0U)) {
        result.error = RosterError::side_emptied;
        return result;
    }

    package_runtime::EncounterLoadResult kept;
    kept.definition.width = loaded.definition.width;
    kept.definition.height = loaded.definition.height;
    kept.definition.terrain = std::move(loaded.definition.terrain);
    kept.definition.turn_order = loaded.definition.turn_order;
    kept.definition.objectives = std::move(loaded.definition.objectives);
    // The weapon, ability and item registries are what the *package* offers,
    // not what a particular board draws from, so they are carried across
    // whole. A
    // registry entry nobody now carries costs a lookup that never happens; a
    // registry rebuilt from the survivors would make the definitions depend on
    // the roster, which is a rule depending on a save file.
    kept.definition.weapons = std::move(loaded.definition.weapons);
    kept.definition.abilities = std::move(loaded.definition.abilities);
    kept.definition.items = std::move(loaded.definition.items);
    kept.terrain = std::move(loaded.terrain);
    // What the encounter says about deploying, carried across whole for the
    // same reason the registries are: it is what the *author* wrote about this
    // board and not a summary of who happens to be standing on it. A region
    // that vanished because somebody was benched would be a board that lost its
    // deployment phase for a reason the author never wrote, and a capacity that
    // vanished the same way would be a cap that stopped applying the moment it
    // started to bite.
    kept.definition.deployment_tiles =
        std::move(loaded.definition.deployment_tiles);
    kept.deployment_zone_id = loaded.deployment_zone_id;
    kept.deployment_capacity = loaded.deployment_capacity;
    // And what the author wrote about the characters themselves, carried
    // across on exactly the same grounds: it is content about who these people
    // are, and a member does not stop being who they are because somebody else
    // was benched. Carried whole rather than filtered to the survivors for the
    // same reason the registries are: a table rebuilt from who is standing
    // would make what an author wrote depend on a save file.
    kept.member_specificities = std::move(loaded.member_specificities);
    // And the campaign's own declaration about whether its company can be
    // felled, which is a fact about the campaign rather than about who happens
    // to be standing on this board. Carried across for the same reason as
    // everything above it: benching somebody must not quietly change the rules
    // the rest of the company fights under.
    kept.company_endures = loaded.company_endures;
    for (std::size_t index = 0; index < loaded.definition.units.size(); ++index) {
        if (is_removed(loaded.definition.units[index].id)) {
            continue;
        }
        kept.definition.units.push_back(std::move(loaded.definition.units[index]));
        kept.placements.push_back(loaded.placements[index]);
    }
    for (package_runtime::UnitBehaviorBinding& behavior : loaded.behaviors) {
        if (!is_removed(behavior.unit_id)) {
            kept.behaviors.push_back(std::move(behavior));
        }
    }

    result.encounter = std::move(kept);
    std::sort(result.excluded.begin(), result.excluded.end());
    apply_roster_join(result, state);
    return result;
}

std::vector<campaign::PersistentEntityId> members_a_board_places(
    const package_runtime::EncounterLoadResult& board,
    const std::vector<RosterAssignment>& roster
) {
    std::vector<campaign::PersistentEntityId> members;
    for (const package_runtime::PlacementIdentity& placement : board.placements) {
        for (const RosterAssignment& assignment : roster) {
            if (assignment.placement_source_key != placement.source_key_id) {
                continue;
            }
            if (assignment.member.value == 0U) continue;
            members.push_back(assignment.member);
            break;
        }
    }
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    return members;
}

std::vector<campaign::PersistentEntityId> members_a_board_fields(
    const package_runtime::EncounterLoadResult& board,
    const campaign::CampaignState& state,
    const std::vector<RosterAssignment>& roster
) {
    std::vector<campaign::PersistentEntityId> members =
        members_a_board_places(board, roster);
    members.erase(
        std::remove_if(
            members.begin(),
            members.end(),
            [&state](campaign::PersistentEntityId member) {
                return !campaign::is_deployable(state, member);
            }
        ),
        members.end()
    );
    return members;
}

// ---------------------------------------------------------------------------
// The satchel, on the way in
// ---------------------------------------------------------------------------

StartingKit starting_kit(
    const package_format::LoadedPackage& package,
    campaign::PersistentEntityId member,
    std::uint64_t unit_type_id
) {
    StartingKit result;
    if (member.value == 0U) {
        return result;
    }
    const package_runtime::UnitStartingItemsLoad authored =
        package_runtime::load_unit_starting_items(package, unit_type_id);
    if (!authored) {
        return result;
    }
    result.found = true;
    for (const std::uint64_t item : authored.items) {
        if (item == 0U) continue;
        result.operations.push_back(campaign::add_item(
            member,
            {package.game_id, core::ContentCategory::item, item},
            1U
        ));
    }
    return result;
}

// ---------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------

std::vector<campaign::CampaignOutcomeOperation> node_item_grants(
    const core::PackageId& package,
    const package_runtime::CampaignDefinition& definition,
    std::uint64_t node_id
) {
    std::vector<campaign::CampaignOutcomeOperation> operations;
    for (const package_runtime::CampaignItemGrant& grant : definition.grants) {
        if (grant.join_node_id != node_id) continue;
        if (grant.item_id == 0U || grant.quantity == 0U) continue;
        // The reserved zero owner is the shared store. It is the one owner an
        // author can name without knowing who is still alive, which is the
        // whole reason a grant lands there rather than in somebody's hands.
        operations.push_back(campaign::add_item(
            campaign::PersistentEntityId{},
            {package, core::ContentCategory::item, grant.item_id},
            grant.quantity
        ));
    }
    return operations;
}

// ---------------------------------------------------------------------------
// What a battle did to a roster
// ---------------------------------------------------------------------------

namespace {

// Ascending by member, so the operations a batch carries, and therefore the
// order the growth stream is drawn in, depend on who earned what and never on
// what order the events happened to arrive in.
struct EarnedExperience final {
    campaign::PersistentEntityId member{};
    std::uint32_t experience{};
};

}  // namespace

std::vector<BoardUnitType> board_unit_types(
    const simulation::EncounterDefinition& board
) {
    // Through the rules' own expansion rather than over `board.units`: a wave
    // authored as one placement is several characters on the field, each with
    // an identifier the content never wrote, and every one of them can be the
    // subject of a defeat event. Asking the rules is also what keeps the
    // identifiers here equal to the ones the battle used.
    //
    // It costs a whole expanded board for as long as this call runs, which on a
    // console is a real number and is still the right trade: the alternative is
    // a second copy of the identifier rule living here, and two copies of that
    // rule disagreeing is a campaign that pays the wrong character for a kill.
    // The copy is transient: what the caller keeps is the narrow table, which
    // is the point of the table existing.
    std::vector<simulation::UnitDefinition> fielded;
    fielded.reserve(board.units.size());
    if (!simulation::expand_arrivals(board.units, fielded)) {
        // A malformed wave, which `create_encounter` refuses outright. There is
        // no battle to derive anything from, so the authored placements are as
        // honest an answer as this can give.
        fielded.assign(board.units.begin(), board.units.end());
    }

    std::vector<BoardUnitType> types;
    types.reserve(fielded.size());
    for (const simulation::UnitDefinition& unit : fielded) {
        types.push_back({unit.id, unit.unit_type_id});
    }
    return types;
}

BattleProgression derive_battle_progression(
    const package_format::LoadedPackage& package,
    const campaign::CampaignState& state,
    const simulation::EncounterDefinition& board,
    const campaign::BattleBinding& binding,
    const std::vector<simulation::Event>& events,
    const campaign::OutcomeSource& source
) {
    return derive_battle_progression(
        package, state, board_unit_types(board), binding, events, source
    );
}

BattleProgression derive_battle_progression(
    const package_format::LoadedPackage& package,
    const campaign::CampaignState& state,
    const std::vector<BoardUnitType>& board,
    const campaign::BattleBinding& binding,
    const std::vector<simulation::Event>& events,
    const campaign::OutcomeSource& source
) {
    BattleProgression result;

    const auto unit_type_of =
        [&board](simulation::UnitId id) -> std::uint64_t {
        for (const BoardUnitType& unit : board) {
            if (unit.unit == id) return unit.unit_type_id;
        }
        return 0U;
    };

    // Who the battle buried. A member who did not survive earns nothing: the
    // batch that records their death would refuse an experience grant on the
    // same member anyway, and a level a corpse reaches is a level nobody has.
    std::vector<simulation::UnitId> fallen;
    for (const simulation::Event& event : events) {
        if (event.type == simulation::EventType::unit_defeated) {
            fallen.push_back(event.unit_id);
        }
    }
    const auto survived = [&fallen](simulation::UnitId id) {
        return std::find(fallen.begin(), fallen.end(), id) == fallen.end();
    };

    std::vector<EarnedExperience> earned;
    for (const simulation::Event& event : events) {
        if (event.type != simulation::EventType::unit_defeated) {
            continue;
        }
        // Whoever struck the felling blow. The simulation already records it:
        // `related_unit_id` on a defeat event is the unit that caused it, which
        // is why this rule needs no new event and no battle-local counter.
        if (event.related_unit_id == 0U || !survived(event.related_unit_id)) {
            continue;
        }
        const campaign::PersistentEntityId member =
            binding.persistent_of(campaign::BattleEntityId{event.related_unit_id});
        if (member.value == 0U) {
            continue;
        }
        const package_runtime::UnitProgressionLoad defeated =
            package_runtime::load_unit_progression(
                package, unit_type_of(event.unit_id)
            );
        if (!defeated) {
            result.error = ProgressionSourceError::unreadable_unit_type;
            return result;
        }
        if (defeated.progression.experience_award == 0U) {
            continue;
        }
        const auto position = std::lower_bound(
            earned.begin(),
            earned.end(),
            member,
            [](const EarnedExperience& entry, campaign::PersistentEntityId id) {
                return entry.member < id;
            }
        );
        if (position != earned.end() && position->member == member) {
            const std::uint64_t total =
                static_cast<std::uint64_t>(position->experience) +
                static_cast<std::uint64_t>(defeated.progression.experience_award);
            position->experience = total > 0xffffffffULL
                                       ? 0xffffffffU
                                       : static_cast<std::uint32_t>(total);
        } else {
            earned.insert(
                position,
                {member, defeated.progression.experience_award}
            );
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
            package_runtime::load_unit_progression(
                package, member->definition.stable_id
            );
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

    // What the battle did to what the campaign owns, appended after everything
    // above so no operation that was already there moved. See the header for
    // why a spend lands on the spender's kit and a drop in the shared store,
    // and why the binding decides whether a consequence is the campaign's at
    // all.
    //
    // Neither loop reads the board, the roster's numbers, or a package record:
    // an inventory consequence is entirely in the events, which is exactly what
    // the `item_used` event was shaped to make true.
    const auto claimed_by_a_member =
        [&binding](simulation::UnitId unit) -> campaign::PersistentEntityId {
        if (unit == 0U) return {};
        return binding.persistent_of(campaign::BattleEntityId{unit});
    };
    const auto item_ref =
        [&package](simulation::ContentId item) -> campaign::DefinitionRef {
        return {package.game_id, core::ContentCategory::item, item};
    };
    // What fell, first, so a batch that both gains and spends one identity is
    // never refused for an ordering.
    for (const simulation::Event& event : events) {
        if (event.type != simulation::EventType::item_dropped) continue;
        if (event.content_id == 0U) continue;
        if (claimed_by_a_member(event.related_unit_id).value == 0U) continue;
        result.operations.push_back(
            campaign::add_item({}, item_ref(event.content_id), 1U)
        );
    }
    // Then what was spent, out of the kit of whoever spent it. Exactly one per
    // event, in the order the battle recorded them.
    for (const simulation::Event& event : events) {
        if (event.type != simulation::EventType::item_used) continue;
        if (event.content_id == 0U) continue;
        const campaign::PersistentEntityId spender =
            claimed_by_a_member(event.unit_id);
        if (spender.value == 0U) continue;
        result.operations.push_back(
            campaign::consume_item(spender, item_ref(event.content_id), 1U)
        );
    }

    // And what the battle turned out to be *about*, last, so no operation above
    // moved. A character talked off the board raises the flag their placement
    // authored, and that flag is what a campaign-graph edge reads to open a map
    // the other route never sees.
    //
    // The identity comes off the event rather than out of the board, and that
    // is the whole reason the event carries one. A battle-local unit id is
    // derived per encounter and is a different number every time the character
    // appears, so it could not be a durable key; the authored record can.
    //
    // Unconditional on the binding, unlike a spend or a drop. Who *owns* a
    // consequence matters when the consequence is somebody's kit; a world flag
    // belongs to the campaign rather than to a member, and a story beat that
    // only counted when a bound member happened to be the one who spoke would
    // be a rule nobody authored.
    for (const simulation::Event& event : events) {
        if (event.type != simulation::EventType::unit_talked) continue;
        if (event.content_id == 0U) continue;
        result.operations.push_back(
            campaign::set_world_flag(
                {package.game_id,
                 core::ContentCategory::world_flag,
                 event.content_id},
                {campaign::WorldValueType::boolean, 1}
            )
        );
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
