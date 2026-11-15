// SPDX-License-Identifier: MIT
#ifndef GRANDLEON_PS1_TERRAIN_MOTION_H
#define GRANDLEON_PS1_TERRAIN_MOTION_H
#include <stdint.h>
/* A generated 128x32, 4-bit terrain sheet. Source and destination are
 * separate: shifting rows preserves all four tile variants and their CLUT. */
static inline void grandleon_water_rows(uint32_t* target,
    const uint32_t* source, unsigned phase) {
    for (unsigned row = 0; row < 32u; ++row)
        for (unsigned word = 0; word < 16u; ++word)
            target[row * 16u + word] =
                source[((row + phase) & 31u) * 16u + word];
}
#endif
