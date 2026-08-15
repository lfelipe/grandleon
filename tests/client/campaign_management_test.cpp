// SPDX-License-Identifier: MIT
#include <grandleon/campaign/migration.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/storage/memory_storage.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// The company, managed between battles, in the maintained demo and end to end.
//
// Three verbs meet on one screen: an aftermath that acts, a way to move a thing
// between the store and a character's hands, and a choice about who takes the
// field. This is that screen's engine side, driven exactly as a client drives
// it: `client::CampaignSession`, a real compiled package, a real device. The
// claim it has to earn is:
//
//   *A tonic that fell on a battlefield can be drunk in the next one, a member
//   the player leaves behind is not on the board, and a campaign saved in the
//   middle of deciding resumes in the middle of deciding.*
//
// Nothing here is an engine rule of its own and the test says so by using none:
// a move is a `consume_item` and an `add_item` in one batch, benching is
// `set_availability(member, retired)`, and every refusal asserted below is a
// name `engine/campaign` has.

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace cr = grandleon::campaign_runtime;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
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

// The company the demo authors, and the identities founding assigns, one-based
// in the order the campaign lists them. The same rule the session founds by.
constexpr campaign::PersistentEntityId vanguard{1};
constexpr campaign::PersistentEntityId outrider{2};
constexpr campaign::PersistentEntityId ferryman{3};

const std::uint64_t muster_road = core::stable_content_id_v1("muster_road");
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
    expect(static_cast<bool>(parsed), "and maps natively");
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and loads");
    return loaded.package;
}

campaign::DefinitionRef tonic_of(const pf::LoadedPackage& package) {
    return {package.game_id, core::ContentCategory::item, field_tonic};
}

// What the slot holds, read back through the pipeline a real resume goes
// through. Every claim about "the campaign was saved when the gesture was made"
// is checked against this rather than against the session's own memory.
campaign::CampaignState state_in_slot(
    const pf::LoadedPackage& package,
    storage::SlotStorage& device,
    const std::string& slot
) {
    const storage::StorageRead read = device.read(slot);
    expect(static_cast<bool>(read), "the slot reads");
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
    expect(static_cast<bool>(restored), "and is a campaign this build reads");
    return restored.save.state;
}

// How much of the tonic one owner holds, out of the slot. Owner zero is the
// company's store.
std::uint32_t held_in_slot(
    const pf::LoadedPackage& package,
    storage::SlotStorage& device,
    const std::string& slot,
    campaign::PersistentEntityId owner
) {
    return campaign::item_quantity(
        state_in_slot(package, device, slot), owner, tonic_of(package)
    );
}

// The crossing, fought by the engine, with the outrider left on the field.
//
// Nearly the script `tests/campaign_runtime/demo_permadeath_test.cpp` fights,
// and for the same reason: it is the shortest way to a campaign that has a
// store worth managing. The one difference is deliberate and is spelled out at
// the line that makes it: this screen needs the store stocked by a rule rather
// than by a roll.
client::BattleReport fight_the_crossing(
    const cr::CampaignEncounter& board,
    const pf::LoadedPackage& package
) {
    client::BattleReport report;
    sim::Encounter::CreateResult created =
        sim::create_encounter(board.encounter.definition);
    expect(static_cast<bool>(created), "the crossing is a board a battle starts from");
    if (!created) return report;
    const std::uint64_t rider = board.binding.battle_of(outrider).value;
    const std::uint64_t lead = board.binding.battle_of(vanguard).value;
    const std::uint64_t picket = core::stable_content_id_v1(
        "muster_road/river_skirmish/muster_picket"
    );
    (void)package;
    const std::vector<sim::Command> script{
        {sim::CommandType::begin_battle, 0, {}, 0},
        {sim::CommandType::attack, rider, {}, picket},
        {sim::CommandType::attack, picket, {}, rider},
        // The outrider keeps its draught and dies holding it, which is what
        // stocks the store this screen is about. A burial returns what is left
        // of a kit by rule, so the tonic the management stage below hands
        // around is there for a reason a die cannot take away, unlike the
        // picket's own drop, which is authored at three times in five and is
        // whatever this battle's seeded stream says it is. The turn it would
        // have spent drinking it spends standing instead.
        {sim::CommandType::wait, rider, {}, 0},
        {sim::CommandType::attack, picket, {}, rider},
        // One activation, not two: the walk leaves the lead rider a second
        // action point, so riding onto the tile the outrider fell from and
        // finishing the picket is a single turn.
        {sim::CommandType::move, lead, {2, 1}, 0},
        {sim::CommandType::attack, lead, {}, picket},
    };
    for (std::size_t index = 0; index < script.size(); ++index) {
        const sim::CommandResult applied = created.encounter.apply(script[index]);
        report.events.insert(
            report.events.end(), applied.events.begin(), applied.events.end()
        );
        if (!applied) {
            std::cerr << "FAIL: crossing command " << index << " refused: "
                      << sim::error_name(applied.error) << '\n';
            ++failures;
            return report;
        }
    }
    report.final_snapshot = created.encounter.snapshot();
    report.canonical_hash = created.encounter.canonical_hash();
    report.outcome = report.final_snapshot.outcome;
    for (const sim::ObjectiveResult& objective : report.final_snapshot.objectives) {
        report.objectives.push_back(objective);
    }
    return report;
}

