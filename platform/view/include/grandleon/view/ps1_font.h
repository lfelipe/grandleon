// SPDX-License-Identifier: MIT
#ifndef GRANDLEON_VIEW_PS1_FONT_H
#define GRANDLEON_VIEW_PS1_FONT_H
#include <stdint.h>

/* The 64x144 PS1 atlas retains the original ASCII cells, then Latin-1 capital
 * letters (except multiplication), followed by inverted ! and ?. Lowercase
 * letters use the matching accented capital, preserving the console UI style. */
enum { GRANDLEON_PS1_FONT_WIDTH = 64, GRANDLEON_PS1_FONT_HEIGHT = 144,
       GRANDLEON_PS1_FONT_GLYPHS = 96 };
static inline uint8_t grandleon_ps1_glyph_slot(unsigned char code) {
    if (code >= 'a' && code <= 'z') code -= 'a' - 'A';
    if (code >= 0xe0 && code <= 0xfe && code != 0xf7) code -= 32;
    if (code >= 32 && code <= 95) return (uint8_t)(code - 32);
    if (code >= 0xc0 && code <= 0xde && code != 0xd7)
        return (uint8_t)(64 + code - 0xc0 - (code > 0xd7 ? 1 : 0));
    if (code == 0xa1) return 94;
    if (code == 0xbf) return 95;
    if (code == 0xa0) return 0;
    return '?' - 32;
}
#endif
