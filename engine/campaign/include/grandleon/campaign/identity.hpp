// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/core/content_identity.hpp>
// For `fnv1a64_step`, which `core` names once so that the generator, the
// canonical hash, and every folded identity here cannot drift apart.
#include <grandleon/core/random.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

// The three names a durable thing goes by.
//
// `DESIGN.md` §8 requires that persistent references "use stable package and
// content identities, never memory addresses or transient array positions",
// and the accepted change's design separates three levels rather than two.
// Each answers a different question, and collapsing any pair of them loses
// something a roster needs:
//
// * `DefinitionRef`: *what kind of thing is this?* The package that authored
//   it, the category it belongs to, and its stable content id. Two recruits of
//   the same unit type share exactly this and nothing else.
// * `PersistentEntityId`: *which one of them is this?* One campaign instance,
//   for the whole life of the campaign. It is what permanent death removes and
//   what progression accrues to. It survives a definition being renamed,
//   because a rename is a change to the definition and not to the person.
// * `BattleEntityId`: *who is this on the board right now?* Battle-local, and
//   meaningful only inside one encounter. A summoned monster or a scripted
//   bystander has one and no persistent identity at all, which is why the map
//   back is partial by construction.
//
// Nothing here allocates, reads a clock, or touches a file, and no type below
// carries a pointer, an index into a container, or a standard-library layout.
// Nothing here serializes either; the shapes are chosen so that a writer can
// put them down as fixed-width little-endian fields and nothing else.

