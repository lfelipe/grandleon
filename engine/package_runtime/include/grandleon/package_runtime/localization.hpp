// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace grandleon::package_runtime {

// The language the game's authored strings are written in. Empty for packages
// compiled before localization metadata existed or for malformed optional
// metadata. The view borrows the package bytes.
[[nodiscard]] std::string_view source_language(
    const package_format::LoadedPackage& package
) noexcept;

// Looks up one reviewed manual translation. The current source string is
// returned for the source language, an absent language/message, malformed
// optional data, or an entry whose stored source snapshot is no longer equal
// to `current_source`. That last rule makes stale work visible to both player
// and author rather than silently showing copy for an older game.
//
// The answer always borrows either `package` or `current_source`; nothing is
// allocated, which keeps this usable by the console runtimes.
[[nodiscard]] std::string_view localized_text(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::string_view message_id,
    std::string_view current_source
) noexcept;

// Languages offered by this authored game, in source-first order. Views borrow
// the package bytes. Malformed optional localization metadata returns an empty
// list so an older game remains playable in its source copy.
[[nodiscard]] std::vector<std::string_view> available_languages(
    const package_format::LoadedPackage& package
);

// Stable-ID lookups used by runtimes after source-project string keys have
// become portable numeric identities. Each function applies the same
// source-snapshot check as localized_text, so incomplete or outdated catalogs
// visibly fall back to the current authored string.
[[nodiscard]] std::string_view localized_content_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    package_format::SectionType section,
    std::uint64_t content_id,
    std::string_view current_source
) noexcept;

[[nodiscard]] std::string_view localized_dialogue_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t dialogue_id,
    std::string_view current_source
) noexcept;

[[nodiscard]] std::string_view localized_dialogue_line(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t dialogue_id,
    std::size_t line_index,
    std::string_view field,
    std::string_view current_source
) noexcept;

[[nodiscard]] std::string_view localized_campaign_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t campaign_id,
    std::string_view current_source
) noexcept;

[[nodiscard]] std::string_view localized_campaign_node_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t campaign_id,
    std::uint64_t node_id,
    std::string_view current_source
) noexcept;

[[nodiscard]] std::string_view localized_campaign_member_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t campaign_id,
    std::uint64_t member_id,
    std::string_view current_source
) noexcept;

}  // namespace grandleon::package_runtime
