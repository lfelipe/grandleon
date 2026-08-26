// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/core/random.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace grandleon::simulation {

using UnitId = std::uint64_t;
using ContentId = std::uint64_t;

struct Position final {
    std::int16_t x{};
    std::int16_t y{};
};

[[nodiscard]] constexpr bool operator==(
    Position lhs,
    Position rhs
) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

enum class Side : std::uint8_t {
    first = 0,
    second = 1,
};

enum class Outcome : std::uint8_t {
    ongoing = 0,
    first_side_won,
    second_side_won,
};

// What a cell asks of a unit that would stand in it. This is the whole
// passability vocabulary, closed on purpose: a cell either takes anyone, or it
// takes only those who can cross water, or only those who can climb.
//
// Whether a cell takes somebody and what it charges them are two questions,
// and this is only the first. Ground that should be slow rather than shut is
// open here and priced by `EncounterDefinition::movement_cost`; an unreachable
// tile and an expensive one are different answers and this engine keeps them
// different.
//
// Values are hashed and serialized, so the list is append-only.
enum class Terrain : std::uint8_t {
    open = 0,
    water = 1,
    heights = 2,
};

// What a unit may enter beyond open ground, as a set of bits on
// `UnitDefinition::crossings`. Zero is the default and means a walker: open
// ground and nothing else.
inline constexpr std::uint8_t crossing_none = 0U;
inline constexpr std::uint8_t crossing_water = 1U << 0U;
inline constexpr std::uint8_t crossing_heights = 1U << 1U;
// Flight, and deliberately the high bit: a flier crosses every terrain there
// is, including any appended below this bit later, so "flying" keeps meaning
// what an author meant by it when the vocabulary grows.
//
// The same promise about price: **a flier pays one everywhere.** Marsh and
// snow are underfoot facts, and nothing underfoot slows somebody who is not
// walking on it. See `entry_cost`. A flier is therefore untouched by any
// price a board carries, exactly as it is untouched by any wall.
inline constexpr std::uint8_t crossing_every = 1U << 7U;

// Whether a unit carrying `crossings` may stand in `terrain`. The single
// definition of passability: the move rule, the read-only queries, and the
// unattended-side policy all ask this and nothing else.
[[nodiscard]] constexpr bool can_enter(
    Terrain terrain,
    std::uint8_t crossings
) noexcept {
    if (terrain == Terrain::open) return true;
    if ((crossings & crossing_every) != 0U) return true;
    switch (terrain) {
        case Terrain::water: return (crossings & crossing_water) != 0U;
        case Terrain::heights: return (crossings & crossing_heights) != 0U;
        case Terrain::open: return true;
    }
    return false;
}

// The cheapest cost any cell can charge, and what every cell of a board that
// prices nothing charges.
inline constexpr std::uint8_t movement_cost_step = 1U;

// What a unit carrying `crossings` pays to enter a cell whose authored price is
// `authored`. The single definition of price, on the terms `can_enter` is the
// single definition of passage: the move rule, the read-only queries and the
// unattended-side policy all ask this and nothing else.
//
// Paid on *entering*. The cell a character starts its turn on charges nothing,
// because it is not entered: a character standing in a marsh is already
// through it.
//
// Zero is not a price, it is a missing one: a board handing this a zero would
// buy an unbounded walk out of a cell that looks ordinary. The floor is here
// rather than at every caller so that no path can be cheaper than its length,
// which is what lets `Encounter::reachable` refuse a far tile by straight-line
// distance before it searches at all.
[[nodiscard]] constexpr std::uint8_t entry_cost(
    std::uint8_t authored,
    std::uint8_t crossings
) noexcept {
    if ((crossings & crossing_every) != 0U) return movement_cost_step;
    return authored < movement_cost_step ? movement_cost_step : authored;
}

// Physical damage is reduced by defence, magical damage by resistance. One
// resistance stat rather than a full type matrix: no authored content needs
// more, and the matrix can be added later without changing this enumeration.
enum class DamageType : std::uint8_t {
    physical = 0,
    magical = 1,
};

// Enumerated rather than scripted, so that resolving an area needs no
// allocation, no interpretation, and no per-target ordering ambiguity.
enum class AreaShape : std::uint8_t {
    single = 0,
    cross = 1,
    diamond = 2,
};

enum class AbilityKind : std::uint8_t {
    damage = 0,
    restore = 1,
};

struct AbilityDefinition final {
    ContentId id{};
    AbilityKind kind{AbilityKind::damage};
    DamageType damage_type{DamageType::physical};
    AreaShape area{AreaShape::single};
    std::int16_t power{};
    std::uint8_t minimum_reach{1};
    std::uint8_t maximum_reach{1};
    std::uint8_t radius{};
    // How often this cast lands, as a percentage in [0, 100]. See
    // `WeaponDefinition::accuracy`; a damaging area rolls once per covered
    // opponent, in ascending unit identifier order. A restoring ability never
    // rolls, so this number is not read for one.
    std::uint8_t accuracy{100};
};

// One kind of weapon, and what holding it is worth against the other kinds.
//
// The edges are directed: a type names what it beats, and the strike coming
// back the other way is worth as much less. A type never names itself, which
// both analyzers and the compiler refuse, because the same edge would then be
// read once in each direction and price every mirror match twice.
struct WeaponTypeDefinition final {
    ContentId id{};
    std::vector<ContentId> strong_against;
    // What the advantage is worth, in damage and in whole percentage points of
    // accuracy. Both zero means an edge that changes nothing, which is what a
    // game states by omitting `weaponAdvantage` while a type still names what
    // it beats.
    std::int16_t damage{};
    std::uint8_t accuracy{};
};

// The numbers the rules read off a weapon record. No damage type: a weapon's
// damage is mitigated by defence, and only an ability carries a type.
struct WeaponDefinition final {
    ContentId id{};
    std::int16_t power{};
    std::uint8_t minimum_reach{1};
    std::uint8_t maximum_reach{1};
    // How often this weapon lands, as a percentage in [0, 100]. One hundred is
    // the default: it always lands and draws no number at all, which keeps the
    // consumption order of the hit stream auditable. Anything below rolls
    // once, from `core::RandomStream::hit`, against exactly the number a
    // forecast shows.
    std::uint8_t accuracy{100};
    // Which kind of weapon this is. Zero is a weapon of no kind, which is what
    // every weapon authored without one is, and a weapon of no kind gets
    // nothing out of a triangle.
    //
    // Last, and deliberately: this is an aggregate the tests and the loader
    // brace-initialise by position, so a field added in the middle would
    // quietly become whatever the next one used to be.
    ContentId weapon_type{};
};

// What using an item does. Append-only: the value is hashed through a unit's
// carried list and decoded from a package record.
enum class ItemKind : std::uint8_t {
    // The item has no effect the rules can apply. A key, a letter, a keepsake:
    // it may be carried, listed and counted, and using it in battle is refused
    // rather than silently doing nothing. This is what every item authored
    // before items could be used says, so no existing content becomes usable
    // by accident.
    none = 0,
    // Restores health to whoever uses it, by `power`, clamped to the health
    // they are missing. Deterministic: mercy does not miss, exactly as a
    // restoring ability does not.
    restore = 1,
};

// The numbers the rules read off an item record. There is no reach band: an
// item reaches the hand that holds it and nothing else, so the only distance
// question is one the rules answer without an authored number.
struct ItemDefinition final {
    ContentId id{};
    ItemKind kind{ItemKind::none};
    std::int16_t power{};
};

enum class ObjectiveKind : std::uint8_t {
    defeat_all_opponents = 0,
    defeat_target = 1,
    protect_target = 2,
    // Hold out until a number of rounds has *completed*. Appended, so no value
    // renumbers and no package written before it re-means anything.
    //
    // It is an ordinary win condition in the model the other three keep: the
    // first objective to resolve decides the outcome. It differs only in when
    // it resolves, at a round boundary rather than after a blow, which is why
    // the objectives are asked a second time when a turn advance moved the
    // round. See `EncounterSnapshot::round` for what a round is.
    survive_rounds = 3,
};

// `side` is the side the objective belongs to: satisfying it favours that side,
// failing it favours the other.
struct ObjectiveDefinition final {
    ContentId id{};
    ObjectiveKind kind{ObjectiveKind::defeat_all_opponents};
    Side side{Side::first};
    UnitId target_unit_id{};
    // How many rounds a `survive_rounds` objective is about, and zero for every
    // other kind.
    //
    // Authored as a pair with the kind or not at all: a count below one on a
    // survive objective is a battle already won before it opened, and a count
    // on a kind that cannot read one is a number nothing will ever consult.
    // Both are `invalid_objective` rather than a silently corrected or ignored
    // half, on the standard a half-authored drop already sets.
    std::uint32_t round_count{};
};

enum class ObjectiveState : std::uint8_t {
    pending = 0,
    satisfied = 1,
    failed = 2,
};

struct ObjectiveResult final {
    ContentId id{};
    ObjectiveState state{ObjectiveState::pending};
};

// How the engine decides who acts next.
enum class TurnOrder : std::uint8_t {
    // The sides alternate and the player picks any unit on the active side.
    // This is the default.
    alternating = 0,
    // Every unit on the first side acts, then every unit on the second. The
    // engine names the *side* whose block is open and never names the actor:
    // whoever holds that side picks any of its characters who has not acted
    // yet, in whatever order they like, and the block ends when none is left.
    //
    // It deliberately does not name the actor as well, fastest first. Playing
    // an order that does showed the cost on a cartridge: told which character
    // to use, a player has to try each of their line in turn to find out which
    // one the engine picked, which reads as a broken board rather than as a
    // rule. `initiative` is where an engine-chosen order is the point.
    //
    // **A block holds no exclusive activation.** Turns interleave freely:
    // walk one character, walk a second, come back and strike with the first.
    // Nobody is locked in by having started, so `active_unit_id` and
    // `remaining_action_points` stay zero for the whole block and every
    // question about what a character has left is answered by that character's
    // own `has_moved`, `has_acted` and `spent_action_points`.
    //
    // The rule per character is unchanged by that: one walk, one action, and a
    // strike finishes the character whatever points are left unless it is
    // authored `acts_after_attacking`.
    side_blocks = 1,
    // All units interleaved by speed, regardless of side. The engine names the
    // actor here, because a speed order across sides is the whole rule.
    initiative = 2,
};

