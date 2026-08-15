// SPDX-License-Identifier: MIT
#include <grandleon/desktop/presenters.hpp>

#include <grandleon/sheet/unit_sheet.hpp>

#include <algorithm>
#include <memory>
#include <utility>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace grandleon::desktop {
namespace {

namespace sim = simulation;

// What a roster member is called in every line the campaign prints: the name
// the author gave them, and after it what they are, because a player who has
// just lost somebody wants both. The persistent id leads only when the author
// wrote no name at all, which the compiler refuses to let happen but a package
// compiled before rosters were authorable still can.
//
// The name comes back folded, through the same resolver the battle log names a
// character by. That is what stops this client contradicting itself: every
// other name it prints is already folded, whether the unit type in the brackets
// beside this one, the weapons, the pack or any line of a sheet, so an unfolded
// person's name was the only mixed-case string on the screen and the only one
// the log and the aftermath could spell two ways.
std::string member_name(
    const client::RosterEntry& entry,
    const package_format::LoadedPackage* package
) {
    std::ostringstream text;
    const grandleon::sheet::ContentName called =
        grandleon::sheet::unit_type_name(package, entry.unit_type.stable_id);
    if (entry.name.empty()) {
        text << '#' << entry.member.value << ' ' << called.c_str();
        return text.str();
    }
    const grandleon::sheet::ContentName person =
        grandleon::sheet::person_name(entry.name.c_str());
    text << person.c_str() << " (" << called.c_str() << ')';
    return text.str();
}

const client::RosterEntry* find_member(
    const std::vector<client::RosterEntry>& roster,
    campaign::PersistentEntityId member
) {
    for (const client::RosterEntry& entry : roster) {
        if (entry.member == member) return &entry;
    }
    return nullptr;
}

}  // namespace

// Draws with ANSI text and reads one line at a time.
//
// Line input rather than raw terminal mode is deliberate: it costs a little
// immediacy and buys a client that can be driven from a file, which makes a
// whole playthrough something a test asserts rather than something a person
// watches.
class TerminalPresenter final : public client::CampaignFrontEnd {
public:
    TerminalPresenter(
        bool colour, const package_format::LoadedPackage* package
    )
        : colour_(colour), package_(package) {}

    void present_dialogue(const package_runtime::Dialogue& dialogue) override {
        std::cout << '\n';
        for (const package_runtime::DialogueLine& line : dialogue.lines) {
            std::cout << "  " << paint(line.speaker, "1;33") << ": "
                      << line.text << '\n';
        }
        std::cout << '\n';
    }

    void battle_begins(
        const sim::EncounterSnapshot& snapshot,
        const Roster& roster,
        sim::Side player_side,
        const std::vector<std::uint64_t>&
    ) override {
        player_side_ = player_side;
        // Which colour on the board is theirs, said once. Every line after
        // this one says "your side" and "the enemy", so this is the single
        // place the two vocabularies are tied together.
        std::cout << "Battle begins. Your side is "
                  << (player_side == sim::Side::first ? paint("blue", "1;34")
                                                      : paint("red", "1;31"))
                  << ". Type 'help' for commands.\n";
        board_ = roster;
        remember_board(snapshot);
        list_units(snapshot, roster);
    }

    // Kept because `info` prints a character's whole sheet, and a snapshot
    // names only the identities a unit holds: what a weapon's band and accuracy
    // are, and how far an ability reaches, live in these registries.
    void battle_definitions(
        const std::vector<sim::WeaponDefinition>& weapons,
        const std::vector<sim::AbilityDefinition>& abilities,
        const std::vector<sim::ItemDefinition>& items,
        const std::vector<sim::ObjectiveDefinition>& objectives
    ) override {
        weapons_ = weapons;
        abilities_ = abilities;
        items_ = items;
        (void)objectives;
    }

    // The region, once, before the first frame of the phase. The tiles are
    // listed as well as lit, because a terminal player types coordinates and
    // reading them off a grid of dots is not the same as being told them.
    void deployment_begins(
        const sim::EncounterSnapshot& snapshot,
        const Roster& roster,
        const std::vector<sim::Position>& zone
    ) override {
        zone_ = zone;
        std::cout << "\nDeployment. Stand your line before the battle opens; "
                     "the ground marked "
                  << paint("+", "1;36") << " is yours.\n";
        for (const sim::UnitSnapshot& value : snapshot.units) {
            if (!sim::is_deployable(snapshot, value)) continue;
            std::cout << "  " << roster.label(value.id) << " may be placed:";
            for (const sim::Position tile : sim::deployable_tiles(
                     snapshot, value.id
                 )) {
                std::cout << ' ' << tile.x << ',' << tile.y;
            }
            std::cout << '\n';
        }
        std::cout << "  deploy <n> <x> <y> to place, begin to open the "
                     "battle.\n";
    }

    void draw(
        const sim::EncounterSnapshot& snapshot,
        const Roster& roster
    ) override {
        remember_board(snapshot);
        std::cout << "\n    ";
        for (std::uint16_t x = 0; x < snapshot.width; ++x) {
            std::cout << (x % 10) << ' ';
        }
        std::cout << '\n';
        for (std::uint16_t y = 0; y < snapshot.height; ++y) {
            std::cout << "  " << (y % 10) << ' ';
            for (std::uint16_t x = 0; x < snapshot.width; ++x) {
                const sim::UnitSnapshot* occupant =
                    occupant_at(snapshot, x, y);
                if (occupant == nullptr) {
                    // An empty region tile is lit while the phase is open, and
                    // is an ordinary tile the moment it closes: the region is
                    // a rule about the phase and drawing it afterwards would
                    // claim otherwise.
                    const bool in_zone =
                        snapshot.deploying &&
                        std::find(
                            zone_.begin(), zone_.end(),
                            sim::Position{
                                static_cast<std::int16_t>(x),
                                static_cast<std::int16_t>(y)
                            }
                        ) != zone_.end();
                    std::cout << paint(in_zone ? "+" : ".",
                                       in_zone ? "1;36" : "2")
                              << ' ';
                } else {
                    std::cout << paint(
                        roster.label(occupant->id),
                        occupant->side == sim::Side::first ? "1;34" : "1;31"
                    ) << ' ';
                }
            }
            std::cout << '\n';
        }
        std::cout << '\n';
    }

