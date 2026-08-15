// SPDX-License-Identifier: MIT
#pragma once

// The kept campaign's autopilot: two scripts, one per emulator process, for the
// Nintendo 64 persistence check.
//
// The check boots the same ROM twice over the same cartridge. Which script
// plays is decided in `main` by what the cartridge is holding and by nothing
// the ROM carries. That is the property under test, so it must not be a flag:
// a cartridge that forgot would take the founding script a second time,
// report FOUNDED twice, and fail the harness on the word.
//
//   `campaign_autopilot_found`   the cartridge is empty. Title, controls, the
//                                slot screen's only row, the company as it was
//                                founded (carrying the store the campaign
//                                authors), the twelve authored cutscene lines
//                                of the three story nodes, and then the
//                                management stage: the mage's Field Tonic into
//                                the store, the second knight benched. Both
//                                gestures commit and both write the cartridge.
//                                Leave.
//
//   `campaign_autopilot_resume`  a second process over the same cartridge. The
//                                slot screen now offers CONTINUE and the caret
//                                is already on it. The campaign comes back
//                                standing on the Fordlight, which it never
//                                fought, so there are no cutscene lines to
//                                read: the company sheet, then the management
//                                stage, and everything asserted about both is
//                                what the first run put in the cartridge.
//
// Every number the ROM checks about the company is in
// `campaign_expectations.h`, derived on the host by
// `tests/nintendo64/campaign_expectations_test.cpp`, which drives this exact
// sequence of gestures through the same session before the ROM is built.
//
// The presses in the management stage, for a reader following along. The
// company screen's caret starts on the first member and the roster is in
// founding order: Halvard, Ondrey, Wren, Emrik. That is the order the
// campaign assigns identities in and the order `roster()` reports.
//
//   1. three DOWNs put the caret on Emrik Vayle, the only member the content
//      arms.
//   2. A opens his menu. It reads: SIT THIS ONE OUT, GIVE FIELD TONIC x2, TAKE
//      FIELD TONIC x1, CANCEL. The GIVE row is there from the first screen
//      because the campaign authors a starting store: the guard marches out
//      with two tonics of its own. So two DOWNs reach TAKE FIELD TONIC x1, and
//      A commits it.
//   3. two UPs put the caret on Ser Ondrey. A opens his menu, whose first row
//      is SIT THIS ONE OUT because he is deployable; A commits the bench.
//   4. B leaves. Nothing is lost: both gestures committed and wrote the
//      cartridge as they were made, which is what the ROM then reports.

#include "autopilot.h"

namespace grandleon::n64autopilot {

inline constexpr Step campaign_autopilot_found[] = {
    // The title screen holds until a button; give the logo a moment first so
    // the checkpoint photographs a settled frame.
    {Op::wait, 40, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::title), 0, 0, "title"},
    {Op::press, button_start, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::controls), 0, 0,
     "controls"},
    {Op::press, button_a, 0, 0, nullptr},

    // The slot screen. The cartridge is empty, so A NEW COMPANY is the only
    // row and the caret is on it.
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::campaign_slot), 0, 0,
     "campaign-slot-empty"},
    {Op::press, button_a, 0, 0, nullptr},

    // The company, as the campaign founded it. The narrator has already
    // checked every name, level and kit against the host derivation by the
    // time this frame is up.
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::campaign_roster), 0, 0,
     "campaign-founded"},
    {Op::press, button_a, 0, 0, nullptr},

    // Three story nodes: twelve authored lines, one press each.
    {Op::wait, 6, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::cutscene), 0, 0,
     "campaign-prologue"},
    {Op::press, button_a, 0, 0, nullptr},
    // The second line, and the one the portrait assertion needs: its speaker
    // is Captain Mirea, whom the scene casts and whose display name spells no
    // archetype at all. The first line's speaker is one the cast and the
    // keyword convention would agree about, so photographing that one would
    // prove nothing about which of the two drew it.
    {Op::wait, 4, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::cutscene_cast), 0, 0,
     "campaign-prologue-cast"},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 4, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},

    // The management stage, before the Fordlight.
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::campaign_management), 0,
     0, "campaign-manage"},

    // The caret to Emrik Vayle, and the tonic out of his hands.
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 3, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 3, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 3, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 6, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::campaign_management), 0,
     0, "campaign-member-menu"},
    // Past GIVE, onto TAKE. Two rows, because the store is not empty at
    // founding: the campaign authors one.
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 3, 0, 0, nullptr},
    {Op::press, button_d_down, 0, 0, nullptr},
    {Op::wait, 3, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},

    // The caret to Ser Ondrey, and off the board he goes.
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::campaign_management), 0,
     0, "campaign-tonic-taken"},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 3, 0, 0, nullptr},
    {Op::press, button_d_up, 0, 0, nullptr},
    {Op::wait, 3, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 6, 0, 0, nullptr},
    {Op::press, button_a, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::campaign_management), 0,
     0, "campaign-knight-benched"},

    // Leave. Everything was saved as it was done.
    {Op::press, button_b, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::finish, 0, 0, 0, nullptr},
};

inline constexpr Step campaign_autopilot_resume[] = {
    {Op::wait, 40, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::title), 0, 0, "title"},
    {Op::press, button_start, 0, 0, nullptr},
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::controls), 0, 0,
     "controls"},
    {Op::press, button_a, 0, 0, nullptr},

    // The slot screen now says the cartridge is holding one, and CONTINUE is
    // the row the caret starts on. A takes it.
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::campaign_slot), 0, 0,
     "campaign-slot-held"},
    {Op::press, button_a, 0, 0, nullptr},

    // The company that came back. The narrator checks every claim about it
    // against the host derivation before this frame is up: the store holding
    // the tonic, the mage holding nothing, the knight still benched.
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::campaign_roster), 0, 0,
     "campaign-resumed"},
    {Op::press, button_a, 0, 0, nullptr},

    // No cutscene lines: the campaign stands on the board it was left before,
    // and an encounter node authors none. Straight to the company.
    {Op::wait, 10, 0, 0, nullptr},
    {Op::checkpoint, static_cast<std::uint16_t>(Check::campaign_management), 0,
     0, "campaign-manage-resumed"},
    {Op::press, button_b, 0, 0, nullptr},
    {Op::wait, 20, 0, 0, nullptr},
    {Op::finish, 0, 0, 0, nullptr},
};

}  // namespace grandleon::n64autopilot