struct UnitDefinition final {
    UnitId id{};
    ContentId unit_type_id{};
    Side side{Side::first};
    Position position{};
    std::int16_t health{};
    std::int16_t strength{};
    // Equipped-weapon power, resolved by the caller like reach. Basic-attack
    // damage is strength + power - defense, minimum one.
    std::int16_t power{};
    std::int16_t defense{};
    std::int16_t resistance{};
    // The four stats that price *whether* a blow lands and what a cast is worth
    // in the caster's own hands. All four default to zero, and at zero every
    // number this engine produces is the number it produced before they
    // existed: `skill`, `luck` and `evasion` fold into the hit chance and
    // cancel out of it at zero, and `magic` folds into a magical cast's damage
    // exactly as `strength` folds into a weapon's.
    //
    // Percentage points added to the chance a strike *this* unit makes lands.
    std::int16_t skill{};
    // Percentage points on this unit's side of every hit roll: added when it
    // swings, subtracted when it is swung at. That symmetry is luck's whole
    // role: there are no critical hits in this engine for it to avoid.
    std::int16_t luck{};
    // Percentage points subtracted from the chance a strike *against* this unit
    // lands.
    std::int16_t evasion{};
    // The magical counterpart to `strength`. A magical cast deals
    // `max(1, magic + power - resistance)`, so a better mage casts harder. A
    // physical cast is unchanged at `max(1, power - defense)`: `strength`
    // already reaches the board through every basic attack, and adding it
    // there too would make an ability a swing under another name.
    std::int16_t magic{};
    // Orthogonal steps per move command. Zero means the unit cannot move.
    std::uint8_t movement{1};
    // Commands the unit may issue during one activation. Two lets it move and
    // then attack; one is the classic single-action turn.
    std::uint8_t action_points{1};
    // Higher acts earlier in an ordered turn. Ignored by alternating order.
    std::uint8_t speed{1};
    // Whether the unit may keep acting after it attacks or uses an ability.
    // Off by default: striking normally ends an activation.
    bool acts_after_attacking{false};
    // Weapon reach of the weapon in hand. When `weapon_ids` is not empty the
    // engine resolves this and `power` from the first carried weapon, so a
    // caller cannot disagree with the engine about what the unit is holding.
    std::uint8_t minimum_reach{1};
    std::uint8_t maximum_reach{1};
    std::vector<ContentId> ability_ids;
    // Every weapon the unit's type lists, in that order. The first is the one
    // in hand; the rest are choices an attack command may name. Empty means
    // bare-handed, and then `power` and the reach band above stand as given.
    std::vector<ContentId> weapon_ids;
    // Terrain this unit may enter beyond open ground: any of `crossing_water`,
    // `crossing_heights`, `crossing_every`. Zero is a walker.
    std::uint8_t crossings{crossing_none};
    // How often the weapon in hand lands, as a percentage. Resolved from the
    // first carried weapon exactly as `power` and the reach band are, so a
    // caller cannot disagree with the engine about how accurate the thing the
    // unit is holding is. A unit carrying nothing keeps what it was defined
    // with, and the default always lands.
    std::uint8_t accuracy{100};
    // Every item the unit's type lists, in that order, and how many of each it
    // brings to this battle. An empty `item_counts` means one of each, which is
    // what a unit type's authored list says today; a caller with a campaign
    // inventory to spend passes the counts it holds. A count of zero, or a
    // length that disagrees with `item_ids`, is `invalid_item` rather than a
    // silently corrected number.
    std::vector<ContentId> item_ids;
    std::vector<std::uint16_t> item_counts;
    // What this unit leaves behind when it falls, and how often. Zero is the
    // default: a unit that leaves nothing and draws no number at all.
    //
    // The two fields are authored together or not at all: a chance without an
    // item is a roll with no outcome, an item without a chance is an outcome
    // nothing reaches, and both are `invalid_item` rather than a silently
    // ignored half. The chance is a whole percentage in [0, 100], on the same
    // terms as a weapon's accuracy and a growth rate: the authored number is
    // the rolled number.
    //
    // The identity is *not* resolved against `items`. A drop is recorded, not
    // handed to anybody, so nothing in the battle reads what it does (see
    // `DropRecord`), and requiring the definition would make a droppable item
    // something the loader had to register on every board it could fall on.
    ContentId drop_item_id{};
    std::uint8_t drop_chance{};
    // Added to the maximum of the band of every weapon this unit strikes with,
    // saturating at the widest band a byte can hold. Zero is the default: a
    // unit whose reach is exactly its weapon's, so no board that does not
    // author one moves.
    //
    // On the unit rather than on the weapon because it is a fact about the
    // archer and not about the bow: two archers carrying one authored weapon
    // record have one bow between them, and only one of them may have been
    // written to shoot further. A campaign fills it from what an author wrote
    // about a member; a board with nobody's specificity on it leaves it zero.
    //
    // It raises the ceiling and never the floor. Lowering the floor would let
    // an archer whose band starts at two answer a swordsman standing on top of
    // her, which is the refusal `counters` documents and every client names.
    //
    // An ability's reach is not affected. An ability's power and shape come
    // from the ability rather than from the caster, and this is about the arm
    // that swings.
    std::uint8_t reach_bonus{};
    // What talking to this unit records. Zero is the default: somebody no talk
    // may reach. A board on which every unit says zero hashes and plays as if
    // the gesture did not exist.
    //
    // Opaque here on purpose. The rules never ask what this identity *means*:
    // they copy it into the `unit_talked` event and stop. Reading it as the key
    // of a world flag is the campaign layer's job, which keeps the rules free
    // of campaign vocabulary exactly as they are free of presentation's.
    //
    // Authored on a placement rather than on a unit type, because being
    // talkable is a fact about this character on this board (the same captain
    // is an ordinary enemy on the next map), and because a battle-local unit
    // identifier is derived per encounter and is a different number every time
    // the character appears, so it is the only identity a campaign could read.
    ContentId talk_record_id{};
    // When this character enters the board, as a round *in progress*, one
    // based. Zero is the default: a character standing on the board from the
    // opening.
    //
    // The earliest arrival is the **second** round, because the first round is
    // the round the battle opens in and somebody who is there when it opens is
    // a placement rather than an arrival. That is worth more than the round it
    // gives up: an arrival can then only ever land on a turn advance, so no
    // wave lands while the deployment phase is open, none lands before the dice
    // are seeded, and there is exactly one place in this file where a wave
    // comes in.
    //
    // `arrival_every` and `arrival_times` state the recurrence: how many rounds
    // separate one arrival from the next, and how many arrivals there are in
    // all. They are authored together or not at all, and either half alone is
    // `invalid_arrival`: a gap with no number of arrivals is a stream no battle
    // could outlast, and a number of arrivals with no gap is several characters
    // landing on one round, which is a stack rather than a wave.
    //
    // `create_encounter` expands the recurrence, here in the rules rather than
    // in either loader, because the browser builds a definition in TypeScript
    // and the consoles build one out of a package: expanding in a loader would
    // be one rule kept in two places that have to agree. The first arrival
    // keeps this definition's identifier, so an objective or a talk that names
    // the placement names the first of the wave; every later arrival takes the
    // lowest identifier the definition does not use.
    std::uint32_t arrival_round{};
    std::uint32_t arrival_every{};
    std::uint16_t arrival_times{};
    // Whether this unit's health may reach zero. False is the default: a unit
    // whose health floor is zero.
    //
    // A unit that endures is left standing at one health by a blow, a counter or
    // a cast that would have taken it to zero or below. It is not immune, it
    // does not answer differently, and it costs nothing extra to hit: the only
    // thing that changes is the lower bound of one subtraction, and the only
    // rule that follows is that it is never defeated.
    //
    // On the unit rather than on the side or the command, for the reason
    // `reach_bonus` is on the unit: it is a fact about this character on this
    // board, and one board may hold characters who endure standing beside
    // characters who do not. What *puts* it there is not this module's
    // business: a campaign fills it from what a project declared, exactly as it
    // fills `reach_bonus` from what an author wrote about a member, and the
    // rules never learn why.
    //
    // It is canonical state. See `Encounter::canonical_hash`, which folds it
    // only where it is set, so that a board on which nobody endures hashes to
    // the value it always did.
    bool endures{false};
};

// The most arrivals one authored character may make. A bound rather than a
// taste: the recurrence is expanded into real characters, so a wave with no
// ceiling is a board with no ceiling.
inline constexpr std::uint16_t maximum_arrivals = 64;

// Boards above this many cells are rejected as invalid_map: every command may
// allocate a visited grid of width x height, and no authored content comes
// near a 256 x 256 board.
inline constexpr std::uint32_t maximum_board_cells = 65536;

// The largest value a stat that reaches the damage arithmetic may carry.
//
// **This is a sanity bound on authored content, and it is the reason a number
// this shape is a refusal at creation instead of a surprise at the board.** It
// bounds every stat that can appear on either side of that arithmetic: a unit's
// `strength`, `power`, `defense`, `resistance` and `magic`, an ability's and a
// weapon's `power`, and the damage a weapon-kind advantage is worth. Both gates
// that build an encounter enforce it, `create_encounter` and the package
// loader.
//
// Half of `int16`'s range, rounded down, which is where the number comes from:
// two of these sum to 32 766, one short of what `int16` holds.
//
// **It is no longer what keeps the narrowing safe, and the history is worth
// knowing.** It used to be: damage is computed wide, in `int32`, and narrowed
// back to the `int16` health is kept in, and "two bounded stats cannot sum past
// the type" was the whole proof. Then the weapon triangle put a third term into
// `attack_damage` and the proof lapsed without a line of this comment changing:
// a strength of 16 383 swinging a power of 16 383 with an advantage of 999
// narrowed to −31 771, the forecast promised it, `apply` delivered it, and being
// hit healed the target. What keeps the narrowing safe now is the narrowing
// itself, which saturates (`narrowed_damage` in `encounter.cpp`), so a fourth
// term cannot reopen this the way the third one did. A bound that has to be
// re-proved by whoever adds the next term is a bound that will eventually be
// wrong.
//
// The largest number in any shipped project's content is 55, so this refuses no
// board anybody has written. It is not a design budget and nothing should read
// it as one: a project wanting bigger numbers than this wants a wider health
// type, and that is a different change.
inline constexpr std::int16_t maximum_stat = 16383;

// The last round a wave may be authored to arrive on.
//
// `arrival_round` is a `uint32` because a round count is, but the
// `unit_arrived` event reports the round in progress in the `int16` field every
// event reports a number in. Saturating there would have a client narrating
// "wave 32767" for the thousandth wave and never learning it had been lied to,
// so the number is bounded here, at creation, where an author can be told. No
// battle runs thirty-two thousand rounds.
inline constexpr std::uint32_t maximum_arrival_round = 32767;

