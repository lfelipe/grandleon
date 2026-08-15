// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/manifest.hpp>

#include <grandleon/package_runtime/names.hpp>

namespace grandleon::package_runtime {

std::string_view project_title(
    const package_format::LoadedPackage& package
) noexcept {
    // The manifest's title record holds a counted string and nothing else, so
    // it decodes exactly as a unit type's or a weapon's name does: the
    // leading string of the record found under an identity. One decoder
    // rather than two, because two would be two chances to disagree about how
    // this format writes a string.
    return content_name(
        package,
        package_format::SectionType::manifest,
        package_format::manifest_title_record_id
    );
}

}  // namespace grandleon::package_runtime
