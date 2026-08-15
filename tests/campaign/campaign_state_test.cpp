// SPDX-License-Identifier: MIT
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/state.hpp>

#include <iostream>
#include <string_view>
#include <vector>

namespace campaign = grandleon::campaign;
namespace core = grandleon::core;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

core::PackageId package(std::uint8_t marker) {
    core::PackageId identity{};
    identity[15] = marker;
    return identity;
}

campaign::DefinitionRef reference(
    core::ContentCategory category,
    std::string_view key
) {
    return {package(1), category, core::stable_content_id_v1(key)};
}

const campaign::DefinitionRef lancer =
    reference(core::ContentCategory::unit_type, "lancer");
const campaign::DefinitionRef healer =
    reference(core::ContentCategory::unit_type, "healer");
const campaign::DefinitionRef potion =
    reference(core::ContentCategory::item, "potion");
const campaign::DefinitionRef torch =
    reference(core::ContentCategory::item, "torch");
const campaign::DefinitionRef hold_the_ford =
    reference(core::ContentCategory::objective, "hold_the_ford");
const campaign::DefinitionRef bridge_burned =
    reference(core::ContentCategory::objective, "bridge_burned");
const campaign::DefinitionRef ford_battle =
    reference(core::ContentCategory::encounter, "ford_battle");

constexpr campaign::PersistentEntityId first{1};
constexpr campaign::PersistentEntityId second{2};
constexpr campaign::PersistentEntityId absent{99};

campaign::OutcomeSource source(std::uint64_t battle_hash, std::uint64_t sequence) {
    return {ford_battle, battle_hash, sequence};
}

// A campaign with two members on the roster and three potions in the store.
campaign::CampaignState opening_roster() {
    campaign::CampaignState state;
    const auto batch = campaign::make_outcome_batch(
        source(0x1111ULL, 0U),
        {
            campaign::recruit_unit(first, lancer),
            campaign::recruit_unit(second, healer),
            campaign::add_item(campaign::PersistentEntityId{}, potion, 3U),
        }
    );
    const auto applied = campaign::apply_outcome(state, batch);
    expect(static_cast<bool>(applied), "the opening roster commits");
    return state;
}

// The id is a function of the battle and of what the batch says, and of
// nothing else. No counter, no clock, no draw.
void an_outcome_id_is_derived_and_reproducible() {
    const std::vector<campaign::CampaignOutcomeOperation> operations{
        campaign::recruit_unit(first, lancer),
        campaign::add_item(first, potion, 2U),
    };

    const campaign::OutcomeId once =
        campaign::derive_outcome_id(source(0xABCDULL, 4U), operations);
    const campaign::OutcomeId again =
        campaign::derive_outcome_id(source(0xABCDULL, 4U), operations);
    expect(once == again, "the same battle and batch derive the same id");
    expect(once.value != 0U, "and it is never the reserved zero id");

    expect(
        !(campaign::derive_outcome_id(source(0xABCEULL, 4U), operations) == once),
        "a battle that ended differently derives a different id"
    );
    expect(
        !(campaign::derive_outcome_id(source(0xABCDULL, 5U), operations) == once),
        "fighting the same encounter again derives a different id"
    );

    const std::vector<campaign::CampaignOutcomeOperation> reordered{
        operations[1],
        operations[0],
    };
    expect(
        !(campaign::derive_outcome_id(source(0xABCDULL, 4U), reordered) == once),
        "the operations are ordered, and a different order is a different batch"
    );

    std::vector<campaign::CampaignOutcomeOperation> corrected = operations;
    corrected[1].amount = 3;
    expect(
        !(campaign::derive_outcome_id(source(0xABCDULL, 4U), corrected) == once),
        "a corrected batch from the same battle is not mistaken for the old one"
    );

    const auto batch =
        campaign::make_outcome_batch(source(0xABCDULL, 4U), operations);
    expect(batch.id == once, "make_outcome_batch fills in the derived id");
    expect(batch.operations.size() == 2U, "and keeps the operations");
}

