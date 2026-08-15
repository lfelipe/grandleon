// SPDX-License-Identifier: MIT
#include <grandleon/campaign/graph.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/state.hpp>

#include <cstdint>
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

campaign::DefinitionRef node(std::string_view key) {
    return reference(core::ContentCategory::campaign_node, key);
}

const campaign::DefinitionRef tarnholt =
    reference(core::ContentCategory::campaign, "tarnholt");
const campaign::DefinitionRef ford = node("ford");
const campaign::DefinitionRef watch = node("watch");
const campaign::DefinitionRef retreat = node("retreat");
const campaign::DefinitionRef muster = node("muster");
const campaign::DefinitionRef ending = node("ending");

const campaign::DefinitionRef hold_the_ford =
    reference(core::ContentCategory::objective, "hold_the_ford");
const campaign::DefinitionRef bridge_burned =
    reference(core::ContentCategory::objective, "bridge_burned");
const campaign::DefinitionRef torch =
    reference(core::ContentCategory::item, "torch");
const campaign::DefinitionRef levy_paid =
    reference(core::ContentCategory::objective, "levy_paid");
const campaign::DefinitionRef ford_open =
    reference(core::ContentCategory::objective, "ford_open");
const campaign::DefinitionRef lancer =
    reference(core::ContentCategory::unit_type, "lancer");
const campaign::DefinitionRef ford_battle =
    reference(core::ContentCategory::encounter, "ford_battle");

constexpr campaign::PersistentEntityId store{};
constexpr campaign::PersistentEntityId first{1};

campaign::OutcomeSource source(std::uint64_t battle_hash, std::uint64_t sequence) {
    return {ford_battle, battle_hash, sequence};
}

campaign::CampaignTransition conditional(
    const campaign::DefinitionRef& target,
    std::uint32_t priority,
    std::vector<campaign::TransitionPredicate> predicates,
    campaign::ConditionCombinator combinator = campaign::ConditionCombinator::all
) {
    campaign::CampaignTransition transition;
    transition.target = target;
    transition.priority = priority;
    transition.combinator = combinator;
    transition.predicates = std::move(predicates);
    return transition;
}

campaign::CampaignGraphNode branching(
    const campaign::DefinitionRef& identity,
    std::vector<campaign::CampaignTransition> transitions,
    const campaign::DefinitionRef* fallback = nullptr
) {
    campaign::CampaignGraphNode result;
    result.node = identity;
    result.transitions = std::move(transitions);
    if (fallback != nullptr) {
        result.has_fallback = true;
        result.fallback = *fallback;
    }
    return result;
}

campaign::CampaignGraphNode terminal(const campaign::DefinitionRef& identity) {
    campaign::CampaignGraphNode result;
    result.node = identity;
    result.terminal = true;
    return result;
}

// The ford branches three ways and both branches recombine at the muster,
// which then ends. Priorities are written out of order on purpose: the graph
// is supposed to not care.
//
//   ford --(1: bridge burned)--> retreat --> muster --> ending
//        --(2: torch in store)--> watch   --> muster
//        --(fallback)-----------> watch
campaign::CampaignGraph tarnholt_graph() {
    const campaign::DefinitionRef watch_target = watch;
    return campaign::make_campaign_graph(
        tarnholt,
        ford,
        {
            branching(
                ford,
                {
                    conditional(
                        watch,
                        2,
                        {campaign::inventory_at_least(store, torch, 1U)}
                    ),
                    conditional(
                        retreat,
                        1,
                        {campaign::objective_result_is(
                            bridge_burned, campaign::ObjectiveOutcome::satisfied
                        )}
                    ),
                },
                &watch_target
            ),
            branching(retreat, {}, &muster),
            branching(watch, {}, &muster),
            branching(muster, {}, &ending),
            terminal(ending),
        }
    );
}

campaign::CampaignOutcomeBatch batch(
    std::uint64_t battle_hash,
    std::uint64_t sequence,
    std::vector<campaign::CampaignOutcomeOperation> operations
) {
    return campaign::make_outcome_batch(source(battle_hash, sequence), std::move(operations));
}

campaign::CampaignState started() {
    campaign::CampaignState state;
    expect(
        campaign::begin_campaign(state, tarnholt_graph()) ==
            campaign::ProgressionError::none,
        "a campaign enters its graph at the entry node"
    );
    return state;
}

