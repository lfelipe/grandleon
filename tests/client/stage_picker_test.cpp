// SPDX-License-Identifier: MIT
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/client/session.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/storage/memory_storage.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Leaving a battle for another Stage, on the larger sample campaign.
//
// The claim is one sentence: *a player checking a game can stand on any of its
// Stages without playing the ones before it, and the campaign that results is
// one the campaign layer accepts and the slot holds.*
//
// Tarnholt is the content because it is the case the aid exists for: six boards
// with cutscenes and a branch between them, where reaching the last one the
// ordinary way is five battles of work every time somebody wants to look at it.
//
// The other half of the claim is the limit, and it is asserted just as hard.
// A jump moves the campaign and changes nothing else, so the Stages it passed
// over are still unreached, the company is exactly the company that was
// standing before, and a screen can tell a safe jump from a risky one because
// the session publishes which Stages this playthrough has actually stood on.
//
// The picker is a build define, and one binary can only be one side of it. This
// executable carries GRANDLEON_STAGE_PICKER however the tree was configured, so
// what follows is the picker working; stage_picker_absent_test.cpp is the other
// side and says what a build without it offers.

namespace campaign = grandleon::campaign;
namespace client = grandleon::client;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace cr = grandleon::campaign_runtime;
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

const std::uint64_t tarnholt = core::stable_content_id_v1("tarnholt_line");
const std::uint64_t fordlight = core::stable_content_id_v1("fordlight_battle");
const std::uint64_t harrow = core::stable_content_id_v1("harrow_burn_battle");
const std::uint64_t coldgate = core::stable_content_id_v1("coldgate_battle");

// The sample campaign, compiled as written or with the Stage picker switched
// on. Switched on here rather than in the file, because the file is a game
// somebody may ship and the setting is one this test is about rather than one
// Tarnholt wants.
pf::LoadedPackage compile_tarnholt() {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) + "/games/tarnholt/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Tarnholt source opens");
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and maps natively");
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and mounts");
    return loaded.package;
}

// A campaign standing on its first board: the three story nodes walked, the
// company left as it was founded.
client::CampaignSessionError walk_to_the_first_board(
    client::CampaignSession& session
) {
    for (int guard = 0; guard < 16; ++guard) {
        const client::CampaignSession::Standing where = session.standing();
        if (where.error != client::CampaignSessionError::none) return where.error;
        if (where.kind != pr::CampaignNodeKind::story) {
            return client::CampaignSessionError::none;
        }
        std::vector<client::RosterEntry> joined;
        const client::CampaignSessionError moved = session.advance_story(joined);
        if (moved != client::CampaignSessionError::none) return moved;
    }
    return client::CampaignSessionError::flow_stalled;
}

const client::CampaignStage* find(
    const std::vector<client::CampaignStage>& stages,
    std::uint64_t node_id
) {
    const auto found = std::find_if(
        stages.begin(), stages.end(),
        [node_id](const client::CampaignStage& stage) {
            return stage.node_id == node_id;
        }
    );
    return found == stages.end() ? nullptr : &*found;
}

