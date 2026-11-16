// SPDX-License-Identifier: MIT
#pragma once
#include <grandleon/core/content_identity.hpp>

namespace grandleon::core {
// Gold has its own identity namespace, even when an item or weapon is named gold.
inline constexpr StableContentId gold_currency_id = stable_content_id_v1("gold");
}
