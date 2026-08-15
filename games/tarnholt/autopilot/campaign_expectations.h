// SPDX-License-Identifier: MIT
#pragma once

// What the Tarnholt campaign is, at the two moments the Nintendo 64 campaign
// autopilot looks at it.
//
// These are the numbers the ROM asserts on the console. They are *not* read off
// a screen: `tests/nintendo64/campaign_expectations_test.cpp` links the real
// engine, compiles the same checked-in project the ROM embeds, drives the same
// `client::CampaignSession` through the same gestures the autopilot script
// presses, and asserts every constant below against what the session actually
// produced, through a real save and a real reload, on the host, before the
// ROM is built.
//
// So there is one place a campaign fact is written down, and it is a place a
// host compiler checks. If the campaign content moves, the host test fails in
// seconds and the console follows; nothing here can be quietly adjusted to make
// a console run go green, because adjusting it breaks the host test that
// derives it.
//
// The gestures the two runs perform, which are the script's and this header's
// shared subject:
//
//   Run one:   the cartridge is empty, so the campaign is founded. Three story
//              nodes are read through, the company is managed: the mage's Field
//              Tonic is taken into the store, and the second knight is benched.
//              Both gestures commit and both write the slot. The run leaves.
//   Run two:   a second emulator process over the same cartridge. The campaign
//              resumes standing exactly where it was left, and the company is
//              what run one made of it.

#include <cstdint>

namespace grandleon::tarnholt {

// The campaign the ROM plays, and the slot it keeps it in.
inline constexpr const char* campaign_key = "tarnholt_line";
inline constexpr const char* campaign_slot = "tarnholt";

// The name the cartridge announces itself by: the project's own `title`, which
// the title screen draws under the engine's mark. Written down here for the
// same reason the two strings above are. The ROM reads it off the project it
// was built from, and this is the independent statement the autopilot holds
// that reading to, so a ROM built for one game cannot quietly announce
// another's.
inline constexpr const char* game_title = "The Tarnholt Line";

// The company the author founds, in authored order. Founding assigns one-based
// persistent identities in exactly this order, which is what makes a save
// written by one build read by another.
inline constexpr int founding_roster_size = 4;

struct FoundingMember final {
    std::uint64_t member;
    const char* name;
    // How many items the campaign puts in their hands at founding, out of their
    // unit type's authored starting kit.
    std::uint32_t carried_stacks;
    std::uint32_t carried_items;
};

inline constexpr FoundingMember founding_roster[founding_roster_size] = {
    {1U, "Ser Halvard", 0U, 0U},
    {2U, "Ser Ondrey", 0U, 0U},
    {3U, "Wren Ashdown", 0U, 0U},
    // The only member the content arms: `dawn_mage` authors one Field Tonic.
    {4U, "Emrik Vayle", 1U, 1U},
};

// The two members the autopilot's gestures are about.
inline constexpr std::uint64_t mage_member = 4U;
inline constexpr std::uint64_t benched_member = 2U;

// Every member is level one and has earned nothing at founding.
inline constexpr std::uint16_t founding_level = 1U;
inline constexpr std::uint32_t founding_experience = 0U;

// The store at founding. The campaign authors a `startingStore` of two Field
// Tonics, the guard's own, in the company's hands rather than anybody's. The
// founding batch seeds it after the members, so the first screen a player sees
// already has something on it.
inline constexpr int founding_store_stacks = 1;
inline constexpr std::uint32_t founding_store_tonics = 2U;

// After the two gestures, which is what the second run must find. The mage's
// tonic goes into the stack that is already there rather than beside it: a
// store holds one stack per item identity.
inline constexpr int resumed_store_stacks = 1;
inline constexpr std::uint32_t resumed_store_tonics = 3U;
inline constexpr std::uint32_t resumed_mage_carried_stacks = 0U;
// `Availability::retired`: on the roster, and not deployable.
inline constexpr std::uint8_t resumed_benched_availability = 3U;
inline constexpr std::uint8_t resumed_available_availability = 2U;

// How many members the Fordlight has a placement for. The board the company is
// standing before, which is what the management stage offers a choice about.
inline constexpr int fordlight_placeable = 4;

// How many outcome batches the campaign has committed by the time the first
// run leaves. This is the "progress" half of the persistence claim: a resumed
// campaign that had forgotten a gesture would hold fewer. Derived, not counted
// by hand: the founding batch, one per story node completed, and one per
// management gesture.
inline constexpr int managed_committed_outcomes = 6;

// The portrait the opening scene's second line must draw.
//
// The scene casts its speakers, so the drawing is a fact about the *package*:
// the archetype and the faction colour it resolves for the unit type the cast
// names, asked with the same two accessors a board asks about a unit standing
// on it. Both are derived by the host test below from the compiled package,
// never written down from a screen.
//
// The second line rather than the first, and that is the whole point of the
// number. The first line's speaker is the Runner, whom the keyword convention
// and the cast happen to agree about (both land on the roster's first
// archetype), so a portrait drawn either way would look the same and prove
// nothing. The second line is Captain Mirea, whom the package says is a
// commander and whose display name spells no archetype at all: with the cast
// she is a commander, and without it she is the default. The console assertion
// requires the first and is required to be able to tell it from the second.
inline constexpr int opening_cast_line = 1;
inline constexpr std::uint8_t opening_cast_archetype = 5;
inline constexpr std::uint8_t opening_cast_colour = 0;

// What that line would have drawn with no cast at all: the roster's first
// archetype, in the first colour. Held here so the console can require the two
// to be distinguishable before it believes a match between them.
inline constexpr std::uint8_t opening_uncast_archetype = 0;
inline constexpr std::uint8_t opening_uncast_colour = 0;

}  // namespace grandleon::tarnholt
