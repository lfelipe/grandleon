// SPDX-License-Identifier: MIT
#include <grandleon/view/console_text.h>
#include <iostream>
#include <string>
#include <string_view>

static std::string display(const char* source) {
    std::string result;
    while (*source) result += grandleon_text_next(&source);
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
    bool ok = check("Nao perca o comandante!", "Não perca o comandante!");
    ok &= check("ACOES: protecao e cura", "AÇÕES: proteção e cura");
    ok &= check("!Defiende el castillo! ?Continuar?", "¡Defiende el castillo! ¿Continuar?");
    ok &= check("'Rowan' - \"Akari\"", "‘Rowan’ — “Akari”");
    ok &= check("Line one\nHP 15/20", "Line one\nHP 15/20");
    ok &= check("?", "\xc3");
    ok &= check("?A", "\xc3" "A");
    ok &= check("?", "\xf0\x9f\x98\x80");
    const char* end = "";
    ok &= grandleon_text_next(&end) == 0;
    return ok ? 0 : 1;
}