struct EncounterDefinition final {
    std::uint16_t width{};
    std::uint16_t height{};
    std::vector<UnitDefinition> units;
    std::vector<AbilityDefinition> abilities;
    // An empty list is treated as a single defeat-all-opponents objective for
    // each side, which is exactly the v0 rule.
    std::vector<ObjectiveDefinition> objectives;
    TurnOrder turn_order{TurnOrder::alternating};
    // Every weapon any unit in this encounter may carry, by identity. A unit
    // names the ones it carries; this is where those names resolve.
    std::vector<WeaponDefinition> weapons;
    // Which kinds of weapon beat which, and what beating them is worth.
    //
    // Empty is a battle with no triangle in it, which is every battle written
    // before one could be drawn, and every battle in a game that draws none.
    // The advantage each entry carries is the project's own and is the same on
    // every one of them: it rides with the edges for the reason an encounter
    // carries its turn order, which is that a runtime reads one record at a
    // time and never sees a project.
    std::vector<WeaponTypeDefinition> weapon_types;
    // Every item any unit in this encounter may carry, by identity, for the
    // same reason weapons are here: a unit names the ones it carries and this
    // is where those names resolve.
    std::vector<ItemDefinition> items;
    // What each cell asks of whoever would stand in it, row-major, width x
    // height. Empty means every cell is open ground, which is what a package
    // written before terrain had a meaning says and what it has always meant.
    // Any other length is invalid_map.
    std::vector<Terrain> terrain;
    // What each cell charges whoever walks into it, row-major, width x height,
    // parallel to `terrain`. Empty means every cell costs one step, which is
    // what a package written before ground had a price says and what it has
    // always meant. Any other length is `invalid_map`, and so is a cell
    // charging zero: a free cell is not a cheap cell, it is an unbounded walk.
    //
    // A separate list from `terrain` rather than a second field on it, because
    // they are answers to different questions, what a cell *takes* and what it
    // *charges*, and because a board that prices nothing carries no list at
    // all rather than one repeating a default per cell.
    std::vector<std::uint8_t> movement_cost;
    // The seed every random stream in this encounter is drawn from. Zero is the
    // default and means no seed was chosen, so `create_encounter` derives one
    // from the encounter's own opening state. See
    // `engine/core/include/grandleon/core/random.hpp`.
    //
    // This is a caller-supplied number rather than an authored one. A campaign
    // that wants a battle to differ between playthroughs passes a seed down
    // from its save; the simulation may not read a clock to invent one.
    std::uint64_t random_seed{};
    // The region the player arranges their own characters in before the first
    // activation. Empty is the default and means there is no deployment phase:
    // the board opens on the first activation.
    //
    // The region says *where*. *Who* falls out of the placements already
    // written: a first-side unit whose defined position is one of these tiles
    // is deployable, and every other unit stands where it was put. Nothing is
    // named twice, and an author who wants a character pinned puts them outside
    // the region.
    //
    // Tiles must be in bounds and distinct; anything else is
    // `invalid_deployment`. They are *not* judged against terrain here: a
    // region tile nobody could stand on is content the compiler refuses at
    // authoring time, where the author is, rather than a rule this module keeps
    // a second copy of.
    std::vector<Position> deployment_tiles;
};

enum class CreateError : std::uint8_t {
    none = 0,
    invalid_map,
    invalid_unit,
    duplicate_unit,
    occupied_position,
    // A side has nobody standing on the board when the battle opens.
    //
    // Membership is not enough, and the difference is a board that cannot be
    // played. A side whose every character is a wave can take no command, since
    // each one is refused `unarrived_unit`, and under `alternating` the round
    // only turns on an *accepted* command, so no wave can ever land,
    // no objective can ever resolve, and the outcome stays `ongoing` for as
    // long as anybody keeps pressing. The same board under `initiative`
    // completes its first round inside `create_encounter` instead, so a
    // `survive_rounds` objective on it resolves a round early. Refused here,
    // once, rather than half-honoured differently by each turn order.
    missing_side,
    invalid_ability,
    invalid_objective,
    invalid_weapon,
    // An item definition is malformed or duplicated, a unit carries an item
    // identity this encounter does not define or carries the same one twice,
    // the counts a unit brings do not line up one-for-one with what it carries,
    // or a unit's drop is half-authored (a chance with nothing to leave, or
    // something to leave with no chance of leaving it), or its chance is above
    // a hundred.
    invalid_item,
    // A deployment tile is off the board, or the same tile is named twice.
    invalid_deployment,
    // An arrival is malformed: a recurrence half-stated, more arrivals than
    // `maximum_arrivals`, or a wave whose expansion the board cannot hold.
    invalid_arrival,
    // A kind of weapon is malformed or duplicated, names an advantage over
    // nothing or over itself, or carries a number the damage arithmetic cannot
    // read.
    //
    // Its own refusal rather than `invalid_weapon`, on the standard the rest of
    // this list keeps: a kind is not a weapon, and an author told "a weapon is
    // invalid" would go looking through the wrong records. The triangle is the
    // one table an author writes about the relationships *between* records, so
    // it is the one whose refusal most needs to say which table it means.
    //
    // Appended, like every enumerator here: the WebAssembly binding reads these
    // names out of the engine rather than restating them, so a value that
    // renumbered would re-mean every refusal a browser has ever shown.
    invalid_weapon_type,
};

[[nodiscard]] std::string_view error_name(CreateError error) noexcept;

// Every character an authored board actually puts on the field: the placements
// as written, plus each later arrival of every wave, each stating the one round
// it comes on. `create_encounter` is the first caller and the reason the rule
// lives in the rules, but it is not the only caller that needs the answer: a
// campaign reading a finished battle's events has to know what unit type the
// third of a wave was, and that character never appeared in the authored list.
// Asking here is what keeps the two from disagreeing about who was on the
// board.
//
// Returns false for a malformed wave, which `create_encounter` reports as
// `CreateError::invalid_arrival`. `expanded` is appended to.
[[nodiscard]] bool expand_arrivals(
    const std::vector<UnitDefinition>& authored,
    std::vector<UnitDefinition>& expanded
);

struct UnitSnapshot final {
    UnitId id{};
    ContentId unit_type_id{};
    Side side{Side::first};
    // How many of this unit's action points its own turn has spent so far.
    // Read it with `has_acted` and `has_moved` further down, which is where it
    // belongs by subject.
    //
    // Zero under `alternating` and `initiative`, where the side commits to one
    // character at a time and `EncounterSnapshot::remaining_action_points` is
    // the whole of the budget being spent. It is kept only under `side_blocks`,
    // where every character on the open side may be part-way through a turn at
    // once, so a single side-wide counter cannot say what any of them has left.
    //
    // Cleared with `has_moved` the moment the character finishes. A unit
    // between turns carries no turn state at all, which is what lets the
    // canonical hash fold this only when it is non-zero.
    //
    // **Its position in this struct is load-bearing.** `side` is one byte
    // followed by a two-byte-aligned `Position`, so the byte after it is
    // padding on every target this engine builds for. Down beside the two
    // flags it belongs with by subject, this field would cost two bytes per
    // character; here it costs none and the struct is exactly the size it was
    // without it. A console allocates one of these per character on the board
    // out of a heap it counts in kilobytes, so the free byte is worth taking.
    std::uint8_t spent_action_points{0};
    Position position{};
    std::int16_t health{};
    std::int16_t maximum_health{};
    std::int16_t strength{};
    std::int16_t power{};
    std::int16_t defense{};
    std::int16_t resistance{};
    // See `UnitDefinition`. `skill` and `luck` raise the chance this unit's own
    // strikes land; `evasion` and `luck` lower the chance a strike against it
    // lands; `magic` prices a magical cast the way `strength` prices a swing.
    // A physical cast is priced by neither.
    std::int16_t skill{};
    std::int16_t luck{};
    std::int16_t evasion{};
    std::int16_t magic{};
    std::uint8_t movement{1};
    std::uint8_t action_points{1};
    std::uint8_t speed{1};
    bool acts_after_attacking{false};
    // The band of the weapon in hand, and the band a counterattack is gated on:
    // a unit struck from inside it strikes back, a unit struck from outside it
    // does not. That makes these two numbers a trade rather than a targeting
    // parameter: an archer with a minimum of two outranges a swordsman and
    // cannot answer one standing on top of it.
    std::uint8_t minimum_reach{1};
    std::uint8_t maximum_reach{1};
    std::vector<ContentId> ability_ids;
    // Whether this unit has already taken its turn in the current round.
    // Always false under alternating order, where a round is a turn for each
    // side rather than a pass over the characters, so there is nothing per
    // character to mark.
    //
    // Under `side_blocks` this is the field a client draws a spent character
    // with, and the field the engine refuses a second turn on: the engine names
    // no actor there, so "may this one still act" is a question only this
    // answers. A spent character stays on the board and stays inspectable,
    // because a player needs to see their whole line, and is simply not
    // choosable.
    //
    // It means *finished*, not *has done something*. Under `side_blocks` a
    // character who has walked but not yet struck is not marked here, because
    // it is still owed the rest of its turn; what marks it is running out of
    // points, striking, waiting or falling. `wait` is the deliberate way a
    // player says "nothing more from this one" and it is what sets this.
    bool has_acted{false};
    // Whether this unit has already made its turn's one move.
    //
    // A unit gets at most one move per turn, however many action points it
    // has; the rest are for acting. Cleared the moment the character finishes,
    // which is what lets the canonical hash fold it only when it is true (see
    // `canonical_hash`). Under `alternating` and `initiative` a character's turn
    // is one activation and this is therefore false between activations; under
    // `side_blocks` several characters may be part-way through their turns at
    // once, and each carries its own.
    bool has_moved{false};
    // And `spent_action_points`, which belongs here by subject and stands up
    // beside `side` because that is where it was free. See it there.
    // The weapons this unit carries, in carried order. The first is in hand
    // and is what `power` and the reach band above describe.
    std::vector<ContentId> weapon_ids;
    // Terrain this unit may enter beyond open ground. See `crossing_water`.
    std::uint8_t crossings{crossing_none};
    // How often the weapon in hand lands, as a percentage. This is the number
    // a counterattack rolls against, and the number a forecast shows when this
    // unit is the one striking back.
    std::uint8_t accuracy{100};
    // The items this unit carries, in carried order, and how many of each it
    // still has. The two lists are always the same length. `item_counts` is
    // battle-local state and canonical: using an item decrements it, the number
    // is hashed like a health total, and a replay of the same commands finds
    // the same satchel. A count of zero is an item spent: the row stays so a
    // client can show what ran out.
    std::vector<ContentId> item_ids;
    std::vector<std::uint16_t> item_counts;
    // What this unit leaves behind when it falls, and how often. See
    // `UnitDefinition`; both are authored constants and neither changes during
    // a battle. They are canonical all the same, because two otherwise
    // identical units, one of which can leave a tonic, are not in the same
    // battle: one of them moves the drop stream and the other does not.
    ContentId drop_item_id{};
    std::uint8_t drop_chance{};
    // See `UnitDefinition::reach_bonus`. Already added into `maximum_reach`
    // above, which is the band of the weapon in hand; it is carried here as
    // well because a strike that *names* a weapon, and the danger overlay that
    // unions every weapon a unit carries, each resolve a band this unit's
    // in-hand number does not describe, and both must widen by the same
    // number rather than by a number each derives for itself.
    //
    // Canonical: announced by a bit of the character's presence byte in
    // `canonical_hash` and folded where that bit is set.
    //
    // It would be tempting to leave it out altogether on the standard the hash
    // sets for the accuracies of carried weapons that are not in hand: that it
    // reaches the hash through `maximum_reach` for the weapon in hand, and
    // through the strikes it enables for the rest. That argument holds only
    // while `maximum_reach` has room to carry it, and `widened_reach`
    // saturates. Two characters whose in-hand band already reaches 250, one
    // with a bonus of ten and one with a bonus of twenty, both snapshot a
    // `maximum_reach` of 255 and would hash the same, while a second carried
    // weapon resolves a band five tiles wider for the second than for the
    // first, so a strike one of them may make the other may not. Two battles a
    // sequence of commands does tell apart is exactly the condition this hash
    // has always refused, so the number is folded rather than inferred.
    std::uint8_t reach_bonus{};
    // See `UnitDefinition::talk_record_id`. Zero is somebody no talk may reach.
    ContentId talk_record_id{};
    // Whether this character has been talked off the board.
    //
    // **Departed is not defeated, and the difference is the point.** A departed
    // character keeps the health it had; expressing departure by zeroing health
    // would have made it a defeat everywhere the board is spelled `health > 0`,
    // which is the one confusion this gesture exists to avoid. It emits no
    // defeat event, so nobody earns experience for it and nobody is recorded
    // dead: both fall out of the event rather than out of a special case.
    //
    // What it does mean is `on_board`: a departed character occupies no tile,
    // is never chosen to act, and is not a living character of its side. So a
    // battle whose last opponent walks away ends by the same elimination
    // backstop that ends a battle whose last opponent falls, and the player
    // wins it without killing anybody.
    bool departed{false};
    // The round this character enters the board on, and whether it has. Zero
    // and true are the defaults and mean somebody who was here from the
    // opening; on such a board these two fields fold not one byte into the
    // canonical hash.
    //
    // **Not arrived is not absent, and the difference is the point.** An
    // unarrived character holds no tile, is never chosen to act, can be aimed
    // at by nothing, and shades no tile in the danger overlay: it stands
    // nowhere, so it does none of those things. But it *is* still in the
    // battle: a side with a wave still marching has not been beaten, so
    // `defeat_all_opponents` and the elimination backstop count it. That is
    // the one distinction waves exist to make, and it is why "on the board" and
    // "in the battle" became two predicates instead of one.
    //
    // `position` before arrival is the tile the content asked for rather than a
    // tile anybody holds. Where that tile is taken when the round comes, the
    // character takes the nearest one it could stand on instead, and the
    // `unit_arrived` event names whichever it got.
    std::uint32_t arrival_round{};
    bool arrived{true};
    // See `UnitDefinition::endures`. The lower bound of this unit's health, as
    // one bit: false is a floor of zero and true is a floor of one.
    //
    // Canonical. Two otherwise identical battles, one of which nobody can be
    // lost in, are not the same battle: the objectives that end by elimination
    // cannot end the same way and no sequence of commands makes them agree. So
    // it is folded into `canonical_hash`. It travels there as one bit of the
    // character's presence byte, on the standard `talk_record_id` and
    // `arrival_round` already set, so a board on which nobody endures spends
    // nothing on it beyond a bit that stays clear.
    bool endures{false};
    // The kind of the weapon in hand, resolved from the first carried weapon
    // exactly as `power`, the reach band and `accuracy` are. Zero is a bare
    // hand or a weapon of no kind, and gets nothing out of a triangle.
    //
    // Last in the struct, and deliberately: `create_encounter` builds one of
    // these by position, so a field added in the middle would quietly become
    // whatever the next one used to be.
    //
    // Not folded into `canonical_hash`. It is derived from the weapon in hand
    // and the weapons a unit carries are folded already, so folding this too
    // would move every board's hash to say a thing already said.
    ContentId weapon_type{};
};