    void report(
        const sim::CommandResult& result,
        const Roster& roster
    ) override {
        for (const sim::Event& event : result.events) {
            switch (event.type) {
                case sim::EventType::unit_moved:
                    std::cout << "    " << roster.label(event.unit_id)
                              << " moved to " << event.position.x << ','
                              << event.position.y << '\n';
                    break;
                case sim::EventType::unit_waited:
                    std::cout << "    " << roster.label(event.unit_id)
                              << " waited\n";
                    break;
                case sim::EventType::unit_damaged:
                    std::cout << "    " << roster.label(event.related_unit_id)
                              << " hit " << roster.label(event.unit_id)
                              << " for " << event.amount << '\n';
                    break;
                case sim::EventType::attack_missed:
                    std::cout << "    " << roster.label(event.related_unit_id)
                              << " missed " << roster.label(event.unit_id)
                              << '\n';
                    break;
                case sim::EventType::unit_restored:
                    std::cout << "    " << roster.label(event.related_unit_id)
                              << " healed " << roster.label(event.unit_id)
                              << " for " << event.amount << '\n';
                    break;
                // Somebody died, said at the moment it happened and with their
                // name on it. A bare "3 was defeated" would name nobody:
                // `Roster::label` is the one-based place a unit takes in the
                // board's list, and it is there so that a player has something
                // short to type at the prompt rather than so that they have
                // something to grieve. Without a name here, the first place a
                // name and a loss appear together is the company screen, which
                // can be a whole battle later.
                //
                // The name leads and the typing label follows it in brackets,
                // because the two are answers to different questions: who just
                // died, and which unit that was in the commands above and
                // below this line. Both are worth having and the order is the
                // order of what matters.
                //
                // "died" or "fell", by the rule the campaign is played under,
                // and that is the whole point of having a word for it. Under
                // the permanent rule this is a death and a player is owed the
                // word. Under the recoverable rule they are down and out of
                // this battle and they are coming back, and calling it a death
                // would be a lie the aftermath screen contradicts.
                case sim::EventType::unit_defeated:
                    std::cout << "    " << character_called(event.unit_id)
                              << " (" << roster.label(event.unit_id) << ") "
                              << fall_word(event.unit_id) << '\n';
                    break;
                // And the blow that did not do it, where the content asked for
                // a floor under everybody's health. It follows the damage line
                // rather than replacing it: the engine emits both, in that
                // order, so a reader is told what was taken before being told
                // it was not enough. It is deliberately not spelled like
                // a defeat, because a character who held on is a character who
                // is still standing there to be commanded.
                case sim::EventType::unit_endured:
                    std::cout << "    " << character_called(event.unit_id)
                              << " (" << roster.label(event.unit_id)
                              << ") held on\n";
                    break;
                case sim::EventType::item_used:
                    std::cout << "    " << roster.label(event.unit_id)
                              << " used "
                              << grandleon::sheet::item_name(event.content_id)
                              << ", " << event.amount << " left\n";
                    break;
                case sim::EventType::item_dropped:
                    std::cout << "    " << roster.label(event.unit_id)
                              << " left behind "
                              << grandleon::sheet::item_name(event.content_id)
                              << ", claimed by "
                              << roster.label(event.related_unit_id) << '\n';
                    break;
                case sim::EventType::unit_arrived:
                    // The tile named is the one the wave actually took, which
                    // is the tile the content asked for or the nearest one it
                    // could stand on when somebody was holding that, so a
                    // reader of the log is never told a tile nobody is on.
                    std::cout << "    " << roster.label(event.unit_id)
                              << " arrived at " << event.position.x << ','
                              << event.position.y << " as round "
                              << event.amount << " began\n";
                    break;
                case sim::EventType::unit_talked:
                    // "left the field", never "was defeated". Departure and
                    // death are two different facts, and this line is where a
                    // reader of the log is told which one happened.
                    std::cout << "    " << roster.label(event.related_unit_id)
                              << " talked " << roster.label(event.unit_id)
                              << " off the field\n";
                    break;
                case sim::EventType::unit_deployed:
                    std::cout << "    " << roster.label(event.unit_id)
                              << " takes position at " << event.position.x
                              << ',' << event.position.y << '\n';
                    break;
                case sim::EventType::deployment_ended:
                    std::cout << "    the line is set; the battle begins\n";
                    break;
                case sim::EventType::activation_ended:
                    break;
                case sim::EventType::encounter_completed:
                    std::cout << "    "
                              << (event.outcome == sim::Outcome::first_side_won
                                      ? side_name(sim::Side::first)
                                      : side_name(sim::Side::second))
                              << " won the battle\n";
                    break;
            }
        }
    }

    void refused(sim::CommandError error) override {
        std::cout << "  refused: " << sim::error_name(error) << '\n';
    }

    void show_state(
        const sim::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash,
        const std::vector<sim::ObjectiveDefinition>& objectives
    ) override {
        std::cout << "  turn: " << side_name(snapshot.active_side)
                  << "  round " << snapshot.round
                  << "  activations " << snapshot.activation_count
                  << "\n  canonical hash: " << std::hex << canonical_hash
                  << std::dec << '\n';
        // The battle's own terms, and the progress against them. Printed only
        // where a board authors objectives at all: a board that authors none
        // prints exactly the two lines above and nothing else.
        for (const sim::ObjectiveDefinition& objective : objectives) {
            std::cout << "  objective: "
                      << grandleon::sheet::objective_name(objective.kind)
                      << " (" << side_name(objective.side) << ')';
            if (objective.kind == sim::ObjectiveKind::survive_rounds) {
                char line[32];
                grandleon::sheet::round_line(
                    snapshot.round, objective.round_count, line, sizeof line
                );
                std::cout << "  " << line;
            }
            std::cout << '\n';
        }
    }

    void battle_ended(
        const sim::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash
    ) override {
        std::cout << (snapshot.outcome == sim::Outcome::first_side_won
                          ? side_name(sim::Side::first)
                          : side_name(sim::Side::second))
                  << " won.  canonical hash: " << std::hex << canonical_hash
                  << std::dec << '\n';
    }

