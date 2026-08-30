// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/identity.hpp>
#include <grandleon/campaign/save.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// What happens when the save on the card is older than the build reading it.
//
// `save.hpp` left a seam and said what belongs in it: "the migration registry
// goes between the two: it rewrites section bytes and section schema versions,
// and everything downstream of it re-runs unchanged."
//
// This is that registry, and it fills the seam from both sides,
// because a save goes out of date along two independent axes and only one of
// them is a version number in the envelope.
//
// * **The format moved.** A section learned a field, or lost one, or changed
//   how it spells something. The envelope records a schema version per section
//   precisely so that this can happen to one section without a migration for
//   all six. A save at `roster` schema 0 reaching a build at schema 1 is this
//   axis, and it is repaired by rewriting the section's *bytes* before anything
//   interprets them.
// * **The content moved.** The package renamed a definition. Nothing about the
//   bytes is wrong (every field is where it should be, every checksum matches)
//   and the save still names a unit type, an item, an objective or a campaign
//   node that the mounted package no longer has under that identity. This is
//   repaired by rewriting the *references* in the interpreted candidate,
//   through the `DefinitionRenameTable` that `identity.hpp` already defines and
//   that a package supplies explicitly. `DESIGN.md` and the accepted change
//   both refuse to let a loader guess here: a rename is declared or it is not a
//   rename.
//
// ## Version by version, never in a leap
//
// A migration step goes from one version to the next and no further. A save at
// section schema 1 reaching a build at schema 3 runs 1→2 and then 2→3, each a
// separately registered function with its own test. The alternative, one
// function per (old, new) pair, is quadratic in the number of versions and
// gives the pair that nobody thought to write no diagnostic at all.
//
// The consequence is that the registry can be asked a question it must answer
// honestly: *is there a path?* `plan_section_migration` walks the chain one
// version at a time and stops at the first version with no registered step out
// of it, naming that version. A gap is `missing_step`, never a silent skip, and
// never an attempt to run the far end of the chain against the near end's
// bytes.
//
// Going backwards is refused rather than attempted. A save written by a newer
// build knows things this one does not, and the only honest transform from a
// newer format to an older one is a lossy one. `downgrade_refused` says so by
// name instead of dropping the fields this build cannot see.
//
// ## Old bytes in, new bytes out
//
// No step mutates anything. A section step is handed the section's bytes and an
// empty buffer to fill; a content step is handed an empty rename table to
// declare into. The migrated `DecodedSave` and the migrated `CampaignSave` are
// *returned*, and the caller's originals are untouched whatever happens. That
// is what makes the whole load transactional in the sense the design asks for:
// "Loading decodes, validates, resolves packages, applies sequential
// migrations, and validates the final candidate before swapping it into the
// live session." A migration that fails at its third step leaves no half-
// upgraded campaign anywhere, because there was never one campaign being
// upgraded: there was a chain of complete candidates, and the last one either
// validated or was dropped.
//
// The final validation is not a new one. A migrated save goes through the same
// `interpret_save` and the same `validate` every save goes through, so a
// migration cannot produce an arrangement that a legal sequence of operations
// could not have reached. `load_campaign_migrated` is the whole pipeline, and
// `load_campaign_migrated_into` is that pipeline with the swap at the end.

namespace grandleon::campaign {

// ---------------------------------------------------------------------------
// What this build has mounted
// ---------------------------------------------------------------------------

// One package the running build actually holds, and what it is.
//
// `SavePackageRequirement` is what the save *believed*; this is what is *there*.
// Comparing the two is the whole of "resolve package requirements against
// mounted content": equal revisions need nothing, a newer mounted revision
// needs the content migrations between them, and an older one is a downgrade.
struct MountedPackage final {
    core::PackageId package{};
    std::uint32_t content_revision{};
    // Whatever the package layer computed over the mounted package. Carried so
    // that a migrated save can record the truth about what it now refers to,
    // rather than the stale value it was written with. This module still does
    // not compute or verify it: it does not link the package format.
    std::uint64_t integrity{};
    // What the package declares, when the caller has it. Optional because this
    // module cannot build one: a `DefinitionRegistry` is filled from compiled
    // content, which lives above this layer. Supplied, it turns "this rename
    // points at nothing" from a silent blank into `missing_definition`.
    const DefinitionRegistry* definitions{nullptr};
};

class MountedContent final {
public:
    // Mount a package, replacing any entry already held for that identity.
    void mount(const MountedPackage& package);

