/* SPDX-License-Identifier: MIT */
#ifndef GRANDLEON_PS1_TEXT_PAGING_H
#define GRANDLEON_PS1_TEXT_PAGING_H
#include <grandleon/view/console_text.h>

/* One source cursor is shared by rendering and Confirm navigation. Columns
 * count decoded console glyphs; returned cursors always begin a UTF-8 scalar. */
static inline unsigned grandleon_text_wrap_line(
    const char** text, char* output, unsigned columns
) {
    const char* cursor = *text;
    const char* space_end = 0;
    unsigned count = 0, space_column = 0;
    while (*cursor && *cursor != '\n' && count < columns) {
        const char character = grandleon_text_next(&cursor);
        if (output) output[count] = character;
        if (character == ' ') { space_column = count; space_end = cursor; }
        ++count;
    }
    if (*cursor && *cursor != '\n' && space_end && space_column > 0) {
        count = space_column;
        cursor = space_end;
    }
    if (output) output[count] = 0;
    while (*cursor == ' ' || *cursor == '\n') ++cursor;
    *text = cursor;
    return count;
}

static inline const char* grandleon_text_page_end(
    const char* text, unsigned columns, unsigned rows
) {
    if (!columns || !rows) return text;
    for (unsigned row = 0; *text && row < rows; ++row)
        grandleon_text_wrap_line(&text, 0, columns);
    return text;
}
#endif
