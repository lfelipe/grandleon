// SPDX-License-Identifier: MIT
// The PlayStation GPU, as much of it as drawing a board needs.
//
// Nothing here is in `engine/`, and nothing in `engine/` knows it exists. No
// Nugget header is on the include path either: Nugget's `psyqo` and its C
// headers are inline assembly throughout, and this repository's
// `-Wpedantic -Wconversion -Werror` discipline would have to be relaxed for a
// translation unit that saw one. The two ports and the handful of command
// encodings this file needs are written out instead, which is the same
// arrangement `psx_runtime.cpp` arrived at for the BIOS teletype.
//
// ---------------------------------------------------------------------------
// The one decision that shapes everything else: single buffering
//
// VRAM is a 1024x512 array of 16-bit halfwords, and the display is a window
// into it. This target puts the framebuffer at the origin and leaves it there:
// GP1(0x05) sets the display start to (0, 0) and the drawing area is the same
// rectangle, so **a screen coordinate is a VRAM coordinate**, with no offset
// and no page flip to reason about.
//
// That is worth more here than a spare frame of latency. The picture is a
// rectangle of memory that both the renderer and any observer can name the
// same way, and the executable's own report can therefore be checked
// against the emulator's frame *and* against VRAM read back through the GPU,
// which are two different questions.
//
// Single buffering does mean a moving picture is drawn where the beam can see
// it. That was free when nothing moved; now that a board is steered it is a
// choice, and it is made in favour of the harness: an observer that can name a
// pixel by its screen coordinate is worth more than a frame of latency, and
// the tearing it costs is a raised cell redrawn a scanline early rather than a
// wrong picture. `turn_exe.cpp` writes every animation frame inside vertical
// blanking to keep even that much off the screen.
//
// ---------------------------------------------------------------------------
// Where things go in VRAM
//
//     Y 0..239    X 0..319     the framebuffer, and the display
//     Y 0..15     X 320..383   the font, one 4bpp sheet of sixty-four glyphs
//     Y 240..255  X 0..1023    CLUTs, sixteen halfwords each on a 16-halfword
//                              grid: 64 to a row, 16 rows, 1,024 in all
//     Y 256..511  X 320..1023  eleven 4bpp texture pages, 64 cells each
//
// The font's row is the one part of this map that was added when a board
// learned to say things. It costs nothing anything else wanted: the rectangle
// to the right of the framebuffer and above the CLUT band (Y 0..239,
// X 320..1023) is 168 KiB that no region above claims, because a texture page
// whose Y base is zero overlaps the framebuffer for its first 320 halfwords and
// was therefore never usable as a page. Sixteen lines of it are, as long as
// nothing needs a whole page there, and a font does not.
//
// A texture page is fixed at 64 halfwords by 256 lines, and its X base must be
// a multiple of 64. That is the hardware's arithmetic, not a choice. A cell is
// 32 texels square at 4bpp, so it is eight halfwords wide and 32 lines tall,
// and a page holds eight across by eight down. Pages 0 to 4 overlap the
// framebuffer and are therefore not used; pages 5 to 15 at Y base 256 are, and
// 704 cells is far more than a board can ask for.
//
// The regions do not overlap, which `psx_gpu.cpp` asserts rather than assumes.

#pragma once

#include <cstdint>