    [[nodiscard]] const MountedPackage* find(
        const core::PackageId& package
    ) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return packages_.size(); }

private:
    // Ascending package id, so a lookup and an iteration are both stable.
    std::vector<MountedPackage> packages_;
};

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

// Why a migration did not happen. Reported to players and written into logs, so
// append only.
enum class MigrationError : std::uint8_t {
    none = 0,
    // The save requires a package this build has not mounted. Nothing can be
    // migrated against content that is not there.
    unmounted_package,
    // The save is newer than this build along one of the two axes: a section
    // schema this build has not reached, or a content revision beyond the
    // mounted one. Refused by name rather than attempted lossily.
    downgrade_refused,
    // The chain has a hole: no registered step leaves the version named by
    // `MigrationReport::from`. The registry says so rather than skipping to
    // whatever step it does have.
    missing_step,
    // A registered step ran and refused. Its own reason is its own; what this
    // layer knows is which step, which is what `MigrationReport` names.
    step_failed,
    // A chain longer than `maximum_migration_steps`. A hostile save claiming
    // section schema 60000 costs one comparison, not sixty thousand lookups.
    step_limit_exceeded,
    // A content step declared a rename table that is not one: the same source
    // mapped twice, or a cycle.
    invalid_rename,
    // A reference survived every rename and still names nothing in the mounted
    // content that declared it.
    missing_definition,
    // Two distinct references were renamed onto one, so the migrated campaign
    // would hold one key twice. Merging them would be a guess about which
    // quantity, which result, or which value survives, and this layer does not
    // guess.
    rename_collision,
    // The migrated candidate is not a campaign. `MigrationReport::state_error`
    // says which invariant it broke.
    invalid_result,
};

[[nodiscard]] std::string_view migration_error_name(MigrationError error) noexcept;

// A chain no longer than this. Both axes are bounded by it, and it is far above
// any plausible format history for the same reason `SaveLimits` is far above
// any plausible campaign: the number exists to make a hostile input cheap, not
// to express a policy about how often a format may move.
inline constexpr std::uint32_t maximum_migration_steps = 64;

// One step that ran, in the order it ran. This is the "migration observable"
// the design's goals ask for: after a load, a caller can say exactly what was
// done to the bytes it was handed, and a test can assert that the chain was
// walked one version at a time rather than jumped.
struct AppliedMigration final {
    // False for a section-schema step, true for a content-revision step.
    bool content{false};
    // The section a schema step upgraded; zero for a content step.
    std::uint32_t section{};
    // The package a content step upgraded; all zero for a section step.
    core::PackageId package{};
    // Schema major, or content revision. On the section axis `to` is always
    // `from + 1`, because a schema major is a dense counter. On the content
    // axis it is whatever the registered step said it lands on, because a
    // content revision is a packed version and its neighbours are patch
    // numbers.
    std::uint32_t from{};
    std::uint32_t to{};
};

[[nodiscard]] bool operator==(
    const AppliedMigration& lhs,
    const AppliedMigration& rhs
) noexcept;

// What a migration did, or what stopped it.
struct MigrationReport final {
    MigrationError error{MigrationError::none};
    // The section the refusal is about, or zero when it is about a package.
    std::uint32_t section{};
    // The package the refusal is about, or all zero when it is about a section.
    core::PackageId package{};
    // The versions the refusal is between. For `missing_step`, `from` is the
    // version with no step out of it.
    std::uint32_t from{};
    std::uint32_t to{};
    // Set when `error` is `invalid_result`.
    StateError state_error{StateError::none};
    // Every step that ran, in order. Populated on success, and on failure holds
    // the steps that ran before the refusal, which were applied to candidates
    // that were then dropped, and to nothing the caller can see.
    std::vector<AppliedMigration> applied;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == MigrationError::none;
    }
};