namespace grandleon::campaign {

// What kind of thing, named by the package that authored it. This is
// `core::ContentRef` under the name the design gives it: the triple of package
// id, content category, and stable content id. It is an alias rather than a
// new struct on purpose: a second spelling of the same triple would be a
// second thing to keep in step, and the compiler already emits this one.
using DefinitionRef = core::ContentRef;

// What a `DefinitionRef` occupies once written: sixteen bytes of package id,
// four of category, eight of stable id. Stated here rather than as `sizeof`,
// because `sizeof` includes whatever padding a host ABI chose and the encoded
// width must not.
inline constexpr std::size_t definition_ref_encoded_size = 28U;

// One campaign instance. A wrapper rather than a bare `std::uint64_t` so that
// a persistent id cannot be passed where a battle id is wanted, or where a
// stable content id is: the whole point of three levels is lost the moment two
// of them are the same type.
//
// Zero is reserved and means "no entity". Campaign inventory uses it for the
// shared store, and a battle unit that belongs to nobody maps back to it.
struct PersistentEntityId final {
    std::uint64_t value{};
};

[[nodiscard]] constexpr bool operator==(
    PersistentEntityId lhs,
    PersistentEntityId rhs
) noexcept {
    return lhs.value == rhs.value;
}

[[nodiscard]] constexpr bool operator!=(
    PersistentEntityId lhs,
    PersistentEntityId rhs
) noexcept {
    return !(lhs == rhs);
}

[[nodiscard]] constexpr bool operator<(
    PersistentEntityId lhs,
    PersistentEntityId rhs
) noexcept {
    return lhs.value < rhs.value;
}

// Who this is on the board, inside one encounter and no longer. Sixty-four
// bits because that is what `simulation::UnitId` is; the type is declared here
// rather than aliased from the simulation because this module must not depend
// on it: persistent state is not battle state, and the dependency would point
// the wrong way. A binding table joins the two.
struct BattleEntityId final {
    std::uint64_t value{};
};

[[nodiscard]] constexpr bool operator==(
    BattleEntityId lhs,
    BattleEntityId rhs
) noexcept {
    return lhs.value == rhs.value;
}

[[nodiscard]] constexpr bool operator!=(
    BattleEntityId lhs,
    BattleEntityId rhs
) noexcept {
    return !(lhs == rhs);
}

[[nodiscard]] constexpr bool operator<(
    BattleEntityId lhs,
    BattleEntityId rhs
) noexcept {
    return lhs.value < rhs.value;
}

// The identity of one committed batch of campaign consequences. Derived from
// what produced the batch and from what the batch says, so that the same
// battle producing the same outcome twice produces the same id twice. See
// `outcome.hpp`.
struct OutcomeId final {
    std::uint64_t value{};
};

[[nodiscard]] constexpr bool operator==(OutcomeId lhs, OutcomeId rhs) noexcept {
    return lhs.value == rhs.value;
}

[[nodiscard]] constexpr bool operator!=(OutcomeId lhs, OutcomeId rhs) noexcept {
    return !(lhs == rhs);
}

[[nodiscard]] constexpr bool operator<(OutcomeId lhs, OutcomeId rhs) noexcept {
    return lhs.value < rhs.value;
}

// None of these may grow a pointer, a virtual table, or a container. A save
// writes their fields; a console reads them back out of two kilobytes.
static_assert(std::is_standard_layout<DefinitionRef>::value);
static_assert(std::is_trivially_copyable<DefinitionRef>::value);
static_assert(std::is_standard_layout<PersistentEntityId>::value);
static_assert(std::is_trivially_copyable<PersistentEntityId>::value);
static_assert(sizeof(PersistentEntityId) == 8U);
static_assert(std::is_standard_layout<BattleEntityId>::value);
static_assert(std::is_trivially_copyable<BattleEntityId>::value);
static_assert(sizeof(BattleEntityId) == 8U);
static_assert(std::is_standard_layout<OutcomeId>::value);
static_assert(std::is_trivially_copyable<OutcomeId>::value);
static_assert(sizeof(OutcomeId) == 8U);

// Total order over definition references: package bytes, then category, then
// stable id. Every collection in this module is kept in a stated order so that
// iteration, hashing, and a future encoding cannot depend on insertion order.
[[nodiscard]] constexpr bool definition_ref_less(
    const DefinitionRef& lhs,
    const DefinitionRef& rhs
) noexcept {
    for (std::size_t index = 0; index < lhs.package_id.size(); ++index) {
        if (lhs.package_id[index] != rhs.package_id[index]) {
            return lhs.package_id[index] < rhs.package_id[index];
        }
    }
    if (lhs.category != rhs.category) {
        return static_cast<std::uint32_t>(lhs.category) <
               static_cast<std::uint32_t>(rhs.category);
    }
    return lhs.stable_id < rhs.stable_id;
}

// Fold a definition reference into a running FNV-1a-64 hash, in the fixed
// little-endian field order a save will write. Twenty-eight bytes, never the
// struct's memory.
[[nodiscard]] std::uint64_t hash_definition_ref(
    std::uint64_t hash,
    const DefinitionRef& reference
) noexcept;

// What can go wrong while establishing identities, before any campaign state
// exists to damage. Serialized in diagnostics, so append only.
enum class IdentityError : std::uint8_t {
    none = 0,
    // Two different source keys in one package and category produced the same
    // stable content id. The compiler must refuse the content; nothing
    // downstream can tell the two records apart.
    definition_collision,
    // One source key was declared twice with different references.
    duplicate_definition_key,
    // A rename mapping was declared twice for the same old reference.
    duplicate_rename,
    // A rename maps a reference onto itself, or a chain of renames returns to
    // where it started.
    rename_cycle,
    // A rename's target does not exist in the mounted content, and no further
    // mapping leads anywhere that does.
    missing_definition,
    // A battle id or a persistent id was bound twice in one battle.
    duplicate_binding,
    // A binding named the reserved zero id on either side.
    reserved_identity,
};

[[nodiscard]] std::string_view identity_error_name(IdentityError error) noexcept;

// The definitions one package declares, and the collision check nothing
// downstream can perform for itself.
//
// `core::stable_content_id_v1` is a persistence contract rather than a
// security hash, and `engine/core/README.md` says plainly that "a collision is
// the compiler's problem". This is the check, stated where the persistent
// layer can also run it: a save that resolves two distinct authored keys to
// one stable id has two characters the roster cannot tell apart, and that must
// be an error at the door rather than a mystery later.
class DefinitionRegistry final {
public:
    // Declare a definition under its authored source key, deriving the stable
    // id the compiler would derive.
    IdentityError declare(
        const core::PackageId& package,
        core::ContentCategory category,
        std::string_view source_key
    );

