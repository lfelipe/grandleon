// SPDX-License-Identifier: MIT
#include <grandleon/campaign/outcome.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace grandleon::campaign {

namespace {

template <typename Unsigned>
[[nodiscard]] std::uint64_t hash_integer(
    std::uint64_t hash,
    Unsigned value
) noexcept {
    for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
        hash = core::fnv1a64_step(
            hash,
            static_cast<std::uint8_t>(value >> (index * 8U))
        );
    }
    return hash;
}

// The finalizer the random substrate uses, for the reason it uses it: FNV-1a
// diffuses low bits upward and never downward, so two batches differing only
// in a low byte of their last field would produce ids that differ almost as
// predictably. It introduces no constant this repository has not already
// proved bit-for-bit on every target.
[[nodiscard]] std::uint64_t finalize(std::uint64_t hash) noexcept {
    hash ^= hash >> 31U;
    hash *= core::fnv1a64_prime;
    hash ^= hash >> 27U;
    hash *= core::fnv1a64_prime;
    hash ^= hash >> 33U;
    hash *= core::fnv1a64_prime;
    hash ^= hash >> 29U;
    hash *= core::fnv1a64_prime;
    hash ^= hash >> 32U;
    // Zero means "no outcome" everywhere in this module, so it is not an id a
    // batch may have.
    return hash == 0U ? core::fnv1a64_offset_basis : hash;
}

[[nodiscard]] std::vector<InventoryStack>* owned_stacks(
    CampaignState& state,
    PersistentEntityId owner
) noexcept {
    if (owner.value == 0U) {
        return &state.store;
    }
    const auto position = std::lower_bound(
        state.units.begin(),
        state.units.end(),
        owner,
        [](const PersistentUnit& unit, PersistentEntityId value) {
            return unit.id < value;
        }
    );
    if (position == state.units.end() || !(position->id == owner)) {
        return nullptr;
    }
    return &position->carried;
}

[[nodiscard]] PersistentUnit* mutable_unit(
    CampaignState& state,
    PersistentEntityId id
) noexcept {
    const auto position = std::lower_bound(
        state.units.begin(),
        state.units.end(),
        id,
        [](const PersistentUnit& unit, PersistentEntityId value) {
            return unit.id < value;
        }
    );
    if (position == state.units.end() || !(position->id == id)) {
        return nullptr;
    }
    return &*position;
}

// Add a quantity to a sorted stack list, keeping it sorted and merged.
[[nodiscard]] OutcomeError add_to_stacks(
    std::vector<InventoryStack>& stacks,
    const DefinitionRef& item,
    std::uint32_t quantity
) {
    const auto position = std::lower_bound(
        stacks.begin(),
        stacks.end(),
        item,
        [](const InventoryStack& stack, const DefinitionRef& value) {
            return definition_ref_less(stack.item, value);
        }
    );
    if (position != stacks.end() && position->item == item) {
        const std::uint64_t total =
            static_cast<std::uint64_t>(position->quantity) + quantity;
        if (total > std::numeric_limits<std::uint32_t>::max()) {
            return OutcomeError::quantity_overflow;
        }
        position->quantity = static_cast<std::uint32_t>(total);
        return OutcomeError::none;
    }
    stacks.insert(position, InventoryStack{item, quantity});
    return OutcomeError::none;
}

[[nodiscard]] OutcomeError remove_from_stacks(
    std::vector<InventoryStack>& stacks,
    const DefinitionRef& item,
    std::uint32_t quantity
) {
    const auto position = std::lower_bound(
        stacks.begin(),
        stacks.end(),
        item,
        [](const InventoryStack& stack, const DefinitionRef& value) {
            return definition_ref_less(stack.item, value);
        }
    );
    if (position == stacks.end() || !(position->item == item)) {
        return OutcomeError::insufficient_items;
    }
    if (position->quantity < quantity) {
        return OutcomeError::insufficient_items;
    }
    position->quantity -= quantity;
    if (position->quantity == 0U) {
        // Absence is spelled one way: an emptied stack is removed rather than
        // kept at zero, so two campaigns that hold nothing hold it identically.
        stacks.erase(position);
    }
    return OutcomeError::none;
}