// The list itself: every board, in the order the author wrote them, under the
// names the author gave them, with nowhere marked reached before anywhere has
// been.
void the_stages_are_the_authored_boards_in_order() {
    const pf::LoadedPackage package = compile_tarnholt();
    const client::PackageBoards boards{package};
    storage::MemorySlotStorage device;
    client::CampaignSessionOptions options;
    // Asked for explicitly rather than left to the build: this executable
    // checks the picker, so it says so, and the same source checks it on a
    // machine that never configured the define.
    options.stage_picker = true;
    client::CampaignSession session{package, tarnholt, boards, device, options};

    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        session.begin(failure, refused, resumed) ==
            client::CampaignSessionError::none,
        "a campaign whose author asked for the picker begins"
    );

    const std::vector<client::CampaignStage> stages = session.stages();
    expect(stages.size() == 6U, "Tarnholt's six boards are its six Stages");
    // The cutscenes between them are not Stages. Jumping to one would be asking
    // to watch it rather than to be anywhere, and the campaign walks past a
    // story node of its own accord the moment it stands there.
    expect(
        std::none_of(
            stages.begin(), stages.end(),
            [](const client::CampaignStage& stage) {
                return stage.encounter_id == 0U;
            }
        ),
        "and none of them is a story node"
    );
    expect(
        stages.front().node_id == fordlight && stages.back().node_id == coldgate,
        "in the order the author wrote them, first board first"
    );
    expect(
        stages.front().name == "The Fordlight Crossing" &&
            stages.back().name == "The Coldgate",
        "under the names the author gave them, read out of the package rather "
        "than derived from a number"
    );
    expect(
        std::none_of(
            stages.begin(), stages.end(),
            [](const client::CampaignStage& stage) { return stage.reached; }
        ),
        "and a campaign that has fought nothing has reached nowhere"
    );
    // Standing is about where the campaign is now, and it begins on a cutscene,
    // which is nowhere on this list.
    expect(
        std::none_of(
            stages.begin(), stages.end(),
            [](const client::CampaignStage& stage) { return stage.standing; }
        ),
        "nor is it standing on any of them, because it opens on a story node"
    );

    expect(
        walk_to_the_first_board(session) == client::CampaignSessionError::none,
        "the campaign walks its opening cutscenes"
    );
    const std::vector<client::CampaignStage> arrived = session.stages();
    const client::CampaignStage* const first = find(arrived, fordlight);
    expect(first != nullptr, "and stands on the first board");
    expect(
        first != nullptr && first->standing && first->reached,
        "which is now both where it is and somewhere it has been"
    );
    expect(
        std::count_if(
            arrived.begin(), arrived.end(),
            [](const client::CampaignStage& stage) { return stage.reached; }
        ) == 1,
        "and it is the only Stage reached, because it is the only one walked to"
    );
}

