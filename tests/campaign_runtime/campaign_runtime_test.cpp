// SPDX-License-Identifier: MIT
#include <grandleon/campaign/graph.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign_runtime/campaign_runtime.hpp>
#include <grandleon/game_content/compiler.hpp>

#include <grandleon/simulation/encounter.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace cr = grandleon::campaign_runtime;
namespace campaign = grandleon::campaign;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Source-key identities of the four placements. Two of them belong to the
// campaign; the other two are the opposing side, which has no roster.
constexpr std::uint64_t kestrel_key = 2000;
constexpr std::uint64_t brand_key = 2001;
constexpr std::uint64_t raider_key = 2002;
constexpr std::uint64_t captain_key = 2003;

constexpr std::uint64_t ford_encounter = 100;
constexpr std::uint64_t watch_encounter = 101;
constexpr std::uint64_t tarnholt_campaign = 110;
constexpr std::uint64_t ford_node = 111;
constexpr std::uint64_t watch_node = 112;
constexpr std::uint64_t retreat_node = 113;
constexpr std::uint64_t ending_node = 114;
constexpr std::uint64_t ford_held = 90;
constexpr std::uint64_t captain_lives = 91;

constexpr campaign::PersistentEntityId kestrel{1};
constexpr campaign::PersistentEntityId brand{2};

// A package with two encounters, a branching campaign and one objective that
// names a placement, so the roster join has something to keep off the board and
// something it must refuse to.
gc::GameSource game_source() {
    gc::GameSource value;
    value.game_id[0] = 0x47U;
    value.title = "Tarnholt slice";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.item_types = {{20, "Consumable"}};
    value.classes = {{30, "Blue class", {6, 4, 1, 2, 3}, {10}}};
    value.weapons = {{40, "Sword", 10, 3, 1, 1}};
    value.items = {{50, "Tonic", 20, 1}};
    // The blue unit grows and the red one is worth defeating. Both chances
    // below are authored, neither is a hundred, and the red unit authors no
    // growth of its own. That is what lets the tests tell "the stream was not
    // drawn from" apart from "the stream was drawn from and said no".
    gc::UnitType blue{60, "Blue unit", 30, 80, {40}, {50}};
    blue.experience_per_level = 50;
    blue.growth.chance = {70, 60, 50, 40, 30, 20};
    gc::UnitType red{61, "Red unit", 30, 81, {40}, {}};
    red.experience_award = 60;
    value.unit_types = {blue, red};
    value.maps = {{70, "Field", 4, 3, std::vector<std::uint64_t>(12, 1)}};
    value.factions = {{80, "Blue"}, {81, "Red"}};
    value.objectives = {
        {ford_held, "Hold the ford", gc::ObjectiveKind::defeat_all_opponents},
        {captain_lives, "The captain lives", gc::ObjectiveKind::protect_target,
         gc::ObjectiveSide::second, captain_key},
    };
    value.encounters = {
        {
            ford_encounter,
            "The ford",
            70,
            {ford_held},
            {
                {1000, kestrel_key, kestrel_key, 60,
                 gc::EncounterSide::first, 0, 0},
                {1001, brand_key, brand_key, 60,
                 gc::EncounterSide::first, 0, 1},
                {1002, raider_key, 0, 61, gc::EncounterSide::second, 3, 0},
            },
        },
        {
            watch_encounter,
            "The watch",
            70,
            {captain_lives},
            {
                {1010, kestrel_key, kestrel_key, 60,
                 gc::EncounterSide::first, 0, 0},
                {1011, brand_key, brand_key, 60,
                 gc::EncounterSide::first, 0, 1},
                {1012, captain_key, 0, 61, gc::EncounterSide::second, 3, 0},
            },
        },
    };
    value.campaigns = {
        {
            tarnholt_campaign,
            "Tarnholt",
            ford_node,
            {
                // The ford branches: the objective satisfied takes the watch,
                // and everything else falls back to the retreat. The branch is
                // written after the fallback and at a priority the array order
                // does not agree with, on purpose.
                {ford_node,
                 gc::CampaignNodeKind::encounter,
                 ford_encounter,
                 {},
                 {retreat_node},
                 {{watch_node,
                   2,
                   gc::ConditionCombinator::all,
                   {{gc::CampaignPredicateKind::objective_result, ford_held,
                     gc::ObjectiveOutcome::satisfied}}}}},
                {watch_node,
                 gc::CampaignNodeKind::encounter,
                 watch_encounter,
                 {},
                 {ending_node},
                 {}},
                {retreat_node,
                 gc::CampaignNodeKind::story,
                 0,
                 {},
                 {ending_node},
                 {}},
                {ending_node, gc::CampaignNodeKind::terminal, 0, {}, {}, {}},
            },
            // The company, authored: two members of one unit type, which is
            // the whole reason a member is not a unit type.
            {
                {kestrel_key, "Kestrel", 60, 0},
                {brand_key, "Brand", 60, 0},
            },
        },
    };
    return value;
}

pf::LoadedPackage compile_and_load() {
    const auto compiled = gc::compile(game_source());
    expect(static_cast<bool>(compiled), "the fixture compiles");
    for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
        std::cerr << "compiler diagnostic: "
                  << gc::diagnostic_name(diagnostic.code) << ' '
                  << diagnostic.path << '\n';
    }
    const auto loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and the package loads");
    return loaded.package;
}

pf::LoadedPackage compile_and_load(const gc::GameSource& authored) {
    const auto compiled = gc::compile(authored);
    expect(static_cast<bool>(compiled), "the authored fixture compiles");
    for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
        std::cerr << "compiler diagnostic: "
                  << gc::diagnostic_name(diagnostic.code) << ' '
                  << diagnostic.path << '\n';
    }
    const auto loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and its package loads");
    return loaded.package;
}

// The fixture with a capacity written onto one of its boards, as a
// capacity-only deployment: the encounter says how many of the company may
// come and nothing about where they stand, which is the shape that leaves the
// board itself untouched.
pf::LoadedPackage capped_package(
    std::uint64_t encounter_id,
    std::uint16_t capacity
) {
    auto authored = game_source();
    for (gc::Encounter& encounter : authored.encounters) {
        if (encounter.id == encounter_id) {
            encounter.deployment = {120, {}, capacity};
        }
    }
    return compile_and_load(authored);
}

campaign::DefinitionRef ref(
    const pf::LoadedPackage& package,
    core::ContentCategory category,
    std::uint64_t id
) {
    return {package.game_id, category, id};
}

// A campaign with both members on the roster, both deployable, and both
// holding what their unit type says they start with.
//
// The stocking is part of founding rather than part of a test's convenience: a
// campaign member's satchel is their kit, so a roster founded without one is a
// roster of characters who would take the field empty-handed. It is written the
// way a client writes it, with `cr::starting_kit` in the batch that recruits
// them, so that what these tests exercise is the loop the clients run.
campaign::CampaignState full_roster(const pf::LoadedPackage& package) {
    campaign::CampaignState state;
    std::vector<campaign::CampaignOutcomeOperation> founding{
        campaign::recruit_unit(
            kestrel, ref(package, core::ContentCategory::unit_type, 60)
        ),
        campaign::recruit_unit(
            brand, ref(package, core::ContentCategory::unit_type, 60)
        ),
        campaign::set_availability(kestrel, campaign::Availability::available),
        campaign::set_availability(brand, campaign::Availability::available),
    };
    for (const campaign::PersistentEntityId member : {kestrel, brand}) {
        const cr::StartingKit kit = cr::starting_kit(package, member, 60);
        expect(static_cast<bool>(kit), "the unit type says what it starts with");
        founding.insert(
            founding.end(), kit.operations.begin(), kit.operations.end()
        );
    }
    const auto batch = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter), 1U, 0U},
        founding
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, batch)),
        "the roster commits"
    );
    return state;
}

// The battle a definition makes, folded. `EncounterDefinition` has no hash of
// its own, and the number that matters is the one a fight would start from.
std::uint64_t board_hash(const sim::EncounterDefinition& definition) {

    const sim::Encounter::CreateResult created =
        sim::create_encounter(definition);
    expect(
        static_cast<bool>(created),
        "the board is a board a battle could start from"
    );
    return created.encounter.canonical_hash();
}

std::vector<cr::RosterAssignment> assignments() {
    return {{kestrel_key, kestrel}, {brand_key, brand}};
}

