// SPDX-License-Identifier: MIT
#include <grandleon/core/random.hpp>

namespace grandleon::core {

std::string_view random_stream_name(RandomStream stream) noexcept {
    switch (stream) {
        case RandomStream::hit: return "hit";
        case RandomStream::drop: return "drop";
        case RandomStream::growth: return "growth";
    }
    return "unknown";
}

std::uint64_t hash_random_state(
    std::uint64_t hash,
    const RandomState& state
) noexcept {
    const auto fold = [&hash](std::uint64_t value, std::size_t width) {
        for (std::size_t index = 0; index < width; ++index) {
            hash = fnv1a64_step(
                hash,
                static_cast<std::uint8_t>(value >> (index * 8U))
            );
        }
    };

    fold(state.seed, 8U);

    std::uint32_t drawn = 0;
    for (std::size_t index = 1; index < random_stream_count; ++index) {
        if (state.positions[index] != 0U) {
            ++drawn;
        }
    }
    fold(drawn, 4U);

    for (std::size_t index = 1; index < random_stream_count; ++index) {
        if (state.positions[index] == 0U) {
            continue;
        }
        fold(static_cast<std::uint16_t>(index), 2U);
        fold(state.positions[index], 8U);
    }
    return hash;
}

}  // namespace grandleon::core
