// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/migration.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/campaign/state.hpp>
#include <grandleon/campaign_runtime/campaign_runtime.hpp>
#include <grandleon/client/presenter.hpp>
#include <grandleon/client/session.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/storage/slot_storage.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The session that plays a campaign the player keeps.
//
// `session.hpp` walks an authored flow through one sitting: a cursor, an
// outcome, the next node, and nothing that survives the process. Everything a
// campaign accumulates lives in `engine/campaign` and
// `engine/campaign_runtime`: who died for good, who grew and by how much, what
// the army owns. It is proved headlessly by
// `tests/campaign_runtime/demo_permadeath_test.cpp` and by nothing a player
// could sit down in front of. This is the loop that test drives, driven
// interactively instead.
//
// ## What this file does not do
//
// It derives nothing. Experience, levels, growth rolls, what a battle left
// behind and what it spent are all `campaign_runtime::derive_battle_progression`
// and are carried to a narrator exactly as that function returned them.
// Exclusion is `campaign_runtime::load_encounter_for_campaign`. Where the
// campaign goes next is `campaign::complete_node`. A client that computed any
// of it would be a client that could disagree with the rules, and the whole
// argument for the campaign runtime existing is that there is one place where a
// battle becomes a campaign fact.
//
// ## Why it is its own library
//
// `grandleon_client` is linked by every console build, and most of them are
// playing a battle rather than keeping a campaign. Putting the persistent
// session in `grandleon_client` would drag
// `engine/campaign`, `engine/campaign_runtime` and `platform/storage` into
// every console link for a loop most of those ROMs never run. So this is
// `grandleon_client_campaign`, above `grandleon_client`, and a console target
// links it the day it has somewhere to put a save.
//
// The Nintendo 64 has reached that day: its campaign ROMs link this and write
// to cartridge SRAM through `grandleon::storage::ByteWindowSlotStorage`, while
// its play, probe and autopilot ROMs link none of it and are unchanged. That is
// the split doing what it is for. The PlayStation plays a battle and keeps no
// campaign yet.
//
// ## Who the player starts with
//
// The author says so. A campaign carries a roster: a list of members, each a
// name, a unit type and an identity of their own. The session founds the
// company from exactly that, one `campaign::recruit_unit` per member the
// campaign begins with, in authored order, with one-based persistent ids
// assigned in that same order so two runs of the same content found the same
// company and a save written by one is read by the other. Members who join
// later are in the same list, marked with the node that brings them in, and
// are recruited in that node's own completion batch.
//
// A roster is still campaign state and never content: the package says who the
// company *starts* as, and everything that happens to them afterwards is
// state. The client does not invent the starting company. A campaign that
// authors nobody is refused by name (`roster_rejected`) rather than played with
// a cast conjured out of its battle placements.
//
// ## What they start out carrying
//
// The same batch that recruits a member puts their unit type's authored
// starting items in their hands, once, through
// `campaign_runtime::starting_kit`: founding members in the founding batch, an
// authored recruit in its own node's batch. After that the type's list is
// never read for that member again: what they carry into a battle is what the
// campaign holds for them, so a draught drunk in one battle is gone from the
// next.
//
// ## The company, between battles
//
// A campaign is not only a sequence of battles; it is a thing a player manages
// between them. The stage that does it stands immediately before every board:
// after a battle has committed and the graph has moved, after a story node, on
// a resume, and before the very first board of a fresh campaign. One rule
// rather than four, and the other three fall out of it.
//
// Its verbs are `give_item`, `take_item`, `set_fielded` and proceed, and each
// of the first three is exactly one `campaign::CampaignOutcomeBatch` through
// `campaign::apply_outcome`. A move is `consume_item` against the owner it
// leaves and `add_item` against the owner it reaches; benching is
// `set_availability(member, retired)`, which is the unavailability a rule may
// reverse and which the exclusion pass already leaves off a board. No new
// engine vocabulary was needed for any of it, which is why there is none.
//
// A management batch is identified by where the company is standing: the
// campaign node as its content reference, a zero battle hash because no battle
// produced it, and the number of outcomes already committed as its sequence.
// The category of that reference is part of the id, so a management batch at a
// node can never be mistaken for the battle fought at its encounter; and the
// count is what makes two identical gestures two moves rather than one move
// committed twice.
//
// The slot is written after every gesture that commits. That is what leaves the
// stage with no pending state at all: a screen shows the campaign the slot
// holds, and a campaign resumed in the middle of managing resumes in the middle
// of managing, with everything the player had already done.
//
// ## The order a battle's consequences commit in
//
// What the characters did, then who did not come back, then the objectives,
// then whoever the node brought in. The first two are in that order because
// `apply_outcome` refuses any operation against a permanently dead member: a
// character who drinks their last draught and then falls has to spend it while
// they are still alive, or the batch refuses itself. It is also the true
// sequence, and it is what leaves `record_permanent_death` returning to the
// store only what is actually left of a kit.

