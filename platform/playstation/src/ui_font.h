// SPDX-License-Identifier: MIT
// Grandleon's PlayStation UI typeface.
//
// This is an original, repository-owned bitmap design made for the target's
// 320x240 television output.  Its compact capitals begin with the project's
// MIT-licensed console letter skeleton, then add PlayStation-specific spacing,
// a one-pixel cut-stone shadow, composed Latin accents and controller marks.
// No third-party font, tracing or converted system typeface is used here.
//
// The source is deliberately data rather than a prebuilt binary atlas.  The
// executable expands it into transparent 4bpp texels at boot, which lets a
// framebuffer test ask the same source which pixels should be ink while the
// GPU still exercises the real texture and CLUT path.

#pragma once

#include <cstddef>
#include <cstdint>

namespace grandleon::playstation::ui_font {

constexpr int cell_size = 8;
constexpr int ascii_first = 0x20;
constexpr int ascii_count = 0x40;  // space through underscore

enum class Accent : std::uint8_t {
    grave,
    acute,
    circumflex,
    tilde,
    diaeresis,
    cedilla,
};

struct AccentedGlyph final {
    std::uint32_t codepoint;
    char base;
    Accent accent;
};

inline constexpr AccentedGlyph accented[] = {
    {0x00C0U, 'A', Accent::grave},
    {0x00C1U, 'A', Accent::acute},
    {0x00C2U, 'A', Accent::circumflex},
    {0x00C3U, 'A', Accent::tilde},
    {0x00C4U, 'A', Accent::diaeresis},
    {0x00C7U, 'C', Accent::cedilla},
    {0x00C8U, 'E', Accent::grave},
    {0x00C9U, 'E', Accent::acute},
    {0x00CAU, 'E', Accent::circumflex},
    {0x00CBU, 'E', Accent::diaeresis},
    {0x00CCU, 'I', Accent::grave},
    {0x00CDU, 'I', Accent::acute},
    {0x00CEU, 'I', Accent::circumflex},
    {0x00CFU, 'I', Accent::diaeresis},
    {0x00D1U, 'N', Accent::tilde},
    {0x00D2U, 'O', Accent::grave},
    {0x00D3U, 'O', Accent::acute},
    {0x00D4U, 'O', Accent::circumflex},
    {0x00D5U, 'O', Accent::tilde},
    {0x00D6U, 'O', Accent::diaeresis},
    {0x00D9U, 'U', Accent::grave},
    {0x00DAU, 'U', Accent::acute},
    {0x00DBU, 'U', Accent::circumflex},
    {0x00DCU, 'U', Accent::diaeresis},
    {0x00DDU, 'Y', Accent::acute},
};

constexpr int accented_count =
    static_cast<int>(sizeof accented / sizeof accented[0]);

// Inverted Spanish punctuation, followed by the four marks printed on a
// PlayStation controller.  The multiplication sign is Cross: it is visually
// the controller mark, not the letter X hidden inside words such as NEXT.
constexpr std::uint32_t inverted_exclamation = 0x00A1U;
constexpr std::uint32_t inverted_question = 0x00BFU;
constexpr std::uint32_t controller_cross = 0x00D7U;
constexpr std::uint32_t controller_circle = 0x25CBU;
constexpr std::uint32_t controller_triangle = 0x25B3U;
constexpr std::uint32_t controller_square = 0x25A1U;
constexpr int special_count = 6;
constexpr int glyph_count = ascii_count + accented_count + special_count;

static_assert(glyph_count <= 128, "the UI font must fit its PS1 atlas");

namespace detail {

// One-bit face rows. Bits 6..2 are the five-pixel drawing area; the remaining
// columns leave room for the face's shadow and proportional tracking.
inline constexpr std::uint8_t ascii[ascii_count][cell_size] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x00}, // !
    {0x28,0x28,0x00,0x00,0x00,0x00,0x00,0x00}, // "
    {0x28,0x7C,0x28,0x7C,0x28,0x00,0x00,0x00}, // #
    {0x10,0x3C,0x50,0x38,0x14,0x78,0x10,0x00}, // $
    {0x64,0x68,0x08,0x10,0x20,0x2C,0x4C,0x00}, // %
    {0x30,0x48,0x50,0x20,0x54,0x48,0x34,0x00}, // &
    {0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00}, // '
    {0x08,0x10,0x20,0x20,0x20,0x10,0x08,0x00}, // (
    {0x20,0x10,0x08,0x08,0x08,0x10,0x20,0x00}, // )
    {0x00,0x10,0x54,0x38,0x54,0x10,0x00,0x00}, // *
    {0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00}, // +
    {0x00,0x00,0x00,0x00,0x18,0x10,0x20,0x00}, // ,
    {0x00,0x00,0x00,0x7C,0x00,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x00}, // .
    {0x04,0x08,0x10,0x20,0x40,0x00,0x00,0x00}, // /
    {0x38,0x44,0x4C,0x54,0x64,0x44,0x38,0x00}, // 0
    {0x10,0x30,0x10,0x10,0x10,0x10,0x38,0x00}, // 1
    {0x38,0x44,0x04,0x08,0x10,0x20,0x7C,0x00}, // 2
    {0x7C,0x08,0x10,0x08,0x04,0x44,0x38,0x00}, // 3
    {0x08,0x18,0x28,0x48,0x7C,0x08,0x08,0x00}, // 4
    {0x7C,0x40,0x78,0x04,0x04,0x44,0x38,0x00}, // 5
    {0x18,0x20,0x40,0x78,0x44,0x44,0x38,0x00}, // 6
    {0x7C,0x04,0x08,0x10,0x20,0x20,0x20,0x00}, // 7
    {0x38,0x44,0x44,0x38,0x44,0x44,0x38,0x00}, // 8
    {0x38,0x44,0x44,0x3C,0x04,0x08,0x30,0x00}, // 9
    {0x00,0x30,0x30,0x00,0x30,0x30,0x00,0x00}, // :
    {0x00,0x30,0x30,0x00,0x30,0x10,0x20,0x00}, // ;
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x00}, // <
    {0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00}, // =
    {0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x00}, // >
    {0x38,0x44,0x04,0x08,0x10,0x00,0x10,0x00}, // ?
    {0x38,0x44,0x04,0x34,0x54,0x54,0x38,0x00}, // @
    {0x38,0x44,0x44,0x7C,0x44,0x44,0x44,0x00}, // A
    {0x78,0x44,0x44,0x78,0x44,0x44,0x78,0x00}, // B
    {0x38,0x44,0x40,0x40,0x40,0x44,0x38,0x00}, // C
    {0x70,0x48,0x44,0x44,0x44,0x48,0x70,0x00}, // D
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x7C,0x00}, // E
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0x00}, // F
    {0x38,0x44,0x40,0x5C,0x44,0x44,0x3C,0x00}, // G
    {0x44,0x44,0x44,0x7C,0x44,0x44,0x44,0x00}, // H
    {0x38,0x10,0x10,0x10,0x10,0x10,0x38,0x00}, // I
    {0x1C,0x08,0x08,0x08,0x08,0x48,0x30,0x00}, // J
    {0x44,0x48,0x50,0x60,0x50,0x48,0x44,0x00}, // K
    {0x40,0x40,0x40,0x40,0x40,0x40,0x7C,0x00}, // L
    {0x44,0x6C,0x54,0x54,0x44,0x44,0x44,0x00}, // M
    {0x44,0x44,0x64,0x54,0x4C,0x44,0x44,0x00}, // N
    {0x38,0x44,0x44,0x44,0x44,0x44,0x38,0x00}, // O
    {0x78,0x44,0x44,0x78,0x40,0x40,0x40,0x00}, // P
    {0x38,0x44,0x44,0x44,0x54,0x48,0x34,0x00}, // Q
    {0x78,0x44,0x44,0x78,0x50,0x48,0x44,0x00}, // R
    {0x3C,0x40,0x40,0x38,0x04,0x04,0x78,0x00}, // S
    {0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x00}, // T
    {0x44,0x44,0x44,0x44,0x44,0x44,0x38,0x00}, // U
    {0x44,0x44,0x44,0x44,0x44,0x28,0x10,0x00}, // V
    {0x44,0x44,0x44,0x54,0x54,0x6C,0x44,0x00}, // W
    {0x44,0x44,0x28,0x10,0x28,0x44,0x44,0x00}, // X
    {0x44,0x44,0x28,0x10,0x10,0x10,0x10,0x00}, // Y
    {0x7C,0x04,0x08,0x10,0x20,0x40,0x7C,0x00}, // Z
    {0x38,0x20,0x20,0x20,0x20,0x20,0x38,0x00}, // [
    {0x40,0x20,0x10,0x08,0x04,0x00,0x00,0x00}, // backslash
    {0x38,0x08,0x08,0x08,0x08,0x08,0x38,0x00}, // ]
    {0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x7C,0x00}, // _
};

