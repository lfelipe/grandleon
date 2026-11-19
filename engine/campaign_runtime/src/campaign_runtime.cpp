// SPDX-License-Identifier: MIT
#include <grandleon/campaign_runtime/campaign_runtime.hpp>
#include <grandleon/campaign_runtime/character_rules.hpp>
#include <grandleon/campaign_runtime/event_consequences.hpp>
#include <grandleon/campaign_runtime/inventory_consequences.hpp>

#include <grandleon/core/random.hpp>
#include <grandleon/core/currency.hpp>

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
                if (predicate.kind ==
                    package_runtime::CampaignPredicateKind::
                        inventory_at_least) {
                    transition.predicates.push_back(
                        campaign::inventory_at_least(
                            campaign::PersistentEntityId{},
                            {package, core::ContentCategory::item,
                             predicate.subject},
                            predicate.value >= 1 &&
                                    predicate.value <= 0xffffffffLL
                                ? static_cast<std::uint32_t>(predicate.value)
                                : 0U
                        )
                    );
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
    const campaign::CampaignState& state,
    const package_format::LoadedPackage* package
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
        apply_roster_growth(unit, record->progression);
        const package_runtime::MemberRules* rules = nullptr;
        for (const auto& placement : result.encounter.placements) {
            if (placement.unit_id != unit.id) continue;
            for (const auto& candidate : result.encounter.member_rules) {
                if (candidate.member_id == placement.source_key_id) rules = &candidate;
            }
        }
        unit.drop_inventory_on_defeat =
            result.encounter.definition.inventory_mode == simulation::InventoryMode::carried &&
            permanent_member_loss(rules ? rules->defeat_policy : package_runtime::DefeatPolicy::inherit,
                                  result.encounter.company_permanent_loss);
        unit.defeat_ends_battle = rules != nullptr &&
            rules->defeat_policy == package_runtime::DefeatPolicy::lose_battle;
        if(!apply_member_kit(unit,*record,rules,result.encounter.definition.inventory_mode==simulation::InventoryMode::carried)){
            result.error=RosterError::encounter_rejected;
            result.load_error=package_runtime::EncounterLoadError::malformed_payload;
            return;
        }
    }
    if (package != nullptr && !package_runtime::resolve_carried_equipment(*package, result.encounter.definition)) {
        result.error = RosterError::encounter_rejected;
        result.load_error = package_runtime::EncounterLoadError::malformed_payload;
        return;
    }
    for (auto& unit : result.encounter.definition.units) {
        for (const auto& placement : result.encounter.placements) {
            if (placement.unit_id != unit.id) continue;
            for (const auto& rules : result.encounter.member_rules) {
                if (rules.member_id == placement.source_key_id)
                    apply_member_permissions(unit, &rules, result.encounter.definition.weapons,
                        result.encounter.definition.inventory_mode==simulation::InventoryMode::carried);
            }
        }
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

std::vector<package_runtime::MemberRules> member_rules(
    const package_runtime::CampaignDefinition& definition
) {
    std::vector<package_runtime::MemberRules> rules;
    for (const auto& member : definition.members) {
        if (member.rules.member_id != 0U) rules.push_back(member.rules);
    }
    return rules;
}

package_runtime::DefeatPolicy member_defeat_policy(
    const package_runtime::CampaignDefinition& definition, std::uint64_t source_member_id
) noexcept {
    for (const auto& member : definition.members) {
        if (member.id == source_member_id && member.rules.defeat_policy != package_runtime::DefeatPolicy::inherit)
            return member.rules.defeat_policy;
    }
    return definition.character_loss == package_runtime::CharacterLoss::permanent
        ? package_runtime::DefeatPolicy::permanent : package_runtime::DefeatPolicy::withdraw;
}

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
            loaded.member_rules = member_rules(authored.definition);
            loaded.company_endures = authored.definition.invulnerable_for_testing;
            loaded.company_permanent_loss = authored.definition.character_loss ==
                package_runtime::CharacterLoss::permanent;
        }
    }
    return join_campaign_roster(std::move(loaded), state, roster, &package);
}