// A node whose conditional edges both match takes the one with the lower
// priority number, and it does so whatever order they were authored in.
void the_lowest_priority_matching_edge_wins() {
    const campaign::CampaignGraph graph = tarnholt_graph();
    expect(
        campaign::validate_graph(graph) == campaign::GraphError::none,
        "the graph is a graph"
    );

    const campaign::CampaignGraphNode* const start =
        campaign::find_graph_node(graph, ford);
    expect(start != nullptr, "the entry node is findable by identity");
    expect(
        start != nullptr && start->transitions.size() == 2U &&
            start->transitions[0].priority == 1U &&
            start->transitions[1].priority == 2U,
        "and its edges are in ascending priority, not authoring order"
    );

    campaign::CampaignState state = started();
    // Both conditions hold: the bridge burned *and* a torch is in the store.
    const auto both = batch(
        0x1111ULL,
        0U,
        {
            campaign::record_objective(
                bridge_burned, campaign::ObjectiveOutcome::satisfied
            ),
            campaign::add_item(store, torch, 1U),
        }
    );
    const campaign::NodeCompletion completion =
        campaign::complete_node(state, graph, both);
    expect(static_cast<bool>(completion), "the completion is accepted");
    expect(completion.advanced, "and the campaign advanced");
    expect(!completion.used_fallback, "not by the fallback");
    expect(completion.priority == 1U, "but by the lowest matching priority");
    expect(completion.target == retreat, "which is the retreat");
    expect(
        state.progress.active_node == retreat,
        "and that is where the campaign now stands"
    );
    expect(
        state.progress.history.size() == 2U &&
            state.progress.history[0].node == ford &&
            state.progress.history[0].cause.value == 0U &&
            state.progress.history[1].node == retreat &&
            state.progress.history[1].cause == both.id,
        "with a route that names the entry and the batch that left it"
    );

    // The same graph told in the opposite authoring order decides the same
    // way, because the priorities decide and the array does not.
    const campaign::DefinitionRef watch_target = watch;
    const campaign::CampaignGraph reversed = campaign::make_campaign_graph(
        tarnholt,
        ford,
        {
            terminal(ending),
            branching(muster, {}, &ending),
            branching(watch, {}, &muster),
            branching(retreat, {}, &muster),
            branching(
                ford,
                {
                    conditional(
                        retreat,
                        1,
                        {campaign::objective_result_is(
                            bridge_burned, campaign::ObjectiveOutcome::satisfied
                        )}
                    ),
                    conditional(
                        watch,
                        2,
                        {campaign::inventory_at_least(store, torch, 1U)}
                    ),
                },
                &watch_target
            ),
        }
    );
    campaign::CampaignState mirrored = started();
    const campaign::NodeCompletion mirror =
        campaign::complete_node(mirrored, reversed, both);
    expect(
        mirror.advanced && mirror.target == retreat && mirror.priority == 1U,
        "an identically authored graph in the opposite order chooses identically"
    );
    expect(
        campaign::canonical_hash(state) == campaign::canonical_hash(mirrored),
        "and leaves the same campaign"
    );
}

// The unconditional edge is eligible only when nothing conditional matched.
void the_fallback_is_the_last_resort() {
    const campaign::CampaignGraph graph = tarnholt_graph();
    campaign::CampaignState state = started();
    // Nothing recorded, nothing carried: neither condition can hold.
    const auto quiet = batch(
        0x2222ULL,
        0U,
        {campaign::record_objective(
            hold_the_ford, campaign::ObjectiveOutcome::satisfied
        )}
    );
    const campaign::NodeCompletion completion =
        campaign::complete_node(state, graph, quiet);
    expect(completion.advanced, "the campaign still advanced");
    expect(completion.used_fallback, "by the unconditional edge");
    expect(completion.target == watch, "to the watch");

    // The same node, with one condition satisfied, does not reach for it.
    campaign::CampaignState armed = started();
    const auto carried = batch(
        0x3333ULL, 0U, {campaign::add_item(store, torch, 2U)}
    );
    const campaign::NodeCompletion chosen =
        campaign::complete_node(armed, graph, carried);
    expect(
        chosen.advanced && !chosen.used_fallback && chosen.priority == 2U &&
            chosen.target == watch,
        "a matching conditional edge is taken instead of the fallback"
    );
    // Both routes ended at the same node and must still be told apart.
    expect(
        campaign::canonical_hash(state) != campaign::canonical_hash(armed),
        "and the two campaigns that reached it are not the same campaign"
    );
}