namespace grandleon::client {

// One member of the company, as the campaign holds them right now.
//
// The identity, the name and the unit type are the author's; the availability
// and the progression are read out of `campaign::CampaignState`. Nothing here
// is computed.
struct RosterEntry final {
    campaign::PersistentEntityId member{};
    // The authored identity of this member, which is the source key every
    // placement fielding them carries. The same key on every board they appear
    // on, which is precisely why the roster is joined to a board through it.
    std::uint64_t placement_source_key{};
    // What the author called them. A narrator says this and never derives a
    // name from a unit type.
    std::string name;
    campaign::DefinitionRef unit_type{};
    campaign::Availability availability{campaign::Availability::unrecruited};
    campaign::Progression progression{};
    // What the campaign holds for this member, ascending by item identity, as
    // `campaign::PersistentUnit::carried` holds it. This is the satchel they
    // will take onto the next board rather than their unit type's list, so a
    // narrator showing it is showing what a player is about to fight with. Read
    // out of the state and never derived.
    std::vector<campaign::InventoryStack> carried;
};

// Why a named slot did not become a campaign.
//
// Four channels rather than one string, because each is somebody else's
// vocabulary and none of them should be re-spelled here: the device says
// `storage::StorageError`, the migration registry says
// `campaign::MigrationError`, the envelope says `campaign::SaveError`, and the
// campaign's own invariants say `campaign::StateError`. A narrator prints
// whichever is set, by that layer's own name for it.
struct SlotFailure final {
    std::string slot;
    storage::StorageError storage{storage::StorageError::none};
    campaign::MigrationError migration{campaign::MigrationError::none};
    campaign::SaveError save{campaign::SaveError::none};
    campaign::StateError state{campaign::StateError::none};
    // The slot held a perfectly good campaign that is standing in a different
    // campaign than the one asked for. No layer below refuses it, because the
    // bytes are valid and the state is valid. So this is the session's own
    // refusal, spelled separately rather than borrowed from somebody else's
    // vocabulary. Resuming anyway would put a player at a node this flow does
    // not contain.
    bool wrong_campaign{false};
};

// The campaign's own name for whoever stands in one board unit, or null when
// nobody does. That is every unit on a board played outside a campaign, and
// the whole of the opposing side on one played inside it.
//
// This is the one part of naming a character that only a client can answer:
// `sheet::character_name` reads the package and the board and decides the rest,
// but the join from a board unit to a roster member is published by the session
// and lives nowhere else. Written once here because every campaign-aware client
// asks it: both consoles and the terminal. Three copies of a six-line lookup is
// how three machines come to name one character three things.
//
// The pointer is borrowed from the roster entry and lives as long as the roster
// does, which is the battle: a board is prepared once and handed to a front end
// for the whole of it.
[[nodiscard]] inline const char* member_name_on_board(
    const campaign::BattleBinding& binding,
    const std::vector<RosterEntry>& roster,
    simulation::UnitId unit
) noexcept {
    const campaign::PersistentEntityId person =
        binding.persistent_of(campaign::BattleEntityId{unit});
    if (person.value == 0U) return nullptr;
    for (const RosterEntry& entry : roster) {
        if (entry.member.value != person.value) continue;
        return entry.name.empty() ? nullptr : entry.name.c_str();
    }
    return nullptr;
}

// One Stage of a campaign, as a picker offers it.
//
// A Stage is an encounter node: a board somebody fights. A story node is not
// one, and is not listed, because jumping to a cutscene is asking to watch it
// rather than to be anywhere.
// Whether this build carries the Stage picker.
//
// A define rather than a setting in a project, and the difference is the point.
// This is an aid for an image somebody makes to debug with: the Nintendo 64's
// autopilot ROMs are selected the same way, by `GRANDLEON_N64_AUTOPILOT`, and
// for the same reason. A project cannot carry it, so it cannot be left switched
// on in a game somebody shares, and no shipped package's bytes move for the sake
// of a testing aid.
//
// A constant rather than an `#ifdef` at each use, so both halves compile in every
// build and a change to either is caught by the ordinary gate rather than only by
// the one configuration that happens to include it.
#ifdef GRANDLEON_STAGE_PICKER
inline constexpr bool stage_picker_built = true;
#else
inline constexpr bool stage_picker_built = false;
#endif

struct CampaignStage final {
    campaign::DefinitionRef node{};
    // The authored node identity, which is what an intent carries and what
    // `jump_to_stage` is asked for. The reference above is the same thing under
    // a content category, and both are here so a front end never has to build
    // one out of the other.
    std::uint64_t node_id{};
    std::uint64_t encounter_id{};
    // What the author called the board fought here, copied out of the package
    // because a front end holds this for the length of a battle and the view a
    // decode borrows is not something to keep. Empty for a board the package has
    // no name for, and a front end then says the Stage's number instead.
    std::string name;
    // This playthrough has stood here. Read out of the progression history,
    // which is the campaign's own record of where it has been, so it survives a
    // save exactly as the route does.
    //
    // It is the one thing that separates a safe jump from a risky one, and it
    // is published rather than left to a screen to work out: a Stage already
    // reached is one whose objectives this campaign recorded and whose recruits
    // it has, and a Stage never reached is one where a jumped-to battle may be
    // unwinnable. A picker that could not say which is which would be offering
    // two different moves under one word.
    bool reached{false};
    // The campaign is standing here now. Jumping to it is that Stage begun
    // again, which is an ordinary thing to want and not an edge case.
    bool standing{false};
};

// The board about to be fought, and who the roster kept off it.
struct CampaignBoard final {
    campaign::DefinitionRef node{};
    std::uint64_t encounter_id{};
    // Ascending. A permanently dead member appears here and nowhere else,
    // however plainly a later map lists them.
    std::vector<campaign::PersistentEntityId> excluded;
    std::vector<RosterEntry> roster;
    // What the company owns beyond what its members carry. Shown beside the
    // roster's kits so a player about to fight can see both halves of what the
    // campaign holds and that they are two different things.
    std::vector<campaign::InventoryStack> store;
    // Which board unit is which roster member, as
    // `load_encounter_for_campaign` published it. A front end that wants to
    // put a level on a character's sheet mid-battle needs exactly this join
    // and must not guess at it: a board id is encounter-local and a member is
    // not.
    campaign::BattleBinding binding;
    // What this campaign does with somebody who falls, so that a client
    // narrating the battle can say it in the rule's own words rather than
    // guessing, or worse, telling a player somebody died who is about to walk
    // back onto the next board.
    package_runtime::CharacterLoss character_loss{
        package_runtime::CharacterLoss::permanent
    };
    // Every Stage this campaign has, in the order its flow reaches them, or
    // empty when its author did not ask for the Stage picker. `stages()` says
    // what that order is and why it is not the order of the array.
    //
    // **Empty is the gate, and it is here rather than in each client.** A front
    // end offers the row when this list is not empty and never asks a project
    // anything, so there is exactly one place that decides whether a game has
    // the picker, and a game that never turned it on cannot show the row on any
    // machine. It is handed over with the board, before the first frame, for
    // the reason the weapon and ability definitions are: the menu that offers it
    // opens in the middle of a battle, and a client should not have to reach
    // back through the seam to draw one.
    std::vector<CampaignStage> stages;
};

// What a finished battle did to the campaign.
//
// `progression` is `campaign_runtime::derive_battle_progression`'s own return
// value, untouched: its `level_ups` are the level-ups, its `operations` are the
// experience granted, the levels reached, the points the growth stream gave,
// what fell into the store and what was drunk out of it. A narrator reads them;
// nothing re-derives them.
struct BattleAftermath final {
    campaign::DefinitionRef node{};
    std::uint64_t encounter_id{};
    simulation::Outcome outcome{simulation::Outcome::ongoing};
    std::uint64_t canonical_hash{};
    campaign_runtime::BattleProgression progression;
    // Members the battle put at zero health, ascending. Derived from nothing
    // but the board, from a unit at zero health that a roster member was
    // standing in, which is why it means the same thing under either rule: it
    // is who fell, and never who was buried.
    //
    // What became of them is `character_loss` below. Under the permanent rule
    // every one of these is dead and their availability says so; under the
    // recoverable rule every one of them is still available and still carrying
    // what the battle left them with. A client that named these people without
    // reading that rule would be a client that told a player somebody had died
    // and then showed them on the next board.
    std::vector<campaign::PersistentEntityId> fallen;
    // The rule that was in force, so a client may word the two cases apart.
    package_runtime::CharacterLoss character_loss{
        package_runtime::CharacterLoss::permanent
    };
    // Members the completed node recruited, in authored order, as the campaign
    // holds them now. Read back out of the committed batch: a node that
    // recruits nobody, or a batch that did not commit, leaves this empty.
    std::vector<RosterEntry> recruited;
    // The roster after the batch committed, so a narrator can state a total
    // without adding anything up. Each entry carries the member's kit as the
    // commit left it, which is what makes "what was drunk stays drunk" a thing
    // a screen can show rather than a claim.
    std::vector<RosterEntry> roster;
    // The company's shared store after the batch committed, ascending by item
    // identity. What fell on the battlefield lands here; what a character drank
    // comes out of their own hands, and a surface that shows the two apart is
    // showing the two owners the campaign actually keeps.
    std::vector<campaign::InventoryStack> store;
    // The board's join, so a narrator can say which of the numbered units a
    // player was steering each of these members was.
    campaign::BattleBinding binding;
    // What `campaign::complete_node` said: whether the campaign moved, where
    // to, and why not when it did not.
    campaign::NodeCompletion completion;
};

enum class CampaignSessionError : std::uint8_t {
    none = 0,
    // The campaign's authored flow did not decode, or the graph it translates
    // to is not one `campaign::validate_graph` accepts.
    graph_rejected,
    // An encounter node's board did not load, or the roster could not be
    // joined to it. `campaign_runtime::roster_error_name` says which.
    board_rejected,
    // A slot name no platform could carry.
    invalid_slot,
    // The founded roster was refused by `campaign::apply_outcome`, or the
    // graph refused to be entered.
    roster_rejected,
    // A battle's consequences were refused, or the campaign could not move.
    // The narrator has already said which; this is the exit code.
    progression_rejected,
    // The flow neither ended nor moved within its bound. A graph that will not
    // hand the turn back is surfaced rather than spun on.
    flow_stalled,
};

[[nodiscard]] std::string_view campaign_session_error_name(
    CampaignSessionError error
) noexcept;

// Where the session's boards come from.
//
// One question, asked once per encounter node: what board does this encounter
// identity name? A desktop or console client answers it out of the mounted
// package and `PackageBoards` below is that answer. The editor cannot: Play
// mode runs content that has never been compiled, so it holds boards it built
// from unsaved source and no package at all.
//
// The seam exists so that the *rest* of the session is one implementation
// rather than two: founding, exclusion, growth, experience, drops, the commit,
// the envelope, the edge. What differs between a compiled game and an
// authoring session is where a board comes from, and that is all this lets
// differ.
// ---------------------------------------------------------------------------
// The company, between battles
// ---------------------------------------------------------------------------

// What the company is, and what the next board has room for, at the moment the
// player is asked what to do with it.
//
// Every field is read out of committed campaign state or off the authored
// board. Nothing here is a plan, a basket, or an arrangement waiting to be
// applied: a management gesture commits before this view is asked for again, so
// two of these taken either side of a gesture differ by exactly what the
// campaign committed.
struct CompanyManagement final {
    CampaignSessionError error{CampaignSessionError::none};
    // Where the company is standing, which is the node the next board is fought
    // at.
    campaign::DefinitionRef node{};
    std::uint64_t encounter_id{};
    // The company, with each member's kit, exactly as `roster()` reports it.
    std::vector<RosterEntry> roster;
    // What the company owns beyond what its members carry.
    std::vector<campaign::InventoryStack> store;
    // Which members the next board has a placement for, ascending
    // (`campaign_runtime::members_a_board_places`). A member who is not here
    // cannot be put on this board by any availability, so a screen offers no
    // choice about them: fielding them would be a gesture that succeeds and
    // changes nothing.
    std::vector<campaign::PersistentEntityId> placeable;
    // Which of those members would actually take the field as the company
    // stands, ascending (`campaign_runtime::members_a_board_fields`): the
    // placeable members the campaign says are deployable. This is the set the
    // capacity below is counted against, and it is published rather than left
    // to each screen to work out so that two clients cannot count differently.
    std::vector<campaign::PersistentEntityId> fielded;
    // How many of the company this board's author lets take its field, or zero
    // for a board that caps nothing. Every board caps nothing by default.
    //
    // A maximum and not a quota: fewer is legal, and the engine never benches
    // anybody to make a party fit. A screen refuses a `field` that would carry
    // `fielded` past this, under `roster_error_name`'s own word for it, and
    // that check is an early copy of the engine's rather than a substitute.
    // `prepare_board` refuses an over-cap company whatever a screen believed,
    // and says so in `refused` below.
    std::uint16_t capacity{};
    // Why the last attempt to take the board did not publish one, when there
    // was one. `side_emptied` is a company that benched everybody;
    // `unavailable_objective_target` is a company that benched the character an
    // objective names. Both were reachable before only by dying, and both are
    // now ordinary things a player can do and undo. So the stage says which,
    // in the roster's own word for it, and stands where it stood.
    campaign_runtime::RosterError refused{campaign_runtime::RosterError::none};
    // Every Stage this campaign has, exactly as `CampaignBoard::stages` carries
    // them, or empty when its author did not ask for the Stage picker.
    //
    // **The picker is here as well as on the board menu, and it is here because
    // of `refused` above.** A jump moves the campaign and recruits nobody on
    // behalf of the Stages it passed over, so a board whose objective names a
    // character the skipped Stages would have brought in cannot be published:
    // Tarnholt's last board is exactly that, because Captain Mirea joins at a
    // cutscene after its first battle. The stage that a refused board sends the
    // player back to is this one, and there is nothing they can do here to
    // recruit somebody — so if the only way to jump were out of a battle, a
    // player who jumped somewhere unplayable would be standing at a Stage they
    // cannot leave, in a slot that has already been written. The way out has to
    // be on the screen the refusal lands on.
    std::vector<CampaignStage> stages;
};

// What a player asked the management stage to do.
//
// `none` is what a front end returns when the player typed something that was
// not a verb, or asked to be shown the company again: the driver asks once
// more rather than proceeding, so a stray line never opens a battle.
enum class ManagementVerb : std::uint8_t {
    none = 0,
    // Move one of `item` out of the store and into `member`'s kit.
    give,
    // Move one of `item` out of `member`'s kit and into the store.
    take,
    // `member` takes the next board.
    field,
    // `member` sits the next board out.
    bench,
    // Publish the board for the company as it stands.
    proceed,
    // Stand the campaign on the Stage `stage` names, without playing the ones
    // between. Only ever offered when `CompanyManagement::stages` is not empty,
    // which is only in a build that carries the picker.
    //
    // Unlike the four above it this is not a gesture about the company; it is
    // the same move the board menu's picker makes, offered here as well because
    // this screen is where a board the roster refuses sends the player, and it
    // is the only way off a Stage a jump landed on and cannot open.
    jump,
    // Leave. Nothing is lost: every gesture was committed and saved when it was
    // made.
    quit,
};

struct ManagementIntent final {
    ManagementVerb verb{ManagementVerb::none};
    campaign::PersistentEntityId member{};
    campaign::DefinitionRef item{};
    // Which Stage a `jump` is for, as the campaign node identity the session
    // published. Zero for every other verb.
    std::uint64_t stage{};
};

// Why a management gesture did not become a campaign fact.
//
// Two channels, and the split is the same one `SlotFailure` makes: the session
// has exactly one refusal of its own (there is no company to manage) and
// everything else is the campaign's own vocabulary, unrepeated here. A store
// that cannot pay is `insufficient_items`; a member the campaign never met is
// `unknown_unit`; a gift to somebody the campaign has buried is `unit_is_dead`.
// A client prints whichever is set, by the name its owner gave it.
//
// A `field` that would take the company past the board's authored capacity is
// not in either channel, and deliberately: it is refused before a batch is
// built, by the screen reading `CompanyManagement::fielded` and `capacity`, in
// the roster's own word `over_deployment_capacity`. That is the same shape the
// terminal already refuses a `field` for somebody the next board never placed,
// and the engine's own gate is still `join_campaign_roster`, which refuses an
// over-cap company however a screen counted.
enum class ManagementError : std::uint8_t {
    none = 0,
    // The campaign is not standing anywhere a company can be managed: it was
    // never begun, or its flow has stalled.
    not_managing,
};

[[nodiscard]] std::string_view management_error_name(
    ManagementError error
) noexcept;

// One management gesture, as it was committed or as it was refused.
struct ManagementCommit final {
    ManagementError error{ManagementError::none};
    // The campaign's own answer to the batch. Meaningful whenever `error` is
    // `none`, which is whenever a batch was actually built.
    campaign::OutcomeApplication application;
    // The batch itself, so a narrator can say what moved by reading the very
    // operations that were committed rather than being told a second time.
    campaign::CampaignOutcomeBatch batch;
    // The slot, written because the gesture committed. Untouched when nothing
    // committed, and therefore `none`.
    storage::StorageError save{storage::StorageError::none};
    bool saved{false};

