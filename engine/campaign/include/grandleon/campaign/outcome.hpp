// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/state.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// How a battle changes a campaign.
//
// The accepted design says it in one sentence: "A battle produces a
// deterministic `CampaignOutcomeBatch` with a unique outcome ID and ordered
// typed operations. Campaign state records applied outcome IDs. The complete
// candidate state is validated before commit, making retries safe and
// preventing half-applied death/inventory changes."
//
// Three properties follow from that shape, and each is a failure this module
// exists to make impossible:
//
// * **Atomic.** A batch is applied to a candidate copy; the whole candidate is
//   validated; only then is it swapped in. A batch that fails at its fourth
//   operation leaves nothing behind from the first three: no character dead
//   with the payment for their rescue still uncollected.
// * **Idempotent.** The campaign records which outcome ids it has committed.
//   Applying one twice is not applying it twice; the second attempt reports
//   that it was already applied and changes nothing. A save written mid-commit
//   and resumed cannot double a death or spend a potion twice.
// * **Deterministic.** The id is derived from what produced the batch and from
//   what the batch says. The same battle producing the same consequences
//   produces the same id on every platform, with no counter, no clock, and no
//   draw from the random substrate. The streams there are named per purpose
//   and identity is not one of them.

namespace grandleon::campaign {

// What one operation does. Persisted in outcome history, so append only.
enum class OutcomeOperationKind : std::uint8_t {
    // Add a member to the roster. `subject` is the new persistent id,
    // `definition` the unit type they are an instance of. The id must be free:
    // recruiting onto an existing member, dead or alive, is refused rather
    // than merged, because the two would be indistinguishable afterwards.
    recruit_unit = 1,
    // Move a member between `unrecruited`, `available`, and `retired`.
    // `selector` is the new `Availability`. Never `dead`: permanent death has
    // its own operation because it is the one transition no rule may reverse,
    // and it should not be reachable by passing a different byte.
    set_availability = 2,
    // End a member permanently. Their carried items return to the shared
    // store; nothing else in the campaign forgets them.
    record_permanent_death = 3,
    // Add `amount` experience to a member.
    grant_experience = 4,
    // Advance a member by `amount` levels. Refused past
    // `maximum_progression_level`.
    advance_level = 5,
    // Add `amount` of `definition` to `subject`'s inventory, or to the shared
    // store when `subject` is the reserved zero id.
    add_item = 6,
    // Remove `amount` of `definition`. Refused when the owner holds fewer,
    // which is what makes an inventory consequence a fact rather than a wish.
    consume_item = 7,
    // Record how an objective ended. `selector` is the `ObjectiveOutcome`.
    record_objective = 8,
    // Set a typed world value. `selector` is the `WorldValueType`, `amount`
    // the value.
    set_world_flag = 9,
    // Add `amount` points to one of a member's growable stats permanently.
    // `selector` is the `GrowableStat`.
    //
    // Separate from `advance_level` on purpose. A level is a threshold crossed
    // and is the same for everybody who crosses it; what the crossing *gave*
    // is a roll, and it belongs in the batch as its own recorded fact so that
    // reading the batch tells you what happened rather than only that
    // something did. It is also why a level-up survives a save with no new
    // rule: the gain is committed the same way a death is.
    grow_stat = 10,
};

[[nodiscard]] std::string_view outcome_operation_name(
    OutcomeOperationKind kind
) noexcept;

// One typed operation, as a flat fixed-width record.
//
// Flat rather than a variant on purpose. A save writes these fields in this
// order and a Nintendo 64 reads them back; a `std::variant` would put a
// standard-library layout across that boundary, which `DESIGN.md` §3.3
// forbids. The tag says which fields carry meaning, the constructor functions
// below make the right ones for each kind, and the unused fields are zero so
// that two operations that mean the same thing encode the same way.
struct CampaignOutcomeOperation final {
    OutcomeOperationKind kind{};
    // `Availability`, `ObjectiveOutcome`, or `WorldValueType` depending on
    // `kind`; zero where the kind names no alternative.
    std::uint8_t selector{};
    // The member acted on, or the inventory owner; zero means the shared
    // store, and for kinds that name no member.
    PersistentEntityId subject{};
    // The unit type, item, objective, or world key.
    DefinitionRef definition{};
    // A quantity, an amount of experience, a count of levels, or a world
    // value; zero where the kind names no number.
    std::int64_t amount{};
};

// The typed constructors. A caller writes what it means and the flat record
// stays an encoding detail.

[[nodiscard]] CampaignOutcomeOperation recruit_unit(
    PersistentEntityId id,
    const DefinitionRef& definition
) noexcept;

[[nodiscard]] CampaignOutcomeOperation set_availability(
    PersistentEntityId id,
    Availability availability
) noexcept;

[[nodiscard]] CampaignOutcomeOperation record_permanent_death(
    PersistentEntityId id
) noexcept;

[[nodiscard]] CampaignOutcomeOperation grant_experience(
    PersistentEntityId id,
    std::uint32_t experience
) noexcept;

[[nodiscard]] CampaignOutcomeOperation advance_level(
    PersistentEntityId id,
    std::uint16_t levels
) noexcept;

[[nodiscard]] CampaignOutcomeOperation add_item(
    PersistentEntityId owner,
    const DefinitionRef& item,
    std::uint32_t quantity
) noexcept;

[[nodiscard]] CampaignOutcomeOperation consume_item(
    PersistentEntityId owner,
    const DefinitionRef& item,
    std::uint32_t quantity
) noexcept;

[[nodiscard]] CampaignOutcomeOperation record_objective(
    const DefinitionRef& objective,
    ObjectiveOutcome result
) noexcept;

[[nodiscard]] CampaignOutcomeOperation set_world_flag(
    const DefinitionRef& key,
    const WorldValue& value
) noexcept;

[[nodiscard]] CampaignOutcomeOperation grow_stat(
    PersistentEntityId id,
    GrowableStat stat,
    std::uint16_t points
) noexcept;

// A batch of consequences, identified and ordered.
//
// Order matters and is preserved: a batch may collect a reward and then spend
// it. Reordering the operations is a different batch and gets a different id.
struct CampaignOutcomeBatch final {
    OutcomeId id{};
    std::vector<CampaignOutcomeOperation> operations;
};

// Where a batch came from. The three fields together are what makes "the same
// battle" a thing a machine can check.
struct OutcomeSource final {
    // The encounter definition that was fought.
    DefinitionRef encounter{};
    // The canonical hash of the completed battle. Two playthroughs that ended
    // differently produced different consequences and must not share an id;
    // two that ended identically produced the same consequences and must.
    std::uint64_t battle_hash{};
    // Which completion this is within the campaign. The one field that
    // separates fighting the same encounter twice, to the same end, from
    // fighting it once, which a repeatable node in a campaign graph makes an
    // ordinary thing rather than an edge case.
    std::uint64_t sequence{};
};

// The id of a batch: FNV-1a-64 over the source and then over every operation,
// in fixed-width little-endian fields, finished the way the random substrate
// finishes a draw.
//
// The operations are part of it deliberately. An id derived from the source
// alone would name *where* an outcome came from but not *what it said*, so a
// corrected batch recomputed from the same battle would be mistaken for one
// already committed and silently dropped. Including them means a retry of the
// identical outcome is recognised as a retry, and anything else is a different
// outcome that must be judged on its own.
//
// Never zero: zero is an unidentified batch, and `apply_outcome` refuses one.
[[nodiscard]] OutcomeId derive_outcome_id(
    const OutcomeSource& source,
    const std::vector<CampaignOutcomeOperation>& operations
) noexcept;

// Build a batch with its derived id.
[[nodiscard]] CampaignOutcomeBatch make_outcome_batch(
    const OutcomeSource& source,
    std::vector<CampaignOutcomeOperation> operations
);

// The seed the growth stream is drawn from for one battle's consequences.
//
// This is the answer to "where does growth get its dice from", and the answer
// is deliberately *not* the encounter's own `core::RandomState`. Three things
// follow, and each is a property this repository has already paid for once:
//
// * **The battle is untouched.** Nothing about a level-up is battle-local
//   state, so no encounter's canonical hash, no forecast, and no golden moves
//   because a character grew. The simulation never learns that a campaign
//   exists, which is the invariant that keeps a canonical hash independent of a
//   save file.
// * **It is still seeded from canonical state.** `OutcomeSource::battle_hash`
//   *is* the completed battle's canonical hash, so the numbers a level-up rolls
//   are a function of the board, the units, the rules and every command that
//   was issued: reproducible on replay, identical on every platform, and
//   different for a battle that ended differently.
// * **A retry rolls the same numbers.** The source is the whole of the seed, so
//   recomputing the consequences of the same completion recomputes the same
//   growth, and `apply_outcome`'s idempotence covers the rest. Fighting the
//   same node a second time is a different `sequence` and therefore a different
//   seed, which is what makes a repeatable node ordinary rather than an edge
//   case.
//
// Derived by FNV-1a-64 over the source's fields in their declared order, then
// through `core::derive_random_seed`, which never returns zero.
[[nodiscard]] std::uint64_t derive_growth_seed(
    const OutcomeSource& source
) noexcept;

// Why a batch was refused. Persisted in diagnostics, so append only.
enum class OutcomeError : std::uint8_t {
    none = 0,
    // The batch carries the reserved zero id. An unidentified batch cannot be
    // recorded as applied, so it could be applied twice; it is refused instead.
    unidentified_batch,
    // The operation's tag is not one this build knows.
    unknown_operation,
    // The operation names a member the roster does not hold.
    unknown_unit,
    // Recruitment named an id already on the roster.
    unit_already_present,
    // The operation acts on a permanently dead member. This is the refusal the
    // whole layer exists for: no later map, and no stray outcome, brings back
    // someone the campaign has buried.
    unit_is_dead,
    // The reserved zero id was used where a member was required.
    reserved_identity,
    // A quantity, amount, or value outside what the field can hold.
    invalid_amount,
    // `selector` is not a member of the enumeration this kind names.
    invalid_selector,
    // The owner holds fewer of the item than the operation consumes.
    insufficient_items,
    // A quantity or a level count that would wrap.
    quantity_overflow,
    // The complete candidate state failed `validate`. The batch is refused
    // whole; `state_error` says which invariant it broke.
    invalid_candidate,
};

[[nodiscard]] std::string_view outcome_error_name(OutcomeError error) noexcept;

struct OutcomeApplication final {
    OutcomeError error{OutcomeError::none};
    // Which operation was refused. Meaningless when `error` is `none` or
    // `invalid_candidate`, which is a property of the whole batch.
    std::size_t operation_index{};
    // Set when `error` is `invalid_candidate`.
    StateError state_error{StateError::none};
    // The batch was already committed and nothing was done. Not an error: it
    // is the correct answer to a retry.
    bool already_applied{false};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == OutcomeError::none;
    }
};

// Apply a batch, or change nothing at all.
//
// The implementation is the guarantee: a candidate copy, every operation, a
// whole-state validation, and only then the swap. Nothing partial can escape,
// because nothing partial ever touches `state`.
[[nodiscard]] OutcomeApplication apply_outcome(
    CampaignState& state,
    const CampaignOutcomeBatch& batch
);

}  // namespace grandleon::campaign
