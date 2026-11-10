// SPDX-License-Identifier: MIT
#include <grandleon/view/console_text.h>
#include <iostream>
#include <string>
#include <string_view>

static std::string display(const char* source) {
    std::string result;
    while (*source) {
        const auto glyph = static_cast<unsigned char>(grandleon_text_next(&source));
        if (glyph < 128) result += static_cast<char>(glyph);
        else {
            result += static_cast<char>(0xc0U | (glyph >> 6));
            result += static_cast<char>(0x80U | (glyph & 63U));
        }
    }
    return result;
}

int main() {
    const auto check = [](std::string_view expected, const char* source) {
        if (display(source) != expected) {
            std::cerr << "Console UTF-8 display mismatch: " << source << '\n';
            return false;
        }
        return true;
    };
    bool ok = check("Não perca o comandante!", "Não perca o comandante!");
    ok &= check("AÇÕES: proteção e cura", "AÇÕES: proteção e cura");
    ok &= check("¡Defiende el castillo! ¿Continuar?", "¡Defiende el castillo! ¿Continuar?");
    ok &= check("'Rowan' - \"Akari\"", "‘Rowan’ — “Akari”");
    ok &= check("Line one\nHP 15/20", "Line one\nHP 15/20");
    ok &= check("?", "\xc3");
    ok &= check("?A", "\xc3" "A");
    ok &= check("?", "\xf0\x9f\x98\x80");
    ok &= check("Aço e Cedro: ação, bênção, coração", "Aço e Cedro: ação, bênção, coração");
    ok &= check("ÁÀÂÃÄÉÈÊËÍÌÎÏÓÒÔÕÖÚÙÛÜÇÑÝ", "ÁÀÂÃÄÉÈÊËÍÌÎÏÓÒÔÕÖÚÙÛÜÇÑÝ");
    ok &= check("áàâãäéèêëíìîïóòôõöúùûüçñýÿ", "áàâãäéèêëíìîïóòôõöúùûüçñýÿ");
    ok &= check("?", "\xed\xa0\x80"); // Surrogate scalar.
    ok &= check("?", "\xf4\x90\x80\x80"); // Beyond Unicode.
    const char* end = "";
    ok &= grandleon_text_next(&end) == 0;
    return ok ? 0 : 1;
}