[[nodiscard]] bool positive_quantity(std::int64_t amount) noexcept {
    return amount > 0 &&
           amount <=
               static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
}

OutcomeError apply_operation(
    CampaignState& state,
    const CampaignOutcomeOperation& operation
) {
    switch (operation.kind) {
        case OutcomeOperationKind::recruit_unit: {
            if (operation.subject.value == 0U) {
                return OutcomeError::reserved_identity;
            }
            const auto position = std::lower_bound(
                state.units.begin(),
                state.units.end(),
                operation.subject,
                [](const PersistentUnit& unit, PersistentEntityId value) {
                    return unit.id < value;
                }
            );
            if (position != state.units.end() &&
                position->id == operation.subject) {
                return OutcomeError::unit_already_present;
            }
            PersistentUnit unit;
            unit.id = operation.subject;
            unit.definition = operation.definition;
            unit.availability = Availability::available;
            state.units.insert(position, std::move(unit));
            return OutcomeError::none;
        }
        case OutcomeOperationKind::set_availability: {
            if (operation.selector != static_cast<std::uint8_t>(Availability::unrecruited) &&
                operation.selector != static_cast<std::uint8_t>(Availability::available) &&
                operation.selector != static_cast<std::uint8_t>(Availability::retired)) {
                return OutcomeError::invalid_selector;
            }
            PersistentUnit* const unit =
                mutable_unit(state, operation.subject);
            if (unit == nullptr) {
                return OutcomeError::unknown_unit;
            }
            if (unit->availability == Availability::dead) {
                return OutcomeError::unit_is_dead;
            }
            unit->availability = static_cast<Availability>(operation.selector);
            return OutcomeError::none;
        }
        case OutcomeOperationKind::record_permanent_death: {
            PersistentUnit* const unit =
                mutable_unit(state, operation.subject);
            if (unit == nullptr) {
                return OutcomeError::unknown_unit;
            }
            if (unit->availability == Availability::dead) {
                // Two batches both claiming one death is not a retry (a retry
                // is caught by the outcome id long before here). It is a
                // disagreement about what happened, and the campaign refuses
                // to guess.
                return OutcomeError::unit_is_dead;
            }
            std::vector<InventoryStack> carried;
            carried.swap(unit->carried);
            unit->availability = Availability::dead;
            for (const InventoryStack& stack : carried) {
                const OutcomeError moved =
                    add_to_stacks(state.store, stack.item, stack.quantity);
                if (moved != OutcomeError::none) {
                    return moved;
                }
            }
            return OutcomeError::none;
        }
        case OutcomeOperationKind::grant_experience: {
            if (!positive_quantity(operation.amount)) {
                return OutcomeError::invalid_amount;
            }
            PersistentUnit* const unit =
                mutable_unit(state, operation.subject);
            if (unit == nullptr) {
                return OutcomeError::unknown_unit;
            }
            if (unit->availability == Availability::dead) {
                return OutcomeError::unit_is_dead;
            }
            const std::uint64_t total =
                static_cast<std::uint64_t>(unit->progression.experience) +
                static_cast<std::uint64_t>(operation.amount);
            if (total > std::numeric_limits<std::uint32_t>::max()) {
                return OutcomeError::quantity_overflow;
            }
            unit->progression.experience = static_cast<std::uint32_t>(total);
            return OutcomeError::none;
        }
        case OutcomeOperationKind::advance_level: {
            if (operation.amount <= 0 ||
                operation.amount >
                    static_cast<std::int64_t>(
                        std::numeric_limits<std::uint16_t>::max()
                    )) {
                return OutcomeError::invalid_amount;
            }
            PersistentUnit* const unit =
                mutable_unit(state, operation.subject);
            if (unit == nullptr) {
                return OutcomeError::unknown_unit;
            }
            if (unit->availability == Availability::dead) {
                return OutcomeError::unit_is_dead;
            }
            const std::uint32_t total =
                static_cast<std::uint32_t>(unit->progression.level) +
                static_cast<std::uint32_t>(operation.amount);
            // The ceiling is `maximum_progression_level` and not the field's
            // own limit. A stated cap is a rule an author can plan against;
            // 65535 is an accident of storage. A batch that would pass it is
            // refused whole rather than clamped, because a clamp would leave a
            // campaign that quietly disagrees with the batch that produced it.
            if (total > static_cast<std::uint32_t>(maximum_progression_level)) {
                return OutcomeError::quantity_overflow;
            }
            unit->progression.level = static_cast<std::uint16_t>(total);
            return OutcomeError::none;
        }
        case OutcomeOperationKind::grow_stat: {
            if (!positive_quantity(operation.amount) ||
                operation.amount >
                    static_cast<std::int64_t>(
                        std::numeric_limits<std::uint16_t>::max()
                    )) {
                return OutcomeError::invalid_amount;
            }
            if (operation.selector >=
                static_cast<std::uint8_t>(growable_stat_count)) {
                return OutcomeError::invalid_selector;
            }
            PersistentUnit* const unit =
                mutable_unit(state, operation.subject);
            if (unit == nullptr) {
                return OutcomeError::unknown_unit;
            }
            if (unit->availability == Availability::dead) {
                return OutcomeError::unit_is_dead;
            }
            std::uint16_t& gain =
                unit->progression.gained[operation.selector];
            const std::uint32_t total = static_cast<std::uint32_t>(gain) +
                                        static_cast<std::uint32_t>(operation.amount);
            if (total > std::numeric_limits<std::uint16_t>::max()) {
                return OutcomeError::quantity_overflow;
            }
            gain = static_cast<std::uint16_t>(total);
            return OutcomeError::none;
        }
        case OutcomeOperationKind::add_item:
        case OutcomeOperationKind::consume_item: {
            if (!positive_quantity(operation.amount)) {
                return OutcomeError::invalid_amount;
            }
            if (operation.subject.value != 0U) {
                const PersistentUnit* const unit =
                    find_unit(state, operation.subject);
                if (unit == nullptr) {
                    return OutcomeError::unknown_unit;
                }
                if (unit->availability == Availability::dead) {
                    return OutcomeError::unit_is_dead;
                }
            }
            std::vector<InventoryStack>* const stacks =
                owned_stacks(state, operation.subject);
            if (stacks == nullptr) {
                return OutcomeError::unknown_unit;
            }
            const auto quantity =
                static_cast<std::uint32_t>(operation.amount);
            return operation.kind == OutcomeOperationKind::add_item
                       ? add_to_stacks(*stacks, operation.definition, quantity)
                       : remove_from_stacks(
                             *stacks, operation.definition, quantity
                         );
        }
        case OutcomeOperationKind::record_objective: {
            if (operation.selector !=
                    static_cast<std::uint8_t>(ObjectiveOutcome::satisfied) &&
                operation.selector !=
                    static_cast<std::uint8_t>(ObjectiveOutcome::failed)) {
                return OutcomeError::invalid_selector;
            }
            const auto result =
                static_cast<ObjectiveOutcome>(operation.selector);
            const auto position = std::lower_bound(
                state.objectives.begin(),
                state.objectives.end(),
                operation.definition,
                [](const ObjectiveRecord& record, const DefinitionRef& value) {
                    return definition_ref_less(record.objective, value);
                }
            );
            if (position != state.objectives.end() &&
                position->objective == operation.definition) {
                position->result = result;
                return OutcomeError::none;
            }
            state.objectives.insert(
                position,
                ObjectiveRecord{operation.definition, result}
            );
            return OutcomeError::none;
        }
        case OutcomeOperationKind::set_world_flag: {
            if (operation.selector !=
                    static_cast<std::uint8_t>(WorldValueType::boolean) &&
                operation.selector !=
                    static_cast<std::uint8_t>(WorldValueType::integer)) {
                return OutcomeError::invalid_selector;
            }
            const auto type = static_cast<WorldValueType>(operation.selector);
            if (type == WorldValueType::boolean &&
                operation.amount != 0 && operation.amount != 1) {
                return OutcomeError::invalid_amount;
            }
            const WorldValue value{type, operation.amount};
            const auto position = std::lower_bound(
                state.world.begin(),
                state.world.end(),
                operation.definition,
                [](const WorldFlag& flag, const DefinitionRef& key) {
                    return definition_ref_less(flag.key, key);
                }
            );
            if (position != state.world.end() &&
                position->key == operation.definition) {
                position->value = value;
                return OutcomeError::none;
            }
            state.world.insert(
                position,
                WorldFlag{operation.definition, value}
            );
            return OutcomeError::none;
        }
    }
    return OutcomeError::unknown_operation;
}