void recruitment_adds_to_the_roster() {
    campaign::CampaignState state = opening_roster();

    expect(state.units.size() == 2U, "two members joined");
    expect(campaign::is_deployable(state, first), "and both may take the field");
    expect(campaign::is_deployable(state, second), "both of them");
    expect(
        campaign::item_quantity(state, campaign::PersistentEntityId{}, potion) ==
            3U,
        "the shared store holds what the batch put there"
    );

    const campaign::PersistentUnit* const member =
        campaign::find_unit(state, first);
    expect(member != nullptr, "the member is on the roster");
    if (member != nullptr) {
        expect(member->definition == lancer, "as an instance of its unit type");
        expect(
            member->availability == campaign::Availability::available,
            "and available"
        );
        expect(member->progression.level == 1U, "at the first level");
    }

    // Two instances of one unit type stay two people with their own numbers.
    const auto grown = campaign::make_outcome_batch(
        source(0x2222ULL, 1U),
        {
            campaign::recruit_unit(campaign::PersistentEntityId{3}, lancer),
            campaign::grant_experience(campaign::PersistentEntityId{3}, 40U),
            campaign::advance_level(campaign::PersistentEntityId{3}, 1U),
        }
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, grown)),
        "a second lancer joins"
    );
    const campaign::PersistentUnit* const twin =
        campaign::find_unit(state, campaign::PersistentEntityId{3});
    expect(twin != nullptr, "and is on the roster");
    if (twin != nullptr) {
        expect(
            twin->definition == campaign::find_unit(state, first)->definition,
            "sharing the first lancer's definition"
        );
        expect(
            twin->progression.experience == 40U &&
                twin->progression.level == 2U,
            "with progression of its own"
        );
        expect(
            campaign::find_unit(state, first)->progression.experience == 0U,
            "which the other lancer did not receive"
        );
    }

    const auto again = campaign::make_outcome_batch(
        source(0x3333ULL, 2U),
        {campaign::recruit_unit(first, healer)}
    );
    const auto refused = campaign::apply_outcome(state, again);
    expect(
        refused.error == campaign::OutcomeError::unit_already_present,
        "recruiting onto an occupied identity is refused, never merged"
    );
}

// The requirement the roster exists for: a permanent death is final, and no
// later encounter can draw the dead into the field.
void permanent_death_excludes_a_member_from_every_later_encounter() {
    campaign::CampaignState state = opening_roster();

    const auto fell = campaign::make_outcome_batch(
        source(0x4444ULL, 1U),
        {campaign::record_permanent_death(first)}
    );
    expect(static_cast<bool>(campaign::apply_outcome(state, fell)), "the death commits");

    const campaign::PersistentUnit* const member =
        campaign::find_unit(state, first);
    expect(member != nullptr, "the record is kept — the campaign remembers them");
    if (member != nullptr) {
        expect(
            member->availability == campaign::Availability::dead,
            "as permanently dead"
        );
    }
    expect(!campaign::is_deployable(state, first), "they are not deployable");

    const std::vector<campaign::PersistentEntityId> roster =
        campaign::deployable_units(state);
    expect(roster.size() == 1U, "a later encounter draws from one member");
    expect(
        roster.size() == 1U && roster[0] == second,
        "and it is the survivor"
    );

    // A later map naming that character does not bring them back, whichever
    // way it tries.
    const auto revive = campaign::make_outcome_batch(
        source(0x5555ULL, 2U),
        {campaign::set_availability(first, campaign::Availability::available)}
    );
    expect(
        campaign::apply_outcome(state, revive).error ==
            campaign::OutcomeError::unit_is_dead,
        "making the dead available again is refused"
    );

    const auto rejoin = campaign::make_outcome_batch(
        source(0x6666ULL, 3U),
        {campaign::recruit_unit(first, lancer)}
    );
    expect(
        campaign::apply_outcome(state, rejoin).error ==
            campaign::OutcomeError::unit_already_present,
        "and re-recruiting their identity is refused"
    );

    const auto reward = campaign::make_outcome_batch(
        source(0x7777ULL, 4U),
        {campaign::grant_experience(first, 10U)}
    );
    expect(
        campaign::apply_outcome(state, reward).error ==
            campaign::OutcomeError::unit_is_dead,
        "the dead do not go on improving"
    );

    expect(
        campaign::validate(state) == campaign::StateError::none,
        "and the campaign is still a campaign"
    );
}

