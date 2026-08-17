// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/state.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

// Where a campaign goes next, and why.
//
// The accepted design states the rule in one paragraph, and every decision in
// this header is one sentence of it:
//
//   "Campaign progression uses package-authored directed graphs rather than
//   inferred chapter order. A completed node first commits its identified
//   outcome batch. Transition predicates then read one immutable candidate
//   campaign-state snapshot. Conditional edges are ordered by explicit unique
//   integer priority; source array order is irrelevant. A single unconditional
//   fallback is eligible only if no conditional edge matches. The selected
//   edge, active node, and progression history are committed atomically, with
//   at most one edge traversed per completion event. Multiple predecessors may
//   share a target and cycles are legal without implying automatic repeated
//   traversal."
//
// ## Two commits, in a stated order
//
// `complete_node` commits twice, and that is the design's ordering rather than
// a compromise. The outcome batch commits first and on its own terms: a battle
// that was fought had consequences, and they are facts whether or not the
// author left an edge out of the node. The snapshot the predicates read is the
// campaign *after* that commit: one value, read once, unchanged for the whole
// of the evaluation, so no predicate can see a campaign a later predicate does
// not. The advance then commits as its own atomic step: active node and
// history together, or neither.
//
// A node whose predicates all fail and which has no fallback leaves the
// campaign holding the outcome and standing where it stood. That is
// `ProgressionError::blocked`, and it is a stated answer rather than a
// half-move.
//
// ## Determinism
//
// Evaluation is a pure function of the graph and the snapshot. No clock, no
// draw from the random substrate, and no dependence on the order a container
// happens to iterate: conditional transitions carry explicit unique integer
// priorities, `make_campaign_graph` sorts by them, and `validate_graph`
// refuses a repeated one. The lowest priority that matches wins, and there is
// never a tie to break.
//
// ## What a graph is made of
//
// Nodes are content, so a node is a `DefinitionRef` under
// `core::ContentCategory::campaign_node`, and a progression entry references
// one by that identity. Nothing here holds an index into a node array, a
// pointer, or a battle-local id. `engine/campaign_runtime` builds one of these
// out of a compiled package; this module never learns what a package is.

