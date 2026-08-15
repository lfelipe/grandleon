// SPDX-License-Identifier: MIT
#include <grandleon/campaign/state.hpp>

#include <algorithm>

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

[[nodiscard]] std::uint64_t hash_stacks(
    std::uint64_t hash,
    const std::vector<InventoryStack>& stacks
) noexcept {
    hash = hash_integer(hash, static_cast<std::uint32_t>(stacks.size()));
    for (const InventoryStack& stack : stacks) {
        hash = hash_definition_ref(hash, stack.item);
        hash = hash_integer(hash, stack.quantity);
    }
    return hash;
}

[[nodiscard]] StateError validate_stacks(
    const std::vector<InventoryStack>& stacks
) noexcept {
    for (std::size_t index = 0; index < stacks.size(); ++index) {
        if (stacks[index].quantity == 0U) {
            return StateError::empty_stack;
        }
        if (index == 0U) {
            continue;
        }
        if (stacks[index - 1U].item == stacks[index].item) {
            return StateError::duplicate_identity;
        }
        if (!definition_ref_less(stacks[index - 1U].item, stacks[index].item)) {
            return StateError::unordered_collection;
        }
    }
    return StateError::none;
}

// A reference nobody set. Category zero is not a member of
// `core::ContentCategory` and never will be (the enumeration starts at one),
// so it is the one spelling of "unset" that no authored identity can collide
// with, and it is what a decoder reads out of zeroed bytes.
[[nodiscard]] bool is_unset(const DefinitionRef& reference) noexcept {
    return static_cast<std::uint32_t>(reference.category) == 0U;
}

[[nodiscard]] StateError validate_progress(const CampaignState& state) noexcept {
    const CampaignProgress& progress = state.progress;
    if (!progress.active) {
        // An inactive progression is empty in every field, so that "has this
        // campaign entered a graph?" has exactly one representation and the
        // save has exactly one encoding of it.
        if (!progress.history.empty() || !is_unset(progress.campaign) ||
            !is_unset(progress.active_node)) {
            return StateError::inconsistent_progression;
        }
        return StateError::none;
    }
    if (progress.history.empty() || is_unset(progress.campaign) ||
        is_unset(progress.active_node)) {
        return StateError::inconsistent_progression;
    }
    if (!(progress.history.back().node == progress.active_node)) {
        return StateError::inconsistent_progression;
    }
    // The entry step is the only one nothing caused.
    if (progress.history.front().cause.value != 0U) {
        return StateError::inconsistent_progression;
    }
    for (std::size_t index = 0; index < progress.history.size(); ++index) {
        const ProgressionEntry& entry = progress.history[index];
        if (is_unset(entry.node)) {
            return StateError::inconsistent_progression;
        }
        if (index == 0U) {
            continue;
        }
        // Every later step was caused by a batch this campaign committed. A
        // route that cites an outcome the roster never saw is a route the
        // roster cannot account for.
        if (entry.cause.value == 0U || !outcome_applied(state, entry.cause)) {
            return StateError::inconsistent_progression;
        }
        // At most one edge per completion event, which means at most one step
        // per committed batch.
        for (std::size_t earlier = 1; earlier < index; ++earlier) {
            if (progress.history[earlier].cause == entry.cause) {
                return StateError::inconsistent_progression;
            }
        }
    }
    return StateError::none;
}

}  // namespace

std::string_view availability_name(Availability availability) noexcept {
    switch (availability) {
        case Availability::unrecruited:
            return "unrecruited";
        case Availability::available:
            return "available";
        case Availability::retired:
            return "retired";
        case Availability::dead:
            return "dead";
    }
    return "unknown";
}

std::string_view growable_stat_name(GrowableStat stat) noexcept {
    switch (stat) {
        case GrowableStat::health:
            return "health";
        case GrowableStat::strength:
            return "strength";
        case GrowableStat::defense:
            return "defense";
        case GrowableStat::resistance:
            return "resistance";
        case GrowableStat::movement:
            return "movement";
        case GrowableStat::action_points:
            return "action_points";
        case GrowableStat::skill:
            return "skill";
        case GrowableStat::luck:
            return "luck";
        case GrowableStat::evasion:
            return "evasion";
        case GrowableStat::magic:
            return "magic";
    }
    return "unknown";
}