// ---------------------------------------------------------------------------
// The steps themselves
// ---------------------------------------------------------------------------

// What a section step is told about the save it is repairing. Everything here
// is already proved: the header passed envelope compatibility, the package
// table is in order, and the limits are the caller's.
struct SectionMigrationContext final {
    SaveHeader header{};
    const std::vector<SavePackageRequirement>* packages{nullptr};
    const SaveLimits* limits{nullptr};
    // The schema this step is upgrading from. `to` is always `from + 1`.
    std::uint16_t from_major{};
};

// One section's bytes at `from_major`, and an empty buffer to write the same
// section at `from_major + 1` into. Returns false to refuse, which is what a
// step does when the old bytes are not the old format, and is exactly as fatal
// as any other refusal on the load path.
//
// A plain function pointer rather than `std::function`: a migration is a pure
// transform with no state to capture, and this module writes no standard-
// library layout into anything a console reads.
using SectionMigrationFunction = bool (*)(
    const SectionMigrationContext& context,
    const std::vector<std::uint8_t>& before,
    std::vector<std::uint8_t>& after
);

// What a content step is told: which package moved, and from which revision.
struct ContentMigrationContext final {
    core::PackageId package{};
    std::uint32_t from_revision{};
    // The mounted package's declarations, when the caller supplied them.
    const DefinitionRegistry* definitions{nullptr};
};

// Declare the renames one content revision introduced, into an empty table.
// Returns false to refuse.
//
// A rename table rather than a free transform, on purpose. The spec requires
// that a renamed definition be repointed "without changing the persistent
// entity identity", and a table can only repoint definitions: it has no way to
// touch a roster member's identity, their progression, or whether they are
// alive. The narrower surface is the guarantee.
using ContentMigrationFunction = bool (*)(
    const ContentMigrationContext& context,
    DefinitionRenameTable& renames
);

// ---------------------------------------------------------------------------
// The registry
// ---------------------------------------------------------------------------

// Every step this build knows, on both axes.
//
// Registration is by (what moved, which version it moved from). Registering the
// same step twice is refused rather than overwritten: two functions claiming one
// version is an ambiguity nobody could resolve later, and the second one
// silently winning is the worst of the three possible answers.
//
// **A section step's destination is the next major; a content step names its
// own.** The two axes count differently and used to be treated the same. A
// schema major really is a dense counter, so `from + 1` is the whole truth
// there. A content revision is a version this project packs as
// `(major << 20) | (minor << 10) | patch`, so the integer after 0.1.0 is 0.1.1
// and the one after that is 0.1.2: assuming the destination made every step a
// patch step, and put a thousand and twenty-four of them between 0.1.0 and
// 0.2.0 -- further than `maximum_migration_steps` allows a chain to be walked.
// A game that moved its minor version could therefore never write a migration
// at all, and the refusal it got named the step limit, which is true and
// useless. So a content step says where it lands and the chain is followed
// edge by edge.
class SaveMigrationRegistry final {
public:
    // Returns false when a step is already registered for this section and
    // version, or when `apply` is null.
    bool add_section_migration(
        SaveSectionType section,
        std::uint16_t from_major,
        SectionMigrationFunction apply
    );

    // Returns false when a step is already registered for this package and
    // revision, when `apply` is null, or when `to_revision` does not move
    // forward: a step that lands where it started is a chain that never ends,
    // and one that lands earlier is a downgrade wearing a migration's clothes.
    bool add_content_migration(
        const core::PackageId& package,
        std::uint32_t from_revision,
        std::uint32_t to_revision,
        ContentMigrationFunction apply
    );

    [[nodiscard]] SectionMigrationFunction find_section_migration(
        SaveSectionType section,
        std::uint16_t from_major
    ) const noexcept;

