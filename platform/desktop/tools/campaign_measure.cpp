// SPDX-License-Identifier: MIT
// How long is this campaign's fight, and what does it cost the company?
//
// An authored board is tuned by numbers: an enemy's health, its defense, how
// many of them there are and how far away they start. The only honest way
// to choose those numbers is to play the board and count. A bespoke driver
// thrown together for one measurement leaves a table of rounds and activations
// in a campaign's README that nobody can reproduce, so the counting lives here
// instead: one instrument, in the repository, run the same way every time.
//
// It plays a whole campaign with nobody at the controls and prints what each
// board cost:
//
//     BOARD <n> outcome=<won|lost|undecided> round=<r> activations=<a>
//     UNIT  <name> <health>/<maximum>
//
// It is the same `client::CampaignSession` over the same engine the consoles
// run, with the board's own counters read out of it. Both sides are
// `tactics::decide`, so a run is deterministic and two runs of the same package
// are the same numbers.
//
// The player's side is played under a behaviour the caller picks, because the
// two extremes bracket what a person will do:
//
//   --advancing  the company walks at the opposition (the default)
//   --holding    the company stands still and lets the opposition come
//
// A board's honest length is somewhere between the two, and a board that is
// instant under *both* is a board with no fight in it, which is exactly the
// defect this tool measures.
//
// Usage:
//   grandleon_campaign_measure <game.gpk> --campaign=<key> [--holding]
//                              [--slot=<name>] [--commands]
//
// `--commands` additionally prints every command the policy chose, in the
// vocabulary `grandleon_play` takes at its prompt, under the board facts a
// press table needs to turn it into gestures:
//
//     WHERE actor 1 2
//     PLAY move 3 5 2
//     WHERE actor 5 2
//     WHERE target 6 2
//     PLAY attack 3 7
//     WHERE actor 3 2
//     PICK weapons 1 ability 0 walk 1
//     PLAY ability 3 9 at 6 2
//
// which is what a press table for the Nintendo 64's autopilot is transcribed
// from. Deriving the presses from the same replay that produced the numbers is
// the point: a table hand-counted against a board that has since been retuned
// is a table that presses the wrong squares.
//
// The `PLAY` line alone cannot be transcribed, which is why the three above it
// exist. A press is a cursor walked to an absolute tile, and a command names
// tiles only where it is going: `move 3 5 2` says nothing about where 3 is
// standing now, and `attack 3 7` names no tile at all. `WHERE actor` is the
// acting unit's current tile at the moment of the command, and `WHERE target`
// the tile of the unit a command aims at. Those are the two coordinates a
// cursor has to be walked to. `PICK` is for the commands a character chooses
// out of what it carries rather than off the board: how many weapons it holds,
// which entry of its own ability or item list this command names, and whether
// its menu is still offering it a walk. Together those say how far down a menu
// built from those lists the choice sits. The walk is there because the row is
// not always: a character that has already walked this turn is not offered one,
// so a caret counted from the top of the menu is one row out for every
// character that moved before it cast. It is `sim::gesture_available`'s answer
// rather than this file's, for the same reason the menu asks rather than
// tracks. All of it is read out of the snapshot the policy decided against, so
// it describes the same instant the command does.

#include <grandleon/campaign_runtime/campaign_runtime.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/client/presenter.hpp>
#include <grandleon/client/session.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/storage/byte_window_storage.hpp>
#include <grandleon/tactics/policy.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace campaign = grandleon::campaign;
namespace campaign_runtime = grandleon::campaign_runtime;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace storage = grandleon::storage;
namespace tactics = grandleon::tactics;

namespace {

// A ceiling rather than a policy. `decide` always proposes something for a unit
// that can act, so a battle that does not end is a bug in the board or in the
// policy, and a measurement that ran forever would report nothing at all. The
// engine's own ceiling is 65,536 activations; stopping two orders of magnitude
// below it means this says which board would not end.
constexpr int command_ceiling = 4096;

[[nodiscard]] const char* outcome_word(sim::Outcome outcome) noexcept {
    switch (outcome) {
        case sim::Outcome::first_side_won: return "won";
        case sim::Outcome::second_side_won: return "lost";
        default: return "undecided";
    }
}

// The front end that plays without a player. Every presenter entry point it
// does not need is answered and discarded. A measurement is what it counts,
// and a front end that printed a board would be measuring a renderer.
class Measurer final : public client::CampaignFrontEnd {
  public:
    Measurer(tactics::Behavior company, bool print_commands) noexcept
        : company_(company), print_commands_(print_commands) {}

