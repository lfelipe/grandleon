// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>

#include <fstream>
#include <iostream>

namespace gc = grandleon::game_content;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: grandleon_template_builder <output.gpk>\n";
        return 64;
    }

    gc::GameSource source;
    source.game_id = {
        0x47, 0x52, 0x41, 0x4e, 0x44, 0x4c, 0x45, 0x4f,
        0x4e, 0x2d, 0x54, 0x45, 0x4d, 0x50, 0x4c, 0x00
    };
    source.title = "Grandleon Game Template";
    source.content_revision = 1;
    source.required_engine = {{0, 1, 0}, {0, 1, 99}};
    source.weapon_types = {{100, "Basic weapon"}};
    source.item_types = {{200, "Basic item"}};
    source.classes = {{300, "Basic class", {10, 1, 1, 0, 3}, {100}}};
    source.weapons = {{400, "Training weapon", 100, 1, 1, 1}};
    source.items = {{500, "Training item", 200, 1}};
    source.unit_types = {
        {600, "Basic unit", 300, 0, {400}, {500}}
    };

    const auto compiled = gc::compile(source);
    if (!compiled) {
        for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
            std::cerr << gc::diagnostic_name(diagnostic.code) << ": "
                      << diagnostic.path << '\n';
        }
        return 65;
    }

    std::ofstream output(argv[1], std::ios::binary);
    output.write(
        reinterpret_cast<const char*>(compiled.package.data()),
        static_cast<std::streamsize>(compiled.package.size())
    );
    if (!output) {
        std::cerr << "cannot write package: " << argv[1] << '\n';
        return 73;
    }
    return 0;
}