// The move itself, forwards past five boards and then back, with the campaign
// still a campaign at every step and the slot holding what the player is
// looking at.
void a_jump_stands_on_a_stage_and_keeps_it() {
    const pf::LoadedPackage package = compile_tarnholt();
    const client::PackageBoards boards{package};
    storage::MemorySlotStorage device;
    client::CampaignSessionOptions options;
    // Asked for explicitly rather than left to the build: this executable
    // checks the picker, so it says so, and the same source checks it on a
    // machine that never configured the define.
    options.stage_picker = true;
    options.slot = "campaign";

    client::CampaignSession session{package, tarnholt, boards, device, options};
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        session.begin(failure, refused, resumed) ==
            client::CampaignSessionError::none &&
            walk_to_the_first_board(session) ==
                client::CampaignSessionError::none,
        "the campaign reaches its first board"
    );

    // Who the company is, before. A jump must leave every one of them exactly
    // as they were: it moves the campaign and it does not play the battles it
    // skipped, so nobody grew, nobody fell and nothing was picked up.
    const std::vector<client::RosterEntry> company = session.roster();
    const std::vector<campaign::InventoryStack> store = session.store();
    expect(!company.empty(), "with a company to carry");

    const client::StageJump jumped = session.jump_to_stage(coldgate);
    expect(
        static_cast<bool>(jumped),
        "the last board is reachable in one move, five battles ahead of where "
        "the campaign stands"
    );
    expect(
        jumped.completion.advanced && jumped.completion.target == jumped.target,
        "and the campaign says it moved, and where to"
    );
    expect(
        jumped.saved && jumped.save == storage::StorageError::none,
        "and the slot is written on the spot, because a jump is a campaign fact"
    );

    const client::CampaignSession::Standing where = session.standing();
    expect(
        where.error == client::CampaignSessionError::none &&
            where.kind == pr::CampaignNodeKind::encounter &&
            where.encounter_id == core::stable_content_id_v1(
                "tarnholt_line/coldgate_battle"
            ),
        "the campaign stands on the board that Stage is fought at"
    );

    // The limit, asserted rather than described. Nothing between was walked, so
    // nothing between is marked, and a screen offering the list can tell the
    // player which jumps are the safe ones.
    const std::vector<client::CampaignStage> after = session.stages();
    const client::CampaignStage* const end = find(after, coldgate);
    const client::CampaignStage* const middle = find(after, harrow);
    expect(end != nullptr && end->standing && end->reached, "the target is both");
    expect(
        middle != nullptr && !middle->reached,
        "and a Stage the jump passed over is still unreached, because it is"
    );
    expect(
        std::count_if(
            after.begin(), after.end(),
            [](const client::CampaignStage& stage) { return stage.reached; }
        ) == 2,
        "so exactly two Stages have been stood on: the one walked to and the "
        "one jumped to"
    );

    // And the company is untouched, which is the other half of "it moves the
    // campaign and changes nothing else".
    const std::vector<client::RosterEntry> after_company = session.roster();
    expect(
        after_company.size() == company.size() &&
            std::equal(
                company.begin(), company.end(), after_company.begin(),
                [](const client::RosterEntry& before,
                   const client::RosterEntry& now) {
                    return before.member.value == now.member.value &&
                           before.name == now.name &&
                           before.availability == now.availability &&
                           before.progression.level == now.progression.level &&
                           before.progression.experience ==
                               now.progression.experience &&
                           before.carried.size() == now.carried.size();
                }
            ),
        "the same company, at the same levels, carrying the same things: a jump "
        "grants nothing on behalf of the battles it skipped"
    );
    expect(
        session.store().size() == store.size(),
        "and the company's store is what it was"
    );

    // And here is the limit, on the shipped campaign, on the first jump anybody
    // would try. Tarnholt's last board carries the objective "keep Captain
    // Mirea alive", and Mirea joins the company at a cutscene *after* the first
    // battle. A jump recruits nobody on behalf of what it passed over, so the
    // board names a character this campaign does not have and the roster refuses
    // to publish it, by name.
    //
    // This is asserted rather than worked around because it is the honest cost
    // of not inventing an author's facts, and because it is what makes the
    // picker's second home necessary: the screen this refusal sends a player
    // back to has to be able to jump, or a jump could leave a written slot
    // standing at a Stage nothing can open.
    const client::CampaignSession::PreparedBoard unopenable =
        session.prepare_board();
    expect(
        unopenable.error == client::CampaignSessionError::board_rejected &&
            unopenable.roster_error ==
                cr::RosterError::unavailable_objective_target,
        "the board at a Stage jumped to may refuse to open, because the "
        "character its objective names joins at a cutscene the jump skipped"
    );

    // Backwards is the same move, and it is the way out of the paragraph above.
    const client::StageJump back = session.jump_to_stage(fordlight);
    expect(static_cast<bool>(back), "and the first board is reachable from the last");
    expect(
        session.standing().encounter_id ==
            core::stable_content_id_v1("tarnholt_line/fordlight_battle"),
        "with the campaign standing on it again"
    );
    expect(
        find(session.stages(), coldgate) != nullptr &&
            find(session.stages(), coldgate)->reached &&
            !find(session.stages(), coldgate)->standing,
        "and the Stage it came back from remembered as somewhere it has been"
    );

    // What the slot holds is what the player is looking at. A second session
    // over the same device resumes standing where the last jump left it, which
    // is the whole reason a jump saves rather than waiting for a battle to
    // finish.
    client::CampaignSessionOptions resuming = options;
    resuming.resume = true;
    client::CampaignSession reopened{
        package, tarnholt, boards, device, resuming
    };
    bool refused_again = false;
    bool resumed_again = false;
    expect(
        reopened.begin(failure, refused_again, resumed_again) ==
                client::CampaignSessionError::none &&
            resumed_again && !refused_again,
        "the slot reads back"
    );
    expect(
        reopened.standing().encounter_id ==
            core::stable_content_id_v1("tarnholt_line/fordlight_battle"),
        "onto the Stage the last jump stood on"
    );
    const std::vector<client::CampaignStage> remembered = reopened.stages();
    expect(
        find(remembered, coldgate) != nullptr &&
            find(remembered, coldgate)->reached &&
            find(remembered, harrow) != nullptr &&
            !find(remembered, harrow)->reached,
        "and remembering which Stages were stood on, because that is the route "
        "the save carries rather than anything this session counted"
    );
}