// Two histories reach one node through different valid predecessors and
// continue from it, keeping everything they committed on the way.
void branches_recombine_without_losing_what_each_carried() {
    const campaign::CampaignGraph graph = tarnholt_graph();

    campaign::CampaignState burned = started();
    const auto burn = batch(
        0x4444ULL,
        0U,
        {
            campaign::record_objective(
                bridge_burned, campaign::ObjectiveOutcome::satisfied
            ),
            campaign::set_world_flag(
                ford_open, {campaign::WorldValueType::boolean, 0}
            ),
        }
    );
    expect(
        campaign::complete_node(burned, graph, burn).target == retreat,
        "one campaign goes by the retreat"
    );

    campaign::CampaignState held = started();
    const auto hold = batch(
        0x5555ULL,
        0U,
        {
            campaign::add_item(store, torch, 1U),
            campaign::record_objective(
                levy_paid, campaign::ObjectiveOutcome::failed
            ),
        }
    );
    expect(
        campaign::complete_node(held, graph, hold).target == watch,
        "and another by the watch"
    );

    // Both predecessors name the muster.
    const auto retreat_done = batch(0x4445ULL, 1U, {});
    const auto watch_done = batch(0x5556ULL, 1U, {});
    expect(
        campaign::complete_node(burned, graph, retreat_done).target == muster,
        "the retreat leads to the muster"
    );
    expect(
        campaign::complete_node(held, graph, watch_done).target == muster,
        "and so does the watch"
    );
    expect(
        burned.progress.active_node == muster &&
            held.progress.active_node == muster,
        "both campaigns now stand at the shared node"
    );
    expect(
        burned.progress.history.size() == 3U &&
            burned.progress.history[1].node == retreat &&
            held.progress.history[1].node == watch,
        "and each remembers the predecessor it came through"
    );
    expect(
        campaign::find_objective(burned, bridge_burned) != nullptr &&
            campaign::find_objective(burned, levy_paid) == nullptr &&
            campaign::item_quantity(burned, store, torch) == 0U,
        "one keeps the objectives and inventory it committed"
    );
    expect(
        campaign::find_objective(held, levy_paid) != nullptr &&
            campaign::find_objective(held, bridge_burned) == nullptr &&
            campaign::item_quantity(held, store, torch) == 1U,
        "and so does the other"
    );
    expect(
        campaign::canonical_hash(burned) != campaign::canonical_hash(held),
        "so recombining did not make them one campaign"
    );

    // And both continue from the shared node, the same way.
    const auto muster_done_left = batch(0x4446ULL, 2U, {});
    const auto muster_done_right = batch(0x5557ULL, 2U, {});
    expect(
        campaign::complete_node(burned, graph, muster_done_left).target ==
                ending &&
            campaign::complete_node(held, graph, muster_done_right).target ==
                ending,
        "both continue from it to the same ending"
    );
}

