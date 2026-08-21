// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/specificity.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace grandleon::package_runtime {

enum class CampaignNodeKind : std::uint8_t {
    encounter = 1,
    terminal = 2,
    story = 3,
};

enum class ObjectiveOutcome : std::uint8_t {
    satisfied = 1,
    failed = 2,
};

enum class ConditionCombinator : std::uint8_t {
    all = 1,
    any = 2,
    none = 3,
};

// What a predicate asks about. The encoded byte was a result and is now a tag;
// its two original values keep their original meanings, so every package
// written before world flags could be read decodes unchanged. Append-only.
enum class CampaignPredicateKind : std::uint8_t {
    objective_result = 1,
    world_flag_equals = 3,
};

// One question. `subject` is the objective for `objective_result` and the flag
// key for `world_flag_equals`: one slot in the record, with the tag saying
// which identity is in it, mirroring `campaign::TransitionPredicate`.
struct CampaignPredicate final {
    CampaignPredicateKind kind{CampaignPredicateKind::objective_result};
    std::uint64_t subject{};
    ObjectiveOutcome result{ObjectiveOutcome::satisfied};
    // Only for `world_flag_equals`: the `campaign::WorldValueType` the flag
    // must be, and the value it must equal.
    std::uint8_t value_type{};
    std::int64_t value{};
};

// A transition taken only when its predicates hold under the combinator.
// Lower priority values are considered first. A bare condition is stored as
// `all` over one predicate, so evaluation has exactly one shape.
struct CampaignBranch final {
    std::uint64_t target_id{};
    std::uint16_t priority{};
    ConditionCombinator combinator{ConditionCombinator::all};
    std::vector<CampaignPredicate> predicates;
};

struct CampaignNode final {
    std::uint64_t id{};
    CampaignNodeKind kind{CampaignNodeKind::terminal};
    std::uint64_t encounter_id{};
    // Presented in order when the node is entered.
    std::vector<std::uint64_t> dialogue_ids;
    std::uint64_t unconditional_target_id{};
    bool has_unconditional_target{false};
    std::vector<CampaignBranch> branches;
};

// One character the authored company holds.
//
// The identity is the member's own: a placement fielding them carries it as
// its source key, so the same character is the same member on every board that
// places them. `join_node_id` is zero for a member the campaign is founded
// with, and otherwise the node whose completion brings them in.
struct CampaignMember final {
    std::uint64_t id{};
    std::uint64_t unit_type_id{};
    std::uint64_t join_node_id{};
    // Where the member's name is in the package, rather than a pointer to it.
    //
    // This is the shape `package_format::RecordView` uses, and it is used here
    // for the same reason: an offset survives everything a pointer does not.
    // A `LoadedPackage` may be copied, and its `bytes` may be appended to by
    // whoever assembled it. A pointer captured while it was decoded would be
    // pointing into a freed buffer after either, while an offset resolved
    // against `byte_data()` at the moment of asking is right in both cases.
    // Nothing is copied out of the package, so a decoded campaign still costs
    // no allocation and this header still pulls in no `std::string`, which is
    // what both consoles need of it.
    std::uint32_t name_offset{};
    std::uint16_t name_size{};

    // The name, read out of the package it was decoded from. Empty for a
    // member with no name, and empty when the package handed in is not the one
    // this member was decoded from and does not have those bytes. A wrong
    // package answers nothing rather than answering somebody else's characters.
    [[nodiscard]] std::string_view name_in(
        const package_format::LoadedPackage& package
    ) const noexcept;
};

// A quantity of one item the campaign puts into its shared store, and where in
// the flow it is put there.
//
// The same convention as `CampaignMember` and for the same reason: what a
// campaign is founded with and what a node gives it are one fact told at two
// moments, so they are one table. `join_node_id` is zero for the founding
// stock and otherwise the node whose completion grants it.
struct CampaignItemGrant final {
    // What is handed over. Exactly one of these is set: a grant names an item
    // or it names a weapon. They are two fields rather than one field and a
    // kind byte because the package writes them into two tables, and every
    // record of a table that means one thing needs no tag saying which.
    //
    // An identity is unique only within a category, so an item and a weapon
    // may share a source key and therefore share a stable id. Which field
    // holds it is the only thing that says which registry to resolve it
    // against.
    std::uint64_t item_id{};
    std::uint32_t quantity{};
    std::uint64_t join_node_id{};
    // Last, and deliberately: this is an aggregate that callers brace-initialise
    // by position, so a field added in the middle would quietly become whatever
    // the next one used to be.
    std::uint64_t weapon_id{};

    [[nodiscard]] bool grants_a_weapon() const noexcept {
        return weapon_id != 0U;
    }
};

