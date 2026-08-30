// SPDX-License-Identifier: MIT
#include <grandleon/campaign/migration.hpp>
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/save.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace campaign = grandleon::campaign;
namespace core = grandleon::core;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

core::PackageId package(std::uint8_t marker) {
    core::PackageId identity{};
    identity[15] = marker;
    return identity;
}

campaign::DefinitionRef ref(
    std::uint8_t marker,
    core::ContentCategory category,
    std::uint64_t stable
) {
    return {package(marker), category, stable};
}

// The two fixtures, read as bytes off the disk exactly as a save would be read
// off a card. Committed rather than generated at test time: the point of a
// golden is that it does not move when the code around it does.
std::vector<std::uint8_t> fixture(std::string_view name) {
    std::string path = GRANDLEON_CAMPAIGN_FIXTURE_DIR;
    path += '/';
    path.append(name);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "FAIL: cannot open fixture " << path << '\n';
        ++failures;
        return {};
    }
    return std::vector<std::uint8_t>{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
}

// ---------------------------------------------------------------------------
// Steps the tests register, which the engine does not ship
// ---------------------------------------------------------------------------

// A step that does nothing but succeed. Enough to prove which steps the planner
// chose and in which order, which is a question about the chain and not about
// any particular byte.
bool identity_step(
    const campaign::SectionMigrationContext&,
    const std::vector<std::uint8_t>& before,
    std::vector<std::uint8_t>& after
) {
    after = before;
    return true;
}

bool refusing_step(
    const campaign::SectionMigrationContext&,
    const std::vector<std::uint8_t>&,
    std::vector<std::uint8_t>&
) {
    return false;
}

// The demonstration rename: the fixture package's revision 2 renamed one unit
// type and one item, and says so. A package's renames are the package's, which
// is why the engine ships the seam and a host registers what went through it.
bool fixture_package_renames_at_revision_1(
    const campaign::ContentMigrationContext& context,
    campaign::DefinitionRenameTable& renames
) {
    const bool unit = renames.add(
                          {{context.package, core::ContentCategory::unit_type, 0x1101},
                           {context.package, core::ContentCategory::unit_type, 0x1201}}
                      ) == campaign::IdentityError::none;
    const bool item = renames.add(
                          {{context.package, core::ContentCategory::item, 0x2101},
                           {context.package, core::ContentCategory::item, 0x2301}}
                      ) == campaign::IdentityError::none;
    return unit && item;
}

// Two distinct items onto one. Legal as a table and impossible as a campaign:
// the migrated roster would hold one key twice, and choosing which quantity
// survives is a guess.
bool colliding_renames(
    const campaign::ContentMigrationContext& context,
    campaign::DefinitionRenameTable& renames
) {
    const bool unit = renames.add(
                          {{context.package, core::ContentCategory::unit_type, 0x1101},
                           {context.package, core::ContentCategory::unit_type, 0x1201}}
                      ) == campaign::IdentityError::none;
    const bool item = renames.add(
                          {{context.package, core::ContentCategory::item, 0x2101},
                           {context.package, core::ContentCategory::item, 0x2105}}
                      ) == campaign::IdentityError::none;
    return unit && item;
}

// A rename whose target the mounted content does not declare.
bool renames_into_nothing(
    const campaign::ContentMigrationContext& context,
    campaign::DefinitionRenameTable& renames
) {
    return renames.add(
               {{context.package, core::ContentCategory::unit_type, 0x1101},
                {context.package, core::ContentCategory::unit_type, 0x9999}}
           ) == campaign::IdentityError::none;
}

bool refusing_content_step(
    const campaign::ContentMigrationContext&,
    campaign::DefinitionRenameTable&
) {
    return false;
}

// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------