void a_fallen_member_leaves_their_kit_behind() {
    campaign::CampaignState state = opening_roster();

    const auto equipped = campaign::make_outcome_batch(
        source(0x8888ULL, 1U),
        {
            campaign::add_item(first, potion, 2U),
            campaign::add_item(first, torch, 1U),
        }
    );
    expect(static_cast<bool>(campaign::apply_outcome(state, equipped)), "the kit is issued");
    expect(
        campaign::item_quantity(state, first, potion) == 2U,
        "and carried by the member"
    );

    const auto fell = campaign::make_outcome_batch(
        source(0x9999ULL, 2U),
        {campaign::record_permanent_death(first)}
    );
    expect(static_cast<bool>(campaign::apply_outcome(state, fell)), "the death commits");

    expect(
        campaign::item_quantity(state, first, potion) == 0U,
        "the dead carry nothing"
    );
    expect(
        campaign::item_quantity(state, campaign::PersistentEntityId{}, potion) ==
            5U,
        "their potions merged into the shared store"
    );
    expect(
        campaign::item_quantity(state, campaign::PersistentEntityId{}, torch) ==
            1U,
        "and so did the rest of the kit"
    );
    expect(
        campaign::validate(state) == campaign::StateError::none,
        "which is the arrangement the invariants require"
    );
}

void inventory_is_spent_and_absence_has_one_spelling() {
    campaign::CampaignState state = opening_roster();

    const auto spent = campaign::make_outcome_batch(
        source(0xAAAAULL, 1U),
        {campaign::consume_item(campaign::PersistentEntityId{}, potion, 3U)}
    );
    expect(static_cast<bool>(campaign::apply_outcome(state, spent)), "three potions are drunk");
    expect(
        campaign::item_quantity(state, campaign::PersistentEntityId{}, potion) ==
            0U,
        "and none are left"
    );
    expect(
        state.store.empty(),
        "an emptied stack is removed rather than kept at zero"
    );

    const auto overdrawn = campaign::make_outcome_batch(
        source(0xBBBBULL, 2U),
        {campaign::consume_item(campaign::PersistentEntityId{}, potion, 1U)}
    );
    expect(
        campaign::apply_outcome(state, overdrawn).error ==
            campaign::OutcomeError::insufficient_items,
        "spending what is not held is refused"
    );

    const auto nobody = campaign::make_outcome_batch(
        source(0xCCCCULL, 3U),
        {campaign::add_item(absent, potion, 1U)}
    );
    expect(
        campaign::apply_outcome(state, nobody).error ==
            campaign::OutcomeError::unknown_unit,
        "and giving an item to nobody is refused"
    );
}

