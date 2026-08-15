// SPDX-License-Identifier: MIT
#include <grandleon/campaign/identity.hpp>

#include <algorithm>

namespace grandleon::campaign {

namespace {

[[nodiscard]] std::uint64_t hash_byte(
    std::uint64_t hash,
    std::uint8_t value
) noexcept {
    return core::fnv1a64_step(hash, value);
}

template <typename Unsigned>
[[nodiscard]] std::uint64_t hash_integer(
    std::uint64_t hash,
    Unsigned value
) noexcept {
    for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
        hash = hash_byte(hash, static_cast<std::uint8_t>(value >> (index * 8U)));
    }
    return hash;
}

[[nodiscard]] bool same_key(
    const std::vector<char>& stored,
    std::string_view candidate
) noexcept {
    if (stored.size() != candidate.size()) {
        return false;
    }
    return std::equal(stored.begin(), stored.end(), candidate.begin());
}

}  // namespace

std::uint64_t hash_definition_ref(
    std::uint64_t hash,
    const DefinitionRef& reference
) noexcept {
    for (const std::uint8_t byte : reference.package_id) {
        hash = hash_byte(hash, byte);
    }
    hash = hash_integer(hash, static_cast<std::uint32_t>(reference.category));
    return hash_integer(hash, reference.stable_id);
}

std::string_view identity_error_name(IdentityError error) noexcept {
    switch (error) {
        case IdentityError::none:
            return "none";
        case IdentityError::definition_collision:
            return "definition_collision";
        case IdentityError::duplicate_definition_key:
            return "duplicate_definition_key";
        case IdentityError::duplicate_rename:
            return "duplicate_rename";
        case IdentityError::rename_cycle:
            return "rename_cycle";
        case IdentityError::missing_definition:
            return "missing_definition";
        case IdentityError::duplicate_binding:
            return "duplicate_binding";
        case IdentityError::reserved_identity:
            return "reserved_identity";
    }
    return "unknown";
}

IdentityError DefinitionRegistry::declare(
    const core::PackageId& package,
    core::ContentCategory category,
    std::string_view source_key
) {
    const DefinitionRef reference{
        package,
        category,
        core::stable_content_id_v1(source_key),
    };
    return declare(reference, source_key);
}

IdentityError DefinitionRegistry::declare(
    const DefinitionRef& reference,
    std::string_view source_key
) {
    const auto position = std::lower_bound(
        entries_.begin(),
        entries_.end(),
        reference,
        [](const Entry& entry, const DefinitionRef& value) {
            return definition_ref_less(entry.reference, value);
        }
    );
    if (position != entries_.end() && position->reference == reference) {
        // The same record declared twice is harmless; two records that
        // resolved to one identity are not, and nothing downstream could ever
        // tell them apart again.
        return same_key(position->source_key, source_key)
                   ? IdentityError::none
                   : IdentityError::definition_collision;
    }

    // The same key twice under different references is the other direction of
    // the same mistake: a save written against one of them cannot say which.
    for (const Entry& entry : entries_) {
        if (entry.reference.category == reference.category &&
            entry.reference.package_id == reference.package_id &&
            same_key(entry.source_key, source_key)) {
            return IdentityError::duplicate_definition_key;
        }
    }

    Entry entry;
    entry.reference = reference;
    entry.source_key.assign(source_key.begin(), source_key.end());
    entries_.insert(position, std::move(entry));
    return IdentityError::none;
}

bool DefinitionRegistry::contains(const DefinitionRef& reference) const noexcept {
    const auto position = std::lower_bound(
        entries_.begin(),
        entries_.end(),
        reference,
        [](const Entry& entry, const DefinitionRef& value) {
            return definition_ref_less(entry.reference, value);
        }
    );
    return position != entries_.end() && position->reference == reference;
}

const DefinitionRef* DefinitionRegistry::find(
    const core::PackageId& package,
    core::ContentCategory category,
    std::string_view source_key
) const noexcept {
    for (const Entry& entry : entries_) {
        if (entry.reference.category == category &&
            entry.reference.package_id == package &&
            same_key(entry.source_key, source_key)) {
            return &entry.reference;
        }
    }
    return nullptr;
}

