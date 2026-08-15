// SPDX-License-Identifier: MIT
// The screen a player sees before a campaign begins, on every console that
// keeps one.
//
// A save device reserves four slots and a flow that offers none of them spends
// exactly one. The rule this file exists to hold is that when one console gets
// a save menu the other gets the same screen rather than its own. The cheapest
// way to mean that is to write the screen once and render it twice, which is
// what `Camera` already does for a board, so this lives beside it.
//
// It knows nothing about storage. It is handed a base name and one boolean per
// slot, saying whether the device answers for it, and it produces rows, a
// caret, labels, a footer, and the one decision the session needs: found here,
// or resume here. A `SlotStorage`, a save, an envelope and a migration are all
// things the caller has and this file has never heard of.
//
// Fixed storage, no allocation, no `printf`, no global constructor: a console
// link may run no static initialiser at all, so this is a plain aggregate a
// caller places where it likes.

#pragma once

#include <cstddef>
#include <cstdint>

namespace grandleon::view {

// How many slots the screen shows. It is four because the byte-window directory
// reserves four; a device with fewer says so by passing a smaller count.
inline constexpr int slot_menu_rows = 4;

// A slot name is at most thirty-one characters by the storage contract, and a
// row's label and the footer are sized to the narrowest page this renders on,
// which is thirty-eight columns.
inline constexpr std::size_t slot_menu_name_size = 32;
inline constexpr std::size_t slot_menu_label_size = 39;

// Slot *row*'s name, without a screen.
//
// A console needs this before it has a screen: the rows say whether the device
// answers, so the device has to be asked four questions before the menu can be
// opened. It is the same arithmetic `SlotMenu` uses, called out so that a ROM
// asking it does not have to build a screen to ask.
//
// Slot one is the base unchanged; slot *n* is the base, a hyphen and *n*. The
// storage contract allows lower-case letters, digits, `_` and `-` up to
// thirty-one characters, so a base already inside it stays inside it with two
// more.
inline void slot_name_at(
    const char* base, int row, char* into, std::size_t size
) noexcept {
    if (into == nullptr || size == 0) return;
    std::size_t at = 0;
    // Three reserved: a hyphen, a digit and the terminator.
    const std::size_t room = size > 3U ? size - 3U : 0U;
    if (base != nullptr) {
        while (base[at] != '\0' && at < room) {
            into[at] = base[at];
            ++at;
        }
    }
    if (row > 0 && at + 2U < size) {
        into[at++] = '-';
        into[at++] = static_cast<char>('0' + (row + 1));
    }
    into[at] = '\0';
}

// What a row is holding, in the screen's own words.
//
// The vocabulary is deliberately small, and deliberately says nothing a slot
// cannot answer. The directory has no timestamps and reading four saves to
// count four rosters would mean loading and migrating four campaigns before one
// begins, on a machine whose heap is the scarcest thing it has. So a row says
// whether something is there, and the campaign says what it is.
enum class SlotState : std::uint8_t {
    // Nothing has ever been written here.
    empty = 0,
    // The device answers for this slot.
    holds,
    // The device answers for this slot and the player has asked to write over
    // it once. One more press of the same button founds; anything else puts it
    // back to `holds`, a caret move included.
    arming,
};

class SlotMenu final {
public:
    // What a press resolved to. `none` is the ordinary answer: a caret moved, a
    // row armed, an arming abandoned. The screen stays up until one of the
    // other two comes back.
    enum class Answer : std::uint8_t {
        none = 0,
        // Read this slot back and play what is in it.
        resume,
        // Found a company in this slot, whatever it was holding.
        found,
    };

    // Opens the screen over a device.
    //
    // `base` is slot one's name and the stem of every other: slot *n* is the
    // base, a hyphen and *n*, so a base of `tarnholt` gives `tarnholt`,
    // `tarnholt-2`, `tarnholt-3`, `tarnholt-4`. Slot one keeps the base
    // unchanged deliberately, so that a cartridge written before this screen
    // existed is found under the first row rather than orphaned by it.
    //
    // The caret opens on row zero for the same reason: it is the row every
    // scripted run has always taken, and it is the row a returning player wants
    // when they have only ever used one slot.
    // `other_button` is what this machine calls the button that starts over,
    // `Z` on the Nintendo 64, and it is always the button that console's unit
    // action menu already opens with.
    //
    // It is a parameter rather than a letter a renderer patches into the
    // finished sentence, and that is not fussiness: a renderer substituting its
    // own letter into a composed sentence matches every occurrence of the one
    // it replaces, and turns `A START A NEW COMPANY` into
    // `A START A NEW ZOMPANY` on a real screen. A screen written once has to be
    // written once all the way down.
    void open(
        const char* base, const bool* holds, int count, char other_button = 'C'
    ) noexcept {
        rows_ = count < 0 ? 0 : (count > slot_menu_rows ? slot_menu_rows : count);
        caret_ = 0;
        other_button_ = other_button;
        for (int row = 0; row < rows_; ++row) {
            slot_name_at(base, row, name_[row], slot_menu_name_size);
            state_[row] = (holds != nullptr && holds[row]) ? SlotState::holds
                                                          : SlotState::empty;
            compose_label(row);
        }
        compose_footer();
    }

    [[nodiscard]] int rows() const noexcept { return rows_; }
    [[nodiscard]] int caret() const noexcept { return caret_; }

    [[nodiscard]] const char* slot_name(int row) const noexcept {
        return in_range(row) ? name_[row] : "";
    }

    [[nodiscard]] bool holds(int row) const noexcept {
        return in_range(row) && state_[row] != SlotState::empty;
    }

