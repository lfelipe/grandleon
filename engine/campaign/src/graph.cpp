// SPDX-License-Identifier: MIT
#include <grandleon/campaign/graph.hpp>

#include <algorithm>
#include <utility>

namespace grandleon::campaign {

namespace {

// Category zero is not a member of `core::ContentCategory`, so it is the one
// spelling of "nobody set this" that no authored identity collides with.
[[nodiscard]] bool is_unset(const DefinitionRef& reference) noexcept {
    return static_cast<std::uint32_t>(reference.category) == 0U;
}

[[nodiscard]] bool known_objective_outcome(std::uint8_t selector) noexcept {
    return selector == static_cast<std::uint8_t>(ObjectiveOutcome::satisfied) ||
           selector == static_cast<std::uint8_t>(ObjectiveOutcome::failed);
}

[[nodiscard]] bool known_world_value_type(std::uint8_t selector) noexcept {
    return selector == static_cast<std::uint8_t>(WorldValueType::boolean) ||
           selector == static_cast<std::uint8_t>(WorldValueType::integer);
}

[[nodiscard]] GraphError validate_predicate(
    const TransitionPredicate& predicate
) noexcept {
    if (is_unset(predicate.subject)) {
        return GraphError::unidentified_node;
    }
    switch (predicate.kind) {
        case TransitionPredicateKind::objective_result:
            if (!known_objective_outcome(predicate.selector) ||
                predicate.amount != 0 || predicate.owner.value != 0U) {
                return GraphError::invalid_predicate;
            }
            return GraphError::none;
        case TransitionPredicateKind::inventory_at_least:
            // Zero of something is a question every campaign answers yes to,
            // which is a transition that is unconditional while looking as if
            // it is not. The authored schema requires at least one for the
            // same reason.
            if (predicate.selector != 0U || predicate.amount < 1 ||
                predicate.amount > 0xffffffffLL) {
                return GraphError::invalid_predicate;
            }
            return GraphError::none;
        case TransitionPredicateKind::world_flag_equals:
            if (!known_world_value_type(predicate.selector) ||
                predicate.owner.value != 0U) {
                return GraphError::invalid_predicate;
            }
            // A boolean world value holds nothing but zero and one, so a
            // predicate that compares one against anything else could never
            // hold and is an authoring mistake rather than a false branch.
            if (predicate.selector ==
                    static_cast<std::uint8_t>(WorldValueType::boolean) &&
                predicate.amount != 0 && predicate.amount != 1) {
                return GraphError::invalid_predicate;
            }
            return GraphError::none;
    }
    return GraphError::invalid_predicate;
}

[[nodiscard]] GraphError validate_transition(
    const CampaignTransition& transition
) noexcept {
    if (is_unset(transition.target)) {
        return GraphError::unidentified_node;
    }
    if (transition.predicates.empty()) {
        return GraphError::invalid_condition;
    }
    if (transition.combinator != ConditionCombinator::all &&
        transition.combinator != ConditionCombinator::any &&
        transition.combinator != ConditionCombinator::none) {
        return GraphError::invalid_condition;
    }
    // `none` is the schema's `not`, which negates exactly one predicate.
    if (transition.combinator == ConditionCombinator::none &&
        transition.predicates.size() != 1U) {
        return GraphError::invalid_condition;
    }
    for (const TransitionPredicate& predicate : transition.predicates) {
        const GraphError error = validate_predicate(predicate);
        if (error != GraphError::none) {
            return error;
        }
    }
    return GraphError::none;
}

}  // namespace

std::string_view transition_predicate_name(
    TransitionPredicateKind kind
) noexcept {
    switch (kind) {
        case TransitionPredicateKind::objective_result:
            return "objective_result";
        case TransitionPredicateKind::inventory_at_least:
            return "inventory_at_least";
        case TransitionPredicateKind::world_flag_equals:
            return "world_flag_equals";
    }
    return "unknown";
}

std::string_view condition_combinator_name(
    ConditionCombinator combinator
) noexcept {
    switch (combinator) {
        case ConditionCombinator::all:
            return "all";
        case ConditionCombinator::any:
            return "any";
        case ConditionCombinator::none:
            return "none";
    }
    return "unknown";
}

std::string_view graph_error_name(GraphError error) noexcept {
    switch (error) {
        case GraphError::none:
            return "none";
        case GraphError::unidentified_node:
            return "unidentified_node";
        case GraphError::duplicate_node:
            return "duplicate_node";
        case GraphError::unordered_nodes:
            return "unordered_nodes";
        case GraphError::missing_entry:
            return "missing_entry";
        case GraphError::missing_target:
            return "missing_target";
        case GraphError::duplicate_priority:
            return "duplicate_priority";
        case GraphError::unordered_transitions:
            return "unordered_transitions";
        case GraphError::invalid_condition:
            return "invalid_condition";
        case GraphError::invalid_predicate:
            return "invalid_predicate";
        case GraphError::terminal_has_edges:
            return "terminal_has_edges";
        case GraphError::dead_end:
            return "dead_end";
    }
    return "unknown";
}

std::string_view progression_error_name(ProgressionError error) noexcept {
    switch (error) {
        case ProgressionError::none:
            return "none";
        case ProgressionError::invalid_graph:
            return "invalid_graph";
        case ProgressionError::not_started:
            return "not_started";
        case ProgressionError::already_started:
            return "already_started";
        case ProgressionError::wrong_campaign:
            return "wrong_campaign";
        case ProgressionError::unknown_active_node:
            return "unknown_active_node";
        case ProgressionError::node_is_terminal:
            return "node_is_terminal";
        case ProgressionError::outcome_rejected:
            return "outcome_rejected";
        case ProgressionError::blocked:
            return "blocked";
        case ProgressionError::invalid_candidate:
            return "invalid_candidate";
        case ProgressionError::unknown_target:
            return "unknown_target";
    }
    return "unknown";
}

TransitionPredicate objective_result_is(
    const DefinitionRef& objective,
    ObjectiveOutcome result
) noexcept {
    TransitionPredicate predicate;
    predicate.kind = TransitionPredicateKind::objective_result;
    predicate.selector = static_cast<std::uint8_t>(result);
    predicate.subject = objective;
    return predicate;
}

TransitionPredicate inventory_at_least(
    PersistentEntityId owner,
    const DefinitionRef& item,
    std::uint32_t quantity
) noexcept {
    TransitionPredicate predicate;
    predicate.kind = TransitionPredicateKind::inventory_at_least;
    predicate.owner = owner;
    predicate.subject = item;
    predicate.amount = static_cast<std::int64_t>(quantity);
    return predicate;
}

TransitionPredicate world_flag_equals(
    const DefinitionRef& key,
    const WorldValue& value
) noexcept {
    TransitionPredicate predicate;
    predicate.kind = TransitionPredicateKind::world_flag_equals;
    predicate.selector = static_cast<std::uint8_t>(value.type);
    predicate.subject = key;
    predicate.amount = value.value;
    return predicate;
}

CampaignGraph make_campaign_graph(
    const DefinitionRef& campaign,
    const DefinitionRef& entry,
    std::vector<CampaignGraphNode> nodes
) {
    CampaignGraph graph;
    graph.campaign = campaign;
    graph.entry = entry;
    for (CampaignGraphNode& node : nodes) {
        // Sorting here is what makes "source array order is irrelevant" true
        // rather than asserted. `validate_graph` then refuses a repeated
        // priority, so the order this produces is total and there is never a
        // tie for the sort to have broken arbitrarily.
        std::sort(
            node.transitions.begin(),
            node.transitions.end(),
            [](const CampaignTransition& lhs, const CampaignTransition& rhs) {
                return lhs.priority < rhs.priority;
            }
        );
    }
    std::sort(
        nodes.begin(),
        nodes.end(),
        [](const CampaignGraphNode& lhs, const CampaignGraphNode& rhs) {
            return definition_ref_less(lhs.node, rhs.node);
        }
    );
    graph.nodes = std::move(nodes);
    return graph;
}

const CampaignGraphNode* find_graph_node(
    const CampaignGraph& graph,
    const DefinitionRef& node
) noexcept {
    const auto position = std::lower_bound(
        graph.nodes.begin(),
        graph.nodes.end(),
        node,
        [](const CampaignGraphNode& candidate, const DefinitionRef& value) {
            return definition_ref_less(candidate.node, value);
        }
    );
    if (position == graph.nodes.end() || !(position->node == node)) {
        return nullptr;
    }
    return &*position;
}

GraphError validate_graph(const CampaignGraph& graph) noexcept {
    if (is_unset(graph.campaign) || is_unset(graph.entry)) {
        return GraphError::unidentified_node;
    }
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        const CampaignGraphNode& node = graph.nodes[index];
        if (is_unset(node.node)) {
            return GraphError::unidentified_node;
        }
        if (index > 0U) {
            const DefinitionRef& previous = graph.nodes[index - 1U].node;
            if (previous == node.node) {
                return GraphError::duplicate_node;
            }
            if (!definition_ref_less(previous, node.node)) {
                return GraphError::unordered_nodes;
            }
        }
        if (node.terminal &&
            (!node.transitions.empty() || node.has_fallback)) {
            return GraphError::terminal_has_edges;
        }
        if (!node.terminal && node.transitions.empty() && !node.has_fallback) {
            return GraphError::dead_end;
        }
        if (node.has_fallback && is_unset(node.fallback)) {
            return GraphError::unidentified_node;
        }
        for (std::size_t edge = 0; edge < node.transitions.size(); ++edge) {
            const CampaignTransition& transition = node.transitions[edge];
            const GraphError error = validate_transition(transition);
            if (error != GraphError::none) {
                return error;
            }
            if (edge > 0U) {
                const std::uint32_t previous =
                    node.transitions[edge - 1U].priority;
                if (previous == transition.priority) {
                    return GraphError::duplicate_priority;
                }
                if (previous > transition.priority) {
                    return GraphError::unordered_transitions;
                }
            }
        }
    }
    if (find_graph_node(graph, graph.entry) == nullptr) {
        return GraphError::missing_entry;
    }
    // Targets last, because the lookup they use needs the node list to have
    // already been proved ordered and free of duplicates.
    for (const CampaignGraphNode& node : graph.nodes) {
        if (node.has_fallback &&
            find_graph_node(graph, node.fallback) == nullptr) {
            return GraphError::missing_target;
        }
        for (const CampaignTransition& transition : node.transitions) {
            if (find_graph_node(graph, transition.target) == nullptr) {
                return GraphError::missing_target;
            }
        }
    }
    return GraphError::none;
}