// A transition that returns to an earlier node is legal, walks once for one
// completion, and needs a new committed outcome to walk again.
void a_cycle_advances_once_per_completion() {
    // patrol --(watch still open)--> patrol, otherwise --> ending
    const campaign::DefinitionRef patrol = node("patrol");
    const campaign::DefinitionRef patrol_target = patrol;
    const campaign::CampaignGraph graph = campaign::make_campaign_graph(
        tarnholt,
        patrol,
        {
            branching(
                patrol,
                {conditional(
                    ending,
                    1,
                    {campaign::world_flag_equals(
                        ford_open, {campaign::WorldValueType::boolean, 1}
                    )}
                )},
                &patrol_target
            ),
            terminal(ending),
        }
    );
    expect(
        campaign::validate_graph(graph) == campaign::GraphError::none,
        "a graph with a cycle is a graph"
    );

    campaign::CampaignState state;
    expect(
        campaign::begin_campaign(state, graph) ==
            campaign::ProgressionError::none,
        "the patrol begins"
    );
    const auto lap = batch(0x6666ULL, 0U, {campaign::add_item(store, torch, 1U)});
    const campaign::NodeCompletion first_lap =
        campaign::complete_node(state, graph, lap);
    expect(
        first_lap.advanced && first_lap.target == patrol &&
            first_lap.used_fallback,
        "one completion walks the edge back to the same node"
    );
    const std::uint64_t after_one = campaign::canonical_hash(state);

    // The identical completion, retried. The batch derives the same id, so it
    // is the same completion event and it does not walk the cycle again.
    const campaign::NodeCompletion retried =
        campaign::complete_node(state, graph, lap);
    expect(static_cast<bool>(retried), "the retry is not an error");
    expect(retried.already_advanced, "it is recognised as already advanced");
    expect(!retried.advanced, "and it advances nothing");
    expect(
        campaign::canonical_hash(state) == after_one,
        "leaving the campaign exactly as one lap left it"
    );
    expect(
        campaign::item_quantity(state, store, torch) == 1U,
        "with the torch counted once"
    );

    // A new committed outcome walks it again, and the history is two laps long.
    const auto second = batch(0x7777ULL, 1U, {campaign::add_item(store, torch, 1U)});
    expect(
        campaign::complete_node(state, graph, second).advanced,
        "a new completion walks the cycle again"
    );
    expect(
        state.progress.history.size() == 3U &&
            state.progress.history[1].node == patrol &&
            state.progress.history[2].node == patrol,
        "and the route records both laps"
    );

    // Opening the ford leaves the cycle.
    const auto leave = batch(
        0x8888ULL,
        2U,
        {campaign::set_world_flag(
            ford_open, {campaign::WorldValueType::boolean, 1}
        )}
    );
    const campaign::NodeCompletion out =
        campaign::complete_node(state, graph, leave);
    expect(
        out.advanced && !out.used_fallback && out.target == ending,
        "and the conditional edge out is taken once its predicate holds"
    );
}

// A node whose conditions all decline and which has no fallback keeps the
// outcome it committed and does not move.
void blocked_progression_keeps_the_outcome_and_the_node() {
    const campaign::DefinitionRef gate = node("gate");
    const campaign::CampaignGraph graph = campaign::make_campaign_graph(
        tarnholt,
        gate,
        {
            branching(
                gate,
                {conditional(
                    ending,
                    1,
                    {campaign::objective_result_is(
                        hold_the_ford, campaign::ObjectiveOutcome::satisfied
                    )}
                )}
            ),
            terminal(ending),
        }
    );

    campaign::CampaignState state;
    expect(
        campaign::begin_campaign(state, graph) ==
            campaign::ProgressionError::none,
        "the campaign begins at the gate"
    );
    const auto failed = batch(
        0x9999ULL,
        0U,
        {
            campaign::record_objective(
                hold_the_ford, campaign::ObjectiveOutcome::failed
            ),
            campaign::recruit_unit(first, lancer),
        }
    );
    const campaign::NodeCompletion completion =
        campaign::complete_node(state, graph, failed);
    expect(
        completion.error == campaign::ProgressionError::blocked,
        "progression is blocked"
    );
    expect(!completion.advanced, "nothing advanced");
    expect(
        state.progress.active_node == gate,
        "the campaign stands where it stood"
    );
    expect(
        state.progress.history.size() == 1U,
        "and its route gained no step"
    );
    // The battle was fought, so its consequences are facts.
    expect(
        campaign::outcome_applied(state, failed.id),
        "the outcome batch is still committed"
    );
    expect(
        campaign::find_unit(state, first) != nullptr,
        "and the recruit it brought is on the roster"
    );

    // A later completion whose predicate holds moves it, once.
    const auto held = batch(
        0xaaaaULL,
        1U,
        {campaign::record_objective(
            hold_the_ford, campaign::ObjectiveOutcome::satisfied
        )}
    );
    expect(
        campaign::complete_node(state, graph, held).target == ending,
        "and the gate opens when the objective is satisfied"
    );
    // The end is the end.
    const auto after = batch(0xbbbbULL, 2U, {});
    expect(
        campaign::complete_node(state, graph, after).error ==
            campaign::ProgressionError::node_is_terminal,
        "a terminal node cannot be completed"
    );
    expect(
        !campaign::outcome_applied(state, after.id),
        "and refusing it committed nothing"
    );
}