// The translation is a translation: an authored flow already carries nodes,
// priorities and a single unconditional target, and the graph is those under
// the identities the persistent layer uses.
void an_authored_flow_becomes_a_graph() {
    const pf::LoadedPackage package = compile_and_load();
    const cr::CampaignGraphLoad loaded =
        cr::load_campaign_graph(package, tarnholt_campaign);
    expect(
        static_cast<bool>(loaded),
        std::string_view{cr::graph_source_error_name(loaded.source.error)}
    );
    const campaign::CampaignGraph& graph = loaded.source.graph;
    expect(
        campaign::validate_graph(graph) == campaign::GraphError::none,
        "the translated graph is a graph"
    );
    expect(
        graph.campaign ==
            ref(package, core::ContentCategory::campaign, tarnholt_campaign),
        "under the campaign's own identity"
    );
    expect(
        graph.entry == cr::campaign_node_ref(package.game_id, ford_node),
        "entering where the flow enters"
    );
    expect(graph.nodes.size() == 4U, "with every authored node");

    const campaign::CampaignGraphNode* const ford = campaign::find_graph_node(
        graph, cr::campaign_node_ref(package.game_id, ford_node)
    );
    expect(ford != nullptr, "and the branching node findable by identity");
    if (ford == nullptr) {
        return;
    }
    expect(
        ford->transitions.size() == 1U && ford->transitions.front().priority == 2U,
        "carrying the authored conditional edge and its priority"
    );
    expect(
        ford->transitions.front().target ==
            cr::campaign_node_ref(package.game_id, watch_node),
        "aimed at the node the author named"
    );
    expect(
        ford->transitions.front().predicates.size() == 1U &&
            ford->transitions.front().predicates.front().kind ==
                campaign::TransitionPredicateKind::objective_result &&
            ford->transitions.front().predicates.front().subject ==
                ref(package, core::ContentCategory::objective, ford_held),
        "reading the objective the author named, under its content identity"
    );
    expect(
        ford->has_fallback &&
            ford->fallback == cr::campaign_node_ref(package.game_id, retreat_node),
        "and the single unconditional target as the fallback"
    );
    const campaign::CampaignGraphNode* const ending = campaign::find_graph_node(
        graph, cr::campaign_node_ref(package.game_id, ending_node)
    );
    expect(
        ending != nullptr && ending->terminal,
        "and the terminal node is terminal"
    );

    // The position the graph produces is the position the encounter loader
    // needs, and the round trip between them is not a caller's arithmetic.
    const pr::CampaignLoadResult flow =
        pr::load_campaign(package, tarnholt_campaign);
    expect(static_cast<bool>(flow), "the flow decodes");
    expect(
        cr::encounter_of_node(flow.definition, graph.entry) == ford_encounter,
        "the entry node names the encounter it is fought at"
    );
    expect(
        cr::encounter_of_node(
            flow.definition, cr::campaign_node_ref(package.game_id, retreat_node)
        ) == 0U,
        "and a story node names none"
    );

    // The graph refuses what the compiled flow can still express: two edges
    // out of one node at one priority, which would leave array order deciding.
    pr::CampaignDefinition ambiguous = flow.definition;
    for (pr::CampaignNode& node : ambiguous.nodes) {
        if (node.id != ford_node) {
            continue;
        }
        pr::CampaignBranch second = node.branches.front();
        second.target_id = retreat_node;
        node.branches.push_back(second);
    }
    const cr::CampaignGraphSource refused = cr::build_campaign_graph(
        package.game_id, tarnholt_campaign, ambiguous
    );
    expect(
        refused.error == cr::GraphSourceError::invalid_graph &&
            refused.graph_error == campaign::GraphError::duplicate_priority,
        "and a flow with two edges of one priority is refused by name"
    );
    expect(
        cr::build_campaign_graph(package.game_id, 0U, flow.definition).error ==
            cr::GraphSourceError::unidentified_campaign,
        "as is a campaign with no identity to namespace its nodes by"
    );
}

// The campaign walks its own graph, decided by what the campaign committed.
void a_campaign_walks_the_graph_its_package_authored() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignGraph graph =
        cr::load_campaign_graph(package, tarnholt_campaign).source.graph;

    campaign::CampaignState held = full_roster(package);
    expect(
        campaign::begin_campaign(held, graph) ==
            campaign::ProgressionError::none,
        "the campaign enters the flow"
    );
    const auto won = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter), 0xaaULL, 0U},
        {campaign::record_objective(
            ref(package, core::ContentCategory::objective, ford_held),
            campaign::ObjectiveOutcome::satisfied
        )}
    );
    const campaign::NodeCompletion victory =
        campaign::complete_node(held, graph, won);
    expect(
        victory.advanced && !victory.used_fallback &&
            victory.target == cr::campaign_node_ref(package.game_id, watch_node),
        "and the satisfied objective takes the authored branch"
    );

    campaign::CampaignState lost = full_roster(package);
    expect(
        campaign::begin_campaign(lost, graph) ==
            campaign::ProgressionError::none,
        "another campaign enters the same flow"
    );
    const auto routed = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter), 0xbbULL, 0U},
        {campaign::record_objective(
            ref(package, core::ContentCategory::objective, ford_held),
            campaign::ObjectiveOutcome::failed
        )}
    );
    const campaign::NodeCompletion defeat =
        campaign::complete_node(lost, graph, routed);
    expect(
        defeat.advanced && defeat.used_fallback &&
            defeat.target ==
                cr::campaign_node_ref(package.game_id, retreat_node),
        "and the failed one falls back"
    );
}

// The requirement, and the property that keeps every golden where it is: a
// full roster loads exactly the board the package holds.
void a_full_roster_loads_the_board_the_package_holds() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignState state = full_roster(package);

    const pr::EncounterLoadResult plain =
        pr::load_encounter(package, ford_encounter);
    expect(static_cast<bool>(plain), "the encounter loads with no campaign");

    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(joined),
        std::string_view{cr::roster_error_name(joined.error)}
    );
    expect(
        joined.encounter.definition.units.size() ==
            plain.definition.units.size(),
        "and a campaign with a full roster loads the same board"
    );
    expect(
        board_hash(joined.encounter.definition) ==
            board_hash(plain.definition),
        "hash for hash, which is why no existing content moves"
    );
    expect(joined.excluded.empty(), "with nobody left off");
    expect(
        joined.binding.size() == 2U &&
            joined.binding.persistent_of(campaign::BattleEntityId{1000}) ==
                kestrel &&
            joined.binding.persistent_of(campaign::BattleEntityId{1001}) == brand,
        "and each deployed member bound to who they are on the board"
    );
    expect(
        joined.binding.persistent_of(campaign::BattleEntityId{1002}).value == 0U,
        "while the opposing side has no campaign identity at all"
    );

    // No assignments is the same board too: an encounter nobody's roster
    // claims is an encounter nobody's roster changes.
    const cr::CampaignEncounter unclaimed =
        cr::load_encounter_for_campaign(package, ford_encounter, state, {});
    expect(
        static_cast<bool>(unclaimed) &&
            board_hash(unclaimed.encounter.definition) ==
                board_hash(plain.definition) &&
            unclaimed.binding.size() == 0U,
        "and an encounter no assignment names is untouched"
    );
}

// Which members a board has somewhere to stand, which is a question about the
// board and about nothing else.
//
// It is what stops a between-battle screen from offering "field this member"
// for somebody the next map never placed: the campaign would accept the
// availability and the board would not change, which is a gesture that succeeds
// and does nothing. The answer consults no campaign state at all. A member
// who is dead, retired or not yet recruited is still somebody this board has
// a place for, and whether they take it is the exclusion pass's business.
void a_board_says_which_members_it_has_a_place_for() {
    const pf::LoadedPackage package = compile_and_load();

    const pr::EncounterLoadResult ford =
        pr::load_encounter(package, ford_encounter);
    expect(static_cast<bool>(ford), "the ford loads");
    expect(
        cr::members_a_board_places(ford, assignments()) ==
            std::vector<campaign::PersistentEntityId>{kestrel, brand},
        "the ford places both members of the company, ascending"
    );

    // A table naming somebody the board does not place answers without them,
    // and the raider, a placement no assignment claims, is nobody's member.
    const std::vector<cr::RosterAssignment> with_a_stranger{
        {kestrel_key, kestrel},
        {brand_key, brand},
        {raider_key, campaign::PersistentEntityId{7}},
        {captain_key, campaign::PersistentEntityId{8}},
    };
    expect(
        cr::members_a_board_places(ford, with_a_stranger) ==
            std::vector<campaign::PersistentEntityId>{
                kestrel, brand, campaign::PersistentEntityId{7}
            },
        "a member the board does place is named however the campaign holds "
        "them, and one it does not is absent"
    );
    expect(
        cr::members_a_board_places(ford, {}).empty(),
        "and a board no assignment claims places nobody at all"
    );

    // The same board with somebody left off it places one fewer, because the
    // published board is the board the question is about.
    campaign::CampaignState state = full_roster(package);
    expect(
        static_cast<bool>(campaign::apply_outcome(
            state,
            campaign::make_outcome_batch(
                {ref(package, core::ContentCategory::encounter, ford_encounter),
                 0U, 9U},
                {campaign::set_availability(
                    brand, campaign::Availability::retired
                )}
            )
        )),
        "a member may be set aside"
    );
    const cr::CampaignEncounter narrowed = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(narrowed) && narrowed.excluded.size() == 1U &&
            narrowed.excluded.front() == brand,
        "and the roster leaves them off exactly as it leaves off the dead"
    );
    expect(
        cr::members_a_board_places(narrowed.encounter, assignments()) ==
            std::vector<campaign::PersistentEntityId>{kestrel},
        "so the published board has a place for the one who stayed"
    );
}

// Which members would actually take a board's field, which is the question the
// capacity is counted against.
//
// `members_a_board_places` asks whether the board has anywhere to put them;
// this asks the second half, whether they would be let out, by the same
// judgement the exclusion pass makes. Published rather than left to each
// screen so that two clients cannot count differently, and so that no screen
// offers a fielding the engine would refuse.
void a_board_says_which_members_it_would_field() {
    const pf::LoadedPackage package = compile_and_load();
    const pr::EncounterLoadResult ford =
        pr::load_encounter(package, ford_encounter);
    expect(static_cast<bool>(ford), "the ford loads");

    const campaign::CampaignState whole = full_roster(package);
    expect(
        cr::members_a_board_fields(ford, whole, assignments()) ==
            cr::members_a_board_places(ford, assignments()),
        "a company nobody has narrowed fields everybody the board places"
    );
    expect(
        cr::members_a_board_fields(ford, whole, assignments()) ==
            std::vector<campaign::PersistentEntityId>{kestrel, brand},
        "which is both of them, ascending"
    );

    campaign::CampaignState benched = whole;
    expect(
        static_cast<bool>(campaign::apply_outcome(
            benched,
            campaign::make_outcome_batch(
                {ref(package, core::ContentCategory::encounter, ford_encounter),
                 0U, 9U},
                {campaign::set_availability(
                    brand, campaign::Availability::retired
                )}
            )
        )),
        "a member is asked to stay behind"
    );
    expect(
        cr::members_a_board_fields(ford, benched, assignments()) ==
            std::vector<campaign::PersistentEntityId>{kestrel},
        "and drops out of the set that would take the field"
    );
    expect(
        cr::members_a_board_places(ford, assignments()) ==
            std::vector<campaign::PersistentEntityId>{kestrel, brand},
        "while the board still has a place for them, because where a board can "
        "put somebody is not a fact about the campaign"
    );

    const campaign::CampaignState nobody;
    expect(
        cr::members_a_board_fields(ford, nobody, assignments()).empty(),
        "and a campaign that holds nobody fields nobody"
    );
}

