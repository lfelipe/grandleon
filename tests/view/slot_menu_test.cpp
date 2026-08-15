// SPDX-License-Identifier: MIT
// Host-side test for the shared slot screen.
//
// The screen is a model rather than a drawing precisely so that this suite can
// pin what it says and what its buttons do, and so that "both consoles show the
// same screen" is a fact about one file rather than a promise about two.
//
// It also links `grandleon::storage` for one assertion and one only: every name
// this screen invents has to be a name every platform could carry, and the
// authority on that is `is_valid_slot_name` rather than a rule restated here.

#include "grandleon/view/slot_menu.hpp"

#include <grandleon/storage/slot_storage.hpp>

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    using grandleon::view::SlotMenu;
    using grandleon::view::SlotState;
    using Answer = SlotMenu::Answer;

    // A device holding nothing. Every row is empty, every row founds, and no
    // row anywhere offers to continue over nothing. That is the rule "a
    // CONTINUE that does nothing is worse than no CONTINUE at all", held one
    // row at a time rather than one device at a time.
    {
        const bool holds[4] = {false, false, false, false};
        SlotMenu menu;
        menu.open("tarnholt", holds, 4);
        expect(menu.rows() == 4, "a four-slot device shows four rows");
        expect(menu.caret() == 0, "the caret opens on the first slot");
        for (int row = 0; row < 4; ++row) {
            expect(!menu.holds(row), "no row holds anything");
            expect(menu.state(row) == SlotState::empty, "and every row is empty");
        }
        expect(std::string_view(menu.row_label(0)) == "SLOT 1  -- EMPTY --",
               "an empty row says so");
        expect(std::string_view(menu.footer()) == "A START A NEW COMPANY",
               "and the footer offers only to found");
        expect(menu.choose() == Answer::found, "A founds in an empty slot");
        expect(menu.over() == Answer::found,
               "and so does the start-over button, with nothing to protect");
    }

    // The names. Slot one keeps the base unchanged, which is the whole of how a
    // cartridge written before this screen existed is still found by it.
    {
        const bool holds[4] = {false, false, false, false};
        SlotMenu menu;
        menu.open("tarnholt", holds, 4);
        expect(std::string_view(menu.slot_name(0)) == "tarnholt",
               "slot one keeps the name the flow has always written");
        expect(std::string_view(menu.slot_name(1)) == "tarnholt-2",
               "slot two is the base and its number");
        expect(std::string_view(menu.slot_name(2)) == "tarnholt-3", "and three");
        expect(std::string_view(menu.slot_name(3)) == "tarnholt-4", "and four");
        for (int row = 0; row < 4; ++row) {
            expect(grandleon::storage::is_valid_slot_name(menu.slot_name(row)),
                   "every name this screen invents is one a device would take");
        }
        // Names are distinct, which is what makes four independent campaigns
        // four campaigns rather than one written four times.
        for (int a = 0; a < 4; ++a) {
            for (int b = a + 1; b < 4; ++b) {
                expect(std::string_view(menu.slot_name(a)) !=
                           std::string_view(menu.slot_name(b)),
                       "no two rows name the same slot");
            }
        }
    }

    // A base long enough to run into the storage contract's own limit is cut to
    // fit rather than overrunning, and what comes out is still a legal name.
    {
        const bool holds[4] = {false, false, false, false};
        SlotMenu menu;
        menu.open("abcdefghijklmnopqrstuvwxyz0123456789", holds, 4);
        for (int row = 0; row < 4; ++row) {
            expect(grandleon::storage::is_valid_slot_name(menu.slot_name(row)),
                   "an over-long base is cut to a name a device would take");
        }
        expect(std::string_view(menu.slot_name(0)) !=
                   std::string_view(menu.slot_name(1)),
               "and the rows are still distinct after the cut");
    }

    // One slot answers and the others do not: CONTINUE is offered exactly
    // there.
    {
        const bool holds[4] = {true, false, false, false};
        SlotMenu menu;
        menu.open("tarnholt", holds, 4);
        expect(menu.holds(0), "the slot the device answers for holds");
        expect(std::string_view(menu.row_label(0)) == "SLOT 1  A COMPANY STANDS",
               "a held row says a company stands in it");
        expect(std::string_view(menu.footer()) == "A CONTINUE   C START OVER",
               "and the footer offers both verbs");
        expect(menu.choose() == Answer::resume, "A continues it");

        menu.move(1);
        expect(menu.caret() == 1, "the caret moves down");
        expect(!menu.holds(1), "onto a slot that answers for nothing");
        expect(std::string_view(menu.footer()) == "A START A NEW COMPANY",
               "where the footer offers only to found");
        expect(menu.choose() == Answer::found, "and A founds there");
    }

    // The caret stays inside the screen.
    {
        const bool holds[4] = {false, false, false, false};
        SlotMenu menu;
        menu.open("tarnholt", holds, 4);
        menu.move(-1);
        expect(menu.caret() == 0, "the caret does not run off the top");
        for (int step = 0; step < 10; ++step) menu.move(1);
        expect(menu.caret() == 3, "nor off the bottom");
    }

    // Writing over a held slot is armed and confirmed. A held campaign does not
    // die on one press.
    {
        const bool holds[4] = {true, true, false, false};
        SlotMenu menu;
        menu.open("tarnholt", holds, 4);
        expect(menu.over() == Answer::none,
               "the start-over button does not found on its first press");
        expect(menu.armed(), "it arms the row");
        expect(std::string_view(menu.row_label(0)) == "SLOT 1  START OVER?",
               "and the row says it is asking");
        expect(std::string_view(menu.footer()) ==
                   "C AGAIN TO CONFIRM   B KEEP IT",
               "and the footer says what confirms it and what escapes it");
        expect(menu.over() == Answer::found, "a second press founds over it");
    }

    // A. On an armed row it is the way out rather than the way through: the
    // button that means "the ordinary thing" does not destroy a campaign.
    {
        const bool holds[4] = {true, false, false, false};
        SlotMenu menu;
        menu.open("tarnholt", holds, 4);
        expect(menu.over() == Answer::none, "armed");
        expect(menu.choose() == Answer::none, "A on an armed row answers nothing");
        expect(!menu.armed(), "and disarms it");
        expect(std::string_view(menu.row_label(0)) == "SLOT 1  A COMPANY STANDS",
               "the row goes back to what it was");
        expect(menu.choose() == Answer::resume, "and A continues it again");
    }

    // B disarms.
    {
        const bool holds[4] = {true, false, false, false};
        SlotMenu menu;
        menu.open("tarnholt", holds, 4);
        expect(menu.over() == Answer::none, "armed");
        menu.cancel();
        expect(!menu.armed(), "B puts an armed row back");
        expect(menu.over() == Answer::none,
               "and the next start-over press arms rather than founds");
    }

    // Moving the caret disarms: the property that makes it impossible to arm
    // one row and write over another.
    {
        const bool holds[4] = {true, true, true, true};
        SlotMenu menu;
        menu.open("tarnholt", holds, 4);
        expect(menu.over() == Answer::none, "slot one armed");
        menu.move(1);
        expect(!menu.armed(), "moving away disarms");
        expect(menu.state(0) == SlotState::holds,
               "and the row left behind is a held row again");
        expect(menu.over() == Answer::none,
               "the new row has to be armed on its own");
        expect(menu.armed(), "which it now is");
        menu.move(-1);
        expect(!menu.armed(), "and moving back disarms that one too");
        expect(menu.choose() == Answer::resume,
               "so A on slot one continues rather than founding over it");
    }

    // A device with fewer slots than the screen has rows.
    {
        const bool holds[2] = {true, false};
        SlotMenu menu;
        menu.open("save", holds, 2);
        expect(menu.rows() == 2, "a two-slot device shows two rows");
        for (int step = 0; step < 5; ++step) menu.move(1);
        expect(menu.caret() == 1, "and the caret stays inside them");
        expect(std::string_view(menu.slot_name(1)) == "save-2",
               "with the second row named off the same base");
    }

    // A device with no slots at all is a screen with nothing on it, and every
    // press answers nothing rather than reading past the end.
    {
        SlotMenu menu;
        menu.open("save", nullptr, 0);
        expect(menu.rows() == 0, "no rows");
        expect(std::string_view(menu.row_label(0)).empty(), "and no label");
        expect(std::string_view(menu.footer()).empty(), "and no footer");
        expect(menu.choose() == Answer::none, "A answers nothing");
        expect(menu.over() == Answer::none, "and so does start over");
        menu.move(1);
        menu.cancel();
        expect(menu.caret() == 0, "and the caret has nowhere to go");
    }

    // The start-over button's letter is this machine's, and the sentence around
    // it is not. This exists because the first attempt let the renderer patch
    // the letter into a finished sentence, matching a `C` at a word boundary,
    // which turned `A START A NEW COMPANY` into `A START A NEW ZOMPANY` on a
    // Nintendo 64, and no assertion anywhere looked at a footer's text.
    {
        const bool holds[4] = {true, false, false, false};
        SlotMenu third_button;
        third_button.open("tarnholt", holds, 4, 'C');
        SlotMenu sixty_four;
        sixty_four.open("tarnholt", holds, 4, 'Z');

        expect(std::string_view(third_button.footer()) ==
                   "A CONTINUE   C START OVER",
               "a machine whose third face button is C names C");
        expect(
            std::string_view(sixty_four.footer()) == "A CONTINUE   Z START OVER",
            "and the Nintendo 64's names Z"
        );

        expect(third_button.over() == Answer::none, "armed on one");
        expect(sixty_four.over() == Answer::none, "and on the other");
        expect(std::string_view(third_button.footer()) ==
                   "C AGAIN TO CONFIRM   B KEEP IT",
               "the armed footer names C");
        expect(std::string_view(sixty_four.footer()) ==
                   "Z AGAIN TO CONFIRM   B KEEP IT",
               "and Z");

        third_button.move(1);
        sixty_four.move(1);
        // The only sentence with no button letter in it but a `C` in a word.
        // Both say the same thing, and neither says ZOMPANY.
        expect(std::string_view(third_button.footer()) ==
                   "A START A NEW COMPANY",
               "an empty row's footer is the same whatever the letter is");
        expect(std::string_view(sixty_four.footer()) == "A START A NEW COMPANY",
               "and the machine's own letter did not eat a word");
    }

    // Every footer fits a television. The Nintendo 64 draws this line from
    // x=20 on a 320-pixel display at eight pixels a character, so a line past
    // thirty characters reaches an edge a set will not show. A wording change
    // that overflowed would otherwise be invisible until somebody looked at a
    // real screen.
    {
        const bool holds[4] = {true, false, false, false};
        SlotMenu menu;
        menu.open("tarnholt", holds, 4);
        for (int row = 0; row < menu.rows(); ++row) {
            expect(std::string_view(menu.row_label(row)).size() <= 30,
                   "a row fits inside a television's safe area");
        }
        expect(std::string_view(menu.footer()).size() <= 30,
               "and so does a held row's footer");
        expect(menu.over() == Answer::none, "armed");
        expect(std::string_view(menu.footer()).size() <= 30,
               "and an armed row's, which is the longest of the three");
        expect(std::string_view(menu.row_label(0)).size() <= 30,
               "and an armed row's label");
        menu.move(1);
        expect(std::string_view(menu.footer()).size() <= 30,
               "and an empty row's");
    }

    // A count larger than the screen holds is taken as the screen's own.
    {
        const bool holds[8] = {true, true, true, true, true, true, true, true};
        SlotMenu menu;
        menu.open("save", holds, 8);
        expect(menu.rows() == grandleon::view::slot_menu_rows,
               "a device claiming more slots than the screen has is capped");
    }

    // Opening again is a clean screen: no arming survives a reopen, and the
    // rows follow what the device says now rather than what it said before.
    {
        const bool first[4] = {true, true, true, true};
        SlotMenu menu;
        menu.open("tarnholt", first, 4);
        expect(menu.over() == Answer::none, "armed");
        menu.move(2);
        const bool second[4] = {false, false, false, false};
        menu.open("tarnholt", second, 4);
        expect(menu.caret() == 0, "reopening puts the caret back on slot one");
        expect(!menu.armed(), "with nothing armed");
        for (int row = 0; row < 4; ++row) {
            expect(!menu.holds(row), "and the rows follow the device");
        }
    }

    if (failures == 0) {
        std::cout << "RESULT slot_menu PASS\n";
        return 0;
    }
    std::cout << "RESULT slot_menu FAIL " << failures << '\n';
    return 1;
}
