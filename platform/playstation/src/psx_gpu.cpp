// SPDX-License-Identifier: MIT
// See psx_gpu.h. Nothing here is in `engine/`.

#include "psx_gpu.h"

#include <cstdint>

namespace grandleon::playstation::gpu {
namespace {

// The two GPU ports, uncached through KSEG1 so a write reaches the hardware
// rather than sitting in a write buffer the next read would not see.
//
//   GP0  write: commands and the data of a VRAM transfer
//        read:  GPUREAD, the data of a VRAM-to-CPU transfer
//   GP1  write: control (reset, display mode, display area, DMA direction)
//        read:  GPUSTAT
volatile std::uint32_t* const gp0 =
    reinterpret_cast<volatile std::uint32_t*>(0xbf801810);
volatile std::uint32_t* const gp1 =
    reinterpret_cast<volatile std::uint32_t*>(0xbf801814);

// GPUSTAT bits this file waits on. 26 and 28 are the two the manual
// distinguishes and the distinction is real: a command word may be accepted
// while a transfer block may not.
constexpr std::uint32_t status_ready_for_command = 1u << 26;
constexpr std::uint32_t status_ready_to_send = 1u << 27;
constexpr std::uint32_t status_ready_for_data = 1u << 28;

void wait_for(std::uint32_t bit) {
    while ((*gp1 & bit) == 0) {
    }
}

void command(std::uint32_t word) {
    wait_for(status_ready_for_command);
    *gp0 = word;
}

// A command's two coordinate words. Both are packed the same way and both are
// masked to the field widths the hardware decodes, so an out-of-range caller
// wraps inside VRAM rather than reaching into an adjacent field.
void coordinates(int x, int y) {
    *gp0 = (static_cast<std::uint32_t>(y) & 0xFFFFu) << 16 |
           (static_cast<std::uint32_t>(x) & 0xFFFFu);
}

void control(std::uint32_t op, std::uint32_t parameter) {
    *gp1 = (op << 24) | (parameter & 0x00FFFFFFu);
}

}  // namespace

void begin(std::uint16_t backdrop) {
    // These are the invariants the VRAM map in the header rests on. They are
    // static_asserts rather than run-time checks because getting one wrong is
    // a compile-time mistake about a compile-time layout: the picture would
    // still be drawn, into a texture page, and nothing would say so.
    static_assert(screen_width <= first_texture_page * page_halfwords,
                  "the framebuffer runs into the first texture page");
    static_assert(clut_y >= screen_height,
                  "the CLUT band runs into the framebuffer");
    static_assert(clut_y + clut_rows <= texture_page_y,
                  "the CLUT band runs into the texture pages");
    static_assert(
        (first_texture_page + texture_page_count) * page_halfwords <= vram_width,
        "the texture pages run off the right of VRAM");
    static_assert(texture_page_y + page_lines <= vram_height,
                  "the texture pages run off the bottom of VRAM");
    static_assert(font_x >= screen_width,
                  "the font sheet runs into the framebuffer");
    static_assert(font_y + font_lines <= clut_y,
                  "the font sheet runs into the CLUT band");
    static_assert(font_columns * glyph_texels <= 256,
                  "a texture coordinate cannot address the whole font sheet");

    control(0x00, 0);  // reset: clears the FIFO, the display mode and the IRQ
    control(0x03, 1);  // display off while the picture is being made
    control(0x04, 0);  // DMA off; every transfer below is CPU-driven

    // 320x240, NTSC, 15-bit colour, no interlace. Bits 0..1 select the
    // horizontal resolution, bit 2 the vertical, bit 3 the video standard,
    // bit 4 the colour depth and bit 5 the interlace.
    control(0x08, 0x01);
    control(0x05, 0);  // the display starts at the VRAM origin

    // The display range, in GPU clocks horizontally and in scanlines
    // vertically. 320 mode spends eight clocks a pixel, so 320 pixels is 2,560
    // clocks from the standard 0x200 left edge; 240 lines is 16 to 256, which
    // is centred on the same 0x88 the BIOS uses.
    control(0x06, 0x200u | (static_cast<std::uint32_t>(0x200 + screen_width * 8) << 12));
    control(0x07, 16u | (static_cast<std::uint32_t>(16 + screen_height) << 10));

    // The drawing area is the framebuffer, exactly, and the offset is zero, so
    // a primitive's coordinates are screen coordinates are VRAM coordinates.
    // Bit 10 of the draw mode, "drawing to the display area allowed", is set
    // because on a single-buffered machine the display area is the only place
    // there is to draw, and with it clear the GPU silently discards every
    // primitive.
    command(0xE1u << 24 | (1u << 10));
    command(0xE2u << 24);  // no texture window
    command(0xE3u << 24 | 0u);
    command(0xE4u << 24 |
            (static_cast<std::uint32_t>(screen_height - 1) << 10 |
             static_cast<std::uint32_t>(screen_width - 1)));
    command(0xE5u << 24 | 0u);
    command(0xE6u << 24 | 0u);  // the mask bit is neither set nor tested

    // The backdrop, through GP0(0x02), which writes VRAM directly and ignores
    // the drawing area and the mask. Its colour is 24-bit and the hardware
    // keeps the top five bits of each channel, so the five-bit value is shifted
    // up rather than expanded. A five-bit level round trips exactly.
    command(0x02u << 24 |
            (static_cast<std::uint32_t>(blue_of(backdrop)) << 19) |
            (static_cast<std::uint32_t>(green_of(backdrop)) << 11) |
            (static_cast<std::uint32_t>(red_of(backdrop)) << 3));
    coordinates(0, 0);
    coordinates(screen_width, screen_height);
}

void show() {
    control(0x03, 0);
}

void upload(int x, int y, int halfwords_across, int lines,
            const unsigned short* source) {
    if (source == nullptr || halfwords_across <= 0 || lines <= 0) return;
    command(0xA0u << 24);
    coordinates(x, y);
    coordinates(halfwords_across, lines);
    const int total = halfwords_across * lines;
    // Two halfwords to a word, low half first, because that is the order VRAM
    // fills. Assembled by shifting rather than by casting the source to a
    // wider pointer: the source is `unsigned short` data generated on a host,
    // and a shift says what it means on any byte order and at any alignment.
    for (int index = 0; index + 1 < total; index += 2) {
        wait_for(status_ready_for_data);
        *gp0 = static_cast<std::uint32_t>(source[index]) |
               (static_cast<std::uint32_t>(source[index + 1]) << 16);
    }
    if ((total & 1) != 0) {
        wait_for(status_ready_for_data);
        *gp0 = static_cast<std::uint32_t>(source[total - 1]);
    }
}

std::uint16_t read_back(int x, int y) {
    command(0xC0u << 24);
    coordinates(x, y);
    coordinates(1, 1);
    wait_for(status_ready_to_send);
    // One halfword was asked for and the port is 32 bits wide, so the hardware
    // returns one word whose upper half is whatever the second texel of the
    // pair would have been. Only the low half is a promise.
    const std::uint32_t word = *gp0;
    return static_cast<std::uint16_t>(word & 0xFFFFu);
}

void draw_cell(int screen_x, int screen_y, int cell, int clut) {
    const int page = first_texture_page + cell / cells_per_page;
    const int within = cell % cells_per_page;
    const int u = (within % cells_per_page_row) * cell_texels;
    const int v = (within / cells_per_page_row) * cell_texels;

    // The texture page, reselected per cell. A textured *rectangle* carries no
    // page field of its own (only polygons do), so the page is part of the
    // draw mode and has to be set before the primitive. Bit 4 carries the Y
    // base, bits 7..8 the colour depth, which is 0 for 4bpp, and bit 10 is
    // again the permission to draw where the display is.
    command(0xE1u << 24 | static_cast<std::uint32_t>(page) |
            (static_cast<std::uint32_t>(texture_page_y / page_lines) << 4) |
            (1u << 10));

    // GP0(0x65): textured rectangle, variable size, opaque, raw texture. The
    // colour word is present in the packet and ignored by a raw draw; 0x808080
    // is the neutral value a blending draw would need, written here so that
    // switching the command to 0x64 one day changes the blend and nothing else.
    command(0x65u << 24 | 0x808080u);
    coordinates(screen_x, screen_y);
    // The CLUT field addresses VRAM in units of sixteen halfwords across and
    // one line down, which is exactly the grid `clut_x_of` and `clut_y_of` lay
    // CLUTs out on.
    *gp0 = (static_cast<std::uint32_t>(clut_y_of(clut)) << 22) |
           (static_cast<std::uint32_t>(clut_x_of(clut) / clut_entries) << 16) |
           (static_cast<std::uint32_t>(v) << 8) |
           static_cast<std::uint32_t>(u);
    coordinates(cell_texels, cell_texels);
}

void draw_cell_scaled(int screen_x, int screen_y, int size, int cell,
                      int clut) {
    if (size <= 0) return;
    // The native size goes through the rectangle it always went through, so a
    // board that needs no scaling draws the pixels it has always drawn.
    if (size == cell_texels) {
        draw_cell(screen_x, screen_y, cell, clut);
        return;
    }

    const int page = first_texture_page + cell / cells_per_page;
    const int within = cell % cells_per_page;
    const int u = (within % cells_per_page_row) * cell_texels;
    const int v = (within / cells_per_page_row) * cell_texels;
    // The far texture coordinate is the cell's last texel, not the first of
    // the next cell. See the header: this is what keeps a shrunk cell from
    // fringing with its neighbour's art.
    const int u_far = u + cell_texels - 1;
    const int v_far = v + cell_texels - 1;

    // The texture page, as a polygon attribute rather than as a draw-mode
    // command. The field is the same one GP0(0xE1) takes: bits 0..3 the X
    // base, bit 4 the Y base, bits 7..8 the colour depth, which is 0 for 4bpp.
    // Bit 10 is not set here and does not need to be: it is a draw-mode
    // permission the mode command carries, and the drawing area already
    // excludes the display.
    const std::uint32_t texpage =
        static_cast<std::uint32_t>(page) |
        (static_cast<std::uint32_t>(texture_page_y / page_lines) << 4);
    const std::uint32_t palette =
        (static_cast<std::uint32_t>(clut_y_of(clut)) << 6) |
        static_cast<std::uint32_t>(clut_x_of(clut) / clut_entries);

    // GP0(0x2D): textured four-point polygon, opaque, raw texture. The colour
    // word is present and ignored by a raw draw, and carries the neutral value
    // for the same reason `draw_cell`'s does.
    command(0x2Du << 24 | 0x808080u);
    // The four corners, in the order the hardware reads them: top left, top
    // right, bottom left, bottom right. It draws two triangles from that, and
    // any other order folds the quad.
    //
    // The far corners are at `screen + size`, one past the last pixel drawn,
    // because this rasteriser fills up to but not including its right and
    // bottom edges. Placing them on the last pixel instead would drop a row
    // and a column of every cell on the board.
    coordinates(screen_x, screen_y);
    *gp0 = (palette << 16) | (static_cast<std::uint32_t>(v) << 8) |
           static_cast<std::uint32_t>(u);
    coordinates(screen_x + size, screen_y);
    *gp0 = (texpage << 16) | (static_cast<std::uint32_t>(v) << 8) |
           static_cast<std::uint32_t>(u_far);
    coordinates(screen_x, screen_y + size);
    *gp0 = (static_cast<std::uint32_t>(v_far) << 8) |
           static_cast<std::uint32_t>(u);
    coordinates(screen_x + size, screen_y + size);
    *gp0 = (static_cast<std::uint32_t>(v_far) << 8) |
           static_cast<std::uint32_t>(u_far);
}

void fill(int screen_x, int screen_y, int width, int height,
          std::uint16_t colour) {
    if (width <= 0 || height <= 0) return;
    // GP0(0x60): monochrome rectangle, variable size, opaque. The colour word
    // is 24-bit and the hardware keeps the top five bits of each channel, so a
    // five-bit level is shifted up rather than expanded and round trips
    // exactly, by the same arithmetic `begin` does for the backdrop.
    command(0x60u << 24 |
            (static_cast<std::uint32_t>(blue_of(colour)) << 19) |
            (static_cast<std::uint32_t>(green_of(colour)) << 11) |
            (static_cast<std::uint32_t>(red_of(colour)) << 3));
    coordinates(screen_x, screen_y);
    coordinates(width, height);
}

void draw_glyph(int screen_x, int screen_y, int slot, int clut) {
    if (slot < 0 || slot >= font_capacity) return;

    // The font's own page, at Y base zero. Reselected per glyph for the reason
    // `draw_cell` reselects a cell's: a textured rectangle carries no page
    // field, so the page is part of the draw mode.
    command(0xE1u << 24 | static_cast<std::uint32_t>(font_page) |
            (static_cast<std::uint32_t>(font_page_y / page_lines) << 4) |
            (1u << 10));

    command(0x65u << 24 | 0x808080u);
    coordinates(screen_x, screen_y);
    *gp0 = (static_cast<std::uint32_t>(clut_y_of(clut)) << 22) |
           (static_cast<std::uint32_t>(clut_x_of(clut) / clut_entries) << 16) |
           (static_cast<std::uint32_t>(glyph_texture_v(slot)) << 8) |
           static_cast<std::uint32_t>(glyph_texture_u(slot));
    coordinates(glyph_texels, glyph_texels);
}

std::uint32_t status() {
    return *gp1;
}

}  // namespace grandleon::playstation::gpu