// The refusal, and the two things it does not do: it publishes no board, and it
// benches nobody to make the company fit.
void a_company_over_its_boards_cap_is_refused() {
    const pf::LoadedPackage package = capped_package(ford_encounter, 1);
    campaign::CampaignState state = full_roster(package);
    const std::uint64_t before = campaign::canonical_hash(state);

    const cr::CampaignEncounter over = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(
        over.error == cr::RosterError::over_deployment_capacity,
        std::string_view{cr::roster_error_name(over.error)}
    );
    expect(
        over.encounter.definition.units.empty() &&
            over.encounter.definition.deployment_tiles.empty() &&
            over.encounter.deployment_zone_id == 0U,
        "and no board is published: the refusal is decided before a single "
        "unit of one reaches a caller"
    );
    expect(
        campaign::canonical_hash(state) == before,
        "and the campaign is exactly the campaign that asked: the engine "
        "refuses rather than choosing who fights"
    );

    // Benching one turns the refusal into a board, at exactly the cap. A
    // capacity is a maximum and not a quota, so the board publishes for a
    // company of one on a board that would seat two.
    expect(
        static_cast<bool>(campaign::apply_outcome(
            state,
            campaign::make_outcome_batch(
                {ref(package, core::ContentCategory::encounter, ford_encounter),
                 0x31ULL, 0U},
                {campaign::set_availability(
                    brand, campaign::Availability::retired
                )}
            )
        )),
        "the player benches somebody"
    );
    const cr::CampaignEncounter at_the_cap = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(at_the_cap),
        std::string_view{cr::roster_error_name(at_the_cap.error)}
    );
    expect(
        at_the_cap.binding.size() == 1U &&
            at_the_cap.binding.battle_of(kestrel).value == 1000U,
        "and the one who stayed is the one who is bound"
    );
    expect(
        at_the_cap.excluded.size() == 1U && at_the_cap.excluded.front() == brand,
        "with the benched member left off exactly as a benched member always "
        "has been"
    );
}

// The claim the whole cap rests on: it invents no third failure.
//
// A capacity refuses rather than leaving anybody off, so the only ways a
// capped board reaches these two are the ways an uncapped one already did, by
// benching. Both are asserted here rather than argued, because a capacity that
// could produce either of them under its own name would be a second
// arrangement of a battle rather than a rule about a roster.
void a_cap_answers_with_the_two_names_a_smaller_board_always_had() {
    const pf::LoadedPackage ford_capped = capped_package(ford_encounter, 1);
    campaign::CampaignState routed = full_roster(ford_capped);
    expect(
        static_cast<bool>(campaign::apply_outcome(
            routed,
            campaign::make_outcome_batch(
                {ref(ford_capped, core::ContentCategory::encounter,
                     ford_encounter),
                 0x41ULL, 0U},
                {campaign::set_availability(
                     kestrel, campaign::Availability::retired
                 ),
                 campaign::set_availability(
                     brand, campaign::Availability::retired
                 )}
            )
        )),
        "a company benches itself down to nothing"
    );
    expect(
        cr::load_encounter_for_campaign(
            ford_capped, ford_encounter, routed, assignments()
        ).error == cr::RosterError::side_emptied,
        "and a capped board answers with the emptied side, not with its cap"
    );

    const pf::LoadedPackage watch_capped = capped_package(watch_encounter, 1);
    campaign::CampaignState sent_home = full_roster(watch_capped);
    const std::vector<cr::RosterAssignment> claiming{
        {kestrel_key, kestrel}, {captain_key, brand}
    };
    expect(
        static_cast<bool>(campaign::apply_outcome(
            sent_home,
            campaign::make_outcome_batch(
                {ref(watch_capped, core::ContentCategory::encounter,
                     watch_encounter),
                 0x42ULL, 0U},
                {campaign::set_availability(
                    brand, campaign::Availability::retired
                )}
            )
        )),
        "and another benches the character an objective protects"
    );
    expect(
        cr::load_encounter_for_campaign(
            watch_capped, watch_encounter, sent_home, claiming
        ).error == cr::RosterError::unavailable_objective_target,
        "which a capped board answers with the unanswerable objective, not "
        "with its cap"
    );
}

// And where the cap does speak, it speaks first. A company that is both over
// its cap and missing an objective's target hears the cap, because the count
// is decided the moment the assignment table is believed and before anything
// about the shape of the board is asked. Stated as a test rather than left to
// the order of two `if`s, because it is the order a client's screens are
// written against.
void the_cap_is_answered_before_the_board_is_examined() {
    const pf::LoadedPackage package = capped_package(watch_encounter, 1);
    const campaign::CampaignState state = full_roster(package);
    // Both members field the watch, which caps one; and the captain the
    // objective protects is claimed for somebody this campaign never met, so
    // the board is unfightable as well as overfull.
    const std::vector<cr::RosterAssignment> crowded{
        {kestrel_key, kestrel},
        {brand_key, brand},
        {captain_key, campaign::PersistentEntityId{7}},
    };
    const cr::CampaignEncounter refused =
        cr::load_encounter_for_campaign(package, watch_encounter, state, crowded);
    expect(
        refused.error == cr::RosterError::over_deployment_capacity,
        std::string_view{cr::roster_error_name(refused.error)}
    );
    expect(
        refused.encounter.definition.units.empty(),
        "and nothing is published for the objective to be missing from"
    );
}

// The direct descendant of `a_region_nobody_used_costs_nothing` in
// `tests/simulation/encounter_test.cpp`, and the same kind of claim one layer
// up.
//
// **The compatibility claim: a board that gains a capacity no company reaches
// is the same board in every observable way.** The two packages below differ by
// exactly the two bytes of the capacity tail, and everything downstream of
// them (the published `EncounterDefinition`, the seed it derives, the join,
// and the canonical hash a battle would start from) is identical. That is what
// lets every golden hold unregenerated: an encounter that authors no capacity
// takes the branch this one takes, and this one changes nothing.
void a_capacity_nobody_reaches_costs_nothing() {
    // Three first-side placements, so a cap of two is a cap that could bind.
    // Two members take the field, so it does not.
    auto ranked = game_source();
    ranked.campaigns.front().roster.push_back({2004, "Rook", 60, 0});
    ranked.encounters.front().placements.push_back(
        {1003, 2004, 2004, 60, gc::EncounterSide::first, 1, 2}
    );
    ranked.encounters.front().deployment = {120, {{0, 0}, {0, 1}}};
    auto capped_source = ranked;
    capped_source.encounters.front().deployment.capacity = 2;

    const pf::LoadedPackage plain = compile_and_load(ranked);
    const pf::LoadedPackage capped = compile_and_load(capped_source);
    const campaign::CampaignState state = full_roster(plain);

    const cr::CampaignEncounter uncapped = cr::load_encounter_for_campaign(
        plain, ford_encounter, state, assignments()
    );
    const cr::CampaignEncounter within = cr::load_encounter_for_campaign(
        capped, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(uncapped) && static_cast<bool>(within),
        std::string_view{cr::roster_error_name(within.error)}
    );
    expect(
        within.encounter.deployment_capacity == 2U &&
            uncapped.encounter.deployment_capacity == 0U,
        "one board carries the cap and the other carries none, which is the "
        "only difference between the two packages"
    );
    expect(
        cr::members_a_board_fields(within.encounter, state, assignments())
                .size() == 2U,
        "and the company reaches it exactly, which is a cap that does not bind"
    );

    const std::vector<sim::UnitDefinition>& left =
        uncapped.encounter.definition.units;
    const std::vector<sim::UnitDefinition>& right =
        within.encounter.definition.units;
    bool same_units = left.size() == right.size();
    for (std::size_t index = 0; same_units && index < left.size(); ++index) {
        same_units = left[index].id == right[index].id &&
                     left[index].unit_type_id == right[index].unit_type_id &&
                     left[index].side == right[index].side &&
                     left[index].position == right[index].position &&
                     left[index].health == right[index].health &&
                     left[index].item_ids == right[index].item_ids;
    }
    expect(same_units && !left.empty(), "the published board is unit for unit the same board");
    expect(
        uncapped.encounter.definition.deployment_tiles ==
                within.encounter.definition.deployment_tiles &&
            uncapped.encounter.deployment_zone_id ==
                within.encounter.deployment_zone_id,
        "with the same region, arranged from the same tiles"
    );
    expect(
        uncapped.binding.size() == within.binding.size() &&
            within.binding.battle_of(kestrel) ==
                uncapped.binding.battle_of(kestrel) &&
            within.binding.battle_of(brand) ==
                uncapped.binding.battle_of(brand),
        "and the same join, member for member"
    );
    expect(
        uncapped.excluded == within.excluded,
        "leaving the same people off, which is nobody"
    );

    auto without = sim::create_encounter(uncapped.encounter.definition);
    auto with = sim::create_encounter(within.encounter.definition);
    expect(
        static_cast<bool>(without) && static_cast<bool>(with),
        "both are boards a battle could start from"
    );
    expect(
        without.encounter.snapshot().random.seed ==
            with.encounter.snapshot().random.seed,
        "a capacity does not move the seed the encounter derives for itself"
    );
    expect(
        board_hash(uncapped.encounter.definition) ==
            board_hash(within.encounter.definition),
        "and from there the two are the same battle, which is why no golden "
        "moves"
    );
}

// One authored specificity, written onto a member of the fixture's company.
gc::GameSource company_with_a_specificity(
    std::uint64_t member,
    gc::CampaignMemberSpecificity specificity
) {
    auto authored = game_source();
    for (gc::CampaignMember& candidate : authored.campaigns.front().roster) {
        if (candidate.id != member) continue;
        candidate.specificity = specificity;
        candidate.states_specificity = true;
    }
    return authored;
}