void objectives_and_world_flags_are_recorded() {
    campaign::CampaignState state = opening_roster();

    const auto recorded = campaign::make_outcome_batch(
        source(0xDDDDULL, 1U),
        {
            campaign::record_objective(
                hold_the_ford, campaign::ObjectiveOutcome::satisfied
            ),
            campaign::record_objective(
                bridge_burned, campaign::ObjectiveOutcome::failed
            ),
            campaign::set_world_flag(
                reference(core::ContentCategory::objective, "ford_open"),
                campaign::WorldValue{campaign::WorldValueType::boolean, 1}
            ),
            campaign::set_world_flag(
                reference(core::ContentCategory::objective, "levy_paid"),
                campaign::WorldValue{campaign::WorldValueType::integer, -40}
            ),
        }
    );
    expect(static_cast<bool>(campaign::apply_outcome(state, recorded)), "the record commits");

    const campaign::ObjectiveRecord* const held =
        campaign::find_objective(state, hold_the_ford);
    expect(held != nullptr, "the objective is remembered");
    if (held != nullptr) {
        expect(
            held->result == campaign::ObjectiveOutcome::satisfied,
            "as satisfied"
        );
    }
    const campaign::WorldValue* const levy = campaign::find_world_value(
        state, reference(core::ContentCategory::objective, "levy_paid")
    );
    expect(levy != nullptr, "a typed world value is remembered");
    if (levy != nullptr) {
        expect(
            levy->type == campaign::WorldValueType::integer && levy->value == -40,
            "with its type and its sign"
        );
    }

    const auto lied = campaign::make_outcome_batch(
        source(0xEEEEULL, 2U),
        {campaign::set_world_flag(
            reference(core::ContentCategory::objective, "ford_open"),
            campaign::WorldValue{campaign::WorldValueType::boolean, 7}
        )}
    );
    expect(
        campaign::apply_outcome(state, lied).error ==
            campaign::OutcomeError::invalid_amount,
        "a boolean holds one of two values, and says so"
    );

    campaign::CampaignOutcomeBatch bad_selector;
    bad_selector.id = campaign::OutcomeId{7};
    bad_selector.operations.push_back(
        campaign::record_objective(
            hold_the_ford, campaign::ObjectiveOutcome::satisfied
        )
    );
    bad_selector.operations[0].selector = 9U;
    expect(
        campaign::apply_outcome(state, bad_selector).error ==
            campaign::OutcomeError::invalid_selector,
        "a selector outside the enumeration is refused"
    );
}

// A retry is not a second application. This is the interrupted-save case the
// design names: the same completed battle's outcome arrives twice and the
// campaign must end up where one application would have left it.
void a_retried_outcome_changes_nothing() {
    campaign::CampaignState state = opening_roster();

    const auto batch = campaign::make_outcome_batch(
        source(0xF00DULL, 1U),
        {
            campaign::record_permanent_death(first),
            campaign::grant_experience(second, 25U),
            campaign::consume_item(campaign::PersistentEntityId{}, potion, 1U),
            campaign::record_objective(
                hold_the_ford, campaign::ObjectiveOutcome::satisfied
            ),
        }
    );

    const auto once = campaign::apply_outcome(state, batch);
    expect(static_cast<bool>(once), "the outcome commits");
    expect(!once.already_applied, "for the first time");
    const std::uint64_t after_once = campaign::canonical_hash(state);

    const auto twice = campaign::apply_outcome(state, batch);
    expect(static_cast<bool>(twice), "applying it again is not an error");
    expect(twice.already_applied, "it reports that it was already applied");
    expect(
        campaign::canonical_hash(state) == after_once,
        "and inventory, progression, availability and flags are unchanged"
    );
    expect(
        campaign::item_quantity(state, campaign::PersistentEntityId{}, potion) ==
            2U,
        "the potion was spent once, not twice"
    );
    expect(
        campaign::find_unit(state, second)->progression.experience == 25U,
        "and the survivor was paid once"
    );
    expect(
        state.applied_outcomes.size() == 2U,
        "the campaign records the opening batch and this one"
    );

    // A rebuilt-from-scratch batch derives the same id, which is what makes a
    // retry across a restart safe rather than merely a retry in one process.
    const auto rebuilt = campaign::make_outcome_batch(
        source(0xF00DULL, 1U),
        {
            campaign::record_permanent_death(first),
            campaign::grant_experience(second, 25U),
            campaign::consume_item(campaign::PersistentEntityId{}, potion, 1U),
            campaign::record_objective(
                hold_the_ford, campaign::ObjectiveOutcome::satisfied
            ),
        }
    );
    const auto resumed = campaign::apply_outcome(state, rebuilt);
    expect(
        resumed.already_applied,
        "a batch rebuilt after a restart is recognised as the same outcome"
    );
    expect(
        campaign::canonical_hash(state) == after_once,
        "and still changes nothing"
    );
}