    [[nodiscard]] ContentMigrationFunction find_content_migration(
        const core::PackageId& package,
        std::uint32_t from_revision
    ) const noexcept;

    // Where the step registered at `from_revision` lands, or zero when there is
    // none. Zero is not a revision anything can migrate *to*, which is what
    // makes it usable as "no step" without a second return value.
    [[nodiscard]] std::uint32_t content_step_target(
        const core::PackageId& package,
        std::uint32_t from_revision
    ) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return sections_.size() + contents_.size();
    }

private:
    struct SectionEntry final {
        SaveSectionType section{};
        std::uint16_t from_major{};
        SectionMigrationFunction apply{};
    };

    struct ContentEntry final {
        core::PackageId package{};
        std::uint32_t from_revision{};
        std::uint32_t to_revision{};
        ContentMigrationFunction apply{};
    };

    std::vector<SectionEntry> sections_;
    std::vector<ContentEntry> contents_;
};

// The steps that lead from `from_major` to `to_major`, or the reason there are
// none. Pure: it consults the registry and runs nothing.
//
// Exposed rather than kept private because "can this build read that save?" is
// a question a save-slot menu wants to answer before it offers the slot, and
// because the chain-walking rule is worth testing on its own: a two-step path,
// a hole in the middle of one, and a backwards one are three different answers
// and none of them needs bytes to demonstrate.
[[nodiscard]] MigrationReport plan_section_migration(
    const SaveMigrationRegistry& registry,
    SaveSectionType section,
    std::uint16_t from_major,
    std::uint16_t to_major
);

// The same question on the content axis.
[[nodiscard]] MigrationReport plan_content_migration(
    const SaveMigrationRegistry& registry,
    const core::PackageId& package,
    std::uint32_t from_revision,
    std::uint32_t to_revision
);

// ---------------------------------------------------------------------------
// Running them
// ---------------------------------------------------------------------------

// A decoded save with every known section brought up to this build's schema.
struct MigratedSections final {
    MigrationReport report;
    // The candidate. Only meaningful when `report` is clear; on a refusal it is
    // empty, so there is no half-migrated save to reach for by mistake.
    DecodedSave decoded;
};

// Bring `decoded`'s known sections up to this build's section schemas.
//
// `decoded` is read and not touched. Unknown sections are carried through
// untouched too: a build that does not know a section cannot know how to
// upgrade it, and `interpret_save` will retain or refuse it by the rule it
// already has.
[[nodiscard]] MigratedSections migrate_sections(
    const DecodedSave& decoded,
    const SaveMigrationRegistry& registry,
    const SaveLoadOptions& options
);

// An interpreted save with every reference brought up to the mounted content.
struct MigratedContent final {
    MigrationReport report;
    CampaignSave save;
};

// Resolve `candidate`'s package requirements against `content`, run the content
// steps between them, repoint every definition reference the campaign holds,
// and validate the whole result.
//
// Persistent identities are untouched by construction: a rename table can only
// say which definition a reference names, which is the spec's requirement that
// a renamed definition leave "the persistent entity identity" alone.
[[nodiscard]] MigratedContent migrate_content(
    const CampaignSave& candidate,
    const SaveMigrationRegistry& registry,
    const MountedContent& content
);

// Decode, migrate the bytes, interpret, migrate the references, validate.
struct MigratedLoad final {
    MigrationReport migration;
    SaveLoadResult load;
    // The migrated save, equal to `load.save` with every rename applied. Empty
    // when either half refused.
    CampaignSave save;

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(migration) && static_cast<bool>(load);
    }
};

[[nodiscard]] MigratedLoad load_campaign_migrated(
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options,
    const SaveMigrationRegistry& registry,
    const MountedContent& content
);

// The whole pipeline with the swap at the end. `live` is replaced only when
// every stage succeeded: decode, section migration, interpretation, content
// migration, and the final whole-state validation. It is otherwise exactly
// what it was.
[[nodiscard]] MigratedLoad load_campaign_migrated_into(
    CampaignSave& live,
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options,
    const SaveMigrationRegistry& registry,
    const MountedContent& content
);

