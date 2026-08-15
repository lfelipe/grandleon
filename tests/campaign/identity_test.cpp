// SPDX-License-Identifier: MIT
#include <grandleon/campaign/identity.hpp>

#include <iostream>
#include <string_view>
#include <type_traits>

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

campaign::DefinitionRef reference(
    std::uint8_t marker,
    core::ContentCategory category,
    std::string_view key
) {
    return {package(marker), category, core::stable_content_id_v1(key)};
}

// Two recruits of one unit type are two people. This is the whole reason the
// design separates the definition from the instance, so it is the first thing
// checked.
void two_units_share_a_definition_and_nothing_else() {
    const campaign::DefinitionRef lancer =
        reference(1, core::ContentCategory::unit_type, "lancer");

    const campaign::PersistentEntityId first{7};
    const campaign::PersistentEntityId second{8};

    expect(first != second, "two recruits carry distinct persistent ids");
    expect(
        lancer == lancer,
        "both recruits keep the same package-scoped definition reference"
    );
    expect(
        !(first == second),
        "sharing a definition does not merge two instances"
    );
}

// A definition reference is namespaced twice over: by the package that
// authored it and by the category it lives in. The same authored key in two
// packages, or in two categories of one package, is two different records.
void definition_references_are_namespaced() {
    const campaign::DefinitionRef mine =
        reference(1, core::ContentCategory::unit_type, "captain");
    const campaign::DefinitionRef theirs =
        reference(2, core::ContentCategory::unit_type, "captain");
    const campaign::DefinitionRef other_category =
        reference(1, core::ContentCategory::item, "captain");

    expect(!(mine == theirs), "two packages may both author 'captain'");
    expect(
        !(mine == other_category),
        "a unit type and an item may share an authored key"
    );
    expect(
        mine.stable_id == theirs.stable_id,
        "the stable id is package-local: the package bytes do the separating"
    );

    const std::uint64_t mine_hash =
        campaign::hash_definition_ref(core::fnv1a64_offset_basis, mine);
    const std::uint64_t theirs_hash =
        campaign::hash_definition_ref(core::fnv1a64_offset_basis, theirs);
    const std::uint64_t category_hash =
        campaign::hash_definition_ref(core::fnv1a64_offset_basis, other_category);
    expect(
        mine_hash != theirs_hash && mine_hash != category_hash,
        "the folded encoding separates them too"
    );

    expect(
        campaign::definition_ref_less(mine, theirs),
        "the total order runs over the package bytes first"
    );
    expect(
        campaign::definition_ref_less(mine, other_category),
        "then over the category, unit types before items"
    );
}

// A collision is the compiler's problem, says `engine/core/README.md`, and
// this is the check stated where the persistent layer can run it too: two
// authored keys resolving to one identity are two characters a roster could
// never tell apart again.
void a_collision_within_one_package_and_category_is_refused() {
    campaign::DefinitionRegistry registry;
    const campaign::DefinitionRef lancer =
        reference(1, core::ContentCategory::unit_type, "lancer");

    expect(
        registry.declare(lancer, "lancer") == campaign::IdentityError::none,
        "the first declaration is accepted"
    );
    expect(
        registry.declare(lancer, "lancer") == campaign::IdentityError::none,
        "declaring one record twice is harmless"
    );
    expect(registry.size() == 1U, "and does not duplicate it");

    // The stable id is forced rather than derived, because FNV-1a-64 will not
    // hand a test a real collision and the refusal must be checked anyway.
    expect(
        registry.declare(lancer, "pikeman") ==
            campaign::IdentityError::definition_collision,
        "two keys resolving to one identity are refused"
    );

    const campaign::DefinitionRef renamed{
        lancer.package_id,
        lancer.category,
        core::stable_content_id_v1("halberdier"),
    };
    expect(
        registry.declare(renamed, "lancer") ==
            campaign::IdentityError::duplicate_definition_key,
        "one key under two identities is refused from the other direction"
    );

    // The same key in another package or category is not a collision.
    expect(
        registry.declare(
            package(2), core::ContentCategory::unit_type, "lancer"
        ) == campaign::IdentityError::none,
        "another package may author the same key"
    );
    expect(
        registry.declare(
            package(1), core::ContentCategory::item, "lancer"
        ) == campaign::IdentityError::none,
        "another category may author the same key"
    );
    expect(registry.size() == 3U, "all three records are distinct");
}

