// SPDX-License-Identifier: MIT
#ifndef GRANDLEON_EXACT_SNAPSHOT_H
#define GRANDLEON_EXACT_SNAPSHOT_H
#include <stddef.h>
#include <stdint.h>

/* Lossless keys for arrays of equal-sized object representations. Each group
 * of up to 32 words stores a changed-word mask, then the changed values.
 * Unchanged words equal the same offset in the previous record (initially 0).
 * No fields are omitted and no hashes are used. Readers may match a prefix.
 * Small arrays retain direct word snapshots; compression is used only when
 * those would exceed the existing allocation. Failed capture invalidates
 * metadata before any buffer write. */
struct grandleon_exact_snapshot { size_t bytes, records; unsigned raw; };
static inline uint32_t grandleon_snapshot_word(const unsigned char* at) {
    uint32_t value;
    __builtin_memcpy(&value, at, 4);
    return value;
}
static inline int grandleon_snapshot_capture(struct grandleon_exact_snapshot* key,
    void* storage, size_t capacity, const void* records, size_t count, size_t words) {
    unsigned char* output = (unsigned char*)storage;
    const unsigned char* source = (const unsigned char*)records;
    size_t used = 0;
    key->bytes = key->records = 0; key->raw = 0;
    if (!words || count > SIZE_MAX / words / 4u) return 0;
    const size_t total_words = count * words;
    if (total_words <= capacity / 4u) {
        for (size_t i = 0; i < total_words; ++i) {
            const uint32_t value = grandleon_snapshot_word(source + i * 4u);
            __builtin_memcpy(output + i * 4u, &value, 4u);
        }
        key->bytes = total_words * 4u; key->records = count; key->raw = 1;
        return 1;
    }
    for (size_t record = 0; record < count; ++record) {
        for (size_t group = 0; group < words; group += 32u) {
            uint32_t mask = 0;
            const size_t begin = used;
            if (capacity - used < 4u) return 0;
            used += 4u;
            for (size_t bit = 0; bit < 32u && group + bit < words; ++bit) {
                const size_t offset = (record * words + group + bit) * 4u;
                const uint32_t value = grandleon_snapshot_word(source + offset);
                const uint32_t previous = record
                    ? grandleon_snapshot_word(source + offset - words * 4u) : 0;
                if (value == previous) continue;
                mask |= (uint32_t)1u << bit;
                if (capacity - used < 4u) return 0;
                __builtin_memcpy(output + used, &value, 4u);
                used += 4u;
            }
            __builtin_memcpy(output + begin, &mask, 4u);
        }
    }
    key->bytes = used; key->records = count;
    return 1;
}
static inline int grandleon_snapshot_matches(const struct grandleon_exact_snapshot* key,
    const void* storage, const void* records, size_t count, size_t words) {
    const unsigned char* input = (const unsigned char*)storage;
    const unsigned char* source = (const unsigned char*)records;
    size_t used = 0;
    if (!words || count > key->records || count > SIZE_MAX / words / 4u) return 0;
    if (key->raw) {
        const size_t total_words = count * words;
        if (total_words > key->bytes / 4u) return 0;
        for (size_t i = 0; i < total_words; ++i)
            if (grandleon_snapshot_word(source + i * 4u) !=
                grandleon_snapshot_word(input + i * 4u)) return 0;
        return 1;
    }
    for (size_t record = 0; record < count; ++record) {
        for (size_t group = 0; group < words; group += 32u) {
            uint32_t mask;
            if (key->bytes - used < 4u) return 0;
            mask = grandleon_snapshot_word(input + used); used += 4u;
            for (size_t bit = 0; bit < 32u && group + bit < words; ++bit) {
                const size_t offset = (record * words + group + bit) * 4u;
                uint32_t expected = record
                    ? grandleon_snapshot_word(source + offset - words * 4u) : 0;
                if (mask & ((uint32_t)1u << bit)) {
                    if (key->bytes - used < 4u) return 0;
                    expected = grandleon_snapshot_word(input + used); used += 4u;
                }
                if (grandleon_snapshot_word(source + offset) != expected) return 0;
            }
        }
    }
    return 1;
}
#endif