// Version by version, and a hole is a hole rather than a leap.
void the_registry_walks_one_version_at_a_time() {
    campaign::SaveMigrationRegistry registry;
    expect(
        registry.add_section_migration(
            campaign::SaveSectionType::world, 0, identity_step
        ),
        "a step registers"
    );
    expect(
        registry.add_section_migration(
            campaign::SaveSectionType::world, 1, identity_step
        ),
        "and so does the next one"
    );
    expect(
        !registry.add_section_migration(
            campaign::SaveSectionType::world, 1, refusing_step
        ),
        "but a second claim on one version is refused rather than overwritten"
    );
    expect(
        !registry.add_section_migration(
            campaign::SaveSectionType::world, 2, nullptr
        ),
        "and so is a step that is not a step"
    );

    const campaign::MigrationReport two = campaign::plan_section_migration(
        registry, campaign::SaveSectionType::world, 0, 2
    );
    expect(static_cast<bool>(two), "a two-version path is a path");
    expect(two.applied.size() == 2U, "walked in two steps rather than one leap");
    expect(
        two.applied.size() == 2U && two.applied[0].from == 0U &&
            two.applied[0].to == 1U && two.applied[1].from == 1U &&
            two.applied[1].to == 2U,
        "each step going exactly one version, in order"
    );
    expect(
        two.applied.size() == 2U && !two.applied[0].content &&
            two.applied[0].section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::world),
        "and naming the section it upgraded"
    );

    const campaign::MigrationReport gap = campaign::plan_section_migration(
        registry, campaign::SaveSectionType::world, 0, 3
    );
    expect(
        gap.error == campaign::MigrationError::missing_step,
        "a hole in the chain is refused"
    );
    expect(
        gap.from == 2U && gap.to == 3U,
        "naming the version nothing leads out of, not the whole journey"
    );

    expect(
        campaign::plan_section_migration(
            registry, campaign::SaveSectionType::world, 2, 0
        ).error == campaign::MigrationError::downgrade_refused,
        "and going backwards is refused by name rather than attempted"
    );
    expect(
        static_cast<bool>(campaign::plan_section_migration(
            registry, campaign::SaveSectionType::world, 2, 2
        )),
        "while a save already at this version needs nothing"
    );
    expect(
        campaign::plan_section_migration(
            registry,
            campaign::SaveSectionType::world,
            0,
            static_cast<std::uint16_t>(campaign::maximum_migration_steps + 1U)
        ).error == campaign::MigrationError::step_limit_exceeded,
        "a chain longer than the cap costs a comparison, not a search"
    );

    // The content axis answers the same three ways.
    campaign::SaveMigrationRegistry content;
    expect(
        content.add_content_migration(
            package(2), 1, 2, fixture_package_renames_at_revision_1
        ),
        "a content step registers"
    );
    expect(
        !content.add_content_migration(
            package(2), 1, 2, fixture_package_renames_at_revision_1
        ),
        "and cannot be registered twice"
    );
    const campaign::MigrationReport one =
        campaign::plan_content_migration(content, package(2), 1, 2);
    expect(
        static_cast<bool>(one) && one.applied.size() == 1U &&
            one.applied[0].content && one.applied[0].package == package(2),
        "one revision is one step, and it says which package"
    );
    expect(
        campaign::plan_content_migration(content, package(2), 1, 3).error ==
            campaign::MigrationError::missing_step,
        "two revisions with only one step registered is a hole"
    );
    expect(
        campaign::plan_content_migration(content, package(2), 2, 1).error ==
            campaign::MigrationError::downgrade_refused,
        "and mounted content older than the save is a downgrade"
    );
}

// ---------------------------------------------------------------------------
// Golden fixture one: the section-schema axis
// ---------------------------------------------------------------------------

campaign::MountedContent fixture_one_content() {
    campaign::MountedContent content;
    campaign::MountedPackage mounted;
    mounted.package = package(1);
    mounted.content_revision = 1;
    mounted.integrity = 0xa1a2a3a4b1b2b3b4ULL;
    content.mount(mounted);
    return content;
}

