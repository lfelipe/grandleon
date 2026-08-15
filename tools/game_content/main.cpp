// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace gc = grandleon::game_content;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr
            << "usage: grandleon_content_compile <project.json> <output.gpk>\n";
        return 64;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open source project: " << argv[1] << '\n';
        return 66;
    }
    const std::string json{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    const auto parsed = gc::parse_source_project_json(json);
    if (!parsed) {
        for (const auto& diagnostic : parsed.diagnostics) {
            std::cerr << gc::source_diagnostic_name(diagnostic.code) << ": "
                      << diagnostic.path << ": " << diagnostic.detail << '\n';
        }
        return 65;
    }
    const auto compiled = gc::compile(parsed.source);
    if (!compiled) {
        for (const auto& diagnostic : compiled.diagnostics) {
            std::cerr << gc::diagnostic_name(diagnostic.code) << ": "
                      << diagnostic.path;
            if (diagnostic.related_id != 0) {
                std::cerr << " (id " << diagnostic.related_id << ')';
            }
            std::cerr << '\n';
        }
        return 65;
    }
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "cannot open output package: " << argv[2] << '\n';
        return 73;
    }
    output.write(
        reinterpret_cast<const char*>(compiled.package.data()),
        static_cast<std::streamsize>(compiled.package.size())
    );
    if (!output) {
        std::cerr << "cannot write output package: " << argv[2] << '\n';
        return 74;
    }
    std::cout << "compiled " << argv[1] << " -> " << argv[2] << " ("
              << compiled.package.size() << " bytes)\n";
    return 0;
}
