// SPDX-License-Identifier: MIT
#include <grandleon/package_format/package.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: grandleon_package_check <package.gpk>\n";
        return 64;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open package: " << argv[1] << '\n';
        return 66;
    }
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    // A desktop runtime's own options, and deliberately not a stricter set.
    //
    // This tool answers one question: would a desktop build load this file?
    // Budgets it invented for itself would make it answer a different one,
    // and a validator that refuses what the runtime accepts is worse than no
    // validator. The counts are content limits and nothing more: the loader
    // measures every declared count against the bytes actually present before
    // it reserves anything, so a large budget here cannot be turned into a
    // large allocation by a small hostile file.
    grandleon::package_format::LoadOptions options;
    options.engine_version = {0, 1, 0};
    options.target = grandleon::package_format::TargetProfile::desktop;
    const auto result =
        grandleon::package_format::load_mock_package(bytes, options);
    if (!result) {
        std::cerr << "invalid package: "
                  << grandleon::package_format::error_name(result.error)
                  << '\n';
        return 65;
    }
    std::cout << "valid package: sections=" << result.package.sections.size()
              << " revision=" << result.package.content_revision << '\n';
    return 0;
}