void a_save_at_an_older_section_schema_loads_through_the_registry() {
    const std::vector<std::uint8_t> bytes = fixture("roster_schema_0.glsv");
    const campaign::SaveLoadOptions options;

    // Without a migration, this is exactly the refusal `save.hpp` says a
    // migration exists to prevent, raised after the seam rather than before it.
    const campaign::SaveLoadResult unmigrated =
        campaign::load_campaign(bytes, options);
    expect(
        unmigrated.error == campaign::SaveError::unsupported_schema &&
            unmigrated.section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster),
        "a build with no registry refuses the old schema and names the section"
    );

    // And with an empty registry the registry itself refuses, earlier and with
    // its own word: there is no path, and it says which version has none.
    const campaign::MigratedLoad unregistered = campaign::load_campaign_migrated(
        bytes, options, campaign::SaveMigrationRegistry{}, fixture_one_content()
    );
    expect(
        unregistered.migration.error == campaign::MigrationError::missing_step &&
            unregistered.migration.from == 0U,
        "an empty registry names the version it has no step out of"
    );

    const campaign::MigratedLoad loaded = campaign::load_campaign_migrated(
        bytes,
        options,
        campaign::standard_save_migrations(),
        fixture_one_content()
    );
    expect(
        static_cast<bool>(loaded),
        std::string_view{campaign::migration_error_name(loaded.migration.error)}
    );
    expect(
        static_cast<bool>(loaded.load),
        std::string_view{campaign::save_error_name(loaded.load.error)}
    );
    // The chain, walked one version at a time and never jumped. This fixture
    // was written at schema 0 and the build reads schema 3, so all three
    // registered steps run, in order, and the report names each of them. The
    // fixture is never regenerated: it is the only thing that proves a save
    // older than every step this build ships still becomes a campaign.
    expect(
        loaded.migration.applied.size() == 3U &&
            loaded.migration.applied[0].section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster) &&
            loaded.migration.applied[0].from == 0U &&
            loaded.migration.applied[0].to == 1U &&
            loaded.migration.applied[1].section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster) &&
            loaded.migration.applied[1].from == 1U &&
            loaded.migration.applied[1].to == 2U &&
            loaded.migration.applied[2].section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster) &&
            loaded.migration.applied[2].from == 2U &&
            loaded.migration.applied[2].to == 3U,
        "all three steps ran, one version at a time, and the report says which"
    );

    // Field by field, because a hash comparison is the weaker claim and the one
    // a bug could satisfy by accident.
    const campaign::CampaignSave& save = loaded.save;
    expect(
        save.header.engine.major == 0U && save.header.engine.minor == 0U &&
            save.header.engine.patch == 9U,
        "the save still says which engine wrote it, which was not this one"
    );
    expect(
        save.packages.size() == 1U && save.packages[0].package == package(1) &&
            save.packages[0].content_revision == 1U,
        "and which package it needs"
    );

    const campaign::CampaignState& state = save.state;
    expect(state.units.size() == 2U, "both members came across");
    if (state.units.size() == 2U) {
        expect(
            state.units[0].id == campaign::PersistentEntityId{1} &&
                state.units[0].definition ==
                    ref(1, core::ContentCategory::unit_type, 0x1001) &&
                state.units[0].availability == campaign::Availability::available &&
                state.units[0].progression.level == 3U,
            "the first with the identity, definition, availability and level "
            "schema 0 recorded"
        );
        expect(
            state.units[0].progression.experience == 0U &&
                state.units[0].carried.empty(),
            "and the documented defaults for the two fields schema 1 added"
        );
        expect(
            state.units[1].id == campaign::PersistentEntityId{2} &&
                state.units[1].definition ==
                    ref(1, core::ContentCategory::unit_type, 0x1002) &&
                state.units[1].availability == campaign::Availability::dead &&
                state.units[1].progression.level == 1U &&
                state.units[1].progression.experience == 0U &&
                state.units[1].carried.empty(),
            "and the member who died stayed dead across the upgrade"
        );
    }

    expect(
        state.store.size() == 2U &&
            state.store[0].item == ref(1, core::ContentCategory::item, 0x2001) &&
            state.store[0].quantity == 5U &&
            state.store[1].item == ref(1, core::ContentCategory::item, 0x2002) &&
            state.store[1].quantity == 2U,
        "the shared store, where a schema 0 campaign kept everything, is intact"
    );
    expect(
        state.objectives.size() == 1U &&
            state.objectives[0].objective ==
                ref(1, core::ContentCategory::objective, 0x3001) &&
            state.objectives[0].result == campaign::ObjectiveOutcome::satisfied,
        "so is the objective it settled"
    );
    expect(
        state.world.size() == 1U &&
            state.world[0].key == ref(1, core::ContentCategory::campaign, 0x4001) &&
            state.world[0].value.type == campaign::WorldValueType::boolean &&
            state.world[0].value.value == 1,
        "so is the typed world value"
    );
    expect(
        state.applied_outcomes.size() == 1U &&
            state.applied_outcomes[0] == campaign::OutcomeId{0x5001},
        "so is the outcome it remembers committing"
    );
    expect(
        state.progress.active &&
            state.progress.campaign ==
                ref(1, core::ContentCategory::campaign, 0x6001) &&
            state.progress.active_node ==
                ref(1, core::ContentCategory::campaign_node, 0x7001) &&
            state.progress.history.size() == 1U &&
            state.progress.history[0].cause.value == 0U,
        "and so is where in its campaign it stood"
    );

    // The upgraded campaign is one this build can write, which is what makes
    // the migration a one-way door rather than a permanent translation layer.
    const campaign::SaveLoadResult rewritten =
        campaign::load_campaign(campaign::save_campaign(save), options);
    expect(
        static_cast<bool>(rewritten) && rewritten.save.state.units.size() == 2U,
        "and saving it again produces a save this build reads with no migration"
    );
}

// ---------------------------------------------------------------------------
// Golden fixture two: the content axis
// ---------------------------------------------------------------------------

// The mounted package at revision 2, declaring what revision 2 has. The two
// identities the fixture names are gone; the ones the rename points at are not.
void declare(
    campaign::DefinitionRegistry& registry,
    core::ContentCategory category,
    std::uint64_t stable,
    std::string_view key
) {
    expect(
        registry.declare(ref(2, category, stable), key) ==
            campaign::IdentityError::none,
        "the mounted content declares what it holds"
    );
}