    // Whether the campaign now holds what the gesture asked for. A batch the
    // campaign had already committed is a success: it is the right answer to a
    // retry, and the thing asked for is held either way.
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ManagementError::none &&
               application.error == campaign::OutcomeError::none;
    }
};

// What a jump between Stages did to the campaign, or why it did nothing.
//
// `completion` is `campaign::jump_to_node`'s own answer, untouched, for the
// reason a management gesture carries `campaign::OutcomeApplication`: the
// campaign has vocabulary for every way this can fail and none of it should be
// re-spelled here.
struct StageJump final {
    CampaignSessionError error{CampaignSessionError::none};
    // Where the player asked to go, and what the campaign said about going
    // there.
    campaign::DefinitionRef target{};
    campaign::NodeCompletion completion{};
    // The slot, written because the campaign moved. A jump is a campaign fact
    // like any other, so it is saved the moment it commits: a tester who jumps,
    // switches the console off and comes back is standing where they jumped to.
    storage::StorageError save{storage::StorageError::none};
    bool saved{false};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CampaignSessionError::none &&
               completion.error == campaign::ProgressionError::none;
    }
};

// Everything a front end says about a campaign, as opposed to about a battle.
//
// Separate from `Presenter` on purpose. `Presenter` is what a console ROM
// implements today, in a library that must not learn what a save is; this is
// the surface a client grows when it gains a campaign. A front end that has
// both implements `CampaignFrontEnd` below.
class CampaignNarrator {
public:
    CampaignNarrator() = default;
    CampaignNarrator(const CampaignNarrator&) = delete;
    CampaignNarrator& operator=(const CampaignNarrator&) = delete;
    virtual ~CampaignNarrator() = default;

