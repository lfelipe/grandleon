// SPDX-License-Identifier: MIT
// A tactics board in 3D, on the PlayStation's Geometry Transformation Engine.
//
// This is a *scratch measurement program*, not a renderer this repository
// ships. It answers one question: what does hardware transform cost on the one
// console in this repository that has any, and where does a
// tactics-board-shaped 3D scene stop fitting inside a frame?
//
// It is checked in so the answer can be re-derived rather than quoted. It is
// built by an opt-in target only and is part of no gate. Nothing depends on a
// number it prints; run it when a 3D board is being considered again.
//
// **The `§n` references below are to a companion analysis that is not in this
// repository, and nothing here needs it.** They are kept because they are
// structure rather than citation: the scenes are laid out in that document's
// order, and each one hashes what its section recorded, so the numbering says
// which scene is which and in what order they were argued. A reader who cannot
// open it loses the argument and keeps everything this program is for, because
// every figure it rests on is printed by the run itself rather than quoted from
// anywhere. That is the whole reason it exists.
//
// It is looser than the code around it in exactly two places, both named here
// rather than left to be discovered:
//
//   * the GTE is reached through inline assembly wrapped in macros, because a
//     coprocessor register number has to be a literal inside the instruction
//     and a function per register would be thirty-odd functions saying one
//     thing;
//   * the scene is hand-written rather than loaded from a package, and nothing
//     here links `engine/`. A measurement of the GPU and the GTE should not be
//     able to fail because a content path changed, and the board below is a
//     board *shape*, not authored content.
//
// Everything else is the repository's own: the warning discipline, the report
// format, the two-channel verdict, fixed point throughout. Numbers are only
// worth having if the program that produced them is held to the standard
// everything else is.
//
// ---------------------------------------------------------------------------
// What the GTE is, in the two paragraphs a reader needs
//
// Coprocessor 2 of the R3000A. It holds a 3x3 signed 1.3.12 fixed-point matrix
// (control registers 0..4), a translation vector (5..7), a projection distance
// H in *pixels* (26) and a screen offset (24, 25). `RTPS` transforms one
// vector, `RTPT` three: camera = R*world + TR, then screen x = H*camera.x /
// camera.z + offset.x, and the same for y. Results arrive in a three-deep FIFO
// of packed 16-bit screen coordinates (data registers 12..14) and a four-deep
// FIFO of depths (16..19).
//
// There is no floating point in it and none in this file. That is not a
// limitation, it is the entire reason a 3D board could stay checkable: a
// fixed-point transform is arithmetic a host program can reproduce, and §4 of
// the document is built on that being true. Whether it is true to the last bit
// is measured here rather than assumed. See the PREDICT lines.
//
// ---------------------------------------------------------------------------
// The camera, stated once so §3 of the document can cite it
//
// World space: X right, Y up (elevation), Z away from the viewer. The board
// lies in the XZ plane with its centre at the origin, so a cell's world
// position is entirely determined by its column, its row and its terrain's
// elevation. Those are the same three things every other client already has.
//
// The view rotation is a single pitch. There is no yaw and no roll, and that is
// a decision rather than a simplification: with row 0 of the matrix equal to
// (1, 0, 0) a world-X offset is a screen-X offset exactly, which is what lets a
// screen-facing billboard be an ordinary world-space quad in the XY plane (see
// `billboard_corners`), and what keeps column order a stable left-to-right
// order on screen. For a pitch of phi off the board plane:
//
//     row 0 = (      1,        0,        0 )
//     row 1 = (      0,  -cos phi, -sin phi )      screen down
//     row 2 = (      0,  -sin phi,  cos phi )      depth
//
// Read the two interesting rows out loud. Row 1: a taller cell (larger world Y)
// gets a *smaller* screen Y, so elevation draws upward, which is what every
// client already does with its lift; and a further cell (larger world Z) also
// draws upward, which is what makes a board a board. Row 2: a taller cell is
// *nearer*, which is what makes a mountain occlude the cell behind it with no
// depth buffer anywhere.
//
// The translation is the camera-space position of the world origin, chosen
// directly rather than derived from an eye position: TRZ is the distance from
// the eye to the board centre, TRY frames the board vertically, TRX is zero.
//
// One hardware constraint binds the choice and is checked rather than assumed:
// the GTE's reciprocal unit overflows when a depth is not greater than H/2 and
// says so in bit 17 of the FLAG register. Every configuration here is required
// to clear it.

#include <cstdint>

#include "psx_art.h"
#include "psx_gpu.h"
#include "psx_runtime.h"
// The art library's terrain registry: the elevation each terrain kind stands
// at. The authority every client reads, because nothing in a package carries an
// elevation. Elevation is drawing.
#include "themes.h"

namespace psx = grandleon::playstation;
namespace art = grandleon::playstation::art;
namespace gpu = grandleon::playstation::gpu;

namespace {

using psx::Line;

// ---------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------

int checks = 0;
int failures = 0;

void expect(bool condition, const char* name) {
    ++checks;
    if (!condition) ++failures;
    Line().text("CHECK ").text(name).text(condition ? " PASS" : " FAIL").flush();
}

[[noreturn]] void give_up(const char* why) {
    Line().text("GIVE UP ").text(why).flush();
    Line().text("RESULT FAIL 0/1").flush();
    // Under -testmode a non-zero write to the control port ends the emulator
    // process, so a failure is a failed run rather than a hang. Nugget's own
    // abort() cannot be used: it pauses the machine first and waits for a
    // debugger that is not there.
    *reinterpret_cast<volatile std::int16_t*>(0x1f802082) = 1;
    for (;;) {
    }
}

// ---------------------------------------------------------------------------
// The clock, and the one trap in it
//
// Root counter 2 is sixteen bits wide and `psx::clock_ticks()` extends it by
// noticing that it went backwards. At one eighth of a 33.8688 MHz clock it
// wraps every 65,536 ticks, which is 15,480 microseconds. A timed region that
// runs longer than fifteen milliseconds without a single call silently loses a
// whole wrap and reports a time far too small.
//
// Every loop below that could run that long therefore calls `tick()` once per
// row. The call is an uncached halfword read and a compare; against a row of
// textured quads it is nothing, and against a lost wrap it is the difference
// between a measurement and a fiction.
// ---------------------------------------------------------------------------

std::uint32_t tick() {
    return psx::clock_ticks();
}

[[nodiscard]] std::uint32_t microseconds(std::uint32_t ticks) noexcept {
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(ticks) * 1000ull) /
        psx::ticks_per_1000_microseconds
    );
}

// ---------------------------------------------------------------------------
// Coprocessor 2
//
// The register number has to be a literal inside the instruction, so these are
// macros. `c2 <imm25>` is the assembler's spelling of a GTE command; the two
// used here are RTPS (transform one vector, 0x0180001) and RTPT (transform
// three, 0x0280030). The load-delay slots after `mtc2`, `cfc2` and `mfc2` are
// the assembler's business and it fills them, which was checked by
// disassembling this object rather than assumed.
// ---------------------------------------------------------------------------

#define GTE_SET_CONTROL(n, value) \
    __asm__ volatile("ctc2 %0, $" #n ::"r"(value))
#define GTE_GET_CONTROL(n, out) __asm__ volatile("cfc2 %0, $" #n : "=r"(out))
#define GTE_SET_DATA(n, value) __asm__ volatile("mtc2 %0, $" #n ::"r"(value))
#define GTE_GET_DATA(n, out) __asm__ volatile("mfc2 %0, $" #n : "=r"(out))
#define GTE_RTPT() __asm__ volatile("c2 0x0280030")

// Bit 17 of FLAG: the reciprocal unit overflowed, which happens when a depth is
// not greater than H/2. The screen coordinate it produced then means nothing,
// so this bit is the difference between a projection and a guess.
constexpr std::uint32_t gte_flag_divide_overflow = 1u << 17;

// A world-space vertex. Sixteen bits an axis is the GTE's input width, not a
// budget chosen here: VXY0 packs two of them into one register.
struct Vertex final {
    std::int16_t x;
    std::int16_t y;
    std::int16_t z;
};

// A projected vertex, as the machine hands it back.
struct Projected final {
    std::int16_t x;
    std::int16_t y;
    std::uint16_t depth;
};

[[nodiscard]] std::int32_t pack_xy(std::int32_t x, std::int32_t y) noexcept {
    return static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(
             static_cast<std::int16_t>(y))) << 16) |
        static_cast<std::uint32_t>(
            static_cast<std::uint16_t>(static_cast<std::int16_t>(x)))
    );
}

[[nodiscard]] Projected unpack(std::int32_t sxy, std::int32_t sz) noexcept {
    return Projected{
        static_cast<std::int16_t>(static_cast<std::uint32_t>(sxy) & 0xFFFFu),
        static_cast<std::int16_t>(static_cast<std::uint32_t>(sxy) >> 16),
        static_cast<std::uint16_t>(static_cast<std::uint32_t>(sz) & 0xFFFFu)
    };
}

// Three vertices, one command. This is the shape a real renderer uses and
// therefore the shape the per-vertex cost below is measured in: RTPT amortises
// the setup across three transforms, and measuring RTPS instead would flatter
// or slander the hardware depending on which way one rounded.
void transform3(const Vertex& a, const Vertex& b, const Vertex& c,
                Projected out[3]) {
    GTE_SET_DATA(0, pack_xy(a.x, a.y));
    GTE_SET_DATA(1, static_cast<std::int32_t>(a.z));
    GTE_SET_DATA(2, pack_xy(b.x, b.y));
    GTE_SET_DATA(3, static_cast<std::int32_t>(b.z));
    GTE_SET_DATA(4, pack_xy(c.x, c.y));
    GTE_SET_DATA(5, static_cast<std::int32_t>(c.z));
    GTE_RTPT();
    std::int32_t sxy0 = 0;
    std::int32_t sxy1 = 0;
    std::int32_t sxy2 = 0;
    std::int32_t sz1 = 0;
    std::int32_t sz2 = 0;
    std::int32_t sz3 = 0;
    GTE_GET_DATA(12, sxy0);
    GTE_GET_DATA(13, sxy1);
    GTE_GET_DATA(14, sxy2);
    GTE_GET_DATA(17, sz1);
    GTE_GET_DATA(18, sz2);
    GTE_GET_DATA(19, sz3);
    out[0] = unpack(sxy0, sz1);
    out[1] = unpack(sxy1, sz2);
    out[2] = unpack(sxy2, sz3);
}

// ---------------------------------------------------------------------------
// Trigonometry without a trigonometry library
//
// Sine at one-degree steps in 1.12 fixed point, for the quadrant. Written out
// because there is no libm on this machine, and because a table is what a
// shipped renderer would carry anyway: a camera pitch is a setting, not a
// continuous quantity, and 91 halfwords is nothing beside 137 KiB of art.
// ---------------------------------------------------------------------------

constexpr std::int32_t fixed_one = 4096;

constexpr std::int16_t sine_table[91] = {
        0,    71,   143,   214,   286,   357,   428,   499,   570,   641,
      711,   782,   852,   921,   991,  1060,  1129,  1198,  1266,  1334,
     1401,  1468,  1534,  1600,  1666,  1731,  1796,  1860,  1923,  1986,
     2048,  2110,  2171,  2231,  2290,  2349,  2408,  2465,  2522,  2578,
     2633,  2687,  2741,  2793,  2845,  2896,  2946,  2996,  3044,  3091,
     3138,  3183,  3228,  3271,  3314,  3355,  3396,  3435,  3474,  3511,
     3547,  3582,  3617,  3650,  3681,  3712,  3742,  3770,  3798,  3824,
     3849,  3873,  3896,  3917,  3937,  3956,  3974,  3991,  4006,  4021,
     4034,  4046,  4056,  4065,  4074,  4080,  4086,  4090,  4094,  4095,
     4096
};

[[nodiscard]] std::int32_t sine_of(int degrees) noexcept {
    if (degrees < 0) degrees = 0;
    if (degrees > 90) degrees = 90;
    return sine_table[degrees];
}

[[nodiscard]] std::int32_t cosine_of(int degrees) noexcept {
    return sine_of(90 - degrees);
}

// The whole circle, from the same quadrant table. The *camera* never needs it:
// §3.1 forbids yaw and a pitch is between zero and ninety. A unit that turns to
// face the way it is walking does need it, and that turn is the one thing a
// mesh can do that a billboard structurally cannot. It is the model that
// rotates here, never the view.
[[nodiscard]] std::int32_t sine_full(int degrees) noexcept {
    int d = degrees % 360;
    if (d < 0) d += 360;
    if (d <= 90) return sine_of(d);
    if (d <= 180) return sine_of(180 - d);
    if (d <= 270) return -sine_of(d - 180);
    return -sine_of(360 - d);
}

[[nodiscard]] std::int32_t cosine_full(int degrees) noexcept {
    return sine_full(degrees + 90);
}

// ---------------------------------------------------------------------------
// The board
//
// Twelve by nine, the largest shape `games/tarnholt` authors, written as a picture
// so a reader can see what is drawn. The letters name art-library terrain
// kinds; the elevation of each comes out of the generated registry rather than
// out of this file, so a mountain is two steps up here because
// `grandleon_terrain_elevation` says it is.
// ---------------------------------------------------------------------------

constexpr int board_columns = 12;
constexpr int board_rows = 9;

constexpr const char* board_picture[board_rows] = {
    "mmhgggggghmm",
    "mhgggrgggghm",
    "hggrrrggghhm",
    "gggrgggggghm",
    "ggrrgwwggggh",
    "ggrggwwwgggg",
    "hggggwwggfgg",
    "hhgggwgggffg",
    "mhhggwggfffh"
};

// Letter to art-library terrain kind. The registry's order is water, road,
// forest, mountain, sand, snow, swamp, hills, ruins, grass, and this table is
// the only place in this file that knows it.
[[nodiscard]] int kind_of_letter(char letter) noexcept {
    switch (letter) {
        case 'w': return 0;
        case 'r': return 1;
        case 'f': return 2;
        case 'm': return 3;
        case 'h': return 7;
        default: return 9;
    }
}

[[nodiscard]] int kind_at(int column, int row) noexcept {
    if (column < 0) column = 0;
    if (column >= board_columns) column = board_columns - 1;
    if (row < 0) row = 0;
    if (row >= board_rows) row = board_rows - 1;
    return kind_of_letter(board_picture[row][column]);
}

// The elevation of a cell in steps, from the generated registry.
[[nodiscard]] int steps_at(int column, int row) noexcept {
    const int kind = kind_at(column, row);
    if (kind < 0 || kind >= grandleon_terrain_kind_count) return 0;
    return static_cast<int>(grandleon_terrain_elevation[kind]);
}

// The height of a *grid vertex*, which is not the height of a cell: a vertex is
// a corner shared by up to four cells, and a heightfield's corner takes the
// highest of them so that a mountain's edge is a cliff rather than a ramp into
// the sea. This is the one geometric decision in the scene, and it is why the
// skirts exist.
[[nodiscard]] int vertex_steps(int column_right, int row_below) noexcept {
    int highest = 0;
    for (int dz = -1; dz <= 0; ++dz) {
        for (int dx = -1; dx <= 0; ++dx) {
            const int column = column_right + dx;
            const int row = row_below + dz;
            if (column < 0 || column >= board_columns) continue;
            if (row < 0 || row >= board_rows) continue;
            const int steps = steps_at(column, row);
            if (steps > highest) highest = steps;
        }
    }
    return highest;
}

// The height of the *near* edge of a grid line: the tallest of the two cells in
// front of it. A skirt spans the gap between this and `vertex_steps`.
[[nodiscard]] int front_steps(int column_right, int row_below) noexcept {
    int highest = 0;
    for (int dx = -1; dx <= 0; ++dx) {
        const int column = column_right + dx;
        if (column < 0 || column >= board_columns) continue;
        if (row_below < 0 || row_below >= board_rows) continue;
        const int steps = steps_at(column, row_below);
        if (steps > highest) highest = steps;
    }
    return highest;
}

// ---------------------------------------------------------------------------
// The scene, in world units
// ---------------------------------------------------------------------------

// One board cell, in world units.
constexpr int tile_world = 64;
// One elevation step. A quarter of a tile, which is exactly what
// `view::elevation_step_for` already draws in two dimensions. Here it is not
// clamped, because in three dimensions elevation is geometry and
// `view::max_lift_for` is a two-dimensional occlusion rule.
constexpr int step_world = tile_world / 4;
// How tall a unit stands. One tile: the art is a 32x32 cell filling its square,
// so a billboard one tile wide and one tile tall is the picture the flat
// renderers draw, stood up.
constexpr int unit_world = 64;

// The camera. A pitch of sixty degrees off the board plane, the board centre
// 744 units in front of the eye, a focal length of 300 pixels, and a vertical
// framing offset of -40 camera units.
//
// The distance is not free and the arithmetic is worth having in the file. A
// tile is 64 world units and the near edge of the board sits at depth 744 -
// 288*cos60 = 600, so a near tile is drawn 300*64/600 = 32 pixels across. That
// is the art's own cell size, and therefore the framing at which a PlayStation
// is not throwing texel columns away. The board is 768 units wide and 384
// pixels of that lands on a 320-pixel display, so the board is windowed exactly
// the way the shipped two-dimensional renderer windows it. Fitting more board
// on screen and keeping 32 pixels a tile are the same trade in three dimensions
// as in two; §3 of the document is mostly about that sentence.
constexpr int camera_pitch_degrees = 60;
constexpr int camera_distance = 744;
constexpr int camera_focal = 300;
constexpr int camera_vertical = -40;

// Half the board, in world units. The board is centred on the origin, so these
// two numbers are its extent and the thing a pan is clamped against.
constexpr std::int32_t board_half_width = board_columns * tile_world / 2;
constexpr std::int32_t board_half_depth = board_rows * tile_world / 2;

// ---------------------------------------------------------------------------
// The pan, as arithmetic
//
// The camera looks at a point on the board plane, its *focus*. Panning moves
// that point. §3.1 forbids yaw and says nothing against translation, so this is
// the whole of it: camera = R*(world - focus) + T0, which rearranges to
// camera = R*world + (T0 - R*focus). With focus = (fx, 0, fz) and R's rows as
// §3.1 states them, R*focus is (fx, -sin(phi)*fz, cos(phi)*fz), so
//
//     TRX = -fx
//     TRY = vertical + sin(phi)*fz
//     TRZ = distance  - cos(phi)*fz
//
// and *nothing else about the camera changes*: not the matrix, not the focal
// length, not the screen offset. A pan is three register writes.
//
// The eye moves the way it should, which is worth checking rather than
// asserting because the signs above are easy to get backwards. Inverting the
// projection the way §3.3 does gives eye = -R*TR, and substituting:
//
//     eye = ( fx,  cos(phi)*vertical + sin(phi)*distance,
//                  sin(phi)*vertical - cos(phi)*distance + fz )
//
// The same height at every focus, translated by exactly (fx, 0, fz) along the
// board plane. At focus (0,0) that is (0, 624, -407), which is the eye position
// §3.3 already records, so this arithmetic is the one already in the document
// with a term added rather than a new one.
//
// TR is written to the coprocessor as whole camera units, so the two
// multiplications above are integer divisions by 4096 and the portable model
// below has to do the same division the same way or the two stop agreeing.
// That is why `translation_for` exists at all rather than the expression being
// written out twice.
// ---------------------------------------------------------------------------

struct Focus final {
    std::int32_t x;
    std::int32_t z;
};

struct Translation final {
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
};

[[nodiscard]] Translation translation_for(int pitch_degrees, int distance,
                                          int vertical, Focus focus) noexcept {
    const std::int32_t sine = sine_of(pitch_degrees);
    const std::int32_t cosine = cosine_of(pitch_degrees);
    return Translation{
        -focus.x,
        static_cast<std::int32_t>(vertical) + (sine * focus.z) / fixed_one,
        static_cast<std::int32_t>(distance) - (cosine * focus.z) / fixed_one,
    };
}

void load_camera(int pitch_degrees, int distance, int focal, int vertical,
                 Focus focus) {
    const std::int32_t sine = sine_of(pitch_degrees);
    const std::int32_t cosine = cosine_of(pitch_degrees);
    const Translation translation =
        translation_for(pitch_degrees, distance, vertical, focus);

    GTE_SET_CONTROL(0, pack_xy(fixed_one, 0));   // R11 R12
    GTE_SET_CONTROL(1, pack_xy(0, 0));           // R13 R21
    GTE_SET_CONTROL(2, pack_xy(-cosine, -sine)); // R22 R23
    GTE_SET_CONTROL(3, pack_xy(0, -sine));       // R31 R32
    GTE_SET_CONTROL(4, cosine);                  // R33

    GTE_SET_CONTROL(5, translation.x);           // TRX
    GTE_SET_CONTROL(6, translation.y);           // TRY
    GTE_SET_CONTROL(7, translation.z);           // TRZ

    // The screen offset is 1.15.16, so the centre of a 320x240 display is
    // 160<<16 and 120<<16.
    GTE_SET_CONTROL(24, static_cast<std::int32_t>(gpu::screen_width / 2) << 16);
    GTE_SET_CONTROL(25, static_cast<std::int32_t>(gpu::screen_height / 2) << 16);
    GTE_SET_CONTROL(26, focal);                  // H
    GTE_SET_CONTROL(31, 0);                      // FLAG
}

// ---------------------------------------------------------------------------
// The divider, reimplemented
//
// The GTE does not divide. It looks a seed up in a 257-entry table and runs two
// Newton-Raphson steps, and the result is *close to* H*0x20000/SZ3 without
// being it. Measured here: over seven cameras and 910 vertices, an honest
// integer division disagrees with the coprocessor on two of them, by one pixel.
//
// Two pixels in nine hundred is nothing to look at and everything to a check
// that asserts a frame. So the algorithm is written out rather than
// approximated, and the PREDICT lines report whether that closes the gap. It
// does, and §4 of the document is that sentence. The table is generated at
// compile time from the definition rather than transcribed, so there is no
// 257-entry constant here for anybody to mistype.
// ---------------------------------------------------------------------------

struct UnrTable final {
    std::uint8_t entry[257];
};

[[nodiscard]] constexpr UnrTable make_unr_table() noexcept {
    UnrTable table{};
    for (int i = 0; i <= 256; ++i) {
        const std::int32_t x = 0x40000 / (i + 0x100);
        std::int32_t value = (x + 1) / 2 - 0x101;
        if (value < 0) value = 0;
        if (value > 0xFF) value = 0xFF;
        table.entry[i] = static_cast<std::uint8_t>(value);
    }
    return table;
}

constexpr UnrTable unr_table = make_unr_table();

[[nodiscard]] int leading_zeros16(std::uint32_t value) noexcept {
    int zeros = 0;
    for (int bit = 15; bit >= 0; --bit) {
        if ((value & (1u << bit)) != 0) break;
        ++zeros;
    }
    return zeros;
}

// (H*0x20000/SZ3 + 1) / 2, the way the hardware gets there. Note that the
// routine produces the *halved* quantity the projection multiplies by, not the
// quotient before it. That factor of two is easy to get wrong, and the PREDICT
// line catches it immediately by reporting a hundred-pixel deviation instead of
// a one-pixel one. Returns 0x1FFFF and sets `overflowed` when SZ3*2 <= H, which
// is the divider's documented floor.
[[nodiscard]] std::uint32_t gte_divide(std::uint32_t h, std::uint32_t sz3,
                                       bool& overflowed) noexcept {
    if (sz3 * 2 <= h) {
        overflowed = true;
        return 0x1FFFFu;
    }
    overflowed = false;
    const int z = leading_zeros16(sz3);
    std::uint64_t n = static_cast<std::uint64_t>(h) << z;
    std::uint64_t d = static_cast<std::uint64_t>(sz3) << z;
    const std::uint32_t index =
        static_cast<std::uint32_t>((d - 0x7FC0u) >> 7);
    const std::uint64_t u =
        static_cast<std::uint64_t>(unr_table.entry[index > 256 ? 256 : index]) +
        0x101u;
    d = (0x2000080ull - (d * u)) >> 8;
    d = (0x0000080ull + (d * u)) >> 8;
    n = ((n * d) + 0x8000ull) >> 16;
    return n > 0x1FFFFull ? 0x1FFFFu : static_cast<std::uint32_t>(n);
}

// The same projection in portable integer arithmetic, with no coprocessor
// anywhere near it.
//
// This exists for one reason and it is the reason §4 of the document turns on:
// if a host program can reproduce what the GTE produced, a 3D frame is
// predictable on the host and this repository's pixel-asserting discipline
// survives the move to three dimensions. If it cannot, that discipline has to
// change shape. The PREDICT line reports which.
[[nodiscard]] Projected project_portably(
    const Vertex& v, int pitch_degrees, int distance, int focal, int vertical,
    Focus focus, bool hardware_divider
) noexcept {
    const std::int32_t sine = sine_of(pitch_degrees);
    const std::int32_t cosine = cosine_of(pitch_degrees);
    const Translation translation =
        translation_for(pitch_degrees, distance, vertical, focus);

    // camera = R*world + TR, with the matrix in 1.3.12 and the GTE's own sf=1
    // shift: MAC = (TR*0x1000 + R*V) >> 12.
    const std::int32_t cx =
        ((translation.x * fixed_one) +
         fixed_one * static_cast<std::int32_t>(v.x)) >> 12;
    const std::int32_t cy =
        ((translation.y * fixed_one) +
         (-cosine) * static_cast<std::int32_t>(v.y) +
         (-sine) * static_cast<std::int32_t>(v.z)) >> 12;
    const std::int32_t cz =
        ((translation.z * fixed_one) +
         (-sine) * static_cast<std::int32_t>(v.y) +
         cosine * static_cast<std::int32_t>(v.z)) >> 12;

    std::int32_t depth = cz;
    if (depth < 0) depth = 0;
    if (depth > 0xFFFF) depth = 0xFFFF;

    // The GTE's screen step is MAC0 = ((H*0x20000/SZ3) + 1)/2 * IR + OFX, and
    // MAC0 >> 16 is the pixel. Written the same way here, integer for integer,
    // because the question is whether the same way gives the same answer.
    std::int32_t half = 0x10000;
    if (depth > 0) {
        if (hardware_divider) {
            bool overflowed = false;
            half = static_cast<std::int32_t>(gte_divide(
                static_cast<std::uint32_t>(focal),
                static_cast<std::uint32_t>(depth), overflowed
            ));
        } else {
            const std::int32_t quotient =
                (static_cast<std::int32_t>(focal) << 17) / depth;
            const std::int32_t reciprocal =
                quotient > 0x1FFFF ? 0x1FFFF : quotient;
            half = (reciprocal + 1) >> 1;
        }
    }
    const std::int32_t sx =
        ((half * cx) +
         (static_cast<std::int32_t>(gpu::screen_width / 2) << 16)) >> 16;
    const std::int32_t sy =
        ((half * cy) +
         (static_cast<std::int32_t>(gpu::screen_height / 2) << 16)) >> 16;
    return Projected{static_cast<std::int16_t>(sx),
                     static_cast<std::int16_t>(sy),
                     static_cast<std::uint16_t>(depth)};
}

// ---------------------------------------------------------------------------
// Drawing
//
// A textured four-point polygon, raw: GP0(0x2D). The vertex order the hardware
// wants makes the two triangles (0,1,2) and (1,2,3), so the corners go in as
// top-left, top-right, bottom-left, bottom-right: a Z, not a ring. Getting
// that backwards draws an hourglass, which is worth writing down once here
// rather than discovering in a screenshot.
//
// Unlike the textured *rectangle* the shipped renderer uses, a polygon carries
// its texture page in its own packet, so there is no separate mode command per
// primitive and the submission cost measured below is the packet and nothing
// else.
// ---------------------------------------------------------------------------

volatile std::uint32_t* const gp0 =
    reinterpret_cast<volatile std::uint32_t*>(0xbf801810);
volatile std::uint32_t* const gp1 =
    reinterpret_cast<volatile std::uint32_t*>(0xbf801814);

constexpr std::uint32_t status_ready_for_command = 1u << 26;
constexpr std::uint32_t status_ready_to_send = 1u << 27;

void wait_for(std::uint32_t bit) {
    while ((*gp1 & bit) == 0) {
    }
}

[[nodiscard]] std::uint32_t texpage_of_cell(int cell) noexcept {
    const int page = gpu::first_texture_page + cell / gpu::cells_per_page;
    return static_cast<std::uint32_t>(page) |
           (static_cast<std::uint32_t>(gpu::texture_page_y / gpu::page_lines)
            << 4) |
           (1u << 10);
}

[[nodiscard]] std::uint32_t clut_field_of(int clut) noexcept {
    return (static_cast<std::uint32_t>(gpu::clut_y_of(clut)) << 6) |
           static_cast<std::uint32_t>(gpu::clut_x_of(clut) / gpu::clut_entries);
}

[[nodiscard]] int cell_u(int cell) noexcept {
    return (cell % gpu::cells_per_page) % gpu::cells_per_page_row *
           gpu::cell_texels;
}
[[nodiscard]] int cell_v(int cell) noexcept {
    return (cell % gpu::cells_per_page) / gpu::cells_per_page_row *
           gpu::cell_texels;
}

// The nine words of a textured quad. `corner` is in Z order.
void draw_textured_quad(const Projected corner[4], int cell, int clut) {
    if (cell < 0 || clut < 0) return;
    const std::uint32_t u = static_cast<std::uint32_t>(cell_u(cell));
    const std::uint32_t v = static_cast<std::uint32_t>(cell_v(cell));
    const std::uint32_t far = static_cast<std::uint32_t>(gpu::cell_texels - 1);
    const std::uint32_t clut_field = clut_field_of(clut);
    const std::uint32_t page = texpage_of_cell(cell);

    wait_for(status_ready_for_command);
    *gp0 = (0x2Du << 24) | 0x808080u;
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[0].x, corner[0].y));
    *gp0 = (clut_field << 16) | (v << 8) | u;
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[1].x, corner[1].y));
    *gp0 = (page << 16) | (v << 8) | (u + far);
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[2].x, corner[2].y));
    *gp0 = ((v + far) << 8) | u;
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[3].x, corner[3].y));
    *gp0 = ((v + far) << 8) | (u + far);
}

// A flat-shaded quad, GP0(0x28), for the phase that asks what the texture unit
// costs by taking it away. From §11 down it also draws every face of a mesh
// unit. Five words against a textured quad's nine.
void draw_flat_quad(const Projected corner[4], std::uint32_t colour) {
    wait_for(status_ready_for_command);
    *gp0 = (0x28u << 24) | (colour & 0x00FFFFFFu);
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[0].x, corner[0].y));
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[1].x, corner[1].y));
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[2].x, corner[2].y));
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[3].x, corner[3].y));
}

// The same quad, semi-transparent: GP0(0x2A). The blend mode is the ABR field
// of the GPU's draw-mode register, and nothing here sets it, deliberately.
// Every textured polygon above carries a texture-page word whose ABR bits are
// zero, so the mode is already B/2 + F/2 and stays there. Stacking n of these
// in the same colour therefore lands 1 - 2^-n of the way toward it, which is
// how §12's fog grades without a second blend mode or a second colour.
void draw_semi_quad(const Projected corner[4], std::uint32_t colour) {
    wait_for(status_ready_for_command);
    *gp0 = (0x2Au << 24) | (colour & 0x00FFFFFFu);
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[0].x, corner[0].y));
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[1].x, corner[1].y));
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[2].x, corner[2].y));
    *gp0 = static_cast<std::uint32_t>(pack_xy(corner[3].x, corner[3].y));
}

// ---------------------------------------------------------------------------
// VRAM residency: the arrangement the shipped renderer uses, unchanged, because
// the question is what 3D costs on top of what is already paid.
// ---------------------------------------------------------------------------

constexpr int theme = 0;

int terrain_cell_of[art::terrain_kind_count][art::variant_count];
int terrain_clut_of[art::terrain_kind_count];
int character_cell_of[art::archetype_count][2];
int character_clut_of[art::archetype_count][2];

int cells_used = 0;
int cluts_used = 0;

[[nodiscard]] int claim_cell(const unsigned short* texels) {
    if (cells_used >= gpu::cell_capacity) return -1;
    const int cell = cells_used++;
    gpu::upload(gpu::cell_texture_x(cell), gpu::cell_texture_y(cell),
                art::halfwords_per_row, art::cell_size, texels);
    return cell;
}

[[nodiscard]] int claim_clut(const unsigned short* entries) {
    if (cluts_used >= gpu::clut_capacity) return -1;
    const int clut = cluts_used++;
    gpu::upload(gpu::clut_x_of(clut), gpu::clut_y_of(clut), art::clut_size, 1,
                entries);
    return clut;
}

// The console's deterministic interior-variant choice, unchanged from the
// shipped renderer and from both other consoles.
[[nodiscard]] constexpr int terrain_variant(int x, int y) noexcept {
    return (x + y * 3) % art::variant_count;
}

void upload_art() {
    for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
        terrain_clut_of[kind] = claim_clut(art::terrain(theme, kind, 0).clut);
        for (int variant = 0; variant < art::variant_count; ++variant) {
            terrain_cell_of[kind][variant] =
                claim_cell(art::terrain(theme, kind, variant).texels);
        }
    }
    for (int archetype = 0; archetype < art::archetype_count; ++archetype) {
        for (int colour = 0; colour < 2; ++colour) {
            const art::Asset asset = art::character(archetype, colour);
            character_clut_of[archetype][colour] = claim_clut(asset.clut);
            character_cell_of[archetype][colour] = claim_cell(asset.texels);
        }
    }
}

// ---------------------------------------------------------------------------
// The scene, transformed
//
// The grid is transformed once into `grid`, and every quad then indexes it.
// That is the whole point of a heightfield: a vertex shared by four cells is
// projected once, so transform cost is per *vertex* and packet cost is per
// *primitive*, and the two scale differently. A renderer that transformed per
// quad would pay four times over for the same picture, and these numbers would
// be a measurement of that mistake.
//
// `skirt_grid` is the same grid projected again at the height of the cells in
// *front* of each line, which is where a cliff face's lower edge is. Two grids,
// one pass, no special case.
// ---------------------------------------------------------------------------

// Enough for the deepest subdivision the ladder reaches.
constexpr int max_factor = 8;
constexpr int max_across = board_columns * max_factor + 1;
constexpr int max_down = board_rows * max_factor + 1;
Projected grid[max_down][max_across];
Projected skirt_grid[max_down][max_across];

// Where a grid vertex sits in the world, for a board subdivided `factor` times
// in each axis. Factor 1 is the board itself; a larger factor subdivides the
// same ground into more, smaller quads, which is the ladder's axis: triangle
// count rises and filled area does not.
[[nodiscard]] Vertex grid_vertex(int gx, int gz, int factor,
                                 bool front_height) noexcept {
    const int columns = board_columns * factor;
    const int rows = board_rows * factor;
    const std::int32_t half_width = board_columns * tile_world / 2;
    const std::int32_t half_depth = board_rows * tile_world / 2;
    const std::int32_t x =
        -half_width +
        static_cast<std::int32_t>(gx) * (board_columns * tile_world) / columns;
    const std::int32_t z =
        -half_depth +
        static_cast<std::int32_t>(gz) * (board_rows * tile_world) / rows;
    const int column_right = (gx + factor - 1) / factor;
    const int row_below = (gz + factor - 1) / factor;
    const int steps = front_height ? front_steps(column_right, row_below)
                                   : vertex_steps(column_right, row_below);
    return Vertex{static_cast<std::int16_t>(x),
                  static_cast<std::int16_t>(steps * step_world),
                  static_cast<std::int16_t>(z)};
}

// Transforms both grids. Returns the number of vertices put through the GTE.
int transform_grid(int factor) {
    const int across = board_columns * factor + 1;
    const int down = board_rows * factor + 1;
    int transformed = 0;
    for (int gz = 0; gz < down; ++gz) {
        for (int gx = 0; gx + 2 < across; gx += 3) {
            Projected out[3];
            transform3(grid_vertex(gx, gz, factor, false),
                       grid_vertex(gx + 1, gz, factor, false),
                       grid_vertex(gx + 2, gz, factor, false), out);
            grid[gz][gx] = out[0];
            grid[gz][gx + 1] = out[1];
            grid[gz][gx + 2] = out[2];
            transform3(grid_vertex(gx, gz, factor, true),
                       grid_vertex(gx + 1, gz, factor, true),
                       grid_vertex(gx + 2, gz, factor, true), out);
            skirt_grid[gz][gx] = out[0];
            skirt_grid[gz][gx + 1] = out[1];
            skirt_grid[gz][gx + 2] = out[2];
            transformed += 6;
        }
        for (int gx = across - across % 3; gx < across; ++gx) {
            Projected out[3];
            const Vertex top = grid_vertex(gx, gz, factor, false);
            const Vertex bottom = grid_vertex(gx, gz, factor, true);
            transform3(top, bottom, top, out);
            grid[gz][gx] = out[0];
            skirt_grid[gz][gx] = out[1];
            transformed += 3;
        }
        (void)tick();
    }
    return transformed;
}

// ---------------------------------------------------------------------------
// The units
// ---------------------------------------------------------------------------

struct Unit final {
    std::int8_t column;
    std::int8_t row;
    std::int8_t archetype;
    std::int8_t colour;
};

// Sixteen units at battle positions: eight a side, across the river the board
// picture draws.
constexpr int unit_count = 16;
constexpr Unit units[unit_count] = {
    {2, 6, 0, 0}, {3, 6, 1, 0}, {4, 7, 2, 0}, {2, 7, 3, 0},
    {3, 8, 4, 0}, {5, 7, 5, 0}, {1, 6, 6, 0}, {4, 6, 7, 0},
    {8, 1, 0, 1}, {9, 1, 1, 1}, {7, 2, 2, 1}, {9, 2, 3, 1},
    {8, 0, 4, 1}, {6, 1, 5, 1}, {10, 2, 6, 1}, {7, 1, 7, 1},
};

// A billboard's four world corners: a quad in the world XY plane at the cell's
// Z. Because the view matrix has no yaw, an XY-plane quad is screen-facing
// exactly. A unit never has to be turned toward the camera, and its whole cost
// is four vertices through the transform the ground already uses. This is the
// entirety of "the existing sprites live as billboards", and it is six lines.
void billboard_corners(const Unit& unit, Vertex out[4]) {
    const std::int32_t half_width = board_columns * tile_world / 2;
    const std::int32_t half_depth = board_rows * tile_world / 2;
    const std::int32_t x =
        -half_width + static_cast<std::int32_t>(unit.column) * tile_world;
    const std::int32_t z =
        -half_depth + static_cast<std::int32_t>(unit.row) * tile_world +
        tile_world / 2;
    const std::int32_t ground =
        static_cast<std::int32_t>(steps_at(unit.column, unit.row)) * step_world;
    out[0] = Vertex{static_cast<std::int16_t>(x),
                    static_cast<std::int16_t>(ground + unit_world),
                    static_cast<std::int16_t>(z)};
    out[1] = Vertex{static_cast<std::int16_t>(x + unit_world),
                    static_cast<std::int16_t>(ground + unit_world),
                    static_cast<std::int16_t>(z)};
    out[2] = Vertex{static_cast<std::int16_t>(x),
                    static_cast<std::int16_t>(ground),
                    static_cast<std::int16_t>(z)};
    out[3] = Vertex{static_cast<std::int16_t>(x + unit_world),
                    static_cast<std::int16_t>(ground),
                    static_cast<std::int16_t>(z)};
}

// The same billboard, pre-stretched so that it lands square on the screen.
//
// A world-vertical quad is foreshortened by cos(pitch). That is the geometry,
// not a defect. At a steep pitch a 32x32 cell is drawn into a wide, short
// rectangle and loses texel rows. Dividing the world height by cos(pitch)
// cancels exactly that, and the sprite comes out as many pixels tall as it is
// wide. It is the difference between a billboard that stands in the world and a
// billboard that faces the screen, and it costs one multiply per unit.
void billboard_corners_upright(const Unit& unit, int pitch_degrees,
                               Vertex out[4]) {
    billboard_corners(unit, out);
    const std::int32_t cosine = cosine_of(pitch_degrees);
    if (cosine <= 0) return;
    const std::int32_t stretched =
        static_cast<std::int32_t>(unit_world) * fixed_one / cosine;
    const std::int32_t ground = out[2].y;
    out[0].y = static_cast<std::int16_t>(ground + stretched);
    out[1].y = out[0].y;
}

Projected billboard_screen[unit_count][4];

int transform_billboards() {
    int transformed = 0;
    for (int i = 0; i < unit_count; ++i) {
        Vertex corner[4];
        billboard_corners(units[i], corner);
        Projected out[3];
        transform3(corner[0], corner[1], corner[2], out);
        billboard_screen[i][0] = out[0];
        billboard_screen[i][1] = out[1];
        billboard_screen[i][2] = out[2];
        transform3(corner[3], corner[3], corner[3], out);
        billboard_screen[i][3] = out[2];
        transformed += 6;
    }
    (void)tick();
    return transformed;
}

// ---------------------------------------------------------------------------
// One frame
//
// Draw order is the board's own row order, far row first, each row's units
// immediately after its ground. That is the finding this function exists to
// demonstrate as much as to use: a tactics board seen at a fixed pitch with no
// yaw has a *known* painter's order. The board is its own ordering table, so
// none of the depth-sorting machinery a general 3D scene needs is needed here.
// No ordering table, no AVSZ4 per primitive, no depth buffer, no sort.
// ---------------------------------------------------------------------------

int last_ground = 0;
int last_skirts = 0;
int last_units = 0;

// `shrink` collapses every primitive toward the screen centre by that divisor,
// which changes how many pixels are filled and changes nothing else. It is how
// the fill-rate question gets asked.
void draw_frame(int factor, bool textured, bool with_units, int shrink) {
    const int columns = board_columns * factor;
    const int rows = board_rows * factor;
    int ground = 0;
    int skirts = 0;
    int drawn_units = 0;

    const auto shrunk = [shrink](Projected p) {
        if (shrink <= 1) return p;
        const int cx = gpu::screen_width / 2;
        const int cy = gpu::screen_height / 2;
        p.x = static_cast<std::int16_t>(cx + (p.x - cx) / shrink);
        p.y = static_cast<std::int16_t>(cy + (p.y - cy) / shrink);
        return p;
    };

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int kind = kind_at(column / factor, row / factor);
            const int variant = terrain_variant(column, row);
            const Projected corner[4] = {
                shrunk(grid[row][column]),
                shrunk(grid[row][column + 1]),
                shrunk(grid[row + 1][column]),
                shrunk(grid[row + 1][column + 1]),
            };
            if (textured) {
                draw_textured_quad(corner, terrain_cell_of[kind][variant],
                                   terrain_clut_of[kind]);
            } else {
                draw_flat_quad(corner, 0x40C060u);
            }
            ++ground;

            // The cliff face under this cell's near edge, where the ground in
            // front of it stands lower. Drawn straight after the cell it hangs
            // from, which is the same painter's order.
            const int column_right = (column + 1 + factor - 1) / factor;
            const int row_below = (row + 1 + factor - 1) / factor;
            if (vertex_steps(column_right, row_below) >
                front_steps(column_right, row_below)) {
                const Projected face[4] = {
                    shrunk(grid[row + 1][column]),
                    shrunk(grid[row + 1][column + 1]),
                    shrunk(skirt_grid[row + 1][column]),
                    shrunk(skirt_grid[row + 1][column + 1]),
                };
                if (textured) {
                    draw_textured_quad(face, terrain_cell_of[kind][0],
                                       terrain_clut_of[kind]);
                } else {
                    draw_flat_quad(face, 0x203040u);
                }
                ++skirts;
            }
        }
        if (with_units) {
            for (int i = 0; i < unit_count; ++i) {
                if (static_cast<int>(units[i].row) * factor + factor - 1 !=
                    row) {
                    continue;
                }
                const Projected corner[4] = {
                    shrunk(billboard_screen[i][0]),
                    shrunk(billboard_screen[i][1]),
                    shrunk(billboard_screen[i][2]),
                    shrunk(billboard_screen[i][3]),
                };
                if (textured) {
                    draw_textured_quad(
                        corner,
                        character_cell_of[units[i].archetype][units[i].colour],
                        character_clut_of[units[i].archetype][units[i].colour]
                    );
                } else {
                    draw_flat_quad(corner, 0xC04040u);
                }
                ++drawn_units;
            }
        }
        (void)tick();
    }

    last_ground = ground;
    last_skirts = skirts;
    last_units = drawn_units;
}

// ---------------------------------------------------------------------------
// The three costs, separated
//
// Everything above measures a *frame as written*: the scene is generated,
// transformed and submitted in one pass, and the arithmetic that decides which
// terrain a cell is and how high its corners stand runs inside the timed
// region. That is an honest measurement of a naive renderer and a dishonest
// measurement of the hardware, because on this board the generating costs more
// than the coprocessor does. It was measured before it was believed: the first
// run of this program reported 8 microseconds a vertex, which is thirty times
// what an RTPT can possibly cost, and the difference was `vertex_steps`.
//
// So the three are separated here, each timed on its own over arrays prepared
// beforehand:
//
//   BUILD      world vertices out of the board (pure integer arithmetic)
//   TRANSFORM  those vertices through RTPT (the coprocessor, nothing else)
//   SUBMIT     prepared packets down the GP0 port (the bus, nothing else)
//
// Between frames a board changes nothing but the camera. A renderer that cached
// the rest pays TRANSFORM and SUBMIT every frame and BUILD once, when the board
// is loaded. Those are the numbers the recommendation is built on, and the
// frame-as-written numbers are what a first implementation would actually see.
// ---------------------------------------------------------------------------

constexpr int max_vertices = max_across * max_down * 2 + unit_count * 4;
constexpr int max_packets = max_across * max_down * 2 + unit_count;

Vertex world_vertices[max_vertices];
// Written by `transform_world` and read by nobody. That is deliberate and not
// dead code: the transform has to land somewhere the compiler cannot prove is
// unobservable, or a measurement of it would be a measurement of an empty loop.
Projected projected_vertices[max_vertices];

struct Packet final {
    Projected corner[4];
    std::int16_t cell;
    std::int16_t clut;
};

Packet packets[max_packets];

// Every vertex the frame transforms, in the order it transforms them.
int build_world(int factor) {
    const int across = board_columns * factor + 1;
    const int down = board_rows * factor + 1;
    int count = 0;
    for (int gz = 0; gz < down; ++gz) {
        for (int gx = 0; gx < across; ++gx) {
            world_vertices[count++] = grid_vertex(gx, gz, factor, false);
            world_vertices[count++] = grid_vertex(gx, gz, factor, true);
        }
        (void)tick();
    }
    for (int i = 0; i < unit_count; ++i) {
        Vertex corner[4];
        billboard_corners(units[i], corner);
        for (int c = 0; c < 4; ++c) world_vertices[count++] = corner[c];
    }
    return count;
}

// Those vertices through the coprocessor and nowhere else.
void transform_world(int count) {
    int i = 0;
    for (; i + 2 < count; i += 3) {
        transform3(world_vertices[i], world_vertices[i + 1],
                   world_vertices[i + 2], &projected_vertices[i]);
        if ((i & 0xFF) == 0) (void)tick();
    }
    for (; i < count; ++i) {
        Projected out[3];
        transform3(world_vertices[i], world_vertices[i], world_vertices[i],
                   out);
        projected_vertices[i] = out[0];
    }
}

// Every primitive the frame draws, prepared. `grid`, `skirt_grid` and
// `billboard_screen` must already hold the current camera's projection.
int prepare_packets(int factor) {
    const int columns = board_columns * factor;
    const int rows = board_rows * factor;
    int count = 0;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int kind = kind_at(column / factor, row / factor);
            const int variant = terrain_variant(column, row);
            Packet& ground = packets[count++];
            ground.corner[0] = grid[row][column];
            ground.corner[1] = grid[row][column + 1];
            ground.corner[2] = grid[row + 1][column];
            ground.corner[3] = grid[row + 1][column + 1];
            ground.cell = static_cast<std::int16_t>(terrain_cell_of[kind][variant]);
            ground.clut = static_cast<std::int16_t>(terrain_clut_of[kind]);

            const int column_right = (column + 1 + factor - 1) / factor;
            const int row_below = (row + 1 + factor - 1) / factor;
            if (vertex_steps(column_right, row_below) >
                front_steps(column_right, row_below)) {
                Packet& face = packets[count++];
                face.corner[0] = grid[row + 1][column];
                face.corner[1] = grid[row + 1][column + 1];
                face.corner[2] = skirt_grid[row + 1][column];
                face.corner[3] = skirt_grid[row + 1][column + 1];
                face.cell = static_cast<std::int16_t>(terrain_cell_of[kind][0]);
                face.clut = static_cast<std::int16_t>(terrain_clut_of[kind]);
            }
        }
        (void)tick();
    }
    for (int i = 0; i < unit_count; ++i) {
        Packet& unit = packets[count++];
        for (int c = 0; c < 4; ++c) unit.corner[c] = billboard_screen[i][c];
        unit.cell = static_cast<std::int16_t>(
            character_cell_of[units[i].archetype][units[i].colour]);
        unit.clut = static_cast<std::int16_t>(
            character_clut_of[units[i].archetype][units[i].colour]);
    }
    return count;
}

void submit_packets(int count) {
    for (int i = 0; i < count; ++i) {
        draw_textured_quad(packets[i].corner, packets[i].cell, packets[i].clut);
        if ((i & 0xFF) == 0) (void)tick();
    }
}

// The same, over one row's worth, for a scene that has to put something between
// two rows of the board.
void submit_packet_range(int from, int to) {
    for (int i = from; i < to; ++i) {
        draw_textured_quad(packets[i].corner, packets[i].cell, packets[i].clut);
    }
    (void)tick();
}

// ---------------------------------------------------------------------------
// The framebuffer, as a number
//
// FNV-1a-64 over all 76,800 halfwords the display shows, read back through
// GP0(0xC0). That is the machine answering what it stored, not the program
// repeating what it meant. Two runs of the same scene must produce the same
// number, and §4's proposal rests on that.
// ---------------------------------------------------------------------------

[[nodiscard]] std::uint64_t hash_framebuffer() {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    wait_for(status_ready_for_command);
    *gp0 = 0xC0u << 24;
    *gp0 = 0;
    *gp0 = (static_cast<std::uint32_t>(gpu::screen_height) << 16) |
           static_cast<std::uint32_t>(gpu::screen_width);
    const int words = gpu::screen_width * gpu::screen_height / 2;
    for (int i = 0; i < words; ++i) {
        wait_for(status_ready_to_send);
        const std::uint32_t word = *gp0;
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= (word >> (byte * 8)) & 0xFFu;
            hash *= 0x100000001b3ull;
        }
        if ((i & 0x3FF) == 0) (void)tick();
    }
    return hash;
}

// ---------------------------------------------------------------------------
// The measurements
// ---------------------------------------------------------------------------

constexpr std::uint32_t thirty_fps_microseconds = 33333;
constexpr std::uint32_t sixty_fps_microseconds = 16666;

struct FrameCost final {
    std::uint32_t transform_us;
    std::uint32_t submit_us;
    std::uint32_t total_us;
    int ground;
    int skirts;
    int units;
    int vertices;
};

[[nodiscard]] FrameCost measure_frame(int factor, bool textured,
                                      bool with_units, int shrink) {
    FrameCost cost{};
    psx::start_clock();
    const std::uint32_t t0 = tick();
    cost.vertices = transform_grid(factor);
    if (with_units) cost.vertices += transform_billboards();
    const std::uint32_t t1 = tick();
    draw_frame(factor, textured, with_units, shrink);
    const std::uint32_t t2 = tick();
    cost.transform_us = microseconds(t1 - t0);
    cost.submit_us = microseconds(t2 - t1);
    cost.total_us = microseconds(t2 - t0);
    cost.ground = last_ground;
    cost.skirts = last_skirts;
    cost.units = last_units;
    return cost;
}

[[nodiscard]] int triangles_of(const FrameCost& cost) noexcept {
    return (cost.ground + cost.skirts + cost.units) * 2;
}

void report_frame(const char* label, const FrameCost& cost) {
    Line()
        .text("FRAME ")
        .text(label)
        .text(" ground ")
        .decimal(static_cast<std::uint32_t>(cost.ground))
        .text(" skirts ")
        .decimal(static_cast<std::uint32_t>(cost.skirts))
        .text(" units ")
        .decimal(static_cast<std::uint32_t>(cost.units))
        .text(" tris ")
        .decimal(static_cast<std::uint32_t>(triangles_of(cost)))
        .text(" verts ")
        .decimal(static_cast<std::uint32_t>(cost.vertices))
        .flush();
    Line()
        .text("FRAME ")
        .text(label)
        .text(" transform ")
        .decimal(cost.transform_us)
        .text(" us submit ")
        .decimal(cost.submit_us)
        .text(" us total ")
        .decimal(cost.total_us)
        .text(" us fps ")
        .decimal(cost.total_us == 0 ? 0u : 1000000u / cost.total_us)
        .flush();
}

// ---------------------------------------------------------------------------
// The pad
//
// SIO0, bit-banged, because there is nothing to reuse: no executable in this
// repository reads a controller on any console yet. The shipped clients are
// driven by frame-counted scripts through a harness, which is exactly why the
// determinism below is possible at all.
//
// The transfer is the documented one. Port 1 is selected, five bytes go out
// (0x01, 0x42, and three pad bytes) and five come back: 0xFF, the device
// identifier (0x41 is a digital pad), 0x5A, and two button halves in which a
// *clear* bit is a pressed button. Every wait is bounded by a spin count, so a
// port with nothing plugged into it produces a measurement rather than a hang.
// That is not a hypothetical: the emulator this runs under is headless and may
// well answer that there is no pad.
//
// The poll happens every frame of the pan below and is inside the timed region,
// because the question is what a *moving* frame costs and a
// renderer that panned would have to read the pad in it.
// ---------------------------------------------------------------------------

volatile std::uint8_t* const joy_data =
    reinterpret_cast<volatile std::uint8_t*>(0xbf801040);
volatile std::uint32_t* const joy_stat =
    reinterpret_cast<volatile std::uint32_t*>(0xbf801044);
volatile std::uint16_t* const joy_mode =
    reinterpret_cast<volatile std::uint16_t*>(0xbf801048);
volatile std::uint16_t* const joy_ctrl =
    reinterpret_cast<volatile std::uint16_t*>(0xbf80104a);
volatile std::uint16_t* const joy_baud =
    reinterpret_cast<volatile std::uint16_t*>(0xbf80104e);

constexpr std::uint32_t joy_stat_tx_ready = 1u << 0;
constexpr std::uint32_t joy_stat_rx_not_empty = 1u << 1;
constexpr std::uint32_t joy_stat_ack = 1u << 7;

// One byte at 250 kHz is 32 microseconds, which is about 1,100 cycles of a
// 33.8688 MHz R3000A, so a few thousand turns of a four-instruction spin is
// generous for a pad that is answering and cheap for one that is not.
constexpr int joy_spins = 2000;

// The acknowledgement is budgeted separately and much more tightly, and the
// reason is a measurement rather than a preference. A pad raises /ACK about ten
// microseconds after each byte; PCSX-Redux delivers the byte and this program
// never observes the level set at all, so a generous budget is not a safety
// margin here. It is four timeouts burned every frame. At 2,000 spins that cost
// 3,572 microseconds a poll, which is more than twice the whole rest of a
// panning frame and would have been reported as the cost of panning. The
// budget is therefore small enough to be cheap and long enough to cover real
// hardware, and `joy_acknowledgements` counts how often the level was actually
// seen so that the number is never taken on trust again.
constexpr int joy_ack_spins = 128;
int joy_acknowledgements = 0;
int joy_acknowledgements_waited = 0;

// Button bits, as the two halves arrive, inverted to active-high. The d-pad is
// the only half this program uses.
constexpr std::uint16_t pad_up = 1u << 4;
constexpr std::uint16_t pad_right = 1u << 5;
constexpr std::uint16_t pad_down = 1u << 6;
constexpr std::uint16_t pad_left = 1u << 7;

struct PadReading final {
    std::uint16_t buttons;
    std::uint8_t identifier;
    bool answered;
};

[[nodiscard]] std::uint8_t joy_exchange(std::uint8_t out, bool& lost) noexcept {
    int spins = joy_spins;
    while ((*joy_stat & joy_stat_tx_ready) == 0) {
        if (--spins <= 0) {
            lost = true;
            return 0xFF;
        }
    }
    *joy_data = out;
    spins = joy_spins;
    while ((*joy_stat & joy_stat_rx_not_empty) == 0) {
        if (--spins <= 0) {
            lost = true;
            return 0xFF;
        }
    }
    return *joy_data;
}

[[nodiscard]] PadReading poll_pad() noexcept {
    *joy_ctrl = 0x0040u;  // reset the port
    for (int i = 0; i < 16; ++i) (void)*joy_stat;
    *joy_mode = 0x000Du;  // eight bits, no parity, baud reload factor 1
    *joy_baud = 0x0088u;  // 250 kHz, which is the pad's rate
    *joy_ctrl = 0x0003u;  // transmit enable, controller port one selected
    for (int i = 0; i < 64; ++i) (void)*joy_stat;

    static const std::uint8_t request[5] = {0x01, 0x42, 0x00, 0x00, 0x00};
    std::uint8_t reply[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool lost = false;
    for (int i = 0; i < 5; ++i) {
        reply[i] = joy_exchange(request[i], lost);
        if (lost) break;
        if (i == 4) break;
        ++joy_acknowledgements_waited;
        int spins = joy_ack_spins;
        while ((*joy_stat & joy_stat_ack) == 0) {
            if (--spins <= 0) break;
        }
        if (spins > 0) ++joy_acknowledgements;
    }
    *joy_ctrl = 0;

    PadReading reading{};
    reading.identifier = reply[1];
    reading.answered = !lost && reply[1] == 0x41;
    const std::uint16_t raw = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(reply[3]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(reply[4]) << 8)
    );
    reading.buttons =
        reading.answered ? static_cast<std::uint16_t>(~raw) : std::uint16_t{0};
    return reading;
}

// ---------------------------------------------------------------------------
// The pad script
//
// Frame-counted, never wall-clocked, and the only input the measurements below
// depend on. The live pad is polled every frame and its buttons are folded in,
// so a person holding a controller steers the same camera. Nothing in this
// report is decided by what they do, which is what makes two runs comparable.
//
// Two hundred and six frames is just under seven seconds at thirty. The circuit
// is deliberate: rest, then the four directions in turn with a settle between,
// so every measurement below exists at rest, in motion and at a clamp. Forty
// frames a direction is long enough that a *free* pan reaches the clamp. A
// cursor-following one is stopped by the cursor's own bounds long before, which
// is itself one of the findings.
// ---------------------------------------------------------------------------

struct PadStep final {
    int frames;
    std::uint16_t buttons;
};

constexpr PadStep pad_script[] = {
    { 6, 0},         // rest
    {40, pad_up},    // toward the far edge
    {10, 0},         // settle
    {40, pad_right},
    {10, 0},
    {40, pad_down},
    {10, 0},
    {40, pad_left},
    {10, 0},
};
constexpr int pad_script_steps =
    static_cast<int>(sizeof(pad_script) / sizeof(pad_script[0]));

[[nodiscard]] constexpr int pad_script_length() noexcept {
    int total = 0;
    for (int i = 0; i < pad_script_steps; ++i) total += pad_script[i].frames;
    return total;
}

constexpr int pan_frames = pad_script_length();

[[nodiscard]] std::uint16_t scripted_buttons(int frame) noexcept {
    int at = 0;
    for (int i = 0; i < pad_script_steps; ++i) {
        at += pad_script[i].frames;
        if (frame < at) return pad_script[i].buttons;
    }
    return 0;
}

// Where the framebuffer is hashed: every twentieth frame from the fifth. Under
// the cursor-following idiom the camera is in motion at 25, 65, 125, 165 and
// 185 and settled at the rest, which is the point. A determinism claim that
// only holds when nothing is moving is not the claim §4 needs.
constexpr int checkpoint_frames[] = {5,   25,  45,  65,  85, 105,
                                     125, 145, 165, 185, 205};
constexpr int checkpoint_count =
    static_cast<int>(sizeof(checkpoint_frames) / sizeof(checkpoint_frames[0]));

// ---------------------------------------------------------------------------
// The cursor, and the camera that follows it
//
// This is `view::Camera::follow` transliterated, and it is worth saying which
// parts are the transliteration and which are new, because the difference is
// where three dimensions actually cost something.
//
// The same: a cursor is a board cell, a direction moves it one cell, the camera
// is not the cursor but chases it, and the chase has a dead zone of `margin`
// cells inside which the camera does not move at all. The shipped Nintendo 64
// client uses margin 2; that is the number, not an invented one.
//
// New, and forced by the projection:
//
//   * the camera's position is continuous rather than a whole cell, because a
//     3D camera that jumped 64 world units between frames would be a cut, not
//     a pan. So it *glides*: `pan_units_per_frame` toward its target every
//     frame, never overshooting.
//   * the clamp is a clamp on the camera's **focus**, not on a viewport
//     rectangle, because a perspective camera's footprint on the board is a
//     trapezoid and there is no rectangle to keep inside anything. What that
//     costs is measured rather than argued: see the CLAMP lines.
//
// The pan rate, decided here and stated once. **A quarter of a tile a frame:
// sixteen world units, or 7.5 cells a second at thirty frames.** Two reasons
// for that number rather than a rounder one. It crosses the twelve-cell board
// in 48 frames and its nine-cell depth in 36, which is a second and a half
// either way and is the range every 3D tactics game's camera lives in. And it
// is exactly one cell per `cursor_repeat_frames`, so a held direction moves the
// cursor at precisely the speed the camera can follow it: the camera never
// falls behind a held d-pad and never catches up early, and the two numbers are
// therefore one number.
// ---------------------------------------------------------------------------

constexpr std::int32_t pan_units_per_frame = tile_world / 4;
constexpr int cursor_repeat_frames = 4;
constexpr int follow_margin = 2;

// The two idioms, because the difference between them turns out to be the
// answer rather than a detail. Under `cursor_follow` the d-pad moves a cursor
// and the camera chases it; under `free_pan` the d-pad moves the camera and the
// cursor is not consulted. The first is what the shipped clients do and what a
// 3D client would inherit; the second is what a camera has to be allowed to do
// if the far row is ever to reach the near row's size, for the reason 9c
// measures.
enum class PanIdiom { cursor_follow, free_pan };

struct Pan final {
    int cursor_column;
    int cursor_row;
    Focus focus;
    Focus target;
    int repeat;
    std::uint16_t held;
};

[[nodiscard]] std::int32_t clamp_to(std::int32_t value,
                                    std::int32_t limit) noexcept {
    if (value < -limit) return -limit;
    if (value > limit) return limit;
    return value;
}

[[nodiscard]] std::int32_t cell_centre_x(int column) noexcept {
    return -board_half_width + static_cast<std::int32_t>(column) * tile_world +
           tile_world / 2;
}

[[nodiscard]] std::int32_t cell_centre_z(int row) noexcept {
    return -board_half_depth + static_cast<std::int32_t>(row) * tile_world +
           tile_world / 2;
}

// `reach` is how far past the board's own edge the focus may travel. Zero is
// the two-dimensional precedent taken literally: the camera looks at the board
// and nothing else. What a non-zero reach buys, and what the precedent costs,
// is the CLAMP measurement.
[[nodiscard]] Focus follow_target(Focus target, int column, int row, int margin,
                                  std::int32_t reach) noexcept {
    const std::int32_t deadzone =
        static_cast<std::int32_t>(margin) * tile_world;
    const std::int32_t cx = cell_centre_x(column);
    const std::int32_t cz = cell_centre_z(row);
    if (cx < target.x - deadzone) target.x = cx + deadzone;
    if (cx > target.x + deadzone) target.x = cx - deadzone;
    if (cz < target.z - deadzone) target.z = cz + deadzone;
    if (cz > target.z + deadzone) target.z = cz - deadzone;
    target.x = clamp_to(target.x, board_half_width + reach);
    target.z = clamp_to(target.z, board_half_depth + reach);
    return target;
}

[[nodiscard]] std::int32_t glide(std::int32_t from, std::int32_t to) noexcept {
    if (to > from) {
        const std::int32_t next = from + pan_units_per_frame;
        return next > to ? to : next;
    }
    if (to < from) {
        const std::int32_t next = from - pan_units_per_frame;
        return next < to ? to : next;
    }
    return from;
}

// One frame of input. Row zero of this scene is the *near* edge, at negative
// world Z, so up the screen is an increasing row index here. That is the
// opposite of the way a two-dimensional board array reads, and only obvious
// once it is written down.
void pan_step(Pan& pan, std::uint16_t buttons, PanIdiom idiom, int margin,
              std::int32_t reach) noexcept {
    if (idiom == PanIdiom::free_pan) {
        // The d-pad drives the camera itself, one glide step a frame, so a held
        // direction is continuous motion and the response is a single frame.
        // The cursor is left where it is: it is not what is being measured.
        if ((buttons & pad_left) != 0) pan.target.x -= pan_units_per_frame;
        if ((buttons & pad_right) != 0) pan.target.x += pan_units_per_frame;
        if ((buttons & pad_up) != 0) pan.target.z += pan_units_per_frame;
        if ((buttons & pad_down) != 0) pan.target.z -= pan_units_per_frame;
        pan.target.x = clamp_to(pan.target.x, board_half_width + reach);
        pan.target.z = clamp_to(pan.target.z, board_half_depth + reach);
        pan.focus.x = glide(pan.focus.x, pan.target.x);
        pan.focus.z = glide(pan.focus.z, pan.target.z);
        return;
    }

    bool move = false;
    if (buttons != pan.held) pan.repeat = 0;
    pan.held = buttons;
    if (buttons == 0) {
        pan.repeat = 0;
    } else {
        move = pan.repeat == 0;
        pan.repeat = pan.repeat + 1 >= cursor_repeat_frames ? 0 : pan.repeat + 1;
    }
    if (move) {
        if ((buttons & pad_left) != 0) --pan.cursor_column;
        if ((buttons & pad_right) != 0) ++pan.cursor_column;
        if ((buttons & pad_up) != 0) ++pan.cursor_row;
        if ((buttons & pad_down) != 0) --pan.cursor_row;
        if (pan.cursor_column < 0) pan.cursor_column = 0;
        if (pan.cursor_column >= board_columns) {
            pan.cursor_column = board_columns - 1;
        }
        if (pan.cursor_row < 0) pan.cursor_row = 0;
        if (pan.cursor_row >= board_rows) pan.cursor_row = board_rows - 1;
    }
    pan.target =
        follow_target(pan.target, pan.cursor_column, pan.cursor_row, margin,
                      reach);
    pan.focus.x = glide(pan.focus.x, pan.target.x);
    pan.focus.z = glide(pan.focus.z, pan.target.z);
}

// The camera starts settled on the cursor, not at the origin, or the first
// frame of a run would be a pan nobody asked for and the latency below would be
// measured against a camera that was already moving.
[[nodiscard]] Pan pan_at_rest(PanIdiom idiom, int margin,
                              std::int32_t reach) noexcept {
    Pan pan{board_columns / 2, board_rows / 2, Focus{0, 0}, Focus{0, 0}, 0, 0};
    if (idiom == PanIdiom::cursor_follow) {
        pan.target = follow_target(pan.target, pan.cursor_column,
                                   pan.cursor_row, margin, reach);
        pan.focus = pan.target;
    }
    return pan;
}

// ---------------------------------------------------------------------------
// The board, cached
//
// The measurement the recommendation rests on is the *cached* one: a board's
// vertices and its topology do not change when the camera moves, so a renderer
// that panned would rebuild neither. `build_world` already caches the world
// vertices; this caches the other half, which `prepare_packets` was recomputing
// every time: which terrain a cell is, which CLUT it takes, whether it has a
// cliff face under it, and which four transformed vertices its quad indexes.
//
// So a panning frame is: read the pad, move the camera, put the cached vertices
// through the coprocessor, copy four screen coordinates into each cached
// packet, clear, and send. Nothing else. That is the frame the numbers below
// are of, and it is the frame a real renderer would have.
// ---------------------------------------------------------------------------

constexpr int pan_plan_capacity = board_columns * board_rows * 2 + unit_count;

struct PacketPlan final {
    std::uint16_t corner[4];
    std::int16_t cell;
    std::int16_t clut;
};

PacketPlan packet_plan[pan_plan_capacity];
int packet_plan_count = 0;
int cached_vertex_count = 0;

// Where `build_world(1)` put a vertex. Ground and skirt alternate, then the
// units' four corners each; these three functions are the only place that
// order is known, and the CHECK below proves them against the transform the
// rest of the program uses rather than trusting them.
[[nodiscard]] std::uint16_t ground_index(int gz, int gx) noexcept {
    return static_cast<std::uint16_t>((gz * (board_columns + 1) + gx) * 2);
}

[[nodiscard]] std::uint16_t skirt_index(int gz, int gx) noexcept {
    return static_cast<std::uint16_t>(ground_index(gz, gx) + 1);
}

[[nodiscard]] std::uint16_t unit_index(int unit, int corner) noexcept {
    return static_cast<std::uint16_t>(
        (board_columns + 1) * (board_rows + 1) * 2 + unit * 4 + corner
    );
}

// `draw_frame`'s painter's order, reproduced exactly, with the row direction
// made a parameter so that it can be measured rather than argued about.
//
// It needs to be, because the file contradicts itself about which direction
// that is. `draw_frame` says "far row first"; the pitch sweep says, correctly,
// that "row zero of the grid is the *near* edge: its world Z is negative", and
// both loops run from row zero upward. So the board is painted **near row
// first**, which is the reverse of what §3.1's no-depth-buffer argument needs:
// a raised cell's far edge lifts up into the screen area of the row behind it,
// and the row behind it is drawn afterwards. Whether that is visible on this
// board is a measurement and not a matter of opinion, so the two orders are
// both drawn and both hashed.
//
// A pan does not disturb the ordering question either way: translating the
// camera along the board plane cannot reorder two cells in depth, because depth
// is an affine function of world Z and the translation is the same for both.
// Where each drawn row's packets end, in the order they were planned. A scene
// that puts something *between* two rows of the board needs the row boundaries
// and not just the total. That is every scene from §11 down, because a unit
// stands on a cell and the row in front of it has to be painted afterwards.
int plan_row_end[board_rows];

int plan_packets(bool far_first, bool with_units) {
    int count = 0;
    for (int step = 0; step < board_rows; ++step) {
        const int row = far_first ? board_rows - 1 - step : step;
        for (int column = 0; column < board_columns; ++column) {
            const int kind = kind_at(column, row);
            const int variant = terrain_variant(column, row);
            PacketPlan& ground = packet_plan[count++];
            ground.corner[0] = ground_index(row, column);
            ground.corner[1] = ground_index(row, column + 1);
            ground.corner[2] = ground_index(row + 1, column);
            ground.corner[3] = ground_index(row + 1, column + 1);
            ground.cell =
                static_cast<std::int16_t>(terrain_cell_of[kind][variant]);
            ground.clut = static_cast<std::int16_t>(terrain_clut_of[kind]);

            if (vertex_steps(column + 1, row + 1) >
                front_steps(column + 1, row + 1)) {
                PacketPlan& face = packet_plan[count++];
                face.corner[0] = ground_index(row + 1, column);
                face.corner[1] = ground_index(row + 1, column + 1);
                face.corner[2] = skirt_index(row + 1, column);
                face.corner[3] = skirt_index(row + 1, column + 1);
                face.cell = static_cast<std::int16_t>(terrain_cell_of[kind][0]);
                face.clut = static_cast<std::int16_t>(terrain_clut_of[kind]);
            }
        }
        if (with_units) {
            for (int i = 0; i < unit_count; ++i) {
                if (static_cast<int>(units[i].row) != row) continue;
                PacketPlan& unit = packet_plan[count++];
                for (int c = 0; c < 4; ++c) {
                    unit.corner[c] = unit_index(i, c);
                }
                unit.cell = static_cast<std::int16_t>(
                    character_cell_of[units[i].archetype][units[i].colour]);
                unit.clut = static_cast<std::int16_t>(
                    character_clut_of[units[i].archetype][units[i].colour]);
            }
        }
        plan_row_end[step] = count;
    }
    return count;
}

void refresh_packets() {
    for (int i = 0; i < packet_plan_count; ++i) {
        Packet& packet = packets[i];
        const PacketPlan& plan = packet_plan[i];
        packet.corner[0] = projected_vertices[plan.corner[0]];
        packet.corner[1] = projected_vertices[plan.corner[1]];
        packet.corner[2] = projected_vertices[plan.corner[2]];
        packet.corner[3] = projected_vertices[plan.corner[3]];
        packet.cell = plan.cell;
        packet.clut = plan.clut;
    }
}

// GP0(0x02), the fill. A camera that moves has to clear, and a camera that does
// not never did, which is why `gpu::begin` is enough for a still frame and why
// the clear is charged to the panning frame here rather than quietly left out
// of it.
void clear_screen_with(std::uint32_t colour) {
    wait_for(status_ready_for_command);
    *gp0 = (0x02u << 24) | (colour & 0x00FFFFFFu);
    *gp0 = 0;
    *gp0 = (static_cast<std::uint32_t>(gpu::screen_height) << 16) |
           static_cast<std::uint32_t>(gpu::screen_width);
}

// The magenta the rest of the program uses: deliberately a colour nothing in
// the art can produce, so off-board area is unmistakable in a measurement.
void clear_screen() {
    clear_screen_with(0x00F800F8u);
}

// The observer's slot. A byte written to 0x1f802081 hands control to
// `PCSX.execSlots[13]`, which is where `platform/playstation/harness`'s render
// probe already lives; the scratch's own Lua uses the same number so there is
// one convention rather than two. With no observer attached the write is
// ignored and the run is identical, which is what lets the plain
// `run-playstation.sh` invocation still work.
constexpr int observer_slot = 13;

// How many pictures have been asked for so far. The host script numbers what it
// is given and reads the guest's own `SHOT` lines to learn which ordinal is
// which, so this counter is the only place the numbering lives. Adding a phase
// that photographs something cannot silently renumber a later one.
int shots_taken = 0;

void take_shot() {
    psx::signal_host(observer_slot);
    ++shots_taken;
}

// `SHOT <ordinal> still <name>`. The host writes `<name>.png`.
void take_still(const char* name) {
    Line()
        .text("SHOT ")
        .decimal(static_cast<std::uint32_t>(shots_taken))
        .text(" still ")
        .text(name)
        .flush();
    take_shot();
}

// `SHOT film <n> begins at ordinal <o>, <name>, <k> frames`. The host writes
// `<name>.gif`. Called immediately before the run that fills it.
int films_announced = 0;

void announce_film(const char* name, int frames) {
    Line()
        .text("SHOT film ")
        .decimal(static_cast<std::uint32_t>(films_announced++))
        .text(" begins at ordinal ")
        .decimal(static_cast<std::uint32_t>(shots_taken))
        .text(", ")
        .text(name)
        .text(", ")
        .decimal(static_cast<std::uint32_t>(frames))
        .text(" frames, 30 fps")
        .flush();
}

// ---------------------------------------------------------------------------
// One panning run
// ---------------------------------------------------------------------------

struct PanRun final {
    std::uint32_t poll_us;
    std::uint32_t camera_us;
    std::uint32_t transform_us;
    std::uint32_t refresh_us;
    std::uint32_t clear_us;
    std::uint32_t submit_us;
    std::uint32_t total_us;
    std::uint32_t shortest_us;
    std::uint32_t longest_us;
    int longest_frame;
    int frames;
    int first_input_frame;
    int first_move_frame;
    int pad_answers;
    std::int32_t furthest_x;
    std::int32_t furthest_z;
};

struct PanSettings final {
    PanIdiom idiom;
    int margin;
    std::int32_t reach;
    bool moving;
};

// `hashes` may be null. `film_from > film_to` films nothing. The hashing and
// the filming both happen outside the timed region, and the filming is done by
// a separate run of the same script so that the numbers and the pictures never
// come from the same pass. An observer that cost time would otherwise be
// measured as the renderer costing time.
PanRun run_pan(PanSettings settings, std::uint64_t* hashes, int film_from,
               int film_to) {
    Pan pan = pan_at_rest(settings.idiom, settings.margin, settings.reach);
    PanRun run{};
    run.shortest_us = 0xFFFFFFFFu;
    run.first_input_frame = -1;
    run.first_move_frame = -1;
    int checkpoint = 0;

    for (int frame = 0; frame < pan_frames; ++frame) {
        psx::start_clock();
        const std::uint32_t t0 = tick();
        const PadReading reading = poll_pad();
        const std::uint32_t t1 = tick();
        const std::uint16_t scripted =
            settings.moving ? scripted_buttons(frame) : std::uint16_t{0};
        const std::uint16_t buttons =
            static_cast<std::uint16_t>(reading.buttons | scripted);
        const Focus before = pan.focus;
        pan_step(pan, buttons, settings.idiom, settings.margin, settings.reach);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, pan.focus);
        const std::uint32_t t2 = tick();
        transform_world(cached_vertex_count);
        const std::uint32_t t3 = tick();
        refresh_packets();
        const std::uint32_t t4 = tick();
        clear_screen();
        const std::uint32_t t5 = tick();
        submit_packets(packet_plan_count);
        const std::uint32_t t6 = tick();

        if (reading.answered) ++run.pad_answers;
        if (scripted != 0 && run.first_input_frame < 0) {
            run.first_input_frame = frame;
        }
        if ((pan.focus.x != before.x || pan.focus.z != before.z) &&
            run.first_move_frame < 0) {
            run.first_move_frame = frame;
        }
        const std::int32_t magnitude_x =
            pan.focus.x < 0 ? -pan.focus.x : pan.focus.x;
        const std::int32_t magnitude_z =
            pan.focus.z < 0 ? -pan.focus.z : pan.focus.z;
        if (magnitude_x > run.furthest_x) run.furthest_x = magnitude_x;
        if (magnitude_z > run.furthest_z) run.furthest_z = magnitude_z;

        run.poll_us += microseconds(t1 - t0);
        run.camera_us += microseconds(t2 - t1);
        run.transform_us += microseconds(t3 - t2);
        run.refresh_us += microseconds(t4 - t3);
        run.clear_us += microseconds(t5 - t4);
        run.submit_us += microseconds(t6 - t5);
        const std::uint32_t total = microseconds(t6 - t0);
        run.total_us += total;
        if (total < run.shortest_us) run.shortest_us = total;
        if (total > run.longest_us) {
            run.longest_us = total;
            run.longest_frame = frame;
        }
        ++run.frames;

        if (hashes != nullptr && checkpoint < checkpoint_count &&
            frame == checkpoint_frames[checkpoint]) {
            hashes[checkpoint] = hash_framebuffer();
            ++checkpoint;
        }
        if (frame >= film_from && frame <= film_to) {
            take_shot();
        }
    }
    return run;
}

void report_pan(const char* label, const PanRun& run) {
    Line()
        .text("PAN ")
        .text(label)
        .text(" frames ")
        .decimal(static_cast<std::uint32_t>(run.frames))
        .text(" mean ")
        .decimal(run.frames == 0 ? 0u :
                 run.total_us / static_cast<std::uint32_t>(run.frames))
        .text(" us shortest ")
        .decimal(run.shortest_us)
        .text(" longest ")
        .decimal(run.longest_us)
        .text(" us at frame ")
        .decimal(static_cast<std::uint32_t>(run.longest_frame))
        .flush();
    const std::uint32_t frames =
        run.frames == 0 ? 1u : static_cast<std::uint32_t>(run.frames);
    Line()
        .text("PAN ")
        .text(label)
        .text(" per frame poll ")
        .decimal(run.poll_us / frames)
        .text(" camera ")
        .decimal(run.camera_us / frames)
        .text(" transform ")
        .decimal(run.transform_us / frames)
        .text(" refresh ")
        .decimal(run.refresh_us / frames)
        .text(" clear ")
        .decimal(run.clear_us / frames)
        .text(" submit ")
        .decimal(run.submit_us / frames)
        .text(" us")
        .flush();
    Line()
        .text("PAN ")
        .text(label)
        .text(" mean fps ")
        .decimal(run.total_us == 0 ? 0u :
                 1000000u * frames / run.total_us)
        .text(" worst fps ")
        .decimal(run.longest_us == 0 ? 0u : 1000000u / run.longest_us)
        .text(" furthest focus x ")
        .signed_decimal(static_cast<int>(run.furthest_x))
        .text(" z ")
        .signed_decimal(static_cast<int>(run.furthest_z))
        .flush();
}

// ---------------------------------------------------------------------------
// What a row is drawn at, from a given focus
//
// The far-cell objection is a claim about pixels, so this reports pixels: how
// wide a tile of the near row and of the far row is drawn, how deep each sits,
// and where the board's own edges land on a 320-pixel display. Panning is
// supposed to move the second set of numbers toward the first; whether it does,
// and how far, is the whole question.
// ---------------------------------------------------------------------------

struct RowMetrics final {
    std::uint16_t near_depth;
    std::uint16_t far_depth;
    int near_tile;
    int far_tile;
    int near_left;
    int near_right;
    int far_left;
    int far_right;
    int near_y;
    int far_y;
};

[[nodiscard]] RowMetrics measure_rows(Focus focus) {
    load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                camera_vertical, focus);
    (void)transform_grid(1);
    RowMetrics metrics{};
    metrics.near_depth = grid[0][board_columns / 2].depth;
    metrics.far_depth = grid[board_rows][board_columns / 2].depth;
    metrics.near_tile = grid[0][1].x - grid[0][0].x;
    metrics.far_tile = grid[board_rows][1].x - grid[board_rows][0].x;
    metrics.near_left = grid[0][0].x;
    metrics.near_right = grid[0][board_columns].x;
    metrics.far_left = grid[board_rows][0].x;
    metrics.far_right = grid[board_rows][board_columns].x;
    metrics.near_y = grid[0][board_columns / 2].y;
    metrics.far_y = grid[board_rows][board_columns / 2].y;
    return metrics;
}

void report_rows(const char* label, Focus focus, const RowMetrics& metrics) {
    Line()
        .text("CLAMP ")
        .text(label)
        .text(" focus x ")
        .signed_decimal(static_cast<int>(focus.x))
        .text(" z ")
        .signed_decimal(static_cast<int>(focus.z))
        .text("; near row depth ")
        .decimal(metrics.near_depth)
        .text(" tile ")
        .signed_decimal(metrics.near_tile)
        .text(" px; far row depth ")
        .decimal(metrics.far_depth)
        .text(" tile ")
        .signed_decimal(metrics.far_tile)
        .text(" px")
        .flush();
    Line()
        .text("CLAMP ")
        .text(label)
        .text(" near row x ")
        .signed_decimal(metrics.near_left)
        .text("..")
        .signed_decimal(metrics.near_right)
        .text(" y ")
        .signed_decimal(metrics.near_y)
        .text("; far row x ")
        .signed_decimal(metrics.far_left)
        .text("..")
        .signed_decimal(metrics.far_right)
        .text(" y ")
        .signed_decimal(metrics.far_y)
        .flush();
}

// ---------------------------------------------------------------------------
// 11. One archetype as a low-poly mesh
//
// §5.1's films show the billboarded units are inadequate, "really bad", and
// asked for one archetype built as a mesh beside its billboard, same board,
// same camera, so the two can be compared the way the panning was. This is that
// mesh and the scene that shows it.
//
// **The knight, and why it rather than another.** It is entry zero of
// `characters.ARCHETYPE_CLASSES`, the first of the six roles the sample
// campaign expects, and the melee unit a tactics board is mostly made of. Its
// silhouette is the one a verdict is really about. It is also the hardest
// case for a mesh and therefore the honest one: the mesh rules are
// silhouette-first, and a knight's silhouette is the least distinctive in the
// roster (a mage's hat, an archer's bow and a beast's four legs all read at
// eight pixels; a knight is a rectangle with a head). If a mesh cannot beat a
// billboard here it cannot beat one anywhere, and if it can, the rest of the
// roster is easier.
//
// **The shape.** Twenty boxes at 6 quads each: **120 quads, 240 triangles, 160
// vertices**. The boxes are boots, shins, thighs, belt, tabard, arms,
// pauldrons, gorget, head, helm, visor, crest, shield, and a sword in two
// pieces. That is inside the 150 to 300 the exploration document priced and
// §5.2 restated as the constraint, and it is checked rather than counted by
// hand. Boxes rather than a smooth body on purpose: at 20 to 40 screen pixels a
// silhouette is all there is, and a box has flat faces that take a flat shade,
// which is what turns a mesh from an outline into a volume.
//
// **The colour, which is the part with a real design decision in it.** The mesh
// must take the six faction ramps, deterministically, without a new art file
// and without growing the master palette. It does, and the route is the
// generated CLUTs themselves rather than anything hand-written:
//
//   an entry of an archetype's CLUT is **faction-bearing** exactly when its
//   colour word is absent from at least one of the other five factions' CLUTs
//   for the same archetype, and **neutral** when all six carry it.
//
// That rule needs no table, no naming convention and no coordinate anybody
// chose: it is computed at start-up from `grandleon_playstation_character_clut`
// and it holds for every archetype in every style. The knight resolves to a
// four-entry faction ramp and eleven neutral entries in all four styles, the
// healer to three and twelve. Each ramp is then sorted by luminance and sampled
// at four rungs, and a mesh face names a ramp and a rung rather than a colour.
// So a mesh knight in violet is the same 120 faces with a different four
// numbers in them, and the six factions are six numbers the art library already
// generated. Nothing is added to `tools/placeholder_art` at all.
//
// **The shade.** One fixed directional key light, from the front, above and to
// the left, as six per-normal factors. An axis-aligned box has six normals and
// therefore six factors, and a face's colour is its rung scaled by its factor.
// This is the whole of the lighting model, it is integer, it is per face rather
// than per pixel, and it is the reason the mesh reads as a solid: the billboard
// it stands beside is a picture of a lit figure, and the mesh *is* one.
//
// **The order, and the finding in it.** No depth buffer, no ordering table, no
// AVSZ4: the same claim §3.1 makes for the board, one level down. Two
// mechanisms and both are checked rather than asserted. Within a box, faces are
// wound outward and a face whose projected corners come out with the wrong
// winding is skipped, which is exact for a convex solid. Between boxes, the
// array is authored far-to-near at the camera's pitch. A pan is a translation
// and cannot reorder two parts in depth, so the authored order is correct at
// every camera position. That is asserted (`the_authored_part_order` …), and a
// runtime sort is carried anyway because a model that *turns* does reorder its
// own parts, and the turn is the point.
// ---------------------------------------------------------------------------

// A ramp a face may take its colour from. The generator names these, because a
// generated face carries a ramp index and a rung index and nothing else, so the
// two files cannot disagree about which index is which.
constexpr int ramp_neutral = grandleon_playstation_mesh_ramp_neutral;
constexpr int ramp_faction = grandleon_playstation_mesh_ramp_faction;
constexpr int ramp_count = grandleon_playstation_mesh_ramp_count;
constexpr int ramp_rungs = grandleon_playstation_mesh_rung_count;

// The four rungs of each ramp, already widened to the 24-bit BGR a GP0 packet
// carries, plus how many entries the archetype's CLUT actually offered.
struct MeshPalette final {
    std::uint32_t rung[ramp_count][ramp_rungs];
    int faction_entries;
    int neutral_entries;
};

MeshPalette mesh_palette[art::archetype_count][art::faction_colour_count];

[[nodiscard]] bool clut_carries(const unsigned short* clut,
                                unsigned short want) noexcept {
    for (int i = 0; i < art::clut_size; ++i) {
        if (clut[i] == want) return true;
    }
    return false;
}

// Five bits a channel to eight, by repeating the top bits, which is exactly
// what `scratch3d_evidence.py` does on the host so a colour means the same
// thing in a packet and in a screenshot. A GP0 colour word is 0x00BBGGRR.
[[nodiscard]] std::uint32_t widen_colour(unsigned short word) noexcept {
    const std::uint32_t red = word & 0x1Fu;
    const std::uint32_t green = (word >> 5) & 0x1Fu;
    const std::uint32_t blue = (word >> 10) & 0x1Fu;
    return (((blue << 3) | (blue >> 2)) << 16) |
           (((green << 3) | (green >> 2)) << 8) | ((red << 3) | (red >> 2));
}

// Luminance in the master palette's own five-bit space, integer, weights 2:5:1.
// Only the *order* it induces is used, so the weights need to be sensible
// rather than colorimetric.
[[nodiscard]] int luminance_of(unsigned short word) noexcept {
    return static_cast<int>(word & 0x1Fu) * 2 +
           static_cast<int>((word >> 5) & 0x1Fu) * 5 +
           static_cast<int>((word >> 10) & 0x1Fu);
}

void sort_by_luminance(unsigned short* entries, int count) noexcept {
    for (int i = 1; i < count; ++i) {
        const unsigned short key = entries[i];
        const int rank = luminance_of(key);
        int j = i - 1;
        while (j >= 0 && luminance_of(entries[j]) > rank) {
            entries[j + 1] = entries[j];
            --j;
        }
        entries[j + 1] = key;
    }
}

[[nodiscard]] unsigned short rung_of(const unsigned short* entries, int count,
                                     int rung) noexcept {
    if (count <= 0) return 0x8000u;
    if (count == 1) return entries[0];
    return entries[rung * (count - 1) / (ramp_rungs - 1)];
}

// The rule stated in the comment above, applied to every archetype and every
// faction colour the embedded style carries. Costs one pass over 16 x 6 x 16
// halfword comparisons per archetype and runs once.
void derive_mesh_palettes() {
    for (int archetype = 0; archetype < art::archetype_count; ++archetype) {
        for (int colour = 0; colour < art::faction_colour_count; ++colour) {
            unsigned short faction[art::clut_size];
            unsigned short neutral[art::clut_size];
            int faction_count = 0;
            int neutral_count = 0;
            const unsigned short* mine = art::character(archetype, colour).clut;
            for (int i = 0; i < art::clut_size; ++i) {
                const unsigned short word = mine[i];
                // A CLUT word of zero is the hole the GPU skips; it is not a
                // colour and a face must never be given it.
                if (word == 0) continue;
                bool everywhere = true;
                for (int other = 0; other < art::faction_colour_count;
                     ++other) {
                    if (!clut_carries(art::character(archetype, other).clut,
                                      word)) {
                        everywhere = false;
                        break;
                    }
                }
                if (everywhere) {
                    neutral[neutral_count++] = word;
                } else {
                    faction[faction_count++] = word;
                }
            }
            sort_by_luminance(faction, faction_count);
            sort_by_luminance(neutral, neutral_count);
            MeshPalette& palette = mesh_palette[archetype][colour];
            palette.faction_entries = faction_count;
            palette.neutral_entries = neutral_count;
            for (int rung = 0; rung < ramp_rungs; ++rung) {
                palette.rung[ramp_neutral][rung] =
                    widen_colour(rung_of(neutral, neutral_count, rung));
                palette.rung[ramp_faction][rung] =
                    widen_colour(rung_of(faction, faction_count, rung));
            }
        }
    }
}

// A rung scaled by a face's light factor, per channel, in eight-bit fixed
// point. 256 is the rung unchanged.
[[nodiscard]] std::uint32_t shade(std::uint32_t colour, int light) noexcept {
    const std::uint32_t factor = static_cast<std::uint32_t>(light);
    const std::uint32_t red = ((colour & 0xFFu) * factor) >> 8;
    const std::uint32_t green = (((colour >> 8) & 0xFFu) * factor) >> 8;
    const std::uint32_t blue = (((colour >> 16) & 0xFFu) * factor) >> 8;
    return (blue << 16) | (green << 8) | red;
}

// ---------------------------------------------------------------------------
// The two figures, as generated tables
//
// They are not here. `tools/placeholder_art/placeholder_art/meshes/` holds
// the knight and the archer as part lists and the art generator emits
// `playstation_meshes_<style>.h` beside the sprite header, chosen by the same
// style seam, which is where a mesh table has to live if adding a style is not
// to cost every build. What is left here is what a *renderer* owns: the face
// winding, the key light, the unpacking into corners, and the arithmetic.
//
// The promotion is proved by hash rather than by inspection: every §5.3, §5.4
// and §5.5 scene below hashes exactly what those sections recorded, and every
// committed evidence file regenerates byte for byte. A generated table that
// could not reproduce a scene exactly would be a rewrite wearing a promotion.
//
// Local space, unchanged: the same axes as the world, x right, y up, z away,
// origin at the centre of the figure's feet. The array is authored **far to
// near at the camera's pitch**, which for a pitch of sixty degrees means
// low-and-behind first and high-and-in-front last: depth falls by sin(phi) per
// unit of height and rises by cos(phi) per unit of z, so feet come before
// shins, shins before thighs, and the crest on the helm is the very last thing
// drawn. The generator refuses a list that is not, and `expect` below measures
// it again on the machine rather than trusting either.
// ---------------------------------------------------------------------------

// **How tall, and the arithmetic that decides it rather than a preference.**
// This is the same trap §1.4 sprang on the world-vertical billboard and it
// springs on a mesh too, because it is geometry and not a defect: a world-
// vertical extent of H is drawn focal x H x cos(phi) / depth pixels tall, so at
// sixty degrees a figure one tile tall is drawn *half* a tile tall. A mesh
// cannot take the screen-space escape a billboard takes, because it has real
// depth and screen-space construction is exactly what it gives up. So it has to
// be **built at unit_world / cos(phi) = 128 world units, two tiles tall**, to
// be drawn one tile tall.
//
// That is the world-stretch §1.4 rejected, and the reason it is right here and
// wrong there is the same measurement. The stretch fails on a billboard because
// its top ends up much nearer the eye than its foot and perspective magnifies
// the correction, by 11 to 140 pixels across the pitch sweep. At the one pitch
// this camera uses, the top of a 128-unit figure is 111 units nearer than its
// foot out of 600 to 888, so the magnification is bounded at about a fifth and
// the drawn height is measured rather than assumed: see the MESH height line.
// A 3D unit *does* get larger as it comes nearer, and unlike the stretched
// billboard it is entitled to.
//
// The generator computes this from the pitch rather than carrying it as a
// preference. Here it arrives, checked against the two the scene is built from
// rather than assumed to agree.
constexpr int mesh_world_height = grandleon_playstation_mesh_world_height;
static_assert(grandleon_playstation_mesh_unit_world == unit_world,
              "the generated meshes were built for a different tile");
static_assert(
    grandleon_playstation_mesh_pitch_degrees == camera_pitch_degrees,
    "the generated meshes were authored far-to-near at a different pitch");
static_assert(mesh_world_height == 128,
              "unit_world / cos(60) is two tiles, and the fit assumes it");

constexpr int mesh_vertices_per_box =
    grandleon_playstation_mesh_vertices_per_part;
constexpr int mesh_faces_per_box = grandleon_playstation_mesh_faces_per_part;
constexpr int mesh_values_per_part =
    grandleon_playstation_mesh_values_per_part;

// The two archetypes that have a mesh, named so the phases below can select
// them. The generator holds the rest of the roster at a null row.
constexpr int knight_archetype_slot = 0;
constexpr int archer_archetype_slot = 1;
constexpr int mesh_part_count =
    grandleon_playstation_mesh_part_count[knight_archetype_slot];
constexpr int archer_part_count =
    grandleon_playstation_mesh_part_count[archer_archetype_slot];
constexpr int mesh_vertex_count = mesh_part_count * mesh_vertices_per_box;
constexpr int mesh_face_count = mesh_part_count * mesh_faces_per_box;
constexpr int mesh_triangle_count = mesh_face_count * 2;
constexpr int archer_triangle_count =
    archer_part_count * mesh_faces_per_box * 2;
constexpr int archer_archetype = archer_archetype_slot;

// The arrays below hold whichever model is active, so they are sized for the
// largest the generator emitted and asserted rather than trusted.
constexpr int mesh_max_parts = 24;
static_assert(grandleon_playstation_mesh_max_parts <= mesh_max_parts,
              "every model must fit the shared mesh arrays");
constexpr int mesh_max_vertices = mesh_max_parts * mesh_vertices_per_box;
constexpr int mesh_max_faces = mesh_max_parts * mesh_faces_per_box;

// A box corner is indexed ix + 2*iy + 4*iz, and these are its six faces in the
// Z order GP0(0x28) wants: top-left, top-right, bottom-left, bottom-right as
// seen *from outside the box*, with screen up being increasing world y for the
// four sides and increasing world z for the two horizontal faces. Wound this
// way every outward face projects to a positive signed area, which is the whole
// of the back-face test below.
constexpr std::uint8_t box_face_corners[mesh_faces_per_box][4] = {
    {2, 3, 0, 1},  // front,  normal -z
    {7, 6, 5, 4},  // back,   normal +z
    {6, 7, 2, 3},  // top,    normal +y
    {0, 1, 4, 5},  // bottom, normal -y
    {6, 2, 4, 0},  // left,   normal -x
    {3, 7, 1, 5},  // right,  normal +x
};

// The key light: front, above, and from the left. Six normals, six factors, and
// this is the entire lighting model. The spread is wide on purpose. At a pitch
// of sixty degrees a viewer sees a box's top and its front and almost nothing
// else, so it is the *top against the front* that has to carry the volume, and
// a gentle ramp between them reads as a flat sticker rather than as a solid.
constexpr std::uint8_t box_face_light[mesh_faces_per_box] = {
    178,  // front
    72,   // back
    255,  // top
    52,   // bottom
    148,  // left
    102,  // right
};

struct MeshVertex final {
    std::int16_t x;
    std::int16_t y;
    std::int16_t z;
};

struct MeshFace final {
    std::uint8_t corner[4];
    std::uint8_t ramp;
    std::uint8_t rung;
    std::uint8_t light;
    std::uint8_t pad;
};

// Both models are unpacked once, into a slot each, and selecting one is two
// pointer writes. A battle that mixes archetypes selects per unit per frame,
// and rebuilding a model's corner table every time was measured at most of a
// millisecond a unit. Every phase before §5.4's fit runs with the knight
// selected, so nothing those phases drew or measured can change.
// One slot per archetype, so a model is selected by its archetype index and
// nothing has to map between the two. Slots whose archetype the generator
// emitted no mesh for hold zero parts and draw nothing, which is what makes an
// uncommissioned archetype cost the renderer a branch it already had rather
// than a special case.
constexpr int model_slots = art::archetype_count;
MeshVertex model_local[model_slots][mesh_max_vertices];
MeshFace model_faces[model_slots][mesh_max_faces];
int model_parts[model_slots];

const MeshVertex* mesh_local = model_local[0];
const MeshFace* mesh_faces = model_faces[0];
int active_part_count = 0;
int active_vertex_count = 0;
int active_face_count = 0;

// Unpacks one archetype's generated part list into corners and faces.
//
// A generated part is eight `short`s: x0,x1,y0,y1,z0,z1,ramp,rung, in the
// generator's `PART_FIELDS` order. It is read positionally here because that is
// the only way a flat integer table can be read, and both sides name the same
// `mesh_values_per_part` so a field appended to one is a compile-time mismatch
// in the other rather than a silently shifted mesh.
void build_model(int slot, const short* values, int part_count) {
    model_parts[slot] = part_count;
    MeshVertex* local = model_local[slot];
    MeshFace* faces = model_faces[slot];
    for (int box = 0; box < part_count; ++box) {
        const short* source = values + box * mesh_values_per_part;
        const std::int16_t xs[2] = {source[0], source[1]};
        const std::int16_t ys[2] = {source[2], source[3]};
        const std::int16_t zs[2] = {source[4], source[5]};
        const std::uint8_t ramp = static_cast<std::uint8_t>(source[6]);
        const std::uint8_t rung = static_cast<std::uint8_t>(source[7]);
        for (int iz = 0; iz < 2; ++iz) {
            for (int iy = 0; iy < 2; ++iy) {
                for (int ix = 0; ix < 2; ++ix) {
                    local[box * mesh_vertices_per_box + ix + 2 * iy +
                          4 * iz] = MeshVertex{xs[ix], ys[iy], zs[iz]};
                }
            }
        }
        for (int face = 0; face < mesh_faces_per_box; ++face) {
            MeshFace& out = faces[box * mesh_faces_per_box + face];
            for (int corner = 0; corner < 4; ++corner) {
                out.corner[corner] = static_cast<std::uint8_t>(
                    box * mesh_vertices_per_box +
                    box_face_corners[face][corner]);
            }
            out.ramp = ramp;
            out.rung = rung;
            out.light = box_face_light[face];
            out.pad = 0;
        }
    }
}

void select_slot(int slot) {
    mesh_local = model_local[slot];
    mesh_faces = model_faces[slot];
    active_part_count = model_parts[slot];
    active_vertex_count = active_part_count * mesh_vertices_per_box;
    active_face_count = active_part_count * mesh_faces_per_box;
}

void build_mesh() {
    for (int archetype = 0; archetype < model_slots; ++archetype) {
        const art::Mesh figure = art::mesh(archetype);
        build_model(archetype, figure.parts, figure.part_count);
    }
    select_slot(knight_archetype_slot);
}

// Where a mesh unit stands and how it is turned. `yaw` turns the model about
// its own vertical axis and `lean` tips it about its own x axis, pivoting at
// the feet. The camera still has no yaw, and §3.1 is untouched. Both are the
// motions a billboard cannot have, which is what the film exists to show.
struct MeshPose final {
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    int yaw_degrees;
    int lean_degrees;
};

Vertex mesh_world[mesh_max_vertices];
Projected mesh_screen[mesh_max_vertices];

// §5.4's screen-stabilised scale, in 1/256ths, applied about the feet (the
// local origin) so a scaled unit stays planted on its tile. It is
// `mesh_scale_one` in every phase before the fit, and the loops below keep the
// exact arithmetic of the unscaled build when it is, so every §5.3 scene and
// hash is untouched by the fit existing.
//
// §5.5 splits the scale by screen axis. Screen x is world x alone (row 0 of
// the view matrix is (1, 0, 0)), while screen y mixes world y and z. So
// `mesh_scale_x` wears the drawn *width* and `mesh_scale`, applied to y and to
// z together, wears the drawn *height*. Every phase before §5.5 sets the two
// equal, which is the uniform scale §5.4 measured, multiply for multiply.
constexpr std::int32_t mesh_scale_one = 256;
constexpr std::int32_t mesh_scale_floor = 64;
constexpr std::int32_t mesh_scale_ceiling = 768;
std::int32_t mesh_scale = mesh_scale_one;
std::int32_t mesh_scale_x = mesh_scale_one;

// Whether the last pose built actually turned the model. A settled unit is a
// pure translation of the authored mesh, so its world build is three adds a
// vertex with no multiply in it and its part order is the authored one. On a
// tactics board that is every unit that is not currently animating, and
// therefore almost all of them almost always. Both fast paths are here rather
// than in a comment because the difference is a third of a frame at sixteen
// units, and the report gives the two costs separately so neither hides the
// other.
bool mesh_pose_turned = false;

void build_mesh_world(const MeshPose& pose) noexcept {
    mesh_pose_turned = pose.yaw_degrees != 0 || pose.lean_degrees != 0;
    if (!mesh_pose_turned) {
        if (mesh_scale == mesh_scale_one && mesh_scale_x == mesh_scale_one) {
            // The unscaled build, kept as the exact arithmetic §5.3 measured.
            for (int i = 0; i < active_vertex_count; ++i) {
                mesh_world[i] = Vertex{
                    static_cast<std::int16_t>(pose.x + mesh_local[i].x),
                    static_cast<std::int16_t>(pose.y + mesh_local[i].y),
                    static_cast<std::int16_t>(pose.z + mesh_local[i].z)};
            }
            return;
        }
        for (int i = 0; i < active_vertex_count; ++i) {
            mesh_world[i] = Vertex{
                static_cast<std::int16_t>(
                    pose.x + (mesh_local[i].x * mesh_scale_x) / mesh_scale_one),
                static_cast<std::int16_t>(
                    pose.y + (mesh_local[i].y * mesh_scale) / mesh_scale_one),
                static_cast<std::int16_t>(
                    pose.z + (mesh_local[i].z * mesh_scale) / mesh_scale_one)};
        }
        return;
    }
    const std::int32_t yaw_sin = sine_full(pose.yaw_degrees);
    const std::int32_t yaw_cos = cosine_full(pose.yaw_degrees);
    const std::int32_t lean_sin = sine_full(pose.lean_degrees);
    const std::int32_t lean_cos = cosine_full(pose.lean_degrees);
    for (int i = 0; i < active_vertex_count; ++i) {
        // Scaling first and rotating after are the same map either way round
        // when the scale is uniform, which it is on every turned pose this
        // program draws (§5.5's split scale never turns), and scaling first
        // keeps the intermediate values small. At scale one the multiplies are
        // skipped, not because dividing by 256 would change a value (it would
        // not) but so §5.3's turning-build figure stays re-derivable as
        // measured.
        std::int32_t lx = mesh_local[i].x;
        std::int32_t ly = mesh_local[i].y;
        std::int32_t lz = mesh_local[i].z;
        if (mesh_scale != mesh_scale_one || mesh_scale_x != mesh_scale_one) {
            lx = (lx * mesh_scale_x) / mesh_scale_one;
            ly = (ly * mesh_scale) / mesh_scale_one;
            lz = (lz * mesh_scale) / mesh_scale_one;
        }
        const std::int32_t turned_x = (lx * yaw_cos + lz * yaw_sin) / fixed_one;
        const std::int32_t turned_z =
            (-lx * yaw_sin + lz * yaw_cos) / fixed_one;
        const std::int32_t tipped_y =
            (ly * lean_cos - turned_z * lean_sin) / fixed_one;
        const std::int32_t tipped_z =
            (ly * lean_sin + turned_z * lean_cos) / fixed_one;
        mesh_world[i] =
            Vertex{static_cast<std::int16_t>(pose.x + turned_x),
                   static_cast<std::int16_t>(pose.y + tipped_y),
                   static_cast<std::int16_t>(pose.z + tipped_z)};
    }
}

void transform_mesh() noexcept {
    int i = 0;
    for (; i + 2 < active_vertex_count; i += 3) {
        transform3(mesh_world[i], mesh_world[i + 1], mesh_world[i + 2],
                   &mesh_screen[i]);
    }
    for (; i < active_vertex_count; ++i) {
        Projected out[3];
        transform3(mesh_world[i], mesh_world[i], mesh_world[i], out);
        mesh_screen[i] = out[0];
    }
}

// The part order, far first. A part's depth is the sum of its eight corners'
// depths, which needs no division and is monotone in the mean. Insertion sort
// over the parts: quadratic in twenty of them, which is nothing, and no
// allocation anywhere.
int mesh_order[mesh_max_parts];

void sort_mesh_parts() noexcept {
    std::int32_t depth[mesh_max_parts];
    for (int part = 0; part < active_part_count; ++part) {
        std::int32_t sum = 0;
        for (int corner = 0; corner < mesh_vertices_per_box; ++corner) {
            sum += mesh_screen[part * mesh_vertices_per_box + corner].depth;
        }
        depth[part] = sum;
        mesh_order[part] = part;
    }
    for (int i = 1; i < active_part_count; ++i) {
        const int part = mesh_order[i];
        const std::int32_t key = depth[part];
        int j = i - 1;
        while (j >= 0 && depth[mesh_order[j]] < key) {
            mesh_order[j + 1] = mesh_order[j];
            --j;
        }
        mesh_order[j + 1] = part;
    }
}

void order_mesh_parts() noexcept {
    if (!mesh_pose_turned) {
        // Depth is affine in world y and z and a translation is the same for
        // both, so a translation cannot reorder two parts in depth. A settled
        // unit is correct in the order the array was authored in, at every
        // camera position a pan can reach. Checked rather than assumed:
        // `an_unturned_mesh_needs_no_sort_at_all` runs the sort anyway and
        // requires it to agree.
        for (int part = 0; part < active_part_count; ++part) {
            mesh_order[part] = part;
        }
        return;
    }
    sort_mesh_parts();
}

// Outward faces project to a positive signed area; this is the sign of the
// cross product of the two edges leaving corner zero of the Z-ordered quad. The
// GTE has NCLIP, which is this in one instruction and would be free; it is done
// here in integer C because the measurement wanted is what a face costs, not
// what one more opcode encoding costs to verify.
[[nodiscard]] bool faces_the_viewer(const Projected corner[4]) noexcept {
    const std::int32_t ax = corner[1].x - corner[0].x;
    const std::int32_t ay = corner[1].y - corner[0].y;
    const std::int32_t bx = corner[2].x - corner[0].x;
    const std::int32_t by = corner[2].y - corner[0].y;
    return ax * by - ay * bx > 0;
}

int last_mesh_faces_drawn = 0;

// `textured` draws each face with the archetype's own 32x32 character cell
// stretched across it instead of a flat shade. It is here because the brief
// asked for both to be tried rather than argued about; §5.3 reports what it
// looks like.
void draw_mesh(int archetype, int colour, bool textured) {
    order_mesh_parts();
    const MeshPalette& palette = mesh_palette[archetype][colour];
    const int cell = character_cell_of[archetype][colour < 2 ? colour : 0];
    const int clut = character_clut_of[archetype][colour < 2 ? colour : 0];
    int drawn = 0;
    for (int slot = 0; slot < active_part_count; ++slot) {
        const int part = mesh_order[slot];
        for (int index = 0; index < mesh_faces_per_box; ++index) {
            const MeshFace& face = mesh_faces[part * mesh_faces_per_box + index];
            const Projected corner[4] = {
                mesh_screen[face.corner[0]],
                mesh_screen[face.corner[1]],
                mesh_screen[face.corner[2]],
                mesh_screen[face.corner[3]],
            };
            if (!faces_the_viewer(corner)) continue;
            if (textured) {
                draw_textured_quad(corner, cell, clut);
            } else {
                draw_flat_quad(corner,
                               shade(palette.rung[face.ramp][face.rung],
                                     static_cast<int>(face.light)));
            }
            ++drawn;
        }
    }
    last_mesh_faces_drawn = drawn;
    (void)tick();
}

// ---------------------------------------------------------------------------
// The comparison scene
//
// The same board, the same camera, and three pairs: a mesh knight and a
// billboard knight in adjacent cells at the near row, the middle row and the
// far row. The sixteen battle units are left out of this scene deliberately.
// The question is which of two ways of drawing a unit reads better, and a
// screen with sixteen other units on it is a screen the eye has to hunt.
// ---------------------------------------------------------------------------

struct ComparePair final {
    std::int8_t column;  // the mesh; the billboard stands at column + 1
    std::int8_t row;
    std::int8_t colour;
};

constexpr ComparePair compare_pairs[] = {
    {4, 0, 0},  // the near row, drawn at 34 px a tile
    {8, 4, 1},  // the middle row
    {6, 8, 0},  // the far row, drawn at 22 px a tile
};
constexpr int compare_pair_count =
    static_cast<int>(sizeof(compare_pairs) / sizeof(compare_pairs[0]));

constexpr int knight_archetype = 0;

// Where a unit's feet sit, in world units: the centre of its cell, at the
// elevation the terrain registry gives that cell.
[[nodiscard]] MeshPose pose_on(int column, int row, std::int32_t dx,
                               std::int32_t dz, std::int32_t dy, int yaw,
                               int lean) noexcept {
    return MeshPose{
        cell_centre_x(column) + dx,
        static_cast<std::int32_t>(steps_at(column, row)) * step_world + dy,
        cell_centre_z(row) + dz,
        yaw,
        lean,
    };
}

// The billboard the mesh is being compared against, and it is deliberately the
// **best** one this document knows how to build rather than the one §1 drew.
//
// §1.4 measured three constructions and §3.2 chose between them: a
// world-vertical quad is foreshortened by cos(phi) and comes out 9 to 18 pixels
// tall at this pitch, a world-stretched one runs away with the pitch, and a
// *screen-space* one (project the foot, then emit a square of side
// focal x size / depth) lands at 22 to 31 pixels and is independent of the
// pitch by construction. That is the one §3.2 says to build, so that is the one
// the mesh has to beat. Comparing a mesh against the foreshortened billboard
// would be comparing it against a defect the document already knows how to fix.
//
// One divide a unit, one transform of one vertex. The quad is an axis-aligned
// integer rectangle, which is also what keeps §4's per-pixel claims applicable
// to a unit.
[[nodiscard]] bool screen_billboard(std::int32_t x, std::int32_t y,
                                    std::int32_t z, Projected out[4]) noexcept {
    const Vertex foot{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                      static_cast<std::int16_t>(z)};
    Projected projected[3];
    transform3(foot, foot, foot, projected);
    const Projected base = projected[0];
    if (base.depth == 0) return false;
    const int side = camera_focal * unit_world / base.depth;
    const int left = base.x - side / 2;
    const int right = left + side;
    const int top = base.y - side;
    out[0] = Projected{static_cast<std::int16_t>(left),
                       static_cast<std::int16_t>(top), base.depth};
    out[1] = Projected{static_cast<std::int16_t>(right),
                       static_cast<std::int16_t>(top), base.depth};
    out[2] = Projected{static_cast<std::int16_t>(left), base.y, base.depth};
    out[3] = Projected{static_cast<std::int16_t>(right), base.y, base.depth};
    return true;
}

void draw_billboard_at(std::int32_t x, std::int32_t y, std::int32_t z,
                       int archetype, int colour) {
    Projected corner[4];
    if (!screen_billboard(x, y, z, corner)) return;
    draw_textured_quad(corner, character_cell_of[archetype][colour],
                       character_clut_of[archetype][colour]);
}

// ---------------------------------------------------------------------------
// The fit: a screen-stabilised mesh (§5.4)
//
// §5.3's mesh is drawn 41 px on the far row and 25 px on the near. That is
// backwards with distance, and reads as units clearly disproportional in size.
// The cause is §5.3's own second finding: a near unit sits below the screen
// centre, so proximity magnifies its positive screen offset and pushes its head
// back down, and no single built height can be right at more than one screen
// position. The global two-tile build is exactly such a single height.
//
// The fix keeps the unit's world position and gives up world-honest *size*. The
// feet stay at the tile centre and the parts keep their real depths against
// each other and against the board, while each unit is scaled about its feet so
// that its drawn height equals its own tile's drawn width, which is also
// exactly the screen-space billboard's side. That is the trade §3.2's billboard
// already makes, taken by the mesh: world position, screen-tuned scale.
//
// The scale is solved from the projection rather than tuned. A foot at
// camera-space height A (screen down positive) and depth D, under pitch phi,
// wearing a world-vertical extent k, is drawn
//
//     h = focal * k * (D*cos - A*sin) / (D * (D - k*sin))
//
// pixels tall. The (D*cos - A*sin) factor is the screen-offset magnification
// §5.3 measured, negative-A far rows drawn taller and positive-A near rows
// shorter. Setting h to the billboard's own side, focal*unit_world/D, and
// solving for k:
//
//     k = 4096 * unit_world * D / (D*cos - A*sin + unit_world*sin)
//
// with the trig in 1.12 fixed point. The focal length cancels, so the fit is
// three multiplies and a divide per unit per frame, on top of one foot
// transform the billboard also pays. Dropping the two A- and sin-terms ("scale
// by depth alone") cancels D as well and leaves k = unit_world/cos = 128, which
// is precisely §5.3's two-tile build: the naive compensation IS the status quo,
// and the screen-offset term is the entire difference. That is measured below
// (the FIT naive line) rather than only argued.
//
// A and D come off the coprocessor itself, from IR2 and SZ3 after transforming
// the foot, so the fit is exactly as deterministic and as host-predictable as
// the projection §4 proved.
// ---------------------------------------------------------------------------

struct FootView final {
    std::int32_t cx;     // camera-space x of the foot: the unit's axis
    std::int32_t cy;     // camera-space height of the foot, screen down
    std::int32_t sy;     // its screen row, for the planted-feet check
    std::int32_t depth;  // SZ3
};

[[nodiscard]] FootView view_foot(std::int32_t x, std::int32_t y,
                                 std::int32_t z) {
    const Vertex foot{static_cast<std::int16_t>(x),
                      static_cast<std::int16_t>(y),
                      static_cast<std::int16_t>(z)};
    Projected out[3];
    transform3(foot, foot, foot, out);
    std::int32_t ir1 = 0;
    std::int32_t ir2 = 0;
    GTE_GET_DATA(9, ir1);   // IR1: camera-space x of the last vertex
    GTE_GET_DATA(10, ir2);  // IR2: camera-space y of the last vertex
    return FootView{ir1, ir2, static_cast<std::int32_t>(out[2].y),
                    static_cast<std::int32_t>(out[2].depth)};
}

// How often a scale ever hit its guard rails. The rails exist so that a foot
// far off screen cannot ask for a pathological scale; every scene below is
// required to never touch them.
int fitted_scales_clamped = 0;

// The closed form, solved for a requested drawn extent: the world-vertical
// extent whose drawn height equals `target_world`'s own billboard projection,
// focal * target_world / depth. §5.4 asks it with `unit_world` (the whole tile
// side) and §5.5 asks it with the sprite's opaque height share of the cell,
// which for the knight is the same number.
[[nodiscard]] std::int32_t solved_scale(const FootView& foot,
                                        std::int32_t target_world,
                                        int& clamped) noexcept {
    const std::int32_t sine = sine_of(camera_pitch_degrees);
    const std::int32_t cosine = cosine_of(camera_pitch_degrees);
    // The fit answers "what height reads right at this screen position", so a
    // foot beyond the display (a panned camera slides the near pair off the
    // bottom) is given the nearest on-display answer: its screen offset is
    // clamped to the rows the display has. That also keeps the denominator
    // positive at every depth (cos exceeds sin times the clamped slope), so an
    // off-screen unit gets a bounded, continuous scale instead of a runaway.
    // There is no 64-bit divide runtime on this machine, so the arithmetic is
    // held inside 32 bits: 4096 * 64 * depth needs depth no larger than 8191,
    // and no scene here is a fifth of that deep, so the depth is clamped for
    // the computation rather than the product widened.
    std::int32_t depth = foot.depth;
    if (depth > 4096) depth = 4096;
    const std::int32_t band = (depth * (gpu::screen_height / 2)) / camera_focal;
    std::int32_t cy = foot.cy;
    if (cy > band) cy = band;
    if (cy < -band) cy = -band;
    const std::int32_t denominator =
        depth * cosine - cy * sine + target_world * sine;
    std::int32_t scale = mesh_scale_ceiling;
    if (denominator > 0) {
        std::int32_t k = (fixed_one * target_world * depth) / denominator;
        // The projection guard: a fitted head is k*sin/4096 nearer than its
        // foot, and nothing is allowed near the divider's floor of H/2. This
        // can only engage for a unit already below the display's bottom edge,
        // so it is a safety rather than a sizing decision and is not counted
        // against the rails.
        const std::int32_t cap =
            ((depth - (camera_focal / 2 + 16)) * fixed_one) / sine;
        if (cap > 0 && k > cap) k = cap;
        scale = (k * mesh_scale_one) / mesh_world_height;
    }
    if (scale < mesh_scale_floor) {
        scale = mesh_scale_floor;
        ++clamped;
    }
    if (scale > mesh_scale_ceiling) {
        scale = mesh_scale_ceiling;
        ++clamped;
    }
    return scale;
}

[[nodiscard]] std::int32_t fitted_scale(const FootView& foot) noexcept {
    return solved_scale(foot, unit_world, fitted_scales_clamped);
}

void select_model(int archetype) { select_slot(archetype); }

// The drawn extent of whatever the mesh arrays currently hold, in screen rows,
// so height is bottom - top. That is the same measurement §5.3's MESH height
// line makes, as a function so the fit can ask it back.
struct DrawnExtent final {
    std::int32_t top;
    std::int32_t bottom;
};

[[nodiscard]] DrawnExtent screen_extent() noexcept {
    DrawnExtent extent{0x7FFFFFFF, -0x7FFFFFFF};
    for (int i = 0; i < active_vertex_count; ++i) {
        if (mesh_screen[i].y < extent.top) extent.top = mesh_screen[i].y;
        if (mesh_screen[i].y > extent.bottom) extent.bottom = mesh_screen[i].y;
    }
    return extent;
}

// Builds and transforms the active model at `pose`, fitted, and returns the
// scale it settled on.
//
// The closed form above models the figure as a line, a head directly over its
// feet. A box model is not one: its boots toe toward the viewer and its helm
// leans away, and the same screen-offset magnification the fit corrects also
// magnifies that spread, by up to nine pixels of forty on the near row. Rather
// than dragging the model's extreme corners into the algebra, the closed form's
// answer is corrected against the measured drawn extent: height is near enough
// linear in scale that `scale * side / height` lands within a pixel in one
// step, and the loop allows a second for the case it does not. Integer
// throughout, so two runs correct identically.
// The scales the fit actually chose, so the report can state the band the
// scenes lived in beside the rails they are checked against.
std::int32_t fitted_scale_low = 0x7FFFFFFF;
std::int32_t fitted_scale_high = 0;

// What §5.4's uniform fit measured, kept so that §5.5's phase can assert its
// own case against it rather than against a number typed into a document. Both
// are the worst over the same three rows and the same 32-camera sweep.
int fitted_row_height_error = 0;
int fitted_sweep_deviation = 0;
std::uint32_t fitted_cache_build_us = 0;

// And the matched sweep's, for the mitigation phases below: what the analytic
// scales cost is a comparison against the refinement they replace, and the
// refinement's number is this one.
int matched_sweep_deviation = 0;

std::int32_t build_fitted(const MeshPose& pose) {
    const FootView foot = view_foot(pose.x, pose.y, pose.z);
    const int side = camera_focal * unit_world / foot.depth;
    std::int32_t scale = fitted_scale(foot);
    // The correction is only meaningful for a unit somebody can see: a foot
    // beyond the display's rows is deep in the frustum's floor, where the
    // projection of an extent no longer means a height. Such a unit takes the
    // analytic answer as-is, bounded and continuous there, and the sweep's
    // deviation measurement skips it for the same reason.
    const bool visible = foot.sy >= 0 && foot.sy < gpu::screen_height;
    const int passes = visible ? 2 : 1;
    for (int refine = 0; refine < passes; ++refine) {
        mesh_scale = scale;
        mesh_scale_x = scale;
        build_mesh_world(pose);
        transform_mesh();
        mesh_scale = mesh_scale_one;
        mesh_scale_x = mesh_scale_one;
        if (!visible) break;
        const DrawnExtent extent = screen_extent();
        const std::int32_t height = extent.bottom - extent.top;
        if (height <= 0) break;
        std::int32_t deviation = height - side;
        if (deviation < 0) deviation = -deviation;
        if (deviation <= 1) break;
        std::int32_t corrected = (scale * side) / height;
        if (corrected < mesh_scale_floor) {
            corrected = mesh_scale_floor;
            ++fitted_scales_clamped;
        }
        if (corrected > mesh_scale_ceiling) {
            corrected = mesh_scale_ceiling;
            ++fitted_scales_clamped;
        }
        if (corrected == scale) break;
        scale = corrected;
    }
    if (scale < fitted_scale_low) fitted_scale_low = scale;
    if (scale > fitted_scale_high) fitted_scale_high = scale;
    return scale;
}

// One fitted unit: fit, build, draw. The scale never escapes the call.
void draw_fitted_mesh(int archetype, int colour, const MeshPose& pose) {
    (void)build_fitted(pose);
    draw_mesh(archetype, colour, false);
}

[[nodiscard]] DrawnExtent measure_drawn(const MeshPose& pose, bool fitted) {
    if (fitted) {
        (void)build_fitted(pose);
    } else {
        mesh_scale = mesh_scale_one;
        mesh_scale_x = mesh_scale_one;
        build_mesh_world(pose);
        transform_mesh();
    }
    return screen_extent();
}

// ---------------------------------------------------------------------------
// The match: a mesh wears its sprite's silhouette (§5.5)
//
// §5.4 matched drawn *height* and the pairs still read as different creatures
// at the three distances. The defect is the one scale: the closed form corrects
// the vertical screen-offset magnification, but a drawn width has no such
// magnification, since screen x is focal * world x / depth and nothing else.
// Scaling width by the height's correction therefore distorts it by exactly
// that correction. The correction grows toward the far rows, so the mismatch is
// not even the same mismatch at the three distances: the numbers below measure
// the §5.4 knight drawn *wider* than its sprite on the near row and far
// *narrower* on the middle and far rows.
//
// The rule that closes it: **a mesh archetype targets its own sprite's
// silhouette**, measured from the art the billboard actually draws. The
// billboard is the sprite's 32x32 cell stretched to a side x side square, so
// the sprite's opaque box (sw x sh texels of the cell) is drawn side*sw/32 by
// side*sh/32; the mesh's drawn box is held to those two numbers with two
// scales: `mesh_scale_x` for width, `mesh_scale` (y and z, the two axes the
// view maps to screen vertical) for height. Height keeps §5.4's closed form
// with the target world extent unit_world*sh/32 instead of the whole tile;
// width needs no screen-position term at all, so its analytic scale is the
// constant unit_world*sw/32 over the model's own authored width. That is
// depth-independent, which is precisely why one uniform scale could never be
// right at more than one distance. Both are refined against the measured drawn
// box the way §5.4 refined height.
// ---------------------------------------------------------------------------

// The sprite's opaque box, read from the cell the billboard draws rather than
// from the art contract's intent: the texels whose CLUT colour is not the
// transparent zero word. Colour 0 is measured; every faction recolours inside
// the same silhouette.
struct SpriteSilhouette final {
    std::int32_t width;
    std::int32_t height;
    std::int32_t area;  // opaque texels, the drawn mass the box carries
};

[[nodiscard]] SpriteSilhouette measure_sprite(int archetype) {
    const art::Asset asset = art::character(archetype, 0);
    int left = art::cell_size;
    int right = -1;
    int top = art::cell_size;
    int bottom = -1;
    int area = 0;
    for (int y = 0; y < art::cell_size; ++y) {
        for (int x = 0; x < art::cell_size; ++x) {
            if (art::colour_at(asset, x, y) == 0) continue;
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
            ++area;
        }
    }
    if (right < left) return SpriteSilhouette{0, 0, 0};
    return SpriteSilhouette{right - left + 1, bottom - top + 1, area};
}

// The active model's authored width, for the width scale's analytic answer.
[[nodiscard]] std::int32_t model_local_width() noexcept {
    std::int32_t left = 0x7FFF;
    std::int32_t right = -0x7FFF;
    for (int i = 0; i < active_vertex_count; ++i) {
        if (mesh_local[i].x < left) left = mesh_local[i].x;
        if (mesh_local[i].x > right) right = mesh_local[i].x;
    }
    return right - left;
}

// ---------------------------------------------------------------------------
// How far the figure is from being a line, in the pixels that costs
//
// Everything the fit and the match get wrong about a figure comes from the
// same place, and it is worth writing down once rather than seven times.
//
// The closed form that solves a scale models a unit as a **vertical line at its
// foot**. A mesh is not one: a box at z = -19 stands nineteen units nearer the
// eye than the axis, and at this pitch that lifts nothing and *lowers*
// everything: screen y falls with z by focal*sin(phi)/depth, which at the near
// row is a third of a pixel per world unit and at unit scale nearly half.
//
// So a figure's departure from the line model is its **authored z, drawn**, and
// the two numbers below are the two ways that departure shows:
//
//   * `model_forward_reach` is how far the nearest box reaches toward the eye.
//     That is exactly how far the drawn silhouette's bottom can sit *below* the
//     foot the unit is standing on, which is what "planted on its tile" asks
//     about. A knight whose boots toe forward is not badly authored; it is a
//     knight with boots.
//   * `model_depth_spread` is nearest box to furthest. That is how much taller
//     or shorter than a line the figure draws, which is what a scale solved for
//     a line has to be corrected for.
//
// Both are properties of the *figure*, and both were previously spelled as a
// constant read off the medieval knight. `drawn_from_local_z` turns either into
// the pixels it is worth at a given scale and depth, which is the form an
// assertion can use.
// ---------------------------------------------------------------------------

[[nodiscard]] std::int32_t model_forward_reach() noexcept {
    std::int32_t nearest = 0x7FFF;
    for (int i = 0; i < active_vertex_count; ++i) {
        if (mesh_local[i].z < nearest) nearest = mesh_local[i].z;
    }
    return nearest < 0 ? -nearest : 0;
}

[[nodiscard]] std::int32_t model_depth_spread() noexcept {
    std::int32_t nearest = 0x7FFF;
    std::int32_t furthest = -0x7FFF;
    for (int i = 0; i < active_vertex_count; ++i) {
        if (mesh_local[i].z < nearest) nearest = mesh_local[i].z;
        if (mesh_local[i].z > furthest) furthest = mesh_local[i].z;
    }
    return furthest - nearest;
}

// One authored z extent, in the screen pixels it is worth at this scale and
// this depth. Rounded up, because it is an allowance: a bound that rounded down
// would refuse a figure for a pixel the arithmetic below it never had.
//
// Two terms, and both come out of the same subtraction. A point at world z
// offset δ from the unit's axis projects to
//
//     focal*(cy - sin*δ) / (depth + cos*δ)
//
// where the line model projects `focal*cy/depth`. Expanding the difference:
//
//     focal*sin*δ/depth        the numerator's term: the point is *lower* on
//                              the screen because it leans toward the eye
//     (sy - OFY)*cos*δ/depth   the denominator's: it is also *nearer*, so
//                              everything about it is magnified, and how much
//                              that moves a point depends on how far from the
//                              screen's centre the point already is
//
// The second is small at the middle of the display and is not small at the top
// of a near-row figure, which is exactly where a tall unit's silhouette is
// measured. `screen_offset` is that distance; pass zero for a bound that only
// wants the first term.
[[nodiscard]] std::int32_t drawn_from_local_z(std::int32_t local_z,
                                              std::int32_t scale,
                                              std::int32_t depth,
                                              std::int32_t screen_offset = 0
                                             ) noexcept {
    if (depth <= 0 || local_z <= 0) return 0;
    // Thirty-two bit steps throughout rather than one sixty-four bit divide:
    // this freestanding R3000A has no `__divdi3`, and the scale can be applied
    // first without losing anything that matters. A scaled world extent is at
    // most a couple of hundred units, and a focal length times a 1.12 sine
    // times that is a couple of hundred million. That is inside a signed
    // thirty-two bit word with an order of magnitude to spare.
    const std::int32_t drawn_world = (local_z * scale) / mesh_scale_one;
    const std::int32_t denominator = fixed_one * depth;
    const std::int32_t lean =
        camera_focal * sine_of(camera_pitch_degrees) * drawn_world;
    const std::int32_t magnification =
        screen_offset * cosine_of(camera_pitch_degrees) * drawn_world;
    return (lean + magnification + denominator - 1) / denominator;
}

// Both screen axes of the transformed model, where §5.4's `screen_extent`
// kept only the vertical. Extents are differences, bottom - top and
// right - left, the same convention the fit measured height with.
struct DrawnBox final {
    std::int32_t left;
    std::int32_t right;
    std::int32_t top;
    std::int32_t bottom;
};

[[nodiscard]] DrawnBox screen_box() noexcept {
    DrawnBox box{0x7FFFFFFF, -0x7FFFFFFF, 0x7FFFFFFF, -0x7FFFFFFF};
    for (int i = 0; i < active_vertex_count; ++i) {
        if (mesh_screen[i].x < box.left) box.left = mesh_screen[i].x;
        if (mesh_screen[i].x > box.right) box.right = mesh_screen[i].x;
        if (mesh_screen[i].y < box.top) box.top = mesh_screen[i].y;
        if (mesh_screen[i].y > box.bottom) box.bottom = mesh_screen[i].y;
    }
    return box;
}

// The transformed model's width about its own axis: each vertex's screen x
// against where the unit's vertical axis lands *at that vertex's own depth*
// (the axis is world-vertical, so its camera-space x is the foot's for every
// height). The difference between this and the box is the perspective lean an
// off-centre tall figure honestly wears, a dozen pixels of a near-row box. The
// lean is 3D truth, not size: matching the sprite's width must squeeze the
// figure, never the lean, or the match itself would make the figure a
// different creature. Measured after the first sweep run drove a box-width
// correction into the floor rail at the display's bottom edge, where the box
// is nearly all lean.
[[nodiscard]] std::int32_t figure_width(const FootView& foot) noexcept {
    std::int32_t narrow = 0x7FFFFFFF;
    std::int32_t wide = -0x7FFFFFFF;
    for (int i = 0; i < active_vertex_count; ++i) {
        const std::int32_t depth = mesh_screen[i].depth;
        if (depth <= 0) continue;
        const std::int32_t axis =
            gpu::screen_width / 2 + (camera_focal * foot.cx) / depth;
        const std::int32_t relative = mesh_screen[i].x - axis;
        if (relative < narrow) narrow = relative;
        if (relative > wide) wide = relative;
    }
    if (wide < narrow) return 0;
    return wide - narrow;
}

// The silhouettes of the two modelled archetypes, measured once at the top of
// the phase, from the very arrays `upload_art` sent to VRAM.
SpriteSilhouette match_silhouette[art::archetype_count];

int matched_scales_clamped = 0;
std::int32_t matched_scale_low = 0x7FFFFFFF;
std::int32_t matched_scale_high = 0;

[[nodiscard]] std::int32_t matched_rail(std::int32_t scale) noexcept {
    if (scale < mesh_scale_floor) {
        scale = mesh_scale_floor;
        ++matched_scales_clamped;
    }
    if (scale > mesh_scale_ceiling) {
        scale = mesh_scale_ceiling;
        ++matched_scales_clamped;
    }
    return scale;
}

struct MatchedScales final {
    std::int32_t width;   // mesh_scale_x, in 1/256ths
    std::int32_t height;  // mesh_scale, likewise
};

// §5.6, mitigation A: "skip the correction pass while panning".
//
// How many times `build_matched` is allowed to build and transform a visible
// unit. Three is §5.5's value and the one every §5.5 scene is drawn with: one
// build from the closed form and up to two correcting passes against what the
// coprocessor actually projected. **One** is the mitigation: the analytic
// scales as solved, built once, no measurement and no re-transform. That is
// where a matched unit's 48,868 µs frame mostly goes.
//
// The loop below is left exactly as §5.5 wrote it for the three-pass case, so
// nothing §5.5 measured can move: the budget only decides whether the
// measurement at the foot of the first pass happens at all.
int matched_refine_budget = 3;

// §5.6, mitigation B: "quantise the scale so small pans invalidate nothing".
//
// The step both scales are rounded to, in the same 1/256ths, or zero for the
// exact scales §5.5 measured. A cache is invalidated by a camera move because
// the fit ties a unit's scale to its depth; rounding the scale to a coarse step
// means a pan that moves a unit a little moves its scale not at all, and the
// cache built for the old camera is still the cache the new one wants. What it
// costs is one more quantum of silhouette error, and the phase measures that
// rather than assuming it is small.
std::int32_t matched_scale_quantum = 0;

[[nodiscard]] std::int32_t quantise_scale(std::int32_t scale) noexcept {
    if (matched_scale_quantum <= 1) return scale;
    const std::int32_t step = matched_scale_quantum;
    return ((scale + step / 2) / step) * step;
}

// Builds and transforms the active model at `pose`, matched to its sprite's
// silhouette, and returns the two scales it settled on. The refinement is
// §5.4's, on both axes: height against the drawn box, §5.4's own measure, and
// width against `figure_width`. Each is near enough linear in its own scale,
// width in the width scale alone and height in the height scale with a small
// cross-term through the parts' depths, that one correcting pass lands within a
// pixel; the loop allows a third for the cross-term.
[[nodiscard]] MatchedScales build_matched(int archetype, const MeshPose& pose) {
    const SpriteSilhouette& sprite = match_silhouette[archetype];
    const FootView foot = view_foot(pose.x, pose.y, pose.z);
    const int side = camera_focal * unit_world / foot.depth;
    const std::int32_t target_w = (side * sprite.width) / art::cell_size;
    const std::int32_t target_h = (side * sprite.height) / art::cell_size;
    std::int32_t height_scale = solved_scale(
        foot, (unit_world * sprite.height) / art::cell_size,
        matched_scales_clamped);
    std::int32_t width_scale = matched_rail(
        (unit_world * sprite.width * mesh_scale_one) /
        (art::cell_size * model_local_width()));
    const bool visible = foot.sy >= 0 && foot.sy < gpu::screen_height;
    const int passes = visible ? matched_refine_budget : 1;
    for (int refine = 0; refine < passes; ++refine) {
        mesh_scale = height_scale;
        mesh_scale_x = width_scale;
        build_mesh_world(pose);
        transform_mesh();
        mesh_scale = mesh_scale_one;
        mesh_scale_x = mesh_scale_one;
        if (!visible) break;
        // Mitigation A: one build is the whole budget, so the analytic scales
        // are the answer and there is nothing to measure them against. The
        // build above is the one the caller draws.
        if (passes <= 1) break;
        const DrawnBox box = screen_box();
        const std::int32_t height = box.bottom - box.top;
        const std::int32_t width = figure_width(foot);
        if (height <= 0 || width <= 0) break;
        std::int32_t deviation_h = height - target_h;
        if (deviation_h < 0) deviation_h = -deviation_h;
        std::int32_t deviation_w = width - target_w;
        if (deviation_w < 0) deviation_w = -deviation_w;
        if (deviation_h <= 1 && deviation_w <= 1) break;
        const std::int32_t corrected_h =
            matched_rail((height_scale * target_h) / height);
        const std::int32_t corrected_w =
            matched_rail((width_scale * target_w) / width);
        if (corrected_h == height_scale && corrected_w == width_scale) break;
        height_scale = corrected_h;
        width_scale = corrected_w;
    }
    // Mitigation B: round both scales to the step and rebuild on the rounded
    // pair, so that what the unit is actually drawn at is what the cache would
    // be keyed by. Off by default, and then this is not even a branch taken.
    if (matched_scale_quantum > 1) {
        const std::int32_t rounded_h = matched_rail(quantise_scale(height_scale));
        const std::int32_t rounded_w = matched_rail(quantise_scale(width_scale));
        if (rounded_h != height_scale || rounded_w != width_scale) {
            height_scale = rounded_h;
            width_scale = rounded_w;
            mesh_scale = height_scale;
            mesh_scale_x = width_scale;
            build_mesh_world(pose);
            transform_mesh();
            mesh_scale = mesh_scale_one;
            mesh_scale_x = mesh_scale_one;
        }
    }
    if (height_scale < matched_scale_low) matched_scale_low = height_scale;
    if (height_scale > matched_scale_high) matched_scale_high = height_scale;
    if (width_scale < matched_scale_low) matched_scale_low = width_scale;
    if (width_scale > matched_scale_high) matched_scale_high = width_scale;
    return MatchedScales{width_scale, height_scale};
}

void draw_matched_mesh(int archetype, int colour, const MeshPose& pose) {
    (void)build_matched(archetype, pose);
    draw_mesh(archetype, colour, false);
}

// What the framebuffer actually holds that is not backdrop: bounding box and
// filled pixel count, read back through GP0(0xC0) the way the hash is. That is
// the machine answering what it drew. This is the measurement that lets a mesh
// silhouette and a sprite silhouette be compared as *pictures*: draw one
// figure alone on the magenta, then count.
struct Coverage final {
    std::int32_t left;
    std::int32_t right;
    std::int32_t top;
    std::int32_t bottom;
    std::int32_t area;
};

[[nodiscard]] Coverage measure_coverage() {
    constexpr std::uint16_t backdrop = 0x7C1F;  // the magenta, in VRAM's word
    Coverage coverage{0x7FFFFFFF, -0x7FFFFFFF, 0x7FFFFFFF, -0x7FFFFFFF, 0};
    wait_for(status_ready_for_command);
    *gp0 = 0xC0u << 24;
    *gp0 = 0;
    *gp0 = (static_cast<std::uint32_t>(gpu::screen_height) << 16) |
           static_cast<std::uint32_t>(gpu::screen_width);
    const int words = gpu::screen_width * gpu::screen_height / 2;
    for (int i = 0; i < words; ++i) {
        wait_for(status_ready_to_send);
        const std::uint32_t word = *gp0;
        for (int half = 0; half < 2; ++half) {
            const std::uint16_t pixel =
                static_cast<std::uint16_t>(word >> (half * 16));
            if (pixel == backdrop) continue;
            const int index = i * 2 + half;
            const std::int32_t x = index % gpu::screen_width;
            const std::int32_t y = index / gpu::screen_width;
            if (x < coverage.left) coverage.left = x;
            if (x > coverage.right) coverage.right = x;
            if (y < coverage.top) coverage.top = y;
            if (y > coverage.bottom) coverage.bottom = y;
            ++coverage.area;
        }
        if ((i & 0x3FF) == 0) (void)tick();
    }
    return coverage;
}

// The fitted comparison frame: the same board, the same three pairs, the mesh
// fitted. `mesh_archetypes` decides which model stands at each pair. The sweep
// film uses the archer at the middle pair so both archetypes are seen at every
// distance, and the stills use one archetype throughout.
void draw_fitted_comparison_frame(const std::int8_t mesh_archetypes[3]) {
    transform_world(cached_vertex_count);
    refresh_packets();
    clear_screen();
    int from = 0;
    for (int step = 0; step < board_rows; ++step) {
        const int row = board_rows - 1 - step;
        submit_packet_range(from, plan_row_end[step]);
        from = plan_row_end[step];
        for (int pair = 0; pair < compare_pair_count; ++pair) {
            if (static_cast<int>(compare_pairs[pair].row) != row) continue;
            const int column = compare_pairs[pair].column;
            const int colour = compare_pairs[pair].colour;
            const int archetype = mesh_archetypes[pair];
            select_model(archetype);
            draw_fitted_mesh(archetype, colour,
                             pose_on(column, row, 0, 0, 0, 0, 0));
            const MeshPose flat_pose = pose_on(column + 1, row, 0, 0, 0, 0, 0);
            draw_billboard_at(flat_pose.x, flat_pose.y, flat_pose.z, archetype,
                              colour < 2 ? colour : 0);
        }
    }
}

// The same frame with §5.5's silhouette match instead of §5.4's height fit.
// This is the picture the two rules are compared by.
void draw_matched_comparison_frame(const std::int8_t mesh_archetypes[3]) {
    transform_world(cached_vertex_count);
    refresh_packets();
    clear_screen();
    int from = 0;
    for (int step = 0; step < board_rows; ++step) {
        const int row = board_rows - 1 - step;
        submit_packet_range(from, plan_row_end[step]);
        from = plan_row_end[step];
        for (int pair = 0; pair < compare_pair_count; ++pair) {
            if (static_cast<int>(compare_pairs[pair].row) != row) continue;
            const int column = compare_pairs[pair].column;
            const int colour = compare_pairs[pair].colour;
            const int archetype = mesh_archetypes[pair];
            select_model(archetype);
            draw_matched_mesh(archetype, colour,
                              pose_on(column, row, 0, 0, 0, 0, 0));
            const MeshPose flat_pose = pose_on(column + 1, row, 0, 0, 0, 0, 0);
            draw_billboard_at(flat_pose.x, flat_pose.y, flat_pose.z, archetype,
                              colour < 2 ? colour : 0);
        }
    }
}

// The fitted battle: sixteen units, each drawn as the mesh of its own
// archetype where one exists: the two archers as archers, the rest as
// knights, which is what §5.3's all-knight sixteen becomes once there is a
// second model to stand up.
void draw_fitted_battle() {
    transform_world(cached_vertex_count);
    refresh_packets();
    clear_screen();
    int from = 0;
    for (int step = 0; step < board_rows; ++step) {
        const int row = board_rows - 1 - step;
        submit_packet_range(from, plan_row_end[step]);
        from = plan_row_end[step];
        for (int i = 0; i < unit_count; ++i) {
            if (static_cast<int>(units[i].row) != row) continue;
            const int archetype =
                units[i].archetype == archer_archetype ? archer_archetype
                                                       : knight_archetype;
            select_model(archetype);
            draw_fitted_mesh(archetype, units[i].colour,
                             pose_on(units[i].column, row, 0, 0, 0, 0, 0));
        }
    }
}

// The matched battle, so the "one army" picture exists under the silhouette
// rule too.
void draw_matched_battle() {
    transform_world(cached_vertex_count);
    refresh_packets();
    clear_screen();
    int from = 0;
    for (int step = 0; step < board_rows; ++step) {
        const int row = board_rows - 1 - step;
        submit_packet_range(from, plan_row_end[step]);
        from = plan_row_end[step];
        for (int i = 0; i < unit_count; ++i) {
            if (static_cast<int>(units[i].row) != row) continue;
            const int archetype =
                units[i].archetype == archer_archetype ? archer_archetype
                                                       : knight_archetype;
            select_model(archetype);
            draw_matched_mesh(archetype, units[i].colour,
                              pose_on(units[i].column, row, 0, 0, 0, 0, 0));
        }
    }
}

// ---------------------------------------------------------------------------
// The face-list cache: §5.3's named remedy, built and measured
//
// §5.3 measured the sixteen-mesh frame at 20,575 µs and found the cost is not
// the triangles: it is 120 faces per unit per frame of corner-gathering and
// winding arithmetic, most of it thrown away. The remedy it named is the one
// the board already uses: cache what a settled unit does not change.
//
// What a settled unit under a settled camera does not change: its scale, its
// world vertices, its part order, its visible face set and its face colours.
// So the cache holds, per unit, the scaled world vertices and the surviving
// faces (corner indices and shaded colour) decided once by the same winding
// test the direct path runs. A cached frame then pays per unit: one RTPT pass
// over its vertices and one prepared submission per visible face. No winding,
// no per-face colour arithmetic, no model rebuild, no scale divide.
//
// What invalidates it, stated honestly: the *camera*. The fit ties a unit's
// scale to its depth, so a panning camera changes every unit's scale and the
// cache must be rebuilt. The build cost is measured below, and it is the §5.3
// frame back again for the frames a pan lasts. A tactics camera is still
// almost always still, and a still frame is what the cache buys.
// ---------------------------------------------------------------------------

struct UnitCache final {
    Vertex world[mesh_max_vertices];
    Projected screen[mesh_max_vertices];
    std::uint8_t corner[mesh_max_faces][4];
    std::uint32_t colour[mesh_max_faces];
    std::int16_t vertices;
    std::int16_t faces;
    // The two scales the entry was built at, so a camera move can be asked
    // whether it invalidated anything rather than assumed to have done.
    std::int16_t scale_w;
    std::int16_t scale_h;
};

UnitCache unit_cache[unit_count];

// Which sizing rule a cache entry is built under. §5.4's height fit is the one
// that section measured; §5.5's per-axis silhouette match is the rule this
// program draws under, and the one §5.5 asserted in prose that the cache "applies to
// unchanged". The two share every line below the sizing call, which is what
// makes that sentence checkable rather than a claim.
enum class UnitSizing { HeightFit, SilhouetteMatch };

void build_unit_cache(UnitSizing sizing) {
    for (int i = 0; i < unit_count; ++i) {
        const int archetype =
            units[i].archetype == archer_archetype ? archer_archetype
                                                   : knight_archetype;
        select_model(archetype);
        const MeshPose pose =
            pose_on(units[i].column, units[i].row, 0, 0, 0, 0, 0);
        if (sizing == UnitSizing::SilhouetteMatch) {
            const MatchedScales scales = build_matched(archetype, pose);
            unit_cache[i].scale_w = static_cast<std::int16_t>(scales.width);
            unit_cache[i].scale_h = static_cast<std::int16_t>(scales.height);
        } else {
            const std::int32_t scale = build_fitted(pose);
            unit_cache[i].scale_w = static_cast<std::int16_t>(scale);
            unit_cache[i].scale_h = static_cast<std::int16_t>(scale);
        }
        order_mesh_parts();

        UnitCache& cache = unit_cache[i];
        cache.vertices = static_cast<std::int16_t>(active_vertex_count);
        for (int v = 0; v < active_vertex_count; ++v) {
            cache.world[v] = mesh_world[v];
        }
        const MeshPalette& palette = mesh_palette[archetype][units[i].colour];
        int faces = 0;
        for (int slot = 0; slot < active_part_count; ++slot) {
            const int part = mesh_order[slot];
            for (int index = 0; index < mesh_faces_per_box; ++index) {
                const MeshFace& face =
                    mesh_faces[part * mesh_faces_per_box + index];
                const Projected corner[4] = {
                    mesh_screen[face.corner[0]],
                    mesh_screen[face.corner[1]],
                    mesh_screen[face.corner[2]],
                    mesh_screen[face.corner[3]],
                };
                if (!faces_the_viewer(corner)) continue;
                for (int c = 0; c < 4; ++c) {
                    cache.corner[faces][c] = face.corner[c];
                }
                cache.colour[faces] =
                    shade(palette.rung[face.ramp][face.rung],
                          static_cast<int>(face.light));
                ++faces;
            }
        }
        cache.faces = static_cast<std::int16_t>(faces);
        (void)tick();
    }
}

// What a cache entry *is*, small enough to keep one per unit per camera.
//
// The question mitigation B is priced against is "did this camera move
// invalidate anything", and a cache entry is invalidated by exactly two things:
// its scales changing (which changes every world vertex it holds) and its
// visible face list changing, which a translation of the camera can do, because
// a box carried off to the side of the frustum starts showing a face it was
// hiding. Both are folded in here: the two scales verbatim, the face count, and
// an FNV-1a-32 over the surviving faces' corner indices in the order they were
// decided. The colours are not folded in because they cannot move: a face's
// shade is its ramp, its rung and its normal, none of which the camera touches.
struct CacheSignature final {
    std::int16_t scale_w;
    std::int16_t scale_h;
    std::int16_t faces;
    std::uint32_t face_hash;
};

[[nodiscard]] CacheSignature signature_of(int i) noexcept {
    const UnitCache& cache = unit_cache[i];
    std::uint32_t hash = 2166136261u;
    for (int f = 0; f < cache.faces; ++f) {
        for (int c = 0; c < 4; ++c) {
            hash ^= cache.corner[f][c];
            hash *= 16777619u;
        }
    }
    return CacheSignature{cache.scale_w, cache.scale_h, cache.faces, hash};
}

int last_cached_faces = 0;

void submit_cached_unit(int i) {
    UnitCache& cache = unit_cache[i];
    const int vertices = cache.vertices;
    int v = 0;
    for (; v + 2 < vertices; v += 3) {
        transform3(cache.world[v], cache.world[v + 1], cache.world[v + 2],
                   &cache.screen[v]);
    }
    for (; v < vertices; ++v) {
        Projected out[3];
        transform3(cache.world[v], cache.world[v], cache.world[v], out);
        cache.screen[v] = out[0];
    }
    for (int f = 0; f < cache.faces; ++f) {
        const Projected corner[4] = {
            cache.screen[cache.corner[f][0]],
            cache.screen[cache.corner[f][1]],
            cache.screen[cache.corner[f][2]],
            cache.screen[cache.corner[f][3]],
        };
        draw_flat_quad(corner, cache.colour[f]);
        ++last_cached_faces;
    }
    (void)tick();
}

// The same battle frame as `draw_fitted_battle`, from the cache. The pixels
// must come out identical, checked by hash rather than assumed, while the
// per-frame work is the board's own discipline applied to the units.
void draw_cached_battle() {
    last_cached_faces = 0;
    transform_world(cached_vertex_count);
    refresh_packets();
    clear_screen();
    int from = 0;
    for (int step = 0; step < board_rows; ++step) {
        const int row = board_rows - 1 - step;
        submit_packet_range(from, plan_row_end[step]);
        from = plan_row_end[step];
        for (int i = 0; i < unit_count; ++i) {
            if (static_cast<int>(units[i].row) != row) continue;
            submit_cached_unit(i);
        }
    }
}

// ---------------------------------------------------------------------------
// The motions of slice one, applied to both
//
// The shared client's timing: six frames a tile along a route, and a six-frame
// flinch. Neither touches the projection, because `view::motion` produces a
// *position* and a 3D renderer projects it instead of scaling it. So the
// offsets below are the same offsets a flat client would apply, and both
// members of every pair get them.
//
// What only the mesh gets is a *turn*: it faces the way it is walking, and it
// leans back when it is hit. That is not a favour done to the mesh, it is the
// difference being measured. A billboard has no facing to turn and no axis to
// lean about; it can only slide.
// ---------------------------------------------------------------------------

constexpr int motion_frames_per_tile = 6;
constexpr int motion_flinch_frames = 6;

// The flinch: the shared client's six-frame nudge, in world units back along
// the facing, and the lean in degrees that goes with it.
constexpr std::int8_t flinch_nudge[motion_flinch_frames] = {6, 10, 8, 5, 3, 1};
constexpr std::int8_t flinch_lean[motion_flinch_frames] = {8, 14, 11, 7, 4, 1};
// A walk bob, one cycle a tile, so a mesh that is walking is not a mesh being
// slid along a rail.
constexpr std::int8_t walk_bob[motion_frames_per_tile] = {0, 2, 3, 2, 0, -1};

// The route the pair walks and the frames the flinch lands on. Three tiles out
// along +x, a settle, a flinch, a settle, three tiles back.
struct MotionState final {
    std::int32_t dx;
    std::int32_t dz;
    std::int32_t dy;
    int yaw;
    int lean;
};

// Two tiles out and two back, which is as far as the route can go without a
// pair leaving the board. The pair furthest right starts at columns 8 and 9 of
// twelve, and the point of the film is what a unit looks like moving on a
// board, not what one looks like standing in the backdrop.
constexpr int motion_route_tiles = 2;
constexpr int motion_film_frames = 54;

[[nodiscard]] MotionState motion_at(int frame) noexcept {
    constexpr int walk = motion_route_tiles * motion_frames_per_tile;
    MotionState state{0, 0, 0, 0, 0};
    // 0..5 rest, 6..17 two tiles right, 18..23 settle, 24..29 flinch,
    // 30..35 settle, 36..47 two tiles back, 48..53 settle.
    if (frame < 6) return state;
    if (frame < 6 + walk) {
        const int step = frame - 6;
        state.dx = static_cast<std::int32_t>(step) * tile_world /
                   motion_frames_per_tile;
        state.dy = walk_bob[step % motion_frames_per_tile];
        state.yaw = 90;
        return state;
    }
    state.dx = motion_route_tiles * tile_world;
    state.yaw = 90;
    if (frame < 12 + walk) return state;
    if (frame < 12 + walk + motion_flinch_frames) {
        const int step = frame - (12 + walk);
        state.dx += flinch_nudge[step];
        state.lean = flinch_lean[step];
        return state;
    }
    if (frame < 18 + walk + motion_flinch_frames) return state;
    if (frame < 18 + walk + motion_flinch_frames + walk) {
        const int step = frame - (18 + walk + motion_flinch_frames);
        state.dx = motion_route_tiles * tile_world -
                   static_cast<std::int32_t>(step) * tile_world /
                       motion_frames_per_tile;
        state.dy = walk_bob[step % motion_frames_per_tile];
        state.yaw = 270;
        return state;
    }
    state.dx = 0;
    state.yaw = 270;
    return state;
}

// One comparison frame: the cached board far row first, with each pair's two
// units drawn immediately after their own row's ground so that a nearer row
// still paints over them.
//
// The row direction here is **far first**, which is the direction §3.1's
// argument requires and the direction §5.1 found `draw_frame` has backwards.
// Nothing above is changed: that would move every figure §1 reports. Nothing
// new inherits it either.
void draw_comparison_frame(bool textured_mesh, const MotionState& motion,
                           bool moving) {
    transform_world(cached_vertex_count);
    refresh_packets();
    clear_screen();
    int from = 0;
    for (int step = 0; step < board_rows; ++step) {
        const int row = board_rows - 1 - step;
        submit_packet_range(from, plan_row_end[step]);
        from = plan_row_end[step];
        for (int pair = 0; pair < compare_pair_count; ++pair) {
            if (static_cast<int>(compare_pairs[pair].row) != row) continue;
            const int column = compare_pairs[pair].column;
            const int colour = compare_pairs[pair].colour;
            const MotionState applied = moving ? motion : MotionState{};
            const MeshPose mesh_pose =
                pose_on(column, row, applied.dx, applied.dz, applied.dy,
                        applied.yaw, applied.lean);
            build_mesh_world(mesh_pose);
            transform_mesh();
            draw_mesh(knight_archetype, colour, textured_mesh);
            const MeshPose flat_pose =
                pose_on(column + 1, row, applied.dx, applied.dz, applied.dy, 0,
                        0);
            draw_billboard_at(flat_pose.x, flat_pose.y, flat_pose.z,
                              knight_archetype, colour < 2 ? colour : 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 12. The themed, non-playable surround
//
// §5.2's other requirement. A panning camera shows ground the flat renderer
// never had to draw, and §5.1 measured that even *unpanned* it already does:
// the far row spans screen x 26 to 291 of 320, so fifty-four columns of nothing
// are visible before anything has moved. The requirement: the surround must
// be themed with the map (the ford's surround is riverbank, not void) while
// reading unmistakably as not-playable.
//
// **Themed with the map costs nothing, because the map already says it.**
// `kind_at` and `steps_at` clamp their arguments, so asking the board what lies
// at column -3 answers with column 0's terrain and column 0's elevation. The
// surround is therefore the map's own terrain kinds and the map's own relief
// continued outward, by construction and with no authored data at all: the
// water column that crosses the board leaves it as water, the mountains in the
// corners keep climbing, the forest in the south-east keeps going. There is no
// new art file here and no new palette entry, and there is no candidate below
// that needed one.
//
// One thing falls out of that and is worth stating because it cuts in the
// surround's favour before any treatment is chosen: the board's own outermost
// grid vertices stop collapsing to elevation zero. `vertex_steps` skips cells
// outside the board, so today a mountain on the edge of the map is drawn
// sloping down into nothing; with a surround it is drawn as a mountain that
// continues. The dressing fixes a geometry artefact the undressed board has.
//
// **Three candidates, all from shipped art, differing in what they spend.**
//
//   (a) `clut`    the same textures through a *second CLUT* per terrain kind
//                 per ring, desaturated toward its own luminance and darkened,
//                 four rings deep. Costs 40 of 1,024 CLUT slots, zero texture
//                 cells, and **nothing at all per frame**: a CLUT is named in
//                 the packet a quad was going to send anyway.
//   (b) `trim`    one ring only, at full colour, under a single
//                 semi-transparent band that frames the playable area. Costs
//                 one ring of ground and one overlay quad per cell of it.
//   (c) `fog`     all four rings at full colour, with ring *n* under *n*
//                 stacked semi-transparent quads in the backdrop colour, so the
//                 world dissolves at 50, 75, 87.5 and 93.75 per cent. Costs one
//                 overlay quad per ring per cell, which is where the price is.
//
// The backdrop for this phase is a plain dark slate rather than the magenta the
// rest of the program uses. The magenta exists so that off-board area is
// unmistakable in a measurement; here the off-board area is the subject, and a
// fog that dissolved into magenta would be a picture of the marker rather than
// of the candidate. `surround-none` is photographed against the same slate, so
// the four pictures differ only in the treatment.
// ---------------------------------------------------------------------------

constexpr int surround_rings = 4;
constexpr int field_columns = board_columns + 2 * surround_rings;
constexpr int field_rows = board_rows + 2 * surround_rings;

// The field's vertices, ground and skirt alternating, exactly as `build_world`
// arranges the board's, and cached the same way and for the same reason. §1.2
// found the hard way that generating a scene inside a timed region measures the
// generating and not the machine; a surround is a board and a board does not
// change between frames.
constexpr int field_vertex_count =
    (field_rows + 1) * (field_columns + 1) * 2;

Vertex field_world[field_vertex_count];
Projected field_projected[field_vertex_count];

[[nodiscard]] int field_ground_index(int fz, int fx) noexcept {
    return (fz * (field_columns + 1) + fx) * 2;
}

[[nodiscard]] int field_skirt_index(int fz, int fx) noexcept {
    return field_ground_index(fz, fx) + 1;
}

// The board's own two, with the "outside the board contributes nothing" rule
// replaced by "outside the board is the edge continued". That single difference
// is what makes the surround a continuation rather than a flat apron.
[[nodiscard]] int field_vertex_steps(int column_right,
                                     int row_below) noexcept {
    int highest = 0;
    for (int dz = -1; dz <= 0; ++dz) {
        for (int dx = -1; dx <= 0; ++dx) {
            const int steps = steps_at(column_right + dx, row_below + dz);
            if (steps > highest) highest = steps;
        }
    }
    return highest;
}

[[nodiscard]] int field_front_steps(int column_right, int row_below) noexcept {
    int highest = 0;
    for (int dx = -1; dx <= 0; ++dx) {
        const int steps = steps_at(column_right + dx, row_below);
        if (steps > highest) highest = steps;
    }
    return highest;
}

[[nodiscard]] Vertex field_vertex(int fx, int fz, bool front_height) noexcept {
    const std::int32_t x = -board_half_width +
                           static_cast<std::int32_t>(fx - surround_rings) *
                               tile_world;
    const std::int32_t z = -board_half_depth +
                           static_cast<std::int32_t>(fz - surround_rings) *
                               tile_world;
    const int column_right = fx - surround_rings;
    const int row_below = fz - surround_rings;
    const int steps = front_height ? field_front_steps(column_right, row_below)
                                   : field_vertex_steps(column_right,
                                                        row_below);
    return Vertex{static_cast<std::int16_t>(x),
                  static_cast<std::int16_t>(steps * step_world),
                  static_cast<std::int16_t>(z)};
}

// Once, when the map is loaded.
void build_field_world() {
    int count = 0;
    for (int fz = 0; fz <= field_rows; ++fz) {
        for (int fx = 0; fx <= field_columns; ++fx) {
            field_world[count++] = field_vertex(fx, fz, false);
            field_world[count++] = field_vertex(fx, fz, true);
        }
        (void)tick();
    }
}

// Every frame, and nothing else every frame.
// RTPT takes three at a time and the field's vertex count is a multiple of
// three, so there is no tail to write. Asserted rather than left to the reader:
// a ring count that broke it would otherwise leave the last two vertices of the
// field untransformed and the outermost corner drawn from stale data.
static_assert(field_vertex_count % 3 == 0,
              "the field's vertex count must be a multiple of three");

int transform_field() {
    for (int i = 0; i < field_vertex_count; i += 3) {
        transform3(field_world[i], field_world[i + 1], field_world[i + 2],
                   &field_projected[i]);
        if ((i & 0xFF) == 0) (void)tick();
    }
    (void)tick();
    return field_vertex_count;
}

// How far outside the board a field cell is; zero is playable.
[[nodiscard]] int ring_of(int fx, int fz) noexcept {
    const int column = fx - surround_rings;
    const int row = fz - surround_rings;
    int across = 0;
    if (column < 0) across = -column;
    if (column >= board_columns) across = column - board_columns + 1;
    int down = 0;
    if (row < 0) down = -row;
    if (row >= board_rows) down = row - board_rows + 1;
    return across > down ? across : down;
}

// How far each ring is pushed toward grey and toward black. The first ring is
// a deliberate *step* rather than the start of a gradient: the requirement is
// that the boundary read as a boundary, and a treatment that eases in over four
// rings dresses the world beautifully and never says where the board stops. The
// three after it recede, so the world still goes somewhere.
//
// Tuned against candidate (c), which fades toward the backdrop by halves (50,
// 75, 87.5, 93.75 per cent), so that the two candidates are compared at the
// same strength and the comparison is about *what* they spend rather than about
// which was pushed harder.
constexpr std::int32_t recede_brightness[surround_rings] = {160, 112, 74, 44};
constexpr std::int32_t recede_saturation[surround_rings] = {120, 76, 44, 20};

// Candidate (a)'s treatment of one palette entry, at one ring depth. Toward its
// own luminance, then down, both in eight-bit fixed point.
//
// The one trap: a CLUT word of zero is the hole the GPU skips, so an entry that
// darkens all the way to zero would turn opaque ground transparent. A source
// hole stays a hole and a darkened colour that lands on zero is given the
// black-with-the-mask-bit-set the hardware treats as opaque black on an opaque
// primitive.
[[nodiscard]] unsigned short recede(unsigned short word, int ring) noexcept {
    if (word == 0) return 0;
    const std::int32_t red = word & 0x1F;
    const std::int32_t green = (word >> 5) & 0x1F;
    const std::int32_t blue = (word >> 10) & 0x1F;
    const std::int32_t grey = (red * 2 + green * 5 + blue) / 8;
    const std::int32_t saturation = recede_saturation[ring - 1];
    const std::int32_t brightness = recede_brightness[ring - 1];
    const auto mix = [&](std::int32_t channel) {
        const std::int32_t toward =
            (channel * saturation + grey * (256 - saturation)) / 256;
        return (toward * brightness) / 256;
    };
    const std::uint32_t out =
        (static_cast<std::uint32_t>(mix(blue)) << 10) |
        (static_cast<std::uint32_t>(mix(green)) << 5) |
        static_cast<std::uint32_t>(mix(red));
    return out == 0 ? static_cast<unsigned short>(0x8000u)
                    : static_cast<unsigned short>(out);
}

int surround_clut_of[surround_rings][art::terrain_kind_count];
int surround_cluts_used = 0;

void upload_surround_cluts() {
    for (int ring = 1; ring <= surround_rings; ++ring) {
        for (int kind = 0; kind < art::terrain_kind_count; ++kind) {
            unsigned short entries[art::clut_size];
            const unsigned short* source = art::terrain(theme, kind, 0).clut;
            for (int i = 0; i < art::clut_size; ++i) {
                entries[i] = recede(source[i], ring);
            }
            surround_clut_of[ring - 1][kind] = claim_clut(entries);
            ++surround_cluts_used;
        }
    }
}

constexpr int surround_none = 0;
constexpr int surround_clut = 1;
constexpr int surround_trim = 2;
constexpr int surround_fog = 3;

// A dark slate, so a candidate can be judged rather than the marker it sits on.
constexpr std::uint32_t surround_backdrop = 0x00302018u;
// The trim band of candidate (b): one semi-transparent pass of a dark ink over
// the ring immediately outside the board.
constexpr std::uint32_t trim_ink = 0x00201418u;

int last_field_ground = 0;
int last_field_skirts = 0;
int last_field_overlays = 0;

// Which terrain a field cell is, which texture cell it takes, how far out it
// lies and whether it has a cliff face under it. All four are properties of the
// map and none of them changes when the camera moves, so they are decided once,
// the same correction §1.2 made to the board and for the same reason.
struct FieldPlan final {
    std::int16_t cell;
    std::uint8_t kind;
    std::uint8_t ring;
    bool skirt;
};

FieldPlan field_plan[field_rows][field_columns];

void build_field_plan() {
    for (int fz = 0; fz < field_rows; ++fz) {
        for (int fx = 0; fx < field_columns; ++fx) {
            const int column = fx - surround_rings;
            const int row = fz - surround_rings;
            const int kind = kind_at(column, row);
            // The variant is chosen from the *field* coordinate, so a surround
            // ring varies the way the board does rather than repeating one
            // tile all the way round. That is only free because the ring count
            // times four is a multiple of the variant count, which means a
            // board cell still gets the variant it gets today. Asserted
            // rather than trusted.
            const int variant = terrain_variant(fx, fz);
            FieldPlan& plan = field_plan[fz][fx];
            plan.kind = static_cast<std::uint8_t>(kind);
            plan.cell =
                static_cast<std::int16_t>(terrain_cell_of[kind][variant]);
            plan.ring = static_cast<std::uint8_t>(ring_of(fx, fz));
            plan.skirt = field_vertex_steps(column + 1, row + 1) >
                         field_front_steps(column + 1, row + 1);
        }
    }
}

// One frame of the extended field, far row first. The playable board is drawn
// exactly as `draw_frame` draws it: same corner order, same cells, same CLUTs.
// The only difference between these four pictures is what happens outside it.
void draw_field(int candidate, bool with_units) {
    int ground = 0;
    int skirts = 0;
    int overlays = 0;
    for (int step = 0; step < field_rows; ++step) {
        const int fz = field_rows - 1 - step;
        for (int fx = 0; fx < field_columns; ++fx) {
            const FieldPlan& plan = field_plan[fz][fx];
            const int ring = plan.ring;
            if (ring > 0) {
                if (candidate == surround_none) continue;
                if (candidate == surround_trim && ring > 1) continue;
            }
            const int clut = (ring > 0 && candidate == surround_clut)
                                 ? surround_clut_of[ring - 1][plan.kind]
                                 : terrain_clut_of[plan.kind];
            const Projected corner[4] = {
                field_projected[field_ground_index(fz, fx)],
                field_projected[field_ground_index(fz, fx + 1)],
                field_projected[field_ground_index(fz + 1, fx)],
                field_projected[field_ground_index(fz + 1, fx + 1)],
            };
            draw_textured_quad(corner, plan.cell, clut);
            ++ground;

            if (plan.skirt) {
                const Projected face[4] = {
                    field_projected[field_ground_index(fz + 1, fx)],
                    field_projected[field_ground_index(fz + 1, fx + 1)],
                    field_projected[field_skirt_index(fz + 1, fx)],
                    field_projected[field_skirt_index(fz + 1, fx + 1)],
                };
                draw_textured_quad(face, terrain_cell_of[plan.kind][0], clut);
                ++skirts;
            }

            // The overlays land immediately on top of the cell they treat,
            // which is correct without a second pass: a nearer row is painted
            // afterwards and can only cover things further away.
            if (ring > 0 && candidate == surround_trim) {
                draw_semi_quad(corner, trim_ink);
                ++overlays;
            }
            if (ring > 0 && candidate == surround_fog) {
                for (int pass = 0; pass < ring; ++pass) {
                    draw_semi_quad(corner, surround_backdrop);
                    ++overlays;
                }
            }
        }
        if (with_units) {
            const int row = fz - surround_rings;
            for (int i = 0; i < unit_count; ++i) {
                if (static_cast<int>(units[i].row) != row) continue;
                draw_textured_quad(
                    billboard_screen[i],
                    character_cell_of[units[i].archetype][units[i].colour],
                    character_clut_of[units[i].archetype][units[i].colour]);
            }
        }
        (void)tick();
    }
    last_field_ground = ground;
    last_field_skirts = skirts;
    last_field_overlays = overlays;
}

struct FieldCost final {
    std::uint32_t transform_us;
    std::uint32_t submit_us;
    std::uint32_t total_us;
    int primitives;
};

[[nodiscard]] FieldCost measure_field(int candidate) {
    psx::start_clock();
    const std::uint32_t t0 = tick();
    (void)transform_field();
    (void)transform_billboards();
    const std::uint32_t t1 = tick();
    clear_screen_with(surround_backdrop);
    draw_field(candidate, true);
    const std::uint32_t t2 = tick();
    FieldCost cost{};
    cost.transform_us = microseconds(t1 - t0);
    cost.submit_us = microseconds(t2 - t1);
    cost.total_us = microseconds(t2 - t0);
    cost.primitives = last_field_ground + last_field_skirts +
                      last_field_overlays + unit_count;
    return cost;
}

void report_field(const char* label, const FieldCost& cost) {
    Line()
        .text("SURROUND ")
        .text(label)
        .text(" ground ")
        .decimal(static_cast<std::uint32_t>(last_field_ground))
        .text(" skirts ")
        .decimal(static_cast<std::uint32_t>(last_field_skirts))
        .text(" overlays ")
        .decimal(static_cast<std::uint32_t>(last_field_overlays))
        .text(" prims ")
        .decimal(static_cast<std::uint32_t>(cost.primitives))
        .text(" tris ")
        .decimal(static_cast<std::uint32_t>(cost.primitives * 2))
        .flush();
    Line()
        .text("SURROUND ")
        .text(label)
        .text(" transform ")
        .decimal(cost.transform_us)
        .text(" us submit ")
        .decimal(cost.submit_us)
        .text(" us total ")
        .decimal(cost.total_us)
        .text(" us fps ")
        .decimal(cost.total_us == 0 ? 0u : 1000000u / cost.total_us)
        .flush();
}

}  // namespace

int main() {
    psx::start_clock();
    gpu::begin(0x7C1F);
    upload_art();

    Line().text("SCRATCH playstation gte board").flush();
    // Which style's figures produced everything below. Printed second, before
    // any number is, because every mesh measurement in this program is a claim
    // about eight *drawn* figures and there are seven sets of those. The name
    // comes out of the generated header rather than from a build definition, so
    // it cannot disagree with the bytes it is naming.
    Line()
        .text("STYLE ")
        .text(grandleon_playstation_mesh_style_name)
        .flush();
    Line()
        .text("SCENE board ")
        .decimal(board_columns)
        .text("x")
        .decimal(board_rows)
        .text(" tile ")
        .decimal(tile_world)
        .text(" step ")
        .decimal(step_world)
        .text(" pitch ")
        .decimal(camera_pitch_degrees)
        .text(" distance ")
        .decimal(camera_distance)
        .text(" focal ")
        .decimal(camera_focal)
        .flush();
    Line()
        .text("VRAM cells ")
        .decimal(static_cast<std::uint32_t>(cells_used))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(gpu::cell_capacity))
        .text(" cluts ")
        .decimal(static_cast<std::uint32_t>(cluts_used))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(gpu::clut_capacity))
        .flush();
    expect(cells_used > 0 && cluts_used > 0, "the art is resident in VRAM");

    load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                camera_vertical, Focus{0, 0});

    // -----------------------------------------------------------------------
    // 1. Does every vertex project at all? The divider overflows when a depth
    //    is not greater than H/2, and a projection that overflowed is a
    //    coordinate that means nothing. FLAG is the machine's own answer, and
    //    it is cleared before the pass so nothing earlier leaks into it.
    // -----------------------------------------------------------------------
    GTE_SET_CONTROL(31, 0);
    (void)transform_grid(1);
    (void)transform_billboards();
    std::int32_t flag = 0;
    GTE_GET_CONTROL(31, flag);
    expect((static_cast<std::uint32_t>(flag) & gte_flag_divide_overflow) == 0,
           "depth_never_overflows_the_divider");

    std::uint16_t nearest = 0xFFFF;
    std::uint16_t furthest = 0;
    for (int gz = 0; gz <= board_rows; ++gz) {
        for (int gx = 0; gx <= board_columns; ++gx) {
            const std::uint16_t depth = grid[gz][gx].depth;
            if (depth < nearest) nearest = depth;
            if (depth > furthest) furthest = depth;
        }
    }
    Line()
        .text("DEPTH nearest ")
        .decimal(nearest)
        .text(" furthest ")
        .decimal(furthest)
        .text(" divider floor ")
        .decimal(static_cast<std::uint32_t>(camera_focal / 2))
        .flush();
    expect(nearest > static_cast<std::uint16_t>(camera_focal / 2),
           "every_depth_clears_the_divider_floor");

    // Where the board actually landed, so the framing in the comments above is
    // checked rather than claimed.
    Line()
        .text("FRAME near edge x ")
        .signed_decimal(grid[0][0].x)
        .text(" to ")
        .signed_decimal(grid[0][board_columns].x)
        .text(" at y ")
        .signed_decimal(grid[0][0].y)
        .text("; far edge x ")
        .signed_decimal(grid[board_rows][0].x)
        .text(" to ")
        .signed_decimal(grid[board_rows][board_columns].x)
        .text(" at y ")
        .signed_decimal(grid[board_rows][0].y)
        .flush();

    // -----------------------------------------------------------------------
    // 2. Is the hardware's answer reproducible in portable integer arithmetic?
    // -----------------------------------------------------------------------
    int compared = 0;
    int exact_plain = 0;
    int worst_plain = 0;
    int exact_unr = 0;
    int worst_unr = 0;
    for (int gz = 0; gz <= board_rows; ++gz) {
        for (int gx = 0; gx <= board_columns; ++gx) {
            const Projected measured = grid[gz][gx];
            const Vertex source = grid_vertex(gx, gz, 1, false);
            ++compared;
            for (int model = 0; model < 2; ++model) {
                const Projected predicted = project_portably(
                    source, camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0}, model == 1
                );
                int dx = measured.x - predicted.x;
                int dy = measured.y - predicted.y;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                const int deviation = dx > dy ? dx : dy;
                if (model == 0) {
                    if (deviation > worst_plain) worst_plain = deviation;
                    if (deviation == 0) ++exact_plain;
                } else {
                    if (deviation > worst_unr) worst_unr = deviation;
                    if (deviation == 0) ++exact_unr;
                }
            }
        }
    }
    Line()
        .text("PREDICT vertices ")
        .decimal(static_cast<std::uint32_t>(compared))
        .text("; plain division exact ")
        .decimal(static_cast<std::uint32_t>(exact_plain))
        .text(" worst ")
        .decimal(static_cast<std::uint32_t>(worst_plain))
        .text("; hardware divider exact ")
        .decimal(static_cast<std::uint32_t>(exact_unr))
        .text(" worst ")
        .decimal(static_cast<std::uint32_t>(worst_unr))
        .flush();
    expect(exact_unr == compared,
           "the_hardware_divider_model_predicts_every_vertex_exactly");

    // -----------------------------------------------------------------------
    // 4. The baseline frame: the board, its cliffs and sixteen units.
    // -----------------------------------------------------------------------
    const FrameCost baseline = measure_frame(1, true, true, 1);
    report_frame("baseline", baseline);
    expect(baseline.ground == board_columns * board_rows,
           "the_baseline_draws_every_cell");
    expect(baseline.units == unit_count, "the_baseline_draws_every_unit");
    expect(baseline.skirts > 0, "the_baseline_draws_cliff_faces");
    expect(baseline.total_us < thirty_fps_microseconds,
           "the_baseline_frame_fits_in_a_thirtieth_of_a_second");
    if (baseline.total_us < sixty_fps_microseconds) {
        Line().text("NOTE the baseline frame also fits in a sixtieth").flush();
    }

    // The billboard's drawn size, which is the number the art direction cares
    // about: a 32x32 cell drawn into fewer than 32 pixels is losing texel
    // columns, and losing texel columns is what breaks a silhouette.
    {
        int widest = 0;
        int narrowest = 0x7FFF;
        int tallest = 0;
        int shortest = 0x7FFF;
        for (int i = 0; i < unit_count; ++i) {
            int width = billboard_screen[i][1].x - billboard_screen[i][0].x;
            int height = billboard_screen[i][2].y - billboard_screen[i][0].y;
            if (width < 0) width = -width;
            if (height < 0) height = -height;
            if (width > widest) widest = width;
            if (width < narrowest) narrowest = width;
            if (height > tallest) tallest = height;
            if (height < shortest) shortest = height;
        }
        Line()
            .text("BILLBOARD width ")
            .decimal(static_cast<std::uint32_t>(narrowest))
            .text(" to ")
            .decimal(static_cast<std::uint32_t>(widest))
            .text(" height ")
            .decimal(static_cast<std::uint32_t>(shortest))
            .text(" to ")
            .decimal(static_cast<std::uint32_t>(tallest))
            .text(" px for a 32x32 cell")
            .flush();
    }

    // -----------------------------------------------------------------------
    // 5. What does this emulator charge for pixels? The same primitives drawn
    //    at a quarter of the size, and then with the texture unit taken away.
    //    If the times agree, the rasteriser is free here and every number in
    //    this report is a CPU-and-GTE number with no fill rate in it at all.
    //    That is the caveat the document has to carry, measured rather than
    //    supposed.
    // -----------------------------------------------------------------------
    const FrameCost shrunk = measure_frame(1, true, true, 4);
    report_frame("quarter-size", shrunk);
    const FrameCost flat = measure_frame(1, false, true, 1);
    report_frame("flat-shaded", flat);

    // -----------------------------------------------------------------------
    // 6. The ladder. The same board and the same screen coverage, subdivided
    //    further each time, so triangle count rises and filled area does not.
    //    That isolates per-primitive cost, which is what a 3D renderer runs out
    //    of first on a machine whose GPU outruns its bus.
    // -----------------------------------------------------------------------
    Line().text("LADDER subdividing the same board").flush();
    int last_fitting = 0;
    int first_failing = 0;
    int last_fitting_cached = 0;
    int first_failing_cached = 0;
    for (int factor = 1; factor <= max_factor; ++factor) {
        // The frame as written is only run for the first few rungs: it is the
        // slowest path by far, and it demonstrates at any size that generating
        // the scene every frame dominates.
        if (factor <= 4) {
            const FrameCost cost = measure_frame(factor, true, true, 1);
            Line label;
            label.text("ladder-x").decimal(static_cast<std::uint32_t>(factor));
            report_frame(label.c_str(), cost);
            const int triangles = triangles_of(cost);
            if (cost.total_us <= thirty_fps_microseconds) {
                if (triangles > last_fitting) last_fitting = triangles;
            } else if (first_failing == 0) {
                first_failing = triangles;
            }
        } else {
            // The split path reads `grid` and `skirt_grid`, so they have to
            // hold this configuration even when the frame above is skipped.
            (void)transform_grid(factor);
            (void)transform_billboards();
        }

        // The same configuration with the three costs separated, so the
        // coprocessor and the bus can be read apart from the arithmetic that
        // decides what to send them.
        psx::start_clock();
        const std::uint32_t b0 = tick();
        const int vertices = build_world(factor);
        const std::uint32_t b1 = tick();
        transform_world(vertices);
        const std::uint32_t b2 = tick();
        const int primitives = prepare_packets(factor);
        const std::uint32_t b3 = tick();
        submit_packets(primitives);
        const std::uint32_t b4 = tick();
        const std::uint32_t build_us = microseconds(b1 - b0);
        const std::uint32_t transform_us = microseconds(b2 - b1);
        const std::uint32_t prepare_us = microseconds(b3 - b2);
        const std::uint32_t submit_us = microseconds(b4 - b3);
        Line()
            .text("SPLIT x")
            .decimal(static_cast<std::uint32_t>(factor))
            .text(" verts ")
            .decimal(static_cast<std::uint32_t>(vertices))
            .text(" prims ")
            .decimal(static_cast<std::uint32_t>(primitives))
            .text(" build ")
            .decimal(build_us)
            .text(" transform ")
            .decimal(transform_us)
            .text(" prepare ")
            .decimal(prepare_us)
            .text(" submit ")
            .decimal(submit_us)
            .text(" us")
            .flush();
        Line()
            .text("SPLIT x")
            .decimal(static_cast<std::uint32_t>(factor))
            .text(" per 1000 verts transform ")
            .decimal(transform_us * 1000u /
                     static_cast<std::uint32_t>(vertices))
            .text(" us; per 1000 prims submit ")
            .decimal(submit_us * 1000u /
                     static_cast<std::uint32_t>(primitives))
            .text(" us")
            .flush();

        // What a renderer that built its board once would pay each frame.
        const std::uint32_t cached_us = transform_us + submit_us;
        const int cached_triangles = primitives * 2;
        Line()
            .text("CACHED x")
            .decimal(static_cast<std::uint32_t>(factor))
            .text(" tris ")
            .decimal(static_cast<std::uint32_t>(cached_triangles))
            .text(" transform plus submit ")
            .decimal(cached_us)
            .text(" us fps ")
            .decimal(cached_us == 0 ? 0u : 1000000u / cached_us)
            .flush();
        if (cached_us <= thirty_fps_microseconds) {
            if (cached_triangles > last_fitting_cached) {
                last_fitting_cached = cached_triangles;
            }
        } else if (first_failing_cached == 0) {
            first_failing_cached = cached_triangles;
        }
    }
    Line()
        .text("BREAK as written: largest triangle count inside 30 fps ")
        .decimal(static_cast<std::uint32_t>(last_fitting))
        .text(" first outside ")
        .decimal(static_cast<std::uint32_t>(first_failing))
        .flush();
    Line()
        .text("BREAK board cached: largest triangle count inside 30 fps ")
        .decimal(static_cast<std::uint32_t>(last_fitting_cached))
        .text(" first outside ")
        .decimal(static_cast<std::uint32_t>(first_failing_cached))
        .flush();

    // -----------------------------------------------------------------------
    // 7. Determinism.
    // -----------------------------------------------------------------------
    gpu::begin(0x7C1F);
    (void)measure_frame(1, true, true, 1);
    const std::uint64_t first_hash = hash_framebuffer();
    gpu::begin(0x7C1F);
    (void)measure_frame(1, true, true, 1);
    const std::uint64_t second_hash = hash_framebuffer();
    Line().text("HASH first ").hex64(first_hash).flush();
    Line().text("HASH second ").hex64(second_hash).flush();
    expect(first_hash == second_hash, "two_runs_hash_the_same_framebuffer");
    expect(first_hash != 0xcbf29ce484222325ull, "the_framebuffer_was_read");

    // The negative control: move the camera one world unit and the hash must
    // change, or the hash is not measuring the picture.
    gpu::begin(0x7C1F);
    load_camera(camera_pitch_degrees, camera_distance + 1, camera_focal,
                camera_vertical, Focus{0, 0});
    (void)measure_frame(1, true, true, 1);
    const std::uint64_t moved_hash = hash_framebuffer();
    Line().text("HASH moved ").hex64(moved_hash).flush();
    expect(moved_hash != first_hash, "a_moved_camera_hashes_differently");

    // -----------------------------------------------------------------------
    // 8. The pitch sweep: what a 32x32 sprite is drawn at, angle by angle. The
    //    art direction is silhouette-first and the sprites are 32 texels
    //    square, so the question a camera model has to answer is which pitches
    //    keep a unit near 32 pixels and keep the board on the display.
    // -----------------------------------------------------------------------
    // Row zero of the grid is the *near* edge: its world Z is negative, and
    // depth is TRZ + cos(pitch) * Z, so a negative Z is closer to the eye.
    // Reading that the other way round is the easiest mistake to make here and
    // the labels below are written the way the arithmetic says, not the way the
    // array index reads.
    int sweep_compared = 0;
    int sweep_exact = 0;
    int sweep_worst = 0;
    int sweep_exact_plain = 0;
    int sweep_worst_plain = 0;
    for (int pitch = 30; pitch <= 90; pitch += 10) {
        load_camera(pitch, camera_distance, camera_focal, camera_vertical,
                    Focus{0, 0});
        (void)transform_grid(1);
        (void)transform_billboards();

        // The prediction check again, at this pitch, under both models. One
        // camera agreeing is a coincidence; seven is a property.
        for (int gz = 0; gz <= board_rows; ++gz) {
            for (int gx = 0; gx <= board_columns; ++gx) {
                const Projected measured = grid[gz][gx];
                const Vertex source = grid_vertex(gx, gz, 1, false);
                ++sweep_compared;
                for (int model = 0; model < 2; ++model) {
                    const Projected predicted = project_portably(
                        source, pitch, camera_distance, camera_focal,
                        camera_vertical, Focus{0, 0}, model == 1
                    );
                    int dx = measured.x - predicted.x;
                    int dy = measured.y - predicted.y;
                    if (dx < 0) dx = -dx;
                    if (dy < 0) dy = -dy;
                    const int deviation = dx > dy ? dx : dy;
                    if (model == 0) {
                        if (deviation > sweep_worst_plain) {
                            sweep_worst_plain = deviation;
                        }
                        if (deviation == 0) ++sweep_exact_plain;
                    } else {
                        if (deviation > sweep_worst) sweep_worst = deviation;
                        if (deviation == 0) ++sweep_exact;
                    }
                }
            }
        }

        int tallest = 0;
        int shortest = 0x7FFF;
        for (int i = 0; i < unit_count; ++i) {
            int height = billboard_screen[i][2].y - billboard_screen[i][0].y;
            if (height < 0) height = -height;
            if (height > tallest) tallest = height;
            if (height < shortest) shortest = height;
        }
        // The same units from a quad pre-stretched in the world by 1/cos(pitch)
        // so the sprite lands square. It does not work, and the numbers say why
        // rather than an argument having to: the stretched quad's top is much
        // nearer the eye than its foot, so perspective magnifies what the
        // stretch was meant to correct and the answer runs away with the pitch.
        int upright_shortest = 0x7FFF;
        int upright_tallest = 0;
        for (int i = 0; i < unit_count; ++i) {
            Vertex corner[4];
            billboard_corners_upright(units[i], pitch, corner);
            Projected out[3];
            transform3(corner[0], corner[2], corner[0], out);
            int height = out[1].y - out[0].y;
            if (height < 0) height = -height;
            if (height > upright_tallest) upright_tallest = height;
            if (height < upright_shortest) upright_shortest = height;
        }
        // And the same units as *screen-space* billboards: the foot is
        // projected and the sprite is then a square of side focal*size/depth,
        // which is one divide a unit and is independent of the pitch by
        // construction. This is the one that works, and it is what §3
        // recommends.
        int screen_shortest = 0x7FFF;
        int screen_tallest = 0;
        for (int i = 0; i < unit_count; ++i) {
            const std::uint16_t depth = billboard_screen[i][2].depth;
            if (depth == 0) continue;
            const int side = camera_focal * unit_world / depth;
            if (side > screen_tallest) screen_tallest = side;
            if (side < screen_shortest) screen_shortest = side;
        }
        const int near_row = grid[0][board_columns / 2].y;
        const int far_row = grid[board_rows][board_columns / 2].y;
        const int near_tile = grid[0][1].x - grid[0][0].x;
        const int far_tile = grid[board_rows][1].x - grid[board_rows][0].x;
        Line()
            .text("PITCH ")
            .decimal(static_cast<std::uint32_t>(pitch))
            .text(" unit height ")
            .decimal(static_cast<std::uint32_t>(shortest))
            .text("-")
            .decimal(static_cast<std::uint32_t>(tallest))
            .text(" px, world-stretched ")
            .decimal(static_cast<std::uint32_t>(upright_shortest))
            .text("-")
            .decimal(static_cast<std::uint32_t>(upright_tallest))
            .text(" px, screen-space ")
            .decimal(static_cast<std::uint32_t>(screen_shortest))
            .text("-")
            .decimal(static_cast<std::uint32_t>(screen_tallest))
            .text(" px, tile width near ")
            .signed_decimal(near_tile)
            .text(" far ")
            .signed_decimal(far_tile)
            .text(", rows span y near ")
            .signed_decimal(near_row)
            .text(" far ")
            .signed_decimal(far_row)
            .flush();
    }
    Line()
        .text("PREDICT sweep vertices ")
        .decimal(static_cast<std::uint32_t>(sweep_compared))
        .text("; plain division exact ")
        .decimal(static_cast<std::uint32_t>(sweep_exact_plain))
        .text(" worst ")
        .decimal(static_cast<std::uint32_t>(sweep_worst_plain))
        .text("; hardware divider exact ")
        .decimal(static_cast<std::uint32_t>(sweep_exact))
        .text(" worst ")
        .decimal(static_cast<std::uint32_t>(sweep_worst))
        .flush();
    expect(sweep_exact == sweep_compared,
           "every_camera_in_the_sweep_is_predicted_exactly");
    load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                camera_vertical, Focus{0, 0});

    // -----------------------------------------------------------------------
    // 9. The pan.
    //
    //    §1.4's central claim does not hold: a far cell is only
    //    permanently smaller if the camera is permanently still. Everything
    //    from here down exists to answer that with numbers and pictures rather
    //    than with an opinion, and it is arranged so that a reader can disagree
    //    with any one part without having to take the rest on trust.
    // -----------------------------------------------------------------------

    // 9a. The board, cached once, and proved cached rather than assumed. The
    //     cached path indexes `projected_vertices` by an arithmetic mapping
    //     onto `build_world`'s output order, and an off-by-one there would draw
    //     a subtly wrong board that still looked like a board. So it is checked
    //     the only way that cannot be fooled: both paths draw the same camera
    //     and the two framebuffers must hash the same.
    cached_vertex_count = build_world(1);
    packet_plan_count = plan_packets(false, true);
    Line()
        .text("CACHE vertices ")
        .decimal(static_cast<std::uint32_t>(cached_vertex_count))
        .text(" packets ")
        .decimal(static_cast<std::uint32_t>(packet_plan_count))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(pan_plan_capacity))
        .flush();

    gpu::begin(0x7C1F);
    (void)measure_frame(1, true, true, 1);
    const std::uint64_t written_hash = hash_framebuffer();
    gpu::begin(0x7C1F);
    transform_world(cached_vertex_count);
    refresh_packets();
    clear_screen();
    submit_packets(packet_plan_count);
    const std::uint64_t cached_hash = hash_framebuffer();
    Line().text("HASH as written ").hex64(written_hash).flush();
    Line().text("HASH cached ").hex64(cached_hash).flush();
    expect(cached_hash == written_hash,
           "the_cached_board_draws_the_same_frame_as_the_written_one");

    // And the ordering question, asked of the machine rather than of the
    // comments: the same board, the same camera, the rows painted the other way
    // round. If the two hashes agree, no cell overlaps another on this board
    // and the direction is free; if they disagree, one of the two orders is
    // wrong and §3.1's "the board is its own ordering table" has a direction
    // that has to be got right.
    packet_plan_count = plan_packets(true, true);
    gpu::begin(0x7C1F);
    transform_world(cached_vertex_count);
    refresh_packets();
    clear_screen();
    submit_packets(packet_plan_count);
    const std::uint64_t far_first_hash = hash_framebuffer();
    packet_plan_count = plan_packets(false, true);
    Line().text("HASH far row first ").hex64(far_first_hash).flush();
    Line()
        .text("ORDER the two row directions ")
        .text(far_first_hash == cached_hash ? "agree: nothing on this board "
                                              "overlaps and the direction is "
                                              "free"
                                            : "DISAGREE: cells overlap, so one "
                                              "of the two is wrong")
        .flush();

    // 9b. The pad. Polled every frame of every run below, inside the timed
    //     region, and folded into the script, so a person with a controller
    //     steers this and no measurement depends on whether one is plugged in.
    {
        psx::start_clock();
        const std::uint32_t p0 = tick();
        const PadReading reading = poll_pad();
        const std::uint32_t p1 = tick();
        Line()
            .text("PAD identifier ")
            .decimal(reading.identifier)
            .text(reading.answered ? " (digital pad answered)"
                                   : " (no pad answered)")
            .text(" buttons ")
            .decimal(reading.buttons)
            .text(" poll ")
            .decimal(microseconds(p1 - p0))
            .text(" us")
            .flush();
        Line()
            .text("PAD acknowledgement seen on ")
            .decimal(static_cast<std::uint32_t>(joy_acknowledgements))
            .text(" of ")
            .decimal(static_cast<std::uint32_t>(joy_acknowledgements_waited))
            .text(" waits, budget ")
            .decimal(static_cast<std::uint32_t>(joy_ack_spins))
            .text(" spins")
            .flush();
        expect(reading.answered, "a_controller_answers_on_port_one");
    }

    // 9c. What the clamp can and cannot reach.
    //
    //     The two-dimensional precedent is `view::Camera::clamp`: the camera
    //     never shows past the edge of the board. Transliterated, that clamps
    //     the focus to the board's own extent. The question is what that leaves
    //     the far row drawn at, and the arithmetic says exactly what to expect:
    //     a far-row depth is TRZ + cos(phi)*half_depth and a near-row depth is
    //     TRZ - cos(phi)*half_depth, and TRZ falls by cos(phi) per unit of
    //     focus, so bringing the far row to the depth the near row sits at
    //     needs a focus of *two* half-depths (one board deep), which is
    //     exactly one half-board past the edge the precedent forbids.
    constexpr std::int32_t reach_to_equalise = board_half_depth;
    {
        const RowMetrics rest = measure_rows(Focus{0, 0});
        report_rows("rest", Focus{0, 0}, rest);
        const Focus tight{0, board_half_depth};
        const RowMetrics clamped = measure_rows(tight);
        report_rows("board-clamped", tight, clamped);
        const Focus beyond{0, board_half_depth + reach_to_equalise};
        const RowMetrics reached = measure_rows(beyond);
        report_rows("one-board-reach", beyond, reached);

        Line()
            .text("CLAMP far row tile width at rest ")
            .signed_decimal(rest.far_tile)
            .text(" px, board-clamped ")
            .signed_decimal(clamped.far_tile)
            .text(" px, one-board-reach ")
            .signed_decimal(reached.far_tile)
            .text(" px; the near row at rest is ")
            .signed_decimal(rest.near_tile)
            .text(" px")
            .flush();
        expect(clamped.far_tile > rest.far_tile,
               "panning_within_the_board_draws_the_far_row_larger");
        expect(reached.far_tile >= rest.near_tile,
               "one_board_of_reach_draws_the_far_row_at_the_near_rows_size");
        // And the finding that costs the precedent its guarantee: even
        // unpanned, the far row does not span the display. A tilted camera's
        // footprint is a trapezoid, so "the camera never shows past the edge"
        // is not a property this projection has to lose. It never had it.
        expect(rest.far_left > 0 || rest.far_right < gpu::screen_width - 1,
               "the_far_row_already_leaves_off_board_pixels_at_rest");
    }

    // 9d. Where the camera actually goes, per follow margin, and how long it
    //     takes to start going there. The margin is `view::Camera::follow`'s
    //     dead zone, which the shipped Nintendo 64 client sets to 2.
    //     In two dimensions a dead zone costs nothing a player can feel,
    //     because the camera moves in whole cells and arrives instantly. Here
    //     it is measured, because it is latency.
    //     The free-pan idiom goes through the same loop for comparison; it has
    //     no dead zone to have a margin for, so it is the pass below zero and
    //     the line it prints says "free pan" rather than a margin.
    for (int margin = follow_margin; margin >= -1; --margin) {
        const PanIdiom idiom =
            margin < 0 ? PanIdiom::free_pan : PanIdiom::cursor_follow;
        Pan dry = pan_at_rest(idiom, margin, reach_to_equalise);
        int first_input = -1;
        int first_move = -1;
        std::int32_t furthest_z = 0;
        for (int frame = 0; frame < pan_frames; ++frame) {
            const std::uint16_t buttons = scripted_buttons(frame);
            if (buttons != 0 && first_input < 0) first_input = frame;
            const Focus before = dry.focus;
            pan_step(dry, buttons, idiom, margin, reach_to_equalise);
            if (first_move < 0 &&
                (dry.focus.x != before.x || dry.focus.z != before.z)) {
                first_move = frame;
            }
            const std::int32_t magnitude =
                dry.focus.z < 0 ? -dry.focus.z : dry.focus.z;
            if (magnitude > furthest_z) furthest_z = magnitude;
        }
        Line()
            .text("LATENCY ")
            .text(margin < 0 ? "free pan" : "cursor follow margin")
            .text(" ")
            .signed_decimal(margin < 0 ? 0 : margin)
            .text(": first input frame ")
            .signed_decimal(first_input)
            .text(", first camera move frame ")
            .signed_decimal(first_move)
            .text(", delay ")
            .signed_decimal(first_move - first_input)
            .text(" frames; furthest focus z ")
            .signed_decimal(static_cast<int>(furthest_z))
            .flush();
    }

    // 9e. The frame, still and moving, measured by the same code. The still run
    //     is the same loop with the script silenced, so the comparison is not
    //     against a number from another phase measured another way.
    Line()
        .text("PAN rate ")
        .decimal(static_cast<std::uint32_t>(pan_units_per_frame))
        .text(" world units a frame, a quarter tile, ")
        .decimal(30u * static_cast<std::uint32_t>(pan_units_per_frame) * 10u /
                 static_cast<std::uint32_t>(tile_world))
        .text(" tenths of a cell a second at 30 fps; cursor repeat ")
        .decimal(static_cast<std::uint32_t>(cursor_repeat_frames))
        .text(" frames, follow margin ")
        .decimal(static_cast<std::uint32_t>(follow_margin))
        .flush();

    std::uint64_t first_pan_hash[checkpoint_count] = {};
    std::uint64_t second_pan_hash[checkpoint_count] = {};
    std::uint64_t free_pan_hash[checkpoint_count] = {};

    constexpr PanSettings still_settings{PanIdiom::cursor_follow, follow_margin,
                                         0, false};
    constexpr PanSettings follow_settings{PanIdiom::cursor_follow,
                                          follow_margin, 0, true};
    constexpr PanSettings free_settings{PanIdiom::free_pan, follow_margin,
                                        reach_to_equalise, true};

    gpu::begin(0x7C1F);
    gpu::show();
    const PanRun still = run_pan(still_settings, nullptr, 1, 0);
    report_pan("still", still);
    const PanRun moving = run_pan(follow_settings, first_pan_hash, 1, 0);
    report_pan("moving", moving);
    expect(moving.longest_us < thirty_fps_microseconds,
           "every_frame_of_the_pan_fits_in_a_thirtieth_of_a_second");
    expect(moving.longest_us < sixty_fps_microseconds,
           "every_frame_of_the_pan_fits_in_a_sixtieth_of_a_second");
    expect(moving.furthest_z > 0 || moving.furthest_x > 0,
           "the_scripted_pan_actually_moved_the_camera");
    expect(still.furthest_z == 0 && still.furthest_x == 0,
           "the_still_run_never_moved_the_camera");
    expect(moving.first_move_frame >= moving.first_input_frame,
           "the_camera_never_moves_before_the_pad_does");
    // The ceiling the cursor imposes, and it is the finding 9c predicted: a
    // focus that follows a cursor confined to the board is itself confined to
    // the board, and more tightly: the dead zone keeps it a further `margin`
    // cells inside. The board clamp of the two-dimensional precedent is
    // therefore never even reached, and a wider clamp buys a cursor-following
    // camera nothing at all.
    Line()
        .text("PAN cursor-follow ceiling: furthest focus z ")
        .signed_decimal(static_cast<int>(moving.furthest_z))
        .text(" of a board half-depth of ")
        .signed_decimal(static_cast<int>(board_half_depth))
        .text(" and the ")
        .signed_decimal(static_cast<int>(board_half_depth + reach_to_equalise))
        .text(" a far row needs to reach the near row's size")
        .flush();
    expect(moving.furthest_z < board_half_depth,
           "a_cursor_following_camera_never_reaches_the_board_clamp");

    // And what that ceiling is worth in pixels, which is the number the whole
    // question reduces to: how big the far row gets when the shipped idiom pans
    // as far as it can.
    const Focus ceiling{0, moving.furthest_z};
    const RowMetrics ceiling_rows = measure_rows(ceiling);
    report_rows("cursor-follow ceiling", ceiling, ceiling_rows);
    Line()
        .text("PAN latency input at frame ")
        .signed_decimal(moving.first_input_frame)
        .text(", camera moved at frame ")
        .signed_decimal(moving.first_move_frame)
        .text(", delay ")
        .signed_decimal(moving.first_move_frame - moving.first_input_frame)
        .text(" frames; pad answered on ")
        .decimal(static_cast<std::uint32_t>(moving.pad_answers))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(moving.frames))
        .flush();

    // 9f. Determinism under motion. Eleven checkpoints, five of them mid-pan.
    const PanRun repeat = run_pan(follow_settings, second_pan_hash, 1, 0);
    (void)repeat;
    int matching = 0;
    for (int i = 0; i < checkpoint_count; ++i) {
        if (first_pan_hash[i] == second_pan_hash[i]) ++matching;
        Line()
            .text("HASH pan frame ")
            .decimal(static_cast<std::uint32_t>(checkpoint_frames[i]))
            .text(" ")
            .hex64(first_pan_hash[i])
            .text(" ")
            .hex64(second_pan_hash[i])
            .text(first_pan_hash[i] == second_pan_hash[i] ? " same"
                                                          : " DIFFERENT")
            .flush();
    }
    expect(matching == checkpoint_count,
           "two_runs_of_the_same_pad_script_hash_the_same_at_every_checkpoint");
    int distinct = 0;
    for (int i = 0; i < checkpoint_count; ++i) {
        bool seen = false;
        for (int j = 0; j < i; ++j) {
            if (first_pan_hash[j] == first_pan_hash[i]) seen = true;
        }
        if (!seen) ++distinct;
    }
    Line()
        .text("HASH the pan visits ")
        .decimal(static_cast<std::uint32_t>(distinct))
        .text(" distinct pictures across ")
        .decimal(static_cast<std::uint32_t>(checkpoint_count))
        .text(" checkpoints")
        .flush();
    expect(distinct * 2 > checkpoint_count,
           "most_of_the_checkpoints_are_different_pictures");

    // The negative control the determinism claim needs: an identical hash at
    // every checkpoint proves nothing unless a camera that moved differently
    // hashes differently. The free-pan run is the same script, the same
    // frames, the same board and a different camera.
    const PanRun freed = run_pan(free_settings, free_pan_hash, 1, 0);
    report_pan("free-pan", freed);
    int differing = 0;
    for (int i = 0; i < checkpoint_count; ++i) {
        if (free_pan_hash[i] != first_pan_hash[i]) ++differing;
    }
    Line()
        .text("HASH free panning differs from cursor following at ")
        .decimal(static_cast<std::uint32_t>(differing))
        .text(" of ")
        .decimal(static_cast<std::uint32_t>(checkpoint_count))
        .text(" checkpoints; furthest focus z ")
        .signed_decimal(static_cast<int>(freed.furthest_z))
        .flush();
    expect(differing > 0, "a_differently_steered_camera_films_a_different_pan");
    expect(freed.furthest_z >= board_half_depth + reach_to_equalise,
           "a_free_pan_reaches_the_offset_the_far_row_needs");

    // 9g. The divider model at translated cameras. §4's whole proposal is that
    //     a host program can predict a console frame; the sweep proved that
    //     over seven pitches at one position, and a camera that moves is a
    //     camera at positions the sweep never visited.
    int pan_compared = 0;
    int pan_exact = 0;
    int pan_worst = 0;
    int pan_exact_plain = 0;
    int pan_worst_plain = 0;
    int pan_overflows = 0;
    for (int ix = -2; ix <= 2; ++ix) {
        for (int iz = -2; iz <= 2; ++iz) {
            const Focus focus{board_half_width * ix / 2,
                              board_half_depth * iz / 2};
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, focus);
            GTE_SET_CONTROL(31, 0);
            (void)transform_grid(1);
            std::int32_t pan_flag = 0;
            GTE_GET_CONTROL(31, pan_flag);
            if ((static_cast<std::uint32_t>(pan_flag) &
                 gte_flag_divide_overflow) != 0) {
                ++pan_overflows;
            }
            for (int gz = 0; gz <= board_rows; ++gz) {
                for (int gx = 0; gx <= board_columns; ++gx) {
                    const Projected measured = grid[gz][gx];
                    const Vertex source = grid_vertex(gx, gz, 1, false);
                    ++pan_compared;
                    for (int model = 0; model < 2; ++model) {
                        const Projected predicted = project_portably(
                            source, camera_pitch_degrees, camera_distance,
                            camera_focal, camera_vertical, focus, model == 1
                        );
                        int dx = measured.x - predicted.x;
                        int dy = measured.y - predicted.y;
                        if (dx < 0) dx = -dx;
                        if (dy < 0) dy = -dy;
                        const int deviation = dx > dy ? dx : dy;
                        if (model == 0) {
                            if (deviation > pan_worst_plain) {
                                pan_worst_plain = deviation;
                            }
                            if (deviation == 0) ++pan_exact_plain;
                        } else {
                            if (deviation > pan_worst) pan_worst = deviation;
                            if (deviation == 0) ++pan_exact;
                        }
                    }
                }
            }
        }
    }
    Line()
        .text("PREDICT pan offsets 25; vertices ")
        .decimal(static_cast<std::uint32_t>(pan_compared))
        .text("; plain division exact ")
        .decimal(static_cast<std::uint32_t>(pan_exact_plain))
        .text(" worst ")
        .decimal(static_cast<std::uint32_t>(pan_worst_plain))
        .text("; hardware divider exact ")
        .decimal(static_cast<std::uint32_t>(pan_exact))
        .text(" worst ")
        .decimal(static_cast<std::uint32_t>(pan_worst))
        .flush();
    Line()
        .text("PREDICT pan divider overflows at ")
        .decimal(static_cast<std::uint32_t>(pan_overflows))
        .text(" of 25 offsets")
        .flush();
    expect(pan_exact == pan_compared,
           "every_translated_camera_is_predicted_exactly");
    expect(pan_overflows == 0,
           "no_translated_camera_overflows_the_divider");

    // 9h. The evidence a person can look at: the same far row, seen from the
    //     three cameras 9c measured. Captured through the observer slot, which
    //     does nothing at all when no observer is attached.
    {
        const Focus stills[4] = {
            Focus{0, 0},
            ceiling,
            Focus{0, board_half_depth},
            Focus{0, board_half_depth + reach_to_equalise},
        };
        static const char* const labels[4] = {"pan-rest",
                                              "pan-cursor-follow-ceiling",
                                              "pan-board-clamped",
                                              "pan-one-board-reach"};
        for (int i = 0; i < 4; ++i) {
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, stills[i]);
            transform_world(cached_vertex_count);
            refresh_packets();
            clear_screen();
            submit_packets(packet_plan_count);
            take_still(labels[i]);
        }
    }

    // 9i. The film. Two separate runs of the same script, so that the
    //     observer's cost is never inside a measurement: the cursor-following
    //     circuit a shipped client would have, and then the free pan that
    //     reaches what the cursor cannot.
    announce_film("pan-cursor-follow", pan_frames);
    const PanRun filmed = run_pan(follow_settings, nullptr, 0, pan_frames - 1);
    expect(filmed.frames == pan_frames, "the_filmed_run_ran_the_whole_script");
    // The second film is the first segment only: rest, then one held
    // direction, then the settle. That is the whole of what it exists to show
    // (a far row arriving at the near distance, and what leaves the screen on
    // the way), and a film is a megabyte a hundred frames.
    constexpr int free_film_frames = 76;
    announce_film("pan-free-pan", free_film_frames);
    const PanRun filmed_free = run_pan(free_settings, nullptr, 0,
                                       free_film_frames - 1);
    expect(filmed_free.frames == pan_frames,
           "the_free_panning_filmed_run_ran_the_whole_script");

    // -----------------------------------------------------------------------
    // 11. One archetype as a mesh, beside its billboard.
    // -----------------------------------------------------------------------
    build_mesh();
    derive_mesh_palettes();

    // What the generated header actually holds, asked of the header rather
    // than of the two archetypes the scenes below happen to draw. Four claims,
    // and each is a rule the art contract's mesh section states:
    //
    //   * a mesh carries no colour: there is nowhere in eight values per part
    //     for one, and every ramp and rung a part names is an index the
    //     renderer's own tables have;
    //   * a figure is built at unit_world / cos(pitch) with its feet at zero;
    //   * every commissioned figure is inside the triangle band; and
    //   * an archetype without a mesh is a null row and a zero count, not a
    //     short table a renderer could index past. That is what makes "a style
    //     without a mesh commission is still a complete style" a fact about
    //     this file rather than a promise.
    {
        int commissioned = 0;
        int uncommissioned = 0;
        int worst_triangles_low = 0x7FFFFFFF;
        int worst_triangles_high = 0;
        bool indices_in_range = true;
        bool built_to_height = true;
        for (int archetype = 0; archetype < art::archetype_count; ++archetype) {
            const art::Mesh figure = art::mesh(archetype);
            if (figure.parts == nullptr || figure.part_count == 0) {
                if (figure.parts != nullptr || figure.part_count != 0) {
                    indices_in_range = false;  // half a row is not a row
                }
                ++uncommissioned;
                continue;
            }
            ++commissioned;
            const int triangles = figure.part_count * mesh_faces_per_box * 2;
            if (triangles < worst_triangles_low) worst_triangles_low = triangles;
            if (triangles > worst_triangles_high) {
                worst_triangles_high = triangles;
            }
            int lowest = 0x7FFF;
            int highest = -0x7FFF;
            for (int part = 0; part < figure.part_count; ++part) {
                const short* values = figure.parts + part * mesh_values_per_part;
                if (values[2] < lowest) lowest = values[2];
                if (values[3] > highest) highest = values[3];
                if (values[0] >= values[1] || values[2] >= values[3] ||
                    values[4] >= values[5]) {
                    indices_in_range = false;  // not a box with volume
                }
                if (values[6] < 0 || values[6] >= ramp_count ||
                    values[7] < 0 || values[7] >= ramp_rungs) {
                    indices_in_range = false;
                }
            }
            if (lowest != 0 || highest != mesh_world_height) {
                built_to_height = false;
            }
        }
        Line()
            .text("MESH generated header: style ")
            .text(grandleon_playstation_mesh_style_name)
            .text(", ")
            .decimal(static_cast<std::uint32_t>(commissioned))
            .text(" archetypes with a mesh and ")
            .decimal(static_cast<std::uint32_t>(uncommissioned))
            .text(" without; ")
            .decimal(static_cast<std::uint32_t>(worst_triangles_low))
            .text(" to ")
            .decimal(static_cast<std::uint32_t>(worst_triangles_high))
            .text(" triangles, built ")
            .decimal(static_cast<std::uint32_t>(mesh_world_height))
            .text(" world units tall")
            .flush();
        expect(grandleon_playstation_mesh_commissioned,
               "the_embedded_style_has_a_mesh_commission");
        expect(commissioned + uncommissioned == art::archetype_count &&
                   commissioned >= 2,
               "the_generated_header_holds_a_row_for_every_archetype");
        expect(indices_in_range,
               "every_generated_part_is_a_box_naming_a_ramp_and_a_rung");
        expect(built_to_height,
               "every_generated_figure_stands_feet_at_zero_at_the_built_"
               "height");
        expect(worst_triangles_low >= 150 && worst_triangles_high <= 300,
               "every_generated_figure_is_inside_the_triangle_band");
    }

    Line()
        .text("MESH knight parts ")
        .decimal(static_cast<std::uint32_t>(mesh_part_count))
        .text(" vertices ")
        .decimal(static_cast<std::uint32_t>(mesh_vertex_count))
        .text(" faces ")
        .decimal(static_cast<std::uint32_t>(mesh_face_count))
        .text(" triangles ")
        .decimal(static_cast<std::uint32_t>(mesh_triangle_count))
        .flush();
    expect(mesh_triangle_count >= 150 && mesh_triangle_count <= 300,
           "the_mesh_is_inside_the_150_to_300_triangle_budget");

    // The colour route, reported per faction so that "the mesh takes the six
    // ramps" is a number rather than a claim.
    {
        int distinct_brights = 0;
        std::uint32_t seen[art::faction_colour_count] = {};
        for (int colour = 0; colour < art::faction_colour_count; ++colour) {
            const MeshPalette& palette =
                mesh_palette[knight_archetype][colour];
            const std::uint32_t bright =
                palette.rung[ramp_faction][ramp_rungs - 1];
            bool already = false;
            for (int i = 0; i < distinct_brights; ++i) {
                if (seen[i] == bright) already = true;
            }
            if (!already) seen[distinct_brights++] = bright;
            Line()
                .text("MESH ramp knight colour ")
                .decimal(static_cast<std::uint32_t>(colour))
                .text(" faction entries ")
                .decimal(static_cast<std::uint32_t>(palette.faction_entries))
                .text(" neutral ")
                .decimal(static_cast<std::uint32_t>(palette.neutral_entries))
                .text(" brightest faction rung 0x")
                .hex64(bright)
                .flush();
        }
        Line()
            .text("MESH the six factions give ")
            .decimal(static_cast<std::uint32_t>(distinct_brights))
            .text(" distinct brightest faction colours")
            .flush();
        expect(distinct_brights == art::faction_colour_count,
               "the_six_faction_ramps_give_the_mesh_six_different_colours");
    }
    {
        int thinnest = 16;
        for (int archetype = 0; archetype < art::archetype_count;
             ++archetype) {
            for (int colour = 0; colour < art::faction_colour_count;
                 ++colour) {
                const int entries =
                    mesh_palette[archetype][colour].faction_entries;
                if (entries < thinnest) thinnest = entries;
            }
        }
        Line()
            .text("MESH thinnest faction ramp over all ")
            .decimal(static_cast<std::uint32_t>(art::archetype_count))
            .text(" archetypes x ")
            .decimal(static_cast<std::uint32_t>(art::faction_colour_count))
            .text(" colours: ")
            .decimal(static_cast<std::uint32_t>(thinnest))
            .text(" entries")
            .flush();
        expect(thinnest >= 2,
               "every_archetype_and_colour_offers_a_faction_ramp");
    }

    // The ordering claim, asked of the machine. The array is authored far to
    // near at this pitch; if that is true the depths come out non-increasing
    // and the runtime sort is a no-op, and both are checked rather than either
    // being assumed.
    load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                camera_vertical, Focus{0, 0});
    {
        const MeshPose upright = pose_on(6, 4, 0, 0, 0, 0, 0);
        build_mesh_world(upright);
        transform_mesh();
        bool monotone = true;
        std::int32_t previous = 0x7FFFFFFF;
        for (int part = 0; part < mesh_part_count; ++part) {
            std::int32_t sum = 0;
            for (int corner = 0; corner < mesh_vertices_per_box; ++corner) {
                sum += mesh_screen[part * mesh_vertices_per_box + corner].depth;
            }
            if (sum > previous) monotone = false;
            previous = sum;
        }
        sort_mesh_parts();
        bool sort_agrees = true;
        for (int part = 0; part < mesh_part_count; ++part) {
            if (mesh_order[part] != part) sort_agrees = false;
        }
        Line()
            .text("MESH authored order monotone ")
            .text(monotone ? "yes" : "NO")
            .text("; the runtime sort reproduces it ")
            .text(sort_agrees ? "yes" : "NO")
            .flush();
        expect(monotone, "the_authored_part_order_is_far_to_near_at_this_pitch");
        expect(sort_agrees,
               "an_unturned_mesh_needs_no_sort_at_all");

        // The same question of every other commissioned figure, because the
        // generator's ordering rule is arithmetic on part *centres* and the
        // machine's is the sum of eight *projected* corner depths. Those are
        // the same order for a well-authored figure and need not be for a
        // badly authored one, so the two are compared rather than one being
        // trusted. A mesh that only the generator agrees is far-to-near is a
        // mesh that would need a depth buffer.
        int checked = 0;
        bool all_monotone = true;
        for (int archetype = 0; archetype < model_slots; ++archetype) {
            if (archetype == knight_archetype) continue;
            if (art::mesh(archetype).part_count == 0) continue;
            ++checked;
            select_model(archetype);
            build_mesh_world(upright);
            transform_mesh();
            std::int32_t last = 0x7FFFFFFF;
            for (int part = 0; part < active_part_count; ++part) {
                std::int32_t sum = 0;
                for (int corner = 0; corner < mesh_vertices_per_box; ++corner) {
                    sum += mesh_screen[part * mesh_vertices_per_box + corner]
                               .depth;
                }
                if (sum > last) all_monotone = false;
                last = sum;
            }
            sort_mesh_parts();
            for (int part = 0; part < active_part_count; ++part) {
                if (mesh_order[part] != part) all_monotone = false;
            }
        }
        select_model(knight_archetype);
        Line()
            .text("MESH every other commissioned figure checked: ")
            .decimal(static_cast<std::uint32_t>(checked))
            .text("; all far-to-near on the machine ")
            .text(all_monotone ? "yes" : "NO")
            .flush();
        expect(checked >= 1,
               "the_ordering_rule_was_asked_of_more_than_one_figure");
        expect(all_monotone,
               "every_generated_figure_is_far_to_near_on_the_machine_too");
    }

    // How many of the 120 faces a viewer ever sees. The answer is the argument
    // for winding out the back faces rather than drawing them: about half the
    // packets never leave the CPU.
    {
        gpu::begin(0x7C1F);
        const MeshPose upright = pose_on(6, 4, 0, 0, 0, 0, 0);
        build_mesh_world(upright);
        transform_mesh();
        draw_mesh(knight_archetype, 0, false);
        Line()
            .text("MESH faces drawn ")
            .decimal(static_cast<std::uint32_t>(last_mesh_faces_drawn))
            .text(" of ")
            .decimal(static_cast<std::uint32_t>(mesh_face_count))
            .text("; back-face winding removes ")
            .decimal(static_cast<std::uint32_t>(mesh_face_count -
                                                last_mesh_faces_drawn))
            .flush();
        expect(last_mesh_faces_drawn > 0 &&
                   last_mesh_faces_drawn < mesh_face_count,
               "the_back_face_test_keeps_some_faces_and_drops_others");
    }

    // How tall the thing is actually drawn, near row and far row, which is the
    // number §1.4 made the argument about and the one the height above was
    // chosen for.
    {
        std::int32_t drawn[2] = {0, 0};
        const int rows[2] = {0, board_rows - 1};
        for (int which = 0; which < 2; ++which) {
            build_mesh_world(pose_on(6, rows[which], 0, 0, 0, 0, 0));
            transform_mesh();
            std::int32_t highest = 0x7FFFFFFF;
            std::int32_t lowest = -0x7FFFFFFF;
            for (int i = 0; i < mesh_vertex_count; ++i) {
                if (mesh_screen[i].y < highest) highest = mesh_screen[i].y;
                if (mesh_screen[i].y > lowest) lowest = mesh_screen[i].y;
            }
            drawn[which] = lowest - highest;
        }
        Line()
            .text("MESH height built ")
            .decimal(static_cast<std::uint32_t>(mesh_world_height))
            .text(" world units, drawn ")
            .signed_decimal(static_cast<int>(drawn[1]))
            .text(" px on the far row and ")
            .signed_decimal(static_cast<int>(drawn[0]))
            .text(" px on the near row, against a 32-texel cell")
            .flush();
    }

    // What one unit costs, each way, over the sixteen a full battle fields.
    // Two mesh figures rather than one: a *settled* unit is a translation of
    // the authored mesh and pays neither the model rotation nor the sort, and
    // on a tactics board almost every unit is settled almost always. A unit
    // mid-animation pays both, and that is the second line.
    {
        constexpr int repeats = unit_count;
        const MeshPose settled = pose_on(6, 4, 0, 0, 0, 0, 0);
        const MeshPose turning = pose_on(6, 4, 0, 0, 0, 90, 8);
        std::uint32_t mesh_total[2] = {0, 0};
        for (int which = 0; which < 2; ++which) {
            const MeshPose& pose = which == 0 ? settled : turning;
            psx::start_clock();
            const std::uint32_t m0 = tick();
            for (int i = 0; i < repeats; ++i) build_mesh_world(pose);
            const std::uint32_t m1 = tick();
            for (int i = 0; i < repeats; ++i) transform_mesh();
            const std::uint32_t m2 = tick();
            for (int i = 0; i < repeats; ++i) order_mesh_parts();
            const std::uint32_t m3 = tick();
            for (int i = 0; i < repeats; ++i) {
                draw_mesh(knight_archetype, 0, false);
            }
            const std::uint32_t m4 = tick();
            const std::uint32_t divisor = static_cast<std::uint32_t>(repeats);
            mesh_total[which] = microseconds(m4 - m0) / divisor;
            Line()
                .text("UNIT mesh ")
                .text(which == 0 ? "settled" : "turning")
                .text(" per unit build ")
                .decimal(microseconds(m1 - m0) / divisor)
                .text(" transform ")
                .decimal(microseconds(m2 - m1) / divisor)
                .text(" order ")
                .decimal(microseconds(m3 - m2) / divisor)
                .text(" cull and submit ")
                .decimal(microseconds(m4 - m3) / divisor)
                .text(" us, total ")
                .decimal(mesh_total[which])
                .text(" us, faces drawn ")
                .decimal(static_cast<std::uint32_t>(last_mesh_faces_drawn))
                .flush();
        }

        Projected flat_screen[4];
        const std::int32_t bx = cell_centre_x(6);
        const std::int32_t bz = cell_centre_z(4);
        const std::int32_t by =
            static_cast<std::int32_t>(steps_at(6, 4)) * step_world;
        psx::start_clock();
        const std::uint32_t b0 = tick();
        for (int i = 0; i < repeats; ++i) {
            (void)screen_billboard(bx, by, bz, flat_screen);
        }
        const std::uint32_t b1 = tick();
        for (int i = 0; i < repeats; ++i) {
            draw_textured_quad(flat_screen,
                               character_cell_of[knight_archetype][0],
                               character_clut_of[knight_archetype][0]);
        }
        const std::uint32_t b2 = tick();
        const std::uint32_t divisor = static_cast<std::uint32_t>(repeats);
        const std::uint32_t flat_total = microseconds(b2 - b0) / divisor;
        Line()
            .text("UNIT screen-space billboard per unit build and transform ")
            .decimal(microseconds(b1 - b0) / divisor)
            .text(" submit ")
            .decimal(microseconds(b2 - b1) / divisor)
            .text(" us, total ")
            .decimal(flat_total)
            .text(" us")
            .flush();
        Line()
            .text("UNIT the settled mesh costs ")
            .decimal(flat_total == 0 ? 0u : mesh_total[0] / flat_total)
            .text(" times a billboard and the turning one ")
            .decimal(flat_total == 0 ? 0u : mesh_total[1] / flat_total)
            .text(" times, for ")
            .decimal(static_cast<std::uint32_t>(mesh_triangle_count / 2))
            .text(" times the triangles")
            .flush();
    }

    // The full battle load, both ways, in the frame a renderer would have: the
    // board built once and re-transformed, sixteen units on top.
    packet_plan_count = plan_packets(true, false);
    {
        gpu::begin(0x7C1F);
        psx::start_clock();
        const std::uint32_t t0 = tick();
        transform_world(cached_vertex_count);
        refresh_packets();
        clear_screen();
        int from = 0;
        int mesh_faces_total = 0;
        for (int step = 0; step < board_rows; ++step) {
            const int row = board_rows - 1 - step;
            submit_packet_range(from, plan_row_end[step]);
            from = plan_row_end[step];
            for (int i = 0; i < unit_count; ++i) {
                if (static_cast<int>(units[i].row) != row) continue;
                build_mesh_world(pose_on(units[i].column, row, 0, 0, 0, 0, 0));
                transform_mesh();
                draw_mesh(knight_archetype, units[i].colour, false);
                mesh_faces_total += last_mesh_faces_drawn;
            }
        }
        const std::uint32_t t1 = tick();
        const std::uint32_t mesh_load_us = microseconds(t1 - t0);
        const int mesh_load_triangles =
            (packet_plan_count + mesh_faces_total) * 2;
        Line()
            .text("LOAD sixteen mesh units frame ")
            .decimal(mesh_load_us)
            .text(" us fps ")
            .decimal(mesh_load_us == 0 ? 0u : 1000000u / mesh_load_us)
            .text("; board packets ")
            .decimal(static_cast<std::uint32_t>(packet_plan_count))
            .text(" mesh faces drawn ")
            .decimal(static_cast<std::uint32_t>(mesh_faces_total))
            .text(" of ")
            .decimal(static_cast<std::uint32_t>(unit_count * mesh_face_count))
            .flush();
        Line()
            .text("LOAD sixteen mesh units triangles submitted ")
            .decimal(static_cast<std::uint32_t>(mesh_load_triangles))
            .text("; transformed ")
            .decimal(static_cast<std::uint32_t>(
                unit_count * mesh_triangle_count))
            .text(" of a 9400-triangle 30 fps budget")
            .flush();
        take_still("mesh-sixteen");
        expect(mesh_load_us < thirty_fps_microseconds,
               "sixteen_mesh_units_and_a_board_fit_in_a_thirtieth_of_a_second");
        if (mesh_load_us < sixty_fps_microseconds) {
            Line().text("NOTE the sixteen-mesh frame also fits in a sixtieth")
                .flush();
        }

        // The same frame with the sixteen drawn as billboards, measured by the
        // same loop, so the comparison is not against a figure from §1.
        packet_plan_count = plan_packets(true, true);
        gpu::begin(0x7C1F);
        psx::start_clock();
        const std::uint32_t f0 = tick();
        transform_world(cached_vertex_count);
        refresh_packets();
        clear_screen();
        submit_packets(packet_plan_count);
        const std::uint32_t f1 = tick();
        const std::uint32_t flat_load_us = microseconds(f1 - f0);
        Line()
            .text("LOAD sixteen billboard units frame ")
            .decimal(flat_load_us)
            .text(" us fps ")
            .decimal(flat_load_us == 0 ? 0u : 1000000u / flat_load_us)
            .text("; the mesh frame is ")
            .decimal(flat_load_us == 0 ? 0u : mesh_load_us * 10u / flat_load_us)
            .text(" tenths of it")
            .flush();
    }

    // The comparison scene: the bare board, three pairs, one camera.
    packet_plan_count = plan_packets(true, false);
    {
        constexpr MotionState still{};
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        draw_comparison_frame(false, still, false);
        const std::uint64_t first = hash_framebuffer();
        take_still("mesh-beside-billboard");
        gpu::begin(0x7C1F);
        draw_comparison_frame(false, still, false);
        const std::uint64_t second = hash_framebuffer();
        Line().text("HASH mesh scene ").hex64(first).text(" ").hex64(second)
            .flush();
        expect(first == second,
               "two_draws_of_the_mesh_scene_hash_the_same_framebuffer");

        // The same three pairs with the camera brought to each in turn, which
        // is what a panning client would give a player who wanted to look. The
        // focus reaches past the board's own edge for the far row, which §5.1
        // measured and named.
        static const char* const pair_labels[compare_pair_count] = {
            "mesh-near-row", "mesh-mid-row", "mesh-far-row"};
        for (int pair = 0; pair < compare_pair_count; ++pair) {
            const Focus at{
                cell_centre_x(compare_pairs[pair].column) + tile_world / 2,
                static_cast<std::int32_t>(compare_pairs[pair].row - 1) *
                    tile_world};
            gpu::begin(0x7C1F);
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, at);
            draw_comparison_frame(false, still, false);
            take_still(pair_labels[pair]);
        }

        // And the same scene with the mesh textured instead of flat-shaded,
        // because the brief asked for both to be tried rather than argued
        // about.
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        draw_comparison_frame(true, still, false);
        take_still("mesh-textured");

        psx::start_clock();
        const std::uint32_t x0 = tick();
        draw_comparison_frame(false, still, false);
        const std::uint32_t x1 = tick();
        draw_comparison_frame(true, still, false);
        const std::uint32_t x2 = tick();
        Line()
            .text("MESH comparison frame flat-shaded ")
            .decimal(microseconds(x1 - x0))
            .text(" us, textured ")
            .decimal(microseconds(x2 - x1))
            .text(" us")
            .flush();
    }

    // The six faction ramps on one screen: six mesh knights along a row, one a
    // colour, drawn from the six CLUTs the art library already generated.
    {
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 128});
        transform_world(cached_vertex_count);
        refresh_packets();
        clear_screen();
        int from = 0;
        for (int step = 0; step < board_rows; ++step) {
            const int row = board_rows - 1 - step;
            submit_packet_range(from, plan_row_end[step]);
            from = plan_row_end[step];
            if (row != 4) continue;
            for (int colour = 0; colour < art::faction_colour_count;
                 ++colour) {
                build_mesh_world(pose_on(3 + colour, row, 0, 0, 0, 0, 0));
                transform_mesh();
                draw_mesh(knight_archetype, colour, false);
            }
        }
        take_still("mesh-faction-ramps");
    }

    // 11a. The camera sweep past all three pairs, filmed. A free pan along the
    //      board's depth, so every pair is seen at every distance.
    constexpr int mesh_sweep_frames = 32;
    announce_film("mesh-camera-sweep", mesh_sweep_frames);
    {
        constexpr MotionState still{};
        for (int frame = 0; frame < mesh_sweep_frames; ++frame) {
            // Out to one board past the far edge and back. Twice the §5.1
            // glide rate, because this film exists to show three pairs at
            // every distance rather than to be judged for fluidity. Fluidity
            // is `pan-cursor-follow.gif`'s job, and a whole-screen film is a
            // megabyte a hundred frames.
            const int half = mesh_sweep_frames / 2;
            const int step = frame < half ? frame : mesh_sweep_frames - frame;
            const Focus at{0, static_cast<std::int32_t>(step) * 2 *
                                  pan_units_per_frame};
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, at);
            draw_comparison_frame(false, still, false);
            take_shot();
        }
    }

    // 11b. The motions of slice one, on both members of every pair. The camera
    //      does not move at all in this one: the only thing changing is the
    //      units, which is what it exists to show. It sits at §1's rest focus,
    //      which is the one focus that has all nine rows on the display at
    //      once (§5.1 measured the near row at screen y 224 and the far row at
    //      22), so all three pairs are in every frame.
    announce_film("mesh-motion", motion_film_frames);
    {
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        for (int frame = 0; frame < motion_film_frames; ++frame) {
            draw_comparison_frame(false, motion_at(frame), true);
            take_shot();
        }
        Line()
            .text("MOTION ")
            .decimal(static_cast<std::uint32_t>(motion_film_frames))
            .text(" frames: ")
            .decimal(static_cast<std::uint32_t>(motion_frames_per_tile))
            .text(" frames a tile along the route, a ")
            .decimal(static_cast<std::uint32_t>(motion_flinch_frames))
            .text("-frame flinch, both applied to the mesh and the billboard "
                  "alike; only the mesh turns and leans")
            .flush();
    }

    // 11c. Determinism under the motions, which is the half §4 needs: the same
    //      frame-counted script twice, hashed at the same frames.
    {
        // Rest, mid-walk, mid-flinch, settled after it, and mid-walk back.
        constexpr int motion_checkpoints[] = {4, 12, 26, 33, 42};
        constexpr int motion_checkpoint_count =
            static_cast<int>(sizeof(motion_checkpoints) /
                             sizeof(motion_checkpoints[0]));
        std::uint64_t first_pass[motion_checkpoint_count] = {};
        std::uint64_t second_pass[motion_checkpoint_count] = {};
        for (int pass = 0; pass < 2; ++pass) {
            int checkpoint = 0;
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, Focus{0, 0});
            for (int frame = 0; frame < motion_film_frames; ++frame) {
                gpu::begin(0x7C1F);
                draw_comparison_frame(false, motion_at(frame), true);
                if (checkpoint < motion_checkpoint_count &&
                    frame == motion_checkpoints[checkpoint]) {
                    const std::uint64_t hash = hash_framebuffer();
                    if (pass == 0) {
                        first_pass[checkpoint] = hash;
                    } else {
                        second_pass[checkpoint] = hash;
                    }
                    ++checkpoint;
                }
            }
        }
        int matching = 0;
        int distinct = 0;
        for (int i = 0; i < motion_checkpoint_count; ++i) {
            if (first_pass[i] == second_pass[i]) ++matching;
            bool seen = false;
            for (int j = 0; j < i; ++j) {
                if (first_pass[j] == first_pass[i]) seen = true;
            }
            if (!seen) ++distinct;
            Line()
                .text("HASH motion frame ")
                .decimal(static_cast<std::uint32_t>(motion_checkpoints[i]))
                .text(" ")
                .hex64(first_pass[i])
                .text(" ")
                .hex64(second_pass[i])
                .text(first_pass[i] == second_pass[i] ? " same" : " DIFFERENT")
                .flush();
        }
        expect(matching == motion_checkpoint_count,
               "two_runs_of_the_motion_script_hash_the_same_at_every_"
               "checkpoint");
        expect(distinct == motion_checkpoint_count,
               "the_motion_script_visits_a_different_picture_at_each_"
               "checkpoint");
    }

    // -----------------------------------------------------------------------
    // 11d. The fit (§5.4). §5.3's stills settled it: flat shading stays,
    //      textured is dead, and the disproportion is the problem, 41 px on
    //      the far row against 25 on the near, meshes reading as
    //      differently-sized creatures. This phase measures the fix: each unit
    //      scaled about its feet so its drawn height is its tile's drawn
    //      width, which is also the billboard's own side.
    // -----------------------------------------------------------------------
    packet_plan_count = plan_packets(true, false);
    load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                camera_vertical, Focus{0, 0});
    select_model(knight_archetype);
    {
        // (a) The arithmetic at the three pairs: what the two-tile build drew,
        //     what the fit draws, and what the billboard beside it draws.
        int worst_deviation = 0;
        int worst_feet = 0;
        int feet_allowed = 0;
        std::int32_t near_height = 0;
        std::int32_t far_height = 0;
        for (int pair = 0; pair < compare_pair_count; ++pair) {
            const int column = compare_pairs[pair].column;
            const int row = compare_pairs[pair].row;
            const MeshPose pose = pose_on(column, row, 0, 0, 0, 0, 0);
            const FootView foot = view_foot(pose.x, pose.y, pose.z);
            const int side = camera_focal * unit_world / foot.depth;
            const DrawnExtent built = measure_drawn(pose, false);
            const std::int32_t scale = build_fitted(pose);
            const DrawnExtent fitted = screen_extent();
            const int built_height = static_cast<int>(built.bottom - built.top);
            const int fitted_height =
                static_cast<int>(fitted.bottom - fitted.top);
            if (pair == 0) near_height = fitted_height;
            if (pair == compare_pair_count - 1) far_height = fitted_height;
            int deviation = fitted_height - side;
            if (deviation < 0) deviation = -deviation;
            if (deviation > worst_deviation) worst_deviation = deviation;
            int feet = static_cast<int>(fitted.bottom - foot.sy);
            if (feet < 0) feet = -feet;
            if (feet > worst_feet) worst_feet = feet;
            // What the figure's own boots are worth, at the scale and depth
            // this row drew them at, and at the height up the display they were
            // drawn: both halves of the departure, as above.
            std::int32_t from_centre =
                fitted.bottom - gpu::screen_height / 2;
            if (from_centre < 0) from_centre = -from_centre;
            const int allowance = static_cast<int>(drawn_from_local_z(
                model_forward_reach(), scale, foot.depth, from_centre));
            if (allowance > feet_allowed) feet_allowed = allowance;
            Line()
                .text("FIT row ")
                .decimal(static_cast<std::uint32_t>(row))
                .text(" depth ")
                .decimal(static_cast<std::uint32_t>(foot.depth))
                .text(" scale ")
                .decimal(static_cast<std::uint32_t>(scale))
                .text("/256: two-tile build ")
                .signed_decimal(built_height)
                .text(" px, fitted ")
                .signed_decimal(fitted_height)
                .text(" px, billboard ")
                .signed_decimal(side)
                .text(" px, feet within ")
                .signed_decimal(feet)
                .text(" of ")
                .signed_decimal(allowance)
                .text(" px the figure reaches forward")
                .flush();
        }
        // §5.17 retired `every_fitted_height_is_within_two_pixels_of_the_
        // billboard` from here. Two pixels was the medieval knight's own
        // number: the uniform fit corrects a closed form solved for a *line*
        // with one measured pass, and what one pass leaves behind is how far
        // the figure is from being a line, so `scifi` came out three and
        // `mythical` four, on figures nothing is wrong with. The property is
        // held on the path that ships and held harder: §5.5's
        // `every_matched_height_is_within_two_pixels_of_the_sprites` is the
        // same two pixels against a better target, on both axes, and it passes
        // for all seven styles. The number is still measured, because §5.5's
        // case rests on this path being the worse of the two and that is now
        // asserted rather than tabulated.
        fitted_row_height_error = worst_deviation;
        expect(near_height > far_height,
               "the_fitted_mesh_shrinks_with_distance_like_its_tile");
        Line()
            .text("FIT feet worst ")
            .signed_decimal(worst_feet)
            .text(" px of ")
            .signed_decimal(feet_allowed)
            .text(" px this figure reaches forward")
            .flush();
        // "Planted on its tile" is not a fixed number of pixels and never was.
        // A unit's drawn bottom sits below its foot by however far its own
        // nearest box reaches toward the eye, projected, so the bound is the
        // figure's, and five was `medieval`'s reach of eight rounded down after
        // the fact. `scifi` measures seven against its own eight and would fail
        // a five for having boots.
        expect(worst_feet <= feet_allowed,
               "a_fitted_unit_stands_no_lower_than_it_reaches_forward");

        // (b) The rejected alternative, measured rather than argued: dropping
        //     the screen-offset term ("scale by depth alone") cancels the
        //     depth too and leaves a constant, and the constant is the
        //     two-tile build. The naive compensation IS the status quo; the
        //     screen-offset term is the entire fix.
        const std::int32_t naive =
            fixed_one * unit_world / cosine_of(camera_pitch_degrees);
        Line()
            .text("FIT the depth-only scale is the constant ")
            .signed_decimal(static_cast<int>(naive))
            .text(" world units, which is the two-tile build itself")
            .flush();
        expect(naive == mesh_world_height,
               "the_depth_only_compensation_is_the_status_quo");
    }

    // The fitted scene, hashed twice, then against the unfitted scene: the
    // determinism claim and its negative control.
    {
        static constexpr std::int8_t knights[compare_pair_count] = {0, 0, 0};
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        draw_fitted_comparison_frame(knights);
        const std::uint64_t first = hash_framebuffer();
        take_still("mesh-fit-beside-billboard");
        gpu::begin(0x7C1F);
        draw_fitted_comparison_frame(knights);
        const std::uint64_t second = hash_framebuffer();
        Line()
            .text("HASH fitted scene ")
            .hex64(first)
            .text(" ")
            .hex64(second)
            .flush();
        expect(first == second,
               "two_draws_of_the_fitted_scene_hash_the_same_framebuffer");

        gpu::begin(0x7C1F);
        constexpr MotionState still{};
        select_model(knight_archetype);
        draw_comparison_frame(false, still, false);
        const std::uint64_t unfitted = hash_framebuffer();
        expect(first != unfitted,
               "the_fit_draws_a_different_picture_from_the_two_tile_build");

        // The same three row visits §5.3 photographed, re-shot with the fit.
        static const char* const fit_labels[compare_pair_count] = {
            "mesh-fit-near-row", "mesh-fit-mid-row", "mesh-fit-far-row"};
        for (int pair = 0; pair < compare_pair_count; ++pair) {
            const Focus at{
                cell_centre_x(compare_pairs[pair].column) + tile_world / 2,
                static_cast<std::int32_t>(compare_pairs[pair].row - 1) *
                    tile_world};
            gpu::begin(0x7C1F);
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, at);
            draw_fitted_comparison_frame(knights);
            take_still(fit_labels[pair]);
        }
    }

    // The archer: the second archetype, held to every check the knight passes,
    // so the fit is a rule and not a knight-shaped coincidence.
    {
        select_model(archer_archetype);
        Line()
            .text("MESH archer parts ")
            .decimal(static_cast<std::uint32_t>(active_part_count))
            .text(" vertices ")
            .decimal(static_cast<std::uint32_t>(active_vertex_count))
            .text(" faces ")
            .decimal(static_cast<std::uint32_t>(active_face_count))
            .text(" triangles ")
            .decimal(static_cast<std::uint32_t>(archer_triangle_count))
            .flush();
        expect(archer_triangle_count >= 150 && archer_triangle_count <= 300,
               "the_archer_is_inside_the_150_to_300_triangle_budget");

        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        const MeshPose upright = pose_on(6, 4, 0, 0, 0, 0, 0);
        build_mesh_world(upright);
        transform_mesh();
        bool monotone = true;
        std::int32_t previous = 0x7FFFFFFF;
        for (int part = 0; part < active_part_count; ++part) {
            std::int32_t sum = 0;
            for (int corner = 0; corner < mesh_vertices_per_box; ++corner) {
                sum += mesh_screen[part * mesh_vertices_per_box + corner].depth;
            }
            if (sum > previous) monotone = false;
            previous = sum;
        }
        sort_mesh_parts();
        bool sort_agrees = true;
        for (int part = 0; part < active_part_count; ++part) {
            if (mesh_order[part] != part) sort_agrees = false;
        }
        Line()
            .text("MESH archer authored order monotone ")
            .text(monotone ? "yes" : "NO")
            .text("; the runtime sort reproduces it ")
            .text(sort_agrees ? "yes" : "NO")
            .flush();
        expect(monotone,
               "the_archers_authored_order_is_far_to_near_at_this_pitch");
        expect(sort_agrees, "an_unturned_archer_needs_no_sort_at_all");

        gpu::begin(0x7C1F);
        draw_mesh(archer_archetype, 0, false);
        Line()
            .text("MESH archer faces drawn ")
            .decimal(static_cast<std::uint32_t>(last_mesh_faces_drawn))
            .text(" of ")
            .decimal(static_cast<std::uint32_t>(active_face_count))
            .flush();
        expect(last_mesh_faces_drawn > 0 &&
                   last_mesh_faces_drawn < active_face_count,
               "the_archers_back_face_test_keeps_some_faces_and_drops_others");

        // The fit holds for the archer at the middle pair, by the same
        // measurement the knight got.
        {
            const MeshPose pose = pose_on(8, 4, 0, 0, 0, 0, 0);
            const FootView foot = view_foot(pose.x, pose.y, pose.z);
            const int side = camera_focal * unit_world / foot.depth;
            const DrawnExtent fitted = measure_drawn(pose, true);
            const int height = static_cast<int>(fitted.bottom - fitted.top);
            int deviation = height - side;
            if (deviation < 0) deviation = -deviation;
            Line()
                .text("FIT archer mid row fitted ")
                .signed_decimal(height)
                .text(" px, billboard ")
                .signed_decimal(side)
                .text(" px")
                .flush();
            expect(deviation <= 2, "the_fitted_archer_matches_its_billboard");
        }

        static constexpr std::int8_t archers[compare_pair_count] = {
            archer_archetype, archer_archetype, archer_archetype};
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        draw_fitted_comparison_frame(archers);
        take_still("mesh-fit-archer");
        const Focus mid{cell_centre_x(8) + tile_world / 2, 3 * tile_world};
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, mid);
        draw_fitted_comparison_frame(archers);
        take_still("mesh-fit-archer-mid-row");
    }

    // The fitted sweep: the knight pairs at the near and far rows, the archer
    // at the middle, so both archetypes are seen at every distance. Hashed at
    // five checkpoints over two runs before it is filmed, which is §5.1's
    // discipline, and the fit itself re-measured at every camera the sweep
    // visits.
    static constexpr std::int8_t sweep_archetypes[compare_pair_count] = {
        0, archer_archetype, 0};
    constexpr int fit_sweep_frames = 32;
    {
        // The sweep goes out and comes back, so a frame and its mirror show
        // the same focus; the checkpoints are chosen off the mirror pairs so
        // the distinct-pictures control means something.
        constexpr int sweep_checkpoints[] = {0, 5, 10, 16, 23};
        constexpr int sweep_checkpoint_count =
            static_cast<int>(sizeof(sweep_checkpoints) /
                             sizeof(sweep_checkpoints[0]));
        std::uint64_t first_pass[sweep_checkpoint_count] = {};
        std::uint64_t second_pass[sweep_checkpoint_count] = {};
        int worst_sweep_deviation = 0;
        for (int pass = 0; pass < 2; ++pass) {
            int checkpoint = 0;
            for (int frame = 0; frame < fit_sweep_frames; ++frame) {
                const int half = fit_sweep_frames / 2;
                const int step =
                    frame < half ? frame : fit_sweep_frames - frame;
                const Focus at{0, static_cast<std::int32_t>(step) * 2 *
                                      pan_units_per_frame};
                gpu::begin(0x7C1F);
                load_camera(camera_pitch_degrees, camera_distance,
                            camera_focal, camera_vertical, at);
                draw_fitted_comparison_frame(sweep_archetypes);
                if (pass == 0) {
                    // The proportion claim under panning: at every camera of
                    // the sweep, every pair whose foot is on the display is
                    // still within a few pixels of its billboard.
                    for (int pair = 0; pair < compare_pair_count; ++pair) {
                        const MeshPose pose =
                            pose_on(compare_pairs[pair].column,
                                    compare_pairs[pair].row, 0, 0, 0, 0, 0);
                        const FootView foot =
                            view_foot(pose.x, pose.y, pose.z);
                        if (foot.sy < 0 || foot.sy >= gpu::screen_height) {
                            continue;
                        }
                        select_model(sweep_archetypes[pair]);
                        const DrawnExtent fitted = measure_drawn(pose, true);
                        const int side =
                            camera_focal * unit_world / foot.depth;
                        int deviation =
                            static_cast<int>(fitted.bottom - fitted.top) -
                            side;
                        if (deviation < 0) deviation = -deviation;
                        if (deviation > worst_sweep_deviation) {
                            worst_sweep_deviation = deviation;
                        }
                    }
                }
                if (checkpoint < sweep_checkpoint_count &&
                    frame == sweep_checkpoints[checkpoint]) {
                    const std::uint64_t hash = hash_framebuffer();
                    if (pass == 0) {
                        first_pass[checkpoint] = hash;
                    } else {
                        second_pass[checkpoint] = hash;
                    }
                    ++checkpoint;
                }
            }
        }
        int matching = 0;
        int distinct = 0;
        for (int i = 0; i < sweep_checkpoint_count; ++i) {
            if (first_pass[i] == second_pass[i]) ++matching;
            bool seen = false;
            for (int j = 0; j < i; ++j) {
                if (first_pass[j] == first_pass[i]) seen = true;
            }
            if (!seen) ++distinct;
            Line()
                .text("HASH fit sweep frame ")
                .decimal(static_cast<std::uint32_t>(sweep_checkpoints[i]))
                .text(" ")
                .hex64(first_pass[i])
                .text(" ")
                .hex64(second_pass[i])
                .text(first_pass[i] == second_pass[i] ? " same" : " DIFFERENT")
                .flush();
        }
        Line()
            .text("FIT sweep worst on-display deviation from the billboard ")
            .signed_decimal(worst_sweep_deviation)
            .text(" px over ")
            .decimal(static_cast<std::uint32_t>(fit_sweep_frames))
            .text(" cameras")
            .flush();
        expect(matching == sweep_checkpoint_count,
               "two_runs_of_the_fitted_sweep_hash_the_same_at_every_"
               "checkpoint");
        expect(distinct == sweep_checkpoint_count,
               "the_fitted_sweep_visits_a_different_picture_at_each_"
               "checkpoint");
        // §5.17 retired `the_fit_holds_at_every_camera_of_the_sweep` here, and
        // it is the clearest of the three. Three pixels is exactly what the
        // medieval knight measures, not two and not four, and four styles
        // measure five on the same sweep, for the same reason the three-row
        // bound above came apart: one correcting pass on a closed form solved
        // for a line leaves the figure's own depth behind, and a deeper figure
        // leaves more of it. A fit that holds at every camera a pan can reach
        // is a property of the path that draws, and
        // §5.5's `the_match_holds_on_both_axes_at_every_camera_of_the_sweep`
        // is that same bound of three over this same 32-camera sweep, on both
        // axes rather than height alone, and measures two for every one of the
        // seven styles. Retiring here loses nothing and stops a number read off
        // one knight from being quoted as a property of a library.
        fitted_sweep_deviation = worst_sweep_deviation;
    }
    announce_film("mesh-fit-camera-sweep", fit_sweep_frames);
    for (int frame = 0; frame < fit_sweep_frames; ++frame) {
        const int half = fit_sweep_frames / 2;
        const int step = frame < half ? frame : fit_sweep_frames - frame;
        const Focus at{0, static_cast<std::int32_t>(step) * 2 *
                              pan_units_per_frame};
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, at);
        draw_fitted_comparison_frame(sweep_archetypes);
        take_shot();
    }

    // The sixteen fitted units, with each archer drawn as an archer, directly,
    // then through the face-list cache §5.3 named as the remedy, with the
    // cache proved against the direct frame by hash rather than trusted.
    {
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        psx::start_clock();
        const std::uint32_t d0 = tick();
        draw_fitted_battle();
        const std::uint32_t d1 = tick();
        const std::uint32_t direct_us = microseconds(d1 - d0);
        const std::uint64_t direct_hash = hash_framebuffer();
        take_still("mesh-fit-sixteen");

        psx::start_clock();
        const std::uint32_t b0 = tick();
        build_unit_cache(UnitSizing::HeightFit);
        const std::uint32_t b1 = tick();
        const std::uint32_t cache_build_us = microseconds(b1 - b0);

        gpu::begin(0x7C1F);
        psx::start_clock();
        const std::uint32_t c0 = tick();
        draw_cached_battle();
        const std::uint32_t c1 = tick();
        const std::uint32_t cached_us = microseconds(c1 - c0);
        const std::uint64_t cached_hash = hash_framebuffer();

        Line()
            .text("LOAD sixteen fitted mesh units direct ")
            .decimal(direct_us)
            .text(" us fps ")
            .decimal(direct_us == 0 ? 0u : 1000000u / direct_us)
            .text("; cached ")
            .decimal(cached_us)
            .text(" us fps ")
            .decimal(cached_us == 0 ? 0u : 1000000u / cached_us)
            .text("; cache built once in ")
            .decimal(cache_build_us)
            .text(" us")
            .flush();
        Line()
            .text("LOAD cached faces submitted ")
            .decimal(static_cast<std::uint32_t>(last_cached_faces))
            .text("; a camera move re-pays the build, a still frame pays ")
            .decimal(cached_us)
            .text(" us")
            .flush();
        Line()
            .text("HASH fitted sixteen ")
            .hex64(direct_hash)
            .text(" cached ")
            .hex64(cached_hash)
            .flush();
        expect(direct_hash == cached_hash,
               "the_cached_battle_draws_the_same_frame_as_the_direct_one");
        expect(cached_us < thirty_fps_microseconds,
               "sixteen_cached_mesh_units_fit_in_a_thirtieth_of_a_second");
        if (cached_us < sixty_fps_microseconds) {
            Line()
                .text("NOTE the cached sixteen-mesh frame also fits in a "
                      "sixtieth")
                .flush();
        }
        // §5.17 retired `the_cache_rebuild_a_camera_move_forces_fits_in_a_
        // thirtieth` from here, and this one is worth being careful about,
        // because a thirtieth of a second is a real budget and not a fitted
        // number. What was fitted is the *scene*: sixteen units of whichever
        // style is embedded, and a style's figures are as many parts as its
        // author spent. Sixteen `sengoku` units are 128 parts and rebuild in
        // 25,030 µs; sixteen `nature` units are 159 and take 33,735, which
        // misses a thirtieth by four hundred microseconds. Neither figure is
        // wrong and neither number is a defect: the bound is on §5.4's
        // superseded uniform fit, and on the path that *ships* the same claim
        // is asserted **false** a few phases below, deliberately: §5.5's
        // matched rebuild costs more than a frame, which is the whole reason
        // §5.6 priced two mitigations and took one. The property that the
        // rebuild a camera move forces fits in a frame is pinned where it
        // decides something, by `and_the_rebuild_that_ends_a_pan_fits_in_a_
        // thirtieth_too`, which is mitigation A's rebuild and clears the
        // budget by a third for every one of the seven styles.
        //
        // What is asserted here instead is the only thing this number is now
        // for: the fit is the cheaper of the two constructions, which is what
        // makes it the control §5.5 is judged against.
        fitted_cache_build_us = cache_build_us;
        Line()
            .text("FIT scales ran ")
            .signed_decimal(static_cast<int>(fitted_scale_low))
            .text(" to ")
            .signed_decimal(static_cast<int>(fitted_scale_high))
            .text("/256 across every fitted scene; rails ")
            .decimal(static_cast<std::uint32_t>(mesh_scale_floor))
            .text(" and ")
            .decimal(static_cast<std::uint32_t>(mesh_scale_ceiling))
            .text(" hit ")
            .decimal(static_cast<std::uint32_t>(fitted_scales_clamped))
            .text(" times")
            .flush();
        expect(fitted_scales_clamped == 0,
               "no_fitted_scale_ever_hit_its_rails");
    }

    // -----------------------------------------------------------------------
    // 11e. The match (§5.5). §5.4's stills settled it: the fit is
    //      better and the pairs still read as visibly different creatures at
    //      the three distances. Height was matched to a pixel or two, so what
    //      differs is what height matching cannot reach: drawn width. One
    //      uniform scale makes width wear the height's screen-position
    //      correction, and that correction is different at every distance.
    //      This phase measures the defect against the sprite's own opaque
    //      silhouette, then holds the mesh to that silhouette on both axes.
    // -----------------------------------------------------------------------
    load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                camera_vertical, Focus{0, 0});
    {
        // (a) The sprites, as drawn: the opaque box inside each archetype's
        //     32x32 cell.
        //
        //     The numbers used are the **generated** ones: the art library
        //     measured them off the same converted assets it repacked into the
        //     texels `upload_art` uploaded, and emitted them into
        //     `playstation_meshes_<style>.h`. That is §5.5's rule made
        //     mechanical: a renderer reads its target out of the same file it
        //     reads its parts out of, so redrawing a sprite moves its mesh's
        //     target with it and nothing has to be found and edited.
        //
        //     The run-time measurement is kept and is now a *check* rather
        //     than the source: it reads the arrays back out of the header the
        //     hardware was fed and requires the generator's answer, computed on
        //     a host in Python from a `Converted` object, to be the same
        //     number. Two independent routes to one measurement, so a
        //     generator that had quietly started measuring something else (a
        //     different faction, the un-repacked PNG, the wrong nibble order)
        //     would fail here rather than shift a silhouette nobody re-checked.
        for (int archetype = 0; archetype < model_slots; ++archetype) {
            const art::MeshSilhouette generated =
                art::mesh_silhouette(archetype);
            const SpriteSilhouette measured = measure_sprite(archetype);
            match_silhouette[archetype] = SpriteSilhouette{
                generated.width, generated.height, generated.area};
            Line()
                .text("MESH generated silhouette ")
                .signed_decimal(generated.width)
                .text("x")
                .signed_decimal(generated.height)
                .text(" area ")
                .signed_decimal(generated.area)
                .text("; measured on the machine ")
                .signed_decimal(static_cast<int>(measured.width))
                .text("x")
                .signed_decimal(static_cast<int>(measured.height))
                .text(" area ")
                .signed_decimal(static_cast<int>(measured.area))
                .flush();
            expect(generated.width == measured.width &&
                       generated.height == measured.height &&
                       generated.area == measured.area,
                   "the_generated_silhouette_is_the_one_the_machine_measures");
        }
        // What every commissioned archetype owes its own sprite, checked on
        // the machine as well as in the generator.
        //
        // The relation is the silhouette rule's: a figure built
        // `unit_world` tall is asked for `unit_world * sw / 32` of authored
        // width, and the shipped tolerance is eight world units. The eight is
        // repeated here rather than derived, exactly as the 150-300 triangle
        // band is repeated here, because the point of running the check on the
        // machine is that it is a *second* route to the same answer: the
        // generator computes it in Python from the part tables and this
        // computes it from the header the hardware was fed.
        //
        // What is deliberately **not** checked is the shape of the sprite's
        // box. An earlier draft of this phase asserted that every modelled
        // sprite stands taller than it is wide, which was true of the knight
        // and the archer and is not a rule: the rogue's box is square (27x27)
        // and the beast's is wider than it is tall (31x25), and the
        // stormcaller's fills its cell. A mesh wears its own sprite's
        // proportions whatever they are. That is the whole of the silhouette
        // rule, so the assertion was refusing figures the rule admits.
        constexpr std::int32_t authored_width_tolerance = 8;
        const char* const* model_names = grandleon_playstation_archetype_names;
        // What the match actually requires of a sprite, and it is not what
        // this once asserted. Written when the knight and the archer were the
        // only commissioned figures, it required every modelled sprite to
        // stand taller than it is wide, which is true of those two and false
        // of the roster: the stormcaller fills its cell on both axes at 32x32,
        // the rogue is square at 27x27, and the beast is wider than tall at
        // 31x25. That was a statement about two archetypes wearing the name of
        // all eight, and it would have refused three of the six still to be
        // drawn for a property no mesh rule asks for.
        //
        // Two things are checked in its place, and they are different
        // questions. The first is that the silhouette read out of the sprite
        // is usable at all: `want_w` is `side*sw/32` and `want_h` is
        // `side*sh/32`, so a zero would ask for a figure with no extent and an
        // over-32 would ask for one larger than the billboard it is matched
        // against. The second is the relation the silhouette rule actually
        // states, that a figure is authored the width its own sprite asks for,
        // and this is the only assertion of it on the machine.
        bool every_silhouette_fits_its_cell = true;
        bool every_model_wears_its_sprites_width = true;
        int commissioned = 0;
        for (int archetype = 0; archetype < model_slots; ++archetype) {
            if (art::mesh(archetype).part_count == 0) continue;
            ++commissioned;
            const SpriteSilhouette& sprite = match_silhouette[archetype];
            select_model(archetype);
            const std::int32_t authored = model_local_width();
            const std::int32_t asked =
                (unit_world * sprite.width) / art::cell_size;
            std::int32_t off = authored - asked;
            if (off < 0) off = -off;
            Line()
                .text("MATCH sprite ")
                .text(model_names[archetype])
                .text(" opaque ")
                .signed_decimal(static_cast<int>(sprite.width))
                .text("x")
                .signed_decimal(static_cast<int>(sprite.height))
                .text(" of 32, area ")
                .signed_decimal(static_cast<int>(sprite.area))
                .text(" texels; model authored width ")
                .signed_decimal(static_cast<int>(authored))
                .text(" world units where the silhouette asks ")
                .signed_decimal(static_cast<int>(asked))
                .flush();
            if (sprite.width <= 0 || sprite.height <= 0 ||
                sprite.width > art::cell_size ||
                sprite.height > art::cell_size) {
                every_silhouette_fits_its_cell = false;
            }
            if (sprite.area <= 0 || off > authored_width_tolerance) {
                every_model_wears_its_sprites_width = false;
            }
        }
        expect(commissioned >= 2,
               "at_least_two_archetypes_are_modelled");
        expect(every_silhouette_fits_its_cell,
               "every_modelled_sprites_silhouette_fits_its_own_cell");
        expect(every_model_wears_its_sprites_width,
               "every_modelled_figure_is_authored_the_width_its_own_sprite_"
               "asks");
    }

    {
        // (b) The diagnosis, measured as pictures: each knight pair drawn
        //     alone on the backdrop (§5.4's fitted mesh, then the billboard
        //     beside it) and the framebuffer read back and counted. The
        //     claim under measurement: the uniform fit's drawn width misses
        //     the sprite's, and misses it by a different amount and even a
        //     different *sign* at the three distances, which is exactly a
        //     pair that "reads as a different creature" at every row.
        //     Then the same pairs matched, which is the fix under the same
        //     measurement.
        select_model(knight_archetype);
        gpu::begin(0x7C1F);
        int fitted_mismatch_low = 0x7FFFFFFF;
        int fitted_mismatch_high = -0x7FFFFFFF;
        int matched_worst_w = 0;
        int matched_worst_h = 0;
        int matched_worst_feet = 0;
        int matched_feet_allowed = 0;
        int target_worst = 0;
        for (int pair = 0; pair < compare_pair_count; ++pair) {
            const int column = compare_pairs[pair].column;
            const int row = compare_pairs[pair].row;
            const MeshPose pose = pose_on(column, row, 0, 0, 0, 0, 0);
            const MeshPose flat_pose = pose_on(column + 1, row, 0, 0, 0, 0, 0);
            const FootView foot = view_foot(pose.x, pose.y, pose.z);
            const int side = camera_focal * unit_world / foot.depth;

            // The billboard alone: the sprite's drawn opaque box and area.
            clear_screen();
            draw_billboard_at(flat_pose.x, flat_pose.y, flat_pose.z,
                              knight_archetype, 0);
            const Coverage sprite = measure_coverage();
            const int sprite_w = static_cast<int>(sprite.right - sprite.left);
            const int sprite_h = static_cast<int>(sprite.bottom - sprite.top);

            // What the target arithmetic says that box should be: the check
            // that side*sw/32 by side*sh/32 is the sprite's own drawn box and
            // not a third convention.
            const int target_w = (side * static_cast<int>(
                match_silhouette[knight_archetype].width)) / art::cell_size;
            const int target_h = (side * static_cast<int>(
                match_silhouette[knight_archetype].height)) / art::cell_size;
            int target_dev = sprite_w - target_w;
            if (target_dev < 0) target_dev = -target_dev;
            if (target_dev > target_worst) target_worst = target_dev;
            target_dev = sprite_h - target_h;
            if (target_dev < 0) target_dev = -target_dev;
            if (target_dev > target_worst) target_worst = target_dev;

            // §5.4's fit alone: same drawn measurement.
            clear_screen();
            draw_fitted_mesh(knight_archetype, 0, pose);
            const Coverage fitted = measure_coverage();
            const int fitted_w = static_cast<int>(fitted.right - fitted.left);
            const int fitted_h = static_cast<int>(fitted.bottom - fitted.top);
            const int mismatch = fitted_w - sprite_w;
            if (mismatch < fitted_mismatch_low) fitted_mismatch_low = mismatch;
            if (mismatch > fitted_mismatch_high) {
                fitted_mismatch_high = mismatch;
            }

            // The match: both axes held to the sprite's silhouette. The
            // *figure* width is the claim: the drawn box is wider than the
            // figure by the perspective lean an off-centre near unit honestly
            // wears, and the billboard, one flat quad at one depth, cannot
            // lean at all; both numbers are reported so neither hides.
            clear_screen();
            select_model(knight_archetype);
            const MatchedScales scales =
                build_matched(knight_archetype, pose);
            draw_mesh(knight_archetype, 0, false);
            const Coverage matched = measure_coverage();
            const int matched_w = static_cast<int>(figure_width(foot));
            const int matched_h =
                static_cast<int>(matched.bottom - matched.top);
            int deviation = matched_w - sprite_w;
            if (deviation < 0) deviation = -deviation;
            if (deviation > matched_worst_w) matched_worst_w = deviation;
            deviation = matched_h - sprite_h;
            if (deviation < 0) deviation = -deviation;
            if (deviation > matched_worst_h) matched_worst_h = deviation;
            const DrawnBox matched_box = screen_box();
            int feet = static_cast<int>(matched_box.bottom - foot.sy);
            if (feet < 0) feet = -feet;
            if (feet > matched_worst_feet) matched_worst_feet = feet;
            std::int32_t from_centre =
                matched_box.bottom - gpu::screen_height / 2;
            if (from_centre < 0) from_centre = -from_centre;
            const int feet_allowance = static_cast<int>(drawn_from_local_z(
                model_forward_reach(), scales.height, foot.depth,
                from_centre));
            if (feet_allowance > matched_feet_allowed) {
                matched_feet_allowed = feet_allowance;
            }

            Line()
                .text("MATCH row ")
                .decimal(static_cast<std::uint32_t>(row))
                .text(" sprite ")
                .signed_decimal(sprite_w)
                .text("x")
                .signed_decimal(sprite_h)
                .text(" px area ")
                .signed_decimal(static_cast<int>(sprite.area))
                .text("; fitted mesh ")
                .signed_decimal(fitted_w)
                .text("x")
                .signed_decimal(fitted_h)
                .text(" px area ")
                .signed_decimal(static_cast<int>(fitted.area))
                .text("; matched mesh figure ")
                .signed_decimal(matched_w)
                .text(" px, box ")
                .signed_decimal(
                    static_cast<int>(matched.right - matched.left))
                .text("x")
                .signed_decimal(matched_h)
                .text(" px area ")
                .signed_decimal(static_cast<int>(matched.area))
                .text(" at ")
                .signed_decimal(static_cast<int>(scales.width))
                .text("/")
                .signed_decimal(static_cast<int>(scales.height))
                .text("/256")
                .flush();
        }
        Line()
            .text("MATCH the fitted width error runs ")
            .signed_decimal(fitted_mismatch_low)
            .text(" to ")
            .signed_decimal(fitted_mismatch_high)
            .text(" px across the three rows; matched widths within ")
            .signed_decimal(matched_worst_w)
            .text(" px, heights within ")
            .signed_decimal(matched_worst_h)
            .text(" px; feet within ")
            .signed_decimal(matched_worst_feet)
            .text(" of ")
            .signed_decimal(matched_feet_allowed)
            .text(" px the figure reaches forward")
            .flush();
        expect(target_worst <= 2,
               "the_silhouette_targets_are_the_sprites_own_drawn_box");
        // §5.5's case against the uniform fit, stated as the comparison it is
        // rather than as two facts about `medieval`. What §5.5 needs is that
        // the one scale's width error is *not one error*: correcting it at one
        // distance leaves it wrong at another. The measurement of that is the
        // spread across the rows against what the match leaves.
        // `>= 6` was a constant; `scifi`'s fit runs -8 to 0 and `mythical`'s -9
        // to 1, so the sign-change spelling was medieval's shape and not the
        // claim. The span is a different error at each distance whichever side
        // of zero it falls.
        expect(fitted_mismatch_high - fitted_mismatch_low > matched_worst_w,
               "the_uniform_fits_width_error_varies_with_distance_more_than_"
               "the_matchs_whole_error");
        expect(matched_worst_w <= 2,
               "every_matched_width_is_within_two_pixels_of_the_sprites");
        expect(matched_worst_h <= 2,
               "every_matched_height_is_within_two_pixels_of_the_sprites");
        // The same bound as the fit's, and the same reason: how far below its
        // foot a unit draws is how far its own nearest box reaches toward the
        // eye. Five pixels held for `medieval` and refused `scifi`, whose
        // knight reaches eighteen world units forward against medieval's
        // sixteen and draws seven pixels low doing it.
        expect(matched_worst_feet <= matched_feet_allowed,
               "a_matched_unit_stands_no_lower_than_it_reaches_forward");
        // §5.4's fit is the control this phase exists to beat, so what it
        // measured is compared here rather than quoted in a document. Both are
        // the worst over these same three rows.
        expect(matched_worst_h <= fitted_row_height_error,
               "the_match_is_no_worse_than_the_uniform_fit_on_the_axis_the_"
               "fit_solved_for");
    }

    {
        // (c) The archer under the same rule, same measurement, all three
        //     distances. No archer term exists to tune, so this is the rule
        //     being a rule.
        int worst_w = 0;
        int worst_h = 0;
        for (int pair = 0; pair < compare_pair_count; ++pair) {
            const int column = compare_pairs[pair].column;
            const int row = compare_pairs[pair].row;
            const MeshPose pose = pose_on(column, row, 0, 0, 0, 0, 0);
            const MeshPose flat_pose = pose_on(column + 1, row, 0, 0, 0, 0, 0);

            clear_screen();
            draw_billboard_at(flat_pose.x, flat_pose.y, flat_pose.z,
                              archer_archetype, 0);
            const Coverage sprite = measure_coverage();

            clear_screen();
            select_model(archer_archetype);
            const FootView foot = view_foot(pose.x, pose.y, pose.z);
            const MatchedScales scales =
                build_matched(archer_archetype, pose);
            draw_mesh(archer_archetype, 0, false);
            const Coverage matched = measure_coverage();

            const int sprite_w = static_cast<int>(sprite.right - sprite.left);
            const int sprite_h = static_cast<int>(sprite.bottom - sprite.top);
            const int matched_w = static_cast<int>(figure_width(foot));
            const int matched_h =
                static_cast<int>(matched.bottom - matched.top);
            int deviation = matched_w - sprite_w;
            if (deviation < 0) deviation = -deviation;
            if (deviation > worst_w) worst_w = deviation;
            deviation = matched_h - sprite_h;
            if (deviation < 0) deviation = -deviation;
            if (deviation > worst_h) worst_h = deviation;
            Line()
                .text("MATCH archer row ")
                .decimal(static_cast<std::uint32_t>(row))
                .text(" sprite ")
                .signed_decimal(sprite_w)
                .text("x")
                .signed_decimal(sprite_h)
                .text(" px area ")
                .signed_decimal(static_cast<int>(sprite.area))
                .text("; matched mesh figure ")
                .signed_decimal(matched_w)
                .text(" px, box ")
                .signed_decimal(
                    static_cast<int>(matched.right - matched.left))
                .text("x")
                .signed_decimal(matched_h)
                .text(" px area ")
                .signed_decimal(static_cast<int>(matched.area))
                .text(" at ")
                .signed_decimal(static_cast<int>(scales.width))
                .text("/")
                .signed_decimal(static_cast<int>(scales.height))
                .text("/256")
                .flush();
        }
        expect(worst_w <= 2,
               "every_matched_archer_width_is_within_two_pixels_of_the_"
               "sprites");
        expect(worst_h <= 2,
               "every_matched_archer_height_is_within_two_pixels_of_the_"
               "sprites");
    }

    // The matched scene, hashed twice, then against §5.4's fitted scene: the
    // determinism claim and the negative control that the match changed the
    // picture at all.
    {
        static constexpr std::int8_t knights[compare_pair_count] = {0, 0, 0};
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        draw_matched_comparison_frame(knights);
        const std::uint64_t first = hash_framebuffer();
        take_still("mesh-match-beside-billboard");
        gpu::begin(0x7C1F);
        draw_matched_comparison_frame(knights);
        const std::uint64_t second = hash_framebuffer();
        Line()
            .text("HASH matched scene ")
            .hex64(first)
            .text(" ")
            .hex64(second)
            .flush();
        expect(first == second,
               "two_draws_of_the_matched_scene_hash_the_same_framebuffer");

        gpu::begin(0x7C1F);
        draw_fitted_comparison_frame(knights);
        const std::uint64_t fitted = hash_framebuffer();
        expect(first != fitted,
               "the_match_draws_a_different_picture_from_the_height_fit");

        // The same three row visits §5.4 photographed, re-shot matched.
        static const char* const match_labels[compare_pair_count] = {
            "mesh-match-near-row", "mesh-match-mid-row", "mesh-match-far-row"};
        for (int pair = 0; pair < compare_pair_count; ++pair) {
            const Focus at{
                cell_centre_x(compare_pairs[pair].column) + tile_world / 2,
                static_cast<std::int32_t>(compare_pairs[pair].row - 1) *
                    tile_world};
            gpu::begin(0x7C1F);
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, at);
            draw_matched_comparison_frame(knights);
            take_still(match_labels[pair]);
        }

        // The archer pairs, matched, at rest and up close.
        static constexpr std::int8_t archers[compare_pair_count] = {
            archer_archetype, archer_archetype, archer_archetype};
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        draw_matched_comparison_frame(archers);
        take_still("mesh-match-archer");
        const Focus mid{cell_centre_x(8) + tile_world / 2, 3 * tile_world};
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, mid);
        draw_matched_comparison_frame(archers);
        take_still("mesh-match-archer-mid-row");

    }

    {
        // (d) Every commission after the first two, held to exactly the checks
        //     the archer is held to and by the same measurement.
        //
        //     Written as a loop over whatever the header carries rather than a
        //     paragraph a figure. The knight and the archer have paragraphs
        //     because the rules were being *derived* from them; from here on a
        //     figure is a row in a generated table, and a renderer that needed
        //     a paragraph per archetype would be a renderer that knew about
        //     archetypes, which is the thing `art::mesh` exists to prevent.
        //     Adding a mesh to the art library therefore adds these checks and
        //     these pictures without this file being edited again.
        for (int archetype = 0; archetype < model_slots; ++archetype) {
            if (archetype == knight_archetype ||
                archetype == archer_archetype) {
                continue;
            }
            if (art::mesh(archetype).part_count == 0) continue;
            const char* const name =
                grandleon_playstation_archetype_names[archetype];
            select_model(archetype);
            const int triangles = active_face_count * 2;
            Line()
                .text("MESH ")
                .text(name)
                .text(" parts ")
                .decimal(static_cast<std::uint32_t>(active_part_count))
                .text(" triangles ")
                .decimal(static_cast<std::uint32_t>(triangles))
                .flush();
            expect(triangles >= 150 && triangles <= 300,
                   "a_later_commission_is_inside_the_150_to_300_triangle_"
                   "budget");

            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, Focus{0, 0});
            gpu::begin(0x7C1F);
            build_mesh_world(pose_on(6, 4, 0, 0, 0, 0, 0));
            transform_mesh();
            draw_mesh(archetype, 0, false);
            Line()
                .text("MESH ")
                .text(name)
                .text(" faces drawn ")
                .decimal(static_cast<std::uint32_t>(last_mesh_faces_drawn))
                .text(" of ")
                .decimal(static_cast<std::uint32_t>(active_face_count))
                .flush();
            expect(last_mesh_faces_drawn > 0 &&
                       last_mesh_faces_drawn < active_face_count,
                   "a_later_commissions_back_face_test_keeps_some_faces_and_"
                   "drops_others");

            int worst_w = 0;
            int worst_h = 0;
            for (int pair = 0; pair < compare_pair_count; ++pair) {
                const int column = compare_pairs[pair].column;
                const int row = compare_pairs[pair].row;
                const MeshPose pose = pose_on(column, row, 0, 0, 0, 0, 0);
                const MeshPose flat_pose =
                    pose_on(column + 1, row, 0, 0, 0, 0, 0);

                clear_screen();
                draw_billboard_at(flat_pose.x, flat_pose.y, flat_pose.z,
                                  archetype, 0);
                const Coverage sprite = measure_coverage();

                clear_screen();
                select_model(archetype);
                const FootView foot = view_foot(pose.x, pose.y, pose.z);
                const MatchedScales scales = build_matched(archetype, pose);
                draw_mesh(archetype, 0, false);
                const Coverage matched = measure_coverage();

                const int sprite_w =
                    static_cast<int>(sprite.right - sprite.left);
                const int sprite_h =
                    static_cast<int>(sprite.bottom - sprite.top);
                const int matched_w = static_cast<int>(figure_width(foot));
                const int matched_h =
                    static_cast<int>(matched.bottom - matched.top);
                int deviation = matched_w - sprite_w;
                if (deviation < 0) deviation = -deviation;
                if (deviation > worst_w) worst_w = deviation;
                deviation = matched_h - sprite_h;
                if (deviation < 0) deviation = -deviation;
                if (deviation > worst_h) worst_h = deviation;
                Line()
                    .text("MATCH ")
                    .text(name)
                    .text(" row ")
                    .decimal(static_cast<std::uint32_t>(row))
                    .text(" sprite ")
                    .signed_decimal(sprite_w)
                    .text("x")
                    .signed_decimal(sprite_h)
                    .text(" px area ")
                    .signed_decimal(static_cast<int>(sprite.area))
                    .text("; matched mesh figure ")
                    .signed_decimal(matched_w)
                    .text(" px, box ")
                    .signed_decimal(
                        static_cast<int>(matched.right - matched.left))
                    .text("x")
                    .signed_decimal(matched_h)
                    .text(" px area ")
                    .signed_decimal(static_cast<int>(matched.area))
                    .text(" at ")
                    .signed_decimal(static_cast<int>(scales.width))
                    .text("/")
                    .signed_decimal(static_cast<int>(scales.height))
                    .text("/256")
                    .flush();
            }
            expect(worst_w <= 2,
                   "every_matched_width_of_a_later_commission_is_within_two_"
                   "pixels_of_its_sprites");
            expect(worst_h <= 2,
                   "every_matched_height_of_a_later_commission_is_within_two_"
                   "pixels_of_its_sprites");

            // The picture, at the three distances the roster is judged at, and
            // named after the archetype so a still cannot be attributed to the
            // wrong figure.
            const std::int8_t seating[compare_pair_count] = {
                static_cast<std::int8_t>(archetype),
                static_cast<std::int8_t>(archetype),
                static_cast<std::int8_t>(archetype)};
            gpu::begin(0x7C1F);
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, Focus{0, 0});
            draw_matched_comparison_frame(seating);
            Line()
                .text("SHOT ")
                .decimal(static_cast<std::uint32_t>(shots_taken))
                .text(" still mesh-match-")
                .text(name)
                .flush();
            take_shot();
        }
        select_model(knight_archetype);
    }

    // The matched sweep: §5.4's 32 cameras and archetype seating, under the
    // silhouette rule, hashed at the same five checkpoints over two runs and
    // the match re-measured on both axes at every camera it is visible from.
    {
        static constexpr std::int8_t sweep_archetypes[compare_pair_count] = {
            0, archer_archetype, 0};
        constexpr int match_sweep_frames = 32;
        constexpr int sweep_checkpoints[] = {0, 5, 10, 16, 23};
        constexpr int sweep_checkpoint_count =
            static_cast<int>(sizeof(sweep_checkpoints) /
                             sizeof(sweep_checkpoints[0]));
        std::uint64_t first_pass[sweep_checkpoint_count] = {};
        std::uint64_t second_pass[sweep_checkpoint_count] = {};
        int worst_sweep_deviation = 0;
        for (int pass = 0; pass < 2; ++pass) {
            int checkpoint = 0;
            for (int frame = 0; frame < match_sweep_frames; ++frame) {
                const int half = match_sweep_frames / 2;
                const int step =
                    frame < half ? frame : match_sweep_frames - frame;
                const Focus at{0, static_cast<std::int32_t>(step) * 2 *
                                      pan_units_per_frame};
                gpu::begin(0x7C1F);
                load_camera(camera_pitch_degrees, camera_distance,
                            camera_focal, camera_vertical, at);
                draw_matched_comparison_frame(sweep_archetypes);
                if (pass == 0) {
                    for (int pair = 0; pair < compare_pair_count; ++pair) {
                        const MeshPose pose =
                            pose_on(compare_pairs[pair].column,
                                    compare_pairs[pair].row, 0, 0, 0, 0, 0);
                        const FootView foot =
                            view_foot(pose.x, pose.y, pose.z);
                        if (foot.sy < 0 || foot.sy >= gpu::screen_height) {
                            continue;
                        }
                        const int archetype = sweep_archetypes[pair];
                        select_model(archetype);
                        (void)build_matched(archetype, pose);
                        const DrawnBox box = screen_box();
                        const int side =
                            camera_focal * unit_world / foot.depth;
                        const SpriteSilhouette& sprite =
                            match_silhouette[archetype];
                        int deviation =
                            static_cast<int>(box.bottom - box.top) -
                            (side * static_cast<int>(sprite.height)) /
                                art::cell_size;
                        if (deviation < 0) deviation = -deviation;
                        if (deviation > worst_sweep_deviation) {
                            worst_sweep_deviation = deviation;
                        }
                        deviation =
                            static_cast<int>(figure_width(foot)) -
                            (side * static_cast<int>(sprite.width)) /
                                art::cell_size;
                        if (deviation < 0) deviation = -deviation;
                        if (deviation > worst_sweep_deviation) {
                            worst_sweep_deviation = deviation;
                        }
                    }
                }
                if (checkpoint < sweep_checkpoint_count &&
                    frame == sweep_checkpoints[checkpoint]) {
                    const std::uint64_t hash = hash_framebuffer();
                    if (pass == 0) {
                        first_pass[checkpoint] = hash;
                    } else {
                        second_pass[checkpoint] = hash;
                    }
                    ++checkpoint;
                }
            }
        }
        int matching = 0;
        int distinct = 0;
        for (int i = 0; i < sweep_checkpoint_count; ++i) {
            if (first_pass[i] == second_pass[i]) ++matching;
            bool seen = false;
            for (int j = 0; j < i; ++j) {
                if (first_pass[j] == first_pass[i]) seen = true;
            }
            if (!seen) ++distinct;
            Line()
                .text("HASH match sweep frame ")
                .decimal(static_cast<std::uint32_t>(sweep_checkpoints[i]))
                .text(" ")
                .hex64(first_pass[i])
                .text(" ")
                .hex64(second_pass[i])
                .text(first_pass[i] == second_pass[i] ? " same" : " DIFFERENT")
                .flush();
        }
        Line()
            .text("MATCH sweep worst on-display deviation from the sprites "
                  "silhouette ")
            .signed_decimal(worst_sweep_deviation)
            .text(" px over ")
            .decimal(static_cast<std::uint32_t>(match_sweep_frames))
            .text(" cameras, box height and figure width")
            .flush();
        expect(matching == sweep_checkpoint_count,
               "two_runs_of_the_matched_sweep_hash_the_same_at_every_"
               "checkpoint");
        expect(distinct == sweep_checkpoint_count,
               "the_matched_sweep_visits_a_different_picture_at_each_"
               "checkpoint");
        matched_sweep_deviation = worst_sweep_deviation;
        expect(worst_sweep_deviation <= 3,
               "the_match_holds_on_both_axes_at_every_camera_of_the_sweep");
        // The retired §5.4 sweep bound's replacement: the fitted sweep is the
        // control, so what it now has to show is that it is the worse of the
        // two over the identical 32 cameras. No constant, and true for all
        // seven styles: the fit runs three to five where the match runs two.
        expect(fitted_sweep_deviation >= worst_sweep_deviation,
               "the_uniform_fit_never_beats_the_match_over_the_same_sweep");

        announce_film("mesh-match-camera-sweep", match_sweep_frames);
        for (int frame = 0; frame < match_sweep_frames; ++frame) {
            const int half = match_sweep_frames / 2;
            const int step = frame < half ? frame : match_sweep_frames - frame;
            const Focus at{0, static_cast<std::int32_t>(step) * 2 *
                                  pan_units_per_frame};
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, at);
            draw_matched_comparison_frame(sweep_archetypes);
            take_shot();
        }
    }

    // The sixteen, matched: the "one army" picture under the silhouette rule,
    // with the frame timed for the honest ledger, not gated. The matched build
    // is §5.4's fit plus one more correcting pass.
    {
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        psx::start_clock();
        const std::uint32_t m0 = tick();
        draw_matched_battle();
        const std::uint32_t m1 = tick();
        const std::uint32_t matched_us = microseconds(m1 - m0);
        take_still("mesh-match-sixteen");
        Line()
            .text("LOAD sixteen matched mesh units direct ")
            .decimal(matched_us)
            .text(" us fps ")
            .decimal(matched_us == 0 ? 0u : 1000000u / matched_us)
            .text("; the face-list cache applies to the match unchanged")
            .flush();
        Line()
            .text("MATCH scales ran ")
            .signed_decimal(static_cast<int>(matched_scale_low))
            .text(" to ")
            .signed_decimal(static_cast<int>(matched_scale_high))
            .text("/256 across every matched scene, both axes; rails ")
            .decimal(static_cast<std::uint32_t>(mesh_scale_floor))
            .text(" and ")
            .decimal(static_cast<std::uint32_t>(mesh_scale_ceiling))
            .text(" hit ")
            .decimal(static_cast<std::uint32_t>(matched_scales_clamped))
            .text(" times")
            .flush();
        expect(matched_scales_clamped == 0,
               "no_matched_scale_ever_hit_its_rails");
    }

    // -----------------------------------------------------------------------
    // 11f. The face-list cache on the silhouette match (§5.6, wave 1).
    //
    //      §5.5 wrote that "the face-list cache applies to the match
    //      unchanged". That was a claim: the cache §5.4 built was built from
    //      `build_fitted`, nothing had ever cached a matched unit, and nothing
    //      had hashed one against the frame it is supposed to be identical to.
    //      This phase makes it a measurement, and the load-bearing assertion is
    //      the hash rather than the clock: **a cache is an optimisation, not a
    //      different picture.** A cache that were merely fast would be a
    //      renderer bug wearing a speed-up.
    // -----------------------------------------------------------------------
    std::uint32_t matched_cached_us = 0;
    std::uint32_t matched_direct_us = 0;
    {
        const int rails_before = matched_scales_clamped;
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        psx::start_clock();
        const std::uint32_t d0 = tick();
        draw_matched_battle();
        const std::uint32_t d1 = tick();
        matched_direct_us = microseconds(d1 - d0);
        const std::uint64_t direct_hash = hash_framebuffer();

        psx::start_clock();
        const std::uint32_t b0 = tick();
        build_unit_cache(UnitSizing::SilhouetteMatch);
        const std::uint32_t b1 = tick();
        const std::uint32_t build_us = microseconds(b1 - b0);

        gpu::begin(0x7C1F);
        psx::start_clock();
        const std::uint32_t c0 = tick();
        draw_cached_battle();
        const std::uint32_t c1 = tick();
        matched_cached_us = microseconds(c1 - c0);
        const std::uint64_t cached_hash = hash_framebuffer();
        // No still is taken here on purpose: the cached frame is asserted
        // bit-identical to `mesh-match-sixteen.png` below, so a second file
        // would be that file under a second name.
        Line()
            .text("CACHE sixteen matched mesh units direct ")
            .decimal(matched_direct_us)
            .text(" us fps ")
            .decimal(matched_direct_us == 0 ? 0u
                                            : 1000000u / matched_direct_us)
            .text("; cached ")
            .decimal(matched_cached_us)
            .text(" us fps ")
            .decimal(matched_cached_us == 0 ? 0u
                                            : 1000000u / matched_cached_us)
            .text("; cache built once in ")
            .decimal(build_us)
            .text(" us")
            .flush();
        Line()
            .text("CACHE matched faces submitted ")
            .decimal(static_cast<std::uint32_t>(last_cached_faces))
            .flush();
        Line()
            .text("HASH matched sixteen direct ")
            .hex64(direct_hash)
            .text(" cached ")
            .hex64(cached_hash)
            .text(direct_hash == cached_hash ? " identical" : " DIFFERENT")
            .flush();
        expect(direct_hash == cached_hash,
               "the_cached_matched_battle_is_bit_identical_to_the_direct_one");
        expect(matched_cached_us < thirty_fps_microseconds,
               "sixteen_cached_matched_mesh_units_fit_in_a_thirtieth_of_a_"
               "second");
        expect(matched_direct_us > thirty_fps_microseconds,
               "the_direct_matched_frame_is_the_one_that_does_not_fit");
        if (matched_cached_us < sixty_fps_microseconds) {
            Line()
                .text("NOTE the cached matched sixteen-mesh frame also fits in "
                      "a sixtieth")
                .flush();
        }
        // The honest half, and the reason the two mitigations below exist: the
        // rebuild a camera move forces under the match is not a frame. §5.4's
        // fitted rebuild was 29,622 µs and did fit; this one is the matched
        // direct frame plus the winding pass, and it does not. Pinned as the
        // truth it is rather than left unasserted.
        Line()
            .text("CACHE the matched rebuild a camera move forces is ")
            .decimal(build_us)
            .text(" us, ")
            .decimal(build_us > thirty_fps_microseconds ? 1u : 0u)
            .text(" = past a thirtieth")
            .flush();
        expect(build_us > thirty_fps_microseconds,
               "the_matched_cache_rebuild_is_what_the_mitigations_are_for");
        // The replacement for §5.4's retired thirtieth-of-a-second bound on the
        // fitted rebuild. That number's only remaining job is to say that the
        // uniform fit is the cheaper construction and therefore the control,
        // and that is a comparison on this run rather than a budget.
        expect(fitted_cache_build_us < build_us,
               "the_uniform_fits_rebuild_is_the_cheaper_of_the_two");
        expect(matched_scales_clamped == rails_before,
               "the_matched_cache_touched_no_scale_rail");
    }

    // -----------------------------------------------------------------------
    // 11g. The two mitigations §5.4 named for the camera-move invalidation,
    //      priced (§5.6, wave 1).
    //
    //      §5.4: "skip the correction while panning (the analytic scale alone
    //      is within nine pixels, in motion), or quantise the scale so small
    //      pans invalidate nothing." Neither was priced. Both are here, each
    //      measured on the two axes that decide it: what it saves, and what
    //      silhouette error it costs against the sprite the match exists to
    //      wear.
    // -----------------------------------------------------------------------

    // Mitigation A. One build from the closed form, no correcting pass, no
    // re-transform. That is where the matched frame's cost over the fitted one
    // went.
    std::uint32_t analytic_direct_us = 0;
    int analytic_worst_deviation = 0;
    int analytic_worst_target = 0;
    int analytic_smallest_target = 0;
    int analytic_worst_spread = 0;
    int analytic_worst_excess = -0x7FFFFFFF;
    {
        const int rails_before = matched_scales_clamped;
        matched_refine_budget = 1;
        gpu::begin(0x7C1F);
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        psx::start_clock();
        const std::uint32_t a0 = tick();
        draw_matched_battle();
        const std::uint32_t a1 = tick();
        analytic_direct_us = microseconds(a1 - a0);

        psx::start_clock();
        const std::uint32_t b0 = tick();
        build_unit_cache(UnitSizing::SilhouetteMatch);
        const std::uint32_t b1 = tick();
        const std::uint32_t build_us = microseconds(b1 - b0);

        // What it costs, on the measurement §5.5 judges the match by: the
        // drawn box height and the figure width against the sprite's own
        // silhouette, at every camera of the same 32-camera sweep, for the
        // same three pairs.
        static constexpr std::int8_t sweep_archetypes[compare_pair_count] = {
            0, archer_archetype, 0};
        constexpr int sweep_frames = 32;
        for (int frame = 0; frame < sweep_frames; ++frame) {
            const int half = sweep_frames / 2;
            const int step = frame < half ? frame : sweep_frames - frame;
            const Focus at{0, static_cast<std::int32_t>(step) * 2 *
                                  pan_units_per_frame};
            load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                        camera_vertical, at);
            for (int pair = 0; pair < compare_pair_count; ++pair) {
                const MeshPose pose =
                    pose_on(compare_pairs[pair].column,
                            compare_pairs[pair].row, 0, 0, 0, 0, 0);
                const FootView foot = view_foot(pose.x, pose.y, pose.z);
                if (foot.sy < 0 || foot.sy >= gpu::screen_height) continue;
                const int archetype = sweep_archetypes[pair];
                select_model(archetype);
                const MatchedScales scales = build_matched(archetype, pose);
                const DrawnBox box = screen_box();
                const int side = camera_focal * unit_world / foot.depth;
                const SpriteSilhouette& sprite = match_silhouette[archetype];
                // The figure's own depth, drawn: what a scale solved for a
                // *line* cannot know about a figure made of boxes, and
                // therefore what the correcting pass this mitigation skips was
                // removing. Reported beside the error so the two can be read
                // against each other.
                // How far up the display this figure reaches, which is what
                // decides the magnification half of the departure above.
                const std::int32_t centre = gpu::screen_height / 2;
                std::int32_t high = box.top - centre;
                if (high < 0) high = -high;
                std::int32_t low = box.bottom - centre;
                if (low < 0) low = -low;
                const int spread = static_cast<int>(drawn_from_local_z(
                    model_depth_spread(), scales.height, foot.depth,
                    high > low ? high : low));
                if (spread > analytic_worst_spread) {
                    analytic_worst_spread = spread;
                }
                int excess = -spread;
                const int target_h =
                    (side * static_cast<int>(sprite.height)) / art::cell_size;
                const int target_w =
                    (side * static_cast<int>(sprite.width)) / art::cell_size;
                int deviation = static_cast<int>(box.bottom - box.top) -
                                target_h;
                if (deviation < 0) deviation = -deviation;
                if (deviation - spread > excess) excess = deviation - spread;
                if (deviation > analytic_worst_deviation) {
                    analytic_worst_deviation = deviation;
                    analytic_worst_target = target_h;
                }
                deviation = static_cast<int>(figure_width(foot)) - target_w;
                if (deviation < 0) deviation = -deviation;
                if (deviation - spread > excess) excess = deviation - spread;
                if (deviation > analytic_worst_deviation) {
                    analytic_worst_deviation = deviation;
                    analytic_worst_target = target_w;
                }
                // By how much this sample's error overran the figure's own
                // drawn depth, which is the bound below. Kept per sample, so a
                // near-row error is never excused by a far-row allowance.
                if (excess > analytic_worst_excess) {
                    analytic_worst_excess = excess;
                }
                // The smallest silhouette any camera of the sweep asks for,
                // reported so the error can be read as a fraction of a figure
                // rather than as a number of pixels.
                if (analytic_smallest_target == 0 ||
                    target_h < analytic_smallest_target) {
                    analytic_smallest_target = target_h;
                }
                if (target_w < analytic_smallest_target) {
                    analytic_smallest_target = target_w;
                }
            }
        }
        matched_refine_budget = 3;
        Line()
            .text("MITIGATE A analytic scales only: frame ")
            .decimal(analytic_direct_us)
            .text(" us fps ")
            .decimal(analytic_direct_us == 0 ? 0u
                                             : 1000000u / analytic_direct_us)
            .text("; cache rebuild ")
            .decimal(build_us)
            .text(" us; worst silhouette deviation ")
            .signed_decimal(analytic_worst_deviation)
            .text(" px of a ")
            .signed_decimal(analytic_worst_target)
            .text(" px target over 32 cameras, against the refined match's ")
            .signed_decimal(matched_sweep_deviation)
            .text(" px")
            .flush();
        // The bound, on its own line so neither can crowd the other out of a
        // 220-character transcript line.
        Line()
            .text("MITIGATE A the error against the figure's own drawn depth: "
                  "worst overrun ")
            .signed_decimal(analytic_worst_excess)
            .text(" px; deepest figure drawn ")
            .signed_decimal(analytic_worst_spread)
            .text(" px; smallest silhouette the sweep asks for ")
            .signed_decimal(analytic_smallest_target)
            .text(" px")
            .flush();
        expect(analytic_direct_us < matched_direct_us,
               "skipping_the_correction_makes_the_panning_frame_cheaper");
        // The whole point of the mitigation, and the number that decides it:
        // the panning frame cannot use the cache, because the camera is moving,
        // and it clears 30 fps on its own.
        expect(analytic_direct_us < thirty_fps_microseconds,
               "the_analytic_only_panning_frame_fits_in_a_thirtieth");
        // And the rebuild the pan's last frame pays, so that the camera
        // settling costs one frame rather than a stall.
        expect(build_us < thirty_fps_microseconds,
               "and_the_rebuild_that_ends_a_pan_fits_in_a_thirtieth_too");
        // What it costs, bounded on both sides, and neither side is a constant
        // any more.
        //
        // The lower side says the refinement was removing something real, and
        // "something real" means more than the refined match's own error over
        // this same sweep, which the match phase measured a few pages up rather
        // than a two somebody typed.
        expect(analytic_worst_deviation > matched_sweep_deviation,
               "and_it_costs_silhouette_error_the_refinement_was_removing");
        // The upper side is what twelve pixels was reaching for and could not
        // say, and the quantity it wanted is not a number of pixels at all.
        //
        // What the skipped pass was removing is exactly one thing: the closed
        // form solves a scale for a **line at the foot**, and a mesh is a stack
        // of boxes with depth. A box at z draws focal*sin(phi)*z/depth lower
        // than the line does, so a figure's silhouette departs from the line's
        // by its own authored depth, drawn at whatever scale and distance it is
        // being drawn at. That is the bound: the analytic-only error may be as
        // large as the figure's depth and no larger, per sample, and it needs
        // no constant because the figure supplies it.
        //
        // Twelve was that quantity for the medieval knight and nobody noticed
        // it was a quantity. A deeper figure is allowed more and takes more.
        expect(analytic_worst_excess <= 0,
               "the_analytic_error_is_the_figures_own_depth_and_no_more");
        expect(matched_scales_clamped == rails_before,
               "the_analytic_only_scales_touched_no_rail");
    }

    // Mitigation B. Round both scales to a step, so a pan that moves a unit a
    // little moves its scale not at all and the cache built for the old camera
    // is the cache the new one wants. What is measured is the thing that
    // decides it: over the same 32-camera sweep, how many camera moves leave
    // every unit's cache entry byte-for-byte the entry it already had.
    {
        const int rails_before = matched_scales_clamped;
        constexpr int quanta[] = {0, 16, 32, 64};
        constexpr int quantum_count =
            static_cast<int>(sizeof(quanta) / sizeof(quanta[0]));
        constexpr int sweep_frames = 32;
        int survived_at[quantum_count] = {};
        int deviation_at[quantum_count] = {};
        int scale_moved_at[quantum_count] = {};
        int faces_moved_at[quantum_count] = {};
        for (int q = 0; q < quantum_count; ++q) {
            matched_scale_quantum = quanta[q];
            CacheSignature previous[unit_count];
            int survived = 0;
            int worst = 0;
            int entries = 0;
            int scale_moved = 0;
            int faces_moved = 0;
            for (int frame = 0; frame < sweep_frames; ++frame) {
                const int half = sweep_frames / 2;
                const int step = frame < half ? frame : sweep_frames - frame;
                const Focus at{0, static_cast<std::int32_t>(step) * 2 *
                                      pan_units_per_frame};
                load_camera(camera_pitch_degrees, camera_distance,
                            camera_focal, camera_vertical, at);
                build_unit_cache(UnitSizing::SilhouetteMatch);
                bool intact = true;
                for (int i = 0; i < unit_count; ++i) {
                    const CacheSignature now = signature_of(i);
                    if (frame > 0) {
                        // Separated on purpose, because the two causes have
                        // completely different answers. A scale that moved is
                        // what quantising is *for*. A visible face list that
                        // moved is what quantising cannot reach at any step:
                        // the camera translated, so a box off to the side of
                        // the frustum starts showing a face it was hiding, and
                        // no amount of rounding the scale prevents that.
                        const bool scale_change =
                            now.scale_w != previous[i].scale_w ||
                            now.scale_h != previous[i].scale_h;
                        const bool face_change =
                            now.faces != previous[i].faces ||
                            now.face_hash != previous[i].face_hash;
                        ++entries;
                        if (scale_change) ++scale_moved;
                        if (face_change) ++faces_moved;
                        if (scale_change || face_change) intact = false;
                    }
                    previous[i] = now;
                }
                if (frame > 0 && intact) ++survived;

                // What the rounding costs, on the same measurement as above.
                for (int pair = 0; pair < compare_pair_count; ++pair) {
                    const MeshPose pose =
                        pose_on(compare_pairs[pair].column,
                                compare_pairs[pair].row, 0, 0, 0, 0, 0);
                    const FootView foot = view_foot(pose.x, pose.y, pose.z);
                    if (foot.sy < 0 || foot.sy >= gpu::screen_height) continue;
                    const int archetype = pair == 1 ? archer_archetype : 0;
                    select_model(archetype);
                    (void)build_matched(archetype, pose);
                    const DrawnBox box = screen_box();
                    const int side = camera_focal * unit_world / foot.depth;
                    const SpriteSilhouette& sprite =
                        match_silhouette[archetype];
                    int deviation = static_cast<int>(box.bottom - box.top) -
                                    (side * static_cast<int>(sprite.height)) /
                                        art::cell_size;
                    if (deviation < 0) deviation = -deviation;
                    if (deviation > worst) worst = deviation;
                    deviation = static_cast<int>(figure_width(foot)) -
                                (side * static_cast<int>(sprite.width)) /
                                    art::cell_size;
                    if (deviation < 0) deviation = -deviation;
                    if (deviation > worst) worst = deviation;
                }
            }
            survived_at[q] = survived;
            deviation_at[q] = worst;
            scale_moved_at[q] = scale_moved;
            faces_moved_at[q] = faces_moved;
            Line()
                .text("MITIGATE B quantum ")
                .decimal(static_cast<std::uint32_t>(quanta[q]))
                .text("/256: ")
                .decimal(static_cast<std::uint32_t>(survived))
                .text(" of ")
                .decimal(static_cast<std::uint32_t>(sweep_frames - 1))
                .text(" camera moves invalidate nothing")
                .flush();
            Line()
                .text("MITIGATE B quantum ")
                .decimal(static_cast<std::uint32_t>(quanta[q]))
                .text("/256: of ")
                .decimal(static_cast<std::uint32_t>(entries))
                .text(" unit caches carried across a camera move, ")
                .decimal(static_cast<std::uint32_t>(scale_moved))
                .text(" lost their scale and ")
                .decimal(static_cast<std::uint32_t>(faces_moved))
                .text(" lost their face list; worst silhouette deviation ")
                .signed_decimal(worst)
                .text(" px")
                .flush();
        }
        matched_scale_quantum = 0;
        load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                    camera_vertical, Focus{0, 0});
        // The negative control the whole mitigation rests on: with no
        // quantisation, a camera move invalidates the cache every single time,
        // which is exactly what §5.4 said and what makes the rounding worth
        // asking about at all.
        expect(survived_at[0] == 0,
               "unquantised_every_camera_move_invalidates_every_unit");
        // The verdict, and it is a refusal. Quantising does what it promises:
        // the coarser the step, the fewer scales move. It still buys nothing,
        // because the *other* cause it cannot touch survives every step: a
        // camera translation changes which faces a box shows. So the
        // whole-frame survival stays at zero at every quantum tried, and this
        // is the assertion that says so rather than a paragraph claiming it.
        expect(scale_moved_at[quantum_count - 1] < scale_moved_at[0],
               "quantising_does_hold_more_scales_still_the_coarser_it_gets");
        expect(faces_moved_at[quantum_count - 1] > 0,
               "and_the_face_list_moves_under_a_pan_whatever_the_scale_does");
        expect(survived_at[quantum_count - 1] == 0,
               "so_no_quantum_tried_lets_one_whole_frame_reuse_its_cache");
        expect(deviation_at[quantum_count - 1] > deviation_at[0],
               "while_the_rounding_is_paid_for_in_silhouette_error");
        expect(matched_scales_clamped == rails_before,
               "no_quantised_scale_hit_a_rail");
    }

    // -----------------------------------------------------------------------
    // 11b. The commission, looked at where it is actually drawn
    //
    // The mesh rules in `meshes/rules.py` are necessary and not sufficient: a
    // `mage` passed every one of them and was still not shipped, because it
    // did not read at the size a board draws it. A commission therefore
    // needs a picture as much as it needs a pass, and this is that picture:
    // every archetype the *generator* commissioned, not a list typed here,
    // matched to its own sprite's silhouette and stood beside its own
    // billboard on the board. The rows run near to far, so the same picture
    // also says whether a figure that reads at one distance still reads at
    // another, which is a question §5.4 had to learn to ask.
    //
    // It carries §5.5's own measurement while it is there, over whatever the
    // commission holds rather than over the two archetypes this program names
    // by hand: the rule has no per-archetype term anywhere, so a new figure
    // either lands inside the same two pixels or the rule was never a rule.
    // -----------------------------------------------------------------------
    {
        // Two pairs a row, both columns astride the board's centre line. A
        // figure far off centre *leans*, which is §5.5's finding and 3D truth
        // rather than a defect, so a gallery that ran along one row would be
        // judging its last archetype through a lean its first never wears.
        static constexpr int gallery_rows[4] = {2, 4, 6, 8};
        static constexpr int gallery_columns = 2;
        int worst_w = 0;
        int worst_h = 0;
        int shown = 0;
        for (int archetype = 0; archetype < model_slots; ++archetype) {
            if (art::mesh(archetype).part_count == 0) continue;
            const int row = gallery_rows[(shown / gallery_columns) %
                                         (sizeof(gallery_rows) /
                                          sizeof(gallery_rows[0]))];
            const int column = 4 + (shown % gallery_columns) * 2;
            ++shown;

            clear_screen();
            const MeshPose flat = pose_on(column + 1, row, 0, 0, 0, 0, 0);
            draw_billboard_at(flat.x, flat.y, flat.z, archetype, 0);
            const Coverage sprite = measure_coverage();

            clear_screen();
            const MeshPose pose = pose_on(column, row, 0, 0, 0, 0, 0);
            select_model(archetype);
            const FootView foot = view_foot(pose.x, pose.y, pose.z);
            const MatchedScales scales = build_matched(archetype, pose);
            draw_mesh(archetype, 0, false);
            const Coverage drawn = measure_coverage();

            const int sprite_w = static_cast<int>(sprite.right - sprite.left);
            const int sprite_h = static_cast<int>(sprite.bottom - sprite.top);
            const int mesh_w = static_cast<int>(figure_width(foot));
            const int mesh_h = static_cast<int>(drawn.bottom - drawn.top);
            int deviation = mesh_w - sprite_w;
            if (deviation < 0) deviation = -deviation;
            if (deviation > worst_w) worst_w = deviation;
            deviation = mesh_h - sprite_h;
            if (deviation < 0) deviation = -deviation;
            if (deviation > worst_h) worst_h = deviation;

            Line()
                .text("COMMISSION archetype ")
                .decimal(static_cast<std::uint32_t>(archetype))
                .text(": ")
                .decimal(static_cast<std::uint32_t>(
                    art::mesh(archetype).part_count))
                .text(" parts, ")
                .decimal(static_cast<std::uint32_t>(
                    art::mesh(archetype).part_count * mesh_faces_per_box * 2))
                .text(" triangles; sprite ")
                .signed_decimal(sprite_w)
                .text("x")
                .signed_decimal(sprite_h)
                .text(" px, mesh figure ")
                .signed_decimal(mesh_w)
                .text(" px, box ")
                .signed_decimal(static_cast<int>(drawn.right - drawn.left))
                .text("x")
                .signed_decimal(mesh_h)
                .text(" px at ")
                .signed_decimal(static_cast<int>(scales.width))
                .text("/")
                .signed_decimal(static_cast<int>(scales.height))
                .text("/256")
                .flush();
        }
        Line()
            .text("COMMISSION ")
            .decimal(static_cast<std::uint32_t>(shown))
            .text(" of ")
            .decimal(static_cast<std::uint32_t>(art::archetype_count))
            .text(" archetypes are modelled; worst width error ")
            .signed_decimal(worst_w)
            .text(" px, worst height error ")
            .signed_decimal(worst_h)
            .text(" px")
            .flush();
        expect(shown >= 2, "the_commission_holds_more_than_one_figure");
        expect(worst_w <= 2,
               "every_commissioned_figure_is_within_two_pixels_of_its_own_"
               "sprites_width");
        expect(worst_h <= 2,
               "every_commissioned_figure_is_within_two_pixels_of_its_own_"
               "sprites_height");

        // The picture. The board, then each commissioned figure with its own
        // billboard next to it, row by row so the terrain in front of a unit is
        // drawn after it.
        transform_world(cached_vertex_count);
        refresh_packets();
        clear_screen();
        int from = 0;
        for (int step = 0; step < board_rows; ++step) {
            const int row = board_rows - 1 - step;
            submit_packet_range(from, plan_row_end[step]);
            from = plan_row_end[step];
            int placed = 0;
            for (int archetype = 0; archetype < model_slots; ++archetype) {
                if (art::mesh(archetype).part_count == 0) continue;
                const int at_row =
                    gallery_rows[(placed / gallery_columns) %
                                 (sizeof(gallery_rows) /
                                  sizeof(gallery_rows[0]))];
                const int column = 4 + (placed % gallery_columns) * 2;
                ++placed;
                if (at_row != row) continue;
                select_model(archetype);
                draw_matched_mesh(archetype, 0,
                                  pose_on(column, row, 0, 0, 0, 0, 0));
                const MeshPose beside = pose_on(column + 1, row, 0, 0, 0, 0, 0);
                draw_billboard_at(beside.x, beside.y, beside.z, archetype, 0);
            }
        }
        take_still("mesh-commission");
        select_model(knight_archetype);
    }

    // -----------------------------------------------------------------------
    // 12. The themed, non-playable surround.
    // -----------------------------------------------------------------------
    {
        const int cluts_before = cluts_used;
        const int cells_before = cells_used;
        upload_surround_cluts();
        psx::start_clock();
        const std::uint32_t g0 = tick();
        build_field_world();
        build_field_plan();
        const std::uint32_t g1 = tick();
        Line()
            .text("SURROUND build once ")
            .decimal(microseconds(g1 - g0))
            .text(" us for ")
            .decimal(static_cast<std::uint32_t>(field_vertex_count))
            .text(" world vertices and ")
            .decimal(static_cast<std::uint32_t>(field_rows * field_columns))
            .text(" cell plans; paid when the map is loaded, never in a frame")
            .flush();
        // The field chooses a cell's interior variant from its own coordinate
        // rather than the board's, so that a surround ring varies. That is only
        // allowed if a board cell still gets the variant it gets today, which
        // holds exactly when four times the ring count is a multiple of the
        // variant count. Asserted rather than reasoned about in a comment.
        bool variants_agree = true;
        for (int row = 0; row < board_rows; ++row) {
            for (int column = 0; column < board_columns; ++column) {
                if (terrain_variant(column + surround_rings,
                                    row + surround_rings) !=
                    terrain_variant(column, row)) {
                    variants_agree = false;
                }
            }
        }
        expect(variants_agree,
               "the_surround_does_not_change_which_variant_a_board_cell_"
               "draws");
        Line()
            .text("SURROUND rings ")
            .decimal(static_cast<std::uint32_t>(surround_rings))
            .text(" field ")
            .decimal(static_cast<std::uint32_t>(field_columns))
            .text("x")
            .decimal(static_cast<std::uint32_t>(field_rows))
            .text(" of a ")
            .decimal(static_cast<std::uint32_t>(board_columns))
            .text("x")
            .decimal(static_cast<std::uint32_t>(board_rows))
            .text(" board; cluts ")
            .decimal(static_cast<std::uint32_t>(cluts_used - cluts_before))
            .text(" more, ")
            .decimal(static_cast<std::uint32_t>(cluts_used))
            .text(" of ")
            .decimal(static_cast<std::uint32_t>(gpu::clut_capacity))
            .text(" used; texture cells unchanged at ")
            .decimal(static_cast<std::uint32_t>(cells_used))
            .flush();
        expect(cluts_used <= gpu::clut_capacity,
               "the_receding_cluts_fit_in_vram");
        expect(surround_cluts_used ==
                   surround_rings * art::terrain_kind_count,
               "every_ring_has_a_clut_for_every_terrain_kind");
        // No new art was needed, which is worth asserting rather than saying:
        // the treated palettes are the shipped ones transformed in place, and
        // every texture cell the surround draws is the board's own.
        expect(cells_used == cells_before,
               "the_surround_uploaded_no_new_texture_cell");
    }

    load_camera(camera_pitch_degrees, camera_distance, camera_focal,
                camera_vertical, Focus{0, board_half_depth});
    {
        static const char* const candidate_names[4] = {
            "surround-none", "surround-clut", "surround-trim", "surround-fog"};
        static const char* const candidate_labels[4] = {
            "none", "receding cluts", "trim ring", "fog rows"};
        for (int candidate = 0; candidate < 4; ++candidate) {
            gpu::begin(0x7C1F);
            const FieldCost cost = measure_field(candidate);
            report_field(candidate_labels[candidate], cost);
            take_still(candidate_names[candidate]);
            expect(cost.total_us < thirty_fps_microseconds,
                   candidate == surround_none
                       ? "the_undressed_field_fits_in_a_thirtieth"
                   : candidate == surround_clut
                       ? "the_receding_clut_surround_fits_in_a_thirtieth"
                   : candidate == surround_trim
                       ? "the_trim_ring_surround_fits_in_a_thirtieth"
                       : "the_fog_surround_fits_in_a_thirtieth");
        }
        expect(last_field_overlays > 0,
               "the_fog_candidate_actually_drew_its_overlays");
    }

    // Determinism, on the candidate that costs nothing per frame.
    {
        gpu::begin(0x7C1F);
        (void)measure_field(surround_clut);
        const std::uint64_t first = hash_framebuffer();
        gpu::begin(0x7C1F);
        (void)measure_field(surround_clut);
        const std::uint64_t second = hash_framebuffer();
        Line().text("HASH surround ").hex64(first).text(" ").hex64(second)
            .flush();
        expect(first == second,
               "two_draws_of_the_surround_hash_the_same_framebuffer");
    }

    // 12a. One short pan per candidate, out from the board centre past its own
    //      edge, so that the moment the camera crosses the boundary can be
    //      judged rather than described. Three films of thirty-two frames
    //      rather than one of ninety-six: they are meant to be compared, and
    //      three things are easier to compare side by side than in sequence.
    {
        constexpr int surround_film_frames = 16;
        static const char* const film_names[3] = {
            "surround-clut", "surround-trim", "surround-fog"};
        static const int film_candidates[3] = {surround_clut, surround_trim,
                                               surround_fog};
        for (int film = 0; film < 3; ++film) {
            announce_film(film_names[film], surround_film_frames);
            for (int frame = 0; frame < surround_film_frames; ++frame) {
                const Focus at{0, static_cast<std::int32_t>(frame) * 2 *
                                      pan_units_per_frame};
                load_camera(camera_pitch_degrees, camera_distance,
                            camera_focal, camera_vertical, at);
                (void)transform_field();
                (void)transform_billboards();
                clear_screen_with(surround_backdrop);
                draw_field(film_candidates[film], true);
                take_shot();
            }
        }
    }

    // -----------------------------------------------------------------------
    // 13. Leave a picture on the screen for a human, and report a heap census
    //     for the same reason every other executable here does.
    // -----------------------------------------------------------------------
    gpu::begin(0x7C1F);
    (void)measure_frame(1, true, true, 1);
    gpu::show();
    expect((gpu::status() & gpu::status_display_disabled) == 0,
           "the_display_is_on_at_the_end");
    const psx::HeapCensus census = psx::heap_census();
    Line()
        .text("HEAP capacity ")
        .decimal(psx::heap_capacity())
        .text(" peak ")
        .decimal(census.peak_allocated_bytes)
        .text(" live ")
        .decimal(census.allocated_bytes)
        .flush();
    expect(census.allocated_bytes == 0, "the_scene_allocates_nothing");

    // The style is on this line as well as on the STYLE line above, because
    // this is the line anybody quotes. `RESULT PASS 147/147` was carried into
    // five commission records as though it said something about the mesh
    // library; it never did. It said something about one style's eight
    // figures, and which style was decided by a file this program does not
    // print. With the style on the line it cannot be quoted without its
    // subject.
    Line()
        .text(failures == 0 ? "RESULT PASS " : "RESULT FAIL ")
        .decimal(static_cast<std::uint32_t>(checks - failures))
        .text("/")
        .decimal(static_cast<std::uint32_t>(checks))
        .text(" style ")
        .text(grandleon_playstation_mesh_style_name)
        .flush();
    if (failures != 0) give_up("checks failed");
    return 0;
}