    // ----- Presenter: the battle -----------------------------------------

    void present_dialogue(const pr::Dialogue& dialogue) override {
        static_cast<void>(dialogue);
    }

    void battle_begins(
        const sim::EncounterSnapshot& snapshot,
        const client::Roster& roster,
        sim::Side player_side,
        const std::vector<std::uint64_t>& terrain
    ) override {
        static_cast<void>(terrain);
        static_cast<void>(roster);
        player_side_ = player_side;
        commands_ = 0;
        ++boards_;
        std::cout << "BOARD " << boards_ << " opens units "
                  << snapshot.units.size() << '\n';
    }

    void battle_definitions(
        const std::vector<sim::WeaponDefinition>& weapons,
        const std::vector<sim::AbilityDefinition>& abilities,
        const std::vector<sim::ItemDefinition>& items,
        const std::vector<sim::ObjectiveDefinition>& objectives
    ) override {
        weapons_ = weapons;
        abilities_ = abilities;
        static_cast<void>(items);
        static_cast<void>(objectives);
    }

    void draw(
        const sim::EncounterSnapshot& snapshot, const client::Roster& roster
    ) override {
        static_cast<void>(roster);
        // The last snapshot the session drew, kept so the ending report can
        // read the round and the activation count out of the engine rather
        // than counting them again beside it. `battle_ended` is handed a
        // snapshot too, and that is the one the numbers come from. This is
        // here so that a run stopped by the ceiling can still say where.
        last_ = snapshot;
    }

    void report(
        const sim::CommandResult& result, const client::Roster& roster
    ) override {
        static_cast<void>(result);
        static_cast<void>(roster);
    }

    void refused(sim::CommandError error) override {
        // A refusal is worth seeing: the policy proposing something the engine
        // will not take is the one way this replay stops meaning what a player
        // would have done.
        std::cout << "REFUSED " << static_cast<int>(error) << '\n';
        ++refusals_;
    }

    void show_state(
        const sim::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash,
        const std::vector<sim::ObjectiveDefinition>& objectives
    ) override {
        static_cast<void>(snapshot);
        static_cast<void>(canonical_hash);
        static_cast<void>(objectives);
    }

    void battle_ended(
        const sim::EncounterSnapshot& snapshot, std::uint64_t canonical_hash
    ) override {
        static_cast<void>(canonical_hash);
        std::cout << "BOARD " << boards_
                  << " outcome=" << outcome_word(snapshot.outcome)
                  << " round=" << snapshot.round
                  << " activations=" << snapshot.activation_count
                  << " commands=" << commands_ << '\n';
        // Every unit on both sides, not only the company's. A board is won by
        // an objective rather than by an empty half of it, and the two are
        // separate claims: "the company came through" is what the player's
        // rows say, and "there was nothing left to fight" is what the
        // opposition's rows say. Reporting only one of them is how a victory
        // declared over living opponents would go unnoticed here.
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            std::cout << "UNIT "
                      << (unit.side == player_side_ ? "ours" : "theirs") << ' '
                      << unit.id << ' ' << unit.health << '/'
                      << unit.maximum_health << '\n';
        }
    }

    void campaign_ended() override { ended_ = true; }