    // The campaign is under way. `resumed` distinguishes a save that was read
    // back from a roster that was just founded, and `store` is what the company
    // owns beyond what its members are already carrying: empty on a founding,
    // and whatever the save says on a resume.
    virtual void campaign_begun(
        const std::vector<RosterEntry>& roster,
        const std::vector<campaign::InventoryStack>& store,
        std::string_view slot,
        bool resumed
    ) = 0;

    // A slot was asked for and could not be honoured. The session is still
    // holding the campaign it was already holding. That property belongs to
    // `campaign::load_campaign_migrated_into` and is proved in
    // `tests/campaign`; this is where a player is told about it.
    virtual void slot_refused(const SlotFailure& failure) = 0;

    virtual void board_prepared(const CampaignBoard& board) = 0;

    virtual void battle_aftermath(const BattleAftermath& aftermath) = 0;

    // Members an authored recruitment brought into the company, as the
    // campaign holds them after the batch that recruited them committed. The
    // driver calls this for a story node's recruits; a battle's arrive in the
    // aftermath, and a front end says the same sentence about both.
    virtual void members_joined(const std::vector<RosterEntry>& joined) = 0;

    // A player took the Stage picker, and this is what the campaign made of it.
    // Said whether it moved or was refused, because a refused jump that said
    // nothing would look exactly like a battle abandoned for no reason.
    //
    // **It is also the far end of the window `board_prepared` opened**, when the
    // jump came out of a battle. A jump ends a board without an aftermath, so
    // this is the last moment a front end holding the prepared board is entitled
    // to it; one that borrows it must drop it here exactly as it drops it in
    // `battle_aftermath`.
    //
    // The slot arrives separately, through `campaign_saved`, because a jump
    // writes the campaign the moment it commits and a write that failed has to
    // reach the same place a failed write after a battle reaches.
    //
    // Defaulted to nothing rather than made pure, unlike everything else on this
    // interface. A front end that never offers the picker can never cause one,
    // so requiring it to say something about one would be requiring an empty
    // function of every campaign client for the sake of a testing aid.
    virtual void stage_jumped(const StageJump& jump) { (void)jump; }