// Evaluation reads campaign state, and only the three questions the source
// schema lets an author ask.
void predicates_read_the_committed_campaign() {
    campaign::CampaignState state;
    const auto opening = campaign::make_outcome_batch(
        source(0xc0c0ULL, 0U),
        {
            campaign::recruit_unit(first, lancer),
            campaign::add_item(first, torch, 2U),
            campaign::add_item(store, torch, 5U),
            campaign::record_objective(
                hold_the_ford, campaign::ObjectiveOutcome::failed
            ),
            campaign::set_world_flag(
                ford_open, {campaign::WorldValueType::integer, 7}
            ),
        }
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, opening)),
        "the campaign is told some things"
    );

    expect(
        campaign::predicate_holds(
            state,
            campaign::objective_result_is(
                hold_the_ford, campaign::ObjectiveOutcome::failed
            )
        ),
        "an objective that failed reads as failed"
    );
    expect(
        !campaign::predicate_holds(
            state,
            campaign::objective_result_is(
                hold_the_ford, campaign::ObjectiveOutcome::satisfied
            )
        ),
        "and not as satisfied"
    );
    expect(
        !campaign::predicate_holds(
            state,
            campaign::objective_result_is(
                bridge_burned, campaign::ObjectiveOutcome::failed
            )
        ),
        "an objective the campaign never recorded has reached no result"
    );
    expect(
        campaign::predicate_holds(
            state, campaign::inventory_at_least(store, torch, 5U)
        ) &&
            !campaign::predicate_holds(
                state, campaign::inventory_at_least(store, torch, 6U)
            ),
        "the store's count is the store's count"
    );
    expect(
        campaign::predicate_holds(
            state, campaign::inventory_at_least(first, torch, 2U)
        ) &&
            !campaign::predicate_holds(
                state, campaign::inventory_at_least(first, torch, 3U)
            ),
        "and a member's is their own"
    );
    expect(
        campaign::predicate_holds(
            state,
            campaign::world_flag_equals(
                ford_open, {campaign::WorldValueType::integer, 7}
            )
        ),
        "a typed world value compares by type and value"
    );
    expect(
        !campaign::predicate_holds(
            state,
            campaign::world_flag_equals(
                ford_open, {campaign::WorldValueType::boolean, 1}
            )
        ),
        "and an integer seven is not a boolean one"
    );

    // Combinators, over a snapshot that satisfies exactly one of two.
    const campaign::TransitionPredicate yes =
        campaign::inventory_at_least(store, torch, 5U);
    const campaign::TransitionPredicate no =
        campaign::inventory_at_least(store, torch, 6U);
    expect(
        !campaign::transition_matches(
            state, conditional(ending, 1, {yes, no})
        ),
        "all requires every predicate"
    );
    expect(
        campaign::transition_matches(
            state,
            conditional(
                ending, 1, {yes, no}, campaign::ConditionCombinator::any
            )
        ),
        "any requires one"
    );
    expect(
        campaign::transition_matches(
            state,
            conditional(ending, 1, {no}, campaign::ConditionCombinator::none)
        ),
        "and none negates the one it is given"
    );
}