[[nodiscard]] constexpr std::uint32_t uppercase(
    std::uint32_t codepoint
) noexcept {
    if (codepoint >= 'a' && codepoint <= 'z') return codepoint - 0x20U;
    switch (codepoint) {
        case 0x00E0U: return 0x00C0U; case 0x00E1U: return 0x00C1U;
        case 0x00E2U: return 0x00C2U; case 0x00E3U: return 0x00C3U;
        case 0x00E4U: return 0x00C4U; case 0x00E7U: return 0x00C7U;
        case 0x00E8U: return 0x00C8U; case 0x00E9U: return 0x00C9U;
        case 0x00EAU: return 0x00CAU; case 0x00EBU: return 0x00CBU;
        case 0x00ECU: return 0x00CCU; case 0x00EDU: return 0x00CDU;
        case 0x00EEU: return 0x00CEU; case 0x00EFU: return 0x00CFU;
        case 0x00F1U: return 0x00D1U; case 0x00F2U: return 0x00D2U;
        case 0x00F3U: return 0x00D3U; case 0x00F4U: return 0x00D4U;
        case 0x00F5U: return 0x00D5U; case 0x00F6U: return 0x00D6U;
        case 0x00F9U: return 0x00D9U; case 0x00FAU: return 0x00DAU;
        case 0x00FBU: return 0x00DBU; case 0x00FCU: return 0x00DCU;
        case 0x00FDU: return 0x00DDU;
        default: return codepoint;
    }
}

