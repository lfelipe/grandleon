// SPDX-License-Identifier: MIT
/* The half of the turn check that decides the gate.
 *
 * Usage:
 *   playstation_turn_probe <log> <expectations.txt> <frame-dir>
 *
 * It reads three things three different machines produced independently, and
 * requires all of them to agree:
 *
 *   - **the expectations**, derived on the host by `grandleon_playstation_expect`
 *     before the executable was built: the controller script, and the
 *     `CHECKPOINT`/`FACT` transcript the rules say it adds up to;
 *   - **the log**, which is what the R3000A said. The same transcript, plus a
 *     `PROBE` line per claimed pixel (a screen coordinate and the five-bit
 *     colour the executable computed from `grandleon::view`, the art library
 *     and its own interface constants) and a `READBACK` line saying what the
 *     GPU then stored at that address, fetched through GP0(0xC0);
 *   - **the frames**, one per checkpoint, captured by the emulator's Lua
 *     harness from the composited display at the instant the executable named,
 *     and written out raw: 15-bit little-endian halfwords, cropped to exactly
 *     the active display, one halfword per pixel, no padding.
 *
 * ---------------------------------------------------------------------------
 * Why this program knows nothing
 *
 * It never reads the package, never sees the art, and knows nothing about the
 * board or the rules, the same discipline `playstation_probe.c` follows and
 * for the same reason. It links nothing from this repository at all, so it
 * cannot agree with the executable by sharing a mistake with it. Its only
 * numbers of its own are the frame's dimensions, and it takes those from the
 * harness's own `FRAMES` line rather than assuming them.
 *
 * There is no tolerance anywhere. A PlayStation stores five bits per channel
 * and a frame is those bits verbatim.
 *
 * ---------------------------------------------------------------------------
 * What agreement proves
 *
 *   transcript  the console reached the state the rules say those presses reach
 *   claim       the executable's arithmetic over the projection and the CLUT
 *   readback    the GPU's rasteriser and CLUT unit, out of VRAM
 *   frame       the emulator's display window over that same VRAM
 *
 * A client that reaches the right state and draws the wrong picture fails on
 * the second; a client that draws a right picture into the wrong place fails on
 * the third; a client that agrees with itself about a state the engine never
 * reaches fails on the first. The expectations exist before the executable does,
 * so no run can be made to pass by adjusting one.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    max_line = 512,
    max_label = 64,
    max_probes_per_block = 512,
    max_expected = 262144,
    max_frames = 512
};

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

static struct claim claims[max_probes_per_block];
static int claim_count = 0;
static int declared_probes = -1;

static char *expected_lines[max_expected];
static int expected_count = 0;
static int expected_index = 0;

static int checks = 0;
static int failures = 0;

static int checkpoints_seen = 0;
static int blocks_compared = 0;
static int pixels_compared = 0;
static char block_name[max_label] = "";
static int block_ok = 1;
static int block_open = 0;

static int frame_width = 0;
static int frame_height = 0;
static int frames_reported = -1;
static const char *frame_dir = NULL;
static uint16_t *frame_pixels = NULL;
/* Pixels `frame_pixels` was allocated for, so the read below is bounded by the
 * buffer it writes into rather than by an argument about the rest of the file. */
static size_t frame_capacity = 0;
static int frame_loaded = -1;

static int harness_result_seen = 0;
static int harness_passed = 0;
static int rom_result_seen = 0;
static int rom_passed = 0;

/* The script, counted at both ends.
 *
 * A harness with pad ports to play the `PRESS` lines into could not let the
 * expectation file and the run differ. Here the script is compiled into the
 * executable and this file only records it, so a script edited without
 * re-deriving, or re-derived without rebuilding, is a real way for the two
 * halves to drift. The transcript comparison would catch it, but it
 * would catch it as a wall of mismatched facts; these three numbers say what
 * actually happened. */
static int script_presses = 0;
static int declared_script = -1;
static int played_presses = -1;

static void expect(int condition, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void expect(int condition, const char *format, ...) {
    va_list arguments;
    ++checks;
    if (!condition) ++failures;
    fputs(condition ? "ok   " : "FAIL ", stdout);
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    fputc('\n', stdout);
}

static void report(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
}

static void trim(char *line) {
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' ||
                          line[length - 1] == ' ')) {
        line[--length] = '\0';
    }
}

