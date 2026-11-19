// SPDX-License-Identifier: MIT
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/state.hpp>
#include <grandleon/campaign_runtime/campaign_runtime.hpp>
#include <grandleon/client/campaign_session.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/manifest.hpp>
#include <grandleon/storage/memory_storage.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// What a fall costs the company, under both rules, over one identical battle.
//
// The claim this file exists to earn is the one the whole `characterLoss`
// setting rests on: **the rule changes what the campaign records and nothing
// else**. So the same source project is compiled twice: once as it is written,
// once with the recoverable rule set on the model the compiler reads. The same
// commands are given to the same board on both. If a single number the
// simulation produces differed between the two, the setting would be a rules
// change and would owe every golden and every console expectation a
// re-derivation. It does not, and this is where that is checked rather than
// asserted.
//
// The battle is the demo's river crossing, the same script
// `tests/client/campaign_management_test.cpp` and
// `tests/campaign_runtime/demo_permadeath_test.cpp` fight, and for the same
// reason: it is the shortest road in maintained content to a battle that ends
// with a member of the company on the ground.
//
// Setting the rule on the parsed model rather than in `games/demo`'s own
// `project.json` is deliberate. Turning it on in the demo would make the demo a
// different game, move the golden its conformance slice pins, and change what
// every other test that reads that file is reading. The setting is content, and
// the test supplies its own.

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

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The persistent identities founding assigns the demo's company, one-based in
// the order the campaign lists them, exactly as `client::CampaignSession`
// assigns them.
constexpr campaign::PersistentEntityId vanguard{1};
constexpr campaign::PersistentEntityId outrider{2};

const std::uint64_t muster_road = core::stable_content_id_v1("muster_road");
const std::uint64_t field_tonic = core::stable_content_id_v1("field_tonic");

// The maintained demo, compiled under whichever rule this run is about. The
// source is read off the disk and parsed by the real reader; only the one field
// the setting owns is set before the real compiler is handed the model.
pf::LoadedPackage compile_the_demo(pr::CharacterLoss rule, bool show_statistics = false,
                                    bool win_required = false) {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) + "/games/demo/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the maintained demo source opens");
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and maps natively");
    parsed.source.character_loss = rule == pr::CharacterLoss::recoverable
                                       ? gc::CharacterLoss::recoverable
                                       : gc::CharacterLoss::permanent;
    parsed.source.show_end_statistics = show_statistics;
    if (win_required) {
        const auto ally = std::find_if(
            parsed.source.unit_types.begin(), parsed.source.unit_types.end(),
            [](const auto& role) { return role.id == core::stable_content_id_v1("dawn_guard_unit"); }
        );
        const auto enemy = std::find_if(
            parsed.source.unit_types.begin(), parsed.source.unit_types.end(),
            [](const auto& role) { return role.id == core::stable_content_id_v1("river_watch_unit"); }
        );
        for (auto& role : parsed.source.classes) {
            if (role.id == ally->class_id) role.base_stats.health = 1;
        }
        for (auto& weapon : parsed.source.weapons) {
            if (weapon.id == enemy->starting_weapons.front()) {
                weapon.power = 30;
                weapon.accuracy = 100;
            }
        }
        for (auto& campaign : parsed.source.campaigns) {
            if (campaign.id != muster_road) continue;
            auto& first = campaign.nodes.front();
            const auto target = first.unconditional_targets.front();
            first.unconditional_targets.clear();
            first.conditional_targets.push_back({
                target, 0, gc::ConditionCombinator::all,
                {{gc::CampaignPredicateKind::objective_result,
                    core::stable_content_id_v1("defeat_all_opponents")}}
            });
        }
        for (auto& encounter : parsed.source.encounters) {
            if (encounter.id == core::stable_content_id_v1("muster_road/river_skirmish")) {
                encounter.placements.front().x = 1;
            }
        }
    }
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and loads");
    return loaded.package;
}

