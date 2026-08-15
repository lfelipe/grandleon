// SPDX-License-Identifier: MIT
#include <grandleon/campaign/graph.hpp>
#include <grandleon/campaign/migration.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/campaign_runtime/campaign_runtime.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/storage/memory_storage.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// The vertical slice, end to end, in the maintained demo and with nothing
// pretending to be anything else.
//
// Every other test in `tests/campaign` and `tests/campaign_runtime` builds its
// content in C++ because it is testing one layer and wants the smallest input
// that exercises it. This one does the opposite on purpose. It reads
// `games/demo/source/project.json` off the disk, parses it with the real source
// reader, compiles it with the real compiler, loads the real package, joins a
// real roster to a real board, fights the fight with the real simulation, turns
// what happened into a real outcome batch, writes real envelope bytes into a
// real storage device, reads them back through the real migration pipeline, and
// then asks the next map for a board. The claim is the spec's, in one sentence:
// *a permanently dead character does not come back because a later map lists
// them*, and it holds across a save.
//
// The demo carries two campaigns. `demo_campaign` is the conformance slice and
// is untouched. Its encounter still plays to `673e5a59765c94c5`, which this
// test checks on its way past, because a content addition that moved that
// value would be a content addition that moved a golden. `muster_road` is the second,
// and it exists because the first cannot express this: one rider a side is a
// side that empties the moment anybody falls, and a board one side cannot field
// is refused rather than published (`RosterError::side_emptied`). Two riders and
// two maps is the smallest content that can show a survivor taking the field
// without the fallen one.

namespace campaign = grandleon::campaign;
namespace core = grandleon::core;
namespace cr = grandleon::campaign_runtime;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The company the package authors, and the persistent identities founding it
// assigns. Who is on a roster is campaign state, but *who a campaign begins
// with* is content: the demo names its two riders and the ferryman who joins
// after the crossing, and the ids below are one-based in exactly the order the
// campaign lists them, which is the rule `client::CampaignSession` founds by.
constexpr campaign::PersistentEntityId vanguard{1};
constexpr campaign::PersistentEntityId outrider{2};
constexpr campaign::PersistentEntityId ferryman{3};

const std::uint64_t muster_road = core::stable_content_id_v1("muster_road");
const std::uint64_t river_skirmish =
    core::stable_content_id_v1("muster_road/river_skirmish");
const std::uint64_t road_watch =
    core::stable_content_id_v1("muster_road/road_watch");
const std::uint64_t dawn_guard_unit =
    core::stable_content_id_v1("dawn_guard_unit");
const std::uint64_t defeat_all_opponents =
    core::stable_content_id_v1("defeat_all_opponents");
const std::uint64_t vanguard_key = core::stable_content_id_v1("muster_vanguard");
const std::uint64_t outrider_key = core::stable_content_id_v1("muster_outrider");
const std::uint64_t ferryman_key = core::stable_content_id_v1("muster_ferryman");
const std::uint64_t river_skirmish_node =
    core::stable_content_id_v1("river_skirmish");
const std::uint64_t field_tonic = core::stable_content_id_v1("field_tonic");

pf::LoadedPackage compile_the_maintained_demo() {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) + "/games/demo/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the maintained demo source opens");
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    for (const gc::SourceDiagnostic& diagnostic : parsed.diagnostics) {
        std::cerr << "source diagnostic: "
                  << gc::source_diagnostic_name(diagnostic.code) << ' '
                  << diagnostic.path << ' ' << diagnostic.detail << '\n';
    }
    expect(static_cast<bool>(parsed), "and maps natively");
    const gc::CompileResult compiled = gc::compile(parsed.source);
    for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
        std::cerr << "compiler diagnostic: "
                  << gc::diagnostic_name(diagnostic.code) << ' '
                  << diagnostic.path << '\n';
    }
    expect(static_cast<bool>(compiled), "and compiles");
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and loads");
    return loaded.package;
}

campaign::DefinitionRef ref(
    const pf::LoadedPackage& package,
    core::ContentCategory category,
    std::uint64_t stable
) {
    return {package.game_id, category, stable};
}

// The company, as the package states it. Nothing here is the test's opinion:
// the members, their names, their unit types and the node each of them joins
// at are decoded out of the campaign record.
pr::CampaignDefinition authored_company(const pf::LoadedPackage& package) {
    pr::CampaignLoadResult loaded = pr::load_campaign(package, muster_road);
    expect(
        static_cast<bool>(loaded),
        std::string_view{pr::error_name(loaded.error)}
    );
    return std::move(loaded.definition);
}