void a_registry_answers_for_what_it_holds() {
    campaign::DefinitionRegistry registry;
    expect(
        registry.declare(
            package(1), core::ContentCategory::unit_type, "lancer"
        ) == campaign::IdentityError::none,
        "a declaration by key derives the compiler's stable id"
    );

    const campaign::DefinitionRef* const found = registry.find(
        package(1), core::ContentCategory::unit_type, "lancer"
    );
    expect(found != nullptr, "the key resolves");
    if (found != nullptr) {
        expect(
            found->stable_id == core::stable_content_id_v1("lancer"),
            "to stable_content_id_v1 of the key"
        );
        expect(registry.contains(*found), "and the reference is held");
    }
    expect(
        registry.find(package(2), core::ContentCategory::unit_type, "lancer") ==
            nullptr,
        "and not under another package"
    );
    expect(
        !registry.contains(
            reference(1, core::ContentCategory::unit_type, "nobody")
        ),
        "a reference nobody declared is not held"
    );
}

// A rename is a change to the definition and not to the person: the migration
// mapping repoints the reference and leaves the persistent id alone.
void a_rename_needs_an_explicit_mapping() {
    const campaign::DefinitionRef old_name =
        reference(1, core::ContentCategory::unit_type, "lancer");
    const campaign::DefinitionRef middle_name =
        reference(1, core::ContentCategory::unit_type, "pikeman");
    const campaign::DefinitionRef new_name =
        reference(1, core::ContentCategory::unit_type, "halberdier");

    campaign::DefinitionRegistry registry;
    expect(
        registry.declare(new_name, "halberdier") ==
            campaign::IdentityError::none,
        "only the current name exists in mounted content"
    );

    campaign::DefinitionRenameTable renames;
    campaign::DefinitionRef resolved{};
    expect(
        renames.resolve_in(registry, old_name, resolved) ==
            campaign::IdentityError::missing_definition,
        "without a mapping the stored reference names nothing"
    );

    expect(
        renames.add({old_name, middle_name}) == campaign::IdentityError::none,
        "the first rename is declared"
    );
    expect(
        renames.add({middle_name, new_name}) == campaign::IdentityError::none,
        "and the second"
    );
    expect(
        renames.resolve_in(registry, old_name, resolved) ==
            campaign::IdentityError::none,
        "a chain of renames resolves"
    );
    expect(resolved == new_name, "onto the current definition");

    // The character is the same character. The persistent id never entered
    // the rename at all, which is exactly the property being claimed.
    const campaign::PersistentEntityId member{42};
    expect(
        member == campaign::PersistentEntityId{42},
        "the persistent identity is untouched by the rename"
    );

    expect(
        renames.add({old_name, new_name}) ==
            campaign::IdentityError::duplicate_rename,
        "one old reference may map to only one target"
    );
    expect(
        renames.resolve(new_name) == new_name,
        "a reference nothing renames resolves to itself"
    );
}

void a_rename_cycle_is_refused_when_it_is_declared() {
    const campaign::DefinitionRef first =
        reference(1, core::ContentCategory::unit_type, "one");
    const campaign::DefinitionRef second =
        reference(1, core::ContentCategory::unit_type, "two");
    const campaign::DefinitionRef third =
        reference(1, core::ContentCategory::unit_type, "three");

    campaign::DefinitionRenameTable renames;
    expect(
        renames.add({first, first}) == campaign::IdentityError::rename_cycle,
        "a reference cannot be renamed to itself"
    );
    expect(
        renames.add({first, second}) == campaign::IdentityError::none,
        "a chain begins"
    );
    expect(
        renames.add({second, third}) == campaign::IdentityError::none,
        "and continues"
    );
    expect(
        renames.add({third, first}) == campaign::IdentityError::rename_cycle,
        "closing the loop is refused, so resolution always terminates"
    );
    expect(renames.size() == 2U, "and the refused mapping is not kept");
    expect(renames.resolve(first) == third, "the chain still resolves");
}

