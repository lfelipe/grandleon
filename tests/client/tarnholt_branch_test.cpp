// SPDX-License-Identifier: MIT
#include <grandleon/campaign/migration.hpp>
#include <grandleon/campaign/state.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/sheet/unit_sheet.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/storage/memory_storage.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// The Tarnholt Line's branch, played through the client every platform plays
// through, out of the shipped project rather than out of a fixture.
//
// The campaign forks on the Harrow Burn, and it forks on something a player
// *did* rather than on whether they won: one of the four Ashen Coil soldiers
// standing in the burnt village is a Tarnholt man who was pressed into the
// Coil, and his placement authors a talk. Talking to him takes him off the
// board alive and raises a world flag; a `worldFlagEquals` edge reads the flag
// and opens the Sunken Mill, which no other route reaches; and the two threads
// rejoin at one node by two edges naming it.
//
// `games/tarnholt/src/play_tarnholt.cpp` plays the conversation itself, on the
// board, because what a talk does to a battle belongs to the simulation. What
// it cannot do is follow the branch: it drives
// `package_runtime::CampaignCursor`, which holds no campaign state and
// therefore no world flag. This does, because `client::CampaignSession` is the
// real thing: the same session the Nintendo 64, the terminal and the browser
// all drive.
//
// Both threads are played from the same founding, differing in one gesture, and
// the claim is:
//
//   *Talking opens a map nobody else sees and brings back a member; killing
//   takes the long way with the company it set out with; and both arrive at the
//   same node.*

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace cr = grandleon::campaign_runtime;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const std::uint64_t tarnholt_line = core::stable_content_id_v1("tarnholt_line");

// Founding assigns one-based identities in authored order: the four the
// campaign is founded with, then each node's recruits in flow order. Captain
// Mirea is the first of the two the marching order brings in.
constexpr campaign::PersistentEntityId mirea{5};

pf::LoadedPackage compile_the_shipped_campaign() {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) + "/games/tarnholt/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Tarnholt source opens");
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

// What the slot holds, read back the way a real resume reads it. The flag is
// asserted against this rather than against the session's own memory, because
// a world flag that only exists until the process ends is not campaign state.
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

std::uint64_t node_of(const client::CampaignSession& session) {
    return session.standing().node.stable_id;
}

// Walks story nodes until the campaign is standing on a board or has finished.
void walk_to_the_next_board(client::CampaignSession& session,
                           const std::string& label) {
    for (int beat = 0; beat < 8; ++beat) {
        if (session.standing().kind != pr::CampaignNodeKind::story) return;
        std::vector<client::RosterEntry> joined;
        if (session.advance_story(joined) != client::CampaignSessionError::none) {
            expect(false, label + ": a story node advances");
            return;
        }
    }
}

// One activation of walking towards `quarry`, chosen out of the engine's own
// reachable set rather than by naming tiles: the burn is a village with six of
// the guard standing in the west end of it, so a hand-picked route is a route
// somebody is standing on.
bool step_towards(sim::Encounter& encounter, std::uint64_t walker,
                  std::uint64_t quarry) {
    const auto snapshot = encounter.snapshot();
    const auto find = [&snapshot](std::uint64_t id) -> const sim::UnitSnapshot* {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.id == id) return &unit;
        }
        return nullptr;
    };
    const auto away = [](sim::Position from, sim::Position to) {
        const int dx = from.x - to.x;
        const int dy = from.y - to.y;
        return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    };
    const sim::UnitSnapshot* const mover = find(walker);
    const sim::UnitSnapshot* const target = find(quarry);
    if (mover == nullptr || target == nullptr) return false;
    int closest = away(mover->position, target->position);
    sim::Position chosen = mover->position;
    for (const sim::Position tile : sim::reachable_tiles(snapshot, walker)) {
        const int distance = away(tile, target->position);
        if (distance < closest) {
            closest = distance;
            chosen = tile;
        }
    }
    if (chosen == mover->position) return false;
    return static_cast<bool>(
        encounter.apply({sim::CommandType::move, walker, chosen, 0, 0})
    );
}