// ---------------------------------------------------------------------------
// The steps this build ships
// ---------------------------------------------------------------------------

// The `roster` section as it was before schema 1: no per-member experience and
// no per-member inventory.
//
// Schema 0 is the shape the roster section had before a member had a
// progression and items of their own; a campaign's whole stock lived in the
// shared store. Its record is forty bytes and has no trailing count:
//
// ```
// unit record, schema 0, 40 bytes
//   0  8   persistent id
//   8 28   definition reference
//  36  1   availability
//  37  1   reserved, zero
//  38  2   level
// ```
//
// The upgrade gives each member the documented default of the fields schema 1
// added: no experience, and nothing carried. It never invents a number: a
// campaign that never recorded experience did not have any, and one whose stock
// was shared did not hold it per member.
//
// This is the format's first migration, and it exists as much to be the worked
// example as to be run. `tests/campaign/migration_test.cpp` loads a save at
// this schema out of `tests/campaign/fixtures/`, which the current writer
// cannot produce, and checks the campaign it becomes field by field.
[[nodiscard]] bool migrate_roster_schema_0_to_1(
    const SectionMigrationContext& context,
    const std::vector<std::uint8_t>& before,
    std::vector<std::uint8_t>& after
);

// The `roster` section as it was before schema 2: a member's level was a
// number and nothing had been decided about what reaching it gave them.
//
// Schema 2 is what growth needed. A level-up rolls each stat's authored chance
// and the successes are permanent, so a roster member carries the points those
// rolls granted (twelve bytes, six stats, `GrowableStat` order) inserted
// after the experience field and before the carried-stack count, which is
// where every fixed-width field in the record already lives.
//
// The upgrade writes zeros, and that is the honest number rather than a
// convenient one: a campaign whose levels predate growth rolled nothing, so its
// members gained nothing. It is also why this step must walk each member rather
// than stride: a schema 1 record is variable-length, because schema 1 is the
// version that gave a member their own inventory.
//
// This is the format's second migration, and with the first it makes the chain
// the registry was built to walk: a save at schema 0 runs 0->1 and then 1->2,
// two separately registered functions, one version at a time.
[[nodiscard]] bool migrate_roster_schema_1_to_2(
    const SectionMigrationContext& context,
    const std::vector<std::uint8_t>& before,
    std::vector<std::uint8_t>& after
);

// The `roster` section as it was before schema 3: six gains per member, which
// was the whole stat line a level-up could add to.
//
// Schema 3 is what the richer stat line needed. `skill`, `luck`, `evasion` and
// `magic` were appended to `GrowableStat`, so the fixed-width run of gains goes
// from twelve bytes to twenty: four more 2-byte values at the end of the
// array, where an appended stat's points belong.
//
// The upgrade writes zeros, and here that is not merely honest but provable:
// those four stats were not growable when a schema 2 campaign took its levels,
// so no roll ever granted a point in them.
//
// This is the format's third migration, and the one that shows why every step
// must be frozen at the schema it names. `migrate_roster_schema_1_to_2` writes
// six gains and always will; had it been written against the live
// `growable_stat_count` it would now emit ten and claim to be schema 2, and the
// 0 -> 1 -> 2 -> 3 chain would produce a record no reader could believe.
[[nodiscard]] bool migrate_roster_schema_2_to_3(
    const SectionMigrationContext& context,
    const std::vector<std::uint8_t>& before,
    std::vector<std::uint8_t>& after
);

// Every section step this build ships, registered in one place so that "which
// saves can this build read?" has one answer and not one per caller.
//
// Content steps are deliberately absent. A rename belongs to the package that
// performed it, and the engine has no way to know that one game renamed a unit
// type. So a host registers its packages' content migrations onto the registry
// this returns, and the engine ships the seam rather than the contents.
[[nodiscard]] SaveMigrationRegistry standard_save_migrations();

}  // namespace grandleon::campaign