// Every authored member, bound to the persistent identity founding gives them.
// Future recruits are in the table too: a board that places somebody the
// campaign does not yet hold leaves them off, which is how a recruit is a
// stranger to every earlier map without anybody saying so.
std::vector<cr::RosterAssignment> assignments(
    const pr::CampaignDefinition& authored
) {
    std::vector<cr::RosterAssignment> table;
    for (const pr::CampaignMember& member : authored.members) {
        table.push_back(
            {member.id,
             campaign::PersistentEntityId{
                 static_cast<std::uint64_t>(table.size() + 1U)
             }}
        );
    }
    return table;
}

// Who joins when a node completes, as that node's own operations, and what
// they arrive carrying. Node zero is the founding, which is the same question
// asked of the campaign itself.
//
// The kit is part of joining rather than a separate step: a unit type's
// authored starting items are read once, into the hands of the member who is
// joining, in the batch that recruits them. It is `client::CampaignSession`'s
// own founding, spelled out here because this test drives the layer below the
// client.
std::vector<campaign::CampaignOutcomeOperation> joining(
    const pf::LoadedPackage& package,
    const pr::CampaignDefinition& authored,
    std::uint64_t node_id
) {
    std::vector<campaign::CampaignOutcomeOperation> operations;
    std::uint64_t identity = 0U;
    for (const pr::CampaignMember& member : authored.members) {
        ++identity;
        if (member.join_node_id != node_id) continue;
        const campaign::PersistentEntityId who{identity};
        operations.push_back(campaign::recruit_unit(
            who,
            ref(package, core::ContentCategory::unit_type, member.unit_type_id)
        ));
        operations.push_back(
            campaign::set_availability(who, campaign::Availability::available)
        );
        const cr::StartingKit kit =
            cr::starting_kit(package, who, member.unit_type_id);
        expect(
            static_cast<bool>(kit),
            "the package says what a member of this type starts with"
        );
        operations.insert(
            operations.end(), kit.operations.begin(), kit.operations.end()
        );
    }
    return operations;
}

// The company the campaign is founded with: the authored members who join at
// no node, recruited and deployable. The test tells the campaign nothing the
// package did not say.
campaign::CampaignState muster_roster(
    const pf::LoadedPackage& package,
    const pr::CampaignDefinition& authored
) {
    campaign::CampaignState state;
    const campaign::CampaignOutcomeBatch batch = campaign::make_outcome_batch(
        {ref(package, core::ContentCategory::encounter, river_skirmish), 1U, 0U},
        joining(package, authored, 0U)
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, batch)),
        "the two riders the campaign begins with join the roster"
    );
    return state;
}

// Fight it. Not a scripted outcome and not a stubbed one: these are commands
// the simulation accepts or refuses, and the deaths below are the deaths its
// own damage arithmetic produced.
//
// Both riders and the picket have seven health, four strength and one defence
// with a powerless training sword, so every blow is three and a defender that
// survives one strikes back. Seven against three is three swings, and the free
// counters are what keep the two sides in step: the picket needs all three to
// put the outrider down and is left on one having taken two counters doing it,
// so the vanguard's single blow is enough to finish it.
struct BattleResult final {
    std::uint64_t hash{};
    sim::Outcome outcome{sim::Outcome::ongoing};
    std::vector<sim::UnitId> fallen;
    // Every event the battle emitted, in the order it emitted them. This is
    // the whole of what the campaign layer reads back out of a fight: a
    // defeat event names who fell and who felled them, and that pair is what
    // experience is derived from.
    std::vector<sim::Event> events;
    // What the battle recorded as having fallen off the defeated. Read from
    // the final snapshot rather than from the events, so the two have to agree.
    std::vector<sim::DropRecord> drops;
};

BattleResult fight(const sim::EncounterDefinition& definition, const std::vector<sim::Command>& commands) {
    BattleResult result;
    sim::Encounter::CreateResult created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the board is a board a battle can start from");
    if (!created) {
        return result;
    }
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const sim::CommandResult applied = created.encounter.apply(commands[index]);
        result.events.insert(
            result.events.end(), applied.events.begin(), applied.events.end()
        );
        if (!applied) {
            std::cerr << "FAIL: command " << index << " refused: "
                      << sim::error_name(applied.error) << '\n';
            ++failures;
            return result;
        }
    }
    const sim::EncounterSnapshot snapshot = created.encounter.snapshot();
    result.hash = created.encounter.canonical_hash();
    result.outcome = snapshot.outcome;
    result.drops = snapshot.drops;
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.health <= 0) {
            result.fallen.push_back(unit.id);
        }
    }
    return result;
}

