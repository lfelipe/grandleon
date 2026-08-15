// SPDX-License-Identifier: MIT
#include <grandleon/campaign/migration.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/campaign_runtime/campaign_runtime.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/storage/memory_storage.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// What a campaign is given, driven exactly as a client drives it.
//
// Two authored fields land here: `campaign.startingStore` and
// `campaignNode.grants`. The claim they have to earn is that neither is a new
// rule. Both reach the campaign as ordinary `campaign::add_item` operations
// against the shared store, inside batches the outcome layer already commits,
// so all three of the guarantees the satchel earned come free: a stocking is
// atomic with the founding or the transition that caused it, a retry is
// recognised as a retry rather than paid twice, and a save carries the result
// with nothing new to teach the save format.
//
// The road below loops on purpose. A grant is an *occurrence* and not an
// assertion about how much the store should hold, so a route that passes the
// abbey twice is blessed twice, while the very same batch offered a second
// time is one blessing, not two. The whole distinction is the sequence a caller
// derives at the moment it builds a batch, and both halves of it are asserted
// here rather than argued.

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace cr = grandleon::campaign_runtime;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace storage = grandleon::storage;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr std::uint64_t tonic = 50;
constexpr std::uint64_t rope = 51;
constexpr std::uint64_t rider_type = 60;
constexpr std::uint64_t road_campaign = 110;
constexpr std::uint64_t abbey_node = 111;
constexpr std::uint64_t crossroads_node = 112;

// A road that loops: the abbey gives, the crossroads gives nothing, and the
// crossroads leads back to the abbey. Two story nodes are all a cycle needs,
// and a cycle is the shape a grant has to be right about.
gc::GameSource road_source() {
    gc::GameSource value;
    value.game_id[0] = 0x53U;
    value.title = "Store slice";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.item_types = {{20, "Consumable"}};
    value.classes = {{30, "Rider class", {6, 4, 1, 2, 3}, {10}}};
    value.weapons = {{40, "Sword", 10, 3, 1, 1}};
    value.items = {{tonic, "Tonic", 20, 5}, {rope, "Rope", 20, 1}};
    // The rider's unit type lists nothing, so every item the campaign ever
    // holds came out of an authored grant and out of nothing else.
    value.unit_types = {{rider_type, "Rider", 30, 80, {40}, {}}};
    value.factions = {{80, "Blue"}};
    value.campaigns = {
        {
            road_campaign,
            "The road",
            abbey_node,
            {
                {abbey_node, gc::CampaignNodeKind::story, 0, {},
                 {crossroads_node}, {}},
                {crossroads_node, gc::CampaignNodeKind::story, 0, {},
                 {abbey_node}, {}},
            },
            {{2000, "Rider", rider_type, 0}},
            // Three draughts and a rope to begin with, and two draughts more
            // every time the road passes the abbey.
            {{tonic, 3, 0}, {rope, 1, 0}, {tonic, 2, abbey_node}},
        },
    };
    return value;
}

