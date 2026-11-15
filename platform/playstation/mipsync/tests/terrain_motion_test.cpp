// SPDX-License-Identifier: MIT
#include "terrain_motion.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <cstdint>

int main() {
    std::array<std::uint32_t, 512> source{};
    for (unsigned i = 0; i < source.size(); ++i)
        source[i] = 0x9e3779b9u * (i + 1u);
    const auto unchanged = source;
    std::array<std::uint32_t, 514> guarded{};
    for (unsigned phase = 0; phase < 1024; ++phase) {
        auto expected = source;
        std::rotate(expected.begin(), expected.begin() + (phase % 32u) * 16u,
                    expected.end());
        guarded.front() = 0x13579bdfu;
        guarded.back() = 0x2468ace0u;
        grandleon_water_rows(guarded.data() + 1, source.data(), phase);
        if (!std::equal(expected.begin(), expected.end(), guarded.begin() + 1) ||
            guarded.front() != 0x13579bdfu || guarded.back() != 0x2468ace0u ||
            source != unchanged) {
            std::cerr << "Water row wrapping, bounds or source changed at phase "
                      << phase << '\n';
            return 1;
        }
    }
}
