// SPDX-License-Identifier: MIT
#include <grandleon/campaign/migration.hpp>

#include <grandleon/core/bounds.hpp>

#include <algorithm>
#include <utility>

namespace grandleon::campaign {
namespace {

// The section layouts these steps read and write are `save.hpp`'s, and the
// arithmetic is the same fixed-width little-endian arithmetic. It is spelled
// again here rather than shared with `save.cpp` on purpose: a migration reads a
// layout that no longer exists, so borrowing the current reader would be
// borrowing exactly the thing that moved. A step must be able to outlive the
// code that used to write its input.

constexpr std::size_t definition_ref_size = 28;
// `roster` schema 0: id, definition, availability, one reserved byte, level.
constexpr std::size_t roster_v0_record_size = 8 + definition_ref_size + 1 + 1 + 2;
// Everything in a schema 1 member record before the carried-stack count: the
// identity, the definition, availability, level and experience.
constexpr std::size_t roster_v1_fixed_size =
    8 + definition_ref_size + 1 + 1 + 2 + 4;
// How many gains a schema 2 member record carries. Frozen at six on purpose:
// a step produces the schema it names, forever, and one written against
// `growable_stat_count` would start emitting a schema it does not claim the
// moment the stat line grew, which it since has. The 2 -> 3 step below is
// where the extra four appear.
constexpr std::size_t roster_v2_growable_stat_count = 6;
// Everything in a schema 2 member record before the carried-stack count: the
// schema 1 fixed run, plus one 2-byte gain per stat that schema knew.
constexpr std::size_t roster_v2_fixed_size =
    roster_v1_fixed_size + (2 * roster_v2_growable_stat_count);
// One inventory stack: a definition reference and a quantity, as `save.cpp`
// writes it.
constexpr std::size_t stack_record_size = definition_ref_size + 4;

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

// The two readers below bound themselves rather than trusting the cursor they
// are handed. Every step in this file happens to keep its cursor inside the
// section it is walking, so a subtraction here would work until the step
// somebody adds tomorrow does not, and a file whose entire purpose is to be
// added to cannot rest a memory-safety property on the discipline of its
// current call sites.
[[nodiscard]] bool read_u16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t& value
) noexcept {
    if (!core::checked_region(bytes.size(), offset, 2U)) {
        return false;
    }
    value = static_cast<std::uint16_t>(
        bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U)
    );
    return true;
}

[[nodiscard]] bool read_u32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t& value
) noexcept {
    if (!core::checked_region(bytes.size(), offset, 4U)) {
        return false;
    }
    value = 0;
    for (unsigned index = 0; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return true;
}

[[nodiscard]] bool package_below(
    const core::PackageId& lhs,
    const core::PackageId& rhs
) noexcept {
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index] != rhs[index]) {
            return lhs[index] < rhs[index];
        }
    }
    return false;
}

MigrationReport section_failure(
    MigrationError error,
    std::uint32_t section,
    std::uint32_t from,
    std::uint32_t to,
    std::vector<AppliedMigration> applied
) {
    MigrationReport report;
    report.error = error;
    report.section = section;
    report.from = from;
    report.to = to;
    report.applied = std::move(applied);
    return report;
}

MigrationReport package_failure(
    MigrationError error,
    const core::PackageId& package,
    std::uint32_t from,
    std::uint32_t to,
    std::vector<AppliedMigration> applied
) {
    MigrationReport report;
    report.error = error;
    report.package = package;
    report.from = from;
    report.to = to;
    report.applied = std::move(applied);
    return report;
}

