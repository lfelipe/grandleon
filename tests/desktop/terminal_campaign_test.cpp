// SPDX-License-Identifier: MIT
#include <grandleon/campaign/migration.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/desktop/presenters.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/storage/filesystem_storage.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The first client that runs a campaign, driven the way a player drives it.
//
// `tests/campaign_runtime/demo_permadeath_test.cpp` proves the loop headlessly:
// a real package, a real board, a real battle, real envelope bytes in a real
// device, and the next map refusing to field the rider who fell. It proves
// nothing about a client, because when it was written there was no client to
// prove anything about. This is the same loop with a person at the other end of
// it: lines typed at a prompt, a terminal reading them, and the campaign
// narrated back.
//
// The claim is narrower than the headless test's and is the one thing only a
// client can be wrong about: **what the terminal says a battle did is what the
// engine says it did.** So every number asserted below is read out of the
// engine's own results (the `BattleProgression` the session was handed, and
// the campaign that came back out of the save slot), and the assertion is that
// the printed text contains those numbers. There is not a copied literal
// anywhere in it, deliberately: a stat gain pinned to a literal would pass
// forever after the growth stream moved under it, which is exactly the failure
// this test exists to catch.

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace desktop = grandleon::desktop;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace storage = grandleon::storage;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_contains(
    const std::string& haystack,
    const std::string& needle,
    std::string_view message
) {
    if (haystack.find(needle) == std::string::npos) {
        std::cerr << "FAIL: " << message << "\n  expected to find: " << needle
                  << '\n';
        ++failures;
    }
}

void expect_lacks(
    const std::string& haystack,
    const std::string& needle,
    std::string_view message
) {
    if (haystack.find(needle) != std::string::npos) {
        std::cerr << "FAIL: " << message << "\n  expected not to find: "
                  << needle << '\n';
        ++failures;
    }
}

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

// A narrator that watches the terminal narrate.
//
// It forwards every call to the real terminal front end, so what is captured
// below is the terminal's own output, not a paraphrase. It keeps the engine
// results it was handed on the way past. That is what lets the assertions
// compare the printed text to the numbers the engine derived, rather than to
// numbers this file chose.
class WatchingNarrator final : public client::CampaignNarrator {
public:
    explicit WatchingNarrator(client::CampaignNarrator& inner) : inner_(inner) {}

    void campaign_begun(
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        std::string_view slot,
        bool resumed
    ) override {
        founded = roster;
        founding_store = store;
        this->resumed = resumed;
        inner_.campaign_begun(roster, store, slot, resumed);
    }

    void slot_refused(const client::SlotFailure& failure) override {
        refusals.push_back(failure);
        inner_.slot_refused(failure);
    }

    void board_prepared(const client::CampaignBoard& board) override {
        boards.push_back(board);
        inner_.board_prepared(board);
    }

    void battle_aftermath(const client::BattleAftermath& aftermath) override {
        aftermaths.push_back(aftermath);
        inner_.battle_aftermath(aftermath);
    }

    void members_joined(
        const std::vector<client::RosterEntry>& joined
    ) override {
        recruited.push_back(joined);
        inner_.members_joined(joined);
    }

    void campaign_saved(std::string_view slot, storage::StorageError error)
        override {
        saves.push_back(error);
        inner_.campaign_saved(slot, error);
    }

    void management_opened(const client::CompanyManagement& company) override {
        managements.push_back(company);
        inner_.management_opened(company);
    }

    void management_committed(const client::ManagementCommit& result) override {
        gestures.push_back(result);
        inner_.management_committed(result);
    }

    client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override {
        return inner_.next_management_intent(company);
    }

    std::vector<client::RosterEntry> founded;
    // Every management screen the session opened, and every gesture it
    // committed or refused, kept the way the boards and aftermaths are: what
    // the terminal printed is asserted against what the engine returned.
    std::vector<client::CompanyManagement> managements;
    std::vector<client::ManagementCommit> gestures;
    std::vector<campaign::InventoryStack> founding_store;
    std::vector<std::vector<client::RosterEntry>> recruited;
    bool resumed{false};
    std::vector<client::SlotFailure> refusals;
    std::vector<client::CampaignBoard> boards;
    std::vector<client::BattleAftermath> aftermaths;
    std::vector<storage::StorageError> saves;

private:
    client::CampaignNarrator& inner_;
};

// One sitting at the terminal: a script of typed lines in, everything the
// client printed out, and the engine results it printed them from.
struct Sitting final {
    std::string printed;
    client::CampaignSessionError status{client::CampaignSessionError::none};
    std::vector<client::CampaignBoard> boards;
    std::vector<client::BattleAftermath> aftermaths;
    std::vector<client::SlotFailure> refusals;
    std::vector<std::vector<client::RosterEntry>> recruited;
    bool resumed{false};
    std::vector<client::CompanyManagement> managements;
    std::vector<client::ManagementCommit> gestures;
};