    // The campaign was written to its slot, or was not.
    virtual void campaign_saved(
        std::string_view slot,
        storage::StorageError error
    ) = 0;

    // The company, between battles, with the next board's placements known.
    // Said once when the stage opens; the intent question below carries the
    // company again after every gesture, so a front end that redraws never has
    // to remember one.
    virtual void management_opened(const CompanyManagement& company) = 0;

    // What a management gesture did, or what the campaign refused about it.
    virtual void management_committed(const ManagementCommit& result) = 0;

    // What to do with the company. Asked until it answers `proceed` or `quit`;
    // `none` means the player said something that was not a verb, and is asked
    // again rather than treated as a proceed.
    //
    // On `CampaignNarrator` rather than on `Presenter` for the same reason the
    // deployment question is on `Presenter`: the question belongs beside the
    // vocabulary it is asked in, and a console front end must not learn that a
    // company exists.
    [[nodiscard]] virtual ManagementIntent next_management_intent(
        const CompanyManagement& company
    ) = 0;
};

// A front end that both draws a battle and narrates a campaign.
class CampaignFrontEnd : public Presenter, public CampaignNarrator {};

struct CampaignSessionOptions final {
    // Where the campaign is written after every battle. Validated by
    // `storage::is_valid_slot_name` before anything is played, because a slot
    // name that cannot be written is a campaign that would be lost at the end
    // of the first battle rather than at the start of the session.
    std::string slot{"campaign"};
    // Read the slot back before playing. A slot that is absent, damaged, or
    // written against content this build does not have is reported by name and
    // the freshly founded campaign is played instead.
    bool resume{false};
    simulation::Side player_side{simulation::Side::first};
    // Whether this session offers the Stage picker. Defaulted from the build, so
    // a console image carries whatever it was built with and nothing has to be
    // passed at every call site.
    //
    // Carried here rather than read from `stage_picker_built` inside the session
    // for one practical reason: the constant is decided where this library is
    // compiled, so a test cannot ask for the other side of the gate by defining
    // it in its own translation unit. An option can be set, and both halves are
    // then checked by the ordinary gate rather than by whichever configuration
    // the machine happened to use.
    bool stage_picker{stage_picker_built};
};