// Whether a character is still this battle's to lose: alive, and not talked off
// the board.
//
// It is deliberately *not* about tiles: a character marching towards a board it
// has not reached yet answers true. "Does this side still have anybody" and
// "who is standing where" are two questions the moment waves can arrive.
// `defeat_all_opponents` and the elimination backstop read this one.
[[nodiscard]] constexpr bool in_the_battle(const UnitSnapshot& unit) noexcept {
    return unit.health > 0 && !unit.departed;
}

// Whether a character is standing on the board at all: still in the battle, and
// arrived.
//
// **This is the board predicate, and every rule that means "is this character
// there" calls it rather than spelling it out.** It decides who holds a tile,
// who may be chosen to act, who anything may be aimed at, which tiles a walk
// may end on, who may be arranged in the deployment region, and which
// characters the opposing policy considers. Spelling it `health > 0` in any of
// those places lets a departed character be struck and an unarrived one be
// arranged: a character has to be absent from all of them or from none.
//
// It is public for the same reason `floor_of` is. `engine/tactics` proposes
// commands this engine then judges, and a policy carrying its own copy of this
// rule is a policy that proposes strikes the engine refuses.
//
// A character who authors no talk record can never depart and a character who
// authors no arrival is arrived from the opening, so on every board written
// before either this is the same predicate as `in_the_battle`.
[[nodiscard]] constexpr bool on_board(const UnitSnapshot& unit) noexcept {
    return in_the_battle(unit) && unit.arrived;
}

// The lowest health a blow may leave this unit holding.
//
// One function, called by `Encounter::apply` and by `forecast_attack` alike,
// which is the whole of why it exists. The forecast is a promise that apply
// delivers exactly what was shown; a floor computed in one and clamped in the
// other would be two rules, and the day they disagreed a player would be shown a
// killing blow and watch the character stand up.
//
// It is the same shape `attack_damage` and `hit_chance_for` already have, and
// for the same reason: the number on the screen and the number in the rule have
// to come out of one place.
[[nodiscard]] constexpr std::int16_t floor_of(const UnitSnapshot& unit) noexcept {
    return unit.endures ? std::int16_t{1} : std::int16_t{0};
}

// One thing a defeated unit left behind.
//
// **Where a drop goes during the battle, decided here.** It goes nowhere. It is
// recorded in this list and in the `item_dropped` event beside it, and it does
// not enter anybody's pack and does not lie on a tile. Three reasons, in the
// order they weighed:
//
//   * There is no ground-tile inventory in this engine, and inventing one to
//     hold a drop would be a second inventory model, a second set of refusals,
//     and a pickup command nobody asked for.
//   * Putting it into the felling unit's pack would change `item_counts`, which
//     is canonical and is drawn on every client's character sheet and in every
//     console's item menu, a menu that would grow a row mid-battle on five
//     platforms for a thing the player did not choose. It would also make the
//     drop immediately drinkable, and nobody drinks a dead man's tonic
//     mid-fight.
//   * A record is exactly what the consequence needs to be. The campaign layer
//     reads the events at battle end
//     (`campaign_runtime::derive_battle_progression`), which is the layer
//     allowed to see a package and a roster at once. The simulation still
//     learns nothing about campaigns.
//
// So the genre floor is kept in the shape this engine can honestly hold: the
// thing goes to the victor's side rather than onto a tile, and "to the side" is
// spelled as a claim recorded against the unit that made it.
struct DropRecord final {
    // Whose body it came off.
    UnitId unit_id{};
    // Who felled them, and therefore whose side claims it. Never zero: a drop
    // is rolled only where a defeat names its cause.
    UnitId claimant_id{};
    // What fell. A content identity and nothing more; the battle never reads
    // what it does.
    ContentId item_id{};
};

[[nodiscard]] constexpr bool operator==(
    const DropRecord& lhs,
    const DropRecord& rhs
) noexcept {
    return lhs.unit_id == rhs.unit_id && lhs.claimant_id == rhs.claimant_id &&
           lhs.item_id == rhs.item_id;
}

struct EncounterSnapshot final {
    std::uint16_t width{};
    std::uint16_t height{};
    Side active_side{Side::first};
    // The unit part-way through an activation, or 0 when the active side has
    // not committed to one yet. Once set, only that unit may act until its
    // action points are spent.
    //
    // **Always zero under `side_blocks`**, and deliberately so: that order has
    // no exclusive activation to hold, so there is no one unit to name and no
    // one budget to count down. It is not a half-kept field there: it is
    // empty, and the per-character answer lives on `UnitSnapshot`. Under
    // `alternating` and `initiative` both fields mean exactly what they always
    // did.
    UnitId active_unit_id{};
    std::uint8_t remaining_action_points{};
    // Rounds that have *completed*, so the battle opens in the first round with
    // this at zero and the round in progress is always one more than this.
    //
    // **A round is one completed pass through the turn order**, and that one
    // rule is spelled in each order's own vocabulary. Under `side_blocks` and
    // `initiative` a pass closes when every character still on the board has
    // acted. Under `alternating` a pass is one turn for each side, so it closes
    // as the turn comes back round to the side that opened the battle, which is
    // forced rather than chosen: under that order a side's block is one
    // activation.
    //
    // Under an ordered turn order the count is kept always, because it always
    // has been. Under alternating order it is kept **where the content gives it
    // consequence**, where an objective reads it or where a character arrives
    // on a round, and is otherwise the zero it has always been. A number
    // nothing consults is not a rule, and counting it would be inventing state
    // that is hashed, replayed and printed on a console's status line, on every
    // board that never asked for a round. So a board that authors neither runs
    // the same code, folds the same bytes and prints the same line it always
    // did. This is the standard `deployment_tiles` already keeps below: state
    // is held while it is a rule.
    std::uint32_t round{};
    std::uint64_t activation_count{};
    Outcome outcome{Outcome::ongoing};
    // How this battle decides who acts next, exactly as the definition asked.
    //
    // **It is here because it is state and because the hash folds it.** Every
    // advance reads it, it decides which commands the board will accept, and
    // two boards that differ only in it are two different battles that no
    // sequence of commands makes agree. So it is canonical, and a snapshot that
    // left it out could not be told apart from a snapshot of the other battle
    // even with its hash beside it. See `canonical_hash`, which folds it where
    // it is not the default.
    TurnOrder turn_order{TurnOrder::alternating};
    std::vector<UnitSnapshot> units;
    std::vector<ObjectiveResult> objectives;
    // The board's passability, row-major, exactly as the definition gave it:
    // empty for an all-open board, otherwise width x height. Carried in the
    // snapshot because the read-only queries below are the client's only view
    // of the movement rule, and they cannot answer it without knowing the
    // ground.
    std::vector<Terrain> terrain;
    // The board's price, row-major and exactly as the definition gave it: empty
    // for a board where every step costs one, otherwise width x height. Carried
    // beside the passability for the same reason and no weaker a one: a client
    // that knew which cells admit a character but not what they charge would
    // paint a reach the board will not honour.
    std::vector<std::uint8_t> movement_cost;
    // What fell, in the order the defeats that produced it resolved. Canonical
    // state, hashed like a health total: the roll that put an entry here came
    // off the encounter's own dice, so a replay of the same commands finds the
    // same list and a mid-battle save resumes with what has already fallen
    // still fallen. Empty is a battle in which nothing has dropped, which is
    // every battle whose content authors no drop.
    std::vector<DropRecord> drops;
    // The encounter's dice: the seed, and how many numbers each stream has
    // taken. Authoritative state: hashed by `canonical_hash`, and complete, so
    // a save that carries a snapshot can resume mid-battle without the next
    // roll changing.
    core::RandomState random;
    // The region the player arranges their own characters in, exactly as the
    // definition gave it but sorted row-major so every platform lists it the
    // same way. Empty for every encounter that authors no region.
    //
    // Carried in the snapshot for the reason the terrain is: `deployable_tiles`
    // is a client's only view of the deployment rule, and it cannot answer
    // without knowing the region.
    std::vector<Position> deployment_tiles;
    // Which kinds of weapon beat which, and what beating them is worth.
    //
    // Carried in the snapshot for the reason `deployment_tiles` is: it is a
    // client's only view of the rule, and a surface that must say *why* one
    // strike is worth more than another cannot work it out from a number. It
    // is also what `forecast_attack` reads, so the forecast and the blow price
    // an edge from one table rather than two.
    //
    // Empty is a battle with no triangle in it, which is every battle written
    // before one could be drawn.
    std::vector<WeaponTypeDefinition> weapon_types;
    // Whether the deployment phase is open. True from `create_encounter` until
    // a `begin_battle` command closes it, and only ever true for an encounter
    // that authors a region. While it is open the ordinary command vocabulary
    // is refused as `wrong_phase`, no activation has begun, and nothing has
    // drawn from any random stream.
    bool deploying{false};
};