bool predicate_holds(
    const CampaignState& snapshot,
    const TransitionPredicate& predicate
) noexcept {
    switch (predicate.kind) {
        case TransitionPredicateKind::objective_result: {
            const ObjectiveRecord* const record =
                find_objective(snapshot, predicate.subject);
            // An objective the campaign never recorded has not reached a
            // result, and an absent fact is not a satisfied one.
            return record != nullptr &&
                   static_cast<std::uint8_t>(record->result) ==
                       predicate.selector;
        }
        case TransitionPredicateKind::inventory_at_least:
            return static_cast<std::int64_t>(
                       item_quantity(
                           snapshot, predicate.owner, predicate.subject
                       )
                   ) >= predicate.amount;
        case TransitionPredicateKind::world_flag_equals: {
            const WorldValue* const value =
                find_world_value(snapshot, predicate.subject);
            return value != nullptr &&
                   static_cast<std::uint8_t>(value->type) ==
                       predicate.selector &&
                   value->value == predicate.amount;
        }
    }
    return false;
}

bool transition_matches(
    const CampaignState& snapshot,
    const CampaignTransition& transition
) noexcept {
    if (transition.predicates.empty()) {
        return false;
    }
    switch (transition.combinator) {
        case ConditionCombinator::all:
            for (const TransitionPredicate& predicate : transition.predicates) {
                if (!predicate_holds(snapshot, predicate)) {
                    return false;
                }
            }
            return true;
        case ConditionCombinator::any:
            for (const TransitionPredicate& predicate : transition.predicates) {
                if (predicate_holds(snapshot, predicate)) {
                    return true;
                }
            }
            return false;
        case ConditionCombinator::none:
            return !predicate_holds(snapshot, transition.predicates.front());
    }
    return false;
}

