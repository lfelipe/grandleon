// SPDX-License-Identifier: MIT
#ifndef GRANDLEON_VIEW_CONSOLE_TEXT_H
#define GRANDLEON_VIEW_CONSOLE_TEXT_H
#include <stdint.h>

/* Decode one character for the consoles' limited built-in fonts. Keep the
 * authored UTF-8 catalog unchanged; only the final display loses accents. */
static inline char grandleon_text_next(const char** cursor) {
    uint32_t code;
    unsigned count, index;
    const unsigned char* text;
    if (!cursor || !*cursor || !**cursor) return 0;
    text = (const unsigned char*)*cursor;
    code = *text++;
    if (code < 128) { *cursor = (const char*)text; return (char)code; }
    if (code >= 0xc2 && code <= 0xdf) { count = 1; code &= 31; }
    else if (code >= 0xe0 && code <= 0xef) { count = 2; code &= 15; }
    else if (code >= 0xf0 && code <= 0xf4) { count = 3; code &= 7; }
    else { *cursor = (const char*)text; return '?'; }
    for (index = 0; index < count; ++index) {
        if ((*text & 0xc0) != 0x80) { *cursor = (const char*)text; return '?'; }
        code = (code << 6) | (*text++ & 63);
    }
    *cursor = (const char*)text;
    if (code >= 0xc0 && code <= 0xc5) return 'A';
    if (code >= 0xe0 && code <= 0xe5) return 'a';
    if (code >= 0xc8 && code <= 0xcb) return 'E';
    if (code >= 0xe8 && code <= 0xeb) return 'e';
    if (code >= 0xcc && code <= 0xcf) return 'I';
    if (code >= 0xec && code <= 0xef) return 'i';
    if (code >= 0xd2 && code <= 0xd6) return 'O';
    if (code >= 0xf2 && code <= 0xf6) return 'o';
    if (code >= 0xd9 && code <= 0xdc) return 'U';
    if (code >= 0xf9 && code <= 0xfc) return 'u';
    switch (code) {
        case 0xc7: return 'C'; case 0xe7: return 'c';
        case 0xd1: return 'N'; case 0xf1: return 'n';
        case 0xdd: case 0x178: return 'Y';
        case 0xfd: case 0xff: return 'y';
        case 0xa1: return '!'; case 0xbf: return '?';
        case 0xa0: return ' ';
        case 0x2018: case 0x2019: return '\'';
        case 0x201c: case 0x201d: return '"';
        case 0x2013: case 0x2014: return '-';
        case 0x2026: return '.';
        default: return '?';
    }
}
#endif