// Rebuild a decoded save's byte buffer out of the section bodies a migration
// produced, keeping the four-byte alignment the format states and the offsets
// the views promise.
//
// Only the sections are rebuilt. The header and the package table are already
// parsed into fields and are handed to `interpret_save` as fields; the buffer
// exists so that a section view stays a range rather than a copy, and nothing
// downstream of the seam reads the metadata out of it again.
void rebuild_sections(
    DecodedSave& decoded,
    const std::vector<std::vector<std::uint8_t>>& bodies
) {
    decoded.bytes.clear();
    for (std::size_t index = 0; index < bodies.size(); ++index) {
        while ((decoded.bytes.size() % save_section_alignment) != 0U) {
            decoded.bytes.push_back(0);
        }
        SaveSectionView& view = decoded.sections[index];
        view.offset = static_cast<std::uint32_t>(decoded.bytes.size());
        view.size = static_cast<std::uint32_t>(bodies[index].size());
        decoded.bytes.insert(
            decoded.bytes.end(), bodies[index].begin(), bodies[index].end()
        );
    }
}

// ---------------------------------------------------------------------------
// Repointing references
// ---------------------------------------------------------------------------

// Resolve one reference through the renames, and require the result to exist
// when the package that owns it told us what it declares.
[[nodiscard]] MigrationError repoint(
    const DefinitionRenameTable& renames,
    const MountedContent& content,
    DefinitionRef& reference
) {
    const MountedPackage* const mounted = content.find(reference.package_id);
    if (mounted != nullptr && mounted->definitions != nullptr) {
        DefinitionRef resolved{};
        const IdentityError error =
            renames.resolve_in(*mounted->definitions, reference, resolved);
        if (error != IdentityError::none) {
            return MigrationError::missing_definition;
        }
        reference = resolved;
        return MigrationError::none;
    }
    reference = renames.resolve(reference);
    return MigrationError::none;
}