[[nodiscard]] std::uint64_t hash_operation(
    std::uint64_t hash,
    const CampaignOutcomeOperation& operation
) noexcept {
    hash = hash_integer(hash, static_cast<std::uint8_t>(operation.kind));
    hash = hash_integer(hash, operation.selector);
    hash = hash_integer(hash, operation.subject.value);
    hash = hash_definition_ref(hash, operation.definition);
    return hash_integer(hash, static_cast<std::uint64_t>(operation.amount));
}

}  // namespace

std::string_view outcome_operation_name(OutcomeOperationKind kind) noexcept {
    switch (kind) {
        case OutcomeOperationKind::recruit_unit:
            return "recruit_unit";
        case OutcomeOperationKind::set_availability:
            return "set_availability";
        case OutcomeOperationKind::record_permanent_death:
            return "record_permanent_death";
        case OutcomeOperationKind::grant_experience:
            return "grant_experience";
        case OutcomeOperationKind::advance_level:
            return "advance_level";
        case OutcomeOperationKind::add_item:
            return "add_item";
        case OutcomeOperationKind::consume_item:
            return "consume_item";
        case OutcomeOperationKind::record_objective:
            return "record_objective";
        case OutcomeOperationKind::set_world_flag:
            return "set_world_flag";
        case OutcomeOperationKind::grow_stat:
            return "grow_stat";
    }
    return "unknown";
}

