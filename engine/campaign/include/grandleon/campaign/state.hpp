// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/identity.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// What a campaign remembers between battles.
//
// `DESIGN.md` §5 divides simulation state into "battle-local state and
// persistent campaign state", and §8 lists what a persistent unit record
// retains: "identity, availability, progression, inventory, injuries, death or
// retirement state, relationships, and game-defined variables". This header is
// the second half of that division. It holds no board, no turn, no position:
// nothing that ends when a battle ends.
//
// It is plain data with a stated canonical order, and every collection is kept
// sorted so that two campaigns that were told the same things are byte-for-byte
// the same campaign. Nothing here allocates on a schedule, reads a clock, or
// knows what a file is.

namespace grandleon::campaign {

// Whether a character may take the field, and why not when they may not.
//
// The distinction the roster exists for is between the last two. `retired` is
// a state a rule may reverse; `dead` is the one it may not, and the whole
// point of persisting it is that "a permanently dead character cannot reappear
// merely because a later map lists that character as available" (`DESIGN.md`
// §5). Values are persisted, so this list is append-only.
enum class Availability : std::uint8_t {
    // Known to the campaign, not yet part of the roster. A character an author
    // has written but the player has not met.
    unrecruited = 1,
    // On the roster and deployable.
    available = 2,
    // On the roster and not deployable: dismissed, away, imprisoned. A rule
    // may bring them back.
    retired = 3,
    // Gone. No rule in this module can undo it, and every operation that would
    // is refused rather than ignored.
    dead = 4,
};

[[nodiscard]] std::string_view availability_name(Availability availability) noexcept;

// Which stats a level-up may add a point to, and the order it rolls them in.
//
// The order is a rule and not an implementation detail: it fixes the growth
// stream's consumption order (`engine/campaign_runtime/README.md`), so it is
// written down in three places that must agree (here, where it is persisted,
// `game_content::GrowableStat` which encodes it, and
// `package_runtime::GrowableStat` which decodes it) and the tests check them
// against each other.
//
// `speed` is deliberately not on it: it orders a whole turn rather than pricing
// one blow, so growing it silently reshuffles who acts when, and it deserves
// its own decision. Everything else the stat line holds is here.
//
// Values are persisted and indexed into, so this list is append-only and a
// value is never reused for a different stat. `skill`, `luck`, `evasion` and
// `magic` were therefore added at the end rather than beside the stats they
// read most like: an existing growth seed's first six draws per level have to
// stay the six draws they always were, and a save written before them has to
// keep meaning what it said. That second guarantee is what
// `migrate_roster_schema_2_to_3` pays for.
enum class GrowableStat : std::uint8_t {
    health = 0,
    strength = 1,
    defense = 2,
    resistance = 3,
    movement = 4,
    action_points = 5,
    skill = 6,
    luck = 7,
    evasion = 8,
    magic = 9,
};

inline constexpr std::size_t growable_stat_count = 10;

[[nodiscard]] std::string_view growable_stat_name(GrowableStat stat) noexcept;

// The highest level a character reaches. A cap rather than the sixteen-bit
// field's own limit, because a stated ceiling is a rule an author can plan
// against and 65535 is an accident of storage. A character at the cap earns no
// further experience at all: the batch simply contains nothing about them,
// which is more honest than recording a number that buys nothing.
inline constexpr std::uint16_t maximum_progression_level = 99;

// What a character has become since they joined: how far along they are, and
// what the levels they gained actually gave them.
//
// `gained` is what makes a level durable rather than decorative. It is the sum
// of every successful growth roll this character has ever had, per stat, and it
// is what `campaign_runtime::load_encounter_for_campaign` adds to the authored
// unit type when the character takes the field. Storing the *gains* rather than
// the resulting stats is deliberate: an author who rebalances a class moves
// every character built on it, and a save that stored totals would freeze the
// old balance into the roster forever.
struct Progression final {
    // Always `1 + experience / (the unit type's experience per level)`, capped
    // at `maximum_progression_level`. It is stored rather than derived because
    // this module does not link a package and so cannot read the threshold; the
    // layer that can is the layer that computes it.
    std::uint16_t level{1};
    // Lifetime experience, never spent. A level costs nothing; it is a
    // threshold this total crosses.
    std::uint32_t experience{};
    // Points added by level-ups, indexed by `GrowableStat`.
    std::array<std::uint16_t, growable_stat_count> gained{};