std::string_view state_error_name(StateError error) noexcept {
    switch (error) {
        case StateError::none:
            return "none";
        case StateError::reserved_identity:
            return "reserved_identity";
        case StateError::duplicate_identity:
            return "duplicate_identity";
        case StateError::unordered_collection:
            return "unordered_collection";
        case StateError::empty_stack:
            return "empty_stack";
        case StateError::inconsistent_availability:
            return "inconsistent_availability";
        case StateError::inconsistent_progression:
            return "inconsistent_progression";
    }
    return "unknown";
}

const PersistentUnit* find_unit(
    const CampaignState& state,
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

bool is_deployable(const CampaignState& state, PersistentEntityId id) noexcept {
    const PersistentUnit* const unit = find_unit(state, id);
    return unit != nullptr && unit->availability == Availability::available;
}

std::vector<PersistentEntityId> deployable_units(const CampaignState& state) {
    std::vector<PersistentEntityId> deployable;
    for (const PersistentUnit& unit : state.units) {
        if (unit.availability == Availability::available) {
            deployable.push_back(unit.id);
        }
    }
    return deployable;
}

std::uint32_t item_quantity(
    const CampaignState& state,
    PersistentEntityId owner,
    const DefinitionRef& item
) noexcept {
    const std::vector<InventoryStack>* stacks = &state.store;
    if (owner.value != 0U) {
        const PersistentUnit* const unit = find_unit(state, owner);
        if (unit == nullptr) {
            return 0U;
        }
        stacks = &unit->carried;
    }
    const auto position = std::lower_bound(
        stacks->begin(),
        stacks->end(),
        item,
        [](const InventoryStack& stack, const DefinitionRef& value) {
            return definition_ref_less(stack.item, value);
        }
    );
    if (position == stacks->end() || !(position->item == item)) {
        return 0U;
    }
    return position->quantity;
}

const WorldValue* find_world_value(
    const CampaignState& state,
    const DefinitionRef& key
) noexcept {
    const auto position = std::lower_bound(
        state.world.begin(),
        state.world.end(),
        key,
        [](const WorldFlag& flag, const DefinitionRef& value) {
            return definition_ref_less(flag.key, value);
        }
    );
    if (position == state.world.end() || !(position->key == key)) {
        return nullptr;
    }
    return &position->value;
}

const ObjectiveRecord* find_objective(
    const CampaignState& state,
    const DefinitionRef& objective
) noexcept {
    const auto position = std::lower_bound(
        state.objectives.begin(),
        state.objectives.end(),
        objective,
        [](const ObjectiveRecord& record, const DefinitionRef& value) {
            return definition_ref_less(record.objective, value);
        }
    );
    if (position == state.objectives.end() ||
        !(position->objective == objective)) {
        return nullptr;
    }
    return &*position;
}

bool outcome_applied(const CampaignState& state, OutcomeId id) noexcept {
    return std::binary_search(
        state.applied_outcomes.begin(),
        state.applied_outcomes.end(),
        id
    );
}

StateError validate(const CampaignState& state) noexcept {
    for (std::size_t index = 0; index < state.units.size(); ++index) {
        const PersistentUnit& unit = state.units[index];
        if (unit.id.value == 0U) {
            return StateError::reserved_identity;
        }
        if (index > 0U) {
            if (state.units[index - 1U].id == unit.id) {
                return StateError::duplicate_identity;
            }
            if (!(state.units[index - 1U].id < unit.id)) {
                return StateError::unordered_collection;
            }
        }
        // A dead member carries nothing. What they held returns to the shared
        // store when the death commits, because the alternative is durable
        // state that no rule can ever reach again, which is a quiet way of
        // losing it, and this layer's job is to lose nothing quietly.
        if (unit.availability == Availability::dead && !unit.carried.empty()) {
            return StateError::inconsistent_availability;
        }
        const StateError carried = validate_stacks(unit.carried);
        if (carried != StateError::none) {
            return carried;
        }
    }

    const StateError store = validate_stacks(state.store);
    if (store != StateError::none) {
        return store;
    }

    for (std::size_t index = 1; index < state.objectives.size(); ++index) {
        const DefinitionRef& previous = state.objectives[index - 1U].objective;
        const DefinitionRef& current = state.objectives[index].objective;
        if (previous == current) {
            return StateError::duplicate_identity;
        }
        if (!definition_ref_less(previous, current)) {
            return StateError::unordered_collection;
        }
    }

    for (std::size_t index = 1; index < state.world.size(); ++index) {
        const DefinitionRef& previous = state.world[index - 1U].key;
        const DefinitionRef& current = state.world[index].key;
        if (previous == current) {
            return StateError::duplicate_identity;
        }
        if (!definition_ref_less(previous, current)) {
            return StateError::unordered_collection;
        }
    }

    for (std::size_t index = 0; index < state.applied_outcomes.size(); ++index) {
        if (state.applied_outcomes[index].value == 0U) {
            return StateError::reserved_identity;
        }
        if (index == 0U) {
            continue;
        }
        if (state.applied_outcomes[index - 1U] ==
            state.applied_outcomes[index]) {
            return StateError::duplicate_identity;
        }
        if (!(state.applied_outcomes[index - 1U] <
              state.applied_outcomes[index])) {
            return StateError::unordered_collection;
        }
    }

    // Last, because it is the only check that reads another collection: a
    // progression step cites an outcome, and the outcomes have just been
    // proved to be in the order `outcome_applied` searches.
    return validate_progress(state);
}

std::uint64_t canonical_hash(const CampaignState& state) noexcept {
    std::uint64_t hash = core::fnv1a64_offset_basis;

    hash = hash_integer(hash, static_cast<std::uint32_t>(state.units.size()));
    for (const PersistentUnit& unit : state.units) {
        hash = hash_integer(hash, unit.id.value);
        hash = hash_definition_ref(hash, unit.definition);
        hash = hash_integer(hash, static_cast<std::uint8_t>(unit.availability));
        hash = hash_integer(hash, unit.progression.level);
        hash = hash_integer(hash, unit.progression.experience);
        for (std::uint16_t gain : unit.progression.gained) {
            hash = hash_integer(hash, gain);
        }
        hash = hash_stacks(hash, unit.carried);
    }

    hash = hash_stacks(hash, state.store);

    hash = hash_integer(
        hash,
        static_cast<std::uint32_t>(state.objectives.size())
    );
    for (const ObjectiveRecord& record : state.objectives) {
        hash = hash_definition_ref(hash, record.objective);
        hash = hash_integer(hash, static_cast<std::uint8_t>(record.result));
    }

    hash = hash_integer(hash, static_cast<std::uint32_t>(state.world.size()));
    for (const WorldFlag& flag : state.world) {
        hash = hash_definition_ref(hash, flag.key);
        hash = hash_integer(hash, static_cast<std::uint8_t>(flag.value.type));
        hash = hash_integer(
            hash,
            static_cast<std::uint64_t>(flag.value.value)
        );
    }

    hash = hash_integer(
        hash,
        static_cast<std::uint32_t>(state.applied_outcomes.size())
    );
    for (const OutcomeId id : state.applied_outcomes) {
        hash = hash_integer(hash, id.value);
    }

    // The route, in the order it was walked. Two campaigns that reached one
    // node through different predecessors fold to different numbers, which is
    // the whole reason a recombined route is worth persisting.
    hash = hash_integer(hash, static_cast<std::uint8_t>(state.progress.active ? 1U : 0U));
    hash = hash_definition_ref(hash, state.progress.campaign);
    hash = hash_definition_ref(hash, state.progress.active_node);
    hash = hash_integer(
        hash,
        static_cast<std::uint32_t>(state.progress.history.size())
    );
    for (const ProgressionEntry& entry : state.progress.history) {
        hash = hash_definition_ref(hash, entry.node);
        hash = hash_integer(hash, entry.cause.value);
    }

    return hash;
}

}  // namespace grandleon::campaign