/* ------------------------------------------------------------------------- */
/* The frames                                                                */
/* ------------------------------------------------------------------------- */

static int load_frame(int index) {
    char path[1024];
    FILE *file;
    size_t wanted;
    size_t read;

    if (frame_loaded == index) return 1;
    if (frame_dir == NULL || frame_width <= 0 || frame_height <= 0) return 0;
    wanted = (size_t)frame_width * (size_t)frame_height;
    if (frame_pixels == NULL) {
        frame_pixels = malloc(wanted * sizeof *frame_pixels);
        if (frame_pixels == NULL) return 0;
        frame_capacity = wanted;
    }
    /* The buffer is sized once, from the frame size `main` settled by reading
     * the whole log before a single block was compared, and `handle` refuses a
     * later line that disagrees with it rather than adopting it. This asks the
     * same question again at the only place it decides anything: how many
     * halfwords `fread` is about to write. Without it, a second `HARNESS
     * FRAMES` line naming a larger frame reads the larger frame into the
     * smaller allocation, and the emulator's output chooses how far past the
     * end of the heap block it writes. */
    if (wanted > frame_capacity) return 0;
    snprintf(path, sizeof path, "%s/frame-%04d.bin", frame_dir, index);
    file = fopen(path, "rb");
    if (file == NULL) return 0;
    read = fread(frame_pixels, sizeof *frame_pixels, wanted, file);
    fclose(file);
    if (read != wanted) return 0;
    frame_loaded = index;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* The transcript                                                            */
/* ------------------------------------------------------------------------- */

static void expect_transcript(const char *line) {
    if (expected_index >= expected_count) {
        report("       the console said more than the host derived: %s\n", line);
        block_ok = 0;
        return;
    }
    if (strcmp(line, expected_lines[expected_index]) != 0) {
        report("       host:    %s\n", expected_lines[expected_index]);
        report("       console: %s\n", line);
        block_ok = 0;
    }
    ++expected_index;
}

/* Every claim in the block just closed, against the frame captured for it.
 *
 * Four assertions a checkpoint, with a printed line per disagreement inside
 * them. Aggregating this way rather than asserting per pixel is deliberate: a
 * checkpoint is the unit a person debugs, one wrong pixel fails its whole
 * block, and the detail lines say which pixel it was. */
static void compare_block(void) {
    int index;
    int loaded;
    int readbacks_agree = 1;
    int frame_agrees = 1;

    if (!block_open) return;
    block_open = 0;

    loaded = load_frame(blocks_compared);
    expect(loaded, "checkpoint %s has a captured frame", block_name);

    expect(declared_probes == claim_count,
           "checkpoint %s printed the %d probes it counted (saw %d)",
           block_name, declared_probes, claim_count);

    for (index = 0; index < claim_count && loaded; ++index) {
        const struct claim *entry = &claims[index];
        uint16_t value;
        int red;
        int green;
        int blue;

        if (entry->x < 0 || entry->y < 0 || entry->x >= frame_width ||
            entry->y >= frame_height) {
            report("       %s/%s is outside the frame at (%d, %d)\n",
                   block_name, entry->label, entry->x, entry->y);
            frame_agrees = 0;
            continue;
        }
        if (!entry->has_readback) {
            report("       %s/%s has no VRAM readback\n", block_name,
                   entry->label);
            readbacks_agree = 0;
            continue;
        }
        if (entry->read_red != entry->red || entry->read_green != entry->green ||
            entry->read_blue != entry->blue) {
            report("       %s/%s claimed %d %d %d, the GPU stored %d %d %d\n",
                   block_name, entry->label, entry->red, entry->green,
                   entry->blue, entry->read_red, entry->read_green,
                   entry->read_blue);
            readbacks_agree = 0;
            continue;
        }
        value = frame_pixels[(size_t)entry->y * (size_t)frame_width +
                             (size_t)entry->x];
        red = value & 0x1F;
        green = (value >> 5) & 0x1F;
        blue = (value >> 10) & 0x1F;
        ++pixels_compared;
        if (red != entry->red || green != entry->green || blue != entry->blue) {
            report("       %s/%s at (%d, %d) claimed %d %d %d, the display "
                   "shows %d %d %d\n",
                   block_name, entry->label, entry->x, entry->y, entry->red,
                   entry->green, entry->blue, red, green, blue);
            frame_agrees = 0;
        }
    }

    expect(readbacks_agree, "checkpoint %s stored every colour it claimed",
           block_name);
    expect(frame_agrees, "checkpoint %s displays every colour it claimed",
           block_name);
    expect(block_ok, "checkpoint %s matches the host's derivation", block_name);
    ++blocks_compared;
    claim_count = 0;
    declared_probes = -1;
    block_ok = 1;
}

static void open_block(const char *line) {
    compare_block();
    ++checkpoints_seen;
    snprintf(block_name, sizeof block_name, "%.*s", (int)sizeof block_name - 1,
             line + 11);
    block_open = 1;
    block_ok = 1;
    claim_count = 0;
    declared_probes = -1;
    expect_transcript(line);
}

static struct claim *find_claim(const char *label) {
    int index;
    for (index = claim_count - 1; index >= 0; --index) {
        if (strcmp(claims[index].label, label) == 0) return &claims[index];
    }
    return NULL;
}

static void handle(const char *line) {
    char label[max_label];
    int x;
    int y;
    int red;
    int green;
    int blue;
    int count;

    if (strncmp(line, "CHECKPOINT ", 11) == 0) {
        open_block(line);
        return;
    }
    if (strncmp(line, "FACT ", 5) == 0) {
        expect_transcript(line);
        return;
    }
    if (sscanf(line, "PROBE %63s %d %d %d %d %d", label, &x, &y, &red, &green,
               &blue) == 6) {
        struct claim *entry;
        if (claim_count >= max_probes_per_block) {
            expect(0, "checkpoint %s stays inside the probe capacity",
                   block_name);
            return;
        }
        if (find_claim(label) != NULL) {
            expect(0, "checkpoint %s names %s once", block_name, label);
        }
        entry = &claims[claim_count++];
        memset(entry, 0, sizeof *entry);
        snprintf(entry->label, sizeof entry->label, "%s", label);
        entry->x = x;
        entry->y = y;
        entry->red = red;
        entry->green = green;
        entry->blue = blue;
        return;
    }
    if (sscanf(line, "READBACK %63s %d %d %d", label, &red, &green, &blue) == 4) {
        struct claim *entry = find_claim(label);
        if (entry == NULL) {
            expect(0, "checkpoint %s reads back only what it claimed (%s)",
                   block_name, label);
            return;
        }
        entry->has_readback = 1;
        entry->read_red = red;
        entry->read_green = green;
        entry->read_blue = blue;
        return;
    }
    if (sscanf(line, "PROBE-COUNT %d", &count) == 1) {
        declared_probes = count;
        return;
    }
    if (sscanf(line, "HARNESS FRAMES %d %dx%d", &count, &x, &y) == 3) {
        frames_reported = count;
        /* The size is not taken from here. `main` settled it by reading the
         * whole log first, precisely because the frame buffer has to be
         * allocated before the first block is compared and this line arrives
         * after it. What is left to do is disagree: a second line naming a
         * different size is the harness contradicting itself, and a probe
         * whose entire job is to refuse disagreement cannot quietly adopt the
         * newer number over a buffer already sized for the older one.
         *
         * Reported only when it happens, so a run of the one line a real
         * harness prints adds no check to the tally. */
        if (x != frame_width || y != frame_height) {
            expect(0, "the harness reports one frame size (%dx%d, then %dx%d)",
                   frame_width, frame_height, x, y);
        }
        return;
    }
    if (strncmp(line, "LAYOUT ", 7) == 0) {
        const char* at = strstr(line, " script ");
        if (at != NULL && sscanf(at, " script %d", &count) == 1) {
            declared_script = count;
        }
        return;
    }
    if (strncmp(line, "SESSION ", 8) == 0) {
        const char* at = strstr(line, " presses ");
        if (at != NULL && sscanf(at, " presses %d", &count) == 1) {
            played_presses = count;
        }
        return;
    }
    if (strncmp(line, "HARNESS RESULT ", 15) == 0) {
        harness_result_seen = 1;
        harness_passed = strncmp(line + 15, "PASS", 4) == 0;
        return;
    }
    if (strncmp(line, "RESULT ", 7) == 0) {
        rom_result_seen = 1;
        rom_passed = strncmp(line + 7, "PASS", 4) == 0;
        return;
    }
}

static int read_expectations(const char *path) {
    FILE *file = fopen(path, "r");
    char line[max_line];
    int in_transcript = 0;

    if (file == NULL) return 0;
    while (fgets(line, sizeof line, file) != NULL) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        if (strcmp(line, "SCRIPT") == 0) continue;
        if (strcmp(line, "TRANSCRIPT") == 0) {
            in_transcript = 1;
            continue;
        }
        if (!in_transcript) {
            if (strncmp(line, "PRESS ", 6) == 0) ++script_presses;
            continue;
        }
        if (expected_count >= max_expected) {
            fclose(file);
            return 0;
        }
        expected_lines[expected_count] = malloc(strlen(line) + 1);
        if (expected_lines[expected_count] == NULL) {
            fclose(file);
            return 0;
        }
        strcpy(expected_lines[expected_count], line);
        ++expected_count;
    }
    fclose(file);
    return 1;
}