// Hands the burn to the other side.
//
// Tarnholt plays in side blocks, so this is not one gesture: the engine names
// no actor, the block stays open until nobody on the side is left to act, and
// handing over means waiting out everybody who has not finished. It also means
// a character who walked this block is finished and cannot speak until the
// round comes back round to it. That is why the two walks below hand over
// rather than pressing the same character twice.
bool hand_the_turn_over(sim::Encounter& encounter) {
    const sim::Side opened = encounter.snapshot().active_side;
    for (int guard = 0; guard < 64; ++guard) {
        const auto standing = encounter.snapshot();
        if (standing.active_side != opened) return true;
        bool waited = false;
        for (const sim::UnitSnapshot& unit : standing.units) {
            if (unit.side != opened) continue;
            if (unit.health <= 0 || unit.departed || !unit.arrived) continue;
            if (static_cast<bool>(encounter.apply(
                    {sim::CommandType::wait, unit.id, {}, 0, 0}
                ))) {
                waited = true;
                break;
            }
        }
        if (!waited) return false;
    }
    return encounter.snapshot().active_side != opened;
}

// A won board, without fighting it. Every board in this campaign except the
// burn is somebody else's subject: `play_tarnholt.cpp` plays them, and what
// this file needs from them is only that the campaign moves through them.
client::BattleReport won(const cr::CampaignEncounter& board,
                        const std::string& label) {
    client::BattleReport report;
    sim::Encounter::CreateResult created =
        sim::create_encounter(board.encounter.definition);
    expect(static_cast<bool>(created), label + ": the board starts a battle");
    if (!created) return report;
    report.final_snapshot = created.encounter.snapshot();
    report.canonical_hash = created.encounter.canonical_hash();
    report.outcome = sim::Outcome::first_side_won;
    return report;
}

}  // namespace