pf::LoadedPackage compile_and_load() {
    const auto compiled = gc::compile(road_source());
    expect(static_cast<bool>(compiled), "the road compiles");
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

campaign::DefinitionRef item_of(
    const pf::LoadedPackage& package,
    std::uint64_t id
) {
    return {package.game_id, core::ContentCategory::item, id};
}

// What the slot holds, read back through the pipeline a real resume goes
// through, so every claim about what was committed is checked against the
// campaign a crashed-and-resumed client would be holding.
campaign::MigratedLoad slot_contents(
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
    return campaign::load_campaign_migrated(
        read.bytes,
        campaign::SaveLoadOptions{},
        campaign::standard_save_migrations(),
        mounted
    );
}

campaign::CampaignState state_in_slot(
    const pf::LoadedPackage& package,
    storage::SlotStorage& device,
    const std::string& slot
) {
    const campaign::MigratedLoad restored =
        slot_contents(package, device, slot);
    expect(
        static_cast<bool>(restored),
        std::string_view{campaign::migration_error_name(restored.migration.error)}
    );
    return restored.save.state;
}

// The founding batch, rebuilt out of the same published functions the session
// builds it from: the company the campaign authors, then what its store is
// founded with, under the campaign's own reference at sequence zero.
//
// Rebuilt rather than captured, on purpose. If the session ever composed its
// founding differently (a different order, a different source, a grant folded
// into the recruitment), this batch would carry a different identity and the
// retry below would be paid a second time instead of being recognised. So the
// assertion that a retry changes nothing is also the assertion that this is the
// batch the client actually derived.
campaign::CampaignOutcomeBatch founding_batch(
    const pf::LoadedPackage& package,
    const pr::CampaignDefinition& authored
) {
    std::vector<campaign::CampaignOutcomeOperation> operations;
    std::uint64_t index = 0;
    for (const pr::CampaignMember& member : authored.members) {
        const campaign::PersistentEntityId who{++index};
        if (member.join_node_id != 0U) continue;
        const campaign::DefinitionRef unit_type{
            package.game_id, core::ContentCategory::unit_type,
            member.unit_type_id
        };
        operations.push_back(campaign::recruit_unit(who, unit_type));
        operations.push_back(
            campaign::set_availability(who, campaign::Availability::available)
        );
        const cr::StartingKit kit =
            cr::starting_kit(package, who, member.unit_type_id);
        operations.insert(
            operations.end(), kit.operations.begin(), kit.operations.end()
        );
    }
    const std::vector<campaign::CampaignOutcomeOperation> stock =
        cr::node_item_grants(package.game_id, authored, 0U);
    operations.insert(operations.end(), stock.begin(), stock.end());
    return campaign::make_outcome_batch(
        {campaign::DefinitionRef{
             package.game_id, core::ContentCategory::campaign, road_campaign
         },
         0U,
         0U},
        operations
    );
}

std::uint32_t held(
    const campaign::CampaignState& state,
    const pf::LoadedPackage& package,
    std::uint64_t item
) {
    return campaign::item_quantity(state, {}, item_of(package, item));
}

// ---------------------------------------------------------------------------

// The founding, and the retry. A campaign founded once holds exactly what its
// author stocked it with; the same founding derived a second time is the same
// batch and is answered as one.
void a_campaign_is_founded_with_the_store_its_author_stocked() {
    const pf::LoadedPackage package = compile_and_load();
    if (failures != 0) return;

    storage::MemorySlotStorage device;
    const client::PackageBoards boards{package};
    client::CampaignSessionOptions options;
    options.slot = "road";
    client::CampaignSession session{
        package, road_campaign, boards, device, options
    };
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        session.begin(failure, refused, resumed) ==
            client::CampaignSessionError::none,
        "the campaign founds"
    );

    const std::vector<campaign::InventoryStack> store = session.store();
    expect(
        store.size() == 2U && store[0].item == item_of(package, tonic) &&
            store[0].quantity == 3U && store[1].item == item_of(package, rope) &&
            store[1].quantity == 1U,
        "and the company owns exactly what the author stocked it with, one "
        "addition per authored entry"
    );
    expect(
        session.roster().size() == 1U &&
            session.roster().front().carried.empty(),
        "with nothing in anybody's hands, because the store and a satchel are "
        "two different owners"
    );
    expect(
        session.save() == storage::StorageError::none,
        "the founded campaign is written"
    );

    // The same founding, derived again and applied to the campaign it founded.
    const pr::CampaignLoadResult authored =
        pr::load_campaign(package, road_campaign);
    expect(static_cast<bool>(authored), "the campaign decodes");
    campaign::CampaignState again = state_in_slot(package, device, "road");
    const campaign::OutcomeApplication retried =
        campaign::apply_outcome(again, founding_batch(package, authored.definition));
    expect(
        static_cast<bool>(retried) && retried.already_applied,
        "a founding derived twice is one founding, recognised by its identity "
        "rather than by comparing what the store holds"
    );
    expect(
        held(again, package, tonic) == 3U && held(again, package, rope) == 1U,
        "so the store still holds what one founding left"
    );
}

