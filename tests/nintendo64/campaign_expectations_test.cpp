// SPDX-License-Identifier: MIT
#include <grandleon/campaign/migration.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/storage/byte_window_storage.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "campaign_expectations.h"

// What the Nintendo 64 campaign ROM is allowed to believe, derived here.
//
// The console autopilot asserts concrete numbers about the Tarnholt company:
// who is on it, what they carry, what the store holds after a gesture, how many
// batches the campaign has committed. A console assertion is only evidence if
// it was written down somewhere a person could not have adjusted it to make a
// run go green. This is that somewhere.
//
// It links the real engine and the real session, compiles the same checked-in
// project the ROM embeds, and drives exactly the sequence the autopilot script
// presses: found, read the three story nodes, take the mage's Field Tonic into
// the store, bench the second knight, save, and then resume through a real
// device, a real envelope and a real reload. Every constant in
// `campaign_expectations.h` is checked against what actually came out, and the
// ROM includes that same header.
//
// The device is `ByteWindowSlotStorage` over a window the size of the
// cartridge, so what the slot round-trips through here is the same directory
// the console writes to SRAM, layout included.

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;
namespace expect_ns = grandleon::tarnholt;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The name the cartridge announces itself by, derived from the same project
// the ROM embeds. The ROM reads it off its own parsed source; this is what
// says the string it will read is the shipped game's.
void derives_the_game_title(const gc::GameSource& source) {
    expect(
        source.title == expect_ns::game_title,
        "the title the console announces is the project's own"
    );
    // And it fits the band the title screen reserves for it, which is three
    // lines of thirty-four columns. A longer one is clipped on purpose rather
    // than allowed to run off the edge, so this is a fact about the shipped
    // game rather than a constraint on every game.
    expect(
        source.title.size() <= 34U * 3U,
        "and fits the three lines the title screen reserves without clipping"
    );
}

pf::LoadedPackage compile_tarnholt() {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) + "/games/tarnholt/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Tarnholt project opens");
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and parses");
    derives_the_game_title(parsed.source);
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and loads");
    return loaded.package;
}

std::uint32_t stacks_of(const std::vector<campaign::InventoryStack>& held) {
    return static_cast<std::uint32_t>(held.size());
}

std::uint32_t items_of(const std::vector<campaign::InventoryStack>& held) {
    std::uint32_t total = 0;
    for (const campaign::InventoryStack& stack : held) total += stack.quantity;
    return total;
}

const client::RosterEntry* member_in(
    const std::vector<client::RosterEntry>& roster,
    std::uint64_t member
) {
    for (const client::RosterEntry& entry : roster) {
        if (entry.member.value == member) return &entry;
    }
    return nullptr;
}

// The company as the campaign founds it, checked field by field against the
// header the ROM reads.
void the_founding_roster_is_what_the_rom_expects(
    const std::vector<client::RosterEntry>& roster,
    const std::vector<campaign::InventoryStack>& store
) {
    expect(
        static_cast<int>(roster.size()) == expect_ns::founding_roster_size,
        "the founded company is the size the ROM expects"
    );
    for (int index = 0; index < expect_ns::founding_roster_size; ++index) {
        const expect_ns::FoundingMember& want =
            expect_ns::founding_roster[index];
        const client::RosterEntry* entry = member_in(roster, want.member);
        expect(entry != nullptr, std::string("the campaign holds ") + want.name);
        if (entry == nullptr) continue;
        expect(
            entry->name == want.name,
            std::string("and calls them ") + want.name
        );
        expect(
            entry->availability == campaign::Availability::available,
            std::string(want.name) + " is deployable at founding"
        );
        expect(
            entry->progression.level == expect_ns::founding_level &&
                entry->progression.experience ==
                    expect_ns::founding_experience,
            std::string(want.name) + " starts at the level the ROM expects"
        );
        expect(
            stacks_of(entry->carried) == want.carried_stacks &&
                items_of(entry->carried) == want.carried_items,
            std::string(want.name) + " carries the authored starting kit"
        );
    }
    expect(
        static_cast<int>(store.size()) == expect_ns::founding_store_stacks,
        "the store holds what the campaign was founded with"
    );
    expect(
        items_of(store) == expect_ns::founding_store_tonics,
        "and it is the two tonics the guard marched out with"
    );
}