    void campaign_ended() override {
        std::cout << paint("THE END", "1;33")
                  << "  Thanks for playing.\n"
                  << "  github.com/lfelipe/grandleon\n";
    }

    // -----------------------------------------------------------------------
    // The campaign, between battles
    //
    // Every number printed below was handed over by the session, which was
    // handed it by `campaign_runtime` and `engine/campaign`. Nothing in this
    // file adds, divides, or decides anything: a level is the level the engine
    // recorded, a stat gain is the point the growth stream granted, and a death
    // is permanent because `record_permanent_death` was committed and not
    // because a terminal decided somebody looked dead.
    // -----------------------------------------------------------------------

    void campaign_begun(
        const std::vector<client::RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        std::string_view slot,
        bool resumed
    ) override {
        campaign_ = true;
        roster_ = roster;
        store_ = store;
        std::cout << '\n'
                  << paint("CAMPAIGN", "1;33") << "  "
                  << (resumed ? "resumed from" : "begun, saving to")
                  << " slot '" << slot << "'\n";
        print_roster();
    }

    void slot_refused(const client::SlotFailure& failure) override {
        // Four vocabularies, each printed in its owner's own words. A terminal
        // that translated `checksum_mismatch` into prose of its own would be a
        // terminal a bug report could not be read out of.
        std::cout << "  slot '" << failure.slot << "' refused:";
        if (failure.storage != storage::StorageError::none) {
            std::cout << " storage " << storage::storage_error_name(failure.storage);
        }
        if (failure.migration != campaign::MigrationError::none) {
            std::cout << " migration "
                      << campaign::migration_error_name(failure.migration);
        }
        if (failure.save != campaign::SaveError::none) {
            std::cout << " save " << campaign::save_error_name(failure.save);
        }
        if (failure.state != campaign::StateError::none) {
            std::cout << " state " << campaign::state_error_name(failure.state);
        }
        if (failure.wrong_campaign) {
            std::cout << " it holds a different campaign than this one";
        }
        std::cout << "\n  the campaign you were holding is untouched.\n";
    }

    void board_prepared(const client::CampaignBoard& board) override {
        campaign_ = true;
        roster_ = board.roster;
        store_ = board.store;
        binding_ = board.binding;
        // What this campaign has decided a fall costs, kept so that the battle
        // can be narrated in the rule's own words. A client that guessed would
        // sooner or later tell a player somebody had died who was about to walk
        // back onto the next board.
        loss_ = board.character_loss;
        if (board.excluded.empty()) {
            std::cout << "\n  Everyone the roster can still field takes this "
                         "board.\n";
            return;
        }
        std::cout << "\n  Left off this board by the roster:\n";
        for (const campaign::PersistentEntityId member : board.excluded) {
            const client::RosterEntry* const entry =
                find_member(board.roster, member);
            if (entry == nullptr) continue;
            std::cout << "    " << member_name(*entry, package_) << " ("
                      << campaign::availability_name(entry->availability)
                      << ")\n";
        }
    }

    void battle_aftermath(const client::BattleAftermath& aftermath) override {
        roster_ = aftermath.roster;
        store_ = aftermath.store;
        std::cout << '\n' << paint("AFTER THE BATTLE", "1;33") << '\n';

        for (const campaign::PersistentEntityId member : aftermath.fallen) {
            const client::RosterEntry* const entry =
                find_member(aftermath.roster, member);
            if (entry == nullptr) continue;
            // The same word the battle said when it happened, because the two
            // lines are about one event and a player who read the first should
            // recognise the second. Which word that is belongs to the campaign
            // and not to this screen, so it is read off the aftermath rather
            // than decided here.
            const bool buried = aftermath.character_loss ==
                                package_runtime::CharacterLoss::permanent;
            std::cout << "  " << member_name(*entry, package_) << ", unit "
                      << board_.label(
                             aftermath.binding.battle_of(member).value
                         )
                      << " on that board, "
                      << (buried ? "died, and will not take the field again."
                                 : "fell, and was carried off; they rejoin the "
                                   "company with what they were carrying.")
                      << '\n';
        }

        for (const campaign_runtime::LevelUp& growth : aftermath.progression.level_ups) {
            const client::RosterEntry* const entry =
                find_member(aftermath.roster, growth.member);
            if (entry == nullptr) continue;
            std::cout << "  " << member_name(*entry, package_) << " reached level "
                      << growth.to_level << " (from " << growth.from_level
                      << "), " << experience_granted(aftermath, growth.member)
                      << " experience earned, "
                      << entry->progression.experience << " in all\n";
            bool any = false;
            for (std::size_t index = 0; index < campaign::growable_stat_count;
                 ++index) {
                const std::uint16_t points = growth.points[index];
                if (points == 0U) continue;
                any = true;
                std::cout << "    +" << points << ' '
                          << campaign::growable_stat_name(
                                 static_cast<campaign::GrowableStat>(index)
                             )
                          << '\n';
            }
            if (!any) {
                std::cout << "    and the growth rolls granted nothing at "
                             "all\n";
            }
        }

        print_inventory_changes(aftermath);

        if (aftermath.completion.already_advanced) {
            std::cout << "  This battle was already recorded; nothing was "
                         "committed twice.\n";
        } else if (aftermath.completion.advanced) {
            std::cout << "  The campaign moves on"
                      << (aftermath.completion.used_fallback
                              ? " along the route the author left open.\n"
                              : ".\n");
        } else {
            // The graph's own word for it, not a guess. `blocked` means the
            // outcome was committed and no edge matched; anything else means
            // the batch itself was refused.
            std::cout << "  The campaign stands where it stood: "
                      << campaign::progression_error_name(
                             aftermath.completion.error
                         )
                      << '\n';
            if (aftermath.completion.error ==
                campaign::ProgressionError::outcome_rejected) {
                std::cout << "    the batch was refused: "
                          << campaign::outcome_error_name(
                                 aftermath.completion.outcome.error
                             )
                          << '\n';
            }
        }
    }