// What a cell costs in `movement_field` when no walk within the allowance
// arrives at it. Distinct from every payable total, because the allowance is a
// byte and every entry costs at least one.
inline constexpr std::uint32_t unreachable_cost = 0xFFFFFFFFU;

// **Where a character can go, once.** For every cell of the board, the least a
// walk from `origin` can pay to stand on it, or `unreachable_cost` for a cell
// no walk within `allowance` arrives at.
//
// This is the single definition of movement, on the terms `can_enter` is the
// single definition of passage: the move rule, both read-only reach queries,
// the danger overlay and `engine/tactics` all consume this and none of them
// walks the board itself. Four flood fills would be four games, and the danger
// overlay is where that would show first: a warning that counted steps while a
// walk counted price would shade tiles the board does not threaten and leave
// threatened tiles bare.
//
// Cheapest-path rather than breadth-first, because reaching a tile *cheaply* is
// the question a priced board asks. Prices come from `snapshot.movement_cost`
// through `entry_cost`, so an unpriced board is a board where every step costs
// one and this answers exactly what a breadth-first fill answers.
//
// The result is a function of the board and not of the traversal: the least
// anything costs is the least whatever order it is found in.
//
// `origin` costs nothing and is always in the result, whatever its terrain and
// even though the moving unit occupies it, because a character is already
// standing there. Callers listing destinations exclude it.
//
// **Who is standing where, decided here and only here, and it is two answers
// rather than one.** A character of `mover`'s own side is walked *through*: an
// ally is somebody to squeeze past rather than a wall, so a company holding a
// line does not shut its own gate. A character of the other side is a wall, and
// is neither entered nor crossed. But no walk may *finish* on anybody, ally
// included, so every tile somebody else is standing on comes back
// `unreachable_cost` however cheaply a path reached it.
//
// That the two answers are one function is the point. "Can pass through" and
// "may finish here" are the sort of pair that becomes two rules in four places
// (the move rule, the two reach queries, the danger overlay and the
// unattended-side policy), and the danger overlay is where the disagreement
// would show: a warning that could not route through the opposition's own line
// would leave threatened tiles bare on exactly the boards where a line is worth
// holding. Everything asks this, so everything gets one answer.
[[nodiscard]] std::vector<std::uint32_t> movement_field(
    const EncounterSnapshot& snapshot,
    Position origin,
    std::uint8_t allowance,
    std::uint8_t crossings,
    Side mover
);

// How far a talk reaches, in tiles. One: the band a bare hand already uses.
//
// Public because a client has to draw it. The tiles a talk covers are the tiles
// every action menu already knows how to highlight, which is half the reason
// adjacency was chosen over an authored band. The other half is that an
// authored band is a number every talkable character would have to state to get
// the answer this constant gives for free.
inline constexpr std::uint32_t talk_reach = 1;

enum class CommandType : std::uint8_t {
    move = 0,
    attack,
    wait,
    ability,
    // Spends one of a carried item on `target_id`, or on the acting unit when
    // that is zero. Costs an action point and closes the activation exactly as
    // a strike or a cast does, and draws nothing from any random stream.
    use_item,
    // Stands one deployable character on one tile of the encounter's deployment
    // region, before the first activation. Costs no action point, ends no
    // activation, advances no activation count, and may be issued any number of
    // times over any deployable character in any order until the phase closes.
    // Draws nothing from any random stream.
    deploy,
    // Closes the deployment phase. The only way it closes: the engine never
    // decides the arrangement is finished, because every character is already
    // standing somewhere and there is no moment it could detect as done.
    // `unit_id` is ignored: this is a statement about the battle, not about a
    // character.
    begin_battle,
    // Talks to the adjacent character `target_id` names, whose placement
    // authored a talk record. Costs an action point and closes the activation
    // exactly as a strike does, cannot be countered, and draws nothing from any
    // random stream, on the `use_item` shape and for the same reasons.
    //
    // What it does is take the talked-to character off the board, alive. See
    // `UnitSnapshot::departed`; the short version is that leaving and dying are
    // two different facts and nothing here confuses them.
    talk,
};

struct Command final {
    CommandType type{CommandType::wait};
    UnitId unit_id{};
    Position destination{};
    UnitId target_id{};
    ContentId ability_id{};
    // Which carried weapon an attack uses. Zero means the weapon in hand, so a
    // command written before weapons could be chosen means what it always did.
    ContentId weapon_id{};
    // Which carried item a use_item command spends. There is no zero default
    // here the way there is for a weapon: nothing is "in hand" among what a
    // character carries in its pack, so a use naming nothing is `unknown_item`.
    ContentId item_id{};
};

enum class CommandError : std::uint8_t {
    none = 0,
    encounter_complete,
    unknown_unit,
    defeated_unit,
    wrong_side,
    invalid_command,
    invalid_destination,
    occupied_destination,
    unknown_target,
    target_defeated,
    friendly_target,
    target_out_of_range,
    unknown_ability,
    unavailable_ability,
    // Another unit on this side is part-way through its activation.
    //
    // **Unreachable under `side_blocks`**, where turns interleave freely and
    // starting one character locks out nobody. It is the rule of `alternating`
    // and `initiative`, where a side's turn *is* one activation.
    activation_in_progress,
    // The unit has no action points left in its own turn.
    no_action_points,
    // The attack named a weapon this encounter does not define.
    unknown_weapon,
    // The attack named a weapon the encounter defines but this unit does not
    // carry.
    unavailable_weapon,
    // The use named an item this encounter does not define, or named nothing.
    unknown_item,
    // The use named an item the encounter defines but this unit does not carry.
    unavailable_item,
    // The unit carries the item and has none of it left.
    depleted_item,
    // The item is carried and in stock, and authors no effect a battle can
    // apply. A key is not a refusal to find later; it is a refusal now.
    unusable_item,
    // The command belongs to the other phase of the battle: an ordinary
    // command while the deployment phase is open, or a `deploy` or
    // `begin_battle` once it has closed, or on an encounter that never had one.
    // Deliberately one refusal for one fact rather than two a client would have
    // to learn separately.
    wrong_phase,
    // A first-side character the content authored outside the deployment
    // region. It stands where it was put, exactly as every character does on a
    // board with no region at all.
    undeployable_unit,
    // The destination is on the board and is not one of the region's tiles.
    // Distinct from `invalid_destination`, which is off the board entirely: one
    // is a tile that does not exist, the other a tile the author did not offer.
    outside_zone,
    // The talk named a character standing on the board, alive, whose placement
    // authors no talk record. A refusal now rather than a silence: a client
    // that offered the row would have been wrong, and one that showed nothing
    // when the row was taken would look broken.
    not_talkable,
    // The command named a character who has already been talked off the board.
    //
    // Deliberately its own refusal rather than `target_defeated`. Somebody who
    // walked away is not somebody who died, and this vocabulary is the place a
    // client learns the difference. Folding the two would undo, at the very
    // last surface, the distinction every rule underneath it keeps. It answers
    // a strike and a cast as well as a talk: a departed character cannot be
    // hit either.
    target_departed,
    // The command named a character who has not arrived on the board yet.
    //
    // Its own refusal for the reason `target_departed` is: somebody who has not
    // come yet is not somebody who has gone, and this vocabulary is where a
    // client learns the difference. It answers a strike, a cast, an item and a
    // talk alike, and it is what a client is told rather than `unknown_target`,
    // because the character is very much part of this battle.
    target_unarrived,
    // The command asked a character who has not arrived on the board yet to
    // act. The actor-side sibling of `target_unarrived`, on the standard
    // `defeated_unit` and `target_defeated` already set: a client naming the
    // wrong character is told which of the two mistakes it made.
    unarrived_unit,
    // The command asked a character who has already taken its turn this round
    // to take another one.
    //
    // Its own refusal rather than `activation_in_progress`, which states the
    // quite different fact that *somebody else* is part-way through a turn.
    // Under `side_blocks` the engine names no actor, so this is the refusal a
    // player meets when they pick a character they have already spent, and
    // telling them "another unit is acting" when none is would be a lie in the
    // one place the rule needs to be legible. Inert under `alternating`, where
    // nobody is ever marked as having acted.
    already_acted,
    // The command asked for a second move in one turn.
    //
    // A unit walks once per turn however many action points it has; the rest
    // are for acting. Move-then-strike is the two-point turn `action_points`
    // describes and is untouched. Move-then-move is what this refuses, and it
    // is refused by name rather than by a silent nothing, because a player
    // pressing move on a character who has already walked has to be told why
    // the board will not take it.
    //
    // A character authored to keep acting after it strikes may still walk
    // afterwards if it has a point left and has not walked yet: this is a
    // budget for walking, not a rule about the order things happen in.
    already_moved,
    // The command asked a character who has been talked off the board to act.
    //
    // The actor-side sibling of `target_departed`, and its own refusal for the
    // same reason: somebody who walked away is not somebody who died, so
    // `defeated_unit` would be a lie, and `unknown_unit` would be one too
    // because the character is very much part of this battle's story. Under
    // `initiative` and `side_blocks` the engine picks the actor and never picks
    // a departed one; under `alternating` the caller picks, and this is what it
    // is told. Without it a character talked off the board could walk, strike,
    // be countered and be buried, undoing at the one surface a client drives
    // the whole distinction `UnitSnapshot::departed` exists to keep.
    departed_unit,
};

[[nodiscard]] std::string_view error_name(CommandError error) noexcept;