namespace grandleon::campaign {

// What a transition asks of the campaign. These are the three questions a
// campaign condition can ask in `schemas/source/v1/campaign.schema.json`, and
// no more: the authored vocabulary is the runtime vocabulary, so a predicate a
// creator can write is a predicate this evaluates and a predicate this
// evaluates is one a creator can write. Persisted in no save, but part of the
// rules contract, so append only.
enum class TransitionPredicateKind : std::uint8_t {
    // The campaign recorded `subject` ending the way `selector` names.
    objective_result = 1,
    // `owner` holds at least `amount` of `subject`; owner zero is the store.
    inventory_at_least = 2,
    // The world value `subject` is of type `selector` and equals `amount`.
    world_flag_equals = 3,
};

[[nodiscard]] std::string_view transition_predicate_name(
    TransitionPredicateKind kind
) noexcept;

// One question, as a flat fixed-width record for the reason
// `CampaignOutcomeOperation` is one: the tag says which fields carry meaning,
// the constructors below make the right ones, and there is no standard-library
// layout anywhere near a console.
struct TransitionPredicate final {
    TransitionPredicateKind kind{TransitionPredicateKind::objective_result};
    // `ObjectiveOutcome` or `WorldValueType` depending on `kind`; zero where
    // the kind names no alternative.
    std::uint8_t selector{};
    // The inventory owner, for `inventory_at_least` only. Zero is the shared
    // store, which is also what every other kind carries.
    PersistentEntityId owner{};
    // The objective, item, or world key being asked about.
    DefinitionRef subject{};
    // A quantity or a world value; zero where the kind names no number.
    std::int64_t amount{};
};

[[nodiscard]] TransitionPredicate objective_result_is(
    const DefinitionRef& objective,
    ObjectiveOutcome result
) noexcept;

[[nodiscard]] TransitionPredicate inventory_at_least(
    PersistentEntityId owner,
    const DefinitionRef& item,
    std::uint32_t quantity
) noexcept;

[[nodiscard]] TransitionPredicate world_flag_equals(
    const DefinitionRef& key,
    const WorldValue& value
) noexcept;

// How a transition's predicates combine. Matches the source schema's `all`,
// `any` and `not`, which is why `none` negates exactly one predicate rather
// than a list: anything else would be ambiguous about what it negates.
enum class ConditionCombinator : std::uint8_t {
    all = 1,
    any = 2,
    none = 3,
};

[[nodiscard]] std::string_view condition_combinator_name(
    ConditionCombinator combinator
) noexcept;

// One conditional edge out of a node.
//
// `priority` is explicit and unique within its node, which is what makes the
// selection independent of the order an author happened to write the array in.
// Lower is considered first, matching the authored field.
struct CampaignTransition final {
    DefinitionRef target{};
    std::uint32_t priority{};
    ConditionCombinator combinator{ConditionCombinator::all};
    // Never empty: a transition with no predicates is an unconditional edge,
    // and a node has at most one of those and spells it `fallback`.
    std::vector<TransitionPredicate> predicates;
};

// One node, and every way out of it.
struct CampaignGraphNode final {
    // `core::ContentCategory::campaign_node`.
    DefinitionRef node{};
    // A node the campaign ends at. It has no edges of any kind, and completing
    // it is refused rather than ignored.
    bool terminal{false};
    // Ascending unique `priority`.
    std::vector<CampaignTransition> transitions;
    // The single unconditional edge, eligible only when no conditional
    // transition matches. A non-terminal node needs at least one edge of one
    // kind or it is a dead end, which `validate_graph` refuses.
    bool has_fallback{false};
    DefinitionRef fallback{};
};

// One authored campaign flow, as the persistent layer sees it.
//
// Cycles are legal and recombination is ordinary: several nodes may name one
// target, and a target may be a node already visited. Neither implies repeated
// traversal, because an edge is only ever walked for an identified completion
// event that has not already been recorded.
struct CampaignGraph final {
    // `core::ContentCategory::campaign`.
    DefinitionRef campaign{};
    DefinitionRef entry{};
    // Ascending `definition_ref_less` over `node`.
    std::vector<CampaignGraphNode> nodes;
};

// Put a graph into its canonical order: nodes ascending, and each node's
// transitions ascending by priority. Source array order is irrelevant, and
// this is where that stops being a claim.
[[nodiscard]] CampaignGraph make_campaign_graph(
    const DefinitionRef& campaign,
    const DefinitionRef& entry,
    std::vector<CampaignGraphNode> nodes
);

[[nodiscard]] const CampaignGraphNode* find_graph_node(
    const CampaignGraph& graph,
    const DefinitionRef& node
) noexcept;

// Why a graph is not a graph. Append only.
enum class GraphError : std::uint8_t {
    none = 0,
    // A campaign, entry, node, or target reference with no content category.
    unidentified_node,
    // Two nodes share one identity.
    duplicate_node,
    // The node list is out of ascending order.
    unordered_nodes,
    // The entry names a node the graph does not hold.
    missing_entry,
    // An edge names a node the graph does not hold.
    missing_target,
    // Two conditional transitions out of one node share a priority, which
    // would leave the taken edge to be decided by array order.
    duplicate_priority,
    // The transitions of one node are not in ascending priority order.
    unordered_transitions,
    // A conditional transition with no predicates, or a `none` combinator over
    // anything but exactly one.
    invalid_condition,
    // A predicate's selector is not a member of the enumeration its kind
    // names, or its amount is outside what the kind allows.
    invalid_predicate,
    // A terminal node with an edge out of it.
    terminal_has_edges,
    // A non-terminal node with no edge of either kind. Progression from it
    // could never be anything but blocked, so it is refused at the door.
    dead_end,
};

[[nodiscard]] std::string_view graph_error_name(GraphError error) noexcept;

// The complete-graph check, stated once here rather than re-derived by each
// caller, exactly as `validate` states the complete-state check.
[[nodiscard]] GraphError validate_graph(const CampaignGraph& graph) noexcept;

// Read one question of one campaign. Pure, and total: a predicate about an
// objective the campaign never recorded, or a world value it never set, does
// not hold. An absent fact is not a satisfied one.
[[nodiscard]] bool predicate_holds(
    const CampaignState& snapshot,
    const TransitionPredicate& predicate
) noexcept;

[[nodiscard]] bool transition_matches(
    const CampaignState& snapshot,
    const CampaignTransition& transition
) noexcept;

// Which edge a node takes out of one snapshot.
struct TransitionChoice final {
    bool selected{false};
    // The conditional edges all declined and the node's unconditional edge was
    // taken. Only ever true when `selected` is.
    bool fallback{false};
    // Meaningless when `fallback` is set: the fallback has no priority,
    // because there is only ever one of it.
    std::uint32_t priority{};
    DefinitionRef target{};
};

// The selection rule, as a pure function of a node and a snapshot: the
// matching conditional transition of lowest priority, or the unconditional
// fallback if no conditional transition matches, or nothing.
[[nodiscard]] TransitionChoice select_transition(
    const CampaignGraphNode& node,
    const CampaignState& snapshot
) noexcept;

// Why a campaign did not move. Append only.
enum class ProgressionError : std::uint8_t {
    none = 0,
    // The graph failed `validate_graph`; `NodeCompletion::graph_error` says
    // how. Checked first, so nothing is committed against a graph whose
    // meaning is not decidable.
    invalid_graph,
    // The campaign has not entered this graph. `begin_campaign` does that.
    not_started,
    // The campaign is already standing in a graph.
    already_started,
    // The campaign's position belongs to a different campaign than this graph.
    wrong_campaign,
    // The active node is not a node of this graph: a save from content that
    // has since dropped the node the player was standing on.
    unknown_active_node,
    // A terminal node cannot be completed: there is nothing after it, and
    // pretending otherwise would be a silent no-op.
    node_is_terminal,
    // The outcome batch was refused; `NodeCompletion::outcome` says why, and
    // nothing at all was committed.
    outcome_rejected,
    // No conditional transition matched and the node has no fallback. The
    // outcome is committed and the campaign stands where it stood.
    blocked,
    // The advance would have left a campaign no sequence of legal steps could
    // reach. `NodeCompletion::state_error` says which invariant.
    invalid_candidate,
    // A jump named a node this graph does not hold. Only `jump_to_node`
    // produces it: `complete_node` never names a target, it selects one, and
    // `validate_graph` has already refused a graph whose edges point nowhere.
    unknown_target,
};

[[nodiscard]] std::string_view progression_error_name(
    ProgressionError error
) noexcept;

// What happened when a node was completed.
struct NodeCompletion final {
    ProgressionError error{ProgressionError::none};
    // Set when `error` is `invalid_graph`.
    GraphError graph_error{GraphError::none};
    // How the outcome batch was received, including whether it was a retry.
    OutcomeApplication outcome{};
    // Set when `error` is `invalid_candidate`.
    StateError state_error{StateError::none};
    // The campaign moved, and `target` is where to.
    bool advanced{false};
    // This completion event was already recorded and nothing was done. Not an
    // error: it is the correct answer to a retry, and it is what stops a cycle
    // from being walked twice for one battle.
    bool already_advanced{false};
    bool used_fallback{false};
    std::uint32_t priority{};
    DefinitionRef target{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ProgressionError::none;
    }
};

// Enter a graph at its entry node, recording the step that has no cause.
[[nodiscard]] ProgressionError begin_campaign(
    CampaignState& state,
    const CampaignGraph& graph
);

// Commit a completed node's consequences and then move, or explain why not.
//
// The order is the design's: the batch commits, the resulting campaign is the
// one immutable snapshot every predicate reads, and the chosen edge, the
// active node and the progression history commit together. At most one edge is
// traversed, and a completion already in the history traverses none.
[[nodiscard]] NodeCompletion complete_node(
    CampaignState& state,
    const CampaignGraph& graph,
    const CampaignOutcomeBatch& batch
);

// Stand the campaign on `target` without walking an edge to it.
//
// **What this is for.** Checking a game on a console means reaching the fifth
// battle to look at one thing in it, and reaching it the ordinary way means
// playing the four before it, every time. This is the move that skips them. It
// is a mechanism and not a permission: whether a game offers it is a setting
// the project declares (`invulnerableForTesting`'s neighbour), and no rule in
// this module reads that setting. What lives here is only the guarantee that a
// campaign moved this way is still a campaign every other function in this
// header will accept.
//
// **It is the second half of `complete_node` with the choosing taken out.**
// The same preamble refuses the same things, the same batch commits first on
// its own terms, the same completion already in the route moves the campaign no
// further, and the same atomic second commit writes the active node and the
// history together. What differs is one line: where `complete_node` reads an
// edge out of the graph, this is told. So a jump is recorded exactly as an
// ordinary step is, cites the batch that caused it exactly as an ordinary step
// does, and a save written after one resumes exactly as any other does. There
// is no second kind of history entry and nothing downstream has to learn that
// jumping exists.
//
// **`target` may be any node of the graph**, including the one the campaign is
// already standing on, one it has already left behind, and a terminal one. None
// of those is an edge, so none of them is `validate_graph`'s business: a jump
// to a terminal node is a campaign standing at its end, and a jump to the
// standing node is that stage begun again. A node this graph does not hold is
// `unknown_target` and nothing at all is committed.
//
// **What a jump does not do is the honest half.** It moves the campaign and
// changes nothing else. The batch is the caller's, and a caller with nothing to
// record passes an empty one; no objective is recorded, no world flag is set,
// and nobody is recruited on behalf of the stages that were passed over. A
// campaign standing on a node it jumped to is therefore a campaign that did not
// do what the route to that node would have done, and a transition out of it
// that asks about any of that will not match. That is a limit and not a defect:
// the alternative is inventing facts the author never wrote, which would be
// wrong differently at every branch. Whoever offers the jump says so to the
// player; this function only refuses to pretend.
[[nodiscard]] NodeCompletion jump_to_node(
    CampaignState& state,
    const CampaignGraph& graph,
    const CampaignOutcomeBatch& batch,
    const DefinitionRef& target
);

}  // namespace grandleon::campaign