// Nothing partial escapes. The batch below kills somebody in its first
// operation and then asks for money that is not there; if the death survived
// the refusal, the campaign would have lost a character for nothing.
void a_failing_batch_leaves_no_half_applied_death() {
    campaign::CampaignState state = opening_roster();
    const std::uint64_t before = campaign::canonical_hash(state);

    const auto doomed = campaign::make_outcome_batch(
        source(0xDEADULL, 1U),
        {
            campaign::record_permanent_death(first),
            campaign::grant_experience(second, 10U),
            campaign::consume_item(campaign::PersistentEntityId{}, potion, 99U),
            campaign::record_objective(
                hold_the_ford, campaign::ObjectiveOutcome::satisfied
            ),
        }
    );

    const auto refused = campaign::apply_outcome(state, doomed);
    expect(
        refused.error == campaign::OutcomeError::insufficient_items,
        "the batch is refused at the operation that could not be honoured"
    );
    expect(refused.operation_index == 2U, "and says which one it was");
    expect(
        campaign::canonical_hash(state) == before,
        "and the campaign is exactly as it was"
    );
    expect(campaign::is_deployable(state, first), "nobody died");
    expect(
        campaign::find_unit(state, second)->progression.experience == 0U,
        "nobody was paid"
    );
    expect(
        !campaign::outcome_applied(state, doomed.id),
        "and the outcome was not recorded as applied"
    );

    // Refused once, it may be corrected and applied. A refusal is not a
    // commitment either.
    const auto corrected = campaign::make_outcome_batch(
        source(0xDEADULL, 1U),
        {
            campaign::record_permanent_death(first),
            campaign::grant_experience(second, 10U),
            campaign::consume_item(campaign::PersistentEntityId{}, potion, 1U),
        }
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, corrected)),
        "the corrected batch commits"
    );
    expect(!campaign::is_deployable(state, first), "and this time somebody died");
}

void an_unidentified_batch_is_refused() {
    campaign::CampaignState state = opening_roster();
    const std::uint64_t before = campaign::canonical_hash(state);

    campaign::CampaignOutcomeBatch unnamed;
    unnamed.operations.push_back(campaign::record_permanent_death(first));

    const auto refused = campaign::apply_outcome(state, unnamed);
    expect(
        refused.error == campaign::OutcomeError::unidentified_batch,
        "a batch with no id cannot be recorded, so it is not applied"
    );
    expect(
        campaign::canonical_hash(state) == before,
        "and nothing changed"
    );

    campaign::CampaignOutcomeBatch unknown_kind;
    unknown_kind.id = campaign::OutcomeId{11};
    campaign::CampaignOutcomeOperation operation;
    operation.kind = static_cast<campaign::OutcomeOperationKind>(200);
    unknown_kind.operations.push_back(operation);
    expect(
        campaign::apply_outcome(state, unknown_kind).error ==
            campaign::OutcomeError::unknown_operation,
        "an operation this build does not know is refused rather than skipped"
    );
    expect(
        campaign::canonical_hash(state) == before,
        "and still nothing changed"
    );
}

