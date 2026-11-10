// SPDX-License-Identifier: MIT
#ifndef GRANDLEON_VIEW_CONSOLE_TEXT_H
#define GRANDLEON_VIEW_CONSOLE_TEXT_H
#include <stdint.h>

/* Decode UTF-8 into the consoles' Latin-1 glyph codes. Treat the returned
 * char as unsigned when selecting a glyph. Authored accents are preserved;
 * unsupported scalars use a visible question mark. */
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
    if ((count == 1 && code < 0x80) ||
        (count == 2 && code < 0x800) ||
        (count == 3 && code < 0x10000) ||
        code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return '?';
    if (code == 0xa0) return ' ';
    if (code >= 0xa1 && code <= 0xff) return (char)code;
    switch (code) {
        case 0xa0: return ' ';
        case 0x2018: case 0x2019: return '\'';
        case 0x201c: case 0x201d: return '"';
        case 0x2013: case 0x2014: return '-';
        case 0x2026: return '.';
        default: return '?';
    }
}
#endif
