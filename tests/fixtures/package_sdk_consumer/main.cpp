// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_format/package.hpp>

int main() {
    grandleon::game_content::GameSource source;
    source.game_id[0] = 1;
    source.title = "External SDK Consumer";
    source.required_engine = {{0, 1, 0}, {0, 1, 0}};
    source.weapon_types.push_back({1, "Blade"});
    source.item_types.push_back({1, "Consumable"});
    source.classes.push_back({1, "Fighter", {10, 2, 1, 0, 3}, {1}});
    source.weapons.push_back({1, "Sword", 1, 2, 1, 1});
    source.items.push_back({1, "Tonic", 1, 5});
    source.unit_types.push_back(
        {1, "Trainee", 1, 0, {1}, {1}}
    );

    const auto result = grandleon::game_content::compile(source);
    if (!result || result.package.empty()) {
        return 1;
    }
    const auto loaded = grandleon::package_format::load_mock_package(
        result.package,
        {
            {0, 1, 0},
            grandleon::package_format::TargetProfile::desktop,
            0,
            32,
            1024
        }
    );
    return loaded &&
                   loaded.package.find(
                       grandleon::package_format::SectionType::unit_types,
                       1
                   ) != nullptr
               ? 0
               : 2;
}
