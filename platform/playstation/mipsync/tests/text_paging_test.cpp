// SPDX-License-Identifier: MIT
#include "text_paging.h"
#include <cassert>
#include <cctype>
#include <string>
#include <iostream>

static std::string glyphs(const char* source) {
    std::string result;
    while (*source) {
        const char value = grandleon_text_next(&source);
        if (!std::isspace(static_cast<unsigned char>(value))) result += value;
    }
    return result;
}

static unsigned check(const std::string& source, unsigned columns, unsigned rows) {
    const char* cursor = source.c_str();
    std::string rendered;
    unsigned pages = 0;
    while (*cursor) {
        const char* page = cursor;
        const char* next = grandleon_text_page_end(page, columns, rows);
        assert(next > page && next <= source.c_str() + source.size());
        // Render with the production line wrapper. Navigation must resume at
        // exactly the byte after the last line that was actually drawn.
        for (unsigned row = 0; *cursor && row < rows; ++row) {
            char line[35];
            const unsigned width = grandleon_text_wrap_line(&cursor, line, columns);
            assert(width <= columns);
            // The wrapper emits Latin-1 glyph codes, not another UTF-8 string.
            for (const char value : std::string(line))
                if (!std::isspace(static_cast<unsigned char>(value))) rendered += value;
        }
        assert(cursor == next);
        assert(!*next || (static_cast<unsigned char>(*next) & 0xc0) != 0x80);
        ++pages;
    }
    assert(rendered == glyphs(source.c_str()));
    return pages;
}

int main() {
    const char* accent = "Aço e ação";
    char first_line[35];
    assert(grandleon_text_wrap_line(&accent, first_line, 34) == 10);
    assert(std::string(first_line) == std::string("A\xe7o e a\xe7\xe3o"));
    assert(!*accent);
    // Real translated-length battle dialogue, accents and punctuation included.
    const std::string translated = "Atenção, companhia! As árvores escondem inimigos. Não avancem sozinhos: protejam os companheiros e mantenham a formação até chegarmos à ponte.";
    assert(check(translated, 34, 3) >= 2);
    assert(check("123456789012345678901234567890123456", 34, 3) == 1);
    assert(check(std::string(150, 'A'), 34, 3) == 2);
    assert(check("¡Atención!\n\nEl camino continúa después del río. São três passos até à última árvore.", 34, 3) >= 1);
    std::string story;
    for (unsigned i = 0; i < 40; ++i) story += translated + " ";
    assert(story.size() > 768);
    assert(check(story, 32, 10) > 3);
    unsigned authored = 0, battle_pages = 0, story_pages = 0;
    std::string text;
    while (std::getline(std::cin, text, '\0')) {
        battle_pages += check(text, 34, 3);
        story_pages += check(text, 32, 10);
        ++authored;
    }
    std::cout << "PS1_TEXT_PAGING_PASS authored=" << authored
              << " battle_pages=" << battle_pages << " story_pages=" << story_pages << '\n';
}