class CampaignBoards {
public:
    CampaignBoards() = default;
    CampaignBoards(const CampaignBoards&) = delete;
    CampaignBoards& operator=(const CampaignBoards&) = delete;
    virtual ~CampaignBoards() = default;

    // The authored board, before any roster touches it. A board this provider
    // does not have answers with its own load error, which the session reports
    // as `board_rejected`.
    [[nodiscard]] virtual package_runtime::EncounterLoadResult board(
        std::uint64_t encounter_id
    ) const = 0;
};

// Boards out of a mounted package: `package_runtime::load_encounter`, and
// nothing else. This is what every client that has a package uses, and it is
// what `run_persistent_campaign` builds for itself.
class PackageBoards final : public CampaignBoards {
public:
    explicit PackageBoards(
        const package_format::LoadedPackage& package
    ) noexcept
        : package_{&package} {}

    [[nodiscard]] package_runtime::EncounterLoadResult board(
        std::uint64_t encounter_id
    ) const override;

private:
    const package_format::LoadedPackage* package_;
};

// The campaign a player keeps, as an object the caller pumps.
//
// This is the same sequence `run_persistent_campaign` walks, turned inside out
// so that whoever owns the loop can be somebody else: found or resume, stand on
// a node, prepare a board through the roster, play, derive, commit, save, walk
// the edge. A terminal front end owns its own loop and calls
// `run_persistent_campaign`, which is a thin driver over this. A browser cannot
// own a blocking loop at all: its battle is a sequence of clicks arriving over
// many event-loop turns, and it must be able to leave a battle and come back to
// an authoring surface. Both drive the identical steps, which is the point: a
// campaign fact is derived once, in C++, and read by every client.
//
// The session derives nothing itself. Every number it hands out came from
// `campaign_runtime::derive_battle_progression`, `campaign::complete_node`, or
// `campaign::CampaignState`.
class CampaignSession final {
public:
    // Neither the package, the boards, nor the device is owned; all three must
    // outlive the session.
    CampaignSession(
        const package_format::LoadedPackage& package,
        std::uint64_t campaign_id,
        const CampaignBoards& boards,
        storage::SlotStorage& device,
        const CampaignSessionOptions& options
    );

    // Founds the roster from the content, enters the graph, and reads the slot
    // back over the top when the options ask for it. A slot that cannot
    // be honoured sets `refused` and fills `failure` in the vocabulary of
    // whichever layer refused it; the freshly founded campaign is what stays
    // standing, and playing continues.
    [[nodiscard]] CampaignSessionError begin(
        SlotFailure& failure,
        bool& refused,
        bool& resumed
    );

    // The roster as the campaign holds it right now: identities and unit types
    // from the founding, availability, progression and kit read back out of
    // state.
    [[nodiscard]] std::vector<RosterEntry> roster() const;

    // What the company owns beyond what its members carry, right now.
    [[nodiscard]] std::vector<campaign::InventoryStack> store() const;

    // Where the campaign stands. `kind` says what to do next, and
    // `encounter_id` is meaningful only for an encounter node.
    struct Standing final {
        CampaignSessionError error{CampaignSessionError::none};
        campaign::DefinitionRef node{};
        package_runtime::CampaignNodeKind kind{
            package_runtime::CampaignNodeKind::story
        };
        std::vector<std::uint64_t> dialogue_ids;
        std::uint64_t encounter_id{};
    };

    [[nodiscard]] Standing standing() const;

    // Completes a story node through the graph rather than stepping past it,
    // because the history a save resumes from is the history the graph wrote.
    // A story node that recruits does it in that same batch, and `joined` is
    // who it brought in.
    [[nodiscard]] CampaignSessionError advance_story(
        std::vector<RosterEntry>& joined
    );