Sitting play(
    const pf::LoadedPackage& package,
    storage::SlotStorage& device,
    const std::string& slot,
    bool resume,
    const std::string& script
) {
    Sitting sitting;
    const std::unique_ptr<client::CampaignFrontEnd> front_end =
        desktop::make_terminal_front_end(false);
    WatchingNarrator watcher(*front_end);

    std::istringstream typed(script);
    std::ostringstream captured;
    std::streambuf* const previous_in = std::cin.rdbuf(typed.rdbuf());
    std::streambuf* const previous_out = std::cout.rdbuf(captured.rdbuf());

    client::CampaignSessionOptions options;
    options.slot = slot;
    options.resume = resume;
    sitting.status = client::run_persistent_campaign(
        package,
        core::stable_content_id_v1("muster_road"),
        *front_end,
        watcher,
        device,
        options
    );

    std::cin.rdbuf(previous_in);
    std::cout.rdbuf(previous_out);

    sitting.printed = captured.str();
    sitting.boards = watcher.boards;
    sitting.aftermaths = watcher.aftermaths;
    sitting.refusals = watcher.refusals;
    sitting.recruited = watcher.recruited;
    sitting.resumed = watcher.resumed;
    sitting.managements = watcher.managements;
    sitting.gestures = watcher.gestures;
    return sitting;
}

std::string decimal(std::uint64_t value) {
    std::ostringstream text;
    text << value;
    return text.str();
}