// What this campaign does with a character who falls in battle.
//
// A statement about what kind of game this is, and the only thing it decides is
// what the campaign records after the battle is over. Under either value a
// character at zero health leaves the battlefield when they reach it, so a
// completed battle's canonical hash is the same hash under both. The rules
// never learn that this byte exists.
//
// Values are serialized, so this list is append-only.
enum class CharacterLoss : std::uint8_t {
    // A character who falls is dead. Their kit returns to the company's store
    // and no later map brings them back. The meaning of an absent rule, and what
    // every campaign compiled before a project could state one means.
    permanent = 1,
    // A character who falls is carried off the field and rejoins the company
    // after the battle, still holding whatever the battle left them with.
    recoverable = 2,
};

[[nodiscard]] std::string_view character_loss_name(CharacterLoss loss) noexcept;

struct CampaignDefinition final {
    std::uint64_t entry_node_id{};
    std::vector<CampaignNode> nodes;
    // The founding members in authored order, then each node's recruits in
    // flow order. A campaign compiled before rosters were authorable carries
    // none, and is a campaign no company can be founded from.
    std::vector<CampaignMember> members;
    // The founding stock in authored order, then each node's grants in flow
    // order. A campaign that authors none carries none, which is a store that
    // fills only from what a battle leaves behind, and what every campaign
    // compiled before this says.
    std::vector<CampaignItemGrant> grants;
    // What the author wrote about individual members beyond their unit types,
    // in authored member order, holding an entry only for a member who authors
    // something. A campaign in which nobody does carries none, which is a
    // company of characters who are exactly their classes. That is what every
    // campaign compiled before this says, and what keeps such a campaign's
    // record byte-identical.
    //
    // The founding roster and every node's recruits share one table for the
    // same reason they share one member shape: a recruit is a member of the
    // company from the moment they join.
    std::vector<MemberSpecificity> specificities;
    // What a fall costs the company, and whether the company can fall at all.
    //
    // Both are resolved by the compiler out of what the *project* declared, the
    // way the project's default turn order is resolved into every encounter's
    // one turn-order byte. They sit on the campaign because the campaign is what
    // holds a company, and because neither means anything without one.
    //
    // A campaign compiled before either could be declared ends at its last
    // specificity and decodes as a permanent-loss campaign whose characters
    // fall exactly as they always have, which is also what a project declaring
    // neither compiles to, byte for byte.
    CharacterLoss character_loss{CharacterLoss::permanent};
    // Whether the members of this campaign's company cannot be reduced below one
    // health.
    //
    // A testing aid rather than a way to play: it exists so that somebody
    // walking their own campaign through does not have to deal with dying on the
    // way. It is deliberately not a third value of `CharacterLoss`, because it
    // is not an answer to the question that enumeration asks.
    //
    // It is declared in the package all the same, and it reaches the simulation
    // through `campaign_runtime`'s roster join as `simulation::UnitDefinition::endures`
    // on every board unit a member stands in. That is not ceremony: it changes
    // what the rules do, so two people holding the same package must get the same
    // battles out of it, and a client that could switch it on for itself would be
    // a client whose battles nobody else could reproduce.
    bool invulnerable_for_testing{false};
};

enum class CampaignError : std::uint8_t {
    none = 0,
    missing_section,
    missing_record,
    malformed_payload,
    missing_reference,
    unsupported_flow,
    already_complete,
    outcome_incomplete,
};

[[nodiscard]] std::string_view error_name(CampaignError error) noexcept;

struct CampaignLoadResult final {
    CampaignError error{CampaignError::none};
    CampaignDefinition definition;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CampaignError::none;
    }
};

[[nodiscard]] CampaignLoadResult load_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id
);

class CampaignCursor final {
public:
    // A definition whose entry node is missing (which load_campaign never
    // produces) yields a cursor that is already complete rather than one that
    // indexes out of bounds.
    explicit CampaignCursor(CampaignDefinition definition);

    [[nodiscard]] const CampaignNode& current() const noexcept;
    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] CampaignError advance_after(
        simulation::Outcome outcome
    ) noexcept;

    // Advances using recorded objective results, taking the satisfied branch of
    // lowest priority value and otherwise the unconditional transition.
    [[nodiscard]] CampaignError advance_after(
        simulation::Outcome outcome,
        const std::vector<simulation::ObjectiveResult>& objectives
    ) noexcept;

    // Advances past a story node, which has no outcome to evaluate.
    [[nodiscard]] CampaignError advance_story() noexcept;

private:
    CampaignDefinition definition_;
    std::size_t current_index_{};
};

}  // namespace grandleon::package_runtime