// ---------------------------------------------------------------------------

void the_company_is_managed_between_battles() {
    const pf::LoadedPackage package = compile_the_maintained_demo();
    if (failures != 0) return;
    const campaign::DefinitionRef tonic = tonic_of(package);

    storage::MemorySlotStorage device;
    const client::PackageBoards boards{package};
    client::CampaignSessionOptions options;
    options.slot = "management";
    client::CampaignSession session{
        package, muster_road, boards, device, options
    };
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        session.begin(failure, refused, resumed) ==
            client::CampaignSessionError::none,
        "the campaign founds from the company the author wrote"
    );

    // 1. The stage stands before the first board, because it stands before
    //    every board. There is a company, so there is something to arrange.
    const client::CompanyManagement opening = session.management();
    expect(
        opening.error == client::CampaignSessionError::none &&
            opening.roster.size() == 2U && opening.store.empty(),
        "a founded company owns nothing beyond what its two members carry"
    );
    expect(
        opening.placeable ==
            std::vector<campaign::PersistentEntityId>{vanguard, outrider},
        "and the crossing has a place for exactly the two who are on it — not "
        "for the ferryman it recruits, whom it never places"
    );

    // 2. A store that cannot pay refuses by the campaign's own name, and
    //    refuses the whole batch: nothing is added anywhere.
    const client::ManagementCommit unpaid = session.give_item(vanguard, tonic);
    expect(
        !unpaid && unpaid.application.error ==
                       campaign::OutcomeError::insufficient_items,
        "a draught the store does not hold cannot be handed to anybody"
    );
    expect(
        unpaid.application.operation_index == 0U,
        "and the refusal names the consume, because the thing is taken out of "
        "the store before it is put into a hand"
    );
    expect(
        !unpaid.saved && session.store().empty() &&
            session.roster().front().carried.size() == 1U,
        "nothing moved and nothing was written"
    );

    // 3. A move that can be paid for. One batch, two operations, committed and
    //    written to the slot in the same step. That is what leaves the stage
    //    with nothing pending.
    const client::ManagementCommit put_by = session.take_item(vanguard, tonic);
    expect(static_cast<bool>(put_by), "the vanguard's own draught goes into the store");
    expect(
        put_by.batch.operations.size() == 2U &&
            put_by.batch.operations[0].kind ==
                campaign::OutcomeOperationKind::consume_item &&
            put_by.batch.operations[0].subject == vanguard &&
            put_by.batch.operations[1].kind ==
                campaign::OutcomeOperationKind::add_item &&
            put_by.batch.operations[1].subject.value == 0U,
        "as a consume against the hands it left and an add against the store"
    );
    expect(
        put_by.saved && put_by.save == storage::StorageError::none,
        "and the slot was written because the gesture committed"
    );
    expect(
        held_in_slot(package, device, "management", {}) == 1U &&
            held_in_slot(package, device, "management", vanguard) == 0U,
        "the slot holds the move, not the intention to make it"
    );

    // 3a. The identity of a management batch, stated rather than assumed. The
    //     source is where the company is standing, a zero battle hash because
    //     no battle produced it, and the count of outcomes the campaign had
    //     already committed.
    const campaign::CampaignState after_move =
        state_in_slot(package, device, "management");
    expect(
        put_by.batch.id ==
            campaign::derive_outcome_id(
                {opening.node,
                 0U,
                 static_cast<std::uint64_t>(
                     after_move.applied_outcomes.size() - 1U
                 )},
                put_by.batch.operations
            ),
        "which is exactly the identity the batch carries"
    );
    expect(
        put_by.batch.id !=
            campaign::derive_outcome_id(
                {campaign::DefinitionRef{
                     package.game_id,
                     core::ContentCategory::encounter,
                     opening.encounter_id
                 },
                 0U,
                 static_cast<std::uint64_t>(
                     after_move.applied_outcomes.size() - 1U
                 )},
                put_by.batch.operations
            ),
        "and never the identity a batch fought at this node's encounter would "
        "carry, because the category of the reference is part of the id"
    );

    // 3b. A replay of the very batch that was committed does nothing further.
    //     Checked against the campaign the slot holds, which is the campaign a
    //     crashed-and-resumed client would be holding.
    campaign::CampaignState replayed = after_move;
    const campaign::OutcomeApplication again =
        campaign::apply_outcome(replayed, put_by.batch);
    expect(
        static_cast<bool>(again) && again.already_applied,
        "the same gesture against the same campaign is one move, not two"
    );
    expect(
        campaign::item_quantity(replayed, {}, tonic) == 1U,
        "so the store still holds one"
    );

    // 3c. And the same gesture made again, on purpose, is a different batch and
    //     a second move, because the count of committed outcomes moved.
    const client::ManagementCommit put_back = session.give_item(vanguard, tonic);
    expect(static_cast<bool>(put_back), "the draught comes back out of the store");
    const client::ManagementCommit and_again = session.take_item(vanguard, tonic);
    const client::ManagementCommit and_once_more =
        session.give_item(vanguard, tonic);
    expect(
        static_cast<bool>(and_again) && static_cast<bool>(and_once_more) &&
            and_again.batch.id != put_by.batch.id &&
            and_once_more.batch.id != put_back.batch.id,
        "two identical gestures are two batches rather than one committed twice"
    );
    expect(
        session.store().empty() &&
            session.roster().front().carried.size() == 1U,
        "and the company is back where it started, having really been moved "
        "four times"
    );

    // 4. Somebody the campaign never met cannot be given anything either. The
    //    store really can pay for this one, so the refusal below is about
    //    who was named and not about what was owed.
    expect(
        static_cast<bool>(session.take_item(vanguard, tonic)),
        "a draught is put in the store for the attempt to pay with"
    );
    const client::ManagementCommit stranger =
        session.give_item(campaign::PersistentEntityId{99}, tonic);
    expect(
        !stranger &&
            stranger.application.error == campaign::OutcomeError::unknown_unit,
        "a member the campaign does not hold is refused by name"
    );
    expect(
        session.store().size() == 1U,
        "and the store still holds what it would have paid with, because the "
        "batch was refused whole"
    );
    expect(
        static_cast<bool>(session.give_item(vanguard, tonic)),
        "so the draught goes back to the rider it came from"
    );

    // 5. The crossing, taken with the company as it stands. That is the
    //    company the author wrote, so this is the battle the demo always fought.
    const client::CampaignSession::PreparedBoard crossing =
        session.prepare_board();
    expect(
        crossing.error == client::CampaignSessionError::none,
        "the board publishes for a company nobody narrowed"
    );
    if (crossing.error != client::CampaignSessionError::none) return;
    const client::BattleReport battle = fight_the_crossing(crossing.encounter, package);
    expect(
        battle.outcome == sim::Outcome::first_side_won,
        "the crossing is won, and at a price"
    );
    client::BattleAftermath aftermath;
    expect(
        session.commit_battle(battle, aftermath) ==
            client::CampaignSessionError::none,
        "and its consequences commit"
    );
    expect(
        aftermath.fallen.size() == 1U && aftermath.fallen.front() == outrider,
        "the outrider did not come back"
    );
    expect(
        aftermath.binding.persistent_of(
            aftermath.binding.battle_of(vanguard)
        ) == vanguard,
        "and the aftermath carries the board's join, which the session keeps "
        "in place of the board"
    );
    // The board the session kept is spent. It keeps the join and the board's
    // unit types rather than the whole encounter, so this is the assertion that
    // says the readiness flag still means what it meant.
    client::BattleAftermath twice;
    expect(
        session.commit_battle(battle, twice) ==
            client::CampaignSessionError::board_rejected,
        "committing the same battle again is refused rather than counted twice"
    );
    expect(
        session.save() == storage::StorageError::none,
        "and the campaign is written, as it is after every battle"
    );

    // 6. The road's management stage. The company now owns something: the
    //    draught the outrider was still carrying when it fell, returned to the
    //    store by the burial, and nothing else: the vanguard drank its own and
    //    the picket kept its. This is the tonic in the store that nobody could
    //    drink.
    const client::CompanyManagement road = session.management();
    expect(
        road.store.size() == 1U && road.store.front().quantity == 1U &&
            road.store.front().item.stable_id == field_tonic,
        "the company owns the one draught the crossing buried a rider holding"
    );
    expect(
        road.roster.size() == 3U,
        "and holds three members: two riders and the ferryman the crossing "
        "brought in"
    );
    expect(
        road.placeable ==
            std::vector<campaign::PersistentEntityId>{
                vanguard, outrider, ferryman
            },
        "all three of whom the road has a place for"
    );

    // 6a. Nothing is given to the fallen. This is the refusal the whole
    //     permanence rule exists for, reached by a player rather than by a bug.
    const client::ManagementCommit to_the_dead =
        session.give_item(outrider, tonic);
    expect(
        !to_the_dead &&
            to_the_dead.application.error == campaign::OutcomeError::unit_is_dead,
        "a draught cannot be put in the hands of somebody the campaign buried"
    );

    // 6b. And the move the whole screen is for: the thing that fell on the
    //     battlefield goes into the hands of somebody who will fight with it.
    const client::ManagementCommit armed = session.give_item(vanguard, tonic);
    expect(
        static_cast<bool>(armed),
        "the vanguard is handed what the outrider left"
    );
    // Two, because the vanguard never drank its own: what the outrider left
    // goes on top of the draught the founding put in this rider's hands.
    expect(
        held_in_slot(package, device, "management", vanguard) == 2U &&
            held_in_slot(package, device, "management", {}) == 0U,
        "and the slot says so, written when the gesture was made"
    );

    // 6c. And the choice about who goes. The ferryman sits this one out.
    const client::ManagementCommit benched = session.set_fielded(ferryman, false);
    expect(static_cast<bool>(benched), "the ferryman is asked to stay behind");
    expect(
        benched.batch.operations.size() == 1U &&
            benched.batch.operations.front().kind ==
                campaign::OutcomeOperationKind::set_availability &&
            benched.batch.operations.front().selector ==
                static_cast<std::uint8_t>(campaign::Availability::retired),
        "as one availability operation and no new vocabulary at all"
    );

    // 7. And now the campaign is put down in the middle of deciding, and picked
    //    up again. A resumed campaign lands on the management stage, holding
    //    everything the player had already done.
    client::CampaignSessionOptions resuming = options;
    resuming.resume = true;
    client::CampaignSession second{
        package, muster_road, boards, device, resuming
    };
    client::SlotFailure second_failure;
    bool second_refused = false;
    bool second_resumed = false;
    expect(
        second.begin(second_failure, second_refused, second_resumed) ==
                client::CampaignSessionError::none &&
            second_resumed && !second_refused,
        "the campaign is picked up from the slot"
    );
    const client::CompanyManagement carried_over = second.management();
    expect(
        carried_over.node == road.node && carried_over.store.empty(),
        "standing where it was, with the store the player emptied into a hand"
    );
    const auto member_of =
        [&carried_over](campaign::PersistentEntityId who)
        -> const client::RosterEntry* {
        for (const client::RosterEntry& entry : carried_over.roster) {
            if (entry.member == who) return &entry;
        }
        return nullptr;
    };
    expect(
        member_of(vanguard) != nullptr &&
            member_of(vanguard)->carried.size() == 1U &&
            member_of(vanguard)->carried.front().quantity == 2U,
        "the rider carrying the draught the player gave them, on top of their "
        "own"
    );
    expect(
        member_of(ferryman) != nullptr &&
            member_of(ferryman)->availability == campaign::Availability::retired,
        "and the ferryman still sitting it out"
    );

    // 8. A line with nobody in it is refused, and the campaign stands where it
    //    stood. Benching is now an ordinary mistake, so it has to be one.
    const client::ManagementCommit alone = second.set_fielded(vanguard, false);
    expect(static_cast<bool>(alone), "the last rider is asked to stay behind too");
    const client::CampaignSession::PreparedBoard nobody = second.prepare_board();
    expect(
        nobody.error == client::CampaignSessionError::board_rejected &&
            nobody.roster_error == cr::RosterError::side_emptied,
        "and the roster refuses the board by its own name"
    );
    expect(
        second.management().node == road.node,
        "nothing was committed and the company is still standing at the same "
        "node, which is what lets the player change their mind"
    );
    expect(
        static_cast<bool>(second.set_fielded(vanguard, true)),
        "so the rider is fielded again"
    );

    // 9. The board, at last. Two of the three members are off it, one because
    //    the crossing buried them and one because the player said so. The
    //    roster reports both the same way, which is the whole seam this test
    //    stands on.
    const client::CampaignSession::PreparedBoard published = second.prepare_board();
    expect(
        published.error == client::CampaignSessionError::none,
        "the road publishes for the company the player chose"
    );
    if (published.error != client::CampaignSessionError::none) return;
    std::vector<campaign::PersistentEntityId> excluded = published.board.excluded;
    std::sort(excluded.begin(), excluded.end());
    expect(
        excluded ==
            std::vector<campaign::PersistentEntityId>{outrider, ferryman},
        "the fallen rider and the benched one are both left off, and the board "
        "cannot tell which is which"
    );
    expect(
        published.encounter.encounter.definition.units.size() == 2U,
        "so the road fields two of the four it lists"
    );
    expect(
        published.board.binding.battle_of(ferryman).value == 0U &&
            published.board.binding.battle_of(vanguard).value != 0U,
        "and only the rider who went is bound to anybody"
    );

    // 9a. Carrying the draught the crossing left the company. This is what
    //     stocking a satchel from the campaign buys: a tonic in the store is a
    //     tonic somebody can drink.
    const campaign::BattleEntityId on_the_road =
        published.board.binding.battle_of(vanguard);
    const sim::UnitDefinition* armed_unit = nullptr;
    for (const sim::UnitDefinition& unit :
         published.encounter.encounter.definition.units) {
        if (unit.id == on_the_road.value) armed_unit = &unit;
    }
    expect(
        armed_unit != nullptr &&
            armed_unit->item_ids == std::vector<sim::ContentId>{field_tonic} &&
            armed_unit->item_counts == std::vector<std::uint16_t>{2},
        "the rider who kept their own at the crossing rides onto the road with "
        "the one the outrider left, because the player put it in their hand"
    );
}

}  // namespace

int main() {
    the_company_is_managed_between_battles();
    return failures == 0 ? 0 : 1;
}