std::string_view outcome_error_name(OutcomeError error) noexcept {
    switch (error) {
        case OutcomeError::none:
            return "none";
        case OutcomeError::unidentified_batch:
            return "unidentified_batch";
        case OutcomeError::unknown_operation:
            return "unknown_operation";
        case OutcomeError::unknown_unit:
            return "unknown_unit";
        case OutcomeError::unit_already_present:
            return "unit_already_present";
        case OutcomeError::unit_is_dead:
            return "unit_is_dead";
        case OutcomeError::reserved_identity:
            return "reserved_identity";
        case OutcomeError::invalid_amount:
            return "invalid_amount";
        case OutcomeError::invalid_selector:
            return "invalid_selector";
        case OutcomeError::insufficient_items:
            return "insufficient_items";
        case OutcomeError::quantity_overflow:
            return "quantity_overflow";
        case OutcomeError::invalid_candidate:
            return "invalid_candidate";
    }
    return "unknown";
}

CampaignOutcomeOperation recruit_unit(
    PersistentEntityId id,
    const DefinitionRef& definition
) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::recruit_unit;
    operation.subject = id;
    operation.definition = definition;
    return operation;
}

CampaignOutcomeOperation set_availability(
    PersistentEntityId id,
    Availability availability
) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::set_availability;
    operation.selector = static_cast<std::uint8_t>(availability);
    operation.subject = id;
    return operation;
}

CampaignOutcomeOperation record_permanent_death(PersistentEntityId id) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::record_permanent_death;
    operation.subject = id;
    return operation;
}

CampaignOutcomeOperation grant_experience(
    PersistentEntityId id,
    std::uint32_t experience
) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::grant_experience;
    operation.subject = id;
    operation.amount = static_cast<std::int64_t>(experience);
    return operation;
}

CampaignOutcomeOperation advance_level(
    PersistentEntityId id,
    std::uint16_t levels
) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::advance_level;
    operation.subject = id;
    operation.amount = static_cast<std::int64_t>(levels);
    return operation;
}

CampaignOutcomeOperation add_item(
    PersistentEntityId owner,
    const DefinitionRef& item,
    std::uint32_t quantity
) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::add_item;
    operation.subject = owner;
    operation.definition = item;
    operation.amount = static_cast<std::int64_t>(quantity);
    return operation;
}