    // The board at the standing node, with everyone the roster cannot field
    // left off it and everyone who can field it carrying what their level-ups
    // granted. The session remembers it until the battle is committed.
    struct PreparedBoard final {
        CampaignSessionError error{CampaignSessionError::none};
        campaign_runtime::RosterError roster_error{
            campaign_runtime::RosterError::none
        };
        // The board to fight, exactly as `campaign_runtime` published it.
        campaign_runtime::CampaignEncounter encounter;
        // The same board as a narrator reads it.
        CampaignBoard board;
    };

    [[nodiscard]] PreparedBoard prepare_board();

    // -----------------------------------------------------------------------
    // The Stage picker
    //
    // A testing aid, and the one place in this session that exists to be used
    // by somebody checking a game rather than playing one. Reaching a late
    // Stage on a console to look at one thing in it costs playing every Stage
    // before it, every time, and this is what stops it costing that.
    // -----------------------------------------------------------------------

    // Every Stage of this campaign, with the ones this playthrough has reached
    // marked, in the order its flow reaches them: breadth first from the entry
    // node, each node's edges taken in the order `select_transition` considers
    // them. That is the order a player meets the Stages, and for a campaign
    // without branches it is the order the author wrote them. It is derived
    // rather than read because a compiled campaign's nodes arrive sorted by
    // identity, which is a hash, and the record carries no ordinal.
    //
    // **Empty unless this was built with the picker**, which is the whole of
    // the gate and is decided here rather than by each client. A front end
    // offers the row when the list is not empty; it never reads a setting, and
    // a game that never turned the setting on cannot show the row on any
    // machine.
    [[nodiscard]] std::vector<CampaignStage> stages() const;

    // Stand the campaign on the Stage `node_id` names, without playing the ones
    // between.
    //
    // The batch is empty and that is the design rather than a shortcut. A jump
    // moves the campaign and changes nothing else: no objective is recorded, no
    // world flag is set, nobody is recruited on behalf of the Stages that were
    // passed over. So a Stage reached this way has not done what the route to it
    // would have done, and the battle there can be unwinnable while the
    // transition out of it can match nothing. The alternative is inventing the
    // author's facts, which would be wrong differently at every branch; what is
    // done instead is `CampaignStage::reached`, so a player can see which jumps
    // are the safe ones before they take one.
    //
    // Committed and saved on the spot, exactly as a management gesture is. A
    // jump is a campaign fact, and the route records it: there is no undo, and
    // the way back is another jump.
    //
    // **Every jump costs the save a route step and an outcome id, and nothing
    // prunes them.** That is what makes a jump resumable and idempotent, and it
    // is the same growth a battle causes; what is different is that a battle
    // takes minutes and a jump takes a press, so a very long checking session
    // is the one way a campaign's save grows towards a slot's limit without the
    // campaign getting anywhere. A slot too small to take the write refuses it
    // by name, and `campaign_saved` carries that refusal to the player, which is
    // why the driver reports the slot after a jump rather than only after a
    // battle.
    [[nodiscard]] StageJump jump_to_stage(std::uint64_t node_id);

    // -----------------------------------------------------------------------
    // The company, between battles
    //
    // Four gestures and a view. Each gesture builds one
    // `campaign::CampaignOutcomeBatch`, commits it through
    // `campaign::apply_outcome`, and writes the slot before returning when it
    // committed. There is deliberately no basket, no pending
    // arrangement and no apply step: the campaign a screen shows is the
    // campaign the slot holds, at every moment the player can look at it.
    // -----------------------------------------------------------------------

    // The company as it stands, and what the next board has room for. Costs one
    // board decode the first time it is asked at a node and nothing after that,
    // because which members a board *places* is a property of the board and not
    // of the campaign.
    [[nodiscard]] CompanyManagement management();

    // One of `item` out of the store and into `member`'s hands.
    [[nodiscard]] ManagementCommit give_item(
        campaign::PersistentEntityId member,
        const campaign::DefinitionRef& item
    );

    // One of `item` out of `member`'s hands and into the store.
    [[nodiscard]] ManagementCommit take_item(
        campaign::PersistentEntityId member,
        const campaign::DefinitionRef& item
    );

    // Whether `member` takes the next board. Fielding is
    // `Availability::available` and benching is `Availability::retired`, which
    // is what the roster calls a member who is with the company and not
    // deployable. The board leaves them off through the exclusion pass that
    // already leaves off the dead, and the simulation learns nothing.
    [[nodiscard]] ManagementCommit set_fielded(
        campaign::PersistentEntityId member,
        bool fielded
    );

    // Turns a finished battle into campaign facts and commits them atomically:
    // the permanent deaths, the objective results, and everything
    // `derive_battle_progression` derived, in one batch through
    // `campaign::complete_node`. Must follow a `prepare_board` that succeeded.
    [[nodiscard]] CampaignSessionError commit_battle(
        const BattleReport& battle,
        BattleAftermath& aftermath
    );

    // Writes the campaign to its slot. Called after every commit, whether or
    // not the campaign moved: a node with no route out of it still buried
    // somebody and still taught somebody something.
    [[nodiscard]] storage::StorageError save();

    // The bytes a save would write, for a caller that keeps its own device.
    [[nodiscard]] std::vector<std::uint8_t> save_bytes();

private:
    [[nodiscard]] const package_runtime::CampaignNode* node_at(
        const campaign::DefinitionRef& standing
    ) const noexcept;

