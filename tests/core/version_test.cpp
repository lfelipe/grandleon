// SPDX-License-Identifier: MIT
#include <grandleon/core/version.hpp>

int main() {
    constexpr grandleon::core::Version expected{0, 1, 0};
    return grandleon::core::engine_version() == expected ? 0 : 1;
}