// Every way a graph can fail to be a graph, and the answer each gets.
void a_graph_that_is_not_a_graph_is_refused() {
    const campaign::DefinitionRef nowhere = node("nowhere");
    const auto refused = [](campaign::CampaignGraph graph) {
        return campaign::validate_graph(graph);
    };

    expect(
        refused(campaign::make_campaign_graph(
            tarnholt, ford, {branching(ford, {}, &muster), terminal(muster)}
        )) == campaign::GraphError::none,
        "the simplest usable graph passes"
    );
    expect(
        refused(campaign::make_campaign_graph(
            campaign::DefinitionRef{}, ford, {terminal(ford)}
        )) == campaign::GraphError::unidentified_node,
        "an unidentified campaign is refused"
    );
    expect(
        refused(campaign::make_campaign_graph(
            tarnholt, nowhere, {terminal(ford)}
        )) == campaign::GraphError::missing_entry,
        "an entry naming no node is refused"
    );
    expect(
        refused(campaign::make_campaign_graph(
            tarnholt, ford, {terminal(ford), terminal(ford)}
        )) == campaign::GraphError::duplicate_node,
        "two nodes of one identity are refused"
    );
    expect(
        refused(campaign::make_campaign_graph(
            tarnholt, ford, {branching(ford, {}, &nowhere), terminal(muster)}
        )) == campaign::GraphError::missing_target,
        "a fallback naming no node is refused"
    );
    expect(
        refused(campaign::make_campaign_graph(
            tarnholt, ford, {branching(ford, {})}
        )) == campaign::GraphError::dead_end,
        "a non-terminal node with no edge is refused"
    );
    {
        campaign::CampaignGraphNode dead = terminal(ford);
        dead.has_fallback = true;
        dead.fallback = muster;
        expect(
            refused(campaign::make_campaign_graph(
                tarnholt, ford, {dead, terminal(muster)}
            )) == campaign::GraphError::terminal_has_edges,
            "a terminal node with an edge is refused"
        );
    }
    expect(
        refused(campaign::make_campaign_graph(
            tarnholt,
            ford,
            {
                branching(
                    ford,
                    {
                        conditional(
                            muster,
                            3,
                            {campaign::inventory_at_least(store, torch, 1U)}
                        ),
                        conditional(
                            ending,
                            3,
                            {campaign::inventory_at_least(store, torch, 2U)}
                        ),
                    }
                ),
                terminal(muster),
                terminal(ending),
            }
        )) == campaign::GraphError::duplicate_priority,
        "two edges of one priority are refused: array order must not decide"
    );
    expect(
        refused(campaign::make_campaign_graph(
            tarnholt,
            ford,
            {branching(ford, {conditional(muster, 1, {})}), terminal(muster)}
        )) == campaign::GraphError::invalid_condition,
        "a conditional edge with no predicate is refused"
    );
    expect(
        refused(campaign::make_campaign_graph(
            tarnholt,
            ford,
            {
                branching(
                    ford,
                    {conditional(
                        muster,
                        1,
                        {campaign::inventory_at_least(store, torch, 1U),
                         campaign::inventory_at_least(store, torch, 2U)},
                        campaign::ConditionCombinator::none
                    )},
                    nullptr
                ),
                terminal(muster),
            }
        )) == campaign::GraphError::invalid_condition,
        "a negation over two predicates is ambiguous and refused"
    );
    {
        campaign::TransitionPredicate broken =
            campaign::objective_result_is(
                hold_the_ford, campaign::ObjectiveOutcome::satisfied
            );
        broken.selector = 9;
        expect(
            refused(campaign::make_campaign_graph(
                tarnholt,
                ford,
                {branching(ford, {conditional(muster, 1, {broken})}),
                 terminal(muster)}
            )) == campaign::GraphError::invalid_predicate,
            "a selector outside its enumeration is refused"
        );
    }
    expect(
        refused(campaign::make_campaign_graph(
            tarnholt,
            ford,
            {branching(
                 ford,
                 {conditional(
                     muster, 1, {campaign::inventory_at_least(store, torch, 0U)}
                 )}
             ),
             terminal(muster)}
        )) == campaign::GraphError::invalid_predicate,
        "asking for none of an item is refused: it is unconditional in disguise"
    );

    // And an invalid graph commits nothing, ever.
    campaign::CampaignState state;
    const campaign::CampaignGraph broken =
        campaign::make_campaign_graph(tarnholt, nowhere, {terminal(ford)});
    expect(
        campaign::begin_campaign(state, broken) ==
            campaign::ProgressionError::invalid_graph,
        "a campaign cannot begin in a graph that is not one"
    );
    expect(!state.progress.active, "and nothing was recorded");
}

