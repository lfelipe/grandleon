// SPDX-License-Identifier: MIT
#pragma once

// The full information sheet: every number a character actually has, in the
// one shape every client draws it in.
//
// Left to themselves, clients say different amounts about a character: a
// six-row panel that follows the cursor on one, a status line on another, a
// sentence in the browser. None of those is the sheet the genre means by
// "info": the numbers a player consults *before* committing, deliberately,
// rather than glimpses at while steering.
//
// So the sheet is built here, once, out of the snapshot and the encounter's own
// weapon, ability and item registries, and handed to the renderers as lines of
// text.
// A console draws them into a framebuffer, the terminal prints them, and none
// of them decides what a sheet says. That is the whole point: a stat a client
// forgot to draw would be a client that disagrees with the rules about what a
// character is.
//
// Nothing here computes. Every number written into a line is read from the
// snapshot the engine produced or from the registry the encounter was created
// with, the weapon accuracies included, which are the authored numbers a hit
// roll starts from. The chance a *particular* strike lands folds the striker's
// skill and luck against the target's evasion and luck, needs a target to fold
// against, and belongs to the forecast; a sheet has no target and states none.
//
// Allocation-free by construction. `UnitSheet` is a fixed block of characters,
// because a client that allocated to draw a panel would perturb the heap census
// the ROM takes beside it, and because every console has to agree with the host
// byte for byte about what a line says.

#include <grandleon/package_format/package.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace grandleon::sheet {

namespace sim = grandleon::simulation;

// ---------------------------------------------------------------------------
// Display names
//
// The engine knows stable content identities and nothing else; a name is the
// client's business. There is one table and every client reads it: three
// identical tables in three clients are exactly how a menu and a sheet come to
// call the same weapon two things.
//
// The table is not the whole answer, though. It holds the *shipped* projects'
// identities, so on its own it leaves a cartridge built from anybody else's
// project calling every character `UNIT`, every weapon `WEAPON` and every spell
// `ABILITY`. Playing such a cartridge shows a fallen bandit produce
// `UNIT DIED`. The package carries every authored name, so the package is asked
// first.
//
// A name is therefore asked for in three places in order, and each is a
// different kind of knowledge: the package, which is what *this* author wrote;
// the table, which is what this build happens to know about the samples; and
// the category, which is all anybody knows.
// ---------------------------------------------------------------------------

// Short display name for a unit type, or "UNIT" for content this build has
// never met.
[[nodiscard]] const char* unit_type_name(std::uint64_t unit_type_id) noexcept;

// Short display name for a weapon, or "WEAPON".
[[nodiscard]] const char* weapon_name(std::uint64_t weapon_id) noexcept;

// Short display name for an ability, or "ABILITY".
[[nodiscard]] const char* ability_name(std::uint64_t ability_id) noexcept;

// Short display name for an item, or "ITEM".
[[nodiscard]] const char* item_name(std::uint64_t item_id) noexcept;

// The most characters of an authored name a client shows. Twenty-four leaves
// room beside it on a forty-column line for the numbers every surface that
// draws a name also draws; a longer name is cut rather than allowed to push
// them off.
inline constexpr std::size_t content_name_capacity = 24;

// One resolved name, in storage the caller owns.
//
// Returned by value rather than as a pointer into something, because
// `package_runtime::content_name` hands back a **borrowed, counted, and not
// NUL-terminated** view of the package's own bytes, and every surface that
// draws a name wants a `const char*`. Somebody has to own the copy; making it
// the caller's stack means no allocation on a machine with no heap, and no
// buffer whose lifetime a reader has to reason about.
//
// It is a temporary in most call sites, which is fine within one full
// expression: `line.text(unit_type_name(p, id).c_str())` works, because the
// temporary outlives it. Storing the `const char*` past the statement does
// not; hold the `ContentName` instead.
struct ContentName final {
    char text[content_name_capacity + 1]{};

    [[nodiscard]] const char* c_str() const noexcept { return text; }
};

// What the author called this unit type, out of the package the platform is
// holding, falling back to the table above and then to `UNIT`.
//
// `package` may be null, for a fixture, a test, or a client with nothing
// loaded, and then the answer is the table's alone.
//
// The name is folded to upper-case ASCII, because the font every console here
// shares (`grandleon::view::glyphs`) runs 0x20 to 0x5F and has no lower case in
// it at all: a name drawn verbatim would be drawn as blanks. Bytes outside
// ASCII become a dash, the way the Nintendo 64 already folds a title.
[[nodiscard]] ContentName unit_type_name(
    const package_format::LoadedPackage* package,
    std::uint64_t unit_type_id
) noexcept;

// The same, for the three other sections a sheet names.
[[nodiscard]] ContentName weapon_name(
    const package_format::LoadedPackage* package, std::uint64_t weapon_id
) noexcept;