gc::CampaignMemberSpecificity stat_delta(
    gc::SpecificStat stat,
    std::int16_t by
) {
    gc::CampaignMemberSpecificity specificity;
    specificity.stat_deltas[static_cast<std::size_t>(stat)] = by;
    specificity.stated[static_cast<std::size_t>(stat)] = true;
    return specificity;
}

// The direct descendant of `a_capacity_nobody_reaches_costs_nothing` above and
// of `a_region_nobody_used_costs_nothing` one layer down, and the same kind of
// claim about the newest tail.
//
// **The compatibility claim: a package that gains the whole specificity
// mechanism, on a board where nobody standing is written to be more than their
// class, is the same board in every observable way.** The two packages below
// differ by exactly the presence of the tail (one campaign record ends at its
// members, the other carries a grants count of zero, a specificity count of
// one and a member's deltas), and everything downstream is identical: the
// published units, the join, the seed the board derives, and the canonical
// hash a battle would start from.
//
// That is what lets all five goldens hold unregenerated. Content that authors
// nothing takes the branch the left-hand side takes here, and the right-hand
// side proves that even carrying the tail costs a board nothing until somebody
// standing on it is the one the tail describes.
void a_specificity_nobody_authors_costs_nothing() {
    // Somebody is written to be more than their class, and joins at a node
    // this board has no placement for, so nobody on the ford is theirs. The
    // tail exists, is decoded, is attached to the board, and reaches nobody.
    auto elsewhere = game_source();
    elsewhere.campaigns.front().roster.push_back({2004, "Rook", 60, watch_node});
    auto specific = elsewhere;
    for (gc::CampaignMember& candidate : specific.campaigns.front().roster) {
        if (candidate.id != 2004) continue;
        candidate.specificity = stat_delta(gc::SpecificStat::defense, 5);
        candidate.specificity.reach_bonus = 3;
        candidate.states_specificity = true;
    }

    const pf::LoadedPackage plain = compile_and_load(elsewhere);
    const pf::LoadedPackage carried = compile_and_load(specific);
    const campaign::CampaignState state = full_roster(plain);

    const pr::CampaignLoadResult plain_record =
        pr::load_campaign(plain, tarnholt_campaign);
    const pr::CampaignLoadResult carried_record =
        pr::load_campaign(carried, tarnholt_campaign);
    expect(
        static_cast<bool>(plain_record) && static_cast<bool>(carried_record),
        "both campaigns load"
    );
    expect(
        plain_record.definition.specificities.empty() &&
            carried_record.definition.specificities.size() == 1U,
        "one record carries no tail and the other carries one entry, which is "
        "the only difference between the two packages"
    );

    const cr::CampaignEncounter without = cr::load_encounter_for_campaign(
        plain, tarnholt_campaign, ford_encounter, state, assignments()
    );
    const cr::CampaignEncounter with = cr::load_encounter_for_campaign(
        carried, tarnholt_campaign, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(without) && static_cast<bool>(with),
        std::string_view{cr::roster_error_name(with.error)}
    );
    expect(
        with.encounter.member_specificities.size() == 1U &&
            without.encounter.member_specificities.empty(),
        "the board really is carrying the table, so this is not a test of an "
        "absent one"
    );

    const std::vector<sim::UnitDefinition>& left =
        without.encounter.definition.units;
    const std::vector<sim::UnitDefinition>& right =
        with.encounter.definition.units;
    bool same_units = left.size() == right.size();
    for (std::size_t index = 0; same_units && index < left.size(); ++index) {
        same_units = left[index].id == right[index].id &&
                     left[index].unit_type_id == right[index].unit_type_id &&
                     left[index].side == right[index].side &&
                     left[index].position == right[index].position &&
                     left[index].health == right[index].health &&
                     left[index].strength == right[index].strength &&
                     left[index].defense == right[index].defense &&
                     left[index].speed == right[index].speed &&
                     left[index].reach_bonus == right[index].reach_bonus &&
                     left[index].item_ids == right[index].item_ids;
    }
    expect(
        same_units && !left.empty(),
        "the published board is unit for unit the same board"
    );
    expect(
        without.binding.size() == with.binding.size() &&
            with.binding.battle_of(kestrel) ==
                without.binding.battle_of(kestrel),
        "and the same join, member for member"
    );

    auto bare = sim::create_encounter(without.encounter.definition);
    auto tailed = sim::create_encounter(with.encounter.definition);
    expect(
        static_cast<bool>(bare) && static_cast<bool>(tailed),
        "both are boards a battle could start from"
    );
    expect(
        bare.encounter.snapshot().random.seed ==
            tailed.encounter.snapshot().random.seed,
        "a specificity nobody on the board authors does not move the seed"
    );
    expect(
        board_hash(without.encounter.definition) ==
            board_hash(with.encounter.definition),
        "and from there the two are the same battle, which is why no golden "
        "moves"
    );
}

// The knob itself: a delta lands on the unit the member stands in, and on
// nobody else.
void an_authored_delta_reaches_the_board() {
    auto specificity = stat_delta(gc::SpecificStat::defense, 4);
    specificity.stat_deltas[static_cast<std::size_t>(gc::SpecificStat::speed)] =
        2;
    specificity.stated[static_cast<std::size_t>(gc::SpecificStat::speed)] = true;
    specificity.reach_bonus = 2;
    const pf::LoadedPackage package =
        compile_and_load(company_with_a_specificity(kestrel_key, specificity));
    const campaign::CampaignState state = full_roster(package);
    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, tarnholt_campaign, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(joined),
        std::string_view{cr::roster_error_name(joined.error)}
    );

    const sim::UnitDefinition* written = nullptr;
    const sim::UnitDefinition* untouched = nullptr;
    for (const sim::UnitDefinition& unit : joined.encounter.definition.units) {
        const campaign::PersistentEntityId member =
            joined.binding.persistent_of(campaign::BattleEntityId{unit.id});
        if (member == kestrel) written = &unit;
        if (member == brand) untouched = &unit;
    }
    expect(
        written != nullptr && untouched != nullptr,
        "both members took the field"
    );
    // The class authors defense 1 and speed 1 (`game_source` above).
    expect(
        written->defense == 5 && written->speed == 3,
        "the character the author wrote about carries the class plus what was "
        "written about them"
    );
    expect(
        written->reach_bonus == 2U,
        "and the reach bonus rides on the unit rather than on the shared weapon"
    );
    expect(
        untouched->defense == 1 && untouched->speed == 1 &&
            untouched->reach_bonus == 0U,
        "and the member nobody wrote anything about is exactly their class, "
        "which is what makes a specificity a fact about a person"
    );
}

// The reason a specificity is an addition and not a replacement: the two kinds
// of adjustment a character carries compose, rather than one overwriting the
// other. This is the property the whole design rests on.
void an_authored_delta_and_an_earned_gain_compose() {
    const pf::LoadedPackage package = compile_and_load(
        company_with_a_specificity(
            kestrel_key, stat_delta(gc::SpecificStat::defense, 4)
        )
    );
    campaign::CampaignState state = full_roster(package);
    const auto grown = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter), 2U, 0U},
        {campaign::grow_stat(kestrel, campaign::GrowableStat::defense, 3)}
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, grown)),
        "the level-up commits"
    );

    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, tarnholt_campaign, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(joined),
        std::string_view{cr::roster_error_name(joined.error)}
    );
    const sim::UnitDefinition* written = nullptr;
    for (const sim::UnitDefinition& unit : joined.encounter.definition.units) {
        if (joined.binding.persistent_of(campaign::BattleEntityId{unit.id}) ==
            kestrel) {
            written = &unit;
        }
    }
    expect(written != nullptr, "the member took the field");
    // Class 1, authored +4, earned +3. Two addends over one authored line, and
    // neither of them is a total.
    expect(
        written->defense == 8,
        "what the author wrote and what the character earned are both added, "
        "so a specificity and a level-up are the same kind of thing"
    );
}

// The second of the two locks on the same door.
//
// The compiler refuses a delta that would take a stat outside what a class may
// hold, so nothing this repository compiles can reach the code below. It is
// reached by handing the join a table directly, which is exactly what a front
// end playing uncompiled content does and what some other tool writing a
// package could do. What it must not produce is a character the rules have no
// reading for. So the add saturates at each stat's own authored floor and at
// the storage ceiling, rather than wrapping to a character who is weaker for
// having been written stronger.
void an_out_of_range_delta_saturates_rather_than_wrapping() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignState state = full_roster(package);

    pr::EncounterLoadResult board =
        pr::load_encounter(package, ford_encounter);
    expect(static_cast<bool>(board), "the board loads");

    pr::MemberSpecificity ruinous;
    ruinous.member_id = kestrel_key;
    // Far below the floor of every stat, and far above the ceiling of one.
    ruinous.stat_deltas[static_cast<std::size_t>(pr::SpecificStat::health)] =
        -32000;
    ruinous.stat_deltas[static_cast<std::size_t>(pr::SpecificStat::movement)] =
        -32000;
    ruinous.stat_deltas[static_cast<std::size_t>(pr::SpecificStat::speed)] =
        -32000;
    ruinous
        .stat_deltas[static_cast<std::size_t>(pr::SpecificStat::action_points)] =
        -32000;
    ruinous.stat_deltas[static_cast<std::size_t>(pr::SpecificStat::defense)] =
        -32000;
    ruinous.stat_deltas[static_cast<std::size_t>(pr::SpecificStat::strength)] =
        32000;
    board.member_specificities = {ruinous};

    const cr::CampaignEncounter joined =
        cr::join_campaign_roster(std::move(board), state, assignments());
    expect(
        static_cast<bool>(joined),
        std::string_view{cr::roster_error_name(joined.error)}
    );
    const sim::UnitDefinition* written = nullptr;
    for (const sim::UnitDefinition& unit : joined.encounter.definition.units) {
        if (joined.binding.persistent_of(campaign::BattleEntityId{unit.id}) ==
            kestrel) {
            written = &unit;
        }
    }
    expect(written != nullptr, "the member took the field");
    expect(
        written->health == 1 && written->movement == 1U &&
            written->speed == 1U && written->action_points == 1U,
        "a delta below a stat's own floor lands on that floor, so no character "
        "is published who cannot live, move, act or take a turn"
    );
    expect(
        written->defense == 0,
        "and a stat whose floor is zero lands on zero rather than going "
        "negative"
    );
    // The class authors strength 4, and 4 + 32000 is inside the ceiling; the
    // point is that it did not wrap.
    expect(
        written->strength == 32004,
        "and a delta upward is the number it says until the ceiling, which it "
        "saturates at rather than wrapping past"
    );
}