// The crossing, fought to the same end by the same commands whichever rule is
// in force. The outrider is left on the field.
client::BattleReport fight_the_crossing(const cr::CampaignEncounter& board) {
    client::BattleReport report;
    sim::Encounter::CreateResult created =
        sim::create_encounter(board.encounter.definition);
    expect(
        static_cast<bool>(created),
        "the crossing is a board a battle starts from"
    );
    if (!created) return report;
    const std::uint64_t rider = board.binding.battle_of(outrider).value;
    const std::uint64_t lead = board.binding.battle_of(vanguard).value;
    const std::uint64_t picket = core::stable_content_id_v1(
        "muster_road/river_skirmish/muster_picket"
    );
    const std::vector<sim::Command> script{
        {sim::CommandType::begin_battle, 0, {}, 0},
        {sim::CommandType::use_item, lead, {}, 0, 0, 0, field_tonic},
        {sim::CommandType::attack, picket, {}, rider},
        {sim::CommandType::use_item, rider, {}, 0, 0, 0, field_tonic},
        {sim::CommandType::attack, picket, {}, rider},
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

// What one run of the crossing left behind, on the terms the two rules differ
// on: who the aftermath named, what the campaign says about them afterwards,
// what they are still carrying, and what the battle hashed to.
struct Crossing final {
    std::uint64_t hash{};
    std::vector<campaign::PersistentEntityId> fallen;
    pr::CharacterLoss rule{pr::CharacterLoss::permanent};
    campaign::Availability outrider_after{};
    std::uint32_t outrider_holds{};
    std::uint32_t store_holds{};
    // Whether the next board the campaign moves to has the outrider standing on
    // it. This is the question a player actually asks, and it is decided by the
    // exclusion pass reading their availability rather than by anything this
    // setting added.
    bool outrider_takes_the_next_field{false};
};

Crossing cross_the_river(pr::CharacterLoss rule, bool show_statistics = false) {
    Crossing result;
    const pf::LoadedPackage package = compile_the_demo(rule, show_statistics);
    if (failures != 0) return result;

    storage::MemorySlotStorage device;
    const client::PackageBoards boards{package};
    client::CampaignSessionOptions options;
    options.slot = "losses";
    client::CampaignSession session{
        package, muster_road, boards, device, options
    };
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    expect(
        session.begin(failure, refused, resumed) ==
            client::CampaignSessionError::none,
        "the campaign founds"
    );

    const client::CampaignSession::PreparedBoard prepared = session.prepare_board();
    expect(
        prepared.error == client::CampaignSessionError::none,
        "the crossing is prepared"
    );
    if (prepared.error != client::CampaignSessionError::none) return result;
    expect(
        prepared.board.character_loss == rule,
        "and the board it published states the rule the project chose"
    );

    const client::BattleReport battle = fight_the_crossing(prepared.encounter);
    if (failures != 0) return result;
    result.hash = battle.canonical_hash;

    client::BattleAftermath aftermath;
    expect(
        session.commit_battle(battle, aftermath) ==
            client::CampaignSessionError::none,
        "and the battle commits"
    );
    expect(pr::show_end_statistics(package) == show_statistics,
           "the end statistics option survives compilation");
    if (show_statistics) {
        expect(session.save() == storage::StorageError::none, "the encounter checkpoint is saved");
        auto resume_options = options;
        resume_options.resume = true;
        client::CampaignSession next_session{package, muster_road, boards, device, resume_options};
        expect(next_session.begin(failure, refused, resumed) == client::CampaignSessionError::none &&
               !refused && resumed, "a fresh session resumes the saved route");
        const auto totals = next_session.statistics();
        expect(totals.battles == 1U && totals.allies_fallen == 1U && totals.enemies_encountered == 1U &&
               totals.enemies_defeated == 1U && totals.enemy_defeat_percentage() == 100U &&
               totals.lost_visit == 0U, "the first battle and its real casualty survive a reload");
    } else {
        expect(session.statistics().battles == 0U, "the disabled option writes no statistics");
    }
    result.fallen = aftermath.fallen;
    result.rule = aftermath.character_loss;

    const campaign::DefinitionRef tonic{
        package.game_id, core::ContentCategory::item, field_tonic
    };
    for (const client::RosterEntry& member : aftermath.roster) {
        if (member.member.value != outrider.value) continue;
        result.outrider_after = member.availability;
        for (const campaign::InventoryStack& stack : member.carried) {
            if (stack.item.stable_id == tonic.stable_id) {
                result.outrider_holds += stack.quantity;
            }
        }
    }
    for (const campaign::InventoryStack& stack : aftermath.store) {
        if (stack.item.stable_id == tonic.stable_id) {
            result.store_holds += stack.quantity;
        }
    }

    // And on to the next board, because that is where a rule about losing
    // people is actually felt. Nothing here reads `characterLoss`: the
    // exclusion pass asks whether a member is deployable, exactly as it always
    // has, and gets a different answer because a different thing was recorded.
    const client::CampaignSession::PreparedBoard next = session.prepare_board();
    if (next.error == client::CampaignSessionError::none) {
        result.outrider_takes_the_next_field =
            next.encounter.binding.battle_of(outrider).value != 0U;
    }
    return result;
}

// ---------------------------------------------------------------------------

void a_fall_under_the_permanent_rule_is_a_death() {
    const Crossing crossing = cross_the_river(pr::CharacterLoss::permanent);
    if (failures != 0) return;
    expect(
        crossing.fallen.size() == 1 &&
            crossing.fallen.front().value == outrider.value,
        "the aftermath names the outrider as the one who fell"
    );
    expect(
        crossing.rule == pr::CharacterLoss::permanent,
        "and says which rule decided what that meant"
    );
    expect(
        crossing.outrider_after == campaign::Availability::dead,
        "under the permanent rule they are dead afterwards"
    );
    expect(
        crossing.outrider_holds == 0U,
        "and carrying nothing, because a death empties the hands"
    );
    expect(
        !crossing.outrider_takes_the_next_field,
        "and no later map puts them back on a board"
    );
}

void a_fall_under_the_recoverable_rule_is_not() {
    const Crossing crossing = cross_the_river(pr::CharacterLoss::recoverable);
    if (failures != 0) return;
    expect(
        crossing.fallen.size() == 1 &&
            crossing.fallen.front().value == outrider.value,
        "the same battle puts the same person on the ground"
    );
    expect(
        crossing.rule == pr::CharacterLoss::recoverable,
        "and the aftermath says which rule is deciding"
    );
    expect(
        crossing.outrider_after == campaign::Availability::available,
        "under the recoverable rule they are available afterwards"
    );
    expect(
        crossing.outrider_takes_the_next_field,
        "and they take the next field, which is the whole of what the rule is "
        "for"
    );
    // Their satchel holds exactly what the battle left in it. Nothing revives
    // them and nothing empties them either, because nothing buried them: the
    // rule is spelled by omitting the operation that would have, so a fall
    // writes nothing about them into the campaign at all.
    //
    // On this particular script the outrider drank their one tonic before they
    // went down, so what the battle left them with is nothing. That is why
    // this is stated as an equality against what the permanent run put in the
    // store rather than as "they kept something". The distinguishing fact is
    // the availability above and the next board below it; the kit is checked
    // here so that a future change that started confiscating a survivor's
    // satchel would fail rather than pass quietly.
    expect(
        crossing.outrider_holds == 0U,
        "carrying exactly what the battle left them with"
    );
}

void the_rule_does_not_reach_the_battle() {
    const Crossing buried = cross_the_river(pr::CharacterLoss::permanent);
    const Crossing carried = cross_the_river(pr::CharacterLoss::recoverable);
    if (failures != 0) return;
    // The load-bearing assertion of the whole setting. The same commands over
    // the same board produce the same battle byte for byte, because nothing in
    // `engine/simulation` has ever heard of a campaign and this setting did not
    // teach it. Every canonical hash pinned anywhere in this repository is
    // therefore unmoved by a project choosing either rule.
    expect(
        buried.hash == carried.hash && buried.hash != 0U,
        "the identical battle hashes identically under both rules"
    );
    // And the two really are different campaigns afterwards, so the assertion
    // above is not passing because nothing happened.
    expect(
        buried.outrider_after != carried.outrider_after,
        "while the campaigns they leave behind are not the same campaign"
    );
    expect(
        buried.outrider_takes_the_next_field !=
            carried.outrider_takes_the_next_field,
        "and only one of them fields the outrider again"
    );
    // What the battle *dropped* lands in the store under both rules, because a
    // drop is the battle's consequence and not the campaign's judgement. Stated
    // as an equality on purpose: the recoverable rule removes an operation and
    // must not be seen quietly removing any other.
    expect(
        buried.store_holds == carried.store_holds,
        "and both collected exactly what the battle left on the field"
    );
}

void defeat_remains_terminal_with_statistics_disabled() {
    const auto package = compile_the_demo(pr::CharacterLoss::recoverable, false, true);
    storage::MemorySlotStorage device;
    const client::PackageBoards boards{package};
    client::CampaignSessionOptions options;
    options.slot = "unrouted-loss";
    client::CampaignSession session{package, muster_road, boards, device, options};
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    if (session.begin(failure, refused, resumed) != client::CampaignSessionError::none) {
        expect(false, "the victory-only campaign starts");
        return;
    }
    const auto prepared = session.prepare_board();
    if (prepared.error != client::CampaignSessionError::none) {
        expect(false, "the victory-only battle opens");
        return;
    }
    client::BattleReport battle;
    auto fight = sim::create_encounter(prepared.encounter.encounter.definition);
    if (!fight) {
        expect(false, "the loss fixture creates a playable battle");
        return;
    }
    const auto lead = prepared.encounter.binding.battle_of(vanguard).value;
    const auto rider = prepared.encounter.binding.battle_of(outrider).value;
    const auto picket = core::stable_content_id_v1("muster_road/river_skirmish/muster_picket");
    const std::vector<sim::Command> commands{
        {sim::CommandType::begin_battle},
        {sim::CommandType::wait, lead},
        {sim::CommandType::attack, picket, {}, rider},
        {sim::CommandType::wait, lead},
        {sim::CommandType::move, picket, {2, 1}},
        {sim::CommandType::attack, picket, {}, lead},
    };
    for (const auto& command : commands) {
        const auto applied = fight.encounter.apply(command);
        if (!applied) {
            expect(false, sim::error_name(applied.error));
            return;
        }
        battle.events.insert(battle.events.end(), applied.events.begin(), applied.events.end());
    }
    battle.final_snapshot = fight.encounter.snapshot();
    battle.outcome = battle.final_snapshot.outcome;
    battle.canonical_hash = fight.encounter.canonical_hash();
    battle.objectives = battle.final_snapshot.objectives;
    expect(battle.outcome == sim::Outcome::second_side_won,
           "the normal battle commands defeat the company");

    client::BattleAftermath aftermath;
    expect(session.commit_battle(battle, aftermath) == client::CampaignSessionError::none &&
        session.defeated() && session.standing().kind == pr::CampaignNodeKind::terminal &&
        session.statistics().battles == 0, "an unrouted loss ends play without collecting statistics");
    expect(session.save() == storage::StorageError::none, "the failed campaign saves its result");
    options.resume = true;
    client::CampaignSession reopened{package, muster_road, boards, device, options};
    expect(reopened.begin(failure, refused, resumed) == client::CampaignSessionError::none &&
        resumed && reopened.defeated() && reopened.standing().kind == pr::CampaignNodeKind::terminal,
        "reopening the save returns to the defeat ending");
}

}  // namespace

int main() {
    a_fall_under_the_permanent_rule_is_a_death();
    a_fall_under_the_recoverable_rule_is_not();
    the_rule_does_not_reach_the_battle();
    cross_the_river(pr::CharacterLoss::recoverable, true);
    defeat_remains_terminal_with_statistics_disabled();
    return failures == 0 ? 0 : 1;
}