// What passing a node puts in the store, and what passing it twice puts there.
void a_road_that_loops_past_the_abbey_is_given_twice() {
    const pf::LoadedPackage package = compile_and_load();
    if (failures != 0) return;

    storage::MemorySlotStorage device;
    const client::PackageBoards boards{package};
    client::CampaignSessionOptions options;
    options.slot = "loop";
    client::CampaignSession session{
        package, road_campaign, boards, device, options
    };
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        session.begin(failure, refused, resumed) ==
            client::CampaignSessionError::none,
        "the campaign founds at the abbey"
    );
    expect(
        session.standing().node ==
            cr::campaign_node_ref(package.game_id, abbey_node),
        "standing where the flow enters"
    );

    std::vector<client::RosterEntry> joined;
    expect(
        session.advance_story(joined) == client::CampaignSessionError::none,
        "the road passes the abbey"
    );
    const std::vector<campaign::InventoryStack> after_the_abbey =
        session.store();
    expect(
        after_the_abbey.size() == 2U &&
            after_the_abbey.front().item == item_of(package, tonic) &&
            after_the_abbey.front().quantity == 5U,
        "the abbey's two draughts land in the store on top of the three the "
        "campaign was founded with"
    );
    expect(
        session.standing().node ==
            cr::campaign_node_ref(package.game_id, crossroads_node),
        "and the road has moved on"
    );

    expect(
        session.advance_story(joined) == client::CampaignSessionError::none,
        "the road passes the crossroads, which gives nothing"
    );
    expect(
        session.store().front().quantity == 5U,
        "so the store is unchanged, said by a node with no grants rather than "
        "by a grant of nothing"
    );
    expect(
        session.standing().node ==
            cr::campaign_node_ref(package.game_id, abbey_node),
        "and the cycle has come back to the abbey"
    );

    expect(
        session.advance_story(joined) == client::CampaignSessionError::none,
        "which the road passes a second time"
    );
    expect(
        session.store().front().quantity == 7U,
        "and is blessed a second time, because a grant is an occurrence and "
        "not a statement about how much the store should hold"
    );
    expect(
        session.save() == storage::StorageError::none,
        "the looped campaign is written"
    );

    // The two passes are two committed batches and not one committed twice.
    // The store above already says so; this says why: four distinct
    // identities, one per thing the campaign committed.
    const campaign::CampaignState walked =
        state_in_slot(package, device, "loop");
    expect(
        walked.applied_outcomes.size() == 4U,
        "the founding and the three completions are four committed batches"
    );
    std::vector<campaign::OutcomeId> ids = walked.applied_outcomes;
    std::sort(ids.begin(), ids.end());
    expect(
        std::adjacent_find(ids.begin(), ids.end()) == ids.end(),
        "each under an identity of its own, so the abbey's two passes are two "
        "batches whose sequences moved apart"
    );
    expect(
        walked.progress.history.size() == 4U,
        "and the route records both laps rather than collapsing them"
    );
}

