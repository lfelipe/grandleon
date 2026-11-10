// SPDX-License-Identifier: MIT
#include <grandleon/view/ps1_font.h>
#include <grandleon/view/console_text.h>
#include "text_paging.h"
#include <cassert>

int main() {
    for (unsigned code = 32; code <= 95; ++code)
        assert(grandleon_ps1_glyph_slot(static_cast<unsigned char>(code)) == code - 32);
    unsigned slot = 64;
    for (unsigned code = 0xc0; code <= 0xde; ++code) {
        if (code == 0xd7) continue;
        assert(grandleon_ps1_glyph_slot(static_cast<unsigned char>(code)) == slot);
        assert(grandleon_ps1_glyph_slot(static_cast<unsigned char>(code + 32)) == slot);
        ++slot;
    }
    assert(slot == 94);
    assert(grandleon_ps1_glyph_slot(0xa1) == 94);
    assert(grandleon_ps1_glyph_slot(0xbf) == 95);
    const char* text = "Aço";
    assert(grandleon_ps1_glyph_slot(static_cast<unsigned char>(grandleon_text_next(&text))) == 'A' - 32);
    assert(grandleon_ps1_glyph_slot(static_cast<unsigned char>(grandleon_text_next(&text))) == 64 + 7);
    assert(grandleon_ps1_glyph_slot(static_cast<unsigned char>(grandleon_text_next(&text))) == 'O' - 32);
    assert(!*text);
    text = "ação, não, pátio, corazón, pingüino, ¿sí? ¡sí!";
    char line[35];
    unsigned cedilla = 0, tilde = 0, inverted = 0;
    while (*text) {
        const unsigned width = grandleon_text_wrap_line(&text, line, 34);
        for (unsigned index = 0; index < width; ++index) {
            const auto code = static_cast<unsigned char>(line[index]);
            const auto glyph = grandleon_ps1_glyph_slot(code);
            assert(glyph < GRANDLEON_PS1_FONT_GLYPHS);
            if (code == 0xe7) { ++cedilla; assert(glyph == 71); }
            if (code == 0xe3) { ++tilde; assert(glyph == 67); }
            if (code == 0xa1 || code == 0xbf) ++inverted;
        }
    }
    assert(cedilla == 1 && tilde == 2 && inverted == 2);
}