campaign::DefinitionRegistry fixture_two_definitions() {
    campaign::DefinitionRegistry registry;
    declare(registry, core::ContentCategory::unit_type, 0x1201, "outrider");
    declare(registry, core::ContentCategory::unit_type, 0x1102, "picket");
    declare(registry, core::ContentCategory::item, 0x2301, "field_tonic");
    declare(registry, core::ContentCategory::item, 0x2105, "torch");
    declare(registry, core::ContentCategory::objective, 0x3101, "hold_the_ford");
    declare(registry, core::ContentCategory::campaign, 0x4101, "ford_held");
    declare(registry, core::ContentCategory::campaign, 0x6101, "tarnholt");
    declare(registry, core::ContentCategory::campaign_node, 0x7101, "ford");
    return registry;
}

campaign::MountedContent fixture_two_content(
    std::uint32_t revision,
    const campaign::DefinitionRegistry* definitions
) {
    campaign::MountedContent content;
    campaign::MountedPackage mounted;
    mounted.package = package(2);
    mounted.content_revision = revision;
    mounted.integrity = 0x2222333344445555ULL;
    mounted.definitions = definitions;
    content.mount(mounted);
    return content;
}

void a_renamed_definition_is_repointed_and_nobody_changes_identity() {
    const std::vector<std::uint8_t> bytes = fixture("content_revision_1.glsv");
    const campaign::SaveLoadOptions options;
    const campaign::DefinitionRegistry definitions = fixture_two_definitions();

    campaign::SaveMigrationRegistry registry = campaign::standard_save_migrations();
    expect(
        registry.add_content_migration(
            package(2), 1, 2, fixture_package_renames_at_revision_1
        ),
        "the package's own rename mapping registers"
    );

    const campaign::MigratedLoad loaded = campaign::load_campaign_migrated(
        bytes, options, registry, fixture_two_content(2, &definitions)
    );
    expect(
        static_cast<bool>(loaded),
        std::string_view{campaign::migration_error_name(loaded.migration.error)}
    );
    // Steps on two different axes, and the report keeps them apart. The
    // fixture's roster is at schema 1, so the section axis walks it to 2 and
    // then to 3; its package is at revision 1, so the content axis walks that
    // to 2. Neither axis knows about the other, and the section axis growing a
    // step does not add one to the content axis.
    expect(
        loaded.migration.applied.size() == 3U &&
            !loaded.migration.applied[0].content &&
            loaded.migration.applied[0].section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster) &&
            loaded.migration.applied[0].from == 1U &&
            loaded.migration.applied[0].to == 2U &&
            !loaded.migration.applied[1].content &&
            loaded.migration.applied[1].section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster) &&
            loaded.migration.applied[1].from == 2U &&
            loaded.migration.applied[1].to == 3U &&
            loaded.migration.applied[2].content &&
            loaded.migration.applied[2].from == 1U &&
            loaded.migration.applied[2].to == 2U,
        "two section schema steps and one content revision, kept apart"
    );

    const campaign::CampaignState& state = loaded.save.state;
    expect(state.units.size() == 2U, "the roster is the roster it was");
    if (state.units.size() == 2U) {
        // The requirement, exactly: the definition moved and the person did not.
        expect(
            state.units[0].id == campaign::PersistentEntityId{1},
            "the persistent identity a rename must not touch is untouched"
        );
        expect(
            state.units[0].definition ==
                ref(2, core::ContentCategory::unit_type, 0x1201),
            "and points at the identity the package renamed it to"
        );
        expect(
            state.units[0].progression.level == 4U &&
                state.units[0].progression.experience == 250U,
            "with the progression that accrued to them, not to their type"
        );
        expect(
            state.units[0].progression.gained ==
                std::array<std::uint16_t, campaign::growable_stat_count>{},
            "and three levels' worth of nothing, because a campaign whose "
            "levels predate growth rolled nothing and the migration invents "
            "no number"
        );
        expect(
            state.units[0].carried.size() == 1U &&
                state.units[0].carried[0].item ==
                    ref(2, core::ContentCategory::item, 0x2301) &&
                state.units[0].carried[0].quantity == 2U,
            "and what they carry renamed under them, quantity intact"
        );
        expect(
            state.units[1].id == campaign::PersistentEntityId{2} &&
                state.units[1].definition ==
                    ref(2, core::ContentCategory::unit_type, 0x1102) &&
                state.units[1].availability == campaign::Availability::dead &&
                state.units[1].progression.level == 2U &&
                state.units[1].progression.experience == 90U,
            "a definition the package did not rename is left where it is"
        );
    }

    // The rename moved a key past its neighbour, so the collection is put back
    // into the order it states rather than left in the order it was written.
    expect(
        state.store.size() == 2U &&
            state.store[0].item == ref(2, core::ContentCategory::item, 0x2105) &&
            state.store[0].quantity == 3U &&
            state.store[1].item == ref(2, core::ContentCategory::item, 0x2301) &&
            state.store[1].quantity == 7U,
        "the store is back in canonical order after the rename reordered it"
    );
    expect(
        campaign::validate(state) == campaign::StateError::none,
        "and the migrated campaign is a campaign"
    );
    expect(
        state.objectives.size() == 1U &&
            state.objectives[0].result == campaign::ObjectiveOutcome::failed &&
            state.world.size() == 1U &&
            state.world[0].value.type == campaign::WorldValueType::integer &&
            state.world[0].value.value == -1234 &&
            state.applied_outcomes.size() == 1U &&
            state.applied_outcomes[0] == campaign::OutcomeId{0x5101},
        "and everything the rename does not name is exactly what it was"
    );
    expect(
        state.progress.active &&
            state.progress.active_node ==
                ref(2, core::ContentCategory::campaign_node, 0x7101) &&
            state.progress.history.size() == 1U,
        "including where in its campaign it stood"
    );
    expect(
        loaded.save.packages.size() == 1U &&
            loaded.save.packages[0].content_revision == 2U &&
            loaded.save.packages[0].integrity == 0x2222333344445555ULL,
        "and the save now records the content revision it actually refers to"
    );
}