// What the campaign holds, read back out of the slot through the pipeline a
// real load goes through. The narration is checked against this too: a terminal
// that printed a level the save did not carry would be a terminal telling a
// player something that will not be there tomorrow.
campaign::CampaignState state_in_slot(
    const pf::LoadedPackage& package,
    storage::SlotStorage& device,
    const std::string& slot
) {
    const storage::StorageRead read = device.read(slot);
    expect(static_cast<bool>(read), "the slot the campaign was saved to reads");
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

// ---------------------------------------------------------------------------

// The crossing, typed. It opens in the deployment phase, because the content
// authors a region on the western bank. So the first three lines are the two
// verbs that phase has and a refusal it names. The tile the vanguard is put on
// is the tile it was authored on, which is accepted and changes nothing: the
// battle after `begin` is exactly the battle this script always fought.
//
// Then the outrider trades with the picket and the picket finishes it on its
// own activation; the vanguard rides up and the picket's counterattack is the
// one it does not survive, so the sixty experience the River Watch is worth
// goes to the rider still standing.
// The crossing opens on the company rather than on the board, because the
// management stage stands before every board. The four lines that open this
// script are the whole of that stage exercised without moving a single number
// the battle below produces: the company is printed, the vanguard's draught is
// put in the store and taken back out again (two committed batches whose net
// effect on the board is nothing), and then the board is taken.
//
// Net zero is the point. What a management gesture *does* is proved in
// `tests/campaign_runtime/demo_permadeath_test.cpp` and in the sitting below
// that benches somebody for real; what is proved here is that the verbs commit,
// that the terminal says what moved, and that a company left as it was fights
// exactly the battle it always fought.
constexpr const char* crossing_script =
    "roster\n"
    "take 1 1\n"
    "give 1 1\n"
    "proceed\n"
    "deploy 1 4 1\n"
    "deploy 1 0 1\n"
    "begin\n"
    "roster\n"
    "attack 3 2\n"
    // The outrider trades with the picket and both are left on one; the lead
    // rider stands still for a turn and the picket's next swing is the one the
    // outrider does not survive. Then the walk and the strike are one turn:
    // two action points are what let the lead ride onto the tile its companion
    // fell from and finish the picket without waiting to be given the board
    // again.
    "wait 1\n"
    "move 1 2 1\n"
    "attack 1 2\n"
    // And then the road's own management stage, where the second member of the
    // company is the rider the crossing buried. Offering the store's draught to
    // them is a gesture the campaign refuses by name, which is the refusal a
    // player can most easily reach and the one that matters most.
    "give 2 1\n"
    "proceed\n"
    "quit\n";

// The road, typed by the ferryman the crossing recruited. He rides down to the
// picket and finishes it himself, which is the whole point of him: a character
// the campaign did not hold when it started, fighting a battle.
// The road authors no region, so it opens on an activation exactly as it
// always did and this script is unchanged. That is the compatibility claim
// stated as a script rather than as a paragraph.
// A resumed campaign lands on the management stage, which is where this script
// starts. The ferryman is benched and fielded again (two committed batches
// that leave the company as the save left it), so the road below is the road
// this script always fought.
constexpr const char* road_script =
    "bench 3\n"
    "field 3\n"
    "proceed\n"
    "move 2 3 2\n"
    "attack 2 1\n"
    "attack 2 1\n";

// A fresh company, and a line that leaves somebody behind for real. The
// outrider sits the crossing out, so the board the client is handed is one
// rider short and says whose. Then the script quits: what this proves is the
// board, not another battle.
constexpr const char* benching_script =
    "bench 2\n"
    "proceed\n"
    "quit\n";

// The same, taken as far as it goes: a company with nobody left to send. The
// roster refuses the board by its own name, the campaign stands where it stood,
// and fielding somebody again makes it a board.
constexpr const char* emptied_script =
    "bench 1\n"
    "bench 2\n"
    "proceed\n"
    "field 1\n"
    "proceed\n"
    "quit\n";

// A name as this client shows it: upper-cased ASCII, because every name the
// terminal prints goes through one resolver and that resolver folds to the
// character set the consoles beside it can draw. Spelled out here rather than
// borrowed from `grandleon::sheet`, so a fold that changed would fail this
// test instead of agreeing with it.
std::string folded(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (const char character : name) {
        out.push_back(
            character >= 'a' && character <= 'z'
                ? static_cast<char>(character - ('a' - 'A'))
                : character
        );
    }
    return out;
}

// What the author called a member, out of the roster the engine handed the
// narrator, as the narrator spells it. Never a literal: the narration is
// asserted against the content, not against a name this file chose.
std::string name_of(
    const std::vector<client::RosterEntry>& roster,
    campaign::PersistentEntityId member
) {
    for (const client::RosterEntry& entry : roster) {
        if (entry.member == member) return folded(entry.name);
    }
    return {};
}

void a_campaign_is_played_narrated_saved_and_resumed() {
    const pf::LoadedPackage package = compile_the_maintained_demo();
    if (failures != 0) return;

    storage::FilesystemSlotStorage device(GRANDLEON_TERMINAL_TEST_ROOT);
    expect(device.available(), "the save directory is a save directory");
    (void)device.erase("muster-road");

    // 1. Begin. The roster is founded from the content, the first board is the
    //    board the package holds, and nobody is left off it.
    const Sitting first =
        play(package, device, "muster-road", false, crossing_script);
    expect(
        first.status == client::CampaignSessionError::none,
        "the sitting ends without the session refusing anything"
    );
    expect(!first.resumed, "a campaign with nothing to resume from is founded");

    // 1a. `roster` was typed in the middle of the crossing, and it answers with
    //     what the campaign holds: each member's own kit, which is what they
    //     are fighting with, and the company's store, which is a different
    //     thing and is printed as one. Both riders were stocked at the founding
    //     from the one place an author states it, and nothing has fallen yet.
    expect_contains(
        first.printed,
        "carrying: 1 x FIELD TONIC",
        "the roster says what each member is carrying"
    );
    expect_contains(
        first.printed,
        "the store: nothing",
        "and says the company's own store is a different, and empty, thing"
    );
    expect(
        first.boards.size() == 2U && first.boards.front().excluded.empty(),
        "the crossing fields everybody, and the road is offered after it"
    );
    expect_contains(
        first.printed,
        "Everyone the roster can still field takes this board.",
        "and the terminal says so"
    );

    // 1a2. And before any of that, the company. The management stage stands
    //      before every board, so a fresh campaign opens on it: the two riders
    //      the author wrote, numbered, each carrying the draught the founding
    //      put in their hands, and an empty store beneath them.
    expect(
        first.managements.size() == 2U,
        "the stage opened once before each of the two boards this sitting saw"
    );
    if (first.managements.empty()) return;
    const client::CompanyManagement& opening = first.managements.front();
    expect(
        opening.store.empty() && opening.roster.size() == 2U,
        "a founded company owns nothing beyond what its members carry"
    );
    // Who the crossing has somewhere to stand. The ferryman is authored onto
    // this campaign and is placed on the *road*, not on the crossing. He has
    // not joined yet either, so the stage names exactly the two riders.
    expect(
        opening.placeable.size() == 2U,
        "the stage says which of the company the crossing has a place for"
    );
    expect_contains(
        first.printed,
        "THE COMPANY  before the next battle.",
        "and the terminal opens on it"
    );
    expect_contains(
        first.printed, "(takes the next board)",
        "saying of each member whether they are going"
    );

    // 1a3. Two gestures that commit, and cancel. The vanguard's draught goes
    //      into the store and comes back out, so the company that fights the
    //      crossing is the company the author wrote. Both moves were real
    //      committed batches, which is what the store line in between says.
    expect(
        first.gestures.size() >= 2U && static_cast<bool>(first.gestures[0]) &&
            static_cast<bool>(first.gestures[1]),
        "the two moves the script typed both committed"
    );
    expect(
        first.gestures.size() >= 2U &&
            first.gestures[0].batch.id != first.gestures[1].batch.id,
        "and are two batches rather than one committed twice, because the "
        "count of committed outcomes moved between them"
    );
    expect(
        first.gestures.size() >= 2U && first.gestures[0].saved &&
            first.gestures[1].saved,
        "each was written to the slot as it was made, so the stage holds "
        "nothing a crash would lose"
    );
    expect_contains(
        first.printed,
        "1 x FIELD TONIC passed from VANGUARD RILLA (DAWN GUARD) to the store.",
        "the terminal says what left whose hands"
    );
    expect_contains(
        first.printed,
        "1 x FIELD TONIC passed from the store to VANGUARD RILLA (DAWN GUARD).",
        "and what came back"
    );

    // 1b. And before anybody acts, the phase the content authored: the region
    //     is announced with the tiles each rider may be stood on, a tile
    //     outside it is refused by the engine's own name for it, standing a
    //     rider where it already stands is accepted, and the battle opens
    //     because somebody opened it.
    expect_contains(
        first.printed,
        "Deployment. Stand your line before the battle opens;",
        "the terminal announces the phase"
    );
    expect_contains(
        first.printed,
        "may be placed: 0,0 0,1 1,1 0,2 1,2",
        "and lists the ground the western bank offers, minus what is held"
    );
    expect_contains(
        first.printed,
        "refused: outside_zone",
        "a tile the author did not offer is refused by name"
    );
    expect_contains(
        first.printed,
        "takes position at 0,1",
        "a rider stood inside the region is reported where it stands"
    );
    expect_contains(
        first.printed,
        "the line is set; the battle begins",
        "and the phase ends because the player ended it"
    );

    // 2. The battle happened and the terminal narrated exactly one aftermath.
    expect(
        first.aftermaths.size() == 1U,
        "one finished battle, one aftermath"
    );
    if (first.aftermaths.empty()) return;
    const client::BattleAftermath& aftermath = first.aftermaths.front();

    // 3. Somebody did not come back, and the narration names them by the
    //    identity the campaign holds them under.
    expect(
        aftermath.fallen.size() == 1U,
        "exactly one roster member did not come back"
    );
    if (aftermath.fallen.empty()) return;
    const campaign::PersistentEntityId fallen = aftermath.fallen.front();
    expect_contains(
        first.printed,
        name_of(aftermath.roster, fallen) +
            " (DAWN GUARD), unit 3 on that board, died, and will not take "
            "the field again.",
        "the terminal says who died, by the name the author gave them, and "
        "which unit on the board they were"
    );

    // 3a. And it said so at the moment it happened, which is the only moment a
    //     player is looking at the battle. The name leads and the typing label
    //     follows it, rather than the line reading "3 was defeated": the number
    //     is the one-based place the unit takes in the board's list, which is
    //     what a player types at the prompt and not something anybody could
    //     grieve. The word is the same word the screen after the battle uses
    //     for the same event.
    expect_contains(
        first.printed,
        "    " + name_of(aftermath.roster, fallen) + " (3) died",
        "the battle log names whoever died, as they die"
    );
    expect(
        first.printed.find("was defeated") == std::string::npos,
        "and nothing anywhere in the sitting still says a number was defeated"
    );

    // The other side is named too, out of the one thing anybody knows about a
    // character no roster member is standing in: what they are. A picket falls
    // on this board, and a log that said "4 died" of them while naming the
    // rider would be a log that told half a battle.
    expect_contains(
        first.printed,
        std::string("    RIVER WATCH (2) died"),
        "and a unit the campaign never met is named by its class"
    );

    // 4. The level-up, pinned against the engine's own derivation of it.
    //    `LevelUp::points` is what the growth stream granted; the loop below
    //    asserts that every point it granted was printed, in the stat's own
    //    name, and that the levels either side of it were printed too. Nothing
    //    here knows what the numbers are. That is the point, because a
    //    literal would survive the stream moving and this does not.
    expect(
        aftermath.progression.level_ups.size() == 1U,
        "the surviving rider is the only one who reached a new level"
    );
    if (aftermath.progression.level_ups.empty()) return;
    const grandleon::campaign_runtime::LevelUp& growth =
        aftermath.progression.level_ups.front();
    expect_contains(
        first.printed,
        name_of(aftermath.roster, growth.member) +
            " (DAWN GUARD) reached level " + decimal(growth.to_level) +
            " (from " + decimal(growth.from_level) + ")",
        "the terminal states the levels the engine derived"
    );
    std::size_t granted = 0;
    for (std::size_t index = 0; index < campaign::growable_stat_count; ++index) {
        const std::uint16_t points = growth.points[index];
        if (points == 0U) continue;
        ++granted;
        expect_contains(
            first.printed,
            "    +" + decimal(points) + " " +
                std::string(campaign::growable_stat_name(
                    static_cast<campaign::GrowableStat>(index)
                )),
            "every point the growth stream granted is a line the player read"
        );
    }
    expect(
        granted != 0U,
        "the roll granted something, so the narration had something to say"
    );

    // 5. The experience, from the committed operations rather than from a
    //    number this file chose.
    std::int64_t experience = 0;
    for (const campaign::CampaignOutcomeOperation& operation :
         aftermath.progression.operations) {
        if (operation.kind != campaign::OutcomeOperationKind::grant_experience) {
            continue;
        }
        if (!(operation.subject == growth.member)) continue;
        experience += operation.amount;
    }
    expect(experience > 0, "the felling blow was worth something");
    expect_contains(
        first.printed,
        decimal(static_cast<std::uint64_t>(experience)) +
            " experience earned",
        "and the terminal says how much"
    );

    // 6. What the battle did to the store, from the very same operations.
    std::size_t added = 0;
    std::size_t consumed = 0;
    for (const campaign::CampaignOutcomeOperation& operation :
         aftermath.progression.operations) {
        if (operation.subject.value != 0U) continue;
        if (operation.kind == campaign::OutcomeOperationKind::add_item) ++added;
        if (operation.kind == campaign::OutcomeOperationKind::consume_item) {
            ++consumed;
        }
    }
    // Nothing the battle itself gave the company: the picket is authored to
    // leave its tonic three times in five, the draw comes off this battle's own
    // seeded drop stream, and on this board it does not come up. Pinned as
    // tightly as a drop would be, in both directions: a line about a draught
    // reaching the store is exactly as wrong as a missing one.
    expect(added == 0U, "the picket kept its tonic, the roll not having landed");
    expect_lacks(
        first.printed,
        "went into the store",
        "so the terminal claims nothing went into the store"
    );
    expect(
        consumed == 0U,
        "and nothing was drunk in this playthrough, so nothing came out of "
        "anybody's pack"
    );

    // 6a. The two owners a campaign keeps, apart. Nobody drank and nothing
    //     fell, so the store holds exactly one: the draught the fallen rider
    //     was still carrying, which `record_permanent_death` returned.
    //     Everybody still standing holds their own, which is what they will
    //     take onto the next board. Every number here is read out of the
    //     campaign rather than out of a unit type.
    expect(
        aftermath.store.size() == 1U && aftermath.store.front().quantity == 1U,
        "the store holds the one draught the fallen rider was still carrying"
    );
    for (const client::RosterEntry& entry : aftermath.roster) {
        const bool alive =
            entry.availability == campaign::Availability::available;
        expect(
            alive == (entry.carried.size() == 1U &&
                      entry.carried.front().quantity == 1U),
            alive ? "everybody still standing carries the draught the founding "
                    "put in their hands"
                  : "while the rider the crossing buried carries nothing, "
                    "having left their kit to the company"
        );
    }

    // 6b. Somebody joined. The ferryman is authored onto the crossing, so he
    //     arrives in the batch that buried the outrider. The terminal reads
    //     him off that batch the way it reads a level-up off it.
    expect(
        aftermath.recruited.size() == 1U,
        "the crossing brought exactly the member the author wrote onto it"
    );
    if (aftermath.recruited.empty()) return;
    const client::RosterEntry& joined = aftermath.recruited.front();
    expect(
        joined.availability == campaign::Availability::available,
        "and brought them in able to take the field"
    );
    expect(
        first.recruited.size() == 1U && first.recruited.front().size() == 1U,
        "the narrator was told once, with one member in it"
    );
    expect_contains(
        first.printed,
        folded(joined.name) + " (DAWN GUARD) joined the company.",
        "and the terminal says so, by the name the author gave them"
    );
    expect(
        std::any_of(
            aftermath.roster.begin(),
            aftermath.roster.end(),
            [&joined](const client::RosterEntry& entry) {
                return entry.member == joined.member;
            }
        ),
        "and the company the terminal printed afterwards holds them"
    );

    // 7. Saved, and the save says what the terminal said.
    expect_contains(
        first.printed, "Saved to slot 'muster-road'.", "the campaign is written"
    );
    const campaign::CampaignState saved =
        state_in_slot(package, device, "muster-road");
    const campaign::PersistentUnit* const survivor =
        campaign::find_unit(saved, growth.member);
    expect(
        survivor != nullptr && survivor->progression.level == growth.to_level,
        "the level the terminal printed is the level the slot holds"
    );
    expect(
        survivor != nullptr &&
            survivor->progression.experience ==
                static_cast<std::uint32_t>(experience),
        "and so is the experience"
    );
    expect(
        survivor != nullptr && survivor->progression.gained == growth.points,
        "and so is every point the roll granted"
    );
    const campaign::PersistentUnit* const buried =
        campaign::find_unit(saved, fallen);
    expect(
        buried != nullptr && buried->availability == campaign::Availability::dead,
        "and the rider the terminal buried came back from the slot buried"
    );

    // 7a. And the refusal a player reaches most easily. At the road's own
    //     management stage the second member of the company is the rider the
    //     crossing buried; offering them the store's draught is refused by the
    //     campaign, under the campaign's own name for it, and nothing moves.
    expect(
        first.gestures.size() == 3U && !static_cast<bool>(first.gestures.back()),
        "the gift to the fallen rider was refused"
    );
    if (first.gestures.size() == 3U) {
        expect(
            first.gestures.back().application.error ==
                campaign::OutcomeError::unit_is_dead,
            "by the one name the whole permanence rule exists for"
        );
        expect(
            !first.gestures.back().saved,
            "and nothing was written, because nothing changed"
        );
        expect_contains(
            first.printed,
            std::string("the campaign refused it: ") +
                std::string(campaign::outcome_error_name(
                    first.gestures.back().application.error
                )),
            "and the terminal repeats the campaign's own word for it"
        );
    }

    // 8. Resume. The campaign stands where the graph left it, and the rider who
    //    fell is not on the board however plainly the road lists them.
    const Sitting second =
        play(package, device, "muster-road", true, road_script);
    expect(
        second.status == client::CampaignSessionError::none,
        "the resumed sitting plays to the end of the flow"
    );
    expect(second.resumed, "and knows it resumed rather than founded");
    expect(second.refusals.empty(), "with nothing refused on the way in");
    expect_contains(
        second.printed,
        "CAMPAIGN  resumed from slot 'muster-road'",
        "the terminal says where the campaign came from"
    );
    expect(
        !second.boards.empty() && second.boards.front().excluded.size() == 1U &&
            second.boards.front().excluded.front() == fallen,
        "the road leaves the fallen rider off, and the client is told so"
    );
    expect_contains(
        second.printed,
        name_of(second.boards.front().roster, fallen) + " (DAWN GUARD) (dead)",
        "and the terminal says who is missing and why"
    );
    // The board the client drew has three units on it and not four. A fourth
    // label would be the rider the campaign buried, standing on a map that
    // lists them.
    expect(
        second.printed.find("  4) ") == std::string::npos,
        "and there is no fourth unit on the road for them to be"
    );
    expect_contains(
        second.printed,
        name_of(second.boards.front().roster, growth.member) +
            " (DAWN GUARD)  level " + decimal(growth.to_level),
        "the survivor takes the field as the character the first battle made "
        "them"
    );
    // The proof the recruitment is for: the ferryman came out of the slot, took
    // the field on a map the campaign reached after he joined, and is the one
    // who finished it.
    expect(
        campaign::is_deployable(
            state_in_slot(package, device, "muster-road"), joined.member
        ),
        "the recruit came back out of the slot as a member of the company"
    );
    expect(
        !second.boards.empty() &&
            second.boards.front().binding.battle_of(joined.member).value != 0U,
        "and stands on the road, bound to who he is"
    );
    expect(
        !second.aftermaths.empty() &&
            second.aftermaths.front().progression.level_ups.size() == 1U &&
            second.aftermaths.front().progression.level_ups.front().member ==
                joined.member,
        "and is the one the road's experience went to"
    );
    expect_contains(
        second.printed, "THE END", "and the muster road reaches its terminal node"
    );
}

// Somebody is left behind, and the board says so.
//
// The sitting above exercises the verbs without moving a number, which is what
// makes the battle it fights comparable to the one it always fought. This is the
// other half: a company the player narrowed on purpose, and a board with one
// fewer rider on it because of a decision rather than a death.
void a_benched_member_is_left_off_the_board() {
    const pf::LoadedPackage package = compile_the_maintained_demo();
    if (failures != 0) return;

    storage::FilesystemSlotStorage device(GRANDLEON_TERMINAL_TEST_ROOT);
    (void)device.erase("benched");
    const Sitting sitting =
        play(package, device, "benched", false, benching_script);
    expect(
        sitting.status == client::CampaignSessionError::none,
        "the sitting ends without the session refusing anything"
    );
    expect(
        sitting.gestures.size() == 1U && static_cast<bool>(sitting.gestures[0]),
        "benching a member is one committed gesture"
    );
    if (sitting.gestures.empty() || sitting.boards.empty()) return;
    expect(
        sitting.gestures[0].batch.operations.size() == 1U &&
            sitting.gestures[0].batch.operations.front().kind ==
                campaign::OutcomeOperationKind::set_availability,
        "and one operation, which is the availability the roster already had a "
        "word for"
    );
    const campaign::PersistentEntityId benched =
        sitting.gestures[0].batch.operations.front().subject;
    expect_contains(
        sitting.printed, " sits the next board out.",
        "the terminal says who is staying behind"
    );

    // The board the roster published. Not "the client drew fewer": the member
    // is in `excluded`, which is the very list the dead appear in, reached
    // through the exclusion pass that already existed.
    const client::CampaignBoard& board = sitting.boards.front();
    expect(
        board.excluded.size() == 1U && board.excluded.front() == benched,
        "the roster left exactly the benched member off the crossing"
    );
    expect_contains(
        sitting.printed,
        name_of(board.roster, benched) + " (DAWN GUARD) (retired)",
        "and the terminal says who is missing and, in the roster's own word, "
        "why"
    );
    expect(
        sitting.printed.find("  3) ") == std::string::npos,
        "the board has two units on it and not three"
    );
    // And it is in the slot, because a management gesture is written when it is
    // made. A campaign resumed here resumes with the same line.
    const campaign::CampaignState saved =
        state_in_slot(package, device, "benched");
    const campaign::PersistentUnit* const sat_out =
        campaign::find_unit(saved, benched);
    expect(
        sat_out != nullptr &&
            sat_out->availability == campaign::Availability::retired,
        "and the slot holds the decision, written when it was made rather than "
        "when the board was taken"
    );
}

// A company with nobody left to send, and what happens next.
//
// `RosterError::side_emptied` was reachable before only by losing everybody.
// Benching makes it an ordinary mistake, so it has to be an ordinary mistake:
// the board is refused by the roster's own name, nothing commits, and the stage
// is still standing when the player fields somebody again.
void a_company_that_benched_everybody_is_told_and_stands() {
    const pf::LoadedPackage package = compile_the_maintained_demo();
    if (failures != 0) return;

    storage::FilesystemSlotStorage device(GRANDLEON_TERMINAL_TEST_ROOT);
    (void)device.erase("emptied");
    const Sitting sitting =
        play(package, device, "emptied", false, emptied_script);
    expect(
        sitting.status == client::CampaignSessionError::none,
        "the campaign is not lost because a line could not take the field"
    );
    expect(
        sitting.gestures.size() == 3U,
        "two benches and the fielding that undid one of them"
    );
    expect(
        sitting.managements.size() == 2U,
        "the stage opened, refused a board, and opened again at the same node"
    );
    expect(
        sitting.managements.size() == 2U &&
            sitting.managements.back().refused ==
                grandleon::campaign_runtime::RosterError::side_emptied,
        "and the second time it says why it is still standing, in the roster's "
        "own word"
    );
    expect_contains(
        sitting.printed,
        std::string("that line could not take the field: ") +
            std::string(grandleon::campaign_runtime::roster_error_name(
                grandleon::campaign_runtime::RosterError::side_emptied
            )),
        "which the terminal repeats rather than paraphrases"
    );
    expect(
        sitting.boards.size() == 1U && sitting.boards.front().excluded.size() == 1U,
        "and the board that was finally published fields the rider who was "
        "fielded again, without the one who was not"
    );
}

// A slot that is not a save is refused in the envelope's own words, and the
// campaign the session was already holding is what gets played. The property
// belongs to `load_campaign_migrated_into` and is proved in `tests/campaign`;
// what is proved here is that a client surfaces it instead of dying on it.
void a_damaged_slot_is_named_and_survived() {
    const pf::LoadedPackage package = compile_the_maintained_demo();
    if (failures != 0) return;

    storage::FilesystemSlotStorage device(GRANDLEON_TERMINAL_TEST_ROOT);
    const std::vector<std::uint8_t> rubbish(48U, 0xA5U);
    expect(
        device.write("damaged", rubbish) == storage::StorageError::none,
        "a slot can hold bytes that are not a save"
    );

    const Sitting sitting = play(package, device, "damaged", true, "proceed\nquit\n");
    expect(
        sitting.status == client::CampaignSessionError::none,
        "the session does not fail; it plays the campaign it was holding"
    );
    expect(
        sitting.refusals.size() == 1U &&
            sitting.refusals.front().save != campaign::SaveError::none,
        "the envelope refused the bytes and said so"
    );
    if (sitting.refusals.empty()) return;
    expect_contains(
        sitting.printed,
        std::string("save ") +
            std::string(campaign::save_error_name(sitting.refusals.front().save)),
        "and the terminal repeats the envelope's own name for the refusal"
    );
    expect_contains(
        sitting.printed,
        "the campaign you were holding is untouched.",
        "and says the session survived it"
    );
    expect(
        !sitting.resumed && sitting.boards.size() == 1U &&
            sitting.boards.front().excluded.empty(),
        "which it did: a full roster takes the opening board"
    );

    // A slot that is not there at all is the device's refusal, not the
    // envelope's, and is reported as such.
    (void)device.erase("never-written");
    const Sitting missing =
        play(package, device, "never-written", true, "proceed\nquit\n");
    expect(
        !missing.refusals.empty() &&
            missing.refusals.front().storage == storage::StorageError::not_found,
        "an absent slot is the device's answer rather than the envelope's"
    );
    expect_contains(
        missing.printed,
        "storage not_found",
        "and the terminal says so in the device's own words"
    );

    // And a slot holding somebody else's campaign (valid bytes, valid state,
    // standing in a graph this flow does not contain) is the session's own
    // refusal, because no layer below it has anything to complain about.
    const grandleon::campaign_runtime::CampaignGraphLoad other =
        grandleon::campaign_runtime::load_campaign_graph(
            package, core::stable_content_id_v1("demo_campaign")
        );
    expect(static_cast<bool>(other), "the demo's other campaign is a graph");
    campaign::CampaignState elsewhere;
    expect(
        campaign::begin_campaign(elsewhere, other.source.graph) ==
            campaign::ProgressionError::none,
        "and can be stood in"
    );
    campaign::SavePackageRequirement requirement;
    requirement.package = package.game_id;
    requirement.content_revision = package.content_revision;
    expect(
        device.write(
            "elsewhere",
            campaign::save_campaign(
                campaign::make_campaign_save(elsewhere, {requirement})
            )
        ) == storage::StorageError::none,
        "and written to a slot as a save this build reads perfectly well"
    );
    const Sitting foreign = play(package, device, "elsewhere", true, "proceed\nquit\n");
    expect(
        !foreign.refusals.empty() && foreign.refusals.front().wrong_campaign &&
            foreign.refusals.front().save == campaign::SaveError::none,
        "nothing below the session refused it; the session did"
    );
    expect_contains(
        foreign.printed,
        "it holds a different campaign than this one",
        "and the terminal says which question it could not answer"
    );
    expect(
        !foreign.resumed && foreign.boards.size() == 1U &&
            foreign.boards.front().excluded.empty(),
        "and the muster road is played from its own beginning"
    );
}

// ---------------------------------------------------------------------------
// A board its author capped
// ---------------------------------------------------------------------------

// A narrow corridor: three riders in the company and a board that lets two of
// them onto it.
//
// The maintained demo authors no capacity, and it must not have to: the screen
// an uncapped board shows is the screen it always showed, which is asserted
// below against the demo itself. So the capped case is its own tiny project:
// three first-side placements, so a cap of two is a cap that can bind, and one
// opponent so the board is a board.
constexpr std::uint64_t capped_campaign = 110;

gc::GameSource capped_source() {
    gc::GameSource value;
    value.game_id[0] = 0x57U;
    value.title = "The corridor";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.item_types = {{20, "Consumable"}};
    value.classes = {{30, "Watch class", {6, 4, 1, 2, 3}, {10}}};
    value.weapons = {{40, "Sword", 10, 3, 1, 1}};
    value.items = {{50, "Tonic", 20, 5}};
    value.unit_types = {
        {60, "Watcher", 30, 80, {40}, {}},
        {61, "Raider", 30, 81, {40}, {}},
    };
    value.maps = {{70, "Corridor", 4, 3, std::vector<std::uint64_t>(12, 1)}};
    value.factions = {{80, "Blue"}, {81, "Red"}};
    value.objectives = {
        {90, "Hold", gc::ObjectiveKind::defeat_all_opponents}
    };
    value.encounters = {
        {
            100,
            "The corridor",
            70,
            {90},
            {
                {1000, 2000, 2000, 60, gc::EncounterSide::first, 0, 0},
                {1001, 2001, 2001, 60, gc::EncounterSide::first, 0, 1},
                {1002, 2002, 2002, 60, gc::EncounterSide::first, 0, 2},
                {1003, 2003, 0, 61, gc::EncounterSide::second, 3, 0},
            },
        },
    };
    // A cap and no region: the corridor says how many may come and nothing
    // about where they stand, which is the whole point of the two being
    // independent.
    value.encounters.front().deployment = {120, {}, 2};
    value.campaigns = {
        {
            capped_campaign,
            "The watch",
            111,
            {
                {111, gc::CampaignNodeKind::encounter, 100, {}, {112}, {}},
                {112, gc::CampaignNodeKind::terminal, 0, {}, {}, {}},
            },
            {
                {2000, "Ash", 60, 0},
                {2001, "Bram", 60, 0},
                {2002, "Cass", 60, 0},
            },
        },
    };
    return value;
}

pf::LoadedPackage compile_the_corridor() {
    const gc::CompileResult compiled = gc::compile(capped_source());
    expect(static_cast<bool>(compiled), "the corridor compiles");
    for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
        std::cerr << "compiler diagnostic: "
                  << gc::diagnostic_name(diagnostic.code) << ' '
                  << diagnostic.path << '\n';
    }
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and loads");
    return loaded.package;
}