enum class EventType : std::uint8_t {
    unit_moved = 0,
    unit_waited,
    unit_damaged,
    unit_defeated,
    encounter_completed,
    unit_restored,
    // Emitted when a unit's activation ends, carrying the points it had left.
    activation_ended,
    // A strike that was legal, was resolved, and did not land. `unit_id` is
    // whoever was swung at, `related_unit_id` whoever swung, and `amount` is
    // zero because nothing was taken. It exists so that a miss is something a
    // client is told about rather than the absence of a damage event: a
    // surface that says nothing when an attack misses is a surface that looks
    // broken.
    attack_missed,
    // One of a carried item was spent. `unit_id` is who spent it,
    // `related_unit_id` who it was spent on (always the same unit, and a
    // separate field so widening the reach later does not re-mean anything),
    // `content_id` names the item, and `amount` is how many of it that unit
    // still carries afterwards. Exactly one is consumed per event, which is
    // what makes a battle's inventory consequence derivable from its events
    // alone: one consume of `content_id` by `unit_id`, per event, in order.
    // The restoring half is reported separately, by the `unit_restored` event
    // a restoring item emits after this one, so a client that already draws
    // healing draws this one too without learning a new rule.
    item_used,
    // A defeated unit left something behind. `unit_id` is who fell,
    // `related_unit_id` who felled them and therefore whose side claims it,
    // `content_id` names what fell, and `amount` is one: exactly one thing
    // falls per event, which is what makes a battle's gained inventory
    // derivable from its events alone, one `add_item` of `content_id` per
    // event, in order. `position` is where the body was, reported for the same
    // reason `unit_defeated` reports it: a surface wants somewhere to put the
    // notice. It is not a tile the thing lies on. See `DropRecord`.
    //
    // Emitted immediately after the `unit_defeated` event that caused it, so a
    // client reading events in order is told who fell before it is told what
    // they left.
    item_dropped,
    // A character was stood on a deployment tile. `unit_id` is who, `position`
    // is where. Emitted once per accepted `deploy`, so a surface narrating the
    // phase narrates from events exactly as it narrates every other rule rather
    // than by diffing snapshots.
    unit_deployed,
    // The deployment phase closed. Emitted once, on the accepted
    // `begin_battle`, and never on an encounter that had no phase.
    deployment_ended,
    // A character was talked off the board. `unit_id` is who was talked to and
    // has now departed, `related_unit_id` who talked to them, `position` is
    // where they were standing when they left, and `content_id` is the talk
    // record their placement authored.
    //
    // That last field is the whole reason a campaign can read this. It is the
    // `item_used` precedent applied to the same problem: a battle-local unit
    // identifier is derived from the encounter and is a different number on
    // every appearance, so the only identity stable enough to cross the battle
    // boundary is the one the author wrote down. Exactly one talk per event, in
    // order, which is what makes a battle's story consequence derivable from
    // its events alone.
    //
    // Emitted *instead of* `unit_defeated`, never beside it. A departure is not
    // a defeat, and the two events are what every layer above reads to tell
    // them apart.
    unit_talked,
    // An authored character entered the board as a round began. `unit_id` is
    // who, `position` is the tile it actually took, either the tile the content
    // asked for or the nearest one it could stand on when that tile was held,
    // and `amount` is the round in progress, one based, so a client narrating
    // "the third wave" narrates from the event rather than by counting
    // snapshots.
    //
    // Emitted only by an encounter that authors a wave, so no board written
    // before waves emits one and no client that ignores it is wrong.
    unit_arrived,
    // A blow that would have felled a unit was caught by that unit's health
    // floor. `unit_id` is who is still standing, `related_unit_id` is who
    // struck: the same two identities, the same way round, as the
    // `unit_damaged` event this one follows.
    //
    // Emitted immediately *after* that damage event, so a client reading events
    // in order is told what was taken before it is told that it was not enough.
    // Emitted only where the floor actually caught something: a blow that leaves
    // a unit that endures with health to spare emits nothing here.
    //
    // It exists so that a surface is told rather than left to work it out. A
    // client holding the previous health and the amount could subtract and
    // notice the two do not add up, and that is exactly the derivation this
    // engine refuses to make anybody do. A miss is an event rather than the
    // absence of damage, a departure is an event rather than a health total
    // that did not move, and holding on is an event for the same reason.
    unit_endured,
};

struct Event final {
    EventType type{};
    UnitId unit_id{};
    UnitId related_unit_id{};
    Position position{};
    std::int16_t amount{};
    Outcome outcome{Outcome::ongoing};
    // The content identity this event is about, when it is about one at all.
    // Zero for every event that names no definition, which is every event
    // written before an item could be spent. Appended rather than folded into
    // `related_unit_id`: a unit identifier and a content identifier are two
    // different namespaces and sharing a field would make them one.
    ContentId content_id{};
};

struct CommandResult final {
    CommandError error{CommandError::none};
    std::vector<Event> events;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CommandError::none;
    }
};

// What one attack would do, before it is committed. `error` is the refusal
// `Encounter::apply` would return for the same attack in the same state; when
// it is `none`, the numbers are what apply would inflict. Forecasting never
// changes state, so a client may show this freely while the player decides.
//
// While the deployment phase is open that refusal is `wrong_phase`, and it is
// the whole answer: no numbers, because there is no attack to price yet. That
// is the forecast contract kept rather than bent: a client shown a number
// during deployment would be shown a promise nothing was going to deliver.
//
// What this promises, exactly, now that an attack can miss: **a chance and the
// numbers behind it**. `hit_chance` is the very number `apply` rolls against,
// not a rounded or averaged one, and every other field says what happens when
// that roll lands. On a miss nothing at all is taken. A client
// that shows the numbers without the chance is showing a promise the engine no
// longer makes; a client that shows a chance it computed itself is showing a
// second rule. Both are why the chance is here.
//
// The second half is the counterattack, and it is here because a forecast that
// priced only the outgoing half would be a broken promise: apply resolves both
// halves under one command, so both belong to the same prediction.
// Which way a pair of weapons leans, for a surface that wants to say so.
//
// The triangle moves both numbers a forecast already reports, and moves them
// silently: a bow priced against a blade shows a smaller blow and a worse
// chance than the same bow against a staff, and nothing on screen says why.
// A player who is not told is left to infer a rule from two numbers they
// cannot compare side by side, which is the one thing the triangle needs them
// to learn.
//
// It is a lean and not a pair of deltas because the deltas are already spent:
// `damage` and `hit_chance` are the folded numbers, so repeating what went
// into them would invite a surface to add them a second time. What a surface
// cannot work out for itself is the *direction*, and that is what this is.
//
// Read from the numbers the advantage actually carried rather than from the
// edge alone. An edge authored at nothing changes no forecast, and an arrow
// over a strike that is priced exactly as it would be without one says the
// rule is doing something it is not.
enum class WeaponLean : std::uint8_t {
    none = 0,
    advantage = 1,
    disadvantage = 2,
};

struct AttackForecast final {
    CommandError error{CommandError::none};
    // How often this strike lands, as a percentage in [0, 100]: the accuracy
    // of the weapon being swung with both units folded into it, plus the
    // attacker's skill and luck, minus the target's evasion and luck, clamped.
    // It is exactly the chance `Encounter::apply` rolls against on the
    // `core::RandomStream::hit` stream. It is the *folded* number and never the
    // weapon's authored one, because there is only ever one chance and this is
    // it. One hundred means certain, and a certain strike draws no number at
    // all.
    std::uint8_t hit_chance{100};
    // What the target loses **when the strike lands**. Zero is taken on a miss.
    std::int16_t damage{};
    std::int16_t target_health_after{};
    bool lethal{};
    // Whether the target answers, given it is still standing when the attack
    // resolves: the separation falls inside its own reach band, and it either
    // survives the blow or the blow can miss. A certain lethal strike is
    // therefore still answered by nobody.
    bool counter{};
    // How often that answer lands, on the same terms as `hit_chance`: the
    // accuracy of the weapon the target has in hand, folded the other way
    // round: the defender's skill and luck add, the attacker's evasion and
    // luck subtract. Meaningless when `counter` is false, and left at one
    // hundred there.
    std::uint8_t counter_chance{100};
    // What the counter strikes for when it lands, zero when none comes. This
    // is the raw number the formula produces, exactly as `damage` above is: a
    // counter that overkills reports the overkill, and `attacker_health_after`
    // is where the clamp shows.
    std::int16_t counter_damage{};
    // The attacker's health once the exchange is over and the counter landed.
    // Equal to its current health when no counter comes, so a client may show
    // it either way.
    std::int16_t attacker_health_after{};
    // Whether the counter fells the attacker when it lands.
    bool counter_lethal{};
    // Which way the weapon in the attacker's hand leans against the weapon in
    // the target's, already folded into `damage` and `hit_chance` above.
    WeaponLean lean{WeaponLean::none};
    // The same for the answering blow, which is a different pairing and not
    // merely this one reversed: a target answers with whatever it is holding,
    // which need not be the weapon the attack was made with. Meaningless when
    // `counter` is false, and left at `none` there.
    WeaponLean counter_lean{WeaponLean::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CommandError::none;
    }
};

[[nodiscard]] AttackForecast forecast_attack(
    const EncounterSnapshot& snapshot,
    UnitId attacker_id,
    UnitId target_id
) noexcept;

// What one cast would do to one character it covers, before it is committed.
//
// **The gesture the forecast family had no answer for.** A strike is priced, an
// item is priced, a talk is promised, and a cast -- the one gesture that can
// touch several characters at once and the one whose numbers a player is least
// able to work out in their head -- showed nothing at all. A client aiming a
// blast could light the tiles it covers and say nothing about what standing in
// it costs.
//
// **One character per call, and that is the shape rather than a shortcut.** A
// cast names a tile and the area decides who is caught, so a forecast carrying
// every caught character would carry a list, and a list is an allocation on
// every move of a cursor on a machine that counts its heap in kilobytes. It
// would also be inexact where it mattered: hit chance is priced against the
// struck character's own evasion and luck, and damage against its own defence
// or resistance, so two characters under one blast are two different numbers
// and any single summary of them is a number the engine will not deliver. A
// caller wanting the whole picture walks `area_tiles` and asks about each
// occupant, which is the same walk it makes to draw them.
//
// `error` is the refusal `Encounter::apply` would give the same cast in the
// same state, asked in apply's order, and it is about the *cast* rather than
// about `affected_id`: a cast aimed at ground the caster cannot reach is
// refused, and a character the area misses is not a refusal at all. That is
// what `covered` says. `spared` is the other half of the same distinction: a
// damaging cast covers the caster's own side and takes nothing from them, so an
// ally under the blast is covered, spared, and costs nothing -- which is
// exactly what the rule delivers and what a client must be able to show rather
// than guess.
//
// A character this encounter does not carry, or one not `on_board`, is
// uncovered: the cast lands, and it lands on somebody who is not there.
struct AbilityForecast final {
    CommandError error{CommandError::none};
    AbilityKind kind{AbilityKind::damage};
    // Whether the area covers this character at all. Everything below is zero
    // when it does not.
    bool covered{false};
    // Whether the cast passes over this character without touching them: a
    // damaging cast and somebody on the caster's own side, the caster included.
    // A restoring cast spares nobody, because mercy asks no side.
    bool spared{false};
    // How often this cast lands on *this* character, as a percentage in
    // [0, 100]: the ability's accuracy with both characters folded into it,
    // exactly as `Encounter::apply` folds it. The weapon triangle does not
    // price a cast, so no advantage enters here. Meaningless for a restoring
    // cast, which never rolls, and left at a hundred there.
    std::uint8_t hit_chance{100};
    // What this character loses when the cast lands. Zero on a miss, on a
    // spared ally, and on a restoring cast.
    std::int16_t damage{};
    // What this character gains from a restoring cast, clamped to the health
    // they are missing, so a full-health character forecasts zero. Zero for a
    // damaging cast.
    std::int16_t restored{};
    // This character's health once the cast has resolved and landed. Equal to
    // their current health where nothing reaches them, so a client may show it
    // either way.
    std::int16_t target_health_after{};
    // Whether the cast fells this character when it lands. False for anybody a
    // health floor catches, exactly as `forecast_attack` reports it.
    bool lethal{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CommandError::none;
    }
};