[[nodiscard]] constexpr std::uint8_t accent_row(Accent accent) noexcept {
    switch (accent) {
        case Accent::grave: return 0x20;
        case Accent::acute: return 0x08;
        case Accent::circumflex: return 0x28;
        case Accent::tilde: return 0x54;
        case Accent::diaeresis: return 0x44;
        case Accent::cedilla: return 0x00;
    }
    return 0;
}

}  // namespace detail

struct Decoded final {
    std::uint32_t codepoint{'?'};
    std::size_t bytes{1U};
};

// Small, strict UTF-8 decoding. Invalid sequences consume one byte and render
// as '?', so malformed authored text cannot walk past a buffer or make every
// following glyph disappear.
[[nodiscard]] inline Decoded decode(const char* text) noexcept {
    if (text == nullptr || text[0] == '\0') return {0U, 0U};
    const auto first = static_cast<unsigned char>(text[0]);
    if (first < 0x80U) return {first, 1U};
    const auto continuation = [](unsigned char byte) {
        return (byte & 0xC0U) == 0x80U;
    };
    if ((first & 0xE0U) == 0xC0U && text[1] != '\0') {
        const auto second = static_cast<unsigned char>(text[1]);
        if (continuation(second)) {
            const std::uint32_t value =
                (static_cast<std::uint32_t>(first & 0x1FU) << 6U) |
                static_cast<std::uint32_t>(second & 0x3FU);
            if (value >= 0x80U) return {value, 2U};
        }
    }
    if ((first & 0xF0U) == 0xE0U && text[1] != '\0' && text[2] != '\0') {
        const auto second = static_cast<unsigned char>(text[1]);
        const auto third = static_cast<unsigned char>(text[2]);
        if (continuation(second) && continuation(third)) {
            const std::uint32_t value =
                (static_cast<std::uint32_t>(first & 0x0FU) << 12U) |
                (static_cast<std::uint32_t>(second & 0x3FU) << 6U) |
                static_cast<std::uint32_t>(third & 0x3FU);
            if (value >= 0x800U) return {value, 3U};
        }
    }
    return {'?', 1U};
}

[[nodiscard]] constexpr int slot_for(std::uint32_t given) noexcept {
    const std::uint32_t codepoint = detail::uppercase(given);
    if (codepoint >= static_cast<std::uint32_t>(ascii_first) &&
        codepoint < static_cast<std::uint32_t>(ascii_first + ascii_count)) {
        return static_cast<int>(codepoint) - ascii_first;
    }
    for (int index = 0; index < accented_count; ++index) {
        if (accented[index].codepoint == codepoint) return ascii_count + index;
    }
    const int first_special = ascii_count + accented_count;
    if (codepoint == inverted_exclamation) return first_special;
    if (codepoint == inverted_question) return first_special + 1;
    if (codepoint == controller_cross) return first_special + 2;
    if (codepoint == controller_circle) return first_special + 3;
    if (codepoint == controller_triangle) return first_special + 4;
    if (codepoint == controller_square) return first_special + 5;
    return '?' - ascii_first;
}

[[nodiscard]] constexpr bool supports(std::uint32_t given) noexcept {
    const std::uint32_t codepoint = detail::uppercase(given);
    if (codepoint >= static_cast<std::uint32_t>(ascii_first) &&
        codepoint < static_cast<std::uint32_t>(ascii_first + ascii_count)) {
        return true;
    }
    for (const AccentedGlyph& glyph : accented) {
        if (glyph.codepoint == codepoint) return true;
    }
    return codepoint == inverted_exclamation || codepoint == inverted_question ||
        codepoint == controller_cross || codepoint == controller_circle ||
        codepoint == controller_triangle || codepoint == controller_square;
}

