// SPDX-License-Identifier: MIT
#include <grandleon/core/content_identity.hpp>

#include <cassert>

int main() {
    using grandleon::core::ContentCategory;
    using grandleon::core::ContentRef;
    using grandleon::core::PackageId;
    using grandleon::core::stable_content_id_v1;

    static_assert(stable_content_id_v1("vanguard") == 401286876892985483ULL);
    static_assert(stable_content_id_v1("vanguard") !=
                  stable_content_id_v1("Vanguard"));

    PackageId first_package{};
    first_package[15] = 1;
    PackageId second_package{};
    second_package[15] = 2;

    const auto local_id = stable_content_id_v1("vanguard");
    const ContentRef first{
        first_package,
        ContentCategory::unit_class,
        local_id,
    };
    const ContentRef same = first;
    const ContentRef other_package{
        second_package,
        ContentCategory::unit_class,
        local_id,
    };
    const ContentRef other_category{
        first_package,
        ContentCategory::unit_type,
        local_id,
    };

    assert(first == same);
    assert(!(first == other_package));
    assert(!(first == other_category));
}