[[nodiscard]] AbilityForecast forecast_ability(
    const EncounterSnapshot& snapshot,
    UnitId caster_id,
    ContentId ability_id,
    Position centre,
    UnitId affected_id,
    const std::vector<AbilityDefinition>& abilities
) noexcept;

// The same forecast for a named carried weapon. A zero `weapon_id` means the
// weapon in hand and is exactly the overload above. Otherwise the weapon is
// resolved against `weapons` and refused the way `Encounter::apply` would
// refuse it, before the target is examined.
[[nodiscard]] AttackForecast forecast_attack(
    const EncounterSnapshot& snapshot,
    UnitId attacker_id,
    UnitId target_id,
    const std::vector<WeaponDefinition>& weapons,
    ContentId weapon_id
) noexcept;

// What spending one carried item would do, before it is committed. The same
// promise `AttackForecast` makes, and a shorter one to keep: an item draws
// nothing from any random stream, so there is no chance to show. `restored` is
// the exact number `Encounter::apply` will deliver, not an average and not the
// authored power before the clamp, and `remaining_after` is what the unit will
// still be carrying. `error` is the refusal apply would return for the same use
// in the same state.
struct ItemForecast final {
    CommandError error{CommandError::none};
    ItemKind kind{ItemKind::none};
    // Health the target gains. Clamped to the health it is missing, so a
    // full-health character forecasts zero. Spending the item anyway spends it
    // for zero, which is what the number is there to prevent.
    std::int16_t restored{};
    std::int16_t target_health_after{};
    // How many of this item the user holds once the use is done.
    std::uint16_t remaining_after{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CommandError::none;
    }
};

// What one talk would do, before it is committed.
//
// The promise this keeps is the easiest one in the engine to keep, and that is
// by design: **a talk that is accepted cannot fail.** Everything a talk could
// have gone wrong at is decided here and refused before anything is committed:
// being asked of nobody, of somebody dead, of somebody who has already walked
// away, of somebody who has not arrived yet, or being aimed at nobody, at
// somebody dead, at somebody already gone, at somebody still to come, at
// somebody with nothing to say, at somebody too far away. What is left is one
// consequence with no number to roll and no clamp to apply, so `error` being
// `none` is the entire forecast: this character will leave the board and that
// talk record will be what the battle reports.
//
// The claim is exact only while this query takes **every** refusal `apply`
// takes, in `apply`'s order. It is not a rule of thumb about which refusals
// matter: a forecast missing one is a forecast that says a talk will land and
// then watches the engine refuse it.
struct TalkForecast final {
    CommandError error{CommandError::none};
    // Who would leave, and what leaving would record. Both zero when `error` is
    // anything but `none`, because a refused talk describes no consequence.
    UnitId departing_id{};
    ContentId record_id{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CommandError::none;
    }
};

[[nodiscard]] TalkForecast forecast_talk(
    const EncounterSnapshot& snapshot,
    UnitId unit_id,
    UnitId target_id
) noexcept;

// `target_id` zero means the acting unit, exactly as the command means it.
[[nodiscard]] ItemForecast forecast_item(
    const EncounterSnapshot& snapshot,
    UnitId unit_id,
    UnitId target_id,
    const std::vector<ItemDefinition>& items,
    ContentId item_id
) noexcept;

// What this character has left of its own turn, in action points.
//
// The rule `apply` and the forecasts use, exposed read-only so no client works
// it out for itself: the side-wide budget while this character is the one
// holding an activation, none once it has finished its turn, and its own budget
// less what its own turn has spent otherwise. Under `alternating` and
// `initiative` only one character is ever part-way through a turn, so the first
// clause is the answer; under `side_blocks` nobody holds an activation and the
// last clause is the whole of it, because several characters can each be
// part-way through a turn at once and one side-wide number cannot describe
// them. Zero for a character this encounter does not carry.
[[nodiscard]] std::uint8_t action_points_left(
    const EncounterSnapshot& snapshot,
    UnitId unit_id
) noexcept;

// Every tile the unit could occupy after one accepted move command: the
// unoccupied tiles `movement_field` prices within the unit's movement
// allowance, entering only terrain the unit may cross and paying what each cell
// it enters charges. This is the same traversal `Encounter::apply` judges a
// move against, exposed read-only so no client re-implements it. A tile is in
// the result exactly when a move command to it would not be refused; a unit
// this encounter does not carry, or one that is not `on_board` (defeated,
// talked off the board, or not arrived yet), reaches nothing, because every
// move command it could make is refused by name. Row-major order (by y, then
// x), so every platform lists the same tiles the same way.
//
// Nothing is reachable while the deployment phase is open, because every move
// command is refused for its phase. Answering the move rule anyway would draw a
// range no player could use; answering the deploy rule under this name would be
// one query with two rules behind it. `deployable_tiles` is the other rule.
[[nodiscard]] std::vector<Position> reachable_tiles(
    const EncounterSnapshot& snapshot,
    UnitId unit_id
);

// Every tile the named character could be deployed to, on exactly the terms
// `reachable_tiles` answers the move rule on: a tile is in the result precisely
// when a `deploy` command naming it would be accepted, so no client decides for
// itself which tiles are lit.
//
// Empty when the phase is closed, and empty for a character that is not
// deployable: unknown, on the other side, authored outside the region, or not
// `on_board`. The tile a character is already
// standing on is in the result, because standing where you already stand is
// accepted. Row-major order, like every other tile list this engine returns.

// Whether the player arranges this character before the first activation.
//
// **Derived rather than stored, and it is exact.** A first-side character who
// is `on_board` and standing on one of the region's tiles is arrangeable;
// everybody else stands where the content put them. That reading is stable for
// the whole phase because the rule keeps it so: a character outside the region
// can never be deployed into it, and one inside it can only be deployed to
// another tile of it. So "authored inside the region" and "standing inside the
// region" are the same set at every moment the phase is open, and the snapshot
// needs no field to remember which.
//
// **A wave is not arrangeable**, and that is `on_board` doing the work rather
// than a clause of its own. An unarrived character's `position` is the tile the
// content asked for and not a tile anybody holds, so arranging one would
// rewrite an authored landing. And because occupancy is `on_board` too, the
// tile it was authored on stays free, so a player could then stand somebody
// else on the tile they had just moved the wave to and put two characters on
// one square.
//
// False for everybody once the phase closes, and for everybody on a board that
// authors no region.
[[nodiscard]] bool is_deployable(
    const EncounterSnapshot& snapshot,
    const UnitSnapshot& unit
) noexcept;

[[nodiscard]] std::vector<Position> deployable_tiles(
    const EncounterSnapshot& snapshot,
    UnitId unit_id
);

// Every tile a unit on `side` could strike in its coming activation: a tile
// whose distance from any stance that unit can take, with the points it will
// have, falls inside its weapon band. This is the Fire Emblem danger zone,
// budgeted by the rules this engine actually applies.
//
// The budget is the whole of the rule, and it is exact rather than an estimate:
//
//   * A unit that has already acted this round contributes nothing. It will act
//     again next round, so the zone answers "before the turn comes back to
//     you", not "ever".
//   * The unit part-way through an activation is measured by the points it has
//     left, not by the points it started with.
//   * A unit with one point may move or strike, not both, so it threatens only
//     the band around where it stands. With two it may move once and strike,
//     which is the classic zone. With three it may walk twice and strike, and
//     the zone says so.
//   * Movement enters only terrain the unit may cross, so a river narrows the
//     zone without anything else being said about it. It pays what the ground
//     charges, from the same `movement_field` a walk is judged against, so a
//     marsh narrows it too. A warning counting steps while a walk counted
//     price would be a board lying about danger in both directions at once.
//   * And it walks through its own line, because a walk does. A side's units do
//     not plug the gaps in their own front, so an opponent standing behind two
//     of its fellows still threatens what it could step out and reach. This is
//     the same `movement_field`, asked with that unit's own side, which is the
//     only reason the zone cannot come to a different answer than the walk it is
//     warning about. It makes the zone larger on any board where a line has
//     depth, and that is the truth growing rather than the warning getting
//     careless.
//
// The honest edge: under alternating turn order no unit is ever marked as
// having acted, because a side's turn is one activation by whichever unit its
// player picks. The zone is then the union over the side's living units, any
// one of whom could be the one who acts. That is the truthful answer to "who
// could reach me before I act again", not a claim that all of them will.
//
// Neither a second carried weapon nor an ability is counted here; for those,
// use the overload below. Row-major order, deduplicated.
[[nodiscard]] std::vector<Position> danger_tiles(
    const EncounterSnapshot& snapshot,
    Side side
);

// The same warning, told everything the units can do: the band of every weapon
// a unit carries and the band of every damaging ability it knows, unioned over
// the same stances and budgeted by the same rule. A restoring ability contributes nothing, because it is not
// a danger. A unit carrying no weapon still contributes its own band, and an
// identity neither registry resolves is skipped rather than guessed at. The
// registry-free overload is kept so a caller with nothing to resolve against
// is never handed a silently different answer.
[[nodiscard]] std::vector<Position> danger_tiles(
    const EncounterSnapshot& snapshot,
    Side side,
    const std::vector<WeaponDefinition>& weapons,
    const std::vector<AbilityDefinition>& abilities
);