// Put a collection back into its stated canonical order after a rename moved
// its keys, and refuse rather than merge when two keys became one.
template <typename Record, typename Key>
[[nodiscard]] bool reorder_unique(std::vector<Record>& records, Key key) {
    std::sort(
        records.begin(),
        records.end(),
        [&key](const Record& lhs, const Record& rhs) {
            return definition_ref_less(key(lhs), key(rhs));
        }
    );
    for (std::size_t index = 1; index < records.size(); ++index) {
        if (key(records[index - 1]) == key(records[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Mounted content
// ---------------------------------------------------------------------------

void MountedContent::mount(const MountedPackage& package) {
    const auto position = std::lower_bound(
        packages_.begin(),
        packages_.end(),
        package.package,
        [](const MountedPackage& entry, const core::PackageId& identity) {
            return package_below(entry.package, identity);
        }
    );
    if (position != packages_.end() && position->package == package.package) {
        *position = package;
        return;
    }
    packages_.insert(position, package);
}

const MountedPackage* MountedContent::find(
    const core::PackageId& package
) const noexcept {
    const auto position = std::lower_bound(
        packages_.begin(),
        packages_.end(),
        package,
        [](const MountedPackage& entry, const core::PackageId& identity) {
            return package_below(entry.package, identity);
        }
    );
    if (position == packages_.end() || !(position->package == package)) {
        return nullptr;
    }
    return &*position;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

std::string_view migration_error_name(MigrationError error) noexcept {
    switch (error) {
        case MigrationError::none: return "none";
        case MigrationError::unmounted_package: return "unmounted_package";
        case MigrationError::downgrade_refused: return "downgrade_refused";
        case MigrationError::missing_step: return "missing_step";
        case MigrationError::step_failed: return "step_failed";
        case MigrationError::step_limit_exceeded: return "step_limit_exceeded";
        case MigrationError::invalid_rename: return "invalid_rename";
        case MigrationError::missing_definition: return "missing_definition";
        case MigrationError::rename_collision: return "rename_collision";
        case MigrationError::invalid_result: return "invalid_result";
    }
    return "unknown";
}

bool operator==(const AppliedMigration& lhs, const AppliedMigration& rhs) noexcept {
    return lhs.content == rhs.content && lhs.section == rhs.section &&
           lhs.package == rhs.package && lhs.from == rhs.from && lhs.to == rhs.to;
}

// ---------------------------------------------------------------------------
// The registry
// ---------------------------------------------------------------------------

bool SaveMigrationRegistry::add_section_migration(
    SaveSectionType section,
    std::uint16_t from_major,
    SectionMigrationFunction apply
) {
    if (apply == nullptr ||
        find_section_migration(section, from_major) != nullptr) {
        return false;
    }
    sections_.push_back({section, from_major, apply});
    return true;
}

bool SaveMigrationRegistry::add_content_migration(
    const core::PackageId& package,
    std::uint32_t from_revision,
    ContentMigrationFunction apply
) {
    if (apply == nullptr ||
        find_content_migration(package, from_revision) != nullptr) {
        return false;
    }
    contents_.push_back({package, from_revision, apply});
    return true;
}

SectionMigrationFunction SaveMigrationRegistry::find_section_migration(
    SaveSectionType section,
    std::uint16_t from_major
) const noexcept {
    for (const SectionEntry& entry : sections_) {
        if (entry.section == section && entry.from_major == from_major) {
            return entry.apply;
        }
    }
    return nullptr;
}

ContentMigrationFunction SaveMigrationRegistry::find_content_migration(
    const core::PackageId& package,
    std::uint32_t from_revision
) const noexcept {
    for (const ContentEntry& entry : contents_) {
        if (entry.package == package && entry.from_revision == from_revision) {
            return entry.apply;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Planning
// ---------------------------------------------------------------------------

MigrationReport plan_section_migration(
    const SaveMigrationRegistry& registry,
    SaveSectionType section,
    std::uint16_t from_major,
    std::uint16_t to_major
) {
    const auto type = static_cast<std::uint32_t>(section);
    if (from_major > to_major) {
        return section_failure(
            MigrationError::downgrade_refused, type, from_major, to_major, {}
        );
    }
    if (static_cast<std::uint32_t>(to_major - from_major) >
        maximum_migration_steps) {
        return section_failure(
            MigrationError::step_limit_exceeded, type, from_major, to_major, {}
        );
    }

    MigrationReport report;
    report.section = type;
    report.from = from_major;
    report.to = to_major;
    for (std::uint16_t version = from_major; version < to_major; ++version) {
        // One version at a time, and the hole is named rather than jumped.
        if (registry.find_section_migration(section, version) == nullptr) {
            return section_failure(
                MigrationError::missing_step,
                type,
                version,
                static_cast<std::uint32_t>(version + 1),
                std::move(report.applied)
            );
        }
        AppliedMigration step;
        step.section = type;
        step.from = version;
        step.to = static_cast<std::uint32_t>(version + 1);
        report.applied.push_back(step);
    }
    return report;
}

MigrationReport plan_content_migration(
    const SaveMigrationRegistry& registry,
    const core::PackageId& package,
    std::uint32_t from_revision,
    std::uint32_t to_revision
) {
    if (from_revision > to_revision) {
        return package_failure(
            MigrationError::downgrade_refused,
            package,
            from_revision,
            to_revision,
            {}
        );
    }
    if (to_revision - from_revision > maximum_migration_steps) {
        return package_failure(
            MigrationError::step_limit_exceeded,
            package,
            from_revision,
            to_revision,
            {}
        );
    }

    MigrationReport report;
    report.package = package;
    report.from = from_revision;
    report.to = to_revision;
    for (std::uint32_t revision = from_revision; revision < to_revision;
         ++revision) {
        if (registry.find_content_migration(package, revision) == nullptr) {
            return package_failure(
                MigrationError::missing_step,
                package,
                revision,
                revision + 1U,
                std::move(report.applied)
            );
        }
        AppliedMigration step;
        step.content = true;
        step.package = package;
        step.from = revision;
        step.to = revision + 1U;
        report.applied.push_back(step);
    }
    return report;
}

// ---------------------------------------------------------------------------
// Running the section axis
// ---------------------------------------------------------------------------

MigratedSections migrate_sections(
    const DecodedSave& decoded,
    const SaveMigrationRegistry& registry,
    const SaveLoadOptions& options
) {
    MigratedSections result;
    // The candidate. Everything below writes here and nothing writes to
    // `decoded`, so a refusal at any step leaves the caller's save untouched
    // and leaves nothing half-upgraded to reach for.
    DecodedSave candidate;
    candidate.header = decoded.header;
    candidate.packages = decoded.packages;
    candidate.sections = decoded.sections;

    std::vector<std::vector<std::uint8_t>> bodies;
    bodies.reserve(decoded.sections.size());

    SectionMigrationContext context;
    context.header = decoded.header;
    context.packages = &decoded.packages;
    context.limits = &options.limits;

    for (std::size_t index = 0; index < decoded.sections.size(); ++index) {
        const SaveSectionView& view = decoded.sections[index];
        const auto begin =
            decoded.bytes.begin() + static_cast<std::ptrdiff_t>(view.offset);
        std::vector<std::uint8_t> body(
            begin, begin + static_cast<std::ptrdiff_t>(view.size)
        );

        // A section this build does not know is a section it cannot upgrade.
        // `interpret_save` retains or refuses it by the rule it already has.
        if (!is_known_save_section(view.type)) {
            bodies.push_back(std::move(body));
            continue;
        }

        const auto type = static_cast<SaveSectionType>(view.type);
        const std::uint16_t target = save_section_schema(type).major;
        MigrationReport plan =
            plan_section_migration(registry, type, view.schema_major, target);
        if (!plan) {
            plan.applied = std::move(result.report.applied);
            result.report = std::move(plan);
            return result;
        }

        for (const AppliedMigration& step : plan.applied) {
            context.from_major = static_cast<std::uint16_t>(step.from);
            const SectionMigrationFunction apply = registry.find_section_migration(
                type, context.from_major
            );
            std::vector<std::uint8_t> upgraded;
            if (apply == nullptr || !apply(context, body, upgraded)) {
                result.report = section_failure(
                    MigrationError::step_failed,
                    view.type,
                    step.from,
                    step.to,
                    std::move(result.report.applied)
                );
                return result;
            }
            body = std::move(upgraded);
            result.report.applied.push_back(step);
        }

        candidate.sections[index].schema_major = target;
        if (view.schema_major != target) {
            // A step delivers the first minor of the version it produces. The
            // build's own minor belongs to the build's own writer, and a
            // migrated save claiming it would be claiming fields no step wrote.
            candidate.sections[index].schema_minor = 0;
        }
        bodies.push_back(std::move(body));
    }

    rebuild_sections(candidate, bodies);
    result.decoded = std::move(candidate);
    return result;
}

// ---------------------------------------------------------------------------
// Running the content axis
// ---------------------------------------------------------------------------

MigratedContent migrate_content(
    const CampaignSave& candidate,
    const SaveMigrationRegistry& registry,
    const MountedContent& content
) {
    MigratedContent result;
    CampaignSave migrated = candidate;

    // One table for every package, because a reference carries the package it
    // belongs to and a rename cannot cross that boundary.
    DefinitionRenameTable renames;
    for (SavePackageRequirement& requirement : migrated.packages) {
        const MountedPackage* const mounted = content.find(requirement.package);
        if (mounted == nullptr) {
            result.report = package_failure(
                MigrationError::unmounted_package,
                requirement.package,
                requirement.content_revision,
                0,
                std::move(result.report.applied)
            );
            return result;
        }

        MigrationReport plan = plan_content_migration(
            registry,
            requirement.package,
            requirement.content_revision,
            mounted->content_revision
        );
        if (!plan) {
            plan.applied = std::move(result.report.applied);
            result.report = std::move(plan);
            return result;
        }

        for (const AppliedMigration& step : plan.applied) {
            ContentMigrationContext context;
            context.package = requirement.package;
            context.from_revision = step.from;
            context.definitions = mounted->definitions;

            DefinitionRenameTable declared;
            const ContentMigrationFunction apply =
                registry.find_content_migration(requirement.package, step.from);
            if (apply == nullptr || !apply(context, declared)) {
                result.report = package_failure(
                    MigrationError::step_failed,
                    requirement.package,
                    step.from,
                    step.to,
                    std::move(result.report.applied)
                );
                return result;
            }
            // Folded into the running table one revision at a time, so a
            // definition renamed twice across two revisions is followed rather
            // than flattened by hand. `DefinitionRenameTable` refuses a cycle
            // and a duplicate source when they are declared, which is what
            // makes `resolve` terminate.
            if (renames.merge(declared) != IdentityError::none) {
                result.report = package_failure(
                    MigrationError::invalid_rename,
                    requirement.package,
                    step.from,
                    step.to,
                    std::move(result.report.applied)
                );
                return result;
            }
            result.report.applied.push_back(step);
        }

        if (!plan.applied.empty()) {
            // The candidate now describes the content that is actually there.
            requirement.content_revision = mounted->content_revision;
            requirement.integrity = mounted->integrity;
        }
    }

    // Repoint everything the campaign names. Persistent identities, progression,
    // availability, quantities and results are all left exactly where they are:
    // a rename table has no way to reach them.
    MigrationError error = MigrationError::none;
    const auto point = [&](DefinitionRef& reference) {
        if (error != MigrationError::none) {
            return;
        }
        error = repoint(renames, content, reference);
    };

    for (PersistentUnit& unit : migrated.state.units) {
        point(unit.definition);
        for (InventoryStack& stack : unit.carried) {
            point(stack.item);
        }
    }
    for (InventoryStack& stack : migrated.state.store) {
        point(stack.item);
    }
    for (ObjectiveRecord& record : migrated.state.objectives) {
        point(record.objective);
    }
    for (WorldFlag& flag : migrated.state.world) {
        point(flag.key);
    }
    if (migrated.state.progress.active) {
        point(migrated.state.progress.campaign);
        point(migrated.state.progress.active_node);
        for (ProgressionEntry& entry : migrated.state.progress.history) {
            point(entry.node);
        }
    }
    if (error != MigrationError::none) {
        result.report.error = error;
        return result;
    }

    // A rename moves a key, and a moved key is out of order. Every collection
    // whose order is stated goes back into it; `progress.history` does not,
    // because there the order is the data.
    bool unique = true;
    for (PersistentUnit& unit : migrated.state.units) {
        unique = reorder_unique(
                     unit.carried,
                     [](const InventoryStack& stack) { return stack.item; }
                 ) &&
                 unique;
    }
    unique = reorder_unique(
                 migrated.state.store,
                 [](const InventoryStack& stack) { return stack.item; }
             ) &&
             unique;
    unique = reorder_unique(
                 migrated.state.objectives,
                 [](const ObjectiveRecord& record) { return record.objective; }
             ) &&
             unique;
    unique = reorder_unique(
                 migrated.state.world,
                 [](const WorldFlag& flag) { return flag.key; }
             ) &&
             unique;
    if (!unique) {
        result.report.error = MigrationError::rename_collision;
        return result;
    }

    // The same whole-state check a commit and a load both go through. A
    // migration that produced an arrangement no operation could reach is a
    // migration that does not run.
    const StateError state_error = validate(migrated.state);
    if (state_error != StateError::none) {
        result.report.error = MigrationError::invalid_result;
        result.report.state_error = state_error;
        return result;
    }

    result.save = std::move(migrated);
    return result;
}

// ---------------------------------------------------------------------------
// The pipeline
// ---------------------------------------------------------------------------

MigratedLoad load_campaign_migrated(
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options,
    const SaveMigrationRegistry& registry,
    const MountedContent& content
) {
    MigratedLoad result;
    SaveDecodeResult decoded = decode_save_envelope(bytes, options);
    if (!decoded) {
        result.load.error = decoded.error;
        result.load.section = decoded.section;
        return result;
    }

    MigratedSections sections =
        migrate_sections(decoded.decoded, registry, options);
    result.migration = sections.report;
    if (!sections.report) {
        return result;
    }

    result.load = interpret_save(std::move(sections.decoded), options);
    if (!result.load) {
        return result;
    }

    MigratedContent reconciled =
        migrate_content(result.load.save, registry, content);
    // One report for the whole pipeline: the section steps, then the content
    // steps, in the order they ran.
    for (const AppliedMigration& step : reconciled.report.applied) {
        result.migration.applied.push_back(step);
    }
    result.migration.error = reconciled.report.error;
    result.migration.state_error = reconciled.report.state_error;
    if (!reconciled.report) {
        result.migration.section = reconciled.report.section;
        result.migration.package = reconciled.report.package;
        result.migration.from = reconciled.report.from;
        result.migration.to = reconciled.report.to;
        return result;
    }
    result.save = std::move(reconciled.save);
    return result;
}

MigratedLoad load_campaign_migrated_into(
    CampaignSave& live,
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options,
    const SaveMigrationRegistry& registry,
    const MountedContent& content
) {
    MigratedLoad result =
        load_campaign_migrated(bytes, options, registry, content);
    if (!result) {
        return result;
    }
    live = result.save;
    return result;
}

// ---------------------------------------------------------------------------
// The steps this build ships
// ---------------------------------------------------------------------------

bool migrate_roster_schema_0_to_1(
    const SectionMigrationContext& context,
    const std::vector<std::uint8_t>& before,
    std::vector<std::uint8_t>& after
) {
    std::uint32_t count = 0;
    if (before.size() < 4U || !read_u32(before, 0, count)) {
        return false;
    }
    const std::uint32_t cap =
        context.limits != nullptr ? context.limits->maximum_units : 0U;
    // The same bound the reader applies, applied before a byte is copied: a
    // count is refused against the bytes that remain and against the caller's
    // cap, never trusted because it appeared in a header.
    if (count > cap ||
        static_cast<std::size_t>(count) >
            (before.size() - 4U) / roster_v0_record_size) {
        return false;
    }
    if (before.size() != 4U + static_cast<std::size_t>(count) * roster_v0_record_size) {
        return false;
    }

    after.clear();
    put_u32(after, count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t record =
            4U + static_cast<std::size_t>(index) * roster_v0_record_size;
        after.insert(
            after.end(),
            before.begin() + static_cast<std::ptrdiff_t>(record),
            before.begin() + static_cast<std::ptrdiff_t>(record + 8U + definition_ref_size)
        );

        const std::uint8_t availability = before[record + 36U];
        const std::uint8_t reserved = before[record + 37U];
        std::uint16_t level = 0;
        if (reserved != 0U || !read_u16(before, record + 38U, level)) {
            return false;
        }
        put_u8(after, availability);
        put_u8(after, 0);
        put_u16(after, level);
        // The two fields schema 1 added, at the values a schema 0 campaign
        // actually had: it recorded no experience, and it held its whole stock
        // in the shared store rather than on its members.
        put_u32(after, 0);
        put_u32(after, 0);
    }
    return true;
}

bool migrate_roster_schema_1_to_2(
    const SectionMigrationContext& context,
    const std::vector<std::uint8_t>& before,
    std::vector<std::uint8_t>& after
) {
    std::uint32_t count = 0;
    if (before.size() < 4U || !read_u32(before, 0, count)) {
        return false;
    }
    const std::uint32_t cap =
        context.limits != nullptr ? context.limits->maximum_units : 0U;
    if (count > cap) {
        return false;
    }
    const std::uint32_t stack_cap =
        context.limits != nullptr ? context.limits->maximum_stacks : 0U;

    after.clear();
    put_u32(after, count);
    std::size_t cursor = 4U;
    for (std::uint32_t index = 0; index < count; ++index) {
        // Everything up to and including the experience field is unchanged, so
        // it is copied rather than decoded: a migration that re-derived fields
        // it was not moving would be a second writer of the old format.
        if (!core::checked_region(
                before.size(), cursor, roster_v1_fixed_size
            )) {
            return false;
        }
        after.insert(
            after.end(),
            before.begin() + static_cast<std::ptrdiff_t>(cursor),
            before.begin() +
                static_cast<std::ptrdiff_t>(cursor + roster_v1_fixed_size)
        );
        cursor += roster_v1_fixed_size;
        // The field schema 2 added, at the value a schema 1 campaign actually
        // had. It never invents a number: a campaign whose level-ups predate
        // growth rolled nothing, so its members gained nothing, and a save that
        // guessed otherwise would hand a player stats they never earned.
        for (std::size_t stat = 0; stat < roster_v2_growable_stat_count;
             ++stat) {
            put_u16(after, 0);
        }
        std::uint32_t stacks = 0;
        if (!read_u32(before, cursor, stacks) || stacks > stack_cap ||
            static_cast<std::size_t>(stacks) >
                (before.size() - cursor - 4U) / stack_record_size) {
            return false;
        }
        const std::size_t tail =
            4U + static_cast<std::size_t>(stacks) * stack_record_size;
        after.insert(
            after.end(),
            before.begin() + static_cast<std::ptrdiff_t>(cursor),
            before.begin() + static_cast<std::ptrdiff_t>(cursor + tail)
        );
        cursor += tail;
    }
    // A roster section is exactly its members and nothing after them. Trailing
    // bytes are a section that is not the section it claims to be, and are
    // refused rather than dropped.
    return cursor == before.size();
}

bool migrate_roster_schema_2_to_3(
    const SectionMigrationContext& context,
    const std::vector<std::uint8_t>& before,
    std::vector<std::uint8_t>& after
) {
    std::uint32_t count = 0;
    if (before.size() < 4U || !read_u32(before, 0, count)) {
        return false;
    }
    const std::uint32_t cap =
        context.limits != nullptr ? context.limits->maximum_units : 0U;
    if (count > cap) {
        return false;
    }
    const std::uint32_t stack_cap =
        context.limits != nullptr ? context.limits->maximum_stacks : 0U;

    after.clear();
    put_u32(after, count);
    std::size_t cursor = 4U;
    for (std::uint32_t index = 0; index < count; ++index) {
        // Everything up to and including the six gains schema 2 knew is copied
        // rather than decoded, for the reason the step before it copies: a
        // migration that re-derived a field it was not moving would be a second
        // writer of the old format.
        if (!core::checked_region(
                before.size(), cursor, roster_v2_fixed_size
            )) {
            return false;
        }
        after.insert(
            after.end(),
            before.begin() + static_cast<std::ptrdiff_t>(cursor),
            before.begin() +
                static_cast<std::ptrdiff_t>(cursor + roster_v2_fixed_size)
        );
        cursor += roster_v2_fixed_size;
        // The four gains schema 3 added, at the value a schema 2 campaign
        // provably had. Skill, luck, evasion and magic were not growable when
        // those levels were taken, so no roll ever granted a point in them and
        // zero is the honest number rather than the convenient one.
        for (std::size_t stat = roster_v2_growable_stat_count;
             stat < growable_stat_count; ++stat) {
            put_u16(after, 0);
        }
        std::uint32_t stacks = 0;
        if (!read_u32(before, cursor, stacks) || stacks > stack_cap ||
            static_cast<std::size_t>(stacks) >
                (before.size() - cursor - 4U) / stack_record_size) {
            return false;
        }
        const std::size_t tail =
            4U + static_cast<std::size_t>(stacks) * stack_record_size;
        after.insert(
            after.end(),
            before.begin() + static_cast<std::ptrdiff_t>(cursor),
            before.begin() + static_cast<std::ptrdiff_t>(cursor + tail)
        );
        cursor += tail;
    }
    // A roster section is exactly its members and nothing after them.
    return cursor == before.size();
}

SaveMigrationRegistry standard_save_migrations() {
    SaveMigrationRegistry registry;
    bool added = registry.add_section_migration(
        SaveSectionType::roster, 0, migrate_roster_schema_0_to_1
    );
    added = registry.add_section_migration(
                SaveSectionType::roster, 1, migrate_roster_schema_1_to_2
            ) &&
            added;
    added = registry.add_section_migration(
                SaveSectionType::roster, 2, migrate_roster_schema_2_to_3
            ) &&
            added;
    static_cast<void>(added);
    return registry;
}

}  // namespace grandleon::campaign