namespace grandleon::playstation::gpu {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

// The active display. 320x240 is the NTSC mode with square-ish pixels and the
// widest choice of vertical range; it is set explicitly by `begin()` rather
// than inherited from whatever the BIOS left behind, so the harness can check
// the registers against these numbers.
constexpr int screen_width = 320;
constexpr int screen_height = 240;

// VRAM, in halfwords across and lines down.
constexpr int vram_width = 1024;
constexpr int vram_height = 512;

// A 4bpp texture page, in halfwords across and lines down. Both are fixed by
// the hardware.
constexpr int page_halfwords = 64;
constexpr int page_lines = 256;

// The art library's cell, in texels, and what it costs in VRAM.
constexpr int cell_texels = 32;
constexpr int cell_halfwords = cell_texels / 4;
constexpr int cells_per_page_row = page_halfwords / cell_halfwords;
constexpr int cells_per_page_column = page_lines / cell_texels;
constexpr int cells_per_page = cells_per_page_row * cells_per_page_column;

// The first texture page clear of the framebuffer, and how many there are.
constexpr int first_texture_page = 5;
constexpr int texture_page_count = 11;
constexpr int texture_page_y = 256;
constexpr int cell_capacity = texture_page_count * cells_per_page;

// The font sheet: sixty-four glyphs of eight texels square, thirty-two across
// and two down, at 4bpp. Thirty-two glyphs of eight texels is 256, which is
// exactly a texture page's width, and it is the widest a texture coordinate can
// address: the U field of a rectangle packet is eight bits.
//
// It lives in texture page `first_texture_page` at Y base zero: the page's X
// base is clear of the framebuffer, and its first sixteen lines are clear of
// the CLUT band, which is the whole of what the map above has to be true about.
constexpr int glyph_texels = 8;
constexpr int glyph_halfwords = glyph_texels / 4;
constexpr int font_columns = 32;
constexpr int font_rows = 2;
constexpr int font_page = first_texture_page;
constexpr int font_page_y = 0;
constexpr int font_x = font_page * page_halfwords;
constexpr int font_y = font_page_y;
constexpr int font_halfwords_across = font_columns * glyph_halfwords;
constexpr int font_lines = font_rows * glyph_texels;
constexpr int font_capacity = font_columns * font_rows;

// The CLUT band, between the framebuffer and the texture pages.
constexpr int clut_entries = 16;
constexpr int clut_y = screen_height;
constexpr int clut_rows = texture_page_y - clut_y;
constexpr int cluts_per_row = vram_width / clut_entries;
constexpr int clut_capacity = clut_rows * cluts_per_row;

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------

// A VRAM halfword is `SBBBBBGG GGGRRRRR`. The top bit is the semi-transparency
// flag, which every draw this target issues ignores; the five-bit channels are
// what a probe compares, levels rather than bytes. Five bits is what the
// machine stores, and anything else is a claim about a digital-to-analogue
// converter.
[[nodiscard]] constexpr int red_of(std::uint16_t colour) noexcept {
    return colour & 0x1F;
}
[[nodiscard]] constexpr int green_of(std::uint16_t colour) noexcept {
    return (colour >> 5) & 0x1F;
}
[[nodiscard]] constexpr int blue_of(std::uint16_t colour) noexcept {
    return (colour >> 10) & 0x1F;
}

// The other direction, for a colour named somewhere in five-bit levels rather
// than checked out of a generated palette. The semi-transparency bit is left
// clear, which is what every draw this target issues wants; a level past
// thirty-one is a caller's arithmetic error and is masked rather than allowed
// to bleed into the channel above it.
[[nodiscard]] constexpr std::uint16_t rgb15(int red, int green, int blue) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(red & 0x1F) |
        static_cast<std::uint16_t>((green & 0x1F) << 5) |
        static_cast<std::uint16_t>((blue & 0x1F) << 10)
    );
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

// Resets the GPU, sets the display mode and the drawing area, and paints the
// framebuffer `backdrop`. The display stays *off* until `show()`, so nothing
// half-drawn is ever the picture.
void begin(std::uint16_t backdrop);

// Turns the display on. Idempotent.
void show();

// Copies `halfwords_across` by `lines` halfwords into VRAM at (`x`, `y`).
// GP0(0xA0), CPU-driven: the source is read as halfwords and paired into the
// 32-bit words the port takes, so nothing here depends on the host's byte
// order or on the source's alignment.
void upload(int x, int y, int halfwords_across, int lines,
            const unsigned short* source);

// One halfword of VRAM, read back through GP0(0xC0). This is the machine
// answering what it actually stored, through the same port a texture went in
// by. It is a different question from what the executable believes it drew,
// which is the point of asking it.
[[nodiscard]] std::uint16_t read_back(int x, int y);

// Draws one cell of a texture page at (`screen_x`, `screen_y`), through
// `clut`. `cell` indexes the allocation `cell_texture_*` describes.
//
// The primitive is GP0(0x65): a textured rectangle, opaque and *raw*. The
// texture is not modulated by a vertex colour, so the halfword the CLUT holds
// is the halfword that lands in VRAM, exactly. A texel whose CLUT entry is
// zero is not drawn at all, which is how the art library's transparent index
// becomes transparency here.
void draw_cell(int screen_x, int screen_y, int cell, int clut);