void the_content_axis_refuses_what_it_cannot_do() {
    const std::vector<std::uint8_t> bytes = fixture("content_revision_1.glsv");
    const campaign::SaveLoadOptions options;
    const campaign::DefinitionRegistry definitions = fixture_two_definitions();
    // No *content* step registered, and the section steps this build ships.
    // The fixture was written at roster schema 1 and this build reads schema 2,
    // so the section axis has a chain to walk whatever the content axis is
    // asked; leaving it out would test the refusal of the wrong axis.
    const campaign::SaveMigrationRegistry empty =
        campaign::standard_save_migrations();

    expect(
        campaign::load_campaign_migrated(
            bytes, options, empty, campaign::MountedContent{}
        ).migration.error == campaign::MigrationError::unmounted_package,
        "a save whose package is not mounted cannot be reconciled against it"
    );
    expect(
        campaign::load_campaign_migrated(
            bytes, options, empty, fixture_two_content(2, &definitions)
        ).migration.error == campaign::MigrationError::missing_step,
        "newer content with no registered rename is a hole, not a guess"
    );
    expect(
        campaign::load_campaign_migrated(
            bytes, options, empty, fixture_two_content(0, &definitions)
        ).migration.error == campaign::MigrationError::downgrade_refused,
        "and older content is a downgrade, refused rather than attempted"
    );

    campaign::SaveMigrationRegistry refusing = campaign::standard_save_migrations();
    expect(
        refusing.add_content_migration(package(2), 1, 2, refusing_content_step),
        "a step that refuses registers like any other"
    );
    expect(
        campaign::load_campaign_migrated(
            bytes, options, refusing, fixture_two_content(2, &definitions)
        ).migration.error == campaign::MigrationError::step_failed,
        "and its refusal is the load's refusal"
    );

    campaign::SaveMigrationRegistry colliding = campaign::standard_save_migrations();
    expect(
        colliding.add_content_migration(package(2), 1, 2, colliding_renames),
        "so does one that maps two keys onto one"
    );
    expect(
        campaign::load_campaign_migrated(
            bytes, options, colliding, fixture_two_content(2, &definitions)
        ).migration.error == campaign::MigrationError::rename_collision,
        "which is refused rather than merged, because merging is a guess"
    );

    campaign::SaveMigrationRegistry nowhere = campaign::standard_save_migrations();
    expect(
        nowhere.add_content_migration(package(2), 1, 2, renames_into_nothing),
        "and one that points at content that is not there"
    );
    expect(
        campaign::load_campaign_migrated(
            bytes, options, nowhere, fixture_two_content(2, &definitions)
        ).migration.error == campaign::MigrationError::missing_definition,
        "which the mounted declarations catch instead of loading a blank"
    );

    // Content at the revision the save was written against needs nothing, and
    // the references stay exactly as the bytes spell them.
    campaign::DefinitionRegistry original;
    declare(original, core::ContentCategory::unit_type, 0x1101, "lancer");
    declare(original, core::ContentCategory::unit_type, 0x1102, "picket");
    declare(original, core::ContentCategory::item, 0x2101, "tonic");
    declare(original, core::ContentCategory::item, 0x2105, "torch");
    declare(original, core::ContentCategory::objective, 0x3101, "hold_the_ford");
    declare(original, core::ContentCategory::campaign, 0x4101, "ford_held");
    declare(original, core::ContentCategory::campaign, 0x6101, "tarnholt");
    declare(original, core::ContentCategory::campaign_node, 0x7101, "ford");
    const campaign::MigratedLoad unchanged = campaign::load_campaign_migrated(
        bytes, options, empty, fixture_two_content(1, &original)
    );
    expect(
        static_cast<bool>(unchanged) &&
            unchanged.migration.applied.size() == 2U &&
            !unchanged.migration.applied[0].content &&
            unchanged.migration.applied[0].section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster) &&
            !unchanged.migration.applied[1].content &&
            unchanged.migration.applied[1].section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster) &&
            unchanged.save.state.units.size() == 2U &&
            unchanged.save.state.units[0].definition ==
                ref(2, core::ContentCategory::unit_type, 0x1101),
        "a save at the mounted revision moves along the section axis only, and "
        "not one reference along the content axis"
    );
}