CampaignEncounter join_campaign_roster(
    package_runtime::EncounterLoadResult&& loaded,
    const campaign::CampaignState& state,
    const std::vector<RosterAssignment>& roster,
    const package_format::LoadedPackage* package
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
    // the map only proposes: a permanently dead character cannot reappear
    // merely because a later map lists that character as available.
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
        apply_roster_join(result, state, package);
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

    // Roster availability changes who takes the field, not the authored map,
    // terrain costs, scenes, visibility or random seed. Keep the whole board
    // and remove only the unavailable units and their per-placement bindings.
    package_runtime::EncounterLoadResult kept = std::move(loaded);
    auto& units = kept.definition.units;
    units.erase(std::remove_if(units.begin(), units.end(),
        [&is_removed](const simulation::UnitDefinition& unit) {
            return is_removed(unit.id);
        }), units.end());
    kept.placements.erase(std::remove_if(kept.placements.begin(), kept.placements.end(),
        [&is_removed](const package_runtime::PlacementIdentity& placement) {
            return is_removed(placement.unit_id);
        }), kept.placements.end());
    kept.behaviors.erase(std::remove_if(kept.behaviors.begin(), kept.behaviors.end(),
        [&is_removed](const package_runtime::UnitBehaviorBinding& behavior) {
            return is_removed(behavior.unit_id);
        }), kept.behaviors.end());

    result.encounter = std::move(kept);
    std::sort(result.excluded.begin(), result.excluded.end());
    apply_roster_join(result, state, package);
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
    std::uint64_t unit_type_id,
    const package_runtime::MemberRules* rules,
    simulation::InventoryMode inventory_mode
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
    if (rules != nullptr && rules->states_starting_weapons) {
        for (const auto weapon : rules->starting_weapons) {
            result.operations.push_back(campaign::add_item(member,
                {package.game_id, core::ContentCategory::weapon, weapon}, 1U));
        }
    }
    if(inventory_mode==simulation::InventoryMode::carried && !(rules && rules->states_starting_weapons)){
        const auto weapons=package_runtime::load_unit_starting_weapons(package,unit_type_id);
        if(!weapons){result.found=false;result.operations.clear();return result;}
        for(const auto weapon:weapons.weapons) result.operations.push_back(campaign::add_item(member,
            {package.game_id,core::ContentCategory::weapon,weapon},1U));
    }
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
    append_item_grants(operations, package, definition.grants, node_id);
    return operations;
}

// ---------------------------------------------------------------------------
// What a battle did to a roster
// ---------------------------------------------------------------------------

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
        types.push_back({unit.id, unit.unit_type_id, unit.side});
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
        package, state, board_unit_types(board), binding, events, source, board.inventory_mode
    );
}

BattleProgression derive_battle_progression(
    const package_format::LoadedPackage& package,
    const campaign::CampaignState& state,
    const std::vector<BoardUnitType>& board,
    const campaign::BattleBinding& binding,
    const std::vector<simulation::Event>& events,
    const campaign::OutcomeSource& source,
    simulation::InventoryMode inventory_mode
) {
    struct LookupContext final {
        const package_format::LoadedPackage& package;
        const campaign::BattleBinding& binding;
    };
    const LookupContext context{package, binding};
    BattleProgression result = derive_roster_experience(
        state, board, events, source,
        {
            &context,
            [](const void* value, simulation::UnitId unit) {
                return static_cast<const LookupContext*>(value)->binding.persistent_of(
                    campaign::BattleEntityId{unit}
                );
            },
            [](const void* value, std::uint64_t type) {
                return package_runtime::load_unit_progression(
                    static_cast<const LookupContext*>(value)->package, type
                );
            }
        }
    );
    if (!result) return result;

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
    append_inventory_consequences(result.operations, package.game_id, events,
                                  inventory_mode, claimed_by_a_member);

    append_event_consequences(result.operations, package.game_id, events);
    return result;
}

}  // namespace grandleon::campaign_runtime