int main(int argc, char **argv) {
    FILE *log;
    char line[max_line];

    if (argc != 4) {
        fprintf(stderr,
                "usage: %s <log> <expectations.txt> <frame-dir>\n",
                argc > 0 ? argv[0] : "playstation_turn_probe");
        return 2;
    }
    frame_dir = argv[3];

    if (!read_expectations(argv[2])) {
        fprintf(stderr, "cannot read the expectations at %s\n", argv[2]);
        return 2;
    }

    log = fopen(argv[1], "r");
    if (log == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    /* The frame size has to be known before the first block is compared, and
     * the harness prints it at the end, so the log is read once for the
     * `HARNESS FRAMES` line and once for everything else. It is a few hundred
     * kilobytes and this is a great deal simpler than buffering the blocks. */
    while (fgets(line, sizeof line, log) != NULL) {
        int count;
        int x;
        int y;
        trim(line);
        if (sscanf(line, "HARNESS FRAMES %d %dx%d", &count, &x, &y) == 3) {
            frames_reported = count;
            frame_width = x;
            frame_height = y;
        }
    }
    expect(frame_width > 0 && frame_height > 0,
           "the harness reported a frame size (%dx%d)", frame_width,
           frame_height);
    if (frame_width <= 0 || frame_height <= 0) {
        fclose(log);
        report("RESULT FAIL %d/%d\n", checks - failures, checks);
        return 1;
    }

    rewind(log);
    while (fgets(line, sizeof line, log) != NULL) {
        trim(line);
        handle(line);
    }
    fclose(log);
    compare_block();

    /* Report integrity, before anything is concluded from it. */
    expect(checkpoints_seen > 0, "the console settled somewhere");
    expect(expected_count > 0, "the host derived a transcript");
    expect(script_presses > 0, "the host derived a script (%d presses)",
           script_presses);
    expect(declared_script == script_presses,
           "the executable carries the script the host derived (%d, derived %d)",
           declared_script, script_presses);
    expect(played_presses == script_presses,
           "the executable played every press of it (%d of %d)", played_presses,
           script_presses);
    expect(expected_index == expected_count,
           "every line the host derived was reported (%d of %d)", expected_index,
           expected_count);
    expect(frames_reported == checkpoints_seen,
           "the harness captured one frame per checkpoint (%d frames, %d "
           "checkpoints)",
           frames_reported, checkpoints_seen);
    expect(pixels_compared > 0, "at least one pixel was compared");
    expect(harness_result_seen, "the emulator's harness reached a verdict");
    expect(harness_passed, "the emulator's harness passed");
    expect(rom_result_seen, "the executable reached a verdict");
    expect(rom_passed, "the executable passed");

    report("CHECKPOINTS %d  BLOCKS %d  PIXELS %d\n", checkpoints_seen,
           blocks_compared, pixels_compared);
    report("RESULT %s %d/%d\n", failures == 0 ? "PASS" : "FAIL",
           checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