    void members_joined(
        const std::vector<client::RosterEntry>& joined
    ) override {
        // Said the way a level-up is said: read off the committed batch, in
        // the order the author wrote them, deriving nothing.
        for (const client::RosterEntry& entry : joined) {
            std::cout << "  " << member_name(entry, package_)
                      << " joined the company.\n";
        }
    }

    void campaign_saved(std::string_view slot, storage::StorageError error)
        override {
        if (error == storage::StorageError::none) {
            std::cout << "  Saved to slot '" << slot << "'.\n";
            return;
        }
        std::cout << "  Could not save to slot '" << slot
                  << "': " << storage::storage_error_name(error) << '\n';
    }

    // -----------------------------------------------------------------------
    // The company, between battles
    //
    // Its own prompt and its own verbs, for the same reason the deployment
    // phase has its own: a player is never offered a gesture that means nothing
    // where they are standing. Members and their packs are numbered exactly as
    // the battle prompt numbers units and their packs, so `give 2 1` reads like
    // `use 2 1` and means the neighbouring thing.
    // -----------------------------------------------------------------------

    void management_opened(const client::CompanyManagement& company) override {
        campaign_ = true;
        adopt(company);
        std::cout << '\n'
                  << paint("THE COMPANY", "1;33")
                  << "  before the next battle.  Type 'help' for verbs.\n";
        if (company.refused != campaign_runtime::RosterError::none) {
            // The roster's own word for it. A company that benched everybody,
            // or benched the character an objective names, is a company that
            // cannot take this board, and saying which is the difference
            // between a player who can fix it and one who cannot.
            std::cout << "  that line could not take the field: "
                      << campaign_runtime::roster_error_name(company.refused)
                      << "\n  field somebody and try again.\n";
        }
        print_company();
    }

    void management_committed(const client::ManagementCommit& result) override {
        if (result.error != client::ManagementError::none) {
            std::cout << "  there is no company to manage: "
                      << client::management_error_name(result.error) << '\n';
            return;
        }
        if (!result) {
            // The campaign's own vocabulary, unrepeated. `insufficient_items`
            // is a store that cannot pay; `unit_is_dead` is a gift to somebody
            // the campaign has buried; `unknown_unit` is somebody it never met.
            std::cout << "  the campaign refused it: "
                      << campaign::outcome_error_name(result.application.error);
            if (result.application.error ==
                campaign::OutcomeError::invalid_candidate) {
                std::cout << " ("
                          << campaign::state_error_name(
                                 result.application.state_error
                             )
                          << ')';
            }
            std::cout << "\n  the company is exactly as it was.\n";
            return;
        }
        if (result.application.already_applied) {
            std::cout << "  that was already done; nothing moved twice.\n";
            return;
        }
        say_what_moved(result.batch);
        if (result.saved && result.save != storage::StorageError::none) {
            std::cout << "  but it could not be saved: "
                      << storage::storage_error_name(result.save) << '\n';
        }
    }

    client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override {
        adopt(company);
        std::cout << "Manage> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << "\nInput ended.\n";
            return {client::ManagementVerb::quit, {}, {}};
        }
        std::istringstream input(line);
        std::string verb;
        if (!(input >> verb)) return {};

        if (verb == "quit") return {client::ManagementVerb::quit, {}, {}};
        if (verb == "proceed") return {client::ManagementVerb::proceed, {}, {}};
        if (verb == "help") {
            print_management_help();
            return {};
        }
        if (verb == "roster") {
            print_company();
            return {};
        }

        int who = 0;
        if (!(input >> who)) {
            std::cout << "  unknown command; try 'help'\n";
            return {};
        }
        const client::RosterEntry* const member = member_at(who);
        if (member == nullptr) {
            std::cout << "  no such member\n";
            return {};
        }

        if (verb == "field" || verb == "bench") {
            // A member the next board has nowhere to stand cannot be put on it
            // by any availability, so the verb is refused here rather than
            // committed to no effect. This is the one thing the terminal
            // decides, and it decides it out of `CompanyManagement::placeable`,
            // which is the board's own answer.
            if (!places(member->member)) {
                std::cout << "  the next board has no place for "
                          << member_name(*member, package_) << '\n';
                return {};
            }
            // And a board its author capped fields no more than it says. The
            // refusal is the roster's own word for it, because it is the same
            // refusal `join_campaign_roster` would give a company that took the
            // field over its cap. This only reaches it a gesture earlier.
            if (verb == "field" &&
                member->availability != campaign::Availability::available &&
                over_capacity_if_fielded()) {
                std::cout << "  "
                          << campaign_runtime::roster_error_name(
                                 campaign_runtime::RosterError::
                                     over_deployment_capacity
                             )
                          << ": this board fields " << capacity_
                          << "; bench somebody first\n";
                return {};
            }
            return {
                verb == "field" ? client::ManagementVerb::field
                                : client::ManagementVerb::bench,
                member->member,
                {}
            };
        }

