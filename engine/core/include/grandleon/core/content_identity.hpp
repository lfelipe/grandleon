// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace grandleon::core {

using PackageId = std::array<std::uint8_t, 16>;
using StableContentId = std::uint64_t;

enum class ContentCategory : std::uint32_t {
    manifest = 1,
    unit_class = 2,
    unit_type = 3,
    weapon = 4,
    item = 5,
    map = 6,
    dialogue = 7,
    presentation = 8,
    weapon_type = 9,
    item_type = 10,
    faction = 11,
    objective = 12,
    encounter = 13,
    campaign = 14,
    // One node of a campaign's authored flow graph. A node has no section of
    // its own: it lives inside its campaign's record. It still has its own
    // stable id, derived from its own source key, and a campaign that names a
    // node the same thing it names itself must still be two identities.
    campaign_node = 15,
    // The key of a durable world value. Like a campaign node it has no section
    // of its own: a flag is named by a campaign's transition predicate and set
    // by an outcome batch, and nothing in a package is a record *of* it. It
    // needs a category all the same, because a `DefinitionRef` without one is
    // not a stable identity. Two campaigns naming a flag the same thing they
    // named an objective must still be two identities.
    world_flag = 16,
};

struct ContentRef final {
    PackageId package_id{};
    ContentCategory category{};
    StableContentId stable_id{};
};

[[nodiscard]] constexpr bool operator==(
    const ContentRef& lhs,
    const ContentRef& rhs
) noexcept {
    for (std::size_t index = 0; index < lhs.package_id.size(); ++index) {
        if (lhs.package_id[index] != rhs.package_id[index]) {
            return false;
        }
    }
    return lhs.category == rhs.category && lhs.stable_id == rhs.stable_id;
}

// Version 1 source-key mapping. This is a persistence contract, not a security
// hash. Compilers must reject collisions within one package and category.
[[nodiscard]] constexpr StableContentId stable_content_id_v1(
    std::string_view source_key
) noexcept {
    StableContentId value = 14695981039346656037ULL;
    for (const char character : source_key) {
        value ^= static_cast<std::uint8_t>(character);
        value *= 1099511628211ULL;
    }
    return value;
}

}  // namespace grandleon::core
