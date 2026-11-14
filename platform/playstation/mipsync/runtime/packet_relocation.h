// SPDX-License-Identifier: MIT
#ifndef GRANDLEON_PACKET_RELOCATION_H
#define GRANDLEON_PACKET_RELOCATION_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t source_packets, target_packets, packet_bytes;
    uint32_t source_ot, target_ot, ot_bytes;
} grandleon_packet_ranges;

static inline int grandleon_gpu_range(uint32_t base, uint32_t bytes) {
    return !(base & 3u) && !(bytes & 3u) &&
        base <= 0xffffffu && bytes <= 0x1000000u - base;
}

static inline int grandleon_gpu_ranges_valid(const grandleon_packet_ranges* ranges) {
    return grandleon_gpu_range(ranges->source_packets, ranges->packet_bytes) &&
        grandleon_gpu_range(ranges->target_packets, ranges->packet_bytes) &&
        grandleon_gpu_range(ranges->source_ot, ranges->ot_bytes) &&
        grandleon_gpu_range(ranges->target_ot, ranges->ot_bytes);
}

/* The caller validates the four ranges once for a complete frame. Every link
 * still has to belong to those ranges or be the DMA end marker. */
static inline int grandleon_gpu_rebase_valid_ranges(uint32_t tag,
        const grandleon_packet_ranges* ranges, uint32_t* output) {
    const uint32_t address = tag & 0xffffffu;
    uint32_t target;
    if (address == 0xffffffu) { *output = tag; return 1; }
    if (address & 3u) return 0;
    if (address >= ranges->source_packets &&
        address - ranges->source_packets < ranges->packet_bytes) {
        target = ranges->target_packets + address - ranges->source_packets;
    } else if (address >= ranges->source_ot &&
               address - ranges->source_ot < ranges->ot_bytes) {
        target = ranges->target_ot + address - ranges->source_ot;
    } else return 0;
    *output = (tag & 0xff000000u) | target;
    return 1;
}

/* DMA addresses occupy the low 24 bits; the high byte is command length. */
static inline int grandleon_gpu_rebase(uint32_t tag,
        const grandleon_packet_ranges* ranges, uint32_t* output) {
    return grandleon_gpu_ranges_valid(ranges) &&
        grandleon_gpu_rebase_valid_ranges(tag, ranges, output);
}

static inline int grandleon_gpu_stream_valid(const uint32_t* source,
        size_t words, const grandleon_packet_ranges* ranges) {
    size_t index = 0;
    if (!grandleon_gpu_ranges_valid(ranges) ||
        words > ranges->packet_bytes / sizeof(uint32_t)) return 0;
    while (index < words) {
        const size_t length = 1u + (source[index] >> 24);
        uint32_t ignored;
        if (length > words - index ||
            !grandleon_gpu_rebase_valid_ranges(source[index], ranges, &ignored)) return 0;
        index += length;
    }
    return 1;
}

/* The caller supplies distinct frame buffers and validates the complete
 * stream first. Payload words (which may resemble addresses) stay untouched. */
static inline void grandleon_gpu_copy_stream(uint32_t* target,
        const uint32_t* source, size_t words,
        const grandleon_packet_ranges* ranges) {
    size_t index = 0;
    for (size_t word = 0; word < words; ++word) target[word] = source[word];
    while (index < words) {
        const size_t length = 1u + (source[index] >> 24);
        uint32_t tag = 0;
        (void)grandleon_gpu_rebase_valid_ranges(source[index], ranges, &tag);
        target[index] = tag;
        index += length;
    }
}
#endif
