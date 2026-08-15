// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>

#include <cstdint>
#include <vector>

// What a unit type says a character of it starts out carrying, decoded for a
// caller that has no board.
//
// The list itself is old: the compiler has written it into the unit type record
// since items existed, and `encounter_loader.cpp` has read it into a battle
// pack ever since. This header exists because a *campaign* needs the same bytes
// at a moment when there is no encounter at all: when a member joins the
// company and is handed what their type says they start with, once, by
// `campaign_runtime::starting_kit`.
//
// It is deliberately not in `progression.hpp`. That header's whole argument for
// existing is that "a board never reads these numbers", which is true of a
// growth block and plainly false of an item list: the board reads this one every
// time it loads. Two different reasons to decode one record deserve two headers
// rather than one whose stated purpose has to be widened until it says nothing.
//
// This is content and only content: data selection with no arithmetic and no
// rule. Who is given what, and when, is a campaign rule and lives in
// `engine/campaign_runtime` where a roster and a package are both in scope.

namespace grandleon::package_runtime {

struct UnitStartingItemsLoad final {
    // False when the unit type is not in the package, or its record does not
    // decode as far as the item list. An empty list on a record that decodes is
    // a success: a unit type whose author listed nothing starts with nothing,
    // which is what most of them say.
    bool found{false};
    // The identities the unit type lists, in the order it lists them. The order
    // is load-bearing rather than incidental: it is the order a satchel is
    // built in, and `UnitDefinition::item_ids` is hashed in order.
    std::vector<std::uint64_t> items;

    [[nodiscard]] explicit operator bool() const noexcept { return found; }
};

// Decode one unit type's starting item list.
//
// The record's layout is written by `tools/game_content/src/compiler.cpp`: a
// name, a class, a faction, the starting weapons, the starting items, the
// abilities, and then the tails that growth and drops appended. Nothing past
// the item list is read here, so this decoder never has to discriminate on a
// tail length.
[[nodiscard]] UnitStartingItemsLoad load_unit_starting_items(
    const package_format::LoadedPackage& package,
    std::uint64_t unit_type_id
);

}  // namespace grandleon::package_runtime
