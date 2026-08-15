// SPDX-License-Identifier: MIT
/* The half of the render check that decides the gate.
 *
 * Usage:
 *   playstation_probe <log> <frame.bin> <frame.ppm>
 *
 * It reads two things a play executable and the emulator produced
 * independently, and requires them to agree at every visible cell:
 *
 *   - the log, which carries the executable's own claims. A `PROBE` line is a
 *     screen coordinate and the five-bit colour the executable computed from
 *     `grandleon::view` and the art library. A `READBACK` line is what the same
 *     executable then read back out of VRAM through GP0(0xC0).
 *   - the frame, which the emulator's Lua harness captured from the composited
 *     display and wrote out raw: 15-bit little-endian halfwords, cropped to
 *     exactly the active display, one halfword per pixel, no padding.
 *
 * ---------------------------------------------------------------------------
 * Why this program knows nothing
 *
 * It never reads the package, never sees the art, and knows nothing about the
 * board. It links nothing from this repository at all, so it cannot agree with
 * the executable by sharing a mistake with it. It has one number of its own,
 * the frame's dimensions, and it takes those from the harness's own `FRAME`
 * line rather than assuming them.
 *
 * There is no tolerance anywhere here. A PlayStation stores five bits per
 * channel and the frame is those bits verbatim, so an exact comparison is the
 * only honest one: nothing is expanded through a DAC ladder on the way out, so
 * there is no ladder to be approximately right about.
 *
 * ---------------------------------------------------------------------------
 * What agreement proves, and what it does not
 *
 * Three quantities meet at each cell, computed by three different machines:
 *
 *   claim     the executable's arithmetic over the projection and the CLUT
 *   readback  the GPU's rasteriser and CLUT unit, read back out of VRAM
 *   frame     the emulator's display window over that same VRAM
 *
 * claim == readback catches a texture uploaded to the wrong page, a CLUT
 * addressed wrongly, a rectangle drawn at the wrong coordinates, a nibble order
 * reversed, and a draw list sorted so that the wrong thing ends up on top.
 * readback == frame catches a picture drawn correctly somewhere the display
 * does not look. Neither catches a display that is switched off, which is why
 * the Lua harness reads the GPU's registers before either of these runs; see
 * `playstation_probe.lua`.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { max_probes = 1024, max_label = 64 };

struct claim {
    char label[max_label];
    int x;
    int y;
    int red;
    int green;
    int blue;
    int has_readback;
    int read_red;
    int read_green;
    int read_blue;
};

static struct claim claims[max_probes];
static int claim_count = 0;
static int checks = 0;
static int failures = 0;

static void expect(int condition, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void expect(int condition, const char *format, ...) {
    va_list arguments;
    ++checks;
    if (!condition) ++failures;
    fputs(condition ? "CHECK PASS " : "CHECK FAIL ", stdout);
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    fputc('\n', stdout);
}

static struct claim *find_claim(const char *label) {
    for (int index = 0; index < claim_count; ++index) {
        if (strcmp(claims[index].label, label) == 0) return &claims[index];
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <log> <frame.bin> <frame.ppm>\n", argv[0]);
        return 64;
    }

    FILE *log = fopen(argv[1], "r");
    if (log == NULL) {
        fprintf(stderr, "cannot open the log: %s\n", argv[1]);
        return 66;
    }

    int width = 0;
    int height = 0;
    int reported_probes = -1;
    int duplicate_labels = 0;
    int orphan_readbacks = 0;
    char line[512];
    while (fgets(line, (int)sizeof line, log) != NULL) {
        char label[max_label];
        int x = 0;
        int y = 0;
        int red = 0;
        int green = 0;
        int blue = 0;

        if (sscanf(line, "HARNESS FRAME %dx%d", &x, &y) == 2) {
            width = x;
            height = y;
            continue;
        }
        if (sscanf(line, "PROBE-COUNT %d", &x) == 1) {
            reported_probes = x;
            continue;
        }
        if (sscanf(line, "PROBE %63s %d %d %d %d %d", label, &x, &y, &red,
                   &green, &blue) == 6) {
            if (find_claim(label) != NULL) {
                ++duplicate_labels;
                continue;
            }
            if (claim_count >= max_probes) {
                fprintf(stderr, "more than %d probes\n", max_probes);
                fclose(log);
                return 70;
            }
            struct claim *entry = &claims[claim_count++];
            memset(entry, 0, sizeof *entry);
            snprintf(entry->label, sizeof entry->label, "%s", label);
            entry->x = x;
            entry->y = y;
            entry->red = red;
            entry->green = green;
            entry->blue = blue;
            continue;
        }
        if (sscanf(line, "READBACK %63s %d %d %d", label, &red, &green,
                   &blue) == 4) {
            struct claim *entry = find_claim(label);
            if (entry == NULL) {
                ++orphan_readbacks;
                continue;
            }
            entry->has_readback = 1;
            entry->read_red = red;
            entry->read_green = green;
            entry->read_blue = blue;
            continue;
        }
    }
    fclose(log);

    /* Everything below is a property of the *report*, before a single pixel is
     * compared. A log that lost lines, or repeated a label, or claimed a
     * different number of probes than it printed, is a broken channel and not a
     * picture to check. An emulator that silently discards debug output would
     * otherwise pass by printing nothing. */
    expect(claim_count > 0, "the executable claimed at least one pixel");
    expect(duplicate_labels == 0, "every probe label is distinct (%d repeated)",
           duplicate_labels);
    expect(orphan_readbacks == 0,
           "every readback names a probe (%d did not)", orphan_readbacks);
    expect(reported_probes == claim_count,
           "the executable printed the %d probes it counted (%d printed)",
           reported_probes, claim_count);
    expect(width > 0 && height > 0,
           "the harness reported the frame size (%dx%d)", width, height);
    if (failures != 0 || width <= 0 || height <= 0) {
        printf("RESULT FAIL %d/%d\n", checks - failures, checks);
        return 1;
    }

    FILE *frame_file = fopen(argv[2], "rb");
    if (frame_file == NULL) {
        fprintf(stderr, "cannot open the frame: %s\n", argv[2]);
        return 66;
    }
    const size_t pixels = (size_t)width * (size_t)height;
    uint16_t *frame = malloc(pixels * sizeof *frame);
    if (frame == NULL) {
        fclose(frame_file);
        fprintf(stderr, "out of memory for a %dx%d frame\n", width, height);
        return 70;
    }
    /* Read as bytes and assemble with shifts. The frame is little-endian
     * because that is what a PlayStation stores, and saying so explicitly costs
     * nothing and makes this program's answer independent of the host it runs
     * on, which is the same argument `engine/package_format` makes about
     * `put_u16`. */
    unsigned char *raw = malloc(pixels * 2);
    if (raw == NULL || fread(raw, 1, pixels * 2, frame_file) != pixels * 2) {
        fclose(frame_file);
        free(frame);
        free(raw);
        fprintf(stderr, "the frame is not %zu bytes\n", pixels * 2);
        return 65;
    }
    fclose(frame_file);
    for (size_t index = 0; index < pixels; ++index) {
        frame[index] = (uint16_t)((unsigned)raw[index * 2] |
                                  ((unsigned)raw[index * 2 + 1] << 8));
    }
    free(raw);

    /* The comparison. Every claim, against the pixel the emulator produced
     * there and against what the executable read back from VRAM itself. */
    int off_frame = 0;
    int missing_readback = 0;
    int readback_mismatch = 0;
    int frame_mismatch = 0;
    for (int index = 0; index < claim_count; ++index) {
        const struct claim *entry = &claims[index];
        if (entry->x < 0 || entry->y < 0 || entry->x >= width ||
            entry->y >= height) {
            ++off_frame;
            fprintf(stderr, "  %s claims (%d, %d), which is off a %dx%d frame\n",
                    entry->label, entry->x, entry->y, width, height);
            continue;
        }
        if (!entry->has_readback) {
            ++missing_readback;
            continue;
        }
        if (entry->read_red != entry->red || entry->read_green != entry->green ||
            entry->read_blue != entry->blue) {
            ++readback_mismatch;
            fprintf(stderr,
                    "  %s at (%d, %d): claimed %d %d %d, VRAM holds %d %d %d\n",
                    entry->label, entry->x, entry->y, entry->red, entry->green,
                    entry->blue, entry->read_red, entry->read_green,
                    entry->read_blue);
            continue;
        }
        const uint16_t seen = frame[(size_t)entry->y * (size_t)width +
                                    (size_t)entry->x];
        const int red = seen & 0x1F;
        const int green = (seen >> 5) & 0x1F;
        const int blue = (seen >> 10) & 0x1F;
        if (red != entry->red || green != entry->green || blue != entry->blue) {
            ++frame_mismatch;
            fprintf(stderr,
                    "  %s at (%d, %d): claimed %d %d %d, the frame shows "
                    "%d %d %d\n",
                    entry->label, entry->x, entry->y, entry->red, entry->green,
                    entry->blue, red, green, blue);
        }
    }

    expect(off_frame == 0, "every claim is inside the frame (%d were not)",
           off_frame);
    expect(missing_readback == 0,
           "every claim has a VRAM readback (%d did not)", missing_readback);
    expect(readback_mismatch == 0,
           "the GPU stored what the executable claimed, at all %d cells "
           "(%d differ)",
           claim_count, readback_mismatch);
    expect(frame_mismatch == 0,
           "the display shows what the executable claimed, at all %d cells "
           "(%d differ)",
           claim_count, frame_mismatch);

    /* A fingerprint of the whole frame, printed and not asserted. It is what a
     * human comparing two runs wants; pinning it would be a strong regression
     * check with an opaque failure, and the per-cell claims above already say
     * *where* a picture went wrong. FNV-1a over the little-endian bytes, which
     * is the digest this repository already uses everywhere else. */
    uint64_t digest = 0xcbf29ce484222325ULL;
    for (size_t index = 0; index < pixels; ++index) {
        digest ^= (uint64_t)(frame[index] & 0xFF);
        digest *= 0x00000100000001b3ULL;
        digest ^= (uint64_t)(frame[index] >> 8);
        digest *= 0x00000100000001b3ULL;
    }
    printf("FRAME %dx%d cells %d digest %016llx\n", width, height, claim_count,
           (unsigned long long)digest);

    /* And the frame itself, beside the log, as a portable pixmap. Not a PNG,
     * because a PNG needs a deflate stream and this program has no business
     * carrying one; a P6 pixmap is a three-line header and the bytes, and every
     * image viewer reads it. Five-bit channels are widened to eight by
     * replicating the top bits, so white is white. That widening is for a
     * human looking at the file and is not what anything above compares. */
    FILE *pixmap = fopen(argv[3], "wb");
    if (pixmap != NULL) {
        fprintf(pixmap, "P6\n%d %d\n255\n", width, height);
        for (size_t index = 0; index < pixels; ++index) {
            const uint16_t colour = frame[index];
            const unsigned channels[3] = {
                (unsigned)(colour & 0x1F),
                (unsigned)((colour >> 5) & 0x1F),
                (unsigned)((colour >> 10) & 0x1F),
            };
            for (int channel = 0; channel < 3; ++channel) {
                const unsigned value = channels[channel];
                fputc((int)((value << 3) | (value >> 2)), pixmap);
            }
        }
        fclose(pixmap);
    }
    free(frame);

    printf("RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
           checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