[[nodiscard]] constexpr std::uint8_t face_row_for_slot(
    int slot, int row
) noexcept {
    if (slot < 0 || slot >= glyph_count || row < 0 || row >= cell_size) return 0;
    if (slot < ascii_count) return detail::ascii[slot][row];
    const int accent_index = slot - ascii_count;
    if (accent_index >= 0 && accent_index < accented_count) {
        const AccentedGlyph& glyph = accented[accent_index];
        const int base_slot = glyph.base - ascii_first;
        if (glyph.accent == Accent::cedilla) {
            return row == cell_size - 1 ? 0x10U : detail::ascii[base_slot][row];
        }
        return row == 0 ? detail::accent_row(glyph.accent)
                        : detail::ascii[base_slot][row - 1];
    }
    const int special = slot - ascii_count - accented_count;
    switch (special) {
        case 0: { // inverted !
            constexpr std::uint8_t rows[cell_size] =
                {0x10,0x00,0x10,0x10,0x10,0x10,0x10,0x00};
            return rows[row];
        }
        case 1: { // inverted ?
            constexpr std::uint8_t rows[cell_size] =
                {0x10,0x00,0x10,0x20,0x40,0x44,0x38,0x00};
            return rows[row];
        }
        case 2: { // Cross
            constexpr std::uint8_t rows[cell_size] =
                {0x00,0x44,0x28,0x10,0x28,0x44,0x00,0x00};
            return rows[row];
        }
        case 3: { // Circle
            constexpr std::uint8_t rows[cell_size] =
                {0x00,0x38,0x44,0x44,0x44,0x38,0x00,0x00};
            return rows[row];
        }
        case 4: { // Triangle
            constexpr std::uint8_t rows[cell_size] =
                {0x00,0x10,0x28,0x28,0x44,0x7C,0x00,0x00};
            return rows[row];
        }
        case 5: { // Square
            constexpr std::uint8_t rows[cell_size] =
                {0x00,0x7C,0x44,0x44,0x44,0x7C,0x00,0x00};
            return rows[row];
        }
        default: return 0;
    }
}

[[nodiscard]] constexpr bool face_pixel(int slot, int x, int y) noexcept {
    if (x < 0 || x >= cell_size) return false;
    return (face_row_for_slot(slot, y) & (0x80U >> x)) != 0U;
}

// 0 is transparent, 1 the dark cut-stone shadow, 2 the chosen face colour.
[[nodiscard]] constexpr std::uint8_t texel(int slot, int x, int y) noexcept {
    if (face_pixel(slot, x, y)) return 2U;
    return x > 0 && y > 0 && face_pixel(slot, x - 1, y - 1) ? 1U : 0U;
}

[[nodiscard]] constexpr int advance(std::uint32_t given) noexcept {
    const std::uint32_t codepoint = detail::uppercase(given);
    if (codepoint == ' ') return 4;
    if (codepoint == 'I' || codepoint == '1' || codepoint == '!' ||
        codepoint == '|' || codepoint == '\'' || codepoint == ':' ||
        codepoint == ';') return 4;
    if (codepoint == '.' || codepoint == ',') return 3;
    if (codepoint == 'M' || codepoint == 'W' || codepoint == '@' ||
        codepoint == controller_square) return 7;
    return 6;
}

[[nodiscard]] inline int measure(const char* text) noexcept {
    int width = 0;
    for (std::size_t at = 0; text != nullptr && text[at] != '\0';) {
        const Decoded decoded = decode(text + at);
        if (decoded.bytes == 0U) break;
        width += advance(decoded.codepoint);
        at += decoded.bytes;
    }
    return width;
}

struct Fit final {
    std::size_t bytes{};
    int width{};
    bool truncated{};
};

// The exact prefix a bounded row may draw. `reserved_pixels` is normally the
// ellipsis; keeping it in this pure helper makes the renderer and its layout
// check prove the same boundary arithmetic.
[[nodiscard]] inline Fit fit(
    const char* text, int max_pixels, int reserved_pixels = 0
) noexcept {
    if (text == nullptr || max_pixels <= 0) {
        return {0U, 0, text != nullptr && text[0] != '\0'};
    }
    const bool truncated = measure(text) > max_pixels;
    int available = max_pixels - (truncated ? reserved_pixels : 0);
    if (available < 0) available = 0;
    Fit result{0U, 0, truncated};
    while (text[result.bytes] != '\0') {
        const Decoded decoded = decode(text + result.bytes);
        if (decoded.bytes == 0U) break;
        const int next = advance(decoded.codepoint);
        if (result.width + next > available) break;
        result.width += next;
        result.bytes += decoded.bytes;
    }
    return result;
}

}  // namespace grandleon::playstation::ui_font