// The same sitting as `play`, at a campaign of its own. Spelled separately so
// the demo's helper keeps saying the demo's campaign and nothing has to be
// threaded through the tests that were already right.
Sitting play_campaign(
    const pf::LoadedPackage& package,
    std::uint64_t campaign_id,
    storage::SlotStorage& device,
    const std::string& slot,
    const std::string& script
) {
    Sitting sitting;
    const std::unique_ptr<client::CampaignFrontEnd> front_end =
        desktop::make_terminal_front_end(false);
    WatchingNarrator watcher(*front_end);

    std::istringstream typed(script);
    std::ostringstream captured;
    std::streambuf* const previous_in = std::cin.rdbuf(typed.rdbuf());
    std::streambuf* const previous_out = std::cout.rdbuf(captured.rdbuf());

    client::CampaignSessionOptions options;
    options.slot = slot;
    sitting.status = client::run_persistent_campaign(
        package, campaign_id, *front_end, watcher, device, options
    );

    std::cin.rdbuf(previous_in);
    std::cout.rdbuf(previous_out);

    sitting.printed = captured.str();
    sitting.boards = watcher.boards;
    sitting.aftermaths = watcher.aftermaths;
    sitting.refusals = watcher.refusals;
    sitting.recruited = watcher.recruited;
    sitting.resumed = watcher.resumed;
    sitting.managements = watcher.managements;
    sitting.gestures = watcher.gestures;
    return sitting;
}