    [[nodiscard]] std::uint16_t gain_in(GrowableStat stat) const noexcept {
        return gained[static_cast<std::size_t>(stat)];
    }
};

[[nodiscard]] constexpr bool operator==(
    const Progression& lhs,
    const Progression& rhs
) noexcept {
    return lhs.level == rhs.level && lhs.experience == rhs.experience &&
           lhs.gained == rhs.gained;
}

// A quantity of one item kind. Held by a campaign member, or by the shared
// store when the owner is the reserved zero id.
struct InventoryStack final {
    DefinitionRef item{};
    std::uint32_t quantity{};
};

// One member of the roster: who they are, what they are, and whether they are
// still with you.
struct PersistentUnit final {
    PersistentEntityId id{};
    // The unit type this member is an instance of. Two members may share it;
    // everything else about them is their own, which is the property the three
    // identity levels exist to guarantee.
    DefinitionRef definition{};
    Availability availability{Availability::unrecruited};
    Progression progression{};
    // Ascending `definition_ref_less` over `item`.
    std::vector<InventoryStack> carried;
};

// How an objective ended, as the campaign remembers it. Mirrors
// `simulation::ObjectiveState` in meaning, and is declared separately because
// this module does not depend on the simulation: a committed campaign fact is
// not a battle observation, and the two are allowed to diverge in future
// without dragging each other along.
enum class ObjectiveOutcome : std::uint8_t {
    satisfied = 1,
    failed = 2,
};

struct ObjectiveRecord final {
    DefinitionRef objective{};
    ObjectiveOutcome result{ObjectiveOutcome::satisfied};
};

// The type of a world value. `DESIGN.md` §5 requires "typed world flags"
// rather than free-form strings, and requires that campaign conditions be
// data rather than expressions; a closed two-member vocabulary is what makes a
// predicate over them decidable. Append-only.
enum class WorldValueType : std::uint8_t {
    boolean = 1,
    integer = 2,
};

// One typed durable value. Sixty-four bits regardless of type, because a fixed
// width is what a save wants and a boolean costs nothing to widen.
struct WorldValue final {
    WorldValueType type{WorldValueType::boolean};
    std::int64_t value{};
};

[[nodiscard]] constexpr bool operator==(
    const WorldValue& lhs,
    const WorldValue& rhs
) noexcept {
    return lhs.type == rhs.type && lhs.value == rhs.value;
}

// A named durable value the campaign carries. This is also where script state
// lives, and that is a deliberate choice rather than an omission: the schema's
// `scriptBindings` are "typed inert bindings retained for a future script ABI"
// and nothing executes them, so a separate script-state model would be a guess
// at the shape of a runtime that does not exist. A script that one day wants a
// durable variable asks for a world value keyed by its own definition
// reference, and the save format learns nothing new.
struct WorldFlag final {
    DefinitionRef key{};
    WorldValue value{};
};

// One step of the route a campaign took through its authored graph: a node it
// entered, and the committed batch whose consequences put it there.
//
// The cause is what makes a completion event identifiable rather than counted.
// A retry of the same battle produces the same `OutcomeId` (`outcome.hpp`), so
// a completion already recorded here is recognised as already recorded, and no
// edge is traversed twice. The entry node has no cause and carries the
// reserved zero id: nothing completed to reach the beginning.
struct ProgressionEntry final {
    // `ContentCategory::campaign_node`.
    DefinitionRef node{};
    OutcomeId cause{};
};

[[nodiscard]] constexpr bool operator==(
    const ProgressionEntry& lhs,
    const ProgressionEntry& rhs
) noexcept {
    return lhs.node == rhs.node && lhs.cause == rhs.cause;
}

// Where a campaign stands in its authored graph, and how it got there.
//
// This is the state a branched, recombined route needs to resume. Two
// campaigns that reached one node through different valid predecessors agree
// about `active_node` and disagree about `history`, which is exactly the
// distinction the spec's recombination scenario asks to survive a save.
//
// `history` is the one collection in this header that is not sorted, because
// here the order *is* the data: a route is a sequence, and sorting it would
// throw away the thing being persisted. It is still canonical: the same route
// produces the same sequence, so the encoding stays deterministic.
struct CampaignProgress final {
    // False before `begin_campaign`. An inactive progression carries nothing
    // else, which `validate` enforces, so "has this campaign started?" has one
    // spelling and a save has one encoding of the answer.
    bool active{false};
    // `ContentCategory::campaign`: which graph this position belongs to.
    DefinitionRef campaign{};
    // `ContentCategory::campaign_node`, and always `history.back().node`.
    DefinitionRef active_node{};
    // Chronological, entry node first.
    std::vector<ProgressionEntry> history;
};

// Everything a campaign carries between battles.
//
// Copyable on purpose, and cheaply so at roster scale: `apply_outcome` builds
// a complete candidate, validates it, and only then swaps it in. That is the
// whole of the atomicity guarantee, and it is why this type owns no handle,
// no reference, and nothing whose copy would mean something different from
// the original.
struct CampaignState final {
    // Ascending `id`.
    std::vector<PersistentUnit> units;
    // The shared store: items belonging to the campaign rather than to any
    // member. Ascending `definition_ref_less` over `item`.
    std::vector<InventoryStack> store;
    // Ascending `definition_ref_less` over `objective`.
    std::vector<ObjectiveRecord> objectives;
    // Ascending `definition_ref_less` over `key`.
    std::vector<WorldFlag> world;
    // The outcome batches already committed, ascending. This is what makes a
    // retry safe: an id already here is a batch already applied, and applying
    // it again does nothing rather than doing it twice.
    std::vector<OutcomeId> applied_outcomes;
    // Where the campaign stands in its authored graph. Empty until a graph is
    // entered, which is what every campaign written before `graph.hpp` existed
    // looks like.
    CampaignProgress progress;
};

// Read-only questions the rest of the engine asks of a campaign. Free
// functions rather than members so that `CampaignState` stays a plain record a
// serializer can walk field by field.

[[nodiscard]] const PersistentUnit* find_unit(
    const CampaignState& state,
    PersistentEntityId id
) noexcept;

// Whether this character may be placed on a board. The one question an
// encounter asks of the roster: a character who is unrecruited, retired, or
// dead is not deployable, and no later map naming them can make them so.
[[nodiscard]] bool is_deployable(
    const CampaignState& state,
    PersistentEntityId id
) noexcept;

// Everyone deployable, in ascending id order. The roster an encounter draws
// from once the wiring exists.
[[nodiscard]] std::vector<PersistentEntityId> deployable_units(
    const CampaignState& state
);

// How many of an item an owner holds; owner zero is the shared store.
[[nodiscard]] std::uint32_t item_quantity(
    const CampaignState& state,
    PersistentEntityId owner,
    const DefinitionRef& item
) noexcept;

[[nodiscard]] const WorldValue* find_world_value(
    const CampaignState& state,
    const DefinitionRef& key
) noexcept;

[[nodiscard]] const ObjectiveRecord* find_objective(
    const CampaignState& state,
    const DefinitionRef& objective
) noexcept;

[[nodiscard]] bool outcome_applied(
    const CampaignState& state,
    OutcomeId id
) noexcept;

// Why a whole campaign state is not a campaign state. Checked over the
// complete candidate before a commit, so an operation cannot leave behind an
// arrangement no sequence of legal operations could have reached. Append only.
enum class StateError : std::uint8_t {
    none = 0,
    // The reserved zero id was used as a roster member.
    reserved_identity,
    // Two members share one persistent id, or a collection holds one key
    // twice.
    duplicate_identity,
    // A collection is out of its stated canonical order.
    unordered_collection,
    // A stack that exists but holds nothing. Absence is spelled one way.
    empty_stack,
    // A dead member still carries equipment, or is otherwise recorded in a way
    // permanent death rules out.
    inconsistent_availability,
    // The progression position contradicts itself: an inactive progression
    // that remembers a node, an active one whose last history entry is not the
    // active node, a route that names one completion twice, or a step caused
    // by a batch this campaign never committed.
    inconsistent_progression,
};

[[nodiscard]] std::string_view state_error_name(StateError error) noexcept;

// The complete-state check. Every invariant the rest of the module relies on
// is stated once here rather than re-derived by each operation, because the
// guarantee the design asks for is about the *resulting state* and not about
// the steps that produced it.
[[nodiscard]] StateError validate(const CampaignState& state) noexcept;

// A digest of the whole campaign, in the same field order a save will write:
// FNV-1a-64 over fixed-width little-endian fields, exactly as
// `simulation::Encounter::canonical_hash` folds a battle.
//
// It is a testing and comparison diagnostic, as `DESIGN.md` §3.2 says a
// canonical hash is. Nothing in the gate pins a literal value for it; what the
// tests use it for is equality: that applying an outcome twice leaves the
// campaign the same campaign it was after applying it once.
[[nodiscard]] std::uint64_t canonical_hash(const CampaignState& state) noexcept;

}  // namespace grandleon::campaign