TransitionChoice select_transition(
    const CampaignGraphNode& node,
    const CampaignState& snapshot
) noexcept {
    TransitionChoice choice;
    // Ascending unique priority, so the first match is the lowest-priority
    // match and there is nothing after it worth asking.
    for (const CampaignTransition& transition : node.transitions) {
        if (transition_matches(snapshot, transition)) {
            choice.selected = true;
            choice.priority = transition.priority;
            choice.target = transition.target;
            return choice;
        }
    }
    if (node.has_fallback) {
        choice.selected = true;
        choice.fallback = true;
        choice.target = node.fallback;
    }
    return choice;
}

ProgressionError begin_campaign(
    CampaignState& state,
    const CampaignGraph& graph
) {
    const GraphError graph_error = validate_graph(graph);
    if (graph_error != GraphError::none) {
        return ProgressionError::invalid_graph;
    }
    if (state.progress.active) {
        return ProgressionError::already_started;
    }
    CampaignState candidate = state;
    candidate.progress.active = true;
    candidate.progress.campaign = graph.campaign;
    candidate.progress.active_node = graph.entry;
    // The step nothing caused. Every later one names the batch that caused it.
    candidate.progress.history.push_back({graph.entry, OutcomeId{}});
    if (validate(candidate) != StateError::none) {
        return ProgressionError::invalid_candidate;
    }
    state = std::move(candidate);
    return ProgressionError::none;
}