// And the reason it is a delta rather than a stored total: a class rebalanced
// underneath a written character moves that character too.
void a_rebalanced_class_still_moves_a_written_character() {
    auto authored = company_with_a_specificity(
        kestrel_key, stat_delta(gc::SpecificStat::defense, 4)
    );
    auto rebalanced = authored;
    rebalanced.classes.front().base_stats.defense = 6;

    const pf::LoadedPackage before = compile_and_load(authored);
    const pf::LoadedPackage after = compile_and_load(rebalanced);
    const campaign::CampaignState state = full_roster(before);

    const auto defense_of = [&state](const pf::LoadedPackage& package) {
        const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
            package, tarnholt_campaign, ford_encounter, state, assignments()
        );
        expect(
            static_cast<bool>(joined),
            std::string_view{cr::roster_error_name(joined.error)}
        );
        for (const sim::UnitDefinition& unit :
             joined.encounter.definition.units) {
            if (joined.binding.persistent_of(
                    campaign::BattleEntityId{unit.id}
                ) == kestrel) {
                return unit.defense;
            }
        }
        expect(false, "the member took the field");
        return static_cast<std::int16_t>(0);
    };
    expect(
        defense_of(before) == 5 && defense_of(after) == 10,
        "the character keeps what was written about them and inherits "
        "everything the author changed underneath it"
    );
}

// The exclusion pass rebuilds the board, and what it rebuilds must still be
// the board the author wrote. A region survived the pass that leaves nobody
// off; this is the assertion that it survives the pass that leaves somebody
// off, which is the only pass a campaign with a casualty ever takes.
void a_narrowed_board_keeps_the_deployment_its_author_wrote() {
    auto ranked = game_source();
    ranked.campaigns.front().roster.push_back({2004, "Rook", 60, 0});
    ranked.encounters.front().placements.push_back(
        {1003, 2004, 2004, 60, gc::EncounterSide::first, 1, 2}
    );
    ranked.encounters.front().deployment = {120, {{0, 0}, {0, 1}}, 2};
    const pf::LoadedPackage package = compile_and_load(ranked);

    campaign::CampaignState state = full_roster(package);
    expect(
        static_cast<bool>(campaign::apply_outcome(
            state,
            campaign::make_outcome_batch(
                {ref(package, core::ContentCategory::encounter, ford_encounter),
                 0x51ULL, 0U},
                {campaign::record_permanent_death(brand)}
            )
        )),
        "a member falls for good"
    );
    const cr::CampaignEncounter narrowed = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(narrowed) && narrowed.excluded.size() == 1U,
        std::string_view{cr::roster_error_name(narrowed.error)}
    );
    expect(
        narrowed.encounter.definition.deployment_tiles ==
            std::vector<sim::Position>{{0, 0}, {0, 1}},
        "the rebuilt board still carries the region the author wrote"
    );
    expect(
        narrowed.encounter.deployment_zone_id == 120U &&
            narrowed.encounter.deployment_capacity == 2U,
        "and its identity and its cap, which a board that lost somebody has no "
        "less of a claim to than one that lost nobody"
    );
    auto created = sim::create_encounter(narrowed.encounter.definition);
    expect(
        static_cast<bool>(created) && created.encounter.snapshot().deploying,
        "so the battle it makes still opens in the deployment phase"
    );
}

// ---------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------

// What a campaign is given by its author, read as operations for the batch that
// gives it. The same shape `starting_kit` has, against the other owner: the
// reserved zero the shared store has always been addressed by.
void a_node_says_what_it_gives_the_store() {
    auto authored = game_source();
    authored.items.push_back({51, "Rope", 20, 1});
    authored.campaigns.front().grants = {
        {50, 3, 0}, {51, 1, 0}, {50, 2, ford_node}
    };
    const pf::LoadedPackage package = compile_and_load(authored);
    const pr::CampaignLoadResult flow =
        pr::load_campaign(package, tarnholt_campaign);
    expect(static_cast<bool>(flow), "the stocked campaign decodes");

    const std::vector<campaign::CampaignOutcomeOperation> founding =
        cr::node_item_grants(package.game_id, flow.definition, 0U);
    expect(
        founding.size() == 2U,
        "the founding stock is what the campaign states at the reserved zero "
        "node"
    );
    if (founding.size() != 2U) return;
    expect(
        founding[0].kind == campaign::OutcomeOperationKind::add_item &&
            founding[0].subject.value == 0U &&
            founding[0].definition ==
                ref(package, core::ContentCategory::item, 50) &&
            founding[0].amount == 3,
        "as an addition against the shared store, of the identity and the "
        "quantity the author wrote"
    );
    expect(
        founding[1].definition ==
                ref(package, core::ContentCategory::item, 51) &&
            founding[1].amount == 1 && founding[1].subject.value == 0U,
        "and the second in the order it was authored, because the order is the "
        "order the operations are built in"
    );

    const std::vector<campaign::CampaignOutcomeOperation> at_the_ford =
        cr::node_item_grants(package.game_id, flow.definition, ford_node);
    expect(
        at_the_ford.size() == 1U && at_the_ford.front().subject.value == 0U &&
            at_the_ford.front().amount == 2 &&
            at_the_ford.front().definition ==
                ref(package, core::ContentCategory::item, 50),
        "a node's own grant is what that node states, and nothing the founding "
        "stated"
    );
    expect(
        cr::node_item_grants(package.game_id, flow.definition, watch_node)
            .empty(),
        "and a node that grants nothing gives nothing, said by an empty list "
        "rather than by an operation of zero"
    );
}

// ---------------------------------------------------------------------------
// Growth
// ---------------------------------------------------------------------------

// The events one battle would have emitted, written by hand. The rules under
// test are the campaign's, not the simulation's, and a hand-written event list
// is what lets a test say "the raider fell to Kestrel" without arranging a
// board that happens to produce it.
sim::Event felled(sim::UnitId who, sim::UnitId by) {
    return {sim::EventType::unit_defeated, who, by, {}, 0, sim::Outcome::ongoing};
}

campaign::OutcomeSource battle_source(
    const pf::LoadedPackage& package,
    std::uint64_t hash,
    std::uint64_t sequence
) {
    return {
        ref(package, core::ContentCategory::encounter, ford_encounter),
        hash,
        sequence
    };
}

// Experience goes to whoever struck the felling blow, and to nobody else. Then
// the level a lifetime total reaches, and then the rolls that level takes.
void a_defeat_pays_the_member_who_caused_it() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignState state = full_roster(package);
    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(static_cast<bool>(joined), "the board joins");

    const cr::BattleProgression earned = cr::derive_battle_progression(
        package,
        state,
        joined.encounter.definition,
        joined.binding,
        {felled(1002, 1000)},
        battle_source(package, 0x1234ULL, 0U)
    );
    expect(
        static_cast<bool>(earned),
        std::string_view{cr::progression_source_error_name(earned.error)}
    );
    expect(
        earned.level_ups.size() == 1U &&
            earned.level_ups.front().member == kestrel &&
            earned.level_ups.front().from_level == 1U &&
            earned.level_ups.front().to_level == 2U,
        "the member who felled the raider reaches level two, and only them"
    );
    // Sixty experience is one level at fifty a level, so six chances are rolled
    // once each and the batch says what they gave.
    expect(
        earned.operations.size() >= 2U &&
            earned.operations[0].kind ==
                campaign::OutcomeOperationKind::grant_experience &&
            earned.operations[0].subject == kestrel &&
            earned.operations[0].amount == 60 &&
            earned.operations[1].kind ==
                campaign::OutcomeOperationKind::advance_level &&
            earned.operations[1].amount == 1,
        "as the experience it earned and the level that total reached, in that "
        "order"
    );
    std::uint16_t granted = 0;
    for (std::size_t index = 2; index < earned.operations.size(); ++index) {
        expect(
            earned.operations[index].kind ==
                campaign::OutcomeOperationKind::grow_stat,
            "and then nothing but what the level gave"
        );
        granted = static_cast<std::uint16_t>(
            granted + earned.operations[index].amount
        );
    }
    std::uint16_t rolled = 0;
    for (std::uint16_t points : earned.level_ups.front().points) {
        rolled = static_cast<std::uint16_t>(rolled + points);
    }
    expect(
        granted == rolled && rolled > 0U,
        "with one operation per stat that grew and none for a stat that did not"
    );

    for (const campaign::CampaignOutcomeOperation& operation :
         earned.operations) {
        expect(
            operation.subject == kestrel,
            "and nobody who did not fell anybody is in the batch"
        );
    }

    // A unit type worth nothing is worth nothing. The blue units author no
    // award, so felling one pays whoever did it exactly zero, and the batch is
    // empty rather than carrying a grant of nothing.
    const cr::BattleProgression worthless = cr::derive_battle_progression(
        package,
        state,
        joined.encounter.definition,
        joined.binding,
        {felled(1001, 1000)},
        battle_source(package, 0x1234ULL, 0U)
    );
    expect(
        static_cast<bool>(worthless) && worthless.operations.empty() &&
            worthless.level_ups.empty(),
        "an unauthored award pays nothing and records nothing"
    );
}

// A member the battle buried earns nothing. Not a policy detail: the batch that
// records their death would refuse a grant on the same member, so the rule is
// what keeps the two halves of one batch consistent.
void the_fallen_earn_nothing() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignState state = full_roster(package);
    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );

    const cr::BattleProgression traded = cr::derive_battle_progression(
        package,
        state,
        joined.encounter.definition,
        joined.binding,
        {felled(1002, 1000), felled(1000, 1002)},
        battle_source(package, 0x2222ULL, 0U)
    );
    expect(
        static_cast<bool>(traded) && traded.operations.empty() &&
            traded.level_ups.empty(),
        "a rider who traded their life for the kill takes nothing from it"
    );
}

