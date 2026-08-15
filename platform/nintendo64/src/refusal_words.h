// SPDX-License-Identifier: MIT
#pragma once

// The engine's refusal vocabulary, in the words a player acts on.
//
// The enum stays authoritative; this is presentation only. It is a header
// rather than a block inside the ROM source because
// `platform/client/include/grandleon/client/turn_client.hpp` says of the
// shared client's copy that it "is the Nintendo 64's table because a player
// who has seen one console's refusal should recognise the other's". That is a
// claim about two tables, and a claim about two tables is only checkable if
// both can be compiled by the same host. This one otherwise sits inside a
// translation unit only a cross compiler can open.
//
// `tests/nintendo64/console_words_test.cpp` compiles this one and links the
// other, and reads every error the engine names out of `sim::error_name`, so a
// refusal added to the engine is covered the day it is added.

#include <grandleon/simulation/encounter.hpp>

namespace grandleon::n64words {

[[nodiscard]] inline const char* refusal_text(
    grandleon::simulation::CommandError error
) noexcept {
    namespace sim = grandleon::simulation;
    switch (error) {
        case sim::CommandError::none: return "";
        case sim::CommandError::encounter_complete:
            return "THE BATTLE IS OVER";
        case sim::CommandError::unknown_unit: return "NO SUCH UNIT";
        case sim::CommandError::defeated_unit: return "THAT UNIT IS DOWN";
        case sim::CommandError::wrong_side: return "NOT YOUR UNIT";
        case sim::CommandError::invalid_command: return "CANNOT DO THAT";
        case sim::CommandError::invalid_destination:
            return "CANNOT MOVE THERE";
        case sim::CommandError::occupied_destination:
            return "THAT TILE IS TAKEN";
        case sim::CommandError::unknown_target: return "NO TARGET THERE";
        case sim::CommandError::target_defeated:
            return "TARGET ALREADY DOWN";
        case sim::CommandError::friendly_target: return "THAT IS AN ALLY";
        // One word for both ends of the band, because one refusal is what the
        // engine gives. Telling too close from too far apart would mean
        // measuring the distance and comparing it against the actor's reach,
        // which is the engine's range rule restated on a console where nothing
        // could notice it drifting.
        case sim::CommandError::target_out_of_range: return "OUT OF RANGE";
        case sim::CommandError::unknown_ability:
        case sim::CommandError::unavailable_ability:
            return "CANNOT USE THAT";
        case sim::CommandError::activation_in_progress:
            return "ANOTHER UNIT IS ACTING";
        case sim::CommandError::no_action_points: return "NO ACTIONS LEFT";
        case sim::CommandError::unknown_weapon:
        case sim::CommandError::unavailable_weapon:
            return "NO SUCH WEAPON";
        case sim::CommandError::unknown_item:
        case sim::CommandError::unavailable_item:
            return "NOT CARRYING THAT";
        case sim::CommandError::depleted_item: return "NONE LEFT";
        case sim::CommandError::unusable_item: return "NOTHING HAPPENS";
        case sim::CommandError::wrong_phase: return "NOT YET";
        case sim::CommandError::undeployable_unit: return "THEY STAND THERE";
        case sim::CommandError::outside_zone: return "NOT YOUR GROUND";
        case sim::CommandError::not_talkable: return "THEY HAVE NOTHING TO SAY";
        // Not "THAT UNIT IS DOWN". Somebody who walked away did not die, and
        // this is the one line on screen where a player learns the difference.
        case sim::CommandError::target_departed: return "THEY HAVE GONE";
        // Not here yet is not gone, and not down either. See the refusals
        // themselves for why the two are kept apart all the way to the screen.
        case sim::CommandError::target_unarrived:
            return "THEY ARE NOT HERE YET";
        case sim::CommandError::unarrived_unit: return "THEY HAVE NOT ARRIVED";
        // The actor-side half of "THEY HAVE GONE": the player picked somebody
        // who has left the board rather than aimed at one.
        case sim::CommandError::departed_unit: return "THEY HAVE LEFT";
        // And already spent is none of those. Under a side block the player
        // picks their own order, so the one thing they have to be told is which
        // of their line has already gone.
        case sim::CommandError::already_acted: return "THEY HAVE ALREADY ACTED";
        case sim::CommandError::already_moved: return "THEY HAVE ALREADY MOVED";
    }
    return "CANNOT DO THAT";
}

}  // namespace grandleon::n64words