// A narrator that presses the autopilot's own gestures, so the sequence this
// test derives from is the sequence the console performs. Everything it is
// asked is answered from the company it was handed; nothing is remembered
// between calls, exactly as the console screen holds nothing.
class ScriptedManager final : public client::CampaignNarrator {
public:
    void campaign_begun(
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        std::string_view slot,
        bool resumed
    ) override {
        founding_roster_ = roster;
        founding_store_ = store;
        roster_ = roster;
        store_ = store;
        slot_ = std::string(slot);
        resumed_ = resumed;
        ++begun_;
    }

    void slot_refused(const client::SlotFailure& failure) override {
        expect(false, "the slot was refused: " + failure.slot);
    }

    void board_prepared(const client::CampaignBoard&) override {}
    void battle_aftermath(const client::BattleAftermath&) override {}
    void members_joined(const std::vector<client::RosterEntry>&) override {}

    void campaign_saved(std::string_view, storage::StorageError error) override {
        expect(
            error == storage::StorageError::none,
            "every save the session made was taken by the device"
        );
        ++saves_;
    }

    void management_opened(const client::CompanyManagement& company) override {
        ++opened_;
        expect(
            static_cast<int>(company.placeable.size()) ==
                expect_ns::fordlight_placeable,
            "the Fordlight places every founding member"
        );
    }

    void management_committed(const client::ManagementCommit& result) override {
        expect(
            static_cast<bool>(result),
            "the gesture the console presses commits"
        );
        expect(result.saved, "and writes the slot as it commits");
    }

    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override {
        roster_ = company.roster;
        store_ = company.store;
        client::ManagementIntent intent;
        if (resumed_) {
            // The second run looks and leaves. Everything it has to prove is
            // what the company already is.
            intent.verb = client::ManagementVerb::quit;
            return intent;
        }
        if (gesture_ == 0) {
            // The mage's Field Tonic, into the store.
            const client::RosterEntry* mage =
                member_in(company.roster, expect_ns::mage_member);
            expect(mage != nullptr, "the mage is on the company");
            expect(
                mage != nullptr && !mage->carried.empty(),
                "and is carrying the tonic the take is about"
            );
            ++gesture_;
            if (mage == nullptr || mage->carried.empty()) {
                intent.verb = client::ManagementVerb::quit;
                return intent;
            }
            intent.verb = client::ManagementVerb::take;
            intent.member = mage->member;
            intent.item = mage->carried.front().item;
            return intent;
        }
        if (gesture_ == 1) {
            ++gesture_;
            intent.verb = client::ManagementVerb::bench;
            intent.member =
                campaign::PersistentEntityId{expect_ns::benched_member};
            return intent;
        }
        intent.verb = client::ManagementVerb::quit;
        return intent;
    }

    // What the session handed over when the campaign began, kept apart from
    // what the gestures made of it: the founding claims and the resumed claims
    // are two different moments and must not be checked against one snapshot.
    [[nodiscard]] const std::vector<client::RosterEntry>& begun_roster() const {
        return founding_roster_;
    }
    [[nodiscard]] const std::vector<campaign::InventoryStack>& begun_store()
        const {
        return founding_store_;
    }
    [[nodiscard]] const std::vector<client::RosterEntry>& roster() const {
        return roster_;
    }
    [[nodiscard]] const std::vector<campaign::InventoryStack>& store() const {
        return store_;
    }
    [[nodiscard]] bool resumed() const noexcept { return resumed_; }
    [[nodiscard]] int opened() const noexcept { return opened_; }
    void resuming() noexcept { resumed_ = true; }

private:
    std::vector<client::RosterEntry> founding_roster_;
    std::vector<campaign::InventoryStack> founding_store_;
    std::vector<client::RosterEntry> roster_;
    std::vector<campaign::InventoryStack> store_;
    std::string slot_;
    bool resumed_{false};
    int gesture_{0};
    int begun_{0};
    int saves_{0};
    int opened_{0};
};