    // Declare a definition whose stable id is already known: read back from a
    // compiled package, or forced by a test that needs a collision the hash
    // will not hand it.
    IdentityError declare(const DefinitionRef& reference, std::string_view source_key);

    [[nodiscard]] bool contains(const DefinitionRef& reference) const noexcept;

    // The reference a source key resolves to, or nothing.
    [[nodiscard]] const DefinitionRef* find(
        const core::PackageId& package,
        core::ContentCategory category,
        std::string_view source_key
    ) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry final {
        DefinitionRef reference{};
        // The authored key, kept only so a collision can be told from a
        // harmless re-declaration of the same record. It is host-side
        // bookkeeping and is never part of a persisted identity.
        std::vector<char> source_key;
    };

    // Ascending `definition_ref_less`.
    std::vector<Entry> entries_;
};

// One authored definition renamed to another. A rename is content evolution,
// not a change of persistent identity: the character the save refers to is the
// same character, and only the definition it points at moved.
struct DefinitionRename final {
    DefinitionRef from{};
    DefinitionRef to{};
};

// The rename mappings a package supplies, and the resolution a load performs
// before it believes a stored reference.
//
// Renames are explicit and package-provided by decision: a loader that
// guessed (by matching names, by position, by anything) would silently
// repoint a dead character's record at whichever record happened to look
// similar.
class DefinitionRenameTable final {
public:
    IdentityError add(const DefinitionRename& rename);

    // Fold another table's mappings into this one, in the order they were
    // declared, and refuse for exactly the reasons `add` refuses.
    //
    // A load that crosses several content revisions collects one table per
    // revision and folds them together rather than flattening them by hand
    // (`migration.hpp`). Folding is what turns "renamed in revision two and
    // again in revision four" into a chain `resolve` can follow, and refusing
    // here is what keeps that chain finite.
    IdentityError merge(const DefinitionRenameTable& other);

    // Follow the chain of renames from `reference` and return where it ends.
    // Chains are followed rather than applied once, so a package that renamed
    // a record twice across two revisions does not need to flatten its own
    // history. A cycle is refused when it is declared, so this terminates.
    [[nodiscard]] DefinitionRef resolve(const DefinitionRef& reference) const noexcept;

    // Resolve and then require the result to exist in mounted content. This is
    // the check a save load owes the player: a reference that survives renames
    // but names nothing is `missing_definition`, not a silent blank.
    [[nodiscard]] IdentityError resolve_in(
        const DefinitionRegistry& registry,
        const DefinitionRef& reference,
        DefinitionRef& resolved
    ) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return renames_.size(); }

private:
    // Ascending `definition_ref_less` over `from`.
    std::vector<DefinitionRename> renames_;
};

// The join between a battle and the campaign that outlives it: which unit on
// the board is which member of the roster.
//
// Partial in one direction on purpose. Every persistent unit deployed into a
// battle has a battle id; not every unit on the board has a persistent one,
// because summons, reinforcements, and the nameless opposing side exist only
// for the length of the fight. Asking the map about one of those answers "no
// campaign identity", which is a fact rather than a failure.
//
// The table is built and read here alone. Nothing loads an encounter through
// it, so binding a battle to the roster moves no canonical hash.
class BattleBinding final {
public:
    IdentityError bind(BattleEntityId battle, PersistentEntityId persistent);

    // The campaign member standing on the board as `battle`, or the reserved
    // zero id when that unit belongs to no campaign member.
    [[nodiscard]] PersistentEntityId persistent_of(BattleEntityId battle) const noexcept;

    // Where a campaign member stands in this battle, or the reserved zero id
    // when that member was not deployed.
    [[nodiscard]] BattleEntityId battle_of(PersistentEntityId persistent) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return pairs_.size(); }

private:
    struct Pair final {
        BattleEntityId battle{};
        PersistentEntityId persistent{};
    };

    // Ascending battle id.
    std::vector<Pair> pairs_;
};

}  // namespace grandleon::campaign