// ---------------------------------------------------------------------------
// The axis that did not move
// ---------------------------------------------------------------------------

// A company whose characters an author wrote to be more than their classes,
// put down and picked up again. No step runs, because none was needed.
//
// The claim is about where authored data lives. The package holds what an
// author wrote about a character: the deltas on their stat line, the further
// step of reach. It is read out of the package every time the campaign is
// loaded. What a save carries is what was *earned*: a level crossed, an
// experience total, the points a roll gave, the items picked up along the way.
// A specificity is a gain nobody earned, so there is nothing new for the
// roster section to hold, its schema stays where it was, and every save this
// build has ever written still loads without a translation layer.
//
// The schema number is asserted rather than merely observed, because "no
// migration ran" is also what a build that quietly forgot to bump a version
// would report.
void a_specific_company_is_saved_and_resumed_with_no_migration() {
    campaign::CampaignState state;
    const auto batch = campaign::make_outcome_batch(
        {ref(1, core::ContentCategory::encounter, 0x8001), 0x99ULL, 0U},
        {campaign::recruit_unit(
             campaign::PersistentEntityId{1},
             ref(1, core::ContentCategory::unit_type, 0x1001)
         ),
         campaign::set_availability(
             campaign::PersistentEntityId{1}, campaign::Availability::available
         ),
         // Everything a character can earn, so the section under test is a
         // full one rather than a roster of defaults.
         campaign::grant_experience(campaign::PersistentEntityId{1}, 120U),
         campaign::advance_level(campaign::PersistentEntityId{1}, 2U),
         campaign::grow_stat(
             campaign::PersistentEntityId{1}, campaign::GrowableStat::health, 3U
         ),
         campaign::add_item(
             campaign::PersistentEntityId{1},
             ref(1, core::ContentCategory::item, 0x2001),
             2U
         ),
         // A second member, so the roster is a list and not a single entry.
         campaign::recruit_unit(
             campaign::PersistentEntityId{2},
             ref(1, core::ContentCategory::unit_type, 0x1002)
         ),
         campaign::set_availability(
             campaign::PersistentEntityId{2}, campaign::Availability::available
         )}
    );
    const auto applied = campaign::apply_outcome(state, batch);
    expect(
        static_cast<bool>(applied),
        std::string_view{campaign::outcome_error_name(applied.error)}
    );

    const campaign::CampaignSave written = campaign::make_campaign_save(
        state,
        {{package(1), 1U, 0xa1a2a3a4b1b2b3b4ULL}}
    );
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(written);
    const campaign::MigratedLoad loaded = campaign::load_campaign_migrated(
        bytes,
        campaign::SaveLoadOptions{},
        campaign::standard_save_migrations(),
        fixture_one_content()
    );
    expect(
        static_cast<bool>(loaded),
        std::string_view{campaign::migration_error_name(loaded.migration.error)}
    );
    expect(
        static_cast<bool>(loaded.load),
        std::string_view{campaign::save_error_name(loaded.load.error)}
    );
    expect(
        loaded.migration.applied.empty(),
        "a save this build wrote while the content held specificities needs no "
        "migration, and none runs: authored data added no format to the save"
    );

    const campaign::SaveSectionSchema roster =
        campaign::save_section_schema(campaign::SaveSectionType::roster);
    expect(
        roster.major == 3U && roster.minor == 0U,
        "because the roster section is still at the schema it reached when a "
        "character last learned something a save has to remember"
    );

    const campaign::CampaignState& kept = loaded.save.state;
    expect(kept.units.size() == 2U, "both members came back");
    if (kept.units.size() == 2U) {
        expect(
            kept.units[0].id == campaign::PersistentEntityId{1} &&
                kept.units[0].definition ==
                    ref(1, core::ContentCategory::unit_type, 0x1001) &&
                kept.units[0].availability ==
                    campaign::Availability::available &&
                kept.units[0].progression.level == 3U &&
                kept.units[0].progression.experience == 120U &&
                kept.units[0].progression.gain_in(
                    campaign::GrowableStat::health
                ) == 3U,
            "the first holding exactly what they earned, and not one field "
            "more: a save that had learned to carry an authored number would "
            "be a save that had to be migrated"
        );
        expect(
            kept.units[0].carried.size() == 1U &&
                kept.units[0].carried[0].item ==
                    ref(1, core::ContentCategory::item, 0x2001) &&
                kept.units[0].carried[0].quantity == 2U,
            "carrying what they picked up"
        );
        expect(
            kept.units[1].id == campaign::PersistentEntityId{2} &&
                kept.units[1].progression.level == 1U,
            "and the second exactly as unremarkable as they were"
        );
    }
    expect(
        campaign::save_campaign(loaded.save) == bytes,
        "and the save written back out is the save that was read, byte for "
        "byte, which is what a format that did not move means"
    );
}