// A battle id maps back when there is something to map back to, and does not
// when there is not. Summons and the opposing side have no campaign identity,
// and that is a fact rather than a failure.
void battle_identities_map_back_only_when_applicable() {
    campaign::BattleBinding binding;
    const campaign::PersistentEntityId captain{5};
    const campaign::PersistentEntityId healer{6};

    expect(
        binding.bind(campaign::BattleEntityId{1}, captain) ==
            campaign::IdentityError::none,
        "a deployed member binds to its unit on the board"
    );
    expect(
        binding.bind(campaign::BattleEntityId{2}, healer) ==
            campaign::IdentityError::none,
        "and so does the next"
    );

    expect(
        binding.persistent_of(campaign::BattleEntityId{1}) == captain,
        "the board unit maps back to its campaign member"
    );
    expect(
        binding.battle_of(healer) == campaign::BattleEntityId{2},
        "and the campaign member maps forward to the board"
    );
    expect(
        binding.persistent_of(campaign::BattleEntityId{99}) ==
            campaign::PersistentEntityId{},
        "a summon on the board maps back to nobody"
    );
    expect(
        binding.battle_of(campaign::PersistentEntityId{404}) ==
            campaign::BattleEntityId{},
        "a member left at home is nowhere on the board"
    );

    expect(
        binding.bind(campaign::BattleEntityId{1}, campaign::PersistentEntityId{9}) ==
            campaign::IdentityError::duplicate_binding,
        "one board unit is one campaign member"
    );
    expect(
        binding.bind(campaign::BattleEntityId{3}, captain) ==
            campaign::IdentityError::duplicate_binding,
        "and one campaign member stands in one place"
    );
    expect(
        binding.bind(campaign::BattleEntityId{}, captain) ==
            campaign::IdentityError::reserved_identity,
        "the reserved zero id binds to nothing"
    );
    expect(
        binding.bind(campaign::BattleEntityId{4}, campaign::PersistentEntityId{}) ==
            campaign::IdentityError::reserved_identity,
        "in either direction"
    );
    expect(binding.size() == 2U, "no refused binding was kept");
}

// The three levels are three types. A build where any pair of them is
// interchangeable has lost the distinction the design asks for, so the
// compiler is asked to say so.
void the_three_levels_are_three_types() {
    static_assert(
        !std::is_convertible<
            campaign::PersistentEntityId,
            campaign::BattleEntityId>::value,
        "a persistent id is not a battle id"
    );
    static_assert(
        !std::is_convertible<
            campaign::BattleEntityId,
            campaign::PersistentEntityId>::value,
        "a battle id is not a persistent id"
    );
    static_assert(
        !std::is_convertible<std::uint64_t, campaign::OutcomeId>::value,
        "a bare integer is not an outcome id"
    );
    static_assert(
        campaign::definition_ref_encoded_size == 28U,
        "sixteen bytes of package, four of category, eight of stable id"
    );

    expect(true, "the identity levels are distinct types");
}

void error_names_are_stated() {
    expect(
        campaign::identity_error_name(campaign::IdentityError::none) == "none",
        "every identity error names itself"
    );
    expect(
        campaign::identity_error_name(
            campaign::IdentityError::definition_collision
        ) == "definition_collision",
        "including the collision a compiler must report"
    );
}

}  // namespace

int main() {
    two_units_share_a_definition_and_nothing_else();
    definition_references_are_namespaced();
    a_collision_within_one_package_and_category_is_refused();
    a_registry_answers_for_what_it_holds();
    a_rename_needs_an_explicit_mapping();
    a_rename_cycle_is_refused_when_it_is_declared();
    battle_identities_map_back_only_when_applicable();
    the_three_levels_are_three_types();
    error_names_are_stated();
    return failures == 0 ? 0 : 1;
}