CampaignOutcomeOperation consume_item(
    PersistentEntityId owner,
    const DefinitionRef& item,
    std::uint32_t quantity
) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::consume_item;
    operation.subject = owner;
    operation.definition = item;
    operation.amount = static_cast<std::int64_t>(quantity);
    return operation;
}

CampaignOutcomeOperation record_objective(
    const DefinitionRef& objective,
    ObjectiveOutcome result
) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::record_objective;
    operation.selector = static_cast<std::uint8_t>(result);
    operation.definition = objective;
    return operation;
}

CampaignOutcomeOperation set_world_flag(
    const DefinitionRef& key,
    const WorldValue& value
) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::set_world_flag;
    operation.selector = static_cast<std::uint8_t>(value.type);
    operation.definition = key;
    operation.amount = value.value;
    return operation;
}

CampaignOutcomeOperation grow_stat(
    PersistentEntityId id,
    GrowableStat stat,
    std::uint16_t points
) noexcept {
    CampaignOutcomeOperation operation;
    operation.kind = OutcomeOperationKind::grow_stat;
    operation.selector = static_cast<std::uint8_t>(stat);
    operation.subject = id;
    operation.amount = static_cast<std::int64_t>(points);
    return operation;
}

OutcomeId derive_outcome_id(
    const OutcomeSource& source,
    const std::vector<CampaignOutcomeOperation>& operations
) noexcept {
    std::uint64_t hash = core::fnv1a64_offset_basis;
    hash = hash_definition_ref(hash, source.encounter);
    hash = hash_integer(hash, source.battle_hash);
    hash = hash_integer(hash, source.sequence);
    hash = hash_integer(hash, static_cast<std::uint32_t>(operations.size()));
    for (const CampaignOutcomeOperation& operation : operations) {
        hash = hash_operation(hash, operation);
    }
    return OutcomeId{finalize(hash)};
}

std::uint64_t derive_growth_seed(const OutcomeSource& source) noexcept {
    // The source and nothing else. Not the operations, because the operations
    // are what the rolls produce and a seed that depended on them would have to
    // be computed before it could be computed.
    std::uint64_t hash = core::fnv1a64_offset_basis;
    hash = hash_definition_ref(hash, source.encounter);
    hash = hash_integer(hash, source.battle_hash);
    hash = hash_integer(hash, source.sequence);
    return core::derive_random_seed(finalize(hash));
}

CampaignOutcomeBatch make_outcome_batch(
    const OutcomeSource& source,
    std::vector<CampaignOutcomeOperation> operations
) {
    CampaignOutcomeBatch batch;
    batch.id = derive_outcome_id(source, operations);
    batch.operations = std::move(operations);
    return batch;
}

OutcomeApplication apply_outcome(
    CampaignState& state,
    const CampaignOutcomeBatch& batch
) {
    if (batch.id.value == 0U) {
        return {OutcomeError::unidentified_batch, 0U, StateError::none, false};
    }
    if (outcome_applied(state, batch.id)) {
        return {OutcomeError::none, 0U, StateError::none, true};
    }

    // The candidate. Everything below happens to this copy, so a refusal at
    // any point costs the live campaign nothing: not a death, not a coin.
    CampaignState candidate = state;
    for (std::size_t index = 0; index < batch.operations.size(); ++index) {
        const OutcomeError error =
            apply_operation(candidate, batch.operations[index]);
        if (error != OutcomeError::none) {
            return {error, index, StateError::none, false};
        }
    }

    const auto position = std::lower_bound(
        candidate.applied_outcomes.begin(),
        candidate.applied_outcomes.end(),
        batch.id
    );
    candidate.applied_outcomes.insert(position, batch.id);

    // The whole candidate, not the operations that made it. The design's
    // guarantee is about the state a commit produces, and an operation that is
    // individually legal can still leave an arrangement that is not.
    const StateError invalid = validate(candidate);
    if (invalid != StateError::none) {
        return {OutcomeError::invalid_candidate, 0U, invalid, false};
    }

    state = std::move(candidate);
    return {OutcomeError::none, 0U, StateError::none, false};
}

}  // namespace grandleon::campaign