// The demo's own conformance slice, played on the way past. A second campaign
// in the same project must leave the first one's board, and therefore the first
// one's canonical hash, exactly where it was.
void the_maintained_conformance_slice_is_untouched(const pf::LoadedPackage& package) {
    const std::uint64_t bridge =
        core::stable_content_id_v1("demo_campaign/bridge_encounter");
    const pr::EncounterLoadResult decoded = pr::load_encounter(package, bridge);
    expect(static_cast<bool>(decoded), "the conformance encounter still decodes");
    const std::uint64_t first = core::stable_content_id_v1(
        "demo_campaign/bridge_encounter/dawn_guard_leader"
    );
    const std::uint64_t second = core::stable_content_id_v1(
        "demo_campaign/bridge_encounter/river_watch_leader"
    );
    const BattleResult played = fight(
        decoded.definition,
        {
            {sim::CommandType::move, first, {1, 1}, 0},
            {sim::CommandType::attack, first, {}, second},
            {sim::CommandType::attack, second, {}, first},
            {sim::CommandType::attack, first, {}, second},
        }
    );
    expect(
        played.outcome == sim::Outcome::first_side_won,
        "and still ends the way the demo says it ends"
    );
    expect(
        played.hash == 0x673e5a59765c94c5ULL,
        "at the golden hash the demo, the browser and the PlayStation all pin"
    );
}

// ---------------------------------------------------------------------------
// The slice
// ---------------------------------------------------------------------------