[[nodiscard]] ContentName ability_name(
    const package_format::LoadedPackage* package, std::uint64_t ability_id
) noexcept;

[[nodiscard]] ContentName item_name(
    const package_format::LoadedPackage* package, std::uint64_t item_id
) noexcept;

// ---------------------------------------------------------------------------
// Who a character is
//
// Every character on every board has a name, and this is the one function that
// says what it is. Not a courtesy: a client that cannot name a character cannot
// write a sentence about one, and the alternative every surface reached for
// instead was the roster digit, a character's position in snapshot order,
// which means nothing to a player and changes meaning between boards.
//
// One function rather than one per client, because a name is exactly the kind
// of thing that drifts. Three clients each resolving "who is this" out of the
// same three sources is three chances to order them differently, and a console
// that called somebody `BANDIT 2` where the terminal called them `BANDIT 1`
// would be two machines disagreeing about who just died.
// ---------------------------------------------------------------------------

// What a character standing on this board is called.
//
// Three sources, asked in this order, and each is a different kind of
// knowledge:
//
//  1. **`member_name`**, what the campaign calls them. Only the client holds
//     the join from a board unit to a roster member, so only the client can
//     answer this, and it passes the answer in. Null or empty when no member
//     stands in this unit, which is every unit on a board played outside a
//     campaign.
//  2. **The authored placement name**, out of the package's
//     `placement_names` section, keyed by this unit's own identity, which is
//     the placement's identity, unique across the project. This is what lets an
//     author name a boss, a talkable captain, or anybody at all on the second
//     side, where no campaign roster reaches.
//  3. **Derived from the unit type**, which is what makes "always" true. The
//     author's own word for the kind of character, plus an ordinal *only when
//     more than one of that kind is on this board*: `BANDIT 1`, `BANDIT 2`,
//     while a unique `BANDIT CHIEF` stays plain.
//
// The ordinal is taken from ascending unit identity among the units of that
// type, and counted over every unit the snapshot carries rather than over the
// ones currently standing. Both halves of that matter. Ascending identity is
// the same order on every machine, so two clients agree by construction rather
// than by both happening to walk the snapshot the same way. Counting the whole
// snapshot keeps the name still: a roster filtered to who is upright would
// promote `BANDIT 2` to `BANDIT 1` the moment the first one fell, and rename a
// character in the middle of the sentence reporting their death.
//
// Folded to upper-case ASCII on the terms `unit_type_name` describes, and for
// the same font. `member_name` is folded too: one answer means one fold, and a
// campaign name drawn verbatim through `grandleon::view::glyphs` is blanks.
[[nodiscard]] ContentName character_name(
    const package_format::LoadedPackage* package,
    const simulation::EncounterSnapshot& snapshot,
    simulation::UnitId unit,
    const char* member_name = nullptr
) noexcept;

// One name a client already holds, folded on the terms above and cut to the
// same capacity.
//
// For the surfaces that name a person outside a battle, such as a company
// roster, an aftermath or a recruitment line. There is no board unit to ask
// about and so nothing for `character_name` to derive from, but the same fold
// has to apply or one client spells one person's name two ways.
[[nodiscard]] ContentName person_name(const char* name) noexcept;

// What kind of character that is: the class its unit type belongs to.
//
// The class rather than the unit type, and the two are worth telling apart.
// `DAWN ARCHER` and `ASHEN ARCHER` are two unit types wearing two factions'
// colours and one class, `ARCHER`, whose stats and growth they both take. A
// surface that writes the name over the class is telling a player who somebody
// is and what they can do, which the unit type alone says only by implication.
//
// Empty when the package is null or holds no class for this unit type: a
// caller with nothing to draw draws nothing, rather than a row reading `CLASS`.
[[nodiscard]] ContentName class_name(
    const package_format::LoadedPackage* package, std::uint64_t unit_type_id
) noexcept;

// ---------------------------------------------------------------------------
// The battle's own line
// ---------------------------------------------------------------------------

// What a win condition is called, in one table rather than one per client, for
// the reason the four tables above are here: three clients naming one rule
// three different ways is how a board comes to mean different things on
// different machines.
[[nodiscard]] const char* objective_name(
    simulation::ObjectiveKind kind
) noexcept;

// How many rounds this encounter is won by surviving, or zero when nothing on
// it is. The first survive-rounds objective in identifier order, because that
// is the order the engine resolves them in and therefore the one a player is
// actually racing.
[[nodiscard]] std::uint32_t rounds_to_survive(
    const std::vector<simulation::ObjectiveDefinition>& objectives
) noexcept;