// The commit validates the whole candidate rather than the operations that
// built it. These are the arrangements it refuses, built by hand because no
// sequence of legal operations can reach them.
void the_whole_state_is_validated() {
    campaign::CampaignState state = opening_roster();
    expect(
        campaign::validate(state) == campaign::StateError::none,
        "a committed campaign validates"
    );

    campaign::CampaignState duplicated = state;
    duplicated.units.push_back(duplicated.units.front());
    expect(
        campaign::validate(duplicated) == campaign::StateError::unordered_collection,
        "a roster out of order is refused"
    );

    campaign::CampaignState reserved = state;
    reserved.units.front().id = campaign::PersistentEntityId{};
    expect(
        campaign::validate(reserved) == campaign::StateError::reserved_identity,
        "the reserved zero id is not a roster member"
    );

    campaign::CampaignState twinned = state;
    twinned.units[1].id = twinned.units[0].id;
    expect(
        campaign::validate(twinned) == campaign::StateError::duplicate_identity,
        "two members may not share one persistent id"
    );

    campaign::CampaignState hollow = state;
    hollow.store.push_back({torch, 0U});
    expect(
        campaign::validate(hollow) == campaign::StateError::empty_stack,
        "a stack holding nothing is not how absence is spelled"
    );

    campaign::CampaignState buried = state;
    buried.units.front().availability = campaign::Availability::dead;
    buried.units.front().carried.push_back({potion, 1U});
    expect(
        campaign::validate(buried) ==
            campaign::StateError::inconsistent_availability,
        "the dead do not hold durable state no rule can reach"
    );

    campaign::CampaignState repeated = state;
    repeated.applied_outcomes.push_back(repeated.applied_outcomes.front());
    expect(
        campaign::validate(repeated) != campaign::StateError::none,
        "an outcome recorded twice is not a history"
    );

    expect(
        campaign::state_error_name(campaign::StateError::empty_stack) ==
            "empty_stack",
        "every state error names itself"
    );
    expect(
        campaign::availability_name(campaign::Availability::dead) == "dead",
        "and so does every availability"
    );
    expect(
        campaign::outcome_error_name(campaign::OutcomeError::unit_is_dead) ==
            "unit_is_dead",
        "and every outcome error"
    );
    expect(
        campaign::outcome_operation_name(
            campaign::OutcomeOperationKind::consume_item
        ) == "consume_item",
        "and every operation kind"
    );
}

// Same inputs, same campaign, on every platform. The hash is a comparison
// diagnostic, so what is pinned here is equality rather than a literal.
void two_campaigns_told_the_same_things_are_the_same_campaign() {
    campaign::CampaignState left;
    campaign::CampaignState right;

    const std::vector<campaign::CampaignOutcomeOperation> operations{
        campaign::recruit_unit(second, healer),
        campaign::recruit_unit(first, lancer),
        campaign::add_item(second, torch, 4U),
        campaign::add_item(campaign::PersistentEntityId{}, potion, 1U),
        campaign::record_objective(
            bridge_burned, campaign::ObjectiveOutcome::failed
        ),
    };

    expect(
        static_cast<bool>(campaign::apply_outcome(
            left, campaign::make_outcome_batch(source(0x1234ULL, 0U), operations)
        )),
        "one campaign is told the story"
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(
            right, campaign::make_outcome_batch(source(0x1234ULL, 0U), operations)
        )),
        "and so is another"
    );
    expect(
        campaign::canonical_hash(left) == campaign::canonical_hash(right),
        "and they are the same campaign"
    );
    expect(
        left.units.front().id == first && left.units.back().id == second,
        "whose roster is in canonical order rather than the order it arrived in"
    );

    const std::vector<campaign::PersistentEntityId> roster =
        campaign::deployable_units(left);
    expect(
        roster.size() == 2U && roster[0] == first && roster[1] == second,
        "and whose deployable list is ordered too"
    );
}

}  // namespace

int main() {
    an_outcome_id_is_derived_and_reproducible();
    recruitment_adds_to_the_roster();
    permanent_death_excludes_a_member_from_every_later_encounter();
    a_fallen_member_leaves_their_kit_behind();
    inventory_is_spent_and_absence_has_one_spelling();
    objectives_and_world_flags_are_recorded();
    a_retried_outcome_changes_nothing();
    a_failing_batch_leaves_no_half_applied_death();
    an_unidentified_batch_is_refused();
    the_whole_state_is_validated();
    two_campaigns_told_the_same_things_are_the_same_campaign();
    return failures == 0 ? 0 : 1;
}