// The whole road, through the driver both consoles run, and both surfaces the
// picker has.
//
// The front end here does what a person checking Tarnholt does: opens the first
// board, takes the picker out of the pause menu and asks for the last Stage,
// finds that the board there will not open because the character its objective
// names has not joined, and takes the picker again — this time off the company
// screen the refusal landed it on — to go back somewhere playable.
//
// That second half is the point. `run_persistent_campaign` is the loop both
// consoles are inside when their pause menu opens, and nothing below the
// presenter seam knows a campaign exists; what carries the request out of a
// battle is `BattleReport::jump_to_stage`. But a refused board never reaches a
// battle at all, so the only screen a stranded player can see is the company,
// and the picker has to be reachable from there or the slot is written and
// stuck.
class ChecksTheLastStage final : public client::CampaignFrontEnd {
public:
    void present_dialogue(const pr::Dialogue&) override {}
    void battle_begins(
        const sim::EncounterSnapshot&,
        const client::Roster&,
        sim::Side,
        const std::vector<std::uint64_t>&
    ) override {
        ++battles_;
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
    void campaign_ended() override {}

    [[nodiscard]] client::Intent next_intent(
        const sim::EncounterSnapshot&,
        const client::Roster&
    ) override {
        client::Intent intent;
        if (asked_++ == 0) {
            intent.kind = client::IntentKind::jump_to_stage;
            intent.stage_id = coldgate;
            return intent;
        }
        intent.kind = client::IntentKind::quit;
        return intent;
    }

    void campaign_begun(
        const std::vector<client::RosterEntry>&,
        const std::vector<campaign::InventoryStack>&,
        std::string_view,
        bool
    ) override {}
    void slot_refused(const client::SlotFailure&) override {}
    void board_prepared(const client::CampaignBoard& board) override {
        offered_on_the_board_ = board.stages;
    }
    void battle_aftermath(const client::BattleAftermath&) override {}
    void members_joined(const std::vector<client::RosterEntry>&) override {}
    void campaign_saved(std::string_view, storage::StorageError error) override {
        ++saves_;
        if (error != storage::StorageError::none) ++save_failures_;
    }
    void management_opened(const client::CompanyManagement& company) override {
        offered_on_the_company_ = company.stages;
        if (company.refused != cr::RosterError::none) {
            refusals_.push_back(company.refused);
        }
    }
    void management_committed(const client::ManagementCommit&) override {}
    void stage_jumped(const client::StageJump& jump) override {
        jumps_.push_back(jump);
    }
    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement& company
    ) override {
        // The escape, taken exactly where a stranded player would have to take
        // it: the board would not open, so this screen is all there is.
        if (company.refused != cr::RosterError::none) {
            client::ManagementIntent intent;
            intent.verb = client::ManagementVerb::jump;
            intent.stage = fordlight;
            return intent;
        }
        return {client::ManagementVerb::proceed, {}, {}, 0U};
    }

    int battles_{0};
    int asked_{0};
    int saves_{0};
    int save_failures_{0};
    std::vector<client::StageJump> jumps_;
    std::vector<cr::RosterError> refusals_;
    std::vector<client::CampaignStage> offered_on_the_board_;
    std::vector<client::CampaignStage> offered_on_the_company_;
};

void the_driver_carries_the_request_out_of_both_screens() {
    const pf::LoadedPackage package = compile_tarnholt();
    storage::MemorySlotStorage device;
    ChecksTheLastStage front_end;
    client::CampaignSessionOptions options;
    // Asked for explicitly rather than left to the build: this executable
    // checks the picker, so it says so, and the same source checks it on a
    // machine that never configured the define.
    options.stage_picker = true;
    options.slot = "campaign";

    expect(
        client::run_persistent_campaign(
            package, tarnholt, front_end, front_end, device, options
        ) == client::CampaignSessionError::none,
        "the campaign runs and ends by being left, not by an error: a jump that "
        "lands on a board nothing can open must not end the session"
    );
    expect(
        front_end.jumps_.size() == 2U &&
            static_cast<bool>(front_end.jumps_[0]) &&
            static_cast<bool>(front_end.jumps_[1]),
        "two jumps were taken and both moved the campaign: out of the battle, "
        "and off the screen the refused board sent the player to"
    );
    expect(
        !front_end.refusals_.empty() &&
            front_end.refusals_.front() ==
                cr::RosterError::unavailable_objective_target,
        "with the roster's own word for why the board would not open said to "
        "the player in between"
    );
    expect(
        front_end.battles_ == 2,
        "and two boards were opened: the one left, and the one the second jump "
        "went back to"
    );
    // The slot, reported on the channel every other commit reports it on. A
    // jump writes the campaign the moment it commits, and a write that failed
    // has to reach the same place a failed write after a battle reaches: a
    // tester who believes a jump was kept and finds it was not has lost exactly
    // the work this aid exists to save them. Neither battle here committed
    // anything, so these two saves are the two jumps and nothing else.
    expect(
        front_end.saves_ == 2 && front_end.save_failures_ == 0,
        "both jumps reported the slot they wrote, and both writes took"
    );
    expect(
        front_end.offered_on_the_board_.size() == 6U &&
            front_end.offered_on_the_company_.size() == 6U,
        "the same six Stages are offered on both screens, handed over before "
        "each is drawn, which is what the two menus are made of"
    );

    // What the slot holds afterwards. Neither battle committed anything, so the
    // only things this campaign has ever recorded are the two jumps, and it is
    // standing where the second one left it.
    const client::PackageBoards boards{package};
    client::CampaignSessionOptions resuming = options;
    resuming.resume = true;
    client::CampaignSession reopened{
        package, tarnholt, boards, device, resuming
    };
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        reopened.begin(failure, refused, resumed) ==
                client::CampaignSessionError::none &&
            resumed,
        "the slot the driver wrote reads back"
    );
    expect(
        reopened.standing().encounter_id ==
            core::stable_content_id_v1("tarnholt_line/fordlight_battle"),
        "standing where the player went, not where they started and not where "
        "they could not stay"
    );
}