IdentityError DefinitionRenameTable::add(const DefinitionRename& rename) {
    if (rename.from == rename.to) {
        return IdentityError::rename_cycle;
    }

    const auto position = std::lower_bound(
        renames_.begin(),
        renames_.end(),
        rename.from,
        [](const DefinitionRename& entry, const DefinitionRef& value) {
            return definition_ref_less(entry.from, value);
        }
    );
    if (position != renames_.end() && position->from == rename.from) {
        return IdentityError::duplicate_rename;
    }

    // Walk the existing chain forward from the proposed target. Reaching the
    // proposed source means the new mapping would close a loop, and
    // `resolve` would never terminate. Refusing it here is what lets
    // `resolve` be a plain loop with no visited set and no bound.
    DefinitionRef step = rename.to;
    for (std::size_t hops = 0; hops <= renames_.size(); ++hops) {
        if (step == rename.from) {
            return IdentityError::rename_cycle;
        }
        const auto next = std::lower_bound(
            renames_.begin(),
            renames_.end(),
            step,
            [](const DefinitionRename& entry, const DefinitionRef& value) {
                return definition_ref_less(entry.from, value);
            }
        );
        if (next == renames_.end() || !(next->from == step)) {
            break;
        }
        step = next->to;
    }

    renames_.insert(position, rename);
    return IdentityError::none;
}

IdentityError DefinitionRenameTable::merge(const DefinitionRenameTable& other) {
    // One at a time, through the same door. A merge that skipped `add` would
    // skip the cycle check, and the cycle check is the whole reason `resolve`
    // may be an unbounded-looking loop.
    //
    // Into a candidate, so a merge that fails halfway leaves this table exactly
    // as it was. A partially folded rename table is the one thing worse than no
    // rename table: it resolves some references and not others, and nothing
    // downstream could tell which.
    DefinitionRenameTable candidate = *this;
    for (const DefinitionRename& rename : other.renames_) {
        const IdentityError error = candidate.add(rename);
        if (error != IdentityError::none) {
            return error;
        }
    }
    *this = std::move(candidate);
    return IdentityError::none;
}

DefinitionRef DefinitionRenameTable::resolve(
    const DefinitionRef& reference
) const noexcept {
    DefinitionRef current = reference;
    for (std::size_t hops = 0; hops < renames_.size(); ++hops) {
        const auto position = std::lower_bound(
            renames_.begin(),
            renames_.end(),
            current,
            [](const DefinitionRename& entry, const DefinitionRef& value) {
                return definition_ref_less(entry.from, value);
            }
        );
        if (position == renames_.end() || !(position->from == current)) {
            return current;
        }
        current = position->to;
    }
    return current;
}

IdentityError DefinitionRenameTable::resolve_in(
    const DefinitionRegistry& registry,
    const DefinitionRef& reference,
    DefinitionRef& resolved
) const noexcept {
    const DefinitionRef candidate = resolve(reference);
    if (!registry.contains(candidate)) {
        return IdentityError::missing_definition;
    }
    resolved = candidate;
    return IdentityError::none;
}

IdentityError BattleBinding::bind(
    BattleEntityId battle,
    PersistentEntityId persistent
) {
    if (battle.value == 0U || persistent.value == 0U) {
        return IdentityError::reserved_identity;
    }

    const auto position = std::lower_bound(
        pairs_.begin(),
        pairs_.end(),
        battle,
        [](const Pair& pair, BattleEntityId value) {
            return pair.battle < value;
        }
    );
    if (position != pairs_.end() && position->battle == battle) {
        return IdentityError::duplicate_binding;
    }
    for (const Pair& pair : pairs_) {
        if (pair.persistent == persistent) {
            return IdentityError::duplicate_binding;
        }
    }

    pairs_.insert(position, Pair{battle, persistent});
    return IdentityError::none;
}

PersistentEntityId BattleBinding::persistent_of(
    BattleEntityId battle
) const noexcept {
    const auto position = std::lower_bound(
        pairs_.begin(),
        pairs_.end(),
        battle,
        [](const Pair& pair, BattleEntityId value) {
            return pair.battle < value;
        }
    );
    if (position == pairs_.end() || !(position->battle == battle)) {
        return PersistentEntityId{};
    }
    return position->persistent;
}

BattleEntityId BattleBinding::battle_of(
    PersistentEntityId persistent
) const noexcept {
    for (const Pair& pair : pairs_) {
        if (pair.persistent == persistent) {
            return pair.battle;
        }
    }
    return BattleEntityId{};
}

}  // namespace grandleon::campaign