    [[nodiscard]] SlotState state(int row) const noexcept {
        return in_range(row) ? state_[row] : SlotState::empty;
    }

    [[nodiscard]] bool armed() const noexcept {
        return in_range(caret_) && state_[caret_] == SlotState::arming;
    }

    // What a row says of itself: which slot it is and what is in it.
    [[nodiscard]] const char* row_label(int row) const noexcept {
        return in_range(row) ? label_[row] : "";
    }

    // What the buttons do, here, now. The second half of the screen: what a
    // button does on this screen depends on what is under the caret, so the
    // footer is a function of the caret rather than a constant.
    //
    // The start-over button appears here under whichever name `open` was given,
    // written into the sentence as it is composed. Every letter it is given
    // names the button that console's own action menu already opens with, which
    // is why this screen speaks the vocabulary the rest of the campaign speaks
    // rather than inventing a modifier.
    //
    // Thirty characters is the ceiling, and it is a television's rather than a
    // buffer's: the Nintendo 64 draws this from x=20 on a 320-pixel display at
    // eight pixels a character, and a set will not show the last of a line that
    // reaches the edge. A forty-column row arrives at the same bound from the
    // other direction.
    [[nodiscard]] const char* footer() const noexcept { return footer_; }

    // Moves the caret, and disarms whatever was armed.
    //
    // Disarming on a move is the property that makes it impossible to arm one
    // row and write over another: the arming belongs to the row it was made on
    // and does not survive leaving it.
    void move(int delta) noexcept {
        if (rows_ <= 0) return;
        disarm();
        int next = caret_ + delta;
        if (next < 0) next = 0;
        if (next >= rows_) next = rows_ - 1;
        caret_ = next;
        compose_footer();
    }

    // A. Continues a slot that answers, founds in one that does not, and puts an
    // armed row back the way it was. A is the button that means "the ordinary
    // thing", and destroying a campaign is not the ordinary thing.
    [[nodiscard]] Answer choose() noexcept {
        if (!in_range(caret_)) return Answer::none;
        switch (state_[caret_]) {
            case SlotState::arming:
                disarm();
                return Answer::none;
            case SlotState::holds:
                return Answer::resume;
            case SlotState::empty:
            default:
                return Answer::found;
        }
    }

    // The start-over button, Z on the Nintendo 64. Founds into an empty slot,
    // because there is nothing there to protect; arms a held one; and founds
    // over a held one on the second press.
    [[nodiscard]] Answer over() noexcept {
        if (!in_range(caret_)) return Answer::none;
        switch (state_[caret_]) {
            case SlotState::empty:
                return Answer::found;
            case SlotState::holds:
                state_[caret_] = SlotState::arming;
                compose_label(caret_);
                compose_footer();
                return Answer::none;
            case SlotState::arming:
            default:
                return Answer::found;
        }
    }

    // B, and every other press a renderer chooses to pass on. Puts an armed row
    // back; on a screen with nothing armed it does nothing, because there is
    // nowhere to back out to before a campaign begins.
    void cancel() noexcept { disarm(); }

private:
    [[nodiscard]] bool in_range(int row) const noexcept {
        return row >= 0 && row < rows_;
    }

    void disarm() noexcept {
        for (int row = 0; row < rows_; ++row) {
            if (state_[row] == SlotState::arming) {
                state_[row] = SlotState::holds;
                compose_label(row);
            }
        }
        compose_footer();
    }

    // `SLOT 1  A COMPANY STANDS`. The number is the row a player counts from
    // one, never the slot's stored name: a name is a thing the device needs
    // and a number is the thing on the screen.
    void compose_label(int row) noexcept {
        std::size_t at = 0;
        const auto put = [&](const char* text) noexcept {
            for (std::size_t i = 0; text[i] != '\0' &&
                                    at + 1U < slot_menu_label_size;
                 ++i) {
                label_[row][at++] = text[i];
            }
        };
        put("SLOT ");
        if (at + 1U < slot_menu_label_size) {
            label_[row][at++] = static_cast<char>('0' + (row + 1));
        }
        put("  ");
        switch (state_[row]) {
            case SlotState::arming:
                put("START OVER?");
                break;
            case SlotState::holds:
                put("A COMPANY STANDS");
                break;
            case SlotState::empty:
            default:
                put("-- EMPTY --");
                break;
        }
        label_[row][at] = '\0';
    }

    // What the buttons do, here, now. Composed rather than chosen from three
    // literals, because the start-over button's letter is this machine's and
    // the sentence around it is not.
    void compose_footer() noexcept {
        std::size_t at = 0;
        const auto put = [&](const char* text) noexcept {
            for (std::size_t i = 0;
                 text[i] != '\0' && at + 1U < slot_menu_label_size; ++i) {
                footer_[at++] = text[i];
            }
        };
        const auto button = [&]() noexcept {
            if (at + 1U < slot_menu_label_size) footer_[at++] = other_button_;
        };
        if (in_range(caret_)) {
            switch (state_[caret_]) {
                case SlotState::arming:
                    button();
                    put(" AGAIN TO CONFIRM   B KEEP IT");
                    break;
                case SlotState::holds:
                    put("A CONTINUE   ");
                    button();
                    put(" START OVER");
                    break;
                case SlotState::empty:
                default:
                    put("A START A NEW COMPANY");
                    break;
            }
        }
        footer_[at] = '\0';
    }

    char name_[slot_menu_rows][slot_menu_name_size]{};
    char label_[slot_menu_rows][slot_menu_label_size]{};
    char footer_[slot_menu_label_size]{};
    SlotState state_[slot_menu_rows]{};
    int rows_{0};
    int caret_{0};
    char other_button_{'C'};
};

}  // namespace grandleon::view