// "ROUND 3 OF 7" written into `out`, or "ROUND 3" when `total` is zero. The
// number shown is the round *in progress*, which is one more than the rounds
// the snapshot says have completed: a player counts the round they are in,
// not the ones behind them.
//
// Written rather than returned so a console with no heap can hold it in a
// stack buffer. `capacity` includes the terminator; the line is truncated
// rather than overrunning.
void round_line(
    std::uint32_t completed_rounds,
    std::uint32_t total,
    char* out,
    std::size_t capacity
) noexcept;

// ---------------------------------------------------------------------------
// The sheet
// ---------------------------------------------------------------------------

// Forty columns, because that is what a 320-pixel console screen holds in an
// eight-pixel font, and both consoles have exactly that much width. A line
// that fits the narrowest surface fits
// every surface, so there is one width rather than one per machine.
inline constexpr int unit_sheet_columns = 40;

// What a campaign knows about a character that a battle does not.
//
// Level and experience were deliberately left off this sheet when it was
// written, with the reason recorded in `platform/sheet/README.md`: the numbers
// existed in `engine/campaign` and no client ran a campaign, so a sheet that
// printed them would have printed a one and a nought for every character in
// every game, forever. A client runs one now, so the block exists as a pointer
// that is null for every caller that has no campaign, which is what keeps a
// battle-only client's sheet byte for byte the sheet it already drew.
//
// Nothing here is computed, exactly as nothing else on the sheet is. Both
// numbers are read out of `campaign::Progression`, which the campaign runtime
// wrote and the save carried; a sheet that derived a level from an experience
// total would be a second answer to a question the engine already answers.
struct CampaignContext final {
    std::uint16_t level{1};
    std::uint32_t experience{};
};

// The tallest sheet the shipped vocabulary produces is twelve lines: the
// header, three stat rows, a weapons heading with one row per carried weapon,
// an abilities heading with one row per ability, and an items heading with one
// row per carried item. Sixteen leaves room for a character carrying more than
// any shipped one does; anything past it is dropped rather than allowed to
// write off the end, which is also what bounds the sheet's height on a screen.
inline constexpr int unit_sheet_capacity = 16;

// The most weapons, abilities and items a sheet lists. A character carrying
// more has the remainder dropped, which is a screen that ran out rather than a
// sheet that lied: everything shown is still that character's own.
inline constexpr int unit_sheet_max_weapons = 4;
inline constexpr int unit_sheet_max_abilities = 4;
inline constexpr int unit_sheet_max_items = 4;

struct UnitSheet final {
    char lines[unit_sheet_capacity][unit_sheet_columns + 1]{};
    int count{0};

    // The line at `index`, or the empty string past the end, so a renderer
    // walking a fixed number of rows never reads uninitialised storage.
    [[nodiscard]] const char* line(int index) const noexcept {
        if (index < 0 || index >= count) return "";
        return lines[index];
    }
};

// Builds the sheet for `unit`.
//
// `name` is what this client calls the character, and it is the one string the
// sheet takes from its caller, because the campaign's own name for a member
// travels through a join only a client holds. **Null is the ordinary case**,
// and it means "you decide": the sheet then asks `character_name`, which is
// where the answer comes from either way. Passing a name already resolved
// through that function is exact, not merely allowed, because the first thing
// it does with one is hand it back.
//
// `weapons`, `abilities` and `items` are the registries the encounter was
// created with, handed to every presenter through `battle_definitions`. A
// weapon the registry does not describe is still listed by name: what is
// missing is its band and its accuracy, and the sheet says so rather than
// inventing them. An item is listed the same way, with how many are left. The
// count is the snapshot's, so a sheet read after a draught was drunk says so.
//
// `remaining_action_points` is the snapshot's own number for the unit part-way
// through an activation, and is ignored for anybody else, whose action points
// are the ones its class gives it. Pass the snapshot; this reads both.
//
// `campaign` is what the campaign holds about this character, or null when
// there is no campaign: a battle played on its own, a console with no save
// adapter, an authoring preview. Null is not a level of one: it is a sheet with
// no campaign row at all, so a client that has never run a campaign is not made
// to show a number that means nothing in it.
// `package` is the package the platform is holding, and is what every name on
// the sheet is asked of first: the class, the weapons, the spells and the
// pack. Null is a caller with no package, and then every row falls back to the
// tables above.
[[nodiscard]] UnitSheet build(
    const sim::EncounterSnapshot& snapshot,
    const sim::UnitSnapshot& unit,
    const char* name,
    const std::vector<sim::WeaponDefinition>& weapons,
    const std::vector<sim::AbilityDefinition>& abilities,
    const std::vector<sim::ItemDefinition>& items,
    const CampaignContext* campaign = nullptr,
    const package_format::LoadedPackage* package = nullptr
);

}  // namespace grandleon::sheet