        if (verb != "give" && verb != "take") {
            std::cout << "  unknown command; try 'help'\n";
            return {};
        }
        int slot = 0;
        if (!(input >> slot)) {
            std::cout << "  which item?\n";
            return {};
        }
        const std::vector<campaign::InventoryStack>& from =
            verb == "give" ? store_ : member->carried;
        if (slot < 1 || static_cast<std::size_t>(slot) > from.size()) {
            std::cout << (verb == "give" ? "  the store has no "
                                         : "  they are not carrying a ")
                      << slot << '\n';
            return {};
        }
        return {
            verb == "give" ? client::ManagementVerb::give
                           : client::ManagementVerb::take,
            member->member,
            from[static_cast<std::size_t>(slot) - 1U].item
        };
    }

    // The phase has its own two verbs and its own prompt, so a player is never
    // offered a menu the engine would refuse. Everything that says nothing
    // about the battle is shared with the battle prompt, because none of it is
    // a command: the board, the unit list, the state, help, quit.
    Intent next_deployment_intent(
        const sim::EncounterSnapshot& snapshot,
        const Roster& roster
    ) override {
        std::cout << "Deploy> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << "\nInput ended.\n";
            return {IntentKind::quit, 0, 0, {}};
        }
        std::istringstream input(line);
        std::string verb;
        if (!(input >> verb)) return {};

        if (verb == "quit") return {IntentKind::quit, 0, 0, {}};
        if (verb == "board") return {IntentKind::redraw, 0, 0, {}};
        if (verb == "state") return {IntentKind::show_state, 0, 0, {}};
        if (verb == "units") {
            list_units(snapshot, roster);
            return {};
        }
        if (verb == "help") {
            print_deployment_help();
            return {};
        }
        if (verb == "begin") return {IntentKind::begin_battle, 0, 0, {}};
        if (verb == "deploy") {
            std::string actor;
            int x = 0;
            int y = 0;
            if (!(input >> actor >> x >> y)) {
                std::cout << "  deploy <n> <x> <y>\n";
                return {};
            }
            const sim::UnitId unit = roster.resolve(actor);
            if (unit == 0) {
                std::cout << "  no such unit\n";
                return {};
            }
            return {
                IntentKind::deploy_to, unit, 0,
                {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)}
            };
        }
        std::cout << "  unknown command; try 'help'\n";
        return {};
    }

    Intent next_intent(
        const sim::EncounterSnapshot& snapshot,
        const Roster& roster
    ) override {
        std::cout << side_name(snapshot.active_side) << "> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << "\nInput ended.\n";
            return {IntentKind::quit, 0, 0, {}};
        }
        std::istringstream input(line);
        std::string verb;
        if (!(input >> verb)) return {};

        if (verb == "quit") return {IntentKind::quit, 0, 0, {}};
        if (verb == "board") return {IntentKind::redraw, 0, 0, {}};
        if (verb == "help") { print_help(); return {IntentKind::help, 0, 0, {}}; }
        if (verb == "units") {
            list_units(snapshot, roster);
            return {IntentKind::list_units, 0, 0, {}};
        }
        if (verb == "state") return {IntentKind::show_state, 0, 0, {}};
        // The campaign's own roster, as opposed to the board's. It answers a
        // question the board cannot: who is still with you at all, what each
        // of them has become, and who is gone for good.
        if (verb == "roster") {
            if (!campaign_) {
                std::cout << "  no campaign is running; try 'units'\n";
                return {IntentKind::list_units, 0, 0, {}};
            }
            print_roster();
            return {IntentKind::list_units, 0, 0, {}};
        }

        std::string actor;
        if (!(input >> actor)) {
            std::cout << "  which unit?\n";
            return {};
        }
        const sim::UnitId unit = roster.resolve(actor);
        if (unit == 0) {
            std::cout << "  no such unit\n";
            return {};
        }

        // The same sheet the consoles put on screen, printed. A terminal has no
        // menu to hang a row off, so the row's gesture here is a verb. It is
        // still the same deliberate ask, about the same character, and the
        // block it prints is composed by `grandleon::sheet`, not by this file.
        if (verb == "info") {
            const sim::UnitSnapshot* subject = nullptr;
            for (const sim::UnitSnapshot& candidate : snapshot.units) {
                if (candidate.id == unit) subject = &candidate;
            }
            if (subject == nullptr) {
                std::cout << "  no such unit\n";
                return {};
            }
            // The campaign block, when there is a campaign and when this
            // particular unit is somebody the campaign owns. A hired sword the
            // roster never met, and every unit in a battle played on its own,
            // gets the sheet they always got: `nullptr` is not a level of one.
            grandleon::sheet::CampaignContext context;
            const grandleon::sheet::CampaignContext* attached = nullptr;
            if (campaign_) {
                const client::RosterEntry* const entry = find_member(
                    roster_, binding_.persistent_of(campaign::BattleEntityId{unit})
                );
                if (entry != nullptr) {
                    context.level = entry->progression.level;
                    context.experience = entry->progression.experience;
                    attached = &context;
                }
            }
            const grandleon::sheet::UnitSheet built = grandleon::sheet::build(
                snapshot, *subject, campaign_name_of(unit), weapons_,
                abilities_, items_, attached, package_
            );
            std::cout << '\n';
            for (int i = 0; i < built.count; ++i) {
                std::cout << "  " << built.line(i) << '\n';
            }
            std::cout << '\n';
            return {};
        }

        if (verb == "move") {
            int x = 0;
            int y = 0;
            if (!(input >> x >> y)) {
                std::cout << "  move <n> <x> <y>\n";
                return {};
            }
            return {
                IntentKind::move_to, unit, 0,
                {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)}
            };
        }
        if (verb == "attack") {
            std::string target;
            if (!(input >> target)) {
                std::cout << "  attack <n> <m>\n";
                return {};
            }
            const sim::UnitId other = roster.resolve(target);
            if (other == 0) {
                std::cout << "  no such target\n";
                return {};
            }
            return {IntentKind::attack, unit, other, {}};
        }
        // The action menu's item row, as a verb: the same shape `info` takes
        // here, and for the same reason. The item is named by its place in the
        // character's pack, one-based, exactly as a unit is named by its place
        // in the roster: a terminal player never types a 64-bit identity.
        if (verb == "use") {
            int slot = 0;
            if (!(input >> slot)) {
                std::cout << "  use <n> <k>\n";
                return {};
            }
            const sim::UnitSnapshot* subject = nullptr;
            for (const sim::UnitSnapshot& candidate : snapshot.units) {
                if (candidate.id == unit) subject = &candidate;
            }
            if (subject == nullptr) {
                std::cout << "  no such unit\n";
                return {};
            }
            if (slot < 1 ||
                static_cast<std::size_t>(slot) > subject->item_ids.size()) {
                std::cout << "  they are not carrying a " << slot << "\n";
                return {};
            }
            return {
                IntentKind::use_item, unit, 0, {}, 0, 0,
                subject->item_ids[static_cast<std::size_t>(slot) - 1]
            };
        }
        // After `use` and before `wait`, which is where the consoles put the
        // row this verb is: the vocabulary is ordered by what each gesture
        // costs, and a talk costs an action point exactly as a strike does.
        // It names its target the way `attack` does, by the roster label the
        // player already types, because a talk reaches somebody standing
        // beside you and not the hand that holds it.
        //
        // Nothing here asks whether the two are adjacent, whether the target
        // has anything to say, or whether they have already walked away. All
        // three are refusals the engine names, and naming them twice is how a
        // client comes to disagree with the rules.
        if (verb == "talk") {
            std::string target;
            if (!(input >> target)) {
                std::cout << "  talk <n> <m>\n";
                return {};
            }
            const sim::UnitId other = roster.resolve(target);
            if (other == 0) {
                std::cout << "  no such target\n";
                return {};
            }
            return {IntentKind::talk, unit, other, {}};
        }
        if (verb == "wait") return {IntentKind::wait, unit, 0, {}};

        std::cout << "  unknown command; try 'help'\n";
        return {};
    }