// ---------------------------------------------------------------------------
// Transactional
// ---------------------------------------------------------------------------

campaign::CampaignSave live_session() {
    campaign::CampaignState state;
    const auto batch = campaign::make_outcome_batch(
        {ref(9, core::ContentCategory::encounter, 0x8001), 0x77ULL, 0U},
        {campaign::recruit_unit(
             campaign::PersistentEntityId{40},
             ref(9, core::ContentCategory::unit_type, 0x8101)
         ),
         campaign::set_availability(
             campaign::PersistentEntityId{40}, campaign::Availability::available
         )}
    );
    const auto applied = campaign::apply_outcome(state, batch);
    expect(static_cast<bool>(applied), "the live session is a campaign");
    return campaign::make_campaign_save(state, {});
}

// The design's whole procedure in one property: whatever fails, and at whatever
// stage, the session is holding the campaign it was already holding.
void a_migration_that_fails_leaves_the_live_session_alone() {
    const std::vector<std::uint8_t> bytes = fixture("roster_schema_0.glsv");
    const campaign::SaveLoadOptions options;
    const campaign::CampaignSave before = live_session();

    const auto untouched = [&before](
                               const campaign::CampaignSave& live,
                               std::string_view message
                           ) { expect(live == before, message); };

    // No step at all.
    campaign::CampaignSave live = before;
    expect(
        !campaign::load_campaign_migrated_into(
            live,
            bytes,
            options,
            campaign::SaveMigrationRegistry{},
            fixture_one_content()
        ),
        "a save with no path to this build is refused"
    );
    untouched(live, "and the session still holds what it held");

    // A step that runs and says no. The chain out of schema 0 is three
    // versions long, so every later step is registered too: a chain with a
    // hole in it would be refused before the refusing step ever ran, and this
    // is a test about a step that runs.
    campaign::SaveMigrationRegistry refusing;
    expect(
        refusing.add_section_migration(
            campaign::SaveSectionType::roster, 0, refusing_step
        ) &&
            refusing.add_section_migration(
                campaign::SaveSectionType::roster, 1, identity_step
            ) &&
            refusing.add_section_migration(
                campaign::SaveSectionType::roster, 2, identity_step
            ),
        "a refusing step registers"
    );
    const campaign::MigratedLoad refused = campaign::load_campaign_migrated_into(
        live, bytes, options, refusing, fixture_one_content()
    );
    expect(
        refused.migration.error == campaign::MigrationError::step_failed &&
            refused.migration.section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster),
        "a step that refuses names itself"
    );
    untouched(live, "and changes nothing");

    // A step that runs, succeeds, and hands on bytes the next stage refuses.
    // The candidate got further than any other failure here and still never
    // reached the session.
    campaign::SaveMigrationRegistry lying;
    expect(
        lying.add_section_migration(
            campaign::SaveSectionType::roster, 0, identity_step
        ) &&
            lying.add_section_migration(
                campaign::SaveSectionType::roster, 1, identity_step
            ) &&
            lying.add_section_migration(
                campaign::SaveSectionType::roster, 2, identity_step
            ),
        "a step that changes nothing registers"
    );
    const campaign::MigratedLoad garbled = campaign::load_campaign_migrated_into(
        live, bytes, options, lying, fixture_one_content()
    );
    expect(
        garbled.load.error == campaign::SaveError::invalid_section &&
            garbled.load.section ==
                static_cast<std::uint32_t>(campaign::SaveSectionType::roster),
        "a migration that produced the wrong bytes is caught by the same "
        "interpreter every save goes through"
    );
    untouched(live, "and still nothing was installed");

    // And the successful load does replace it, so the property above is about
    // failure and not about the function never doing anything.
    const campaign::MigratedLoad accepted = campaign::load_campaign_migrated_into(
        live,
        bytes,
        options,
        campaign::standard_save_migrations(),
        fixture_one_content()
    );
    expect(static_cast<bool>(accepted), "a save with a path is loaded");
    expect(!(live == before), "and the session is holding it");
    expect(live.state.units.size() == 2U, "roster and all");
}

}  // namespace