// The same warning, asked only about the tiles a caller already has in hand.
//
// **This is the shape a board being played actually needs.** A client lights
// where a character may walk and shades which of those tiles would be dangerous
// to stand on. That is the reach set: a couple of dozen tiles. Asked through
// the overload above it computed a warning about every cell of the board, from
// every character on the opposing side, and kept two dozen of it.
//
// The answer is exactly the overload above narrowed to `among`, and it is
// reached by not doing the work rather than by doing less of it. No cell costs
// less than one to enter, so a stance a character can afford is at most its
// allowance away in a straight line and a strike from there reaches at most its
// widest band further; a character with no tile of `among` inside allowance plus
// band cannot threaten one, whatever the ground does, and is skipped before its
// movement search. On a board where the opposition is spread out, that is most
// of them.
//
// `among` is the caller's own set and needs no order or uniqueness. An empty one
// answers empty, and a tile off the board is ignored rather than refused: this
// is a filter, not a gate. The result is row-major, like every other tile list
// this engine returns.
[[nodiscard]] std::vector<Position> danger_tiles(
    const EncounterSnapshot& snapshot,
    Side side,
    const std::vector<WeaponDefinition>& weapons,
    const std::vector<AbilityDefinition>& abilities,
    const std::vector<Position>& among
);

// Which gesture a character took out of its menu and is now pointing at the
// board.
//
// There are four because there are four things a client hands the player the
// cursor back for. Using an item is not among them: an item reaches the hand
// that holds it, so a use is committed from the row rather than aimed, and a
// fifth value whose only answer would be "the tile you are standing on" would
// be a lit tile nobody is being asked to choose.
enum class Gesture : std::uint8_t {
    // Where this character will walk. Its landing tiles are its movement
    // range. Walking is named here rather than left to the caller because a
    // client that had to special-case one of its four aims would be deciding
    // for itself which aims the engine answers.
    walk = 0,
    // A strike, at whoever is standing on the tile.
    strike = 1,
    // A cast, at the tile itself, occupied or not.
    cast = 2,
    // A talk, at the neighbour standing on the tile.
    talk = 3,
};

// A gesture with whatever identity the player picked it by. This is a client's
// own aiming state said in the engine's words: a client holds exactly these
// three values between choosing a menu row and pressing confirm, so it hands
// them over instead of turning them into a rule of its own.
struct AimedGesture final {
    Gesture kind{Gesture::walk};
    // Which weapon a strike drew. Zero is the weapon in hand, exactly as an
    // attack command means it. Read only for `strike`.
    ContentId weapon_id{};
    // Which ability a cast named. Read only for `cast`.
    ContentId ability_id{};
};

// Whether this character could make that gesture at all right now, whatever it
// were aimed at. This is what decides whether a menu offers the row.
//
// **The line it draws is between the gesture and its aim, and it is the whole
// point of the query.** False means every command carrying this gesture is
// refused before the engine looks at what it named: a character who has already
// walked this turn, one whose turn is over, one on the side that is not acting,
// a weapon it is not carrying, an ability it does not know. True means the
// gesture itself is accepted and only the aim is left to judge. So a strike
// with nobody in reach is *available* and lights no tile, and those are two
// different facts a player is told two different ways. A menu that dropped the
// row would be teaching that rows come and go for reasons the board does not
// show; the aiming highlight is what says "nobody, from here".
//
// So: `aimable_tiles` is empty whenever this is false, and may also be empty
// when it is true. Nothing may be read backwards from an empty tile list.
[[nodiscard]] bool gesture_available(
    const EncounterSnapshot& snapshot,
    UnitId unit_id,
    const AimedGesture& gesture,
    const std::vector<WeaponDefinition>& weapons,
    const std::vector<AbilityDefinition>& abilities
) noexcept;

// Every tile this character could aim that gesture at: a tile is in the result
// exactly when the command committing the gesture there would be accepted, so
// no client decides for itself what its own pick can reach. Row-major order,
// like every other tile list this engine returns.
//
// This is the aiming counterpart of `reachable_tiles`, and it is one query
// rather than four because a client holds one aim. The four gestures differ in
// where the band comes from and in who may be standing in it, but they share
// the sentence above, and a client that had to pick the query would be holding
// the one piece of the rule the engine had not been asked for.
//
// Everything that refuses the gesture before it is aimed at anything empties
// the result: a finished or unbegun battle, an unknown, fallen, unarrived or
// departed character, one on the side that is not acting, one whose turn is
// over, one locked out by somebody else's open activation, and one with no
// point left to spend. A character who cannot act lights nothing, which is the
// answer a player most needs and the one no forecast panel can give without
// being pointed at a tile first.
//
// Per gesture, and each is the rule `apply` applies rather than a restatement
// of it:
//
//   * `walk` answers `reachable_tiles`, and refuses for the same second reason
//     it does: a character who has already walked this turn lands nowhere.
//   * `strike` resolves the drawn weapon exactly as an attack command resolves
//     it, widened by the striker's own reach bonus, and lights the tile of
//     every opposing character standing on the board inside that band.
//   * `cast` resolves the named ability, refuses one this character does not
//     know, and lights every in-bounds tile in its band whether or not
//     anybody is standing there. **The band, not the splash**; see
//     `area_tiles`.
//   * `talk` lights the tile of every neighbour with something to say,
//     whichever side they are on, because the talk rule does not ask.
//
// The registries are the ones the encounter was created with. There is no
// registry-free overload and none is wanted: an identity these cannot resolve
// empties the result, which is not a silently different answer but the true
// one, because the command naming it would be refused as unknown too. A strike
// that names no weapon needs no registry at all.
[[nodiscard]] std::vector<Position> aimable_tiles(
    const EncounterSnapshot& snapshot,
    UnitId unit_id,
    const AimedGesture& gesture,
    const std::vector<WeaponDefinition>& weapons,
    const std::vector<AbilityDefinition>& abilities
);

// Every tile an area cast aimed at `centre` would cover: the same membership
// test `apply` walks the units against, asked of the board instead. Row-major,
// clipped to the board, and it includes `centre` itself.
//
// Separate from `aimable_tiles` because it answers a different question about a
// different subject. `aimable_tiles` describes the character and changes only
// when the character does; this describes one candidate tile and changes every
// time the cursor moves. Folding them together would be one query with two
// rules behind it, and would make the cheap answer pay for the moving one.
//
// Empty for an ability no registry resolves and empty for a single-tile
// ability, because a splash of one tile is the tile the cursor is already on
// and drawing it would be the cursor drawn twice. Nothing here asks whether the
// cast may be aimed at `centre`; that is `aimable_tiles`'s question, and a
// caller drawing a splash outside the band would be drawing a cast the engine
// will refuse.
[[nodiscard]] std::vector<Position> area_tiles(
    const EncounterSnapshot& snapshot,
    ContentId ability_id,
    Position centre,
    const std::vector<AbilityDefinition>& abilities
);

// The sixty-four bits that name this battle: every byte of canonical state,
// folded in a fixed order, on every platform alike.
//
// **A function of the snapshot and of nothing else.** That is the property the
// conformance ROM rests on, and it is why this is a free function over
// `EncounterSnapshot` rather than a method reading a member nobody else can
// see: a snapshot and its hash travel together, and state the hash reads that
// the snapshot does not carry is a battle two snapshots cannot tell apart.
// `Encounter::canonical_hash` is this, over its own state.
//
// What is folded and what is deliberately not is argued field by field at the
// definition, and `tests/simulation/canonical_hash_test.cpp` holds every field
// of every structure a battle is made of to one of those two verdicts: the
// snapshot and the definition, of the battle and of a character, and the small
// records their lists hold. A field added to any of them and left out of both
// stops the build rather than quietly narrowing what a hash can tell apart.
[[nodiscard]] std::uint64_t canonical_hash(const EncounterSnapshot& state
) noexcept;

class Encounter final {
public:
    struct CreateResult;

    [[nodiscard]] CommandResult apply(const Command& command);
    [[nodiscard]] EncounterSnapshot snapshot() const;
    [[nodiscard]] std::uint64_t canonical_hash() const noexcept;
    // The abilities this encounter was created with, in the order they were
    // declared. Exposed read-only because a snapshot names only the ability
    // identities a unit knows, and anything choosing a cast (a client's menu,
    // the unattended-side policy) needs the definitions behind them.
    [[nodiscard]] const std::vector<AbilityDefinition>& abilities(
    ) const noexcept {
        return abilities_;
    }
    // The weapons this encounter was created with, in declaration order, for
    // the same reason: a snapshot names only the identities a unit carries,
    // and anything offering or pricing a strike needs the numbers behind them.
    [[nodiscard]] const std::vector<WeaponDefinition>& weapons(
    ) const noexcept {
        return weapons_;
    }
    // Which kinds of weapon beat which, and what beating them is worth, for
    // the same reason again: pricing a strike needs to know what the two hands
    // are holding and what that is worth, and a snapshot names only a kind.
    [[nodiscard]] const std::vector<WeaponTypeDefinition>& weapon_types(
    ) const noexcept {
        return state_.weapon_types;
    }
    // The items this encounter was created with, in declaration order, for the
    // same reason again: a snapshot names the identities a unit carries and how
    // many are left, and anything offering or pricing a use needs the numbers
    // behind them.
    [[nodiscard]] const std::vector<ItemDefinition>& items() const noexcept {
        return items_;
    }
    // And the objectives, in identifier order, for the same reason a third
    // time: a snapshot carries only `ObjectiveResult{id, state}`, and a client
    // that must draw "round 3 of 7" needs the definition behind the identity.
    // Read-only and out of the snapshot on purpose: the count an author wrote
    // is content, not battle state, and the simulation/presentation boundary
    // holds by the snapshot carrying neither.
    [[nodiscard]] const std::vector<ObjectiveDefinition>& objectives(
    ) const noexcept {
        return objectives_;
    }

private:
    explicit Encounter(EncounterSnapshot state);
    friend CreateResult create_encounter(const EncounterDefinition&);

    [[nodiscard]] bool reachable(
        Position origin,
        Position destination,
        std::uint8_t allowance,
        std::uint8_t crossings,
        Side mover
    ) const;
    void evaluate_objectives(const UnitSnapshot& actor, CommandResult& result);
    // Advances to whoever the turn order says acts next. Alternating order
    // simply hands the turn to the other side and lets the caller choose.
    // Where the advance closes a round it lands whatever the new round brings,
    // appending an arrival event per character, which is why it is handed the
    // result to write into.
    void begin_next_activation(Side previous, CommandResult& result);
    // Stands every character whose arrival round is the round now in progress
    // on the board, on its authored tile or the nearest one it could stand on.
    void land_arrivals(CommandResult& result);

    EncounterSnapshot state_;
    // Whether this encounter gives the round count consequence: a
    // survive-rounds objective, or a character who arrives on a round. Under
    // an ordered turn order the count is kept regardless; under alternating
    // order this is what decides whether it is kept at all. See
    // `EncounterSnapshot::round`.
    bool counts_rounds_{false};
    std::vector<AbilityDefinition> abilities_;
    std::vector<WeaponDefinition> weapons_;
    std::vector<ItemDefinition> items_;
    std::vector<ObjectiveDefinition> objectives_;
};

struct Encounter::CreateResult final {
    CreateError error{CreateError::none};
    Encounter encounter;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CreateError::none;
    }
};

[[nodiscard]] Encounter::CreateResult create_encounter(
    const EncounterDefinition& definition
);

}  // namespace grandleon::simulation