// Three in the company, two allowed out. The screen says both numbers, and the
// verb that would carry the company past the second is refused before a batch
// is built, under the roster's own word for it, because it is the refusal
// `join_campaign_roster` would give and this only reaches it a gesture earlier.
constexpr const char* capped_script =
    "bench 3\n"
    "roster\n"
    "field 3\n"
    "quit\n";

void a_capped_board_is_counted_and_its_cap_is_refused_by_name() {
    const pf::LoadedPackage package = compile_the_corridor();
    if (failures != 0) return;

    storage::FilesystemSlotStorage device(GRANDLEON_TERMINAL_TEST_ROOT);
    (void)device.erase("corridor");
    const Sitting sitting = play_campaign(
        package, capped_campaign, device, "corridor", capped_script
    );
    expect(
        sitting.status == client::CampaignSessionError::none,
        "the sitting ends without the session refusing anything"
    );
    expect(
        !sitting.managements.empty() && sitting.managements.front().capacity == 2U,
        "the stage publishes the cap the board's author wrote"
    );
    expect(
        !sitting.managements.empty() &&
            sitting.managements.front().fielded.size() == 3U,
        "and how many of the company would take it, which is one too many"
    );
    expect_contains(
        sitting.printed, "fielded 3 of 2",
        "so the screen says both numbers before anybody is benched"
    );
    expect_contains(
        sitting.printed, "fielded 2 of 2",
        "and says them again after, out of the campaign rather than out of "
        "arithmetic of its own"
    );

    // The refusal. Nothing was committed for it and nothing was saved, because
    // the verb never became a batch.
    expect_contains(
        sitting.printed,
        std::string(grandleon::campaign_runtime::roster_error_name(
            grandleon::campaign_runtime::RosterError::over_deployment_capacity
        )),
        "and a fielding that would carry the company past the cap is refused "
        "under the engine's own name for it"
    );
    expect(
        sitting.gestures.size() == 1U,
        "with the bench as the only committed gesture: the refused fielding "
        "built no batch at all"
    );
    expect(
        sitting.gestures.size() == 1U &&
            sitting.gestures.front().batch.operations.size() == 1U &&
            sitting.gestures.front().batch.operations.front().selector ==
                static_cast<std::uint8_t>(campaign::Availability::retired),
        "and that gesture is the bench, in the availability the roster already "
        "had a word for"
    );
}