namespace {

// Everything a move through a graph refuses before it commits anything, in the
// order it refuses it, and the node the campaign is standing on when it does
// not.
//
// Shared by `complete_node` and `jump_to_node` rather than written twice,
// because these are not two functions' checks that happen to agree: they are
// one question, "may this campaign move at all", and two answers to it is how
// one of the two comes to accept a state the other refuses. Null is returned
// with `completion.error` already set, so a caller returns what it was handed.
[[nodiscard]] const CampaignGraphNode* node_a_move_starts_from(
    const CampaignState& state,
    const CampaignGraph& graph,
    const CampaignOutcomeBatch& batch,
    NodeCompletion& completion
) {
    completion.graph_error = validate_graph(graph);
    if (completion.graph_error != GraphError::none) {
        completion.error = ProgressionError::invalid_graph;
        return nullptr;
    }
    if (!state.progress.active) {
        completion.error = ProgressionError::not_started;
        return nullptr;
    }
    if (!(state.progress.campaign == graph.campaign)) {
        completion.error = ProgressionError::wrong_campaign;
        return nullptr;
    }
    const CampaignGraphNode* const node =
        find_graph_node(graph, state.progress.active_node);
    if (node == nullptr) {
        completion.error = ProgressionError::unknown_active_node;
        return nullptr;
    }

    // A completion already in the route moved the campaign once and moves it
    // no further. This is what keeps a cycle from being walked twice for one
    // battle: the edge back is only ever taken for a batch that is not yet in
    // the history, and a retry of the same battle derives the same id.
    for (const ProgressionEntry& entry : state.progress.history) {
        if (entry.cause.value != 0U && entry.cause == batch.id) {
            completion.already_advanced = true;
            completion.outcome.already_applied = true;
            return nullptr;
        }
    }
    return node;
}

// The second commit: the active node and the history, together or not at all.
//
// Shared for the reason the preamble above is shared. The atomicity claim this
// module makes is about the candidate being complete and validated before it is
// swapped in, and a second copy of the swap is a second place that claim can
// quietly stop being true.
void stand_on(
    CampaignState& state,
    const DefinitionRef& target,
    OutcomeId cause,
    NodeCompletion& completion
) {
    CampaignState candidate = state;
    candidate.progress.active_node = target;
    candidate.progress.history.push_back({target, cause});
    const StateError state_error = validate(candidate);
    if (state_error != StateError::none) {
        completion.error = ProgressionError::invalid_candidate;
        completion.state_error = state_error;
        return;
    }
    state = std::move(candidate);
    completion.advanced = true;
    completion.target = target;
}

}  // namespace

