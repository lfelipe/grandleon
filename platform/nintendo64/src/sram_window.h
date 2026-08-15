// SPDX-License-Identifier: MIT
#pragma once

// The cartridge's battery-backed bytes, as a byte window.
//
// This is the whole Nintendo 64 half of the save path. The names, the
// directory, the budget, the ordering and every refusal are
// `grandleon::storage::ByteWindowSlotStorage`, which links nothing and is
// proved on a host by `tests/storage/storage_contract_test.cpp`. What is left
// for a console is moving bytes, and that is all this file does.
//
// ## Why SRAM
//
// The measured campaign save is 2,868 bytes and the largest this repository
// produces is 3,128 (`tests/campaign/save_test.cpp`). Four kilobits of EEPROM
// is 512 bytes and sixteen is 2,048: neither holds one save. 256 kilobits of
// SRAM is 32,768 bytes, which holds ten of them. FlashRAM would also fit and
// costs a command/status protocol to erase a sector before writing it, which is
// a protocol nobody here needs.
//
// libdragon at the pinned commit has no SRAM API at all. What it does have is
// `eeprom.h` and a whole `eepromfs.h`, a filesystem for a device too small to
// hold one save. So the transfer is written here against `dma.h`, whose own
// documentation names `0x0500_0000 to 0x0FFF_FFFF` as "used by N64DD and SRAM
// on cartridge". SRAM sits at `0x0800_0000`.
//
// ## Why the whole cartridge is shadowed in RDRAM
//
// `dma_read` and `dma_write` are unusable: `dma.h` records that both carry a
// historical mistake which forces the PI address into the ROM range, and SRAM
// is not in it. The raw pair is well-defined only for 8-byte-aligned RDRAM,
// 2-byte-aligned PI addresses and even lengths.
//
// Rather than satisfy those three constraints at every call site, this window
// satisfies them once: it holds all 32,768 bytes in aligned `.bss`, reads them
// with one DMA at boot, and writes them back with one DMA per commit. The cost
// is 32 KiB of a four-megabyte machine and one transfer per save. A transfer
// per field would be both slower and a place for an alignment bug to live.

#include <libdragon.h>

#include <grandleon/storage/byte_window_storage.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace grandleon::n64save {

// 256 kilobits, which is what the ROM header declares and what every emulator
// and flash cart allocates for that declaration.
inline constexpr std::size_t sram_bytes = 32U * 1024U;

// Where the cartridge's SRAM answers on the peripheral bus.
inline constexpr std::uint32_t sram_base = 0x08000000U;

// How many slots the directory above reserves room for. Four, because that is
// what a save menu would ever offer on a console and because four campaign
// saves is under a third of the cartridge. The rest is headroom for a save that
// grows, which is the resource a fixed device cannot get more of later.
inline constexpr std::size_t sram_slots = 4U;

// The PI's domain-2 timing registers. libdragon does not set these, having no
// SRAM support, and every SRAM cartridge wants the same four values. An
// emulator does not need them and hardware does, which is the only reason they
// are here: a ROM that saves only under emulation is not what this repository
// claims anywhere else.
inline void configure_cartridge_domain() {
    volatile std::uint32_t* const latency =
        reinterpret_cast<volatile std::uint32_t*>(0xA4600024U);
    volatile std::uint32_t* const pulse_width =
        reinterpret_cast<volatile std::uint32_t*>(0xA4600028U);
    volatile std::uint32_t* const page_size =
        reinterpret_cast<volatile std::uint32_t*>(0xA460002CU);
    volatile std::uint32_t* const release =
        reinterpret_cast<volatile std::uint32_t*>(0xA4600030U);
    dma_wait();
    *latency = 0x05U;
    *pulse_width = 0x0CU;
    *page_size = 0x0DU;
    *release = 0x02U;
}

class SramWindow final : public grandleon::storage::ByteWindow {
public:
    SramWindow() {
        configure_cartridge_domain();
        // A DMA into RDRAM does not go through the CPU's data cache, so any
        // stale line covering the shadow would win the next read. Invalidate
        // before the transfer, not after: after is a race with nothing and
        // before is the guarantee.
        data_cache_hit_writeback_invalidate(shadow_, sizeof shadow_);
        dma_read_raw_async(shadow_, sram_base, sizeof shadow_);
        dma_wait();
        present_ = true;
    }

    [[nodiscard]] std::size_t size() const noexcept override {
        return sizeof shadow_;
    }

    // Throw the shadow away and read the cartridge again.
    //
    // This exists for exactly one caller: the check that proves the bytes
    // reached the hardware. Every `commit` below returns true because a PI DMA
    // has no failure to report, so "the device took the save" is a weaker claim
    // than it sounds, and the only way to strengthen it without switching the
    // machine off is to read the cartridge back over the top of the copy that
    // was written from. A `reload` followed by a directory that still parses is
    // a round trip through the bus.
    void reload() {
        // Scribble first, and this is the whole point of the function.
        //
        // A PI DMA to a region no cartridge answers on is not an error. It is
        // nothing at all, and RDRAM is left holding whatever it held. So a
        // reload that did not scribble would "read back" the very copy it was
        // about to compare against, and would report a pass on a machine with
        // no save hardware in it whatsoever. The pattern is what makes a
        // silent no-op look like the empty device it actually is.
        std::memset(shadow_, 0x5A, sizeof shadow_);
        data_cache_hit_writeback_invalidate(shadow_, sizeof shadow_);
        dma_read_raw_async(shadow_, sram_base, sizeof shadow_);
        dma_wait();
    }

    [[nodiscard]] bool read(
        std::size_t offset,
        std::uint8_t* into,
        std::size_t length
    ) const override {
        if (!present_) return false;
        if (length == 0) return offset <= sizeof shadow_;
        if (offset > sizeof shadow_ || length > sizeof shadow_ - offset) {
            return false;
        }
        std::memcpy(into, shadow_ + offset, length);
        return true;
    }

    [[nodiscard]] bool write(
        std::size_t offset,
        const std::uint8_t* from,
        std::size_t length
    ) override {
        if (!present_) return false;
        if (length == 0) return offset <= sizeof shadow_;
        if (offset > sizeof shadow_ || length > sizeof shadow_ - offset) {
            return false;
        }
        std::memcpy(shadow_ + offset, from, length);
        return true;
    }

    // One aligned transfer of the whole cartridge. The write-back is the other
    // half of the cache argument above: the DMA reads RDRAM directly, so
    // everything `memcpy` left sitting in the data cache has to be in RDRAM
    // before the transfer starts or the cartridge receives the bytes that were
    // there before.
    [[nodiscard]] bool commit() override {
        if (!present_) return false;
        data_cache_hit_writeback(shadow_, sizeof shadow_);
        dma_write_raw_async(shadow_, sram_base, sizeof shadow_);
        dma_wait();
        return true;
    }

private:
    // 8-byte aligned because `dma_read_raw_async` is only well-defined for
    // RDRAM addresses that are; 16 for the cache lines the write-back above
    // operates on.
    alignas(16) std::uint8_t shadow_[sram_bytes]{};
    bool present_{false};
};

// The budget a 32 KiB cartridge with four slots carries. Stated by the
// hardware, worked out by the directory, and never guessed at here.
[[nodiscard]] inline grandleon::storage::StorageBudget cartridge_budget() {
    return grandleon::storage::ByteWindowSlotStorage::budget_for(
        sram_bytes, sram_slots
    );
}

}  // namespace grandleon::n64save
