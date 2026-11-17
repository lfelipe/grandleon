// SPDX-License-Identifier: MIT
#pragma once
#include <grandleon/core/content_identity.hpp>
#include <cstdint>

namespace grandleon::package_runtime {
struct ShopOffer final {
    std::uint64_t id{};
    core::ContentCategory category{core::ContentCategory::item};
    std::uint64_t content_id{};
    std::uint16_t price{};
    // Zero is unlimited; authored finite stock is positive.
    std::uint16_t stock{};
};
}  // namespace grandleon::package_runtime
