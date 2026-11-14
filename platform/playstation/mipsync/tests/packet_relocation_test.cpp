// SPDX-License-Identifier: MIT
#include "packet_relocation.h"
#include <array>
#include <cstdint>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "failed: " #condition << " at line " << __LINE__ << '\n'; \
    return 1; } } while (0)

int main() {
    grandleon_packet_ranges ranges{0x1000, 0x8000, 24, 0x2000, 0x9000, 16};
    // A three-word command followed by a one-word command. Payload values
    // deliberately look like addresses and must never be relocated.
    const std::array<uint32_t, 6> packets{
        0x03001010u, 0x001000u, 0x002000u, 0x00ffffffu,
        0x01002008u, 0x00001010u
    };
    CHECK(grandleon_gpu_stream_valid(packets.data(), packets.size(), &ranges));
    std::array<uint32_t, 8> destination{};
    destination[6] = 0xfeedfaceu; // UI/reserved memory after the world stream.
    grandleon_gpu_copy_stream(destination.data(), packets.data(), packets.size(), &ranges);
    CHECK(destination[0] == 0x03008010u);
    CHECK(destination[4] == 0x01009008u);
    for (const unsigned index : {1, 2, 3, 5}) CHECK(destination[index] == packets[index]);
    CHECK(destination[6] == 0xfeedfaceu);

    uint32_t tag;
    CHECK(grandleon_gpu_rebase(0x00ffffffu, &ranges, &tag) && tag == 0x00ffffffu);
    CHECK(grandleon_gpu_rebase(0x1200200cu, &ranges, &tag) && tag == 0x1200900cu);
    CHECK(!grandleon_gpu_rebase(0x001018u, &ranges, &tag)); // packet end
    CHECK(!grandleon_gpu_rebase(0x002010u, &ranges, &tag)); // table end
    CHECK(!grandleon_gpu_rebase(0x001001u, &ranges, &tag)); // alignment
    CHECK(!grandleon_gpu_rebase(0x400000u, &ranges, &tag)); // foreign memory
    auto invalid = ranges;
    invalid.target_packets = 0xfffffcu;
    CHECK(!grandleon_gpu_rebase(packets[0], &invalid, &tag)); // 24-bit overflow
    auto truncated = packets;
    truncated[0] = 0xff001010u;
    CHECK(!grandleon_gpu_stream_valid(truncated.data(), truncated.size(), &ranges));
    CHECK(!grandleon_gpu_stream_valid(packets.data(), 5, &ranges));
    CHECK(!grandleon_gpu_stream_valid(packets.data(), 7, &ranges));
    CHECK(grandleon_gpu_stream_valid(packets.data(), 0, &ranges));

    // Repeated double-buffer swaps preserve commands and cycle both kinds of
    // link back to their original addresses; no address drift accumulates.
    std::array<uint32_t, 6> a = packets, b{};
    for (unsigned frame = 0; frame < 100; ++frame) {
        grandleon_gpu_copy_stream(b.data(), a.data(), a.size(), &ranges);
        grandleon_packet_ranges reverse{ranges.target_packets, ranges.source_packets,
            ranges.packet_bytes, ranges.target_ot, ranges.source_ot, ranges.ot_bytes};
        grandleon_gpu_copy_stream(a.data(), b.data(), b.size(), &reverse);
        CHECK(a == packets);
    }
    std::cout << "PS1 DMA links, commands, bounds and double-buffer relocation pass\n";
}