NodeCompletion complete_node(
    CampaignState& state,
    const CampaignGraph& graph,
    const CampaignOutcomeBatch& batch
) {
    NodeCompletion completion;
    const CampaignGraphNode* const node =
        node_a_move_starts_from(state, graph, batch, completion);
    if (node == nullptr) return completion;

    if (node->terminal) {
        // Refused before the batch is committed, so a caller that mistakes the
        // end of a campaign for the middle of it changes nothing at all.
        completion.error = ProgressionError::node_is_terminal;
        return completion;
    }

    // First commit: the consequences of the battle that was fought. Atomic and
    // idempotent on its own terms; a refusal leaves the campaign untouched.
    completion.outcome = apply_outcome(state, batch);
    if (!completion.outcome) {
        completion.error = ProgressionError::outcome_rejected;
        return completion;
    }

    // The one immutable snapshot. Every predicate below reads this campaign
    // and no other, so no edge can be decided against a state a later edge
    // does not see.
    const CampaignState& snapshot = state;
    const TransitionChoice choice = select_transition(*node, snapshot);
    if (!choice.selected) {
        // The outcome stands. The campaign does not move, because the author
        // named nowhere for it to move to.
        completion.error = ProgressionError::blocked;
        return completion;
    }

    // Second commit: the edge, the active node and the history, together.
    stand_on(state, choice.target, batch.id, completion);
    if (!completion.advanced) return completion;
    completion.used_fallback = choice.fallback;
    completion.priority = choice.priority;
    return completion;
}

NodeCompletion jump_to_node(
    CampaignState& state,
    const CampaignGraph& graph,
    const CampaignOutcomeBatch& batch,
    const DefinitionRef& target
) {
    NodeCompletion completion;
    // The node the campaign is standing on is not read for anything a jump
    // decides, and is asked for all the same: every refusal above it is a
    // refusal a jump has to make too, and this is the call that makes them.
    // Standing on a terminal node is the one refusal a jump does not inherit,
    // which is why it is `complete_node`'s and not this preamble's: a terminal
    // node has nowhere to go *next*, and a jump was told where to go.
    if (node_a_move_starts_from(state, graph, batch, completion) == nullptr) {
        return completion;
    }
    if (find_graph_node(graph, target) == nullptr) {
        // Refused before the batch is committed, so a caller that named a node
        // this content does not have changes nothing at all.
        completion.error = ProgressionError::unknown_target;
        return completion;
    }

    // First commit: whatever the caller is recording alongside the jump, on the
    // same terms `complete_node` commits a battle's consequences. An empty
    // batch is the ordinary case and still commits: it is what makes the jump
    // identifiable, so a jump repeated is recognised as the jump it already
    // made rather than walked a second time.
    completion.outcome = apply_outcome(state, batch);
    if (!completion.outcome) {
        completion.error = ProgressionError::outcome_rejected;
        return completion;
    }

    // Second commit: the node the caller named, and the history, together. No
    // transition was selected, so `used_fallback` and `priority` say nothing
    // and are left as they are: a jump took no edge, and reporting one it did
    // not take would be inventing the route it deliberately did not walk.
    stand_on(state, target, batch.id, completion);
    return completion;
}

}  // namespace grandleon::campaign
