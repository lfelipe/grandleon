// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_runtime/localization.hpp>

#include <iostream>
#include <string_view>

namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;

namespace {
int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

gc::GameSource source() {
    gc::GameSource value;
    value.game_id[0] = 0x4d;
    value.title = "Localized";
    value.source_language = "en";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.classes = {{30, "Knight", {8, 4, 2, 0, 3}, {10}}};
    value.weapons = {{40, "Sword", 10, 3, 1, 1}};
    value.items = {};
    value.unit_types = {{60, "Rowan", 30, 0, {40}, {}}};
    value.translations = {
        {"es", {{"unit_types.rowan.name", "Rowan", "Roldán"}}},
        {"pt-BR", {{"unit_types.rowan.name", "Rowan", "Rowan"}}}
    };
    return value;
}

}  // namespace

int main() {
    const auto compiled = gc::compile(source());
    expect(static_cast<bool>(compiled), "localized source compiles");
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 64, 10000}
    );
    expect(static_cast<bool>(loaded), "localized package loads");
    expect(pr::source_language(loaded.package) == "en", "source language is portable");
    expect(
        pr::localized_text(
            loaded.package, "es", "unit_types.rowan.name", "Rowan"
        ) == "Roldán",
        "reviewed Spanish text resolves"
    );
    expect(
        pr::localized_text(
            loaded.package, "pt-BR", "unit_types.rowan.name", "Rowan"
        ) == "Rowan",
        "Brazilian Portuguese is a portable catalog"
    );
    expect(
        pr::localized_text(
            loaded.package, "es", "unit_types.rowan.name", "Sir Rowan"
        ) == "Sir Rowan",
        "outdated translation visibly falls back to current source"
    );
    expect(
        pr::localized_text(
            loaded.package, "fr", "unit_types.rowan.name", "Rowan"
        ) == "Rowan",
        "missing language visibly falls back to source"
    );
    return failures == 0 ? 0 : 1;
}