// The two properties the seed exists for: the same completion rolls the same
// numbers, and a different completion rolls different ones.
void the_same_completion_rolls_the_same_numbers() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignState state = full_roster(package);
    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    const std::vector<sim::Event> events{felled(1002, 1000)};

    const cr::BattleProgression first = cr::derive_battle_progression(
        package, state, joined.encounter.definition, joined.binding, events,
        battle_source(package, 0x3333ULL, 0U)
    );
    const cr::BattleProgression again = cr::derive_battle_progression(
        package, state, joined.encounter.definition, joined.binding, events,
        battle_source(package, 0x3333ULL, 0U)
    );
    expect(
        first.level_ups.size() == 1U && again.level_ups.size() == 1U &&
            first.level_ups.front().points == again.level_ups.front().points,
        "recomputing one completion recomputes its rolls, which is what makes "
        "a retry safe"
    );

    // A second completion of the same node is a different sequence, and a
    // battle that ended differently is a different hash. Either changes the
    // seed, so neither reuses the other's numbers.
    expect(
        campaign::derive_growth_seed(battle_source(package, 0x3333ULL, 0U)) !=
                campaign::derive_growth_seed(
                    battle_source(package, 0x3333ULL, 1U)
                ) &&
            campaign::derive_growth_seed(
                battle_source(package, 0x3333ULL, 0U)
            ) != campaign::derive_growth_seed(
                     battle_source(package, 0x4444ULL, 0U)
                 ),
        "while a second completion and a different ending each get their own "
        "seed"
    );
}

// The ceiling, and what a character at it earns: nothing, said by an empty
// batch rather than by a number that buys nothing.
void a_character_at_the_ceiling_earns_nothing() {
    const pf::LoadedPackage package = compile_and_load();
    campaign::CampaignState state = full_roster(package);
    const auto raised = campaign::make_outcome_batch(
        battle_source(package, 0x5555ULL, 0U),
        {campaign::advance_level(
            kestrel,
            static_cast<std::uint16_t>(campaign::maximum_progression_level - 1U)
        )}
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, raised)),
        "a member is walked up to the ceiling"
    );
    expect(
        campaign::find_unit(state, kestrel)->progression.level ==
            campaign::maximum_progression_level,
        "and stands on it"
    );

    // One more level is refused rather than clamped: a campaign that quietly
    // disagreed with the batch that produced it would be worse than a refusal.
    const auto beyond = campaign::make_outcome_batch(
        battle_source(package, 0x5556ULL, 0U),
        {campaign::advance_level(kestrel, 1U)}
    );
    expect(
        campaign::apply_outcome(state, beyond).error ==
            campaign::OutcomeError::quantity_overflow,
        "and cannot be pushed past it"
    );

    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    const cr::BattleProgression capped = cr::derive_battle_progression(
        package,
        state,
        joined.encounter.definition,
        joined.binding,
        {felled(1002, 1000)},
        battle_source(package, 0x5557ULL, 0U)
    );
    expect(
        static_cast<bool>(capped) && capped.operations.empty() &&
            capped.level_ups.empty(),
        "a member at the ceiling earns nothing at all, and the batch says so "
        "by containing nothing about them"
    );
}

// The second half of growth: what the roster adds to the board, and what it
// does not add to a board nobody's roster claims.
void a_levelled_member_takes_the_field_as_who_they_became() {
    const pf::LoadedPackage package = compile_and_load();
    campaign::CampaignState state = full_roster(package);

    const pr::EncounterLoadResult plain =
        pr::load_encounter(package, ford_encounter);
    const std::uint64_t authored = board_hash(plain.definition);

    const auto grew = campaign::make_outcome_batch(
        battle_source(package, 0x6666ULL, 0U),
        {campaign::advance_level(kestrel, 2U),
         campaign::grow_stat(kestrel, campaign::GrowableStat::health, 3U),
         campaign::grow_stat(kestrel, campaign::GrowableStat::movement, 1U)}
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, grew)),
        "two levels' worth of points commit"
    );

    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(static_cast<bool>(joined), "and the board still loads");
    const sim::UnitDefinition* stood = nullptr;
    for (const sim::UnitDefinition& unit : joined.encounter.definition.units) {
        if (unit.id == 1000U) stood = &unit;
    }
    // The blue class is six health and three movement.
    expect(
        stood != nullptr && stood->health == 9 && stood->movement == 4,
        "the member stands with the points their levels granted added to the "
        "authored class"
    );
    expect(
        stood != nullptr && stood->strength == 4 && stood->defense == 1,
        "and with every stat the levels did not touch exactly as authored"
    );
    expect(
        board_hash(joined.encounter.definition) != authored,
        "so the campaign's board is not the package's board any more, which is "
        "the whole point of a character who grew"
    );
    expect(
        board_hash(pr::load_encounter(package, ford_encounter).definition) ==
            authored,
        "while the package with no campaign attached loads exactly what it "
        "always loaded"
    );
}

// What a campaign character carries is what the campaign holds for them, and
// nothing else. This is the whole of that wire, asserted at the join.
void a_member_fields_the_kit_the_campaign_holds() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::DefinitionRef tonic =
        ref(package, core::ContentCategory::item, 50);

    // The authored list, read for a caller that has no board. One item, in the
    // order the unit type states it.
    const cr::StartingKit kit = cr::starting_kit(package, kestrel, 60);
    expect(
        static_cast<bool>(kit) && kit.operations.size() == 1U &&
            kit.operations.front().kind ==
                campaign::OutcomeOperationKind::add_item &&
            kit.operations.front().subject == kestrel &&
            kit.operations.front().definition == tonic &&
            kit.operations.front().amount == 1,
        "a unit type's starting items become one addition each, against the "
        "member who is joining"
    );
    expect(
        !cr::starting_kit(package, kestrel, 9999U),
        "and a unit type the package does not carry says so rather than "
        "quietly starting nobody with nothing"
    );
    const cr::StartingKit red = cr::starting_kit(package, kestrel, 61);
    expect(
        static_cast<bool>(red) && red.operations.empty(),
        "while a unit type that lists nothing is a successful read of nothing"
    );

    campaign::CampaignState state = full_roster(package);
    expect(
        campaign::item_quantity(state, kestrel, tonic) == 1U &&
            campaign::item_quantity(state, brand, tonic) == 1U &&
            campaign::item_quantity(state, {}, tonic) == 0U,
        "founding puts the authored draught in each member's own hands and "
        "nothing at all in the shared store"
    );

    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(joined),
        std::string_view{cr::roster_error_name(joined.error)}
    );
    const auto unit_on_board =
        [&joined](std::uint64_t id) -> const sim::UnitDefinition* {
        for (const sim::UnitDefinition& unit : joined.encounter.definition.units) {
            if (unit.id == id) return &unit;
        }
        return nullptr;
    };
    const sim::UnitDefinition* const stocked = unit_on_board(1000U);
    expect(
        stocked != nullptr && stocked->item_ids.size() == 1U &&
            stocked->item_ids.front() == 50U &&
            stocked->item_counts.size() == 1U &&
            stocked->item_counts.front() == 1U,
        "a member holding one draught fields one draught, counted rather than "
        "left to the loader's default"
    );
    const sim::UnitDefinition* const raider = unit_on_board(1002U);
    expect(
        raider != nullptr && raider->item_ids.empty(),
        "and the opposing side, which no member stands in, carries what its "
        "type lists"
    );

    // A spend, and then a later map. This is the dishonesty the rule exists to
    // kill, at the smallest scale it can be shown: what a campaign spends stays
    // spent.
    const auto drunk = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter),
         0xa1ULL, 0U},
        {campaign::consume_item(kestrel, tonic, 1U)}
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, drunk)),
        "a member spends their only draught"
    );
    const cr::CampaignEncounter after = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(static_cast<bool>(after), "and the next board still loads");
    const sim::UnitDefinition* emptied = nullptr;
    const sim::UnitDefinition* untouched = nullptr;
    for (const sim::UnitDefinition& unit : after.encounter.definition.units) {
        if (unit.id == 1000U) emptied = &unit;
        if (unit.id == 1001U) untouched = &unit;
    }
    expect(
        emptied != nullptr && emptied->item_ids.empty() &&
            emptied->item_counts.empty(),
        "the spender takes the next field with nothing, however plainly their "
        "unit type lists a draught"
    );
    expect(
        untouched != nullptr && untouched->item_ids.size() == 1U,
        "while the member who spent nothing still carries theirs"
    );

    // A board a battle can still start from, which is the other half of
    // fielding an empty pack: nothing about an empty satchel is invalid.
    expect(
        board_hash(after.encounter.definition) !=
            board_hash(joined.encounter.definition),
        "and the two boards are not the same board, because what a character "
        "carries is canonical state"
    );
}