int main() {
    const pf::LoadedPackage package = compile_the_shipped_campaign();
    if (failures != 0) return 1;

    // The two characters on the second side who are people rather than kinds of
    // soldier, named where a person's name belongs.
    //
    // A unit type is a class. `ashen_commander` and `ashen_marshal` are
    // entirely plausible ids for a stranger's project, and a campaign that put
    // its Warden's name on one of them would label every commander in every
    // project that shares the id. Neither of these two stands on a campaign
    // roster (the second side reaches no roster), so their names are authored
    // on their placements and travel in the package's `placement_names`
    // section, keyed by the placement's own identity. This asks the resolver
    // every client's screen asks.
    {
        const auto called = [&package](const char* board, const char* key) {
            const auto id = core::stable_content_id_v1(
                std::string("tarnholt_line/") + board + "/" + key
            );
            const auto load = pr::load_encounter(
                package,
                core::stable_content_id_v1(std::string("tarnholt_line/") + board)
            );
            expect(
                load.error == pr::EncounterLoadError::none,
                std::string("the ") + board + " loads"
            );
            auto built = sim::create_encounter(load.definition);
            expect(static_cast<bool>(built), std::string("the ") + board + " starts");
            if (!built) return std::string();
            return std::string(
                grandleon::sheet::character_name(
                    &package, built.encounter.snapshot(), id
                )
                    .text
            );
        };
        expect(
            called("ashen_watch_battle", "ashen_commander_kesh") == "WARDEN KESH",
            "the Warden is named on his placement"
        );
        expect(
            called("coldgate_battle", "ashen_marshal_vorne") == "MARSHAL VORNE",
            "and the Marshal on his"
        );
        expect(
            std::string(grandleon::sheet::unit_type_name(
                            &package, core::stable_content_id_v1("ashen_commander")
                        )
                            .text) == "ASHEN COMMANDER" &&
                std::string(grandleon::sheet::unit_type_name(
                                &package, core::stable_content_id_v1("ashen_marshal")
                            )
                                .text) == "ASHEN MARSHAL" &&
                std::string(grandleon::sheet::unit_type_name(
                                &package, core::stable_content_id_v1("dawn_commander")
                            )
                                .text) == "DAWN COMMANDER",
            "while the classes they belong to are classes"
        );
    }

    const campaign::DefinitionRef flag{
        package.game_id, core::ContentCategory::world_flag,
        core::stable_content_id_v1("coll_rankin_heard")
    };

    for (const bool hear_him_out : {true, false}) {
        const std::string label = hear_him_out ? "talked" : "fought";
        const std::string slot = "branch-" + label;
        storage::MemorySlotStorage device;
        const client::PackageBoards boards{package};
        client::CampaignSessionOptions options;
        options.slot = slot;
        client::CampaignSession session{
            package, tarnholt_line, boards, device, options
        };
        client::SlotFailure failure;
        bool refused = false;
        bool resumed = false;
        expect(
            session.begin(failure, refused, resumed) ==
                client::CampaignSessionError::none,
            label + ": the campaign founds"
        );

        // The store the campaign authors is there before anybody has fought
        // anything, which is the whole of what a starting store is for.
        expect(
            session.store().size() == 1U && session.store()[0].quantity == 2U,
            label + ": the guard marches out with the tonics it was given"
        );

        // Chapter one, the ford, taken as won.
        walk_to_the_next_board(session, label);
        expect(
            node_of(session) == core::stable_content_id_v1("fordlight_battle"),
            label + ": the prologue reaches the ford"
        );
        {
            const client::CampaignSession::PreparedBoard prepared =
                session.prepare_board();
            expect(
                prepared.error == client::CampaignSessionError::none,
                label + ": the ford publishes"
            );
            // The campaign's one worked specificity, on the board rather than
            // in the record: Wren Ashdown's Long Bow is authored two to three
            // and she shoots two to four, because the extra tile is a fact
            // about her and travels with her.
            const std::uint64_t wren = core::stable_content_id_v1(
                "tarnholt_line/fordlight_battle/dawn_archer_ford"
            );
            for (const sim::UnitDefinition& unit :
                 prepared.encounter.encounter.definition.units) {
                if (unit.id != wren) continue;
                expect(
                    unit.reach_bonus == 1U,
                    label + ": the archer carries her own extra tile"
                );
            }
            {
                sim::Encounter::CreateResult started = sim::create_encounter(
                    prepared.encounter.encounter.definition
                );
                for (const sim::UnitSnapshot& unit :
                     started.encounter.snapshot().units) {
                    if (unit.id != wren) continue;
                    expect(
                        unit.minimum_reach == 2U && unit.maximum_reach == 4U,
                        label + ": and the board resolves her band as two to "
                                "four"
                    );
                }
            }
            client::BattleAftermath aftermath;
            expect(
                session.commit_battle(won(prepared.encounter, label), aftermath) ==
                    client::CampaignSessionError::none,
                label + ": the ford commits"
            );
        }

        // The marching order recruits the Captain and the healer on the way to
        // Harrow, which is why there is somebody with a sword to walk up to the
        // levy with.
        walk_to_the_next_board(session, label);
        expect(
            node_of(session) == core::stable_content_id_v1("harrow_burn_battle"),
            label + ": the marching order reaches the burn"
        );
        expect(
            session.roster().size() == 6U,
            label + ": six of the guard are standing before the burn"
        );

        const client::CampaignSession::PreparedBoard burn = session.prepare_board();
        expect(
            burn.error == client::CampaignSessionError::none,
            label + ": the burn publishes"
        );
        if (burn.error != client::CampaignSessionError::none) return 1;

        // The one gesture the two runs differ in. The Captain crosses the
        // village, several rounds of it, because a levy who holds his ground
        // is seven tiles away and one walk is all a round gives her. Then she
        // either hears him out or does not.
        client::BattleReport fought;
        {
            sim::Encounter::CreateResult created =
                sim::create_encounter(burn.encounter.encounter.definition);
            expect(static_cast<bool>(created), label + ": the burn starts");
            if (!created) return 1;
            const std::uint64_t captain =
                burn.encounter.binding.battle_of(mirea).value;
            const std::uint64_t levy = core::stable_content_id_v1(
                "tarnholt_line/harrow_burn_battle/ashen_levy_coll"
            );
            const auto press = [&](const sim::Command& command) {
                const sim::CommandResult applied =
                    created.encounter.apply(command);
                fought.events.insert(
                    fought.events.end(), applied.events.begin(),
                    applied.events.end()
                );
                return static_cast<bool>(applied);
            };
            bool reached = false;
            for (int turn = 0; turn < 40 && !reached; ++turn) {
                const auto standing = created.encounter.snapshot();
                if (standing.active_side != sim::Side::first) {
                    if (!hand_the_turn_over(created.encounter)) break;
                    continue;
                }
                const sim::UnitSnapshot* her = nullptr;
                for (const sim::UnitSnapshot& unit : standing.units) {
                    if (unit.id == captain) her = &unit;
                }
                // Her second action point is what lets her walk up to him and
                // speak in the same turn. What it is not is a second walk: one
                // per turn however many points there are. So a turn she cannot
                // close the distance in still has to be handed over before she
                // gets another, which is what the two places below that close
                // the block are for.
                if (her == nullptr || her->has_acted) {
                    if (!hand_the_turn_over(created.encounter)) break;
                    continue;
                }
                if (hear_him_out) {
                    if (press({sim::CommandType::talk, captain, {}, levy, 0})) {
                        reached = true;
                        break;
                    }
                } else if (press({sim::CommandType::attack, captain, {}, levy, 0})) {
                    // Two blows with a Guard Sword is what a levy in a borrowed
                    // coat is worth against a commander, and he answers each.
                    const auto after = created.encounter.snapshot();
                    for (const sim::UnitSnapshot& unit : after.units) {
                        if (unit.id == levy && unit.health <= 0) reached = true;
                    }
                    continue;
                }
                if (!step_towards(created.encounter, captain, levy)) {
                    if (!hand_the_turn_over(created.encounter)) break;
                }
            }
            expect(reached, label + ": the Captain crossed the burn");
            fought.final_snapshot = created.encounter.snapshot();
            fought.canonical_hash = created.encounter.canonical_hash();
            // The rest of the Coil is not this test's subject; what matters is
            // which road the campaign takes out of a board it walked away from.
            fought.outcome = sim::Outcome::first_side_won;
        }

        // Departure and defeat are two different facts, and the events say so.
        bool talked = false;
        bool killed = false;
        for (const sim::Event& event : fought.events) {
            if (event.type == sim::EventType::unit_talked) talked = true;
            if (event.type == sim::EventType::unit_defeated) killed = true;
        }
        expect(
            talked == hear_him_out && killed == !hear_him_out,
            label + ": the levy left the board the way he was asked to"
        );

        client::BattleAftermath aftermath;
        expect(
            session.commit_battle(fought, aftermath) ==
                client::CampaignSessionError::none,
            label + ": the burn commits"
        );
        expect(
            aftermath.completion.advanced &&
                !aftermath.completion.used_fallback == hear_him_out,
            label + ": the road out was the conversation's or the fallback's"
        );
        expect(
            session.save() == storage::StorageError::none,
            label + ": the campaign is written"
        );

        // The flag is campaign state that survived the board and the save, or
        // it is not there at all.
        const campaign::CampaignState kept =
            state_in_slot(package, device, slot);
        const campaign::WorldValue* const held =
            campaign::find_world_value(kept, flag);
        if (hear_him_out) {
            expect(
                held != nullptr &&
                    held->type == campaign::WorldValueType::boolean &&
                    held->value == 1,
                label + ": the campaign is holding the flag the talk raised"
            );
            expect(
                node_of(session) == core::stable_content_id_v1("colls_word"),
                label + ": and the branch opened"
            );
        } else {
            expect(
                held == nullptr,
                label + ": no conversation raised no flag"
            );
            expect(
                node_of(session) == core::stable_content_id_v1("the_long_way"),
                label + ": and an unraised flag takes the fallback"
            );
        }

        // The branch's reward is a person, so the two threads meet the
        // reconvergence with different companies.
        const std::size_t before = session.roster().size();
        walk_to_the_next_board(session, label);
        const std::size_t after = session.roster().size();
        if (hear_him_out) {
            expect(
                node_of(session) ==
                    core::stable_content_id_v1("sunken_mill_battle"),
                label + ": the talked thread reaches the mill"
            );
            expect(
                after == before + 1U,
                label + ": and the man who stood on the other side joined"
            );
        } else {
            expect(
                node_of(session) ==
                    core::stable_content_id_v1("emberhall_battle"),
                label + ": the other thread walks straight to the yard"
            );
            expect(after == before, label + ": with the company it had");
        }
    }

    // And the reconvergence itself: the talked thread takes the mill, is paid
    // in tonics for it, and arrives at the same node the other thread reached
    // without either of those.
    {
        storage::MemorySlotStorage device;
        const client::PackageBoards boards{package};
        client::CampaignSessionOptions options;
        options.slot = "rejoin";
        client::CampaignSession session{
            package, tarnholt_line, boards, device, options
        };
        client::SlotFailure failure;
        bool refused = false;
        bool resumed = false;
        (void)session.begin(failure, refused, resumed);
        // Straight down the talked thread, every board taken as won, until the
        // campaign is standing where both roads end.
        for (int board = 0; board < 3; ++board) {
            walk_to_the_next_board(session, "rejoin");
            const client::CampaignSession::PreparedBoard prepared =
                session.prepare_board();
            if (prepared.error != client::CampaignSessionError::none) break;
            client::BattleAftermath aftermath;
            if (board == 1) {
                // The burn, talked through, so the mill is the next board.
                sim::Encounter::CreateResult created = sim::create_encounter(
                    prepared.encounter.encounter.definition
                );
                client::BattleReport report;
                if (created) {
                    const std::uint64_t captain =
                        prepared.encounter.binding.battle_of(mirea).value;
                    const std::uint64_t levy = core::stable_content_id_v1(
                        "tarnholt_line/harrow_burn_battle/ashen_levy_coll"
                    );
                    for (int turn = 0; turn < 40; ++turn) {
                        const auto standing = created.encounter.snapshot();
                        if (standing.active_side != sim::Side::first) {
                            if (!hand_the_turn_over(created.encounter)) break;
                            continue;
                        }
                        const sim::UnitSnapshot* her = nullptr;
                        for (const sim::UnitSnapshot& unit : standing.units) {
                            if (unit.id == captain) her = &unit;
                        }
                        if (her == nullptr || her->has_acted) {
                            if (!hand_the_turn_over(created.encounter)) break;
                            continue;
                        }
                        const sim::CommandResult heard = created.encounter.apply(
                            {sim::CommandType::talk, captain, {}, levy, 0}
                        );
                        if (heard) {
                            report.events = heard.events;
                            break;
                        }
                        if (!step_towards(created.encounter, captain, levy)) {
                            if (!hand_the_turn_over(created.encounter)) break;
                        }
                    }
                    report.final_snapshot = created.encounter.snapshot();
                    report.canonical_hash = created.encounter.canonical_hash();
                    report.outcome = sim::Outcome::first_side_won;
                }
                (void)session.commit_battle(report, aftermath);
            } else {
                (void)session.commit_battle(
                    won(prepared.encounter, "rejoin"), aftermath
                );
            }
        }
        walk_to_the_next_board(session, "rejoin");
        expect(
            node_of(session) == core::stable_content_id_v1("emberhall_battle"),
            "rejoin: the talked thread arrives at the yard the other one did"
        );
        // Six tonics in one stack: the two the guard marched out with, the one
        // the far bank of the ford had on it, and three out of the mill's
        // cellar. A grant is an event and a store holds one stack per item, so
        // the branch pays in supplies as well as in a person and the payment
        // lands in the stack that was already there.
        expect(
            session.store().size() == 1U && session.store()[0].quantity == 6U,
            "rejoin: and the mill's stores came with it"
        );
        expect(
            session.roster().size() == 7U,
            "rejoin: seven of the guard reach the yard the long way's six do"
        );
    }

    // And the content revision. `contentRevision` moves whenever the campaign's
    // content does, and a save written against an earlier revision is refused
    // **by name** rather than being quietly replaced by a fresh campaign: the
    // migration registry's content axis is built and deliberately empty, so it
    // plans one step, finds none registered, and says `missing_step`. Every
    // client shows that word; the browser offers to start fresh rather than
    // doing it unasked.
    {
        // A package identical to the shipped one except that it declares an
        // earlier content revision. Everything a save records about its content
        // comes from here.
        pf::LoadedPackage older = package;
        older.content_revision = 1024U;  // 0.1.0, packed
        expect(
            package.content_revision == 2048U,
            "the shipped campaign declares revision 0.2.0"
        );
        storage::MemorySlotStorage device;
        const client::PackageBoards boards{older};
        client::CampaignSessionOptions options;
        options.slot = "older";
        client::CampaignSession session{
            older, tarnholt_line, boards, device, options
        };
        client::SlotFailure failure;
        bool refused = false;
        bool resumed = false;
        (void)session.begin(failure, refused, resumed);
        expect(
            session.save() == storage::StorageError::none,
            "a campaign at the older revision writes its slot"
        );

        const storage::StorageRead read = device.read("older");
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
        // `missing_step`, which is the true thing: nobody registered a step
        // from the revision this save was written at.
        //
        // It used to be `step_limit_exceeded`, and the note that stood here
        // called that a diagnostic-quality question about the packing rather
        // than about this content. It was, and it is settled. A revision is
        // packed `(major << 20) | (minor << 10) | patch`, so 0.1.0 to 0.2.0 is
        // 1024 apart; the planner walked those integers one at a time and hit
        // its 64-step ceiling long before it reached the revision with nothing
        // registered, so it reported a limit it had genuinely exceeded while
        // saying nothing about the hole that was actually there. A content step
        // now declares where it lands and the chain is followed edge by edge,
        // so the first thing the walk finds is the missing step, and it says
        // so.
        expect(
            !restored &&
                restored.migration.error ==
                    campaign::MigrationError::missing_step,
            std::string("and this build refuses it by the registry's own name, "
                        "got ") + std::string(campaign::migration_error_name(
                            restored.migration.error))
        );
        expect(
            restored.migration.from == 1024U && restored.migration.to == 2048U,
            "naming the revisions it was asked to travel between"
        );
        expect(
            restored.save.state.units.empty(),
            "and hands back nothing, so no client can mistake it for a campaign"
        );
    }

    if (failures == 0) {
        std::cerr << "the Tarnholt branch opens on a conversation and both "
                     "threads rejoin\n";
    }
    return failures == 0 ? 0 : 1;
}
