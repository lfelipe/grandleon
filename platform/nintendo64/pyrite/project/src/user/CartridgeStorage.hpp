// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/storage/byte_window_storage.hpp>
#include <libdragon.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace grandleon::nintendo64::pyrite {

inline constexpr std::size_t cartridge_bytes = 32U * 1024U;
inline constexpr std::uint32_t cartridge_base = 0x08000000U;
inline constexpr std::size_t cartridge_slots = 4U;

class CartridgeStorage final : public storage::ByteWindow {
public:
    CartridgeStorage() {
        volatile std::uint32_t* const latency =
            reinterpret_cast<volatile std::uint32_t*>(0xA4600024U);
        volatile std::uint32_t* const pulse =
            reinterpret_cast<volatile std::uint32_t*>(0xA4600028U);
        volatile std::uint32_t* const page =
            reinterpret_cast<volatile std::uint32_t*>(0xA460002CU);
        volatile std::uint32_t* const release =
            reinterpret_cast<volatile std::uint32_t*>(0xA4600030U);
        dma_wait();
        *latency = 0x05U;
        *pulse = 0x0CU;
        *page = 0x0DU;
        *release = 0x02U;
        data_cache_hit_writeback_invalidate(shadow_, sizeof shadow_);
        dma_read_raw_async(shadow_, cartridge_base, sizeof shadow_);
        dma_wait();
        present_ = true;
    }

    [[nodiscard]] std::size_t size() const noexcept override {
        return sizeof shadow_;
    }

    [[nodiscard]] bool read(
        std::size_t offset,
        std::uint8_t* into,
        std::size_t length
    ) const override {
        if (!present_ || offset > sizeof shadow_ ||
            length > sizeof shadow_ - offset) return false;
        std::memcpy(into, shadow_ + offset, length);
        return true;
    }

    [[nodiscard]] bool write(
        std::size_t offset,
        const std::uint8_t* from,
        std::size_t length
    ) override {
        if (!present_ || offset > sizeof shadow_ ||
            length > sizeof shadow_ - offset) return false;
        std::memcpy(shadow_ + offset, from, length);
        return true;
    }

    [[nodiscard]] bool commit() override {
        if (!present_) return false;
        data_cache_hit_writeback(shadow_, sizeof shadow_);
        dma_write_raw_async(shadow_, cartridge_base, sizeof shadow_);
        dma_wait();
        return true;
    }

    [[nodiscard]] static storage::StorageBudget budget() noexcept {
        return storage::ByteWindowSlotStorage::budget_for(
            cartridge_bytes, cartridge_slots
        );
    }

private:
    alignas(16) std::uint8_t shadow_[cartridge_bytes]{};
    bool present_{};
};

}  // namespace grandleon::nintendo64::pyrite
