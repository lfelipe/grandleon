// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>

#include <string_view>

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

}  // namespace grandleon::package_runtime
