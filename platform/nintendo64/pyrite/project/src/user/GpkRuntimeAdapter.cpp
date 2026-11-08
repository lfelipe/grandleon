// SPDX-License-Identifier: MIT
#include "GpkRuntimeAdapter.hpp"

#include <libdragon.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace grandleon::nintendo64::pyrite {

GpkRuntimeAdapter::~GpkRuntimeAdapter() {
    unload();
}

bool GpkRuntimeAdapter::load(
    const char* rom_asset,
    std::uint64_t encounter_id,
    std::uint8_t local_players
) noexcept {
    if (!load_package(rom_asset)) return false;

    encounter_ = package_runtime::load_encounter(
        package_.package,
        encounter_id,
        {local_players}
    );
    if (!encounter_) {
        error_ = Error::encounter_invalid;
        return false;
    }
    return true;
}

bool GpkRuntimeAdapter::load_package(const char* rom_asset) noexcept {
    unload();
    int byte_count = 0;
    package_bytes_ = asset_load(rom_asset, &byte_count);
    if (package_bytes_ == nullptr || byte_count <= 0) {
        error_ = Error::asset_missing;
        return false;
    }
    package_size_ = static_cast<std::size_t>(byte_count);
    package_ = package_format::load_mock_package_in_place(
        {
            static_cast<const std::uint8_t*>(package_bytes_),
            package_size_,
        },
        {
            {0, 1, 0},
            package_format::TargetProfile::nintendo64,
            0,
            64,
            20'000,
            48,
        }
    );
    if (!package_) {
        error_ = Error::package_invalid;
        return false;
    }
    return true;
}

void GpkRuntimeAdapter::unload() noexcept {
    encounter_ = {};
    package_ = {};
    if (package_bytes_ != nullptr) {
        std::free(package_bytes_);
        package_bytes_ = nullptr;
    }
    package_size_ = 0;
    error_ = Error::none;
}

}  // namespace grandleon::nintendo64::pyrite
