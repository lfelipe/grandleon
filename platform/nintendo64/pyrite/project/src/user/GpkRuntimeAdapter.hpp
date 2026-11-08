// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>

#include <cstddef>
#include <cstdint>

namespace grandleon::nintendo64::pyrite {

// Cartridge-facing boundary between Pyrite presentation and Grandleon's
// portable package/runtime. The package bytes remain owned here because the
// package loader deliberately borrows the ROM asset rather than copying it.
class GpkRuntimeAdapter final {
public:
    enum class Error : std::uint8_t {
        none = 0,
        asset_missing,
        package_invalid,
        encounter_invalid,
    };

    GpkRuntimeAdapter() = default;
    ~GpkRuntimeAdapter();

    GpkRuntimeAdapter(const GpkRuntimeAdapter&) = delete;
    GpkRuntimeAdapter& operator=(const GpkRuntimeAdapter&) = delete;

    [[nodiscard]] bool load(
        const char* rom_asset,
        std::uint64_t encounter_id,
        std::uint8_t local_players
    ) noexcept;
    void unload() noexcept;

    [[nodiscard]] Error error() const noexcept { return error_; }
    [[nodiscard]] std::size_t package_size() const noexcept {
        return package_size_;
    }
    [[nodiscard]] const package_runtime::EncounterLoadResult& encounter()
        const noexcept {
        return encounter_;
    }
    [[nodiscard]] const package_format::LoadedPackage& package() const noexcept {
        return package_.package;
    }

private:
    void* package_bytes_{};
    std::size_t package_size_{};
    package_format::LoadResult package_{};
    package_runtime::EncounterLoadResult encounter_{};
    Error error_{Error::none};
};

}  // namespace grandleon::nintendo64::pyrite
