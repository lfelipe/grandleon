// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/client/presenter.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace grandleon::client {

enum class SessionError : std::uint8_t {
    none = 0,
    package_rejected,
    campaign_rejected,
    encounter_rejected,
    flow_stalled,
};

[[nodiscard]] std::string_view error_name(SessionError error) noexcept;

// What one battle turned out to be.
//
// Everything in it is read off the finished encounter; nothing is interpreted.
// A caller that only wants to know who won reads `outcome`; a caller that has
// to turn the battle into campaign consequences needs `events` and the final
// snapshot, because a defeat event names who fell and who felled them and that
// pair is the whole of what experience is derived from.
struct BattleReport final {
    simulation::Outcome outcome{simulation::Outcome::ongoing};
    std::uint64_t canonical_hash{};
    simulation::EncounterSnapshot final_snapshot;
    std::vector<simulation::ObjectiveResult> objectives;
    // Every event the battle emitted, in the order it emitted them: the
    // player's commands and the opposing side's alike.
    std::vector<simulation::Event> events;
    // The player left before the battle ended. There is no outcome, and a
    // caller must not pretend there is one.
    bool quit{false};
};

// Play one already-loaded board to its end through a presenter.
//
// Split out of `run_campaign` so that a session which loads its boards
// differently plays them through exactly the same loop, with the same
// presenter vocabulary and the same opposing-side policy. A persistent
// campaign loads its boards through the roster, leaving the permanently dead
// off. A client that had two battle loops would be a client whose two campaign
// modes could disagree about what a battle is.
[[nodiscard]] SessionError play_battle(
    const package_runtime::EncounterLoadResult& board,
    simulation::Side player_side,
    Presenter& presenter,
    BattleReport& report
);

// Present a flow node's whole dialogue sequence, in authored order. A cutscene
// is a story node with several dialogues; the presenter sees one at a time.
// Shared for the same reason the battle loop is: two sessions that entered a
// node differently would otherwise say different amounts about it.
void present_dialogue_sequence(
    const package_format::LoadedPackage& package,
    const std::vector<std::uint64_t>& dialogue_ids,
    Presenter& presenter
);

// Plays a campaign to its terminal node through a presenter.
//
// Everything about rules, campaign flow, and the opposing side lives here, so a
// new client is a new Presenter and nothing else. The player commands one side;
// the other acts for itself through engine/tactics.
[[nodiscard]] SessionError run_campaign(
    const package_format::LoadedPackage& package,
    std::uint64_t campaign_id,
    simulation::Side player_side,
    Presenter& presenter
);

}  // namespace grandleon::client