// The refusals that protect a position: no graph entered, the wrong graph, a
// node the content no longer holds, and a batch the campaign will not take.
void a_position_is_only_moved_by_its_own_graph() {
    const campaign::CampaignGraph graph = tarnholt_graph();
    const auto anything = batch(0xd00dULL, 0U, {});

    campaign::CampaignState fresh;
    expect(
        campaign::complete_node(fresh, graph, anything).error ==
            campaign::ProgressionError::not_started,
        "a campaign that never entered a graph completes no node"
    );
    expect(
        !campaign::outcome_applied(fresh, anything.id),
        "and the refusal committed nothing"
    );

    campaign::CampaignState state = started();
    expect(
        campaign::begin_campaign(state, graph) ==
            campaign::ProgressionError::already_started,
        "a campaign enters its graph once"
    );

    const campaign::CampaignGraph other = campaign::make_campaign_graph(
        reference(core::ContentCategory::campaign, "elsewhere"),
        ford,
        {branching(ford, {}, &muster), terminal(muster)}
    );
    expect(
        campaign::complete_node(state, other, anything).error ==
            campaign::ProgressionError::wrong_campaign,
        "another campaign's graph does not move this position"
    );

    const campaign::CampaignGraph pruned = campaign::make_campaign_graph(
        tarnholt, muster, {branching(muster, {}, &ending), terminal(ending)}
    );
    expect(
        campaign::complete_node(state, pruned, anything).error ==
            campaign::ProgressionError::unknown_active_node,
        "and content that dropped the node under the player says so"
    );

    campaign::CampaignOutcomeBatch unidentified;
    unidentified.operations = anything.operations;
    const campaign::NodeCompletion rejected =
        campaign::complete_node(state, graph, unidentified);
    expect(
        rejected.error == campaign::ProgressionError::outcome_rejected &&
            rejected.outcome.error ==
                campaign::OutcomeError::unidentified_batch,
        "an unidentified batch is refused before anything moves"
    );
    expect(
        state.progress.active_node == ford && state.progress.history.size() == 1U,
        "leaving the position untouched"
    );
}

// A progression the state layer could not have produced is refused whole, so
// bytes off a disk cannot smuggle one in.
void an_impossible_route_is_not_a_route() {
    campaign::CampaignState state = started();
    expect(
        campaign::validate(state) == campaign::StateError::none,
        "a started campaign validates"
    );

    campaign::CampaignState orphaned = state;
    orphaned.progress.active = false;
    expect(
        campaign::validate(orphaned) ==
            campaign::StateError::inconsistent_progression,
        "an inactive progression that remembers a node is refused"
    );

    campaign::CampaignState mismatched = state;
    mismatched.progress.active_node = ending;
    expect(
        campaign::validate(mismatched) ==
            campaign::StateError::inconsistent_progression,
        "an active node the route does not end at is refused"
    );

    campaign::CampaignState invented = state;
    invented.progress.history.push_back({watch, campaign::OutcomeId{0x1234ULL}});
    invented.progress.active_node = watch;
    expect(
        campaign::validate(invented) ==
            campaign::StateError::inconsistent_progression,
        "a step caused by a batch this campaign never committed is refused"
    );

    campaign::CampaignState uncaused = state;
    uncaused.progress.history.push_back({watch, campaign::OutcomeId{}});
    uncaused.progress.active_node = watch;
    expect(
        campaign::validate(uncaused) ==
            campaign::StateError::inconsistent_progression,
        "and so is a step nothing caused after the first"
    );

    // One committed batch moves the campaign once, so one cause appears once.
    const campaign::CampaignGraph graph = tarnholt_graph();
    campaign::CampaignState walked = started();
    const auto once = batch(0xe0e0ULL, 0U, {campaign::add_item(store, torch, 1U)});
    expect(
        campaign::complete_node(walked, graph, once).advanced,
        "a campaign walks an edge"
    );
    campaign::CampaignState doubled = walked;
    doubled.progress.history.push_back({muster, once.id});
    doubled.progress.active_node = muster;
    expect(
        campaign::validate(doubled) ==
            campaign::StateError::inconsistent_progression,
        "and one completion cannot appear twice in the route"
    );
}

}  // namespace

int main() {
    the_lowest_priority_matching_edge_wins();
    the_fallback_is_the_last_resort();
    branches_recombine_without_losing_what_each_carried();
    a_cycle_advances_once_per_completion();
    blocked_progression_keeps_the_outcome_and_the_node();
    predicates_read_the_committed_campaign();
    a_graph_that_is_not_a_graph_is_refused();
    a_position_is_only_moved_by_its_own_graph();
    an_impossible_route_is_not_a_route();
    return failures == 0 ? 0 : 1;
}