// The whole point of the gesture, end to end: a battle in which somebody was
// talked off the board raises a world flag, and a graph edge conditioned on
// that flag is the one the campaign takes.
void a_talk_raises_a_flag_a_graph_edge_reads() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignState state = full_roster(package);
    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(joined),
        std::string_view{cr::roster_error_name(joined.error)}
    );

    constexpr sim::ContentId heard_him_out = 0x5150U;
    std::vector<sim::Event> events;
    events.push_back(
        {sim::EventType::unit_talked, 1002U, 1000U, {}, 0,
         sim::Outcome::ongoing, heard_him_out}
    );
    const cr::BattleProgression derived = cr::derive_battle_progression(
        package,
        state,
        joined.encounter.definition,
        joined.binding,
        events,
        {ref(package, core::ContentCategory::encounter, ford_encounter),
         0xb3ULL, 0U}
    );
    expect(
        static_cast<bool>(derived),
        std::string_view{cr::progression_source_error_name(derived.error)}
    );
    expect(
        derived.operations.size() == 1U &&
            derived.operations[0].kind ==
                campaign::OutcomeOperationKind::set_world_flag,
        "a talk becomes exactly one world-flag operation and nothing else"
    );
    expect(
        derived.operations[0].definition.category ==
                core::ContentCategory::world_flag &&
            derived.operations[0].definition.stable_id ==
                heard_him_out,
        "and it names the flag the placement authored, under its own category"
    );

    // Nobody was paid for the conversation. A defeat event in the same slot
    // would have earned experience; a departure earns none, which is what
    // keeps talking somebody down from being a kill under another name.
    expect(
        derived.level_ups.empty(),
        "and nobody levelled for talking to somebody"
    );

    // A battle in which nobody was talked to raises nothing at all.
    const cr::BattleProgression quiet = cr::derive_battle_progression(
        package,
        state,
        joined.encounter.definition,
        joined.binding,
        {},
        {ref(package, core::ContentCategory::encounter, ford_encounter),
         0xb3ULL, 0U}
    );
    expect(
        quiet.operations.empty(),
        "a battle with no talk in it raises no flag"
    );

    // And the flag, once set, is what an edge reads.
    campaign::CampaignState advanced = state;
    expect(
        static_cast<bool>(
            campaign::apply_outcome(
                advanced,
                campaign::make_outcome_batch(
                    {ref(package, core::ContentCategory::encounter,
                         ford_encounter),
                     0xb3ULL, 0U},
                    derived.operations
                )
            )
        ),
        "the batch applies"
    );
    const campaign::WorldValue* held = campaign::find_world_value(
        advanced,
        {package.game_id, core::ContentCategory::world_flag, heard_him_out}
    );
    expect(
        held != nullptr && held->type == campaign::WorldValueType::boolean &&
            held->value == 1,
        "the campaign now holds the flag the battle raised"
    );
    expect(
        campaign::predicate_holds(
            advanced,
            campaign::world_flag_equals(
                {package.game_id, core::ContentCategory::world_flag,
                 heard_him_out},
                {campaign::WorldValueType::boolean, 1}
            )
        ),
        "so an edge asking about it holds, and the branch nobody else sees "
        "opens"
    );
}

// Where a spend lands, and where a drop lands. Two owners, decided by where the
// thing came from rather than by one rule for both.
void a_spend_is_charged_to_whoever_spent_it() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignState state = full_roster(package);
    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(joined),
        std::string_view{cr::roster_error_name(joined.error)}
    );

    // Three events: the member drinks, the opposing side drinks its own, and
    // something falls to the member. Only two of them are the campaign's.
    std::vector<sim::Event> events;
    events.push_back(
        {sim::EventType::item_used, 1000U, 1000U, {}, 0, sim::Outcome::ongoing, 50U}
    );
    events.push_back(
        {sim::EventType::item_used, 1002U, 1002U, {}, 0, sim::Outcome::ongoing, 50U}
    );
    events.push_back(
        {sim::EventType::item_dropped, 1002U, 1000U, {}, 0, sim::Outcome::ongoing, 50U}
    );
    const cr::BattleProgression derived = cr::derive_battle_progression(
        package,
        state,
        joined.encounter.definition,
        joined.binding,
        events,
        {ref(package, core::ContentCategory::encounter, ford_encounter),
         0xb2ULL, 0U}
    );
    expect(
        static_cast<bool>(derived),
        std::string_view{cr::progression_source_error_name(derived.error)}
    );
    expect(
        derived.operations.size() == 2U,
        "an enemy drinking its own draught is not the campaign's business"
    );
    expect(
        derived.operations[0].kind ==
                campaign::OutcomeOperationKind::add_item &&
            derived.operations[0].subject.value == 0U,
        "what fell goes to the army, first"
    );
    expect(
        derived.operations[1].kind ==
                campaign::OutcomeOperationKind::consume_item &&
            derived.operations[1].subject == kestrel,
        "and what was drunk comes out of the hands that drank it"
    );

    // And it is a fact rather than a wish: a member who does not hold it is
    // refused, whole.
    campaign::CampaignState empty_handed;
    const auto founded = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter), 2U, 0U},
        {campaign::recruit_unit(
             kestrel, ref(package, core::ContentCategory::unit_type, 60)
         ),
         campaign::set_availability(kestrel, campaign::Availability::available)}
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(empty_handed, founded)),
        "a campaign that stocked nobody founds anyway"
    );
    const campaign::OutcomeApplication refused = campaign::apply_outcome(
        empty_handed,
        campaign::make_outcome_batch(
            {ref(package, core::ContentCategory::encounter, ford_encounter),
             0xb3ULL, 0U},
            {campaign::consume_item(
                kestrel, ref(package, core::ContentCategory::item, 50), 1U
            )}
        )
    );
    expect(
        refused.error == campaign::OutcomeError::insufficient_items,
        "and spending out of an empty pair of hands is refused by name"
    );
}

// The requirement itself: a permanently dead member does not appear on a later
// map, however plainly that map lists them.
void a_dead_member_is_left_off_every_later_map() {
    const pf::LoadedPackage package = compile_and_load();
    campaign::CampaignState state = full_roster(package);
    const auto fell = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter), 0xccULL, 0U},
        {campaign::record_permanent_death(brand)}
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, fell)),
        "a member falls for good"
    );

    // The ford lists both. The roster lists one.
    const cr::CampaignEncounter later = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(
        static_cast<bool>(later),
        std::string_view{cr::roster_error_name(later.error)}
    );
    expect(
        later.encounter.definition.units.size() == 2U,
        "the later map fields one member fewer"
    );
    expect(
        later.excluded.size() == 1U && later.excluded.front() == brand,
        "and says who it left off"
    );
    const std::vector<sim::UnitDefinition>& units =
        later.encounter.definition.units;
    expect(
        std::none_of(
            units.begin(),
            units.end(),
            [](const sim::UnitDefinition& unit) { return unit.id == 1001; }
        ),
        "the fallen member is not on the board"
    );
    expect(
        later.encounter.behaviors.size() == 2U &&
            later.encounter.placements.size() == 2U,
        "and neither is anything that was parallel to them"
    );
    expect(
        later.binding.size() == 1U &&
            later.binding.battle_of(brand).value == 0U &&
            later.binding.battle_of(kestrel).value == 1000U,
        "and the binding holds only who was actually deployed"
    );
    expect(
        board_hash(later.encounter.definition) !=
            board_hash(pr::load_encounter(package, ford_encounter).definition),
        "so the board is not the board the package alone would have made"
    );

    // Retired and unrecruited are the same answer. Only `dead` is the one no
    // rule reverses, and reversing the reversible one puts them back.
    campaign::CampaignState retired = full_roster(package);
    const auto sent_away = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter), 0xddULL, 0U},
        {campaign::set_availability(brand, campaign::Availability::retired)}
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(retired, sent_away)),
        "a member is sent away"
    );
    expect(
        cr::load_encounter_for_campaign(
            package, ford_encounter, retired, assignments()
        ).excluded.size() == 1U,
        "and is left off while they are away"
    );
    const auto recalled = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter), 0xeeULL, 1U},
        {campaign::set_availability(brand, campaign::Availability::available)}
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(retired, recalled)),
        "and recalled"
    );
    expect(
        cr::load_encounter_for_campaign(
            package, ford_encounter, retired, assignments()
        ).excluded.empty(),
        "and takes the field again, which death never does"
    );

    // A member the campaign has never heard of cannot be deployed either.
    const campaign::CampaignState empty;
    const cr::CampaignEncounter strangers = cr::load_encounter_for_campaign(
        package, ford_encounter, empty, assignments()
    );
    expect(
        strangers.excluded.size() == 2U,
        "and a roster that holds nobody deploys nobody"
    );
}

// The refusals: a board whose objective names somebody who is not there, a
// side emptied entirely, and an assignment table that cannot be believed.
void a_board_that_cannot_be_fought_is_refused_rather_than_published() {
    const pf::LoadedPackage package = compile_and_load();
    campaign::CampaignState state = full_roster(package);

    // The watch's objective protects the captain, who is the opposing side and
    // has no campaign identity until a roster claims them.
    const std::vector<cr::RosterAssignment> claiming{
        {kestrel_key, kestrel}, {captain_key, brand}
    };
    const auto fell = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, watch_encounter), 0xfULL, 0U},
        {campaign::record_permanent_death(brand)}
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, fell)),
        "the protected character is gone for good"
    );
    const cr::CampaignEncounter unfightable = cr::load_encounter_for_campaign(
        package, watch_encounter, state, claiming
    );
    expect(
        unfightable.error == cr::RosterError::unavailable_objective_target,
        "an objective whose target is not there has no answer, and is refused"
    );

    // And the same refusal reached the other way, which is the way a player can
    // now reach it: the protected character is not dead, they were set aside.
    // A campaign that leaves an objective's target behind has no answer either,
    // so the board is refused rather than published with a rule that cannot
    // resolve. That is what sends a between-battle screen back to the company.
    campaign::CampaignState sent_home = full_roster(package);
    expect(
        static_cast<bool>(campaign::apply_outcome(
            sent_home,
            campaign::make_outcome_batch(
                {ref(package, core::ContentCategory::encounter, watch_encounter),
                 0x21ULL, 0U},
                {campaign::set_availability(
                    brand, campaign::Availability::retired
                )}
            )
        )),
        "the protected character is set aside rather than buried"
    );
    expect(
        cr::load_encounter_for_campaign(
            package, watch_encounter, sent_home, claiming
        ).error == cr::RosterError::unavailable_objective_target,
        "and the board is refused by the same name, because the objective is "
        "as unanswerable either way"
    );

    // Emptying a side is refused for the same reason: a battle decided before
    // it began is not a battle.
    campaign::CampaignState routed = full_roster(package);
    const auto both_fell = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, ford_encounter), 0x11ULL, 0U},
        {
            campaign::record_permanent_death(kestrel),
            campaign::record_permanent_death(brand),
        }
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(routed, both_fell)),
        "everybody falls"
    );
    expect(
        cr::load_encounter_for_campaign(
            package, ford_encounter, routed, assignments()
        ).error == cr::RosterError::side_emptied,
        "and a side with nobody left to field is refused"
    );

    // And a table that names one placement twice, or one member twice, is
    // refused before a byte of the encounter is read.
    expect(
        cr::load_encounter_for_campaign(
            package,
            ford_encounter,
            state,
            {{kestrel_key, kestrel}, {kestrel_key, brand}}
        ).error == cr::RosterError::duplicate_assignment,
        "one placement cannot be two members"
    );
    expect(
        cr::load_encounter_for_campaign(
            package,
            ford_encounter,
            state,
            {{kestrel_key, kestrel}, {brand_key, kestrel}}
        ).error == cr::RosterError::duplicate_assignment,
        "and one member cannot be in two places"
    );
    expect(
        cr::load_encounter_for_campaign(
            package,
            ford_encounter,
            state,
            {{kestrel_key, campaign::PersistentEntityId{}}}
        ).error == cr::RosterError::reserved_identity,
        "and the reserved id is nobody"
    );
    const cr::CampaignEncounter missing = cr::load_encounter_for_campaign(
        package, 9999U, state, assignments()
    );
    expect(
        missing.error == cr::RosterError::encounter_rejected &&
            missing.load_error == pr::EncounterLoadError::missing_record,
        "and an encounter that is not there answers with the loader's own word"
    );
}

}  // namespace