// The other half of the same distinction: a batch recomputed from a campaign
// that has committed nothing since folds the same source over the same
// operations, so it is the same batch and changes nothing.
//
// The batch below is built the way the session builds one, from the campaign
// as it stood *before* the node completed: the standing node as the reference,
// a zero battle hash because no battle produced it, and the history's length
// as the sequence. That it is then recognised is what makes the client's rule
// load-bearing: the sequence is derived at the moment a batch is built and is
// never cached across a commit.
void a_completing_nodes_batch_recomputed_is_recognised_as_a_retry() {
    const pf::LoadedPackage package = compile_and_load();
    if (failures != 0) return;

    storage::MemorySlotStorage device;
    const client::PackageBoards boards{package};
    client::CampaignSessionOptions options;
    options.slot = "retry";
    client::CampaignSession session{
        package, road_campaign, boards, device, options
    };
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        session.begin(failure, refused, resumed) ==
            client::CampaignSessionError::none,
        "the campaign founds"
    );
    expect(session.save() == storage::StorageError::none, "and is written");

    const pr::CampaignLoadResult authored =
        pr::load_campaign(package, road_campaign);
    expect(static_cast<bool>(authored), "the campaign decodes");
    const campaign::CampaignState before =
        state_in_slot(package, device, "retry");
    const campaign::CampaignOutcomeBatch abbey = campaign::make_outcome_batch(
        {cr::campaign_node_ref(package.game_id, abbey_node),
         0U,
         static_cast<std::uint64_t>(before.progress.history.size())},
        cr::node_item_grants(package.game_id, authored.definition, abbey_node)
    );

    std::vector<client::RosterEntry> joined;
    expect(
        session.advance_story(joined) == client::CampaignSessionError::none,
        "the road passes the abbey"
    );
    expect(session.save() == storage::StorageError::none, "and is written");

    campaign::CampaignState after = state_in_slot(package, device, "retry");
    expect(
        held(after, package, tonic) == 5U,
        "the store holds the three it was founded with and the two the abbey "
        "gave"
    );
    const campaign::OutcomeApplication again =
        campaign::apply_outcome(after, abbey);
    expect(
        static_cast<bool>(again) && again.already_applied,
        "and the batch recomputed from the campaign that had committed nothing "
        "since is the batch that was committed"
    );
    expect(
        held(after, package, tonic) == 5U,
        "so the store is unchanged, which is the right answer to an "
        "interrupted write rather than to a second visit"
    );
}

// A stocked campaign, put down and picked up. The store is what the store was,
// and nothing had to be taught a new format to carry it: a quantity of an item
// identity is what the roster's section has always held.
void a_stocked_campaign_is_saved_and_resumed() {
    const pf::LoadedPackage package = compile_and_load();
    if (failures != 0) return;

    storage::MemorySlotStorage device;
    const client::PackageBoards boards{package};
    client::CampaignSessionOptions options;
    options.slot = "kept";
    client::CampaignSession session{
        package, road_campaign, boards, device, options
    };
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        session.begin(failure, refused, resumed) ==
            client::CampaignSessionError::none,
        "the campaign founds"
    );
    std::vector<client::RosterEntry> joined;
    expect(
        session.advance_story(joined) == client::CampaignSessionError::none,
        "and passes the abbey"
    );
    expect(session.save() == storage::StorageError::none, "and is written");

    const campaign::MigratedLoad restored =
        slot_contents(package, device, "kept");
    expect(
        static_cast<bool>(restored),
        std::string_view{campaign::migration_error_name(restored.migration.error)}
    );
    expect(
        restored.migration.applied.empty(),
        "a save this build wrote needs no migration, and none runs: an "
        "authored stock added no format"
    );
    const campaign::CampaignState& kept = restored.save.state;
    expect(
        kept.store.size() == 2U && kept.store[0].item == item_of(package, tonic) &&
            kept.store[0].quantity == 5U &&
            kept.store[1].item == item_of(package, rope) &&
            kept.store[1].quantity == 1U,
        "and the store comes back holding exactly what it held: the founding "
        "stock and what the abbey gave"
    );

    // And a session that resumes it stands where it stood, holding it.
    client::CampaignSessionOptions resuming = options;
    resuming.resume = true;
    client::CampaignSession second{
        package, road_campaign, boards, device, resuming
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
    expect(
        second.store().size() == 2U && second.store().front().quantity == 5U,
        "with the store the save carried, and not a store founded a second "
        "time on top of it"
    );
    expect(
        second.standing().node ==
            cr::campaign_node_ref(package.game_id, crossroads_node),
        "standing where the road had reached"
    );
}

}  // namespace

int main() {
    a_campaign_is_founded_with_the_store_its_author_stocked();
    a_road_that_loops_past_the_abbey_is_given_twice();
    a_completing_nodes_batch_recomputed_is_recognised_as_a_retry();
    a_stocked_campaign_is_saved_and_resumed();
    return failures == 0 ? 0 : 1;
}