// A presenter that must never be asked anything: both runs leave the
// management stage before a board is published, exactly as the console does.
class UnusedPresenter final : public client::Presenter {
public:
    void present_dialogue(
        const grandleon::package_runtime::Dialogue& dialogue
    ) override {
        lines_ += static_cast<int>(dialogue.lines.size());
    }
    void battle_begins(
        const sim::EncounterSnapshot&,
        const client::Roster&,
        sim::Side,
        const std::vector<std::uint64_t>&
    ) override {
        expect(false, "no board is opened by either run");
    }
    void draw(const sim::EncounterSnapshot&, const client::Roster&) override {}
    void report(const sim::CommandResult&, const client::Roster&) override {}
    void refused(sim::CommandError) override {}
    void show_state(
        const sim::EncounterSnapshot&,
        std::uint64_t,
        const std::vector<sim::ObjectiveDefinition>&
    ) override {}
    void battle_ended(const sim::EncounterSnapshot&, std::uint64_t) override {}
    void campaign_ended() override {
        expect(false, "neither run reaches the end of the campaign");
    }
    [[nodiscard]] client::Intent next_intent(
        const sim::EncounterSnapshot&,
        const client::Roster&
    ) override {
        return {client::IntentKind::quit};
    }
    [[nodiscard]] int lines() const noexcept { return lines_; }

private:
    int lines_{0};
};

std::size_t committed_outcomes(
    const pf::LoadedPackage& package,
    storage::SlotStorage& device
) {
    const storage::StorageRead read = device.read(expect_ns::campaign_slot);
    expect(static_cast<bool>(read), "the slot reads back");
    if (!read) return 0;
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
    if (!restored) return 0;
    std::cerr << "  derived: the slot holds " << read.bytes.size()
              << " bytes and " << restored.save.state.applied_outcomes.size()
              << " committed outcome batches\n";
    return restored.save.state.applied_outcomes.size();
}

}  // namespace

// The portrait the opening scene's second line must draw, derived from the
// compiled package rather than written down beside the ROM.
//
// It asks, of the unit type the scene's *cast* names for that line, the two
// questions a board asks about a unit standing on it: which archetype the
// package resolved for that unit type, and which colour. So the console's
// assertion is a consequence of the content, and a recast character fails
// here, in seconds, before a ROM is built.
void derives_the_opening_portrait(const pf::LoadedPackage& package) {
    const auto scene = pr::load_dialogue(
        package, core::stable_content_id_v1("prologue_lines")
    );
    expect(static_cast<bool>(scene), "the opening scene decodes");
    if (!scene) return;
    const auto shown = pr::load_presentation(package);
    expect(static_cast<bool>(shown), "the presentation section decodes");
    if (!shown) return;

    expect(
        static_cast<int>(scene.dialogue.lines.size()) >
            expect_ns::opening_cast_line,
        "the opening scene has the line the console photographs"
    );
    if (static_cast<int>(scene.dialogue.lines.size()) <=
        expect_ns::opening_cast_line) {
        return;
    }
    const pr::DialogueLine& line =
        scene.dialogue.lines[expect_ns::opening_cast_line];
    const std::uint64_t* unit_type = scene.dialogue.speaker_unit_type(line);
    expect(
        unit_type != nullptr,
        "and the scene casts somebody for it — a line nobody is cast for "
        "would make the console's assertion about the fallback instead"
    );
    if (unit_type == nullptr) return;

    expect(
        shown.presentation.archetype_of_unit_type(*unit_type) ==
            expect_ns::opening_cast_archetype,
        "the archetype the console expects is the one the package resolves"
    );
    expect(
        shown.presentation.colour_of_unit_type(*unit_type) ==
            expect_ns::opening_cast_colour,
        "and so is the colour"
    );
    // The line the console can tell it from. Written down as the roster's
    // defaults because that is what a portrait falls back to for a speaker no
    // scene cast, and the console requires the two drawings to be
    // distinguishable before it believes a match between them.
    expect(
        expect_ns::opening_cast_archetype != expect_ns::opening_uncast_archetype ||
            expect_ns::opening_cast_colour != expect_ns::opening_uncast_colour,
        "the cast portrait and the portrait a bare speaker name would have "
        "chosen are two different drawings, which is what makes the console's "
        "assertion evidence rather than a coincidence"
    );
    expect(
        gc::archetype_index(line.speaker) == gc::archetype_unnamed,
        "and the speaker's own display name spells no archetype, so nothing "
        "but the cast could have produced the expected drawing"
    );
}