    // The same lookup by the authored identity rather than by the reference
    // built from it. The Stage picker walks the flow by node id, and turning
    // each one into a reference only to compare it back would be doing the
    // derivation twice per edge.
    [[nodiscard]] const package_runtime::CampaignNode* node_by_id(
        std::uint64_t node_id
    ) const noexcept;

    // One authored member, and the node that brings them in. A member the
    // campaign is founded with joins at node zero, which is no node.
    struct AuthoredMember final {
        RosterEntry entry;
        std::uint64_t join_node_id{};
    };

    // The company as the campaign holds it right now: authored identities,
    // names and unit types, with availability and progression read back out of
    // the state. A member the state does not hold has not joined yet and is on
    // no roster until the node that recruits them completes.
    [[nodiscard]] std::vector<RosterEntry> roster_now() const;

    // What a completing node recruits, as operations for that node's own
    // batch: the recruitment, the availability, and the kit their unit type
    // says they arrive with. Empty for a node that recruits nobody, which is
    // most of them.
    [[nodiscard]] std::vector<campaign::CampaignOutcomeOperation> recruitment_at(
        std::uint64_t node_id
    ) const;

    // What a completing node grants the company's store, as operations for
    // that node's own batch. Node zero is the founding stock. Empty for a node
    // that grants nothing, which is most of them.
    //
    // Separate from `recruitment_at` and not folded into it, because an empty
    // recruitment at node zero is how `begin` refuses a campaign that authors
    // no company, and a starting store must not be able to answer that
    // question on a roster's behalf.
    [[nodiscard]] std::vector<campaign::CampaignOutcomeOperation> grants_at(
        std::uint64_t node_id
    ) const;

    // Who that node recruited, as the campaign holds them after the batch
    // committed.
    [[nodiscard]] std::vector<RosterEntry> recruited_at(
        std::uint64_t node_id
    ) const;

    // One management gesture, committed or refused. Both moves and both
    // availability verbs are this function with different operations, because
    // they are the same transaction with different contents: build, identify,
    // apply, and write the slot only when something actually changed.
    [[nodiscard]] ManagementCommit commit_management(
        std::vector<campaign::CampaignOutcomeOperation> operations
    );

    const package_format::LoadedPackage* package_;
    std::uint64_t campaign_id_;
    const CampaignBoards* boards_;
    storage::SlotStorage* device_;
    CampaignSessionOptions options_;

    campaign::CampaignGraph graph_;
    package_runtime::CampaignDefinition authored_;
    std::vector<campaign_runtime::RosterAssignment> assignments_;
    // Everybody the campaign can ever hold, in authored order. A member who
    // has not joined yet is here and is not on the roster: the assignment
    // table names them, so a board that places them early leaves them off
    // rather than fielding a stranger.
    std::vector<AuthoredMember> members_;
    // What the author wrote about individual characters beyond their unit
    // types, attached to every board this session prepares. Content rather than
    // state: it is read once out of the campaign record and never moves, which
    // is exactly why it does not live in the save.
    std::vector<package_runtime::MemberSpecificity> specificities_;
    campaign::CampaignSave live_;
    bool loaded_{false};

    // What the last `prepare_board` published, kept down to what committing a
    // battle actually reads.
    //
    // Keeping the whole `campaign_runtime::CampaignEncounter` alongside the
    // copy handed to the caller would leave the terrain, the unit definitions,
    // the weapon, ability and item registries, the placements and the
    // objectives all existing twice for the length of a battle, on top of the
    // `sim::Encounter` built from them. A commit reads two things out of that:
    // the join between board units and campaign members, and the type each
    // board unit was fielded as
    // (`campaign_runtime::derive_battle_progression`, which reads the board for
    // nothing else). So those are what is kept, and the board itself is moved
    // out to whoever is going to fight it.
    //
    // On a machine that counts its heap in kilobytes this is the difference
    // between publishing a board and failing on the shape of the heap. On every
    // other machine it is the same campaign, derived the same way, holding
    // less.
    campaign::BattleBinding prepared_binding_;
    std::vector<campaign_runtime::BoardUnitType> prepared_unit_types_;
    campaign::DefinitionRef prepared_node_{};
    std::uint64_t prepared_node_id_{};
    std::uint64_t prepared_encounter_{};
    std::uint64_t prepared_sequence_{};
    bool prepared_ready_{false};

    // Which members the standing node's board has a placement for, and which
    // board that answer is about. A board's placements do not move when a
    // campaign does, so this is cached per encounter rather than recomputed per
    // gesture. A management screen asks the question after every keystroke.
    std::uint64_t placeable_encounter_{};
    std::vector<campaign::PersistentEntityId> placeable_;
    std::uint16_t placeable_capacity_{};
};

// Play a campaign the player keeps.
//
// Begins (or resumes), and then, for each node the graph stands on: presents
// the node's dialogue, loads the board with everyone the roster cannot field
// left off it, plays the battle through the presenter's existing vocabulary,
// derives the battle's campaign consequences, commits them, narrates them, and
// writes the campaign to its slot before walking the next edge. A terminal node
// ends it.
//
// Quitting mid-battle returns `none` without committing that battle: an
// unfinished fight is not an outcome, and the slot still holds the campaign as
// it stood after the last one.
[[nodiscard]] CampaignSessionError run_persistent_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id,
    Presenter& presenter,
    CampaignNarrator& narrator,
    storage::SlotStorage& storage,
    const CampaignSessionOptions& options
);

}  // namespace grandleon::client