// And the other half, which is the one a capped board must not cost: a company
// standing before a board that authors no cap sees the screen it always saw.
void an_uncapped_boards_screen_is_what_it_was() {
    const pf::LoadedPackage package = compile_the_maintained_demo();
    if (failures != 0) return;

    storage::FilesystemSlotStorage device(GRANDLEON_TERMINAL_TEST_ROOT);
    (void)device.erase("uncapped");
    const Sitting sitting = play(package, device, "uncapped", false, "quit\n");
    expect(
        !sitting.managements.empty() && sitting.managements.front().capacity == 0U,
        "the demo's crossing authors no capacity, and the stage reports none"
    );
    expect(
        !sitting.managements.empty() &&
            sitting.managements.front().fielded ==
                sitting.managements.front().placeable,
        "everybody the board places would take it, because nothing caps it"
    );
    expect(
        sitting.printed.find("fielded ") == std::string::npos,
        "and the screen says nothing extra: an uncapped board's listing is the "
        "listing it was before capacities existed"
    );
}

}  // namespace

int main() {
    a_campaign_is_played_narrated_saved_and_resumed();
    a_benched_member_is_left_off_the_board();
    a_company_that_benched_everybody_is_told_and_stands();
    a_capped_board_is_counted_and_its_cap_is_refused_by_name();
    an_uncapped_boards_screen_is_what_it_was();
    a_damaged_slot_is_named_and_survived();
    return failures == 0 ? 0 : 1;
}
