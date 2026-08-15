// SPDX-License-Identifier: MIT
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <grandleon/client/turn_client.hpp>
#include <grandleon/simulation/encounter.hpp>

#include "refusal_words.h"

// The two consoles' refusal vocabularies, side by side.
//
// `platform/client/include/grandleon/client/turn_client.hpp` says of the
// shared client's table that it "is the Nintendo 64's table because a player
// who has seen one console's refusal should recognise the other's". Two tables
// and a comment is not a shared table; it is a copy with a promise attached,
// and the promise is only checkable if a host can compile both copies. The
// console's is a header for exactly that reason
// (`platform/nintendo64/src/refusal_words.h`), and this is the check.
//
// The errors are read out of `sim::error_name` rather than listed here, so a
// refusal added to the engine is covered on the day it is added rather than on
// the day somebody remembers this file.
//
// Nothing disagrees. The ledger below is empty, and an empty ledger is the
// strongest form this check takes: every error the engine names is said in the
// same words on both consoles, and the day one of them changes, this test says
// which one and what it now says.
//
// The list is a ledger and not an allowance. A row that stops disagreeing
// fails just as loudly as a new row that starts, so a disagreement can only
// ever be *recorded* deliberately. The record is a thing somebody has to
// justify in writing, next to the two words, rather than a silence.

namespace sim = grandleon::simulation;
namespace turn = grandleon::client::turn;
namespace n64 = grandleon::n64words;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// A row the two tables say differently, and the reading of it.
struct Drift final {
    sim::CommandError error;
    const char* console;
    const char* shared;
    const char* whose_word_is_better;
};

// Empty, and meant to stay that way. The four rows this once held were all
// resolved the same way. The Nintendo 64's word was the better one and the
// shared client took it:
//
//   invalid_destination   the tile is off the board, so "CANNOT REACH THERE"
//                         read as a claim about how far the walk goes, which
//                         is the one thing it is not
//   target_defeated       "THAT UNIT IS DOWN" was also `defeated_unit`'s word,
//                         folding the actor's half of the fact into the
//                         target's, and the engine keeps the two apart
//   unknown_ability       "NO SUCH ABILITY" is false of `unavailable_ability`:
//   unavailable_ability   the ability exists and this character has not got it
const std::vector<Drift> ledger = {};

const Drift* recorded(sim::CommandError error) noexcept {
    for (const Drift& row : ledger) {
        if (row.error == error) return &row;
    }
    return nullptr;
}

// The banner the Nintendo 64 draws a refusal in is centred and sized to the
// sentence: `strlen(text) * 8 + 16`, framed by two more pixels a side. A word
// wider than the display draws its frame off the left edge.
constexpr int banner_limit = 320;

int banner_width(const char* text) noexcept {
    return static_cast<int>(std::strlen(text)) * 8 + 16 + 4;
}

}  // namespace

int main() {
    int compared = 0;
    int drifted = 0;

    // Every error the engine names, walked from zero until the enum runs out.
    // `error_name` answers "unknown" past the end, which is what says where
    // the end is without a list here that could fall behind one.
    for (int value = 0;; ++value) {
        const auto error = static_cast<sim::CommandError>(value);
        const std::string_view named = sim::error_name(error);
        if (named == "unknown") break;
        ++compared;

        const char* console = n64::refusal_text(error);
        const char* shared = turn::refusal_text(error);
        const std::string what(named);

        expect(
            console != nullptr && shared != nullptr,
            what + " has a word on both consoles"
        );
        if (console == nullptr || shared == nullptr) continue;

        // `none` is the absence of a refusal and is drawn by nobody.
        if (error != sim::CommandError::none) {
            expect(
                console[0] != '\0',
                what + " is said in words on the Nintendo 64"
            );
            expect(
                shared[0] != '\0',
                what + " is said in words on the shared client"
            );
        }

        expect(
            banner_width(console) <= banner_limit,
            what + " fits the Nintendo 64's refusal banner"
        );

        const Drift* row = recorded(error);
        if (std::strcmp(console, shared) == 0) {
            expect(
                row == nullptr,
                what + " is recorded as a disagreement and the two tables "
                       "now agree on it — take the row out of the ledger"
            );
            continue;
        }
        ++drifted;
        expect(
            row != nullptr,
            what + " is said differently by the two consoles and is not in "
                   "the ledger: the Nintendo 64 says \"" +
                console + "\", the shared client says \"" + shared + "\""
        );
        if (row == nullptr) continue;
        expect(
            std::strcmp(row->console, console) == 0 &&
                std::strcmp(row->shared, shared) == 0,
            what + " drifted again: the ledger records \"" +
                std::string(row->console) + "\" against \"" + row->shared +
                "\" and the tables now say \"" + console + "\" against \"" +
                shared + "\""
        );
    }

    expect(
        compared > 20,
        "the walk over the engine's refusals reached the whole vocabulary"
    );
    expect(
        drifted == static_cast<int>(ledger.size()),
        "every recorded disagreement was found, and no more"
    );

    std::cerr << "  derived: " << compared
              << " refusals compared, " << drifted
              << " said differently by the two consoles\n";
    for (const Drift& row : ledger) {
        std::cerr << "    " << sim::error_name(row.error) << ": console \""
                  << row.console << "\" vs shared \"" << row.shared
                  << "\" — " << row.whose_word_is_better
                  << " has the better word\n";
    }

    if (failures == 0) {
        std::cerr << "the two consoles' refusal vocabularies are as recorded\n";
    }
    return failures == 0 ? 0 : 1;
}