void the_demo_carries_a_death_through_a_save_into_the_next_map() {
    const pf::LoadedPackage package = compile_the_maintained_demo();
    if (failures != 0) {
        return;
    }
    the_maintained_conformance_slice_is_untouched(package);

    // 1. The campaign begins, walking the graph the package authored.
    const cr::CampaignGraphLoad flow = cr::load_campaign_graph(package, muster_road);
    expect(
        static_cast<bool>(flow),
        std::string_view{cr::graph_source_error_name(flow.source.error)}
    );
    const campaign::CampaignGraph& graph = flow.source.graph;
    // The company, read off the package. Three members: the two riders the
    // road begins with, and the ferryman the crossing brings in.
    const pr::CampaignDefinition authored = authored_company(package);
    expect(
        authored.members.size() == 3U,
        "the campaign says who plays it, and says it three times"
    );
    expect(
        authored.members[0].id == vanguard_key &&
            authored.members[0].name_in(package) == "Vanguard Rilla" &&
            authored.members[0].join_node_id == 0U &&
            authored.members[1].id == outrider_key &&
            authored.members[1].name_in(package) == "Outrider Bevan" &&
            authored.members[1].join_node_id == 0U,
        "the two the road begins with are named, in the order the author wrote "
        "them"
    );
    expect(
        authored.members[2].id == ferryman_key &&
            authored.members[2].name_in(package) == "Torvald the Ferryman" &&
            authored.members[2].unit_type_id == dawn_guard_unit &&
            authored.members[2].join_node_id == river_skirmish_node,
        "and the third is a recruit, authored at the node that brings him in"
    );
    campaign::CampaignState state = muster_roster(package, authored);
    expect(
        campaign::is_deployable(state, vanguard) &&
            campaign::is_deployable(state, outrider) &&
            campaign::find_unit(state, ferryman) == nullptr,
        "founding holds the two the campaign begins with and nobody else"
    );
    expect(
        campaign::begin_campaign(state, graph) == campaign::ProgressionError::none,
        "the campaign enters its authored flow"
    );

    // 2. The first encounter's board, joined to the roster. Both riders are
    //    deployable, so it is exactly the board the package holds.
    const cr::CampaignEncounter opening = cr::load_encounter_for_campaign(
        package, river_skirmish, state, assignments(authored)
    );
    expect(
        static_cast<bool>(opening),
        std::string_view{cr::roster_error_name(opening.error)}
    );
    expect(
        opening.encounter.definition.units.size() == 3U && opening.excluded.empty(),
        "a full roster fields everybody the crossing lists"
    );
    const campaign::BattleEntityId outrider_board = opening.binding.battle_of(outrider);
    const campaign::BattleEntityId vanguard_board = opening.binding.battle_of(vanguard);
    expect(
        outrider_board.value != 0U && vanguard_board.value != 0U,
        "and both riders are bound to who they are on it"
    );
    const std::uint64_t picket_board = core::stable_content_id_v1(
        "muster_road/river_skirmish/muster_picket"
    );
    expect(
        opening.binding.persistent_of(campaign::BattleEntityId{picket_board}).value == 0U,
        "while the watch on the far bank belongs to no campaign at all"
    );

    // 3. The battle, fought by the engine.
    //
    // Seven health a side, and a blow of three: four strength less one defence,
    // the same for everybody here. So the picket needs three swings to put the
    // outrider down, and the outrider's free counters put the picket on one
    // while it does it. The tonic is drunk in the middle of that, at four
    // health, which is a real restore rather than a draught poured on the
    // ground; it buys the rider a swing rather than a life, because three more
    // is still three. The vanguard then rides onto the tile its
    // companion is no longer standing on and finishes a picket that has spent
    // everything the counters left it. The picket's own tonic is what it
    // leaves on the road.
    //
    // That last pair is one turn, and is the point of the second action point:
    // the walk does not end the vanguard's turn, so riding up and striking is
    // one activation rather than an arrival and then a wait for permission.
    //
    // The crossing authors a deployment region, so the battle opens with the
    // riders to be stood rather than with an activation. This script arranges
    // nobody: it takes the line the content authored and opens the battle, so
    // every number below is the number it was before the region existed.
    const BattleResult battle = fight(
        opening.encounter.definition,
        {
            {sim::CommandType::begin_battle, 0, {}, 0},
            // The vanguard drinks its own, unwounded, before a blow has landed
            // on anybody. "A draught drunk at full health is a draught gone" is
            // the engine's own rule (`encounter.cpp`), and spending it for
            // nothing is exactly what makes the next map's proof worth
            // anything: whatever this rider carries onto the road, it will not
            // be this.
            {
                sim::CommandType::use_item, vanguard_board.value, {}, 0, 0, 0,
                field_tonic
            },
            {sim::CommandType::attack, picket_board, {}, outrider_board.value},
            {
                sim::CommandType::use_item, outrider_board.value, {}, 0, 0, 0,
                field_tonic
            },
            {sim::CommandType::attack, picket_board, {}, outrider_board.value},
            {sim::CommandType::wait, outrider_board.value, {}, 0},
            {sim::CommandType::attack, picket_board, {}, outrider_board.value},
            {sim::CommandType::move, vanguard_board.value, {2, 1}, 0},
            {sim::CommandType::attack, vanguard_board.value, {}, picket_board},
        }
    );
    expect(
        battle.outcome == sim::Outcome::first_side_won,
        "the crossing is won, and at a price"
    );
    expect(
        std::find(battle.fallen.begin(), battle.fallen.end(), outrider_board.value) !=
            battle.fallen.end(),
        "the outrider is among the fallen the simulation reports"
    );
    expect(
        std::find(battle.fallen.begin(), battle.fallen.end(), vanguard_board.value) ==
            battle.fallen.end(),
        "and the vanguard is not"
    );

    // 4. What the battle did, as campaign consequences, in the order a batch
    //    has to carry them: what the characters did while they were alive,
    //    then who did not come back, then the objectives, then who joined.
    //
    //    The order is load-bearing rather than tidy. A spend now comes out of
    //    the spender's own kit, and `apply_outcome` refuses every operation
    //    against a permanently dead member, so a rider who drinks their last
    //    draught and then falls has to be charged for it before they are
    //    buried, or the batch refuses itself. It is also the true sequence, and
    //    it is what leaves `record_permanent_death` returning to the store only
    //    what is actually left of a kit.
    //
    //    The deaths are derived from the snapshot and the binding rather than
    //    asserted: a board unit at zero health that a roster member was
    //    standing in is that member's death.
    std::vector<campaign::CampaignOutcomeOperation> deaths;
    for (const sim::UnitId fallen : battle.fallen) {
        const campaign::PersistentEntityId member =
            opening.binding.persistent_of(campaign::BattleEntityId{fallen});
        if (member.value != 0U) {
            deaths.push_back(campaign::record_permanent_death(member));
        }
    }
    expect(deaths.size() == 1U, "exactly one roster member did not come back");
    deaths.push_back(campaign::record_objective(
        ref(package, core::ContentCategory::objective, defeat_all_opponents),
        campaign::ObjectiveOutcome::satisfied
    ));

    // 4a2. And who the crossing brought in. The ferryman is authored onto this
    //      node, so he joins in this node's own batch: the same batch that
    //      buries the outrider, which is what makes a recruitment survive a
    //      save for exactly the reason a death does. He arrives with the kit
    //      his unit type says he arrives with, in that same batch and never
    //      again.
    const std::vector<campaign::CampaignOutcomeOperation> recruited =
        joining(package, authored, river_skirmish_node);
    expect(
        recruited.size() == 3U &&
            recruited[0].subject == ferryman &&
            recruited[0].kind ==
                campaign::OutcomeOperationKind::recruit_unit &&
            recruited[1].kind ==
                campaign::OutcomeOperationKind::set_availability,
        "the crossing recruits exactly the member authored onto it"
    );
    expect(
        recruited.size() == 3U &&
            recruited[2].kind == campaign::OutcomeOperationKind::add_item &&
            recruited[2].subject == ferryman &&
            recruited[2].definition.stable_id == field_tonic &&
            recruited[2].amount == 1,
        "and hands him the draught his type starts with, once, as he joins"
    );
    deaths.insert(deaths.end(), recruited.begin(), recruited.end());

    // 4b. What the battle did to the survivor's numbers. Derived from the very
    //     same events, by the layer that is allowed to read a package and a
    //     roster at once, and by nothing inside the battle, which is why the
    //     hash above is the hash it always was.
    const campaign::OutcomeSource source{
        ref(package, core::ContentCategory::encounter, river_skirmish),
        battle.hash,
        0U
    };
    const cr::BattleProgression growth = cr::derive_battle_progression(
        package, state, opening.encounter.definition, opening.binding,
        battle.events, source
    );
    expect(
        static_cast<bool>(growth),
        std::string_view{cr::progression_source_error_name(growth.error)}
    );
    // The picket fell to the vanguard's counterattack, so the sixty experience
    // the River Watch is authored to be worth went to the vanguard. Fifty a
    // level makes that one level exactly. The outrider, who the battle buried,
    // earns nothing at all.
    expect(
        growth.level_ups.size() == 1U &&
            growth.level_ups.front().member == vanguard &&
            growth.level_ups.front().from_level == 1U &&
            growth.level_ups.front().to_level == 2U,
        "the surviving rider reaches level two and the fallen one reaches "
        "nothing"
    );
    // What the level gave, pinned. Six chances are authored and none of them is
    // a hundred, so all six are rolled: six numbers off the growth stream,
    // seeded from this battle's own canonical hash. These two points are
    // what they said. The four the richer stat line appended are authored at
    // zero here, so they draw nothing and gain nothing, which is why the array
    // is ten long and its tail is empty. It is a golden in the same sense the
    // battle hash above is: not a number anybody chose, and one that must not
    // move without somebody meaning it. Anything that moves the hash it is
    // seeded from moves it too, and then it is re-derived rather than adjusted:
    // a re-seeded stream says what it says, and there is nothing to adjust it
    // towards.
    //
    // A deployment region is one thing that moves it not at all, and that is
    // the encoding working: the region leaves the canonical hash the moment the
    // battle begins, so a board arranged the way the content authored it seeds
    // exactly the stream it would have seeded with nothing to arrange.
    expect(
        growth.level_ups.size() == 1U &&
            growth.level_ups.front().points ==
                std::array<std::uint16_t, campaign::growable_stat_count>{
                    1U, 1U, 1U, 1U, 0U, 0U, 0U, 0U, 0U, 0U
                },
        "and the level gives exactly what the growth stream said: a point of "
        "health, of strength, of defence and of resistance, and nothing else"
    );
    // 4c. What the battle did to what the campaign owns, from the very same
    //     events. Both riders spent a draught, and both are in the batch
    //     because the acting unit is a roster member. Nothing here reads the
    //     board or a package record.
    //
    //     The picket left nothing, and that is a roll rather than a rule: it is
    //     authored to leave its tonic three times in five, the draw is taken
    //     off this battle's own seeded drop stream, and on this board it does
    //     not come up. Pinned as tightly as a drop would be, because a drop
    //     appearing here is exactly as much a change as one disappearing.
    //     `campaign_runtime_test` proves what a drop does when it lands, from
    //     events it states rather than rolls.
    expect(
        battle.drops.empty(),
        "the picket kept its tonic, the sixty in a hundred not having come up"
    );
    std::size_t used_events = 0;
    std::size_t dropped_events = 0;
    for (const sim::Event& event : battle.events) {
        if (event.type == sim::EventType::item_used) ++used_events;
        if (event.type == sim::EventType::item_dropped) ++dropped_events;
    }
    expect(
        used_events == 2U && dropped_events == 0U,
        "and the events say the same thing the snapshot does: two drunk, "
        "nothing left behind"
    );
    std::size_t added = 0;
    std::size_t consumed = 0;
    std::vector<campaign::PersistentEntityId> drinkers;
    for (const campaign::CampaignOutcomeOperation& operation :
         growth.operations) {
        if (operation.definition.stable_id != field_tonic) continue;
        if (operation.kind == campaign::OutcomeOperationKind::add_item) {
            ++added;
        }
        if (operation.kind == campaign::OutcomeOperationKind::consume_item) {
            ++consumed;
            drinkers.push_back(operation.subject);
            expect(
                operation.subject.value != 0U && operation.amount == 1,
                "what was drunk comes out of the hands that drank it, one at a "
                "time"
            );
        }
    }
    expect(
        added == 0U && consumed == 2U,
        "the battle's inventory becomes two consumptions and nothing else, "
        "because nothing fell to add"
    );
    expect(
        drinkers == std::vector<campaign::PersistentEntityId>{vanguard, outrider},
        "and each consumption names the rider who actually drank, in the order "
        "the battle recorded the draughts"
    );

    // The batch, assembled in the one order it can be assembled in.
    std::vector<campaign::CampaignOutcomeOperation> operations =
        growth.operations;
    operations.insert(operations.end(), deaths.begin(), deaths.end());
    const campaign::CampaignOutcomeBatch consequences =
        campaign::make_outcome_batch(source, operations);

    // 5. Commit, and let the graph move. One batch, one edge.
    const campaign::NodeCompletion completion =
        campaign::complete_node(state, graph, consequences);
    expect(
        static_cast<bool>(completion) && completion.advanced,
        "the completed node commits its consequences and the campaign moves"
    );
    expect(
        completion.target == cr::campaign_node_ref(
            package.game_id, core::stable_content_id_v1("road_watch")
        ),
        "to the node the author put after the crossing"
    );
    expect(
        !campaign::is_deployable(state, outrider),
        "and the outrider can no longer take the field"
    );
    expect(
        campaign::is_deployable(state, ferryman),
        "while the ferryman, who was nobody before this battle, can"
    );
    // Where every draught in this battle ended up. The two riders drank the two
    // the founding put in their hands, so both hands are empty; the picket kept
    // its own; and the ferryman arrived with his and has not touched it.
    //
    // The distinction worth asserting is that both consumptions were charged to
    // the riders who drank rather than to the company, so the store is not
    // "level again". It is empty, and it is empty because this battle gave the
    // company nothing at all.
    const campaign::DefinitionRef tonic =
        ref(package, core::ContentCategory::item, field_tonic);
    expect(
        campaign::item_quantity(state, {}, tonic) == 0U,
        "the store holds nothing: neither what the riders drank nor a drop that "
        "was not rolled"
    );
    expect(
        campaign::item_quantity(state, vanguard, tonic) == 0U,
        "the surviving rider's hands are empty, because they drank what was in "
        "them"
    );
    expect(
        campaign::item_quantity(state, ferryman, tonic) == 1U,
        "while the ferryman arrived carrying his own"
    );
    const campaign::PersistentUnit* const buried =
        campaign::find_unit(state, outrider);
    expect(
        buried != nullptr && buried->carried.empty(),
        "and the rider the crossing buried left nothing behind, having drunk "
        "it first — which is exactly why the spend is committed before the "
        "death"
    );
    {
        // The counter-proof, in the shape the rule gives it. A character can
        // only spend what the campaign put in their hands, so the batch a
        // campaign refuses is not one that spends out of a store it never
        // stocked. It is one whose members were never given anything.
        campaign::CampaignState unstocked;
        std::vector<campaign::CampaignOutcomeOperation> bare;
        for (const campaign::CampaignOutcomeOperation& operation :
             joining(package, authored, 0U)) {
            if (operation.kind != campaign::OutcomeOperationKind::add_item) {
                bare.push_back(operation);
            }
        }
        expect(
            static_cast<bool>(campaign::apply_outcome(
                unstocked,
                campaign::make_outcome_batch(
                    {ref(package, core::ContentCategory::encounter,
                         river_skirmish),
                     2U, 0U},
                    bare
                )
            )),
            "a campaign founded without stocking anybody founds anyway"
        );
        expect(
            campaign::begin_campaign(unstocked, graph) ==
                campaign::ProgressionError::none,
            "and starts the same way"
        );
        const campaign::OutcomeApplication refused =
            campaign::apply_outcome(unstocked, consequences);
        expect(
            refused.error == campaign::OutcomeError::insufficient_items,
            "and a rider drinking a draught nobody ever handed them is refused "
            "whole, which is what makes the founding above load-bearing"
        );
        expect(
            campaign::item_quantity(unstocked, {}, tonic) == 0U &&
                campaign::is_deployable(unstocked, outrider),
            "and refused atomically: no death, no level, no inventory"
        );
    }

    // 6. Save, through the envelope, into a device.
    campaign::SavePackageRequirement requirement;
    requirement.package = package.game_id;
    requirement.content_revision = package.content_revision;
    const campaign::CampaignSave saved =
        campaign::make_campaign_save(state, {requirement});
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(saved);
    storage::MemorySlotStorage device;
    expect(
        device.write("muster-road", bytes) == storage::StorageError::none,
        "the campaign is written to a storage slot as opaque bytes"
    );

    // 7. Load it back, through the migration pipeline a real load goes through.
    const storage::StorageRead read = device.read("muster-road");
    expect(
        static_cast<bool>(read) && read.bytes == bytes,
        "and read back byte for byte"
    );
    campaign::MountedContent mounted;
    campaign::MountedPackage present;
    present.package = package.game_id;
    present.content_revision = package.content_revision;
    mounted.mount(present);
    const campaign::MigratedLoad restored = campaign::load_campaign_migrated(
        read.bytes,
        campaign::SaveLoadOptions{},
        campaign::standard_save_migrations(),
        mounted
    );
    expect(
        static_cast<bool>(restored),
        std::string_view{campaign::migration_error_name(restored.migration.error)}
    );
    expect(
        static_cast<bool>(restored.load),
        std::string_view{campaign::save_error_name(restored.load.error)}
    );
    expect(
        restored.migration.applied.empty(),
        "a save this build wrote needs no migration, and none runs"
    );
    const campaign::CampaignState& resumed = restored.save.state;
    expect(
        resumed.progress.active &&
            resumed.progress.active_node ==
                cr::campaign_node_ref(
                    package.game_id, core::stable_content_id_v1("road_watch")
                ),
        "the resumed campaign stands where the graph left it"
    );
    expect(
        resumed.progress.history.size() == 2U,
        "remembering the route it walked and not only where it ended"
    );
    expect(
        cr::encounter_of_node(authored, resumed.progress.active_node) ==
            road_watch,
        "and that node is fought at the next encounter"
    );

    const campaign::PersistentUnit* const fallen =
        campaign::find_unit(resumed, outrider);
    expect(
        fallen != nullptr && fallen->availability == campaign::Availability::dead,
        "the outrider came back from the card exactly as dead as they went in"
    );
    expect(
        campaign::is_deployable(resumed, vanguard),
        "and the vanguard exactly as alive"
    );
    const campaign::PersistentUnit* const survivor =
        campaign::find_unit(resumed, vanguard);
    expect(
        survivor != nullptr && survivor->progression.level == 2U &&
            survivor->progression.experience == 60U,
        "and exactly as levelled: a save carries what a battle taught them"
    );
    expect(
        survivor != nullptr &&
            survivor->progression.gained ==
                std::array<std::uint16_t, campaign::growable_stat_count>{
                    1U, 1U, 1U, 1U, 0U, 0U, 0U, 0U, 0U, 0U
                },
        "with the four points the roll granted, and not the six the author "
        "hoped for"
    );
    // And the inventory, both owners of it, which needed no schema bump to
    // survive the save: a kit is a list of quantities of item identities the
    // roster section already knew how to hold, so the campaign section's format
    // is the format it was and no migration ran (asserted above).
    expect(
        campaign::item_quantity(resumed, {}, tonic) == 0U,
        "the store comes back holding what it held: nothing, the crossing "
        "having given the company nothing"
    );
    expect(
        campaign::item_quantity(resumed, vanguard, tonic) == 0U,
        "the surviving rider comes back with the empty hands they finished the "
        "crossing with"
    );
    expect(
        campaign::item_quantity(resumed, ferryman, tonic) == 1U,
        "and the ferryman with the draught he joined carrying"
    );
    const campaign::PersistentUnit* const emptied =
        campaign::find_unit(resumed, vanguard);
    expect(
        emptied != nullptr && emptied->carried.empty(),
        "held as an absence rather than as a stack of nothing, which is the "
        "one spelling `validate` accepts"
    );

    // 8. The next map lists three riders and a picket. The roster lists the
    //    survivor and the recruit.
    const cr::CampaignEncounter later = cr::load_encounter_for_campaign(
        package, road_watch, resumed, assignments(authored)
    );
    expect(
        static_cast<bool>(later),
        std::string_view{cr::roster_error_name(later.error)}
    );
    expect(
        later.excluded.size() == 1U && later.excluded.front() == outrider,
        "the later board leaves the fallen rider off, and says so"
    );
    expect(
        later.encounter.definition.units.size() == 3U,
        "and fields one fewer than the four the package alone would have"
    );
    // The proof the whole recruitment is for: somebody the campaign did not
    // hold when it started, who joined at an authored node, came back out of a
    // save and is standing on the next board.
    const campaign::PersistentUnit* const joined =
        campaign::find_unit(resumed, ferryman);
    expect(
        joined != nullptr &&
            joined->availability == campaign::Availability::available,
        "the ferryman came back out of the save as a member of the company"
    );
    expect(
        later.binding.battle_of(ferryman).value != 0U,
        "and takes the field on the road, which he could not have done before "
        "the crossing recruited him"
    );
    const std::uint64_t outrider_on_the_road = core::stable_content_id_v1(
        "muster_road/road_watch/muster_outrider"
    );
    const std::vector<sim::UnitDefinition>& units = later.encounter.definition.units;
    expect(
        std::none_of(
            units.begin(),
            units.end(),
            [](const sim::UnitDefinition& unit) {
                return unit.id == outrider_on_the_road;
            }
        ),
        "the fallen rider is not standing on the road, however plainly it "
        "lists them"
    );
    expect(
        later.binding.battle_of(outrider).value == 0U &&
            later.binding.battle_of(vanguard).value != 0U,
        "and only the survivor is bound to anybody"
    );

    // 9. And the survivor takes the field as the character they became. The
    //    authored Dawn Guard has seven health, four strength, one defence, no
    //    resistance and three movement; the rider who crossed the river has
    //    eight health, five strength, two defence and a point of resistance,
    //    because the roster adds exactly what the level-up granted on the way
    //    to the board and the roll granted a point of each of the four.
    const campaign::BattleEntityId vanguard_on_the_road =
        later.binding.battle_of(vanguard);
    const sim::UnitDefinition* grown = nullptr;
    for (const sim::UnitDefinition& unit : later.encounter.definition.units) {
        if (unit.id == vanguard_on_the_road.value) {
            grown = &unit;
        }
    }
    expect(
        grown != nullptr && grown->health == 8 && grown->strength == 5 &&
            grown->defense == 2 && grown->resistance == 1 &&
            grown->movement == 3,
        "the survivor stands on the second map with the points the first one "
        "earned them"
    );
    expect(
        grown != nullptr && grown->action_points == 2 && grown->skill == 0 &&
            grown->luck == 0 && grown->evasion == 0 && grown->magic == 0,
        "and with the stats the roll did not touch — including all four the "
        "richer stat line added, which this content never authors — exactly as "
        "the author wrote them"
    );

    // 9a. And with the satchel the campaign holds for them, which is where the
    //     truth about a spent draught lives. The rider drank their draught at
    //     the crossing; the Dawn Guard unit type still lists one, in the same
    //     words it always did; and the rider rides onto the road with nothing.
    //     A draught spent is a draught gone, and the map that lists it cannot
    //     conjure it back.
    expect(
        grown != nullptr && grown->item_ids.empty() &&
            grown->item_counts.empty(),
        "the rider who drank at the crossing rides onto the road carrying "
        "nothing, however plainly their unit type lists a draught"
    );
    const campaign::BattleEntityId ferryman_on_the_road =
        later.binding.battle_of(ferryman);
    const sim::UnitDefinition* joined_board = nullptr;
    for (const sim::UnitDefinition& unit : later.encounter.definition.units) {
        if (unit.id == ferryman_on_the_road.value) {
            joined_board = &unit;
        }
    }
    expect(
        joined_board != nullptr &&
            joined_board->item_ids ==
                std::vector<sim::ContentId>{field_tonic} &&
            joined_board->item_counts == std::vector<std::uint16_t>{1},
        "while the ferryman, who joined after it and has drunk nothing, rides "
        "with the one his type gave him"
    );
    const std::uint64_t picket_on_the_road = core::stable_content_id_v1(
        "muster_road/road_watch/muster_picket"
    );
    const sim::UnitDefinition* watch = nullptr;
    for (const sim::UnitDefinition& unit : later.encounter.definition.units) {
        if (unit.id == picket_on_the_road) watch = &unit;
    }
    expect(
        watch != nullptr && watch->item_ids.empty(),
        "and the watch on the far bank, which belongs to no campaign, carries "
        "exactly what its type lists"
    );

    // The board the package alone would have made still has all four, and the
    // Dawn Guard on it is the Dawn Guard the author wrote. Nothing about the
    // content changed; what changed is who the campaign can send, and what the
    // campaign has made of them.
    const pr::EncounterLoadResult plain = pr::load_encounter(package, road_watch);
    expect(
        static_cast<bool>(plain) && plain.definition.units.size() == 4U,
        "the authored road is the authored road, with nobody removed from it"
    );
    const sim::UnitDefinition* ungrown = nullptr;
    for (const sim::UnitDefinition& unit : plain.definition.units) {
        if (unit.id == vanguard_on_the_road.value) {
            ungrown = &unit;
        }
    }
    expect(
        ungrown != nullptr && ungrown->health == 7 &&
            ungrown->strength == 4 && ungrown->defense == 1,
        "and its riders are the authored seven, four and one: growth is a "
        "campaign's, and a package with no campaign attached loads exactly the "
        "board it always loaded"
    );
}

}  // namespace

int main() {
    the_demo_carries_a_death_through_a_save_into_the_next_map();
    return failures == 0 ? 0 : 1;
}