int main() {
    const pf::LoadedPackage package = compile_tarnholt();
    derives_the_opening_portrait(package);
    const std::uint64_t campaign_id =
        core::stable_content_id_v1(expect_ns::campaign_key);

    // The cartridge, on a host: the same directory over the same 32 KiB the
    // console writes to SRAM, so the slot this test round-trips through has the
    // console's layout and the console's budget.
    storage::VectorByteWindow cartridge(32U * 1024U, 0xFF);
    storage::ByteWindowSlotStorage device(
        cartridge, storage::ByteWindowSlotStorage::budget_for(32U * 1024U, 4U)
    );
    expect(device.available(), "the cartridge-sized device is available");

    client::CampaignSessionOptions options;
    options.slot = expect_ns::campaign_slot;
    options.resume = false;

    // Run one: the cartridge is empty, so the campaign is founded.
    {
        UnusedPresenter presenter;
        ScriptedManager narrator;
        const client::CampaignSessionError status =
            client::run_persistent_campaign(
                package, campaign_id, presenter, narrator, device, options
            );
        expect(
            status == client::CampaignSessionError::none,
            "the founding run completes"
        );
        expect(
            presenter.lines() == 12,
            "and reads the twelve authored cutscene lines the script presses A "
            "through"
        );
        expect(narrator.opened() == 1, "and opens the management stage once");
        the_founding_roster_is_what_the_rom_expects(
            narrator.begun_roster(), narrator.begun_store()
        );
    }

    const std::size_t after_managing = committed_outcomes(package, device);
    expect(
        static_cast<int>(after_managing) ==
            expect_ns::managed_committed_outcomes,
        "the slot holds the number of committed batches the ROM expects"
    );

    // Run two: a second session over the same cartridge, resuming.
    options.resume = true;
    {
        UnusedPresenter presenter;
        ScriptedManager narrator;
        narrator.resuming();
        const client::CampaignSessionError status =
            client::run_persistent_campaign(
                package, campaign_id, presenter, narrator, device, options
            );
        expect(
            status == client::CampaignSessionError::none,
            "the resuming run completes"
        );
        expect(
            presenter.lines() == 0,
            "and reads no cutscene lines, because the campaign stands on the "
            "board it was left before"
        );

        expect(
            static_cast<int>(narrator.begun_roster().size()) ==
                expect_ns::founding_roster_size,
            "the resumed company is whole"
        );
        const client::RosterEntry* mage =
            member_in(narrator.begun_roster(), expect_ns::mage_member);
        expect(mage != nullptr, "the mage survived the round trip");
        expect(
            mage != nullptr &&
                stacks_of(mage->carried) ==
                    expect_ns::resumed_mage_carried_stacks,
            "and is no longer carrying the tonic that was taken"
        );
        const client::RosterEntry* benched =
            member_in(narrator.begun_roster(), expect_ns::benched_member);
        expect(benched != nullptr, "the benched knight survived it too");
        expect(
            benched != nullptr &&
                static_cast<std::uint8_t>(benched->availability) ==
                    expect_ns::resumed_benched_availability,
            "and is still benched"
        );
        for (const client::RosterEntry& entry : narrator.begun_roster()) {
            if (entry.member.value == expect_ns::benched_member) continue;
            expect(
                static_cast<std::uint8_t>(entry.availability) ==
                    expect_ns::resumed_available_availability,
                "and nobody else was benched by the round trip"
            );
        }
        expect(
            static_cast<int>(narrator.begun_store().size()) ==
                expect_ns::resumed_store_stacks,
            "the store holds one kind of thing"
        );
        expect(
            items_of(narrator.begun_store()) == expect_ns::resumed_store_tonics,
            "and the mage's went into the same stack"
        );
    }

    if (failures == 0) {
        std::cerr << "the Nintendo 64 campaign expectations are derived and "
                     "hold\n";
    }
    return failures == 0 ? 0 : 1;
}