// Draws one cell into a `size` by `size` square, shrinking or enlarging the
// art to fit it.
//
// This exists because `draw_cell` cannot do it, and the reason is the
// primitive rather than the code. A textured *rectangle* has no scale: the
// hardware walks one texel across for every pixel across, so a rectangle
// asked for twenty pixels draws the sprite's first twenty columns and throws
// the rest away. Cropping a character is not shrinking one.
//
// So this issues GP0(0x2D) instead: a textured four-point polygon, opaque and
// raw, whose four vertices each carry their own texture coordinate. The
// hardware interpolates between them, which is what makes the picture fit the
// square rather than be cut to it. A polygon also carries its own texture page
// field, so unlike `draw_cell` this needs no separate draw-mode command.
//
// The sampling is nearest-neighbour, because this GPU has no filter. A cell
// drawn smaller therefore drops texels rather than blending them, and the
// result is crunchier than the same board on a machine that filters. That is
// a property of the console and not something to fix here.
//
// **A cell never samples past its own thirty-two columns.** The texture
// coordinates span `u` to `u + 31` rather than `u + 32`, so the far edge lands
// on this cell's last texel instead of the first texel of whichever sprite is
// stored next to it. Getting that wrong would not look like a bug, it would
// look like a one-pixel seam of somebody else's art.
//
// `size` at or below zero draws nothing. `size` equal to `cell_texels` is
// handed to `draw_cell`, so a board drawn at the native size is drawn by the
// primitive it has always used and its pixels are unchanged.
void draw_cell_scaled(int screen_x, int screen_y, int size, int cell, int clut);

// Where a cell lives, so a caller can upload into it and a probe can name it.
[[nodiscard]] constexpr int cell_texture_x(int cell) noexcept {
    const int page = cell / cells_per_page;
    const int within = cell % cells_per_page;
    return (first_texture_page + page) * page_halfwords +
           (within % cells_per_page_row) * cell_halfwords;
}
[[nodiscard]] constexpr int cell_texture_y(int cell) noexcept {
    return texture_page_y + (cell % cells_per_page) / cells_per_page_row * cell_texels;
}

// Where a CLUT lives.
[[nodiscard]] constexpr int clut_x_of(int clut) noexcept {
    return (clut % cluts_per_row) * clut_entries;
}
[[nodiscard]] constexpr int clut_y_of(int clut) noexcept {
    return clut_y + clut / cluts_per_row;
}

// Paints a rectangle in one flat colour, opaquely, through GP0(0x60).
//
// Not GP0(0x02), which is what `begin` uses for the backdrop: that command
// writes VRAM directly and rounds its X and its width to sixteen-pixel
// boundaries, so a box eight pixels wide would be sixteen and a box at column
// three would be at column zero. A monochrome rectangle is per-pixel, respects
// the drawing area, and is what an interface has to be drawn out of.
void fill(int screen_x, int screen_y, int width, int height,
          std::uint16_t colour);

// Draws one glyph of the font sheet at (`screen_x`, `screen_y`) through `clut`.
// `slot` is the glyph's index into the sheet, which is its character less
// `view::first_glyph`; out of range draws nothing rather than whatever is next
// in the page.
void draw_glyph(int screen_x, int screen_y, int slot, int clut);

// Where a glyph lives in the sheet, so a caller can upload into it.
[[nodiscard]] constexpr int glyph_texture_u(int slot) noexcept {
    return (slot % font_columns) * glyph_texels;
}
[[nodiscard]] constexpr int glyph_texture_v(int slot) noexcept {
    return (slot / font_columns) * glyph_texels;
}

// GPUSTAT, unfiltered, so the report can carry what the hardware says about
// itself rather than what this file believes it asked for.
[[nodiscard]] std::uint32_t status();

// Bit 23 of GPUSTAT is set while the display is *disabled*, which is the one
// bit that decides whether a correctly drawn framebuffer is on screen at all.
constexpr std::uint32_t status_display_disabled = 1u << 23;

}  // namespace grandleon::playstation::gpu