    [[nodiscard]] client::Intent next_intent(
        const sim::EncounterSnapshot& snapshot, const client::Roster& roster
    ) override {
        static_cast<void>(roster);
        if (++commands_ > command_ceiling) {
            std::cout << "CEILING the board did not end within "
                      << command_ceiling << " commands\n";
            return {client::IntentKind::quit};
        }
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.side != snapshot.active_side || !sim::on_board(unit)) {
                continue;
            }
            // Somebody who has already had their turn this round is not a
            // candidate. It matters under `side_blocks`, where the engine names
            // the side and leaves the choice open: without this the replay
            // keeps proposing the block's first character, is refused by name,
            // and never reaches the rest of the block. The two campaigns
            // outside this repository, both of which state `sideBlocks`, would
            // replay as a wall of refusals without it.
            if (unit.has_acted) continue;
            // And whoever holds an activation holds it alone, under the two
            // orders that hand one out. Inert under `side_blocks`, which names
            // no actor and commits to nobody: there a character that has walked
            // and not yet struck comes back round to this loop, and the policy
            // answers it with the strike or the wait its walk was for.
            if (snapshot.active_unit_id != 0 &&
                snapshot.active_unit_id != unit.id) {
                continue;
            }
            const tactics::Plan plan = tactics::decide(
                snapshot, unit.id, company_, {}, abilities_, weapons_
            );
            if (!plan.actionable) continue;
            if (print_commands_) print_command(snapshot, plan.command);
            return intent_for(plan.command);
        }
        return {client::IntentKind::none};
    }

    // ----- CampaignNarrator: the campaign ---------------------------------

    void campaign_begun(
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        std::string_view slot,
        bool resumed
    ) override {
        static_cast<void>(slot);
        static_cast<void>(resumed);
        std::cout << "COMPANY roster " << roster.size() << " store "
                  << store.size() << '\n';
    }

    void slot_refused(const client::SlotFailure& failure) override {
        std::cout << "SLOT refused storage "
                  << static_cast<int>(failure.storage) << '\n';
        ++refusals_;
    }

    void board_prepared(const client::CampaignBoard& board) override {
        static_cast<void>(board);
    }

    void battle_aftermath(const client::BattleAftermath& aftermath) override {
        std::cout << "AFTERMATH fallen " << aftermath.fallen.size() << '\n';
    }

    void members_joined(
        const std::vector<client::RosterEntry>& joined
    ) override {
        static_cast<void>(joined);
    }

    void campaign_saved(
        std::string_view slot, storage::StorageError error
    ) override {
        static_cast<void>(slot);
        if (error != storage::StorageError::none) ++refusals_;
    }

    void management_opened(const client::CompanyManagement& company) override {
        // Why a board would not take this company, in the roster's own word.
        // A measurement that stops has to say what stopped it: `board_rejected`
        // alone sends a reader to the wrong question, and the answers are not
        // alike: a cap is fixed by benching, a dead objective target is not
        // fixable at all and means the campaign is over.
        if (company.refused != campaign_runtime::RosterError::none &&
            company.refused != last_refusal_) {
            last_refusal_ = company.refused;
            std::cout << "REFUSED "
                      << campaign_runtime::roster_error_name(company.refused)
                      << " (company " << company.fielded.size()
                      << ", board fields "
                      << static_cast<int>(company.capacity) << ")\n";
        }
    }

    void management_committed(const client::ManagementCommit& result) override {
        static_cast<void>(result);
    }

    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override {
        // Nothing is bought or given. The measurement is of the boards, and a
        // company managed differently between two runs would be two different
        // companies fighting them.
        //
        // Benching is the one gesture this has to make, and only when a board
        // authors a deployment capacity smaller than the company. The engine
        // refuses an over-capacity company rather than trimming it, because
        // choosing who fights is a decision it will not take from a player.
        // So a headless run that benches nobody cannot open such a board at
        // all, and every board after it goes unmeasured.
        //
        // Which characters it sits down is arbitrary, and that is exactly why
        // it says so on the transcript: bench the archer rather than the
        // knight and the board plays differently, so a reader comparing two
        // runs has to be able to see that they fielded the same five. It takes
        // them from the end of the fielded list, which is recruitment order,
        // so the company that opens a campaign is the company that keeps
        // playing it.
        // Capacity zero is a board that authored no deployment region, which
        // is every board that takes the whole company. It is the absence of a
        // cap and not a cap of nothing.
        // Only one refusal is answerable from here. `over_deployment_capacity`
        // is a company too big for the board and benching is the answer;
        // everything else is the campaign having ended badly, whether an
        // objective naming somebody who is dead or a side with nobody left,
        // and no gesture at this screen repairs it. Proceeding would ask
        // again, be refused again, and spin.
        if (company.refused != campaign_runtime::RosterError::none &&
            company.refused !=
                campaign_runtime::RosterError::over_deployment_capacity) {
            std::cout << "STOPPED the company cannot take this board\n";
            return {client::ManagementVerb::quit};
        }
        if (company.capacity != 0 &&
            company.fielded.size() > company.capacity) {
            const campaign::PersistentEntityId sits_down =
                company.fielded.back();
            std::string called = std::to_string(sits_down.value);
            for (const client::RosterEntry& entry : company.roster) {
                if (entry.member.value == sits_down.value && !entry.name.empty()) {
                    called = entry.name;
                    break;
                }
            }
            std::cout << "BENCH " << called << " (this board fields "
                      << static_cast<int>(company.capacity) << " of "
                      << company.fielded.size() << ")\n";
            return {client::ManagementVerb::bench, sits_down, {}};
        }
        return {client::ManagementVerb::proceed};
    }

    // The last roster refusal reported, so a management stage that is asked
    // repeatedly says why once rather than once per ask.
    campaign_runtime::RosterError last_refusal_{
        campaign_runtime::RosterError::none};

    [[nodiscard]] int refusals() const noexcept { return refusals_; }
    [[nodiscard]] bool ended() const noexcept { return ended_; }

  private:
    // The unit a command names, or nothing. A command can name a unit that is
    // no longer on the board. Nothing here does, because the policy decides
    // against this same snapshot, but a caller reading the output is owed the
    // difference between "standing there" and "not said" rather than a
    // coordinate invented to fill the line.
    [[nodiscard]] static const sim::UnitSnapshot* standing(
        const sim::EncounterSnapshot& snapshot, sim::UnitId unit
    ) noexcept {
        for (const sim::UnitSnapshot& candidate : snapshot.units) {
            if (candidate.id == unit) return &candidate;
        }
        return nullptr;
    }

    static void print_where(
        const char* role, const sim::UnitSnapshot& unit
    ) {
        std::cout << "WHERE " << role << ' ' << unit.position.x << ' '
                  << unit.position.y << '\n';
    }

    // Which entry of what the character carries this command names, for the
    // two commands that name one. A move or a strike is chosen off the board
    // and has no list behind it, so neither prints a line: an index of zero
    // against no list would read as a choice that was never made.
    static void print_pick(
        const sim::EncounterSnapshot& snapshot,
        const sim::UnitSnapshot& actor,
        const sim::Command& command,
        const std::vector<sim::WeaponDefinition>& weapons,
        const std::vector<sim::AbilityDefinition>& abilities
    ) {
        const auto entry =
            [](const std::vector<sim::ContentId>& carried, sim::ContentId named
            ) -> int {
            for (std::size_t index = 0; index < carried.size(); ++index) {
                if (carried[index] == named) return static_cast<int>(index);
            }
            return -1;
        };
        int index = -1;
        const char* list = nullptr;
        if (command.type == sim::CommandType::ability) {
            list = "ability";
            index = entry(actor.ability_ids, command.ability_id);
        } else if (command.type == sim::CommandType::use_item) {
            list = "item";
            index = entry(actor.item_ids, command.item_id);
        }
        if (list == nullptr || index < 0) return;
        const bool walk = sim::gesture_available(
            snapshot, actor.id, {sim::Gesture::walk, 0, 0}, weapons, abilities
        );
        std::cout << "PICK weapons " << actor.weapon_ids.size() << ' ' << list
                  << ' ' << index << " walk " << (walk ? 1 : 0) << '\n';
    }

    void print_command(
        const sim::EncounterSnapshot& snapshot, const sim::Command& command
    ) const {
        const sim::UnitSnapshot* const actor =
            standing(snapshot, command.unit_id);
        if (actor != nullptr) print_where("actor", *actor);
        if (command.type == sim::CommandType::attack) {
            const sim::UnitSnapshot* const target =
                standing(snapshot, command.target_id);
            if (target != nullptr) print_where("target", *target);
        }
        if (actor != nullptr) {
            print_pick(snapshot, *actor, command, weapons_, abilities_);
        }
        switch (command.type) {
            case sim::CommandType::move:
                std::cout << "PLAY move " << command.unit_id << ' '
                          << command.destination.x << ' '
                          << command.destination.y << '\n';
                break;
            case sim::CommandType::attack:
                std::cout << "PLAY attack " << command.unit_id << ' '
                          << command.target_id << '\n';
                break;
            case sim::CommandType::ability:
                std::cout << "PLAY ability " << command.unit_id << ' '
                          << command.ability_id << " at "
                          << command.destination.x << ' '
                          << command.destination.y << '\n';
                break;
            case sim::CommandType::use_item:
                std::cout << "PLAY use " << command.unit_id << ' '
                          << command.item_id << '\n';
                break;
            default:
                std::cout << "PLAY wait " << command.unit_id << '\n';
                break;
        }
    }

    [[nodiscard]] static client::Intent intent_for(const sim::Command& command) {
        client::Intent intent;
        intent.unit_id = command.unit_id;
        switch (command.type) {
            case sim::CommandType::move:
                intent.kind = client::IntentKind::move_to;
                intent.destination = command.destination;
                break;
            case sim::CommandType::attack:
                intent.kind = client::IntentKind::attack;
                intent.target_id = command.target_id;
                intent.weapon_id = command.weapon_id;
                break;
            case sim::CommandType::ability:
                intent.kind = client::IntentKind::ability;
                intent.destination = command.destination;
                intent.ability_id = command.ability_id;
                break;
            case sim::CommandType::use_item:
                intent.kind = client::IntentKind::use_item;
                intent.item_id = command.item_id;
                break;
            default:
                intent.kind = client::IntentKind::wait;
                break;
        }
        return intent;
    }

    std::vector<sim::WeaponDefinition> weapons_;
    std::vector<sim::AbilityDefinition> abilities_;
    sim::EncounterSnapshot last_{};
    tactics::Behavior company_{tactics::Behavior::pursue};
    sim::Side player_side_{sim::Side::first};
    bool print_commands_{false};
    int commands_ = 0;
    int boards_ = 0;
    int refusals_ = 0;
    bool ended_ = false;
};

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    std::string campaign_key;
    std::string slot = "measure";
    tactics::Behavior company = tactics::Behavior::pursue;
    bool print_commands = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--holding") {
            company = tactics::Behavior::hold;
        } else if (argument == "--advancing") {
            company = tactics::Behavior::pursue;
        } else if (argument == "--commands") {
            print_commands = true;
        } else if (argument.rfind("--campaign=", 0) == 0) {
            campaign_key = argument.substr(11);
        } else if (argument.rfind("--slot=", 0) == 0) {
            slot = argument.substr(7);
        } else if (path.empty()) {
            path = argument;
        }
    }
    if (path.empty() || campaign_key.empty()) {
        std::cerr << "usage: grandleon_campaign_measure <game.gpk> "
                     "--campaign=<key> [--holding|--advancing] "
                     "[--slot=<name>] [--commands]\n";
        return 64;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "cannot read " << path << '\n';
        return 66;
    }
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
    const auto loaded = pf::load_mock_package(
        bytes,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 4096, 4'000'000}
    );
    if (loaded.error != pf::Error::none) {
        std::cerr << "the package did not open: "
                  << pf::error_name(loaded.error) << '\n';
        return 65;
    }

    // The cartridge, in memory. A campaign has to be kept for the roster to
    // travel between boards, and the numbers this tool reports are numbers
    // about a company that carries its wounds forward, so a measurement over
    // a session with no slot would be measuring a different game.
    storage::VectorByteWindow window(32U * 1024U, 0xFF);
    storage::ByteWindowSlotStorage device(
        window, storage::ByteWindowSlotStorage::budget_for(32U * 1024U, 4)
    );

    Measurer measurer(company, print_commands);
    client::CampaignSessionOptions options;
    options.slot = slot.c_str();
    options.resume = false;
    options.player_side = sim::Side::first;
    const auto status = client::run_persistent_campaign(
        loaded.package,
        core::stable_content_id_v1(campaign_key),
        measurer,
        measurer,
        device,
        options
    );
    if (status != client::CampaignSessionError::none) {
        std::cerr << "the campaign did not run: "
                  << client::campaign_session_error_name(status) << '\n';
        return 65;
    }
    std::cout << "RESULT " << (measurer.refusals() == 0 ? "clean" : "refused")
              << " ended=" << (measurer.ended() ? 1 : 0) << '\n';
    return measurer.refusals() == 0 ? 0 : 1;
}