// A player who jumps a great many times is still a player.
//
// The driver bounds its own loop so that a flow which neither ends nor moves is
// reported rather than spun on. That bound is about the graph: it exists for a
// campaign that would go round with nobody touching a button. A jump is a press,
// so it must not count against it, or a long checking session would end by
// telling a tester their campaign's flow had stopped working — which would be
// false, and would be the aid reporting a bug in the game it was added to find
// bugs in.
class JumpsBackAndForth final : public client::CampaignFrontEnd {
public:
    void present_dialogue(const pr::Dialogue&) override {}
    void battle_begins(
        const sim::EncounterSnapshot&,
        const client::Roster&,
        sim::Side,
        const std::vector<std::uint64_t>&
    ) override {}
    void draw(const sim::EncounterSnapshot&, const client::Roster&) override {}
    void report(const sim::CommandResult&, const client::Roster&) override {}
    void refused(sim::CommandError) override {}
    void show_state(
        const sim::EncounterSnapshot&,
        std::uint64_t,
        const std::vector<sim::ObjectiveDefinition>&
    ) override {}
    void battle_ended(const sim::EncounterSnapshot&, std::uint64_t) override {}
    void campaign_ended() override {}
    [[nodiscard]] client::Intent next_intent(
        const sim::EncounterSnapshot&,
        const client::Roster&
    ) override {
        return {client::IntentKind::quit};
    }
    void campaign_begun(
        const std::vector<client::RosterEntry>&,
        const std::vector<campaign::InventoryStack>&,
        std::string_view,
        bool
    ) override {}
    void slot_refused(const client::SlotFailure&) override {}
    void board_prepared(const client::CampaignBoard&) override {}
    void battle_aftermath(const client::BattleAftermath&) override {}
    void members_joined(const std::vector<client::RosterEntry>&) override {}
    void campaign_saved(std::string_view, storage::StorageError) override {}
    void management_opened(const client::CompanyManagement&) override {}
    void management_committed(const client::ManagementCommit&) override {}
    void stage_jumped(const client::StageJump& jump) override {
        if (static_cast<bool>(jump)) ++taken_;
    }
    [[nodiscard]] client::ManagementIntent next_management_intent(
        const client::CompanyManagement&
    ) override {
        if (asked_ >= jumps) return {client::ManagementVerb::quit, {}, {}, 0U};
        client::ManagementIntent intent;
        intent.verb = client::ManagementVerb::jump;
        intent.stage = (asked_++ % 2) == 0 ? harrow : fordlight;
        return intent;
    }

    // Comfortably past the driver's own bound, so a jump that counted against
    // it would stall this run rather than merely approach the limit.
    static constexpr int jumps = 300;
    int asked_{0};
    int taken_{0};
};

void jumping_is_not_stalling() {
    const pf::LoadedPackage package = compile_tarnholt();
    storage::MemorySlotStorage device;
    JumpsBackAndForth front_end;
    client::CampaignSessionOptions options;
    // Asked for explicitly rather than left to the build: this executable
    // checks the picker, so it says so, and the same source checks it on a
    // machine that never configured the define.
    options.stage_picker = true;
    options.slot = "campaign";

    expect(
        client::run_persistent_campaign(
            package, tarnholt, front_end, front_end, device, options
        ) == client::CampaignSessionError::none,
        "three hundred jumps end by being left, not by the flow being called "
        "stalled"
    );
    expect(
        front_end.taken_ == JumpsBackAndForth::jumps,
        "and every one of them moved the campaign"
    );
}

}  // namespace

int main() {
    the_stages_are_the_authored_boards_in_order();
    a_jump_stands_on_a_stage_and_keeps_it();
    the_driver_carries_the_request_out_of_both_screens();
    jumping_is_not_stalling();
    if (failures == 0) std::cout << "stage picker checks passed\n";
    return failures == 0 ? 0 : 1;
}