// A content chain is followed edge by edge, because a revision is a version
// rather than a counter.
//
// The two axes count differently and used to be walked the same way. A schema
// major really is a dense counter, so `from + 1` is the whole truth there. But
// `tools/game_content/src/source_project.cpp` packs a content revision as
// `(major << 20) | (minor << 10) | patch`, so the integer after 0.1.0 is 0.1.1.
// Walking the integers between two revisions therefore made every chain a chain
// of patch steps, and put 1024 of them between 0.1.0 and 0.2.0 -- past
// `maximum_migration_steps`. A game that moved its minor version could not
// write a migration at all, and was told it had exceeded a step limit, which
// was true in the planner's own terms and no help to anybody.
//
// So a step says where it lands. These cases are the four answers that gives.
void a_content_chain_is_followed_edge_by_edge() {
    const core::PackageId subject = package(3);
    constexpr std::uint32_t v0_1_0 = (0U << 20U) | (1U << 10U) | 0U;
    constexpr std::uint32_t v0_2_0 = (0U << 20U) | (2U << 10U) | 0U;
    constexpr std::uint32_t v1_0_0 = (1U << 20U) | (0U << 10U) | 0U;

    // Stated rather than assumed, because the whole defect was an assumption
    // about this arithmetic.
    expect(
        v0_2_0 - v0_1_0 == 1024U && v0_2_0 - v0_1_0 > campaign::maximum_migration_steps,
        "a minor bump is further apart than a chain may be walked"
    );

    // One step across a minor bump, which is the case that could not be
    // expressed at all before.
    campaign::SaveMigrationRegistry registry = campaign::standard_save_migrations();
    expect(
        registry.add_content_migration(
            subject, v0_1_0, v0_2_0, fixture_package_renames_at_revision_1
        ),
        "a step may be registered across a minor bump"
    );
    const campaign::MigrationReport minor =
        campaign::plan_content_migration(registry, subject, v0_1_0, v0_2_0);
    expect(
        static_cast<bool>(minor) && minor.applied.size() == 1U &&
            minor.applied[0].from == v0_1_0 && minor.applied[0].to == v0_2_0,
        "and it is one step, from where it was written to where it lands"
    );

    // Two of them in a row, so the walk is a walk and not a special case.
    expect(
        registry.add_content_migration(
            subject, v0_2_0, v1_0_0, fixture_package_renames_at_revision_1
        ),
        "a second step registers behind the first"
    );
    const campaign::MigrationReport both =
        campaign::plan_content_migration(registry, subject, v0_1_0, v1_0_0);
    expect(
        static_cast<bool>(both) && both.applied.size() == 2U &&
            both.applied[1].to == v1_0_0,
        "and a chain of two crosses a major bump"
    );

    // A hole is a hole, and says so. This is the answer the old planner gave as
    // `step_limit_exceeded`, which named the wrong thing entirely.
    campaign::SaveMigrationRegistry empty = campaign::standard_save_migrations();
    const campaign::MigrationReport hole =
        campaign::plan_content_migration(empty, subject, v0_1_0, v0_2_0);
    expect(
        !hole && hole.error == campaign::MigrationError::missing_step,
        "a chain with no step at all is a missing step, not a step limit"
    );

    // A step that overshoots what is mounted is the same answer: nothing in the
    // registry reaches the version this build is actually holding.
    const campaign::MigrationReport overshoot =
        campaign::plan_content_migration(registry, subject, v0_1_0, v0_1_0 + 1U);
    expect(
        !overshoot && overshoot.error == campaign::MigrationError::missing_step,
        "and a step that lands past the mounted revision is a hole too"
    );

    // And a step that does not move forward is refused where it is written,
    // rather than found at load: one that lands where it started is a chain
    // with no end.
    campaign::SaveMigrationRegistry bad = campaign::standard_save_migrations();
    expect(
        !bad.add_content_migration(
            subject, v0_2_0, v0_2_0, fixture_package_renames_at_revision_1
        ) &&
            !bad.add_content_migration(
                subject, v0_2_0, v0_1_0, fixture_package_renames_at_revision_1
            ),
        "a step that stands still or goes backwards is refused at registration"
    );
}

int main() {
    the_registry_walks_one_version_at_a_time();
    a_save_at_an_older_section_schema_loads_through_the_registry();
    a_renamed_definition_is_repointed_and_nobody_changes_identity();
    the_content_axis_refuses_what_it_cannot_do();
    a_specific_company_is_saved_and_resumed_with_no_migration();
    a_migration_that_fails_leaves_the_live_session_alone();
    a_content_chain_is_followed_edge_by_edge();
    return failures == 0 ? 0 : 1;
}