private:
    [[nodiscard]] std::string paint(
        const std::string& text,
        const char* code
    ) const {
        if (!colour_) return text;
        return std::string("\033[") + code + "m" + text + "\033[0m";
    }

    // A side by the only name that means anything to the person reading it:
    // theirs, or the enemy's. The colour stays keyed to the side's ordinal:
    // the first side is blue and the second red wherever either is drawn, so
    // a player holding the second side reads "Your side" in red, which is the
    // colour their own units are.
    [[nodiscard]] std::string side_name(sim::Side side) const {
        const char* code = side == sim::Side::first ? "1;34" : "1;31";
        return paint(side == player_side_ ? "Your side" : "The enemy", code);
    }

    // The board being fought, kept between frames.
    //
    // `report` is handed the engine's events and the numbered roster and
    // nothing else, no snapshot, so a line that wants to say more than a
    // number about the unit an event names has to have remembered one. The
    // whole snapshot rather than a table of unit types, because naming a
    // character takes both the type and the company it stands in: an ordinal
    // is only drawn when more than one of a kind is on the board, and that is
    // a question about everybody.
    //
    // Refreshed from every frame rather than kept from the opening, because a
    // board that authors waves grows characters part-way through the battle
    // and a table built at the opening would not have them.
    void remember_board(const sim::EncounterSnapshot& snapshot) {
        last_snapshot_ = snapshot;
    }

    // What to call whoever is standing in one unit of the board, in the words a
    // player reads.
    //
    // The campaign's own name first, through the join the session published:
    // `binding_` says which roster member a board unit is, and the roster says
    // what the author called them. This is the same join `info` already uses to
    // put a level on a character's sheet, and it is the reason a defeat can be
    // narrated by name at the moment it happens rather than a battle later on
    // the company screen.
    //
    // A unit no member stands in falls through to its unit type, which is the
    // only thing anybody knows about it. The name of a member the author left
    // unnamed is not a name, so it falls through as well: an empty string in
    // the middle of a sentence would say less than "RIVER WATCH".
    // What the campaign being played calls this character going down. A death
    // under the permanent rule and a fall under the recoverable one: the same
    // event, two vocabularies, and which one is in force belongs to the
    // campaign rather than to this screen.
    //
    // The softer word belongs to the company and to nobody else. A campaign
    // that carries its own people off the field does not carry the bandit off
    // with them, and a log that said the bandit fell would be promising a
    // reader they were going to meet him again. So anybody the join does not
    // know is a death whatever rule the company plays under, as is everybody on
    // a board played outside a campaign at all.
    [[nodiscard]] const char* fall_word(sim::UnitId id) const {
        if (loss_ != package_runtime::CharacterLoss::recoverable) return "died";
        return binding_.persistent_of(campaign::BattleEntityId{id}).value != 0U
                   ? "fell"
                   : "died";
    }

    // The campaign's own name for whoever stands in a board unit, or null when
    // nobody does. The join is the one piece of naming only a client can
    // answer; what is done with the answer belongs to
    // `sheet::character_name`, shared with both consoles.
    [[nodiscard]] const char* campaign_name_of(sim::UnitId id) const {
        if (!campaign_) return nullptr;
        return client::member_name_on_board(binding_, roster_, id);
    }

    [[nodiscard]] std::string character_called(sim::UnitId id) const {
        return grandleon::sheet::character_name(
                   package_, last_snapshot_, id, campaign_name_of(id)
        ).c_str();
    }

    static const sim::UnitSnapshot* occupant_at(
        const sim::EncounterSnapshot& snapshot,
        std::uint16_t x,
        std::uint16_t y
    ) {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (sim::on_board(unit) &&
                unit.position.x == static_cast<std::int16_t>(x) &&
                unit.position.y == static_cast<std::int16_t>(y)) {
                return &unit;
            }
        }
        return nullptr;
    }

    // The whole roster, one row each, and deliberately not filtered by
    // `sim::on_board`. This is a listing of everybody the battle knows about
    // rather than a query about who holds a tile, so somebody who walked away
    // or has not marched in yet still gets a row.
    //
    // But the row has to say so. "(down)" is a health test and only a health
    // test; a departed character keeps its health and an unarrived one has
    // never been struck, so both would print clean and both would print a tile.
    // Their `position` is the tile they left or the tile the content asked for,
    // not one anybody holds, so the qualifier is what keeps "at 3,4" from
    // reading as a claim about the board.
    void list_units(
        const sim::EncounterSnapshot& snapshot,
        const Roster& roster
    ) const {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            const char* standing = "";
            if (unit.health <= 0) {
                standing = " (down)";
            } else if (unit.departed) {
                standing = " (gone)";
            } else if (!unit.arrived) {
                standing = " (not here yet)";
            }
            std::cout << "  " << roster.label(unit.id) << ") "
                      << side_name(unit.side)
                      << standing
                      << "  hp " << unit.health << '/' << unit.maximum_health
                      << "  at " << unit.position.x << ',' << unit.position.y
                      << "  reach " << static_cast<int>(unit.minimum_reach)
                      << '-' << static_cast<int>(unit.maximum_reach)
                      << "  move " << static_cast<int>(unit.movement)
                      << "  ap " << static_cast<int>(unit.action_points);
            if (unit.id == snapshot.active_unit_id) {
                std::cout << "  <- acting, "
                          << static_cast<int>(snapshot.remaining_action_points)
                          << " left";
            }
            std::cout << '\n';
        }
    }

    // A list of stacks, as one line of "2 x TONIC, 1 x TOKEN". Used for both
    // owners a campaign keeps, because they are the same shape and a reader
    // should be able to compare them at a glance.
    static void print_stacks(
        const std::vector<campaign::InventoryStack>& stacks
    ) {
        bool any = false;
        for (const campaign::InventoryStack& stack : stacks) {
            std::cout << (any ? ", " : "") << stack.quantity << " x "
                      << grandleon::sheet::item_name(stack.item.stable_id);
            any = true;
        }
        if (!any) std::cout << "nothing";
    }

    // The campaign's roster, and what the campaign has made of each of them.
    // Every field is read straight out of `campaign::PersistentUnit`, including
    // the kit: what a member carries is what they will take onto the next
    // board, so this is the pack a player is about to fight with rather than
    // the list their unit type happens to name.
    void print_roster() const {
        std::cout << "  the company:\n";
        for (const client::RosterEntry& entry : roster_) {
            std::cout << "    " << member_name(entry, package_) << "  level "
                      << entry.progression.level << "  exp "
                      << entry.progression.experience << "  "
                      << campaign::availability_name(entry.availability);
            bool any = false;
            for (std::size_t index = 0; index < campaign::growable_stat_count;
                 ++index) {
                if (entry.progression.gained[index] == 0U) continue;
                std::cout << (any ? ", " : "  gains: ") << '+'
                          << entry.progression.gained[index] << ' '
                          << campaign::growable_stat_name(
                                 static_cast<campaign::GrowableStat>(index)
                             );
                any = true;
            }
            std::cout << "\n      carrying: ";
            print_stacks(entry.carried);
            std::cout << '\n';
        }
        // And what nobody is carrying. The store is the company's, and it is a
        // different thing from the sum of what the company holds in its hands,
        // which is exactly why the two are printed apart.
        std::cout << "  the store: ";
        print_stacks(store_);
        std::cout << '\n';
    }

    // The company the last management view described. Kept so that a verb typed
    // at the prompt resolves against exactly the company the player was shown,
    // rather than against whatever the session holds a moment later.
    void adopt(const client::CompanyManagement& company) {
        roster_ = company.roster;
        store_ = company.store;
        placeable_ = company.placeable;
        fielded_ = company.fielded.size();
        capacity_ = company.capacity;
    }

    [[nodiscard]] bool places(campaign::PersistentEntityId member) const {
        return std::find(placeable_.begin(), placeable_.end(), member) !=
               placeable_.end();
    }

    // Whether one more member would take this board past what its author
    // allows. Read out of `CompanyManagement`, never summed here: the session
    // publishes both numbers precisely so that two clients cannot count
    // differently.
    [[nodiscard]] bool over_capacity_if_fielded() const {
        return capacity_ != 0U && fielded_ >= static_cast<std::size_t>(capacity_);
    }

    // The nth member of the company as the screen numbered them, one-based.
    [[nodiscard]] const client::RosterEntry* member_at(int index) const {
        if (index < 1 || static_cast<std::size_t>(index) > roster_.size()) {
            return nullptr;
        }
        return &roster_[static_cast<std::size_t>(index) - 1U];
    }

    // A numbered list of stacks, one per line, so that `give 2 1` has something
    // to name. The battle prompt numbers a unit's pack the same way.
    static void print_numbered_stacks(
        const std::vector<campaign::InventoryStack>& stacks,
        const char* indent
    ) {
        if (stacks.empty()) {
            std::cout << indent << "nothing\n";
            return;
        }
        int slot = 0;
        for (const campaign::InventoryStack& stack : stacks) {
            ++slot;
            std::cout << indent << slot << ") " << stack.quantity << " x "
                      << grandleon::sheet::item_name(stack.item.stable_id)
                      << '\n';
        }
    }

    // The company, numbered, with what each member carries and whether the next
    // board has anywhere to put them. Every number is read out of the campaign;
    // nothing here is summed, compared or decided.
    void print_company() const {
        std::cout << "  the company:\n";
        int index = 0;
        for (const client::RosterEntry& entry : roster_) {
            ++index;
            std::cout << "    " << index << ") " << member_name(entry, package_)
                      << "  level " << entry.progression.level << "  "
                      << campaign::availability_name(entry.availability);
            if (!places(entry.member)) {
                std::cout << "  (the next board has no place for them)";
            } else if (entry.availability == campaign::Availability::available) {
                std::cout << "  (takes the next board)";
            } else {
                std::cout << "  (sits the next board out)";
            }
            std::cout << "\n      carrying:\n";
            print_numbered_stacks(entry.carried, "        ");
        }
        if (capacity_ != 0U) {
            std::cout << "  fielded " << fielded_ << " of " << capacity_
                      << '\n';
        }
        std::cout << "  the store:\n";
        print_numbered_stacks(store_, "    ");
    }

    // What one management batch did, read off the very operations that were
    // committed. A move is a consume and an add, so it is said as the one
    // sentence it is rather than as the two operations it encodes.
    void say_what_moved(const campaign::CampaignOutcomeBatch& batch) const {
        const campaign::CampaignOutcomeOperation* left = nullptr;
        const campaign::CampaignOutcomeOperation* arrived = nullptr;
        for (const campaign::CampaignOutcomeOperation& operation :
             batch.operations) {
            switch (operation.kind) {
                case campaign::OutcomeOperationKind::consume_item:
                    left = &operation;
                    break;
                case campaign::OutcomeOperationKind::add_item:
                    arrived = &operation;
                    break;
                case campaign::OutcomeOperationKind::set_availability:
                    std::cout << "  " << owner_name(operation.subject)
                              << (operation.selector ==
                                          static_cast<std::uint8_t>(
                                              campaign::Availability::available
                                          )
                                      ? " takes the next board.\n"
                                      : " sits the next board out.\n");
                    break;
                default:
                    break;
            }
        }
        if (left == nullptr || arrived == nullptr) return;
        std::cout << "  " << arrived->amount << " x "
                  << grandleon::sheet::item_name(arrived->definition.stable_id)
                  << " passed from " << owner_name(left->subject) << " to "
                  << owner_name(arrived->subject) << ".\n";
    }

    // Who an operation's subject is, in the words a player reads. Owner zero is
    // the company's own store, which is not a member and is never named as one.
    [[nodiscard]] std::string owner_name(
        campaign::PersistentEntityId subject
    ) const {
        if (subject.value == 0U) return "the store";
        const client::RosterEntry* const entry = find_member(roster_, subject);
        return entry == nullptr ? std::string("a member") : member_name(*entry, package_);
    }

    // What the battle moved, read off the very operations that were committed.
    // Owner zero is the shared store, which is where a drop lands; any other
    // owner is a member, which is where a spend comes from. A campaign
    // character fights out of their own kit, and this says so rather than
    // pretending the army paid for it.
    void print_inventory_changes(
        const client::BattleAftermath& aftermath
    ) const {
        for (const campaign::CampaignOutcomeOperation& operation :
             aftermath.progression.operations) {
            const bool added =
                operation.kind == campaign::OutcomeOperationKind::add_item;
            const bool spent =
                operation.kind == campaign::OutcomeOperationKind::consume_item;
            if (!added && !spent) continue;
            std::cout << "  " << operation.amount << " x "
                      << grandleon::sheet::item_name(
                             operation.definition.stable_id
                         );
            if (operation.subject.value == 0U) {
                std::cout << (added ? " went into the store\n"
                                    : " came out of the store\n");
                continue;
            }
            const client::RosterEntry* const owner =
                find_member(aftermath.roster, operation.subject);
            const std::string who =
                owner == nullptr ? std::string("a member") : member_name(*owner, package_);
            std::cout << (added ? " went into " : " came out of ") << who
                      << "'s pack\n";
        }
    }

    // What this battle granted one member, summed off the committed
    // operations rather than recomputed from the events.
    static std::int64_t experience_granted(
        const client::BattleAftermath& aftermath,
        campaign::PersistentEntityId member
    ) {
        std::int64_t total = 0;
        for (const campaign::CampaignOutcomeOperation& operation :
             aftermath.progression.operations) {
            if (operation.kind !=
                campaign::OutcomeOperationKind::grant_experience) {
                continue;
            }
            if (!(operation.subject == member)) continue;
            total += operation.amount;
        }
        return total;
    }

    static void print_help() {
        std::cout <<
            "  units             list every unit and its numbered label\n"
            "  roster            the company: level, experience, fate, packs\n"
            "  info <n>          unit n's full sheet: every stat it has\n"
            "  board             redraw the board\n"
            "  move <n> <x> <y>  move unit n to a tile\n"
            "  attack <n> <m>    unit n attacks unit m\n"
            "  use <n> <k>       unit n spends the kth item in its pack\n"
            "  talk <n> <m>      unit n talks unit m off the board\n"
            "  wait <n>          unit n ends its activation\n"
            "  state             show the turn and round\n"
            "  help              this list\n"
            "  quit              leave\n";
    }

    static void print_management_help() {
        std::cout <<
            "  roster            the company, its packs, and the store\n"
            "  give <n> <k>      hand member n the kth thing in the store\n"
            "  take <n> <k>      put the kth thing in member n's pack back\n"
            "  field <n>         member n takes the next board\n"
            "  bench <n>         member n sits the next board out\n"
            "  proceed           take the board with the company as it stands\n"
            "  help              this list\n"
            "  quit              leave; nothing you did here is lost\n";
    }

    static void print_deployment_help() {
        std::cout <<
            "  units               list every unit and its numbered label\n"
            "  board               redraw the board; + is ground you may take\n"
            "  deploy <n> <x> <y>  stand unit n on a tile of your ground\n"
            "  begin               open the battle with the line as it stands\n"
            "  state               show the turn and round\n"
            "  help                this list\n"
            "  quit                leave\n";
    }

    bool colour_{true};
    // The side the player holds, kept from the opening of the battle so every
    // line below can name a side as the player sees it rather than by its
    // ordinal. Defaulted to the first side because that is what a battle begun
    // without a stated side gives them.
    sim::Side player_side_{sim::Side::first};
    // The package this client opened, or null. Every name printed below is
    // asked of it before the shipped table, so a terminal and a cartridge
    // running one project agree about what a character is called.
    const package_format::LoadedPackage* package_{nullptr};
    // The region, kept between frames so the board can be drawn without
    // reading it out of a snapshot every time. Empty for a board with none.
    std::vector<sim::Position> zone_;
    std::vector<sim::WeaponDefinition> weapons_;
    std::vector<sim::AbilityDefinition> abilities_;
    std::vector<sim::ItemDefinition> items_;
    // Everything below is the campaign's, and stays empty for a session that
    // has none. `campaign_` is what makes the difference visible: the verbs and
    // the sheet block appear when a campaign is running and not otherwise.
    bool campaign_{false};
    std::vector<client::RosterEntry> roster_;
    // What the company owns beyond what its members carry, as the last thing
    // the session told this front end. Cached beside the roster for the same
    // reason: `roster` may be typed between battles, when nothing is narrating.
    std::vector<campaign::InventoryStack> store_;
    // Which members the next board has a placement for, as the management view
    // last stated it. The one thing the prompt refuses on its own, and it
    // refuses it out of the board's answer rather than out of a rule of its
    // own.
    std::vector<campaign::PersistentEntityId> placeable_;
    std::size_t fielded_{};
    std::uint16_t capacity_{};
    campaign::BattleBinding binding_;
    // The rule the campaign being narrated plays under. Permanent until a
    // campaign says otherwise, which is what a board played outside any campaign
    // also means: nobody is coming back from a battle nobody is keeping score of.
    package_runtime::CharacterLoss loss_{
        package_runtime::CharacterLoss::permanent
    };
    Roster board_;
    // The board being fought, as the last frame drew it. See
    // `remember_board`.
    sim::EncounterSnapshot last_snapshot_{};
};

std::unique_ptr<client::CampaignFrontEnd> make_terminal_front_end(
    bool colour, const package_format::LoadedPackage* package
) {
    return std::make_unique<TerminalPresenter>(colour, package);
}

std::unique_ptr<Presenter> make_terminal_presenter(
    bool colour, const package_format::LoadedPackage* package
) {
    return make_terminal_front_end(colour, package);
}

}  // namespace grandleon::desktop