// A whole board and the table read off it derive the same campaign.
//
// The overload over a table exists so that a client too small to keep an
// encounter alive across a battle can still commit one. It would be worthless
// if it derived anything else, so the claim is made against the identical
// battle rather than against a plausible one: the same package, the same state,
// the same binding, the same events and the same source, twice.
void a_unit_type_table_derives_what_the_whole_board_derives() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignState state = full_roster(package);
    const cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(static_cast<bool>(joined), "the board joins");

    const std::vector<cr::BoardUnitType> table =
        cr::board_unit_types(joined.encounter.definition);
    expect(
        table.size() == joined.encounter.definition.units.size(),
        "the table names every unit the board placed"
    );
    bool same_types = true;
    for (std::size_t index = 0; index < table.size(); ++index) {
        const sim::UnitDefinition& unit =
            joined.encounter.definition.units[index];
        same_types = same_types && table[index].unit == unit.id &&
                     table[index].unit_type_id == unit.unit_type_id;
    }
    expect(same_types, "in the board's own order, with the board's own types");

    const std::vector<sim::Event> events{
        felled(1002, 1000), felled(1001, 1000)
    };
    const campaign::OutcomeSource source =
        battle_source(package, 0x5150ULL, 3U);
    const cr::BattleProgression whole = cr::derive_battle_progression(
        package, state, joined.encounter.definition, joined.binding, events,
        source
    );
    const cr::BattleProgression narrow = cr::derive_battle_progression(
        package, state, table, joined.binding, events, source
    );

    bool identical = whole.error == narrow.error &&
                     whole.operations.size() == narrow.operations.size() &&
                     whole.level_ups.size() == narrow.level_ups.size();
    for (std::size_t index = 0;
         identical && index < whole.operations.size();
         ++index) {
        const campaign::CampaignOutcomeOperation& left = whole.operations[index];
        const campaign::CampaignOutcomeOperation& right =
            narrow.operations[index];
        identical = left.kind == right.kind && left.subject == right.subject &&
                    left.amount == right.amount &&
                    left.selector == right.selector &&
                    left.definition == right.definition;
    }
    for (std::size_t index = 0; identical && index < whole.level_ups.size();
         ++index) {
        identical = whole.level_ups[index].member ==
                        narrow.level_ups[index].member &&
                    whole.level_ups[index].from_level ==
                        narrow.level_ups[index].from_level &&
                    whole.level_ups[index].to_level ==
                        narrow.level_ups[index].to_level &&
                    whole.level_ups[index].points ==
                        narrow.level_ups[index].points;
    }
    expect(
        identical && !whole.operations.empty(),
        "the table derives the same operations, the same levels and the same "
        "growth as the board it was read off"
    );
}

// Killing the second of a wave pays for it, exactly as killing the first does.
//
// A wave is one authored placement and several characters, and only the first
// of them carries an identifier the content wrote: the rest are minted by
// `create_encounter` when it expands the recurrence. So a table read off the
// authored placements alone knows nothing about the character standing on the
// board that a player just felled, cannot say what unit type it was, and
// refuses the whole battle as `unreadable_unit_type`. In a campaign that is
// a board that can be won and then not committed. The claim is made against
// the identifiers the battle really used rather than against guessed ones.
void a_wave_pays_for_every_character_it_lands() {
    const pf::LoadedPackage package = compile_and_load();
    const campaign::CampaignState state = full_roster(package);
    cr::CampaignEncounter joined = cr::load_encounter_for_campaign(
        package, ford_encounter, state, assignments()
    );
    expect(static_cast<bool>(joined), "the board joins");

    // The raider arrives three times rather than once, and a picket of the same
    // kind stands in the ford from the opening so that the side is present:
    // a side made entirely of waves is `missing_side`, which is its own rule.
    sim::UnitDefinition* found = nullptr;
    for (sim::UnitDefinition& unit : joined.encounter.definition.units) {
        if (unit.id == 1002U) found = &unit;
    }
    expect(found != nullptr, "the raider is on the board");
    if (found == nullptr) return;
    sim::UnitDefinition picket = *found;
    picket.id = 1500U;
    picket.position = {found->position.x, static_cast<std::int16_t>(
                                              found->position.y + 1)};
    const std::uint64_t wave_type = found->unit_type_id;
    found->arrival_round = 2U;
    found->arrival_every = 2U;
    found->arrival_times = 3U;
    joined.encounter.definition.units.push_back(picket);

    const sim::Encounter::CreateResult built =
        sim::create_encounter(joined.encounter.definition);
    expect(
        static_cast<bool>(built),
        std::string_view{sim::error_name(built.error)}
    );
    const std::vector<cr::BoardUnitType> table =
        cr::board_unit_types(joined.encounter.definition);

    // Every character the battle fields is in the table, under the type the
    // battle gave it. Two of the three raiders were never written down.
    bool complete = built.encounter.snapshot().units.size() == table.size();
    std::vector<sim::UnitId> raiders;
    for (const sim::UnitSnapshot& unit : built.encounter.snapshot().units) {
        const auto row = std::find_if(
            table.begin(), table.end(),
            [&unit](const cr::BoardUnitType& entry) {
                return entry.unit == unit.id;
            }
        );
        if (row == table.end() || row->unit_type_id != unit.unit_type_id) {
            complete = false;
            continue;
        }
        if (unit.unit_type_id == wave_type && unit.id != 1500U) {
            raiders.push_back(unit.id);
        }
    }
    expect(
        complete && raiders.size() == 3U,
        "the table names every character the wave lands, not only the one the "
        "content wrote"
    );
    if (raiders.size() != 3U) return;

    // And a minted one is worth what the written one is worth. A later arrival
    // takes the lowest identifier the board does not use, so it sorts below the
    // content hashes rather than above them.
    const sim::UnitId minted =
        *std::min_element(raiders.begin(), raiders.end());
    expect(minted != 1002U, "a later arrival is a character of its own");

    const cr::BattleProgression written = cr::derive_battle_progression(
        package, state, table, joined.binding, {felled(1002U, 1000U)},
        battle_source(package, 0x7a1eULL, 0U)
    );
    const cr::BattleProgression unwritten = cr::derive_battle_progression(
        package, state, table, joined.binding, {felled(minted, 1000U)},
        battle_source(package, 0x7a1eULL, 0U)
    );
    expect(
        static_cast<bool>(unwritten),
        std::string_view{cr::progression_source_error_name(unwritten.error)}
    );
    bool alike = written.error == unwritten.error &&
                 written.operations.size() == unwritten.operations.size() &&
                 !written.operations.empty();
    for (std::size_t index = 0; alike && index < written.operations.size();
         ++index) {
        alike = written.operations[index].kind ==
                    unwritten.operations[index].kind &&
                written.operations[index].subject ==
                    unwritten.operations[index].subject &&
                written.operations[index].amount ==
                    unwritten.operations[index].amount;
    }
    expect(
        alike,
        "the second of a wave pays exactly what the placement it came from pays"
    );
}

int main() {
    an_authored_flow_becomes_a_graph();
    a_campaign_walks_the_graph_its_package_authored();
    a_full_roster_loads_the_board_the_package_holds();
    a_member_fields_the_kit_the_campaign_holds();
    a_spend_is_charged_to_whoever_spent_it();
    a_talk_raises_a_flag_a_graph_edge_reads();
    a_dead_member_is_left_off_every_later_map();
    a_board_that_cannot_be_fought_is_refused_rather_than_published();
    a_board_says_which_members_it_has_a_place_for();
    a_board_says_which_members_it_would_field();
    a_company_over_its_boards_cap_is_refused();
    a_cap_answers_with_the_two_names_a_smaller_board_always_had();
    the_cap_is_answered_before_the_board_is_examined();
    a_capacity_nobody_reaches_costs_nothing();
    a_specificity_nobody_authors_costs_nothing();
    an_authored_delta_reaches_the_board();
    an_authored_delta_and_an_earned_gain_compose();
    a_rebalanced_class_still_moves_a_written_character();
    an_out_of_range_delta_saturates_rather_than_wrapping();
    a_narrowed_board_keeps_the_deployment_its_author_wrote();
    a_node_says_what_it_gives_the_store();
    a_defeat_pays_the_member_who_caused_it();
    the_fallen_earn_nothing();
    the_same_completion_rolls_the_same_numbers();
    a_character_at_the_ceiling_earns_nothing();
    a_levelled_member_takes_the_field_as_who_they_became();
    a_unit_type_table_derives_what_the_whole_board_derives();
    a_wave_pays_for_every_character_it_lands();
    return failures == 0 ? 0 : 1;
}
